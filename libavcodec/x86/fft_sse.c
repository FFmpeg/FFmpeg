/*
 * FFT/MDCT transform with SSE optimizations
 * Copyright (c) 2008 Loren Merritt
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

DECLARE_ASM_CONST(16, int, ff_m1m1m1m1)[4] =
    { 1 << 31, 1 << 31, 1 << 31, 1 << 31 };

void ff_fft_dispatch_sse(FFTComplex *z, int nbits);
void ff_fft_dispatch_interleave_sse(FFTComplex *z, int nbits);

void ff_fft_calc_sse(FFTContext *s, FFTComplex *z)
{
    int n = 1 << s->nbits;

    ff_fft_dispatch_interleave_sse(z, s->nbits);

    if(n <= 16) {
        x86_reg i = -8*n;
        __asm__ volatile(
            "1: \n"
            "movaps     (%0,%1), %%xmm0 \n"
            "movaps      %%xmm0, %%xmm1 \n"
            "unpcklps 16(%0,%1), %%xmm0 \n"
            "unpckhps 16(%0,%1), %%xmm1 \n"
            "movaps      %%xmm0,   (%0,%1) \n"
            "movaps      %%xmm1, 16(%0,%1) \n"
            "add $32, %0 \n"
            "jl 1b \n"
            :"+r"(i)
            :"r"(z+n)
            :"memory"
        );
    }
}

void ff_fft_permute_sse(FFTContext *s, FFTComplex *z)
{
    int n = 1 << s->nbits;
    int i;
    for(i=0; i<n; i+=2) {
        __asm__ volatile(
            "movaps %2, %%xmm0 \n"
            "movlps %%xmm0, %0 \n"
            "movhps %%xmm0, %1 \n"
            :"=m"(s->tmp_buf[s->revtab[i]]),
             "=m"(s->tmp_buf[s->revtab[i+1]])
            :"m"(z[i])
        );
    }
    memcpy(z, s->tmp_buf, n*sizeof(FFTComplex));
}

void ff_imdct_half_sse(FFTContext *s, FFTSample *output, const FFTSample *input)
{
    av_unused x86_reg i, j, k, l;
    long n = 1 << s->mdct_bits;
    long n2 = n >> 1;
    long n4 = n >> 2;
    long n8 = n >> 3;
    const uint16_t *revtab = s->revtab + n8;
    const FFTSample *tcos = s->tcos;
    const FFTSample *tsin = s->tsin;
    FFTComplex *z = (FFTComplex *)output;

    /* pre rotation */
    for(k=n8-2; k>=0; k-=2) {
        __asm__ volatile(
            "movaps     (%2,%1,2), %%xmm0 \n" // { z[k].re,    z[k].im,    z[k+1].re,  z[k+1].im  }
            "movaps  -16(%2,%0,2), %%xmm1 \n" // { z[-k-2].re, z[-k-2].im, z[-k-1].re, z[-k-1].im }
            "movaps        %%xmm0, %%xmm2 \n"
            "shufps $0x88, %%xmm1, %%xmm0 \n" // { z[k].re,    z[k+1].re,  z[-k-2].re, z[-k-1].re }
            "shufps $0x77, %%xmm2, %%xmm1 \n" // { z[-k-1].im, z[-k-2].im, z[k+1].im,  z[k].im    }
            "movlps       (%3,%1), %%xmm4 \n"
            "movlps       (%4,%1), %%xmm5 \n"
            "movhps     -8(%3,%0), %%xmm4 \n" // { cos[k],     cos[k+1],   cos[-k-2],  cos[-k-1]  }
            "movhps     -8(%4,%0), %%xmm5 \n" // { sin[k],     sin[k+1],   sin[-k-2],  sin[-k-1]  }
            "movaps        %%xmm0, %%xmm2 \n"
            "movaps        %%xmm1, %%xmm3 \n"
            "mulps         %%xmm5, %%xmm0 \n" // re*sin
            "mulps         %%xmm4, %%xmm1 \n" // im*cos
            "mulps         %%xmm4, %%xmm2 \n" // re*cos
            "mulps         %%xmm5, %%xmm3 \n" // im*sin
            "subps         %%xmm0, %%xmm1 \n" // -> re
            "addps         %%xmm3, %%xmm2 \n" // -> im
            "movaps        %%xmm1, %%xmm0 \n"
            "unpcklps      %%xmm2, %%xmm1 \n" // { z[k],    z[k+1]  }
            "unpckhps      %%xmm2, %%xmm0 \n" // { z[-k-2], z[-k-1] }
            ::"r"(-4*k), "r"(4*k),
              "r"(input+n4), "r"(tcos+n8), "r"(tsin+n8)
        );
#if ARCH_X86_64
        // if we have enough regs, don't let gcc make the luts latency-bound
        // but if not, latency is faster than spilling
        __asm__("movlps %%xmm0, %0 \n"
            "movhps %%xmm0, %1 \n"
            "movlps %%xmm1, %2 \n"
            "movhps %%xmm1, %3 \n"
            :"=m"(z[revtab[-k-2]]),
             "=m"(z[revtab[-k-1]]),
             "=m"(z[revtab[ k  ]]),
             "=m"(z[revtab[ k+1]])
        );
#else
        __asm__("movlps %%xmm0, %0" :"=m"(z[revtab[-k-2]]));
        __asm__("movhps %%xmm0, %0" :"=m"(z[revtab[-k-1]]));
        __asm__("movlps %%xmm1, %0" :"=m"(z[revtab[ k  ]]));
        __asm__("movhps %%xmm1, %0" :"=m"(z[revtab[ k+1]]));
#endif
    }

    ff_fft_dispatch_sse(z, s->nbits);

    /* post rotation + reinterleave + reorder */

