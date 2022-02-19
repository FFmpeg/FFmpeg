/*
 * Copyright 2007 Bobby Bingham
 * Copyright 2012 Robert Nagy <ronag89 gmail com>
 * Copyright 2012 Anton Khirnov <anton khirnov net>
 * Copyright 2018 Calvin Walton <calvin.walton@kepstin.ca>
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
 * a filter enforcing given constant framerate
 */

#include <float.h>
#include <stdint.h>

#include "libavutil/avassert.h"
#include "libavutil/eval.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "filters.h"
#include "internal.h"

enum EOFAction {
    EOF_ACTION_ROUND,
    EOF_ACTION_PASS,
    EOF_ACTION_NB
};

static const char *const var_names[] = {
  "source_fps",
  "ntsc",
  "pal",
  "film",
  "ntsc_film",
  NULL
};

enum var_name {
  VAR_SOURCE_FPS,
  VAR_FPS_NTSC,
  VAR_FPS_PAL,
  VAR_FPS_FILM,
  VAR_FPS_NTSC_FILM,
  VARS_NB
};

static const double ntsc_fps = 30000.0 / 1001.0;
static const double pal_fps = 25.0;
static const double film_fps = 24.0;
static const double ntsc_film_fps = 24000.0 / 1001.0;

typedef struct FPSContext {
    const AVClass *class;

    double start_time;      ///< pts, in seconds, of the expected first frame

    char *framerate;        ///< expression that defines the target framerate
    int rounding;           ///< AVRounding method for timestamps
    int eof_action;         ///< action performed for last frame in FIFO

    /* Set during outlink configuration */
    int64_t  in_pts_off;    ///< input frame pts offset for start_time handling
    int64_t  out_pts_off;   ///< output frame pts offset for start_time handling

    /* Runtime state */
    int      status;        ///< buffered input status
    int64_t  status_pts;    ///< buffered input status timestamp

    AVFrame *frames[2];     ///< buffered frames
    int      frames_count;  ///< number of buffered frames

    int64_t  next_pts;      ///< pts of the next frame to output

    /* statistics */
    int cur_frame_out;         ///< number of times current frame has been output
    int frames_in;             ///< number of frames on input
    int frames_out;            ///< number of frames on output
    int dup;                   ///< number of frames duplicated
    int drop;                  ///< number of framed dropped
} FPSContext;

