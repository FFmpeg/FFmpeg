/*
 * RTP parser for DV payload format (RFC 6469)
 * Copyright (c) 2015 Thomas Volkert <thomas@homer-conferencing.com>
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

#include "libavutil/avstring.h"

#include "libavcodec/bytestream.h"

#include "avio_internal.h"
#include "rtpdec_formats.h"

struct PayloadContext {
    AVIOContext *buf;
    uint32_t    timestamp;
    int         bundled_audio;
};

static av_cold void dv_close_context(PayloadContext *data)
{
    ffio_free_dyn_buf(&data->buf);
}

static av_cold int dv_sdp_parse_fmtp_config(AVFormatContext *s,
                                            AVStream *stream,
                                            PayloadContext *dv_data,
                                            const char *attr, const char *value)
{
    /* does the DV stream include audio? */
    if (!strcmp(attr, "audio") && !strcmp(value, "bundled"))
        dv_data->bundled_audio = 1;

    /* extract the DV profile */
    if (!strcmp(attr, "encode")) {
        /* SD-VCR/525-60 */
        /* SD-VCR/625-50 */
        /* HD-VCR/1125-60 */
        /* HD-VCR/1250-50 */
        /* SDL-VCR/525-60 */
        /* SDL-VCR/625-50 */
        /* 314M-25/525-60 */
        /* 314M-25/625-50 */
        /* 314M-50/525-60 */
        /* 314M-50/625-50 */
        /* 370M/1080-60i */
        /* 370M/1080-50i */
        /* 370M/720-60p */
        /* 370M/720-50p */
        /* 306M/525-60 (for backward compatibility) */
        /* 306M/625-50 (for backward compatibility) */
    }

    return 0;
}

static av_cold int dv_parse_sdp_line(AVFormatContext *ctx, int st_index,
                                     PayloadContext *dv_data, const char *line)
{
    AVStream *current_stream;
    const char *sdp_line_ptr = line;

    if (st_index < 0)
        return 0;

    current_stream = ctx->streams[st_index];

    if (av_strstart(sdp_line_ptr, "fmtp:", &sdp_line_ptr)) {
        return ff_parse_fmtp(ctx, current_stream, dv_data, sdp_line_ptr,
                             dv_sdp_parse_fmtp_config);
    }

    return 0;
}

static int dv_handle_packet(AVFormatContext *ctx, PayloadContext *rtp_dv_ctx,
                            AVStream *st, AVPacket *pkt, uint32_t *timestamp,
                            const uint8_t *buf, int len, uint16_t seq,
                            int flags)
{
    int res = 0;

    /* drop data of previous packets in case of non-continuous (lossy) packet stream */
    if (rtp_dv_ctx->buf && rtp_dv_ctx->timestamp != *timestamp) {
        ffio_free_dyn_buf(&rtp_dv_ctx->buf);
    }

    /* sanity check for size of input packet: 1 byte payload at least */
    if (len < 1) {
        av_log(ctx, AV_LOG_ERROR, "Too short RTP/DV packet, got %d bytes\n", len);
        return AVERROR_INVALIDDATA;
    }

    /* start frame buffering with new dynamic buffer */
    if (!rtp_dv_ctx->buf) {
        res = avio_open_dyn_buf(&rtp_dv_ctx->buf);
        if (res < 0)
            return res;
        /* update the timestamp in the frame packet with the one from the RTP packet */
        rtp_dv_ctx->timestamp = *timestamp;
    }

    /* write the fragment to the dyn. buffer */
    avio_write(rtp_dv_ctx->buf, buf, len);

    /* RTP marker bit means: last fragment of current frame was received;
       otherwise, an additional fragment is needed for the current frame */
    if (!(flags & RTP_FLAG_MARKER))
        return AVERROR(EAGAIN);

    /* close frame buffering and create resulting A/V packet */
    res = ff_rtp_finalize_packet(pkt, &rtp_dv_ctx->buf, st->index);
    if (res < 0)
        return res;

    return 0;
}

RTPDynamicProtocolHandler ff_dv_dynamic_handler = {
    .enc_name         = "DV",
    .codec_type       = AVMEDIA_TYPE_VIDEO,
    .codec_id         = AV_CODEC_ID_DVVIDEO,
    .need_parsing     = AVSTREAM_PARSE_FULL,
    .parse_sdp_a_line = dv_parse_sdp_line,
    .priv_data_size   = sizeof(PayloadContext),
    .close             = dv_close_context,
    .parse_packet     = dv_handle_packet,
};
