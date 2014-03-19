/*
 * MMX optimized motion estimation
 * Copyright (c) 2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer
 *
 * mostly by Michael Niedermayer <michaelni@gmx.at>
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

#include "libavutil/attributes.h"
#include "libavutil/avassert.h"
#include "libavutil/mem.h"
#include "libavutil/x86/asm.h"
#include "libavutil/x86/cpu.h"
#include "dsputil_x86.h"

#if HAVE_INLINE_ASM

DECLARE_ASM_CONST(8, uint64_t, round_tab)[3] = {
    0x0000000000000000ULL,
    0x0001000100010001ULL,
    0x0002000200020002ULL,
};

DECLARE_ASM_CONST(8, uint64_t, bone) = 0x0101010101010101LL;

static inline void sad8_1_mmx(uint8_t *blk1, uint8_t *blk2, int stride, int h)
{
    x86_reg len = -(x86_reg)stride * h;
    __asm__ volatile (
        ".p2align 4                     \n\t"
        "1:                             \n\t"
        "movq (%1, %%"REG_a"), %%mm0    \n\t"
        "movq (%2, %%"REG_a"), %%mm2    \n\t"
        "movq (%2, %%"REG_a"), %%mm4    \n\t"
        "add %3, %%"REG_a"              \n\t"
        "psubusb %%mm0, %%mm2           \n\t"
        "psubusb %%mm4, %%mm0           \n\t"
        "movq (%1, %%"REG_a"), %%mm1    \n\t"
        "movq (%2, %%"REG_a"), %%mm3    \n\t"
        "movq (%2, %%"REG_a"), %%mm5    \n\t"
        "psubusb %%mm1, %%mm3           \n\t"
        "psubusb %%mm5, %%mm1           \n\t"
        "por %%mm2, %%mm0               \n\t"
        "por %%mm1, %%mm3               \n\t"
        "movq %%mm0, %%mm1              \n\t"
        "movq %%mm3, %%mm2              \n\t"
        "punpcklbw %%mm7, %%mm0         \n\t"
        "punpckhbw %%mm7, %%mm1         \n\t"
        "punpcklbw %%mm7, %%mm3         \n\t"
        "punpckhbw %%mm7, %%mm2         \n\t"
        "paddw %%mm1, %%mm0             \n\t"
        "paddw %%mm3, %%mm2             \n\t"
        "paddw %%mm2, %%mm0             \n\t"
        "paddw %%mm0, %%mm6             \n\t"
        "add %3, %%"REG_a"              \n\t"
        " js 1b                         \n\t"
        : "+a" (len)
        : "r" (blk1 - len), "r" (blk2 - len), "r" ((x86_reg) stride));
}

static inline void sad8_1_mmxext(uint8_t *blk1, uint8_t *blk2,
                                 int stride, int h)
{
    __asm__ volatile (
        ".p2align 4                     \n\t"
        "1:                             \n\t"
        "movq (%1), %%mm0               \n\t"
        "movq (%1, %3), %%mm1           \n\t"
        "psadbw (%2), %%mm0             \n\t"
        "psadbw (%2, %3), %%mm1         \n\t"
        "paddw %%mm0, %%mm6             \n\t"
        "paddw %%mm1, %%mm6             \n\t"
        "lea (%1,%3,2), %1              \n\t"
        "lea (%2,%3,2), %2              \n\t"
        "sub $2, %0                     \n\t"
        " jg 1b                         \n\t"
        : "+r" (h), "+r" (blk1), "+r" (blk2)
        : "r" ((x86_reg) stride));
}

static int sad16_sse2(void *v, uint8_t *blk2, uint8_t *blk1, int stride, int h)
{
    int ret;
    __asm__ volatile (
        "pxor %%xmm2, %%xmm2            \n\t"
        ".p2align 4                     \n\t"
        "1:                             \n\t"
        "movdqu (%1), %%xmm0            \n\t"
        "movdqu (%1, %4), %%xmm1        \n\t"
        "psadbw (%2), %%xmm0            \n\t"
        "psadbw (%2, %4), %%xmm1        \n\t"
        "paddw %%xmm0, %%xmm2           \n\t"
        "paddw %%xmm1, %%xmm2           \n\t"
        "lea (%1,%4,2), %1              \n\t"
        "lea (%2,%4,2), %2              \n\t"
        "sub $2, %0                     \n\t"
        " jg 1b                         \n\t"
        "movhlps %%xmm2, %%xmm0         \n\t"
        "paddw   %%xmm0, %%xmm2         \n\t"
        "movd    %%xmm2, %3             \n\t"
        : "+r" (h), "+r" (blk1), "+r" (blk2), "=r" (ret)
        : "r" ((x86_reg) stride));
    return ret;
}

static inline void sad8_x2a_mmxext(uint8_t *blk1, uint8_t *blk2,
                                   int stride, int h)
{
    __asm__ volatile (
        ".p2align 4                     \n\t"
        "1:                             \n\t"
        "movq (%1), %%mm0               \n\t"
        "movq (%1, %3), %%mm1           \n\t"
        "pavgb 1(%1), %%mm0             \n\t"
        "pavgb 1(%1, %3), %%mm1         \n\t"
        "psadbw (%2), %%mm0             \n\t"
        "psadbw (%2, %3), %%mm1         \n\t"
        "paddw %%mm0, %%mm6             \n\t"
        "paddw %%mm1, %%mm6             \n\t"
        "lea (%1,%3,2), %1              \n\t"
        "lea (%2,%3,2), %2              \n\t"
        "sub $2, %0                     \n\t"
        " jg 1b                         \n\t"
        : "+r" (h), "+r" (blk1), "+r" (blk2)
        : "r" ((x86_reg) stride));
}

static inline void sad8_y2a_mmxext(uint8_t *blk1, uint8_t *blk2,
                                   int stride, int h)
{
    __asm__ volatile (
        "movq (%1), %%mm0               \n\t"
        "add %3, %1                     \n\t"
        ".p2align 4                     \n\t"
        "1:                             \n\t"
        "movq (%1), %%mm1               \n\t"
        "movq (%1, %3), %%mm2           \n\t"
        "pavgb %%mm1, %%mm0             \n\t"
        "pavgb %%mm2, %%mm1             \n\t"
        "psadbw (%2), %%mm0             \n\t"
        "psadbw (%2, %3), %%mm1         \n\t"
        "paddw %%mm0, %%mm6             \n\t"
        "paddw %%mm1, %%mm6             \n\t"
        "movq %%mm2, %%mm0              \n\t"
        "lea (%1,%3,2), %1              \n\t"
        "lea (%2,%3,2), %2              \n\t"
        "sub $2, %0                     \n\t"
        " jg 1b                         \n\t"
        : "+r" (h), "+r" (blk1), "+r" (blk2)
        : "r" ((x86_reg) stride));
}

static inline void sad8_4_mmxext(uint8_t *blk1, uint8_t *blk2,
                                 int stride, int h)
{
    __asm__ volatile (
        "movq "MANGLE(bone)", %%mm5     \n\t"
        "movq (%1), %%mm0               \n\t"
        "pavgb 1(%1), %%mm0             \n\t"
        "add %3, %1                     \n\t"
        ".p2align 4                     \n\t"
        "1:                             \n\t"
        "movq (%1), %%mm1               \n\t"
        "movq (%1,%3), %%mm2            \n\t"
        "pavgb 1(%1), %%mm1             \n\t"
        "pavgb 1(%1,%3), %%mm2          \n\t"
        "psubusb %%mm5, %%mm1           \n\t"
        "pavgb %%mm1, %%mm0             \n\t"
        "pavgb %%mm2, %%mm1             \n\t"
        "psadbw (%2), %%mm0             \n\t"
        "psadbw (%2,%3), %%mm1          \n\t"
        "paddw %%mm0, %%mm6             \n\t"
        "paddw %%mm1, %%mm6             \n\t"
        "movq %%mm2, %%mm0              \n\t"
        "lea (%1,%3,2), %1              \n\t"
        "lea (%2,%3,2), %2              \n\t"
        "sub $2, %0                     \n\t"
        " jg 1b                         \n\t"
        : "+r" (h), "+r" (blk1), "+r" (blk2)
        : "r" ((x86_reg) stride)
          NAMED_CONSTRAINTS_ADD(bone));
}

static inline void sad8_2_mmx(uint8_t *blk1a, uint8_t *blk1b, uint8_t *blk2,
                              int stride, int h)
{
    x86_reg len = -(x86_reg)stride * h;
    __asm__ volatile (
        ".p2align 4                     \n\t"
        "1:                             \n\t"
        "movq (%1, %%"REG_a"), %%mm0    \n\t"
        "movq (%2, %%"REG_a"), %%mm1    \n\t"
        "movq (%1, %%"REG_a"), %%mm2    \n\t"
        "movq (%2, %%"REG_a"), %%mm3    \n\t"
        "punpcklbw %%mm7, %%mm0         \n\t"
        "punpcklbw %%mm7, %%mm1         \n\t"
        "punpckhbw %%mm7, %%mm2         \n\t"
        "punpckhbw %%mm7, %%mm3         \n\t"
        "paddw %%mm0, %%mm1             \n\t"
        "paddw %%mm2, %%mm3             \n\t"
        "movq (%3, %%"REG_a"), %%mm4    \n\t"
        "movq (%3, %%"REG_a"), %%mm2    \n\t"
        "paddw %%mm5, %%mm1             \n\t"
        "paddw %%mm5, %%mm3             \n\t"
        "psrlw $1, %%mm1                \n\t"
        "psrlw $1, %%mm3                \n\t"
        "packuswb %%mm3, %%mm1          \n\t"
        "psubusb %%mm1, %%mm4           \n\t"
        "psubusb %%mm2, %%mm1           \n\t"
        "por %%mm4, %%mm1               \n\t"
        "movq %%mm1, %%mm0              \n\t"
        "punpcklbw %%mm7, %%mm0         \n\t"
        "punpckhbw %%mm7, %%mm1         \n\t"
        "paddw %%mm1, %%mm0             \n\t"
        "paddw %%mm0, %%mm6             \n\t"
        "add %4, %%"REG_a"              \n\t"
        " js 1b                         \n\t"
        : "+a" (len)
        : "r" (blk1a - len), "r" (blk1b - len), "r" (blk2 - len),
          "r" ((x86_reg) stride));
}

static inline void sad8_4_mmx(uint8_t *blk1, uint8_t *blk2, int stride, int h)
{
    x86_reg len = -(x86_reg)stride * h;
    __asm__ volatile (
        "movq  (%1, %%"REG_a"), %%mm0   \n\t"
        "movq 1(%1, %%"REG_a"), %%mm2   \n\t"
        "movq %%mm0, %%mm1              \n\t"
        "movq %%mm2, %%mm3              \n\t"
        "punpcklbw %%mm7, %%mm0         \n\t"
        "punpckhbw %%mm7, %%mm1         \n\t"
        "punpcklbw %%mm7, %%mm2         \n\t"
        "punpckhbw %%mm7, %%mm3         \n\t"
        "paddw %%mm2, %%mm0             \n\t"
        "paddw %%mm3, %%mm1             \n\t"
        ".p2align 4                     \n\t"
        "1:                             \n\t"
        "movq  (%2, %%"REG_a"), %%mm2   \n\t"
        "movq 1(%2, %%"REG_a"), %%mm4   \n\t"
        "movq %%mm2, %%mm3              \n\t"
        "movq %%mm4, %%mm5              \n\t"
        "punpcklbw %%mm7, %%mm2         \n\t"
        "punpckhbw %%mm7, %%mm3         \n\t"
        "punpcklbw %%mm7, %%mm4         \n\t"
        "punpckhbw %%mm7, %%mm5         \n\t"
        "paddw %%mm4, %%mm2             \n\t"
        "paddw %%mm5, %%mm3             \n\t"
        "movq 16+"MANGLE(round_tab)", %%mm5 \n\t"
        "paddw %%mm2, %%mm0             \n\t"
        "paddw %%mm3, %%mm1             \n\t"
        "paddw %%mm5, %%mm0             \n\t"
        "paddw %%mm5, %%mm1             \n\t"
        "movq (%3, %%"REG_a"), %%mm4    \n\t"
        "movq (%3, %%"REG_a"), %%mm5    \n\t"
        "psrlw $2, %%mm0                \n\t"
        "psrlw $2, %%mm1                \n\t"
        "packuswb %%mm1, %%mm0          \n\t"
        "psubusb %%mm0, %%mm4           \n\t"
        "psubusb %%mm5, %%mm0           \n\t"
        "por %%mm4, %%mm0               \n\t"
        "movq %%mm0, %%mm4              \n\t"
        "punpcklbw %%mm7, %%mm0         \n\t"
        "punpckhbw %%mm7, %%mm4         \n\t"
        "paddw %%mm0, %%mm6             \n\t"
        "paddw %%mm4, %%mm6             \n\t"
        "movq  %%mm2, %%mm0             \n\t"
        "movq  %%mm3, %%mm1             \n\t"
        "add %4, %%"REG_a"              \n\t"
        " js 1b                         \n\t"
        : "+a" (len)
        : "r" (blk1 - len), "r" (blk1 - len + stride), "r" (blk2 - len),
          "r" ((x86_reg) stride)
          NAMED_CONSTRAINTS_ADD(round_tab));
}

static inline int sum_mmx(void)
{
    int ret;
    __asm__ volatile (
        "movq %%mm6, %%mm0              \n\t"
        "psrlq $32, %%mm6               \n\t"
        "paddw %%mm0, %%mm6             \n\t"
        "movq %%mm6, %%mm0              \n\t"
        "psrlq $16, %%mm6               \n\t"
        "paddw %%mm0, %%mm6             \n\t"
        "movd %%mm6, %0                 \n\t"
        : "=r" (ret));
    return ret & 0xFFFF;
}

static inline int sum_mmxext(void)
{
    int ret;
    __asm__ volatile (
        "movd %%mm6, %0                 \n\t"
        : "=r" (ret));
    return ret;
}

static inline void sad8_x2a_mmx(uint8_t *blk1, uint8_t *blk2, int stride, int h)
{
    sad8_2_mmx(blk1, blk1 + 1, blk2, stride, h);
}

static inline void sad8_y2a_mmx(uint8_t *blk1, uint8_t *blk2, int stride, int h)
{
    sad8_2_mmx(blk1, blk1 + stride, blk2, stride, h);
}

#define PIX_SAD(suf)                                                    \
static int sad8_ ## suf(void *v, uint8_t *blk2,                         \
                        uint8_t *blk1, int stride, int h)               \
{                                                                       \
    av_assert2(h == 8);                                                     \
    __asm__ volatile (                                                  \
        "pxor %%mm7, %%mm7     \n\t"                                    \
        "pxor %%mm6, %%mm6     \n\t"                                    \
        :);                                                             \
                                                                        \
    sad8_1_ ## suf(blk1, blk2, stride, 8);                              \
                                                                        \
    return sum_ ## suf();                                               \
}                                                                       \
                                                                        \
static int sad8_x2_ ## suf(void *v, uint8_t *blk2,                      \
                           uint8_t *blk1, int stride, int h)            \
{                                                                       \
    av_assert2(h == 8);                                                     \
    __asm__ volatile (                                                  \
        "pxor %%mm7, %%mm7     \n\t"                                    \
        "pxor %%mm6, %%mm6     \n\t"                                    \
        "movq %0, %%mm5        \n\t"                                    \
        :: "m" (round_tab[1]));                                         \
                                                                        \
    sad8_x2a_ ## suf(blk1, blk2, stride, 8);                            \
                                                                        \
    return sum_ ## suf();                                               \
}                                                                       \
                                                                        \
static int sad8_y2_ ## suf(void *v, uint8_t *blk2,                      \
                           uint8_t *blk1, int stride, int h)            \
{                                                                       \
    av_assert2(h == 8);                                                     \
    __asm__ volatile (                                                  \
        "pxor %%mm7, %%mm7     \n\t"                                    \
        "pxor %%mm6, %%mm6     \n\t"                                    \
        "movq %0, %%mm5        \n\t"                                    \
        :: "m" (round_tab[1]));                                         \
                                                                        \
    sad8_y2a_ ## suf(blk1, blk2, stride, 8);                            \
                                                                        \
    return sum_ ## suf();                                               \
}                                                                       \
                                                                        \
static int sad8_xy2_ ## suf(void *v, uint8_t *blk2,                     \
                            uint8_t *blk1, int stride, int h)           \
{                                                                       \
    av_assert2(h == 8);                                                     \
    __asm__ volatile (                                                  \
        "pxor %%mm7, %%mm7     \n\t"                                    \
        "pxor %%mm6, %%mm6     \n\t"                                    \
        ::);                                                            \
                                                                        \
    sad8_4_ ## suf(blk1, blk2, stride, 8);                              \
                                                                        \
    return sum_ ## suf();                                               \
}                                                                       \
                                                                        \
static int sad16_ ## suf(void *v, uint8_t *blk2,                        \
                         uint8_t *blk1, int stride, int h)              \
{                                                                       \
    __asm__ volatile (                                                  \
        "pxor %%mm7, %%mm7     \n\t"                                    \
        "pxor %%mm6, %%mm6     \n\t"                                    \
        :);                                                             \
                                                                        \
    sad8_1_ ## suf(blk1,     blk2,     stride, h);                      \
    sad8_1_ ## suf(blk1 + 8, blk2 + 8, stride, h);                      \
                                                                        \
    return sum_ ## suf();                                               \
}                                                                       \
                                                                        \
static int sad16_x2_ ## suf(void *v, uint8_t *blk2,                     \
                            uint8_t *blk1, int stride, int h)           \
{                                                                       \
    __asm__ volatile (                                                  \
        "pxor %%mm7, %%mm7     \n\t"                                    \
        "pxor %%mm6, %%mm6     \n\t"                                    \
        "movq %0, %%mm5        \n\t"                                    \
        :: "m" (round_tab[1]));                                         \
                                                                        \
    sad8_x2a_ ## suf(blk1,     blk2,     stride, h);                    \
    sad8_x2a_ ## suf(blk1 + 8, blk2 + 8, stride, h);                    \
                                                                        \
    return sum_ ## suf();                                               \
}                                                                       \
                                                                        \
static int sad16_y2_ ## suf(void *v, uint8_t *blk2,                     \
                            uint8_t *blk1, int stride, int h)           \
{                                                                       \
    __asm__ volatile (                                                  \
        "pxor %%mm7, %%mm7     \n\t"                                    \
        "pxor %%mm6, %%mm6     \n\t"                                    \
        "movq %0, %%mm5        \n\t"                                    \
        :: "m" (round_tab[1]));                                         \
                                                                        \
    sad8_y2a_ ## suf(blk1,     blk2,     stride, h);                    \
    sad8_y2a_ ## suf(blk1 + 8, blk2 + 8, stride, h);                    \
                                                                        \
    return sum_ ## suf();                                               \
}                                                                       \
                                                                        \
static int sad16_xy2_ ## suf(void *v, uint8_t *blk2,                    \
                             uint8_t *blk1, int stride, int h)          \
{                                                                       \
    __asm__ volatile (                                                  \
        "pxor %%mm7, %%mm7     \n\t"                                    \
        "pxor %%mm6, %%mm6     \n\t"                                    \
        ::);                                                            \
                                                                        \
    sad8_4_ ## suf(blk1,     blk2,     stride, h);                      \
    sad8_4_ ## suf(blk1 + 8, blk2 + 8, stride, h);                      \
                                                                        \
    return sum_ ## suf();                                               \
}                                                                       \

PIX_SAD(mmx)
PIX_SAD(mmxext)

#endif /* HAVE_INLINE_ASM */

