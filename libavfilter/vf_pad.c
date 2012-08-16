/*
 * Copyright (c) 2008 vmrsss
 * Copyright (c) 2009 Stefano Sabatini
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
 * video padding filter
 */

#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"
#include "libavutil/avstring.h"
#include "libavutil/common.h"
#include "libavutil/eval.h"
#include "libavutil/pixdesc.h"
#include "libavutil/colorspace.h"
#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/parseutils.h"
#include "libavutil/mathematics.h"
#include "drawutils.h"

static const char *const var_names[] = {
    "in_w",   "iw",
    "in_h",   "ih",
    "out_w",  "ow",
    "out_h",  "oh",
    "x",
    "y",
    "a",
    "sar",
    "dar",
    "hsub",
    "vsub",
    NULL
};

enum var_name {
    VAR_IN_W,   VAR_IW,
    VAR_IN_H,   VAR_IH,
    VAR_OUT_W,  VAR_OW,
    VAR_OUT_H,  VAR_OH,
    VAR_X,
    VAR_Y,
    VAR_A,
    VAR_SAR,
    VAR_DAR,
    VAR_HSUB,
    VAR_VSUB,
    VARS_NB
};

static int query_formats(AVFilterContext *ctx)
{
    ff_set_common_formats(ctx, ff_draw_supported_pixel_formats(0));
    return 0;
}

typedef struct {
    int w, h;               ///< output dimensions, a value of 0 will result in the input size
    int x, y;               ///< offsets of the input area with respect to the padded area
    int in_w, in_h;         ///< width and height for the padded input video, which has to be aligned to the chroma values in order to avoid chroma issues

    char w_expr[256];       ///< width  expression string
    char h_expr[256];       ///< height expression string
    char x_expr[256];       ///< width  expression string
    char y_expr[256];       ///< height expression string

    uint8_t rgba_color[4];  ///< color for the padding area
    FFDrawContext draw;
    FFDrawColor color;
    int needs_copy;
} PadContext;

