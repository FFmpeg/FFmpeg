/*
 * Copyright (C) 2001-2010 Krzysztof Foltman, Markus Schmidt, Thor Harald Johansen and others
 * Copyright (c) 2015 Paul B Mahol
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
 * Sidechain compressor filter
 */

#include "libavutil/avassert.h"
#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/opt.h"

#include "audio.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"

typedef struct SidechainCompressContext {
    const AVClass *class;

    double attack, attack_coeff;
    double release, release_coeff;
    double lin_slope;
    double ratio;
    double threshold;
    double makeup;
    double thres;
    double knee;
    double knee_start;
    double knee_stop;
    double lin_knee_start;
    double compressed_knee_stop;
    int link;
    int detection;

    AVFrame *input_frame[2];
} SidechainCompressContext;

#define OFFSET(x) offsetof(SidechainCompressContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM
#define F AV_OPT_FLAG_FILTERING_PARAM

static const AVOption sidechaincompress_options[] = {
    { "threshold", "set threshold",    OFFSET(threshold), AV_OPT_TYPE_DOUBLE, {.dbl=0.125}, 0.000976563,    1, A|F },
    { "ratio",     "set ratio",        OFFSET(ratio),     AV_OPT_TYPE_DOUBLE, {.dbl=2},               1,   20, A|F },
    { "attack",    "set attack",       OFFSET(attack),    AV_OPT_TYPE_DOUBLE, {.dbl=20},           0.01, 2000, A|F },
    { "release",   "set release",      OFFSET(release),   AV_OPT_TYPE_DOUBLE, {.dbl=250},          0.01, 9000, A|F },
    { "makeup",    "set make up gain", OFFSET(makeup),    AV_OPT_TYPE_DOUBLE, {.dbl=2},               1,   64, A|F },
    { "knee",      "set knee",         OFFSET(knee),      AV_OPT_TYPE_DOUBLE, {.dbl=2.82843},         1,    8, A|F },
    { "link",      "set link type",    OFFSET(link),      AV_OPT_TYPE_INT,    {.i64=0},               0,    1, A|F, "link" },
    {   "average", 0,                  0,                 AV_OPT_TYPE_CONST,  {.i64=0},               0,    0, A|F, "link" },
    {   "maximum", 0,                  0,                 AV_OPT_TYPE_CONST,  {.i64=1},               0,    0, A|F, "link" },
    { "detection", "set detection",    OFFSET(detection), AV_OPT_TYPE_INT,    {.i64=1},               0,    1, A|F, "detection" },
    {   "peak",    0,                  0,                 AV_OPT_TYPE_CONST,  {.i64=0},               0,    0, A|F, "detection" },
    {   "rms",     0,                  0,                 AV_OPT_TYPE_CONST,  {.i64=1},               0,    0, A|F, "detection" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(sidechaincompress);

static av_cold int init(AVFilterContext *ctx)
{
    SidechainCompressContext *s = ctx->priv;

    s->thres = log(s->threshold);
    s->lin_knee_start = s->threshold / sqrt(s->knee);
    s->knee_start = log(s->lin_knee_start);
    s->knee_stop = log(s->threshold * sqrt(s->knee));
    s->compressed_knee_stop = (s->knee_stop - s->thres) / s->ratio + s->thres;

    return 0;
}

static inline float hermite_interpolation(float x, float x0, float x1,
                                          float p0, float p1,
                                          float m0, float m1)
{
    float width = x1 - x0;
    float t = (x - x0) / width;
    float t2, t3;
    float ct0, ct1, ct2, ct3;

    m0 *= width;
    m1 *= width;

    t2 = t*t;
    t3 = t2*t;
    ct0 = p0;
    ct1 = m0;

    ct2 = -3 * p0 - 2 * m0 + 3 * p1 - m1;
    ct3 = 2 * p0 + m0  - 2 * p1 + m1;

    return ct3 * t3 + ct2 * t2 + ct1 * t + ct0;
}

// A fake infinity value (because real infinity may break some hosts)
#define FAKE_INFINITY (65536.0 * 65536.0)

// Check for infinity (with appropriate-ish tolerance)
#define IS_FAKE_INFINITY(value) (fabs(value-FAKE_INFINITY) < 1.0)

static double output_gain(double lin_slope, double ratio, double thres,
                          double knee, double knee_start, double knee_stop,
                          double compressed_knee_stop, int detection)
{
    double slope = log(lin_slope);
    double gain = 0.0;
    double delta = 0.0;

    if (detection)
        slope *= 0.5;

    if (IS_FAKE_INFINITY(ratio)) {
        gain = thres;
        delta = 0.0;
    } else {
        gain = (slope - thres) / ratio + thres;
        delta = 1.0 / ratio;
    }

    if (knee > 1.0 && slope < knee_stop)
        gain = hermite_interpolation(slope, knee_start, knee_stop,
                                     knee_start, compressed_knee_stop,
                                     1.0, delta);

    return exp(gain - slope);
}

static int filter_frame(AVFilterLink *link, AVFrame *frame)
{
    AVFilterContext *ctx = link->dst;
    SidechainCompressContext *s = ctx->priv;
    AVFilterLink *sclink = ctx->inputs[1];
    AVFilterLink *outlink = ctx->outputs[0];
    const double makeup = s->makeup;
    const double *scsrc;
    double *sample;
    int nb_samples;
    int ret, i, c;

    for (i = 0; i < 2; i++)
        if (link == ctx->inputs[i])
            break;
    av_assert0(i < 2 && !s->input_frame[i]);
    s->input_frame[i] = frame;

    if (!s->input_frame[0] || !s->input_frame[1])
        return 0;

    nb_samples = FFMIN(s->input_frame[0]->nb_samples,
                       s->input_frame[1]->nb_samples);

    sample = (double *)s->input_frame[0]->data[0];
    scsrc = (const double *)s->input_frame[1]->data[0];

    for (i = 0; i < nb_samples; i++) {
        double abs_sample, gain = 1.0;

        abs_sample = FFABS(scsrc[0]);

        if (s->link == 1) {
            for (c = 1; c < sclink->channels; c++)
                abs_sample = FFMAX(FFABS(scsrc[c]), abs_sample);
        } else {
            for (c = 1; c < sclink->channels; c++)
                abs_sample += FFABS(scsrc[c]);

            abs_sample /= sclink->channels;
        }

        if (s->detection)
            abs_sample *= abs_sample;

        s->lin_slope += (abs_sample - s->lin_slope) * (abs_sample > s->lin_slope ? s->attack_coeff : s->release_coeff);

        if (s->lin_slope > 0.0 && s->lin_slope > s->lin_knee_start)
            gain = output_gain(s->lin_slope, s->ratio, s->thres, s->knee,
                               s->knee_start, s->knee_stop,
                               s->compressed_knee_stop, s->detection);

        for (c = 0; c < outlink->channels; c++)
            sample[c] *= gain * makeup;

        sample += outlink->channels;
        scsrc += sclink->channels;
    }

    ret = ff_filter_frame(outlink, s->input_frame[0]);

    s->input_frame[0] = NULL;
    av_frame_free(&s->input_frame[1]);

    return ret;
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    SidechainCompressContext *s = ctx->priv;
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

static int query_formats(AVFilterContext *ctx)
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

    ff_add_channel_layout(&layouts, ctx->inputs[0]->in_channel_layouts->channel_layouts[0]);
    if (!layouts)
        return AVERROR(ENOMEM);
    ff_channel_layouts_ref(layouts, &ctx->outputs[0]->in_channel_layouts);

    for (i = 0; i < 2; i++) {
        layouts = ff_all_channel_layouts();
        if (!layouts)
            return AVERROR(ENOMEM);
        ff_channel_layouts_ref(layouts, &ctx->inputs[i]->out_channel_layouts);
    }

    formats = ff_make_format_list(sample_fmts);
    if (!formats)
        return AVERROR(ENOMEM);
    ret = ff_set_common_formats(ctx, formats);
    if (ret < 0)
        return ret;

    formats = ff_all_samplerates();
    if (!formats)
        return AVERROR(ENOMEM);
    return ff_set_common_samplerates(ctx, formats);
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    SidechainCompressContext *s = ctx->priv;

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

    s->attack_coeff = FFMIN(1.f, 1.f / (s->attack * outlink->sample_rate / 4000.f));
    s->release_coeff = FFMIN(1.f, 1.f / (s->release * outlink->sample_rate / 4000.f));

    return 0;
}

static const AVFilterPad sidechaincompress_inputs[] = {
    {
        .name           = "main",
        .type           = AVMEDIA_TYPE_AUDIO,
        .filter_frame   = filter_frame,
        .needs_writable = 1,
        .needs_fifo     = 1,
    },{
        .name           = "sidechain",
        .type           = AVMEDIA_TYPE_AUDIO,
        .filter_frame   = filter_frame,
        .needs_fifo     = 1,
    },
    { NULL }
};

static const AVFilterPad sidechaincompress_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .config_props  = config_output,
        .request_frame = request_frame,
    },
    { NULL }
};

AVFilter ff_af_sidechaincompress = {
    .name           = "sidechaincompress",
    .description    = NULL_IF_CONFIG_SMALL("Sidechain compressor."),
    .priv_size      = sizeof(SidechainCompressContext),
    .priv_class     = &sidechaincompress_class,
    .init           = init,
    .query_formats  = query_formats,
    .inputs         = sidechaincompress_inputs,
    .outputs        = sidechaincompress_outputs,
};
