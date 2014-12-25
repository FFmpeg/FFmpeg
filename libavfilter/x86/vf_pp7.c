/*
 * Copyright (c) 2005 Michael Niedermayer <michaelni@gmx.at>
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
#include "libavfilter/vf_pp7.h"

static void dctB_mmx(int16_t *dst, int16_t *src)
{
#if HAVE_MMX_INLINE
    __asm__ volatile (
        "movq  (%0), %%mm0      \n\t"
        "movq  1*4*2(%0), %%mm1 \n\t"
        "paddw 6*4*2(%0), %%mm0 \n\t"
        "paddw 5*4*2(%0), %%mm1 \n\t"
        "movq  2*4*2(%0), %%mm2 \n\t"
        "movq  3*4*2(%0), %%mm3 \n\t"
        "paddw 4*4*2(%0), %%mm2 \n\t"
        "paddw %%mm3, %%mm3     \n\t" //s
        "movq %%mm3, %%mm4      \n\t" //s
        "psubw %%mm0, %%mm3     \n\t" //s-s0
        "paddw %%mm0, %%mm4     \n\t" //s+s0
        "movq %%mm2, %%mm0      \n\t" //s2
        "psubw %%mm1, %%mm2     \n\t" //s2-s1
        "paddw %%mm1, %%mm0     \n\t" //s2+s1
        "movq %%mm4, %%mm1      \n\t" //s0'
        "psubw %%mm0, %%mm4     \n\t" //s0'-s'
        "paddw %%mm0, %%mm1     \n\t" //s0'+s'
        "movq %%mm3, %%mm0      \n\t" //s3'
        "psubw %%mm2, %%mm3     \n\t"
        "psubw %%mm2, %%mm3     \n\t"
        "paddw %%mm0, %%mm2     \n\t"
        "paddw %%mm0, %%mm2     \n\t"
        "movq %%mm1, (%1)       \n\t"
        "movq %%mm4, 2*4*2(%1)  \n\t"
        "movq %%mm2, 1*4*2(%1)  \n\t"
        "movq %%mm3, 3*4*2(%1)  \n\t"
        :: "r" (src), "r"(dst)
    );
#endif
}

av_cold void ff_pp7_init_x86(PP7Context *p)
{
    int cpu_flags = av_get_cpu_flags();

    if (HAVE_MMX_INLINE && cpu_flags & AV_CPU_FLAG_MMX)
        p->dctB = dctB_mmx;
}
