/*
 * Copyright (c) 2001 Heikki Leinonen
 * Copyright (c) 2001 Chris Bagwell
 * Copyright (c) 2003 Donnie Smith
 * Copyright (c) 2014 Paul B Mahol
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

#include <float.h> /* DBL_MAX */

#include "libavutil/avassert.h"
#include "libavutil/opt.h"
#include "libavutil/timestamp.h"
#include "audio.h"
#include "filters.h"
#include "formats.h"
#include "avfilter.h"
#include "internal.h"

enum SilenceDetect {
    D_PEAK,
    D_RMS,
};

enum ThresholdMode {
    T_ANY,
    T_ALL,
};

typedef struct SilenceRemoveContext {
    const AVClass *class;

    int start_mode;
    int start_periods;
    int64_t start_duration;
    int64_t start_duration_opt;
    double start_threshold;
    int64_t start_silence;
    int64_t start_silence_opt;

    int stop_mode;
    int stop_periods;
    int64_t stop_duration;
    int64_t stop_duration_opt;
    double stop_threshold;
    int64_t stop_silence;
    int64_t stop_silence_opt;

    int64_t window_duration_opt;

    int start_found_periods;
    int stop_found_periods;

    int start_sample_count;
    int start_silence_count;

    int stop_sample_count;
    int stop_silence_count;

    AVFrame *start_window;
    AVFrame *stop_window;

    int64_t window_duration;

    int start_window_pos;
    int start_window_size;

    int stop_window_pos;
    int stop_window_size;

    double *start_cache;
    double *stop_cache;

    AVFrame *start_queuef;
    int start_queue_pos;
    int start_queue_size;

    AVFrame *stop_queuef;
    int stop_queue_pos;
    int stop_queue_size;

    int restart;
    int found_nonsilence;
    int64_t next_pts;

    int detection;

    float (*compute_flt)(float *c, float s, float ws, int size);
    double (*compute_dbl)(double *c, double s, double ws, int size);
} SilenceRemoveContext;

#define OFFSET(x) offsetof(SilenceRemoveContext, x)
#define AF AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_AUDIO_PARAM

static const AVOption silenceremove_options[] = {
    { "start_periods",   "set periods of silence parts to skip from start",    OFFSET(start_periods),       AV_OPT_TYPE_INT,      {.i64=0},     0,      9000, AF },
    { "start_duration",  "set start duration of non-silence part",             OFFSET(start_duration_opt),  AV_OPT_TYPE_DURATION, {.i64=0},     0, INT32_MAX, AF },
    { "start_threshold", "set threshold for start silence detection",          OFFSET(start_threshold),     AV_OPT_TYPE_DOUBLE,   {.dbl=0},     0,   DBL_MAX, AF },
    { "start_silence",   "set start duration of silence part to keep",         OFFSET(start_silence_opt),   AV_OPT_TYPE_DURATION, {.i64=0},     0, INT32_MAX, AF },
    { "start_mode",      "set which channel will trigger trimming from start", OFFSET(start_mode),          AV_OPT_TYPE_INT,      {.i64=T_ANY}, T_ANY, T_ALL, AF, "mode" },
    {   "any",           0,                                                    0,                           AV_OPT_TYPE_CONST,    {.i64=T_ANY}, 0,         0, AF, "mode" },
    {   "all",           0,                                                    0,                           AV_OPT_TYPE_CONST,    {.i64=T_ALL}, 0,         0, AF, "mode" },
    { "stop_periods",    "set periods of silence parts to skip from end",      OFFSET(stop_periods),        AV_OPT_TYPE_INT,      {.i64=0}, -9000,      9000, AF },
    { "stop_duration",   "set stop duration of silence part",                  OFFSET(stop_duration_opt),   AV_OPT_TYPE_DURATION, {.i64=0},     0, INT32_MAX, AF },
    { "stop_threshold",  "set threshold for stop silence detection",           OFFSET(stop_threshold),      AV_OPT_TYPE_DOUBLE,   {.dbl=0},     0,   DBL_MAX, AF },
    { "stop_silence",    "set stop duration of silence part to keep",          OFFSET(stop_silence_opt),    AV_OPT_TYPE_DURATION, {.i64=0},     0, INT32_MAX, AF },
    { "stop_mode",       "set which channel will trigger trimming from end",   OFFSET(stop_mode),           AV_OPT_TYPE_INT,      {.i64=T_ANY}, T_ANY, T_ALL, AF, "mode" },
    { "detection",       "set how silence is detected",                        OFFSET(detection),           AV_OPT_TYPE_INT,      {.i64=D_RMS}, D_PEAK,D_RMS, AF, "detection" },
    {   "peak",          "use absolute values of samples",                     0,                           AV_OPT_TYPE_CONST,    {.i64=D_PEAK},0,         0, AF, "detection" },
    {   "rms",           "use squared values of samples",                      0,                           AV_OPT_TYPE_CONST,    {.i64=D_RMS}, 0,         0, AF, "detection" },
    { "window",          "set duration of window for silence detection",       OFFSET(window_duration_opt), AV_OPT_TYPE_DURATION, {.i64=20000}, 0, 100000000, AF },
    { NULL }
};

