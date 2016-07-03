/*
 * Copyright (C) 2016 Dan Parrot <dan.parrot@mail.com>
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

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "libavutil/avutil.h"
#include "libavutil/bswap.h"
#include "libavutil/cpu.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mathematics.h"
#include "libavutil/pixdesc.h"
#include "libavutil/avassert.h"
#include "config.h"
#include "libswscale/rgb2rgb.h"
#include "libswscale/swscale.h"
#include "libswscale/swscale_internal.h"

#if HAVE_VSX

static void abgrToA_c_vsx(uint8_t *_dst, const uint8_t *src, const uint8_t *unused1, const uint8_t *unused2,
                          int width, uint32_t *unused)
{
    int16_t *dst = (int16_t *)_dst;
    int i, width_adj, frag_len;

    uintptr_t src_addr = (uintptr_t)src;
    uintptr_t dst_addr = (uintptr_t)dst;

    // compute integral number of vector-length items and length of final fragment
    width_adj = width >> 3;
    width_adj = width_adj << 3;
    frag_len = width - width_adj;

    for ( i = 0; i < width_adj; i += 8) {
        vector int v_rd0 = vec_vsx_ld(0, (int *)src_addr);
        vector int v_rd1 = vec_vsx_ld(0, (int *)(src_addr + 16));

        v_rd0 = vec_and(v_rd0, vec_splats(0x0ff));
        v_rd1 = vec_and(v_rd1, vec_splats(0x0ff));

        v_rd0 = vec_sl(v_rd0, vec_splats((unsigned)6));
        v_rd1 = vec_sl(v_rd1, vec_splats((unsigned)6));

        vector int v_dst = vec_perm(v_rd0, v_rd1, ((vector unsigned char)
                                                   {0, 1, 4, 5, 8, 9, 12, 13, 16, 17, 20, 21, 24, 25, 28, 29}));
        vec_vsx_st((vector unsigned char)v_dst, 0, (unsigned char *)dst_addr);

        src_addr += 32;
        dst_addr += 16;
    }

    for (i=width_adj; i< width_adj + frag_len; i++) {
        dst[i]= src[4*i]<<6;
    }
}

static void rgbaToA_c_vsx(uint8_t *_dst, const uint8_t *src, const uint8_t *unused1, const uint8_t *unused2,
                          int width, uint32_t *unused)
{
    int16_t *dst = (int16_t *)_dst;
    int i, width_adj, frag_len;

    uintptr_t src_addr = (uintptr_t)src;
    uintptr_t dst_addr = (uintptr_t)dst;

    // compute integral number of vector-length items and length of final fragment
    width_adj = width >> 3;
    width_adj = width_adj << 3;
    frag_len = width - width_adj;

    for ( i = 0; i < width_adj; i += 8) {
        vector int v_rd0 = vec_vsx_ld(0, (int *)src_addr);
        vector int v_rd1 = vec_vsx_ld(0, (int *)(src_addr + 16));

        v_rd0 = vec_sld(v_rd0, v_rd0, 13);
        v_rd1 = vec_sld(v_rd1, v_rd1, 13);

        v_rd0 = vec_and(v_rd0, vec_splats(0x0ff));
        v_rd1 = vec_and(v_rd1, vec_splats(0x0ff));

        v_rd0 = vec_sl(v_rd0, vec_splats((unsigned)6));
        v_rd1 = vec_sl(v_rd1, vec_splats((unsigned)6));

        vector int v_dst = vec_perm(v_rd0, v_rd1, ((vector unsigned char)
                                                   {0, 1, 4, 5, 8, 9, 12, 13, 16, 17, 20, 21, 24, 25, 28, 29}));
        vec_vsx_st((vector unsigned char)v_dst, 0, (unsigned char *)dst_addr);

        src_addr += 32;
        dst_addr += 16;
    }

    for (i=width_adj; i< width_adj + frag_len; i++) {
        dst[i]= src[4*i+3]<<6;
    }
}

static void yuy2ToY_c_vsx(uint8_t *dst, const uint8_t *src, const uint8_t *unused1, const uint8_t *unused2,  int width,
                          uint32_t *unused)
{
    int i, width_adj, frag_len;

    uintptr_t src_addr = (uintptr_t)src;
    uintptr_t dst_addr = (uintptr_t)dst;

    // compute integral number of vector-length items and length of final fragment
    width_adj = width >> 4;
    width_adj = width_adj << 4;
    frag_len = width - width_adj;

    for ( i = 0; i < width_adj; i += 16) {
        vector unsigned char v_rd0 = vec_vsx_ld(0, (unsigned char *)src_addr);
        vector unsigned char v_rd1 = vec_vsx_ld(0, (unsigned char *)(src_addr + 16));

        vector unsigned char v_dst = vec_perm(v_rd0, v_rd1, ((vector unsigned char)
                                                             {0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30}));
        vec_vsx_st((vector unsigned char)v_dst, 0, (unsigned char *)dst_addr);

        src_addr += 32;
        dst_addr += 16;
    }

    for (i=width_adj; i< width_adj + frag_len; i++) {
        dst[i] = src[2 * i];
    }
}

static void yuy2ToUV_c_vsx(uint8_t *dstU, uint8_t *dstV, const uint8_t *unused0, const uint8_t *src1,
                           const uint8_t *src2, int width, uint32_t *unused)
{
    int i, width_adj, frag_len;

    uintptr_t src1_addr = (uintptr_t)src1;
    uintptr_t dstu_addr = (uintptr_t)dstU;
    uintptr_t dstv_addr = (uintptr_t)dstV;

    // compute integral number of vector-length items and length of final fragment
    width_adj = width >> 4;
    width_adj = width_adj << 4;
    frag_len = width - width_adj;

    for ( i = 0; i < width_adj; i += 16) {
        vector unsigned char v_src1_0 = vec_vsx_ld(0, (unsigned char *)src1_addr);
        vector unsigned char v_src1_1 = vec_vsx_ld(0, (unsigned char *)(src1_addr + 16));
        vector unsigned char v_src1_2 = vec_vsx_ld(0, (unsigned char *)(src1_addr + 32));
        vector unsigned char v_src1_3 = vec_vsx_ld(0, (unsigned char *)(src1_addr + 48));

        vector unsigned char v_dstu = vec_perm(v_src1_0, v_src1_1,
                                              ((vector unsigned char)
                                               {1, 5, 9, 13, 17, 21, 25, 29, 1, 5, 9, 13, 17, 21, 25, 29}));
        vector unsigned char v_dstv = vec_perm(v_src1_0, v_src1_1,
                                              ((vector unsigned char)
                                               {3, 7, 11, 15, 19, 23, 27, 31, 1, 5, 9, 13, 17, 21, 25, 29}));

        v_dstu = vec_perm(v_dstu, v_src1_2,((vector unsigned char)
                                            {0, 1, 2, 3, 4, 5, 6, 7, 17, 21, 25, 29, 17, 21, 25, 29}));
        v_dstv = vec_perm(v_dstv, v_src1_2,((vector unsigned char)
                                            {0, 1, 2, 3, 4, 5, 6, 7, 19, 23, 27, 31, 17, 21, 25, 29}));

        v_dstu = vec_perm(v_dstu, v_src1_3,((vector unsigned char)
                                          {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 17, 21, 25, 29}));
        v_dstv = vec_perm(v_dstv, v_src1_3,((vector unsigned char)
                                          {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 19, 23, 27, 31}));

        vec_vsx_st((vector unsigned char)v_dstu, 0, (unsigned char *)dstu_addr);
        vec_vsx_st((vector unsigned char)v_dstv, 0, (unsigned char *)dstv_addr);

        src1_addr += 64;
        dstu_addr += 16;
        dstv_addr += 16;
    }

    for (i=width_adj; i< width_adj + frag_len; i++) {
        dstU[i] = src1[4 * i + 1];
        dstV[i] = src1[4 * i + 3];
    }
    av_assert1(src1 == src2);
}

static void yvy2ToUV_c_vsx(uint8_t *dstU, uint8_t *dstV, const uint8_t *unused0, const uint8_t *src1,
                           const uint8_t *src2, int width, uint32_t *unused)
{
    int i, width_adj, frag_len;

    uintptr_t src1_addr = (uintptr_t)src1;
    uintptr_t dstu_addr = (uintptr_t)dstU;
    uintptr_t dstv_addr = (uintptr_t)dstV;

    // compute integral number of vector-length items and length of final fragment
    width_adj = width >> 4;
    width_adj = width_adj << 4;
    frag_len = width - width_adj;

    for ( i = 0; i < width_adj; i += 16) {
        vector unsigned char v_src1_0 = vec_vsx_ld(0, (unsigned char *)src1_addr);
        vector unsigned char v_src1_1 = vec_vsx_ld(0, (unsigned char *)(src1_addr + 16));
        vector unsigned char v_src1_2 = vec_vsx_ld(0, (unsigned char *)(src1_addr + 32));
        vector unsigned char v_src1_3 = vec_vsx_ld(0, (unsigned char *)(src1_addr + 48));

        vector unsigned char v_dstv = vec_perm(v_src1_0, v_src1_1,
                                              ((vector unsigned char)
                                               {1, 5, 9, 13, 17, 21, 25, 29, 1, 5, 9, 13, 17, 21, 25, 29}));
        vector unsigned char v_dstu = vec_perm(v_src1_0, v_src1_1,
                                              ((vector unsigned char)
                                               {3, 7, 11, 15, 19, 23, 27, 31, 1, 5, 9, 13, 17, 21, 25, 29}));

        v_dstv = vec_perm(v_dstv, v_src1_2,((vector unsigned char)
                                            {0, 1, 2, 3, 4, 5, 6, 7, 17, 21, 25, 29, 17, 21, 25, 29}));
        v_dstu = vec_perm(v_dstu, v_src1_2,((vector unsigned char)
                                            {0, 1, 2, 3, 4, 5, 6, 7, 19, 23, 27, 31, 17, 21, 25, 29}));

        v_dstv = vec_perm(v_dstv, v_src1_3,((vector unsigned char)
                                          {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 17, 21, 25, 29}));
        v_dstu = vec_perm(v_dstu, v_src1_3,((vector unsigned char)
                                          {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 19, 23, 27, 31}));

        vec_vsx_st((vector unsigned char)v_dstu, 0, (unsigned char *)dstu_addr);
        vec_vsx_st((vector unsigned char)v_dstv, 0, (unsigned char *)dstv_addr);

        src1_addr += 64;
        dstu_addr += 16;
        dstv_addr += 16;
    }

    for (i=width_adj; i< width_adj + frag_len; i++) {
        dstV[i] = src1[4 * i + 1];
        dstU[i] = src1[4 * i + 3];
    }
    av_assert1(src1 == src2);
}

static void uyvyToY_c_vsx(uint8_t *dst, const uint8_t *src, const uint8_t *unused1, const uint8_t *unused2,  int width,
                          uint32_t *unused)
{
    int i, width_adj, frag_len;

    uintptr_t src_addr = (uintptr_t)src;
    uintptr_t dst_addr = (uintptr_t)dst;

    // compute integral number of vector-length items and length of final fragment
    width_adj = width >> 4;
    width_adj = width_adj << 4;
    frag_len = width - width_adj;

    for ( i = 0; i < width_adj; i += 16) {
        vector unsigned char v_rd0 = vec_vsx_ld(0, (unsigned char *)src_addr);
        vector unsigned char v_rd1 = vec_vsx_ld(0, (unsigned char *)(src_addr + 16));

        vector unsigned char v_dst = vec_perm(v_rd0, v_rd1, ((vector unsigned char)
                                                             {1, 3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 31}));
        vec_vsx_st((vector unsigned char)v_dst, 0, (unsigned char *)dst_addr);

        src_addr += 32;
        dst_addr += 16;
    }

    for (i=width_adj; i< width_adj + frag_len; i++) {
        dst[i] = src[2 * i + 1];
    }
}

static void uyvyToUV_c_vsx(uint8_t *dstU, uint8_t *dstV, const uint8_t *unused0, const uint8_t *src1,
                           const uint8_t *src2, int width, uint32_t *unused)
{
    int i, width_adj, frag_len;

    uintptr_t src1_addr = (uintptr_t)src1;
    uintptr_t dstu_addr = (uintptr_t)dstU;
    uintptr_t dstv_addr = (uintptr_t)dstV;

    // compute integral number of vector-length items and length of final fragment
    width_adj = width >> 4;
    width_adj = width_adj << 4;
    frag_len = width - width_adj;

    for ( i = 0; i < width_adj; i += 16) {
        vector unsigned char v_src1_0 = vec_vsx_ld(0, (unsigned char *)src1_addr);
        vector unsigned char v_src1_1 = vec_vsx_ld(0, (unsigned char *)(src1_addr + 16));
        vector unsigned char v_src1_2 = vec_vsx_ld(0, (unsigned char *)(src1_addr + 32));
        vector unsigned char v_src1_3 = vec_vsx_ld(0, (unsigned char *)(src1_addr + 48));

        vector unsigned char v_dstu = vec_perm(v_src1_0, v_src1_1,
                                              ((vector unsigned char)
                                               {0, 4, 8, 12, 16, 20, 24, 28, 1, 5, 9, 13, 17, 21, 25, 29}));
        vector unsigned char v_dstv = vec_perm(v_src1_0, v_src1_1,
                                              ((vector unsigned char)
                                               {2, 6, 10, 14, 18, 22, 26, 30, 1, 5, 9, 13, 17, 21, 25, 29}));

        v_dstu = vec_perm(v_dstu, v_src1_2,((vector unsigned char)
                                            {0, 1, 2, 3, 4, 5, 6, 7, 16, 20, 24, 28, 17, 21, 25, 29}));
        v_dstv = vec_perm(v_dstv, v_src1_2,((vector unsigned char)
                                            {0, 1, 2, 3, 4, 5, 6, 7, 18, 22, 26, 30, 17, 21, 25, 29}));

        v_dstu = vec_perm(v_dstu, v_src1_3,((vector unsigned char)
                                          {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 16, 20, 24, 28}));
        v_dstv = vec_perm(v_dstv, v_src1_3,((vector unsigned char)
                                          {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 18, 22, 26, 30}));

        vec_vsx_st((vector unsigned char)v_dstu, 0, (unsigned char *)dstu_addr);
        vec_vsx_st((vector unsigned char)v_dstv, 0, (unsigned char *)dstv_addr);

        src1_addr += 64;
        dstu_addr += 16;
        dstv_addr += 16;
    }

    for (i=width_adj; i< width_adj + frag_len; i++) {
        dstU[i] = src1[4 * i + 0];
        dstV[i] = src1[4 * i + 2];
    }
    av_assert1(src1 == src2);
}

static av_always_inline void nvXXtoUV_c_vsx(uint8_t *dst1, uint8_t *dst2, const uint8_t *src, int width)
{
    int i, width_adj, frag_len;

    uintptr_t src_addr  = (uintptr_t)src;
    uintptr_t dst1_addr = (uintptr_t)dst1;
    uintptr_t dst2_addr = (uintptr_t)dst2;

    // compute integral number of vector-length items and length of final fragment
    width_adj = width >> 4;
    width_adj = width_adj << 4;
    frag_len = width - width_adj;

    for ( i = 0; i < width_adj; i += 16) {
        vector unsigned char v_rd0 = vec_vsx_ld(0, (unsigned char *)src_addr);
        vector unsigned char v_rd1 = vec_vsx_ld(0, (unsigned char *)(src_addr + 16));

        vector unsigned char v_dst1 = vec_perm(v_rd0, v_rd1, ((vector unsigned char)
                                                              {0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30}));
        vector unsigned char v_dst2 = vec_perm(v_rd0, v_rd1, ((vector unsigned char)
                                                              {1, 3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 31}));

        vec_vsx_st((vector unsigned char)v_dst1, 0, (unsigned char *)dst1_addr);
        vec_vsx_st((vector unsigned char)v_dst2, 0, (unsigned char *)dst2_addr);

        src_addr    += 32;
        dst1_addr   += 16;
        dst2_addr   += 16;
    }

    for (i=width_adj; i< width_adj + frag_len; i++) {
        dst1[i] = src[2 * i + 0];
        dst2[i] = src[2 * i + 1];
    }
}

static void nv12ToUV_c_vsx(uint8_t *dstU, uint8_t *dstV,
                           const uint8_t *unused0, const uint8_t *src1, const uint8_t *src2,
                           int width, uint32_t *unused)
{
    nvXXtoUV_c_vsx(dstU, dstV, src1, width);
}

static void nv21ToUV_c_vsx(uint8_t *dstU, uint8_t *dstV,
                           const uint8_t *unused0, const uint8_t *src1, const uint8_t *src2,
                           int width, uint32_t *unused)
{
    nvXXtoUV_c_vsx(dstV, dstU, src1, width);
}

#endif /* HAVE_VSX */

