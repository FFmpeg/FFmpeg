/*
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

#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/asm.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/svq1enc.h"

#if HAVE_INLINE_ASM

static int ssd_int8_vs_int16_mmx(const int8_t *pix1, const int16_t *pix2,
                                 int size)
{
    int sum;
    x86_reg i = size;

    __asm__ volatile (
        "pxor %%mm4, %%mm4 \n"
        "1: \n"
        "sub $8, %0 \n"
        "movq (%2, %0), %%mm2 \n"
        "movq (%3, %0, 2), %%mm0 \n"
        "movq 8(%3, %0, 2), %%mm1 \n"
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
        : "+r" (i), "=r" (sum)
        : "r" (pix1), "r" (pix2));

    return sum;
}

#endif /* HAVE_INLINE_ASM */

av_cold void ff_svq1enc_init_x86(SVQ1EncContext *c)
{
#if HAVE_INLINE_ASM
    int cpu_flags = av_get_cpu_flags();

    if (INLINE_MMX(cpu_flags)) {
        c->ssd_int8_vs_int16 = ssd_int8_vs_int16_mmx;
    }
#endif /* HAVE_INLINE_ASM */
}
