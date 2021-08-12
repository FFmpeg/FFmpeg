/*
 * Copyright (c) 2019 Paul B Mahol
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

#include "libavutil/avstring.h"
#include "libavutil/common.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"

#include "avfilter.h"
#include "filters.h"
#include "internal.h"
#include "video.h"

typedef struct FreezeFramesContext {
    const AVClass *class;
    int64_t first, last, replace;

    AVFrame *replace_frame;
} FreezeFramesContext;

#define OFFSET(x) offsetof(FreezeFramesContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)

static const AVOption freezeframes_options[] = {
    { "first",   "set first frame to freeze", OFFSET(first),   AV_OPT_TYPE_INT64, {.i64=0}, 0, INT64_MAX, FLAGS },
    { "last",    "set last frame to freeze",  OFFSET(last),    AV_OPT_TYPE_INT64, {.i64=0}, 0, INT64_MAX, FLAGS },
    { "replace", "set frame to replace",      OFFSET(replace), AV_OPT_TYPE_INT64, {.i64=0}, 0, INT64_MAX, FLAGS },
    { NULL },
};

AVFILTER_DEFINE_CLASS(freezeframes);

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *sourcelink = ctx->inputs[0];
    AVFilterLink *replacelink = ctx->inputs[1];

    if (sourcelink->w != replacelink->w || sourcelink->h != replacelink->h) {
        av_log(ctx, AV_LOG_ERROR,
               "Input frame sizes do not match (%dx%d vs %dx%d).\n",
               sourcelink->w, sourcelink->h,
               replacelink->w, replacelink->h);
        return AVERROR(EINVAL);
    }

    outlink->w = sourcelink->w;
    outlink->h = sourcelink->h;
    outlink->time_base = sourcelink->time_base;
    outlink->sample_aspect_ratio = sourcelink->sample_aspect_ratio;
    outlink->frame_rate = sourcelink->frame_rate;

    return 0;
}

static int activate(AVFilterContext *ctx)
{
    AVFilterLink *outlink = ctx->outputs[0];
    FreezeFramesContext *s = ctx->priv;
    AVFrame *frame = NULL;
    int drop = ctx->inputs[0]->frame_count_out >= s->first &&
               ctx->inputs[0]->frame_count_out <= s->last;
    int replace = ctx->inputs[1]->frame_count_out == s->replace;
    int ret;

    FF_FILTER_FORWARD_STATUS_BACK_ALL(outlink, ctx);

    if (drop && s->replace_frame) {
        ret = ff_inlink_consume_frame(ctx->inputs[0], &frame);
        if (ret < 0)
            return ret;

        if (frame) {
            int64_t dropped_pts = frame->pts;

            av_frame_free(&frame);
            frame = av_frame_clone(s->replace_frame);
            if (!frame)
                return AVERROR(ENOMEM);
            frame->pts = dropped_pts;
            return ff_filter_frame(outlink, frame);
        }
    } else if (!drop) {
        ret = ff_inlink_consume_frame(ctx->inputs[0], &frame);
        if (ret < 0)
            return ret;

        if (frame)
            return ff_filter_frame(outlink, frame);
    }

    ret = ff_inlink_consume_frame(ctx->inputs[1], &frame);
    if (ret < 0)
        return ret;
    if (replace && frame) {
        s->replace_frame = frame;
    } else if (frame) {
        av_frame_free(&frame);
    }

    FF_FILTER_FORWARD_STATUS(ctx->inputs[0], outlink);
    FF_FILTER_FORWARD_STATUS(ctx->inputs[1], outlink);

    if (!drop || (drop && s->replace_frame))
        FF_FILTER_FORWARD_WANTED(outlink, ctx->inputs[0]);
    if (!s->replace_frame)
        FF_FILTER_FORWARD_WANTED(outlink, ctx->inputs[1]);

    return FFERROR_NOT_READY;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    FreezeFramesContext *s = ctx->priv;

    av_frame_free(&s->replace_frame);
}

static const AVFilterPad freezeframes_inputs[] = {
    {
        .name = "source",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    {
        .name = "replace",
        .type = AVMEDIA_TYPE_VIDEO,
    },
};

static const AVFilterPad freezeframes_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_output,
    },
};

const AVFilter ff_vf_freezeframes = {
    .name          = "freezeframes",
    .description   = NULL_IF_CONFIG_SMALL("Freeze video frames."),
    .priv_size     = sizeof(FreezeFramesContext),
    .priv_class    = &freezeframes_class,
    FILTER_INPUTS(freezeframes_inputs),
    FILTER_OUTPUTS(freezeframes_outputs),
    .activate      = activate,
    .uninit        = uninit,
};
