/*
 * RV40 decoder motion compensation functions
 * Copyright (c) 2008 Konstantin Shishkov
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

/**
 * @file
 * RV40 decoder motion compensation functions
 */

#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"
#include "avcodec.h"
#include "h264qpel.h"
#include "mathops.h"
#include "pixels.h"
#include "rnd_avg.h"
#include "rv34dsp.h"
#include "libavutil/avassert.h"

#define RV40_LOWPASS(OPNAME, OP) \
static void OPNAME ## rv40_qpel8_h_lowpass(uint8_t *dst, const uint8_t *src, int dstStride, int srcStride,\
                                                     const int h, const int C1, const int C2, const int SHIFT){\
    const uint8_t *cm = ff_crop_tab + MAX_NEG_CROP;\
    int i;\
    for(i = 0; i < h; i++)\
    {\
        OP(dst[0], (src[-2] + src[ 3] - 5*(src[-1]+src[2]) + src[0]*C1 + src[1]*C2 + (1<<(SHIFT-1))) >> SHIFT);\
        OP(dst[1], (src[-1] + src[ 4] - 5*(src[ 0]+src[3]) + src[1]*C1 + src[2]*C2 + (1<<(SHIFT-1))) >> SHIFT);\
        OP(dst[2], (src[ 0] + src[ 5] - 5*(src[ 1]+src[4]) + src[2]*C1 + src[3]*C2 + (1<<(SHIFT-1))) >> SHIFT);\
        OP(dst[3], (src[ 1] + src[ 6] - 5*(src[ 2]+src[5]) + src[3]*C1 + src[4]*C2 + (1<<(SHIFT-1))) >> SHIFT);\
        OP(dst[4], (src[ 2] + src[ 7] - 5*(src[ 3]+src[6]) + src[4]*C1 + src[5]*C2 + (1<<(SHIFT-1))) >> SHIFT);\
        OP(dst[5], (src[ 3] + src[ 8] - 5*(src[ 4]+src[7]) + src[5]*C1 + src[6]*C2 + (1<<(SHIFT-1))) >> SHIFT);\
        OP(dst[6], (src[ 4] + src[ 9] - 5*(src[ 5]+src[8]) + src[6]*C1 + src[7]*C2 + (1<<(SHIFT-1))) >> SHIFT);\
        OP(dst[7], (src[ 5] + src[10] - 5*(src[ 6]+src[9]) + src[7]*C1 + src[8]*C2 + (1<<(SHIFT-1))) >> SHIFT);\
        dst += dstStride;\
        src += srcStride;\
    }\
}\
\
static void OPNAME ## rv40_qpel8_v_lowpass(uint8_t *dst, const uint8_t *src, int dstStride, int srcStride,\
                                           const int w, const int C1, const int C2, const int SHIFT){\
    const uint8_t *cm = ff_crop_tab + MAX_NEG_CROP;\
    int i;\
    for(i = 0; i < w; i++)\
    {\
        const int srcB  = src[-2*srcStride];\
        const int srcA  = src[-1*srcStride];\
        const int src0  = src[0 *srcStride];\
        const int src1  = src[1 *srcStride];\
        const int src2  = src[2 *srcStride];\
        const int src3  = src[3 *srcStride];\
        const int src4  = src[4 *srcStride];\
        const int src5  = src[5 *srcStride];\
        const int src6  = src[6 *srcStride];\
        const int src7  = src[7 *srcStride];\
        const int src8  = src[8 *srcStride];\
        const int src9  = src[9 *srcStride];\
        const int src10 = src[10*srcStride];\
        OP(dst[0*dstStride], (srcB + src3  - 5*(srcA+src2) + src0*C1 + src1*C2 + (1<<(SHIFT-1))) >> SHIFT);\
        OP(dst[1*dstStride], (srcA + src4  - 5*(src0+src3) + src1*C1 + src2*C2 + (1<<(SHIFT-1))) >> SHIFT);\
        OP(dst[2*dstStride], (src0 + src5  - 5*(src1+src4) + src2*C1 + src3*C2 + (1<<(SHIFT-1))) >> SHIFT);\
        OP(dst[3*dstStride], (src1 + src6  - 5*(src2+src5) + src3*C1 + src4*C2 + (1<<(SHIFT-1))) >> SHIFT);\
        OP(dst[4*dstStride], (src2 + src7  - 5*(src3+src6) + src4*C1 + src5*C2 + (1<<(SHIFT-1))) >> SHIFT);\
        OP(dst[5*dstStride], (src3 + src8  - 5*(src4+src7) + src5*C1 + src6*C2 + (1<<(SHIFT-1))) >> SHIFT);\
        OP(dst[6*dstStride], (src4 + src9  - 5*(src5+src8) + src6*C1 + src7*C2 + (1<<(SHIFT-1))) >> SHIFT);\
        OP(dst[7*dstStride], (src5 + src10 - 5*(src6+src9) + src7*C1 + src8*C2 + (1<<(SHIFT-1))) >> SHIFT);\
        dst++;\
        src++;\
    }\
}\
\
static void OPNAME ## rv40_qpel16_v_lowpass(uint8_t *dst, const uint8_t *src, int dstStride, int srcStride,\
                                            const int w, const int C1, const int C2, const int SHIFT){\
    OPNAME ## rv40_qpel8_v_lowpass(dst  , src  , dstStride, srcStride, 8, C1, C2, SHIFT);\
    OPNAME ## rv40_qpel8_v_lowpass(dst+8, src+8, dstStride, srcStride, 8, C1, C2, SHIFT);\
    src += 8*srcStride;\
    dst += 8*dstStride;\
    OPNAME ## rv40_qpel8_v_lowpass(dst  , src  , dstStride, srcStride, w-8, C1, C2, SHIFT);\
    OPNAME ## rv40_qpel8_v_lowpass(dst+8, src+8, dstStride, srcStride, w-8, C1, C2, SHIFT);\
}\
\
static void OPNAME ## rv40_qpel16_h_lowpass(uint8_t *dst, const uint8_t *src, int dstStride, int srcStride,\
                                            const int h, const int C1, const int C2, const int SHIFT){\
    OPNAME ## rv40_qpel8_h_lowpass(dst  , src  , dstStride, srcStride, 8, C1, C2, SHIFT);\
    OPNAME ## rv40_qpel8_h_lowpass(dst+8, src+8, dstStride, srcStride, 8, C1, C2, SHIFT);\
    src += 8*srcStride;\
    dst += 8*dstStride;\
    OPNAME ## rv40_qpel8_h_lowpass(dst  , src  , dstStride, srcStride, h-8, C1, C2, SHIFT);\
    OPNAME ## rv40_qpel8_h_lowpass(dst+8, src+8, dstStride, srcStride, h-8, C1, C2, SHIFT);\
}\
\

