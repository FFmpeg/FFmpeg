/*
 * (I)DCT Transforms
 * Copyright (c) 2009 Peter Ross <pross@xvid.org>
 * Copyright (c) 2010 Alex Converse <alex.converse@gmail.com>
 * Copyright (c) 2010 Vitor Sessak
 *
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

/**
 * @file
 * (Inverse) Discrete Cosine Transforms. These are also known as the
 * type II and type III DCTs respectively.
 */

#include <math.h>
#include <string.h>

#include "libavutil/mathematics.h"
#include "dct.h"
#include "dct32.h"

/* sin((M_PI * x / (2 * n)) */
#define SIN(s, n, x) (s->costab[(n) - (x)])

/* cos((M_PI * x / (2 * n)) */
#define COS(s, n, x) (s->costab[x])

static void dst_calc_I_c(DCTContext *ctx, FFTSample *data)
{
    int n = 1 << ctx->nbits;
    int i;

    data[0] = 0;
    for (i = 1; i < n / 2; i++) {
        float tmp1   = data[i    ];
        float tmp2   = data[n - i];
        float s      = SIN(ctx, n, 2 * i);

        s           *= tmp1 + tmp2;
        tmp1         = (tmp1 - tmp2) * 0.5f;
        data[i]      = s + tmp1;
        data[n - i]  = s - tmp1;
    }

    data[n / 2] *= 2;
    ctx->rdft.rdft_calc(&ctx->rdft, data);

    data[0] *= 0.5f;

    for (i = 1; i < n - 2; i += 2) {
        data[i + 1] +=  data[i - 1];
        data[i]      = -data[i + 2];
    }

    data[n - 1] = 0;
}

static void dct_calc_I_c(DCTContext *ctx, FFTSample *data)
{
    int n = 1 << ctx->nbits;
    int i;
    float next = -0.5f * (data[0] - data[n]);

    for (i = 0; i < n / 2; i++) {
        float tmp1 = data[i];
        float tmp2 = data[n - i];
        float s    = SIN(ctx, n, 2 * i);
        float c    = COS(ctx, n, 2 * i);

        c *= tmp1 - tmp2;
        s *= tmp1 - tmp2;

        next += c;

        tmp1        = (tmp1 + tmp2) * 0.5f;
        data[i]     = tmp1 - s;
        data[n - i] = tmp1 + s;
    }

    ctx->rdft.rdft_calc(&ctx->rdft, data);
    data[n] = data[1];
    data[1] = next;

    for (i = 3; i <= n; i += 2)
        data[i] = data[i - 2] - data[i];
}

static void dct_calc_III_c(DCTContext *ctx, FFTSample *data)
{
    int n = 1 << ctx->nbits;
    int i;

    float next  = data[n - 1];
    float inv_n = 1.0f / n;

    for (i = n - 2; i >= 2; i -= 2) {
        float val1 = data[i];
        float val2 = data[i - 1] - data[i + 1];
        float c    = COS(ctx, n, i);
        float s    = SIN(ctx, n, i);

        data[i]     = c * val1 + s * val2;
        data[i + 1] = s * val1 - c * val2;
    }

    data[1] = 2 * next;

    ctx->rdft.rdft_calc(&ctx->rdft, data);

    for (i = 0; i < n / 2; i++) {
        float tmp1 = data[i]         * inv_n;
        float tmp2 = data[n - i - 1] * inv_n;
        float csc  = ctx->csc2[i] * (tmp1 - tmp2);

        tmp1            += tmp2;
        data[i]          = tmp1 + csc;
        data[n - i - 1]  = tmp1 - csc;
    }
}

static void dct_calc_II_c(DCTContext *ctx, FFTSample *data)
{
    int n = 1 << ctx->nbits;
    int i;
    float next;

    for (i = 0; i < n / 2; i++) {
        float tmp1 = data[i];
        float tmp2 = data[n - i - 1];
        float s    = SIN(ctx, n, 2 * i + 1);

        s    *= tmp1 - tmp2;
        tmp1  = (tmp1 + tmp2) * 0.5f;

        data[i]     = tmp1 + s;
        data[n-i-1] = tmp1 - s;
    }

    ctx->rdft.rdft_calc(&ctx->rdft, data);

    next     = data[1] * 0.5;
    data[1] *= -1;

    for (i = n - 2; i >= 0; i -= 2) {
        float inr = data[i    ];
        float ini = data[i + 1];
        float c   = COS(ctx, n, i);
        float s   = SIN(ctx, n, i);

        data[i]     = c * inr + s * ini;
        data[i + 1] = next;

        next += s * inr - c * ini;
    }
}

static void dct32_func(DCTContext *ctx, FFTSample *data)
{
    ctx->dct32(data, data);
}

av_cold int ff_dct_init(DCTContext *s, int nbits, enum DCTTransformType inverse)
{
    int n = 1 << nbits;
    int i;

    memset(s, 0, sizeof(*s));

    s->nbits   = nbits;
    s->inverse = inverse;

    if (inverse == DCT_II && nbits == 5) {
        s->dct_calc = dct32_func;
    } else {
        ff_init_ff_cos_tabs(nbits + 2);

        s->costab = ff_cos_tabs[nbits + 2];
        s->csc2   = av_malloc(n / 2 * sizeof(FFTSample));
        if (!s->csc2)
            return AVERROR(ENOMEM);

        if (ff_rdft_init(&s->rdft, nbits, inverse == DCT_III) < 0) {
            av_free(s->csc2);
            return -1;
        }

        for (i = 0; i < n / 2; i++)
            s->csc2[i] = 0.5 / sin((M_PI / (2 * n) * (2 * i + 1)));

        switch (inverse) {
        case DCT_I  : s->dct_calc = dct_calc_I_c;   break;
        case DCT_II : s->dct_calc = dct_calc_II_c;  break;
        case DCT_III: s->dct_calc = dct_calc_III_c; break;
        case DST_I  : s->dct_calc = dst_calc_I_c;   break;
        }
    }

    s->dct32 = ff_dct32_float;
    if (ARCH_X86)
        ff_dct_init_x86(s);

    return 0;
}

av_cold void ff_dct_end(DCTContext *s)
{
    ff_rdft_end(&s->rdft);
    av_free(s->csc2);
}
