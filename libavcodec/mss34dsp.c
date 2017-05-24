/*
 * Common stuff for some Microsoft Screen codecs
 * Copyright (C) 2012 Konstantin Shishkov
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

#include <stdint.h>
#include "libavutil/common.h"
#include "mss34dsp.h"

static const uint8_t luma_quant[64] = {
    16,  11,  10,  16,  24,  40,  51,  61,
    12,  12,  14,  19,  26,  58,  60,  55,
    14,  13,  16,  24,  40,  57,  69,  56,
    14,  17,  22,  29,  51,  87,  80,  62,
    18,  22,  37,  56,  68, 109, 103,  77,
    24,  35,  55,  64,  81, 104, 113,  92,
    49,  64,  78,  87, 103, 121, 120, 101,
    72,  92,  95,  98, 112, 100, 103,  99
};

static const uint8_t chroma_quant[64] = {
    17, 18, 24, 47, 99, 99, 99, 99,
    18, 21, 26, 66, 99, 99, 99, 99,
    24, 26, 56, 99, 99, 99, 99, 99,
    47, 66, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99
};

void ff_mss34_gen_quant_mat(uint16_t *qmat, int quality, int luma)
{
    int i;
    const uint8_t *qsrc = luma ? luma_quant : chroma_quant;

    if (quality >= 50) {
        int scale = 200 - 2 * quality;

        for (i = 0; i < 64; i++)
            qmat[i] = (qsrc[i] * scale + 50) / 100;
    } else {
        for (i = 0; i < 64; i++)
            qmat[i] = (5000 * qsrc[i] / quality + 50) / 100;
    }
}

#define DCT_TEMPLATE(blk, step, SOP, shift)                         \
    const unsigned t0 =-39409U * blk[7 * step] - 58980U * blk[1 * step]; \
    const unsigned t1 = 39410U * blk[1 * step] - 58980U * blk[7 * step]; \
    const unsigned t2 =-33410U * blk[5 * step] -167963U * blk[3 * step]; \
    const unsigned t3 = 33410U * blk[3 * step] -167963U * blk[5 * step]; \
    const unsigned t4 =          blk[3 * step] +          blk[7 * step]; \
    const unsigned t5 =          blk[1 * step] +          blk[5 * step]; \
    const unsigned t6 = 77062U * t4            + 51491U * t5;            \
    const unsigned t7 = 77062U * t5            - 51491U * t4;            \
    const unsigned t8 = 35470U * blk[2 * step] - 85623U * blk[6 * step]; \
    const unsigned t9 = 35470U * blk[6 * step] + 85623U * blk[2 * step]; \
    const unsigned tA = SOP(blk[0 * step] - blk[4 * step]);              \
    const unsigned tB = SOP(blk[0 * step] + blk[4 * step]);              \
                                                                    \
    blk[0 * step] = (int)(  t1 + t6  + t9 + tB) >> shift;                \
    blk[1 * step] = (int)(  t3 + t7  + t8 + tA) >> shift;                \
    blk[2 * step] = (int)(  t2 + t6  - t8 + tA) >> shift;                \
    blk[3 * step] = (int)(  t0 + t7  - t9 + tB) >> shift;                \
    blk[4 * step] = (int)(-(t0 + t7) - t9 + tB) >> shift;                \
    blk[5 * step] = (int)(-(t2 + t6) - t8 + tA) >> shift;                \
    blk[6 * step] = (int)(-(t3 + t7) + t8 + tA) >> shift;                \
    blk[7 * step] = (int)(-(t1 + t6) + t9 + tB) >> shift;                \

#define SOP_ROW(a) (((a) * (1U << 16)) + 0x2000)
#define SOP_COL(a) (((a) + 32) * (1U << 16))

void ff_mss34_dct_put(uint8_t *dst, ptrdiff_t stride, int *block)
{
    int i, j;
    int *ptr;

    ptr = block;
    for (i = 0; i < 8; i++) {
        DCT_TEMPLATE(ptr, 1, SOP_ROW, 13);
        ptr += 8;
    }

    ptr = block;
    for (i = 0; i < 8; i++) {
        DCT_TEMPLATE(ptr, 8, SOP_COL, 22);
        ptr++;
    }

    ptr = block;
    for (j = 0; j < 8; j++) {
        for (i = 0; i < 8; i++)
            dst[i] = av_clip_uint8(ptr[i] + 128);
        dst += stride;
        ptr += 8;
    }
}
