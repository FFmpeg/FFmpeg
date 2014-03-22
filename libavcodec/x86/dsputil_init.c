/*
 * Copyright (c) 2000, 2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
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

#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/internal.h"
#include "libavutil/x86/asm.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/dsputil.h"
#include "libavcodec/pixels.h"
#include "libavcodec/simple_idct.h"
#include "libavcodec/version.h"
#include "dsputil_x86.h"
#include "fpel.h"
#include "idct_xvid.h"

void ff_put_pixels8_l2_mmxext(uint8_t *dst, uint8_t *src1, uint8_t *src2,
                              int dstStride, int src1Stride, int h);
void ff_put_no_rnd_pixels8_l2_mmxext(uint8_t *dst, uint8_t *src1,
                                     uint8_t *src2, int dstStride,
                                     int src1Stride, int h);
void ff_avg_pixels8_l2_mmxext(uint8_t *dst, uint8_t *src1, uint8_t *src2,
                              int dstStride, int src1Stride, int h);
void ff_put_pixels16_l2_mmxext(uint8_t *dst, uint8_t *src1, uint8_t *src2,
                               int dstStride, int src1Stride, int h);
void ff_avg_pixels16_l2_mmxext(uint8_t *dst, uint8_t *src1, uint8_t *src2,
                               int dstStride, int src1Stride, int h);
void ff_put_no_rnd_pixels16_l2_mmxext(uint8_t *dst, uint8_t *src1, uint8_t *src2,
                                      int dstStride, int src1Stride, int h);
void ff_put_mpeg4_qpel16_h_lowpass_mmxext(uint8_t *dst, uint8_t *src,
                                          int dstStride, int srcStride, int h);
void ff_avg_mpeg4_qpel16_h_lowpass_mmxext(uint8_t *dst, uint8_t *src,
                                          int dstStride, int srcStride, int h);
void ff_put_no_rnd_mpeg4_qpel16_h_lowpass_mmxext(uint8_t *dst, uint8_t *src,
                                                 int dstStride, int srcStride,
                                                 int h);
void ff_put_mpeg4_qpel8_h_lowpass_mmxext(uint8_t *dst, uint8_t *src,
                                         int dstStride, int srcStride, int h);
void ff_avg_mpeg4_qpel8_h_lowpass_mmxext(uint8_t *dst, uint8_t *src,
                                         int dstStride, int srcStride, int h);
void ff_put_no_rnd_mpeg4_qpel8_h_lowpass_mmxext(uint8_t *dst, uint8_t *src,
                                                int dstStride, int srcStride,
                                                int h);
void ff_put_mpeg4_qpel16_v_lowpass_mmxext(uint8_t *dst, uint8_t *src,
                                          int dstStride, int srcStride);
void ff_avg_mpeg4_qpel16_v_lowpass_mmxext(uint8_t *dst, uint8_t *src,
                                          int dstStride, int srcStride);
void ff_put_no_rnd_mpeg4_qpel16_v_lowpass_mmxext(uint8_t *dst, uint8_t *src,
                                                 int dstStride, int srcStride);
void ff_put_mpeg4_qpel8_v_lowpass_mmxext(uint8_t *dst, uint8_t *src,
                                         int dstStride, int srcStride);
void ff_avg_mpeg4_qpel8_v_lowpass_mmxext(uint8_t *dst, uint8_t *src,
                                         int dstStride, int srcStride);
void ff_put_no_rnd_mpeg4_qpel8_v_lowpass_mmxext(uint8_t *dst, uint8_t *src,
                                                int dstStride, int srcStride);
#define ff_put_no_rnd_pixels16_mmxext ff_put_pixels16_mmxext
#define ff_put_no_rnd_pixels8_mmxext ff_put_pixels8_mmxext

int32_t ff_scalarproduct_int16_mmxext(const int16_t *v1, const int16_t *v2,
                                      int order);
int32_t ff_scalarproduct_int16_sse2(const int16_t *v1, const int16_t *v2,
                                    int order);
int32_t ff_scalarproduct_and_madd_int16_mmxext(int16_t *v1, const int16_t *v2,
                                               const int16_t *v3,
                                               int order, int mul);
int32_t ff_scalarproduct_and_madd_int16_sse2(int16_t *v1, const int16_t *v2,
                                             const int16_t *v3,
                                             int order, int mul);
int32_t ff_scalarproduct_and_madd_int16_ssse3(int16_t *v1, const int16_t *v2,
                                              const int16_t *v3,
                                              int order, int mul);

void ff_bswap32_buf_ssse3(uint32_t *dst, const uint32_t *src, int w);
void ff_bswap32_buf_sse2(uint32_t *dst, const uint32_t *src, int w);

void ff_add_hfyu_median_prediction_mmxext(uint8_t *dst, const uint8_t *top,
                                          const uint8_t *diff, int w,
                                          int *left, int *left_top);
int ff_add_hfyu_left_prediction_ssse3(uint8_t *dst, const uint8_t *src,
                                      int w, int left);
int ff_add_hfyu_left_prediction_sse4(uint8_t *dst, const uint8_t *src,
                                     int w, int left);

void ff_vector_clip_int32_mmx(int32_t *dst, const int32_t *src,
                              int32_t min, int32_t max, unsigned int len);
void ff_vector_clip_int32_sse2(int32_t *dst, const int32_t *src,
                               int32_t min, int32_t max, unsigned int len);
void ff_vector_clip_int32_int_sse2(int32_t *dst, const int32_t *src,
                                   int32_t min, int32_t max, unsigned int len);
void ff_vector_clip_int32_sse4(int32_t *dst, const int32_t *src,
                               int32_t min, int32_t max, unsigned int len);

#if HAVE_YASM

CALL_2X_PIXELS(ff_avg_pixels16_mmxext, ff_avg_pixels8_mmxext, 8)
CALL_2X_PIXELS(ff_put_pixels16_mmxext, ff_put_pixels8_mmxext, 8)

#define QPEL_OP(OPNAME, RND, MMX)                                       \
static void OPNAME ## qpel8_mc00_ ## MMX(uint8_t *dst, uint8_t *src,    \
                                         ptrdiff_t stride)              \
{                                                                       \
    ff_ ## OPNAME ## pixels8_ ## MMX(dst, src, stride, 8);              \
}                                                                       \
                                                                        \
static void OPNAME ## qpel8_mc10_ ## MMX(uint8_t *dst, uint8_t *src,    \
                                         ptrdiff_t stride)              \
{                                                                       \
    uint64_t temp[8];                                                   \
    uint8_t *const half = (uint8_t *) temp;                             \
    ff_put ## RND ## mpeg4_qpel8_h_lowpass_ ## MMX(half, src, 8,        \
                                                   stride, 8);          \
    ff_ ## OPNAME ## pixels8_l2_ ## MMX(dst, src, half,                 \
                                        stride, stride, 8);             \
}                                                                       \
                                                                        \
static void OPNAME ## qpel8_mc20_ ## MMX(uint8_t *dst, uint8_t *src,    \
                                         ptrdiff_t stride)              \
{                                                                       \
    ff_ ## OPNAME ## mpeg4_qpel8_h_lowpass_ ## MMX(dst, src, stride,    \
                                                   stride, 8);          \
}                                                                       \
                                                                        \
static void OPNAME ## qpel8_mc30_ ## MMX(uint8_t *dst, uint8_t *src,    \
                                         ptrdiff_t stride)              \
{                                                                       \
    uint64_t temp[8];                                                   \
    uint8_t *const half = (uint8_t *) temp;                             \
    ff_put ## RND ## mpeg4_qpel8_h_lowpass_ ## MMX(half, src, 8,        \
                                                   stride, 8);          \
    ff_ ## OPNAME ## pixels8_l2_ ## MMX(dst, src + 1, half, stride,     \
                                        stride, 8);                     \
}                                                                       \
                                                                        \
static void OPNAME ## qpel8_mc01_ ## MMX(uint8_t *dst, uint8_t *src,    \
                                         ptrdiff_t stride)              \
{                                                                       \
    uint64_t temp[8];                                                   \
    uint8_t *const half = (uint8_t *) temp;                             \
    ff_put ## RND ## mpeg4_qpel8_v_lowpass_ ## MMX(half, src,           \
                                                   8, stride);          \
    ff_ ## OPNAME ## pixels8_l2_ ## MMX(dst, src, half,                 \
                                        stride, stride, 8);             \
}                                                                       \
                                                                        \
static void OPNAME ## qpel8_mc02_ ## MMX(uint8_t *dst, uint8_t *src,    \
                                         ptrdiff_t stride)              \
{                                                                       \
    ff_ ## OPNAME ## mpeg4_qpel8_v_lowpass_ ## MMX(dst, src,            \
                                                   stride, stride);     \
}                                                                       \
                                                                        \
static void OPNAME ## qpel8_mc03_ ## MMX(uint8_t *dst, uint8_t *src,    \
                                         ptrdiff_t stride)              \
{                                                                       \
    uint64_t temp[8];                                                   \
    uint8_t *const half = (uint8_t *) temp;                             \
    ff_put ## RND ## mpeg4_qpel8_v_lowpass_ ## MMX(half, src,           \
                                                   8, stride);          \
    ff_ ## OPNAME ## pixels8_l2_ ## MMX(dst, src + stride, half, stride,\
                                        stride, 8);                     \
}                                                                       \
                                                                        \
static void OPNAME ## qpel8_mc11_ ## MMX(uint8_t *dst, uint8_t *src,    \
                                         ptrdiff_t stride)              \
{                                                                       \
    uint64_t half[8 + 9];                                               \
    uint8_t *const halfH  = (uint8_t *) half + 64;                      \
    uint8_t *const halfHV = (uint8_t *) half;                           \
    ff_put ## RND ## mpeg4_qpel8_h_lowpass_ ## MMX(halfH, src, 8,       \
                                                   stride, 9);          \
    ff_put ## RND ## pixels8_l2_ ## MMX(halfH, src, halfH, 8,           \
                                        stride, 9);                     \
    ff_put ## RND ## mpeg4_qpel8_v_lowpass_ ## MMX(halfHV, halfH, 8, 8);\
    ff_ ## OPNAME ## pixels8_l2_ ## MMX(dst, halfH, halfHV,             \
                                        stride, 8, 8);                  \
}                                                                       \
                                                                        \
static void OPNAME ## qpel8_mc31_ ## MMX(uint8_t *dst, uint8_t *src,    \
                                         ptrdiff_t stride)              \
{                                                                       \
    uint64_t half[8 + 9];                                               \
    uint8_t *const halfH  = (uint8_t *) half + 64;                      \
    uint8_t *const halfHV = (uint8_t *) half;                           \
    ff_put ## RND ## mpeg4_qpel8_h_lowpass_ ## MMX(halfH, src, 8,       \
                                                   stride, 9);          \
    ff_put ## RND ## pixels8_l2_ ## MMX(halfH, src + 1, halfH, 8,       \
                                        stride, 9);                     \
    ff_put ## RND ## mpeg4_qpel8_v_lowpass_ ## MMX(halfHV, halfH, 8, 8);\
    ff_ ## OPNAME ## pixels8_l2_ ## MMX(dst, halfH, halfHV,             \
                                        stride, 8, 8);                  \
}                                                                       \
                                                                        \
static void OPNAME ## qpel8_mc13_ ## MMX(uint8_t *dst, uint8_t *src,    \
                                         ptrdiff_t stride)              \
{                                                                       \
    uint64_t half[8 + 9];                                               \
    uint8_t *const halfH  = (uint8_t *) half + 64;                      \
    uint8_t *const halfHV = (uint8_t *) half;                           \
    ff_put ## RND ## mpeg4_qpel8_h_lowpass_ ## MMX(halfH, src, 8,       \
                                                   stride, 9);          \
    ff_put ## RND ## pixels8_l2_ ## MMX(halfH, src, halfH, 8,           \
                                        stride, 9);                     \
    ff_put ## RND ## mpeg4_qpel8_v_lowpass_ ## MMX(halfHV, halfH, 8, 8);\
    ff_ ## OPNAME ## pixels8_l2_ ## MMX(dst, halfH + 8, halfHV,         \
                                        stride, 8, 8);                  \
}                                                                       \
                                                                        \
static void OPNAME ## qpel8_mc33_ ## MMX(uint8_t *dst, uint8_t *src,    \
                                         ptrdiff_t stride)              \
{                                                                       \
    uint64_t half[8 + 9];                                               \
    uint8_t *const halfH  = (uint8_t *) half + 64;                      \
    uint8_t *const halfHV = (uint8_t *) half;                           \
    ff_put ## RND ## mpeg4_qpel8_h_lowpass_ ## MMX(halfH, src, 8,       \
                                                   stride, 9);          \
    ff_put ## RND ## pixels8_l2_ ## MMX(halfH, src + 1, halfH, 8,       \
                                        stride, 9);                     \
    ff_put ## RND ## mpeg4_qpel8_v_lowpass_ ## MMX(halfHV, halfH, 8, 8);\
    ff_ ## OPNAME ## pixels8_l2_ ## MMX(dst, halfH + 8, halfHV,         \
                                        stride, 8, 8);                  \
}                                                                       \
                                                                        \
static void OPNAME ## qpel8_mc21_ ## MMX(uint8_t *dst, uint8_t *src,    \
                                         ptrdiff_t stride)              \
{                                                                       \
    uint64_t half[8 + 9];                                               \
    uint8_t *const halfH  = (uint8_t *) half + 64;                      \
    uint8_t *const halfHV = (uint8_t *) half;                           \
    ff_put ## RND ## mpeg4_qpel8_h_lowpass_ ## MMX(halfH, src, 8,       \
                                                   stride, 9);          \
    ff_put ## RND ## mpeg4_qpel8_v_lowpass_ ## MMX(halfHV, halfH, 8, 8);\
    ff_ ## OPNAME ## pixels8_l2_ ## MMX(dst, halfH, halfHV,             \
                                        stride, 8, 8);                  \
}                                                                       \
                                                                        \
static void OPNAME ## qpel8_mc23_ ## MMX(uint8_t *dst, uint8_t *src,    \
                                         ptrdiff_t stride)              \
{                                                                       \
    uint64_t half[8 + 9];                                               \
    uint8_t *const halfH  = (uint8_t *) half + 64;                      \
    uint8_t *const halfHV = (uint8_t *) half;                           \
    ff_put ## RND ## mpeg4_qpel8_h_lowpass_ ## MMX(halfH, src, 8,       \
                                                   stride, 9);          \
    ff_put ## RND ## mpeg4_qpel8_v_lowpass_ ## MMX(halfHV, halfH, 8, 8);\
    ff_ ## OPNAME ## pixels8_l2_ ## MMX(dst, halfH + 8, halfHV,         \
                                        stride, 8, 8);                  \
}                                                                       \
                                                                        \
static void OPNAME ## qpel8_mc12_ ## MMX(uint8_t *dst, uint8_t *src,    \
                                         ptrdiff_t stride)              \
{                                                                       \
    uint64_t half[8 + 9];                                               \
    uint8_t *const halfH = (uint8_t *) half;                            \
    ff_put ## RND ## mpeg4_qpel8_h_lowpass_ ## MMX(halfH, src, 8,       \
                                                   stride, 9);          \
    ff_put ## RND ## pixels8_l2_ ## MMX(halfH, src, halfH,              \
                                        8, stride, 9);                  \
    ff_ ## OPNAME ## mpeg4_qpel8_v_lowpass_ ## MMX(dst, halfH,          \
                                                   stride, 8);          \
}                                                                       \
                                                                        \
static void OPNAME ## qpel8_mc32_ ## MMX(uint8_t *dst, uint8_t *src,    \
                                         ptrdiff_t stride)              \
{                                                                       \
    uint64_t half[8 + 9];                                               \
    uint8_t *const halfH = (uint8_t *) half;                            \
    ff_put ## RND ## mpeg4_qpel8_h_lowpass_ ## MMX(halfH, src, 8,       \
                                                   stride, 9);          \
    ff_put ## RND ## pixels8_l2_ ## MMX(halfH, src + 1, halfH, 8,       \
                                        stride, 9);                     \
    ff_ ## OPNAME ## mpeg4_qpel8_v_lowpass_ ## MMX(dst, halfH,          \
                                                   stride, 8);          \
}                                                                       \
                                                                        \
static void OPNAME ## qpel8_mc22_ ## MMX(uint8_t *dst, uint8_t *src,    \
                                         ptrdiff_t stride)              \
{                                                                       \
    uint64_t half[9];                                                   \
    uint8_t *const halfH = (uint8_t *) half;                            \
    ff_put ## RND ## mpeg4_qpel8_h_lowpass_ ## MMX(halfH, src, 8,       \
                                                   stride, 9);          \
    ff_ ## OPNAME ## mpeg4_qpel8_v_lowpass_ ## MMX(dst, halfH,          \
                                                   stride, 8);          \
}                                                                       \
                                                                        \
static void OPNAME ## qpel16_mc00_ ## MMX(uint8_t *dst, uint8_t *src,   \
                                          ptrdiff_t stride)             \
{                                                                       \
    ff_ ## OPNAME ## pixels16_ ## MMX(dst, src, stride, 16);            \
}                                                                       \
                                                                        \
static void OPNAME ## qpel16_mc10_ ## MMX(uint8_t *dst, uint8_t *src,   \
                                          ptrdiff_t stride)             \
{                                                                       \
    uint64_t temp[32];                                                  \
    uint8_t *const half = (uint8_t *) temp;                             \
    ff_put ## RND ## mpeg4_qpel16_h_lowpass_ ## MMX(half, src, 16,      \
                                                    stride, 16);        \
    ff_ ## OPNAME ## pixels16_l2_ ## MMX(dst, src, half, stride,        \
                                         stride, 16);                   \
}                                                                       \
                                                                        \
static void OPNAME ## qpel16_mc20_ ## MMX(uint8_t *dst, uint8_t *src,   \
                                          ptrdiff_t stride)             \
{                                                                       \
    ff_ ## OPNAME ## mpeg4_qpel16_h_lowpass_ ## MMX(dst, src,           \
                                                    stride, stride, 16);\
}                                                                       \
                                                                        \
static void OPNAME ## qpel16_mc30_ ## MMX(uint8_t *dst, uint8_t *src,   \
                                          ptrdiff_t stride)             \
{                                                                       \
    uint64_t temp[32];                                                  \
    uint8_t *const half = (uint8_t*) temp;                              \
    ff_put ## RND ## mpeg4_qpel16_h_lowpass_ ## MMX(half, src, 16,      \
                                                    stride, 16);        \
    ff_ ## OPNAME ## pixels16_l2_ ## MMX(dst, src + 1, half,            \
                                         stride, stride, 16);           \
}                                                                       \
                                                                        \
static void OPNAME ## qpel16_mc01_ ## MMX(uint8_t *dst, uint8_t *src,   \
                                          ptrdiff_t stride)             \
{                                                                       \
    uint64_t temp[32];                                                  \
    uint8_t *const half = (uint8_t *) temp;                             \
    ff_put ## RND ## mpeg4_qpel16_v_lowpass_ ## MMX(half, src, 16,      \
                                                    stride);            \
    ff_ ## OPNAME ## pixels16_l2_ ## MMX(dst, src, half, stride,        \
                                         stride, 16);                   \
}                                                                       \
                                                                        \
static void OPNAME ## qpel16_mc02_ ## MMX(uint8_t *dst, uint8_t *src,   \
                                          ptrdiff_t stride)             \
{                                                                       \
    ff_ ## OPNAME ## mpeg4_qpel16_v_lowpass_ ## MMX(dst, src,           \
                                                    stride, stride);    \
}                                                                       \
                                                                        \
static void OPNAME ## qpel16_mc03_ ## MMX(uint8_t *dst, uint8_t *src,   \
                                          ptrdiff_t stride)             \
{                                                                       \
    uint64_t temp[32];                                                  \
    uint8_t *const half = (uint8_t *) temp;                             \
    ff_put ## RND ## mpeg4_qpel16_v_lowpass_ ## MMX(half, src, 16,      \
                                                    stride);            \
    ff_ ## OPNAME ## pixels16_l2_ ## MMX(dst, src+stride, half,         \
                                         stride, stride, 16);           \
}                                                                       \
                                                                        \
static void OPNAME ## qpel16_mc11_ ## MMX(uint8_t *dst, uint8_t *src,   \
                                          ptrdiff_t stride)             \
{                                                                       \
    uint64_t half[16 * 2 + 17 * 2];                                     \
    uint8_t *const halfH  = (uint8_t *) half + 256;                     \
    uint8_t *const halfHV = (uint8_t *) half;                           \
    ff_put ## RND ## mpeg4_qpel16_h_lowpass_ ## MMX(halfH, src, 16,     \
                                                    stride, 17);        \
    ff_put ## RND ## pixels16_l2_ ## MMX(halfH, src, halfH, 16,         \
                                         stride, 17);                   \
    ff_put ## RND ## mpeg4_qpel16_v_lowpass_ ## MMX(halfHV, halfH,      \
                                                    16, 16);            \
    ff_ ## OPNAME ## pixels16_l2_ ## MMX(dst, halfH, halfHV,            \
                                         stride, 16, 16);               \
}                                                                       \
                                                                        \
static void OPNAME ## qpel16_mc31_ ## MMX(uint8_t *dst, uint8_t *src,   \
                                          ptrdiff_t stride)             \
{                                                                       \
    uint64_t half[16 * 2 + 17 * 2];                                     \
    uint8_t *const halfH  = (uint8_t *) half + 256;                     \
    uint8_t *const halfHV = (uint8_t *) half;                           \
    ff_put ## RND ## mpeg4_qpel16_h_lowpass_ ## MMX(halfH, src, 16,     \
                                                    stride, 17);        \
    ff_put ## RND ## pixels16_l2_ ## MMX(halfH, src + 1, halfH, 16,     \
                                         stride, 17);                   \
    ff_put ## RND ## mpeg4_qpel16_v_lowpass_ ## MMX(halfHV, halfH,      \
                                                    16, 16);            \
    ff_ ## OPNAME ## pixels16_l2_ ## MMX(dst, halfH, halfHV,            \
                                         stride, 16, 16);               \
}                                                                       \
                                                                        \
static void OPNAME ## qpel16_mc13_ ## MMX(uint8_t *dst, uint8_t *src,   \
                                          ptrdiff_t stride)             \
{                                                                       \
    uint64_t half[16 * 2 + 17 * 2];                                     \
    uint8_t *const halfH  = (uint8_t *) half + 256;                     \
    uint8_t *const halfHV = (uint8_t *) half;                           \
    ff_put ## RND ## mpeg4_qpel16_h_lowpass_ ## MMX(halfH, src, 16,     \
                                                    stride, 17);        \
    ff_put ## RND ## pixels16_l2_ ## MMX(halfH, src, halfH, 16,         \
                                         stride, 17);                   \
    ff_put ## RND ## mpeg4_qpel16_v_lowpass_ ## MMX(halfHV, halfH,      \
                                                    16, 16);            \
    ff_ ## OPNAME ## pixels16_l2_ ## MMX(dst, halfH + 16, halfHV,       \
                                         stride, 16, 16);               \
}                                                                       \
                                                                        \
static void OPNAME ## qpel16_mc33_ ## MMX(uint8_t *dst, uint8_t *src,   \
                                          ptrdiff_t stride)             \
{                                                                       \
    uint64_t half[16 * 2 + 17 * 2];                                     \
    uint8_t *const halfH  = (uint8_t *) half + 256;                     \
    uint8_t *const halfHV = (uint8_t *) half;                           \
    ff_put ## RND ## mpeg4_qpel16_h_lowpass_ ## MMX(halfH, src, 16,     \
                                                    stride, 17);        \
    ff_put ## RND ## pixels16_l2_ ## MMX(halfH, src + 1, halfH, 16,     \
                                         stride, 17);                   \
    ff_put ## RND ## mpeg4_qpel16_v_lowpass_ ## MMX(halfHV, halfH,      \
                                                    16, 16);            \
    ff_ ## OPNAME ## pixels16_l2_ ## MMX(dst, halfH + 16, halfHV,       \
                                         stride, 16, 16);               \
}                                                                       \
                                                                        \
static void OPNAME ## qpel16_mc21_ ## MMX(uint8_t *dst, uint8_t *src,   \
                                          ptrdiff_t stride)             \
{                                                                       \
    uint64_t half[16 * 2 + 17 * 2];                                     \
    uint8_t *const halfH  = (uint8_t *) half + 256;                     \
    uint8_t *const halfHV = (uint8_t *) half;                           \
    ff_put ## RND ## mpeg4_qpel16_h_lowpass_ ## MMX(halfH, src, 16,     \
                                                    stride, 17);        \
    ff_put ## RND ## mpeg4_qpel16_v_lowpass_ ## MMX(halfHV, halfH,      \
                                                    16, 16);            \
    ff_ ## OPNAME ## pixels16_l2_ ## MMX(dst, halfH, halfHV,            \
                                         stride, 16, 16);               \
}                                                                       \
                                                                        \
static void OPNAME ## qpel16_mc23_ ## MMX(uint8_t *dst, uint8_t *src,   \
                                          ptrdiff_t stride)             \
{                                                                       \
    uint64_t half[16 * 2 + 17 * 2];                                     \
    uint8_t *const halfH  = (uint8_t *) half + 256;                     \
    uint8_t *const halfHV = (uint8_t *) half;                           \
    ff_put ## RND ## mpeg4_qpel16_h_lowpass_ ## MMX(halfH, src, 16,     \
                                                    stride, 17);        \
    ff_put ## RND ## mpeg4_qpel16_v_lowpass_ ## MMX(halfHV, halfH,      \
                                                    16, 16);            \
    ff_ ## OPNAME ## pixels16_l2_ ## MMX(dst, halfH + 16, halfHV,       \
                                         stride, 16, 16);               \
}                                                                       \
                                                                        \
static void OPNAME ## qpel16_mc12_ ## MMX(uint8_t *dst, uint8_t *src,   \
                                          ptrdiff_t stride)             \
{                                                                       \
    uint64_t half[17 * 2];                                              \
    uint8_t *const halfH = (uint8_t *) half;                            \
    ff_put ## RND ## mpeg4_qpel16_h_lowpass_ ## MMX(halfH, src, 16,     \
                                                    stride, 17);        \
    ff_put ## RND ## pixels16_l2_ ## MMX(halfH, src, halfH, 16,         \
                                         stride, 17);                   \
    ff_ ## OPNAME ## mpeg4_qpel16_v_lowpass_ ## MMX(dst, halfH,         \
                                                    stride, 16);        \
}                                                                       \
                                                                        \
static void OPNAME ## qpel16_mc32_ ## MMX(uint8_t *dst, uint8_t *src,   \
                                          ptrdiff_t stride)             \
{                                                                       \
    uint64_t half[17 * 2];                                              \
    uint8_t *const halfH = (uint8_t *) half;                            \
    ff_put ## RND ## mpeg4_qpel16_h_lowpass_ ## MMX(halfH, src, 16,     \
                                                    stride, 17);        \
    ff_put ## RND ## pixels16_l2_ ## MMX(halfH, src + 1, halfH, 16,     \
                                         stride, 17);                   \
    ff_ ## OPNAME ## mpeg4_qpel16_v_lowpass_ ## MMX(dst, halfH,         \
                                                    stride, 16);        \
}                                                                       \
                                                                        \
static void OPNAME ## qpel16_mc22_ ## MMX(uint8_t *dst, uint8_t *src,   \
                                          ptrdiff_t stride)             \
{                                                                       \
    uint64_t half[17 * 2];                                              \
    uint8_t *const halfH = (uint8_t *) half;                            \
    ff_put ## RND ## mpeg4_qpel16_h_lowpass_ ## MMX(halfH, src, 16,     \
                                                    stride, 17);        \
    ff_ ## OPNAME ## mpeg4_qpel16_v_lowpass_ ## MMX(dst, halfH,         \
                                                    stride, 16);        \
}

QPEL_OP(put_,        _,        mmxext)
QPEL_OP(avg_,        _,        mmxext)
QPEL_OP(put_no_rnd_, _no_rnd_, mmxext)

#endif /* HAVE_YASM */

