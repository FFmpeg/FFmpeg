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

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/asm.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/h264.h"
#include "libavcodec/h264qpel.h"
#include "libavcodec/pixels.h"
#include "fpel.h"

#if HAVE_YASM
void ff_put_pixels4_l2_mmxext(uint8_t *dst, const uint8_t *src1, const uint8_t *src2,
                              int dstStride, int src1Stride, int h);
void ff_avg_pixels4_l2_mmxext(uint8_t *dst, const uint8_t *src1, const uint8_t *src2,
                              int dstStride, int src1Stride, int h);
void ff_put_pixels8_l2_mmxext(uint8_t *dst, const uint8_t *src1, const uint8_t *src2,
                              int dstStride, int src1Stride, int h);
void ff_avg_pixels8_l2_mmxext(uint8_t *dst, const uint8_t *src1, const uint8_t *src2,
                              int dstStride, int src1Stride, int h);
void ff_put_pixels16_l2_mmxext(uint8_t *dst, const uint8_t *src1, const uint8_t *src2,
                               int dstStride, int src1Stride, int h);
void ff_avg_pixels16_l2_mmxext(uint8_t *dst, const uint8_t *src1, const uint8_t *src2,
                               int dstStride, int src1Stride, int h);
#define ff_put_pixels8_l2_sse2  ff_put_pixels8_l2_mmxext
#define ff_avg_pixels8_l2_sse2  ff_avg_pixels8_l2_mmxext
#define ff_put_pixels16_l2_sse2 ff_put_pixels16_l2_mmxext
#define ff_avg_pixels16_l2_sse2 ff_avg_pixels16_l2_mmxext
#define ff_put_pixels16_mmxext  ff_put_pixels16_mmx
#define ff_put_pixels8_mmxext   ff_put_pixels8_mmx
#define ff_put_pixels4_mmxext   ff_put_pixels4_mmx

#define DEF_QPEL(OPNAME)\
void ff_ ## OPNAME ## _h264_qpel4_h_lowpass_mmxext(uint8_t *dst, const uint8_t *src, int dstStride, int srcStride);\
void ff_ ## OPNAME ## _h264_qpel8_h_lowpass_mmxext(uint8_t *dst, const uint8_t *src, int dstStride, int srcStride);\
void ff_ ## OPNAME ## _h264_qpel8_h_lowpass_ssse3(uint8_t *dst, const uint8_t *src, int dstStride, int srcStride);\
void ff_ ## OPNAME ## _h264_qpel4_h_lowpass_l2_mmxext(uint8_t *dst, const uint8_t *src, const uint8_t *src2, int dstStride, int src2Stride);\
void ff_ ## OPNAME ## _h264_qpel8_h_lowpass_l2_mmxext(uint8_t *dst, const uint8_t *src, const uint8_t *src2, int dstStride, int src2Stride);\
void ff_ ## OPNAME ## _h264_qpel8_h_lowpass_l2_ssse3(uint8_t *dst, const uint8_t *src, const uint8_t *src2, int dstStride, int src2Stride);\
void ff_ ## OPNAME ## _h264_qpel4_v_lowpass_mmxext(uint8_t *dst, const uint8_t *src, int dstStride, int srcStride);\
void ff_ ## OPNAME ## _h264_qpel8or16_v_lowpass_op_mmxext(uint8_t *dst, const uint8_t *src, int dstStride, int srcStride, int h);\
void ff_ ## OPNAME ## _h264_qpel8or16_v_lowpass_sse2(uint8_t *dst, const uint8_t *src, int dstStride, int srcStride, int h);\
void ff_ ## OPNAME ## _h264_qpel4_hv_lowpass_v_mmxext(const uint8_t *src, int16_t *tmp, int srcStride);\
void ff_ ## OPNAME ## _h264_qpel4_hv_lowpass_h_mmxext(int16_t *tmp, uint8_t *dst, int dstStride);\
void ff_ ## OPNAME ## _h264_qpel8or16_hv1_lowpass_op_mmxext(const uint8_t *src, int16_t *tmp, int srcStride, int size);\
void ff_ ## OPNAME ## _h264_qpel8or16_hv1_lowpass_op_sse2(const uint8_t *src, int16_t *tmp, int srcStride, int size);\
void ff_ ## OPNAME ## _h264_qpel8or16_hv2_lowpass_op_mmxext(uint8_t *dst, int16_t *tmp, int dstStride, int unused, int h);\
void ff_ ## OPNAME ## _h264_qpel8or16_hv2_lowpass_ssse3(uint8_t *dst, int16_t *tmp, int dstStride, int tmpStride, int size);\
void ff_ ## OPNAME ## _pixels4_l2_shift5_mmxext(uint8_t *dst, const int16_t *src16, const uint8_t *src8, int dstStride, int src8Stride, int h);\
void ff_ ## OPNAME ## _pixels8_l2_shift5_mmxext(uint8_t *dst, const int16_t *src16, const uint8_t *src8, int dstStride, int src8Stride, int h);

