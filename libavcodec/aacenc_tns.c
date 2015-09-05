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

/**
 * Encode TNS data.
 * Coefficient compression saves a single bit per coefficient.
 */
void ff_aac_encode_tns_info(AACEncContext *s, SingleChannelElement *sce)
{
    uint8_t u_coef;
    const uint8_t coef_res = TNS_Q_BITS == 4;
    int i, w, filt, coef_len, coef_compress = 0;
    const int is8 = sce->ics.window_sequence[0] == EIGHT_SHORT_SEQUENCE;
    TemporalNoiseShaping *tns = &sce->tns;

    if (!sce->tns.present)
        return;

    for (i = 0; i < sce->ics.num_windows; i++) {
        put_bits(&s->pb, 2 - is8, sce->tns.n_filt[i]);
        if (tns->n_filt[i]) {
            put_bits(&s->pb, 1, coef_res);
            for (filt = 0; filt < tns->n_filt[i]; filt++) {
                put_bits(&s->pb, 6 - 2 * is8, tns->length[i][filt]);
                put_bits(&s->pb, 5 - 2 * is8, tns->order[i][filt]);
                if (tns->order[i][filt]) {
                    put_bits(&s->pb, 1, !!tns->direction[i][filt]);
                    put_bits(&s->pb, 1, !!coef_compress);
                    coef_len = coef_res + 3 - coef_compress;
                    for (w = 0; w < tns->order[i][filt]; w++) {
                        u_coef = (tns->coef_idx[i][filt][w])&(~(~0<<coef_len));
                        put_bits(&s->pb, coef_len, u_coef);
                    }
                }
            }
        }
    }
}

static inline void quantize_coefs(double *coef, int *idx, float *lpc, int order)
{
    int i;
    uint8_t u_coef;
    const float *quant_arr = tns_tmp2_map[TNS_Q_BITS == 4];
    const double iqfac_p = ((1 << (TNS_Q_BITS-1)) - 0.5)/(M_PI/2.0);
    const double iqfac_m = ((1 << (TNS_Q_BITS-1)) + 0.5)/(M_PI/2.0);
    for (i = 0; i < order; i++) {
        idx[i] = ceilf(asin(coef[i])*((coef[i] >= 0) ? iqfac_p : iqfac_m));
        u_coef = (idx[i])&(~(~0<<TNS_Q_BITS));
        lpc[i] = quant_arr[u_coef];
    }
}

/* Apply TNS filter */
void ff_aac_apply_tns(AACEncContext *s, SingleChannelElement *sce)
{
    TemporalNoiseShaping *tns = &sce->tns;
    IndividualChannelStream *ics = &sce->ics;
    int w, filt, m, i, top, order, bottom, start, end, size, inc;
    const int mmm = FFMIN(ics->tns_max_bands, ics->max_sfb);
    float lpc[TNS_MAX_ORDER];

    for (w = 0; w < ics->num_windows; w++) {
        bottom = ics->num_swb;
        for (filt = 0; filt < tns->n_filt[w]; filt++) {
            top    = bottom;
            bottom = FFMAX(0, top - tns->length[w][filt]);
            order  = tns->order[w][filt];
            if (order == 0)
                continue;

            // tns_decode_coef
            compute_lpc_coefs(tns->coef[w][filt], order, lpc, 0, 0, 0);

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

            // ar filter
            for (m = 0; m < size; m++, start += inc)
                for (i = 1; i <= FFMIN(m, order); i++)
                    sce->coeffs[start] += lpc[i-1]*sce->pcoeffs[start - i*inc];
        }
    }
}

void ff_aac_search_for_tns(AACEncContext *s, SingleChannelElement *sce)
{
    TemporalNoiseShaping *tns = &sce->tns;
    int w, w2, g, count = 0;
    const int mmm = FFMIN(sce->ics.tns_max_bands, sce->ics.max_sfb);
    const int is8 = sce->ics.window_sequence[0] == EIGHT_SHORT_SEQUENCE;
    const int order = is8 ? 7 : s->profile == FF_PROFILE_AAC_LOW ? 12 : TNS_MAX_ORDER;

    int sfb_start = av_clip(tns_min_sfb[is8][s->samplerate_index], 0, mmm);
    int sfb_end   = av_clip(sce->ics.num_swb, 0, mmm);

    for (w = 0; w < sce->ics.num_windows; w++) {
        float e_ratio = 0.0f, threshold = 0.0f, spread = 0.0f, en[2] = {0.0, 0.0f};
        double gain = 0.0f, coefs[MAX_LPC_ORDER] = {0};
        int coef_start = w*sce->ics.num_swb + sce->ics.swb_offset[sfb_start];
        int coef_len = sce->ics.swb_offset[sfb_end] - sce->ics.swb_offset[sfb_start];

        for (g = 0;  g < sce->ics.num_swb; g++) {
            if (w*16+g < sfb_start || w*16+g > sfb_end)
                continue;
            for (w2 = 0; w2 < sce->ics.group_len[w]; w2++) {
                FFPsyBand *band = &s->psy.ch[s->cur_channel].psy_bands[(w+w2)*16+g];
                if ((w+w2)*16+g > sfb_start + ((sfb_end - sfb_start)/2))
                    en[1] += band->energy;
                else
                    en[0] += band->energy;
                threshold += band->threshold;
                spread += band->spread;
            }
        }

        if (coef_len <= 0 || (sfb_end - sfb_start) <= 0)
            continue;
        else
            e_ratio = en[0]/en[1];

        /* LPC */
        gain = ff_lpc_calc_ref_coefs_f(&s->lpc, &sce->coeffs[coef_start],
                                       coef_len, order, coefs);

        if (gain > TNS_GAIN_THRESHOLD_LOW && gain < TNS_GAIN_THRESHOLD_HIGH &&
            (en[0]+en[1]) > TNS_GAIN_THRESHOLD_LOW*threshold &&
            spread < TNS_SPREAD_THRESHOLD && order) {
            if (is8 || order < 2 || (e_ratio > TNS_E_RATIO_LOW && e_ratio < TNS_E_RATIO_HIGH)) {
                tns->n_filt[w] = 1;
                for (g = 0; g < tns->n_filt[w]; g++) {
                    tns->length[w][g] = sfb_end - sfb_start;
                    tns->direction[w][g] = en[0] < en[1];
                    tns->order[w][g] = order;
                    quantize_coefs(coefs, tns->coef_idx[w][g], tns->coef[w][g],
                                   order);
                }
            } else {  /* 2 filters due to energy disbalance */
                tns->n_filt[w] = 2;
                for (g = 0; g < tns->n_filt[w]; g++) {
                    tns->direction[w][g] = en[g] < en[!g];
                    tns->order[w][g] = !g ? order/2 : order - tns->order[w][g-1];
                    tns->length[w][g] = !g ? (sfb_end - sfb_start)/2 : \
                                    (sfb_end - sfb_start) - tns->length[w][g-1];
                    quantize_coefs(&coefs[!g ? 0 : order - tns->order[w][g-1]],
                                   tns->coef_idx[w][g], tns->coef[w][g],
                                   tns->order[w][g]);
                }
            }
            count++;
        }
    }

    sce->tns.present = !!count;
}
