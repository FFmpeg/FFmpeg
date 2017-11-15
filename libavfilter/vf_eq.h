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

#ifndef AVFILTER_EQ_H
#define AVFILTER_EQ_H

#include "avfilter.h"
#include "libavutil/eval.h"

static const char *const var_names[] = {
    "n",   // frame count
    "pos", // frame position
    "r",   // frame rate
    "t",   // timestamp expressed in seconds
    NULL
};

enum var_name {
    VAR_N,
    VAR_POS,
    VAR_R,
    VAR_T,
    VAR_NB
};

typedef struct EQParameters {
    void (*adjust)(struct EQParameters *eq, uint8_t *dst, int dst_stride,
                   const uint8_t *src, int src_stride, int w, int h);

    uint8_t lut[256];

    double brightness, contrast, gamma, gamma_weight;
    int lut_clean;

} EQParameters;

typedef struct EQContext {
    const AVClass *class;

    EQParameters param[3];

    char   *contrast_expr;
    AVExpr *contrast_pexpr;
    double  contrast;

    char   *brightness_expr;
    AVExpr *brightness_pexpr;
    double  brightness;

    char   *saturation_expr;
    AVExpr *saturation_pexpr;
    double  saturation;

    char   *gamma_expr;
    AVExpr *gamma_pexpr;
    double  gamma;

    char   *gamma_weight_expr;
    AVExpr *gamma_weight_pexpr;
    double  gamma_weight;

    char   *gamma_r_expr;
    AVExpr *gamma_r_pexpr;
    double  gamma_r;

    char   *gamma_g_expr;
    AVExpr *gamma_g_pexpr;
    double  gamma_g;

    char   *gamma_b_expr;
    AVExpr *gamma_b_pexpr;
    double  gamma_b;

    double var_values[VAR_NB];

    void (*process)(struct EQParameters *par, uint8_t *dst, int dst_stride,
                    const uint8_t *src, int src_stride, int w, int h);

    enum EvalMode { EVAL_MODE_INIT, EVAL_MODE_FRAME, EVAL_MODE_NB } eval_mode;
} EQContext;

void ff_eq_init_x86(EQContext *eq);

#endif /* AVFILTER_EQ_H */
