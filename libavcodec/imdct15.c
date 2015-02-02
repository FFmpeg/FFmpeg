/*
 * Copyright (c) 2013-2014 Mozilla Corporation
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
 * @file
 * Celt non-power of 2 iMDCT
 */

#include <float.h>
#include <math.h>
#include <stddef.h>

#include "config.h"

#include "libavutil/attributes.h"
#include "libavutil/common.h"

#include "avfft.h"
#include "imdct15.h"
#include "opus.h"

// minimal iMDCT size to make SIMD opts easier
#define CELT_MIN_IMDCT_SIZE 120

// complex c = a * b
#define CMUL3(cre, cim, are, aim, bre, bim)          \
do {                                                 \
    cre = are * bre - aim * bim;                     \
    cim = are * bim + aim * bre;                     \
} while (0)

#define CMUL(c, a, b) CMUL3((c).re, (c).im, (a).re, (a).im, (b).re, (b).im)

// complex c = a * b
//         d = a * conjugate(b)
#define CMUL2(c, d, a, b)                            \
do {                                                 \
    float are = (a).re;                              \
    float aim = (a).im;                              \
    float bre = (b).re;                              \
    float bim = (b).im;                              \
    float rr  = are * bre;                           \
    float ri  = are * bim;                           \
    float ir  = aim * bre;                           \
    float ii  = aim * bim;                           \
    (c).re =  rr - ii;                               \
    (c).im =  ri + ir;                               \
    (d).re =  rr + ii;                               \
    (d).im = -ri + ir;                               \
} while (0)

av_cold void ff_imdct15_uninit(IMDCT15Context **ps)
{
    IMDCT15Context *s = *ps;
    int i;

    if (!s)
        return;

    for (i = 0; i < FF_ARRAY_ELEMS(s->exptab); i++)
        av_freep(&s->exptab[i]);

    av_freep(&s->twiddle_exptab);

    av_freep(&s->tmp);

    av_freep(ps);
}

static void imdct15_half(IMDCT15Context *s, float *dst, const float *src,
                         ptrdiff_t stride, float scale);

av_cold int ff_imdct15_init(IMDCT15Context **ps, int N)
{
    IMDCT15Context *s;
    int len2 = 15 * (1 << N);
    int len  = 2 * len2;
    int i, j;

    if (len2 > CELT_MAX_FRAME_SIZE || len2 < CELT_MIN_IMDCT_SIZE)
        return AVERROR(EINVAL);

    s = av_mallocz(sizeof(*s));
    if (!s)
        return AVERROR(ENOMEM);

    s->fft_n = N - 1;
    s->len4 = len2 / 2;
    s->len2 = len2;

    s->tmp  = av_malloc_array(len, 2 * sizeof(*s->tmp));
    if (!s->tmp)
        goto fail;

    s->twiddle_exptab  = av_malloc_array(s->len4, sizeof(*s->twiddle_exptab));
    if (!s->twiddle_exptab)
        goto fail;

    for (i = 0; i < s->len4; i++) {
        s->twiddle_exptab[i].re = cos(2 * M_PI * (i + 0.125 + s->len4) / len);
        s->twiddle_exptab[i].im = sin(2 * M_PI * (i + 0.125 + s->len4) / len);
    }

    for (i = 0; i < FF_ARRAY_ELEMS(s->exptab); i++) {
        int N = 15 * (1 << i);
        s->exptab[i] = av_malloc(sizeof(*s->exptab[i]) * FFMAX(N, 19));
        if (!s->exptab[i])
            goto fail;

        for (j = 0; j < N; j++) {
            s->exptab[i][j].re = cos(2 * M_PI * j / N);
            s->exptab[i][j].im = sin(2 * M_PI * j / N);
        }
    }

    // wrap around to simplify fft15
    for (j = 15; j < 19; j++)
        s->exptab[0][j] = s->exptab[0][j - 15];

    s->imdct_half = imdct15_half;

    if (ARCH_AARCH64)
        ff_imdct15_init_aarch64(s);

    *ps = s;

    return 0;

fail:
    ff_imdct15_uninit(&s);
    return AVERROR(ENOMEM);
}

