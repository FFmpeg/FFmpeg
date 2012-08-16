/*
 * Copyright (c) 2007 Bobby Bingham
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
 * video crop filter
 */

/* #define DEBUG */

#include <stdio.h>

#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"
#include "libavutil/eval.h"
#include "libavutil/avstring.h"
#include "libavutil/internal.h"
#include "libavutil/libm.h"
#include "libavutil/imgutils.h"
#include "libavutil/mathematics.h"

static const char *const var_names[] = {
    "in_w", "iw",   ///< width  of the input video
    "in_h", "ih",   ///< height of the input video
    "out_w", "ow",  ///< width  of the cropped video
    "out_h", "oh",  ///< height of the cropped video
    "a",
    "sar",
    "dar",
    "hsub",
    "vsub",
    "x",
    "y",
    "n",            ///< number of frame
    "pos",          ///< position in the file
    "t",            ///< timestamp expressed in seconds
    NULL
};

enum var_name {
    VAR_IN_W,  VAR_IW,
    VAR_IN_H,  VAR_IH,
    VAR_OUT_W, VAR_OW,
    VAR_OUT_H, VAR_OH,
    VAR_A,
    VAR_SAR,
    VAR_DAR,
    VAR_HSUB,
    VAR_VSUB,
    VAR_X,
    VAR_Y,
    VAR_N,
    VAR_POS,
    VAR_T,
    VAR_VARS_NB
};

typedef struct {
    int  x;             ///< x offset of the non-cropped area with respect to the input area
    int  y;             ///< y offset of the non-cropped area with respect to the input area
    int  w;             ///< width of the cropped area
    int  h;             ///< height of the cropped area

    AVRational out_sar; ///< output sample aspect ratio
    int keep_aspect;    ///< keep display aspect ratio when cropping

    int max_step[4];    ///< max pixel step for each plane, expressed as a number of bytes
    int hsub, vsub;     ///< chroma subsampling
    char x_expr[256], y_expr[256], ow_expr[256], oh_expr[256];
    AVExpr *x_pexpr, *y_pexpr;  /* parsed expressions for x and y */
    double var_values[VAR_VARS_NB];
} CropContext;

static int query_formats(AVFilterContext *ctx)
{
    static const enum PixelFormat pix_fmts[] = {
        PIX_FMT_RGB48BE,      PIX_FMT_RGB48LE,
        PIX_FMT_BGR48BE,      PIX_FMT_BGR48LE,
        PIX_FMT_ARGB,         PIX_FMT_RGBA,
        PIX_FMT_ABGR,         PIX_FMT_BGRA,
        PIX_FMT_RGB24,        PIX_FMT_BGR24,
        PIX_FMT_RGB565BE,     PIX_FMT_RGB565LE,
        PIX_FMT_RGB555BE,     PIX_FMT_RGB555LE,
        PIX_FMT_BGR565BE,     PIX_FMT_BGR565LE,
        PIX_FMT_BGR555BE,     PIX_FMT_BGR555LE,
        PIX_FMT_GRAY16BE,     PIX_FMT_GRAY16LE,
        PIX_FMT_YUV420P16LE,  PIX_FMT_YUV420P16BE,
        PIX_FMT_YUV422P16LE,  PIX_FMT_YUV422P16BE,
        PIX_FMT_YUV444P16LE,  PIX_FMT_YUV444P16BE,
        PIX_FMT_YUV444P,      PIX_FMT_YUV422P,
        PIX_FMT_YUV420P,      PIX_FMT_YUV411P,
        PIX_FMT_YUV410P,      PIX_FMT_YUV440P,
        PIX_FMT_YUVJ444P,     PIX_FMT_YUVJ422P,
        PIX_FMT_YUVJ420P,     PIX_FMT_YUVJ440P,
        PIX_FMT_YUVA420P,
        PIX_FMT_RGB8,         PIX_FMT_BGR8,
        PIX_FMT_RGB4_BYTE,    PIX_FMT_BGR4_BYTE,
        PIX_FMT_PAL8,         PIX_FMT_GRAY8,
        PIX_FMT_NONE
    };

    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));

    return 0;
}

