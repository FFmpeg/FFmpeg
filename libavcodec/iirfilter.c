/*
 * IIR filter
 * Copyright (c) 2008 Konstantin Shishkov
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
 * different IIR filters implementation
 */

#include <math.h>

#include "libavutil/attributes.h"
#include "libavutil/common.h"

#include "iirfilter.h"

/**
 * IIR filter global parameters
 */
typedef struct FFIIRFilterCoeffs {
    int   order;
    float gain;
    int   *cx;
    float *cy;
} FFIIRFilterCoeffs;

/**
 * IIR filter state
 */
typedef struct FFIIRFilterState {
    float x[1];
} FFIIRFilterState;

/// maximum supported filter order
#define MAXORDER 30

static av_cold int butterworth_init_coeffs(void *avc,
                                           struct FFIIRFilterCoeffs *c,
                                           enum IIRFilterMode filt_mode,
                                           int order, float cutoff_ratio,
                                           float stopband)
{
    int i, j;
    double wa;
    double p[MAXORDER + 1][2];

    if (filt_mode != FF_FILTER_MODE_LOWPASS) {
        av_log(avc, AV_LOG_ERROR, "Butterworth filter currently only supports "
                                  "low-pass filter mode\n");
        return -1;
    }
    if (order & 1) {
        av_log(avc, AV_LOG_ERROR, "Butterworth filter currently only supports "
                                  "even filter orders\n");
        return -1;
    }

    wa = 2 * tan(M_PI * 0.5 * cutoff_ratio);

    c->cx[0] = 1;
    for (i = 1; i < (order >> 1) + 1; i++)
        c->cx[i] = c->cx[i - 1] * (order - i + 1LL) / i;

    p[0][0] = 1.0;
    p[0][1] = 0.0;
    for (i = 1; i <= order; i++)
        p[i][0] = p[i][1] = 0.0;
    for (i = 0; i < order; i++) {
        double zp[2];
        double th = (i + (order >> 1) + 0.5) * M_PI / order;
        double a_re, a_im, c_re, c_im;
        zp[0] = cos(th) * wa;
        zp[1] = sin(th) * wa;
        a_re  = zp[0] + 2.0;
        c_re  = zp[0] - 2.0;
        a_im  =
        c_im  = zp[1];
        zp[0] = (a_re * c_re + a_im * c_im) / (c_re * c_re + c_im * c_im);
        zp[1] = (a_im * c_re - a_re * c_im) / (c_re * c_re + c_im * c_im);

        for (j = order; j >= 1; j--) {
            a_re    = p[j][0];
            a_im    = p[j][1];
            p[j][0] = a_re * zp[0] - a_im * zp[1] + p[j - 1][0];
            p[j][1] = a_re * zp[1] + a_im * zp[0] + p[j - 1][1];
        }
        a_re    = p[0][0] * zp[0] - p[0][1] * zp[1];
        p[0][1] = p[0][0] * zp[1] + p[0][1] * zp[0];
        p[0][0] = a_re;
    }
    c->gain = p[order][0];
    for (i = 0; i < order; i++) {
        c->gain += p[i][0];
        c->cy[i] = (-p[i][0] * p[order][0] + -p[i][1] * p[order][1]) /
                   (p[order][0] * p[order][0] + p[order][1] * p[order][1]);
    }
    c->gain /= 1 << order;

    return 0;
}

