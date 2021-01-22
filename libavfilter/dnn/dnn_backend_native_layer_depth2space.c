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

/**
 * @file
 * DNN native backend implementation.
 */

#include "dnn_backend_native.h"
#include "libavutil/avassert.h"
#include "dnn_backend_native_layer_depth2space.h"

int ff_dnn_load_layer_depth2space(Layer *layer, AVIOContext *model_file_context, int file_size, int operands_num)
{
    DepthToSpaceParams *params;
    int dnn_size = 0;
    params = av_malloc(sizeof(*params));
    if (!params)
        return 0;

    params->block_size = (int32_t)avio_rl32(model_file_context);
    dnn_size += 4;
    layer->input_operand_indexes[0] = (int32_t)avio_rl32(model_file_context);
    layer->output_operand_index = (int32_t)avio_rl32(model_file_context);
    dnn_size += 8;
    layer->params = params;

    if (layer->input_operand_indexes[0] >= operands_num || layer->output_operand_index >= operands_num) {
        return 0;
    }

    return dnn_size;
}

int ff_dnn_execute_layer_depth2space(DnnOperand *operands, const int32_t *input_operand_indexes,
                                     int32_t output_operand_index, const void *parameters, NativeContext *ctx)
{
    float *output;
    const DepthToSpaceParams *params = parameters;
    int block_size = params->block_size;
    int32_t input_operand_index = input_operand_indexes[0];
    int number = operands[input_operand_index].dims[0];
    int height = operands[input_operand_index].dims[1];
    int width = operands[input_operand_index].dims[2];
    int channels = operands[input_operand_index].dims[3];
    const float *input = operands[input_operand_index].data;

    int y, x, by, bx, ch;
    int new_channels = channels / (block_size * block_size);
    int output_linesize = width * channels;
    int by_linesize = output_linesize / block_size;
    int x_linesize = new_channels * block_size;

    DnnOperand *output_operand = &operands[output_operand_index];
    output_operand->dims[0] = number;
    output_operand->dims[1] = height * block_size;
    output_operand->dims[2] = width * block_size;
    output_operand->dims[3] = new_channels;
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

    for (y = 0; y < height; ++y){
        for (x = 0; x < width; ++x){
            for (by = 0; by < block_size; ++by){
                for (bx = 0; bx < block_size; ++bx){
                    for (ch = 0; ch < new_channels; ++ch){
                        output[by * by_linesize + x * x_linesize + bx * new_channels + ch] = input[ch];
                    }
                    input += new_channels;
                }
            }
        }
        output += output_linesize;
    }
    return 0;
}
