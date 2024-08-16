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
 * scale video filter
 */

#include <float.h>
#include <stdio.h>
#include <string.h>

#include "avfilter.h"
#include "filters.h"
#include "formats.h"
#include "framesync.h"
#include "scale_eval.h"
#include "video.h"
#include "libavutil/eval.h"
#include "libavutil/imgutils_internal.h"
#include "libavutil/internal.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/pixdesc.h"
#include "libswscale/swscale.h"

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
    "n",
    "t",
#if FF_API_FRAME_PKT
    "pos",
#endif
    "ref_w", "rw",
    "ref_h", "rh",
    "ref_a",
    "ref_sar",
    "ref_dar", "rdar",
    "ref_hsub",
    "ref_vsub",
    "ref_n",
    "ref_t",
    "ref_pos",
    /* Legacy variables for scale2ref */
    "main_w",
    "main_h",
    "main_a",
    "main_sar",
    "main_dar", "mdar",
    "main_hsub",
    "main_vsub",
    "main_n",
    "main_t",
    "main_pos",
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
    VAR_N,
    VAR_T,
#if FF_API_FRAME_PKT
    VAR_POS,
#endif
    VAR_REF_W, VAR_RW,
    VAR_REF_H, VAR_RH,
    VAR_REF_A,
    VAR_REF_SAR,
    VAR_REF_DAR, VAR_RDAR,
    VAR_REF_HSUB,
    VAR_REF_VSUB,
    VAR_REF_N,
    VAR_REF_T,
    VAR_REF_POS,
    VAR_S2R_MAIN_W,
    VAR_S2R_MAIN_H,
    VAR_S2R_MAIN_A,
    VAR_S2R_MAIN_SAR,
    VAR_S2R_MAIN_DAR, VAR_S2R_MDAR,
    VAR_S2R_MAIN_HSUB,
    VAR_S2R_MAIN_VSUB,
    VAR_S2R_MAIN_N,
    VAR_S2R_MAIN_T,
    VAR_S2R_MAIN_POS,
    VARS_NB
};

enum EvalMode {
    EVAL_MODE_INIT,
    EVAL_MODE_FRAME,
    EVAL_MODE_NB
};

typedef struct ScaleContext {
    const AVClass *class;
    struct SwsContext *sws;     ///< software scaler context
    struct SwsContext *isws[2]; ///< software scaler context for interlaced material
    // context used for forwarding options to sws
    struct SwsContext *sws_opts;
    FFFrameSync fs;

    /**
     * New dimensions. Special values are:
     *   0 = original width/height
     *  -1 = keep original aspect
     *  -N = try to keep aspect but make sure it is divisible by N
     */
    int w, h;
    char *size_str;
    double param[2];            // sws params

    int hsub, vsub;             ///< chroma subsampling
    int slice_y;                ///< top of current output slice
    int input_is_pal;           ///< set to 1 if the input format is paletted
    int output_is_pal;          ///< set to 1 if the output format is paletted
    int interlaced;
    int uses_ref;

    char *w_expr;               ///< width  expression string
    char *h_expr;               ///< height expression string
    AVExpr *w_pexpr;
    AVExpr *h_pexpr;
    double var_values[VARS_NB];

    char *flags_str;

    int in_color_matrix;
    int out_color_matrix;

    int in_range;
    int out_range;

    int in_chroma_loc;
    int out_chroma_loc;
    int out_h_chr_pos;
    int out_v_chr_pos;
    int in_h_chr_pos;
    int in_v_chr_pos;

    int force_original_aspect_ratio;
    int force_divisible_by;

    int eval_mode;              ///< expression evaluation mode

} ScaleContext;

const AVFilter ff_vf_scale2ref;

static int config_props(AVFilterLink *outlink);

static int check_exprs(AVFilterContext *ctx)
{
    ScaleContext *scale = ctx->priv;
    unsigned vars_w[VARS_NB] = { 0 }, vars_h[VARS_NB] = { 0 };

    if (!scale->w_pexpr && !scale->h_pexpr)
        return AVERROR(EINVAL);

    if (scale->w_pexpr)
        av_expr_count_vars(scale->w_pexpr, vars_w, VARS_NB);
    if (scale->h_pexpr)
        av_expr_count_vars(scale->h_pexpr, vars_h, VARS_NB);

    if (vars_w[VAR_OUT_W] || vars_w[VAR_OW]) {
        av_log(ctx, AV_LOG_ERROR, "Width expression cannot be self-referencing: '%s'.\n", scale->w_expr);
        return AVERROR(EINVAL);
    }

    if (vars_h[VAR_OUT_H] || vars_h[VAR_OH]) {
        av_log(ctx, AV_LOG_ERROR, "Height expression cannot be self-referencing: '%s'.\n", scale->h_expr);
        return AVERROR(EINVAL);
    }

    if ((vars_w[VAR_OUT_H] || vars_w[VAR_OH]) &&
        (vars_h[VAR_OUT_W] || vars_h[VAR_OW])) {
        av_log(ctx, AV_LOG_WARNING, "Circular references detected for width '%s' and height '%s' - possibly invalid.\n", scale->w_expr, scale->h_expr);
    }

    if (vars_w[VAR_REF_W]    || vars_h[VAR_REF_W]    ||
        vars_w[VAR_RW]       || vars_h[VAR_RW]       ||
        vars_w[VAR_REF_H]    || vars_h[VAR_REF_H]    ||
        vars_w[VAR_RH]       || vars_h[VAR_RH]       ||
        vars_w[VAR_REF_A]    || vars_h[VAR_REF_A]    ||
        vars_w[VAR_REF_SAR]  || vars_h[VAR_REF_SAR]  ||
        vars_w[VAR_REF_DAR]  || vars_h[VAR_REF_DAR]  ||
        vars_w[VAR_RDAR]     || vars_h[VAR_RDAR]     ||
        vars_w[VAR_REF_HSUB] || vars_h[VAR_REF_HSUB] ||
        vars_w[VAR_REF_VSUB] || vars_h[VAR_REF_VSUB] ||
        vars_w[VAR_REF_N]    || vars_h[VAR_REF_N]    ||
        vars_w[VAR_REF_T]    || vars_h[VAR_REF_T]    ||
        vars_w[VAR_REF_POS]  || vars_h[VAR_REF_POS]) {
        scale->uses_ref = 1;
    }

    if (ctx->filter != &ff_vf_scale2ref &&
        (vars_w[VAR_S2R_MAIN_W]    || vars_h[VAR_S2R_MAIN_W]    ||
         vars_w[VAR_S2R_MAIN_H]    || vars_h[VAR_S2R_MAIN_H]    ||
         vars_w[VAR_S2R_MAIN_A]    || vars_h[VAR_S2R_MAIN_A]    ||
         vars_w[VAR_S2R_MAIN_SAR]  || vars_h[VAR_S2R_MAIN_SAR]  ||
         vars_w[VAR_S2R_MAIN_DAR]  || vars_h[VAR_S2R_MAIN_DAR]  ||
         vars_w[VAR_S2R_MDAR]      || vars_h[VAR_S2R_MDAR]      ||
         vars_w[VAR_S2R_MAIN_HSUB] || vars_h[VAR_S2R_MAIN_HSUB] ||
         vars_w[VAR_S2R_MAIN_VSUB] || vars_h[VAR_S2R_MAIN_VSUB] ||
         vars_w[VAR_S2R_MAIN_N]    || vars_h[VAR_S2R_MAIN_N]    ||
         vars_w[VAR_S2R_MAIN_T]    || vars_h[VAR_S2R_MAIN_T]    ||
         vars_w[VAR_S2R_MAIN_POS]  || vars_h[VAR_S2R_MAIN_POS]) ) {
        av_log(ctx, AV_LOG_ERROR, "Expressions with scale2ref variables are not valid in scale filter.\n");
        return AVERROR(EINVAL);
    }

    if (ctx->filter != &ff_vf_scale2ref &&
        (vars_w[VAR_S2R_MAIN_W]    || vars_h[VAR_S2R_MAIN_W]    ||
         vars_w[VAR_S2R_MAIN_H]    || vars_h[VAR_S2R_MAIN_H]    ||
         vars_w[VAR_S2R_MAIN_A]    || vars_h[VAR_S2R_MAIN_A]    ||
         vars_w[VAR_S2R_MAIN_SAR]  || vars_h[VAR_S2R_MAIN_SAR]  ||
         vars_w[VAR_S2R_MAIN_DAR]  || vars_h[VAR_S2R_MAIN_DAR]  ||
         vars_w[VAR_S2R_MDAR]      || vars_h[VAR_S2R_MDAR]      ||
         vars_w[VAR_S2R_MAIN_HSUB] || vars_h[VAR_S2R_MAIN_HSUB] ||
         vars_w[VAR_S2R_MAIN_VSUB] || vars_h[VAR_S2R_MAIN_VSUB] ||
         vars_w[VAR_S2R_MAIN_N]    || vars_h[VAR_S2R_MAIN_N]    ||
         vars_w[VAR_S2R_MAIN_T]    || vars_h[VAR_S2R_MAIN_T]    ||
         vars_w[VAR_S2R_MAIN_POS]  || vars_h[VAR_S2R_MAIN_POS]) ) {
        av_log(ctx, AV_LOG_ERROR, "Expressions with scale2ref variables are not valid in scale filter.\n");
        return AVERROR(EINVAL);
    }

    if (scale->eval_mode == EVAL_MODE_INIT &&
        (vars_w[VAR_N]            || vars_h[VAR_N]           ||
         vars_w[VAR_T]            || vars_h[VAR_T]           ||
#if FF_API_FRAME_PKT
         vars_w[VAR_POS]          || vars_h[VAR_POS]         ||
#endif
         vars_w[VAR_S2R_MAIN_N]   || vars_h[VAR_S2R_MAIN_N]  ||
         vars_w[VAR_S2R_MAIN_T]   || vars_h[VAR_S2R_MAIN_T]  ||
         vars_w[VAR_S2R_MAIN_POS] || vars_h[VAR_S2R_MAIN_POS]) ) {
        av_log(ctx, AV_LOG_ERROR, "Expressions with frame variables 'n', 't', 'pos' are not valid in init eval_mode.\n");
        return AVERROR(EINVAL);
    }

    return 0;
}