static av_cold int biquad_init_coeffs(void *avc, struct FFIIRFilterCoeffs *c,
                                      enum IIRFilterMode filt_mode, int order,
                                      float cutoff_ratio, float stopband)
{
    double cos_w0, sin_w0;
    double a0, x0, x1;

    if (filt_mode != FF_FILTER_MODE_HIGHPASS &&
        filt_mode != FF_FILTER_MODE_LOWPASS) {
        av_log(avc, AV_LOG_ERROR, "Biquad filter currently only supports "
                                  "high-pass and low-pass filter modes\n");
        return -1;
    }
    if (order != 2) {
        av_log(avc, AV_LOG_ERROR, "Biquad filter must have order of 2\n");
        return -1;
    }

    cos_w0 = cos(M_PI * cutoff_ratio);
    sin_w0 = sin(M_PI * cutoff_ratio);

    a0 = 1.0 + (sin_w0 / 2.0);

    if (filt_mode == FF_FILTER_MODE_HIGHPASS) {
        c->gain  =  ((1.0 + cos_w0) / 2.0)  / a0;
        x0       =  ((1.0 + cos_w0) / 2.0)  / a0;
        x1       = (-(1.0 + cos_w0))        / a0;
    } else { // FF_FILTER_MODE_LOWPASS
        c->gain  =  ((1.0 - cos_w0) / 2.0)  / a0;
        x0       =  ((1.0 - cos_w0) / 2.0)  / a0;
        x1       =   (1.0 - cos_w0)         / a0;
    }
    c->cy[0] = (-1.0 + (sin_w0 / 2.0)) / a0;
    c->cy[1] =  (2.0 *  cos_w0)        / a0;

    // divide by gain to make the x coeffs integers.
    // during filtering, the delay state will include the gain multiplication
    c->cx[0] = lrintf(x0 / c->gain);
    c->cx[1] = lrintf(x1 / c->gain);

    return 0;
}

av_cold struct FFIIRFilterCoeffs *ff_iir_filter_init_coeffs(void *avc,
                                                            enum IIRFilterType filt_type,
                                                            enum IIRFilterMode filt_mode,
                                                            int order, float cutoff_ratio,
                                                            float stopband, float ripple)
{
    FFIIRFilterCoeffs *c;
    int ret = 0;

    if (order <= 0 || order > MAXORDER || cutoff_ratio >= 1.0)
        return NULL;

    FF_ALLOCZ_OR_GOTO(avc, c, sizeof(FFIIRFilterCoeffs),
                      init_fail);
    FF_ALLOC_OR_GOTO(avc, c->cx, sizeof(c->cx[0]) * ((order >> 1) + 1),
                     init_fail);
    FF_ALLOC_OR_GOTO(avc, c->cy, sizeof(c->cy[0]) * order,
                     init_fail);
    c->order = order;

    switch (filt_type) {
    case FF_FILTER_TYPE_BUTTERWORTH:
        ret = butterworth_init_coeffs(avc, c, filt_mode, order, cutoff_ratio,
                                      stopband);
        break;
    case FF_FILTER_TYPE_BIQUAD:
        ret = biquad_init_coeffs(avc, c, filt_mode, order, cutoff_ratio,
                                 stopband);
        break;
    default:
        av_log(avc, AV_LOG_ERROR, "filter type is not currently implemented\n");
        goto init_fail;
    }

    if (!ret)
        return c;

init_fail:
    ff_iir_filter_free_coeffsp(&c);
    return NULL;
}

av_cold struct FFIIRFilterState *ff_iir_filter_init_state(int order)
{
    FFIIRFilterState *s = av_mallocz(sizeof(FFIIRFilterState) + sizeof(s->x[0]) * (order - 1));
    return s;
}

#define CONV_S16(dest, source) dest = av_clip_int16(lrintf(source));

#define CONV_FLT(dest, source) dest = source;

#define FILTER_BW_O4_1(i0, i1, i2, i3, fmt)             \
    in = *src0    * c->gain  +                          \
         c->cy[0] * s->x[i0] +                          \
         c->cy[1] * s->x[i1] +                          \
         c->cy[2] * s->x[i2] +                          \
         c->cy[3] * s->x[i3];                           \
    res = (s->x[i0] + in)       * 1 +                   \
          (s->x[i1] + s->x[i3]) * 4 +                   \
           s->x[i2]             * 6;                    \
    CONV_ ## fmt(*dst0, res)                            \
    s->x[i0] = in;                                      \
    src0    += sstep;                                   \
    dst0    += dstep;

