/*
 * RTP packetizer for HEVC/H.265 payload format (draft version 6)
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

#include "avc.h"
#include "avformat.h"
#include "rtpenc.h"

#define RTP_HEVC_HEADERS_SIZE 3

static void nal_send(AVFormatContext *ctx, const uint8_t *buf, int len, int last_packet_of_frame)
{
    RTPMuxContext *rtp_ctx = ctx->priv_data;
    int rtp_payload_size   = rtp_ctx->max_payload_size - RTP_HEVC_HEADERS_SIZE;
    int nal_type           = (buf[0] >> 1) & 0x3F;

    /* send it as one single NAL unit? */
    if (len <= rtp_ctx->max_payload_size) {
        /* use the original NAL unit buffer and transmit it as RTP payload */
        ff_rtp_send_data(ctx, buf, len, last_packet_of_frame);
    } else {
        /*
          create the HEVC payload header and transmit the buffer as fragmentation units (FU)

             0                   1
             0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
            +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
            |F|   Type    |  LayerId  | TID |
            +-------------+-----------------+

               F       = 0
               Type    = 49 (fragmentation unit (FU))
               LayerId = 0
               TID     = 1
         */
        rtp_ctx->buf[0] = 49 << 1;
        rtp_ctx->buf[1] = 1;

        /*
              create the FU header

              0 1 2 3 4 5 6 7
             +-+-+-+-+-+-+-+-+
             |S|E|  FuType   |
             +---------------+

                S       = variable
                E       = variable
                FuType  = NAL unit type
         */
        rtp_ctx->buf[2]  = nal_type;
        /* set the S bit: mark as start fragment */
        rtp_ctx->buf[2] |= 1 << 7;

        /* pass the original NAL header */
        buf += 2;
        len -= 2;

        while (len > rtp_payload_size) {
            /* complete and send current RTP packet */
            memcpy(&rtp_ctx->buf[RTP_HEVC_HEADERS_SIZE], buf, rtp_payload_size);
            ff_rtp_send_data(ctx, rtp_ctx->buf, rtp_ctx->max_payload_size, 0);

            buf += rtp_payload_size;
            len -= rtp_payload_size;

            /* reset the S bit */
            rtp_ctx->buf[2] &= ~(1 << 7);
        }

        /* set the E bit: mark as last fragment */
        rtp_ctx->buf[2] |= 1 << 6;

        /* complete and send last RTP packet */
        memcpy(&rtp_ctx->buf[RTP_HEVC_HEADERS_SIZE], buf, len);
        ff_rtp_send_data(ctx, rtp_ctx->buf, len + 2, last_packet_of_frame);
    }
}

void ff_rtp_send_hevc(AVFormatContext *ctx, const uint8_t *frame_buf, int frame_size)
{
    const uint8_t *next_NAL_unit;
    const uint8_t *buf_ptr, *buf_end = frame_buf + frame_size;
    RTPMuxContext *rtp_ctx = ctx->priv_data;

    /* use the default 90 KHz time stamp */
    rtp_ctx->timestamp = rtp_ctx->cur_timestamp;

    if (rtp_ctx->nal_length_size)
        buf_ptr = ff_avc_mp4_find_startcode(frame_buf, buf_end, rtp_ctx->nal_length_size) ? frame_buf : buf_end;
    else
        buf_ptr = ff_avc_find_startcode(frame_buf, buf_end);

    /* find all NAL units and send them as separate packets */
    while (buf_ptr < buf_end) {
        if (rtp_ctx->nal_length_size) {
            next_NAL_unit = ff_avc_mp4_find_startcode(buf_ptr, buf_end, rtp_ctx->nal_length_size);
            if (!next_NAL_unit)
                next_NAL_unit = buf_end;

            buf_ptr += rtp_ctx->nal_length_size;
        } else {
            while (!*(buf_ptr++))
                ;
            next_NAL_unit = ff_avc_find_startcode(buf_ptr, buf_end);
        }
        /* send the next NAL unit */
        nal_send(ctx, buf_ptr, next_NAL_unit - buf_ptr, next_NAL_unit == buf_end);

        /* jump to the next NAL unit */
        buf_ptr = next_NAL_unit;
    }
}
