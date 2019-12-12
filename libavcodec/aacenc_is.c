/*
 * AAC encoder intensity stereo
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
 * AAC encoder Intensity Stereo
 * @author Rostislav Pehlivanov ( atomnuker gmail com )
 */

#include "aacenc.h"
#include "aacenc_utils.h"
#include "aacenc_is.h"
#include "aacenc_quantization.h"

struct AACISError ff_aac_is_encoding_err(AACEncContext *s, ChannelElement *cpe,
                                         int start, int w, int g, float ener0,
                                         float ener1, float ener01,
                                         int use_pcoeffs, int phase)
{
    int i, w2;
    SingleChannelElement *sce0 = &cpe->ch[0];
    SingleChannelElement *sce1 = &cpe->ch[1];
    float *L = use_pcoeffs ? sce0->pcoeffs : sce0->coeffs;
    float *R = use_pcoeffs ? sce1->pcoeffs : sce1->coeffs;
    float *L34 = &s->scoefs[256*0], *R34 = &s->scoefs[256*1];
    float *IS  = &s->scoefs[256*2], *I34 = &s->scoefs[256*3];
    float dist1 = 0.0f, dist2 = 0.0f;
    struct AACISError is_error = {0};

    if (ener01 <= 0 || ener0 <= 0) {
        is_error.pass = 0;
        return is_error;
    }

    for (w2 = 0; w2 < sce0->ics.group_len[w]; w2++) {
        FFPsyBand *band0 = &s->psy.ch[s->cur_channel+0].psy_bands[(w+w2)*16+g];
        FFPsyBand *band1 = &s->psy.ch[s->cur_channel+1].psy_bands[(w+w2)*16+g];
        int is_band_type, is_sf_idx = FFMAX(1, sce0->sf_idx[w*16+g]-4);
        float e01_34 = phase*pos_pow34(ener1/ener0);
        float maxval, dist_spec_err = 0.0f;
        float minthr = FFMIN(band0->threshold, band1->threshold);
        for (i = 0; i < sce0->ics.swb_sizes[g]; i++)
            IS[i] = (L[start+(w+w2)*128+i] + phase*R[start+(w+w2)*128+i])*sqrt(ener0/ener01);
        s->abs_pow34(L34, &L[start+(w+w2)*128], sce0->ics.swb_sizes[g]);
        s->abs_pow34(R34, &R[start+(w+w2)*128], sce0->ics.swb_sizes[g]);
        s->abs_pow34(I34, IS,                   sce0->ics.swb_sizes[g]);
        maxval = find_max_val(1, sce0->ics.swb_sizes[g], I34);
        is_band_type = find_min_book(maxval, is_sf_idx);
        dist1 += quantize_band_cost(s, &L[start + (w+w2)*128], L34,
                                    sce0->ics.swb_sizes[g],
                                    sce0->sf_idx[w*16+g],
                                    sce0->band_type[w*16+g],
                                    s->lambda / band0->threshold, INFINITY, NULL, NULL, 0);
        dist1 += quantize_band_cost(s, &R[start + (w+w2)*128], R34,
                                    sce1->ics.swb_sizes[g],
                                    sce1->sf_idx[w*16+g],
                                    sce1->band_type[w*16+g],
                                    s->lambda / band1->threshold, INFINITY, NULL, NULL, 0);
        dist2 += quantize_band_cost(s, IS, I34, sce0->ics.swb_sizes[g],
                                    is_sf_idx, is_band_type,
                                    s->lambda / minthr, INFINITY, NULL, NULL, 0);
        for (i = 0; i < sce0->ics.swb_sizes[g]; i++) {
            dist_spec_err += (L34[i] - I34[i])*(L34[i] - I34[i]);
            dist_spec_err += (R34[i] - I34[i]*e01_34)*(R34[i] - I34[i]*e01_34);
        }
        dist_spec_err *= s->lambda / minthr;
        dist2 += dist_spec_err;
    }

    is_error.pass = dist2 <= dist1;
    is_error.phase = phase;
    is_error.error = dist2 - dist1;
    is_error.dist1 = dist1;
    is_error.dist2 = dist2;
    is_error.ener01 = ener01;

    return is_error;
}

