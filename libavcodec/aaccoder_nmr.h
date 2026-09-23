/*
 * AAC encoder NMR (noise-to-mask ratio) scalefactor coder
 * Copyright (c) 2026 Lynne <dev@lynne.ee>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * AAC encoder NMR scalefactor coder.
 *
 * Optimizes the same noise-to-mask objective as the two-loop coder, but with an
 * optimal Viterbi search over scalefactors instead of a heuristic loop. For each
 * coded band the per-scalefactor distortion/bits curve is precomputed, then a
 * trellis over the (window-group, band) coding sequence minimizes
 *   sum_g = dist_g(sf_g)/threshold_g +
 *           lambda * (spectral_bits_g(sf_g) + scalefactor_differential_bits)
 * with |sf_g - sf_{g-1}| <= SCALE_MAX_DIFF as a constraint, and lambda
 * binary-searched so the coded size meets the per-frame bit budget
 *
 * Perceptual noise substitution (PNS) is integrated into the same objective: once
 * the trellis settles on its operating lambda, each noise-like band (flagged by
 * mark_pns) is offered a terminal "code as noise" candidate whose cost is
 * nmr_pns + lambda*NMR_PNS_BITS. Because NMR_PNS_BITS is far below a band's spectral bit
 * count, this candidate only wins when lambda is large, i.e. when the encoder is
 * struggling to hold the bitrate. The bits freed by the chosen PNS bands are
 * then re-spent by a second trellis pass over the remaining bands.
 */

#ifndef AVCODEC_AACCODER_NMR_H
#define AVCODEC_AACCODER_NMR_H

#include <float.h>
#include <string.h>
#include "libavutil/mathematics.h"
#include "mathops.h"
#include "avcodec.h"
#include "put_bits.h"
#include "aac.h"
#include "aacenc.h"
#include "aactab.h"
#include "aacenctab.h"

/* differential scalefactor coding cost, clamped to the legal delta range */
#define NMR_SFBITS(d) ff_aac_scalefactor_bits[av_clip((d) + SCALE_DIFF_ZERO, 0, 2*SCALE_MAX_DIFF)]

#define NMR_ITERS  14 /* lambda binary-search iters */
#define NMR_IFINE    9 /* fine-pass lambda iters */
#define NMR_CITERS   7 /* coarse-pass lambda iters */
#define NMR_CWARM    5 /* coarse-pass iters when warm-started off the previous frame's
                        * lambda: the bracket spans 10 octaves instead of ~43, so fewer
                        * bisection steps reach the same resolution */
#define NMR_COARSE   8 /* two-pass coarse->fine grid step, cuts the Viterbi ncand^2 with no
                        * quality loss, 0 disables it (single full-resolution pass) */
#define NMR_STEP     1 /* fine-pass scalefactor candidate granularity */

#define NMR_PNS_BITS 9 /* approx cost in bits of signalling PNS */

/* Spectral-hole fill: noise-like bands the trellis left mostly empty are filled with
 * energy-matched noise (PNS); an audible hole sounds worse than matched noise. */
#define NMR_PNS_HOLE_FRAC   0.5f
#define NMR_PNS_HOLE_SPREAD 0.5f
/* Hole fill applies at any operating lambda above this (comfortable rates
 * included): sparse-line renditions of noise are audible at every rate. */
#define NMR_PNS_HOLE_LAM    20.0f

/* RC servo gain: scale the corridor centre by exp2(-K*fill/R) each frame to hold
 * the long-run mean rate; without it a bad centre drifts for dozens of frames. */
#define NMR_RC_K_CBR 0.5f

#define NMR_RC_ITERS 8 /* lambda bisection iters when clamping an over-cap frame */
/* Corridor: bisect within [lam_rc/NMR_RC_CORR, lam_rc*NMR_RC_CORR] so quality stays
 * smooth while per-frame demand is tracked; 1.5 cuts lambda jitter ~25%. */
#define NMR_RC_CORR   1.5f
/* Reservoir-cap re-solve coarsens at most this far past the corridor per
 * escalation step; the residual overage rides as reservoir debt. */
#define NMR_RC_CAPK   3.0f

/* Quality-target calibration anchor: the nd set-point of -q:a 1, which lands
 * near 128-136 kbps stereo on the tuning corpora with the q ladder's own
 * bandwidth. It moved -2.1 -> -0.75 with the quality-target mask floor
 * (PSY_THRFL_QUALITY), which lowers thr_real and so lifts the whole statistic.
 * The reachable-range clamp is set by the stat's -4/band sub-mask clip, which
 * the mask floor does not touch: targets below it are asymptotically
 * unreachable - the bisect rails at the finest lambda, the frame exceeds the
 * decoder buffer and the outer re-encode loop never converges. */
#define NMR_VBR_ANCHOR (-0.75f)
#define NMR_VBR_TMIN   (-3.8f)

/* VBR shorts: the measured stat inflation counts fully once this share of
 * the frame's energy sits above 6 kHz */
#define NMR_INFL_HF 0.10f
/* Reservoir half-window (bits/ch); swept 512/1536/3072, 1536 optimal. */
#define NMR_CBR_BUF   1536
/* Slew limit on the FINAL operating lambda per frame; bits deviate instead,
 * the reservoir absorbs. See memory: aac-castanets-transient-rc. */
#define NMR_SLEW      1.6f
#define NMR_SLEW_RUN  1.15f /* within short runs */
#define NMR_RC_CITERS 3 /* corridor coarse-pass iters */

/* Transition premask: an attack cannot mask backwards; clamp a START frame's
 * thresholds toward the previous long frame's. */
#define NMR_TRANS_PM 2.0f

/* Zero-decision hysteresis: previously-coded bands need this margin below
 * threshold to zero (marginal bands flicker audibly otherwise). */
#define NMR_ZERO_STICKY 0.5f

/* Short-window groups keep a borderline window coded down to this fraction
 * of its zero threshold rather than muting it inside a coded group. */
#define NMR_GROUP_KEEP 0.25f

/* HF precision taper: thresholds rise by NMR_HF_TAPER dB per kHz^2 above the
 * knee (Apple's measured HF precision rolloff). */
#define NMR_HF_TAPER      0.08f
#define NMR_HF_TAPER_KNEE 8000.0f

/* Transient bit-burst: an isolated onset (preceded by >= NMR_BURST_GAP long frames)
 * is coded NMR_BURST_GAIN x finer, held uniform across the run, repaid from steady stretches. */
#define NMR_BURST_GAP   10
#define NMR_BURST_GAIN  8.0f
/* Dense-beat boost: short runs with gap < NMR_BURST_GAP get a budget factor
 * ramping with the gap (starvation-scaled at the use site). */
#define NMR_SHORT_BOOST 2.0f
#define NMR_RC_FITERS 4 /* corridor fine-pass iters */
#define NMR_RC_TRACK  0.1f /* per-frame pull of the corridor centre toward the realized lambda */

/* PNS noise-distortion gate: only bands coded well above the masking floor become noise. */
#define NMR_PNS_NDGATE 4.0f

/* Energy/threshold cap for PNS: loud bands (energy >> mask) yield clipping random peaks;
 * only near-masked bands are safe substitution targets. */
#define NMR_PNS_MAX_ET 8.0f

/* Operating-lambda floor for PNS: below it the encoder is not struggling, so
 * substituting real texture for 9 signalling bits is net-negative. */
#define NMR_PNS_LAM 100.0f

/* PNS decision hysteresis: enter and leave both cost a margin. */
#define NMR_PNS_ENTER 0.7f
#define NMR_PNS_STAY  1.4f
/* PNS debounce: enter after NMR_PNS_ON consecutive wants, leave after
 * NMR_PNS_OFF (chronically marginal bands never qualify). */
#define NMR_PNS_ON  8
#define NMR_PNS_OFF 4

/**
 * Viterbi over the coding sequence act[0..nact-1] (indices into the per-band
 * curves nd/nb), with lambda binary-searched so the coded size ~ destbits.
 * Fills chosen[band] for every band referenced by act. Returns the operating
 * lambda. node cost = dist/threshold + lambda*spectral_bits;
 * edge cost = lambda*sf_differential_bits; |delta sf| <= SCALE_MAX_DIFF hard.
 */
