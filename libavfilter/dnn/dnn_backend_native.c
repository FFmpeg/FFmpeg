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
#include "dnn_backend_native_layer_pad.h"

static DNNReturnType set_input_output_native(void *model, DNNInputData *input, const char *input_name, const char **output_names, uint32_t nb_output)
{
    ConvolutionalNetwork *network = (ConvolutionalNetwork *)model;
    InputParams *input_params;
    ConvolutionalParams *conv_params;
    DepthToSpaceParams *depth_to_space_params;
    LayerPadParams *pad_params;
    int cur_width, cur_height, cur_channels;
    int32_t layer;

    if (network->layers_num <= 0 || network->layers[0].type != INPUT){
        return DNN_ERROR;
    }
    else{
        input_params = (InputParams *)network->layers[0].params;
        input_params->width = cur_width = input->width;
        input_params->height = cur_height = input->height;
        input_params->channels = cur_channels = input->channels;
        if (input->data){
            av_freep(&input->data);
        }
        av_assert0(input->dt == DNN_FLOAT);
        network->layers[0].output = input->data = av_malloc(cur_height * cur_width * cur_channels * sizeof(float));
        if (!network->layers[0].output){
            return DNN_ERROR;
        }
    }

    for (layer = 1; layer < network->layers_num; ++layer){
        switch (network->layers[layer].type){
        case CONV:
            conv_params = (ConvolutionalParams *)network->layers[layer].params;
            if (conv_params->input_num != cur_channels){
                return DNN_ERROR;
            }
            cur_channels = conv_params->output_num;

            if (conv_params->padding_method == VALID) {
                int pad_size = (conv_params->kernel_size - 1) * conv_params->dilation;
                cur_height -= pad_size;
                cur_width -= pad_size;
            }
            break;
        case DEPTH_TO_SPACE:
            depth_to_space_params = (DepthToSpaceParams *)network->layers[layer].params;
            if (cur_channels % (depth_to_space_params->block_size * depth_to_space_params->block_size) != 0){
                return DNN_ERROR;
            }
            cur_channels = cur_channels / (depth_to_space_params->block_size * depth_to_space_params->block_size);
            cur_height *= depth_to_space_params->block_size;
            cur_width *= depth_to_space_params->block_size;
            break;
        case MIRROR_PAD:
            pad_params = (LayerPadParams *)network->layers[layer].params;
            cur_height = cur_height + pad_params->paddings[1][0] + pad_params->paddings[1][1];
            cur_width = cur_width + pad_params->paddings[2][0] + pad_params->paddings[2][1];
            cur_channels = cur_channels + pad_params->paddings[3][0] + pad_params->paddings[3][1];
            break;
        default:
            return DNN_ERROR;
        }
        if (network->layers[layer].output){
            av_freep(&network->layers[layer].output);
        }

        if (cur_height <= 0 || cur_width <= 0)
            return DNN_ERROR;

        network->layers[layer].output = av_malloc(cur_height * cur_width * cur_channels * sizeof(float));
        if (!network->layers[layer].output){
            return DNN_ERROR;
        }
    }

    return DNN_SUCCESS;
}

