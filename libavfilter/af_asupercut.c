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
#include "formats.h"

typedef struct BiquadCoeffs {
    double a1, a2;
    double b0, b1, b2;
} BiquadCoeffs;

typedef struct ASuperCutContext {
    const AVClass *class;

    double cutoff;
    int order;

    int filter_count;
    int bypass;

    BiquadCoeffs coeffs[10];

    AVFrame *w;

    int (*filter_channels)(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);
} ASuperCutContext;

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats = NULL;
    AVFilterChannelLayouts *layouts = NULL;
    static const enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_FLTP,
        AV_SAMPLE_FMT_DBLP,
        AV_SAMPLE_FMT_NONE
    };
    int ret;

    formats = ff_make_format_list(sample_fmts);
    if (!formats)
        return AVERROR(ENOMEM);
    ret = ff_set_common_formats(ctx, formats);
    if (ret < 0)
        return ret;

    layouts = ff_all_channel_counts();
    if (!layouts)
        return AVERROR(ENOMEM);

    ret = ff_set_common_channel_layouts(ctx, layouts);
    if (ret < 0)
        return ret;

    formats = ff_all_samplerates();
    return ff_set_common_samplerates(ctx, formats);
}

static void calc_q_factors(int n, double *q)
{
    for (int i = 0; i < n / 2; i++)
        q[i] = 1. / (-2. * cos(M_PI * (2. * (i + 1) + n - 1.) / (2. * n)));
}

static int get_coeffs(AVFilterContext *ctx)
{
    ASuperCutContext *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    double w0 = s->cutoff / inlink->sample_rate;
    double K = tan(M_PI * w0);
    double q[10];

    s->bypass = w0 >= 0.5;
    if (s->bypass)
        return 0;

    s->filter_count = s->order / 2 + (s->order & 1);
    calc_q_factors(s->order, q);

    if (s->order & 1) {
        BiquadCoeffs *coeffs = &s->coeffs[0];
        double omega = 2. * tan(M_PI * w0);

        coeffs->b0 = omega / (2. + omega);
        coeffs->b1 = coeffs->b0;
        coeffs->b2 = 0.;
        coeffs->a1 = -(omega - 2.) / (2. + omega);
        coeffs->a2 = 0.;
    }

    for (int b = (s->order & 1); b < s->filter_count; b++) {
        BiquadCoeffs *coeffs = &s->coeffs[b];
        const int idx = b - (s->order & 1);
        double norm = 1.0 / (1.0 + K / q[idx] + K * K);

        coeffs->b0 = K * K * norm;
        coeffs->b1 = 2.0 * coeffs->b0;
        coeffs->b2 = coeffs->b0;
        coeffs->a1 = -2.0 * (K * K - 1.0) * norm;
        coeffs->a2 = -(1.0 - K / q[idx] + K * K) * norm;
    }

    return 0;
}

typedef struct ThreadData {
    AVFrame *in, *out;
} ThreadData;

#define FILTER(name, type)                                          \
static int filter_channels_## name(AVFilterContext *ctx, void *arg, \
                                   int jobnr, int nb_jobs)          \
{                                                                   \
    ASuperCutContext *s = ctx->priv;                                \
    ThreadData *td = arg;                                           \
    AVFrame *out = td->out;                                         \
    AVFrame *in = td->in;                                           \
    const int start = (in->channels * jobnr) / nb_jobs;             \
    const int end = (in->channels * (jobnr+1)) / nb_jobs;           \
                                                                    \
    for (int ch = start; ch < end; ch++) {                          \
        const type *src = (const type *)in->extended_data[ch];      \
        type *dst = (type *)out->extended_data[ch];                 \
                                                                    \
        for (int b = 0; b < s->filter_count; b++) {                 \
            BiquadCoeffs *coeffs = &s->coeffs[b];                   \
            const type a1 = coeffs->a1;                             \
            const type a2 = coeffs->a2;                             \
            const type b0 = coeffs->b0;                             \
            const type b1 = coeffs->b1;                             \
            const type b2 = coeffs->b2;                             \
            type *w = ((type *)s->w->extended_data[ch]) + b * 2;    \
                                                                    \
            for (int n = 0; n < in->nb_samples; n++) {              \
                type sin = b ? dst[n] : src[n];                     \
                type sout = sin * b0 + w[0];                        \
                                                                    \
                w[0] = b1 * sin + w[1] + a1 * sout;                 \
                w[1] = b2 * sin + a2 * sout;                        \
                                                                    \
                dst[n] = sout;                                      \
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
    ASuperCutContext *s = ctx->priv;

    switch (inlink->format) {
    case AV_SAMPLE_FMT_FLTP: s->filter_channels = filter_channels_fltp; break;
    case AV_SAMPLE_FMT_DBLP: s->filter_channels = filter_channels_dblp; break;
    }

    s->w = ff_get_audio_buffer(inlink, 2 * 10);
    if (!s->w)
        return AVERROR(ENOMEM);

    return get_coeffs(ctx);
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    ASuperCutContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    ThreadData td;
    AVFrame *out;

    if (s->bypass)
        return ff_filter_frame(outlink, in);

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
    ctx->internal->execute(ctx, s->filter_channels, &td, NULL, FFMIN(inlink->channels,
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
    ASuperCutContext *s = ctx->priv;

    av_frame_free(&s->w);
}

#define OFFSET(x) offsetof(ASuperCutContext, x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption asupercut_options[] = {
    { "cutoff", "set cutoff frequency", OFFSET(cutoff), AV_OPT_TYPE_DOUBLE, {.dbl=20000}, 20000, 192000, FLAGS },
    { "order",  "set filter order",     OFFSET(order),  AV_OPT_TYPE_INT,    {.i64=10},        3,     20, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(asupercut);

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
    { NULL }
};

static const AVFilterPad outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_AUDIO,
    },
    { NULL }
};

AVFilter ff_af_asupercut = {
    .name            = "asupercut",
    .description     = NULL_IF_CONFIG_SMALL("Cut super frequencies."),
    .query_formats   = query_formats,
    .priv_size       = sizeof(ASuperCutContext),
    .priv_class      = &asupercut_class,
    .uninit          = uninit,
    .inputs          = inputs,
    .outputs         = outputs,
    .process_command = process_command,
    .flags           = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC |
                       AVFILTER_FLAG_SLICE_THREADS,
};
