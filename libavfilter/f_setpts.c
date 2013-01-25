/*
 * Copyright (c) 2010 Stefano Sabatini
 * Copyright (c) 2008 Victor Paesa
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
 * video presentation timestamp (PTS) modification filter
 */

#include "libavutil/eval.h"
#include "libavutil/internal.h"
#include "libavutil/mathematics.h"
#include "libavutil/time.h"
#include "avfilter.h"
#include "internal.h"
#include "audio.h"
#include "video.h"

static const char *const var_names[] = {
    "FRAME_RATE",  ///< defined only for constant frame-rate video
    "INTERLACED",  ///< tell if the current frame is interlaced
    "N",           ///< frame number (starting at zero)
    "NB_CONSUMED_SAMPLES", ///< number of samples consumed by the filter (only audio)
    "NB_SAMPLES",  ///< number of samples in the current frame (only audio)
    "POS",         ///< original position in the file of the frame
    "PREV_INPTS",  ///< previous  input PTS
    "PREV_INT",    ///< previous  input time in seconds
    "PREV_OUTPTS", ///< previous output PTS
    "PREV_OUTT",   ///< previous output time in seconds
    "PTS",         ///< original pts in the file of the frame
    "SAMPLE_RATE", ///< sample rate (only audio)
    "STARTPTS",    ///< PTS at start of movie
    "STARTT",      ///< time at start of movie
    "T",           ///< original time in the file of the frame
    "TB",          ///< timebase
    "RTCTIME",     ///< wallclock (RTC) time in micro seconds
    "RTCSTART",    ///< wallclock (RTC) time at the start of the movie in micro seconds
    NULL
};

enum var_name {
    VAR_FRAME_RATE,
    VAR_INTERLACED,
    VAR_N,
    VAR_NB_CONSUMED_SAMPLES,
    VAR_NB_SAMPLES,
    VAR_POS,
    VAR_PREV_INPTS,
    VAR_PREV_INT,
    VAR_PREV_OUTPTS,
    VAR_PREV_OUTT,
    VAR_PTS,
    VAR_SAMPLE_RATE,
    VAR_STARTPTS,
    VAR_STARTT,
    VAR_T,
    VAR_TB,
    VAR_RTCTIME,
    VAR_RTCSTART,
    VAR_VARS_NB
};

typedef struct {
    AVExpr *expr;
    double var_values[VAR_VARS_NB];
    enum AVMediaType type;
} SetPTSContext;

static av_cold int init(AVFilterContext *ctx, const char *args)
{
    SetPTSContext *setpts = ctx->priv;
    int ret;

    if ((ret = av_expr_parse(&setpts->expr, args ? args : "PTS",
                             var_names, NULL, NULL, NULL, NULL, 0, ctx)) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Error while parsing expression '%s'\n", args);
        return ret;
    }

    setpts->var_values[VAR_N          ] = 0.0;
    setpts->var_values[VAR_PREV_INPTS ] = setpts->var_values[VAR_PREV_INT ] = NAN;
    setpts->var_values[VAR_PREV_OUTPTS] = setpts->var_values[VAR_PREV_OUTT] = NAN;
    setpts->var_values[VAR_STARTPTS   ] = setpts->var_values[VAR_STARTT   ] = NAN;
    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    SetPTSContext *setpts = ctx->priv;

    setpts->type = inlink->type;
    setpts->var_values[VAR_TB] = av_q2d(inlink->time_base);
    setpts->var_values[VAR_RTCSTART] = av_gettime();

    setpts->var_values[VAR_SAMPLE_RATE] =
        setpts->type == AVMEDIA_TYPE_AUDIO ? inlink->sample_rate : NAN;

    setpts->var_values[VAR_FRAME_RATE] = inlink->frame_rate.num && inlink->frame_rate.den ?
        av_q2d(inlink->frame_rate) : NAN;

    av_log(inlink->src, AV_LOG_VERBOSE, "TB:%f FRAME_RATE:%f SAMPLE_RATE:%f\n",
           setpts->var_values[VAR_TB],
           setpts->var_values[VAR_FRAME_RATE],
           setpts->var_values[VAR_SAMPLE_RATE]);
    return 0;
}

#define D2TS(d)  (isnan(d) ? AV_NOPTS_VALUE : (int64_t)(d))
#define TS2D(ts) ((ts) == AV_NOPTS_VALUE ? NAN : (double)(ts))
#define TS2T(ts, tb) ((ts) == AV_NOPTS_VALUE ? NAN : (double)(ts)*av_q2d(tb))

#define BUF_SIZE 64

static inline char *double2int64str(char *buf, double v)
{
    if (isnan(v)) snprintf(buf, BUF_SIZE, "nan");
    else          snprintf(buf, BUF_SIZE, "%"PRId64, (int64_t)v);
    return buf;
}

#define d2istr(v) double2int64str((char[BUF_SIZE]){0}, v)

