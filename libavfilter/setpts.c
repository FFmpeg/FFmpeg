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

#include "config_components.h"

#include <inttypes.h>

#include "libavutil/eval.h"
#include "libavutil/internal.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"
#include "audio.h"
#include "avfilter.h"
#include "filters.h"
#include "video.h"

static const char *const var_names[] = {
    "FRAME_RATE",  ///< defined only for constant frame-rate video
    "INTERLACED",  ///< tell if the current frame is interlaced
    "N",           ///< frame / sample number (starting at zero)
    "NB_CONSUMED_SAMPLES", ///< number of samples consumed by the filter (only audio)
    "NB_SAMPLES",  ///< number of samples in the current frame (only audio)
#if FF_API_FRAME_PKT
    "POS",         ///< original position in the file of the frame
#endif
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
    "S",           //   Number of samples in the current frame
    "SR",          //   Audio sample rate
    "FR",          ///< defined only for constant frame-rate video
    "T_CHANGE",    ///< time of first frame after latest command was applied
    NULL
};

enum var_name {
    VAR_FRAME_RATE,
    VAR_INTERLACED,
    VAR_N,
    VAR_NB_CONSUMED_SAMPLES,
    VAR_NB_SAMPLES,
#if FF_API_FRAME_PKT
    VAR_POS,
#endif
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
    VAR_S,
    VAR_SR,
    VAR_FR,
    VAR_T_CHANGE,
    VAR_VARS_NB,
};

typedef struct SetPTSContext {
    const AVClass *class;
    char *expr_str;
    AVExpr *expr;
    double var_values[VAR_VARS_NB];
    enum AVMediaType type;
} SetPTSContext;

#define V(name_) \
    setpts->var_values[VAR_##name_]

static av_cold int init(AVFilterContext *ctx)
{
    SetPTSContext *setpts = ctx->priv;
    int ret;

    if ((ret = av_expr_parse(&setpts->expr, setpts->expr_str,
                             var_names, NULL, NULL, NULL, NULL, 0, ctx)) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Error while parsing expression '%s'\n", setpts->expr_str);
        return ret;
    }

    V(N)           = 0.0;
    V(S)           = 0.0;
    V(PREV_INPTS)  = NAN;
    V(PREV_INT)    = NAN;
    V(PREV_OUTPTS) = NAN;
    V(PREV_OUTT)   = NAN;
    V(STARTPTS)    = NAN;
    V(STARTT)      = NAN;
    V(T_CHANGE)    = NAN;
    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    FilterLink *l = ff_filter_link(inlink);
    AVFilterContext *ctx = inlink->dst;
    SetPTSContext *setpts = ctx->priv;

    setpts->type = inlink->type;
    V(TB) = av_q2d(inlink->time_base);
    V(RTCSTART) = av_gettime();

    V(SR) = V(SAMPLE_RATE) =
        setpts->type == AVMEDIA_TYPE_AUDIO ? inlink->sample_rate : NAN;

    V(FRAME_RATE) = V(FR) =
        l->frame_rate.num && l->frame_rate.den ?
        av_q2d(l->frame_rate) : NAN;

    av_log(inlink->src, AV_LOG_VERBOSE, "TB:%f FRAME_RATE:%f SAMPLE_RATE:%f\n",
           V(TB), V(FRAME_RATE), V(SAMPLE_RATE));
    return 0;
}

static int config_output_video(AVFilterLink *outlink)
{
    FilterLink *l = ff_filter_link(outlink);

    l->frame_rate = (AVRational){ 1, 0 };

    return 0;
}

#define BUF_SIZE 64

static inline char *double2int64str(char *buf, double v)
{
    if (isnan(v)) snprintf(buf, BUF_SIZE, "nan");
    else          snprintf(buf, BUF_SIZE, "%"PRId64, (int64_t)v);
    return buf;
}

static double eval_pts(SetPTSContext *setpts, AVFilterLink *inlink, AVFrame *frame, int64_t pts)
{
    if (isnan(V(STARTPTS))) {
        V(STARTPTS) = TS2D(pts);
        V(STARTT  ) = TS2T(pts, inlink->time_base);
    }
    if (isnan(V(T_CHANGE))) {
        V(T_CHANGE) = TS2T(pts, inlink->time_base);
    }
    V(PTS       ) = TS2D(pts);
    V(T         ) = TS2T(pts, inlink->time_base);
#if FF_API_FRAME_PKT
FF_DISABLE_DEPRECATION_WARNINGS
    V(POS       ) = !frame || frame->pkt_pos == -1 ? NAN : frame->pkt_pos;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
    V(RTCTIME   ) = av_gettime();

    if (frame) {
        if (inlink->type == AVMEDIA_TYPE_VIDEO) {
            V(INTERLACED) = !!(frame->flags & AV_FRAME_FLAG_INTERLACED);
        } else if (inlink->type == AVMEDIA_TYPE_AUDIO) {
            V(S) = frame->nb_samples;
            V(NB_SAMPLES) = frame->nb_samples;
        }
    }

    return av_expr_eval(setpts->expr, setpts->var_values, NULL);
}
#define d2istr(v) double2int64str((char[BUF_SIZE]){0}, v)

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    SetPTSContext *setpts = inlink->dst->priv;
    int64_t in_pts = frame->pts;
    double d;

    d = eval_pts(setpts, inlink, frame, frame->pts);
    frame->pts = D2TS(d);
    frame->duration = 0;

    av_log(inlink->dst, AV_LOG_TRACE,
            "N:%"PRId64" PTS:%s T:%f",
           (int64_t)V(N), d2istr(V(PTS)), V(T));
    switch (inlink->type) {
    case AVMEDIA_TYPE_VIDEO:
        av_log(inlink->dst, AV_LOG_TRACE, " INTERLACED:%"PRId64,
                (int64_t)V(INTERLACED));
        break;
    case AVMEDIA_TYPE_AUDIO:
        av_log(inlink->dst, AV_LOG_TRACE, " NB_SAMPLES:%"PRId64" NB_CONSUMED_SAMPLES:%"PRId64,
                (int64_t)V(NB_SAMPLES),
                (int64_t)V(NB_CONSUMED_SAMPLES));
        break;
    }
    av_log(inlink->dst, AV_LOG_TRACE, " -> PTS:%s T:%f\n", d2istr(d), TS2T(d, inlink->time_base));

    if (inlink->type == AVMEDIA_TYPE_VIDEO) {
        V(N) += 1.0;
    } else {
        V(N) += frame->nb_samples;
    }

    V(PREV_INPTS ) = TS2D(in_pts);
    V(PREV_INT   ) = TS2T(in_pts, inlink->time_base);
    V(PREV_OUTPTS) = TS2D(frame->pts);
    V(PREV_OUTT)   = TS2T(frame->pts, inlink->time_base);
    if (setpts->type == AVMEDIA_TYPE_AUDIO) {
        V(NB_CONSUMED_SAMPLES) += frame->nb_samples;
    }
    return ff_filter_frame(inlink->dst->outputs[0], frame);
}

