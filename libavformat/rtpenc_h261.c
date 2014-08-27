/*
 * RTP packetization for H.261 video (RFC 4587)
 * Copyright (c) 2014 Thomas Volkert <thomas@homer-conferencing.com>
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
#include "rtpenc.h"

void ff_rtp_send_h261(AVFormatContext *s1, const uint8_t *frame_buf, int frame_size)
{
    RTPMuxContext *rtp_ctx = s1->priv_data;
    int processed_frame_size;
    int last_packet_of_frame;
    uint8_t *tmp_buf_ptr;

    /* use the default 90 KHz time stamp */
    rtp_ctx->timestamp = rtp_ctx->cur_timestamp;

    /* continue as long as not all frame data is processed */
    while (frame_size > 0) {
        tmp_buf_ptr = rtp_ctx->buf;
        *tmp_buf_ptr++ = 1; /* V=1 */
        *tmp_buf_ptr++ = 0;
        *tmp_buf_ptr++ = 0;
        *tmp_buf_ptr++ = 0;

        processed_frame_size = FFMIN(rtp_ctx->max_payload_size - 4, frame_size);

        //XXX: parse the h.261 bitstream and improve frame splitting here

        last_packet_of_frame = (processed_frame_size == frame_size);

        memcpy(tmp_buf_ptr, frame_buf, processed_frame_size);
        tmp_buf_ptr += processed_frame_size;

        ff_rtp_send_data(s1, rtp_ctx->buf, tmp_buf_ptr - rtp_ctx->buf, last_packet_of_frame);

        frame_buf += processed_frame_size;
        frame_size -= processed_frame_size;
    }
}
