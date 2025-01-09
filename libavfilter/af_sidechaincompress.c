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

#include "config_components.h"

#include "libavutil/audio_fifo.h"
#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/opt.h"

#include "audio.h"
#include "avfilter.h"
#include "filters.h"
#include "formats.h"
#include "hermite.h"

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
    double lin_knee_stop;
    double adj_knee_start;
    double adj_knee_stop;
    double compressed_knee_start;
    double compressed_knee_stop;
    int link;
    int detection;
    int mode;

    AVAudioFifo *fifo[2];
    int64_t pts;
} SidechainCompressContext;

#define OFFSET(x) offsetof(SidechainCompressContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM
#define F AV_OPT_FLAG_FILTERING_PARAM
#define R AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption options[] = {
    { "level_in",  "set input gain",     OFFSET(level_in),  AV_OPT_TYPE_DOUBLE, {.dbl=1},        0.015625,   64, A|F|R },
    { "mode",      "set mode",           OFFSET(mode),      AV_OPT_TYPE_INT,    {.i64=0},               0,    1, A|F|R, .unit = "mode" },
    {   "downward",0,                    0,                 AV_OPT_TYPE_CONST,  {.i64=0},               0,    0, A|F|R, .unit = "mode" },
    {   "upward",  0,                    0,                 AV_OPT_TYPE_CONST,  {.i64=1},               0,    0, A|F|R, .unit = "mode" },
    { "threshold", "set threshold",      OFFSET(threshold), AV_OPT_TYPE_DOUBLE, {.dbl=0.125}, 0.000976563,    1, A|F|R },
    { "ratio",     "set ratio",          OFFSET(ratio),     AV_OPT_TYPE_DOUBLE, {.dbl=2},               1,   20, A|F|R },
    { "attack",    "set attack",         OFFSET(attack),    AV_OPT_TYPE_DOUBLE, {.dbl=20},           0.01, 2000, A|F|R },
    { "release",   "set release",        OFFSET(release),   AV_OPT_TYPE_DOUBLE, {.dbl=250},          0.01, 9000, A|F|R },
    { "makeup",    "set make up gain",   OFFSET(makeup),    AV_OPT_TYPE_DOUBLE, {.dbl=1},               1,   64, A|F|R },
    { "knee",      "set knee",           OFFSET(knee),      AV_OPT_TYPE_DOUBLE, {.dbl=2.82843},         1,    8, A|F|R },
    { "link",      "set link type",      OFFSET(link),      AV_OPT_TYPE_INT,    {.i64=0},               0,    1, A|F|R, .unit = "link" },
    {   "average", 0,                    0,                 AV_OPT_TYPE_CONST,  {.i64=0},               0,    0, A|F|R, .unit = "link" },
    {   "maximum", 0,                    0,                 AV_OPT_TYPE_CONST,  {.i64=1},               0,    0, A|F|R, .unit = "link" },
    { "detection", "set detection",      OFFSET(detection), AV_OPT_TYPE_INT,    {.i64=1},               0,    1, A|F|R, .unit = "detection" },
    {   "peak",    0,                    0,                 AV_OPT_TYPE_CONST,  {.i64=0},               0,    0, A|F|R, .unit = "detection" },
    {   "rms",     0,                    0,                 AV_OPT_TYPE_CONST,  {.i64=1},               0,    0, A|F|R, .unit = "detection" },
    { "level_sc",  "set sidechain gain", OFFSET(level_sc),  AV_OPT_TYPE_DOUBLE, {.dbl=1},        0.015625,   64, A|F|R },
    { "mix",       "set mix",            OFFSET(mix),       AV_OPT_TYPE_DOUBLE, {.dbl=1},               0,    1, A|F|R },
    { NULL }
};

AVFILTER_DEFINE_CLASS_EXT(sidechaincompress_acompressor,
                          "acompressor/sidechaincompress",
                          options);

// A fake infinity value (because real infinity may break some hosts)
#define FAKE_INFINITY (65536.0 * 65536.0)

// Check for infinity (with appropriate-ish tolerance)
#define IS_FAKE_INFINITY(value) (fabs(value-FAKE_INFINITY) < 1.0)