static int scale_parse_expr(AVFilterContext *ctx, char *str_expr, AVExpr **pexpr_ptr, const char *var, const char *args)
{
    ScaleContext *scale = ctx->priv;
    int ret, is_inited = 0;
    char *old_str_expr = NULL;
    AVExpr *old_pexpr = NULL;

    if (str_expr) {
        old_str_expr = av_strdup(str_expr);
        if (!old_str_expr)
            return AVERROR(ENOMEM);
        av_opt_set(scale, var, args, 0);
    }

    if (*pexpr_ptr) {
        old_pexpr = *pexpr_ptr;
        *pexpr_ptr = NULL;
        is_inited = 1;
    }

    ret = av_expr_parse(pexpr_ptr, args, var_names,
                        NULL, NULL, NULL, NULL, 0, ctx);
    if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Cannot parse expression for %s: '%s'\n", var, args);
        goto revert;
    }

    ret = check_exprs(ctx);
    if (ret < 0)
        goto revert;

    if (is_inited && (ret = config_props(ctx->outputs[0])) < 0)
        goto revert;

    av_expr_free(old_pexpr);
    old_pexpr = NULL;
    av_freep(&old_str_expr);

    return 0;

revert:
    av_expr_free(*pexpr_ptr);
    *pexpr_ptr = NULL;
    if (old_str_expr) {
        av_opt_set(scale, var, old_str_expr, 0);
        av_free(old_str_expr);
    }
    if (old_pexpr)
        *pexpr_ptr = old_pexpr;

    return ret;
}

static av_cold int preinit(AVFilterContext *ctx)
{
    ScaleContext *scale = ctx->priv;
    int ret;

    scale->sws_opts = sws_alloc_context();
    if (!scale->sws_opts)
        return AVERROR(ENOMEM);

    // set threads=0, so we can later check whether the user modified it
    ret = av_opt_set_int(scale->sws_opts, "threads", 0, 0);
    if (ret < 0)
        return ret;

    ff_framesync_preinit(&scale->fs);

    return 0;
}

static const int sws_colorspaces[] = {
    AVCOL_SPC_UNSPECIFIED,
    AVCOL_SPC_RGB,
    AVCOL_SPC_BT709,
    AVCOL_SPC_BT470BG,
    AVCOL_SPC_SMPTE170M,
    AVCOL_SPC_FCC,
    AVCOL_SPC_SMPTE240M,
    AVCOL_SPC_BT2020_NCL,
    -1
};

static int do_scale(FFFrameSync *fs);

static av_cold int init(AVFilterContext *ctx)
{
    ScaleContext *scale = ctx->priv;
    int64_t threads;
    int ret;

    if (ctx->filter == &ff_vf_scale2ref)
        av_log(ctx, AV_LOG_WARNING, "scale2ref is deprecated, use scale=rw:rh instead\n");

    if (scale->size_str && (scale->w_expr || scale->h_expr)) {
        av_log(ctx, AV_LOG_ERROR,
               "Size and width/height expressions cannot be set at the same time.\n");
            return AVERROR(EINVAL);
    }

    if (scale->w_expr && !scale->h_expr)
        FFSWAP(char *, scale->w_expr, scale->size_str);

    if (scale->size_str) {
        char buf[32];
        if ((ret = av_parse_video_size(&scale->w, &scale->h, scale->size_str)) < 0) {
            av_log(ctx, AV_LOG_ERROR,
                   "Invalid size '%s'\n", scale->size_str);
            return ret;
        }
        snprintf(buf, sizeof(buf)-1, "%d", scale->w);
        av_opt_set(scale, "w", buf, 0);
        snprintf(buf, sizeof(buf)-1, "%d", scale->h);
        av_opt_set(scale, "h", buf, 0);
    }
    if (!scale->w_expr)
        av_opt_set(scale, "w", "iw", 0);
    if (!scale->h_expr)
        av_opt_set(scale, "h", "ih", 0);

    ret = scale_parse_expr(ctx, NULL, &scale->w_pexpr, "width", scale->w_expr);
    if (ret < 0)
        return ret;

    ret = scale_parse_expr(ctx, NULL, &scale->h_pexpr, "height", scale->h_expr);
    if (ret < 0)
        return ret;

    if (scale->in_color_matrix != -1 &&
        !ff_fmt_is_in(scale->in_color_matrix, sws_colorspaces)) {
        av_log(ctx, AV_LOG_ERROR, "Unsupported input color matrix '%s'\n",
               av_color_space_name(scale->in_color_matrix));
        return AVERROR(EINVAL);
    }

    if (!ff_fmt_is_in(scale->out_color_matrix, sws_colorspaces)) {
        av_log(ctx, AV_LOG_ERROR, "Unsupported output color matrix '%s'\n",
               av_color_space_name(scale->out_color_matrix));
        return AVERROR(EINVAL);
    }

    av_log(ctx, AV_LOG_VERBOSE, "w:%s h:%s flags:'%s' interl:%d\n",
           scale->w_expr, scale->h_expr, (char *)av_x_if_null(scale->flags_str, ""), scale->interlaced);

    if (scale->flags_str && *scale->flags_str) {
        ret = av_opt_set(scale->sws_opts, "sws_flags", scale->flags_str, 0);
        if (ret < 0)
            return ret;
    }

    for (int i = 0; i < FF_ARRAY_ELEMS(scale->param); i++)
        if (scale->param[i] != DBL_MAX) {
            ret = av_opt_set_double(scale->sws_opts, i ? "param1" : "param0",
                                    scale->param[i], 0);
            if (ret < 0)
                return ret;
        }

    // use generic thread-count if the user did not set it explicitly
    ret = av_opt_get_int(scale->sws_opts, "threads", 0, &threads);
    if (ret < 0)
        return ret;
    if (!threads)
        av_opt_set_int(scale->sws_opts, "threads", ff_filter_get_nb_threads(ctx), 0);

    if (ctx->filter != &ff_vf_scale2ref && scale->uses_ref) {
        AVFilterPad pad = {
            .name = "ref",
            .type = AVMEDIA_TYPE_VIDEO,
        };
        ret = ff_append_inpad(ctx, &pad);
        if (ret < 0)
            return ret;
    }

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    ScaleContext *scale = ctx->priv;
    av_expr_free(scale->w_pexpr);
    av_expr_free(scale->h_pexpr);
    scale->w_pexpr = scale->h_pexpr = NULL;
    ff_framesync_uninit(&scale->fs);
    sws_freeContext(scale->sws_opts);
    sws_freeContext(scale->sws);
    sws_freeContext(scale->isws[0]);
    sws_freeContext(scale->isws[1]);
    scale->sws = NULL;
}

