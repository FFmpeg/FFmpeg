/*
 * AV1 helper functions for muxers
 * Copyright (c) 2018 James Almer <jamrial@gmail.com>
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

#include "libavutil/mem.h"
#include "libavcodec/av1.h"
#include "libavcodec/av1_parse.h"
#include "av1.h"
#include "avio.h"

int ff_av1_filter_obus(AVIOContext *pb, const uint8_t *buf, int size)
{
    const uint8_t *end = buf + size;
    int64_t obu_size;
    int start_pos, type, temporal_id, spatial_id;

    size = 0;
    while (buf < end) {
        int ret = parse_obu_header(buf, end - buf, &obu_size, &start_pos,
                                   &type, &temporal_id, &spatial_id);
        if (ret < 0)
            return ret;

        obu_size += start_pos;
        if (obu_size > INT_MAX)
            return AVERROR_INVALIDDATA;

        switch (type) {
        case AV1_OBU_TEMPORAL_DELIMITER:
        case AV1_OBU_REDUNDANT_FRAME_HEADER:
        case AV1_OBU_PADDING:
            break;
        default:
            avio_write(pb, buf, obu_size);
            size += obu_size;
            break;
        }
        buf += obu_size;
    }

    return size;
}

int ff_av1_filter_obus_buf(const uint8_t *buf, uint8_t **out, int *size)
{
    AVIOContext *pb;
    int ret;

    ret = avio_open_dyn_buf(&pb);
    if (ret < 0)
        return ret;

    ret = ff_av1_filter_obus(pb, buf, *size);
    if (ret < 0)
        return ret;

    av_freep(out);
    *size = avio_close_dyn_buf(pb, out);

    return ret;
}

int ff_isom_write_av1c(AVIOContext *pb, const uint8_t *buf, int size)
{
    int64_t obu_size;
    int start_pos, type, temporal_id, spatial_id;

    while (size > 0) {
        int ret = parse_obu_header(buf, size, &obu_size, &start_pos,
                                   &type, &temporal_id, &spatial_id);
        if (ret < 0)
            return ret;

        obu_size += start_pos;
        if (obu_size > INT_MAX)
            return AVERROR_INVALIDDATA;

        switch (type) {
        case AV1_OBU_SEQUENCE_HEADER:
        case AV1_OBU_METADATA:
            avio_write(pb, buf, obu_size);
            break;
        default:
            break;
        }
        size -= obu_size;
        buf  += obu_size;
    }

    return 0;
}
