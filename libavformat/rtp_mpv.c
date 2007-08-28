/*
 * RTP packetization for MPEG video
 * Copyright (c) 2002 Fabrice Bellard.
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
#include "rtp_internal.h"

/* NOTE: a single frame must be passed with sequence header if
   needed. XXX: use slices. */
void ff_rtp_send_mpegvideo(AVFormatContext *s1, const uint8_t *buf1, int size)
{
    RTPDemuxContext *s = s1->priv_data;
    AVStream *st = s1->streams[0];
    int len, h, max_packet_size;
    int b=1, e=0;
    uint8_t *q;

    max_packet_size = s->max_payload_size;

    while (size > 0) {
        len = max_packet_size - 4;

        if (len >= size) {
            len = size;
            e = 1;
        }

        h = 0;
        h |= b << 12;
        h |= e << 11;

//        if (st->codec->sub_id == 2)
//            h |= 1 << 26; /* mpeg 2 indicator */

        q = s->buf;
        *q++ = h >> 24;
        *q++ = h >> 16;
        *q++ = h >> 8;
        *q++ = h;

/*        if (st->codec->sub_id == 2) {
            h = 0;
            *q++ = h >> 24;
            *q++ = h >> 16;
            *q++ = h >> 8;
            *q++ = h;
        } */

        memcpy(q, buf1, len);
        q += len;

        /* 90 KHz time stamp */
        s->timestamp = s->base_timestamp +
            av_rescale((int64_t)s->cur_timestamp * st->codec->time_base.num, 90000, st->codec->time_base.den); //FIXME pass timestamps
        ff_rtp_send_data(s1, s->buf, q - s->buf, (len == size));

        buf1 += len;
        size -= len;
    }
    s->cur_timestamp++;
}