DEF_QPEL(avg)
DEF_QPEL(put)

#define QPEL_H264(OPNAME, OP, MMX)\
static av_always_inline void ff_ ## OPNAME ## h264_qpel4_hv_lowpass_ ## MMX(uint8_t *dst, int16_t *tmp, const uint8_t *src, int dstStride, int tmpStride, int srcStride){\
    int w=3;\
    src -= 2*srcStride+2;\
    while(w--){\
        ff_ ## OPNAME ## h264_qpel4_hv_lowpass_v_mmxext(src, tmp, srcStride);\
        tmp += 4;\
        src += 4;\
    }\
    tmp -= 3*4;\
    ff_ ## OPNAME ## h264_qpel4_hv_lowpass_h_mmxext(tmp, dst, dstStride);\
}\
\
static av_always_inline void ff_ ## OPNAME ## h264_qpel8or16_v_lowpass_ ## MMX(uint8_t *dst, const uint8_t *src, int dstStride, int srcStride, int h){\
    src -= 2*srcStride;\
    ff_ ## OPNAME ## h264_qpel8or16_v_lowpass_op_mmxext(dst, src, dstStride, srcStride, h);\
    src += 4;\
    dst += 4;\
    ff_ ## OPNAME ## h264_qpel8or16_v_lowpass_op_mmxext(dst, src, dstStride, srcStride, h);\
}\
static av_always_inline void ff_ ## OPNAME ## h264_qpel8or16_hv1_lowpass_ ## MMX(int16_t *tmp, const uint8_t *src, int tmpStride, int srcStride, int size){\
    int w = (size+8)>>2;\
    src -= 2*srcStride+2;\
    while(w--){\
        ff_ ## OPNAME ## h264_qpel8or16_hv1_lowpass_op_mmxext(src, tmp, srcStride, size);\
        tmp += 4;\
        src += 4;\
    }\
}\
static av_always_inline void ff_ ## OPNAME ## h264_qpel8or16_hv2_lowpass_ ## MMX(uint8_t *dst, int16_t *tmp, int dstStride, int tmpStride, int size){\
    int w = size>>4;\
    do{\
    ff_ ## OPNAME ## h264_qpel8or16_hv2_lowpass_op_mmxext(dst, tmp, dstStride, 0, size);\
    tmp += 8;\
    dst += 8;\
    }while(w--);\
}\
\
static av_always_inline void ff_ ## OPNAME ## h264_qpel8_v_lowpass_ ## MMX(uint8_t *dst, const uint8_t *src, int dstStride, int srcStride){\
    ff_ ## OPNAME ## h264_qpel8or16_v_lowpass_ ## MMX(dst  , src  , dstStride, srcStride, 8);\
}\
static av_always_inline void ff_ ## OPNAME ## h264_qpel16_v_lowpass_ ## MMX(uint8_t *dst, const uint8_t *src, int dstStride, int srcStride){\
    ff_ ## OPNAME ## h264_qpel8or16_v_lowpass_ ## MMX(dst  , src  , dstStride, srcStride, 16);\
    ff_ ## OPNAME ## h264_qpel8or16_v_lowpass_ ## MMX(dst+8, src+8, dstStride, srcStride, 16);\
}\
\
static av_always_inline void ff_ ## OPNAME ## h264_qpel16_h_lowpass_ ## MMX(uint8_t *dst, const uint8_t *src, int dstStride, int srcStride){\
    ff_ ## OPNAME ## h264_qpel8_h_lowpass_ ## MMX(dst  , src  , dstStride, srcStride);\
    ff_ ## OPNAME ## h264_qpel8_h_lowpass_ ## MMX(dst+8, src+8, dstStride, srcStride);\
    src += 8*srcStride;\
    dst += 8*dstStride;\
    ff_ ## OPNAME ## h264_qpel8_h_lowpass_ ## MMX(dst  , src  , dstStride, srcStride);\
    ff_ ## OPNAME ## h264_qpel8_h_lowpass_ ## MMX(dst+8, src+8, dstStride, srcStride);\
}\
\
static av_always_inline void ff_ ## OPNAME ## h264_qpel16_h_lowpass_l2_ ## MMX(uint8_t *dst, const uint8_t *src, const uint8_t *src2, int dstStride, int src2Stride){\
    ff_ ## OPNAME ## h264_qpel8_h_lowpass_l2_ ## MMX(dst  , src  , src2  , dstStride, src2Stride);\
    ff_ ## OPNAME ## h264_qpel8_h_lowpass_l2_ ## MMX(dst+8, src+8, src2+8, dstStride, src2Stride);\
    src += 8*dstStride;\
    dst += 8*dstStride;\
    src2 += 8*src2Stride;\
    ff_ ## OPNAME ## h264_qpel8_h_lowpass_l2_ ## MMX(dst  , src  , src2  , dstStride, src2Stride);\
    ff_ ## OPNAME ## h264_qpel8_h_lowpass_l2_ ## MMX(dst+8, src+8, src2+8, dstStride, src2Stride);\
}\
\
static av_always_inline void ff_ ## OPNAME ## h264_qpel8or16_hv_lowpass_ ## MMX(uint8_t *dst, int16_t *tmp, const uint8_t *src, int dstStride, int tmpStride, int srcStride, int size){\
    ff_put_h264_qpel8or16_hv1_lowpass_ ## MMX(tmp, src, tmpStride, srcStride, size);\
    ff_ ## OPNAME ## h264_qpel8or16_hv2_lowpass_ ## MMX(dst, tmp, dstStride, tmpStride, size);\
}\
static av_always_inline void ff_ ## OPNAME ## h264_qpel8_hv_lowpass_ ## MMX(uint8_t *dst, int16_t *tmp, const uint8_t *src, int dstStride, int tmpStride, int srcStride){\
    ff_ ## OPNAME ## h264_qpel8or16_hv_lowpass_ ## MMX(dst  , tmp  , src  , dstStride, tmpStride, srcStride, 8);\
}\
\
static av_always_inline void ff_ ## OPNAME ## h264_qpel16_hv_lowpass_ ## MMX(uint8_t *dst, int16_t *tmp, const uint8_t *src, int dstStride, int tmpStride, int srcStride){\
    ff_ ## OPNAME ## h264_qpel8or16_hv_lowpass_ ## MMX(dst  , tmp  , src  , dstStride, tmpStride, srcStride, 16);\
}\
\
static av_always_inline void ff_ ## OPNAME ## pixels16_l2_shift5_ ## MMX(uint8_t *dst, const int16_t *src16, const uint8_t *src8, int dstStride, int src8Stride, int h)\
{\
    ff_ ## OPNAME ## pixels8_l2_shift5_ ## MMX(dst  , src16  , src8  , dstStride, src8Stride, h);\
    ff_ ## OPNAME ## pixels8_l2_shift5_ ## MMX(dst+8, src16+8, src8+8, dstStride, src8Stride, h);\
}\


