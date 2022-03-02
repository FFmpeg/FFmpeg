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

#include "libavutil/avassert.h"
#include "dnn_backend_native_layer_avgpool.h"

int ff_dnn_load_layer_avg_pool(Layer *layer, AVIOContext *model_file_context, int file_size, int operands_num)
{
    AvgPoolParams *avgpool_params;
    int dnn_size = 0;
    avgpool_params = av_malloc(sizeof(*avgpool_params));
    if(!avgpool_params)
        return 0;

    avgpool_params->strides = (int32_t)avio_rl32(model_file_context);
    avgpool_params->padding_method = (int32_t)avio_rl32(model_file_context);
    avgpool_params->kernel_size = (int32_t)avio_rl32(model_file_context);
    dnn_size += 12;

    if (dnn_size > file_size || avgpool_params->kernel_size <= 0 || avgpool_params->strides <=0){
        av_freep(&avgpool_params);
        return 0;
    }

    layer->params = avgpool_params;
    layer->input_operand_indexes[0] = (int32_t)avio_rl32(model_file_context);
    layer->output_operand_index = (int32_t)avio_rl32(model_file_context);
    dnn_size += 8;

    if (layer->input_operand_indexes[0] >= operands_num || layer->output_operand_index >= operands_num) {
        return 0;
    }
    return dnn_size;
}

int ff_dnn_execute_layer_avg_pool(DnnOperand *operands, const int32_t *input_operand_indexes,
                                  int32_t output_operand_index, const void *parameters, NativeContext *ctx)
{
    float *output;
    int height_end, width_end, height_radius, width_radius, output_height, output_width, kernel_area;
    int32_t input_operand_index = input_operand_indexes[0];
    int number = operands[input_operand_index].dims[0];
    int height = operands[input_operand_index].dims[1];
    int width = operands[input_operand_index].dims[2];
    int channel = operands[input_operand_index].dims[3];
    const float *input = operands[input_operand_index].data;
    const AvgPoolParams *avgpool_params = parameters;

    int kernel_strides = avgpool_params->strides;
    int src_linesize = width * channel;
    DnnOperand *output_operand = &operands[output_operand_index];

    /**
     * When padding_method = SAME, the tensorflow will only padding the hald number of 0 pixels
     * except the remainders.
     * Eg: assuming the input height = 1080, the strides = 11, so the remainders = 1080 % 11 = 2
     *     and if ksize = 5: it will fill (5 - 2) >> 1 = 1 line before the first line of input image,
     *                       and 5 - 2 - 1 = 2 lines after the last line of input image.
     *     and if ksize = 7: it will fill (7 - 2) >> 1 = 2 lines before the first line of input image,
     *                       and 7 - 2 - 2 = 3 lines after the last line of input image.
     */
    if (avgpool_params->padding_method == SAME) {
        height_end = height;
        width_end = width;
        height_radius = avgpool_params->kernel_size - ((height - 1) % kernel_strides + 1);
        width_radius = avgpool_params->kernel_size - ((width - 1) % kernel_strides + 1);
        height_radius = height_radius < 0 ? 0 : height_radius >> 1;
        width_radius = width_radius < 0 ? 0 : width_radius >> 1;
        output_height = ceil(height / (kernel_strides * 1.0));
        output_width = ceil(width / (kernel_strides * 1.0));
    } else {
        av_assert0(avgpool_params->padding_method == VALID);
        height_end = height - avgpool_params->kernel_size + 1;
        width_end = width - avgpool_params->kernel_size + 1;
        height_radius = 0;
        width_radius = 0;
        output_height = ceil((height - avgpool_params->kernel_size + 1) / (kernel_strides * 1.0));
        output_width = ceil((width - avgpool_params->kernel_size + 1) / (kernel_strides * 1.0));
    }

    output_operand->dims[0] = number;
    output_operand->dims[1] = output_height;
    output_operand->dims[2] = output_width;
    // not support pooling in channel dimension now
    output_operand->dims[3] = channel;
    output_operand->data_type = operands[input_operand_index].data_type;
    output_operand->length = ff_calculate_operand_data_length(output_operand);
    if (output_operand->length <= 0) {
        av_log(ctx, AV_LOG_ERROR, "The output data length overflow\n");
        return AVERROR(EINVAL);
    }
    output_operand->data = av_realloc(output_operand->data, output_operand->length);
    if (!output_operand->data) {
        av_log(ctx, AV_LOG_ERROR, "Failed to reallocate memory for output\n");
        return AVERROR(ENOMEM);
    }
    output = output_operand->data;

    for (int y = 0; y < height_end; y += kernel_strides) {
        for (int x = 0; x < width_end; x += kernel_strides) {
            for (int n_channel = 0; n_channel < channel; ++n_channel) {
                output[n_channel] = 0.0;
                kernel_area = 0;
                for (int kernel_y = 0; kernel_y < avgpool_params->kernel_size; ++kernel_y) {
                    for (int kernel_x = 0; kernel_x < avgpool_params->kernel_size; ++kernel_x) {
                        float input_pel;
                        int y_pos = y + (kernel_y - height_radius);
                        int x_pos = x + (kernel_x - width_radius);
                        if (x_pos < 0 || x_pos >= width || y_pos < 0 || y_pos >= height) {
                            input_pel = 0.0;
                        } else {
                            kernel_area++;
                            input_pel = input[y_pos * src_linesize + x_pos * channel + n_channel];
                        }
                        output[n_channel] += input_pel;
                    }
                }
                output[n_channel] /= kernel_area;
            }
            output += channel;
        }
    }

    return 0;
}
