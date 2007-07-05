/*
 * XVID MPEG-4 VIDEO CODEC
 * - MMX and XMM forward discrete cosine transform -
 *
 * Copyright(C) 2001 Peter Ross <pross@xvid.org>
 *
 * Originally provided by Intel at AP-922
 * http://developer.intel.com/vtune/cbts/strmsimd/922down.htm
 * (See more app notes at http://developer.intel.com/vtune/cbts/strmsimd/appnotes.htm)
 * but in a limited edition.
 * New macro implements a column part for precise iDCT
 * The routine precision now satisfies IEEE standard 1180-1990.
 *
 * Copyright(C) 2000-2001 Peter Gubanov <peter@elecard.net.ru>
 * Rounding trick Copyright(C) 2000 Michel Lespinasse <walken@zoy.org>
 *
 * http://www.elecard.com/peter/idct.html
 * http://www.linuxvideo.org/mpeg2dec/
 *
 * These examples contain code fragments for first stage iDCT 8x8
 * (for rows) and first stage DCT 8x8 (for columns)
 *
 * conversion to gcc syntax by Michael Niedermayer
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
 * You should have received a copy of the GNU Lesser General Public License
 * along with FFmpeg; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <inttypes.h>
#include "avcodec.h"

//=============================================================================
// Macros and other preprocessor constants
//=============================================================================

#define BITS_INV_ACC    5                              // 4 or 5 for IEEE
#define SHIFT_INV_ROW   (16 - BITS_INV_ACC) //11
#define SHIFT_INV_COL   (1 + BITS_INV_ACC) //6
#define RND_INV_ROW     (1024 * (6 - BITS_INV_ACC))
#define RND_INV_COL     (16 * (BITS_INV_ACC - 3))
#define RND_INV_CORR    (RND_INV_COL - 1)

#define BITS_FRW_ACC    3                              // 2 or 3 for accuracy
#define SHIFT_FRW_COL   BITS_FRW_ACC
#define SHIFT_FRW_ROW   (BITS_FRW_ACC + 17)
#define RND_FRW_ROW     (262144*(BITS_FRW_ACC - 1))


//-----------------------------------------------------------------------------
// Various memory constants (trigonometric values or rounding values)
//-----------------------------------------------------------------------------


static const int16_t tg_1_16[4*4] attribute_used __attribute__ ((aligned(8))) = {
  13036,13036,13036,13036,        // tg * (2<<16) + 0.5
  27146,27146,27146,27146,        // tg * (2<<16) + 0.5
  -21746,-21746,-21746,-21746,    // tg * (2<<16) + 0.5
  23170,23170,23170,23170};       // cos * (2<<15) + 0.5

static const int32_t rounder_0[2*8] attribute_used __attribute__ ((aligned(8))) = {
  65536,65536,
  3597,3597,
  2260,2260,
  1203,1203,
  0,0,
  120,120,
  512,512,
  512,512};

//-----------------------------------------------------------------------------
//
// The first stage iDCT 8x8 - inverse DCTs of rows
//
//-----------------------------------------------------------------------------
// The 8-point inverse DCT direct algorithm
//-----------------------------------------------------------------------------
//
// static const short w[32] = {
//       FIX(cos_4_16),  FIX(cos_2_16),  FIX(cos_4_16),  FIX(cos_6_16),
//       FIX(cos_4_16),  FIX(cos_6_16), -FIX(cos_4_16), -FIX(cos_2_16),
//       FIX(cos_4_16), -FIX(cos_6_16), -FIX(cos_4_16),  FIX(cos_2_16),
//       FIX(cos_4_16), -FIX(cos_2_16),  FIX(cos_4_16), -FIX(cos_6_16),
//       FIX(cos_1_16),  FIX(cos_3_16),  FIX(cos_5_16),  FIX(cos_7_16),
//       FIX(cos_3_16), -FIX(cos_7_16), -FIX(cos_1_16), -FIX(cos_5_16),
//       FIX(cos_5_16), -FIX(cos_1_16),  FIX(cos_7_16),  FIX(cos_3_16),
//       FIX(cos_7_16), -FIX(cos_5_16),  FIX(cos_3_16), -FIX(cos_1_16) };
//
// #define DCT_8_INV_ROW(x, y)
// {
//       int a0, a1, a2, a3, b0, b1, b2, b3;
//
//       a0 =x[0]*w[0]+x[2]*w[1]+x[4]*w[2]+x[6]*w[3];
//       a1 =x[0]*w[4]+x[2]*w[5]+x[4]*w[6]+x[6]*w[7];
//       a2 = x[0] * w[ 8] + x[2] * w[ 9] + x[4] * w[10] + x[6] * w[11];
//       a3 = x[0] * w[12] + x[2] * w[13] + x[4] * w[14] + x[6] * w[15];
//       b0 = x[1] * w[16] + x[3] * w[17] + x[5] * w[18] + x[7] * w[19];
//       b1 = x[1] * w[20] + x[3] * w[21] + x[5] * w[22] + x[7] * w[23];
//       b2 = x[1] * w[24] + x[3] * w[25] + x[5] * w[26] + x[7] * w[27];
//       b3 = x[1] * w[28] + x[3] * w[29] + x[5] * w[30] + x[7] * w[31];
//
//       y[0] = SHIFT_ROUND ( a0 + b0 );
//       y[1] = SHIFT_ROUND ( a1 + b1 );
//       y[2] = SHIFT_ROUND ( a2 + b2 );
//       y[3] = SHIFT_ROUND ( a3 + b3 );
//       y[4] = SHIFT_ROUND ( a3 - b3 );
//       y[5] = SHIFT_ROUND ( a2 - b2 );
//       y[6] = SHIFT_ROUND ( a1 - b1 );
//       y[7] = SHIFT_ROUND ( a0 - b0 );
// }
//
//-----------------------------------------------------------------------------
//
// In this implementation the outputs of the iDCT-1D are multiplied
//       for rows 0,4 - by cos_4_16,
//       for rows 1,7 - by cos_1_16,
//       for rows 2,6 - by cos_2_16,
//       for rows 3,5 - by cos_3_16
// and are shifted to the left for better accuracy
//
// For the constants used,
//       FIX(float_const) = (short) (float_const * (1<<15) + 0.5)
//
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Tables for mmx processors
//-----------------------------------------------------------------------------

// Table for rows 0,4 - constants are multiplied by cos_4_16
static const int16_t tab_i_04_mmx[32*4] attribute_used __attribute__ ((aligned(8))) = {
  16384,16384,16384,-16384,       // movq-> w06 w04 w02 w00
  21407,8867,8867,-21407,         // w07 w05 w03 w01
  16384,-16384,16384,16384,       // w14 w12 w10 w08
  -8867,21407,-21407,-8867,       // w15 w13 w11 w09
  22725,12873,19266,-22725,       // w22 w20 w18 w16
  19266,4520,-4520,-12873,        // w23 w21 w19 w17
  12873,4520,4520,19266,          // w30 w28 w26 w24
  -22725,19266,-12873,-22725,     // w31 w29 w27 w25
// Table for rows 1,7 - constants are multiplied by cos_1_16
  22725,22725,22725,-22725,       // movq-> w06 w04 w02 w00
  29692,12299,12299,-29692,       // w07 w05 w03 w01
  22725,-22725,22725,22725,       // w14 w12 w10 w08
  -12299,29692,-29692,-12299,     // w15 w13 w11 w09
  31521,17855,26722,-31521,       // w22 w20 w18 w16
  26722,6270,-6270,-17855,        // w23 w21 w19 w17
  17855,6270,6270,26722,          // w30 w28 w26 w24
  -31521,26722,-17855,-31521,     // w31 w29 w27 w25
// Table for rows 2,6 - constants are multiplied by cos_2_16
  21407,21407,21407,-21407,       // movq-> w06 w04 w02 w00
  27969,11585,11585,-27969,       // w07 w05 w03 w01
  21407,-21407,21407,21407,       // w14 w12 w10 w08
  -11585,27969,-27969,-11585,     // w15 w13 w11 w09
  29692,16819,25172,-29692,       // w22 w20 w18 w16
  25172,5906,-5906,-16819,        // w23 w21 w19 w17
  16819,5906,5906,25172,          // w30 w28 w26 w24
  -29692,25172,-16819,-29692,     // w31 w29 w27 w25
// Table for rows 3,5 - constants are multiplied by cos_3_16
  19266,19266,19266,-19266,       // movq-> w06 w04 w02 w00
  25172,10426,10426,-25172,       // w07 w05 w03 w01
  19266,-19266,19266,19266,       // w14 w12 w10 w08
  -10426,25172,-25172,-10426,     // w15 w13 w11 w09
  26722,15137,22654,-26722,       // w22 w20 w18 w16
  22654,5315,-5315,-15137,        // w23 w21 w19 w17
  15137,5315,5315,22654,          // w30 w28 w26 w24
  -26722,22654,-15137,-26722,     // w31 w29 w27 w25
};
//-----------------------------------------------------------------------------
// Tables for xmm processors
//-----------------------------------------------------------------------------

// %3 for rows 0,4 - constants are multiplied by cos_4_16
static const int16_t tab_i_04_xmm[32*4] attribute_used __attribute__ ((aligned(8))) = {
  16384,21407,16384,8867,      // movq-> w05 w04 w01 w00
  16384,8867,-16384,-21407,    // w07 w06 w03 w02
  16384,-8867,16384,-21407,    // w13 w12 w09 w08
  -16384,21407,16384,-8867,    // w15 w14 w11 w10
  22725,19266,19266,-4520,     // w21 w20 w17 w16
  12873,4520,-22725,-12873,    // w23 w22 w19 w18
  12873,-22725,4520,-12873,    // w29 w28 w25 w24
  4520,19266,19266,-22725,     // w31 w30 w27 w26
// %3 for rows 1,7 - constants are multiplied by cos_1_16
  22725,29692,22725,12299,     // movq-> w05 w04 w01 w00
  22725,12299,-22725,-29692,   // w07 w06 w03 w02
  22725,-12299,22725,-29692,   // w13 w12 w09 w08
  -22725,29692,22725,-12299,   // w15 w14 w11 w10
  31521,26722,26722,-6270,     // w21 w20 w17 w16
  17855,6270,-31521,-17855,    // w23 w22 w19 w18
  17855,-31521,6270,-17855,    // w29 w28 w25 w24
  6270,26722,26722,-31521,     // w31 w30 w27 w26
// %3 for rows 2,6 - constants are multiplied by cos_2_16
  21407,27969,21407,11585,     // movq-> w05 w04 w01 w00
  21407,11585,-21407,-27969,   // w07 w06 w03 w02
  21407,-11585,21407,-27969,   // w13 w12 w09 w08
  -21407,27969,21407,-11585,   // w15 w14 w11 w10
  29692,25172,25172,-5906,     // w21 w20 w17 w16
  16819,5906,-29692,-16819,    // w23 w22 w19 w18
  16819,-29692,5906,-16819,    // w29 w28 w25 w24
  5906,25172,25172,-29692,     // w31 w30 w27 w26
// %3 for rows 3,5 - constants are multiplied by cos_3_16
  19266,25172,19266,10426,     // movq-> w05 w04 w01 w00
  19266,10426,-19266,-25172,   // w07 w06 w03 w02
  19266,-10426,19266,-25172,   // w13 w12 w09 w08
  -19266,25172,19266,-10426,   // w15 w14 w11 w10
  26722,22654,22654,-5315,     // w21 w20 w17 w16
  15137,5315,-26722,-15137,    // w23 w22 w19 w18
  15137,-26722,5315,-15137,    // w29 w28 w25 w24
  5315,22654,22654,-26722,     // w31 w30 w27 w26
};
//=============================================================================
// Helper macros for the code
//=============================================================================

//-----------------------------------------------------------------------------
// DCT_8_INV_ROW_MMX( INP, OUT, TABLE, ROUNDER
//-----------------------------------------------------------------------------

#define DCT_8_INV_ROW_MMX(A1,A2,A3,A4)\
  "movq " #A1 ",%%mm0              \n\t"/* 0 ; x3 x2 x1 x0*/\
  "movq 8+" #A1 ",%%mm1            \n\t"/* 1 ; x7 x6 x5 x4*/\
  "movq %%mm0,%%mm2            \n\t"/* 2  ; x3 x2 x1 x0*/\
  "movq " #A3 ",%%mm3              \n\t"/* 3 ; w06 w04 w02 w00*/\
  "punpcklwd %%mm1,%%mm0       \n\t"/* x5 x1 x4 x0*/\
  "movq %%mm0,%%mm5            \n\t"/* 5 ; x5 x1 x4 x0*/\
  "punpckldq %%mm0,%%mm0       \n\t"/* x4 x0 x4 x0*/\
  "movq 8+" #A3 ",%%mm4            \n\t"/* 4 ; w07 w05 w03 w01*/\
  "punpckhwd %%mm1,%%mm2       \n\t"/* 1 ; x7 x3 x6 x2*/\
  "pmaddwd %%mm0,%%mm3         \n\t"/* x4*w06+x0*w04 x4*w02+x0*w00*/\
  "movq %%mm2,%%mm6            \n\t"/* 6 ; x7 x3 x6 x2*/\
  "movq 32+" #A3 ",%%mm1           \n\t"/* 1 ; w22 w20 w18 w16*/\
  "punpckldq %%mm2,%%mm2       \n\t"/* x6 x2 x6 x2*/\
  "pmaddwd %%mm2,%%mm4         \n\t"/* x6*w07+x2*w05 x6*w03+x2*w01*/\
  "punpckhdq %%mm5,%%mm5       \n\t"/* x5 x1 x5 x1*/\
  "pmaddwd 16+" #A3 ",%%mm0        \n\t"/* x4*w14+x0*w12 x4*w10+x0*w08*/\
  "punpckhdq %%mm6,%%mm6       \n\t"/* x7 x3 x7 x3*/\
  "movq 40+" #A3 ",%%mm7           \n\t"/* 7 ; w23 w21 w19 w17*/\
  "pmaddwd %%mm5,%%mm1         \n\t"/* x5*w22+x1*w20 x5*w18+x1*w16*/\
  "paddd " #A4 ",%%mm3             \n\t"/* +%4*/\
  "pmaddwd %%mm6,%%mm7         \n\t"/* x7*w23+x3*w21 x7*w19+x3*w17*/\
  "pmaddwd 24+" #A3 ",%%mm2        \n\t"/* x6*w15+x2*w13 x6*w11+x2*w09*/\
  "paddd %%mm4,%%mm3           \n\t"/* 4 ; a1=sum(even1) a0=sum(even0)*/\
  "pmaddwd 48+" #A3 ",%%mm5        \n\t"/* x5*w30+x1*w28 x5*w26+x1*w24*/\
  "movq %%mm3,%%mm4            \n\t"/* 4 ; a1 a0*/\
  "pmaddwd 56+" #A3 ",%%mm6        \n\t"/* x7*w31+x3*w29 x7*w27+x3*w25*/\
  "paddd %%mm7,%%mm1           \n\t"/* 7 ; b1=sum(odd1) b0=sum(odd0)*/\
  "paddd " #A4 ",%%mm0             \n\t"/* +%4*/\
  "psubd %%mm1,%%mm3           \n\t"/* a1-b1 a0-b0*/\
  "psrad $11,%%mm3 \n\t"/* y6=a1-b1 y7=a0-b0*/\
  "paddd %%mm4,%%mm1           \n\t"/* 4 ; a1+b1 a0+b0*/\
  "paddd %%mm2,%%mm0           \n\t"/* 2 ; a3=sum(even3) a2=sum(even2)*/\
  "psrad $11,%%mm1 \n\t"/* y1=a1+b1 y0=a0+b0*/\
  "paddd %%mm6,%%mm5           \n\t"/* 6 ; b3=sum(odd3) b2=sum(odd2)*/\
  "movq %%mm0,%%mm4            \n\t"/* 4 ; a3 a2*/\
  "paddd %%mm5,%%mm0           \n\t"/* a3+b3 a2+b2*/\
  "psubd %%mm5,%%mm4           \n\t"/* 5 ; a3-b3 a2-b2*/\
  "psrad $11,%%mm0 \n\t"/* y3=a3+b3 y2=a2+b2*/\
  "psrad $11,%%mm4 \n\t"/* y4=a3-b3 y5=a2-b2*/\
  "packssdw %%mm0,%%mm1        \n\t"/* 0 ; y3 y2 y1 y0*/\
  "packssdw %%mm3,%%mm4        \n\t"/* 3 ; y6 y7 y4 y5*/\
  "movq %%mm4,%%mm7            \n\t"/* 7 ; y6 y7 y4 y5*/\
  "psrld $16,%%mm4            \n\t"/* 0 y6 0 y4*/\
  "pslld $16,%%mm7            \n\t"/* y7 0 y5 0*/\
  "movq %%mm1," #A2 "              \n\t"/* 1 ; save y3 y2 y1 y0*/\
  "por %%mm4,%%mm7             \n\t"/* 4 ; y7 y6 y5 y4*/\
  "movq %%mm7,8            +" #A2 "\n\t"/* 7 ; save y7 y6 y5 y4*/\


