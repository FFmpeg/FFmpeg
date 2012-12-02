/*
 * Copyright (c) 2012 Stefano Sabatini
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
 * @file framestep filter, inspired on libmpcodecs/vf_framestep.c by
 * Daniele Fornighieri <guru AT digitalfantasy it>.
 */

#include "avfilter.h"
#include "internal.h"
#include "video.h"

typedef struct {
    int frame_step, frame_count, frame_selected;
} FrameStepContext;

static av_cold int init(AVFilterContext *ctx, const char *args)
{
    FrameStepContext *framestep = ctx->priv;
    char *tailptr;
    long int n = 1;

    if (args) {
        n = strtol(args, &tailptr, 10);
        if (*tailptr || n <= 0 || n >= INT_MAX) {
            av_log(ctx, AV_LOG_ERROR,
                   "Invalid argument '%s', must be a positive integer <= INT_MAX\n", args);
            return AVERROR(EINVAL);
        }
    }

    framestep->frame_step = n;
    return 0;
}

static int config_output_props(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    FrameStepContext *framestep = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];

    outlink->frame_rate =
        av_div_q(inlink->frame_rate, (AVRational){framestep->frame_step, 1});

    av_log(ctx, AV_LOG_VERBOSE, "step:%d frame_rate:%d/%d(%f) -> frame_rate:%d/%d(%f)\n",
           framestep->frame_step,
           inlink->frame_rate.num, inlink->frame_rate.den, av_q2d(inlink->frame_rate),
           outlink->frame_rate.num, outlink->frame_rate.den, av_q2d(outlink->frame_rate));
    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFilterBufferRef *ref)
{
    FrameStepContext *framestep = inlink->dst->priv;

    if (!(framestep->frame_count++ % framestep->frame_step)) {
        framestep->frame_selected = 1;
        return ff_filter_frame(inlink->dst->outputs[0], ref);
    } else {
        framestep->frame_selected = 0;
        avfilter_unref_buffer(ref);
        return 0;
    }
}

static int request_frame(AVFilterLink *outlink)
{
    FrameStepContext *framestep = outlink->src->priv;
    AVFilterLink *inlink = outlink->src->inputs[0];
    int ret;

    framestep->frame_selected = 0;
    do {
        ret = ff_request_frame(inlink);
    } while (!framestep->frame_selected && ret >= 0);

    return ret;
}

static const AVFilterPad framestep_inputs[] = {
    {
        .name             = "default",
        .type             = AVMEDIA_TYPE_VIDEO,
        .get_video_buffer = ff_null_get_video_buffer,
        .filter_frame     = filter_frame,
    },
    { NULL }
};

static const AVFilterPad framestep_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output_props,
        .request_frame = request_frame,
    },
    { NULL }
};

AVFilter avfilter_vf_framestep = {
    .name      = "framestep",
    .description = NULL_IF_CONFIG_SMALL("Select one frame every N frames."),
    .init      = init,
    .priv_size = sizeof(FrameStepContext),
    .inputs    = framestep_inputs,
    .outputs   = framestep_outputs,
};