#define RV40_MC(OPNAME, SIZE) \
static void OPNAME ## rv40_qpel ## SIZE ## _mc10_c(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
    OPNAME ## rv40_qpel ## SIZE ## _h_lowpass(dst, src, stride, stride, SIZE, 52, 20, 6);\
}\
\
static void OPNAME ## rv40_qpel ## SIZE ## _mc30_c(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
    OPNAME ## rv40_qpel ## SIZE ## _h_lowpass(dst, src, stride, stride, SIZE, 20, 52, 6);\
}\
\
static void OPNAME ## rv40_qpel ## SIZE ## _mc01_c(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
    OPNAME ## rv40_qpel ## SIZE ## _v_lowpass(dst, src, stride, stride, SIZE, 52, 20, 6);\
}\
\
static void OPNAME ## rv40_qpel ## SIZE ## _mc11_c(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
    uint8_t full[SIZE*(SIZE+5)];\
    uint8_t * const full_mid = full + SIZE*2;\
    put_rv40_qpel ## SIZE ## _h_lowpass(full, src - 2*stride, SIZE, stride, SIZE+5, 52, 20, 6);\
    OPNAME ## rv40_qpel ## SIZE ## _v_lowpass(dst, full_mid, stride, SIZE, SIZE, 52, 20, 6);\
}\
\
static void OPNAME ## rv40_qpel ## SIZE ## _mc21_c(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
    uint8_t full[SIZE*(SIZE+5)];\
    uint8_t * const full_mid = full + SIZE*2;\
    put_rv40_qpel ## SIZE ## _h_lowpass(full, src - 2*stride, SIZE, stride, SIZE+5, 20, 20, 5);\
    OPNAME ## rv40_qpel ## SIZE ## _v_lowpass(dst, full_mid, stride, SIZE, SIZE, 52, 20, 6);\
}\
\
static void OPNAME ## rv40_qpel ## SIZE ## _mc31_c(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
    uint8_t full[SIZE*(SIZE+5)];\
    uint8_t * const full_mid = full + SIZE*2;\
    put_rv40_qpel ## SIZE ## _h_lowpass(full, src - 2*stride, SIZE, stride, SIZE+5, 20, 52, 6);\
    OPNAME ## rv40_qpel ## SIZE ## _v_lowpass(dst, full_mid, stride, SIZE, SIZE, 52, 20, 6);\
}\
\
static void OPNAME ## rv40_qpel ## SIZE ## _mc12_c(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
    uint8_t full[SIZE*(SIZE+5)];\
    uint8_t * const full_mid = full + SIZE*2;\
    put_rv40_qpel ## SIZE ## _h_lowpass(full, src - 2*stride, SIZE, stride, SIZE+5, 52, 20, 6);\
    OPNAME ## rv40_qpel ## SIZE ## _v_lowpass(dst, full_mid, stride, SIZE, SIZE, 20, 20, 5);\
}\
\
static void OPNAME ## rv40_qpel ## SIZE ## _mc22_c(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
    uint8_t full[SIZE*(SIZE+5)];\
    uint8_t * const full_mid = full + SIZE*2;\
    put_rv40_qpel ## SIZE ## _h_lowpass(full, src - 2*stride, SIZE, stride, SIZE+5, 20, 20, 5);\
    OPNAME ## rv40_qpel ## SIZE ## _v_lowpass(dst, full_mid, stride, SIZE, SIZE, 20, 20, 5);\
}\
\
static void OPNAME ## rv40_qpel ## SIZE ## _mc32_c(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
    uint8_t full[SIZE*(SIZE+5)];\
    uint8_t * const full_mid = full + SIZE*2;\
    put_rv40_qpel ## SIZE ## _h_lowpass(full, src - 2*stride, SIZE, stride, SIZE+5, 20, 52, 6);\
    OPNAME ## rv40_qpel ## SIZE ## _v_lowpass(dst, full_mid, stride, SIZE, SIZE, 20, 20, 5);\
}\
\
static void OPNAME ## rv40_qpel ## SIZE ## _mc03_c(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
    OPNAME ## rv40_qpel ## SIZE ## _v_lowpass(dst, src, stride, stride, SIZE, 20, 52, 6);\
}\
\
static void OPNAME ## rv40_qpel ## SIZE ## _mc13_c(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
    uint8_t full[SIZE*(SIZE+5)];\
    uint8_t * const full_mid = full + SIZE*2;\
    put_rv40_qpel ## SIZE ## _h_lowpass(full, src - 2*stride, SIZE, stride, SIZE+5, 52, 20, 6);\
    OPNAME ## rv40_qpel ## SIZE ## _v_lowpass(dst, full_mid, stride, SIZE, SIZE, 20, 52, 6);\
}\
\
static void OPNAME ## rv40_qpel ## SIZE ## _mc23_c(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
    uint8_t full[SIZE*(SIZE+5)];\
    uint8_t * const full_mid = full + SIZE*2;\
    put_rv40_qpel ## SIZE ## _h_lowpass(full, src - 2*stride, SIZE, stride, SIZE+5, 20, 20, 5);\
    OPNAME ## rv40_qpel ## SIZE ## _v_lowpass(dst, full_mid, stride, SIZE, SIZE, 20, 52, 6);\
}\
\

