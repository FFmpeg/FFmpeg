/*
 * FFT/MDCT transform with SSE optimizations
 * Copyright (c) 2002 Fabrice Bellard.
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

static const int p1p1p1m1[4] __attribute__((aligned(16))) =
    { 0, 0, 0, 1 << 31 };

static const int p1p1m1p1[4] __attribute__((aligned(16))) =
    { 0, 0, 1 << 31, 0 };

static const int p1p1m1m1[4] __attribute__((aligned(16))) =
    { 0, 0, 1 << 31, 1 << 31 };

static const int p1m1p1m1[4] __attribute__((aligned(16))) =
    { 0, 1 << 31, 0, 1 << 31 };

static const int m1m1m1m1[4] __attribute__((aligned(16))) =
    { 1 << 31, 1 << 31, 1 << 31, 1 << 31 };

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

void ff_imdct_calc_sse(MDCTContext *s, FFTSample *output,
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

    asm volatile ("movaps %0, %%xmm7\n\t"::"m"(*p1m1p1m1));

    /* pre rotation */
    in1 = input;
    in2 = input + n2 - 4;

    /* Complex multiplication
       Two complex products per iteration, we could have 4 with 8 xmm
       registers, 8 with 16 xmm registers.
       Maybe we should unroll more.
    */
    for (k = 0; k < n4; k += 2) {
        asm volatile (
            "movaps          %0, %%xmm0 \n\t"   // xmm0 = r0 X  r1 X : in2
            "movaps          %1, %%xmm3 \n\t"   // xmm3 = X  i1 X  i0: in1
            "movlps          %2, %%xmm1 \n\t"   // xmm1 = X  X  R1 R0: tcos
            "movlps          %3, %%xmm2 \n\t"   // xmm2 = X  X  I1 I0: tsin
            "shufps $95, %%xmm0, %%xmm0 \n\t"   // xmm0 = r1 r1 r0 r0
            "shufps $160,%%xmm3, %%xmm3 \n\t"   // xmm3 = i1 i1 i0 i0
            "unpcklps    %%xmm2, %%xmm1 \n\t"   // xmm1 = I1 R1 I0 R0
            "movaps      %%xmm1, %%xmm2 \n\t"   // xmm2 = I1 R1 I0 R0
            "xorps       %%xmm7, %%xmm2 \n\t"   // xmm2 = -I1 R1 -I0 R0
            "mulps       %%xmm1, %%xmm0 \n\t"   // xmm0 = rI rR rI rR
            "shufps $177,%%xmm2, %%xmm2 \n\t"   // xmm2 = R1 -I1 R0 -I0
            "mulps       %%xmm2, %%xmm3 \n\t"   // xmm3 = Ri -Ii Ri -Ii
            "addps       %%xmm3, %%xmm0 \n\t"   // xmm0 = result
            ::"m"(in2[-2*k]), "m"(in1[2*k]),
              "m"(tcos[k]), "m"(tsin[k])
        );
        /* Should be in the same block, hack for gcc2.95 & gcc3 */
        asm (
            "movlps      %%xmm0, %0     \n\t"
            "movhps      %%xmm0, %1     \n\t"
            :"=m"(z[revtab[k]]), "=m"(z[revtab[k + 1]])
        );
    }

    ff_fft_calc_sse(&s->fft, z);

    /* Not currently needed, added for safety */
    asm volatile ("movaps %0, %%xmm7\n\t"::"m"(*p1m1p1m1));

    /* post rotation + reordering */
    for (k = 0; k < n4; k += 2) {
        asm (
            "movaps          %0, %%xmm0 \n\t"   // xmm0 = i1 r1 i0 r0: z
            "movlps          %1, %%xmm1 \n\t"   // xmm1 = X  X  R1 R0: tcos
            "movaps      %%xmm0, %%xmm3 \n\t"   // xmm3 = i1 r1 i0 r0
            "movlps          %2, %%xmm2 \n\t"   // xmm2 = X  X  I1 I0: tsin
            "shufps $160,%%xmm0, %%xmm0 \n\t"   // xmm0 = r1 r1 r0 r0
            "shufps $245,%%xmm3, %%xmm3 \n\t"   // xmm3 = i1 i1 i0 i0
            "unpcklps    %%xmm2, %%xmm1 \n\t"   // xmm1 = I1 R1 I0 R0
            "movaps      %%xmm1, %%xmm2 \n\t"   // xmm2 = I1 R1 I0 R0
            "xorps       %%xmm7, %%xmm2 \n\t"   // xmm2 = -I1 R1 -I0 R0
            "mulps       %%xmm1, %%xmm0 \n\t"   // xmm0 = rI rR rI rR
            "shufps $177,%%xmm2, %%xmm2 \n\t"   // xmm2 = R1 -I1 R0 -I0
            "mulps       %%xmm2, %%xmm3 \n\t"   // xmm3 = Ri -Ii Ri -Ii
            "addps       %%xmm3, %%xmm0 \n\t"   // xmm0 = result
            "movaps      %%xmm0, %0     \n\t"
            :"+m"(z[k])
            :"m"(tcos[k]), "m"(tsin[k])
        );
    }

    /*
       Mnemonics:
       0 = z[k].re
       1 = z[k].im
       2 = z[k + 1].re
       3 = z[k + 1].im
       4 = z[-k - 2].re
       5 = z[-k - 2].im
       6 = z[-k - 1].re
       7 = z[-k - 1].im
    */
    k = 16-n;
    asm volatile("movaps %0, %%xmm7 \n\t"::"m"(*m1m1m1m1));
    asm volatile(
        "1: \n\t"
        "movaps  -16(%4,%0), %%xmm1 \n\t"   // xmm1 = 4 5 6 7 = z[-2-k]
        "neg %0 \n\t"
        "movaps     (%4,%0), %%xmm0 \n\t"   // xmm0 = 0 1 2 3 = z[k]
        "xorps       %%xmm7, %%xmm0 \n\t"   // xmm0 = -0 -1 -2 -3
        "movaps      %%xmm0, %%xmm2 \n\t"   // xmm2 = -0 -1 -2 -3
        "shufps $141,%%xmm1, %%xmm0 \n\t"   // xmm0 = -1 -3 4 6
        "shufps $216,%%xmm1, %%xmm2 \n\t"   // xmm2 = -0 -2 5 7
        "shufps $156,%%xmm0, %%xmm0 \n\t"   // xmm0 = -1 6 -3 4 !
        "shufps $156,%%xmm2, %%xmm2 \n\t"   // xmm2 = -0 7 -2 5 !
        "movaps      %%xmm0, (%1,%0) \n\t"  // output[2*k]
        "movaps      %%xmm2, (%2,%0) \n\t"  // output[n2+2*k]
        "neg %0 \n\t"
        "shufps $27, %%xmm0, %%xmm0 \n\t"   // xmm0 = 4 -3 6 -1
        "xorps       %%xmm7, %%xmm0 \n\t"   // xmm0 = -4 3 -6 1 !
        "shufps $27, %%xmm2, %%xmm2 \n\t"   // xmm2 = 5 -2 7 -0 !
        "movaps      %%xmm0, -16(%2,%0) \n\t" // output[n2-4-2*k]
        "movaps      %%xmm2, -16(%3,%0) \n\t" // output[n-4-2*k]
        "add $16, %0 \n\t"
        "jle 1b \n\t"
        :"+r"(k)
        :"r"(output), "r"(output+n2), "r"(output+n), "r"(z+n8)
        :"memory"
    );
}

