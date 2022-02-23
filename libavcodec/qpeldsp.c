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

/**
 * @file
 * quarterpel DSP functions
 */

#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "config_components.h"
#include "libavutil/attributes.h"
#include "copy_block.h"
#include "qpeldsp.h"
#include "diracdsp.h"

#define BIT_DEPTH 8
#include "hpel_template.c"
#include "pel_template.c"
#include "qpel_template.c"

#define QPEL_MC(r, OPNAME, RND, OP)                                           \
static void OPNAME ## mpeg4_qpel8_h_lowpass(uint8_t *dst, const uint8_t *src, \
                                            int dstStride, int srcStride,     \
                                            int h)                            \
{                                                                             \
    const uint8_t *cm = ff_crop_tab + MAX_NEG_CROP;                           \
    int i;                                                                    \
                                                                              \
    for (i = 0; i < h; i++) {                                                 \
        OP(dst[0], (src[0] + src[1]) * 20 - (src[0] + src[2]) * 6 + (src[1] + src[3]) * 3 - (src[2] + src[4])); \
        OP(dst[1], (src[1] + src[2]) * 20 - (src[0] + src[3]) * 6 + (src[0] + src[4]) * 3 - (src[1] + src[5])); \
        OP(dst[2], (src[2] + src[3]) * 20 - (src[1] + src[4]) * 6 + (src[0] + src[5]) * 3 - (src[0] + src[6])); \
        OP(dst[3], (src[3] + src[4]) * 20 - (src[2] + src[5]) * 6 + (src[1] + src[6]) * 3 - (src[0] + src[7])); \
        OP(dst[4], (src[4] + src[5]) * 20 - (src[3] + src[6]) * 6 + (src[2] + src[7]) * 3 - (src[1] + src[8])); \
        OP(dst[5], (src[5] + src[6]) * 20 - (src[4] + src[7]) * 6 + (src[3] + src[8]) * 3 - (src[2] + src[8])); \
        OP(dst[6], (src[6] + src[7]) * 20 - (src[5] + src[8]) * 6 + (src[4] + src[8]) * 3 - (src[3] + src[7])); \
        OP(dst[7], (src[7] + src[8]) * 20 - (src[6] + src[8]) * 6 + (src[5] + src[7]) * 3 - (src[4] + src[6])); \
        dst += dstStride;                                                     \
        src += srcStride;                                                     \
    }                                                                         \
}                                                                             \
                                                                              \
static void OPNAME ## mpeg4_qpel8_v_lowpass(uint8_t *dst, const uint8_t *src, \
                                            int dstStride, int srcStride)     \
{                                                                             \
    const uint8_t *cm = ff_crop_tab + MAX_NEG_CROP;                           \
    const int w = 8;                                                          \
    int i;                                                                    \
                                                                              \
    for (i = 0; i < w; i++) {                                                 \
        const int src0 = src[0 * srcStride];                                  \
        const int src1 = src[1 * srcStride];                                  \
        const int src2 = src[2 * srcStride];                                  \
        const int src3 = src[3 * srcStride];                                  \
        const int src4 = src[4 * srcStride];                                  \
        const int src5 = src[5 * srcStride];                                  \
        const int src6 = src[6 * srcStride];                                  \
        const int src7 = src[7 * srcStride];                                  \
        const int src8 = src[8 * srcStride];                                  \
        OP(dst[0 * dstStride], (src0 + src1) * 20 - (src0 + src2) * 6 + (src1 + src3) * 3 - (src2 + src4)); \
        OP(dst[1 * dstStride], (src1 + src2) * 20 - (src0 + src3) * 6 + (src0 + src4) * 3 - (src1 + src5)); \
        OP(dst[2 * dstStride], (src2 + src3) * 20 - (src1 + src4) * 6 + (src0 + src5) * 3 - (src0 + src6)); \
        OP(dst[3 * dstStride], (src3 + src4) * 20 - (src2 + src5) * 6 + (src1 + src6) * 3 - (src0 + src7)); \
        OP(dst[4 * dstStride], (src4 + src5) * 20 - (src3 + src6) * 6 + (src2 + src7) * 3 - (src1 + src8)); \
        OP(dst[5 * dstStride], (src5 + src6) * 20 - (src4 + src7) * 6 + (src3 + src8) * 3 - (src2 + src8)); \
        OP(dst[6 * dstStride], (src6 + src7) * 20 - (src5 + src8) * 6 + (src4 + src8) * 3 - (src3 + src7)); \
        OP(dst[7 * dstStride], (src7 + src8) * 20 - (src6 + src8) * 6 + (src5 + src7) * 3 - (src4 + src6)); \
        dst++;                                                                \
        src++;                                                                \
    }                                                                         \
}                                                                             \
                                                                              \
