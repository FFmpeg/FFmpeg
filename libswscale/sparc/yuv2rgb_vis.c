/*
 * VIS optimized software YUV to RGB converter
 * Copyright (c) 2007 Denes Balatoni <dbalatoni@programozo.hu>
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

#include <inttypes.h>
#include <stdlib.h>

#include "libavutil/attributes.h"
#include "libswscale/swscale.h"
#include "libswscale/swscale_internal.h"

#define YUV2RGB_INIT                               \
    "wr %%g0, 0x10, %%gsr \n\t"                    \
    "ldd [%5],      %%f32 \n\t"                    \
    "ldd [%5 +  8], %%f34 \n\t"                    \
    "ldd [%5 + 16], %%f36 \n\t"                    \
    "ldd [%5 + 24], %%f38 \n\t"                    \
    "ldd [%5 + 32], %%f40 \n\t"                    \
    "ldd [%5 + 40], %%f42 \n\t"                    \
    "ldd [%5 + 48], %%f44 \n\t"                    \
    "ldd [%5 + 56], %%f46 \n\t"                    \
    "ldd [%5 + 64], %%f48 \n\t"                    \
    "ldd [%5 + 72], %%f50 \n\t"

#define YUV2RGB_KERNEL                             \
    /* ^^^^ f0=Y f3=u f5=v */                      \
    "fmul8x16 %%f3,  %%f48,  %%f6 \n\t"            \
    "fmul8x16 %%f19, %%f48, %%f22 \n\t"            \
    "fmul8x16 %%f5,  %%f44,  %%f8 \n\t"            \
    "fmul8x16 %%f21, %%f44, %%f24 \n\t"            \
    "fmul8x16 %%f0,  %%f42,  %%f0 \n\t"            \
    "fmul8x16 %%f16, %%f42, %%f16 \n\t"            \
    "fmul8x16 %%f3,  %%f50,  %%f2 \n\t"            \
    "fmul8x16 %%f19, %%f50, %%f18 \n\t"            \
    "fmul8x16 %%f5,  %%f46,  %%f4 \n\t"            \
    "fmul8x16 %%f21, %%f46, %%f20 \n\t"            \
                                                   \
    "fpsub16 %%f6,  %%f34,  %%f6 \n\t" /* 1 */     \
    "fpsub16 %%f22, %%f34, %%f22 \n\t" /* 1 */     \
    "fpsub16 %%f8,  %%f38,  %%f8 \n\t" /* 3 */     \
    "fpsub16 %%f24, %%f38, %%f24 \n\t" /* 3 */     \
    "fpsub16 %%f0,  %%f32,  %%f0 \n\t" /* 0 */     \
    "fpsub16 %%f16, %%f32, %%f16 \n\t" /* 0 */     \
    "fpsub16 %%f2,  %%f36,  %%f2 \n\t" /* 2 */     \
    "fpsub16 %%f18, %%f36, %%f18 \n\t" /* 2 */     \
    "fpsub16 %%f4,  %%f40,  %%f4 \n\t" /* 4 */     \
    "fpsub16 %%f20, %%f40, %%f20 \n\t" /* 4 */     \
                                                   \
    "fpadd16 %%f0,  %%f8,  %%f8  \n\t" /* Gt */    \
    "fpadd16 %%f16, %%f24, %%f24 \n\t" /* Gt */    \
    "fpadd16 %%f0,  %%f4,  %%f4  \n\t" /* R */     \
    "fpadd16 %%f16, %%f20, %%f20 \n\t" /* R */     \
    "fpadd16 %%f0,  %%f6,  %%f6  \n\t" /* B */     \
    "fpadd16 %%f16, %%f22, %%f22 \n\t" /* B */     \
    "fpadd16 %%f8,  %%f2,  %%f2  \n\t" /* G */     \
    "fpadd16 %%f24, %%f18, %%f18 \n\t" /* G */     \
                                                   \
    "fpack16 %%f4,  %%f4  \n\t"                    \
    "fpack16 %%f20, %%f20 \n\t"                    \
    "fpack16 %%f6,  %%f6  \n\t"                    \
    "fpack16 %%f22, %%f22 \n\t"                    \
    "fpack16 %%f2,  %%f2  \n\t"                    \
    "fpack16 %%f18, %%f18 \n\t"

