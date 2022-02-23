/*
 * Copyright (c) 2013 Paul B Mahol
 * Copyright (c) 2006-2008 Rob Sykes <robs@users.sourceforge.net>
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

/*
 * 2-pole filters designed by Robert Bristow-Johnson <rbj@audioimagination.com>
 *   see http://www.musicdsp.org/files/Audio-EQ-Cookbook.txt
 *
 * 1-pole filters based on code (c) 2000 Chris Bagwell <cbagwell@sprynet.com>
 *   Algorithms: Recursive single pole low/high pass filter
 *   Reference: The Scientist and Engineer's Guide to Digital Signal Processing
 *
 *   low-pass: output[N] = input[N] * A + output[N-1] * B
 *     X = exp(-2.0 * pi * Fc)
 *     A = 1 - X
 *     B = X
 *     Fc = cutoff freq / sample rate
 *
 *     Mimics an RC low-pass filter:
 *
 *     ---/\/\/\/\----------->
 *                   |
 *                  --- C
 *                  ---
 *                   |
 *                   |
 *                   V
 *
 *   high-pass: output[N] = A0 * input[N] + A1 * input[N-1] + B1 * output[N-1]
 *     X  = exp(-2.0 * pi * Fc)
 *     A0 = (1 + X) / 2
 *     A1 = -(1 + X) / 2
 *     B1 = X
 *     Fc = cutoff freq / sample rate
 *
 *     Mimics an RC high-pass filter:
 *
 *         || C
 *     ----||--------->
 *         ||    |
 *               <
 *               > R
 *               <
 *               |
 *               V
 */

#include "config_components.h"

#include "libavutil/avassert.h"
#include "libavutil/channel_layout.h"
#include "libavutil/ffmath.h"
#include "libavutil/opt.h"
#include "audio.h"
#include "avfilter.h"
#include "internal.h"

enum FilterType {
    biquad,
    equalizer,
    bass,
    treble,
    bandpass,
    bandreject,
    allpass,
    highpass,
    lowpass,
    lowshelf,
    highshelf,
};

enum WidthType {
    NONE,
    HERTZ,
    OCTAVE,
    QFACTOR,
    SLOPE,
    KHERTZ,
    NB_WTYPE,
};

enum TransformType {
    DI,
    DII,
    TDII,
    LATT,
    SVF,
    NB_TTYPE,
};

typedef struct ChanCache {
    double i1, i2;
    double o1, o2;
    int clippings;
} ChanCache;

typedef struct BiquadsContext {
    const AVClass *class;

    enum FilterType filter_type;
    int width_type;
    int poles;
    int csg;
    int transform_type;
    int precision;

    int bypass;

    double gain;
    double frequency;
    double width;
    double mix;
    char *ch_layout_str;
    AVChannelLayout ch_layout;
    int normalize;
    int order;

    double a0, a1, a2;
    double b0, b1, b2;

    double oa0, oa1, oa2;
    double ob0, ob1, ob2;

    ChanCache *cache;
    int block_align;

    void (*filter)(struct BiquadsContext *s, const void *ibuf, void *obuf, int len,
                   double *i1, double *i2, double *o1, double *o2,
                   double b0, double b1, double b2, double a1, double a2, int *clippings,
                   int disabled);
} BiquadsContext;

static int query_formats(AVFilterContext *ctx)
{
    BiquadsContext *s = ctx->priv;
    static const enum AVSampleFormat auto_sample_fmts[] = {
        AV_SAMPLE_FMT_S16P,
        AV_SAMPLE_FMT_S32P,
        AV_SAMPLE_FMT_FLTP,
        AV_SAMPLE_FMT_DBLP,
        AV_SAMPLE_FMT_NONE
    };
    enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_S16P,
        AV_SAMPLE_FMT_NONE
    };
    const enum AVSampleFormat *sample_fmts_list = sample_fmts;
    int ret = ff_set_common_all_channel_counts(ctx);
    if (ret < 0)
        return ret;

    switch (s->precision) {
    case 0:
        sample_fmts[0] = AV_SAMPLE_FMT_S16P;
        break;
    case 1:
        sample_fmts[0] = AV_SAMPLE_FMT_S32P;
        break;
    case 2:
        sample_fmts[0] = AV_SAMPLE_FMT_FLTP;
        break;
    case 3:
        sample_fmts[0] = AV_SAMPLE_FMT_DBLP;
        break;
    default:
        sample_fmts_list = auto_sample_fmts;
        break;
    }
    ret = ff_set_common_formats_from_list(ctx, sample_fmts_list);
    if (ret < 0)
        return ret;

    return ff_set_common_all_samplerates(ctx);
}

#define BIQUAD_FILTER(name, type, min, max, need_clipping)                    \
static void biquad_## name (BiquadsContext *s,                                \
                            const void *input, void *output, int len,         \
                            double *in1, double *in2,                         \
                            double *out1, double *out2,                       \
                            double b0, double b1, double b2,                  \
                            double a1, double a2, int *clippings,             \
                            int disabled)                                     \
{                                                                             \
    const type *ibuf = input;                                                 \
    type *obuf = output;                                                      \
    double i1 = *in1;                                                         \
    double i2 = *in2;                                                         \
    double o1 = *out1;                                                        \
    double o2 = *out2;                                                        \
    double wet = s->mix;                                                      \
    double dry = 1. - wet;                                                    \
    double out;                                                               \
    int i;                                                                    \
    a1 = -a1;                                                                 \
    a2 = -a2;                                                                 \
                                                                              \
    for (i = 0; i+1 < len; i++) {                                             \
        o2 = i2 * b2 + i1 * b1 + ibuf[i] * b0 + o2 * a2 + o1 * a1;            \
        i2 = ibuf[i];                                                         \
        out = o2 * wet + i2 * dry;                                            \
        if (disabled) {                                                       \
            obuf[i] = i2;                                                     \
        } else if (need_clipping && out < min) {                              \
            (*clippings)++;                                                   \
            obuf[i] = min;                                                    \
        } else if (need_clipping && out > max) {                              \
            (*clippings)++;                                                   \
            obuf[i] = max;                                                    \
        } else {                                                              \
            obuf[i] = out;                                                    \
        }                                                                     \
        i++;                                                                  \
        o1 = i1 * b2 + i2 * b1 + ibuf[i] * b0 + o1 * a2 + o2 * a1;            \
        i1 = ibuf[i];                                                         \
        out = o1 * wet + i1 * dry;                                            \
        if (disabled) {                                                       \
            obuf[i] = i1;                                                     \
        } else if (need_clipping && out < min) {                              \
            (*clippings)++;                                                   \
            obuf[i] = min;                                                    \
        } else if (need_clipping && out > max) {                              \
            (*clippings)++;                                                   \
            obuf[i] = max;                                                    \
        } else {                                                              \
            obuf[i] = out;                                                    \
        }                                                                     \
    }                                                                         \
    if (i < len) {                                                            \
        double o0 = ibuf[i] * b0 + i1 * b1 + i2 * b2 + o1 * a1 + o2 * a2;     \
        i2 = i1;                                                              \
        i1 = ibuf[i];                                                         \
        o2 = o1;                                                              \
        o1 = o0;                                                              \
        out = o0 * wet + i1 * dry;                                            \
        if (disabled) {                                                       \
            obuf[i] = i1;                                                     \
        } else if (need_clipping && out < min) {                              \
            (*clippings)++;                                                   \
            obuf[i] = min;                                                    \
        } else if (need_clipping && out > max) {                              \
            (*clippings)++;                                                   \
            obuf[i] = max;                                                    \
        } else {                                                              \
            obuf[i] = out;                                                    \
        }                                                                     \
    }                                                                         \
    *in1  = i1;                                                               \
    *in2  = i2;                                                               \
    *out1 = o1;                                                               \
    *out2 = o2;                                                               \
}

BIQUAD_FILTER(s16, int16_t, INT16_MIN, INT16_MAX, 1)
BIQUAD_FILTER(s32, int32_t, INT32_MIN, INT32_MAX, 1)
BIQUAD_FILTER(flt, float,   -1., 1., 0)
BIQUAD_FILTER(dbl, double,  -1., 1., 0)

#define BIQUAD_DII_FILTER(name, type, min, max, need_clipping)                \
static void biquad_dii_## name (BiquadsContext *s,                            \
                            const void *input, void *output, int len,         \
                            double *z1, double *z2,                           \
                            double *unused1, double *unused2,                 \
                            double b0, double b1, double b2,                  \
                            double a1, double a2, int *clippings,             \
                            int disabled)                                     \
{                                                                             \
    const type *ibuf = input;                                                 \
    type *obuf = output;                                                      \
    double w1 = *z1;                                                          \
    double w2 = *z2;                                                          \
    double wet = s->mix;                                                      \
    double dry = 1. - wet;                                                    \
    double in, out, w0;                                                       \
                                                                              \
    a1 = -a1;                                                                 \
    a2 = -a2;                                                                 \
                                                                              \
    for (int i = 0; i < len; i++) {                                           \
        in = ibuf[i];                                                         \
        w0 = in + a1 * w1 + a2 * w2;                                          \
        out = b0 * w0 + b1 * w1 + b2 * w2;                                    \
        w2 = w1;                                                              \
        w1 = w0;                                                              \
        out = out * wet + in * dry;                                           \
        if (disabled) {                                                       \
            obuf[i] = in;                                                     \
        } else if (need_clipping && out < min) {                              \
            (*clippings)++;                                                   \
            obuf[i] = min;                                                    \
        } else if (need_clipping && out > max) {                              \
            (*clippings)++;                                                   \
            obuf[i] = max;                                                    \
        } else {                                                              \
            obuf[i] = out;                                                    \
        }                                                                     \
    }                                                                         \
    *z1 = w1;                                                                 \
    *z2 = w2;                                                                 \
}