static void OPNAME ## mpeg4_qpel16_h_lowpass(uint8_t *dst,                    \
                                             const uint8_t *src,              \
                                             int dstStride, int srcStride,    \
                                             int h)                           \
{                                                                             \
    const uint8_t *cm = ff_crop_tab + MAX_NEG_CROP;                           \
    int i;                                                                    \
                                                                              \
    for (i = 0; i < h; i++) {                                                 \
        OP(dst[0],  (src[0]  + src[1])  * 20 - (src[0]  + src[2])  * 6 + (src[1]  + src[3])  * 3 - (src[2]  + src[4]));  \
        OP(dst[1],  (src[1]  + src[2])  * 20 - (src[0]  + src[3])  * 6 + (src[0]  + src[4])  * 3 - (src[1]  + src[5]));  \
        OP(dst[2],  (src[2]  + src[3])  * 20 - (src[1]  + src[4])  * 6 + (src[0]  + src[5])  * 3 - (src[0]  + src[6]));  \
        OP(dst[3],  (src[3]  + src[4])  * 20 - (src[2]  + src[5])  * 6 + (src[1]  + src[6])  * 3 - (src[0]  + src[7]));  \
        OP(dst[4],  (src[4]  + src[5])  * 20 - (src[3]  + src[6])  * 6 + (src[2]  + src[7])  * 3 - (src[1]  + src[8]));  \
        OP(dst[5],  (src[5]  + src[6])  * 20 - (src[4]  + src[7])  * 6 + (src[3]  + src[8])  * 3 - (src[2]  + src[9]));  \
        OP(dst[6],  (src[6]  + src[7])  * 20 - (src[5]  + src[8])  * 6 + (src[4]  + src[9])  * 3 - (src[3]  + src[10])); \
        OP(dst[7],  (src[7]  + src[8])  * 20 - (src[6]  + src[9])  * 6 + (src[5]  + src[10]) * 3 - (src[4]  + src[11])); \
        OP(dst[8],  (src[8]  + src[9])  * 20 - (src[7]  + src[10]) * 6 + (src[6]  + src[11]) * 3 - (src[5]  + src[12])); \
        OP(dst[9],  (src[9]  + src[10]) * 20 - (src[8]  + src[11]) * 6 + (src[7]  + src[12]) * 3 - (src[6]  + src[13])); \
        OP(dst[10], (src[10] + src[11]) * 20 - (src[9]  + src[12]) * 6 + (src[8]  + src[13]) * 3 - (src[7]  + src[14])); \
        OP(dst[11], (src[11] + src[12]) * 20 - (src[10] + src[13]) * 6 + (src[9]  + src[14]) * 3 - (src[8]  + src[15])); \
        OP(dst[12], (src[12] + src[13]) * 20 - (src[11] + src[14]) * 6 + (src[10] + src[15]) * 3 - (src[9]  + src[16])); \
        OP(dst[13], (src[13] + src[14]) * 20 - (src[12] + src[15]) * 6 + (src[11] + src[16]) * 3 - (src[10] + src[16])); \
        OP(dst[14], (src[14] + src[15]) * 20 - (src[13] + src[16]) * 6 + (src[12] + src[16]) * 3 - (src[11] + src[15])); \
        OP(dst[15], (src[15] + src[16]) * 20 - (src[14] + src[16]) * 6 + (src[13] + src[15]) * 3 - (src[12] + src[14])); \
        dst += dstStride;                                                     \
        src += srcStride;                                                     \
    }                                                                         \
}                                                                             \
                                                                              \
static void OPNAME ## mpeg4_qpel16_v_lowpass(uint8_t *dst,                    \
                                             const uint8_t *src,              \
                                             int dstStride, int srcStride)    \
{                                                                             \
    const uint8_t *cm = ff_crop_tab + MAX_NEG_CROP;                           \
    const int w = 16;                                                         \
    int i;                                                                    \
                                                                              \
    for (i = 0; i < w; i++) {                                                 \
        const int src0  = src[0  * srcStride];                                \
        const int src1  = src[1  * srcStride];                                \
        const int src2  = src[2  * srcStride];                                \
        const int src3  = src[3  * srcStride];                                \
        const int src4  = src[4  * srcStride];                                \
        const int src5  = src[5  * srcStride];                                \
        const int src6  = src[6  * srcStride];                                \
        const int src7  = src[7  * srcStride];                                \
        const int src8  = src[8  * srcStride];                                \
        const int src9  = src[9  * srcStride];                                \
        const int src10 = src[10 * srcStride];                                \
        const int src11 = src[11 * srcStride];                                \
        const int src12 = src[12 * srcStride];                                \
        const int src13 = src[13 * srcStride];                                \
        const int src14 = src[14 * srcStride];                                \
        const int src15 = src[15 * srcStride];                                \
        const int src16 = src[16 * srcStride];                                \
        OP(dst[0  * dstStride], (src0  + src1)  * 20 - (src0  + src2)  * 6 + (src1  + src3)  * 3 - (src2  + src4));  \
        OP(dst[1  * dstStride], (src1  + src2)  * 20 - (src0  + src3)  * 6 + (src0  + src4)  * 3 - (src1  + src5));  \
        OP(dst[2  * dstStride], (src2  + src3)  * 20 - (src1  + src4)  * 6 + (src0  + src5)  * 3 - (src0  + src6));  \
        OP(dst[3  * dstStride], (src3  + src4)  * 20 - (src2  + src5)  * 6 + (src1  + src6)  * 3 - (src0  + src7));  \
        OP(dst[4  * dstStride], (src4  + src5)  * 20 - (src3  + src6)  * 6 + (src2  + src7)  * 3 - (src1  + src8));  \
        OP(dst[5  * dstStride], (src5  + src6)  * 20 - (src4  + src7)  * 6 + (src3  + src8)  * 3 - (src2  + src9));  \
        OP(dst[6  * dstStride], (src6  + src7)  * 20 - (src5  + src8)  * 6 + (src4  + src9)  * 3 - (src3  + src10)); \
        OP(dst[7  * dstStride], (src7  + src8)  * 20 - (src6  + src9)  * 6 + (src5  + src10) * 3 - (src4  + src11)); \
        OP(dst[8  * dstStride], (src8  + src9)  * 20 - (src7  + src10) * 6 + (src6  + src11) * 3 - (src5  + src12)); \
        OP(dst[9  * dstStride], (src9  + src10) * 20 - (src8  + src11) * 6 + (src7  + src12) * 3 - (src6  + src13)); \
        OP(dst[10 * dstStride], (src10 + src11) * 20 - (src9  + src12) * 6 + (src8  + src13) * 3 - (src7  + src14)); \
        OP(dst[11 * dstStride], (src11 + src12) * 20 - (src10 + src13) * 6 + (src9  + src14) * 3 - (src8  + src15)); \
        OP(dst[12 * dstStride], (src12 + src13) * 20 - (src11 + src14) * 6 + (src10 + src15) * 3 - (src9  + src16)); \
        OP(dst[13 * dstStride], (src13 + src14) * 20 - (src12 + src15) * 6 + (src11 + src16) * 3 - (src10 + src16)); \
        OP(dst[14 * dstStride], (src14 + src15) * 20 - (src13 + src16) * 6 + (src12 + src16) * 3 - (src11 + src15)); \
        OP(dst[15 * dstStride], (src15 + src16) * 20 - (src14 + src16) * 6 + (src13 + src15) * 3 - (src12 + src14)); \
        dst++;                                                                \
        src++;                                                                \
    }                                                                         \
}                                                                             \
                                                                              \
