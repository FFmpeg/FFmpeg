/*
 * Copyright (c) 2018 Sergey Lavrushkin
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
#include "dnn_backend_native_layer_conv2d.h"

#define CLAMP_TO_EDGE(x, w) ((x) < 0 ? 0 : ((x) >= (w) ? (w - 1) : (x)))

int dnn_load_layer_conv2d(Layer *layer, AVIOContext *model_file_context, int file_size)
{
    ConvolutionalParams *conv_params;
    int kernel_size;
    int dnn_size = 0;
    conv_params = av_malloc(sizeof(*conv_params));
    if (!conv_params)
        return 0;

    conv_params->dilation = (int32_t)avio_rl32(model_file_context);
    conv_params->padding_method = (int32_t)avio_rl32(model_file_context);
    conv_params->activation = (int32_t)avio_rl32(model_file_context);
    conv_params->input_num = (int32_t)avio_rl32(model_file_context);
    conv_params->output_num = (int32_t)avio_rl32(model_file_context);
    conv_params->kernel_size = (int32_t)avio_rl32(model_file_context);
    kernel_size = conv_params->input_num * conv_params->output_num *
                  conv_params->kernel_size * conv_params->kernel_size;
    dnn_size += 24 + (kernel_size + conv_params->output_num << 2);
    if (dnn_size > file_size || conv_params->input_num <= 0 ||
        conv_params->output_num <= 0 || conv_params->kernel_size <= 0){
        av_freep(&conv_params);
        return 0;
    }
    conv_params->kernel = av_malloc(kernel_size * sizeof(float));
    conv_params->biases = av_malloc(conv_params->output_num * sizeof(float));
    if (!conv_params->kernel || !conv_params->biases){
        av_freep(&conv_params->kernel);
        av_freep(&conv_params->biases);
        av_freep(&conv_params);
        return 0;
    }
    for (int i = 0; i < kernel_size; ++i){
        conv_params->kernel[i] = av_int2float(avio_rl32(model_file_context));
    }
    for (int i = 0; i < conv_params->output_num; ++i){
        conv_params->biases[i] = av_int2float(avio_rl32(model_file_context));
    }

    layer->params = conv_params;

    layer->input_operand_indexes[0] = (int32_t)avio_rl32(model_file_context);
    layer->output_operand_index = (int32_t)avio_rl32(model_file_context);
    dnn_size += 8;
    return dnn_size;
}

int dnn_execute_layer_conv2d(DnnOperand *operands, const int32_t *input_operand_indexes,
                             int32_t output_operand_index, const void *parameters)
{
    float *output;
    int32_t input_operand_index = input_operand_indexes[0];
    int number = operands[input_operand_index].dims[0];
    int height = operands[input_operand_index].dims[1];
    int width = operands[input_operand_index].dims[2];
    int channel = operands[input_operand_index].dims[3];
    const float *input = operands[input_operand_index].data;
    const ConvolutionalParams *conv_params = (const ConvolutionalParams *)parameters;

    int radius = conv_params->kernel_size >> 1;
    int src_linesize = width * conv_params->input_num;
    int filter_linesize = conv_params->kernel_size * conv_params->input_num;
    int filter_size = conv_params->kernel_size * filter_linesize;
    int pad_size = (conv_params->padding_method == VALID) ? (conv_params->kernel_size - 1) / 2 * conv_params->dilation : 0;

    DnnOperand *output_operand = &operands[output_operand_index];
    output_operand->dims[0] = number;
    output_operand->dims[1] = height - pad_size * 2;
    output_operand->dims[2] = width - pad_size * 2;
    output_operand->dims[3] = conv_params->output_num;
    output_operand->length = calculate_operand_data_length(output_operand);
    output_operand->data = av_realloc(output_operand->data, output_operand->length);
    if (!output_operand->data)
        return -1;
    output = output_operand->data;

    av_assert0(channel == conv_params->input_num);

    for (int y = pad_size; y < height - pad_size; ++y) {
        for (int x = pad_size; x < width - pad_size; ++x) {
            for (int n_filter = 0; n_filter < conv_params->output_num; ++n_filter) {
                output[n_filter] = conv_params->biases[n_filter];

                for (int ch = 0; ch < conv_params->input_num; ++ch) {
                    for (int kernel_y = 0; kernel_y < conv_params->kernel_size; ++kernel_y) {
                        for (int kernel_x = 0; kernel_x < conv_params->kernel_size; ++kernel_x) {
                            float input_pel;
                            if (conv_params->padding_method == SAME_CLAMP_TO_EDGE) {
                                int y_pos = CLAMP_TO_EDGE(y + (kernel_y - radius) * conv_params->dilation, height);
                                int x_pos = CLAMP_TO_EDGE(x + (kernel_x - radius) * conv_params->dilation, width);
                                input_pel = input[y_pos * src_linesize + x_pos * conv_params->input_num + ch];
                            } else {
                                int y_pos = y + (kernel_y - radius) * conv_params->dilation;
                                int x_pos = x + (kernel_x - radius) * conv_params->dilation;
                                input_pel = (x_pos < 0 || x_pos >= width || y_pos < 0 || y_pos >= height) ? 0.0 :
                                                   input[y_pos * src_linesize + x_pos * conv_params->input_num + ch];
                            }


                            output[n_filter] += input_pel * conv_params->kernel[n_filter * filter_size + kernel_y * filter_linesize +
                                                                                kernel_x * conv_params->input_num + ch];
                        }
                    }
                }
                switch (conv_params->activation){
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
            output += conv_params->output_num;
        }
    }
    return 0;
}
