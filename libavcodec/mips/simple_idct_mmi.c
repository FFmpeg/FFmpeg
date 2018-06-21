/*
 * Loongson SIMD optimized simple idct
 *
 * Copyright (c) 2015 Loongson Technology Corporation Limited
 * Copyright (c) 2015 Zhou Xiaoyong <zhouxiaoyong@loongson.cn>
 *                    Zhang Shuangshuang <zhangshuangshuang@ict.ac.cn>
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

#include "idctdsp_mips.h"
#include "constants.h"

#define C0 23170 //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define C1 22725 //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define C2 21407 //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define C3 19266 //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define C4 16383 //cos(i*M_PI/16)*sqrt(2)*(1<<14) - 0.5
#define C5 12873 //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define C6 8867  //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define C7 4520  //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5

#define ROW_SHIFT 11
#define COL_SHIFT 20

DECLARE_ALIGNED(8, static const int16_t, coeffs)[]= {
    1<<(ROW_SHIFT-1),   0, 1<<(ROW_SHIFT-1),   0,
    1<<(ROW_SHIFT-1),   1, 1<<(ROW_SHIFT-1),   0,
                  C4,  C4,               C4,  C4,
                  C4, -C4,               C4, -C4,
                  C2,  C6,               C2,  C6,
                  C6, -C2,               C6, -C2,
                  C1,  C3,               C1,  C3,
                  C5,  C7,               C5,  C7,
                  C3, -C7,               C3, -C7,
                 -C1, -C5,              -C1, -C5,
                  C5, -C1,               C5, -C1,
                  C7,  C3,               C7,  C3,
                  C7, -C5,               C7, -C5,
                  C3, -C1,               C3, -C1
};

void ff_simple_idct_mmi(int16_t *block)
{
        DECLARE_ALIGNED(8, int64_t, align_tmp)[16];
        int16_t * const temp= (int16_t*)align_tmp;

        __asm__ volatile (
#undef  DC_COND_IDCT
#define DC_COND_IDCT(src0, src4, src1, src5, dst, rounder, rarg, shift)      \
        "ldc1 $f0, " #src0 "            \n\t" /* R4     R0      r4      r0 */\
        "ldc1 $f2, " #src4 "            \n\t" /* R6     R2      r6      r2 */\
        "ldc1 $f4, " #src1 "            \n\t" /* R3     R1      r3      r1 */\
        "ldc1 $f6, " #src5 "            \n\t" /* R7     R5      r7      r5 */\
        "ldc1 $f8, %3                   \n\t"                                \
        "and  $f8, $f8, $f0             \n\t"                                \
        "or $f8, $f8, $f2               \n\t"                                \
        "or $f8, $f8, $f4               \n\t"                                \
        "or $f8, $f8, $f6               \n\t"                                \
        "packsswh $f8, $f8, $f8         \n\t"                                \
        "li $11, " #shift "             \n\t"                                \
        "mfc1 $10, $f8                  \n\t"                                \
        "mtc1 $11, $f18                 \n\t"                                \
        "beqz $10, 1f                   \n\t"                                \
        "ldc1 $f8, 16(%2)               \n\t" /* C4     C4      C4      C4 */\
        "pmaddhw $f8, $f8, $f0          \n\t" /* C4R4+C4R0      C4r4+C4r0  */\
        "ldc1 $f10, 24(%2)              \n\t" /* -C4    C4      -C4     C4 */\
        "pmaddhw $f0, $f0, $f10         \n\t" /* -C4R4+C4R0     -C4r4+C4r0 */\
        "ldc1 $f10, 32(%2)              \n\t" /* C6     C2      C6      C2 */\
        "pmaddhw $f10, $f10, $f2        \n\t" /* C6R6+C2R2      C6r6+C2r2  */\
        "ldc1 $f12, 40(%2)              \n\t" /* -C2    C6      -C2     C6 */\
        "pmaddhw $f2, $f2, $f12         \n\t" /* -C2R6+C6R2     -C2r6+C6r2 */\
        "ldc1 $f14, 48(%2)              \n\t" /* C3     C1      C3      C1 */\
        "ldc1 $f16, " #rarg "           \n\t"                                \
        "pmaddhw $f14, $f14, $f4        \n\t" /* C3R3+C1R1      C3r3+C1r1  */\
        #rounder " $f8, $f8, $f16       \n\t"                                \
        "mov.d $f12, $f8                \n\t" /* C4R4+C4R0      C4r4+C4r0  */\
        "paddw $f8, $f8, $f10           \n\t" /* A0             a0         */\
        "psubw $f12, $f12, $f10         \n\t" /* A3             a3         */\
        "ldc1 $f10, 56(%2)              \n\t" /* C7     C5      C7      C5 */\
        "ldc1 $f16, " #rarg "           \n\t"                                \
        "pmaddhw $f10, $f10, $f6        \n\t" /* C7R7+C5R5      C7r7+C5r5  */\
        #rounder " $f0, $f0, $f16       \n\t"                                \
        "paddw $f2, $f2, $f0            \n\t" /* A1             a1         */\
        "ldc1 $f16, 64(%2)              \n\t"                                \
        "paddw $f0, $f0, $f0            \n\t"                                \
        "psubw $f0, $f0, $f2            \n\t" /* A2             a2         */\
        "pmaddhw $f4, $f4, $f16         \n\t" /* -C7R3+C3R1     -C7r3+C3r1 */\
        "paddw $f14, $f14, $f10         \n\t" /* B0             b0         */\
        "ldc1 $f10, 72(%2)              \n\t" /* -C5    -C1     -C5    -C1 */\
        "pmaddhw $f10, $f10, $f6        \n\t" /* -C5R7-C1R5     -C5r7-C1r5 */\
        "paddw $f14, $f14, $f8          \n\t" /* A0+B0          a0+b0      */\
        "paddw $f8, $f8, $f8            \n\t" /* 2A0            2a0        */\
        "psubw $f8, $f8, $f14           \n\t" /* A0-B0          a0-b0      */\
        "paddw $f10, $f10, $f4          \n\t" /* B1             b1         */\
        "psraw $f14, $f14, $f18         \n\t"                                \
        "psraw $f8, $f8, $f18           \n\t"                                \
        "mov.d $f4, $f2                 \n\t" /* A1             a1         */\
        "paddw $f2, $f2, $f10           \n\t" /* A1+B1          a1+b1      */\
        "psubw $f4, $f4, $f10           \n\t" /* A1-B1          a1-b1      */\
        "psraw $f2, $f2, $f18           \n\t"                                \
        "psraw $f4, $f4, $f18           \n\t"                                \
        "packsswh $f14, $f14, $f2       \n\t" /* A1+B1 a1+b1 A0+B0 a0+b0   */\
        "packsswh $f4, $f4, $f8         \n\t" /* A0-B0 a0-b0 A1-B1 a1-b1   */\
        "sdc1 $f14, " #dst "            \n\t"                                \
        "ldc1 $f2, " #src1 "            \n\t" /* R3     R1      r3      r1 */\
        "ldc1 $f8, 80(%2)               \n\t" /* -C1    C5      -C1     C5 */\
        "sdc1 $f4, 24+" #dst "          \n\t"                                \
        "pmaddhw $f8, $f8, $f2          \n\t" /* -C1R3+C5R1     -C1r3+C5r1 */\
        "ldc1 $f16, 96(%2)              \n\t"                                \
        "ldc1 $f14, 88(%2)              \n\t" /* C3     C7      C3      C7 */\
        "pmaddhw $f2, $f2, $f16         \n\t" /* -C5R3+C7R1     -C5r3+C7r1 */\
        "pmaddhw $f14, $f14, $f6        \n\t" /* C3R7+C7R5      C3r7+C7r5  */\
        "ldc1 $f16, 104(%2)             \n\t"                                \
        "mov.d $f4, $f0                 \n\t" /* A2             a2         */\
        "pmaddhw $f6, $f6, $f16         \n\t" /* -C1R7+C3R5     -C1r7+C3r5 */\
        "paddw $f8, $f8, $f14           \n\t" /* B2             b2         */\
        "paddw $f4, $f4, $f8            \n\t" /* A2+B2          a2+b2      */\
        "psubw $f0, $f0, $f8            \n\t" /* a2-B2          a2-b2      */\
        "psraw $f4, $f4, $f18           \n\t"                                \
        "psraw $f0, $f0, $f18           \n\t"                                \
        "mov.d $f8, $f12                \n\t" /* A3             a3         */\
        "paddw $f6, $f6, $f2            \n\t" /* B3             b3         */\
        "paddw $f12, $f12, $f6          \n\t" /* A3+B3          a3+b3      */\
        "psubw $f8, $f8, $f6            \n\t" /* a3-B3          a3-b3      */\
        "psraw $f12, $f12, $f18         \n\t"                                \
        "packsswh $f4, $f4, $f12        \n\t" /* A3+B3 a3+b3 A2+B2 a2+b2   */\
        "sdc1 $f4, 8+" #dst "           \n\t"                                \
        "psraw $f8, $f8, $f18           \n\t"                                \
        "packsswh $f8, $f8, $f0         \n\t" /* A2-B2 a2-b2 A3-B3 a3-b3   */\
        "sdc1 $f8, 16+" #dst "          \n\t"                                \
        "b 2f                           \n\t"                                \
        "1:                             \n\t"                                \
        "li $10, 16                     \n\t"                                \
        "mtc1 $10, $f16                 \n\t"                                \
        "psllw $f0, $f0, $f16           \n\t"                                \
        "ldc1 $f16, %4                  \n\t"                                \
        "paddw $f0, $f0, $f16           \n\t"                                \
        "li $10, 13                     \n\t"                                \
        "mtc1 $10, $f16                 \n\t"                                \
        "psraw $f0, $f0, $f16           \n\t"                                \
        "packsswh $f0, $f0, $f0         \n\t"                                \
        "sdc1 $f0, " #dst "             \n\t"                                \
        "sdc1 $f0, 8+" #dst "           \n\t"                                \
        "sdc1 $f0, 16+" #dst "          \n\t"                                \
        "sdc1 $f0, 24+" #dst "          \n\t"                                \
        "2:                             \n\t"

