/*
 * quarterpel DSP functions
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

#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/pixels.h"
#include "libavcodec/qpeldsp.h"
#include "fpel.h"

void ff_put_pixels8_l2_mmxext(uint8_t *dst,
                              const uint8_t *src1, const uint8_t *src2,
                              int dstStride, int src1Stride, int h);
void ff_put_no_rnd_pixels8_l2_mmxext(uint8_t *dst,
                                     const uint8_t *src1, const uint8_t *src2,
                                     int dstStride, int src1Stride, int h);
void ff_avg_pixels8_l2_mmxext(uint8_t *dst,
                              const uint8_t *src1, const uint8_t *src2,
                              int dstStride, int src1Stride, int h);
void ff_put_pixels16_l2_mmxext(uint8_t *dst,
                               const uint8_t *src1, const uint8_t *src2,
                               int dstStride, int src1Stride, int h);
void ff_avg_pixels16_l2_mmxext(uint8_t *dst,
                               const uint8_t *src1, const uint8_t *src2,
                               int dstStride, int src1Stride, int h);
void ff_put_no_rnd_pixels16_l2_mmxext(uint8_t *dst,
                                      const uint8_t *src1, const uint8_t *src2,
                                      int dstStride, int src1Stride, int h);
void ff_put_mpeg4_qpel16_h_lowpass_mmxext(uint8_t *dst, const uint8_t *src,
                                          int dstStride, int srcStride, int h);
void ff_avg_mpeg4_qpel16_h_lowpass_mmxext(uint8_t *dst, const uint8_t *src,
                                          int dstStride, int srcStride, int h);
void ff_put_no_rnd_mpeg4_qpel16_h_lowpass_mmxext(uint8_t *dst,
                                                 const uint8_t *src,
                                                 int dstStride, int srcStride,
                                                 int h);
void ff_put_mpeg4_qpel8_h_lowpass_mmxext(uint8_t *dst, const uint8_t *src,
                                         int dstStride, int srcStride, int h);
void ff_avg_mpeg4_qpel8_h_lowpass_mmxext(uint8_t *dst, const uint8_t *src,
                                         int dstStride, int srcStride, int h);
void ff_put_no_rnd_mpeg4_qpel8_h_lowpass_mmxext(uint8_t *dst,
                                                const uint8_t *src,
                                                int dstStride, int srcStride,
                                                int h);
void ff_put_mpeg4_qpel16_v_lowpass_mmxext(uint8_t *dst, const uint8_t *src,
                                          int dstStride, int srcStride);
void ff_avg_mpeg4_qpel16_v_lowpass_mmxext(uint8_t *dst, const uint8_t *src,
                                          int dstStride, int srcStride);
void ff_put_no_rnd_mpeg4_qpel16_v_lowpass_mmxext(uint8_t *dst,
                                                 const uint8_t *src,
                                                 int dstStride, int srcStride);
void ff_put_mpeg4_qpel8_v_lowpass_mmxext(uint8_t *dst, const uint8_t *src,
                                         int dstStride, int srcStride);
void ff_avg_mpeg4_qpel8_v_lowpass_mmxext(uint8_t *dst, const uint8_t *src,
                                         int dstStride, int srcStride);
void ff_put_no_rnd_mpeg4_qpel8_v_lowpass_mmxext(uint8_t *dst,
                                                const uint8_t *src,
                                                int dstStride, int srcStride);
#define ff_put_no_rnd_pixels16_mmxext ff_put_pixels16_mmxext
#define ff_put_no_rnd_pixels8_mmxext ff_put_pixels8_mmxext

#if HAVE_YASM

CALL_2X_PIXELS(ff_avg_pixels16_mmxext, ff_avg_pixels8_mmxext, 8)
CALL_2X_PIXELS(ff_put_pixels16_mmxext, ff_put_pixels8_mmxext, 8)

#define QPEL_OP(OPNAME, RND, MMX)                                       \
static void OPNAME ## qpel8_mc00_ ## MMX(uint8_t *dst,                  \
                                         const uint8_t *src,            \
                                         ptrdiff_t stride)              \
{                                                                       \
    ff_ ## OPNAME ## pixels8_ ## MMX(dst, src, stride, 8);              \
}                                                                       \
                                                                        \
static void OPNAME ## qpel8_mc10_ ## MMX(uint8_t *dst,                  \
                                         const uint8_t *src,            \
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
static void OPNAME ## qpel8_mc20_ ## MMX(uint8_t *dst,                  \
                                         const uint8_t *src,            \
                                         ptrdiff_t stride)              \
{                                                                       \
    ff_ ## OPNAME ## mpeg4_qpel8_h_lowpass_ ## MMX(dst, src, stride,    \
                                                   stride, 8);          \
}                                                                       \
                                                                        \
static void OPNAME ## qpel8_mc30_ ## MMX(uint8_t *dst,                  \
                                         const uint8_t *src,            \
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
static void OPNAME ## qpel8_mc01_ ## MMX(uint8_t *dst,                  \
                                         const uint8_t *src,            \
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
static void OPNAME ## qpel8_mc02_ ## MMX(uint8_t *dst,                  \
                                         const uint8_t *src,            \
                                         ptrdiff_t stride)              \
{                                                                       \
    ff_ ## OPNAME ## mpeg4_qpel8_v_lowpass_ ## MMX(dst, src,            \
                                                   stride, stride);     \
}                                                                       \
                                                                        \
static void OPNAME ## qpel8_mc03_ ## MMX(uint8_t *dst,                  \
                                         const uint8_t *src,            \
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
static void OPNAME ## qpel8_mc11_ ## MMX(uint8_t *dst,                  \
                                         const uint8_t *src,            \
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
static void OPNAME ## qpel8_mc31_ ## MMX(uint8_t *dst,                  \
                                         const uint8_t *src,            \
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
static void OPNAME ## qpel8_mc13_ ## MMX(uint8_t *dst,                  \
                                         const uint8_t *src,            \
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
static void OPNAME ## qpel8_mc33_ ## MMX(uint8_t *dst,                  \
                                         const uint8_t *src,            \
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
static void OPNAME ## qpel8_mc21_ ## MMX(uint8_t *dst,                  \
                                         const uint8_t *src,            \
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
static void OPNAME ## qpel8_mc23_ ## MMX(uint8_t *dst,                  \
                                         const uint8_t *src,            \
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
static void OPNAME ## qpel8_mc12_ ## MMX(uint8_t *dst,                  \
                                         const uint8_t *src,            \
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
static void OPNAME ## qpel8_mc32_ ## MMX(uint8_t *dst,                  \
                                         const uint8_t *src,            \
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
static void OPNAME ## qpel8_mc22_ ## MMX(uint8_t *dst,                  \
                                         const uint8_t *src,            \
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
static void OPNAME ## qpel16_mc00_ ## MMX(uint8_t *dst,                 \
                                          const uint8_t *src,           \
                                          ptrdiff_t stride)             \
{                                                                       \
    ff_ ## OPNAME ## pixels16_ ## MMX(dst, src, stride, 16);            \
}                                                                       \
                                                                        \
static void OPNAME ## qpel16_mc10_ ## MMX(uint8_t *dst,                 \
                                          const uint8_t *src,           \
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
static void OPNAME ## qpel16_mc20_ ## MMX(uint8_t *dst,                 \
                                          const uint8_t *src,           \
                                          ptrdiff_t stride)             \
{                                                                       \
    ff_ ## OPNAME ## mpeg4_qpel16_h_lowpass_ ## MMX(dst, src,           \
                                                    stride, stride, 16);\
}                                                                       \
                                                                        \
static void OPNAME ## qpel16_mc30_ ## MMX(uint8_t *dst,                 \
                                          const uint8_t *src,           \
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
static void OPNAME ## qpel16_mc01_ ## MMX(uint8_t *dst,                 \
                                          const uint8_t *src,           \
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
static void OPNAME ## qpel16_mc02_ ## MMX(uint8_t *dst,                 \
                                          const uint8_t *src,           \
                                          ptrdiff_t stride)             \
{                                                                       \
    ff_ ## OPNAME ## mpeg4_qpel16_v_lowpass_ ## MMX(dst, src,           \
                                                    stride, stride);    \
}                                                                       \
                                                                        \
static void OPNAME ## qpel16_mc03_ ## MMX(uint8_t *dst,                 \
                                          const uint8_t *src,           \
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
static void OPNAME ## qpel16_mc11_ ## MMX(uint8_t *dst,                 \
                                          const uint8_t *src,           \
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
static void OPNAME ## qpel16_mc31_ ## MMX(uint8_t *dst,                 \
                                          const uint8_t *src,           \
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
static void OPNAME ## qpel16_mc13_ ## MMX(uint8_t *dst,                 \
                                          const uint8_t *src,           \
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
static void OPNAME ## qpel16_mc33_ ## MMX(uint8_t *dst,                 \
                                          const uint8_t *src,           \
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
static void OPNAME ## qpel16_mc21_ ## MMX(uint8_t *dst,                 \
                                          const uint8_t *src,           \
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
static void OPNAME ## qpel16_mc23_ ## MMX(uint8_t *dst,                 \
                                          const uint8_t *src,           \
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
static void OPNAME ## qpel16_mc12_ ## MMX(uint8_t *dst,                 \
                                          const uint8_t *src,           \
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
static void OPNAME ## qpel16_mc32_ ## MMX(uint8_t *dst,                 \
                                          const uint8_t *src,           \
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
static void OPNAME ## qpel16_mc22_ ## MMX(uint8_t *dst,                 \
                                          const uint8_t *src,           \
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

av_cold void ff_qpeldsp_init_x86(QpelDSPContext *c)
{
    int cpu_flags = av_get_cpu_flags();

    if (X86_MMXEXT(cpu_flags)) {
#if HAVE_MMXEXT_EXTERNAL
        SET_QPEL_FUNCS(avg_qpel,        0, 16, mmxext, );
        SET_QPEL_FUNCS(avg_qpel,        1,  8, mmxext, );

        SET_QPEL_FUNCS(put_qpel,        0, 16, mmxext, );
        SET_QPEL_FUNCS(put_qpel,        1,  8, mmxext, );
        SET_QPEL_FUNCS(put_no_rnd_qpel, 0, 16, mmxext, );
        SET_QPEL_FUNCS(put_no_rnd_qpel, 1,  8, mmxext, );
#endif /* HAVE_MMXEXT_EXTERNAL */
    }
}