#define op_avg(a, b)  a = (((a)+cm[b]+1)>>1)
#define op_put(a, b)  a = cm[b]

RV40_LOWPASS(put_       , op_put)
RV40_LOWPASS(avg_       , op_avg)

#undef op_avg
#undef op_put

RV40_MC(put_, 8)
RV40_MC(put_, 16)
RV40_MC(avg_, 8)
RV40_MC(avg_, 16)

#define PIXOP2(OPNAME, OP)                                              \
static inline void OPNAME ## _pixels8_xy2_8_c(uint8_t *block,           \
                                              const uint8_t *pixels,    \
                                              ptrdiff_t line_size,      \
                                              int h)                    \
{                                                                       \
    /* FIXME HIGH BIT DEPTH */                                          \
    int j;                                                              \
                                                                        \
    for (j = 0; j < 2; j++) {                                           \
        int i;                                                          \
        const uint32_t a = AV_RN32(pixels);                             \
        const uint32_t b = AV_RN32(pixels + 1);                         \
        uint32_t l0 = (a & 0x03030303UL) +                              \
                      (b & 0x03030303UL) +                              \
                           0x02020202UL;                                \
        uint32_t h0 = ((a & 0xFCFCFCFCUL) >> 2) +                       \
                      ((b & 0xFCFCFCFCUL) >> 2);                        \
        uint32_t l1, h1;                                                \
                                                                        \
        pixels += line_size;                                            \
        for (i = 0; i < h; i += 2) {                                    \
            uint32_t a = AV_RN32(pixels);                               \
            uint32_t b = AV_RN32(pixels + 1);                           \
            l1 = (a & 0x03030303UL) +                                   \
                 (b & 0x03030303UL);                                    \
            h1 = ((a & 0xFCFCFCFCUL) >> 2) +                            \
                 ((b & 0xFCFCFCFCUL) >> 2);                             \
            OP(*((uint32_t *) block),                                   \
               h0 + h1 + (((l0 + l1) >> 2) & 0x0F0F0F0FUL));            \
            pixels += line_size;                                        \
            block  += line_size;                                        \
            a = AV_RN32(pixels);                                        \
            b = AV_RN32(pixels + 1);                                    \
            l0 = (a & 0x03030303UL) +                                   \
                 (b & 0x03030303UL) +                                   \
                      0x02020202UL;                                     \
            h0 = ((a & 0xFCFCFCFCUL) >> 2) +                            \
                 ((b & 0xFCFCFCFCUL) >> 2);                             \
            OP(*((uint32_t *) block),                                   \
               h0 + h1 + (((l0 + l1) >> 2) & 0x0F0F0F0FUL));            \
            pixels += line_size;                                        \
            block  += line_size;                                        \
        }                                                               \
        pixels += 4 - line_size * (h + 1);                              \
        block  += 4 - line_size * h;                                    \
    }                                                                   \
}                                                                       \
                                                                        \
