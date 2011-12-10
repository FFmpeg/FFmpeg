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

#include "libavcodec/flac.h"
#include "libavcodec/bytestream.h"
#include "avformat.h"
#include "flacenc.h"

int ff_flac_write_header(AVIOContext *pb, AVCodecContext *codec,
                         int last_block)
{
    uint8_t header[8] = {
        0x66, 0x4C, 0x61, 0x43, 0x00, 0x00, 0x00, 0x22
    };
    uint8_t *streaminfo;
    enum FLACExtradataFormat format;

    header[4] = last_block ? 0x80 : 0x00;
    if (!avpriv_flac_is_extradata_valid(codec, &format, &streaminfo))
        return -1;

    /* write "fLaC" stream marker and first metadata block header */
    avio_write(pb, header, 8);

    /* write STREAMINFO */
    avio_write(pb, streaminfo, FLAC_STREAMINFO_SIZE);

    return 0;
}
