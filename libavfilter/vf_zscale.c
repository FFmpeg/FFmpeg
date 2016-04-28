/*
 * Copyright (c) 2015 Paul B Mahol
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
 * zscale video filter using z.lib library
 */

#include <stdio.h>
#include <string.h>

#include <zimg.h>

#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"
#include "libavutil/avstring.h"
#include "libavutil/eval.h"
#include "libavutil/internal.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/pixdesc.h"
#include "libavutil/imgutils.h"
#include "libavutil/avassert.h"

static const char *const var_names[] = {
    "in_w",   "iw",
    "in_h",   "ih",
    "out_w",  "ow",
    "out_h",  "oh",
    "a",
    "sar",
    "dar",
    "hsub",
    "vsub",
    "ohsub",
    "ovsub",
    NULL
};

enum var_name {
    VAR_IN_W,   VAR_IW,
    VAR_IN_H,   VAR_IH,
    VAR_OUT_W,  VAR_OW,
    VAR_OUT_H,  VAR_OH,
    VAR_A,
    VAR_SAR,
    VAR_DAR,
    VAR_HSUB,
    VAR_VSUB,
    VAR_OHSUB,
    VAR_OVSUB,
    VARS_NB
};

typedef struct ZScaleContext {
    const AVClass *class;

    /**
     * New dimensions. Special values are:
     *   0 = original width/height
     *  -1 = keep original aspect
     *  -N = try to keep aspect but make sure it is divisible by N
     */
    int w, h;
    int dither;
    int filter;
    int colorspace;
    int trc;
    int primaries;
    int range;
    int colorspace_in;
    int trc_in;
    int primaries_in;
    int range_in;
    char *size_str;

    char *w_expr;               ///< width  expression string
    char *h_expr;               ///< height expression string

    int out_h_chr_pos;
    int out_v_chr_pos;
    int in_h_chr_pos;
    int in_v_chr_pos;

    int force_original_aspect_ratio;

    void *tmp;
    size_t tmp_size;

    zimg_image_format src_format, dst_format;
    zimg_image_format alpha_src_format, alpha_dst_format;
    zimg_graph_builder_params alpha_params, params;
    zimg_filter_graph *alpha_graph, *graph;

    enum AVColorSpace in_colorspace, out_colorspace;
    enum AVColorTransferCharacteristic in_trc, out_trc;
    enum AVColorPrimaries in_primaries, out_primaries;
    enum AVColorRange in_range, out_range;
} ZScaleContext;

static av_cold int init_dict(AVFilterContext *ctx, AVDictionary **opts)
{
    ZScaleContext *s = ctx->priv;
    int ret;

    if (s->size_str && (s->w_expr || s->h_expr)) {
        av_log(ctx, AV_LOG_ERROR,
               "Size and width/height expressions cannot be set at the same time.\n");
            return AVERROR(EINVAL);
    }

    if (s->w_expr && !s->h_expr)
        FFSWAP(char *, s->w_expr, s->size_str);

    if (s->size_str) {
        char buf[32];
        if ((ret = av_parse_video_size(&s->w, &s->h, s->size_str)) < 0) {
            av_log(ctx, AV_LOG_ERROR,
                   "Invalid size '%s'\n", s->size_str);
            return ret;
        }
        snprintf(buf, sizeof(buf)-1, "%d", s->w);
        av_opt_set(s, "w", buf, 0);
        snprintf(buf, sizeof(buf)-1, "%d", s->h);
        av_opt_set(s, "h", buf, 0);
    }
    if (!s->w_expr)
        av_opt_set(s, "w", "iw", 0);
    if (!s->h_expr)
        av_opt_set(s, "h", "ih", 0);

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pixel_fmts[] = {
        AV_PIX_FMT_YUV410P, AV_PIX_FMT_YUV411P,
        AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P,
        AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUV444P,
        AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ422P,
        AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_YUVJ444P,
        AV_PIX_FMT_YUVJ411P,
        AV_PIX_FMT_YUV420P9, AV_PIX_FMT_YUV422P9, AV_PIX_FMT_YUV444P9,
        AV_PIX_FMT_YUV420P10, AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV444P10,
        AV_PIX_FMT_YUV420P12, AV_PIX_FMT_YUV422P12, AV_PIX_FMT_YUV444P12,
        AV_PIX_FMT_YUV420P14, AV_PIX_FMT_YUV422P14, AV_PIX_FMT_YUV444P14,
        AV_PIX_FMT_YUV420P16, AV_PIX_FMT_YUV422P16, AV_PIX_FMT_YUV444P16,
        AV_PIX_FMT_YUVA420P, AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUVA444P,
        AV_PIX_FMT_YUVA420P9, AV_PIX_FMT_YUVA422P9, AV_PIX_FMT_YUVA444P9,
        AV_PIX_FMT_YUVA420P10, AV_PIX_FMT_YUVA422P10, AV_PIX_FMT_YUVA444P10,
        AV_PIX_FMT_YUVA420P16, AV_PIX_FMT_YUVA422P16, AV_PIX_FMT_YUVA444P16,
        AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRP9, AV_PIX_FMT_GBRP10,
        AV_PIX_FMT_GBRP12, AV_PIX_FMT_GBRP14, AV_PIX_FMT_GBRP16,
        AV_PIX_FMT_GBRAP, AV_PIX_FMT_GBRAP16,
        AV_PIX_FMT_NONE
    };
    int ret;

    ret = ff_formats_ref(ff_make_format_list(pixel_fmts), &ctx->inputs[0]->out_formats);
    if (ret < 0)
        return ret;
    return ff_formats_ref(ff_make_format_list(pixel_fmts), &ctx->outputs[0]->in_formats);
}

