/*
 * raw FLAC muxer
 * Copyright (C) 2009 Justin Ruggles
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

#include "libavcodec/flac.h"

#include "avformat.h"
#include "flacenc.h"

int ff_flac_write_header(AVIOContext *pb, uint8_t *extradata,
                         int extradata_size, int last_block)
{
    uint8_t header[8] = {
        0x66, 0x4C, 0x61, 0x43, 0x00, 0x00, 0x00, 0x22
    };

    header[4] = last_block ? 0x80 : 0x00;

    if (extradata_size < FLAC_STREAMINFO_SIZE)
        return AVERROR_INVALIDDATA;

    /* write "fLaC" stream marker and first metadata block header */
    avio_write(pb, header, 8);

    /* write STREAMINFO */
    avio_write(pb, extradata, FLAC_STREAMINFO_SIZE);

    return 0;
}

int ff_flac_is_native_layout(uint64_t channel_layout)
{
    if (channel_layout == AV_CH_LAYOUT_MONO     ||
        channel_layout == AV_CH_LAYOUT_STEREO   ||
        channel_layout == AV_CH_LAYOUT_SURROUND ||
        channel_layout == AV_CH_LAYOUT_QUAD     ||
        channel_layout == AV_CH_LAYOUT_5POINT0  ||
        channel_layout == AV_CH_LAYOUT_5POINT1  ||
        channel_layout == AV_CH_LAYOUT_6POINT1  ||
        channel_layout == AV_CH_LAYOUT_7POINT1)
        return 1;
    return 0;
}
