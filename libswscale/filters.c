/*
 * Copyright (C) 2026 Niklas Haas
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
#include <stdbool.h>

#include <libavutil/attributes.h>
#include <libavutil/avassert.h>
#include <libavutil/mem.h>

#include "filters.h"

#ifdef _WIN32
#  define j1 _j1
#endif

/* Maximum (pre-stretching) radius (for tunable filters) */
#define RADIUS_MAX 10.0

/* Defined only on [0, radius]. */
typedef double (*SwsFilterKernel)(double x, const double *params);

typedef struct SwsFilterFunction {
    char name[16];
    double radius; /* negative means resizable */
    SwsFilterKernel kernel;
    SwsFilterKernel window; /* optional */
    double params[SWS_NUM_SCALER_PARAMS]; /* default params */
} SwsFilterFunction;

static const SwsFilterFunction filter_functions[SWS_SCALE_NB];

static double scaler_sample(const SwsFilterFunction *f, double x)
{
    x = fabs(x);
    if (x > f->radius)
        return 0.0;

    double w = f->kernel(x, f->params);
    if (f->window)
        w *= f->window(x / f->radius, f->params);
    return w;
}

static void compute_row(SwsFilterWeights *f, const SwsFilterFunction *fun,
                        double radius, double ratio_inv, double stretch_inv,
                        int dst_pos, double *tmp)
{
    int *out = &f->weights[dst_pos * f->filter_size];
    int *pos = &f->offsets[dst_pos];

    /**
     * Explanation of the 0.5 offsets: Normally, pixel samples are assumed
     * to be representative of the center of their containing area; e.g. for
     * a 2x2 image, the samples are located at {0.5, 1.5}^2. However, with
     * integer indexing, we round sample positions down (0-based indexing).
     * So the (0, 0) sample is actually located at (0.5, 0.5) and represents
     * the entire square from (0,0) to (1,1). When normalizing between different
     * image sizes, we therefore need to add/subtract off these 0.5 offsets.
     */
    const double src_pos = (dst_pos + 0.5) * ratio_inv - 0.5;
    if (f->filter_size == 1) {
        *pos = fmin(fmax(round(src_pos), 0.0), f->src_size - 1);
        *out = SWS_FILTER_SCALE;
        return;
    }

    /* First pixel that is actually within the filter envelope */
    const double start_pos = src_pos - radius;
    int64_t start_idx = ceil(start_pos);
    start_idx = FFMAX(start_idx, 0); /* edge clamping */
    start_idx = FFMIN(start_idx, f->src_size - f->filter_size);
    const double offset = start_idx - src_pos;
    *pos = start_idx;

    /**
     * Generate raw filter weights with maximum precision. Sum the positive
     * and negative weights separately to avoid catastrophic cancellation. This
     * summation order should already give the best precision because abs(w)
     * is monotonically decreasing
     */
    const double base = stretch_inv * offset;
    double wsum_pos = 0.0, wsum_neg = 0.0;
    for (int i = 0; i < f->filter_size; i++) {
        tmp[i] = scaler_sample(fun, base + stretch_inv * i);
        if (tmp[i] >= 0)
            wsum_pos += tmp[i];
        else
            wsum_neg += tmp[i];
    }

    const double wsum = wsum_pos + wsum_neg;
    av_assert0(wsum > 0);

    /* Generate correctly rounded filter weights with error diffusion */
    double error = 0.0;
    int sum_pos = 0, sum_neg = 0;
    for (int i = 0; i < f->filter_size; i++) {
        if (i == f->filter_size - 1) {
            /* Ensure weights sum to exactly SWS_FILTER_SCALE */
            out[i] = SWS_FILTER_SCALE - sum_pos - sum_neg;
        } else {
            const double w = tmp[i] / wsum + error;
            out[i] = round(w * SWS_FILTER_SCALE);
            error = w - (double) out[i] / SWS_FILTER_SCALE;
        }
        if (out[i] >= 0)
            sum_pos += out[i];
        else
            sum_neg += out[i];
    }

    if (sum_pos > f->sum_positive)
        f->sum_positive = sum_pos;
    if (sum_neg < f->sum_negative)
        f->sum_negative = sum_neg;
}