#define SET_QPEL_FUNCS(PFX, IDX, SIZE, CPU, PREFIX)                          \
do {                                                                         \
    c->PFX ## _pixels_tab[IDX][ 0] = PREFIX ## PFX ## SIZE ## _mc00_ ## CPU; \
    c->PFX ## _pixels_tab[IDX][ 1] = PREFIX ## PFX ## SIZE ## _mc10_ ## CPU; \
    c->PFX ## _pixels_tab[IDX][ 2] = PREFIX ## PFX ## SIZE ## _mc20_ ## CPU; \
    c->PFX ## _pixels_tab[IDX][ 3] = PREFIX ## PFX ## SIZE ## _mc30_ ## CPU; \
    c->PFX ## _pixels_tab[IDX][ 4] = PREFIX ## PFX ## SIZE ## _mc01_ ## CPU; \
    c->PFX ## _pixels_tab[IDX][ 5] = PREFIX ## PFX ## SIZE ## _mc11_ ## CPU; \
    c->PFX ## _pixels_tab[IDX][ 6] = PREFIX ## PFX ## SIZE ## _mc21_ ## CPU; \
    c->PFX ## _pixels_tab[IDX][ 7] = PREFIX ## PFX ## SIZE ## _mc31_ ## CPU; \
    c->PFX ## _pixels_tab[IDX][ 8] = PREFIX ## PFX ## SIZE ## _mc02_ ## CPU; \
    c->PFX ## _pixels_tab[IDX][ 9] = PREFIX ## PFX ## SIZE ## _mc12_ ## CPU; \
    c->PFX ## _pixels_tab[IDX][10] = PREFIX ## PFX ## SIZE ## _mc22_ ## CPU; \
    c->PFX ## _pixels_tab[IDX][11] = PREFIX ## PFX ## SIZE ## _mc32_ ## CPU; \
    c->PFX ## _pixels_tab[IDX][12] = PREFIX ## PFX ## SIZE ## _mc03_ ## CPU; \
    c->PFX ## _pixels_tab[IDX][13] = PREFIX ## PFX ## SIZE ## _mc13_ ## CPU; \
    c->PFX ## _pixels_tab[IDX][14] = PREFIX ## PFX ## SIZE ## _mc23_ ## CPU; \
    c->PFX ## _pixels_tab[IDX][15] = PREFIX ## PFX ## SIZE ## _mc33_ ## CPU; \
} while (0)

