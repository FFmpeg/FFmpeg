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
 * AAC encoder main prediction
 * @author Rostislav Pehlivanov ( atomnuker gmail com )
 */

#include "aactab.h"
#include "aacenc_pred.h"
#include "aacenc_utils.h"
#include "aacenc_quantization.h"

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

static inline void predict(PredictorState *ps, float *coef, float *rcoef,
                           int output_enable)
{
    const float a     = 0.953125; // 61.0 / 64
    float k2;
    float   r0 = ps->r0,     r1 = ps->r1;
    float cor0 = ps->cor0, cor1 = ps->cor1;
    float var0 = ps->var0, var1 = ps->var1;

    ps->k1 = var0 > 1 ? cor0 * flt16_even(a / var0) : 0;
        k2 = var1 > 1 ? cor1 * flt16_even(a / var1) : 0;

    ps->x_est = flt16_round(ps->k1*r0 + k2*r1);

    if (output_enable)
        *coef -= ps->x_est;
    else
        *rcoef = *coef - ps->x_est;
}

static inline void update_predictor(PredictorState *ps, float qcoef)
{
    const float alpha = 0.90625;  // 29.0 / 32
    const float a     = 0.953125; // 61.0 / 64
    float k1 = ps->k1;
    float r0 = ps->r0;
    float r1 = ps->r1;
    float e0 = qcoef + ps->x_est;
    float e1 = e0 - k1 * r0;
    float cor0 = ps->cor0, cor1 = ps->cor1;
    float var0 = ps->var0, var1 = ps->var1;

    ps->cor1 = flt16_trunc(alpha * cor1 + r1 * e1);
    ps->var1 = flt16_trunc(alpha * var1 + 0.5f * (r1 * r1 + e1 * e1));
    ps->cor0 = flt16_trunc(alpha * cor0 + r0 * e0);
    ps->var0 = flt16_trunc(alpha * var0 + 0.5f * (r0 * r0 + e0 * e0));

    ps->r1 = flt16_trunc(a * (r0 - k1 * e0));
    ps->r0 = flt16_trunc(a * e0);
}

static inline void reset_predict_state(PredictorState *ps)
{
    ps->r0   = 0.0f;
    ps->r1   = 0.0f;
    ps->cor0 = 0.0f;
    ps->cor1 = 0.0f;
    ps->var0 = 1.0f;
    ps->var1 = 1.0f;
    ps->k1   = 0.0f;
    ps->x_est= 0.0f;
}

static inline void reset_all_predictors(SingleChannelElement *sce)
{
    int i;
    for (i = 0; i < MAX_PREDICTORS; i++)
        reset_predict_state(&sce->predictor_state[i]);
    for (i = 1; i < 31; i++)
        sce->ics.predictor_reset_count[i] = 0;
}

static inline void reset_predictor_group(SingleChannelElement *sce, int group_num)
{
    int i;
    PredictorState *ps = sce->predictor_state;
    sce->ics.predictor_reset_count[group_num] = 0;
    for (i = group_num - 1; i < MAX_PREDICTORS; i += 30)
        reset_predict_state(&ps[i]);
}

void ff_aac_apply_main_pred(AACEncContext *s, SingleChannelElement *sce)
{
    int sfb, k;

    if (sce->ics.window_sequence[0] != EIGHT_SHORT_SEQUENCE) {
        for (sfb = 0; sfb < ff_aac_pred_sfb_max[s->samplerate_index]; sfb++) {
            for (k = sce->ics.swb_offset[sfb]; k < sce->ics.swb_offset[sfb + 1]; k++)
                predict(&sce->predictor_state[k], &sce->coeffs[k], &sce->prcoeffs[k],
                        (sce->ics.predictor_present && sce->ics.prediction_used[sfb]));
        }
    }
}