static double output_gain(double lin_slope, double ratio, double thres,
                          double knee, double knee_start, double knee_stop,
                          double compressed_knee_start,
                          double compressed_knee_stop,
                          int detection, int mode)
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

    if (mode) {
        if (knee > 1.0 && slope > knee_start)
            gain = hermite_interpolation(slope, knee_stop, knee_start,
                                         knee_stop, compressed_knee_start,
                                         1.0, delta);
    } else {
        if (knee > 1.0 && slope < knee_stop)
            gain = hermite_interpolation(slope, knee_start, knee_stop,
                                         knee_start, compressed_knee_stop,
                                         1.0, delta);
    }

    return exp(gain - slope);
}

static int compressor_config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    SidechainCompressContext *s = ctx->priv;

    s->thres = log(s->threshold);
    s->lin_knee_start = s->threshold / sqrt(s->knee);
    s->lin_knee_stop = s->threshold * sqrt(s->knee);
    s->adj_knee_start = s->lin_knee_start * s->lin_knee_start;
    s->adj_knee_stop = s->lin_knee_stop * s->lin_knee_stop;
    s->knee_start = log(s->lin_knee_start);
    s->knee_stop = log(s->lin_knee_stop);
    s->compressed_knee_start = (s->knee_start - s->thres) / s->ratio + s->thres;
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
        double detector;
        int detected;

        abs_sample = fabs(scsrc[0] * level_sc);

        if (s->link == 1) {
            for (c = 1; c < sclink->ch_layout.nb_channels; c++)
                abs_sample = FFMAX(fabs(scsrc[c] * level_sc), abs_sample);
        } else {
            for (c = 1; c < sclink->ch_layout.nb_channels; c++)
                abs_sample += fabs(scsrc[c] * level_sc);

            abs_sample /= sclink->ch_layout.nb_channels;
        }

        if (s->detection)
            abs_sample *= abs_sample;

        s->lin_slope += (abs_sample - s->lin_slope) * (abs_sample > s->lin_slope ? s->attack_coeff : s->release_coeff);

        if (s->mode) {
            detector = (s->detection ? s->adj_knee_stop : s->lin_knee_stop);
            detected = s->lin_slope < detector;
        } else {
            detector = (s->detection ? s->adj_knee_start : s->lin_knee_start);
            detected = s->lin_slope > detector;
        }

        if (s->lin_slope > 0.0 && detected)
            gain = output_gain(s->lin_slope, s->ratio, s->thres, s->knee,
                               s->knee_start, s->knee_stop,
                               s->compressed_knee_start,
                               s->compressed_knee_stop,
                               s->detection, s->mode);

        for (c = 0; c < inlink->ch_layout.nb_channels; c++)
            dst[c] = src[c] * level_in * (gain * makeup * mix + (1. - mix));

        src += inlink->ch_layout.nb_channels;
        dst += inlink->ch_layout.nb_channels;
        scsrc += sclink->ch_layout.nb_channels;
    }
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    int ret;

    ret = ff_filter_process_command(ctx, cmd, args, res, res_len, flags);
    if (ret < 0)
        return ret;

    compressor_config_output(ctx->outputs[0]);

    return 0;
}

