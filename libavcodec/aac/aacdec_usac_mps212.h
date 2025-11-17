/*
 * Copyright (c) 2025 Lynne <dev@lynne.ee>
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

#ifndef AVCODEC_AAC_AACDEC_USAC_MPS212_H
#define AVCODEC_AAC_AACDEC_USAC_MPS212_H

#include <stdbool.h>

#include "libavcodec/get_bits.h"
#include "libavcodec/aac.h"

enum AACMPSDataType {
    MPS_CLD = 0,
    MPS_ICC,
    MPS_IPD,

    MPS_ELE_NB,
};

typedef struct AACMPSLosslessData {
    int16_t data[MPS_MAX_PARAM_SETS][MPS_MAX_PARAM_BANDS];
    int16_t last_data[MPS_MAX_PARAM_BANDS];

    int16_t data_mode[MPS_MAX_PARAM_SETS];
    bool coarse_quant[MPS_MAX_PARAM_SETS];
    int16_t freq_res[MPS_MAX_PARAM_SETS];
    int16_t coarse_quant_no[MPS_MAX_PARAM_SETS];

    bool quant_coarse_prev;
} AACMPSLosslessData;

int ff_aac_ec_data_dec(GetBitContext *gb, AACMPSLosslessData *ld,
                       enum AACMPSDataType data_type,
                       int default_val,
                       int start_band, int end_band, int frame_indep_flag,
                       int indep_flag, int nb_param_sets);

int ff_aac_map_index_data(AACMPSLosslessData *ld,
                          enum AACMPSDataType data_type,
                          int dst_idx[MPS_MAX_PARAM_SETS][MPS_MAX_PARAM_BANDS],
                          int default_value, int start_band, int stop_band,
                          int nb_param_sets, const int *param_set_idx,
                          int extend_frame);

int ff_aac_huff_dec_reshape(GetBitContext *gb, int16_t *out_data,
                            int nb_val);

#endif /* AVCODEC_AAC_AACDEC_USAC_MPS212_H */