#undef  Z_COND_IDCT
#define Z_COND_IDCT(src0, src4, src1, src5, dst, rounder, rarg, shift, bt)   \
        "ldc1 $f0, " #src0 "            \n\t" /* R4     R0      r4      r0 */\
        "ldc1 $f2, " #src4 "            \n\t" /* R6     R2      r6      r2 */\
        "ldc1 $f4, " #src1 "            \n\t" /* R3     R1      r3      r1 */\
        "ldc1 $f6, " #src5 "            \n\t" /* R7     R5      r7      r5 */\
        "mov.d $f8, $f0                 \n\t"                                \
        "or $f8, $f8, $f2               \n\t"                                \
        "or $f8, $f8, $f4               \n\t"                                \
        "or $f8, $f8, $f6               \n\t"                                \
        "packsswh $f8, $f8, $f8         \n\t"                                \
        "mfc1 $10, $f8                  \n\t"                                \
        "beqz $10, " #bt "              \n\t"                                \
        "ldc1 $f8, 16(%2)               \n\t" /* C4     C4      C4      C4 */\
        "pmaddhw $f8, $f8, $f0          \n\t" /* C4R4+C4R0      C4r4+C4r0  */\
        "ldc1 $f10, 24(%2)              \n\t" /* -C4    C4      -C4     C4 */\
        "pmaddhw $f0, $f0, $f10         \n\t" /* -C4R4+C4R0     -C4r4+C4r0 */\
        "ldc1 $f10, 32(%2)              \n\t" /* C6     C2      C6      C2 */\
        "pmaddhw $f10, $f10, $f2        \n\t" /* C6R6+C2R2      C6r6+C2r2  */\
        "ldc1 $f12, 40(%2)              \n\t" /* -C2    C6      -C2     C6 */\
        "pmaddhw $f2, $f2, $f12         \n\t" /* -C2R6+C6R2     -C2r6+C6r2 */\
        "ldc1 $f14, 48(%2)              \n\t" /* C3     C1      C3      C1 */\
        "ldc1 $f16, " #rarg "           \n\t"                                \
        "pmaddhw $f14, $f14, $f4        \n\t" /* C3R3+C1R1      C3r3+C1r1  */\
        #rounder " $f8, $f8, $f16       \n\t"                                \
        "mov.d $f12, $f8                \n\t" /* C4R4+C4R0      C4r4+C4r0  */\
        "paddw $f8, $f8, $f10           \n\t" /* A0             a0         */\
        "psubw $f12, $f12, $f10         \n\t" /* A3             a3         */\
        "ldc1 $f10, 56(%2)              \n\t" /* C7     C5      C7      C5 */\
        "ldc1 $f16, " #rarg "           \n\t"                                \
        "pmaddhw $f10, $f10, $f6        \n\t" /* C7R7+C5R5      C7r7+C5r5  */\
        #rounder " $f0, $f0, $f16       \n\t"                                \
        "paddw $f2, $f2, $f0            \n\t" /* A1             a1         */\
        "paddw $f0, $f0, $f0            \n\t"                                \
        "ldc1 $f16, 64(%2)              \n\t"                                \
        "psubw $f0, $f0, $f2            \n\t" /* A2             a2         */\
        "pmaddhw $f4, $f4, $f16         \n\t" /* -C7R3+C3R1     -C7r3+C3r1 */\
        "paddw $f14, $f14, $f10         \n\t" /* B0             b0         */\
        "ldc1 $f10, 72(%2)              \n\t" /* -C5    -C1     -C5    -C1 */\
        "pmaddhw $f10, $f10, $f6        \n\t" /* -C5R7-C1R5     -C5r7-C1r5 */\
        "paddw $f14, $f14, $f8          \n\t" /* A0+B0          a0+b0      */\
        "paddw $f8, $f8, $f8            \n\t" /* 2A0            2a0        */\
        "li $10, " #shift "             \n\t"                                \
        "psubw $f8, $f8, $f14           \n\t" /* A0-B0          a0-b0      */\
        "mtc1 $10, $f18                 \n\t"                                \
        "paddw $f10, $f10, $f4          \n\t" /* B1             b1         */\
        "psraw $f14, $f14, $f18         \n\t"                                \
        "psraw $f8, $f8, $f18           \n\t"                                \
        "mov.d $f4, $f2                 \n\t" /* A1             a1         */\
        "paddw $f2, $f2, $f10           \n\t" /* A1+B1          a1+b1      */\
        "psubw $f4, $f4, $f10           \n\t" /* A1-B1          a1-b1      */\
        "psraw $f2, $f2, $f18           \n\t"                                \
        "psraw $f4, $f4, $f18           \n\t"                                \
        "packsswh $f14, $f14, $f2       \n\t" /* A1+B1 a1+b1 A0+B0 a0+b0   */\
        "packsswh $f4, $f4, $f8         \n\t" /* A0-B0 a0-b0 A1-B1 a1-b1   */\
        "sdc1 $f14, " #dst "            \n\t"                                \
        "ldc1 $f2, " #src1 "            \n\t" /* R3     R1      r3      r1 */\
        "ldc1 $f8, 80(%2)               \n\t" /* -C1    C5      -C1     C5 */\
        "sdc1 $f4, 24+" #dst "          \n\t"                                \
        "pmaddhw $f8, $f8, $f2          \n\t" /* -C1R3+C5R1     -C1r3+C5r1 */\
        "ldc1 $f16, 96(%2)              \n\t"                                \
        "ldc1 $f14, 88(%2)              \n\t" /* C3     C7      C3      C7 */\
        "pmaddhw $f2, $f2, $f16         \n\t" /* -C5R3+C7R1     -C5r3+C7r1 */\
        "pmaddhw $f14, $f14, $f6        \n\t" /* C3R7+C7R5      C3r7+C7r5  */\
        "ldc1 $f16, 104(%2)             \n\t"                                \
        "mov.d $f4, $f0                 \n\t" /* A2             a2         */\
        "pmaddhw $f6, $f6, $f16         \n\t" /* -C1R7+C3R5     -C1r7+C3r5 */\
        "paddw $f8, $f8, $f14           \n\t" /* B2             b2         */\
        "paddw $f4, $f4, $f8            \n\t" /* A2+B2          a2+b2      */\
        "psubw $f0, $f0, $f8            \n\t" /* a2-B2          a2-b2      */\
        "psraw $f4, $f4, $f18           \n\t"                                \
        "psraw $f0, $f0, $f18           \n\t"                                \
        "mov.d $f8, $f12                \n\t" /* A3             a3         */\
        "paddw $f6, $f6, $f2            \n\t" /* B3             b3         */\
        "paddw $f12, $f12, $f6          \n\t" /* A3+B3          a3+b3      */\
        "psubw $f8, $f8, $f6            \n\t" /* a3-B3          a3-b3      */\
        "psraw $f12, $f12, $f18         \n\t"                                \
        "packsswh $f4, $f4, $f12        \n\t" /* A3+B3 a3+b3 A2+B2 a2+b2   */\
        "sdc1 $f4, 8+" #dst "           \n\t"                                \
        "psraw $f8, $f8, $f18           \n\t"                                \
        "packsswh $f8, $f8, $f0         \n\t" /* A2-B2 a2-b2 A3-B3 a3-b3   */\
        "sdc1 $f8, 16+" #dst "          \n\t"                                \

        //IDCT(       src0,   src4,   src1,   src5,    dst,     rounder, shift)
        DC_COND_IDCT(0(%0),  8(%0), 16(%0), 24(%0),  0(%1), paddw,8(%2), 11)
        Z_COND_IDCT(32(%0), 40(%0), 48(%0), 56(%0), 32(%1), paddw,(%2), 11, 4f)
        Z_COND_IDCT(64(%0), 72(%0), 80(%0), 88(%0), 64(%1), paddw,(%2), 11, 2f)
        Z_COND_IDCT(96(%0),104(%0),112(%0),120(%0), 96(%1), paddw,(%2), 11, 1f)

