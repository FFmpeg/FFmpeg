/*
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

#include "libavutil/mem.h"
#include "avfft.h"
#include "fft.h"

/* FFT */

FFTContext *av_fft_init(int nbits, int inverse)
{
    FFTContext *s = av_malloc(sizeof(*s));

    if (s)
        ff_fft_init(s, nbits, inverse);

    return s;
}

void av_fft_permute(FFTContext *s, FFTComplex *z)
{
    s->fft_permute(s, z);
}

void av_fft_calc(FFTContext *s, FFTComplex *z)
{
    s->fft_calc(s, z);
}

void av_fft_end(FFTContext *s)
{
    if (s) {
        ff_fft_end(s);
        av_free(s);
    }
}

#if CONFIG_MDCT

FFTContext *av_mdct_init(int nbits, int inverse, double scale)
{
    FFTContext *s = av_malloc(sizeof(*s));

    if (s)
        ff_mdct_init(s, nbits, inverse, scale);

    return s;
}

void av_imdct_calc(FFTContext *s, FFTSample *output, const FFTSample *input)
{
    s->imdct_calc(s, output, input);
}

void av_imdct_half(FFTContext *s, FFTSample *output, const FFTSample *input)
{
    s->imdct_half(s, output, input);
}

void av_mdct_calc(FFTContext *s, FFTSample *output, const FFTSample *input)
{
    s->mdct_calc(s, output, input);
}

void av_mdct_end(FFTContext *s)
{
    if (s) {
        ff_mdct_end(s);
        av_free(s);
    }
}

#endif /* CONFIG_MDCT */

#if CONFIG_RDFT

RDFTContext *av_rdft_init(int nbits, enum RDFTransformType trans)
{
    RDFTContext *s = av_malloc(sizeof(*s));

    if (s)
        ff_rdft_init(s, nbits, trans);

    return s;
}

void av_rdft_calc(RDFTContext *s, FFTSample *data)
{
    ff_rdft_calc(s, data);
}

void av_rdft_end(RDFTContext *s)
{
    if (s) {
        ff_rdft_end(s);
        av_free(s);
    }
}

#endif /* CONFIG_RDFT */

#if CONFIG_DCT

DCTContext *av_dct_init(int nbits, enum DCTTransformType inverse)
{
    DCTContext *s = av_malloc(sizeof(*s));

    if (s)
        ff_dct_init(s, nbits, inverse);

    return s;
}

void av_dct_calc(DCTContext *s, FFTSample *data)
{
    ff_dct_calc(s, data);
}

void av_dct_end(DCTContext *s)
{
    if (s) {
        ff_dct_end(s);
        av_free(s);
    }
}

#endif /* CONFIG_DCT */