static float nmr_solve(AACEncContext *s,
                       const float (*nd)[NMR_NCAND], const int (*nb)[NMR_NCAND],
                       const int *blo, const int *bnc, int step,
                       const int *act, int nact, int destbits, int *chosen,
                       float lo_l, float hi_l, int iters)
{
    float dp[NMR_NCAND], dpp[NMR_NCAND], node[NMR_NCAND];
    float lamsf[2*SCALE_MAX_DIFF + 1];   /* lam*sfdiff bit cost, per lambda */
    uint8_t bp[128][NMR_NCAND];
    float lam = 1.0f;

    if (nact <= 0)
        return lam;

    for (int it = 0; it < iters; it++) {
        lam = sqrtf(lo_l * hi_l);
        for (int i = 0; i <= 2*SCALE_MAX_DIFF; i++)
            lamsf[i] = lam * ff_aac_scalefactor_bits[i];   /* edge cost for this lambda */

        int b0 = act[0];
        for (int o = 0; o < bnc[b0]; o++)
            dp[o] = nd[b0][o] + lam * nb[b0][o];   /* anchor band node cost */

        for (int k = 1; k < nact; k++) {
            int b = act[k], pb = act[k-1];
            memcpy(dpp, dp, sizeof(dp));
            for (int o = 0; o < bnc[b]; o++)
                node[o] = nd[b][o] + lam * nb[b][o];
            /* dp[o] = node[o] + min_op(dpp[op] + edge cost) */
            s->aacdsp.nmr_trellis_step(dp, bp[k], dpp, node, lamsf,
                                       bnc[b], bnc[pb], blo[b] - blo[pb], step,
                                       SCALE_MAX_DIFF);
        }

        /* backtrack */
        int beo = 0, b = act[nact-1];
        float bec = FLT_MAX;
        for (int o = 0; o < bnc[b]; o++)
            if (dp[o] < bec) { bec = dp[o]; beo = o; }
        chosen[b] = beo;
        for (int k = nact-1; k > 0; k--)
            chosen[act[k-1]] = bp[k][chosen[act[k]]];

        /* calc cost */
        int total = 0;
        for (int k = 0; k < nact; k++)
            total += nb[act[k]][chosen[act[k]]];
        for (int k = 1; k < nact; k++)
            total += NMR_SFBITS((blo[act[k]]+chosen[act[k]]*step) - (blo[act[k-1]]+chosen[act[k-1]]*step));

        if (it == iters - 1)
            break;

        /* check if we went over budget, go coarser if we did */
        if (total > destbits)
            lo_l = lam;
        else
            hi_l = lam;
    }
    return lam;
}

/* Build one coded band's (dist/threshold, bits) cost curve, candidates sf = lo + o*step
 * for o in [0,maxn), stopping when the band would drop (cb <= 0). Returns the bit count. */
static int nmr_band_curve(AACEncContext *s, SingleChannelElement *sce, int w, int g,
                          int start, int lo, int step, int maxn, float invthr,
                          float maxval, float *nd_row, int *nb_row)
{
    int ncand = 0;
    for (int o = 0; o < maxn && lo + o*step <= SCALE_MAX_POS; o++) {
        int sf = lo + o*step, btot = 0, cb = find_min_book(maxval, sf);
        float dist = 0.0f;
        if (cb <= 0)
            break;
        for (int w2 = 0; w2 < sce->ics.group_len[w]; w2++) {
            int bb;
            dist += quantize_band_cost_cached(s, w + w2, g, sce->coeffs + start + w2*128,
                                              s->scoefs + start + w2*128, sce->ics.swb_sizes[g],
                                              sf, cb, 1.0f, INFINITY, &bb, NULL, 0);
            btot += bb;
        }
        nd_row[ncand] = (dist - btot) * invthr;
        nb_row[ncand] = btot;
        ncand++;
    }
    return ncand;
}

/* Zero a channel with nothing codeable; stale band_types would resurrect
 * bands with chain-illegal scalefactors. */
static void nmr_bail_channel(SingleChannelElement *sce)
{
    for (int i = 0; i < 128; i++) {
        if (sce->band_type[i] == INTENSITY_BT || sce->band_type[i] == INTENSITY_BT2)
            continue;
        sce->zeroes[i]    = 1;
        sce->band_type[i] = 0;
    }
}

/* Per-channel setup into slot t: short-block threshold shaping, the
 * allocation law, zero decisions, and the PASS 1 coarse candidate curves.
 * Returns the coded-band count; 0 = nothing codeable (caller bails). */
