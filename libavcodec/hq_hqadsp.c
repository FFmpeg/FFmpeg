/*
 * Canopus HQ/HQA decoder
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

#include "libavutil/attributes.h"
#include "libavutil/common.h"

#include "hq_hqadsp.h"

#define FIX_1_082 17734
#define FIX_1_847 30274
#define FIX_1_414 23170
#define FIX_2_613 21407 // divided by two to fit the range

#define IDCTMUL(a, b) ((int)((a) * (unsigned)(b)) >> 16)

static inline void idct_row(int16_t *blk)
{
    int tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7, tmp8, tmp9, tmpA;
    int tmpB, tmpC, tmpD, tmpE, tmpF, tmp10, tmp11, tmp12, tmp13, tmp14;

    tmp0 = blk[5] - blk[3];
    tmp1 = blk[5] + blk[3];
    tmp2 = blk[1] - blk[7];
    tmp3 = blk[1] + blk[7];
    tmp4 = tmp3 - tmp1;
    tmp5 = IDCTMUL(tmp0 + tmp2, FIX_1_847);
    tmp6 = IDCTMUL(tmp2,        FIX_1_082) - tmp5;
    tmp7 = tmp5 - IDCTMUL(tmp0, FIX_2_613) * 2;
    tmp8 = tmp3 + tmp1;
    tmp9 = tmp7 * 4 - tmp8;
    tmpA = IDCTMUL(tmp4, FIX_1_414) * 4 - tmp9;
    tmpB = tmp6 * 4 + tmpA;
    tmpC = blk[2] + blk[6];
    tmpD = blk[2] - blk[6];
    tmpE = blk[0] - blk[4];
    tmpF = blk[0] + blk[4];

    tmp10 = IDCTMUL(tmpD, FIX_1_414) * 4 - tmpC;
    tmp11 = tmpE - tmp10;
    tmp12 = tmpF - tmpC;
    tmp13 = tmpE + tmp10;
    tmp14 = tmpF + tmpC;

    blk[0] = tmp14 + tmp8;
    blk[1] = tmp13 + tmp9;
    blk[2] = tmp11 + tmpA;
    blk[3] = tmp12 - tmpB;
    blk[4] = tmp12 + tmpB;
    blk[5] = tmp11 - tmpA;
    blk[6] = tmp13 - tmp9;
    blk[7] = tmp14 - tmp8;
}

static inline void idct_col(int16_t *blk)
{
    int tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7, tmp8, tmp9, tmpA;
    int tmpB, tmpC, tmpD, tmpE, tmpF, tmp10, tmp11, tmp12, tmp13, tmp14;

    tmp0 = blk[5 * 8] - blk[3 * 8];
    tmp1 = blk[5 * 8] + blk[3 * 8];
    tmp2 = blk[1 * 8] * 2 - (blk[7 * 8] >> 2);
    tmp3 = blk[1 * 8] * 2 + (blk[7 * 8] >> 2);
    tmp4 = tmp3 - tmp1;
    tmp5 = IDCTMUL(tmp0 + tmp2, FIX_1_847);
    tmp6 = IDCTMUL(tmp2,        FIX_1_082) - tmp5;
    tmp7 = tmp5 - IDCTMUL(tmp0, FIX_2_613) * 2;
    tmp8 = (tmp3 + tmp1) >> 1;
    tmp9 = tmp7 * 2 - tmp8;
    tmpA = IDCTMUL(tmp4, FIX_1_414) * 2 - tmp9;
    tmpB = tmp6 * 2 + tmpA;
    tmpC =  blk[2 * 8] + (blk[6 * 8] >> 1) >> 1;
    tmpD =  blk[2 * 8] - (blk[6 * 8] >> 1);
    tmpE = (blk[0 * 8] >> 1) - (blk[4 * 8] >> 1) + 0x2020;
    tmpF = (blk[0 * 8] >> 1) + (blk[4 * 8] >> 1) + 0x2020;

    tmp10 = IDCTMUL(tmpD, FIX_1_414) * 2 - tmpC;
    tmp11 = tmpE - tmp10;
    tmp12 = tmpF - tmpC;
    tmp13 = tmpE + tmp10;
    tmp14 = tmpF + tmpC;

    blk[0 * 8] = (tmp14 + tmp8) >> 6;
    blk[1 * 8] = (tmp13 + tmp9) >> 6;
    blk[2 * 8] = (tmp11 + tmpA) >> 6;
    blk[3 * 8] = (tmp12 - tmpB) >> 6;
    blk[4 * 8] = (tmp12 + tmpB) >> 6;
    blk[5 * 8] = (tmp11 - tmpA) >> 6;
    blk[6 * 8] = (tmp13 - tmp9) >> 6;
    blk[7 * 8] = (tmp14 - tmp8) >> 6;
}

static void hq_idct_put(uint8_t *dst, int stride, int16_t *block)
{
    int i, j;

    for (i = 0; i < 8; i++)
        idct_row(block + i * 8);
    for (i = 0; i < 8; i++)
        idct_col(block + i);

    for (i = 0; i < 8; i++) {
        for (j = 0; j < 8; j++)
            dst[j] = av_clip_uint8(block[j + i * 8]);
        dst += stride;
    }
}

av_cold void ff_hqdsp_init(HQDSPContext *c)
{
    c->idct_put = hq_idct_put;
}
