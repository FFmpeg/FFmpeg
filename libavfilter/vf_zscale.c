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

#include <float.h>
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
#include "libavutil/intreadwrite.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/pixdesc.h"
#include "libavutil/imgutils.h"
#include "libavutil/avassert.h"

#define ZIMG_ALIGNMENT 32

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
    int chromal;
    int colorspace_in;
    int trc_in;
    int primaries_in;
    int range_in;
    int chromal_in;
    char *size_str;
    double nominal_peak_luminance;
    int approximate_gamma;

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
    enum AVChromaLocation in_chromal, out_chromal;
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
        AV_PIX_FMT_GBRPF32, AV_PIX_FMT_GBRAPF32,
        AV_PIX_FMT_NONE
    };
    int ret;

    ret = ff_formats_ref(ff_make_format_list(pixel_fmts), &ctx->inputs[0]->outcfg.formats);
    if (ret < 0)
        return ret;
    return ff_formats_ref(ff_make_format_list(pixel_fmts), &ctx->outputs[0]->incfg.formats);
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

    return AVERROR_EXTERNAL;
}

static int convert_chroma_location(enum AVChromaLocation chroma_location)
{
    switch (chroma_location) {
    case AVCHROMA_LOC_UNSPECIFIED:
    case AVCHROMA_LOC_LEFT:
        return ZIMG_CHROMA_LEFT;
    case AVCHROMA_LOC_CENTER:
        return ZIMG_CHROMA_CENTER;
    case AVCHROMA_LOC_TOPLEFT:
        return ZIMG_CHROMA_TOP_LEFT;
    case AVCHROMA_LOC_TOP:
        return ZIMG_CHROMA_TOP;
    case AVCHROMA_LOC_BOTTOMLEFT:
        return ZIMG_CHROMA_BOTTOM_LEFT;
    case AVCHROMA_LOC_BOTTOM:
        return ZIMG_CHROMA_BOTTOM;
    }
    return ZIMG_CHROMA_LEFT;
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
    case AVCOL_SPC_FCC:
        return ZIMG_MATRIX_FCC;
    case AVCOL_SPC_BT470BG:
        return ZIMG_MATRIX_470BG;
    case AVCOL_SPC_SMPTE170M:
        return ZIMG_MATRIX_170M;
    case AVCOL_SPC_SMPTE240M:
        return ZIMG_MATRIX_240M;
    case AVCOL_SPC_YCGCO:
        return ZIMG_MATRIX_YCGCO;
    case AVCOL_SPC_BT2020_NCL:
        return ZIMG_MATRIX_2020_NCL;
    case AVCOL_SPC_BT2020_CL:
        return ZIMG_MATRIX_2020_CL;
    case AVCOL_SPC_CHROMA_DERIVED_NCL:
        return ZIMG_MATRIX_CHROMATICITY_DERIVED_NCL;
    case AVCOL_SPC_CHROMA_DERIVED_CL:
        return ZIMG_MATRIX_CHROMATICITY_DERIVED_CL;
    case AVCOL_SPC_ICTCP:
        return ZIMG_MATRIX_ICTCP;
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
    case AVCOL_TRC_GAMMA22:
        return ZIMG_TRANSFER_470_M;
    case AVCOL_TRC_GAMMA28:
        return ZIMG_TRANSFER_470_BG;
    case AVCOL_TRC_SMPTE170M:
        return ZIMG_TRANSFER_601;
    case AVCOL_TRC_SMPTE240M:
        return ZIMG_TRANSFER_240M;
    case AVCOL_TRC_LINEAR:
        return ZIMG_TRANSFER_LINEAR;
    case AVCOL_TRC_LOG:
        return ZIMG_TRANSFER_LOG_100;
    case AVCOL_TRC_LOG_SQRT:
        return ZIMG_TRANSFER_LOG_316;
    case AVCOL_TRC_IEC61966_2_4:
        return ZIMG_TRANSFER_IEC_61966_2_4;
    case AVCOL_TRC_BT2020_10:
        return ZIMG_TRANSFER_2020_10;
    case AVCOL_TRC_BT2020_12:
        return ZIMG_TRANSFER_2020_12;
    case AVCOL_TRC_SMPTE2084:
        return ZIMG_TRANSFER_ST2084;
    case AVCOL_TRC_ARIB_STD_B67:
        return ZIMG_TRANSFER_ARIB_B67;
    case AVCOL_TRC_IEC61966_2_1:
        return ZIMG_TRANSFER_IEC_61966_2_1;
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
    case AVCOL_PRI_BT470M:
        return ZIMG_PRIMARIES_470_M;
    case AVCOL_PRI_BT470BG:
        return ZIMG_PRIMARIES_470_BG;
    case AVCOL_PRI_SMPTE170M:
        return ZIMG_PRIMARIES_170M;
    case AVCOL_PRI_SMPTE240M:
        return ZIMG_PRIMARIES_240M;
    case AVCOL_PRI_FILM:
        return ZIMG_PRIMARIES_FILM;
    case AVCOL_PRI_BT2020:
        return ZIMG_PRIMARIES_2020;
    case AVCOL_PRI_SMPTE428:
        return ZIMG_PRIMARIES_ST428;
    case AVCOL_PRI_SMPTE431:
        return ZIMG_PRIMARIES_ST431_2;
    case AVCOL_PRI_SMPTE432:
        return ZIMG_PRIMARIES_ST432_1;
    case AVCOL_PRI_JEDEC_P22:
        return ZIMG_PRIMARIES_EBU3213_E;
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

static void format_init(zimg_image_format *format, AVFrame *frame, const AVPixFmtDescriptor *desc,
                        int colorspace, int primaries, int transfer, int range, int location)
{
    format->width = frame->width;
    format->height = frame->height;
    format->subsample_w = desc->log2_chroma_w;
    format->subsample_h = desc->log2_chroma_h;
    format->depth = desc->comp[0].depth;
    format->pixel_type = (desc->flags & AV_PIX_FMT_FLAG_FLOAT) ? ZIMG_PIXEL_FLOAT : desc->comp[0].depth > 8 ? ZIMG_PIXEL_WORD : ZIMG_PIXEL_BYTE;
    format->color_family = (desc->flags & AV_PIX_FMT_FLAG_RGB) ? ZIMG_COLOR_RGB : ZIMG_COLOR_YUV;
    format->matrix_coefficients = (desc->flags & AV_PIX_FMT_FLAG_RGB) ? ZIMG_MATRIX_RGB : colorspace == -1 ? convert_matrix(frame->colorspace) : colorspace;
    format->color_primaries = primaries == -1 ? convert_primaries(frame->color_primaries) : primaries;
    format->transfer_characteristics = transfer == - 1 ? convert_trc(frame->color_trc) : transfer;
    format->pixel_range = (desc->flags & AV_PIX_FMT_FLAG_RGB) ? ZIMG_RANGE_FULL : range == -1 ? convert_range(frame->color_range) : range;
    format->chroma_location = location == -1 ? convert_chroma_location(frame->chroma_location) : location;
}

static int graph_build(zimg_filter_graph **graph, zimg_graph_builder_params *params,
                       zimg_image_format *src_format, zimg_image_format *dst_format,
                       void **tmp, size_t *tmp_size)
{
    int ret;
    size_t size;

    zimg_filter_graph_free(*graph);
    *graph = zimg_filter_graph_build(src_format, dst_format, params);
    if (!*graph)
        return print_zimg_error(NULL);

    ret = zimg_filter_graph_get_tmp_size(*graph, &size);
    if (ret)
        return print_zimg_error(NULL);

    if (size > *tmp_size) {
        av_freep(tmp);
        *tmp = av_malloc(size);
        if (!*tmp)
            return AVERROR(ENOMEM);

        *tmp_size = size;
    }

    return 0;
}

static int realign_frame(const AVPixFmtDescriptor *desc, AVFrame **frame)
{
    AVFrame *aligned = NULL;
    int ret = 0, plane;

    /* Realign any unaligned input frame. */
    for (plane = 0; plane < 3; plane++) {
        int p = desc->comp[plane].plane;
        if ((uintptr_t)(*frame)->data[p] % ZIMG_ALIGNMENT || (*frame)->linesize[p] % ZIMG_ALIGNMENT) {
            if (!(aligned = av_frame_alloc())) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }

            aligned->format = (*frame)->format;
            aligned->width  = (*frame)->width;
            aligned->height = (*frame)->height;

            if ((ret = av_frame_get_buffer(aligned, ZIMG_ALIGNMENT)) < 0)
                goto fail;

            if ((ret = av_frame_copy(aligned, *frame)) < 0)
                goto fail;

            if ((ret = av_frame_copy_props(aligned, *frame)) < 0)
                goto fail;

            av_frame_free(frame);
            *frame = aligned;
            return 0;
        }
    }

fail:
    av_frame_free(&aligned);
    return ret;
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
    int ret = 0, plane;
    AVFrame *out = NULL;

    if ((ret = realign_frame(desc, &in)) < 0)
        goto fail;

    if (!(out = ff_get_video_buffer(outlink, outlink->w, outlink->h))) {
        ret =  AVERROR(ENOMEM);
        goto fail;
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
       || s->out_range != out->color_range
       || s->in_chromal != in->chroma_location
       || s->out_chromal != out->chroma_location) {
        snprintf(buf, sizeof(buf)-1, "%d", outlink->w);
        av_opt_set(s, "w", buf, 0);
        snprintf(buf, sizeof(buf)-1, "%d", outlink->h);
        av_opt_set(s, "h", buf, 0);

        link->dst->inputs[0]->format = in->format;
        link->dst->inputs[0]->w      = in->width;
        link->dst->inputs[0]->h      = in->height;

        if ((ret = config_props(outlink)) < 0)
            goto fail;

        zimg_image_format_default(&s->src_format, ZIMG_API_VERSION);
        zimg_image_format_default(&s->dst_format, ZIMG_API_VERSION);
        zimg_graph_builder_params_default(&s->params, ZIMG_API_VERSION);

        s->params.dither_type = s->dither;
        s->params.cpu_type = ZIMG_CPU_AUTO;
        s->params.resample_filter = s->filter;
        s->params.resample_filter_uv = s->filter;
        s->params.nominal_peak_luminance = s->nominal_peak_luminance;
        s->params.allow_approximate_gamma = s->approximate_gamma;

        format_init(&s->src_format, in, desc, s->colorspace_in,
                    s->primaries_in, s->trc_in, s->range_in, s->chromal_in);
        format_init(&s->dst_format, out, odesc, s->colorspace,
                    s->primaries, s->trc, s->range, s->chromal);

        if (s->colorspace != -1)
            out->colorspace = (int)s->dst_format.matrix_coefficients;

        if (s->primaries != -1)
            out->color_primaries = (int)s->dst_format.color_primaries;

        if (s->range != -1)
            out->color_range = (int)s->dst_format.pixel_range + 1;

        if (s->trc != -1)
            out->color_trc = (int)s->dst_format.transfer_characteristics;

        if (s->chromal != -1)
            out->chroma_location = (int)s->dst_format.chroma_location - 1;

        ret = graph_build(&s->graph, &s->params, &s->src_format, &s->dst_format,
                          &s->tmp, &s->tmp_size);
        if (ret < 0)
            goto fail;

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
            s->alpha_src_format.pixel_type = (desc->flags & AV_PIX_FMT_FLAG_FLOAT) ? ZIMG_PIXEL_FLOAT : desc->comp[0].depth > 8 ? ZIMG_PIXEL_WORD : ZIMG_PIXEL_BYTE;
            s->alpha_src_format.color_family = ZIMG_COLOR_GREY;

            s->alpha_dst_format.width = out->width;
            s->alpha_dst_format.height = out->height;
            s->alpha_dst_format.depth = odesc->comp[0].depth;
            s->alpha_dst_format.pixel_type = (odesc->flags & AV_PIX_FMT_FLAG_FLOAT) ? ZIMG_PIXEL_FLOAT : odesc->comp[0].depth > 8 ? ZIMG_PIXEL_WORD : ZIMG_PIXEL_BYTE;
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
        ret = print_zimg_error(link->dst);
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
            ret = print_zimg_error(link->dst);
            goto fail;
        }
    } else if (odesc->flags & AV_PIX_FMT_FLAG_ALPHA) {
        int x, y;

        if (odesc->flags & AV_PIX_FMT_FLAG_FLOAT) {
            for (y = 0; y < out->height; y++) {
                for (x = 0; x < out->width; x++) {
                    AV_WN32(out->data[3] + x * odesc->comp[3].step + y * out->linesize[3],
                            av_float2int(1.0f));
                }
            }
        } else {
            for (y = 0; y < outlink->h; y++)
                memset(out->data[3] + y * out->linesize[3], 0xff, outlink->w);
        }
    }