#define FILTER_BW_O4(type, fmt) {           \
    int i;                                  \
    const type *src0 = src;                 \
    type       *dst0 = dst;                 \
    for (i = 0; i < size; i += 4) {         \
        float in, res;                      \
        FILTER_BW_O4_1(0, 1, 2, 3, fmt);    \
        FILTER_BW_O4_1(1, 2, 3, 0, fmt);    \
        FILTER_BW_O4_1(2, 3, 0, 1, fmt);    \
        FILTER_BW_O4_1(3, 0, 1, 2, fmt);    \
    }                                       \
}

#define FILTER_DIRECT_FORM_II(type, fmt) {                                  \
    int i;                                                                  \
    const type *src0 = src;                                                 \
    type       *dst0 = dst;                                                 \
    for (i = 0; i < size; i++) {                                            \
        int j;                                                              \
        float in, res;                                                      \
        in = *src0 * c->gain;                                               \
        for (j = 0; j < c->order; j++)                                      \
            in += c->cy[j] * s->x[j];                                       \
        res = s->x[0] + in + s->x[c->order >> 1] * c->cx[c->order >> 1];    \
        for (j = 1; j < c->order >> 1; j++)                                 \
            res += (s->x[j] + s->x[c->order - j]) * c->cx[j];               \
        for (j = 0; j < c->order - 1; j++)                                  \
            s->x[j] = s->x[j + 1];                                          \
        CONV_ ## fmt(*dst0, res)                                            \
        s->x[c->order - 1] = in;                                            \
        src0              += sstep;                                         \
        dst0              += dstep;                                         \
    }                                                                       \
}

#define FILTER_O2(type, fmt) {                                              \
    int i;                                                                  \
    const type *src0 = src;                                                 \
    type       *dst0 = dst;                                                 \
    for (i = 0; i < size; i++) {                                            \
        float in = *src0   * c->gain  +                                     \
                   s->x[0] * c->cy[0] +                                     \
                   s->x[1] * c->cy[1];                                      \
        CONV_ ## fmt(*dst0, s->x[0] + in + s->x[1] * c->cx[1])              \
        s->x[0] = s->x[1];                                                  \
        s->x[1] = in;                                                       \
        src0   += sstep;                                                    \
        dst0   += dstep;                                                    \
    }                                                                       \
}

void ff_iir_filter(const struct FFIIRFilterCoeffs *c,
                   struct FFIIRFilterState *s, int size,
                   const int16_t *src, ptrdiff_t sstep,
                   int16_t *dst, ptrdiff_t dstep)
{
    if (c->order == 2) {
        FILTER_O2(int16_t, S16)
    } else if (c->order == 4) {
        FILTER_BW_O4(int16_t, S16)
    } else {
        FILTER_DIRECT_FORM_II(int16_t, S16)
    }
}

void ff_iir_filter_flt(const struct FFIIRFilterCoeffs *c,
                       struct FFIIRFilterState *s, int size,
                       const float *src, ptrdiff_t sstep,
                       float *dst, ptrdiff_t dstep)
{
    if (c->order == 2) {
        FILTER_O2(float, FLT)
    } else if (c->order == 4) {
        FILTER_BW_O4(float, FLT)
    } else {
        FILTER_DIRECT_FORM_II(float, FLT)
    }
}

av_cold void ff_iir_filter_free_statep(struct FFIIRFilterState **state)
{
    av_freep(state);
}

av_cold void ff_iir_filter_free_coeffsp(struct FFIIRFilterCoeffs **coeffsp)
{
    struct FFIIRFilterCoeffs *coeffs = *coeffsp;
    if (coeffs) {
        av_freep(&coeffs->cx);
        av_freep(&coeffs->cy);
    }
    av_freep(coeffsp);
}

void ff_iir_filter_init(FFIIRFilterContext *f) {
    f->filter_flt = ff_iir_filter_flt;

    if (HAVE_MIPSFPU)
        ff_iir_filter_init_mips(f);
}
