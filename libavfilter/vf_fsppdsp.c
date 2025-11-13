/*
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (C) 2005 Nikolaj Poroshin <porosh3@psu.ru>
 * Copyright (c) 2014 Arwa Arif <arwaarif1994@gmail.com>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdint.h>

#include "vf_fsppdsp.h"

#include "libavutil/common.h"
#include "libavutil/mathematics.h"
#include "libavutil/mem_internal.h"

#define DCTSIZE 8

#define FIX(x,s)  (int)((x) * (1 << s) + 0.5)

#define MULTIPLY16H(x,k)   (((x) * (k)) >> 16)
#define THRESHOLD(r,x,t)                         \
    if (((unsigned)((x) + t)) >= t * 2) r = (x); \
    else r = 0;
#define DESCALE(x,n)  (((x) + (1 << ((n) - 1))) >> n)

typedef int32_t int_simd16_t;

enum {
    FIX_0_382683433   = FIX(0.382683433, 14),
    FIX_0_541196100   = FIX(0.541196100, 14),
    FIX_0_707106781   = FIX(M_SQRT1_2  , 14),
    FIX_1_306562965   = FIX(1.306562965, 14),
    FIX_1_414213562_A = FIX(M_SQRT2    , 14),
    FIX_1_847759065   = FIX(1.847759065, 13),
    FIX_2_613125930   = FIX(-2.613125930, 13),
    FIX_1_414213562   = FIX(M_SQRT2    , 13),
    FIX_1_082392200   = FIX(1.082392200, 13),
};

DECLARE_ALIGNED(8, const uint8_t, ff_fspp_dither)[8][8] = {
    {  0,  48,  12,  60,   3,  51,  15,  63, },
    { 32,  16,  44,  28,  35,  19,  47,  31, },
    {  8,  56,   4,  52,  11,  59,   7,  55, },
    { 40,  24,  36,  20,  43,  27,  39,  23, },
    {  2,  50,  14,  62,   1,  49,  13,  61, },
    { 34,  18,  46,  30,  33,  17,  45,  29, },
    { 10,  58,   6,  54,   9,  57,   5,  53, },
    { 42,  26,  38,  22,  41,  25,  37,  21, },
};

//This func reads from 1 slice, 1 and clears 0 & 1
void ff_store_slice_c(uint8_t *restrict dst, int16_t *restrict src,
                      ptrdiff_t dst_stride, ptrdiff_t src_stride,
                      ptrdiff_t width, ptrdiff_t height, ptrdiff_t log2_scale)
{
#define STORE(pos)                                                             \
    temp = (src[x + pos] + (d[pos] >> log2_scale)) >> (6 - log2_scale);        \
    src[x + pos] = src[x + pos - 8 * src_stride] = 0;                          \
    temp = av_clip_uint8(temp);                                                \
    dst[x + pos] = temp;

    for (int y = 0; y < height; y++) {
        const uint8_t *d = ff_fspp_dither[y];
        for (int x = 0; x < width; x += 8) {
            int temp;
            STORE(0);
            STORE(1);
            STORE(2);
            STORE(3);
            STORE(4);
            STORE(5);
            STORE(6);
            STORE(7);
        }
        src += src_stride;
        dst += dst_stride;
    }
}

//This func reads from 2 slices, 0 & 2  and clears 2-nd
void ff_store_slice2_c(uint8_t *restrict dst, int16_t *restrict src,
                       ptrdiff_t dst_stride, ptrdiff_t src_stride,
                       ptrdiff_t width, ptrdiff_t height, ptrdiff_t log2_scale)
{
#define STORE2(pos)                                                                                       \
    temp = (src[x + pos] + src[x + pos + 16 * src_stride] + (d[pos] >> log2_scale)) >> (6 - log2_scale);  \
    src[x + pos + 16 * src_stride] = 0;                                                                   \
    temp = av_clip_uint8(temp);                                                                           \
    dst[x + pos] = temp;

    for (int y = 0; y < height; y++) {
        const uint8_t *d = ff_fspp_dither[y];
        for (int x = 0; x < width; x += 8) {
            int temp;
            STORE2(0);
            STORE2(1);
            STORE2(2);
            STORE2(3);
            STORE2(4);
            STORE2(5);
            STORE2(6);
            STORE2(7);
        }
        src += src_stride;
        dst += dst_stride;
    }
}

void ff_mul_thrmat_c(const int16_t *restrict thr_adr_noq, int16_t *restrict thr_adr, int q)
{
    for (int a = 0; a < 64; a++)
        thr_adr[a] = q * thr_adr_noq[a];
}

void ff_column_fidct_c(const int16_t *restrict thr_adr, const int16_t *restrict data,
                       int16_t *restrict output, int cnt)
{
    int_simd16_t tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
    int_simd16_t tmp10, tmp11, tmp12, tmp13;
    int_simd16_t z1,z2,z3,z4,z5, z10, z11, z12, z13;
    int_simd16_t d0, d1, d2, d3, d4, d5, d6, d7;

    int16_t *wsptr;

    wsptr = output;

    for (; cnt > 0; cnt -= 2) { //start positions
        const int16_t *threshold = thr_adr;//threshold_mtx
        for (int ctr = DCTSIZE; ctr > 0; ctr--) {
            // Process columns from input, add to output.
            tmp0 = data[DCTSIZE * 0] + data[DCTSIZE * 7];
            tmp7 = data[DCTSIZE * 0] - data[DCTSIZE * 7];

            tmp1 = data[DCTSIZE * 1] + data[DCTSIZE * 6];
            tmp6 = data[DCTSIZE * 1] - data[DCTSIZE * 6];

            tmp2 = data[DCTSIZE * 2] + data[DCTSIZE * 5];
            tmp5 = data[DCTSIZE * 2] - data[DCTSIZE * 5];

            tmp3 = data[DCTSIZE * 3] + data[DCTSIZE * 4];
            tmp4 = data[DCTSIZE * 3] - data[DCTSIZE * 4];

            // Even part of FDCT

            tmp10 = tmp0 + tmp3;
            tmp13 = tmp0 - tmp3;
            tmp11 = tmp1 + tmp2;
            tmp12 = tmp1 - tmp2;

            d0 = tmp10 + tmp11;
            d4 = tmp10 - tmp11;

            z1 = MULTIPLY16H(tmp12 + tmp13, FIX_0_707106781 << 2);
            d2 = tmp13 + z1;
            d6 = tmp13 - z1;

            // Even part of IDCT

            THRESHOLD(tmp0, d0, threshold[0 * 8]);
            THRESHOLD(tmp1, d2, threshold[2 * 8]);
            THRESHOLD(tmp2, d4, threshold[4 * 8]);
            THRESHOLD(tmp3, d6, threshold[6 * 8]);
            tmp0 += 2;
            tmp10 = (tmp0 + tmp2) >> 2;
            tmp11 = (tmp0 - tmp2) >> 2;

            tmp13 = (tmp1 + tmp3) >>2; //+2 !  (psnr decides)
            tmp12 = MULTIPLY16H((tmp1 - tmp3), FIX_1_414213562_A) - tmp13; //<<2

            tmp0 = tmp10 + tmp13; //->temps
            tmp3 = tmp10 - tmp13; //->temps
            tmp1 = tmp11 + tmp12; //->temps
            tmp2 = tmp11 - tmp12; //->temps

            // Odd part of FDCT

            tmp10 = tmp4 + tmp5;
            tmp11 = tmp5 + tmp6;
            tmp12 = tmp6 + tmp7;

            z5 = MULTIPLY16H(tmp10 - tmp12, FIX_0_382683433 << 2);
            z2 = MULTIPLY16H(tmp10, FIX_0_541196100 << 2) + z5;
            z4 = MULTIPLY16H(tmp12, FIX_1_306562965 << 2) + z5;
            z3 = MULTIPLY16H(tmp11, FIX_0_707106781 << 2);

            z11 = tmp7 + z3;
            z13 = tmp7 - z3;

            d5 = z13 + z2;
            d3 = z13 - z2;
            d1 = z11 + z4;
            d7 = z11 - z4;

            // Odd part of IDCT

            THRESHOLD(tmp4, d1, threshold[1 * 8]);
            THRESHOLD(tmp5, d3, threshold[3 * 8]);
            THRESHOLD(tmp6, d5, threshold[5 * 8]);
            THRESHOLD(tmp7, d7, threshold[7 * 8]);

            //Simd version uses here a shortcut for the tmp5,tmp6,tmp7 == 0
            z13 = tmp6 + tmp5;
            z10 = (tmp6 - tmp5) * 2;
            z11 = tmp4 + tmp7;
            z12 = (tmp4 - tmp7) * 2;

            tmp7  = (z11 + z13) >> 2; //+2 !
            tmp11 = MULTIPLY16H(z11 - z13, FIX_1_414213562 << 1);
            z5    = MULTIPLY16H(z10 + z12, FIX_1_847759065);
            tmp10 = MULTIPLY16H(z12,       FIX_1_082392200) - z5;
            tmp12 = MULTIPLY16H(z10,       FIX_2_613125930) + z5; // - !!

            tmp6 = tmp12 - tmp7;
            tmp5 = tmp11 - tmp6;
            tmp4 = tmp10 + tmp5;

            wsptr[DCTSIZE * 0] +=  (tmp0 + tmp7);
            wsptr[DCTSIZE * 1] +=  (tmp1 + tmp6);
            wsptr[DCTSIZE * 2] +=  (tmp2 + tmp5);
            wsptr[DCTSIZE * 3] +=  (tmp3 - tmp4);
            wsptr[DCTSIZE * 4] +=  (tmp3 + tmp4);
            wsptr[DCTSIZE * 5] +=  (tmp2 - tmp5);
            wsptr[DCTSIZE * 6]  =  (tmp1 - tmp6);
            wsptr[DCTSIZE * 7]  =  (tmp0 - tmp7);
            //
            data++; //next column
            wsptr++;
            threshold++;
        }
        data  += 8; //skip each second start pos
        wsptr   += 8;
    }
}

void ff_row_idct_c(const int16_t *restrict wsptr, int16_t *restrict output_adr,
                   ptrdiff_t output_stride, int cnt)
{
    int_simd16_t tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
    int_simd16_t tmp10, tmp11, tmp12, tmp13;
    int_simd16_t z5, z10, z11, z12, z13;
    int16_t *outptr;

    cnt *= 4;
    outptr = output_adr;
    for (; cnt > 0; cnt--) {
        // Even part
        //Simd version reads 4x4 block and transposes it
        tmp10 = wsptr[2] +  wsptr[3];
        tmp11 = wsptr[2] -  wsptr[3];

        tmp13 = wsptr[0] +  wsptr[1];
        tmp12 = (MULTIPLY16H(wsptr[0] - wsptr[1], FIX_1_414213562_A) * 4) - tmp13;//this shift order to avoid overflow

        tmp0 = tmp10 + tmp13; //->temps
        tmp3 = tmp10 - tmp13; //->temps
        tmp1 = tmp11 + tmp12;
        tmp2 = tmp11 - tmp12;

        // Odd part
        //Also transpose, with previous:
        // ---- ----      ||||
        // ---- ---- idct ||||
        // ---- ---- ---> ||||
        // ---- ----      ||||
        z13 = wsptr[4] + wsptr[5];
        z10 = wsptr[4] - wsptr[5];
        z11 = wsptr[6] + wsptr[7];
        z12 = wsptr[6] - wsptr[7];

        tmp7 = z11 + z13;
        tmp11 = MULTIPLY16H(z11 - z13, FIX_1_414213562);

        z5 =    MULTIPLY16H(z10 + z12, FIX_1_847759065);
        tmp10 = MULTIPLY16H(z12,       FIX_1_082392200) - z5;
        tmp12 = MULTIPLY16H(z10,       FIX_2_613125930) + z5; // - FIX_

        tmp6 = tmp12 * 8 - tmp7;
        tmp5 = tmp11 * 8 - tmp6;
        tmp4 = tmp10 * 8 + tmp5;

        // Final output stage: descale and write column
        outptr[0 * output_stride] += DESCALE(tmp0 + tmp7, 3);
        outptr[1 * output_stride] += DESCALE(tmp1 + tmp6, 3);
        outptr[2 * output_stride] += DESCALE(tmp2 + tmp5, 3);
        outptr[3 * output_stride] += DESCALE(tmp3 - tmp4, 3);
        outptr[4 * output_stride] += DESCALE(tmp3 + tmp4, 3);
        outptr[5 * output_stride] += DESCALE(tmp2 - tmp5, 3);
        outptr[6 * output_stride] += DESCALE(tmp1 - tmp6, 3); //no += ?
        outptr[7 * output_stride] += DESCALE(tmp0 - tmp7, 3); //no += ?
        outptr++;

        wsptr += DCTSIZE;       // advance pointer to next row
    }
}

void ff_row_fdct_c(int16_t *restrict data, const uint8_t *restrict pixels,
                   ptrdiff_t line_size, int cnt)
{
    int_simd16_t tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
    int_simd16_t tmp10, tmp11, tmp12, tmp13;
    int_simd16_t z1, z2, z3, z4, z5, z11, z13;
    int16_t *dataptr;

    cnt *= 4;
    // Pass 1: process rows.

    dataptr = data;
    for (; cnt > 0; cnt--) {
        tmp0 = pixels[line_size * 0] + pixels[line_size * 7];
        tmp7 = pixels[line_size * 0] - pixels[line_size * 7];
        tmp1 = pixels[line_size * 1] + pixels[line_size * 6];
        tmp6 = pixels[line_size * 1] - pixels[line_size * 6];
        tmp2 = pixels[line_size * 2] + pixels[line_size * 5];
        tmp5 = pixels[line_size * 2] - pixels[line_size * 5];
        tmp3 = pixels[line_size * 3] + pixels[line_size * 4];
        tmp4 = pixels[line_size * 3] - pixels[line_size * 4];

        // Even part

        tmp10 = tmp0 + tmp3;
        tmp13 = tmp0 - tmp3;
        tmp11 = tmp1 + tmp2;
        tmp12 = tmp1 - tmp2;
        //Even columns are written first, this leads to different order of columns
        //in column_fidct(), but they are processed independently, so all ok.
        //Later in the row_idct() columns are read in the same order.
        dataptr[2] = tmp10 + tmp11;
        dataptr[3] = tmp10 - tmp11;

        z1 = MULTIPLY16H(tmp12 + tmp13, FIX_0_707106781 << 2);
        dataptr[0] = tmp13 + z1;
        dataptr[1] = tmp13 - z1;

        // Odd part

        tmp10 = tmp4 + tmp5;
        tmp11 = tmp5 + tmp6;
        tmp12 = tmp6 + tmp7;

        z5 = MULTIPLY16H(tmp10 - tmp12, FIX_0_382683433 << 2);
        z2 = MULTIPLY16H(tmp10,         FIX_0_541196100 << 2) + z5;
        z4 = MULTIPLY16H(tmp12,         FIX_1_306562965 << 2) + z5;
        z3 = MULTIPLY16H(tmp11,         FIX_0_707106781 << 2);

        z11 = tmp7 + z3;
        z13 = tmp7 - z3;

        dataptr[4] = z13 + z2;
        dataptr[5] = z13 - z2;
        dataptr[6] = z11 + z4;
        dataptr[7] = z11 - z4;

        pixels++;               // advance pointer to next column
        dataptr += DCTSIZE;
    }
}
