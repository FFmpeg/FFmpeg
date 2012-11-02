/*
 * RTP VP8 Depacketizer
 * Copyright (c) 2010 Josh Allmann
 * Copyright (c) 2012 Martin Storsjo
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
 * @brief RTP support for the VP8 payload
 * @author Josh Allmann <joshua.allmann@gmail.com>
 * @see http://tools.ietf.org/html/draft-ietf-payload-vp8-05
 */

#include "libavcodec/bytestream.h"

#include "rtpdec_formats.h"

struct PayloadContext {
    AVIOContext *data;
    uint32_t       timestamp;
};

static int vp8_handle_packet(AVFormatContext *ctx,
                             PayloadContext *vp8,
                             AVStream *st,
                             AVPacket *pkt,
                             uint32_t *timestamp,
                             const uint8_t *buf,
                             int len, int flags)
{
    int start_partition, end_packet;
    int extended_bits, part_id;
    int pictureid_present = 0, tl0picidx_present = 0, tid_present = 0,
        keyidx_present = 0;

    if (len < 1)
        return AVERROR_INVALIDDATA;

    extended_bits   = buf[0] & 0x80;
    start_partition = buf[0] & 0x10;
    part_id         = buf[0] & 0x0f;
    end_packet      = flags & RTP_FLAG_MARKER;
    buf++;
    len--;
    if (extended_bits) {
        if (len < 1)
            return AVERROR_INVALIDDATA;
        pictureid_present = buf[0] & 0x80;
        tl0picidx_present = buf[0] & 0x40;
        tid_present       = buf[0] & 0x20;
        keyidx_present    = buf[0] & 0x10;
        buf++;
        len--;
    }
    if (pictureid_present) {
        int size;
        if (len < 1)
            return AVERROR_INVALIDDATA;
        size = buf[0] & 0x80 ? 2 : 1;
        buf += size;
        len -= size;
    }
    if (tl0picidx_present) {
        // Ignoring temporal level zero index
        buf++;
        len--;
    }
    if (tid_present || keyidx_present) {
        // Ignoring temporal layer index, layer sync bit and keyframe index
        buf++;
        len--;
    }
    if (len < 1)
        return AVERROR_INVALIDDATA;

    if (start_partition && part_id == 0) {
        int res;
        if (vp8->data) {
            uint8_t *tmp;
            avio_close_dyn_buf(vp8->data, &tmp);
            av_free(tmp);
            vp8->data = NULL;
        }
        if ((res = avio_open_dyn_buf(&vp8->data)) < 0)
            return res;
        vp8->timestamp = *timestamp;
     }

    if (!vp8->data || vp8->timestamp != *timestamp) {
        av_log(ctx, AV_LOG_WARNING,
               "Received no start marker; dropping frame\n");
        return AVERROR(EAGAIN);
    }

    avio_write(vp8->data, buf, len);

    if (end_packet) {
        int ret = ff_rtp_finalize_packet(pkt, &vp8->data, st->index);
        if (ret < 0)
            return ret;
        return 0;
    }

    return AVERROR(EAGAIN);
}

static PayloadContext *vp8_new_context(void)
{
    return av_mallocz(sizeof(PayloadContext));
}

static void vp8_free_context(PayloadContext *vp8)
{
    if (vp8->data) {
        uint8_t *tmp;
        avio_close_dyn_buf(vp8->data, &tmp);
        av_free(tmp);
    }
    av_free(vp8);
}

RTPDynamicProtocolHandler ff_vp8_dynamic_handler = {
    .enc_name       = "VP8",
    .codec_type     = AVMEDIA_TYPE_VIDEO,
    .codec_id       = AV_CODEC_ID_VP8,
    .alloc          = vp8_new_context,
    .free           = vp8_free_context,
    .parse_packet   = vp8_handle_packet,
};
