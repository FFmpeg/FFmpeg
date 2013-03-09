/*
 * H.26L/H.264/AVC/JVT/14496-10/... encoder/decoder
 * Copyright (c) 2003-2010 Michael Niedermayer <michaelni@gmx.at>
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

#include "libavutil/common.h"
#include "bit_depth_template.c"
#include "hpel_template.c"

static inline void FUNC(copy_block2)(uint8_t *dst, const uint8_t *src, int dstStride, int srcStride, int h)
{
    int i;
    for(i=0; i<h; i++)
    {
        AV_WN2P(dst   , AV_RN2P(src   ));
        dst+=dstStride;
        src+=srcStride;
    }
}

static inline void FUNC(copy_block4)(uint8_t *dst, const uint8_t *src, int dstStride, int srcStride, int h)
{
    int i;
    for(i=0; i<h; i++)
    {
        AV_WN4P(dst   , AV_RN4P(src   ));
        dst+=dstStride;
        src+=srcStride;
    }
}

static inline void FUNC(copy_block8)(uint8_t *dst, const uint8_t *src, int dstStride, int srcStride, int h)
{
    int i;
    for(i=0; i<h; i++)
    {
        AV_WN4P(dst                , AV_RN4P(src                ));
        AV_WN4P(dst+4*sizeof(pixel), AV_RN4P(src+4*sizeof(pixel)));
        dst+=dstStride;
        src+=srcStride;
    }
}

static inline void FUNC(copy_block16)(uint8_t *dst, const uint8_t *src, int dstStride, int srcStride, int h)
{
    int i;
    for(i=0; i<h; i++)
    {
        AV_WN4P(dst                 , AV_RN4P(src                 ));
        AV_WN4P(dst+ 4*sizeof(pixel), AV_RN4P(src+ 4*sizeof(pixel)));
        AV_WN4P(dst+ 8*sizeof(pixel), AV_RN4P(src+ 8*sizeof(pixel)));
        AV_WN4P(dst+12*sizeof(pixel), AV_RN4P(src+12*sizeof(pixel)));
        dst+=dstStride;
        src+=srcStride;
    }
}

#define H264_LOWPASS(OPNAME, OP, OP2) \
static av_unused void FUNC(OPNAME ## h264_qpel2_h_lowpass)(uint8_t *_dst, uint8_t *_src, int dstStride, int srcStride){\
    const int h=2;\
    INIT_CLIP\
    int i;\
    pixel *dst = (pixel*)_dst;\
    pixel *src = (pixel*)_src;\
    dstStride /= sizeof(pixel);\
    srcStride /= sizeof(pixel);\
    for(i=0; i<h; i++)\
    {\
        OP(dst[0], (src[0]+src[1])*20 - (src[-1]+src[2])*5 + (src[-2]+src[3]));\
        OP(dst[1], (src[1]+src[2])*20 - (src[0 ]+src[3])*5 + (src[-1]+src[4]));\
        dst+=dstStride;\
        src+=srcStride;\
    }\
}\
\
static av_unused void FUNC(OPNAME ## h264_qpel2_v_lowpass)(uint8_t *_dst, uint8_t *_src, int dstStride, int srcStride){\
    const int w=2;\
    INIT_CLIP\
    int i;\
    pixel *dst = (pixel*)_dst;\
    pixel *src = (pixel*)_src;\
    dstStride /= sizeof(pixel);\
    srcStride /= sizeof(pixel);\
    for(i=0; i<w; i++)\
    {\
        const int srcB= src[-2*srcStride];\
        const int srcA= src[-1*srcStride];\
        const int src0= src[0 *srcStride];\
        const int src1= src[1 *srcStride];\
        const int src2= src[2 *srcStride];\
        const int src3= src[3 *srcStride];\
        const int src4= src[4 *srcStride];\
        OP(dst[0*dstStride], (src0+src1)*20 - (srcA+src2)*5 + (srcB+src3));\
        OP(dst[1*dstStride], (src1+src2)*20 - (src0+src3)*5 + (srcA+src4));\
        dst++;\
        src++;\
    }\
}\
\
static av_unused void FUNC(OPNAME ## h264_qpel2_hv_lowpass)(uint8_t *_dst, int16_t *tmp, uint8_t *_src, int dstStride, int tmpStride, int srcStride){\
    const int h=2;\
    const int w=2;\
    const int pad = (BIT_DEPTH > 9) ? (-10 * ((1<<BIT_DEPTH)-1)) : 0;\
    INIT_CLIP\
    int i;\
    pixel *dst = (pixel*)_dst;\
    pixel *src = (pixel*)_src;\
    dstStride /= sizeof(pixel);\
    srcStride /= sizeof(pixel);\
    src -= 2*srcStride;\
    for(i=0; i<h+5; i++)\
    {\
        tmp[0]= (src[0]+src[1])*20 - (src[-1]+src[2])*5 + (src[-2]+src[3]) + pad;\
        tmp[1]= (src[1]+src[2])*20 - (src[0 ]+src[3])*5 + (src[-1]+src[4]) + pad;\
        tmp+=tmpStride;\
        src+=srcStride;\
    }\
    tmp -= tmpStride*(h+5-2);\
    for(i=0; i<w; i++)\
    {\
        const int tmpB= tmp[-2*tmpStride] - pad;\
        const int tmpA= tmp[-1*tmpStride] - pad;\
        const int tmp0= tmp[0 *tmpStride] - pad;\
        const int tmp1= tmp[1 *tmpStride] - pad;\
        const int tmp2= tmp[2 *tmpStride] - pad;\
        const int tmp3= tmp[3 *tmpStride] - pad;\
        const int tmp4= tmp[4 *tmpStride] - pad;\
        OP2(dst[0*dstStride], (tmp0+tmp1)*20 - (tmpA+tmp2)*5 + (tmpB+tmp3));\
        OP2(dst[1*dstStride], (tmp1+tmp2)*20 - (tmp0+tmp3)*5 + (tmpA+tmp4));\
        dst++;\
        tmp++;\
    }\
}\
static void FUNC(OPNAME ## h264_qpel4_h_lowpass)(uint8_t *_dst, uint8_t *_src, int dstStride, int srcStride){\
    const int h=4;\
    INIT_CLIP\
    int i;\
    pixel *dst = (pixel*)_dst;\
    pixel *src = (pixel*)_src;\
    dstStride /= sizeof(pixel);\
    srcStride /= sizeof(pixel);\
    for(i=0; i<h; i++)\
    {\
        OP(dst[0], (src[0]+src[1])*20 - (src[-1]+src[2])*5 + (src[-2]+src[3]));\
        OP(dst[1], (src[1]+src[2])*20 - (src[0 ]+src[3])*5 + (src[-1]+src[4]));\
        OP(dst[2], (src[2]+src[3])*20 - (src[1 ]+src[4])*5 + (src[0 ]+src[5]));\
        OP(dst[3], (src[3]+src[4])*20 - (src[2 ]+src[5])*5 + (src[1 ]+src[6]));\
        dst+=dstStride;\
        src+=srcStride;\
    }\
}\
\
static void FUNC(OPNAME ## h264_qpel4_v_lowpass)(uint8_t *_dst, uint8_t *_src, int dstStride, int srcStride){\
    const int w=4;\
    INIT_CLIP\
    int i;\
    pixel *dst = (pixel*)_dst;\
    pixel *src = (pixel*)_src;\
    dstStride /= sizeof(pixel);\
    srcStride /= sizeof(pixel);\
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
        OP(dst[0*dstStride], (src0+src1)*20 - (srcA+src2)*5 + (srcB+src3));\
        OP(dst[1*dstStride], (src1+src2)*20 - (src0+src3)*5 + (srcA+src4));\
        OP(dst[2*dstStride], (src2+src3)*20 - (src1+src4)*5 + (src0+src5));\
        OP(dst[3*dstStride], (src3+src4)*20 - (src2+src5)*5 + (src1+src6));\
        dst++;\
        src++;\
    }\
}\
\
static void FUNC(OPNAME ## h264_qpel4_hv_lowpass)(uint8_t *_dst, int16_t *tmp, uint8_t *_src, int dstStride, int tmpStride, int srcStride){\
    const int h=4;\
    const int w=4;\
    const int pad = (BIT_DEPTH > 9) ? (-10 * ((1<<BIT_DEPTH)-1)) : 0;\
    INIT_CLIP\
    int i;\
    pixel *dst = (pixel*)_dst;\
    pixel *src = (pixel*)_src;\
    dstStride /= sizeof(pixel);\
    srcStride /= sizeof(pixel);\
    src -= 2*srcStride;\
    for(i=0; i<h+5; i++)\
    {\
        tmp[0]= (src[0]+src[1])*20 - (src[-1]+src[2])*5 + (src[-2]+src[3]) + pad;\
        tmp[1]= (src[1]+src[2])*20 - (src[0 ]+src[3])*5 + (src[-1]+src[4]) + pad;\
        tmp[2]= (src[2]+src[3])*20 - (src[1 ]+src[4])*5 + (src[0 ]+src[5]) + pad;\
        tmp[3]= (src[3]+src[4])*20 - (src[2 ]+src[5])*5 + (src[1 ]+src[6]) + pad;\
        tmp+=tmpStride;\
        src+=srcStride;\
    }\
    tmp -= tmpStride*(h+5-2);\
    for(i=0; i<w; i++)\
    {\
        const int tmpB= tmp[-2*tmpStride] - pad;\
        const int tmpA= tmp[-1*tmpStride] - pad;\
        const int tmp0= tmp[0 *tmpStride] - pad;\
        const int tmp1= tmp[1 *tmpStride] - pad;\
        const int tmp2= tmp[2 *tmpStride] - pad;\
        const int tmp3= tmp[3 *tmpStride] - pad;\
        const int tmp4= tmp[4 *tmpStride] - pad;\
        const int tmp5= tmp[5 *tmpStride] - pad;\
        const int tmp6= tmp[6 *tmpStride] - pad;\
        OP2(dst[0*dstStride], (tmp0+tmp1)*20 - (tmpA+tmp2)*5 + (tmpB+tmp3));\
        OP2(dst[1*dstStride], (tmp1+tmp2)*20 - (tmp0+tmp3)*5 + (tmpA+tmp4));\
        OP2(dst[2*dstStride], (tmp2+tmp3)*20 - (tmp1+tmp4)*5 + (tmp0+tmp5));\
        OP2(dst[3*dstStride], (tmp3+tmp4)*20 - (tmp2+tmp5)*5 + (tmp1+tmp6));\
        dst++;\
        tmp++;\
    }\
}\
\
static void FUNC(OPNAME ## h264_qpel8_h_lowpass)(uint8_t *_dst, uint8_t *_src, int dstStride, int srcStride){\
    const int h=8;\
    INIT_CLIP\
    int i;\
    pixel *dst = (pixel*)_dst;\
    pixel *src = (pixel*)_src;\
    dstStride /= sizeof(pixel);\
    srcStride /= sizeof(pixel);\
    for(i=0; i<h; i++)\
    {\
        OP(dst[0], (src[0]+src[1])*20 - (src[-1]+src[2])*5 + (src[-2]+src[3 ]));\
        OP(dst[1], (src[1]+src[2])*20 - (src[0 ]+src[3])*5 + (src[-1]+src[4 ]));\
        OP(dst[2], (src[2]+src[3])*20 - (src[1 ]+src[4])*5 + (src[0 ]+src[5 ]));\
        OP(dst[3], (src[3]+src[4])*20 - (src[2 ]+src[5])*5 + (src[1 ]+src[6 ]));\
        OP(dst[4], (src[4]+src[5])*20 - (src[3 ]+src[6])*5 + (src[2 ]+src[7 ]));\
        OP(dst[5], (src[5]+src[6])*20 - (src[4 ]+src[7])*5 + (src[3 ]+src[8 ]));\
        OP(dst[6], (src[6]+src[7])*20 - (src[5 ]+src[8])*5 + (src[4 ]+src[9 ]));\
        OP(dst[7], (src[7]+src[8])*20 - (src[6 ]+src[9])*5 + (src[5 ]+src[10]));\
        dst+=dstStride;\
        src+=srcStride;\
    }\
}\
\
static void FUNC(OPNAME ## h264_qpel8_v_lowpass)(uint8_t *_dst, uint8_t *_src, int dstStride, int srcStride){\
    const int w=8;\
    INIT_CLIP\
    int i;\
    pixel *dst = (pixel*)_dst;\
    pixel *src = (pixel*)_src;\
    dstStride /= sizeof(pixel);\
    srcStride /= sizeof(pixel);\
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
        const int src10=src[10*srcStride];\
        OP(dst[0*dstStride], (src0+src1)*20 - (srcA+src2)*5 + (srcB+src3));\
        OP(dst[1*dstStride], (src1+src2)*20 - (src0+src3)*5 + (srcA+src4));\
        OP(dst[2*dstStride], (src2+src3)*20 - (src1+src4)*5 + (src0+src5));\
        OP(dst[3*dstStride], (src3+src4)*20 - (src2+src5)*5 + (src1+src6));\
        OP(dst[4*dstStride], (src4+src5)*20 - (src3+src6)*5 + (src2+src7));\
        OP(dst[5*dstStride], (src5+src6)*20 - (src4+src7)*5 + (src3+src8));\
        OP(dst[6*dstStride], (src6+src7)*20 - (src5+src8)*5 + (src4+src9));\
        OP(dst[7*dstStride], (src7+src8)*20 - (src6+src9)*5 + (src5+src10));\
        dst++;\
        src++;\
    }\
}\
\
static void FUNC(OPNAME ## h264_qpel8_hv_lowpass)(uint8_t *_dst, int16_t *tmp, uint8_t *_src, int dstStride, int tmpStride, int srcStride){\
    const int h=8;\
    const int w=8;\
    const int pad = (BIT_DEPTH > 9) ? (-10 * ((1<<BIT_DEPTH)-1)) : 0;\
    INIT_CLIP\
    int i;\
    pixel *dst = (pixel*)_dst;\
    pixel *src = (pixel*)_src;\
    dstStride /= sizeof(pixel);\
    srcStride /= sizeof(pixel);\
    src -= 2*srcStride;\
    for(i=0; i<h+5; i++)\
    {\
        tmp[0]= (src[0]+src[1])*20 - (src[-1]+src[2])*5 + (src[-2]+src[3 ]) + pad;\
        tmp[1]= (src[1]+src[2])*20 - (src[0 ]+src[3])*5 + (src[-1]+src[4 ]) + pad;\
        tmp[2]= (src[2]+src[3])*20 - (src[1 ]+src[4])*5 + (src[0 ]+src[5 ]) + pad;\
        tmp[3]= (src[3]+src[4])*20 - (src[2 ]+src[5])*5 + (src[1 ]+src[6 ]) + pad;\
        tmp[4]= (src[4]+src[5])*20 - (src[3 ]+src[6])*5 + (src[2 ]+src[7 ]) + pad;\
        tmp[5]= (src[5]+src[6])*20 - (src[4 ]+src[7])*5 + (src[3 ]+src[8 ]) + pad;\
        tmp[6]= (src[6]+src[7])*20 - (src[5 ]+src[8])*5 + (src[4 ]+src[9 ]) + pad;\
        tmp[7]= (src[7]+src[8])*20 - (src[6 ]+src[9])*5 + (src[5 ]+src[10]) + pad;\
        tmp+=tmpStride;\
        src+=srcStride;\
    }\
    tmp -= tmpStride*(h+5-2);\
    for(i=0; i<w; i++)\
    {\
        const int tmpB= tmp[-2*tmpStride] - pad;\
        const int tmpA= tmp[-1*tmpStride] - pad;\
        const int tmp0= tmp[0 *tmpStride] - pad;\
        const int tmp1= tmp[1 *tmpStride] - pad;\
        const int tmp2= tmp[2 *tmpStride] - pad;\
        const int tmp3= tmp[3 *tmpStride] - pad;\
        const int tmp4= tmp[4 *tmpStride] - pad;\
        const int tmp5= tmp[5 *tmpStride] - pad;\
        const int tmp6= tmp[6 *tmpStride] - pad;\
        const int tmp7= tmp[7 *tmpStride] - pad;\
        const int tmp8= tmp[8 *tmpStride] - pad;\
        const int tmp9= tmp[9 *tmpStride] - pad;\
        const int tmp10=tmp[10*tmpStride] - pad;\
        OP2(dst[0*dstStride], (tmp0+tmp1)*20 - (tmpA+tmp2)*5 + (tmpB+tmp3));\
        OP2(dst[1*dstStride], (tmp1+tmp2)*20 - (tmp0+tmp3)*5 + (tmpA+tmp4));\
        OP2(dst[2*dstStride], (tmp2+tmp3)*20 - (tmp1+tmp4)*5 + (tmp0+tmp5));\
        OP2(dst[3*dstStride], (tmp3+tmp4)*20 - (tmp2+tmp5)*5 + (tmp1+tmp6));\
        OP2(dst[4*dstStride], (tmp4+tmp5)*20 - (tmp3+tmp6)*5 + (tmp2+tmp7));\
        OP2(dst[5*dstStride], (tmp5+tmp6)*20 - (tmp4+tmp7)*5 + (tmp3+tmp8));\
        OP2(dst[6*dstStride], (tmp6+tmp7)*20 - (tmp5+tmp8)*5 + (tmp4+tmp9));\
        OP2(dst[7*dstStride], (tmp7+tmp8)*20 - (tmp6+tmp9)*5 + (tmp5+tmp10));\
        dst++;\
        tmp++;\
    }\
}\
\
static void FUNC(OPNAME ## h264_qpel16_v_lowpass)(uint8_t *dst, uint8_t *src, int dstStride, int srcStride){\
    FUNC(OPNAME ## h264_qpel8_v_lowpass)(dst                , src                , dstStride, srcStride);\
    FUNC(OPNAME ## h264_qpel8_v_lowpass)(dst+8*sizeof(pixel), src+8*sizeof(pixel), dstStride, srcStride);\
    src += 8*srcStride;\
    dst += 8*dstStride;\
    FUNC(OPNAME ## h264_qpel8_v_lowpass)(dst                , src                , dstStride, srcStride);\
    FUNC(OPNAME ## h264_qpel8_v_lowpass)(dst+8*sizeof(pixel), src+8*sizeof(pixel), dstStride, srcStride);\
}\
\
static void FUNC(OPNAME ## h264_qpel16_h_lowpass)(uint8_t *dst, uint8_t *src, int dstStride, int srcStride){\
    FUNC(OPNAME ## h264_qpel8_h_lowpass)(dst                , src                , dstStride, srcStride);\
    FUNC(OPNAME ## h264_qpel8_h_lowpass)(dst+8*sizeof(pixel), src+8*sizeof(pixel), dstStride, srcStride);\
    src += 8*srcStride;\
    dst += 8*dstStride;\
    FUNC(OPNAME ## h264_qpel8_h_lowpass)(dst                , src                , dstStride, srcStride);\
    FUNC(OPNAME ## h264_qpel8_h_lowpass)(dst+8*sizeof(pixel), src+8*sizeof(pixel), dstStride, srcStride);\
}\
\
static void FUNC(OPNAME ## h264_qpel16_hv_lowpass)(uint8_t *dst, int16_t *tmp, uint8_t *src, int dstStride, int tmpStride, int srcStride){\
    FUNC(OPNAME ## h264_qpel8_hv_lowpass)(dst                , tmp  , src                , dstStride, tmpStride, srcStride);\
    FUNC(OPNAME ## h264_qpel8_hv_lowpass)(dst+8*sizeof(pixel), tmp+8, src+8*sizeof(pixel), dstStride, tmpStride, srcStride);\
    src += 8*srcStride;\
    dst += 8*dstStride;\
    FUNC(OPNAME ## h264_qpel8_hv_lowpass)(dst                , tmp  , src                , dstStride, tmpStride, srcStride);\
    FUNC(OPNAME ## h264_qpel8_hv_lowpass)(dst+8*sizeof(pixel), tmp+8, src+8*sizeof(pixel), dstStride, tmpStride, srcStride);\
}\

#define H264_MC(OPNAME, SIZE) \
static av_unused void FUNCC(OPNAME ## h264_qpel ## SIZE ## _mc00)(uint8_t *dst, uint8_t *src, ptrdiff_t stride)\
{\
    FUNCC(OPNAME ## pixels ## SIZE)(dst, src, stride, SIZE);\
}\
\
static void FUNCC(OPNAME ## h264_qpel ## SIZE ## _mc10)(uint8_t *dst, uint8_t *src, ptrdiff_t stride)\
{\
    uint8_t half[SIZE*SIZE*sizeof(pixel)];\
    FUNC(put_h264_qpel ## SIZE ## _h_lowpass)(half, src, SIZE*sizeof(pixel), stride);\
    FUNC(OPNAME ## pixels ## SIZE ## _l2)(dst, src, half, stride, stride, SIZE*sizeof(pixel), SIZE);\
}\
\
static void FUNCC(OPNAME ## h264_qpel ## SIZE ## _mc20)(uint8_t *dst, uint8_t *src, ptrdiff_t stride)\
{\
    FUNC(OPNAME ## h264_qpel ## SIZE ## _h_lowpass)(dst, src, stride, stride);\
}\
\
static void FUNCC(OPNAME ## h264_qpel ## SIZE ## _mc30)(uint8_t *dst, uint8_t *src, ptrdiff_t stride)\
{\
    uint8_t half[SIZE*SIZE*sizeof(pixel)];\
    FUNC(put_h264_qpel ## SIZE ## _h_lowpass)(half, src, SIZE*sizeof(pixel), stride);\
    FUNC(OPNAME ## pixels ## SIZE ## _l2)(dst, src+sizeof(pixel), half, stride, stride, SIZE*sizeof(pixel), SIZE);\
}\
\
static void FUNCC(OPNAME ## h264_qpel ## SIZE ## _mc01)(uint8_t *dst, uint8_t *src, ptrdiff_t stride)\
{\
    uint8_t full[SIZE*(SIZE+5)*sizeof(pixel)];\
    uint8_t * const full_mid= full + SIZE*2*sizeof(pixel);\
    uint8_t half[SIZE*SIZE*sizeof(pixel)];\
    FUNC(copy_block ## SIZE )(full, src - stride*2, SIZE*sizeof(pixel),  stride, SIZE + 5);\
    FUNC(put_h264_qpel ## SIZE ## _v_lowpass)(half, full_mid, SIZE*sizeof(pixel), SIZE*sizeof(pixel));\
    FUNC(OPNAME ## pixels ## SIZE ## _l2)(dst, full_mid, half, stride, SIZE*sizeof(pixel), SIZE*sizeof(pixel), SIZE);\
}\
\
static void FUNCC(OPNAME ## h264_qpel ## SIZE ## _mc02)(uint8_t *dst, uint8_t *src, ptrdiff_t stride)\
{\
    uint8_t full[SIZE*(SIZE+5)*sizeof(pixel)];\
    uint8_t * const full_mid= full + SIZE*2*sizeof(pixel);\
    FUNC(copy_block ## SIZE )(full, src - stride*2, SIZE*sizeof(pixel),  stride, SIZE + 5);\
    FUNC(OPNAME ## h264_qpel ## SIZE ## _v_lowpass)(dst, full_mid, stride, SIZE*sizeof(pixel));\
}\
\
static void FUNCC(OPNAME ## h264_qpel ## SIZE ## _mc03)(uint8_t *dst, uint8_t *src, ptrdiff_t stride)\
{\
    uint8_t full[SIZE*(SIZE+5)*sizeof(pixel)];\
    uint8_t * const full_mid= full + SIZE*2*sizeof(pixel);\
    uint8_t half[SIZE*SIZE*sizeof(pixel)];\
    FUNC(copy_block ## SIZE )(full, src - stride*2, SIZE*sizeof(pixel),  stride, SIZE + 5);\
    FUNC(put_h264_qpel ## SIZE ## _v_lowpass)(half, full_mid, SIZE*sizeof(pixel), SIZE*sizeof(pixel));\
    FUNC(OPNAME ## pixels ## SIZE ## _l2)(dst, full_mid+SIZE*sizeof(pixel), half, stride, SIZE*sizeof(pixel), SIZE*sizeof(pixel), SIZE);\
}\
\
static void FUNCC(OPNAME ## h264_qpel ## SIZE ## _mc11)(uint8_t *dst, uint8_t *src, ptrdiff_t stride)\
{\
    uint8_t full[SIZE*(SIZE+5)*sizeof(pixel)];\
    uint8_t * const full_mid= full + SIZE*2*sizeof(pixel);\
    uint8_t halfH[SIZE*SIZE*sizeof(pixel)];\
    uint8_t halfV[SIZE*SIZE*sizeof(pixel)];\
    FUNC(put_h264_qpel ## SIZE ## _h_lowpass)(halfH, src, SIZE*sizeof(pixel), stride);\
    FUNC(copy_block ## SIZE )(full, src - stride*2, SIZE*sizeof(pixel),  stride, SIZE + 5);\
    FUNC(put_h264_qpel ## SIZE ## _v_lowpass)(halfV, full_mid, SIZE*sizeof(pixel), SIZE*sizeof(pixel));\
    FUNC(OPNAME ## pixels ## SIZE ## _l2)(dst, halfH, halfV, stride, SIZE*sizeof(pixel), SIZE*sizeof(pixel), SIZE);\
}\
\
static void FUNCC(OPNAME ## h264_qpel ## SIZE ## _mc31)(uint8_t *dst, uint8_t *src, ptrdiff_t stride)\
{\
    uint8_t full[SIZE*(SIZE+5)*sizeof(pixel)];\
    uint8_t * const full_mid= full + SIZE*2*sizeof(pixel);\
    uint8_t halfH[SIZE*SIZE*sizeof(pixel)];\
    uint8_t halfV[SIZE*SIZE*sizeof(pixel)];\
    FUNC(put_h264_qpel ## SIZE ## _h_lowpass)(halfH, src, SIZE*sizeof(pixel), stride);\
    FUNC(copy_block ## SIZE )(full, src - stride*2 + sizeof(pixel), SIZE*sizeof(pixel),  stride, SIZE + 5);\
    FUNC(put_h264_qpel ## SIZE ## _v_lowpass)(halfV, full_mid, SIZE*sizeof(pixel), SIZE*sizeof(pixel));\
    FUNC(OPNAME ## pixels ## SIZE ## _l2)(dst, halfH, halfV, stride, SIZE*sizeof(pixel), SIZE*sizeof(pixel), SIZE);\
}\
\
static void FUNCC(OPNAME ## h264_qpel ## SIZE ## _mc13)(uint8_t *dst, uint8_t *src, ptrdiff_t stride)\
{\
    uint8_t full[SIZE*(SIZE+5)*sizeof(pixel)];\
    uint8_t * const full_mid= full + SIZE*2*sizeof(pixel);\
    uint8_t halfH[SIZE*SIZE*sizeof(pixel)];\
    uint8_t halfV[SIZE*SIZE*sizeof(pixel)];\
    FUNC(put_h264_qpel ## SIZE ## _h_lowpass)(halfH, src + stride, SIZE*sizeof(pixel), stride);\
    FUNC(copy_block ## SIZE )(full, src - stride*2, SIZE*sizeof(pixel),  stride, SIZE + 5);\
    FUNC(put_h264_qpel ## SIZE ## _v_lowpass)(halfV, full_mid, SIZE*sizeof(pixel), SIZE*sizeof(pixel));\
    FUNC(OPNAME ## pixels ## SIZE ## _l2)(dst, halfH, halfV, stride, SIZE*sizeof(pixel), SIZE*sizeof(pixel), SIZE);\
}\
\
static void FUNCC(OPNAME ## h264_qpel ## SIZE ## _mc33)(uint8_t *dst, uint8_t *src, ptrdiff_t stride)\
{\
    uint8_t full[SIZE*(SIZE+5)*sizeof(pixel)];\
    uint8_t * const full_mid= full + SIZE*2*sizeof(pixel);\
    uint8_t halfH[SIZE*SIZE*sizeof(pixel)];\
    uint8_t halfV[SIZE*SIZE*sizeof(pixel)];\
    FUNC(put_h264_qpel ## SIZE ## _h_lowpass)(halfH, src + stride, SIZE*sizeof(pixel), stride);\
    FUNC(copy_block ## SIZE )(full, src - stride*2 + sizeof(pixel), SIZE*sizeof(pixel),  stride, SIZE + 5);\
    FUNC(put_h264_qpel ## SIZE ## _v_lowpass)(halfV, full_mid, SIZE*sizeof(pixel), SIZE*sizeof(pixel));\
    FUNC(OPNAME ## pixels ## SIZE ## _l2)(dst, halfH, halfV, stride, SIZE*sizeof(pixel), SIZE*sizeof(pixel), SIZE);\
}\
\
static void FUNCC(OPNAME ## h264_qpel ## SIZE ## _mc22)(uint8_t *dst, uint8_t *src, ptrdiff_t stride)\
{\
    int16_t tmp[SIZE*(SIZE+5)*sizeof(pixel)];\
    FUNC(OPNAME ## h264_qpel ## SIZE ## _hv_lowpass)(dst, tmp, src, stride, SIZE*sizeof(pixel), stride);\
}\
\
static void FUNCC(OPNAME ## h264_qpel ## SIZE ## _mc21)(uint8_t *dst, uint8_t *src, ptrdiff_t stride)\
{\
    int16_t tmp[SIZE*(SIZE+5)*sizeof(pixel)];\
    uint8_t halfH[SIZE*SIZE*sizeof(pixel)];\
    uint8_t halfHV[SIZE*SIZE*sizeof(pixel)];\
    FUNC(put_h264_qpel ## SIZE ## _h_lowpass)(halfH, src, SIZE*sizeof(pixel), stride);\
    FUNC(put_h264_qpel ## SIZE ## _hv_lowpass)(halfHV, tmp, src, SIZE*sizeof(pixel), SIZE*sizeof(pixel), stride);\
    FUNC(OPNAME ## pixels ## SIZE ## _l2)(dst, halfH, halfHV, stride, SIZE*sizeof(pixel), SIZE*sizeof(pixel), SIZE);\
}\
\
static void FUNCC(OPNAME ## h264_qpel ## SIZE ## _mc23)(uint8_t *dst, uint8_t *src, ptrdiff_t stride)\
{\
    int16_t tmp[SIZE*(SIZE+5)*sizeof(pixel)];\
    uint8_t halfH[SIZE*SIZE*sizeof(pixel)];\
    uint8_t halfHV[SIZE*SIZE*sizeof(pixel)];\
    FUNC(put_h264_qpel ## SIZE ## _h_lowpass)(halfH, src + stride, SIZE*sizeof(pixel), stride);\
    FUNC(put_h264_qpel ## SIZE ## _hv_lowpass)(halfHV, tmp, src, SIZE*sizeof(pixel), SIZE*sizeof(pixel), stride);\
    FUNC(OPNAME ## pixels ## SIZE ## _l2)(dst, halfH, halfHV, stride, SIZE*sizeof(pixel), SIZE*sizeof(pixel), SIZE);\
}\
\
static void FUNCC(OPNAME ## h264_qpel ## SIZE ## _mc12)(uint8_t *dst, uint8_t *src, ptrdiff_t stride)\
{\
    uint8_t full[SIZE*(SIZE+5)*sizeof(pixel)];\
    uint8_t * const full_mid= full + SIZE*2*sizeof(pixel);\
    int16_t tmp[SIZE*(SIZE+5)*sizeof(pixel)];\
    uint8_t halfV[SIZE*SIZE*sizeof(pixel)];\
    uint8_t halfHV[SIZE*SIZE*sizeof(pixel)];\
    FUNC(copy_block ## SIZE )(full, src - stride*2, SIZE*sizeof(pixel),  stride, SIZE + 5);\
    FUNC(put_h264_qpel ## SIZE ## _v_lowpass)(halfV, full_mid, SIZE*sizeof(pixel), SIZE*sizeof(pixel));\
    FUNC(put_h264_qpel ## SIZE ## _hv_lowpass)(halfHV, tmp, src, SIZE*sizeof(pixel), SIZE*sizeof(pixel), stride);\
    FUNC(OPNAME ## pixels ## SIZE ## _l2)(dst, halfV, halfHV, stride, SIZE*sizeof(pixel), SIZE*sizeof(pixel), SIZE);\
}\
\
static void FUNCC(OPNAME ## h264_qpel ## SIZE ## _mc32)(uint8_t *dst, uint8_t *src, ptrdiff_t stride)\
{\
    uint8_t full[SIZE*(SIZE+5)*sizeof(pixel)];\
    uint8_t * const full_mid= full + SIZE*2*sizeof(pixel);\
    int16_t tmp[SIZE*(SIZE+5)*sizeof(pixel)];\
    uint8_t halfV[SIZE*SIZE*sizeof(pixel)];\
    uint8_t halfHV[SIZE*SIZE*sizeof(pixel)];\
    FUNC(copy_block ## SIZE )(full, src - stride*2 + sizeof(pixel), SIZE*sizeof(pixel),  stride, SIZE + 5);\
    FUNC(put_h264_qpel ## SIZE ## _v_lowpass)(halfV, full_mid, SIZE*sizeof(pixel), SIZE*sizeof(pixel));\
    FUNC(put_h264_qpel ## SIZE ## _hv_lowpass)(halfHV, tmp, src, SIZE*sizeof(pixel), SIZE*sizeof(pixel), stride);\
    FUNC(OPNAME ## pixels ## SIZE ## _l2)(dst, halfV, halfHV, stride, SIZE*sizeof(pixel), SIZE*sizeof(pixel), SIZE);\
}\

#define op_avg(a, b)  a = (((a)+CLIP(((b) + 16)>>5)+1)>>1)
//#define op_avg2(a, b) a = (((a)*w1+cm[((b) + 16)>>5]*w2 + o + 64)>>7)
#define op_put(a, b)  a = CLIP(((b) + 16)>>5)
#define op2_avg(a, b)  a = (((a)+CLIP(((b) + 512)>>10)+1)>>1)
#define op2_put(a, b)  a = CLIP(((b) + 512)>>10)

H264_LOWPASS(put_       , op_put, op2_put)
H264_LOWPASS(avg_       , op_avg, op2_avg)
H264_MC(put_, 2)
H264_MC(put_, 4)
H264_MC(put_, 8)
H264_MC(put_, 16)
H264_MC(avg_, 4)
H264_MC(avg_, 8)
H264_MC(avg_, 16)

#undef op_avg
#undef op_put
#undef op2_avg
#undef op2_put
