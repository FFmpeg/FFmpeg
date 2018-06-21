/*
 * Chinese AVS video (AVS1-P2, JiZhun profile) decoder.
 *
 * DSP functions
 *
 * Copyright (c) 2006  Stefan Gehrer <stefan.gehrer@gmx.de>
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

#include <stdio.h>

#include "idctdsp.h"
#include "mathops.h"
#include "cavsdsp.h"
#include "libavutil/common.h"

/*****************************************************************************
 *
 * in-loop deblocking filter
 *
 ****************************************************************************/

#define P2 p0_p[-3*stride]
#define P1 p0_p[-2*stride]
#define P0 p0_p[-1*stride]
#define Q0 p0_p[ 0*stride]
#define Q1 p0_p[ 1*stride]
#define Q2 p0_p[ 2*stride]

static inline void loop_filter_l2(uint8_t *p0_p, ptrdiff_t stride, int alpha, int beta)
{
    int p0 = P0;
    int q0 = Q0;

    if(abs(p0-q0)<alpha && abs(P1-p0)<beta && abs(Q1-q0)<beta) {
        int s = p0 + q0 + 2;
        alpha = (alpha>>2) + 2;
        if(abs(P2-p0) < beta && abs(p0-q0) < alpha) {
            P0 = (P1 + p0 + s) >> 2;
            P1 = (2*P1 + s) >> 2;
        } else
            P0 = (2*P1 + s) >> 2;
        if(abs(Q2-q0) < beta && abs(q0-p0) < alpha) {
            Q0 = (Q1 + q0 + s) >> 2;
            Q1 = (2*Q1 + s) >> 2;
        } else
            Q0 = (2*Q1 + s) >> 2;
    }
}

static inline void loop_filter_l1(uint8_t *p0_p, ptrdiff_t stride, int alpha, int beta, int tc)
{
    int p0 = P0;
    int q0 = Q0;

    if(abs(p0-q0)<alpha && abs(P1-p0)<beta && abs(Q1-q0)<beta) {
        int delta = av_clip(((q0-p0)*3+P1-Q1+4)>>3,-tc, tc);
        P0 = av_clip_uint8(p0+delta);
        Q0 = av_clip_uint8(q0-delta);
        if(abs(P2-p0)<beta) {
            delta = av_clip(((P0-P1)*3+P2-Q0+4)>>3, -tc, tc);
            P1 = av_clip_uint8(P1+delta);
        }
        if(abs(Q2-q0)<beta) {
            delta = av_clip(((Q1-Q0)*3+P0-Q2+4)>>3, -tc, tc);
            Q1 = av_clip_uint8(Q1-delta);
        }
    }
}

static inline void loop_filter_c2(uint8_t *p0_p, ptrdiff_t stride, int alpha, int beta)
{
    int p0 = P0;
    int q0 = Q0;

    if(abs(p0-q0)<alpha && abs(P1-p0)<beta && abs(Q1-q0)<beta) {
        int s = p0 + q0 + 2;
        alpha = (alpha>>2) + 2;
        if(abs(P2-p0) < beta && abs(p0-q0) < alpha) {
            P0 = (P1 + p0 + s) >> 2;
        } else
            P0 = (2*P1 + s) >> 2;
        if(abs(Q2-q0) < beta && abs(q0-p0) < alpha) {
            Q0 = (Q1 + q0 + s) >> 2;
        } else
            Q0 = (2*Q1 + s) >> 2;
    }
}

static inline void loop_filter_c1(uint8_t *p0_p, ptrdiff_t stride, int alpha, int beta,
                                  int tc)
{
    if(abs(P0-Q0)<alpha && abs(P1-P0)<beta && abs(Q1-Q0)<beta) {
        int delta = av_clip(((Q0-P0)*3+P1-Q1+4)>>3, -tc, tc);
        P0 = av_clip_uint8(P0+delta);
        Q0 = av_clip_uint8(Q0-delta);
    }
}

#undef P0
#undef P1
#undef P2
#undef Q0
#undef Q1
#undef Q2

