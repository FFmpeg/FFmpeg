/*
 * AAC coefficients encoder
 * Copyright (C) 2008-2009 Konstantin Shishkov
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
 * @file
 * AAC coefficients encoder
 */

/***********************************
 *              TODOs:
 * speedup quantizer selection
 * add sane pulse detection
 ***********************************/

#include "libavutil/libm.h" // brought forward to work around cygwin header breakage

#include <float.h>

#include "libavutil/mathematics.h"
#include "mathops.h"
#include "avcodec.h"
#include "put_bits.h"
#include "aac.h"
#include "aacenc.h"
#include "aactab.h"
#include "aacenctab.h"
#include "aacenc_utils.h"
#include "aacenc_quantization.h"

#include "aacenc_is.h"
#include "aacenc_tns.h"
#include "aacenc_ltp.h"
#include "aacenc_pred.h"

#include "libavcodec/aaccoder_twoloop.h"

/* Parameter of f(x) = a*(lambda/100), defines the maximum fourier spread
 * beyond which no PNS is used (since the SFBs contain tone rather than noise) */
#define NOISE_SPREAD_THRESHOLD 0.9f

/* Parameter of f(x) = a*(100/lambda), defines how much PNS is allowed to
 * replace low energy non zero bands */
#define NOISE_LAMBDA_REPLACE 1.948f

#include "libavcodec/aaccoder_trellis.h"

/**
 * structure used in optimal codebook search
 */
typedef struct BandCodingPath {
    int prev_idx; ///< pointer to the previous path point
    float cost;   ///< path cost
    int run;
} BandCodingPath;

/**
 * Encode band info for single window group bands.
 */
static void encode_window_bands_info(AACEncContext *s, SingleChannelElement *sce,
                                     int win, int group_len, const float lambda)
{
    BandCodingPath path[120][CB_TOT_ALL];
    int w, swb, cb, start, size;
    int i, j;
    const int max_sfb  = sce->ics.max_sfb;
    const int run_bits = sce->ics.num_windows == 1 ? 5 : 3;
    const int run_esc  = (1 << run_bits) - 1;
    int idx, ppos, count;
    int stackrun[120], stackcb[120], stack_len;
    float next_minrd = INFINITY;
    int next_mincb = 0;

    abs_pow34_v(s->scoefs, sce->coeffs, 1024);
    start = win*128;
    for (cb = 0; cb < CB_TOT_ALL; cb++) {
        path[0][cb].cost     = 0.0f;
        path[0][cb].prev_idx = -1;
        path[0][cb].run      = 0;
    }
    for (swb = 0; swb < max_sfb; swb++) {
        size = sce->ics.swb_sizes[swb];
        if (sce->zeroes[win*16 + swb]) {
            for (cb = 0; cb < CB_TOT_ALL; cb++) {
                path[swb+1][cb].prev_idx = cb;
                path[swb+1][cb].cost     = path[swb][cb].cost;
                path[swb+1][cb].run      = path[swb][cb].run + 1;
            }
        } else {
            float minrd = next_minrd;
            int mincb = next_mincb;
            next_minrd = INFINITY;
            next_mincb = 0;
            for (cb = 0; cb < CB_TOT_ALL; cb++) {
                float cost_stay_here, cost_get_here;
                float rd = 0.0f;
                if (cb >= 12 && sce->band_type[win*16+swb] < aac_cb_out_map[cb] ||
                    cb  < aac_cb_in_map[sce->band_type[win*16+swb]] && sce->band_type[win*16+swb] > aac_cb_out_map[cb]) {
                    path[swb+1][cb].prev_idx = -1;
                    path[swb+1][cb].cost     = INFINITY;
                    path[swb+1][cb].run      = path[swb][cb].run + 1;
                    continue;
                }
                for (w = 0; w < group_len; w++) {
                    FFPsyBand *band = &s->psy.ch[s->cur_channel].psy_bands[(win+w)*16+swb];
                    rd += quantize_band_cost(s, &sce->coeffs[start + w*128],
                                             &s->scoefs[start + w*128], size,
                                             sce->sf_idx[(win+w)*16+swb], aac_cb_out_map[cb],
                                             lambda / band->threshold, INFINITY, NULL, NULL, 0);
                }
                cost_stay_here = path[swb][cb].cost + rd;
                cost_get_here  = minrd              + rd + run_bits + 4;
                if (   run_value_bits[sce->ics.num_windows == 8][path[swb][cb].run]
                    != run_value_bits[sce->ics.num_windows == 8][path[swb][cb].run+1])
                    cost_stay_here += run_bits;
                if (cost_get_here < cost_stay_here) {
                    path[swb+1][cb].prev_idx = mincb;
                    path[swb+1][cb].cost     = cost_get_here;
                    path[swb+1][cb].run      = 1;
                } else {
                    path[swb+1][cb].prev_idx = cb;
                    path[swb+1][cb].cost     = cost_stay_here;
                    path[swb+1][cb].run      = path[swb][cb].run + 1;
                }
                if (path[swb+1][cb].cost < next_minrd) {
                    next_minrd = path[swb+1][cb].cost;
                    next_mincb = cb;
                }
            }
        }
        start += sce->ics.swb_sizes[swb];
    }

    //convert resulting path from backward-linked list
    stack_len = 0;
    idx       = 0;
    for (cb = 1; cb < CB_TOT_ALL; cb++)
        if (path[max_sfb][cb].cost < path[max_sfb][idx].cost)
            idx = cb;
    ppos = max_sfb;
    while (ppos > 0) {
        av_assert1(idx >= 0);
        cb = idx;
        stackrun[stack_len] = path[ppos][cb].run;
        stackcb [stack_len] = cb;
        idx = path[ppos-path[ppos][cb].run+1][cb].prev_idx;
        ppos -= path[ppos][cb].run;
        stack_len++;
    }
    //perform actual band info encoding
    start = 0;
    for (i = stack_len - 1; i >= 0; i--) {
        cb = aac_cb_out_map[stackcb[i]];
        put_bits(&s->pb, 4, cb);
        count = stackrun[i];
        memset(sce->zeroes + win*16 + start, !cb, count);
        //XXX: memset when band_type is also uint8_t
        for (j = 0; j < count; j++) {
            sce->band_type[win*16 + start] = cb;
            start++;
        }
        while (count >= run_esc) {
            put_bits(&s->pb, run_bits, run_esc);
            count -= run_esc;
        }
        put_bits(&s->pb, run_bits, count);
    }
}


