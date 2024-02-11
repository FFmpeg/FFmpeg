/*
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

#include <nppi.h>
#include <stdio.h>
#include <string.h>

#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_cuda_internal.h"
#include "libavutil/cuda_check.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/eval.h"
#include "libavutil/pixdesc.h"

#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "scale_eval.h"
#include "video.h"

#define CHECK_CU(x) FF_CUDA_CHECK_DL(ctx, device_hwctx->internal->cuda_dl, x)

static const enum AVPixelFormat supported_formats[] = {
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUVA420P,
    AV_PIX_FMT_NV12,
    AV_PIX_FMT_YUV444P,
};

static const enum AVPixelFormat deinterleaved_formats[][2] = {
    { AV_PIX_FMT_NV12, AV_PIX_FMT_YUV420P },
};

enum ScaleStage {
    STAGE_DEINTERLEAVE,
    STAGE_RESIZE,
    STAGE_INTERLEAVE,
    STAGE_NB,
};

typedef struct NPPScaleStageContext {
    int stage_needed;
    enum AVPixelFormat in_fmt;
    enum AVPixelFormat out_fmt;

    struct {
        int width;
        int height;
    } planes_in[4], planes_out[4];

    AVBufferRef *frames_ctx;
    AVFrame     *frame;
} NPPScaleStageContext;

static const char *const var_names[] = {
    "in_w",   "iw",
    "in_h",   "ih",
    "out_w",  "ow",
    "out_h",  "oh",
    "a",
    "sar",
    "dar",
    "n",
    "t",
#if FF_API_FRAME_PKT
    "pos",
#endif
    "main_w",
    "main_h",
    "main_a",
    "main_sar",
    "main_dar", "mdar",
    "main_n",
    "main_t",
#if FF_API_FRAME_PKT
    "main_pos",
#endif
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
    VAR_N,
    VAR_T,
#if FF_API_FRAME_PKT
    VAR_POS,
#endif
    VAR_S2R_MAIN_W,
    VAR_S2R_MAIN_H,
    VAR_S2R_MAIN_A,
    VAR_S2R_MAIN_SAR,
    VAR_S2R_MAIN_DAR, VAR_S2R_MDAR,
    VAR_S2R_MAIN_N,
    VAR_S2R_MAIN_T,
#if FF_API_FRAME_PKT
    VAR_S2R_MAIN_POS,
#endif
    VARS_NB
};

enum EvalMode {
    EVAL_MODE_INIT,
    EVAL_MODE_FRAME,
    EVAL_MODE_NB
};

typedef struct NPPScaleContext {
    const AVClass *class;

    NPPScaleStageContext stages[STAGE_NB];
    AVFrame *tmp_frame;
    int passthrough;

    int shift_width, shift_height;

    /**
     * New dimensions. Special values are:
     *   0 = original width/height
     *  -1 = keep original aspect
     */
    int w, h;

    /**
     * Output sw format. AV_PIX_FMT_NONE for no conversion.
     */
    enum AVPixelFormat format;

    char *w_expr;               ///< width  expression string
    char *h_expr;               ///< height expression string
    char *format_str;

    int force_original_aspect_ratio;
    int force_divisible_by;

    int interp_algo;

    char* size_str;

    AVExpr* w_pexpr;
    AVExpr* h_pexpr;

    double var_values[VARS_NB];

    int eval_mode;
} NPPScaleContext;

const AVFilter ff_vf_scale2ref_npp;

static int config_props(AVFilterLink *outlink);

static int check_exprs(AVFilterContext* ctx)
{
    NPPScaleContext* scale = ctx->priv;
    unsigned vars_w[VARS_NB] = {0}, vars_h[VARS_NB] = {0};

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

    if (ctx->filter != &ff_vf_scale2ref_npp &&
        (vars_w[VAR_S2R_MAIN_W]   || vars_h[VAR_S2R_MAIN_W]   ||
         vars_w[VAR_S2R_MAIN_H]   || vars_h[VAR_S2R_MAIN_H]   ||
         vars_w[VAR_S2R_MAIN_A]   || vars_h[VAR_S2R_MAIN_A]   ||
         vars_w[VAR_S2R_MAIN_SAR] || vars_h[VAR_S2R_MAIN_SAR] ||
         vars_w[VAR_S2R_MAIN_DAR] || vars_h[VAR_S2R_MAIN_DAR] ||
         vars_w[VAR_S2R_MDAR]     || vars_h[VAR_S2R_MDAR]     ||
         vars_w[VAR_S2R_MAIN_N]   || vars_h[VAR_S2R_MAIN_N]   ||
         vars_w[VAR_S2R_MAIN_T]   || vars_h[VAR_S2R_MAIN_T]
#if FF_API_FRAME_PKT
         || vars_w[VAR_S2R_MAIN_POS] || vars_h[VAR_S2R_MAIN_POS]
#endif
         )) {
        av_log(ctx, AV_LOG_ERROR, "Expressions with scale2ref_npp variables are not valid in scale_npp filter.\n");
        return AVERROR(EINVAL);
    }

    if (scale->eval_mode == EVAL_MODE_INIT &&
        (vars_w[VAR_N]            || vars_h[VAR_N]           ||
         vars_w[VAR_T]            || vars_h[VAR_T]           ||
#if FF_API_FRAME_PKT
         vars_w[VAR_POS]          || vars_h[VAR_POS]         ||
#endif
         vars_w[VAR_S2R_MAIN_N]   || vars_h[VAR_S2R_MAIN_N]  ||
         vars_w[VAR_S2R_MAIN_T]   || vars_h[VAR_S2R_MAIN_T]
#if FF_API_FRAME_PKT
         || vars_w[VAR_S2R_MAIN_POS] || vars_h[VAR_S2R_MAIN_POS]
#endif
         ) ) {
        av_log(ctx, AV_LOG_ERROR, "Expressions with frame variables 'n', 't', are not valid in init eval_mode.\n");
        return AVERROR(EINVAL);
    }

    return 0;
}