static void cavs_filter_lv_c(uint8_t *d, ptrdiff_t stride, int alpha, int beta, int tc,
                             int bs1, int bs2)
{
    int i;
    if(bs1==2)
        for(i=0;i<16;i++)
            loop_filter_l2(d + i*stride,1,alpha,beta);
    else {
        if(bs1)
            for(i=0;i<8;i++)
                loop_filter_l1(d + i*stride,1,alpha,beta,tc);
        if (bs2)
            for(i=8;i<16;i++)
                loop_filter_l1(d + i*stride,1,alpha,beta,tc);
    }
}

static void cavs_filter_lh_c(uint8_t *d, ptrdiff_t stride, int alpha, int beta, int tc,
                             int bs1, int bs2)
{
    int i;
    if(bs1==2)
        for(i=0;i<16;i++)
            loop_filter_l2(d + i,stride,alpha,beta);
    else {
        if(bs1)
            for(i=0;i<8;i++)
                loop_filter_l1(d + i,stride,alpha,beta,tc);
        if (bs2)
            for(i=8;i<16;i++)
                loop_filter_l1(d + i,stride,alpha,beta,tc);
    }
}

static void cavs_filter_cv_c(uint8_t *d, ptrdiff_t stride, int alpha, int beta, int tc,
                             int bs1, int bs2)
{
    int i;
    if(bs1==2)
        for(i=0;i<8;i++)
            loop_filter_c2(d + i*stride,1,alpha,beta);
    else {
        if(bs1)
            for(i=0;i<4;i++)
                loop_filter_c1(d + i*stride,1,alpha,beta,tc);
        if (bs2)
            for(i=4;i<8;i++)
                loop_filter_c1(d + i*stride,1,alpha,beta,tc);
    }
}

static void cavs_filter_ch_c(uint8_t *d, ptrdiff_t stride, int alpha, int beta, int tc,
                             int bs1, int bs2)
{
    int i;
    if(bs1==2)
        for(i=0;i<8;i++)
            loop_filter_c2(d + i,stride,alpha,beta);
    else {
        if(bs1)
            for(i=0;i<4;i++)
                loop_filter_c1(d + i,stride,alpha,beta,tc);
        if (bs2)
            for(i=4;i<8;i++)
                loop_filter_c1(d + i,stride,alpha,beta,tc);
    }
}

/*****************************************************************************
 *
 * inverse transform
 *
 ****************************************************************************/

