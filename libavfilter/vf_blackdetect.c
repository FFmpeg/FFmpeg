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
 * @file
 * Video black detector, loosely based on blackframe with extended
 * syntax and features
 */

#include <float.h>
#include "libavutil/opt.h"
#include "libavutil/timestamp.h"
#include "avfilter.h"
#include "internal.h"

typedef struct BlackDetectContext {
    const AVClass *class;
    double  black_min_duration_time; ///< minimum duration of detected black, in seconds
    int64_t black_min_duration;      ///< minimum duration of detected black, expressed in timebase units
    int64_t black_start;             ///< pts start time of the first black picture
    int64_t black_end;               ///< pts end time of the last black picture
    int64_t last_picref_pts;         ///< pts of the last input picture
    int black_started;

    double       picture_black_ratio_th;
    double       pixel_black_th;
    unsigned int pixel_black_th_i;

    unsigned int nb_black_pixels;   ///< number of black pixels counted so far
    AVRational   time_base;
} BlackDetectContext;

#define OFFSET(x) offsetof(BlackDetectContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption blackdetect_options[] = {
    { "d",                  "set minimum detected black duration in seconds", OFFSET(black_min_duration_time), AV_OPT_TYPE_DOUBLE, {.dbl=2}, 0, DBL_MAX, FLAGS },
    { "black_min_duration", "set minimum detected black duration in seconds", OFFSET(black_min_duration_time), AV_OPT_TYPE_DOUBLE, {.dbl=2}, 0, DBL_MAX, FLAGS },
    { "picture_black_ratio_th", "set the picture black ratio threshold", OFFSET(picture_black_ratio_th), AV_OPT_TYPE_DOUBLE, {.dbl=.98}, 0, 1, FLAGS },
    { "pic_th",                 "set the picture black ratio threshold", OFFSET(picture_black_ratio_th), AV_OPT_TYPE_DOUBLE, {.dbl=.98}, 0, 1, FLAGS },
    { "pixel_black_th", "set the pixel black threshold", OFFSET(pixel_black_th), AV_OPT_TYPE_DOUBLE, {.dbl=.10}, 0, 1, FLAGS },
    { "pix_th",         "set the pixel black threshold", OFFSET(pixel_black_th), AV_OPT_TYPE_DOUBLE, {.dbl=.10}, 0, 1, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(blackdetect);

#define YUVJ_FORMATS \
    AV_PIX_FMT_YUVJ411P, AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ440P

static const enum AVPixelFormat yuvj_formats[] = {
    YUVJ_FORMATS, AV_PIX_FMT_NONE
};

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_GRAY8,
        AV_PIX_FMT_YUV410P, AV_PIX_FMT_YUV411P,
        AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P,
        AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUV444P,
        AV_PIX_FMT_NV12, AV_PIX_FMT_NV21,
        YUVJ_FORMATS,
        AV_PIX_FMT_NONE
    };

    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    BlackDetectContext *s = ctx->priv;

    s->time_base = inlink->time_base;
    s->black_min_duration = s->black_min_duration_time / av_q2d(s->time_base);

    s->pixel_black_th_i = ff_fmt_is_in(inlink->format, yuvj_formats) ?
        // luminance_minimum_value + pixel_black_th * luminance_range_size
             s->pixel_black_th *  255 :
        16 + s->pixel_black_th * (235 - 16);

    av_log(s, AV_LOG_VERBOSE,
           "black_min_duration:%s pixel_black_th:%f pixel_black_th_i:%d picture_black_ratio_th:%f\n",
           av_ts2timestr(s->black_min_duration, &s->time_base),
           s->pixel_black_th, s->pixel_black_th_i,
           s->picture_black_ratio_th);
    return 0;
}

static void check_black_end(AVFilterContext *ctx)
{
    BlackDetectContext *s = ctx->priv;

    if ((s->black_end - s->black_start) >= s->black_min_duration) {
        av_log(s, AV_LOG_INFO,
               "black_start:%s black_end:%s black_duration:%s\n",
               av_ts2timestr(s->black_start, &s->time_base),
               av_ts2timestr(s->black_end,   &s->time_base),
               av_ts2timestr(s->black_end - s->black_start, &s->time_base));
    }
}

static int filter_frame(AVFilterLink *inlink, AVFrame *picref)
{
    AVFilterContext *ctx = inlink->dst;
    BlackDetectContext *s = ctx->priv;
    double picture_black_ratio = 0;
    const uint8_t *p = picref->data[0];
    int x, i;

    for (i = 0; i < inlink->h; i++) {
        for (x = 0; x < inlink->w; x++)
            s->nb_black_pixels += p[x] <= s->pixel_black_th_i;
        p += picref->linesize[0];
    }

    picture_black_ratio = (double)s->nb_black_pixels / (inlink->w * inlink->h);

    av_log(ctx, AV_LOG_DEBUG,
           "frame:%"PRId64" picture_black_ratio:%f pts:%s t:%s type:%c\n",
           inlink->frame_count_out, picture_black_ratio,
           av_ts2str(picref->pts), av_ts2timestr(picref->pts, &s->time_base),
           av_get_picture_type_char(picref->pict_type));

    if (picture_black_ratio >= s->picture_black_ratio_th) {
        if (!s->black_started) {
            /* black starts here */
            s->black_started = 1;
            s->black_start = picref->pts;
            av_dict_set(&picref->metadata, "lavfi.black_start",
                av_ts2timestr(s->black_start, &s->time_base), 0);
        }
    } else if (s->black_started) {
        /* black ends here */
        s->black_started = 0;
        s->black_end = picref->pts;
        check_black_end(ctx);
        av_dict_set(&picref->metadata, "lavfi.black_end",
            av_ts2timestr(s->black_end, &s->time_base), 0);
    }

    s->last_picref_pts = picref->pts;
    s->nb_black_pixels = 0;
    return ff_filter_frame(inlink->dst->outputs[0], picref);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    BlackDetectContext *s = ctx->priv;

    if (s->black_started) {
        // FIXME: black_end should be set to last_picref_pts + last_picref_duration
        s->black_end = s->last_picref_pts;
        check_black_end(ctx);
    }
}

static const AVFilterPad blackdetect_inputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_input,
        .filter_frame  = filter_frame,
    },
    { NULL }
};

static const AVFilterPad blackdetect_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_blackdetect = {
    .name          = "blackdetect",
    .description   = NULL_IF_CONFIG_SMALL("Detect video intervals that are (almost) black."),
    .priv_size     = sizeof(BlackDetectContext),
    .query_formats = query_formats,
    .inputs        = blackdetect_inputs,
    .outputs       = blackdetect_outputs,
    .uninit        = uninit,
    .priv_class    = &blackdetect_class,
};
