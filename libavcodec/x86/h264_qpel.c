/*
 * Copyright (c) 2004-2005 Michael Niedermayer, Loren Merritt
 * Copyright (c) 2011 Daniel Kang
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

#include "libavutil/attributes.h"
#include "libavutil/avassert.h"
#include "libavutil/cpu.h"
#include "libavutil/mem_internal.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/h264qpel.h"
#include "fpel.h"
#include "qpel.h"

#if HAVE_X86ASM
void ff_avg_pixels4_mmxext(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);
void ff_put_pixels4x4_l2_mmxext(uint8_t *dst, const uint8_t *src1, const uint8_t *src2,
                                ptrdiff_t stride);
void ff_avg_pixels4x4_l2_mmxext(uint8_t *dst, const uint8_t *src1, const uint8_t *src2,
                                ptrdiff_t stride);
#define ff_put_pixels4x4_l2_mmxext(dst, src1, src2, dststride, src1stride) \
    ff_put_pixels4x4_l2_mmxext((dst), (src1), (src2), (dststride))
#define ff_avg_pixels4x4_l2_mmxext(dst, src1, src2, dststride, src1stride) \
    ff_avg_pixels4x4_l2_mmxext((dst), (src1), (src2), (dststride))
#define ff_put_pixels8x8_l2_sse2  ff_put_pixels8x8_l2_mmxext
#define ff_avg_pixels8x8_l2_sse2  ff_avg_pixels8x8_l2_mmxext

#define DEF_QPEL(OPNAME)\
void ff_ ## OPNAME ## _h264_qpel4_h_lowpass_mmxext(uint8_t *dst, const uint8_t *src, ptrdiff_t dstStride, ptrdiff_t srcStride);\
void ff_ ## OPNAME ## _h264_qpel8_h_lowpass_ssse3(uint8_t *dst, const uint8_t *src, ptrdiff_t dstStride, ptrdiff_t srcStride);\
void ff_ ## OPNAME ## _h264_qpel4_h_lowpass_l2_mmxext(uint8_t *dst, const uint8_t *src, const uint8_t *src2, ptrdiff_t dstStride, ptrdiff_t src2Stride);\
void ff_ ## OPNAME ## _h264_qpel8_h_lowpass_l2_sse2(uint8_t *dst, const uint8_t *src, const uint8_t *src2, ptrdiff_t dstStride, ptrdiff_t src2Stride);\
void ff_ ## OPNAME ## _h264_qpel16_h_lowpass_l2_sse2(uint8_t *dst, const uint8_t *src, const uint8_t *src2, ptrdiff_t dstStride, ptrdiff_t src2Stride);\
void ff_ ## OPNAME ## _h264_qpel8_h_lowpass_l2_ssse3(uint8_t *dst, const uint8_t *src, const uint8_t *src2, ptrdiff_t dstStride, ptrdiff_t src2Stride);\
void ff_ ## OPNAME ## _h264_qpel4_v_lowpass_mmxext(uint8_t *dst, const uint8_t *src, ptrdiff_t dstStride, ptrdiff_t srcStride);\
void ff_ ## OPNAME ## _h264_qpel8or16_v_lowpass_sse2(uint8_t *dst, const uint8_t *src, ptrdiff_t dstStride, ptrdiff_t srcStride, int h);\
void ff_ ## OPNAME ## _h264_qpel4_hv_lowpass_h_mmxext(int16_t *tmp, uint8_t *dst, ptrdiff_t dstStride);\
void ff_ ## OPNAME ## _h264_qpel8or16_hv1_lowpass_op_sse2(const uint8_t *src, int16_t *tmp, ptrdiff_t srcStride, int size);\
void ff_ ## OPNAME ## _h264_qpel8_hv2_lowpass_sse2(uint8_t *dst, int16_t *tmp, ptrdiff_t dstStride);\
void ff_ ## OPNAME ## _h264_qpel16_hv2_lowpass_sse2(uint8_t *dst, int16_t *tmp, ptrdiff_t dstStride);\
void ff_ ## OPNAME ## _h264_qpel8_hv2_lowpass_ssse3(uint8_t *dst, int16_t *tmp, ptrdiff_t dstStride);\
void ff_ ## OPNAME ## _h264_qpel16_hv2_lowpass_ssse3(uint8_t *dst, int16_t *tmp, ptrdiff_t dstStride);\
void ff_ ## OPNAME ## _pixels4_l2_shift5_mmxext(uint8_t *dst, const int16_t *src16, const uint8_t *src8, ptrdiff_t dstStride);\
void ff_ ## OPNAME ## _pixels8_l2_shift5_sse2(uint8_t *dst, const int16_t *src16, const uint8_t *src8, ptrdiff_t dstStride);\
void ff_ ## OPNAME ## _pixels16_l2_shift5_sse2(uint8_t *dst, const int16_t *src16, const uint8_t *src8, ptrdiff_t dstStride);\

void ff_put_h264_qpel4_hv_lowpass_v_mmxext(const uint8_t *src, int16_t *tmp, ptrdiff_t srcStride);

DEF_QPEL(avg)
DEF_QPEL(put)

#define QPEL_H264(OPNAME, MMX)\
static av_always_inline void OPNAME ## h264_qpel4_hv_lowpass_ ## MMX(uint8_t *dst, int16_t *tmp, const uint8_t *src, ptrdiff_t dstStride, ptrdiff_t srcStride)\
{\
    src -= 2*srcStride+2;\
    ff_put_h264_qpel4_hv_lowpass_v_mmxext(src, tmp, srcStride);\
    ff_ ## OPNAME ## h264_qpel4_hv_lowpass_h_mmxext(tmp, dst, dstStride);\
}\

#define QPEL_H264_H16(OPNAME, EXT) \
static av_always_inline void ff_ ## OPNAME ## h264_qpel16_h_lowpass_l2_ ## EXT(uint8_t *dst, const uint8_t *src, const uint8_t *src2, ptrdiff_t dstStride, ptrdiff_t src2Stride)\
{\
    ff_ ## OPNAME ## h264_qpel8_h_lowpass_l2_ ## EXT(dst  , src  , src2  , dstStride, src2Stride);\
    ff_ ## OPNAME ## h264_qpel8_h_lowpass_l2_ ## EXT(dst+8, src+8, src2+8, dstStride, src2Stride);\
    src += 8*dstStride;\
    dst += 8*dstStride;\
    src2 += 8*src2Stride;\
    ff_ ## OPNAME ## h264_qpel8_h_lowpass_l2_ ## EXT(dst  , src  , src2  , dstStride, src2Stride);\
    ff_ ## OPNAME ## h264_qpel8_h_lowpass_l2_ ## EXT(dst+8, src+8, src2+8, dstStride, src2Stride);\
}\


#if ARCH_X86_64
#define QPEL_H264_H16_XMM(OPNAME, MMX)\

void ff_avg_h264_qpel16_h_lowpass_l2_ssse3(uint8_t *dst, const uint8_t *src, const uint8_t *src2, ptrdiff_t dstStride, ptrdiff_t src2Stride);
void ff_put_h264_qpel16_h_lowpass_l2_ssse3(uint8_t *dst, const uint8_t *src, const uint8_t *src2, ptrdiff_t dstStride, ptrdiff_t src2Stride);

#else // ARCH_X86_64
#define QPEL_H264_H16_XMM(OPNAME, EXT) QPEL_H264_H16(OPNAME, EXT)
#endif // ARCH_X86_64

#define QPEL_H264_H_XMM(OPNAME, MMX)\
QPEL_H264_H16_XMM(OPNAME, MMX)\
static av_always_inline void ff_ ## OPNAME ## h264_qpel16_h_lowpass_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t dstStride, ptrdiff_t srcStride)\
{\
    ff_ ## OPNAME ## h264_qpel8_h_lowpass_ ## MMX(dst  , src  , dstStride, srcStride);\
    ff_ ## OPNAME ## h264_qpel8_h_lowpass_ ## MMX(dst+8, src+8, dstStride, srcStride);\
    src += 8*srcStride;\
    dst += 8*dstStride;\
    ff_ ## OPNAME ## h264_qpel8_h_lowpass_ ## MMX(dst  , src  , dstStride, srcStride);\
    ff_ ## OPNAME ## h264_qpel8_h_lowpass_ ## MMX(dst+8, src+8, dstStride, srcStride);\
}\

#define QPEL_H264_V_XMM(OPNAME, XMM, XMM2)\
static av_always_inline void ff_ ## OPNAME ## h264_qpel8_v_lowpass_ ## XMM(uint8_t *dst, const uint8_t *src, ptrdiff_t dstStride, ptrdiff_t srcStride)\
{\
    ff_ ## OPNAME ## h264_qpel8or16_v_lowpass_ ## XMM2(dst  , src  , dstStride, srcStride, 8);\
}\
static av_always_inline void ff_ ## OPNAME ## h264_qpel16_v_lowpass_ ## XMM(uint8_t *dst, const uint8_t *src, ptrdiff_t dstStride, ptrdiff_t srcStride)\
{\
    ff_ ## OPNAME ## h264_qpel8or16_v_lowpass_ ## XMM2(dst  , src  , dstStride, srcStride, 16);\
    ff_ ## OPNAME ## h264_qpel8or16_v_lowpass_ ## XMM2(dst+8, src+8, dstStride, srcStride, 16);\
}

static av_always_inline void put_h264_qpel8or16_hv1_lowpass_sse2(int16_t *tmp,
                                                                 const uint8_t *src,
                                                                 ptrdiff_t srcStride,
                                                                 int size)
{
    int w = (size+8)>>3;
    src -= 2*srcStride+2;
    while(w--){
        ff_put_h264_qpel8or16_hv1_lowpass_op_sse2(src, tmp, srcStride, size);
        tmp += 8;
        src += 8;
    }
}

#define QPEL_H264_HV_XMM(OPNAME, MMX)\
static av_always_inline void OPNAME ## h264_qpel8_hv_lowpass_ ## MMX(uint8_t *dst, int16_t *tmp, const uint8_t *src, ptrdiff_t dstStride, ptrdiff_t srcStride)\
{\
    put_h264_qpel8or16_hv1_lowpass_sse2(tmp, src, srcStride, 8);\
    ff_ ## OPNAME ## h264_qpel8_hv2_lowpass_ ## MMX(dst, tmp, dstStride);\
}\
static av_always_inline void OPNAME ## h264_qpel16_hv_lowpass_ ## MMX(uint8_t *dst, int16_t *tmp, const uint8_t *src, ptrdiff_t dstStride, ptrdiff_t srcStride)\
{\
    put_h264_qpel8or16_hv1_lowpass_sse2(tmp, src, srcStride, 16);\
    ff_ ## OPNAME ## h264_qpel16_hv2_lowpass_ ## MMX(dst, tmp, dstStride);\
}\

#define H264_MC_V_H_HV(OPNAME, SIZE, MMX, ALIGN, SHIFT5_EXT) \
H264_MC_V(OPNAME, SIZE, MMX, ALIGN, SHIFT5_EXT)\
H264_MC_H(OPNAME, SIZE, MMX, ALIGN, SHIFT5_EXT)\
H264_MC_HV(OPNAME, SIZE, MMX, ALIGN, SHIFT5_EXT)\

#define H264_MC_H(OPNAME, SIZE, MMX, ALIGN, UNUSED) \
static void OPNAME ## h264_qpel ## SIZE ## _mc10_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
    ff_ ## OPNAME ## h264_qpel ## SIZE ## _h_lowpass_l2_ ## MMX(dst, src, src, stride, stride);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc20_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
    ff_ ## OPNAME ## h264_qpel ## SIZE ## _h_lowpass_ ## MMX(dst, src, stride, stride);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc30_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
    ff_ ## OPNAME ## h264_qpel ## SIZE ## _h_lowpass_l2_ ## MMX(dst, src, src+1, stride, stride);\
}\

#define H264_MC_V(OPNAME, SIZE, MMX, ALIGN, UNUSED) \
static void OPNAME ## h264_qpel ## SIZE ## _mc01_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
    LOCAL_ALIGNED(ALIGN, uint8_t, temp, [SIZE*SIZE]);\
    ff_put_h264_qpel ## SIZE ## _v_lowpass_ ## MMX(temp, src, SIZE, stride);\
    ff_ ## OPNAME ## pixels ## SIZE ## x ## SIZE ## _l2_ ## MMX(dst, src, temp, stride, stride);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc02_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
    ff_ ## OPNAME ## h264_qpel ## SIZE ## _v_lowpass_ ## MMX(dst, src, stride, stride);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc03_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
    LOCAL_ALIGNED(ALIGN, uint8_t, temp, [SIZE*SIZE]);\
    ff_put_h264_qpel ## SIZE ## _v_lowpass_ ## MMX(temp, src, SIZE, stride);\
    ff_ ## OPNAME ## pixels ## SIZE ## x ## SIZE ## _l2_ ## MMX(dst, src+stride, temp, stride, stride);\
}\

#define H264_MC_HV(OPNAME, SIZE, MMX, ALIGN, SHIFT5_EXT) \
static void OPNAME ## h264_qpel ## SIZE ## _mc11_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
    LOCAL_ALIGNED(ALIGN, uint8_t, temp, [SIZE*SIZE]);\
    ff_put_h264_qpel ## SIZE ## _v_lowpass_ ## MMX(temp, src, SIZE, stride);\
    ff_ ## OPNAME ## h264_qpel ## SIZE ## _h_lowpass_l2_ ## MMX(dst, src, temp, stride, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc31_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
    LOCAL_ALIGNED(ALIGN, uint8_t, temp, [SIZE*SIZE]);\
    ff_put_h264_qpel ## SIZE ## _v_lowpass_ ## MMX(temp, src+1, SIZE, stride);\
    ff_ ## OPNAME ## h264_qpel ## SIZE ## _h_lowpass_l2_ ## MMX(dst, src, temp, stride, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc13_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
    LOCAL_ALIGNED(ALIGN, uint8_t, temp, [SIZE*SIZE]);\
    ff_put_h264_qpel ## SIZE ## _v_lowpass_ ## MMX(temp, src, SIZE, stride);\
    ff_ ## OPNAME ## h264_qpel ## SIZE ## _h_lowpass_l2_ ## MMX(dst, src+stride, temp, stride, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc33_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
    LOCAL_ALIGNED(ALIGN, uint8_t, temp, [SIZE*SIZE]);\
    ff_put_h264_qpel ## SIZE ## _v_lowpass_ ## MMX(temp, src+1, SIZE, stride);\
    ff_ ## OPNAME ## h264_qpel ## SIZE ## _h_lowpass_l2_ ## MMX(dst, src+stride, temp, stride, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc22_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
    LOCAL_ALIGNED(ALIGN, uint16_t, temp, [SIZE*(SIZE<8?12:24)]);\
    OPNAME ## h264_qpel ## SIZE ## _hv_lowpass_ ## MMX(dst, temp, src, stride, stride);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc21_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
    LOCAL_ALIGNED(ALIGN, uint8_t, temp, [SIZE*(SIZE<8?12:24)*2 + SIZE*SIZE]);\
    uint8_t * const halfHV= temp;\
    int16_t * const halfV= (int16_t*)(temp + SIZE*SIZE);\
    av_assert2(((uintptr_t)temp & 7) == 0);\
    put_h264_qpel ## SIZE ## _hv_lowpass_ ## MMX(halfHV, halfV, src, SIZE, stride);\
    ff_ ## OPNAME ## h264_qpel ## SIZE ## _h_lowpass_l2_ ## MMX(dst, src, halfHV, stride, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc23_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
    LOCAL_ALIGNED(ALIGN, uint8_t, temp, [SIZE*(SIZE<8?12:24)*2 + SIZE*SIZE]);\
    uint8_t * const halfHV= temp;\
    int16_t * const halfV= (int16_t*)(temp + SIZE*SIZE);\
    av_assert2(((uintptr_t)temp & 7) == 0);\
    put_h264_qpel ## SIZE ## _hv_lowpass_ ## MMX(halfHV, halfV, src, SIZE, stride);\
    ff_ ## OPNAME ## h264_qpel ## SIZE ## _h_lowpass_l2_ ## MMX(dst, src+stride, halfHV, stride, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc12_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
    LOCAL_ALIGNED(ALIGN, uint8_t, temp, [SIZE*(SIZE<8?12:24)*2 + SIZE*SIZE]);\
    uint8_t * const halfHV= temp;\
    int16_t * const halfV= (int16_t*)(temp + SIZE*SIZE);\
    av_assert2(((uintptr_t)temp & 7) == 0);\
    put_h264_qpel ## SIZE ## _hv_lowpass_ ## MMX(halfHV, halfV, src, SIZE, stride);\
    ff_ ## OPNAME ## pixels ## SIZE ## _l2_shift5_ ## SHIFT5_EXT(dst, halfV+2, halfHV, stride);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc32_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
    LOCAL_ALIGNED(ALIGN, uint8_t, temp, [SIZE*(SIZE<8?12:24)*2 + SIZE*SIZE]);\
    uint8_t * const halfHV= temp;\
    int16_t * const halfV= (int16_t*)(temp + SIZE*SIZE);\
    av_assert2(((uintptr_t)temp & 7) == 0);\
    put_h264_qpel ## SIZE ## _hv_lowpass_ ## MMX(halfHV, halfV, src, SIZE, stride);\
    ff_ ## OPNAME ## pixels ## SIZE ## _l2_shift5_ ## SHIFT5_EXT(dst, halfV+3, halfHV, stride);\
}\

#define H264_MC(QPEL, SIZE, MMX, ALIGN, SHIFT5_EXT)\
QPEL(put_, SIZE, MMX, ALIGN, SHIFT5_EXT) \
QPEL(avg_, SIZE, MMX, ALIGN, SHIFT5_EXT) \

#define H264_MC_816(QPEL, XMM, SHIFT5_EXT)\
QPEL(put_, 8, XMM, 16, SHIFT5_EXT)\
QPEL(put_, 16,XMM, 16, SHIFT5_EXT)\
QPEL(avg_, 8, XMM, 16, SHIFT5_EXT)\
QPEL(avg_, 16,XMM, 16, SHIFT5_EXT)\

QPEL_H264(put_, mmxext)
QPEL_H264(avg_, mmxext)
QPEL_H264_V_XMM(put_, sse2, sse2)
QPEL_H264_V_XMM(avg_, sse2, sse2)
QPEL_H264_HV_XMM(put_, sse2)
QPEL_H264_HV_XMM(avg_, sse2)
QPEL_H264_H_XMM(put_, ssse3)
QPEL_H264_H_XMM(avg_, ssse3)
QPEL_H264_V_XMM(put_, ssse3, sse2)
QPEL_H264_HV_XMM(put_, ssse3)
QPEL_H264_HV_XMM(avg_, ssse3)

H264_MC(H264_MC_V_H_HV, 4, mmxext, 8, mmxext)
H264_MC_816(H264_MC_V, sse2, sse2)
H264_MC_816(H264_MC_HV, sse2, sse2)
H264_MC_816(H264_MC_H, ssse3, sse2)
H264_MC_816(H264_MC_HV, ssse3, sse2)


//10bit
#define LUMA_MC_OP(OP, NUM, DEPTH, TYPE, OPT) \
void ff_ ## OP ## _h264_qpel ## NUM ## _ ## TYPE ## _ ## DEPTH ## _ ## OPT \
    (uint8_t *dst, const uint8_t *src, ptrdiff_t stride);

#define LUMA_MC_4(DEPTH, TYPE, OPT) \
    LUMA_MC_OP(put,  4, DEPTH, TYPE, OPT) \
    LUMA_MC_OP(avg,  4, DEPTH, TYPE, OPT)

#define LUMA_MC_816(DEPTH, TYPE, OPT) \
    LUMA_MC_OP(put,  8, DEPTH, TYPE, OPT) \
    LUMA_MC_OP(avg,  8, DEPTH, TYPE, OPT) \
    LUMA_MC_OP(put, 16, DEPTH, TYPE, OPT) \
    LUMA_MC_OP(avg, 16, DEPTH, TYPE, OPT)

LUMA_MC_4(10, mc00, mmxext)
LUMA_MC_4(10, mc10, mmxext)
LUMA_MC_4(10, mc20, mmxext)
LUMA_MC_4(10, mc30, mmxext)
LUMA_MC_4(10, mc01, mmxext)
LUMA_MC_4(10, mc11, mmxext)
LUMA_MC_4(10, mc21, mmxext)
LUMA_MC_4(10, mc31, mmxext)
LUMA_MC_4(10, mc02, mmxext)
LUMA_MC_4(10, mc12, mmxext)
LUMA_MC_4(10, mc22, mmxext)
LUMA_MC_4(10, mc32, mmxext)
LUMA_MC_4(10, mc03, mmxext)
LUMA_MC_4(10, mc13, mmxext)
LUMA_MC_4(10, mc23, mmxext)
LUMA_MC_4(10, mc33, mmxext)

LUMA_MC_816(10, mc00, sse2)
LUMA_MC_816(10, mc10, sse2)
LUMA_MC_816(10, mc10, ssse3_cache64)
LUMA_MC_816(10, mc20, sse2)
LUMA_MC_816(10, mc20, ssse3_cache64)
LUMA_MC_816(10, mc30, sse2)
LUMA_MC_816(10, mc30, ssse3_cache64)
LUMA_MC_816(10, mc01, sse2)
LUMA_MC_816(10, mc11, sse2)
LUMA_MC_816(10, mc21, sse2)
LUMA_MC_816(10, mc31, sse2)
LUMA_MC_816(10, mc02, sse2)
LUMA_MC_816(10, mc12, sse2)
LUMA_MC_816(10, mc22, sse2)
LUMA_MC_816(10, mc32, sse2)
LUMA_MC_816(10, mc03, sse2)
LUMA_MC_816(10, mc13, sse2)
LUMA_MC_816(10, mc23, sse2)
LUMA_MC_816(10, mc33, sse2)

#endif /* HAVE_X86ASM */

