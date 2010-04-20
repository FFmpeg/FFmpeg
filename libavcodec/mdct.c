/*
 * MDCT/IMDCT transforms
 * Copyright (c) 2002 Fabrice Bellard
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

#include <stdlib.h>
#include <string.h>
#include "libavutil/common.h"
#include "libavutil/mathematics.h"
#include "fft.h"

/**
 * @file
 * MDCT/IMDCT transforms.
 */

// Generate a Kaiser-Bessel Derived Window.
#define BESSEL_I0_ITER 50 // default: 50 iterations of Bessel I0 approximation
av_cold void ff_kbd_window_init(float *window, float alpha, int n)
{
   int i, j;
   double sum = 0.0, bessel, tmp;
   double local_window[n];
   double alpha2 = (alpha * M_PI / n) * (alpha * M_PI / n);

   for (i = 0; i < n; i++) {
       tmp = i * (n - i) * alpha2;
       bessel = 1.0;
       for (j = BESSEL_I0_ITER; j > 0; j--)
           bessel = bessel * tmp / (j * j) + 1;
       sum += bessel;
       local_window[i] = sum;
   }

   sum++;
   for (i = 0; i < n; i++)
       window[i] = sqrt(local_window[i] / sum);
}

#include "mdct_tablegen.h"

/**
 * init MDCT or IMDCT computation.
 */
av_cold int ff_mdct_init(FFTContext *s, int nbits, int inverse, double scale)
{
    int n, n4, i;
    double alpha, theta;
    int tstep;

    memset(s, 0, sizeof(*s));
    n = 1 << nbits;
    s->mdct_bits = nbits;
    s->mdct_size = n;
    n4 = n >> 2;
    s->permutation = FF_MDCT_PERM_NONE;

    if (ff_fft_init(s, s->mdct_bits - 2, inverse) < 0)
        goto fail;

    s->tcos = av_malloc(n/2 * sizeof(FFTSample));
    if (!s->tcos)
        goto fail;

    switch (s->permutation) {
    case FF_MDCT_PERM_NONE:
        s->tsin = s->tcos + n4;
        tstep = 1;
        break;
    case FF_MDCT_PERM_INTERLEAVE:
        s->tsin = s->tcos + 1;
        tstep = 2;
        break;
    default:
        goto fail;
    }

    theta = 1.0 / 8.0 + (scale < 0 ? n4 : 0);
    scale = sqrt(fabs(scale));
    for(i=0;i<n4;i++) {
        alpha = 2 * M_PI * (i + theta) / n;
        s->tcos[i*tstep] = -cos(alpha) * scale;
        s->tsin[i*tstep] = -sin(alpha) * scale;
    }
    return 0;
 fail:
    ff_mdct_end(s);
    return -1;
}

/* complex multiplication: p = a * b */
#define CMUL(pre, pim, are, aim, bre, bim) \
{\
    FFTSample _are = (are);\
    FFTSample _aim = (aim);\
    FFTSample _bre = (bre);\
    FFTSample _bim = (bim);\
    (pre) = _are * _bre - _aim * _bim;\
    (pim) = _are * _bim + _aim * _bre;\
}

/**
 * Compute the middle half of the inverse MDCT of size N = 2^nbits,
 * thus excluding the parts that can be derived by symmetry
 * @param output N/2 samples
 * @param input N/2 samples
 */
void ff_imdct_half_c(FFTContext *s, FFTSample *output, const FFTSample *input)
{
    int k, n8, n4, n2, n, j;
    const uint16_t *revtab = s->revtab;
    const FFTSample *tcos = s->tcos;
    const FFTSample *tsin = s->tsin;
    const FFTSample *in1, *in2;
    FFTComplex *z = (FFTComplex *)output;

    n = 1 << s->mdct_bits;
    n2 = n >> 1;
    n4 = n >> 2;
    n8 = n >> 3;

    /* pre rotation */
    in1 = input;
    in2 = input + n2 - 1;
    for(k = 0; k < n4; k++) {
        j=revtab[k];
        CMUL(z[j].re, z[j].im, *in2, *in1, tcos[k], tsin[k]);
        in1 += 2;
        in2 -= 2;
    }
    ff_fft_calc(s, z);

    /* post rotation + reordering */
    for(k = 0; k < n8; k++) {
        FFTSample r0, i0, r1, i1;
        CMUL(r0, i1, z[n8-k-1].im, z[n8-k-1].re, tsin[n8-k-1], tcos[n8-k-1]);
        CMUL(r1, i0, z[n8+k  ].im, z[n8+k  ].re, tsin[n8+k  ], tcos[n8+k  ]);
        z[n8-k-1].re = r0;
        z[n8-k-1].im = i0;
        z[n8+k  ].re = r1;
        z[n8+k  ].im = i1;
    }
}

/**
 * Compute inverse MDCT of size N = 2^nbits
 * @param output N samples
 * @param input N/2 samples
 */
void ff_imdct_calc_c(FFTContext *s, FFTSample *output, const FFTSample *input)
{
    int k;
    int n = 1 << s->mdct_bits;
    int n2 = n >> 1;
    int n4 = n >> 2;

    ff_imdct_half_c(s, output+n4, input);

    for(k = 0; k < n4; k++) {
        output[k] = -output[n2-k-1];
        output[n-k-1] = output[n2+k];
    }
}

/**
 * Compute MDCT of size N = 2^nbits
 * @param input N samples
 * @param out N/2 samples
 */
void ff_mdct_calc_c(FFTContext *s, FFTSample *out, const FFTSample *input)
{
    int i, j, n, n8, n4, n2, n3;
    FFTSample re, im;
    const uint16_t *revtab = s->revtab;
    const FFTSample *tcos = s->tcos;
    const FFTSample *tsin = s->tsin;
    FFTComplex *x = (FFTComplex *)out;

    n = 1 << s->mdct_bits;
    n2 = n >> 1;
    n4 = n >> 2;
    n8 = n >> 3;
    n3 = 3 * n4;

    /* pre rotation */
    for(i=0;i<n8;i++) {
        re = -input[2*i+3*n4] - input[n3-1-2*i];
        im = -input[n4+2*i] + input[n4-1-2*i];
        j = revtab[i];
        CMUL(x[j].re, x[j].im, re, im, -tcos[i], tsin[i]);

        re = input[2*i] - input[n2-1-2*i];
        im = -(input[n2+2*i] + input[n-1-2*i]);
        j = revtab[n8 + i];
        CMUL(x[j].re, x[j].im, re, im, -tcos[n8 + i], tsin[n8 + i]);
    }

    ff_fft_calc(s, x);

    /* post rotation */
    for(i=0;i<n8;i++) {
        FFTSample r0, i0, r1, i1;
        CMUL(i1, r0, x[n8-i-1].re, x[n8-i-1].im, -tsin[n8-i-1], -tcos[n8-i-1]);
        CMUL(i0, r1, x[n8+i  ].re, x[n8+i  ].im, -tsin[n8+i  ], -tcos[n8+i  ]);
        x[n8-i-1].re = r0;
        x[n8-i-1].im = i0;
        x[n8+i  ].re = r1;
        x[n8+i  ].im = i1;
    }
}

av_cold void ff_mdct_end(FFTContext *s)
{
    av_freep(&s->tcos);
    ff_fft_end(s);
}
