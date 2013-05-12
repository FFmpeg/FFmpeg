/*
 * DSP utils mmx functions are compiled twice for rnd/no_rnd
 * Copyright (c) 2000, 2001 Fabrice Bellard
 * Copyright (c) 2003-2004 Michael Niedermayer <michaelni@gmx.at>
 *
 * MMX optimization by Nick Kurshev <nickols_k@mail.ru>
 * mostly rewritten by Michael Niedermayer <michaelni@gmx.at>
 * and improved by Zdenek Kabelac <kabi@users.sf.net>
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

// put_pixels
static void DEF(put, pixels8_x2)(uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h)
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

static void DEF(put, pixels16_x2)(uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h)
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

static void DEF(put, pixels8_y2)(uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h)
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

static void DEF(avg, pixels16_x2)(uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h)
{
    MOVQ_BFE(mm6);
    JUMPALIGN();
    do {
        __asm__ volatile(
            "movq  %1, %%mm0            \n\t"
            "movq  1%1, %%mm1           \n\t"
            "movq  %0, %%mm3            \n\t"
            PAVGB(%%mm0, %%mm1, %%mm2, %%mm6)
            PAVGB_MMX(%%mm3, %%mm2, %%mm0, %%mm6)
            "movq  %%mm0, %0            \n\t"
            "movq  8%1, %%mm0           \n\t"
            "movq  9%1, %%mm1           \n\t"
            "movq  8%0, %%mm3           \n\t"
            PAVGB(%%mm0, %%mm1, %%mm2, %%mm6)
            PAVGB_MMX(%%mm3, %%mm2, %%mm0, %%mm6)
            "movq  %%mm0, 8%0           \n\t"
            :"+m"(*block)
            :"m"(*pixels)
            :"memory");
        pixels += line_size;
        block += line_size;
    } while (--h);
}

static void DEF(avg, pixels8_y2)(uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h)
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
        PAVGB_MMX(%%mm3, %%mm4, %%mm0, %%mm6)
        "movq   (%2, %3), %%mm3         \n\t"
        PAVGB_MMX(%%mm3, %%mm5, %%mm1, %%mm6)
        "movq   %%mm0, (%2)             \n\t"
        "movq   %%mm1, (%2, %3)         \n\t"
        "add    %%"REG_a", %1           \n\t"
        "add    %%"REG_a", %2           \n\t"

        "movq   (%1, %3), %%mm1         \n\t"
        "movq   (%1, %%"REG_a"), %%mm0  \n\t"
        PAVGBP(%%mm1, %%mm2, %%mm4,   %%mm0, %%mm1, %%mm5)
        "movq   (%2), %%mm3             \n\t"
        PAVGB_MMX(%%mm3, %%mm4, %%mm2, %%mm6)
        "movq   (%2, %3), %%mm3         \n\t"
        PAVGB_MMX(%%mm3, %%mm5, %%mm1, %%mm6)
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

//FIXME optimize
static void DEF(put, pixels16_y2)(uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h){
    DEF(put, pixels8_y2)(block  , pixels  , line_size, h);
    DEF(put, pixels8_y2)(block+8, pixels+8, line_size, h);
}

static void DEF(avg, pixels16_y2)(uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h){
    DEF(avg, pixels8_y2)(block  , pixels  , line_size, h);
    DEF(avg, pixels8_y2)(block+8, pixels+8, line_size, h);
}
