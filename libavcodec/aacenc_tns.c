/*
 * AAC encoder TNS
 * Copyright (C) 2015 Rostislav Pehlivanov
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
 * AAC encoder temporal noise shaping
 * @author Rostislav Pehlivanov ( atomnuker gmail com )
 */

#include "libavutil/libm.h"
#include "aacenc.h"
#include "aacenc_tns.h"
#include "aactab.h"
#include "aacenc_utils.h"
#include "lpc_functions.h"

/* Could be set to 3 to save an additional bit at the cost of little quality */
#define TNS_Q_BITS 4

/* Coefficient resolution in short windows */
#define TNS_Q_BITS_IS8 4

/* We really need the bits we save here elsewhere */
#define TNS_ENABLE_COEF_COMPRESSION

/* Apple-derived TNS: weighted-spectrum predictor, accepted only if the measured
 * post-quantization prediction gain clears a block-type-dependent bar (Apple RE). */
#define TNS_PREDGAIN_GATE   1.4f    /* first gate: predicted LPC gain */
#define TNS_PG_C1_LONG      1.4f    /* min measured gain, long blocks */
#define TNS_PG_C1_SHORT     2.2f    /* min measured gain, short blocks */
#define TNS_PG_CLAMP        6.0f    /* upper bound: poles near unit circle → noise blowup */
#define TNS_WEIGHT_FLOOR    0.01f   /* per-bin masking floor for the weighted spectrum */

static inline int compress_coeffs(int *coef, int order, int c_bits)
{
    int i;
    const int low_idx   = c_bits ?  4 : 2;
    const int shift_val = c_bits ?  8 : 4;
    const int high_idx  = c_bits ? 11 : 5;
#ifndef TNS_ENABLE_COEF_COMPRESSION
    return 0;
#endif /* TNS_ENABLE_COEF_COMPRESSION */
    for (i = 0; i < order; i++)
        if (coef[i] >= low_idx && coef[i] <= high_idx)
            return 0;
    for (i = 0; i < order; i++)
        coef[i] -= (coef[i] > high_idx) ? shift_val : 0;
    return 1;
}

/** Encode TNS data. */
void ff_aac_encode_tns_info(AACEncContext *s, SingleChannelElement *sce)
{
    TemporalNoiseShaping *tns = &sce->tns;
    int i, w, filt, coef_compress = 0, coef_len;
    const int is8 = sce->ics.window_sequence[0] == EIGHT_SHORT_SEQUENCE;
    const int c_bits = is8 ? TNS_Q_BITS_IS8 == 4 : TNS_Q_BITS == 4;

    if (!sce->tns.present)
        return;

    for (i = 0; i < sce->ics.num_windows; i++) {
        put_bits(&s->pb, 2 - is8, sce->tns.n_filt[i]);
        if (!tns->n_filt[i])
            continue;
        put_bits(&s->pb, 1, c_bits);
        for (filt = 0; filt < tns->n_filt[i]; filt++) {
            put_bits(&s->pb, 6 - 2 * is8, tns->length[i][filt]);
            put_bits(&s->pb, 5 - 2 * is8, tns->order[i][filt]);
            if (!tns->order[i][filt])
                continue;
            put_bits(&s->pb, 1, tns->direction[i][filt]);
            coef_compress = compress_coeffs(tns->coef_idx[i][filt],
                                            tns->order[i][filt], c_bits);
            put_bits(&s->pb, 1, coef_compress);
            coef_len = c_bits + 3 - coef_compress;
            for (w = 0; w < tns->order[i][filt]; w++)
                put_bits(&s->pb, coef_len, tns->coef_idx[i][filt][w]);
        }
    }
}

/* Cap the TNS band range at the first PNS band to avoid TNS+PNS conflicts. */
static int tns_max_nonpns(const SingleChannelElement *sce, int mmm)
{
    for (int w = 0; w < sce->ics.num_windows; w += sce->ics.group_len[w])
        for (int g = 0; g < mmm; g++)
            if (sce->band_type[w*16+g] == NOISE_BT) { mmm = g; break; }
    return mmm;
}