static void fft5(FFTComplex *out, const FFTComplex *in, ptrdiff_t stride)
{
    // [0] = exp(2 * i * pi / 5), [1] = exp(2 * i * pi * 2 / 5)
    static const FFTComplex fact[] = { { 0.30901699437494745,  0.95105651629515353 },
                                       { -0.80901699437494734, 0.58778525229247325 } };

    FFTComplex z[4][4];

    CMUL2(z[0][0], z[0][3], in[1 * stride], fact[0]);
    CMUL2(z[0][1], z[0][2], in[1 * stride], fact[1]);
    CMUL2(z[1][0], z[1][3], in[2 * stride], fact[0]);
    CMUL2(z[1][1], z[1][2], in[2 * stride], fact[1]);
    CMUL2(z[2][0], z[2][3], in[3 * stride], fact[0]);
    CMUL2(z[2][1], z[2][2], in[3 * stride], fact[1]);
    CMUL2(z[3][0], z[3][3], in[4 * stride], fact[0]);
    CMUL2(z[3][1], z[3][2], in[4 * stride], fact[1]);

    out[0].re = in[0].re + in[stride].re + in[2 * stride].re + in[3 * stride].re + in[4 * stride].re;
    out[0].im = in[0].im + in[stride].im + in[2 * stride].im + in[3 * stride].im + in[4 * stride].im;

    out[1].re = in[0].re + z[0][0].re + z[1][1].re + z[2][2].re + z[3][3].re;
    out[1].im = in[0].im + z[0][0].im + z[1][1].im + z[2][2].im + z[3][3].im;

    out[2].re = in[0].re + z[0][1].re + z[1][3].re + z[2][0].re + z[3][2].re;
    out[2].im = in[0].im + z[0][1].im + z[1][3].im + z[2][0].im + z[3][2].im;

    out[3].re = in[0].re + z[0][2].re + z[1][0].re + z[2][3].re + z[3][1].re;
    out[3].im = in[0].im + z[0][2].im + z[1][0].im + z[2][3].im + z[3][1].im;

    out[4].re = in[0].re + z[0][3].re + z[1][2].re + z[2][1].re + z[3][0].re;
    out[4].im = in[0].im + z[0][3].im + z[1][2].im + z[2][1].im + z[3][0].im;
}

static void fft15(IMDCT15Context *s, FFTComplex *out, const FFTComplex *in,
                  ptrdiff_t stride)
{
    const FFTComplex *exptab = s->exptab[0];
    FFTComplex tmp[5];
    FFTComplex tmp1[5];
    FFTComplex tmp2[5];
    int k;

    fft5(tmp,  in,              stride * 3);
    fft5(tmp1, in +     stride, stride * 3);
    fft5(tmp2, in + 2 * stride, stride * 3);

    for (k = 0; k < 5; k++) {
        FFTComplex t1, t2;

        CMUL(t1, tmp1[k], exptab[k]);
        CMUL(t2, tmp2[k], exptab[2 * k]);
        out[k].re = tmp[k].re + t1.re + t2.re;
        out[k].im = tmp[k].im + t1.im + t2.im;

        CMUL(t1, tmp1[k], exptab[k + 5]);
        CMUL(t2, tmp2[k], exptab[2 * (k + 5)]);
        out[k + 5].re = tmp[k].re + t1.re + t2.re;
        out[k + 5].im = tmp[k].im + t1.im + t2.im;

        CMUL(t1, tmp1[k], exptab[k + 10]);
        CMUL(t2, tmp2[k], exptab[2 * k + 5]);
        out[k + 10].re = tmp[k].re + t1.re + t2.re;
        out[k + 10].im = tmp[k].im + t1.im + t2.im;
    }
}

/*
 * FFT of the length 15 * (2^N)
 */
static void fft_calc(IMDCT15Context *s, FFTComplex *out, const FFTComplex *in,
                     int N, ptrdiff_t stride)
{
    if (N) {
        const FFTComplex *exptab = s->exptab[N];
        const int len2 = 15 * (1 << (N - 1));
        int k;

        fft_calc(s, out,        in,          N - 1, stride * 2);
        fft_calc(s, out + len2, in + stride, N - 1, stride * 2);

        for (k = 0; k < len2; k++) {
            FFTComplex t;

            CMUL(t, out[len2 + k], exptab[k]);

            out[len2 + k].re = out[k].re - t.re;
            out[len2 + k].im = out[k].im - t.im;

            out[k].re += t.re;
            out[k].im += t.im;
        }
    } else
        fft15(s, out, in, stride);
}

static void imdct15_half(IMDCT15Context *s, float *dst, const float *src,
                         ptrdiff_t stride, float scale)
{
    FFTComplex *z = (FFTComplex *)dst;
    const int len8 = s->len4 / 2;
    const float *in1 = src;
    const float *in2 = src + (s->len2 - 1) * stride;
    int i;

    for (i = 0; i < s->len4; i++) {
        FFTComplex tmp = { *in2, *in1 };
        CMUL(s->tmp[i], tmp, s->twiddle_exptab[i]);
        in1 += 2 * stride;
        in2 -= 2 * stride;
    }

    fft_calc(s, z, s->tmp, s->fft_n, 1);

    for (i = 0; i < len8; i++) {
        float r0, i0, r1, i1;

        CMUL3(r0, i1, z[len8 - i - 1].im, z[len8 - i - 1].re,  s->twiddle_exptab[len8 - i - 1].im, s->twiddle_exptab[len8 - i - 1].re);
        CMUL3(r1, i0, z[len8 + i].im,     z[len8 + i].re,      s->twiddle_exptab[len8 + i].im,     s->twiddle_exptab[len8 + i].re);
        z[len8 - i - 1].re = scale * r0;
        z[len8 - i - 1].im = scale * i0;
        z[len8 + i].re     = scale * r1;
        z[len8 + i].im     = scale * i1;
    }
}
