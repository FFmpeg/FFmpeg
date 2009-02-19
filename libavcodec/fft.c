/*
 * FFT/IFFT transforms
 * Copyright (c) 2008 Loren Merritt
 * Copyright (c) 2002 Fabrice Bellard
 * Partly based on libdjbfft by D. J. Bernstein
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
 * @file libavcodec/fft.c
 * FFT/IFFT transforms.
 */

#include "dsputil.h"

/* cos(2*pi*x/n) for 0<=x<=n/4, followed by its reverse */
DECLARE_ALIGNED_16(FFTSample, ff_cos_16[8]);
DECLARE_ALIGNED_16(FFTSample, ff_cos_32[16]);
DECLARE_ALIGNED_16(FFTSample, ff_cos_64[32]);
DECLARE_ALIGNED_16(FFTSample, ff_cos_128[64]);
DECLARE_ALIGNED_16(FFTSample, ff_cos_256[128]);
DECLARE_ALIGNED_16(FFTSample, ff_cos_512[256]);
DECLARE_ALIGNED_16(FFTSample, ff_cos_1024[512]);
DECLARE_ALIGNED_16(FFTSample, ff_cos_2048[1024]);
DECLARE_ALIGNED_16(FFTSample, ff_cos_4096[2048]);
DECLARE_ALIGNED_16(FFTSample, ff_cos_8192[4096]);
DECLARE_ALIGNED_16(FFTSample, ff_cos_16384[8192]);
DECLARE_ALIGNED_16(FFTSample, ff_cos_32768[16384]);
DECLARE_ALIGNED_16(FFTSample, ff_cos_65536[32768]);
FFTSample *ff_cos_tabs[] = {
    ff_cos_16, ff_cos_32, ff_cos_64, ff_cos_128, ff_cos_256, ff_cos_512, ff_cos_1024,
    ff_cos_2048, ff_cos_4096, ff_cos_8192, ff_cos_16384, ff_cos_32768, ff_cos_65536,
};

static int split_radix_permutation(int i, int n, int inverse)
{
    int m;
    if(n <= 2) return i&1;
    m = n >> 1;
    if(!(i&m))            return split_radix_permutation(i, m, inverse)*2;
    m >>= 1;
    if(inverse == !(i&m)) return split_radix_permutation(i, m, inverse)*4 + 1;
    else                  return split_radix_permutation(i, m, inverse)*4 - 1;
}

av_cold int ff_fft_init(FFTContext *s, int nbits, int inverse)
{
    int i, j, m, n;
    float alpha, c1, s1, s2;
    int split_radix = 1;
    int av_unused has_vectors;

    if (nbits < 2 || nbits > 16)
        goto fail;
    s->nbits = nbits;
    n = 1 << nbits;

    s->tmp_buf = NULL;
    s->exptab  = av_malloc((n / 2) * sizeof(FFTComplex));
    if (!s->exptab)
        goto fail;
    s->revtab = av_malloc(n * sizeof(uint16_t));
    if (!s->revtab)
        goto fail;
    s->inverse = inverse;

    s2 = inverse ? 1.0 : -1.0;

    s->fft_permute = ff_fft_permute_c;
    s->fft_calc    = ff_fft_calc_c;
    s->imdct_calc  = ff_imdct_calc_c;
    s->imdct_half  = ff_imdct_half_c;
    s->exptab1     = NULL;

#if HAVE_MMX && HAVE_YASM
    has_vectors = mm_support();
    if (has_vectors & FF_MM_SSE && HAVE_SSE) {
        /* SSE for P3/P4/K8 */
        s->imdct_calc  = ff_imdct_calc_sse;
        s->imdct_half  = ff_imdct_half_sse;
        s->fft_permute = ff_fft_permute_sse;
        s->fft_calc    = ff_fft_calc_sse;
    } else if (has_vectors & FF_MM_3DNOWEXT && HAVE_AMD3DNOWEXT) {
        /* 3DNowEx for K7 */
        s->imdct_calc = ff_imdct_calc_3dn2;
        s->imdct_half = ff_imdct_half_3dn2;
        s->fft_calc   = ff_fft_calc_3dn2;
    } else if (has_vectors & FF_MM_3DNOW && HAVE_AMD3DNOW) {
        /* 3DNow! for K6-2/3 */
        s->imdct_calc = ff_imdct_calc_3dn;
        s->imdct_half = ff_imdct_half_3dn;
        s->fft_calc   = ff_fft_calc_3dn;
    }
#elif HAVE_ALTIVEC && !defined ALTIVEC_USE_REFERENCE_C_CODE
    has_vectors = mm_support();
    if (has_vectors & FF_MM_ALTIVEC) {
        s->fft_calc = ff_fft_calc_altivec;
        split_radix = 0;
    }
#endif

    if (split_radix) {
        for(j=4; j<=nbits; j++) {
            int m = 1<<j;
            double freq = 2*M_PI/m;
            FFTSample *tab = ff_cos_tabs[j-4];
            for(i=0; i<=m/4; i++)
                tab[i] = cos(i*freq);
            for(i=1; i<m/4; i++)
                tab[m/2-i] = tab[i];
        }
        for(i=0; i<n; i++)
            s->revtab[-split_radix_permutation(i, n, s->inverse) & (n-1)] = i;
        s->tmp_buf = av_malloc(n * sizeof(FFTComplex));
    } else {
        int np, nblocks, np2, l;
        FFTComplex *q;

        for(i=0; i<(n/2); i++) {
            alpha = 2 * M_PI * (float)i / (float)n;
            c1 = cos(alpha);
            s1 = sin(alpha) * s2;
            s->exptab[i].re = c1;
            s->exptab[i].im = s1;
        }

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

        /* compute bit reverse table */
        for(i=0;i<n;i++) {
            m=0;
            for(j=0;j<nbits;j++) {
                m |= ((i >> j) & 1) << (nbits-j-1);
            }
            s->revtab[i]=m;
        }
    }

    return 0;
 fail:
    av_freep(&s->revtab);
    av_freep(&s->exptab);
    av_freep(&s->exptab1);
    av_freep(&s->tmp_buf);
    return -1;
}

