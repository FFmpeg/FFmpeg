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

#include "libavutil/channel_layout.h"
#include "libavutil/ffmath.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "audio.h"

#define MAX_ORDER 30

typedef struct Coeffs {
    double g;
    double a1;
    double b0, b1;
} Coeffs;

typedef struct ATiltContext {
    const AVClass *class;

    double freq;
    double level;
    double slope;
    double width;
    int order;

    Coeffs coeffs[MAX_ORDER];

    AVFrame *w;

    int (*filter_channels)(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);
} ATiltContext;

static double prewarp(double w, double T, double wp)
{
    return wp * tan(w * T * 0.5) / tan(wp * T * 0.5);
}

static double mz(int i, double w0, double r, double alpha)
{
    return w0 * pow(r, -alpha + i);
}

static double mp(int i, double w0, double r)
{
    return w0 * pow(r, i);
}

static double mzh(int i, double T, double w0, double r, double alpha)
{
    return prewarp(mz(i, w0, r, alpha), T, w0);
}

static double mph(int i, double T, double w0, double r)
{
    return prewarp(mp(i, w0, r), T, w0);
}

static void set_tf1s(Coeffs *coeffs, double b1, double b0, double a0,
                     double w1, double sr, double alpha)
{
    double c = 1.0 / tan(w1 * 0.5 / sr);
    double d = a0 + c;

    coeffs->b1 = (b0 - b1 * c) / d;
    coeffs->b0 = (b0 + b1 * c) / d;
    coeffs->a1 = (a0 - c) / d;
    coeffs->g = a0 / b0;
}

static void set_filter(AVFilterContext *ctx,
                       int order, double sr, double f0,
                       double bw, double alpha)
{
    ATiltContext *s = ctx->priv;
    const double w0 = 2. * M_PI * f0;
    const double f1 = f0 + bw;
    const double w1 = 1.;
    const double r = pow(f1 / f0, 1.0 / (order - 1.0));
    const double T = 1. / sr;

    for (int i = 0; i < order; i++) {
        Coeffs *coeffs = &s->coeffs[i];

        set_tf1s(coeffs, 1.0, mzh(i, T, w0, r, alpha), mph(i, T, w0, r),
                 w1, sr, alpha);
    }
}

static int get_coeffs(AVFilterContext *ctx)
{
    ATiltContext *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];

    set_filter(ctx, s->order, inlink->sample_rate, s->freq, s->width, s->slope);

    return 0;
}

typedef struct ThreadData {
    AVFrame *in, *out;
} ThreadData;

#define FILTER(name, type)                                          \
static int filter_channels_## name(AVFilterContext *ctx, void *arg, \
                                   int jobnr, int nb_jobs)          \
{                                                                   \
    ATiltContext *s = ctx->priv;                                    \
    ThreadData *td = arg;                                           \
    AVFrame *out = td->out;                                         \
    AVFrame *in = td->in;                                           \
    const int start = (in->ch_layout.nb_channels * jobnr) / nb_jobs; \
    const int end = (in->ch_layout.nb_channels * (jobnr+1)) / nb_jobs; \
    const type level = s->level;                                    \
                                                                    \
    for (int ch = start; ch < end; ch++) {                          \
        const type *src = (const type *)in->extended_data[ch];      \
        type *dst = (type *)out->extended_data[ch];                 \
                                                                    \
        for (int b = 0; b < s->order; b++) {                        \
            Coeffs *coeffs = &s->coeffs[b];                         \
            const type g = coeffs->g;                               \
            const type a1 = coeffs->a1;                             \
            const type b0 = coeffs->b0;                             \
            const type b1 = coeffs->b1;                             \
            type *w = ((type *)s->w->extended_data[ch]) + b * 2;    \
                                                                    \
            for (int n = 0; n < in->nb_samples; n++) {              \
                type sain = b ? dst[n] : src[n] * level;            \
                type saout = sain * b0 + w[0] * b1 - w[1] * a1;     \
                                                                    \
                w[0] = sain;                                        \
                w[1] = saout;                                       \
                                                                    \
                dst[n] = saout * g;                                 \
            }                                                       \
        }                                                           \
    }                                                               \
                                                                    \
    return 0;                                                       \
}

FILTER(fltp, float)
FILTER(dblp, double)

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    ATiltContext *s = ctx->priv;

    switch (inlink->format) {
    case AV_SAMPLE_FMT_FLTP: s->filter_channels = filter_channels_fltp; break;
    case AV_SAMPLE_FMT_DBLP: s->filter_channels = filter_channels_dblp; break;
    }

    s->w = ff_get_audio_buffer(inlink, 2 * MAX_ORDER);
    if (!s->w)
        return AVERROR(ENOMEM);

    return get_coeffs(ctx);
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    ATiltContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    ThreadData td;
    AVFrame *out;

    if (av_frame_is_writable(in)) {
        out = in;
    } else {
        out = ff_get_audio_buffer(outlink, in->nb_samples);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }
        av_frame_copy_props(out, in);
    }

    td.in = in; td.out = out;
    ff_filter_execute(ctx, s->filter_channels, &td, NULL, FFMIN(inlink->ch_layout.nb_channels,
                                                               ff_filter_get_nb_threads(ctx)));

    if (out != in)
        av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    int ret;

    ret = ff_filter_process_command(ctx, cmd, args, res, res_len, flags);
    if (ret < 0)
        return ret;

    return get_coeffs(ctx);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    ATiltContext *s = ctx->priv;

    av_frame_free(&s->w);
}

#define OFFSET(x) offsetof(ATiltContext, x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption atilt_options[] = {
    { "freq",   "set central frequency",OFFSET(freq),   AV_OPT_TYPE_DOUBLE, {.dbl=10000},    20, 192000, FLAGS },
    { "slope",  "set filter slope",     OFFSET(slope),  AV_OPT_TYPE_DOUBLE, {.dbl=0},        -1,      1, FLAGS },
    { "width",  "set filter width",     OFFSET(width),  AV_OPT_TYPE_DOUBLE, {.dbl=1000},    100,  10000, FLAGS },
    { "order",  "set filter order",     OFFSET(order),  AV_OPT_TYPE_INT,    {.i64=5},       2,MAX_ORDER, FLAGS },
    { "level",  "set input level",      OFFSET(level),  AV_OPT_TYPE_DOUBLE, {.dbl=1.},        0.,    4., FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(atilt);

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
};

const AVFilter ff_af_atilt = {
    .name            = "atilt",
    .description     = NULL_IF_CONFIG_SMALL("Apply spectral tilt to audio."),
    .priv_size       = sizeof(ATiltContext),
    .priv_class      = &atilt_class,
    .uninit          = uninit,
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(ff_audio_default_filterpad),
    FILTER_SAMPLEFMTS(AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_DBLP),
    .process_command = process_command,
    .flags           = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC |
                       AVFILTER_FLAG_SLICE_THREADS,
};
