/*
 * RTP packetization for MPEG video
 * Copyright (c) 2002 Fabrice Bellard
 * Copyright (c) 2007 Luca Abeni
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

#include "libavcodec/mpegvideo.h"
#include "avformat.h"
#include "rtpenc.h"

/* NOTE: a single frame must be passed with sequence header if
   needed. XXX: use slices. */
void ff_rtp_send_mpegvideo(AVFormatContext *s1, const uint8_t *buf1, int size)
{
    RTPMuxContext *s = s1->priv_data;
    int len, h, max_packet_size;
    uint8_t *q;
    const uint8_t *end = buf1 + size;
    int begin_of_slice, end_of_slice, frame_type, temporal_reference;

    max_packet_size = s->max_payload_size;
    begin_of_slice = 1;
    end_of_slice = 0;
    frame_type = 0;
    temporal_reference = 0;

    while (size > 0) {
        int begin_of_sequence;

        begin_of_sequence = 0;
        len = max_packet_size - 4;

        if (len >= size) {
            len = size;
            end_of_slice = 1;
        } else {
            const uint8_t *r, *r1;
            int start_code;

            r1 = buf1;
            while (1) {
                start_code = -1;
                r = avpriv_mpv_find_start_code(r1, end, &start_code);
                if((start_code & 0xFFFFFF00) == 0x100) {
                    /* New start code found */
                    if (start_code == 0x100) {
                        frame_type = (r[1] & 0x38) >> 3;
                        temporal_reference = (int)r[0] << 2 | r[1] >> 6;
                    }
                    if (start_code == 0x1B8) {
                        begin_of_sequence = 1;
                    }

                    if (r - buf1 - 4 <= len) {
                        /* The current slice fits in the packet */
                        if (begin_of_slice == 0) {
                            /* no slice at the beginning of the packet... */
                            end_of_slice = 1;
                            len = r - buf1 - 4;
                            break;
                        }
                        r1 = r;
                    } else {
                        if ((r1 - buf1 > 4) && (r - r1 < max_packet_size)) {
                            len = r1 - buf1 - 4;
                            end_of_slice = 1;
                        }
                        break;
                    }
                } else {
                    break;
                }
            }
        }

        h = 0;
        h |= temporal_reference << 16;
        h |= begin_of_sequence << 13;
        h |= begin_of_slice << 12;
        h |= end_of_slice << 11;
        h |= frame_type << 8;

        q = s->buf;
        *q++ = h >> 24;
        *q++ = h >> 16;
        *q++ = h >> 8;
        *q++ = h;

        memcpy(q, buf1, len);
        q += len;

        /* 90kHz time stamp */
        s->timestamp = s->cur_timestamp;
        ff_rtp_send_data(s1, s->buf, q - s->buf, (len == size));

        buf1 += len;
        size -= len;
        begin_of_slice = end_of_slice;
        end_of_slice = 0;
    }
}
