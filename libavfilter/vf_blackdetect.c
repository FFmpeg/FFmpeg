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

typedef struct {
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

    unsigned int frame_count;       ///< frame number
    unsigned int nb_black_pixels;   ///< number of black pixels counted so far
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
    { NULL },
};

AVFILTER_DEFINE_CLASS(blackdetect);

#define YUVJ_FORMATS \
    PIX_FMT_YUVJ420P, PIX_FMT_YUVJ422P, PIX_FMT_YUVJ444P, PIX_FMT_YUVJ440P

static enum PixelFormat yuvj_formats[] = {
    YUVJ_FORMATS, PIX_FMT_NONE
};

static int query_formats(AVFilterContext *ctx)
{
    static const enum PixelFormat pix_fmts[] = {
        PIX_FMT_YUV410P, PIX_FMT_YUV420P, PIX_FMT_GRAY8, PIX_FMT_NV12,
        PIX_FMT_NV21, PIX_FMT_YUV444P, PIX_FMT_YUV422P, PIX_FMT_YUV411P,
        YUVJ_FORMATS,
        PIX_FMT_NONE
    };

    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
    return 0;
}

static av_cold int init(AVFilterContext *ctx, const char *args)
{
    int ret;
    BlackDetectContext *blackdetect = ctx->priv;

    blackdetect->class = &blackdetect_class;
    av_opt_set_defaults(blackdetect);

    if ((ret = av_set_options_string(blackdetect, args, "=", ":")) < 0)
        return ret;

    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    BlackDetectContext *blackdetect = ctx->priv;

    blackdetect->black_min_duration =
        blackdetect->black_min_duration_time / av_q2d(inlink->time_base);

    blackdetect->pixel_black_th_i = ff_fmt_is_in(inlink->format, yuvj_formats) ?
        // luminance_minimum_value + pixel_black_th * luminance_range_size
             blackdetect->pixel_black_th *  255 :
        16 + blackdetect->pixel_black_th * (235 - 16);

    av_log(blackdetect, AV_LOG_VERBOSE,
           "black_min_duration:%s pixel_black_th:%f pixel_black_th_i:%d picture_black_ratio_th:%f\n",
           av_ts2timestr(blackdetect->black_min_duration, &inlink->time_base),
           blackdetect->pixel_black_th, blackdetect->pixel_black_th_i,
           blackdetect->picture_black_ratio_th);
    return 0;
}

static void check_black_end(AVFilterContext *ctx)
{
    BlackDetectContext *blackdetect = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];

    if ((blackdetect->black_end - blackdetect->black_start) >= blackdetect->black_min_duration) {
        av_log(blackdetect, AV_LOG_INFO,
               "black_start:%s black_end:%s black_duration:%s\n",
               av_ts2timestr(blackdetect->black_start, &inlink->time_base),
               av_ts2timestr(blackdetect->black_end,   &inlink->time_base),
               av_ts2timestr(blackdetect->black_end - blackdetect->black_start, &inlink->time_base));
    }
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    BlackDetectContext *blackdetect = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    int ret = ff_request_frame(inlink);

    if (ret == AVERROR_EOF && blackdetect->black_started) {
        // FIXME: black_end should be set to last_picref_pts + last_picref_duration
        blackdetect->black_end = blackdetect->last_picref_pts;
        check_black_end(ctx);
    }
    return ret;
}

static int draw_slice(AVFilterLink *inlink, int y, int h, int slice_dir)
{
    AVFilterContext *ctx = inlink->dst;
    BlackDetectContext *blackdetect = ctx->priv;
    AVFilterBufferRef *picref = inlink->cur_buf;
    int x, i;
    const uint8_t *p = picref->data[0] + y * picref->linesize[0];

    for (i = 0; i < h; i++) {
        for (x = 0; x < inlink->w; x++)
            blackdetect->nb_black_pixels += p[x] <= blackdetect->pixel_black_th_i;
        p += picref->linesize[0];
    }

    return ff_draw_slice(ctx->outputs[0], y, h, slice_dir);
}

static int end_frame(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    BlackDetectContext *blackdetect = ctx->priv;
    AVFilterBufferRef *picref = inlink->cur_buf;
    double picture_black_ratio = 0;

    picture_black_ratio = (double)blackdetect->nb_black_pixels / (inlink->w * inlink->h);

    av_log(ctx, AV_LOG_DEBUG,
           "frame:%u picture_black_ratio:%f pos:%"PRId64" pts:%s t:%s type:%c\n",
           blackdetect->frame_count, picture_black_ratio,
           picref->pos, av_ts2str(picref->pts), av_ts2timestr(picref->pts, &inlink->time_base),
           av_get_picture_type_char(picref->video->pict_type));

    if (picture_black_ratio >= blackdetect->picture_black_ratio_th) {
        if (!blackdetect->black_started) {
            /* black starts here */
            blackdetect->black_started = 1;
            blackdetect->black_start = picref->pts;
        }
    } else if (blackdetect->black_started) {
        /* black ends here */
        blackdetect->black_started = 0;
        blackdetect->black_end = picref->pts;
        check_black_end(ctx);
    }

    blackdetect->last_picref_pts = picref->pts;
    blackdetect->frame_count++;
    blackdetect->nb_black_pixels = 0;
    return ff_end_frame(inlink->dst->outputs[0]);
}

AVFilter avfilter_vf_blackdetect = {
    .name          = "blackdetect",
    .description   = NULL_IF_CONFIG_SMALL("Detect video intervals that are (almost) black."),
    .priv_size     = sizeof(BlackDetectContext),
    .init          = init,
    .query_formats = query_formats,

    .inputs = (const AVFilterPad[]) {
        { .name             = "default",
          .type             = AVMEDIA_TYPE_VIDEO,
          .config_props     = config_input,
          .draw_slice       = draw_slice,
          .get_video_buffer = ff_null_get_video_buffer,
          .start_frame      = ff_null_start_frame,
          .end_frame        = end_frame, },
        { .name = NULL }
    },

    .outputs = (const AVFilterPad[]) {
        { .name             = "default",
          .type             = AVMEDIA_TYPE_VIDEO,
          .request_frame    = request_frame, },
        { .name = NULL }
    },
    .priv_class = &blackdetect_class,
};