BIQUAD_DII_FILTER(s16, int16_t, INT16_MIN, INT16_MAX, 1)
BIQUAD_DII_FILTER(s32, int32_t, INT32_MIN, INT32_MAX, 1)
BIQUAD_DII_FILTER(flt, float,   -1., 1., 0)
BIQUAD_DII_FILTER(dbl, double,  -1., 1., 0)

#define BIQUAD_TDII_FILTER(name, type, min, max, need_clipping)               \
static void biquad_tdii_## name (BiquadsContext *s,                           \
                            const void *input, void *output, int len,         \
                            double *z1, double *z2,                           \
                            double *unused1, double *unused2,                 \
                            double b0, double b1, double b2,                  \
                            double a1, double a2, int *clippings,             \
                            int disabled)                                     \
{                                                                             \
    const type *ibuf = input;                                                 \
    type *obuf = output;                                                      \
    double w1 = *z1;                                                          \
    double w2 = *z2;                                                          \
    double wet = s->mix;                                                      \
    double dry = 1. - wet;                                                    \
    double in, out;                                                           \
                                                                              \
    a1 = -a1;                                                                 \
    a2 = -a2;                                                                 \
                                                                              \
    for (int i = 0; i < len; i++) {                                           \
        in = ibuf[i];                                                         \
        out = b0 * in + w1;                                                   \
        w1 = b1 * in + w2 + a1 * out;                                         \
        w2 = b2 * in + a2 * out;                                              \
        out = out * wet + in * dry;                                           \
        if (disabled) {                                                       \
            obuf[i] = in;                                                     \
        } else if (need_clipping && out < min) {                              \
            (*clippings)++;                                                   \
            obuf[i] = min;                                                    \
        } else if (need_clipping && out > max) {                              \
            (*clippings)++;                                                   \
            obuf[i] = max;                                                    \
        } else {                                                              \
            obuf[i] = out;                                                    \
        }                                                                     \
    }                                                                         \
    *z1 = w1;                                                                 \
    *z2 = w2;                                                                 \
}

BIQUAD_TDII_FILTER(s16, int16_t, INT16_MIN, INT16_MAX, 1)
BIQUAD_TDII_FILTER(s32, int32_t, INT32_MIN, INT32_MAX, 1)
BIQUAD_TDII_FILTER(flt, float,   -1., 1., 0)
BIQUAD_TDII_FILTER(dbl, double,  -1., 1., 0)

#define BIQUAD_LATT_FILTER(name, type, min, max, need_clipping)               \
static void biquad_latt_## name (BiquadsContext *s,                           \
                           const void *input, void *output, int len,          \
                           double *z1, double *z2,                            \
                           double *unused1, double *unused2,                  \
                           double v0, double v1, double v2,                   \
                           double k0, double k1, int *clippings,              \
                           int disabled)                                      \
{                                                                             \
    const type *ibuf = input;                                                 \
    type *obuf = output;                                                      \
    double s0 = *z1;                                                          \
    double s1 = *z2;                                                          \
    double wet = s->mix;                                                      \
    double dry = 1. - wet;                                                    \
    double in, out;                                                           \
    double t0, t1;                                                            \
                                                                              \
    for (int i = 0; i < len; i++) {                                           \
        out  = 0.;                                                            \
        in   = ibuf[i];                                                       \
        t0   = in - k1 * s0;                                                  \
        t1   = t0 * k1 + s0;                                                  \
        out += t1 * v2;                                                       \
                                                                              \
        t0    = t0 - k0 * s1;                                                 \
        t1    = t0 * k0 + s1;                                                 \
        out  += t1 * v1;                                                      \
                                                                              \
        out  += t0 * v0;                                                      \
        s0    = t1;                                                           \
        s1    = t0;                                                           \
                                                                              \
        out = out * wet + in * dry;                                           \
        if (disabled) {                                                       \
            obuf[i] = in;                                                     \
        } else if (need_clipping && out < min) {                              \
            (*clippings)++;                                                   \
            obuf[i] = min;                                                    \
        } else if (need_clipping && out > max) {                              \
            (*clippings)++;                                                   \
            obuf[i] = max;                                                    \
        } else {                                                              \
            obuf[i] = out;                                                    \
        }                                                                     \
    }                                                                         \
    *z1 = s0;                                                                 \
    *z2 = s1;                                                                 \
}

BIQUAD_LATT_FILTER(s16, int16_t, INT16_MIN, INT16_MAX, 1)
BIQUAD_LATT_FILTER(s32, int32_t, INT32_MIN, INT32_MAX, 1)
BIQUAD_LATT_FILTER(flt, float,   -1., 1., 0)
BIQUAD_LATT_FILTER(dbl, double,  -1., 1., 0)

#define BIQUAD_SVF_FILTER(name, type, min, max, need_clipping)                \
static void biquad_svf_## name (BiquadsContext *s,                            \
                           const void *input, void *output, int len,          \
                           double *y0, double *y1,                            \
                           double *unused1, double *unused2,                  \
                           double b0, double b1, double b2,                   \
                           double a1, double a2, int *clippings,              \
                           int disabled)                                      \
{                                                                             \
    const type *ibuf = input;                                                 \
    type *obuf = output;                                                      \
    double s0 = *y0;                                                          \
    double s1 = *y1;                                                          \
    double wet = s->mix;                                                      \
    double dry = 1. - wet;                                                    \
    double in, out;                                                           \
    double t0, t1;                                                            \
                                                                              \
    for (int i = 0; i < len; i++) {                                           \
        in   = ibuf[i];                                                       \
        out  = b2 * in + s0;                                                  \
        t0   = b0 * in + a1 * s0 + s1;                                        \
        t1   = b1 * in + a2 * s0;                                             \
        s0   = t0;                                                            \
        s1   = t1;                                                            \
                                                                              \
        out = out * wet + in * dry;                                           \
        if (disabled) {                                                       \
            obuf[i] = in;                                                     \
        } else if (need_clipping && out < min) {                              \
            (*clippings)++;                                                   \
            obuf[i] = min;                                                    \
        } else if (need_clipping && out > max) {                              \
            (*clippings)++;                                                   \
            obuf[i] = max;                                                    \
        } else {                                                              \
            obuf[i] = out;                                                    \
        }                                                                     \
    }                                                                         \
    *y0 = s0;                                                                 \
    *y1 = s1;                                                                 \
}

BIQUAD_SVF_FILTER(s16, int16_t, INT16_MIN, INT16_MAX, 1)
BIQUAD_SVF_FILTER(s32, int32_t, INT32_MIN, INT32_MAX, 1)
BIQUAD_SVF_FILTER(flt, float,   -1., 1., 0)
BIQUAD_SVF_FILTER(dbl, double,  -1., 1., 0)

static void convert_dir2latt(BiquadsContext *s)
{
    double k0, k1, v0, v1, v2;

    k1 = s->a2;
    k0 = s->a1 / (1. + k1);
    v2 = s->b2;
    v1 = s->b1 - v2 * s->a1;
    v0 = s->b0 - v1 * k0 - v2 * k1;

    s->a1 = k0;
    s->a2 = k1;
    s->b0 = v0;
    s->b1 = v1;
    s->b2 = v2;
}

static void convert_dir2svf(BiquadsContext *s)
{
    double a[2];
    double b[3];

    a[0] = -s->a1;
    a[1] = -s->a2;
    b[0] = s->b1 - s->a1 * s->b0;
    b[1] = s->b2 - s->a2 * s->b0;
    b[2] = s->b0;

    s->a1 = a[0];
    s->a2 = a[1];
    s->b0 = b[0];
    s->b1 = b[1];
    s->b2 = b[2];
}