static void OPNAME ## qpel8_mc10_c(uint8_t *dst, const uint8_t *src,          \
                                   ptrdiff_t stride)                          \
{                                                                             \
    uint8_t half[64];                                                         \
                                                                              \
    put ## RND ## mpeg4_qpel8_h_lowpass(half, src, 8, stride, 8);             \
    OPNAME ## pixels8_l2_8(dst, src, half, stride, stride, 8, 8);             \
}                                                                             \
                                                                              \
static void OPNAME ## qpel8_mc20_c(uint8_t *dst, const uint8_t *src,          \
                                   ptrdiff_t stride)                          \
{                                                                             \
    OPNAME ## mpeg4_qpel8_h_lowpass(dst, src, stride, stride, 8);             \
}                                                                             \
                                                                              \
static void OPNAME ## qpel8_mc30_c(uint8_t *dst, const uint8_t *src,          \
                                   ptrdiff_t stride)                          \
{                                                                             \
    uint8_t half[64];                                                         \
                                                                              \
    put ## RND ## mpeg4_qpel8_h_lowpass(half, src, 8, stride, 8);             \
    OPNAME ## pixels8_l2_8(dst, src + 1, half, stride, stride, 8, 8);         \
}                                                                             \
                                                                              \
static void OPNAME ## qpel8_mc01_c(uint8_t *dst, const uint8_t *src,          \
                                   ptrdiff_t stride)                          \
{                                                                             \
    uint8_t full[16 * 9];                                                     \
    uint8_t half[64];                                                         \
                                                                              \
    copy_block9(full, src, 16, stride, 9);                                    \
    put ## RND ## mpeg4_qpel8_v_lowpass(half, full, 8, 16);                   \
    OPNAME ## pixels8_l2_8(dst, full, half, stride, 16, 8, 8);                \
}                                                                             \
                                                                              \
static void OPNAME ## qpel8_mc02_c(uint8_t *dst, const uint8_t *src,          \
                                   ptrdiff_t stride)                          \
{                                                                             \
    uint8_t full[16 * 9];                                                     \
                                                                              \
    copy_block9(full, src, 16, stride, 9);                                    \
    OPNAME ## mpeg4_qpel8_v_lowpass(dst, full, stride, 16);                   \
}                                                                             \
                                                                              \
static void OPNAME ## qpel8_mc03_c(uint8_t *dst, const uint8_t *src,          \
                                   ptrdiff_t stride)                          \
{                                                                             \
    uint8_t full[16 * 9];                                                     \
    uint8_t half[64];                                                         \
                                                                              \
    copy_block9(full, src, 16, stride, 9);                                    \
    put ## RND ## mpeg4_qpel8_v_lowpass(half, full, 8, 16);                   \
    OPNAME ## pixels8_l2_8(dst, full + 16, half, stride, 16, 8, 8);           \
}                                                                             \
                                                                              \
void ff_ ## OPNAME ## qpel8_mc11_old_c(uint8_t *dst, const uint8_t *src,      \
                                       ptrdiff_t stride)                      \
{                                                                             \
    uint8_t full[16 * 9];                                                     \
    uint8_t halfH[72];                                                        \
    uint8_t halfV[64];                                                        \
    uint8_t halfHV[64];                                                       \
                                                                              \
    copy_block9(full, src, 16, stride, 9);                                    \
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, full, 8, 16, 9);               \
    put ## RND ## mpeg4_qpel8_v_lowpass(halfV, full, 8, 16);                  \
    put ## RND ## mpeg4_qpel8_v_lowpass(halfHV, halfH, 8, 8);                 \
    OPNAME ## pixels8_l4_8(dst, full, halfH, halfV, halfHV,                   \
                           stride, 16, 8, 8, 8, 8);                           \
}                                                                             \
                                                                              \
static void OPNAME ## qpel8_mc11_c(uint8_t *dst, const uint8_t *src,          \
                                   ptrdiff_t stride)                          \
{                                                                             \
    uint8_t full[16 * 9];                                                     \
    uint8_t halfH[72];                                                        \
    uint8_t halfHV[64];                                                       \
                                                                              \
    copy_block9(full, src, 16, stride, 9);                                    \
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, full, 8, 16, 9);               \
    put ## RND ## pixels8_l2_8(halfH, halfH, full, 8, 8, 16, 9);              \
    put ## RND ## mpeg4_qpel8_v_lowpass(halfHV, halfH, 8, 8);                 \
    OPNAME ## pixels8_l2_8(dst, halfH, halfHV, stride, 8, 8, 8);              \
}                                                                             \
                                                                              \
void ff_ ## OPNAME ## qpel8_mc31_old_c(uint8_t *dst, const uint8_t *src,      \
                                       ptrdiff_t stride)                      \
{                                                                             \
    uint8_t full[16 * 9];                                                     \
    uint8_t halfH[72];                                                        \
    uint8_t halfV[64];                                                        \
    uint8_t halfHV[64];                                                       \
                                                                              \
    copy_block9(full, src, 16, stride, 9);                                    \
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, full, 8, 16, 9);               \
    put ## RND ## mpeg4_qpel8_v_lowpass(halfV, full + 1, 8, 16);              \
    put ## RND ## mpeg4_qpel8_v_lowpass(halfHV, halfH, 8, 8);                 \
    OPNAME ## pixels8_l4_8(dst, full + 1, halfH, halfV, halfHV,               \
                           stride, 16, 8, 8, 8, 8);                           \
}                                                                             \
                                                                              \