static int query_formats(AVFilterContext *ctx)
{
    ScaleContext *scale = ctx->priv;
    AVFilterFormats *formats;
    const AVPixFmtDescriptor *desc;
    enum AVPixelFormat pix_fmt;
    int ret;

    desc    = NULL;
    formats = NULL;
    while ((desc = av_pix_fmt_desc_next(desc))) {
        pix_fmt = av_pix_fmt_desc_get_id(desc);
        if ((sws_isSupportedInput(pix_fmt) ||
             sws_isSupportedEndiannessConversion(pix_fmt))
            && (ret = ff_add_format(&formats, pix_fmt)) < 0) {
            return ret;
        }
    }
    if ((ret = ff_formats_ref(formats, &ctx->inputs[0]->outcfg.formats)) < 0)
        return ret;

    desc    = NULL;
    formats = NULL;
    while ((desc = av_pix_fmt_desc_next(desc))) {
        pix_fmt = av_pix_fmt_desc_get_id(desc);
        if ((sws_isSupportedOutput(pix_fmt) || pix_fmt == AV_PIX_FMT_PAL8 ||
             sws_isSupportedEndiannessConversion(pix_fmt))
            && (ret = ff_add_format(&formats, pix_fmt)) < 0) {
            return ret;
        }
    }
    if ((ret = ff_formats_ref(formats, &ctx->outputs[0]->incfg.formats)) < 0)
        return ret;

    /* accept all supported inputs, even if user overrides their properties */
    if ((ret = ff_formats_ref(ff_make_format_list(sws_colorspaces),
                              &ctx->inputs[0]->outcfg.color_spaces)) < 0)
        return ret;

    if ((ret = ff_formats_ref(ff_all_color_ranges(),
                              &ctx->inputs[0]->outcfg.color_ranges)) < 0)
        return ret;

    /* propagate output properties if overridden */
    formats = scale->out_color_matrix != AVCOL_SPC_UNSPECIFIED
                ? ff_make_formats_list_singleton(scale->out_color_matrix)
                : ff_make_format_list(sws_colorspaces);
    if ((ret = ff_formats_ref(formats, &ctx->outputs[0]->incfg.color_spaces)) < 0)
        return ret;

    formats = scale->out_range != AVCOL_RANGE_UNSPECIFIED
                ? ff_make_formats_list_singleton(scale->out_range)
                : ff_all_color_ranges();
    if ((ret = ff_formats_ref(formats, &ctx->outputs[0]->incfg.color_ranges)) < 0)
        return ret;

    return 0;
}

static int scale_eval_dimensions(AVFilterContext *ctx)
{
    ScaleContext *scale = ctx->priv;
    const char scale2ref = ctx->filter == &ff_vf_scale2ref;
    const AVFilterLink *inlink = scale2ref ? ctx->inputs[1] : ctx->inputs[0];
    const AVFilterLink *outlink = ctx->outputs[0];
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    const AVPixFmtDescriptor *out_desc = av_pix_fmt_desc_get(outlink->format);
    char *expr;
    int eval_w, eval_h;
    int ret;
    double res;
    const AVPixFmtDescriptor *main_desc;
    const AVFilterLink *main_link;

    if (scale2ref) {
        main_link = ctx->inputs[0];
        main_desc = av_pix_fmt_desc_get(main_link->format);
    }

    scale->var_values[VAR_IN_W]  = scale->var_values[VAR_IW] = inlink->w;
    scale->var_values[VAR_IN_H]  = scale->var_values[VAR_IH] = inlink->h;
    scale->var_values[VAR_OUT_W] = scale->var_values[VAR_OW] = NAN;
    scale->var_values[VAR_OUT_H] = scale->var_values[VAR_OH] = NAN;
    scale->var_values[VAR_A]     = (double) inlink->w / inlink->h;
    scale->var_values[VAR_SAR]   = inlink->sample_aspect_ratio.num ?
        (double) inlink->sample_aspect_ratio.num / inlink->sample_aspect_ratio.den : 1;
    scale->var_values[VAR_DAR]   = scale->var_values[VAR_A] * scale->var_values[VAR_SAR];
    scale->var_values[VAR_HSUB]  = 1 << desc->log2_chroma_w;
    scale->var_values[VAR_VSUB]  = 1 << desc->log2_chroma_h;
    scale->var_values[VAR_OHSUB] = 1 << out_desc->log2_chroma_w;
    scale->var_values[VAR_OVSUB] = 1 << out_desc->log2_chroma_h;

    if (scale2ref) {
        scale->var_values[VAR_S2R_MAIN_W] = main_link->w;
        scale->var_values[VAR_S2R_MAIN_H] = main_link->h;
        scale->var_values[VAR_S2R_MAIN_A] = (double) main_link->w / main_link->h;
        scale->var_values[VAR_S2R_MAIN_SAR] = main_link->sample_aspect_ratio.num ?
            (double) main_link->sample_aspect_ratio.num / main_link->sample_aspect_ratio.den : 1;
        scale->var_values[VAR_S2R_MAIN_DAR] = scale->var_values[VAR_S2R_MDAR] =
            scale->var_values[VAR_S2R_MAIN_A] * scale->var_values[VAR_S2R_MAIN_SAR];
        scale->var_values[VAR_S2R_MAIN_HSUB] = 1 << main_desc->log2_chroma_w;
        scale->var_values[VAR_S2R_MAIN_VSUB] = 1 << main_desc->log2_chroma_h;
    }

    if (scale->uses_ref) {
        const AVFilterLink *reflink = ctx->inputs[1];
        const AVPixFmtDescriptor *ref_desc = av_pix_fmt_desc_get(reflink->format);
        scale->var_values[VAR_REF_W] = scale->var_values[VAR_RW] = reflink->w;
        scale->var_values[VAR_REF_H] = scale->var_values[VAR_RH] = reflink->h;
        scale->var_values[VAR_REF_A] = (double) reflink->w / reflink->h;
        scale->var_values[VAR_REF_SAR] = reflink->sample_aspect_ratio.num ?
            (double) reflink->sample_aspect_ratio.num / reflink->sample_aspect_ratio.den : 1;
        scale->var_values[VAR_REF_DAR] = scale->var_values[VAR_RDAR] =
            scale->var_values[VAR_REF_A] * scale->var_values[VAR_REF_SAR];
        scale->var_values[VAR_REF_HSUB] = 1 << ref_desc->log2_chroma_w;
        scale->var_values[VAR_REF_VSUB] = 1 << ref_desc->log2_chroma_h;
    }

    res = av_expr_eval(scale->w_pexpr, scale->var_values, NULL);
    eval_w = scale->var_values[VAR_OUT_W] = scale->var_values[VAR_OW] = (int) res == 0 ? inlink->w : (int) res;

    res = av_expr_eval(scale->h_pexpr, scale->var_values, NULL);
    if (isnan(res)) {
        expr = scale->h_expr;
        ret = AVERROR(EINVAL);
        goto fail;
    }
    eval_h = scale->var_values[VAR_OUT_H] = scale->var_values[VAR_OH] = (int) res == 0 ? inlink->h : (int) res;

    res = av_expr_eval(scale->w_pexpr, scale->var_values, NULL);
    if (isnan(res)) {
        expr = scale->w_expr;
        ret = AVERROR(EINVAL);
        goto fail;
    }
    eval_w = scale->var_values[VAR_OUT_W] = scale->var_values[VAR_OW] = (int) res == 0 ? inlink->w : (int) res;

    scale->w = eval_w;
    scale->h = eval_h;

    return 0;

fail:
    av_log(ctx, AV_LOG_ERROR,
           "Error when evaluating the expression '%s'.\n", expr);
    return ret;
}