typedef struct TrellisPath {
    float cost;
    int prev;
} TrellisPath;

#define TRELLIS_STAGES 121
#define TRELLIS_STATES (SCALE_MAX_DIFF+1)

static void set_special_band_scalefactors(AACEncContext *s, SingleChannelElement *sce)
{
    int w, g;
    int prevscaler_n = -255, prevscaler_i = 0;
    int bands = 0;

    for (w = 0; w < sce->ics.num_windows; w += sce->ics.group_len[w]) {
        for (g = 0;  g < sce->ics.num_swb; g++) {
            if (sce->zeroes[w*16+g])
                continue;
            if (sce->band_type[w*16+g] == INTENSITY_BT || sce->band_type[w*16+g] == INTENSITY_BT2) {
                sce->sf_idx[w*16+g] = av_clip(roundf(log2f(sce->is_ener[w*16+g])*2), -155, 100);
                bands++;
            } else if (sce->band_type[w*16+g] == NOISE_BT) {
                sce->sf_idx[w*16+g] = av_clip(3+ceilf(log2f(sce->pns_ener[w*16+g])*2), -100, 155);
                if (prevscaler_n == -255)
                    prevscaler_n = sce->sf_idx[w*16+g];
                bands++;
            }
        }
    }

    if (!bands)
        return;

    /* Clip the scalefactor indices */
    for (w = 0; w < sce->ics.num_windows; w += sce->ics.group_len[w]) {
        for (g = 0;  g < sce->ics.num_swb; g++) {
            if (sce->zeroes[w*16+g])
                continue;
            if (sce->band_type[w*16+g] == INTENSITY_BT || sce->band_type[w*16+g] == INTENSITY_BT2) {
                sce->sf_idx[w*16+g] = prevscaler_i = av_clip(sce->sf_idx[w*16+g], prevscaler_i - SCALE_MAX_DIFF, prevscaler_i + SCALE_MAX_DIFF);
            } else if (sce->band_type[w*16+g] == NOISE_BT) {
                sce->sf_idx[w*16+g] = prevscaler_n = av_clip(sce->sf_idx[w*16+g], prevscaler_n - SCALE_MAX_DIFF, prevscaler_n + SCALE_MAX_DIFF);
            }
        }
    }
}

