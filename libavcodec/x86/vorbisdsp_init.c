/*
 * Copyright (C) 2006 Loren Merritt <lorenm@u.washington.edu>
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

#include "config.h"
#include "libavutil/cpu.h"
#include "libavcodec/vorbisdsp.h"
#include "dsputil_mmx.h" // for ff_pdw_80000000

#if HAVE_INLINE_ASM
#if ARCH_X86_32
static void vorbis_inverse_coupling_3dnow(float *mag, float *ang, int blocksize)
{
    int i;
    __asm__ volatile ("pxor %%mm7, %%mm7":);
    for (i = 0; i < blocksize; i += 2) {
        __asm__ volatile (
            "movq       %0, %%mm0   \n\t"
            "movq       %1, %%mm1   \n\t"
            "movq    %%mm0, %%mm2   \n\t"
            "movq    %%mm1, %%mm3   \n\t"
            "pfcmpge %%mm7, %%mm2   \n\t" // m <= 0.0
            "pfcmpge %%mm7, %%mm3   \n\t" // a <= 0.0
            "pslld     $31, %%mm2   \n\t" // keep only the sign bit
            "pxor    %%mm2, %%mm1   \n\t"
            "movq    %%mm3, %%mm4   \n\t"
            "pand    %%mm1, %%mm3   \n\t"
            "pandn   %%mm1, %%mm4   \n\t"
            "pfadd   %%mm0, %%mm3   \n\t" // a = m + ((a < 0) & (a ^ sign(m)))
            "pfsub   %%mm4, %%mm0   \n\t" // m = m + ((a > 0) & (a ^ sign(m)))
            "movq    %%mm3, %1      \n\t"
            "movq    %%mm0, %0      \n\t"
            : "+m"(mag[i]), "+m"(ang[i])
            :: "memory"
        );
    }
    __asm__ volatile ("femms");
}
#endif

static void vorbis_inverse_coupling_sse(float *mag, float *ang, int blocksize)
{
    int i;

    __asm__ volatile (
        "movaps  %0, %%xmm5 \n\t"
        :: "m"(ff_pdw_80000000[0])
    );
    for (i = 0; i < blocksize; i += 4) {
        __asm__ volatile (
            "movaps      %0, %%xmm0 \n\t"
            "movaps      %1, %%xmm1 \n\t"
            "xorps   %%xmm2, %%xmm2 \n\t"
            "xorps   %%xmm3, %%xmm3 \n\t"
            "cmpleps %%xmm0, %%xmm2 \n\t" // m <= 0.0
            "cmpleps %%xmm1, %%xmm3 \n\t" // a <= 0.0
            "andps   %%xmm5, %%xmm2 \n\t" // keep only the sign bit
            "xorps   %%xmm2, %%xmm1 \n\t"
            "movaps  %%xmm3, %%xmm4 \n\t"
            "andps   %%xmm1, %%xmm3 \n\t"
            "andnps  %%xmm1, %%xmm4 \n\t"
            "addps   %%xmm0, %%xmm3 \n\t" // a = m + ((a < 0) & (a ^ sign(m)))
            "subps   %%xmm4, %%xmm0 \n\t" // m = m + ((a > 0) & (a ^ sign(m)))
            "movaps  %%xmm3, %1     \n\t"
            "movaps  %%xmm0, %0     \n\t"
            : "+m"(mag[i]), "+m"(ang[i])
            :: "memory"
        );
    }
}
#endif

void ff_vorbisdsp_init_x86(VorbisDSPContext *dsp)
{
#if HAVE_INLINE_ASM
    int mm_flags = av_get_cpu_flags();

#if ARCH_X86_32
    if (mm_flags & AV_CPU_FLAG_3DNOW)
        dsp->vorbis_inverse_coupling = vorbis_inverse_coupling_3dnow;
#endif /* ARCH_X86_32 */
    if (mm_flags & AV_CPU_FLAG_SSE)
        dsp->vorbis_inverse_coupling = vorbis_inverse_coupling_sse;
#endif /* HAVE_INLINE_ASM */
}