static int activate(AVFilterContext *ctx)
{
    SetPTSContext *setpts = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *in;
    int status;
    int64_t pts;
    int ret;

    FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink);

    ret = ff_inlink_consume_frame(inlink, &in);
    if (ret < 0)
        return ret;
    if (ret > 0)
        return filter_frame(inlink, in);

    if (ff_inlink_acknowledge_status(inlink, &status, &pts)) {
        double d = eval_pts(setpts, inlink, NULL, pts);

        av_log(ctx, AV_LOG_TRACE, "N:EOF PTS:%s T:%f -> PTS:%s T:%f\n",
               d2istr(V(PTS)), V(T), d2istr(d), TS2T(d, inlink->time_base));
        ff_outlink_set_status(outlink, status, D2TS(d));
        return 0;
    }

    FF_FILTER_FORWARD_WANTED(outlink, inlink);

    return FFERROR_NOT_READY;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    SetPTSContext *setpts = ctx->priv;
    av_expr_free(setpts->expr);
    setpts->expr = NULL;
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *arg,
                           char *res, int res_len, int flags)
{
    SetPTSContext *setpts = ctx->priv;
    AVExpr *new_expr;
    int ret;

    ret = ff_filter_process_command(ctx, cmd, arg, res, res_len, flags);

    if (ret < 0)
        return ret;

    if (!strcmp(cmd, "expr")) {
        ret = av_expr_parse(&new_expr, arg, var_names, NULL, NULL, NULL, NULL, 0, ctx);
        // Only free and replace previous expression if new one succeeds,
        // otherwise defensively keep everything intact even if reporting an error.
        if (ret < 0) {
            av_log(ctx, AV_LOG_ERROR, "Error while parsing expression '%s'\n", arg);
        } else {
            av_expr_free(setpts->expr);
            setpts->expr = new_expr;
            V(T_CHANGE) = NAN;
        }
    } else {
        ret = AVERROR(EINVAL);
    }

    return ret;
}
#undef V

#define OFFSET(x) offsetof(SetPTSContext, x)
#define V AV_OPT_FLAG_VIDEO_PARAM
#define A AV_OPT_FLAG_AUDIO_PARAM
#define R AV_OPT_FLAG_RUNTIME_PARAM
#define F AV_OPT_FLAG_FILTERING_PARAM

#if CONFIG_SETPTS_FILTER
static const AVOption setpts_options[] = {
    { "expr", "Expression determining the frame timestamp", OFFSET(expr_str), AV_OPT_TYPE_STRING, { .str = "PTS" }, .flags = V|F|R },
    { NULL }
};
AVFILTER_DEFINE_CLASS(setpts);

static const AVFilterPad avfilter_vf_setpts_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
    },
};

static const AVFilterPad outputs_video[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_output_video,
    },
};

const AVFilter ff_vf_setpts = {
    .name            = "setpts",
    .description     = NULL_IF_CONFIG_SMALL("Set PTS for the output video frame."),
    .init            = init,
    .activate        = activate,
    .uninit          = uninit,
    .process_command = process_command,
    .flags           = AVFILTER_FLAG_METADATA_ONLY,

    .priv_size = sizeof(SetPTSContext),
    .priv_class = &setpts_class,

    FILTER_INPUTS(avfilter_vf_setpts_inputs),
    FILTER_OUTPUTS(outputs_video),
};
#endif /* CONFIG_SETPTS_FILTER */

#if CONFIG_ASETPTS_FILTER

static const AVOption asetpts_options[] = {
    { "expr", "Expression determining the frame timestamp", OFFSET(expr_str), AV_OPT_TYPE_STRING, { .str = "PTS" }, .flags = A|F|R },
    { NULL }
};
AVFILTER_DEFINE_CLASS(asetpts);

static const AVFilterPad asetpts_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_input,
    },
};

const AVFilter ff_af_asetpts = {
    .name            = "asetpts",
    .description     = NULL_IF_CONFIG_SMALL("Set PTS for the output audio frame."),
    .init            = init,
    .activate        = activate,
    .uninit          = uninit,
    .process_command = process_command,
    .priv_size       = sizeof(SetPTSContext),
    .priv_class      = &asetpts_class,
    .flags           = AVFILTER_FLAG_METADATA_ONLY,
    FILTER_INPUTS(asetpts_inputs),
    FILTER_OUTPUTS(ff_audio_default_filterpad),
};
#endif /* CONFIG_ASETPTS_FILTER */
