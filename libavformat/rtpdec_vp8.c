/*
 * RTP VP8 Depacketizer
 * Copyright (c) 2010 Josh Allmann
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
 * @see http://www.webmproject.org/code/specs/rtp/
 */

#include "libavcodec/bytestream.h"

#include "rtpdec_formats.h"

struct PayloadContext {
    AVIOContext *data;
    uint32_t       timestamp;
    int is_keyframe;
};

static void prepare_packet(AVPacket *pkt, PayloadContext *vp8, int stream)
{
    av_init_packet(pkt);
    pkt->stream_index = stream;
    pkt->flags        = vp8->is_keyframe ? AV_PKT_FLAG_KEY : 0;
    pkt->size         = avio_close_dyn_buf(vp8->data, &pkt->data);
    pkt->destruct     = av_destruct_packet;
    vp8->data         = NULL;
}

static int vp8_handle_packet(AVFormatContext *ctx,
                             PayloadContext *vp8,
                             AVStream *st,
                             AVPacket *pkt,
                             uint32_t *timestamp,
                             const uint8_t *buf,
                             int len, int flags)
{
    int start_packet, end_packet, has_au, ret = AVERROR(EAGAIN);

    if (!buf) {
        // only called when vp8_handle_packet returns 1
        if (!vp8->data) {
            av_log(ctx, AV_LOG_ERROR, "Invalid VP8 data passed\n");
            return AVERROR_INVALIDDATA;
        }
        prepare_packet(pkt, vp8, st->index);
        *timestamp = vp8->timestamp;
        return 0;
    }

    start_packet = *buf & 1;
    end_packet   = flags & RTP_FLAG_MARKER;
    has_au       = *buf & 2;
    buf++;
    len--;

    if (start_packet) {
        int res;
        uint32_t ts = *timestamp;
        if (vp8->data) {
            // missing end marker; return old frame anyway. untested
            prepare_packet(pkt, vp8, st->index);
            *timestamp = vp8->timestamp; // reset timestamp from old frame

            // if current frame fits into one rtp packet, need to hold
            // that for the next av_get_packet call
            ret = end_packet ? 1 : 0;
        }
        if ((res = avio_open_dyn_buf(&vp8->data)) < 0)
            return res;
        vp8->is_keyframe = *buf & 1;
        vp8->timestamp   = ts;
     }

    if (!vp8->data || vp8->timestamp != *timestamp && ret == AVERROR(EAGAIN)) {
        av_log(ctx, AV_LOG_WARNING,
               "Received no start marker; dropping frame\n");
        return AVERROR(EAGAIN);
    }

    // cycle through VP8AU headers if needed
    // not tested with actual VP8AUs
    while (len) {
        int au_len = len;
        if (has_au && len > 2) {
            au_len = AV_RB16(buf);
            buf += 2;
            len -= 2;
            if (buf + au_len > buf + len) {
                av_log(ctx, AV_LOG_ERROR, "Invalid VP8AU length\n");
                return AVERROR_INVALIDDATA;
            }
        }

        avio_write(vp8->data, buf, au_len);
        buf += au_len;
        len -= au_len;
    }

    if (ret != AVERROR(EAGAIN)) // did we miss a end marker?
        return ret;

    if (end_packet) {
        prepare_packet(pkt, vp8, st->index);
        return 0;
    }

    return AVERROR(EAGAIN);
}

static PayloadContext *vp8_new_context(void)
{
    av_log(NULL, AV_LOG_ERROR, "RTP VP8 payload implementation is incompatible "
                               "with the latest spec drafts.\n");
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