static int nmr_setup_channel(AVCodecContext *avctx, AACEncContext *s,
                             SingleChannelElement *sce, NMRSlot *t)
{
    float (*nd)[NMR_NCAND] = s->nmr->nd[t->si];
    int   (*nb)[NMR_NCAND] = s->nmr->nb[t->si];
    const int cstep = NMR_COARSE > 0 ? NMR_COARSE : NMR_STEP;
    int allz = 0, cutoff = 1024, nbnd = 0;

    uint8_t *zprev = s->nmr->zero_prev[s->cur_channel & 15];
    if (s->nmr->zero_nw[s->cur_channel & 15] != sce->ics.num_windows) {
        memset(zprev, 1, 128);
        s->nmr->zero_nw[s->cur_channel & 15] = sce->ics.num_windows;
    }

    t->sce    = sce;
    t->cur_ch = s->cur_channel;
    t->is8    = sce->ics.window_sequence[0] == EIGHT_SHORT_SEQUENCE;
    t->nbnd   = t->nact = 0;

    /* band cutoff index for this frame's window size; the bandwidth is fixed
     * at init and shared with the psy model */
    cutoff = s->bandwidth * 2 * (1024 / sce->ics.num_windows) / avctx->sample_rate;

    /* Short-block shaping: temporal premask + per-window threshold flatten. */
    if (sce->ics.window_sequence[0] == EIGHT_SHORT_SEQUENCE) {
        const float pm_p1 = 0.1f, pm_p2 = 2.0f, pm_p3 = 4.0f;
        for (int g = 0; g < sce->ics.num_swb; g++) {
            float t1 = FLT_MAX, t2 = FLT_MAX;   /* original thr of w-1, w-2 */
            for (int w = 0; w < sce->ics.num_windows; w++) {
                FFPsyBand *b = &s->psy.ch[s->cur_channel].psy_bands[w*16+g];
                float th = b->threshold;
                float c = FFMIN(th, FFMIN(t1*pm_p2, t2*pm_p3));
                b->threshold = FFMAX(c, th*pm_p1);
                t2 = t1; t1 = th;
            }
        }
        {
            for (int w = 0; w < sce->ics.num_windows; w++) {
                float sum = 0.0f, esum = 0.0f; int n = 0;
                for (int g = 0; g < sce->ics.num_swb; g++) {
                    FFPsyBand *b = &s->psy.ch[s->cur_channel].psy_bands[w*16+g];
                    if (b->energy > b->threshold && b->threshold > 0.0f) { sum += b->threshold; esum += b->energy; n++; }
                }
                if (n > 0) {
                    /* keep each window codeable: cap the mean 12dB under the
                     * window's mean audible energy */
                    float mean = FFMIN(sum / n, (esum / n) * expf(-12.0f * (float)M_LN10 / 10.0f));
                    for (int g = 0; g < sce->ics.num_swb; g++) {
                        FFPsyBand *b = &s->psy.ch[s->cur_channel].psy_bands[w*16+g];
                        if (b->energy > b->threshold && b->threshold > 0.0f)
                            b->threshold = FFMIN(mean, b->threshold * 1e9f);
                    }
                }
            }
        }
    }

    /* Allocation law; short frames blend to softer energy exponents under
     * pressure (roll anti-starvation, see memory). */
    /* The Apple-RE constants (0.443/0.111) describe THEIR bit distribution,
     * not this trellis's optimum: more psy-threshold influence is worth -38%
     * Zim / +0.06 ViSQOL at 128k stereo and -15% at 64k, flat plateau beyond. */
    float a_ae = 0.50f, a_at = 0.18f;
    if (sce->ics.num_windows == 8 && s->nmr) {
        /* blend to mask-weighted exponents under rate pressure */
        a_ae += (0.35f - a_ae) * s->nmr->press;
        a_at += (0.3f  - a_at) * s->nmr->press;
    }
    for (int w = 0; w < sce->ics.num_windows; w += sce->ics.group_len[w]) {
        int start = 0;
        for (int g = 0; g < sce->ics.num_swb; start += sce->ics.swb_sizes[g++]) {
            float uplim = 0.0f, ener = 0.0f, spread = 2.0f;
            int nz = 0;
            if (sce->band_type[w*16+g] == INTENSITY_BT ||
                sce->band_type[w*16+g] == INTENSITY_BT2) {
                /* pre-decided intensity band (right channel): keep its
                 * signalling, it is not trellis-coded */
                for (int w2 = 0; w2 < sce->ics.group_len[w]; w2++)
                    sce->zeroes[(w+w2)*16+g] = 0;
                continue;
            }
            float zthr_mul = zprev[w*16+g] ? 1.0f : NMR_ZERO_STICKY;
            /* M/S side bands: zero-reluctance scaled by side/mid ratio (a tiny
             * side IS the image; zeroing it flickers). */
            if ((t->cur_ch & 1) && s->nmr && s->nmr->pair &&
                s->nmr->smode_band[(t->cur_ch >> 1) & 7][w*16+g] == 1) {
                const FFPsyBand *mb = &s->psy.ch[s->cur_channel - 1].psy_bands[w*16+g];
                float ratio = 0.0f;
                float eside = 0.0f;
                for (int w2 = 0; w2 < sce->ics.group_len[w]; w2++) {
                    const FFPsyBand *bb = &s->psy.ch[s->cur_channel].psy_bands[(w+w2)*16+g];
                    eside += bb->energy;
                }
                ratio = eside / FFMAX(mb->energy * sce->ics.group_len[w], 1e-9f);
                zthr_mul *= 0.25f + 0.75f * av_clipf(ratio / 0.3f, 0.0f, 1.0f);
            }
            /* HF precision taper (bitstream RE): at 128k Apple codes
             * 7.5-12.6k ~6dB coarser and 12.6k+ ~28dB coarser than we do,
             * funding their LF/mid SNR edge; a quadratic thr rise above the
             * knee reproduces that rolloff */
            float hftdb;
            {
                float bfreq = start * (avctx->sample_rate * (sce->ics.num_windows == 8 ? 4.0f : 0.5f)) / 1024.0f;
                float k = FFMAX(0.0f, bfreq - NMR_HF_TAPER_KNEE) / 1000.0f;
                hftdb = NMR_HF_TAPER * k * k;
            }
            t->hftx[w*16+g] = 0;
            for (int w2 = 0; w2 < sce->ics.group_len[w]; w2++) {
                FFPsyBand *band = &s->psy.ch[s->cur_channel].psy_bands[(w+w2)*16+g];
                ener   += band->energy;
                spread  = FFMIN(spread, band->spread);
                if (start >= cutoff || band->energy <= band->threshold * zthr_mul ||
                    band->threshold == 0.0f) {
                    sce->zeroes[(w+w2)*16+g] = 1;
                    continue;
                }
                uplim += band->threshold;
                nz = 1;
            }
            if (nz && sce->ics.group_len[w] > 1) {
                /* a coded group must not mute individual borderline windows:
                 * they share the group's scalefactor (near-free to keep) and
                 * a 3-6ms in-band mute right before an attack reads as a
                 * gated crunch on every beat */
                for (int w2 = 0; w2 < sce->ics.group_len[w]; w2++) {
                    FFPsyBand *band = &s->psy.ch[s->cur_channel].psy_bands[(w+w2)*16+g];
                    if (sce->zeroes[(w+w2)*16+g] && start < cutoff &&
                        band->threshold > 0.0f &&
                        band->energy > band->threshold * zthr_mul * NMR_GROUP_KEEP) {
                        sce->zeroes[(w+w2)*16+g] = 0;
                        uplim += band->threshold;
                    }
                }
            }
            zprev[w*16+g] = !nz;
            sce->zeroes[w*16+g] = !nz;
            t->thr_real[w*16+g] = uplim;    /* real mask, before the allocation law (PNS gate) */
            if (nz && ener > 0.0f && uplim > 0.0f) { /* allocation law */
                uplim = expf(a_ae * logf(ener) + a_at * logf(uplim));
                if (hftdb != 0.0f) {
                    uplim *= powf(10.0f, hftdb * 0.1f);
                    /* a band pushed well past its real mask reads as huge
                     * achieved-nd; keep the deliberate deficit out of the
                     * psy-reliability stat or press ramps into noise-class
                     * RC on clean tonal content */
                    if (hftdb > 2.0f)
                        t->hftx[w*16+g] = 1;
                }
            }
            t->thr[w*16+g]     = uplim;
            t->pener[w*16+g]   = ener;
            t->pspread[w*16+g] = spread;
            allz |= nz;
        }
    }
    if (!allz)
        return 0;

    /* transition premask (see NMR_TRANS_PM) */
    if (sce->ics.num_windows == 1) {
        int ci = t->cur_ch & 15;
        if (sce->ics.window_sequence[0] == LONG_START_SEQUENCE &&
            s->nmr->thr_prev_ok[ci]) {
            for (int g = 0; g < sce->ics.num_swb && g < 64; g++)
                if (t->thr[g] > 0.0f && s->nmr->thr_prev[ci][g] > 0.0f)
                    t->thr[g] = FFMIN(t->thr[g], s->nmr->thr_prev[ci][g] * NMR_TRANS_PM);
        }
        for (int g = 0; g < sce->ics.num_swb && g < 64; g++)
            s->nmr->thr_prev[ci][g] = t->thr[g];
        s->nmr->thr_prev_ok[ci] = 1;
    } else {
        s->nmr->thr_prev_ok[t->cur_ch & 15] = 0;
    }

    s->aacdsp.abs_pow34(s->scoefs, sce->coeffs, 1024);
    ff_quantize_band_cost_cache_init(s);

    /* TNS synthesis gain per band: the decoder re-amplifies residual-domain
     * quantization noise by the whitening gain (shorts only). */
    for (int i = 0; i < 128; i++)
        t->tnsg[i] = 1.0f;
    if (sce->ics.num_windows == 8 && sce->tns.present) {
        const int mmm2 = FFMIN(sce->ics.tns_max_bands, sce->ics.max_sfb ? sce->ics.max_sfb : sce->ics.num_swb);
        for (int w = 0; w < 8; w++) {
            int bottom2 = sce->ics.num_swb;
            for (int filt = 0; filt < sce->tns.n_filt[w]; filt++) {
                int top2 = bottom2;
                bottom2 = FFMAX(0, top2 - sce->tns.length[w][filt]);
                if (!sce->tns.order[w][filt])
                    continue;
                for (int g = FFMIN(bottom2, mmm2); g < FFMIN(top2, mmm2); g++) {
                    int s0 = sce->ics.swb_offset[g] + w*128;
                    int s1 = sce->ics.swb_offset[g+1] + w*128;
                    float eres = 0.0f;
                    const FFPsyBand *pb = &s->psy.ch[s->cur_channel].psy_bands[w*16+g];
                    for (int k = s0; k < s1; k++)
                        eres += sce->coeffs[k]*sce->coeffs[k];
                    t->tnsg[w*16+g] = av_clipf(pb->energy / FFMAX(eres, 1e-12f), 1.0f, 64.0f);
                }
            }
        }
    }

    /* finest codeable scalefactor and max value per band */
    for (int w = 0; w < sce->ics.num_windows; w += sce->ics.group_len[w]) {
        int start = w*128;
        for (int g = 0; g < sce->ics.num_swb; g++) {
            t->maxvals[w*16+g] = find_max_val(sce->ics.group_len[w], sce->ics.swb_sizes[g], s->scoefs + start);
            t->minsf[w*16+g]   = t->maxvals[w*16+g] > 0 ? coef2minsf(t->maxvals[w*16+g]) : 0;
            start += sce->ics.swb_sizes[g];
        }
    }

    /* PASS 1: coarse candidate curves per coded band
     * (the lambda search runs on this cheap grid, PASS 2 refines the winner) */
    {
        for (int w = 0; w < sce->ics.num_windows; w += sce->ics.group_len[w]) {
            int start = w*128;
            for (int g = 0; g < sce->ics.num_swb; g++) {
                if (!sce->zeroes[w*16+g] && t->maxvals[w*16+g] > 0 && nbnd < 128) {
                    int lo = av_clip(t->minsf[w*16+g], 0, SCALE_MAX_POS);
                    float invthr = 1.0f / FFMAX(t->thr[w*16+g], 1e-9f);
                    int ncand = nmr_band_curve(s, sce, w, g, start, lo, cstep, NMR_NCAND,
                                               invthr, t->maxvals[w*16+g], nd[nbnd], nb[nbnd]);
                    if (t->tnsg[w*16+g] > 1.0f)
                        for (int o = 0; o < ncand; o++)
                            nd[nbnd][o] *= t->tnsg[w*16+g];
                    if (ncand == 0) {
                        /* nothing codeable: drop the group band incl. subwindow
                         * flags (group flag is re-derived by ANDing) */
                        for (int w2 = 0; w2 < sce->ics.group_len[w]; w2++)
                            sce->zeroes[(w+w2)*16+g] = 1;
                    } else {
                        t->bidx[nbnd] = w*16+g;
                        t->bw[nbnd] = w;
                        t->bg[nbnd] = g;
                        t->bst[nbnd] = start;
                        t->blo[nbnd] = lo;
                        t->bnc[nbnd] = ncand;
                        nbnd++;
                    }
                }
                start += sce->ics.swb_sizes[g];
            }
        }
    }
    t->nbnd = nbnd;
    for (int b = 0; b < nbnd; b++) {
        t->act[b]    = b;
        t->is_pns[b] = 0;
    }
    t->nact = nbnd;
    return nbnd;
}

/* total bits of a slot's current chosen[] on grid `step`, incl. sf deltas */
static int nmr_slot_bits(const NMRSlot *t, const int (*nb)[NMR_NCAND], int step)
{
    int tot = 0;
    for (int k = 0; k < t->nact; k++)
        tot += nb[t->act[k]][t->chosen[t->act[k]]];
    for (int k = 1; k < t->nact; k++)
        tot += NMR_SFBITS((t->blo[t->act[k]]+t->chosen[t->act[k]]*step) -
                          (t->blo[t->act[k-1]]+t->chosen[t->act[k-1]]*step));
    return tot;
}

