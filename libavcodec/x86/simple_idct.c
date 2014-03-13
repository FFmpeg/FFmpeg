/*
 * Simple IDCT MMX
 *
 * Copyright (c) 2001, 2002 Michael Niedermayer <michaelni@gmx.at>
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
#include "libavcodec/simple_idct.h"
#include "libavutil/mem.h"
#include "libavutil/x86/asm.h"
#include "dsputil_x86.h"

#if HAVE_INLINE_ASM

/*
23170.475006
22725.260826
21406.727617
19265.545870
16384.000000
12872.826198
8866.956905
4520.335430
*/
#define C0 23170 //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define C1 22725 //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define C2 21407 //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define C3 19266 //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define C4 16383 //cos(i*M_PI/16)*sqrt(2)*(1<<14) - 0.5
#define C5 12873 //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define C6 8867  //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define C7 4520  //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5

#define ROW_SHIFT 11
#define COL_SHIFT 20 // 6

DECLARE_ASM_CONST(8, uint64_t, wm1010)= 0xFFFF0000FFFF0000ULL;
DECLARE_ASM_CONST(8, uint64_t, d40000)= 0x0000000000040000ULL;

DECLARE_ALIGNED(8, static const int16_t, coeffs)[]= {
        1<<(ROW_SHIFT-1), 0, 1<<(ROW_SHIFT-1), 0,
//        1<<(COL_SHIFT-1), 0, 1<<(COL_SHIFT-1), 0,
//        0, 1<<(COL_SHIFT-1-16), 0, 1<<(COL_SHIFT-1-16),
        1<<(ROW_SHIFT-1), 1, 1<<(ROW_SHIFT-1), 0,
        // the 1 = ((1<<(COL_SHIFT-1))/C4)<<ROW_SHIFT :)
//        0, 0, 0, 0,
//        0, 0, 0, 0,

 C4,  C4,  C4,  C4,
 C4, -C4,  C4, -C4,

 C2,  C6,  C2,  C6,
 C6, -C2,  C6, -C2,

 C1,  C3,  C1,  C3,
 C5,  C7,  C5,  C7,

 C3, -C7,  C3, -C7,
-C1, -C5, -C1, -C5,

 C5, -C1,  C5, -C1,
 C7,  C3,  C7,  C3,

 C7, -C5,  C7, -C5,
 C3, -C1,  C3, -C1
};