// FIXME: must be changed to set alpha to 255 instead of 0
static int vis_420P_ARGB32(SwsContext *c, uint8_t *src[], int srcStride[],
                           int srcSliceY, int srcSliceH,
                           uint8_t *dst[], int dstStride[])
{
    int y, out1, out2, out3, out4, out5, out6;

    for (y = 0; y < srcSliceH; ++y)
        __asm__ volatile (
            YUV2RGB_INIT
            "wr %%g0, 0xd2, %%asi        \n\t"  /* ASI_FL16_P */
            "1:                          \n\t"
            "ldda [%1]     %%asi, %%f2   \n\t"
            "ldda [%1 + 2] %%asi, %%f18  \n\t"
            "ldda [%2]     %%asi, %%f4   \n\t"
            "ldda [%2 + 2] %%asi, %%f20  \n\t"
            "ld [%0], %%f0               \n\t"
            "ld [%0+4], %%f16            \n\t"
            "fpmerge %%f3,  %%f3,  %%f2  \n\t"
            "fpmerge %%f19, %%f19, %%f18 \n\t"
            "fpmerge %%f5,  %%f5,  %%f4  \n\t"
            "fpmerge %%f21, %%f21, %%f20 \n\t"
            YUV2RGB_KERNEL
            "fzero %%f0                  \n\t"
            "fpmerge %%f4,  %%f6,  %%f8  \n\t"  // r, b, t1
            "fpmerge %%f20, %%f22, %%f24 \n\t"  // r, b, t1
            "fpmerge %%f0,  %%f2,  %%f10 \n\t"  // 0, g, t2
            "fpmerge %%f0,  %%f18, %%f26 \n\t"  // 0, g, t2
            "fpmerge %%f10, %%f8,  %%f4  \n\t"  // t2, t1, msb
            "fpmerge %%f26, %%f24, %%f20 \n\t"  // t2, t1, msb
            "fpmerge %%f11, %%f9,  %%f6  \n\t"  // t2, t1, lsb
            "fpmerge %%f27, %%f25, %%f22 \n\t"  // t2, t1, lsb
            "std %%f4,  [%3]             \n\t"
            "std %%f20, [%3 + 16]        \n\t"
            "std %%f6,  [%3 +  8]        \n\t"
            "std %%f22, [%3 + 24]        \n\t"

            "add %0, 8, %0   \n\t"
            "add %1, 4, %1   \n\t"
            "add %2, 4, %2   \n\t"
            "subcc %4, 8, %4 \n\t"
            "bne 1b          \n\t"
            "add %3, 32, %3  \n\t"              // delay slot
            : "=r" (out1), "=r" (out2), "=r" (out3), "=r" (out4), "=r" (out5), "=r" (out6)
            : "0" (src[0] + (y + srcSliceY) * srcStride[0]), "1" (src[1] + ((y + srcSliceY) >> 1) * srcStride[1]),
            "2" (src[2] + ((y + srcSliceY) >> 1) * srcStride[2]), "3" (dst[0] + (y + srcSliceY) * dstStride[0]),
            "4" (c->dstW),
            "5" (c->sparc_coeffs)
            );

    return srcSliceH;
}

