/*
 * RTP packetization for H.263 video
 * Copyright (c) 2009 Luca Abeni
 * Copyright (c) 2009 Martin Storsjo
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

const uint8_t *ff_h263_find_resync_marker_reverse(const uint8_t *restrict start,
                                                  const uint8_t *restrict end)
{
    const uint8_t *p = end - 1;
    start += 1; /* Make sure we never return the original start. */
    for (; p > start; p -= 2) {
        if (!*p) {
            if      (!p[ 1] && p[2]) return p;
            else if (!p[-1] && p[1]) return p - 1;
        }
    }
    return end;
}

/**
 * Packetize H.263 frames into RTP packets according to RFC 4629
 */
void ff_rtp_send_h263(AVFormatContext *s1, const uint8_t *buf1, int size)
{
    RTPMuxContext *s = s1->priv_data;
    int len, max_packet_size;
    uint8_t *q;

    max_packet_size = s->max_payload_size;

    while (size > 0) {
        q = s->buf;
        if (size >= 2 && (buf1[0] == 0) && (buf1[1] == 0)) {
            *q++ = 0x04;
            buf1 += 2;
            size -= 2;
        } else {
            *q++ = 0;
        }
        *q++ = 0;

        len = FFMIN(max_packet_size - 2, size);

        /* Look for a better place to split the frame into packets. */
        if (len < size) {
            const uint8_t *end = ff_h263_find_resync_marker_reverse(buf1,
                                                                    buf1 + len);
            len = end - buf1;
        }

        memcpy(q, buf1, len);
        q += len;

        /* 90 KHz time stamp */
        s->timestamp = s->cur_timestamp;
        ff_rtp_send_data(s1, s->buf, q - s->buf, (len == size));

        buf1 += len;
        size -= len;
    }
}
