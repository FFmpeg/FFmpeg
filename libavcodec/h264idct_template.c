/*
 * H.264 IDCT
 * Copyright (c) 2004-2011 Michael Niedermayer <michaelni@gmx.at>
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
 * H.264 IDCT.
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#include "high_bit_depth.h"

#ifndef AVCODEC_H264IDCT_INTERNAL_H
#define AVCODEC_H264IDCT_INTERNAL_H
//FIXME this table is a duplicate from h264data.h, and will be removed once the tables from, h264 have been split
static const uint8_t scan8[16*3]={
 4+ 1*8, 5+ 1*8, 4+ 2*8, 5+ 2*8,
 6+ 1*8, 7+ 1*8, 6+ 2*8, 7+ 2*8,
 4+ 3*8, 5+ 3*8, 4+ 4*8, 5+ 4*8,
 6+ 3*8, 7+ 3*8, 6+ 4*8, 7+ 4*8,
 4+ 6*8, 5+ 6*8, 4+ 7*8, 5+ 7*8,
 6+ 6*8, 7+ 6*8, 6+ 7*8, 7+ 7*8,
 4+ 8*8, 5+ 8*8, 4+ 9*8, 5+ 9*8,
 6+ 8*8, 7+ 8*8, 6+ 9*8, 7+ 9*8,
 4+11*8, 5+11*8, 4+12*8, 5+12*8,
 6+11*8, 7+11*8, 6+12*8, 7+12*8,
 4+13*8, 5+13*8, 4+14*8, 5+14*8,
 6+13*8, 7+13*8, 6+14*8, 7+14*8
};
#endif

static av_always_inline void FUNCC(idct_internal)(uint8_t *p_dst, DCTELEM *p_block, int stride, int block_stride, int shift, int add){
    int i;
    INIT_CLIP
    pixel *dst = (pixel*)p_dst;
    dctcoef *block = (dctcoef*)p_block;
    stride >>= sizeof(pixel)-1;

    block[0] += 1<<(shift-1);

    for(i=0; i<4; i++){
        const int z0=  block[i + block_stride*0]     +  block[i + block_stride*2];
        const int z1=  block[i + block_stride*0]     -  block[i + block_stride*2];
        const int z2= (block[i + block_stride*1]>>1) -  block[i + block_stride*3];
        const int z3=  block[i + block_stride*1]     + (block[i + block_stride*3]>>1);

        block[i + block_stride*0]= z0 + z3;
        block[i + block_stride*1]= z1 + z2;
        block[i + block_stride*2]= z1 - z2;
        block[i + block_stride*3]= z0 - z3;
    }

    for(i=0; i<4; i++){
        const int z0=  block[0 + block_stride*i]     +  block[2 + block_stride*i];
        const int z1=  block[0 + block_stride*i]     -  block[2 + block_stride*i];
        const int z2= (block[1 + block_stride*i]>>1) -  block[3 + block_stride*i];
        const int z3=  block[1 + block_stride*i]     + (block[3 + block_stride*i]>>1);

        dst[i + 0*stride]= CLIP(add*dst[i + 0*stride] + ((z0 + z3) >> shift));
        dst[i + 1*stride]= CLIP(add*dst[i + 1*stride] + ((z1 + z2) >> shift));
        dst[i + 2*stride]= CLIP(add*dst[i + 2*stride] + ((z1 - z2) >> shift));
        dst[i + 3*stride]= CLIP(add*dst[i + 3*stride] + ((z0 - z3) >> shift));
    }
}

void FUNCC(ff_h264_idct_add)(uint8_t *dst, DCTELEM *block, int stride){
    FUNCC(idct_internal)(dst, block, stride, 4, 6, 1);
}

void FUNCC(ff_h264_lowres_idct_add)(uint8_t *dst, int stride, DCTELEM *block){
    FUNCC(idct_internal)(dst, block, stride, 8, 3, 1);
}

void FUNCC(ff_h264_lowres_idct_put)(uint8_t *dst, int stride, DCTELEM *block){
    FUNCC(idct_internal)(dst, block, stride, 8, 3, 0);
}

void FUNCC(ff_h264_idct8_add)(uint8_t *p_dst, DCTELEM *p_block, int stride){
    int i;
    INIT_CLIP
    pixel *dst = (pixel*)p_dst;
    dctcoef *block = (dctcoef*)p_block;
    stride >>= sizeof(pixel)-1;

    block[0] += 32;

    for( i = 0; i < 8; i++ )
    {
        const int a0 =  block[i+0*8] + block[i+4*8];
        const int a2 =  block[i+0*8] - block[i+4*8];
        const int a4 = (block[i+2*8]>>1) - block[i+6*8];
        const int a6 = (block[i+6*8]>>1) + block[i+2*8];

        const int b0 = a0 + a6;
        const int b2 = a2 + a4;
        const int b4 = a2 - a4;
        const int b6 = a0 - a6;

        const int a1 = -block[i+3*8] + block[i+5*8] - block[i+7*8] - (block[i+7*8]>>1);
        const int a3 =  block[i+1*8] + block[i+7*8] - block[i+3*8] - (block[i+3*8]>>1);
        const int a5 = -block[i+1*8] + block[i+7*8] + block[i+5*8] + (block[i+5*8]>>1);
        const int a7 =  block[i+3*8] + block[i+5*8] + block[i+1*8] + (block[i+1*8]>>1);

        const int b1 = (a7>>2) + a1;
        const int b3 =  a3 + (a5>>2);
        const int b5 = (a3>>2) - a5;
        const int b7 =  a7 - (a1>>2);

        block[i+0*8] = b0 + b7;
        block[i+7*8] = b0 - b7;
        block[i+1*8] = b2 + b5;
        block[i+6*8] = b2 - b5;
        block[i+2*8] = b4 + b3;
        block[i+5*8] = b4 - b3;
        block[i+3*8] = b6 + b1;
        block[i+4*8] = b6 - b1;
    }
    for( i = 0; i < 8; i++ )
    {
        const int a0 =  block[0+i*8] + block[4+i*8];
        const int a2 =  block[0+i*8] - block[4+i*8];
        const int a4 = (block[2+i*8]>>1) - block[6+i*8];
        const int a6 = (block[6+i*8]>>1) + block[2+i*8];

        const int b0 = a0 + a6;
        const int b2 = a2 + a4;
        const int b4 = a2 - a4;
        const int b6 = a0 - a6;

        const int a1 = -block[3+i*8] + block[5+i*8] - block[7+i*8] - (block[7+i*8]>>1);
        const int a3 =  block[1+i*8] + block[7+i*8] - block[3+i*8] - (block[3+i*8]>>1);
        const int a5 = -block[1+i*8] + block[7+i*8] + block[5+i*8] + (block[5+i*8]>>1);
        const int a7 =  block[3+i*8] + block[5+i*8] + block[1+i*8] + (block[1+i*8]>>1);

        const int b1 = (a7>>2) + a1;
        const int b3 =  a3 + (a5>>2);
        const int b5 = (a3>>2) - a5;
        const int b7 =  a7 - (a1>>2);

        dst[i + 0*stride] = CLIP( dst[i + 0*stride] + ((b0 + b7) >> 6) );
        dst[i + 1*stride] = CLIP( dst[i + 1*stride] + ((b2 + b5) >> 6) );
        dst[i + 2*stride] = CLIP( dst[i + 2*stride] + ((b4 + b3) >> 6) );
        dst[i + 3*stride] = CLIP( dst[i + 3*stride] + ((b6 + b1) >> 6) );
        dst[i + 4*stride] = CLIP( dst[i + 4*stride] + ((b6 - b1) >> 6) );
        dst[i + 5*stride] = CLIP( dst[i + 5*stride] + ((b4 - b3) >> 6) );
        dst[i + 6*stride] = CLIP( dst[i + 6*stride] + ((b2 - b5) >> 6) );
        dst[i + 7*stride] = CLIP( dst[i + 7*stride] + ((b0 - b7) >> 6) );
    }
}

// assumes all AC coefs are 0
void FUNCC(ff_h264_idct_dc_add)(uint8_t *p_dst, DCTELEM *block, int stride){
    int i, j;
    int dc = (((dctcoef*)block)[0] + 32) >> 6;
    INIT_CLIP
    pixel *dst = (pixel*)p_dst;
    stride >>= sizeof(pixel)-1;
    for( j = 0; j < 4; j++ )
    {
        for( i = 0; i < 4; i++ )
            dst[i] = CLIP( dst[i] + dc );
        dst += stride;
    }
}

void FUNCC(ff_h264_idct8_dc_add)(uint8_t *p_dst, DCTELEM *block, int stride){
    int i, j;
    int dc = (((dctcoef*)block)[0] + 32) >> 6;
    INIT_CLIP
    pixel *dst = (pixel*)p_dst;
    stride >>= sizeof(pixel)-1;
    for( j = 0; j < 8; j++ )
    {
        for( i = 0; i < 8; i++ )
            dst[i] = CLIP( dst[i] + dc );
        dst += stride;
    }
}

void FUNCC(ff_h264_idct_add16)(uint8_t *dst, const int *block_offset, DCTELEM *block, int stride, const uint8_t nnzc[15*8]){
    int i;
    for(i=0; i<16; i++){
        int nnz = nnzc[ scan8[i] ];
        if(nnz){
            if(nnz==1 && ((dctcoef*)block)[i*16]) FUNCC(ff_h264_idct_dc_add)(dst + block_offset[i], block + i*16*sizeof(pixel), stride);
            else                                  FUNCC(idct_internal      )(dst + block_offset[i], block + i*16*sizeof(pixel), stride, 4, 6, 1);
        }
    }
}

void FUNCC(ff_h264_idct_add16intra)(uint8_t *dst, const int *block_offset, DCTELEM *block, int stride, const uint8_t nnzc[15*8]){
    int i;
    for(i=0; i<16; i++){
        if(nnzc[ scan8[i] ])             FUNCC(idct_internal      )(dst + block_offset[i], block + i*16*sizeof(pixel), stride, 4, 6, 1);
        else if(((dctcoef*)block)[i*16]) FUNCC(ff_h264_idct_dc_add)(dst + block_offset[i], block + i*16*sizeof(pixel), stride);
    }
}

void FUNCC(ff_h264_idct8_add4)(uint8_t *dst, const int *block_offset, DCTELEM *block, int stride, const uint8_t nnzc[15*8]){
    int i;
    for(i=0; i<16; i+=4){
        int nnz = nnzc[ scan8[i] ];
        if(nnz){
            if(nnz==1 && ((dctcoef*)block)[i*16]) FUNCC(ff_h264_idct8_dc_add)(dst + block_offset[i], block + i*16*sizeof(pixel), stride);
            else                                  FUNCC(ff_h264_idct8_add   )(dst + block_offset[i], block + i*16*sizeof(pixel), stride);
        }
    }
}

void FUNCC(ff_h264_idct_add8)(uint8_t **dest, const int *block_offset, DCTELEM *block, int stride, const uint8_t nnzc[15*8]){
    int i, j;
    for(j=1; j<3; j++){
        for(i=j*16; i<j*16+4; i++){
            if(nnzc[ scan8[i] ])
                FUNCC(ff_h264_idct_add   )(dest[j-1] + block_offset[i], block + i*16*sizeof(pixel), stride);
            else if(((dctcoef*)block)[i*16])
                FUNCC(ff_h264_idct_dc_add)(dest[j-1] + block_offset[i], block + i*16*sizeof(pixel), stride);
        }
    }
}
/**
 * IDCT transforms the 16 dc values and dequantizes them.
 * @param qp quantization parameter
 */