#if CONFIG_SIDECHAINCOMPRESS_FILTER
static int activate(AVFilterContext *ctx)
{
    SidechainCompressContext *s = ctx->priv;
    AVFrame *out = NULL, *in[2] = { NULL };
    int ret, i, nb_samples;
    double *dst;

    FF_FILTER_FORWARD_STATUS_BACK_ALL(ctx->outputs[0], ctx);
    if ((ret = ff_inlink_consume_frame(ctx->inputs[0], &in[0])) > 0) {
        av_audio_fifo_write(s->fifo[0], (void **)in[0]->extended_data,
                            in[0]->nb_samples);
        av_frame_free(&in[0]);
    }
    if (ret < 0)
        return ret;
    if ((ret = ff_inlink_consume_frame(ctx->inputs[1], &in[1])) > 0) {
        av_audio_fifo_write(s->fifo[1], (void **)in[1]->extended_data,
                            in[1]->nb_samples);
        av_frame_free(&in[1]);
    }
    if (ret < 0)
        return ret;

    nb_samples = FFMIN(av_audio_fifo_size(s->fifo[0]), av_audio_fifo_size(s->fifo[1]));
    if (nb_samples) {
        out = ff_get_audio_buffer(ctx->outputs[0], nb_samples);
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
        s->pts += av_rescale_q(nb_samples, (AVRational){1, ctx->outputs[0]->sample_rate}, ctx->outputs[0]->time_base);

        compressor(s, (double *)in[0]->data[0], dst,
                   (double *)in[1]->data[0], nb_samples,
                   s->level_in, s->level_sc,
                   ctx->inputs[0], ctx->inputs[1]);

        av_frame_free(&in[0]);
        av_frame_free(&in[1]);

        ret = ff_filter_frame(ctx->outputs[0], out);
        if (ret < 0)
            return ret;
    }
    FF_FILTER_FORWARD_STATUS(ctx->inputs[0], ctx->outputs[0]);
    FF_FILTER_FORWARD_STATUS(ctx->inputs[1], ctx->outputs[0]);
    if (ff_outlink_frame_wanted(ctx->outputs[0])) {
        if (!av_audio_fifo_size(s->fifo[0]))
            ff_inlink_request_frame(ctx->inputs[0]);
        if (!av_audio_fifo_size(s->fifo[1]))
            ff_inlink_request_frame(ctx->inputs[1]);
    }
    return 0;
}

static int query_formats(const AVFilterContext *ctx,
                         AVFilterFormatsConfig **cfg_in,
                         AVFilterFormatsConfig **cfg_out)
{
    static const enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_DBL,
        AV_SAMPLE_FMT_NONE
    };
    int ret;

    /* Generic code will link the channel properties of the main input and the output;
     * it won't touch the second input as its channel_layouts is already set. */
    ret = ff_channel_layouts_ref(ff_all_channel_counts(),
                                 &cfg_in[1]->channel_layouts);
    if (ret < 0)
        return ret;

    if ((ret = ff_set_common_formats_from_list2(ctx, cfg_in, cfg_out, sample_fmts)) < 0)
        return ret;

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    SidechainCompressContext *s = ctx->priv;

    outlink->time_base   = ctx->inputs[0]->time_base;

    s->fifo[0] = av_audio_fifo_alloc(ctx->inputs[0]->format, ctx->inputs[0]->ch_layout.nb_channels, 1024);
    s->fifo[1] = av_audio_fifo_alloc(ctx->inputs[1]->format, ctx->inputs[1]->ch_layout.nb_channels, 1024);
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
    },{
        .name           = "sidechain",
        .type           = AVMEDIA_TYPE_AUDIO,
    },
};

static const AVFilterPad sidechaincompress_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .config_props  = config_output,
    },
};

const FFFilter ff_af_sidechaincompress = {
    .p.name         = "sidechaincompress",
    .p.description  = NULL_IF_CONFIG_SMALL("Sidechain compressor."),
    .p.priv_class   = &sidechaincompress_acompressor_class,
    .priv_size      = sizeof(SidechainCompressContext),
    .activate       = activate,
    .uninit         = uninit,
    FILTER_INPUTS(sidechaincompress_inputs),
    FILTER_OUTPUTS(sidechaincompress_outputs),
    FILTER_QUERY_FUNC2(query_formats),
    .process_command = process_command,
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
        out = ff_get_audio_buffer(outlink, in->nb_samples);
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

static const AVFilterPad acompressor_inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_AUDIO,
        .filter_frame   = acompressor_filter_frame,
    },
};

static const AVFilterPad acompressor_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .config_props  = compressor_config_output,
    },
};

const FFFilter ff_af_acompressor = {
    .p.name         = "acompressor",
    .p.description  = NULL_IF_CONFIG_SMALL("Audio compressor."),
    .p.priv_class   = &sidechaincompress_acompressor_class,
    .priv_size      = sizeof(SidechainCompressContext),
    FILTER_INPUTS(acompressor_inputs),
    FILTER_OUTPUTS(acompressor_outputs),
    FILTER_SINGLE_SAMPLEFMT(AV_SAMPLE_FMT_DBL),
    .process_command = process_command,
};
#endif  /* CONFIG_ACOMPRESSOR_FILTER */
