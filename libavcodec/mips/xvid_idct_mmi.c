/*
 * Loongson SIMD optimized xvid idct
 *
 * Copyright (c) 2015 Loongson Technology Corporation Limited
 * Copyright (c) 2015 Zhou Xiaoyong <zhouxiaoyong@loongson.cn>
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

#include "libavutil/mem_internal.h"

#include "idctdsp_mips.h"
#include "xvididct_mips.h"

#define BITS_INV_ACC    5                           // 4 or 5 for IEEE
#define SHIFT_INV_ROW   (16 - BITS_INV_ACC)         //11
#define SHIFT_INV_COL   (1 + BITS_INV_ACC)          //6
#define RND_INV_ROW     (1024 * (6 - BITS_INV_ACC))
#define RND_INV_COL     (16 * (BITS_INV_ACC - 3))
#define RND_INV_CORR    (RND_INV_COL - 1)

#define BITS_FRW_ACC    3                           // 2 or 3 for accuracy
#define SHIFT_FRW_COL   BITS_FRW_ACC
#define SHIFT_FRW_ROW   (BITS_FRW_ACC + 17)
#define RND_FRW_ROW     (262144*(BITS_FRW_ACC - 1))

DECLARE_ALIGNED(8, static const int16_t, tg_1_16)[4*4] = {
     13036, 13036, 13036, 13036,    //  tg * (2<<16) + 0.5
     27146, 27146, 27146, 27146,    //  tg * (2<<16) + 0.5
    -21746,-21746,-21746,-21746,    //  tg * (2<<16) + 0.5
     23170, 23170, 23170, 23170     // cos * (2<<15) + 0.5
};

DECLARE_ALIGNED(8, static const int32_t, rounder_0)[2*8] = {
    65536,65536,
     3597, 3597,
     2260, 2260,
     1203, 1203,
        0,    0,
      120,  120,
      512,  512,
      512,  512
};

DECLARE_ALIGNED(8, static const int16_t, tab_i_04_mmi)[32*4] = {
     16384, 21407, 16384,  8867,    // w05 w04 w01 w00
     16384,  8867,-16384,-21407,    // w07 w06 w03 w02
     16384, -8867, 16384,-21407,    // w13 w12 w09 w08
    -16384, 21407, 16384, -8867,    // w15 w14 w11 w10
     22725, 19266, 19266, -4520,    // w21 w20 w17 w16
     12873,  4520,-22725,-12873,    // w23 w22 w19 w18
     12873,-22725,  4520,-12873,    // w29 w28 w25 w24
      4520, 19266, 19266,-22725,    // w31 w30 w27 w26

     22725, 29692, 22725, 12299,    // w05 w04 w01 w00
     22725, 12299,-22725,-29692,    // w07 w06 w03 w02
     22725,-12299, 22725,-29692,    // w13 w12 w09 w08
    -22725, 29692, 22725,-12299,    // w15 w14 w11 w10
     31521, 26722, 26722, -6270,    // w21 w20 w17 w16
     17855,  6270,-31521,-17855,    // w23 w22 w19 w18
     17855,-31521,  6270,-17855,    // w29 w28 w25 w24
      6270, 26722, 26722,-31521,    // w31 w30 w27 w26

     21407, 27969, 21407, 11585,    // w05 w04 w01 w00
     21407, 11585,-21407,-27969,    // w07 w06 w03 w02
     21407,-11585, 21407,-27969,    // w13 w12 w09 w08
    -21407, 27969, 21407,-11585,    // w15 w14 w11 w10
     29692, 25172, 25172, -5906,    // w21 w20 w17 w16
     16819,  5906,-29692,-16819,    // w23 w22 w19 w18
     16819,-29692,  5906,-16819,    // w29 w28 w25 w24
      5906, 25172, 25172,-29692,    // w31 w30 w27 w26

     19266, 25172, 19266, 10426,    // w05 w04 w01 w00
     19266, 10426,-19266,-25172,    // w07 w06 w03 w02
     19266,-10426, 19266,-25172,    // w13 w12 w09 w08
    -19266, 25172, 19266,-10426,    // w15 w14 w11 w10
     26722, 22654, 22654, -5315,    // w21 w20 w17 w16
     15137,  5315,-26722,-15137,    // w23 w22 w19 w18
     15137,-26722,  5315,-15137,    // w29 w28 w25 w24
      5315, 22654, 22654,-26722,    // w31 w30 w27 w26
};

#define DCT_8_INV_ROW_MMI(A1,A2,A3,A4)                                      \
    "dli $10, 0x88              \n\t"                                       \
    "ldc1 $f4, "#A1"            \n\t" /* 0; x3 x2 x1 x0                   */\
    "dmtc1 $10, $f16            \n\t"                                       \
    "ldc1 $f10, 8+"#A1"         \n\t" /* 1; x7 x6 x5 x4                   */\
    "ldc1 $f6, "#A3"            \n\t" /* 3; w05 w04 w01 w00               */\
    "pshufh $f0, $f4, $f16      \n\t" /* x2 x0 x2 x0                      */\
    "ldc1 $f8, 8+"#A3"          \n\t" /* 4; w07 w06 w03 w02               */\
    "ldc1 $f12, 32+"#A3"        \n\t" /* 6; w21 w20 w17 w16               */\
    "pmaddhw $f6, $f6, $f0      \n\t" /* x2*w05+x0*w04 x2*w01+x0*w00      */\
    "dli $10, 0xdd              \n\t"                                       \
    "pshufh $f2, $f10, $f16     \n\t" /* x6 x4 x6 x4                      */\
    "dmtc1 $10, $f16            \n\t"                                       \
    "pmaddhw $f8, $f8, $f2      \n\t" /* x6*w07+x4*w06 x6*w03+x4*w02      */\
    "ldc1 $f14, 40+"#A3"        \n\t" /* 7; w23 w22 w19 w18               */\
    "pshufh $f4, $f4, $f16      \n\t" /* x3 x1 x3 x1                      */\
    "pmaddhw $f12, $f12, $f4    \n\t" /* x3*w21+x1*w20 x3*w17+x1*w16      */\
    "pshufh $f10, $f10, $f16    \n\t" /* x7 x5 x7 x5                      */\
    "ldc1 $f18, "#A4"           \n\t"                                       \
    "pmaddhw $f14, $f14, $f10   \n\t" /* x7*w23+x5*w22 x7*w19+x5*w18      */\
    "paddw $f6, $f6, $f18       \n\t" /* +%4                              */\
    "ldc1 $f16, 16+"#A3"        \n\t"                                       \
    "pmaddhw $f0, $f0, $f16     \n\t" /* x2*w13+x0*w12 x2*w09+x0*w08      */\
    "ldc1 $f16, 24+"#A3"        \n\t"                                       \
    "paddw $f6, $f6, $f8        \n\t" /* 4; a1=sum(even1) a0=sum(even0)   */\
    "pmaddhw $f2, $f2, $f16     \n\t" /* x6*w15+x4*w14 x6*w11+x4*w10      */\
    "ldc1 $f16, 48+"#A3"        \n\t"                                       \
    "pmaddhw $f4, $f4, $f16     \n\t" /* x3*w29+x1*w28 x3*w25+x1*w24      */\
    "ldc1 $f16, 56+"#A3"        \n\t"                                       \
    "paddw $f12, $f12, $f14     \n\t" /* 7; b1=sum(odd1) b0=sum(odd0)     */\
    "dli $10, 11                \n\t"                                       \
    "pmaddhw $f10, $f10, $f16   \n\t" /* x7*w31+x5*w30 x7*w27+x5*w26      */\
    "dmtc1 $10, $f16            \n\t"                                       \
    "psubw $f8, $f6, $f12       \n\t" /* 6; a1-b1 a0-b0                   */\
    "paddw $f6, $f6, $f12       \n\t" /* a1+b1 a0+b0                      */\
    "paddw $f0, $f0, $f18       \n\t" /* +%4                              */\
    "psraw $f6, $f6, $f16       \n\t" /* y1=a1+b1 y0=a0+b0                */\
    "paddw $f0, $f0, $f2        \n\t" /* 1; a3=sum(even3) a2=sum(even2)   */\
    "paddw $f4, $f4, $f10       \n\t" /* 5; b3=sum(odd3) b2=sum(odd2)     */\
    "psraw $f8, $f8, $f16       \n\t" /* y6=a1-b1 y7=a0-b0                */\
    "psubw $f14, $f0, $f4       \n\t" /* 2; a3-b3 a2-b2                   */\
    "paddw $f0, $f0, $f4        \n\t" /* a3+b3 a2+b2                      */\
    "psraw $f0, $f0, $f16       \n\t" /* y3=a3+b3 y2=a2+b2                */\
    "psraw $f14, $f14, $f16     \n\t" /* y4=a3-b3 y5=a2-b2                */\
    "dli $10, 0xb1              \n\t"                                       \
    "packsswh $f6, $f6, $f0     \n\t" /* 0; y3 y2 y1 y0                   */\
    "dmtc1 $10, $f16            \n\t"                                       \
    "packsswh $f14, $f14, $f8   \n\t" /* 4; y6 y7 y4 y5                   */\
    "sdc1 $f6, "#A2"            \n\t" /* 3; save y3 y2 y1 y0              */\
    "pshufh $f14, $f14, $f16    \n\t" /* y7 y6 y5 y4                      */\
    "sdc1 $f14, 8+"#A2"         \n\t" /* 7; save y7 y6 y5 y4              */\


