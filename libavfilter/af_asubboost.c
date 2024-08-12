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
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "audio.h"
#include "filters.h"

typedef struct ASubBoostContext {
    const AVClass *class;

    double dry_gain;
    double wet_gain;
    double feedback;
    double max_boost;
    double decay;
    double delay;
    double cutoff;
    double slope;

    double a0, a1, a2;
    double b0, b1, b2;

    char *ch_layout_str;
    AVChannelLayout ch_layout;

    int *write_pos;
    int buffer_samples;

    AVFrame *w;
    AVFrame *buffer;
} ASubBoostContext;

static int get_coeffs(AVFilterContext *ctx)
{
    ASubBoostContext *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    double w0 = 2 * M_PI * s->cutoff / inlink->sample_rate;
    double alpha = sin(w0) / 2 * sqrt(2. * (1. / s->slope - 1.) + 2.);

    s->a0 =  1 + alpha;
    s->a1 = -2 * cos(w0);
    s->a2 =  1 - alpha;
    s->b0 = (1 - cos(w0)) / 2;
    s->b1 =  1 - cos(w0);
    s->b2 = (1 - cos(w0)) / 2;

    s->a1 /= s->a0;
    s->a2 /= s->a0;
    s->b0 /= s->a0;
    s->b1 /= s->a0;
    s->b2 /= s->a0;

    s->buffer_samples = inlink->sample_rate * s->delay / 1000;

    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    ASubBoostContext *s = ctx->priv;

    s->buffer = ff_get_audio_buffer(inlink, inlink->sample_rate / 10);
    s->w = ff_get_audio_buffer(inlink, 3);
    s->write_pos = av_calloc(inlink->ch_layout.nb_channels, sizeof(*s->write_pos));
    if (!s->buffer || !s->w || !s->write_pos)
        return AVERROR(ENOMEM);

    return get_coeffs(ctx);
}

typedef struct ThreadData {
    AVFrame *in, *out;
} ThreadData;

static int filter_channels(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ASubBoostContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *out = td->out;
    AVFrame *in = td->in;
    const double mix = ctx->is_disabled ? 0. : 1.;
    const double wet = ctx->is_disabled ? 1. : s->wet_gain;
    const double dry = ctx->is_disabled ? 1. : s->dry_gain;
    const double feedback = s->feedback, decay = s->decay;
    const double max_boost = s->max_boost;
    const double b0 = s->b0;
    const double b1 = s->b1;
    const double b2 = s->b2;
    const double a1 = -s->a1;
    const double a2 = -s->a2;
    const int start = (in->ch_layout.nb_channels * jobnr) / nb_jobs;
    const int end = (in->ch_layout.nb_channels * (jobnr+1)) / nb_jobs;
    const int buffer_samples = s->buffer_samples;

    for (int ch = start; ch < end; ch++) {
        const double *src = (const double *)in->extended_data[ch];
        double *dst = (double *)out->extended_data[ch];
        double *buffer = (double *)s->buffer->extended_data[ch];
        double *w = (double *)s->w->extended_data[ch];
        int write_pos = s->write_pos[ch];
        enum AVChannel channel = av_channel_layout_channel_from_index(&in->ch_layout, ch);
        const int bypass = av_channel_layout_index_from_channel(&s->ch_layout, channel) < 0;
        const double a = 0.00001;
        const double b = 1. - a;

        if (bypass) {
            if (in != out)
                memcpy(out->extended_data[ch], in->extended_data[ch],
                       in->nb_samples * sizeof(double));
            continue;
        }

        for (int n = 0; n < in->nb_samples; n++) {
            double out_sample, boost;

            out_sample = src[n] * b0 + w[0];
            w[0] = b1 * src[n] + w[1] + a1 * out_sample;
            w[1] = b2 * src[n] + a2 * out_sample;

            buffer[write_pos] = buffer[write_pos] * decay + out_sample * feedback;
            boost = av_clipd((1. -  (fabs(src[n] * dry))) / fabs(buffer[write_pos]), 0., max_boost);
            w[2] = boost > w[2] ? w[2] * b + a * boost : w[2] * a + b * boost;
            w[2] = av_clipd(w[2], 0., max_boost);
            dst[n] = (src[n] * dry + w[2] * buffer[write_pos] * mix) * wet;

            if (++write_pos >= buffer_samples)
                write_pos = 0;
        }

        s->write_pos[ch] = write_pos;
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    ASubBoostContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    ThreadData td;
    AVFrame *out;
    int ret;

    ret = av_channel_layout_copy(&s->ch_layout, &inlink->ch_layout);
    if (ret < 0)
        return ret;
    if (strcmp(s->ch_layout_str, "all"))
        av_channel_layout_from_string(&s->ch_layout,
                                      s->ch_layout_str);

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
    ff_filter_execute(ctx, filter_channels, &td, NULL,
                      FFMIN(inlink->ch_layout.nb_channels, ff_filter_get_nb_threads(ctx)));

    if (out != in)
        av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    ASubBoostContext *s = ctx->priv;

    av_channel_layout_uninit(&s->ch_layout);
    av_frame_free(&s->buffer);
    av_frame_free(&s->w);
    av_freep(&s->write_pos);
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

#define OFFSET(x) offsetof(ASubBoostContext, x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption asubboost_options[] = {
    { "dry",      "set dry gain", OFFSET(dry_gain), AV_OPT_TYPE_DOUBLE, {.dbl=1.0},      0,   1, FLAGS },
    { "wet",      "set wet gain", OFFSET(wet_gain), AV_OPT_TYPE_DOUBLE, {.dbl=1.0},      0,   1, FLAGS },
    { "boost",    "set max boost",OFFSET(max_boost),AV_OPT_TYPE_DOUBLE, {.dbl=2.0},      1,  12, FLAGS },
    { "decay",    "set decay",    OFFSET(decay),    AV_OPT_TYPE_DOUBLE, {.dbl=0.0},      0,   1, FLAGS },
    { "feedback", "set feedback", OFFSET(feedback), AV_OPT_TYPE_DOUBLE, {.dbl=0.9},      0,   1, FLAGS },
    { "cutoff",   "set cutoff",   OFFSET(cutoff),   AV_OPT_TYPE_DOUBLE, {.dbl=100},     50, 900, FLAGS },
    { "slope",    "set slope",    OFFSET(slope),    AV_OPT_TYPE_DOUBLE, {.dbl=0.5}, 0.0001,   1, FLAGS },
    { "delay",    "set delay",    OFFSET(delay),    AV_OPT_TYPE_DOUBLE, {.dbl=20},       1, 100, FLAGS },
    { "channels", "set channels to filter", OFFSET(ch_layout_str), AV_OPT_TYPE_STRING, {.str="all"}, 0, 0, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(asubboost);

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
};

const AVFilter ff_af_asubboost = {
    .name           = "asubboost",
    .description    = NULL_IF_CONFIG_SMALL("Boost subwoofer frequencies."),
    .priv_size      = sizeof(ASubBoostContext),
    .priv_class     = &asubboost_class,
    .uninit         = uninit,
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(ff_audio_default_filterpad),
    FILTER_SINGLE_SAMPLEFMT(AV_SAMPLE_FMT_DBLP),
    .process_command = process_command,
    .flags           = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL |
                       AVFILTER_FLAG_SLICE_THREADS,
};
