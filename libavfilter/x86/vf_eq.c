/*
 *
 * Original MPlayer filters by Richard Felker.
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
#include "libavfilter/vf_eq.h"

#if HAVE_MMX_INLINE && HAVE_6REGS
static void process_MMX(EQParameters *param, uint8_t *dst, int dst_stride,
                        const uint8_t *src, int src_stride, int w, int h)
{
        int i;
        int pel;
        int dstep = dst_stride - w;
        int sstep = src_stride - w;
        short brvec[4];
        short contvec[4];
        int contrast = (int) (param->contrast * 256 * 16);
        int brightness = ((int) (100.0 * param->brightness + 100.0) * 511) / 200 - 128 - contrast / 32;

        brvec[0] = brvec[1] = brvec[2] = brvec[3] = brightness;
        contvec[0] = contvec[1] = contvec[2] = contvec[3] = contrast;

        while (h--) {
                __asm__ volatile (
                        "movq (%5), %%mm3      \n\t"
                        "movq (%6), %%mm4      \n\t"
                        "pxor %%mm0, %%mm0     \n\t"
                        "movl %4, %%eax        \n\t"
                        ".p2align 4 \n\t"
                        "1:                    \n\t"
                        "movq (%0), %%mm1      \n\t"
                        "movq (%0), %%mm2      \n\t"
                        "punpcklbw %%mm0, %%mm1\n\t"
                        "punpckhbw %%mm0, %%mm2\n\t"
                        "psllw $4, %%mm1       \n\t"
                        "psllw $4, %%mm2       \n\t"
                        "pmulhw %%mm4, %%mm1   \n\t"
                        "pmulhw %%mm4, %%mm2   \n\t"
                        "paddw %%mm3, %%mm1    \n\t"
                        "paddw %%mm3, %%mm2    \n\t"
                        "packuswb %%mm2, %%mm1 \n\t"
                        "add $8, %0            \n\t"
                        "movq %%mm1, (%1)      \n\t"
                        "add $8, %1            \n\t"
                        "decl %%eax            \n\t"
                        "jnz 1b                \n\t"
                        : "=r" (src), "=r" (dst)
                        : "0" (src), "1" (dst), "r" (w>>3), "r" (brvec), "r" (contvec)
                        : "%eax"
                );

                for (i = w&7; i; i--) {
                        pel = ((*src++ * contrast) >> 12) + brightness;
                        if (pel & ~255)
                            pel = (-pel) >> 31;
                        *dst++ = pel;
                }

                src += sstep;
                dst += dstep;
        }
        __asm__ volatile ( "emms \n\t" ::: "memory" );
}
#endif

av_cold void ff_eq_init_x86(EQContext *eq)
{
#if HAVE_MMX_INLINE && HAVE_6REGS
    int cpu_flags = av_get_cpu_flags();

    if (cpu_flags & AV_CPU_FLAG_MMX) {
        eq->process = process_MMX;
    }
#endif
}