// FIXME: must be changed to set alpha to 255 instead of 0
static int vis_422P_ARGB32(SwsContext *c, uint8_t *src[], int srcStride[],
                           int srcSliceY, int srcSliceH,
                           uint8_t *dst[], int dstStride[])
{
    int y, out1, out2, out3, out4, out5, out6;

    for (y = 0; y < srcSliceH; ++y)
        __asm__ volatile (
            YUV2RGB_INIT
            "wr %%g0, 0xd2, %%asi        \n\t" /* ASI_FL16_P */
            "1:                          \n\t"
            "ldda [%1]     %%asi, %%f2   \n\t"
            "ldda [%1 + 2] %%asi, %%f18  \n\t"
            "ldda [%2]     %%asi, %%f4   \n\t"
            "ldda [%2 + 2] %%asi, %%f20  \n\t"
            "ld [%0],     %%f0           \n\t"
            "ld [%0 + 4], %%f16          \n\t"
            "fpmerge %%f3,  %%f3,  %%f2  \n\t"
            "fpmerge %%f19, %%f19, %%f18 \n\t"
            "fpmerge %%f5,  %%f5,  %%f4  \n\t"
            "fpmerge %%f21, %%f21, %%f20 \n\t"
            YUV2RGB_KERNEL
            "fzero %%f0 \n\t"
            "fpmerge %%f4,  %%f6,  %%f8  \n\t"  // r,b,t1
            "fpmerge %%f20, %%f22, %%f24 \n\t"  // r,b,t1
            "fpmerge %%f0,  %%f2,  %%f10 \n\t"  // 0,g,t2
            "fpmerge %%f0,  %%f18, %%f26 \n\t"  // 0,g,t2
            "fpmerge %%f10, %%f8,  %%f4  \n\t"  // t2,t1,msb
            "fpmerge %%f26, %%f24, %%f20 \n\t"  // t2,t1,msb
            "fpmerge %%f11, %%f9,  %%f6  \n\t"  // t2,t1,lsb
            "fpmerge %%f27, %%f25, %%f22 \n\t"  // t2,t1,lsb
            "std %%f4,  [%3]             \n\t"
            "std %%f20, [%3 + 16]        \n\t"
            "std %%f6,  [%3 + 8]         \n\t"
            "std %%f22, [%3 + 24]        \n\t"

            "add %0, 8, %0   \n\t"
            "add %1, 4, %1   \n\t"
            "add %2, 4, %2   \n\t"
            "subcc %4, 8, %4 \n\t"
            "bne 1b          \n\t"
            "add %3, 32, %3  \n\t" //delay slot
            : "=r" (out1), "=r" (out2), "=r" (out3), "=r" (out4), "=r" (out5), "=r" (out6)
            : "0" (src[0] + (y + srcSliceY) * srcStride[0]), "1" (src[1] + (y + srcSliceY) * srcStride[1]),
            "2" (src[2] + (y + srcSliceY) * srcStride[2]), "3" (dst[0] + (y + srcSliceY) * dstStride[0]),
            "4" (c->dstW),
            "5" (c->sparc_coeffs)
            );

    return srcSliceH;
}

av_cold SwsFunc ff_yuv2rgb_init_vis(SwsContext *c)
{
    c->sparc_coeffs[5] = c->yCoeff;
    c->sparc_coeffs[6] = c->vgCoeff;
    c->sparc_coeffs[7] = c->vrCoeff;
    c->sparc_coeffs[8] = c->ubCoeff;
    c->sparc_coeffs[9] = c->ugCoeff;

    c->sparc_coeffs[0] = (((int16_t)c->yOffset * (int16_t)c->yCoeff  >> 11) & 0xffff) * 0x0001000100010001ULL;
    c->sparc_coeffs[1] = (((int16_t)c->uOffset * (int16_t)c->ubCoeff >> 11) & 0xffff) * 0x0001000100010001ULL;
    c->sparc_coeffs[2] = (((int16_t)c->uOffset * (int16_t)c->ugCoeff >> 11) & 0xffff) * 0x0001000100010001ULL;
    c->sparc_coeffs[3] = (((int16_t)c->vOffset * (int16_t)c->vgCoeff >> 11) & 0xffff) * 0x0001000100010001ULL;
    c->sparc_coeffs[4] = (((int16_t)c->vOffset * (int16_t)c->vrCoeff >> 11) & 0xffff) * 0x0001000100010001ULL;

    if (c->dstFormat == AV_PIX_FMT_RGB32 && c->srcFormat == AV_PIX_FMT_YUV422P && (c->dstW & 7) == 0) {
        av_log(c, AV_LOG_INFO,
               "SPARC VIS accelerated YUV422P -> RGB32 (WARNING: alpha value is wrong)\n");
        return vis_422P_ARGB32;
    } else if (c->dstFormat == AV_PIX_FMT_RGB32 && c->srcFormat == AV_PIX_FMT_YUV420P && (c->dstW & 7) == 0) {
        av_log(c, AV_LOG_INFO,
               "SPARC VIS accelerated YUV420P -> RGB32 (WARNING: alpha value is wrong)\n");
        return vis_420P_ARGB32;
    }
    return NULL;
}
