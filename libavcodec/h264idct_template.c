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

#include "bit_depth_template.c"
#include "libavutil/common.h"
#include "h264dec.h"
#include "h264idct.h"

void FUNCC(ff_h264_idct_add)(uint8_t *_dst, int16_t *_block, int stride)
{
    int i;
    pixel *dst = (pixel*)_dst;
    dctcoef *block = (dctcoef*)_block;
    stride >>= sizeof(pixel)-1;

    block[0] += 1 << 5;

    for(i=0; i<4; i++){
        const SUINT z0=  block[i + 4*0]     +  (unsigned)block[i + 4*2];
        const SUINT z1=  block[i + 4*0]     -  (unsigned)block[i + 4*2];
        const SUINT z2= (block[i + 4*1]>>1) -  (unsigned)block[i + 4*3];
        const SUINT z3=  block[i + 4*1]     + (unsigned)(block[i + 4*3]>>1);

        block[i + 4*0]= z0 + z3;
        block[i + 4*1]= z1 + z2;
        block[i + 4*2]= z1 - z2;
        block[i + 4*3]= z0 - z3;
    }

    for(i=0; i<4; i++){
        const SUINT z0=  block[0 + 4*i]     +  (SUINT)block[2 + 4*i];
        const SUINT z1=  block[0 + 4*i]     -  (SUINT)block[2 + 4*i];
        const SUINT z2= (block[1 + 4*i]>>1) -  (SUINT)block[3 + 4*i];
        const SUINT z3=  block[1 + 4*i]     + (SUINT)(block[3 + 4*i]>>1);

        dst[i + 0*stride]= av_clip_pixel(dst[i + 0*stride] + ((int)(z0 + z3) >> 6));
        dst[i + 1*stride]= av_clip_pixel(dst[i + 1*stride] + ((int)(z1 + z2) >> 6));
        dst[i + 2*stride]= av_clip_pixel(dst[i + 2*stride] + ((int)(z1 - z2) >> 6));
        dst[i + 3*stride]= av_clip_pixel(dst[i + 3*stride] + ((int)(z0 - z3) >> 6));
    }

    memset(block, 0, 16 * sizeof(dctcoef));
}

void FUNCC(ff_h264_idct8_add)(uint8_t *_dst, int16_t *_block, int stride){
    int i;
    pixel *dst = (pixel*)_dst;
    dctcoef *block = (dctcoef*)_block;
    stride >>= sizeof(pixel)-1;

    block[0] += 32;

    for( i = 0; i < 8; i++ )
    {
        const unsigned int a0 =  block[i+0*8] + (unsigned)block[i+4*8];
        const unsigned int a2 =  block[i+0*8] - (unsigned)block[i+4*8];
        const unsigned int a4 = (block[i+2*8]>>1) - (unsigned)block[i+6*8];
        const unsigned int a6 = (block[i+6*8]>>1) + (unsigned)block[i+2*8];

        const unsigned int b0 = a0 + a6;
        const unsigned int b2 = a2 + a4;
        const unsigned int b4 = a2 - a4;
        const unsigned int b6 = a0 - a6;

        const int a1 = -block[i+3*8] + (unsigned)block[i+5*8] - block[i+7*8] - (block[i+7*8]>>1);
        const int a3 =  block[i+1*8] + (unsigned)block[i+7*8] - block[i+3*8] - (block[i+3*8]>>1);
        const int a5 = -block[i+1*8] + (unsigned)block[i+7*8] + block[i+5*8] + (block[i+5*8]>>1);
        const int a7 =  block[i+3*8] + (unsigned)block[i+5*8] + block[i+1*8] + (block[i+1*8]>>1);

        const int b1 = (a7>>2) + (unsigned)a1;
        const int b3 =  (unsigned)a3 + (a5>>2);
        const int b5 = (a3>>2) - (unsigned)a5;
        const int b7 =  (unsigned)a7 - (a1>>2);

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
        const unsigned a0 =  block[0+i*8] + (unsigned)block[4+i*8];
        const unsigned a2 =  block[0+i*8] - (unsigned)block[4+i*8];
        const unsigned a4 = (block[2+i*8]>>1) - (unsigned)block[6+i*8];
        const unsigned a6 = (block[6+i*8]>>1) + (unsigned)block[2+i*8];

        const unsigned b0 = a0 + a6;
        const unsigned b2 = a2 + a4;
        const unsigned b4 = a2 - a4;
        const unsigned b6 = a0 - a6;

        const int a1 = -(unsigned)block[3+i*8] + block[5+i*8] - block[7+i*8] - (block[7+i*8]>>1);
        const int a3 =  (unsigned)block[1+i*8] + block[7+i*8] - block[3+i*8] - (block[3+i*8]>>1);
        const int a5 = -(unsigned)block[1+i*8] + block[7+i*8] + block[5+i*8] + (block[5+i*8]>>1);
        const int a7 =  (unsigned)block[3+i*8] + block[5+i*8] + block[1+i*8] + (block[1+i*8]>>1);

        const unsigned b1 = (a7>>2) + (unsigned)a1;
        const unsigned b3 =  (unsigned)a3 + (a5>>2);
        const unsigned b5 = (a3>>2) - (unsigned)a5;
        const unsigned b7 =  (unsigned)a7 - (a1>>2);

        dst[i + 0*stride] = av_clip_pixel( dst[i + 0*stride] + ((int)(b0 + b7) >> 6) );
        dst[i + 1*stride] = av_clip_pixel( dst[i + 1*stride] + ((int)(b2 + b5) >> 6) );
        dst[i + 2*stride] = av_clip_pixel( dst[i + 2*stride] + ((int)(b4 + b3) >> 6) );
        dst[i + 3*stride] = av_clip_pixel( dst[i + 3*stride] + ((int)(b6 + b1) >> 6) );
        dst[i + 4*stride] = av_clip_pixel( dst[i + 4*stride] + ((int)(b6 - b1) >> 6) );
        dst[i + 5*stride] = av_clip_pixel( dst[i + 5*stride] + ((int)(b4 - b3) >> 6) );
        dst[i + 6*stride] = av_clip_pixel( dst[i + 6*stride] + ((int)(b2 - b5) >> 6) );
        dst[i + 7*stride] = av_clip_pixel( dst[i + 7*stride] + ((int)(b0 - b7) >> 6) );
    }

    memset(block, 0, 64 * sizeof(dctcoef));
}