#if ARCH_X86_64
#define QPEL_H264_H16_XMM(OPNAME, OP, MMX)\

void ff_avg_h264_qpel16_h_lowpass_l2_ssse3(uint8_t *dst, const uint8_t *src, const uint8_t *src2, int dstStride, int src2Stride);
void ff_put_h264_qpel16_h_lowpass_l2_ssse3(uint8_t *dst, const uint8_t *src, const uint8_t *src2, int dstStride, int src2Stride);

#else // ARCH_X86_64
#define QPEL_H264_H16_XMM(OPNAME, OP, MMX)\
static av_always_inline void ff_ ## OPNAME ## h264_qpel16_h_lowpass_l2_ ## MMX(uint8_t *dst, const uint8_t *src, const uint8_t *src2, int dstStride, int src2Stride){\
    ff_ ## OPNAME ## h264_qpel8_h_lowpass_l2_ ## MMX(dst  , src  , src2  , dstStride, src2Stride);\
    ff_ ## OPNAME ## h264_qpel8_h_lowpass_l2_ ## MMX(dst+8, src+8, src2+8, dstStride, src2Stride);\
    src += 8*dstStride;\
    dst += 8*dstStride;\
    src2 += 8*src2Stride;\
    ff_ ## OPNAME ## h264_qpel8_h_lowpass_l2_ ## MMX(dst  , src  , src2  , dstStride, src2Stride);\
    ff_ ## OPNAME ## h264_qpel8_h_lowpass_l2_ ## MMX(dst+8, src+8, src2+8, dstStride, src2Stride);\
}
#endif // ARCH_X86_64