/* Run every slot's trellis at one fixed lambda; returns the pooled bits. */
static int nmr_eval_slots(AACEncContext *s, NMRSlot *const *sl, int nsl, int step, float lam)
{
    int total = 0;
    for (int k = 0; k < nsl; k++) {
        NMRSlot *t = sl[k];
        if (!t->nact)
            continue;
        nmr_solve(s, s->nmr->nd[t->si], s->nmr->nb[t->si], t->blo, t->bnc, step,
                  t->act, t->nact, 0, t->chosen, lam, lam, 1);
        total += nmr_slot_bits(t, s->nmr->nb[t->si], step);
    }
    return total;
}

/* Bisect ONE shared lambda across the slots so the POOLED bits meet destbits.
 * This is the CPE budget pool: bits flow to whichever channel of the pair has
 * demand at the common operating point, instead of an equal per-channel split. */
static float nmr_solve_slots(AACEncContext *s, NMRSlot *const *sl, int nsl, int step,
                             int destbits, float lo_l, float hi_l, int iters)
{
    float lam = 1.0f;
    for (int it = 0; it < iters; it++) {
        lam = sqrtf(lo_l * hi_l);
        int total = nmr_eval_slots(s, sl, nsl, step, lam);
        if (it == iters - 1)
            break;
        /* over budget -> go coarser */
        if (total > destbits)
            lo_l = lam;
        else
            hi_l = lam;
    }
    return lam;
}

/* Mean log2 achieved dist/real-mask over the active bands of all slots at
 * their current chosen[] - average dB-distance from the mask (Brandenburg
 * mean-NMR). Log compression keeps unavoidably-loud bands from swamping the
 * statistic. NAN when nothing is coded. */
static float nmr_nd_stat(AACEncContext *s, NMRSlot *const *sl, int nsl)
{
    float ndsum = 0.0f;
    int n = 0;
    for (int k = 0; k < nsl; k++) {
        NMRSlot *t = sl[k];
        const float (*ndk)[NMR_NCAND] = (const float (*)[NMR_NCAND])s->nmr->nd[t->si];
        for (int b_ = 0; b_ < t->nact; b_++) {
            int b = t->act[b_], bi = t->bidx[b];
            if (t->thr_real[bi] > 0.0f && t->thr[bi] > 0.0f) {
                /* floor the sub-mask credit: a flat sf-chain over-codes
                 * quiet bands and their unbounded negative log2 would let a
                 * COARSER lambda read as a finer statistic */
                ndsum += FFMAX(log2f(FFMAX(ndk[b][t->chosen[b]] * t->thr[bi] /
                                           t->thr_real[bi], 1e-6f)), -4.0f);
                n++;
            }
        }
    }
    return n ? ndsum / n : NAN;
}

/* Bisect ONE shared lambda across the slots so the achieved noise-to-mask
 * statistic meets the target: quality-domain VBR. Monotone: coarser lambda ->
 * more distortion. */
static float nmr_solve_slots_nd(AACEncContext *s, NMRSlot *const *sl, int nsl,
                                int step, float target, float lo_l, float hi_l,
                                int iters)
{
    float lam = 1.0f;
    for (int it = 0; it < iters; it++) {
        float st;
        lam = sqrtf(lo_l * hi_l);
        nmr_eval_slots(s, sl, nsl, step, lam);
        st = nmr_nd_stat(s, sl, nsl);
        if (isnan(st) || it == iters - 1)
            break;
        if (st > target)
            hi_l = lam;
        else
            lo_l = lam;
    }
    return lam;
}

/* Write a solved slot back into its channel: band types, scalefactors, and the
 * SCALE_MAX_DIFF legality fixups. Verbatim from the pre-pool single-channel tail. */
static void nmr_commit_channel(AACEncContext *s, NMRSlot *t)
{
    SingleChannelElement *sce = t->sce;
    const int (*nb)[NMR_NCAND] = (const int (*)[NMR_NCAND])s->nmr->nb[t->si];

    for (int b = 0; b < t->nbnd; b++) {
        int bi = t->bidx[b];
        if (t->is_pns[b]) {
            sce->band_type[bi] = NOISE_BT;
            sce->zeroes[bi]    = 0;
            sce->pns_ener[bi]  = t->pener[bi] * FFMIN(1.0f, t->pspread[bi]*t->pspread[bi]);
        } else {
            sce->sf_idx[bi] = av_clip(t->blo[b] + t->chosen[b]*NMR_STEP, 0, SCALE_MAX_POS);
        }
    }


    /* record the bits this solve accounted for; the encoder compares them
     * against the channel's real output to keep the budget honest. Only the
     * surviving active bands are coded: bands shed for legality are not. */
    s->nmr->counted[t->cur_ch] = nmr_slot_bits(t, nb, NMR_STEP);

    /* SCALE_MAX_DIFF condition:
     * re-clamp, codebook fixup, drop uncodeable, set global gain
     * NOISE_BT bands keep their own scalefactor chain via set_special_band_scalefactors) */
    {
        uint8_t nextband[128];
        int prev = -1;
        ff_init_nextband_map(sce, nextband);
        for (int w = 0; w < sce->ics.num_windows; w += sce->ics.group_len[w]) {
            for (int g = 0; g < sce->ics.num_swb; g++) {
                if (sce->band_type[w*16+g] == NOISE_BT ||
                    sce->band_type[w*16+g] == INTENSITY_BT ||
                    sce->band_type[w*16+g] == INTENSITY_BT2)
                    continue;
                if (sce->zeroes[w*16+g]) {
                    sce->band_type[w*16+g] = 0;
                    continue;
                }

                if (prev != -1)
                    sce->sf_idx[w*16+g] = av_clip(sce->sf_idx[w*16+g], prev - SCALE_MAX_DIFF, prev + SCALE_MAX_DIFF);
                sce->band_type[w*16+g] = find_min_book(t->maxvals[w*16+g], sce->sf_idx[w*16+g]);
                if (sce->band_type[w*16+g] <= 0) {
                    if (!ff_sfdelta_can_remove_band(sce, nextband, prev, w*16+g)) {
                        sce->band_type[w*16+g] = 1;
                    } else {
                        /* drop subwindow flags too, see the PASS 1 drop above */
                        for (int w2 = 0; w2 < sce->ics.group_len[w]; w2++)
                            sce->zeroes[(w+w2)*16+g] = 1;
                        sce->band_type[w*16+g] = 0;
                        continue;
                    }
                }
                if (prev == -1)
                    sce->sf_idx[0] = sce->sf_idx[w*16+g];   /* global gain */
                prev = sce->sf_idx[w*16+g];
            }
        }

        /* every band must carry a chain-legal scalefactor (re-clamp, codebook
     * fixup, global gain) */
        if (prev != -1) {
            int last = sce->sf_idx[0];
            for (int w = 0; w < sce->ics.num_windows; w += sce->ics.group_len[w]) {
                for (int g = 0; g < sce->ics.num_swb; g++) {
                    if (!sce->zeroes[w*16+g] && sce->band_type[w*16+g] != NOISE_BT &&
                        sce->band_type[w*16+g] < RESERVED_BT)
                        last = sce->sf_idx[w*16+g];
                    else if (sce->band_type[w*16+g] < RESERVED_BT && (w*16+g) > 0)
                        sce->sf_idx[w*16+g] = last;
                }
            }
        }
    }
}

/* Solve one element group (a solo channel, or a CPE pair pooled under one
 * shared lambda and one pooled budget), then PNS and commit. */
