/*
 * FFT/IFFT transforms
 * AltiVec-enabled
 * Copyright (c) 2003 Romain Dolbeau <romain@dolbeau.org>
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

#include "gcc_fixes.h"

#include "dsputil_altivec.h"

/*
  those three macros are from libavcodec/fft.c
  and are required for the reference C code
*/
/* butter fly op */
#define BF(pre, pim, qre, qim, pre1, pim1, qre1, qim1) \
{\
  FFTSample ax, ay, bx, by;\
  bx=pre1;\
  by=pim1;\
  ax=qre1;\
  ay=qim1;\
  pre = (bx + ax);\
  pim = (by + ay);\
  qre = (bx - ax);\
  qim = (by - ay);\
}
#define MUL16(a,b) ((a) * (b))
#define CMUL(pre, pim, are, aim, bre, bim) \
{\
   pre = (MUL16(are, bre) - MUL16(aim, bim));\
   pim = (MUL16(are, bim) + MUL16(bre, aim));\
}


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
#ifdef ALTIVEC_USE_REFERENCE_C_CODE
    int ln = s->nbits;
    int	j, np, np2;
    int	nblocks, nloops;
    register FFTComplex *p, *q;
    FFTComplex *exptab = s->exptab;
    int l;
    FFTSample tmp_re, tmp_im;
    
POWERPC_PERF_START_COUNT(altivec_fft_num, s->nbits >= 6);
 
    np = 1 << ln;

    /* pass 0 */

    p=&z[0];
    j=(np >> 1);
    do {
        BF(p[0].re, p[0].im, p[1].re, p[1].im, 
           p[0].re, p[0].im, p[1].re, p[1].im);
        p+=2;
    } while (--j != 0);

    /* pass 1 */

    
    p=&z[0];
    j=np >> 2;
    if (s->inverse) {
        do {
            BF(p[0].re, p[0].im, p[2].re, p[2].im, 
               p[0].re, p[0].im, p[2].re, p[2].im);
            BF(p[1].re, p[1].im, p[3].re, p[3].im, 
               p[1].re, p[1].im, -p[3].im, p[3].re);
            p+=4;
        } while (--j != 0);
    } else {
        do {
            BF(p[0].re, p[0].im, p[2].re, p[2].im, 
               p[0].re, p[0].im, p[2].re, p[2].im);
            BF(p[1].re, p[1].im, p[3].re, p[3].im, 
               p[1].re, p[1].im, p[3].im, -p[3].re);
            p+=4;
        } while (--j != 0);
    }
    /* pass 2 .. ln-1 */

    nblocks = np >> 3;
    nloops = 1 << 2;
    np2 = np >> 1;
    do {
        p = z;
        q = z + nloops;
        for (j = 0; j < nblocks; ++j) {
            BF(p->re, p->im, q->re, q->im,
               p->re, p->im, q->re, q->im);
            
            p++;
            q++;
            for(l = nblocks; l < np2; l += nblocks) {
                CMUL(tmp_re, tmp_im, exptab[l].re, exptab[l].im, q->re, q->im);
                BF(p->re, p->im, q->re, q->im,
                   p->re, p->im, tmp_re, tmp_im);
                p++;
                q++;
            }

            p += nloops;
            q += nloops;
        }
        nblocks = nblocks >> 1;
        nloops = nloops << 1;
    } while (nblocks != 0);

POWERPC_PERF_STOP_COUNT(altivec_fft_num, s->nbits >= 6);

#else /* ALTIVEC_USE_REFERENCE_C_CODE */
#ifdef CONFIG_DARWIN
    register const vector float vczero = (const vector float)(0.);
#else
    register const vector float vczero = (const vector float){0.,0.,0.,0.};
#endif
    
    int ln = s->nbits;
    int	j, np, np2;
    int	nblocks, nloops;
    register FFTComplex *p, *q;
    FFTComplex *cptr, *cptr1;
    int k;

POWERPC_PERF_START_COUNT(altivec_fft_num, s->nbits >= 6);

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

POWERPC_PERF_STOP_COUNT(altivec_fft_num, s->nbits >= 6);

#endif /* ALTIVEC_USE_REFERENCE_C_CODE */
}
