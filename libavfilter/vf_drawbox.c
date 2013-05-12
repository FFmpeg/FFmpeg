/*
 * Copyright (c) 2008 Affine Systems, Inc (Michael Sullivan, Bobby Impollonia)
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
 * Box drawing filter. Also a nice template for a filter that needs to
 * write in the input frame.
 */

#include "libavutil/colorspace.h"
#include "libavutil/common.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/parseutils.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

enum { Y, U, V, A };

typedef struct {
    const AVClass *class;
    int x, y, w, h, thickness;
    char *color_str;
    unsigned char yuv_color[4];
    int invert_color; ///< invert luma color
    int vsub, hsub;   ///< chroma subsampling
} DrawBoxContext;

static av_cold int init(AVFilterContext *ctx)
{
    DrawBoxContext *drawbox = ctx->priv;
    uint8_t rgba_color[4];

    if (!strcmp(drawbox->color_str, "invert"))
        drawbox->invert_color = 1;
    else if (av_parse_color(rgba_color, drawbox->color_str, -1, ctx) < 0)
        return AVERROR(EINVAL);

    if (!drawbox->invert_color) {
        drawbox->yuv_color[Y] = RGB_TO_Y_CCIR(rgba_color[0], rgba_color[1], rgba_color[2]);
        drawbox->yuv_color[U] = RGB_TO_U_CCIR(rgba_color[0], rgba_color[1], rgba_color[2], 0);
        drawbox->yuv_color[V] = RGB_TO_V_CCIR(rgba_color[0], rgba_color[1], rgba_color[2], 0);
        drawbox->yuv_color[A] = rgba_color[3];
    }

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUV444P,  AV_PIX_FMT_YUV422P,  AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_YUV411P,  AV_PIX_FMT_YUV410P,
        AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ420P,
        AV_PIX_FMT_YUV440P,  AV_PIX_FMT_YUVJ440P,
        AV_PIX_FMT_NONE
    };

    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    DrawBoxContext *drawbox = inlink->dst->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);

    drawbox->hsub = desc->log2_chroma_w;
    drawbox->vsub = desc->log2_chroma_h;

    if (drawbox->w == 0) drawbox->w = inlink->w;
    if (drawbox->h == 0) drawbox->h = inlink->h;

    av_log(inlink->dst, AV_LOG_VERBOSE, "x:%d y:%d w:%d h:%d color:0x%02X%02X%02X%02X\n",
           drawbox->x, drawbox->y, drawbox->w, drawbox->h,
           drawbox->yuv_color[Y], drawbox->yuv_color[U], drawbox->yuv_color[V], drawbox->yuv_color[A]);

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    DrawBoxContext *drawbox = inlink->dst->priv;
    int plane, x, y, xb = drawbox->x, yb = drawbox->y;
    unsigned char *row[4];

    for (y = FFMAX(yb, 0); y < frame->height && y < (yb + drawbox->h); y++) {
        row[0] = frame->data[0] + y * frame->linesize[0];

        for (plane = 1; plane < 3; plane++)
            row[plane] = frame->data[plane] +
                 frame->linesize[plane] * (y >> drawbox->vsub);

        if (drawbox->invert_color) {
            for (x = FFMAX(xb, 0); x < xb + drawbox->w && x < frame->width; x++)
                if ((y - yb < drawbox->thickness-1) || (yb + drawbox->h - y < drawbox->thickness) ||
                    (x - xb < drawbox->thickness-1) || (xb + drawbox->w - x < drawbox->thickness))
                    row[0][x] = 0xff - row[0][x];
        } else {
            for (x = FFMAX(xb, 0); x < xb + drawbox->w && x < frame->width; x++) {
                double alpha = (double)drawbox->yuv_color[A] / 255;

                if ((y - yb < drawbox->thickness-1) || (yb + drawbox->h - y < drawbox->thickness) ||
                    (x - xb < drawbox->thickness-1) || (xb + drawbox->w - x < drawbox->thickness)) {
                    row[0][x                 ] = (1 - alpha) * row[0][x                 ] + alpha * drawbox->yuv_color[Y];
                    row[1][x >> drawbox->hsub] = (1 - alpha) * row[1][x >> drawbox->hsub] + alpha * drawbox->yuv_color[U];
                    row[2][x >> drawbox->hsub] = (1 - alpha) * row[2][x >> drawbox->hsub] + alpha * drawbox->yuv_color[V];
                }
            }
        }
    }

    return ff_filter_frame(inlink->dst->outputs[0], frame);
}

#define OFFSET(x) offsetof(DrawBoxContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption drawbox_options[] = {
    { "x",      "Horizontal position of the left box edge", OFFSET(x),         AV_OPT_TYPE_INT,    { .i64 = 0 }, INT_MIN, INT_MAX, FLAGS },
    { "y",      "Vertical position of the top box edge",    OFFSET(y),         AV_OPT_TYPE_INT,    { .i64 = 0 }, INT_MIN, INT_MAX, FLAGS },
    { "width",  "Width of the box",                         OFFSET(w),         AV_OPT_TYPE_INT,    { .i64 = 0 }, 0,       INT_MAX, FLAGS },
    { "w",      "Width of the box",                         OFFSET(w),         AV_OPT_TYPE_INT,    { .i64 = 0 }, 0,       INT_MAX, FLAGS },
    { "height", "Height of the box",                        OFFSET(h),         AV_OPT_TYPE_INT,    { .i64 = 0 }, 0,       INT_MAX, FLAGS },
    { "h",      "Height of the box",                        OFFSET(h),         AV_OPT_TYPE_INT,    { .i64 = 0 }, 0,       INT_MAX, FLAGS },
    { "color",  "Color of the box",                         OFFSET(color_str), AV_OPT_TYPE_STRING, { .str = "black" }, CHAR_MIN, CHAR_MAX, .flags = FLAGS },
    { "c",      "Color of the box",                         OFFSET(color_str), AV_OPT_TYPE_STRING, { .str = "black" }, CHAR_MIN, CHAR_MAX, .flags = FLAGS },
    { "thickness",   "set the box maximum thickness",       OFFSET(thickness), AV_OPT_TYPE_INT, {.i64=4}, 0, INT_MAX, FLAGS },
    { "t",           "set the box maximum thickness",       OFFSET(thickness), AV_OPT_TYPE_INT, {.i64=4}, 0, INT_MAX, FLAGS },
    { NULL },
};

AVFILTER_DEFINE_CLASS(drawbox);

static const AVFilterPad avfilter_vf_drawbox_inputs[] = {
    {
        .name             = "default",
        .type             = AVMEDIA_TYPE_VIDEO,
        .config_props     = config_input,
        .get_video_buffer = ff_null_get_video_buffer,
        .filter_frame     = filter_frame,
        .needs_writable   = 1,
    },
    { NULL }
};

static const AVFilterPad avfilter_vf_drawbox_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter avfilter_vf_drawbox = {
    .name      = "drawbox",
    .description = NULL_IF_CONFIG_SMALL("Draw a colored box on the input video."),
    .priv_size = sizeof(DrawBoxContext),
    .priv_class = &drawbox_class,
    .init      = init,

    .query_formats   = query_formats,
    .inputs    = avfilter_vf_drawbox_inputs,
    .outputs   = avfilter_vf_drawbox_outputs,
    .flags     = AVFILTER_FLAG_SUPPORT_TIMELINE,
};