#undef  IDCT
#define IDCT(src0, src4, src1, src5, dst, shift)                             \
        "ldc1 $f0, " #src0 "            \n\t" /* R4     R0      r4      r0 */\
        "ldc1 $f2, " #src4 "            \n\t" /* R6     R2      r6      r2 */\
        "ldc1 $f4, " #src1 "            \n\t" /* R3     R1      r3      r1 */\
        "ldc1 $f6, " #src5 "            \n\t" /* R7     R5      r7      r5 */\
        "ldc1 $f8, 16(%2)               \n\t" /* C4     C4      C4      C4 */\
        "pmaddhw $f8, $f8, $f0          \n\t" /* C4R4+C4R0      C4r4+C4r0  */\
        "ldc1 $f10, 24(%2)              \n\t" /* -C4    C4      -C4     C4 */\
        "pmaddhw $f0, $f0, $f10         \n\t" /* -C4R4+C4R0     -C4r4+C4r0 */\
        "ldc1 $f10, 32(%2)              \n\t" /* C6     C2      C6      C2 */\
        "pmaddhw $f10, $f10, $f2        \n\t" /* C6R6+C2R2      C6r6+C2r2  */\
        "ldc1 $f12, 40(%2)              \n\t" /* -C2    C6      -C2     C6 */\
        "pmaddhw $f2, $f2, $f12         \n\t" /* -C2R6+C6R2     -C2r6+C6r2 */\
        "mov.d $f12, $f8                \n\t" /* C4R4+C4R0      C4r4+C4r0  */\
        "ldc1 $f14, 48(%2)              \n\t" /* C3     C1      C3      C1 */\
        "pmaddhw $f14, $f14, $f4        \n\t" /* C3R3+C1R1      C3r3+C1r1  */\
        "paddw $f8, $f8, $f10           \n\t" /* A0             a0         */\
        "psubw $f12, $f12, $f10         \n\t" /* A3             a3         */\
        "mov.d $f10, $f0                \n\t" /* -C4R4+C4R0     -C4r4+C4r0 */\
        "paddw $f0, $f0, $f2            \n\t" /* A1             a1         */\
        "psubw $f10, $f10, $f2          \n\t" /* A2             a2         */\
        "ldc1 $f2, 56(%2)               \n\t" /* C7     C5      C7      C5 */\
        "ldc1 $f16, 64(%2)              \n\t"                                \
        "pmaddhw $f2, $f2, $f6          \n\t" /* C7R7+C5R5      C7r7+C5r5  */\
        "pmaddhw $f4, $f4, $f16         \n\t" /* -C7R3+C3R1     -C7r3+C3r1 */\
        "li $10, " #shift "             \n\t"                                \
        "paddw $f14, $f14, $f2          \n\t" /* B0             b0         */\
        "ldc1 $f2, 72(%2)               \n\t" /* -C5    -C1     -C5    -C1 */\
        "mtc1 $10, $f18                 \n\t"                                \
        "pmaddhw $f2, $f2, $f6          \n\t" /* -C5R7-C1R5     -C5r7-C1r5 */\
        "paddw $f14, $f14, $f8          \n\t" /* A0+B0          a0+b0      */\
        "paddw $f8, $f8, $f8            \n\t" /* 2A0            2a0        */\
        "psubw $f8, $f8, $f14           \n\t" /* A0-B0          a0-b0      */\
        "paddw $f2, $f2, $f4            \n\t" /* B1             b1         */\
        "psraw $f14, $f14, $f18         \n\t"                                \
        "psraw $f8, $f8, $f18           \n\t"                                \
        "mov.d $f4, $f0                 \n\t" /* A1             a1         */\
        "paddw $f0, $f0, $f2            \n\t" /* A1+B1          a1+b1      */\
        "psubw $f4, $f4, $f2            \n\t" /* A1-B1          a1-b1      */\
        "psraw $f0, $f0, $f18           \n\t"                                \
        "psraw $f4, $f4, $f18           \n\t"                                \
        "packsswh $f14, $f14, $f14      \n\t" /* A0+B0          a0+b0      */\
        "swc1 $f14, " #dst "            \n\t"                                \
        "packsswh $f0, $f0, $f0         \n\t" /* A1+B1          a1+b1      */\
        "swc1 $f0, 16+" #dst "          \n\t"                                \
        "packsswh $f4, $f4, $f4         \n\t" /* A1-B1          a1-b1      */\
        "swc1 $f4, 96+" #dst "          \n\t"                                \
        "packsswh $f8, $f8, $f8         \n\t" /* A0-B0          a0-b0      */\
        "swc1 $f8, 112+" #dst "         \n\t"                                \
        "ldc1 $f0, " #src1 "            \n\t" /* R3     R1      r3      r1 */\
        "ldc1 $f8, 80(%2)               \n\t" /* -C1    C5      -C1     C5 */\
        "pmaddhw $f8, $f8, $f0          \n\t" /* -C1R3+C5R1     -C1r3+C5r1 */\
        "ldc1 $f16, 96(%2)              \n\t"                                \
        "ldc1 $f14, 88(%2)              \n\t" /* C3     C7      C3      C7 */\
        "pmaddhw $f0, $f0, $f16         \n\t" /* -C5R3+C7R1     -C5r3+C7r1 */\
        "pmaddhw $f14, $f14, $f6        \n\t" /* C3R7+C7R5      C3r7+C7r5  */\
        "ldc1 $f16, 104(%2)             \n\t"                                \
        "mov.d $f4, $f10                \n\t" /* A2             a2         */\
        "pmaddhw $f6, $f6, $f16         \n\t" /* -C1R7+C3R5     -C1r7+C3r5 */\
        "paddw $f8, $f8, $f14           \n\t" /* B2             b2         */\
        "paddw $f4, $f4, $f8            \n\t" /* A2+B2          a2+b2      */\
        "psubw $f10, $f10, $f8          \n\t" /* a2-B2          a2-b2      */\
        "psraw $f4, $f4, $f18           \n\t"                                \
        "psraw $f10, $f10, $f18         \n\t"                                \
        "mov.d $f8, $f12                \n\t" /* A3             a3         */\
        "paddw $f6, $f6, $f0            \n\t" /* B3             b3         */\
        "paddw $f12, $f12, $f6          \n\t" /* A3+B3          a3+b3      */\
        "psubw $f8, $f8, $f6            \n\t" /* a3-B3          a3-b3      */\
        "psraw $f12, $f12, $f18         \n\t"                                \
        "psraw $f8, $f8, $f18           \n\t"                                \
        "packsswh $f4, $f4, $f4         \n\t" /* A2+B2          a2+b2      */\
        "packsswh $f12, $f12, $f12      \n\t" /* A3+B3          a3+b3      */\
        "swc1 $f4, 32+" #dst "          \n\t"                                \
        "packsswh $f8, $f8, $f8         \n\t" /* A3-B3          a3-b3      */\
        "packsswh $f10, $f10, $f10      \n\t" /* A2-B2          a2-b2      */\
        "swc1 $f12, 48+" #dst "         \n\t"                                \
        "swc1 $f8, 64+" #dst "          \n\t"                                \
        "swc1 $f10, 80+" #dst "         \n\t"

        //IDCT(  src0,   src4,   src1,    src5,    dst, shift)
        IDCT(    (%1), 64(%1), 32(%1),  96(%1),  0(%0),    20)
        IDCT(   8(%1), 72(%1), 40(%1), 104(%1),  4(%0),    20)
        IDCT(  16(%1), 80(%1), 48(%1), 112(%1),  8(%0),    20)
        IDCT(  24(%1), 88(%1), 56(%1), 120(%1), 12(%0),    20)
        "b 9f                           \n\t"

        "# .p2align 4                   \n\t"
        "4:                             \n\t"
        Z_COND_IDCT(64(%0), 72(%0), 80(%0), 88(%0), 64(%1),paddw,(%2), 11, 6f)
        Z_COND_IDCT(96(%0),104(%0),112(%0),120(%0), 96(%1),paddw,(%2), 11, 5f)