static void search_for_quantizers_anmr(AVCodecContext *avctx, AACEncContext *s,
                                       SingleChannelElement *sce,
                                       const float lambda)
{
    int q, w, w2, g, start = 0;
    int i, j;
    int idx;
    TrellisPath paths[TRELLIS_STAGES][TRELLIS_STATES];
    int bandaddr[TRELLIS_STAGES];
    int minq;
    float mincost;
    float q0f = FLT_MAX, q1f = 0.0f, qnrgf = 0.0f;
    int q0, q1, qcnt = 0;

    for (i = 0; i < 1024; i++) {
        float t = fabsf(sce->coeffs[i]);
        if (t > 0.0f) {
            q0f = FFMIN(q0f, t);
            q1f = FFMAX(q1f, t);
            qnrgf += t*t;
            qcnt++;
        }
    }

    if (!qcnt) {
        memset(sce->sf_idx, 0, sizeof(sce->sf_idx));
        memset(sce->zeroes, 1, sizeof(sce->zeroes));
        return;
    }

    //minimum scalefactor index is when minimum nonzero coefficient after quantizing is not clipped
    q0 = av_clip(coef2minsf(q0f), 0, SCALE_MAX_POS-1);
    //maximum scalefactor index is when maximum coefficient after quantizing is still not zero
    q1 = av_clip(coef2maxsf(q1f), 1, SCALE_MAX_POS);
    if (q1 - q0 > 60) {
        int q0low  = q0;
        int q1high = q1;
        //minimum scalefactor index is when maximum nonzero coefficient after quantizing is not clipped
        int qnrg = av_clip_uint8(log2f(sqrtf(qnrgf/qcnt))*4 - 31 + SCALE_ONE_POS - SCALE_DIV_512);
        q1 = qnrg + 30;
        q0 = qnrg - 30;
        if (q0 < q0low) {
            q1 += q0low - q0;
            q0  = q0low;
        } else if (q1 > q1high) {
            q0 -= q1 - q1high;
            q1  = q1high;
        }
    }
    // q0 == q1 isn't really a legal situation
    if (q0 == q1) {
        // the following is indirect but guarantees q1 != q0 && q1 near q0
        q1 = av_clip(q0+1, 1, SCALE_MAX_POS);
        q0 = av_clip(q1-1, 0, SCALE_MAX_POS - 1);
    }

    for (i = 0; i < TRELLIS_STATES; i++) {
        paths[0][i].cost    = 0.0f;
        paths[0][i].prev    = -1;
    }
    for (j = 1; j < TRELLIS_STAGES; j++) {
        for (i = 0; i < TRELLIS_STATES; i++) {
            paths[j][i].cost    = INFINITY;
            paths[j][i].prev    = -2;
        }
    }
    idx = 1;
    abs_pow34_v(s->scoefs, sce->coeffs, 1024);
    for (w = 0; w < sce->ics.num_windows; w += sce->ics.group_len[w]) {
        start = w*128;
        for (g = 0; g < sce->ics.num_swb; g++) {
            const float *coefs = &sce->coeffs[start];
            float qmin, qmax;
            int nz = 0;

            bandaddr[idx] = w * 16 + g;
            qmin = INT_MAX;
            qmax = 0.0f;
            for (w2 = 0; w2 < sce->ics.group_len[w]; w2++) {
                FFPsyBand *band = &s->psy.ch[s->cur_channel].psy_bands[(w+w2)*16+g];
                if (band->energy <= band->threshold || band->threshold == 0.0f) {
                    sce->zeroes[(w+w2)*16+g] = 1;
                    continue;
                }
                sce->zeroes[(w+w2)*16+g] = 0;
                nz = 1;
                for (i = 0; i < sce->ics.swb_sizes[g]; i++) {
                    float t = fabsf(coefs[w2*128+i]);
                    if (t > 0.0f)
                        qmin = FFMIN(qmin, t);
                    qmax = FFMAX(qmax, t);
                }
            }
            if (nz) {
                int minscale, maxscale;
                float minrd = INFINITY;
                float maxval;
                //minimum scalefactor index is when minimum nonzero coefficient after quantizing is not clipped
                minscale = coef2minsf(qmin);
                //maximum scalefactor index is when maximum coefficient after quantizing is still not zero
                maxscale = coef2maxsf(qmax);
                minscale = av_clip(minscale - q0, 0, TRELLIS_STATES - 1);
                maxscale = av_clip(maxscale - q0, 0, TRELLIS_STATES);
                if (minscale == maxscale) {
                    maxscale = av_clip(minscale+1, 1, TRELLIS_STATES);
                    minscale = av_clip(maxscale-1, 0, TRELLIS_STATES - 1);
                }
                maxval = find_max_val(sce->ics.group_len[w], sce->ics.swb_sizes[g], s->scoefs+start);
                for (q = minscale; q < maxscale; q++) {
                    float dist = 0;
                    int cb = find_min_book(maxval, sce->sf_idx[w*16+g]);
                    for (w2 = 0; w2 < sce->ics.group_len[w]; w2++) {
                        FFPsyBand *band = &s->psy.ch[s->cur_channel].psy_bands[(w+w2)*16+g];
                        dist += quantize_band_cost(s, coefs + w2*128, s->scoefs + start + w2*128, sce->ics.swb_sizes[g],
                                                   q + q0, cb, lambda / band->threshold, INFINITY, NULL, NULL, 0);
                    }
                    minrd = FFMIN(minrd, dist);

                    for (i = 0; i < q1 - q0; i++) {
                        float cost;
                        cost = paths[idx - 1][i].cost + dist
                               + ff_aac_scalefactor_bits[q - i + SCALE_DIFF_ZERO];
                        if (cost < paths[idx][q].cost) {
                            paths[idx][q].cost    = cost;
                            paths[idx][q].prev    = i;
                        }
                    }
                }
            } else {
                for (q = 0; q < q1 - q0; q++) {
                    paths[idx][q].cost = paths[idx - 1][q].cost + 1;
                    paths[idx][q].prev = q;
                }
            }
            sce->zeroes[w*16+g] = !nz;
            start += sce->ics.swb_sizes[g];
            idx++;
        }
    }
    idx--;
    mincost = paths[idx][0].cost;
    minq    = 0;
    for (i = 1; i < TRELLIS_STATES; i++) {
        if (paths[idx][i].cost < mincost) {
            mincost = paths[idx][i].cost;
            minq = i;
        }
    }
    while (idx) {
        sce->sf_idx[bandaddr[idx]] = minq + q0;
        minq = FFMAX(paths[idx][minq].prev, 0);
        idx--;
    }
    //set the same quantizers inside window groups
    for (w = 0; w < sce->ics.num_windows; w += sce->ics.group_len[w])
        for (g = 0;  g < sce->ics.num_swb; g++)
            for (w2 = 1; w2 < sce->ics.group_len[w]; w2++)
                sce->sf_idx[(w+w2)*16+g] = sce->sf_idx[w*16+g];
}

