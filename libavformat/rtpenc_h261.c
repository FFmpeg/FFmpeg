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

#define RTP_H261_HEADER_SIZE 4

static const uint8_t *find_resync_marker_reverse(const uint8_t *restrict start,
                                                 const uint8_t *restrict end)
{
    const uint8_t *p = end - 1;
    start += 1; /* Make sure we never return the original start. */
    for (; p > start; p--) {
        if (p[0] == 0 && p[1] == 1)
            return p;
    }
    return end;
}

void ff_rtp_send_h261(AVFormatContext *ctx, const uint8_t *frame_buf, int frame_size)
{
    int cur_frame_size;
    int last_packet_of_frame;
    RTPMuxContext *rtp_ctx = ctx->priv_data;

    /* use the default 90 KHz time stamp */
    rtp_ctx->timestamp = rtp_ctx->cur_timestamp;

    /* continue as long as not all frame data is processed */
    while (frame_size > 0) {
        /*
         * encode the H.261 payload header according to section 4.1 of RFC 4587:
         * (uses 4 bytes between RTP header and H.261 stream per packet)
         *
         *    0                   1                   2                   3
         *    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
         *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         *   |SBIT |EBIT |I|V| GOBN  |   MBAP  |  QUANT  |  HMVD   |  VMVD   |
         *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         *
         *      Start bit position (SBIT): 3 bits
         *      End bit position (EBIT): 3 bits
         *      INTRA-frame encoded data (I): 1 bit
         *      Motion Vector flag (V): 1 bit
         *      GOB number (GOBN): 4 bits
         *      Macroblock address predictor (MBAP): 5 bits
         *      Quantizer (QUANT): 5 bits
         *      Horizontal motion vector data (HMVD): 5 bits
         *      Vertical motion vector data (VMVD): 5 bits
         */
        rtp_ctx->buf[0] = 1; /* sbit=0, ebit=0, i=0, v=1 */
        rtp_ctx->buf[1] = 0; /* gobn=0, mbap=0 */
        rtp_ctx->buf[2] = 0; /* quant=0, hmvd=5 */
        rtp_ctx->buf[3] = 0; /* vmvd=0 */
        if (frame_size < 2 || frame_buf[0] != 0 || frame_buf[1] != 1) {
            /* A full, correct fix for this would be to make the H.261 encoder
             * support inserting extra GOB headers (triggered by setting e.g.
             * "-ps 1"), and including information about macroblock boundaries
             * (such as for h263_rfc2190). */
            av_log(ctx, AV_LOG_WARNING,
                   "RTP/H.261 packet not cut at a GOB boundary, not signaled correctly\n");
        }

        cur_frame_size = FFMIN(rtp_ctx->max_payload_size - RTP_H261_HEADER_SIZE, frame_size);

        /* look for a better place to split the frame into packets */
        if (cur_frame_size < frame_size) {
            const uint8_t *packet_end = find_resync_marker_reverse(frame_buf,
                                                                   frame_buf + cur_frame_size);
            cur_frame_size = packet_end - frame_buf;
        }

        /* calculate the "marker" bit for the RTP header */
        last_packet_of_frame = cur_frame_size == frame_size;

        /* complete and send RTP packet */
        memcpy(&rtp_ctx->buf[RTP_H261_HEADER_SIZE], frame_buf, cur_frame_size);
        ff_rtp_send_data(ctx, rtp_ctx->buf, RTP_H261_HEADER_SIZE + cur_frame_size, last_packet_of_frame);

        frame_buf  += cur_frame_size;
        frame_size -= cur_frame_size;
    }
}
