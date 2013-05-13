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
#include "libavutil/opt.h"

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

    char *w_expr;           ///< width  expression string
    char *h_expr;           ///< height expression string
    char *x_expr;           ///< width  expression string
    char *y_expr;           ///< height expression string
    char *color_str;
    uint8_t rgba_color[4];  ///< color for the padding area
    FFDrawContext draw;
    FFDrawColor color;
} PadContext;

static av_cold int init(AVFilterContext *ctx)
{
    PadContext *pad = ctx->priv;

    if (av_parse_color(pad->rgba_color, pad->color_str, -1, ctx) < 0)
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

static AVFrame *get_video_buffer(AVFilterLink *inlink, int w, int h)
{
    PadContext *pad = inlink->dst->priv;

    AVFrame *frame = ff_get_video_buffer(inlink->dst->outputs[0],
                                         w + (pad->w - pad->in_w),
                                         h + (pad->h - pad->in_h));
    int plane;

    if (!frame)
        return NULL;

    frame->width  = w;
    frame->height = h;

    for (plane = 0; plane < 4 && frame->data[plane]; plane++) {
        int hsub = pad->draw.hsub[plane];
        int vsub = pad->draw.vsub[plane];
        frame->data[plane] += (pad->x >> hsub) * pad->draw.pixelstep[plane] +
                              (pad->y >> vsub) * frame->linesize[plane];
    }

    return frame;
}

/* check whether each plane in this buffer can be padded without copying */
static int buffer_needs_copy(PadContext *s, AVFrame *frame, AVBufferRef *buf)
{
    int planes[4] = { -1, -1, -1, -1}, *p = planes;
    int i, j;

    /* get all planes in this buffer */
    for (i = 0; i < FF_ARRAY_ELEMS(planes) && frame->data[i]; i++) {
        if (av_frame_get_plane_buffer(frame, i) == buf)
            *p++ = i;
    }

    /* for each plane in this buffer, check that it can be padded without
     * going over buffer bounds or other planes */
    for (i = 0; i < FF_ARRAY_ELEMS(planes) && planes[i] >= 0; i++) {
        int hsub = s->draw.hsub[planes[i]];
        int vsub = s->draw.vsub[planes[i]];

        uint8_t *start = frame->data[planes[i]];
        uint8_t *end   = start + (frame->height >> vsub) *
                                 frame->linesize[planes[i]];

        /* amount of free space needed before the start and after the end
         * of the plane */
        ptrdiff_t req_start = (s->x >> hsub) * s->draw.pixelstep[planes[i]] +
                              (s->y >> vsub) * frame->linesize[planes[i]];
        ptrdiff_t req_end   = ((s->w - s->x - frame->width) >> hsub) *
                              s->draw.pixelstep[planes[i]] +
                              (s->y >> vsub) * frame->linesize[planes[i]];

        if (frame->linesize[planes[i]] < (s->w >> hsub) * s->draw.pixelstep[planes[i]])
            return 1;
        if (start - buf->data < req_start ||
            (buf->data + buf->size) - end < req_end)
            return 1;

#define SIGN(x) ((x) > 0 ? 1 : -1)
        for (j = 0; j < FF_ARRAY_ELEMS(planes) && planes[j] >= 0; j++) {
            int vsub1 = s->draw.vsub[planes[j]];
            uint8_t *start1 = frame->data[planes[j]];
            uint8_t *end1   = start1 + (frame->height >> vsub1) *
                                       frame->linesize[planes[j]];
            if (i == j)
                continue;

            if (SIGN(start - end1) != SIGN(start - end1 - req_start) ||
                SIGN(end - start1) != SIGN(end - start1 + req_end))
                return 1;
        }
    }

    return 0;
}

static int frame_needs_copy(PadContext *s, AVFrame *frame)
{
    int i;

    if (!av_frame_is_writable(frame))
        return 1;

    for (i = 0; i < 4 && frame->buf[i]; i++)
        if (buffer_needs_copy(s, frame, frame->buf[i]))
            return 1;
    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    PadContext *pad = inlink->dst->priv;
    AVFrame *out;
    int needs_copy = frame_needs_copy(pad, in);

    if (needs_copy) {
        av_log(inlink->dst, AV_LOG_DEBUG, "Direct padding impossible allocating new frame\n");
        out = ff_get_video_buffer(inlink->dst->outputs[0],
                                  FFMAX(inlink->w, pad->w),
                                  FFMAX(inlink->h, pad->h));
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }

        av_frame_copy_props(out, in);
    } else {
        int i;

        out = in;
        for (i = 0; i < 4 && out->data[i]; i++) {
            int hsub = pad->draw.hsub[i];
            int vsub = pad->draw.vsub[i];
            out->data[i] -= (pad->x >> hsub) * pad->draw.pixelstep[i] +
                            (pad->y >> vsub) * out->linesize[i];
        }
    }

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
                      0, pad->y, pad->x, in->height);

    if (needs_copy) {
        ff_copy_rectangle2(&pad->draw,
                          out->data, out->linesize, in->data, in->linesize,
                          pad->x, pad->y, 0, 0, in->width, in->height);
    }

    /* right border */
    ff_fill_rectangle(&pad->draw, &pad->color, out->data, out->linesize,
                      pad->x + pad->in_w, pad->y, pad->w - pad->x - pad->in_w,
                      in->height);

    out->width  = pad->w;
    out->height = pad->h;

    if (in != out)
        av_frame_free(&in);
    return ff_filter_frame(inlink->dst->outputs[0], out);
}

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
    { NULL },
};

AVFILTER_DEFINE_CLASS(pad);

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
    .priv_class    = &pad_class,
    .init          = init,
    .query_formats = query_formats,

    .inputs    = avfilter_vf_pad_inputs,

    .outputs   = avfilter_vf_pad_outputs,
};
