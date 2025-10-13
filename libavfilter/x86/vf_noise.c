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
static void line_noise_avg_sse2(uint8_t *dst, const uint8_t *src,
                                int len, const int8_t * const *shift)
{
    x86_reg xmm_len = len & (~15);

    __asm__ volatile(
            "mov                    %5, %%"FF_REG_a"       \n\t"
            "pxor               %%xmm4, %%xmm4             \n\t"
            ".p2align 4                                    \n\t"
            "1:                                            \n\t"
            "movdqu (%1, %%"FF_REG_a"), %%xmm1             \n\t"
            "movdqu (%2, %%"FF_REG_a"), %%xmm2             \n\t"
            "movdqu (%3, %%"FF_REG_a"), %%xmm3             \n\t"
            "movdqa (%0, %%"FF_REG_a"), %%xmm0             \n\t"
            "paddb              %%xmm2, %%xmm1             \n\t"
            "paddb              %%xmm3, %%xmm1             \n\t"
            "movdqa             %%xmm4, %%xmm5             \n\t"
            "pcmpgtb            %%xmm0, %%xmm5             \n\t"
            "movdqa             %%xmm0, %%xmm6             \n\t"
            "movdqa             %%xmm0, %%xmm2             \n\t"
            "punpcklbw          %%xmm5, %%xmm0             \n\t"
            "punpckhbw          %%xmm5, %%xmm2             \n\t"
            "movdqa             %%xmm4, %%xmm5             \n\t"
            "pcmpgtb            %%xmm1, %%xmm5             \n\t"
            "movdqa             %%xmm1, %%xmm3             \n\t"
            "punpcklbw          %%xmm5, %%xmm1             \n\t"
            "punpckhbw          %%xmm5, %%xmm3             \n\t"
            "pmullw             %%xmm0, %%xmm1             \n\t"
            "pmullw             %%xmm2, %%xmm3             \n\t"
            "psraw                  $7, %%xmm1             \n\t"
            "psraw                  $7, %%xmm3             \n\t"
            "packsswb           %%xmm3, %%xmm1             \n\t"
            "paddb              %%xmm6, %%xmm1             \n\t"
            "movdqa             %%xmm1, (%4, %%"FF_REG_a") \n\t"
            "add                   $16, %%"FF_REG_a"       \n\t"
            " js 1b                         \n\t"
            :: "r" (src+xmm_len), "r" (shift[0]+xmm_len), "r" (shift[1]+xmm_len), "r" (shift[2]+xmm_len),
               "r" (dst+xmm_len), "g" (-xmm_len)
            : XMM_CLOBBERS("%xmm0", "%xmm1", "%xmm2", "%xmm3",
                           "%xmm4", "%xmm5", "%xmm6",) "%"FF_REG_a
        );

    if (xmm_len != len){
        const int8_t *shift2[3] = { shift[0]+xmm_len, shift[1]+xmm_len, shift[2]+xmm_len };
        ff_line_noise_avg_c(dst + xmm_len, src + xmm_len, len - xmm_len, shift2);
    }
}
#endif /* HAVE_6REGS */

static void line_noise_sse2(uint8_t *dst, const uint8_t *src,
                            const int8_t *noise, int len, int shift)
{
    x86_reg xmm_len = len & (~15);
    noise += shift;

    __asm__ volatile(
            "mov                    %3, %%"FF_REG_a"       \n\t"
            "pcmpeqb            %%xmm2, %%xmm2             \n\t"
            "psllw                 $15, %%xmm2             \n\t"
            "packsswb           %%xmm2, %%xmm2             \n\t"
            ".p2align 4                                    \n\t"
            "1:                                            \n\t"
            "movdqa (%0, %%"FF_REG_a"), %%xmm0             \n\t"
            "movdqu (%1, %%"FF_REG_a"), %%xmm1             \n\t"
            "pxor               %%xmm2, %%xmm0             \n\t"
            "paddsb             %%xmm1, %%xmm0             \n\t"
            "pxor               %%xmm2, %%xmm0             \n\t"
            "movntdq            %%xmm0, (%2, %%"FF_REG_a") \n\t"
            "add                   $16, %%"FF_REG_a"       \n\t"
            " js                    1b                     \n\t"
            :: "r" (src+xmm_len), "r" (noise+xmm_len), "r" (dst+xmm_len), "g" (-xmm_len)
            : XMM_CLOBBERS("%xmm0", "%xmm1", "%xmm2",) "%"FF_REG_a
            );
    if (xmm_len != len)
        ff_line_noise_c(dst+xmm_len, src + xmm_len, noise + xmm_len, len - xmm_len, 0);
}
#endif /* HAVE_INLINE_ASM */

av_cold void ff_noise_init_x86(NoiseContext *n)
{
#if HAVE_INLINE_ASM
    int cpu_flags = av_get_cpu_flags();

    if (INLINE_SSE2(cpu_flags)) {
#if HAVE_6REGS
        n->line_noise_avg = line_noise_avg_sse2;
#endif
        n->line_noise     = line_noise_sse2;
    }
#endif
}
