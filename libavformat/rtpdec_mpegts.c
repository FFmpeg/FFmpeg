/*
 * RTP MPEG2TS depacketizer
 * Copyright (c) 2003 Fabrice Bellard
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

#include "mpegts.h"
#include "rtpdec_formats.h"

struct PayloadContext {
    struct MpegTSContext *ts;
    int read_buf_index;
    int read_buf_size;
    uint8_t buf[RTP_MAX_PACKET_LENGTH];
};

static PayloadContext *mpegts_new_context(void)
{
    return av_mallocz(sizeof(PayloadContext));
}

static void mpegts_free_context(PayloadContext *data)
{
    if (!data)
        return;
    if (data->ts)
        ff_mpegts_parse_close(data->ts);
    av_free(data);
}

static int mpegts_init(AVFormatContext *ctx, int st_index, PayloadContext *data)
{
    data->ts = ff_mpegts_parse_open(ctx);
    if (!data->ts)
        return AVERROR(ENOMEM);
    return 0;
}

static int mpegts_handle_packet(AVFormatContext *ctx, PayloadContext *data,
                                AVStream *st, AVPacket *pkt, uint32_t *timestamp,
                                const uint8_t *buf, int len, uint16_t seq,
                                int flags)
{
    int ret;

    // We don't want to use the RTP timestamps at all. If the mpegts demuxer
    // doesn't set any pts/dts, the generic rtpdec code shouldn't try to
    // fill it in either, since the mpegts and RTP timestamps are in totally
    // different ranges.
    *timestamp = RTP_NOTS_VALUE;

    if (!data->ts)
        return AVERROR(EINVAL);

    if (!buf) {
        if (data->read_buf_index >= data->read_buf_size)
            return AVERROR(EAGAIN);
        ret = ff_mpegts_parse_packet(data->ts, pkt, data->buf + data->read_buf_index,
                                     data->read_buf_size - data->read_buf_index);
        if (ret < 0)
            return AVERROR(EAGAIN);
        data->read_buf_index += ret;
        if (data->read_buf_index < data->read_buf_size)
            return 1;
        else
            return 0;
    }

    ret = ff_mpegts_parse_packet(data->ts, pkt, buf, len);
    /* The only error that can be returned from ff_mpegts_parse_packet
     * is "no more data to return from the provided buffer", so return
     * AVERROR(EAGAIN) for all errors */
    if (ret < 0)
        return AVERROR(EAGAIN);
    if (ret < len) {
        data->read_buf_size = FFMIN(len - ret, sizeof(data->buf));
        memcpy(data->buf, buf + ret, data->read_buf_size);
        data->read_buf_index = 0;
        return 1;
    }
    return 0;
}

RTPDynamicProtocolHandler ff_mpegts_dynamic_handler = {
    .codec_type        = AVMEDIA_TYPE_DATA,
    .parse_packet      = mpegts_handle_packet,
    .alloc             = mpegts_new_context,
    .init              = mpegts_init,
    .free              = mpegts_free_context,
    .static_payload_id = 33,
};