static void calc_chroma_pos(int *h_pos_out, int *v_pos_out, int chroma_loc,
                            int h_pos_override, int v_pos_override,
                            int h_sub, int v_sub, int index)
{
    int h_pos, v_pos;

    /* Explicitly default to center siting for compatibility with swscale */
    if (chroma_loc == AVCHROMA_LOC_UNSPECIFIED)
        chroma_loc = AVCHROMA_LOC_CENTER;

    /* av_chroma_location_enum_to_pos() always gives us values in the range from
     * 0 to 256, but we need to adjust this to the true value range of the
     * subsampling grid, which may be larger for h/v_sub > 1 */
    av_chroma_location_enum_to_pos(&h_pos, &v_pos, chroma_loc);
    h_pos *= (1 << h_sub) - 1;
    v_pos *= (1 << v_sub) - 1;

    if (h_pos_override != -513)
        h_pos = h_pos_override;
    if (v_pos_override != -513)
        v_pos = v_pos_override;

    /* Fix vertical chroma position for interlaced frames */
    if (v_sub && index > 0) {
        /* When vertically subsampling, chroma samples are effectively only
         * placed next to even rows. To access them from the odd field, we need
         * to account for this shift by offsetting the distance of one luma row.
         *
         * For 4x vertical subsampling (v_sub == 2), they are only placed
         * next to every *other* even row, so we need to shift by three luma
         * rows to get to the chroma sample. */
        if (index == 2)
            v_pos += (256 << v_sub) - 256;

        /* Luma row distance is doubled for fields, so halve offsets */
        v_pos >>= 1;
    }

    /* Explicitly strip chroma offsets when not subsampling, because it
     * interferes with the operation of flags like SWS_FULL_CHR_H_INP */
    *h_pos_out = h_sub ? h_pos : -513;
    *v_pos_out = v_sub ? v_pos : -513;
}

