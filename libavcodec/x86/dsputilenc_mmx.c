/*
 * MMX optimized DSP utils
 * Copyright (c) 2000, 2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
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
 *
 * MMX optimization by Nick Kurshev <nickols_k@mail.ru>
 */

#include "libavutil/x86_cpu.h"
#include "libavcodec/dsputil.h"
#include "libavcodec/mpegvideo.h"
#include "libavcodec/mathops.h"
#include "dsputil_mmx.h"


static void get_pixels_mmx(DCTELEM *block, const uint8_t *pixels, int line_size)
{
    __asm__ volatile(
        "mov $-128, %%"REG_a"           \n\t"
        "pxor %%mm7, %%mm7              \n\t"
        ASMALIGN(4)
        "1:                             \n\t"
        "movq (%0), %%mm0               \n\t"
        "movq (%0, %2), %%mm2           \n\t"
        "movq %%mm0, %%mm1              \n\t"
        "movq %%mm2, %%mm3              \n\t"
        "punpcklbw %%mm7, %%mm0         \n\t"
        "punpckhbw %%mm7, %%mm1         \n\t"
        "punpcklbw %%mm7, %%mm2         \n\t"
        "punpckhbw %%mm7, %%mm3         \n\t"
        "movq %%mm0, (%1, %%"REG_a")    \n\t"
        "movq %%mm1, 8(%1, %%"REG_a")   \n\t"
        "movq %%mm2, 16(%1, %%"REG_a")  \n\t"
        "movq %%mm3, 24(%1, %%"REG_a")  \n\t"
        "add %3, %0                     \n\t"
        "add $32, %%"REG_a"             \n\t"
        "js 1b                          \n\t"
        : "+r" (pixels)
        : "r" (block+64), "r" ((x86_reg)line_size), "r" ((x86_reg)line_size*2)
        : "%"REG_a
    );
}

static void get_pixels_sse2(DCTELEM *block, const uint8_t *pixels, int line_size)
{
    __asm__ volatile(
        "pxor %%xmm7,      %%xmm7         \n\t"
        "movq (%0),        %%xmm0         \n\t"
        "movq (%0, %2),    %%xmm1         \n\t"
        "movq (%0, %2,2),  %%xmm2         \n\t"
        "movq (%0, %3),    %%xmm3         \n\t"
        "lea (%0,%2,4), %0                \n\t"
        "punpcklbw %%xmm7, %%xmm0         \n\t"
        "punpcklbw %%xmm7, %%xmm1         \n\t"
        "punpcklbw %%xmm7, %%xmm2         \n\t"
        "punpcklbw %%xmm7, %%xmm3         \n\t"
        "movdqa %%xmm0,      (%1)         \n\t"
        "movdqa %%xmm1,    16(%1)         \n\t"
        "movdqa %%xmm2,    32(%1)         \n\t"
        "movdqa %%xmm3,    48(%1)         \n\t"
        "movq (%0),        %%xmm0         \n\t"
        "movq (%0, %2),    %%xmm1         \n\t"
        "movq (%0, %2,2),  %%xmm2         \n\t"
        "movq (%0, %3),    %%xmm3         \n\t"
        "punpcklbw %%xmm7, %%xmm0         \n\t"
        "punpcklbw %%xmm7, %%xmm1         \n\t"
        "punpcklbw %%xmm7, %%xmm2         \n\t"
        "punpcklbw %%xmm7, %%xmm3         \n\t"
        "movdqa %%xmm0,    64(%1)         \n\t"
        "movdqa %%xmm1,    80(%1)         \n\t"
        "movdqa %%xmm2,    96(%1)         \n\t"
        "movdqa %%xmm3,   112(%1)         \n\t"
        : "+r" (pixels)
        : "r" (block), "r" ((x86_reg)line_size), "r" ((x86_reg)line_size*3)
    );
}

static inline void diff_pixels_mmx(DCTELEM *block, const uint8_t *s1, const uint8_t *s2, int stride)
{
    __asm__ volatile(
        "pxor %%mm7, %%mm7              \n\t"
        "mov $-128, %%"REG_a"           \n\t"
        ASMALIGN(4)
        "1:                             \n\t"
        "movq (%0), %%mm0               \n\t"
        "movq (%1), %%mm2               \n\t"
        "movq %%mm0, %%mm1              \n\t"
        "movq %%mm2, %%mm3              \n\t"
        "punpcklbw %%mm7, %%mm0         \n\t"
        "punpckhbw %%mm7, %%mm1         \n\t"
        "punpcklbw %%mm7, %%mm2         \n\t"
        "punpckhbw %%mm7, %%mm3         \n\t"
        "psubw %%mm2, %%mm0             \n\t"
        "psubw %%mm3, %%mm1             \n\t"
        "movq %%mm0, (%2, %%"REG_a")    \n\t"
        "movq %%mm1, 8(%2, %%"REG_a")   \n\t"
        "add %3, %0                     \n\t"
        "add %3, %1                     \n\t"
        "add $16, %%"REG_a"             \n\t"
        "jnz 1b                         \n\t"
        : "+r" (s1), "+r" (s2)
        : "r" (block+64), "r" ((x86_reg)stride)
        : "%"REG_a
    );
}

static int pix_sum16_mmx(uint8_t * pix, int line_size){
    const int h=16;
    int sum;
    x86_reg index= -line_size*h;

    __asm__ volatile(
                "pxor %%mm7, %%mm7              \n\t"
                "pxor %%mm6, %%mm6              \n\t"
                "1:                             \n\t"
                "movq (%2, %1), %%mm0           \n\t"
                "movq (%2, %1), %%mm1           \n\t"
                "movq 8(%2, %1), %%mm2          \n\t"
                "movq 8(%2, %1), %%mm3          \n\t"
                "punpcklbw %%mm7, %%mm0         \n\t"
                "punpckhbw %%mm7, %%mm1         \n\t"
                "punpcklbw %%mm7, %%mm2         \n\t"
                "punpckhbw %%mm7, %%mm3         \n\t"
                "paddw %%mm0, %%mm1             \n\t"
                "paddw %%mm2, %%mm3             \n\t"
                "paddw %%mm1, %%mm3             \n\t"
                "paddw %%mm3, %%mm6             \n\t"
                "add %3, %1                     \n\t"
                " js 1b                         \n\t"
                "movq %%mm6, %%mm5              \n\t"
                "psrlq $32, %%mm6               \n\t"
                "paddw %%mm5, %%mm6             \n\t"
                "movq %%mm6, %%mm5              \n\t"
                "psrlq $16, %%mm6               \n\t"
                "paddw %%mm5, %%mm6             \n\t"
                "movd %%mm6, %0                 \n\t"
                "andl $0xFFFF, %0               \n\t"
                : "=&r" (sum), "+r" (index)
                : "r" (pix - index), "r" ((x86_reg)line_size)
        );

        return sum;
}

static int pix_norm1_mmx(uint8_t *pix, int line_size) {
    int tmp;
  __asm__ volatile (
      "movl $16,%%ecx\n"
      "pxor %%mm0,%%mm0\n"
      "pxor %%mm7,%%mm7\n"
      "1:\n"
      "movq (%0),%%mm2\n"       /* mm2 = pix[0-7] */
      "movq 8(%0),%%mm3\n"      /* mm3 = pix[8-15] */

      "movq %%mm2,%%mm1\n"      /* mm1 = mm2 = pix[0-7] */

      "punpckhbw %%mm0,%%mm1\n" /* mm1 = [pix4-7] */
      "punpcklbw %%mm0,%%mm2\n" /* mm2 = [pix0-3] */

      "movq %%mm3,%%mm4\n"      /* mm4 = mm3 = pix[8-15] */
      "punpckhbw %%mm0,%%mm3\n" /* mm3 = [pix12-15] */
      "punpcklbw %%mm0,%%mm4\n" /* mm4 = [pix8-11] */

      "pmaddwd %%mm1,%%mm1\n"   /* mm1 = (pix0^2+pix1^2,pix2^2+pix3^2) */
      "pmaddwd %%mm2,%%mm2\n"   /* mm2 = (pix4^2+pix5^2,pix6^2+pix7^2) */

      "pmaddwd %%mm3,%%mm3\n"
      "pmaddwd %%mm4,%%mm4\n"

      "paddd %%mm1,%%mm2\n"     /* mm2 = (pix0^2+pix1^2+pix4^2+pix5^2,
                                          pix2^2+pix3^2+pix6^2+pix7^2) */
      "paddd %%mm3,%%mm4\n"
      "paddd %%mm2,%%mm7\n"

      "add %2, %0\n"
      "paddd %%mm4,%%mm7\n"
      "dec %%ecx\n"
      "jnz 1b\n"

      "movq %%mm7,%%mm1\n"
      "psrlq $32, %%mm7\n"      /* shift hi dword to lo */
      "paddd %%mm7,%%mm1\n"
      "movd %%mm1,%1\n"
      : "+r" (pix), "=r"(tmp) : "r" ((x86_reg)line_size) : "%ecx" );
    return tmp;
}

