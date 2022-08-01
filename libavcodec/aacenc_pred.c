/*
 * AAC encoder main-type prediction
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
 * AAC encoder main-type prediction
 * @author Rostislav Pehlivanov ( atomnuker gmail com )
 */

#include "aactab.h"
#include "aacenc_pred.h"
#include "aacenc_utils.h"
#include "aacenc_is.h"            /* <- Needed for common window distortions */
#include "aacenc_quantization.h"

#define RESTORE_PRED(sce, sfb) \
        if (sce->ics.prediction_used[sfb]) {\
            sce->ics.prediction_used[sfb] = 0;\
            sce->band_type[sfb] = sce->band_alt[sfb];\
        }

static inline float flt16_round(float pf)
{
    union av_intfloat32 tmp;
    tmp.f = pf;
    tmp.i = (tmp.i + 0x00008000U) & 0xFFFF0000U;
    return tmp.f;
}

static inline float flt16_even(float pf)
{
    union av_intfloat32 tmp;
    tmp.f = pf;
    tmp.i = (tmp.i + 0x00007FFFU + (tmp.i & 0x00010000U >> 16)) & 0xFFFF0000U;
    return tmp.f;
}

static inline float flt16_trunc(float pf)
{
    union av_intfloat32 pun;
    pun.f = pf;
    pun.i &= 0xFFFF0000U;
    return pun.f;
}

static inline void predict(PredictorState *ps, float *coef, float *rcoef, int set)
{
    float k2;
    const float a     = 0.953125; // 61.0 / 64
    const float alpha = 0.90625;  // 29.0 / 32
    const float   k1 = ps->k1;
    const float   r0 = ps->r0,     r1 = ps->r1;
    const float cor0 = ps->cor0, cor1 = ps->cor1;
    const float var0 = ps->var0, var1 = ps->var1;
    const float e0 = *coef - ps->x_est;
    const float e1 = e0 - k1 * r0;

    if (set)
        *coef = e0;

    ps->cor1 = flt16_trunc(alpha * cor1 + r1 * e1);
    ps->var1 = flt16_trunc(alpha * var1 + 0.5f * (r1 * r1 + e1 * e1));
    ps->cor0 = flt16_trunc(alpha * cor0 + r0 * e0);
    ps->var0 = flt16_trunc(alpha * var0 + 0.5f * (r0 * r0 + e0 * e0));
    ps->r1   = flt16_trunc(a * (r0 - k1 * e0));
    ps->r0   = flt16_trunc(a * e0);

    /* Prediction for next frame */
    ps->k1   = ps->var0 > 1 ? ps->cor0 * flt16_even(a / ps->var0) : 0;
    k2       = ps->var1 > 1 ? ps->cor1 * flt16_even(a / ps->var1) : 0;
    *rcoef   = ps->x_est = flt16_round(ps->k1*ps->r0 + k2*ps->r1);
}

static inline void reset_predict_state(PredictorState *ps)
{
    ps->r0    = 0.0f;
    ps->r1    = 0.0f;
    ps->k1    = 0.0f;
    ps->cor0  = 0.0f;
    ps->cor1  = 0.0f;
    ps->var0  = 1.0f;
    ps->var1  = 1.0f;
    ps->x_est = 0.0f;
}

static inline void reset_all_predictors(PredictorState *ps)
{
    int i;
    for (i = 0; i < MAX_PREDICTORS; i++)
        reset_predict_state(&ps[i]);
}

static inline void reset_predictor_group(SingleChannelElement *sce, int group_num)
{
    int i;
    PredictorState *ps = sce->predictor_state;
    for (i = group_num - 1; i < MAX_PREDICTORS; i += 30)
        reset_predict_state(&ps[i]);
}

void ff_aac_apply_main_pred(AACEncContext *s, SingleChannelElement *sce)
{
    int sfb, k;
    const int pmax = FFMIN(sce->ics.max_sfb, ff_aac_pred_sfb_max[s->samplerate_index]);

    if (sce->ics.window_sequence[0] != EIGHT_SHORT_SEQUENCE) {
        for (sfb = 0; sfb < pmax; sfb++) {
            for (k = sce->ics.swb_offset[sfb]; k < sce->ics.swb_offset[sfb + 1]; k++) {
                predict(&sce->predictor_state[k], &sce->coeffs[k], &sce->prcoeffs[k],
                        sce->ics.predictor_present && sce->ics.prediction_used[sfb]);
            }
        }
        if (sce->ics.predictor_reset_group) {
            reset_predictor_group(sce, sce->ics.predictor_reset_group);
        }
    } else {
        reset_all_predictors(sce->predictor_state);
    }
}

/* If inc = 0 you can check if this returns 0 to see if you can reset freely */
static inline int update_counters(IndividualChannelStream *ics, int inc)
{
    int i;
    for (i = 1; i < 31; i++) {
        ics->predictor_reset_count[i] += inc;
        if (ics->predictor_reset_count[i] > PRED_RESET_FRAME_MIN)
            return i; /* Reset this immediately */
    }
    return 0;
}

