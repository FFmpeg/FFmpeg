/*
 * H.264 IDCT
 * Copyright (c) 2004 Michael Niedermayer <michaelni@gmx.at>
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

#include "dsputil.h"

static av_always_inline void idct_internal(uint8_t *dst, DCTELEM *block, int stride, int block_stride, int shift, int add){
    int i;
    uint8_t *cm = ff_cropTbl + MAX_NEG_CROP;

    block[0] += 1<<(shift-1);

    for(i=0; i<4; i++){
        const int z0=  block[0 + block_stride*i]     +  block[2 + block_stride*i];
        const int z1=  block[0 + block_stride*i]     -  block[2 + block_stride*i];
        const int z2= (block[1 + block_stride*i]>>1) -  block[3 + block_stride*i];
        const int z3=  block[1 + block_stride*i]     + (block[3 + block_stride*i]>>1);

        block[0 + block_stride*i]= z0 + z3;
        block[1 + block_stride*i]= z1 + z2;
        block[2 + block_stride*i]= z1 - z2;
        block[3 + block_stride*i]= z0 - z3;
    }

    for(i=0; i<4; i++){
        const int z0=  block[i + block_stride*0]     +  block[i + block_stride*2];
        const int z1=  block[i + block_stride*0]     -  block[i + block_stride*2];
        const int z2= (block[i + block_stride*1]>>1) -  block[i + block_stride*3];
        const int z3=  block[i + block_stride*1]     + (block[i + block_stride*3]>>1);

        dst[i + 0*stride]= cm[ add*dst[i + 0*stride] + ((z0 + z3) >> shift) ];
        dst[i + 1*stride]= cm[ add*dst[i + 1*stride] + ((z1 + z2) >> shift) ];
        dst[i + 2*stride]= cm[ add*dst[i + 2*stride] + ((z1 - z2) >> shift) ];
        dst[i + 3*stride]= cm[ add*dst[i + 3*stride] + ((z0 - z3) >> shift) ];
    }
}

void ff_h264_idct_add_c(uint8_t *dst, DCTELEM *block, int stride){
    idct_internal(dst, block, stride, 4, 6, 1);
}

void ff_h264_lowres_idct_add_c(uint8_t *dst, int stride, DCTELEM *block){
    idct_internal(dst, block, stride, 8, 3, 1);
}

void ff_h264_lowres_idct_put_c(uint8_t *dst, int stride, DCTELEM *block){
    idct_internal(dst, block, stride, 8, 3, 0);
}

void ff_h264_idct8_add_c(uint8_t *dst, DCTELEM *block, int stride){
    int i;
    uint8_t *cm = ff_cropTbl + MAX_NEG_CROP;

    block[0] += 32;

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

        block[0+i*8] = b0 + b7;
        block[7+i*8] = b0 - b7;
        block[1+i*8] = b2 + b5;
        block[6+i*8] = b2 - b5;
        block[2+i*8] = b4 + b3;
        block[5+i*8] = b4 - b3;
        block[3+i*8] = b6 + b1;
        block[4+i*8] = b6 - b1;
    }
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

        dst[i + 0*stride] = cm[ dst[i + 0*stride] + ((b0 + b7) >> 6) ];
        dst[i + 1*stride] = cm[ dst[i + 1*stride] + ((b2 + b5) >> 6) ];
        dst[i + 2*stride] = cm[ dst[i + 2*stride] + ((b4 + b3) >> 6) ];
        dst[i + 3*stride] = cm[ dst[i + 3*stride] + ((b6 + b1) >> 6) ];
        dst[i + 4*stride] = cm[ dst[i + 4*stride] + ((b6 - b1) >> 6) ];
        dst[i + 5*stride] = cm[ dst[i + 5*stride] + ((b4 - b3) >> 6) ];
        dst[i + 6*stride] = cm[ dst[i + 6*stride] + ((b2 - b5) >> 6) ];
        dst[i + 7*stride] = cm[ dst[i + 7*stride] + ((b0 - b7) >> 6) ];
    }
}

// assumes all AC coefs are 0
void ff_h264_idct_dc_add_c(uint8_t *dst, DCTELEM *block, int stride){
    int i, j;
    uint8_t *cm = ff_cropTbl + MAX_NEG_CROP;
    int dc = (block[0] + 32) >> 6;
    for( j = 0; j < 4; j++ )
    {
        for( i = 0; i < 4; i++ )
            dst[i] = cm[ dst[i] + dc ];
        dst += stride;
    }
}

void ff_h264_idct8_dc_add_c(uint8_t *dst, DCTELEM *block, int stride){
    int i, j;
    uint8_t *cm = ff_cropTbl + MAX_NEG_CROP;
    int dc = (block[0] + 32) >> 6;
    for( j = 0; j < 8; j++ )
    {
        for( i = 0; i < 8; i++ )
            dst[i] = cm[ dst[i] + dc ];
        dst += stride;
    }
}

//FIXME this table is a duplicate from h264data.h, and will be removed once the tables from, h264 have been split
static const uint8_t scan8[16 + 2*4]={
 4+1*8, 5+1*8, 4+2*8, 5+2*8,
 6+1*8, 7+1*8, 6+2*8, 7+2*8,
 4+3*8, 5+3*8, 4+4*8, 5+4*8,
 6+3*8, 7+3*8, 6+4*8, 7+4*8,
 1+1*8, 2+1*8,
 1+2*8, 2+2*8,
 1+4*8, 2+4*8,
 1+5*8, 2+5*8,
};

void ff_h264_idct_add16_c(uint8_t *dst, const int *block_offset, DCTELEM *block, int stride, const uint8_t nnzc[6*8]){
    int i;
    for(i=0; i<16; i++){
        int nnz = nnzc[ scan8[i] ];
        if(nnz){
            if(nnz==1 && block[i*16]) ff_h264_idct_dc_add_c(dst + block_offset[i], block + i*16, stride);
            else                      idct_internal        (dst + block_offset[i], block + i*16, stride, 4, 6, 1);
        }
    }
}

void ff_h264_idct_add16intra_c(uint8_t *dst, const int *block_offset, DCTELEM *block, int stride, const uint8_t nnzc[6*8]){
    int i;
    for(i=0; i<16; i++){
        if(nnzc[ scan8[i] ]) idct_internal        (dst + block_offset[i], block + i*16, stride, 4, 6, 1);
        else if(block[i*16]) ff_h264_idct_dc_add_c(dst + block_offset[i], block + i*16, stride);
    }
}

void ff_h264_idct8_add4_c(uint8_t *dst, const int *block_offset, DCTELEM *block, int stride, const uint8_t nnzc[6*8]){
    int i;
    for(i=0; i<16; i+=4){
        int nnz = nnzc[ scan8[i] ];
        if(nnz){
            if(nnz==1 && block[i*16]) ff_h264_idct8_dc_add_c(dst + block_offset[i], block + i*16, stride);
            else                      ff_h264_idct8_add_c   (dst + block_offset[i], block + i*16, stride);
        }
    }
}

void ff_h264_idct_add8_c(uint8_t **dest, const int *block_offset, DCTELEM *block, int stride, const uint8_t nnzc[6*8]){
    int i;
    for(i=16; i<16+8; i++){
        if(nnzc[ scan8[i] ])
            ff_h264_idct_add_c   (dest[(i&4)>>2] + block_offset[i], block + i*16, stride);
        else if(block[i*16])
            ff_h264_idct_dc_add_c(dest[(i&4)>>2] + block_offset[i], block + i*16, stride);
    }
}
