/*
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (C) 2005 Nikolaj Poroshin <porosh3@psu.ru>
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

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/mem.h"
#include "libavutil/x86/asm.h"
#include "libavfilter/vf_fspp.h"

#if HAVE_MMX_INLINE
DECLARE_ALIGNED(32, static const uint8_t, dither)[8][8] = {
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
static void store_slice_mmx(uint8_t *dst, int16_t *src,
                            ptrdiff_t dst_stride, ptrdiff_t src_stride,
                            ptrdiff_t width, ptrdiff_t height, ptrdiff_t log2_scale)
{
    const uint8_t *od = &dither[0][0];
    const uint8_t *end = &dither[height][0];
    width = (width + 7) & ~7;
    dst_stride -= width;

    __asm__ volatile(
        "mov %5 , %%"REG_d"                \n\t"
        "mov %6 , %%"REG_S"                \n\t"
        "mov %7 , %%"REG_D"                \n\t"
        "mov %1 , %%"REG_a"                \n\t"
        "movd %%"REG_d" , %%mm5            \n\t"
        "xor $-1 , %%"REG_d"               \n\t"
        "mov %%"REG_a" , %%"REG_c"         \n\t"
        "add $7 , %%"REG_d"                \n\t"
        "neg %%"REG_a"                     \n\t"
        "sub %0 , %%"REG_c"                \n\t"
        "add %%"REG_c" , %%"REG_c"         \n\t"
        "movd %%"REG_d" , %%mm2            \n\t"
        "mov %%"REG_c" , %1                \n\t"
        "mov %2 , %%"REG_d"                \n\t"
        "shl $4 , %%"REG_a"                \n\t"

        "2:                                \n\t"
        "movq (%%"REG_d") , %%mm3          \n\t"
        "movq %%mm3 , %%mm4                \n\t"
        "pxor %%mm7 , %%mm7                \n\t"
        "punpcklbw %%mm7 , %%mm3           \n\t"
        "punpckhbw %%mm7 , %%mm4           \n\t"
        "mov %0 , %%"REG_c"                \n\t"
        "psraw %%mm5 , %%mm3               \n\t"
        "psraw %%mm5 , %%mm4               \n\t"
        "1:                                \n\t"
        "movq %%mm7, (%%"REG_S",%%"REG_a") \n\t"
        "movq (%%"REG_S") , %%mm0          \n\t"
        "movq 8(%%"REG_S"), %%mm1          \n\t"

        "movq %%mm7, 8(%%"REG_S",%%"REG_a")\n\t"
        "paddw %%mm3, %%mm0                \n\t"
        "paddw %%mm4, %%mm1                \n\t"

        "movq %%mm7, (%%"REG_S")           \n\t"
        "psraw %%mm2, %%mm0                \n\t"
        "psraw %%mm2, %%mm1                \n\t"

        "movq %%mm7, 8(%%"REG_S")          \n\t"
        "packuswb %%mm1, %%mm0             \n\t"
        "add $16, %%"REG_S"                \n\t"

        "movq %%mm0, (%%"REG_D")           \n\t"
        "add $8, %%"REG_D"                 \n\t"
        "sub $8, %%"REG_c"                 \n\t"
        "jg 1b                             \n\t"
        "add %1, %%"REG_S"                 \n\t"
        "add $8, %%"REG_d"                 \n\t"
        "add %3, %%"REG_D"                 \n\t"
        "cmp %4, %%"REG_d"                 \n\t"
        "jl 2b                             \n\t"

        :
        : "m" (width),      "m" (src_stride), "erm" (od), "m" (dst_stride), "erm" (end),
          "m" (log2_scale), "m" (src),        "m" (dst)                                     //input
        : "%"REG_a, "%"REG_c, "%"REG_d, "%"REG_S, "%"REG_D
        );
}

//This func reads from 2 slices, 0 & 2  and clears 2-nd
static void store_slice2_mmx(uint8_t *dst, int16_t *src,
                             ptrdiff_t dst_stride, ptrdiff_t src_stride,
                             ptrdiff_t width, ptrdiff_t height, ptrdiff_t log2_scale)
{
    const uint8_t *od = &dither[0][0];
    const uint8_t *end = &dither[height][0];
    width = (width + 7) & ~7;
    dst_stride -= width;

    __asm__ volatile(
        "mov %5, %%"REG_d"                \n\t"
        "mov %6, %%"REG_S"                \n\t"
        "mov %7, %%"REG_D"                \n\t"
        "mov %1, %%"REG_a"                \n\t"
        "movd %%"REG_d", %%mm5            \n\t"
        "xor $-1, %%"REG_d"               \n\t"
        "mov %%"REG_a", %%"REG_c"         \n\t"
        "add $7, %%"REG_d"                \n\t"
        "sub %0, %%"REG_c"                \n\t"
        "add %%"REG_c", %%"REG_c"         \n\t"
        "movd %%"REG_d", %%mm2            \n\t"
        "mov %%"REG_c", %1                \n\t"
        "mov %2, %%"REG_d"                \n\t"
        "shl $5, %%"REG_a"                \n\t"

        "2:                               \n\t"
        "movq (%%"REG_d"), %%mm3          \n\t"
        "movq %%mm3, %%mm4                \n\t"
        "pxor %%mm7, %%mm7                \n\t"
        "punpcklbw %%mm7, %%mm3           \n\t"
        "punpckhbw %%mm7, %%mm4           \n\t"
        "mov %0, %%"REG_c"                \n\t"
        "psraw %%mm5, %%mm3               \n\t"
        "psraw %%mm5, %%mm4               \n\t"
        "1:                               \n\t"
        "movq (%%"REG_S"), %%mm0          \n\t"
        "movq 8(%%"REG_S"), %%mm1         \n\t"
        "paddw %%mm3, %%mm0               \n\t"

        "paddw (%%"REG_S",%%"REG_a"),%%mm0\n\t"
        "paddw %%mm4, %%mm1               \n\t"
        "movq 8(%%"REG_S",%%"REG_a"),%%mm6\n\t"

        "movq %%mm7, (%%"REG_S",%%"REG_a")\n\t"
        "psraw %%mm2, %%mm0               \n\t"
        "paddw %%mm6, %%mm1               \n\t"

        "movq %%mm7,8(%%"REG_S",%%"REG_a")\n\t"
        "psraw %%mm2, %%mm1               \n\t"
        "packuswb %%mm1, %%mm0            \n\t"

        "movq %%mm0, (%%"REG_D")          \n\t"
        "add $16, %%"REG_S"               \n\t"
        "add $8, %%"REG_D"                \n\t"
        "sub $8, %%"REG_c"                \n\t"
        "jg 1b                            \n\t"
        "add %1, %%"REG_S"                \n\t"
        "add $8, %%"REG_d"                \n\t"
        "add %3, %%"REG_D"                \n\t"
        "cmp %4, %%"REG_d"                \n\t"
        "jl 2b                            \n\t"

        :
        : "m" (width),      "m" (src_stride), "erm" (od), "m" (dst_stride), "erm" (end),
          "m" (log2_scale), "m" (src),        "m" (dst)                                     //input
        : "%"REG_a, "%"REG_c, "%"REG_d, "%"REG_D, "%"REG_S
        );
}

static void mul_thrmat_mmx(FSPPContext *p, int q)
{
    uint64_t *adr = &p->threshold_mtx_noq[0];

    __asm__ volatile(
        "movd %0, %%mm7                   \n\t"
        "add $8*8*2, %%"REG_D"            \n\t"
        "movq 0*8(%%"REG_S"), %%mm0       \n\t"
        "punpcklwd %%mm7, %%mm7           \n\t"
        "movq 1*8(%%"REG_S"), %%mm1       \n\t"
        "punpckldq %%mm7, %%mm7           \n\t"
        "pmullw %%mm7, %%mm0              \n\t"

        "movq 2*8(%%"REG_S"), %%mm2       \n\t"
        "pmullw %%mm7, %%mm1              \n\t"

        "movq 3*8(%%"REG_S"), %%mm3       \n\t"
        "pmullw %%mm7, %%mm2              \n\t"

        "movq %%mm0, 0*8(%%"REG_D")       \n\t"
        "movq 4*8(%%"REG_S"), %%mm4       \n\t"
        "pmullw %%mm7, %%mm3              \n\t"

        "movq %%mm1, 1*8(%%"REG_D")       \n\t"
        "movq 5*8(%%"REG_S"), %%mm5       \n\t"
        "pmullw %%mm7, %%mm4              \n\t"

        "movq %%mm2, 2*8(%%"REG_D")       \n\t"
        "movq 6*8(%%"REG_S"), %%mm6       \n\t"
        "pmullw %%mm7, %%mm5              \n\t"

        "movq %%mm3, 3*8(%%"REG_D")       \n\t"
        "movq 7*8+0*8(%%"REG_S"), %%mm0   \n\t"
        "pmullw %%mm7, %%mm6              \n\t"

        "movq %%mm4, 4*8(%%"REG_D")       \n\t"
        "movq 7*8+1*8(%%"REG_S"), %%mm1   \n\t"
        "pmullw %%mm7, %%mm0              \n\t"

        "movq %%mm5, 5*8(%%"REG_D")       \n\t"
        "movq 7*8+2*8(%%"REG_S"), %%mm2   \n\t"
        "pmullw %%mm7, %%mm1              \n\t"

        "movq %%mm6, 6*8(%%"REG_D")       \n\t"
        "movq 7*8+3*8(%%"REG_S"), %%mm3   \n\t"
        "pmullw %%mm7, %%mm2              \n\t"

        "movq %%mm0, 7*8+0*8(%%"REG_D")   \n\t"
        "movq 7*8+4*8(%%"REG_S"), %%mm4   \n\t"
        "pmullw %%mm7, %%mm3              \n\t"

        "movq %%mm1, 7*8+1*8(%%"REG_D")   \n\t"
        "movq 7*8+5*8(%%"REG_S"), %%mm5   \n\t"
        "pmullw %%mm7, %%mm4              \n\t"

        "movq %%mm2, 7*8+2*8(%%"REG_D")   \n\t"
        "movq 7*8+6*8(%%"REG_S"), %%mm6   \n\t"
        "pmullw %%mm7, %%mm5              \n\t"

        "movq %%mm3, 7*8+3*8(%%"REG_D")   \n\t"
        "movq 14*8+0*8(%%"REG_S"), %%mm0  \n\t"
        "pmullw %%mm7, %%mm6              \n\t"

        "movq %%mm4, 7*8+4*8(%%"REG_D")   \n\t"
        "movq 14*8+1*8(%%"REG_S"), %%mm1  \n\t"
        "pmullw %%mm7, %%mm0              \n\t"

        "movq %%mm5, 7*8+5*8(%%"REG_D")   \n\t"
        "pmullw %%mm7, %%mm1              \n\t"

        "movq %%mm6, 7*8+6*8(%%"REG_D")   \n\t"
        "movq %%mm0, 14*8+0*8(%%"REG_D")  \n\t"
        "movq %%mm1, 14*8+1*8(%%"REG_D")  \n\t"

        : "+g" (q), "+S" (adr), "+D" (adr)
        :
        );
}

DECLARE_ASM_CONST(8, uint64_t, MM_FIX_0_382683433)   = FIX64(0.382683433, 14);
DECLARE_ALIGNED  (8, uint64_t, ff_MM_FIX_0_541196100)= FIX64(0.541196100, 14);
DECLARE_ALIGNED  (8, uint64_t, ff_MM_FIX_0_707106781)= FIX64(0.707106781, 14);
DECLARE_ASM_CONST(8, uint64_t, MM_FIX_1_306562965)   = FIX64(1.306562965, 14);

DECLARE_ASM_CONST(8, uint64_t, MM_FIX_1_414213562_A) = FIX64(1.414213562, 14);

DECLARE_ASM_CONST(8, uint64_t, MM_FIX_1_847759065)   = FIX64(1.847759065, 13);
DECLARE_ASM_CONST(8, uint64_t, MM_FIX_2_613125930)   = FIX64(-2.613125930, 13);
DECLARE_ASM_CONST(8, uint64_t, MM_FIX_1_414213562)   = FIX64(1.414213562, 13);
DECLARE_ASM_CONST(8, uint64_t, MM_FIX_1_082392200)   = FIX64(1.082392200, 13);
//for t3,t5,t7 == 0 shortcut
DECLARE_ASM_CONST(8, uint64_t, MM_FIX_0_847759065)   = FIX64(0.847759065, 14);
DECLARE_ASM_CONST(8, uint64_t, MM_FIX_0_566454497)   = FIX64(0.566454497, 14);
DECLARE_ASM_CONST(8, uint64_t, MM_FIX_0_198912367)   = FIX64(0.198912367, 14);

DECLARE_ASM_CONST(8, uint64_t, MM_DESCALE_RND)       = C64(4);
DECLARE_ASM_CONST(8, uint64_t, MM_2)                 = C64(2);

static void column_fidct_mmx(int16_t *thr_adr, int16_t *data, int16_t *output, int cnt)
{
    DECLARE_ALIGNED(8, uint64_t, temps)[4];

    __asm__ volatile(

        "1:                                       \n\t"
        "movq "DCTSIZE_S"*0*2(%%"REG_S"), %%mm1   \n\t"
        //
        "movq "DCTSIZE_S"*3*2(%%"REG_S"), %%mm7   \n\t"
        "movq %%mm1, %%mm0                        \n\t"

        "paddw "DCTSIZE_S"*7*2(%%"REG_S"), %%mm1  \n\t" //t0
        "movq %%mm7, %%mm3                        \n\t"

        "paddw "DCTSIZE_S"*4*2(%%"REG_S"), %%mm7  \n\t" //t3
        "movq %%mm1, %%mm5             \n\t"

        "movq "DCTSIZE_S"*1*2(%%"REG_S"), %%mm6   \n\t"
        "psubw %%mm7, %%mm1                       \n\t" //t13

        "movq "DCTSIZE_S"*2*2(%%"REG_S"), %%mm2   \n\t"
        "movq %%mm6, %%mm4                        \n\t"

        "paddw "DCTSIZE_S"*6*2(%%"REG_S"), %%mm6  \n\t" //t1
        "paddw %%mm7, %%mm5                       \n\t" //t10

        "paddw "DCTSIZE_S"*5*2(%%"REG_S"), %%mm2  \n\t" //t2
        "movq %%mm6, %%mm7                        \n\t"

        "paddw %%mm2, %%mm6                       \n\t" //t11
        "psubw %%mm2, %%mm7                       \n\t" //t12

        "movq %%mm5, %%mm2                        \n\t"
        "paddw %%mm6, %%mm5                       \n\t" //d0
        // i0 t13 t12 i3 i1 d0 - d4
        "psubw %%mm6, %%mm2                       \n\t" //d4
        "paddw %%mm1, %%mm7                       \n\t"

        "movq  4*16(%%"REG_d"), %%mm6             \n\t"
        "psllw $2, %%mm7                          \n\t"

        "psubw 0*16(%%"REG_d"), %%mm5             \n\t"
        "psubw %%mm6, %%mm2                       \n\t"

        "paddusw 0*16(%%"REG_d"), %%mm5           \n\t"
        "paddusw %%mm6, %%mm2                     \n\t"

        "pmulhw "MANGLE(ff_MM_FIX_0_707106781)", %%mm7 \n\t"
        //
        "paddw 0*16(%%"REG_d"), %%mm5             \n\t"
        "paddw %%mm6, %%mm2                       \n\t"

        "psubusw 0*16(%%"REG_d"), %%mm5           \n\t"
        "psubusw %%mm6, %%mm2                     \n\t"

//This func is totally compute-bound,  operates at huge speed. So,  DC shortcut
// at this place isn't worthwhile due to BTB miss penalty (checked on Pent. 3).
//However,  typical numbers: nondc - 29%%,  dc - 46%%,  zero - 25%%. All <> 0 case is very rare.
        "paddw "MANGLE(MM_2)", %%mm5              \n\t"
        "movq %%mm2, %%mm6                        \n\t"

        "paddw %%mm5, %%mm2                       \n\t"
        "psubw %%mm6, %%mm5                       \n\t"

        "movq %%mm1, %%mm6                        \n\t"
        "paddw %%mm7, %%mm1                       \n\t" //d2

        "psubw 2*16(%%"REG_d"), %%mm1             \n\t"
        "psubw %%mm7, %%mm6                       \n\t" //d6

        "movq 6*16(%%"REG_d"), %%mm7              \n\t"
        "psraw $2, %%mm5                          \n\t"

        "paddusw 2*16(%%"REG_d"), %%mm1           \n\t"
        "psubw %%mm7, %%mm6                       \n\t"
        // t7 d2 /t11 t4 t6 - d6 /t10

        "paddw 2*16(%%"REG_d"), %%mm1             \n\t"
        "paddusw %%mm7, %%mm6                     \n\t"

        "psubusw 2*16(%%"REG_d"), %%mm1           \n\t"
        "paddw %%mm7, %%mm6                       \n\t"

        "psubw "DCTSIZE_S"*4*2(%%"REG_S"), %%mm3  \n\t"
        "psubusw %%mm7, %%mm6                     \n\t"

        //movq [edi+"DCTSIZE_S"*2*2], mm1
        //movq [edi+"DCTSIZE_S"*6*2], mm6
        "movq %%mm1, %%mm7                        \n\t"
        "psraw $2, %%mm2                          \n\t"

        "psubw "DCTSIZE_S"*6*2(%%"REG_S"), %%mm4  \n\t"
        "psubw %%mm6, %%mm1                       \n\t"

        "psubw "DCTSIZE_S"*7*2(%%"REG_S"), %%mm0  \n\t"
        "paddw %%mm7, %%mm6                       \n\t" //'t13

        "psraw $2, %%mm6                          \n\t" //paddw mm6, MM_2 !!    ---
        "movq %%mm2, %%mm7                        \n\t"

        "pmulhw "MANGLE(MM_FIX_1_414213562_A)", %%mm1 \n\t"
        "paddw %%mm6, %%mm2                       \n\t" //'t0

        "movq %%mm2, 0*8+%3                       \n\t" //!
        "psubw %%mm6, %%mm7                       \n\t" //'t3

        "movq "DCTSIZE_S"*2*2(%%"REG_S"), %%mm2   \n\t"
        "psubw %%mm6, %%mm1                       \n\t" //'t12

        "psubw "DCTSIZE_S"*5*2(%%"REG_S"), %%mm2  \n\t" //t5
        "movq %%mm5, %%mm6                        \n\t"

        "movq %%mm7, 3*8+%3                       \n\t"
        "paddw %%mm2, %%mm3                       \n\t" //t10

        "paddw %%mm4, %%mm2                       \n\t" //t11
        "paddw %%mm0, %%mm4                       \n\t" //t12

        "movq %%mm3, %%mm7                        \n\t"
        "psubw %%mm4, %%mm3                       \n\t"

        "psllw $2, %%mm3                          \n\t"
        "psllw $2, %%mm7                          \n\t" //opt for P6

        "pmulhw "MANGLE(MM_FIX_0_382683433)", %%mm3 \n\t"
        "psllw $2, %%mm4                          \n\t"

        "pmulhw "MANGLE(ff_MM_FIX_0_541196100)", %%mm7 \n\t"
        "psllw $2, %%mm2                          \n\t"

        "pmulhw "MANGLE(MM_FIX_1_306562965)", %%mm4 \n\t"
        "paddw %%mm1, %%mm5                       \n\t" //'t1

        "pmulhw "MANGLE(ff_MM_FIX_0_707106781)", %%mm2 \n\t"
        "psubw %%mm1, %%mm6                       \n\t" //'t2
        // t7 't12 't11 t4 t6 - 't13 't10   ---

        "paddw %%mm3, %%mm7                       \n\t" //z2

        "movq %%mm5, 1*8+%3                       \n\t"
        "paddw %%mm3, %%mm4                       \n\t" //z4

        "movq 3*16(%%"REG_d"), %%mm3              \n\t"
        "movq %%mm0, %%mm1                        \n\t"

        "movq %%mm6, 2*8+%3                       \n\t"
        "psubw %%mm2, %%mm1                       \n\t" //z13

//===
        "paddw %%mm2, %%mm0                       \n\t" //z11
        "movq %%mm1, %%mm5                        \n\t"

        "movq 5*16(%%"REG_d"), %%mm2              \n\t"
        "psubw %%mm7, %%mm1                       \n\t" //d3

        "paddw %%mm7, %%mm5                       \n\t" //d5
        "psubw %%mm3, %%mm1                       \n\t"

        "movq 1*16(%%"REG_d"), %%mm7              \n\t"
        "psubw %%mm2, %%mm5                       \n\t"

        "movq %%mm0, %%mm6                        \n\t"
        "paddw %%mm4, %%mm0                       \n\t" //d1

        "paddusw %%mm3, %%mm1                     \n\t"
        "psubw %%mm4, %%mm6                       \n\t" //d7

        // d1 d3 - - - d5 d7 -
        "movq 7*16(%%"REG_d"), %%mm4              \n\t"
        "psubw %%mm7, %%mm0                       \n\t"

        "psubw %%mm4, %%mm6                       \n\t"
        "paddusw %%mm2, %%mm5                     \n\t"

        "paddusw %%mm4, %%mm6                     \n\t"
        "paddw %%mm3, %%mm1                       \n\t"

        "paddw %%mm2, %%mm5                       \n\t"
        "paddw %%mm4, %%mm6                       \n\t"

        "psubusw %%mm3, %%mm1                     \n\t"
        "psubusw %%mm2, %%mm5                     \n\t"

        "psubusw %%mm4, %%mm6                     \n\t"
        "movq %%mm1, %%mm4                        \n\t"

        "por %%mm5, %%mm4                         \n\t"
        "paddusw %%mm7, %%mm0                     \n\t"

        "por %%mm6, %%mm4                         \n\t"
        "paddw %%mm7, %%mm0                       \n\t"

        "packssdw %%mm4, %%mm4                    \n\t"
        "psubusw %%mm7, %%mm0                     \n\t"

        "movd %%mm4, %%"REG_a"                    \n\t"
        "or %%"REG_a", %%"REG_a"                  \n\t"
        "jnz 2f                                   \n\t"
        //movq [edi+"DCTSIZE_S"*3*2], mm1
        //movq [edi+"DCTSIZE_S"*5*2], mm5
        //movq [edi+"DCTSIZE_S"*1*2], mm0
        //movq [edi+"DCTSIZE_S"*7*2], mm6
        // t4 t5 - - - t6 t7 -
        //--- t4 (mm0) may be <>0; mm1, mm5, mm6 == 0
//Typical numbers: nondc - 19%%,  dc - 26%%,  zero - 55%%. zero case alone isn't worthwhile
        "movq 0*8+%3, %%mm4                      \n\t"
        "movq %%mm0, %%mm1                       \n\t"

        "pmulhw "MANGLE(MM_FIX_0_847759065)", %%mm0 \n\t" //tmp6
        "movq %%mm1, %%mm2                       \n\t"

        "movq "DCTSIZE_S"*0*2(%%"REG_D"), %%mm5 \n\t"
        "movq %%mm2, %%mm3                      \n\t"

        "pmulhw "MANGLE(MM_FIX_0_566454497)", %%mm1 \n\t" //tmp5
        "paddw %%mm4, %%mm5                     \n\t"

        "movq 1*8+%3, %%mm6                     \n\t"
        //paddw mm3, MM_2
        "psraw $2, %%mm3                        \n\t" //tmp7

        "pmulhw "MANGLE(MM_FIX_0_198912367)", %%mm2 \n\t" //-tmp4
        "psubw %%mm3, %%mm4                     \n\t"

        "movq "DCTSIZE_S"*1*2(%%"REG_D"), %%mm7 \n\t"
        "paddw %%mm3, %%mm5                     \n\t"

        "movq %%mm4, "DCTSIZE_S"*7*2(%%"REG_D") \n\t"
        "paddw %%mm6, %%mm7                     \n\t"

        "movq 2*8+%3, %%mm3                     \n\t"
        "psubw %%mm0, %%mm6                     \n\t"

        "movq "DCTSIZE_S"*2*2(%%"REG_D"), %%mm4 \n\t"
        "paddw %%mm0, %%mm7                     \n\t"

        "movq %%mm5, "DCTSIZE_S"*0*2(%%"REG_D") \n\t"
        "paddw %%mm3, %%mm4                     \n\t"

        "movq %%mm6, "DCTSIZE_S"*6*2(%%"REG_D") \n\t"
        "psubw %%mm1, %%mm3                     \n\t"

        "movq "DCTSIZE_S"*5*2(%%"REG_D"), %%mm5 \n\t"
        "paddw %%mm1, %%mm4                     \n\t"

        "movq "DCTSIZE_S"*3*2(%%"REG_D"), %%mm6 \n\t"
        "paddw %%mm3, %%mm5                     \n\t"

        "movq 3*8+%3, %%mm0                     \n\t"
        "add $8, %%"REG_S"                      \n\t"

        "movq %%mm7, "DCTSIZE_S"*1*2(%%"REG_D") \n\t"
        "paddw %%mm0, %%mm6                     \n\t"

        "movq %%mm4, "DCTSIZE_S"*2*2(%%"REG_D") \n\t"
        "psubw %%mm2, %%mm0                     \n\t"

        "movq "DCTSIZE_S"*4*2(%%"REG_D"), %%mm7 \n\t"
        "paddw %%mm2, %%mm6                     \n\t"

        "movq %%mm5, "DCTSIZE_S"*5*2(%%"REG_D") \n\t"
        "paddw %%mm0, %%mm7                     \n\t"

        "movq %%mm6, "DCTSIZE_S"*3*2(%%"REG_D") \n\t"

        "movq %%mm7, "DCTSIZE_S"*4*2(%%"REG_D") \n\t"
        "add $8, %%"REG_D"                      \n\t"
        "jmp 4f                                 \n\t"

        "2:                                     \n\t"
        //--- non DC2
        //psraw mm1, 2 w/o it -> offset. thr1, thr1, thr1  (actually thr1, thr1, thr1-1)
        //psraw mm5, 2
        //psraw mm0, 2
        //psraw mm6, 2
        "movq %%mm5, %%mm3                      \n\t"
        "psubw %%mm1, %%mm5                     \n\t"

        "psllw $1, %%mm5                        \n\t" //'z10
        "paddw %%mm1, %%mm3                     \n\t" //'z13

        "movq %%mm0, %%mm2                      \n\t"
        "psubw %%mm6, %%mm0                     \n\t"

        "movq %%mm5, %%mm1                      \n\t"
        "psllw $1, %%mm0                        \n\t" //'z12

        "pmulhw "MANGLE(MM_FIX_2_613125930)", %%mm1 \n\t" //-
        "paddw %%mm0, %%mm5                     \n\t"

        "pmulhw "MANGLE(MM_FIX_1_847759065)", %%mm5 \n\t" //'z5
        "paddw %%mm6, %%mm2                     \n\t" //'z11

        "pmulhw "MANGLE(MM_FIX_1_082392200)", %%mm0 \n\t"
        "movq %%mm2, %%mm7                      \n\t"

        //---
        "movq 0*8+%3, %%mm4                     \n\t"
        "psubw %%mm3, %%mm2                     \n\t"

        "psllw $1, %%mm2                        \n\t"
        "paddw %%mm3, %%mm7                     \n\t" //'t7

        "pmulhw "MANGLE(MM_FIX_1_414213562)", %%mm2 \n\t" //'t11
        "movq %%mm4, %%mm6                      \n\t"
        //paddw mm7, MM_2
        "psraw $2, %%mm7                        \n\t"

        "paddw "DCTSIZE_S"*0*2(%%"REG_D"), %%mm4\n\t"
        "psubw %%mm7, %%mm6                     \n\t"

        "movq 1*8+%3, %%mm3                     \n\t"
        "paddw %%mm7, %%mm4                     \n\t"

        "movq %%mm6, "DCTSIZE_S"*7*2(%%"REG_D") \n\t"
        "paddw %%mm5, %%mm1                     \n\t" //'t12

        "movq %%mm4, "DCTSIZE_S"*0*2(%%"REG_D") \n\t"
        "psubw %%mm7, %%mm1                     \n\t" //'t6

        "movq 2*8+%3, %%mm7                     \n\t"
        "psubw %%mm5, %%mm0                     \n\t" //'t10

        "movq 3*8+%3, %%mm6                     \n\t"
        "movq %%mm3, %%mm5                      \n\t"

        "paddw "DCTSIZE_S"*1*2(%%"REG_D"), %%mm3\n\t"
        "psubw %%mm1, %%mm5                     \n\t"

        "psubw %%mm1, %%mm2                     \n\t" //'t5
        "paddw %%mm1, %%mm3                     \n\t"

        "movq %%mm5, "DCTSIZE_S"*6*2(%%"REG_D") \n\t"
        "movq %%mm7, %%mm4                      \n\t"

        "paddw "DCTSIZE_S"*2*2(%%"REG_D"), %%mm7\n\t"
        "psubw %%mm2, %%mm4                     \n\t"

        "paddw "DCTSIZE_S"*5*2(%%"REG_D"), %%mm4\n\t"
        "paddw %%mm2, %%mm7                     \n\t"

        "movq %%mm3, "DCTSIZE_S"*1*2(%%"REG_D") \n\t"
        "paddw %%mm2, %%mm0                     \n\t" //'t4

        // 't4 't6 't5 - - - - 't7
        "movq %%mm7, "DCTSIZE_S"*2*2(%%"REG_D") \n\t"
        "movq %%mm6, %%mm1                      \n\t"

        "paddw "DCTSIZE_S"*4*2(%%"REG_D"), %%mm6\n\t"
        "psubw %%mm0, %%mm1                     \n\t"

        "paddw "DCTSIZE_S"*3*2(%%"REG_D"), %%mm1\n\t"
        "paddw %%mm0, %%mm6                     \n\t"

        "movq %%mm4, "DCTSIZE_S"*5*2(%%"REG_D") \n\t"
        "add $8, %%"REG_S"                      \n\t"

        "movq %%mm6, "DCTSIZE_S"*4*2(%%"REG_D") \n\t"

        "movq %%mm1, "DCTSIZE_S"*3*2(%%"REG_D") \n\t"
        "add $8, %%"REG_D"                      \n\t"

        "4:                                     \n\t"
        "movq "DCTSIZE_S"*0*2(%%"REG_S"), %%mm1 \n\t"
        //
        "movq "DCTSIZE_S"*3*2(%%"REG_S"), %%mm7 \n\t"
        "movq %%mm1, %%mm0                      \n\t"

        "paddw "DCTSIZE_S"*7*2(%%"REG_S"), %%mm1\n\t" //t0
        "movq %%mm7, %%mm3                      \n\t"

        "paddw "DCTSIZE_S"*4*2(%%"REG_S"), %%mm7\n\t" //t3
        "movq %%mm1, %%mm5                      \n\t"

        "movq "DCTSIZE_S"*1*2(%%"REG_S"), %%mm6 \n\t"
        "psubw %%mm7, %%mm1                     \n\t" //t13

        "movq "DCTSIZE_S"*2*2(%%"REG_S"), %%mm2 \n\t"
        "movq %%mm6, %%mm4                      \n\t"

        "paddw "DCTSIZE_S"*6*2(%%"REG_S"), %%mm6\n\t" //t1
        "paddw %%mm7, %%mm5                     \n\t" //t10

        "paddw "DCTSIZE_S"*5*2(%%"REG_S"), %%mm2\n\t" //t2
        "movq %%mm6, %%mm7                      \n\t"

        "paddw %%mm2, %%mm6                     \n\t" //t11
        "psubw %%mm2, %%mm7                     \n\t" //t12

        "movq %%mm5, %%mm2                      \n\t"
        "paddw %%mm6, %%mm5                     \n\t" //d0
        // i0 t13 t12 i3 i1 d0 - d4
        "psubw %%mm6, %%mm2                     \n\t" //d4
        "paddw %%mm1, %%mm7                     \n\t"

        "movq  1*8+4*16(%%"REG_d"), %%mm6       \n\t"
        "psllw $2, %%mm7                        \n\t"

        "psubw 1*8+0*16(%%"REG_d"), %%mm5       \n\t"
        "psubw %%mm6, %%mm2                     \n\t"

        "paddusw 1*8+0*16(%%"REG_d"), %%mm5     \n\t"
        "paddusw %%mm6, %%mm2                   \n\t"

        "pmulhw "MANGLE(ff_MM_FIX_0_707106781)", %%mm7 \n\t"
        //
        "paddw 1*8+0*16(%%"REG_d"), %%mm5       \n\t"
        "paddw %%mm6, %%mm2                     \n\t"

        "psubusw 1*8+0*16(%%"REG_d"), %%mm5     \n\t"
        "psubusw %%mm6, %%mm2                   \n\t"

//This func is totally compute-bound,  operates at huge speed. So,  DC shortcut
// at this place isn't worthwhile due to BTB miss penalty (checked on Pent. 3).
//However,  typical numbers: nondc - 29%%,  dc - 46%%,  zero - 25%%. All <> 0 case is very rare.
        "paddw "MANGLE(MM_2)", %%mm5            \n\t"
        "movq %%mm2, %%mm6                      \n\t"

        "paddw %%mm5, %%mm2                     \n\t"
        "psubw %%mm6, %%mm5                     \n\t"

        "movq %%mm1, %%mm6                      \n\t"
        "paddw %%mm7, %%mm1                     \n\t" //d2

        "psubw 1*8+2*16(%%"REG_d"), %%mm1       \n\t"
        "psubw %%mm7, %%mm6                     \n\t" //d6

        "movq 1*8+6*16(%%"REG_d"), %%mm7        \n\t"
        "psraw $2, %%mm5                        \n\t"

        "paddusw 1*8+2*16(%%"REG_d"), %%mm1     \n\t"
        "psubw %%mm7, %%mm6                     \n\t"
        // t7 d2 /t11 t4 t6 - d6 /t10

        "paddw 1*8+2*16(%%"REG_d"), %%mm1       \n\t"
        "paddusw %%mm7, %%mm6                   \n\t"

        "psubusw 1*8+2*16(%%"REG_d"), %%mm1     \n\t"
        "paddw %%mm7, %%mm6                     \n\t"

        "psubw "DCTSIZE_S"*4*2(%%"REG_S"), %%mm3\n\t"
        "psubusw %%mm7, %%mm6                   \n\t"

        //movq [edi+"DCTSIZE_S"*2*2], mm1
        //movq [edi+"DCTSIZE_S"*6*2], mm6
        "movq %%mm1, %%mm7                      \n\t"
        "psraw $2, %%mm2                        \n\t"

        "psubw "DCTSIZE_S"*6*2(%%"REG_S"), %%mm4\n\t"
        "psubw %%mm6, %%mm1                     \n\t"

        "psubw "DCTSIZE_S"*7*2(%%"REG_S"), %%mm0\n\t"
        "paddw %%mm7, %%mm6                     \n\t" //'t13

        "psraw $2, %%mm6                        \n\t" //paddw mm6, MM_2 !!    ---
        "movq %%mm2, %%mm7                      \n\t"

        "pmulhw "MANGLE(MM_FIX_1_414213562_A)", %%mm1 \n\t"
        "paddw %%mm6, %%mm2                     \n\t" //'t0

        "movq %%mm2, 0*8+%3                     \n\t" //!
        "psubw %%mm6, %%mm7                     \n\t" //'t3

        "movq "DCTSIZE_S"*2*2(%%"REG_S"), %%mm2 \n\t"
        "psubw %%mm6, %%mm1                     \n\t" //'t12

        "psubw "DCTSIZE_S"*5*2(%%"REG_S"), %%mm2\n\t" //t5
        "movq %%mm5, %%mm6                      \n\t"

        "movq %%mm7, 3*8+%3                     \n\t"
        "paddw %%mm2, %%mm3                     \n\t" //t10

        "paddw %%mm4, %%mm2                     \n\t" //t11
        "paddw %%mm0, %%mm4                     \n\t" //t12

        "movq %%mm3, %%mm7                      \n\t"
        "psubw %%mm4, %%mm3                     \n\t"

        "psllw $2, %%mm3                        \n\t"
        "psllw $2, %%mm7                        \n\t" //opt for P6

        "pmulhw "MANGLE(MM_FIX_0_382683433)", %%mm3 \n\t"
        "psllw $2, %%mm4                        \n\t"

        "pmulhw "MANGLE(ff_MM_FIX_0_541196100)", %%mm7 \n\t"
        "psllw $2, %%mm2                        \n\t"

        "pmulhw "MANGLE(MM_FIX_1_306562965)", %%mm4 \n\t"
        "paddw %%mm1, %%mm5                     \n\t" //'t1

        "pmulhw "MANGLE(ff_MM_FIX_0_707106781)", %%mm2 \n\t"
        "psubw %%mm1, %%mm6                     \n\t" //'t2
        // t7 't12 't11 t4 t6 - 't13 't10   ---

        "paddw %%mm3, %%mm7                     \n\t" //z2

        "movq %%mm5, 1*8+%3                     \n\t"
        "paddw %%mm3, %%mm4                     \n\t" //z4

        "movq 1*8+3*16(%%"REG_d"), %%mm3        \n\t"
        "movq %%mm0, %%mm1                      \n\t"

        "movq %%mm6, 2*8+%3                     \n\t"
        "psubw %%mm2, %%mm1                     \n\t" //z13

//===
        "paddw %%mm2, %%mm0                     \n\t" //z11
        "movq %%mm1, %%mm5                      \n\t"

        "movq 1*8+5*16(%%"REG_d"), %%mm2        \n\t"
        "psubw %%mm7, %%mm1                     \n\t" //d3

        "paddw %%mm7, %%mm5                     \n\t" //d5
        "psubw %%mm3, %%mm1                     \n\t"

        "movq 1*8+1*16(%%"REG_d"), %%mm7        \n\t"
        "psubw %%mm2, %%mm5                     \n\t"

        "movq %%mm0, %%mm6                      \n\t"
        "paddw %%mm4, %%mm0                     \n\t" //d1

        "paddusw %%mm3, %%mm1                   \n\t"
        "psubw %%mm4, %%mm6                     \n\t" //d7

        // d1 d3 - - - d5 d7 -
        "movq 1*8+7*16(%%"REG_d"), %%mm4        \n\t"
        "psubw %%mm7, %%mm0                     \n\t"

        "psubw %%mm4, %%mm6                     \n\t"
        "paddusw %%mm2, %%mm5                   \n\t"

        "paddusw %%mm4, %%mm6                   \n\t"
        "paddw %%mm3, %%mm1                     \n\t"

        "paddw %%mm2, %%mm5                     \n\t"
        "paddw %%mm4, %%mm6                     \n\t"

        "psubusw %%mm3, %%mm1                   \n\t"
        "psubusw %%mm2, %%mm5                   \n\t"

        "psubusw %%mm4, %%mm6                   \n\t"
        "movq %%mm1, %%mm4                      \n\t"

        "por %%mm5, %%mm4                       \n\t"
        "paddusw %%mm7, %%mm0                   \n\t"

        "por %%mm6, %%mm4                       \n\t"
        "paddw %%mm7, %%mm0                     \n\t"

        "packssdw %%mm4, %%mm4                  \n\t"
        "psubusw %%mm7, %%mm0                   \n\t"

        "movd %%mm4, %%"REG_a"                  \n\t"
        "or %%"REG_a", %%"REG_a"                \n\t"
        "jnz 3f                                 \n\t"
        //movq [edi+"DCTSIZE_S"*3*2], mm1
        //movq [edi+"DCTSIZE_S"*5*2], mm5
        //movq [edi+"DCTSIZE_S"*1*2], mm0
        //movq [edi+"DCTSIZE_S"*7*2], mm6
        // t4 t5 - - - t6 t7 -
        //--- t4 (mm0) may be <>0; mm1, mm5, mm6 == 0
//Typical numbers: nondc - 19%%,  dc - 26%%,  zero - 55%%. zero case alone isn't worthwhile
        "movq 0*8+%3, %%mm4                    \n\t"
        "movq %%mm0, %%mm1                     \n\t"

        "pmulhw "MANGLE(MM_FIX_0_847759065)", %%mm0 \n\t" //tmp6
        "movq %%mm1, %%mm2                     \n\t"

        "movq "DCTSIZE_S"*0*2(%%"REG_D"), %%mm5\n\t"
        "movq %%mm2, %%mm3                     \n\t"

        "pmulhw "MANGLE(MM_FIX_0_566454497)", %%mm1 \n\t" //tmp5
        "paddw %%mm4, %%mm5                    \n\t"

        "movq 1*8+%3, %%mm6                    \n\t"
        //paddw mm3, MM_2
        "psraw $2, %%mm3                       \n\t" //tmp7

        "pmulhw "MANGLE(MM_FIX_0_198912367)", %%mm2 \n\t" //-tmp4
        "psubw %%mm3, %%mm4                    \n\t"

        "movq "DCTSIZE_S"*1*2(%%"REG_D"), %%mm7\n\t"
        "paddw %%mm3, %%mm5                    \n\t"

        "movq %%mm4, "DCTSIZE_S"*7*2(%%"REG_D")\n\t"
        "paddw %%mm6, %%mm7                    \n\t"

        "movq 2*8+%3, %%mm3                    \n\t"
        "psubw %%mm0, %%mm6                    \n\t"

        "movq "DCTSIZE_S"*2*2(%%"REG_D"), %%mm4\n\t"
        "paddw %%mm0, %%mm7                    \n\t"

        "movq %%mm5, "DCTSIZE_S"*0*2(%%"REG_D")\n\t"
        "paddw %%mm3, %%mm4                    \n\t"

        "movq %%mm6, "DCTSIZE_S"*6*2(%%"REG_D")\n\t"
        "psubw %%mm1, %%mm3                    \n\t"

        "movq "DCTSIZE_S"*5*2(%%"REG_D"), %%mm5\n\t"
        "paddw %%mm1, %%mm4                    \n\t"

        "movq "DCTSIZE_S"*3*2(%%"REG_D"), %%mm6\n\t"
        "paddw %%mm3, %%mm5                    \n\t"

        "movq 3*8+%3, %%mm0                    \n\t"
        "add $24, %%"REG_S"                    \n\t"

        "movq %%mm7, "DCTSIZE_S"*1*2(%%"REG_D")\n\t"
        "paddw %%mm0, %%mm6                    \n\t"

        "movq %%mm4, "DCTSIZE_S"*2*2(%%"REG_D")\n\t"
        "psubw %%mm2, %%mm0                    \n\t"

        "movq "DCTSIZE_S"*4*2(%%"REG_D"), %%mm7\n\t"
        "paddw %%mm2, %%mm6                    \n\t"

        "movq %%mm5, "DCTSIZE_S"*5*2(%%"REG_D")\n\t"
        "paddw %%mm0, %%mm7                    \n\t"

        "movq %%mm6, "DCTSIZE_S"*3*2(%%"REG_D")\n\t"

        "movq %%mm7, "DCTSIZE_S"*4*2(%%"REG_D")\n\t"
        "add $24, %%"REG_D"                    \n\t"
        "sub $2, %%"REG_c"                     \n\t"
        "jnz 1b                                \n\t"
        "jmp 5f                                \n\t"

        "3:                                    \n\t"
        //--- non DC2
        //psraw mm1, 2 w/o it -> offset. thr1, thr1, thr1  (actually thr1, thr1, thr1-1)
        //psraw mm5, 2
        //psraw mm0, 2
        //psraw mm6, 2
        "movq %%mm5, %%mm3                    \n\t"
        "psubw %%mm1, %%mm5                   \n\t"

        "psllw $1, %%mm5                      \n\t" //'z10
        "paddw %%mm1, %%mm3                   \n\t" //'z13

        "movq %%mm0, %%mm2                    \n\t"
        "psubw %%mm6, %%mm0                   \n\t"

        "movq %%mm5, %%mm1                    \n\t"
        "psllw $1, %%mm0                      \n\t" //'z12

        "pmulhw "MANGLE(MM_FIX_2_613125930)", %%mm1 \n\t" //-
        "paddw %%mm0, %%mm5                   \n\t"

        "pmulhw "MANGLE(MM_FIX_1_847759065)", %%mm5 \n\t" //'z5
        "paddw %%mm6, %%mm2                   \n\t" //'z11

        "pmulhw "MANGLE(MM_FIX_1_082392200)", %%mm0 \n\t"
        "movq %%mm2, %%mm7                    \n\t"

        //---
        "movq 0*8+%3, %%mm4                   \n\t"
        "psubw %%mm3, %%mm2                   \n\t"

        "psllw $1, %%mm2                      \n\t"
        "paddw %%mm3, %%mm7                   \n\t" //'t7

        "pmulhw "MANGLE(MM_FIX_1_414213562)", %%mm2 \n\t" //'t11
        "movq %%mm4, %%mm6                    \n\t"
        //paddw mm7, MM_2
        "psraw $2, %%mm7                      \n\t"

        "paddw "DCTSIZE_S"*0*2(%%"REG_D"), %%mm4 \n\t"
        "psubw %%mm7, %%mm6                   \n\t"

        "movq 1*8+%3, %%mm3                   \n\t"
        "paddw %%mm7, %%mm4                   \n\t"

        "movq %%mm6, "DCTSIZE_S"*7*2(%%"REG_D") \n\t"
        "paddw %%mm5, %%mm1                   \n\t" //'t12

        "movq %%mm4, "DCTSIZE_S"*0*2(%%"REG_D") \n\t"
        "psubw %%mm7, %%mm1                   \n\t" //'t6

        "movq 2*8+%3, %%mm7                   \n\t"
        "psubw %%mm5, %%mm0                   \n\t" //'t10

        "movq 3*8+%3, %%mm6                   \n\t"
        "movq %%mm3, %%mm5                    \n\t"

        "paddw "DCTSIZE_S"*1*2(%%"REG_D"), %%mm3 \n\t"
        "psubw %%mm1, %%mm5                   \n\t"

        "psubw %%mm1, %%mm2                   \n\t" //'t5
        "paddw %%mm1, %%mm3                   \n\t"

        "movq %%mm5, "DCTSIZE_S"*6*2(%%"REG_D") \n\t"
        "movq %%mm7, %%mm4                    \n\t"

        "paddw "DCTSIZE_S"*2*2(%%"REG_D"), %%mm7 \n\t"
        "psubw %%mm2, %%mm4                   \n\t"

        "paddw "DCTSIZE_S"*5*2(%%"REG_D"), %%mm4 \n\t"
        "paddw %%mm2, %%mm7                   \n\t"

        "movq %%mm3, "DCTSIZE_S"*1*2(%%"REG_D") \n\t"
        "paddw %%mm2, %%mm0                    \n\t" //'t4

        // 't4 't6 't5 - - - - 't7
        "movq %%mm7, "DCTSIZE_S"*2*2(%%"REG_D") \n\t"
        "movq %%mm6, %%mm1                     \n\t"

        "paddw "DCTSIZE_S"*4*2(%%"REG_D"), %%mm6 \n\t"
        "psubw %%mm0, %%mm1                    \n\t"

        "paddw "DCTSIZE_S"*3*2(%%"REG_D"), %%mm1 \n\t"
        "paddw %%mm0, %%mm6                    \n\t"

        "movq %%mm4, "DCTSIZE_S"*5*2(%%"REG_D") \n\t"
        "add $24, %%"REG_S"                    \n\t"

        "movq %%mm6, "DCTSIZE_S"*4*2(%%"REG_D") \n\t"

        "movq %%mm1, "DCTSIZE_S"*3*2(%%"REG_D") \n\t"
        "add $24, %%"REG_D"                    \n\t"
        "sub $2, %%"REG_c"                     \n\t"
        "jnz 1b                                \n\t"
        "5:                                    \n\t"

        : "+S"(data), "+D"(output), "+c"(cnt), "=o"(temps)
        : "d"(thr_adr)
          NAMED_CONSTRAINTS_ADD(ff_MM_FIX_0_707106781, MM_2,MM_FIX_1_414213562_A, MM_FIX_1_414213562, MM_FIX_0_382683433,
                                ff_MM_FIX_0_541196100, MM_FIX_1_306562965, MM_FIX_0_847759065)
          NAMED_CONSTRAINTS_ADD(MM_FIX_0_566454497, MM_FIX_0_198912367, MM_FIX_2_613125930, MM_FIX_1_847759065,
                                MM_FIX_1_082392200)
        : "%"REG_a
        );
}

static void row_idct_mmx (int16_t *workspace, int16_t *output_adr, int output_stride, int cnt)
{
    DECLARE_ALIGNED(8, uint64_t, temps)[4];

    __asm__ volatile(
        "lea (%%"REG_a",%%"REG_a",2), %%"REG_d"    \n\t"
        "1:                     \n\t"
        "movq "DCTSIZE_S"*0*2(%%"REG_S"), %%mm0    \n\t"
        //

        "movq "DCTSIZE_S"*1*2(%%"REG_S"), %%mm1    \n\t"
        "movq %%mm0, %%mm4                         \n\t"

        "movq "DCTSIZE_S"*2*2(%%"REG_S"), %%mm2    \n\t"
        "punpcklwd %%mm1, %%mm0                    \n\t"

        "movq "DCTSIZE_S"*3*2(%%"REG_S"), %%mm3    \n\t"
        "punpckhwd %%mm1, %%mm4                    \n\t"

        //transpose 4x4
        "movq %%mm2, %%mm7                         \n\t"
        "punpcklwd %%mm3, %%mm2                    \n\t"

        "movq %%mm0, %%mm6                         \n\t"
        "punpckldq %%mm2, %%mm0                    \n\t" //0

        "punpckhdq %%mm2, %%mm6                    \n\t" //1
        "movq %%mm0, %%mm5                         \n\t"

        "punpckhwd %%mm3, %%mm7                    \n\t"
        "psubw %%mm6, %%mm0                        \n\t"

        "pmulhw "MANGLE(MM_FIX_1_414213562_A)", %%mm0 \n\t"
        "movq %%mm4, %%mm2                         \n\t"

        "punpckldq %%mm7, %%mm4                    \n\t" //2
        "paddw %%mm6, %%mm5                        \n\t"

        "punpckhdq %%mm7, %%mm2                    \n\t" //3
        "movq %%mm4, %%mm1                         \n\t"

        "psllw $2, %%mm0                           \n\t"
        "paddw %%mm2, %%mm4                        \n\t" //t10

        "movq "DCTSIZE_S"*0*2+"DCTSIZE_S"(%%"REG_S"), %%mm3 \n\t"
        "psubw %%mm2, %%mm1                        \n\t" //t11

        "movq "DCTSIZE_S"*1*2+"DCTSIZE_S"(%%"REG_S"), %%mm2 \n\t"
        "psubw %%mm5, %%mm0                        \n\t"

        "movq %%mm4, %%mm6                         \n\t"
        "paddw %%mm5, %%mm4                        \n\t" //t0

        "psubw %%mm5, %%mm6                        \n\t" //t3
        "movq %%mm1, %%mm7                         \n\t"

        "movq "DCTSIZE_S"*2*2+"DCTSIZE_S"(%%"REG_S"), %%mm5 \n\t"
        "paddw %%mm0, %%mm1                        \n\t" //t1

        "movq %%mm4, 0*8+%3                        \n\t" //t0
        "movq %%mm3, %%mm4                         \n\t"

        "movq %%mm6, 1*8+%3                        \n\t" //t3
        "punpcklwd %%mm2, %%mm3                    \n\t"

        //transpose 4x4
        "movq "DCTSIZE_S"*3*2+"DCTSIZE_S"(%%"REG_S"), %%mm6 \n\t"
        "punpckhwd %%mm2, %%mm4                    \n\t"

        "movq %%mm5, %%mm2                         \n\t"
        "punpcklwd %%mm6, %%mm5                    \n\t"

        "psubw %%mm0, %%mm7                        \n\t" //t2
        "punpckhwd %%mm6, %%mm2                    \n\t"

        "movq %%mm3, %%mm0                         \n\t"
        "punpckldq %%mm5, %%mm3                    \n\t" //4

        "punpckhdq %%mm5, %%mm0                    \n\t" //5
        "movq %%mm4, %%mm5                         \n\t"

        //
        "movq %%mm3, %%mm6                         \n\t"
        "punpckldq %%mm2, %%mm4                    \n\t" //6

        "psubw %%mm0, %%mm3                        \n\t" //z10
        "punpckhdq %%mm2, %%mm5                    \n\t" //7

        "paddw %%mm0, %%mm6                        \n\t" //z13
        "movq %%mm4, %%mm2                         \n\t"

        "movq %%mm3, %%mm0                         \n\t"
        "psubw %%mm5, %%mm4                        \n\t" //z12

        "pmulhw "MANGLE(MM_FIX_2_613125930)", %%mm0\n\t" //-
        "paddw %%mm4, %%mm3                        \n\t"

        "pmulhw "MANGLE(MM_FIX_1_847759065)", %%mm3\n\t" //z5
        "paddw %%mm5, %%mm2                        \n\t" //z11  >

        "pmulhw "MANGLE(MM_FIX_1_082392200)", %%mm4\n\t"
        "movq %%mm2, %%mm5                         \n\t"

        "psubw %%mm6, %%mm2                        \n\t"
        "paddw %%mm6, %%mm5                        \n\t" //t7

        "pmulhw "MANGLE(MM_FIX_1_414213562)", %%mm2\n\t" //t11
        "paddw %%mm3, %%mm0                        \n\t" //t12

        "psllw $3, %%mm0                           \n\t"
        "psubw %%mm3, %%mm4                        \n\t" //t10

        "movq 0*8+%3, %%mm6                        \n\t"
        "movq %%mm1, %%mm3                         \n\t"

        "psllw $3, %%mm4                           \n\t"
        "psubw %%mm5, %%mm0                        \n\t" //t6

        "psllw $3, %%mm2                           \n\t"
        "paddw %%mm0, %%mm1                        \n\t" //d1

        "psubw %%mm0, %%mm2                        \n\t" //t5
        "psubw %%mm0, %%mm3                        \n\t" //d6

        "paddw %%mm2, %%mm4                        \n\t" //t4
        "movq %%mm7, %%mm0                         \n\t"

        "paddw %%mm2, %%mm7                        \n\t" //d2
        "psubw %%mm2, %%mm0                        \n\t" //d5

        "movq "MANGLE(MM_DESCALE_RND)", %%mm2      \n\t" //4
        "psubw %%mm5, %%mm6                        \n\t" //d7

        "paddw 0*8+%3, %%mm5                       \n\t" //d0
        "paddw %%mm2, %%mm1                        \n\t"

        "paddw %%mm2, %%mm5                        \n\t"
        "psraw $3, %%mm1                           \n\t"

        "paddw %%mm2, %%mm7                        \n\t"
        "psraw $3, %%mm5                           \n\t"

        "paddw (%%"REG_D"), %%mm5                  \n\t"
        "psraw $3, %%mm7                           \n\t"

        "paddw (%%"REG_D",%%"REG_a"), %%mm1        \n\t"
        "paddw %%mm2, %%mm0                        \n\t"

        "paddw (%%"REG_D",%%"REG_a",2), %%mm7      \n\t"
        "paddw %%mm2, %%mm3                        \n\t"

        "movq %%mm5, (%%"REG_D")                   \n\t"
        "paddw %%mm2, %%mm6                        \n\t"

        "movq %%mm1, (%%"REG_D",%%"REG_a")         \n\t"
        "psraw $3, %%mm0                           \n\t"

        "movq %%mm7, (%%"REG_D",%%"REG_a",2)       \n\t"
        "add %%"REG_d", %%"REG_D"                  \n\t" //3*ls

        "movq 1*8+%3, %%mm5                        \n\t" //t3
        "psraw $3, %%mm3                           \n\t"

        "paddw (%%"REG_D",%%"REG_a",2), %%mm0      \n\t"
        "psubw %%mm4, %%mm5                        \n\t" //d3

        "paddw (%%"REG_D",%%"REG_d"), %%mm3        \n\t"
        "psraw $3, %%mm6                           \n\t"

        "paddw 1*8+%3, %%mm4                       \n\t" //d4
        "paddw %%mm2, %%mm5                        \n\t"

        "paddw (%%"REG_D",%%"REG_a",4), %%mm6      \n\t"
        "paddw %%mm2, %%mm4                        \n\t"

        "movq %%mm0, (%%"REG_D",%%"REG_a",2)       \n\t"
        "psraw $3, %%mm5                           \n\t"

        "paddw (%%"REG_D"), %%mm5                  \n\t"
        "psraw $3, %%mm4                           \n\t"

        "paddw (%%"REG_D",%%"REG_a"), %%mm4        \n\t"
        "add $"DCTSIZE_S"*2*4, %%"REG_S"           \n\t" //4 rows

        "movq %%mm3, (%%"REG_D",%%"REG_d")         \n\t"
        "movq %%mm6, (%%"REG_D",%%"REG_a",4)       \n\t"
        "movq %%mm5, (%%"REG_D")                   \n\t"
        "movq %%mm4, (%%"REG_D",%%"REG_a")         \n\t"

        "sub %%"REG_d", %%"REG_D"                  \n\t"
        "add $8, %%"REG_D"                         \n\t"
        "dec %%"REG_c"                             \n\t"
        "jnz 1b                                    \n\t"

        : "+S"(workspace), "+D"(output_adr), "+c"(cnt), "=o"(temps)
        : "a"(output_stride * sizeof(short))
        NAMED_CONSTRAINTS_ADD(MM_FIX_1_414213562_A, MM_FIX_2_613125930, MM_FIX_1_847759065, MM_FIX_1_082392200,
                              MM_FIX_1_414213562,MM_DESCALE_RND)
        : "%"REG_d
        );
}

static void row_fdct_mmx(int16_t *data, const uint8_t *pixels, int line_size, int cnt)
{
    DECLARE_ALIGNED(8, uint64_t, temps)[4];

    __asm__ volatile(
        "lea (%%"REG_a",%%"REG_a",2), %%"REG_d"    \n\t"
        "6:                                        \n\t"
        "movd (%%"REG_S"), %%mm0                   \n\t"
        "pxor %%mm7, %%mm7                         \n\t"

        "movd (%%"REG_S",%%"REG_a"), %%mm1         \n\t"
        "punpcklbw %%mm7, %%mm0                    \n\t"

        "movd (%%"REG_S",%%"REG_a",2), %%mm2       \n\t"
        "punpcklbw %%mm7, %%mm1                    \n\t"

        "punpcklbw %%mm7, %%mm2                    \n\t"
        "add %%"REG_d", %%"REG_S"                  \n\t"

        "movq %%mm0, %%mm5                         \n\t"
        //

        "movd (%%"REG_S",%%"REG_a",4), %%mm3       \n\t" //7  ;prefetch!
        "movq %%mm1, %%mm6                         \n\t"

        "movd (%%"REG_S",%%"REG_d"), %%mm4         \n\t" //6
        "punpcklbw %%mm7, %%mm3                    \n\t"

        "psubw %%mm3, %%mm5                        \n\t"
        "punpcklbw %%mm7, %%mm4                    \n\t"

        "paddw %%mm3, %%mm0                        \n\t"
        "psubw %%mm4, %%mm6                        \n\t"

        "movd (%%"REG_S",%%"REG_a",2), %%mm3       \n\t" //5
        "paddw %%mm4, %%mm1                        \n\t"

        "movq %%mm5, %3                            \n\t" //t7
        "punpcklbw %%mm7, %%mm3                    \n\t"

        "movq %%mm6, %4                            \n\t" //t6
        "movq %%mm2, %%mm4                         \n\t"

        "movd (%%"REG_S"), %%mm5                   \n\t" //3
        "paddw %%mm3, %%mm2                        \n\t"

        "movd (%%"REG_S",%%"REG_a"), %%mm6         \n\t" //4
        "punpcklbw %%mm7, %%mm5                    \n\t"

        "psubw %%mm3, %%mm4                        \n\t"
        "punpcklbw %%mm7, %%mm6                    \n\t"

        "movq %%mm5, %%mm3                         \n\t"
        "paddw %%mm6, %%mm5                        \n\t" //t3

        "psubw %%mm6, %%mm3                        \n\t" //t4  ; t0 t1 t2 t4 t5 t3 - -
        "movq %%mm0, %%mm6                         \n\t"

        "movq %%mm1, %%mm7                         \n\t"
        "psubw %%mm5, %%mm0                        \n\t" //t13

        "psubw %%mm2, %%mm1                        \n\t"
        "paddw %%mm2, %%mm7                        \n\t" //t11

        "paddw %%mm0, %%mm1                        \n\t"
        "movq %%mm7, %%mm2                         \n\t"

        "psllw $2, %%mm1                           \n\t"
        "paddw %%mm5, %%mm6                        \n\t" //t10

        "pmulhw "MANGLE(ff_MM_FIX_0_707106781)", %%mm1 \n\t"
        "paddw %%mm6, %%mm7                        \n\t" //d2

        "psubw %%mm2, %%mm6                        \n\t" //d3
        "movq %%mm0, %%mm5                         \n\t"

        //transpose 4x4
        "movq %%mm7, %%mm2                         \n\t"
        "punpcklwd %%mm6, %%mm7                    \n\t"

        "paddw %%mm1, %%mm0                        \n\t" //d0
        "punpckhwd %%mm6, %%mm2                    \n\t"

        "psubw %%mm1, %%mm5                        \n\t" //d1
        "movq %%mm0, %%mm6                         \n\t"

        "movq %4, %%mm1                            \n\t"
        "punpcklwd %%mm5, %%mm0                    \n\t"

        "punpckhwd %%mm5, %%mm6                    \n\t"
        "movq %%mm0, %%mm5                         \n\t"

        "punpckldq %%mm7, %%mm0                    \n\t" //0
        "paddw %%mm4, %%mm3                        \n\t"

        "punpckhdq %%mm7, %%mm5                    \n\t" //1
        "movq %%mm6, %%mm7                         \n\t"

        "movq %%mm0, "DCTSIZE_S"*0*2(%%"REG_D")    \n\t"
        "punpckldq %%mm2, %%mm6                    \n\t" //2

        "movq %%mm5, "DCTSIZE_S"*1*2(%%"REG_D")    \n\t"
        "punpckhdq %%mm2, %%mm7                    \n\t" //3

        "movq %%mm6, "DCTSIZE_S"*2*2(%%"REG_D")    \n\t"
        "paddw %%mm1, %%mm4                        \n\t"

        "movq %%mm7, "DCTSIZE_S"*3*2(%%"REG_D")    \n\t"
        "psllw $2, %%mm3                           \n\t" //t10

        "movq %3, %%mm2                            \n\t"
        "psllw $2, %%mm4                           \n\t" //t11

        "pmulhw "MANGLE(ff_MM_FIX_0_707106781)", %%mm4 \n\t" //z3
        "paddw %%mm2, %%mm1                        \n\t"

        "psllw $2, %%mm1                           \n\t" //t12
        "movq %%mm3, %%mm0                         \n\t"

        "pmulhw "MANGLE(ff_MM_FIX_0_541196100)", %%mm0 \n\t"
        "psubw %%mm1, %%mm3                        \n\t"

        "pmulhw "MANGLE(MM_FIX_0_382683433)", %%mm3 \n\t" //z5
        "movq %%mm2, %%mm5                         \n\t"

        "pmulhw "MANGLE(MM_FIX_1_306562965)", %%mm1 \n\t"
        "psubw %%mm4, %%mm2                        \n\t" //z13

        "paddw %%mm4, %%mm5                        \n\t" //z11
        "movq %%mm2, %%mm6                         \n\t"

        "paddw %%mm3, %%mm0                        \n\t" //z2
        "movq %%mm5, %%mm7                         \n\t"

        "paddw %%mm0, %%mm2                        \n\t" //d4
        "psubw %%mm0, %%mm6                        \n\t" //d5

        "movq %%mm2, %%mm4                         \n\t"
        "paddw %%mm3, %%mm1                        \n\t" //z4

        //transpose 4x4
        "punpcklwd %%mm6, %%mm2                    \n\t"
        "paddw %%mm1, %%mm5                        \n\t" //d6

        "punpckhwd %%mm6, %%mm4                    \n\t"
        "psubw %%mm1, %%mm7                        \n\t" //d7

        "movq %%mm5, %%mm6                         \n\t"
        "punpcklwd %%mm7, %%mm5                    \n\t"

        "punpckhwd %%mm7, %%mm6                    \n\t"
        "movq %%mm2, %%mm7                         \n\t"

        "punpckldq %%mm5, %%mm2                    \n\t" //4
        "sub %%"REG_d", %%"REG_S"                  \n\t"

        "punpckhdq %%mm5, %%mm7                    \n\t" //5
        "movq %%mm4, %%mm5                         \n\t"

        "movq %%mm2, "DCTSIZE_S"*0*2+"DCTSIZE_S"(%%"REG_D") \n\t"
        "punpckldq %%mm6, %%mm4                    \n\t" //6

        "movq %%mm7, "DCTSIZE_S"*1*2+"DCTSIZE_S"(%%"REG_D") \n\t"
        "punpckhdq %%mm6, %%mm5                    \n\t" //7

        "movq %%mm4, "DCTSIZE_S"*2*2+"DCTSIZE_S"(%%"REG_D") \n\t"
        "add $4, %%"REG_S"                         \n\t"

        "movq %%mm5, "DCTSIZE_S"*3*2+"DCTSIZE_S"(%%"REG_D") \n\t"
        "add $"DCTSIZE_S"*2*4, %%"REG_D"           \n\t" //4 rows
        "dec %%"REG_c"                             \n\t"
        "jnz 6b                                    \n\t"

        : "+S"(pixels), "+D"(data), "+c"(cnt), "=o"(temps), "=o"(temps[1])
        : "a"(line_size)
        NAMED_CONSTRAINTS_ADD(ff_MM_FIX_0_707106781, ff_MM_FIX_0_541196100, MM_FIX_0_382683433, MM_FIX_1_306562965)
        : "%"REG_d);
}
#endif

av_cold void ff_fspp_init_x86(FSPPContext *s)
{
#if HAVE_MMX_INLINE
    int cpu_flags = av_get_cpu_flags();

    if (HAVE_MMX_INLINE && cpu_flags & AV_CPU_FLAG_MMX) {
        s->store_slice  = store_slice_mmx;
        s->store_slice2 = store_slice2_mmx;
        s->mul_thrmat   = mul_thrmat_mmx;
        s->column_fidct = column_fidct_mmx;
        s->row_idct     = row_idct_mmx;
        s->row_fdct     = row_fdct_mmx;
    }
#endif
}
