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

#include "libavutil/avassert.h"
#include "dnn_backend_native_layer_dense.h"

int ff_dnn_load_layer_dense(Layer *layer, AVIOContext *model_file_context, int file_size, int operands_num)
{
    DenseParams *dense_params;
    int kernel_size;
    int dnn_size = 0;
    dense_params = av_malloc(sizeof(*dense_params));
    if (!dense_params)
        return 0;

    dense_params->activation = (int32_t)avio_rl32(model_file_context);
    dense_params->input_num = (int32_t)avio_rl32(model_file_context);
    dense_params->output_num = (int32_t)avio_rl32(model_file_context);
    dense_params->has_bias = (int32_t)avio_rl32(model_file_context);
    dnn_size += 16;

    kernel_size = dense_params->input_num * dense_params->output_num;
    dnn_size += kernel_size * 4;
    if (dense_params->has_bias)
        dnn_size += dense_params->output_num * 4;

    if (dnn_size > file_size || dense_params->input_num <= 0 ||
        dense_params->output_num <= 0){
        av_freep(&dense_params);
        return 0;
    }

    dense_params->kernel = av_malloc(kernel_size * sizeof(float));
    if (!dense_params->kernel) {
        av_freep(&dense_params);
        return 0;
    }
    for (int i = 0; i < kernel_size; ++i) {
        dense_params->kernel[i] = av_int2float(avio_rl32(model_file_context));
    }

    dense_params->biases = NULL;
    if (dense_params->has_bias) {
        dense_params->biases = av_malloc(dense_params->output_num * sizeof(float));
        if (!dense_params->biases){
            av_freep(&dense_params->kernel);
            av_freep(&dense_params);
            return 0;
        }
        for (int i = 0; i < dense_params->output_num; ++i){
            dense_params->biases[i] = av_int2float(avio_rl32(model_file_context));
        }
    }

    layer->params = dense_params;

    layer->input_operand_indexes[0] = (int32_t)avio_rl32(model_file_context);
    layer->output_operand_index = (int32_t)avio_rl32(model_file_context);
    dnn_size += 8;

    if (layer->input_operand_indexes[0] >= operands_num || layer->output_operand_index >= operands_num) {
        return 0;
    }

    return dnn_size;
}

int ff_dnn_execute_layer_dense(DnnOperand *operands, const int32_t *input_operand_indexes,
                               int32_t output_operand_index, const void *parameters, NativeContext *ctx)
{
    float *output;
    int32_t input_operand_index = input_operand_indexes[0];
    int number = operands[input_operand_index].dims[0];
    int height = operands[input_operand_index].dims[1];
    int width = operands[input_operand_index].dims[2];
    int channel = operands[input_operand_index].dims[3];
    const float *input = operands[input_operand_index].data;
    const DenseParams *dense_params = parameters;

    int src_linesize = width * channel;
    DnnOperand *output_operand = &operands[output_operand_index];
    output_operand->dims[0] = number;
    output_operand->dims[1] = height;
    output_operand->dims[2] = width;
    output_operand->dims[3] = dense_params->output_num;
    output_operand->data_type = operands[input_operand_index].data_type;
    output_operand->length = ff_calculate_operand_data_length(output_operand);
    if (output_operand->length <= 0) {
        av_log(ctx, AV_LOG_ERROR, "The output data length overflow\n");
        return DNN_ERROR;
    }
    output_operand->data = av_realloc(output_operand->data, output_operand->length);
    if (!output_operand->data) {
        av_log(ctx, AV_LOG_ERROR, "Failed to reallocate memory for output\n");
        return DNN_ERROR;
    }
    output = output_operand->data;

    av_assert0(channel == dense_params->input_num);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            for (int n_filter = 0; n_filter < dense_params->output_num; ++n_filter) {
                if (dense_params->has_bias)
                    output[n_filter] = dense_params->biases[n_filter];
                else
                    output[n_filter] = 0.f;

                for (int ch = 0; ch < dense_params->input_num; ++ch) {
                    float input_pel;
                    input_pel = input[y * src_linesize + x * dense_params->input_num + ch];
                    output[n_filter] += input_pel * dense_params->kernel[n_filter*dense_params->input_num + ch];
                }
                switch (dense_params->activation){
                case RELU:
                    output[n_filter] = FFMAX(output[n_filter], 0.0);
                    break;
                case TANH:
                    output[n_filter] = 2.0f  / (1.0f + exp(-2.0f * output[n_filter])) - 1.0f;
                    break;
                case SIGMOID:
                    output[n_filter] = 1.0f / (1.0f + exp(-output[n_filter]));
                    break;
                case NONE:
                    break;
                case LEAKY_RELU:
                    output[n_filter] = FFMAX(output[n_filter], 0.0) + 0.2 * FFMIN(output[n_filter], 0.0);
                }
            }
            output += dense_params->output_num;
        }
    }
    return 0;
}