static int config_props(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = outlink->src->inputs[0];
    ZScaleContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    const AVPixFmtDescriptor *out_desc = av_pix_fmt_desc_get(outlink->format);
    int64_t w, h;
    double var_values[VARS_NB], res;
    char *expr;
    int ret;
    int factor_w, factor_h;

    var_values[VAR_IN_W]  = var_values[VAR_IW] = inlink->w;
    var_values[VAR_IN_H]  = var_values[VAR_IH] = inlink->h;
    var_values[VAR_OUT_W] = var_values[VAR_OW] = NAN;
    var_values[VAR_OUT_H] = var_values[VAR_OH] = NAN;
    var_values[VAR_A]     = (double) inlink->w / inlink->h;
    var_values[VAR_SAR]   = inlink->sample_aspect_ratio.num ?
        (double) inlink->sample_aspect_ratio.num / inlink->sample_aspect_ratio.den : 1;
    var_values[VAR_DAR]   = var_values[VAR_A] * var_values[VAR_SAR];
    var_values[VAR_HSUB]  = 1 << desc->log2_chroma_w;
    var_values[VAR_VSUB]  = 1 << desc->log2_chroma_h;
    var_values[VAR_OHSUB] = 1 << out_desc->log2_chroma_w;
    var_values[VAR_OVSUB] = 1 << out_desc->log2_chroma_h;

    /* evaluate width and height */
    av_expr_parse_and_eval(&res, (expr = s->w_expr),
                           var_names, var_values,
                           NULL, NULL, NULL, NULL, NULL, 0, ctx);
    s->w = var_values[VAR_OUT_W] = var_values[VAR_OW] = res;
    if ((ret = av_expr_parse_and_eval(&res, (expr = s->h_expr),
                                      var_names, var_values,
                                      NULL, NULL, NULL, NULL, NULL, 0, ctx)) < 0)
        goto fail;
    s->h = var_values[VAR_OUT_H] = var_values[VAR_OH] = res;
    /* evaluate again the width, as it may depend on the output height */
    if ((ret = av_expr_parse_and_eval(&res, (expr = s->w_expr),
                                      var_names, var_values,
                                      NULL, NULL, NULL, NULL, NULL, 0, ctx)) < 0)
        goto fail;
    s->w = res;

    w = s->w;
    h = s->h;

    /* Check if it is requested that the result has to be divisible by a some
     * factor (w or h = -n with n being the factor). */
    factor_w = 1;
    factor_h = 1;
    if (w < -1) {
        factor_w = -w;
    }
    if (h < -1) {
        factor_h = -h;
    }

    if (w < 0 && h < 0)
        s->w = s->h = 0;

    if (!(w = s->w))
        w = inlink->w;
    if (!(h = s->h))
        h = inlink->h;

    /* Make sure that the result is divisible by the factor we determined
     * earlier. If no factor was set, it is nothing will happen as the default
     * factor is 1 */
    if (w < 0)
        w = av_rescale(h, inlink->w, inlink->h * factor_w) * factor_w;
    if (h < 0)
        h = av_rescale(w, inlink->h, inlink->w * factor_h) * factor_h;

    /* Note that force_original_aspect_ratio may overwrite the previous set
     * dimensions so that it is not divisible by the set factors anymore. */
    if (s->force_original_aspect_ratio) {
        int tmp_w = av_rescale(h, inlink->w, inlink->h);
        int tmp_h = av_rescale(w, inlink->h, inlink->w);

        if (s->force_original_aspect_ratio == 1) {
             w = FFMIN(tmp_w, w);
             h = FFMIN(tmp_h, h);
        } else {
             w = FFMAX(tmp_w, w);
             h = FFMAX(tmp_h, h);
        }
    }

    if (w > INT_MAX || h > INT_MAX ||
        (h * inlink->w) > INT_MAX  ||
        (w * inlink->h) > INT_MAX)
        av_log(ctx, AV_LOG_ERROR, "Rescaled value for width or height is too big.\n");

    outlink->w = w;
    outlink->h = h;

    if (inlink->w == outlink->w &&
        inlink->h == outlink->h &&
        inlink->format == outlink->format)
        ;
    else {
    }

    if (inlink->sample_aspect_ratio.num){
        outlink->sample_aspect_ratio = av_mul_q((AVRational){outlink->h * inlink->w, outlink->w * inlink->h}, inlink->sample_aspect_ratio);
    } else
        outlink->sample_aspect_ratio = inlink->sample_aspect_ratio;

    av_log(ctx, AV_LOG_VERBOSE, "w:%d h:%d fmt:%s sar:%d/%d -> w:%d h:%d fmt:%s sar:%d/%d\n",
           inlink ->w, inlink ->h, av_get_pix_fmt_name( inlink->format),
           inlink->sample_aspect_ratio.num, inlink->sample_aspect_ratio.den,
           outlink->w, outlink->h, av_get_pix_fmt_name(outlink->format),
           outlink->sample_aspect_ratio.num, outlink->sample_aspect_ratio.den);
    return 0;

fail:
    av_log(ctx, AV_LOG_ERROR,
           "Error when evaluating the expression '%s'.\n"
           "Maybe the expression for out_w:'%s' or for out_h:'%s' is self-referencing.\n",
           expr, s->w_expr, s->h_expr);
    return ret;
}

