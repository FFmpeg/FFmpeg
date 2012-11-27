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

/* #define DEBUG */

#include "libavutil/eval.h"
#include "libavutil/internal.h"
#include "libavutil/mathematics.h"
#include "avfilter.h"
#include "internal.h"
#include "video.h"

static const char *const var_names[] = {
    "E",           ///< Euler number
    "INTERLACED",  ///< tell if the current frame is interlaced
    "N",           ///< frame number (starting at zero)
    "PHI",         ///< golden ratio
    "PI",          ///< greek pi
    "POS",         ///< original position in the file of the frame
    "PREV_INPTS",  ///< previous  input PTS
    "PREV_OUTPTS", ///< previous output PTS
    "PTS",         ///< original pts in the file of the frame
    "STARTPTS",   ///< PTS at start of movie
    "TB",          ///< timebase
    NULL
};

enum var_name {
    VAR_E,
    VAR_INTERLACED,
    VAR_N,
    VAR_PHI,
    VAR_PI,
    VAR_POS,
    VAR_PREV_INPTS,
    VAR_PREV_OUTPTS,
    VAR_PTS,
    VAR_STARTPTS,
    VAR_TB,
    VAR_VARS_NB
};

typedef struct {
    AVExpr *expr;
    double var_values[VAR_VARS_NB];
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

    setpts->var_values[VAR_E          ] = M_E;
    setpts->var_values[VAR_N          ] = 0.0;
    setpts->var_values[VAR_PHI        ] = M_PHI;
    setpts->var_values[VAR_PI         ] = M_PI;
    setpts->var_values[VAR_PREV_INPTS ] = NAN;
    setpts->var_values[VAR_PREV_OUTPTS] = NAN;
    setpts->var_values[VAR_STARTPTS   ] = NAN;
    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    SetPTSContext *setpts = inlink->dst->priv;

    setpts->var_values[VAR_TB] = av_q2d(inlink->time_base);

    av_log(inlink->src, AV_LOG_VERBOSE, "TB:%f\n", setpts->var_values[VAR_TB]);
    return 0;
}

#define D2TS(d)  (isnan(d) ? AV_NOPTS_VALUE : (int64_t)(d))
#define TS2D(ts) ((ts) == AV_NOPTS_VALUE ? NAN : (double)(ts))

static int filter_frame(AVFilterLink *inlink, AVFilterBufferRef *frame)
{
    SetPTSContext *setpts = inlink->dst->priv;
    int64_t in_pts = frame->pts;
    double d;

    if (isnan(setpts->var_values[VAR_STARTPTS]))
        setpts->var_values[VAR_STARTPTS] = TS2D(frame->pts);

    setpts->var_values[VAR_INTERLACED] = frame->video->interlaced;
    setpts->var_values[VAR_PTS       ] = TS2D(frame->pts);
    setpts->var_values[VAR_POS       ] = frame->pos == -1 ? NAN : frame->pos;

    d = av_expr_eval(setpts->expr, setpts->var_values, NULL);
    frame->pts = D2TS(d);

#ifdef DEBUG
    av_log(inlink->dst, AV_LOG_DEBUG,
           "n:%"PRId64" interlaced:%d pos:%"PRId64" pts:%"PRId64" t:%f -> pts:%"PRId64" t:%f\n",
           (int64_t)setpts->var_values[VAR_N],
           (int)setpts->var_values[VAR_INTERLACED],
           frame->pos, in_pts, in_pts * av_q2d(inlink->time_base),
           frame->pts, frame->pts * av_q2d(inlink->time_base));
#endif


    setpts->var_values[VAR_N] += 1.0;
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