static int nppscale_parse_expr(AVFilterContext* ctx, char* str_expr,
                               AVExpr** pexpr_ptr, const char* var,
                               const char* args)
{
    NPPScaleContext* scale = ctx->priv;
    int ret, is_inited = 0;
    char* old_str_expr = NULL;
    AVExpr* old_pexpr = NULL;

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

    ret = av_expr_parse(pexpr_ptr, args, var_names, NULL, NULL, NULL, NULL, 0,
                        ctx);
    if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Cannot parse expression for %s: '%s'\n", var,
               args);
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

static av_cold int nppscale_init(AVFilterContext* ctx)
{
    NPPScaleContext* scale = ctx->priv;
    int i, ret;

    if (!strcmp(scale->format_str, "same")) {
        scale->format = AV_PIX_FMT_NONE;
    } else {
        scale->format = av_get_pix_fmt(scale->format_str);
        if (scale->format == AV_PIX_FMT_NONE) {
            av_log(ctx, AV_LOG_ERROR, "Unrecognized pixel format: %s\n", scale->format_str);
            return AVERROR(EINVAL);
        }
    }

    if (scale->size_str && (scale->w_expr || scale->h_expr)) {
        av_log(ctx, AV_LOG_ERROR,
               "Size and width/height exprs cannot be set at the same time.\n");
        return AVERROR(EINVAL);
    }

    if (scale->w_expr && !scale->h_expr)
        FFSWAP(char*, scale->w_expr, scale->size_str);

    if (scale->size_str) {
        char buf[32];
        ret = av_parse_video_size(&scale->w, &scale->h, scale->size_str);
        if (0 > ret) {
            av_log(ctx, AV_LOG_ERROR, "Invalid size '%s'\n", scale->size_str);
            return ret;
        }

        snprintf(buf, sizeof(buf) - 1, "%d", scale->w);
        ret = av_opt_set(scale, "w", buf, 0);
        if (ret < 0)
            return ret;

        snprintf(buf, sizeof(buf) - 1, "%d", scale->h);
        ret = av_opt_set(scale, "h", buf, 0);
        if (ret < 0)
            return ret;
    }

    if (!scale->w_expr) {
        ret = av_opt_set(scale, "w", "iw", 0);
        if (ret < 0)
            return ret;
    }

    if (!scale->h_expr) {
        ret = av_opt_set(scale, "h", "ih", 0);
        if (ret < 0)
            return ret;
    }

    ret = nppscale_parse_expr(ctx, NULL, &scale->w_pexpr, "width", scale->w_expr);
    if (ret < 0)
        return ret;

    ret = nppscale_parse_expr(ctx, NULL, &scale->h_pexpr, "height", scale->h_expr);
    if (ret < 0)
        return ret;

    for (i = 0; i < FF_ARRAY_ELEMS(scale->stages); i++) {
        scale->stages[i].frame = av_frame_alloc();
        if (!scale->stages[i].frame)
            return AVERROR(ENOMEM);
    }
    scale->tmp_frame = av_frame_alloc();
    if (!scale->tmp_frame)
        return AVERROR(ENOMEM);

    return 0;
}

