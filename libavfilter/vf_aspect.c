/*
 * Copyright (c) 2010 Bobby Bingham
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
 * aspect ratio modification video filters
 */

#include <float.h>

#include "libavutil/common.h"
#include "libavutil/eval.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"

#include "avfilter.h"
#include "internal.h"
#include "video.h"

typedef struct {
    const AVClass *class;
    AVRational dar;
    AVRational sar;
    int max;
#if FF_API_OLD_FILTER_OPTS
    float aspect_den;
#endif
    char *ratio_str;
} AspectContext;

static av_cold int init(AVFilterContext *ctx)
{
    AspectContext *s = ctx->priv;
    int ret;

#if FF_API_OLD_FILTER_OPTS
    if (s->ratio_str && s->aspect_den > 0) {
        double num;
        av_log(ctx, AV_LOG_WARNING,
               "num:den syntax is deprecated, please use num/den or named options instead\n");
        ret = av_expr_parse_and_eval(&num, s->ratio_str, NULL, NULL,
                                     NULL, NULL, NULL, NULL, NULL, 0, ctx);
        if (ret < 0) {
            av_log(ctx, AV_LOG_ERROR, "Unable to parse ratio numerator \"%s\"\n", s->ratio_str);
            return AVERROR(EINVAL);
        }
        s->sar = s->dar = av_d2q(num / s->aspect_den, s->max);
    } else
#endif
    if (s->ratio_str) {
        ret = av_parse_ratio(&s->sar, s->ratio_str, s->max, 0, ctx);
        if (ret < 0 || s->sar.num < 0 || s->sar.den <= 0) {
            av_log(ctx, AV_LOG_ERROR,
                   "Invalid string '%s' for aspect ratio\n", s->ratio_str);
            return AVERROR(EINVAL);
        }
        s->dar = s->sar;
    }
    return 0;
}

static int filter_frame(AVFilterLink *link, AVFrame *frame)
{
    AspectContext *s = link->dst->priv;

    frame->sample_aspect_ratio = s->sar;
    return ff_filter_frame(link->dst->outputs[0], frame);
}

#define OFFSET(x) offsetof(AspectContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static inline void compute_dar(AVRational *dar, AVRational sar, int w, int h)
{
    if (sar.num && sar.den) {
        av_reduce(&dar->num, &dar->den, sar.num * w, sar.den * h, INT_MAX);
    } else {
        av_reduce(&dar->num, &dar->den, w, h, INT_MAX);
    }
}

#if CONFIG_SETDAR_FILTER

static int setdar_config_props(AVFilterLink *inlink)
{
    AspectContext *s = inlink->dst->priv;
    AVRational dar;
    AVRational old_dar;
    AVRational old_sar = inlink->sample_aspect_ratio;

    if (s->dar.num && s->dar.den) {
        av_reduce(&s->sar.num, &s->sar.den,
                   s->dar.num * inlink->h,
                   s->dar.den * inlink->w, INT_MAX);
        inlink->sample_aspect_ratio = s->sar;
        dar = s->dar;
    } else {
        inlink->sample_aspect_ratio = (AVRational){ 1, 1 };
        dar = (AVRational){ inlink->w, inlink->h };
    }

    compute_dar(&old_dar, old_sar, inlink->w, inlink->h);
    av_log(inlink->dst, AV_LOG_VERBOSE, "w:%d h:%d dar:%d/%d sar:%d/%d -> dar:%d/%d sar:%d/%d\n",
           inlink->w, inlink->h, old_dar.num, old_dar.den, old_sar.num, old_sar.den,
           dar.num, dar.den, inlink->sample_aspect_ratio.num, inlink->sample_aspect_ratio.den);

    return 0;
}

static const AVOption setdar_options[] = {
    { "dar",   "set display aspect ratio", OFFSET(ratio_str), AV_OPT_TYPE_STRING, {.str="0"}, .flags=FLAGS },
    { "ratio", "set display aspect ratio", OFFSET(ratio_str), AV_OPT_TYPE_STRING, {.str="0"}, .flags=FLAGS },
    { "r",     "set display aspect ratio", OFFSET(ratio_str), AV_OPT_TYPE_STRING, {.str="0"}, .flags=FLAGS },
#if FF_API_OLD_FILTER_OPTS
    { "dar_den", NULL, OFFSET(aspect_den), AV_OPT_TYPE_FLOAT, { .dbl = 0 }, 0, FLT_MAX, FLAGS },
#endif
    { "max",   "set max value for nominator or denominator in the ratio", OFFSET(max), AV_OPT_TYPE_INT, {.i64=100}, 1, INT_MAX, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(setdar);

static const AVFilterPad avfilter_vf_setdar_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = setdar_config_props,
        .filter_frame = filter_frame,
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
    .name        = "setdar",
    .description = NULL_IF_CONFIG_SMALL("Set the frame display aspect ratio."),
    .init        = init,
    .priv_size   = sizeof(AspectContext),
    .priv_class  = &setdar_class,
    .inputs      = avfilter_vf_setdar_inputs,
    .outputs     = avfilter_vf_setdar_outputs,
};

#endif /* CONFIG_SETDAR_FILTER */

#if CONFIG_SETSAR_FILTER

static int setsar_config_props(AVFilterLink *inlink)
{
    AspectContext *s = inlink->dst->priv;
    AVRational old_sar = inlink->sample_aspect_ratio;
    AVRational old_dar, dar;

    inlink->sample_aspect_ratio = s->sar;

    compute_dar(&old_dar, old_sar, inlink->w, inlink->h);
    compute_dar(&dar, s->sar, inlink->w, inlink->h);
    av_log(inlink->dst, AV_LOG_VERBOSE, "w:%d h:%d sar:%d/%d dar:%d/%d -> sar:%d/%d dar:%d/%d\n",
           inlink->w, inlink->h, old_sar.num, old_sar.den, old_dar.num, old_dar.den,
           inlink->sample_aspect_ratio.num, inlink->sample_aspect_ratio.den, dar.num, dar.den);

    return 0;
}

static const AVOption setsar_options[] = {
    { "sar",   "set sample (pixel) aspect ratio", OFFSET(ratio_str), AV_OPT_TYPE_STRING, {.str="0"}, .flags=FLAGS },
    { "ratio", "set sample (pixel) aspect ratio", OFFSET(ratio_str), AV_OPT_TYPE_STRING, {.str="0"}, .flags=FLAGS },
    { "r",     "set sample (pixel) aspect ratio", OFFSET(ratio_str), AV_OPT_TYPE_STRING, {.str="0"}, .flags=FLAGS },
#if FF_API_OLD_FILTER_OPTS
    { "sar_den", NULL, OFFSET(aspect_den), AV_OPT_TYPE_FLOAT, { .dbl = 0 }, 0, FLT_MAX, FLAGS },
#endif
    { "max",   "set max value for nominator or denominator in the ratio", OFFSET(max), AV_OPT_TYPE_INT, {.i64=100}, 1, INT_MAX, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(setsar);

static const AVFilterPad avfilter_vf_setsar_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = setsar_config_props,
        .filter_frame = filter_frame,
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
    .name        = "setsar",
    .description = NULL_IF_CONFIG_SMALL("Set the pixel sample aspect ratio."),
    .init        = init,
    .priv_size   = sizeof(AspectContext),
    .priv_class  = &setsar_class,
    .inputs      = avfilter_vf_setsar_inputs,
    .outputs     = avfilter_vf_setsar_outputs,
};

#endif /* CONFIG_SETSAR_FILTER */