#undef  IDCT
#define IDCT(src0, src4, src1, src5, dst, shift)                             \
        "ldc1 $f0, " #src0 "            \n\t" /* R4     R0      r4      r0 */\
        "ldc1 $f2, " #src4 "            \n\t" /* R6     R2      r6      r2 */\
        "ldc1 $f6, " #src5 "            \n\t" /* R7     R5      r7      r5 */\
        "ldc1 $f8, 16(%2)               \n\t" /* C4     C4      C4      C4 */\
        "pmaddhw $f8, $f8, $f0          \n\t" /* C4R4+C4R0      C4r4+C4r0  */\
        "ldc1 $f10, 24(%2)              \n\t" /* -C4    C4      -C4     C4 */\
        "pmaddhw $f0, $f0, $f10         \n\t" /* -C4R4+C4R0     -C4r4+C4r0 */\
        "ldc1 $f10, 32(%2)              \n\t" /* C6     C2      C6      C2 */\
        "pmaddhw $f10, $f10, $f2        \n\t" /* C6R6+C2R2      C6r6+C2r2  */\
        "ldc1 $f12, 40(%2)              \n\t" /* -C2    C6      -C2     C6 */\
        "pmaddhw $f2, $f2, $f12         \n\t" /* -C2R6+C6R2     -C2r6+C6r2 */\
        "mov.d $f12, $f8                \n\t" /* C4R4+C4R0      C4r4+C4r0  */\
        "paddw $f8, $f8, $f10           \n\t" /* A0             a0         */\
        "psubw $f12, $f12, $f10         \n\t" /* A3             a3         */\
        "mov.d $f10, $f0                \n\t" /* -C4R4+C4R0     -C4r4+C4r0 */\
        "paddw $f0, $f0, $f2            \n\t" /* A1             a1         */\
        "psubw $f10, $f10, $f2          \n\t" /* A2             a2         */\
        "ldc1 $f2, 56(%2)               \n\t" /* C7     C5      C7      C5 */\
        "li $10, " #shift "             \n\t"                                \
        "pmaddhw $f2, $f2, $f6          \n\t" /* C7R7+C5R5      C7r7+C5r5  */\
        "ldc1 $f14, 72(%2)              \n\t" /* -C5    -C1     -C5    -C1 */\
        "mtc1 $10, $f18                 \n\t"                                \
        "pmaddhw $f14, $f14, $f6        \n\t" /* -C5R7-C1R5     -C5r7-C1r5 */\
        "paddw $f2, $f2, $f8            \n\t" /* A0+B0          a0+b0      */\
        "paddw $f8, $f8, $f8            \n\t" /* 2A0            2a0        */\
        "psubw $f8, $f8, $f2            \n\t" /* A0-B0          a0-b0      */\
        "psraw $f2, $f2, $f18           \n\t"                                \
        "psraw $f8, $f8, $f18           \n\t"                                \
        "mov.d $f4, $f0                 \n\t" /* A1             a1         */\
        "paddw $f0, $f0, $f14           \n\t" /* A1+B1          a1+b1      */\
        "psubw $f4, $f4, $f14           \n\t" /* A1-B1          a1-b1      */\
        "psraw $f0, $f0, $f18           \n\t"                                \
        "psraw $f4, $f4, $f18           \n\t"                                \
        "packsswh $f2, $f2, $f2         \n\t" /* A0+B0          a0+b0      */\
        "swc1 $f2, " #dst "             \n\t"                                \
        "packsswh $f0, $f0, $f0         \n\t" /* A1+B1          a1+b1      */\
        "swc1 $f0, 16+" #dst "          \n\t"                                \
        "packsswh $f4, $f4, $f4         \n\t" /* A1-B1          a1-b1      */\
        "swc1 $f4, 96+" #dst "          \n\t"                                \
        "packsswh $f8, $f8, $f8         \n\t" /* A0-B0          a0-b0      */\
        "swc1 $f8, 112+" #dst "         \n\t"                                \
        "ldc1 $f2, 88(%2)               \n\t" /* C3     C7      C3      C7 */\
        "ldc1 $f16, 104(%2)             \n\t"                                \
        "pmaddhw $f2, $f2, $f6          \n\t" /* C3R7+C7R5      C3r7+C7r5  */\
        "mov.d $f4, $f10                \n\t" /* A2             a2         */\
        "pmaddhw $f6, $f6, $f16         \n\t" /* -C1R7+C3R5     -C1r7+C3r5 */\
        "paddw $f4, $f4, $f2            \n\t" /* A2+B2          a2+b2      */\
        "psubw $f10, $f10, $f2          \n\t" /* a2-B2          a2-b2      */\
        "psraw $f4, $f4, $f18           \n\t"                                \
        "psraw $f10, $f10, $f18         \n\t"                                \
        "mov.d $f2, $f12                \n\t" /* A3             a3         */\
        "paddw $f12, $f12, $f6          \n\t" /* A3+B3          a3+b3      */\
        "psubw $f2, $f2, $f6            \n\t" /* a3-B3          a3-b3      */\
        "psraw $f12, $f12, $f18         \n\t"                                \
        "psraw $f2, $f2, $f18           \n\t"                                \
        "packsswh $f4, $f4, $f4         \n\t" /* A2+B2          a2+b2      */\
        "packsswh $f12, $f12, $f12      \n\t" /* A3+B3          a3+b3      */\
        "swc1 $f4, 32+" #dst "          \n\t"                                \
        "packsswh $f2, $f2, $f2         \n\t" /* A3-B3          a3-b3      */\
        "packsswh $f10, $f10, $f10      \n\t" /* A2-B2          a2-b2      */\
        "swc1 $f12, 48+" #dst "         \n\t"                                \
        "swc1 $f2, 64+" #dst "          \n\t"                                \
        "swc1 $f10, 80+" #dst "         \n\t"

        //IDCT(  src0,   src4,   src1,    src5,    dst, shift)
        IDCT(    (%1), 64(%1), 32(%1),  96(%1),  0(%0),    20)
        IDCT(   8(%1), 72(%1), 40(%1), 104(%1),  4(%0),    20)
        IDCT(  16(%1), 80(%1), 48(%1), 112(%1),  8(%0),    20)
        IDCT(  24(%1), 88(%1), 56(%1), 120(%1), 12(%0),    20)
        "b 9f                           \n\t"

        "# .p2align 4                   \n\t"
        "6:                             \n\t"
        Z_COND_IDCT(96(%0),104(%0),112(%0),120(%0), 96(%1),paddw,(%2), 11, 7f)

