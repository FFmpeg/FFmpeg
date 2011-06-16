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
#include "config.h"

DECLARE_ASM_CONST(16, int, ff_m1m1m1m1)[4] =
    { 1 << 31, 1 << 31, 1 << 31, 1 << 31 };

void ff_fft_dispatch_sse(FFTComplex *z, int nbits);
void ff_fft_dispatch_interleave_sse(FFTComplex *z, int nbits);
void ff_fft_dispatch_interleave_avx(FFTComplex *z, int nbits);

#if HAVE_AVX
void ff_fft_calc_avx(FFTContext *s, FFTComplex *z)
{
    ff_fft_dispatch_interleave_avx(z, s->nbits);
}
#endif

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

void ff_imdct_calc_sse(FFTContext *s, FFTSample *output, const FFTSample *input)
{
    x86_reg j, k;
    long n = s->mdct_size;
    long n4 = n >> 2;

    s->imdct_half(s, output + n4, input);

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
        XMM_CLOBBERS_ONLY("%xmm0", "%xmm1", "%xmm7")
    );
}