#define DCT_8_INV_COL(A1,A2)                                                \
    "ldc1 $f2, 2*8(%3)          \n\t"                                       \
    "ldc1 $f6, 16*3+"#A1"       \n\t"                                       \
    "ldc1 $f10, 16*5+"#A1"      \n\t"                                       \
    "pmulhh $f0, $f2, $f6       \n\t" /* x3*(tg_3_16-1)                   */\
    "ldc1 $f4, 0(%3)            \n\t"                                       \
    "pmulhh $f2, $f2, $f10      \n\t" /* x5*(tg_3_16-1)                   */\
    "ldc1 $f14, 16*7+"#A1"      \n\t"                                       \
    "ldc1 $f12, 16*1+"#A1"      \n\t"                                       \
    "pmulhh $f8, $f4, $f14      \n\t" /* x7*tg_1_16                       */\
    "paddsh $f0, $f0, $f6       \n\t" /* x3*tg_3_16                       */\
    "pmulhh $f4, $f4, $f12      \n\t" /* x1*tg_1_16                       */\
    "paddsh $f2, $f2, $f6       \n\t" /* x3+x5*(tg_3_16-1)                */\
    "psubsh $f0, $f0, $f10      \n\t" /* x3*tg_3_16-x5 = tm35             */\
    "ldc1 $f6, 3*8(%3)          \n\t"                                       \
    "paddsh $f2, $f2, $f10      \n\t" /* x3+x5*tg_3_16 = tp35             */\
    "paddsh $f8, $f8, $f12      \n\t" /* x1+tg_1_16*x7 = tp17             */\
    "psubsh $f4, $f4, $f14      \n\t" /* x1*tg_1_16-x7 = tm17             */\
    "paddsh $f10, $f8, $f2      \n\t" /* tp17+tp35 = b0                   */\
    "psubsh $f12, $f4, $f0      \n\t" /* tm17-tm35 = b3                   */\
    "psubsh $f8, $f8, $f2       \n\t" /* tp17-tp35 = t1                   */\
    "paddsh $f4, $f4, $f0       \n\t" /* tm17+tm35 = t2                   */\
    "ldc1 $f14, 1*8(%3)         \n\t"                                       \
    "sdc1 $f10, 3*16+"#A2"      \n\t" /* save b0                          */\
    "paddsh $f2, $f8, $f4       \n\t" /* t1+t2                            */\
    "sdc1 $f12, 5*16+"#A2"      \n\t" /* save b3                          */\
    "psubsh $f8, $f8, $f4       \n\t" /* t1-t2                            */\
    "ldc1 $f10, 2*16+"#A1"      \n\t"                                       \
    "ldc1 $f12, 6*16+"#A1"      \n\t"                                       \
    "pmulhh $f0, $f14, $f10     \n\t" /* x2*tg_2_16                       */\
    "pmulhh $f14, $f14, $f12    \n\t" /* x6*tg_2_16                       */\
    "pmulhh $f2, $f2, $f6       \n\t" /* ocos_4_16*(t1+t2) = b1/2         */\
    "ldc1 $f4, 0*16+"#A1"       \n\t"                                       \
    "pmulhh $f8, $f8, $f6       \n\t" /* ocos_4_16*(t1-t2) = b2/2         */\
    "psubsh $f0, $f0, $f12      \n\t" /* t2*tg_2_16-x6 = tm26             */\
    "ldc1 $f12, 4*16+"#A1"      \n\t"                                       \
    "paddsh $f14, $f14, $f10    \n\t" /* x2+x6*tg_2_16 = tp26             */\
    "psubsh $f6, $f4, $f12      \n\t" /* x0-x4 = tm04                     */\
    "paddsh $f4, $f4, $f12      \n\t" /* x0+x4 = tp04                     */\
    "paddsh $f10, $f4, $f14     \n\t" /* tp04+tp26 = a0                   */\
    "psubsh $f12, $f6, $f0      \n\t" /* tm04-tm26 = a2                   */\
    "psubsh $f4, $f4, $f14      \n\t" /* tp04-tp26 = a3                   */\
    "paddsh $f6, $f6, $f0       \n\t" /* tm04+tm26 = a1                   */\
    "paddsh $f2, $f2, $f2       \n\t" /* b1                               */\
    "paddsh $f8, $f8, $f8       \n\t" /* b2                               */\
    "psubsh $f14, $f6, $f2      \n\t" /* a1-b1                            */\
    "dli $10, 6                 \n\t"                                       \
    "paddsh $f6, $f6, $f2       \n\t" /* a1+b1                            */\
    "dmtc1 $10, $f16            \n\t"                                       \
    "psubsh $f0, $f12, $f8      \n\t" /* a2-b2                            */\
    "paddsh $f12, $f12, $f8     \n\t" /* a2+b2                            */\
    "psrah $f6, $f6, $f16       \n\t" /* dst1                             */\
    "psrah $f12, $f12, $f16     \n\t" /* dst2                             */\
    "ldc1 $f2, 3*16+"#A2"       \n\t" /* load b0                          */\
    "psrah $f14, $f14, $f16     \n\t" /* dst6                             */\
    "psrah $f0, $f0, $f16       \n\t" /* dst5                             */\
    "sdc1 $f6, 1*16+"#A2"       \n\t"                                       \
    "psubsh $f8, $f10, $f2      \n\t" /* a0-b0                            */\
    "paddsh $f10, $f10, $f2     \n\t" /* a0+b0                            */\
    "sdc1 $f12, 2*16+"#A2"      \n\t"                                       \
    "ldc1 $f6, 5*16+"#A2"       \n\t" /* load b3                          */\
    "psrah $f10, $f10, $f16     \n\t" /* dst0                             */\
    "psrah $f8, $f8, $f16       \n\t" /* dst7                             */\
    "sdc1 $f0, 5*16+"#A2"       \n\t"                                       \
    "psubsh $f12, $f4, $f6      \n\t" /* a3-b3                            */\
    "paddsh $f4, $f4, $f6       \n\t" /* a3+b3                            */\
    "sdc1 $f14, 6*16+"#A2"      \n\t"                                       \
    "sdc1 $f10, 0*16+"#A2"      \n\t"                                       \
    "psrah $f4, $f4, $f16       \n\t" /* dst3                             */\
    "sdc1 $f8, 7*16+"#A2"       \n\t"                                       \
    "psrah $f12, $f12, $f16     \n\t" /* dst4                             */\
    "sdc1 $f4, 3*16+"#A2"       \n\t"                                       \
    "sdc1 $f12, 4*16+"#A2"      \n\t"                                       \