static int nppscale_eval_dimensions(AVFilterContext* ctx)
{
    NPPScaleContext* scale = ctx->priv;
    const char scale2ref = ctx->filter == &ff_vf_scale2ref_npp;
    const AVFilterLink* inlink = ctx->inputs[scale2ref ? 1 : 0];
    char* expr;
    int eval_w, eval_h;
    int ret;
    double res;

    scale->var_values[VAR_IN_W]  = scale->var_values[VAR_IW] = inlink->w;
    scale->var_values[VAR_IN_H]  = scale->var_values[VAR_IH] = inlink->h;
    scale->var_values[VAR_OUT_W] = scale->var_values[VAR_OW] = NAN;
    scale->var_values[VAR_OUT_H] = scale->var_values[VAR_OH] = NAN;
    scale->var_values[VAR_A]     = (double)inlink->w / inlink->h;
    scale->var_values[VAR_SAR]   = inlink->sample_aspect_ratio.num ?
        (double)inlink->sample_aspect_ratio.num / inlink->sample_aspect_ratio.den : 1;
    scale->var_values[VAR_DAR]   = scale->var_values[VAR_A] * scale->var_values[VAR_SAR];

    if (scale2ref) {
        const AVFilterLink* main_link = ctx->inputs[0];

        scale->var_values[VAR_S2R_MAIN_W] = main_link->w;
        scale->var_values[VAR_S2R_MAIN_H] = main_link->h;
        scale->var_values[VAR_S2R_MAIN_A] = (double)main_link->w / main_link->h;
        scale->var_values[VAR_S2R_MAIN_SAR] = main_link->sample_aspect_ratio.num ?
            (double)main_link->sample_aspect_ratio.num / main_link->sample_aspect_ratio.den : 1;
        scale->var_values[VAR_S2R_MAIN_DAR] = scale->var_values[VAR_S2R_MDAR] =
            scale->var_values[VAR_S2R_MAIN_A] * scale->var_values[VAR_S2R_MAIN_SAR];
    }

    res = av_expr_eval(scale->w_pexpr, scale->var_values, NULL);
    eval_w = scale->var_values[VAR_OUT_W] = scale->var_values[VAR_OW] = (int)res == 0 ? inlink->w : (int)res;

    res = av_expr_eval(scale->h_pexpr, scale->var_values, NULL);
    if (isnan(res)) {
        expr = scale->h_expr;
        ret = AVERROR(EINVAL);
        goto fail;
    }
    eval_h = scale->var_values[VAR_OUT_H] = scale->var_values[VAR_OH] = (int)res == 0 ? inlink->h : (int)res;

    res = av_expr_eval(scale->w_pexpr, scale->var_values, NULL);
    if (isnan(res)) {
        expr = scale->w_expr;
        ret = AVERROR(EINVAL);
        goto fail;
    }
    eval_w = scale->var_values[VAR_OUT_W] = scale->var_values[VAR_OW] = (int)res == 0 ? inlink->w : (int)res;

    scale->w = eval_w;
    scale->h = eval_h;

    return 0;

fail:
    av_log(ctx, AV_LOG_ERROR, "Error when evaluating the expression '%s'.\n",
           expr);
    return ret;
}

static void nppscale_uninit(AVFilterContext *ctx)
{
    NPPScaleContext *s = ctx->priv;
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(s->stages); i++) {
        av_frame_free(&s->stages[i].frame);
        av_buffer_unref(&s->stages[i].frames_ctx);
    }
    av_frame_free(&s->tmp_frame);

    av_expr_free(s->w_pexpr);
    av_expr_free(s->h_pexpr);
    s->w_pexpr = s->h_pexpr = NULL;
}

static int init_stage(NPPScaleStageContext *stage, AVBufferRef *device_ctx)
{
    AVBufferRef *out_ref = NULL;
    AVHWFramesContext *out_ctx;
    int in_sw, in_sh, out_sw, out_sh;
    int ret, i;

    av_pix_fmt_get_chroma_sub_sample(stage->in_fmt,  &in_sw,  &in_sh);
    av_pix_fmt_get_chroma_sub_sample(stage->out_fmt, &out_sw, &out_sh);
    if (!stage->planes_out[0].width) {
        stage->planes_out[0].width  = stage->planes_in[0].width;
        stage->planes_out[0].height = stage->planes_in[0].height;
    }

    for (i = 1; i < FF_ARRAY_ELEMS(stage->planes_in); i++) {
        stage->planes_in[i].width   = stage->planes_in[0].width   >> in_sw;
        stage->planes_in[i].height  = stage->planes_in[0].height  >> in_sh;
        stage->planes_out[i].width  = stage->planes_out[0].width  >> out_sw;
        stage->planes_out[i].height = stage->planes_out[0].height >> out_sh;
    }

    if (AV_PIX_FMT_YUVA420P == stage->in_fmt) {
        stage->planes_in[3].width = stage->planes_in[0].width;
        stage->planes_in[3].height = stage->planes_in[0].height;
        stage->planes_out[3].width = stage->planes_out[0].width;
        stage->planes_out[3].height = stage->planes_out[0].height;
    }

    out_ref = av_hwframe_ctx_alloc(device_ctx);
    if (!out_ref)
        return AVERROR(ENOMEM);
    out_ctx = (AVHWFramesContext*)out_ref->data;

    out_ctx->format    = AV_PIX_FMT_CUDA;
    out_ctx->sw_format = stage->out_fmt;
    out_ctx->width     = FFALIGN(stage->planes_out[0].width,  32);
    out_ctx->height    = FFALIGN(stage->planes_out[0].height, 32);

    ret = av_hwframe_ctx_init(out_ref);
    if (ret < 0)
        goto fail;

    av_frame_unref(stage->frame);
    ret = av_hwframe_get_buffer(out_ref, stage->frame, 0);
    if (ret < 0)
        goto fail;

    stage->frame->width  = stage->planes_out[0].width;
    stage->frame->height = stage->planes_out[0].height;

    av_buffer_unref(&stage->frames_ctx);
    stage->frames_ctx = out_ref;

    return 0;
fail:
    av_buffer_unref(&out_ref);
    return ret;
}