static void search_for_quantizers_fast(AVCodecContext *avctx, AACEncContext *s,
                                       SingleChannelElement *sce,
                                       const float lambda)
{
    int i, w, w2, g;
    int minq = 255;

    memset(sce->sf_idx, 0, sizeof(sce->sf_idx));
    for (w = 0; w < sce->ics.num_windows; w += sce->ics.group_len[w]) {
        for (g = 0; g < sce->ics.num_swb; g++) {
            for (w2 = 0; w2 < sce->ics.group_len[w]; w2++) {
                FFPsyBand *band = &s->psy.ch[s->cur_channel].psy_bands[(w+w2)*16+g];
                if (band->energy <= band->threshold) {
                    sce->sf_idx[(w+w2)*16+g] = 218;
                    sce->zeroes[(w+w2)*16+g] = 1;
                } else {
                    sce->sf_idx[(w+w2)*16+g] = av_clip(SCALE_ONE_POS - SCALE_DIV_512 + log2f(band->threshold), 80, 218);
                    sce->zeroes[(w+w2)*16+g] = 0;
                }
                minq = FFMIN(minq, sce->sf_idx[(w+w2)*16+g]);
            }
        }
    }
    for (i = 0; i < 128; i++) {
        sce->sf_idx[i] = 140;
        //av_clip(sce->sf_idx[i], minq, minq + SCALE_MAX_DIFF - 1);
    }
    //set the same quantizers inside window groups
    for (w = 0; w < sce->ics.num_windows; w += sce->ics.group_len[w])
        for (g = 0;  g < sce->ics.num_swb; g++)
            for (w2 = 1; w2 < sce->ics.group_len[w]; w2++)
                sce->sf_idx[(w+w2)*16+g] = sce->sf_idx[w*16+g];
}