static int filter_frame(AVFilterLink *inlink, AVFilterBufferRef *frame)
{
    SetPTSContext *setpts = inlink->dst->priv;
    int64_t in_pts = frame->pts;
    double d;

    if (isnan(setpts->var_values[VAR_STARTPTS])) {
        setpts->var_values[VAR_STARTPTS] = TS2D(frame->pts);
        setpts->var_values[VAR_STARTT  ] = TS2T(frame->pts, inlink->time_base);
    }
    setpts->var_values[VAR_PTS       ] = TS2D(frame->pts);
    setpts->var_values[VAR_T         ] = TS2T(frame->pts, inlink->time_base);
    setpts->var_values[VAR_POS       ] = frame->pos == -1 ? NAN : frame->pos;
    setpts->var_values[VAR_RTCTIME   ] = av_gettime();

    switch (inlink->type) {
    case AVMEDIA_TYPE_VIDEO:
        setpts->var_values[VAR_INTERLACED] = frame->video->interlaced;
        break;

    case AVMEDIA_TYPE_AUDIO:
        setpts->var_values[VAR_NB_SAMPLES] = frame->audio->nb_samples;
        break;
    }

    d = av_expr_eval(setpts->expr, setpts->var_values, NULL);

    av_log(inlink->dst, AV_LOG_DEBUG,
           "N:%"PRId64" PTS:%s T:%f POS:%s",
           (int64_t)setpts->var_values[VAR_N],
           d2istr(setpts->var_values[VAR_PTS]),
           setpts->var_values[VAR_T],
           d2istr(setpts->var_values[VAR_POS]));
    switch (inlink->type) {
    case AVMEDIA_TYPE_VIDEO:
        av_log(inlink->dst, AV_LOG_DEBUG, " INTERLACED:%"PRId64,
               (int64_t)setpts->var_values[VAR_INTERLACED]);
        break;
    case AVMEDIA_TYPE_AUDIO:
        av_log(inlink->dst, AV_LOG_DEBUG, " NB_SAMPLES:%"PRId64" NB_CONSUMED_SAMPLES:%"PRId64,
               (int64_t)setpts->var_values[VAR_NB_SAMPLES],
               (int64_t)setpts->var_values[VAR_NB_CONSUMED_SAMPLES]);
        break;
    }
    av_log(inlink->dst, AV_LOG_DEBUG, " -> PTS:%s T:%f\n", d2istr(d), TS2T(d, inlink->time_base));

    frame->pts = D2TS(d);

    setpts->var_values[VAR_PREV_INPTS ] = TS2D(in_pts);
    setpts->var_values[VAR_PREV_INT   ] = TS2T(in_pts, inlink->time_base);
    setpts->var_values[VAR_PREV_OUTPTS] = TS2D(frame->pts);
    setpts->var_values[VAR_PREV_OUTT]   = TS2T(frame->pts, inlink->time_base);
    setpts->var_values[VAR_N] += 1.0;
    if (setpts->type == AVMEDIA_TYPE_AUDIO) {
        setpts->var_values[VAR_NB_CONSUMED_SAMPLES] += frame->audio->nb_samples;
    }
    return ff_filter_frame(inlink->dst->outputs[0], frame);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    SetPTSContext *setpts = ctx->priv;
    av_expr_free(setpts->expr);
    setpts->expr = NULL;
}

#if CONFIG_ASETPTS_FILTER
static const AVFilterPad avfilter_af_asetpts_inputs[] = {
    {
        .name             = "default",
        .type             = AVMEDIA_TYPE_AUDIO,
        .get_audio_buffer = ff_null_get_audio_buffer,
        .config_props     = config_input,
        .filter_frame     = filter_frame,
    },
    { NULL }
};

static const AVFilterPad avfilter_af_asetpts_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_AUDIO,
    },
    { NULL }
};

AVFilter avfilter_af_asetpts = {
    .name      = "asetpts",
    .description = NULL_IF_CONFIG_SMALL("Set PTS for the output audio frame."),
    .init      = init,
    .uninit    = uninit,
    .priv_size = sizeof(SetPTSContext),
    .inputs    = avfilter_af_asetpts_inputs,
    .outputs   = avfilter_af_asetpts_outputs,
};
#endif /* CONFIG_ASETPTS_FILTER */

#if CONFIG_SETPTS_FILTER
static const AVFilterPad avfilter_vf_setpts_inputs[] = {
    {
        .name             = "default",
        .type             = AVMEDIA_TYPE_VIDEO,
        .get_video_buffer = ff_null_get_video_buffer,
        .config_props     = config_input,
        .filter_frame     = filter_frame,
    },
    { NULL }
};

static const AVFilterPad avfilter_vf_setpts_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter avfilter_vf_setpts = {
    .name      = "setpts",
    .description = NULL_IF_CONFIG_SMALL("Set PTS for the output video frame."),
    .init      = init,
    .uninit    = uninit,

    .priv_size = sizeof(SetPTSContext),

    .inputs    = avfilter_vf_setpts_inputs,
    .outputs   = avfilter_vf_setpts_outputs,
};
#endif /* CONFIG_SETPTS_FILTER */
