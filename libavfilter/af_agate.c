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

#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "audio.h"
#include "formats.h"
#include "hermite.h"

typedef struct AudioGateContext {
    const AVClass *class;

    double level_in;
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
} AudioGateContext;

#define OFFSET(x) offsetof(AudioGateContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption agate_options[] = {
    { "level_in",  "set input level",        OFFSET(level_in),  AV_OPT_TYPE_DOUBLE, {.dbl=1},           0.015625,   64, A },
    { "range",     "set max gain reduction", OFFSET(range),     AV_OPT_TYPE_DOUBLE, {.dbl=0.06125},     0, 1, A },
    { "threshold", "set threshold",          OFFSET(threshold), AV_OPT_TYPE_DOUBLE, {.dbl=0.125},       0, 1, A },
    { "ratio",     "set ratio",              OFFSET(ratio),     AV_OPT_TYPE_DOUBLE, {.dbl=2},           1,  9000, A },
    { "attack",    "set attack",             OFFSET(attack),    AV_OPT_TYPE_DOUBLE, {.dbl=20},          0.01, 9000, A },
    { "release",   "set release",            OFFSET(release),   AV_OPT_TYPE_DOUBLE, {.dbl=250},         0.01, 9000, A },
    { "makeup",    "set makeup gain",        OFFSET(makeup),    AV_OPT_TYPE_DOUBLE, {.dbl=1},           1,   64, A },
    { "knee",      "set knee",               OFFSET(knee),      AV_OPT_TYPE_DOUBLE, {.dbl=2.828427125}, 1,    8, A },
    { "detection", "set detection",          OFFSET(detection), AV_OPT_TYPE_INT,    {.i64=0},           0,    1, A, "detection" },
    {   "peak",    0,                        0,                 AV_OPT_TYPE_CONST,  {.i64=0},           0,    0, A, "detection" },
    {   "rms",     0,                        0,                 AV_OPT_TYPE_CONST,  {.i64=1},           0,    0, A, "detection" },
    { "link",      "set link",               OFFSET(link),      AV_OPT_TYPE_INT,    {.i64=0},           0,    1, A, "link" },
    {   "average", 0,                        0,                 AV_OPT_TYPE_CONST,  {.i64=0},           0,    0, A, "link" },
    {   "maximum", 0,                        0,                 AV_OPT_TYPE_CONST,  {.i64=1},           0,    0, A, "link" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(agate);

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats = NULL;
    AVFilterChannelLayouts *layouts;
    int ret;

    ff_add_format(&formats, AV_SAMPLE_FMT_DBL);
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

static int config_input(AVFilterLink *inlink)
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

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    AudioGateContext *s = ctx->priv;
    const double *src = (const double *)in->data[0];
    const double makeup = s->makeup;
    const double attack_coeff = s->attack_coeff;
    const double release_coeff = s->release_coeff;
    const double level_in = s->level_in;
    AVFrame *out = NULL;
    double *dst;
    int n, c;

    if (av_frame_is_writable(in)) {
        out = in;
    } else {
        AVFrame *out = ff_get_audio_buffer(inlink, in->nb_samples);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }
        av_frame_copy_props(out, in);
    }
    dst = (double *)out->data[0];

    for (n = 0; n < in->nb_samples; n++, src += inlink->channels, dst += inlink->channels) {
        double abs_sample = FFABS(src[0]), gain = 1.0;

        for (c = 0; c < inlink->channels; c++)
            dst[c] = src[c] * level_in;

        if (s->link == 1) {
            for (c = 1; c < inlink->channels; c++)
                abs_sample = FFMAX(FFABS(src[c]), abs_sample);
        } else {
            for (c = 1; c < inlink->channels; c++)
                abs_sample += FFABS(src[c]);

            abs_sample /= inlink->channels;
        }

        if (s->detection)
            abs_sample *= abs_sample;

        s->lin_slope += (abs_sample - s->lin_slope) * (abs_sample > s->lin_slope ? attack_coeff : release_coeff);
        if (s->lin_slope > 0.0)
            gain = output_gain(s->lin_slope, s->ratio, s->thres,
                               s->knee, s->knee_start, s->knee_stop,
                               s->lin_knee_stop, s->range);

        for (c = 0; c < inlink->channels; c++)
            dst[c] *= gain * makeup;
    }

    if (out != in)
        av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

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

AVFilter ff_af_agate = {
    .name           = "agate",
    .description    = NULL_IF_CONFIG_SMALL("Audio gate."),
    .query_formats  = query_formats,
    .priv_size      = sizeof(AudioGateContext),
    .priv_class     = &agate_class,
    .inputs         = inputs,
    .outputs        = outputs,
};