static void OPNAME ## qpel8_mc31_c(uint8_t *dst, const uint8_t *src,          \
                                   ptrdiff_t stride)                          \
{                                                                             \
    uint8_t full[16 * 9];                                                     \
    uint8_t halfH[72];                                                        \
    uint8_t halfHV[64];                                                       \
                                                                              \
    copy_block9(full, src, 16, stride, 9);                                    \
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, full, 8, 16, 9);               \
    put ## RND ## pixels8_l2_8(halfH, halfH, full + 1, 8, 8, 16, 9);          \
    put ## RND ## mpeg4_qpel8_v_lowpass(halfHV, halfH, 8, 8);                 \
    OPNAME ## pixels8_l2_8(dst, halfH, halfHV, stride, 8, 8, 8);              \
}                                                                             \
                                                                              \
void ff_ ## OPNAME ## qpel8_mc13_old_c(uint8_t *dst, const uint8_t *src,      \
                                       ptrdiff_t stride)                      \
{                                                                             \
    uint8_t full[16 * 9];                                                     \
    uint8_t halfH[72];                                                        \
    uint8_t halfV[64];                                                        \
    uint8_t halfHV[64];                                                       \
                                                                              \
    copy_block9(full, src, 16, stride, 9);                                    \
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, full, 8, 16, 9);               \
    put ## RND ## mpeg4_qpel8_v_lowpass(halfV, full, 8, 16);                  \
    put ## RND ## mpeg4_qpel8_v_lowpass(halfHV, halfH, 8, 8);                 \
    OPNAME ## pixels8_l4_8(dst, full + 16, halfH + 8, halfV, halfHV,          \
                           stride, 16, 8, 8, 8, 8);                           \
}                                                                             \
                                                                              \
static void OPNAME ## qpel8_mc13_c(uint8_t *dst, const uint8_t *src,    \
                                   ptrdiff_t stride)                          \
{                                                                             \
    uint8_t full[16 * 9];                                                     \
    uint8_t halfH[72];                                                        \
    uint8_t halfHV[64];                                                       \
                                                                              \
    copy_block9(full, src, 16, stride, 9);                                    \
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, full, 8, 16, 9);               \
    put ## RND ## pixels8_l2_8(halfH, halfH, full, 8, 8, 16, 9);              \
    put ## RND ## mpeg4_qpel8_v_lowpass(halfHV, halfH, 8, 8);                 \
    OPNAME ## pixels8_l2_8(dst, halfH + 8, halfHV, stride, 8, 8, 8);          \
}                                                                             \
                                                                              \
void ff_ ## OPNAME ## qpel8_mc33_old_c(uint8_t *dst, const uint8_t *src,      \
                                       ptrdiff_t stride)                      \
{                                                                             \
    uint8_t full[16 * 9];                                                     \
    uint8_t halfH[72];                                                        \
    uint8_t halfV[64];                                                        \
    uint8_t halfHV[64];                                                       \
                                                                              \
    copy_block9(full, src, 16, stride, 9);                                    \
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, full, 8, 16, 9);               \
    put ## RND ## mpeg4_qpel8_v_lowpass(halfV, full + 1, 8, 16);              \
    put ## RND ## mpeg4_qpel8_v_lowpass(halfHV, halfH, 8, 8);                 \
    OPNAME ## pixels8_l4_8(dst, full + 17, halfH + 8, halfV, halfHV,          \
                           stride, 16, 8, 8, 8, 8);                           \
}                                                                             \
                                                                              \
static void OPNAME ## qpel8_mc33_c(uint8_t *dst, const uint8_t *src,          \
                                   ptrdiff_t stride)                          \
{                                                                             \
    uint8_t full[16 * 9];                                                     \
    uint8_t halfH[72];                                                        \
    uint8_t halfHV[64];                                                       \
                                                                              \
    copy_block9(full, src, 16, stride, 9);                                    \
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, full, 8, 16, 9);               \
    put ## RND ## pixels8_l2_8(halfH, halfH, full + 1, 8, 8, 16, 9);          \
    put ## RND ## mpeg4_qpel8_v_lowpass(halfHV, halfH, 8, 8);                 \
    OPNAME ## pixels8_l2_8(dst, halfH + 8, halfHV, stride, 8, 8, 8);          \
}                                                                             \
                                                                              \
static void OPNAME ## qpel8_mc21_c(uint8_t *dst, const uint8_t *src,          \
                                   ptrdiff_t stride)                          \
{                                                                             \
    uint8_t halfH[72];                                                        \
    uint8_t halfHV[64];                                                       \
                                                                              \
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, src, 8, stride, 9);            \
    put ## RND ## mpeg4_qpel8_v_lowpass(halfHV, halfH, 8, 8);                 \
    OPNAME ## pixels8_l2_8(dst, halfH, halfHV, stride, 8, 8, 8);              \
}                                                                             \
                                                                              \
static void OPNAME ## qpel8_mc23_c(uint8_t *dst, const uint8_t *src,          \
                                   ptrdiff_t stride)                          \
{                                                                             \
    uint8_t halfH[72];                                                        \
    uint8_t halfHV[64];                                                       \
                                                                              \
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, src, 8, stride, 9);            \
    put ## RND ## mpeg4_qpel8_v_lowpass(halfHV, halfH, 8, 8);                 \
    OPNAME ## pixels8_l2_8(dst, halfH + 8, halfHV, stride, 8, 8, 8);          \
}                                                                             \
                                                                              \
void ff_ ## OPNAME ## qpel8_mc12_old_c(uint8_t *dst, const uint8_t *src,      \
                                       ptrdiff_t stride)                      \
{                                                                             \
    uint8_t full[16 * 9];                                                     \
    uint8_t halfH[72];                                                        \
    uint8_t halfV[64];                                                        \
    uint8_t halfHV[64];                                                       \
                                                                              \
    copy_block9(full, src, 16, stride, 9);                                    \
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, full, 8, 16, 9);               \
    put ## RND ## mpeg4_qpel8_v_lowpass(halfV, full, 8, 16);                  \
    put ## RND ## mpeg4_qpel8_v_lowpass(halfHV, halfH, 8, 8);                 \
    OPNAME ## pixels8_l2_8(dst, halfV, halfHV, stride, 8, 8, 8);              \
}                                                                             \
                                                                              \
