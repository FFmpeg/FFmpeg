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

#include <stddef.h>
#include <string.h>

#include "libavutil/attributes.h"
#include "libavutil/macros.h"
#include "libavutil/mem.h"
#include "libavutil/tx.h"
#include "avfft.h"

typedef struct AVTXWrapper {
    AVTXContext *ctx;
    av_tx_fn fn;

    AVTXContext *ctx2;
    av_tx_fn fn2;

    ptrdiff_t stride;
    int len;
    int inv;

    float *tmp;
    int out_of_place;
} AVTXWrapper;

/* FFT */

FFTContext *av_fft_init(int nbits, int inverse)
{
    int ret;
    float scale = 1.0f;
    AVTXWrapper *s = av_mallocz(sizeof(*s));
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

FFTContext *av_mdct_init(int nbits, int inverse, double scale)
{
    int ret;
    float scale_f = scale;
    AVTXWrapper *s = av_mallocz(sizeof(*s));
    if (!s)
        return NULL;

    ret = av_tx_init(&s->ctx, &s->fn, AV_TX_FLOAT_MDCT, inverse, 1 << (nbits - 1), &scale_f, 0);
    if (ret < 0) {
        av_free(s);
        return NULL;
    }

    if (inverse) {
        ret = av_tx_init(&s->ctx2, &s->fn2, AV_TX_FLOAT_MDCT, inverse, 1 << (nbits - 1),
                         &scale_f, AV_TX_FULL_IMDCT);
        if (ret < 0) {
            av_tx_uninit(&s->ctx);
            av_free(s);
            return NULL;
        }
    }

    return (FFTContext *)s;
}

void av_imdct_calc(FFTContext *s, FFTSample *output, const FFTSample *input)
{
    AVTXWrapper *w = (AVTXWrapper *)s;
    w->fn2(w->ctx2, output, (void *)input, sizeof(float));
}

void av_imdct_half(FFTContext *s, FFTSample *output, const FFTSample *input)
{
    AVTXWrapper *w = (AVTXWrapper *)s;
    w->fn(w->ctx, output, (void *)input, sizeof(float));
}

void av_mdct_calc(FFTContext *s, FFTSample *output, const FFTSample *input)
{
    AVTXWrapper *w = (AVTXWrapper *)s;
    w->fn(w->ctx, output, (void *)input, sizeof(float));
}

av_cold void av_mdct_end(FFTContext *s)
{
    if (s) {
        AVTXWrapper *w = (AVTXWrapper *)s;
        av_tx_uninit(&w->ctx2);
        av_tx_uninit(&w->ctx);
        av_free(w);
    }
}

RDFTContext *av_rdft_init(int nbits, enum RDFTransformType trans)
{
    int ret;
    float scale = trans == IDFT_C2R ? 0.5f : 1.0f;
    AVTXWrapper *s;

    /* The other 2 modes are unconventional, do not form an orthogonal
     * transform, have never been useful, and so they're not implemented. */
    if (trans != IDFT_C2R && trans != DFT_R2C)
        return NULL;

    s = av_mallocz(sizeof(*s));
    if (!s)
        return NULL;

    ret = av_tx_init(&s->ctx, &s->fn, AV_TX_FLOAT_RDFT, trans == IDFT_C2R,
                     1 << nbits, &scale, 0x0);
    if (ret < 0) {
        av_free(s);
        return NULL;
    }

    s->stride = (trans == DFT_C2R) ? sizeof(float) : sizeof(AVComplexFloat);
    s->len = 1 << nbits;
    s->inv = trans == IDFT_C2R;

    s->tmp = av_malloc((s->len + 2)*sizeof(float));
    if (!s->tmp) {
        av_tx_uninit(&s->ctx);
        av_free(s);
        return NULL;
    }

    return (RDFTContext *)s;
}

void av_rdft_calc(RDFTContext *s, FFTSample *data)
{
    AVTXWrapper *w = (AVTXWrapper *)s;
    float *src = w->inv ? w->tmp : (float *)data;
    float *dst = w->inv ? (float *)data : w->tmp;

    if (w->inv) {
        memcpy(src, data, w->len*sizeof(float));

        src[w->len] = src[1];
        src[1] = 0.0f;
    }

    w->fn(w->ctx, dst, (void *)src, w->stride);

    if (!w->inv) {
        dst[1] = dst[w->len];
        memcpy(data, dst, w->len*sizeof(float));
    }
}

av_cold void av_rdft_end(RDFTContext *s)
{
    if (s) {
        AVTXWrapper *w = (AVTXWrapper *)s;
        av_tx_uninit(&w->ctx);
        av_free(w->tmp);
        av_free(w);
    }
}

DCTContext *av_dct_init(int nbits, enum DCTTransformType inverse)
{
    int ret;
    const float scale_map[] = {
        [DCT_II] = 0.5f,
        [DCT_III] = 1.0f / (1 << nbits),
        [DCT_I] = 0.5f,
        [DST_I] = 2.0f,
    };
    static const enum AVTXType type_map[] = {
        [DCT_II] = AV_TX_FLOAT_DCT,
        [DCT_III] = AV_TX_FLOAT_DCT,
        [DCT_I] = AV_TX_FLOAT_DCT_I,
        [DST_I] = AV_TX_FLOAT_DST_I,
    };

    AVTXWrapper *s = av_mallocz(sizeof(*s));
    if (!s)
        return NULL;

    s->len = (1 << nbits);
    s->out_of_place = (inverse == DCT_I) || (inverse == DST_I);

    ret = av_tx_init(&s->ctx, &s->fn, type_map[inverse],
                     (inverse == DCT_III), 1 << (nbits - (inverse == DCT_III)),
                     &scale_map[inverse], s->out_of_place ? 0 : AV_TX_INPLACE);
    if (ret < 0) {
        av_free(s);
        return NULL;
    }

    if (s->out_of_place) {
        s->tmp = av_malloc((1 << (nbits + 1))*sizeof(float));
        if (!s->tmp) {
            av_tx_uninit(&s->ctx);
            av_free(s);
            return NULL;
        }
    }

    return (DCTContext *)s;
}

void av_dct_calc(DCTContext *s, FFTSample *data)
{
    AVTXWrapper *w = (AVTXWrapper *)s;
    if (w->out_of_place) {
        memcpy(w->tmp, data, w->len*sizeof(float));
        w->fn(w->ctx, (void *)data, w->tmp, sizeof(float));
    } else {
        w->fn(w->ctx, data, (void *)data, sizeof(float));
    }
}

av_cold void av_dct_end(DCTContext *s)
{
    if (s) {
        AVTXWrapper *w = (AVTXWrapper *)s;
        av_tx_uninit(&w->ctx);
        av_free(w->tmp);
        av_free(w);
    }
}