static void nmr_solve_group(AVCodecContext *avctx, AACEncContext *s,
                            const float lambda, NMRSlot *const *sl, int nsl,
                            int chans, int rc_eligible, int rc_global,
                            int rc_rate_frame, int rc_bmax)
{
    const int cstep = NMR_COARSE > 0 ? NMR_COARSE : NMR_STEP;
    int bch = ((avctx->flags & AV_CODEC_FLAG_QSCALE) ? 2.0f : avctx->ch_layout.nb_channels);
    int destbits = avctx->bit_rate * 1024.0 / avctx->sample_rate / bch * (lambda / 120.f) * chans;
    int is8_any = 0;
    float lam;
    float rc_off = 1.0f, lam_dem = 0.0f;
    /* the outer loop's starting lambda: legality caps scale with lambda
     * relative to it, so they only shrink on a hard-overflow retry */
    const float lam_ref = (avctx->flags & AV_CODEC_FLAG_QSCALE) && avctx->global_quality > 0 ?
                          avctx->global_quality : 120.0f;

    for (int k = 0; k < nsl; k++)
        is8_any |= sl[k]->is8;

    /* decoupled solo solves keep per-channel slew state: sharing one
     * lam_slew clamps each channel against the OTHER's operating lambda */
    float *vslew_st = &s->nmr->lam_slew;
    if (nsl == 1 && s->channels > 1) {
        vslew_st = &s->nmr->lam_slew_ch[sl[0]->cur_ch & 15];
        if (*vslew_st <= 0.0f)
            *vslew_st = s->nmr->lam_slew;
    }

    if (s->psy.bitres.alloc >= 0)
        destbits = s->psy.bitres.alloc *
                   (lambda / (avctx->global_quality ? avctx->global_quality : 120)) * chans;
    if (rc_global && s->psy.bitres.alloc >= 0) {
        /* CBR target: nominal + repayment, bounded +-30%/frame */
        double rr = avctx->bit_rate * 1024.0 / avctx->sample_rate;
        destbits = (rr + av_clipd(s->nmr->rc_fill / 2.0, -0.3 * rr, 0.3 * rr)) * chans / s->channels;
    } else if (rc_eligible && s->psy.bitres.alloc >= 0) {
        /* pre-bootstrap CBR frames: target nominal (psy bitres is cold) */
        destbits = (avctx->bit_rate * 1024.0 / avctx->sample_rate) * chans / s->channels;
    }
    destbits = FFMIN(destbits, 5800 * chans);
    /* honest budget: subtract the measured non-trellis overhead (section data, ICS,
     * sf/PNS signalling), which is rate-dependent hence adaptive. */
    if (s->nmr->side_inited)
        destbits = av_clip(destbits - (int)(s->nmr->side_ema * chans / s->channels), 64, 5800 * chans);

    /* Held transient burst, bank-aware: spend banked bits, never borrow deep
     * (payback troughs starve the next transient). */
    if (s->nmr->run_burst > 1.0f) {
        int extra = destbits * (s->nmr->run_burst - 1.0f);
        int avail = FFMAX(0, (int)((s->nmr->rc_fill + rc_bmax / 2) * (int64_t)chans / s->channels));
        destbits = av_clip(destbits + FFMIN(extra, avail), 64, 6800 * chans);
    }

    int vbr = (avctx->flags & AV_CODEC_FLAG_QSCALE) && s->nmr;
    int vbr_subst = 1;
    float vbr_t = 0.0f;
    if (vbr) {
        /* nd-target VBR: constant achieved noise-to-mask, bits float. */
        /* target in log2(dist/real-mask), anchored so -q:a 1 lands near
         * 128 kbps stereo on the tuning corpus; higher q is finer, each q
         * doubling ~ -1.2 */
        vbr_t = FFMAX(NMR_VBR_ANCHOR - 1.2f * log2f(avctx->global_quality > 0 ?
                                            avctx->global_quality / (float)FF_QP2LAMBDA : 1.0f),
                      NMR_VBR_TMIN);
    }
    if (vbr && is8_any) {
        /* short frames: the grouped-band statistic is inflated (same reason
         * nd_ema excludes shorts), so they bisect against an offset target
         * rather than the long-frame one - and never write solver state
         * (transient-dense content would otherwise starve the long-frame
         * warm start of updates). Warm off the surrounding operating point. */
        float lam0 = *vslew_st > 0.0f ? FFMIN(*vslew_st, 1e4f) : 0.0f;
        float *infl = &s->nmr->vbr_infl[sl[0]->cur_ch & 15];
        if (lam0 > 0.0f) {
            /* measured stat offset, decided at shorts-run entry: evaluate
             * the grouped stat at the long-anchor lambda - continuity says
             * true quality is on target there, so any excess IS this
             * content's inflation. Broadband beats (velvet) measure +2..+4,
             * tonal musical decays measure none; the old fixed +3 picked
             * one class and audibly damaged the other. Re-measured every
             * short frame: run-entry-only attribution tars a whole tonal
             * decay run with its mini-attack entry frame. */
            float st0, ehf = 0.0f, etot = 0.0f, hfw;
            nmr_eval_slots(s, sl, nsl, cstep, lam0);
            st0 = nmr_nd_stat(s, sl, nsl);
            /* the stat reads inflated on BOTH classes; audibility does not.
             * Coarse shorts hide under HF-dominant transients (hats, claps)
             * and stick out on LF/mid tonal material - weight the offset by
             * the frame's HF energy share. */
            for (int k = 0; k < nsl; k++)
                for (int b_ = 0; b_ < sl[k]->nact; b_++) {
                    int b = sl[k]->act[b_], bi = sl[k]->bidx[b];
                    float en = sl[k]->pener[bi];
                    int bin = sl[k]->bst[b] - sl[k]->bw[b]*128;
                    etot += en;
                    if (bin * (avctx->sample_rate * 4.0f) / 1024.0f > 6000.0f)
                        ehf += en;
                }
            hfw = etot > 0.0f ? av_clipf(ehf / (etot * NMR_INFL_HF), 0.0f, 1.0f) : 1.0f;
            *infl = isnan(st0) ? 0.0f : av_clipf(st0 - vbr_t, 0.0f, 4.0f) * hfw;
        }
        if (lam0 > 0.0f)
            lam = nmr_solve_slots_nd(s, sl, nsl, cstep, vbr_t + *infl,
                                     lam0/32.0f, FFMIN(lam0*32.0f, 1e4f), NMR_CWARM + 2);
        else
            lam = nmr_solve_slots_nd(s, sl, nsl, cstep, vbr_t, 1e-9f, 1e4f, NMR_ITERS);
        if (lam0 > 0.0f) {
            /* the offset-bisect assumes isolated transients between long
             * anchors; on continuous-shorts content the per-frame grouped
             * stat is bisect noise and lambda careens across decades -
             * audible as whooshing. Hold shorts to the same near-constant
             * run slew CBR uses (dives toward finer stay free: transient
             * bits float by design). */
            float kup = s->nmr->prev_was_short ? NMR_SLEW_RUN : NMR_SLEW;
            if (lam > lam0 * kup || lam < lam0 / 4.0f) {
                lam = av_clipf(lam, lam0 / 4.0f, lam0 * kup);
                nmr_eval_slots(s, sl, nsl, cstep, lam);
            }
        }
        vbr_subst = 0;
    } else if (vbr) {
        /* warm-started off the previous frame's lambda */
        float lam0 = s->nmr->lam[sl[0]->cur_ch];
        lam = 1.0f;
        if (lam0 > 0.0f) {
            lam0 = FFMIN(lam0, 1e4f);
            lam = nmr_solve_slots_nd(s, sl, nsl, cstep, vbr_t,
                                     lam0/32.0f, FFMIN(lam0*32.0f, 1e4f), NMR_CWARM + 2);
            if (lam < lam0/16.0f || lam > lam0*16.0f)
                lam0 = 0.0f;
        }
        if (lam0 <= 0.0f)
            lam = nmr_solve_slots_nd(s, sl, nsl, cstep, vbr_t, 1e-9f, 1e4f, NMR_ITERS);
    } else if (rc_global) {
        /* corridor bisect around the servoed centre; pressure = stateless
         * rc_off multiplier (folding it into lam_rc winds up) */
        float R = avctx->bit_rate * 1024.0 / avctx->sample_rate;
        float cen;
        int tot, hardcap, rc_cap;
        float lo;
        rc_off = exp2f(-NMR_RC_K_CBR * s->nmr->rc_fill / R);
        cen    = s->nmr->lam_rc * rc_off;
        lo     = cen / NMR_RC_CORR;
        /* transient burst: widen the lower bound so the boosted destbits can
         * actually pour into the onset frame */
        if (is8_any && s->nmr->run_burst > 1.0f)
            lo /= s->nmr->run_burst;
        lam = nmr_solve_slots(s, sl, nsl, cstep, destbits,
                              lo, cen * NMR_RC_CORR, NMR_RC_CITERS);

        tot = 0;
        for (int k = 0; k < nsl; k++)
            tot += nmr_slot_bits(sl[k], s->nmr->nb[sl[k]->si], cstep);
        hardcap = av_clip((int)(5800.f * FFMIN(1.f, lambda / 120.f)), 256, 5800) * chans;
        /* legality cap only; no spend-floor (rc_off spends the bank) */
        rc_cap   = FFMIN(hardcap, (s->nmr->rc_fill + rc_rate_frame + rc_bmax) * chans / s->channels);
        if (tot > rc_cap) {
            /* reservoir-empty: coarsen, but never past a bounded excursion
             * of the corridor. The coarse grid's bit curve is steppy: an
             * unbounded fit here can jump lambda by orders of magnitude and
             * potato-frame the burst recovery frame (audible LF scratch).
             * The fine pass works from the bounded lambda, and the reservoir
             * cap is enforced once more on the fine grid after it, where the
             * fit only moves lambda as far as the frame really needs. */
            lam = nmr_solve_slots(s, sl, nsl, cstep, rc_cap, lam, lam * NMR_RC_CAPK, NMR_CITERS);
            tot = 0;
            for (int k = 0; k < nsl; k++)
                tot += nmr_slot_bits(sl[k], s->nmr->nb[sl[k]->si], cstep);
            if (tot > rc_cap && s->nmr->rc_fill <= -(rc_bmax * 9 / 10)) {
                /* the debt ledger clips at -rc_bmax: with no capacity
                 * left, further overage would be silently forgiven and
                 * sustained transient content rides hot forever (a
                 * castanets roll hit 215 kbps for its first second on a
                 * 128k ask). A one-frame hard fit trades that for a
                 * potato frame (audible click); instead the bound
                 * ESCALATES with consecutive saturated frames - chronic
                 * rolls converge within a few frames, isolated bursts
                 * never see more than one escalation step. */
                float ek = NMR_RC_CAPK * (1 + FFMIN(s->nmr->rc_satrun, 8));
                lam = nmr_solve_slots(s, sl, nsl, cstep, rc_cap, lam, lam * ek, NMR_CITERS);
                s->nmr->rc_sat_frame = 1;
                tot = 0;
                for (int k = 0; k < nsl; k++)
                    tot += nmr_slot_bits(sl[k], s->nmr->nb[sl[k]->si], cstep);
            }
            if (tot > hardcap)   /* decoder-buffer legality is absolute */
                lam = nmr_solve_slots(s, sl, nsl, cstep, hardcap, lam, 1e4f, NMR_CITERS);
        }
    } else {
        /* per-frame bisection, warm-started off the previous frame's lambda;
         * a result at the bracket edge means redo the full search */
        float lam0 = s->nmr->lam[sl[0]->cur_ch];
        lam = 1.0f;
        if (NMR_COARSE > 0 && lam0 > 0.0f) {
            lam = nmr_solve_slots(s, sl, nsl, cstep, destbits, lam0/32.0f, lam0*32.0f, NMR_CWARM);
            if (lam < lam0/16.0f || lam > lam0*16.0f)
                lam0 = 0.0f;
        }
        if (lam0 <= 0.0f)
            lam = nmr_solve_slots(s, sl, nsl, cstep, destbits,
                                  1e-9f, 1e4f, NMR_COARSE > 0 ? NMR_CITERS : NMR_ITERS);
    }

    /* PASS 2:
     * refine each band at full granularity (NMR_STEP) in a +/-cstep window
     * around the coarse pick, then re-solve. Recovers single-pass quality while the
     * lambda search stayed cheap on the coarse grid. */
    if (NMR_COARSE > 0) {
        /* nmr_speed, 0 = slowest/best, higher = faster; see the option docs. */
        int win = NMR_COARSE - av_clip(s->options.nmr_speed, 0, 4);
        for (int k = 0; k < nsl; k++) {
            NMRSlot *t = sl[k];
            float (*ndk)[NMR_NCAND] = s->nmr->nd[t->si];
            int   (*nbk)[NMR_NCAND] = s->nmr->nb[t->si];
            if (!t->nact)
                continue;
            /* the pow34 spectrum and the quantize cache are per-channel state */
            s->aacdsp.abs_pow34(s->scoefs, t->sce->coeffs, 1024);
            ff_quantize_band_cost_cache_init(s);
            for (int b = 0; b < t->nbnd; b++) {
                int center = t->blo[b] + t->chosen[b]*cstep;
                int flo    = av_clip(center - win, av_clip(t->minsf[t->bidx[b]], 0, SCALE_MAX_POS), SCALE_MAX_POS);
                int maxn   = FFMIN(NMR_NCAND, 2*win/NMR_STEP + 1);
                float invthr = 1.0f / FFMAX(t->thr[t->bidx[b]], 1e-9f);
                int ncand  = nmr_band_curve(s, t->sce, t->bw[b], t->bg[b], t->bst[b], flo, NMR_STEP, maxn,
                                            invthr, t->maxvals[t->bidx[b]], ndk[b], nbk[b]);
                if (t->tnsg[t->bidx[b]] > 1.0f)
                    for (int o = 0; o < ncand; o++)
                        ndk[b][o] *= t->tnsg[t->bidx[b]];
                t->blo[b] = flo;
                t->bnc[b] = FFMAX(1, ncand);
            }
        }
        /* fine pass: narrow corridor around the coarse solve */
        if (vbr && !vbr_subst) {
            float infl2 = is8_any ? s->nmr->vbr_infl[sl[0]->cur_ch & 15] : 0.0f;
            lam = nmr_solve_slots_nd(s, sl, nsl, NMR_STEP, vbr_t + infl2,
                                     lam/32.0f, FFMIN(lam*32.0f, 1e4f), NMR_IFINE);
        } else if (vbr)
            /* wide re-bisect: the fine grid sits systematically finer than
             * the coarse grid at equal lambda, well past a 1-octave bracket */
            lam = nmr_solve_slots_nd(s, sl, nsl, NMR_STEP, vbr_t,
                                     lam/32.0f, FFMIN(lam*32.0f, 1e4f), NMR_IFINE);
        else if (rc_global)
            lam = nmr_solve_slots(s, sl, nsl, NMR_STEP, destbits, lam/2.0f, lam*2.0f, NMR_RC_FITERS);
        else
            lam = nmr_solve_slots(s, sl, nsl, NMR_STEP, destbits, lam/16.0f, lam*16.0f, NMR_IFINE);
    }

    lam_dem = lam;   /* demand-solved lambda, pre bucket clamp: what content wants */

    if (vbr) {
        /* quality slew: lambda moves smoothly, bits follow content (the same
         * anti-flutter rule the CBR path uses; unconstrained per-frame lambda
         * reads as framerate-rate quality modulation). Low-content frames
         * (silence, stop-gaps) rail lambda high legitimately - they must
         * neither be clamped nor write back solver state, or every gap
         * poisons the slew/warm-start and the solve re-traps at the rail. */
        int subst = 0;
        float st_fin = nmr_nd_stat(s, sl, nsl);
        for (int k = 0; k < nsl; k++)
            subst += sl[k]->nact;
        /* a frame counts as an operating point only if it bisected the
         * statistic (not a short frame holding lambda) and the solve
         * actually REACHED the target: cheap frames (room tone, gaps) sit
         * far below it at any lambda and rail high while costing nothing */
        /* two-sided: a frame railed COARSE (st far above target, e.g. the
         * first content frames of a quiet channel bisecting from nothing)
         * is no more an operating point than a railed-fine cheap frame -
         * writing its rail lambda builds a slew prison the channel then
         * climbs out of one rung per frame, muffling the stream head */
        subst = vbr_subst && subst >= 8 && !isnan(st_fin) &&
                st_fin > vbr_t - 0.5f && st_fin < vbr_t + 0.5f;
        if (subst) {
            if (*vslew_st > 0.0f) {
                /* only bisected LONG frames land here (shorts run free); on
                 * steady-tonal content per-frame lambda jitter reads as
                 * framerate-rate noise modulation (see NMR_RC_CORR) */
                if (lam > *vslew_st * NMR_SLEW || lam < *vslew_st / NMR_SLEW) {
                    lam = av_clipf(lam, *vslew_st / NMR_SLEW, *vslew_st * NMR_SLEW);
                    nmr_eval_slots(s, sl, nsl, NMR_STEP, lam);
                }
            }
            *vslew_st = s->nmr->lam_slew = lam;
        } else if (is8_any && *vslew_st > 0.0f) {
            /* short frames: PASS 2 re-bisects the inflated grouped stat on
             * a fresh +-32x bracket, undoing any earlier bound - the clamp
             * must sit here, after the last solve (love.flac whoosh: lambda
             * railed at exactly lam*32 through an all-shorts passage) */
            float kup  = s->nmr->prev_was_short ? NMR_SLEW_RUN : NMR_SLEW;
            float lam0 = FFMIN(*vslew_st, 1e4f);
            if (lam > lam0 * kup || lam < lam0 / 4.0f) {
                lam = av_clipf(lam, lam0 / 4.0f, lam0 * kup);
                nmr_eval_slots(s, sl, nsl, NMR_STEP, lam);
            }
        }
        vbr_subst = subst;
    }
    if (vbr) {
        /* legality only: a frame may never exceed the bit reservoir bound.
         * Side bits ride on top of the trellis count; a negative side
         * estimate must never enlarge the cap. The cap is the format limit
         * at the reference lambda (the -q:a quality itself for VBR) and
         * only shrinks on an outer hard-overflow retry, so the retry
         * converges. */
        int hardcap = av_clip((int)(5800.f * FFMIN(1.f, lambda / lam_ref)), 256, 5800) * chans;
        int tot = 0;
        if (s->nmr->side_inited)
            hardcap = FFMAX(hardcap - (int)(FFMAX(s->nmr->side_ema, 0.0f) * chans / s->channels), 256);
        for (int k = 0; k < nsl; k++)
            tot += nmr_slot_bits(sl[k], s->nmr->nb[sl[k]->si], NMR_STEP);
        if (tot > hardcap) {
            lam = nmr_solve_slots(s, sl, nsl, NMR_STEP, hardcap, lam, 1e4f, NMR_RC_ITERS);
            tot = 0;
            for (int k = 0; k < nsl; k++)
                tot += nmr_slot_bits(sl[k], s->nmr->nb[sl[k]->si], NMR_STEP);
            /* dense short frames at high rates can exceed the decoder
             * buffer even at the coarsest candidate grid; an illegal frame
             * traps the outer re-encode loop forever. Shed the highest
             * bands until the frame fits. act[] runs window group by window
             * group, so pick the highest band across all groups; the first
             * band anchors the scalefactor chain and stays. */
            while (tot > hardcap) {
                int dropped = 0;
                for (int k = 0; k < nsl; k++) {
                    NMRSlot *t = sl[k];
                    SingleChannelElement *sce = t->sce;
                    int hi = 1, b, w0, g;
                    if (t->nact <= 1)
                        continue;
                    for (int b_ = 2; b_ < t->nact; b_++)
                        if (t->bg[t->act[b_]] >= t->bg[t->act[hi]])
                            hi = b_;
                    b  = t->act[hi];
                    w0 = t->bw[b]; g = t->bg[b];
                    for (int w2 = 0; w2 < sce->ics.group_len[w0]; w2++)
                        sce->zeroes[(w0+w2)*16+g] = 1;
                    memmove(&t->act[hi], &t->act[hi + 1], (t->nact - hi - 1) * sizeof(t->act[0]));
                    t->nact--;
                    dropped = 1;
                }
                if (!dropped)
                    break;
                tot = nmr_eval_slots(s, sl, nsl, NMR_STEP, lam);
            }
        }
    }

    if (rc_global) {
        /* reservoir cap on the fine grid (see the coarse-pass bound above),
         * then the quality slew limiter */
        int hardcap = av_clip((int)(5800.f * FFMIN(1.f, lambda / 120.f)), 256, 5800) * chans;
        int tot = 0, rc_cap;
        for (int k = 0; k < nsl; k++)
            tot += nmr_slot_bits(sl[k], s->nmr->nb[sl[k]->si], NMR_STEP);
        rc_cap   = FFMIN(hardcap, (s->nmr->rc_fill + rc_rate_frame + rc_bmax) * chans / s->channels);
        if (tot > rc_cap) {
            lam = nmr_solve_slots(s, sl, nsl, NMR_STEP, rc_cap, lam, 1e4f, NMR_RC_ITERS);
        }
        {   /* bucket-full spend floor: bits saved beyond the reservoir's
             * remaining headroom are simply lost, so with a full bucket the
             * plan must not sit below nominal minus what can still be
             * banked. Without this a quiet intro poisons the corridor high
             * and the loud entrance rate-limits through it - a muffled
             * first second at the exact moment the listener tunes in. */
            int headroom = rc_bmax - av_clip(s->nmr->rc_fill, -rc_bmax, rc_bmax);
            int fbits = (rc_rate_frame - headroom) * chans / s->channels;
            if (tot < fbits) {
                lam = nmr_solve_slots(s, sl, nsl, NMR_STEP, fbits, lam / 64.0f, lam, NMR_RC_ITERS);
                tot = 0;
                for (int k = 0; k < nsl; k++)
                    tot += nmr_slot_bits(sl[k], s->nmr->nb[sl[k]->si], NMR_STEP);
            }
        }
        if (s->nmr->lam_slew > 0.0f) {
            float kup, kdn;
            /* hold lambda near-constant within short runs; bits follow content */
            kup = (is8_any && s->nmr->prev_was_short) ? NMR_SLEW_RUN : NMR_SLEW;
            /* a deliberate onset burst may dive as far as its widened corridor
             * allows; the RECOVERY back up is what must stay gradual */
            kdn = (is8_any && s->nmr->run_burst > 1.0f) ? NMR_SLEW * s->nmr->run_burst :
                  (is8_any && s->nmr->prev_was_short)    ? NMR_SLEW_RUN : NMR_SLEW;
            if (lam > s->nmr->lam_slew * kup || lam < s->nmr->lam_slew / kdn) {
                lam = av_clipf(lam, s->nmr->lam_slew / kdn, s->nmr->lam_slew * kup);
                tot = nmr_eval_slots(s, sl, nsl, NMR_STEP, lam);
                /* never at the price of an illegal reservoir excursion */
                if (tot > rc_cap) {
                    lam = nmr_solve_slots(s, sl, nsl, NMR_STEP, rc_cap, lam, 1e4f, NMR_RC_ITERS);
                }
            }
        }
        s->nmr->lam_slew = lam;
    } else if (rc_eligible && !vbr) {
        /* corridor not yet bootstrapped: the same bucket-full floor, so a
         * fade-in bootstraps lam_rc at a spending operating point instead
         * of memorizing the intro's starvation lambda */
        int headroom = rc_bmax - av_clip(s->nmr->rc_fill, -rc_bmax, rc_bmax);
        int fbits = (rc_rate_frame - headroom) * chans / s->channels;
        int tot = 0;
        for (int k = 0; k < nsl; k++)
            tot += nmr_slot_bits(sl[k], s->nmr->nb[sl[k]->si], NMR_STEP);
        if (tot < fbits)
            lam = nmr_solve_slots(s, sl, nsl, NMR_STEP, fbits, lam / 64.0f, lam, NMR_RC_ITERS);
    }

    if (!vbr || vbr_subst)
        for (int k = 0; k < nsl; k++)
            s->nmr->lam[sl[k]->cur_ch] = lam;   /* warm start for the next frame */
    {   /* nd: mean achieved dist/real-mask (dimensionless starvation +
         * noise-class signal) */
        float ndsum = 0.0f; int ndn = 0;
        for (int k = 0; k < nsl; k++) {
            NMRSlot *t = sl[k];
            float (*ndk)[NMR_NCAND] = s->nmr->nd[t->si];
            for (int b_ = 0; b_ < t->nact; b_++) {
                int b = t->act[b_], bi = t->bidx[b];
                if (t->thr_real[bi] > 0.0f && t->thr[bi] > 0.0f && !t->hftx[bi]) {
                    ndsum += ndk[b][t->chosen[b]] * t->thr[bi] / t->thr_real[bi];
                    ndn++;
                }
            }
        }
        /* long frames only (short groups inflate the ratio) */
        if (ndn >= 8 && !is8_any) {
            float nd = ndsum / ndn;
            s->nmr->nd_ema = s->nmr->nd_ema > 0.0f ?
                             0.95f * s->nmr->nd_ema + 0.05f * nd : nd;
        }
    }
    {   /* track short vs long operating lambda (dense-beat boost scaling) */
        float *ema = is8_any ? &s->nmr->lam_short_ema : &s->nmr->lam_long_ema;
        *ema = *ema > 0.0f ? 0.9f * *ema + 0.1f * lam : lam;
        /* sustained-strain floor: snaps down at any comfortable moment,
         * recovers only slowly, so bursty content cannot bank pressure
         * credit between its lambda valleys. */
        s->nmr->lam_floor = s->nmr->lam_floor > 0.0f ?
            fminf(s->nmr->lam_floor * 1.02f, lam) : lam;
    }
    {   /* shared rate-pressure ramp: lambda vs nd-scaled anchors */
        float scale, ramp;
        scale = 1.0f + av_clipf(s->nmr->nd_ema / 50.0f, 0.0f, 8.0f);
        ramp  = s->nmr->lam_long_ema > 0.0f ?
                av_clipf((s->nmr->lam_long_ema - 120.0f * scale) /
                         (350.0f * scale - 120.0f * scale), 0.0f, 1.0f) : 0.0f;
        /* transparency veto: lambda*nd below ~74 = comfortable */
        if (s->nmr->nd_ema > 0.0f)
            ramp *= av_clipf((s->nmr->lam_long_ema * s->nmr->nd_ema - 60.0f) /
                             (120.0f - 60.0f), 0.0f, 1.0f);
        s->nmr->press = ramp;
    }
    if (rc_global) {
        /* track the centre toward the CONTENT lambda (demand-solved, pressure
         * divided out); clamped lambda is rate noise, not content */
        float c = s->nmr->lam_rc * powf(lam_dem / rc_off / s->nmr->lam_rc, NMR_RC_TRACK);
        s->nmr->lam_rc = av_clipf(c, 1e-6f, 1e4f);
    } else if (rc_eligible) {
        /* bootstrap the servo off the first substantive frame (silent lead-ins
         * have degenerate budgets) */
        int nbnd_max = 0;
        for (int k = 0; k < nsl; k++)
            nbnd_max = FFMAX(nbnd_max, sl[k]->nbnd);
        if (nbnd_max >= 8) {
            s->nmr->lam_rc  = av_clipf(lam, 1e-4f, 1e4f);
            s->nmr->lam_slew = s->nmr->lam_rc;
        }
    }

    {   /* PNS, per channel at the group's operating lambda */
        const float pns_lam = NMR_PNS_LAM;
        int pns_total = 0;
        for (int k = 0; k < nsl; k++) {
            NMRSlot *t = sl[k];
            const float (*ndk)[NMR_NCAND] = (const float (*)[NMR_NCAND])s->nmr->nd[t->si];
            const int   (*nbk)[NMR_NCAND] = (const int (*)[NMR_NCAND])s->nmr->nb[t->si];
            int pns_count = 0;
            /* band 0 (lowest freq) is kept as the global-gain / sf-chain anchor */
            for (int b = 1; b < t->nbnd; b++) {
                int bi = t->bidx[b];
                float spread = t->pspread[bi];
                float nmr_pns, cost_keep, cost_pns, frac;
                /* zeroes[] is only set on a coded band when it was shed for
                 * legality; such a band must not come back as noise */
                if (!t->sce->can_pns[bi] || t->sce->zeroes[bi])
                    continue;

                int was  = s->nmr->pns_prev[t->cur_ch & 15][bi];
                float bias = was ? NMR_PNS_STAY : NMR_PNS_ENTER;
                int want = 0, force_exit = 0;

                /* (can_pns was already checked above; gates below fill `want`) */
                if (t->pener[bi] > NMR_PNS_MAX_ET * t->thr_real[bi]) {
                    force_exit = 1;                       /* loud-band guard */
                } else if (lam > NMR_PNS_HOLE_LAM &&
                           (frac = ndk[b][t->chosen[b]] * t->thr[bi] /
                                   FFMAX(t->pener[bi], 1e-9f)) > NMR_PNS_HOLE_FRAC * (was ? 0.7f : 1.0f) &&
                           spread > NMR_PNS_HOLE_SPREAD) {
                    /* Spectral-hole fill: a noise-like band whose chosen
                     * rendition leaves most of its energy uncoded turns to
                     * sparse lines - structure damage the NMR objective
                     * cannot see - at ANY comfortable lambda */
                    want = 1;
                } else if (lam > pns_lam) {
                    if (ndk[b][t->chosen[b]] * t->thr[bi] >
                               NMR_PNS_NDGATE * t->thr_real[bi] * (was ? 0.5f : 1.0f)) {
                        /* replace only a band coded audibly badly; cost of
                         * energy-matched noise = its non-noise-like fraction */
                        nmr_pns = FFMAX(0.0f, t->pener[bi] * (1.0f - spread*spread))
                                  / FFMAX(t->thr[bi], 1e-9f);
                        cost_keep = ndk[b][t->chosen[b]] + lam * nbk[b][t->chosen[b]];
                        cost_pns  = nmr_pns + lam * NMR_PNS_BITS;
                        want = cost_pns < cost_keep * bias;
                    }
                }
                {   /* debounce; near-mask deletion candidates skip entry
                     * (noise beats the ~silent rendition they'd get) */
                    uint8_t *ron  = &s->nmr->pns_run_on [t->cur_ch & 15][bi];
                    uint8_t *roff = &s->nmr->pns_run_off[t->cur_ch & 15][bi];
                    int near = t->pener[bi] < 2.0f * t->thr_real[bi];
                    if (want) { if (*ron  < 255) (*ron)++;  *roff = 0; }
                    else      { if (*roff < 255) (*roff)++; *ron  = 0; }
                    if (force_exit)
                        want = was && *roff < 2;   /* tolerate 1-frame loudness blips */
                    else if (!was)
                        want = *ron >= (near ? 2 : NMR_PNS_ON);
                    else if (near)
                        want = 1;   /* physics-hysteresis: noise until audible */
                    else
                        want = !(*roff >= NMR_PNS_OFF);
                }
                if (want) {
                    t->is_pns[b] = 1;
                    pns_count++;
                }
            }
            if (pns_count) {
                /* filter the active list rather than rebuilding it from
                 * nbnd: bands shed for legality must stay out */
                int n = 0;
                for (int b_ = 0; b_ < t->nact; b_++)
                    if (!t->is_pns[t->act[b_]])
                        t->act[n++] = t->act[b_];
                t->nact = n;
            }
            pns_total += pns_count;
        }
        if (pns_total) {
            /* re-solve over the survivors: at fixed lambda the allocation is
             * the same except for the repaired sf-delta chain; in bisection
             * mode re-spend the freed budget */
            if (rc_global || vbr)
                nmr_eval_slots(s, sl, nsl, NMR_STEP, lam);
            else
                nmr_solve_slots(s, sl, nsl, NMR_STEP, destbits - pns_total * NMR_PNS_BITS,
                                1e-9f, 1e4f, NMR_ITERS);
        }
    }

    for (int k = 0; k < nsl; k++) {
        NMRSlot *t = sl[k];
        uint8_t *pp = s->nmr->pns_prev[t->cur_ch & 15];
        uint8_t now[128] = {0};
        for (int b = 0; b < t->nbnd; b++)
            if (t->is_pns[b])
                now[t->bidx[b]] = 1;
        memcpy(pp, now, 128);
    }
    for (int k = 0; k < nsl; k++)
        nmr_commit_channel(s, sl[k]);

}