static void OPNAME ## qpel8_mc12_c(uint8_t *dst, const uint8_t *src,          \
                                   ptrdiff_t stride)                          \
{                                                                             \
    uint8_t full[16 * 9];                                                     \
    uint8_t halfH[72];                                                        \
                                                                              \
    copy_block9(full, src, 16, stride, 9);                                    \
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, full, 8, 16, 9);               \
    put ## RND ## pixels8_l2_8(halfH, halfH, full, 8, 8, 16, 9);              \
    OPNAME ## mpeg4_qpel8_v_lowpass(dst, halfH, stride, 8);                   \
}                                                                             \
                                                                              \
void ff_ ## OPNAME ## qpel8_mc32_old_c(uint8_t *dst, const uint8_t *src,      \
                                       ptrdiff_t stride)                      \
{                                                                             \
    uint8_t full[16 * 9];                                                     \
    uint8_t halfH[72];                                                        \
    uint8_t halfV[64];                                                        \
    uint8_t halfHV[64];                                                       \
                                                                              \
    copy_block9(full, src, 16, stride, 9);                                    \
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, full, 8, 16, 9);               \
    put ## RND ## mpeg4_qpel8_v_lowpass(halfV, full + 1, 8, 16);              \
    put ## RND ## mpeg4_qpel8_v_lowpass(halfHV, halfH, 8, 8);                 \
    OPNAME ## pixels8_l2_8(dst, halfV, halfHV, stride, 8, 8, 8);              \
}                                                                             \
                                                                              \
static void OPNAME ## qpel8_mc32_c(uint8_t *dst, const uint8_t *src,          \
                                   ptrdiff_t stride)                          \
{                                                                             \
    uint8_t full[16 * 9];                                                     \
    uint8_t halfH[72];                                                        \
                                                                              \
    copy_block9(full, src, 16, stride, 9);                                    \
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, full, 8, 16, 9);               \
    put ## RND ## pixels8_l2_8(halfH, halfH, full + 1, 8, 8, 16, 9);          \
    OPNAME ## mpeg4_qpel8_v_lowpass(dst, halfH, stride, 8);                   \
}                                                                             \
                                                                              \
static void OPNAME ## qpel8_mc22_c(uint8_t *dst, const uint8_t *src,          \
                                   ptrdiff_t stride)                          \
{                                                                             \
    uint8_t halfH[72];                                                        \
                                                                              \
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, src, 8, stride, 9);            \
    OPNAME ## mpeg4_qpel8_v_lowpass(dst, halfH, stride, 8);                   \
}                                                                             \
                                                                              \
static void OPNAME ## qpel16_mc10_c(uint8_t *dst, const uint8_t *src,         \
                                    ptrdiff_t stride)                         \
{                                                                             \
    uint8_t half[256];                                                        \
                                                                              \
    put ## RND ## mpeg4_qpel16_h_lowpass(half, src, 16, stride, 16);          \
    OPNAME ## pixels16_l2_8(dst, src, half, stride, stride, 16, 16);          \
}                                                                             \
                                                                              \
static void OPNAME ## qpel16_mc20_c(uint8_t *dst, const uint8_t *src,         \
                                    ptrdiff_t stride)                         \
{                                                                             \
    OPNAME ## mpeg4_qpel16_h_lowpass(dst, src, stride, stride, 16);           \
}                                                                             \
                                                                              \
static void OPNAME ## qpel16_mc30_c(uint8_t *dst, const uint8_t *src,         \
                                    ptrdiff_t stride)                         \
{                                                                             \
    uint8_t half[256];                                                        \
                                                                              \
    put ## RND ## mpeg4_qpel16_h_lowpass(half, src, 16, stride, 16);          \
    OPNAME ## pixels16_l2_8(dst, src + 1, half, stride, stride, 16, 16);      \
}                                                                             \
                                                                              \
static void OPNAME ## qpel16_mc01_c(uint8_t *dst, const uint8_t *src,         \
                                    ptrdiff_t stride)                         \
{                                                                             \
    uint8_t full[24 * 17];                                                    \
    uint8_t half[256];                                                        \
                                                                              \
    copy_block17(full, src, 24, stride, 17);                                  \
    put ## RND ## mpeg4_qpel16_v_lowpass(half, full, 16, 24);                 \
    OPNAME ## pixels16_l2_8(dst, full, half, stride, 24, 16, 16);             \
}                                                                             \
                                                                              \
static void OPNAME ## qpel16_mc02_c(uint8_t *dst, const uint8_t *src,         \
                                    ptrdiff_t stride)                         \
{                                                                             \
    uint8_t full[24 * 17];                                                    \
                                                                              \
    copy_block17(full, src, 24, stride, 17);                                  \
    OPNAME ## mpeg4_qpel16_v_lowpass(dst, full, stride, 24);                  \
}                                                                             \
                                                                              \
static void OPNAME ## qpel16_mc03_c(uint8_t *dst, const uint8_t *src,         \
                                    ptrdiff_t stride)                         \
{                                                                             \
    uint8_t full[24 * 17];                                                    \
    uint8_t half[256];                                                        \
                                                                              \
    copy_block17(full, src, 24, stride, 17);                                  \
    put ## RND ## mpeg4_qpel16_v_lowpass(half, full, 16, 24);                 \
    OPNAME ## pixels16_l2_8(dst, full + 24, half, stride, 24, 16, 16);        \
}                                                                             \
                                                                              \
void ff_ ## OPNAME ## qpel16_mc11_old_c(uint8_t *dst, const uint8_t *src,     \
                                        ptrdiff_t stride)                     \
{                                                                             \
    uint8_t full[24 * 17];                                                    \
    uint8_t halfH[272];                                                       \
    uint8_t halfV[256];                                                       \
    uint8_t halfHV[256];                                                      \
                                                                              \
    copy_block17(full, src, 24, stride, 17);                                  \
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, full, 16, 24, 17);            \
    put ## RND ## mpeg4_qpel16_v_lowpass(halfV, full, 16, 24);                \
    put ## RND ## mpeg4_qpel16_v_lowpass(halfHV, halfH, 16, 16);              \
    OPNAME ## pixels16_l4_8(dst, full, halfH, halfV, halfHV,                  \
                            stride, 24, 16, 16, 16, 16);                      \
}                                                                             \
                                                                              \