static void sws_filter_free(AVRefStructOpaque opaque, void *obj)
{
    SwsFilterWeights *filter = obj;
    av_refstruct_unref(&filter->weights);
    av_refstruct_unref(&filter->offsets);
}

static bool validate_params(const SwsFilterFunction *fun, SwsScaler scaler)
{
    switch (scaler) {
    case SWS_SCALE_GAUSSIAN:
        return fun->params[0] >= 0.0; /* sigma */
    case SWS_SCALE_LANCZOS:
        return fun->params[0] >= 1.0 && fun->params[0] <= RADIUS_MAX; /* radius */
    case SWS_SCALE_BICUBIC:
        return fun->params[0] < 3.0; /* B param (division by zero) */
    default:
        return true;
    }
}

static double filter_radius(const SwsFilterFunction *fun)
{
    const double bound = fun->radius;
    const double step  = 1e-2;

    double radius = bound;
    double prev = 0.0, fprev = 1.0; /* f(0) is always 1.0 */
    double integral = 0.0;
    for (double x = step; x < bound + step; x += step) {
        const double fx = scaler_sample(fun, x);
        integral += (fprev + fx) * step; /* trapezoidal rule (mirrored) */
        double cutoff = SWS_MAX_REDUCE_CUTOFF * integral;
        if ((fprev > cutoff && fx <= cutoff) || (fprev < -cutoff && fx >= -cutoff)) {
            /* estimate crossing with secant method; note that we have to
             * bias by the cutoff to find the actual cutoff radius */
            double estimate = fx + (fx > fprev ? cutoff : -cutoff);
            double root = x - estimate * (x - prev) / (fx - fprev);
            radius = fmin(root, bound);
        }
        prev  = x;
        fprev = fx;
    }

    return radius;
}