static int format_is_supported(enum AVPixelFormat fmt)
{
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(supported_formats); i++)
        if (supported_formats[i] == fmt)
            return 1;
    return 0;
}

static enum AVPixelFormat get_deinterleaved_format(enum AVPixelFormat fmt)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(fmt);
    int i, planes;

    planes = av_pix_fmt_count_planes(fmt);
    if (planes == desc->nb_components)
        return fmt;
    for (i = 0; i < FF_ARRAY_ELEMS(deinterleaved_formats); i++)
        if (deinterleaved_formats[i][0] == fmt)
            return deinterleaved_formats[i][1];
    return AV_PIX_FMT_NONE;
}

static int init_processing_chain(AVFilterContext *ctx, int in_width, int in_height,
                                 int out_width, int out_height)
{
    NPPScaleContext *s = ctx->priv;

    AVHWFramesContext *in_frames_ctx;

    enum AVPixelFormat in_format;
    enum AVPixelFormat out_format;
    enum AVPixelFormat in_deinterleaved_format;
    enum AVPixelFormat out_deinterleaved_format;

    int i, ret, last_stage = -1;

    /* check that we have a hw context */
    if (!ctx->inputs[0]->hw_frames_ctx) {
        av_log(ctx, AV_LOG_ERROR, "No hw context provided on input\n");
        return AVERROR(EINVAL);
    }
    in_frames_ctx = (AVHWFramesContext*)ctx->inputs[0]->hw_frames_ctx->data;
    in_format     = in_frames_ctx->sw_format;
    out_format    = (s->format == AV_PIX_FMT_NONE) ? in_format : s->format;

    if (!format_is_supported(in_format)) {
        av_log(ctx, AV_LOG_ERROR, "Unsupported input format: %s\n",
               av_get_pix_fmt_name(in_format));
        return AVERROR(ENOSYS);
    }
    if (!format_is_supported(out_format)) {
        av_log(ctx, AV_LOG_ERROR, "Unsupported output format: %s\n",
               av_get_pix_fmt_name(out_format));
        return AVERROR(ENOSYS);
    }

    in_deinterleaved_format  = get_deinterleaved_format(in_format);
    out_deinterleaved_format = get_deinterleaved_format(out_format);
    if (in_deinterleaved_format  == AV_PIX_FMT_NONE ||
        out_deinterleaved_format == AV_PIX_FMT_NONE)
        return AVERROR_BUG;

    /* figure out which stages need to be done */
    if (in_width != out_width || in_height != out_height ||
        in_deinterleaved_format != out_deinterleaved_format) {
        s->stages[STAGE_RESIZE].stage_needed = 1;

        if (s->interp_algo == NPPI_INTER_SUPER &&
            (out_width > in_width && out_height > in_height)) {
            s->interp_algo = NPPI_INTER_LANCZOS;
            av_log(ctx, AV_LOG_WARNING, "super-sampling not supported for output dimensions, using lanczos instead.\n");
        }
        if (s->interp_algo == NPPI_INTER_SUPER &&
            !(out_width < in_width && out_height < in_height)) {
            s->interp_algo = NPPI_INTER_CUBIC;
            av_log(ctx, AV_LOG_WARNING, "super-sampling not supported for output dimensions, using cubic instead.\n");
        }
    }

    if (!s->stages[STAGE_RESIZE].stage_needed && in_format == out_format)
        s->passthrough = 1;

    if (!s->passthrough) {
        if (in_format != in_deinterleaved_format)
            s->stages[STAGE_DEINTERLEAVE].stage_needed = 1;
        if (out_format != out_deinterleaved_format)
            s->stages[STAGE_INTERLEAVE].stage_needed = 1;
    }

    s->stages[STAGE_DEINTERLEAVE].in_fmt              = in_format;
    s->stages[STAGE_DEINTERLEAVE].out_fmt             = in_deinterleaved_format;
    s->stages[STAGE_DEINTERLEAVE].planes_in[0].width  = in_width;
    s->stages[STAGE_DEINTERLEAVE].planes_in[0].height = in_height;

    s->stages[STAGE_RESIZE].in_fmt               = in_deinterleaved_format;
    s->stages[STAGE_RESIZE].out_fmt              = out_deinterleaved_format;
    s->stages[STAGE_RESIZE].planes_in[0].width   = in_width;
    s->stages[STAGE_RESIZE].planes_in[0].height  = in_height;
    s->stages[STAGE_RESIZE].planes_out[0].width  = out_width;
    s->stages[STAGE_RESIZE].planes_out[0].height = out_height;

    s->stages[STAGE_INTERLEAVE].in_fmt              = out_deinterleaved_format;
    s->stages[STAGE_INTERLEAVE].out_fmt             = out_format;
    s->stages[STAGE_INTERLEAVE].planes_in[0].width  = out_width;
    s->stages[STAGE_INTERLEAVE].planes_in[0].height = out_height;

    /* init the hardware contexts */
    for (i = 0; i < FF_ARRAY_ELEMS(s->stages); i++) {
        if (!s->stages[i].stage_needed)
            continue;

        ret = init_stage(&s->stages[i], in_frames_ctx->device_ref);
        if (ret < 0)
            return ret;

        last_stage = i;
    }

    if (last_stage >= 0)
        ctx->outputs[0]->hw_frames_ctx = av_buffer_ref(s->stages[last_stage].frames_ctx);
    else
        ctx->outputs[0]->hw_frames_ctx = av_buffer_ref(ctx->inputs[0]->hw_frames_ctx);

    if (!ctx->outputs[0]->hw_frames_ctx)
        return AVERROR(ENOMEM);

    return 0;
}

