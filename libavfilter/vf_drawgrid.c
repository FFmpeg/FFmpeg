/*
 * Copyright (c) 2008 Affine Systems, Inc (Michael Sullivan, Bobby Impollonia)
 * Copyright (c) 2013 Andrey Utkin <andrey.krieger.utkin gmail com>
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
 * Grid drawing filter.
 * Code derived from drawbox filter.
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
} DrawGridContext;

static av_cold int init(AVFilterContext *ctx)
{
    DrawGridContext *drawgrid = ctx->priv;
    uint8_t rgba_color[4];

    if (!strcmp(drawgrid->color_str, "invert"))
        drawgrid->invert_color = 1;
    else if (av_parse_color(rgba_color, drawgrid->color_str, -1, ctx) < 0)
        return AVERROR(EINVAL);

    if (!drawgrid->invert_color) {
        drawgrid->yuv_color[Y] = RGB_TO_Y_CCIR(rgba_color[0], rgba_color[1], rgba_color[2]);
        drawgrid->yuv_color[U] = RGB_TO_U_CCIR(rgba_color[0], rgba_color[1], rgba_color[2], 0);
        drawgrid->yuv_color[V] = RGB_TO_V_CCIR(rgba_color[0], rgba_color[1], rgba_color[2], 0);
        drawgrid->yuv_color[A] = rgba_color[3];
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
    DrawGridContext *drawgrid = inlink->dst->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);

    drawgrid->hsub = desc->log2_chroma_w;
    drawgrid->vsub = desc->log2_chroma_h;

    if (drawgrid->w == 0) drawgrid->w = inlink->w - drawgrid->thickness;
    if (drawgrid->h == 0) drawgrid->h = inlink->h - drawgrid->thickness;

    av_log(inlink->dst, AV_LOG_VERBOSE, "x:%d y:%d w:%d h:%d color:0x%02X%02X%02X%02X\n",
           drawgrid->x, drawgrid->y, drawgrid->w, drawgrid->h,
           drawgrid->yuv_color[Y], drawgrid->yuv_color[U], drawgrid->yuv_color[V], drawgrid->yuv_color[A]);

    return 0;
}

static av_pure av_always_inline int pixel_belongs_to_grid(DrawGridContext *drawgrid, int x, int y)
{
    // x is horizontal (width) coord,
    // y is vertical (height) coord
    int x_modulo;
    int y_modulo;

    // Abstract from the offset
    x -= drawgrid->x;
    y -= drawgrid->y;

    x_modulo = x % drawgrid->w;
    y_modulo = y % drawgrid->h;

    // If x or y got negative, fix values to preserve logics
    if (x_modulo < 0)
        x_modulo += drawgrid->w;
    if (y_modulo < 0)
        y_modulo += drawgrid->h;

    return x_modulo < drawgrid->thickness  // Belongs to vertical line
        || y_modulo < drawgrid->thickness;  // Belongs to horizontal line
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    DrawGridContext *drawgrid = inlink->dst->priv;
    int plane, x, y;
    uint8_t *row[4];

    for (y = 0; y < frame->height; y++) {
        row[0] = frame->data[0] + y * frame->linesize[0];

        for (plane = 1; plane < 3; plane++)
            row[plane] = frame->data[plane] +
                 frame->linesize[plane] * (y >> drawgrid->vsub);

        if (drawgrid->invert_color) {
            for (x = 0; x < frame->width; x++)
                if (pixel_belongs_to_grid(drawgrid, x, y))
                    row[0][x] = 0xff - row[0][x];
        } else {
            for (x = 0; x < frame->width; x++) {
                double alpha = (double)drawgrid->yuv_color[A] / 255;

                if (pixel_belongs_to_grid(drawgrid, x, y)) {
                    row[0][x                  ] = (1 - alpha) * row[0][x                  ] + alpha * drawgrid->yuv_color[Y];
                    row[1][x >> drawgrid->hsub] = (1 - alpha) * row[1][x >> drawgrid->hsub] + alpha * drawgrid->yuv_color[U];
                    row[2][x >> drawgrid->hsub] = (1 - alpha) * row[2][x >> drawgrid->hsub] + alpha * drawgrid->yuv_color[V];
                }
            }
        }
    }

    return ff_filter_frame(inlink->dst->outputs[0], frame);
}

#define OFFSET(x) offsetof(DrawGridContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption drawgrid_options[] = {
    { "x",         "set horizontal offset",   OFFSET(x),         AV_OPT_TYPE_INT,    { .i64 = 0 },       INT_MIN,  INT_MAX,  FLAGS },
    { "y",         "set vertical offset",     OFFSET(y),         AV_OPT_TYPE_INT,    { .i64 = 0 },       INT_MIN,  INT_MAX,  FLAGS },
    { "width",     "set width of grid cell",  OFFSET(w),         AV_OPT_TYPE_INT,    { .i64 = 0 },       0,        INT_MAX,  FLAGS },
    { "w",         "set width of grid cell",  OFFSET(w),         AV_OPT_TYPE_INT,    { .i64 = 0 },       0,        INT_MAX,  FLAGS },
    { "height",    "set height of grid cell", OFFSET(h),         AV_OPT_TYPE_INT,    { .i64 = 0 },       0,        INT_MAX,  FLAGS },
    { "h",         "set height of grid cell", OFFSET(h),         AV_OPT_TYPE_INT,    { .i64 = 0 },       0,        INT_MAX,  FLAGS },
    { "color",     "set color of the grid",   OFFSET(color_str), AV_OPT_TYPE_STRING, { .str = "black" }, CHAR_MIN, CHAR_MAX, FLAGS },
    { "c",         "set color of the grid",   OFFSET(color_str), AV_OPT_TYPE_STRING, { .str = "black" }, CHAR_MIN, CHAR_MAX, FLAGS },
    { "thickness", "set grid line thickness", OFFSET(thickness), AV_OPT_TYPE_INT,    {.i64=1},           0,        INT_MAX,  FLAGS },
    { "t",         "set grid line thickness", OFFSET(thickness), AV_OPT_TYPE_INT,    {.i64=1},           0,        INT_MAX,  FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(drawgrid);

static const AVFilterPad avfilter_vf_drawgrid_inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_VIDEO,
        .config_props   = config_input,
        .filter_frame   = filter_frame,
        .needs_writable = 1,
    },
    { NULL }
};

static const AVFilterPad avfilter_vf_drawgrid_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter avfilter_vf_drawgrid = {
    .name          = "drawgrid",
    .description   = NULL_IF_CONFIG_SMALL("Draw a colored grid on the input video."),
    .priv_size     = sizeof(DrawGridContext),
    .priv_class    = &drawgrid_class,
    .init          = init,
    .query_formats = query_formats,
    .inputs        = avfilter_vf_drawgrid_inputs,
    .outputs       = avfilter_vf_drawgrid_outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