void ff_aac_adjust_common_pred(AACEncContext *s, ChannelElement *cpe)
{
    int start, w, w2, g, i, count = 0;
    SingleChannelElement *sce0 = &cpe->ch[0];
    SingleChannelElement *sce1 = &cpe->ch[1];
    const int pmax0 = FFMIN(sce0->ics.max_sfb, ff_aac_pred_sfb_max[s->samplerate_index]);
    const int pmax1 = FFMIN(sce1->ics.max_sfb, ff_aac_pred_sfb_max[s->samplerate_index]);
    const int pmax  = FFMIN(pmax0, pmax1);

    if (!cpe->common_window ||
        sce0->ics.window_sequence[0] == EIGHT_SHORT_SEQUENCE ||
        sce1->ics.window_sequence[0] == EIGHT_SHORT_SEQUENCE)
        return;

    for (w = 0; w < sce0->ics.num_windows; w += sce0->ics.group_len[w]) {
        start = 0;
        for (g = 0; g < sce0->ics.num_swb; g++) {
            int sfb = w*16+g;
            int sum = sce0->ics.prediction_used[sfb] + sce1->ics.prediction_used[sfb];
            float ener0 = 0.0f, ener1 = 0.0f, ener01 = 0.0f;
            struct AACISError ph_err1, ph_err2, *erf;
            if (sfb < PRED_SFB_START || sfb > pmax || sum != 2) {
                RESTORE_PRED(sce0, sfb);
                RESTORE_PRED(sce1, sfb);
                start += sce0->ics.swb_sizes[g];
                continue;
            }
            for (w2 = 0; w2 < sce0->ics.group_len[w]; w2++) {
                for (i = 0; i < sce0->ics.swb_sizes[g]; i++) {
                    float coef0 = sce0->pcoeffs[start+(w+w2)*128+i];
                    float coef1 = sce1->pcoeffs[start+(w+w2)*128+i];
                    ener0  += coef0*coef0;
                    ener1  += coef1*coef1;
                    ener01 += (coef0 + coef1)*(coef0 + coef1);
                }
            }
            ph_err1 = ff_aac_is_encoding_err(s, cpe, start, w, g,
                                             ener0, ener1, ener01, 1, -1);
            ph_err2 = ff_aac_is_encoding_err(s, cpe, start, w, g,
                                             ener0, ener1, ener01, 1, +1);
            erf = ph_err1.error < ph_err2.error ? &ph_err1 : &ph_err2;
            if (erf->pass) {
                sce0->ics.prediction_used[sfb] = 1;
                sce1->ics.prediction_used[sfb] = 1;
                count++;
            } else {
                RESTORE_PRED(sce0, sfb);
                RESTORE_PRED(sce1, sfb);
            }
            start += sce0->ics.swb_sizes[g];
        }
    }

    sce1->ics.predictor_present = sce0->ics.predictor_present = !!count;
}

static void update_pred_resets(SingleChannelElement *sce)
{
    int i, max_group_id_c, max_frame = 0;
    float avg_frame = 0.0f;
    IndividualChannelStream *ics = &sce->ics;

    /* Update the counters and immediately update any frame behind schedule */
    if ((ics->predictor_reset_group = update_counters(&sce->ics, 1)))
        return;

    for (i = 1; i < 31; i++) {
        /* Count-based */
        if (ics->predictor_reset_count[i] > max_frame) {
            max_group_id_c = i;
            max_frame = ics->predictor_reset_count[i];
        }
        avg_frame = (ics->predictor_reset_count[i] + avg_frame)/2;
    }

    if (max_frame > PRED_RESET_MIN) {
        ics->predictor_reset_group = max_group_id_c;
    } else {
        ics->predictor_reset_group = 0;
    }
}

