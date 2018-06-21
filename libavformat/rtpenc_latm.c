/*
 * RTP Packetization of MPEG-4 Audio (RFC 3016)
 * Copyright (c) 2011 Juan Carlos Rodriguez <ing.juancarlosrodriguez@hotmail.com>
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

void ff_rtp_send_latm(AVFormatContext *s1, const uint8_t *buff, int size)
{
    /* MP4A-LATM
     * The RTP payload format specification is described in RFC 3016
     * The encoding specifications are provided in ISO/IEC 14496-3 */

    RTPMuxContext *s = s1->priv_data;
    int header_size;
    int offset = 0;
    int len    = 0;

    /* skip ADTS header, if present */
    if ((s1->streams[0]->codecpar->extradata_size) == 0) {
        size -= 7;
        buff += 7;
    }

    /* PayloadLengthInfo() */
    header_size = size/0xFF + 1;
    memset(s->buf, 0xFF, header_size - 1);
    s->buf[header_size - 1] = size % 0xFF;

    s->timestamp = s->cur_timestamp;

    /* PayloadMux() */
    while (size > 0) {
        len   = FFMIN(size, s->max_payload_size - (!offset ? header_size : 0));
        size -= len;
        if (!offset) {
            memcpy(s->buf + header_size, buff, len);
            ff_rtp_send_data(s1, s->buf, header_size + len, !size);
        } else {
            ff_rtp_send_data(s1, buff + offset, len, !size);
        }
        offset += len;
    }
}
