/*
 * AltiVec-enhanced yuv-to-yuv convertion routines.
 *
 * Copyright (C) 2004 Romain Dolbeau <romain@dolbeau.org>
 * based on the equivalent C code in swscale.c
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
#include "config.h"
#include "libswscale/swscale.h"
#include "libswscale/swscale_internal.h"
#include "libavutil/cpu.h"

static int yv12toyuy2_unscaled_altivec(SwsContext *c, const uint8_t* src[],
                                       int srcStride[], int srcSliceY,
                                       int srcSliceH, uint8_t* dstParam[],
                                       int dstStride_a[])
{
    uint8_t *dst=dstParam[0] + dstStride_a[0]*srcSliceY;
    // yv12toyuy2(src[0], src[1], src[2], dst, c->srcW, srcSliceH, srcStride[0], srcStride[1], dstStride[0]);
    const uint8_t *ysrc = src[0];
    const uint8_t *usrc = src[1];
    const uint8_t *vsrc = src[2];
    const int width = c->srcW;
    const int height = srcSliceH;
    const int lumStride = srcStride[0];
    const int chromStride = srcStride[1];
    const int dstStride = dstStride_a[0];
    const vector unsigned char yperm = vec_lvsl(0, ysrc);
    const int vertLumPerChroma = 2;
    register unsigned int y;

    /* This code assumes:

    1) dst is 16 bytes-aligned
    2) dstStride is a multiple of 16
    3) width is a multiple of 16
    4) lum & chrom stride are multiples of 8
    */

    for (y=0; y<height; y++) {
        int i;
        for (i = 0; i < width - 31; i+= 32) {
            const unsigned int j = i >> 1;
            vector unsigned char v_yA = vec_ld(i, ysrc);
            vector unsigned char v_yB = vec_ld(i + 16, ysrc);
            vector unsigned char v_yC = vec_ld(i + 32, ysrc);
            vector unsigned char v_y1 = vec_perm(v_yA, v_yB, yperm);
            vector unsigned char v_y2 = vec_perm(v_yB, v_yC, yperm);
            vector unsigned char v_uA = vec_ld(j, usrc);
            vector unsigned char v_uB = vec_ld(j + 16, usrc);
            vector unsigned char v_u = vec_perm(v_uA, v_uB, vec_lvsl(j, usrc));
            vector unsigned char v_vA = vec_ld(j, vsrc);
            vector unsigned char v_vB = vec_ld(j + 16, vsrc);
            vector unsigned char v_v = vec_perm(v_vA, v_vB, vec_lvsl(j, vsrc));
            vector unsigned char v_uv_a = vec_mergeh(v_u, v_v);
            vector unsigned char v_uv_b = vec_mergel(v_u, v_v);
            vector unsigned char v_yuy2_0 = vec_mergeh(v_y1, v_uv_a);
            vector unsigned char v_yuy2_1 = vec_mergel(v_y1, v_uv_a);
            vector unsigned char v_yuy2_2 = vec_mergeh(v_y2, v_uv_b);
            vector unsigned char v_yuy2_3 = vec_mergel(v_y2, v_uv_b);
            vec_st(v_yuy2_0, (i << 1), dst);
            vec_st(v_yuy2_1, (i << 1) + 16, dst);
            vec_st(v_yuy2_2, (i << 1) + 32, dst);
            vec_st(v_yuy2_3, (i << 1) + 48, dst);
        }
        if (i < width) {
            const unsigned int j = i >> 1;
            vector unsigned char v_y1 = vec_ld(i, ysrc);
            vector unsigned char v_u = vec_ld(j, usrc);
            vector unsigned char v_v = vec_ld(j, vsrc);
            vector unsigned char v_uv_a = vec_mergeh(v_u, v_v);
            vector unsigned char v_yuy2_0 = vec_mergeh(v_y1, v_uv_a);
            vector unsigned char v_yuy2_1 = vec_mergel(v_y1, v_uv_a);
            vec_st(v_yuy2_0, (i << 1), dst);
            vec_st(v_yuy2_1, (i << 1) + 16, dst);
        }
        if ((y&(vertLumPerChroma-1)) == vertLumPerChroma-1) {
            usrc += chromStride;
            vsrc += chromStride;
        }
        ysrc += lumStride;
        dst += dstStride;
    }

    return srcSliceH;
}

