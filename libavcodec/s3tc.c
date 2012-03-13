/*
 * S3 Texture Compression (S3TC) decoding functions
 * Copyright (c) 2007 by Ivo van Poorten
 *
 * see also: http://wiki.multimedia.cx/index.php?title=S3TC
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

#include "libavcodec/bytestream.h"
#include "avcodec.h"
#include "s3tc.h"

static inline void dxt1_decode_pixels(GetByteContext *gb, uint32_t *d,
                                      unsigned int qstride, unsigned int flag,
                                      uint64_t alpha) {
    unsigned int x, y, c0, c1, a = (!flag * 255u) << 24;
    unsigned int rb0, rb1, rb2, rb3, g0, g1, g2, g3;
    uint32_t colors[4], pixels;

    c0 = bytestream2_get_le16(gb);
    c1 = bytestream2_get_le16(gb);

    rb0  = (c0<<3 | c0<<8) & 0xf800f8;
    rb1  = (c1<<3 | c1<<8) & 0xf800f8;
    rb0 +=        (rb0>>5) & 0x070007;
    rb1 +=        (rb1>>5) & 0x070007;
    g0   =        (c0 <<5) & 0x00fc00;
    g1   =        (c1 <<5) & 0x00fc00;
    g0  +=        (g0 >>6) & 0x000300;
    g1  +=        (g1 >>6) & 0x000300;

    colors[0] = rb0 + g0 + a;
    colors[1] = rb1 + g1 + a;

    if (c0 > c1 || flag) {
        rb2 = (((2*rb0+rb1) * 21) >> 6) & 0xff00ff;
        rb3 = (((2*rb1+rb0) * 21) >> 6) & 0xff00ff;
        g2  = (((2*g0 +g1 ) * 21) >> 6) & 0x00ff00;
        g3  = (((2*g1 +g0 ) * 21) >> 6) & 0x00ff00;
        colors[3] = rb3 + g3 + a;
    } else {
        rb2 = ((rb0+rb1) >> 1) & 0xff00ff;
        g2  = ((g0 +g1 ) >> 1) & 0x00ff00;
        colors[3] = 0;
    }

    colors[2] = rb2 + g2 + a;

    pixels = bytestream2_get_le32(gb);
    for (y=0; y<4; y++) {
        for (x=0; x<4; x++) {
            a        = (alpha & 0x0f) << 28;
            a       += a >> 4;
            d[x]     = a + colors[pixels&3];
            pixels >>= 2;
            alpha  >>= 4;
        }
        d += qstride;
    }
}

void ff_decode_dxt1(GetByteContext *gb, uint8_t *dst,
                    const unsigned int w, const unsigned int h,
                    const unsigned int stride) {
    unsigned int bx, by, qstride = stride/4;
    uint32_t *d = (uint32_t *) dst;

    for (by=0; by < h/4; by++, d += stride-w)
        for (bx = 0; bx < w / 4; bx++, d += 4)
            dxt1_decode_pixels(gb, d, qstride, 0, 0LL);
}

void ff_decode_dxt3(GetByteContext *gb, uint8_t *dst,
                    const unsigned int w, const unsigned int h,
                    const unsigned int stride) {
    unsigned int bx, by, qstride = stride/4;
    uint32_t *d = (uint32_t *) dst;

    for (by=0; by < h/4; by++, d += stride-w)
        for (bx = 0; bx < w / 4; bx++, d += 4)
            dxt1_decode_pixels(gb, d, qstride, 1, bytestream2_get_le64(gb));
}
