/*
 * FFT/MDCT transform with 3DNow! optimizations
 * Copyright (c) 2006 Zuxy MENG Jie, Loren Merritt
 * Based on fft_sse.c copyright (c) 2002 Fabrice Bellard.
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
#include "../dsputil.h"

static const int p1m1[2] __attribute__((aligned(8))) =
    { 0, 1 << 31 };

static const int m1p1[2] __attribute__((aligned(8))) =
    { 1 << 31, 0 };

void ff_fft_calc_3dn(FFTContext *s, FFTComplex *z)
{
    int ln = s->nbits;
    long i, j;
    long nblocks, nloops;
    FFTComplex *p, *cptr;

    asm volatile(
        /* FEMMS is not a must here but recommended by AMD */
        "femms \n\t"
        "movq %0, %%mm7 \n\t"
        ::"m"(*(s->inverse ? m1p1 : p1m1))
    );

    i = 8 << ln;
    asm volatile(
        "1: \n\t"
        "sub $32, %0 \n\t"
        "movq    (%0,%1), %%mm0 \n\t"
        "movq  16(%0,%1), %%mm1 \n\t"
        "movq   8(%0,%1), %%mm2 \n\t"
        "movq  24(%0,%1), %%mm3 \n\t"
        "movq      %%mm0, %%mm4 \n\t"
        "movq      %%mm1, %%mm5 \n\t"
        "pfadd     %%mm2, %%mm0 \n\t"
        "pfadd     %%mm3, %%mm1 \n\t"
        "pfsub     %%mm2, %%mm4 \n\t"
        "pfsub     %%mm3, %%mm5 \n\t"
        "movq      %%mm0, %%mm2 \n\t"
        "punpckldq %%mm5, %%mm6 \n\t"
        "punpckhdq %%mm6, %%mm5 \n\t"
        "movq      %%mm4, %%mm3 \n\t"
        "pxor      %%mm7, %%mm5 \n\t"
        "pfadd     %%mm1, %%mm0 \n\t"
        "pfadd     %%mm5, %%mm4 \n\t"
        "pfsub     %%mm1, %%mm2 \n\t"
        "pfsub     %%mm5, %%mm3 \n\t"
        "movq      %%mm0,   (%0,%1) \n\t"
        "movq      %%mm4,  8(%0,%1) \n\t"
        "movq      %%mm2, 16(%0,%1) \n\t"
        "movq      %%mm3, 24(%0,%1) \n\t"
        "jg 1b \n\t"
        :"+r"(i)
        :"r"(z)
    );
    /* pass 2 .. ln-1 */

    nblocks = 1 << (ln-3);
    nloops = 1 << 2;
    cptr = s->exptab1;
    do {
        p = z;
        j = nblocks;
        do {
            i = nloops*8;
            asm volatile(
                "1: \n\t"
                "sub $16, %0 \n\t"
                "movq    (%1,%0), %%mm0 \n\t"
                "movq   8(%1,%0), %%mm1 \n\t"
                "movq    (%2,%0), %%mm2 \n\t"
                "movq   8(%2,%0), %%mm3 \n\t"
                "movq      %%mm2, %%mm4 \n\t"
                "movq      %%mm3, %%mm5 \n\t"
                "punpckldq %%mm2, %%mm2 \n\t"
                "punpckldq %%mm3, %%mm3 \n\t"
                "punpckhdq %%mm4, %%mm4 \n\t"
                "punpckhdq %%mm5, %%mm5 \n\t"
                "pfmul   (%3,%0,2), %%mm2 \n\t" //  cre*re cim*re
                "pfmul  8(%3,%0,2), %%mm3 \n\t"
                "pfmul 16(%3,%0,2), %%mm4 \n\t" // -cim*im cre*im
                "pfmul 24(%3,%0,2), %%mm5 \n\t"
                "pfadd     %%mm2, %%mm4 \n\t" // cre*re-cim*im cim*re+cre*im
                "pfadd     %%mm3, %%mm5 \n\t"
                "movq      %%mm0, %%mm2 \n\t"
                "movq      %%mm1, %%mm3 \n\t"
                "pfadd     %%mm4, %%mm0 \n\t"
                "pfadd     %%mm5, %%mm1 \n\t"
                "pfsub     %%mm4, %%mm2 \n\t"
                "pfsub     %%mm5, %%mm3 \n\t"
                "movq      %%mm0,  (%1,%0) \n\t"
                "movq      %%mm1, 8(%1,%0) \n\t"
                "movq      %%mm2,  (%2,%0) \n\t"
                "movq      %%mm3, 8(%2,%0) \n\t"
                "jg 1b \n\t"
                :"+r"(i)
                :"r"(p), "r"(p + nloops), "r"(cptr)
            );
            p += nloops*2;
        } while (--j);
        cptr += nloops*2;
        nblocks >>= 1;
        nloops <<= 1;
    } while (nblocks != 0);
    asm volatile("femms");
}