CALL_2X_PIXELS(OPNAME ## _pixels16_xy2_8_c,                             \
               OPNAME ## _pixels8_xy2_8_c,                              \
               8)                                                       \

#define op_avg(a, b) a = rnd_avg32(a, b)
#define op_put(a, b) a = b
PIXOP2(avg, op_avg)
PIXOP2(put, op_put)
#undef op_avg
#undef op_put

static void put_rv40_qpel16_mc33_c(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)
{
    put_pixels16_xy2_8_c(dst, src, stride, 16);
}
static void avg_rv40_qpel16_mc33_c(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)
{
    avg_pixels16_xy2_8_c(dst, src, stride, 16);
}
static void put_rv40_qpel8_mc33_c(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)
{
    put_pixels8_xy2_8_c(dst, src, stride, 8);
}
static void avg_rv40_qpel8_mc33_c(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)
{
    avg_pixels8_xy2_8_c(dst, src, stride, 8);
}

static const int rv40_bias[4][4] = {
    {  0, 16, 32, 16 },
    { 32, 28, 32, 28 },
    {  0, 32, 16, 32 },
    { 32, 28, 32, 28 }
};

#define RV40_CHROMA_MC(OPNAME, OP)\
static void OPNAME ## rv40_chroma_mc4_c(uint8_t *dst/*align 8*/, uint8_t *src/*align 1*/, int stride, int h, int x, int y){\
    const int A = (8-x) * (8-y);\
    const int B = (  x) * (8-y);\
    const int C = (8-x) * (  y);\
    const int D = (  x) * (  y);\
    int i;\
    int bias = rv40_bias[y>>1][x>>1];\
    \
    av_assert2(x<8 && y<8 && x>=0 && y>=0);\
\
    if(D){\
        for(i = 0; i < h; i++){\
            OP(dst[0], (A*src[0] + B*src[1] + C*src[stride+0] + D*src[stride+1] + bias));\
            OP(dst[1], (A*src[1] + B*src[2] + C*src[stride+1] + D*src[stride+2] + bias));\
            OP(dst[2], (A*src[2] + B*src[3] + C*src[stride+2] + D*src[stride+3] + bias));\
            OP(dst[3], (A*src[3] + B*src[4] + C*src[stride+3] + D*src[stride+4] + bias));\
            dst += stride;\
            src += stride;\
        }\
    }else{\
        const int E = B + C;\
        const int step = C ? stride : 1;\
        for(i = 0; i < h; i++){\
            OP(dst[0], (A*src[0] + E*src[step+0] + bias));\
            OP(dst[1], (A*src[1] + E*src[step+1] + bias));\
            OP(dst[2], (A*src[2] + E*src[step+2] + bias));\
            OP(dst[3], (A*src[3] + E*src[step+3] + bias));\
            dst += stride;\
            src += stride;\
        }\
    }\
}\
\
static void OPNAME ## rv40_chroma_mc8_c(uint8_t *dst/*align 8*/, uint8_t *src/*align 1*/, int stride, int h, int x, int y){\
    const int A = (8-x) * (8-y);\
    const int B = (  x) * (8-y);\
    const int C = (8-x) * (  y);\
    const int D = (  x) * (  y);\
    int i;\
    int bias = rv40_bias[y>>1][x>>1];\
    \
    av_assert2(x<8 && y<8 && x>=0 && y>=0);\
