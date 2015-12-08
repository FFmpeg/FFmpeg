/*
 * AAC encoder long term prediction extension
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
 * AAC encoder long term prediction extension
 * @author Rostislav Pehlivanov ( atomnuker gmail com )
 */

#include "aacenc_ltp.h"
#include "aacenc_quantization.h"
#include "aacenc_utils.h"

/**
 * Encode LTP data.
 */
void ff_aac_encode_ltp_info(AACEncContext *s, SingleChannelElement *sce,
                            int common_window)
{
    int i;
    IndividualChannelStream *ics = &sce->ics;
    if (s->profile != FF_PROFILE_AAC_LTP || !ics->predictor_present)
        return;
    if (common_window)
        put_bits(&s->pb, 1, 0);
    put_bits(&s->pb, 1, ics->ltp.present);
    if (!ics->ltp.present)
        return;
    put_bits(&s->pb, 11, ics->ltp.lag);
    put_bits(&s->pb, 3,  ics->ltp.coef_idx);
    for (i = 0; i < FFMIN(ics->max_sfb, MAX_LTP_LONG_SFB); i++)
        put_bits(&s->pb, 1, ics->ltp.used[i]);
}

void ff_aac_ltp_insert_new_frame(AACEncContext *s)
{
    int i, ch, tag, chans, cur_channel, start_ch = 0;
    ChannelElement *cpe;
    SingleChannelElement *sce;
    for (i = 0; i < s->chan_map[0]; i++) {
        cpe = &s->cpe[i];
        tag      = s->chan_map[i+1];
        chans    = tag == TYPE_CPE ? 2 : 1;
        for (ch = 0; ch < chans; ch++) {
            sce = &cpe->ch[ch];
            cur_channel = start_ch + ch;
            /* New sample + overlap */
            memcpy(&sce->ltp_state[0],    &sce->ltp_state[1024], 1024*sizeof(sce->ltp_state[0]));
            memcpy(&sce->ltp_state[1024], &s->planar_samples[cur_channel][2048], 1024*sizeof(sce->ltp_state[0]));
            memcpy(&sce->ltp_state[2048], &sce->ret_buf[0], 1024*sizeof(sce->ltp_state[0]));
            sce->ics.ltp.lag = 0;
        }
        start_ch += chans;
    }
}

static void get_lag(float *buf, const float *new, LongTermPrediction *ltp)
{
    int i, j, lag, max_corr = 0;
    float max_ratio;
    for (i = 0; i < 2048; i++) {
        float corr, s0 = 0.0f, s1 = 0.0f;
        const int start = FFMAX(0, i - 1024);
        for (j = start; j < 2048; j++) {
            const int idx = j - i + 1024;
            s0 += new[j]*buf[idx];
            s1 += buf[idx]*buf[idx];
        }
        corr = s1 > 0.0f ? s0/sqrt(s1) : 0.0f;
        if (corr > max_corr) {
            max_corr = corr;
            lag = i;
            max_ratio = corr/(2048-start);
        }
    }
    ltp->lag = FFMAX(av_clip_uintp2(lag, 11), 0);
    ltp->coef_idx = quant_array_idx(max_ratio, ltp_coef, 8);
    ltp->coef = ltp_coef[ltp->coef_idx];
}

static void generate_samples(float *buf, LongTermPrediction *ltp)
{
    int i, samples_num = 2048;
    if (!ltp->lag) {
        ltp->present = 0;
        return;
    } else if (ltp->lag < 1024) {
        samples_num = ltp->lag + 1024;
    }
    for (i = 0; i < samples_num; i++)
        buf[i] = ltp->coef*buf[i + 2048 - ltp->lag];
    memset(&buf[i], 0, (2048 - i)*sizeof(float));
}

/**
 * Process LTP parameters
 * @see Patent WO2006070265A1
 */
void ff_aac_update_ltp(AACEncContext *s, SingleChannelElement *sce)
{
    float *pred_signal = &sce->ltp_state[0];
    const float *samples = &s->planar_samples[s->cur_channel][1024];

    if (s->profile != FF_PROFILE_AAC_LTP)
        return;

    /* Calculate lag */
    get_lag(pred_signal, samples, &sce->ics.ltp);
    generate_samples(pred_signal, &sce->ics.ltp);
}