static void OPNAME ## qpel16_mc11_c(uint8_t *dst, const uint8_t *src,         \
                                    ptrdiff_t stride)                         \
{                                                                             \
    uint8_t full[24 * 17];                                                    \
    uint8_t halfH[272];                                                       \
    uint8_t halfHV[256];                                                      \
                                                                              \
    copy_block17(full, src, 24, stride, 17);                                  \
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, full, 16, 24, 17);            \
    put ## RND ## pixels16_l2_8(halfH, halfH, full, 16, 16, 24, 17);          \
    put ## RND ## mpeg4_qpel16_v_lowpass(halfHV, halfH, 16, 16);              \
    OPNAME ## pixels16_l2_8(dst, halfH, halfHV, stride, 16, 16, 16);          \
}                                                                             \
                                                                              \
void ff_ ## OPNAME ## qpel16_mc31_old_c(uint8_t *dst, const uint8_t *src,     \
                                        ptrdiff_t stride)                     \
{                                                                             \
    uint8_t full[24 * 17];                                                    \
    uint8_t halfH[272];                                                       \
    uint8_t halfV[256];                                                       \
    uint8_t halfHV[256];                                                      \
                                                                              \
    copy_block17(full, src, 24, stride, 17);                                  \
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, full, 16, 24, 17);            \
    put ## RND ## mpeg4_qpel16_v_lowpass(halfV, full + 1, 16, 24);            \
    put ## RND ## mpeg4_qpel16_v_lowpass(halfHV, halfH, 16, 16);              \
    OPNAME ## pixels16_l4_8(dst, full + 1, halfH, halfV, halfHV,              \
                            stride, 24, 16, 16, 16, 16);                      \
}                                                                             \
                                                                              \
static void OPNAME ## qpel16_mc31_c(uint8_t *dst, const uint8_t *src,         \
                                    ptrdiff_t stride)                         \
{                                                                             \
    uint8_t full[24 * 17];                                                    \
    uint8_t halfH[272];                                                       \
    uint8_t halfHV[256];                                                      \
                                                                              \
    copy_block17(full, src, 24, stride, 17);                                  \
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, full, 16, 24, 17);            \
    put ## RND ## pixels16_l2_8(halfH, halfH, full + 1, 16, 16, 24, 17);      \
    put ## RND ## mpeg4_qpel16_v_lowpass(halfHV, halfH, 16, 16);              \
    OPNAME ## pixels16_l2_8(dst, halfH, halfHV, stride, 16, 16, 16);          \
}                                                                             \
                                                                              \
void ff_ ## OPNAME ## qpel16_mc13_old_c(uint8_t *dst, const uint8_t *src,     \
                                        ptrdiff_t stride)                     \
{                                                                             \
    uint8_t full[24 * 17];                                                    \
    uint8_t halfH[272];                                                       \
    uint8_t halfV[256];                                                       \
    uint8_t halfHV[256];                                                      \
                                                                              \
    copy_block17(full, src, 24, stride, 17);                                  \
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, full, 16, 24, 17);            \
    put ## RND ## mpeg4_qpel16_v_lowpass(halfV, full, 16, 24);                \
    put ## RND ## mpeg4_qpel16_v_lowpass(halfHV, halfH, 16, 16);              \
    OPNAME ## pixels16_l4_8(dst, full + 24, halfH + 16, halfV, halfHV,        \
                            stride, 24, 16, 16, 16, 16);                      \
}                                                                             \
                                                                              \
static void OPNAME ## qpel16_mc13_c(uint8_t *dst, const uint8_t *src,         \
                                    ptrdiff_t stride)                         \
{                                                                             \
    uint8_t full[24 * 17];                                                    \
    uint8_t halfH[272];                                                       \
    uint8_t halfHV[256];                                                      \
                                                                              \
    copy_block17(full, src, 24, stride, 17);                                  \
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, full, 16, 24, 17);            \
    put ## RND ## pixels16_l2_8(halfH, halfH, full, 16, 16, 24, 17);          \
    put ## RND ## mpeg4_qpel16_v_lowpass(halfHV, halfH, 16, 16);              \
    OPNAME ## pixels16_l2_8(dst, halfH + 16, halfHV, stride, 16, 16, 16);     \
}                                                                             \
                                                                              \
void ff_ ## OPNAME ## qpel16_mc33_old_c(uint8_t *dst, const uint8_t *src,     \
                                        ptrdiff_t stride)                     \
{                                                                             \
    uint8_t full[24 * 17];                                                    \
    uint8_t halfH[272];                                                       \
    uint8_t halfV[256];                                                       \
    uint8_t halfHV[256];                                                      \
                                                                              \
    copy_block17(full, src, 24, stride, 17);                                  \
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, full, 16, 24, 17);            \
    put ## RND ## mpeg4_qpel16_v_lowpass(halfV, full + 1, 16, 24);            \
    put ## RND ## mpeg4_qpel16_v_lowpass(halfHV, halfH, 16, 16);              \
    OPNAME ## pixels16_l4_8(dst, full + 25, halfH + 16, halfV, halfHV,        \
                            stride, 24, 16, 16, 16, 16);                      \
}                                                                             \
                                                                              \
static void OPNAME ## qpel16_mc33_c(uint8_t *dst, const uint8_t *src,         \
                                    ptrdiff_t stride)                         \
{                                                                             \
    uint8_t full[24 * 17];                                                    \
    uint8_t halfH[272];                                                       \
    uint8_t halfHV[256];                                                      \
                                                                              \
    copy_block17(full, src, 24, stride, 17);                                  \
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, full, 16, 24, 17);            \
    put ## RND ## pixels16_l2_8(halfH, halfH, full + 1, 16, 16, 24, 17);      \
    put ## RND ## mpeg4_qpel16_v_lowpass(halfHV, halfH, 16, 16);              \
    OPNAME ## pixels16_l2_8(dst, halfH + 16, halfHV, stride, 16, 16, 16);     \
}                                                                             \
                                                                              \
