/*
 * RTP packetization for AMR audio
 * Copyright (c) 2007 Luca Abeni
 * Copyright (c) 2009 Martin Storsjo
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

#include "avformat.h"
#include "rtpenc.h"

/**
 * Packetize AMR frames into RTP packets according to RFC 3267,
 * in octet-aligned mode.
 */
void ff_rtp_send_amr(AVFormatContext *s1, const uint8_t *buff, int size)
{
    RTPMuxContext *s          = s1->priv_data;
    AVStream *st              = s1->streams[0];
    int max_header_toc_size   = 1 + s->max_frames_per_packet;
    uint8_t *p;
    int len;

    /* Test if the packet must be sent. */
    len = s->buf_ptr - s->buf;
    if (s->num_frames &&
        (s->num_frames == s->max_frames_per_packet ||
         len + size - 1 > s->max_payload_size ||
         av_compare_ts(s->cur_timestamp - s->timestamp, st->time_base,
                       s1->max_delay, AV_TIME_BASE_Q) >= 0)) {
        int header_size = s->num_frames + 1;
        p = s->buf + max_header_toc_size - header_size;
        if (p != s->buf)
            memmove(p, s->buf, header_size);

        ff_rtp_send_data(s1, p, s->buf_ptr - p, 1);

        s->num_frames = 0;
    }

    if (!s->num_frames) {
        s->buf[0]    = 0xf0;
        s->buf_ptr   = s->buf + max_header_toc_size;
        s->timestamp = s->cur_timestamp;
    } else {
        /* Mark the previous TOC entry as having more entries following. */
        s->buf[1 + s->num_frames - 1] |= 0x80;
    }

    /* Copy the frame type and quality bits. */
    s->buf[1 + s->num_frames++] = buff[0] & 0x7C;
    buff++;
    size--;
    memcpy(s->buf_ptr, buff, size);
    s->buf_ptr += size;
}