static av_cold void dsputil_init_mmx(DSPContext *c, AVCodecContext *avctx,
                                     int cpu_flags, unsigned high_bit_depth)
{
#if HAVE_MMX_INLINE
    c->put_pixels_clamped        = ff_put_pixels_clamped_mmx;
    c->put_signed_pixels_clamped = ff_put_signed_pixels_clamped_mmx;
    c->add_pixels_clamped        = ff_add_pixels_clamped_mmx;

    if (!high_bit_depth) {
        c->clear_block  = ff_clear_block_mmx;
        c->clear_blocks = ff_clear_blocks_mmx;
        c->draw_edges   = ff_draw_edges_mmx;
    }

#if CONFIG_VIDEODSP && (ARCH_X86_32 || !HAVE_YASM)
    c->gmc = ff_gmc_mmx;
#endif

    c->add_bytes = ff_add_bytes_mmx;
#endif /* HAVE_MMX_INLINE */

#if HAVE_MMX_EXTERNAL
    c->vector_clip_int32 = ff_vector_clip_int32_mmx;
#endif /* HAVE_MMX_EXTERNAL */
}

static av_cold void dsputil_init_mmxext(DSPContext *c, AVCodecContext *avctx,
                                        int cpu_flags, unsigned high_bit_depth)
{
#if HAVE_MMXEXT_INLINE
    if (!high_bit_depth && avctx->idct_algo == FF_IDCT_XVIDMMX && avctx->lowres == 0) {
        c->idct_put = ff_idct_xvid_mmxext_put;
        c->idct_add = ff_idct_xvid_mmxext_add;
        c->idct     = ff_idct_xvid_mmxext;
    }
#endif /* HAVE_MMXEXT_INLINE */

#if HAVE_MMXEXT_EXTERNAL
    SET_QPEL_FUNCS(avg_qpel,        0, 16, mmxext, );
    SET_QPEL_FUNCS(avg_qpel,        1,  8, mmxext, );

    SET_QPEL_FUNCS(put_qpel,        0, 16, mmxext, );
    SET_QPEL_FUNCS(put_qpel,        1,  8, mmxext, );
    SET_QPEL_FUNCS(put_no_rnd_qpel, 0, 16, mmxext, );
    SET_QPEL_FUNCS(put_no_rnd_qpel, 1,  8, mmxext, );

    /* slower than cmov version on AMD */
    if (!(cpu_flags & AV_CPU_FLAG_3DNOW))
        c->add_hfyu_median_prediction = ff_add_hfyu_median_prediction_mmxext;

    c->scalarproduct_int16          = ff_scalarproduct_int16_mmxext;
    c->scalarproduct_and_madd_int16 = ff_scalarproduct_and_madd_int16_mmxext;
#endif /* HAVE_MMXEXT_EXTERNAL */
}

