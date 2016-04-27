/*
 * Copyright (c) 2010 Stefano Sabatini
 * Copyright (c) 2008 Victor Paesa
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * video presentation timestamp (PTS) modification filter
 */

#include <inttypes.h>

#include "libavutil/eval.h"
#include "libavutil/internal.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"

#include "audio.h"
#include "avfilter.h"
#include "internal.h"
#include "video.h"

#include "config.h"

static const char *const var_names[] = {
    "E",           ///< Euler number
    "FRAME_RATE",  ///< defined only for constant frame-rate video
    "INTERLACED",  ///< tell if the current frame is interlaced
    "N",           ///< frame / sample number (starting at zero)
    "PHI",         ///< golden ratio
    "PI",          ///< Greek pi
    "PREV_INPTS",  ///< previous  input PTS
    "PREV_OUTPTS", ///< previous output PTS
    "PTS",         ///< original pts in the file of the frame
    "STARTPTS",    ///< PTS at start of movie
    "TB",          ///< timebase
    "RTCTIME",     ///< wallclock (RTC) time in micro seconds
    "RTCSTART",    ///< wallclock (RTC) time at the start of the movie in micro seconds
    "S",           //   Number of samples in the current frame
    "SR",          //   Audio sample rate
    NULL
};

enum var_name {
    VAR_E,
    VAR_FRAME_RATE,
    VAR_INTERLACED,
    VAR_N,
    VAR_PHI,
    VAR_PI,
    VAR_PREV_INPTS,
    VAR_PREV_OUTPTS,
    VAR_PTS,
    VAR_STARTPTS,
    VAR_TB,
    VAR_RTCTIME,
    VAR_RTCSTART,
    VAR_S,
    VAR_SR,
    VAR_VARS_NB
};

typedef struct SetPTSContext {
    const AVClass *class;
    char *expr_str;
    AVExpr *expr;
    double var_values[VAR_VARS_NB];
} SetPTSContext;

static av_cold int init(AVFilterContext *ctx)
{
    SetPTSContext *setpts = ctx->priv;
    int ret;

    if ((ret = av_expr_parse(&setpts->expr, setpts->expr_str,
                             var_names, NULL, NULL, NULL, NULL, 0, ctx)) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Error while parsing expression '%s'\n", setpts->expr_str);
        return ret;
    }

    setpts->var_values[VAR_E]           = M_E;
    setpts->var_values[VAR_N]           = 0.0;
    setpts->var_values[VAR_S]           = 0.0;
    setpts->var_values[VAR_PHI]         = M_PHI;
    setpts->var_values[VAR_PI]          = M_PI;
    setpts->var_values[VAR_PREV_INPTS]  = NAN;
    setpts->var_values[VAR_PREV_OUTPTS] = NAN;
    setpts->var_values[VAR_STARTPTS]    = NAN;
    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    SetPTSContext *setpts = inlink->dst->priv;

    setpts->var_values[VAR_TB] = av_q2d(inlink->time_base);
    setpts->var_values[VAR_RTCSTART] = av_gettime();

    if (inlink->type == AVMEDIA_TYPE_AUDIO) {
        setpts->var_values[VAR_SR] = inlink->sample_rate;
    }

    setpts->var_values[VAR_FRAME_RATE] = inlink->frame_rate.num &&
                                         inlink->frame_rate.den ?
                                            av_q2d(inlink->frame_rate) : NAN;

    // Indicate the output can be variable framerate.
    inlink->frame_rate = (AVRational){1, 0};

    av_log(inlink->src, AV_LOG_VERBOSE, "TB:%f\n", setpts->var_values[VAR_TB]);
    return 0;
}

#define D2TS(d)  (isnan(d) ? AV_NOPTS_VALUE : (int64_t)(d))
#define TS2D(ts) ((ts) == AV_NOPTS_VALUE ? NAN : (double)(ts))

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    SetPTSContext *setpts = inlink->dst->priv;
    int64_t in_pts = frame->pts;
    double d;

    if (isnan(setpts->var_values[VAR_STARTPTS]))
        setpts->var_values[VAR_STARTPTS] = TS2D(frame->pts);

    setpts->var_values[VAR_PTS       ] = TS2D(frame->pts);
    setpts->var_values[VAR_RTCTIME   ] = av_gettime();

    if (inlink->type == AVMEDIA_TYPE_VIDEO) {
        setpts->var_values[VAR_INTERLACED] = frame->interlaced_frame;
    } else {
        setpts->var_values[VAR_S] = frame->nb_samples;
    }

    d = av_expr_eval(setpts->expr, setpts->var_values, NULL);
    frame->pts = D2TS(d);

    av_log(inlink->dst, AV_LOG_TRACE,
            "n:%"PRId64" interlaced:%d pts:%"PRId64" t:%f -> pts:%"PRId64" t:%f\n",
            (int64_t)setpts->var_values[VAR_N],
            (int)setpts->var_values[VAR_INTERLACED],
            in_pts, in_pts * av_q2d(inlink->time_base),
            frame->pts, frame->pts * av_q2d(inlink->time_base));

    if (inlink->type == AVMEDIA_TYPE_VIDEO) {
        setpts->var_values[VAR_N] += 1.0;
    } else {
        setpts->var_values[VAR_N] += frame->nb_samples;
    }

    setpts->var_values[VAR_PREV_INPTS ] = TS2D(in_pts);
    setpts->var_values[VAR_PREV_OUTPTS] = TS2D(frame->pts);
    return ff_filter_frame(inlink->dst->outputs[0], frame);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    SetPTSContext *setpts = ctx->priv;
    av_expr_free(setpts->expr);
    setpts->expr = NULL;
}

#define OFFSET(x) offsetof(SetPTSContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_AUDIO_PARAM
static const AVOption options[] = {
    { "expr", "Expression determining the frame timestamp", OFFSET(expr_str), AV_OPT_TYPE_STRING, { .str = "PTS" }, .flags = FLAGS },
    { NULL },
};

#if CONFIG_SETPTS_FILTER
static const AVClass setpts_class = {
    .class_name = "setpts",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

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

AVFilter ff_vf_setpts = {
    .name      = "setpts",
    .description = NULL_IF_CONFIG_SMALL("Set PTS for the output video frame."),
    .init      = init,
    .uninit    = uninit,

    .priv_size = sizeof(SetPTSContext),
    .priv_class = &setpts_class,

    .inputs    = avfilter_vf_setpts_inputs,
    .outputs   = avfilter_vf_setpts_outputs,
};
#endif

#if CONFIG_ASETPTS_FILTER
static const AVClass asetpts_class = {
    .class_name = "asetpts",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVFilterPad asetpts_inputs[] = {
    {
        .name             = "default",
        .type             = AVMEDIA_TYPE_AUDIO,
        .get_audio_buffer = ff_null_get_audio_buffer,
        .config_props     = config_input,
        .filter_frame     = filter_frame,
    },
    { NULL }
};

static const AVFilterPad asetpts_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_AUDIO,
    },
    { NULL }
};

AVFilter ff_af_asetpts = {
    .name        = "asetpts",
    .description = NULL_IF_CONFIG_SMALL("Set PTS for the output audio frame."),
    .init        = init,
    .uninit      = uninit,

    .priv_size  = sizeof(SetPTSContext),
    .priv_class = &asetpts_class,

    .inputs    = asetpts_inputs,
    .outputs   = asetpts_outputs,
};
#endif
