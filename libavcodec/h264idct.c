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
 * @file h264-idct.c
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
    DCTELEM (*src)[8] = (DCTELEM(*)[8])block;
    uint8_t *cm = ff_cropTbl + MAX_NEG_CROP;

    block[0] += 32;

    for( i = 0; i < 8; i++ )
    {
        const int a0 =  src[i][0] + src[i][4];
        const int a2 =  src[i][0] - src[i][4];
        const int a4 = (src[i][2]>>1) - src[i][6];
        const int a6 = (src[i][6]>>1) + src[i][2];

        const int b0 = a0 + a6;
        const int b2 = a2 + a4;
        const int b4 = a2 - a4;
        const int b6 = a0 - a6;

        const int a1 = -src[i][3] + src[i][5] - src[i][7] - (src[i][7]>>1);
        const int a3 =  src[i][1] + src[i][7] - src[i][3] - (src[i][3]>>1);
        const int a5 = -src[i][1] + src[i][7] + src[i][5] + (src[i][5]>>1);
        const int a7 =  src[i][3] + src[i][5] + src[i][1] + (src[i][1]>>1);

        const int b1 = (a7>>2) + a1;
        const int b3 =  a3 + (a5>>2);
        const int b5 = (a3>>2) - a5;
        const int b7 =  a7 - (a1>>2);

        src[i][0] = b0 + b7;
        src[i][7] = b0 - b7;
        src[i][1] = b2 + b5;
        src[i][6] = b2 - b5;
        src[i][2] = b4 + b3;
        src[i][5] = b4 - b3;
        src[i][3] = b6 + b1;
        src[i][4] = b6 - b1;
    }
    for( i = 0; i < 8; i++ )
    {
        const int a0 =  src[0][i] + src[4][i];
        const int a2 =  src[0][i] - src[4][i];
        const int a4 = (src[2][i]>>1) - src[6][i];
        const int a6 = (src[6][i]>>1) + src[2][i];

        const int b0 = a0 + a6;
        const int b2 = a2 + a4;
        const int b4 = a2 - a4;
        const int b6 = a0 - a6;

        const int a1 = -src[3][i] + src[5][i] - src[7][i] - (src[7][i]>>1);
        const int a3 =  src[1][i] + src[7][i] - src[3][i] - (src[3][i]>>1);
        const int a5 = -src[1][i] + src[7][i] + src[5][i] + (src[5][i]>>1);
        const int a7 =  src[3][i] + src[5][i] + src[1][i] + (src[1][i]>>1);

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
