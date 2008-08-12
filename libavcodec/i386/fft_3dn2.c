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

#include "libavutil/x86_cpu.h"
#include "libavcodec/dsputil.h"

#ifdef EMULATE_3DNOWEXT
#define ff_fft_calc_3dn2 ff_fft_calc_3dn
#define ff_fft_dispatch_3dn2 ff_fft_dispatch_3dn
#define ff_fft_dispatch_interleave_3dn2 ff_fft_dispatch_interleave_3dn
#define ff_imdct_calc_3dn2 ff_imdct_calc_3dn
#define ff_imdct_half_3dn2 ff_imdct_half_3dn
#endif

void ff_fft_dispatch_3dn2(FFTComplex *z, int nbits);
void ff_fft_dispatch_interleave_3dn2(FFTComplex *z, int nbits);

void ff_fft_calc_3dn2(FFTContext *s, FFTComplex *z)
{
    int n = 1<<s->nbits;
    int i;
    ff_fft_dispatch_interleave_3dn2(z, s->nbits);
    asm volatile("femms");
    if(n <= 8)
        for(i=0; i<n; i+=2)
            FFSWAP(FFTSample, z[i].im, z[i+1].re);
}

static void imdct_3dn2(MDCTContext *s, const FFTSample *input, FFTSample *tmp)
{
    long n4, n2, n;
    x86_reg k;
    const uint16_t *revtab = s->fft.revtab;
    const FFTSample *tcos = s->tcos;
    const FFTSample *tsin = s->tsin;
    const FFTSample *in1, *in2;
    FFTComplex *z = (FFTComplex *)tmp;

    n = 1 << s->nbits;
    n2 = n >> 1;
    n4 = n >> 2;

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

    ff_fft_calc_3dn2(&s->fft, z);

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
}

void ff_imdct_calc_3dn2(MDCTContext *s, FFTSample *output,
                        const FFTSample *input, FFTSample *tmp)
{
    x86_reg k;
    long n8, n2, n;
    FFTComplex *z = (FFTComplex *)tmp;

    n = 1 << s->nbits;
    n2 = n >> 1;
    n8 = n >> 3;

    imdct_3dn2(s, input, tmp);

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

void ff_imdct_half_3dn2(MDCTContext *s, FFTSample *output,
                        const FFTSample *input, FFTSample *tmp)
{
    x86_reg j, k;
    long n8, n4, n;
    FFTComplex *z = (FFTComplex *)tmp;

    n = 1 << s->nbits;
    n4 = n >> 2;
    n8 = n >> 3;

    imdct_3dn2(s, input, tmp);

    j = -n;
    k = n-8;
    asm volatile("movd %0, %%mm7" ::"r"(1<<31));
    asm volatile(
        "1: \n\t"
        "movq    (%3,%1), %%mm0 \n\t" // z[n8+k]
        "pswapd  (%3,%0), %%mm1 \n\t" // z[n8-1-k]
        "movq      %%mm0, %%mm2 \n\t"
        "punpckldq %%mm1, %%mm0 \n\t"
        "punpckhdq %%mm2, %%mm1 \n\t"
        "pxor      %%mm7, %%mm0 \n\t"
        "pxor      %%mm7, %%mm1 \n\t"
        "movq      %%mm0, (%2,%1) \n\t" // output[n4+2*k]   = { -z[n8+k].re, z[n8-1-k].im }
        "movq      %%mm1, (%2,%0) \n\t" // output[n4-2-2*k] = { -z[n8-1-k].re, z[n8+k].im }
        "sub $8, %1 \n\t"
        "add $8, %0 \n\t"
        "jl 1b \n\t"
        :"+r"(j), "+r"(k)
        :"r"(output+n4), "r"(z+n8)
        :"memory"
    );
    asm volatile("femms");
}