static av_cold int init(AVFilterContext *ctx, const char *args)
{
    CropContext *crop = ctx->priv;

    av_strlcpy(crop->ow_expr, "iw", sizeof(crop->ow_expr));
    av_strlcpy(crop->oh_expr, "ih", sizeof(crop->oh_expr));
    av_strlcpy(crop->x_expr, "(in_w-out_w)/2", sizeof(crop->x_expr));
    av_strlcpy(crop->y_expr, "(in_h-out_h)/2", sizeof(crop->y_expr));

    if (args)
        sscanf(args, "%255[^:]:%255[^:]:%255[^:]:%255[^:]:%d", crop->ow_expr, crop->oh_expr, crop->x_expr, crop->y_expr, &crop->keep_aspect);

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    CropContext *crop = ctx->priv;

    av_expr_free(crop->x_pexpr); crop->x_pexpr = NULL;
    av_expr_free(crop->y_pexpr); crop->y_pexpr = NULL;
}

static inline int normalize_double(int *n, double d)
{
    int ret = 0;

    if (isnan(d)) {
        ret = AVERROR(EINVAL);
    } else if (d > INT_MAX || d < INT_MIN) {
        *n = d > INT_MAX ? INT_MAX : INT_MIN;
        ret = AVERROR(EINVAL);
    } else
        *n = round(d);

    return ret;
}