#undef  IDCT
#define IDCT(src0, src4, src1, src5, dst, shift)                             \
        "ldc1 $f0, " #src0 "            \n\t" /* R4     R0      r4      r0 */\
        "ldc1 $f6, " #src5 "            \n\t" /* R7     R5      r7      r5 */\
        "ldc1 $f8, 16(%2)               \n\t" /* C4     C4      C4      C4 */\
        "pmaddhw $f8, $f8, $f0          \n\t" /* C4R4+C4R0      C4r4+C4r0  */\
        "ldc1 $f10, 24(%2)              \n\t" /* -C4    C4      -C4     C4 */\
        "pmaddhw $f0, $f0, $f10         \n\t" /* -C4R4+C4R0     -C4r4+C4r0 */\
        "mov.d $f12, $f8                \n\t" /* C4R4+C4R0      C4r4+C4r0  */\
        "mov.d $f10, $f0                \n\t" /* -C4R4+C4R0     -C4r4+C4r0 */\
        "ldc1 $f2, 56(%2)               \n\t" /* C7     C5      C7      C5 */\
        "pmaddhw $f2, $f2, $f6          \n\t" /* C7R7+C5R5      C7r7+C5r5  */\
        "ldc1 $f14, 72(%2)              \n\t" /* -C5    -C1     -C5    -C1 */\
        "li $10, " #shift "             \n\t"                                \
        "pmaddhw $f14, $f14, $f6        \n\t" /* -C5R7-C1R5     -C5r7-C1r5 */\
        "paddw $f2, $f2, $f8            \n\t" /* A0+B0          a0+b0      */\
        "mtc1 $10, $f18                 \n\t"                                \
        "paddw $f8, $f8, $f8            \n\t" /* 2A0            2a0        */\
        "psubw $f8, $f8, $f2            \n\t" /* A0-B0          a0-b0      */\
        "psraw $f2, $f2, $f18           \n\t"                                \
        "psraw $f8, $f8, $f18           \n\t"                                \
        "mov.d $f4, $f0                 \n\t" /* A1             a1         */\
        "paddw $f0, $f0, $f14           \n\t" /* A1+B1          a1+b1      */\
        "psubw $f4, $f4, $f14           \n\t" /* A1-B1          a1-b1      */\
        "psraw $f0, $f0, $f18           \n\t"                                \
        "psraw $f4, $f4, $f18           \n\t"                                \
        "packsswh $f2, $f2, $f2         \n\t" /* A0+B0          a0+b0      */\
        "swc1 $f2, " #dst "             \n\t"                                \
        "packsswh $f0, $f0, $f0         \n\t" /* A1+B1          a1+b1      */\
        "swc1 $f0, 16+" #dst "          \n\t"                                \
        "packsswh $f4, $f4, $f4         \n\t" /* A1-B1          a1-b1      */\
        "swc1 $f4, 96+" #dst "          \n\t"                                \
        "packsswh $f8, $f8, $f8         \n\t" /* A0-B0          a0-b0      */\
        "swc1 $f8, 112+" #dst "         \n\t"                                \
        "ldc1 $f2, 88(%2)               \n\t" /* C3     C7      C3      C7 */\
        "ldc1 $f16, 104(%2)             \n\t"                                \
        "pmaddhw $f2, $f2, $f6          \n\t" /* C3R7+C7R5      C3r7+C7r5  */\
        "mov.d $f4, $f10                \n\t" /* A2             a2         */\
        "pmaddhw $f6, $f6, $f16         \n\t" /* -C1R7+C3R5     -C1r7+C3r5 */\
        "paddw $f4, $f4, $f2            \n\t" /* A2+B2          a2+b2      */\
        "psubw $f10, $f10, $f2          \n\t" /* a2-B2          a2-b2      */\
        "psraw $f4, $f4, $f18           \n\t"                                \
        "psraw $f10, $f10, $f18         \n\t"                                \
        "mov.d $f2, $f12                \n\t" /* A3             a3         */\
        "paddw $f12, $f12, $f6          \n\t" /* A3+B3          a3+b3      */\
        "psubw $f2, $f2, $f6            \n\t" /* a3-B3          a3-b3      */\
        "psraw $f12, $f12, $f18         \n\t"                                \
        "psraw $f2, $f2, $f18           \n\t"                                \
        "packsswh $f4, $f4, $f4         \n\t" /* A2+B2          a2+b2      */\
        "packsswh $f12, $f12, $f12      \n\t" /* A3+B3          a3+b3      */\
        "swc1 $f4, 32+" #dst "          \n\t"                                \
        "packsswh $f2, $f2, $f2         \n\t" /* A3-B3          a3-b3      */\
        "packsswh $f10, $f10, $f10      \n\t" /* A2-B2          a2-b2      */\
        "swc1 $f12, 48+" #dst "         \n\t"                                \
        "swc1 $f2, 64+" #dst "          \n\t"                                \
        "swc1 $f10, 80+" #dst "         \n\t"

        //IDCT(  src0,   src4,   src1,    src5,    dst, shift)
        IDCT(    (%1), 64(%1), 32(%1),  96(%1),  0(%0),    20)
        IDCT(   8(%1), 72(%1), 40(%1), 104(%1),  4(%0),    20)
        IDCT(  16(%1), 80(%1), 48(%1), 112(%1),  8(%0),    20)
        IDCT(  24(%1), 88(%1), 56(%1), 120(%1), 12(%0),    20)
        "b 9f                           \n\t"

        "# .p2align 4                   \n\t"
        "2:                             \n\t"
        Z_COND_IDCT(96(%0),104(%0),112(%0),120(%0), 96(%1),paddw,(%2), 11, 3f)

