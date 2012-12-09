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
 * ( http://tools.ietf.org/html/draft-ietf-payload-vp8-05 ) */
void ff_rtp_send_vp8(AVFormatContext *s1, const uint8_t *buf, int size)
{
    RTPMuxContext *s = s1->priv_data;
    int len, max_packet_size, header_size;

    s->buf_ptr      = s->buf;
    s->timestamp    = s->cur_timestamp;

    // extended control bit set, reference frame, start of partition,
    // partition id 0
    *s->buf_ptr++ = 0x90;
    *s->buf_ptr++ = 0x80; // Picture id present
    *s->buf_ptr++ = s->frame_count++ & 0x7f;
    // Calculate the number of remaining bytes
    header_size     = s->buf_ptr - s->buf;
    max_packet_size = s->max_payload_size - header_size;

    while (size > 0) {
        len = FFMIN(size, max_packet_size);

        memcpy(s->buf_ptr, buf, len);
        // marker bit is last packet in frame
        ff_rtp_send_data(s1, s->buf, len + header_size, size == len);

        size         -= len;
        buf          += len;
        // Clear the partition start bit, keep the rest of the header untouched
        s->buf[0]    &= ~0x10;
    }
}