void ff_aac_search_for_is(AACEncContext *s, AVCodecContext *avctx, ChannelElement *cpe)
{
    SingleChannelElement *sce0 = &cpe->ch[0];
    SingleChannelElement *sce1 = &cpe->ch[1];
    int start = 0, count = 0, w, w2, g, i, prev_sf1 = -1, prev_bt = -1, prev_is = 0;
    const float freq_mult = avctx->sample_rate/(1024.0f/sce0->ics.num_windows)/2.0f;
    uint8_t nextband1[128];

    if (!cpe->common_window)
        return;

    /** Scout out next nonzero bands */
    ff_init_nextband_map(sce1, nextband1);

    for (w = 0; w < sce0->ics.num_windows; w += sce0->ics.group_len[w]) {
        start = 0;
        for (g = 0;  g < sce0->ics.num_swb; g++) {
            if (start*freq_mult > INT_STEREO_LOW_LIMIT*(s->lambda/170.0f) &&
                cpe->ch[0].band_type[w*16+g] != NOISE_BT && !cpe->ch[0].zeroes[w*16+g] &&
                cpe->ch[1].band_type[w*16+g] != NOISE_BT && !cpe->ch[1].zeroes[w*16+g] &&
                ff_sfdelta_can_remove_band(sce1, nextband1, prev_sf1, w*16+g)) {
                float ener0 = 0.0f, ener1 = 0.0f, ener01 = 0.0f, ener01p = 0.0f;
                struct AACISError ph_err1, ph_err2, *best;
                for (w2 = 0; w2 < sce0->ics.group_len[w]; w2++) {
                    for (i = 0; i < sce0->ics.swb_sizes[g]; i++) {
                        float coef0 = sce0->coeffs[start+(w+w2)*128+i];
                        float coef1 = sce1->coeffs[start+(w+w2)*128+i];
                        ener0  += coef0*coef0;
                        ener1  += coef1*coef1;
                        ener01 += (coef0 + coef1)*(coef0 + coef1);
                        ener01p += (coef0 - coef1)*(coef0 - coef1);
                    }
                }
                ph_err1 = ff_aac_is_encoding_err(s, cpe, start, w, g,
                                                 ener0, ener1, ener01p, 0, -1);
                ph_err2 = ff_aac_is_encoding_err(s, cpe, start, w, g,
                                                 ener0, ener1, ener01, 0, +1);
                best = (ph_err1.pass && ph_err1.error < ph_err2.error) ? &ph_err1 : &ph_err2;
                if (best->pass) {
                    cpe->is_mask[w*16+g] = 1;
                    cpe->ms_mask[w*16+g] = 0;
                    cpe->ch[0].is_ener[w*16+g] = sqrt(ener0 / best->ener01);
                    cpe->ch[1].is_ener[w*16+g] = ener0/ener1;
                    cpe->ch[1].band_type[w*16+g] = (best->phase > 0) ? INTENSITY_BT : INTENSITY_BT2;
                    if (prev_is && prev_bt != cpe->ch[1].band_type[w*16+g]) {
                        /** Flip M/S mask and pick the other CB, since it encodes more efficiently */
                        cpe->ms_mask[w*16+g] = 1;
                        cpe->ch[1].band_type[w*16+g] = (best->phase > 0) ? INTENSITY_BT2 : INTENSITY_BT;
                    }
                    prev_bt = cpe->ch[1].band_type[w*16+g];
                    count++;
                }
            }
            if (!sce1->zeroes[w*16+g] && sce1->band_type[w*16+g] < RESERVED_BT)
                prev_sf1 = sce1->sf_idx[w*16+g];
            prev_is = cpe->is_mask[w*16+g];
            start += sce0->ics.swb_sizes[g];
        }
    }
    cpe->is_mode = !!count;
}
