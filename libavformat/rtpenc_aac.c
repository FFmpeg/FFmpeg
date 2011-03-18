/*
 * copyright (c) 2007 Luca Abeni
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


void ff_rtp_send_aac(AVFormatContext *s1, const uint8_t *buff, int size)
{
    RTPMuxContext *s = s1->priv_data;
    int len, max_packet_size;
    uint8_t *p;
    const int max_frames_per_packet = s->max_frames_per_packet ? s->max_frames_per_packet : 5;
    const int max_au_headers_size = 2 + 2 * max_frames_per_packet;

    /* skip ADTS header, if present */
    if ((s1->streams[0]->codec->extradata_size) == 0) {
        size -= 7;
        buff += 7;
    }
    max_packet_size = s->max_payload_size - max_au_headers_size;

    /* test if the packet must be sent */
    len = (s->buf_ptr - s->buf);
    if ((s->num_frames == max_frames_per_packet) || (len && (len + size) > s->max_payload_size)) {
        int au_size = s->num_frames * 2;

        p = s->buf + max_au_headers_size - au_size - 2;
        if (p != s->buf) {
            memmove(p + 2, s->buf + 2, au_size);
        }
        /* Write the AU header size */
        p[0] = ((au_size * 8) & 0xFF) >> 8;
        p[1] = (au_size * 8) & 0xFF;

        ff_rtp_send_data(s1, p, s->buf_ptr - p, 1);

        s->num_frames = 0;
    }
    if (s->num_frames == 0) {
        s->buf_ptr = s->buf + max_au_headers_size;
        s->timestamp = s->cur_timestamp;
    }

    if (size <= max_packet_size) {
        p = s->buf + s->num_frames++ * 2 + 2;
        *p++ = size >> 5;
        *p = (size & 0x1F) << 3;
        memcpy(s->buf_ptr, buff, size);
        s->buf_ptr += size;
    } else {
        int au_size = size;

        max_packet_size = s->max_payload_size - 4;
        p = s->buf;
        p[0] = 0;
        p[1] = 16;
        while (size > 0) {
            len = FFMIN(size, max_packet_size);
            p[2] = au_size >> 5;
            p[3] = (au_size & 0x1F) << 3;
            memcpy(p + 4, buff, len);
            ff_rtp_send_data(s1, p, len + 4, len == size);
            size -= len;
            buff += len;
        }
    }
}