/* Apply TNS filter */
void ff_aac_apply_tns(AACEncContext *s, SingleChannelElement *sce)
{
    TemporalNoiseShaping *tns = &sce->tns;
    IndividualChannelStream *ics = &sce->ics;
    int w, filt, m, i, top, order, bottom, start, end, size, inc;
    const int mmm = tns_max_nonpns(sce, FFMIN(ics->tns_max_bands, ics->max_sfb));
    float lpc[TNS_MAX_ORDER];

    /* TNS predicts from the post-M/S and post-I/S coefficients. */
    float hist[1024];
    memcpy(hist, sce->coeffs, sizeof(hist));

    for (w = 0; w < ics->num_windows; w++) {
        bottom = ics->num_swb;
        for (filt = 0; filt < tns->n_filt[w]; filt++) {
            top    = bottom;
            bottom = FFMAX(0, top - tns->length[w][filt]);
            order  = tns->order[w][filt];
            if (order == 0)
                continue;

            // tns_decode_coef
            compute_lpc_coefs(tns->coef[w][filt], 0, order, lpc, 0, 0, 0, NULL);

            start = ics->swb_offset[FFMIN(bottom, mmm)];
            end   = ics->swb_offset[FFMIN(   top, mmm)];
            if ((size = end - start) <= 0)
                continue;
            if (tns->direction[w][filt]) {
                inc = -1;
                start = end - 1;
            } else {
                inc = 1;
            }
            start += w * 128;

            /* AR filter */
            for (m = 0; m < size; m++, start += inc) {
                for (i = 1; i <= FFMIN(m, order); i++) {
                    sce->coeffs[start] += lpc[i-1]*hist[start - i*inc];
                }
            }
        }
    }
}

/*
 * c_bits - 1 if 4 bit coefficients, 0 if 3 bit coefficients
 */
static inline void quantize_coefs(double *coef, int *idx, float *lpc, int order,
                                  int c_bits)
{
    int i;
    const float *quant_arr = ff_tns_tmp2_map[c_bits];
    for (i = 0; i < order; i++) {
        idx[i] = quant_array_idx(coef[i], quant_arr, c_bits ? 16 : 8);
        lpc[i] = quant_arr[idx[i]];
    }
}

/*
 * 3 bits per coefficient with 8 short windows
 */
