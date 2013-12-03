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
    uint8_t rgba_color[4];  ///< color for the padding area
    FFDrawContext draw;
    FFDrawColor color;
} PadContext;

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    PadContext *s = ctx->priv;
    int ret;
    double var_values[VARS_NB], res;
    char *expr;

    ff_draw_init(&s->draw, inlink->format, 0);
    ff_draw_color(&s->draw, &s->color, s->rgba_color);

    var_values[VAR_IN_W]  = var_values[VAR_IW] = inlink->w;
    var_values[VAR_IN_H]  = var_values[VAR_IH] = inlink->h;
    var_values[VAR_OUT_W] = var_values[VAR_OW] = NAN;
    var_values[VAR_OUT_H] = var_values[VAR_OH] = NAN;
    var_values[VAR_A]     = (double) inlink->w / inlink->h;
    var_values[VAR_SAR]   = inlink->sample_aspect_ratio.num ?
        (double) inlink->sample_aspect_ratio.num / inlink->sample_aspect_ratio.den : 1;
    var_values[VAR_DAR]   = var_values[VAR_A] * var_values[VAR_SAR];
    var_values[VAR_HSUB]  = 1 << s->draw.hsub_max;
    var_values[VAR_VSUB]  = 1 << s->draw.vsub_max;

    /* evaluate width and height */
    av_expr_parse_and_eval(&res, (expr = s->w_expr),
                           var_names, var_values,
                           NULL, NULL, NULL, NULL, NULL, 0, ctx);
    s->w = var_values[VAR_OUT_W] = var_values[VAR_OW] = res;
    if ((ret = av_expr_parse_and_eval(&res, (expr = s->h_expr),
                                      var_names, var_values,
                                      NULL, NULL, NULL, NULL, NULL, 0, ctx)) < 0)
        goto eval_fail;
    s->h = var_values[VAR_OUT_H] = var_values[VAR_OH] = res;
    /* evaluate the width again, as it may depend on the evaluated output height */
    if ((ret = av_expr_parse_and_eval(&res, (expr = s->w_expr),
                                      var_names, var_values,
                                      NULL, NULL, NULL, NULL, NULL, 0, ctx)) < 0)
        goto eval_fail;
    s->w = var_values[VAR_OUT_W] = var_values[VAR_OW] = res;

    /* evaluate x and y */
    av_expr_parse_and_eval(&res, (expr = s->x_expr),
                           var_names, var_values,
                           NULL, NULL, NULL, NULL, NULL, 0, ctx);
    s->x = var_values[VAR_X] = res;
    if ((ret = av_expr_parse_and_eval(&res, (expr = s->y_expr),
                                      var_names, var_values,
                                      NULL, NULL, NULL, NULL, NULL, 0, ctx)) < 0)
        goto eval_fail;
    s->y = var_values[VAR_Y] = res;
    /* evaluate x again, as it may depend on the evaluated y value */
    if ((ret = av_expr_parse_and_eval(&res, (expr = s->x_expr),
                                      var_names, var_values,
                                      NULL, NULL, NULL, NULL, NULL, 0, ctx)) < 0)
        goto eval_fail;
    s->x = var_values[VAR_X] = res;

    /* sanity check params */
    if (s->w < 0 || s->h < 0 || s->x < 0 || s->y < 0) {
        av_log(ctx, AV_LOG_ERROR, "Negative values are not acceptable.\n");
        return AVERROR(EINVAL);
    }

    if (!s->w)
        s->w = inlink->w;
    if (!s->h)
        s->h = inlink->h;

    s->w    = ff_draw_round_to_sub(&s->draw, 0, -1, s->w);
    s->h    = ff_draw_round_to_sub(&s->draw, 1, -1, s->h);
    s->x    = ff_draw_round_to_sub(&s->draw, 0, -1, s->x);
    s->y    = ff_draw_round_to_sub(&s->draw, 1, -1, s->y);
    s->in_w = ff_draw_round_to_sub(&s->draw, 0, -1, inlink->w);
    s->in_h = ff_draw_round_to_sub(&s->draw, 1, -1, inlink->h);

    av_log(ctx, AV_LOG_VERBOSE, "w:%d h:%d -> w:%d h:%d x:%d y:%d color:0x%02X%02X%02X%02X\n",
           inlink->w, inlink->h, s->w, s->h, s->x, s->y,
           s->rgba_color[0], s->rgba_color[1], s->rgba_color[2], s->rgba_color[3]);

    if (s->x <  0 || s->y <  0                      ||
        s->w <= 0 || s->h <= 0                      ||
        (unsigned)s->x + (unsigned)inlink->w > s->w ||
        (unsigned)s->y + (unsigned)inlink->h > s->h) {
        av_log(ctx, AV_LOG_ERROR,
               "Input area %d:%d:%d:%d not within the padded area 0:0:%d:%d or zero-sized\n",
               s->x, s->y, s->x + inlink->w, s->y + inlink->h, s->w, s->h);
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
    PadContext *s = outlink->src->priv;

    outlink->w = s->w;
    outlink->h = s->h;
    return 0;
}

