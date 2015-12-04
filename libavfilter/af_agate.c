/*
 * Copyright (C) 2001-2010 Krzysztof Foltman, Markus Schmidt, Thor Harald Johansen, Damien Zammit
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
 * Audio (Sidechain) Gate filter
 */

#include "libavutil/avassert.h"
#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "audio.h"
#include "formats.h"
#include "hermite.h"

typedef struct AudioGateContext {
    const AVClass *class;

    double level_in;
    double level_sc;
    double attack;
    double release;
    double threshold;
    double ratio;
    double knee;
    double makeup;
    double range;
    int link;
    int detection;

    double thres;
    double knee_start;
    double lin_knee_stop;
    double knee_stop;
    double lin_slope;
    double attack_coeff;
    double release_coeff;

    AVFrame *input_frame[2];
} AudioGateContext;

#define OFFSET(x) offsetof(AudioGateContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption options[] = {
    { "level_in",  "set input level",        OFFSET(level_in),  AV_OPT_TYPE_DOUBLE, {.dbl=1},           0.015625,   64, A },
    { "range",     "set max gain reduction", OFFSET(range),     AV_OPT_TYPE_DOUBLE, {.dbl=0.06125},     0, 1, A },
    { "threshold", "set threshold",          OFFSET(threshold), AV_OPT_TYPE_DOUBLE, {.dbl=0.125},       0, 1, A },
    { "ratio",     "set ratio",              OFFSET(ratio),     AV_OPT_TYPE_DOUBLE, {.dbl=2},           1,  9000, A },
    { "attack",    "set attack",             OFFSET(attack),    AV_OPT_TYPE_DOUBLE, {.dbl=20},          0.01, 9000, A },
    { "release",   "set release",            OFFSET(release),   AV_OPT_TYPE_DOUBLE, {.dbl=250},         0.01, 9000, A },
    { "makeup",    "set makeup gain",        OFFSET(makeup),    AV_OPT_TYPE_DOUBLE, {.dbl=1},           1,   64, A },
    { "knee",      "set knee",               OFFSET(knee),      AV_OPT_TYPE_DOUBLE, {.dbl=2.828427125}, 1,    8, A },
    { "detection", "set detection",          OFFSET(detection), AV_OPT_TYPE_INT,    {.i64=1},           0,    1, A, "detection" },
    {   "peak",    0,                        0,                 AV_OPT_TYPE_CONST,  {.i64=0},           0,    0, A, "detection" },
    {   "rms",     0,                        0,                 AV_OPT_TYPE_CONST,  {.i64=1},           0,    0, A, "detection" },
    { "link",      "set link",               OFFSET(link),      AV_OPT_TYPE_INT,    {.i64=0},           0,    1, A, "link" },
    {   "average", 0,                        0,                 AV_OPT_TYPE_CONST,  {.i64=0},           0,    0, A, "link" },
    {   "maximum", 0,                        0,                 AV_OPT_TYPE_CONST,  {.i64=1},           0,    0, A, "link" },
    { "level_sc",  "set sidechain gain",     OFFSET(level_sc),  AV_OPT_TYPE_DOUBLE, {.dbl=1},           0.015625,   64, A },
    { NULL }
};

static int agate_config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    AudioGateContext *s = ctx->priv;
    double lin_threshold = s->threshold;
    double lin_knee_sqrt = sqrt(s->knee);
    double lin_knee_start;

    if (s->detection)
        lin_threshold *= lin_threshold;

    s->attack_coeff  = FFMIN(1., 1. / (s->attack * inlink->sample_rate / 4000.));
    s->release_coeff = FFMIN(1., 1. / (s->release * inlink->sample_rate / 4000.));
    s->lin_knee_stop = lin_threshold * lin_knee_sqrt;
    lin_knee_start = lin_threshold / lin_knee_sqrt;
    s->thres = log(lin_threshold);
    s->knee_start = log(lin_knee_start);
    s->knee_stop = log(s->lin_knee_stop);

    return 0;
}

// A fake infinity value (because real infinity may break some hosts)
#define FAKE_INFINITY (65536.0 * 65536.0)

// Check for infinity (with appropriate-ish tolerance)
#define IS_FAKE_INFINITY(value) (fabs(value-FAKE_INFINITY) < 1.0)