//-----------------------------------------------------------------------------
// DCT_8_INV_ROW_XMM( INP, OUT, TABLE, ROUNDER
//-----------------------------------------------------------------------------

#define DCT_8_INV_ROW_XMM(A1,A2,A3,A4)\
  "movq " #A1 ",%%mm0                  \n\t"/* 0     ; x3 x2 x1 x0*/\
  "movq 8+" #A1 ",%%mm1                \n\t"/* 1     ; x7 x6 x5 x4*/\
  "movq %%mm0,%%mm2                \n\t"/* 2     ; x3 x2 x1 x0*/\
  "movq " #A3 ",%%mm3                  \n\t"/* 3     ; w05 w04 w01 w00*/\
  "pshufw $0x88,%%mm0,%%mm0        \n\t"/* x2 x0 x2 x0*/\
  "movq 8+" #A3 ",%%mm4                \n\t"/* 4     ; w07 w06 w03 w02*/\
  "movq %%mm1,%%mm5                \n\t"/* 5     ; x7 x6 x5 x4*/\
  "pmaddwd %%mm0,%%mm3             \n\t"/* x2*w05+x0*w04 x2*w01+x0*w00*/\
  "movq 32+" #A3 ",%%mm6               \n\t"/* 6     ; w21 w20 w17 w16*/\
  "pshufw $0x88,%%mm1,%%mm1        \n\t"/* x6 x4 x6 x4*/\
  "pmaddwd %%mm1,%%mm4             \n\t"/* x6*w07+x4*w06 x6*w03+x4*w02*/\
  "movq 40+" #A3 ",%%mm7               \n\t"/* 7    ; w23 w22 w19 w18*/\
  "pshufw $0xdd,%%mm2,%%mm2        \n\t"/* x3 x1 x3 x1*/\
  "pmaddwd %%mm2,%%mm6             \n\t"/* x3*w21+x1*w20 x3*w17+x1*w16*/\
  "pshufw $0xdd,%%mm5,%%mm5        \n\t"/* x7 x5 x7 x5*/\
  "pmaddwd %%mm5,%%mm7             \n\t"/* x7*w23+x5*w22 x7*w19+x5*w18*/\
  "paddd " #A4 ",%%mm3                 \n\t"/* +%4*/\
  "pmaddwd 16+" #A3 ",%%mm0            \n\t"/* x2*w13+x0*w12 x2*w09+x0*w08*/\
  "paddd %%mm4,%%mm3               \n\t"/* 4     ; a1=sum(even1) a0=sum(even0)*/\
  "pmaddwd 24+" #A3 ",%%mm1            \n\t"/* x6*w15+x4*w14 x6*w11+x4*w10*/\
  "movq %%mm3,%%mm4                \n\t"/* 4     ; a1 a0*/\
  "pmaddwd 48+" #A3 ",%%mm2            \n\t"/* x3*w29+x1*w28 x3*w25+x1*w24*/\
  "paddd %%mm7,%%mm6               \n\t"/* 7     ; b1=sum(odd1) b0=sum(odd0)*/\
  "pmaddwd 56+" #A3 ",%%mm5            \n\t"/* x7*w31+x5*w30 x7*w27+x5*w26*/\
  "paddd %%mm6,%%mm3               \n\t"/* a1+b1 a0+b0*/\
  "paddd " #A4 ",%%mm0                 \n\t"/* +%4*/\
  "psrad $11,%%mm3     \n\t"/* y1=a1+b1 y0=a0+b0*/\
  "paddd %%mm1,%%mm0               \n\t"/* 1     ; a3=sum(even3) a2=sum(even2)*/\
  "psubd %%mm6,%%mm4               \n\t"/* 6     ; a1-b1 a0-b0*/\
  "movq %%mm0,%%mm7                \n\t"/* 7     ; a3 a2*/\
  "paddd %%mm5,%%mm2               \n\t"/* 5     ; b3=sum(odd3) b2=sum(odd2)*/\
  "paddd %%mm2,%%mm0               \n\t"/* a3+b3 a2+b2*/\
  "psrad $11,%%mm4     \n\t"/* y6=a1-b1 y7=a0-b0*/\
  "psubd %%mm2,%%mm7               \n\t"/* 2     ; a3-b3 a2-b2*/\
  "psrad $11,%%mm0     \n\t"/* y3=a3+b3 y2=a2+b2*/\
  "psrad $11,%%mm7     \n\t"/* y4=a3-b3 y5=a2-b2*/\
  "packssdw %%mm0,%%mm3            \n\t"/* 0     ; y3 y2 y1 y0*/\
  "packssdw %%mm4,%%mm7            \n\t"/* 4     ; y6 y7 y4 y5*/\
  "movq %%mm3, " #A2 "                  \n\t"/* 3     ; save y3 y2 y1 y0*/\
  "pshufw $0xb1,%%mm7,%%mm7        \n\t"/* y7 y6 y5 y4*/\
  "movq %%mm7,8                +" #A2 "\n\t"/* 7     ; save y7 y6 y5 y4*/\


