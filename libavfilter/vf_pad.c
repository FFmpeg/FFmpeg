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
#include "video.h"
#include "libavutil/avstring.h"
#include "libavutil/eval.h"
#include "libavutil/pixdesc.h"
#include "libavutil/colorspace.h"
#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/parseutils.h"
#include "libavutil/mathematics.h"
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
    static const enum PixelFormat pix_fmts[] = {
        PIX_FMT_ARGB,         PIX_FMT_RGBA,
        PIX_FMT_ABGR,         PIX_FMT_BGRA,
        PIX_FMT_RGB24,        PIX_FMT_BGR24,

        PIX_FMT_YUV444P,      PIX_FMT_YUV422P,
        PIX_FMT_YUV420P,      PIX_FMT_YUV411P,
        PIX_FMT_YUV410P,      PIX_FMT_YUV440P,
        PIX_FMT_YUVJ444P,     PIX_FMT_YUVJ422P,
        PIX_FMT_YUVJ420P,     PIX_FMT_YUVJ440P,
        PIX_FMT_YUVA420P,

        PIX_FMT_NONE
    };

    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
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

    uint8_t color[4];       ///< color expressed either in YUVA or RGBA colorspace for the padding area
    uint8_t *line[4];
    int      line_step[4];
    int hsub, vsub;         ///< chroma subsampling values
    int needs_copy;
} PadContext;

static av_cold int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    PadContext *pad = ctx->priv;
    char color_string[128] = "black";

    av_strlcpy(pad->w_expr, "iw", sizeof(pad->w_expr));
    av_strlcpy(pad->h_expr, "ih", sizeof(pad->h_expr));
    av_strlcpy(pad->x_expr, "0" , sizeof(pad->w_expr));
    av_strlcpy(pad->y_expr, "0" , sizeof(pad->h_expr));

    if (args)
        sscanf(args, "%255[^:]:%255[^:]:%255[^:]:%255[^:]:%255s",
               pad->w_expr, pad->h_expr, pad->x_expr, pad->y_expr, color_string);

    if (av_parse_color(pad->color, color_string, -1, ctx) < 0)
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
    const AVPixFmtDescriptor *pix_desc = &av_pix_fmt_descriptors[inlink->format];
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
    var_values[VAR_A]     = (float) inlink->w / inlink->h;
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

    av_log(ctx, AV_LOG_INFO, "w:%d h:%d -> w:%d h:%d x:%d y:%d color:0x%02X%02X%02X%02X[%s]\n",
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

static AVFilterBufferRef *get_video_buffer(AVFilterLink *inlink, int perms, int w, int h)
{
    PadContext *pad = inlink->dst->priv;

    AVFilterBufferRef *picref = avfilter_get_video_buffer(inlink->dst->outputs[0], perms,
                                                       w + (pad->w - pad->in_w),
                                                       h + (pad->h - pad->in_h));
    int plane;

    picref->video->w = w;
    picref->video->h = h;

    for (plane = 0; plane < 4 && picref->data[plane]; plane++) {
        int hsub = (plane == 1 || plane == 2) ? pad->hsub : 0;
        int vsub = (plane == 1 || plane == 2) ? pad->vsub : 0;

        picref->data[plane] += (pad->x >> hsub) * pad->line_step[plane] +
            (pad->y >> vsub) * picref->linesize[plane];
    }

    return picref;
}

static int does_clip(PadContext *pad, AVFilterBufferRef *outpicref, int plane, int hsub, int vsub, int x, int y)
{
    int64_t x_in_buf, y_in_buf;

    x_in_buf =  outpicref->data[plane] - outpicref->buf->data[plane]
             +  (x >> hsub) * pad      ->line_step[plane]
             +  (y >> vsub) * outpicref->linesize [plane];

    if(x_in_buf < 0 || x_in_buf % pad->line_step[plane])
        return 1;
    x_in_buf /= pad->line_step[plane];

    av_assert0(outpicref->buf->linesize[plane]>0); //while reference can use negative linesize the main buffer should not

    y_in_buf = x_in_buf / outpicref->buf->linesize[plane];
    x_in_buf %= outpicref->buf->linesize[plane];

    if(   y_in_buf<<vsub >= outpicref->buf->h
       || x_in_buf<<hsub >= outpicref->buf->w)
        return 1;
    return 0;
}

static void start_frame(AVFilterLink *inlink, AVFilterBufferRef *inpicref)
{
    PadContext *pad = inlink->dst->priv;
    AVFilterBufferRef *outpicref = avfilter_ref_buffer(inpicref, ~0);
    int plane;

    for (plane = 0; plane < 4 && outpicref->data[plane]; plane++) {
        int hsub = (plane == 1 || plane == 2) ? pad->hsub : 0;
        int vsub = (plane == 1 || plane == 2) ? pad->vsub : 0;

        av_assert0(outpicref->buf->w>0 && outpicref->buf->h>0);

        if(outpicref->format != outpicref->buf->format) //unsupported currently
            break;

        outpicref->data[plane] -=   (pad->x  >> hsub) * pad      ->line_step[plane]
                                  + (pad->y  >> vsub) * outpicref->linesize [plane];

        if(   does_clip(pad, outpicref, plane, hsub, vsub, 0, 0)
           || does_clip(pad, outpicref, plane, hsub, vsub, 0, pad->h-1)
           || does_clip(pad, outpicref, plane, hsub, vsub, pad->w-1, 0)
           || does_clip(pad, outpicref, plane, hsub, vsub, pad->w-1, pad->h-1)
          )
            break;
    }
    pad->needs_copy= plane < 4 && outpicref->data[plane];
    if(pad->needs_copy){
        av_log(inlink->dst, AV_LOG_DEBUG, "Direct padding impossible allocating new frame\n");
        avfilter_unref_buffer(outpicref);
        outpicref = avfilter_get_video_buffer(inlink->dst->outputs[0], AV_PERM_WRITE | AV_PERM_NEG_LINESIZES,
                                                       FFMAX(inlink->w, pad->w),
                                                       FFMAX(inlink->h, pad->h));
        avfilter_copy_buffer_ref_props(outpicref, inpicref);
    }

    inlink->dst->outputs[0]->out_buf = outpicref;

    outpicref->video->w = pad->w;
    outpicref->video->h = pad->h;

    ff_start_frame(inlink->dst->outputs[0], outpicref);
}

static void end_frame(AVFilterLink *link)
{
    ff_end_frame(link->dst->outputs[0]);
    avfilter_unref_buffer(link->cur_buf);
}

static void draw_send_bar_slice(AVFilterLink *link, int y, int h, int slice_dir, int before_slice)
{
    PadContext *pad = link->dst->priv;
    int bar_y, bar_h = 0;

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
        ff_draw_rectangle(link->dst->outputs[0]->out_buf->data,
                          link->dst->outputs[0]->out_buf->linesize,
                          pad->line, pad->line_step, pad->hsub, pad->vsub,
                          0, bar_y, pad->w, bar_h);
        ff_draw_slice(link->dst->outputs[0], bar_y, bar_h, slice_dir);
    }
}