static int config_input(AVFilterLink *link)
{
    AVFilterContext *ctx = link->dst;
    CropContext *crop = ctx->priv;
    const AVPixFmtDescriptor *pix_desc = &av_pix_fmt_descriptors[link->format];
    int ret;
    const char *expr;
    double res;

    crop->var_values[VAR_IN_W]  = crop->var_values[VAR_IW] = ctx->inputs[0]->w;
    crop->var_values[VAR_IN_H]  = crop->var_values[VAR_IH] = ctx->inputs[0]->h;
    crop->var_values[VAR_A]     = (float) link->w / link->h;
    crop->var_values[VAR_SAR]   = link->sample_aspect_ratio.num ? av_q2d(link->sample_aspect_ratio) : 1;
    crop->var_values[VAR_DAR]   = crop->var_values[VAR_A] * crop->var_values[VAR_SAR];
    crop->var_values[VAR_HSUB]  = 1<<pix_desc->log2_chroma_w;
    crop->var_values[VAR_VSUB]  = 1<<pix_desc->log2_chroma_h;
    crop->var_values[VAR_X]     = NAN;
    crop->var_values[VAR_Y]     = NAN;
    crop->var_values[VAR_OUT_W] = crop->var_values[VAR_OW] = NAN;
    crop->var_values[VAR_OUT_H] = crop->var_values[VAR_OH] = NAN;
    crop->var_values[VAR_N]     = 0;
    crop->var_values[VAR_T]     = NAN;
    crop->var_values[VAR_POS]   = NAN;

    av_image_fill_max_pixsteps(crop->max_step, NULL, pix_desc);
    crop->hsub = av_pix_fmt_descriptors[link->format].log2_chroma_w;
    crop->vsub = av_pix_fmt_descriptors[link->format].log2_chroma_h;

    if ((ret = av_expr_parse_and_eval(&res, (expr = crop->ow_expr),
                                      var_names, crop->var_values,
                                      NULL, NULL, NULL, NULL, NULL, 0, ctx)) < 0) goto fail_expr;
    crop->var_values[VAR_OUT_W] = crop->var_values[VAR_OW] = res;
    if ((ret = av_expr_parse_and_eval(&res, (expr = crop->oh_expr),
                                      var_names, crop->var_values,
                                      NULL, NULL, NULL, NULL, NULL, 0, ctx)) < 0) goto fail_expr;
    crop->var_values[VAR_OUT_H] = crop->var_values[VAR_OH] = res;
    /* evaluate again ow as it may depend on oh */
    if ((ret = av_expr_parse_and_eval(&res, (expr = crop->ow_expr),
                                      var_names, crop->var_values,
                                      NULL, NULL, NULL, NULL, NULL, 0, ctx)) < 0) goto fail_expr;
    crop->var_values[VAR_OUT_W] = crop->var_values[VAR_OW] = res;
    if (normalize_double(&crop->w, crop->var_values[VAR_OUT_W]) < 0 ||
        normalize_double(&crop->h, crop->var_values[VAR_OUT_H]) < 0) {
        av_log(ctx, AV_LOG_ERROR,
               "Too big value or invalid expression for out_w/ow or out_h/oh. "
               "Maybe the expression for out_w:'%s' or for out_h:'%s' is self-referencing.\n",
               crop->ow_expr, crop->oh_expr);
        return AVERROR(EINVAL);
    }
    crop->w &= ~((1 << crop->hsub) - 1);
    crop->h &= ~((1 << crop->vsub) - 1);

    if ((ret = av_expr_parse(&crop->x_pexpr, crop->x_expr, var_names,
                             NULL, NULL, NULL, NULL, 0, ctx)) < 0 ||
        (ret = av_expr_parse(&crop->y_pexpr, crop->y_expr, var_names,
                             NULL, NULL, NULL, NULL, 0, ctx)) < 0)
        return AVERROR(EINVAL);

    if (crop->keep_aspect) {
        AVRational dar = av_mul_q(link->sample_aspect_ratio,
                                  (AVRational){ link->w, link->h });
        av_reduce(&crop->out_sar.num, &crop->out_sar.den,
                  dar.num * crop->h, dar.den * crop->w, INT_MAX);
    } else
        crop->out_sar = link->sample_aspect_ratio;

    av_log(ctx, AV_LOG_VERBOSE, "w:%d h:%d sar:%d/%d -> w:%d h:%d sar:%d/%d\n",
           link->w, link->h, link->sample_aspect_ratio.num, link->sample_aspect_ratio.den,
           crop->w, crop->h, crop->out_sar.num, crop->out_sar.den);

    if (crop->w <= 0 || crop->h <= 0 ||
        crop->w > link->w || crop->h > link->h) {
        av_log(ctx, AV_LOG_ERROR,
               "Invalid too big or non positive size for width '%d' or height '%d'\n",
               crop->w, crop->h);
        return AVERROR(EINVAL);
    }

    /* set default, required in the case the first computed value for x/y is NAN */
    crop->x = (link->w - crop->w) / 2;
    crop->y = (link->h - crop->h) / 2;
    crop->x &= ~((1 << crop->hsub) - 1);
    crop->y &= ~((1 << crop->vsub) - 1);
    return 0;

fail_expr:
    av_log(NULL, AV_LOG_ERROR, "Error when evaluating the expression '%s'\n", expr);
    return ret;
}

static int config_output(AVFilterLink *link)
{
    CropContext *crop = link->src->priv;

    link->w = crop->w;
    link->h = crop->h;
    link->sample_aspect_ratio = crop->out_sar;

    return 0;
}

