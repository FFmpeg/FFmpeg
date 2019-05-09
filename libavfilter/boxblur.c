/*
 * Copyright (c) 2002 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (c) 2011 Stefano Sabatini
 * Copyright (c) 2018 Danil Iashchenko
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

#include "boxblur.h"

static const char *const var_names[] = {
    "w",
    "h",
    "cw",
    "ch",
    "hsub",
    "vsub",
    NULL
};

enum var_name {
    VAR_W,
    VAR_H,
    VAR_CW,
    VAR_CH,
    VAR_HSUB,
    VAR_VSUB,
    VARS_NB
};


int ff_boxblur_eval_filter_params(AVFilterLink *inlink,
                                  FilterParam *luma_param,
                                  FilterParam *chroma_param,
                                  FilterParam *alpha_param)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    AVFilterContext *ctx = inlink->dst;
    int w = inlink->w, h = inlink->h;
    int cw, ch;
    double var_values[VARS_NB], res;
    char *expr;
    int ret;

    if (!luma_param->radius_expr) {
        av_log(ctx, AV_LOG_ERROR, "Luma radius expression is not set.\n");
        return AVERROR(EINVAL);
    }

    /* fill missing params */
    if (!chroma_param->radius_expr) {
        chroma_param->radius_expr = av_strdup(luma_param->radius_expr);
        if (!chroma_param->radius_expr)
            return AVERROR(ENOMEM);
    }
    if (chroma_param->power < 0)
        chroma_param->power = luma_param->power;

    if (!alpha_param->radius_expr) {
        alpha_param->radius_expr = av_strdup(luma_param->radius_expr);
        if (!alpha_param->radius_expr)
            return AVERROR(ENOMEM);
    }
    if (alpha_param->power < 0)
        alpha_param->power = luma_param->power;

    var_values[VAR_W]       = inlink->w;
    var_values[VAR_H]       = inlink->h;
    var_values[VAR_CW] = cw = w>>(desc->log2_chroma_w);
    var_values[VAR_CH] = ch = h>>(desc->log2_chroma_h);
    var_values[VAR_HSUB]    = 1<<(desc->log2_chroma_w);
    var_values[VAR_VSUB]    = 1<<(desc->log2_chroma_h);

#define EVAL_RADIUS_EXPR(comp)                                          \
    expr = comp->radius_expr;                                           \
    ret = av_expr_parse_and_eval(&res, expr, var_names, var_values,     \
                                 NULL, NULL, NULL, NULL, NULL, 0, ctx); \
    comp->radius = res;                                                 \
    if (ret < 0) {                                                      \
        av_log(NULL, AV_LOG_ERROR,                                      \
               "Error when evaluating " #comp " radius expression '%s'\n", expr); \
        return ret;                                                     \
    }

    EVAL_RADIUS_EXPR(luma_param);
    EVAL_RADIUS_EXPR(chroma_param);
    EVAL_RADIUS_EXPR(alpha_param);

    av_log(ctx, AV_LOG_VERBOSE,
           "luma_radius:%d luma_power:%d "
           "chroma_radius:%d chroma_power:%d "
           "alpha_radius:%d alpha_power:%d "
           "w:%d chroma_w:%d h:%d chroma_h:%d\n",
           luma_param  ->radius, luma_param  ->power,
           chroma_param->radius, chroma_param->power,
           alpha_param ->radius, alpha_param ->power,
           w, cw, h, ch);


#define CHECK_RADIUS_VAL(w_, h_, comp)                                  \
    if (comp->radius < 0 ||                                   \
        2*comp->radius > FFMIN(w_, h_)) {                     \
        av_log(ctx, AV_LOG_ERROR,                                       \
               "Invalid " #comp " radius value %d, must be >= 0 and <= %d\n", \
               comp->radius, FFMIN(w_, h_)/2);                \
        return AVERROR(EINVAL);                                         \
    }
    CHECK_RADIUS_VAL(w,  h,  luma_param);
    CHECK_RADIUS_VAL(cw, ch, chroma_param);
    CHECK_RADIUS_VAL(w,  h,  alpha_param);

    return 0;
}
