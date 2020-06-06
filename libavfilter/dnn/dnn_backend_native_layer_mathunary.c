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
 * DNN native backend implementation.
 */

#include <math.h>

#include "dnn_backend_native.h"
#include "libavutil/avassert.h"
#include "dnn_backend_native_layer_mathunary.h"

int dnn_load_layer_math_unary(Layer *layer, AVIOContext *model_file_context, int file_size)
{
    DnnLayerMathUnaryParams *params;
    int dnn_size = 0;
    params = av_malloc(sizeof(*params));
    if(!params)
        return 0;

    params->un_op = (int32_t)avio_rl32(model_file_context);
    dnn_size += 4;
    layer->params = params;
    layer->input_operand_indexes[0] = (int32_t)avio_rl32(model_file_context);
    layer->output_operand_index = (int32_t)avio_rl32(model_file_context);
    dnn_size += 8;

    return dnn_size;

}

int dnn_execute_layer_math_unary(DnnOperand *operands, const int32_t *input_operand_indexes,
                                int32_t output_operand_index, const void *parameters)
{
    const DnnOperand *input = &operands[input_operand_indexes[0]];
    DnnOperand *output = &operands[output_operand_index];
    const DnnLayerMathUnaryParams *params = (const DnnLayerMathUnaryParams *)parameters;
    int dims_count;
    const float *src;
    float *dst;

    for (int i = 0; i < 4; ++i)
        output->dims[i] = input->dims[i];

    output->data_type = input->data_type;
    output->length = calculate_operand_data_length(output);
    output->data = av_realloc(output->data, output->length);
    if (!output->data)
        return DNN_ERROR;

    dims_count = calculate_operand_dims_count(output);
    src = input->data;
    dst = output->data;

    switch (params->un_op) {
    case DMUO_ABS:
        for (int i = 0; i < dims_count; ++i)
            dst[i] = FFABS(src[i]);
        return 0;
    case DMUO_SIN:
        for (int i = 0; i < dims_count; ++i)
            dst[i] = sin(src[i]);
        return 0;
    case DMUO_COS:
        for (int i = 0; i < dims_count; ++i)
            dst[i] = cos(src[i]);
        return 0;
    case DMUO_TAN:
        for (int i = 0; i < dims_count; ++i)
            dst[i] = tan(src[i]);
        return 0;
    default:
        return -1;
    }
}
