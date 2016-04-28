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
 * Audio (Sidechain) Compressor filter
 */

#include "libavutil/audio_fifo.h"
#include "libavutil/avassert.h"
#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/opt.h"

#include "audio.h"
#include "avfilter.h"
#include "formats.h"
#include "hermite.h"
#include "internal.h"

typedef struct SidechainCompressContext {
    const AVClass *class;

    double level_in;
    double level_sc;
    double attack, attack_coeff;
    double release, release_coeff;
    double lin_slope;
    double ratio;
    double threshold;
    double makeup;
    double mix;
    double thres;
    double knee;
    double knee_start;
    double knee_stop;
    double lin_knee_start;
    double adj_knee_start;
    double compressed_knee_stop;
    int link;
    int detection;

    AVAudioFifo *fifo[2];
    int64_t pts;
} SidechainCompressContext;

#define OFFSET(x) offsetof(SidechainCompressContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM
#define F AV_OPT_FLAG_FILTERING_PARAM

static const AVOption options[] = {
    { "level_in",  "set input gain",     OFFSET(level_in),  AV_OPT_TYPE_DOUBLE, {.dbl=1},        0.015625,   64, A|F },
    { "threshold", "set threshold",      OFFSET(threshold), AV_OPT_TYPE_DOUBLE, {.dbl=0.125}, 0.000976563,    1, A|F },
    { "ratio",     "set ratio",          OFFSET(ratio),     AV_OPT_TYPE_DOUBLE, {.dbl=2},               1,   20, A|F },
    { "attack",    "set attack",         OFFSET(attack),    AV_OPT_TYPE_DOUBLE, {.dbl=20},           0.01, 2000, A|F },
    { "release",   "set release",        OFFSET(release),   AV_OPT_TYPE_DOUBLE, {.dbl=250},          0.01, 9000, A|F },
    { "makeup",    "set make up gain",   OFFSET(makeup),    AV_OPT_TYPE_DOUBLE, {.dbl=2},               1,   64, A|F },
    { "knee",      "set knee",           OFFSET(knee),      AV_OPT_TYPE_DOUBLE, {.dbl=2.82843},         1,    8, A|F },
    { "link",      "set link type",      OFFSET(link),      AV_OPT_TYPE_INT,    {.i64=0},               0,    1, A|F, "link" },
    {   "average", 0,                    0,                 AV_OPT_TYPE_CONST,  {.i64=0},               0,    0, A|F, "link" },
    {   "maximum", 0,                    0,                 AV_OPT_TYPE_CONST,  {.i64=1},               0,    0, A|F, "link" },
    { "detection", "set detection",      OFFSET(detection), AV_OPT_TYPE_INT,    {.i64=1},               0,    1, A|F, "detection" },
    {   "peak",    0,                    0,                 AV_OPT_TYPE_CONST,  {.i64=0},               0,    0, A|F, "detection" },
    {   "rms",     0,                    0,                 AV_OPT_TYPE_CONST,  {.i64=1},               0,    0, A|F, "detection" },
    { "level_sc",  "set sidechain gain", OFFSET(level_sc),  AV_OPT_TYPE_DOUBLE, {.dbl=1},        0.015625,   64, A|F },
    { "mix",       "set mix",            OFFSET(mix),       AV_OPT_TYPE_DOUBLE, {.dbl=1},               0,    1, A|F },
    { NULL }
};

#define sidechaincompress_options options
AVFILTER_DEFINE_CLASS(sidechaincompress);

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

static int compressor_config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    SidechainCompressContext *s = ctx->priv;

    s->thres = log(s->threshold);
    s->lin_knee_start = s->threshold / sqrt(s->knee);
    s->adj_knee_start = s->lin_knee_start * s->lin_knee_start;
    s->knee_start = log(s->lin_knee_start);
    s->knee_stop = log(s->threshold * sqrt(s->knee));
    s->compressed_knee_stop = (s->knee_stop - s->thres) / s->ratio + s->thres;

    s->attack_coeff = FFMIN(1., 1. / (s->attack * outlink->sample_rate / 4000.));
    s->release_coeff = FFMIN(1., 1. / (s->release * outlink->sample_rate / 4000.));

    return 0;
}

static void compressor(SidechainCompressContext *s,
                       const double *src, double *dst, const double *scsrc, int nb_samples,
                       double level_in, double level_sc,
                       AVFilterLink *inlink, AVFilterLink *sclink)
{
    const double makeup = s->makeup;
    const double mix = s->mix;
    int i, c;

    for (i = 0; i < nb_samples; i++) {
        double abs_sample, gain = 1.0;

        abs_sample = fabs(scsrc[0] * level_sc);

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

        s->lin_slope += (abs_sample - s->lin_slope) * (abs_sample > s->lin_slope ? s->attack_coeff : s->release_coeff);

        if (s->lin_slope > 0.0 && s->lin_slope > (s->detection ? s->adj_knee_start : s->lin_knee_start))
            gain = output_gain(s->lin_slope, s->ratio, s->thres, s->knee,
                               s->knee_start, s->knee_stop,
                               s->compressed_knee_stop, s->detection);

        for (c = 0; c < inlink->channels; c++)
            dst[c] = src[c] * level_in * (gain * makeup * mix + (1. - mix));

        src += inlink->channels;
        dst += inlink->channels;
        scsrc += sclink->channels;
    }
}