static av_cold int init(AVFilterContext *ctx, const char *args)
{
    PadContext *pad = ctx->priv;
    char color_string[128] = "black";

    av_strlcpy(pad->w_expr, "iw", sizeof(pad->w_expr));
    av_strlcpy(pad->h_expr, "ih", sizeof(pad->h_expr));
    av_strlcpy(pad->x_expr, "0" , sizeof(pad->w_expr));
    av_strlcpy(pad->y_expr, "0" , sizeof(pad->h_expr));

    if (args)
        sscanf(args, "%255[^:]:%255[^:]:%255[^:]:%255[^:]:%127s",
               pad->w_expr, pad->h_expr, pad->x_expr, pad->y_expr, color_string);

    if (av_parse_color(pad->rgba_color, color_string, -1, ctx) < 0)
        return AVERROR(EINVAL);

    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    PadContext *pad = ctx->priv;
    int ret;
    double var_values[VARS_NB], res;
    char *expr;

    ff_draw_init(&pad->draw, inlink->format, 0);
    ff_draw_color(&pad->draw, &pad->color, pad->rgba_color);

    var_values[VAR_IN_W]  = var_values[VAR_IW] = inlink->w;
    var_values[VAR_IN_H]  = var_values[VAR_IH] = inlink->h;
    var_values[VAR_OUT_W] = var_values[VAR_OW] = NAN;
    var_values[VAR_OUT_H] = var_values[VAR_OH] = NAN;
    var_values[VAR_A]     = (float) inlink->w / inlink->h;
    var_values[VAR_SAR]   = inlink->sample_aspect_ratio.num ?
        (float) inlink->sample_aspect_ratio.num / inlink->sample_aspect_ratio.den : 1;
    var_values[VAR_DAR]   = var_values[VAR_A] * var_values[VAR_SAR];
    var_values[VAR_HSUB]  = 1 << pad->draw.hsub_max;
    var_values[VAR_VSUB]  = 1 << pad->draw.vsub_max;

    /* evaluate width and height */
    av_expr_parse_and_eval(&res, (expr = pad->w_expr),
                           var_names, var_values,
                           NULL, NULL, NULL, NULL, NULL, 0, ctx);
    pad->w = var_values[VAR_OUT_W] = var_values[VAR_OW] = res;
    if ((ret = av_expr_parse_and_eval(&res, (expr = pad->h_expr),
                                      var_names, var_values,
                                      NULL, NULL, NULL, NULL, NULL, 0, ctx)) < 0)
        goto eval_fail;
    pad->h = var_values[VAR_OUT_H] = var_values[VAR_OH] = res;
    /* evaluate the width again, as it may depend on the evaluated output height */
    if ((ret = av_expr_parse_and_eval(&res, (expr = pad->w_expr),
                                      var_names, var_values,
                                      NULL, NULL, NULL, NULL, NULL, 0, ctx)) < 0)
        goto eval_fail;
    pad->w = var_values[VAR_OUT_W] = var_values[VAR_OW] = res;

    /* evaluate x and y */
    av_expr_parse_and_eval(&res, (expr = pad->x_expr),
                           var_names, var_values,
                           NULL, NULL, NULL, NULL, NULL, 0, ctx);
    pad->x = var_values[VAR_X] = res;
    if ((ret = av_expr_parse_and_eval(&res, (expr = pad->y_expr),
                                      var_names, var_values,
                                      NULL, NULL, NULL, NULL, NULL, 0, ctx)) < 0)
        goto eval_fail;
    pad->y = var_values[VAR_Y] = res;
    /* evaluate x again, as it may depend on the evaluated y value */
    if ((ret = av_expr_parse_and_eval(&res, (expr = pad->x_expr),
                                      var_names, var_values,
                                      NULL, NULL, NULL, NULL, NULL, 0, ctx)) < 0)
        goto eval_fail;
    pad->x = var_values[VAR_X] = res;

    /* sanity check params */
    if (pad->w < 0 || pad->h < 0 || pad->x < 0 || pad->y < 0) {
        av_log(ctx, AV_LOG_ERROR, "Negative values are not acceptable.\n");
        return AVERROR(EINVAL);
    }

    if (!pad->w)
        pad->w = inlink->w;
    if (!pad->h)
        pad->h = inlink->h;

    pad->w    = ff_draw_round_to_sub(&pad->draw, 0, -1, pad->w);
    pad->h    = ff_draw_round_to_sub(&pad->draw, 1, -1, pad->h);
    pad->x    = ff_draw_round_to_sub(&pad->draw, 0, -1, pad->x);
    pad->y    = ff_draw_round_to_sub(&pad->draw, 1, -1, pad->y);
    pad->in_w = ff_draw_round_to_sub(&pad->draw, 0, -1, inlink->w);
    pad->in_h = ff_draw_round_to_sub(&pad->draw, 1, -1, inlink->h);

    av_log(ctx, AV_LOG_VERBOSE, "w:%d h:%d -> w:%d h:%d x:%d y:%d color:0x%02X%02X%02X%02X\n",
           inlink->w, inlink->h, pad->w, pad->h, pad->x, pad->y,
           pad->rgba_color[0], pad->rgba_color[1], pad->rgba_color[2], pad->rgba_color[3]);

    if (pad->x <  0 || pad->y <  0                      ||
        pad->w <= 0 || pad->h <= 0                      ||
        (unsigned)pad->x + (unsigned)inlink->w > pad->w ||
        (unsigned)pad->y + (unsigned)inlink->h > pad->h) {
        av_log(ctx, AV_LOG_ERROR,
               "Input area %d:%d:%d:%d not within the padded area 0:0:%d:%d or zero-sized\n",
               pad->x, pad->y, pad->x + inlink->w, pad->y + inlink->h, pad->w, pad->h);
        return AVERROR(EINVAL);
    }

    return 0;

eval_fail:
    av_log(NULL, AV_LOG_ERROR,
           "Error when evaluating the expression '%s'\n", expr);
    return ret;

}

