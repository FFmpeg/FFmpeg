/*
 * RTP parser for AC3 payload format (RFC 4184)
 * Copyright (c) 2015 Gilles Chanteperdrix <gch@xenomai.org>
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

#include "avformat.h"
#include "avio_internal.h"
#include "rtpdec_formats.h"

#define RTP_AC3_PAYLOAD_HEADER_SIZE 2

struct PayloadContext {
    unsigned nr_frames;
    unsigned last_frame;
    uint32_t timestamp;
    AVIOContext *fragment;
};

static void ac3_close_context(PayloadContext *data)
{
    ffio_free_dyn_buf(&data->fragment);
}

static int ac3_handle_packet(AVFormatContext *ctx, PayloadContext *data,
                             AVStream *st, AVPacket *pkt, uint32_t *timestamp,
                             const uint8_t *buf, int len, uint16_t seq,
                             int flags)
{
    unsigned frame_type;
    unsigned nr_frames;
    int err;

    if (len < RTP_AC3_PAYLOAD_HEADER_SIZE + 1) {
        av_log(ctx, AV_LOG_ERROR, "Invalid %d bytes packet\n", len);
        return AVERROR_INVALIDDATA;
    }

    frame_type = buf[0] & 0x3;
    nr_frames = buf[1];
    buf += RTP_AC3_PAYLOAD_HEADER_SIZE;
    len -= RTP_AC3_PAYLOAD_HEADER_SIZE;

    switch (frame_type) {
    case 0: /* One or more complete frames */
        if (!nr_frames) {
            av_log(ctx, AV_LOG_ERROR, "Invalid AC3 packet data\n");
            return AVERROR_INVALIDDATA;
        }
        if (av_new_packet(pkt, len)) {
            av_log(ctx, AV_LOG_ERROR, "Out of memory.\n");
            return AVERROR(ENOMEM);
        }

        pkt->stream_index = st->index;
        memcpy(pkt->data, buf, len);
        return 0;

    case 1:
    case 2: /* First fragment */
        ffio_free_dyn_buf(&data->fragment);

        data->last_frame = 1;
        data->nr_frames = nr_frames;
        err = avio_open_dyn_buf(&data->fragment);
        if (err < 0)
            return err;

        avio_write(data->fragment, buf, len);
        data->timestamp = *timestamp;
        return AVERROR(EAGAIN);

    case 3: /* Fragment other than first */
        if (!data->fragment) {
            av_log(ctx, AV_LOG_WARNING,
                   "Received packet without a start fragment; dropping.\n");
            return AVERROR(EAGAIN);
        }
        if (nr_frames != data->nr_frames ||
            data->timestamp != *timestamp) {
            ffio_free_dyn_buf(&data->fragment);
            av_log(ctx, AV_LOG_ERROR, "Invalid packet received\n");
            return AVERROR_INVALIDDATA;
        }

        avio_write(data->fragment, buf, len);
        data->last_frame++;
    }

    if (!(flags & RTP_FLAG_MARKER))
        return AVERROR(EAGAIN);

    if (data->last_frame != data->nr_frames) {
        ffio_free_dyn_buf(&data->fragment);
        av_log(ctx, AV_LOG_ERROR, "Missed %d packets\n",
               data->nr_frames - data->last_frame);
        return AVERROR_INVALIDDATA;
    }

    err = ff_rtp_finalize_packet(pkt, &data->fragment, st->index);
    if (err < 0) {
        av_log(ctx, AV_LOG_ERROR,
               "Error occurred when getting fragment buffer.\n");
        return err;
    }

    return 0;
}

const RTPDynamicProtocolHandler ff_ac3_dynamic_handler = {
    .enc_name           = "ac3",
    .codec_type         = AVMEDIA_TYPE_AUDIO,
    .codec_id           = AV_CODEC_ID_AC3,
    .need_parsing       = AVSTREAM_PARSE_FULL,
    .priv_data_size     = sizeof(PayloadContext),
    .close              = ac3_close_context,
    .parse_packet       = ac3_handle_packet,
};