\
    if(D){\
        for(i = 0; i < h; i++){\
            OP(dst[0], (A*src[0] + B*src[1] + C*src[stride+0] + D*src[stride+1] + bias));\
            OP(dst[1], (A*src[1] + B*src[2] + C*src[stride+1] + D*src[stride+2] + bias));\
            OP(dst[2], (A*src[2] + B*src[3] + C*src[stride+2] + D*src[stride+3] + bias));\
            OP(dst[3], (A*src[3] + B*src[4] + C*src[stride+3] + D*src[stride+4] + bias));\
            OP(dst[4], (A*src[4] + B*src[5] + C*src[stride+4] + D*src[stride+5] + bias));\
            OP(dst[5], (A*src[5] + B*src[6] + C*src[stride+5] + D*src[stride+6] + bias));\
            OP(dst[6], (A*src[6] + B*src[7] + C*src[stride+6] + D*src[stride+7] + bias));\
            OP(dst[7], (A*src[7] + B*src[8] + C*src[stride+7] + D*src[stride+8] + bias));\
            dst += stride;\
            src += stride;\
        }\
    }else{\
        const int E = B + C;\
        const int step = C ? stride : 1;\
        for(i = 0; i < h; i++){\
            OP(dst[0], (A*src[0] + E*src[step+0] + bias));\
            OP(dst[1], (A*src[1] + E*src[step+1] + bias));\
            OP(dst[2], (A*src[2] + E*src[step+2] + bias));\
            OP(dst[3], (A*src[3] + E*src[step+3] + bias));\
            OP(dst[4], (A*src[4] + E*src[step+4] + bias));\
            OP(dst[5], (A*src[5] + E*src[step+5] + bias));\
            OP(dst[6], (A*src[6] + E*src[step+6] + bias));\
            OP(dst[7], (A*src[7] + E*src[step+7] + bias));\
            dst += stride;\
            src += stride;\
        }\
    }\
}

#define op_avg(a, b) a = (((a)+((b)>>6)+1)>>1)
#define op_put(a, b) a = ((b)>>6)

RV40_CHROMA_MC(put_, op_put)
RV40_CHROMA_MC(avg_, op_avg)

#define RV40_WEIGHT_FUNC(size) \
static void rv40_weight_func_rnd_ ## size (uint8_t *dst, uint8_t *src1, uint8_t *src2, int w1, int w2, ptrdiff_t stride)\
{\
    int i, j;\
\
    for (j = 0; j < size; j++) {\
        for (i = 0; i < size; i++)\
            dst[i] = (((w2 * src1[i]) >> 9) + ((w1 * src2[i]) >> 9) + 0x10) >> 5;\
        src1 += stride;\
        src2 += stride;\
        dst  += stride;\
    }\
}\
static void rv40_weight_func_nornd_ ## size (uint8_t *dst, uint8_t *src1, uint8_t *src2, int w1, int w2, ptrdiff_t stride)\
{\
    int i, j;\
\
    for (j = 0; j < size; j++) {\
        for (i = 0; i < size; i++)\
            dst[i] = (w2 * src1[i] + w1 * src2[i] + 0x10) >> 5;\
        src1 += stride;\
        src2 += stride;\
        dst  += stride;\
    }\
}

RV40_WEIGHT_FUNC(16)
RV40_WEIGHT_FUNC(8)

/**
 * dither values for deblocking filter - left/top values
 */
static const uint8_t rv40_dither_l[16] = {
    0x40, 0x50, 0x20, 0x60, 0x30, 0x50, 0x40, 0x30,
    0x50, 0x40, 0x50, 0x30, 0x60, 0x20, 0x50, 0x40
};

/**
 * dither values for deblocking filter - right/bottom values
 */
static const uint8_t rv40_dither_r[16] = {
    0x40, 0x30, 0x60, 0x20, 0x50, 0x30, 0x30, 0x40,
    0x40, 0x40, 0x50, 0x30, 0x20, 0x60, 0x30, 0x40
};

#define CLIP_SYMM(a, b) av_clip(a, -(b), b)
/**
 * weaker deblocking very similar to the one described in 4.4.2 of JVT-A003r1
 */
static av_always_inline void rv40_weak_loop_filter(uint8_t *src,
                                                   const int step,
                                                   const ptrdiff_t stride,
                                                   const int filter_p1,
                                                   const int filter_q1,
                                                   const int alpha,
                                                   const int beta,
                                                   const int lim_p0q0,
                                                   const int lim_q1,
                                                   const int lim_p1)
{
    const uint8_t *cm = ff_crop_tab + MAX_NEG_CROP;
    int i, t, u, diff;

    for (i = 0; i < 4; i++, src += stride) {
        int diff_p1p0 = src[-2*step] - src[-1*step];
        int diff_q1q0 = src[ 1*step] - src[ 0*step];
        int diff_p1p2 = src[-2*step] - src[-3*step];
        int diff_q1q2 = src[ 1*step] - src[ 2*step];

        t = src[0*step] - src[-1*step];
        if (!t)
            continue;

        u = (alpha * FFABS(t)) >> 7;
        if (u > 3 - (filter_p1 && filter_q1))
            continue;

        t <<= 2;
        if (filter_p1 && filter_q1)
            t += src[-2*step] - src[1*step];

        diff = CLIP_SYMM((t + 4) >> 3, lim_p0q0);
        src[-1*step] = cm[src[-1*step] + diff];
        src[ 0*step] = cm[src[ 0*step] - diff];

        if (filter_p1 && FFABS(diff_p1p2) <= beta) {
            t = (diff_p1p0 + diff_p1p2 - diff) >> 1;
            src[-2*step] = cm[src[-2*step] - CLIP_SYMM(t, lim_p1)];
        }

        if (filter_q1 && FFABS(diff_q1q2) <= beta) {
            t = (diff_q1q0 + diff_q1q2 + diff) >> 1;
            src[ 1*step] = cm[src[ 1*step] - CLIP_SYMM(t, lim_q1)];
        }
    }
}