static int config_output(AVFilterLink *outlink)
{
    PadContext *pad = outlink->src->priv;

    outlink->w = pad->w;
    outlink->h = pad->h;
    return 0;
}

static AVFilterBufferRef *get_video_buffer(AVFilterLink *inlink, int perms, int w, int h)
{
    PadContext *pad = inlink->dst->priv;
    int align = (perms&AV_PERM_ALIGN) ? AVFILTER_ALIGN : 1;

    AVFilterBufferRef *picref = ff_get_video_buffer(inlink->dst->outputs[0], perms,
                                                    w + (pad->w - pad->in_w) + 4*align,
                                                    h + (pad->h - pad->in_h));
    int plane;

    if (!picref)
        return NULL;

    picref->video->w = w;
    picref->video->h = h;

    for (plane = 0; plane < 4 && picref->data[plane]; plane++)
        picref->data[plane] += FFALIGN(pad->x >> pad->draw.hsub[plane], align) * pad->draw.pixelstep[plane] +
                                      (pad->y >> pad->draw.vsub[plane])        * picref->linesize[plane];

    return picref;
}

static int does_clip(PadContext *pad, AVFilterBufferRef *outpicref, int plane, int hsub, int vsub, int x, int y)
{
    int64_t x_in_buf, y_in_buf;

    x_in_buf =  outpicref->data[plane] - outpicref->buf->data[plane]
             +  (x >> hsub) * pad->draw.pixelstep[plane]
             +  (y >> vsub) * outpicref->linesize[plane];

    if(x_in_buf < 0 || x_in_buf % pad->draw.pixelstep[plane])
        return 1;
    x_in_buf /= pad->draw.pixelstep[plane];

    av_assert0(outpicref->buf->linesize[plane]>0); //while reference can use negative linesize the main buffer should not

    y_in_buf = x_in_buf / outpicref->buf->linesize[plane];
    x_in_buf %= outpicref->buf->linesize[plane];

    if(   y_in_buf<<vsub >= outpicref->buf->h
       || x_in_buf<<hsub >= outpicref->buf->w)
        return 1;
    return 0;
}

static int start_frame(AVFilterLink *inlink, AVFilterBufferRef *inpicref)
{
    PadContext *pad = inlink->dst->priv;
    AVFilterBufferRef *outpicref = avfilter_ref_buffer(inpicref, ~0);
    AVFilterBufferRef *for_next_filter;
    int plane, ret = 0;

    if (!outpicref)
        return AVERROR(ENOMEM);

    for (plane = 0; plane < 4 && outpicref->data[plane] && pad->draw.pixelstep[plane]; plane++) {
        int hsub = pad->draw.hsub[plane];
        int vsub = pad->draw.vsub[plane];

        av_assert0(outpicref->buf->w>0 && outpicref->buf->h>0);

        if(outpicref->format != outpicref->buf->format) //unsupported currently
            break;

        outpicref->data[plane] -=   (pad->x  >> hsub) * pad->draw.pixelstep[plane]
                                  + (pad->y  >> vsub) * outpicref->linesize[plane];

        if(   does_clip(pad, outpicref, plane, hsub, vsub, 0, 0)
           || does_clip(pad, outpicref, plane, hsub, vsub, 0, pad->h-1)
           || does_clip(pad, outpicref, plane, hsub, vsub, pad->w-1, 0)
           || does_clip(pad, outpicref, plane, hsub, vsub, pad->w-1, pad->h-1)
          )
            break;
    }
    pad->needs_copy= plane < 4 && outpicref->data[plane] || !(outpicref->perms & AV_PERM_WRITE);
    if(pad->needs_copy){
        av_log(inlink->dst, AV_LOG_DEBUG, "Direct padding impossible allocating new frame\n");
        avfilter_unref_buffer(outpicref);
        outpicref = ff_get_video_buffer(inlink->dst->outputs[0], AV_PERM_WRITE | AV_PERM_NEG_LINESIZES,
                                        FFMAX(inlink->w, pad->w),
                                        FFMAX(inlink->h, pad->h));
        if (!outpicref)
            return AVERROR(ENOMEM);

        avfilter_copy_buffer_ref_props(outpicref, inpicref);
    }

    outpicref->video->w = pad->w;
    outpicref->video->h = pad->h;

    for_next_filter = avfilter_ref_buffer(outpicref, ~0);
    if (!for_next_filter) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    ret = ff_start_frame(inlink->dst->outputs[0], for_next_filter);
    if (ret < 0)
        goto fail;

    inlink->dst->outputs[0]->out_buf = outpicref;
    return 0;

fail:
    avfilter_unref_bufferp(&outpicref);
    return ret;
}

