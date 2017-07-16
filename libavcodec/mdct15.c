/*
 * Copyright (c) 2013-2014 Mozilla Corporation
 * Copyright (c) 2017 Rostislav Pehlivanov <atomnuker@gmail.com>
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

#include "mdct15.h"

#define FFT_FLOAT 1
#include "fft-internal.h"

#define CMUL3(c, a, b) CMUL((c).re, (c).im, (a).re, (a).im, (b).re, (b).im)

av_cold void ff_mdct15_uninit(MDCT15Context **ps)
{
    MDCT15Context *s = *ps;

    if (!s)
        return;

    ff_fft_end(&s->ptwo_fft);

    av_freep(&s->pfa_prereindex);
    av_freep(&s->pfa_postreindex);
    av_freep(&s->twiddle_exptab);
    av_freep(&s->tmp);

    av_freep(ps);
}

static inline int init_pfa_reindex_tabs(MDCT15Context *s)
{
    int i, j;
    const int b_ptwo = s->ptwo_fft.nbits; /* Bits for the power of two FFTs */
    const int l_ptwo = 1 << b_ptwo; /* Total length for the power of two FFTs */
    const int inv_1 = l_ptwo << ((4 - b_ptwo) & 3); /* (2^b_ptwo)^-1 mod 15 */
    const int inv_2 = 0xeeeeeeef & ((1U << b_ptwo) - 1); /* 15^-1 mod 2^b_ptwo */

    s->pfa_prereindex = av_malloc(15 * l_ptwo * sizeof(*s->pfa_prereindex));
    if (!s->pfa_prereindex)
        return 1;

    s->pfa_postreindex = av_malloc(15 * l_ptwo * sizeof(*s->pfa_postreindex));
    if (!s->pfa_postreindex)
        return 1;

    /* Pre/Post-reindex */
    for (i = 0; i < l_ptwo; i++) {
        for (j = 0; j < 15; j++) {
            const int q_pre = ((l_ptwo * j)/15 + i) >> b_ptwo;
            const int q_post = (((j*inv_1)/15) + (i*inv_2)) >> b_ptwo;
            const int k_pre = 15*i + (j - q_pre*15)*(1 << b_ptwo);
            const int k_post = i*inv_2*15 + j*inv_1 - 15*q_post*l_ptwo;
            s->pfa_prereindex[i*15 + j] = k_pre;
            s->pfa_postreindex[k_post] = l_ptwo*j + i;
        }
    }

    return 0;
}

/* Stride is hardcoded to 3 */
static inline void fft5(FFTComplex *out, FFTComplex *in, FFTComplex exptab[2])
{
    FFTComplex z0[4], t[6];

    t[0].re = in[3].re + in[12].re;
    t[0].im = in[3].im + in[12].im;
    t[1].im = in[3].re - in[12].re;
    t[1].re = in[3].im - in[12].im;
    t[2].re = in[6].re + in[ 9].re;
    t[2].im = in[6].im + in[ 9].im;
    t[3].im = in[6].re - in[ 9].re;
    t[3].re = in[6].im - in[ 9].im;

    out[0].re = in[0].re + in[3].re + in[6].re + in[9].re + in[12].re;
    out[0].im = in[0].im + in[3].im + in[6].im + in[9].im + in[12].im;

    t[4].re = exptab[0].re * t[2].re - exptab[1].re * t[0].re;
    t[4].im = exptab[0].re * t[2].im - exptab[1].re * t[0].im;
    t[0].re = exptab[0].re * t[0].re - exptab[1].re * t[2].re;
    t[0].im = exptab[0].re * t[0].im - exptab[1].re * t[2].im;
    t[5].re = exptab[0].im * t[3].re - exptab[1].im * t[1].re;
    t[5].im = exptab[0].im * t[3].im - exptab[1].im * t[1].im;
    t[1].re = exptab[0].im * t[1].re + exptab[1].im * t[3].re;
    t[1].im = exptab[0].im * t[1].im + exptab[1].im * t[3].im;

    z0[0].re = t[0].re - t[1].re;
    z0[0].im = t[0].im - t[1].im;
    z0[1].re = t[4].re + t[5].re;
    z0[1].im = t[4].im + t[5].im;

    z0[2].re = t[4].re - t[5].re;
    z0[2].im = t[4].im - t[5].im;
    z0[3].re = t[0].re + t[1].re;
    z0[3].im = t[0].im + t[1].im;

    out[1].re = in[0].re + z0[3].re;
    out[1].im = in[0].im + z0[0].im;
    out[2].re = in[0].re + z0[2].re;
    out[2].im = in[0].im + z0[1].im;
    out[3].re = in[0].re + z0[1].re;
    out[3].im = in[0].im + z0[2].im;
    out[4].re = in[0].re + z0[0].re;
    out[4].im = in[0].im + z0[3].im;
}