AVFILTER_DEFINE_CLASS(silenceremove);

#define DEPTH 32
#include "silenceremove_template.c"

#undef DEPTH
#define DEPTH 64
#include "silenceremove_template.c"

static av_cold int init(AVFilterContext *ctx)
{
    SilenceRemoveContext *s = ctx->priv;

    if (s->stop_periods < 0) {
        s->stop_periods = -s->stop_periods;
        s->restart = 1;
    }

    return 0;
}

static void clear_windows(SilenceRemoveContext *s)
{
    av_samples_set_silence(s->start_window->extended_data, 0,
                           s->start_window->nb_samples,
                           s->start_window->ch_layout.nb_channels,
                           s->start_window->format);
    av_samples_set_silence(s->stop_window->extended_data, 0,
                           s->stop_window->nb_samples,
                           s->stop_window->ch_layout.nb_channels,
                           s->stop_window->format);

    s->start_window_pos = 0;
    s->start_window_size = 0;
    s->stop_window_pos = 0;
    s->stop_window_size = 0;
    s->start_queue_pos = 0;
    s->start_queue_size = 0;
    s->stop_queue_pos = 0;
    s->stop_queue_size = 0;
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    SilenceRemoveContext *s = ctx->priv;

    s->next_pts = AV_NOPTS_VALUE;
    s->window_duration = av_rescale(s->window_duration_opt, inlink->sample_rate,
                                   AV_TIME_BASE);
    s->window_duration = FFMAX(1, s->window_duration);

    s->start_duration = av_rescale(s->start_duration_opt, inlink->sample_rate,
                                   AV_TIME_BASE);
    s->start_silence  = av_rescale(s->start_silence_opt, inlink->sample_rate,
                                   AV_TIME_BASE);
    s->stop_duration = av_rescale(s->stop_duration_opt, inlink->sample_rate,
                                   AV_TIME_BASE);
    s->stop_silence  = av_rescale(s->stop_silence_opt, inlink->sample_rate,
                                   AV_TIME_BASE);

    s->start_found_periods = 0;
    s->stop_found_periods  = 0;

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    SilenceRemoveContext *s = ctx->priv;

    s->start_window = ff_get_audio_buffer(outlink, s->window_duration);
    s->stop_window = ff_get_audio_buffer(outlink, s->window_duration);
    if (!s->start_window || !s->stop_window)
        return AVERROR(ENOMEM);

    s->start_queuef = ff_get_audio_buffer(outlink, s->start_silence + 1);
    s->stop_queuef = ff_get_audio_buffer(outlink, s->stop_silence + 1);
    if (!s->start_queuef || !s->stop_queuef)
        return AVERROR(ENOMEM);

    s->start_cache = av_calloc(outlink->ch_layout.nb_channels, sizeof(*s->start_cache));
    s->stop_cache = av_calloc(outlink->ch_layout.nb_channels, sizeof(*s->stop_cache));
    if (!s->start_cache || !s->stop_cache)
        return AVERROR(ENOMEM);

    clear_windows(s);

    switch (s->detection) {
    case D_PEAK:
        s->compute_flt = compute_peak_flt;
        s->compute_dbl = compute_peak_dbl;
        break;
    case D_RMS:
        s->compute_flt = compute_rms_flt;
        s->compute_dbl = compute_rms_dbl;
        break;
    }

    return 0;
}

