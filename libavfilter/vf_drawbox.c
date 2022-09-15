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
 * Box and grid drawing filters. Also a nice template for a filter
 * that needs to write in the input frame.
 */

#include "config_components.h"

#include "libavutil/colorspace.h"
#include "libavutil/common.h"
#include "libavutil/opt.h"
#include "libavutil/eval.h"
#include "libavutil/pixdesc.h"
#include "libavutil/parseutils.h"
#include "libavutil/detection_bbox.h"
#include "avfilter.h"
#include "drawutils.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

static const char *const var_names[] = {
    "dar",
    "hsub", "vsub",
    "in_h", "ih",      ///< height of the input video
    "in_w", "iw",      ///< width  of the input video
    "sar",
    "x",
    "y",
    "h",              ///< height of the rendered box
    "w",              ///< width  of the rendered box
    "t",
    "fill",
    NULL
};

enum { Y, U, V, A };
enum { R, G, B };

enum var_name {
    VAR_DAR,
    VAR_HSUB, VAR_VSUB,
    VAR_IN_H, VAR_IH,
    VAR_IN_W, VAR_IW,
    VAR_SAR,
    VAR_X,
    VAR_Y,
    VAR_H,
    VAR_W,
    VAR_T,
    VAR_MAX,
    VARS_NB
};

struct DrawBoxContext;

typedef int (*PixelBelongsToRegion)(struct DrawBoxContext *s, int x, int y);

typedef struct DrawBoxContext {
    const AVClass *class;
    int x, y, w, h;
    int thickness;
    char *color_str;
    uint8_t rgba_map[4];
    uint8_t rgba_color[4];
    unsigned char yuv_color[4];
    int invert_color; ///< invert luma color
    int vsub, hsub;   ///< chroma subsampling
    char *x_expr, *y_expr; ///< expression for x and y
    char *w_expr, *h_expr; ///< expression for width and height
    char *t_expr;          ///< expression for thickness
    char *box_source_string; ///< string for box data source
    int have_alpha;
    int replace;
    int step;
    enum AVFrameSideDataType box_source;

    void (*draw_region)(AVFrame *frame, struct DrawBoxContext *ctx, int left, int top, int right, int down,
                        PixelBelongsToRegion pixel_belongs_to_region);
} DrawBoxContext;

static const int NUM_EXPR_EVALS = 5;

#define ASSIGN_THREE_CHANNELS                                        \
    row[0] = frame->data[0] +  y               * frame->linesize[0]; \
    row[1] = frame->data[1] + (y >> ctx->vsub) * frame->linesize[1]; \
    row[2] = frame->data[2] + (y >> ctx->vsub) * frame->linesize[2];

#define ASSIGN_FOUR_CHANNELS                          \
    ASSIGN_THREE_CHANNELS                             \
    row[3] = frame->data[3] + y * frame->linesize[3];

static void draw_region(AVFrame *frame, DrawBoxContext *ctx, int left, int top, int right, int down,
                        PixelBelongsToRegion pixel_belongs_to_region)
{
    unsigned char *row[4];
    int x, y;
    if (ctx->have_alpha && ctx->replace) {
        for (y = top; y < down; y++) {
            ASSIGN_FOUR_CHANNELS
            if (ctx->invert_color) {
                for (x = left; x < right; x++)
                    if (pixel_belongs_to_region(ctx, x, y))
                        row[0][x] = 0xff - row[0][x];
            } else {
                for (x = left; x < right; x++) {
                    if (pixel_belongs_to_region(ctx, x, y)) {
                        row[0][x             ] = ctx->yuv_color[Y];
                        row[1][x >> ctx->hsub] = ctx->yuv_color[U];
                        row[2][x >> ctx->hsub] = ctx->yuv_color[V];
                        row[3][x             ] = ctx->yuv_color[A];
                    }
                }
            }
        }
    } else {
        for (y = top; y < down; y++) {
            ASSIGN_THREE_CHANNELS
            if (ctx->invert_color) {
                for (x = left; x < right; x++)
                    if (pixel_belongs_to_region(ctx, x, y))
                        row[0][x] = 0xff - row[0][x];
            } else {
                for (x = left; x < right; x++) {
                    double alpha = (double)ctx->yuv_color[A] / 255;

                    if (pixel_belongs_to_region(ctx, x, y)) {
                        row[0][x             ] = (1 - alpha) * row[0][x             ] + alpha * ctx->yuv_color[Y];
                        row[1][x >> ctx->hsub] = (1 - alpha) * row[1][x >> ctx->hsub] + alpha * ctx->yuv_color[U];
                        row[2][x >> ctx->hsub] = (1 - alpha) * row[2][x >> ctx->hsub] + alpha * ctx->yuv_color[V];
                    }
                }
            }
        }
    }
}

