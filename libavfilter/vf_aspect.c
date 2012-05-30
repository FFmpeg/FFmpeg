/*
 * Copyright (c) 2010 Bobby Bingham

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
 * aspect ratio modification video filters
 */

#include "libavutil/mathematics.h"
#include "avfilter.h"
#include "video.h"

typedef struct {
    AVRational aspect;
} AspectContext;

static av_cold int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    AspectContext *aspect = ctx->priv;
    double  ratio;
    int64_t gcd;
    char c = 0;

    if (args) {
        if (sscanf(args, "%d:%d%c", &aspect->aspect.num, &aspect->aspect.den, &c) != 2)
            if (sscanf(args, "%lf%c", &ratio, &c) == 1)
                aspect->aspect = av_d2q(ratio, 100);

        if (c || aspect->aspect.num <= 0 || aspect->aspect.den <= 0) {
            av_log(ctx, AV_LOG_ERROR,
                   "Invalid string '%s' for aspect ratio.\n", args);
            return AVERROR(EINVAL);
        }

        gcd = av_gcd(FFABS(aspect->aspect.num), FFABS(aspect->aspect.den));
        if (gcd) {
            aspect->aspect.num /= gcd;
            aspect->aspect.den /= gcd;
        }
    }

    if (aspect->aspect.den == 0)
        aspect->aspect = (AVRational) {0, 1};

    av_log(ctx, AV_LOG_INFO, "a:%d/%d\n", aspect->aspect.num, aspect->aspect.den);
    return 0;
}

static void start_frame(AVFilterLink *link, AVFilterBufferRef *picref)
{
    AspectContext *aspect = link->dst->priv;

    picref->video->pixel_aspect = aspect->aspect;
    ff_start_frame(link->dst->outputs[0], picref);
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

    .inputs    = (AVFilterPad[]) {{ .name             = "default",
                                    .type             = AVMEDIA_TYPE_VIDEO,
                                    .config_props     = setdar_config_props,
                                    .get_video_buffer = ff_null_get_video_buffer,
                                    .start_frame      = start_frame,
                                    .end_frame        = ff_null_end_frame },
                                  { .name = NULL}},

    .outputs   = (AVFilterPad[]) {{ .name             = "default",
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

    .inputs    = (AVFilterPad[]) {{ .name             = "default",
                                    .type             = AVMEDIA_TYPE_VIDEO,
                                    .config_props     = setsar_config_props,
                                    .get_video_buffer = ff_null_get_video_buffer,
                                    .start_frame      = start_frame,
                                    .end_frame        = ff_null_end_frame },
                                  { .name = NULL}},

    .outputs   = (AVFilterPad[]) {{ .name             = "default",
                                    .type             = AVMEDIA_TYPE_VIDEO, },
                                  { .name = NULL}},
};
#endif /* CONFIG_SETSAR_FILTER */
