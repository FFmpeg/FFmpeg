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

#include "dnn_backend_native.h"
#include "libavutil/avassert.h"
#include "dnn_backend_native_layer_mathbinary.h"

int dnn_load_layer_math_binary(Layer *layer, AVIOContext *model_file_context, int file_size, int operands_num)
{
    DnnLayerMathBinaryParams *params;
    int dnn_size = 0;
    int input_index = 0;
    params = av_malloc(sizeof(*params));
    if (!params)
        return 0;

    params->bin_op = (int32_t)avio_rl32(model_file_context);
    dnn_size += 4;

    params->input0_broadcast = (int32_t)avio_rl32(model_file_context);
    dnn_size += 4;
    if (params->input0_broadcast) {
        params->v = av_int2float(avio_rl32(model_file_context));
    } else {
        layer->input_operand_indexes[input_index] = (int32_t)avio_rl32(model_file_context);
        if (layer->input_operand_indexes[input_index] >= operands_num) {
            return 0;
        }
        input_index++;
    }
    dnn_size += 4;

    params->input1_broadcast = (int32_t)avio_rl32(model_file_context);
    dnn_size += 4;
    if (params->input1_broadcast) {
        params->v = av_int2float(avio_rl32(model_file_context));
    } else {
        layer->input_operand_indexes[input_index] = (int32_t)avio_rl32(model_file_context);
        if (layer->input_operand_indexes[input_index] >= operands_num) {
            return 0;
        }
        input_index++;
    }
    dnn_size += 4;

    layer->output_operand_index = (int32_t)avio_rl32(model_file_context);
    dnn_size += 4;
    layer->params = params;

    if (layer->output_operand_index >= operands_num) {
        return 0;
    }

    return dnn_size;
}

int dnn_execute_layer_math_binary(DnnOperand *operands, const int32_t *input_operand_indexes,
                                 int32_t output_operand_index, const void *parameters)
{
    const DnnOperand *input = &operands[input_operand_indexes[0]];
    DnnOperand *output = &operands[output_operand_index];
    const DnnLayerMathBinaryParams *params = (const DnnLayerMathBinaryParams *)parameters;
    int dims_count;
    const float *src;
    float *dst;

    for (int i = 0; i < 4; ++i)
        output->dims[i] = input->dims[i];

    output->data_type = input->data_type;
    output->length = calculate_operand_data_length(output);
    if (output->length <= 0)
        return DNN_ERROR;
    output->data = av_realloc(output->data, output->length);
    if (!output->data)
        return DNN_ERROR;

    dims_count = calculate_operand_dims_count(output);
    src = input->data;
    dst = output->data;

    switch (params->bin_op) {
    case DMBO_SUB:
        if (params->input0_broadcast) {
            for (int i = 0; i < dims_count; ++i) {
                dst[i] = params->v - src[i];
            }
        } else if (params->input1_broadcast) {
            for (int i = 0; i < dims_count; ++i) {
                dst[i] = src[i] - params->v;
            }
        } else {
            const DnnOperand *input1 = &operands[input_operand_indexes[1]];
            const float *src1 = input1->data;
            for (int i = 0; i < dims_count; ++i) {
                dst[i] = src[i] - src1[i];
            }
        }
        return 0;
    case DMBO_ADD:
        if (params->input0_broadcast || params->input1_broadcast) {
            for (int i = 0; i < dims_count; ++i) {
                dst[i] = params->v + src[i];
            }
        } else {
            const DnnOperand *input1 = &operands[input_operand_indexes[1]];
            const float *src1 = input1->data;
            for (int i = 0; i < dims_count; ++i) {
                dst[i] = src[i] + src1[i];
            }
        }
        return 0;
    case DMBO_MUL:
        if (params->input0_broadcast || params->input1_broadcast) {
            for (int i = 0; i < dims_count; ++i) {
                dst[i] = params->v * src[i];
            }
        } else {
            const DnnOperand *input1 = &operands[input_operand_indexes[1]];
            const float *src1 = input1->data;
            for (int i = 0; i < dims_count; ++i) {
                dst[i] = src[i] * src1[i];
            }
        }
        return 0;
    case DMBO_REALDIV:
        if (params->input0_broadcast) {
            for (int i = 0; i < dims_count; ++i) {
                dst[i] = params->v / src[i];
            }
        } else if (params->input1_broadcast) {
            for (int i = 0; i < dims_count; ++i) {
                dst[i] = src[i] / params->v;
            }
        } else {
            const DnnOperand *input1 = &operands[input_operand_indexes[1]];
            const float *src1 = input1->data;
            for (int i = 0; i < dims_count; ++i) {
                dst[i] = src[i] / src1[i];
            }
        }
        return 0;
    case DMBO_MINIMUM:
        if (params->input0_broadcast || params->input1_broadcast) {
            for (int i = 0; i < dims_count; ++i) {
                dst[i] = FFMIN(params->v, src[i]);
            }
        } else {
            const DnnOperand *input1 = &operands[input_operand_indexes[1]];
            const float *src1 = input1->data;
            for (int i = 0; i < dims_count; ++i) {
                dst[i] = FFMIN(src[i], src1[i]);
            }
        }
        return 0;
    default:
        return -1;
    }
}
