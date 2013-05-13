/*
 * H.26L/H.264/AVC/JVT/14496-10/... encoder/decoder
 * Copyright (c) 2003-2011 Michael Niedermayer <michaelni@gmx.at>
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
 * H.264 / AVC / MPEG4 part10 prediction functions.
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#include "mathops.h"

#include "bit_depth_template.c"

static void FUNCC(pred4x4_vertical)(uint8_t *_src, const uint8_t *topright,
                                    ptrdiff_t _stride)
{
    pixel *src = (pixel*)_src;
    int stride = _stride>>(sizeof(pixel)-1);
    const pixel4 a= AV_RN4PA(src-stride);

    AV_WN4PA(src+0*stride, a);
    AV_WN4PA(src+1*stride, a);
    AV_WN4PA(src+2*stride, a);
    AV_WN4PA(src+3*stride, a);
}

static void FUNCC(pred4x4_horizontal)(uint8_t *_src, const uint8_t *topright,
                                      ptrdiff_t _stride)
{
    pixel *src = (pixel*)_src;
    int stride = _stride>>(sizeof(pixel)-1);
    AV_WN4PA(src+0*stride, PIXEL_SPLAT_X4(src[-1+0*stride]));
    AV_WN4PA(src+1*stride, PIXEL_SPLAT_X4(src[-1+1*stride]));
    AV_WN4PA(src+2*stride, PIXEL_SPLAT_X4(src[-1+2*stride]));
    AV_WN4PA(src+3*stride, PIXEL_SPLAT_X4(src[-1+3*stride]));
}

static void FUNCC(pred4x4_dc)(uint8_t *_src, const uint8_t *topright,
                              ptrdiff_t _stride)
{
    pixel *src = (pixel*)_src;
    int stride = _stride>>(sizeof(pixel)-1);
    const int dc= (  src[-stride] + src[1-stride] + src[2-stride] + src[3-stride]
                   + src[-1+0*stride] + src[-1+1*stride] + src[-1+2*stride] + src[-1+3*stride] + 4) >>3;
    const pixel4 a = PIXEL_SPLAT_X4(dc);

    AV_WN4PA(src+0*stride, a);
    AV_WN4PA(src+1*stride, a);
    AV_WN4PA(src+2*stride, a);
    AV_WN4PA(src+3*stride, a);
}

static void FUNCC(pred4x4_left_dc)(uint8_t *_src, const uint8_t *topright,
                                   ptrdiff_t _stride)
{
    pixel *src = (pixel*)_src;
    int stride = _stride>>(sizeof(pixel)-1);
    const int dc= (  src[-1+0*stride] + src[-1+1*stride] + src[-1+2*stride] + src[-1+3*stride] + 2) >>2;
    const pixel4 a = PIXEL_SPLAT_X4(dc);

    AV_WN4PA(src+0*stride, a);
    AV_WN4PA(src+1*stride, a);
    AV_WN4PA(src+2*stride, a);
    AV_WN4PA(src+3*stride, a);
}

static void FUNCC(pred4x4_top_dc)(uint8_t *_src, const uint8_t *topright,
                                  ptrdiff_t _stride)
{
    pixel *src = (pixel*)_src;
    int stride = _stride>>(sizeof(pixel)-1);
    const int dc= (  src[-stride] + src[1-stride] + src[2-stride] + src[3-stride] + 2) >>2;
    const pixel4 a = PIXEL_SPLAT_X4(dc);

    AV_WN4PA(src+0*stride, a);
    AV_WN4PA(src+1*stride, a);
    AV_WN4PA(src+2*stride, a);
    AV_WN4PA(src+3*stride, a);
}

static void FUNCC(pred4x4_128_dc)(uint8_t *_src, const uint8_t *topright,
                                  ptrdiff_t _stride)
{
    pixel *src = (pixel*)_src;
    int stride = _stride>>(sizeof(pixel)-1);
    const pixel4 a = PIXEL_SPLAT_X4(1<<(BIT_DEPTH-1));

    AV_WN4PA(src+0*stride, a);
    AV_WN4PA(src+1*stride, a);
    AV_WN4PA(src+2*stride, a);
    AV_WN4PA(src+3*stride, a);
}

static void FUNCC(pred4x4_127_dc)(uint8_t *_src, const uint8_t *topright,
                                  ptrdiff_t _stride)
{
    pixel *src = (pixel*)_src;
    int stride = _stride>>(sizeof(pixel)-1);
    const pixel4 a = PIXEL_SPLAT_X4((1<<(BIT_DEPTH-1))-1);

    AV_WN4PA(src+0*stride, a);
    AV_WN4PA(src+1*stride, a);
    AV_WN4PA(src+2*stride, a);
    AV_WN4PA(src+3*stride, a);
}

static void FUNCC(pred4x4_129_dc)(uint8_t *_src, const uint8_t *topright,
                                  ptrdiff_t _stride)
{
    pixel *src = (pixel*)_src;
    int stride = _stride>>(sizeof(pixel)-1);
    const pixel4 a = PIXEL_SPLAT_X4((1<<(BIT_DEPTH-1))+1);

    AV_WN4PA(src+0*stride, a);
    AV_WN4PA(src+1*stride, a);
    AV_WN4PA(src+2*stride, a);
    AV_WN4PA(src+3*stride, a);
}


#define LOAD_TOP_RIGHT_EDGE\
    const unsigned av_unused t4 = topright[0];\
    const unsigned av_unused t5 = topright[1];\
    const unsigned av_unused t6 = topright[2];\
    const unsigned av_unused t7 = topright[3];\

#define LOAD_DOWN_LEFT_EDGE\
    const unsigned av_unused l4 = src[-1+4*stride];\
    const unsigned av_unused l5 = src[-1+5*stride];\
    const unsigned av_unused l6 = src[-1+6*stride];\
    const unsigned av_unused l7 = src[-1+7*stride];\

#define LOAD_LEFT_EDGE\
    const unsigned av_unused l0 = src[-1+0*stride];\
    const unsigned av_unused l1 = src[-1+1*stride];\
    const unsigned av_unused l2 = src[-1+2*stride];\
    const unsigned av_unused l3 = src[-1+3*stride];\

#define LOAD_TOP_EDGE\
    const unsigned av_unused t0 = src[ 0-1*stride];\
    const unsigned av_unused t1 = src[ 1-1*stride];\
    const unsigned av_unused t2 = src[ 2-1*stride];\
    const unsigned av_unused t3 = src[ 3-1*stride];\

static void FUNCC(pred4x4_down_right)(uint8_t *_src, const uint8_t *topright,
                                      ptrdiff_t _stride)
{
    pixel *src = (pixel*)_src;
    int stride = _stride>>(sizeof(pixel)-1);
    const int lt= src[-1-1*stride];
    LOAD_TOP_EDGE
    LOAD_LEFT_EDGE

    src[0+3*stride]=(l3 + 2*l2 + l1 + 2)>>2;
    src[0+2*stride]=
    src[1+3*stride]=(l2 + 2*l1 + l0 + 2)>>2;
    src[0+1*stride]=
    src[1+2*stride]=
    src[2+3*stride]=(l1 + 2*l0 + lt + 2)>>2;
    src[0+0*stride]=
    src[1+1*stride]=
    src[2+2*stride]=
    src[3+3*stride]=(l0 + 2*lt + t0 + 2)>>2;
    src[1+0*stride]=
    src[2+1*stride]=
    src[3+2*stride]=(lt + 2*t0 + t1 + 2)>>2;
    src[2+0*stride]=
    src[3+1*stride]=(t0 + 2*t1 + t2 + 2)>>2;
    src[3+0*stride]=(t1 + 2*t2 + t3 + 2)>>2;
}

static void FUNCC(pred4x4_down_left)(uint8_t *_src, const uint8_t *_topright,
                                     ptrdiff_t _stride)
{
    pixel *src = (pixel*)_src;
    const pixel *topright = (const pixel*)_topright;
    int stride = _stride>>(sizeof(pixel)-1);
    LOAD_TOP_EDGE
    LOAD_TOP_RIGHT_EDGE
//    LOAD_LEFT_EDGE

    src[0+0*stride]=(t0 + t2 + 2*t1 + 2)>>2;
    src[1+0*stride]=
    src[0+1*stride]=(t1 + t3 + 2*t2 + 2)>>2;
    src[2+0*stride]=
    src[1+1*stride]=
    src[0+2*stride]=(t2 + t4 + 2*t3 + 2)>>2;
    src[3+0*stride]=
    src[2+1*stride]=
    src[1+2*stride]=
    src[0+3*stride]=(t3 + t5 + 2*t4 + 2)>>2;
    src[3+1*stride]=
    src[2+2*stride]=
    src[1+3*stride]=(t4 + t6 + 2*t5 + 2)>>2;
    src[3+2*stride]=
    src[2+3*stride]=(t5 + t7 + 2*t6 + 2)>>2;
    src[3+3*stride]=(t6 + 3*t7 + 2)>>2;
}

static void FUNCC(pred4x4_vertical_right)(uint8_t *_src,
                                          const uint8_t *topright,
                                          ptrdiff_t _stride)
{
    pixel *src = (pixel*)_src;
    int stride = _stride>>(sizeof(pixel)-1);
    const int lt= src[-1-1*stride];
    LOAD_TOP_EDGE
    LOAD_LEFT_EDGE

    src[0+0*stride]=
    src[1+2*stride]=(lt + t0 + 1)>>1;
    src[1+0*stride]=
    src[2+2*stride]=(t0 + t1 + 1)>>1;
    src[2+0*stride]=
    src[3+2*stride]=(t1 + t2 + 1)>>1;
    src[3+0*stride]=(t2 + t3 + 1)>>1;
    src[0+1*stride]=
    src[1+3*stride]=(l0 + 2*lt + t0 + 2)>>2;
    src[1+1*stride]=
    src[2+3*stride]=(lt + 2*t0 + t1 + 2)>>2;
    src[2+1*stride]=
    src[3+3*stride]=(t0 + 2*t1 + t2 + 2)>>2;
    src[3+1*stride]=(t1 + 2*t2 + t3 + 2)>>2;
    src[0+2*stride]=(lt + 2*l0 + l1 + 2)>>2;
    src[0+3*stride]=(l0 + 2*l1 + l2 + 2)>>2;
}

static void FUNCC(pred4x4_vertical_left)(uint8_t *_src,
                                         const uint8_t *_topright,
                                         ptrdiff_t _stride)
{
    pixel *src = (pixel*)_src;
    const pixel *topright = (const pixel*)_topright;
    int stride = _stride>>(sizeof(pixel)-1);
    LOAD_TOP_EDGE
    LOAD_TOP_RIGHT_EDGE

    src[0+0*stride]=(t0 + t1 + 1)>>1;
    src[1+0*stride]=
    src[0+2*stride]=(t1 + t2 + 1)>>1;
    src[2+0*stride]=
    src[1+2*stride]=(t2 + t3 + 1)>>1;
    src[3+0*stride]=
    src[2+2*stride]=(t3 + t4+ 1)>>1;
    src[3+2*stride]=(t4 + t5+ 1)>>1;
    src[0+1*stride]=(t0 + 2*t1 + t2 + 2)>>2;
    src[1+1*stride]=
    src[0+3*stride]=(t1 + 2*t2 + t3 + 2)>>2;
    src[2+1*stride]=
    src[1+3*stride]=(t2 + 2*t3 + t4 + 2)>>2;
    src[3+1*stride]=
    src[2+3*stride]=(t3 + 2*t4 + t5 + 2)>>2;
    src[3+3*stride]=(t4 + 2*t5 + t6 + 2)>>2;
}

static void FUNCC(pred4x4_horizontal_up)(uint8_t *_src, const uint8_t *topright,
                                         ptrdiff_t _stride)
{
    pixel *src = (pixel*)_src;
    int stride = _stride>>(sizeof(pixel)-1);
    LOAD_LEFT_EDGE

    src[0+0*stride]=(l0 + l1 + 1)>>1;
    src[1+0*stride]=(l0 + 2*l1 + l2 + 2)>>2;
    src[2+0*stride]=
    src[0+1*stride]=(l1 + l2 + 1)>>1;
    src[3+0*stride]=
    src[1+1*stride]=(l1 + 2*l2 + l3 + 2)>>2;
    src[2+1*stride]=
    src[0+2*stride]=(l2 + l3 + 1)>>1;
    src[3+1*stride]=
    src[1+2*stride]=(l2 + 2*l3 + l3 + 2)>>2;
    src[3+2*stride]=
    src[1+3*stride]=
    src[0+3*stride]=
    src[2+2*stride]=
    src[2+3*stride]=
    src[3+3*stride]=l3;
}

static void FUNCC(pred4x4_horizontal_down)(uint8_t *_src,
                                           const uint8_t *topright,
                                           ptrdiff_t _stride)
{
    pixel *src = (pixel*)_src;
    int stride = _stride>>(sizeof(pixel)-1);
    const int lt= src[-1-1*stride];
    LOAD_TOP_EDGE
    LOAD_LEFT_EDGE

    src[0+0*stride]=
    src[2+1*stride]=(lt + l0 + 1)>>1;
    src[1+0*stride]=
    src[3+1*stride]=(l0 + 2*lt + t0 + 2)>>2;
    src[2+0*stride]=(lt + 2*t0 + t1 + 2)>>2;
    src[3+0*stride]=(t0 + 2*t1 + t2 + 2)>>2;
    src[0+1*stride]=
    src[2+2*stride]=(l0 + l1 + 1)>>1;
    src[1+1*stride]=
    src[3+2*stride]=(lt + 2*l0 + l1 + 2)>>2;
    src[0+2*stride]=
    src[2+3*stride]=(l1 + l2+ 1)>>1;
    src[1+2*stride]=
    src[3+3*stride]=(l0 + 2*l1 + l2 + 2)>>2;
    src[0+3*stride]=(l2 + l3 + 1)>>1;
    src[1+3*stride]=(l1 + 2*l2 + l3 + 2)>>2;
}

static void FUNCC(pred16x16_vertical)(uint8_t *_src, ptrdiff_t _stride)
{
    int i;
    pixel *src = (pixel*)_src;
    int stride = _stride>>(sizeof(pixel)-1);
    const pixel4 a = AV_RN4PA(((pixel4*)(src-stride))+0);
    const pixel4 b = AV_RN4PA(((pixel4*)(src-stride))+1);
    const pixel4 c = AV_RN4PA(((pixel4*)(src-stride))+2);
    const pixel4 d = AV_RN4PA(((pixel4*)(src-stride))+3);

    for(i=0; i<16; i++){
        AV_WN4PA(((pixel4*)(src+i*stride))+0, a);
        AV_WN4PA(((pixel4*)(src+i*stride))+1, b);
        AV_WN4PA(((pixel4*)(src+i*stride))+2, c);
        AV_WN4PA(((pixel4*)(src+i*stride))+3, d);
    }
}

static void FUNCC(pred16x16_horizontal)(uint8_t *_src, ptrdiff_t stride)
{
    int i;
    pixel *src = (pixel*)_src;
    stride >>= sizeof(pixel)-1;

    for(i=0; i<16; i++){
        const pixel4 a = PIXEL_SPLAT_X4(src[-1+i*stride]);

        AV_WN4PA(((pixel4*)(src+i*stride))+0, a);
        AV_WN4PA(((pixel4*)(src+i*stride))+1, a);
        AV_WN4PA(((pixel4*)(src+i*stride))+2, a);
        AV_WN4PA(((pixel4*)(src+i*stride))+3, a);
    }
}

#define PREDICT_16x16_DC(v)\
    for(i=0; i<16; i++){\
        AV_WN4PA(src+ 0, v);\
        AV_WN4PA(src+ 4, v);\
        AV_WN4PA(src+ 8, v);\
        AV_WN4PA(src+12, v);\
        src += stride;\
    }

static void FUNCC(pred16x16_dc)(uint8_t *_src, ptrdiff_t stride)
{
    int i, dc=0;
    pixel *src = (pixel*)_src;
    pixel4 dcsplat;
    stride >>= sizeof(pixel)-1;

    for(i=0;i<16; i++){
        dc+= src[-1+i*stride];
    }

    for(i=0;i<16; i++){
        dc+= src[i-stride];
    }

    dcsplat = PIXEL_SPLAT_X4((dc+16)>>5);
    PREDICT_16x16_DC(dcsplat);
}

static void FUNCC(pred16x16_left_dc)(uint8_t *_src, ptrdiff_t stride)
{
    int i, dc=0;
    pixel *src = (pixel*)_src;
    pixel4 dcsplat;
    stride >>= sizeof(pixel)-1;

    for(i=0;i<16; i++){
        dc+= src[-1+i*stride];
    }

    dcsplat = PIXEL_SPLAT_X4((dc+8)>>4);
    PREDICT_16x16_DC(dcsplat);
}

static void FUNCC(pred16x16_top_dc)(uint8_t *_src, ptrdiff_t stride)
{
    int i, dc=0;
    pixel *src = (pixel*)_src;
    pixel4 dcsplat;
    stride >>= sizeof(pixel)-1;

    for(i=0;i<16; i++){
        dc+= src[i-stride];
    }

    dcsplat = PIXEL_SPLAT_X4((dc+8)>>4);
    PREDICT_16x16_DC(dcsplat);
}

#define PRED16x16_X(n, v) \
static void FUNCC(pred16x16_##n##_dc)(uint8_t *_src, ptrdiff_t stride)\
{\
    int i;\
    pixel *src = (pixel*)_src;\
    stride >>= sizeof(pixel)-1;\
    PREDICT_16x16_DC(PIXEL_SPLAT_X4(v));\
}

PRED16x16_X(127, (1<<(BIT_DEPTH-1))-1)
PRED16x16_X(128, (1<<(BIT_DEPTH-1))+0)
PRED16x16_X(129, (1<<(BIT_DEPTH-1))+1)

static inline void FUNCC(pred16x16_plane_compat)(uint8_t *_src,
                                                 ptrdiff_t _stride,
                                                 const int svq3,
                                                 const int rv40)
{
  int i, j, k;
  int a;
  INIT_CLIP
  pixel *src = (pixel*)_src;
  int stride = _stride>>(sizeof(pixel)-1);
  const pixel * const src0 = src +7-stride;
  const pixel *       src1 = src +8*stride-1;
  const pixel *       src2 = src1-2*stride;    // == src+6*stride-1;
  int H = src0[1] - src0[-1];
  int V = src1[0] - src2[ 0];
  for(k=2; k<=8; ++k) {
    src1 += stride; src2 -= stride;
    H += k*(src0[k] - src0[-k]);
    V += k*(src1[0] - src2[ 0]);
  }
  if(svq3){
    H = ( 5*(H/4) ) / 16;
    V = ( 5*(V/4) ) / 16;

    /* required for 100% accuracy */
    i = H; H = V; V = i;
  }else if(rv40){
    H = ( H + (H>>2) ) >> 4;
    V = ( V + (V>>2) ) >> 4;
  }else{
    H = ( 5*H+32 ) >> 6;
    V = ( 5*V+32 ) >> 6;
  }

  a = 16*(src1[0] + src2[16] + 1) - 7*(V+H);
  for(j=16; j>0; --j) {
    int b = a;
    a += V;
    for(i=-16; i<0; i+=4) {
      src[16+i] = CLIP((b    ) >> 5);
      src[17+i] = CLIP((b+  H) >> 5);
      src[18+i] = CLIP((b+2*H) >> 5);
      src[19+i] = CLIP((b+3*H) >> 5);
      b += 4*H;
    }
    src += stride;
  }
}