// Loads model and its parameters that are stored in a binary file with following structure:
// layers_num,layer_type,layer_parameterss,layer_type,layer_parameters...
// For CONV layer: activation_function, input_num, output_num, kernel_size, kernel, biases
// For DEPTH_TO_SPACE layer: block_size
DNNModel *ff_dnn_load_model_native(const char *model_filename)
{
    DNNModel *model = NULL;
    ConvolutionalNetwork *network = NULL;
    AVIOContext *model_file_context;
    int file_size, dnn_size, kernel_size, i;
    int32_t layer;
    DNNLayerType layer_type;
    ConvolutionalParams *conv_params;
    DepthToSpaceParams *depth_to_space_params;
    LayerPadParams *pad_params;

    model = av_malloc(sizeof(DNNModel));
    if (!model){
        return NULL;
    }

    if (avio_open(&model_file_context, model_filename, AVIO_FLAG_READ) < 0){
        av_freep(&model);
        return NULL;
    }
    file_size = avio_size(model_file_context);

    network = av_malloc(sizeof(ConvolutionalNetwork));
    if (!network){
        avio_closep(&model_file_context);
        av_freep(&model);
        return NULL;
    }
    model->model = (void *)network;

    network->layers_num = 1 + (int32_t)avio_rl32(model_file_context);
    dnn_size = 4;

    network->layers = av_malloc(network->layers_num * sizeof(Layer));
    if (!network->layers){
        av_freep(&network);
        avio_closep(&model_file_context);
        av_freep(&model);
        return NULL;
    }

    for (layer = 0; layer < network->layers_num; ++layer){
        network->layers[layer].output = NULL;
        network->layers[layer].params = NULL;
    }
    network->layers[0].type = INPUT;
    network->layers[0].params = av_malloc(sizeof(InputParams));
    if (!network->layers[0].params){
        avio_closep(&model_file_context);
        ff_dnn_free_model_native(&model);
        return NULL;
    }

    for (layer = 1; layer < network->layers_num; ++layer){
        layer_type = (int32_t)avio_rl32(model_file_context);
        dnn_size += 4;
        switch (layer_type){
        case CONV:
            conv_params = av_malloc(sizeof(ConvolutionalParams));
            if (!conv_params){
                avio_closep(&model_file_context);
                ff_dnn_free_model_native(&model);
                return NULL;
            }
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
                avio_closep(&model_file_context);
                av_freep(&conv_params);
                ff_dnn_free_model_native(&model);
                return NULL;
            }
            conv_params->kernel = av_malloc(kernel_size * sizeof(float));
            conv_params->biases = av_malloc(conv_params->output_num * sizeof(float));
            if (!conv_params->kernel || !conv_params->biases){
                avio_closep(&model_file_context);
                av_freep(&conv_params->kernel);
                av_freep(&conv_params->biases);
                av_freep(&conv_params);
                ff_dnn_free_model_native(&model);
                return NULL;
            }
            for (i = 0; i < kernel_size; ++i){
                conv_params->kernel[i] = av_int2float(avio_rl32(model_file_context));
            }
            for (i = 0; i < conv_params->output_num; ++i){
                conv_params->biases[i] = av_int2float(avio_rl32(model_file_context));
            }
            network->layers[layer].type = CONV;
            network->layers[layer].params = conv_params;
            break;
        case DEPTH_TO_SPACE:
            depth_to_space_params = av_malloc(sizeof(DepthToSpaceParams));
            if (!depth_to_space_params){
                avio_closep(&model_file_context);
                ff_dnn_free_model_native(&model);
                return NULL;
            }
            depth_to_space_params->block_size = (int32_t)avio_rl32(model_file_context);
            dnn_size += 4;
            network->layers[layer].type = DEPTH_TO_SPACE;
            network->layers[layer].params = depth_to_space_params;
            break;
        case MIRROR_PAD:
            pad_params = av_malloc(sizeof(LayerPadParams));
            if (!pad_params){
                avio_closep(&model_file_context);
                ff_dnn_free_model_native(&model);
                return NULL;
            }
            pad_params->mode = (int32_t)avio_rl32(model_file_context);
            dnn_size += 4;
            for (i = 0; i < 4; ++i) {
                pad_params->paddings[i][0] = avio_rl32(model_file_context);
                pad_params->paddings[i][1] = avio_rl32(model_file_context);
                dnn_size += 8;
            }
            network->layers[layer].type = MIRROR_PAD;
            network->layers[layer].params = pad_params;
            break;
        default:
            avio_closep(&model_file_context);
            ff_dnn_free_model_native(&model);
            return NULL;
        }
    }

    avio_closep(&model_file_context);

    if (dnn_size != file_size){
        ff_dnn_free_model_native(&model);
        return NULL;
    }

    model->set_input_output = &set_input_output_native;

    return model;
}

#define CLAMP_TO_EDGE(x, w) ((x) < 0 ? 0 : ((x) >= (w) ? (w - 1) : (x)))