#undef  IDCT
#define IDCT(src0, src4, src1, src5, dst, shift)                             \
        "ldc1 $f0, " #src0 "            \n\t" /* R4     R0      r4      r0 */\
        "ldc1 $f4, " #src1 "            \n\t" /* R3     R1      r3      r1 */\
        "ldc1 $f6, " #src5 "            \n\t" /* R7     R5      r7      r5 */\
        "ldc1 $f8, 16(%2)               \n\t" /* C4     C4      C4      C4 */\
        "pmaddhw $f8, $f8, $f0          \n\t" /* C4R4+C4R0      C4r4+C4r0  */\
        "ldc1 $f10, 24(%2)              \n\t" /* -C4    C4      -C4     C4 */\
        "pmaddhw $f0, $f0, $f10         \n\t" /* -C4R4+C4R0     -C4r4+C4r0 */\
        "mov.d $f12, $f8                \n\t" /* C4R4+C4R0      C4r4+C4r0  */\
        "ldc1 $f14, 48(%2)              \n\t" /* C3     C1      C3      C1 */\
        "pmaddhw $f14, $f14, $f4        \n\t" /* C3R3+C1R1      C3r3+C1r1  */\
        "mov.d $f10, $f0                \n\t" /* -C4R4+C4R0     -C4r4+C4r0 */\
        "ldc1 $f2, 56(%2)               \n\t" /* C7     C5      C7      C5 */\
        "pmaddhw $f2, $f2, $f6          \n\t" /* C7R7+C5R5      C7r7+C5r5  */\
        "ldc1 $f16, 64(%2)              \n\t"                                \
        "pmaddhw $f4, $f4, $f16         \n\t" /* -C7R3+C3R1     -C7r3+C3r1 */\
        "paddw $f14, $f14, $f2          \n\t" /* B0             b0         */\
        "ldc1 $f2, 72(%2)               \n\t" /* -C5    -C1     -C5    -C1 */\
        "li $10, " #shift "             \n\t"                                \
        "pmaddhw $f2, $f2, $f6          \n\t" /* -C5R7-C1R5     -C5r7-C1r5 */\
        "paddw $f14, $f14, $f8          \n\t" /* A0+B0          a0+b0      */\
        "mtc1 $10, $f18                 \n\t"                                \
        "paddw $f8, $f8, $f8            \n\t" /* 2A0            2a0        */\
        "psubw $f8, $f8, $f14           \n\t" /* A0-B0          a0-b0      */\
        "paddw $f2, $f2, $f4            \n\t" /* B1             b1         */\
        "psraw $f14, $f14, $f18         \n\t"                                \
        "psraw $f8, $f8, $f18           \n\t"                                \
        "mov.d $f4, $f0                 \n\t" /* A1             a1         */\
        "paddw $f0, $f0, $f2            \n\t" /* A1+B1          a1+b1      */\
        "psubw $f4, $f4, $f2            \n\t" /* A1-B1          a1-b1      */\
        "psraw $f0, $f0, $f18           \n\t"                                \
        "psraw $f4, $f4, $f18           \n\t"                                \
        "packsswh $f14, $f14, $f14      \n\t" /* A0+B0          a0+b0      */\
        "swc1 $f14, " #dst "            \n\t"                                \
        "packsswh $f0, $f0, $f0         \n\t" /* A1+B1          a1+b1      */\
        "swc1 $f0, 16+" #dst "          \n\t"                                \
        "packsswh $f4, $f4, $f4         \n\t" /* A1-B1          a1-b1      */\
        "swc1 $f4, 96+" #dst "          \n\t"                                \
        "packsswh $f8, $f8, $f8         \n\t" /* A0-B0          a0-b0      */\
        "swc1 $f8, 112+" #dst "         \n\t"                                \
        "ldc1 $f0, " #src1 "            \n\t" /* R3     R1      r3      r1 */\
        "ldc1 $f8, 80(%2)               \n\t" /* -C1    C5      -C1     C5 */\
        "pmaddhw $f8, $f8, $f0          \n\t" /* -C1R3+C5R1     -C1r3+C5r1 */\
        "ldc1 $f14, 88(%2)              \n\t" /* C3     C7      C3      C7 */\
        "ldc1 $f16, 96(%2)              \n\t"                                \
        "pmaddhw $f0, $f0, $f16         \n\t" /* -C5R3+C7R1     -C5r3+C7r1 */\
        "pmaddhw $f14, $f14, $f6        \n\t" /* C3R7+C7R5      C3r7+C7r5  */\
        "mov.d $f4, $f10                \n\t" /* A2             a2         */\
        "ldc1 $f16, 104(%2)             \n\t"                                \
        "pmaddhw $f6, $f6, $f16         \n\t" /* -C1R7+C3R5     -C1r7+C3r5 */\
        "paddw $f8, $f8, $f14           \n\t" /* B2             b2         */\
        "paddw $f4, $f4, $f8            \n\t" /* A2+B2          a2+b2      */\
        "psubw $f10, $f10, $f8          \n\t" /* a2-B2          a2-b2      */\
        "psraw $f4, $f4, $f18           \n\t"                                \
        "psraw $f10, $f10, $f18         \n\t"                                \
        "mov.d $f8, $f12                \n\t" /* A3             a3         */\
        "paddw $f6, $f6, $f0            \n\t" /* B3             b3         */\
        "paddw $f12, $f12, $f6          \n\t" /* A3+B3          a3+b3      */\
        "psubw $f8, $f8, $f6            \n\t" /* a3-B3          a3-b3      */\
        "psraw $f12, $f12, $f18         \n\t"                                \
        "psraw $f8, $f8, $f18           \n\t"                                \
        "packsswh $f4, $f4, $f4         \n\t" /* A2+B2          a2+b2      */\
        "packsswh $f12, $f12, $f12      \n\t" /* A3+B3          a3+b3      */\
        "swc1 $f4, 32+" #dst "          \n\t"                                \
        "packsswh $f8, $f8, $f8         \n\t" /* A3-B3          a3-b3      */\
        "packsswh $f10, $f10, $f10      \n\t" /* A2-B2          a2-b2      */\
        "swc1 $f12, 48+" #dst "         \n\t"                                \
        "swc1 $f8, 64+" #dst "          \n\t"                                \
        "swc1 $f10, 80+" #dst "         \n\t"

        //IDCT(  src0,   src4,   src1,    src5,    dst, shift)
        IDCT(    (%1), 64(%1), 32(%1),  96(%1),  0(%0),    20)
        IDCT(   8(%1), 72(%1), 40(%1), 104(%1),  4(%0),    20)
        IDCT(  16(%1), 80(%1), 48(%1), 112(%1),  8(%0),    20)
        IDCT(  24(%1), 88(%1), 56(%1), 120(%1), 12(%0),    20)
        "b 9f                           \n\t"

        "# .p2align 4                   \n\t"
        "3:                             \n\t"

#undef  IDCT
#define IDCT(src0, src4, src1, src5, dst, shift)                             \
        "ldc1 $f0, " #src0 "            \n\t" /* R4     R0      r4      r0 */\
        "ldc1 $f4, " #src1 "            \n\t" /* R3     R1      r3      r1 */\
        "ldc1 $f8, 16(%2)               \n\t" /* C4     C4      C4      C4 */\
        "pmaddhw $f8, $f8, $f0          \n\t" /* C4R4+C4R0      C4r4+C4r0  */\
        "ldc1 $f10, 24(%2)              \n\t" /* -C4    C4      -C4     C4 */\
        "pmaddhw $f0, $f0, $f10         \n\t" /* -C4R4+C4R0     -C4r4+C4r0 */\
        "mov.d $f12, $f8                \n\t" /* C4R4+C4R0      C4r4+C4r0  */\
        "ldc1 $f14, 48(%2)              \n\t" /* C3     C1      C3      C1 */\
        "pmaddhw $f14, $f14, $f4        \n\t" /* C3R3+C1R1      C3r3+C1r1  */\
        "mov.d $f10, $f0                \n\t" /* -C4R4+C4R0     -C4r4+C4r0 */\
        "ldc1 $f6, 64(%2)               \n\t"                                \
        "pmaddhw $f6, $f6, $f4          \n\t" /* -C7R3+C3R1     -C7r3+C3r1 */\
        "li $10, " #shift "             \n\t"                                \
        "paddw $f14, $f14, $f8          \n\t" /* A0+B0          a0+b0      */\
        "mtc1 $10, $f18                 \n\t"                                \
        "paddw $f8, $f8, $f8            \n\t" /* 2A0            2a0        */\
        "psubw $f8, $f8, $f14           \n\t" /* A0-B0          a0-b0      */\
        "psraw $f14, $f14, $f18         \n\t"                                \
        "psraw $f8, $f8, $f18           \n\t"                                \
        "mov.d $f2, $f0                 \n\t" /* A1             a1         */\
        "paddw $f0, $f0, $f6            \n\t" /* A1+B1          a1+b1      */\
        "psubw $f2, $f2, $f6            \n\t" /* A1-B1          a1-b1      */\
        "psraw $f0, $f0, $f18           \n\t"                                \
        "psraw $f2, $f2, $f18           \n\t"                                \
        "packsswh $f14, $f14, $f14      \n\t" /* A0+B0  a0+b0              */\
        "swc1 $f14, " #dst "            \n\t"                                \
        "packsswh $f0, $f0, $f0         \n\t" /* A1+B1  a1+b1              */\
        "swc1 $f0, 16+" #dst "          \n\t"                                \
        "packsswh $f2, $f2, $f2         \n\t" /* A1-B1  a1-b1              */\
        "swc1 $f2, 96+" #dst "          \n\t"                                \
        "packsswh $f8, $f8, $f8         \n\t" /* A0-B0  a0-b0              */\
        "swc1 $f8, 112+" #dst "         \n\t"                                \
        "ldc1 $f8, 80(%2)               \n\t" /* -C1    C5      -C1     C5 */\
        "ldc1 $f16, 96(%2)              \n\t"                                \
        "pmaddhw $f8, $f8, $f4          \n\t" /* -C1R3+C5R1     -C1r3+C5r1 */\
        "pmaddhw $f4, $f4, $f16         \n\t" /* -C5R3+C7R1     -C5r3+C7r1 */\
        "mov.d $f2, $f10                \n\t" /* A2             a2         */\
        "paddw $f2, $f2, $f8            \n\t" /* A2+B2          a2+b2      */\
        "psubw $f10, $f10, $f8          \n\t" /* a2-B2          a2-b2      */\
        "psraw $f2, $f2, $f18           \n\t"                                \
        "psraw $f10, $f10, $f18         \n\t"                                \
        "mov.d $f8, $f12                \n\t" /* A3             a3         */\
        "paddw $f12, $f12, $f4          \n\t" /* A3+B3          a3+b3      */\
        "psubw $f8, $f8, $f4            \n\t" /* a3-B3          a3-b3      */\
        "psraw $f12, $f12, $f18         \n\t"                                \
        "psraw $f8, $f8, $f18           \n\t"                                \
        "packsswh $f2, $f2, $f2         \n\t" /* A2+B2  a2+b2              */\
        "packsswh $f12, $f12, $f12      \n\t" /* A3+B3  a3+b3              */\
        "swc1 $f2, 32+" #dst "          \n\t"                                \
        "packsswh $f8, $f8, $f8         \n\t" /* A3-B3  a3-b3              */\
        "packsswh $f10, $f10, $f10      \n\t" /* A2-B2  a2-b2              */\
        "swc1 $f12, 48+" #dst "         \n\t"                                \
        "swc1 $f8, 64+" #dst "          \n\t"                                \
        "swc1 $f10, 80+" #dst "         \n\t"

        //IDCT(  src0,   src4,   src1,    src5,    dst, shift)
        IDCT(    (%1), 64(%1), 32(%1),  96(%1),  0(%0),    20)
        IDCT(   8(%1), 72(%1), 40(%1), 104(%1),  4(%0),    20)
        IDCT(  16(%1), 80(%1), 48(%1), 112(%1),  8(%0),    20)
        IDCT(  24(%1), 88(%1), 56(%1), 120(%1), 12(%0),    20)
        "b 9f                           \n\t"

        "# .p2align 4                   \n\t"
        "5:                             \n\t"