static int sse8_mmx(void *v, uint8_t * pix1, uint8_t * pix2, int line_size, int h) {
    int tmp;
  __asm__ volatile (
      "movl %4,%%ecx\n"
      "shr $1,%%ecx\n"
      "pxor %%mm0,%%mm0\n"      /* mm0 = 0 */
      "pxor %%mm7,%%mm7\n"      /* mm7 holds the sum */
      "1:\n"
      "movq (%0),%%mm1\n"       /* mm1 = pix1[0][0-7] */
      "movq (%1),%%mm2\n"       /* mm2 = pix2[0][0-7] */
      "movq (%0,%3),%%mm3\n"    /* mm3 = pix1[1][0-7] */
      "movq (%1,%3),%%mm4\n"    /* mm4 = pix2[1][0-7] */

      /* todo: mm1-mm2, mm3-mm4 */
      /* algo: subtract mm1 from mm2 with saturation and vice versa */
      /*       OR the results to get absolute difference */
      "movq %%mm1,%%mm5\n"
      "movq %%mm3,%%mm6\n"
      "psubusb %%mm2,%%mm1\n"
      "psubusb %%mm4,%%mm3\n"
      "psubusb %%mm5,%%mm2\n"
      "psubusb %%mm6,%%mm4\n"

      "por %%mm1,%%mm2\n"
      "por %%mm3,%%mm4\n"

      /* now convert to 16-bit vectors so we can square them */
      "movq %%mm2,%%mm1\n"
      "movq %%mm4,%%mm3\n"

      "punpckhbw %%mm0,%%mm2\n"
      "punpckhbw %%mm0,%%mm4\n"
      "punpcklbw %%mm0,%%mm1\n" /* mm1 now spread over (mm1,mm2) */
      "punpcklbw %%mm0,%%mm3\n" /* mm4 now spread over (mm3,mm4) */

      "pmaddwd %%mm2,%%mm2\n"
      "pmaddwd %%mm4,%%mm4\n"
      "pmaddwd %%mm1,%%mm1\n"
      "pmaddwd %%mm3,%%mm3\n"

      "lea (%0,%3,2), %0\n"     /* pix1 += 2*line_size */
      "lea (%1,%3,2), %1\n"     /* pix2 += 2*line_size */

      "paddd %%mm2,%%mm1\n"
      "paddd %%mm4,%%mm3\n"
      "paddd %%mm1,%%mm7\n"
      "paddd %%mm3,%%mm7\n"

      "decl %%ecx\n"
      "jnz 1b\n"

      "movq %%mm7,%%mm1\n"
      "psrlq $32, %%mm7\n"      /* shift hi dword to lo */
      "paddd %%mm7,%%mm1\n"
      "movd %%mm1,%2\n"
      : "+r" (pix1), "+r" (pix2), "=r"(tmp)
      : "r" ((x86_reg)line_size) , "m" (h)
      : "%ecx");
    return tmp;
}

static int sse16_mmx(void *v, uint8_t * pix1, uint8_t * pix2, int line_size, int h) {
    int tmp;
  __asm__ volatile (
      "movl %4,%%ecx\n"
      "pxor %%mm0,%%mm0\n"      /* mm0 = 0 */
      "pxor %%mm7,%%mm7\n"      /* mm7 holds the sum */
      "1:\n"
      "movq (%0),%%mm1\n"       /* mm1 = pix1[0-7] */
      "movq (%1),%%mm2\n"       /* mm2 = pix2[0-7] */
      "movq 8(%0),%%mm3\n"      /* mm3 = pix1[8-15] */
      "movq 8(%1),%%mm4\n"      /* mm4 = pix2[8-15] */

      /* todo: mm1-mm2, mm3-mm4 */
      /* algo: subtract mm1 from mm2 with saturation and vice versa */
      /*       OR the results to get absolute difference */
      "movq %%mm1,%%mm5\n"
      "movq %%mm3,%%mm6\n"
      "psubusb %%mm2,%%mm1\n"
      "psubusb %%mm4,%%mm3\n"
      "psubusb %%mm5,%%mm2\n"
      "psubusb %%mm6,%%mm4\n"

      "por %%mm1,%%mm2\n"
      "por %%mm3,%%mm4\n"

      /* now convert to 16-bit vectors so we can square them */
      "movq %%mm2,%%mm1\n"
      "movq %%mm4,%%mm3\n"

      "punpckhbw %%mm0,%%mm2\n"
      "punpckhbw %%mm0,%%mm4\n"
      "punpcklbw %%mm0,%%mm1\n" /* mm1 now spread over (mm1,mm2) */
      "punpcklbw %%mm0,%%mm3\n" /* mm4 now spread over (mm3,mm4) */

      "pmaddwd %%mm2,%%mm2\n"
      "pmaddwd %%mm4,%%mm4\n"
      "pmaddwd %%mm1,%%mm1\n"
      "pmaddwd %%mm3,%%mm3\n"

      "add %3,%0\n"
      "add %3,%1\n"

      "paddd %%mm2,%%mm1\n"
      "paddd %%mm4,%%mm3\n"
      "paddd %%mm1,%%mm7\n"
      "paddd %%mm3,%%mm7\n"

      "decl %%ecx\n"
      "jnz 1b\n"

      "movq %%mm7,%%mm1\n"
      "psrlq $32, %%mm7\n"      /* shift hi dword to lo */
      "paddd %%mm7,%%mm1\n"
      "movd %%mm1,%2\n"
      : "+r" (pix1), "+r" (pix2), "=r"(tmp)
      : "r" ((x86_reg)line_size) , "m" (h)
      : "%ecx");
    return tmp;
}

static int sse16_sse2(void *v, uint8_t * pix1, uint8_t * pix2, int line_size, int h) {
    int tmp;
  __asm__ volatile (
      "shr $1,%2\n"
      "pxor %%xmm0,%%xmm0\n"    /* mm0 = 0 */
      "pxor %%xmm7,%%xmm7\n"    /* mm7 holds the sum */
      "1:\n"
      "movdqu (%0),%%xmm1\n"    /* mm1 = pix1[0][0-15] */
      "movdqu (%1),%%xmm2\n"    /* mm2 = pix2[0][0-15] */
      "movdqu (%0,%4),%%xmm3\n" /* mm3 = pix1[1][0-15] */
      "movdqu (%1,%4),%%xmm4\n" /* mm4 = pix2[1][0-15] */

      /* todo: mm1-mm2, mm3-mm4 */
      /* algo: subtract mm1 from mm2 with saturation and vice versa */
      /*       OR the results to get absolute difference */
      "movdqa %%xmm1,%%xmm5\n"
      "movdqa %%xmm3,%%xmm6\n"
      "psubusb %%xmm2,%%xmm1\n"
      "psubusb %%xmm4,%%xmm3\n"
      "psubusb %%xmm5,%%xmm2\n"
      "psubusb %%xmm6,%%xmm4\n"

      "por %%xmm1,%%xmm2\n"
      "por %%xmm3,%%xmm4\n"

      /* now convert to 16-bit vectors so we can square them */
      "movdqa %%xmm2,%%xmm1\n"
      "movdqa %%xmm4,%%xmm3\n"

      "punpckhbw %%xmm0,%%xmm2\n"
      "punpckhbw %%xmm0,%%xmm4\n"
      "punpcklbw %%xmm0,%%xmm1\n"  /* mm1 now spread over (mm1,mm2) */
      "punpcklbw %%xmm0,%%xmm3\n"  /* mm4 now spread over (mm3,mm4) */

      "pmaddwd %%xmm2,%%xmm2\n"
      "pmaddwd %%xmm4,%%xmm4\n"
      "pmaddwd %%xmm1,%%xmm1\n"
      "pmaddwd %%xmm3,%%xmm3\n"

      "lea (%0,%4,2), %0\n"        /* pix1 += 2*line_size */
      "lea (%1,%4,2), %1\n"        /* pix2 += 2*line_size */

      "paddd %%xmm2,%%xmm1\n"
      "paddd %%xmm4,%%xmm3\n"
      "paddd %%xmm1,%%xmm7\n"
      "paddd %%xmm3,%%xmm7\n"

      "decl %2\n"
      "jnz 1b\n"

      "movdqa %%xmm7,%%xmm1\n"
      "psrldq $8, %%xmm7\n"        /* shift hi qword to lo */
      "paddd %%xmm1,%%xmm7\n"
      "movdqa %%xmm7,%%xmm1\n"
      "psrldq $4, %%xmm7\n"        /* shift hi dword to lo */
      "paddd %%xmm1,%%xmm7\n"
      "movd %%xmm7,%3\n"
      : "+r" (pix1), "+r" (pix2), "+r"(h), "=r"(tmp)
      : "r" ((x86_reg)line_size));
    return tmp;
}