static av_cold void dsputil_init_sse(DSPContext *c, AVCodecContext *avctx,
                                     int cpu_flags, unsigned high_bit_depth)
{
#if HAVE_SSE_INLINE
    c->vector_clipf = ff_vector_clipf_sse;

    /* XvMCCreateBlocks() may not allocate 16-byte aligned blocks */
    if (CONFIG_XVMC && avctx->hwaccel && avctx->hwaccel->decode_mb)
        return;

    if (!high_bit_depth) {
        c->clear_block  = ff_clear_block_sse;
        c->clear_blocks = ff_clear_blocks_sse;
    }
#endif /* HAVE_SSE_INLINE */

#if HAVE_YASM
#if HAVE_INLINE_ASM && CONFIG_VIDEODSP
    c->gmc = ff_gmc_sse;
#endif
#endif /* HAVE_YASM */
}

static av_cold void dsputil_init_sse2(DSPContext *c, AVCodecContext *avctx,
                                      int cpu_flags, unsigned high_bit_depth)
{
#if HAVE_SSE2_INLINE
    if (!high_bit_depth && avctx->idct_algo == FF_IDCT_XVIDMMX && avctx->lowres == 0) {
        c->idct_put              = ff_idct_xvid_sse2_put;
        c->idct_add              = ff_idct_xvid_sse2_add;
        c->idct                  = ff_idct_xvid_sse2;
        c->idct_permutation_type = FF_SSE2_IDCT_PERM;
    }
#endif /* HAVE_SSE2_INLINE */

#if HAVE_SSE2_EXTERNAL
    c->scalarproduct_int16          = ff_scalarproduct_int16_sse2;
    c->scalarproduct_and_madd_int16 = ff_scalarproduct_and_madd_int16_sse2;
    if (cpu_flags & AV_CPU_FLAG_ATOM) {
        c->vector_clip_int32 = ff_vector_clip_int32_int_sse2;
    } else {
        c->vector_clip_int32 = ff_vector_clip_int32_sse2;
    }
    c->bswap_buf = ff_bswap32_buf_sse2;
#endif /* HAVE_SSE2_EXTERNAL */
}