int ff_sws_filter_generate(void *log, const SwsFilterParams *params,
                           SwsFilterWeights **out)
{
    SwsScaler scaler = params->scaler;
    if (scaler >= SWS_SCALE_NB)
        return AVERROR(EINVAL);

    if (scaler == SWS_SCALE_AUTO)
        scaler = SWS_SCALE_BICUBIC;

    const double ratio = (double) params->dst_size / params->src_size;
    double stretch = 1.0;
    if (ratio < 1.0 && scaler != SWS_SCALE_POINT) {
        /* Widen filter for downscaling (anti-aliasing) */
        stretch = 1.0 / ratio;
    }

    if (scaler == SWS_SCALE_AREA) {
        /**
         * SWS_SCALE_AREA is a pseudo-filter that is equivalent to bilinear
         * filtering for upscaling (since bilinear just evenly mixes samples
         * according to the relative distance), and equivalent to (anti-aliased)
         * point sampling for downscaling.
         */
        scaler = ratio >= 1.0 ? SWS_SCALE_BILINEAR : SWS_SCALE_POINT;
    }

    SwsFilterFunction fun = filter_functions[scaler];
    if (!fun.kernel)
        return AVERROR(EINVAL);

    for (int i = 0; i < SWS_NUM_SCALER_PARAMS; i++) {
        if (params->scaler_params[i] != SWS_PARAM_DEFAULT)
            fun.params[i] = params->scaler_params[i];
    }

    if (!validate_params(&fun, scaler)) {
        av_log(log, AV_LOG_ERROR, "Invalid parameters for scaler %s: {%f, %f}\n",
               fun.name, fun.params[0], fun.params[1]);
        return AVERROR(EINVAL);
    }

    if (fun.radius < 0.0) /* tunable width kernels like lanczos */
        fun.radius = fun.params[0];

    const double radius = filter_radius(&fun) * stretch;
    int filter_size = ceil(radius * 2.0);
    filter_size = FFMIN(filter_size, params->src_size);
    av_assert0(filter_size >= 1);
    if (filter_size > SWS_FILTER_SIZE_MAX)
        return AVERROR(ENOTSUP);

    SwsFilterWeights *filter;
    filter = av_refstruct_alloc_ext(sizeof(*filter), 0, NULL, sws_filter_free);
    if (!filter)
        return AVERROR(ENOMEM);
    memcpy(filter->name, fun.name, sizeof(filter->name));
    filter->src_size = params->src_size;
    filter->dst_size = params->dst_size;
    filter->filter_size = filter_size;
    if (filter->filter_size == 1)
        filter->sum_positive = SWS_FILTER_SCALE;

    av_log(log, AV_LOG_DEBUG, "Generating %s filter with %d taps (radius = %f)\n",
           filter->name, filter->filter_size, radius);

    filter->num_weights = (size_t) params->dst_size * filter->filter_size;
    filter->weights = av_refstruct_allocz(filter->num_weights * sizeof(*filter->weights));
    if (!filter->weights) {
        av_refstruct_unref(&filter);
        return AVERROR(ENOMEM);
    }

    filter->offsets = av_refstruct_allocz(params->dst_size * sizeof(*filter->offsets));
    if (!filter->offsets) {
        av_refstruct_unref(&filter);
        return AVERROR(ENOMEM);
    }

    double *tmp = av_malloc(filter->filter_size * sizeof(*tmp));
    if (!tmp) {
        av_refstruct_unref(&filter);
        return AVERROR(ENOMEM);
    }

    const double ratio_inv = 1.0 / ratio, stretch_inv = 1.0 / stretch;
    for (int i = 0; i < params->dst_size; i++)
        compute_row(filter, &fun, radius, ratio_inv, stretch_inv, i, tmp);
    av_free(tmp);

    *out = filter;
    return 0;
}

/*
 * Some of the filter code originally derives (via libplacebo/mpv) from Glumpy:
 * # Copyright (c) 2009-2016 Nicolas P. Rougier. All rights reserved.
 * # Distributed under the (new) BSD License.
 * (https://github.com/glumpy/glumpy/blob/master/glumpy/library/build-spatial-filters.py)
 *
 * The math underlying each filter function was written from scratch, with
 * some algorithms coming from a number of different sources, including:
 * - https://en.wikipedia.org/wiki/Window_function
 * - https://en.wikipedia.org/wiki/Jinc
 * - http://vector-agg.cvs.sourceforge.net/viewvc/vector-agg/agg-2.5/include/agg_image_filters.h
 * - Vapoursynth plugin fmtconv (WTFPL Licensed), which is based on
 *   dither plugin for avisynth from the same author:
 *   https://github.com/vapoursynth/fmtconv/tree/master/src/fmtc
 * - Paul Heckbert's "zoom"
 * - XBMC: ConvolutionKernels.cpp etc.
 * - https://github.com/AviSynth/jinc-resize (only used to verify the math)
 */

av_unused static double box(double x, const double *params)
{
    return 1.0;
}

av_unused static double triangle(double x, const double *params)
{
    return 1.0 - x;
}

av_unused static double cosine(double x, const double *params)
{
    return cos(x);
}

av_unused static double hann(double x, const double *params)
{
    return 0.5 + 0.5 * cos(M_PI * x);
}

av_unused static double hamming(double x, const double *params)
{
    return 0.54 + 0.46 * cos(M_PI * x);
}

av_unused static double welch(double x, const double *params)
{
    return 1.0 - x * x;
}

av_unused static double bessel_i0(double x)
{
    double s = 1.0;
    double y = x * x / 4.0;
    double t = y;
    int i = 2;
    while (t > 1e-12) {
        s += t;
        t *= y / (i * i);
        i += 1;
    }
    return s;
}