static inline void idct(int16_t *block)
{
        LOCAL_ALIGNED_8(int64_t, align_tmp, [16]);
        int16_t * const temp= (int16_t*)align_tmp;

        __asm__ volatile(
#if 0 //Alternative, simpler variant

#define ROW_IDCT(src0, src4, src1, src5, dst, rounder, shift) \
        "movq " #src0 ", %%mm0          \n\t" /* R4     R0      r4      r0 */\
        "movq " #src4 ", %%mm1          \n\t" /* R6     R2      r6      r2 */\
        "movq " #src1 ", %%mm2          \n\t" /* R3     R1      r3      r1 */\
        "movq " #src5 ", %%mm3          \n\t" /* R7     R5      r7      r5 */\
        "movq 16(%2), %%mm4             \n\t" /* C4     C4      C4      C4 */\
        "pmaddwd %%mm0, %%mm4           \n\t" /* C4R4+C4R0      C4r4+C4r0 */\
        "movq 24(%2), %%mm5             \n\t" /* -C4    C4      -C4     C4 */\
        "pmaddwd %%mm5, %%mm0           \n\t" /* -C4R4+C4R0     -C4r4+C4r0 */\
        "movq 32(%2), %%mm5             \n\t" /* C6     C2      C6      C2 */\
        "pmaddwd %%mm1, %%mm5           \n\t" /* C6R6+C2R2      C6r6+C2r2 */\
        "movq 40(%2), %%mm6             \n\t" /* -C2    C6      -C2     C6 */\
        "pmaddwd %%mm6, %%mm1           \n\t" /* -C2R6+C6R2     -C2r6+C6r2 */\
        "movq 48(%2), %%mm7             \n\t" /* C3     C1      C3      C1 */\
        "pmaddwd %%mm2, %%mm7           \n\t" /* C3R3+C1R1      C3r3+C1r1 */\
        #rounder ", %%mm4               \n\t"\
        "movq %%mm4, %%mm6              \n\t" /* C4R4+C4R0      C4r4+C4r0 */\
        "paddd %%mm5, %%mm4             \n\t" /* A0             a0 */\
        "psubd %%mm5, %%mm6             \n\t" /* A3             a3 */\
        "movq 56(%2), %%mm5             \n\t" /* C7     C5      C7      C5 */\
        "pmaddwd %%mm3, %%mm5           \n\t" /* C7R7+C5R5      C7r7+C5r5 */\
        #rounder ", %%mm0               \n\t"\
        "paddd %%mm0, %%mm1             \n\t" /* A1             a1 */\
        "paddd %%mm0, %%mm0             \n\t" \
        "psubd %%mm1, %%mm0             \n\t" /* A2             a2 */\
        "pmaddwd 64(%2), %%mm2          \n\t" /* -C7R3+C3R1     -C7r3+C3r1 */\
        "paddd %%mm5, %%mm7             \n\t" /* B0             b0 */\
        "movq 72(%2), %%mm5             \n\t" /* -C5    -C1     -C5     -C1 */\
        "pmaddwd %%mm3, %%mm5           \n\t" /* -C5R7-C1R5     -C5r7-C1r5 */\
        "paddd %%mm4, %%mm7             \n\t" /* A0+B0          a0+b0 */\
        "paddd %%mm4, %%mm4             \n\t" /* 2A0            2a0 */\
        "psubd %%mm7, %%mm4             \n\t" /* A0-B0          a0-b0 */\
        "paddd %%mm2, %%mm5             \n\t" /* B1             b1 */\
        "psrad $" #shift ", %%mm7       \n\t"\
        "psrad $" #shift ", %%mm4       \n\t"\
        "movq %%mm1, %%mm2              \n\t" /* A1             a1 */\
        "paddd %%mm5, %%mm1             \n\t" /* A1+B1          a1+b1 */\
        "psubd %%mm5, %%mm2             \n\t" /* A1-B1          a1-b1 */\
        "psrad $" #shift ", %%mm1       \n\t"\
        "psrad $" #shift ", %%mm2       \n\t"\
        "packssdw %%mm1, %%mm7          \n\t" /* A1+B1  a1+b1   A0+B0   a0+b0 */\
        "packssdw %%mm4, %%mm2          \n\t" /* A0-B0  a0-b0   A1-B1   a1-b1 */\
        "movq %%mm7, " #dst "           \n\t"\
        "movq " #src1 ", %%mm1          \n\t" /* R3     R1      r3      r1 */\
        "movq 80(%2), %%mm4             \n\t" /* -C1    C5      -C1     C5 */\
        "movq %%mm2, 24+" #dst "        \n\t"\
        "pmaddwd %%mm1, %%mm4           \n\t" /* -C1R3+C5R1     -C1r3+C5r1 */\
        "movq 88(%2), %%mm7             \n\t" /* C3     C7      C3      C7 */\
        "pmaddwd 96(%2), %%mm1          \n\t" /* -C5R3+C7R1     -C5r3+C7r1 */\
        "pmaddwd %%mm3, %%mm7           \n\t" /* C3R7+C7R5      C3r7+C7r5 */\
        "movq %%mm0, %%mm2              \n\t" /* A2             a2 */\
        "pmaddwd 104(%2), %%mm3         \n\t" /* -C1R7+C3R5     -C1r7+C3r5 */\
        "paddd %%mm7, %%mm4             \n\t" /* B2             b2 */\
        "paddd %%mm4, %%mm2             \n\t" /* A2+B2          a2+b2 */\
        "psubd %%mm4, %%mm0             \n\t" /* a2-B2          a2-b2 */\
        "psrad $" #shift ", %%mm2       \n\t"\
        "psrad $" #shift ", %%mm0       \n\t"\
        "movq %%mm6, %%mm4              \n\t" /* A3             a3 */\
        "paddd %%mm1, %%mm3             \n\t" /* B3             b3 */\
        "paddd %%mm3, %%mm6             \n\t" /* A3+B3          a3+b3 */\
        "psubd %%mm3, %%mm4             \n\t" /* a3-B3          a3-b3 */\
        "psrad $" #shift ", %%mm6       \n\t"\
        "packssdw %%mm6, %%mm2          \n\t" /* A3+B3  a3+b3   A2+B2   a2+b2 */\
        "movq %%mm2, 8+" #dst "         \n\t"\
        "psrad $" #shift ", %%mm4       \n\t"\
        "packssdw %%mm0, %%mm4          \n\t" /* A2-B2  a2-b2   A3-B3   a3-b3 */\
        "movq %%mm4, 16+" #dst "        \n\t"\

#define COL_IDCT(src0, src4, src1, src5, dst, shift) \
        "movq " #src0 ", %%mm0          \n\t" /* R4     R0      r4      r0 */\
        "movq " #src4 ", %%mm1          \n\t" /* R6     R2      r6      r2 */\
        "movq " #src1 ", %%mm2          \n\t" /* R3     R1      r3      r1 */\
        "movq " #src5 ", %%mm3          \n\t" /* R7     R5      r7      r5 */\
        "movq 16(%2), %%mm4             \n\t" /* C4     C4      C4      C4 */\
        "pmaddwd %%mm0, %%mm4           \n\t" /* C4R4+C4R0      C4r4+C4r0 */\
        "movq 24(%2), %%mm5             \n\t" /* -C4    C4      -C4     C4 */\
        "pmaddwd %%mm5, %%mm0           \n\t" /* -C4R4+C4R0     -C4r4+C4r0 */\
        "movq 32(%2), %%mm5             \n\t" /* C6     C2      C6      C2 */\
        "pmaddwd %%mm1, %%mm5           \n\t" /* C6R6+C2R2      C6r6+C2r2 */\
        "movq 40(%2), %%mm6             \n\t" /* -C2    C6      -C2     C6 */\
        "pmaddwd %%mm6, %%mm1           \n\t" /* -C2R6+C6R2     -C2r6+C6r2 */\
        "movq %%mm4, %%mm6              \n\t" /* C4R4+C4R0      C4r4+C4r0 */\
        "movq 48(%2), %%mm7             \n\t" /* C3     C1      C3      C1 */\
        "pmaddwd %%mm2, %%mm7           \n\t" /* C3R3+C1R1      C3r3+C1r1 */\
        "paddd %%mm5, %%mm4             \n\t" /* A0             a0 */\
        "psubd %%mm5, %%mm6             \n\t" /* A3             a3 */\
        "movq %%mm0, %%mm5              \n\t" /* -C4R4+C4R0     -C4r4+C4r0 */\
        "paddd %%mm1, %%mm0             \n\t" /* A1             a1 */\
        "psubd %%mm1, %%mm5             \n\t" /* A2             a2 */\
        "movq 56(%2), %%mm1             \n\t" /* C7     C5      C7      C5 */\
        "pmaddwd %%mm3, %%mm1           \n\t" /* C7R7+C5R5      C7r7+C5r5 */\
        "pmaddwd 64(%2), %%mm2          \n\t" /* -C7R3+C3R1     -C7r3+C3r1 */\
        "paddd %%mm1, %%mm7             \n\t" /* B0             b0 */\
        "movq 72(%2), %%mm1             \n\t" /* -C5    -C1     -C5     -C1 */\
        "pmaddwd %%mm3, %%mm1           \n\t" /* -C5R7-C1R5     -C5r7-C1r5 */\
        "paddd %%mm4, %%mm7             \n\t" /* A0+B0          a0+b0 */\
        "paddd %%mm4, %%mm4             \n\t" /* 2A0            2a0 */\
        "psubd %%mm7, %%mm4             \n\t" /* A0-B0          a0-b0 */\
        "paddd %%mm2, %%mm1             \n\t" /* B1             b1 */\
        "psrad $" #shift ", %%mm7       \n\t"\
        "psrad $" #shift ", %%mm4       \n\t"\
        "movq %%mm0, %%mm2              \n\t" /* A1             a1 */\
        "paddd %%mm1, %%mm0             \n\t" /* A1+B1          a1+b1 */\
        "psubd %%mm1, %%mm2             \n\t" /* A1-B1          a1-b1 */\
        "psrad $" #shift ", %%mm0       \n\t"\
        "psrad $" #shift ", %%mm2       \n\t"\
        "packssdw %%mm7, %%mm7          \n\t" /* A0+B0  a0+b0 */\
        "movd %%mm7, " #dst "           \n\t"\
        "packssdw %%mm0, %%mm0          \n\t" /* A1+B1  a1+b1 */\
        "movd %%mm0, 16+" #dst "        \n\t"\
        "packssdw %%mm2, %%mm2          \n\t" /* A1-B1  a1-b1 */\
        "movd %%mm2, 96+" #dst "        \n\t"\
        "packssdw %%mm4, %%mm4          \n\t" /* A0-B0  a0-b0 */\
        "movd %%mm4, 112+" #dst "       \n\t"\
        "movq " #src1 ", %%mm0          \n\t" /* R3     R1      r3      r1 */\
        "movq 80(%2), %%mm4             \n\t" /* -C1    C5      -C1     C5 */\
        "pmaddwd %%mm0, %%mm4           \n\t" /* -C1R3+C5R1     -C1r3+C5r1 */\
        "movq 88(%2), %%mm7             \n\t" /* C3     C7      C3      C7 */\
        "pmaddwd 96(%2), %%mm0          \n\t" /* -C5R3+C7R1     -C5r3+C7r1 */\
        "pmaddwd %%mm3, %%mm7           \n\t" /* C3R7+C7R5      C3r7+C7r5 */\
        "movq %%mm5, %%mm2              \n\t" /* A2             a2 */\
        "pmaddwd 104(%2), %%mm3         \n\t" /* -C1R7+C3R5     -C1r7+C3r5 */\
        "paddd %%mm7, %%mm4             \n\t" /* B2             b2 */\
        "paddd %%mm4, %%mm2             \n\t" /* A2+B2          a2+b2 */\
        "psubd %%mm4, %%mm5             \n\t" /* a2-B2          a2-b2 */\
        "psrad $" #shift ", %%mm2       \n\t"\
        "psrad $" #shift ", %%mm5       \n\t"\
        "movq %%mm6, %%mm4              \n\t" /* A3             a3 */\
        "paddd %%mm0, %%mm3             \n\t" /* B3             b3 */\
        "paddd %%mm3, %%mm6             \n\t" /* A3+B3          a3+b3 */\
        "psubd %%mm3, %%mm4             \n\t" /* a3-B3          a3-b3 */\
        "psrad $" #shift ", %%mm6       \n\t"\
        "psrad $" #shift ", %%mm4       \n\t"\
        "packssdw %%mm2, %%mm2          \n\t" /* A2+B2  a2+b2 */\
        "packssdw %%mm6, %%mm6          \n\t" /* A3+B3  a3+b3 */\
        "movd %%mm2, 32+" #dst "        \n\t"\
        "packssdw %%mm4, %%mm4          \n\t" /* A3-B3  a3-b3 */\
        "packssdw %%mm5, %%mm5          \n\t" /* A2-B2  a2-b2 */\
        "movd %%mm6, 48+" #dst "        \n\t"\
        "movd %%mm4, 64+" #dst "        \n\t"\
        "movd %%mm5, 80+" #dst "        \n\t"\


#define DC_COND_ROW_IDCT(src0, src4, src1, src5, dst, rounder, shift) \
        "movq " #src0 ", %%mm0          \n\t" /* R4     R0      r4      r0 */\
        "movq " #src4 ", %%mm1          \n\t" /* R6     R2      r6      r2 */\
        "movq " #src1 ", %%mm2          \n\t" /* R3     R1      r3      r1 */\
        "movq " #src5 ", %%mm3          \n\t" /* R7     R5      r7      r5 */\
        "movq "MANGLE(wm1010)", %%mm4   \n\t"\
        "pand %%mm0, %%mm4              \n\t"\
        "por %%mm1, %%mm4               \n\t"\
        "por %%mm2, %%mm4               \n\t"\
        "por %%mm3, %%mm4               \n\t"\
        "packssdw %%mm4,%%mm4           \n\t"\
        "movd %%mm4, %%eax              \n\t"\
        "orl %%eax, %%eax               \n\t"\
        "jz 1f                          \n\t"\
        "movq 16(%2), %%mm4             \n\t" /* C4     C4      C4      C4 */\
        "pmaddwd %%mm0, %%mm4           \n\t" /* C4R4+C4R0      C4r4+C4r0 */\
        "movq 24(%2), %%mm5             \n\t" /* -C4    C4      -C4     C4 */\
        "pmaddwd %%mm5, %%mm0           \n\t" /* -C4R4+C4R0     -C4r4+C4r0 */\
        "movq 32(%2), %%mm5             \n\t" /* C6     C2      C6      C2 */\
        "pmaddwd %%mm1, %%mm5           \n\t" /* C6R6+C2R2      C6r6+C2r2 */\
        "movq 40(%2), %%mm6             \n\t" /* -C2    C6      -C2     C6 */\
        "pmaddwd %%mm6, %%mm1           \n\t" /* -C2R6+C6R2     -C2r6+C6r2 */\
        "movq 48(%2), %%mm7             \n\t" /* C3     C1      C3      C1 */\
        "pmaddwd %%mm2, %%mm7           \n\t" /* C3R3+C1R1      C3r3+C1r1 */\
        #rounder ", %%mm4               \n\t"\
        "movq %%mm4, %%mm6              \n\t" /* C4R4+C4R0      C4r4+C4r0 */\
        "paddd %%mm5, %%mm4             \n\t" /* A0             a0 */\
        "psubd %%mm5, %%mm6             \n\t" /* A3             a3 */\
        "movq 56(%2), %%mm5             \n\t" /* C7     C5      C7      C5 */\
        "pmaddwd %%mm3, %%mm5           \n\t" /* C7R7+C5R5      C7r7+C5r5 */\
        #rounder ", %%mm0               \n\t"\
        "paddd %%mm0, %%mm1             \n\t" /* A1             a1 */\
        "paddd %%mm0, %%mm0             \n\t" \
        "psubd %%mm1, %%mm0             \n\t" /* A2             a2 */\
        "pmaddwd 64(%2), %%mm2          \n\t" /* -C7R3+C3R1     -C7r3+C3r1 */\
        "paddd %%mm5, %%mm7             \n\t" /* B0             b0 */\
        "movq 72(%2), %%mm5             \n\t" /* -C5    -C1     -C5     -C1 */\
        "pmaddwd %%mm3, %%mm5           \n\t" /* -C5R7-C1R5     -C5r7-C1r5 */\
        "paddd %%mm4, %%mm7             \n\t" /* A0+B0          a0+b0 */\
        "paddd %%mm4, %%mm4             \n\t" /* 2A0            2a0 */\
        "psubd %%mm7, %%mm4             \n\t" /* A0-B0          a0-b0 */\
        "paddd %%mm2, %%mm5             \n\t" /* B1             b1 */\
        "psrad $" #shift ", %%mm7       \n\t"\
        "psrad $" #shift ", %%mm4       \n\t"\
        "movq %%mm1, %%mm2              \n\t" /* A1             a1 */\
        "paddd %%mm5, %%mm1             \n\t" /* A1+B1          a1+b1 */\
        "psubd %%mm5, %%mm2             \n\t" /* A1-B1          a1-b1 */\
        "psrad $" #shift ", %%mm1       \n\t"\
        "psrad $" #shift ", %%mm2       \n\t"\
        "packssdw %%mm1, %%mm7          \n\t" /* A1+B1  a1+b1   A0+B0   a0+b0 */\
        "packssdw %%mm4, %%mm2          \n\t" /* A0-B0  a0-b0   A1-B1   a1-b1 */\
        "movq %%mm7, " #dst "           \n\t"\
        "movq " #src1 ", %%mm1          \n\t" /* R3     R1      r3      r1 */\
        "movq 80(%2), %%mm4             \n\t" /* -C1    C5      -C1     C5 */\
        "movq %%mm2, 24+" #dst "        \n\t"\
        "pmaddwd %%mm1, %%mm4           \n\t" /* -C1R3+C5R1     -C1r3+C5r1 */\
        "movq 88(%2), %%mm7             \n\t" /* C3     C7      C3      C7 */\
        "pmaddwd 96(%2), %%mm1          \n\t" /* -C5R3+C7R1     -C5r3+C7r1 */\
        "pmaddwd %%mm3, %%mm7           \n\t" /* C3R7+C7R5      C3r7+C7r5 */\
        "movq %%mm0, %%mm2              \n\t" /* A2             a2 */\
        "pmaddwd 104(%2), %%mm3         \n\t" /* -C1R7+C3R5     -C1r7+C3r5 */\
        "paddd %%mm7, %%mm4             \n\t" /* B2             b2 */\
        "paddd %%mm4, %%mm2             \n\t" /* A2+B2          a2+b2 */\
        "psubd %%mm4, %%mm0             \n\t" /* a2-B2          a2-b2 */\
        "psrad $" #shift ", %%mm2       \n\t"\
        "psrad $" #shift ", %%mm0       \n\t"\
        "movq %%mm6, %%mm4              \n\t" /* A3             a3 */\
        "paddd %%mm1, %%mm3             \n\t" /* B3             b3 */\
        "paddd %%mm3, %%mm6             \n\t" /* A3+B3          a3+b3 */\
        "psubd %%mm3, %%mm4             \n\t" /* a3-B3          a3-b3 */\
        "psrad $" #shift ", %%mm6       \n\t"\
        "packssdw %%mm6, %%mm2          \n\t" /* A3+B3  a3+b3   A2+B2   a2+b2 */\
        "movq %%mm2, 8+" #dst "         \n\t"\
        "psrad $" #shift ", %%mm4       \n\t"\
        "packssdw %%mm0, %%mm4          \n\t" /* A2-B2  a2-b2   A3-B3   a3-b3 */\
        "movq %%mm4, 16+" #dst "        \n\t"\
        "jmp 2f                         \n\t"\
        "1:                             \n\t"\
        "pslld $16, %%mm0               \n\t"\
        "#paddd "MANGLE(d40000)", %%mm0 \n\t"\
        "psrad $13, %%mm0               \n\t"\
        "packssdw %%mm0, %%mm0          \n\t"\
        "movq %%mm0, " #dst "           \n\t"\
        "movq %%mm0, 8+" #dst "         \n\t"\
        "movq %%mm0, 16+" #dst "        \n\t"\
        "movq %%mm0, 24+" #dst "        \n\t"\
        "2:                             \n\t"


//IDCT(      src0,   src4,   src1,   src5,    dst,    rounder, shift)
ROW_IDCT(    (%0),  8(%0), 16(%0), 24(%0),  0(%1),paddd 8(%2), 11)
/*ROW_IDCT(  32(%0), 40(%0), 48(%0), 56(%0), 32(%1), paddd (%2), 11)
ROW_IDCT(  64(%0), 72(%0), 80(%0), 88(%0), 64(%1), paddd (%2), 11)
ROW_IDCT(  96(%0),104(%0),112(%0),120(%0), 96(%1), paddd (%2), 11)*/

DC_COND_ROW_IDCT(  32(%0), 40(%0), 48(%0), 56(%0), 32(%1),paddd (%2), 11)
DC_COND_ROW_IDCT(  64(%0), 72(%0), 80(%0), 88(%0), 64(%1),paddd (%2), 11)
DC_COND_ROW_IDCT(  96(%0),104(%0),112(%0),120(%0), 96(%1),paddd (%2), 11)


//IDCT(      src0,   src4,   src1,    src5,    dst, shift)
COL_IDCT(    (%1), 64(%1), 32(%1),  96(%1),  0(%0), 20)
COL_IDCT(   8(%1), 72(%1), 40(%1), 104(%1),  4(%0), 20)
COL_IDCT(  16(%1), 80(%1), 48(%1), 112(%1),  8(%0), 20)
COL_IDCT(  24(%1), 88(%1), 56(%1), 120(%1), 12(%0), 20)

#else

#define DC_COND_IDCT(src0, src4, src1, src5, dst, rounder, shift) \
        "movq " #src0 ", %%mm0          \n\t" /* R4     R0      r4      r0 */\
        "movq " #src4 ", %%mm1          \n\t" /* R6     R2      r6      r2 */\
        "movq " #src1 ", %%mm2          \n\t" /* R3     R1      r3      r1 */\
        "movq " #src5 ", %%mm3          \n\t" /* R7     R5      r7      r5 */\
        "movq "MANGLE(wm1010)", %%mm4   \n\t"\
        "pand %%mm0, %%mm4              \n\t"\
        "por %%mm1, %%mm4               \n\t"\
        "por %%mm2, %%mm4               \n\t"\
        "por %%mm3, %%mm4               \n\t"\
        "packssdw %%mm4,%%mm4           \n\t"\
        "movd %%mm4, %%eax              \n\t"\
        "orl %%eax, %%eax               \n\t"\
        "jz 1f                          \n\t"\
        "movq 16(%2), %%mm4             \n\t" /* C4     C4      C4      C4 */\
        "pmaddwd %%mm0, %%mm4           \n\t" /* C4R4+C4R0      C4r4+C4r0 */\
        "movq 24(%2), %%mm5             \n\t" /* -C4    C4      -C4     C4 */\
        "pmaddwd %%mm5, %%mm0           \n\t" /* -C4R4+C4R0     -C4r4+C4r0 */\
        "movq 32(%2), %%mm5             \n\t" /* C6     C2      C6      C2 */\
        "pmaddwd %%mm1, %%mm5           \n\t" /* C6R6+C2R2      C6r6+C2r2 */\
        "movq 40(%2), %%mm6             \n\t" /* -C2    C6      -C2     C6 */\
        "pmaddwd %%mm6, %%mm1           \n\t" /* -C2R6+C6R2     -C2r6+C6r2 */\
        "movq 48(%2), %%mm7             \n\t" /* C3     C1      C3      C1 */\
        "pmaddwd %%mm2, %%mm7           \n\t" /* C3R3+C1R1      C3r3+C1r1 */\
        #rounder ", %%mm4               \n\t"\
        "movq %%mm4, %%mm6              \n\t" /* C4R4+C4R0      C4r4+C4r0 */\
        "paddd %%mm5, %%mm4             \n\t" /* A0             a0 */\
        "psubd %%mm5, %%mm6             \n\t" /* A3             a3 */\
        "movq 56(%2), %%mm5             \n\t" /* C7     C5      C7      C5 */\
        "pmaddwd %%mm3, %%mm5           \n\t" /* C7R7+C5R5      C7r7+C5r5 */\
        #rounder ", %%mm0               \n\t"\
        "paddd %%mm0, %%mm1             \n\t" /* A1             a1 */\
        "paddd %%mm0, %%mm0             \n\t" \
        "psubd %%mm1, %%mm0             \n\t" /* A2             a2 */\
        "pmaddwd 64(%2), %%mm2          \n\t" /* -C7R3+C3R1     -C7r3+C3r1 */\
        "paddd %%mm5, %%mm7             \n\t" /* B0             b0 */\
        "movq 72(%2), %%mm5             \n\t" /* -C5    -C1     -C5     -C1 */\
        "pmaddwd %%mm3, %%mm5           \n\t" /* -C5R7-C1R5     -C5r7-C1r5 */\
        "paddd %%mm4, %%mm7             \n\t" /* A0+B0          a0+b0 */\
        "paddd %%mm4, %%mm4             \n\t" /* 2A0            2a0 */\
        "psubd %%mm7, %%mm4             \n\t" /* A0-B0          a0-b0 */\
        "paddd %%mm2, %%mm5             \n\t" /* B1             b1 */\
        "psrad $" #shift ", %%mm7       \n\t"\
        "psrad $" #shift ", %%mm4       \n\t"\
        "movq %%mm1, %%mm2              \n\t" /* A1             a1 */\
        "paddd %%mm5, %%mm1             \n\t" /* A1+B1          a1+b1 */\
        "psubd %%mm5, %%mm2             \n\t" /* A1-B1          a1-b1 */\
        "psrad $" #shift ", %%mm1       \n\t"\
        "psrad $" #shift ", %%mm2       \n\t"\
        "packssdw %%mm1, %%mm7          \n\t" /* A1+B1  a1+b1   A0+B0   a0+b0 */\
        "packssdw %%mm4, %%mm2          \n\t" /* A0-B0  a0-b0   A1-B1   a1-b1 */\
        "movq %%mm7, " #dst "           \n\t"\
        "movq " #src1 ", %%mm1          \n\t" /* R3     R1      r3      r1 */\
        "movq 80(%2), %%mm4             \n\t" /* -C1    C5      -C1     C5 */\
        "movq %%mm2, 24+" #dst "        \n\t"\
        "pmaddwd %%mm1, %%mm4           \n\t" /* -C1R3+C5R1     -C1r3+C5r1 */\
        "movq 88(%2), %%mm7             \n\t" /* C3     C7      C3      C7 */\
        "pmaddwd 96(%2), %%mm1          \n\t" /* -C5R3+C7R1     -C5r3+C7r1 */\
        "pmaddwd %%mm3, %%mm7           \n\t" /* C3R7+C7R5      C3r7+C7r5 */\
        "movq %%mm0, %%mm2              \n\t" /* A2             a2 */\
        "pmaddwd 104(%2), %%mm3         \n\t" /* -C1R7+C3R5     -C1r7+C3r5 */\
        "paddd %%mm7, %%mm4             \n\t" /* B2             b2 */\
        "paddd %%mm4, %%mm2             \n\t" /* A2+B2          a2+b2 */\
        "psubd %%mm4, %%mm0             \n\t" /* a2-B2          a2-b2 */\
        "psrad $" #shift ", %%mm2       \n\t"\
        "psrad $" #shift ", %%mm0       \n\t"\
        "movq %%mm6, %%mm4              \n\t" /* A3             a3 */\
        "paddd %%mm1, %%mm3             \n\t" /* B3             b3 */\
        "paddd %%mm3, %%mm6             \n\t" /* A3+B3          a3+b3 */\
        "psubd %%mm3, %%mm4             \n\t" /* a3-B3          a3-b3 */\
        "psrad $" #shift ", %%mm6       \n\t"\
        "packssdw %%mm6, %%mm2          \n\t" /* A3+B3  a3+b3   A2+B2   a2+b2 */\
        "movq %%mm2, 8+" #dst "         \n\t"\
        "psrad $" #shift ", %%mm4       \n\t"\
        "packssdw %%mm0, %%mm4          \n\t" /* A2-B2  a2-b2   A3-B3   a3-b3 */\
        "movq %%mm4, 16+" #dst "        \n\t"\
        "jmp 2f                         \n\t"\
        "1:                             \n\t"\
        "pslld $16, %%mm0               \n\t"\
        "paddd "MANGLE(d40000)", %%mm0  \n\t"\
        "psrad $13, %%mm0               \n\t"\
        "packssdw %%mm0, %%mm0          \n\t"\
        "movq %%mm0, " #dst "           \n\t"\
        "movq %%mm0, 8+" #dst "         \n\t"\
        "movq %%mm0, 16+" #dst "        \n\t"\
        "movq %%mm0, 24+" #dst "        \n\t"\
        "2:                             \n\t"

#define Z_COND_IDCT(src0, src4, src1, src5, dst, rounder, shift, bt) \
        "movq " #src0 ", %%mm0          \n\t" /* R4     R0      r4      r0 */\
        "movq " #src4 ", %%mm1          \n\t" /* R6     R2      r6      r2 */\
        "movq " #src1 ", %%mm2          \n\t" /* R3     R1      r3      r1 */\
        "movq " #src5 ", %%mm3          \n\t" /* R7     R5      r7      r5 */\
        "movq %%mm0, %%mm4              \n\t"\
        "por %%mm1, %%mm4               \n\t"\
        "por %%mm2, %%mm4               \n\t"\
        "por %%mm3, %%mm4               \n\t"\
        "packssdw %%mm4,%%mm4           \n\t"\
        "movd %%mm4, %%eax              \n\t"\
        "orl %%eax, %%eax               \n\t"\
        "jz " #bt "                     \n\t"\
        "movq 16(%2), %%mm4             \n\t" /* C4     C4      C4      C4 */\
        "pmaddwd %%mm0, %%mm4           \n\t" /* C4R4+C4R0      C4r4+C4r0 */\
        "movq 24(%2), %%mm5             \n\t" /* -C4    C4      -C4     C4 */\
        "pmaddwd %%mm5, %%mm0           \n\t" /* -C4R4+C4R0     -C4r4+C4r0 */\
        "movq 32(%2), %%mm5             \n\t" /* C6     C2      C6      C2 */\
        "pmaddwd %%mm1, %%mm5           \n\t" /* C6R6+C2R2      C6r6+C2r2 */\
        "movq 40(%2), %%mm6             \n\t" /* -C2    C6      -C2     C6 */\
        "pmaddwd %%mm6, %%mm1           \n\t" /* -C2R6+C6R2     -C2r6+C6r2 */\
        "movq 48(%2), %%mm7             \n\t" /* C3     C1      C3      C1 */\
        "pmaddwd %%mm2, %%mm7           \n\t" /* C3R3+C1R1      C3r3+C1r1 */\
        #rounder ", %%mm4               \n\t"\
        "movq %%mm4, %%mm6              \n\t" /* C4R4+C4R0      C4r4+C4r0 */\
        "paddd %%mm5, %%mm4             \n\t" /* A0             a0 */\
        "psubd %%mm5, %%mm6             \n\t" /* A3             a3 */\
        "movq 56(%2), %%mm5             \n\t" /* C7     C5      C7      C5 */\
        "pmaddwd %%mm3, %%mm5           \n\t" /* C7R7+C5R5      C7r7+C5r5 */\
        #rounder ", %%mm0               \n\t"\
        "paddd %%mm0, %%mm1             \n\t" /* A1             a1 */\
        "paddd %%mm0, %%mm0             \n\t" \
        "psubd %%mm1, %%mm0             \n\t" /* A2             a2 */\
        "pmaddwd 64(%2), %%mm2          \n\t" /* -C7R3+C3R1     -C7r3+C3r1 */\
        "paddd %%mm5, %%mm7             \n\t" /* B0             b0 */\
        "movq 72(%2), %%mm5             \n\t" /* -C5    -C1     -C5     -C1 */\
        "pmaddwd %%mm3, %%mm5           \n\t" /* -C5R7-C1R5     -C5r7-C1r5 */\
        "paddd %%mm4, %%mm7             \n\t" /* A0+B0          a0+b0 */\
        "paddd %%mm4, %%mm4             \n\t" /* 2A0            2a0 */\
        "psubd %%mm7, %%mm4             \n\t" /* A0-B0          a0-b0 */\
        "paddd %%mm2, %%mm5             \n\t" /* B1             b1 */\
        "psrad $" #shift ", %%mm7       \n\t"\
        "psrad $" #shift ", %%mm4       \n\t"\
        "movq %%mm1, %%mm2              \n\t" /* A1             a1 */\
        "paddd %%mm5, %%mm1             \n\t" /* A1+B1          a1+b1 */\
        "psubd %%mm5, %%mm2             \n\t" /* A1-B1          a1-b1 */\
        "psrad $" #shift ", %%mm1       \n\t"\
        "psrad $" #shift ", %%mm2       \n\t"\
        "packssdw %%mm1, %%mm7          \n\t" /* A1+B1  a1+b1   A0+B0   a0+b0 */\
        "packssdw %%mm4, %%mm2          \n\t" /* A0-B0  a0-b0   A1-B1   a1-b1 */\
        "movq %%mm7, " #dst "           \n\t"\
        "movq " #src1 ", %%mm1          \n\t" /* R3     R1      r3      r1 */\
        "movq 80(%2), %%mm4             \n\t" /* -C1    C5      -C1     C5 */\
        "movq %%mm2, 24+" #dst "        \n\t"\
        "pmaddwd %%mm1, %%mm4           \n\t" /* -C1R3+C5R1     -C1r3+C5r1 */\
        "movq 88(%2), %%mm7             \n\t" /* C3     C7      C3      C7 */\
        "pmaddwd 96(%2), %%mm1          \n\t" /* -C5R3+C7R1     -C5r3+C7r1 */\
        "pmaddwd %%mm3, %%mm7           \n\t" /* C3R7+C7R5      C3r7+C7r5 */\
        "movq %%mm0, %%mm2              \n\t" /* A2             a2 */\
        "pmaddwd 104(%2), %%mm3         \n\t" /* -C1R7+C3R5     -C1r7+C3r5 */\
        "paddd %%mm7, %%mm4             \n\t" /* B2             b2 */\
        "paddd %%mm4, %%mm2             \n\t" /* A2+B2          a2+b2 */\
        "psubd %%mm4, %%mm0             \n\t" /* a2-B2          a2-b2 */\
        "psrad $" #shift ", %%mm2       \n\t"\
        "psrad $" #shift ", %%mm0       \n\t"\
        "movq %%mm6, %%mm4              \n\t" /* A3             a3 */\
        "paddd %%mm1, %%mm3             \n\t" /* B3             b3 */\
        "paddd %%mm3, %%mm6             \n\t" /* A3+B3          a3+b3 */\
        "psubd %%mm3, %%mm4             \n\t" /* a3-B3          a3-b3 */\
        "psrad $" #shift ", %%mm6       \n\t"\
        "packssdw %%mm6, %%mm2          \n\t" /* A3+B3  a3+b3   A2+B2   a2+b2 */\
        "movq %%mm2, 8+" #dst "         \n\t"\
        "psrad $" #shift ", %%mm4       \n\t"\
        "packssdw %%mm0, %%mm4          \n\t" /* A2-B2  a2-b2   A3-B3   a3-b3 */\
        "movq %%mm4, 16+" #dst "        \n\t"\

#define ROW_IDCT(src0, src4, src1, src5, dst, rounder, shift) \
        "movq " #src0 ", %%mm0          \n\t" /* R4     R0      r4      r0 */\
        "movq " #src4 ", %%mm1          \n\t" /* R6     R2      r6      r2 */\
        "movq " #src1 ", %%mm2          \n\t" /* R3     R1      r3      r1 */\
        "movq " #src5 ", %%mm3          \n\t" /* R7     R5      r7      r5 */\
        "movq 16(%2), %%mm4             \n\t" /* C4     C4      C4      C4 */\
        "pmaddwd %%mm0, %%mm4           \n\t" /* C4R4+C4R0      C4r4+C4r0 */\
        "movq 24(%2), %%mm5             \n\t" /* -C4    C4      -C4     C4 */\
        "pmaddwd %%mm5, %%mm0           \n\t" /* -C4R4+C4R0     -C4r4+C4r0 */\
        "movq 32(%2), %%mm5             \n\t" /* C6     C2      C6      C2 */\
        "pmaddwd %%mm1, %%mm5           \n\t" /* C6R6+C2R2      C6r6+C2r2 */\
        "movq 40(%2), %%mm6             \n\t" /* -C2    C6      -C2     C6 */\
        "pmaddwd %%mm6, %%mm1           \n\t" /* -C2R6+C6R2     -C2r6+C6r2 */\
        "movq 48(%2), %%mm7             \n\t" /* C3     C1      C3      C1 */\
        "pmaddwd %%mm2, %%mm7           \n\t" /* C3R3+C1R1      C3r3+C1r1 */\
        #rounder ", %%mm4               \n\t"\
        "movq %%mm4, %%mm6              \n\t" /* C4R4+C4R0      C4r4+C4r0 */\
        "paddd %%mm5, %%mm4             \n\t" /* A0             a0 */\
        "psubd %%mm5, %%mm6             \n\t" /* A3             a3 */\
        "movq 56(%2), %%mm5             \n\t" /* C7     C5      C7      C5 */\
        "pmaddwd %%mm3, %%mm5           \n\t" /* C7R7+C5R5      C7r7+C5r5 */\
        #rounder ", %%mm0               \n\t"\
        "paddd %%mm0, %%mm1             \n\t" /* A1             a1 */\
        "paddd %%mm0, %%mm0             \n\t" \
        "psubd %%mm1, %%mm0             \n\t" /* A2             a2 */\
        "pmaddwd 64(%2), %%mm2          \n\t" /* -C7R3+C3R1     -C7r3+C3r1 */\
        "paddd %%mm5, %%mm7             \n\t" /* B0             b0 */\
        "movq 72(%2), %%mm5             \n\t" /* -C5    -C1     -C5     -C1 */\
        "pmaddwd %%mm3, %%mm5           \n\t" /* -C5R7-C1R5     -C5r7-C1r5 */\
        "paddd %%mm4, %%mm7             \n\t" /* A0+B0          a0+b0 */\
        "paddd %%mm4, %%mm4             \n\t" /* 2A0            2a0 */\
        "psubd %%mm7, %%mm4             \n\t" /* A0-B0          a0-b0 */\
        "paddd %%mm2, %%mm5             \n\t" /* B1             b1 */\
        "psrad $" #shift ", %%mm7       \n\t"\
        "psrad $" #shift ", %%mm4       \n\t"\
        "movq %%mm1, %%mm2              \n\t" /* A1             a1 */\
        "paddd %%mm5, %%mm1             \n\t" /* A1+B1          a1+b1 */\
        "psubd %%mm5, %%mm2             \n\t" /* A1-B1          a1-b1 */\
        "psrad $" #shift ", %%mm1       \n\t"\
        "psrad $" #shift ", %%mm2       \n\t"\
        "packssdw %%mm1, %%mm7          \n\t" /* A1+B1  a1+b1   A0+B0   a0+b0 */\
        "packssdw %%mm4, %%mm2          \n\t" /* A0-B0  a0-b0   A1-B1   a1-b1 */\
        "movq %%mm7, " #dst "           \n\t"\
        "movq " #src1 ", %%mm1          \n\t" /* R3     R1      r3      r1 */\
        "movq 80(%2), %%mm4             \n\t" /* -C1    C5      -C1     C5 */\
        "movq %%mm2, 24+" #dst "        \n\t"\
        "pmaddwd %%mm1, %%mm4           \n\t" /* -C1R3+C5R1     -C1r3+C5r1 */\
        "movq 88(%2), %%mm7             \n\t" /* C3     C7      C3      C7 */\
        "pmaddwd 96(%2), %%mm1          \n\t" /* -C5R3+C7R1     -C5r3+C7r1 */\
        "pmaddwd %%mm3, %%mm7           \n\t" /* C3R7+C7R5      C3r7+C7r5 */\
        "movq %%mm0, %%mm2              \n\t" /* A2             a2 */\
        "pmaddwd 104(%2), %%mm3         \n\t" /* -C1R7+C3R5     -C1r7+C3r5 */\
        "paddd %%mm7, %%mm4             \n\t" /* B2             b2 */\
        "paddd %%mm4, %%mm2             \n\t" /* A2+B2          a2+b2 */\
        "psubd %%mm4, %%mm0             \n\t" /* a2-B2          a2-b2 */\
        "psrad $" #shift ", %%mm2       \n\t"\
        "psrad $" #shift ", %%mm0       \n\t"\
        "movq %%mm6, %%mm4              \n\t" /* A3             a3 */\
        "paddd %%mm1, %%mm3             \n\t" /* B3             b3 */\
        "paddd %%mm3, %%mm6             \n\t" /* A3+B3          a3+b3 */\
        "psubd %%mm3, %%mm4             \n\t" /* a3-B3          a3-b3 */\
        "psrad $" #shift ", %%mm6       \n\t"\
        "packssdw %%mm6, %%mm2          \n\t" /* A3+B3  a3+b3   A2+B2   a2+b2 */\
        "movq %%mm2, 8+" #dst "         \n\t"\
        "psrad $" #shift ", %%mm4       \n\t"\
        "packssdw %%mm0, %%mm4          \n\t" /* A2-B2  a2-b2   A3-B3   a3-b3 */\
        "movq %%mm4, 16+" #dst "        \n\t"\

//IDCT(         src0,   src4,   src1,   src5,    dst,   rounder, shift)
DC_COND_IDCT(  0(%0),  8(%0), 16(%0), 24(%0),  0(%1),paddd 8(%2), 11)
Z_COND_IDCT(  32(%0), 40(%0), 48(%0), 56(%0), 32(%1),paddd (%2), 11, 4f)
Z_COND_IDCT(  64(%0), 72(%0), 80(%0), 88(%0), 64(%1),paddd (%2), 11, 2f)
Z_COND_IDCT(  96(%0),104(%0),112(%0),120(%0), 96(%1),paddd (%2), 11, 1f)

#undef IDCT
#define IDCT(src0, src4, src1, src5, dst, shift) \
        "movq " #src0 ", %%mm0          \n\t" /* R4     R0      r4      r0 */\
        "movq " #src4 ", %%mm1          \n\t" /* R6     R2      r6      r2 */\
        "movq " #src1 ", %%mm2          \n\t" /* R3     R1      r3      r1 */\
        "movq " #src5 ", %%mm3          \n\t" /* R7     R5      r7      r5 */\
        "movq 16(%2), %%mm4             \n\t" /* C4     C4      C4      C4 */\
        "pmaddwd %%mm0, %%mm4           \n\t" /* C4R4+C4R0      C4r4+C4r0 */\
        "movq 24(%2), %%mm5             \n\t" /* -C4    C4      -C4     C4 */\
        "pmaddwd %%mm5, %%mm0           \n\t" /* -C4R4+C4R0     -C4r4+C4r0 */\
        "movq 32(%2), %%mm5             \n\t" /* C6     C2      C6      C2 */\
        "pmaddwd %%mm1, %%mm5           \n\t" /* C6R6+C2R2      C6r6+C2r2 */\
        "movq 40(%2), %%mm6             \n\t" /* -C2    C6      -C2     C6 */\
        "pmaddwd %%mm6, %%mm1           \n\t" /* -C2R6+C6R2     -C2r6+C6r2 */\
        "movq %%mm4, %%mm6              \n\t" /* C4R4+C4R0      C4r4+C4r0 */\
        "movq 48(%2), %%mm7             \n\t" /* C3     C1      C3      C1 */\
        "pmaddwd %%mm2, %%mm7           \n\t" /* C3R3+C1R1      C3r3+C1r1 */\
        "paddd %%mm5, %%mm4             \n\t" /* A0             a0 */\
        "psubd %%mm5, %%mm6             \n\t" /* A3             a3 */\
        "movq %%mm0, %%mm5              \n\t" /* -C4R4+C4R0     -C4r4+C4r0 */\
        "paddd %%mm1, %%mm0             \n\t" /* A1             a1 */\
        "psubd %%mm1, %%mm5             \n\t" /* A2             a2 */\
        "movq 56(%2), %%mm1             \n\t" /* C7     C5      C7      C5 */\
        "pmaddwd %%mm3, %%mm1           \n\t" /* C7R7+C5R5      C7r7+C5r5 */\
        "pmaddwd 64(%2), %%mm2          \n\t" /* -C7R3+C3R1     -C7r3+C3r1 */\
        "paddd %%mm1, %%mm7             \n\t" /* B0             b0 */\
        "movq 72(%2), %%mm1             \n\t" /* -C5    -C1     -C5     -C1 */\
        "pmaddwd %%mm3, %%mm1           \n\t" /* -C5R7-C1R5     -C5r7-C1r5 */\
        "paddd %%mm4, %%mm7             \n\t" /* A0+B0          a0+b0 */\
        "paddd %%mm4, %%mm4             \n\t" /* 2A0            2a0 */\
        "psubd %%mm7, %%mm4             \n\t" /* A0-B0          a0-b0 */\
        "paddd %%mm2, %%mm1             \n\t" /* B1             b1 */\
        "psrad $" #shift ", %%mm7       \n\t"\
        "psrad $" #shift ", %%mm4       \n\t"\
        "movq %%mm0, %%mm2              \n\t" /* A1             a1 */\
        "paddd %%mm1, %%mm0             \n\t" /* A1+B1          a1+b1 */\
        "psubd %%mm1, %%mm2             \n\t" /* A1-B1          a1-b1 */\
        "psrad $" #shift ", %%mm0       \n\t"\
        "psrad $" #shift ", %%mm2       \n\t"\
        "packssdw %%mm7, %%mm7          \n\t" /* A0+B0  a0+b0 */\
        "movd %%mm7, " #dst "           \n\t"\
        "packssdw %%mm0, %%mm0          \n\t" /* A1+B1  a1+b1 */\
        "movd %%mm0, 16+" #dst "        \n\t"\
        "packssdw %%mm2, %%mm2          \n\t" /* A1-B1  a1-b1 */\
        "movd %%mm2, 96+" #dst "        \n\t"\
        "packssdw %%mm4, %%mm4          \n\t" /* A0-B0  a0-b0 */\
        "movd %%mm4, 112+" #dst "       \n\t"\
        "movq " #src1 ", %%mm0          \n\t" /* R3     R1      r3      r1 */\
        "movq 80(%2), %%mm4             \n\t" /* -C1    C5      -C1     C5 */\
        "pmaddwd %%mm0, %%mm4           \n\t" /* -C1R3+C5R1     -C1r3+C5r1 */\
        "movq 88(%2), %%mm7             \n\t" /* C3     C7      C3      C7 */\
        "pmaddwd 96(%2), %%mm0          \n\t" /* -C5R3+C7R1     -C5r3+C7r1 */\
        "pmaddwd %%mm3, %%mm7           \n\t" /* C3R7+C7R5      C3r7+C7r5 */\
        "movq %%mm5, %%mm2              \n\t" /* A2             a2 */\
        "pmaddwd 104(%2), %%mm3         \n\t" /* -C1R7+C3R5     -C1r7+C3r5 */\
        "paddd %%mm7, %%mm4             \n\t" /* B2             b2 */\
        "paddd %%mm4, %%mm2             \n\t" /* A2+B2          a2+b2 */\
        "psubd %%mm4, %%mm5             \n\t" /* a2-B2          a2-b2 */\
        "psrad $" #shift ", %%mm2       \n\t"\
        "psrad $" #shift ", %%mm5       \n\t"\
        "movq %%mm6, %%mm4              \n\t" /* A3             a3 */\
        "paddd %%mm0, %%mm3             \n\t" /* B3             b3 */\
        "paddd %%mm3, %%mm6             \n\t" /* A3+B3          a3+b3 */\
        "psubd %%mm3, %%mm4             \n\t" /* a3-B3          a3-b3 */\
        "psrad $" #shift ", %%mm6       \n\t"\
        "psrad $" #shift ", %%mm4       \n\t"\
        "packssdw %%mm2, %%mm2          \n\t" /* A2+B2  a2+b2 */\
        "packssdw %%mm6, %%mm6          \n\t" /* A3+B3  a3+b3 */\
        "movd %%mm2, 32+" #dst "        \n\t"\
        "packssdw %%mm4, %%mm4          \n\t" /* A3-B3  a3-b3 */\
        "packssdw %%mm5, %%mm5          \n\t" /* A2-B2  a2-b2 */\
        "movd %%mm6, 48+" #dst "        \n\t"\
        "movd %%mm4, 64+" #dst "        \n\t"\
        "movd %%mm5, 80+" #dst "        \n\t"


//IDCT(  src0,   src4,   src1,    src5,    dst, shift)
IDCT(    (%1), 64(%1), 32(%1),  96(%1),  0(%0), 20)
IDCT(   8(%1), 72(%1), 40(%1), 104(%1),  4(%0), 20)
IDCT(  16(%1), 80(%1), 48(%1), 112(%1),  8(%0), 20)
IDCT(  24(%1), 88(%1), 56(%1), 120(%1), 12(%0), 20)
        "jmp 9f                         \n\t"

        "# .p2align 4                   \n\t"\
        "4:                             \n\t"
Z_COND_IDCT(  64(%0), 72(%0), 80(%0), 88(%0), 64(%1),paddd (%2), 11, 6f)
Z_COND_IDCT(  96(%0),104(%0),112(%0),120(%0), 96(%1),paddd (%2), 11, 5f)

#undef IDCT
#define IDCT(src0, src4, src1, src5, dst, shift) \
        "movq " #src0 ", %%mm0          \n\t" /* R4     R0      r4      r0 */\
        "movq " #src4 ", %%mm1          \n\t" /* R6     R2      r6      r2 */\
        "movq " #src5 ", %%mm3          \n\t" /* R7     R5      r7      r5 */\
        "movq 16(%2), %%mm4             \n\t" /* C4     C4      C4      C4 */\
        "pmaddwd %%mm0, %%mm4           \n\t" /* C4R4+C4R0      C4r4+C4r0 */\
        "movq 24(%2), %%mm5             \n\t" /* -C4    C4      -C4     C4 */\
        "pmaddwd %%mm5, %%mm0           \n\t" /* -C4R4+C4R0     -C4r4+C4r0 */\
        "movq 32(%2), %%mm5             \n\t" /* C6     C2      C6      C2 */\
        "pmaddwd %%mm1, %%mm5           \n\t" /* C6R6+C2R2      C6r6+C2r2 */\
        "movq 40(%2), %%mm6             \n\t" /* -C2    C6      -C2     C6 */\
        "pmaddwd %%mm6, %%mm1           \n\t" /* -C2R6+C6R2     -C2r6+C6r2 */\
        "movq %%mm4, %%mm6              \n\t" /* C4R4+C4R0      C4r4+C4r0 */\
        "paddd %%mm5, %%mm4             \n\t" /* A0             a0 */\
        "psubd %%mm5, %%mm6             \n\t" /* A3             a3 */\
        "movq %%mm0, %%mm5              \n\t" /* -C4R4+C4R0     -C4r4+C4r0 */\
        "paddd %%mm1, %%mm0             \n\t" /* A1             a1 */\
        "psubd %%mm1, %%mm5             \n\t" /* A2             a2 */\
        "movq 56(%2), %%mm1             \n\t" /* C7     C5      C7      C5 */\
        "pmaddwd %%mm3, %%mm1           \n\t" /* C7R7+C5R5      C7r7+C5r5 */\
        "movq 72(%2), %%mm7             \n\t" /* -C5    -C1     -C5     -C1 */\
        "pmaddwd %%mm3, %%mm7           \n\t" /* -C5R7-C1R5     -C5r7-C1r5 */\
        "paddd %%mm4, %%mm1             \n\t" /* A0+B0          a0+b0 */\
        "paddd %%mm4, %%mm4             \n\t" /* 2A0            2a0 */\
        "psubd %%mm1, %%mm4             \n\t" /* A0-B0          a0-b0 */\
        "psrad $" #shift ", %%mm1       \n\t"\
        "psrad $" #shift ", %%mm4       \n\t"\
        "movq %%mm0, %%mm2              \n\t" /* A1             a1 */\
        "paddd %%mm7, %%mm0             \n\t" /* A1+B1          a1+b1 */\
        "psubd %%mm7, %%mm2             \n\t" /* A1-B1          a1-b1 */\
        "psrad $" #shift ", %%mm0       \n\t"\
        "psrad $" #shift ", %%mm2       \n\t"\
        "packssdw %%mm1, %%mm1          \n\t" /* A0+B0  a0+b0 */\
        "movd %%mm1, " #dst "           \n\t"\
        "packssdw %%mm0, %%mm0          \n\t" /* A1+B1  a1+b1 */\
        "movd %%mm0, 16+" #dst "        \n\t"\
        "packssdw %%mm2, %%mm2          \n\t" /* A1-B1  a1-b1 */\
        "movd %%mm2, 96+" #dst "        \n\t"\
        "packssdw %%mm4, %%mm4          \n\t" /* A0-B0  a0-b0 */\
        "movd %%mm4, 112+" #dst "       \n\t"\
        "movq 88(%2), %%mm1             \n\t" /* C3     C7      C3      C7 */\
        "pmaddwd %%mm3, %%mm1           \n\t" /* C3R7+C7R5      C3r7+C7r5 */\
        "movq %%mm5, %%mm2              \n\t" /* A2             a2 */\
        "pmaddwd 104(%2), %%mm3         \n\t" /* -C1R7+C3R5     -C1r7+C3r5 */\
        "paddd %%mm1, %%mm2             \n\t" /* A2+B2          a2+b2 */\
        "psubd %%mm1, %%mm5             \n\t" /* a2-B2          a2-b2 */\
        "psrad $" #shift ", %%mm2       \n\t"\
        "psrad $" #shift ", %%mm5       \n\t"\
        "movq %%mm6, %%mm1              \n\t" /* A3             a3 */\
        "paddd %%mm3, %%mm6             \n\t" /* A3+B3          a3+b3 */\
        "psubd %%mm3, %%mm1             \n\t" /* a3-B3          a3-b3 */\
        "psrad $" #shift ", %%mm6       \n\t"\
        "psrad $" #shift ", %%mm1       \n\t"\
        "packssdw %%mm2, %%mm2          \n\t" /* A2+B2  a2+b2 */\
        "packssdw %%mm6, %%mm6          \n\t" /* A3+B3  a3+b3 */\
        "movd %%mm2, 32+" #dst "        \n\t"\
        "packssdw %%mm1, %%mm1          \n\t" /* A3-B3  a3-b3 */\
        "packssdw %%mm5, %%mm5          \n\t" /* A2-B2  a2-b2 */\
        "movd %%mm6, 48+" #dst "        \n\t"\
        "movd %%mm1, 64+" #dst "        \n\t"\
        "movd %%mm5, 80+" #dst "        \n\t"

//IDCT(  src0,   src4,   src1,    src5,    dst, shift)
IDCT(    (%1), 64(%1), 32(%1),  96(%1),  0(%0), 20)
IDCT(   8(%1), 72(%1), 40(%1), 104(%1),  4(%0), 20)
IDCT(  16(%1), 80(%1), 48(%1), 112(%1),  8(%0), 20)
IDCT(  24(%1), 88(%1), 56(%1), 120(%1), 12(%0), 20)
        "jmp 9f                         \n\t"

        "# .p2align 4                   \n\t"\
        "6:                             \n\t"
Z_COND_IDCT(  96(%0),104(%0),112(%0),120(%0), 96(%1),paddd (%2), 11, 7f)

#undef IDCT
#define IDCT(src0, src4, src1, src5, dst, shift) \
        "movq " #src0 ", %%mm0          \n\t" /* R4     R0      r4      r0 */\
        "movq " #src5 ", %%mm3          \n\t" /* R7     R5      r7      r5 */\
        "movq 16(%2), %%mm4             \n\t" /* C4     C4      C4      C4 */\
        "pmaddwd %%mm0, %%mm4           \n\t" /* C4R4+C4R0      C4r4+C4r0 */\
        "movq 24(%2), %%mm5             \n\t" /* -C4    C4      -C4     C4 */\
        "pmaddwd %%mm5, %%mm0           \n\t" /* -C4R4+C4R0     -C4r4+C4r0 */\
        "movq %%mm4, %%mm6              \n\t" /* C4R4+C4R0      C4r4+C4r0 */\
        "movq %%mm0, %%mm5              \n\t" /* -C4R4+C4R0     -C4r4+C4r0 */\
        "movq 56(%2), %%mm1             \n\t" /* C7     C5      C7      C5 */\
        "pmaddwd %%mm3, %%mm1           \n\t" /* C7R7+C5R5      C7r7+C5r5 */\
        "movq 72(%2), %%mm7             \n\t" /* -C5    -C1     -C5     -C1 */\
        "pmaddwd %%mm3, %%mm7           \n\t" /* -C5R7-C1R5     -C5r7-C1r5 */\
        "paddd %%mm4, %%mm1             \n\t" /* A0+B0          a0+b0 */\
        "paddd %%mm4, %%mm4             \n\t" /* 2A0            2a0 */\
        "psubd %%mm1, %%mm4             \n\t" /* A0-B0          a0-b0 */\
        "psrad $" #shift ", %%mm1       \n\t"\
        "psrad $" #shift ", %%mm4       \n\t"\
        "movq %%mm0, %%mm2              \n\t" /* A1             a1 */\
        "paddd %%mm7, %%mm0             \n\t" /* A1+B1          a1+b1 */\
        "psubd %%mm7, %%mm2             \n\t" /* A1-B1          a1-b1 */\
        "psrad $" #shift ", %%mm0       \n\t"\
        "psrad $" #shift ", %%mm2       \n\t"\
        "packssdw %%mm1, %%mm1          \n\t" /* A0+B0  a0+b0 */\
        "movd %%mm1, " #dst "           \n\t"\
        "packssdw %%mm0, %%mm0          \n\t" /* A1+B1  a1+b1 */\
        "movd %%mm0, 16+" #dst "        \n\t"\
        "packssdw %%mm2, %%mm2          \n\t" /* A1-B1  a1-b1 */\
        "movd %%mm2, 96+" #dst "        \n\t"\
        "packssdw %%mm4, %%mm4          \n\t" /* A0-B0  a0-b0 */\
        "movd %%mm4, 112+" #dst "       \n\t"\
        "movq 88(%2), %%mm1             \n\t" /* C3     C7      C3      C7 */\
        "pmaddwd %%mm3, %%mm1           \n\t" /* C3R7+C7R5      C3r7+C7r5 */\
        "movq %%mm5, %%mm2              \n\t" /* A2             a2 */\
        "pmaddwd 104(%2), %%mm3         \n\t" /* -C1R7+C3R5     -C1r7+C3r5 */\
        "paddd %%mm1, %%mm2             \n\t" /* A2+B2          a2+b2 */\
        "psubd %%mm1, %%mm5             \n\t" /* a2-B2          a2-b2 */\
        "psrad $" #shift ", %%mm2       \n\t"\
        "psrad $" #shift ", %%mm5       \n\t"\
        "movq %%mm6, %%mm1              \n\t" /* A3             a3 */\
        "paddd %%mm3, %%mm6             \n\t" /* A3+B3          a3+b3 */\
        "psubd %%mm3, %%mm1             \n\t" /* a3-B3          a3-b3 */\
        "psrad $" #shift ", %%mm6       \n\t"\
        "psrad $" #shift ", %%mm1       \n\t"\
        "packssdw %%mm2, %%mm2          \n\t" /* A2+B2  a2+b2 */\
        "packssdw %%mm6, %%mm6          \n\t" /* A3+B3  a3+b3 */\
        "movd %%mm2, 32+" #dst "        \n\t"\
        "packssdw %%mm1, %%mm1          \n\t" /* A3-B3  a3-b3 */\
        "packssdw %%mm5, %%mm5          \n\t" /* A2-B2  a2-b2 */\
        "movd %%mm6, 48+" #dst "        \n\t"\
        "movd %%mm1, 64+" #dst "        \n\t"\
        "movd %%mm5, 80+" #dst "        \n\t"


//IDCT(  src0,   src4,   src1,    src5,    dst, shift)
IDCT(    (%1), 64(%1), 32(%1),  96(%1),  0(%0), 20)
IDCT(   8(%1), 72(%1), 40(%1), 104(%1),  4(%0), 20)
IDCT(  16(%1), 80(%1), 48(%1), 112(%1),  8(%0), 20)
IDCT(  24(%1), 88(%1), 56(%1), 120(%1), 12(%0), 20)
        "jmp 9f                         \n\t"

        "# .p2align 4                   \n\t"\
        "2:                             \n\t"
Z_COND_IDCT(  96(%0),104(%0),112(%0),120(%0), 96(%1),paddd (%2), 11, 3f)

#undef IDCT
#define IDCT(src0, src4, src1, src5, dst, shift) \
        "movq " #src0 ", %%mm0          \n\t" /* R4     R0      r4      r0 */\
        "movq " #src1 ", %%mm2          \n\t" /* R3     R1      r3      r1 */\
        "movq " #src5 ", %%mm3          \n\t" /* R7     R5      r7      r5 */\
        "movq 16(%2), %%mm4             \n\t" /* C4     C4      C4      C4 */\
        "pmaddwd %%mm0, %%mm4           \n\t" /* C4R4+C4R0      C4r4+C4r0 */\
        "movq 24(%2), %%mm5             \n\t" /* -C4    C4      -C4     C4 */\
        "pmaddwd %%mm5, %%mm0           \n\t" /* -C4R4+C4R0     -C4r4+C4r0 */\
        "movq %%mm4, %%mm6              \n\t" /* C4R4+C4R0      C4r4+C4r0 */\
        "movq 48(%2), %%mm7             \n\t" /* C3     C1      C3      C1 */\
        "pmaddwd %%mm2, %%mm7           \n\t" /* C3R3+C1R1      C3r3+C1r1 */\
        "movq %%mm0, %%mm5              \n\t" /* -C4R4+C4R0     -C4r4+C4r0 */\
        "movq 56(%2), %%mm1             \n\t" /* C7     C5      C7      C5 */\
        "pmaddwd %%mm3, %%mm1           \n\t" /* C7R7+C5R5      C7r7+C5r5 */\
        "pmaddwd 64(%2), %%mm2          \n\t" /* -C7R3+C3R1     -C7r3+C3r1 */\
        "paddd %%mm1, %%mm7             \n\t" /* B0             b0 */\
        "movq 72(%2), %%mm1             \n\t" /* -C5    -C1     -C5     -C1 */\
        "pmaddwd %%mm3, %%mm1           \n\t" /* -C5R7-C1R5     -C5r7-C1r5 */\
        "paddd %%mm4, %%mm7             \n\t" /* A0+B0          a0+b0 */\
        "paddd %%mm4, %%mm4             \n\t" /* 2A0            2a0 */\
        "psubd %%mm7, %%mm4             \n\t" /* A0-B0          a0-b0 */\
        "paddd %%mm2, %%mm1             \n\t" /* B1             b1 */\
        "psrad $" #shift ", %%mm7       \n\t"\
        "psrad $" #shift ", %%mm4       \n\t"\
        "movq %%mm0, %%mm2              \n\t" /* A1             a1 */\
        "paddd %%mm1, %%mm0             \n\t" /* A1+B1          a1+b1 */\
        "psubd %%mm1, %%mm2             \n\t" /* A1-B1          a1-b1 */\
        "psrad $" #shift ", %%mm0       \n\t"\
        "psrad $" #shift ", %%mm2       \n\t"\
        "packssdw %%mm7, %%mm7          \n\t" /* A0+B0  a0+b0 */\
        "movd %%mm7, " #dst "           \n\t"\
        "packssdw %%mm0, %%mm0          \n\t" /* A1+B1  a1+b1 */\
        "movd %%mm0, 16+" #dst "        \n\t"\
        "packssdw %%mm2, %%mm2          \n\t" /* A1-B1  a1-b1 */\
        "movd %%mm2, 96+" #dst "        \n\t"\
        "packssdw %%mm4, %%mm4          \n\t" /* A0-B0  a0-b0 */\
        "movd %%mm4, 112+" #dst "       \n\t"\
        "movq " #src1 ", %%mm0          \n\t" /* R3     R1      r3      r1 */\
        "movq 80(%2), %%mm4             \n\t" /* -C1    C5      -C1     C5 */\
        "pmaddwd %%mm0, %%mm4           \n\t" /* -C1R3+C5R1     -C1r3+C5r1 */\
        "movq 88(%2), %%mm7             \n\t" /* C3     C7      C3      C7 */\
        "pmaddwd 96(%2), %%mm0          \n\t" /* -C5R3+C7R1     -C5r3+C7r1 */\
        "pmaddwd %%mm3, %%mm7           \n\t" /* C3R7+C7R5      C3r7+C7r5 */\
        "movq %%mm5, %%mm2              \n\t" /* A2             a2 */\
        "pmaddwd 104(%2), %%mm3         \n\t" /* -C1R7+C3R5     -C1r7+C3r5 */\
        "paddd %%mm7, %%mm4             \n\t" /* B2             b2 */\
        "paddd %%mm4, %%mm2             \n\t" /* A2+B2          a2+b2 */\
        "psubd %%mm4, %%mm5             \n\t" /* a2-B2          a2-b2 */\
        "psrad $" #shift ", %%mm2       \n\t"\
        "psrad $" #shift ", %%mm5       \n\t"\
        "movq %%mm6, %%mm4              \n\t" /* A3             a3 */\
        "paddd %%mm0, %%mm3             \n\t" /* B3             b3 */\
        "paddd %%mm3, %%mm6             \n\t" /* A3+B3          a3+b3 */\
        "psubd %%mm3, %%mm4             \n\t" /* a3-B3          a3-b3 */\
        "psrad $" #shift ", %%mm6       \n\t"\
        "psrad $" #shift ", %%mm4       \n\t"\
        "packssdw %%mm2, %%mm2          \n\t" /* A2+B2  a2+b2 */\
        "packssdw %%mm6, %%mm6          \n\t" /* A3+B3  a3+b3 */\
        "movd %%mm2, 32+" #dst "        \n\t"\
        "packssdw %%mm4, %%mm4          \n\t" /* A3-B3  a3-b3 */\
        "packssdw %%mm5, %%mm5          \n\t" /* A2-B2  a2-b2 */\
        "movd %%mm6, 48+" #dst "        \n\t"\
        "movd %%mm4, 64+" #dst "        \n\t"\
        "movd %%mm5, 80+" #dst "        \n\t"

//IDCT(  src0,   src4,   src1,    src5,    dst, shift)
IDCT(    (%1), 64(%1), 32(%1),  96(%1),  0(%0), 20)
IDCT(   8(%1), 72(%1), 40(%1), 104(%1),  4(%0), 20)
IDCT(  16(%1), 80(%1), 48(%1), 112(%1),  8(%0), 20)
IDCT(  24(%1), 88(%1), 56(%1), 120(%1), 12(%0), 20)
        "jmp 9f                         \n\t"

        "# .p2align 4                   \n\t"\
        "3:                             \n\t"
#undef IDCT
#define IDCT(src0, src4, src1, src5, dst, shift) \
        "movq " #src0 ", %%mm0          \n\t" /* R4     R0      r4      r0 */\
        "movq " #src1 ", %%mm2          \n\t" /* R3     R1      r3      r1 */\
        "movq 16(%2), %%mm4             \n\t" /* C4     C4      C4      C4 */\
        "pmaddwd %%mm0, %%mm4           \n\t" /* C4R4+C4R0      C4r4+C4r0 */\
        "movq 24(%2), %%mm5             \n\t" /* -C4    C4      -C4     C4 */\
        "pmaddwd %%mm5, %%mm0           \n\t" /* -C4R4+C4R0     -C4r4+C4r0 */\
        "movq %%mm4, %%mm6              \n\t" /* C4R4+C4R0      C4r4+C4r0 */\
        "movq 48(%2), %%mm7             \n\t" /* C3     C1      C3      C1 */\
        "pmaddwd %%mm2, %%mm7           \n\t" /* C3R3+C1R1      C3r3+C1r1 */\
        "movq %%mm0, %%mm5              \n\t" /* -C4R4+C4R0     -C4r4+C4r0 */\
        "movq 64(%2), %%mm3             \n\t"\
        "pmaddwd %%mm2, %%mm3           \n\t" /* -C7R3+C3R1     -C7r3+C3r1 */\
        "paddd %%mm4, %%mm7             \n\t" /* A0+B0          a0+b0 */\
        "paddd %%mm4, %%mm4             \n\t" /* 2A0            2a0 */\
        "psubd %%mm7, %%mm4             \n\t" /* A0-B0          a0-b0 */\
        "psrad $" #shift ", %%mm7       \n\t"\
        "psrad $" #shift ", %%mm4       \n\t"\
        "movq %%mm0, %%mm1              \n\t" /* A1             a1 */\
        "paddd %%mm3, %%mm0             \n\t" /* A1+B1          a1+b1 */\
        "psubd %%mm3, %%mm1             \n\t" /* A1-B1          a1-b1 */\
        "psrad $" #shift ", %%mm0       \n\t"\
        "psrad $" #shift ", %%mm1       \n\t"\
        "packssdw %%mm7, %%mm7          \n\t" /* A0+B0  a0+b0 */\
        "movd %%mm7, " #dst "           \n\t"\
        "packssdw %%mm0, %%mm0          \n\t" /* A1+B1  a1+b1 */\
        "movd %%mm0, 16+" #dst "        \n\t"\
        "packssdw %%mm1, %%mm1          \n\t" /* A1-B1  a1-b1 */\
        "movd %%mm1, 96+" #dst "        \n\t"\
        "packssdw %%mm4, %%mm4          \n\t" /* A0-B0  a0-b0 */\
        "movd %%mm4, 112+" #dst "       \n\t"\
        "movq 80(%2), %%mm4             \n\t" /* -C1    C5      -C1     C5 */\
        "pmaddwd %%mm2, %%mm4           \n\t" /* -C1R3+C5R1     -C1r3+C5r1 */\
        "pmaddwd 96(%2), %%mm2          \n\t" /* -C5R3+C7R1     -C5r3+C7r1 */\
        "movq %%mm5, %%mm1              \n\t" /* A2             a2 */\
        "paddd %%mm4, %%mm1             \n\t" /* A2+B2          a2+b2 */\
        "psubd %%mm4, %%mm5             \n\t" /* a2-B2          a2-b2 */\
        "psrad $" #shift ", %%mm1       \n\t"\
        "psrad $" #shift ", %%mm5       \n\t"\
        "movq %%mm6, %%mm4              \n\t" /* A3             a3 */\
        "paddd %%mm2, %%mm6             \n\t" /* A3+B3          a3+b3 */\
        "psubd %%mm2, %%mm4             \n\t" /* a3-B3          a3-b3 */\
        "psrad $" #shift ", %%mm6       \n\t"\
        "psrad $" #shift ", %%mm4       \n\t"\
        "packssdw %%mm1, %%mm1          \n\t" /* A2+B2  a2+b2 */\
        "packssdw %%mm6, %%mm6          \n\t" /* A3+B3  a3+b3 */\
        "movd %%mm1, 32+" #dst "        \n\t"\
        "packssdw %%mm4, %%mm4          \n\t" /* A3-B3  a3-b3 */\
        "packssdw %%mm5, %%mm5          \n\t" /* A2-B2  a2-b2 */\
        "movd %%mm6, 48+" #dst "        \n\t"\
        "movd %%mm4, 64+" #dst "        \n\t"\
        "movd %%mm5, 80+" #dst "        \n\t"


//IDCT(  src0,   src4,   src1,    src5,    dst, shift)
IDCT(    (%1), 64(%1), 32(%1),  96(%1),  0(%0), 20)
IDCT(   8(%1), 72(%1), 40(%1), 104(%1),  4(%0), 20)
IDCT(  16(%1), 80(%1), 48(%1), 112(%1),  8(%0), 20)
IDCT(  24(%1), 88(%1), 56(%1), 120(%1), 12(%0), 20)
        "jmp 9f                         \n\t"

        "# .p2align 4                   \n\t"\
        "5:                             \n\t"
#undef IDCT
#define IDCT(src0, src4, src1, src5, dst, shift) \
        "movq " #src0 ", %%mm0          \n\t" /* R4     R0      r4      r0 */\
        "movq " #src4 ", %%mm1          \n\t" /* R6     R2      r6      r2 */\
        "movq 16(%2), %%mm4             \n\t" /* C4     C4      C4      C4 */\
        "pmaddwd %%mm0, %%mm4           \n\t" /* C4R4+C4R0      C4r4+C4r0 */\
        "movq 24(%2), %%mm5             \n\t" /* -C4    C4      -C4     C4 */\
        "pmaddwd %%mm5, %%mm0           \n\t" /* -C4R4+C4R0     -C4r4+C4r0 */\
        "movq 32(%2), %%mm5             \n\t" /* C6     C2      C6      C2 */\
        "pmaddwd %%mm1, %%mm5           \n\t" /* C6R6+C2R2      C6r6+C2r2 */\
        "movq 40(%2), %%mm6             \n\t" /* -C2    C6      -C2     C6 */\
        "pmaddwd %%mm6, %%mm1           \n\t" /* -C2R6+C6R2     -C2r6+C6r2 */\
        "movq %%mm4, %%mm6              \n\t" /* C4R4+C4R0      C4r4+C4r0 */\
        "paddd %%mm5, %%mm4             \n\t" /* A0             a0 */\
        "psubd %%mm5, %%mm6             \n\t" /* A3             a3 */\
        "movq %%mm0, %%mm5              \n\t" /* -C4R4+C4R0     -C4r4+C4r0 */\
        "paddd %%mm1, %%mm0             \n\t" /* A1             a1 */\
        "psubd %%mm1, %%mm5             \n\t" /* A2             a2 */\
        "movq 8+" #src0 ", %%mm2        \n\t" /* R4     R0      r4      r0 */\
        "movq 8+" #src4 ", %%mm3        \n\t" /* R6     R2      r6      r2 */\
        "movq 16(%2), %%mm1             \n\t" /* C4     C4      C4      C4 */\
        "pmaddwd %%mm2, %%mm1           \n\t" /* C4R4+C4R0      C4r4+C4r0 */\
        "movq 24(%2), %%mm7             \n\t" /* -C4    C4      -C4     C4 */\
        "pmaddwd %%mm7, %%mm2           \n\t" /* -C4R4+C4R0     -C4r4+C4r0 */\
        "movq 32(%2), %%mm7             \n\t" /* C6     C2      C6      C2 */\
        "pmaddwd %%mm3, %%mm7           \n\t" /* C6R6+C2R2      C6r6+C2r2 */\
        "pmaddwd 40(%2), %%mm3          \n\t" /* -C2R6+C6R2     -C2r6+C6r2 */\
        "paddd %%mm1, %%mm7             \n\t" /* A0             a0 */\
        "paddd %%mm1, %%mm1             \n\t" /* 2C0            2c0 */\
        "psubd %%mm7, %%mm1             \n\t" /* A3             a3 */\
        "paddd %%mm2, %%mm3             \n\t" /* A1             a1 */\
        "paddd %%mm2, %%mm2             \n\t" /* 2C1            2c1 */\
        "psubd %%mm3, %%mm2             \n\t" /* A2             a2 */\
        "psrad $" #shift ", %%mm4       \n\t"\
        "psrad $" #shift ", %%mm7       \n\t"\
        "psrad $" #shift ", %%mm3       \n\t"\
        "packssdw %%mm7, %%mm4          \n\t" /* A0     a0 */\
        "movq %%mm4, " #dst "           \n\t"\
        "psrad $" #shift ", %%mm0       \n\t"\
        "packssdw %%mm3, %%mm0          \n\t" /* A1     a1 */\
        "movq %%mm0, 16+" #dst "        \n\t"\
        "movq %%mm0, 96+" #dst "        \n\t"\
        "movq %%mm4, 112+" #dst "       \n\t"\
        "psrad $" #shift ", %%mm5       \n\t"\
        "psrad $" #shift ", %%mm6       \n\t"\
        "psrad $" #shift ", %%mm2       \n\t"\
        "packssdw %%mm2, %%mm5          \n\t" /* A2-B2  a2-b2 */\
        "movq %%mm5, 32+" #dst "        \n\t"\
        "psrad $" #shift ", %%mm1       \n\t"\
        "packssdw %%mm1, %%mm6          \n\t" /* A3+B3  a3+b3 */\
        "movq %%mm6, 48+" #dst "        \n\t"\
        "movq %%mm6, 64+" #dst "        \n\t"\
        "movq %%mm5, 80+" #dst "        \n\t"


//IDCT(  src0,   src4,   src1,    src5,    dst, shift)
IDCT(    0(%1), 64(%1), 32(%1),  96(%1),  0(%0), 20)
//IDCT(   8(%1), 72(%1), 40(%1), 104(%1),  4(%0), 20)
IDCT(  16(%1), 80(%1), 48(%1), 112(%1),  8(%0), 20)
//IDCT(  24(%1), 88(%1), 56(%1), 120(%1), 12(%0), 20)
        "jmp 9f                         \n\t"


        "# .p2align 4                   \n\t"\
        "1:                             \n\t"
#undef IDCT
#define IDCT(src0, src4, src1, src5, dst, shift) \
        "movq " #src0 ", %%mm0          \n\t" /* R4     R0      r4      r0 */\
        "movq " #src4 ", %%mm1          \n\t" /* R6     R2      r6      r2 */\
        "movq " #src1 ", %%mm2          \n\t" /* R3     R1      r3      r1 */\
        "movq 16(%2), %%mm4             \n\t" /* C4     C4      C4      C4 */\
        "pmaddwd %%mm0, %%mm4           \n\t" /* C4R4+C4R0      C4r4+C4r0 */\
        "movq 24(%2), %%mm5             \n\t" /* -C4    C4      -C4     C4 */\
        "pmaddwd %%mm5, %%mm0           \n\t" /* -C4R4+C4R0     -C4r4+C4r0 */\
        "movq 32(%2), %%mm5             \n\t" /* C6     C2      C6      C2 */\
        "pmaddwd %%mm1, %%mm5           \n\t" /* C6R6+C2R2      C6r6+C2r2 */\
        "movq 40(%2), %%mm6             \n\t" /* -C2    C6      -C2     C6 */\
        "pmaddwd %%mm6, %%mm1           \n\t" /* -C2R6+C6R2     -C2r6+C6r2 */\
        "movq %%mm4, %%mm6              \n\t" /* C4R4+C4R0      C4r4+C4r0 */\
        "movq 48(%2), %%mm7             \n\t" /* C3     C1      C3      C1 */\
        "pmaddwd %%mm2, %%mm7           \n\t" /* C3R3+C1R1      C3r3+C1r1 */\
        "paddd %%mm5, %%mm4             \n\t" /* A0             a0 */\
        "psubd %%mm5, %%mm6             \n\t" /* A3             a3 */\
        "movq %%mm0, %%mm5              \n\t" /* -C4R4+C4R0     -C4r4+C4r0 */\
        "paddd %%mm1, %%mm0             \n\t" /* A1             a1 */\
        "psubd %%mm1, %%mm5             \n\t" /* A2             a2 */\
        "movq 64(%2), %%mm1             \n\t"\
        "pmaddwd %%mm2, %%mm1           \n\t" /* -C7R3+C3R1     -C7r3+C3r1 */\
        "paddd %%mm4, %%mm7             \n\t" /* A0+B0          a0+b0 */\
        "paddd %%mm4, %%mm4             \n\t" /* 2A0            2a0 */\
        "psubd %%mm7, %%mm4             \n\t" /* A0-B0          a0-b0 */\
        "psrad $" #shift ", %%mm7       \n\t"\
        "psrad $" #shift ", %%mm4       \n\t"\
        "movq %%mm0, %%mm3              \n\t" /* A1             a1 */\
        "paddd %%mm1, %%mm0             \n\t" /* A1+B1          a1+b1 */\
        "psubd %%mm1, %%mm3             \n\t" /* A1-B1          a1-b1 */\
        "psrad $" #shift ", %%mm0       \n\t"\
        "psrad $" #shift ", %%mm3       \n\t"\
        "packssdw %%mm7, %%mm7          \n\t" /* A0+B0  a0+b0 */\
        "movd %%mm7, " #dst "           \n\t"\
        "packssdw %%mm0, %%mm0          \n\t" /* A1+B1  a1+b1 */\
        "movd %%mm0, 16+" #dst "        \n\t"\
        "packssdw %%mm3, %%mm3          \n\t" /* A1-B1  a1-b1 */\
        "movd %%mm3, 96+" #dst "        \n\t"\
        "packssdw %%mm4, %%mm4          \n\t" /* A0-B0  a0-b0 */\
        "movd %%mm4, 112+" #dst "       \n\t"\
        "movq 80(%2), %%mm4             \n\t" /* -C1    C5      -C1     C5 */\
        "pmaddwd %%mm2, %%mm4           \n\t" /* -C1R3+C5R1     -C1r3+C5r1 */\
        "pmaddwd 96(%2), %%mm2          \n\t" /* -C5R3+C7R1     -C5r3+C7r1 */\
        "movq %%mm5, %%mm3              \n\t" /* A2             a2 */\
        "paddd %%mm4, %%mm3             \n\t" /* A2+B2          a2+b2 */\
        "psubd %%mm4, %%mm5             \n\t" /* a2-B2          a2-b2 */\
        "psrad $" #shift ", %%mm3       \n\t"\
        "psrad $" #shift ", %%mm5       \n\t"\
        "movq %%mm6, %%mm4              \n\t" /* A3             a3 */\
        "paddd %%mm2, %%mm6             \n\t" /* A3+B3          a3+b3 */\
        "psubd %%mm2, %%mm4             \n\t" /* a3-B3          a3-b3 */\
        "psrad $" #shift ", %%mm6       \n\t"\
        "packssdw %%mm3, %%mm3          \n\t" /* A2+B2  a2+b2 */\
        "movd %%mm3, 32+" #dst "        \n\t"\
        "psrad $" #shift ", %%mm4       \n\t"\
        "packssdw %%mm6, %%mm6          \n\t" /* A3+B3  a3+b3 */\
        "movd %%mm6, 48+" #dst "        \n\t"\
        "packssdw %%mm4, %%mm4          \n\t" /* A3-B3  a3-b3 */\
        "packssdw %%mm5, %%mm5          \n\t" /* A2-B2  a2-b2 */\
        "movd %%mm4, 64+" #dst "        \n\t"\
        "movd %%mm5, 80+" #dst "        \n\t"


//IDCT(  src0,   src4,   src1,    src5,    dst, shift)
IDCT(    (%1), 64(%1), 32(%1),  96(%1),  0(%0), 20)
IDCT(   8(%1), 72(%1), 40(%1), 104(%1),  4(%0), 20)
IDCT(  16(%1), 80(%1), 48(%1), 112(%1),  8(%0), 20)
IDCT(  24(%1), 88(%1), 56(%1), 120(%1), 12(%0), 20)
        "jmp 9f                         \n\t"


        "# .p2align 4                   \n\t"
        "7:                             \n\t"
#undef IDCT
#define IDCT(src0, src4, src1, src5, dst, shift) \
        "movq " #src0 ", %%mm0          \n\t" /* R4     R0      r4      r0 */\
        "movq 16(%2), %%mm4             \n\t" /* C4     C4      C4      C4 */\
        "pmaddwd %%mm0, %%mm4           \n\t" /* C4R4+C4R0      C4r4+C4r0 */\
        "movq 24(%2), %%mm5             \n\t" /* -C4    C4      -C4     C4 */\
        "pmaddwd %%mm5, %%mm0           \n\t" /* -C4R4+C4R0     -C4r4+C4r0 */\
        "psrad $" #shift ", %%mm4       \n\t"\
        "psrad $" #shift ", %%mm0       \n\t"\
        "movq 8+" #src0 ", %%mm2        \n\t" /* R4     R0      r4      r0 */\
        "movq 16(%2), %%mm1             \n\t" /* C4     C4      C4      C4 */\
        "pmaddwd %%mm2, %%mm1           \n\t" /* C4R4+C4R0      C4r4+C4r0 */\
        "movq 24(%2), %%mm7             \n\t" /* -C4    C4      -C4     C4 */\
        "pmaddwd %%mm7, %%mm2           \n\t" /* -C4R4+C4R0     -C4r4+C4r0 */\
        "movq 32(%2), %%mm7             \n\t" /* C6     C2      C6      C2 */\
        "psrad $" #shift ", %%mm1       \n\t"\
        "packssdw %%mm1, %%mm4          \n\t" /* A0     a0 */\
        "movq %%mm4, " #dst "           \n\t"\
        "psrad $" #shift ", %%mm2       \n\t"\
        "packssdw %%mm2, %%mm0          \n\t" /* A1     a1 */\
        "movq %%mm0, 16+" #dst "        \n\t"\
        "movq %%mm0, 96+" #dst "        \n\t"\
        "movq %%mm4, 112+" #dst "       \n\t"\
        "movq %%mm0, 32+" #dst "        \n\t"\
        "movq %%mm4, 48+" #dst "        \n\t"\
        "movq %%mm4, 64+" #dst "        \n\t"\
        "movq %%mm0, 80+" #dst "        \n\t"

//IDCT(  src0,   src4,   src1,    src5,    dst, shift)
IDCT(   0(%1), 64(%1), 32(%1),  96(%1),  0(%0), 20)
//IDCT(   8(%1), 72(%1), 40(%1), 104(%1),  4(%0), 20)
IDCT(  16(%1), 80(%1), 48(%1), 112(%1),  8(%0), 20)
//IDCT(  24(%1), 88(%1), 56(%1), 120(%1), 12(%0), 20)


#endif

/*
Input
 00 40 04 44 20 60 24 64
 10 30 14 34 50 70 54 74
 01 41 03 43 21 61 23 63
 11 31 13 33 51 71 53 73
 02 42 06 46 22 62 26 66
 12 32 16 36 52 72 56 76
 05 45 07 47 25 65 27 67
 15 35 17 37 55 75 57 77

Temp
 00 04 10 14 20 24 30 34
 40 44 50 54 60 64 70 74
 01 03 11 13 21 23 31 33
 41 43 51 53 61 63 71 73
 02 06 12 16 22 26 32 36
 42 46 52 56 62 66 72 76
 05 07 15 17 25 27 35 37
 45 47 55 57 65 67 75 77
*/

"9: \n\t"
                :: "r" (block), "r" (temp), "r" (coeffs)
                : "%eax"
        );
}

void ff_simple_idct_mmx(int16_t *block)
{
    idct(block);
}

//FIXME merge add/put into the idct

void ff_simple_idct_put_mmx(uint8_t *dest, int line_size, int16_t *block)
{
    idct(block);
    ff_put_pixels_clamped_mmx(block, dest, line_size);
}
void ff_simple_idct_add_mmx(uint8_t *dest, int line_size, int16_t *block)
{
    idct(block);
    ff_add_pixels_clamped_mmx(block, dest, line_size);
}

#endif /* HAVE_INLINE_ASM */
