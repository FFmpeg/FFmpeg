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

#include <float.h>

#include "libavutil/common.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"

#include "avfilter.h"
#include "internal.h"
#include "video.h"

typedef struct {
    const AVClass *class;
    AVRational dar;
    AVRational sar;
#if FF_API_OLD_FILTER_OPTS
    float aspect_num, aspect_den;
#endif
} AspectContext;

#if FF_API_OLD_FILTER_OPTS
static av_cold int init(AVFilterContext *ctx)
{
    AspectContext *s = ctx->priv;

    if (s->aspect_num > 0 && s->aspect_den > 0) {
        av_log(ctx, AV_LOG_WARNING, "This syntax is deprecated, use "
               "dar=<number> or dar=num/den.\n");
        s->sar = s->dar = av_d2q(s->aspect_num / s->aspect_den, INT_MAX);
    }

    return 0;
}
#endif

static int filter_frame(AVFilterLink *link, AVFrame *frame)
{
    AspectContext *s = link->dst->priv;

    frame->sample_aspect_ratio = s->sar;
    return ff_filter_frame(link->dst->outputs[0], frame);
}

#define OFFSET(x) offsetof(AspectContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM

#if CONFIG_SETDAR_FILTER
/* for setdar filter, convert from frame aspect ratio to pixel aspect ratio */
static int setdar_config_props(AVFilterLink *inlink)
{
    AspectContext *s = inlink->dst->priv;
    AVRational dar;

    if (s->dar.num && s->dar.den) {
        av_reduce(&s->sar.num, &s->sar.den,
                   s->dar.num * inlink->h,
                   s->dar.den * inlink->w, 100);
        inlink->sample_aspect_ratio = s->sar;
        dar = s->dar;
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
#if FF_API_OLD_FILTER_OPTS
    { "dar_num", NULL, OFFSET(aspect_num), AV_OPT_TYPE_FLOAT, { .dbl = 0 }, 0, FLT_MAX, FLAGS },
    { "dar_den", NULL, OFFSET(aspect_den), AV_OPT_TYPE_FLOAT, { .dbl = 0 }, 0, FLT_MAX, FLAGS },
#endif
    { "dar", "display aspect ratio", OFFSET(dar), AV_OPT_TYPE_RATIONAL, { .dbl = 0 }, 0, INT_MAX, FLAGS },
    { NULL },
};

static const AVClass setdar_class = {
    .class_name = "setdar",
    .item_name  = av_default_item_name,
    .option     = setdar_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

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
/* for setdar filter, convert from frame aspect ratio to pixel aspect ratio */
static int setsar_config_props(AVFilterLink *inlink)
{
    AspectContext *s = inlink->dst->priv;

    inlink->sample_aspect_ratio = s->sar;

    return 0;
}

static const AVOption setsar_options[] = {
#if FF_API_OLD_FILTER_OPTS
    { "sar_num", NULL, OFFSET(aspect_num), AV_OPT_TYPE_FLOAT, { .dbl = 0 }, 0, FLT_MAX, FLAGS },
    { "sar_den", NULL, OFFSET(aspect_den), AV_OPT_TYPE_FLOAT, { .dbl = 0 }, 0, FLT_MAX, FLAGS },
#endif
    { "sar", "sample (pixel) aspect ratio", OFFSET(sar), AV_OPT_TYPE_RATIONAL, { .dbl = 1 }, 0, INT_MAX, FLAGS },
    { NULL },
};

static const AVClass setsar_class = {
    .class_name = "setsar",
    .item_name  = av_default_item_name,
    .option     = setsar_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

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