static int config_props(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink0 = outlink->src->inputs[0];
    AVFilterLink *inlink  = ctx->filter == &ff_vf_scale2ref_npp ?
                            outlink->src->inputs[1] :
                            outlink->src->inputs[0];
    NPPScaleContext *s = ctx->priv;
    int ret;

    if ((ret = nppscale_eval_dimensions(ctx)) < 0)
        goto fail;

    ff_scale_adjust_dimensions(inlink, &s->w, &s->h,
                               s->force_original_aspect_ratio,
                               s->force_divisible_by);

    if (s->w > INT_MAX || s->h > INT_MAX ||
        (s->h * inlink->w) > INT_MAX ||
        (s->w * inlink->h) > INT_MAX)
        av_log(ctx, AV_LOG_ERROR, "Rescaled value for width or height is too big.\n");

    outlink->w = s->w;
    outlink->h = s->h;

    ret = init_processing_chain(ctx, inlink0->w, inlink0->h, outlink->w, outlink->h);
    if (ret < 0)
        return ret;

    av_log(ctx, AV_LOG_VERBOSE, "w:%d h:%d -> w:%d h:%d\n",
           inlink->w, inlink->h, outlink->w, outlink->h);

    if (inlink->sample_aspect_ratio.num)
        outlink->sample_aspect_ratio = av_mul_q((AVRational){outlink->h*inlink->w,
                                                             outlink->w*inlink->h},
                                                inlink->sample_aspect_ratio);
    else
        outlink->sample_aspect_ratio = inlink->sample_aspect_ratio;

    return 0;

fail:
    return ret;
}

static int config_props_ref(AVFilterLink *outlink)
{
    AVFilterLink *inlink = outlink->src->inputs[1];
    AVFilterContext *ctx = outlink->src;

    outlink->w = inlink->w;
    outlink->h = inlink->h;
    outlink->sample_aspect_ratio = inlink->sample_aspect_ratio;
    outlink->time_base = inlink->time_base;
    outlink->frame_rate = inlink->frame_rate;

    ctx->outputs[1]->hw_frames_ctx = av_buffer_ref(ctx->inputs[1]->hw_frames_ctx);

    return 0;
}

static int nppscale_deinterleave(AVFilterContext *ctx, NPPScaleStageContext *stage,
                                 AVFrame *out, AVFrame *in)
{
    AVHWFramesContext *in_frames_ctx = (AVHWFramesContext*)in->hw_frames_ctx->data;
    NppStatus err;

    switch (in_frames_ctx->sw_format) {
    case AV_PIX_FMT_NV12:
        err = nppiYCbCr420_8u_P2P3R(in->data[0], in->linesize[0],
                                    in->data[1], in->linesize[1],
                                    out->data, out->linesize,
                                    (NppiSize){ in->width, in->height });
        break;
    default:
        return AVERROR_BUG;
    }
    if (err != NPP_SUCCESS) {
        av_log(ctx, AV_LOG_ERROR, "NPP deinterleave error: %d\n", err);
        return AVERROR_UNKNOWN;
    }

    return 0;
}

static int nppscale_resize(AVFilterContext *ctx, NPPScaleStageContext *stage,
                           AVFrame *out, AVFrame *in)
{
    NPPScaleContext *s = ctx->priv;
    NppStatus err;
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(stage->planes_in) && i < FF_ARRAY_ELEMS(in->data) && in->data[i]; i++) {
        int iw = stage->planes_in[i].width;
        int ih = stage->planes_in[i].height;
        int ow = stage->planes_out[i].width;
        int oh = stage->planes_out[i].height;

        err = nppiResizeSqrPixel_8u_C1R(in->data[i], (NppiSize){ iw, ih },
                                        in->linesize[i], (NppiRect){ 0, 0, iw, ih },
                                        out->data[i], out->linesize[i],
                                        (NppiRect){ 0, 0, ow, oh },
                                        (double)ow / iw, (double)oh / ih,
                                        0.0, 0.0, s->interp_algo);
        if (err != NPP_SUCCESS) {
            av_log(ctx, AV_LOG_ERROR, "NPP resize error: %d\n", err);
            return AVERROR_UNKNOWN;
        }
    }

    return 0;
}