static int draw_send_bar_slice(AVFilterLink *link, int y, int h, int slice_dir, int before_slice)
{
    PadContext *pad = link->dst->priv;
    int bar_y, bar_h = 0, ret = 0;

    if        (slice_dir * before_slice ==  1 && y == pad->y) {
        /* top bar */
        bar_y = 0;
        bar_h = pad->y;
    } else if (slice_dir * before_slice == -1 && (y + h) == (pad->y + pad->in_h)) {
        /* bottom bar */
        bar_y = pad->y + pad->in_h;
        bar_h = pad->h - pad->in_h - pad->y;
    }

    if (bar_h) {
        ff_fill_rectangle(&pad->draw, &pad->color,
                          link->dst->outputs[0]->out_buf->data,
                          link->dst->outputs[0]->out_buf->linesize,
                          0, bar_y, pad->w, bar_h);
        ret = ff_draw_slice(link->dst->outputs[0], bar_y, bar_h, slice_dir);
    }
    return ret;
}

static int draw_slice(AVFilterLink *link, int y, int h, int slice_dir)
{
    PadContext *pad = link->dst->priv;
    AVFilterBufferRef *outpic = link->dst->outputs[0]->out_buf;
    AVFilterBufferRef *inpic = link->cur_buf;
    int ret;

    y += pad->y;

    y = ff_draw_round_to_sub(&pad->draw, 1, -1, y);
    h = ff_draw_round_to_sub(&pad->draw, 1, -1, h);

    if (!h)
        return 0;
    draw_send_bar_slice(link, y, h, slice_dir, 1);

    /* left border */
    ff_fill_rectangle(&pad->draw, &pad->color, outpic->data, outpic->linesize,
                      0, y, pad->x, h);

    if(pad->needs_copy){
        ff_copy_rectangle2(&pad->draw,
                           outpic->data, outpic->linesize,
                           inpic ->data, inpic ->linesize,
                           pad->x, y, 0, y - pad->y, inpic->video->w, h);
    }

    /* right border */
    ff_fill_rectangle(&pad->draw, &pad->color, outpic->data, outpic->linesize,
                      pad->x + pad->in_w, y, pad->w - pad->x - pad->in_w, h);
    ret = ff_draw_slice(link->dst->outputs[0], y, h, slice_dir);
    if (ret < 0)
        return ret;

    return draw_send_bar_slice(link, y, h, slice_dir, -1);
}

AVFilter avfilter_vf_pad = {
    .name          = "pad",
    .description   = NULL_IF_CONFIG_SMALL("Pad input image to width:height[:x:y[:color]] (default x and y: 0, default color: black)."),

    .priv_size     = sizeof(PadContext),
    .init          = init,
    .query_formats = query_formats,

    .inputs    = (const AVFilterPad[]) {{ .name             = "default",
                                          .type             = AVMEDIA_TYPE_VIDEO,
                                          .config_props     = config_input,
                                          .get_video_buffer = get_video_buffer,
                                          .start_frame      = start_frame,
                                          .draw_slice       = draw_slice, },
                                        { .name = NULL}},

    .outputs   = (const AVFilterPad[]) {{ .name             = "default",
                                          .type             = AVMEDIA_TYPE_VIDEO,
                                          .config_props     = config_output, },
                                        { .name = NULL}},
};