static void cavs_idct8_add_c(uint8_t *dst, int16_t *block, ptrdiff_t stride)
{
    int i;
    int16_t (*src)[8] = (int16_t(*)[8])block;

    src[0][0] += 8;

    for( i = 0; i < 8; i++ ) {
        const int a0 =  3*src[i][1] - (src[i][7]<<1);
        const int a1 =  3*src[i][3] + (src[i][5]<<1);
        const int a2 =  (src[i][3]<<1) - 3*src[i][5];
        const int a3 =  (src[i][1]<<1) + 3*src[i][7];

        const int b4 = ((a0 + a1 + a3)<<1) + a1;
        const int b5 = ((a0 - a1 + a2)<<1) + a0;
        const int b6 = ((a3 - a2 - a1)<<1) + a3;
        const int b7 = ((a0 - a2 - a3)<<1) - a2;

        const int a7 = (src[i][2]<<2) - 10*src[i][6];
        const int a6 = (src[i][6]<<2) + 10*src[i][2];
        const int a5 = ((src[i][0] - src[i][4]) << 3) + 4;
        const int a4 = ((src[i][0] + src[i][4]) << 3) + 4;

        const int b0 = a4 + a6;
        const int b1 = a5 + a7;
        const int b2 = a5 - a7;
        const int b3 = a4 - a6;

        src[i][0] = (b0 + b4) >> 3;
        src[i][1] = (b1 + b5) >> 3;
        src[i][2] = (b2 + b6) >> 3;
        src[i][3] = (b3 + b7) >> 3;
        src[i][4] = (b3 - b7) >> 3;
        src[i][5] = (b2 - b6) >> 3;
        src[i][6] = (b1 - b5) >> 3;
        src[i][7] = (b0 - b4) >> 3;
    }
    for( i = 0; i < 8; i++ ) {
        const int a0 =  3*src[1][i] - (src[7][i]<<1);
        const int a1 =  3*src[3][i] + (src[5][i]<<1);
        const int a2 =  (src[3][i]<<1) - 3*src[5][i];
        const int a3 =  (src[1][i]<<1) + 3*src[7][i];

        const int b4 = ((a0 + a1 + a3)<<1) + a1;
        const int b5 = ((a0 - a1 + a2)<<1) + a0;
        const int b6 = ((a3 - a2 - a1)<<1) + a3;
        const int b7 = ((a0 - a2 - a3)<<1) - a2;

        const int a7 = (src[2][i]<<2) - 10*src[6][i];
        const int a6 = (src[6][i]<<2) + 10*src[2][i];
        const int a5 = (src[0][i] - src[4][i]) << 3;
        const int a4 = (src[0][i] + src[4][i]) << 3;

        const int b0 = a4 + a6;
        const int b1 = a5 + a7;
        const int b2 = a5 - a7;
        const int b3 = a4 - a6;

        dst[i + 0*stride] = av_clip_uint8( dst[i + 0*stride] + ((b0 + b4) >> 7));
        dst[i + 1*stride] = av_clip_uint8( dst[i + 1*stride] + ((b1 + b5) >> 7));
        dst[i + 2*stride] = av_clip_uint8( dst[i + 2*stride] + ((b2 + b6) >> 7));
        dst[i + 3*stride] = av_clip_uint8( dst[i + 3*stride] + ((b3 + b7) >> 7));
        dst[i + 4*stride] = av_clip_uint8( dst[i + 4*stride] + ((b3 - b7) >> 7));
        dst[i + 5*stride] = av_clip_uint8( dst[i + 5*stride] + ((b2 - b6) >> 7));
        dst[i + 6*stride] = av_clip_uint8( dst[i + 6*stride] + ((b1 - b5) >> 7));
        dst[i + 7*stride] = av_clip_uint8( dst[i + 7*stride] + ((b0 - b4) >> 7));
    }
}

/*****************************************************************************
 *
 * motion compensation
 *
 ****************************************************************************/