void ff_aac_search_for_pred(AACEncContext *s, SingleChannelElement *sce)
{
    int sfb, i, count = 0, cost_coeffs = 0, cost_pred = 0;
    const int pmax = FFMIN(sce->ics.max_sfb, ff_aac_pred_sfb_max[s->samplerate_index]);
    float *O34  = &s->scoefs[128*0], *P34 = &s->scoefs[128*1];
    float *SENT = &s->scoefs[128*2], *S34 = &s->scoefs[128*3];
    float *QERR = &s->scoefs[128*4];

    if (sce->ics.window_sequence[0] == EIGHT_SHORT_SEQUENCE) {
        sce->ics.predictor_present = 0;
        return;
    }

    if (!sce->ics.predictor_initialized) {
        reset_all_predictors(sce->predictor_state);
        sce->ics.predictor_initialized = 1;
        memcpy(sce->prcoeffs, sce->coeffs, 1024*sizeof(float));
        for (i = 1; i < 31; i++)
            sce->ics.predictor_reset_count[i] = i;
    }

    update_pred_resets(sce);
    memcpy(sce->band_alt, sce->band_type, sizeof(sce->band_type));

    for (sfb = PRED_SFB_START; sfb < pmax; sfb++) {
        int cost1, cost2, cb_p;
        float dist1, dist2, dist_spec_err = 0.0f;
        const int cb_n = sce->zeroes[sfb] ? 0 : sce->band_type[sfb];
        const int cb_min = sce->zeroes[sfb] ? 0 : 1;
        const int cb_max = sce->zeroes[sfb] ? 0 : RESERVED_BT;
        const int start_coef = sce->ics.swb_offset[sfb];
        const int num_coeffs = sce->ics.swb_offset[sfb + 1] - start_coef;
        const FFPsyBand *band = &s->psy.ch[s->cur_channel].psy_bands[sfb];

        if (start_coef + num_coeffs > MAX_PREDICTORS ||
            (s->cur_channel && sce->band_type[sfb] >= INTENSITY_BT2) ||
            sce->band_type[sfb] == NOISE_BT)
            continue;

        /* Normal coefficients */
        s->abs_pow34(O34, &sce->coeffs[start_coef], num_coeffs);
        dist1 = ff_quantize_and_encode_band_cost(s, NULL, &sce->coeffs[start_coef], NULL,
                                                 O34, num_coeffs, sce->sf_idx[sfb],
                                                 cb_n, s->lambda / band->threshold, INFINITY, &cost1, NULL);
        cost_coeffs += cost1;

        /* Encoded coefficients - needed for #bits, band type and quant. error */
        for (i = 0; i < num_coeffs; i++)
            SENT[i] = sce->coeffs[start_coef + i] - sce->prcoeffs[start_coef + i];
        s->abs_pow34(S34, SENT, num_coeffs);
        if (cb_n < RESERVED_BT)
            cb_p = av_clip(find_min_book(find_max_val(1, num_coeffs, S34), sce->sf_idx[sfb]), cb_min, cb_max);
        else
            cb_p = cb_n;
        ff_quantize_and_encode_band_cost(s, NULL, SENT, QERR, S34, num_coeffs,
                                         sce->sf_idx[sfb], cb_p, s->lambda / band->threshold, INFINITY,
                                         &cost2, NULL);

        /* Reconstructed coefficients - needed for distortion measurements */
        for (i = 0; i < num_coeffs; i++)
            sce->prcoeffs[start_coef + i] += QERR[i] != 0.0f ? (sce->prcoeffs[start_coef + i] - QERR[i]) : 0.0f;
        s->abs_pow34(P34, &sce->prcoeffs[start_coef], num_coeffs);
        if (cb_n < RESERVED_BT)
            cb_p = av_clip(find_min_book(find_max_val(1, num_coeffs, P34), sce->sf_idx[sfb]), cb_min, cb_max);
        else
            cb_p = cb_n;
        dist2 = ff_quantize_and_encode_band_cost(s, NULL, &sce->prcoeffs[start_coef], NULL,
                                                 P34, num_coeffs, sce->sf_idx[sfb],
                                                 cb_p, s->lambda / band->threshold, INFINITY, NULL, NULL);
        for (i = 0; i < num_coeffs; i++)
            dist_spec_err += (O34[i] - P34[i])*(O34[i] - P34[i]);
        dist_spec_err *= s->lambda / band->threshold;
        dist2 += dist_spec_err;

        if (dist2 <= dist1 && cb_p <= cb_n) {
            cost_pred += cost2;
            sce->ics.prediction_used[sfb] = 1;
            sce->band_alt[sfb]  = cb_n;
            sce->band_type[sfb] = cb_p;
            count++;
        } else {
            cost_pred += cost1;
            sce->band_alt[sfb] = cb_p;
        }
    }

    if (count && cost_coeffs < cost_pred) {
        count = 0;
        for (sfb = PRED_SFB_START; sfb < pmax; sfb++)
            RESTORE_PRED(sce, sfb);
        memset(&sce->ics.prediction_used, 0, sizeof(sce->ics.prediction_used));
    }

    sce->ics.predictor_present = !!count;
}

/**
 * Encoder predictors data.
 */
void ff_aac_encode_main_pred(AACEncContext *s, SingleChannelElement *sce)
{
    int sfb;
    IndividualChannelStream *ics = &sce->ics;
    const int pmax = FFMIN(ics->max_sfb, ff_aac_pred_sfb_max[s->samplerate_index]);

    if (s->profile != FF_PROFILE_AAC_MAIN ||
        !ics->predictor_present)
        return;

    put_bits(&s->pb, 1, !!ics->predictor_reset_group);
    if (ics->predictor_reset_group)
        put_bits(&s->pb, 5, ics->predictor_reset_group);
    for (sfb = 0; sfb < pmax; sfb++)
        put_bits(&s->pb, 1, ics->prediction_used[sfb]);
}
