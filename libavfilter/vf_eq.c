/*
 * Original MPlayer filters by Richard Felker, Hampa Hug, Daniel Moreno,
 * and Michael Niedermeyer.
 *
 * Copyright (c) 2014 James Darnley <james.darnley@gmail.com>
 * Copyright (c) 2015 Arwa Arif <arwaarif1994@gmail.com>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/**
 * @file
 * very simple video equalizer
 */

#include "libavfilter/internal.h"
#include "libavutil/common.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "vf_eq.h"

static void create_lut(EQParameters *param)
{
    int i;
    double   g  = 1.0 / param->gamma;
    double   lw = 1.0 - param->gamma_weight;

    for (i = 0; i < 256; i++) {
        double v = i / 255.0;
        v = param->contrast * (v - 0.5) + 0.5 + param->brightness;

        if (v <= 0.0) {
            param->lut[i] = 0;
        } else {
            v = v * lw + pow(v, g) * param->gamma_weight;

            if (v >= 1.0)
                param->lut[i] = 255;
            else
                param->lut[i] = 256.0 * v;
        }
    }

    param->lut_clean = 1;
}

static void apply_lut(EQParameters *param, uint8_t *dst, int dst_stride,
                      const uint8_t *src, int src_stride, int w, int h)
{
    int x, y;

    if (!param->lut_clean)
        create_lut(param);

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            dst[y * dst_stride + x] = param->lut[src[y * src_stride + x]];
        }
    }
}

static void process_c(EQParameters *param, uint8_t *dst, int dst_stride,
                      const uint8_t *src, int src_stride, int w, int h)
{
    int x, y, pel;

    int contrast = (int) (param->contrast * 256 * 16);
    int brightness = ((int) (100.0 * param->brightness + 100.0) * 511) / 200 - 128 - contrast / 32;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            pel = ((src[y * src_stride + x] * contrast) >> 12) + brightness;

            if (pel & ~255)
                pel = (-pel) >> 31;

            dst[y * dst_stride + x] = pel;
        }
    }
}

static void check_values(EQParameters *param, EQContext *eq)
{
    if (param->contrast == 1.0 && param->brightness == 0.0 && param->gamma == 1.0)
        param->adjust = NULL;
    else if (param->gamma == 1.0 && fabs(param->contrast) < 7.9)
        param->adjust = eq->process;
    else
        param->adjust = apply_lut;
}

static void set_contrast(EQContext *eq)
{
    eq->contrast = av_clipf(av_expr_eval(eq->contrast_pexpr, eq->var_values, eq), -1000.0, 1000.0);
    eq->param[0].contrast = eq->contrast;
    eq->param[0].lut_clean = 0;
    check_values(&eq->param[0], eq);
}

static void set_brightness(EQContext *eq)
{
    eq->brightness = av_clipf(av_expr_eval(eq->brightness_pexpr, eq->var_values, eq), -1.0, 1.0);
    eq->param[0].brightness = eq->brightness;
    eq->param[0].lut_clean = 0;
    check_values(&eq->param[0], eq);
}

static void set_gamma(EQContext *eq)
{
    int i;

    eq->gamma        = av_clipf(av_expr_eval(eq->gamma_pexpr,        eq->var_values, eq), 0.1, 10.0);
    eq->gamma_r      = av_clipf(av_expr_eval(eq->gamma_r_pexpr,      eq->var_values, eq), 0.1, 10.0);
    eq->gamma_g      = av_clipf(av_expr_eval(eq->gamma_g_pexpr,      eq->var_values, eq), 0.1, 10.0);
    eq->gamma_b      = av_clipf(av_expr_eval(eq->gamma_b_pexpr,      eq->var_values, eq), 0.1, 10.0);
    eq->gamma_weight = av_clipf(av_expr_eval(eq->gamma_weight_pexpr, eq->var_values, eq), 0.0,  1.0);

    eq->param[0].gamma = eq->gamma * eq->gamma_g;
    eq->param[1].gamma = sqrt(eq->gamma_b / eq->gamma_g);
    eq->param[2].gamma = sqrt(eq->gamma_r / eq->gamma_g);

    for (i = 0; i < 3; i++) {
        eq->param[i].gamma_weight = eq->gamma_weight;
        eq->param[i].lut_clean = 0;
        check_values(&eq->param[i], eq);
    }
}

static void set_saturation(EQContext *eq)
{
    int i;

    eq->saturation = av_clipf(av_expr_eval(eq->saturation_pexpr, eq->var_values, eq), 0.0, 3.0);

    for (i = 1; i < 3; i++) {
        eq->param[i].contrast = eq->saturation;
        eq->param[i].lut_clean = 0;
        check_values(&eq->param[i], eq);
    }
}