#define CAVS_SUBPIX(OPNAME, OP, NAME, A, B, C, D, E, F) \
static void OPNAME ## cavs_filt8_h_ ## NAME(uint8_t *dst, const uint8_t *src, ptrdiff_t dstStride, ptrdiff_t srcStride)\
{                                                                       \
    const int h=8;\
    const uint8_t *cm = ff_crop_tab + MAX_NEG_CROP;\
    int i;\
    for(i=0; i<h; i++)\
    {\
        OP(dst[0], A*src[-2] + B*src[-1] + C*src[0] + D*src[1] + E*src[2] + F*src[3]);\
        OP(dst[1], A*src[-1] + B*src[ 0] + C*src[1] + D*src[2] + E*src[3] + F*src[4]);\
        OP(dst[2], A*src[ 0] + B*src[ 1] + C*src[2] + D*src[3] + E*src[4] + F*src[5]);\
        OP(dst[3], A*src[ 1] + B*src[ 2] + C*src[3] + D*src[4] + E*src[5] + F*src[6]);\
        OP(dst[4], A*src[ 2] + B*src[ 3] + C*src[4] + D*src[5] + E*src[6] + F*src[7]);\
        OP(dst[5], A*src[ 3] + B*src[ 4] + C*src[5] + D*src[6] + E*src[7] + F*src[8]);\
        OP(dst[6], A*src[ 4] + B*src[ 5] + C*src[6] + D*src[7] + E*src[8] + F*src[9]);\
        OP(dst[7], A*src[ 5] + B*src[ 6] + C*src[7] + D*src[8] + E*src[9] + F*src[10]);\
        dst+=dstStride;\
        src+=srcStride;\
    }\
}\
\
static void OPNAME ## cavs_filt8_v_  ## NAME(uint8_t *dst, const uint8_t *src, ptrdiff_t dstStride, ptrdiff_t srcStride)\
{                                                                       \
    const int w=8;\
    const uint8_t *cm = ff_crop_tab + MAX_NEG_CROP;\
    int i;\
    for(i=0; i<w; i++)\
    {\
        const int srcB= src[-2*srcStride];\
        const int srcA= src[-1*srcStride];\
        const int src0= src[0 *srcStride];\
        const int src1= src[1 *srcStride];\
        const int src2= src[2 *srcStride];\
        const int src3= src[3 *srcStride];\
        const int src4= src[4 *srcStride];\
        const int src5= src[5 *srcStride];\
        const int src6= src[6 *srcStride];\
        const int src7= src[7 *srcStride];\
        const int src8= src[8 *srcStride];\
        const int src9= src[9 *srcStride];\
        const int src10= src[10 *srcStride];\
        OP(dst[0*dstStride], A*srcB + B*srcA + C*src0 + D*src1 + E*src2 + F*src3);\
        OP(dst[1*dstStride], A*srcA + B*src0 + C*src1 + D*src2 + E*src3 + F*src4);\
        OP(dst[2*dstStride], A*src0 + B*src1 + C*src2 + D*src3 + E*src4 + F*src5);\
        OP(dst[3*dstStride], A*src1 + B*src2 + C*src3 + D*src4 + E*src5 + F*src6);\
        OP(dst[4*dstStride], A*src2 + B*src3 + C*src4 + D*src5 + E*src6 + F*src7);\
        OP(dst[5*dstStride], A*src3 + B*src4 + C*src5 + D*src6 + E*src7 + F*src8);\
        OP(dst[6*dstStride], A*src4 + B*src5 + C*src6 + D*src7 + E*src8 + F*src9);\
        OP(dst[7*dstStride], A*src5 + B*src6 + C*src7 + D*src8 + E*src9 + F*src10);\
        dst++;\
        src++;\
    }\
}\
\
static void OPNAME ## cavs_filt16_v_ ## NAME(uint8_t *dst, const uint8_t *src, ptrdiff_t dstStride, ptrdiff_t srcStride)\
{                                                                       \
    OPNAME ## cavs_filt8_v_ ## NAME(dst  , src  , dstStride, srcStride);\
    OPNAME ## cavs_filt8_v_ ## NAME(dst+8, src+8, dstStride, srcStride);\
    src += 8*srcStride;\
    dst += 8*dstStride;\
    OPNAME ## cavs_filt8_v_ ## NAME(dst  , src  , dstStride, srcStride);\
    OPNAME ## cavs_filt8_v_ ## NAME(dst+8, src+8, dstStride, srcStride);\
}\
\
static void OPNAME ## cavs_filt16_h_ ## NAME(uint8_t *dst, const uint8_t *src, ptrdiff_t dstStride, ptrdiff_t srcStride)\
{                                                                       \
    OPNAME ## cavs_filt8_h_ ## NAME(dst  , src  , dstStride, srcStride);\
    OPNAME ## cavs_filt8_h_ ## NAME(dst+8, src+8, dstStride, srcStride);\
    src += 8*srcStride;\
    dst += 8*dstStride;\
    OPNAME ## cavs_filt8_h_ ## NAME(dst  , src  , dstStride, srcStride);\
    OPNAME ## cavs_filt8_h_ ## NAME(dst+8, src+8, dstStride, srcStride);\
}\

