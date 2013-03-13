/*
 * Copyright (c) 2008 vmrsss
 * Copyright (c) 2009 Stefano Sabatini
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
    "PI",
    "PHI",
    "E",
    "in_w",   "iw",
    "in_h",   "ih",
    "out_w",  "ow",
    "out_h",  "oh",
    "x",
    "y",
    "a",
    "hsub",
    "vsub",
    NULL
};

enum var_name {
    VAR_PI,
    VAR_PHI,
    VAR_E,
    VAR_IN_W,   VAR_IW,
    VAR_IN_H,   VAR_IH,
    VAR_OUT_W,  VAR_OW,
    VAR_OUT_H,  VAR_OH,
    VAR_X,
    VAR_Y,
    VAR_A,
    VAR_HSUB,
    VAR_VSUB,
    VARS_NB
};

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_ARGB,         AV_PIX_FMT_RGBA,
        AV_PIX_FMT_ABGR,         AV_PIX_FMT_BGRA,
        AV_PIX_FMT_RGB24,        AV_PIX_FMT_BGR24,

        AV_PIX_FMT_YUV444P,      AV_PIX_FMT_YUV422P,
        AV_PIX_FMT_YUV420P,      AV_PIX_FMT_YUV411P,
        AV_PIX_FMT_YUV410P,      AV_PIX_FMT_YUV440P,
        AV_PIX_FMT_YUVJ444P,     AV_PIX_FMT_YUVJ422P,
        AV_PIX_FMT_YUVJ420P,     AV_PIX_FMT_YUVJ440P,
        AV_PIX_FMT_YUVA420P,

        AV_PIX_FMT_NONE
    };

    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
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

    uint8_t color[4];       ///< color expressed either in YUVA or RGBA colorspace for the padding area
    uint8_t *line[4];
    int      line_step[4];
    int hsub, vsub;         ///< chroma subsampling values
} PadContext;

static av_cold int init(AVFilterContext *ctx)
{
    PadContext *pad = ctx->priv;

    if (av_parse_color(pad->color, pad->color_str, -1, ctx) < 0)
        return AVERROR(EINVAL);

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    PadContext *pad = ctx->priv;
    int i;

    for (i = 0; i < 4; i++) {
        av_freep(&pad->line[i]);
        pad->line_step[i] = 0;
    }
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    PadContext *pad = ctx->priv;
    const AVPixFmtDescriptor *pix_desc = av_pix_fmt_desc_get(inlink->format);
    uint8_t rgba_color[4];
    int ret, is_packed_rgba;
    double var_values[VARS_NB], res;
    char *expr;

    pad->hsub = pix_desc->log2_chroma_w;
    pad->vsub = pix_desc->log2_chroma_h;

    var_values[VAR_PI]    = M_PI;
    var_values[VAR_PHI]   = M_PHI;
    var_values[VAR_E]     = M_E;
    var_values[VAR_IN_W]  = var_values[VAR_IW] = inlink->w;
    var_values[VAR_IN_H]  = var_values[VAR_IH] = inlink->h;
    var_values[VAR_OUT_W] = var_values[VAR_OW] = NAN;
    var_values[VAR_OUT_H] = var_values[VAR_OH] = NAN;
    var_values[VAR_A]     = (double) inlink->w / inlink->h;
    var_values[VAR_HSUB]  = 1<<pad->hsub;
    var_values[VAR_VSUB]  = 1<<pad->vsub;

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

    pad->w &= ~((1 << pad->hsub) - 1);
    pad->h &= ~((1 << pad->vsub) - 1);
    pad->x &= ~((1 << pad->hsub) - 1);
    pad->y &= ~((1 << pad->vsub) - 1);

    pad->in_w = inlink->w & ~((1 << pad->hsub) - 1);
    pad->in_h = inlink->h & ~((1 << pad->vsub) - 1);

    memcpy(rgba_color, pad->color, sizeof(rgba_color));
    ff_fill_line_with_color(pad->line, pad->line_step, pad->w, pad->color,
                            inlink->format, rgba_color, &is_packed_rgba, NULL);

    av_log(ctx, AV_LOG_VERBOSE, "w:%d h:%d -> w:%d h:%d x:%d y:%d color:0x%02X%02X%02X%02X[%s]\n",
           inlink->w, inlink->h, pad->w, pad->h, pad->x, pad->y,
           pad->color[0], pad->color[1], pad->color[2], pad->color[3],
           is_packed_rgba ? "rgba" : "yuva");

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
        int hsub = (plane == 1 || plane == 2) ? pad->hsub : 0;
        int vsub = (plane == 1 || plane == 2) ? pad->vsub : 0;

        frame->data[plane] += (pad->x >> hsub) * pad->line_step[plane] +
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
        int hsub = (planes[i] == 1 || planes[i] == 2) ? s->hsub : 0;
        int vsub = (planes[i] == 1 || planes[i] == 2) ? s->vsub : 0;

        uint8_t *start = frame->data[planes[i]];
        uint8_t *end   = start + (frame->height >> hsub) *
                                 frame->linesize[planes[i]];

        /* amount of free space needed before the start and after the end
         * of the plane */
        ptrdiff_t req_start = (s->x >> hsub) * s->line_step[planes[i]] +
                              (s->y >> vsub) * frame->linesize[planes[i]];
        ptrdiff_t req_end   = ((s->w - s->x - frame->width) >> hsub) *
                              s->line_step[planes[i]] +
                              (s->y >> vsub) * frame->linesize[planes[i]];

        if (frame->linesize[planes[i]] < (s->w >> hsub) * s->line_step[planes[i]])
            return 1;
        if (start - buf->data < req_start ||
            (buf->data + buf->size) - end < req_end)
            return 1;

