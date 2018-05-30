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
#include "dnn_srcnn.h"
#include "libavformat/avio.h"

typedef enum {INPUT, CONV} LayerType;

typedef struct Layer{
    LayerType type;
    float* output;
    void* params;
} Layer;

typedef struct ConvolutionalParams{
    int32_t input_num, output_num, kernel_size;
    float* kernel;
    float* biases;
} ConvolutionalParams;

typedef struct InputParams{
    int height, width, channels;
} InputParams;

// Represents simple feed-forward convolutional network.
typedef struct ConvolutionalNetwork{
    Layer* layers;
    int32_t layers_num;
} ConvolutionalNetwork;

static DNNReturnType set_input_output_native(void* model, const DNNData* input, const DNNData* output)
{
    ConvolutionalNetwork* network = (ConvolutionalNetwork*)model;
    InputParams* input_params;
    ConvolutionalParams* conv_params;
    int cur_width, cur_height, cur_channels;
    int32_t layer;

    if (network->layers_num <= 0 || network->layers[0].type != INPUT){
        return DNN_ERROR;
    }
    else{
        network->layers[0].output = input->data;
        input_params = (InputParams*)network->layers[0].params;
        input_params->width = cur_width = input->width;
        input_params->height = cur_height = input->height;
        input_params->channels = cur_channels = input->channels;
    }

    for (layer = 1; layer < network->layers_num; ++layer){
        switch (network->layers[layer].type){
        case CONV:
            conv_params = (ConvolutionalParams*)network->layers[layer].params;
            if (conv_params->input_num != cur_channels){
                return DNN_ERROR;
            }
            cur_channels = conv_params->output_num;
            if (layer < network->layers_num - 1){
                if (!network->layers[layer].output){
                    av_freep(&network->layers[layer].output);
                }
                network->layers[layer].output = av_malloc(cur_height * cur_width * cur_channels * sizeof(float));
                if (!network->layers[layer].output){
                    return DNN_ERROR;
                }
            }
            else{
                network->layers[layer].output = output->data;
                if (output->width != cur_width || output->height != cur_height || output->channels != cur_channels){
                    return DNN_ERROR;
                }
            }
            break;
        default:
            return DNN_ERROR;
        }
    }

    return DNN_SUCCESS;
}

// Loads model and its parameters that are stored in a binary file with following structure:
// layers_num,conv_input_num,conv_output_num,conv_kernel_size,conv_kernel,conv_biases,conv_input_num...
DNNModel* ff_dnn_load_model_native(const char* model_filename)
{
    DNNModel* model = NULL;
    ConvolutionalNetwork* network = NULL;
    AVIOContext* model_file_context;
    int file_size, dnn_size, kernel_size, i;
    int32_t layer;
    ConvolutionalParams* conv_params;

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
    model->model = (void*)network;

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
        conv_params = av_malloc(sizeof(ConvolutionalParams));
        if (!conv_params){
            avio_closep(&model_file_context);
            ff_dnn_free_model_native(&model);
            return NULL;
        }
        conv_params->input_num = (int32_t)avio_rl32(model_file_context);
        conv_params->output_num = (int32_t)avio_rl32(model_file_context);
        conv_params->kernel_size = (int32_t)avio_rl32(model_file_context);
        kernel_size = conv_params->input_num * conv_params->output_num *
                      conv_params->kernel_size * conv_params->kernel_size;
        dnn_size += 12 + (kernel_size + conv_params->output_num << 2);
        if (dnn_size > file_size || conv_params->input_num <= 0 ||
            conv_params->output_num <= 0 || conv_params->kernel_size <= 0){
            avio_closep(&model_file_context);
            ff_dnn_free_model_native(&model);
            return NULL;
        }
        conv_params->kernel = av_malloc(kernel_size * sizeof(float));
        conv_params->biases = av_malloc(conv_params->output_num * sizeof(float));
        if (!conv_params->kernel || !conv_params->biases){
            avio_closep(&model_file_context);
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
    }

    avio_closep(&model_file_context);

    if (dnn_size != file_size){
        ff_dnn_free_model_native(&model);
        return NULL;
    }

    model->set_input_output = &set_input_output_native;

    return model;
}

static int set_up_conv_layer(Layer* layer, const float* kernel, const float* biases, int32_t input_num, int32_t output_num, int32_t size)
{
    ConvolutionalParams* conv_params;
    int kernel_size;

    conv_params = av_malloc(sizeof(ConvolutionalParams));
    if (!conv_params){
        return DNN_ERROR;
    }
    conv_params->input_num = input_num;
    conv_params->output_num = output_num;
    conv_params->kernel_size = size;
    kernel_size = input_num * output_num * size * size;
    conv_params->kernel = av_malloc(kernel_size * sizeof(float));
    conv_params->biases = av_malloc(conv_params->output_num * sizeof(float));
    if (!conv_params->kernel || !conv_params->biases){
        av_freep(&conv_params->kernel);
        av_freep(&conv_params->biases);
        av_freep(&conv_params);
        return DNN_ERROR;
    }
    memcpy(conv_params->kernel, kernel, kernel_size * sizeof(float));
    memcpy(conv_params->biases, biases, output_num * sizeof(float));
    layer->type = CONV;
    layer->params = conv_params;

    return DNN_SUCCESS;
}