static int print_zimg_error(AVFilterContext *ctx)
{
    char err_msg[1024];
    int err_code = zimg_get_last_error(err_msg, sizeof(err_msg));

    av_log(ctx, AV_LOG_ERROR, "code %d: %s\n", err_code, err_msg);

    return err_code;
}

static int convert_matrix(enum AVColorSpace colorspace)
{
    switch (colorspace) {
    case AVCOL_SPC_RGB:
        return ZIMG_MATRIX_RGB;
    case AVCOL_SPC_BT709:
        return ZIMG_MATRIX_709;
    case AVCOL_SPC_UNSPECIFIED:
        return ZIMG_MATRIX_UNSPECIFIED;
    case AVCOL_SPC_BT470BG:
        return ZIMG_MATRIX_470BG;
    case AVCOL_SPC_SMPTE170M:
        return ZIMG_MATRIX_170M;
    case AVCOL_SPC_YCGCO:
        return ZIMG_MATRIX_YCGCO;
    case AVCOL_SPC_BT2020_NCL:
        return ZIMG_MATRIX_2020_NCL;
    case AVCOL_SPC_BT2020_CL:
        return ZIMG_MATRIX_2020_CL;
    }
    return ZIMG_MATRIX_UNSPECIFIED;
}

static int convert_trc(enum AVColorTransferCharacteristic color_trc)
{
    switch (color_trc) {
    case AVCOL_TRC_UNSPECIFIED:
        return ZIMG_TRANSFER_UNSPECIFIED;
    case AVCOL_TRC_BT709:
        return ZIMG_TRANSFER_709;
    case AVCOL_TRC_SMPTE170M:
        return ZIMG_TRANSFER_601;
    case AVCOL_TRC_LINEAR:
        return ZIMG_TRANSFER_LINEAR;
    case AVCOL_TRC_BT2020_10:
        return ZIMG_TRANSFER_2020_10;
    case AVCOL_TRC_BT2020_12:
        return ZIMG_TRANSFER_2020_12;
    }
    return ZIMG_TRANSFER_UNSPECIFIED;
}