static void search_for_quantizers_nmr(AVCodecContext *avctx,
                                      AACEncContext *s,
                                      SingleChannelElement *sce,
                                      const float lambda)
{
    AACNMRCurves *n = s->nmr;
    /* Global-lambda RC: one solve per frame at a servoed centre lambda; the reservoir
     * holds the long-run mean rate. Bypassed for VBR (-q:a) and the bootstrap frame. */
    int rc_eligible = !(avctx->flags & AV_CODEC_FLAG_QSCALE) && avctx->bit_rate > 0 &&
                      avctx->bit_rate_tolerance != 0;
    /* Signed reservoir; soft steering (bounded repay + rc_off), hard cap =
     * legality only. */
    int rc_rate_frame = avctx->bit_rate * 1024.0 / avctx->sample_rate;
    int rc_bmax = FFMIN(FFMAX(6144 * s->channels - rc_rate_frame, 256), NMR_CBR_BUF * s->channels);

    int rc_global, defer;
    NMRSlot *t;

    s->nmr->counted[s->cur_channel] = 0;

    if (rc_eligible && !n->rc_fill_seeded) {
        /* the decoder bit reservoir starts FULL: seed it so the head may frontload */
        n->rc_fill = rc_bmax;
        n->rc_fill_seeded = 1;
    }
    if (rc_eligible && avctx->frame_num != n->rc_frame_num) {
        if (n->rc_frame_num > 0 && n->lam_rc > 0.0f)
            n->rc_fill = av_clip(n->rc_fill + rc_rate_frame - s->last_frame_pb_count,
                                 -rc_bmax, rc_bmax);
        n->rc_frame_num = avctx->frame_num;
        n->pending = 0;    /* a deferred first channel never crosses a frame */
        /* consecutive saturated frames, once per frame across all element
         * groups: any frame without a saturated overage ends the run */
        n->rc_satrun    = n->rc_sat_frame ? n->rc_satrun + 1 : 0;
        n->rc_sat_frame = 0;
        /* latch the RC mode per frame: a mid-frame bootstrap must not flip
         * the CPE defer logic between channels */
        n->rc_gl = rc_eligible && n->lam_rc > 0.0f;
    }
    if (avctx->frame_num != n->win_frame_num) {
        /* Window history, once per frame in every rate-control mode (the
         * quality-target slew and the ABR servo read it too).
         * Transient burst run state: set at run start and held across the run so
         * coding stays uniform; repaid from the reservoir's steady stretches. */
        int is_short = sce->ics.window_sequence[0] == EIGHT_SHORT_SEQUENCE;
        n->win_frame_num = avctx->frame_num;
        if (is_short) {
            if (!n->prev_was_short) {           /* run start */
                if (n->frames_since_short >= NMR_BURST_GAP) {
                    n->run_burst = NMR_BURST_GAIN;
                } else {
                    /* dense-beat boost, scaled by measured short-frame starvation */
                    float imb = 0.0f;
                    if (n->lam_long_ema > 0.0f && n->lam_short_ema > 0.0f)
                        imb = av_clipf(n->lam_short_ema / n->lam_long_ema - 1.0f,
                                       0.0f, 1.0f);
                    n->run_burst = 1.0f + (NMR_SHORT_BOOST - 1.0f) * imb *
                                   n->frames_since_short / (float)NMR_BURST_GAP;
                }
            }
            n->frames_since_short = 0;
        } else {
            /* the frame closing a run (the STOP) absorbs the corridor recoil
             * of the boosted shorts; give it half the run's factor so the
             * repayment spreads into the steady stretch instead */
            n->run_burst = n->prev_was_short ? sqrtf(n->run_burst) : 1.0f;
            n->frames_since_short++;
        }
        n->prev_was_short = is_short;
    }
    rc_global = rc_eligible && n->rc_gl;

    /* CPE budget pool: under global-lambda RC, defer the pair's first channel
     * and solve both against one pooled budget when the second one arrives. */
    defer = n->pair && rc_global;

    t = &n->slot[(defer && n->pending) ? 1 : 0];
    t->si = (defer && n->pending) ? 1 : 0;

    if (!nmr_setup_channel(avctx, s, sce, t)) {
        nmr_bail_channel(sce);
        t->nbnd = t->nact = 0;
    }

    if (defer && !n->pending) {
        n->pending = 1;                          /* wait for the partner channel */
        return;
    }

    {
        NMRSlot *sl[2];
        int nsl = 0, chans = 1;
        if (defer) {
            n->pending = 0;
            chans = 2;
            if (n->slot[0].nact)
                sl[nsl++] = &n->slot[0];
            if (n->slot[1].nact)
                sl[nsl++] = &n->slot[1];
        } else if (t->nact) {
            sl[nsl++] = t;
        }
        if (!nsl)
            return;                              /* nothing codeable in the group */
        nmr_solve_group(avctx, s, lambda, sl, nsl, chans,
                        rc_eligible, rc_global, rc_rate_frame, rc_bmax);
    }
}

#endif /* AVCODEC_AACCODER_NMR_H */
