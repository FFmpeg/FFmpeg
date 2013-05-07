/*
 * ALAC encoder and decoder common data
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

#include "libavutil/channel_layout.h"
#include "alac_data.h"

const uint8_t ff_alac_channel_layout_offsets[ALAC_MAX_CHANNELS][ALAC_MAX_CHANNELS] = {
    { 0 },
    { 0, 1 },
    { 2, 0, 1 },
    { 2, 0, 1, 3 },
    { 2, 0, 1, 3, 4 },
    { 2, 0, 1, 4, 5, 3 },
    { 2, 0, 1, 4, 5, 6, 3 },
    { 2, 6, 7, 0, 1, 4, 5, 3 }
};

const AVChannelLayout ff_alac_ch_layouts[ALAC_MAX_CHANNELS + 1] = {
    AV_CHANNEL_LAYOUT_MONO,
    AV_CHANNEL_LAYOUT_STEREO,
    AV_CHANNEL_LAYOUT_SURROUND,
    AV_CHANNEL_LAYOUT_4POINT0,
    AV_CHANNEL_LAYOUT_5POINT0_BACK,
    AV_CHANNEL_LAYOUT_5POINT1_BACK,
    AV_CHANNEL_LAYOUT_6POINT1_BACK,
    AV_CHANNEL_LAYOUT_7POINT1_WIDE_BACK,
    { 0 }
};

const enum AlacRawDataBlockType ff_alac_channel_elements[ALAC_MAX_CHANNELS][5] = {
    { TYPE_SCE,                                         },
    { TYPE_CPE,                                         },
    { TYPE_SCE, TYPE_CPE,                               },
    { TYPE_SCE, TYPE_CPE, TYPE_SCE                      },
    { TYPE_SCE, TYPE_CPE, TYPE_CPE,                     },
    { TYPE_SCE, TYPE_CPE, TYPE_CPE, TYPE_SCE,           },
    { TYPE_SCE, TYPE_CPE, TYPE_CPE, TYPE_SCE, TYPE_SCE, },
    { TYPE_SCE, TYPE_CPE, TYPE_CPE, TYPE_CPE, TYPE_SCE, },
};
