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

#include <strings.h>
#include "aacenc.h"
#include "aacenc_tns.h"
#include "aactab.h"
#include "aacenc_utils.h"
#include "aacenc_quantization.h"

static inline void conv_to_int32(int32_t *loc, float *samples, int num, float norm)
{
    int i;
    for (i = 0; i < num; i++)
        loc[i] = ceilf((samples[i]/norm)*INT32_MAX);
}

static inline void conv_to_float(float *arr, int32_t *cof, int num)
{
    int i;
    for (i = 0; i < num; i++)
        arr[i] = (float)cof[i]/INT32_MAX;
}

/* Input: quantized 4 bit coef, output: 1 if first (MSB) 2 bits are the same */
static inline int coef_test_compression(int coef)
{
    int res = 0;
    /*coef = coef >> 3;
    res += ffs(coef);
    coef = coef >> 1;
    res += ffs(coef);*/
    return 0;
}

static inline int compress_coef(int *coefs, int num)
{
    int i, res = 0;
    for (i = 0; i < num; i++)
        res += coef_test_compression(coefs[i]);
    return 0;
}

/**
 * Encode TNS data.
 * Coefficient compression saves a single bit.
 */
void encode_tns_info(AACEncContext *s, SingleChannelElement *sce)
{
    int i, w, filt, coef_len, coef_compress;
    const int coef_res = MAX_LPC_PRECISION == 4 ? 1 : 0;
    const int is8 = sce->ics.window_sequence[0] == EIGHT_SHORT_SEQUENCE;

    put_bits(&s->pb, 1, !!sce->tns.present);

    if (!sce->tns.present)
        return;

    for (i = 0; i < sce->ics.num_windows; i++) {
        put_bits(&s->pb, 2 - is8, sce->tns.n_filt[i]);
        if (sce->tns.n_filt[i]) {
            put_bits(&s->pb, 1, !!coef_res);
            for (filt = 0; filt < sce->tns.n_filt[i]; filt++) {
                put_bits(&s->pb, 6 - 2 * is8, sce->tns.length[i][filt]);
                put_bits(&s->pb, 5 - 2 * is8, sce->tns.order[i][filt]);
                if (sce->tns.order[i][filt]) {
                    coef_compress = compress_coef(sce->tns.coef_idx[i][filt],
                                                  sce->tns.order[i][filt]);
                    put_bits(&s->pb, 1, !!sce->tns.direction[i][filt]);
                    put_bits(&s->pb, 1, !!coef_compress);
                    coef_len = coef_res + 3 - coef_compress;
                    for (w = 0; w < sce->tns.order[i][filt]; w++)
                        put_bits(&s->pb, coef_len, sce->tns.coef_idx[i][filt][w]);
                }
            }
        }
    }
}

static int process_tns_coeffs(TemporalNoiseShaping *tns, float *tns_coefs_raw,
                              int order, int w, int filt)
{
    int i, j;
    int *idx = tns->coef_idx[w][filt];
    float *lpc = tns->coef[w][filt];
    const int iqfac_p = ((1 << (MAX_LPC_PRECISION-1)) - 0.5)/(M_PI/2.0);
    const int iqfac_m = ((1 << (MAX_LPC_PRECISION-1)) + 0.5)/(M_PI/2.0);
    float temp[TNS_MAX_ORDER] = {0.0f}, out[TNS_MAX_ORDER] = {0.0f};

    /* Quantization */
    for (i = 0; i < order; i++) {
        idx[i] = ceilf(asin(tns_coefs_raw[i])*((tns_coefs_raw[i] >= 0) ? iqfac_p : iqfac_m));
        lpc[i] = 2*sin(idx[i]/((idx[i] >= 0) ? iqfac_p : iqfac_m));
    }

    /* Trim any coeff less than 0.1f from the end */
    for (i = order; i > -1; i--) {
        lpc[i] = (fabs(lpc[i]) > 0.1f) ? lpc[i] : 0.0f;
        if (lpc[i] != 0.0 ) {
            order = i;
            break;
        }
    }

    if (!order)
        return 0;

    /* Step up procedure, convert to LPC coeffs */
    out[0] = 1.0f;
    for (i = 1; i <= order; i++) {
        for (j = 1; j < i; j++) {
            temp[j] = out[j] + lpc[i]*out[i-j];
        }
        for (j = 1; j <= i; j++) {
            out[j] = temp[j];
        }
        out[i] = lpc[i-1];
    }
    memcpy(lpc, out, TNS_MAX_ORDER*sizeof(float));

    return order;
}

