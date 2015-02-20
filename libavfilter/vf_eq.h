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

static const char * const var_names[] = {
    "contrast",
    "brightness",
    "saturation",
    "gamma",
    "gamma_weight",
    "gamma_r",
    "gamma_g",
    "gamma_b",
    NULL
};

enum var_name {
    VAR_CONTRAST ,
    VAR_BRIGHTNESS ,
    VAR_SATURATION ,
    VAR_GAMMA ,
    VAR_GAMMA_WEIGHT ,
    VAR_GAMMA_R ,
    VAR_GAMMA_G ,
    VAR_GAMMA_B ,
    VAR_VARS_NB ,
};

typedef struct EQParameters {
    void (*adjust)(struct EQParameters *eq, uint8_t *dst, int dst_stride,
                   const uint8_t *src, int src_stride, int w, int h);

    uint8_t lut[256];

    double brightness, contrast, gamma, gamma_weight;
    int lut_clean;

} EQParameters;

typedef struct {
    const AVClass *class;

    EQParameters param[3];

    char   *contrast_expr;
    AVExpr *contrast_pexpr;

    char   *brightness_expr;
    AVExpr *brightness_pexpr;

    char   *saturation_expr;
    AVExpr *saturation_pexpr;

    char   *gamma_expr;
    AVExpr *gamma_pexpr;

    char   *gamma_weight_expr;
    AVExpr *gamma_weight_pexpr;

    char   *gamma_r_expr;
    AVExpr *gamma_r_pexpr;

    char   *gamma_g_expr;
    AVExpr *gamma_g_pexpr;

    char   *gamma_b_expr;
    AVExpr *gamma_b_pexpr;

    double var_values[VAR_VARS_NB];

    void (*process)(struct EQParameters *par, uint8_t *dst, int dst_stride,
                    const uint8_t *src, int src_stride, int w, int h);

} EQContext;

void ff_eq_init_x86(EQContext *eq);

#endif /* AVFILTER_EQ_H */