static int start_frame(AVFilterLink *link, AVFilterBufferRef *picref)
{
    AVFilterContext *ctx = link->dst;
    CropContext *crop = ctx->priv;
    AVFilterBufferRef *ref2;
    int i;

    ref2 = avfilter_ref_buffer(picref, ~0);
    if (!ref2)
        return AVERROR(ENOMEM);

    ref2->video->w = crop->w;
    ref2->video->h = crop->h;

    crop->var_values[VAR_T] = picref->pts == AV_NOPTS_VALUE ?
        NAN : picref->pts * av_q2d(link->time_base);
    crop->var_values[VAR_POS] = picref->pos == -1 ? NAN : picref->pos;
    crop->var_values[VAR_X] = av_expr_eval(crop->x_pexpr, crop->var_values, NULL);
    crop->var_values[VAR_Y] = av_expr_eval(crop->y_pexpr, crop->var_values, NULL);
    crop->var_values[VAR_X] = av_expr_eval(crop->x_pexpr, crop->var_values, NULL);

    normalize_double(&crop->x, crop->var_values[VAR_X]);
    normalize_double(&crop->y, crop->var_values[VAR_Y]);

    if (crop->x < 0) crop->x = 0;
    if (crop->y < 0) crop->y = 0;
    if ((unsigned)crop->x + (unsigned)crop->w > link->w) crop->x = link->w - crop->w;
    if ((unsigned)crop->y + (unsigned)crop->h > link->h) crop->y = link->h - crop->h;
    crop->x &= ~((1 << crop->hsub) - 1);
    crop->y &= ~((1 << crop->vsub) - 1);

    av_dlog(ctx, "n:%d t:%f x:%d y:%d x+w:%d y+h:%d\n",
            (int)crop->var_values[VAR_N], crop->var_values[VAR_T], crop->x,
            crop->y, crop->x+crop->w, crop->y+crop->h);

    ref2->data[0] += crop->y * ref2->linesize[0];
    ref2->data[0] += crop->x * crop->max_step[0];

    if (!(av_pix_fmt_descriptors[link->format].flags & PIX_FMT_PAL ||
          av_pix_fmt_descriptors[link->format].flags & PIX_FMT_PSEUDOPAL)) {
        for (i = 1; i < 3; i ++) {
            if (ref2->data[i]) {
                ref2->data[i] += (crop->y >> crop->vsub) * ref2->linesize[i];
                ref2->data[i] += (crop->x * crop->max_step[i]) >> crop->hsub;
            }
        }
    }

    /* alpha plane */
    if (ref2->data[3]) {
        ref2->data[3] += crop->y * ref2->linesize[3];
        ref2->data[3] += crop->x * crop->max_step[3];
    }

    return ff_start_frame(link->dst->outputs[0], ref2);
}

static int draw_slice(AVFilterLink *link, int y, int h, int slice_dir)
{
    AVFilterContext *ctx = link->dst;
    CropContext *crop = ctx->priv;

    if (y >= crop->y + crop->h || y + h <= crop->y)
        return 0;

    if (y < crop->y) {
        h -= crop->y - y;
        y  = crop->y;
    }
    if (y + h > crop->y + crop->h)
        h = crop->y + crop->h - y;

    return ff_draw_slice(ctx->outputs[0], y - crop->y, h, slice_dir);
}

static int end_frame(AVFilterLink *link)
{
    CropContext *crop = link->dst->priv;

    crop->var_values[VAR_N] += 1.0;
    return ff_end_frame(link->dst->outputs[0]);
}

AVFilter avfilter_vf_crop = {
    .name      = "crop",
    .description = NULL_IF_CONFIG_SMALL("Crop the input video to width:height:x:y."),

    .priv_size = sizeof(CropContext),

    .query_formats = query_formats,
    .init          = init,
    .uninit        = uninit,

    .inputs    = (const AVFilterPad[]) {{ .name             = "default",
                                          .type             = AVMEDIA_TYPE_VIDEO,
                                          .start_frame      = start_frame,
                                          .draw_slice       = draw_slice,
                                          .end_frame        = end_frame,
                                          .get_video_buffer = ff_null_get_video_buffer,
                                          .config_props     = config_input, },
                                        { .name = NULL}},
    .outputs   = (const AVFilterPad[]) {{ .name             = "default",
                                          .type             = AVMEDIA_TYPE_VIDEO,
                                          .config_props     = config_output, },
                                        { .name = NULL}},
};