static int config_filter(AVFilterLink *outlink, int reset)
{
    AVFilterContext *ctx    = outlink->src;
    BiquadsContext *s       = ctx->priv;
    AVFilterLink *inlink    = ctx->inputs[0];
    double A = ff_exp10(s->gain / 40);
    double w0 = 2 * M_PI * s->frequency / inlink->sample_rate;
    double K = tan(w0 / 2.);
    double alpha, beta;

    s->bypass = (((w0 > M_PI || w0 <= 0.) && reset) || (s->width <= 0.)) && (s->filter_type != biquad);
    if (s->bypass) {
        av_log(ctx, AV_LOG_WARNING, "Invalid frequency and/or width!\n");
        return 0;
    }

    if ((w0 > M_PI || w0 <= 0.) && (s->filter_type != biquad))
        return AVERROR(EINVAL);

    switch (s->width_type) {
    case NONE:
        alpha = 0.0;
        break;
    case HERTZ:
        alpha = sin(w0) / (2 * s->frequency / s->width);
        break;
    case KHERTZ:
        alpha = sin(w0) / (2 * s->frequency / (s->width * 1000));
        break;
    case OCTAVE:
        alpha = sin(w0) * sinh(log(2.) / 2 * s->width * w0 / sin(w0));
        break;
    case QFACTOR:
        alpha = sin(w0) / (2 * s->width);
        break;
    case SLOPE:
        alpha = sin(w0) / 2 * sqrt((A + 1 / A) * (1 / s->width - 1) + 2);
        break;
    default:
        av_assert0(0);
    }

    beta = 2 * sqrt(A);

    switch (s->filter_type) {
    case biquad:
        s->a0 = s->oa0;
        s->a1 = s->oa1;
        s->a2 = s->oa2;
        s->b0 = s->ob0;
        s->b1 = s->ob1;
        s->b2 = s->ob2;
        break;
    case equalizer:
        s->a0 =   1 + alpha / A;
        s->a1 =  -2 * cos(w0);
        s->a2 =   1 - alpha / A;
        s->b0 =   1 + alpha * A;
        s->b1 =  -2 * cos(w0);
        s->b2 =   1 - alpha * A;
        break;
    case bass:
        beta = sqrt((A * A + 1) - (A - 1) * (A - 1));
    case lowshelf:
        if (s->poles == 1) {
            double A = ff_exp10(s->gain / 20);
            double ro = -sin(w0 / 2. - M_PI_4) / sin(w0 / 2. + M_PI_4);
            double n = (A + 1) / (A - 1);
            double alpha1 = A == 1. ? 0. : n - FFSIGN(n) * sqrt(n * n - 1);
            double beta0 = ((1 + A) + (1 - A) * alpha1) * 0.5;
            double beta1 = ((1 - A) + (1 + A) * alpha1) * 0.5;

            s->a0 = 1 + ro * alpha1;
            s->a1 = -ro - alpha1;
            s->a2 = 0;
            s->b0 = beta0 + ro * beta1;
            s->b1 = -beta1 - ro * beta0;
            s->b2 = 0;
        } else {
            s->a0 =          (A + 1) + (A - 1) * cos(w0) + beta * alpha;
            s->a1 =    -2 * ((A - 1) + (A + 1) * cos(w0));
            s->a2 =          (A + 1) + (A - 1) * cos(w0) - beta * alpha;
            s->b0 =     A * ((A + 1) - (A - 1) * cos(w0) + beta * alpha);
            s->b1 = 2 * A * ((A - 1) - (A + 1) * cos(w0));
            s->b2 =     A * ((A + 1) - (A - 1) * cos(w0) - beta * alpha);
        }
        break;
    case treble:
        beta = sqrt((A * A + 1) - (A - 1) * (A - 1));
    case highshelf:
        if (s->poles == 1) {
            double A = ff_exp10(s->gain / 20);
            double ro = sin(w0 / 2. - M_PI_4) / sin(w0 / 2. + M_PI_4);
            double n = (A + 1) / (A - 1);
            double alpha1 = A == 1. ? 0. : n - FFSIGN(n) * sqrt(n * n - 1);
            double beta0 = ((1 + A) + (1 - A) * alpha1) * 0.5;
            double beta1 = ((1 - A) + (1 + A) * alpha1) * 0.5;

            s->a0 = 1 + ro * alpha1;
            s->a1 = ro + alpha1;
            s->a2 = 0;
            s->b0 = beta0 + ro * beta1;
            s->b1 = beta1 + ro * beta0;
            s->b2 = 0;
        } else {
            s->a0 =          (A + 1) - (A - 1) * cos(w0) + beta * alpha;
            s->a1 =     2 * ((A - 1) - (A + 1) * cos(w0));
            s->a2 =          (A + 1) - (A - 1) * cos(w0) - beta * alpha;
            s->b0 =     A * ((A + 1) + (A - 1) * cos(w0) + beta * alpha);
            s->b1 =-2 * A * ((A - 1) + (A + 1) * cos(w0));
            s->b2 =     A * ((A + 1) + (A - 1) * cos(w0) - beta * alpha);
        }
        break;
    case bandpass:
        if (s->csg) {
            s->a0 =  1 + alpha;
            s->a1 = -2 * cos(w0);
            s->a2 =  1 - alpha;
            s->b0 =  sin(w0) / 2;
            s->b1 =  0;
            s->b2 = -sin(w0) / 2;
        } else {
            s->a0 =  1 + alpha;
            s->a1 = -2 * cos(w0);
            s->a2 =  1 - alpha;
            s->b0 =  alpha;
            s->b1 =  0;
            s->b2 = -alpha;
        }
        break;
    case bandreject:
        s->a0 =  1 + alpha;
        s->a1 = -2 * cos(w0);
        s->a2 =  1 - alpha;
        s->b0 =  1;
        s->b1 = -2 * cos(w0);
        s->b2 =  1;
        break;
    case lowpass:
        if (s->poles == 1) {
            s->a0 = 1;
            s->a1 = -exp(-w0);
            s->a2 = 0;
            s->b0 = 1 + s->a1;
            s->b1 = 0;
            s->b2 = 0;
        } else {
            s->a0 =  1 + alpha;
            s->a1 = -2 * cos(w0);
            s->a2 =  1 - alpha;
            s->b0 = (1 - cos(w0)) / 2;
            s->b1 =  1 - cos(w0);
            s->b2 = (1 - cos(w0)) / 2;
        }
        break;
    case highpass:
        if (s->poles == 1) {
            s->a0 = 1;
            s->a1 = -exp(-w0);
            s->a2 = 0;
            s->b0 = (1 - s->a1) / 2;
            s->b1 = -s->b0;
            s->b2 = 0;
        } else {
            s->a0 =   1 + alpha;
            s->a1 =  -2 * cos(w0);
            s->a2 =   1 - alpha;
            s->b0 =  (1 + cos(w0)) / 2;
            s->b1 = -(1 + cos(w0));
            s->b2 =  (1 + cos(w0)) / 2;
        }
        break;
    case allpass:
        switch (s->order) {
        case 1:
            s->a0 = 1.;
            s->a1 = -(1. - K) / (1. + K);
            s->a2 = 0.;
            s->b0 = s->a1;
            s->b1 = s->a0;
            s->b2 = 0.;
            break;
        case 2:
            s->a0 =  1 + alpha;
            s->a1 = -2 * cos(w0);
            s->a2 =  1 - alpha;
            s->b0 =  1 - alpha;
            s->b1 = -2 * cos(w0);
            s->b2 =  1 + alpha;
        break;
        }
        break;
    default:
        av_assert0(0);
    }

    av_log(ctx, AV_LOG_VERBOSE, "a=%f %f %f:b=%f %f %f\n", s->a0, s->a1, s->a2, s->b0, s->b1, s->b2);

    s->a1 /= s->a0;
    s->a2 /= s->a0;
    s->b0 /= s->a0;
    s->b1 /= s->a0;
    s->b2 /= s->a0;
    s->a0 /= s->a0;

    if (s->normalize && fabs(s->b0 + s->b1 + s->b2) > 1e-6) {
        double factor = (s->a0 + s->a1 + s->a2) / (s->b0 + s->b1 + s->b2);

        s->b0 *= factor;
        s->b1 *= factor;
        s->b2 *= factor;
    }

    s->cache = av_realloc_f(s->cache, sizeof(ChanCache), inlink->ch_layout.nb_channels);
    if (!s->cache)
        return AVERROR(ENOMEM);
    if (reset)
        memset(s->cache, 0, sizeof(ChanCache) * inlink->ch_layout.nb_channels);

    switch (s->transform_type) {
    case DI:
        switch (inlink->format) {
        case AV_SAMPLE_FMT_S16P:
            s->filter = biquad_s16;
            break;
        case AV_SAMPLE_FMT_S32P:
            s->filter = biquad_s32;
            break;
        case AV_SAMPLE_FMT_FLTP:
            s->filter = biquad_flt;
            break;
        case AV_SAMPLE_FMT_DBLP:
            s->filter = biquad_dbl;
            break;
        default: av_assert0(0);
        }
        break;
    case DII:
        switch (inlink->format) {
        case AV_SAMPLE_FMT_S16P:
            s->filter = biquad_dii_s16;
            break;
        case AV_SAMPLE_FMT_S32P:
            s->filter = biquad_dii_s32;
            break;
        case AV_SAMPLE_FMT_FLTP:
            s->filter = biquad_dii_flt;
            break;
        case AV_SAMPLE_FMT_DBLP:
            s->filter = biquad_dii_dbl;
            break;
        default: av_assert0(0);
        }
        break;
    case TDII:
        switch (inlink->format) {
        case AV_SAMPLE_FMT_S16P:
            s->filter = biquad_tdii_s16;
            break;
        case AV_SAMPLE_FMT_S32P:
            s->filter = biquad_tdii_s32;
            break;
        case AV_SAMPLE_FMT_FLTP:
            s->filter = biquad_tdii_flt;
            break;
        case AV_SAMPLE_FMT_DBLP:
            s->filter = biquad_tdii_dbl;
            break;
        default: av_assert0(0);
        }
        break;
    case LATT:
        switch (inlink->format) {
        case AV_SAMPLE_FMT_S16P:
            s->filter = biquad_latt_s16;
            break;
        case AV_SAMPLE_FMT_S32P:
            s->filter = biquad_latt_s32;
            break;
        case AV_SAMPLE_FMT_FLTP:
            s->filter = biquad_latt_flt;
            break;
        case AV_SAMPLE_FMT_DBLP:
            s->filter = biquad_latt_dbl;
            break;
        default: av_assert0(0);
        }
        break;
    case SVF:
        switch (inlink->format) {
        case AV_SAMPLE_FMT_S16P:
            s->filter = biquad_svf_s16;
            break;
        case AV_SAMPLE_FMT_S32P:
            s->filter = biquad_svf_s32;
            break;
        case AV_SAMPLE_FMT_FLTP:
            s->filter = biquad_svf_flt;
            break;
        case AV_SAMPLE_FMT_DBLP:
            s->filter = biquad_svf_dbl;
            break;
        default: av_assert0(0);
        }
        break;
    default:
        av_assert0(0);
     }

     s->block_align = av_get_bytes_per_sample(inlink->format);

     if (s->transform_type == LATT)
         convert_dir2latt(s);
     else if (s->transform_type == SVF)
         convert_dir2svf(s);

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    return config_filter(outlink, 1);
}