#undef  IDCT
#define IDCT(src0, src4, src1, src5, dst, shift)                             \
        "ldc1 $f0, " #src0 "            \n\t" /* R4     R0      r4      r0 */\
        "ldc1 $f2, " #src4 "            \n\t" /* R6     R2      r6      r2 */\
        "ldc1 $f8, 16(%2)               \n\t" /* C4     C4      C4      C4 */\
        "pmaddhw $f8, $f8, $f0          \n\t" /* C4R4+C4R0      C4r4+C4r0  */\
        "ldc1 $f10, 24(%2)              \n\t" /* -C4    C4      -C4     C4 */\
        "pmaddhw $f0, $f0, $f10         \n\t" /* -C4R4+C4R0     -C4r4+C4r0 */\
        "ldc1 $f10, 32(%2)              \n\t" /* C6     C2      C6      C2 */\
        "pmaddhw $f10, $f10, $f2        \n\t" /* C6R6+C2R2      C6r6+C2r2  */\
        "ldc1 $f12, 40(%2)              \n\t" /* -C2    C6      -C2     C6 */\
        "pmaddhw $f2, $f2, $f12         \n\t" /* -C2R6+C6R2     -C2r6+C6r2 */\
        "mov.d $f12, $f8                \n\t" /* C4R4+C4R0      C4r4+C4r0  */\
        "paddw $f8, $f8, $f10           \n\t" /* A0             a0         */\
        "psubw $f12, $f12, $f10         \n\t" /* A3             a3         */\
        "mov.d $f10, $f0                \n\t" /* -C4R4+C4R0     -C4r4+C4r0 */\
        "paddw $f0, $f0, $f2            \n\t" /* A1             a1         */\
        "psubw $f10, $f10, $f2          \n\t" /* A2             a2         */\
        "ldc1 $f4, 8+" #src0 "          \n\t" /* R4     R0      r4      r0 */\
        "ldc1 $f6, 8+" #src4 "          \n\t" /* R6     R2      r6      r2 */\
        "ldc1 $f2, 16(%2)               \n\t" /* C4     C4      C4      C4 */\
        "pmaddhw $f2, $f2, $f4          \n\t" /* C4R4+C4R0      C4r4+C4r0  */\
        "ldc1 $f14, 24(%2)              \n\t" /* -C4    C4      -C4     C4 */\
        "pmaddhw $f4, $f4, $f14         \n\t" /* -C4R4+C4R0     -C4r4+C4r0 */\
        "ldc1 $f14, 32(%2)              \n\t" /* C6     C2      C6      C2 */\
        "ldc1 $f16, 40(%2)              \n\t"                                \
        "pmaddhw $f14, $f14, $f6        \n\t" /* C6R6+C2R2      C6r6+C2r2  */\
        "pmaddhw $f6, $f6, $f16         \n\t" /* -C2R6+C6R2     -C2r6+C6r2 */\
        "paddw $f14, $f14, $f2          \n\t" /* A0             a0         */\
        "paddw $f2, $f2, $f2            \n\t" /* 2C0            2c0        */\
        "psubw $f2, $f2, $f14           \n\t" /* A3             a3         */\
        "li $10, " #shift "             \n\t"                                \
        "paddw $f6, $f6, $f4            \n\t" /* A1             a1         */\
        "mtc1 $10, $f18                 \n\t"                                \
        "paddw $f4, $f4, $f4            \n\t" /* 2C1            2c1        */\
        "psubw $f4, $f4, $f6            \n\t" /* A2             a2         */\
        "psraw $f8, $f8, $f18           \n\t"                                \
        "psraw $f14, $f14, $f18         \n\t"                                \
        "psraw $f6, $f6, $f18           \n\t"                                \
        "packsswh $f8, $f8, $f14        \n\t" /* A0             a0         */\
        "sdc1 $f8, " #dst "             \n\t"                                \
        "psraw $f0, $f0, $f18           \n\t"                                \
        "packsswh $f0, $f0, $f6         \n\t" /* A1             a1         */\
        "sdc1 $f0, 16+" #dst "          \n\t"                                \
        "sdc1 $f0, 96+" #dst "          \n\t"                                \
        "sdc1 $f8, 112+" #dst "         \n\t"                                \
        "psraw $f10, $f10, $f18         \n\t"                                \
        "psraw $f12, $f12, $f18         \n\t"                                \
        "psraw $f4, $f4, $f18           \n\t"                                \
        "packsswh $f10, $f10, $f4       \n\t" /* A2-B2          a2-b2      */\
        "sdc1 $f10, 32+" #dst "         \n\t"                                \
        "psraw $f2, $f2, $f18           \n\t"                                \
        "packsswh $f12, $f12, $f2       \n\t" /* A3+B3          a3+b3      */\
        "sdc1 $f12, 48+" #dst "         \n\t"                                \
        "sdc1 $f12, 64+" #dst "         \n\t"                                \
        "sdc1 $f10, 80+" #dst "         \n\t"

        //IDCT(  src0,   src4,   src1,    src5,    dst, shift)
        IDCT(   0(%1), 64(%1), 32(%1),  96(%1),  0(%0),    20)
        IDCT(  16(%1), 80(%1), 48(%1), 112(%1),  8(%0),    20)
        "b 9f                           \n\t"

        "# .p2align 4                   \n\t"
        "1:                             \n\t"