static void draw_slice(AVFilterLink *link, int y, int h, int slice_dir)
{
    PadContext *pad = link->dst->priv;
    AVFilterBufferRef *outpic = link->dst->outputs[0]->out_buf;
    AVFilterBufferRef *inpic = link->cur_buf;

    y += pad->y;

    y &= ~((1 << pad->vsub) - 1);
    h &= ~((1 << pad->vsub) - 1);

    if (!h)
        return;
    draw_send_bar_slice(link, y, h, slice_dir, 1);

    /* left border */
    ff_draw_rectangle(outpic->data, outpic->linesize, pad->line, pad->line_step,
                      pad->hsub, pad->vsub, 0, y, pad->x, h);

    if(pad->needs_copy){
        ff_copy_rectangle(outpic->data, outpic->linesize,
                          inpic->data, inpic->linesize, pad->line_step,
                          pad->hsub, pad->vsub,
                          pad->x, y, y-pad->y, inpic->video->w, h);
    }

    /* right border */
    ff_draw_rectangle(outpic->data, outpic->linesize,
                      pad->line, pad->line_step, pad->hsub, pad->vsub,
                      pad->x + pad->in_w, y, pad->w - pad->x - pad->in_w, h);
    ff_draw_slice(link->dst->outputs[0], y, h, slice_dir);

    draw_send_bar_slice(link, y, h, slice_dir, -1);
}

AVFilter avfilter_vf_pad = {
    .name          = "pad",
    .description   = NULL_IF_CONFIG_SMALL("Pad input image to width:height[:x:y[:color]] (default x and y: 0, default color: black)."),

    .priv_size     = sizeof(PadContext),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,

    .inputs    = (AVFilterPad[]) {{ .name             = "default",
                                    .type             = AVMEDIA_TYPE_VIDEO,
                                    .config_props     = config_input,
                                    .get_video_buffer = get_video_buffer,
                                    .start_frame      = start_frame,
                                    .draw_slice       = draw_slice,
                                    .end_frame        = end_frame, },
                                  { .name = NULL}},

    .outputs   = (AVFilterPad[]) {{ .name             = "default",
                                    .type             = AVMEDIA_TYPE_VIDEO,
                                    .config_props     = config_output, },
                                  { .name = NULL}},
};