typedef struct ThreadData {
    AVFrame *in, *out;
} ThreadData;

static int filter_channel(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    AVFilterLink *inlink = ctx->inputs[0];
    ThreadData *td = arg;
    AVFrame *buf = td->in;
    AVFrame *out_buf = td->out;
    BiquadsContext *s = ctx->priv;
    const int start = (buf->ch_layout.nb_channels * jobnr) / nb_jobs;
    const int end = (buf->ch_layout.nb_channels * (jobnr+1)) / nb_jobs;
    int ch;

    for (ch = start; ch < end; ch++) {
        enum AVChannel channel = av_channel_layout_channel_from_index(&inlink->ch_layout, ch);

        if (av_channel_layout_index_from_channel(&s->ch_layout, channel) < 0) {

            if (buf != out_buf)
                memcpy(out_buf->extended_data[ch], buf->extended_data[ch],
                       buf->nb_samples * s->block_align);
            continue;
        }

        s->filter(s, buf->extended_data[ch], out_buf->extended_data[ch], buf->nb_samples,
                  &s->cache[ch].i1, &s->cache[ch].i2, &s->cache[ch].o1, &s->cache[ch].o2,
                  s->b0, s->b1, s->b2, s->a1, s->a2, &s->cache[ch].clippings, ctx->is_disabled);
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *buf)
{
    AVFilterContext  *ctx = inlink->dst;
    BiquadsContext *s     = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out_buf;
    ThreadData td;
    int ch, ret;

    if (s->bypass)
        return ff_filter_frame(outlink, buf);

    ret = av_channel_layout_copy(&s->ch_layout, &inlink->ch_layout);
    if (ret < 0) {
        av_frame_free(&buf);
        return ret;
    }
    if (strcmp(s->ch_layout_str, "all"))
        av_channel_layout_from_string(&s->ch_layout,
                                      s->ch_layout_str);

    if (av_frame_is_writable(buf)) {
        out_buf = buf;
    } else {
        out_buf = ff_get_audio_buffer(outlink, buf->nb_samples);
        if (!out_buf) {
            av_frame_free(&buf);
            return AVERROR(ENOMEM);
        }
        av_frame_copy_props(out_buf, buf);
    }

    td.in = buf;
    td.out = out_buf;
    ff_filter_execute(ctx, filter_channel, &td, NULL,
                      FFMIN(outlink->ch_layout.nb_channels, ff_filter_get_nb_threads(ctx)));

    for (ch = 0; ch < outlink->ch_layout.nb_channels; ch++) {
        if (s->cache[ch].clippings > 0)
            av_log(ctx, AV_LOG_WARNING, "Channel %d clipping %d times. Please reduce gain.\n",
                   ch, s->cache[ch].clippings);
        s->cache[ch].clippings = 0;
    }

    if (buf != out_buf)
        av_frame_free(&buf);

    return ff_filter_frame(outlink, out_buf);
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    AVFilterLink *outlink = ctx->outputs[0];
    int ret;

    ret = ff_filter_process_command(ctx, cmd, args, res, res_len, flags);
    if (ret < 0)
        return ret;

    return config_filter(outlink, 0);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    BiquadsContext *s = ctx->priv;

    av_freep(&s->cache);
    av_channel_layout_uninit(&s->ch_layout);
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
    },
};

static const AVFilterPad outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_output,
    },
};

#define OFFSET(x) offsetof(BiquadsContext, x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM
#define AF AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

#define DEFINE_BIQUAD_FILTER_2(name_, description_, priv_class_)        \
static av_cold int name_##_init(AVFilterContext *ctx)                   \
{                                                                       \
    BiquadsContext *s = ctx->priv;                                      \
    s->filter_type = name_;                                             \
    return 0;                                                           \
}                                                                       \
                                                         \
const AVFilter ff_af_##name_ = {                         \
    .name          = #name_,                             \
    .description   = NULL_IF_CONFIG_SMALL(description_), \
    .priv_class    = &priv_class_##_class,               \
    .priv_size     = sizeof(BiquadsContext),             \
    .init          = name_##_init,                       \
    .uninit        = uninit,                             \
    FILTER_INPUTS(inputs),                               \
    FILTER_OUTPUTS(outputs),                             \
    FILTER_QUERY_FUNC(query_formats),                    \
    .process_command = process_command,                  \
    .flags         = AVFILTER_FLAG_SLICE_THREADS | AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL, \
}

#define DEFINE_BIQUAD_FILTER(name, description)                         \
    AVFILTER_DEFINE_CLASS(name);                                        \
    DEFINE_BIQUAD_FILTER_2(name, description, name)

