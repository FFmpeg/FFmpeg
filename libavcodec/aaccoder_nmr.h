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

/* RC servo gain: scale the corridor centre by exp2(-K*fill/R) each frame to hold
 * the long-run mean rate; without it a bad centre drifts for dozens of frames. */
#define NMR_RC_K_CBR 0.5f

#define NMR_RC_ITERS 8 /* lambda bisection iters when clamping an over-cap frame */
/* Corridor: bisect within [lam_rc/NMR_RC_CORR, lam_rc*NMR_RC_CORR] so quality stays
 * smooth while per-frame demand is tracked; 1.5 cuts lambda jitter ~25%. */
#define NMR_RC_CORR   1.5f

/* Leaky-bucket half-depth (bits/ch); 512 is the sweet spot — tighter rebounds as
 * frames cannot hit the narrow window. Clamped to the 6144 bits/ch decoder buffer. */
#define NMR_CBR_BUF   512
#define NMR_RC_CITERS 3 /* corridor coarse-pass iters */

/* Transient bit-burst: an isolated onset (preceded by >= NMR_BURST_GAP long frames)
 * is coded NMR_BURST_GAIN x finer, held uniform across the run, repaid from steady stretches. */
#define NMR_BURST_GAP   10
#define NMR_BURST_GAIN  8.0f
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

