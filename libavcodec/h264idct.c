/*
 * H.264 IDCT
 * Copyright (c) 2004 Michael Niedermayer <michaelni@gmx.at>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
 
/**
 * @file h264-idct.c
 * H.264 IDCT.
 * @author Michael Niedermayer <michaelni@gmx.at>
 */
 
#include "dsputil.h"

static always_inline void idct_internal(uint8_t *dst, DCTELEM *block, int stride, int block_stride, int shift, int add){
    int i;
    uint8_t *cm = cropTbl + MAX_NEG_CROP;

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
