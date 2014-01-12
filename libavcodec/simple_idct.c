/*
 * Simple IDCT
 *
 * Copyright (c) 2001 Michael Niedermayer <michaelni@gmx.at>
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
 * simpleidct in C.
 */

#include "libavutil/intreadwrite.h"
#include "avcodec.h"
#include "mathops.h"
#include "simple_idct.h"

#define BIT_DEPTH 8
#include "simple_idct_template.c"
#undef BIT_DEPTH

#define BIT_DEPTH 10
#include "simple_idct_template.c"
#undef BIT_DEPTH

#define BIT_DEPTH 12
#include "simple_idct_template.c"
#undef BIT_DEPTH

/* 2x4x8 idct */

#define CN_SHIFT 12
#define C_FIX(x) ((int)((x) * (1 << CN_SHIFT) + 0.5))
#define C1 C_FIX(0.6532814824)
#define C2 C_FIX(0.2705980501)

/* row idct is multiple by 16 * sqrt(2.0), col idct4 is normalized,
   and the butterfly must be multiplied by 0.5 * sqrt(2.0) */
#define C_SHIFT (4+1+12)

static inline void idct4col_put(uint8_t *dest, int line_size, const int16_t *col)
{
    int c0, c1, c2, c3, a0, a1, a2, a3;

    a0 = col[8*0];
    a1 = col[8*2];
    a2 = col[8*4];
    a3 = col[8*6];
    c0 = ((a0 + a2) << (CN_SHIFT - 1)) + (1 << (C_SHIFT - 1));
    c2 = ((a0 - a2) << (CN_SHIFT - 1)) + (1 << (C_SHIFT - 1));
    c1 = a1 * C1 + a3 * C2;
    c3 = a1 * C2 - a3 * C1;
    dest[0] = av_clip_uint8((c0 + c1) >> C_SHIFT);
    dest += line_size;
    dest[0] = av_clip_uint8((c2 + c3) >> C_SHIFT);
    dest += line_size;
    dest[0] = av_clip_uint8((c2 - c3) >> C_SHIFT);
    dest += line_size;
    dest[0] = av_clip_uint8((c0 - c1) >> C_SHIFT);
}

#define BF(k) \
{\
    int a0, a1;\
    a0 = ptr[k];\
    a1 = ptr[8 + k];\
    ptr[k] = a0 + a1;\
    ptr[8 + k] = a0 - a1;\
}

/* only used by DV codec. The input must be interlaced. 128 is added
   to the pixels before clamping to avoid systematic error
   (1024*sqrt(2)) offset would be needed otherwise. */
/* XXX: I think a 1.0/sqrt(2) normalization should be needed to
   compensate the extra butterfly stage - I don't have the full DV
   specification */
void ff_simple_idct248_put(uint8_t *dest, int line_size, int16_t *block)
{
    int i;
    int16_t *ptr;

    /* butterfly */
    ptr = block;
    for(i=0;i<4;i++) {
        BF(0);
        BF(1);
        BF(2);
        BF(3);
        BF(4);
        BF(5);
        BF(6);
        BF(7);
        ptr += 2 * 8;
    }

    /* IDCT8 on each line */
    for(i=0; i<8; i++) {
        idctRowCondDC_8(block + i*8, 0);
    }

    /* IDCT4 and store */
    for(i=0;i<8;i++) {
        idct4col_put(dest + i, 2 * line_size, block + i);
        idct4col_put(dest + line_size + i, 2 * line_size, block + 8 + i);
    }
}

/* 8x4 & 4x8 WMV2 IDCT */
#undef CN_SHIFT
#undef C_SHIFT
#undef C_FIX
#undef C1
#undef C2
#define CN_SHIFT 12
#define C_FIX(x) ((int)((x) * 1.414213562 * (1 << CN_SHIFT) + 0.5))
#define C1 C_FIX(0.6532814824)
#define C2 C_FIX(0.2705980501)
#define C3 C_FIX(0.5)
#define C_SHIFT (4+1+12)
static inline void idct4col_add(uint8_t *dest, int line_size, const int16_t *col)
{
    int c0, c1, c2, c3, a0, a1, a2, a3;

    a0 = col[8*0];
    a1 = col[8*1];
    a2 = col[8*2];
    a3 = col[8*3];
    c0 = (a0 + a2)*C3 + (1 << (C_SHIFT - 1));
    c2 = (a0 - a2)*C3 + (1 << (C_SHIFT - 1));
    c1 = a1 * C1 + a3 * C2;
    c3 = a1 * C2 - a3 * C1;
    dest[0] = av_clip_uint8(dest[0] + ((c0 + c1) >> C_SHIFT));
    dest += line_size;
    dest[0] = av_clip_uint8(dest[0] + ((c2 + c3) >> C_SHIFT));
    dest += line_size;
    dest[0] = av_clip_uint8(dest[0] + ((c2 - c3) >> C_SHIFT));
    dest += line_size;
    dest[0] = av_clip_uint8(dest[0] + ((c0 - c1) >> C_SHIFT));
}

#define RN_SHIFT 15
#define R_FIX(x) ((int)((x) * 1.414213562 * (1 << RN_SHIFT) + 0.5))
#define R1 R_FIX(0.6532814824)
#define R2 R_FIX(0.2705980501)
#define R3 R_FIX(0.5)
#define R_SHIFT 11
static inline void idct4row(int16_t *row)
{
    int c0, c1, c2, c3, a0, a1, a2, a3;

    a0 = row[0];
    a1 = row[1];
    a2 = row[2];
    a3 = row[3];
    c0 = (a0 + a2)*R3 + (1 << (R_SHIFT - 1));
    c2 = (a0 - a2)*R3 + (1 << (R_SHIFT - 1));
    c1 = a1 * R1 + a3 * R2;
    c3 = a1 * R2 - a3 * R1;
    row[0]= (c0 + c1) >> R_SHIFT;
    row[1]= (c2 + c3) >> R_SHIFT;
    row[2]= (c2 - c3) >> R_SHIFT;
    row[3]= (c0 - c1) >> R_SHIFT;
}

void ff_simple_idct84_add(uint8_t *dest, int line_size, int16_t *block)
{
    int i;

    /* IDCT8 on each line */
    for(i=0; i<4; i++) {
        idctRowCondDC_8(block + i*8, 0);
    }

    /* IDCT4 and store */
    for(i=0;i<8;i++) {
        idct4col_add(dest + i, line_size, block + i);
    }
}

void ff_simple_idct48_add(uint8_t *dest, int line_size, int16_t *block)
{
    int i;

    /* IDCT4 on each line */
    for(i=0; i<8; i++) {
        idct4row(block + i*8);
    }

    /* IDCT8 and store */
    for(i=0; i<4; i++){
        idctSparseColAdd_8(dest + i, line_size, block + i);
    }
}

void ff_simple_idct44_add(uint8_t *dest, int line_size, int16_t *block)
{
    int i;

    /* IDCT4 on each line */
    for(i=0; i<4; i++) {
        idct4row(block + i*8);
    }

    /* IDCT4 and store */
    for(i=0; i<4; i++){
        idct4col_add(dest + i, line_size, block + i);
    }
}

void ff_prores_idct(int16_t *block, const int16_t *qmat)
{
    int i;

    for (i = 0; i < 64; i++)
        block[i] *= qmat[i];

    for (i = 0; i < 8; i++)
        idctRowCondDC_10(block + i*8, 2);

    for (i = 0; i < 8; i++) {
        block[i] += 8192;
        idctSparseCol_10(block + i);
    }
}