//-----------------------------------------------------------------------------
//
// The first stage DCT 8x8 - forward DCTs of columns
//
// The %2puts are multiplied
// for rows 0,4 - on cos_4_16,
// for rows 1,7 - on cos_1_16,
// for rows 2,6 - on cos_2_16,
// for rows 3,5 - on cos_3_16
// and are shifted to the left for rise of accuracy
//
//-----------------------------------------------------------------------------
//
// The 8-point scaled forward DCT algorithm (26a8m)
//
//-----------------------------------------------------------------------------
//
// #define DCT_8_FRW_COL(x, y)
//{
// short t0, t1, t2, t3, t4, t5, t6, t7;
// short tp03, tm03, tp12, tm12, tp65, tm65;
// short tp465, tm465, tp765, tm765;
//
// t0 = LEFT_SHIFT ( x[0] + x[7] );
// t1 = LEFT_SHIFT ( x[1] + x[6] );
// t2 = LEFT_SHIFT ( x[2] + x[5] );
// t3 = LEFT_SHIFT ( x[3] + x[4] );
// t4 = LEFT_SHIFT ( x[3] - x[4] );
// t5 = LEFT_SHIFT ( x[2] - x[5] );
// t6 = LEFT_SHIFT ( x[1] - x[6] );
// t7 = LEFT_SHIFT ( x[0] - x[7] );
//
// tp03 = t0 + t3;
// tm03 = t0 - t3;
// tp12 = t1 + t2;
// tm12 = t1 - t2;
//
// y[0] = tp03 + tp12;
// y[4] = tp03 - tp12;
//
// y[2] = tm03 + tm12 * tg_2_16;
// y[6] = tm03 * tg_2_16 - tm12;
//
// tp65 =(t6 +t5 )*cos_4_16;
// tm65 =(t6 -t5 )*cos_4_16;
//
// tp765 = t7 + tp65;
// tm765 = t7 - tp65;
// tp465 = t4 + tm65;
// tm465 = t4 - tm65;
//
// y[1] = tp765 + tp465 * tg_1_16;
// y[7] = tp765 * tg_1_16 - tp465;
// y[5] = tm765 * tg_3_16 + tm465;
// y[3] = tm765 - tm465 * tg_3_16;
//}
//
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// DCT_8_INV_COL_4  INP,OUT
//-----------------------------------------------------------------------------

