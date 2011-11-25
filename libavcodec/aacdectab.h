/*
 * AAC decoder data
 * Copyright (c) 2005-2006 Oded Shimon ( ods15 ods15 dyndns org )
 * Copyright (c) 2006-2007 Maxim Gavrilov ( maxim.gavrilov gmail com )
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
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

#include "libavutil/audioconvert.h"
#include "aac.h"

#include <stdint.h>

/* @name ltp_coef
 * Table of the LTP coefficients
 */
static const float ltp_coef[8] = {
    0.570829, 0.696616, 0.813004, 0.911304,
    0.984900, 1.067894, 1.194601, 1.369533,
};

/* @name tns_tmp2_map
 * Tables of the tmp2[] arrays of LPC coefficients used for TNS.
 * The suffix _M_N[] indicate the values of coef_compress and coef_res
 * respectively.
 * @{
 */
static const float tns_tmp2_map_1_3[4] = {
     0.00000000, -0.43388373,  0.64278758,  0.34202015,
};

static const float tns_tmp2_map_0_3[8] = {
     0.00000000, -0.43388373, -0.78183150, -0.97492790,
     0.98480773,  0.86602539,  0.64278758,  0.34202015,
};

static const float tns_tmp2_map_1_4[8] = {
     0.00000000, -0.20791170, -0.40673664, -0.58778524,
     0.67369562,  0.52643216,  0.36124167,  0.18374951,
};

static const float tns_tmp2_map_0_4[16] = {
     0.00000000, -0.20791170, -0.40673664, -0.58778524,
    -0.74314481, -0.86602539, -0.95105654, -0.99452192,
     0.99573416,  0.96182561,  0.89516330,  0.79801720,
     0.67369562,  0.52643216,  0.36124167,  0.18374951,
};

static const float * const tns_tmp2_map[4] = {
    tns_tmp2_map_0_3,
    tns_tmp2_map_0_4,
    tns_tmp2_map_1_3,
    tns_tmp2_map_1_4
};
// @}

static const int8_t tags_per_config[16] = { 0, 1, 1, 2, 3, 3, 4, 5, 0, 0, 0, 0, 0, 0, 0, 0 };

static const uint8_t aac_channel_layout_map[7][5][2] = {
    { { TYPE_SCE, 0 }, },
    { { TYPE_CPE, 0 }, },
    { { TYPE_CPE, 0 }, { TYPE_SCE, 0 }, },
    { { TYPE_CPE, 0 }, { TYPE_SCE, 0 }, { TYPE_SCE, 1 }, },
    { { TYPE_CPE, 0 }, { TYPE_SCE, 0 }, { TYPE_CPE, 1 }, },
    { { TYPE_CPE, 0 }, { TYPE_SCE, 0 }, { TYPE_LFE, 0 }, { TYPE_CPE, 1 }, },
    { { TYPE_CPE, 0 }, { TYPE_SCE, 0 }, { TYPE_LFE, 0 }, { TYPE_CPE, 2 }, { TYPE_CPE, 1 }, },
};

static const uint64_t aac_channel_layout[8] = {
    AV_CH_LAYOUT_MONO,
    AV_CH_LAYOUT_STEREO,
    AV_CH_LAYOUT_SURROUND,
    AV_CH_LAYOUT_4POINT0,
    AV_CH_LAYOUT_5POINT0_BACK,
    AV_CH_LAYOUT_5POINT1_BACK,
    AV_CH_LAYOUT_7POINT1_WIDE,
    0,
};

#endif /* AVCODEC_AACDECTAB_H */
