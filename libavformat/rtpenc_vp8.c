/*
 * RTP VP8 Packetizer
 * Copyright (c) 2010 Josh Allmann
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "rtpenc.h"

/* Based on a draft spec for VP8 RTP.
 * ( http://www.webmproject.org/code/specs/rtp/ ) */
void ff_rtp_send_vp8(AVFormatContext *s1, const uint8_t *buf, int size)
{
    RTPMuxContext *s = s1->priv_data;
    int len, max_packet_size;

    s->buf_ptr      = s->buf;
    s->timestamp    = s->cur_timestamp;
    max_packet_size = s->max_payload_size - 1; // minus one for header byte

    *s->buf_ptr++ = 1; // 0b1 indicates start of frame
    while (size > 0) {
        len = FFMIN(size, max_packet_size);

        memcpy(s->buf_ptr, buf, len);
        ff_rtp_send_data(s1, s->buf, len+1, size == len); // marker bit is last packet in frame

        size         -= len;
        buf          += len;
        s->buf_ptr    = s->buf;
        *s->buf_ptr++ = 0; // payload descriptor
    }
}