static int hf_noise8_mmx(uint8_t * pix1, int line_size, int h) {
    int tmp;
  __asm__ volatile (
      "movl %3,%%ecx\n"
      "pxor %%mm7,%%mm7\n"
      "pxor %%mm6,%%mm6\n"

      "movq (%0),%%mm0\n"
      "movq %%mm0, %%mm1\n"
      "psllq $8, %%mm0\n"
      "psrlq $8, %%mm1\n"
      "psrlq $8, %%mm0\n"
      "movq %%mm0, %%mm2\n"
      "movq %%mm1, %%mm3\n"
      "punpcklbw %%mm7,%%mm0\n"
      "punpcklbw %%mm7,%%mm1\n"
      "punpckhbw %%mm7,%%mm2\n"
      "punpckhbw %%mm7,%%mm3\n"
      "psubw %%mm1, %%mm0\n"
      "psubw %%mm3, %%mm2\n"

      "add %2,%0\n"

      "movq (%0),%%mm4\n"
      "movq %%mm4, %%mm1\n"
      "psllq $8, %%mm4\n"
      "psrlq $8, %%mm1\n"
      "psrlq $8, %%mm4\n"
      "movq %%mm4, %%mm5\n"
      "movq %%mm1, %%mm3\n"
      "punpcklbw %%mm7,%%mm4\n"
      "punpcklbw %%mm7,%%mm1\n"
      "punpckhbw %%mm7,%%mm5\n"
      "punpckhbw %%mm7,%%mm3\n"
      "psubw %%mm1, %%mm4\n"
      "psubw %%mm3, %%mm5\n"
      "psubw %%mm4, %%mm0\n"
      "psubw %%mm5, %%mm2\n"
      "pxor %%mm3, %%mm3\n"
      "pxor %%mm1, %%mm1\n"
      "pcmpgtw %%mm0, %%mm3\n\t"
      "pcmpgtw %%mm2, %%mm1\n\t"
      "pxor %%mm3, %%mm0\n"
      "pxor %%mm1, %%mm2\n"
      "psubw %%mm3, %%mm0\n"
      "psubw %%mm1, %%mm2\n"
      "paddw %%mm0, %%mm2\n"
      "paddw %%mm2, %%mm6\n"

      "add %2,%0\n"
      "1:\n"

      "movq (%0),%%mm0\n"
      "movq %%mm0, %%mm1\n"
      "psllq $8, %%mm0\n"
      "psrlq $8, %%mm1\n"
      "psrlq $8, %%mm0\n"
      "movq %%mm0, %%mm2\n"
      "movq %%mm1, %%mm3\n"
      "punpcklbw %%mm7,%%mm0\n"
      "punpcklbw %%mm7,%%mm1\n"
      "punpckhbw %%mm7,%%mm2\n"
      "punpckhbw %%mm7,%%mm3\n"
      "psubw %%mm1, %%mm0\n"
      "psubw %%mm3, %%mm2\n"
      "psubw %%mm0, %%mm4\n"
      "psubw %%mm2, %%mm5\n"
      "pxor %%mm3, %%mm3\n"
      "pxor %%mm1, %%mm1\n"
      "pcmpgtw %%mm4, %%mm3\n\t"
      "pcmpgtw %%mm5, %%mm1\n\t"
      "pxor %%mm3, %%mm4\n"
      "pxor %%mm1, %%mm5\n"
      "psubw %%mm3, %%mm4\n"
      "psubw %%mm1, %%mm5\n"
      "paddw %%mm4, %%mm5\n"
      "paddw %%mm5, %%mm6\n"

      "add %2,%0\n"

      "movq (%0),%%mm4\n"
      "movq %%mm4, %%mm1\n"
      "psllq $8, %%mm4\n"
      "psrlq $8, %%mm1\n"
      "psrlq $8, %%mm4\n"
      "movq %%mm4, %%mm5\n"
      "movq %%mm1, %%mm3\n"
      "punpcklbw %%mm7,%%mm4\n"
      "punpcklbw %%mm7,%%mm1\n"
      "punpckhbw %%mm7,%%mm5\n"
      "punpckhbw %%mm7,%%mm3\n"
      "psubw %%mm1, %%mm4\n"
      "psubw %%mm3, %%mm5\n"
      "psubw %%mm4, %%mm0\n"
      "psubw %%mm5, %%mm2\n"
      "pxor %%mm3, %%mm3\n"
      "pxor %%mm1, %%mm1\n"
      "pcmpgtw %%mm0, %%mm3\n\t"
      "pcmpgtw %%mm2, %%mm1\n\t"
      "pxor %%mm3, %%mm0\n"
      "pxor %%mm1, %%mm2\n"
      "psubw %%mm3, %%mm0\n"
      "psubw %%mm1, %%mm2\n"
      "paddw %%mm0, %%mm2\n"
      "paddw %%mm2, %%mm6\n"

      "add %2,%0\n"
      "subl $2, %%ecx\n"
      " jnz 1b\n"

      "movq %%mm6, %%mm0\n"
      "punpcklwd %%mm7,%%mm0\n"
      "punpckhwd %%mm7,%%mm6\n"
      "paddd %%mm0, %%mm6\n"

      "movq %%mm6,%%mm0\n"
      "psrlq $32, %%mm6\n"
      "paddd %%mm6,%%mm0\n"
      "movd %%mm0,%1\n"
      : "+r" (pix1), "=r"(tmp)
      : "r" ((x86_reg)line_size) , "g" (h-2)
      : "%ecx");
      return tmp;
}