#define ASSIGN_THREE_CHANNELS_PACKED                  \
    row[0] = frame->data[0] + y * frame->linesize[0] + ctx->rgba_map[0]; \
    row[1] = frame->data[0] + y * frame->linesize[0] + ctx->rgba_map[1]; \
    row[2] = frame->data[0] + y * frame->linesize[0] + ctx->rgba_map[2];

#define ASSIGN_FOUR_CHANNELS_PACKED                   \
    ASSIGN_THREE_CHANNELS_PACKED                      \
    row[3] = frame->data[0] + y * frame->linesize[0] + ctx->rgba_map[3];

static void draw_region_rgb_packed(AVFrame *frame, DrawBoxContext *ctx, int left, int top, int right, int down,
                                   PixelBelongsToRegion pixel_belongs_to_region)
{
    const int C = ctx->step;
    uint8_t *row[4];

    if (ctx->have_alpha && ctx->replace) {
        for (int y = top; y < down; y++) {
            ASSIGN_FOUR_CHANNELS_PACKED
            if (ctx->invert_color) {
                for (int x = left; x < right; x++)
                    if (pixel_belongs_to_region(ctx, x, y)) {
                        row[0][x*C] = 0xff - row[0][x*C];
                        row[1][x*C] = 0xff - row[1][x*C];
                        row[2][x*C] = 0xff - row[2][x*C];
                    }
            } else {
                for (int x = left; x < right; x++) {
                    if (pixel_belongs_to_region(ctx, x, y)) {
                        row[0][x*C] = ctx->rgba_color[R];
                        row[1][x*C] = ctx->rgba_color[G];
                        row[2][x*C] = ctx->rgba_color[B];
                        row[3][x*C] = ctx->rgba_color[A];
                    }
                }
            }
        }
    } else {
        for (int y = top; y < down; y++) {
            ASSIGN_THREE_CHANNELS_PACKED
            if (ctx->invert_color) {
                for (int x = left; x < right; x++)
                    if (pixel_belongs_to_region(ctx, x, y)) {
                        row[0][x*C] = 0xff - row[0][x*C];
                        row[1][x*C] = 0xff - row[1][x*C];
                        row[2][x*C] = 0xff - row[2][x*C];
                    }
            } else {
                for (int x = left; x < right; x++) {
                    float alpha = (float)ctx->rgba_color[A] / 255.f;

                    if (pixel_belongs_to_region(ctx, x, y)) {
                        row[0][x*C] = (1.f - alpha) * row[0][x*C] + alpha * ctx->rgba_color[R];
                        row[1][x*C] = (1.f - alpha) * row[1][x*C] + alpha * ctx->rgba_color[G];
                        row[2][x*C] = (1.f - alpha) * row[2][x*C] + alpha * ctx->rgba_color[B];
                    }
                }
            }
        }
    }
}

static enum AVFrameSideDataType box_source_string_parse(const char *box_source_string)
{
    av_assert0(box_source_string);
    if (!strcmp(box_source_string, "side_data_detection_bboxes")) {
        return AV_FRAME_DATA_DETECTION_BBOXES;
    } else {
        // will support side_data_regions_of_interest next
        return AVERROR(EINVAL);
    }
}