#if CONFIG_EQUALIZER_FILTER
static const AVOption equalizer_options[] = {
    {"frequency", "set central frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=0}, 0, 999999, FLAGS},
    {"f",         "set central frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=0}, 0, 999999, FLAGS},
    {"width_type", "set filter-width type", OFFSET(width_type), AV_OPT_TYPE_INT, {.i64=QFACTOR}, HERTZ, NB_WTYPE-1, FLAGS, "width_type"},
    {"t",          "set filter-width type", OFFSET(width_type), AV_OPT_TYPE_INT, {.i64=QFACTOR}, HERTZ, NB_WTYPE-1, FLAGS, "width_type"},
    {"h", "Hz", 0, AV_OPT_TYPE_CONST, {.i64=HERTZ}, 0, 0, FLAGS, "width_type"},
    {"q", "Q-Factor", 0, AV_OPT_TYPE_CONST, {.i64=QFACTOR}, 0, 0, FLAGS, "width_type"},
    {"o", "octave", 0, AV_OPT_TYPE_CONST, {.i64=OCTAVE}, 0, 0, FLAGS, "width_type"},
    {"s", "slope", 0, AV_OPT_TYPE_CONST, {.i64=SLOPE}, 0, 0, FLAGS, "width_type"},
    {"k", "kHz", 0, AV_OPT_TYPE_CONST, {.i64=KHERTZ}, 0, 0, FLAGS, "width_type"},
    {"width", "set band-width", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 99999, FLAGS},
    {"w",     "set band-width", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 99999, FLAGS},
    {"gain", "set gain", OFFSET(gain), AV_OPT_TYPE_DOUBLE, {.dbl=0}, -900, 900, FLAGS},
    {"g",    "set gain", OFFSET(gain), AV_OPT_TYPE_DOUBLE, {.dbl=0}, -900, 900, FLAGS},
    {"mix", "set mix", OFFSET(mix), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS},
    {"m",   "set mix", OFFSET(mix), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS},
    {"channels", "set channels to filter", OFFSET(ch_layout_str), AV_OPT_TYPE_STRING, {.str="all"}, 0, 0, FLAGS},
    {"c",        "set channels to filter", OFFSET(ch_layout_str), AV_OPT_TYPE_STRING, {.str="all"}, 0, 0, FLAGS},
    {"normalize", "normalize coefficients", OFFSET(normalize), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS},
    {"n",         "normalize coefficients", OFFSET(normalize), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS},
    {"transform", "set transform type", OFFSET(transform_type), AV_OPT_TYPE_INT, {.i64=0}, 0, NB_TTYPE-1, AF, "transform_type"},
    {"a",         "set transform type", OFFSET(transform_type), AV_OPT_TYPE_INT, {.i64=0}, 0, NB_TTYPE-1, AF, "transform_type"},
    {"di",   "direct form I",  0, AV_OPT_TYPE_CONST, {.i64=DI}, 0, 0, AF, "transform_type"},
    {"dii",  "direct form II", 0, AV_OPT_TYPE_CONST, {.i64=DII}, 0, 0, AF, "transform_type"},
    {"tdii", "transposed direct form II", 0, AV_OPT_TYPE_CONST, {.i64=TDII}, 0, 0, AF, "transform_type"},
    {"latt", "lattice-ladder form", 0, AV_OPT_TYPE_CONST, {.i64=LATT}, 0, 0, AF, "transform_type"},
    {"svf",  "state variable filter form", 0, AV_OPT_TYPE_CONST, {.i64=SVF}, 0, 0, AF, "transform_type"},
    {"precision", "set filtering precision", OFFSET(precision), AV_OPT_TYPE_INT, {.i64=-1}, -1, 3, AF, "precision"},
    {"r",         "set filtering precision", OFFSET(precision), AV_OPT_TYPE_INT, {.i64=-1}, -1, 3, AF, "precision"},
    {"auto", "automatic",            0, AV_OPT_TYPE_CONST, {.i64=-1}, 0, 0, AF, "precision"},
    {"s16", "signed 16-bit",         0, AV_OPT_TYPE_CONST, {.i64=0},  0, 0, AF, "precision"},
    {"s32", "signed 32-bit",         0, AV_OPT_TYPE_CONST, {.i64=1},  0, 0, AF, "precision"},
    {"f32", "floating-point single", 0, AV_OPT_TYPE_CONST, {.i64=2},  0, 0, AF, "precision"},
    {"f64", "floating-point double", 0, AV_OPT_TYPE_CONST, {.i64=3},  0, 0, AF, "precision"},
    {NULL}
};

DEFINE_BIQUAD_FILTER(equalizer, "Apply two-pole peaking equalization (EQ) filter.");
#endif  /* CONFIG_EQUALIZER_FILTER */
#if CONFIG_BASS_FILTER || CONFIG_LOWSHELF_FILTER
static const AVOption bass_lowshelf_options[] = {
    {"frequency", "set central frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=100}, 0, 999999, FLAGS},
    {"f",         "set central frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=100}, 0, 999999, FLAGS},
    {"width_type", "set filter-width type", OFFSET(width_type), AV_OPT_TYPE_INT, {.i64=QFACTOR}, HERTZ, NB_WTYPE-1, FLAGS, "width_type"},
    {"t",          "set filter-width type", OFFSET(width_type), AV_OPT_TYPE_INT, {.i64=QFACTOR}, HERTZ, NB_WTYPE-1, FLAGS, "width_type"},
    {"h", "Hz", 0, AV_OPT_TYPE_CONST, {.i64=HERTZ}, 0, 0, FLAGS, "width_type"},
    {"q", "Q-Factor", 0, AV_OPT_TYPE_CONST, {.i64=QFACTOR}, 0, 0, FLAGS, "width_type"},
    {"o", "octave", 0, AV_OPT_TYPE_CONST, {.i64=OCTAVE}, 0, 0, FLAGS, "width_type"},
    {"s", "slope", 0, AV_OPT_TYPE_CONST, {.i64=SLOPE}, 0, 0, FLAGS, "width_type"},
    {"k", "kHz", 0, AV_OPT_TYPE_CONST, {.i64=KHERTZ}, 0, 0, FLAGS, "width_type"},
    {"width", "set shelf transition steep", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=0.5}, 0, 99999, FLAGS},
    {"w",     "set shelf transition steep", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=0.5}, 0, 99999, FLAGS},
    {"gain", "set gain", OFFSET(gain), AV_OPT_TYPE_DOUBLE, {.dbl=0}, -900, 900, FLAGS},
    {"g",    "set gain", OFFSET(gain), AV_OPT_TYPE_DOUBLE, {.dbl=0}, -900, 900, FLAGS},
    {"poles", "set number of poles", OFFSET(poles), AV_OPT_TYPE_INT, {.i64=2}, 1, 2, AF},
    {"p",     "set number of poles", OFFSET(poles), AV_OPT_TYPE_INT, {.i64=2}, 1, 2, AF},
    {"mix", "set mix", OFFSET(mix), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS},
    {"m",   "set mix", OFFSET(mix), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS},
    {"channels", "set channels to filter", OFFSET(ch_layout_str), AV_OPT_TYPE_STRING, {.str="all"}, 0, 0, FLAGS},
    {"c",        "set channels to filter", OFFSET(ch_layout_str), AV_OPT_TYPE_STRING, {.str="all"}, 0, 0, FLAGS},
    {"normalize", "normalize coefficients", OFFSET(normalize), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS},
    {"n",         "normalize coefficients", OFFSET(normalize), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS},
    {"transform", "set transform type", OFFSET(transform_type), AV_OPT_TYPE_INT, {.i64=0}, 0, NB_TTYPE-1, AF, "transform_type"},
    {"a",         "set transform type", OFFSET(transform_type), AV_OPT_TYPE_INT, {.i64=0}, 0, NB_TTYPE-1, AF, "transform_type"},
    {"di",   "direct form I",  0, AV_OPT_TYPE_CONST, {.i64=DI}, 0, 0, AF, "transform_type"},
    {"dii",  "direct form II", 0, AV_OPT_TYPE_CONST, {.i64=DII}, 0, 0, AF, "transform_type"},
    {"tdii", "transposed direct form II", 0, AV_OPT_TYPE_CONST, {.i64=TDII}, 0, 0, AF, "transform_type"},
    {"latt", "lattice-ladder form", 0, AV_OPT_TYPE_CONST, {.i64=LATT}, 0, 0, AF, "transform_type"},
    {"svf",  "state variable filter form", 0, AV_OPT_TYPE_CONST, {.i64=SVF}, 0, 0, AF, "transform_type"},
    {"precision", "set filtering precision", OFFSET(precision), AV_OPT_TYPE_INT, {.i64=-1}, -1, 3, AF, "precision"},
    {"r",         "set filtering precision", OFFSET(precision), AV_OPT_TYPE_INT, {.i64=-1}, -1, 3, AF, "precision"},
    {"auto", "automatic",            0, AV_OPT_TYPE_CONST, {.i64=-1}, 0, 0, AF, "precision"},
    {"s16", "signed 16-bit",         0, AV_OPT_TYPE_CONST, {.i64=0},  0, 0, AF, "precision"},
    {"s32", "signed 32-bit",         0, AV_OPT_TYPE_CONST, {.i64=1},  0, 0, AF, "precision"},
    {"f32", "floating-point single", 0, AV_OPT_TYPE_CONST, {.i64=2},  0, 0, AF, "precision"},
    {"f64", "floating-point double", 0, AV_OPT_TYPE_CONST, {.i64=3},  0, 0, AF, "precision"},
    {NULL}
};

AVFILTER_DEFINE_CLASS_EXT(bass_lowshelf, "bass/lowshelf", bass_lowshelf_options);
#if CONFIG_BASS_FILTER
DEFINE_BIQUAD_FILTER_2(bass, "Boost or cut lower frequencies.", bass_lowshelf);
#endif  /* CONFIG_BASS_FILTER */

#if CONFIG_LOWSHELF_FILTER
DEFINE_BIQUAD_FILTER_2(lowshelf, "Apply a low shelf filter.", bass_lowshelf);
#endif  /* CONFIG_LOWSHELF_FILTER */
#endif  /* CONFIG_BASS_FILTER || CONFIG LOWSHELF_FILTER */
#if CONFIG_TREBLE_FILTER || CONFIG_HIGHSHELF_FILTER
static const AVOption treble_highshelf_options[] = {
    {"frequency", "set central frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=3000}, 0, 999999, FLAGS},
    {"f",         "set central frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=3000}, 0, 999999, FLAGS},
    {"width_type", "set filter-width type", OFFSET(width_type), AV_OPT_TYPE_INT, {.i64=QFACTOR}, HERTZ, NB_WTYPE-1, FLAGS, "width_type"},
    {"t",          "set filter-width type", OFFSET(width_type), AV_OPT_TYPE_INT, {.i64=QFACTOR}, HERTZ, NB_WTYPE-1, FLAGS, "width_type"},
    {"h", "Hz", 0, AV_OPT_TYPE_CONST, {.i64=HERTZ}, 0, 0, FLAGS, "width_type"},
    {"q", "Q-Factor", 0, AV_OPT_TYPE_CONST, {.i64=QFACTOR}, 0, 0, FLAGS, "width_type"},
    {"o", "octave", 0, AV_OPT_TYPE_CONST, {.i64=OCTAVE}, 0, 0, FLAGS, "width_type"},
    {"s", "slope", 0, AV_OPT_TYPE_CONST, {.i64=SLOPE}, 0, 0, FLAGS, "width_type"},
    {"k", "kHz", 0, AV_OPT_TYPE_CONST, {.i64=KHERTZ}, 0, 0, FLAGS, "width_type"},
    {"width", "set shelf transition steep", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=0.5}, 0, 99999, FLAGS},
    {"w",     "set shelf transition steep", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=0.5}, 0, 99999, FLAGS},
    {"gain", "set gain", OFFSET(gain), AV_OPT_TYPE_DOUBLE, {.dbl=0}, -900, 900, FLAGS},
    {"g",    "set gain", OFFSET(gain), AV_OPT_TYPE_DOUBLE, {.dbl=0}, -900, 900, FLAGS},
    {"poles", "set number of poles", OFFSET(poles), AV_OPT_TYPE_INT, {.i64=2}, 1, 2, AF},
    {"p",     "set number of poles", OFFSET(poles), AV_OPT_TYPE_INT, {.i64=2}, 1, 2, AF},
    {"mix", "set mix", OFFSET(mix), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS},
    {"m",   "set mix", OFFSET(mix), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS},
    {"channels", "set channels to filter", OFFSET(ch_layout_str), AV_OPT_TYPE_STRING, {.str="all"}, 0, 0, FLAGS},
    {"c",        "set channels to filter", OFFSET(ch_layout_str), AV_OPT_TYPE_STRING, {.str="all"}, 0, 0, FLAGS},
    {"normalize", "normalize coefficients", OFFSET(normalize), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS},
    {"n",         "normalize coefficients", OFFSET(normalize), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS},
    {"transform", "set transform type", OFFSET(transform_type), AV_OPT_TYPE_INT, {.i64=0}, 0, NB_TTYPE-1, AF, "transform_type"},
    {"a",         "set transform type", OFFSET(transform_type), AV_OPT_TYPE_INT, {.i64=0}, 0, NB_TTYPE-1, AF, "transform_type"},
    {"di",   "direct form I",  0, AV_OPT_TYPE_CONST, {.i64=DI}, 0, 0, AF, "transform_type"},
    {"dii",  "direct form II", 0, AV_OPT_TYPE_CONST, {.i64=DII}, 0, 0, AF, "transform_type"},
    {"tdii", "transposed direct form II", 0, AV_OPT_TYPE_CONST, {.i64=TDII}, 0, 0, AF, "transform_type"},
    {"latt", "lattice-ladder form", 0, AV_OPT_TYPE_CONST, {.i64=LATT}, 0, 0, AF, "transform_type"},
    {"svf",  "state variable filter form", 0, AV_OPT_TYPE_CONST, {.i64=SVF}, 0, 0, AF, "transform_type"},
    {"precision", "set filtering precision", OFFSET(precision), AV_OPT_TYPE_INT, {.i64=-1}, -1, 3, AF, "precision"},
    {"r",         "set filtering precision", OFFSET(precision), AV_OPT_TYPE_INT, {.i64=-1}, -1, 3, AF, "precision"},
    {"auto", "automatic",            0, AV_OPT_TYPE_CONST, {.i64=-1}, 0, 0, AF, "precision"},
    {"s16", "signed 16-bit",         0, AV_OPT_TYPE_CONST, {.i64=0},  0, 0, AF, "precision"},
    {"s32", "signed 32-bit",         0, AV_OPT_TYPE_CONST, {.i64=1},  0, 0, AF, "precision"},
    {"f32", "floating-point single", 0, AV_OPT_TYPE_CONST, {.i64=2},  0, 0, AF, "precision"},
    {"f64", "floating-point double", 0, AV_OPT_TYPE_CONST, {.i64=3},  0, 0, AF, "precision"},
    {NULL}
};

AVFILTER_DEFINE_CLASS_EXT(treble_highshelf, "treble/highshelf",
                          treble_highshelf_options);

#if CONFIG_TREBLE_FILTER
DEFINE_BIQUAD_FILTER_2(treble, "Boost or cut upper frequencies.", treble_highshelf);
#endif  /* CONFIG_TREBLE_FILTER */

#if CONFIG_HIGHSHELF_FILTER
DEFINE_BIQUAD_FILTER_2(highshelf, "Apply a high shelf filter.", treble_highshelf);
#endif  /* CONFIG_HIGHSHELF_FILTER */
#endif  /* CONFIG_TREBLE_FILTER || CONFIG_HIGHSHELF_FILTER */
#if CONFIG_BANDPASS_FILTER
static const AVOption bandpass_options[] = {
    {"frequency", "set central frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=3000}, 0, 999999, FLAGS},
    {"f",         "set central frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=3000}, 0, 999999, FLAGS},
    {"width_type", "set filter-width type", OFFSET(width_type), AV_OPT_TYPE_INT, {.i64=QFACTOR}, HERTZ, NB_WTYPE-1, FLAGS, "width_type"},
    {"t",          "set filter-width type", OFFSET(width_type), AV_OPT_TYPE_INT, {.i64=QFACTOR}, HERTZ, NB_WTYPE-1, FLAGS, "width_type"},
    {"h", "Hz", 0, AV_OPT_TYPE_CONST, {.i64=HERTZ}, 0, 0, FLAGS, "width_type"},
    {"q", "Q-Factor", 0, AV_OPT_TYPE_CONST, {.i64=QFACTOR}, 0, 0, FLAGS, "width_type"},
    {"o", "octave", 0, AV_OPT_TYPE_CONST, {.i64=OCTAVE}, 0, 0, FLAGS, "width_type"},
    {"s", "slope", 0, AV_OPT_TYPE_CONST, {.i64=SLOPE}, 0, 0, FLAGS, "width_type"},
    {"k", "kHz", 0, AV_OPT_TYPE_CONST, {.i64=KHERTZ}, 0, 0, FLAGS, "width_type"},
    {"width", "set band-width", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=0.5}, 0, 99999, FLAGS},
    {"w",     "set band-width", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=0.5}, 0, 99999, FLAGS},
    {"csg",   "use constant skirt gain", OFFSET(csg), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS},
    {"mix", "set mix", OFFSET(mix), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS},
    {"m",   "set mix", OFFSET(mix), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS},
    {"channels", "set channels to filter", OFFSET(ch_layout_str), AV_OPT_TYPE_STRING, {.str="all"}, 0, 0, FLAGS},
    {"c",        "set channels to filter", OFFSET(ch_layout_str), AV_OPT_TYPE_STRING, {.str="all"}, 0, 0, FLAGS},
    {"normalize", "normalize coefficients", OFFSET(normalize), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS},
    {"n",         "normalize coefficients", OFFSET(normalize), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS},
    {"transform", "set transform type", OFFSET(transform_type), AV_OPT_TYPE_INT, {.i64=0}, 0, NB_TTYPE-1, AF, "transform_type"},
    {"a",         "set transform type", OFFSET(transform_type), AV_OPT_TYPE_INT, {.i64=0}, 0, NB_TTYPE-1, AF, "transform_type"},
    {"di",   "direct form I",  0, AV_OPT_TYPE_CONST, {.i64=DI}, 0, 0, AF, "transform_type"},
    {"dii",  "direct form II", 0, AV_OPT_TYPE_CONST, {.i64=DII}, 0, 0, AF, "transform_type"},
    {"tdii", "transposed direct form II", 0, AV_OPT_TYPE_CONST, {.i64=TDII}, 0, 0, AF, "transform_type"},
    {"latt", "lattice-ladder form", 0, AV_OPT_TYPE_CONST, {.i64=LATT}, 0, 0, AF, "transform_type"},
    {"svf",  "state variable filter form", 0, AV_OPT_TYPE_CONST, {.i64=SVF}, 0, 0, AF, "transform_type"},
    {"precision", "set filtering precision", OFFSET(precision), AV_OPT_TYPE_INT, {.i64=-1}, -1, 3, AF, "precision"},
    {"r",         "set filtering precision", OFFSET(precision), AV_OPT_TYPE_INT, {.i64=-1}, -1, 3, AF, "precision"},
    {"auto", "automatic",            0, AV_OPT_TYPE_CONST, {.i64=-1}, 0, 0, AF, "precision"},
    {"s16", "signed 16-bit",         0, AV_OPT_TYPE_CONST, {.i64=0},  0, 0, AF, "precision"},
    {"s32", "signed 32-bit",         0, AV_OPT_TYPE_CONST, {.i64=1},  0, 0, AF, "precision"},
    {"f32", "floating-point single", 0, AV_OPT_TYPE_CONST, {.i64=2},  0, 0, AF, "precision"},
    {"f64", "floating-point double", 0, AV_OPT_TYPE_CONST, {.i64=3},  0, 0, AF, "precision"},
    {NULL}
};

DEFINE_BIQUAD_FILTER(bandpass, "Apply a two-pole Butterworth band-pass filter.");
#endif  /* CONFIG_BANDPASS_FILTER */
#if CONFIG_BANDREJECT_FILTER
static const AVOption bandreject_options[] = {
    {"frequency", "set central frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=3000}, 0, 999999, FLAGS},
    {"f",         "set central frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=3000}, 0, 999999, FLAGS},
    {"width_type", "set filter-width type", OFFSET(width_type), AV_OPT_TYPE_INT, {.i64=QFACTOR}, HERTZ, NB_WTYPE-1, FLAGS, "width_type"},
    {"t",          "set filter-width type", OFFSET(width_type), AV_OPT_TYPE_INT, {.i64=QFACTOR}, HERTZ, NB_WTYPE-1, FLAGS, "width_type"},
    {"h", "Hz", 0, AV_OPT_TYPE_CONST, {.i64=HERTZ}, 0, 0, FLAGS, "width_type"},
    {"q", "Q-Factor", 0, AV_OPT_TYPE_CONST, {.i64=QFACTOR}, 0, 0, FLAGS, "width_type"},
    {"o", "octave", 0, AV_OPT_TYPE_CONST, {.i64=OCTAVE}, 0, 0, FLAGS, "width_type"},
    {"s", "slope", 0, AV_OPT_TYPE_CONST, {.i64=SLOPE}, 0, 0, FLAGS, "width_type"},
    {"k", "kHz", 0, AV_OPT_TYPE_CONST, {.i64=KHERTZ}, 0, 0, FLAGS, "width_type"},
    {"width", "set band-width", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=0.5}, 0, 99999, FLAGS},
    {"w",     "set band-width", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=0.5}, 0, 99999, FLAGS},
    {"mix", "set mix", OFFSET(mix), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS},
    {"m",   "set mix", OFFSET(mix), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS},
    {"channels", "set channels to filter", OFFSET(ch_layout_str), AV_OPT_TYPE_STRING, {.str="all"}, 0, 0, FLAGS},
    {"c",        "set channels to filter", OFFSET(ch_layout_str), AV_OPT_TYPE_STRING, {.str="all"}, 0, 0, FLAGS},
    {"normalize", "normalize coefficients", OFFSET(normalize), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS},
    {"n",         "normalize coefficients", OFFSET(normalize), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS},
    {"transform", "set transform type", OFFSET(transform_type), AV_OPT_TYPE_INT, {.i64=0}, 0, NB_TTYPE-1, AF, "transform_type"},
    {"a",         "set transform type", OFFSET(transform_type), AV_OPT_TYPE_INT, {.i64=0}, 0, NB_TTYPE-1, AF, "transform_type"},
    {"di",   "direct form I",  0, AV_OPT_TYPE_CONST, {.i64=DI}, 0, 0, AF, "transform_type"},
    {"dii",  "direct form II", 0, AV_OPT_TYPE_CONST, {.i64=DII}, 0, 0, AF, "transform_type"},
    {"tdii", "transposed direct form II", 0, AV_OPT_TYPE_CONST, {.i64=TDII}, 0, 0, AF, "transform_type"},
    {"latt", "lattice-ladder form", 0, AV_OPT_TYPE_CONST, {.i64=LATT}, 0, 0, AF, "transform_type"},
    {"svf",  "state variable filter form", 0, AV_OPT_TYPE_CONST, {.i64=SVF}, 0, 0, AF, "transform_type"},
    {"precision", "set filtering precision", OFFSET(precision), AV_OPT_TYPE_INT, {.i64=-1}, -1, 3, AF, "precision"},
    {"r",         "set filtering precision", OFFSET(precision), AV_OPT_TYPE_INT, {.i64=-1}, -1, 3, AF, "precision"},
    {"auto", "automatic",            0, AV_OPT_TYPE_CONST, {.i64=-1}, 0, 0, AF, "precision"},
    {"s16", "signed 16-bit",         0, AV_OPT_TYPE_CONST, {.i64=0},  0, 0, AF, "precision"},
    {"s32", "signed 32-bit",         0, AV_OPT_TYPE_CONST, {.i64=1},  0, 0, AF, "precision"},
    {"f32", "floating-point single", 0, AV_OPT_TYPE_CONST, {.i64=2},  0, 0, AF, "precision"},
    {"f64", "floating-point double", 0, AV_OPT_TYPE_CONST, {.i64=3},  0, 0, AF, "precision"},
    {NULL}
};

DEFINE_BIQUAD_FILTER(bandreject, "Apply a two-pole Butterworth band-reject filter.");
#endif  /* CONFIG_BANDREJECT_FILTER */
#if CONFIG_LOWPASS_FILTER
static const AVOption lowpass_options[] = {
    {"frequency", "set frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=500}, 0, 999999, FLAGS},
    {"f",         "set frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=500}, 0, 999999, FLAGS},
    {"width_type", "set filter-width type", OFFSET(width_type), AV_OPT_TYPE_INT, {.i64=QFACTOR}, HERTZ, NB_WTYPE-1, FLAGS, "width_type"},
    {"t",          "set filter-width type", OFFSET(width_type), AV_OPT_TYPE_INT, {.i64=QFACTOR}, HERTZ, NB_WTYPE-1, FLAGS, "width_type"},
    {"h", "Hz", 0, AV_OPT_TYPE_CONST, {.i64=HERTZ}, 0, 0, FLAGS, "width_type"},
    {"q", "Q-Factor", 0, AV_OPT_TYPE_CONST, {.i64=QFACTOR}, 0, 0, FLAGS, "width_type"},
    {"o", "octave", 0, AV_OPT_TYPE_CONST, {.i64=OCTAVE}, 0, 0, FLAGS, "width_type"},
    {"s", "slope", 0, AV_OPT_TYPE_CONST, {.i64=SLOPE}, 0, 0, FLAGS, "width_type"},
    {"k", "kHz", 0, AV_OPT_TYPE_CONST, {.i64=KHERTZ}, 0, 0, FLAGS, "width_type"},
    {"width", "set width", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=0.707}, 0, 99999, FLAGS},
    {"w",     "set width", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=0.707}, 0, 99999, FLAGS},
    {"poles", "set number of poles", OFFSET(poles), AV_OPT_TYPE_INT, {.i64=2}, 1, 2, AF},
    {"p",     "set number of poles", OFFSET(poles), AV_OPT_TYPE_INT, {.i64=2}, 1, 2, AF},
    {"mix", "set mix", OFFSET(mix), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS},
    {"m",   "set mix", OFFSET(mix), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS},
    {"channels", "set channels to filter", OFFSET(ch_layout_str), AV_OPT_TYPE_STRING, {.str="all"}, 0, 0, FLAGS},
    {"c",        "set channels to filter", OFFSET(ch_layout_str), AV_OPT_TYPE_STRING, {.str="all"}, 0, 0, FLAGS},
    {"normalize", "normalize coefficients", OFFSET(normalize), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS},
    {"n",         "normalize coefficients", OFFSET(normalize), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS},
    {"transform", "set transform type", OFFSET(transform_type), AV_OPT_TYPE_INT, {.i64=0}, 0, NB_TTYPE-1, AF, "transform_type"},
    {"a",         "set transform type", OFFSET(transform_type), AV_OPT_TYPE_INT, {.i64=0}, 0, NB_TTYPE-1, AF, "transform_type"},
    {"di",   "direct form I",  0, AV_OPT_TYPE_CONST, {.i64=DI}, 0, 0, AF, "transform_type"},
    {"dii",  "direct form II", 0, AV_OPT_TYPE_CONST, {.i64=DII}, 0, 0, AF, "transform_type"},
    {"tdii", "transposed direct form II", 0, AV_OPT_TYPE_CONST, {.i64=TDII}, 0, 0, AF, "transform_type"},
    {"latt", "lattice-ladder form", 0, AV_OPT_TYPE_CONST, {.i64=LATT}, 0, 0, AF, "transform_type"},
    {"svf",  "state variable filter form", 0, AV_OPT_TYPE_CONST, {.i64=SVF}, 0, 0, AF, "transform_type"},
    {"precision", "set filtering precision", OFFSET(precision), AV_OPT_TYPE_INT, {.i64=-1}, -1, 3, AF, "precision"},
    {"r",         "set filtering precision", OFFSET(precision), AV_OPT_TYPE_INT, {.i64=-1}, -1, 3, AF, "precision"},
    {"auto", "automatic",            0, AV_OPT_TYPE_CONST, {.i64=-1}, 0, 0, AF, "precision"},
    {"s16", "signed 16-bit",         0, AV_OPT_TYPE_CONST, {.i64=0},  0, 0, AF, "precision"},
    {"s32", "signed 32-bit",         0, AV_OPT_TYPE_CONST, {.i64=1},  0, 0, AF, "precision"},
    {"f32", "floating-point single", 0, AV_OPT_TYPE_CONST, {.i64=2},  0, 0, AF, "precision"},
    {"f64", "floating-point double", 0, AV_OPT_TYPE_CONST, {.i64=3},  0, 0, AF, "precision"},
    {NULL}
};

DEFINE_BIQUAD_FILTER(lowpass, "Apply a low-pass filter with 3dB point frequency.");
#endif  /* CONFIG_LOWPASS_FILTER */
#if CONFIG_HIGHPASS_FILTER
static const AVOption highpass_options[] = {
    {"frequency", "set frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=3000}, 0, 999999, FLAGS},
    {"f",         "set frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=3000}, 0, 999999, FLAGS},
    {"width_type", "set filter-width type", OFFSET(width_type), AV_OPT_TYPE_INT, {.i64=QFACTOR}, HERTZ, NB_WTYPE-1, FLAGS, "width_type"},
    {"t",          "set filter-width type", OFFSET(width_type), AV_OPT_TYPE_INT, {.i64=QFACTOR}, HERTZ, NB_WTYPE-1, FLAGS, "width_type"},
    {"h", "Hz", 0, AV_OPT_TYPE_CONST, {.i64=HERTZ}, 0, 0, FLAGS, "width_type"},
    {"q", "Q-Factor", 0, AV_OPT_TYPE_CONST, {.i64=QFACTOR}, 0, 0, FLAGS, "width_type"},
    {"o", "octave", 0, AV_OPT_TYPE_CONST, {.i64=OCTAVE}, 0, 0, FLAGS, "width_type"},
    {"s", "slope", 0, AV_OPT_TYPE_CONST, {.i64=SLOPE}, 0, 0, FLAGS, "width_type"},
    {"k", "kHz", 0, AV_OPT_TYPE_CONST, {.i64=KHERTZ}, 0, 0, FLAGS, "width_type"},
    {"width", "set width", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=0.707}, 0, 99999, FLAGS},
    {"w",     "set width", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=0.707}, 0, 99999, FLAGS},
    {"poles", "set number of poles", OFFSET(poles), AV_OPT_TYPE_INT, {.i64=2}, 1, 2, AF},
    {"p",     "set number of poles", OFFSET(poles), AV_OPT_TYPE_INT, {.i64=2}, 1, 2, AF},
    {"mix", "set mix", OFFSET(mix), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS},
    {"m",   "set mix", OFFSET(mix), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS},
    {"channels", "set channels to filter", OFFSET(ch_layout_str), AV_OPT_TYPE_STRING, {.str="all"}, 0, 0, FLAGS},
    {"c",        "set channels to filter", OFFSET(ch_layout_str), AV_OPT_TYPE_STRING, {.str="all"}, 0, 0, FLAGS},
    {"normalize", "normalize coefficients", OFFSET(normalize), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS},
    {"n",         "normalize coefficients", OFFSET(normalize), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS},
    {"transform", "set transform type", OFFSET(transform_type), AV_OPT_TYPE_INT, {.i64=0}, 0, NB_TTYPE-1, AF, "transform_type"},
    {"a",         "set transform type", OFFSET(transform_type), AV_OPT_TYPE_INT, {.i64=0}, 0, NB_TTYPE-1, AF, "transform_type"},
    {"di",   "direct form I",  0, AV_OPT_TYPE_CONST, {.i64=DI}, 0, 0, AF, "transform_type"},
    {"dii",  "direct form II", 0, AV_OPT_TYPE_CONST, {.i64=DII}, 0, 0, AF, "transform_type"},
    {"tdii", "transposed direct form II", 0, AV_OPT_TYPE_CONST, {.i64=TDII}, 0, 0, AF, "transform_type"},
    {"latt", "lattice-ladder form", 0, AV_OPT_TYPE_CONST, {.i64=LATT}, 0, 0, AF, "transform_type"},
    {"svf",  "state variable filter form", 0, AV_OPT_TYPE_CONST, {.i64=SVF}, 0, 0, AF, "transform_type"},
    {"precision", "set filtering precision", OFFSET(precision), AV_OPT_TYPE_INT, {.i64=-1}, -1, 3, AF, "precision"},
    {"r",         "set filtering precision", OFFSET(precision), AV_OPT_TYPE_INT, {.i64=-1}, -1, 3, AF, "precision"},
    {"auto", "automatic",            0, AV_OPT_TYPE_CONST, {.i64=-1}, 0, 0, AF, "precision"},
    {"s16", "signed 16-bit",         0, AV_OPT_TYPE_CONST, {.i64=0},  0, 0, AF, "precision"},
    {"s32", "signed 32-bit",         0, AV_OPT_TYPE_CONST, {.i64=1},  0, 0, AF, "precision"},
    {"f32", "floating-point single", 0, AV_OPT_TYPE_CONST, {.i64=2},  0, 0, AF, "precision"},
    {"f64", "floating-point double", 0, AV_OPT_TYPE_CONST, {.i64=3},  0, 0, AF, "precision"},
    {NULL}
};

DEFINE_BIQUAD_FILTER(highpass, "Apply a high-pass filter with 3dB point frequency.");
#endif  /* CONFIG_HIGHPASS_FILTER */
#if CONFIG_ALLPASS_FILTER
static const AVOption allpass_options[] = {
    {"frequency", "set central frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=3000}, 0, 999999, FLAGS},
    {"f",         "set central frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=3000}, 0, 999999, FLAGS},
    {"width_type", "set filter-width type", OFFSET(width_type), AV_OPT_TYPE_INT, {.i64=HERTZ}, HERTZ, NB_WTYPE-1, FLAGS, "width_type"},
    {"t",          "set filter-width type", OFFSET(width_type), AV_OPT_TYPE_INT, {.i64=HERTZ}, HERTZ, NB_WTYPE-1, FLAGS, "width_type"},
    {"h", "Hz", 0, AV_OPT_TYPE_CONST, {.i64=HERTZ}, 0, 0, FLAGS, "width_type"},
    {"q", "Q-Factor", 0, AV_OPT_TYPE_CONST, {.i64=QFACTOR}, 0, 0, FLAGS, "width_type"},
    {"o", "octave", 0, AV_OPT_TYPE_CONST, {.i64=OCTAVE}, 0, 0, FLAGS, "width_type"},
    {"s", "slope", 0, AV_OPT_TYPE_CONST, {.i64=SLOPE}, 0, 0, FLAGS, "width_type"},
    {"k", "kHz", 0, AV_OPT_TYPE_CONST, {.i64=KHERTZ}, 0, 0, FLAGS, "width_type"},
    {"width", "set filter-width", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=707.1}, 0, 99999, FLAGS},
    {"w",     "set filter-width", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=707.1}, 0, 99999, FLAGS},
    {"mix", "set mix", OFFSET(mix), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS},
    {"m",   "set mix", OFFSET(mix), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS},
    {"channels", "set channels to filter", OFFSET(ch_layout_str), AV_OPT_TYPE_STRING, {.str="all"}, 0, 0, FLAGS},
    {"c",        "set channels to filter", OFFSET(ch_layout_str), AV_OPT_TYPE_STRING, {.str="all"}, 0, 0, FLAGS},
    {"normalize", "normalize coefficients", OFFSET(normalize), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS},
    {"n",         "normalize coefficients", OFFSET(normalize), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS},
    {"order", "set filter order", OFFSET(order), AV_OPT_TYPE_INT, {.i64=2}, 1, 2, FLAGS},
    {"o",     "set filter order", OFFSET(order), AV_OPT_TYPE_INT, {.i64=2}, 1, 2, FLAGS},
    {"transform", "set transform type", OFFSET(transform_type), AV_OPT_TYPE_INT, {.i64=0}, 0, NB_TTYPE-1, AF, "transform_type"},
    {"a",         "set transform type", OFFSET(transform_type), AV_OPT_TYPE_INT, {.i64=0}, 0, NB_TTYPE-1, AF, "transform_type"},
    {"di",   "direct form I",  0, AV_OPT_TYPE_CONST, {.i64=DI}, 0, 0, AF, "transform_type"},
    {"dii",  "direct form II", 0, AV_OPT_TYPE_CONST, {.i64=DII}, 0, 0, AF, "transform_type"},
    {"tdii", "transposed direct form II", 0, AV_OPT_TYPE_CONST, {.i64=TDII}, 0, 0, AF, "transform_type"},
    {"latt", "lattice-ladder form", 0, AV_OPT_TYPE_CONST, {.i64=LATT}, 0, 0, AF, "transform_type"},
    {"svf",  "state variable filter form", 0, AV_OPT_TYPE_CONST, {.i64=SVF}, 0, 0, AF, "transform_type"},
    {"precision", "set filtering precision", OFFSET(precision), AV_OPT_TYPE_INT, {.i64=-1}, -1, 3, AF, "precision"},
    {"r",         "set filtering precision", OFFSET(precision), AV_OPT_TYPE_INT, {.i64=-1}, -1, 3, AF, "precision"},
    {"auto", "automatic",            0, AV_OPT_TYPE_CONST, {.i64=-1}, 0, 0, AF, "precision"},
    {"s16", "signed 16-bit",         0, AV_OPT_TYPE_CONST, {.i64=0},  0, 0, AF, "precision"},
    {"s32", "signed 32-bit",         0, AV_OPT_TYPE_CONST, {.i64=1},  0, 0, AF, "precision"},
    {"f32", "floating-point single", 0, AV_OPT_TYPE_CONST, {.i64=2},  0, 0, AF, "precision"},
    {"f64", "floating-point double", 0, AV_OPT_TYPE_CONST, {.i64=3},  0, 0, AF, "precision"},
    {NULL}
};

DEFINE_BIQUAD_FILTER(allpass, "Apply a two-pole all-pass filter.");
#endif  /* CONFIG_ALLPASS_FILTER */
#if CONFIG_BIQUAD_FILTER
static const AVOption biquad_options[] = {
    {"a0", NULL, OFFSET(oa0), AV_OPT_TYPE_DOUBLE, {.dbl=1}, INT32_MIN, INT32_MAX, FLAGS},
    {"a1", NULL, OFFSET(oa1), AV_OPT_TYPE_DOUBLE, {.dbl=0}, INT32_MIN, INT32_MAX, FLAGS},
    {"a2", NULL, OFFSET(oa2), AV_OPT_TYPE_DOUBLE, {.dbl=0}, INT32_MIN, INT32_MAX, FLAGS},
    {"b0", NULL, OFFSET(ob0), AV_OPT_TYPE_DOUBLE, {.dbl=0}, INT32_MIN, INT32_MAX, FLAGS},
    {"b1", NULL, OFFSET(ob1), AV_OPT_TYPE_DOUBLE, {.dbl=0}, INT32_MIN, INT32_MAX, FLAGS},
    {"b2", NULL, OFFSET(ob2), AV_OPT_TYPE_DOUBLE, {.dbl=0}, INT32_MIN, INT32_MAX, FLAGS},
    {"mix", "set mix", OFFSET(mix), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS},
    {"m",   "set mix", OFFSET(mix), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS},
    {"channels", "set channels to filter", OFFSET(ch_layout_str), AV_OPT_TYPE_STRING, {.str="all"}, 0, 0, FLAGS},
    {"c",        "set channels to filter", OFFSET(ch_layout_str), AV_OPT_TYPE_STRING, {.str="all"}, 0, 0, FLAGS},
    {"normalize", "normalize coefficients", OFFSET(normalize), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS},
    {"n",         "normalize coefficients", OFFSET(normalize), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS},
    {"transform", "set transform type", OFFSET(transform_type), AV_OPT_TYPE_INT, {.i64=0}, 0, NB_TTYPE-1, AF, "transform_type"},
    {"a",         "set transform type", OFFSET(transform_type), AV_OPT_TYPE_INT, {.i64=0}, 0, NB_TTYPE-1, AF, "transform_type"},
    {"di",   "direct form I",  0, AV_OPT_TYPE_CONST, {.i64=DI}, 0, 0, AF, "transform_type"},
    {"dii",  "direct form II", 0, AV_OPT_TYPE_CONST, {.i64=DII}, 0, 0, AF, "transform_type"},
    {"tdii", "transposed direct form II", 0, AV_OPT_TYPE_CONST, {.i64=TDII}, 0, 0, AF, "transform_type"},
    {"latt", "lattice-ladder form", 0, AV_OPT_TYPE_CONST, {.i64=LATT}, 0, 0, AF, "transform_type"},
    {"svf",  "state variable filter form", 0, AV_OPT_TYPE_CONST, {.i64=SVF}, 0, 0, AF, "transform_type"},
    {"precision", "set filtering precision", OFFSET(precision), AV_OPT_TYPE_INT, {.i64=-1}, -1, 3, AF, "precision"},
    {"r",         "set filtering precision", OFFSET(precision), AV_OPT_TYPE_INT, {.i64=-1}, -1, 3, AF, "precision"},
    {"auto", "automatic",            0, AV_OPT_TYPE_CONST, {.i64=-1}, 0, 0, AF, "precision"},
    {"s16", "signed 16-bit",         0, AV_OPT_TYPE_CONST, {.i64=0},  0, 0, AF, "precision"},
    {"s32", "signed 32-bit",         0, AV_OPT_TYPE_CONST, {.i64=1},  0, 0, AF, "precision"},
    {"f32", "floating-point single", 0, AV_OPT_TYPE_CONST, {.i64=2},  0, 0, AF, "precision"},
    {"f64", "floating-point double", 0, AV_OPT_TYPE_CONST, {.i64=3},  0, 0, AF, "precision"},
    {NULL}
};

DEFINE_BIQUAD_FILTER(biquad, "Apply a biquad IIR filter with the given coefficients.");
#endif  /* CONFIG_BIQUAD_FILTER */
