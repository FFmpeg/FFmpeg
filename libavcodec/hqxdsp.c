/*
 * HQX DSP routines
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

#include "hqxdsp.h"

static inline void idct_col(int16_t *blk, const uint8_t *quant)
{
    int t0, t1, t2, t3, t4, t5, t6, t7, t8, t9, tA, tB, tC, tD, tE, tF;
    int t10, t11, t12, t13;
    int s0, s1, s2, s3, s4, s5, s6, s7;

    s0 = (int) blk[0 * 8] * quant[0 * 8];
    s1 = (int) blk[1 * 8] * quant[1 * 8];
    s2 = (int) blk[2 * 8] * quant[2 * 8];
    s3 = (int) blk[3 * 8] * quant[3 * 8];
    s4 = (int) blk[4 * 8] * quant[4 * 8];
    s5 = (int) blk[5 * 8] * quant[5 * 8];
    s6 = (int) blk[6 * 8] * quant[6 * 8];
    s7 = (int) blk[7 * 8] * quant[7 * 8];

    t0  =  (int)(s3 * 19266U + s5 * 12873U) >> 15;
    t1  =  (int)(s5 * 19266U - s3 * 12873U) >> 15;
    t2  = ((int)(s7 * 4520U  + s1 * 22725U) >> 15) - t0;
    t3  = ((int)(s1 * 4520U  - s7 * 22725U) >> 15) - t1;
    t4  = t0 * 2 + t2;
    t5  = t1 * 2 + t3;
    t6  = t2 - t3;
    t7  = t3 * 2 + t6;
    t8  = (int)(t6 * 11585U) >> 14;
    t9  = (int)(t7 * 11585U) >> 14;
    tA  = (int)(s2 * 8867U - s6 * 21407U) >> 14;
    tB  = (int)(s6 * 8867U + s2 * 21407U) >> 14;
    tC  = (s0 >> 1) - (s4 >> 1);
    tD  = (s4 >> 1) * 2 + tC;
    tE  = tC - (tA >> 1);
    tF  = tD - (tB >> 1);
    t10 = tF - t5;
    t11 = tE - t8;
    t12 = tE + (tA >> 1) * 2 - t9;
    t13 = tF + (tB >> 1) * 2 - t4;

    blk[0 * 8] = t13 + t4 * 2;
    blk[1 * 8] = t12 + t9 * 2;
    blk[2 * 8] = t11 + t8 * 2;
    blk[3 * 8] = t10 + t5 * 2;
    blk[4 * 8] = t10;
    blk[5 * 8] = t11;
    blk[6 * 8] = t12;
    blk[7 * 8] = t13;
}

static inline void idct_row(int16_t *blk)
{
    int t0, t1, t2, t3, t4, t5, t6, t7, t8, t9, tA, tB, tC, tD, tE, tF;
    int t10, t11, t12, t13;

    t0  =  (blk[3] * 19266 + blk[5] * 12873) >> 14;
    t1  =  (blk[5] * 19266 - blk[3] * 12873) >> 14;
    t2  = ((blk[7] * 4520  + blk[1] * 22725) >> 14) - t0;
    t3  = ((blk[1] * 4520  - blk[7] * 22725) >> 14) - t1;
    t4  = t0 * 2 + t2;
    t5  = t1 * 2 + t3;
    t6  = t2 - t3;
    t7  = t3 * 2 + t6;
    t8  = (t6 * 11585) >> 14;
    t9  = (t7 * 11585) >> 14;
    tA  = (blk[2] * 8867 - blk[6] * 21407) >> 14;
    tB  = (blk[6] * 8867 + blk[2] * 21407) >> 14;
    tC  = blk[0] - blk[4];
    tD  = blk[4] * 2 + tC;
    tE  = tC - tA;
    tF  = tD - tB;
    t10 = tF - t5;
    t11 = tE - t8;
    t12 = tE + tA * 2 - t9;
    t13 = tF + tB * 2 - t4;

    blk[0] = (t13 + t4 * 2 + 4) >> 3;
    blk[1] = (t12 + t9 * 2 + 4) >> 3;
    blk[2] = (t11 + t8 * 2 + 4) >> 3;
    blk[3] = (t10 + t5 * 2 + 4) >> 3;
    blk[4] = (t10          + 4) >> 3;
    blk[5] = (t11          + 4) >> 3;
    blk[6] = (t12          + 4) >> 3;
    blk[7] = (t13          + 4) >> 3;
}

static void hqx_idct_put(uint16_t *dst, ptrdiff_t stride,
                         int16_t *block, const uint8_t *quant)
{
    int i, j;

    for (i = 0; i < 8; i++)
        idct_col(block + i, quant + i);
    for (i = 0; i < 8; i++)
        idct_row(block + i * 8);

    for (i = 0; i < 8; i++) {
        for (j = 0; j < 8; j++) {
            int v = av_clip_uintp2(block[j + i * 8] + 0x800, 12);
            dst[j] = (v << 4) | (v >> 8);
        }
        dst += stride >> 1;
    }
}

av_cold void ff_hqxdsp_init(HQXDSPContext *c)
{
    c->idct_put = hqx_idct_put;
}
