/*
 * Copyright (c) 2010 Bobby Bingham

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
 * aspect ratio modification video filters
 */

#include "libavutil/mathematics.h"
#include "libavutil/parseutils.h"
#include "avfilter.h"

typedef struct {
    AVRational aspect;
} AspectContext;

static av_cold int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    AspectContext *aspect = ctx->priv;
    int ret;

    if (args) {
        if ((ret = av_parse_ratio(&aspect->aspect, args, 100, 0, ctx)) < 0 ||
            aspect->aspect.num < 0 || aspect->aspect.den <= 0) {
            av_log(ctx, AV_LOG_ERROR,
                   "Invalid string '%s' for aspect ratio.\n", args);
            return ret;
        }

        av_log(ctx, AV_LOG_INFO, "a:%d/%d\n", aspect->aspect.num, aspect->aspect.den);
    }
    return 0;
}

static void start_frame(AVFilterLink *link, AVFilterBufferRef *picref)
{
    AspectContext *aspect = link->dst->priv;

    picref->video->sample_aspect_ratio = aspect->aspect;
    avfilter_start_frame(link->dst->outputs[0], picref);
}

#if CONFIG_SETDAR_FILTER
/* for setdar filter, convert from frame aspect ratio to pixel aspect ratio */
static int setdar_config_props(AVFilterLink *inlink)
{
    AspectContext *aspect = inlink->dst->priv;
    AVRational dar = aspect->aspect;

    av_reduce(&aspect->aspect.num, &aspect->aspect.den,
               aspect->aspect.num * inlink->h,
               aspect->aspect.den * inlink->w, 100);

    av_log(inlink->dst, AV_LOG_INFO, "w:%d h:%d -> dar:%d/%d sar:%d/%d\n",
           inlink->w, inlink->h, dar.num, dar.den, aspect->aspect.num, aspect->aspect.den);

    inlink->sample_aspect_ratio = aspect->aspect;

    return 0;
}

AVFilter avfilter_vf_setdar = {
    .name      = "setdar",
    .description = NULL_IF_CONFIG_SMALL("Set the frame display aspect ratio."),

    .init      = init,

    .priv_size = sizeof(AspectContext),

    .inputs    = (const AVFilterPad[]) {{ .name       = "default",
                                    .type             = AVMEDIA_TYPE_VIDEO,
                                    .config_props     = setdar_config_props,
                                    .get_video_buffer = avfilter_null_get_video_buffer,
                                    .start_frame      = start_frame,
                                    .end_frame        = avfilter_null_end_frame },
                                  { .name = NULL}},

    .outputs   = (const AVFilterPad[]) {{ .name       = "default",
                                    .type             = AVMEDIA_TYPE_VIDEO, },
                                  { .name = NULL}},
};
#endif /* CONFIG_SETDAR_FILTER */

#if CONFIG_SETSAR_FILTER
/* for setdar filter, convert from frame aspect ratio to pixel aspect ratio */
static int setsar_config_props(AVFilterLink *inlink)
{
    AspectContext *aspect = inlink->dst->priv;

    inlink->sample_aspect_ratio = aspect->aspect;

    return 0;
}

AVFilter avfilter_vf_setsar = {
    .name      = "setsar",
    .description = NULL_IF_CONFIG_SMALL("Set the pixel sample aspect ratio."),

    .init      = init,

    .priv_size = sizeof(AspectContext),

    .inputs    = (const AVFilterPad[]) {{ .name       = "default",
                                    .type             = AVMEDIA_TYPE_VIDEO,
                                    .config_props     = setsar_config_props,
                                    .get_video_buffer = avfilter_null_get_video_buffer,
                                    .start_frame      = start_frame,
                                    .end_frame        = avfilter_null_end_frame },
                                  { .name = NULL}},

    .outputs   = (const AVFilterPad[]) {{ .name       = "default",
                                    .type             = AVMEDIA_TYPE_VIDEO, },
                                  { .name = NULL}},
};
#endif /* CONFIG_SETSAR_FILTER */
