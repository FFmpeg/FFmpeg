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

#include "libavutil/attributes.h"
#include "libavutil/mem.h"
#include "libavutil/tx.h"
#include "avfft.h"
#include "fft.h"
#include "rdft.h"
#include "dct.h"

typedef struct AVTXWrapper {
    AVTXContext *ctx;
    av_tx_fn fn;

    AVTXContext *ctx2;
    av_tx_fn fn2;

    ptrdiff_t stride;
} AVTXWrapper;

/* FFT */

FFTContext *av_fft_init(int nbits, int inverse)
{
    int ret;
    float scale = 1.0f;
    AVTXWrapper *s = av_malloc(sizeof(*s));
    if (!s)
        return NULL;

    ret = av_tx_init(&s->ctx, &s->fn, AV_TX_FLOAT_FFT, inverse, 1 << nbits,
                     &scale, AV_TX_INPLACE);
    if (ret < 0) {
        av_free(s);
        return NULL;
    }

    return (FFTContext *)s;
}

void av_fft_permute(FFTContext *s, FFTComplex *z)
{
    /* Empty */
}

void av_fft_calc(FFTContext *s, FFTComplex *z)
{
    AVTXWrapper *w = (AVTXWrapper *)s;
    w->fn(w->ctx, z, (void *)z, sizeof(AVComplexFloat));
}

av_cold void av_fft_end(FFTContext *s)
{
    if (s) {
        AVTXWrapper *w = (AVTXWrapper *)s;
        av_tx_uninit(&w->ctx);
        av_tx_uninit(&w->ctx2);
        av_free(w);
    }
}

#if CONFIG_MDCT

FFTContext *av_mdct_init(int nbits, int inverse, double scale)
{
    FFTContext *s = av_malloc(sizeof(*s));

    if (s && ff_mdct_init(s, nbits, inverse, scale))
        av_freep(&s);

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

av_cold void av_mdct_end(FFTContext *s)
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

    if (s && ff_rdft_init(s, nbits, trans))
        av_freep(&s);

    return s;
}

void av_rdft_calc(RDFTContext *s, FFTSample *data)
{
    s->rdft_calc(s, data);
}

av_cold void av_rdft_end(RDFTContext *s)
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

    if (s && ff_dct_init(s, nbits, inverse))
        av_freep(&s);

    return s;
}

void av_dct_calc(DCTContext *s, FFTSample *data)
{
    s->dct_calc(s, data);
}

av_cold void av_dct_end(DCTContext *s)
{
    if (s) {
        ff_dct_end(s);
        av_free(s);
    }
}

#endif /* CONFIG_DCT */