static int nppscale_interleave(AVFilterContext *ctx, NPPScaleStageContext *stage,
                               AVFrame *out, AVFrame *in)
{
    AVHWFramesContext *out_frames_ctx = (AVHWFramesContext*)out->hw_frames_ctx->data;
    NppStatus err;

    switch (out_frames_ctx->sw_format) {
    case AV_PIX_FMT_NV12:
        err = nppiYCbCr420_8u_P3P2R((const uint8_t**)in->data,
                                    in->linesize,
                                    out->data[0], out->linesize[0],
                                    out->data[1], out->linesize[1],
                                    (NppiSize){ in->width, in->height });
        break;
    default:
        return AVERROR_BUG;
    }
    if (err != NPP_SUCCESS) {
        av_log(ctx, AV_LOG_ERROR, "NPP deinterleave error: %d\n", err);
        return AVERROR_UNKNOWN;
    }

    return 0;
}

static int (*const nppscale_process[])(AVFilterContext *ctx, NPPScaleStageContext *stage,
                                       AVFrame *out, AVFrame *in) = {
    [STAGE_DEINTERLEAVE] = nppscale_deinterleave,
    [STAGE_RESIZE]       = nppscale_resize,
    [STAGE_INTERLEAVE]   = nppscale_interleave,
};

static int nppscale_scale(AVFilterLink *link, AVFrame *out, AVFrame *in)
{
    AVFilterContext *ctx = link->dst;
    NPPScaleContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *src = in;
    char buf[32];
    int i, ret, last_stage = -1;
    int frame_changed;

    frame_changed = in->width  != link->w ||
                    in->height != link->h ||
                    in->format != link->format ||
                    in->sample_aspect_ratio.den != link->sample_aspect_ratio.den ||
                    in->sample_aspect_ratio.num != link->sample_aspect_ratio.num;

    if (s->eval_mode == EVAL_MODE_FRAME || frame_changed) {
        unsigned vars_w[VARS_NB] = { 0 }, vars_h[VARS_NB] = { 0 };

        av_expr_count_vars(s->w_pexpr, vars_w, VARS_NB);
        av_expr_count_vars(s->h_pexpr, vars_h, VARS_NB);

        if (s->eval_mode == EVAL_MODE_FRAME && !frame_changed && ctx->filter != &ff_vf_scale2ref_npp &&
            !(vars_w[VAR_N] || vars_w[VAR_T]
#if FF_API_FRAME_PKT
              || vars_w[VAR_POS]
#endif
              ) &&
            !(vars_h[VAR_N] || vars_h[VAR_T]
#if FF_API_FRAME_PKT
              || vars_h[VAR_POS]
#endif
              ) && s->w && s->h)
            goto scale;

        if (s->eval_mode == EVAL_MODE_INIT) {
            snprintf(buf, sizeof(buf)-1, "%d", outlink->w);
            av_opt_set(s, "w", buf, 0);
            snprintf(buf, sizeof(buf)-1, "%d", outlink->h);
            av_opt_set(s, "h", buf, 0);

            ret = nppscale_parse_expr(ctx, NULL, &s->w_pexpr, "width", s->w_expr);
            if (ret < 0)
                return ret;

            ret = nppscale_parse_expr(ctx, NULL, &s->h_pexpr, "height", s->h_expr);
            if (ret < 0)
                return ret;
        }

        if (ctx->filter == &ff_vf_scale2ref_npp) {
            s->var_values[VAR_S2R_MAIN_N] = link->frame_count_out;
            s->var_values[VAR_S2R_MAIN_T] = TS2T(in->pts, link->time_base);
#if FF_API_FRAME_PKT
FF_DISABLE_DEPRECATION_WARNINGS
            s->var_values[VAR_S2R_MAIN_POS] = in->pkt_pos == -1 ? NAN : in->pkt_pos;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
        } else {
            s->var_values[VAR_N] = link->frame_count_out;
            s->var_values[VAR_T] = TS2T(in->pts, link->time_base);
#if FF_API_FRAME_PKT
FF_DISABLE_DEPRECATION_WARNINGS
            s->var_values[VAR_POS] = in->pkt_pos == -1 ? NAN : in->pkt_pos;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
        }

        link->format = in->format;
        link->w      = in->width;
        link->h      = in->height;

        link->sample_aspect_ratio.den = in->sample_aspect_ratio.den;
        link->sample_aspect_ratio.num = in->sample_aspect_ratio.num;

        if ((ret = config_props(outlink)) < 0)
            return ret;
    }

scale:
    for (i = 0; i < FF_ARRAY_ELEMS(s->stages); i++) {
        if (!s->stages[i].stage_needed)
            continue;

        ret = nppscale_process[i](ctx, &s->stages[i], s->stages[i].frame, src);
        if (ret < 0)
            return ret;

        src        = s->stages[i].frame;
        last_stage = i;
    }
    if (last_stage < 0)
        return AVERROR_BUG;

    ret = av_hwframe_get_buffer(src->hw_frames_ctx, s->tmp_frame, 0);
    if (ret < 0)
        return ret;

    s->tmp_frame->width  = src->width;
    s->tmp_frame->height = src->height;

    av_frame_move_ref(out, src);
    av_frame_move_ref(src, s->tmp_frame);

    ret = av_frame_copy_props(out, in);
    if (ret < 0)
        return ret;

    return 0;
}

