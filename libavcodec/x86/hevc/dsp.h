/*
 * HEVC video decoder
 *
 * Copyright (C) 2012 - 2013 Guillaume Martres
 * Copyright (C) 2013 - 2014 Pierre-Edouard Lepere
 *
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

#ifndef AVCODEC_X86_HEVC_DSP_H
#define AVCODEC_X86_HEVC_DSP_H

#include <stddef.h>
#include <stdint.h>

typedef void bi_pel_func(uint8_t *_dst, ptrdiff_t _dststride,
                         const uint8_t *_src, ptrdiff_t _srcstride, const int16_t *src2,
                         int height, intptr_t mx, intptr_t my, int width);

#define BI_PEL_PROTOTYPE(name, W, D, opt) \
bi_pel_func ff_hevc_put_bi_ ## name ## W ## _ ## D ## _##opt

///////////////////////////////////////////////////////////////////////////////
// MC functions
///////////////////////////////////////////////////////////////////////////////

#define WEIGHTING_PROTOTYPE(width, bitd, opt) \
void ff_hevc_put_uni_w##width##_##bitd##_##opt(uint8_t *dst, ptrdiff_t dststride, const int16_t *_src, int height, int denom,  int _wx, int _ox);      \
void ff_hevc_put_bi_w##width##_##bitd##_##opt(uint8_t *dst, ptrdiff_t dststride, const int16_t *_src, const int16_t *_src2, int height, int denom,  int _wx0,  int _wx1, int _ox0, int _ox1)

#define WEIGHTING_PROTOTYPES(bitd, opt) \
        WEIGHTING_PROTOTYPE(4, bitd, opt); \
        WEIGHTING_PROTOTYPE(6, bitd, opt); \
        WEIGHTING_PROTOTYPE(8, bitd, opt); \
        WEIGHTING_PROTOTYPE(12, bitd, opt); \
        WEIGHTING_PROTOTYPE(16, bitd, opt); \
        WEIGHTING_PROTOTYPE(24, bitd, opt); \
        WEIGHTING_PROTOTYPE(32, bitd, opt); \
        WEIGHTING_PROTOTYPE(48, bitd, opt); \
        WEIGHTING_PROTOTYPE(64, bitd, opt)


///////////////////////////////////////////////////////////////////////////////
// EPEL_PIXELS
///////////////////////////////////////////////////////////////////////////////

BI_PEL_PROTOTYPE(pel_pixels,  4,  8, sse4);
BI_PEL_PROTOTYPE(pel_pixels,  4, 10, sse4);
BI_PEL_PROTOTYPE(pel_pixels,  4, 12, sse4);
BI_PEL_PROTOTYPE(pel_pixels,  6,  8, sse4);
BI_PEL_PROTOTYPE(pel_pixels,  6, 10, sse4);
BI_PEL_PROTOTYPE(pel_pixels,  6, 12, sse4);
BI_PEL_PROTOTYPE(pel_pixels,  8,  8, sse4);
BI_PEL_PROTOTYPE(pel_pixels,  8, 10, sse4);
BI_PEL_PROTOTYPE(pel_pixels,  8, 12, sse4);
BI_PEL_PROTOTYPE(pel_pixels, 12,  8, sse4);
BI_PEL_PROTOTYPE(pel_pixels, 16,  8, sse4);
BI_PEL_PROTOTYPE(pel_pixels, 16, 10, avx2);
BI_PEL_PROTOTYPE(pel_pixels, 32,  8, avx2);

///////////////////////////////////////////////////////////////////////////////
// EPEL
///////////////////////////////////////////////////////////////////////////////

BI_PEL_PROTOTYPE(epel_h,   4,  8, sse4);
BI_PEL_PROTOTYPE(epel_h,   4, 10, sse4);
BI_PEL_PROTOTYPE(epel_h,   4, 12, sse4);
BI_PEL_PROTOTYPE(epel_h,   6,  8, sse4);
BI_PEL_PROTOTYPE(epel_h,   6, 10, sse4);
BI_PEL_PROTOTYPE(epel_h,   6, 12, sse4);
BI_PEL_PROTOTYPE(epel_h,   8,  8, sse4);
BI_PEL_PROTOTYPE(epel_h,   8, 10, sse4);
BI_PEL_PROTOTYPE(epel_h,   8, 12, sse4);
BI_PEL_PROTOTYPE(epel_h,  12,  8, sse4);
BI_PEL_PROTOTYPE(epel_h,  16,  8, sse4);
BI_PEL_PROTOTYPE(epel_h,  16, 10, avx2);
BI_PEL_PROTOTYPE(epel_h,  32,  8, avx2);

BI_PEL_PROTOTYPE(epel_hv,  4,  8, sse4);
BI_PEL_PROTOTYPE(epel_hv,  4, 10, sse4);
BI_PEL_PROTOTYPE(epel_hv,  4, 12, sse4);
BI_PEL_PROTOTYPE(epel_hv,  6,  8, sse4);
BI_PEL_PROTOTYPE(epel_hv,  6, 10, sse4);
BI_PEL_PROTOTYPE(epel_hv,  6, 12, sse4);
BI_PEL_PROTOTYPE(epel_hv,  8,  8, sse4);
BI_PEL_PROTOTYPE(epel_hv,  8, 10, sse4);
BI_PEL_PROTOTYPE(epel_hv,  8, 12, sse4);
BI_PEL_PROTOTYPE(epel_hv, 16,  8, sse4);
BI_PEL_PROTOTYPE(epel_hv, 16, 10, avx2);
BI_PEL_PROTOTYPE(epel_hv, 32,  8, avx2);

BI_PEL_PROTOTYPE(epel_v,   4,  8, sse4);
BI_PEL_PROTOTYPE(epel_v,   4, 10, sse4);
BI_PEL_PROTOTYPE(epel_v,   4, 12, sse4);
BI_PEL_PROTOTYPE(epel_v,   6,  8, sse4);
BI_PEL_PROTOTYPE(epel_v,   6, 10, sse4);
BI_PEL_PROTOTYPE(epel_v,   6, 12, sse4);
BI_PEL_PROTOTYPE(epel_v,   8,  8, sse4);
BI_PEL_PROTOTYPE(epel_v,   8, 10, sse4);
BI_PEL_PROTOTYPE(epel_v,   8, 12, sse4);
BI_PEL_PROTOTYPE(epel_v,  12,  8, sse4);
BI_PEL_PROTOTYPE(epel_v,  16,  8, sse4);
BI_PEL_PROTOTYPE(epel_v,  16, 10, avx2);
BI_PEL_PROTOTYPE(epel_v,  32,  8, avx2);

///////////////////////////////////////////////////////////////////////////////
// QPEL
///////////////////////////////////////////////////////////////////////////////

BI_PEL_PROTOTYPE(qpel_h,   4,  8, sse4);
BI_PEL_PROTOTYPE(qpel_h,   4, 10, sse4);
BI_PEL_PROTOTYPE(qpel_h,   4, 12, sse4);
BI_PEL_PROTOTYPE(qpel_h,   8,  8, sse4);
BI_PEL_PROTOTYPE(qpel_h,   8, 10, sse4);
BI_PEL_PROTOTYPE(qpel_h,   8, 12, sse4);
BI_PEL_PROTOTYPE(qpel_h,  12,  8, sse4);
BI_PEL_PROTOTYPE(qpel_h,  16,  8, sse4);
BI_PEL_PROTOTYPE(qpel_h,  16, 10, avx2);
BI_PEL_PROTOTYPE(qpel_h,  32,  8, avx2);

BI_PEL_PROTOTYPE(qpel_hv,  4,  8, sse4);
BI_PEL_PROTOTYPE(qpel_hv,  4, 10, sse4);
BI_PEL_PROTOTYPE(qpel_hv,  4, 12, sse4);
BI_PEL_PROTOTYPE(qpel_hv,  8,  8, sse4);
BI_PEL_PROTOTYPE(qpel_hv,  8, 10, sse4);
BI_PEL_PROTOTYPE(qpel_hv,  8, 12, sse4);
BI_PEL_PROTOTYPE(qpel_hv, 16, 10, avx2);

BI_PEL_PROTOTYPE(qpel_v,   4,  8, sse4);
BI_PEL_PROTOTYPE(qpel_v,   4, 10, sse4);
BI_PEL_PROTOTYPE(qpel_v,   4, 12, sse4);
BI_PEL_PROTOTYPE(qpel_v,   8,  8, sse4);
BI_PEL_PROTOTYPE(qpel_v,   8, 10, sse4);
BI_PEL_PROTOTYPE(qpel_v,   8, 12, sse4);
BI_PEL_PROTOTYPE(qpel_v,  12,  8, sse4);
BI_PEL_PROTOTYPE(qpel_v,  16,  8, sse4);
BI_PEL_PROTOTYPE(qpel_v,  16, 10, avx2);
BI_PEL_PROTOTYPE(qpel_v,  32,  8, avx2);

WEIGHTING_PROTOTYPES(8, sse4);
WEIGHTING_PROTOTYPES(10, sse4);
WEIGHTING_PROTOTYPES(12, sse4);

void ff_hevc_put_qpel_h4_8_avx512icl(int16_t *dst, const uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width);
void ff_hevc_put_qpel_h8_8_avx512icl(int16_t *dst, const uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width);
void ff_hevc_put_qpel_h16_8_avx512icl(int16_t *dst, const uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width);
void ff_hevc_put_qpel_h32_8_avx512icl(int16_t *dst, const uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width);
void ff_hevc_put_qpel_h64_8_avx512icl(int16_t *dst, const uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width);
void ff_hevc_put_qpel_hv8_8_avx512icl(int16_t *dst, const uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width);

///////////////////////////////////////////////////////////////////////////////
// TRANSFORM_ADD
///////////////////////////////////////////////////////////////////////////////

void ff_hevc_add_residual_4_8_mmxext(uint8_t *dst, const int16_t *res, ptrdiff_t stride);
void ff_hevc_add_residual_8_8_sse2(uint8_t *dst, const int16_t *res, ptrdiff_t stride);
void ff_hevc_add_residual_16_8_sse2(uint8_t *dst, const int16_t *res, ptrdiff_t stride);
void ff_hevc_add_residual_32_8_sse2(uint8_t *dst, const int16_t *res, ptrdiff_t stride);

void ff_hevc_add_residual_8_8_avx(uint8_t *dst, const int16_t *res, ptrdiff_t stride);
void ff_hevc_add_residual_16_8_avx(uint8_t *dst, const int16_t *res, ptrdiff_t stride);
void ff_hevc_add_residual_32_8_avx(uint8_t *dst, const int16_t *res, ptrdiff_t stride);

void ff_hevc_add_residual_32_8_avx2(uint8_t *dst, const int16_t *res, ptrdiff_t stride);

void ff_hevc_add_residual_4_10_mmxext(uint8_t *dst, const int16_t *res, ptrdiff_t stride);
void ff_hevc_add_residual_8_10_sse2(uint8_t *dst, const int16_t *res, ptrdiff_t stride);
void ff_hevc_add_residual_16_10_sse2(uint8_t *dst, const int16_t *res, ptrdiff_t stride);
void ff_hevc_add_residual_32_10_sse2(uint8_t *dst, const int16_t *res, ptrdiff_t stride);

void ff_hevc_add_residual_16_10_avx2(uint8_t *dst, const int16_t *res, ptrdiff_t stride);
void ff_hevc_add_residual_32_10_avx2(uint8_t *dst, const int16_t *res, ptrdiff_t stride);

#endif // AVCODEC_X86_HEVC_DSP_H