#define CAVS_SUBPIX_HV(OPNAME, OP, NAME, AH, BH, CH, DH, EH, FH, AV, BV, CV, DV, EV, FV, FULL) \
static void OPNAME ## cavs_filt8_hv_ ## NAME(uint8_t *dst, const uint8_t *src1, const uint8_t *src2, ptrdiff_t dstStride, ptrdiff_t srcStride)\
{                                                                       \
    int16_t temp[8*(8+5)];\
    int16_t *tmp = temp;\
    const int h=8;\
    const int w=8;\
    const uint8_t *cm = ff_crop_tab + MAX_NEG_CROP;\
    int i;\
    src1 -= 2*srcStride;\
    for(i=0; i<h+5; i++)\
    {\
        tmp[0]= AH*src1[-2] + BH*src1[-1] + CH*src1[0] + DH*src1[1] + EH*src1[2] + FH*src1[3];\
        tmp[1]= AH*src1[-1] + BH*src1[ 0] + CH*src1[1] + DH*src1[2] + EH*src1[3] + FH*src1[4];\
        tmp[2]= AH*src1[ 0] + BH*src1[ 1] + CH*src1[2] + DH*src1[3] + EH*src1[4] + FH*src1[5];\
        tmp[3]= AH*src1[ 1] + BH*src1[ 2] + CH*src1[3] + DH*src1[4] + EH*src1[5] + FH*src1[6];\
        tmp[4]= AH*src1[ 2] + BH*src1[ 3] + CH*src1[4] + DH*src1[5] + EH*src1[6] + FH*src1[7];\
        tmp[5]= AH*src1[ 3] + BH*src1[ 4] + CH*src1[5] + DH*src1[6] + EH*src1[7] + FH*src1[8];\
        tmp[6]= AH*src1[ 4] + BH*src1[ 5] + CH*src1[6] + DH*src1[7] + EH*src1[8] + FH*src1[9];\
        tmp[7]= AH*src1[ 5] + BH*src1[ 6] + CH*src1[7] + DH*src1[8] + EH*src1[9] + FH*src1[10];\
        tmp+=8;\
        src1+=srcStride;\
    }\
    if(FULL) {\
      tmp = temp+8*2;                           \
      for(i=0; i<w; i++)                        \
        {                                       \
          const int tmpB= tmp[-2*8];    \
          const int tmpA= tmp[-1*8];    \
          const int tmp0= tmp[0 *8];    \
          const int tmp1= tmp[1 *8];    \
          const int tmp2= tmp[2 *8];    \
          const int tmp3= tmp[3 *8];    \
          const int tmp4= tmp[4 *8];    \
          const int tmp5= tmp[5 *8];    \
          const int tmp6= tmp[6 *8];    \
          const int tmp7= tmp[7 *8];    \
          const int tmp8= tmp[8 *8];    \
          const int tmp9= tmp[9 *8];    \
          const int tmp10=tmp[10*8];                            \
          OP(dst[0*dstStride], AV*tmpB + BV*tmpA + CV*tmp0 + DV*tmp1 + EV*tmp2 + FV*tmp3 + 64*src2[0*srcStride]); \
          OP(dst[1*dstStride], AV*tmpA + BV*tmp0 + CV*tmp1 + DV*tmp2 + EV*tmp3 + FV*tmp4 + 64*src2[1*srcStride]); \
          OP(dst[2*dstStride], AV*tmp0 + BV*tmp1 + CV*tmp2 + DV*tmp3 + EV*tmp4 + FV*tmp5 + 64*src2[2*srcStride]); \
          OP(dst[3*dstStride], AV*tmp1 + BV*tmp2 + CV*tmp3 + DV*tmp4 + EV*tmp5 + FV*tmp6 + 64*src2[3*srcStride]); \
          OP(dst[4*dstStride], AV*tmp2 + BV*tmp3 + CV*tmp4 + DV*tmp5 + EV*tmp6 + FV*tmp7 + 64*src2[4*srcStride]); \
          OP(dst[5*dstStride], AV*tmp3 + BV*tmp4 + CV*tmp5 + DV*tmp6 + EV*tmp7 + FV*tmp8 + 64*src2[5*srcStride]); \
          OP(dst[6*dstStride], AV*tmp4 + BV*tmp5 + CV*tmp6 + DV*tmp7 + EV*tmp8 + FV*tmp9 + 64*src2[6*srcStride]); \
          OP(dst[7*dstStride], AV*tmp5 + BV*tmp6 + CV*tmp7 + DV*tmp8 + EV*tmp9 + FV*tmp10 + 64*src2[7*srcStride]); \
          dst++;                                                        \
          tmp++;                                                        \
          src2++;                                                       \
        }                                                               \
    } else {\
      tmp = temp+8*2;                           \
      for(i=0; i<w; i++)                        \
        {                                       \
          const int tmpB= tmp[-2*8];    \
          const int tmpA= tmp[-1*8];    \
          const int tmp0= tmp[0 *8];    \
          const int tmp1= tmp[1 *8];    \
          const int tmp2= tmp[2 *8];    \
          const int tmp3= tmp[3 *8];    \
          const int tmp4= tmp[4 *8];    \
          const int tmp5= tmp[5 *8];    \
          const int tmp6= tmp[6 *8];    \
          const int tmp7= tmp[7 *8];    \
          const int tmp8= tmp[8 *8];    \
          const int tmp9= tmp[9 *8];    \
          const int tmp10=tmp[10*8];                            \
          OP(dst[0*dstStride], AV*tmpB + BV*tmpA + CV*tmp0 + DV*tmp1 + EV*tmp2 + FV*tmp3); \
          OP(dst[1*dstStride], AV*tmpA + BV*tmp0 + CV*tmp1 + DV*tmp2 + EV*tmp3 + FV*tmp4); \
          OP(dst[2*dstStride], AV*tmp0 + BV*tmp1 + CV*tmp2 + DV*tmp3 + EV*tmp4 + FV*tmp5); \
          OP(dst[3*dstStride], AV*tmp1 + BV*tmp2 + CV*tmp3 + DV*tmp4 + EV*tmp5 + FV*tmp6); \
          OP(dst[4*dstStride], AV*tmp2 + BV*tmp3 + CV*tmp4 + DV*tmp5 + EV*tmp6 + FV*tmp7); \
          OP(dst[5*dstStride], AV*tmp3 + BV*tmp4 + CV*tmp5 + DV*tmp6 + EV*tmp7 + FV*tmp8); \
          OP(dst[6*dstStride], AV*tmp4 + BV*tmp5 + CV*tmp6 + DV*tmp7 + EV*tmp8 + FV*tmp9); \
          OP(dst[7*dstStride], AV*tmp5 + BV*tmp6 + CV*tmp7 + DV*tmp8 + EV*tmp9 + FV*tmp10); \
          dst++;                                                        \
          tmp++;                                                        \
        }                                                               \
    }\
}\
\
static void OPNAME ## cavs_filt16_hv_ ## NAME(uint8_t *dst, const uint8_t *src1, const uint8_t *src2, ptrdiff_t dstStride, ptrdiff_t srcStride)\
{                                                                       \
    OPNAME ## cavs_filt8_hv_ ## NAME(dst  , src1,   src2  , dstStride, srcStride); \
    OPNAME ## cavs_filt8_hv_ ## NAME(dst+8, src1+8, src2+8, dstStride, srcStride); \
    src1 += 8*srcStride;\
    src2 += 8*srcStride;\
    dst += 8*dstStride;\
    OPNAME ## cavs_filt8_hv_ ## NAME(dst  , src1,   src2  , dstStride, srcStride); \
    OPNAME ## cavs_filt8_hv_ ## NAME(dst+8, src1+8, src2+8, dstStride, srcStride); \
}\