static av_cold int init(AVFilterContext *ctx)
{
    DrawBoxContext *s = ctx->priv;

    if (s->box_source_string) {
        s->box_source = box_source_string_parse(s->box_source_string);
        if ((int)s->box_source < 0) {
            av_log(ctx, AV_LOG_ERROR, "Error box source: %s\n",s->box_source_string);
            return AVERROR(EINVAL);
        }
    }

    if (!strcmp(s->color_str, "invert"))
        s->invert_color = 1;
    else if (av_parse_color(s->rgba_color, s->color_str, -1, ctx) < 0)
        return AVERROR(EINVAL);

    if (!s->invert_color) {
        s->yuv_color[Y] = RGB_TO_Y_CCIR(s->rgba_color[0], s->rgba_color[1], s->rgba_color[2]);
        s->yuv_color[U] = RGB_TO_U_CCIR(s->rgba_color[0], s->rgba_color[1], s->rgba_color[2], 0);
        s->yuv_color[V] = RGB_TO_V_CCIR(s->rgba_color[0], s->rgba_color[1], s->rgba_color[2], 0);
        s->yuv_color[A] = s->rgba_color[3];
    }

    return 0;
}

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_YUV444P,  AV_PIX_FMT_YUV422P,  AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUV411P,  AV_PIX_FMT_YUV410P,
    AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ420P,
    AV_PIX_FMT_YUV440P,  AV_PIX_FMT_YUVJ440P,
    AV_PIX_FMT_YUVA420P, AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUVA444P,
    AV_PIX_FMT_RGB24,  AV_PIX_FMT_BGR24,
    AV_PIX_FMT_RGBA,   AV_PIX_FMT_BGRA,
    AV_PIX_FMT_ARGB,   AV_PIX_FMT_ABGR,
    AV_PIX_FMT_0RGB,   AV_PIX_FMT_0BGR,
    AV_PIX_FMT_RGB0,   AV_PIX_FMT_BGR0,
    AV_PIX_FMT_NONE
};

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    DrawBoxContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    double var_values[VARS_NB], res;
    char *expr;
    int ret;
    int i;

    ff_fill_rgba_map(s->rgba_map, inlink->format);

    if (!(desc->flags & AV_PIX_FMT_FLAG_RGB))
        s->draw_region = draw_region;
    else
        s->draw_region = draw_region_rgb_packed;

    s->step = av_get_padded_bits_per_pixel(desc) >> 3;
    s->hsub = desc->log2_chroma_w;
    s->vsub = desc->log2_chroma_h;
    s->have_alpha = desc->flags & AV_PIX_FMT_FLAG_ALPHA;

    var_values[VAR_IN_H] = var_values[VAR_IH] = inlink->h;
    var_values[VAR_IN_W] = var_values[VAR_IW] = inlink->w;
    var_values[VAR_SAR]  = inlink->sample_aspect_ratio.num ? av_q2d(inlink->sample_aspect_ratio) : 1;
    var_values[VAR_DAR]  = (double)inlink->w / inlink->h * var_values[VAR_SAR];
    var_values[VAR_HSUB] = s->hsub;
    var_values[VAR_VSUB] = s->vsub;
    var_values[VAR_X] = NAN;
    var_values[VAR_Y] = NAN;
    var_values[VAR_H] = NAN;
    var_values[VAR_W] = NAN;
    var_values[VAR_T] = NAN;

    for (i = 0; i <= NUM_EXPR_EVALS; i++) {
        /* evaluate expressions, fail on last iteration */
        var_values[VAR_MAX] = inlink->w;
        if ((ret = av_expr_parse_and_eval(&res, (expr = s->x_expr),
                                          var_names, var_values,
                                          NULL, NULL, NULL, NULL, NULL, 0, ctx)) < 0 && i == NUM_EXPR_EVALS)
            goto fail;
        s->x = var_values[VAR_X] = res;

        var_values[VAR_MAX] = inlink->h;
        if ((ret = av_expr_parse_and_eval(&res, (expr = s->y_expr),
                                          var_names, var_values,
                                          NULL, NULL, NULL, NULL, NULL, 0, ctx)) < 0 && i == NUM_EXPR_EVALS)
            goto fail;
        s->y = var_values[VAR_Y] = res;

        var_values[VAR_MAX] = inlink->w - s->x;
        if ((ret = av_expr_parse_and_eval(&res, (expr = s->w_expr),
                                          var_names, var_values,
                                          NULL, NULL, NULL, NULL, NULL, 0, ctx)) < 0 && i == NUM_EXPR_EVALS)
            goto fail;
        s->w = var_values[VAR_W] = res;

        var_values[VAR_MAX] = inlink->h - s->y;
        if ((ret = av_expr_parse_and_eval(&res, (expr = s->h_expr),
                                          var_names, var_values,
                                          NULL, NULL, NULL, NULL, NULL, 0, ctx)) < 0 && i == NUM_EXPR_EVALS)
            goto fail;
        s->h = var_values[VAR_H] = res;

        var_values[VAR_MAX] = INT_MAX;
        if ((ret = av_expr_parse_and_eval(&res, (expr = s->t_expr),
                                          var_names, var_values,
                                          NULL, NULL, NULL, NULL, NULL, 0, ctx)) < 0 && i == NUM_EXPR_EVALS)
            goto fail;
        s->thickness = var_values[VAR_T] = res;
    }

    /* if w or h are zero, use the input w/h */
    s->w = (s->w > 0) ? s->w : inlink->w;
    s->h = (s->h > 0) ? s->h : inlink->h;

    /* sanity check width and height */
    if (s->w <  0 || s->h <  0) {
        av_log(ctx, AV_LOG_ERROR, "Size values less than 0 are not acceptable.\n");
        return AVERROR(EINVAL);
    }

    av_log(ctx, AV_LOG_VERBOSE, "x:%d y:%d w:%d h:%d color:0x%02X%02X%02X%02X\n",
           s->x, s->y, s->w, s->h,
           s->yuv_color[Y], s->yuv_color[U], s->yuv_color[V], s->yuv_color[A]);

    return 0;

