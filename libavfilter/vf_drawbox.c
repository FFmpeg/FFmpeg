/*
 * Copyright (c) 2008 Affine Systems, Inc (Michael Sullivan, Bobby Impollonia)
 *
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
    int x, y, w_opt, h_opt, w, h;
    char *color_str;
    unsigned char yuv_color[4];
    int vsub, hsub;   ///< chroma subsampling
} DrawBoxContext;

static av_cold int init(AVFilterContext *ctx)
{
    DrawBoxContext *s = ctx->priv;
    uint8_t rgba_color[4];

    if (av_parse_color(rgba_color, s->color_str, -1, ctx) < 0)
        return AVERROR(EINVAL);

    s->yuv_color[Y] = RGB_TO_Y_CCIR(rgba_color[0], rgba_color[1], rgba_color[2]);
    s->yuv_color[U] = RGB_TO_U_CCIR(rgba_color[0], rgba_color[1], rgba_color[2], 0);
    s->yuv_color[V] = RGB_TO_V_CCIR(rgba_color[0], rgba_color[1], rgba_color[2], 0);
    s->yuv_color[A] = rgba_color[3];

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    enum AVPixelFormat pix_fmts[] = {
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
    DrawBoxContext *s = inlink->dst->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);

    s->hsub = desc->log2_chroma_w;
    s->vsub = desc->log2_chroma_h;

    s->w = (s->w_opt > 0) ? s->w_opt : inlink->w;
    s->h = (s->h_opt > 0) ? s->h_opt : inlink->h;

    av_log(inlink->dst, AV_LOG_VERBOSE, "x:%d y:%d w:%d h:%d color:0x%02X%02X%02X%02X\n",
           s->w, s->y, s->w, s->h,
           s->yuv_color[Y], s->yuv_color[U], s->yuv_color[V], s->yuv_color[A]);

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    DrawBoxContext *s = inlink->dst->priv;
    int plane, x, y, xb = s->x, yb = s->y;
    unsigned char *row[4];

    for (y = FFMAX(yb, 0); y < frame->height && y < (yb + s->h); y++) {
        row[0] = frame->data[0] + y * frame->linesize[0];

        for (plane = 1; plane < 3; plane++)
            row[plane] = frame->data[plane] +
                 frame->linesize[plane] * (y >> s->vsub);

        for (x = FFMAX(xb, 0); x < (xb + s->w) && x < frame->width; x++) {
            double alpha = (double)s->yuv_color[A] / 255;

            if ((y - yb < 3) || (yb + s->h - y < 4) ||
                (x - xb < 3) || (xb + s->w - x < 4)) {
                row[0][x                 ] = (1 - alpha) * row[0][x                 ] + alpha * s->yuv_color[Y];
                row[1][x >> s->hsub] = (1 - alpha) * row[1][x >> s->hsub] + alpha * s->yuv_color[U];
                row[2][x >> s->hsub] = (1 - alpha) * row[2][x >> s->hsub] + alpha * s->yuv_color[V];
            }
        }
    }

    return ff_filter_frame(inlink->dst->outputs[0], frame);
}

#define OFFSET(x) offsetof(DrawBoxContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM
static const AVOption options[] = {
    { "x",      "Horizontal position of the left box edge", OFFSET(x),         AV_OPT_TYPE_INT,    { .i64 = 0 }, INT_MIN, INT_MAX, FLAGS },
    { "y",      "Vertical position of the top box edge",    OFFSET(y),         AV_OPT_TYPE_INT,    { .i64 = 0 }, INT_MIN, INT_MAX, FLAGS },
    { "width",  "Width of the box",                         OFFSET(w_opt),     AV_OPT_TYPE_INT,    { .i64 = 0 }, 0,       INT_MAX, FLAGS },
    { "height", "Height of the box",                        OFFSET(h_opt),     AV_OPT_TYPE_INT,    { .i64 = 0 }, 0,       INT_MAX, FLAGS },
    { "color",  "Color of the box",                         OFFSET(color_str), AV_OPT_TYPE_STRING, { .str = "black" },    .flags = FLAGS },
    { NULL },
};

static const AVClass drawbox_class = {
    .class_name = "drawbox",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

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

AVFilter ff_vf_drawbox = {
    .name      = "drawbox",
    .description = NULL_IF_CONFIG_SMALL("Draw a colored box on the input video."),
    .priv_size = sizeof(DrawBoxContext),
    .priv_class = &drawbox_class,
    .init      = init,

    .query_formats   = query_formats,
    .inputs    = avfilter_vf_drawbox_inputs,
    .outputs   = avfilter_vf_drawbox_outputs,
};