av_cold void ff_sws_init_input_funcs_vsx(SwsContext *c)
{
#if HAVE_VSX

    enum AVPixelFormat srcFormat = c->srcFormat;

    switch (srcFormat) {
    case AV_PIX_FMT_YUYV422:
        c->chrToYV12 = yuy2ToUV_c_vsx;
        break;
    case AV_PIX_FMT_YVYU422:
        c->chrToYV12 = yvy2ToUV_c_vsx;
        break;
    case AV_PIX_FMT_UYVY422:
        c->chrToYV12 = uyvyToUV_c_vsx;
        break;
    case AV_PIX_FMT_NV12:
        c->chrToYV12 = nv12ToUV_c_vsx;
        break;
    case AV_PIX_FMT_NV21:
        c->chrToYV12 = nv21ToUV_c_vsx;
        break;
    }

    switch (srcFormat) {
    case AV_PIX_FMT_YUYV422:
    case AV_PIX_FMT_YVYU422:
    case AV_PIX_FMT_YA8:
        c->lumToYV12 = yuy2ToY_c_vsx;
        break;
    case AV_PIX_FMT_UYVY422:
        c->lumToYV12 = uyvyToY_c_vsx;
        break;
    }

    if (c->needAlpha) {
        switch (srcFormat) {
        case AV_PIX_FMT_BGRA:
        case AV_PIX_FMT_RGBA:
            c->alpToYV12 = rgbaToA_c_vsx;
            break;
        case AV_PIX_FMT_ABGR:
        case AV_PIX_FMT_ARGB:
            c->alpToYV12 = abgrToA_c_vsx;
            break;
        case AV_PIX_FMT_YA8:
            c->alpToYV12 = uyvyToY_c_vsx;
            break;
        }
    }

#endif /* HAVE_VSX */
}
