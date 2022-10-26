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

static const int8_t tags_per_config[16] = { 0, 1, 1, 2, 3, 3, 4, 5, 0, 0, 0, 5, 5, 16, 5, 0 };

static const uint8_t aac_channel_layout_map[16][16][3] = {
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
    {
      { TYPE_SCE, 0, AAC_CHANNEL_FRONT }, // SCE1 = FC,
      { TYPE_CPE, 0, AAC_CHANNEL_FRONT }, // CPE1 = FLc and FRc,
      { TYPE_CPE, 1, AAC_CHANNEL_FRONT }, // CPE2 = FL and FR,
      { TYPE_CPE, 2, AAC_CHANNEL_SIDE  }, // CPE3 = SiL and SiR,
      { TYPE_CPE, 3, AAC_CHANNEL_BACK  }, // CPE4 = BL and BR,
      { TYPE_SCE, 1, AAC_CHANNEL_BACK  }, // SCE2 = BC,
      { TYPE_LFE, 0, AAC_CHANNEL_LFE   }, // LFE1 = LFE1,
      { TYPE_LFE, 1, AAC_CHANNEL_LFE   }, // LFE2 = LFE2,
      { TYPE_SCE, 2, AAC_CHANNEL_FRONT }, // SCE3 = TpFC,
      { TYPE_CPE, 4, AAC_CHANNEL_FRONT }, // CPE5 = TpFL and TpFR,
      { TYPE_CPE, 5, AAC_CHANNEL_SIDE  }, // CPE6 = TpSiL and TpSiR,
      { TYPE_SCE, 3, AAC_CHANNEL_FRONT }, // SCE4 = TpC,
      { TYPE_CPE, 6, AAC_CHANNEL_BACK  }, // CPE7 = TpBL and TpBR,
      { TYPE_SCE, 4, AAC_CHANNEL_BACK  }, // SCE5 = TpBC,
      { TYPE_SCE, 5, AAC_CHANNEL_FRONT }, // SCE6 = BtFC,
      { TYPE_CPE, 7, AAC_CHANNEL_FRONT }, // CPE8 = BtFL and BtFR
    },
    { { 0, } },
    /* TODO: Add 7+1 TOP configuration */
};

#if FF_API_OLD_CHANNEL_LAYOUT
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
    AV_CH_LAYOUT_6POINT1_BACK,
    AV_CH_LAYOUT_7POINT1,
    AV_CH_LAYOUT_22POINT2,
    0,
    /* AV_CH_LAYOUT_7POINT1_TOP, */
};
#endif

static const AVChannelLayout aac_ch_layout[16] = {
    AV_CHANNEL_LAYOUT_MONO,
    AV_CHANNEL_LAYOUT_STEREO,
    AV_CHANNEL_LAYOUT_SURROUND,
    AV_CHANNEL_LAYOUT_4POINT0,
    AV_CHANNEL_LAYOUT_5POINT0_BACK,
    AV_CHANNEL_LAYOUT_5POINT1_BACK,
    AV_CHANNEL_LAYOUT_7POINT1_WIDE_BACK,
    { 0 },
    { 0 },
    { 0 },
    AV_CHANNEL_LAYOUT_6POINT1_BACK,
    AV_CHANNEL_LAYOUT_7POINT1,
    AV_CHANNEL_LAYOUT_22POINT2,
    { 0 },
    /* AV_CHANNEL_LAYOUT_7POINT1_TOP, */
};

#endif /* AVCODEC_AACDECTAB_H */
