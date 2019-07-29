/*
 * Copyright (c) 2019 Guo Yejun
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
 * layer pad (equivalent to tf.pad) for native backend.
 */
#ifndef AVFILTER_DNN_DNN_BACKEND_NATIVE_LAYER_PAD_H
#define AVFILTER_DNN_DNN_BACKEND_NATIVE_LAYER_PAD_H

#include <stdint.h>

typedef enum {LPMP_CONSTANT, LPMP_REFLECT, LPMP_SYMMETRIC} LayerPadModeParam;

typedef struct LayerPadParams{
    int32_t paddings[4][2];
    LayerPadModeParam mode;
    float constant_values;
} LayerPadParams;

void dnn_execute_layer_pad(const float *input, float *output, const LayerPadParams *params, int number, int height, int width, int channel);

#endif