static double output_gain(double lin_slope, double ratio, double thres,
                          double knee, double knee_start, double knee_stop,
                          double lin_knee_stop, double range)
{
    if (lin_slope < lin_knee_stop) {
        double slope = log(lin_slope);
        double tratio = ratio;
        double gain = 0.;
        double delta = 0.;

        if (IS_FAKE_INFINITY(ratio))
            tratio = 1000.;
        gain = (slope - thres) * tratio + thres;
        delta = tratio;

        if (knee > 1. && slope > knee_start) {
            gain = hermite_interpolation(slope, knee_start, knee_stop, ((knee_start - thres) * tratio  + thres), knee_stop, delta, 1.);
        }
        return FFMAX(range, exp(gain - slope));
    }

    return 1.;
}

static void gate(AudioGateContext *s,
                 const double *src, double *dst, const double *scsrc,
                 int nb_samples, double level_in, double level_sc,
                 AVFilterLink *inlink, AVFilterLink *sclink)
{
    const double makeup = s->makeup;
    const double attack_coeff = s->attack_coeff;
    const double release_coeff = s->release_coeff;
    int n, c;

    for (n = 0; n < nb_samples; n++, src += inlink->channels, dst += inlink->channels, scsrc += sclink->channels) {
        double abs_sample = fabs(scsrc[0] * level_sc), gain = 1.0;

        if (s->link == 1) {
            for (c = 1; c < sclink->channels; c++)
                abs_sample = FFMAX(fabs(scsrc[c] * level_sc), abs_sample);
        } else {
            for (c = 1; c < sclink->channels; c++)
                abs_sample += fabs(scsrc[c] * level_sc);

            abs_sample /= sclink->channels;
        }

        if (s->detection)
            abs_sample *= abs_sample;

        s->lin_slope += (abs_sample - s->lin_slope) * (abs_sample > s->lin_slope ? attack_coeff : release_coeff);
        if (s->lin_slope > 0.0)
            gain = output_gain(s->lin_slope, s->ratio, s->thres,
                               s->knee, s->knee_start, s->knee_stop,
                               s->lin_knee_stop, s->range);

        for (c = 0; c < inlink->channels; c++)
            dst[c] = src[c] * level_in * gain * makeup;
    }
}

#if CONFIG_AGATE_FILTER

#define agate_options options
AVFILTER_DEFINE_CLASS(agate);

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats = NULL;
    AVFilterChannelLayouts *layouts;
    int ret;

    if ((ret = ff_add_format(&formats, AV_SAMPLE_FMT_DBL)) < 0)
        return ret;
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
    if (!formats)
        return AVERROR(ENOMEM);

    return ff_set_common_samplerates(ctx, formats);
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    const double *src = (const double *)in->data[0];
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    AudioGateContext *s = ctx->priv;
    AVFrame *out;
    double *dst;

    if (av_frame_is_writable(in)) {
        out = in;
    } else {
        out = ff_get_audio_buffer(inlink, in->nb_samples);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }
        av_frame_copy_props(out, in);
    }
    dst = (double *)out->data[0];

    gate(s, src, dst, src, in->nb_samples,
         s->level_in, s->level_in, inlink, inlink);

    if (out != in)
        av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
        .config_props = agate_config_input,
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

AVFilter ff_af_agate = {
    .name           = "agate",
    .description    = NULL_IF_CONFIG_SMALL("Audio gate."),
    .query_formats  = query_formats,
    .priv_size      = sizeof(AudioGateContext),
    .priv_class     = &agate_class,
    .inputs         = inputs,
    .outputs        = outputs,
};

#endif /* CONFIG_AGATE_FILTER */

#if CONFIG_SIDECHAINGATE_FILTER

#define sidechaingate_options options
AVFILTER_DEFINE_CLASS(sidechaingate);

