/*
 * Copyright (c) 2020
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
 * DNN inference functions interface for native backend.
 */

#ifndef AVFILTER_DNN_DNN_BACKEND_NATIVE_LAYER_MATHUNARY_H
#define AVFILTER_DNN_DNN_BACKEND_NATIVE_LAYER_MATHUNARY_H

#include "libavformat/avio.h"
#include "dnn_backend_native.h"

typedef enum {
    DMUO_ABS = 0,
    DMUO_COUNT
} DNNMathUnaryOperation;

typedef struct DnnLayerMathUnaryParams{
    DNNMathUnaryOperation un_op;
} DnnLayerMathUnaryParams;

int dnn_load_layer_math_unary(Layer *layer, AVIOContext *model_file_context, int file_size);
int dnn_execute_layer_math_unary(DnnOperand *operands, const int32_t *input_operand_indexes,
                                int32_t output_operand_index, const void *parameters);

#endif