// assumes all AC coefs are 0
void FUNCC(ff_h264_idct_dc_add)(uint8_t *_dst, int16_t *_block, int stride){
    int i, j;
    pixel *dst = (pixel*)_dst;
    dctcoef *block = (dctcoef*)_block;
    int dc = (block[0] + 32) >> 6;
    stride /= sizeof(pixel);
    block[0] = 0;
    for( j = 0; j < 4; j++ )
    {
        for( i = 0; i < 4; i++ )
            dst[i] = av_clip_pixel( dst[i] + dc );
        dst += stride;
    }
}

void FUNCC(ff_h264_idct8_dc_add)(uint8_t *_dst, int16_t *_block, int stride){
    int i, j;
    pixel *dst = (pixel*)_dst;
    dctcoef *block = (dctcoef*)_block;
    int dc = (block[0] + 32) >> 6;
    block[0] = 0;
    stride /= sizeof(pixel);
    for( j = 0; j < 8; j++ )
    {
        for( i = 0; i < 8; i++ )
            dst[i] = av_clip_pixel( dst[i] + dc );
        dst += stride;
    }
}

void FUNCC(ff_h264_idct_add16)(uint8_t *dst, const int *block_offset, int16_t *block, int stride, const uint8_t nnzc[15*8]){
    int i;
    for(i=0; i<16; i++){
        int nnz = nnzc[ scan8[i] ];
        if(nnz){
            if(nnz==1 && ((dctcoef*)block)[i*16]) FUNCC(ff_h264_idct_dc_add)(dst + block_offset[i], block + i*16*sizeof(pixel), stride);
            else                                  FUNCC(ff_h264_idct_add   )(dst + block_offset[i], block + i*16*sizeof(pixel), stride);
        }
    }
}

void FUNCC(ff_h264_idct_add16intra)(uint8_t *dst, const int *block_offset, int16_t *block, int stride, const uint8_t nnzc[15*8]){
    int i;
    for(i=0; i<16; i++){
        if(nnzc[ scan8[i] ])             FUNCC(ff_h264_idct_add   )(dst + block_offset[i], block + i*16*sizeof(pixel), stride);
        else if(((dctcoef*)block)[i*16]) FUNCC(ff_h264_idct_dc_add)(dst + block_offset[i], block + i*16*sizeof(pixel), stride);
    }
}

void FUNCC(ff_h264_idct8_add4)(uint8_t *dst, const int *block_offset, int16_t *block, int stride, const uint8_t nnzc[15*8]){
    int i;
    for(i=0; i<16; i+=4){
        int nnz = nnzc[ scan8[i] ];
        if(nnz){
            if(nnz==1 && ((dctcoef*)block)[i*16]) FUNCC(ff_h264_idct8_dc_add)(dst + block_offset[i], block + i*16*sizeof(pixel), stride);
            else                                  FUNCC(ff_h264_idct8_add   )(dst + block_offset[i], block + i*16*sizeof(pixel), stride);
        }
    }
}

