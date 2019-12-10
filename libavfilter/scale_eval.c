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

#include <stdint.h>
#include "scale_eval.h"
#include "libavutil/eval.h"
#include "libavutil/mathematics.h"
#include "libavutil/pixdesc.h"

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

/**
 * This must be kept in sync with var_names so that it is always a
 * complete list of var_names with the scale2ref specific names
 * appended. scale2ref values must appear in the order they appear
 * in the var_name_scale2ref enum but also be below all of the
 * non-scale2ref specific values.
 */
static const char *const var_names_scale2ref[] = {
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
    "main_w",
    "main_h",
    "main_a",
    "main_sar",
    "main_dar", "mdar",
    "main_hsub",
    "main_vsub",
    NULL
};

enum var_name_scale2ref {
    VAR_S2R_MAIN_W,
    VAR_S2R_MAIN_H,
    VAR_S2R_MAIN_A,
    VAR_S2R_MAIN_SAR,
    VAR_S2R_MAIN_DAR, VAR_S2R_MDAR,
    VAR_S2R_MAIN_HSUB,
    VAR_S2R_MAIN_VSUB,
    VARS_S2R_NB
};

int ff_scale_eval_dimensions(void *log_ctx,
    const char *w_expr, const char *h_expr,
    AVFilterLink *inlink, AVFilterLink *outlink,
    int *ret_w, int *ret_h)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    const AVPixFmtDescriptor *out_desc = av_pix_fmt_desc_get(outlink->format);
    const char *expr;
    int eval_w, eval_h;
    int ret;
    const char scale2ref = outlink->src->nb_inputs == 2 && outlink->src->inputs[1] == inlink;
    double var_values[VARS_NB + VARS_S2R_NB], res;
    const AVPixFmtDescriptor *main_desc;
    const AVFilterLink *main_link;
    const char *const *names = scale2ref ? var_names_scale2ref : var_names;

    if (scale2ref) {
        main_link = outlink->src->inputs[0];
        main_desc = av_pix_fmt_desc_get(main_link->format);
    }

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

    if (scale2ref) {
        var_values[VARS_NB + VAR_S2R_MAIN_W] = main_link->w;
        var_values[VARS_NB + VAR_S2R_MAIN_H] = main_link->h;
        var_values[VARS_NB + VAR_S2R_MAIN_A] = (double) main_link->w / main_link->h;
        var_values[VARS_NB + VAR_S2R_MAIN_SAR] = main_link->sample_aspect_ratio.num ?
            (double) main_link->sample_aspect_ratio.num / main_link->sample_aspect_ratio.den : 1;
        var_values[VARS_NB + VAR_S2R_MAIN_DAR] = var_values[VARS_NB + VAR_S2R_MDAR] =
            var_values[VARS_NB + VAR_S2R_MAIN_A] * var_values[VARS_NB + VAR_S2R_MAIN_SAR];
        var_values[VARS_NB + VAR_S2R_MAIN_HSUB] = 1 << main_desc->log2_chroma_w;
        var_values[VARS_NB + VAR_S2R_MAIN_VSUB] = 1 << main_desc->log2_chroma_h;
    }

    /* evaluate width and height */
    av_expr_parse_and_eval(&res, (expr = w_expr),
                           names, var_values,
                           NULL, NULL, NULL, NULL, NULL, 0, log_ctx);
    eval_w = var_values[VAR_OUT_W] = var_values[VAR_OW] = (int) res == 0 ? inlink->w : (int) res;

    if ((ret = av_expr_parse_and_eval(&res, (expr = h_expr),
                                      names, var_values,
                                      NULL, NULL, NULL, NULL, NULL, 0, log_ctx)) < 0)
        goto fail;
    eval_h = var_values[VAR_OUT_H] = var_values[VAR_OH] = (int) res == 0 ? inlink->h : (int) res;
    /* evaluate again the width, as it may depend on the output height */
    if ((ret = av_expr_parse_and_eval(&res, (expr = w_expr),
                                      names, var_values,
                                      NULL, NULL, NULL, NULL, NULL, 0, log_ctx)) < 0)
        goto fail;
    eval_w = (int) res == 0 ? inlink->w : (int) res;

    *ret_w = eval_w;
    *ret_h = eval_h;

    return 0;

fail:
    av_log(log_ctx, AV_LOG_ERROR,
           "Error when evaluating the expression '%s'.\n"
           "Maybe the expression for out_w:'%s' or for out_h:'%s' is self-referencing.\n",
           expr, w_expr, h_expr);
    return ret;
}

int ff_scale_adjust_dimensions(AVFilterLink *inlink,
    int *ret_w, int *ret_h,
    int force_original_aspect_ratio, int force_divisible_by)
{
    int w, h;
    int factor_w, factor_h;

    w = *ret_w;
    h = *ret_h;

    /* Check if it is requested that the result has to be divisible by some
     * factor (w or h = -n with n being the factor). */
    factor_w = 1;
    factor_h = 1;
    if (w < -1) {
        factor_w = -w;
    }
    if (h < -1) {
        factor_h = -h;
    }

    if (w < 0 && h < 0) {
        w = inlink->w;
        h = inlink->h;
    }

    /* Make sure that the result is divisible by the factor we determined
     * earlier. If no factor was set, nothing will happen as the default
     * factor is 1 */
    if (w < 0)
        w = av_rescale(h, inlink->w, inlink->h * factor_w) * factor_w;
    if (h < 0)
        h = av_rescale(w, inlink->h, inlink->w * factor_h) * factor_h;

    /* Note that force_original_aspect_ratio may overwrite the previous set
     * dimensions so that it is not divisible by the set factors anymore
     * unless force_divisible_by is defined as well */
    if (force_original_aspect_ratio) {
        int tmp_w = av_rescale(h, inlink->w, inlink->h);
        int tmp_h = av_rescale(w, inlink->h, inlink->w);

        if (force_original_aspect_ratio == 1) {
             w = FFMIN(tmp_w, w);
             h = FFMIN(tmp_h, h);
             if (force_divisible_by > 1) {
                 // round down
                 w = w / force_divisible_by * force_divisible_by;
                 h = h / force_divisible_by * force_divisible_by;
             }
        } else {
             w = FFMAX(tmp_w, w);
             h = FFMAX(tmp_h, h);
             if (force_divisible_by > 1) {
                 // round up
                 w = (w + force_divisible_by - 1) / force_divisible_by * force_divisible_by;
                 h = (h + force_divisible_by - 1) / force_divisible_by * force_divisible_by;
             }
        }
    }

    *ret_w = w;
    *ret_h = h;

    return 0;
}
