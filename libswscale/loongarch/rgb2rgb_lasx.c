/*
 * Copyright (c) 2022 Loongson Technology Corporation Limited
 * Contributed by Hao Chen(chenhao@loongson.cn)
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

#include "swscale_loongarch.h"
#include "libavutil/loongarch/loongson_intrinsics.h"

void ff_interleave_bytes_lasx(const uint8_t *src1, const uint8_t *src2,
                              uint8_t *dest, int width, int height,
                              int src1Stride, int src2Stride, int dstStride)
{
    int h;
    int len = width & (0xFFFFFFF0);

    for (h = 0; h < height; h++) {
        int w, index = 0;
        __m256i src_1, src_2, dst;

        for (w = 0; w < len; w += 16) {
            DUP2_ARG2(__lasx_xvld, src1 + w, 0, src2 + w, 0, src_1, src_2);
            src_1 = __lasx_xvpermi_d(src_1, 0xD8);
            src_2 = __lasx_xvpermi_d(src_2, 0xD8);
            dst   = __lasx_xvilvl_b(src_2, src_1);
            __lasx_xvst(dst, dest + index, 0);
            index  += 32;
        }
        for (; w < width; w++) {
            dest[(w << 1) + 0] = src1[w];
            dest[(w << 1) + 1] = src2[w];
        }
        dest += dstStride;
        src1 += src1Stride;
        src2 += src2Stride;
    }
}
