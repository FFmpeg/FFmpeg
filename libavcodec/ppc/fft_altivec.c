/*
 * FFT/IFFT transforms
 * AltiVec-enabled
 * Copyright (c) 2002 Romain Dolbeau <romain@dolbeau.org>
 * Based on code Copyright (c) 2002 Fabrice Bellard.
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

#include "dsputil_altivec.h"

// used to build registers permutation vectors (vcprm)
// the 's' are for words in the _s_econd vector
#define WORD_0 0x00,0x01,0x02,0x03
#define WORD_1 0x04,0x05,0x06,0x07
#define WORD_2 0x08,0x09,0x0a,0x0b
#define WORD_3 0x0c,0x0d,0x0e,0x0f
#define WORD_s0 0x10,0x11,0x12,0x13
#define WORD_s1 0x14,0x15,0x16,0x17
#define WORD_s2 0x18,0x19,0x1a,0x1b
#define WORD_s3 0x1c,0x1d,0x1e,0x1f

#define vcprm(a,b,c,d) (const vector unsigned char)(WORD_ ## a, WORD_ ## b, WORD_ ## c, WORD_ ## d)

// vcprmle is used to keep the same index as in the SSE version.
// it's the same as vcprm, with the index inversed
// ('le' is Little Endian)
#define vcprmle(a,b,c,d) vcprm(d,c,b,a)

// used to build inverse/identity vectors (vcii)
// n is _n_egative, p is _p_ositive
#define FLOAT_n -1.
#define FLOAT_p 1.

#define vcii(a,b,c,d) (const vector float)(FLOAT_ ## a, FLOAT_ ## b, FLOAT_ ## c, FLOAT_ ## d)

/**
 * Do a complex FFT with the parameters defined in fft_init(). The
 * input data must be permuted before with s->revtab table. No
 * 1.0/sqrt(n) normalization is done.
 * AltiVec-enabled
 * This code assumes that the 'z' pointer is 16 bytes-aligned
 * It also assumes all FFTComplex are 8 bytes-aligned pair of float
 * The code is exactly the same as the SSE version, except
 * that successive MUL + ADD/SUB have been fusionned into
 * fused multiply-add ('vec_madd' in altivec)
 *
 * To test this code you can use fft-test in libavcodec ; use
 * the following line in libavcodec to compile (MacOS X):
 * #####
 * gcc -I. -Ippc -no-cpp-precomp -pipe -O3 -fomit-frame-pointer -mdynamic-no-pic -Wall
 *     -faltivec -DARCH_POWERPC -DHAVE_ALTIVEC -DCONFIG_DARWIN fft-test.c fft.c
 *     ppc/fft_altivec.c ppc/dsputil_altivec.c mdct.c -DHAVE_LRINTF -o fft-test
 * #####
 */
void fft_calc_altivec(FFTContext *s, FFTComplex *z)
{
    register const vector float vczero = (vector float)( 0., 0., 0., 0.);
    
    int ln = s->nbits;
    int	j, np, np2;
    int	nblocks, nloops;
    register FFTComplex *p, *q;
    FFTComplex *cptr, *cptr1;
    int k;

    np = 1 << ln;

    {
        vector float *r, a, b, a1, c1, c2;

        r = (vector float *)&z[0];

        c1 = vcii(p,p,n,n);
        
        if (s->inverse)
            {
                c2 = vcii(p,p,n,p);
            }
        else
            {
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
}