#define CAVS_MC(OPNAME, SIZE) \
static void OPNAME ## cavs_qpel ## SIZE ## _mc10_c(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
    OPNAME ## cavs_filt ## SIZE ## _h_qpel_l(dst, src, stride, stride);\
}\
\
static void OPNAME ## cavs_qpel ## SIZE ## _mc20_c(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
    OPNAME ## cavs_filt ## SIZE ## _h_hpel(dst, src, stride, stride);\
}\
\
static void OPNAME ## cavs_qpel ## SIZE ## _mc30_c(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
    OPNAME ## cavs_filt ## SIZE ## _h_qpel_r(dst, src, stride, stride);\
}\
\
static void OPNAME ## cavs_qpel ## SIZE ## _mc01_c(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
    OPNAME ## cavs_filt ## SIZE ## _v_qpel_l(dst, src, stride, stride);\
}\
\
static void OPNAME ## cavs_qpel ## SIZE ## _mc02_c(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
    OPNAME ## cavs_filt ## SIZE ## _v_hpel(dst, src, stride, stride);\
}\
\
static void OPNAME ## cavs_qpel ## SIZE ## _mc03_c(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
    OPNAME ## cavs_filt ## SIZE ## _v_qpel_r(dst, src, stride, stride);\
}\
\
static void OPNAME ## cavs_qpel ## SIZE ## _mc22_c(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
  OPNAME ## cavs_filt ## SIZE ## _hv_jj(dst, src, NULL, stride, stride); \
}\
\
static void OPNAME ## cavs_qpel ## SIZE ## _mc11_c(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
  OPNAME ## cavs_filt ## SIZE ## _hv_egpr(dst, src, src, stride, stride); \
}\
\
static void OPNAME ## cavs_qpel ## SIZE ## _mc13_c(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
  OPNAME ## cavs_filt ## SIZE ## _hv_egpr(dst, src, src+stride, stride, stride); \
}\
\
static void OPNAME ## cavs_qpel ## SIZE ## _mc31_c(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
  OPNAME ## cavs_filt ## SIZE ## _hv_egpr(dst, src, src+1, stride, stride); \
}\
\
static void OPNAME ## cavs_qpel ## SIZE ## _mc33_c(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
  OPNAME ## cavs_filt ## SIZE ## _hv_egpr(dst, src, src+stride+1,stride, stride); \
}\
\
static void OPNAME ## cavs_qpel ## SIZE ## _mc21_c(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
  OPNAME ## cavs_filt ## SIZE ## _hv_ff(dst, src, src+stride+1,stride, stride); \
}\
\
static void OPNAME ## cavs_qpel ## SIZE ## _mc12_c(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
  OPNAME ## cavs_filt ## SIZE ## _hv_ii(dst, src, src+stride+1,stride, stride); \
}\
\
static void OPNAME ## cavs_qpel ## SIZE ## _mc32_c(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
  OPNAME ## cavs_filt ## SIZE ## _hv_kk(dst, src, src+stride+1,stride, stride); \
}\
\
static void OPNAME ## cavs_qpel ## SIZE ## _mc23_c(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
  OPNAME ## cavs_filt ## SIZE ## _hv_qq(dst, src, src+stride+1,stride, stride); \
}\