static int filter_frame(AVFilterLink *outlink, AVFrame *in)
{
    const int nb_channels = outlink->ch_layout.nb_channels;
    AVFilterContext *ctx = outlink->src;
    SilenceRemoveContext *s = ctx->priv;
    int max_out_nb_samples;
    int out_nb_samples = 0;
    int in_nb_samples;
    const double *srcd;
    const float *srcf;
    AVFrame *out;
    double *dstd;
    float *dstf;

    if (s->next_pts == AV_NOPTS_VALUE)
        s->next_pts = in->pts;

    in_nb_samples = in->nb_samples;
    max_out_nb_samples = in->nb_samples +
                         s->start_silence +
                         s->stop_silence;
    if (max_out_nb_samples <= 0) {
        av_frame_free(&in);
        ff_filter_set_ready(ctx, 100);
        return 0;
    }

    out = ff_get_audio_buffer(outlink, max_out_nb_samples);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }

    out->pts = s->next_pts;

    switch (outlink->format) {
    case AV_SAMPLE_FMT_FLT:
        srcf = (const float *)in->data[0];
        dstf = (float *)out->data[0];
        if (s->start_periods > 0 && s->stop_periods > 0) {
            for (int n = 0; n < in_nb_samples; n++) {
                filter_start_flt(ctx, srcf + n * nb_channels,
                                 dstf, &out_nb_samples,
                                 nb_channels);
            }
            in_nb_samples = out_nb_samples;
            out_nb_samples = 0;
            for (int n = 0; n < in_nb_samples; n++) {
                filter_stop_flt(ctx, dstf + n * nb_channels,
                                dstf, &out_nb_samples,
                                nb_channels);
            }
        } else if (s->start_periods > 0) {
            for (int n = 0; n < in_nb_samples; n++) {
                filter_start_flt(ctx, srcf + n * nb_channels,
                                 dstf, &out_nb_samples,
                                 nb_channels);
            }
        } else if (s->stop_periods > 0) {
            for (int n = 0; n < in_nb_samples; n++) {
                filter_stop_flt(ctx, srcf + n * nb_channels,
                                dstf, &out_nb_samples,
                                nb_channels);
            }
        }
        break;
    case AV_SAMPLE_FMT_DBL:
        srcd = (const double *)in->data[0];
        dstd = (double *)out->data[0];
        if (s->start_periods > 0 && s->stop_periods > 0) {
            for (int n = 0; n < in_nb_samples; n++) {
                filter_start_dbl(ctx, srcd + n * nb_channels,
                                 dstd, &out_nb_samples,
                                 nb_channels);
            }
            in_nb_samples = out_nb_samples;
            out_nb_samples = 0;
            for (int n = 0; n < in_nb_samples; n++) {
                filter_stop_dbl(ctx, dstd + n * nb_channels,
                                dstd, &out_nb_samples,
                                nb_channels);
            }
        } else if (s->start_periods > 0) {
            for (int n = 0; n < in_nb_samples; n++) {
                filter_start_dbl(ctx, srcd + n * nb_channels,
                                 dstd, &out_nb_samples,
                                 nb_channels);
            }
        } else if (s->stop_periods > 0) {
            for (int n = 0; n < in_nb_samples; n++) {
                filter_stop_dbl(ctx, srcd + n * nb_channels,
                                dstd, &out_nb_samples,
                                nb_channels);
            }
        }
        break;
    }

    av_frame_free(&in);
    if (out_nb_samples > 0) {
        s->next_pts += out_nb_samples;
        out->nb_samples = out_nb_samples;
        return ff_filter_frame(outlink, out);
    }

    av_frame_free(&out);
    ff_filter_set_ready(ctx, 100);

    return 0;
}

static int activate(AVFilterContext *ctx)
{
    AVFilterLink *outlink = ctx->outputs[0];
    AVFilterLink *inlink = ctx->inputs[0];
    SilenceRemoveContext *s = ctx->priv;
    AVFrame *in;
    int ret;

    FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink);

    ret = ff_inlink_consume_frame(inlink, &in);
    if (ret < 0)
        return ret;
    if (ret > 0) {
        if (s->start_periods == 0 && s->stop_periods == 0)
            return ff_filter_frame(outlink, in);
        return filter_frame(outlink, in);
    }

    FF_FILTER_FORWARD_STATUS(inlink, outlink);
    FF_FILTER_FORWARD_WANTED(outlink, inlink);

    return FFERROR_NOT_READY;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    SilenceRemoveContext *s = ctx->priv;

    av_frame_free(&s->start_window);
    av_frame_free(&s->stop_window);
    av_frame_free(&s->start_queuef);
    av_frame_free(&s->stop_queuef);
    av_freep(&s->start_cache);
    av_freep(&s->stop_cache);
}

static const AVFilterPad silenceremove_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_input,
    },
};

static const AVFilterPad silenceremove_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_output,
    },
};

const AVFilter ff_af_silenceremove = {
    .name          = "silenceremove",
    .description   = NULL_IF_CONFIG_SMALL("Remove silence."),
    .priv_size     = sizeof(SilenceRemoveContext),
    .priv_class    = &silenceremove_class,
    .init          = init,
    .activate      = activate,
    .uninit        = uninit,
    FILTER_INPUTS(silenceremove_inputs),
    FILTER_OUTPUTS(silenceremove_outputs),
    FILTER_SAMPLEFMTS(AV_SAMPLE_FMT_FLT,
                      AV_SAMPLE_FMT_DBL),
};