#if CONFIG_SIDECHAINCOMPRESS_FILTER
static int filter_frame(AVFilterLink *link, AVFrame *frame)
{
    AVFilterContext *ctx = link->dst;
    SidechainCompressContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out = NULL, *in[2] = { NULL };
    double *dst;
    int nb_samples;
    int i;

    for (i = 0; i < 2; i++)
        if (link == ctx->inputs[i])
            break;
    av_assert0(i < 2);
    av_audio_fifo_write(s->fifo[i], (void **)frame->extended_data,
                        frame->nb_samples);
    av_frame_free(&frame);

    nb_samples = FFMIN(av_audio_fifo_size(s->fifo[0]), av_audio_fifo_size(s->fifo[1]));
    if (!nb_samples)
        return 0;

    out = ff_get_audio_buffer(outlink, nb_samples);
    if (!out)
        return AVERROR(ENOMEM);
    for (i = 0; i < 2; i++) {
        in[i] = ff_get_audio_buffer(ctx->inputs[i], nb_samples);
        if (!in[i]) {
            av_frame_free(&in[0]);
            av_frame_free(&in[1]);
            av_frame_free(&out);
            return AVERROR(ENOMEM);
        }
        av_audio_fifo_read(s->fifo[i], (void **)in[i]->data, nb_samples);
    }

    dst = (double *)out->data[0];
    out->pts = s->pts;
    s->pts += nb_samples;

    compressor(s, (double *)in[0]->data[0], dst,
               (double *)in[1]->data[0], nb_samples,
               s->level_in, s->level_sc,
               ctx->inputs[0], ctx->inputs[1]);

    av_frame_free(&in[0]);
    av_frame_free(&in[1]);

    return ff_filter_frame(outlink, out);
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    SidechainCompressContext *s = ctx->priv;
    int i;

    /* get a frame on each input */
    for (i = 0; i < 2; i++) {
        AVFilterLink *inlink = ctx->inputs[i];
        if (!av_audio_fifo_size(s->fifo[i]))
            return ff_request_frame(inlink);
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

    s->fifo[0] = av_audio_fifo_alloc(ctx->inputs[0]->format, ctx->inputs[0]->channels, 1024);
    s->fifo[1] = av_audio_fifo_alloc(ctx->inputs[1]->format, ctx->inputs[1]->channels, 1024);
    if (!s->fifo[0] || !s->fifo[1])
        return AVERROR(ENOMEM);

    compressor_config_output(outlink);

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    SidechainCompressContext *s = ctx->priv;

    av_audio_fifo_free(s->fifo[0]);
    av_audio_fifo_free(s->fifo[1]);
}

static const AVFilterPad sidechaincompress_inputs[] = {
    {
        .name           = "main",
        .type           = AVMEDIA_TYPE_AUDIO,
        .filter_frame   = filter_frame,
    },{
        .name           = "sidechain",
        .type           = AVMEDIA_TYPE_AUDIO,
        .filter_frame   = filter_frame,
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
    .query_formats  = query_formats,
    .uninit         = uninit,
    .inputs         = sidechaincompress_inputs,
    .outputs        = sidechaincompress_outputs,
};
#endif  /* CONFIG_SIDECHAINCOMPRESS_FILTER */

#if CONFIG_ACOMPRESSOR_FILTER
static int acompressor_filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    const double *src = (const double *)in->data[0];
    AVFilterContext *ctx = inlink->dst;
    SidechainCompressContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
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

    compressor(s, src, dst, src, in->nb_samples,
               s->level_in, s->level_in,
               inlink, inlink);

    if (out != in)
        av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static int acompressor_query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats;
    AVFilterChannelLayouts *layouts;
    static const enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_DBL,
        AV_SAMPLE_FMT_NONE
    };
    int ret;

    layouts = ff_all_channel_counts();
    if (!layouts)
        return AVERROR(ENOMEM);
    ret = ff_set_common_channel_layouts(ctx, layouts);
    if (ret < 0)
        return ret;

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

#define acompressor_options options
AVFILTER_DEFINE_CLASS(acompressor);

static const AVFilterPad acompressor_inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_AUDIO,
        .filter_frame   = acompressor_filter_frame,
    },
    { NULL }
};

static const AVFilterPad acompressor_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .config_props  = compressor_config_output,
    },
    { NULL }
};

AVFilter ff_af_acompressor = {
    .name           = "acompressor",
    .description    = NULL_IF_CONFIG_SMALL("Audio compressor."),
    .priv_size      = sizeof(SidechainCompressContext),
    .priv_class     = &acompressor_class,
    .query_formats  = acompressor_query_formats,
    .inputs         = acompressor_inputs,
    .outputs        = acompressor_outputs,
};
#endif  /* CONFIG_ACOMPRESSOR_FILTER */
