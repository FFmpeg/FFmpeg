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

#include "aacenc.h"
#include "aacenc_tns.h"
#include "aactab.h"
#include "aacenc_utils.h"
#include "aacenc_quantization.h"

static inline int compress_coef(int *coefs, int num)
{
    int i, c = 0;
    for (i = 0; i < num; i++)
        c += coefs[i] < 4 || coefs[i] > 11;
    return c == num;
}

/**
 * Encode TNS data.
 * Coefficient compression saves a single bit per coefficient.
 */
void ff_aac_encode_tns_info(AACEncContext *s, SingleChannelElement *sce)
{
    int i, w, filt, coef_len, coef_compress;
    const int is8 = sce->ics.window_sequence[0] == EIGHT_SHORT_SEQUENCE;

    if (!sce->tns.present)
        return;

    for (i = 0; i < sce->ics.num_windows; i++) {
        put_bits(&s->pb, 2 - is8, sce->tns.n_filt[i]);
        if (sce->tns.n_filt[i]) {
            put_bits(&s->pb, 1, 1);
            for (filt = 0; filt < sce->tns.n_filt[i]; filt++) {
                put_bits(&s->pb, 6 - 2 * is8, sce->tns.length[i][filt]);
                put_bits(&s->pb, 5 - 2 * is8, sce->tns.order[i][filt]);
                if (sce->tns.order[i][filt]) {
                    coef_compress = compress_coef(sce->tns.coef_idx[i][filt],
                                                  sce->tns.order[i][filt]);
                    put_bits(&s->pb, 1, !!sce->tns.direction[i][filt]);
                    put_bits(&s->pb, 1, !!coef_compress);
                    coef_len = 4 - coef_compress;
                    for (w = 0; w < sce->tns.order[i][filt]; w++)
                        put_bits(&s->pb, coef_len, sce->tns.coef_idx[i][filt][w]);
                }
            }
        }
    }
}

static void process_tns_coeffs(TemporalNoiseShaping *tns, double *coef_raw,
                               int *order_p, int w, int filt)
{
    int i, j, order = *order_p;
    int *idx = tns->coef_idx[w][filt];
    float *lpc = tns->coef[w][filt];
    float temp[TNS_MAX_ORDER] = {0.0f}, out[TNS_MAX_ORDER] = {0.0f};

    if (!order)
        return;

    /* Not what the specs say, but it's better */
    for (i = 0; i < order; i++) {
        idx[i] = quant_array_idx(coef_raw[i], tns_tmp2_map_0_4, 16);
        lpc[i] = tns_tmp2_map_0_4[idx[i]];
    }

    /* Trim any coeff less than 0.1f from the end */
    for (i = order-1; i > -1; i--) {
        lpc[i] = (fabs(lpc[i]) > 0.1f) ? lpc[i] : 0.0f;
        if (lpc[i] != 0.0 ) {
            order = i;
            break;
        }
    }

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
    *order_p = order;
    memcpy(lpc, out, TNS_MAX_ORDER*sizeof(float));
}

/* Apply TNS filter */
void ff_aac_apply_tns(SingleChannelElement *sce)
{
    const int mmm = FFMIN(sce->ics.tns_max_bands, sce->ics.max_sfb);
    float *coef = sce->pcoeffs;
    TemporalNoiseShaping *tns = &sce->tns;
    int w, filt, m, i;
    int bottom, top, order, start, end, size, inc;
    float *lpc, tmp[TNS_MAX_ORDER+1];

    return;

    for (w = 0; w < sce->ics.num_windows; w++) {
        bottom = sce->ics.num_swb;
        for (filt = 0; filt < tns->n_filt[w]; filt++) {
            top    = bottom;
            bottom = FFMAX(0, top - tns->length[w][filt]);
            order  = tns->order[w][filt];
            lpc    = tns->coef[w][filt];
            if (!order)
                continue;

            start = sce->ics.swb_offset[FFMIN(bottom, mmm)];
            end   = sce->ics.swb_offset[FFMIN(   top, mmm)];
            if ((size = end - start) <= 0)
                continue;
            if (tns->direction[w][filt]) {
                inc = -1;
                start = end - 1;
            } else {
                inc = 1;
            }
            start += w * 128;

            if (!sce->ics.ltp.present) {
                // ar filter
                for (m = 0; m < size; m++, start += inc)
                    for (i = 1; i <= FFMIN(m, order); i++)
                        coef[start] += coef[start - i * inc]*lpc[i - 1];
            } else {
                // ma filter
                for (m = 0; m < size; m++, start += inc) {
                    tmp[0] = coef[start];
                    for (i = 1; i <= FFMIN(m, order); i++)
                        coef[start] += tmp[i]*lpc[i - 1];
                    for (i = order; i > 0; i--)
                        tmp[i] = tmp[i - 1];
                }
            }
        }
    }
}

void ff_aac_search_for_tns(AACEncContext *s, SingleChannelElement *sce)
{
    TemporalNoiseShaping *tns = &sce->tns;
    int w, g, w2, prev_end_sfb = 0, count = 0;
    const int is8 = sce->ics.window_sequence[0] == EIGHT_SHORT_SEQUENCE;
    const int tns_max_order = is8 ? 7 : s->profile == FF_PROFILE_AAC_LOW ? 12 : TNS_MAX_ORDER;

    for (w = 0; w < sce->ics.num_windows; w++) {
        int order = 0, filters = 1;
        int sfb_start = 0, sfb_len = 0;
        int coef_start = 0, coef_len = 0;
        float energy = 0.0f, threshold = 0.0f;
        double coefs[MAX_LPC_ORDER][MAX_LPC_ORDER] = {{0}};
        for (g = 0;  g < sce->ics.num_swb; g++) {
            if (!sfb_start && w*16+g > TNS_LOW_LIMIT && w*16+g > prev_end_sfb) {
                sfb_start = w*16+g;
                coef_start =  sce->ics.swb_offset[sfb_start];
            }
            if (sfb_start) {
                for (w2 = 0; w2 < sce->ics.group_len[w]; w2++) {
                    FFPsyBand *band = &s->psy.ch[s->cur_channel].psy_bands[(w+w2)*16+g];
                    if (!sfb_len && band->energy < band->threshold*1.3f) {
                        sfb_len = (w+w2)*16+g - sfb_start;
                        prev_end_sfb = sfb_start + sfb_len;
                        coef_len = sce->ics.swb_offset[sfb_start + sfb_len] - coef_start;
                        break;
                    }
                    energy += band->energy;
                    threshold += band->threshold;
                }
                if (!sfb_len) {
                    sfb_len = (w+1)*16+g - sfb_start - 1;
                    coef_len = sce->ics.swb_offset[sfb_start + sfb_len] - coef_start;
                }
            }
        }

        if (sfb_len <= 0 || coef_len <= 0)
            continue;
        if (coef_start + coef_len > 1024)
            coef_len = 1024 - coef_start;

        /* LPC */
        order = ff_lpc_calc_levinsion(&s->lpc, &sce->coeffs[coef_start], coef_len,
                                      coefs, 0, tns_max_order, ORDER_METHOD_LOG);

        if (energy > threshold) {
            int direction = 0;
            tns->n_filt[w] = filters++;
            for (g = 0; g < tns->n_filt[w]; g++) {
                process_tns_coeffs(tns, coefs[order], &order, w, g);
                tns->order[w][g]     = order;
                tns->length[w][g]    = sfb_len;
                tns->direction[w][g] = direction;
            }
            count++;
        }
    }

    sce->tns.present = !!count;
}
