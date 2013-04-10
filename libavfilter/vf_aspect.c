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

#include <float.h>

#include "libavutil/common.h"
#include "libavutil/opt.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"

#include "avfilter.h"
#include "internal.h"
#include "video.h"

typedef struct {
    const AVClass *class;
    AVRational aspect;
    char *ratio_str;
    int max;
#if FF_API_OLD_FILTER_OPTS
    float aspect_num, aspect_den;
#endif
} AspectContext;

#if FF_API_OLD_FILTER_OPTS
static av_cold int init(AVFilterContext *ctx, const char *args)
{
    AspectContext *s = ctx->priv;
    static const char *shorthand[] = { "ratio", "max", NULL };
    char c;
    int ret;
    AVRational q;

    if (args && sscanf(args, "%d:%d%c", &q.num, &q.den, &c) == 2) {
        s->ratio_str = av_strdup(args);
        av_log(ctx, AV_LOG_WARNING,
               "num:den syntax is deprecated, please use num/den or named options instead\n");
    } else if ((ret = av_opt_set_from_string(s, args, shorthand, "=", ":")) < 0) {
        return ret;
    }

    if (s->ratio_str) {
        ret = av_parse_ratio(&s->aspect, s->ratio_str, s->max, 0, ctx);
        if (ret < 0 || s->aspect.num < 0 || s->aspect.den <= 0) {
            av_log(ctx, AV_LOG_ERROR,
                   "Invalid string '%s' for aspect ratio\n", args);
            return AVERROR(EINVAL);
        }
    }

    return 0;
}
#endif

static int filter_frame(AVFilterLink *link, AVFrame *frame)
{
    AspectContext *aspect = link->dst->priv;

    frame->sample_aspect_ratio = aspect->aspect;
    return ff_filter_frame(link->dst->outputs[0], frame);
}

#define OFFSET(x) offsetof(AspectContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

#if CONFIG_SETDAR_FILTER

static int setdar_config_props(AVFilterLink *inlink)
{
    AspectContext *aspect = inlink->dst->priv;
    AVRational dar = aspect->aspect;

    if (aspect->aspect.num && aspect->aspect.den) {
        av_reduce(&aspect->aspect.num, &aspect->aspect.den,
                   aspect->aspect.num * inlink->h,
                   aspect->aspect.den * inlink->w, 100);
        inlink->sample_aspect_ratio = aspect->aspect;
    } else {
        inlink->sample_aspect_ratio = (AVRational){ 1, 1 };
        dar = (AVRational){ inlink->w, inlink->h };
    }

    av_log(inlink->dst, AV_LOG_VERBOSE, "w:%d h:%d -> dar:%d/%d sar:%d/%d\n",
           inlink->w, inlink->h, dar.num, dar.den,
           inlink->sample_aspect_ratio.num, inlink->sample_aspect_ratio.den);

    return 0;
}

static const AVOption setdar_options[] = {
    {"max", "set max value for nominator or denominator in the ratio", OFFSET(max), AV_OPT_TYPE_INT, {.i64=100}, 1, INT_MAX, FLAGS },
    {"ratio", "display aspect ratio", OFFSET(ratio_str), AV_OPT_TYPE_STRING, {.str="0"}, 0, 0, FLAGS },
    {"r",     "display aspect ratio", OFFSET(ratio_str), AV_OPT_TYPE_STRING, {.str="0"}, 0, 0, FLAGS },
    {"dar",   "display aspect ratio", OFFSET(ratio_str), AV_OPT_TYPE_STRING, {.str="0"}, 0, 0, FLAGS },
    {NULL}
};

AVFILTER_DEFINE_CLASS(setdar);

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

#if FF_API_OLD_FILTER_OPTS
    .init      = init,
#endif

    .priv_size = sizeof(AspectContext),
    .priv_class = &setdar_class,

    .inputs    = avfilter_vf_setdar_inputs,

    .outputs   = avfilter_vf_setdar_outputs,
};

#endif /* CONFIG_SETDAR_FILTER */

#if CONFIG_SETSAR_FILTER


static int setsar_config_props(AVFilterLink *inlink)
{
    AspectContext *aspect = inlink->dst->priv;

    inlink->sample_aspect_ratio = aspect->aspect;

    return 0;
}

static const AVOption setsar_options[] = {
    {"max", "set max value for nominator or denominator in the ratio", OFFSET(max), AV_OPT_TYPE_INT, {.i64=100}, 1, INT_MAX, FLAGS },
    {"ratio", "sample (pixel) aspect ratio", OFFSET(ratio_str), AV_OPT_TYPE_STRING, {.str="0"}, 0, 0, FLAGS },
    {"r",     "sample (pixel) aspect ratio", OFFSET(ratio_str), AV_OPT_TYPE_STRING, {.str="0"}, 0, 0, FLAGS },
    {"sar",   "sample (pixel) aspect ratio", OFFSET(ratio_str), AV_OPT_TYPE_STRING, {.str="0"}, 0, 0, FLAGS },
    {NULL}
};

AVFILTER_DEFINE_CLASS(setsar);

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

#if FF_API_OLD_FILTER_OPTS
    .init      = init,
#endif

    .priv_size = sizeof(AspectContext),
    .priv_class = &setsar_class,

    .inputs    = avfilter_vf_setsar_inputs,

    .outputs   = avfilter_vf_setsar_outputs,
};

#endif /* CONFIG_SETSAR_FILTER */