static int hf_noise16_mmx(uint8_t * pix1, int line_size, int h) {
    int tmp;
    uint8_t * pix= pix1;
  __asm__ volatile (
      "movl %3,%%ecx\n"
      "pxor %%mm7,%%mm7\n"
      "pxor %%mm6,%%mm6\n"

      "movq (%0),%%mm0\n"
      "movq 1(%0),%%mm1\n"
      "movq %%mm0, %%mm2\n"
      "movq %%mm1, %%mm3\n"
      "punpcklbw %%mm7,%%mm0\n"
      "punpcklbw %%mm7,%%mm1\n"
      "punpckhbw %%mm7,%%mm2\n"
      "punpckhbw %%mm7,%%mm3\n"
      "psubw %%mm1, %%mm0\n"
      "psubw %%mm3, %%mm2\n"

      "add %2,%0\n"

      "movq (%0),%%mm4\n"
      "movq 1(%0),%%mm1\n"
      "movq %%mm4, %%mm5\n"
      "movq %%mm1, %%mm3\n"
      "punpcklbw %%mm7,%%mm4\n"
      "punpcklbw %%mm7,%%mm1\n"
      "punpckhbw %%mm7,%%mm5\n"
      "punpckhbw %%mm7,%%mm3\n"
      "psubw %%mm1, %%mm4\n"
      "psubw %%mm3, %%mm5\n"
      "psubw %%mm4, %%mm0\n"
      "psubw %%mm5, %%mm2\n"
      "pxor %%mm3, %%mm3\n"
      "pxor %%mm1, %%mm1\n"
      "pcmpgtw %%mm0, %%mm3\n\t"
      "pcmpgtw %%mm2, %%mm1\n\t"
      "pxor %%mm3, %%mm0\n"
      "pxor %%mm1, %%mm2\n"
      "psubw %%mm3, %%mm0\n"
      "psubw %%mm1, %%mm2\n"
      "paddw %%mm0, %%mm2\n"
      "paddw %%mm2, %%mm6\n"

      "add %2,%0\n"
      "1:\n"

      "movq (%0),%%mm0\n"
      "movq 1(%0),%%mm1\n"
      "movq %%mm0, %%mm2\n"
      "movq %%mm1, %%mm3\n"
      "punpcklbw %%mm7,%%mm0\n"
      "punpcklbw %%mm7,%%mm1\n"
      "punpckhbw %%mm7,%%mm2\n"
      "punpckhbw %%mm7,%%mm3\n"
      "psubw %%mm1, %%mm0\n"
      "psubw %%mm3, %%mm2\n"
      "psubw %%mm0, %%mm4\n"
      "psubw %%mm2, %%mm5\n"
      "pxor %%mm3, %%mm3\n"
      "pxor %%mm1, %%mm1\n"
      "pcmpgtw %%mm4, %%mm3\n\t"
      "pcmpgtw %%mm5, %%mm1\n\t"
      "pxor %%mm3, %%mm4\n"
      "pxor %%mm1, %%mm5\n"
      "psubw %%mm3, %%mm4\n"
      "psubw %%mm1, %%mm5\n"
      "paddw %%mm4, %%mm5\n"
      "paddw %%mm5, %%mm6\n"

      "add %2,%0\n"

      "movq (%0),%%mm4\n"
      "movq 1(%0),%%mm1\n"
      "movq %%mm4, %%mm5\n"
      "movq %%mm1, %%mm3\n"
      "punpcklbw %%mm7,%%mm4\n"
      "punpcklbw %%mm7,%%mm1\n"
      "punpckhbw %%mm7,%%mm5\n"
      "punpckhbw %%mm7,%%mm3\n"
      "psubw %%mm1, %%mm4\n"
      "psubw %%mm3, %%mm5\n"
      "psubw %%mm4, %%mm0\n"
      "psubw %%mm5, %%mm2\n"
      "pxor %%mm3, %%mm3\n"
      "pxor %%mm1, %%mm1\n"
      "pcmpgtw %%mm0, %%mm3\n\t"
      "pcmpgtw %%mm2, %%mm1\n\t"
      "pxor %%mm3, %%mm0\n"
      "pxor %%mm1, %%mm2\n"
      "psubw %%mm3, %%mm0\n"
      "psubw %%mm1, %%mm2\n"
      "paddw %%mm0, %%mm2\n"
      "paddw %%mm2, %%mm6\n"

      "add %2,%0\n"
      "subl $2, %%ecx\n"
      " jnz 1b\n"

      "movq %%mm6, %%mm0\n"
      "punpcklwd %%mm7,%%mm0\n"
      "punpckhwd %%mm7,%%mm6\n"
      "paddd %%mm0, %%mm6\n"

      "movq %%mm6,%%mm0\n"
      "psrlq $32, %%mm6\n"
      "paddd %%mm6,%%mm0\n"
      "movd %%mm0,%1\n"
      : "+r" (pix1), "=r"(tmp)
      : "r" ((x86_reg)line_size) , "g" (h-2)
      : "%ecx");
      return tmp + hf_noise8_mmx(pix+8, line_size, h);
}

static int nsse16_mmx(void *p, uint8_t * pix1, uint8_t * pix2, int line_size, int h) {
    MpegEncContext *c = p;
    int score1, score2;

    if(c) score1 = c->dsp.sse[0](c, pix1, pix2, line_size, h);
    else  score1 = sse16_mmx(c, pix1, pix2, line_size, h);
    score2= hf_noise16_mmx(pix1, line_size, h) - hf_noise16_mmx(pix2, line_size, h);

    if(c) return score1 + FFABS(score2)*c->avctx->nsse_weight;
    else  return score1 + FFABS(score2)*8;
}

static int nsse8_mmx(void *p, uint8_t * pix1, uint8_t * pix2, int line_size, int h) {
    MpegEncContext *c = p;
    int score1= sse8_mmx(c, pix1, pix2, line_size, h);
    int score2= hf_noise8_mmx(pix1, line_size, h) - hf_noise8_mmx(pix2, line_size, h);

    if(c) return score1 + FFABS(score2)*c->avctx->nsse_weight;
    else  return score1 + FFABS(score2)*8;
}

static int vsad_intra16_mmx(void *v, uint8_t * pix, uint8_t * dummy, int line_size, int h) {
    int tmp;

    assert( (((int)pix) & 7) == 0);
    assert((line_size &7) ==0);

#define SUM(in0, in1, out0, out1) \
      "movq (%0), %%mm2\n"\
      "movq 8(%0), %%mm3\n"\
      "add %2,%0\n"\
      "movq %%mm2, " #out0 "\n"\
      "movq %%mm3, " #out1 "\n"\
      "psubusb " #in0 ", %%mm2\n"\
      "psubusb " #in1 ", %%mm3\n"\
      "psubusb " #out0 ", " #in0 "\n"\
      "psubusb " #out1 ", " #in1 "\n"\
      "por %%mm2, " #in0 "\n"\
      "por %%mm3, " #in1 "\n"\
      "movq " #in0 ", %%mm2\n"\
      "movq " #in1 ", %%mm3\n"\
      "punpcklbw %%mm7, " #in0 "\n"\
      "punpcklbw %%mm7, " #in1 "\n"\
      "punpckhbw %%mm7, %%mm2\n"\
      "punpckhbw %%mm7, %%mm3\n"\
      "paddw " #in1 ", " #in0 "\n"\
      "paddw %%mm3, %%mm2\n"\
      "paddw %%mm2, " #in0 "\n"\
      "paddw " #in0 ", %%mm6\n"


  __asm__ volatile (
      "movl %3,%%ecx\n"
      "pxor %%mm6,%%mm6\n"
      "pxor %%mm7,%%mm7\n"
      "movq (%0),%%mm0\n"
      "movq 8(%0),%%mm1\n"
      "add %2,%0\n"
      "jmp 2f\n"
      "1:\n"

      SUM(%%mm4, %%mm5, %%mm0, %%mm1)
      "2:\n"
      SUM(%%mm0, %%mm1, %%mm4, %%mm5)

      "subl $2, %%ecx\n"
      "jnz 1b\n"

      "movq %%mm6,%%mm0\n"
      "psrlq $32, %%mm6\n"
      "paddw %%mm6,%%mm0\n"
      "movq %%mm0,%%mm6\n"
      "psrlq $16, %%mm0\n"
      "paddw %%mm6,%%mm0\n"
      "movd %%mm0,%1\n"
      : "+r" (pix), "=r"(tmp)
      : "r" ((x86_reg)line_size) , "m" (h)
      : "%ecx");
    return tmp & 0xFFFF;
}
#undef SUM

static int vsad_intra16_mmx2(void *v, uint8_t * pix, uint8_t * dummy, int line_size, int h) {
    int tmp;

    assert( (((int)pix) & 7) == 0);
    assert((line_size &7) ==0);

#define SUM(in0, in1, out0, out1) \
      "movq (%0), " #out0 "\n"\
      "movq 8(%0), " #out1 "\n"\
      "add %2,%0\n"\
      "psadbw " #out0 ", " #in0 "\n"\
      "psadbw " #out1 ", " #in1 "\n"\
      "paddw " #in1 ", " #in0 "\n"\
      "paddw " #in0 ", %%mm6\n"

  __asm__ volatile (
      "movl %3,%%ecx\n"
      "pxor %%mm6,%%mm6\n"
      "pxor %%mm7,%%mm7\n"
      "movq (%0),%%mm0\n"
      "movq 8(%0),%%mm1\n"
      "add %2,%0\n"
      "jmp 2f\n"
      "1:\n"

      SUM(%%mm4, %%mm5, %%mm0, %%mm1)
      "2:\n"
      SUM(%%mm0, %%mm1, %%mm4, %%mm5)

      "subl $2, %%ecx\n"
      "jnz 1b\n"

      "movd %%mm6,%1\n"
      : "+r" (pix), "=r"(tmp)
      : "r" ((x86_reg)line_size) , "m" (h)
      : "%ecx");
    return tmp;
}
#undef SUM

