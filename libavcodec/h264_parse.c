/*
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

#include "get_bits.h"
#include "golomb.h"
#include "h264.h"
#include "h264_parse.h"

int ff_h264_pred_weight_table(GetBitContext *gb, const SPS *sps,
                              const int *ref_count, int slice_type_nos,
                              H264PredWeightTable *pwt)
{
    int list, i;
    int luma_def, chroma_def;

    pwt->use_weight             = 0;
    pwt->use_weight_chroma      = 0;
    pwt->luma_log2_weight_denom = get_ue_golomb(gb);
    if (sps->chroma_format_idc)
        pwt->chroma_log2_weight_denom = get_ue_golomb(gb);

    if (pwt->luma_log2_weight_denom > 7U) {
        av_log(NULL, AV_LOG_ERROR, "luma_log2_weight_denom %d is out of range\n", pwt->luma_log2_weight_denom);
        pwt->luma_log2_weight_denom = 0;
    }
    if (pwt->chroma_log2_weight_denom > 7U) {
        av_log(NULL, AV_LOG_ERROR, "chroma_log2_weight_denom %d is out of range\n", pwt->chroma_log2_weight_denom);
        pwt->chroma_log2_weight_denom = 0;
    }

    luma_def   = 1 << pwt->luma_log2_weight_denom;
    chroma_def = 1 << pwt->chroma_log2_weight_denom;

    for (list = 0; list < 2; list++) {
        pwt->luma_weight_flag[list]   = 0;
        pwt->chroma_weight_flag[list] = 0;
        for (i = 0; i < ref_count[list]; i++) {
            int luma_weight_flag, chroma_weight_flag;

            luma_weight_flag = get_bits1(gb);
            if (luma_weight_flag) {
                pwt->luma_weight[i][list][0] = get_se_golomb(gb);
                pwt->luma_weight[i][list][1] = get_se_golomb(gb);
                if (pwt->luma_weight[i][list][0] != luma_def ||
                    pwt->luma_weight[i][list][1] != 0) {
                    pwt->use_weight             = 1;
                    pwt->luma_weight_flag[list] = 1;
                }
            } else {
                pwt->luma_weight[i][list][0] = luma_def;
                pwt->luma_weight[i][list][1] = 0;
            }

            if (sps->chroma_format_idc) {
                chroma_weight_flag = get_bits1(gb);
                if (chroma_weight_flag) {
                    int j;
                    for (j = 0; j < 2; j++) {
                        pwt->chroma_weight[i][list][j][0] = get_se_golomb(gb);
                        pwt->chroma_weight[i][list][j][1] = get_se_golomb(gb);
                        if (pwt->chroma_weight[i][list][j][0] != chroma_def ||
                            pwt->chroma_weight[i][list][j][1] != 0) {
                            pwt->use_weight_chroma        = 1;
                            pwt->chroma_weight_flag[list] = 1;
                        }
                    }
                } else {
                    int j;
                    for (j = 0; j < 2; j++) {
                        pwt->chroma_weight[i][list][j][0] = chroma_def;
                        pwt->chroma_weight[i][list][j][1] = 0;
                    }
                }
            }
        }
        if (slice_type_nos != AV_PICTURE_TYPE_B)
            break;
    }
    pwt->use_weight = pwt->use_weight || pwt->use_weight_chroma;
    return 0;
}

/**
 * Check if the top & left blocks are available if needed and
 * change the dc mode so it only uses the available blocks.
 */
int ff_h264_check_intra4x4_pred_mode(int8_t *pred_mode_cache, void *logctx,
                                     int top_samples_available, int left_samples_available)
{
    static const int8_t top[12] = {
        -1, 0, LEFT_DC_PRED, -1, -1, -1, -1, -1, 0
    };
    static const int8_t left[12] = {
        0, -1, TOP_DC_PRED, 0, -1, -1, -1, 0, -1, DC_128_PRED
    };
    int i;

    if (!(top_samples_available & 0x8000)) {
        for (i = 0; i < 4; i++) {
            int status = top[pred_mode_cache[scan8[0] + i]];
            if (status < 0) {
                av_log(logctx, AV_LOG_ERROR,
					   "top block unavailable for requested intra mode %d\n",
                       status);
                return AVERROR_INVALIDDATA;
            } else if (status) {
                pred_mode_cache[scan8[0] + i] = status;
            }
        }
    }

    if ((left_samples_available & 0x8888) != 0x8888) {
        static const int mask[4] = { 0x8000, 0x2000, 0x80, 0x20 };
        for (i = 0; i < 4; i++)
            if (!(left_samples_available & mask[i])) {
                int status = left[pred_mode_cache[scan8[0] + 8 * i]];
                if (status < 0) {
                    av_log(logctx, AV_LOG_ERROR,
                           "left block unavailable for requested intra4x4 mode %d\n",
                           status);
                    return AVERROR_INVALIDDATA;
                } else if (status) {
                    pred_mode_cache[scan8[0] + 8 * i] = status;
                }
            }
    }

    return 0;
}

/**
 * Check if the top & left blocks are available if needed and
 * change the dc mode so it only uses the available blocks.
 */
int ff_h264_check_intra_pred_mode(void *logctx, int top_samples_available,
                                  int left_samples_available,
                                  int mode, int is_chroma)
{
    static const int8_t top[4]  = { LEFT_DC_PRED8x8, 1, -1, -1 };
    static const int8_t left[5] = { TOP_DC_PRED8x8, -1,  2, -1, DC_128_PRED8x8 };

    if (mode > 3U) {
        av_log(logctx, AV_LOG_ERROR,
               "out of range intra chroma pred mode\n");
        return AVERROR_INVALIDDATA;
    }

    if (!(top_samples_available & 0x8000)) {
        mode = top[mode];
        if (mode < 0) {
            av_log(logctx, AV_LOG_ERROR,
                   "top block unavailable for requested intra mode\n");
            return AVERROR_INVALIDDATA;
        }
    }

    if ((left_samples_available & 0x8080) != 0x8080) {
        mode = left[mode];
        if (mode < 0) {
            av_log(logctx, AV_LOG_ERROR,
                   "left block unavailable for requested intra mode\n");
            return AVERROR_INVALIDDATA;
        }
        if (is_chroma && (left_samples_available & 0x8080)) {
            // mad cow disease mode, aka MBAFF + constrained_intra_pred
            mode = ALZHEIMER_DC_L0T_PRED8x8 +
                   (!(left_samples_available & 0x8000)) +
                   2 * (mode == DC_128_PRED8x8);
        }
    }

    return mode;
}