static void convolve(const float *input, float *output, const ConvolutionalParams *conv_params, int width, int height)
{
    int radius = conv_params->kernel_size >> 1;
    int src_linesize = width * conv_params->input_num;
    int filter_linesize = conv_params->kernel_size * conv_params->input_num;
    int filter_size = conv_params->kernel_size * filter_linesize;
    int pad_size = (conv_params->padding_method == VALID) ? (conv_params->kernel_size - 1) / 2 * conv_params->dilation : 0;

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
}

static void depth_to_space(const float *input, float *output, int block_size, int width, int height, int channels)
{
    int y, x, by, bx, ch;
    int new_channels = channels / (block_size * block_size);
    int output_linesize = width * channels;
    int by_linesize = output_linesize / block_size;
    int x_linesize = new_channels * block_size;

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
}

DNNReturnType ff_dnn_execute_model_native(const DNNModel *model, DNNData *outputs, uint32_t nb_output)
{
    ConvolutionalNetwork *network = (ConvolutionalNetwork *)model->model;
    int cur_width, cur_height, cur_channels;
    int32_t layer;
    InputParams *input_params;
    ConvolutionalParams *conv_params;
    DepthToSpaceParams *depth_to_space_params;
    LayerPadParams *pad_params;

    if (network->layers_num <= 0 || network->layers[0].type != INPUT || !network->layers[0].output){
        return DNN_ERROR;
    }
    else{
        input_params = (InputParams *)network->layers[0].params;
        cur_width = input_params->width;
        cur_height = input_params->height;
        cur_channels = input_params->channels;
    }

    for (layer = 1; layer < network->layers_num; ++layer){
        if (!network->layers[layer].output){
            return DNN_ERROR;
        }
        switch (network->layers[layer].type){
        case CONV:
            conv_params = (ConvolutionalParams *)network->layers[layer].params;
            convolve(network->layers[layer - 1].output, network->layers[layer].output, conv_params, cur_width, cur_height);
            cur_channels = conv_params->output_num;
            if (conv_params->padding_method == VALID) {
                int pad_size = (conv_params->kernel_size - 1) * conv_params->dilation;
                cur_height -= pad_size;
                cur_width -= pad_size;
            }
            break;
        case DEPTH_TO_SPACE:
            depth_to_space_params = (DepthToSpaceParams *)network->layers[layer].params;
            depth_to_space(network->layers[layer - 1].output, network->layers[layer].output,
                           depth_to_space_params->block_size, cur_width, cur_height, cur_channels);
            cur_height *= depth_to_space_params->block_size;
            cur_width *= depth_to_space_params->block_size;
            cur_channels /= depth_to_space_params->block_size * depth_to_space_params->block_size;
            break;
        case MIRROR_PAD:
            pad_params = (LayerPadParams *)network->layers[layer].params;
            dnn_execute_layer_pad(network->layers[layer - 1].output, network->layers[layer].output,
                                  pad_params, 1, cur_height, cur_width, cur_channels);
            cur_height = cur_height + pad_params->paddings[1][0] + pad_params->paddings[1][1];
            cur_width = cur_width + pad_params->paddings[2][0] + pad_params->paddings[2][1];
            cur_channels = cur_channels + pad_params->paddings[3][0] + pad_params->paddings[3][1];
            break;
        case INPUT:
            return DNN_ERROR;
        }
    }

    // native mode does not support multiple outputs yet
    if (nb_output > 1)
        return DNN_ERROR;
    outputs[0].data = network->layers[network->layers_num - 1].output;
    outputs[0].height = cur_height;
    outputs[0].width = cur_width;
    outputs[0].channels = cur_channels;

    return DNN_SUCCESS;
}

void ff_dnn_free_model_native(DNNModel **model)
{
    ConvolutionalNetwork *network;
    ConvolutionalParams *conv_params;
    int32_t layer;

    if (*model)
    {
        network = (ConvolutionalNetwork *)(*model)->model;
        for (layer = 0; layer < network->layers_num; ++layer){
            av_freep(&network->layers[layer].output);
            if (network->layers[layer].type == CONV){
                conv_params = (ConvolutionalParams *)network->layers[layer].params;
                av_freep(&conv_params->kernel);
                av_freep(&conv_params->biases);
            }
            av_freep(&network->layers[layer].params);
        }
        av_freep(&network->layers);
        av_freep(&network);
        av_freep(model);
    }
}