static int set_expr(AVExpr **pexpr, const char *expr, const char *option, void *log_ctx)
{
    int ret;
    AVExpr *old = NULL;

    if (*pexpr)
        old = *pexpr;
    ret = av_expr_parse(pexpr, expr, var_names, NULL, NULL, NULL, NULL, 0, log_ctx);
    if (ret < 0) {
        av_log(log_ctx, AV_LOG_ERROR,
               "Error when parsing the expression '%s' for %s\n",
               expr, option);
        *pexpr = old;
        return ret;
    }

    av_expr_free(old);
    return 0;
}

static int initialize(AVFilterContext *ctx)
{
    EQContext *eq = ctx->priv;
    int ret;

    eq->process = process_c;

    if ((ret = set_expr(&eq->contrast_pexpr,     eq->contrast_expr,     "contrast",     ctx)) < 0 ||
        (ret = set_expr(&eq->brightness_pexpr,   eq->brightness_expr,   "brightness",   ctx)) < 0 ||
        (ret = set_expr(&eq->saturation_pexpr,   eq->saturation_expr,   "saturation",   ctx)) < 0 ||
        (ret = set_expr(&eq->gamma_pexpr,        eq->gamma_expr,        "gamma",        ctx)) < 0 ||
        (ret = set_expr(&eq->gamma_r_pexpr,      eq->gamma_r_expr,      "gamma_r",      ctx)) < 0 ||
        (ret = set_expr(&eq->gamma_g_pexpr,      eq->gamma_g_expr,      "gamma_g",      ctx)) < 0 ||
        (ret = set_expr(&eq->gamma_b_pexpr,      eq->gamma_b_expr,      "gamma_b",      ctx)) < 0 ||
        (ret = set_expr(&eq->gamma_weight_pexpr, eq->gamma_weight_expr, "gamma_weight", ctx)) < 0 )
        return ret;

    if (ARCH_X86)
        ff_eq_init_x86(eq);

    if (eq->eval_mode == EVAL_MODE_INIT) {
        set_gamma(eq);
        set_contrast(eq);
        set_brightness(eq);
        set_saturation(eq);
    }

    return 0;
}

static void uninit(AVFilterContext *ctx)
{
    EQContext *eq = ctx->priv;

    av_expr_free(eq->contrast_pexpr);     eq->contrast_pexpr     = NULL;
    av_expr_free(eq->brightness_pexpr);   eq->brightness_pexpr   = NULL;
    av_expr_free(eq->saturation_pexpr);   eq->saturation_pexpr   = NULL;
    av_expr_free(eq->gamma_pexpr);        eq->gamma_pexpr        = NULL;
    av_expr_free(eq->gamma_weight_pexpr); eq->gamma_weight_pexpr = NULL;
    av_expr_free(eq->gamma_r_pexpr);      eq->gamma_r_pexpr      = NULL;
    av_expr_free(eq->gamma_g_pexpr);      eq->gamma_g_pexpr      = NULL;
    av_expr_free(eq->gamma_b_pexpr);      eq->gamma_b_pexpr      = NULL;
}