static void OPNAME ## qpel16_mc21_c(uint8_t *dst, const uint8_t *src,         \
                                    ptrdiff_t stride)                         \
{                                                                             \
    uint8_t halfH[272];                                                       \
    uint8_t halfHV[256];                                                      \
                                                                              \
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, src, 16, stride, 17);         \
    put ## RND ## mpeg4_qpel16_v_lowpass(halfHV, halfH, 16, 16);              \
    OPNAME ## pixels16_l2_8(dst, halfH, halfHV, stride, 16, 16, 16);          \
}                                                                             \
                                                                              \
static void OPNAME ## qpel16_mc23_c(uint8_t *dst, const uint8_t *src,         \
                                    ptrdiff_t stride)                         \
{                                                                             \
    uint8_t halfH[272];                                                       \
    uint8_t halfHV[256];                                                      \
                                                                              \
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, src, 16, stride, 17);         \
    put ## RND ## mpeg4_qpel16_v_lowpass(halfHV, halfH, 16, 16);              \
    OPNAME ## pixels16_l2_8(dst, halfH + 16, halfHV, stride, 16, 16, 16);     \
}                                                                             \
                                                                              \
void ff_ ## OPNAME ## qpel16_mc12_old_c(uint8_t *dst, const uint8_t *src,     \
                                        ptrdiff_t stride)                     \
{                                                                             \
    uint8_t full[24 * 17];                                                    \
    uint8_t halfH[272];                                                       \
    uint8_t halfV[256];                                                       \
    uint8_t halfHV[256];                                                      \
                                                                              \
    copy_block17(full, src, 24, stride, 17);                                  \
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, full, 16, 24, 17);            \
    put ## RND ## mpeg4_qpel16_v_lowpass(halfV, full, 16, 24);                \
    put ## RND ## mpeg4_qpel16_v_lowpass(halfHV, halfH, 16, 16);              \
    OPNAME ## pixels16_l2_8(dst, halfV, halfHV, stride, 16, 16, 16);          \
}                                                                             \
                                                                              \
static void OPNAME ## qpel16_mc12_c(uint8_t *dst, const uint8_t *src,         \
                                    ptrdiff_t stride)                         \
{                                                                             \
    uint8_t full[24 * 17];                                                    \
    uint8_t halfH[272];                                                       \
                                                                              \
    copy_block17(full, src, 24, stride, 17);                                  \
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, full, 16, 24, 17);            \
    put ## RND ## pixels16_l2_8(halfH, halfH, full, 16, 16, 24, 17);          \
    OPNAME ## mpeg4_qpel16_v_lowpass(dst, halfH, stride, 16);                 \
}                                                                             \
                                                                              \
void ff_ ## OPNAME ## qpel16_mc32_old_c(uint8_t *dst, const uint8_t *src,     \
                                        ptrdiff_t stride)                     \
{                                                                             \
    uint8_t full[24 * 17];                                                    \
    uint8_t halfH[272];                                                       \
    uint8_t halfV[256];                                                       \
    uint8_t halfHV[256];                                                      \
                                                                              \
    copy_block17(full, src, 24, stride, 17);                                  \
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, full, 16, 24, 17);            \
    put ## RND ## mpeg4_qpel16_v_lowpass(halfV, full + 1, 16, 24);            \
    put ## RND ## mpeg4_qpel16_v_lowpass(halfHV, halfH, 16, 16);              \
    OPNAME ## pixels16_l2_8(dst, halfV, halfHV, stride, 16, 16, 16);          \
}                                                                             \
                                                                              \
static void OPNAME ## qpel16_mc32_c(uint8_t *dst, const uint8_t *src,         \
                                    ptrdiff_t stride)                         \
{                                                                             \
    uint8_t full[24 * 17];                                                    \
    uint8_t halfH[272];                                                       \
                                                                              \
    copy_block17(full, src, 24, stride, 17);                                  \
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, full, 16, 24, 17);            \
    put ## RND ## pixels16_l2_8(halfH, halfH, full + 1, 16, 16, 24, 17);      \
    OPNAME ## mpeg4_qpel16_v_lowpass(dst, halfH, stride, 16);                 \
}                                                                             \
                                                                              \
static void OPNAME ## qpel16_mc22_c(uint8_t *dst, const uint8_t *src,         \
                                    ptrdiff_t stride)                         \
{                                                                             \
    uint8_t halfH[272];                                                       \
                                                                              \
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, src, 16, stride, 17);         \
    OPNAME ## mpeg4_qpel16_v_lowpass(dst, halfH, stride, 16);                 \
}

#define op_avg(a, b)        a = (((a) + cm[((b) + 16) >> 5] + 1) >> 1)
#define op_put(a, b)        a = cm[((b) + 16) >> 5]
#define op_put_no_rnd(a, b) a = cm[((b) + 15) >> 5]

QPEL_MC(0, put_, _, op_put)
QPEL_MC(1, put_no_rnd_, _no_rnd_, op_put_no_rnd)
QPEL_MC(0, avg_, _, op_avg)

#undef op_avg
#undef op_put
#undef op_put_no_rnd

void ff_put_pixels8x8_c(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)
{
    put_pixels8_8_c(dst, src, stride, 8);
}

void ff_avg_pixels8x8_c(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)
{
    avg_pixels8_8_c(dst, src, stride, 8);
}

void ff_put_pixels16x16_c(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)
{
    put_pixels16_8_c(dst, src, stride, 16);
}

void ff_avg_pixels16x16_c(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)
{
    avg_pixels16_8_c(dst, src, stride, 16);
}