fail:
    av_log(ctx, AV_LOG_ERROR,
           "Error when evaluating the expression '%s'.\n",
           expr);
    return ret;
}

static av_pure av_always_inline int pixel_belongs_to_box(DrawBoxContext *s, int x, int y)
{
    return (y - s->y < s->thickness) || (s->y + s->h - 1 - y < s->thickness) ||
           (x - s->x < s->thickness) || (s->x + s->w - 1 - x < s->thickness);
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    DrawBoxContext *s = inlink->dst->priv;
    const AVDetectionBBoxHeader *header = NULL;
    const AVDetectionBBox *bbox;
    AVFrameSideData *sd;
    int loop = 1;

    if (s->box_source == AV_FRAME_DATA_DETECTION_BBOXES) {
        sd = av_frame_get_side_data(frame, AV_FRAME_DATA_DETECTION_BBOXES);
        if (sd) {
            header = (AVDetectionBBoxHeader *)sd->data;
            loop = header->nb_bboxes;
        } else {
            av_log(s, AV_LOG_WARNING, "No detection bboxes.\n");
            return ff_filter_frame(inlink->dst->outputs[0], frame);
        }
    }

    for (int i = 0; i < loop; i++) {
        if (header) {
            bbox = av_get_detection_bbox(header, i);
            s->y = bbox->y;
            s->x = bbox->x;
            s->h = bbox->h;
            s->w = bbox->w;
        }

        s->draw_region(frame, s, FFMAX(s->x, 0), FFMAX(s->y, 0), FFMIN(s->x + s->w, frame->width),
                       FFMIN(s->y + s->h, frame->height), pixel_belongs_to_box);
    }

    return ff_filter_frame(inlink->dst->outputs[0], frame);
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args, char *res, int res_len, int flags)
{
    AVFilterLink *inlink = ctx->inputs[0];
    DrawBoxContext *s = ctx->priv;
    int old_x = s->x;
    int old_y = s->y;
    int old_w = s->w;
    int old_h = s->h;
    int old_t = s->thickness;
    int old_r = s->replace;
    int ret;

    ret = ff_filter_process_command(ctx, cmd, args, res, res_len, flags);
    if (ret < 0)
        return ret;

    ret = init(ctx);
    if (ret < 0)
        goto end;
    ret = config_input(inlink);
end:
    if (ret < 0) {
        s->x = old_x;
        s->y = old_y;
        s->w = old_w;
        s->h = old_h;
        s->thickness = old_t;
        s->replace = old_r;
    }

    return ret;
}

#define OFFSET(x) offsetof(DrawBoxContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

#if CONFIG_DRAWBOX_FILTER

static const AVOption drawbox_options[] = {
    { "x",         "set horizontal position of the left box edge", OFFSET(x_expr),    AV_OPT_TYPE_STRING, { .str="0" },       0, 0, FLAGS },
    { "y",         "set vertical position of the top box edge",    OFFSET(y_expr),    AV_OPT_TYPE_STRING, { .str="0" },       0, 0, FLAGS },
    { "width",     "set width of the box",                         OFFSET(w_expr),    AV_OPT_TYPE_STRING, { .str="0" },       0, 0, FLAGS },
    { "w",         "set width of the box",                         OFFSET(w_expr),    AV_OPT_TYPE_STRING, { .str="0" },       0, 0, FLAGS },
    { "height",    "set height of the box",                        OFFSET(h_expr),    AV_OPT_TYPE_STRING, { .str="0" },       0, 0, FLAGS },
    { "h",         "set height of the box",                        OFFSET(h_expr),    AV_OPT_TYPE_STRING, { .str="0" },       0, 0, FLAGS },
    { "color",     "set color of the box",                         OFFSET(color_str), AV_OPT_TYPE_STRING, { .str = "black" }, 0, 0, FLAGS },
    { "c",         "set color of the box",                         OFFSET(color_str), AV_OPT_TYPE_STRING, { .str = "black" }, 0, 0, FLAGS },
    { "thickness", "set the box thickness",                        OFFSET(t_expr),    AV_OPT_TYPE_STRING, { .str="3" },       0, 0, FLAGS },
    { "t",         "set the box thickness",                        OFFSET(t_expr),    AV_OPT_TYPE_STRING, { .str="3" },       0, 0, FLAGS },
    { "replace",   "replace color & alpha",                        OFFSET(replace),   AV_OPT_TYPE_BOOL,   { .i64=0   },       0, 1, FLAGS },
    { "box_source", "use datas from bounding box in side data",    OFFSET(box_source_string), AV_OPT_TYPE_STRING, { .str=NULL }, 0, 1, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(drawbox);

static const AVFilterPad drawbox_inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_VIDEO,
        .flags          = AVFILTERPAD_FLAG_NEEDS_WRITABLE,
        .config_props   = config_input,
        .filter_frame   = filter_frame,
    },
};

