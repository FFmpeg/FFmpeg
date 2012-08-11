/*
 * DSP utils mmx functions are compiled twice for rnd/no_rnd
 * Copyright (c) 2000, 2001 Fabrice Bellard
 * Copyright (c) 2003-2004 Michael Niedermayer <michaelni@gmx.at>
 *
 * MMX optimization by Nick Kurshev <nickols_k@mail.ru>
 * mostly rewritten by Michael Niedermayer <michaelni@gmx.at>
 * and improved by Zdenek Kabelac <kabi@users.sf.net>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

// put_pixels
static void DEF(put, pixels8_x2)(uint8_t *block, const uint8_t *pixels, int line_size, int h)
{
    MOVQ_BFE(mm6);
    __asm__ volatile(
        "lea    (%3, %3), %%"REG_a"     \n\t"
        ".p2align 3                     \n\t"
        "1:                             \n\t"
        "movq   (%1), %%mm0             \n\t"
        "movq   1(%1), %%mm1            \n\t"
        "movq   (%1, %3), %%mm2         \n\t"
        "movq   1(%1, %3), %%mm3        \n\t"
        PAVGBP(%%mm0, %%mm1, %%mm4,   %%mm2, %%mm3, %%mm5)
        "movq   %%mm4, (%2)             \n\t"
        "movq   %%mm5, (%2, %3)         \n\t"
        "add    %%"REG_a", %1           \n\t"
        "add    %%"REG_a", %2           \n\t"
        "movq   (%1), %%mm0             \n\t"
        "movq   1(%1), %%mm1            \n\t"
        "movq   (%1, %3), %%mm2         \n\t"
        "movq   1(%1, %3), %%mm3        \n\t"
        PAVGBP(%%mm0, %%mm1, %%mm4,   %%mm2, %%mm3, %%mm5)
        "movq   %%mm4, (%2)             \n\t"
        "movq   %%mm5, (%2, %3)         \n\t"
        "add    %%"REG_a", %1           \n\t"
        "add    %%"REG_a", %2           \n\t"
        "subl   $4, %0                  \n\t"
        "jnz    1b                      \n\t"
        :"+g"(h), "+S"(pixels), "+D"(block)
        :"r"((x86_reg)line_size)
        :REG_a, "memory");
}

static void av_unused DEF(put, pixels8_l2)(uint8_t *dst, uint8_t *src1, uint8_t *src2, int dstStride, int src1Stride, int h)
{
    MOVQ_BFE(mm6);
    __asm__ volatile(
        "testl $1, %0                   \n\t"
        " jz 1f                         \n\t"
        "movq   (%1), %%mm0             \n\t"
        "movq   (%2), %%mm1             \n\t"
        "add    %4, %1                  \n\t"
        "add    $8, %2                  \n\t"
        PAVGB(%%mm0, %%mm1, %%mm4, %%mm6)
        "movq   %%mm4, (%3)             \n\t"
        "add    %5, %3                  \n\t"
        "decl   %0                      \n\t"
        ".p2align 3                     \n\t"
        "1:                             \n\t"
        "movq   (%1), %%mm0             \n\t"
        "movq   (%2), %%mm1             \n\t"
        "add    %4, %1                  \n\t"
        "movq   (%1), %%mm2             \n\t"
        "movq   8(%2), %%mm3            \n\t"
        "add    %4, %1                  \n\t"
        PAVGBP(%%mm0, %%mm1, %%mm4,   %%mm2, %%mm3, %%mm5)
        "movq   %%mm4, (%3)             \n\t"
        "add    %5, %3                  \n\t"
        "movq   %%mm5, (%3)             \n\t"
        "add    %5, %3                  \n\t"
        "movq   (%1), %%mm0             \n\t"
        "movq   16(%2), %%mm1           \n\t"
        "add    %4, %1                  \n\t"
        "movq   (%1), %%mm2             \n\t"
        "movq   24(%2), %%mm3           \n\t"
        "add    %4, %1                  \n\t"
        "add    $32, %2                 \n\t"
        PAVGBP(%%mm0, %%mm1, %%mm4,   %%mm2, %%mm3, %%mm5)
        "movq   %%mm4, (%3)             \n\t"
        "add    %5, %3                  \n\t"
        "movq   %%mm5, (%3)             \n\t"
        "add    %5, %3                  \n\t"
        "subl   $4, %0                  \n\t"
        "jnz    1b                      \n\t"
#if !HAVE_EBX_AVAILABLE //Note "+bm" and "+mb" are buggy too (with gcc 3.2.2 at least) and cannot be used
        :"+m"(h), "+a"(src1), "+c"(src2), "+d"(dst)
#else
        :"+b"(h), "+a"(src1), "+c"(src2), "+d"(dst)
#endif
        :"S"((x86_reg)src1Stride), "D"((x86_reg)dstStride)
        :"memory");
}

static void DEF(put, pixels16_x2)(uint8_t *block, const uint8_t *pixels, int line_size, int h)
{
    MOVQ_BFE(mm6);
    __asm__ volatile(
        "lea        (%3, %3), %%"REG_a" \n\t"
        ".p2align 3                     \n\t"
        "1:                             \n\t"
        "movq   (%1), %%mm0             \n\t"
        "movq   1(%1), %%mm1            \n\t"
        "movq   (%1, %3), %%mm2         \n\t"
        "movq   1(%1, %3), %%mm3        \n\t"
        PAVGBP(%%mm0, %%mm1, %%mm4,   %%mm2, %%mm3, %%mm5)
        "movq   %%mm4, (%2)             \n\t"
        "movq   %%mm5, (%2, %3)         \n\t"
        "movq   8(%1), %%mm0            \n\t"
        "movq   9(%1), %%mm1            \n\t"
        "movq   8(%1, %3), %%mm2        \n\t"
        "movq   9(%1, %3), %%mm3        \n\t"
        PAVGBP(%%mm0, %%mm1, %%mm4,   %%mm2, %%mm3, %%mm5)
        "movq   %%mm4, 8(%2)            \n\t"
        "movq   %%mm5, 8(%2, %3)        \n\t"
        "add    %%"REG_a", %1           \n\t"
        "add    %%"REG_a", %2           \n\t"
        "movq   (%1), %%mm0             \n\t"
        "movq   1(%1), %%mm1            \n\t"
        "movq   (%1, %3), %%mm2         \n\t"
        "movq   1(%1, %3), %%mm3        \n\t"
        PAVGBP(%%mm0, %%mm1, %%mm4,   %%mm2, %%mm3, %%mm5)
        "movq   %%mm4, (%2)             \n\t"
        "movq   %%mm5, (%2, %3)         \n\t"
        "movq   8(%1), %%mm0            \n\t"
        "movq   9(%1), %%mm1            \n\t"
        "movq   8(%1, %3), %%mm2        \n\t"
        "movq   9(%1, %3), %%mm3        \n\t"
        PAVGBP(%%mm0, %%mm1, %%mm4,   %%mm2, %%mm3, %%mm5)
        "movq   %%mm4, 8(%2)            \n\t"
        "movq   %%mm5, 8(%2, %3)        \n\t"
        "add    %%"REG_a", %1           \n\t"
        "add    %%"REG_a", %2           \n\t"
        "subl   $4, %0                  \n\t"
        "jnz    1b                      \n\t"
        :"+g"(h), "+S"(pixels), "+D"(block)
        :"r"((x86_reg)line_size)
        :REG_a, "memory");
}

static void av_unused DEF(put, pixels16_l2)(uint8_t *dst, uint8_t *src1, uint8_t *src2, int dstStride, int src1Stride, int h)
{
    MOVQ_BFE(mm6);
    __asm__ volatile(
        "testl $1, %0                   \n\t"
        " jz 1f                         \n\t"
        "movq   (%1), %%mm0             \n\t"
        "movq   (%2), %%mm1             \n\t"
        "movq   8(%1), %%mm2            \n\t"
        "movq   8(%2), %%mm3            \n\t"
        "add    %4, %1                  \n\t"
        "add    $16, %2                 \n\t"
        PAVGBP(%%mm0, %%mm1, %%mm4,   %%mm2, %%mm3, %%mm5)
        "movq   %%mm4, (%3)             \n\t"
        "movq   %%mm5, 8(%3)            \n\t"
        "add    %5, %3                  \n\t"
        "decl   %0                      \n\t"
        ".p2align 3                     \n\t"
        "1:                             \n\t"
        "movq   (%1), %%mm0             \n\t"
        "movq   (%2), %%mm1             \n\t"
        "movq   8(%1), %%mm2            \n\t"
        "movq   8(%2), %%mm3            \n\t"
        "add    %4, %1                  \n\t"
        PAVGBP(%%mm0, %%mm1, %%mm4,   %%mm2, %%mm3, %%mm5)
        "movq   %%mm4, (%3)             \n\t"
        "movq   %%mm5, 8(%3)            \n\t"
        "add    %5, %3                  \n\t"
        "movq   (%1), %%mm0             \n\t"
        "movq   16(%2), %%mm1           \n\t"
        "movq   8(%1), %%mm2            \n\t"
        "movq   24(%2), %%mm3           \n\t"
        "add    %4, %1                  \n\t"
        PAVGBP(%%mm0, %%mm1, %%mm4,   %%mm2, %%mm3, %%mm5)
        "movq   %%mm4, (%3)             \n\t"
        "movq   %%mm5, 8(%3)            \n\t"
        "add    %5, %3                  \n\t"
        "add    $32, %2                 \n\t"
        "subl   $2, %0                  \n\t"
        "jnz    1b                      \n\t"
#if !HAVE_EBX_AVAILABLE  //Note "+bm" and "+mb" are buggy too (with gcc 3.2.2 at least) and cannot be used
        :"+m"(h), "+a"(src1), "+c"(src2), "+d"(dst)
#else
        :"+b"(h), "+a"(src1), "+c"(src2), "+d"(dst)
#endif
        :"S"((x86_reg)src1Stride), "D"((x86_reg)dstStride)
        :"memory");
}

static void DEF(put, pixels8_y2)(uint8_t *block, const uint8_t *pixels, int line_size, int h)
{
    MOVQ_BFE(mm6);
    __asm__ volatile(
        "lea (%3, %3), %%"REG_a"        \n\t"
        "movq (%1), %%mm0               \n\t"
        ".p2align 3                     \n\t"
        "1:                             \n\t"
        "movq   (%1, %3), %%mm1         \n\t"
        "movq   (%1, %%"REG_a"),%%mm2   \n\t"
        PAVGBP(%%mm1, %%mm0, %%mm4,   %%mm2, %%mm1, %%mm5)
        "movq   %%mm4, (%2)             \n\t"
        "movq   %%mm5, (%2, %3)         \n\t"
        "add    %%"REG_a", %1           \n\t"
        "add    %%"REG_a", %2           \n\t"
        "movq   (%1, %3), %%mm1         \n\t"
        "movq   (%1, %%"REG_a"),%%mm0   \n\t"
        PAVGBP(%%mm1, %%mm2, %%mm4,   %%mm0, %%mm1, %%mm5)
        "movq   %%mm4, (%2)             \n\t"
        "movq   %%mm5, (%2, %3)         \n\t"
        "add    %%"REG_a", %1           \n\t"
        "add    %%"REG_a", %2           \n\t"
        "subl   $4, %0                  \n\t"
        "jnz    1b                      \n\t"
        :"+g"(h), "+S"(pixels), "+D"(block)
        :"r"((x86_reg)line_size)
        :REG_a, "memory");
}

static void DEF(put, pixels8_xy2)(uint8_t *block, const uint8_t *pixels, int line_size, int h)
{
    MOVQ_ZERO(mm7);
    SET_RND(mm6); // =2 for rnd  and  =1 for no_rnd version
    __asm__ volatile(
        "movq   (%1), %%mm0             \n\t"
        "movq   1(%1), %%mm4            \n\t"
        "movq   %%mm0, %%mm1            \n\t"
        "movq   %%mm4, %%mm5            \n\t"
        "punpcklbw %%mm7, %%mm0         \n\t"
        "punpcklbw %%mm7, %%mm4         \n\t"
        "punpckhbw %%mm7, %%mm1         \n\t"
        "punpckhbw %%mm7, %%mm5         \n\t"
        "paddusw %%mm0, %%mm4           \n\t"
        "paddusw %%mm1, %%mm5           \n\t"
        "xor    %%"REG_a", %%"REG_a"    \n\t"
        "add    %3, %1                  \n\t"
        ".p2align 3                     \n\t"
        "1:                             \n\t"
        "movq   (%1, %%"REG_a"), %%mm0  \n\t"
        "movq   1(%1, %%"REG_a"), %%mm2 \n\t"
        "movq   %%mm0, %%mm1            \n\t"
        "movq   %%mm2, %%mm3            \n\t"
        "punpcklbw %%mm7, %%mm0         \n\t"
        "punpcklbw %%mm7, %%mm2         \n\t"
        "punpckhbw %%mm7, %%mm1         \n\t"
        "punpckhbw %%mm7, %%mm3         \n\t"
        "paddusw %%mm2, %%mm0           \n\t"
        "paddusw %%mm3, %%mm1           \n\t"
        "paddusw %%mm6, %%mm4           \n\t"
        "paddusw %%mm6, %%mm5           \n\t"
        "paddusw %%mm0, %%mm4           \n\t"
        "paddusw %%mm1, %%mm5           \n\t"
        "psrlw  $2, %%mm4               \n\t"
        "psrlw  $2, %%mm5               \n\t"
        "packuswb  %%mm5, %%mm4         \n\t"
        "movq   %%mm4, (%2, %%"REG_a")  \n\t"
        "add    %3, %%"REG_a"           \n\t"

        "movq   (%1, %%"REG_a"), %%mm2  \n\t" // 0 <-> 2   1 <-> 3
        "movq   1(%1, %%"REG_a"), %%mm4 \n\t"
        "movq   %%mm2, %%mm3            \n\t"
        "movq   %%mm4, %%mm5            \n\t"
        "punpcklbw %%mm7, %%mm2         \n\t"
        "punpcklbw %%mm7, %%mm4         \n\t"
        "punpckhbw %%mm7, %%mm3         \n\t"
        "punpckhbw %%mm7, %%mm5         \n\t"
        "paddusw %%mm2, %%mm4           \n\t"
        "paddusw %%mm3, %%mm5           \n\t"
        "paddusw %%mm6, %%mm0           \n\t"
        "paddusw %%mm6, %%mm1           \n\t"
        "paddusw %%mm4, %%mm0           \n\t"
        "paddusw %%mm5, %%mm1           \n\t"
        "psrlw  $2, %%mm0               \n\t"
        "psrlw  $2, %%mm1               \n\t"
        "packuswb  %%mm1, %%mm0         \n\t"
        "movq   %%mm0, (%2, %%"REG_a")  \n\t"
        "add    %3, %%"REG_a"           \n\t"

        "subl   $2, %0                  \n\t"
        "jnz    1b                      \n\t"
        :"+g"(h), "+S"(pixels)
        :"D"(block), "r"((x86_reg)line_size)
        :REG_a, "memory");
}

// avg_pixels
static void av_unused DEF(avg, pixels4)(uint8_t *block, const uint8_t *pixels, int line_size, int h)
{
    MOVQ_BFE(mm6);
    JUMPALIGN();
    do {
        __asm__ volatile(
             "movd  %0, %%mm0           \n\t"
             "movd  %1, %%mm1           \n\t"
             OP_AVG(%%mm0, %%mm1, %%mm2, %%mm6)
             "movd  %%mm2, %0           \n\t"
             :"+m"(*block)
             :"m"(*pixels)
             :"memory");
        pixels += line_size;
        block += line_size;
    }
    while (--h);
}

// in case more speed is needed - unroling would certainly help
static void DEF(avg, pixels8)(uint8_t *block, const uint8_t *pixels, int line_size, int h)
{
    MOVQ_BFE(mm6);
    JUMPALIGN();
    do {
        __asm__ volatile(
             "movq  %0, %%mm0           \n\t"
             "movq  %1, %%mm1           \n\t"
             OP_AVG(%%mm0, %%mm1, %%mm2, %%mm6)
             "movq  %%mm2, %0           \n\t"
             :"+m"(*block)
             :"m"(*pixels)
             :"memory");
        pixels += line_size;
        block += line_size;
    }
    while (--h);
}

static void DEF(avg, pixels16)(uint8_t *block, const uint8_t *pixels, int line_size, int h)
{
    MOVQ_BFE(mm6);
    JUMPALIGN();
    do {
        __asm__ volatile(
             "movq  %0, %%mm0           \n\t"
             "movq  %1, %%mm1           \n\t"
             OP_AVG(%%mm0, %%mm1, %%mm2, %%mm6)
             "movq  %%mm2, %0           \n\t"
             "movq  8%0, %%mm0          \n\t"
             "movq  8%1, %%mm1          \n\t"
             OP_AVG(%%mm0, %%mm1, %%mm2, %%mm6)
             "movq  %%mm2, 8%0          \n\t"
             :"+m"(*block)
             :"m"(*pixels)
             :"memory");
        pixels += line_size;
        block += line_size;
    }
    while (--h);
}

static void DEF(avg, pixels8_x2)(uint8_t *block, const uint8_t *pixels, int line_size, int h)
{
    MOVQ_BFE(mm6);
    JUMPALIGN();
    do {
        __asm__ volatile(
            "movq  %1, %%mm0            \n\t"
            "movq  1%1, %%mm1           \n\t"
            "movq  %0, %%mm3            \n\t"
            PAVGB(%%mm0, %%mm1, %%mm2, %%mm6)
            OP_AVG(%%mm3, %%mm2, %%mm0, %%mm6)
            "movq  %%mm0, %0            \n\t"
            :"+m"(*block)
            :"m"(*pixels)
            :"memory");
        pixels += line_size;
        block += line_size;
    } while (--h);
}

static av_unused void DEF(avg, pixels8_l2)(uint8_t *dst, uint8_t *src1, uint8_t *src2, int dstStride, int src1Stride, int h)
{
    MOVQ_BFE(mm6);
    JUMPALIGN();
    do {
        __asm__ volatile(
            "movq  %1, %%mm0            \n\t"
            "movq  %2, %%mm1            \n\t"
            "movq  %0, %%mm3            \n\t"
            PAVGB(%%mm0, %%mm1, %%mm2, %%mm6)
            OP_AVG(%%mm3, %%mm2, %%mm0, %%mm6)
            "movq  %%mm0, %0            \n\t"
            :"+m"(*dst)
            :"m"(*src1), "m"(*src2)
            :"memory");
        dst += dstStride;
        src1 += src1Stride;
        src2 += 8;
    } while (--h);
}

static void DEF(avg, pixels16_x2)(uint8_t *block, const uint8_t *pixels, int line_size, int h)
{
    MOVQ_BFE(mm6);
    JUMPALIGN();
    do {
        __asm__ volatile(
            "movq  %1, %%mm0            \n\t"
            "movq  1%1, %%mm1           \n\t"
            "movq  %0, %%mm3            \n\t"
            PAVGB(%%mm0, %%mm1, %%mm2, %%mm6)
            OP_AVG(%%mm3, %%mm2, %%mm0, %%mm6)
            "movq  %%mm0, %0            \n\t"
            "movq  8%1, %%mm0           \n\t"
            "movq  9%1, %%mm1           \n\t"
            "movq  8%0, %%mm3           \n\t"
            PAVGB(%%mm0, %%mm1, %%mm2, %%mm6)
            OP_AVG(%%mm3, %%mm2, %%mm0, %%mm6)
            "movq  %%mm0, 8%0           \n\t"
            :"+m"(*block)
            :"m"(*pixels)
            :"memory");
        pixels += line_size;
        block += line_size;
    } while (--h);
}

static av_unused void DEF(avg, pixels16_l2)(uint8_t *dst, uint8_t *src1, uint8_t *src2, int dstStride, int src1Stride, int h)
{
    MOVQ_BFE(mm6);
    JUMPALIGN();
    do {
        __asm__ volatile(
            "movq  %1, %%mm0            \n\t"
            "movq  %2, %%mm1            \n\t"
            "movq  %0, %%mm3            \n\t"
            PAVGB(%%mm0, %%mm1, %%mm2, %%mm6)
            OP_AVG(%%mm3, %%mm2, %%mm0, %%mm6)
            "movq  %%mm0, %0            \n\t"
            "movq  8%1, %%mm0           \n\t"
            "movq  8%2, %%mm1           \n\t"
            "movq  8%0, %%mm3           \n\t"
            PAVGB(%%mm0, %%mm1, %%mm2, %%mm6)
            OP_AVG(%%mm3, %%mm2, %%mm0, %%mm6)
            "movq  %%mm0, 8%0           \n\t"
            :"+m"(*dst)
            :"m"(*src1), "m"(*src2)
            :"memory");
        dst += dstStride;
        src1 += src1Stride;
        src2 += 16;
    } while (--h);
}

static void DEF(avg, pixels8_y2)(uint8_t *block, const uint8_t *pixels, int line_size, int h)
{
    MOVQ_BFE(mm6);
    __asm__ volatile(
        "lea    (%3, %3), %%"REG_a"     \n\t"
        "movq   (%1), %%mm0             \n\t"
        ".p2align 3                     \n\t"
        "1:                             \n\t"
        "movq   (%1, %3), %%mm1         \n\t"
        "movq   (%1, %%"REG_a"), %%mm2  \n\t"
        PAVGBP(%%mm1, %%mm0, %%mm4,   %%mm2, %%mm1, %%mm5)
        "movq   (%2), %%mm3             \n\t"
        OP_AVG(%%mm3, %%mm4, %%mm0, %%mm6)
        "movq   (%2, %3), %%mm3         \n\t"
        OP_AVG(%%mm3, %%mm5, %%mm1, %%mm6)
        "movq   %%mm0, (%2)             \n\t"
        "movq   %%mm1, (%2, %3)         \n\t"
        "add    %%"REG_a", %1           \n\t"
        "add    %%"REG_a", %2           \n\t"

        "movq   (%1, %3), %%mm1         \n\t"
        "movq   (%1, %%"REG_a"), %%mm0  \n\t"
        PAVGBP(%%mm1, %%mm2, %%mm4,   %%mm0, %%mm1, %%mm5)
        "movq   (%2), %%mm3             \n\t"
        OP_AVG(%%mm3, %%mm4, %%mm2, %%mm6)
        "movq   (%2, %3), %%mm3         \n\t"
        OP_AVG(%%mm3, %%mm5, %%mm1, %%mm6)
        "movq   %%mm2, (%2)             \n\t"
        "movq   %%mm1, (%2, %3)         \n\t"
        "add    %%"REG_a", %1           \n\t"
        "add    %%"REG_a", %2           \n\t"

        "subl   $4, %0                  \n\t"
        "jnz    1b                      \n\t"
        :"+g"(h), "+S"(pixels), "+D"(block)
        :"r"((x86_reg)line_size)
        :REG_a, "memory");
}

// this routine is 'slightly' suboptimal but mostly unused
static void DEF(avg, pixels8_xy2)(uint8_t *block, const uint8_t *pixels, int line_size, int h)
{
    MOVQ_ZERO(mm7);
    SET_RND(mm6); // =2 for rnd  and  =1 for no_rnd version
    __asm__ volatile(
        "movq   (%1), %%mm0             \n\t"
        "movq   1(%1), %%mm4            \n\t"
        "movq   %%mm0, %%mm1            \n\t"
        "movq   %%mm4, %%mm5            \n\t"
        "punpcklbw %%mm7, %%mm0         \n\t"
        "punpcklbw %%mm7, %%mm4         \n\t"
        "punpckhbw %%mm7, %%mm1         \n\t"
        "punpckhbw %%mm7, %%mm5         \n\t"
        "paddusw %%mm0, %%mm4           \n\t"
        "paddusw %%mm1, %%mm5           \n\t"
        "xor    %%"REG_a", %%"REG_a"    \n\t"
        "add    %3, %1                  \n\t"
        ".p2align 3                     \n\t"
        "1:                             \n\t"
        "movq   (%1, %%"REG_a"), %%mm0  \n\t"
        "movq   1(%1, %%"REG_a"), %%mm2 \n\t"
        "movq   %%mm0, %%mm1            \n\t"
        "movq   %%mm2, %%mm3            \n\t"
        "punpcklbw %%mm7, %%mm0         \n\t"
        "punpcklbw %%mm7, %%mm2         \n\t"
        "punpckhbw %%mm7, %%mm1         \n\t"
        "punpckhbw %%mm7, %%mm3         \n\t"
        "paddusw %%mm2, %%mm0           \n\t"
        "paddusw %%mm3, %%mm1           \n\t"
        "paddusw %%mm6, %%mm4           \n\t"
        "paddusw %%mm6, %%mm5           \n\t"
        "paddusw %%mm0, %%mm4           \n\t"
        "paddusw %%mm1, %%mm5           \n\t"
        "psrlw  $2, %%mm4               \n\t"
        "psrlw  $2, %%mm5               \n\t"
                "movq   (%2, %%"REG_a"), %%mm3  \n\t"
        "packuswb  %%mm5, %%mm4         \n\t"
                "pcmpeqd %%mm2, %%mm2   \n\t"
                "paddb %%mm2, %%mm2     \n\t"
                OP_AVG(%%mm3, %%mm4, %%mm5, %%mm2)
                "movq   %%mm5, (%2, %%"REG_a")  \n\t"
        "add    %3, %%"REG_a"                \n\t"

        "movq   (%1, %%"REG_a"), %%mm2  \n\t" // 0 <-> 2   1 <-> 3
        "movq   1(%1, %%"REG_a"), %%mm4 \n\t"
        "movq   %%mm2, %%mm3            \n\t"
        "movq   %%mm4, %%mm5            \n\t"
        "punpcklbw %%mm7, %%mm2         \n\t"
        "punpcklbw %%mm7, %%mm4         \n\t"
        "punpckhbw %%mm7, %%mm3         \n\t"
        "punpckhbw %%mm7, %%mm5         \n\t"
        "paddusw %%mm2, %%mm4           \n\t"
        "paddusw %%mm3, %%mm5           \n\t"
        "paddusw %%mm6, %%mm0           \n\t"
        "paddusw %%mm6, %%mm1           \n\t"
        "paddusw %%mm4, %%mm0           \n\t"
        "paddusw %%mm5, %%mm1           \n\t"
        "psrlw  $2, %%mm0               \n\t"
        "psrlw  $2, %%mm1               \n\t"
                "movq   (%2, %%"REG_a"), %%mm3  \n\t"
        "packuswb  %%mm1, %%mm0         \n\t"
                "pcmpeqd %%mm2, %%mm2   \n\t"
                "paddb %%mm2, %%mm2     \n\t"
                OP_AVG(%%mm3, %%mm0, %%mm1, %%mm2)
                "movq   %%mm1, (%2, %%"REG_a")  \n\t"
        "add    %3, %%"REG_a"           \n\t"

        "subl   $2, %0                  \n\t"
        "jnz    1b                      \n\t"
        :"+g"(h), "+S"(pixels)
        :"D"(block), "r"((x86_reg)line_size)
        :REG_a, "memory");
}

//FIXME optimize
static void DEF(put, pixels16_y2)(uint8_t *block, const uint8_t *pixels, int line_size, int h){
    DEF(put, pixels8_y2)(block  , pixels  , line_size, h);
    DEF(put, pixels8_y2)(block+8, pixels+8, line_size, h);
}

static void DEF(put, pixels16_xy2)(uint8_t *block, const uint8_t *pixels, int line_size, int h){
    DEF(put, pixels8_xy2)(block  , pixels  , line_size, h);
    DEF(put, pixels8_xy2)(block+8, pixels+8, line_size, h);
}

static void DEF(avg, pixels16_y2)(uint8_t *block, const uint8_t *pixels, int line_size, int h){
    DEF(avg, pixels8_y2)(block  , pixels  , line_size, h);
    DEF(avg, pixels8_y2)(block+8, pixels+8, line_size, h);
}

static void DEF(avg, pixels16_xy2)(uint8_t *block, const uint8_t *pixels, int line_size, int h){
    DEF(avg, pixels8_xy2)(block  , pixels  , line_size, h);
    DEF(avg, pixels8_xy2)(block+8, pixels+8, line_size, h);
}