av_unused static double kaiser(double x, const double *params)
{
    double alpha = fmax(params[0], 0.0);
    double scale = bessel_i0(alpha);
    return bessel_i0(alpha * sqrt(1.0 - x * x)) / scale;
}

av_unused static double blackman(double x, const double *params)
{
    double a = params[0];
    double a0 = (1 - a) / 2.0, a1 = 1 / 2.0, a2 = a / 2.0;
    x *= M_PI;
    return a0 + a1 * cos(x) + a2 * cos(2 * x);
}

av_unused static double bohman(double x, const double *params)
{
    double pix = M_PI * x;
    return (1.0 - x) * cos(pix) + sin(pix) / M_PI;
}

av_unused static double gaussian(double x, const double *params)
{
    return exp(-params[0] * x * x);
}

av_unused static double quadratic(double x, const double *params)
{
    if (x < 0.5) {
        return 1.0 - 4.0/3.0 * (x * x);
    } else {
        return 2.0 / 3.0 * (x - 1.5) * (x - 1.5);
    }
}

av_unused static double sinc(double x, const double *params)
{
    if (x < 1e-8)
        return 1.0;
    x *= M_PI;
    return sin(x) / x;
}

av_unused static double jinc(double x, const double *params)
{
    if (x < 1e-8)
        return 1.0;
    x *= M_PI;
    return 2.0 * j1(x) / x;
}

av_unused static double sphinx(double x, const double *params)
{
    if (x < 1e-8)
        return 1.0;
    x *= M_PI;
    return 3.0 * (sin(x) - x * cos(x)) / (x * x * x);
}

av_unused static double cubic(double x, const double *params)
{
    const double b = params[0], c = params[1];
    double p0 = 6.0 - 2.0 * b,
           p2 = -18.0 + 12.0 * b + 6.0 * c,
           p3 = 12.0 - 9.0 * b - 6.0 * c,
           q0 = 8.0 * b + 24.0 * c,
           q1 = -12.0 * b - 48.0 * c,
           q2 = 6.0 * b + 30.0 * c,
           q3 = -b - 6.0 * c;

    if (x < 1.0) {
        return (p0 + x * x * (p2 + x * p3)) / p0;
    } else {
        return (q0 + x * (q1 + x * (q2 + x * q3))) / p0;
    }
}

static double spline_coeff(double a, double b, double c, double d, double x)
{
    if (x <= 1.0) {
        return ((d * x + c) * x + b) * x + a;
    } else {
        return spline_coeff(0.0,
                            b + 2.0 * c + 3.0 * d,
                            c + 3.0 * d,
                           -b - 3.0 * c - 6.0 * d,
                            x - 1.0);
    }
}

av_unused static double spline(double x, const double *params)
{
    const double p = -2.196152422706632;
    return spline_coeff(1.0, 0.0, p, -p - 1.0, x);
}

static const SwsFilterFunction filter_functions[SWS_SCALE_NB] = {
    [SWS_SCALE_BILINEAR]    = { "bilinear",     1.0, triangle },
    [SWS_SCALE_BICUBIC]     = { "bicubic",      2.0, cubic, .params = { 0.0, 0.6 } },
    [SWS_SCALE_POINT]       = { "point",        0.5, box },
    [SWS_SCALE_GAUSSIAN]    = { "gaussian",     4.0, gaussian, .params = { 3.0 } },
    [SWS_SCALE_SINC]        = { "sinc",         RADIUS_MAX, sinc },
    [SWS_SCALE_LANCZOS]     = { "lanczos",      -1.0, sinc, sinc, .params = { 3.0 } },
    [SWS_SCALE_SPLINE]      = { "spline",       RADIUS_MAX, spline },
    /* SWS_SCALE_AREA is a pseudo-filter, see code above */
};