static int vsad16_mmx(void *v, uint8_t * pix1, uint8_t * pix2, int line_size, int h) {
    int tmp;

    assert( (((int)pix1) & 7) == 0);
    assert( (((int)pix2) & 7) == 0);
    assert((line_size &7) ==0);

#define SUM(in0, in1, out0, out1) \
      "movq (%0),%%mm2\n"\
      "movq (%1)," #out0 "\n"\
      "movq 8(%0),%%mm3\n"\
      "movq 8(%1)," #out1 "\n"\
      "add %3,%0\n"\
      "add %3,%1\n"\
      "psubb " #out0 ", %%mm2\n"\
      "psubb " #out1 ", %%mm3\n"\
      "pxor %%mm7, %%mm2\n"\
      "pxor %%mm7, %%mm3\n"\
      "movq %%mm2, " #out0 "\n"\
      "movq %%mm3, " #out1 "\n"\
      "psubusb " #in0 ", %%mm2\n"\
      "psubusb " #in1 ", %%mm3\n"\
      "psubusb " #out0 ", " #in0 "\n"\
      "psubusb " #out1 ", " #in1 "\n"\
      "por %%mm2, " #in0 "\n"\
      "por %%mm3, " #in1 "\n"\
      "movq " #in0 ", %%mm2\n"\
      "movq " #in1 ", %%mm3\n"\
      "punpcklbw %%mm7, " #in0 "\n"\
      "punpcklbw %%mm7, " #in1 "\n"\
      "punpckhbw %%mm7, %%mm2\n"\
      "punpckhbw %%mm7, %%mm3\n"\
      "paddw " #in1 ", " #in0 "\n"\
      "paddw %%mm3, %%mm2\n"\
      "paddw %%mm2, " #in0 "\n"\
      "paddw " #in0 ", %%mm6\n"


  __asm__ volatile (
      "movl %4,%%ecx\n"
      "pxor %%mm6,%%mm6\n"
      "pcmpeqw %%mm7,%%mm7\n"
      "psllw $15, %%mm7\n"
      "packsswb %%mm7, %%mm7\n"
      "movq (%0),%%mm0\n"
      "movq (%1),%%mm2\n"
      "movq 8(%0),%%mm1\n"
      "movq 8(%1),%%mm3\n"
      "add %3,%0\n"
      "add %3,%1\n"
      "psubb %%mm2, %%mm0\n"
      "psubb %%mm3, %%mm1\n"
      "pxor %%mm7, %%mm0\n"
      "pxor %%mm7, %%mm1\n"
      "jmp 2f\n"
      "1:\n"

      SUM(%%mm4, %%mm5, %%mm0, %%mm1)
      "2:\n"
      SUM(%%mm0, %%mm1, %%mm4, %%mm5)

      "subl $2, %%ecx\n"
      "jnz 1b\n"

      "movq %%mm6,%%mm0\n"
      "psrlq $32, %%mm6\n"
      "paddw %%mm6,%%mm0\n"
      "movq %%mm0,%%mm6\n"
      "psrlq $16, %%mm0\n"
      "paddw %%mm6,%%mm0\n"
      "movd %%mm0,%2\n"
      : "+r" (pix1), "+r" (pix2), "=r"(tmp)
      : "r" ((x86_reg)line_size) , "m" (h)
      : "%ecx");
    return tmp & 0x7FFF;
}
#undef SUM

static int vsad16_mmx2(void *v, uint8_t * pix1, uint8_t * pix2, int line_size, int h) {
    int tmp;

    assert( (((int)pix1) & 7) == 0);
    assert( (((int)pix2) & 7) == 0);
    assert((line_size &7) ==0);

#define SUM(in0, in1, out0, out1) \
      "movq (%0)," #out0 "\n"\
      "movq (%1),%%mm2\n"\
      "movq 8(%0)," #out1 "\n"\
      "movq 8(%1),%%mm3\n"\
      "add %3,%0\n"\
      "add %3,%1\n"\
      "psubb %%mm2, " #out0 "\n"\
      "psubb %%mm3, " #out1 "\n"\
      "pxor %%mm7, " #out0 "\n"\
      "pxor %%mm7, " #out1 "\n"\
      "psadbw " #out0 ", " #in0 "\n"\
      "psadbw " #out1 ", " #in1 "\n"\
      "paddw " #in1 ", " #in0 "\n"\
      "paddw " #in0 ", %%mm6\n"

  __asm__ volatile (
      "movl %4,%%ecx\n"
      "pxor %%mm6,%%mm6\n"
      "pcmpeqw %%mm7,%%mm7\n"
      "psllw $15, %%mm7\n"
      "packsswb %%mm7, %%mm7\n"
      "movq (%0),%%mm0\n"
      "movq (%1),%%mm2\n"
      "movq 8(%0),%%mm1\n"
      "movq 8(%1),%%mm3\n"
      "add %3,%0\n"
      "add %3,%1\n"
      "psubb %%mm2, %%mm0\n"
      "psubb %%mm3, %%mm1\n"
      "pxor %%mm7, %%mm0\n"
      "pxor %%mm7, %%mm1\n"
      "jmp 2f\n"
      "1:\n"

      SUM(%%mm4, %%mm5, %%mm0, %%mm1)
      "2:\n"
      SUM(%%mm0, %%mm1, %%mm4, %%mm5)

      "subl $2, %%ecx\n"
      "jnz 1b\n"

      "movd %%mm6,%2\n"
      : "+r" (pix1), "+r" (pix2), "=r"(tmp)
      : "r" ((x86_reg)line_size) , "m" (h)
      : "%ecx");
    return tmp;
}
#undef SUM

static void diff_bytes_mmx(uint8_t *dst, uint8_t *src1, uint8_t *src2, int w){
    x86_reg i=0;
    __asm__ volatile(
        "1:                             \n\t"
        "movq  (%2, %0), %%mm0          \n\t"
        "movq  (%1, %0), %%mm1          \n\t"
        "psubb %%mm0, %%mm1             \n\t"
        "movq %%mm1, (%3, %0)           \n\t"
        "movq 8(%2, %0), %%mm0          \n\t"
        "movq 8(%1, %0), %%mm1          \n\t"
        "psubb %%mm0, %%mm1             \n\t"
        "movq %%mm1, 8(%3, %0)          \n\t"
        "add $16, %0                    \n\t"
        "cmp %4, %0                     \n\t"
        " jb 1b                         \n\t"
        : "+r" (i)
        : "r"(src1), "r"(src2), "r"(dst), "r"((x86_reg)w-15)
    );
    for(; i<w; i++)
        dst[i+0] = src1[i+0]-src2[i+0];
}

static void sub_hfyu_median_prediction_mmx2(uint8_t *dst, uint8_t *src1, uint8_t *src2, int w, int *left, int *left_top){
    x86_reg i=0;
    uint8_t l, lt;

    __asm__ volatile(
        "1:                             \n\t"
        "movq  -1(%1, %0), %%mm0        \n\t" // LT
        "movq  (%1, %0), %%mm1          \n\t" // T
        "movq  -1(%2, %0), %%mm2        \n\t" // L
        "movq  (%2, %0), %%mm3          \n\t" // X
        "movq %%mm2, %%mm4              \n\t" // L
        "psubb %%mm0, %%mm2             \n\t"
        "paddb %%mm1, %%mm2             \n\t" // L + T - LT
        "movq %%mm4, %%mm5              \n\t" // L
        "pmaxub %%mm1, %%mm4            \n\t" // max(T, L)
        "pminub %%mm5, %%mm1            \n\t" // min(T, L)
        "pminub %%mm2, %%mm4            \n\t"
        "pmaxub %%mm1, %%mm4            \n\t"
        "psubb %%mm4, %%mm3             \n\t" // dst - pred
        "movq %%mm3, (%3, %0)           \n\t"
        "add $8, %0                     \n\t"
        "cmp %4, %0                     \n\t"
        " jb 1b                         \n\t"
        : "+r" (i)
        : "r"(src1), "r"(src2), "r"(dst), "r"((x86_reg)w)
    );

    l= *left;
    lt= *left_top;

    dst[0]= src2[0] - mid_pred(l, src1[0], (l + src1[0] - lt)&0xFF);

    *left_top= src1[w-1];
    *left    = src2[w-1];
}