static int convert_primaries(enum AVColorPrimaries color_primaries)
{
    switch (color_primaries) {
    case AVCOL_PRI_UNSPECIFIED:
        return ZIMG_PRIMARIES_UNSPECIFIED;
    case AVCOL_PRI_BT709:
        return ZIMG_PRIMARIES_709;
    case AVCOL_PRI_SMPTE170M:
        return ZIMG_PRIMARIES_170M;
    case AVCOL_PRI_SMPTE240M:
        return ZIMG_PRIMARIES_240M;
    case AVCOL_PRI_BT2020:
        return ZIMG_PRIMARIES_2020;
    }
    return ZIMG_PRIMARIES_UNSPECIFIED;
}

static int convert_range(enum AVColorRange color_range)
{
    switch (color_range) {
    case AVCOL_RANGE_UNSPECIFIED:
    case AVCOL_RANGE_MPEG:
        return ZIMG_RANGE_LIMITED;
    case AVCOL_RANGE_JPEG:
        return ZIMG_RANGE_FULL;
    }
    return ZIMG_RANGE_LIMITED;
}

static int filter_frame(AVFilterLink *link, AVFrame *in)
{
    ZScaleContext *s = link->dst->priv;
    AVFilterLink *outlink = link->dst->outputs[0];
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(link->format);
    const AVPixFmtDescriptor *odesc = av_pix_fmt_desc_get(outlink->format);
    zimg_image_buffer_const src_buf = { ZIMG_API_VERSION };
    zimg_image_buffer dst_buf = { ZIMG_API_VERSION };
    char buf[32];
    size_t tmp_size;
    int ret = 0, plane;
    AVFrame *out;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }

    av_frame_copy_props(out, in);
    out->width  = outlink->w;
    out->height = outlink->h;

    if(   in->width  != link->w
       || in->height != link->h
       || in->format != link->format
       || s->in_colorspace != in->colorspace
       || s->in_trc  != in->color_trc
       || s->in_primaries != in->color_primaries
       || s->in_range != in->color_range
       || s->out_colorspace != out->colorspace
       || s->out_trc  != out->color_trc
       || s->out_primaries != out->color_primaries
       || s->out_range != out->color_range) {
        snprintf(buf, sizeof(buf)-1, "%d", outlink->w);
        av_opt_set(s, "w", buf, 0);
        snprintf(buf, sizeof(buf)-1, "%d", outlink->h);
        av_opt_set(s, "h", buf, 0);

        link->dst->inputs[0]->format = in->format;
        link->dst->inputs[0]->w      = in->width;
        link->dst->inputs[0]->h      = in->height;

        if ((ret = config_props(outlink)) < 0) {
            av_frame_free(&in);
            av_frame_free(&out);
            return ret;
        }

        zimg_image_format_default(&s->src_format, ZIMG_API_VERSION);
        zimg_image_format_default(&s->dst_format, ZIMG_API_VERSION);
        zimg_graph_builder_params_default(&s->params, ZIMG_API_VERSION);

        s->params.dither_type = s->dither;
        s->params.cpu_type = ZIMG_CPU_AUTO;
        s->params.resample_filter = s->filter;
        s->params.resample_filter_uv = s->filter;

        s->src_format.width = in->width;
        s->src_format.height = in->height;
        s->src_format.subsample_w = desc->log2_chroma_w;
        s->src_format.subsample_h = desc->log2_chroma_h;
        s->src_format.depth = desc->comp[0].depth;
        s->src_format.pixel_type = desc->comp[0].depth > 8 ? ZIMG_PIXEL_WORD : ZIMG_PIXEL_BYTE;
        s->src_format.color_family = (desc->flags & AV_PIX_FMT_FLAG_RGB) ? ZIMG_COLOR_RGB : ZIMG_COLOR_YUV;
        s->src_format.matrix_coefficients = (desc->flags & AV_PIX_FMT_FLAG_RGB) ? ZIMG_MATRIX_RGB : s->colorspace_in == -1 ? convert_matrix(in->colorspace) : s->colorspace_in;
        s->src_format.transfer_characteristics = s->trc_in == - 1 ? convert_trc(in->color_trc) : s->trc_in;
        s->src_format.color_primaries = s->primaries_in == -1 ? convert_primaries(in->color_primaries) : s->primaries_in;
        s->src_format.pixel_range = (desc->flags & AV_PIX_FMT_FLAG_RGB) ? ZIMG_RANGE_FULL : s->range_in == -1 ? convert_range(in->color_range) : s->range_in;

        s->dst_format.width = out->width;
        s->dst_format.height = out->height;
        s->dst_format.subsample_w = odesc->log2_chroma_w;
        s->dst_format.subsample_h = odesc->log2_chroma_h;
        s->dst_format.depth = odesc->comp[0].depth;
        s->dst_format.pixel_type = odesc->comp[0].depth > 8 ? ZIMG_PIXEL_WORD : ZIMG_PIXEL_BYTE;
        s->dst_format.color_family = (odesc->flags & AV_PIX_FMT_FLAG_RGB) ? ZIMG_COLOR_RGB : ZIMG_COLOR_YUV;
        s->dst_format.matrix_coefficients = (odesc->flags & AV_PIX_FMT_FLAG_RGB) ? ZIMG_MATRIX_RGB : s->colorspace == -1 ? convert_matrix(out->colorspace) : s->colorspace;
        s->dst_format.transfer_characteristics = s->trc == -1 ? convert_trc(out->color_trc) : s->trc;
        s->dst_format.color_primaries = s->primaries == -1 ? convert_primaries(out->color_primaries) : s->primaries;
        s->dst_format.pixel_range = (odesc->flags & AV_PIX_FMT_FLAG_RGB) ? ZIMG_RANGE_FULL : s->range == -1 ? convert_range(out->color_range) : s->range;

        if (s->colorspace != -1)
            out->colorspace = (int)s->dst_format.matrix_coefficients;

        if (s->primaries != -1)
            out->color_primaries = (int)s->dst_format.color_primaries;

        if (s->range != -1)
            out->color_range = (int)s->dst_format.pixel_range + 1;

        if (s->trc != -1)
            out->color_trc = (int)s->dst_format.transfer_characteristics;

        zimg_filter_graph_free(s->graph);
        s->graph = zimg_filter_graph_build(&s->src_format, &s->dst_format, &s->params);
        if (!s->graph) {
            ret = print_zimg_error(link->dst);
            goto fail;
        }

        if ((ret = zimg_filter_graph_get_tmp_size(s->graph, &tmp_size))) {
            ret = print_zimg_error(link->dst);
            goto fail;
        }

        if (tmp_size > s->tmp_size) {
            av_freep(&s->tmp);
            s->tmp = av_malloc(tmp_size);
            if (!s->tmp) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }
            s->tmp_size = tmp_size;
        }

        s->in_colorspace  = in->colorspace;
        s->in_trc         = in->color_trc;
        s->in_primaries   = in->color_primaries;
        s->in_range       = in->color_range;
        s->out_colorspace = out->colorspace;
        s->out_trc        = out->color_trc;
        s->out_primaries  = out->color_primaries;
        s->out_range      = out->color_range;

        if (desc->flags & AV_PIX_FMT_FLAG_ALPHA && odesc->flags & AV_PIX_FMT_FLAG_ALPHA) {
            zimg_image_format_default(&s->alpha_src_format, ZIMG_API_VERSION);
            zimg_image_format_default(&s->alpha_dst_format, ZIMG_API_VERSION);
            zimg_graph_builder_params_default(&s->alpha_params, ZIMG_API_VERSION);

            s->alpha_params.dither_type = s->dither;
            s->alpha_params.cpu_type = ZIMG_CPU_AUTO;
            s->alpha_params.resample_filter = s->filter;

            s->alpha_src_format.width = in->width;
            s->alpha_src_format.height = in->height;
            s->alpha_src_format.depth = desc->comp[0].depth;
            s->alpha_src_format.pixel_type = desc->comp[0].depth > 8 ? ZIMG_PIXEL_WORD : ZIMG_PIXEL_BYTE;
            s->alpha_src_format.color_family = ZIMG_COLOR_GREY;

            s->alpha_dst_format.width = out->width;
            s->alpha_dst_format.height = out->height;
            s->alpha_dst_format.depth = odesc->comp[0].depth;
            s->alpha_dst_format.pixel_type = odesc->comp[0].depth > 8 ? ZIMG_PIXEL_WORD : ZIMG_PIXEL_BYTE;
            s->alpha_dst_format.color_family = ZIMG_COLOR_GREY;

            zimg_filter_graph_free(s->alpha_graph);
            s->alpha_graph = zimg_filter_graph_build(&s->alpha_src_format, &s->alpha_dst_format, &s->alpha_params);
            if (!s->alpha_graph) {
                ret = print_zimg_error(link->dst);
                goto fail;
            }
        }
    }

    if (s->colorspace != -1)
        out->colorspace = (int)s->dst_format.matrix_coefficients;

    if (s->primaries != -1)
        out->color_primaries = (int)s->dst_format.color_primaries;

    if (s->range != -1)
        out->color_range = (int)s->dst_format.pixel_range;

    if (s->trc != -1)
        out->color_trc = (int)s->dst_format.transfer_characteristics;

    av_reduce(&out->sample_aspect_ratio.num, &out->sample_aspect_ratio.den,
              (int64_t)in->sample_aspect_ratio.num * outlink->h * link->w,
              (int64_t)in->sample_aspect_ratio.den * outlink->w * link->h,
              INT_MAX);

    for (plane = 0; plane < 3; plane++) {
        int p = desc->comp[plane].plane;
        src_buf.plane[plane].data   = in->data[p];
        src_buf.plane[plane].stride = in->linesize[p];
        src_buf.plane[plane].mask   = -1;

        p = odesc->comp[plane].plane;
        dst_buf.plane[plane].data   = out->data[p];
        dst_buf.plane[plane].stride = out->linesize[p];
        dst_buf.plane[plane].mask   = -1;
    }

    ret = zimg_filter_graph_process(s->graph, &src_buf, &dst_buf, s->tmp, 0, 0, 0, 0);
    if (ret) {
        print_zimg_error(link->dst);
        goto fail;
    }

    if (desc->flags & AV_PIX_FMT_FLAG_ALPHA && odesc->flags & AV_PIX_FMT_FLAG_ALPHA) {
        src_buf.plane[0].data   = in->data[3];
        src_buf.plane[0].stride = in->linesize[3];
        src_buf.plane[0].mask   = -1;

        dst_buf.plane[0].data   = out->data[3];
        dst_buf.plane[0].stride = out->linesize[3];
        dst_buf.plane[0].mask   = -1;

        ret = zimg_filter_graph_process(s->alpha_graph, &src_buf, &dst_buf, s->tmp, 0, 0, 0, 0);
        if (ret) {
            print_zimg_error(link->dst);
            goto fail;
        }
    } else if (odesc->flags & AV_PIX_FMT_FLAG_ALPHA) {
        int y;

        for (y = 0; y < outlink->h; y++)
            memset(out->data[3] + y * out->linesize[3], 0xff, outlink->w);
    }

