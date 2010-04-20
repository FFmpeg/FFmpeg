/*
 * H.26L/H.264/AVC/JVT/14496-10/... encoder/decoder
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
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

#include "avcodec.h"
#include "mpegvideo.h"
#include "h264pred.h"

static void pred4x4_vertical_c(uint8_t *src, uint8_t *topright, int stride){
    const uint32_t a= ((uint32_t*)(src-stride))[0];
    ((uint32_t*)(src+0*stride))[0]= a;
    ((uint32_t*)(src+1*stride))[0]= a;
    ((uint32_t*)(src+2*stride))[0]= a;
    ((uint32_t*)(src+3*stride))[0]= a;
}

static void pred4x4_horizontal_c(uint8_t *src, uint8_t *topright, int stride){
    ((uint32_t*)(src+0*stride))[0]= src[-1+0*stride]*0x01010101;
    ((uint32_t*)(src+1*stride))[0]= src[-1+1*stride]*0x01010101;
    ((uint32_t*)(src+2*stride))[0]= src[-1+2*stride]*0x01010101;
    ((uint32_t*)(src+3*stride))[0]= src[-1+3*stride]*0x01010101;
}

static void pred4x4_dc_c(uint8_t *src, uint8_t *topright, int stride){
    const int dc= (  src[-stride] + src[1-stride] + src[2-stride] + src[3-stride]
                   + src[-1+0*stride] + src[-1+1*stride] + src[-1+2*stride] + src[-1+3*stride] + 4) >>3;

    ((uint32_t*)(src+0*stride))[0]=
    ((uint32_t*)(src+1*stride))[0]=
    ((uint32_t*)(src+2*stride))[0]=
    ((uint32_t*)(src+3*stride))[0]= dc* 0x01010101;
}

static void pred4x4_left_dc_c(uint8_t *src, uint8_t *topright, int stride){
    const int dc= (  src[-1+0*stride] + src[-1+1*stride] + src[-1+2*stride] + src[-1+3*stride] + 2) >>2;

    ((uint32_t*)(src+0*stride))[0]=
    ((uint32_t*)(src+1*stride))[0]=
    ((uint32_t*)(src+2*stride))[0]=
    ((uint32_t*)(src+3*stride))[0]= dc* 0x01010101;
}

static void pred4x4_top_dc_c(uint8_t *src, uint8_t *topright, int stride){
    const int dc= (  src[-stride] + src[1-stride] + src[2-stride] + src[3-stride] + 2) >>2;

    ((uint32_t*)(src+0*stride))[0]=
    ((uint32_t*)(src+1*stride))[0]=
    ((uint32_t*)(src+2*stride))[0]=
    ((uint32_t*)(src+3*stride))[0]= dc* 0x01010101;
}

static void pred4x4_128_dc_c(uint8_t *src, uint8_t *topright, int stride){
    ((uint32_t*)(src+0*stride))[0]=
    ((uint32_t*)(src+1*stride))[0]=
    ((uint32_t*)(src+2*stride))[0]=
    ((uint32_t*)(src+3*stride))[0]= 128U*0x01010101U;
}


#define LOAD_TOP_RIGHT_EDGE\
    const int av_unused t4= topright[0];\
    const int av_unused t5= topright[1];\
    const int av_unused t6= topright[2];\
    const int av_unused t7= topright[3];\

#define LOAD_DOWN_LEFT_EDGE\
    const int av_unused l4= src[-1+4*stride];\
    const int av_unused l5= src[-1+5*stride];\
    const int av_unused l6= src[-1+6*stride];\
    const int av_unused l7= src[-1+7*stride];\

#define LOAD_LEFT_EDGE\
    const int av_unused l0= src[-1+0*stride];\
    const int av_unused l1= src[-1+1*stride];\
    const int av_unused l2= src[-1+2*stride];\
    const int av_unused l3= src[-1+3*stride];\

#define LOAD_TOP_EDGE\
    const int av_unused t0= src[ 0-1*stride];\
    const int av_unused t1= src[ 1-1*stride];\
    const int av_unused t2= src[ 2-1*stride];\
    const int av_unused t3= src[ 3-1*stride];\

static void pred4x4_down_right_c(uint8_t *src, uint8_t *topright, int stride){
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

static void pred4x4_down_left_c(uint8_t *src, uint8_t *topright, int stride){
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

static void pred4x4_down_left_svq3_c(uint8_t *src, uint8_t *topright, int stride){
    LOAD_TOP_EDGE
    LOAD_LEFT_EDGE
    const av_unused int unu0= t0;
    const av_unused int unu1= l0;

    src[0+0*stride]=(l1 + t1)>>1;
    src[1+0*stride]=
    src[0+1*stride]=(l2 + t2)>>1;
    src[2+0*stride]=
    src[1+1*stride]=
    src[0+2*stride]=
    src[3+0*stride]=
    src[2+1*stride]=
    src[1+2*stride]=
    src[0+3*stride]=
    src[3+1*stride]=
    src[2+2*stride]=
    src[1+3*stride]=
    src[3+2*stride]=
    src[2+3*stride]=
    src[3+3*stride]=(l3 + t3)>>1;
}

static void pred4x4_down_left_rv40_c(uint8_t *src, uint8_t *topright, int stride){
    LOAD_TOP_EDGE
    LOAD_TOP_RIGHT_EDGE
    LOAD_LEFT_EDGE
    LOAD_DOWN_LEFT_EDGE

    src[0+0*stride]=(t0 + t2 + 2*t1 + 2 + l0 + l2 + 2*l1 + 2)>>3;
    src[1+0*stride]=
    src[0+1*stride]=(t1 + t3 + 2*t2 + 2 + l1 + l3 + 2*l2 + 2)>>3;
    src[2+0*stride]=
    src[1+1*stride]=
    src[0+2*stride]=(t2 + t4 + 2*t3 + 2 + l2 + l4 + 2*l3 + 2)>>3;
    src[3+0*stride]=
    src[2+1*stride]=
    src[1+2*stride]=
    src[0+3*stride]=(t3 + t5 + 2*t4 + 2 + l3 + l5 + 2*l4 + 2)>>3;
    src[3+1*stride]=
    src[2+2*stride]=
    src[1+3*stride]=(t4 + t6 + 2*t5 + 2 + l4 + l6 + 2*l5 + 2)>>3;
    src[3+2*stride]=
    src[2+3*stride]=(t5 + t7 + 2*t6 + 2 + l5 + l7 + 2*l6 + 2)>>3;
    src[3+3*stride]=(t6 + t7 + 1 + l6 + l7 + 1)>>2;
}

static void pred4x4_down_left_rv40_nodown_c(uint8_t *src, uint8_t *topright, int stride){
    LOAD_TOP_EDGE
    LOAD_TOP_RIGHT_EDGE
    LOAD_LEFT_EDGE

    src[0+0*stride]=(t0 + t2 + 2*t1 + 2 + l0 + l2 + 2*l1 + 2)>>3;
    src[1+0*stride]=
    src[0+1*stride]=(t1 + t3 + 2*t2 + 2 + l1 + l3 + 2*l2 + 2)>>3;
    src[2+0*stride]=
    src[1+1*stride]=
    src[0+2*stride]=(t2 + t4 + 2*t3 + 2 + l2 + 3*l3 + 2)>>3;
    src[3+0*stride]=
    src[2+1*stride]=
    src[1+2*stride]=
    src[0+3*stride]=(t3 + t5 + 2*t4 + 2 + l3*4 + 2)>>3;
    src[3+1*stride]=
    src[2+2*stride]=
    src[1+3*stride]=(t4 + t6 + 2*t5 + 2 + l3*4 + 2)>>3;
    src[3+2*stride]=
    src[2+3*stride]=(t5 + t7 + 2*t6 + 2 + l3*4 + 2)>>3;
    src[3+3*stride]=(t6 + t7 + 1 + 2*l3 + 1)>>2;
}

static void pred4x4_vertical_right_c(uint8_t *src, uint8_t *topright, int stride){
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

static void pred4x4_vertical_left_c(uint8_t *src, uint8_t *topright, int stride){
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

static void pred4x4_vertical_left_rv40(uint8_t *src, uint8_t *topright, int stride,
                                      const int l0, const int l1, const int l2, const int l3, const int l4){
    LOAD_TOP_EDGE
    LOAD_TOP_RIGHT_EDGE

    src[0+0*stride]=(2*t0 + 2*t1 + l1 + 2*l2 + l3 + 4)>>3;
    src[1+0*stride]=
    src[0+2*stride]=(t1 + t2 + 1)>>1;
    src[2+0*stride]=
    src[1+2*stride]=(t2 + t3 + 1)>>1;
    src[3+0*stride]=
    src[2+2*stride]=(t3 + t4+ 1)>>1;
    src[3+2*stride]=(t4 + t5+ 1)>>1;
    src[0+1*stride]=(t0 + 2*t1 + t2 + l2 + 2*l3 + l4 + 4)>>3;
    src[1+1*stride]=
    src[0+3*stride]=(t1 + 2*t2 + t3 + 2)>>2;
    src[2+1*stride]=
    src[1+3*stride]=(t2 + 2*t3 + t4 + 2)>>2;
    src[3+1*stride]=
    src[2+3*stride]=(t3 + 2*t4 + t5 + 2)>>2;
    src[3+3*stride]=(t4 + 2*t5 + t6 + 2)>>2;
}

static void pred4x4_vertical_left_rv40_c(uint8_t *src, uint8_t *topright, int stride){
    LOAD_LEFT_EDGE
    LOAD_DOWN_LEFT_EDGE

    pred4x4_vertical_left_rv40(src, topright, stride, l0, l1, l2, l3, l4);
}

static void pred4x4_vertical_left_rv40_nodown_c(uint8_t *src, uint8_t *topright, int stride){
    LOAD_LEFT_EDGE

    pred4x4_vertical_left_rv40(src, topright, stride, l0, l1, l2, l3, l3);
}

static void pred4x4_horizontal_up_c(uint8_t *src, uint8_t *topright, int stride){
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

static void pred4x4_horizontal_up_rv40_c(uint8_t *src, uint8_t *topright, int stride){
    LOAD_LEFT_EDGE
    LOAD_DOWN_LEFT_EDGE
    LOAD_TOP_EDGE
    LOAD_TOP_RIGHT_EDGE

    src[0+0*stride]=(t1 + 2*t2 + t3 + 2*l0 + 2*l1 + 4)>>3;
    src[1+0*stride]=(t2 + 2*t3 + t4 + l0 + 2*l1 + l2 + 4)>>3;
    src[2+0*stride]=
    src[0+1*stride]=(t3 + 2*t4 + t5 + 2*l1 + 2*l2 + 4)>>3;
    src[3+0*stride]=
    src[1+1*stride]=(t4 + 2*t5 + t6 + l1 + 2*l2 + l3 + 4)>>3;
    src[2+1*stride]=
    src[0+2*stride]=(t5 + 2*t6 + t7 + 2*l2 + 2*l3 + 4)>>3;
    src[3+1*stride]=
    src[1+2*stride]=(t6 + 3*t7 + l2 + 3*l3 + 4)>>3;
    src[3+2*stride]=
    src[1+3*stride]=(l3 + 2*l4 + l5 + 2)>>2;
    src[0+3*stride]=
    src[2+2*stride]=(t6 + t7 + l3 + l4 + 2)>>2;
    src[2+3*stride]=(l4 + l5 + 1)>>1;
    src[3+3*stride]=(l4 + 2*l5 + l6 + 2)>>2;
}

static void pred4x4_horizontal_up_rv40_nodown_c(uint8_t *src, uint8_t *topright, int stride){
    LOAD_LEFT_EDGE
    LOAD_TOP_EDGE
    LOAD_TOP_RIGHT_EDGE

    src[0+0*stride]=(t1 + 2*t2 + t3 + 2*l0 + 2*l1 + 4)>>3;
    src[1+0*stride]=(t2 + 2*t3 + t4 + l0 + 2*l1 + l2 + 4)>>3;
    src[2+0*stride]=
    src[0+1*stride]=(t3 + 2*t4 + t5 + 2*l1 + 2*l2 + 4)>>3;
    src[3+0*stride]=
    src[1+1*stride]=(t4 + 2*t5 + t6 + l1 + 2*l2 + l3 + 4)>>3;
    src[2+1*stride]=
    src[0+2*stride]=(t5 + 2*t6 + t7 + 2*l2 + 2*l3 + 4)>>3;
    src[3+1*stride]=
    src[1+2*stride]=(t6 + 3*t7 + l2 + 3*l3 + 4)>>3;
    src[3+2*stride]=
    src[1+3*stride]=l3;
    src[0+3*stride]=
    src[2+2*stride]=(t6 + t7 + 2*l3 + 2)>>2;
    src[2+3*stride]=
    src[3+3*stride]=l3;
}

static void pred4x4_horizontal_down_c(uint8_t *src, uint8_t *topright, int stride){
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

static void pred16x16_vertical_c(uint8_t *src, int stride){
    int i;
    const uint32_t a= ((uint32_t*)(src-stride))[0];
    const uint32_t b= ((uint32_t*)(src-stride))[1];
    const uint32_t c= ((uint32_t*)(src-stride))[2];
    const uint32_t d= ((uint32_t*)(src-stride))[3];

    for(i=0; i<16; i++){
        ((uint32_t*)(src+i*stride))[0]= a;
        ((uint32_t*)(src+i*stride))[1]= b;
        ((uint32_t*)(src+i*stride))[2]= c;
        ((uint32_t*)(src+i*stride))[3]= d;
    }
}

static void pred16x16_horizontal_c(uint8_t *src, int stride){
    int i;

    for(i=0; i<16; i++){
        ((uint32_t*)(src+i*stride))[0]=
        ((uint32_t*)(src+i*stride))[1]=
        ((uint32_t*)(src+i*stride))[2]=
        ((uint32_t*)(src+i*stride))[3]= src[-1+i*stride]*0x01010101;
    }
}

static void pred16x16_dc_c(uint8_t *src, int stride){
    int i, dc=0;

    for(i=0;i<16; i++){
        dc+= src[-1+i*stride];
    }

    for(i=0;i<16; i++){
        dc+= src[i-stride];
    }

    dc= 0x01010101*((dc + 16)>>5);

    for(i=0; i<16; i++){
        ((uint32_t*)(src+i*stride))[0]=
        ((uint32_t*)(src+i*stride))[1]=
        ((uint32_t*)(src+i*stride))[2]=
        ((uint32_t*)(src+i*stride))[3]= dc;
    }
}

static void pred16x16_left_dc_c(uint8_t *src, int stride){
    int i, dc=0;

    for(i=0;i<16; i++){
        dc+= src[-1+i*stride];
    }

    dc= 0x01010101*((dc + 8)>>4);

    for(i=0; i<16; i++){
        ((uint32_t*)(src+i*stride))[0]=
        ((uint32_t*)(src+i*stride))[1]=
        ((uint32_t*)(src+i*stride))[2]=
        ((uint32_t*)(src+i*stride))[3]= dc;
    }
}

static void pred16x16_top_dc_c(uint8_t *src, int stride){
    int i, dc=0;

    for(i=0;i<16; i++){
        dc+= src[i-stride];
    }
    dc= 0x01010101*((dc + 8)>>4);

    for(i=0; i<16; i++){
        ((uint32_t*)(src+i*stride))[0]=
        ((uint32_t*)(src+i*stride))[1]=
        ((uint32_t*)(src+i*stride))[2]=
        ((uint32_t*)(src+i*stride))[3]= dc;
    }
}

static void pred16x16_128_dc_c(uint8_t *src, int stride){
    int i;

    for(i=0; i<16; i++){
        ((uint32_t*)(src+i*stride))[0]=
        ((uint32_t*)(src+i*stride))[1]=
        ((uint32_t*)(src+i*stride))[2]=
        ((uint32_t*)(src+i*stride))[3]= 0x01010101U*128U;
    }
}

static inline void pred16x16_plane_compat_c(uint8_t *src, int stride, const int svq3, const int rv40){
  int i, j, k;
  int a;
  uint8_t *cm = ff_cropTbl + MAX_NEG_CROP;
  const uint8_t * const src0 = src+7-stride;
  const uint8_t *src1 = src+8*stride-1;
  const uint8_t *src2 = src1-2*stride;      // == src+6*stride-1;
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
      src[16+i] = cm[ (b    ) >> 5 ];
      src[17+i] = cm[ (b+  H) >> 5 ];
      src[18+i] = cm[ (b+2*H) >> 5 ];
      src[19+i] = cm[ (b+3*H) >> 5 ];
      b += 4*H;
    }
    src += stride;
  }
}

static void pred16x16_plane_c(uint8_t *src, int stride){
    pred16x16_plane_compat_c(src, stride, 0, 0);
}

static void pred16x16_plane_svq3_c(uint8_t *src, int stride){
    pred16x16_plane_compat_c(src, stride, 1, 0);
}

static void pred16x16_plane_rv40_c(uint8_t *src, int stride){
    pred16x16_plane_compat_c(src, stride, 0, 1);
}

static void pred8x8_vertical_c(uint8_t *src, int stride){
    int i;
    const uint32_t a= ((uint32_t*)(src-stride))[0];
    const uint32_t b= ((uint32_t*)(src-stride))[1];

    for(i=0; i<8; i++){
        ((uint32_t*)(src+i*stride))[0]= a;
        ((uint32_t*)(src+i*stride))[1]= b;
    }
}

static void pred8x8_horizontal_c(uint8_t *src, int stride){
    int i;

    for(i=0; i<8; i++){
        ((uint32_t*)(src+i*stride))[0]=
        ((uint32_t*)(src+i*stride))[1]= src[-1+i*stride]*0x01010101;
    }
}

static void pred8x8_128_dc_c(uint8_t *src, int stride){
    int i;

    for(i=0; i<8; i++){
        ((uint32_t*)(src+i*stride))[0]=
        ((uint32_t*)(src+i*stride))[1]= 0x01010101U*128U;
    }
}

static void pred8x8_left_dc_c(uint8_t *src, int stride){
    int i;
    int dc0, dc2;

    dc0=dc2=0;
    for(i=0;i<4; i++){
        dc0+= src[-1+i*stride];
        dc2+= src[-1+(i+4)*stride];
    }
    dc0= 0x01010101*((dc0 + 2)>>2);
    dc2= 0x01010101*((dc2 + 2)>>2);

    for(i=0; i<4; i++){
        ((uint32_t*)(src+i*stride))[0]=
        ((uint32_t*)(src+i*stride))[1]= dc0;
    }
    for(i=4; i<8; i++){
        ((uint32_t*)(src+i*stride))[0]=
        ((uint32_t*)(src+i*stride))[1]= dc2;
    }
}

static void pred8x8_left_dc_rv40_c(uint8_t *src, int stride){
    int i;
    int dc0;

    dc0=0;
    for(i=0;i<8; i++)
        dc0+= src[-1+i*stride];
    dc0= 0x01010101*((dc0 + 4)>>3);

    for(i=0; i<8; i++){
        ((uint32_t*)(src+i*stride))[0]=
        ((uint32_t*)(src+i*stride))[1]= dc0;
    }
}

static void pred8x8_top_dc_c(uint8_t *src, int stride){
    int i;
    int dc0, dc1;

    dc0=dc1=0;
    for(i=0;i<4; i++){
        dc0+= src[i-stride];
        dc1+= src[4+i-stride];
    }
    dc0= 0x01010101*((dc0 + 2)>>2);
    dc1= 0x01010101*((dc1 + 2)>>2);

    for(i=0; i<4; i++){
        ((uint32_t*)(src+i*stride))[0]= dc0;
        ((uint32_t*)(src+i*stride))[1]= dc1;
    }
    for(i=4; i<8; i++){
        ((uint32_t*)(src+i*stride))[0]= dc0;
        ((uint32_t*)(src+i*stride))[1]= dc1;
    }
}

static void pred8x8_top_dc_rv40_c(uint8_t *src, int stride){
    int i;
    int dc0;

    dc0=0;
    for(i=0;i<8; i++)
        dc0+= src[i-stride];
    dc0= 0x01010101*((dc0 + 4)>>3);

    for(i=0; i<8; i++){
        ((uint32_t*)(src+i*stride))[0]=
        ((uint32_t*)(src+i*stride))[1]= dc0;
    }
}


static void pred8x8_dc_c(uint8_t *src, int stride){
    int i;
    int dc0, dc1, dc2, dc3;

    dc0=dc1=dc2=0;
    for(i=0;i<4; i++){
        dc0+= src[-1+i*stride] + src[i-stride];
        dc1+= src[4+i-stride];
        dc2+= src[-1+(i+4)*stride];
    }
    dc3= 0x01010101*((dc1 + dc2 + 4)>>3);
    dc0= 0x01010101*((dc0 + 4)>>3);
    dc1= 0x01010101*((dc1 + 2)>>2);
    dc2= 0x01010101*((dc2 + 2)>>2);

    for(i=0; i<4; i++){
        ((uint32_t*)(src+i*stride))[0]= dc0;
        ((uint32_t*)(src+i*stride))[1]= dc1;
    }
    for(i=4; i<8; i++){
        ((uint32_t*)(src+i*stride))[0]= dc2;
        ((uint32_t*)(src+i*stride))[1]= dc3;
    }
}

//the following 4 function should not be optimized!
static void pred8x8_mad_cow_dc_l0t(uint8_t *src, int stride){
    pred8x8_top_dc_c(src, stride);
    pred4x4_dc_c(src, NULL, stride);
}

static void pred8x8_mad_cow_dc_0lt(uint8_t *src, int stride){
    pred8x8_dc_c(src, stride);
    pred4x4_top_dc_c(src, NULL, stride);
}

static void pred8x8_mad_cow_dc_l00(uint8_t *src, int stride){
    pred8x8_left_dc_c(src, stride);
    pred4x4_128_dc_c(src + 4*stride    , NULL, stride);
    pred4x4_128_dc_c(src + 4*stride + 4, NULL, stride);
}

static void pred8x8_mad_cow_dc_0l0(uint8_t *src, int stride){
    pred8x8_left_dc_c(src, stride);
    pred4x4_128_dc_c(src    , NULL, stride);
    pred4x4_128_dc_c(src + 4, NULL, stride);
}

static void pred8x8_dc_rv40_c(uint8_t *src, int stride){
    int i;
    int dc0=0;

    for(i=0;i<4; i++){
        dc0+= src[-1+i*stride] + src[i-stride];
        dc0+= src[4+i-stride];
        dc0+= src[-1+(i+4)*stride];
    }
    dc0= 0x01010101*((dc0 + 8)>>4);

    for(i=0; i<4; i++){
        ((uint32_t*)(src+i*stride))[0]= dc0;
        ((uint32_t*)(src+i*stride))[1]= dc0;
    }
    for(i=4; i<8; i++){
        ((uint32_t*)(src+i*stride))[0]= dc0;
        ((uint32_t*)(src+i*stride))[1]= dc0;
    }
}

static void pred8x8_plane_c(uint8_t *src, int stride){
  int j, k;
  int a;
  uint8_t *cm = ff_cropTbl + MAX_NEG_CROP;
  const uint8_t * const src0 = src+3-stride;
  const uint8_t *src1 = src+4*stride-1;
  const uint8_t *src2 = src1-2*stride;      // == src+2*stride-1;
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
    src[0] = cm[ (b    ) >> 5 ];
    src[1] = cm[ (b+  H) >> 5 ];
    src[2] = cm[ (b+2*H) >> 5 ];
    src[3] = cm[ (b+3*H) >> 5 ];
    src[4] = cm[ (b+4*H) >> 5 ];
    src[5] = cm[ (b+5*H) >> 5 ];
    src[6] = cm[ (b+6*H) >> 5 ];
    src[7] = cm[ (b+7*H) >> 5 ];
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
        ((uint32_t*)src)[0] = \
        ((uint32_t*)src)[1] = v; \
        src += stride; \
    }

static void pred8x8l_128_dc_c(uint8_t *src, int has_topleft, int has_topright, int stride)
{
    PREDICT_8x8_DC(0x80808080);
}
static void pred8x8l_left_dc_c(uint8_t *src, int has_topleft, int has_topright, int stride)
{
    PREDICT_8x8_LOAD_LEFT;
    const uint32_t dc = ((l0+l1+l2+l3+l4+l5+l6+l7+4) >> 3) * 0x01010101;
    PREDICT_8x8_DC(dc);
}
static void pred8x8l_top_dc_c(uint8_t *src, int has_topleft, int has_topright, int stride)
{
    PREDICT_8x8_LOAD_TOP;
    const uint32_t dc = ((t0+t1+t2+t3+t4+t5+t6+t7+4) >> 3) * 0x01010101;
    PREDICT_8x8_DC(dc);
}
static void pred8x8l_dc_c(uint8_t *src, int has_topleft, int has_topright, int stride)
{
    PREDICT_8x8_LOAD_LEFT;
    PREDICT_8x8_LOAD_TOP;
    const uint32_t dc = ((l0+l1+l2+l3+l4+l5+l6+l7
                         +t0+t1+t2+t3+t4+t5+t6+t7+8) >> 4) * 0x01010101;
    PREDICT_8x8_DC(dc);
}
static void pred8x8l_horizontal_c(uint8_t *src, int has_topleft, int has_topright, int stride)
{
    PREDICT_8x8_LOAD_LEFT;
#define ROW(y) ((uint32_t*)(src+y*stride))[0] =\
               ((uint32_t*)(src+y*stride))[1] = 0x01010101 * l##y
    ROW(0); ROW(1); ROW(2); ROW(3); ROW(4); ROW(5); ROW(6); ROW(7);
#undef ROW
}
static void pred8x8l_vertical_c(uint8_t *src, int has_topleft, int has_topright, int stride)
{
    int y;
    PREDICT_8x8_LOAD_TOP;
    src[0] = t0;
    src[1] = t1;
    src[2] = t2;
    src[3] = t3;
    src[4] = t4;
    src[5] = t5;
    src[6] = t6;
    src[7] = t7;
    for( y = 1; y < 8; y++ )
        *(uint64_t*)(src+y*stride) = *(uint64_t*)src;
}
static void pred8x8l_down_left_c(uint8_t *src, int has_topleft, int has_topright, int stride)
{
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
static void pred8x8l_down_right_c(uint8_t *src, int has_topleft, int has_topright, int stride)
{
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
static void pred8x8l_vertical_right_c(uint8_t *src, int has_topleft, int has_topright, int stride)
{
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
static void pred8x8l_horizontal_down_c(uint8_t *src, int has_topleft, int has_topright, int stride)
{
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
static void pred8x8l_vertical_left_c(uint8_t *src, int has_topleft, int has_topright, int stride)
{
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
static void pred8x8l_horizontal_up_c(uint8_t *src, int has_topleft, int has_topright, int stride)
{
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

static void pred4x4_vertical_add_c(uint8_t *pix, const DCTELEM *block, int stride){
    int i;
    pix -= stride;
    for(i=0; i<4; i++){
        uint8_t v = pix[0];
        pix[1*stride]= v += block[0];
        pix[2*stride]= v += block[4];
        pix[3*stride]= v += block[8];
        pix[4*stride]= v +  block[12];
        pix++;
        block++;
    }
}

static void pred4x4_horizontal_add_c(uint8_t *pix, const DCTELEM *block, int stride){
    int i;
    for(i=0; i<4; i++){
        uint8_t v = pix[-1];
        pix[0]= v += block[0];
        pix[1]= v += block[1];
        pix[2]= v += block[2];
        pix[3]= v +  block[3];
        pix+= stride;
        block+= 4;
    }
}

static void pred8x8l_vertical_add_c(uint8_t *pix, const DCTELEM *block, int stride){
    int i;
    pix -= stride;
    for(i=0; i<8; i++){
        uint8_t v = pix[0];
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
}

static void pred8x8l_horizontal_add_c(uint8_t *pix, const DCTELEM *block, int stride){
    int i;
    for(i=0; i<8; i++){
        uint8_t v = pix[-1];
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
}

static void pred16x16_vertical_add_c(uint8_t *pix, const int *block_offset, const DCTELEM *block, int stride){
    int i;
    for(i=0; i<16; i++)
        pred4x4_vertical_add_c(pix + block_offset[i], block + i*16, stride);
}

static void pred16x16_horizontal_add_c(uint8_t *pix, const int *block_offset, const DCTELEM *block, int stride){
    int i;
    for(i=0; i<16; i++)
        pred4x4_horizontal_add_c(pix + block_offset[i], block + i*16, stride);
}

static void pred8x8_vertical_add_c(uint8_t *pix, const int *block_offset, const DCTELEM *block, int stride){
    int i;
    for(i=0; i<4; i++)
        pred4x4_vertical_add_c(pix + block_offset[i], block + i*16, stride);
}

static void pred8x8_horizontal_add_c(uint8_t *pix, const int *block_offset, const DCTELEM *block, int stride){
    int i;
    for(i=0; i<4; i++)
        pred4x4_horizontal_add_c(pix + block_offset[i], block + i*16, stride);
}


/**
 * Sets the intra prediction function pointers.
 */