static int nppscale_filter_frame(AVFilterLink *link, AVFrame *in)
{
    AVFilterContext              *ctx = link->dst;
    NPPScaleContext                *s = ctx->priv;
    AVFilterLink             *outlink = ctx->outputs[0];
    AVHWFramesContext     *frames_ctx = (AVHWFramesContext*)outlink->hw_frames_ctx->data;
    AVCUDADeviceContext *device_hwctx = frames_ctx->device_ctx->hwctx;

    AVFrame *out = NULL;
    CUcontext dummy;
    int ret = 0;

    if (s->passthrough)
        return ff_filter_frame(outlink, in);

    out = av_frame_alloc();
    if (!out) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    ret = CHECK_CU(device_hwctx->internal->cuda_dl->cuCtxPushCurrent(device_hwctx->cuda_ctx));
    if (ret < 0)
        goto fail;

    ret = nppscale_scale(link, out, in);

    CHECK_CU(device_hwctx->internal->cuda_dl->cuCtxPopCurrent(&dummy));
    if (ret < 0)
        goto fail;

    av_reduce(&out->sample_aspect_ratio.num, &out->sample_aspect_ratio.den,
              (int64_t)in->sample_aspect_ratio.num * outlink->h * link->w,
              (int64_t)in->sample_aspect_ratio.den * outlink->w * link->h,
              INT_MAX);

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
fail:
    av_frame_free(&in);
    av_frame_free(&out);
    return ret;
}

static int nppscale_filter_frame_ref(AVFilterLink *link, AVFrame *in)
{
    NPPScaleContext *scale = link->dst->priv;
    AVFilterLink *outlink = link->dst->outputs[1];
    int frame_changed;

    frame_changed = in->width  != link->w ||
                    in->height != link->h ||
                    in->format != link->format ||
                    in->sample_aspect_ratio.den != link->sample_aspect_ratio.den ||
                    in->sample_aspect_ratio.num != link->sample_aspect_ratio.num;

    if (frame_changed) {
        link->format = in->format;
        link->w = in->width;
        link->h = in->height;
        link->sample_aspect_ratio.num = in->sample_aspect_ratio.num;
        link->sample_aspect_ratio.den = in->sample_aspect_ratio.den;

        config_props_ref(outlink);
    }

    if (scale->eval_mode == EVAL_MODE_FRAME) {
        scale->var_values[VAR_N] = link->frame_count_out;
        scale->var_values[VAR_T] = TS2T(in->pts, link->time_base);
#if FF_API_FRAME_PKT
FF_DISABLE_DEPRECATION_WARNINGS
        scale->var_values[VAR_POS] = in->pkt_pos == -1 ? NAN : in->pkt_pos;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
    }

    return ff_filter_frame(outlink, in);
}

static int request_frame(AVFilterLink *outlink)
{
    return ff_request_frame(outlink->src->inputs[0]);
}

static int request_frame_ref(AVFilterLink *outlink)
{
    return ff_request_frame(outlink->src->inputs[1]);
}