#define CMUL(j,xmm0,xmm1)\
        "movaps   (%2,"#j",2), %%xmm6 \n"\
        "movaps 16(%2,"#j",2), "#xmm0"\n"\
        "movaps        %%xmm6, "#xmm1"\n"\
        "movaps        "#xmm0",%%xmm7 \n"\
        "mulps      (%3,"#j"), %%xmm6 \n"\
        "mulps      (%4,"#j"), "#xmm0"\n"\
        "mulps      (%4,"#j"), "#xmm1"\n"\
        "mulps      (%3,"#j"), %%xmm7 \n"\
        "subps         %%xmm6, "#xmm0"\n"\
        "addps         %%xmm7, "#xmm1"\n"

    j = -n2;
    k = n2-16;
    __asm__ volatile(
        "1: \n"
        CMUL(%0, %%xmm0, %%xmm1)
        CMUL(%1, %%xmm4, %%xmm5)
        "shufps    $0x1b, %%xmm1, %%xmm1 \n"
        "shufps    $0x1b, %%xmm5, %%xmm5 \n"
        "movaps   %%xmm4, %%xmm6 \n"
        "unpckhps %%xmm1, %%xmm4 \n"
        "unpcklps %%xmm1, %%xmm6 \n"
        "movaps   %%xmm0, %%xmm2 \n"
        "unpcklps %%xmm5, %%xmm0 \n"
        "unpckhps %%xmm5, %%xmm2 \n"
        "movaps   %%xmm6,   (%2,%1,2) \n"
        "movaps   %%xmm4, 16(%2,%1,2) \n"
        "movaps   %%xmm0,   (%2,%0,2) \n"
        "movaps   %%xmm2, 16(%2,%0,2) \n"
        "sub $16, %1 \n"
        "add $16, %0 \n"
        "jl 1b \n"
        :"+&r"(j), "+&r"(k)
        :"r"(z+n8), "r"(tcos+n8), "r"(tsin+n8)
        :"memory"
    );
}

void ff_imdct_calc_sse(FFTContext *s, FFTSample *output, const FFTSample *input)
{
    x86_reg j, k;
    long n = 1 << s->mdct_bits;
    long n4 = n >> 2;

    ff_imdct_half_sse(s, output+n4, input);

    j = -n;
    k = n-16;
    __asm__ volatile(
        "movaps "MANGLE(ff_m1m1m1m1)", %%xmm7 \n"
        "1: \n"
        "movaps       (%2,%1), %%xmm0 \n"
        "movaps       (%3,%0), %%xmm1 \n"
        "shufps $0x1b, %%xmm0, %%xmm0 \n"
        "shufps $0x1b, %%xmm1, %%xmm1 \n"
        "xorps         %%xmm7, %%xmm0 \n"
        "movaps        %%xmm1, (%3,%1) \n"
        "movaps        %%xmm0, (%2,%0) \n"
        "sub $16, %1 \n"
        "add $16, %0 \n"
        "jl 1b \n"
        :"+r"(j), "+r"(k)
        :"r"(output+n4), "r"(output+n4*3)
    );
}

