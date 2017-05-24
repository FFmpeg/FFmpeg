/*
 * RTP packetizer for VP9 payload format (draft version 02) - experimental
 * Copyright (c) 2016 Thomas Volkert <thomas@netzeal.de>
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

#include "rtpenc.h"

#define RTP_VP9_DESC_REQUIRED_SIZE 1

void ff_rtp_send_vp9(AVFormatContext *ctx, const uint8_t *buf, int size)
{
    RTPMuxContext *rtp_ctx = ctx->priv_data;
    int len;

    rtp_ctx->timestamp  = rtp_ctx->cur_timestamp;
    rtp_ctx->buf_ptr    = rtp_ctx->buf;

    /* mark the first fragment */
    *rtp_ctx->buf_ptr++ = 0x08;

    while (size > 0) {
        len = FFMIN(size, rtp_ctx->max_payload_size - RTP_VP9_DESC_REQUIRED_SIZE);

        if (len == size) {
            /* mark the last fragment */
            rtp_ctx->buf[0] |= 0x04;
        }

        memcpy(rtp_ctx->buf_ptr, buf, len);
        ff_rtp_send_data(ctx, rtp_ctx->buf, len + RTP_VP9_DESC_REQUIRED_SIZE, size == len);

        size            -= len;
        buf             += len;

        /* clear the end bit */
        rtp_ctx->buf[0] &= ~0x08;
    }
}