#define op_put1(a, b)  a = cm[((b)+4)>>3]
#define op_put2(a, b)  a = cm[((b)+64)>>7]
#define op_put3(a, b)  a = cm[((b)+32)>>6]
#define op_put4(a, b)  a = cm[((b)+512)>>10]
#define op_avg1(a, b)  a = ((a)+cm[((b)+4)>>3]   +1)>>1
#define op_avg2(a, b)  a = ((a)+cm[((b)+64)>>7]  +1)>>1
#define op_avg3(a, b)  a = ((a)+cm[((b)+32)>>6]  +1)>>1
#define op_avg4(a, b)  a = ((a)+cm[((b)+512)>>10]+1)>>1
CAVS_SUBPIX(put_   , op_put1, hpel,    0, -1,  5,  5, -1,  0)
CAVS_SUBPIX(put_   , op_put2, qpel_l, -1, -2, 96, 42, -7,  0)
CAVS_SUBPIX(put_   , op_put2, qpel_r,  0, -7, 42, 96, -2, -1)
CAVS_SUBPIX_HV(put_, op_put3, jj,      0, -1,  5,  5, -1,  0,  0, -1,  5,  5, -1, 0, 0)
CAVS_SUBPIX_HV(put_, op_put4, ff,      0, -1,  5,  5, -1,  0, -1, -2, 96, 42, -7, 0, 0)
CAVS_SUBPIX_HV(put_, op_put4, ii,     -1, -2, 96, 42, -7,  0,  0, -1,  5,  5, -1, 0, 0)
CAVS_SUBPIX_HV(put_, op_put4, kk,      0, -7, 42, 96, -2, -1,  0, -1,  5,  5, -1, 0, 0)
CAVS_SUBPIX_HV(put_, op_put4, qq,      0, -1,  5,  5, -1,  0,  0, -7, 42, 96, -2,-1, 0)
CAVS_SUBPIX_HV(put_, op_put2, egpr,    0, -1,  5,  5, -1,  0,  0, -1,  5,  5, -1, 0, 1)
CAVS_SUBPIX(avg_   , op_avg1, hpel,    0, -1,  5,  5, -1,  0)
CAVS_SUBPIX(avg_   , op_avg2, qpel_l, -1, -2, 96, 42, -7,  0)
CAVS_SUBPIX(avg_   , op_avg2, qpel_r,  0, -7, 42, 96, -2, -1)
CAVS_SUBPIX_HV(avg_, op_avg3, jj,      0, -1,  5,  5, -1,  0,  0, -1,  5,  5, -1, 0, 0)
CAVS_SUBPIX_HV(avg_, op_avg4, ff,      0, -1,  5,  5, -1,  0, -1, -2, 96, 42, -7, 0, 0)
CAVS_SUBPIX_HV(avg_, op_avg4, ii,     -1, -2, 96, 42, -7,  0,  0, -1,  5,  5, -1, 0, 0)
CAVS_SUBPIX_HV(avg_, op_avg4, kk,      0, -7, 42, 96, -2, -1,  0, -1,  5,  5, -1, 0, 0)
CAVS_SUBPIX_HV(avg_, op_avg4, qq,      0, -1,  5,  5, -1,  0,  0, -7, 42, 96, -2,-1, 0)
CAVS_SUBPIX_HV(avg_, op_avg2, egpr,    0, -1,  5,  5, -1,  0,  0, -1,  5,  5, -1, 0, 1)
CAVS_MC(put_, 8)
CAVS_MC(put_, 16)
CAVS_MC(avg_, 8)
CAVS_MC(avg_, 16)