static int yv12touyvy_unscaled_altivec(SwsContext *c, const uint8_t* src[],
                                       int srcStride[], int srcSliceY,
                                       int srcSliceH, uint8_t* dstParam[],
                                       int dstStride_a[])
{
    uint8_t *dst=dstParam[0] + dstStride_a[0]*srcSliceY;
    // yv12toyuy2(src[0], src[1], src[2], dst, c->srcW, srcSliceH, srcStride[0], srcStride[1], dstStride[0]);
    const uint8_t *ysrc = src[0];
    const uint8_t *usrc = src[1];
    const uint8_t *vsrc = src[2];
    const int width = c->srcW;
    const int height = srcSliceH;
    const int lumStride = srcStride[0];
    const int chromStride = srcStride[1];
    const int dstStride = dstStride_a[0];
    const int vertLumPerChroma = 2;
    const vector unsigned char yperm = vec_lvsl(0, ysrc);
    register unsigned int y;

    /* This code assumes:

    1) dst is 16 bytes-aligned
    2) dstStride is a multiple of 16
    3) width is a multiple of 16
    4) lum & chrom stride are multiples of 8
    */

    for (y=0; y<height; y++) {
        int i;
        for (i = 0; i < width - 31; i+= 32) {
            const unsigned int j = i >> 1;
            vector unsigned char v_yA = vec_ld(i, ysrc);
            vector unsigned char v_yB = vec_ld(i + 16, ysrc);
            vector unsigned char v_yC = vec_ld(i + 32, ysrc);
            vector unsigned char v_y1 = vec_perm(v_yA, v_yB, yperm);
            vector unsigned char v_y2 = vec_perm(v_yB, v_yC, yperm);
            vector unsigned char v_uA = vec_ld(j, usrc);
            vector unsigned char v_uB = vec_ld(j + 16, usrc);
            vector unsigned char v_u = vec_perm(v_uA, v_uB, vec_lvsl(j, usrc));
            vector unsigned char v_vA = vec_ld(j, vsrc);
            vector unsigned char v_vB = vec_ld(j + 16, vsrc);
            vector unsigned char v_v = vec_perm(v_vA, v_vB, vec_lvsl(j, vsrc));
            vector unsigned char v_uv_a = vec_mergeh(v_u, v_v);
            vector unsigned char v_uv_b = vec_mergel(v_u, v_v);
            vector unsigned char v_uyvy_0 = vec_mergeh(v_uv_a, v_y1);
            vector unsigned char v_uyvy_1 = vec_mergel(v_uv_a, v_y1);
            vector unsigned char v_uyvy_2 = vec_mergeh(v_uv_b, v_y2);
            vector unsigned char v_uyvy_3 = vec_mergel(v_uv_b, v_y2);
            vec_st(v_uyvy_0, (i << 1), dst);
            vec_st(v_uyvy_1, (i << 1) + 16, dst);
            vec_st(v_uyvy_2, (i << 1) + 32, dst);
            vec_st(v_uyvy_3, (i << 1) + 48, dst);
        }
        if (i < width) {
            const unsigned int j = i >> 1;
            vector unsigned char v_y1 = vec_ld(i, ysrc);
            vector unsigned char v_u = vec_ld(j, usrc);
            vector unsigned char v_v = vec_ld(j, vsrc);
            vector unsigned char v_uv_a = vec_mergeh(v_u, v_v);
            vector unsigned char v_uyvy_0 = vec_mergeh(v_uv_a, v_y1);
            vector unsigned char v_uyvy_1 = vec_mergel(v_uv_a, v_y1);
            vec_st(v_uyvy_0, (i << 1), dst);
            vec_st(v_uyvy_1, (i << 1) + 16, dst);
        }
        if ((y&(vertLumPerChroma-1)) == vertLumPerChroma-1) {
            usrc += chromStride;
            vsrc += chromStride;
        }
        ysrc += lumStride;
        dst += dstStride;
    }
    return srcSliceH;
}

void ff_swscale_get_unscaled_altivec(SwsContext *c)
{
    if ((av_get_cpu_flags() & AV_CPU_FLAG_ALTIVEC) && !(c->srcW & 15) &&
        !(c->flags & SWS_BITEXACT) && c->srcFormat == PIX_FMT_YUV420P) {
        enum PixelFormat dstFormat = c->dstFormat;

        // unscaled YV12 -> packed YUV, we want speed
        if (dstFormat == PIX_FMT_YUYV422)
            c->swScale= yv12toyuy2_unscaled_altivec;
        else if (dstFormat == PIX_FMT_UYVY422)
            c->swScale= yv12touyvy_unscaled_altivec;
    }
}