static void FUNCC(pred16x16_plane)(uint8_t *src, ptrdiff_t stride)
{
    FUNCC(pred16x16_plane_compat)(src, stride, 0, 0);
}

static void FUNCC(pred8x8_vertical)(uint8_t *_src, ptrdiff_t _stride)
{
    int i;
    pixel *src = (pixel*)_src;
    int stride = _stride>>(sizeof(pixel)-1);
    const pixel4 a= AV_RN4PA(((pixel4*)(src-stride))+0);
    const pixel4 b= AV_RN4PA(((pixel4*)(src-stride))+1);

    for(i=0; i<8; i++){
        AV_WN4PA(((pixel4*)(src+i*stride))+0, a);
        AV_WN4PA(((pixel4*)(src+i*stride))+1, b);
    }
}

static void FUNCC(pred8x16_vertical)(uint8_t *_src, ptrdiff_t _stride)
{
    int i;
    pixel *src = (pixel*)_src;
    int stride = _stride>>(sizeof(pixel)-1);
    const pixel4 a= AV_RN4PA(((pixel4*)(src-stride))+0);
    const pixel4 b= AV_RN4PA(((pixel4*)(src-stride))+1);

    for(i=0; i<16; i++){
        AV_WN4PA(((pixel4*)(src+i*stride))+0, a);
        AV_WN4PA(((pixel4*)(src+i*stride))+1, b);
    }
}