#define put_cavs_qpel8_mc00_c  ff_put_pixels8x8_c
#define avg_cavs_qpel8_mc00_c  ff_avg_pixels8x8_c
#define put_cavs_qpel16_mc00_c ff_put_pixels16x16_c
#define avg_cavs_qpel16_mc00_c ff_avg_pixels16x16_c

av_cold void ff_cavsdsp_init(CAVSDSPContext* c, AVCodecContext *avctx) {
#define dspfunc(PFX, IDX, NUM) \
    c->PFX ## _pixels_tab[IDX][ 0] = PFX ## NUM ## _mc00_c; \
    c->PFX ## _pixels_tab[IDX][ 1] = PFX ## NUM ## _mc10_c; \
    c->PFX ## _pixels_tab[IDX][ 2] = PFX ## NUM ## _mc20_c; \
    c->PFX ## _pixels_tab[IDX][ 3] = PFX ## NUM ## _mc30_c; \
    c->PFX ## _pixels_tab[IDX][ 4] = PFX ## NUM ## _mc01_c; \
    c->PFX ## _pixels_tab[IDX][ 5] = PFX ## NUM ## _mc11_c; \
    c->PFX ## _pixels_tab[IDX][ 6] = PFX ## NUM ## _mc21_c; \
    c->PFX ## _pixels_tab[IDX][ 7] = PFX ## NUM ## _mc31_c; \
    c->PFX ## _pixels_tab[IDX][ 8] = PFX ## NUM ## _mc02_c; \
    c->PFX ## _pixels_tab[IDX][ 9] = PFX ## NUM ## _mc12_c; \
    c->PFX ## _pixels_tab[IDX][10] = PFX ## NUM ## _mc22_c; \
    c->PFX ## _pixels_tab[IDX][11] = PFX ## NUM ## _mc32_c; \
    c->PFX ## _pixels_tab[IDX][12] = PFX ## NUM ## _mc03_c; \
    c->PFX ## _pixels_tab[IDX][13] = PFX ## NUM ## _mc13_c; \
    c->PFX ## _pixels_tab[IDX][14] = PFX ## NUM ## _mc23_c; \
    c->PFX ## _pixels_tab[IDX][15] = PFX ## NUM ## _mc33_c
    dspfunc(put_cavs_qpel, 0, 16);
    dspfunc(put_cavs_qpel, 1, 8);
    dspfunc(avg_cavs_qpel, 0, 16);
    dspfunc(avg_cavs_qpel, 1, 8);
    c->cavs_filter_lv = cavs_filter_lv_c;
    c->cavs_filter_lh = cavs_filter_lh_c;
    c->cavs_filter_cv = cavs_filter_cv_c;
    c->cavs_filter_ch = cavs_filter_ch_c;
    c->cavs_idct8_add = cavs_idct8_add_c;
    c->idct_perm = FF_IDCT_PERM_NONE;

    if (ARCH_X86)
        ff_cavsdsp_init_x86(c, avctx);
}