static void search_for_quantizers_nmr(AVCodecContext *avctx,
                                      AACEncContext *s,
                                      SingleChannelElement *sce,
                                      const float lambda)
{
    int bch = ((avctx->flags & AV_CODEC_FLAG_QSCALE) ? 2.0f : avctx->ch_layout.nb_channels);
    int destbits = avctx->bit_rate * 1024.0 / avctx->sample_rate / bch * (lambda / 120.f);
    int allz = 0, cutoff = 1024, nbnd = 0;

    float thr[128];                 /* allocation-law effective threshold (drives the trellis) */
    float thr_real[128];            /* real masking threshold (perceptual gates: PNS) */
    float pener[128];               /* band energy (for PNS noise target)  */
    float pspread[128];             /* band tonality spread (1 = noise)     */
    int   minsf[128];
    float maxvals[128];

    /* coded-band trellis state (indexed 0..nbnd-1) */
    int bidx[128];                  /* sce band index (w*16+g) */
    int bw[128], bg[128], bst[128]; /* window group, swb, coef start per coded band */
    int blo[128];                   /* finest candidate scalefactor */
    int bnc[128];                   /* number of candidates */
    int chosen[128];
    int act[128];                   /* active (non-PNS) band coding order */
    uint8_t is_pns[128];            /* trellis band coded as noise */

    float (*nd)[NMR_NCAND] = s->nmr->nd; /* dist / threshold per candidate (heap) */
    int   (*nb)[NMR_NCAND] = s->nmr->nb; /* spectral bits per candidate (heap)    */

    /* two-pass coarse->fine grid step (see NMR_COARSE), the lambda search runs on
     * the cheap coarse grid, PASS 2 refines the winner at NMR_STEP granularity */
    const int cstep = NMR_COARSE > 0 ? NMR_COARSE : NMR_STEP;

    s->nmr->counted[s->cur_channel] = 0;

    /* Global-lambda RC: one solve per frame at a servoed centre lambda; the reservoir
     * holds the long-run mean rate. Bypassed for VBR (-q:a) and the bootstrap frame. */
    int rc_eligible = !(avctx->flags & AV_CODEC_FLAG_QSCALE) && avctx->bit_rate > 0 &&
                      avctx->bit_rate_tolerance != 0;
    /* Leaky-bucket reservoir: rc_fill (signed +-rc_bmax); the spend-floor/cap below force
     * lambda so no frame banks past +rc_bmax or borrows past -rc_bmax. */
    int rc_rate_frame = avctx->bit_rate * 1024.0 / avctx->sample_rate;
    int rc_bmax = FFMIN(FFMAX(6144 * s->channels - rc_rate_frame, 256), NMR_CBR_BUF * s->channels);
    if (rc_eligible && avctx->frame_num != s->nmr->rc_frame_num) {
        if (s->nmr->rc_frame_num > 0 && s->nmr->lam_rc > 0.0f)
            s->nmr->rc_fill = av_clip(s->nmr->rc_fill + rc_rate_frame - s->last_frame_pb_count,
                                      -rc_bmax, rc_bmax);
        s->nmr->rc_frame_num = avctx->frame_num;

        /* Transient burst run state: set at run start and held across the run so
         * coding stays uniform; repaid from the reservoir's steady stretches. */
        int is_short = sce->ics.window_sequence[0] == EIGHT_SHORT_SEQUENCE;
        if (is_short) {
            if (!s->nmr->prev_was_short)        /* run start */
                s->nmr->run_burst = s->nmr->frames_since_short >= NMR_BURST_GAP
                                  ? NMR_BURST_GAIN : 1.0f;
            s->nmr->frames_since_short = 0;
        } else {
            s->nmr->run_burst = 1.0f;
            s->nmr->frames_since_short++;
        }
        s->nmr->prev_was_short = is_short;
    }
    int rc_global = rc_eligible && s->nmr->lam_rc > 0.0f;

    if (s->psy.bitres.alloc >= 0)
        destbits = s->psy.bitres.alloc *
                   (lambda / (avctx->global_quality ? avctx->global_quality : 120));
    if (rc_global && s->psy.bitres.alloc >= 0)
        /* uniform CBR target: nominal rate plus fast reservoir repayment */
        destbits = (avctx->bit_rate * 1024.0 / avctx->sample_rate
                    + s->nmr->rc_fill / 2.0) / s->channels;
    destbits = FFMIN(destbits, 5800);
    /* honest budget: subtract the measured non-trellis overhead (section data, ICS,
     * sf/PNS signalling), which is rate-dependent hence adaptive. */
    if (s->nmr->side_inited)
        destbits = av_clip(destbits - (int)(s->nmr->side_ema / s->channels), 64, 5800);

    /* Apply the held transient burst factor (set in the run-state machine above). */
    if (sce->ics.window_sequence[0] == EIGHT_SHORT_SEQUENCE && s->nmr->run_burst > 1.0f)
        destbits = av_clip((int)(destbits * s->nmr->run_burst), 64, 6800);

    /* band cutoff index for this frame's window size; the bandwidth is fixed
     * at init and shared with the psy model */
    cutoff = s->bandwidth * 2 * (1024 / sce->ics.num_windows) / avctx->sample_rate;

    /* Short-block transient noise shaping (pairs with short-block TNS): temporal
     * premasking clamps each window's threshold toward the preceding windows'
     * (Apple's preEchoReduction), and flat-residual flattens each window's thresholds
     * to their per-window mean so TNS synthesis has a white floor to concentrate. */
    if (sce->ics.window_sequence[0] == EIGHT_SHORT_SEQUENCE) {
        const float pm_p1 = 0.1f, pm_p2 = 2.0f, pm_p3 = 4.0f;
        for (int g = 0; g < sce->ics.num_swb; g++) {
            float t1 = FLT_MAX, t2 = FLT_MAX;   /* original thr of w-1, w-2 */
            for (int w = 0; w < sce->ics.num_windows; w++) {
                FFPsyBand *b = &s->psy.ch[s->cur_channel].psy_bands[w*16+g];
                float t = b->threshold;
                float c = FFMIN(t, FFMIN(t1*pm_p2, t2*pm_p3));
                b->threshold = FFMAX(c, t*pm_p1);
                t2 = t1; t1 = t;
            }
        }
        {
            for (int w = 0; w < sce->ics.num_windows; w++) {
                float sum = 0.0f; int n = 0;
                for (int g = 0; g < sce->ics.num_swb; g++) {
                    FFPsyBand *b = &s->psy.ch[s->cur_channel].psy_bands[w*16+g];
                    if (b->energy > b->threshold && b->threshold > 0.0f) { sum += b->threshold; n++; }
                }
                if (n > 0) {
                    float mean = sum / n;
                    for (int g = 0; g < sce->ics.num_swb; g++) {
                        FFPsyBand *b = &s->psy.ch[s->cur_channel].psy_bands[w*16+g];
                        if (b->energy > b->threshold && b->threshold > 0.0f)
                            b->threshold = mean;
                    }
                }
            }
        }
    }

    /* Allocation curve to favour high frequencies */
    const float a_ae = 0.443f, a_at = 0.111f;
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
            for (int w2 = 0; w2 < sce->ics.group_len[w]; w2++) {
                FFPsyBand *band = &s->psy.ch[s->cur_channel].psy_bands[(w+w2)*16+g];
                ener   += band->energy;
                spread  = FFMIN(spread, band->spread);
                if (start >= cutoff || band->energy <= band->threshold || band->threshold == 0.0f) {
                    sce->zeroes[(w+w2)*16+g] = 1;
                    continue;
                }
                uplim += band->threshold;
                nz = 1;
            }
            sce->zeroes[w*16+g] = !nz;
            thr_real[w*16+g] = uplim;       /* real mask, before the allocation law (PNS gate) */
            if (nz && ener > 0.0f && uplim > 0.0f)
                uplim = expf(a_ae * logf(ener) + a_at * logf(uplim));
            thr[w*16+g]     = uplim;
            pener[w*16+g]   = ener;
            pspread[w*16+g] = spread;
            allz |= nz;
        }
    }
    if (!allz)
        goto bail;

    s->aacdsp.abs_pow34(s->scoefs, sce->coeffs, 1024);
    ff_quantize_band_cost_cache_init(s);

    /* finest codeable scalefactor and max value per band */
    for (int w = 0; w < sce->ics.num_windows; w += sce->ics.group_len[w]) {
        int start = w*128;
        for (int g = 0; g < sce->ics.num_swb; g++) {
            maxvals[w*16+g] = find_max_val(sce->ics.group_len[w], sce->ics.swb_sizes[g], s->scoefs + start);
            minsf[w*16+g]   = maxvals[w*16+g] > 0 ? coef2minsf(maxvals[w*16+g]) : 0;
            start += sce->ics.swb_sizes[g];
        }
    }

    /* PASS 1:
     * precompute each coded band's cost curve at the coarse candidate step
     * (the lambda search runs on this cheap grid, PASS 2 refines the winner) */
    {
        for (int w = 0; w < sce->ics.num_windows; w += sce->ics.group_len[w]) {
            int start = w*128;
            for (int g = 0; g < sce->ics.num_swb; g++) {
                if (!sce->zeroes[w*16+g] && maxvals[w*16+g] > 0 && nbnd < 128) {
                    int lo = av_clip(minsf[w*16+g], 0, SCALE_MAX_POS);
                    float invthr = 1.0f / FFMAX(thr[w*16+g], 1e-9f);
                    int ncand = nmr_band_curve(s, sce, w, g, start, lo, cstep, NMR_NCAND,
                                               invthr, maxvals[w*16+g], nd[nbnd], nb[nbnd]);
                    if (ncand == 0) {
                        /* nothing codeable -> drop the whole group band. The
                         * subwindow flags must be cleared too: the encoder later
                         * re-derives the group flag by ANDing them, which would
                         * resurrect the band with a never-assigned scalefactor. */
                        for (int w2 = 0; w2 < sce->ics.group_len[w]; w2++)
                            sce->zeroes[(w+w2)*16+g] = 1;
                    } else {
                        bidx[nbnd] = w*16+g;
                        bw[nbnd] = w;
                        bg[nbnd] = g;
                        bst[nbnd] = start;
                        blo[nbnd] = lo;
                        bnc[nbnd] = ncand;
                        nbnd++;
                    }
                }
                start += sce->ics.swb_sizes[g];
            }
        }
    }
    if (!nbnd)
        goto bail;

    /* solve the trellis over all coded bands, then offer PNS at the operating
     * lambda and re-solve over the survivors with the freed budget */
    {
        int nact = nbnd, pns_count = 0;
        float lam0 = s->nmr->lam[s->cur_channel];
        float lam;

        for (int b = 0; b < nbnd; b++) {
            act[b] = b;
            is_pns[b] = 0;
        }
        if (rc_global) {
            /* bisect to this frame's bit demand within the corridor around the
             * servoed lambda: per-frame psy demand is tracked, but lambda cannot
             * jump, which keeps quality smooth across frames */
            float lo = s->nmr->lam_rc / NMR_RC_CORR;
            /* Transient burst: widen the lower lambda bound so the bisection can actually
             * pour the boosted destbits into an onset frame (finer coding kills the
             * pre-echo); reservoir servo repays it from the steady frames. run_burst==1 on
             * non-onset frames leaves the corridor unchanged. */
            if (sce->ics.window_sequence[0] == EIGHT_SHORT_SEQUENCE && s->nmr->run_burst > 1.0f)
                lo /= s->nmr->run_burst;
            lam = nmr_solve(s, nd, nb, blo, bnc, cstep, act, nact, destbits, chosen,
                            lo, s->nmr->lam_rc * NMR_RC_CORR,
                            NMR_RC_CITERS);

            int tot = 0;
            for (int k = 0; k < nact; k++)
                tot += nb[act[k]][chosen[act[k]]];
            for (int k = 1; k < nact; k++)
                tot += NMR_SFBITS((blo[act[k]]+chosen[act[k]]*cstep) - (blo[act[k-1]]+chosen[act[k-1]]*cstep));
            int hardcap = av_clip((int)(5800.f * FFMIN(1.f, lambda / 120.f)), 256, 5800);
            /* leaky-bucket window: don't borrow past -rc_bmax (cap) or bank past +rc_bmax (floor) */
            int rc_cap   = FFMIN(hardcap, (s->nmr->rc_fill + rc_rate_frame + rc_bmax) / s->channels);
            int rc_floor = FFMAX(0, (s->nmr->rc_fill + rc_rate_frame - rc_bmax) / s->channels);
            if (tot > rc_cap)
                lam = nmr_solve(s, nd, nb, blo, bnc, cstep, act, nact, rc_cap, chosen,
                                lam, 1e4f, NMR_CITERS);
            else if (tot < rc_floor)
                lam = nmr_solve(s, nd, nb, blo, bnc, cstep, act, nact, rc_floor, chosen,
                                1e-9f, lam, NMR_CITERS);
        } else if (NMR_COARSE > 0 && lam0 > 0.0f) {
            /* per-frame bisection; lambda is strongly frame-correlated, so when a
             * previous frame's operating lambda exists, bisect a narrow bracket
             * around it. A result near the bracket edge means the budget crossing
             * lies outside (hard content transition) == redo the full search. */
            lam = nmr_solve(s, nd, nb, blo, bnc, cstep, act, nact, destbits, chosen,
                            lam0/32.0f, lam0*32.0f, NMR_CWARM);
            if (lam < lam0/16.0f || lam > lam0*16.0f)
                lam0 = 0.0f;
        }
        if (!rc_global && lam0 <= 0.0f)
            lam = nmr_solve(s, nd, nb, blo, bnc, cstep, act, nact, destbits, chosen,
                            1e-9f, 1e4f, NMR_COARSE > 0 ? NMR_CITERS : NMR_ITERS);

        /* PASS 2:
         * refine each band at full granularity (NMR_STEP) in a +/-cstep window
         * around the coarse pick, then re-solve. Recovers single-pass quality while the
         * lambda search stayed cheap on the coarse grid. */
        if (NMR_COARSE > 0) {
            /* nmr_speed, 0 = slowest/best, higher = faster. It narrows the fine
             * refine +/-window (scalefactors) below NMR_COARSE: at speed 0 the window
             * spans the whole coarse-grid gap, so the two-pass result matches the
             * exhaustive single-pass search.
             * Each speed level shaves one sf off the window.
             * At @64k mono (Zim / xRT): speed 0 -> 0.00095/15x,
             * 2 -> 0.00096/18x, 3 -> 0.00100/20x, 4 -> 0.00103/22x */
            int win = NMR_COARSE - av_clip(s->options.nmr_speed, 0, 4);
            for (int b = 0; b < nbnd; b++) {
                int center = blo[b] + chosen[b]*cstep;
                int flo    = av_clip(center - win, av_clip(minsf[bidx[b]], 0, SCALE_MAX_POS), SCALE_MAX_POS);
                int maxn   = FFMIN(NMR_NCAND, 2*win/NMR_STEP + 1);
                float invthr = 1.0f / FFMAX(thr[bidx[b]], 1e-9f);
                int ncand  = nmr_band_curve(s, sce, bw[b], bg[b], bst[b], flo, NMR_STEP, maxn,
                                            invthr, maxvals[bidx[b]], nd[b], nb[b]);
                blo[b] = flo;
                bnc[b] = FFMAX(1, ncand);
            }
            /* fine pass: narrow corridor around the coarse solve */
            if (rc_global)
                lam = nmr_solve(s, nd, nb, blo, bnc, NMR_STEP, act, nact, destbits, chosen,
                                lam/2.0f, lam*2.0f, NMR_RC_FITERS);
            else
                lam = nmr_solve(s, nd, nb, blo, bnc, NMR_STEP, act, nact, destbits, chosen,
                                lam/16.0f, lam*16.0f, NMR_IFINE);
        }

        if (rc_global) {
            /* leaky-bucket clamp: keep the frame within [rc_floor, rc_cap] so the reservoir
             * stays in +-rc_bmax -- clamp lambda UP if it would borrow past the cap, DOWN if it
             * would bank past the floor (spend-floor). The hard cap follows the encoder's outer
             * lambda so the (rare) hard-overflow re-encode -- which shrinks that lambda -- always
             * converges; on the first pass lambda is nominal and this is 5800. */
            int hardcap = av_clip((int)(5800.f * FFMIN(1.f, lambda / 120.f)), 256, 5800);
            int tot = 0;
            for (int k = 0; k < nact; k++)
                tot += nb[act[k]][chosen[act[k]]];
            for (int k = 1; k < nact; k++)
                tot += NMR_SFBITS((blo[act[k]]+chosen[act[k]]*NMR_STEP) - (blo[act[k-1]]+chosen[act[k-1]]*NMR_STEP));
            int rc_cap   = FFMIN(hardcap, (s->nmr->rc_fill + rc_rate_frame + rc_bmax) / s->channels);
            int rc_floor = FFMAX(0, (s->nmr->rc_fill + rc_rate_frame - rc_bmax) / s->channels);
            if (tot > rc_cap)
                lam = nmr_solve(s, nd, nb, blo, bnc, NMR_STEP, act, nact, rc_cap, chosen,
                                lam, 1e4f, NMR_RC_ITERS);
            else if (tot < rc_floor)
                lam = nmr_solve(s, nd, nb, blo, bnc, NMR_STEP, act, nact, rc_floor, chosen,
                                1e-9f, lam, NMR_RC_ITERS);
        }

        s->nmr->lam[s->cur_channel] = lam;   /* warm start for the next frame */
        if (rc_global) {
            /* drag the corridor centre toward the realized lambda so it follows
             * content drift faster than the reservoir term alone */
            float c = s->nmr->lam_rc * powf(lam / s->nmr->lam_rc, NMR_RC_TRACK);
            /* then servo the centre off the reservoir error so the long-run rate
             * returns to nominal. rc_fill>0 = bits banked (undershooting) -> lower
             * lambda to spend them; <0 -> raise it. This is what holds the mean;
             * the corridor tracking alone has no rate authority and a bad centre
             * would otherwise drift for dozens of frames, starving each one. */
            float R = avctx->bit_rate * 1024.0 / avctx->sample_rate;
            c *= exp2f(-NMR_RC_K_CBR * s->nmr->rc_fill / R);
            s->nmr->lam_rc = av_clipf(c, 1e-6f, 1e4f);
        } else if (rc_eligible && nbnd >= 8) {
            /* bootstrap the servo off the first substantive frame; near-silent
             * lead-in frames have degenerate budgets that rail the bisection to
             * a nonsense lambda and would poison the whole stream */
            s->nmr->lam_rc = av_clipf(lam, 1e-4f, 10.0f);
        }

        {   /* PNS */
            const float pns_lam = NMR_PNS_LAM;
            /* band 0 (lowest freq) is kept as the global-gain / sf-chain anchor */
            for (int b = 1; b < nbnd; b++) {
                int bi = bidx[b];
                float spread = pspread[bi];
                float nmr_pns, cost_keep, cost_pns, frac;
                if (!sce->can_pns[bi])
                    continue;

                /* Loud-band guard: never substitute a band whose energy is far above the
                 * masking threshold -- energy-matched noise on a dominant band clips/pops
                 * (and is audibly wrong). PNS is for near-masked noise only. */
                if (pener[bi] > NMR_PNS_MAX_ET * thr_real[bi])
                    continue;

                /* Struggle gate: no PNS at all unless the encoder is genuinely under bit
                 * pressure (high operating lambda). */
                if (lam <= pns_lam)
                    continue;

                /* Spectral-hole fill: a noise-like band the trellis left mostly empty */
                frac = nd[b][chosen[b]] * thr[bi] / FFMAX(pener[bi], 1e-9f);
                if (spread > NMR_PNS_HOLE_SPREAD && frac > NMR_PNS_HOLE_FRAC) {
                    is_pns[b] = 1;
                    pns_count++;
                    continue;
                }

                /* Only replace a band that is being coded audibly badly */
                if (nd[b][chosen[b]] * thr[bi] <= NMR_PNS_NDGATE * thr_real[bi])
                    continue;

                /* perceptual cost of replacing the band with energy-matched noise:
                 * the non-noise-like fraction of its energy, in dist/threshold units */
                nmr_pns = FFMAX(0.0f, pener[bi] * (1.0f - spread*spread))
                          / FFMAX(thr[bi], 1e-9f);
                cost_keep = nd[b][chosen[b]] + lam * nb[b][chosen[b]];
                cost_pns  = nmr_pns + lam * NMR_PNS_BITS;
                if (cost_pns < cost_keep) {
                    is_pns[b] = 1;
                    pns_count++;
                }
            }
            if (pns_count) {
                int budget2 = destbits - pns_count * NMR_PNS_BITS;
                nact = 0;
                for (int b = 0; b < nbnd; b++)
                    if (!is_pns[b])
                        act[nact++] = b;
                /* re-solve over the survivors: at fixed lambda the allocation is
                 * the same except for the repaired sf-delta chain; in bisection
                 * mode re-spend the freed budget */
                if (rc_global)
                    nmr_solve(s, nd, nb, blo, bnc, NMR_STEP, act, nact, budget2, chosen,
                              lam, lam, 1);
                else
                    nmr_solve(s, nd, nb, blo, bnc, NMR_STEP, act, nact, budget2, chosen,
                              1e-9f, 1e4f, NMR_ITERS);
            }
        }
        for (int b = 0; b < nbnd; b++) {
            int bi = bidx[b];
            if (is_pns[b]) {
                sce->band_type[bi] = NOISE_BT;
                sce->zeroes[bi]    = 0;
                sce->pns_ener[bi]  = pener[bi] * FFMIN(1.0f, pspread[bi]*pspread[bi]);
            } else {
                sce->sf_idx[bi] = av_clip(blo[b] + chosen[b]*NMR_STEP, 0, SCALE_MAX_POS);
            }
        }

        {   /* record the bits this solve accounted for; the encoder compares them
             * against the channel's real output to keep the budget honest */
            int tot = 0, prevb = -1;
            for (int b = 0; b < nbnd; b++) {
                if (is_pns[b])
                    continue;
                tot += nb[b][chosen[b]];
                if (prevb >= 0)
                    tot += NMR_SFBITS((blo[b]+chosen[b]*NMR_STEP) - (blo[prevb]+chosen[prevb]*NMR_STEP));
                prevb = b;
            }
            s->nmr->counted[s->cur_channel] = tot;
        }
    }

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
                sce->band_type[w*16+g] = find_min_book(maxvals[w*16+g], sce->sf_idx[w*16+g]);
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

        /* Every band, coded or not, must carry a chain-legal scalefactor: the
         * codebook trellis (encode_window_bands_info) may later absorb a dropped
         * band into a nonzero section, resurrecting it, and its sf then gets
         * coded. Forward-fill with the previous coded sf (delta 0, cheapest);
         * leading bands get the global gain. */
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
    return;

bail:
    /* Nothing codeable in this channel. Leave a fully consistent state: any
     * stale nonzero band_type acts as a codebook lower bound in the encoder's
     * section trellis (encode_window_bands_info), which would forbid the zero
     * section and resurrect the band with a stale, chain-illegal scalefactor.
     * Pre-decided intensity bands keep their signalling. */
    for (int i = 0; i < 128; i++) {
        if (sce->band_type[i] == INTENSITY_BT || sce->band_type[i] == INTENSITY_BT2)
            continue;
        sce->zeroes[i]    = 1;
        sce->band_type[i] = 0;
    }
}

#endif /* AVCODEC_AACCODER_NMR_H */
