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

/**
 * TODO:
 * - Add support to process_command
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
    else if (param->gamma == 1.0)
        param->adjust = eq->process;
    else
        param->adjust = apply_lut;
}

static void set_contrast(EQContext *eq)
{
    eq->var_values[VAR_CONTRAST] = av_clipf(av_expr_eval(eq->contrast_pexpr, eq->var_values, eq),-2.0, 2.0);
    eq->param[0].contrast = eq->var_values[VAR_CONTRAST];
    eq->param[0].lut_clean = 0;
    check_values(&eq->param[0], eq);
}

static void set_brightness(EQContext *eq)
{
    eq->var_values[VAR_BRIGHTNESS] =  av_clipf(av_expr_eval(eq->brightness_pexpr, eq->var_values, eq), -1.0, 1.0);
    eq->param[0].brightness = eq->var_values[VAR_BRIGHTNESS];
    eq->param[0].lut_clean = 0;
    check_values(&eq->param[0], eq);
}

static void set_gamma(EQContext *eq)
{
    int i;

    eq->var_values[VAR_GAMMA]        =  av_clipf(av_expr_eval(eq->gamma_pexpr,        eq->var_values, eq),  0.1, 10.0);
    eq->var_values[VAR_GAMMA_R]      =  av_clipf(av_expr_eval(eq->gamma_r_pexpr,      eq->var_values, eq),  0.1, 10.0);
    eq->var_values[VAR_GAMMA_G]      =  av_clipf(av_expr_eval(eq->gamma_g_pexpr,      eq->var_values, eq),  0.1, 10.0);
    eq->var_values[VAR_GAMMA_B]      =  av_clipf(av_expr_eval(eq->gamma_b_pexpr,      eq->var_values, eq),  0.1, 10.0);
    eq->var_values[VAR_GAMMA_WEIGHT] =  av_clipf(av_expr_eval(eq->gamma_weight_pexpr, eq->var_values, eq),  0.0,  1.0);

    eq->param[0].gamma = eq->var_values[VAR_GAMMA] * eq->var_values[VAR_GAMMA_G];
    eq->param[1].gamma = sqrt(eq->var_values[VAR_GAMMA_B] / eq->var_values[VAR_GAMMA_G]);
    eq->param[2].gamma = sqrt(eq->var_values[VAR_GAMMA_R] / eq->var_values[VAR_GAMMA_G]);

    for (i = 0; i < 3; i++) {
        eq->param[i].gamma_weight = eq->var_values[VAR_GAMMA_WEIGHT];
        eq->param[i].lut_clean = 0;
        check_values(&eq->param[i], eq);
    }
}

static void set_saturation(EQContext *eq)
{
    int i;

    eq->var_values[VAR_SATURATION] = av_clipf(av_expr_eval(eq->saturation_pexpr, eq->var_values, eq), 0.0, 3.0);

    for (i = 1; i < 3; i++) {
        eq->param[i].contrast = eq->var_values[VAR_SATURATION];
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
    ret = av_expr_parse(pexpr, expr, var_names,
                        NULL, NULL, NULL, NULL, 0, log_ctx);
    if (ret < 0) {
        av_log(log_ctx, AV_LOG_ERROR,
               "Error when evaluating the expression '%s' for %s\n",
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

    set_gamma(eq);
    set_contrast(eq);
    set_brightness(eq);
    set_saturation(eq);

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

    ff_set_common_formats(ctx, ff_make_format_list(pixel_fmts_eq));

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = inlink->dst->outputs[0];
    EQContext *eq = ctx->priv;
    AVFrame *out;
    const AVPixFmtDescriptor *desc;
    int i;

    out = ff_get_video_buffer(outlink, inlink->w, inlink->h);
    if (!out)
        return AVERROR(ENOMEM);

    av_frame_copy_props(out, in);
    desc = av_pix_fmt_desc_get(inlink->format);

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

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    EQContext *eq = ctx->priv;
    int ret;

    if (!strcmp(cmd, "contrast")) {
        ret = set_expr(&eq->contrast_pexpr, args, cmd, ctx);
        set_contrast(eq);
        return ret;
    }
    else if (!strcmp(cmd, "brightness")) {
        ret = set_expr(&eq->brightness_pexpr, args, cmd, ctx);
        set_brightness(eq);
        return ret;
    }
    else if (!strcmp(cmd, "saturation")) {
        ret = set_expr(&eq->saturation_pexpr, args, cmd, ctx);
        set_saturation(eq);
        return ret;
    }
    else if (!strcmp(cmd, "gamma")) {
        ret = set_expr(&eq->gamma_pexpr, args, cmd, ctx);
        set_gamma(eq);
        return ret;
    }
    else if (!strcmp(cmd, "gamma_r")) {
        ret = set_expr(&eq->gamma_r_pexpr, args, cmd, ctx);
        set_gamma(eq);
        return ret;
    }
    else if (!strcmp(cmd, "gamma_g")) {
        ret = set_expr(&eq->gamma_g_pexpr, args, cmd, ctx);
        set_gamma(eq);
        return ret;
    }
    else if (!strcmp(cmd, "gamma_b")) {
        ret = set_expr(&eq->gamma_b_pexpr, args, cmd, ctx);
        set_gamma(eq);
        return ret;
    }
    else if (!strcmp(cmd, "gamma_weight")) {
        ret = set_expr(&eq->gamma_weight_pexpr, args, cmd, ctx);
        set_gamma(eq);
        return ret;
    }
    else
        return AVERROR(ENOSYS);
}

static const AVFilterPad eq_inputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
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
