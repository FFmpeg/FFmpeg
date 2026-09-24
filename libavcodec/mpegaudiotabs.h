/*
 * MPEG Audio common tables
 * copyright (c) 2002 Fabrice Bellard
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

#ifndef AVCODEC_MPEGAUDIOTABS_H
#define AVCODEC_MPEGAUDIOTABS_H

#include <stdint.h>

#include "libavutil/channel_layout.h"

const uint16_t ff_mpa_bitrate_tab[2][3][15] = {
    { { 0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448 },
      { 0, 32, 48, 56,  64,  80,  96, 112, 128, 160, 192, 224, 256, 320, 384 },
      { 0, 32, 40, 48,  56,  64,  80,  96, 112, 128, 160, 192, 224, 256, 320 } },
    { { 0, 32, 48, 56,  64,  80,  96, 112, 128, 144, 160, 176, 192, 224, 256 },
      { 0,  8, 16, 24,  32,  40,  48,  56,  64,  80,  96, 112, 128, 144, 160 },
      { 0,  8, 16, 24,  32,  40,  48,  56,  64,  80,  96, 112, 128, 144, 160 }
    }
};

const uint16_t ff_mpa_freq_tab[3] = { 44100, 48000, 32000 };

const uint64_t ff_mpa_cicp_channel_layout_masks[20] = {
    0,
    AV_CH_LAYOUT_MONO,               AV_CH_LAYOUT_STEREO,
    AV_CH_LAYOUT_SURROUND,           AV_CH_LAYOUT_4POINT0,
    AV_CH_LAYOUT_5POINT0,            AV_CH_LAYOUT_5POINT1,
    AV_CH_LAYOUT_7POINT1_WIDE,       0,
    AV_CH_LAYOUT_2_1,                AV_CH_LAYOUT_2_2,
    AV_CH_LAYOUT_6POINT1,            AV_CH_LAYOUT_7POINT1,
    AV_CH_LAYOUT_22POINT2,           AV_CH_LAYOUT_5POINT1POINT2,
    AV_CH_LAYOUT_7POINT2POINT3,      AV_CH_LAYOUT_5POINT1POINT4,
    AV_CH_LAYOUT_5POINT1POINT6,      AV_CH_LAYOUT_7POINT1POINT6,
    AV_CH_LAYOUT_7POINT1POINT4,
};

#endif