static void fft15_c(FFTComplex *out, FFTComplex *in, FFTComplex *exptab, ptrdiff_t stride)
{
    int k;
    FFTComplex tmp1[5], tmp2[5], tmp3[5];

    fft5(tmp1, in + 0, exptab + 19);
    fft5(tmp2, in + 1, exptab + 19);
    fft5(tmp3, in + 2, exptab + 19);

    for (k = 0; k < 5; k++) {
        FFTComplex t[2];

        CMUL3(t[0], tmp2[k], exptab[k]);
        CMUL3(t[1], tmp3[k], exptab[2 * k]);
        out[stride*k].re = tmp1[k].re + t[0].re + t[1].re;
        out[stride*k].im = tmp1[k].im + t[0].im + t[1].im;

        CMUL3(t[0], tmp2[k], exptab[k + 5]);
        CMUL3(t[1], tmp3[k], exptab[2 * (k + 5)]);
        out[stride*(k + 5)].re = tmp1[k].re + t[0].re + t[1].re;
        out[stride*(k + 5)].im = tmp1[k].im + t[0].im + t[1].im;

        CMUL3(t[0], tmp2[k], exptab[k + 10]);
        CMUL3(t[1], tmp3[k], exptab[2 * k + 5]);
        out[stride*(k + 10)].re = tmp1[k].re + t[0].re + t[1].re;
        out[stride*(k + 10)].im = tmp1[k].im + t[0].im + t[1].im;
    }
}

static void mdct15(MDCT15Context *s, float *dst, const float *src, ptrdiff_t stride)
{
    int i, j;
    const int len4 = s->len4, len3 = len4 * 3, len8 = len4 >> 1;
    const int l_ptwo = 1 << s->ptwo_fft.nbits;
    FFTComplex fft15in[15];

    /* Folding and pre-reindexing */
    for (i = 0; i < l_ptwo; i++) {
        for (j = 0; j < 15; j++) {
            float re, im;
            const int k = s->pfa_prereindex[i*15 + j];
            if (k < len8) {
                re = -src[2*k+len3] - src[len3-1-2*k];
                im = -src[len4+2*k] + src[len4-1-2*k];
            } else {
                re =  src[2*k-len4] - src[1*len3-1-2*k];
                im = -src[2*k+len4] - src[5*len4-1-2*k];
            }
            CMUL(fft15in[j].re, fft15in[j].im, re, im, s->twiddle_exptab[k].re, -s->twiddle_exptab[k].im);
        }
        s->fft15(s->tmp + s->ptwo_fft.revtab[i], fft15in, s->exptab, l_ptwo);
    }

    /* Then a 15xN FFT (where N is a power of two) */
    for (i = 0; i < 15; i++)
        s->ptwo_fft.fft_calc(&s->ptwo_fft, s->tmp + l_ptwo*i);

    /* Reindex again, apply twiddles and output */
    for (i = 0; i < len8; i++) {
        float re0, im0, re1, im1;
        const int i0 = len8 + i, i1 = len8 - i - 1;
        const int s0 = s->pfa_postreindex[i0], s1 = s->pfa_postreindex[i1];

        CMUL(im1, re0, s->tmp[s1].re, s->tmp[s1].im, s->twiddle_exptab[i1].im, s->twiddle_exptab[i1].re);
        CMUL(im0, re1, s->tmp[s0].re, s->tmp[s0].im, s->twiddle_exptab[i0].im, s->twiddle_exptab[i0].re);
        dst[2*i1*stride         ] = re0;
        dst[2*i1*stride + stride] = im0;
        dst[2*i0*stride         ] = re1;
        dst[2*i0*stride + stride] = im1;
    }
}