#define OFFSET(x) offsetof(FPSContext, x)
#define V AV_OPT_FLAG_VIDEO_PARAM
#define F AV_OPT_FLAG_FILTERING_PARAM
static const AVOption fps_options[] = {
    { "fps", "A string describing desired output framerate", OFFSET(framerate), AV_OPT_TYPE_STRING, { .str = "25" }, 0, 0, V|F },
    { "start_time", "Assume the first PTS should be this value.", OFFSET(start_time), AV_OPT_TYPE_DOUBLE, { .dbl = DBL_MAX}, -DBL_MAX, DBL_MAX, V|F },
    { "round", "set rounding method for timestamps", OFFSET(rounding), AV_OPT_TYPE_INT, { .i64 = AV_ROUND_NEAR_INF }, 0, 5, V|F, "round" },
        { "zero", "round towards 0",                 0, AV_OPT_TYPE_CONST, { .i64 = AV_ROUND_ZERO     }, 0, 0, V|F, "round" },
        { "inf",  "round away from 0",               0, AV_OPT_TYPE_CONST, { .i64 = AV_ROUND_INF      }, 0, 0, V|F, "round" },
        { "down", "round towards -infty",            0, AV_OPT_TYPE_CONST, { .i64 = AV_ROUND_DOWN     }, 0, 0, V|F, "round" },
        { "up",   "round towards +infty",            0, AV_OPT_TYPE_CONST, { .i64 = AV_ROUND_UP       }, 0, 0, V|F, "round" },
        { "near", "round to nearest",                0, AV_OPT_TYPE_CONST, { .i64 = AV_ROUND_NEAR_INF }, 0, 0, V|F, "round" },
    { "eof_action", "action performed for last frame", OFFSET(eof_action), AV_OPT_TYPE_INT, { .i64 = EOF_ACTION_ROUND }, 0, EOF_ACTION_NB-1, V|F, "eof_action" },
        { "round", "round similar to other frames",  0, AV_OPT_TYPE_CONST, { .i64 = EOF_ACTION_ROUND }, 0, 0, V|F, "eof_action" },
        { "pass",  "pass through last frame",        0, AV_OPT_TYPE_CONST, { .i64 = EOF_ACTION_PASS  }, 0, 0, V|F, "eof_action" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(fps);

static av_cold int init(AVFilterContext *ctx)
{
    FPSContext *s = ctx->priv;

    s->status_pts   = AV_NOPTS_VALUE;
    s->next_pts     = AV_NOPTS_VALUE;

    return 0;
}

/* Remove the first frame from the buffer, returning it */
static AVFrame *shift_frame(AVFilterContext *ctx, FPSContext *s)
{
    AVFrame *frame;

    /* Must only be called when there are frames in the buffer */
    av_assert1(s->frames_count > 0);

    frame = s->frames[0];
    s->frames[0] = s->frames[1];
    s->frames[1] = NULL;
    s->frames_count--;

    /* Update statistics counters */
    s->frames_out += s->cur_frame_out;
    if (s->cur_frame_out > 1) {
        av_log(ctx, AV_LOG_DEBUG, "Duplicated frame with pts %"PRId64" %d times\n",
               frame->pts, s->cur_frame_out - 1);
        s->dup += s->cur_frame_out - 1;
    } else if (s->cur_frame_out == 0) {
        av_log(ctx, AV_LOG_DEBUG, "Dropping frame with pts %"PRId64"\n",
               frame->pts);
        s->drop++;
    }
    s->cur_frame_out = 0;

    return frame;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    FPSContext *s = ctx->priv;

    AVFrame *frame;

    while (s->frames_count > 0) {
        frame = shift_frame(ctx, s);
        av_frame_free(&frame);
    }

    av_log(ctx, AV_LOG_VERBOSE, "%d frames in, %d frames out; %d frames dropped, "
           "%d frames duplicated.\n", s->frames_in, s->frames_out, s->drop, s->dup);
}

static int config_props(AVFilterLink* outlink)
{
    AVFilterContext *ctx    = outlink->src;
    AVFilterLink    *inlink = ctx->inputs[0];
    FPSContext      *s      = ctx->priv;

    double var_values[VARS_NB], res;
    int ret;

    var_values[VAR_SOURCE_FPS]    = av_q2d(inlink->frame_rate);
    var_values[VAR_FPS_NTSC]      = ntsc_fps;
    var_values[VAR_FPS_PAL]       = pal_fps;
    var_values[VAR_FPS_FILM]      = film_fps;
    var_values[VAR_FPS_NTSC_FILM] = ntsc_film_fps;
    ret = av_expr_parse_and_eval(&res, s->framerate,
                                 var_names, var_values,
                                 NULL, NULL, NULL, NULL, NULL, 0, ctx);
    if (ret < 0)
        return ret;

    outlink->frame_rate = av_d2q(res, INT_MAX);
    outlink->time_base  = av_inv_q(outlink->frame_rate);

    /* Calculate the input and output pts offsets for start_time */
    if (s->start_time != DBL_MAX && s->start_time != AV_NOPTS_VALUE) {
        double first_pts = s->start_time * AV_TIME_BASE;
        if (first_pts < INT64_MIN || first_pts > INT64_MAX) {
            av_log(ctx, AV_LOG_ERROR, "Start time %f cannot be represented in internal time base\n",
                   s->start_time);
            return AVERROR(EINVAL);
        }
        s->in_pts_off  = av_rescale_q_rnd(first_pts, AV_TIME_BASE_Q, inlink->time_base,
                                          s->rounding | AV_ROUND_PASS_MINMAX);
        s->out_pts_off = av_rescale_q_rnd(first_pts, AV_TIME_BASE_Q, outlink->time_base,
                                          s->rounding | AV_ROUND_PASS_MINMAX);
        s->next_pts = s->out_pts_off;
        av_log(ctx, AV_LOG_VERBOSE, "Set first pts to (in:%"PRId64" out:%"PRId64") from start time %f\n",
               s->in_pts_off, s->out_pts_off, s->start_time);
    }

    av_log(ctx, AV_LOG_VERBOSE, "fps=%d/%d\n", outlink->frame_rate.num, outlink->frame_rate.den);

    return 0;
}

/* Read a frame from the input and save it in the buffer */
static int read_frame(AVFilterContext *ctx, FPSContext *s, AVFilterLink *inlink, AVFilterLink *outlink)
{
    AVFrame *frame;
    int ret;
    int64_t in_pts;

    /* Must only be called when we have buffer room available */
    av_assert1(s->frames_count < 2);

    ret = ff_inlink_consume_frame(inlink, &frame);
    /* Caller must have run ff_inlink_check_available_frame first */
    av_assert1(ret);
    if (ret < 0)
        return ret;

    /* Convert frame pts to output timebase.
     * The dance with offsets is required to match the rounding behaviour of the
     * previous version of the fps filter when using the start_time option. */
    in_pts = frame->pts;
    frame->pts = s->out_pts_off + av_rescale_q_rnd(in_pts - s->in_pts_off,
                                                   inlink->time_base, outlink->time_base,
                                                   s->rounding | AV_ROUND_PASS_MINMAX);

    av_log(ctx, AV_LOG_DEBUG, "Read frame with in pts %"PRId64", out pts %"PRId64"\n",
           in_pts, frame->pts);

    s->frames[s->frames_count++] = frame;
    s->frames_in++;

    return 1;
}

/* Write a frame to the output */
static int write_frame(AVFilterContext *ctx, FPSContext *s, AVFilterLink *outlink, int *again)
{
    AVFrame *frame;

    av_assert1(s->frames_count == 2 || (s->status && s->frames_count == 1));

    /* We haven't yet determined the pts of the first frame */
    if (s->next_pts == AV_NOPTS_VALUE) {
        if (s->frames[0]->pts != AV_NOPTS_VALUE) {
            s->next_pts = s->frames[0]->pts;
            av_log(ctx, AV_LOG_VERBOSE, "Set first pts to %"PRId64"\n", s->next_pts);
        } else {
            av_log(ctx, AV_LOG_WARNING, "Discarding initial frame(s) with no "
                   "timestamp.\n");
            frame = shift_frame(ctx, s);
            av_frame_free(&frame);
            *again = 1;
            return 0;
        }
    }

    /* There are two conditions where we want to drop a frame:
     * - If we have two buffered frames and the second frame is acceptable
     *   as the next output frame, then drop the first buffered frame.
     * - If we have status (EOF) set, drop frames when we hit the
     *   status timestamp. */
    if ((s->frames_count == 2 && s->frames[1]->pts <= s->next_pts) ||
        (s->status            && s->status_pts     <= s->next_pts)) {

        frame = shift_frame(ctx, s);
        av_frame_free(&frame);
        *again = 1;
        return 0;

    /* Output a copy of the first buffered frame */
    } else {
        frame = av_frame_clone(s->frames[0]);
        if (!frame)
            return AVERROR(ENOMEM);
        // Make sure Closed Captions will not be duplicated
        av_frame_remove_side_data(s->frames[0], AV_FRAME_DATA_A53_CC);
        frame->pts = s->next_pts++;

        av_log(ctx, AV_LOG_DEBUG, "Writing frame with pts %"PRId64" to pts %"PRId64"\n",
               s->frames[0]->pts, frame->pts);
        s->cur_frame_out++;
        *again = 1;
        return ff_filter_frame(outlink, frame);
    }
}

/* Convert status_pts to outlink timebase */
static void update_eof_pts(AVFilterContext *ctx, FPSContext *s, AVFilterLink *inlink, AVFilterLink *outlink, int64_t status_pts)
{
    int eof_rounding = (s->eof_action == EOF_ACTION_PASS) ? AV_ROUND_UP : s->rounding;
    s->status_pts = av_rescale_q_rnd(status_pts, inlink->time_base, outlink->time_base,
                                     eof_rounding | AV_ROUND_PASS_MINMAX);

    av_log(ctx, AV_LOG_DEBUG, "EOF is at pts %"PRId64"\n", s->status_pts);
}

static int activate(AVFilterContext *ctx)
{
    FPSContext   *s       = ctx->priv;
    AVFilterLink *inlink  = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];

    int ret;
    int again = 0;
    int64_t status_pts;

    FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink);

    /* No buffered status: normal operation */
    if (!s->status) {

        /* Read available input frames if we have room */
        while (s->frames_count < 2 && ff_inlink_check_available_frame(inlink)) {
            ret = read_frame(ctx, s, inlink, outlink);
            if (ret < 0)
                return ret;
        }

        /* We do not yet have enough frames to produce output */
        if (s->frames_count < 2) {
            /* Check if we've hit EOF (or otherwise that an error status is set) */
            ret = ff_inlink_acknowledge_status(inlink, &s->status, &status_pts);
            if (ret > 0)
                update_eof_pts(ctx, s, inlink, outlink, status_pts);

            if (!ret) {
                /* If someone wants us to output, we'd better ask for more input */
                FF_FILTER_FORWARD_WANTED(outlink, inlink);
                return 0;
            }
        }
    }

    /* Buffered frames are available, so generate an output frame */
    if (s->frames_count > 0) {
        ret = write_frame(ctx, s, outlink, &again);
        /* Couldn't generate a frame, so schedule us to perform another step */
        if (again && ff_inoutlink_check_flow(inlink, outlink))
            ff_filter_set_ready(ctx, 100);
        return ret;
    }

    /* No frames left, so forward the status */
    if (s->status && s->frames_count == 0) {
        ff_outlink_set_status(outlink, s->status, s->next_pts);
        return 0;
    }

    return FFERROR_NOT_READY;
}

static const AVFilterPad avfilter_vf_fps_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
    },
};

static const AVFilterPad avfilter_vf_fps_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_props,
    },
};

const AVFilter ff_vf_fps = {
    .name        = "fps",
    .description = NULL_IF_CONFIG_SMALL("Force constant framerate."),
    .init        = init,
    .uninit      = uninit,
    .priv_size   = sizeof(FPSContext),
    .priv_class  = &fps_class,
    .activate    = activate,
    .flags       = AVFILTER_FLAG_METADATA_ONLY,
    FILTER_INPUTS(avfilter_vf_fps_inputs),
    FILTER_OUTPUTS(avfilter_vf_fps_outputs),
};
