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
        int len = parse_obu_header(buf, end - buf, &obu_size, &start_pos,
                                   &type, &temporal_id, &spatial_id);
        if (len < 0)
            return len;

        switch (type) {
        case AV1_OBU_TEMPORAL_DELIMITER:
        case AV1_OBU_REDUNDANT_FRAME_HEADER:
        case AV1_OBU_PADDING:
            break;
        default:
            avio_write(pb, buf, len);
            size += len;
            break;
        }
        buf += len;
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
    AVIOContext *seq_pb = NULL, *meta_pb = NULL;
    uint8_t *seq = NULL, *meta = NULL;
    int64_t obu_size;
    int start_pos, type, temporal_id, spatial_id;
    int ret, nb_seq = 0, seq_size, meta_size;

    if (size <= 0)
        return AVERROR_INVALIDDATA;

    ret = avio_open_dyn_buf(&seq_pb);
    if (ret < 0)
        return ret;
    ret = avio_open_dyn_buf(&meta_pb);
    if (ret < 0)
        goto fail;

    while (size > 0) {
        int len = parse_obu_header(buf, size, &obu_size, &start_pos,
                                   &type, &temporal_id, &spatial_id);
        if (len < 0) {
            ret = len;
            goto fail;
        }

        switch (type) {
        case AV1_OBU_SEQUENCE_HEADER:
            nb_seq++;
            if (!obu_size || nb_seq > 1) {
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }
            avio_write(seq_pb, buf, len);
            break;
        case AV1_OBU_METADATA:
            if (!obu_size) {
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }
            avio_write(meta_pb, buf, len);
            break;
        default:
            break;
        }
        size -= len;
        buf  += len;
    }

    seq_size  = avio_close_dyn_buf(seq_pb, &seq);
    if (!seq_size) {
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }
    avio_write(pb, seq, seq_size);

    meta_size = avio_close_dyn_buf(meta_pb, &meta);
    if (meta_size)
        avio_write(pb, meta, meta_size);

fail:
    if (!seq)
        avio_close_dyn_buf(seq_pb, &seq);
    if (!meta)
        avio_close_dyn_buf(meta_pb, &meta);
    av_free(seq);
    av_free(meta);

    return ret;
}