void ff_xvid_idct_mmi(int16_t *block)
{
    __asm__ volatile (
        //# Process each row
        DCT_8_INV_ROW_MMI(0*16(%0), 0*16(%0), 64*0(%2), 8*0(%1))
        DCT_8_INV_ROW_MMI(1*16(%0), 1*16(%0), 64*1(%2), 8*1(%1))
        DCT_8_INV_ROW_MMI(2*16(%0), 2*16(%0), 64*2(%2), 8*2(%1))
        DCT_8_INV_ROW_MMI(3*16(%0), 3*16(%0), 64*3(%2), 8*3(%1))
        DCT_8_INV_ROW_MMI(4*16(%0), 4*16(%0), 64*0(%2), 8*4(%1))
        DCT_8_INV_ROW_MMI(5*16(%0), 5*16(%0), 64*3(%2), 8*5(%1))
        DCT_8_INV_ROW_MMI(6*16(%0), 6*16(%0), 64*2(%2), 8*6(%1))
        DCT_8_INV_ROW_MMI(7*16(%0), 7*16(%0), 64*1(%2), 8*7(%1))
        //# Process the columns (4 at a time)
        DCT_8_INV_COL(0(%0), 0(%0))
        DCT_8_INV_COL(8(%0), 8(%0))
        ::"r"(block),"r"(rounder_0),"r"(tab_i_04_mmi),"r"(tg_1_16)
        : "$10"
    );
}

void ff_xvid_idct_put_mmi(uint8_t *dest, ptrdiff_t line_size, int16_t *block)
{
    ff_xvid_idct_mmi(block);
    ff_put_pixels_clamped_mmi(block, dest, line_size);
}

void ff_xvid_idct_add_mmi(uint8_t *dest, ptrdiff_t line_size, int16_t *block)
{
    ff_xvid_idct_mmi(block);
    ff_add_pixels_clamped_mmi(block, dest, line_size);
}