static int config_props(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink0 = outlink->src->inputs[0];
    AVFilterLink *inlink  = ctx->filter == &ff_vf_scale2ref ?
                            outlink->src->inputs[1] :
                            outlink->src->inputs[0];
    enum AVPixelFormat outfmt = outlink->format;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    const AVPixFmtDescriptor *outdesc = av_pix_fmt_desc_get(outfmt);
    ScaleContext *scale = ctx->priv;
    uint8_t *flags_val = NULL;
    int in_range, in_colorspace;
    int ret;

    if ((ret = scale_eval_dimensions(ctx)) < 0)
        goto fail;

    outlink->w = scale->w;
    outlink->h = scale->h;

    ret = ff_scale_adjust_dimensions(inlink, &outlink->w, &outlink->h,
                               scale->force_original_aspect_ratio,
                               scale->force_divisible_by);

    if (ret < 0)
        goto fail;

    if (outlink->w > INT_MAX ||
        outlink->h > INT_MAX ||
        (outlink->h * inlink->w) > INT_MAX ||
        (outlink->w * inlink->h) > INT_MAX)
        av_log(ctx, AV_LOG_ERROR, "Rescaled value for width or height is too big.\n");

    /* TODO: make algorithm configurable */

    scale->input_is_pal = desc->flags & AV_PIX_FMT_FLAG_PAL;
    if (outfmt == AV_PIX_FMT_PAL8) outfmt = AV_PIX_FMT_BGR8;
    scale->output_is_pal = av_pix_fmt_desc_get(outfmt)->flags & AV_PIX_FMT_FLAG_PAL;

    in_range = scale->in_range;
    if (in_range == AVCOL_RANGE_UNSPECIFIED)
        in_range = inlink0->color_range;

    in_colorspace = scale->in_color_matrix;
    if (in_colorspace == -1 /* auto */)
        in_colorspace = inlink0->colorspace;

    if (scale->sws)
        sws_freeContext(scale->sws);
    if (scale->isws[0])
        sws_freeContext(scale->isws[0]);
    if (scale->isws[1])
        sws_freeContext(scale->isws[1]);
    scale->isws[0] = scale->isws[1] = scale->sws = NULL;
    if (inlink0->w == outlink->w &&
        inlink0->h == outlink->h &&
        in_range == outlink->color_range &&
        in_colorspace == outlink->colorspace &&
        inlink0->format == outlink->format &&
        scale->in_chroma_loc == scale->out_chroma_loc)
        ;
    else {
        struct SwsContext **swscs[3] = {&scale->sws, &scale->isws[0], &scale->isws[1]};
        int i;

        for (i = 0; i < 3; i++) {
            int in_full, out_full, brightness, contrast, saturation;
            int h_chr_pos, v_chr_pos;
            const int *inv_table, *table;
            struct SwsContext *const s = sws_alloc_context();
            if (!s)
                return AVERROR(ENOMEM);
            *swscs[i] = s;

            ret = av_opt_copy(s, scale->sws_opts);
            if (ret < 0)
                return ret;

            av_opt_set_int(s, "srcw", inlink0 ->w, 0);
            av_opt_set_int(s, "srch", inlink0 ->h >> !!i, 0);
            av_opt_set_int(s, "src_format", inlink0->format, 0);
            av_opt_set_int(s, "dstw", outlink->w, 0);
            av_opt_set_int(s, "dsth", outlink->h >> !!i, 0);
            av_opt_set_int(s, "dst_format", outfmt, 0);
            if (in_range != AVCOL_RANGE_UNSPECIFIED)
                av_opt_set_int(s, "src_range",
                               in_range == AVCOL_RANGE_JPEG, 0);
            if (outlink->color_range != AVCOL_RANGE_UNSPECIFIED)
                av_opt_set_int(s, "dst_range",
                               outlink->color_range == AVCOL_RANGE_JPEG, 0);

            calc_chroma_pos(&h_chr_pos, &v_chr_pos, scale->in_chroma_loc,
                            scale->in_h_chr_pos, scale->in_v_chr_pos,
                            desc->log2_chroma_w, desc->log2_chroma_h, i);
            av_opt_set_int(s, "src_h_chr_pos", h_chr_pos, 0);
            av_opt_set_int(s, "src_v_chr_pos", v_chr_pos, 0);

            calc_chroma_pos(&h_chr_pos, &v_chr_pos, scale->out_chroma_loc,
                            scale->out_h_chr_pos, scale->out_v_chr_pos,
                            outdesc->log2_chroma_w, outdesc->log2_chroma_h, i);
            av_opt_set_int(s, "dst_h_chr_pos", h_chr_pos, 0);
            av_opt_set_int(s, "dst_v_chr_pos", v_chr_pos, 0);

            if ((ret = sws_init_context(s, NULL, NULL)) < 0)
                return ret;

            sws_getColorspaceDetails(s, (int **)&inv_table, &in_full,
                                     (int **)&table, &out_full,
                                     &brightness, &contrast, &saturation);

            if (scale->in_color_matrix == -1 /* auto */)
                inv_table = sws_getCoefficients(inlink0->colorspace);
            else if (scale->in_color_matrix != AVCOL_SPC_UNSPECIFIED)
                inv_table = sws_getCoefficients(scale->in_color_matrix);
            if (outlink->colorspace != AVCOL_SPC_UNSPECIFIED)
                table = sws_getCoefficients(outlink->colorspace);
            else if (scale->in_color_matrix != AVCOL_SPC_UNSPECIFIED)
                table = inv_table;

            sws_setColorspaceDetails(s, inv_table, in_full,
                                     table, out_full,
                                     brightness, contrast, saturation);

            if (!scale->interlaced)
                break;
        }
    }

    if (inlink0->sample_aspect_ratio.num){
        outlink->sample_aspect_ratio = av_mul_q((AVRational){outlink->h * inlink0->w, outlink->w * inlink0->h}, inlink0->sample_aspect_ratio);
    } else
        outlink->sample_aspect_ratio = inlink0->sample_aspect_ratio;

    if (scale->sws)
        av_opt_get(scale->sws, "sws_flags", 0, &flags_val);

    av_log(ctx, AV_LOG_VERBOSE, "w:%d h:%d fmt:%s csp:%s range:%s sar:%d/%d -> w:%d h:%d fmt:%s csp:%s range:%s sar:%d/%d flags:%s\n",
           inlink ->w, inlink ->h, av_get_pix_fmt_name( inlink->format),
           av_color_space_name(inlink->colorspace), av_color_range_name(inlink->color_range),
           inlink->sample_aspect_ratio.num, inlink->sample_aspect_ratio.den,
           outlink->w, outlink->h, av_get_pix_fmt_name(outlink->format),
           av_color_space_name(outlink->colorspace), av_color_range_name(outlink->color_range),
           outlink->sample_aspect_ratio.num, outlink->sample_aspect_ratio.den,
           flags_val);
    av_freep(&flags_val);

    if (ctx->filter != &ff_vf_scale2ref) {
        ff_framesync_uninit(&scale->fs);
        ret = ff_framesync_init(&scale->fs, ctx, ctx->nb_inputs);
        if (ret < 0)
            return ret;
        scale->fs.on_event        = do_scale;
        scale->fs.in[0].time_base = ctx->inputs[0]->time_base;
        scale->fs.in[0].sync      = 1;
        scale->fs.in[0].before    = EXT_STOP;
        scale->fs.in[0].after     = EXT_STOP;
        if (scale->uses_ref) {
            av_assert0(ctx->nb_inputs == 2);
            scale->fs.in[1].time_base = ctx->inputs[1]->time_base;
            scale->fs.in[1].sync      = 0;
            scale->fs.in[1].before    = EXT_NULL;
            scale->fs.in[1].after     = EXT_INFINITY;
        }

        ret = ff_framesync_configure(&scale->fs);
        if (ret < 0)
            return ret;
    }

    return 0;

fail:
    return ret;
}

static int config_props_ref(AVFilterLink *outlink)
{
    AVFilterLink *inlink = outlink->src->inputs[1];
    FilterLink *il = ff_filter_link(inlink);
    FilterLink *ol = ff_filter_link(outlink);

    outlink->w = inlink->w;
    outlink->h = inlink->h;
    outlink->sample_aspect_ratio = inlink->sample_aspect_ratio;
    outlink->time_base = inlink->time_base;
    ol->frame_rate = il->frame_rate;
    outlink->colorspace = inlink->colorspace;
    outlink->color_range = inlink->color_range;

    return 0;
}

static int request_frame(AVFilterLink *outlink)
{
    return ff_request_frame(outlink->src->inputs[0]);
}

static int request_frame_ref(AVFilterLink *outlink)
{
    return ff_request_frame(outlink->src->inputs[1]);
}

static void frame_offset(AVFrame *frame, int dir, int is_pal)
{
    for (int i = 0; i < 4 && frame->data[i]; i++) {
        if (i == 1 && is_pal)
            break;
        frame->data[i] += frame->linesize[i] * dir;
    }
}

static int scale_field(ScaleContext *scale, AVFrame *dst, AVFrame *src,
                       int field)
{
    int orig_h_src = src->height;
    int orig_h_dst = dst->height;
    int ret;

    // offset the data pointers for the bottom field
    if (field) {
        frame_offset(src, 1, scale->input_is_pal);
        frame_offset(dst, 1, scale->output_is_pal);
    }

    // take every second line
    for (int i = 0; i < 4; i++) {
        src->linesize[i] *= 2;
        dst->linesize[i] *= 2;
    }
    src->height /= 2;
    dst->height /= 2;

    ret = sws_scale_frame(scale->isws[field], dst, src);
    if (ret < 0)
        return ret;

    // undo the changes we made above
    for (int i = 0; i < 4; i++) {
        src->linesize[i] /= 2;
        dst->linesize[i] /= 2;
    }
    src->height = orig_h_src;
    dst->height = orig_h_dst;

    if (field) {
        frame_offset(src, -1, scale->input_is_pal);
        frame_offset(dst, -1, scale->output_is_pal);
    }

    return 0;
}