void ff_fft_permute_c(FFTContext *s, FFTComplex *z)
{
    int j, k, np;
    FFTComplex tmp;
    const uint16_t *revtab = s->revtab;
    np = 1 << s->nbits;

    if (s->tmp_buf) {
        /* TODO: handle split-radix permute in a more optimal way, probably in-place */
        for(j=0;j<np;j++) s->tmp_buf[revtab[j]] = z[j];
        memcpy(z, s->tmp_buf, np * sizeof(FFTComplex));
        return;
    }

    /* reverse */
    for(j=0;j<np;j++) {
        k = revtab[j];
        if (k < j) {
            tmp = z[k];
            z[k] = z[j];
            z[j] = tmp;
        }
    }
}

av_cold void ff_fft_end(FFTContext *s)
{
    av_freep(&s->revtab);
    av_freep(&s->exptab);
    av_freep(&s->exptab1);
    av_freep(&s->tmp_buf);
}

#define sqrthalf (float)M_SQRT1_2

#define BF(x,y,a,b) {\
    x = a - b;\
    y = a + b;\
}

#define BUTTERFLIES(a0,a1,a2,a3) {\
    BF(t3, t5, t5, t1);\
    BF(a2.re, a0.re, a0.re, t5);\
    BF(a3.im, a1.im, a1.im, t3);\
    BF(t4, t6, t2, t6);\
    BF(a3.re, a1.re, a1.re, t4);\
    BF(a2.im, a0.im, a0.im, t6);\
}

// force loading all the inputs before storing any.
// this is slightly slower for small data, but avoids store->load aliasing
// for addresses separated by large powers of 2.
#define BUTTERFLIES_BIG(a0,a1,a2,a3) {\
    FFTSample r0=a0.re, i0=a0.im, r1=a1.re, i1=a1.im;\
    BF(t3, t5, t5, t1);\
    BF(a2.re, a0.re, r0, t5);\
    BF(a3.im, a1.im, i1, t3);\
    BF(t4, t6, t2, t6);\
    BF(a3.re, a1.re, r1, t4);\
    BF(a2.im, a0.im, i0, t6);\
}

#define TRANSFORM(a0,a1,a2,a3,wre,wim) {\
    t1 = a2.re * wre + a2.im * wim;\
    t2 = a2.im * wre - a2.re * wim;\
    t5 = a3.re * wre - a3.im * wim;\
    t6 = a3.im * wre + a3.re * wim;\
    BUTTERFLIES(a0,a1,a2,a3)\
}

#define TRANSFORM_ZERO(a0,a1,a2,a3) {\
    t1 = a2.re;\
    t2 = a2.im;\
    t5 = a3.re;\
    t6 = a3.im;\
    BUTTERFLIES(a0,a1,a2,a3)\
}