#define QPEL_H264_H_XMM(OPNAME, OP, MMX)\
QPEL_H264_H16_XMM(OPNAME, OP, MMX)\
static av_always_inline void ff_ ## OPNAME ## h264_qpel16_h_lowpass_ ## MMX(uint8_t *dst, const uint8_t *src, int dstStride, int srcStride){\
    ff_ ## OPNAME ## h264_qpel8_h_lowpass_ ## MMX(dst  , src  , dstStride, srcStride);\
    ff_ ## OPNAME ## h264_qpel8_h_lowpass_ ## MMX(dst+8, src+8, dstStride, srcStride);\
    src += 8*srcStride;\
    dst += 8*dstStride;\
    ff_ ## OPNAME ## h264_qpel8_h_lowpass_ ## MMX(dst  , src  , dstStride, srcStride);\
    ff_ ## OPNAME ## h264_qpel8_h_lowpass_ ## MMX(dst+8, src+8, dstStride, srcStride);\
}\

#define QPEL_H264_V_XMM(OPNAME, OP, MMX)\
static av_always_inline void ff_ ## OPNAME ## h264_qpel8_v_lowpass_ ## MMX(uint8_t *dst, const uint8_t *src, int dstStride, int srcStride){\
    ff_ ## OPNAME ## h264_qpel8or16_v_lowpass_ ## MMX(dst  , src  , dstStride, srcStride, 8);\
}\
static av_always_inline void ff_ ## OPNAME ## h264_qpel16_v_lowpass_ ## MMX(uint8_t *dst, const uint8_t *src, int dstStride, int srcStride){\
    ff_ ## OPNAME ## h264_qpel8or16_v_lowpass_ ## MMX(dst  , src  , dstStride, srcStride, 16);\
    ff_ ## OPNAME ## h264_qpel8or16_v_lowpass_ ## MMX(dst+8, src+8, dstStride, srcStride, 16);\
}

static av_always_inline void put_h264_qpel8or16_hv1_lowpass_sse2(int16_t *tmp,
                                                                 const uint8_t *src,
                                                                 int tmpStride,
                                                                 int srcStride,
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

#define QPEL_H264_HV_XMM(OPNAME, OP, MMX)\
static av_always_inline void ff_ ## OPNAME ## h264_qpel8or16_hv_lowpass_ ## MMX(uint8_t *dst, int16_t *tmp, const uint8_t *src, int dstStride, int tmpStride, int srcStride, int size){\
    put_h264_qpel8or16_hv1_lowpass_sse2(tmp, src, tmpStride, srcStride, size);\
    ff_ ## OPNAME ## h264_qpel8or16_hv2_lowpass_ ## MMX(dst, tmp, dstStride, tmpStride, size);\
}\
static av_always_inline void ff_ ## OPNAME ## h264_qpel8_hv_lowpass_ ## MMX(uint8_t *dst, int16_t *tmp, const uint8_t *src, int dstStride, int tmpStride, int srcStride){\
    ff_ ## OPNAME ## h264_qpel8or16_hv_lowpass_ ## MMX(dst, tmp, src, dstStride, tmpStride, srcStride, 8);\
}\
static av_always_inline void ff_ ## OPNAME ## h264_qpel16_hv_lowpass_ ## MMX(uint8_t *dst, int16_t *tmp, const uint8_t *src, int dstStride, int tmpStride, int srcStride){\
    ff_ ## OPNAME ## h264_qpel8or16_hv_lowpass_ ## MMX(dst, tmp, src, dstStride, tmpStride, srcStride, 16);\
}\

