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

    double contrast;
    double brightness;
    double saturation;

    double gamma;
    double gamma_weight;
    double gamma_r, gamma_g, gamma_b;

    void (*process)(struct EQParameters *par, uint8_t *dst, int dst_stride,
                    const uint8_t *src, int src_stride, int w, int h);

} EQContext;

void ff_eq_init_x86(EQContext *eq);

#endif /* AVFILTER_EQ_H */