#define DIFF_PIXELS_1(m,a,t,p1,p2)\
    "mov"#m" "#p1", "#a"              \n\t"\
    "mov"#m" "#p2", "#t"              \n\t"\
    "punpcklbw "#a", "#t"             \n\t"\
    "punpcklbw "#a", "#a"             \n\t"\
    "psubw     "#t", "#a"             \n\t"\

#define DIFF_PIXELS_8(m0,m1,mm,p1,p2,stride,temp) {\
    uint8_t *p1b=p1, *p2b=p2;\
    __asm__ volatile(\
        DIFF_PIXELS_1(m0, mm##0, mm##7, (%1), (%2))\
        DIFF_PIXELS_1(m0, mm##1, mm##7, (%1,%3), (%2,%3))\
        DIFF_PIXELS_1(m0, mm##2, mm##7, (%1,%3,2), (%2,%3,2))\
        "add %4, %1                   \n\t"\
        "add %4, %2                   \n\t"\
        DIFF_PIXELS_1(m0, mm##3, mm##7, (%1), (%2))\
        DIFF_PIXELS_1(m0, mm##4, mm##7, (%1,%3), (%2,%3))\
        DIFF_PIXELS_1(m0, mm##5, mm##7, (%1,%3,2), (%2,%3,2))\
        DIFF_PIXELS_1(m0, mm##6, mm##7, (%1,%4), (%2,%4))\
        "mov"#m1" "#mm"0, %0          \n\t"\
        DIFF_PIXELS_1(m0, mm##7, mm##0, (%1,%3,4), (%2,%3,4))\
        "mov"#m1" %0, "#mm"0          \n\t"\
        : "+m"(temp), "+r"(p1b), "+r"(p2b)\
        : "r"((x86_reg)stride), "r"((x86_reg)stride*3)\
    );\
}
    //the "+m"(temp) is needed as gcc 2.95 sometimes fails to compile "=m"(temp)

#define DIFF_PIXELS_4x8(p1,p2,stride,temp) DIFF_PIXELS_8(d, q,   %%mm,  p1, p2, stride, temp)
#define DIFF_PIXELS_8x8(p1,p2,stride,temp) DIFF_PIXELS_8(q, dqa, %%xmm, p1, p2, stride, temp)

#define LBUTTERFLY2(a1,b1,a2,b2)\
    "paddw " #b1 ", " #a1 "           \n\t"\
    "paddw " #b2 ", " #a2 "           \n\t"\
    "paddw " #b1 ", " #b1 "           \n\t"\
    "paddw " #b2 ", " #b2 "           \n\t"\
    "psubw " #a1 ", " #b1 "           \n\t"\
    "psubw " #a2 ", " #b2 "           \n\t"

#define HADAMARD8(m0, m1, m2, m3, m4, m5, m6, m7)\
        LBUTTERFLY2(m0, m1, m2, m3)\
        LBUTTERFLY2(m4, m5, m6, m7)\
        LBUTTERFLY2(m0, m2, m1, m3)\
        LBUTTERFLY2(m4, m6, m5, m7)\
        LBUTTERFLY2(m0, m4, m1, m5)\
        LBUTTERFLY2(m2, m6, m3, m7)\

#define HADAMARD48 HADAMARD8(%%mm0, %%mm1, %%mm2, %%mm3, %%mm4, %%mm5, %%mm6, %%mm7)

#define MMABS_MMX(a,z)\
    "pxor " #z ", " #z "              \n\t"\
    "pcmpgtw " #a ", " #z "           \n\t"\
    "pxor " #z ", " #a "              \n\t"\
    "psubw " #z ", " #a "             \n\t"

#define MMABS_MMX2(a,z)\
    "pxor " #z ", " #z "              \n\t"\
    "psubw " #a ", " #z "             \n\t"\
    "pmaxsw " #z ", " #a "            \n\t"

#define MMABS_SSSE3(a,z)\
    "pabsw " #a ", " #a "             \n\t"

#define MMABS_SUM(a,z, sum)\
    MMABS(a,z)\
    "paddusw " #a ", " #sum "         \n\t"

#define MMABS_SUM_8x8_NOSPILL\
    MMABS(%%xmm0, %%xmm8)\
    MMABS(%%xmm1, %%xmm9)\
    MMABS_SUM(%%xmm2, %%xmm8, %%xmm0)\
    MMABS_SUM(%%xmm3, %%xmm9, %%xmm1)\
    MMABS_SUM(%%xmm4, %%xmm8, %%xmm0)\
    MMABS_SUM(%%xmm5, %%xmm9, %%xmm1)\
    MMABS_SUM(%%xmm6, %%xmm8, %%xmm0)\
    MMABS_SUM(%%xmm7, %%xmm9, %%xmm1)\
    "paddusw %%xmm1, %%xmm0           \n\t"

#if ARCH_X86_64
#define MMABS_SUM_8x8_SSE2 MMABS_SUM_8x8_NOSPILL
#else
#define MMABS_SUM_8x8_SSE2\
    "movdqa %%xmm7, (%1)              \n\t"\
    MMABS(%%xmm0, %%xmm7)\
    MMABS(%%xmm1, %%xmm7)\
    MMABS_SUM(%%xmm2, %%xmm7, %%xmm0)\
    MMABS_SUM(%%xmm3, %%xmm7, %%xmm1)\
    MMABS_SUM(%%xmm4, %%xmm7, %%xmm0)\
    MMABS_SUM(%%xmm5, %%xmm7, %%xmm1)\
    MMABS_SUM(%%xmm6, %%xmm7, %%xmm0)\
    "movdqa (%1), %%xmm2              \n\t"\
    MMABS_SUM(%%xmm2, %%xmm7, %%xmm1)\
    "paddusw %%xmm1, %%xmm0           \n\t"
#endif

/* FIXME: HSUM_* saturates at 64k, while an 8x8 hadamard or dct block can get up to
 * about 100k on extreme inputs. But that's very unlikely to occur in natural video,
 * and it's even more unlikely to not have any alternative mvs/modes with lower cost. */
#define HSUM_MMX(a, t, dst)\
    "movq "#a", "#t"                  \n\t"\
    "psrlq $32, "#a"                  \n\t"\
    "paddusw "#t", "#a"               \n\t"\
    "movq "#a", "#t"                  \n\t"\
    "psrlq $16, "#a"                  \n\t"\
    "paddusw "#t", "#a"               \n\t"\
    "movd "#a", "#dst"                \n\t"\

#define HSUM_MMX2(a, t, dst)\
    "pshufw $0x0E, "#a", "#t"         \n\t"\
    "paddusw "#t", "#a"               \n\t"\
    "pshufw $0x01, "#a", "#t"         \n\t"\
    "paddusw "#t", "#a"               \n\t"\
    "movd "#a", "#dst"                \n\t"\

#define HSUM_SSE2(a, t, dst)\
    "movhlps "#a", "#t"               \n\t"\
    "paddusw "#t", "#a"               \n\t"\
    "pshuflw $0x0E, "#a", "#t"        \n\t"\
    "paddusw "#t", "#a"               \n\t"\
    "pshuflw $0x01, "#a", "#t"        \n\t"\
    "paddusw "#t", "#a"               \n\t"\
    "movd "#a", "#dst"                \n\t"\

#define HADAMARD8_DIFF_MMX(cpu) \
static int hadamard8_diff_##cpu(void *s, uint8_t *src1, uint8_t *src2, int stride, int h){\
    DECLARE_ALIGNED_8(uint64_t, temp[13]);\
    int sum;\
\
    assert(h==8);\
\
    DIFF_PIXELS_4x8(src1, src2, stride, temp[0]);\
\
    __asm__ volatile(\
        HADAMARD48\
\
        "movq %%mm7, 96(%1)             \n\t"\
\
        TRANSPOSE4(%%mm0, %%mm1, %%mm2, %%mm3, %%mm7)\
        STORE4(8,  0(%1), %%mm0, %%mm3, %%mm7, %%mm2)\
\
        "movq 96(%1), %%mm7             \n\t"\
        TRANSPOSE4(%%mm4, %%mm5, %%mm6, %%mm7, %%mm0)\
        STORE4(8, 64(%1), %%mm4, %%mm7, %%mm0, %%mm6)\
\
        : "=r" (sum)\
        : "r"(temp)\
    );\
\
    DIFF_PIXELS_4x8(src1+4, src2+4, stride, temp[4]);\
\
    __asm__ volatile(\
        HADAMARD48\
\
        "movq %%mm7, 96(%1)             \n\t"\
\
        TRANSPOSE4(%%mm0, %%mm1, %%mm2, %%mm3, %%mm7)\
        STORE4(8, 32(%1), %%mm0, %%mm3, %%mm7, %%mm2)\
\
        "movq 96(%1), %%mm7             \n\t"\
        TRANSPOSE4(%%mm4, %%mm5, %%mm6, %%mm7, %%mm0)\
        "movq %%mm7, %%mm5              \n\t"/*FIXME remove*/\
        "movq %%mm6, %%mm7              \n\t"\
        "movq %%mm0, %%mm6              \n\t"\
\
        LOAD4(8, 64(%1), %%mm0, %%mm1, %%mm2, %%mm3)\
\
        HADAMARD48\
        "movq %%mm7, 64(%1)             \n\t"\
        MMABS(%%mm0, %%mm7)\
        MMABS(%%mm1, %%mm7)\
        MMABS_SUM(%%mm2, %%mm7, %%mm0)\
        MMABS_SUM(%%mm3, %%mm7, %%mm1)\
        MMABS_SUM(%%mm4, %%mm7, %%mm0)\
        MMABS_SUM(%%mm5, %%mm7, %%mm1)\
        MMABS_SUM(%%mm6, %%mm7, %%mm0)\
        "movq 64(%1), %%mm2             \n\t"\
        MMABS_SUM(%%mm2, %%mm7, %%mm1)\
        "paddusw %%mm1, %%mm0           \n\t"\
        "movq %%mm0, 64(%1)             \n\t"\
\
        LOAD4(8,  0(%1), %%mm0, %%mm1, %%mm2, %%mm3)\
        LOAD4(8, 32(%1), %%mm4, %%mm5, %%mm6, %%mm7)\
\
        HADAMARD48\
        "movq %%mm7, (%1)               \n\t"\
        MMABS(%%mm0, %%mm7)\
        MMABS(%%mm1, %%mm7)\
        MMABS_SUM(%%mm2, %%mm7, %%mm0)\
        MMABS_SUM(%%mm3, %%mm7, %%mm1)\
        MMABS_SUM(%%mm4, %%mm7, %%mm0)\
        MMABS_SUM(%%mm5, %%mm7, %%mm1)\
        MMABS_SUM(%%mm6, %%mm7, %%mm0)\
        "movq (%1), %%mm2               \n\t"\
        MMABS_SUM(%%mm2, %%mm7, %%mm1)\
        "paddusw 64(%1), %%mm0          \n\t"\
        "paddusw %%mm1, %%mm0           \n\t"\
\
        HSUM(%%mm0, %%mm1, %0)\
\
        : "=r" (sum)\
        : "r"(temp)\
    );\
    return sum&0xFFFF;\
}\
WRAPPER8_16_SQ(hadamard8_diff_##cpu, hadamard8_diff16_##cpu)

#define HADAMARD8_DIFF_SSE2(cpu) \
static int hadamard8_diff_##cpu(void *s, uint8_t *src1, uint8_t *src2, int stride, int h){\
    DECLARE_ALIGNED_16(uint64_t, temp[4]);\
    int sum;\
\
    assert(h==8);\
\
    DIFF_PIXELS_8x8(src1, src2, stride, temp[0]);\
\
    __asm__ volatile(\
        HADAMARD8(%%xmm0, %%xmm1, %%xmm2, %%xmm3, %%xmm4, %%xmm5, %%xmm6, %%xmm7)\
        TRANSPOSE8(%%xmm0, %%xmm1, %%xmm2, %%xmm3, %%xmm4, %%xmm5, %%xmm6, %%xmm7, (%1))\
        HADAMARD8(%%xmm0, %%xmm5, %%xmm7, %%xmm3, %%xmm6, %%xmm4, %%xmm2, %%xmm1)\
        MMABS_SUM_8x8\
        HSUM_SSE2(%%xmm0, %%xmm1, %0)\
        : "=r" (sum)\
        : "r"(temp)\
    );\
    return sum&0xFFFF;\
}\
WRAPPER8_16_SQ(hadamard8_diff_##cpu, hadamard8_diff16_##cpu)

#define MMABS(a,z)         MMABS_MMX(a,z)
#define HSUM(a,t,dst)      HSUM_MMX(a,t,dst)
HADAMARD8_DIFF_MMX(mmx)
#undef MMABS
#undef HSUM

#define MMABS(a,z)         MMABS_MMX2(a,z)
#define MMABS_SUM_8x8      MMABS_SUM_8x8_SSE2
#define HSUM(a,t,dst)      HSUM_MMX2(a,t,dst)
HADAMARD8_DIFF_MMX(mmx2)
HADAMARD8_DIFF_SSE2(sse2)
#undef MMABS
#undef MMABS_SUM_8x8
#undef HSUM

#if HAVE_SSSE3
#define MMABS(a,z)         MMABS_SSSE3(a,z)
#define MMABS_SUM_8x8      MMABS_SUM_8x8_NOSPILL
HADAMARD8_DIFF_SSE2(ssse3)
#undef MMABS
#undef MMABS_SUM_8x8
#endif

#define DCT_SAD4(m,mm,o)\
    "mov"#m" "#o"+ 0(%1), "#mm"2      \n\t"\
    "mov"#m" "#o"+16(%1), "#mm"3      \n\t"\
    "mov"#m" "#o"+32(%1), "#mm"4      \n\t"\
    "mov"#m" "#o"+48(%1), "#mm"5      \n\t"\
    MMABS_SUM(mm##2, mm##6, mm##0)\
    MMABS_SUM(mm##3, mm##7, mm##1)\
    MMABS_SUM(mm##4, mm##6, mm##0)\
    MMABS_SUM(mm##5, mm##7, mm##1)\

#define DCT_SAD_MMX\
    "pxor %%mm0, %%mm0                \n\t"\
    "pxor %%mm1, %%mm1                \n\t"\
    DCT_SAD4(q, %%mm, 0)\
    DCT_SAD4(q, %%mm, 8)\
    DCT_SAD4(q, %%mm, 64)\
    DCT_SAD4(q, %%mm, 72)\
    "paddusw %%mm1, %%mm0             \n\t"\
    HSUM(%%mm0, %%mm1, %0)

#define DCT_SAD_SSE2\
    "pxor %%xmm0, %%xmm0              \n\t"\
    "pxor %%xmm1, %%xmm1              \n\t"\
    DCT_SAD4(dqa, %%xmm, 0)\
    DCT_SAD4(dqa, %%xmm, 64)\
    "paddusw %%xmm1, %%xmm0           \n\t"\
    HSUM(%%xmm0, %%xmm1, %0)

#define DCT_SAD_FUNC(cpu) \
static int sum_abs_dctelem_##cpu(DCTELEM *block){\
    int sum;\
    __asm__ volatile(\
        DCT_SAD\
        :"=r"(sum)\
        :"r"(block)\
    );\
    return sum&0xFFFF;\
}

#define DCT_SAD       DCT_SAD_MMX
#define HSUM(a,t,dst) HSUM_MMX(a,t,dst)
#define MMABS(a,z)    MMABS_MMX(a,z)
DCT_SAD_FUNC(mmx)
#undef MMABS
#undef HSUM

#define HSUM(a,t,dst) HSUM_MMX2(a,t,dst)
#define MMABS(a,z)    MMABS_MMX2(a,z)
DCT_SAD_FUNC(mmx2)
#undef HSUM
#undef DCT_SAD

#define DCT_SAD       DCT_SAD_SSE2
#define HSUM(a,t,dst) HSUM_SSE2(a,t,dst)
DCT_SAD_FUNC(sse2)
#undef MMABS

#if HAVE_SSSE3
#define MMABS(a,z)    MMABS_SSSE3(a,z)
DCT_SAD_FUNC(ssse3)
#undef MMABS
#endif
#undef HSUM
#undef DCT_SAD

static int ssd_int8_vs_int16_mmx(const int8_t *pix1, const int16_t *pix2, int size){
    int sum;
    x86_reg i=size;
    __asm__ volatile(
        "pxor %%mm4, %%mm4 \n"
        "1: \n"
        "sub $8, %0 \n"
        "movq (%2,%0), %%mm2 \n"
        "movq (%3,%0,2), %%mm0 \n"
        "movq 8(%3,%0,2), %%mm1 \n"
        "punpckhbw %%mm2, %%mm3 \n"
        "punpcklbw %%mm2, %%mm2 \n"
        "psraw $8, %%mm3 \n"
        "psraw $8, %%mm2 \n"
        "psubw %%mm3, %%mm1 \n"
        "psubw %%mm2, %%mm0 \n"
        "pmaddwd %%mm1, %%mm1 \n"
        "pmaddwd %%mm0, %%mm0 \n"
        "paddd %%mm1, %%mm4 \n"
        "paddd %%mm0, %%mm4 \n"
        "jg 1b \n"
        "movq %%mm4, %%mm3 \n"
        "psrlq $32, %%mm3 \n"
        "paddd %%mm3, %%mm4 \n"
        "movd %%mm4, %1 \n"
        :"+r"(i), "=r"(sum)
        :"r"(pix1), "r"(pix2)
    );
    return sum;
}

#define PHADDD(a, t)\
    "movq "#a", "#t"                  \n\t"\
    "psrlq $32, "#a"                  \n\t"\
    "paddd "#t", "#a"                 \n\t"
/*
   pmulhw: dst[0-15]=(src[0-15]*dst[0-15])[16-31]
   pmulhrw: dst[0-15]=(src[0-15]*dst[0-15] + 0x8000)[16-31]
   pmulhrsw: dst[0-15]=(src[0-15]*dst[0-15] + 0x4000)[15-30]
 */
#define PMULHRW(x, y, s, o)\
    "pmulhw " #s ", "#x "            \n\t"\
    "pmulhw " #s ", "#y "            \n\t"\
    "paddw " #o ", "#x "             \n\t"\
    "paddw " #o ", "#y "             \n\t"\
    "psraw $1, "#x "                 \n\t"\
    "psraw $1, "#y "                 \n\t"
#define DEF(x) x ## _mmx
#define SET_RND MOVQ_WONE
#define SCALE_OFFSET 1

#include "dsputil_mmx_qns_template.c"

#undef DEF
#undef SET_RND
#undef SCALE_OFFSET
#undef PMULHRW

#define DEF(x) x ## _3dnow
#define SET_RND(x)
#define SCALE_OFFSET 0
#define PMULHRW(x, y, s, o)\
    "pmulhrw " #s ", "#x "           \n\t"\
    "pmulhrw " #s ", "#y "           \n\t"

#include "dsputil_mmx_qns_template.c"

#undef DEF
#undef SET_RND
#undef SCALE_OFFSET
#undef PMULHRW

#if HAVE_SSSE3
#undef PHADDD
#define DEF(x) x ## _ssse3
#define SET_RND(x)
#define SCALE_OFFSET -1
#define PHADDD(a, t)\
    "pshufw $0x0E, "#a", "#t"         \n\t"\
    "paddd "#t", "#a"                 \n\t" /* faster than phaddd on core2 */
#define PMULHRW(x, y, s, o)\
    "pmulhrsw " #s ", "#x "          \n\t"\
    "pmulhrsw " #s ", "#y "          \n\t"

#include "dsputil_mmx_qns_template.c"

#undef DEF
#undef SET_RND
#undef SCALE_OFFSET
#undef PMULHRW
#undef PHADDD
#endif //HAVE_SSSE3


/* FLAC specific */
void ff_flac_compute_autocorr_sse2(const int32_t *data, int len, int lag,
                                   double *autoc);


void dsputilenc_init_mmx(DSPContext* c, AVCodecContext *avctx)
{
    if (mm_flags & FF_MM_MMX) {
        const int dct_algo = avctx->dct_algo;
        if(dct_algo==FF_DCT_AUTO || dct_algo==FF_DCT_MMX){
            if(mm_flags & FF_MM_SSE2){
                c->fdct = ff_fdct_sse2;
            }else if(mm_flags & FF_MM_MMXEXT){
                c->fdct = ff_fdct_mmx2;
            }else{
                c->fdct = ff_fdct_mmx;
            }
        }

        c->get_pixels = get_pixels_mmx;
        c->diff_pixels = diff_pixels_mmx;
        c->pix_sum = pix_sum16_mmx;

        c->diff_bytes= diff_bytes_mmx;
        c->sum_abs_dctelem= sum_abs_dctelem_mmx;

        c->hadamard8_diff[0]= hadamard8_diff16_mmx;
        c->hadamard8_diff[1]= hadamard8_diff_mmx;

        c->pix_norm1 = pix_norm1_mmx;
        c->sse[0] = (mm_flags & FF_MM_SSE2) ? sse16_sse2 : sse16_mmx;
          c->sse[1] = sse8_mmx;
        c->vsad[4]= vsad_intra16_mmx;

        c->nsse[0] = nsse16_mmx;
        c->nsse[1] = nsse8_mmx;
        if(!(avctx->flags & CODEC_FLAG_BITEXACT)){
            c->vsad[0] = vsad16_mmx;
        }

        if(!(avctx->flags & CODEC_FLAG_BITEXACT)){
            c->try_8x8basis= try_8x8basis_mmx;
        }
        c->add_8x8basis= add_8x8basis_mmx;

        c->ssd_int8_vs_int16 = ssd_int8_vs_int16_mmx;


        if (mm_flags & FF_MM_MMXEXT) {
            c->sum_abs_dctelem= sum_abs_dctelem_mmx2;
            c->hadamard8_diff[0]= hadamard8_diff16_mmx2;
            c->hadamard8_diff[1]= hadamard8_diff_mmx2;
            c->vsad[4]= vsad_intra16_mmx2;

            if(!(avctx->flags & CODEC_FLAG_BITEXACT)){
                c->vsad[0] = vsad16_mmx2;
            }

            c->sub_hfyu_median_prediction= sub_hfyu_median_prediction_mmx2;
        }

        if(mm_flags & FF_MM_SSE2){
            c->get_pixels = get_pixels_sse2;
            c->sum_abs_dctelem= sum_abs_dctelem_sse2;
            c->hadamard8_diff[0]= hadamard8_diff16_sse2;
            c->hadamard8_diff[1]= hadamard8_diff_sse2;
            if (CONFIG_FLAC_ENCODER)
                c->flac_compute_autocorr = ff_flac_compute_autocorr_sse2;
        }

#if HAVE_SSSE3
        if(mm_flags & FF_MM_SSSE3){
            if(!(avctx->flags & CODEC_FLAG_BITEXACT)){
                c->try_8x8basis= try_8x8basis_ssse3;
            }
            c->add_8x8basis= add_8x8basis_ssse3;
            c->sum_abs_dctelem= sum_abs_dctelem_ssse3;
            c->hadamard8_diff[0]= hadamard8_diff16_ssse3;
            c->hadamard8_diff[1]= hadamard8_diff_ssse3;
        }
#endif

        if(mm_flags & FF_MM_3DNOW){
            if(!(avctx->flags & CODEC_FLAG_BITEXACT)){
                c->try_8x8basis= try_8x8basis_3dnow;
            }
            c->add_8x8basis= add_8x8basis_3dnow;
        }
    }

    dsputil_init_pix_mmx(c, avctx);
}
