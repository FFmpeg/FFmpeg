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

/*
  based upon some outcommented c code from mpeg2dec (idct_mmx.c
  written by Aaron Holtzman <aholtzma@ess.engr.uvic.ca>)
 */

#include "bit_depth_template.c"

#undef W1
#undef W2
#undef W3
#undef W4
#undef W5
#undef W6
#undef W7
#undef ROW_SHIFT
#undef COL_SHIFT
#undef DC_SHIFT
#undef MUL
#undef MAC

#if BIT_DEPTH == 8

#define W1  22725  //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define W2  21407  //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define W3  19266  //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define W4  16383  //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define W5  12873  //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define W6  8867   //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define W7  4520   //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5

#define ROW_SHIFT 11
#define COL_SHIFT 20
#define DC_SHIFT 3

#define MUL(a, b)    MUL16(a, b)
#define MAC(a, b, c) MAC16(a, b, c)

#elif BIT_DEPTH == 10 || BIT_DEPTH == 12

#if BIT_DEPTH == 10
#define W1 90901
#define W2 85627
#define W3 77062
#define W4 65535
#define W5 51491
#define W6 35468
#define W7 18081

#define ROW_SHIFT 15
#define COL_SHIFT 20
#define DC_SHIFT 1
#else
#define W1 45451
#define W2 42813
#define W3 38531
#define W4 32767
#define W5 25746
#define W6 17734
#define W7 9041

#define ROW_SHIFT 16
#define COL_SHIFT 17
#define DC_SHIFT -1
#endif

#define MUL(a, b)    ((a) * (b))
#define MAC(a, b, c) ((a) += (b) * (c))

#else

#error "Unsupported bitdepth"

#endif

static inline void FUNC(idctRowCondDC)(int16_t *row, int extra_shift)
{
    int a0, a1, a2, a3, b0, b1, b2, b3;

#if HAVE_FAST_64BIT
#define ROW0_MASK (0xffffLL << 48 * HAVE_BIGENDIAN)
    if (((((uint64_t *)row)[0] & ~ROW0_MASK) | ((uint64_t *)row)[1]) == 0) {
        uint64_t temp;
        if (DC_SHIFT - extra_shift > 0) {
            temp = (row[0] << (DC_SHIFT - extra_shift)) & 0xffff;
        } else {
            temp = (row[0] >> (extra_shift - DC_SHIFT)) & 0xffff;
        }
        temp += temp << 16;
        temp += temp << 32;
        ((uint64_t *)row)[0] = temp;
        ((uint64_t *)row)[1] = temp;
        return;
    }
#else
    if (!(((uint32_t*)row)[1] |
          ((uint32_t*)row)[2] |
          ((uint32_t*)row)[3] |
          row[1])) {
        uint32_t temp;
        if (DC_SHIFT - extra_shift > 0) {
            temp = (row[0] << (DC_SHIFT - extra_shift)) & 0xffff;
        } else {
            temp = (row[0] >> (extra_shift - DC_SHIFT)) & 0xffff;
        }
        temp += temp << 16;
        ((uint32_t*)row)[0]=((uint32_t*)row)[1] =
            ((uint32_t*)row)[2]=((uint32_t*)row)[3] = temp;
        return;
    }
#endif

    a0 = (W4 * row[0]) + (1 << (ROW_SHIFT - 1));
    a1 = a0;
    a2 = a0;
    a3 = a0;

    a0 += W2 * row[2];
    a1 += W6 * row[2];
    a2 -= W6 * row[2];
    a3 -= W2 * row[2];

    b0 = MUL(W1, row[1]);
    MAC(b0, W3, row[3]);
    b1 = MUL(W3, row[1]);
    MAC(b1, -W7, row[3]);
    b2 = MUL(W5, row[1]);
    MAC(b2, -W1, row[3]);
    b3 = MUL(W7, row[1]);
    MAC(b3, -W5, row[3]);

    if (AV_RN64A(row + 4)) {
        a0 +=   W4*row[4] + W6*row[6];
        a1 += - W4*row[4] - W2*row[6];
        a2 += - W4*row[4] + W2*row[6];
        a3 +=   W4*row[4] - W6*row[6];

        MAC(b0,  W5, row[5]);
        MAC(b0,  W7, row[7]);

        MAC(b1, -W1, row[5]);
        MAC(b1, -W5, row[7]);

        MAC(b2,  W7, row[5]);
        MAC(b2,  W3, row[7]);

        MAC(b3,  W3, row[5]);
        MAC(b3, -W1, row[7]);
    }

    row[0] = (a0 + b0) >> (ROW_SHIFT + extra_shift);
    row[7] = (a0 - b0) >> (ROW_SHIFT + extra_shift);
    row[1] = (a1 + b1) >> (ROW_SHIFT + extra_shift);
    row[6] = (a1 - b1) >> (ROW_SHIFT + extra_shift);
    row[2] = (a2 + b2) >> (ROW_SHIFT + extra_shift);
    row[5] = (a2 - b2) >> (ROW_SHIFT + extra_shift);
    row[3] = (a3 + b3) >> (ROW_SHIFT + extra_shift);
    row[4] = (a3 - b3) >> (ROW_SHIFT + extra_shift);
}