static av_cold void dsputil_init_ssse3(DSPContext *c, AVCodecContext *avctx,
                                       int cpu_flags, unsigned high_bit_depth)
{
#if HAVE_SSSE3_EXTERNAL
    c->add_hfyu_left_prediction = ff_add_hfyu_left_prediction_ssse3;
    if (cpu_flags & AV_CPU_FLAG_SSE4) // not really SSE4, just slow on Conroe
        c->add_hfyu_left_prediction = ff_add_hfyu_left_prediction_sse4;

    if (!(cpu_flags & (AV_CPU_FLAG_SSE42 | AV_CPU_FLAG_3DNOW))) // cachesplit
        c->scalarproduct_and_madd_int16 = ff_scalarproduct_and_madd_int16_ssse3;
    c->bswap_buf = ff_bswap32_buf_ssse3;
#endif /* HAVE_SSSE3_EXTERNAL */
}

static av_cold void dsputil_init_sse4(DSPContext *c, AVCodecContext *avctx,
                                      int cpu_flags, unsigned high_bit_depth)
{
#if HAVE_SSE4_EXTERNAL
    c->vector_clip_int32 = ff_vector_clip_int32_sse4;
#endif /* HAVE_SSE4_EXTERNAL */
}

av_cold void ff_dsputil_init_x86(DSPContext *c, AVCodecContext *avctx,
                                 unsigned high_bit_depth)
{
    int cpu_flags = av_get_cpu_flags();

#if HAVE_7REGS && HAVE_INLINE_ASM
    if (HAVE_MMX && cpu_flags & AV_CPU_FLAG_CMOV)
        c->add_hfyu_median_prediction = ff_add_hfyu_median_prediction_cmov;
#endif

    if (X86_MMX(cpu_flags)) {
#if HAVE_INLINE_ASM
        const int idct_algo = avctx->idct_algo;

        if (avctx->lowres == 0 && !high_bit_depth) {
            if (idct_algo == FF_IDCT_AUTO || idct_algo == FF_IDCT_SIMPLEMMX) {
                c->idct_put              = ff_simple_idct_put_mmx;
                c->idct_add              = ff_simple_idct_add_mmx;
                c->idct                  = ff_simple_idct_mmx;
                c->idct_permutation_type = FF_SIMPLE_IDCT_PERM;
            } else if (idct_algo == FF_IDCT_XVIDMMX) {
                c->idct_put              = ff_idct_xvid_mmx_put;
                c->idct_add              = ff_idct_xvid_mmx_add;
                c->idct                  = ff_idct_xvid_mmx;
            }
        }
#endif /* HAVE_INLINE_ASM */

        dsputil_init_mmx(c, avctx, cpu_flags, high_bit_depth);
    }

    if (X86_MMXEXT(cpu_flags))
        dsputil_init_mmxext(c, avctx, cpu_flags, high_bit_depth);

    if (X86_SSE(cpu_flags))
        dsputil_init_sse(c, avctx, cpu_flags, high_bit_depth);

    if (X86_SSE2(cpu_flags))
        dsputil_init_sse2(c, avctx, cpu_flags, high_bit_depth);

    if (EXTERNAL_SSSE3(cpu_flags))
        dsputil_init_ssse3(c, avctx, cpu_flags, high_bit_depth);

    if (EXTERNAL_SSE4(cpu_flags))
        dsputil_init_sse4(c, avctx, cpu_flags, high_bit_depth);

    if (CONFIG_ENCODERS)
        ff_dsputilenc_init_mmx(c, avctx, high_bit_depth);
}