/* Takes over ownership of *frame_in, passes ownership of *frame_out to caller */
static int scale_frame(AVFilterLink *link, AVFrame **frame_in,
                       AVFrame **frame_out)
{
    FilterLink *inl = ff_filter_link(link);
    AVFilterContext *ctx = link->dst;
    ScaleContext *scale = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out, *in = *frame_in;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(link->format);
    char buf[32];
    int ret;
    int frame_changed;

    *frame_in = NULL;
    if (in->colorspace == AVCOL_SPC_YCGCO)
        av_log(link->dst, AV_LOG_WARNING, "Detected unsupported YCgCo colorspace.\n");

    frame_changed = in->width  != link->w ||
                    in->height != link->h ||
                    in->format != link->format ||
                    in->sample_aspect_ratio.den != link->sample_aspect_ratio.den ||
                    in->sample_aspect_ratio.num != link->sample_aspect_ratio.num ||
                    in->colorspace != link->colorspace ||
                    in->color_range != link->color_range;

    if (scale->eval_mode == EVAL_MODE_FRAME || frame_changed) {
        unsigned vars_w[VARS_NB] = { 0 }, vars_h[VARS_NB] = { 0 };

        av_expr_count_vars(scale->w_pexpr, vars_w, VARS_NB);
        av_expr_count_vars(scale->h_pexpr, vars_h, VARS_NB);

        if (scale->eval_mode == EVAL_MODE_FRAME &&
            !frame_changed &&
            ctx->filter != &ff_vf_scale2ref &&
            !(vars_w[VAR_N] || vars_w[VAR_T]
#if FF_API_FRAME_PKT
              || vars_w[VAR_POS]
#endif
              ) &&
            !(vars_h[VAR_N] || vars_h[VAR_T]
#if FF_API_FRAME_PKT
              || vars_h[VAR_POS]
#endif
              ) &&
            scale->w && scale->h)
            goto scale;

        if (scale->eval_mode == EVAL_MODE_INIT) {
            snprintf(buf, sizeof(buf) - 1, "%d", scale->w);
            av_opt_set(scale, "w", buf, 0);
            snprintf(buf, sizeof(buf) - 1, "%d", scale->h);
            av_opt_set(scale, "h", buf, 0);

            ret = scale_parse_expr(ctx, NULL, &scale->w_pexpr, "width", scale->w_expr);
            if (ret < 0)
                goto err;

            ret = scale_parse_expr(ctx, NULL, &scale->h_pexpr, "height", scale->h_expr);
            if (ret < 0)
                goto err;
        }

        if (ctx->filter == &ff_vf_scale2ref) {
            scale->var_values[VAR_S2R_MAIN_N] = inl->frame_count_out;
            scale->var_values[VAR_S2R_MAIN_T] = TS2T(in->pts, link->time_base);
#if FF_API_FRAME_PKT
FF_DISABLE_DEPRECATION_WARNINGS
            scale->var_values[VAR_S2R_MAIN_POS] = in->pkt_pos == -1 ? NAN : in->pkt_pos;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
        } else {
            scale->var_values[VAR_N] = inl->frame_count_out;
            scale->var_values[VAR_T] = TS2T(in->pts, link->time_base);
#if FF_API_FRAME_PKT
FF_DISABLE_DEPRECATION_WARNINGS
            scale->var_values[VAR_POS] = in->pkt_pos == -1 ? NAN : in->pkt_pos;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
        }

        link->dst->inputs[0]->format        = in->format;
        link->dst->inputs[0]->w             = in->width;
        link->dst->inputs[0]->h             = in->height;
        link->dst->inputs[0]->colorspace    = in->colorspace;
        link->dst->inputs[0]->color_range   = in->color_range;

        link->dst->inputs[0]->sample_aspect_ratio.den = in->sample_aspect_ratio.den;
        link->dst->inputs[0]->sample_aspect_ratio.num = in->sample_aspect_ratio.num;

        if ((ret = config_props(outlink)) < 0)
            goto err;
    }

scale:
    if (!scale->sws) {
        *frame_out = in;
        return 0;
    }

    scale->hsub = desc->log2_chroma_w;
    scale->vsub = desc->log2_chroma_h;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        ret = AVERROR(ENOMEM);
        goto err;
    }

    av_frame_copy_props(out, in);
    out->width  = outlink->w;
    out->height = outlink->h;
    out->color_range = outlink->color_range;
    out->colorspace = outlink->colorspace;
    if (scale->out_chroma_loc != AVCHROMA_LOC_UNSPECIFIED)
        out->chroma_location = scale->out_chroma_loc;

    if (scale->output_is_pal)
        avpriv_set_systematic_pal2((uint32_t*)out->data[1], outlink->format == AV_PIX_FMT_PAL8 ? AV_PIX_FMT_BGR8 : outlink->format);

    av_reduce(&out->sample_aspect_ratio.num, &out->sample_aspect_ratio.den,
              (int64_t)in->sample_aspect_ratio.num * outlink->h * link->w,
              (int64_t)in->sample_aspect_ratio.den * outlink->w * link->h,
              INT_MAX);

    if (scale->interlaced>0 || (scale->interlaced<0 &&
        (in->flags & AV_FRAME_FLAG_INTERLACED))) {
        ret = scale_field(scale, out, in, 0);
        if (ret >= 0)
            ret = scale_field(scale, out, in, 1);
    } else {
        ret = sws_scale_frame(scale->sws, out, in);
    }

    if (ret < 0)
        av_frame_free(&out);
    *frame_out = out;

err:
    av_frame_free(&in);
    return ret;
}

static int do_scale(FFFrameSync *fs)
{
    AVFilterContext *ctx = fs->parent;
    ScaleContext *scale = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out, *in = NULL, *ref = NULL;
    int ret = 0, frame_changed;

    ret = ff_framesync_get_frame(fs, 0, &in, 1);
    if (ret < 0)
        goto err;

    if (scale->uses_ref) {
        ret = ff_framesync_get_frame(fs, 1, &ref, 0);
        if (ret < 0)
            goto err;
    }

    if (ref) {
        AVFilterLink *reflink = ctx->inputs[1];
        FilterLink      *rl   = ff_filter_link(reflink);

        frame_changed = ref->width  != reflink->w ||
                        ref->height != reflink->h ||
                        ref->format != reflink->format ||
                        ref->sample_aspect_ratio.den != reflink->sample_aspect_ratio.den ||
                        ref->sample_aspect_ratio.num != reflink->sample_aspect_ratio.num ||
                        ref->colorspace != reflink->colorspace ||
                        ref->color_range != reflink->color_range;

        if (frame_changed) {
            reflink->format = ref->format;
            reflink->w = ref->width;
            reflink->h = ref->height;
            reflink->sample_aspect_ratio.num = ref->sample_aspect_ratio.num;
            reflink->sample_aspect_ratio.den = ref->sample_aspect_ratio.den;
            reflink->colorspace = ref->colorspace;
            reflink->color_range = ref->color_range;

            ret = config_props(outlink);
            if (ret < 0)
                goto err;
        }

        if (scale->eval_mode == EVAL_MODE_FRAME) {
            scale->var_values[VAR_REF_N] = rl->frame_count_out;
            scale->var_values[VAR_REF_T] = TS2T(ref->pts, reflink->time_base);
#if FF_API_FRAME_PKT
FF_DISABLE_DEPRECATION_WARNINGS
            scale->var_values[VAR_REF_POS] = ref->pkt_pos == -1 ? NAN : ref->pkt_pos;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
        }
    }

    ret = scale_frame(ctx->inputs[0], &in, &out);
    if (ret < 0)
        goto err;

    av_assert0(out);
    out->pts = av_rescale_q(fs->pts, fs->time_base, outlink->time_base);
    return ff_filter_frame(outlink, out);

err:
    av_frame_free(&in);
    return ret;
}

static int filter_frame(AVFilterLink *link, AVFrame *in)
{
    AVFilterContext *ctx = link->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;
    int ret;

    ret = scale_frame(link, &in, &out);
    if (out)
        return ff_filter_frame(outlink, out);

    return ret;
}