static void rv40_h_weak_loop_filter(uint8_t *src, const ptrdiff_t stride,
                                    const int filter_p1, const int filter_q1,
                                    const int alpha, const int beta,
                                    const int lim_p0q0, const int lim_q1,
                                    const int lim_p1)
{
    rv40_weak_loop_filter(src, stride, 1, filter_p1, filter_q1,
                          alpha, beta, lim_p0q0, lim_q1, lim_p1);
}

static void rv40_v_weak_loop_filter(uint8_t *src, const ptrdiff_t stride,
                                    const int filter_p1, const int filter_q1,
                                    const int alpha, const int beta,
                                    const int lim_p0q0, const int lim_q1,
                                    const int lim_p1)
{
    rv40_weak_loop_filter(src, 1, stride, filter_p1, filter_q1,
                          alpha, beta, lim_p0q0, lim_q1, lim_p1);
}

static av_always_inline void rv40_strong_loop_filter(uint8_t *src,
                                                     const int step,
                                                     const ptrdiff_t stride,
                                                     const int alpha,
                                                     const int lims,
                                                     const int dmode,
                                                     const int chroma)
{
    int i;

    for(i = 0; i < 4; i++, src += stride){
        int sflag, p0, q0, p1, q1;
        int t = src[0*step] - src[-1*step];

        if (!t)
            continue;

        sflag = (alpha * FFABS(t)) >> 7;
        if (sflag > 1)
            continue;

        p0 = (25*src[-3*step] + 26*src[-2*step] + 26*src[-1*step] +
              26*src[ 0*step] + 25*src[ 1*step] +
              rv40_dither_l[dmode + i]) >> 7;

        q0 = (25*src[-2*step] + 26*src[-1*step] + 26*src[ 0*step] +
              26*src[ 1*step] + 25*src[ 2*step] +
              rv40_dither_r[dmode + i]) >> 7;

        if (sflag) {
            p0 = av_clip(p0, src[-1*step] - lims, src[-1*step] + lims);
            q0 = av_clip(q0, src[ 0*step] - lims, src[ 0*step] + lims);
        }

        p1 = (25*src[-4*step] + 26*src[-3*step] + 26*src[-2*step] + 26*p0 +
              25*src[ 0*step] + rv40_dither_l[dmode + i]) >> 7;
        q1 = (25*src[-1*step] + 26*q0 + 26*src[ 1*step] + 26*src[ 2*step] +
              25*src[ 3*step] + rv40_dither_r[dmode + i]) >> 7;

        if (sflag) {
            p1 = av_clip(p1, src[-2*step] - lims, src[-2*step] + lims);
            q1 = av_clip(q1, src[ 1*step] - lims, src[ 1*step] + lims);
        }

        src[-2*step] = p1;
        src[-1*step] = p0;
        src[ 0*step] = q0;
        src[ 1*step] = q1;

        if(!chroma){
            src[-3*step] = (25*src[-1*step] + 26*src[-2*step] +
                            51*src[-3*step] + 26*src[-4*step] + 64) >> 7;
            src[ 2*step] = (25*src[ 0*step] + 26*src[ 1*step] +
                            51*src[ 2*step] + 26*src[ 3*step] + 64) >> 7;
        }
    }
}

static void rv40_h_strong_loop_filter(uint8_t *src, const ptrdiff_t stride,
                                      const int alpha, const int lims,
                                      const int dmode, const int chroma)
{
    rv40_strong_loop_filter(src, stride, 1, alpha, lims, dmode, chroma);
}

static void rv40_v_strong_loop_filter(uint8_t *src, const ptrdiff_t stride,
                                      const int alpha, const int lims,
                                      const int dmode, const int chroma)
{
    rv40_strong_loop_filter(src, 1, stride, alpha, lims, dmode, chroma);
}