static void apply_tns_filter(float *out, float *in, int order, int direction,
                             float *tns_coefs, int ltp_used, int w, int filt, int start_i, int len)
{
    int i, j, inc, start = start_i;
    float tmp[TNS_MAX_ORDER+1];
    if (direction) {
        inc = -1;
        start = (start + len) - 1;
    } else {
        inc = 1;
    }
    if (!ltp_used) {    /* AR filter */
        for (i = 0; i < len; i++, start += inc)
            out[i] = in[start];
            for (j = 1; j <= FFMIN(i, order); j++)
                out[i] += tns_coefs[j]*in[start - j*inc];
    } else {            /* MA filter */
        for (i = 0; i < len; i++, start += inc) {
            tmp[0] = out[i] = in[start];
            for (j = 1; j <= FFMIN(i, order); j++)
                out[i] += tmp[j]*tns_coefs[j];
            for (j = order; j > 0; j--)
                tmp[j] = tmp[j - 1];
        }
    }
}

void search_for_tns(AACEncContext *s, SingleChannelElement *sce)
{
    TemporalNoiseShaping *tns = &sce->tns;
    int w, g, order, sfb_start, sfb_len, coef_start, shift[MAX_LPC_ORDER], count = 0;
    const int is8 = sce->ics.window_sequence[0] == EIGHT_SHORT_SEQUENCE;
    const int tns_max_order = is8 ? 7 : s->profile == FF_PROFILE_AAC_LOW ? 12 : TNS_MAX_ORDER;
    const float freq_mult = mpeg4audio_sample_rates[s->samplerate_index]/(1024.0f/sce->ics.num_windows)/2.0f;
    float max_coef = 0.0f;

    for (coef_start = 0; coef_start < 1024; coef_start++)
        max_coef = FFMAX(max_coef, sce->pcoeffs[coef_start]);

    for (w = 0; w < sce->ics.num_windows; w++) {
        int filters = 1, start = 0, coef_len = 0;
        int32_t conv_coeff[1024] = {0};
        int32_t coefs_t[MAX_LPC_ORDER][MAX_LPC_ORDER] = {{0}};

        /* Determine start sfb + coef - excludes anything below threshold */
        for (g = 0;  g < sce->ics.num_swb; g++) {
            if (start*freq_mult > TNS_LOW_LIMIT) {
                sfb_start = w*16+g;
                sfb_len   = (w+1)*16 + g - sfb_start;
                coef_start = sce->ics.swb_offset[sfb_start];
                coef_len  = sce->ics.swb_offset[sfb_start + sfb_len] - coef_start;
                break;
            }
            start += sce->ics.swb_sizes[g];
        }

        if (coef_len <= 0)
            continue;

        conv_to_int32(conv_coeff, &sce->pcoeffs[coef_start], coef_len, max_coef);

        /* LPC */
        order = ff_lpc_calc_coefs(&s->lpc, conv_coeff, coef_len,
                                  TNS_MIN_PRED_ORDER, tns_max_order,
                                  32, coefs_t, shift,
                                  FF_LPC_TYPE_LEVINSON, 10,
                                  ORDER_METHOD_EST, MAX_LPC_SHIFT, 0) - 1;

        /* Works surprisingly well, remember to tweak MAX_LPC_SHIFT if you want to play around with this */
        if (shift[order] > 3) {
            int direction = 0;
            float tns_coefs_raw[TNS_MAX_ORDER];
            tns->n_filt[w] = filters++;
            conv_to_float(tns_coefs_raw, coefs_t[order], order);
            for (g = 0; g < tns->n_filt[w]; g++) {
                process_tns_coeffs(tns, tns_coefs_raw, order, w, g);
                apply_tns_filter(&sce->coeffs[coef_start], sce->pcoeffs, order, direction, tns->coef[w][g],
                                 sce->ics.ltp.present, w, g, coef_start, coef_len);
                tns->order[w][g]     = order;
                tns->length[w][g]    = sfb_len;
                tns->direction[w][g] = direction;
            }
            count++;
        }
    }

    sce->tns.present = !!count;
}
