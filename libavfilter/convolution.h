/*
 * Copyright (c) 2012-2013 Oka Motofumi (chikuzen.mo at gmail dot com)
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
#ifndef AVFILTER_CONVOLUTION_H
#define AVFILTER_CONVOLUTION_H
#include "avfilter.h"

enum MatrixMode {
    MATRIX_SQUARE,
    MATRIX_ROW,
    MATRIX_COLUMN,
    MATRIX_NBMODES,
};

typedef struct ConvolutionContext {
    const AVClass *class;

    char *matrix_str[4];
    float rdiv[4];
    float bias[4];
    int mode[4];
    float scale;
    float delta;
    int planes;

    int size[4];
    int depth;
    int max;
    int bpc;
    int nb_planes;
    int nb_threads;
    int planewidth[4];
    int planeheight[4];
    int matrix[4][49];
    int matrix_length[4];
    int copy[4];

    void (*setup[4])(int radius, const uint8_t *c[], const uint8_t *src, int stride,
                     int x, int width, int y, int height, int bpc);
    void (*filter[4])(uint8_t *dst, int width,
                      float rdiv, float bias, const int *const matrix,
                      const uint8_t *c[], int peak, int radius,
                      int dstride, int stride);
} ConvolutionContext;

void ff_convolution_init_x86(ConvolutionContext *s);
#endif
