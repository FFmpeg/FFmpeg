/*
 * Copyright (c) 2016 Ronald S. Bultje <rsbultje@gmail.com>
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

#include "libavutil/avassert.h"

#undef opixel
#define opixel pixel

#undef ipixel
#if IN_BIT_DEPTH == 8
#define ipixel uint8_t
#else
#define ipixel uint16_t
#endif

#undef fn
#undef fn2
#undef fn3
#define fn3(a,b,c,d) a##_##d##p##b##to##c##_c
#define fn2(a,b,c,d) fn3(a,b,c,d)
#define fn(a) fn2(a, IN_BIT_DEPTH, OUT_BIT_DEPTH, ss)

static void fn(yuv2yuv)(uint8_t *_dst[3], const ptrdiff_t dst_stride[3],
                        uint8_t *_src[3], const ptrdiff_t src_stride[3],
                        int w, int h, const int16_t c[3][3][8],
                        const int16_t yuv_offset[2][8])
{
    opixel **dst = (opixel **) _dst;
    ipixel **src = (ipixel **) _src;
    const ipixel *src0 = src[0], *src1 = src[1], *src2 = src[2];
    opixel *dst0 = dst[0], *dst1 = dst[1], *dst2 = dst[2];
    int y, x;
    const int sh = 14 + IN_BIT_DEPTH - OUT_BIT_DEPTH;
    const int rnd = 1 << (sh - 1);
    int y_off_in = yuv_offset[0][0];
    int y_off_out = yuv_offset[1][0] << sh;
    const int uv_off_in = 128 << (IN_BIT_DEPTH - 8);
    const int uv_off_out = rnd + (128 << (OUT_BIT_DEPTH - 8 + sh));
    int cyy = c[0][0][0], cyu = c[0][1][0], cyv = c[0][2][0];
    int cuu = c[1][1][0], cuv = c[1][2][0], cvu = c[2][1][0], cvv = c[2][2][0];

    av_assert2(c[1][0][0] == 0);
    av_assert2(c[2][0][0] == 0);
    w = AV_CEIL_RSHIFT(w, SS_W);
    h = AV_CEIL_RSHIFT(h, SS_H);
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            int y00 = src0[x << SS_W] - y_off_in;
#if SS_W == 1
            int y01 = src0[2 * x + 1] - y_off_in;
#if SS_H == 1
            int y10 = src0[src_stride[0] / sizeof(ipixel) + 2 * x] - y_off_in;
            int y11 = src0[src_stride[0] / sizeof(ipixel) + 2 * x + 1] - y_off_in;
#endif
#endif
            int u = src1[x] - uv_off_in, v = src2[x] - uv_off_in;
            int uv_val = cyu * u + cyv * v + rnd + y_off_out;

            dst0[x << SS_W] = av_clip_pixel((cyy * y00 + uv_val) >> sh);
#if SS_W == 1
            dst0[x * 2 + 1] = av_clip_pixel((cyy * y01 + uv_val) >> sh);
#if SS_H == 1
            dst0[x * 2 + 0 + dst_stride[0] / sizeof(opixel)] =
                              av_clip_pixel((cyy * y10 + uv_val) >> sh);
            dst0[x * 2 + 1 + dst_stride[0] / sizeof(opixel)] =
                              av_clip_pixel((cyy * y11 + uv_val) >> sh);
#endif
#endif

            dst1[x] = av_clip_pixel((u * cuu + v * cuv + uv_off_out) >> sh);
            dst2[x] = av_clip_pixel((u * cvu + v * cvv + uv_off_out) >> sh);
        }

        dst0 += (dst_stride[0] * (1 << SS_H)) / sizeof(opixel);
        dst1 += dst_stride[1] / sizeof(opixel);
        dst2 += dst_stride[2] / sizeof(opixel);
        src0 += (src_stride[0] * (1 << SS_H)) / sizeof(ipixel);
        src1 += src_stride[1] / sizeof(ipixel);
        src2 += src_stride[2] / sizeof(ipixel);
    }
}
