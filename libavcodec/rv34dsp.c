/*
 * RV30/40 decoder common dsp functions
 * Copyright (c) 2007 Mike Melanson, Konstantin Shishkov
 * Copyright (c) 2011 Janne Grunau
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

/**
 * @file
 * RV30/40 decoder common dsp functions
 */
#include "dsputil.h"
#include "rv34dsp.h"

/**
 * @name RV30/40 inverse transform functions
 * @{
 */

static av_always_inline void rv34_row_transform(int temp[16], DCTELEM *block)
{
    int i;

    for(i = 0; i < 4; i++){
        const int z0 = 13*(block[i+8*0] +    block[i+8*2]);
        const int z1 = 13*(block[i+8*0] -    block[i+8*2]);
        const int z2 =  7* block[i+8*1] - 17*block[i+8*3];
        const int z3 = 17* block[i+8*1] +  7*block[i+8*3];

        temp[4*i+0] = z0 + z3;
        temp[4*i+1] = z1 + z2;
        temp[4*i+2] = z1 - z2;
        temp[4*i+3] = z0 - z3;
    }
}

/**
 * Real Video 3.0/4.0 inverse transform
 * Code is almost the same as in SVQ3, only scaling is different.
 */
static void rv34_inv_transform_c(DCTELEM *block){
    int temp[16];
    int i;

    rv34_row_transform(temp, block);

    for(i = 0; i < 4; i++){
        const int z0 = 13*(temp[4*0+i] +    temp[4*2+i]) + 0x200;
        const int z1 = 13*(temp[4*0+i] -    temp[4*2+i]) + 0x200;
        const int z2 =  7* temp[4*1+i] - 17*temp[4*3+i];
        const int z3 = 17* temp[4*1+i] +  7*temp[4*3+i];

        block[i*8+0] = (z0 + z3) >> 10;
        block[i*8+1] = (z1 + z2) >> 10;
        block[i*8+2] = (z1 - z2) >> 10;
        block[i*8+3] = (z0 - z3) >> 10;
    }
}

/**
 * RealVideo 3.0/4.0 inverse transform for DC block
 *
 * Code is almost the same as rv34_inv_transform()
 * but final coefficients are multiplied by 1.5 and have no rounding.
 */
static void rv34_inv_transform_noround_c(DCTELEM *block){
    int temp[16];
    int i;

    rv34_row_transform(temp, block);

    for(i = 0; i < 4; i++){
        const int z0 = 13*(temp[4*0+i] +    temp[4*2+i]);
        const int z1 = 13*(temp[4*0+i] -    temp[4*2+i]);
        const int z2 =  7* temp[4*1+i] - 17*temp[4*3+i];
        const int z3 = 17* temp[4*1+i] +  7*temp[4*3+i];

        block[i*8+0] = ((z0 + z3) * 3) >> 11;
        block[i*8+1] = ((z1 + z2) * 3) >> 11;
        block[i*8+2] = ((z1 - z2) * 3) >> 11;
        block[i*8+3] = ((z0 - z3) * 3) >> 11;
    }
}

/** @} */ // transform


av_cold void ff_rv34dsp_init(RV34DSPContext *c, DSPContext* dsp) {
    c->rv34_inv_transform_tab[0] = rv34_inv_transform_c;
    c->rv34_inv_transform_tab[1] = rv34_inv_transform_noround_c;
}