#define DCT_8_INV_COL(A1,A2)\
  "movq 2*8(%3),%%mm0\n\t"\
  "movq 16*3+" #A1 ",%%mm3\n\t"\
  "movq %%mm0,%%mm1            \n\t"/* tg_3_16*/\
  "movq 16*5+" #A1 ",%%mm5\n\t"\
  "pmulhw %%mm3,%%mm0          \n\t"/* x3*(tg_3_16-1)*/\
  "movq (%3),%%mm4\n\t"\
  "pmulhw %%mm5,%%mm1          \n\t"/* x5*(tg_3_16-1)*/\
  "movq 16*7+" #A1 ",%%mm7\n\t"\
  "movq %%mm4,%%mm2            \n\t"/* tg_1_16*/\
  "movq 16*1+" #A1 ",%%mm6\n\t"\
  "pmulhw %%mm7,%%mm4          \n\t"/* x7*tg_1_16*/\
  "paddsw %%mm3,%%mm0          \n\t"/* x3*tg_3_16*/\
  "pmulhw %%mm6,%%mm2          \n\t"/* x1*tg_1_16*/\
  "paddsw %%mm3,%%mm1          \n\t"/* x3+x5*(tg_3_16-1)*/\
  "psubsw %%mm5,%%mm0          \n\t"/* x3*tg_3_16-x5 = tm35*/\
  "movq 3*8(%3),%%mm3\n\t"\
  "paddsw %%mm5,%%mm1          \n\t"/* x3+x5*tg_3_16 = tp35*/\
  "paddsw %%mm6,%%mm4          \n\t"/* x1+tg_1_16*x7 = tp17*/\
  "psubsw %%mm7,%%mm2          \n\t"/* x1*tg_1_16-x7 = tm17*/\
  "movq %%mm4,%%mm5            \n\t"/* tp17*/\
  "movq %%mm2,%%mm6            \n\t"/* tm17*/\
  "paddsw %%mm1,%%mm5          \n\t"/* tp17+tp35 = b0*/\
  "psubsw %%mm0,%%mm6          \n\t"/* tm17-tm35 = b3*/\
  "psubsw %%mm1,%%mm4          \n\t"/* tp17-tp35 = t1*/\
  "paddsw %%mm0,%%mm2          \n\t"/* tm17+tm35 = t2*/\
  "movq 1*8(%3),%%mm7\n\t"\
  "movq %%mm4,%%mm1            \n\t"/* t1*/\
  "movq %%mm5,3*16         +" #A2 "\n\t"/* save b0*/\
  "paddsw %%mm2,%%mm1          \n\t"/* t1+t2*/\
  "movq %%mm6,5*16         +" #A2 "\n\t"/* save b3*/\
  "psubsw %%mm2,%%mm4          \n\t"/* t1-t2*/\
  "movq 2*16+" #A1 ",%%mm5\n\t"\
  "movq %%mm7,%%mm0            \n\t"/* tg_2_16*/\
  "movq 6*16+" #A1 ",%%mm6\n\t"\
  "pmulhw %%mm5,%%mm0          \n\t"/* x2*tg_2_16*/\
  "pmulhw %%mm6,%%mm7          \n\t"/* x6*tg_2_16*/\
  "pmulhw %%mm3,%%mm1          \n\t"/* ocos_4_16*(t1+t2) = b1/2*/\
  "movq 0*16+" #A1 ",%%mm2\n\t"\
  "pmulhw %%mm3,%%mm4          \n\t"/* ocos_4_16*(t1-t2) = b2/2*/\
  "psubsw %%mm6,%%mm0          \n\t"/* t2*tg_2_16-x6 = tm26*/\
  "movq %%mm2,%%mm3            \n\t"/* x0*/\
  "movq 4*16+" #A1 ",%%mm6\n\t"\
  "paddsw %%mm5,%%mm7          \n\t"/* x2+x6*tg_2_16 = tp26*/\
  "paddsw %%mm6,%%mm2          \n\t"/* x0+x4 = tp04*/\
  "psubsw %%mm6,%%mm3          \n\t"/* x0-x4 = tm04*/\
  "movq %%mm2,%%mm5            \n\t"/* tp04*/\
  "movq %%mm3,%%mm6            \n\t"/* tm04*/\
  "psubsw %%mm7,%%mm2          \n\t"/* tp04-tp26 = a3*/\
  "paddsw %%mm0,%%mm3          \n\t"/* tm04+tm26 = a1*/\
  "paddsw %%mm1,%%mm1          \n\t"/* b1*/\
  "paddsw %%mm4,%%mm4          \n\t"/* b2*/\
  "paddsw %%mm7,%%mm5          \n\t"/* tp04+tp26 = a0*/\
  "psubsw %%mm0,%%mm6          \n\t"/* tm04-tm26 = a2*/\
  "movq %%mm3,%%mm7            \n\t"/* a1*/\
  "movq %%mm6,%%mm0            \n\t"/* a2*/\
  "paddsw %%mm1,%%mm3          \n\t"/* a1+b1*/\
  "paddsw %%mm4,%%mm6          \n\t"/* a2+b2*/\
  "psraw $6,%%mm3 \n\t"/* dst1*/\
  "psubsw %%mm1,%%mm7          \n\t"/* a1-b1*/\
  "psraw $6,%%mm6 \n\t"/* dst2*/\
  "psubsw %%mm4,%%mm0          \n\t"/* a2-b2*/\
  "movq 3*16+" #A2 ",%%mm1         \n\t"/* load b0*/\
  "psraw $6,%%mm7 \n\t"/* dst6*/\
  "movq %%mm5,%%mm4            \n\t"/* a0*/\
  "psraw $6,%%mm0 \n\t"/* dst5*/\
  "movq %%mm3,1*16+" #A2 "\n\t"\
  "paddsw %%mm1,%%mm5          \n\t"/* a0+b0*/\
  "movq %%mm6,2*16+" #A2 "\n\t"\
  "psubsw %%mm1,%%mm4          \n\t"/* a0-b0*/\
  "movq 5*16+" #A2 ",%%mm3         \n\t"/* load b3*/\
  "psraw $6,%%mm5 \n\t"/* dst0*/\
  "movq %%mm2,%%mm6            \n\t"/* a3*/\
  "psraw $6,%%mm4 \n\t"/* dst7*/\
  "movq %%mm0,5*16+" #A2 "\n\t"\
  "paddsw %%mm3,%%mm2          \n\t"/* a3+b3*/\
  "movq %%mm7,6*16+" #A2 "\n\t"\
  "psubsw %%mm3,%%mm6          \n\t"/* a3-b3*/\
  "movq %%mm5,0*16+" #A2 "\n\t"\
  "psraw $6,%%mm2 \n\t"/* dst3*/\
  "movq %%mm4,7*16+" #A2 "\n\t"\
  "psraw $6,%%mm6 \n\t"/* dst4*/\
  "movq %%mm2,3*16+" #A2 "\n\t"\
  "movq %%mm6,4*16+" #A2 "\n\t"

