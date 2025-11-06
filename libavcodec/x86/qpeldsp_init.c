/*
 * quarterpel DSP functions
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

#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/mem_internal.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/qpeldsp.h"
#include "fpel.h"
#include "qpel.h"

void ff_put_pixels8x9_l2_mmxext(uint8_t *dst,
                                const uint8_t *src1, const uint8_t *src2,
                                ptrdiff_t dstStride, ptrdiff_t src1Stride);
void ff_put_pixels16x17_l2_mmxext(uint8_t *dst,
                                  const uint8_t *src1, const uint8_t *src2,
                                  ptrdiff_t dstStride, ptrdiff_t src1Stride);
void ff_put_no_rnd_pixels8x8_l2_mmxext(uint8_t *dst,
                                       const uint8_t *src1, const uint8_t *src2,
                                       ptrdiff_t dstStride, ptrdiff_t src1Stride);
void ff_put_no_rnd_pixels8x9_l2_mmxext(uint8_t *dst,
                                       const uint8_t *src1, const uint8_t *src2,
                                       ptrdiff_t dstStride, ptrdiff_t src1Stride);
void ff_put_no_rnd_pixels16x16_l2_mmxext(uint8_t *dst,
                                         const uint8_t *src1, const uint8_t *src2,
                                         ptrdiff_t dstStride, ptrdiff_t src1Stride);
void ff_put_no_rnd_pixels16x17_l2_mmxext(uint8_t *dst,
                                         const uint8_t *src1, const uint8_t *src2,
                                         ptrdiff_t dstStride, ptrdiff_t src1Stride);

#define QPEL_H(OPNAME, RND, SIZE, UNUSED1, XMM, UNUSED2, UNUSED3, L2)                   \
void ff_ ## OPNAME ## _mpeg4_qpel ## SIZE ## _h_lowpass_ ## XMM (uint8_t *dst,          \
                                                                 const uint8_t *src,    \
                                                                 ptrdiff_t dstStride,   \
                                                                 ptrdiff_t srcStride,   \
                                                                 int h);                \
static void OPNAME ## _qpel ## SIZE ## _mc10_ ## XMM(uint8_t *dst,                      \
                                                     const uint8_t *src,                \
                                                     ptrdiff_t stride)                  \
{                                                                                       \
    DECLARE_ALIGNED(SIZE, uint8_t, half)[SIZE*SIZE];                                    \
    ff_put_ ## RND ## mpeg4_qpel ## SIZE ## _h_lowpass_ ## XMM(half, src, SIZE,         \
                                                               stride, SIZE);           \
    ff_ ## OPNAME ## _pixels ## SIZE ## x ## SIZE ## _l2_ ## L2(dst, src, half,         \
                                                                stride, stride);        \
}                                                                                       \
                                                                                        \
static void OPNAME ## _qpel ## SIZE ## _mc20_ ## XMM(uint8_t *dst,                      \
                                                     const uint8_t *src,                \
                                                     ptrdiff_t stride)                  \
{                                                                                       \
    ff_ ## OPNAME ## _mpeg4_qpel ## SIZE ## _h_lowpass_ ## XMM(dst, src, stride,        \
                                                               stride, SIZE);           \
}                                                                                       \
                                                                                        \
static void OPNAME ## _qpel ## SIZE ## _mc30_ ## XMM(uint8_t *dst,                      \
                                                     const uint8_t *src,                \
                                                     ptrdiff_t stride)                  \
{                                                                                       \
    DECLARE_ALIGNED(SIZE, uint8_t, half)[SIZE*SIZE];                                    \
    ff_put_ ## RND ## mpeg4_qpel ## SIZE ## _h_lowpass_ ## XMM(half, src, SIZE,         \
                                                               stride, SIZE);           \
    ff_ ## OPNAME ## _pixels ## SIZE ## x ## SIZE ## _l2_ ## L2(dst, src + 1, half,     \
                                                                stride, stride);        \
}

#define QPEL_V(OPNAME, RND, SIZE, UNUSED1, UNUSED2, XMM, UNUSED3, L2)                   \
void ff_ ## OPNAME ## _mpeg4_qpel ## SIZE ## _v_lowpass_ ## XMM (uint8_t *dst,          \
                                                                 const uint8_t *src,    \
                                                                 ptrdiff_t dstStride,   \
                                                                 ptrdiff_t srcStride);  \
static void OPNAME ## _qpel ## SIZE ## _mc01_ ## XMM(uint8_t *dst,                      \
                                                     const uint8_t *src,                \
                                                     ptrdiff_t stride)                  \
{                                                                                       \
    DECLARE_ALIGNED(SIZE, uint8_t, half)[SIZE*SIZE];                                    \
    ff_put_ ## RND ## mpeg4_qpel ## SIZE ## _v_lowpass_ ## XMM(half, src,               \
                                                               SIZE, stride);           \
    ff_ ## OPNAME ## _pixels ## SIZE ## x ## SIZE ## _l2_ ## L2(dst, src, half,         \
                                                                stride, stride);        \
}                                                                                       \
                                                                                        \
static void OPNAME ## _qpel ## SIZE ## _mc02_ ## XMM(uint8_t *dst,                      \
                                                     const uint8_t *src,                \
                                                     ptrdiff_t stride)                  \
{                                                                                       \
    ff_ ## OPNAME ## _mpeg4_qpel ## SIZE ## _v_lowpass_ ## XMM(dst, src,                \
                                                               stride, stride);         \
}                                                                                       \
                                                                                        \
static void OPNAME ## _qpel ## SIZE ## _mc03_ ## XMM(uint8_t *dst,                      \
                                                     const uint8_t *src,                \
                                                     ptrdiff_t stride)                  \
{                                                                                       \
    DECLARE_ALIGNED(SIZE, uint8_t, half)[SIZE*SIZE];                                    \
    ff_put_ ## RND ## mpeg4_qpel ## SIZE ## _v_lowpass_ ## XMM(half, src,               \
                                                               SIZE, stride);           \
    ff_ ## OPNAME ## _pixels ## SIZE ## x ## SIZE ## _l2_ ## L2(dst, src + stride,      \
                                                                half, stride, stride);  \
}

#define QPEL_HV(OPNAME, RND, SIZE, SIZEP1, HXMM, VXMM, HVXMM, L2)                       \
static void OPNAME ## _qpel ## SIZE ## _mc11_ ## HVXMM(uint8_t *dst,                    \
                                                       const uint8_t *src,              \
                                                       ptrdiff_t stride)                \
{                                                                                       \
    DECLARE_ALIGNED(SIZE, uint8_t, half)[(SIZE + SIZEP1)*SIZE];                         \
    uint8_t *const halfH  = half + SIZE*SIZE;                                           \
    uint8_t *const halfHV = half;                                                       \
    ff_put_ ## RND ## mpeg4_qpel ## SIZE ## _h_lowpass_ ## HXMM(halfH, src, SIZE,       \
                                                                stride, SIZEP1);        \
    ff_put_ ## RND ## pixels ## SIZE ## x ## SIZEP1 ## _l2_ ## L2(halfH, src, halfH,    \
                                                                  SIZE, stride);        \
    ff_put_ ## RND ## mpeg4_qpel ## SIZE ## _v_lowpass_ ## VXMM(halfHV, halfH,          \
                                                                SIZE, SIZE);            \
    ff_ ## OPNAME ## _pixels ## SIZE ## x ## SIZE ## _l2_ ## L2(dst, halfH, halfHV,     \
                                                                stride, SIZE);          \
}                                                                                       \
                                                                                        \
static void OPNAME ## _qpel ## SIZE ## _mc31_ ## HVXMM(uint8_t *dst,                    \
                                                       const uint8_t *src,              \
                                                       ptrdiff_t stride)                \
{                                                                                       \
    DECLARE_ALIGNED(SIZE, uint8_t, half)[(SIZE + SIZEP1)*SIZE];                         \
    uint8_t *const halfH  = half + SIZE*SIZE;                                           \
    uint8_t *const halfHV = half;                                                       \
    ff_put_ ## RND ## mpeg4_qpel ## SIZE ## _h_lowpass_ ## HXMM(halfH, src, SIZE,       \
                                                                stride, SIZEP1);        \
    ff_put_ ## RND ## pixels ## SIZE ## x ## SIZEP1 ## _l2_ ## L2(halfH, src + 1,       \
                                                                  halfH, SIZE, stride); \
    ff_put_ ## RND ## mpeg4_qpel ## SIZE ## _v_lowpass_ ## VXMM(halfHV, halfH,          \
                                                                SIZE, SIZE);            \
    ff_ ## OPNAME ## _pixels ## SIZE ## x ## SIZE ## _l2_ ## L2(dst, halfH, halfHV,     \
                                                                stride, SIZE);          \
}                                                                                       \
                                                                                        \
static void OPNAME ## _qpel ## SIZE ## _mc13_ ## HVXMM(uint8_t *dst,                    \
                                                       const uint8_t *src,              \
                                                       ptrdiff_t stride)                \
{                                                                                       \
    DECLARE_ALIGNED(SIZE, uint8_t, half)[(SIZE + SIZEP1)*SIZE];                         \
    uint8_t *const halfH  = half + SIZE*SIZE;                                           \
    uint8_t *const halfHV = half;                                                       \
    ff_put_ ## RND ## mpeg4_qpel ## SIZE ## _h_lowpass_ ## HXMM(halfH, src, SIZE,       \
                                                                stride, SIZEP1);        \
    ff_put_ ## RND ## pixels ## SIZE ## x ## SIZEP1 ## _l2_ ## L2(halfH, src, halfH,    \
                                                                  SIZE, stride);        \
    ff_put_ ## RND ## mpeg4_qpel ## SIZE ## _v_lowpass_ ## VXMM(halfHV, halfH,          \
                                                                SIZE, SIZE);            \
    ff_ ## OPNAME ## _pixels ## SIZE ## x ## SIZE ## _l2_ ## L2(dst, halfH + SIZE,      \
                                                                halfHV, stride, SIZE);  \
}                                                                                       \
                                                                                        \
static void OPNAME ## _qpel ## SIZE ## _mc33_ ## HVXMM(uint8_t *dst,                    \
                                                       const uint8_t *src,              \
                                                       ptrdiff_t stride)                \
{                                                                                       \
    DECLARE_ALIGNED(SIZE, uint8_t, half)[(SIZE + SIZEP1)*SIZE];                         \
    uint8_t *const halfH  = half + SIZE*SIZE;                                           \
    uint8_t *const halfHV = half;                                                       \
    ff_put_ ## RND ## mpeg4_qpel ## SIZE ## _h_lowpass_ ## HXMM(halfH, src, SIZE,       \
                                                                stride, SIZEP1);        \
    ff_put_ ## RND ## pixels ## SIZE ## x ## SIZEP1 ## _l2_ ## L2(halfH, src + 1, halfH,\
                                                                  SIZE, stride);        \
    ff_put_ ## RND ## mpeg4_qpel ## SIZE ## _v_lowpass_ ## VXMM(halfHV, halfH,          \
                                                                SIZE, SIZE);            \
    ff_ ## OPNAME ## _pixels ## SIZE ## x ## SIZE ## _l2_ ## L2(dst, halfH + SIZE,      \
                                                                halfHV, stride, SIZE);  \
}                                                                                       \
                                                                                        \
static void OPNAME ## _qpel ## SIZE ## _mc21_ ## HVXMM(uint8_t *dst,                    \
                                                       const uint8_t *src,              \
                                                       ptrdiff_t stride)                \
{                                                                                       \
    DECLARE_ALIGNED(SIZE, uint8_t, half)[(SIZE + SIZEP1)*SIZE];                         \
    uint8_t *const halfH  = half + SIZE*SIZE;                                           \
    uint8_t *const halfHV = half;                                                       \
    ff_put_ ## RND ## mpeg4_qpel ## SIZE ## _h_lowpass_ ## HXMM(halfH, src, SIZE,       \
                                                                stride, SIZEP1);        \
    ff_put_ ## RND ## mpeg4_qpel ## SIZE ## _v_lowpass_ ## VXMM(halfHV, halfH,          \
                                                                SIZE, SIZE);            \
    ff_ ## OPNAME ## _pixels ## SIZE ## x ## SIZE ## _l2_ ## L2(dst, halfH, halfHV,     \
                                                                stride, SIZE);          \
}                                                                                       \
                                                                                        \
static void OPNAME ## _qpel ## SIZE ## _mc23_ ## HVXMM(uint8_t *dst,                    \
                                                       const uint8_t *src,              \
                                                       ptrdiff_t stride)                \
{                                                                                       \
    DECLARE_ALIGNED(SIZE, uint8_t, half)[(SIZE + SIZEP1)*SIZE];                         \
    uint8_t *const halfH  = half + SIZE*SIZE;                                           \
    uint8_t *const halfHV = half;                                                       \
    ff_put_ ## RND ## mpeg4_qpel ## SIZE ## _h_lowpass_ ## HXMM(halfH, src, SIZE,       \
                                                                stride, SIZEP1);        \
    ff_put_ ## RND ## mpeg4_qpel ## SIZE ## _v_lowpass_ ## VXMM(halfHV, halfH,          \
                                                                SIZE, SIZE);            \
    ff_ ## OPNAME ## _pixels ## SIZE ## x ## SIZE ## _l2_ ## L2(dst, halfH + SIZE,      \
                                                                halfHV, stride, SIZE);  \
}                                                                                       \
                                                                                        \
static void OPNAME ## _qpel ## SIZE ## _mc12_ ## HVXMM(uint8_t *dst,                    \
                                                       const uint8_t *src,              \
                                                       ptrdiff_t stride)                \
{                                                                                       \
    DECLARE_ALIGNED(SIZE, uint8_t, halfH)[SIZEP1*SIZE];                                 \
    ff_put_ ## RND ## mpeg4_qpel ## SIZE ## _h_lowpass_ ## HXMM(halfH, src, SIZE,       \
                                                                stride, SIZEP1);        \
    ff_put_ ## RND ## pixels ## SIZE ## x ## SIZEP1 ## _l2_ ## L2(halfH, src, halfH,    \
                                                                  SIZE, stride);        \
    ff_ ## OPNAME ## _mpeg4_qpel ## SIZE ## _v_lowpass_ ## VXMM(dst, halfH,             \
                                                                stride, SIZE);          \
}                                                                                       \
                                                                                        \
static void OPNAME ## _qpel ## SIZE ## _mc32_ ## HVXMM(uint8_t *dst,                    \
                                                       const uint8_t *src,              \
                                                       ptrdiff_t stride)                \
{                                                                                       \
    DECLARE_ALIGNED(SIZE, uint8_t, halfH)[SIZEP1*SIZE];                                 \
    ff_put_ ## RND ## mpeg4_qpel ## SIZE ## _h_lowpass_ ## HXMM(halfH, src, SIZE,       \
                                                                stride, SIZEP1);        \
    ff_put_ ## RND ## pixels ## SIZE ## x ## SIZEP1 ## _l2_ ## L2(halfH, src + 1, halfH,\
                                                                  SIZE, stride);        \
    ff_ ## OPNAME ## _mpeg4_qpel ## SIZE ## _v_lowpass_ ## VXMM(dst, halfH,             \
                                                                stride, SIZE);          \
}                                                                                       \
                                                                                        \
static void OPNAME ## _qpel ## SIZE ## _mc22_ ## HVXMM(uint8_t *dst,                    \
                                                       const uint8_t *src,              \
                                                       ptrdiff_t stride)                \
{                                                                                       \
    DECLARE_ALIGNED(SIZE, uint8_t, halfH)[SIZEP1*SIZE];                                 \
    ff_put_ ## RND ## mpeg4_qpel ## SIZE ## _h_lowpass_ ## HXMM(halfH, src, SIZE,       \
                                                                stride, SIZEP1);        \
    ff_ ## OPNAME ## _mpeg4_qpel ## SIZE ## _v_lowpass_ ## VXMM(dst, halfH,             \
                                                                stride, SIZE);          \
}

#define QPEL3(MACRO, SIZE, SIZEP1, HXMM, VXMM, HVXMM, L2)       \
MACRO(put,,                SIZE, SIZEP1, HXMM, VXMM, HVXMM, L2) \
MACRO(avg,,                SIZE, SIZEP1, HXMM, VXMM, HVXMM, L2) \
MACRO(put_no_rnd, no_rnd_, SIZE, SIZEP1, HXMM, VXMM, HVXMM, L2)

QPEL3(QPEL_H,   8,  9, mmxext, mmxext, mmxext, mmxext)
QPEL3(QPEL_V,   8,  9, mmxext, mmxext, mmxext, mmxext)
QPEL3(QPEL_HV,  8,  9, mmxext, mmxext, mmxext, mmxext)

QPEL3(QPEL_H,  16, 17, mmxext, mmxext, mmxext, mmxext)
QPEL3(QPEL_V,  16, 17, mmxext, mmxext, mmxext, mmxext)
QPEL3(QPEL_HV, 16, 17, mmxext, mmxext, mmxext, mmxext)

#define SET_QPEL_FUNCS(PFX, IDX, SIZE, CPU, PREFIX)                          \
do {                                                                         \
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
        c->avg_qpel_pixels_tab[1][0] = ff_avg_pixels8x8_mmxext;
        SET_QPEL_FUNCS(avg_qpel,        1,  8, mmxext, );

        SET_QPEL_FUNCS(put_qpel,        0, 16, mmxext, );
        SET_QPEL_FUNCS(put_qpel,        1,  8, mmxext, );
        SET_QPEL_FUNCS(put_no_rnd_qpel, 0, 16, mmxext, );
        SET_QPEL_FUNCS(put_no_rnd_qpel, 1,  8, mmxext, );
#endif /* HAVE_MMXEXT_EXTERNAL */
    }
#if HAVE_SSE2_EXTERNAL
    if (EXTERNAL_SSE2(cpu_flags)) {
        c->put_no_rnd_qpel_pixels_tab[0][0] =
        c->put_qpel_pixels_tab[0][0] = ff_put_pixels16x16_sse2;
        c->put_no_rnd_qpel_pixels_tab[1][0] =
        c->put_qpel_pixels_tab[1][0] = ff_put_pixels8x8_sse2;
        c->avg_qpel_pixels_tab[0][0] = ff_avg_pixels16x16_sse2;
    }
#endif
}