static const AVFilterPad drawbox_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
};

const AVFilter ff_vf_drawbox = {
    .name          = "drawbox",
    .description   = NULL_IF_CONFIG_SMALL("Draw a colored box on the input video."),
    .priv_size     = sizeof(DrawBoxContext),
    .priv_class    = &drawbox_class,
    .init          = init,
    FILTER_INPUTS(drawbox_inputs),
    FILTER_OUTPUTS(drawbox_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .process_command = process_command,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
#endif /* CONFIG_DRAWBOX_FILTER */

#if CONFIG_DRAWGRID_FILTER
static av_pure av_always_inline int pixel_belongs_to_grid(DrawBoxContext *drawgrid, int x, int y)
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

static int drawgrid_filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    DrawBoxContext *drawgrid = inlink->dst->priv;

    drawgrid->draw_region(frame, drawgrid, 0, 0, frame->width, frame->height, pixel_belongs_to_grid);

    return ff_filter_frame(inlink->dst->outputs[0], frame);
}

static const AVOption drawgrid_options[] = {
    { "x",         "set horizontal offset",   OFFSET(x_expr),    AV_OPT_TYPE_STRING, { .str="0" },       0, 0, FLAGS },
    { "y",         "set vertical offset",     OFFSET(y_expr),    AV_OPT_TYPE_STRING, { .str="0" },       0, 0, FLAGS },
    { "width",     "set width of grid cell",  OFFSET(w_expr),    AV_OPT_TYPE_STRING, { .str="0" },       0, 0, FLAGS },
    { "w",         "set width of grid cell",  OFFSET(w_expr),    AV_OPT_TYPE_STRING, { .str="0" },       0, 0, FLAGS },
    { "height",    "set height of grid cell", OFFSET(h_expr),    AV_OPT_TYPE_STRING, { .str="0" },       0, 0, FLAGS },
    { "h",         "set height of grid cell", OFFSET(h_expr),    AV_OPT_TYPE_STRING, { .str="0" },       0, 0, FLAGS },
    { "color",     "set color of the grid",   OFFSET(color_str), AV_OPT_TYPE_STRING, { .str = "black" }, 0, 0, FLAGS },
    { "c",         "set color of the grid",   OFFSET(color_str), AV_OPT_TYPE_STRING, { .str = "black" }, 0, 0, FLAGS },
    { "thickness", "set grid line thickness", OFFSET(t_expr),    AV_OPT_TYPE_STRING, {.str="1"},         0, 0, FLAGS },
    { "t",         "set grid line thickness", OFFSET(t_expr),    AV_OPT_TYPE_STRING, {.str="1"},         0, 0, FLAGS },
    { "replace",   "replace color & alpha",   OFFSET(replace),   AV_OPT_TYPE_BOOL,   { .i64=0 },         0,        1,        FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(drawgrid);

static const AVFilterPad drawgrid_inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_VIDEO,
        .flags          = AVFILTERPAD_FLAG_NEEDS_WRITABLE,
        .config_props   = config_input,
        .filter_frame   = drawgrid_filter_frame,
    },
};

static const AVFilterPad drawgrid_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
};

const AVFilter ff_vf_drawgrid = {
    .name          = "drawgrid",
    .description   = NULL_IF_CONFIG_SMALL("Draw a colored grid on the input video."),
    .priv_size     = sizeof(DrawBoxContext),
    .priv_class    = &drawgrid_class,
    .init          = init,
    FILTER_INPUTS(drawgrid_inputs),
    FILTER_OUTPUTS(drawgrid_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
    .process_command = process_command,
};

#endif  /* CONFIG_DRAWGRID_FILTER */