//=============================================================================
// Code
//=============================================================================

//-----------------------------------------------------------------------------
// void idct_mmx(uint16_t block[64]);
//-----------------------------------------------------------------------------


void ff_idct_xvid_mmx(short *block){
asm volatile(
            //# Process each row
    DCT_8_INV_ROW_MMX(0*16(%0), 0*16(%0), 64*0(%2), 8*0(%1))
    DCT_8_INV_ROW_MMX(1*16(%0), 1*16(%0), 64*1(%2), 8*1(%1))
    DCT_8_INV_ROW_MMX(2*16(%0), 2*16(%0), 64*2(%2), 8*2(%1))
    DCT_8_INV_ROW_MMX(3*16(%0), 3*16(%0), 64*3(%2), 8*3(%1))
    DCT_8_INV_ROW_MMX(4*16(%0), 4*16(%0), 64*0(%2), 8*4(%1))
    DCT_8_INV_ROW_MMX(5*16(%0), 5*16(%0), 64*3(%2), 8*5(%1))
    DCT_8_INV_ROW_MMX(6*16(%0), 6*16(%0), 64*2(%2), 8*6(%1))
    DCT_8_INV_ROW_MMX(7*16(%0), 7*16(%0), 64*1(%2), 8*7(%1))

            //# Process the columns (4 at a time)
    DCT_8_INV_COL(0(%0), 0(%0))
    DCT_8_INV_COL(8(%0), 8(%0))
    :: "r"(block), "r"(rounder_0), "r"(tab_i_04_mmx), "r"(tg_1_16));
}

