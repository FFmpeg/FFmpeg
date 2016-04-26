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

/**
 * @file
 * H.264 decoder/parser shared code
 */

#ifndef AVCODEC_H264_PARSE_H
#define AVCODEC_H264_PARSE_H

#include "get_bits.h"

typedef struct H264PredWeightTable {
    int use_weight;
    int use_weight_chroma;
    int luma_log2_weight_denom;
    int chroma_log2_weight_denom;
    int luma_weight_flag[2];    ///< 7.4.3.2 luma_weight_lX_flag
    int chroma_weight_flag[2];  ///< 7.4.3.2 chroma_weight_lX_flag
    // The following 2 can be changed to int8_t but that causes 10cpu cycles speedloss
    int luma_weight[48][2][2];
    int chroma_weight[48][2][2][2];
    int implicit_weight[48][48][2];
} H264PredWeightTable;

struct SPS;

int ff_h264_pred_weight_table(GetBitContext *gb, const struct SPS *sps,
                              const int *ref_count, int slice_type_nos,
                              H264PredWeightTable *pwt);

#endif /* AVCODEC_H264_PARSE_H */