static av_always_inline int rv40_loop_filter_strength(uint8_t *src,
                                                      int step, ptrdiff_t stride,
                                                      int beta, int beta2,
                                                      int edge,
                                                      int *p1, int *q1)
{
    int sum_p1p0 = 0, sum_q1q0 = 0, sum_p1p2 = 0, sum_q1q2 = 0;
    int strong0 = 0, strong1 = 0;
    uint8_t *ptr;
    int i;

    for (i = 0, ptr = src; i < 4; i++, ptr += stride) {
        sum_p1p0 += ptr[-2*step] - ptr[-1*step];
        sum_q1q0 += ptr[ 1*step] - ptr[ 0*step];
    }

    *p1 = FFABS(sum_p1p0) < (beta << 2);
    *q1 = FFABS(sum_q1q0) < (beta << 2);

    if(!*p1 && !*q1)
        return 0;

    if (!edge)
        return 0;

    for (i = 0, ptr = src; i < 4; i++, ptr += stride) {
        sum_p1p2 += ptr[-2*step] - ptr[-3*step];
        sum_q1q2 += ptr[ 1*step] - ptr[ 2*step];
    }

    strong0 = *p1 && (FFABS(sum_p1p2) < beta2);
    strong1 = *q1 && (FFABS(sum_q1q2) < beta2);

    return strong0 && strong1;
}

static int rv40_h_loop_filter_strength(uint8_t *src, ptrdiff_t stride,
                                       int beta, int beta2, int edge,
                                       int *p1, int *q1)
{
    return rv40_loop_filter_strength(src, stride, 1, beta, beta2, edge, p1, q1);
}

static int rv40_v_loop_filter_strength(uint8_t *src, ptrdiff_t stride,
                                       int beta, int beta2, int edge,
                                       int *p1, int *q1)
{
    return rv40_loop_filter_strength(src, 1, stride, beta, beta2, edge, p1, q1);
}