static void decode_joint_stereo(ChannelElement *cpe)
{
    int i, w, w2, g;
    SingleChannelElement *sce0 = &cpe->ch[0];
    SingleChannelElement *sce1 = &cpe->ch[1];
    IndividualChannelStream *ics;

    for (i = 0; i < MAX_PREDICTORS; i++)
        sce0->prcoeffs[i] = sce0->predictor_state[i].x_est;

    ics = &sce0->ics;
    for (w = 0; w < ics->num_windows; w += ics->group_len[w]) {
        for (w2 =  0; w2 < ics->group_len[w]; w2++) {
            int start = (w+w2) * 128;
            for (g = 0; g < ics->num_swb; g++) {
                int sfb = w*16 + g;
                //apply Intensity stereo coeffs transformation
                if (cpe->is_mask[sfb]) {
                    int p = -1 + 2 * (sce1->band_type[sfb] - 14);
                    float rscale = ff_aac_pow2sf_tab[-sce1->sf_idx[sfb] + POW_SF2_ZERO];
                    p *= 1 - 2 * cpe->ms_mask[sfb];
                    for (i = 0; i < ics->swb_sizes[g]; i++) {
                        sce0->pqcoeffs[start+i] = (sce0->prcoeffs[start+i] + p*sce0->pqcoeffs[start+i]) * rscale;
                    }
                } else if (cpe->ms_mask[sfb] &&
                           sce0->band_type[sfb] < NOISE_BT &&
                           sce1->band_type[sfb] < NOISE_BT) {
                    for (i = 0; i < ics->swb_sizes[g]; i++) {
                        float L = sce0->pqcoeffs[start+i] + sce1->pqcoeffs[start+i];
                        float R = sce0->pqcoeffs[start+i] - sce1->pqcoeffs[start+i];
                        sce0->pqcoeffs[start+i] = L;
                        sce1->pqcoeffs[start+i] = R;
                    }
                }
                start += ics->swb_sizes[g];
            }
        }
    }
}

static inline void prepare_predictors(SingleChannelElement *sce)
{
    int k;
    for (k = 0; k < MAX_PREDICTORS; k++)
        predict(&sce->predictor_state[k], &sce->coeffs[k], &sce->prcoeffs[k], 0);
}

void ff_aac_update_main_pred(AACEncContext *s, SingleChannelElement *sce, ChannelElement *cpe)
{
    int k;

    if (sce->ics.window_sequence[0] == EIGHT_SHORT_SEQUENCE)
        return;

    if (cpe && cpe->common_window)
        decode_joint_stereo(cpe);

    for (k = 0; k < MAX_PREDICTORS; k++)
        update_predictor(&sce->predictor_state[k], sce->pqcoeffs[k]);

    if (sce->ics.window_sequence[0] == EIGHT_SHORT_SEQUENCE) {
        reset_all_predictors(sce);
    }

    if (sce->ics.predictor_reset_group)
        reset_predictor_group(sce, sce->ics.predictor_reset_group);
}

/* If inc == 0 check if it returns 0 to see if you can reset freely */
static inline int update_counters(IndividualChannelStream *ics, int inc)
{
    int i, rg = 0;
    for (i = 1; i < 31; i++) {
        ics->predictor_reset_count[i] += inc;
        if (!rg && ics->predictor_reset_count[i] > PRED_RESET_FRAME_MIN)
            rg = i; /* Reset this immediately */
    }
    return rg;
}

void ff_aac_adjust_common_prediction(AACEncContext *s, ChannelElement *cpe)
{
    int start, w, g, count = 0;
    SingleChannelElement *sce0 = &cpe->ch[0];
    SingleChannelElement *sce1 = &cpe->ch[1];

    if (!cpe->common_window || sce0->ics.window_sequence[0] == EIGHT_SHORT_SEQUENCE)
        return;

    /* Predict if IS or MS is on and at least one channel is marked or when both are */
    for (w = 0; w < sce0->ics.num_windows; w += sce0->ics.group_len[w]) {
        start = 0;
        for (g = 0; g < sce0->ics.num_swb; g++) {
            int sfb = w*16+g;
            if (sfb < PRED_SFB_START || sfb > ff_aac_pred_sfb_max[s->samplerate_index]) {
                ;
            } else if ((cpe->is_mask[sfb] || cpe->ms_mask[sfb]) &&
                (sce0->ics.prediction_used[sfb] || sce1->ics.prediction_used[sfb])) {
                sce0->ics.prediction_used[sfb] = sce1->ics.prediction_used[sfb] = 1;
                count++;
            } else if (sce0->ics.prediction_used[sfb] && sce1->ics.prediction_used[sfb]) {
                count++;
            } else {
                /* Restore band types, if changed - prediction never sets > RESERVED_BT */
                if (sce0->ics.prediction_used[sfb] && sce0->band_type[sfb] < RESERVED_BT)
                    sce0->band_type[sfb] = sce0->orig_band_type[sfb];
                if (sce1->ics.prediction_used[sfb] && sce1->band_type[sfb] < RESERVED_BT)
                    sce1->band_type[sfb] = sce1->orig_band_type[sfb];
                sce0->ics.prediction_used[sfb] = sce1->ics.prediction_used[sfb] = 0;
            }
            start += sce0->ics.swb_sizes[g];
        }
    }

    sce1->ics.predictor_present = sce0->ics.predictor_present = !!count;

    if (!count)
        return;

    sce1->ics.predictor_reset_group = sce0->ics.predictor_reset_group;
}

