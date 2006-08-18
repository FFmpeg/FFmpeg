/*
 * FFT/MDCT transform with SSE optimizations
 * Copyright (c) 2002 Fabrice Bellard.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */
#include "../dsputil.h"

static const int p1p1p1m1[4] __attribute__((aligned(16))) =
    { 0, 0, 0, 1 << 31 };

static const int p1p1m1p1[4] __attribute__((aligned(16))) =
    { 0, 0, 1 << 31, 0 };

static const int p1p1m1m1[4] __attribute__((aligned(16))) =
    { 0, 0, 1 << 31, 1 << 31 };

#if 0
static void print_v4sf(const char *str, __m128 a)
{
    float *p = (float *)&a;
    printf("%s: %f %f %f %f\n",
           str, p[0], p[1], p[2], p[3]);
}
#endif

/* XXX: handle reverse case */
void ff_fft_calc_sse(FFTContext *s, FFTComplex *z)
{
    int ln = s->nbits;
    long i, j;
    long nblocks, nloops;
    FFTComplex *p, *cptr;

    asm volatile(
        "movaps %0, %%xmm4 \n\t"
        "movaps %1, %%xmm5 \n\t"
        ::"m"(*p1p1m1m1),
          "m"(*(s->inverse ? p1p1m1p1 : p1p1p1m1))
    );

    i = 8 << ln;
    asm volatile(
        "1: \n\t"
        "sub $32, %0 \n\t"
        /* do the pass 0 butterfly */
        "movaps   (%0,%1), %%xmm0 \n\t"
        "movaps    %%xmm0, %%xmm1 \n\t"
        "shufps     $0x4E, %%xmm0, %%xmm0 \n\t"
        "xorps     %%xmm4, %%xmm1 \n\t"
        "addps     %%xmm1, %%xmm0 \n\t"
        "movaps 16(%0,%1), %%xmm2 \n\t"
        "movaps    %%xmm2, %%xmm3 \n\t"
        "shufps     $0x4E, %%xmm2, %%xmm2 \n\t"
        "xorps     %%xmm4, %%xmm3 \n\t"
        "addps     %%xmm3, %%xmm2 \n\t"
        /* multiply third by -i */
        /* by toggling the sign bit */
        "shufps     $0xB4, %%xmm2, %%xmm2 \n\t"
        "xorps     %%xmm5, %%xmm2 \n\t"
        /* do the pass 1 butterfly */
        "movaps    %%xmm0, %%xmm1 \n\t"
        "addps     %%xmm2, %%xmm0 \n\t"
        "subps     %%xmm2, %%xmm1 \n\t"
        "movaps    %%xmm0,   (%0,%1) \n\t"
        "movaps    %%xmm1, 16(%0,%1) \n\t"
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
                "movaps    (%2,%0), %%xmm1 \n\t"
                "movaps    (%1,%0), %%xmm0 \n\t"
                "movaps     %%xmm1, %%xmm2 \n\t"
                "shufps      $0xA0, %%xmm1, %%xmm1 \n\t"
                "shufps      $0xF5, %%xmm2, %%xmm2 \n\t"
                "mulps   (%3,%0,2), %%xmm1 \n\t" //  cre*re cim*re
                "mulps 16(%3,%0,2), %%xmm2 \n\t" // -cim*im cre*im
                "addps      %%xmm2, %%xmm1 \n\t"
                "movaps     %%xmm0, %%xmm3 \n\t"
                "addps      %%xmm1, %%xmm0 \n\t"
                "subps      %%xmm1, %%xmm3 \n\t"
                "movaps     %%xmm0, (%1,%0) \n\t"
                "movaps     %%xmm3, (%2,%0) \n\t"
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
}