fail:
    av_frame_free(&in);
    if (ret) {
        av_frame_free(&out);
        return ret;
    }

    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    ZScaleContext *s = ctx->priv;

    zimg_filter_graph_free(s->graph);
    zimg_filter_graph_free(s->alpha_graph);
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
#define TFLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption zscale_options[] = {
    { "w",      "Output video width",  OFFSET(w_expr),    AV_OPT_TYPE_STRING, .flags = TFLAGS },
    { "width",  "Output video width",  OFFSET(w_expr),    AV_OPT_TYPE_STRING, .flags = TFLAGS },
    { "h",      "Output video height", OFFSET(h_expr),    AV_OPT_TYPE_STRING, .flags = TFLAGS },
    { "height", "Output video height", OFFSET(h_expr),    AV_OPT_TYPE_STRING, .flags = TFLAGS },
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
    { "out_range", "set color range",  OFFSET(range),     AV_OPT_TYPE_INT, {.i64 = -1}, -1, ZIMG_RANGE_FULL, FLAGS, "range" },
    { "range", "set color range",      OFFSET(range),     AV_OPT_TYPE_INT, {.i64 = -1}, -1, ZIMG_RANGE_FULL, FLAGS, "range" },
    { "r",     "set color range",      OFFSET(range),     AV_OPT_TYPE_INT, {.i64 = -1}, -1, ZIMG_RANGE_FULL, FLAGS, "range" },
    {     "input",            0,       0,                 AV_OPT_TYPE_CONST, {.i64 = -1},                 0, 0, FLAGS, "range" },
    {     "limited",          0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_RANGE_LIMITED}, 0, 0, FLAGS, "range" },
    {     "full",             0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_RANGE_FULL},    0, 0, FLAGS, "range" },
    {     "unknown",          0,       0,                 AV_OPT_TYPE_CONST, {.i64 = -1},                 0, 0, FLAGS, "range" },
    {     "tv",               0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_RANGE_LIMITED}, 0, 0, FLAGS, "range" },
    {     "pc",               0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_RANGE_FULL},    0, 0, FLAGS, "range" },
    { "primaries", "set color primaries", OFFSET(primaries), AV_OPT_TYPE_INT, {.i64 = -1}, -1, INT_MAX, FLAGS, "primaries" },
    { "p",         "set color primaries", OFFSET(primaries), AV_OPT_TYPE_INT, {.i64 = -1}, -1, INT_MAX, FLAGS, "primaries" },
    {     "input",            0,       0,                 AV_OPT_TYPE_CONST, {.i64 = -1},                         0, 0, FLAGS, "primaries" },
    {     "709",              0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_PRIMARIES_709},         0, 0, FLAGS, "primaries" },
    {     "unspecified",      0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_PRIMARIES_UNSPECIFIED}, 0, 0, FLAGS, "primaries" },
    {     "170m",             0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_PRIMARIES_170M},        0, 0, FLAGS, "primaries" },
    {     "240m",             0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_PRIMARIES_240M},        0, 0, FLAGS, "primaries" },
    {     "2020",             0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_PRIMARIES_2020},        0, 0, FLAGS, "primaries" },
    {     "unknown",          0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_PRIMARIES_UNSPECIFIED}, 0, 0, FLAGS, "primaries" },
    {     "bt709",            0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_PRIMARIES_709},         0, 0, FLAGS, "primaries" },
    {     "bt470m",           0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_PRIMARIES_470_M},       0, 0, FLAGS, "primaries" },
    {     "bt470bg",          0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_PRIMARIES_470_BG},      0, 0, FLAGS, "primaries" },
    {     "smpte170m",        0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_PRIMARIES_170M},        0, 0, FLAGS, "primaries" },
    {     "smpte240m",        0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_PRIMARIES_240M},        0, 0, FLAGS, "primaries" },
    {     "film",             0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_PRIMARIES_FILM},        0, 0, FLAGS, "primaries" },
    {     "bt2020",           0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_PRIMARIES_2020},        0, 0, FLAGS, "primaries" },
    {     "smpte428",         0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_PRIMARIES_ST428},       0, 0, FLAGS, "primaries" },
    {     "smpte431",         0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_PRIMARIES_ST431_2},     0, 0, FLAGS, "primaries" },
    {     "smpte432",         0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_PRIMARIES_ST432_1},     0, 0, FLAGS, "primaries" },
    {     "jedec-p22",        0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_PRIMARIES_EBU3213_E},   0, 0, FLAGS, "primaries" },
    {     "ebu3213",          0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_PRIMARIES_EBU3213_E},   0, 0, FLAGS, "primaries" },
    { "transfer", "set transfer characteristic", OFFSET(trc), AV_OPT_TYPE_INT, {.i64 = -1}, -1, INT_MAX, FLAGS, "transfer" },
    { "t",        "set transfer characteristic", OFFSET(trc), AV_OPT_TYPE_INT, {.i64 = -1}, -1, INT_MAX, FLAGS, "transfer" },
    {     "input",            0,       0,                 AV_OPT_TYPE_CONST, {.i64 = -1},                         0, 0, FLAGS, "transfer" },
    {     "709",              0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_TRANSFER_709},         0, 0, FLAGS, "transfer" },
    {     "unspecified",      0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_TRANSFER_UNSPECIFIED}, 0, 0, FLAGS, "transfer" },
    {     "601",              0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_TRANSFER_601},         0, 0, FLAGS, "transfer" },
    {     "linear",           0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_TRANSFER_LINEAR},      0, 0, FLAGS, "transfer" },
    {     "2020_10",          0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_TRANSFER_2020_10},     0, 0, FLAGS, "transfer" },
    {     "2020_12",          0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_TRANSFER_2020_12},     0, 0, FLAGS, "transfer" },
    {     "unknown",          0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_TRANSFER_UNSPECIFIED}, 0, 0, FLAGS, "transfer" },
    {     "bt470m",           0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_TRANSFER_470_M},       0, 0, FLAGS, "transfer" },
    {     "bt470bg",          0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_TRANSFER_470_BG},      0, 0, FLAGS, "transfer" },
    {     "smpte170m",        0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_TRANSFER_601},         0, 0, FLAGS, "transfer" },
    {     "bt709",            0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_TRANSFER_709},         0, 0, FLAGS, "transfer" },
    {     "linear",           0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_TRANSFER_LINEAR},      0, 0, FLAGS, "transfer" },
    {     "log100",           0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_TRANSFER_LOG_100},     0, 0, FLAGS, "transfer" },
    {     "log316",           0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_TRANSFER_LOG_316},     0, 0, FLAGS, "transfer" },
    {     "bt2020-10",        0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_TRANSFER_2020_10},     0, 0, FLAGS, "transfer" },
    {     "bt2020-12",        0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_TRANSFER_2020_12},     0, 0, FLAGS, "transfer" },
    {     "smpte2084",        0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_TRANSFER_ST2084},      0, 0, FLAGS, "transfer" },
    {     "iec61966-2-4",     0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_TRANSFER_IEC_61966_2_4},0, 0, FLAGS, "transfer" },
    {     "iec61966-2-1",     0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_TRANSFER_IEC_61966_2_1},0, 0, FLAGS, "transfer" },
    {     "arib-std-b67",     0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_TRANSFER_ARIB_B67},    0, 0, FLAGS, "transfer" },
    { "matrix", "set colorspace matrix", OFFSET(colorspace), AV_OPT_TYPE_INT, {.i64 = -1}, -1, INT_MAX, FLAGS, "matrix" },
    { "m",      "set colorspace matrix", OFFSET(colorspace), AV_OPT_TYPE_INT, {.i64 = -1}, -1, INT_MAX, FLAGS, "matrix" },
    {     "input",            0,       0,                 AV_OPT_TYPE_CONST, {.i64 = -1},                      0, 0, FLAGS, "matrix" },
    {     "709",              0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_MATRIX_709},         0, 0, FLAGS, "matrix" },
    {     "unspecified",      0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_MATRIX_UNSPECIFIED}, 0, 0, FLAGS, "matrix" },
    {     "470bg",            0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_MATRIX_470BG},       0, 0, FLAGS, "matrix" },
    {     "170m",             0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_MATRIX_170M},        0, 0, FLAGS, "matrix" },
    {     "2020_ncl",         0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_MATRIX_2020_NCL},    0, 0, FLAGS, "matrix" },
    {     "2020_cl",          0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_MATRIX_2020_CL},     0, 0, FLAGS, "matrix" },
    {     "unknown",          0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_MATRIX_UNSPECIFIED}, 0, 0, FLAGS, "matrix" },
    {     "gbr",              0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_MATRIX_RGB},         0, 0, FLAGS, "matrix" },
    {     "bt709",            0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_MATRIX_709},         0, 0, FLAGS, "matrix" },
    {     "fcc",              0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_MATRIX_FCC},         0, 0, FLAGS, "matrix" },
    {     "bt470bg",          0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_MATRIX_470BG},       0, 0, FLAGS, "matrix" },
    {     "smpte170m",        0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_MATRIX_170M},        0, 0, FLAGS, "matrix" },
    {     "smpte2400m",       0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_MATRIX_240M},        0, 0, FLAGS, "matrix" },
    {     "ycgco",            0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_MATRIX_YCGCO},       0, 0, FLAGS, "matrix" },
    {     "bt2020nc",         0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_MATRIX_2020_NCL},    0, 0, FLAGS, "matrix" },
    {     "bt2020c",          0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_MATRIX_2020_CL},     0, 0, FLAGS, "matrix" },
    {     "chroma-derived-nc",0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_MATRIX_CHROMATICITY_DERIVED_NCL}, 0, 0, FLAGS, "matrix" },
    {     "chroma-derived-c", 0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_MATRIX_CHROMATICITY_DERIVED_CL}, 0, 0, FLAGS, "matrix" },
    {     "ictcp",            0,       0,                 AV_OPT_TYPE_CONST, {.i64 = ZIMG_MATRIX_ICTCP},       0, 0, FLAGS, "matrix" },
    { "in_range", "set input color range", OFFSET(range_in),    AV_OPT_TYPE_INT, {.i64 = -1}, -1, ZIMG_RANGE_FULL, FLAGS, "range" },
    { "rangein", "set input color range", OFFSET(range_in),     AV_OPT_TYPE_INT, {.i64 = -1}, -1, ZIMG_RANGE_FULL, FLAGS, "range" },
    { "rin",     "set input color range", OFFSET(range_in),     AV_OPT_TYPE_INT, {.i64 = -1}, -1, ZIMG_RANGE_FULL, FLAGS, "range" },
    { "primariesin", "set input color primaries", OFFSET(primaries_in), AV_OPT_TYPE_INT, {.i64 = -1}, -1, INT_MAX, FLAGS, "primaries" },
    { "pin",         "set input color primaries", OFFSET(primaries_in), AV_OPT_TYPE_INT, {.i64 = -1}, -1, INT_MAX, FLAGS, "primaries" },
    { "transferin", "set input transfer characteristic", OFFSET(trc_in), AV_OPT_TYPE_INT, {.i64 = -1}, -1, INT_MAX, FLAGS, "transfer" },
    { "tin",        "set input transfer characteristic", OFFSET(trc_in), AV_OPT_TYPE_INT, {.i64 = -1}, -1, INT_MAX, FLAGS, "transfer" },
    { "matrixin", "set input colorspace matrix", OFFSET(colorspace_in), AV_OPT_TYPE_INT, {.i64 = -1}, -1, INT_MAX, FLAGS, "matrix" },
    { "min",      "set input colorspace matrix", OFFSET(colorspace_in), AV_OPT_TYPE_INT, {.i64 = -1}, -1, INT_MAX, FLAGS, "matrix" },
    { "chromal",  "set output chroma location", OFFSET(chromal), AV_OPT_TYPE_INT, {.i64 = -1}, -1, ZIMG_CHROMA_BOTTOM, FLAGS, "chroma" },
    { "c",        "set output chroma location", OFFSET(chromal), AV_OPT_TYPE_INT, {.i64 = -1}, -1, ZIMG_CHROMA_BOTTOM, FLAGS, "chroma" },
    {     "input",     0, 0, AV_OPT_TYPE_CONST, {.i64 = -1},                       0, 0, FLAGS, "chroma" },
    {     "left",      0, 0, AV_OPT_TYPE_CONST, {.i64 = ZIMG_CHROMA_LEFT},         0, 0, FLAGS, "chroma" },
    {     "center",    0, 0, AV_OPT_TYPE_CONST, {.i64 = ZIMG_CHROMA_CENTER},       0, 0, FLAGS, "chroma" },
    {     "topleft",   0, 0, AV_OPT_TYPE_CONST, {.i64 = ZIMG_CHROMA_TOP_LEFT},     0, 0, FLAGS, "chroma" },
    {     "top",       0, 0, AV_OPT_TYPE_CONST, {.i64 = ZIMG_CHROMA_TOP},          0, 0, FLAGS, "chroma" },
    {     "bottomleft",0, 0, AV_OPT_TYPE_CONST, {.i64 = ZIMG_CHROMA_BOTTOM_LEFT},  0, 0, FLAGS, "chroma" },
    {     "bottom",    0, 0, AV_OPT_TYPE_CONST, {.i64 = ZIMG_CHROMA_BOTTOM},       0, 0, FLAGS, "chroma" },
    { "chromalin",  "set input chroma location", OFFSET(chromal_in), AV_OPT_TYPE_INT, {.i64 = -1}, -1, ZIMG_CHROMA_BOTTOM, FLAGS, "chroma" },
    { "cin",        "set input chroma location", OFFSET(chromal_in), AV_OPT_TYPE_INT, {.i64 = -1}, -1, ZIMG_CHROMA_BOTTOM, FLAGS, "chroma" },
    { "npl",       "set nominal peak luminance", OFFSET(nominal_peak_luminance), AV_OPT_TYPE_DOUBLE, {.dbl = NAN}, 0, DBL_MAX, FLAGS },
    { "agamma",       "allow approximate gamma", OFFSET(approximate_gamma),      AV_OPT_TYPE_BOOL,   {.i64 = 1},   0, 1,       FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(zscale);

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