fail:
    av_frame_free(&in);
    if (ret) {
        av_frame_free(&out);
        return ret;
    }

    return ff_filter_frame(outlink, out);
}

static void uninit(AVFilterContext *ctx)
{
    ZScaleContext *s = ctx->priv;

    zimg_filter_graph_free(s->graph);
    av_freep(&s->tmp);
    s->tmp_size = 0;
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    ZScaleContext *s = ctx->priv;
    int ret;

    if (   !strcmp(cmd, "width")  || !strcmp(cmd, "w")
        || !strcmp(cmd, "height") || !strcmp(cmd, "h")) {

        int old_w = s->w;
        int old_h = s->h;
        AVFilterLink *outlink = ctx->outputs[0];

        av_opt_set(s, cmd, args, 0);
        if ((ret = config_props(outlink)) < 0) {
            s->w = old_w;
            s->h = old_h;
        }
    } else
        ret = AVERROR(ENOSYS);

    return ret;
}

#define OFFSET(x) offsetof(ZScaleContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption zscale_options[] = {
    { "w",      "Output video width",  OFFSET(w_expr),    AV_OPT_TYPE_STRING, .flags = FLAGS },
    { "width",  "Output video width",  OFFSET(w_expr),    AV_OPT_TYPE_STRING, .flags = FLAGS },
    { "h",      "Output video height", OFFSET(h_expr),    AV_OPT_TYPE_STRING, .flags = FLAGS },
    { "height", "Output video height", OFFSET(h_expr),    AV_OPT_TYPE_STRING, .flags = FLAGS },
    { "size",   "set video size",      OFFSET(size_str),  AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, FLAGS },
    { "s",      "set video size",      OFFSET(size_str),  AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, FLAGS },
    { "dither", "set dither type",     OFFSET(dither),    AV_OPT_TYPE_INT, {.i64 = 0}, 0, ZIMG_DITHER_ERROR_DIFFUSION, FLAGS, "dither" },
    { "d",      "set dither type",     OFFSET(dither),    AV_OPT_TYPE_INT, {.i64 = 0}, 0, ZIMG_DITHER_ERROR_DIFFUSION, FLAGS, "dither" },
    {     "none",             0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_DITHER_NONE},     0, 0, FLAGS, "dither" },
    {     "ordered",          0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_DITHER_ORDERED},  0, 0, FLAGS, "dither" },
    {     "random",           0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_DITHER_RANDOM},   0, 0, FLAGS, "dither" },
    {     "error_diffusion",  0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_DITHER_ERROR_DIFFUSION}, 0, 0, FLAGS, "dither" },
    { "filter", "set filter type",     OFFSET(filter),    AV_OPT_TYPE_INT, {.i64 = ZIMG_RESIZE_BILINEAR}, 0, ZIMG_RESIZE_LANCZOS, FLAGS, "filter" },
    { "f",      "set filter type",     OFFSET(filter),    AV_OPT_TYPE_INT, {.i64 = ZIMG_RESIZE_BILINEAR}, 0, ZIMG_RESIZE_LANCZOS, FLAGS, "filter" },
    {     "point",            0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_RESIZE_POINT},    0, 0, FLAGS, "filter" },
    {     "bilinear",         0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_RESIZE_BILINEAR}, 0, 0, FLAGS, "filter" },
    {     "bicubic",          0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_RESIZE_BICUBIC},  0, 0, FLAGS, "filter" },
    {     "spline16",         0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_RESIZE_SPLINE16}, 0, 0, FLAGS, "filter" },
    {     "spline36",         0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_RESIZE_SPLINE36}, 0, 0, FLAGS, "filter" },
    {     "lanczos",          0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_RESIZE_LANCZOS},  0, 0, FLAGS, "filter" },
    { "range", "set color range",      OFFSET(range),     AV_OPT_TYPE_INT, {.i64 = -1}, -1, ZIMG_RANGE_FULL, FLAGS, "range" },
    { "r",     "set color range",      OFFSET(range),     AV_OPT_TYPE_INT, {.i64 = -1}, -1, ZIMG_RANGE_FULL, FLAGS, "range" },
    {     "input",            0,       0,                 AV_OPT_TYPE_CONST, {.i64 = -1},                 0, 0, FLAGS, "range" },
    {     "limited",          0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_RANGE_LIMITED}, 0, 0, FLAGS, "range" },
    {     "full",             0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_RANGE_FULL},    0, 0, FLAGS, "range" },
    { "primaries", "set color primaries", OFFSET(primaries), AV_OPT_TYPE_INT, {.i64 = -1}, -1, ZIMG_PRIMARIES_2020, FLAGS, "primaries" },
    { "p",         "set color primaries", OFFSET(primaries), AV_OPT_TYPE_INT, {.i64 = -1}, -1, ZIMG_PRIMARIES_2020, FLAGS, "primaries" },
    {     "input",            0,       0,                 AV_OPT_TYPE_CONST, {.i64 = -1},                         0, 0, FLAGS, "primaries" },
    {     "709",              0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_PRIMARIES_709},         0, 0, FLAGS, "primaries" },
    {     "unspecified",      0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_PRIMARIES_UNSPECIFIED}, 0, 0, FLAGS, "primaries" },
    {     "170m",             0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_PRIMARIES_170M},        0, 0, FLAGS, "primaries" },
    {     "240m",             0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_PRIMARIES_240M},        0, 0, FLAGS, "primaries" },
    {     "2020",             0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_PRIMARIES_2020},        0, 0, FLAGS, "primaries" },
    { "transfer", "set transfer characteristic", OFFSET(trc), AV_OPT_TYPE_INT, {.i64 = -1}, -1, ZIMG_TRANSFER_2020_12, FLAGS, "transfer" },
    { "t",        "set transfer characteristic", OFFSET(trc), AV_OPT_TYPE_INT, {.i64 = -1}, -1, ZIMG_TRANSFER_2020_12, FLAGS, "transfer" },
    {     "input",            0,       0,                 AV_OPT_TYPE_CONST, {.i64 = -1},                         0, 0, FLAGS, "transfer" },
    {     "709",              0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_TRANSFER_709},         0, 0, FLAGS, "transfer" },
    {     "unspecified",      0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_TRANSFER_UNSPECIFIED}, 0, 0, FLAGS, "transfer" },
    {     "601",              0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_TRANSFER_601},         0, 0, FLAGS, "transfer" },
    {     "linear",           0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_TRANSFER_LINEAR},      0, 0, FLAGS, "transfer" },
    {     "2020_10",          0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_TRANSFER_2020_10},     0, 0, FLAGS, "transfer" },
    {     "2020_12",          0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_TRANSFER_2020_12},     0, 0, FLAGS, "transfer" },
    { "matrix", "set colorspace matrix", OFFSET(colorspace), AV_OPT_TYPE_INT, {.i64 = -1}, -1, ZIMG_MATRIX_2020_CL, FLAGS, "matrix" },
    { "m",      "set colorspace matrix", OFFSET(colorspace), AV_OPT_TYPE_INT, {.i64 = -1}, -1, ZIMG_MATRIX_2020_CL, FLAGS, "matrix" },
    {     "input",            0,       0,                 AV_OPT_TYPE_CONST, {.i64 = -1},                      0, 0, FLAGS, "matrix" },
    {     "709",              0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_MATRIX_709},         0, 0, FLAGS, "matrix" },
    {     "unspecified",      0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_MATRIX_UNSPECIFIED}, 0, 0, FLAGS, "matrix" },
    {     "470bg",            0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_MATRIX_470BG},       0, 0, FLAGS, "matrix" },
    {     "170m",             0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_MATRIX_170M},        0, 0, FLAGS, "matrix" },
    {     "ycgco",            0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_MATRIX_YCGCO},       0, 0, FLAGS, "matrix" },
    {     "2020_ncl",         0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_MATRIX_2020_NCL},    0, 0, FLAGS, "matrix" },
    {     "2020_cl",          0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_MATRIX_2020_CL},     0, 0, FLAGS, "matrix" },
    { "rangein", "set input color range", OFFSET(range_in),     AV_OPT_TYPE_INT, {.i64 = -1}, -1, ZIMG_RANGE_FULL, FLAGS, "range" },
    { "rin",     "set input color range", OFFSET(range_in),     AV_OPT_TYPE_INT, {.i64 = -1}, -1, ZIMG_RANGE_FULL, FLAGS, "range" },
    { "primariesin", "set input color primaries", OFFSET(primaries_in), AV_OPT_TYPE_INT, {.i64 = -1}, -1, ZIMG_PRIMARIES_2020, FLAGS, "primaries" },
    { "pin",         "set input color primaries", OFFSET(primaries_in), AV_OPT_TYPE_INT, {.i64 = -1}, -1, ZIMG_PRIMARIES_2020, FLAGS, "primaries" },
    { "transferin", "set input transfer characteristic", OFFSET(trc_in), AV_OPT_TYPE_INT, {.i64 = -1}, -1, ZIMG_TRANSFER_2020_12, FLAGS, "transfer" },
    { "tin",        "set input transfer characteristic", OFFSET(trc_in), AV_OPT_TYPE_INT, {.i64 = -1}, -1, ZIMG_TRANSFER_2020_12, FLAGS, "transfer" },
    { "matrixin", "set input colorspace matrix", OFFSET(colorspace_in), AV_OPT_TYPE_INT, {.i64 = -1}, -1, ZIMG_MATRIX_2020_CL, FLAGS, "matrix" },
    { "min",      "set input colorspace matrix", OFFSET(colorspace_in), AV_OPT_TYPE_INT, {.i64 = -1}, -1, ZIMG_MATRIX_2020_CL, FLAGS, "matrix" },
    { NULL }
};

static const AVClass zscale_class = {
    .class_name       = "zscale",
    .item_name        = av_default_item_name,
    .option           = zscale_options,
    .version          = LIBAVUTIL_VERSION_INT,
    .category         = AV_CLASS_CATEGORY_FILTER,
};

static const AVFilterPad avfilter_vf_zscale_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad avfilter_vf_zscale_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_props,
    },
    { NULL }
};

AVFilter ff_vf_zscale = {
    .name            = "zscale",
    .description     = NULL_IF_CONFIG_SMALL("Apply resizing, colorspace and bit depth conversion."),
    .init_dict       = init_dict,
    .query_formats   = query_formats,
    .priv_size       = sizeof(ZScaleContext),
    .priv_class      = &zscale_class,
    .uninit          = uninit,
    .inputs          = avfilter_vf_zscale_inputs,
    .outputs         = avfilter_vf_zscale_outputs,
    .process_command = process_command,
};