static void search_for_pns(AACEncContext *s, AVCodecContext *avctx, SingleChannelElement *sce)
{
    FFPsyBand *band;
    int w, g, w2, i;
    int wlen = 1024 / sce->ics.num_windows;
    int bandwidth, cutoff;
    float *PNS = &s->scoefs[0*128], *PNS34 = &s->scoefs[1*128];
    float *NOR34 = &s->scoefs[3*128];
    uint8_t nextband[128];
    const float lambda = s->lambda;
    const float freq_mult = avctx->sample_rate*0.5f/wlen;
    const float thr_mult = NOISE_LAMBDA_REPLACE*(100.0f/lambda);
    const float spread_threshold = FFMIN(0.75f, NOISE_SPREAD_THRESHOLD*FFMAX(0.5f, lambda/100.f));
    const float dist_bias = av_clipf(4.f * 120 / lambda, 0.25f, 4.0f);
    const float pns_transient_energy_r = FFMIN(0.7f, lambda / 140.f);

    int refbits = avctx->bit_rate * 1024.0 / avctx->sample_rate
        / ((avctx->flags & CODEC_FLAG_QSCALE) ? 2.0f : avctx->channels)
        * (lambda / 120.f);

    /** Keep this in sync with twoloop's cutoff selection */
    float rate_bandwidth_multiplier = 1.5f;
    int prev = -1000, prev_sf = -1;
    int frame_bit_rate = (avctx->flags & CODEC_FLAG_QSCALE)
        ? (refbits * rate_bandwidth_multiplier * avctx->sample_rate / 1024)
        : (avctx->bit_rate / avctx->channels);

    frame_bit_rate *= 1.15f;

    if (avctx->cutoff > 0) {
        bandwidth = avctx->cutoff;
    } else {
        bandwidth = FFMAX(3000, AAC_CUTOFF_FROM_BITRATE(frame_bit_rate, 1, avctx->sample_rate));
    }

    cutoff = bandwidth * 2 * wlen / avctx->sample_rate;

    memcpy(sce->band_alt, sce->band_type, sizeof(sce->band_type));
    ff_init_nextband_map(sce, nextband);
    for (w = 0; w < sce->ics.num_windows; w += sce->ics.group_len[w]) {
        int wstart = w*128;
        for (g = 0;  g < sce->ics.num_swb; g++) {
            int noise_sfi;
            float dist1 = 0.0f, dist2 = 0.0f, noise_amp;
            float pns_energy = 0.0f, pns_tgt_energy, energy_ratio, dist_thresh;
            float sfb_energy = 0.0f, threshold = 0.0f, spread = 2.0f;
            float min_energy = -1.0f, max_energy = 0.0f;
            const int start = wstart+sce->ics.swb_offset[g];
            const float freq = (start-wstart)*freq_mult;
            const float freq_boost = FFMAX(0.88f*freq/NOISE_LOW_LIMIT, 1.0f);
            if (freq < NOISE_LOW_LIMIT || (start-wstart) >= cutoff) {
                if (!sce->zeroes[w*16+g])
                    prev_sf = sce->sf_idx[w*16+g];
                continue;
            }
            for (w2 = 0; w2 < sce->ics.group_len[w]; w2++) {
                band = &s->psy.ch[s->cur_channel].psy_bands[(w+w2)*16+g];
                sfb_energy += band->energy;
                spread     = FFMIN(spread, band->spread);
                threshold  += band->threshold;
                if (!w2) {
                    min_energy = max_energy = band->energy;
                } else {
                    min_energy = FFMIN(min_energy, band->energy);
                    max_energy = FFMAX(max_energy, band->energy);
                }
            }

            /* Ramps down at ~8000Hz and loosens the dist threshold */
            dist_thresh = av_clipf(2.5f*NOISE_LOW_LIMIT/freq, 0.5f, 2.5f) * dist_bias;

            /* PNS is acceptable when all of these are true:
             * 1. high spread energy (noise-like band)
             * 2. near-threshold energy (high PE means the random nature of PNS content will be noticed)
             * 3. on short window groups, all windows have similar energy (variations in energy would be destroyed by PNS)
             *
             * At this stage, point 2 is relaxed for zeroed bands near the noise threshold (hole avoidance is more important)
             */
            if ((!sce->zeroes[w*16+g] && !ff_sfdelta_can_remove_band(sce, nextband, prev_sf, w*16+g)) ||
                ((sce->zeroes[w*16+g] || !sce->band_alt[w*16+g]) && sfb_energy < threshold*sqrtf(1.0f/freq_boost)) || spread < spread_threshold ||
                (!sce->zeroes[w*16+g] && sce->band_alt[w*16+g] && sfb_energy > threshold*thr_mult*freq_boost) ||
                min_energy < pns_transient_energy_r * max_energy ) {
                sce->pns_ener[w*16+g] = sfb_energy;
                if (!sce->zeroes[w*16+g])
                    prev_sf = sce->sf_idx[w*16+g];
                continue;
            }

            pns_tgt_energy = sfb_energy*FFMIN(1.0f, spread*spread);
            noise_sfi = av_clip(roundf(log2f(pns_tgt_energy)*2), -100, 155); /* Quantize */
            noise_amp = -ff_aac_pow2sf_tab[noise_sfi + POW_SF2_ZERO];    /* Dequantize */
            if (prev != -1000) {
                int noise_sfdiff = noise_sfi - prev + SCALE_DIFF_ZERO;
                if (noise_sfdiff < 0 || noise_sfdiff > 2*SCALE_MAX_DIFF) {
                    if (!sce->zeroes[w*16+g])
                        prev_sf = sce->sf_idx[w*16+g];
                    continue;
                }
            }
            for (w2 = 0; w2 < sce->ics.group_len[w]; w2++) {
                float band_energy, scale, pns_senergy;
                const int start_c = (w+w2)*128+sce->ics.swb_offset[g];
                band = &s->psy.ch[s->cur_channel].psy_bands[(w+w2)*16+g];
                for (i = 0; i < sce->ics.swb_sizes[g]; i+=2) {
                    double rnd[2];
                    av_bmg_get(&s->lfg, rnd);
                    PNS[i+0] = (float)rnd[0];
                    PNS[i+1] = (float)rnd[1];
                }
                band_energy = s->fdsp->scalarproduct_float(PNS, PNS, sce->ics.swb_sizes[g]);
                scale = noise_amp/sqrtf(band_energy);
                s->fdsp->vector_fmul_scalar(PNS, PNS, scale, sce->ics.swb_sizes[g]);
                pns_senergy = s->fdsp->scalarproduct_float(PNS, PNS, sce->ics.swb_sizes[g]);
                pns_energy += pns_senergy;
                abs_pow34_v(NOR34, &sce->coeffs[start_c], sce->ics.swb_sizes[g]);
                abs_pow34_v(PNS34, PNS, sce->ics.swb_sizes[g]);
                dist1 += quantize_band_cost(s, &sce->coeffs[start_c],
                                            NOR34,
                                            sce->ics.swb_sizes[g],
                                            sce->sf_idx[(w+w2)*16+g],
                                            sce->band_alt[(w+w2)*16+g],
                                            lambda/band->threshold, INFINITY, NULL, NULL, 0);
                /* Estimate rd on average as 5 bits for SF, 4 for the CB, plus spread energy * lambda/thr */
                dist2 += band->energy/(band->spread*band->spread)*lambda*dist_thresh/band->threshold;
            }
            if (g && sce->band_type[w*16+g-1] == NOISE_BT) {
                dist2 += 5;
            } else {
                dist2 += 9;
            }
            energy_ratio = pns_tgt_energy/pns_energy; /* Compensates for quantization error */
            sce->pns_ener[w*16+g] = energy_ratio*pns_tgt_energy;
            if (sce->zeroes[w*16+g] || !sce->band_alt[w*16+g] || (energy_ratio > 0.85f && energy_ratio < 1.25f && dist2 < dist1)) {
                sce->band_type[w*16+g] = NOISE_BT;
                sce->zeroes[w*16+g] = 0;
                prev = noise_sfi;
            } else {
                if (!sce->zeroes[w*16+g])
                    prev_sf = sce->sf_idx[w*16+g];
            }
        }
    }
}