static int scfilter_frame(AVFilterLink *link, AVFrame *in)
{
    AVFilterContext *ctx = link->dst;
    AudioGateContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    const double *scsrc;
    double *sample;
    int nb_samples;
    int ret, i;

    for (i = 0; i < 2; i++)
        if (link == ctx->inputs[i])
            break;
    av_assert0(i < 2 && !s->input_frame[i]);
    s->input_frame[i] = in;

    if (!s->input_frame[0] || !s->input_frame[1])
        return 0;

    nb_samples = FFMIN(s->input_frame[0]->nb_samples,
                       s->input_frame[1]->nb_samples);

    sample = (double *)s->input_frame[0]->data[0];
    scsrc = (const double *)s->input_frame[1]->data[0];

    gate(s, sample, sample, scsrc, nb_samples,
         s->level_in, s->level_sc,
         ctx->inputs[0], ctx->inputs[1]);
    ret = ff_filter_frame(outlink, s->input_frame[0]);

    s->input_frame[0] = NULL;
    av_frame_free(&s->input_frame[1]);

    return ret;
}

static int screquest_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AudioGateContext *s = ctx->priv;
    int i, ret;

    /* get a frame on each input */
    for (i = 0; i < 2; i++) {
        AVFilterLink *inlink = ctx->inputs[i];
        if (!s->input_frame[i] &&
            (ret = ff_request_frame(inlink)) < 0)
            return ret;

        /* request the same number of samples on all inputs */
        if (i == 0)
            ctx->inputs[1]->request_samples = s->input_frame[0]->nb_samples;
    }

    return 0;
}

static int scquery_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats;
    AVFilterChannelLayouts *layouts = NULL;
    static const enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_DBL,
        AV_SAMPLE_FMT_NONE
    };
    int ret, i;

    if (!ctx->inputs[0]->in_channel_layouts ||
        !ctx->inputs[0]->in_channel_layouts->nb_channel_layouts) {
        av_log(ctx, AV_LOG_WARNING,
               "No channel layout for input 1\n");
            return AVERROR(EAGAIN);
    }

    if ((ret = ff_add_channel_layout(&layouts, ctx->inputs[0]->in_channel_layouts->channel_layouts[0])) < 0 ||
        (ret = ff_channel_layouts_ref(layouts, &ctx->outputs[0]->in_channel_layouts)) < 0)
        return ret;

    for (i = 0; i < 2; i++) {
        layouts = ff_all_channel_counts();
        if ((ret = ff_channel_layouts_ref(layouts, &ctx->inputs[i]->out_channel_layouts)) < 0)
            return ret;
    }

    formats = ff_make_format_list(sample_fmts);
    if ((ret = ff_set_common_formats(ctx, formats)) < 0)
        return ret;

    formats = ff_all_samplerates();
    return ff_set_common_samplerates(ctx, formats);
}

static int scconfig_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;

    if (ctx->inputs[0]->sample_rate != ctx->inputs[1]->sample_rate) {
        av_log(ctx, AV_LOG_ERROR,
               "Inputs must have the same sample rate "
               "%d for in0 vs %d for in1\n",
               ctx->inputs[0]->sample_rate, ctx->inputs[1]->sample_rate);
        return AVERROR(EINVAL);
    }

    outlink->sample_rate = ctx->inputs[0]->sample_rate;
    outlink->time_base   = ctx->inputs[0]->time_base;
    outlink->channel_layout = ctx->inputs[0]->channel_layout;
    outlink->channels = ctx->inputs[0]->channels;

    agate_config_input(ctx->inputs[0]);

    return 0;
}

static const AVFilterPad sidechaingate_inputs[] = {
    {
        .name           = "main",
        .type           = AVMEDIA_TYPE_AUDIO,
        .filter_frame   = scfilter_frame,
        .needs_writable = 1,
        .needs_fifo     = 1,
    },{
        .name           = "sidechain",
        .type           = AVMEDIA_TYPE_AUDIO,
        .filter_frame   = scfilter_frame,
        .needs_fifo     = 1,
    },
    { NULL }
};

static const AVFilterPad sidechaingate_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .config_props  = scconfig_output,
        .request_frame = screquest_frame,
    },
    { NULL }
};

AVFilter ff_af_sidechaingate = {
    .name           = "sidechaingate",
    .description    = NULL_IF_CONFIG_SMALL("Audio sidechain gate."),
    .priv_size      = sizeof(AudioGateContext),
    .priv_class     = &sidechaingate_class,
    .query_formats  = scquery_formats,
    .inputs         = sidechaingate_inputs,
    .outputs        = sidechaingate_outputs,
};
#endif  /* CONFIG_SIDECHAINGATE_FILTER */