void ff_h264_pred_init(H264PredContext *h, int codec_id){
//    MpegEncContext * const s = &h->s;

    if(codec_id != CODEC_ID_RV40){
        h->pred4x4[VERT_PRED           ]= pred4x4_vertical_c;
        h->pred4x4[HOR_PRED            ]= pred4x4_horizontal_c;
        h->pred4x4[DC_PRED             ]= pred4x4_dc_c;
        if(codec_id == CODEC_ID_SVQ3)
            h->pred4x4[DIAG_DOWN_LEFT_PRED ]= pred4x4_down_left_svq3_c;
        else
            h->pred4x4[DIAG_DOWN_LEFT_PRED ]= pred4x4_down_left_c;
        h->pred4x4[DIAG_DOWN_RIGHT_PRED]= pred4x4_down_right_c;
        h->pred4x4[VERT_RIGHT_PRED     ]= pred4x4_vertical_right_c;
        h->pred4x4[HOR_DOWN_PRED       ]= pred4x4_horizontal_down_c;
        h->pred4x4[VERT_LEFT_PRED      ]= pred4x4_vertical_left_c;
        h->pred4x4[HOR_UP_PRED         ]= pred4x4_horizontal_up_c;
        h->pred4x4[LEFT_DC_PRED        ]= pred4x4_left_dc_c;
        h->pred4x4[TOP_DC_PRED         ]= pred4x4_top_dc_c;
        h->pred4x4[DC_128_PRED         ]= pred4x4_128_dc_c;
    }else{
        h->pred4x4[VERT_PRED           ]= pred4x4_vertical_c;
        h->pred4x4[HOR_PRED            ]= pred4x4_horizontal_c;
        h->pred4x4[DC_PRED             ]= pred4x4_dc_c;
        h->pred4x4[DIAG_DOWN_LEFT_PRED ]= pred4x4_down_left_rv40_c;
        h->pred4x4[DIAG_DOWN_RIGHT_PRED]= pred4x4_down_right_c;
        h->pred4x4[VERT_RIGHT_PRED     ]= pred4x4_vertical_right_c;
        h->pred4x4[HOR_DOWN_PRED       ]= pred4x4_horizontal_down_c;
        h->pred4x4[VERT_LEFT_PRED      ]= pred4x4_vertical_left_rv40_c;
        h->pred4x4[HOR_UP_PRED         ]= pred4x4_horizontal_up_rv40_c;
        h->pred4x4[LEFT_DC_PRED        ]= pred4x4_left_dc_c;
        h->pred4x4[TOP_DC_PRED         ]= pred4x4_top_dc_c;
        h->pred4x4[DC_128_PRED         ]= pred4x4_128_dc_c;
        h->pred4x4[DIAG_DOWN_LEFT_PRED_RV40_NODOWN]= pred4x4_down_left_rv40_nodown_c;
        h->pred4x4[HOR_UP_PRED_RV40_NODOWN]= pred4x4_horizontal_up_rv40_nodown_c;
        h->pred4x4[VERT_LEFT_PRED_RV40_NODOWN]= pred4x4_vertical_left_rv40_nodown_c;
    }

    h->pred8x8l[VERT_PRED           ]= pred8x8l_vertical_c;
    h->pred8x8l[HOR_PRED            ]= pred8x8l_horizontal_c;
    h->pred8x8l[DC_PRED             ]= pred8x8l_dc_c;
    h->pred8x8l[DIAG_DOWN_LEFT_PRED ]= pred8x8l_down_left_c;
    h->pred8x8l[DIAG_DOWN_RIGHT_PRED]= pred8x8l_down_right_c;
    h->pred8x8l[VERT_RIGHT_PRED     ]= pred8x8l_vertical_right_c;
    h->pred8x8l[HOR_DOWN_PRED       ]= pred8x8l_horizontal_down_c;
    h->pred8x8l[VERT_LEFT_PRED      ]= pred8x8l_vertical_left_c;
    h->pred8x8l[HOR_UP_PRED         ]= pred8x8l_horizontal_up_c;
    h->pred8x8l[LEFT_DC_PRED        ]= pred8x8l_left_dc_c;
    h->pred8x8l[TOP_DC_PRED         ]= pred8x8l_top_dc_c;
    h->pred8x8l[DC_128_PRED         ]= pred8x8l_128_dc_c;

    h->pred8x8[VERT_PRED8x8   ]= pred8x8_vertical_c;
    h->pred8x8[HOR_PRED8x8    ]= pred8x8_horizontal_c;
    h->pred8x8[PLANE_PRED8x8  ]= pred8x8_plane_c;
    if(codec_id != CODEC_ID_RV40){
        h->pred8x8[DC_PRED8x8     ]= pred8x8_dc_c;
        h->pred8x8[LEFT_DC_PRED8x8]= pred8x8_left_dc_c;
        h->pred8x8[TOP_DC_PRED8x8 ]= pred8x8_top_dc_c;
        h->pred8x8[ALZHEIMER_DC_L0T_PRED8x8 ]= pred8x8_mad_cow_dc_l0t;
        h->pred8x8[ALZHEIMER_DC_0LT_PRED8x8 ]= pred8x8_mad_cow_dc_0lt;
        h->pred8x8[ALZHEIMER_DC_L00_PRED8x8 ]= pred8x8_mad_cow_dc_l00;
        h->pred8x8[ALZHEIMER_DC_0L0_PRED8x8 ]= pred8x8_mad_cow_dc_0l0;
    }else{
        h->pred8x8[DC_PRED8x8     ]= pred8x8_dc_rv40_c;
        h->pred8x8[LEFT_DC_PRED8x8]= pred8x8_left_dc_rv40_c;
        h->pred8x8[TOP_DC_PRED8x8 ]= pred8x8_top_dc_rv40_c;
    }
    h->pred8x8[DC_128_PRED8x8 ]= pred8x8_128_dc_c;

    h->pred16x16[DC_PRED8x8     ]= pred16x16_dc_c;
    h->pred16x16[VERT_PRED8x8   ]= pred16x16_vertical_c;
    h->pred16x16[HOR_PRED8x8    ]= pred16x16_horizontal_c;
    h->pred16x16[PLANE_PRED8x8  ]= pred16x16_plane_c;
    switch(codec_id){
    case CODEC_ID_SVQ3:
       h->pred16x16[PLANE_PRED8x8  ]= pred16x16_plane_svq3_c;
       break;
    case CODEC_ID_RV40:
       h->pred16x16[PLANE_PRED8x8  ]= pred16x16_plane_rv40_c;
       break;
    default:
       h->pred16x16[PLANE_PRED8x8  ]= pred16x16_plane_c;
    }
    h->pred16x16[LEFT_DC_PRED8x8]= pred16x16_left_dc_c;
    h->pred16x16[TOP_DC_PRED8x8 ]= pred16x16_top_dc_c;
    h->pred16x16[DC_128_PRED8x8 ]= pred16x16_128_dc_c;

    //special lossless h/v prediction for h264
    h->pred4x4_add  [VERT_PRED   ]= pred4x4_vertical_add_c;
    h->pred4x4_add  [ HOR_PRED   ]= pred4x4_horizontal_add_c;
    h->pred8x8l_add [VERT_PRED   ]= pred8x8l_vertical_add_c;
    h->pred8x8l_add [ HOR_PRED   ]= pred8x8l_horizontal_add_c;
    h->pred8x8_add  [VERT_PRED8x8]= pred8x8_vertical_add_c;
    h->pred8x8_add  [ HOR_PRED8x8]= pred8x8_horizontal_add_c;
    h->pred16x16_add[VERT_PRED8x8]= pred16x16_vertical_add_c;
    h->pred16x16_add[ HOR_PRED8x8]= pred16x16_horizontal_add_c;

    if (ARCH_ARM) ff_h264_pred_init_arm(h, codec_id);
}
