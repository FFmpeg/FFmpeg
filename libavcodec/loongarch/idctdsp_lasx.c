/*
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 * Contributed by Hao Chen <chenhao@loongson.cn>
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

#include "idctdsp_loongarch.h"
#include "libavutil/loongarch/loongson_intrinsics.h"

void ff_put_pixels_clamped_lasx(const int16_t *block,
                                uint8_t *restrict pixels,
                                ptrdiff_t stride)
{
    __m256i b0, b1, b2, b3;
    __m256i temp0, temp1;
    ptrdiff_t stride_2x = stride << 1;
    ptrdiff_t stride_4x = stride << 2;
    ptrdiff_t stride_3x = stride_2x + stride;

    DUP4_ARG2(__lasx_xvld, block, 0, block, 32, block, 64, block, 96,
              b0, b1, b2, b3);
    DUP4_ARG1(__lasx_xvclip255_h, b0, b1, b2, b3, b0, b1, b2, b3);
    DUP2_ARG2(__lasx_xvpickev_b, b1, b0, b3, b2, temp0, temp1);
    __lasx_xvstelm_d(temp0, pixels, 0, 0);
    __lasx_xvstelm_d(temp0, pixels + stride, 0, 2);
    __lasx_xvstelm_d(temp0, pixels + stride_2x, 0, 1);
    __lasx_xvstelm_d(temp0, pixels + stride_3x, 0, 3);
    pixels += stride_4x;
    __lasx_xvstelm_d(temp1, pixels, 0, 0);
    __lasx_xvstelm_d(temp1, pixels + stride, 0, 2);
    __lasx_xvstelm_d(temp1, pixels + stride_2x, 0, 1);
    __lasx_xvstelm_d(temp1, pixels + stride_3x, 0, 3);
}

void ff_put_signed_pixels_clamped_lasx(const int16_t *block,
                                       uint8_t *restrict pixels,
                                       ptrdiff_t stride)
{
    __m256i b0, b1, b2, b3;
    __m256i temp0, temp1;
    __m256i const_128 = {0x0080008000800080, 0x0080008000800080,
                         0x0080008000800080, 0x0080008000800080};
    ptrdiff_t stride_2x = stride << 1;
    ptrdiff_t stride_4x = stride << 2;
    ptrdiff_t stride_3x = stride_2x + stride;

    DUP4_ARG2(__lasx_xvld, block, 0, block, 32, block, 64, block, 96,
              b0, b1, b2, b3);
    DUP4_ARG2(__lasx_xvadd_h, b0, const_128, b1, const_128, b2, const_128,
              b3, const_128, b0, b1, b2, b3);
    DUP4_ARG1(__lasx_xvclip255_h, b0, b1, b2, b3, b0, b1, b2, b3);
    DUP2_ARG2(__lasx_xvpickev_b, b1, b0, b3, b2, temp0, temp1);
    __lasx_xvstelm_d(temp0, pixels, 0, 0);
    __lasx_xvstelm_d(temp0, pixels + stride, 0, 2);
    __lasx_xvstelm_d(temp0, pixels + stride_2x, 0, 1);
    __lasx_xvstelm_d(temp0, pixels + stride_3x, 0, 3);
    pixels += stride_4x;
    __lasx_xvstelm_d(temp1, pixels, 0, 0);
    __lasx_xvstelm_d(temp1, pixels + stride, 0, 2);
    __lasx_xvstelm_d(temp1, pixels + stride_2x, 0, 1);
    __lasx_xvstelm_d(temp1, pixels + stride_3x, 0, 3);
}

void ff_add_pixels_clamped_lasx(const int16_t *block,
                                uint8_t *restrict pixels,
                                ptrdiff_t stride)
{
    __m256i b0, b1, b2, b3;
    __m256i p0, p1, p2, p3, p4, p5, p6, p7;
    __m256i temp0, temp1, temp2, temp3;
    uint8_t *pix = pixels;
    ptrdiff_t stride_2x = stride << 1;
    ptrdiff_t stride_4x = stride << 2;
    ptrdiff_t stride_3x = stride_2x + stride;

    DUP4_ARG2(__lasx_xvld, block, 0, block, 32, block, 64, block, 96,
              b0, b1, b2, b3);
    p0   = __lasx_xvldrepl_d(pix, 0);
    pix += stride;
    p1   = __lasx_xvldrepl_d(pix, 0);
    pix += stride;
    p2   = __lasx_xvldrepl_d(pix, 0);
    pix += stride;
    p3   = __lasx_xvldrepl_d(pix, 0);
    pix += stride;
    p4   = __lasx_xvldrepl_d(pix, 0);
    pix += stride;
    p5   = __lasx_xvldrepl_d(pix, 0);
    pix += stride;
    p6   = __lasx_xvldrepl_d(pix, 0);
    pix += stride;
    p7   = __lasx_xvldrepl_d(pix, 0);
    DUP4_ARG3(__lasx_xvpermi_q, p1, p0, 0x20, p3, p2, 0x20, p5, p4, 0x20,
              p7, p6, 0x20, temp0, temp1, temp2, temp3);
    DUP4_ARG2(__lasx_xvaddw_h_h_bu, b0, temp0, b1, temp1, b2, temp2, b3, temp3,
              temp0, temp1, temp2, temp3);
    DUP4_ARG1(__lasx_xvclip255_h, temp0, temp1, temp2, temp3,
              temp0, temp1, temp2, temp3);
    DUP2_ARG2(__lasx_xvpickev_b, temp1, temp0, temp3, temp2, temp0, temp1);
    __lasx_xvstelm_d(temp0, pixels, 0, 0);
    __lasx_xvstelm_d(temp0, pixels + stride, 0, 2);
    __lasx_xvstelm_d(temp0, pixels + stride_2x, 0, 1);
    __lasx_xvstelm_d(temp0, pixels + stride_3x, 0, 3);
    pixels += stride_4x;
    __lasx_xvstelm_d(temp1, pixels, 0, 0);
    __lasx_xvstelm_d(temp1, pixels + stride, 0, 2);
    __lasx_xvstelm_d(temp1, pixels + stride_2x, 0, 1);
    __lasx_xvstelm_d(temp1, pixels + stride_3x, 0, 3);
}
