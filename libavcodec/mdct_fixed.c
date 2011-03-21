/*
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#define CONFIG_FFT_FLOAT 0
#include "mdct.c"

/* same as ff_mdct_calcw_c with double-width unscaled output */
void ff_mdct_calcw_c(FFTContext *s, FFTDouble *out, const FFTSample *input)
{
    int i, j, n, n8, n4, n2, n3;
    FFTDouble re, im;
    const uint16_t *revtab = s->revtab;
    const FFTSample *tcos = s->tcos;
    const FFTSample *tsin = s->tsin;
    FFTComplex *x = s->tmp_buf;
    FFTDComplex *o = (FFTDComplex *)out;

    n = 1 << s->mdct_bits;
    n2 = n >> 1;
    n4 = n >> 2;
    n8 = n >> 3;
    n3 = 3 * n4;

    /* pre rotation */
    for(i=0;i<n8;i++) {
        re = RSCALE(-input[2*i+n3] - input[n3-1-2*i]);
        im = RSCALE(-input[n4+2*i] + input[n4-1-2*i]);
        j = revtab[i];
        CMUL(x[j].re, x[j].im, re, im, -tcos[i], tsin[i]);

        re = RSCALE( input[2*i]    - input[n2-1-2*i]);
        im = RSCALE(-input[n2+2*i] - input[ n-1-2*i]);
        j = revtab[n8 + i];
        CMUL(x[j].re, x[j].im, re, im, -tcos[n8 + i], tsin[n8 + i]);
    }

    s->fft_calc(s, x);

    /* post rotation */
    for(i=0;i<n8;i++) {
        FFTDouble r0, i0, r1, i1;
        CMULL(i1, r0, x[n8-i-1].re, x[n8-i-1].im, -tsin[n8-i-1], -tcos[n8-i-1]);
        CMULL(i0, r1, x[n8+i  ].re, x[n8+i  ].im, -tsin[n8+i  ], -tcos[n8+i  ]);
        o[n8-i-1].re = r0;
        o[n8-i-1].im = i0;
        o[n8+i  ].re = r1;
        o[n8+i  ].im = i1;
    }
}