static void mark_pns(AACEncContext *s, AVCodecContext *avctx, SingleChannelElement *sce)
{
    FFPsyBand *band;
    int w, g, w2;
    int wlen = 1024 / sce->ics.num_windows;
    int bandwidth, cutoff;
    const float lambda = s->lambda;
    const float freq_mult = avctx->sample_rate*0.5f/wlen;
    const float spread_threshold = FFMIN(0.75f, NOISE_SPREAD_THRESHOLD*FFMAX(0.5f, lambda/100.f));
    const float pns_transient_energy_r = FFMIN(0.7f, lambda / 140.f);

    int refbits = avctx->bit_rate * 1024.0 / avctx->sample_rate
        / ((avctx->flags & CODEC_FLAG_QSCALE) ? 2.0f : avctx->channels)
        * (lambda / 120.f);

    /** Keep this in sync with twoloop's cutoff selection */
    float rate_bandwidth_multiplier = 1.5f;
    int frame_bit_rate = (avctx->flags & CODEC_FLAG_QSCALE)
        ? (refbits * rate_bandwidth_multiplier * avctx->sample_rate / 1024)
        : (avctx->bit_rate / avctx->channels);

    frame_bit_rate *= 1.15f;

    if (avctx->cutoff > 0) {
        bandwidth = avctx->cutoff;
    } else {
        bandwidth = FFMAX(3000, AAC_CUTOFF_FROM_BITRATE(frame_bit_rate, 1, avctx->sample_rate));
    }

    cutoff = bandwidth * 2 * wlen / avctx->sample_rate;

    memcpy(sce->band_alt, sce->band_type, sizeof(sce->band_type));
    for (w = 0; w < sce->ics.num_windows; w += sce->ics.group_len[w]) {
        for (g = 0;  g < sce->ics.num_swb; g++) {
            float sfb_energy = 0.0f, threshold = 0.0f, spread = 2.0f;
            float min_energy = -1.0f, max_energy = 0.0f;
            const int start = sce->ics.swb_offset[g];
            const float freq = start*freq_mult;
            const float freq_boost = FFMAX(0.88f*freq/NOISE_LOW_LIMIT, 1.0f);
            if (freq < NOISE_LOW_LIMIT || start >= cutoff) {
                sce->can_pns[w*16+g] = 0;
                continue;
            }
            for (w2 = 0; w2 < sce->ics.group_len[w]; w2++) {
                band = &s->psy.ch[s->cur_channel].psy_bands[(w+w2)*16+g];
                sfb_energy += band->energy;
                spread     = FFMIN(spread, band->spread);
                threshold  += band->threshold;
                if (!w2) {
                    min_energy = max_energy = band->energy;
                } else {
                    min_energy = FFMIN(min_energy, band->energy);
                    max_energy = FFMAX(max_energy, band->energy);
                }
            }

            /* PNS is acceptable when all of these are true:
             * 1. high spread energy (noise-like band)
             * 2. near-threshold energy (high PE means the random nature of PNS content will be noticed)
             * 3. on short window groups, all windows have similar energy (variations in energy would be destroyed by PNS)
             */
            sce->pns_ener[w*16+g] = sfb_energy;
            if (sfb_energy < threshold*sqrtf(1.5f/freq_boost) || spread < spread_threshold || min_energy < pns_transient_energy_r * max_energy) {
                sce->can_pns[w*16+g] = 0;
            } else {
                sce->can_pns[w*16+g] = 1;
            }
        }
    }
}