#define ff_put_h264_qpel8_h_lowpass_l2_sse2  ff_put_h264_qpel8_h_lowpass_l2_mmxext
#define ff_avg_h264_qpel8_h_lowpass_l2_sse2  ff_avg_h264_qpel8_h_lowpass_l2_mmxext
#define ff_put_h264_qpel16_h_lowpass_l2_sse2 ff_put_h264_qpel16_h_lowpass_l2_mmxext
#define ff_avg_h264_qpel16_h_lowpass_l2_sse2 ff_avg_h264_qpel16_h_lowpass_l2_mmxext

#define ff_put_h264_qpel8_v_lowpass_ssse3  ff_put_h264_qpel8_v_lowpass_sse2
#define ff_avg_h264_qpel8_v_lowpass_ssse3  ff_avg_h264_qpel8_v_lowpass_sse2
#define ff_put_h264_qpel16_v_lowpass_ssse3 ff_put_h264_qpel16_v_lowpass_sse2
#define ff_avg_h264_qpel16_v_lowpass_ssse3 ff_avg_h264_qpel16_v_lowpass_sse2

#define ff_put_h264_qpel8or16_hv2_lowpass_sse2 ff_put_h264_qpel8or16_hv2_lowpass_mmxext
#define ff_avg_h264_qpel8or16_hv2_lowpass_sse2 ff_avg_h264_qpel8or16_hv2_lowpass_mmxext

#define H264_MC(OPNAME, SIZE, MMX, ALIGN) \
H264_MC_C(OPNAME, SIZE, MMX, ALIGN)\
H264_MC_V(OPNAME, SIZE, MMX, ALIGN)\
H264_MC_H(OPNAME, SIZE, MMX, ALIGN)\
H264_MC_HV(OPNAME, SIZE, MMX, ALIGN)\

static void put_h264_qpel16_mc00_sse2 (uint8_t *dst, const uint8_t *src,
                                       ptrdiff_t stride)
{
    ff_put_pixels16_sse2(dst, src, stride, 16);
}
static void avg_h264_qpel16_mc00_sse2 (uint8_t *dst, const uint8_t *src,
                                       ptrdiff_t stride)
{
    ff_avg_pixels16_sse2(dst, src, stride, 16);
}
#define put_h264_qpel8_mc00_sse2 put_h264_qpel8_mc00_mmxext
#define avg_h264_qpel8_mc00_sse2 avg_h264_qpel8_mc00_mmxext

#define H264_MC_C(OPNAME, SIZE, MMX, ALIGN) \
static void OPNAME ## h264_qpel ## SIZE ## _mc00_ ## MMX (uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
    ff_ ## OPNAME ## pixels ## SIZE ## _ ## MMX(dst, src, stride, SIZE);\
}\

#define H264_MC_H(OPNAME, SIZE, MMX, ALIGN) \
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

#define H264_MC_V(OPNAME, SIZE, MMX, ALIGN) \
static void OPNAME ## h264_qpel ## SIZE ## _mc01_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
    LOCAL_ALIGNED(ALIGN, uint8_t, temp, [SIZE*SIZE]);\
    ff_put_h264_qpel ## SIZE ## _v_lowpass_ ## MMX(temp, src, SIZE, stride);\
    ff_ ## OPNAME ## pixels ## SIZE ## _l2_ ## MMX(dst, src, temp, stride, stride, SIZE);\
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
    ff_ ## OPNAME ## pixels ## SIZE ## _l2_ ## MMX(dst, src+stride, temp, stride, stride, SIZE);\
}\

