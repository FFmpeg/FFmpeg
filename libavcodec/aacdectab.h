/*
 * AAC decoder data
 * Copyright (c) 2005-2006 Oded Shimon ( ods15 ods15 dyndns org )
 * Copyright (c) 2006-2007 Maxim Gavrilov ( maxim.gavrilov gmail com )
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
 * AAC decoder data
 * @author Oded Shimon  ( ods15 ods15 dyndns org )
 * @author Maxim Gavrilov ( maxim.gavrilov gmail com )
 */

#ifndef AVCODEC_AACDECTAB_H
#define AVCODEC_AACDECTAB_H

#include "libavutil/channel_layout.h"
#include "aac.h"

#include <stdint.h>

/* @name ltp_coef
 * Table of the LTP coefficients
 */
static const INTFLOAT ltp_coef[8] = {
    Q30(0.570829f), Q30(0.696616f), Q30(0.813004f), Q30(0.911304f),
    Q30(0.984900f), Q30(1.067894f), Q30(1.194601f), Q30(1.369533f),
};

/* @name tns_tmp2_map
 * Tables of the tmp2[] arrays of LPC coefficients used for TNS.
 * The suffix _M_N[] indicate the values of coef_compress and coef_res
 * respectively.
 * @{
 */
static const INTFLOAT tns_tmp2_map_1_3[4] = {
     Q31(0.00000000f), Q31(-0.43388373f),  Q31(0.64278758f),  Q31(0.34202015f),
};

static const INTFLOAT tns_tmp2_map_0_3[8] = {
     Q31(0.00000000f), Q31(-0.43388373f), Q31(-0.78183150f), Q31(-0.97492790f),
     Q31(0.98480773f), Q31( 0.86602539f), Q31( 0.64278758f), Q31( 0.34202015f),
};

static const INTFLOAT tns_tmp2_map_1_4[8] = {
     Q31(0.00000000f), Q31(-0.20791170f), Q31(-0.40673664f), Q31(-0.58778524f),
     Q31(0.67369562f), Q31( 0.52643216f), Q31( 0.36124167f), Q31( 0.18374951f),
};

static const INTFLOAT tns_tmp2_map_0_4[16] = {
    Q31( 0.00000000f), Q31(-0.20791170f), Q31(-0.40673664f), Q31(-0.58778524f),
    Q31(-0.74314481f), Q31(-0.86602539f), Q31(-0.95105654f), Q31(-0.99452192f),
    Q31( 0.99573416f), Q31( 0.96182561f), Q31( 0.89516330f), Q31( 0.79801720f),
    Q31( 0.67369562f), Q31( 0.52643216f), Q31( 0.36124167f), Q31( 0.18374951f),
};

static const INTFLOAT * const tns_tmp2_map[4] = {
    tns_tmp2_map_0_3,
    tns_tmp2_map_0_4,
    tns_tmp2_map_1_3,
    tns_tmp2_map_1_4
};
// @}

static const int8_t tags_per_config[16] = { 0, 1, 1, 2, 3, 3, 4, 5, 0, 0, 0, 4, 5, 0, 5, 0 };

static const uint8_t aac_channel_layout_map[16][5][3] = {
    { { TYPE_SCE, 0, AAC_CHANNEL_FRONT }, },
    { { TYPE_CPE, 0, AAC_CHANNEL_FRONT }, },
    { { TYPE_SCE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 0, AAC_CHANNEL_FRONT }, },
    { { TYPE_SCE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 0, AAC_CHANNEL_FRONT }, { TYPE_SCE, 1, AAC_CHANNEL_BACK }, },
    { { TYPE_SCE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 1, AAC_CHANNEL_BACK }, },
    { { TYPE_SCE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 1, AAC_CHANNEL_BACK }, { TYPE_LFE, 0, AAC_CHANNEL_LFE  }, },
    { { TYPE_SCE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 1, AAC_CHANNEL_FRONT }, { TYPE_CPE, 2, AAC_CHANNEL_BACK }, { TYPE_LFE, 0, AAC_CHANNEL_LFE  }, },
    { { 0, } },
    { { 0, } },
    { { 0, } },
    { { TYPE_SCE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 1, AAC_CHANNEL_BACK }, { TYPE_SCE, 1, AAC_CHANNEL_BACK }, { TYPE_LFE, 0, AAC_CHANNEL_LFE  }, },
    { { TYPE_SCE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 1, AAC_CHANNEL_SIDE }, { TYPE_CPE, 2, AAC_CHANNEL_BACK }, { TYPE_LFE, 0, AAC_CHANNEL_LFE  }, },
    { { 0, } },
    /* TODO: Add 7+1 TOP configuration */
};

static const uint64_t aac_channel_layout[16] = {
    AV_CH_LAYOUT_MONO,
    AV_CH_LAYOUT_STEREO,
    AV_CH_LAYOUT_SURROUND,
    AV_CH_LAYOUT_4POINT0,
    AV_CH_LAYOUT_5POINT0_BACK,
    AV_CH_LAYOUT_5POINT1_BACK,
    AV_CH_LAYOUT_7POINT1_WIDE_BACK,
    0,
    0,
    0,
    AV_CH_LAYOUT_6POINT1,
    AV_CH_LAYOUT_7POINT1,
    0,
    /* AV_CH_LAYOUT_7POINT1_TOP, */
};

#endif /* AVCODEC_AACDECTAB_H */