static void search_for_ms(AACEncContext *s, ChannelElement *cpe)
{
    int start = 0, i, w, w2, g, sid_sf_boost, prev_mid, prev_side;
    uint8_t nextband0[128], nextband1[128];
    float M[128], S[128];
    float *L34 = s->scoefs, *R34 = s->scoefs + 128, *M34 = s->scoefs + 128*2, *S34 = s->scoefs + 128*3;
    const float lambda = s->lambda;
    const float mslambda = FFMIN(1.0f, lambda / 120.f);
    SingleChannelElement *sce0 = &cpe->ch[0];
    SingleChannelElement *sce1 = &cpe->ch[1];
    if (!cpe->common_window)
        return;

    /** Scout out next nonzero bands */
    ff_init_nextband_map(sce0, nextband0);
    ff_init_nextband_map(sce1, nextband1);

    prev_mid = sce0->sf_idx[0];
    prev_side = sce1->sf_idx[0];
    for (w = 0; w < sce0->ics.num_windows; w += sce0->ics.group_len[w]) {
        start = 0;
        for (g = 0;  g < sce0->ics.num_swb; g++) {
            float bmax = bval2bmax(g * 17.0f / sce0->ics.num_swb) / 0.0045f;
            if (!cpe->is_mask[w*16+g])
                cpe->ms_mask[w*16+g] = 0;
            if (!sce0->zeroes[w*16+g] && !sce1->zeroes[w*16+g] && !cpe->is_mask[w*16+g]) {
                float Mmax = 0.0f, Smax = 0.0f;

                /* Must compute mid/side SF and book for the whole window group */
                for (w2 = 0; w2 < sce0->ics.group_len[w]; w2++) {
                    for (i = 0; i < sce0->ics.swb_sizes[g]; i++) {
                        M[i] = (sce0->coeffs[start+(w+w2)*128+i]
                              + sce1->coeffs[start+(w+w2)*128+i]) * 0.5;
                        S[i] =  M[i]
                              - sce1->coeffs[start+(w+w2)*128+i];
                    }
                    abs_pow34_v(M34, M, sce0->ics.swb_sizes[g]);
                    abs_pow34_v(S34, S, sce0->ics.swb_sizes[g]);
                    for (i = 0; i < sce0->ics.swb_sizes[g]; i++ ) {
                        Mmax = FFMAX(Mmax, M34[i]);
                        Smax = FFMAX(Smax, S34[i]);
                    }
                }

                for (sid_sf_boost = 0; sid_sf_boost < 4; sid_sf_boost++) {
                    float dist1 = 0.0f, dist2 = 0.0f;
                    int B0 = 0, B1 = 0;
                    int minidx;
                    int mididx, sididx;
                    int midcb, sidcb;

                    minidx = FFMIN(sce0->sf_idx[w*16+g], sce1->sf_idx[w*16+g]);
                    mididx = av_clip(minidx, 0, SCALE_MAX_POS - SCALE_DIV_512);
                    sididx = av_clip(minidx - sid_sf_boost * 3, 0, SCALE_MAX_POS - SCALE_DIV_512);
                    if (sce0->band_type[w*16+g] != NOISE_BT && sce1->band_type[w*16+g] != NOISE_BT
                        && (   !ff_sfdelta_can_replace(sce0, nextband0, prev_mid, mididx, w*16+g)
                            || !ff_sfdelta_can_replace(sce1, nextband1, prev_side, sididx, w*16+g))) {
                        /* scalefactor range violation, bad stuff, will decrease quality unacceptably */
                        continue;
                    }

                    midcb = find_min_book(Mmax, mididx);
                    sidcb = find_min_book(Smax, sididx);

                    /* No CB can be zero */
                    midcb = FFMAX(1,midcb);
                    sidcb = FFMAX(1,sidcb);

                    for (w2 = 0; w2 < sce0->ics.group_len[w]; w2++) {
                        FFPsyBand *band0 = &s->psy.ch[s->cur_channel+0].psy_bands[(w+w2)*16+g];
                        FFPsyBand *band1 = &s->psy.ch[s->cur_channel+1].psy_bands[(w+w2)*16+g];
                        float minthr = FFMIN(band0->threshold, band1->threshold);
                        int b1,b2,b3,b4;
                        for (i = 0; i < sce0->ics.swb_sizes[g]; i++) {
                            M[i] = (sce0->coeffs[start+(w+w2)*128+i]
                                  + sce1->coeffs[start+(w+w2)*128+i]) * 0.5;
                            S[i] =  M[i]
                                  - sce1->coeffs[start+(w+w2)*128+i];
                        }

                        abs_pow34_v(L34, sce0->coeffs+start+(w+w2)*128, sce0->ics.swb_sizes[g]);
                        abs_pow34_v(R34, sce1->coeffs+start+(w+w2)*128, sce0->ics.swb_sizes[g]);
                        abs_pow34_v(M34, M,                         sce0->ics.swb_sizes[g]);
                        abs_pow34_v(S34, S,                         sce0->ics.swb_sizes[g]);
                        dist1 += quantize_band_cost(s, &sce0->coeffs[start + (w+w2)*128],
                                                    L34,
                                                    sce0->ics.swb_sizes[g],
                                                    sce0->sf_idx[w*16+g],
                                                    sce0->band_type[w*16+g],
                                                    lambda / band0->threshold, INFINITY, &b1, NULL, 0);
                        dist1 += quantize_band_cost(s, &sce1->coeffs[start + (w+w2)*128],
                                                    R34,
                                                    sce1->ics.swb_sizes[g],
                                                    sce1->sf_idx[w*16+g],
                                                    sce1->band_type[w*16+g],
                                                    lambda / band1->threshold, INFINITY, &b2, NULL, 0);
                        dist2 += quantize_band_cost(s, M,
                                                    M34,
                                                    sce0->ics.swb_sizes[g],
                                                    mididx,
                                                    midcb,
                                                    lambda / minthr, INFINITY, &b3, NULL, 0);
                        dist2 += quantize_band_cost(s, S,
                                                    S34,
                                                    sce1->ics.swb_sizes[g],
                                                    sididx,
                                                    sidcb,
                                                    mslambda / (minthr * bmax), INFINITY, &b4, NULL, 0);
                        B0 += b1+b2;
                        B1 += b3+b4;
                        dist1 -= b1+b2;
                        dist2 -= b3+b4;
                    }
                    cpe->ms_mask[w*16+g] = dist2 <= dist1 && B1 < B0;
                    if (cpe->ms_mask[w*16+g]) {
                        if (sce0->band_type[w*16+g] != NOISE_BT && sce1->band_type[w*16+g] != NOISE_BT) {
                            sce0->sf_idx[w*16+g] = mididx;
                            sce1->sf_idx[w*16+g] = sididx;
                            sce0->band_type[w*16+g] = midcb;
                            sce1->band_type[w*16+g] = sidcb;
                        } else if ((sce0->band_type[w*16+g] != NOISE_BT) ^ (sce1->band_type[w*16+g] != NOISE_BT)) {
                            /* ms_mask unneeded, and it confuses some decoders */
                            cpe->ms_mask[w*16+g] = 0;
                        }
                        break;
                    } else if (B1 > B0) {
                        /* More boost won't fix this */
                        break;
                    }
                }
            }
            if (!sce0->zeroes[w*16+g] && sce0->band_type[w*16+g] < RESERVED_BT)
                prev_mid = sce0->sf_idx[w*16+g];
            if (!sce1->zeroes[w*16+g] && !cpe->is_mask[w*16+g] && sce1->band_type[w*16+g] < RESERVED_BT)
                prev_side = sce1->sf_idx[w*16+g];
            start += sce0->ics.swb_sizes[g];
        }
    }
}