static int config_props(AVFilterLink *inlink)
{
    EQContext *eq = inlink->dst->priv;

    eq->var_values[VAR_N] = 0;
    eq->var_values[VAR_R] = inlink->frame_rate.num == 0 || inlink->frame_rate.den == 0 ?
        NAN : av_q2d(inlink->frame_rate);

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pixel_fmts_eq[] = {
        AV_PIX_FMT_GRAY8,
        AV_PIX_FMT_YUV410P,
        AV_PIX_FMT_YUV411P,
        AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_YUV422P,
        AV_PIX_FMT_YUV444P,
        AV_PIX_FMT_NONE
    };
    AVFilterFormats *fmts_list = ff_make_format_list(pixel_fmts_eq);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

#define TS2T(ts, tb) ((ts) == AV_NOPTS_VALUE ? NAN : (double)(ts) * av_q2d(tb))

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = inlink->dst->outputs[0];
    EQContext *eq = ctx->priv;
    AVFrame *out;
    int64_t pos = av_frame_get_pkt_pos(in);
    const AVPixFmtDescriptor *desc;
    int i;

    out = ff_get_video_buffer(outlink, inlink->w, inlink->h);
    if (!out)
        return AVERROR(ENOMEM);

    av_frame_copy_props(out, in);
    desc = av_pix_fmt_desc_get(inlink->format);

    eq->var_values[VAR_N]   = inlink->frame_count;
    eq->var_values[VAR_POS] = pos == -1 ? NAN : pos;
    eq->var_values[VAR_T]   = TS2T(in->pts, inlink->time_base);

    if (eq->eval_mode == EVAL_MODE_FRAME) {
        set_gamma(eq);
        set_contrast(eq);
        set_brightness(eq);
        set_saturation(eq);
    }

    for (i = 0; i < desc->nb_components; i++) {
        int w = inlink->w;
        int h = inlink->h;

        if (i == 1 || i == 2) {
            w = FF_CEIL_RSHIFT(w, desc->log2_chroma_w);
            h = FF_CEIL_RSHIFT(h, desc->log2_chroma_h);
        }

        if (eq->param[i].adjust)
            eq->param[i].adjust(&eq->param[i], out->data[i], out->linesize[i],
                                 in->data[i], in->linesize[i], w, h);
        else
            av_image_copy_plane(out->data[i], out->linesize[i],
                                in->data[i], in->linesize[i], w, h);
    }

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static inline int set_param(AVExpr **pexpr, const char *args, const char *cmd,
                            void (*set_fn)(EQContext *eq), AVFilterContext *ctx)
{
    EQContext *eq = ctx->priv;
    int ret;
    if ((ret = set_expr(pexpr, args, cmd, ctx)) < 0)
        return ret;
    if (eq->eval_mode == EVAL_MODE_INIT)
        set_fn(eq);
    return 0;
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    EQContext *eq = ctx->priv;

#define SET_PARAM(param_name, set_fn_name)                              \
    if (!strcmp(cmd, #param_name)) return set_param(&eq->param_name##_pexpr, args, cmd, set_##set_fn_name, ctx);

         SET_PARAM(contrast, contrast)
    else SET_PARAM(brightness, brightness)
    else SET_PARAM(saturation, saturation)
    else SET_PARAM(gamma, gamma)
    else SET_PARAM(gamma_r, gamma)
    else SET_PARAM(gamma_g, gamma)
    else SET_PARAM(gamma_b, gamma)
    else SET_PARAM(gamma_weight, gamma)
    else return AVERROR(ENOSYS);
}

static const AVFilterPad eq_inputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_props,
    },
    { NULL }
};

static const AVFilterPad eq_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

#define OFFSET(x) offsetof(EQContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption eq_options[] = {
    { "contrast",     "set the contrast adjustment, negative values give a negative image",
        OFFSET(contrast_expr),     AV_OPT_TYPE_STRING, {.str = "1.0"}, CHAR_MIN, CHAR_MAX, FLAGS },
    { "brightness",   "set the brightness adjustment",
        OFFSET(brightness_expr),   AV_OPT_TYPE_STRING, {.str = "0.0"}, CHAR_MIN, CHAR_MAX, FLAGS },
    { "saturation",   "set the saturation adjustment",
        OFFSET(saturation_expr),   AV_OPT_TYPE_STRING, {.str = "1.0"}, CHAR_MIN, CHAR_MAX, FLAGS },
    { "gamma",        "set the initial gamma value",
        OFFSET(gamma_expr),        AV_OPT_TYPE_STRING, {.str = "1.0"}, CHAR_MIN, CHAR_MAX, FLAGS },
    { "gamma_r",      "gamma value for red",
        OFFSET(gamma_r_expr),      AV_OPT_TYPE_STRING, {.str = "1.0"}, CHAR_MIN, CHAR_MAX, FLAGS },
    { "gamma_g",      "gamma value for green",
        OFFSET(gamma_g_expr),      AV_OPT_TYPE_STRING, {.str = "1.0"}, CHAR_MIN, CHAR_MAX, FLAGS },
    { "gamma_b",      "gamma value for blue",
        OFFSET(gamma_b_expr),      AV_OPT_TYPE_STRING, {.str = "1.0"}, CHAR_MIN, CHAR_MAX, FLAGS },
    { "gamma_weight", "set the gamma weight which reduces the effect of gamma on bright areas",
        OFFSET(gamma_weight_expr), AV_OPT_TYPE_STRING, {.str = "1.0"}, CHAR_MIN, CHAR_MAX, FLAGS },
    { "eval", "specify when to evaluate expressions", OFFSET(eval_mode), AV_OPT_TYPE_INT, {.i64 = EVAL_MODE_INIT}, 0, EVAL_MODE_NB-1, FLAGS, "eval" },
         { "init",  "eval expressions once during initialization", 0, AV_OPT_TYPE_CONST, {.i64=EVAL_MODE_INIT},  .flags = FLAGS, .unit = "eval" },
         { "frame", "eval expressions per-frame",                  0, AV_OPT_TYPE_CONST, {.i64=EVAL_MODE_FRAME}, .flags = FLAGS, .unit = "eval" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(eq);

AVFilter ff_vf_eq = {
    .name            = "eq",
    .description     = NULL_IF_CONFIG_SMALL("Adjust brightness, contrast, gamma, and saturation."),
    .priv_size       = sizeof(EQContext),
    .priv_class      = &eq_class,
    .inputs          = eq_inputs,
    .outputs         = eq_outputs,
    .process_command = process_command,
    .query_formats   = query_formats,
    .init            = initialize,
    .uninit          = uninit,
};