void FUNCC(ff_h264_idct_add8)(uint8_t **dest, const int *block_offset, int16_t *block, int stride, const uint8_t nnzc[15*8]){
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

void FUNCC(ff_h264_idct_add8_422)(uint8_t **dest, const int *block_offset, int16_t *block, int stride, const uint8_t nnzc[15*8]){
    int i, j;

    for(j=1; j<3; j++){
        for(i=j*16; i<j*16+4; i++){
            if(nnzc[ scan8[i] ])
                FUNCC(ff_h264_idct_add   )(dest[j-1] + block_offset[i], block + i*16*sizeof(pixel), stride);
            else if(((dctcoef*)block)[i*16])
                FUNCC(ff_h264_idct_dc_add)(dest[j-1] + block_offset[i], block + i*16*sizeof(pixel), stride);
        }
    }

    for(j=1; j<3; j++){
        for(i=j*16+4; i<j*16+8; i++){
            if(nnzc[ scan8[i+4] ])
                FUNCC(ff_h264_idct_add   )(dest[j-1] + block_offset[i+4], block + i*16*sizeof(pixel), stride);
            else if(((dctcoef*)block)[i*16])
                FUNCC(ff_h264_idct_dc_add)(dest[j-1] + block_offset[i+4], block + i*16*sizeof(pixel), stride);
        }
    }
}

/**
 * IDCT transforms the 16 dc values and dequantizes them.
 * @param qmul quantization parameter
 */
void FUNCC(ff_h264_luma_dc_dequant_idct)(int16_t *_output, int16_t *_input, int qmul){
#define stride 16
    int i;
    int temp[16];
    static const uint8_t x_offset[4]={0, 2*stride, 8*stride, 10*stride};
    dctcoef *input = (dctcoef*)_input;
    dctcoef *output = (dctcoef*)_output;

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
        const SUINT z0= temp[4*0+i] + temp[4*2+i];
        const SUINT z1= temp[4*0+i] - temp[4*2+i];
        const SUINT z2= temp[4*1+i] - temp[4*3+i];
        const SUINT z3= temp[4*1+i] + temp[4*3+i];

        output[stride* 0+offset]= (int)((z0 + z3)*qmul + 128 ) >> 8;
        output[stride* 1+offset]= (int)((z1 + z2)*qmul + 128 ) >> 8;
        output[stride* 4+offset]= (int)((z1 - z2)*qmul + 128 ) >> 8;
        output[stride* 5+offset]= (int)((z0 - z3)*qmul + 128 ) >> 8;
    }
#undef stride
}

void FUNCC(ff_h264_chroma422_dc_dequant_idct)(int16_t *_block, int qmul){
    const int stride= 16*2;
    const int xStride= 16;
    int i;
    unsigned temp[8];
    static const uint8_t x_offset[2]={0, 16};
    dctcoef *block = (dctcoef*)_block;

    for(i=0; i<4; i++){
        temp[2*i+0] = block[stride*i + xStride*0] + (unsigned)block[stride*i + xStride*1];
        temp[2*i+1] = block[stride*i + xStride*0] - (unsigned)block[stride*i + xStride*1];
    }

    for(i=0; i<2; i++){
        const int offset= x_offset[i];
        const SUINT z0= temp[2*0+i] + temp[2*2+i];
        const SUINT z1= temp[2*0+i] - temp[2*2+i];
        const SUINT z2= temp[2*1+i] - temp[2*3+i];
        const SUINT z3= temp[2*1+i] + temp[2*3+i];

        block[stride*0+offset]= (int)((z0 + z3)*qmul + 128) >> 8;
        block[stride*1+offset]= (int)((z1 + z2)*qmul + 128) >> 8;
        block[stride*2+offset]= (int)((z1 - z2)*qmul + 128) >> 8;
        block[stride*3+offset]= (int)((z0 - z3)*qmul + 128) >> 8;
    }
}

void FUNCC(ff_h264_chroma_dc_dequant_idct)(int16_t *_block, int qmul){
    const int stride= 16*2;
    const int xStride= 16;
    SUINT a,b,c,d,e;
    dctcoef *block = (dctcoef*)_block;

    a= block[stride*0 + xStride*0];
    b= block[stride*0 + xStride*1];
    c= block[stride*1 + xStride*0];
    d= block[stride*1 + xStride*1];

    e= a-b;
    a= a+b;
    b= c-d;
    c= c+d;

    block[stride*0 + xStride*0]= (int)((a+c)*qmul) >> 7;
    block[stride*0 + xStride*1]= (int)((e+b)*qmul) >> 7;
    block[stride*1 + xStride*0]= (int)((a-c)*qmul) >> 7;
    block[stride*1 + xStride*1]= (int)((e-b)*qmul) >> 7;
}