static AVFrame *get_video_buffer(AVFilterLink *inlink, int w, int h)
{
    PadContext *s = inlink->dst->priv;

    AVFrame *frame = ff_get_video_buffer(inlink->dst->outputs[0],
                                         w + (s->w - s->in_w),
                                         h + (s->h - s->in_h));
    int plane;

    if (!frame)
        return NULL;

    frame->width  = w;
    frame->height = h;

    for (plane = 0; plane < 4 && frame->data[plane] && frame->linesize[plane]; plane++) {
        int hsub = s->draw.hsub[plane];
        int vsub = s->draw.vsub[plane];
        frame->data[plane] += (s->x >> hsub) * s->draw.pixelstep[plane] +
                              (s->y >> vsub) * frame->linesize[plane];
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
                              ((s->h - s->y - frame->height) >> vsub) * frame->linesize[planes[i]];

        if (frame->linesize[planes[i]] < (s->w >> hsub) * s->draw.pixelstep[planes[i]])
            return 1;
        if (start - buf->data < req_start ||
            (buf->data + buf->size) - end < req_end)
            return 1;

        for (j = 0; j < FF_ARRAY_ELEMS(planes) && planes[j] >= 0; j++) {
            int vsub1 = s->draw.vsub[planes[j]];
            uint8_t *start1 = frame->data[planes[j]];
            uint8_t *end1   = start1 + (frame->height >> vsub1) *
                                       frame->linesize[planes[j]];
            if (i == j)
                continue;

            if (FFSIGN(start - end1) != FFSIGN(start - end1 - req_start) ||
                FFSIGN(end - start1) != FFSIGN(end - start1 + req_end))
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
    PadContext *s = inlink->dst->priv;
    AVFrame *out;
    int needs_copy = frame_needs_copy(s, in);

    if (needs_copy) {
        av_log(inlink->dst, AV_LOG_DEBUG, "Direct padding impossible allocating new frame\n");
        out = ff_get_video_buffer(inlink->dst->outputs[0],
                                  FFMAX(inlink->w, s->w),
                                  FFMAX(inlink->h, s->h));
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }

        av_frame_copy_props(out, in);
    } else {
        int i;

        out = in;
        for (i = 0; i < 4 && out->data[i] && out->linesize[i]; i++) {
            int hsub = s->draw.hsub[i];
            int vsub = s->draw.vsub[i];
            out->data[i] -= (s->x >> hsub) * s->draw.pixelstep[i] +
                            (s->y >> vsub) * out->linesize[i];
        }
    }

    /* top bar */
    if (s->y) {
        ff_fill_rectangle(&s->draw, &s->color,
                          out->data, out->linesize,
                          0, 0, s->w, s->y);
    }

    /* bottom bar */
    if (s->h > s->y + s->in_h) {
        ff_fill_rectangle(&s->draw, &s->color,
                          out->data, out->linesize,
                          0, s->y + s->in_h, s->w, s->h - s->y - s->in_h);
    }

    /* left border */
    ff_fill_rectangle(&s->draw, &s->color, out->data, out->linesize,
                      0, s->y, s->x, in->height);

    if (needs_copy) {
        ff_copy_rectangle2(&s->draw,
                          out->data, out->linesize, in->data, in->linesize,
                          s->x, s->y, 0, 0, in->width, in->height);
    }

    /* right border */
    ff_fill_rectangle(&s->draw, &s->color, out->data, out->linesize,
                      s->x + s->in_w, s->y, s->w - s->x - s->in_w,
                      in->height);

    out->width  = s->w;
    out->height = s->h;

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
    { "color",  "set the color of the padded area border", OFFSET(rgba_color), AV_OPT_TYPE_COLOR, {.str = "black"}, .flags = FLAGS },
    { NULL }
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
    .description   = NULL_IF_CONFIG_SMALL("Pad the input video."),
    .priv_size     = sizeof(PadContext),
    .priv_class    = &pad_class,
    .query_formats = query_formats,
    .inputs        = avfilter_vf_pad_inputs,
    .outputs       = avfilter_vf_pad_outputs,
};
