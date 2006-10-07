/*
 * FFT/MDCT transform with Extended 3DNow! optimizations
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

void ff_fft_calc_3dn2(FFTContext *s, FFTComplex *z)
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
        "pswapd    %%mm5, %%mm5 \n\t"
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
                "movq  (%3,%0,2), %%mm4 \n\t"
                "movq 8(%3,%0,2), %%mm5 \n\t"
                "pswapd    %%mm4, %%mm6 \n\t" // no need for cptr[2] & cptr[3]
                "pswapd    %%mm5, %%mm7 \n\t"
                "pfmul     %%mm2, %%mm4 \n\t" // cre*re cim*im
                "pfmul     %%mm3, %%mm5 \n\t"
                "pfmul     %%mm2, %%mm6 \n\t" // cim*re cre*im
                "pfmul     %%mm3, %%mm7 \n\t"
                "pfpnacc   %%mm6, %%mm4 \n\t" // cre*re-cim*im cim*re+cre*im
                "pfpnacc   %%mm7, %%mm5 \n\t"
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

void ff_imdct_calc_3dn2(MDCTContext *s, FFTSample *output,
                        const FFTSample *input, FFTSample *tmp)
{
    long k, n8, n4, n2, n;
    const uint16_t *revtab = s->fft.revtab;
    const FFTSample *tcos = s->tcos;
    const FFTSample *tsin = s->tsin;
    const FFTSample *in1, *in2;
    FFTComplex *z = (FFTComplex *)tmp;

    n = 1 << s->nbits;
    n2 = n >> 1;
    n4 = n >> 2;
    n8 = n >> 3;

    /* pre rotation */
    in1 = input;
    in2 = input + n2 - 1;
    for(k = 0; k < n4; k++) {
        // FIXME a single block is faster, but gcc 2.95 and 3.4.x on 32bit can't compile it
        asm volatile(
            "movd       %0, %%mm0 \n\t"
            "movd       %2, %%mm1 \n\t"
            "punpckldq  %1, %%mm0 \n\t"
            "punpckldq  %3, %%mm1 \n\t"
            "movq    %%mm0, %%mm2 \n\t"
            "pfmul   %%mm1, %%mm0 \n\t"
            "pswapd  %%mm1, %%mm1 \n\t"
            "pfmul   %%mm1, %%mm2 \n\t"
            "pfpnacc %%mm2, %%mm0 \n\t"
            ::"m"(in2[-2*k]), "m"(in1[2*k]),
              "m"(tcos[k]), "m"(tsin[k])
        );
        asm volatile(
            "movq    %%mm0, %0    \n\t"
            :"=m"(z[revtab[k]])
        );
    }

    ff_fft_calc(&s->fft, z);

    /* post rotation + reordering */
    for(k = 0; k < n4; k++) {
        asm volatile(
            "movq       %0, %%mm0 \n\t"
            "movd       %1, %%mm1 \n\t"
            "punpckldq  %2, %%mm1 \n\t"
            "movq    %%mm0, %%mm2 \n\t"
            "pfmul   %%mm1, %%mm0 \n\t"
            "pswapd  %%mm1, %%mm1 \n\t"
            "pfmul   %%mm1, %%mm2 \n\t"
            "pfpnacc %%mm2, %%mm0 \n\t"
            "movq    %%mm0, %0    \n\t"
            :"+m"(z[k])
            :"m"(tcos[k]), "m"(tsin[k])
        );
    }

    k = n-8;
    asm volatile("movd %0, %%mm7" ::"r"(1<<31));
    asm volatile(
        "1: \n\t"
        "movq    (%4,%0), %%mm0 \n\t" // z[n8+k]
        "neg %0 \n\t"
        "pswapd -8(%4,%0), %%mm1 \n\t" // z[n8-1-k]
        "movq      %%mm0, %%mm2 \n\t"
        "pxor      %%mm7, %%mm2 \n\t"
        "punpckldq %%mm1, %%mm2 \n\t"
        "pswapd    %%mm2, %%mm3 \n\t"
        "punpckhdq %%mm1, %%mm0 \n\t"
        "pswapd    %%mm0, %%mm4 \n\t"
        "pxor      %%mm7, %%mm0 \n\t"
        "pxor      %%mm7, %%mm4 \n\t"
        "movq      %%mm3, -8(%3,%0) \n\t" // output[n-2-2*k] = { z[n8-1-k].im, -z[n8+k].re }
        "movq      %%mm4, -8(%2,%0) \n\t" // output[n2-2-2*k]= { -z[n8-1-k].re, z[n8+k].im }
        "neg %0 \n\t"
        "movq      %%mm0, (%1,%0) \n\t"   // output[2*k]     = { -z[n8+k].im, z[n8-1-k].re }
        "movq      %%mm2, (%2,%0) \n\t"   // output[n2+2*k]  = { -z[n8+k].re, z[n8-1-k].im }
        "sub $8, %0 \n\t"
        "jge 1b \n\t"
        :"+r"(k)
        :"r"(output), "r"(output+n2), "r"(output+n), "r"(z+n8)
        :"memory"
    );
    asm volatile("femms");
}