#define SIGN(x) ((x) > 0 ? 1 : -1)
        for (j = 0; j < FF_ARRAY_ELEMS(planes) && planes[j] >= 0; j++) {
            int hsub1 = (planes[j] == 1 || planes[j] == 2) ? s->hsub : 0;
            uint8_t *start1 = frame->data[planes[j]];
            uint8_t *end1   = start1 + (frame->height >> hsub1) *
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

    for (i = 0; i < FF_ARRAY_ELEMS(frame->buf) && frame->buf[i]; i++)
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
        for (i = 0; i < FF_ARRAY_ELEMS(out->data) && out->data[i]; i++) {
            int hsub = (i == 1 || i == 2) ? pad->hsub : 0;
            int vsub = (i == 1 || i == 2) ? pad->vsub : 0;
            out->data[i] -= (pad->x >> hsub) * pad->line_step[i] +
                            (pad->y >> vsub) * out->linesize[i];
        }
    }

    /* top bar */
    if (pad->y) {
        ff_draw_rectangle(out->data, out->linesize,
                          pad->line, pad->line_step, pad->hsub, pad->vsub,
                          0, 0, pad->w, pad->y);
    }

    /* bottom bar */
    if (pad->h > pad->y + pad->in_h) {
        ff_draw_rectangle(out->data, out->linesize,
                          pad->line, pad->line_step, pad->hsub, pad->vsub,
                          0, pad->y + pad->in_h, pad->w, pad->h - pad->y - pad->in_h);
    }

    /* left border */
    ff_draw_rectangle(out->data, out->linesize, pad->line, pad->line_step,
                      pad->hsub, pad->vsub, 0, pad->y, pad->x, in->height);

    if (needs_copy) {
        ff_copy_rectangle(out->data, out->linesize, in->data, in->linesize,
                          pad->line_step, pad->hsub, pad->vsub,
                          pad->x, pad->y, 0, in->width, in->height);
    }

    /* right border */
    ff_draw_rectangle(out->data, out->linesize,
                      pad->line, pad->line_step, pad->hsub, pad->vsub,
                      pad->x + pad->in_w, pad->y, pad->w - pad->x - pad->in_w,
                      in->height);

    out->width  = pad->w;
    out->height = pad->h;

    if (in != out)
        av_frame_free(&in);
    return ff_filter_frame(inlink->dst->outputs[0], out);
}

#define OFFSET(x) offsetof(PadContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM
static const AVOption options[] = {
    { "width",  "Output video width",       OFFSET(w_expr),    AV_OPT_TYPE_STRING, { .str = "iw" },    .flags = FLAGS },
    { "height", "Output video height",      OFFSET(h_expr),    AV_OPT_TYPE_STRING, { .str = "ih" },    .flags = FLAGS },
    { "x",      "Horizontal position of the left edge of the input video in the "
        "output video",                     OFFSET(x_expr),    AV_OPT_TYPE_STRING, { .str = "0"  },    .flags = FLAGS },
    { "y",      "Vertical position of the top edge of the input video in the "
        "output video",                     OFFSET(y_expr),    AV_OPT_TYPE_STRING, { .str = "0"  },    .flags = FLAGS },
    { "color",  "Color of the padded area", OFFSET(color_str), AV_OPT_TYPE_STRING, { .str = "black" }, .flags = FLAGS },
    { NULL },
};

static const AVClass pad_class = {
    .class_name = "pad",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

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
    .uninit        = uninit,
    .query_formats = query_formats,

    .inputs    = avfilter_vf_pad_inputs,

    .outputs   = avfilter_vf_pad_outputs,
};