static void imdct15_half(MDCT15Context *s, float *dst, const float *src,
                         ptrdiff_t stride)
{
    FFTComplex fft15in[15];
    FFTComplex *z = (FFTComplex *)dst;
    int i, j, len8 = s->len4 >> 1, l_ptwo = 1 << s->ptwo_fft.nbits;
    const float *in1 = src, *in2 = src + (s->len2 - 1) * stride;

    /* Reindex input, putting it into a buffer and doing an Nx15 FFT */
    for (i = 0; i < l_ptwo; i++) {
        for (j = 0; j < 15; j++) {
            const int k = s->pfa_prereindex[i*15 + j];
            FFTComplex tmp = { *(in2 - 2*k*stride), *(in1 + 2*k*stride) };
            CMUL3(fft15in[j], tmp, s->twiddle_exptab[k]);
        }
        s->fft15(s->tmp + s->ptwo_fft.revtab[i], fft15in, s->exptab, l_ptwo);
    }

    /* Then a 15xN FFT (where N is a power of two) */
    for (i = 0; i < 15; i++)
        s->ptwo_fft.fft_calc(&s->ptwo_fft, s->tmp + l_ptwo*i);

    /* Reindex again, apply twiddles and output */
    for (i = 0; i < len8; i++) {
        const int i0 = len8 + i, i1 = len8 - i - 1;
        const int s0 = s->pfa_postreindex[i0], s1 = s->pfa_postreindex[i1];

        CMUL(z[i1].re, z[i0].im, s->tmp[s1].im, s->tmp[s1].re,  s->twiddle_exptab[i1].im, s->twiddle_exptab[i1].re);
        CMUL(z[i0].re, z[i1].im, s->tmp[s0].im, s->tmp[s0].re,  s->twiddle_exptab[i0].im, s->twiddle_exptab[i0].re);
    }
}

av_cold int ff_mdct15_init(MDCT15Context **ps, int inverse, int N, double scale)
{
    MDCT15Context *s;
    double alpha, theta;
    int len2 = 15 * (1 << N);
    int len  = 2 * len2;
    int i;

    /* Tested and verified to work on everything in between */
    if ((N < 2) || (N > 13))
        return AVERROR(EINVAL);

    s = av_mallocz(sizeof(*s));
    if (!s)
        return AVERROR(ENOMEM);

    s->fft_n      = N - 1;
    s->len4       = len2 / 2;
    s->len2       = len2;
    s->inverse    = inverse;
    s->fft15      = fft15_c;
    s->mdct       = mdct15;
    s->imdct_half = imdct15_half;

    if (ff_fft_init(&s->ptwo_fft, N - 1, s->inverse) < 0)
        goto fail;

    if (init_pfa_reindex_tabs(s))
        goto fail;

    s->tmp  = av_malloc_array(len, 2 * sizeof(*s->tmp));
    if (!s->tmp)
        goto fail;

    s->twiddle_exptab  = av_malloc_array(s->len4, sizeof(*s->twiddle_exptab));
    if (!s->twiddle_exptab)
        goto fail;

    theta = 0.125f + (scale < 0 ? s->len4 : 0);
    scale = sqrt(fabs(scale));
    for (i = 0; i < s->len4; i++) {
        alpha = 2 * M_PI * (i + theta) / len;
        s->twiddle_exptab[i].re = cosf(alpha) * scale;
        s->twiddle_exptab[i].im = sinf(alpha) * scale;
    }

    /* 15-point FFT exptab */
    for (i = 0; i < 19; i++) {
        if (i < 15) {
            double theta = (2.0f * M_PI * i) / 15.0f;
            if (!s->inverse)
                theta *= -1;
            s->exptab[i].re = cosf(theta);
            s->exptab[i].im = sinf(theta);
        } else { /* Wrap around to simplify fft15 */
            s->exptab[i] = s->exptab[i - 15];
        }
    }

    /* 5-point FFT exptab */
    s->exptab[19].re = cosf(2.0f * M_PI / 5.0f);
    s->exptab[19].im = sinf(2.0f * M_PI / 5.0f);
    s->exptab[20].re = cosf(1.0f * M_PI / 5.0f);
    s->exptab[20].im = sinf(1.0f * M_PI / 5.0f);

    /* Invert the phase for an inverse transform, do nothing for a forward transform */
    if (s->inverse) {
        s->exptab[19].im *= -1;
        s->exptab[20].im *= -1;
    }

    if (ARCH_X86)
        ff_mdct15_init_x86(s);

    *ps = s;

    return 0;

fail:
    ff_mdct15_uninit(&s);
    return AVERROR(ENOMEM);
}
