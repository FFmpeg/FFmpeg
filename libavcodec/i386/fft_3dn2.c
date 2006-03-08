/*
 * FFT/MDCT transform with Extended 3DNow! optimizations
 * Copyright (c) 2006 Zuxy MENG Jie.
 * Based on fft_sse.c copyright (c) 2002 Fabrice Bellard.
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
#include <math.h>

#ifdef HAVE_MM3DNOW

#include <mm3dnow.h>

static const int p1m1[2] __attribute__((aligned(8))) =
    { 0, 1 << 31 };

static const int m1p1[2] __attribute__((aligned(8))) =
    { 1 << 31, 0 };

void ff_fft_calc_3dn2(FFTContext *s, FFTComplex *z)
{
    int ln = s->nbits;
    int j, np, np2;
    int nblocks, nloops;
    register FFTComplex *p, *q;
    FFTComplex *cptr, *cptr1;
    int k;

    np = 1 << ln;
    /* FEMMS is not a must here but recommended by AMD */
    _m_femms();

    {
        __m64 *r, a0, a1, b0, b1, c;

        r = (__m64 *)&z[0];
        if (s->inverse)
            c = *(__m64 *)m1p1;
        else
            c = *(__m64 *)p1m1;

        j = (np >> 2);
        do {
            /* do the pass 0 butterfly */
            a0 = _m_pfadd(r[0], r[1]);
            a1 = _m_pfsub(r[0], r[1]);

            /* do the pass 0 butterfly */
            b0 = _m_pfadd(r[2], r[3]);
            b1 = _m_pfsub(r[2], r[3]);

            /* multiply third by -i */
            b1 = _m_pswapd(b1);
            b1 = _m_pxor(b1, c);

            r[0] = _m_pfadd(a0, b0);
            r[1] = _m_pfadd(a1, b1);
            r[2] = _m_pfsub(a0, b0);
            r[3] = _m_pfsub(a1, b1);
            r += 4;
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
                __m64 a0, a1, b0, b1, c0, c1, t10, t11, t20, t21;

                a0 = *(__m64 *)&p[0];
                a1 = *(__m64 *)&p[1];
                b0 = *(__m64 *)&q[0];
                b1 = *(__m64 *)&q[1];

                /* complex mul */
                c0 = *(__m64 *)&cptr[0];
                c1 = *(__m64 *)&cptr[1];
                /* cre*re cim*im */
                t10 = _m_pfmul(c0, b0);
                t11 = _m_pfmul(c1, b1);
                /* no need to access cptr[2] & cptr[3] */
                c0 = _m_pswapd(c0);
                c1 = _m_pswapd(c1);
                /* cim*re cre*im */
                t20 = _m_pfmul(c0, b0);
                t21 = _m_pfmul(c1, b1);

                /* cre*re-cim*im cim*re+cre*im */
                b0 = _m_pfpnacc(t10, t20);
                b1 = _m_pfpnacc(t11, t21);

                /* butterfly */
                *(__m64 *)&p[0] = _m_pfadd(a0, b0);
                *(__m64 *)&p[1] = _m_pfadd(a1, b1);
                *(__m64 *)&q[0] = _m_pfsub(a0, b0);
                *(__m64 *)&q[1] = _m_pfsub(a1, b1);

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
    _m_femms();
}

#endif