static void FUNCC(pred8x8_horizontal)(uint8_t *_src, ptrdiff_t stride)
{
    int i;
    pixel *src = (pixel*)_src;
    stride >>= sizeof(pixel)-1;

    for(i=0; i<8; i++){
        const pixel4 a = PIXEL_SPLAT_X4(src[-1+i*stride]);
        AV_WN4PA(((pixel4*)(src+i*stride))+0, a);
        AV_WN4PA(((pixel4*)(src+i*stride))+1, a);
    }
}

static void FUNCC(pred8x16_horizontal)(uint8_t *_src, ptrdiff_t stride)
{
    int i;
    pixel *src = (pixel*)_src;
    stride >>= sizeof(pixel)-1;
    for(i=0; i<16; i++){
        const pixel4 a = PIXEL_SPLAT_X4(src[-1+i*stride]);
        AV_WN4PA(((pixel4*)(src+i*stride))+0, a);
        AV_WN4PA(((pixel4*)(src+i*stride))+1, a);
    }
}

#define PRED8x8_X(n, v)\
static void FUNCC(pred8x8_##n##_dc)(uint8_t *_src, ptrdiff_t stride)\
{\
    int i;\
    const pixel4 a = PIXEL_SPLAT_X4(v);\
    pixel *src = (pixel*)_src;\
    stride >>= sizeof(pixel)-1;\
    for(i=0; i<8; i++){\
        AV_WN4PA(((pixel4*)(src+i*stride))+0, a);\
        AV_WN4PA(((pixel4*)(src+i*stride))+1, a);\
    }\
}

PRED8x8_X(127, (1<<(BIT_DEPTH-1))-1)
PRED8x8_X(128, (1<<(BIT_DEPTH-1))+0)
PRED8x8_X(129, (1<<(BIT_DEPTH-1))+1)

static void FUNCC(pred8x16_128_dc)(uint8_t *_src, ptrdiff_t stride)
{
    FUNCC(pred8x8_128_dc)(_src, stride);
    FUNCC(pred8x8_128_dc)(_src+8*stride, stride);
}

static void FUNCC(pred8x8_left_dc)(uint8_t *_src, ptrdiff_t stride)
{
    int i;
    int dc0, dc2;
    pixel4 dc0splat, dc2splat;
    pixel *src = (pixel*)_src;
    stride >>= sizeof(pixel)-1;

    dc0=dc2=0;
    for(i=0;i<4; i++){
        dc0+= src[-1+i*stride];
        dc2+= src[-1+(i+4)*stride];
    }
    dc0splat = PIXEL_SPLAT_X4((dc0 + 2)>>2);
    dc2splat = PIXEL_SPLAT_X4((dc2 + 2)>>2);

    for(i=0; i<4; i++){
        AV_WN4PA(((pixel4*)(src+i*stride))+0, dc0splat);
        AV_WN4PA(((pixel4*)(src+i*stride))+1, dc0splat);
    }
    for(i=4; i<8; i++){
        AV_WN4PA(((pixel4*)(src+i*stride))+0, dc2splat);
        AV_WN4PA(((pixel4*)(src+i*stride))+1, dc2splat);
    }
}

static void FUNCC(pred8x16_left_dc)(uint8_t *_src, ptrdiff_t stride)
{
    FUNCC(pred8x8_left_dc)(_src, stride);
    FUNCC(pred8x8_left_dc)(_src+8*stride, stride);
}

static void FUNCC(pred8x8_top_dc)(uint8_t *_src, ptrdiff_t stride)
{
    int i;
    int dc0, dc1;
    pixel4 dc0splat, dc1splat;
    pixel *src = (pixel*)_src;
    stride >>= sizeof(pixel)-1;

    dc0=dc1=0;
    for(i=0;i<4; i++){
        dc0+= src[i-stride];
        dc1+= src[4+i-stride];
    }
    dc0splat = PIXEL_SPLAT_X4((dc0 + 2)>>2);
    dc1splat = PIXEL_SPLAT_X4((dc1 + 2)>>2);

    for(i=0; i<4; i++){
        AV_WN4PA(((pixel4*)(src+i*stride))+0, dc0splat);
        AV_WN4PA(((pixel4*)(src+i*stride))+1, dc1splat);
    }
    for(i=4; i<8; i++){
        AV_WN4PA(((pixel4*)(src+i*stride))+0, dc0splat);
        AV_WN4PA(((pixel4*)(src+i*stride))+1, dc1splat);
    }
}

static void FUNCC(pred8x16_top_dc)(uint8_t *_src, ptrdiff_t stride)
{
    int i;
    int dc0, dc1;
    pixel4 dc0splat, dc1splat;
    pixel *src = (pixel*)_src;
    stride >>= sizeof(pixel)-1;

    dc0=dc1=0;
    for(i=0;i<4; i++){
        dc0+= src[i-stride];
        dc1+= src[4+i-stride];
    }
    dc0splat = PIXEL_SPLAT_X4((dc0 + 2)>>2);
    dc1splat = PIXEL_SPLAT_X4((dc1 + 2)>>2);

    for(i=0; i<16; i++){
        AV_WN4PA(((pixel4*)(src+i*stride))+0, dc0splat);
        AV_WN4PA(((pixel4*)(src+i*stride))+1, dc1splat);
    }
}

static void FUNCC(pred8x8_dc)(uint8_t *_src, ptrdiff_t stride)
{
    int i;
    int dc0, dc1, dc2;
    pixel4 dc0splat, dc1splat, dc2splat, dc3splat;
    pixel *src = (pixel*)_src;
    stride >>= sizeof(pixel)-1;

    dc0=dc1=dc2=0;
    for(i=0;i<4; i++){
        dc0+= src[-1+i*stride] + src[i-stride];
        dc1+= src[4+i-stride];
        dc2+= src[-1+(i+4)*stride];
    }
    dc0splat = PIXEL_SPLAT_X4((dc0 + 4)>>3);
    dc1splat = PIXEL_SPLAT_X4((dc1 + 2)>>2);
    dc2splat = PIXEL_SPLAT_X4((dc2 + 2)>>2);
    dc3splat = PIXEL_SPLAT_X4((dc1 + dc2 + 4)>>3);

    for(i=0; i<4; i++){
        AV_WN4PA(((pixel4*)(src+i*stride))+0, dc0splat);
        AV_WN4PA(((pixel4*)(src+i*stride))+1, dc1splat);
    }
    for(i=4; i<8; i++){
        AV_WN4PA(((pixel4*)(src+i*stride))+0, dc2splat);
        AV_WN4PA(((pixel4*)(src+i*stride))+1, dc3splat);
    }
}

static void FUNCC(pred8x16_dc)(uint8_t *_src, ptrdiff_t stride)
{
    int i;
    int dc0, dc1, dc2, dc3, dc4;
    pixel4 dc0splat, dc1splat, dc2splat, dc3splat, dc4splat, dc5splat, dc6splat, dc7splat;
    pixel *src = (pixel*)_src;
    stride >>= sizeof(pixel)-1;

    dc0=dc1=dc2=dc3=dc4=0;
    for(i=0;i<4; i++){
        dc0+= src[-1+i*stride] + src[i-stride];
        dc1+= src[4+i-stride];
        dc2+= src[-1+(i+4)*stride];
        dc3+= src[-1+(i+8)*stride];
        dc4+= src[-1+(i+12)*stride];
    }
    dc0splat = PIXEL_SPLAT_X4((dc0 + 4)>>3);
    dc1splat = PIXEL_SPLAT_X4((dc1 + 2)>>2);
    dc2splat = PIXEL_SPLAT_X4((dc2 + 2)>>2);
    dc3splat = PIXEL_SPLAT_X4((dc1 + dc2 + 4)>>3);
    dc4splat = PIXEL_SPLAT_X4((dc3 + 2)>>2);
    dc5splat = PIXEL_SPLAT_X4((dc1 + dc3 + 4)>>3);
    dc6splat = PIXEL_SPLAT_X4((dc4 + 2)>>2);
    dc7splat = PIXEL_SPLAT_X4((dc1 + dc4 + 4)>>3);

    for(i=0; i<4; i++){
        AV_WN4PA(((pixel4*)(src+i*stride))+0, dc0splat);
        AV_WN4PA(((pixel4*)(src+i*stride))+1, dc1splat);
    }
    for(i=4; i<8; i++){
        AV_WN4PA(((pixel4*)(src+i*stride))+0, dc2splat);
        AV_WN4PA(((pixel4*)(src+i*stride))+1, dc3splat);
    }
    for(i=8; i<12; i++){
        AV_WN4PA(((pixel4*)(src+i*stride))+0, dc4splat);
        AV_WN4PA(((pixel4*)(src+i*stride))+1, dc5splat);
    }
    for(i=12; i<16; i++){
        AV_WN4PA(((pixel4*)(src+i*stride))+0, dc6splat);
        AV_WN4PA(((pixel4*)(src+i*stride))+1, dc7splat);
    }
}

//the following 4 function should not be optimized!
static void FUNC(pred8x8_mad_cow_dc_l0t)(uint8_t *src, ptrdiff_t stride)
{
    FUNCC(pred8x8_top_dc)(src, stride);
    FUNCC(pred4x4_dc)(src, NULL, stride);
}

static void FUNC(pred8x16_mad_cow_dc_l0t)(uint8_t *src, ptrdiff_t stride)
{
    FUNCC(pred8x16_top_dc)(src, stride);
    FUNCC(pred4x4_dc)(src, NULL, stride);
}

static void FUNC(pred8x8_mad_cow_dc_0lt)(uint8_t *src, ptrdiff_t stride)
{
    FUNCC(pred8x8_dc)(src, stride);
    FUNCC(pred4x4_top_dc)(src, NULL, stride);
}

static void FUNC(pred8x16_mad_cow_dc_0lt)(uint8_t *src, ptrdiff_t stride)
{
    FUNCC(pred8x16_dc)(src, stride);
    FUNCC(pred4x4_top_dc)(src, NULL, stride);
}

static void FUNC(pred8x8_mad_cow_dc_l00)(uint8_t *src, ptrdiff_t stride)
{
    FUNCC(pred8x8_left_dc)(src, stride);
    FUNCC(pred4x4_128_dc)(src + 4*stride                  , NULL, stride);
    FUNCC(pred4x4_128_dc)(src + 4*stride + 4*sizeof(pixel), NULL, stride);
}

static void FUNC(pred8x16_mad_cow_dc_l00)(uint8_t *src, ptrdiff_t stride)
{
    FUNCC(pred8x16_left_dc)(src, stride);
    FUNCC(pred4x4_128_dc)(src + 4*stride                  , NULL, stride);
    FUNCC(pred4x4_128_dc)(src + 4*stride + 4*sizeof(pixel), NULL, stride);
}

static void FUNC(pred8x8_mad_cow_dc_0l0)(uint8_t *src, ptrdiff_t stride)
{
    FUNCC(pred8x8_left_dc)(src, stride);
    FUNCC(pred4x4_128_dc)(src                  , NULL, stride);
    FUNCC(pred4x4_128_dc)(src + 4*sizeof(pixel), NULL, stride);
}

static void FUNC(pred8x16_mad_cow_dc_0l0)(uint8_t *src, ptrdiff_t stride)
{
    FUNCC(pred8x16_left_dc)(src, stride);
    FUNCC(pred4x4_128_dc)(src                  , NULL, stride);
    FUNCC(pred4x4_128_dc)(src + 4*sizeof(pixel), NULL, stride);
}

static void FUNCC(pred8x8_plane)(uint8_t *_src, ptrdiff_t _stride)
{
  int j, k;
  int a;
  INIT_CLIP
  pixel *src = (pixel*)_src;
  int stride = _stride>>(sizeof(pixel)-1);
  const pixel * const src0 = src +3-stride;
  const pixel *       src1 = src +4*stride-1;
  const pixel *       src2 = src1-2*stride;    // == src+2*stride-1;
  int H = src0[1] - src0[-1];
  int V = src1[0] - src2[ 0];
  for(k=2; k<=4; ++k) {
    src1 += stride; src2 -= stride;
    H += k*(src0[k] - src0[-k]);
    V += k*(src1[0] - src2[ 0]);
  }
  H = ( 17*H+16 ) >> 5;
  V = ( 17*V+16 ) >> 5;

  a = 16*(src1[0] + src2[8]+1) - 3*(V+H);
  for(j=8; j>0; --j) {
    int b = a;
    a += V;
    src[0] = CLIP((b    ) >> 5);
    src[1] = CLIP((b+  H) >> 5);
    src[2] = CLIP((b+2*H) >> 5);
    src[3] = CLIP((b+3*H) >> 5);
    src[4] = CLIP((b+4*H) >> 5);
    src[5] = CLIP((b+5*H) >> 5);
    src[6] = CLIP((b+6*H) >> 5);
    src[7] = CLIP((b+7*H) >> 5);
    src += stride;
  }
}

static void FUNCC(pred8x16_plane)(uint8_t *_src, ptrdiff_t _stride)
{
  int j, k;
  int a;
  INIT_CLIP
  pixel *src = (pixel*)_src;
  int stride = _stride>>(sizeof(pixel)-1);
  const pixel * const src0 = src +3-stride;
  const pixel *       src1 = src +8*stride-1;
  const pixel *       src2 = src1-2*stride;    // == src+6*stride-1;
  int H = src0[1] - src0[-1];
  int V = src1[0] - src2[ 0];

  for (k = 2; k <= 4; ++k) {
      src1 += stride; src2 -= stride;
      H += k*(src0[k] - src0[-k]);
      V += k*(src1[0] - src2[ 0]);
  }
  for (; k <= 8; ++k) {
      src1 += stride; src2 -= stride;
      V += k*(src1[0] - src2[0]);
  }

  H = (17*H+16) >> 5;
  V = (5*V+32) >> 6;

  a = 16*(src1[0] + src2[8] + 1) - 7*V - 3*H;
  for(j=16; j>0; --j) {
    int b = a;
    a += V;
    src[0] = CLIP((b    ) >> 5);
    src[1] = CLIP((b+  H) >> 5);
    src[2] = CLIP((b+2*H) >> 5);
    src[3] = CLIP((b+3*H) >> 5);
    src[4] = CLIP((b+4*H) >> 5);
    src[5] = CLIP((b+5*H) >> 5);
    src[6] = CLIP((b+6*H) >> 5);
    src[7] = CLIP((b+7*H) >> 5);
    src += stride;
  }
}

#define SRC(x,y) src[(x)+(y)*stride]
#define PL(y) \
    const int l##y = (SRC(-1,y-1) + 2*SRC(-1,y) + SRC(-1,y+1) + 2) >> 2;
#define PREDICT_8x8_LOAD_LEFT \
    const int l0 = ((has_topleft ? SRC(-1,-1) : SRC(-1,0)) \
                     + 2*SRC(-1,0) + SRC(-1,1) + 2) >> 2; \
    PL(1) PL(2) PL(3) PL(4) PL(5) PL(6) \
    const int l7 av_unused = (SRC(-1,6) + 3*SRC(-1,7) + 2) >> 2

#define PT(x) \
    const int t##x = (SRC(x-1,-1) + 2*SRC(x,-1) + SRC(x+1,-1) + 2) >> 2;
#define PREDICT_8x8_LOAD_TOP \
    const int t0 = ((has_topleft ? SRC(-1,-1) : SRC(0,-1)) \
                     + 2*SRC(0,-1) + SRC(1,-1) + 2) >> 2; \
    PT(1) PT(2) PT(3) PT(4) PT(5) PT(6) \
    const int t7 av_unused = ((has_topright ? SRC(8,-1) : SRC(7,-1)) \
                     + 2*SRC(7,-1) + SRC(6,-1) + 2) >> 2

#define PTR(x) \
    t##x = (SRC(x-1,-1) + 2*SRC(x,-1) + SRC(x+1,-1) + 2) >> 2;
#define PREDICT_8x8_LOAD_TOPRIGHT \
    int t8, t9, t10, t11, t12, t13, t14, t15; \
    if(has_topright) { \
        PTR(8) PTR(9) PTR(10) PTR(11) PTR(12) PTR(13) PTR(14) \
        t15 = (SRC(14,-1) + 3*SRC(15,-1) + 2) >> 2; \
    } else t8=t9=t10=t11=t12=t13=t14=t15= SRC(7,-1);

#define PREDICT_8x8_LOAD_TOPLEFT \
    const int lt = (SRC(-1,0) + 2*SRC(-1,-1) + SRC(0,-1) + 2) >> 2

#define PREDICT_8x8_DC(v) \
    int y; \
    for( y = 0; y < 8; y++ ) { \
        AV_WN4PA(((pixel4*)src)+0, v); \
        AV_WN4PA(((pixel4*)src)+1, v); \
        src += stride; \
    }

static void FUNCC(pred8x8l_128_dc)(uint8_t *_src, int has_topleft,
                                   int has_topright, ptrdiff_t _stride)
{
    pixel *src = (pixel*)_src;
    int stride = _stride>>(sizeof(pixel)-1);

    PREDICT_8x8_DC(PIXEL_SPLAT_X4(1<<(BIT_DEPTH-1)));
}
static void FUNCC(pred8x8l_left_dc)(uint8_t *_src, int has_topleft,
                                    int has_topright, ptrdiff_t _stride)
{
    pixel *src = (pixel*)_src;
    int stride = _stride>>(sizeof(pixel)-1);

    PREDICT_8x8_LOAD_LEFT;
    const pixel4 dc = PIXEL_SPLAT_X4((l0+l1+l2+l3+l4+l5+l6+l7+4) >> 3);
    PREDICT_8x8_DC(dc);
}
static void FUNCC(pred8x8l_top_dc)(uint8_t *_src, int has_topleft,
                                   int has_topright, ptrdiff_t _stride)
{
    pixel *src = (pixel*)_src;
    int stride = _stride>>(sizeof(pixel)-1);

    PREDICT_8x8_LOAD_TOP;
    const pixel4 dc = PIXEL_SPLAT_X4((t0+t1+t2+t3+t4+t5+t6+t7+4) >> 3);
    PREDICT_8x8_DC(dc);
}
static void FUNCC(pred8x8l_dc)(uint8_t *_src, int has_topleft,
                               int has_topright, ptrdiff_t _stride)
{
    pixel *src = (pixel*)_src;
    int stride = _stride>>(sizeof(pixel)-1);

    PREDICT_8x8_LOAD_LEFT;
    PREDICT_8x8_LOAD_TOP;
    const pixel4 dc = PIXEL_SPLAT_X4((l0+l1+l2+l3+l4+l5+l6+l7
                                     +t0+t1+t2+t3+t4+t5+t6+t7+8) >> 4);
    PREDICT_8x8_DC(dc);
}
static void FUNCC(pred8x8l_horizontal)(uint8_t *_src, int has_topleft,
                                       int has_topright, ptrdiff_t _stride)
{
    pixel *src = (pixel*)_src;
    int stride = _stride>>(sizeof(pixel)-1);
    pixel4 a;

    PREDICT_8x8_LOAD_LEFT;
#define ROW(y) a = PIXEL_SPLAT_X4(l##y); \
               AV_WN4PA(src+y*stride, a); \
               AV_WN4PA(src+y*stride+4, a);
    ROW(0); ROW(1); ROW(2); ROW(3); ROW(4); ROW(5); ROW(6); ROW(7);
#undef ROW
}
static void FUNCC(pred8x8l_vertical)(uint8_t *_src, int has_topleft,
                                     int has_topright, ptrdiff_t _stride)
{
    int y;
    pixel *src = (pixel*)_src;
    int stride = _stride>>(sizeof(pixel)-1);
    pixel4 a, b;

    PREDICT_8x8_LOAD_TOP;
    src[0] = t0;
    src[1] = t1;
    src[2] = t2;
    src[3] = t3;
    src[4] = t4;
    src[5] = t5;
    src[6] = t6;
    src[7] = t7;
    a = AV_RN4PA(((pixel4*)src)+0);
    b = AV_RN4PA(((pixel4*)src)+1);
    for( y = 1; y < 8; y++ ) {
        AV_WN4PA(((pixel4*)(src+y*stride))+0, a);
        AV_WN4PA(((pixel4*)(src+y*stride))+1, b);
    }
}
static void FUNCC(pred8x8l_down_left)(uint8_t *_src, int has_topleft,
                                      int has_topright, ptrdiff_t _stride)
{
    pixel *src = (pixel*)_src;
    int stride = _stride>>(sizeof(pixel)-1);
    PREDICT_8x8_LOAD_TOP;
    PREDICT_8x8_LOAD_TOPRIGHT;
    SRC(0,0)= (t0 + 2*t1 + t2 + 2) >> 2;
    SRC(0,1)=SRC(1,0)= (t1 + 2*t2 + t3 + 2) >> 2;
    SRC(0,2)=SRC(1,1)=SRC(2,0)= (t2 + 2*t3 + t4 + 2) >> 2;
    SRC(0,3)=SRC(1,2)=SRC(2,1)=SRC(3,0)= (t3 + 2*t4 + t5 + 2) >> 2;
    SRC(0,4)=SRC(1,3)=SRC(2,2)=SRC(3,1)=SRC(4,0)= (t4 + 2*t5 + t6 + 2) >> 2;
    SRC(0,5)=SRC(1,4)=SRC(2,3)=SRC(3,2)=SRC(4,1)=SRC(5,0)= (t5 + 2*t6 + t7 + 2) >> 2;
    SRC(0,6)=SRC(1,5)=SRC(2,4)=SRC(3,3)=SRC(4,2)=SRC(5,1)=SRC(6,0)= (t6 + 2*t7 + t8 + 2) >> 2;
    SRC(0,7)=SRC(1,6)=SRC(2,5)=SRC(3,4)=SRC(4,3)=SRC(5,2)=SRC(6,1)=SRC(7,0)= (t7 + 2*t8 + t9 + 2) >> 2;
    SRC(1,7)=SRC(2,6)=SRC(3,5)=SRC(4,4)=SRC(5,3)=SRC(6,2)=SRC(7,1)= (t8 + 2*t9 + t10 + 2) >> 2;
    SRC(2,7)=SRC(3,6)=SRC(4,5)=SRC(5,4)=SRC(6,3)=SRC(7,2)= (t9 + 2*t10 + t11 + 2) >> 2;
    SRC(3,7)=SRC(4,6)=SRC(5,5)=SRC(6,4)=SRC(7,3)= (t10 + 2*t11 + t12 + 2) >> 2;
    SRC(4,7)=SRC(5,6)=SRC(6,5)=SRC(7,4)= (t11 + 2*t12 + t13 + 2) >> 2;
    SRC(5,7)=SRC(6,6)=SRC(7,5)= (t12 + 2*t13 + t14 + 2) >> 2;
    SRC(6,7)=SRC(7,6)= (t13 + 2*t14 + t15 + 2) >> 2;
    SRC(7,7)= (t14 + 3*t15 + 2) >> 2;
}
static void FUNCC(pred8x8l_down_right)(uint8_t *_src, int has_topleft,
                                       int has_topright, ptrdiff_t _stride)
{
    pixel *src = (pixel*)_src;
    int stride = _stride>>(sizeof(pixel)-1);
    PREDICT_8x8_LOAD_TOP;
    PREDICT_8x8_LOAD_LEFT;
    PREDICT_8x8_LOAD_TOPLEFT;
    SRC(0,7)= (l7 + 2*l6 + l5 + 2) >> 2;
    SRC(0,6)=SRC(1,7)= (l6 + 2*l5 + l4 + 2) >> 2;
    SRC(0,5)=SRC(1,6)=SRC(2,7)= (l5 + 2*l4 + l3 + 2) >> 2;
    SRC(0,4)=SRC(1,5)=SRC(2,6)=SRC(3,7)= (l4 + 2*l3 + l2 + 2) >> 2;
    SRC(0,3)=SRC(1,4)=SRC(2,5)=SRC(3,6)=SRC(4,7)= (l3 + 2*l2 + l1 + 2) >> 2;
    SRC(0,2)=SRC(1,3)=SRC(2,4)=SRC(3,5)=SRC(4,6)=SRC(5,7)= (l2 + 2*l1 + l0 + 2) >> 2;
    SRC(0,1)=SRC(1,2)=SRC(2,3)=SRC(3,4)=SRC(4,5)=SRC(5,6)=SRC(6,7)= (l1 + 2*l0 + lt + 2) >> 2;
    SRC(0,0)=SRC(1,1)=SRC(2,2)=SRC(3,3)=SRC(4,4)=SRC(5,5)=SRC(6,6)=SRC(7,7)= (l0 + 2*lt + t0 + 2) >> 2;
    SRC(1,0)=SRC(2,1)=SRC(3,2)=SRC(4,3)=SRC(5,4)=SRC(6,5)=SRC(7,6)= (lt + 2*t0 + t1 + 2) >> 2;
    SRC(2,0)=SRC(3,1)=SRC(4,2)=SRC(5,3)=SRC(6,4)=SRC(7,5)= (t0 + 2*t1 + t2 + 2) >> 2;
    SRC(3,0)=SRC(4,1)=SRC(5,2)=SRC(6,3)=SRC(7,4)= (t1 + 2*t2 + t3 + 2) >> 2;
    SRC(4,0)=SRC(5,1)=SRC(6,2)=SRC(7,3)= (t2 + 2*t3 + t4 + 2) >> 2;
    SRC(5,0)=SRC(6,1)=SRC(7,2)= (t3 + 2*t4 + t5 + 2) >> 2;
    SRC(6,0)=SRC(7,1)= (t4 + 2*t5 + t6 + 2) >> 2;
    SRC(7,0)= (t5 + 2*t6 + t7 + 2) >> 2;
}
static void FUNCC(pred8x8l_vertical_right)(uint8_t *_src, int has_topleft,
                                           int has_topright, ptrdiff_t _stride)
{
    pixel *src = (pixel*)_src;
    int stride = _stride>>(sizeof(pixel)-1);
    PREDICT_8x8_LOAD_TOP;
    PREDICT_8x8_LOAD_LEFT;
    PREDICT_8x8_LOAD_TOPLEFT;
    SRC(0,6)= (l5 + 2*l4 + l3 + 2) >> 2;
    SRC(0,7)= (l6 + 2*l5 + l4 + 2) >> 2;
    SRC(0,4)=SRC(1,6)= (l3 + 2*l2 + l1 + 2) >> 2;
    SRC(0,5)=SRC(1,7)= (l4 + 2*l3 + l2 + 2) >> 2;
    SRC(0,2)=SRC(1,4)=SRC(2,6)= (l1 + 2*l0 + lt + 2) >> 2;
    SRC(0,3)=SRC(1,5)=SRC(2,7)= (l2 + 2*l1 + l0 + 2) >> 2;
    SRC(0,1)=SRC(1,3)=SRC(2,5)=SRC(3,7)= (l0 + 2*lt + t0 + 2) >> 2;
    SRC(0,0)=SRC(1,2)=SRC(2,4)=SRC(3,6)= (lt + t0 + 1) >> 1;
    SRC(1,1)=SRC(2,3)=SRC(3,5)=SRC(4,7)= (lt + 2*t0 + t1 + 2) >> 2;
    SRC(1,0)=SRC(2,2)=SRC(3,4)=SRC(4,6)= (t0 + t1 + 1) >> 1;
    SRC(2,1)=SRC(3,3)=SRC(4,5)=SRC(5,7)= (t0 + 2*t1 + t2 + 2) >> 2;
    SRC(2,0)=SRC(3,2)=SRC(4,4)=SRC(5,6)= (t1 + t2 + 1) >> 1;
    SRC(3,1)=SRC(4,3)=SRC(5,5)=SRC(6,7)= (t1 + 2*t2 + t3 + 2) >> 2;
    SRC(3,0)=SRC(4,2)=SRC(5,4)=SRC(6,6)= (t2 + t3 + 1) >> 1;
    SRC(4,1)=SRC(5,3)=SRC(6,5)=SRC(7,7)= (t2 + 2*t3 + t4 + 2) >> 2;
    SRC(4,0)=SRC(5,2)=SRC(6,4)=SRC(7,6)= (t3 + t4 + 1) >> 1;
    SRC(5,1)=SRC(6,3)=SRC(7,5)= (t3 + 2*t4 + t5 + 2) >> 2;
    SRC(5,0)=SRC(6,2)=SRC(7,4)= (t4 + t5 + 1) >> 1;
    SRC(6,1)=SRC(7,3)= (t4 + 2*t5 + t6 + 2) >> 2;
    SRC(6,0)=SRC(7,2)= (t5 + t6 + 1) >> 1;
    SRC(7,1)= (t5 + 2*t6 + t7 + 2) >> 2;
    SRC(7,0)= (t6 + t7 + 1) >> 1;
}
static void FUNCC(pred8x8l_horizontal_down)(uint8_t *_src, int has_topleft,
                                            int has_topright, ptrdiff_t _stride)
{
    pixel *src = (pixel*)_src;
    int stride = _stride>>(sizeof(pixel)-1);
    PREDICT_8x8_LOAD_TOP;
    PREDICT_8x8_LOAD_LEFT;
    PREDICT_8x8_LOAD_TOPLEFT;
    SRC(0,7)= (l6 + l7 + 1) >> 1;
    SRC(1,7)= (l5 + 2*l6 + l7 + 2) >> 2;
    SRC(0,6)=SRC(2,7)= (l5 + l6 + 1) >> 1;
    SRC(1,6)=SRC(3,7)= (l4 + 2*l5 + l6 + 2) >> 2;
    SRC(0,5)=SRC(2,6)=SRC(4,7)= (l4 + l5 + 1) >> 1;
    SRC(1,5)=SRC(3,6)=SRC(5,7)= (l3 + 2*l4 + l5 + 2) >> 2;
    SRC(0,4)=SRC(2,5)=SRC(4,6)=SRC(6,7)= (l3 + l4 + 1) >> 1;
    SRC(1,4)=SRC(3,5)=SRC(5,6)=SRC(7,7)= (l2 + 2*l3 + l4 + 2) >> 2;
    SRC(0,3)=SRC(2,4)=SRC(4,5)=SRC(6,6)= (l2 + l3 + 1) >> 1;
    SRC(1,3)=SRC(3,4)=SRC(5,5)=SRC(7,6)= (l1 + 2*l2 + l3 + 2) >> 2;
    SRC(0,2)=SRC(2,3)=SRC(4,4)=SRC(6,5)= (l1 + l2 + 1) >> 1;
    SRC(1,2)=SRC(3,3)=SRC(5,4)=SRC(7,5)= (l0 + 2*l1 + l2 + 2) >> 2;
    SRC(0,1)=SRC(2,2)=SRC(4,3)=SRC(6,4)= (l0 + l1 + 1) >> 1;
    SRC(1,1)=SRC(3,2)=SRC(5,3)=SRC(7,4)= (lt + 2*l0 + l1 + 2) >> 2;
    SRC(0,0)=SRC(2,1)=SRC(4,2)=SRC(6,3)= (lt + l0 + 1) >> 1;
    SRC(1,0)=SRC(3,1)=SRC(5,2)=SRC(7,3)= (l0 + 2*lt + t0 + 2) >> 2;
    SRC(2,0)=SRC(4,1)=SRC(6,2)= (t1 + 2*t0 + lt + 2) >> 2;
    SRC(3,0)=SRC(5,1)=SRC(7,2)= (t2 + 2*t1 + t0 + 2) >> 2;
    SRC(4,0)=SRC(6,1)= (t3 + 2*t2 + t1 + 2) >> 2;
    SRC(5,0)=SRC(7,1)= (t4 + 2*t3 + t2 + 2) >> 2;
    SRC(6,0)= (t5 + 2*t4 + t3 + 2) >> 2;
    SRC(7,0)= (t6 + 2*t5 + t4 + 2) >> 2;
}
static void FUNCC(pred8x8l_vertical_left)(uint8_t *_src, int has_topleft,
                                          int has_topright, ptrdiff_t _stride)
{
    pixel *src = (pixel*)_src;
    int stride = _stride>>(sizeof(pixel)-1);
    PREDICT_8x8_LOAD_TOP;
    PREDICT_8x8_LOAD_TOPRIGHT;
    SRC(0,0)= (t0 + t1 + 1) >> 1;
    SRC(0,1)= (t0 + 2*t1 + t2 + 2) >> 2;
    SRC(0,2)=SRC(1,0)= (t1 + t2 + 1) >> 1;
    SRC(0,3)=SRC(1,1)= (t1 + 2*t2 + t3 + 2) >> 2;
    SRC(0,4)=SRC(1,2)=SRC(2,0)= (t2 + t3 + 1) >> 1;
    SRC(0,5)=SRC(1,3)=SRC(2,1)= (t2 + 2*t3 + t4 + 2) >> 2;
    SRC(0,6)=SRC(1,4)=SRC(2,2)=SRC(3,0)= (t3 + t4 + 1) >> 1;
    SRC(0,7)=SRC(1,5)=SRC(2,3)=SRC(3,1)= (t3 + 2*t4 + t5 + 2) >> 2;
    SRC(1,6)=SRC(2,4)=SRC(3,2)=SRC(4,0)= (t4 + t5 + 1) >> 1;
    SRC(1,7)=SRC(2,5)=SRC(3,3)=SRC(4,1)= (t4 + 2*t5 + t6 + 2) >> 2;
    SRC(2,6)=SRC(3,4)=SRC(4,2)=SRC(5,0)= (t5 + t6 + 1) >> 1;
    SRC(2,7)=SRC(3,5)=SRC(4,3)=SRC(5,1)= (t5 + 2*t6 + t7 + 2) >> 2;
    SRC(3,6)=SRC(4,4)=SRC(5,2)=SRC(6,0)= (t6 + t7 + 1) >> 1;
    SRC(3,7)=SRC(4,5)=SRC(5,3)=SRC(6,1)= (t6 + 2*t7 + t8 + 2) >> 2;
    SRC(4,6)=SRC(5,4)=SRC(6,2)=SRC(7,0)= (t7 + t8 + 1) >> 1;
    SRC(4,7)=SRC(5,5)=SRC(6,3)=SRC(7,1)= (t7 + 2*t8 + t9 + 2) >> 2;
    SRC(5,6)=SRC(6,4)=SRC(7,2)= (t8 + t9 + 1) >> 1;
    SRC(5,7)=SRC(6,5)=SRC(7,3)= (t8 + 2*t9 + t10 + 2) >> 2;
    SRC(6,6)=SRC(7,4)= (t9 + t10 + 1) >> 1;
    SRC(6,7)=SRC(7,5)= (t9 + 2*t10 + t11 + 2) >> 2;
    SRC(7,6)= (t10 + t11 + 1) >> 1;
    SRC(7,7)= (t10 + 2*t11 + t12 + 2) >> 2;
}
static void FUNCC(pred8x8l_horizontal_up)(uint8_t *_src, int has_topleft,
                                          int has_topright, ptrdiff_t _stride)
{
    pixel *src = (pixel*)_src;
    int stride = _stride>>(sizeof(pixel)-1);
    PREDICT_8x8_LOAD_LEFT;
    SRC(0,0)= (l0 + l1 + 1) >> 1;
    SRC(1,0)= (l0 + 2*l1 + l2 + 2) >> 2;
    SRC(0,1)=SRC(2,0)= (l1 + l2 + 1) >> 1;
    SRC(1,1)=SRC(3,0)= (l1 + 2*l2 + l3 + 2) >> 2;
    SRC(0,2)=SRC(2,1)=SRC(4,0)= (l2 + l3 + 1) >> 1;
    SRC(1,2)=SRC(3,1)=SRC(5,0)= (l2 + 2*l3 + l4 + 2) >> 2;
    SRC(0,3)=SRC(2,2)=SRC(4,1)=SRC(6,0)= (l3 + l4 + 1) >> 1;
    SRC(1,3)=SRC(3,2)=SRC(5,1)=SRC(7,0)= (l3 + 2*l4 + l5 + 2) >> 2;
    SRC(0,4)=SRC(2,3)=SRC(4,2)=SRC(6,1)= (l4 + l5 + 1) >> 1;
    SRC(1,4)=SRC(3,3)=SRC(5,2)=SRC(7,1)= (l4 + 2*l5 + l6 + 2) >> 2;
    SRC(0,5)=SRC(2,4)=SRC(4,3)=SRC(6,2)= (l5 + l6 + 1) >> 1;
    SRC(1,5)=SRC(3,4)=SRC(5,3)=SRC(7,2)= (l5 + 2*l6 + l7 + 2) >> 2;
    SRC(0,6)=SRC(2,5)=SRC(4,4)=SRC(6,3)= (l6 + l7 + 1) >> 1;
    SRC(1,6)=SRC(3,5)=SRC(5,4)=SRC(7,3)= (l6 + 3*l7 + 2) >> 2;
    SRC(0,7)=SRC(1,7)=SRC(2,6)=SRC(2,7)=SRC(3,6)=
    SRC(3,7)=SRC(4,5)=SRC(4,6)=SRC(4,7)=SRC(5,5)=
    SRC(5,6)=SRC(5,7)=SRC(6,4)=SRC(6,5)=SRC(6,6)=
    SRC(6,7)=SRC(7,4)=SRC(7,5)=SRC(7,6)=SRC(7,7)= l7;
}
#undef PREDICT_8x8_LOAD_LEFT
#undef PREDICT_8x8_LOAD_TOP
#undef PREDICT_8x8_LOAD_TOPLEFT
#undef PREDICT_8x8_LOAD_TOPRIGHT
#undef PREDICT_8x8_DC
#undef PTR
#undef PT
#undef PL
#undef SRC

static void FUNCC(pred4x4_vertical_add)(uint8_t *_pix, int16_t *_block,
                                        ptrdiff_t stride)
{
    int i;
    pixel *pix = (pixel*)_pix;
    const dctcoef *block = (const dctcoef*)_block;
    stride >>= sizeof(pixel)-1;
    pix -= stride;
    for(i=0; i<4; i++){
        pixel v = pix[0];
        pix[1*stride]= v += block[0];
        pix[2*stride]= v += block[4];
        pix[3*stride]= v += block[8];
        pix[4*stride]= v +  block[12];
        pix++;
        block++;
    }

    memset(_block, 0, sizeof(dctcoef) * 16);
}

static void FUNCC(pred4x4_horizontal_add)(uint8_t *_pix, int16_t *_block,
                                          ptrdiff_t stride)
{
    int i;
    pixel *pix = (pixel*)_pix;
    const dctcoef *block = (const dctcoef*)_block;
    stride >>= sizeof(pixel)-1;
    for(i=0; i<4; i++){
        pixel v = pix[-1];
        pix[0]= v += block[0];
        pix[1]= v += block[1];
        pix[2]= v += block[2];
        pix[3]= v +  block[3];
        pix+= stride;
        block+= 4;
    }

    memset(_block, 0, sizeof(dctcoef) * 16);
}

static void FUNCC(pred8x8l_vertical_add)(uint8_t *_pix, int16_t *_block,
                                         ptrdiff_t stride)
{
    int i;
    pixel *pix = (pixel*)_pix;
    const dctcoef *block = (const dctcoef*)_block;
    stride >>= sizeof(pixel)-1;
    pix -= stride;
    for(i=0; i<8; i++){
        pixel v = pix[0];
        pix[1*stride]= v += block[0];
        pix[2*stride]= v += block[8];
        pix[3*stride]= v += block[16];
        pix[4*stride]= v += block[24];
        pix[5*stride]= v += block[32];
        pix[6*stride]= v += block[40];
        pix[7*stride]= v += block[48];
        pix[8*stride]= v +  block[56];
        pix++;
        block++;
    }

    memset(_block, 0, sizeof(dctcoef) * 64);
}

static void FUNCC(pred8x8l_horizontal_add)(uint8_t *_pix, int16_t *_block,
                                           ptrdiff_t stride)
{
    int i;
    pixel *pix = (pixel*)_pix;
    const dctcoef *block = (const dctcoef*)_block;
    stride >>= sizeof(pixel)-1;
    for(i=0; i<8; i++){
        pixel v = pix[-1];
        pix[0]= v += block[0];
        pix[1]= v += block[1];
        pix[2]= v += block[2];
        pix[3]= v += block[3];
        pix[4]= v += block[4];
        pix[5]= v += block[5];
        pix[6]= v += block[6];
        pix[7]= v +  block[7];
        pix+= stride;
        block+= 8;
    }

    memset(_block, 0, sizeof(dctcoef) * 64);
}

static void FUNCC(pred16x16_vertical_add)(uint8_t *pix, const int *block_offset,
                                          int16_t *block,
                                          ptrdiff_t stride)
{
    int i;
    for(i=0; i<16; i++)
        FUNCC(pred4x4_vertical_add)(pix + block_offset[i], block + i*16*sizeof(pixel), stride);
}

static void FUNCC(pred16x16_horizontal_add)(uint8_t *pix,
                                            const int *block_offset,
                                            int16_t *block,
                                            ptrdiff_t stride)
{
    int i;
    for(i=0; i<16; i++)
        FUNCC(pred4x4_horizontal_add)(pix + block_offset[i], block + i*16*sizeof(pixel), stride);
}

static void FUNCC(pred8x8_vertical_add)(uint8_t *pix, const int *block_offset,
                                        int16_t *block, ptrdiff_t stride)
{
    int i;
    for(i=0; i<4; i++)
        FUNCC(pred4x4_vertical_add)(pix + block_offset[i], block + i*16*sizeof(pixel), stride);
}

static void FUNCC(pred8x16_vertical_add)(uint8_t *pix, const int *block_offset,
                                         int16_t *block, ptrdiff_t stride)
{
    int i;
    for(i=0; i<4; i++)
        FUNCC(pred4x4_vertical_add)(pix + block_offset[i], block + i*16*sizeof(pixel), stride);
    for(i=4; i<8; i++)
        FUNCC(pred4x4_vertical_add)(pix + block_offset[i+4], block + i*16*sizeof(pixel), stride);
}

static void FUNCC(pred8x8_horizontal_add)(uint8_t *pix, const int *block_offset,
                                          int16_t *block,
                                          ptrdiff_t stride)
{
    int i;
    for(i=0; i<4; i++)
        FUNCC(pred4x4_horizontal_add)(pix + block_offset[i], block + i*16*sizeof(pixel), stride);
}

static void FUNCC(pred8x16_horizontal_add)(uint8_t *pix,
                                           const int *block_offset,
                                           int16_t *block, ptrdiff_t stride)
{
    int i;
    for(i=0; i<4; i++)
        FUNCC(pred4x4_horizontal_add)(pix + block_offset[i], block + i*16*sizeof(pixel), stride);
    for(i=4; i<8; i++)
        FUNCC(pred4x4_horizontal_add)(pix + block_offset[i+4], block + i*16*sizeof(pixel), stride);
}