av_cold void ff_rv40dsp_init(RV34DSPContext *c)
{
    H264QpelContext qpel;

    ff_rv34dsp_init(c);
    ff_h264qpel_init(&qpel, 8);

    c->put_pixels_tab[0][ 0] = qpel.put_h264_qpel_pixels_tab[0][0];
    c->put_pixels_tab[0][ 1] = put_rv40_qpel16_mc10_c;
    c->put_pixels_tab[0][ 2] = qpel.put_h264_qpel_pixels_tab[0][2];
    c->put_pixels_tab[0][ 3] = put_rv40_qpel16_mc30_c;
    c->put_pixels_tab[0][ 4] = put_rv40_qpel16_mc01_c;
    c->put_pixels_tab[0][ 5] = put_rv40_qpel16_mc11_c;
    c->put_pixels_tab[0][ 6] = put_rv40_qpel16_mc21_c;
    c->put_pixels_tab[0][ 7] = put_rv40_qpel16_mc31_c;
    c->put_pixels_tab[0][ 8] = qpel.put_h264_qpel_pixels_tab[0][8];
    c->put_pixels_tab[0][ 9] = put_rv40_qpel16_mc12_c;
    c->put_pixels_tab[0][10] = put_rv40_qpel16_mc22_c;
    c->put_pixels_tab[0][11] = put_rv40_qpel16_mc32_c;
    c->put_pixels_tab[0][12] = put_rv40_qpel16_mc03_c;
    c->put_pixels_tab[0][13] = put_rv40_qpel16_mc13_c;
    c->put_pixels_tab[0][14] = put_rv40_qpel16_mc23_c;
    c->put_pixels_tab[0][15] = put_rv40_qpel16_mc33_c;
    c->avg_pixels_tab[0][ 0] = qpel.avg_h264_qpel_pixels_tab[0][0];
    c->avg_pixels_tab[0][ 1] = avg_rv40_qpel16_mc10_c;
    c->avg_pixels_tab[0][ 2] = qpel.avg_h264_qpel_pixels_tab[0][2];
    c->avg_pixels_tab[0][ 3] = avg_rv40_qpel16_mc30_c;
    c->avg_pixels_tab[0][ 4] = avg_rv40_qpel16_mc01_c;
    c->avg_pixels_tab[0][ 5] = avg_rv40_qpel16_mc11_c;
    c->avg_pixels_tab[0][ 6] = avg_rv40_qpel16_mc21_c;
    c->avg_pixels_tab[0][ 7] = avg_rv40_qpel16_mc31_c;
    c->avg_pixels_tab[0][ 8] = qpel.avg_h264_qpel_pixels_tab[0][8];
    c->avg_pixels_tab[0][ 9] = avg_rv40_qpel16_mc12_c;
    c->avg_pixels_tab[0][10] = avg_rv40_qpel16_mc22_c;
    c->avg_pixels_tab[0][11] = avg_rv40_qpel16_mc32_c;
    c->avg_pixels_tab[0][12] = avg_rv40_qpel16_mc03_c;
    c->avg_pixels_tab[0][13] = avg_rv40_qpel16_mc13_c;
    c->avg_pixels_tab[0][14] = avg_rv40_qpel16_mc23_c;
    c->avg_pixels_tab[0][15] = avg_rv40_qpel16_mc33_c;
    c->put_pixels_tab[1][ 0] = qpel.put_h264_qpel_pixels_tab[1][0];
    c->put_pixels_tab[1][ 1] = put_rv40_qpel8_mc10_c;
    c->put_pixels_tab[1][ 2] = qpel.put_h264_qpel_pixels_tab[1][2];
    c->put_pixels_tab[1][ 3] = put_rv40_qpel8_mc30_c;
    c->put_pixels_tab[1][ 4] = put_rv40_qpel8_mc01_c;
    c->put_pixels_tab[1][ 5] = put_rv40_qpel8_mc11_c;
    c->put_pixels_tab[1][ 6] = put_rv40_qpel8_mc21_c;
    c->put_pixels_tab[1][ 7] = put_rv40_qpel8_mc31_c;
    c->put_pixels_tab[1][ 8] = qpel.put_h264_qpel_pixels_tab[1][8];
    c->put_pixels_tab[1][ 9] = put_rv40_qpel8_mc12_c;
    c->put_pixels_tab[1][10] = put_rv40_qpel8_mc22_c;
    c->put_pixels_tab[1][11] = put_rv40_qpel8_mc32_c;
    c->put_pixels_tab[1][12] = put_rv40_qpel8_mc03_c;
    c->put_pixels_tab[1][13] = put_rv40_qpel8_mc13_c;
    c->put_pixels_tab[1][14] = put_rv40_qpel8_mc23_c;
    c->put_pixels_tab[1][15] = put_rv40_qpel8_mc33_c;
    c->avg_pixels_tab[1][ 0] = qpel.avg_h264_qpel_pixels_tab[1][0];
    c->avg_pixels_tab[1][ 1] = avg_rv40_qpel8_mc10_c;
    c->avg_pixels_tab[1][ 2] = qpel.avg_h264_qpel_pixels_tab[1][2];
    c->avg_pixels_tab[1][ 3] = avg_rv40_qpel8_mc30_c;
    c->avg_pixels_tab[1][ 4] = avg_rv40_qpel8_mc01_c;
    c->avg_pixels_tab[1][ 5] = avg_rv40_qpel8_mc11_c;
    c->avg_pixels_tab[1][ 6] = avg_rv40_qpel8_mc21_c;
    c->avg_pixels_tab[1][ 7] = avg_rv40_qpel8_mc31_c;
    c->avg_pixels_tab[1][ 8] = qpel.avg_h264_qpel_pixels_tab[1][8];
    c->avg_pixels_tab[1][ 9] = avg_rv40_qpel8_mc12_c;
    c->avg_pixels_tab[1][10] = avg_rv40_qpel8_mc22_c;
    c->avg_pixels_tab[1][11] = avg_rv40_qpel8_mc32_c;
    c->avg_pixels_tab[1][12] = avg_rv40_qpel8_mc03_c;
    c->avg_pixels_tab[1][13] = avg_rv40_qpel8_mc13_c;
    c->avg_pixels_tab[1][14] = avg_rv40_qpel8_mc23_c;
    c->avg_pixels_tab[1][15] = avg_rv40_qpel8_mc33_c;

    c->put_chroma_pixels_tab[0] = put_rv40_chroma_mc8_c;
    c->put_chroma_pixels_tab[1] = put_rv40_chroma_mc4_c;
    c->avg_chroma_pixels_tab[0] = avg_rv40_chroma_mc8_c;
    c->avg_chroma_pixels_tab[1] = avg_rv40_chroma_mc4_c;

    c->rv40_weight_pixels_tab[0][0] = rv40_weight_func_rnd_16;
    c->rv40_weight_pixels_tab[0][1] = rv40_weight_func_rnd_8;
    c->rv40_weight_pixels_tab[1][0] = rv40_weight_func_nornd_16;
    c->rv40_weight_pixels_tab[1][1] = rv40_weight_func_nornd_8;

    c->rv40_weak_loop_filter[0]     = rv40_h_weak_loop_filter;
    c->rv40_weak_loop_filter[1]     = rv40_v_weak_loop_filter;
    c->rv40_strong_loop_filter[0]   = rv40_h_strong_loop_filter;
    c->rv40_strong_loop_filter[1]   = rv40_v_strong_loop_filter;
    c->rv40_loop_filter_strength[0] = rv40_h_loop_filter_strength;
    c->rv40_loop_filter_strength[1] = rv40_v_loop_filter_strength;

    if (ARCH_AARCH64)
        ff_rv40dsp_init_aarch64(c);
    if (ARCH_ARM)
        ff_rv40dsp_init_arm(c);
    if (ARCH_X86)
        ff_rv40dsp_init_x86(c);
}