void ff_aac_adjust_common_ltp(AACEncContext *s, ChannelElement *cpe)
{
    int sfb, count = 0;
    SingleChannelElement *sce0 = &cpe->ch[0];
    SingleChannelElement *sce1 = &cpe->ch[1];

    if (!cpe->common_window ||
        sce0->ics.window_sequence[0] == EIGHT_SHORT_SEQUENCE ||
        sce1->ics.window_sequence[0] == EIGHT_SHORT_SEQUENCE) {
        sce0->ics.ltp.present = 0;
        return;
    }

    for (sfb = 0; sfb < FFMIN(sce0->ics.max_sfb, MAX_LTP_LONG_SFB); sfb++) {
        int sum = sce0->ics.ltp.used[sfb] + sce1->ics.ltp.used[sfb];
        if (sum != 2) {
            sce0->ics.ltp.used[sfb] = 0;
        } else if (sum == 2) {
            count++;
        }
    }

    sce0->ics.ltp.present = !!count;
    sce0->ics.predictor_present = !!count;
}

/**
 * Mark LTP sfb's
 */
void ff_aac_search_for_ltp(AACEncContext *s, SingleChannelElement *sce,
                           int common_window)
{
    int w, g, w2, i, start = 0, count = 0;
    int saved_bits = -(15 + FFMIN(sce->ics.max_sfb, MAX_LTP_LONG_SFB));
    float *C34 = &s->scoefs[128*0], *PCD = &s->scoefs[128*1];
    float *PCD34 = &s->scoefs[128*2];
    const int max_ltp = FFMIN(sce->ics.max_sfb, MAX_LTP_LONG_SFB);

    if (sce->ics.window_sequence[0] == EIGHT_SHORT_SEQUENCE) {
        if (sce->ics.ltp.lag) {
            memset(&sce->ltp_state[0], 0, 3072*sizeof(sce->ltp_state[0]));
            memset(&sce->ics.ltp, 0, sizeof(LongTermPrediction));
        }
        return;
    }

    if (!sce->ics.ltp.lag || s->lambda > 120.0f)
        return;

    for (w = 0; w < sce->ics.num_windows; w += sce->ics.group_len[w]) {
        start = 0;
        for (g = 0;  g < sce->ics.num_swb; g++) {
            int bits1 = 0, bits2 = 0;
            float dist1 = 0.0f, dist2 = 0.0f;
            if (w*16+g > max_ltp) {
                start += sce->ics.swb_sizes[g];
                continue;
            }
            for (w2 = 0; w2 < sce->ics.group_len[w]; w2++) {
                int bits_tmp1, bits_tmp2;
                FFPsyBand *band = &s->psy.ch[s->cur_channel].psy_bands[(w+w2)*16+g];
                for (i = 0; i < sce->ics.swb_sizes[g]; i++)
                    PCD[i] = sce->coeffs[start+(w+w2)*128+i] - sce->lcoeffs[start+(w+w2)*128+i];
                abs_pow34_v(C34,  &sce->coeffs[start+(w+w2)*128],  sce->ics.swb_sizes[g]);
                abs_pow34_v(PCD34, PCD, sce->ics.swb_sizes[g]);
                dist1 += quantize_band_cost(s, &sce->coeffs[start+(w+w2)*128], C34, sce->ics.swb_sizes[g],
                                            sce->sf_idx[(w+w2)*16+g], sce->band_type[(w+w2)*16+g],
                                            s->lambda/band->threshold, INFINITY, &bits_tmp1, NULL, 0);
                dist2 += quantize_band_cost(s, PCD, PCD34, sce->ics.swb_sizes[g],
                                            sce->sf_idx[(w+w2)*16+g],
                                            sce->band_type[(w+w2)*16+g],
                                            s->lambda/band->threshold, INFINITY, &bits_tmp2, NULL, 0);
                bits1 += bits_tmp1;
                bits2 += bits_tmp2;
            }
            if (dist2 < dist1 && bits2 < bits1) {
                for (w2 = 0; w2 < sce->ics.group_len[w]; w2++)
                    for (i = 0; i < sce->ics.swb_sizes[g]; i++)
                        sce->coeffs[start+(w+w2)*128+i] -= sce->lcoeffs[start+(w+w2)*128+i];
                sce->ics.ltp.used[w*16+g] = 1;
                saved_bits += bits1 - bits2;
                count++;
            }
            start += sce->ics.swb_sizes[g];
        }
    }

    sce->ics.ltp.present = !!count && (saved_bits >= 0);
    sce->ics.predictor_present = !!sce->ics.ltp.present;

    /* Reset any marked sfbs */
    if (!sce->ics.ltp.present && !!count) {
        for (w = 0; w < sce->ics.num_windows; w += sce->ics.group_len[w]) {
            start = 0;
            for (g = 0;  g < sce->ics.num_swb; g++) {
                if (sce->ics.ltp.used[w*16+g]) {
                    for (w2 = 0; w2 < sce->ics.group_len[w]; w2++) {
                        for (i = 0; i < sce->ics.swb_sizes[g]; i++) {
                            sce->coeffs[start+(w+w2)*128+i] += sce->lcoeffs[start+(w+w2)*128+i];
                        }
                    }
                }
                start += sce->ics.swb_sizes[g];
            }
        }
    }
}