static int filter_frame_ref(AVFilterLink *link, AVFrame *in)
{
    FilterLink *l = ff_filter_link(link);
    ScaleContext *scale = link->dst->priv;
    AVFilterLink *outlink = link->dst->outputs[1];
    int frame_changed;

    frame_changed = in->width  != link->w ||
                    in->height != link->h ||
                    in->format != link->format ||
                    in->sample_aspect_ratio.den != link->sample_aspect_ratio.den ||
                    in->sample_aspect_ratio.num != link->sample_aspect_ratio.num ||
                    in->colorspace != link->colorspace ||
                    in->color_range != link->color_range;

    if (frame_changed) {
        link->format = in->format;
        link->w = in->width;
        link->h = in->height;
        link->sample_aspect_ratio.num = in->sample_aspect_ratio.num;
        link->sample_aspect_ratio.den = in->sample_aspect_ratio.den;
        link->colorspace = in->colorspace;
        link->color_range = in->color_range;

        config_props_ref(outlink);
    }

    if (scale->eval_mode == EVAL_MODE_FRAME) {
        scale->var_values[VAR_N] = l->frame_count_out;
        scale->var_values[VAR_T] = TS2T(in->pts, link->time_base);
#if FF_API_FRAME_PKT
FF_DISABLE_DEPRECATION_WARNINGS
        scale->var_values[VAR_POS] = in->pkt_pos == -1 ? NAN : in->pkt_pos;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
    }

    return ff_filter_frame(outlink, in);
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    ScaleContext *scale = ctx->priv;
    char *str_expr;
    AVExpr **pexpr_ptr;
    int ret, w, h;

    w = !strcmp(cmd, "width")  || !strcmp(cmd, "w");
    h = !strcmp(cmd, "height")  || !strcmp(cmd, "h");

    if (w || h) {
        str_expr = w ? scale->w_expr : scale->h_expr;
        pexpr_ptr = w ? &scale->w_pexpr : &scale->h_pexpr;

        ret = scale_parse_expr(ctx, str_expr, pexpr_ptr, cmd, args);
    } else
        ret = AVERROR(ENOSYS);

    if (ret < 0)
        av_log(ctx, AV_LOG_ERROR, "Failed to process command. Continuing with existing parameters.\n");

    return ret;
}

static int activate(AVFilterContext *ctx)
{
    ScaleContext *scale = ctx->priv;
    return ff_framesync_activate(&scale->fs);
}

static const AVClass *child_class_iterate(void **iter)
{
    switch ((uintptr_t) *iter) {
    case 0:
        *iter = (void*)(uintptr_t) 1;
        return sws_get_class();
    case 1:
        *iter = (void*)(uintptr_t) 2;
        return &ff_framesync_class;
    }

    return NULL;
}

static void *child_next(void *obj, void *prev)
{
    ScaleContext *s = obj;
    if (!prev)
        return s->sws_opts;
    if (prev == s->sws_opts)
        return &s->fs;
    return NULL;
}

#define OFFSET(x) offsetof(ScaleContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
#define TFLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption scale_options[] = {
    { "w",     "Output video width",          OFFSET(w_expr),    AV_OPT_TYPE_STRING,        .flags = TFLAGS },
    { "width", "Output video width",          OFFSET(w_expr),    AV_OPT_TYPE_STRING,        .flags = TFLAGS },
    { "h",     "Output video height",         OFFSET(h_expr),    AV_OPT_TYPE_STRING,        .flags = TFLAGS },
    { "height","Output video height",         OFFSET(h_expr),    AV_OPT_TYPE_STRING,        .flags = TFLAGS },
    { "flags", "Flags to pass to libswscale", OFFSET(flags_str), AV_OPT_TYPE_STRING, { .str = "" }, .flags = FLAGS },
    { "interl", "set interlacing", OFFSET(interlaced), AV_OPT_TYPE_BOOL, {.i64 = 0 }, -1, 1, FLAGS },
    { "size",   "set video size",          OFFSET(size_str), AV_OPT_TYPE_STRING, {.str = NULL}, 0, .flags = FLAGS },
    { "s",      "set video size",          OFFSET(size_str), AV_OPT_TYPE_STRING, {.str = NULL}, 0, .flags = FLAGS },
    {  "in_color_matrix", "set input YCbCr type",   OFFSET(in_color_matrix),  AV_OPT_TYPE_INT, { .i64 = -1 }, -1, AVCOL_SPC_NB-1, .flags = FLAGS, .unit = "color" },
    { "out_color_matrix", "set output YCbCr type",  OFFSET(out_color_matrix), AV_OPT_TYPE_INT, { .i64 = AVCOL_SPC_UNSPECIFIED }, 0, AVCOL_SPC_NB-1, .flags = FLAGS, .unit = "color"},
        { "auto",        NULL, 0, AV_OPT_TYPE_CONST, { .i64 = -1 },                     0, 0, FLAGS, .unit = "color" },
        { "bt601",       NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_SPC_BT470BG },      0, 0, FLAGS, .unit = "color" },
        { "bt470",       NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_SPC_BT470BG },      0, 0, FLAGS, .unit = "color" },
        { "smpte170m",   NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_SPC_BT470BG },      0, 0, FLAGS, .unit = "color" },
        { "bt709",       NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_SPC_BT709 },        0, 0, FLAGS, .unit = "color" },
        { "fcc",         NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_SPC_FCC },          0, 0, FLAGS, .unit = "color" },
        { "smpte240m",   NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_SPC_SMPTE240M },    0, 0, FLAGS, .unit = "color" },
        { "bt2020",      NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_SPC_BT2020_NCL },   0, 0, FLAGS, .unit = "color" },
    {  "in_range", "set input color range",  OFFSET( in_range), AV_OPT_TYPE_INT, {.i64 = AVCOL_RANGE_UNSPECIFIED }, 0, 2, FLAGS, .unit = "range" },
    { "out_range", "set output color range", OFFSET(out_range), AV_OPT_TYPE_INT, {.i64 = AVCOL_RANGE_UNSPECIFIED }, 0, 2, FLAGS, .unit = "range" },
    { "auto",   NULL, 0, AV_OPT_TYPE_CONST, {.i64 = AVCOL_RANGE_UNSPECIFIED }, 0, 0, FLAGS, .unit = "range" },
    { "unknown", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = AVCOL_RANGE_UNSPECIFIED }, 0, 0, FLAGS, .unit = "range" },
    { "full",   NULL, 0, AV_OPT_TYPE_CONST, {.i64 = AVCOL_RANGE_JPEG}, 0, 0, FLAGS, .unit = "range" },
    { "limited",NULL, 0, AV_OPT_TYPE_CONST, {.i64 = AVCOL_RANGE_MPEG}, 0, 0, FLAGS, .unit = "range" },
    { "jpeg",   NULL, 0, AV_OPT_TYPE_CONST, {.i64 = AVCOL_RANGE_JPEG}, 0, 0, FLAGS, .unit = "range" },
    { "mpeg",   NULL, 0, AV_OPT_TYPE_CONST, {.i64 = AVCOL_RANGE_MPEG}, 0, 0, FLAGS, .unit = "range" },
    { "tv",     NULL, 0, AV_OPT_TYPE_CONST, {.i64 = AVCOL_RANGE_MPEG}, 0, 0, FLAGS, .unit = "range" },
    { "pc",     NULL, 0, AV_OPT_TYPE_CONST, {.i64 = AVCOL_RANGE_JPEG}, 0, 0, FLAGS, .unit = "range" },
    { "in_chroma_loc",  "set input chroma sample location",  OFFSET(in_chroma_loc),  AV_OPT_TYPE_INT, { .i64 = AVCHROMA_LOC_UNSPECIFIED }, 0, AVCHROMA_LOC_NB-1, .flags = FLAGS, .unit = "chroma_loc" },
    { "out_chroma_loc", "set output chroma sample location", OFFSET(out_chroma_loc), AV_OPT_TYPE_INT, { .i64 = AVCHROMA_LOC_UNSPECIFIED }, 0, AVCHROMA_LOC_NB-1, .flags = FLAGS, .unit = "chroma_loc" },
        {"auto",          NULL, 0, AV_OPT_TYPE_CONST, {.i64=AVCHROMA_LOC_UNSPECIFIED}, 0, 0, FLAGS, .unit = "chroma_loc"},
        {"unknown",       NULL, 0, AV_OPT_TYPE_CONST, {.i64=AVCHROMA_LOC_UNSPECIFIED}, 0, 0, FLAGS, .unit = "chroma_loc"},
        {"left",          NULL, 0, AV_OPT_TYPE_CONST, {.i64=AVCHROMA_LOC_LEFT},        0, 0, FLAGS, .unit = "chroma_loc"},
        {"center",        NULL, 0, AV_OPT_TYPE_CONST, {.i64=AVCHROMA_LOC_CENTER},      0, 0, FLAGS, .unit = "chroma_loc"},
        {"topleft",       NULL, 0, AV_OPT_TYPE_CONST, {.i64=AVCHROMA_LOC_TOPLEFT},     0, 0, FLAGS, .unit = "chroma_loc"},
        {"top",           NULL, 0, AV_OPT_TYPE_CONST, {.i64=AVCHROMA_LOC_TOP},         0, 0, FLAGS, .unit = "chroma_loc"},
        {"bottomleft",    NULL, 0, AV_OPT_TYPE_CONST, {.i64=AVCHROMA_LOC_BOTTOMLEFT},  0, 0, FLAGS, .unit = "chroma_loc"},
        {"bottom",        NULL, 0, AV_OPT_TYPE_CONST, {.i64=AVCHROMA_LOC_BOTTOM},      0, 0, FLAGS, .unit = "chroma_loc"},
    { "in_v_chr_pos",   "input vertical chroma position in luma grid/256"  ,   OFFSET(in_v_chr_pos),  AV_OPT_TYPE_INT, { .i64 = -513}, -513, 512, FLAGS },
    { "in_h_chr_pos",   "input horizontal chroma position in luma grid/256",   OFFSET(in_h_chr_pos),  AV_OPT_TYPE_INT, { .i64 = -513}, -513, 512, FLAGS },
    { "out_v_chr_pos",   "output vertical chroma position in luma grid/256"  , OFFSET(out_v_chr_pos), AV_OPT_TYPE_INT, { .i64 = -513}, -513, 512, FLAGS },
    { "out_h_chr_pos",   "output horizontal chroma position in luma grid/256", OFFSET(out_h_chr_pos), AV_OPT_TYPE_INT, { .i64 = -513}, -513, 512, FLAGS },
    { "force_original_aspect_ratio", "decrease or increase w/h if necessary to keep the original AR", OFFSET(force_original_aspect_ratio), AV_OPT_TYPE_INT, { .i64 = 0}, 0, 2, FLAGS, .unit = "force_oar" },
    { "disable",  NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 0 }, 0, 0, FLAGS, .unit = "force_oar" },
    { "decrease", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 1 }, 0, 0, FLAGS, .unit = "force_oar" },
    { "increase", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 2 }, 0, 0, FLAGS, .unit = "force_oar" },
    { "force_divisible_by", "enforce that the output resolution is divisible by a defined integer when force_original_aspect_ratio is used", OFFSET(force_divisible_by), AV_OPT_TYPE_INT, { .i64 = 1}, 1, 256, FLAGS },
    { "param0", "Scaler param 0",             OFFSET(param[0]),  AV_OPT_TYPE_DOUBLE, { .dbl = DBL_MAX  }, -DBL_MAX, DBL_MAX, FLAGS },
    { "param1", "Scaler param 1",             OFFSET(param[1]),  AV_OPT_TYPE_DOUBLE, { .dbl = DBL_MAX  }, -DBL_MAX, DBL_MAX, FLAGS },
    { "eval", "specify when to evaluate expressions", OFFSET(eval_mode), AV_OPT_TYPE_INT, {.i64 = EVAL_MODE_INIT}, 0, EVAL_MODE_NB-1, FLAGS, .unit = "eval" },
         { "init",  "eval expressions once during initialization", 0, AV_OPT_TYPE_CONST, {.i64=EVAL_MODE_INIT},  .flags = FLAGS, .unit = "eval" },
         { "frame", "eval expressions during initialization and per-frame", 0, AV_OPT_TYPE_CONST, {.i64=EVAL_MODE_FRAME}, .flags = FLAGS, .unit = "eval" },
    { NULL }
};

