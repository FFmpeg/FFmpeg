/*
 * FFT/MDCT transform with Extended 3DNow! optimizations
 * Copyright (c) 2006-2008 Zuxy MENG Jie, Loren Merritt
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
#include "fft.h"

DECLARE_ALIGNED(8, static const unsigned int, m1m1)[2] = { 1U<<31, 1U<<31 };

#ifdef EMULATE_3DNOWEXT
#define PSWAPD(s,d)\
    "movq "#s","#d"\n"\
    "psrlq $32,"#d"\n"\
    "punpckldq "#s","#d"\n"
#define ff_fft_calc_3dn2 ff_fft_calc_3dn
#define ff_fft_dispatch_3dn2 ff_fft_dispatch_3dn
#define ff_fft_dispatch_interleave_3dn2 ff_fft_dispatch_interleave_3dn
#define ff_imdct_calc_3dn2 ff_imdct_calc_3dn
#define ff_imdct_half_3dn2 ff_imdct_half_3dn
#else
#define PSWAPD(s,d) "pswapd "#s","#d"\n"
#endif

void ff_fft_dispatch_3dn2(FFTComplex *z, int nbits);
void ff_fft_dispatch_interleave_3dn2(FFTComplex *z, int nbits);

void ff_fft_calc_3dn2(FFTContext *s, FFTComplex *z)
{
    int n = 1<<s->nbits;
    int i;
    ff_fft_dispatch_interleave_3dn2(z, s->nbits);
    __asm__ volatile("femms");
    if(n <= 8)
        for(i=0; i<n; i+=2)
            FFSWAP(FFTSample, z[i].im, z[i+1].re);
}

void ff_imdct_half_3dn2(FFTContext *s, FFTSample *output, const FFTSample *input)
{
    x86_reg j, k;
    long n = s->mdct_size;
    long n2 = n >> 1;
    long n4 = n >> 2;
    long n8 = n >> 3;
    const uint16_t *revtab = s->revtab;
    const FFTSample *tcos = s->tcos;
    const FFTSample *tsin = s->tsin;
    const FFTSample *in1, *in2;
    FFTComplex *z = (FFTComplex *)output;

    /* pre rotation */
    in1 = input;
    in2 = input + n2 - 1;
#ifdef EMULATE_3DNOWEXT
    __asm__ volatile("movd %0, %%mm7" ::"r"(1U<<31));
#endif
    for(k = 0; k < n4; k++) {
        // FIXME a single block is faster, but gcc 2.95 and 3.4.x on 32bit can't compile it
        __asm__ volatile(
            "movd         %0, %%mm0 \n"
            "movd         %2, %%mm1 \n"
            "punpckldq    %1, %%mm0 \n"
            "punpckldq    %3, %%mm1 \n"
            "movq      %%mm0, %%mm2 \n"
            PSWAPD(    %%mm1, %%mm3 )
            "pfmul     %%mm1, %%mm0 \n"
            "pfmul     %%mm3, %%mm2 \n"
#ifdef EMULATE_3DNOWEXT
            "movq      %%mm0, %%mm1 \n"
            "punpckhdq %%mm2, %%mm0 \n"
            "punpckldq %%mm2, %%mm1 \n"
            "pxor      %%mm7, %%mm0 \n"
            "pfadd     %%mm1, %%mm0 \n"
#else
            "pfpnacc   %%mm2, %%mm0 \n"
#endif
            ::"m"(in2[-2*k]), "m"(in1[2*k]),
              "m"(tcos[k]), "m"(tsin[k])
        );
        __asm__ volatile(
            "movq    %%mm0, %0    \n\t"
            :"=m"(z[revtab[k]])
        );
    }

    ff_fft_dispatch_3dn2(z, s->nbits);

#define CMUL(j,mm0,mm1)\
        "movq  (%2,"#j",2), %%mm6 \n"\
        "movq 8(%2,"#j",2), "#mm0"\n"\
        "movq        %%mm6, "#mm1"\n"\
        "movq        "#mm0",%%mm7 \n"\
        "pfmul   (%3,"#j"), %%mm6 \n"\
        "pfmul   (%4,"#j"), "#mm0"\n"\
        "pfmul   (%4,"#j"), "#mm1"\n"\
        "pfmul   (%3,"#j"), %%mm7 \n"\
        "pfsub       %%mm6, "#mm0"\n"\
        "pfadd       %%mm7, "#mm1"\n"

    /* post rotation */
    j = -n2;
    k = n2-8;
    __asm__ volatile(
        "1: \n"
        CMUL(%0, %%mm0, %%mm1)
        CMUL(%1, %%mm2, %%mm3)
        "movd   %%mm0,  (%2,%0,2) \n"
        "movd   %%mm1,12(%2,%1,2) \n"
        "movd   %%mm2,  (%2,%1,2) \n"
        "movd   %%mm3,12(%2,%0,2) \n"
        "psrlq  $32,   %%mm0 \n"
        "psrlq  $32,   %%mm1 \n"
        "psrlq  $32,   %%mm2 \n"
        "psrlq  $32,   %%mm3 \n"
        "movd   %%mm0, 8(%2,%0,2) \n"
        "movd   %%mm1, 4(%2,%1,2) \n"
        "movd   %%mm2, 8(%2,%1,2) \n"
        "movd   %%mm3, 4(%2,%0,2) \n"
        "sub $8, %1 \n"
        "add $8, %0 \n"
        "jl 1b \n"
        :"+r"(j), "+r"(k)
        :"r"(z+n8), "r"(tcos+n8), "r"(tsin+n8)
        :"memory"
    );
    __asm__ volatile("femms");
}

void ff_imdct_calc_3dn2(FFTContext *s, FFTSample *output, const FFTSample *input)
{
    x86_reg j, k;
    long n = s->mdct_size;
    long n4 = n >> 2;

    ff_imdct_half_3dn2(s, output+n4, input);

    j = -n;
    k = n-8;
    __asm__ volatile(
        "movq %4, %%mm7 \n"
        "1: \n"
        PSWAPD((%2,%1), %%mm0)
        PSWAPD((%3,%0), %%mm1)
        "pxor    %%mm7, %%mm0 \n"
        "movq    %%mm1, (%3,%1) \n"
        "movq    %%mm0, (%2,%0) \n"
        "sub $8, %1 \n"
        "add $8, %0 \n"
        "jl 1b \n"
        :"+r"(j), "+r"(k)
        :"r"(output+n4), "r"(output+n4*3),
         "m"(*m1m1)
    );
    __asm__ volatile("femms");
}