static void update_pred_resets(SingleChannelElement *sce)
{
    int i, max_group_id_c, max_frame = 0;
    float avg_frame = 0.0f;
    IndividualChannelStream *ics = &sce->ics;

    /* Some other code probably chose the reset group */
    if (ics->predictor_reset_group)
        return;

    if ((ics->predictor_reset_group = update_counters(&sce->ics, 1)))
        return;

    for (i = 1; i < 31; i++) {
        if (ics->predictor_reset_count[i] > max_frame) {
            max_group_id_c = i;
            max_frame = ics->predictor_reset_count[i];
        }
        avg_frame = (ics->predictor_reset_count[i] + avg_frame)/2;
    }

    if (avg_frame*2 > max_frame && max_frame > PRED_RESET_MIN ||
        max_frame > (2*PRED_RESET_MIN)/3) {
        ics->predictor_reset_group = max_group_id_c;
    } else {
        ics->predictor_reset_group = 0;
    }
}

void ff_aac_search_for_pred(AACEncContext *s, SingleChannelElement *sce)
{
    int sfb, i, count = 0;
    float *O34  = &s->scoefs[256*0], *P34  = &s->scoefs[256*1];
    int cost_coeffs = PRICE_OFFSET;
    int cost_pred = 1+(sce->ics.predictor_reset_group ? 5 : 0) +
                  FFMIN(sce->ics.max_sfb, ff_aac_pred_sfb_max[s->samplerate_index]);

    memcpy(sce->orig_band_type, sce->band_type, 128*sizeof(enum BandType));

    if (!sce->ics.predictor_initialized ||
        sce->ics.window_sequence[0] == EIGHT_SHORT_SEQUENCE) {
        reset_all_predictors(sce);
        for (i = 1; i < 31; i++)
            sce->ics.predictor_reset_count[i] = i;
        sce->ics.predictor_initialized = 1;
    }

    update_pred_resets(sce);
    prepare_predictors(sce);
    sce->ics.predictor_reset_group = 0;

    for (sfb = PRED_SFB_START; sfb < ff_aac_pred_sfb_max[s->samplerate_index]; sfb++) {
        float dist1 = 0.0f, dist2 = 0.0f;
        int swb_start = sce->ics.swb_offset[sfb];
        int swb_len = sce->ics.swb_offset[sfb + 1] - swb_start;
        int cb1 = sce->band_type[sfb], cb2, bits1 = 0, bits2 = 0;
        FFPsyBand *band = &s->psy.ch[s->cur_channel].psy_bands[sfb];
        abs_pow34_v(O34, &sce->coeffs[swb_start], swb_len);
        abs_pow34_v(P34, &sce->prcoeffs[swb_start], swb_len);
        cb2 = find_min_book(find_max_val(1, swb_len, P34), sce->sf_idx[sfb]);
        if (cb2 <= cb1) {
            dist1 += quantize_band_cost(s, &sce->coeffs[swb_start],   O34, swb_len,
                                        sce->sf_idx[sfb], cb1, s->lambda / band->threshold,
                                        INFINITY, &bits1, 0);
            dist2 += quantize_band_cost(s, &sce->prcoeffs[swb_start], P34, swb_len,
                                        sce->sf_idx[sfb], cb2, s->lambda / band->threshold,
                                        INFINITY, &bits2, 0);
            if (dist2 <= dist1) {
                sce->ics.prediction_used[sfb] = 1;
                sce->band_type[sfb] = cb2;
                count++;
            }
            cost_coeffs += bits1;
            cost_pred   += bits2;
        }
    }

    if (count && cost_pred > cost_coeffs) {
        memset(sce->ics.prediction_used, 0, sizeof(sce->ics.prediction_used));
        memcpy(sce->band_type, sce->orig_band_type, sizeof(sce->band_type));
        count = 0;
    }

    sce->ics.predictor_present = !!count;
}

/**
 * Encoder predictors data.
 */
void ff_aac_encode_main_pred(AACEncContext *s, SingleChannelElement *sce)
{
    int sfb;

    if (!sce->ics.predictor_present ||
        sce->ics.window_sequence[0] == EIGHT_SHORT_SEQUENCE)
        return;

    put_bits(&s->pb, 1, !!sce->ics.predictor_reset_group);
    if (sce->ics.predictor_reset_group)
        put_bits(&s->pb, 5, sce->ics.predictor_reset_group);
    for (sfb = 0; sfb < FFMIN(sce->ics.max_sfb, ff_aac_pred_sfb_max[s->samplerate_index]); sfb++)
        put_bits(&s->pb, 1, sce->ics.prediction_used[sfb]);
}