void ff_aac_search_for_tns(AACEncContext *s, SingleChannelElement *sce)
{
    TemporalNoiseShaping *tns = &sce->tns;
    int w, count = 0;
    const int mmm = tns_max_nonpns(sce, FFMIN(sce->ics.tns_max_bands, sce->ics.max_sfb));
    const int is8 = sce->ics.window_sequence[0] == EIGHT_SHORT_SEQUENCE;
    const int c_bits = is8 ? TNS_Q_BITS_IS8 == 4 : TNS_Q_BITS == 4;
    const int sfb_start = av_clip(tns_min_sfb[is8][s->samplerate_index], 0, mmm);
    const int sfb_end   = av_clip(sce->ics.num_swb, 0, mmm);
    const int order = is8 ? 7 : 12;
    const int slant = sce->ics.window_sequence[0] == LONG_STOP_SEQUENCE  ? 1 :
                      sce->ics.window_sequence[0] == LONG_START_SEQUENCE ? 0 : 2;
    const int sfb_len = sfb_end - sfb_start;
    const int coef_len = sce->ics.swb_offset[sfb_end] - sce->ics.swb_offset[sfb_start];
    const int n_filt = is8 ? 1 : order != TNS_MAX_ORDER ? 2 : 3;
    const int ord_g  = order / n_filt;

    /* Apple's accept bar (minimum measured prediction gain): higher on short blocks,
     * where a weak filter's shaped-noise tail spreads across the 50% overlap. */
    const float c1 = is8 ? TNS_PG_C1_SHORT : TNS_PG_C1_LONG;
    FFPsyBand *const psy_bands = &s->psy.ch[s->cur_channel].psy_bands[0];

    if (coef_len <= 0 || sfb_len <= 0) {
        sce->tns.present = 0;
        return;
    }

    /* time-domain window length backing one coding window: a long MDCT block is
     * fed 2048 windowed samples (current 1024 + overlap), each short block 256. */
    const int tlen = is8 ? 256 : 2048;

    for (w = 0; w < sce->ics.num_windows; w++) {
        int filt, any = 0;

        /* The filter gets ran in the direction of the signal's *temporal* energy,
         * so the quantization noise stays in the loud masked part rather than spilling
         * into the quiet part. */
        const float *tw = sce->ret_buf + w*tlen;
        float e_early = 0.0f, e_late = 0.0f;
        int ti;
        for (ti = 0; ti < tlen/2; ti++)
            e_early += tw[ti]*tw[ti];
        for (; ti < tlen; ti++)
            e_late += tw[ti]*tw[ti];
        const int tdir = e_early > e_late;

        /* Walk the frequency regions exactly as the decoder does: filter 0 is the
         * topmost band region, each subsequent filter covers the next region down,
         * clamped to mmm. Each filter gets its own LPC over its own region. */
        int top_sfb = sce->ics.num_swb;
        for (filt = 0; filt < n_filt; filt++) {
            double coefs[MAX_LPC_ORDER];
            float wspec[1024], tmp[1024], lpc_q[TNS_MAX_ORDER];
            int len_sfb = (filt == n_filt - 1) ? sfb_len - filt*(sfb_len/n_filt)
                                               : sfb_len/n_filt;
            int bot_sfb = FFMAX(0, top_sfb - len_sfb);
            int g_lo = FFMIN(bot_sfb, mmm), g_hi = FFMIN(top_sfb, mmm);
            int c_lo = sce->ics.swb_offset[g_lo];
            int c_hi = sce->ics.swb_offset[g_hi];
            int clen = c_hi - c_lo;
            const int dir = slant != 2 ? slant : tdir;
            float gain, orig_e = 0.0f, filt_e = 0.0f;
            int m, i, g, inc, st;

            tns->length[w][filt] = len_sfb;
            tns->order[w][filt]  = 0;     /* default: region carries no filter */
            top_sfb = bot_sfb;

            if (clen <= 2*ord_g)          /* too short for a stable order-ord_g LPC */
                continue;

            /* Fit LPC on the perceptually-weighted spectrum X/sqrt(thr), floored
             * to avoid a near-zero threshold blowing up a single bin (Apple). */
            {
                float maxrms = 0.0f, floorrms;
                int k;
                for (g = g_lo; g < g_hi; g++) {
                    int s0 = sce->ics.swb_offset[g], s1 = sce->ics.swb_offset[g+1];
                    float rms = sqrtf(FFMAX(psy_bands[w*16 + g].threshold, 0.0f) /
                                      FFMAX(s1 - s0, 1));
                    maxrms = FFMAX(maxrms, rms);
                }
                floorrms = FFMAX(maxrms * TNS_WEIGHT_FLOOR, 1e-9f);
                for (g = g_lo; g < g_hi; g++) {
                    int s0 = sce->ics.swb_offset[g], s1 = sce->ics.swb_offset[g+1];
                    float rms = sqrtf(FFMAX(psy_bands[w*16 + g].threshold, 0.0f) /
                                      FFMAX(s1 - s0, 1));
                    float wgt = 1.0f / FFMAX(rms, floorrms);
                    for (k = s0; k < s1; k++)
                        wspec[k - c_lo] = sce->coeffs[w*128 + k] * wgt;
                }
                /* Short blocks: unwindowed fit; Hann window zeros the edges of the
                 * tiny region, wrecking the LPC. Long blocks keep the window. */
                gain = ff_lpc_calc_ref_coefs_f(&s->lpc, wspec, clen, ord_g, coefs, !is8);
            }
            /* Reject below the first gate and above the clamp (poles near unit circle). */
            if (!isfinite(gain) || gain < TNS_PREDGAIN_GATE || gain > TNS_PG_CLAMP)
                continue;
            /* Negate: ff_lpc_calc_ref_coefs_f sign convention is opposite to what
             * ff_aac_apply_tns's MA filter needs; fed unnegated, it anti-whitens. */
            for (i = 0; i < ord_g; i++)
                coefs[i] = -coefs[i];

            /* Quantize, then build the decoder's direct-form LPC. */
            quantize_coefs(coefs, tns->coef_idx[w][filt], tns->coef[w][filt],
                           ord_g, c_bits);
            compute_lpc_coefs(tns->coef[w][filt], 0, ord_g, lpc_q, 0, 0, 0, NULL);

            /* Apply the quantized filter to the weighted spectrum and measure gain. */
            const float *msrc = wspec;
            inc = dir ? -1 : 1;
            st  = dir ? clen - 1 : 0;
            for (m = 0; m < clen; m++) {
                int idx = st + m*inc;
                float acc = msrc[idx];
                for (i = 1; i <= FFMIN(m, ord_g); i++)
                    acc += lpc_q[i-1] * msrc[idx - i*inc];
                tmp[idx] = acc;
            }
            for (m = 0; m < clen; m++) {
                orig_e += msrc[m]*msrc[m];
                filt_e += tmp[m]*tmp[m];
            }
            filt_e = FFMAX(filt_e, 1e-9f);

            /* Keep only if measured post-quantization gain clears C1 (Apple's outcome gate). */
            if (orig_e < c1*filt_e)
                continue;

            tns->order[w][filt] = ord_g;
            tns->direction[w][filt] = dir;
            any = 1;
        }
        tns->n_filt[w] = any ? n_filt : 0;
        if (any)
            count++;
    }
    sce->tns.present = !!count;
}