#define IDCT_COLS do {                                  \
        a0 = W4 * (col[8*0] + ((1<<(COL_SHIFT-1))/W4)); \
        a1 = a0;                                        \
        a2 = a0;                                        \
        a3 = a0;                                        \
                                                        \
        a0 +=  W2*col[8*2];                             \
        a1 +=  W6*col[8*2];                             \
        a2 += -W6*col[8*2];                             \
        a3 += -W2*col[8*2];                             \
                                                        \
        b0 = MUL(W1, col[8*1]);                         \
        b1 = MUL(W3, col[8*1]);                         \
        b2 = MUL(W5, col[8*1]);                         \
        b3 = MUL(W7, col[8*1]);                         \
                                                        \
        MAC(b0,  W3, col[8*3]);                         \
        MAC(b1, -W7, col[8*3]);                         \
        MAC(b2, -W1, col[8*3]);                         \
        MAC(b3, -W5, col[8*3]);                         \
                                                        \
        if (col[8*4]) {                                 \
            a0 +=  W4*col[8*4];                         \
            a1 += -W4*col[8*4];                         \
            a2 += -W4*col[8*4];                         \
            a3 +=  W4*col[8*4];                         \
        }                                               \
                                                        \
        if (col[8*5]) {                                 \
            MAC(b0,  W5, col[8*5]);                     \
            MAC(b1, -W1, col[8*5]);                     \
            MAC(b2,  W7, col[8*5]);                     \
            MAC(b3,  W3, col[8*5]);                     \
        }                                               \
                                                        \
        if (col[8*6]) {                                 \
            a0 +=  W6*col[8*6];                         \
            a1 += -W2*col[8*6];                         \
            a2 +=  W2*col[8*6];                         \
            a3 += -W6*col[8*6];                         \
        }                                               \
                                                        \
        if (col[8*7]) {                                 \
            MAC(b0,  W7, col[8*7]);                     \
            MAC(b1, -W5, col[8*7]);                     \
            MAC(b2,  W3, col[8*7]);                     \
            MAC(b3, -W1, col[8*7]);                     \
        }                                               \
    } while (0)

static inline void FUNC(idctSparseColPut)(pixel *dest, int line_size,
                                          int16_t *col)
{
    int a0, a1, a2, a3, b0, b1, b2, b3;

    IDCT_COLS;

    dest[0] = av_clip_pixel((a0 + b0) >> COL_SHIFT);
    dest += line_size;
    dest[0] = av_clip_pixel((a1 + b1) >> COL_SHIFT);
    dest += line_size;
    dest[0] = av_clip_pixel((a2 + b2) >> COL_SHIFT);
    dest += line_size;
    dest[0] = av_clip_pixel((a3 + b3) >> COL_SHIFT);
    dest += line_size;
    dest[0] = av_clip_pixel((a3 - b3) >> COL_SHIFT);
    dest += line_size;
    dest[0] = av_clip_pixel((a2 - b2) >> COL_SHIFT);
    dest += line_size;
    dest[0] = av_clip_pixel((a1 - b1) >> COL_SHIFT);
    dest += line_size;
    dest[0] = av_clip_pixel((a0 - b0) >> COL_SHIFT);
}

static inline void FUNC(idctSparseColAdd)(pixel *dest, int line_size,
                                          int16_t *col)
{
    int a0, a1, a2, a3, b0, b1, b2, b3;

    IDCT_COLS;

    dest[0] = av_clip_pixel(dest[0] + ((a0 + b0) >> COL_SHIFT));
    dest += line_size;
    dest[0] = av_clip_pixel(dest[0] + ((a1 + b1) >> COL_SHIFT));
    dest += line_size;
    dest[0] = av_clip_pixel(dest[0] + ((a2 + b2) >> COL_SHIFT));
    dest += line_size;
    dest[0] = av_clip_pixel(dest[0] + ((a3 + b3) >> COL_SHIFT));
    dest += line_size;
    dest[0] = av_clip_pixel(dest[0] + ((a3 - b3) >> COL_SHIFT));
    dest += line_size;
    dest[0] = av_clip_pixel(dest[0] + ((a2 - b2) >> COL_SHIFT));
    dest += line_size;
    dest[0] = av_clip_pixel(dest[0] + ((a1 - b1) >> COL_SHIFT));
    dest += line_size;
    dest[0] = av_clip_pixel(dest[0] + ((a0 - b0) >> COL_SHIFT));
}

static inline void FUNC(idctSparseCol)(int16_t *col)
{
    int a0, a1, a2, a3, b0, b1, b2, b3;

    IDCT_COLS;

    col[0 ] = ((a0 + b0) >> COL_SHIFT);
    col[8 ] = ((a1 + b1) >> COL_SHIFT);
    col[16] = ((a2 + b2) >> COL_SHIFT);
    col[24] = ((a3 + b3) >> COL_SHIFT);
    col[32] = ((a3 - b3) >> COL_SHIFT);
    col[40] = ((a2 - b2) >> COL_SHIFT);
    col[48] = ((a1 - b1) >> COL_SHIFT);
    col[56] = ((a0 - b0) >> COL_SHIFT);
}

void FUNC(ff_simple_idct_put)(uint8_t *dest_, int line_size, int16_t *block)
{
    pixel *dest = (pixel *)dest_;
    int i;

    line_size /= sizeof(pixel);

    for (i = 0; i < 8; i++)
        FUNC(idctRowCondDC)(block + i*8, 0);

    for (i = 0; i < 8; i++)
        FUNC(idctSparseColPut)(dest + i, line_size, block + i);
}

void FUNC(ff_simple_idct_add)(uint8_t *dest_, int line_size, int16_t *block)
{
    pixel *dest = (pixel *)dest_;
    int i;

    line_size /= sizeof(pixel);

    for (i = 0; i < 8; i++)
        FUNC(idctRowCondDC)(block + i*8, 0);

    for (i = 0; i < 8; i++)
        FUNC(idctSparseColAdd)(dest + i, line_size, block + i);
}

void FUNC(ff_simple_idct)(int16_t *block)
{
    int i;

    for (i = 0; i < 8; i++)
        FUNC(idctRowCondDC)(block + i*8, 0);

    for (i = 0; i < 8; i++)
        FUNC(idctSparseCol)(block + i);
}