static const AVClass scale_class = {
    .class_name       = "scale",
    .item_name        = av_default_item_name,
    .option           = scale_options,
    .version          = LIBAVUTIL_VERSION_INT,
    .category         = AV_CLASS_CATEGORY_FILTER,
    .child_class_iterate = child_class_iterate,
    .child_next          = child_next,
};

static const AVFilterPad avfilter_vf_scale_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
    },
};

static const AVFilterPad avfilter_vf_scale_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_props,
    },
};

const AVFilter ff_vf_scale = {
    .name            = "scale",
    .description     = NULL_IF_CONFIG_SMALL("Scale the input video size and/or convert the image format."),
    .preinit         = preinit,
    .init            = init,
    .uninit          = uninit,
    .priv_size       = sizeof(ScaleContext),
    .priv_class      = &scale_class,
    FILTER_INPUTS(avfilter_vf_scale_inputs),
    FILTER_OUTPUTS(avfilter_vf_scale_outputs),
    FILTER_QUERY_FUNC(query_formats),
    .activate        = activate,
    .process_command = process_command,
    .flags           = AVFILTER_FLAG_DYNAMIC_INPUTS,
};

static const AVClass *scale2ref_child_class_iterate(void **iter)
{
    const AVClass *c = *iter ? NULL : sws_get_class();
    *iter = (void*)(uintptr_t)c;
    return c;
}

static void *scale2ref_child_next(void *obj, void *prev)
{
    ScaleContext *s = obj;
    if (!prev)
        return s->sws_opts;
    return NULL;
}

static const AVClass scale2ref_class = {
    .class_name       = "scale(2ref)",
    .item_name        = av_default_item_name,
    .option           = scale_options,
    .version          = LIBAVUTIL_VERSION_INT,
    .category         = AV_CLASS_CATEGORY_FILTER,
    .child_class_iterate = scale2ref_child_class_iterate,
    .child_next          = scale2ref_child_next,
};

static const AVFilterPad avfilter_vf_scale2ref_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
    {
        .name         = "ref",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame_ref,
    },
};

static const AVFilterPad avfilter_vf_scale2ref_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_props,
        .request_frame= request_frame,
    },
    {
        .name         = "ref",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_props_ref,
        .request_frame= request_frame_ref,
    },
};

const AVFilter ff_vf_scale2ref = {
    .name            = "scale2ref",
    .description     = NULL_IF_CONFIG_SMALL("Scale the input video size and/or convert the image format to the given reference."),
    .preinit         = preinit,
    .init            = init,
    .uninit          = uninit,
    .priv_size       = sizeof(ScaleContext),
    .priv_class      = &scale2ref_class,
    FILTER_INPUTS(avfilter_vf_scale2ref_inputs),
    FILTER_OUTPUTS(avfilter_vf_scale2ref_outputs),
    FILTER_QUERY_FUNC(query_formats),
    .process_command = process_command,
};
