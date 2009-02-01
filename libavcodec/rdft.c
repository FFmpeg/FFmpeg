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
#include <math.h>
#include "dsputil.h"

/**
 * @file libavcodec/rdft.c
 * (Inverse) Real Discrete Fourier Transforms.
 */

/* sin(2*pi*x/n) for 0<=x<n/4, followed by n/2<=x<3n/4 */
DECLARE_ALIGNED_16(FFTSample, ff_sin_16[8]);
DECLARE_ALIGNED_16(FFTSample, ff_sin_32[16]);
DECLARE_ALIGNED_16(FFTSample, ff_sin_64[32]);
DECLARE_ALIGNED_16(FFTSample, ff_sin_128[64]);
DECLARE_ALIGNED_16(FFTSample, ff_sin_256[128]);
DECLARE_ALIGNED_16(FFTSample, ff_sin_512[256]);
DECLARE_ALIGNED_16(FFTSample, ff_sin_1024[512]);
DECLARE_ALIGNED_16(FFTSample, ff_sin_2048[1024]);
DECLARE_ALIGNED_16(FFTSample, ff_sin_4096[2048]);
DECLARE_ALIGNED_16(FFTSample, ff_sin_8192[4096]);
DECLARE_ALIGNED_16(FFTSample, ff_sin_16384[8192]);
DECLARE_ALIGNED_16(FFTSample, ff_sin_32768[16384]);
DECLARE_ALIGNED_16(FFTSample, ff_sin_65536[32768]);
FFTSample *ff_sin_tabs[] = {
    ff_sin_16, ff_sin_32, ff_sin_64, ff_sin_128, ff_sin_256, ff_sin_512, ff_sin_1024,
    ff_sin_2048, ff_sin_4096, ff_sin_8192, ff_sin_16384, ff_sin_32768, ff_sin_65536,
};

av_cold int ff_rdft_init(RDFTContext *s, int nbits, enum RDFTransformType trans)
{
    int n = 1 << nbits;
    int i;
    const double theta = (trans == RDFT || trans == IRIDFT ? -1 : 1)*2*M_PI/n;

    s->nbits           = nbits;
    s->inverse         = trans == IRDFT || trans == IRIDFT;
    s->sign_convention = trans == RIDFT || trans == IRIDFT ? 1 : -1;

    if (nbits < 4 || nbits > 16)
        return -1;

    if (ff_fft_init(&s->fft, nbits-1, trans == IRDFT || trans == RIDFT) < 0)
        return -1;

    s->tcos = ff_cos_tabs[nbits-4];
    s->tsin = ff_sin_tabs[nbits-4]+(trans == RDFT || trans == IRIDFT)*(n>>2);
    for (i = 0; i < (n>>2); i++) {
        s->tcos[i] = cos(i*theta);
        s->tsin[i] = sin(i*theta);
    }
    return 0;
}

/** Map one real FFT into two parallel real even and odd FFTs. Then interleave
 * the two real FFTs into one complex FFT. Unmangle the results.
 * ref: http://www.engineeringproductivitytools.com/stuff/T0001/PT10.HTM
 */
void ff_rdft_calc_c(RDFTContext* s, FFTSample* data)
{
    int i, i1, i2;
    FFTComplex ev, od;
    const int n = 1 << s->nbits;
    const float k1 = 0.5;
    const float k2 = 0.5 - s->inverse;
    const FFTSample *tcos = s->tcos;
    const FFTSample *tsin = s->tsin;

    if (!s->inverse) {
        ff_fft_permute(&s->fft, (FFTComplex*)data);
        ff_fft_calc(&s->fft, (FFTComplex*)data);
    }
    /* i=0 is a special case because of packing, the DC term is real, so we
       are going to throw the N/2 term (also real) in with it. */
    ev.re = data[0];
    data[0] = ev.re+data[1];
    data[1] = ev.re-data[1];
    for (i = 1; i < (n>>2); i++) {
        i1 = 2*i;
        i2 = n-i1;
        /* Separate even and odd FFTs */
        ev.re =  k1*(data[i1  ]+data[i2  ]);
        od.im = -k2*(data[i1  ]-data[i2  ]);
        ev.im =  k1*(data[i1+1]-data[i2+1]);
        od.re =  k2*(data[i1+1]+data[i2+1]);
        /* Apply twiddle factors to the odd FFT and add to the even FFT */
        data[i1  ] =  ev.re + od.re*tcos[i] - od.im*tsin[i];
        data[i1+1] =  ev.im + od.im*tcos[i] + od.re*tsin[i];
        data[i2  ] =  ev.re - od.re*tcos[i] + od.im*tsin[i];
        data[i2+1] = -ev.im + od.im*tcos[i] + od.re*tsin[i];
    }
    data[2*i+1]=s->sign_convention*data[2*i+1];
    if (s->inverse) {
        data[0] *= k1;
        data[1] *= k1;
        ff_fft_permute(&s->fft, (FFTComplex*)data);
        ff_fft_calc(&s->fft, (FFTComplex*)data);
    }
}

void ff_rdft_calc(RDFTContext *s, FFTSample *data)
{
    ff_rdft_calc_c(s, data);
}

av_cold void ff_rdft_end(RDFTContext *s)
{
    ff_fft_end(&s->fft);
}
