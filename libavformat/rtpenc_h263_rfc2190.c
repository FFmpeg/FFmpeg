/*
 * RTP packetization for H.263 video
 * Copyright (c) 2012 Martin Storsjo
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
#include "libavcodec/put_bits.h"
#include "libavcodec/get_bits.h"

struct H263Info {
    int src;
    int i;
    int u;
    int s;
    int a;
    int pb;
    int tr;
};

static void send_mode_a(AVFormatContext *s1, const struct H263Info *info,
                        const uint8_t *buf, int len, int m)
{
    RTPMuxContext *s = s1->priv_data;
    PutBitContext pb;

    init_put_bits(&pb, s->buf, 32);
    put_bits(&pb, 1, 0); /* F - 0, mode A */
    put_bits(&pb, 1, 0); /* P - 0, normal I/P */
    put_bits(&pb, 3, 0); /* SBIT - 0 bits */
    put_bits(&pb, 3, 0); /* EBIT - 0 bits */
    put_bits(&pb, 3, info->src); /* SRC - source format */
    put_bits(&pb, 1, info->i); /* I - inter/intra */
    put_bits(&pb, 1, info->u); /* U - unrestricted motion vector */
    put_bits(&pb, 1, info->s); /* S - syntax-baesd arithmetic coding */
    put_bits(&pb, 1, info->a); /* A - advanced prediction */
    put_bits(&pb, 4, 0); /* R - reserved */
    put_bits(&pb, 2, 0); /* DBQ - 0 */
    put_bits(&pb, 3, 0); /* TRB - 0 */
    put_bits(&pb, 8, info->tr); /* TR */
    flush_put_bits(&pb);
    memcpy(s->buf + 4, buf, len);

    ff_rtp_send_data(s1, s->buf, len + 4, m);
}

void ff_rtp_send_h263_rfc2190(AVFormatContext *s1, const uint8_t *buf, int size)
{
    RTPMuxContext *s = s1->priv_data;
    int len;
    GetBitContext gb;
    struct H263Info info = { 0 };

    s->timestamp = s->cur_timestamp;

    init_get_bits(&gb, buf, size*8);
    if (get_bits(&gb, 22) == 0x20) { /* Picture Start Code */
        info.tr  = get_bits(&gb, 8);
        skip_bits(&gb, 2); /* PTYPE start, H261 disambiguation */
        skip_bits(&gb, 3); /* Split screen, document camera, freeze picture release */
        info.src = get_bits(&gb, 3);
        info.i   = get_bits(&gb, 1);
        info.u   = get_bits(&gb, 1);
        info.s   = get_bits(&gb, 1);
        info.a   = get_bits(&gb, 1);
        info.pb  = get_bits(&gb, 1);
    }

    while (size > 0) {
        len = FFMIN(s->max_payload_size - 4, size);

        /* Look for a better place to split the frame into packets. */
        if (len < size) {
            const uint8_t *end = ff_h263_find_resync_marker_reverse(buf,
                                                                    buf + len);
            len = end - buf;
            if (len == s->max_payload_size - 4)
                av_log(s1, AV_LOG_WARNING,
                       "No GOB boundary found within MTU size, splitting at "
                       "a random boundary\n");
        }

        send_mode_a(s1, &info, buf, len, len == size);

        buf  += len;
        size -= len;
    }
}
