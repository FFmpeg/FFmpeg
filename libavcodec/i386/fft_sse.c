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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "../dsputil.h"
#include <math.h>

#ifdef HAVE_BUILTIN_VECTOR

#include <xmmintrin.h>

static const float p1p1p1m1[4] __attribute__((aligned(16))) = 
    { 1.0, 1.0, 1.0, -1.0 };

static const float p1p1m1p1[4] __attribute__((aligned(16))) = 
    { 1.0, 1.0, -1.0, 1.0 };

static const float p1p1m1m1[4] __attribute__((aligned(16))) = 
    { 1.0, 1.0, -1.0, -1.0 };

#if 0
static void print_v4sf(const char *str, __m128 a)
{
    float *p = (float *)&a;
    printf("%s: %f %f %f %f\n",
           str, p[0], p[1], p[2], p[3]);
}
#endif

/* XXX: handle reverse case */
void fft_calc_sse(FFTContext *s, FFTComplex *z)
{
    int ln = s->nbits;
    int	j, np, np2;
    int	nblocks, nloops;
    register FFTComplex *p, *q;
    FFTComplex *cptr, *cptr1;
    int k;

    np = 1 << ln;

    {
        __m128 *r, a, b, a1, c1, c2;

        r = (__m128 *)&z[0];
        c1 = *(__m128 *)p1p1m1m1;
        c2 = *(__m128 *)p1p1p1m1;
        if (s->inverse)
            c2 = *(__m128 *)p1p1m1p1;
        else
            c2 = *(__m128 *)p1p1p1m1;

        j = (np >> 2);
        do {
            a = r[0];
            b = _mm_shuffle_ps(a, a, _MM_SHUFFLE(1, 0, 3, 2));
            a = _mm_mul_ps(a, c1);
            /* do the pass 0 butterfly */
            a = _mm_add_ps(a, b);

            a1 = r[1];
            b = _mm_shuffle_ps(a1, a1, _MM_SHUFFLE(1, 0, 3, 2));
            a1 = _mm_mul_ps(a1, c1);
            /* do the pass 0 butterfly */
            b = _mm_add_ps(a1, b);

            /* multiply third by -i */
            b = _mm_shuffle_ps(b, b, _MM_SHUFFLE(2, 3, 1, 0));
            b = _mm_mul_ps(b, c2);

            /* do the pass 1 butterfly */
            r[0] = _mm_add_ps(a, b);
            r[1] = _mm_sub_ps(a, b);
            r += 2;
        } while (--j != 0);
    }
    /* pass 2 .. ln-1 */

    nblocks = np >> 3;
    nloops = 1 << 2;
    np2 = np >> 1;

    cptr1 = s->exptab1;
    do {
        p = z;
        q = z + nloops;
        j = nblocks;
        do {
            cptr = cptr1;
            k = nloops >> 1;
            do {
                __m128 a, b, c, t1, t2;

                a = *(__m128 *)p;
                b = *(__m128 *)q;
                
                /* complex mul */
                c = *(__m128 *)cptr;
                /*  cre*re cim*re */
                t1 = _mm_mul_ps(c, 
                                _mm_shuffle_ps(b, b, _MM_SHUFFLE(2, 2, 0, 0))); 
                c = *(__m128 *)(cptr + 2);
                /*  -cim*im cre*im */
                t2 = _mm_mul_ps(c,
                                _mm_shuffle_ps(b, b, _MM_SHUFFLE(3, 3, 1, 1))); 
                b = _mm_add_ps(t1, t2);
                
                /* butterfly */
                *(__m128 *)p = _mm_add_ps(a, b);
                *(__m128 *)q = _mm_sub_ps(a, b);
                
                p += 2;
                q += 2;
                cptr += 4;
            } while (--k);
        
            p += nloops;
            q += nloops;
        } while (--j);
        cptr1 += nloops * 2;
        nblocks = nblocks >> 1;
        nloops = nloops << 1;
    } while (nblocks != 0);
}

#endif