av_cold void ff_dsputil_init_pix_mmx(DSPContext *c, AVCodecContext *avctx)
{
#if HAVE_INLINE_ASM
    int cpu_flags = av_get_cpu_flags();

    if (INLINE_MMX(cpu_flags)) {
        c->pix_abs[0][0] = sad16_mmx;
        c->pix_abs[0][1] = sad16_x2_mmx;
        c->pix_abs[0][2] = sad16_y2_mmx;
        c->pix_abs[0][3] = sad16_xy2_mmx;
        c->pix_abs[1][0] = sad8_mmx;
        c->pix_abs[1][1] = sad8_x2_mmx;
        c->pix_abs[1][2] = sad8_y2_mmx;
        c->pix_abs[1][3] = sad8_xy2_mmx;

        c->sad[0] = sad16_mmx;
        c->sad[1] = sad8_mmx;
    }
    if (INLINE_MMXEXT(cpu_flags)) {
        c->pix_abs[0][0] = sad16_mmxext;
        c->pix_abs[1][0] = sad8_mmxext;

        c->sad[0] = sad16_mmxext;
        c->sad[1] = sad8_mmxext;

        if (!(avctx->flags & CODEC_FLAG_BITEXACT)) {
            c->pix_abs[0][1] = sad16_x2_mmxext;
            c->pix_abs[0][2] = sad16_y2_mmxext;
            c->pix_abs[0][3] = sad16_xy2_mmxext;
            c->pix_abs[1][1] = sad8_x2_mmxext;
            c->pix_abs[1][2] = sad8_y2_mmxext;
            c->pix_abs[1][3] = sad8_xy2_mmxext;
        }
    }
    if (INLINE_SSE2(cpu_flags) && !(cpu_flags & AV_CPU_FLAG_3DNOW) && avctx->codec_id != AV_CODEC_ID_SNOW) {
        c->sad[0] = sad16_sse2;
    }
#endif /* HAVE_INLINE_ASM */
}