#define H264_MC_HV(OPNAME, SIZE, MMX, ALIGN) \
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
    ff_ ## OPNAME ## h264_qpel ## SIZE ## _hv_lowpass_ ## MMX(dst, temp, src, stride, SIZE, stride);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc21_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
    LOCAL_ALIGNED(ALIGN, uint8_t, temp, [SIZE*(SIZE<8?12:24)*2 + SIZE*SIZE]);\
    uint8_t * const halfHV= temp;\
    int16_t * const halfV= (int16_t*)(temp + SIZE*SIZE);\
    av_assert2(((int)temp & 7) == 0);\
    ff_put_h264_qpel ## SIZE ## _hv_lowpass_ ## MMX(halfHV, halfV, src, SIZE, SIZE, stride);\
    ff_ ## OPNAME ## h264_qpel ## SIZE ## _h_lowpass_l2_ ## MMX(dst, src, halfHV, stride, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc23_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
    LOCAL_ALIGNED(ALIGN, uint8_t, temp, [SIZE*(SIZE<8?12:24)*2 + SIZE*SIZE]);\
    uint8_t * const halfHV= temp;\
    int16_t * const halfV= (int16_t*)(temp + SIZE*SIZE);\
    av_assert2(((int)temp & 7) == 0);\
    ff_put_h264_qpel ## SIZE ## _hv_lowpass_ ## MMX(halfHV, halfV, src, SIZE, SIZE, stride);\
    ff_ ## OPNAME ## h264_qpel ## SIZE ## _h_lowpass_l2_ ## MMX(dst, src+stride, halfHV, stride, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc12_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
    LOCAL_ALIGNED(ALIGN, uint8_t, temp, [SIZE*(SIZE<8?12:24)*2 + SIZE*SIZE]);\
    uint8_t * const halfHV= temp;\
    int16_t * const halfV= (int16_t*)(temp + SIZE*SIZE);\
    av_assert2(((int)temp & 7) == 0);\
    ff_put_h264_qpel ## SIZE ## _hv_lowpass_ ## MMX(halfHV, halfV, src, SIZE, SIZE, stride);\
    ff_ ## OPNAME ## pixels ## SIZE ## _l2_shift5_mmxext(dst, halfV+2, halfHV, stride, SIZE, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc32_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
    LOCAL_ALIGNED(ALIGN, uint8_t, temp, [SIZE*(SIZE<8?12:24)*2 + SIZE*SIZE]);\
    uint8_t * const halfHV= temp;\
    int16_t * const halfV= (int16_t*)(temp + SIZE*SIZE);\
    av_assert2(((int)temp & 7) == 0);\
    ff_put_h264_qpel ## SIZE ## _hv_lowpass_ ## MMX(halfHV, halfV, src, SIZE, SIZE, stride);\
    ff_ ## OPNAME ## pixels ## SIZE ## _l2_shift5_mmxext(dst, halfV+3, halfHV, stride, SIZE, SIZE);\
}\

#define H264_MC_4816(MMX)\
H264_MC(put_, 4, MMX, 8)\
H264_MC(put_, 8, MMX, 8)\
H264_MC(put_, 16,MMX, 8)\
H264_MC(avg_, 4, MMX, 8)\
H264_MC(avg_, 8, MMX, 8)\
H264_MC(avg_, 16,MMX, 8)\

#define H264_MC_816(QPEL, XMM)\
QPEL(put_, 8, XMM, 16)\
QPEL(put_, 16,XMM, 16)\
QPEL(avg_, 8, XMM, 16)\
QPEL(avg_, 16,XMM, 16)\

QPEL_H264(put_,        PUT_OP, mmxext)
QPEL_H264(avg_, AVG_MMXEXT_OP, mmxext)
QPEL_H264_V_XMM(put_,       PUT_OP, sse2)
QPEL_H264_V_XMM(avg_,AVG_MMXEXT_OP, sse2)
QPEL_H264_HV_XMM(put_,       PUT_OP, sse2)
QPEL_H264_HV_XMM(avg_,AVG_MMXEXT_OP, sse2)
QPEL_H264_H_XMM(put_,       PUT_OP, ssse3)
QPEL_H264_H_XMM(avg_,AVG_MMXEXT_OP, ssse3)
QPEL_H264_HV_XMM(put_,       PUT_OP, ssse3)
QPEL_H264_HV_XMM(avg_,AVG_MMXEXT_OP, ssse3)