/* z[0...8n-1], w[1...2n-1] */
#define PASS(name)\
static void name(FFTComplex *z, const FFTSample *wre, unsigned int n)\
{\
    FFTSample t1, t2, t3, t4, t5, t6;\
    int o1 = 2*n;\
    int o2 = 4*n;\
    int o3 = 6*n;\
    const FFTSample *wim = wre+o1;\
    n--;\
\
    TRANSFORM_ZERO(z[0],z[o1],z[o2],z[o3]);\
    TRANSFORM(z[1],z[o1+1],z[o2+1],z[o3+1],wre[1],wim[-1]);\
    do {\
        z += 2;\
        wre += 2;\
        wim -= 2;\
        TRANSFORM(z[0],z[o1],z[o2],z[o3],wre[0],wim[0]);\
        TRANSFORM(z[1],z[o1+1],z[o2+1],z[o3+1],wre[1],wim[-1]);\
    } while(--n);\
}

PASS(pass)
#undef BUTTERFLIES
#define BUTTERFLIES BUTTERFLIES_BIG
PASS(pass_big)

#define DECL_FFT(n,n2,n4)\
static void fft##n(FFTComplex *z)\
{\
    fft##n2(z);\
    fft##n4(z+n4*2);\
    fft##n4(z+n4*3);\
    pass(z,ff_cos_##n,n4/2);\
}

static void fft4(FFTComplex *z)
{
    FFTSample t1, t2, t3, t4, t5, t6, t7, t8;

    BF(t3, t1, z[0].re, z[1].re);
    BF(t8, t6, z[3].re, z[2].re);
    BF(z[2].re, z[0].re, t1, t6);
    BF(t4, t2, z[0].im, z[1].im);
    BF(t7, t5, z[2].im, z[3].im);
    BF(z[3].im, z[1].im, t4, t8);
    BF(z[3].re, z[1].re, t3, t7);
    BF(z[2].im, z[0].im, t2, t5);
}

static void fft8(FFTComplex *z)
{
    FFTSample t1, t2, t3, t4, t5, t6, t7, t8;

    fft4(z);

    BF(t1, z[5].re, z[4].re, -z[5].re);
    BF(t2, z[5].im, z[4].im, -z[5].im);
    BF(t3, z[7].re, z[6].re, -z[7].re);
    BF(t4, z[7].im, z[6].im, -z[7].im);
    BF(t8, t1, t3, t1);
    BF(t7, t2, t2, t4);
    BF(z[4].re, z[0].re, z[0].re, t1);
    BF(z[4].im, z[0].im, z[0].im, t2);
    BF(z[6].re, z[2].re, z[2].re, t7);
    BF(z[6].im, z[2].im, z[2].im, t8);

    TRANSFORM(z[1],z[3],z[5],z[7],sqrthalf,sqrthalf);
}

#if !CONFIG_SMALL
static void fft16(FFTComplex *z)
{
    FFTSample t1, t2, t3, t4, t5, t6;

    fft8(z);
    fft4(z+8);
    fft4(z+12);

    TRANSFORM_ZERO(z[0],z[4],z[8],z[12]);
    TRANSFORM(z[2],z[6],z[10],z[14],sqrthalf,sqrthalf);
    TRANSFORM(z[1],z[5],z[9],z[13],ff_cos_16[1],ff_cos_16[3]);
    TRANSFORM(z[3],z[7],z[11],z[15],ff_cos_16[3],ff_cos_16[1]);
}
#else
DECL_FFT(16,8,4)
#endif
DECL_FFT(32,16,8)
DECL_FFT(64,32,16)
DECL_FFT(128,64,32)
DECL_FFT(256,128,64)
DECL_FFT(512,256,128)
#if !CONFIG_SMALL
#define pass pass_big
#endif
DECL_FFT(1024,512,256)
DECL_FFT(2048,1024,512)
DECL_FFT(4096,2048,1024)
DECL_FFT(8192,4096,2048)
DECL_FFT(16384,8192,4096)
DECL_FFT(32768,16384,8192)
DECL_FFT(65536,32768,16384)

static void (*fft_dispatch[])(FFTComplex*) = {
    fft4, fft8, fft16, fft32, fft64, fft128, fft256, fft512, fft1024,
    fft2048, fft4096, fft8192, fft16384, fft32768, fft65536,
};

void ff_fft_calc_c(FFTContext *s, FFTComplex *z)
{
    fft_dispatch[s->nbits-2](z);
}

