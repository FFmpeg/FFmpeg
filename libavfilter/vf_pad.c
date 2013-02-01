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
#include "libavutil/opt.h"
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
    const AVClass *class;
    int w, h;               ///< output dimensions, a value of 0 will result in the input size
    int x, y;               ///< offsets of the input area with respect to the padded area
    int in_w, in_h;         ///< width and height for the padded input video, which has to be aligned to the chroma values in order to avoid chroma issues

    char *w_expr;       ///< width  expression string
    char *h_expr;       ///< height expression string
    char *x_expr;       ///< width  expression string
    char *y_expr;       ///< height expression string
    char *color_str;
    uint8_t rgba_color[4];  ///< color for the padding area
    FFDrawContext draw;
    FFDrawColor color;
} PadContext;

#define OFFSET(x) offsetof(PadContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption pad_options[] = {
    { "width",  "set the pad area width expression",       OFFSET(w_expr), AV_OPT_TYPE_STRING, {.str = "iw"}, CHAR_MIN, CHAR_MAX, FLAGS },
    { "w",      "set the pad area width expression",       OFFSET(w_expr), AV_OPT_TYPE_STRING, {.str = "iw"}, CHAR_MIN, CHAR_MAX, FLAGS },
    { "height", "set the pad area height expression",      OFFSET(h_expr), AV_OPT_TYPE_STRING, {.str = "ih"}, CHAR_MIN, CHAR_MAX, FLAGS },
    { "h",      "set the pad area height expression",      OFFSET(h_expr), AV_OPT_TYPE_STRING, {.str = "ih"}, CHAR_MIN, CHAR_MAX, FLAGS },
    { "x",      "set the x offset expression for the input image position", OFFSET(x_expr), AV_OPT_TYPE_STRING, {.str = "0"}, CHAR_MIN, CHAR_MAX, FLAGS },
    { "y",      "set the y offset expression for the input image position", OFFSET(y_expr), AV_OPT_TYPE_STRING, {.str = "0"}, CHAR_MIN, CHAR_MAX, FLAGS },
    { "color",  "set the color of the padded area border", OFFSET(color_str), AV_OPT_TYPE_STRING, {.str = "black"}, .flags = FLAGS },
    {NULL}
};

AVFILTER_DEFINE_CLASS(pad);

static av_cold int init(AVFilterContext *ctx, const char *args)
{
    PadContext *pad = ctx->priv;
    static const char *shorthand[] = { "width", "height", "x", "y", "color", NULL };
    int ret;

    pad->class = &pad_class;
    av_opt_set_defaults(pad);

    if ((ret = av_opt_set_from_string(pad, args, shorthand, "=", ":")) < 0)
        return ret;

    if (av_parse_color(pad->rgba_color, pad->color_str, -1, ctx) < 0)
        return AVERROR(EINVAL);

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    PadContext *pad = ctx->priv;
    av_opt_free(pad);
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
    var_values[VAR_A]     = (double) inlink->w / inlink->h;
    var_values[VAR_SAR]   = inlink->sample_aspect_ratio.num ?
        (double) inlink->sample_aspect_ratio.num / inlink->sample_aspect_ratio.den : 1;
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

static int filter_frame(AVFilterLink *inlink, AVFilterBufferRef *in)
{
    PadContext *pad = inlink->dst->priv;
    AVFilterBufferRef *out = avfilter_ref_buffer(in, ~0);
    int plane, needs_copy;

    if (!out) {
        avfilter_unref_bufferp(&in);
        return AVERROR(ENOMEM);
    }

    for (plane = 0; plane < 4 && out->data[plane] && pad->draw.pixelstep[plane]; plane++) {
        int hsub = pad->draw.hsub[plane];
        int vsub = pad->draw.vsub[plane];

        av_assert0(out->buf->w > 0 && out->buf->h > 0);

        if (out->format != out->buf->format) //unsupported currently
            break;

        out->data[plane] -= (pad->x  >> hsub) * pad->draw.pixelstep[plane] +
                            (pad->y  >> vsub) * out->linesize[plane];

        if (does_clip(pad, out, plane, hsub, vsub, 0,                   0) ||
            does_clip(pad, out, plane, hsub, vsub, 0,          pad->h - 1) ||
            does_clip(pad, out, plane, hsub, vsub, pad->w - 1,          0) ||
            does_clip(pad, out, plane, hsub, vsub, pad->w - 1, pad->h - 1))
            break;
    }
    needs_copy = plane < 4 && out->data[plane] || !(out->perms & AV_PERM_WRITE);
    if (needs_copy) {
        av_log(inlink->dst, AV_LOG_DEBUG, "Direct padding impossible allocating new frame\n");
        avfilter_unref_buffer(out);
        out = ff_get_video_buffer(inlink->dst->outputs[0], AV_PERM_WRITE | AV_PERM_NEG_LINESIZES,
                                  FFMAX(inlink->w, pad->w),
                                  FFMAX(inlink->h, pad->h));
        if (!out) {
            avfilter_unref_bufferp(&in);
            return AVERROR(ENOMEM);
        }

        avfilter_copy_buffer_ref_props(out, in);
    }

    out->video->w = pad->w;
    out->video->h = pad->h;

    /* top bar */
    if (pad->y) {
        ff_fill_rectangle(&pad->draw, &pad->color,
                          out->data, out->linesize,
                          0, 0, pad->w, pad->y);
    }

    /* bottom bar */
    if (pad->h > pad->y + pad->in_h) {
        ff_fill_rectangle(&pad->draw, &pad->color,
                          out->data, out->linesize,
                          0, pad->y + pad->in_h, pad->w, pad->h - pad->y - pad->in_h);
    }

    /* left border */
    ff_fill_rectangle(&pad->draw, &pad->color, out->data, out->linesize,
                      0, pad->y, pad->x, in->video->h);

    if (needs_copy) {
        ff_copy_rectangle2(&pad->draw,
                          out->data, out->linesize, in->data, in->linesize,
                          pad->x, pad->y, 0, 0, in->video->w, in->video->h);
    }

    /* right border */
    ff_fill_rectangle(&pad->draw, &pad->color, out->data, out->linesize,
                      pad->x + pad->in_w, pad->y, pad->w - pad->x - pad->in_w,
                      in->video->h);

    avfilter_unref_bufferp(&in);
    return ff_filter_frame(inlink->dst->outputs[0], out);
}

static const AVFilterPad avfilter_vf_pad_inputs[] = {
    {
        .name             = "default",
        .type             = AVMEDIA_TYPE_VIDEO,
        .config_props     = config_input,
        .get_video_buffer = get_video_buffer,
        .filter_frame     = filter_frame,
    },
    { NULL }
};

static const AVFilterPad avfilter_vf_pad_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_output,
    },
    { NULL }
};

AVFilter avfilter_vf_pad = {
    .name          = "pad",
    .description   = NULL_IF_CONFIG_SMALL("Pad input image to width:height[:x:y[:color]] (default x and y: 0, default color: black)."),

    .priv_size     = sizeof(PadContext),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,

    .inputs    = avfilter_vf_pad_inputs,

    .outputs   = avfilter_vf_pad_outputs,
    .priv_class = &pad_class,
};