H264_MC_4816(mmxext)
H264_MC_816(H264_MC_V, sse2)
H264_MC_816(H264_MC_HV, sse2)
H264_MC_816(H264_MC_H, ssse3)
H264_MC_816(H264_MC_HV, ssse3)


//10bit
#define LUMA_MC_OP(OP, NUM, DEPTH, TYPE, OPT) \
void ff_ ## OP ## _h264_qpel ## NUM ## _ ## TYPE ## _ ## DEPTH ## _ ## OPT \
    (uint8_t *dst, const uint8_t *src, ptrdiff_t stride);

#define LUMA_MC_ALL(DEPTH, TYPE, OPT) \
    LUMA_MC_OP(put,  4, DEPTH, TYPE, OPT) \
    LUMA_MC_OP(avg,  4, DEPTH, TYPE, OPT) \
    LUMA_MC_OP(put,  8, DEPTH, TYPE, OPT) \
    LUMA_MC_OP(avg,  8, DEPTH, TYPE, OPT) \
    LUMA_MC_OP(put, 16, DEPTH, TYPE, OPT) \
    LUMA_MC_OP(avg, 16, DEPTH, TYPE, OPT)

#define LUMA_MC_816(DEPTH, TYPE, OPT) \
    LUMA_MC_OP(put,  8, DEPTH, TYPE, OPT) \
    LUMA_MC_OP(avg,  8, DEPTH, TYPE, OPT) \
    LUMA_MC_OP(put, 16, DEPTH, TYPE, OPT) \
    LUMA_MC_OP(avg, 16, DEPTH, TYPE, OPT)

LUMA_MC_ALL(10, mc00, mmxext)
LUMA_MC_ALL(10, mc10, mmxext)
LUMA_MC_ALL(10, mc20, mmxext)
LUMA_MC_ALL(10, mc30, mmxext)
LUMA_MC_ALL(10, mc01, mmxext)
LUMA_MC_ALL(10, mc11, mmxext)
LUMA_MC_ALL(10, mc21, mmxext)
LUMA_MC_ALL(10, mc31, mmxext)
LUMA_MC_ALL(10, mc02, mmxext)
LUMA_MC_ALL(10, mc12, mmxext)
LUMA_MC_ALL(10, mc22, mmxext)
LUMA_MC_ALL(10, mc32, mmxext)
LUMA_MC_ALL(10, mc03, mmxext)
LUMA_MC_ALL(10, mc13, mmxext)
LUMA_MC_ALL(10, mc23, mmxext)
LUMA_MC_ALL(10, mc33, mmxext)

LUMA_MC_816(10, mc00, sse2)
LUMA_MC_816(10, mc10, sse2)
LUMA_MC_816(10, mc10, sse2_cache64)
LUMA_MC_816(10, mc10, ssse3_cache64)
LUMA_MC_816(10, mc20, sse2)
LUMA_MC_816(10, mc20, sse2_cache64)
LUMA_MC_816(10, mc20, ssse3_cache64)
LUMA_MC_816(10, mc30, sse2)
LUMA_MC_816(10, mc30, sse2_cache64)
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

#define QPEL16_OPMC(OP, MC, MMX)\
void ff_ ## OP ## _h264_qpel16_ ## MC ## _10_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t stride){\
    ff_ ## OP ## _h264_qpel8_ ## MC ## _10_ ## MMX(dst   , src   , stride);\
    ff_ ## OP ## _h264_qpel8_ ## MC ## _10_ ## MMX(dst+16, src+16, stride);\
    src += 8*stride;\
    dst += 8*stride;\
    ff_ ## OP ## _h264_qpel8_ ## MC ## _10_ ## MMX(dst   , src   , stride);\
    ff_ ## OP ## _h264_qpel8_ ## MC ## _10_ ## MMX(dst+16, src+16, stride);\
}

#define QPEL16_OP(MC, MMX)\
QPEL16_OPMC(put, MC, MMX)\
QPEL16_OPMC(avg, MC, MMX)