//-----------------------------------------------------------------------------
// void idct_xmm(uint16_t block[64]);
//-----------------------------------------------------------------------------


void ff_idct_xvid_mmx2(short *block){
asm volatile(
            //# Process each row
    DCT_8_INV_ROW_XMM(0*16(%0), 0*16(%0), 64*0(%2), 8*0(%1))
    DCT_8_INV_ROW_XMM(1*16(%0), 1*16(%0), 64*1(%2), 8*1(%1))
    DCT_8_INV_ROW_XMM(2*16(%0), 2*16(%0), 64*2(%2), 8*2(%1))
    DCT_8_INV_ROW_XMM(3*16(%0), 3*16(%0), 64*3(%2), 8*3(%1))
    DCT_8_INV_ROW_XMM(4*16(%0), 4*16(%0), 64*0(%2), 8*4(%1))
    DCT_8_INV_ROW_XMM(5*16(%0), 5*16(%0), 64*3(%2), 8*5(%1))
    DCT_8_INV_ROW_XMM(6*16(%0), 6*16(%0), 64*2(%2), 8*6(%1))
    DCT_8_INV_ROW_XMM(7*16(%0), 7*16(%0), 64*1(%2), 8*7(%1))

            //# Process the columns (4 at a time)
    DCT_8_INV_COL(0(%0), 0(%0))
    DCT_8_INV_COL(8(%0), 8(%0))
    :: "r"(block), "r"(rounder_0), "r"(tab_i_04_xmm), "r"(tg_1_16));
}

