/*
 * Copyright (C) 2007 Marc Hoffman <marc.hoffman@analog.com>
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

#include "libavcodec/avcodec.h"
#include "libavcodec/dsputil.h"
#include "dsputil_bfin.h"

void ff_bfin_vp3_idct (DCTELEM *block) attribute_l1_text;
void ff_bfin_idct (DCTELEM *block) attribute_l1_text;
void ff_bfin_add_pixels_clamped (DCTELEM *block, uint8_t *dest, int line_size) attribute_l1_text;
void ff_bfin_put_pixels_clamped (DCTELEM *block, uint8_t *dest, int line_size) attribute_l1_text;

/* Intra iDCT offset 128 */
void ff_bfin_vp3_idct_put (uint8_t *dest, int line_size, DCTELEM *block)
{
    uint8_t *cm = ff_cropTbl + MAX_NEG_CROP;
    int i,j;

    ff_bfin_vp3_idct (block);

    for (i=0;i<8;i++)
        for (j=0;j<8;j++)
            dest[line_size*i+j]=cm[128+block[i*8+j]];
}

/* Inter iDCT */
void ff_bfin_vp3_idct_add (uint8_t *dest, int line_size, DCTELEM *block)
{
    ff_bfin_vp3_idct (block);
    ff_bfin_add_pixels_clamped (block, dest, line_size);
}


