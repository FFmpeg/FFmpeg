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

#include "libavutil/common.h"
#include "libavutil/opt.h"
#include "libavutil/mathematics.h"
#include "libavutil/parseutils.h"
#include "avfilter.h"
#include "internal.h"
#include "video.h"

typedef struct {
    const AVClass *class;
    AVRational ratio;
    char *ratio_str;
    int max;
} AspectContext;

#define OFFSET(x) offsetof(AspectContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption options[] = {
    {"max", "set max value for nominator or denominator in the ratio", OFFSET(max), AV_OPT_TYPE_INT, {.i64=100}, 1, INT_MAX, FLAGS },
    {"ratio", "set ratio", OFFSET(ratio_str), AV_OPT_TYPE_STRING, {.str="0"}, 0, 0, FLAGS },
    {"r",     "set ratio", OFFSET(ratio_str), AV_OPT_TYPE_STRING, {.str="0"}, 0, 0, FLAGS },
    {NULL}
};

static av_cold int init(AVFilterContext *ctx, const char *args, const AVClass *class)
{
    AspectContext *aspect = ctx->priv;
    static const char *shorthand[] = { "ratio", "max", NULL };
    char c;
    int ret;
    AVRational q;

    aspect->class = class;
    av_opt_set_defaults(aspect);

    if (sscanf(args, "%d:%d%c", &q.num, &q.den, &c) == 2) {
        aspect->ratio_str = av_strdup(args);
        av_log(ctx, AV_LOG_WARNING,
               "num:den syntax is deprecated, please use num/den or named options instead\n");
    } else if ((ret = av_opt_set_from_string(aspect, args, shorthand, "=", ":")) < 0) {
        return ret;
    }

    if (aspect->ratio_str) {
        ret = av_parse_ratio(&aspect->ratio, aspect->ratio_str, aspect->max, 0, ctx);
        if (ret < 0 || aspect->ratio.num < 0 || aspect->ratio.den <= 0) {
            av_log(ctx, AV_LOG_ERROR,
                   "Invalid string '%s' for aspect ratio\n", args);
            return AVERROR(EINVAL);
        }
    }

    av_log(ctx, AV_LOG_VERBOSE, "a:%d/%d\n", aspect->ratio.num, aspect->ratio.den);
    return 0;
}

static int filter_frame(AVFilterLink *link, AVFilterBufferRef *frame)
{
    AspectContext *aspect = link->dst->priv;

    frame->video->sample_aspect_ratio = aspect->ratio;
    return ff_filter_frame(link->dst->outputs[0], frame);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AspectContext *aspect = ctx->priv;

    av_opt_free(aspect);
}

#if CONFIG_SETDAR_FILTER

#define setdar_options options
AVFILTER_DEFINE_CLASS(setdar);

static av_cold int setdar_init(AVFilterContext *ctx, const char *args)
{
    return init(ctx, args, &setdar_class);
}

static int setdar_config_props(AVFilterLink *inlink)
{
    AspectContext *aspect = inlink->dst->priv;
    AVRational dar = aspect->ratio;

    av_reduce(&aspect->ratio.num, &aspect->ratio.den,
               aspect->ratio.num * inlink->h,
               aspect->ratio.den * inlink->w, 100);

    av_log(inlink->dst, AV_LOG_VERBOSE, "w:%d h:%d -> dar:%d/%d sar:%d/%d\n",
           inlink->w, inlink->h, dar.num, dar.den, aspect->ratio.num, aspect->ratio.den);

    inlink->sample_aspect_ratio = aspect->ratio;

    return 0;
}

static const AVFilterPad avfilter_vf_setdar_inputs[] = {
    {
        .name             = "default",
        .type             = AVMEDIA_TYPE_VIDEO,
        .config_props     = setdar_config_props,
        .get_video_buffer = ff_null_get_video_buffer,
        .filter_frame     = filter_frame,
    },
    { NULL }
};

static const AVFilterPad avfilter_vf_setdar_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter avfilter_vf_setdar = {
    .name      = "setdar",
    .description = NULL_IF_CONFIG_SMALL("Set the frame display aspect ratio."),

    .init      = setdar_init,
    .uninit    = uninit,

    .priv_size = sizeof(AspectContext),

    .inputs    = avfilter_vf_setdar_inputs,

    .outputs   = avfilter_vf_setdar_outputs,
    .priv_class = &setdar_class,
};

#endif /* CONFIG_SETDAR_FILTER */

#if CONFIG_SETSAR_FILTER

#define setsar_options options
AVFILTER_DEFINE_CLASS(setsar);

static av_cold int setsar_init(AVFilterContext *ctx, const char *args)
{
    return init(ctx, args, &setsar_class);
}

static int setsar_config_props(AVFilterLink *inlink)
{
    AspectContext *aspect = inlink->dst->priv;

    inlink->sample_aspect_ratio = aspect->ratio;

    return 0;
}

static const AVFilterPad avfilter_vf_setsar_inputs[] = {
    {
        .name             = "default",
        .type             = AVMEDIA_TYPE_VIDEO,
        .config_props     = setsar_config_props,
        .get_video_buffer = ff_null_get_video_buffer,
        .filter_frame     = filter_frame,
    },
    { NULL }
};

static const AVFilterPad avfilter_vf_setsar_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter avfilter_vf_setsar = {
    .name      = "setsar",
    .description = NULL_IF_CONFIG_SMALL("Set the pixel sample aspect ratio."),

    .init      = setsar_init,
    .uninit    = uninit,

    .priv_size = sizeof(AspectContext),

    .inputs    = avfilter_vf_setsar_inputs,

    .outputs   = avfilter_vf_setsar_outputs,
    .priv_class = &setsar_class,
};

#endif /* CONFIG_SETSAR_FILTER */