AACCoefficientsEncoder ff_aac_coders[AAC_CODER_NB] = {
    [AAC_CODER_ANMR] = {
        search_for_quantizers_anmr,
        encode_window_bands_info,
        quantize_and_encode_band,
        ff_aac_encode_tns_info,
        ff_aac_encode_ltp_info,
        ff_aac_encode_main_pred,
        ff_aac_adjust_common_pred,
        ff_aac_adjust_common_ltp,
        ff_aac_apply_main_pred,
        ff_aac_apply_tns,
        ff_aac_update_ltp,
        ff_aac_ltp_insert_new_frame,
        set_special_band_scalefactors,
        search_for_pns,
        mark_pns,
        ff_aac_search_for_tns,
        ff_aac_search_for_ltp,
        search_for_ms,
        ff_aac_search_for_is,
        ff_aac_search_for_pred,
    },
    [AAC_CODER_TWOLOOP] = {
        search_for_quantizers_twoloop,
        codebook_trellis_rate,
        quantize_and_encode_band,
        ff_aac_encode_tns_info,
        ff_aac_encode_ltp_info,
        ff_aac_encode_main_pred,
        ff_aac_adjust_common_pred,
        ff_aac_adjust_common_ltp,
        ff_aac_apply_main_pred,
        ff_aac_apply_tns,
        ff_aac_update_ltp,
        ff_aac_ltp_insert_new_frame,
        set_special_band_scalefactors,
        search_for_pns,
        mark_pns,
        ff_aac_search_for_tns,
        ff_aac_search_for_ltp,
        search_for_ms,
        ff_aac_search_for_is,
        ff_aac_search_for_pred,
    },
    [AAC_CODER_FAST] = {
        search_for_quantizers_fast,
        encode_window_bands_info,
        quantize_and_encode_band,
        ff_aac_encode_tns_info,
        ff_aac_encode_ltp_info,
        ff_aac_encode_main_pred,
        ff_aac_adjust_common_pred,
        ff_aac_adjust_common_ltp,
        ff_aac_apply_main_pred,
        ff_aac_apply_tns,
        ff_aac_update_ltp,
        ff_aac_ltp_insert_new_frame,
        set_special_band_scalefactors,
        search_for_pns,
        mark_pns,
        ff_aac_search_for_tns,
        ff_aac_search_for_ltp,
        search_for_ms,
        ff_aac_search_for_is,
        ff_aac_search_for_pred,
    },
};