#undef  IDCT
#define IDCT(src0, src4, src1, src5, dst, shift)                             \
        "ldc1 $f0, " #src0 "            \n\t" /* R4     R0      r4      r0 */\
        "ldc1 $f2, " #src4 "            \n\t" /* R6     R2      r6      r2 */\
        "ldc1 $f4, " #src1 "            \n\t" /* R3     R1      r3      r1 */\
        "ldc1 $f8, 16(%2)               \n\t" /* C4     C4      C4      C4 */\
        "li $10, " #shift "             \n\t"                                \
        "pmaddhw $f8, $f8, $f0          \n\t" /* C4R4+C4R0      C4r4+C4r0  */\
        "ldc1 $f10, 24(%2)              \n\t" /* -C4    C4      -C4     C4 */\
        "mtc1 $10, $f18                 \n\t"                                \
        "pmaddhw $f0, $f0, $f10         \n\t" /* -C4R4+C4R0     -C4r4+C4r0 */\
        "ldc1 $f10, 32(%2)              \n\t" /* C6     C2      C6      C2 */\
        "pmaddhw $f10, $f10, $f2        \n\t" /* C6R6+C2R2      C6r6+C2r2  */\
        "ldc1 $f12, 40(%2)              \n\t" /* -C2    C6      -C2     C6 */\
        "pmaddhw $f2, $f2, $f12         \n\t" /* -C2R6+C6R2     -C2r6+C6r2 */\
        "mov.d $f12, $f8                \n\t" /* C4R4+C4R0      C4r4+C4r0  */\
        "ldc1 $f14, 48(%2)              \n\t" /* C3     C1      C3      C1 */\
        "pmaddhw $f14, $f14, $f4        \n\t" /* C3R3+C1R1      C3r3+C1r1  */\
        "paddw $f8, $f8, $f10           \n\t" /* A0             a0         */\
        "psubw $f12, $f12, $f10         \n\t" /* A3             a3         */\
        "mov.d $f10, $f0                \n\t" /* -C4R4+C4R0     -C4r4+C4r0 */\
        "paddw $f0, $f0, $f2            \n\t" /* A1             a1         */\
        "psubw $f10, $f10, $f2          \n\t" /* A2             a2         */\
        "ldc1 $f2, 64(%2)               \n\t"                                \
        "pmaddhw $f2, $f2, $f4          \n\t" /* -C7R3+C3R1     -C7r3+C3r1 */\
        "paddw $f14, $f14, $f8          \n\t" /* A0+B0          a0+b0      */\
        "paddw $f8, $f8, $f8            \n\t" /* 2A0            2a0        */\
        "psubw $f8, $f8, $f14           \n\t" /* A0-B0          a0-b0      */\
        "psraw $f14, $f14, $f18         \n\t"                                \
        "psraw $f8, $f8, $f18           \n\t"                                \
        "mov.d $f6, $f0                 \n\t" /* A1             a1         */\
        "paddw $f0, $f0, $f2            \n\t" /* A1+B1          a1+b1      */\
        "psubw $f6, $f6, $f2            \n\t" /* A1-B1          a1-b1      */\
        "psraw $f0, $f0, $f18           \n\t"                                \
        "psraw $f6, $f6, $f18           \n\t"                                \
        "packsswh $f14, $f14, $f14      \n\t" /* A0+B0  a0+b0              */\
        "swc1 $f14, " #dst "            \n\t"                                \
        "packsswh $f0, $f0, $f0         \n\t" /* A1+B1  a1+b1              */\
        "swc1 $f0, 16+" #dst "          \n\t"                                \
        "packsswh $f6, $f6, $f6         \n\t" /* A1-B1  a1-b1              */\
        "swc1 $f6, 96+" #dst "          \n\t"                                \
        "packsswh $f8, $f8, $f8         \n\t" /* A0-B0  a0-b0              */\
        "swc1 $f8, 112+" #dst "         \n\t"                                \
        "ldc1 $f8, 80(%2)               \n\t" /* -C1    C5      -C1     C5 */\
        "ldc1 $f16, 96(%2)              \n\t"                                \
        "pmaddhw $f8, $f8, $f4          \n\t" /* -C1R3+C5R1     -C1r3+C5r1 */\
        "pmaddhw $f4, $f4, $f16         \n\t" /* -C5R3+C7R1     -C5r3+C7r1 */\
        "mov.d $f6, $f10                \n\t" /* A2             a2         */\
        "paddw $f6, $f6, $f8            \n\t" /* A2+B2          a2+b2      */\
        "psubw $f10, $f10, $f8          \n\t" /* a2-B2          a2-b2      */\
        "psraw $f6, $f6, $f18           \n\t"                                \
        "psraw $f10, $f10, $f18         \n\t"                                \
        "mov.d $f8, $f12                \n\t" /* A3             a3         */\
        "paddw $f12, $f12, $f4          \n\t" /* A3+B3          a3+b3      */\
        "psubw $f8, $f8, $f4            \n\t" /* a3-B3          a3-b3      */\
        "psraw $f12, $f12, $f18         \n\t"                                \
        "packsswh $f6, $f6, $f6         \n\t" /* A2+B2          a2+b2      */\
        "swc1 $f6, 32+" #dst "          \n\t"                                \
        "psraw $f8, $f8, $f18           \n\t"                                \
        "packsswh $f12, $f12, $f12      \n\t" /* A3+B3          a3+b3      */\
        "swc1 $f12, 48+" #dst "         \n\t"                                \
        "packsswh $f8, $f8, $f8         \n\t" /* A3-B3          a3-b3      */\
        "packsswh $f10, $f10, $f10      \n\t" /* A2-B2          a2-b2      */\
        "swc1 $f8, 64+" #dst "          \n\t"                                \
        "swc1 $f10, 80+" #dst "         \n\t"

        //IDCT(  src0,   src4,   src1,    src5,    dst, shift)
        IDCT(    (%1), 64(%1), 32(%1),  96(%1),  0(%0),    20)
        IDCT(   8(%1), 72(%1), 40(%1), 104(%1),  4(%0),    20)
        IDCT(  16(%1), 80(%1), 48(%1), 112(%1),  8(%0),    20)
        IDCT(  24(%1), 88(%1), 56(%1), 120(%1), 12(%0),    20)
        "b 9f                           \n\t"

        "# .p2align 4                   \n\t"
        "7:                             \n\t"

#undef  IDCT
#define IDCT(src0, src4, src1, src5, dst, shift)                             \
        "ldc1 $f0, " #src0 "            \n\t" /* R4     R0      r4      r0 */\
        "ldc1 $f8, 16(%2)               \n\t" /* C4     C4      C4      C4 */\
        "li $10, " #shift "             \n\t"                                \
        "pmaddhw $f8, $f8, $f0          \n\t" /* C4R4+C4R0      C4r4+C4r0  */\
        "mtc1 $10, $f18                 \n\t"                                \
        "ldc1 $f10, 24(%2)              \n\t" /* -C4    C4      -C4     C4 */\
        "pmaddhw $f0, $f0, $f10         \n\t" /* -C4R4+C4R0     -C4r4+C4r0 */\
        "psraw $f8, $f8, $f18           \n\t"                                \
        "psraw $f0, $f0, $f18           \n\t"                                \
        "ldc1 $f4, 8+" #src0 "          \n\t" /* R4     R0      r4      r0 */\
        "ldc1 $f2, 16(%2)               \n\t" /* C4     C4      C4      C4 */\
        "pmaddhw $f2, $f2, $f4          \n\t" /* C4R4+C4R0      C4r4+C4r0  */\
        "ldc1 $f14, 24(%2)              \n\t" /* -C4    C4      -C4     C4 */\
        "pmaddhw $f4, $f4, $f14         \n\t" /* -C4R4+C4R0     -C4r4+C4r0 */\
        "ldc1 $f14, 32(%2)              \n\t" /* C6     C2      C6      C2 */\
        "psraw $f2, $f2, $f18           \n\t"                                \
        "packsswh $f8, $f8, $f2         \n\t" /* A0             a0         */\
        "sdc1 $f8, " #dst "             \n\t"                                \
        "psraw $f4, $f4, $f18           \n\t"                                \
        "packsswh $f0, $f0, $f4         \n\t" /* A1             a1         */\
        "sdc1 $f0, 16+" #dst "          \n\t"                                \
        "sdc1 $f0, 96+" #dst "          \n\t"                                \
        "sdc1 $f8, 112+" #dst "         \n\t"                                \
        "sdc1 $f0, 32+" #dst "          \n\t"                                \
        "sdc1 $f8, 48+" #dst "          \n\t"                                \
        "sdc1 $f8, 64+" #dst "          \n\t"                                \
        "sdc1 $f0, 80+" #dst "          \n\t"

        //IDCT(  src0,   src4,   src1,    src5,    dst, shift)
        IDCT(   0(%1), 64(%1), 32(%1),  96(%1),  0(%0),    20)
        IDCT(  16(%1), 80(%1), 48(%1), 112(%1),  8(%0),    20)

        "9:                             \n\t"
        ::"r"(block),"r"(temp),"r"(coeffs),"m"(ff_wm1010),"m"(ff_d40000)
        : "$10","$11"
    );
}