#define put_qpel8_mc00_c         ff_put_pixels8x8_c
#define avg_qpel8_mc00_c         ff_avg_pixels8x8_c
#define put_qpel16_mc00_c        ff_put_pixels16x16_c
#define avg_qpel16_mc00_c        ff_avg_pixels16x16_c
#define put_no_rnd_qpel8_mc00_c  ff_put_pixels8x8_c
#define put_no_rnd_qpel16_mc00_c ff_put_pixels16x16_c

void ff_put_pixels8_l2_8(uint8_t *dst, const uint8_t *src1, const uint8_t *src2,
                         int dst_stride, int src_stride1, int src_stride2,
                         int h)
{
    put_pixels8_l2_8(dst, src1, src2, dst_stride, src_stride1, src_stride2, h);

}

#if CONFIG_DIRAC_DECODER
#define DIRAC_MC(OPNAME)\
void ff_ ## OPNAME ## _dirac_pixels8_c(uint8_t *dst, const uint8_t *src[5], int stride, int h)\
{\
     OPNAME ## _pixels8_8_c(dst, src[0], stride, h);\
}\
void ff_ ## OPNAME ## _dirac_pixels16_c(uint8_t *dst, const uint8_t *src[5], int stride, int h)\
{\
    OPNAME ## _pixels16_8_c(dst, src[0], stride, h);\
}\
void ff_ ## OPNAME ## _dirac_pixels32_c(uint8_t *dst, const uint8_t *src[5], int stride, int h)\
{\
    OPNAME ## _pixels16_8_c(dst   , src[0]   , stride, h);\
    OPNAME ## _pixels16_8_c(dst+16, src[0]+16, stride, h);\
}\
void ff_ ## OPNAME ## _dirac_pixels8_l2_c(uint8_t *dst, const uint8_t *src[5], int stride, int h)\
{\
    OPNAME ## _pixels8_l2_8(dst, src[0], src[1], stride, stride, stride, h);\
}\
void ff_ ## OPNAME ## _dirac_pixels16_l2_c(uint8_t *dst, const uint8_t *src[5], int stride, int h)\
{\
    OPNAME ## _pixels16_l2_8(dst, src[0], src[1], stride, stride, stride, h);\
}\
void ff_ ## OPNAME ## _dirac_pixels32_l2_c(uint8_t *dst, const uint8_t *src[5], int stride, int h)\
{\
    OPNAME ## _pixels16_l2_8(dst   , src[0]   , src[1]   , stride, stride, stride, h);\
    OPNAME ## _pixels16_l2_8(dst+16, src[0]+16, src[1]+16, stride, stride, stride, h);\
}\
void ff_ ## OPNAME ## _dirac_pixels8_l4_c(uint8_t *dst, const uint8_t *src[5], int stride, int h)\
{\
    OPNAME ## _pixels8_l4_8(dst, src[0], src[1], src[2], src[3], stride, stride, stride, stride, stride, h);\
}\
void ff_ ## OPNAME ## _dirac_pixels16_l4_c(uint8_t *dst, const uint8_t *src[5], int stride, int h)\
{\
    OPNAME ## _pixels16_l4_8(dst, src[0], src[1], src[2], src[3], stride, stride, stride, stride, stride, h);\
}\
void ff_ ## OPNAME ## _dirac_pixels32_l4_c(uint8_t *dst, const uint8_t *src[5], int stride, int h)\
{\
    OPNAME ## _pixels16_l4_8(dst   , src[0]   , src[1]   , src[2]   , src[3]   , stride, stride, stride, stride, stride, h);\
    OPNAME ## _pixels16_l4_8(dst+16, src[0]+16, src[1]+16, src[2]+16, src[3]+16, stride, stride, stride, stride, stride, h);\
}
DIRAC_MC(put)
DIRAC_MC(avg)
#endif

av_cold void ff_qpeldsp_init(QpelDSPContext *c)
{
#define dspfunc(PFX, IDX, NUM)                              \
    c->PFX ## _pixels_tab[IDX][0]  = PFX ## NUM ## _mc00_c; \
    c->PFX ## _pixels_tab[IDX][1]  = PFX ## NUM ## _mc10_c; \
    c->PFX ## _pixels_tab[IDX][2]  = PFX ## NUM ## _mc20_c; \
    c->PFX ## _pixels_tab[IDX][3]  = PFX ## NUM ## _mc30_c; \
    c->PFX ## _pixels_tab[IDX][4]  = PFX ## NUM ## _mc01_c; \
    c->PFX ## _pixels_tab[IDX][5]  = PFX ## NUM ## _mc11_c; \
    c->PFX ## _pixels_tab[IDX][6]  = PFX ## NUM ## _mc21_c; \
    c->PFX ## _pixels_tab[IDX][7]  = PFX ## NUM ## _mc31_c; \
    c->PFX ## _pixels_tab[IDX][8]  = PFX ## NUM ## _mc02_c; \
    c->PFX ## _pixels_tab[IDX][9]  = PFX ## NUM ## _mc12_c; \
    c->PFX ## _pixels_tab[IDX][10] = PFX ## NUM ## _mc22_c; \
    c->PFX ## _pixels_tab[IDX][11] = PFX ## NUM ## _mc32_c; \
    c->PFX ## _pixels_tab[IDX][12] = PFX ## NUM ## _mc03_c; \
    c->PFX ## _pixels_tab[IDX][13] = PFX ## NUM ## _mc13_c; \
    c->PFX ## _pixels_tab[IDX][14] = PFX ## NUM ## _mc23_c; \
    c->PFX ## _pixels_tab[IDX][15] = PFX ## NUM ## _mc33_c

    dspfunc(put_qpel, 0, 16);
    dspfunc(put_qpel, 1, 8);

    dspfunc(put_no_rnd_qpel, 0, 16);
    dspfunc(put_no_rnd_qpel, 1, 8);

    dspfunc(avg_qpel, 0, 16);
    dspfunc(avg_qpel, 1, 8);

    if (ARCH_X86)
        ff_qpeldsp_init_x86(c);
    if (ARCH_MIPS)
        ff_qpeldsp_init_mips(c);
}