DNNModel* ff_dnn_load_default_model_native(DNNDefaultModel model_type)
{
    DNNModel* model = NULL;
    ConvolutionalNetwork* network = NULL;
    int32_t layer;

    model = av_malloc(sizeof(DNNModel));
    if (!model){
        return NULL;
    }

    network = av_malloc(sizeof(ConvolutionalNetwork));
    if (!network){
        av_freep(&model);
        return NULL;
    }
    model->model = (void*)network;

    switch (model_type){
    case DNN_SRCNN:
        network->layers_num = 4;

        network->layers = av_malloc(network->layers_num * sizeof(Layer));
        if (!network->layers){
            av_freep(&network);
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
            ff_dnn_free_model_native(&model);
            return NULL;
        }

        if (set_up_conv_layer(network->layers + 1, conv1_kernel, conv1_biases, 1, 64, 9) != DNN_SUCCESS ||
            set_up_conv_layer(network->layers + 2, conv2_kernel, conv2_biases, 64, 32, 1) != DNN_SUCCESS ||
            set_up_conv_layer(network->layers + 3, conv3_kernel, conv3_biases, 32, 1, 5) != DNN_SUCCESS){
            ff_dnn_free_model_native(&model);
            return NULL;
        }

        model->set_input_output = &set_input_output_native;

        return model;
    default:
        av_freep(&network);
        av_freep(&model);
        return NULL;
    }
}

#define CLAMP_TO_EDGE(x, w) ((x) < 0 ? 0 : ((x) >= (w) ? (w - 1) : (x)))

static void convolve(const float* input, float* output, const ConvolutionalParams* conv_params, int32_t width, int32_t height)
{
    int y, x, n_filter, ch, kernel_y, kernel_x;
    int radius = conv_params->kernel_size >> 1;
    int src_linesize = width * conv_params->input_num;
    int filter_linesize = conv_params->kernel_size * conv_params->input_num;
    int filter_size = conv_params->kernel_size * filter_linesize;

    for (y = 0; y < height; ++y){
        for (x = 0; x < width; ++x){
            for (n_filter = 0; n_filter < conv_params->output_num; ++n_filter){
                output[n_filter] = conv_params->biases[n_filter];
                for (ch = 0; ch < conv_params->input_num; ++ch){
                    for (kernel_y = 0; kernel_y < conv_params->kernel_size; ++kernel_y){
                        for (kernel_x = 0; kernel_x < conv_params->kernel_size; ++kernel_x){
                            output[n_filter] += input[CLAMP_TO_EDGE(y + kernel_y - radius, height) * src_linesize +
                                                      CLAMP_TO_EDGE(x + kernel_x - radius, width) * conv_params->input_num + ch] *
                                                conv_params->kernel[n_filter * filter_size + kernel_y * filter_linesize +
                                                                    kernel_x * conv_params->input_num + ch];
                        }
                    }
                }
                output[n_filter] = FFMAX(output[n_filter], 0.0);
            }
            output += conv_params->output_num;
        }
    }
}

DNNReturnType ff_dnn_execute_model_native(const DNNModel* model)
{
    ConvolutionalNetwork* network = (ConvolutionalNetwork*)model->model;
    InputParams* input_params;
    int cur_width, cur_height;
    int32_t layer;

    if (network->layers_num <= 0 || network->layers[0].type != INPUT || !network->layers[0].output){
        return DNN_ERROR;
    }
    else{
        input_params = (InputParams*)network->layers[0].params;
        cur_width = input_params->width;
        cur_height = input_params->height;
    }

    for (layer = 1; layer < network->layers_num; ++layer){
        if (!network->layers[layer].output){
            return DNN_ERROR;
        }
        switch (network->layers[layer].type){
        case CONV:
            convolve(network->layers[layer - 1].output, network->layers[layer].output, (ConvolutionalParams*)network->layers[layer].params, cur_width, cur_height);
            break;
        case INPUT:
            return DNN_ERROR;
        }
    }

    return DNN_SUCCESS;
}

void ff_dnn_free_model_native(DNNModel** model)
{
    ConvolutionalNetwork* network;
    ConvolutionalParams* conv_params;
    int32_t layer;

    if (*model)
    {
        network = (ConvolutionalNetwork*)(*model)->model;
        for (layer = 0; layer < network->layers_num; ++layer){
            switch (network->layers[layer].type){
            case CONV:
                if (layer < network->layers_num - 1){
                    av_freep(&network->layers[layer].output);
                }
                conv_params = (ConvolutionalParams*)network->layers[layer].params;
                av_freep(&conv_params->kernel);
                av_freep(&conv_params->biases);
                av_freep(&conv_params);
                break;
            case INPUT:
                av_freep(&network->layers[layer].params);
            }
        }
        av_freep(network);
        av_freep(model);
    }
}