#define QPEL16(MMX)\
QPEL16_OP(mc00, MMX)\
QPEL16_OP(mc01, MMX)\
QPEL16_OP(mc02, MMX)\
QPEL16_OP(mc03, MMX)\
QPEL16_OP(mc10, MMX)\
QPEL16_OP(mc11, MMX)\
QPEL16_OP(mc12, MMX)\
QPEL16_OP(mc13, MMX)\
QPEL16_OP(mc20, MMX)\
QPEL16_OP(mc21, MMX)\
QPEL16_OP(mc22, MMX)\
QPEL16_OP(mc23, MMX)\
QPEL16_OP(mc30, MMX)\
QPEL16_OP(mc31, MMX)\
QPEL16_OP(mc32, MMX)\
QPEL16_OP(mc33, MMX)

#if ARCH_X86_32 // ARCH_X86_64 implies SSE2+
QPEL16(mmxext)
#endif

#endif /* HAVE_YASM */

#define SET_QPEL_FUNCS(PFX, IDX, SIZE, CPU, PREFIX)                          \
    do {                                                                     \
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
#if HAVE_YASM
    int high_bit_depth = bit_depth > 8;
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_MMXEXT(cpu_flags)) {
        if (!high_bit_depth) {
            SET_QPEL_FUNCS(put_h264_qpel, 0, 16, mmxext, );
            SET_QPEL_FUNCS(put_h264_qpel, 1,  8, mmxext, );
            SET_QPEL_FUNCS(put_h264_qpel, 2,  4, mmxext, );
            SET_QPEL_FUNCS(avg_h264_qpel, 0, 16, mmxext, );
            SET_QPEL_FUNCS(avg_h264_qpel, 1,  8, mmxext, );
            SET_QPEL_FUNCS(avg_h264_qpel, 2,  4, mmxext, );
        } else if (bit_depth == 10) {
#if ARCH_X86_32
            SET_QPEL_FUNCS(avg_h264_qpel, 0, 16, 10_mmxext, ff_);
            SET_QPEL_FUNCS(put_h264_qpel, 0, 16, 10_mmxext, ff_);
            SET_QPEL_FUNCS(put_h264_qpel, 1,  8, 10_mmxext, ff_);
            SET_QPEL_FUNCS(avg_h264_qpel, 1,  8, 10_mmxext, ff_);
#endif
            SET_QPEL_FUNCS(put_h264_qpel, 2, 4,  10_mmxext, ff_);
            SET_QPEL_FUNCS(avg_h264_qpel, 2, 4,  10_mmxext, ff_);
        }
    }

    if (EXTERNAL_SSE2(cpu_flags)) {
        if (!(cpu_flags & AV_CPU_FLAG_SSE2SLOW) && !high_bit_depth) {
            // these functions are slower than mmx on AMD, but faster on Intel
            H264_QPEL_FUNCS(0, 0, sse2);
        }

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
        }

        if (bit_depth == 10) {
            SET_QPEL_FUNCS(put_h264_qpel, 0, 16, 10_sse2, ff_);
            SET_QPEL_FUNCS(put_h264_qpel, 1,  8, 10_sse2, ff_);
            SET_QPEL_FUNCS(avg_h264_qpel, 0, 16, 10_sse2, ff_);
            SET_QPEL_FUNCS(avg_h264_qpel, 1,  8, 10_sse2, ff_);
            H264_QPEL_FUNCS_10(1, 0, sse2_cache64);
            H264_QPEL_FUNCS_10(2, 0, sse2_cache64);
            H264_QPEL_FUNCS_10(3, 0, sse2_cache64);
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

    if (EXTERNAL_AVX(cpu_flags)) {
        /* AVX implies 64 byte cache lines without the need to avoid unaligned
         * memory accesses that cross the boundary between two cache lines.
         * TODO: Port X264_CPU_CACHELINE_32/64 detection from x264 to avoid
         * having to treat SSE2 functions with such properties as AVX. */
        if (bit_depth == 10) {
            H264_QPEL_FUNCS_10(1, 0, sse2);
            H264_QPEL_FUNCS_10(2, 0, sse2);
            H264_QPEL_FUNCS_10(3, 0, sse2);
        }
    }
#endif
}
