/*
 * FFT/IFFT transforms
 * AltiVec-enabled
 * Copyright (c) 2003 Romain Dolbeau <romain@dolbeau.org>
 * Based on code Copyright (c) 2002 Fabrice Bellard
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
#include "libavcodec/dsputil.h"
#include "dsputil_ppc.h"
#include "util_altivec.h"
/**
 * Do a complex FFT with the parameters defined in ff_fft_init(). The
 * input data must be permuted before with s->revtab table. No
 * 1.0/sqrt(n) normalization is done.
 * AltiVec-enabled
 * This code assumes that the 'z' pointer is 16 bytes-aligned
 * It also assumes all FFTComplex are 8 bytes-aligned pair of float
 * The code is exactly the same as the SSE version, except
 * that successive MUL + ADD/SUB have been merged into
 * fused multiply-add ('vec_madd' in altivec)
 */
void ff_fft_calc_altivec(FFTContext *s, FFTComplex *z)
{
POWERPC_PERF_DECLARE(altivec_fft_num, s->nbits >= 6);
    register const vector float vczero = (const vector float)vec_splat_u32(0.);

    int ln = s->nbits;
    int j, np, np2;
    int nblocks, nloops;
    register FFTComplex *p, *q;
    FFTComplex *cptr, *cptr1;
    int k;

POWERPC_PERF_START_COUNT(altivec_fft_num, s->nbits >= 6);

    np = 1 << ln;

    {
        vector float *r, a, b, a1, c1, c2;

        r = (vector float *)&z[0];

        c1 = vcii(p,p,n,n);

        if (s->inverse) {
            c2 = vcii(p,p,n,p);
        } else {
            c2 = vcii(p,p,p,n);
        }

        j = (np >> 2);
        do {
            a = vec_ld(0, r);
            a1 = vec_ld(sizeof(vector float), r);

            b = vec_perm(a,a,vcprmle(1,0,3,2));
            a = vec_madd(a,c1,b);
            /* do the pass 0 butterfly */

            b = vec_perm(a1,a1,vcprmle(1,0,3,2));
            b = vec_madd(a1,c1,b);
            /* do the pass 0 butterfly */

            /* multiply third by -i */
            b = vec_perm(b,b,vcprmle(2,3,1,0));

            /* do the pass 1 butterfly */
            vec_st(vec_madd(b,c2,a), 0, r);
            vec_st(vec_nmsub(b,c2,a), sizeof(vector float), r);

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
                vector float a,b,c,t1;

                a = vec_ld(0, (float*)p);
                b = vec_ld(0, (float*)q);

                /* complex mul */
                c = vec_ld(0, (float*)cptr);
                /*  cre*re cim*re */
                t1 = vec_madd(c, vec_perm(b,b,vcprmle(2,2,0,0)),vczero);
                c = vec_ld(sizeof(vector float), (float*)cptr);
                /*  -cim*im cre*im */
                b = vec_madd(c, vec_perm(b,b,vcprmle(3,3,1,1)),t1);

                /* butterfly */
                vec_st(vec_add(a,b), 0, (float*)p);
                vec_st(vec_sub(a,b), 0, (float*)q);

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

POWERPC_PERF_STOP_COUNT(altivec_fft_num, s->nbits >= 6);
}
