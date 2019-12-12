/*
 * (I)RDFT transforms
 * Copyright (c) 2009 Alex Converse <alex dot converse at gmail dot com>
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
#include <math.h>
#include "libavutil/mathematics.h"
#include "rdft.h"

/**
 * @file
 * (Inverse) Real Discrete Fourier Transforms.
 */

/** Map one real FFT into two parallel real even and odd FFTs. Then interleave
 * the two real FFTs into one complex FFT. Unmangle the results.
 * ref: http://www.engineeringproductivitytools.com/stuff/T0001/PT10.HTM
 */
static void rdft_calc_c(RDFTContext *s, FFTSample *data)
{
    int i, i1, i2;
    FFTComplex ev, od, odsum;
    const int n = 1 << s->nbits;
    const float k1 = 0.5;
    const float k2 = 0.5 - s->inverse;
    const FFTSample *tcos = s->tcos;
    const FFTSample *tsin = s->tsin;

    if (!s->inverse) {
        s->fft.fft_permute(&s->fft, (FFTComplex*)data);
        s->fft.fft_calc(&s->fft, (FFTComplex*)data);
    }
    /* i=0 is a special case because of packing, the DC term is real, so we
       are going to throw the N/2 term (also real) in with it. */
    ev.re = data[0];
    data[0] = ev.re+data[1];
    data[1] = ev.re-data[1];

#define RDFT_UNMANGLE(sign0, sign1)                                         \
    for (i = 1; i < (n>>2); i++) {                                          \
        i1 = 2*i;                                                           \
        i2 = n-i1;                                                          \
        /* Separate even and odd FFTs */                                    \
        ev.re =  k1*(data[i1  ]+data[i2  ]);                                \
        od.im =  k2*(data[i2  ]-data[i1  ]);                                \
        ev.im =  k1*(data[i1+1]-data[i2+1]);                                \
        od.re =  k2*(data[i1+1]+data[i2+1]);                                \
        /* Apply twiddle factors to the odd FFT and add to the even FFT */  \
        odsum.re = od.re*tcos[i] sign0 od.im*tsin[i];                       \
        odsum.im = od.im*tcos[i] sign1 od.re*tsin[i];                       \
        data[i1  ] =  ev.re + odsum.re;                                     \
        data[i1+1] =  ev.im + odsum.im;                                     \
        data[i2  ] =  ev.re - odsum.re;                                     \
        data[i2+1] =  odsum.im - ev.im;                                     \
    }

    if (s->negative_sin) {
        RDFT_UNMANGLE(+,-)
    } else {
        RDFT_UNMANGLE(-,+)
    }

    data[2*i+1]=s->sign_convention*data[2*i+1];
    if (s->inverse) {
        data[0] *= k1;
        data[1] *= k1;
        s->fft.fft_permute(&s->fft, (FFTComplex*)data);
        s->fft.fft_calc(&s->fft, (FFTComplex*)data);
    }
}

av_cold int ff_rdft_init(RDFTContext *s, int nbits, enum RDFTransformType trans)
{
    int n = 1 << nbits;
    int ret;

    s->nbits           = nbits;
    s->inverse         = trans == IDFT_C2R || trans == DFT_C2R;
    s->sign_convention = trans == IDFT_R2C || trans == DFT_C2R ? 1 : -1;
    s->negative_sin    = trans == DFT_C2R || trans == DFT_R2C;

    if (nbits < 4 || nbits > 16)
        return AVERROR(EINVAL);

    if ((ret = ff_fft_init(&s->fft, nbits-1, trans == IDFT_C2R || trans == IDFT_R2C)) < 0)
        return ret;

    ff_init_ff_cos_tabs(nbits);
    s->tcos = ff_cos_tabs[nbits];
    s->tsin = ff_cos_tabs[nbits] + (n >> 2);
    s->rdft_calc   = rdft_calc_c;

    if (ARCH_ARM) ff_rdft_init_arm(s);

    return 0;
}

av_cold void ff_rdft_end(RDFTContext *s)
{
    ff_fft_end(&s->fft);
}
