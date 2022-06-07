/*
 * Copyright (c) 2002 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (c) 2013 Paul B Mahol
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
#include "libavutil/x86/cpu.h"
#include "libavutil/x86/asm.h"
#include "libavfilter/vf_noise.h"

#if HAVE_INLINE_ASM
#if HAVE_6REGS
static void line_noise_avg_mmx(uint8_t *dst, const uint8_t *src,
                                      int len, const int8_t * const *shift)
{
    x86_reg mmx_len = len & (~7);

    __asm__ volatile(
            "mov %5, %%"FF_REG_a"           \n\t"
            ".p2align 4                     \n\t"
            "1:                             \n\t"
            "movq (%1, %%"FF_REG_a"), %%mm1 \n\t"
            "movq (%0, %%"FF_REG_a"), %%mm0 \n\t"
            "paddb (%2, %%"FF_REG_a"), %%mm1\n\t"
            "paddb (%3, %%"FF_REG_a"), %%mm1\n\t"
            "movq %%mm0, %%mm2              \n\t"
            "movq %%mm1, %%mm3              \n\t"
            "punpcklbw %%mm0, %%mm0         \n\t"
            "punpckhbw %%mm2, %%mm2         \n\t"
            "punpcklbw %%mm1, %%mm1         \n\t"
            "punpckhbw %%mm3, %%mm3         \n\t"
            "pmulhw %%mm0, %%mm1            \n\t"
            "pmulhw %%mm2, %%mm3            \n\t"
            "paddw %%mm1, %%mm1             \n\t"
            "paddw %%mm3, %%mm3             \n\t"
            "paddw %%mm0, %%mm1             \n\t"
            "paddw %%mm2, %%mm3             \n\t"
            "psrlw $8, %%mm1                \n\t"
            "psrlw $8, %%mm3                \n\t"
            "packuswb %%mm3, %%mm1          \n\t"
            "movq %%mm1, (%4, %%"FF_REG_a") \n\t"
            "add $8, %%"FF_REG_a"           \n\t"
            " js 1b                         \n\t"
            :: "r" (src+mmx_len), "r" (shift[0]+mmx_len), "r" (shift[1]+mmx_len), "r" (shift[2]+mmx_len),
               "r" (dst+mmx_len), "g" (-mmx_len)
            : "%"FF_REG_a
        );

    if (mmx_len != len){
        const int8_t *shift2[3] = { shift[0]+mmx_len, shift[1]+mmx_len, shift[2]+mmx_len };
        ff_line_noise_avg_c(dst+mmx_len, src+mmx_len, len-mmx_len, shift2);
    }
}
#endif /* HAVE_6REGS */

static void line_noise_mmxext(uint8_t *dst, const uint8_t *src,
                              const int8_t *noise, int len, int shift)
{
    x86_reg mmx_len = len & (~7);
    noise += shift;

    __asm__ volatile(
            "mov %3, %%"FF_REG_a"             \n\t"
            "pcmpeqb %%mm7, %%mm7             \n\t"
            "psllw $15, %%mm7                 \n\t"
            "packsswb %%mm7, %%mm7            \n\t"
            ".p2align 4                       \n\t"
            "1:                               \n\t"
            "movq (%0, %%"FF_REG_a"), %%mm0   \n\t"
            "movq (%1, %%"FF_REG_a"), %%mm1   \n\t"
            "pxor %%mm7, %%mm0                \n\t"
            "paddsb %%mm1, %%mm0              \n\t"
            "pxor %%mm7, %%mm0                \n\t"
            "movntq %%mm0, (%2, %%"FF_REG_a") \n\t"
            "add $8, %%"FF_REG_a"             \n\t"
            " js 1b                           \n\t"
            :: "r" (src+mmx_len), "r" (noise+mmx_len), "r" (dst+mmx_len), "g" (-mmx_len)
            : "%"FF_REG_a
            );
    if (mmx_len != len)
        ff_line_noise_c(dst+mmx_len, src+mmx_len, noise+mmx_len, len-mmx_len, 0);
}
#endif /* HAVE_INLINE_ASM */

av_cold void ff_noise_init_x86(NoiseContext *n)
{
#if HAVE_INLINE_ASM
    int cpu_flags = av_get_cpu_flags();

    if (INLINE_MMX(cpu_flags)) {
#if HAVE_6REGS
        n->line_noise_avg = line_noise_avg_mmx;
#endif
    }
    if (INLINE_MMXEXT(cpu_flags)) {
        n->line_noise     = line_noise_mmxext;
    }
#endif
}