void FUNCC(ff_h264_luma_dc_dequant_idct)(DCTELEM *p_output, DCTELEM *p_input, int qmul){
#define stride 16
    int i;
    int temp[16];
    static const uint8_t x_offset[4]={0, 2*stride, 8*stride, 10*stride};
    dctcoef *input = (dctcoef*)p_input;
    dctcoef *output = (dctcoef*)p_output;

    for(i=0; i<4; i++){
        const int z0= input[4*i+0] + input[4*i+1];
        const int z1= input[4*i+0] - input[4*i+1];
        const int z2= input[4*i+2] - input[4*i+3];
        const int z3= input[4*i+2] + input[4*i+3];

        temp[4*i+0]= z0+z3;
        temp[4*i+1]= z0-z3;
        temp[4*i+2]= z1-z2;
        temp[4*i+3]= z1+z2;
    }

    for(i=0; i<4; i++){
        const int offset= x_offset[i];
        const int z0= temp[4*0+i] + temp[4*2+i];
        const int z1= temp[4*0+i] - temp[4*2+i];
        const int z2= temp[4*1+i] - temp[4*3+i];
        const int z3= temp[4*1+i] + temp[4*3+i];

        output[stride* 0+offset]= ((((z0 + z3)*qmul + 128 ) >> 8));
        output[stride* 1+offset]= ((((z1 + z2)*qmul + 128 ) >> 8));
        output[stride* 4+offset]= ((((z1 - z2)*qmul + 128 ) >> 8));
        output[stride* 5+offset]= ((((z0 - z3)*qmul + 128 ) >> 8));
    }
#undef stride
}

void FUNCC(ff_h264_chroma_dc_dequant_idct)(DCTELEM *p_block, int qmul){
    const int stride= 16*2;
    const int xStride= 16;
    int a,b,c,d,e;
    dctcoef *block = (dctcoef*)p_block;

    a= block[stride*0 + xStride*0];
    b= block[stride*0 + xStride*1];
    c= block[stride*1 + xStride*0];
    d= block[stride*1 + xStride*1];

    e= a-b;
    a= a+b;
    b= c-d;
    c= c+d;

    block[stride*0 + xStride*0]= ((a+c)*qmul) >> 7;
    block[stride*0 + xStride*1]= ((e+b)*qmul) >> 7;
    block[stride*1 + xStride*0]= ((a-c)*qmul) >> 7;
    block[stride*1 + xStride*1]= ((e-b)*qmul) >> 7;
}