#define OFFSET(x) offsetof(NPPScaleContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM)
static const AVOption options[] = {
    { "w",      "Output video width",  OFFSET(w_expr),     AV_OPT_TYPE_STRING, .flags = FLAGS },
    { "h",      "Output video height", OFFSET(h_expr),     AV_OPT_TYPE_STRING, .flags = FLAGS },
    { "format", "Output pixel format", OFFSET(format_str), AV_OPT_TYPE_STRING, { .str = "same" }, .flags = FLAGS },
    { "s",      "Output video size",   OFFSET(size_str),   AV_OPT_TYPE_STRING, { .str = NULL   }, .flags = FLAGS },

    { "interp_algo", "Interpolation algorithm used for resizing", OFFSET(interp_algo), AV_OPT_TYPE_INT, { .i64 = NPPI_INTER_CUBIC }, 0, INT_MAX, FLAGS, .unit = "interp_algo" },
        { "nn",                 "nearest neighbour",                 0, AV_OPT_TYPE_CONST, { .i64 = NPPI_INTER_NN                 }, 0, 0, FLAGS, .unit = "interp_algo" },
        { "linear",             "linear",                            0, AV_OPT_TYPE_CONST, { .i64 = NPPI_INTER_LINEAR             }, 0, 0, FLAGS, .unit = "interp_algo" },
        { "cubic",              "cubic",                             0, AV_OPT_TYPE_CONST, { .i64 = NPPI_INTER_CUBIC              }, 0, 0, FLAGS, .unit = "interp_algo" },
        { "cubic2p_bspline",    "2-parameter cubic (B=1, C=0)",      0, AV_OPT_TYPE_CONST, { .i64 = NPPI_INTER_CUBIC2P_BSPLINE    }, 0, 0, FLAGS, .unit = "interp_algo" },
        { "cubic2p_catmullrom", "2-parameter cubic (B=0, C=1/2)",    0, AV_OPT_TYPE_CONST, { .i64 = NPPI_INTER_CUBIC2P_CATMULLROM }, 0, 0, FLAGS, .unit = "interp_algo" },
        { "cubic2p_b05c03",     "2-parameter cubic (B=1/2, C=3/10)", 0, AV_OPT_TYPE_CONST, { .i64 = NPPI_INTER_CUBIC2P_B05C03     }, 0, 0, FLAGS, .unit = "interp_algo" },
        { "super",              "supersampling",                     0, AV_OPT_TYPE_CONST, { .i64 = NPPI_INTER_SUPER              }, 0, 0, FLAGS, .unit = "interp_algo" },
        { "lanczos",            "Lanczos",                           0, AV_OPT_TYPE_CONST, { .i64 = NPPI_INTER_LANCZOS            }, 0, 0, FLAGS, .unit = "interp_algo" },
    { "force_original_aspect_ratio", "decrease or increase w/h if necessary to keep the original AR", OFFSET(force_original_aspect_ratio), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 2, FLAGS, .unit = "force_oar" },
    { "disable",  NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 0 }, 0, 0, FLAGS, .unit = "force_oar" },
    { "decrease", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 1 }, 0, 0, FLAGS, .unit = "force_oar" },
    { "increase", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 2 }, 0, 0, FLAGS, .unit = "force_oar" },
    { "force_divisible_by", "enforce that the output resolution is divisible by a defined integer when force_original_aspect_ratio is used", OFFSET(force_divisible_by), AV_OPT_TYPE_INT, { .i64 = 1 }, 1, 256, FLAGS },
    { "eval", "specify when to evaluate expressions", OFFSET(eval_mode), AV_OPT_TYPE_INT, { .i64 = EVAL_MODE_INIT }, 0, EVAL_MODE_NB-1, FLAGS, .unit = "eval" },
         { "init",  "eval expressions once during initialization",          0, AV_OPT_TYPE_CONST, { .i64 = EVAL_MODE_INIT  }, 0, 0, FLAGS, .unit = "eval" },
         { "frame", "eval expressions during initialization and per-frame", 0, AV_OPT_TYPE_CONST, { .i64 = EVAL_MODE_FRAME }, 0, 0, FLAGS, .unit = "eval" },
    { NULL },
};

static const AVClass nppscale_class = {
    .class_name = "nppscale",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_FILTER,
};

static const AVFilterPad nppscale_inputs[] = {
    {
        .name        = "default",
        .type        = AVMEDIA_TYPE_VIDEO,
        .filter_frame = nppscale_filter_frame,
    }
};

static const AVFilterPad nppscale_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_props,
    }
};

const AVFilter ff_vf_scale_npp = {
    .name      = "scale_npp",
    .description = NULL_IF_CONFIG_SMALL("NVIDIA Performance Primitives video "
                                        "scaling and format conversion"),

    .init          = nppscale_init,
    .uninit        = nppscale_uninit,

    .priv_size = sizeof(NPPScaleContext),
    .priv_class = &nppscale_class,

    FILTER_INPUTS(nppscale_inputs),
    FILTER_OUTPUTS(nppscale_outputs),

    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_CUDA),

    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};

static const AVFilterPad nppscale2ref_inputs[] = {
    {
        .name        = "default",
        .type        = AVMEDIA_TYPE_VIDEO,
        .filter_frame = nppscale_filter_frame,
    },
    {
        .name        = "ref",
        .type        = AVMEDIA_TYPE_VIDEO,
        .filter_frame = nppscale_filter_frame_ref,
    }
};

static const AVFilterPad nppscale2ref_outputs[] = {
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
    }
};

const AVFilter ff_vf_scale2ref_npp = {
    .name          = "scale2ref_npp",
    .description   = NULL_IF_CONFIG_SMALL("NVIDIA Performance Primitives video "
                                          "scaling and format conversion to the "
                                          "given reference."),

    .init          = nppscale_init,
    .uninit        = nppscale_uninit,

    .priv_size = sizeof(NPPScaleContext),
    .priv_class = &nppscale_class,

    FILTER_INPUTS(nppscale2ref_inputs),
    FILTER_OUTPUTS(nppscale2ref_outputs),

    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_CUDA),

    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