#define SET_QPEL_FUNCS_1PP(PFX, IDX, SIZE, CPU, PREFIX)                      \
    do {                                                                     \
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
#define SET_QPEL_FUNCS(PFX, IDX, SIZE, CPU, PREFIX)                          \
    do {                                                                     \
    c->PFX ## _pixels_tab[IDX][ 0] = PREFIX ## PFX ## SIZE ## _mc00_ ## CPU; \
    SET_QPEL_FUNCS_1PP(PFX, IDX, SIZE, CPU, PREFIX);                         \
    } while (0)

#define H264_QPEL_FUNCS(x, y, CPU)                                                            \
    do {                                                                                      \
        c->put_h264_qpel_pixels_tab[0][x + y * 4] = put_h264_qpel16_mc ## x ## y ## _ ## CPU; \
        c->put_h264_qpel_pixels_tab[1][x + y * 4] = put_h264_qpel8_mc  ## x ## y ## _ ## CPU; \
        c->avg_h264_qpel_pixels_tab[0][x + y * 4] = avg_h264_qpel16_mc ## x ## y ## _ ## CPU; \
        c->avg_h264_qpel_pixels_tab[1][x + y * 4] = avg_h264_qpel8_mc  ## x ## y ## _ ## CPU; \
    } while (0)

#define H264_QPEL_FUNCS_10(x, y, CPU)                                                               \
    do {                                                                                            \
        c->put_h264_qpel_pixels_tab[0][x + y * 4] = ff_put_h264_qpel16_mc ## x ## y ## _10_ ## CPU; \
        c->put_h264_qpel_pixels_tab[1][x + y * 4] = ff_put_h264_qpel8_mc  ## x ## y ## _10_ ## CPU; \
        c->avg_h264_qpel_pixels_tab[0][x + y * 4] = ff_avg_h264_qpel16_mc ## x ## y ## _10_ ## CPU; \
        c->avg_h264_qpel_pixels_tab[1][x + y * 4] = ff_avg_h264_qpel8_mc  ## x ## y ## _10_ ## CPU; \
    } while (0)

av_cold void ff_h264qpel_init_x86(H264QpelContext *c, int bit_depth)
{
#if HAVE_X86ASM
    int high_bit_depth = bit_depth > 8;
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_MMXEXT(cpu_flags)) {
        if (!high_bit_depth) {
            SET_QPEL_FUNCS_1PP(put_h264_qpel, 2,  4, mmxext, );
            c->avg_h264_qpel_pixels_tab[1][0] = ff_avg_pixels8x8_mmxext;
            SET_QPEL_FUNCS_1PP(avg_h264_qpel, 2,  4, mmxext, );
            c->avg_h264_qpel_pixels_tab[2][0] = ff_avg_pixels4_mmxext;
        } else if (bit_depth == 10) {
            SET_QPEL_FUNCS(put_h264_qpel, 2, 4,  10_mmxext, ff_);
            SET_QPEL_FUNCS(avg_h264_qpel, 2, 4,  10_mmxext, ff_);
        }
    }

    if (EXTERNAL_SSE2(cpu_flags)) {
        if (!high_bit_depth) {
            H264_QPEL_FUNCS(0, 1, sse2);
            H264_QPEL_FUNCS(0, 2, sse2);
            H264_QPEL_FUNCS(0, 3, sse2);
            H264_QPEL_FUNCS(1, 1, sse2);
            H264_QPEL_FUNCS(1, 2, sse2);
            H264_QPEL_FUNCS(1, 3, sse2);
            H264_QPEL_FUNCS(2, 1, sse2);
            H264_QPEL_FUNCS(2, 2, sse2);
            H264_QPEL_FUNCS(2, 3, sse2);
            H264_QPEL_FUNCS(3, 1, sse2);
            H264_QPEL_FUNCS(3, 2, sse2);
            H264_QPEL_FUNCS(3, 3, sse2);
            c->put_h264_qpel_pixels_tab[0][0] = ff_put_pixels16x16_sse2;
            c->avg_h264_qpel_pixels_tab[0][0] = ff_avg_pixels16x16_sse2;
        }

        if (bit_depth == 10) {
            SET_QPEL_FUNCS(put_h264_qpel, 0, 16, 10_sse2, ff_);
            SET_QPEL_FUNCS(put_h264_qpel, 1,  8, 10_sse2, ff_);
            SET_QPEL_FUNCS(avg_h264_qpel, 0, 16, 10_sse2, ff_);
            SET_QPEL_FUNCS(avg_h264_qpel, 1,  8, 10_sse2, ff_);
            H264_QPEL_FUNCS_10(1, 0, sse2);
            H264_QPEL_FUNCS_10(2, 0, sse2);
            H264_QPEL_FUNCS_10(3, 0, sse2);
        }
    }

    if (EXTERNAL_SSSE3(cpu_flags)) {
        if (!high_bit_depth) {
            H264_QPEL_FUNCS(1, 0, ssse3);
            H264_QPEL_FUNCS(1, 1, ssse3);
            H264_QPEL_FUNCS(1, 2, ssse3);
            H264_QPEL_FUNCS(1, 3, ssse3);
            H264_QPEL_FUNCS(2, 0, ssse3);
            H264_QPEL_FUNCS(2, 1, ssse3);
            H264_QPEL_FUNCS(2, 2, ssse3);
            H264_QPEL_FUNCS(2, 3, ssse3);
            H264_QPEL_FUNCS(3, 0, ssse3);
            H264_QPEL_FUNCS(3, 1, ssse3);
            H264_QPEL_FUNCS(3, 2, ssse3);
            H264_QPEL_FUNCS(3, 3, ssse3);
        }

        if (bit_depth == 10) {
            H264_QPEL_FUNCS_10(1, 0, ssse3_cache64);
            H264_QPEL_FUNCS_10(2, 0, ssse3_cache64);
            H264_QPEL_FUNCS_10(3, 0, ssse3_cache64);
        }
    }
#endif
}
