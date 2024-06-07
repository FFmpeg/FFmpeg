/*
 * RTP Packetization of RAW video (RFC4175)
 * Copyright (c) 2021 Limin Wang
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

void ff_rtp_send_raw_rfc4175(AVFormatContext *s1, const uint8_t *buf, int size, int interlaced, int field)
{
    RTPMuxContext *s = s1->priv_data;
    int width = s1->streams[0]->codecpar->width;
    int height = s1->streams[0]->codecpar->height;
    int xinc, yinc, pgroup;
    int i = 0;
    int offset = 0;

    s->timestamp = s->cur_timestamp;
    switch (s1->streams[0]->codecpar->format) {
        case AV_PIX_FMT_UYVY422:
            xinc = 2;
            yinc = 1 << interlaced;
            pgroup = 4;
            break;
        case AV_PIX_FMT_YUV422P10:
            xinc = 2;
            yinc = 1 << interlaced;
            pgroup = 5;
            break;
        case AV_PIX_FMT_YUV420P:
            xinc = 4;
            yinc = 1 << interlaced;
            pgroup = 6;
            break;
        case AV_PIX_FMT_RGB24:
            xinc = 1;
            yinc = 1 << interlaced;
            pgroup = 3;
            break;
        case AV_PIX_FMT_BGR24:
            xinc = 1;
            yinc = 1 << interlaced;
            pgroup = 3;
            break;
        default:
            return;
    }

    while (i < height) {
        int left = s->max_payload_size;
        uint8_t *dest = s->buf;
        uint8_t *headers;
        const int head_size = 6;
        int next_line;
        int length, cont, pixels;

        /* Extended Sequence Number */
        *dest++ = 0;
        *dest++ = 0;
        left   -= 2;

        headers = dest;
        do {
            int l_line;

            pixels = width - offset;
            length = (pixels * pgroup) / xinc;

            left -= head_size;
            if (left >= length) {
                next_line = 1;
            } else {
                pixels = (left / pgroup) * xinc;
                length = (pixels * pgroup) / xinc;
                next_line = 0;
            }
            left -= length;

            /* Length */
            *dest++ = (length >> 8) & 0xff;
            *dest++ = length & 0xff;

            /* Line No */
            l_line = i >> interlaced;
            *dest++ = ((l_line >> 8) & 0x7f) | ((field << 7) & 0x80);
            *dest++ = l_line & 0xff;
            if (next_line) i += yinc;

            cont = (left > (head_size + pgroup) && i < height) ? 0x80 : 0x00;
            /* Offset and Continuation marker */
            *dest++ = ((offset >> 8) & 0x7f) | cont;
            *dest++ = offset & 0xff;

            if (next_line)
                offset  = 0;
            else
                offset += pixels;
        } while (cont);

        do {
            int l_field;
            int l_line;
            int l_off;
            int64_t copy_offset;

            length    = (headers[0] << 8) | headers[1];
            l_field   = (headers[2] & 0x80) >> 7;
            l_line    = ((headers[2] & 0x7f) << 8) | headers[3];
            l_off     = ((headers[4] & 0x7f) << 8) | headers[5];
            cont      = headers[4] & 0x80;
            headers  += head_size;

            if (interlaced)
                l_line = 2 * l_line + l_field;
            copy_offset = (l_line * (int64_t)width + l_off) * pgroup / xinc;
            if (copy_offset + length > size)
                break;
            memcpy (dest, buf + copy_offset, length);
            dest += length;
        } while (cont);

        ff_rtp_send_data (s1, s->buf, s->max_payload_size - left, i >= height);
    }
}
