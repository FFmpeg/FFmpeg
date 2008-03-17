/*
 * FFT/IFFT transforms
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

/**
 * @file fft.c
 * FFT/IFFT transforms.
 */

#include "dsputil.h"

/**
 * The size of the FFT is 2^nbits. If inverse is TRUE, inverse FFT is
 * done
 */
int ff_fft_init(FFTContext *s, int nbits, int inverse)
{
    int i, j, m, n;
    float alpha, c1, s1, s2;
    int shuffle = 0;
    int av_unused has_vectors;

    s->nbits = nbits;
    n = 1 << nbits;

    s->exptab = av_malloc((n / 2) * sizeof(FFTComplex));
    if (!s->exptab)
        goto fail;
    s->revtab = av_malloc(n * sizeof(uint16_t));
    if (!s->revtab)
        goto fail;
    s->inverse = inverse;

    s2 = inverse ? 1.0 : -1.0;

    for(i=0;i<(n/2);i++) {
        alpha = 2 * M_PI * (float)i / (float)n;
        c1 = cos(alpha);
        s1 = sin(alpha) * s2;
        s->exptab[i].re = c1;
        s->exptab[i].im = s1;
    }
    s->fft_calc = ff_fft_calc_c;
    s->imdct_calc = ff_imdct_calc;
    s->exptab1 = NULL;

#ifdef HAVE_MMX
    has_vectors = mm_support();
    shuffle = 1;
    if (has_vectors & MM_3DNOWEXT) {
        /* 3DNowEx for K7/K8 */
        s->imdct_calc = ff_imdct_calc_3dn2;
        s->fft_calc = ff_fft_calc_3dn2;
    } else if (has_vectors & MM_3DNOW) {
        /* 3DNow! for K6-2/3 */
        s->fft_calc = ff_fft_calc_3dn;
    } else if (has_vectors & MM_SSE) {
        /* SSE for P3/P4 */
        s->imdct_calc = ff_imdct_calc_sse;
        s->fft_calc = ff_fft_calc_sse;
    } else {
        shuffle = 0;
    }
#elif defined HAVE_ALTIVEC && !defined ALTIVEC_USE_REFERENCE_C_CODE
    has_vectors = mm_support();
    if (has_vectors & MM_ALTIVEC) {
        s->fft_calc = ff_fft_calc_altivec;
        shuffle = 1;
    }
#endif

    /* compute constant table for HAVE_SSE version */
    if (shuffle) {
        int np, nblocks, np2, l;
        FFTComplex *q;

        np = 1 << nbits;
        nblocks = np >> 3;
        np2 = np >> 1;
        s->exptab1 = av_malloc(np * 2 * sizeof(FFTComplex));
        if (!s->exptab1)
            goto fail;
        q = s->exptab1;
        do {
            for(l = 0; l < np2; l += 2 * nblocks) {
                *q++ = s->exptab[l];
                *q++ = s->exptab[l + nblocks];

                q->re = -s->exptab[l].im;
                q->im = s->exptab[l].re;
                q++;
                q->re = -s->exptab[l + nblocks].im;
                q->im = s->exptab[l + nblocks].re;
                q++;
            }
            nblocks = nblocks >> 1;
        } while (nblocks != 0);
        av_freep(&s->exptab);
    }

    /* compute bit reverse table */

    for(i=0;i<n;i++) {
        m=0;
        for(j=0;j<nbits;j++) {
            m |= ((i >> j) & 1) << (nbits-j-1);
        }
        s->revtab[i]=m;
    }
    return 0;
 fail:
    av_freep(&s->revtab);
    av_freep(&s->exptab);
    av_freep(&s->exptab1);
    return -1;
}

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
 */
void ff_fft_calc_c(FFTContext *s, FFTComplex *z)
{
    int ln = s->nbits;
    int j, np, np2;
    int nblocks, nloops;
    register FFTComplex *p, *q;
    FFTComplex *exptab = s->exptab;
    int l;
    FFTSample tmp_re, tmp_im;

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
}

/**
 * Do the permutation needed BEFORE calling ff_fft_calc()
 */
void ff_fft_permute(FFTContext *s, FFTComplex *z)
{
    int j, k, np;
    FFTComplex tmp;
    const uint16_t *revtab = s->revtab;

    /* reverse */
    np = 1 << s->nbits;
    for(j=0;j<np;j++) {
        k = revtab[j];
        if (k < j) {
            tmp = z[k];
            z[k] = z[j];
            z[j] = tmp;
        }
    }
}

void ff_fft_end(FFTContext *s)
{
    av_freep(&s->revtab);
    av_freep(&s->exptab);
    av_freep(&s->exptab1);
}

