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

    if (network->layers_num <= 0 || network->operands_num <= 0)
        return DNN_ERROR;

    av_assert0(input->dt == DNN_FLOAT);

    /**
     * as the first step, suppose network->operands[0] is the input operand.
     */
    network->operands[0].dims[0] = 1;
    network->operands[0].dims[1] = input->height;
    network->operands[0].dims[2] = input->width;
    network->operands[0].dims[3] = input->channels;
    network->operands[0].type = DOT_INPUT;
    network->operands[0].data_type = DNN_FLOAT;
    network->operands[0].isNHWC = 1;

    av_freep(&network->operands[0].data);
    network->operands[0].length = calculate_operand_data_length(&network->operands[0]);
    network->operands[0].data = av_malloc(network->operands[0].length);
    if (!network->operands[0].data)
        return DNN_ERROR;

    input->data = network->operands[0].data;
    return DNN_SUCCESS;
}

// Loads model and its parameters that are stored in a binary file with following structure:
// layers_num,layer_type,layer_parameterss,layer_type,layer_parameters...
// For CONV layer: activation_function, input_num, output_num, kernel_size, kernel, biases
// For DEPTH_TO_SPACE layer: block_size
DNNModel *ff_dnn_load_model_native(const char *model_filename)
{
    DNNModel *model = NULL;
    char header_expected[] = "FFMPEGDNNNATIVE";
    char *buf;
    size_t size;
    int version, header_size, major_version_expected = 0;
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

    /**
     * check file header with string and version
     */
    size = sizeof(header_expected);
    buf = av_malloc(size);
    if (!buf) {
        avio_closep(&model_file_context);
        av_freep(&model);
        return NULL;
    }

    // size - 1 to skip the ending '\0' which is not saved in file
    avio_get_str(model_file_context, size - 1, buf, size);
    dnn_size = size - 1;
    if (strncmp(buf, header_expected, size) != 0) {
        av_freep(&buf);
        avio_closep(&model_file_context);
        av_freep(&model);
        return NULL;
    }
    av_freep(&buf);

    version = (int32_t)avio_rl32(model_file_context);
    dnn_size += 4;
    if (version != major_version_expected) {
        avio_closep(&model_file_context);
        av_freep(&model);
        return NULL;
    }

    // currently no need to check minor version
    version = (int32_t)avio_rl32(model_file_context);
    dnn_size += 4;
    header_size = dnn_size;

    network = av_mallocz(sizeof(ConvolutionalNetwork));
    if (!network){
        avio_closep(&model_file_context);
        av_freep(&model);
        return NULL;
    }
    model->model = (void *)network;

    avio_seek(model_file_context, file_size - 8, SEEK_SET);
    network->layers_num = (int32_t)avio_rl32(model_file_context);
    network->operands_num = (int32_t)avio_rl32(model_file_context);
    dnn_size += 8;
    avio_seek(model_file_context, header_size, SEEK_SET);

    network->layers = av_mallocz(network->layers_num * sizeof(Layer));
    if (!network->layers){
        avio_closep(&model_file_context);
        ff_dnn_free_model_native(&model);
        return NULL;
    }

    network->operands = av_mallocz(network->operands_num * sizeof(DnnOperand));
    if (!network->operands){
        avio_closep(&model_file_context);
        ff_dnn_free_model_native(&model);
        return NULL;
    }

    for (layer = 0; layer < network->layers_num; ++layer){
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
            network->layers[layer].input_operand_indexes[0] = (int32_t)avio_rl32(model_file_context);
            network->layers[layer].output_operand_index = (int32_t)avio_rl32(model_file_context);
            dnn_size += 8;
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
            network->layers[layer].input_operand_indexes[0] = (int32_t)avio_rl32(model_file_context);
            network->layers[layer].output_operand_index = (int32_t)avio_rl32(model_file_context);
            dnn_size += 8;
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
            network->layers[layer].input_operand_indexes[0] = (int32_t)avio_rl32(model_file_context);
            network->layers[layer].output_operand_index = (int32_t)avio_rl32(model_file_context);
            dnn_size += 8;
            network->layers[layer].type = MIRROR_PAD;
            network->layers[layer].params = pad_params;
            break;
        default:
            avio_closep(&model_file_context);
            ff_dnn_free_model_native(&model);
            return NULL;
        }
    }

    for (int32_t i = 0; i < network->operands_num; ++i){
        DnnOperand *oprd;
        int32_t name_len;
        int32_t operand_index = (int32_t)avio_rl32(model_file_context);
        dnn_size += 4;

        oprd = &network->operands[operand_index];
        name_len = (int32_t)avio_rl32(model_file_context);
        dnn_size += 4;

        avio_get_str(model_file_context, name_len, oprd->name, sizeof(oprd->name));
        dnn_size += name_len;

        oprd->type = (int32_t)avio_rl32(model_file_context);
        dnn_size += 4;

        oprd->data_type = (int32_t)avio_rl32(model_file_context);
        dnn_size += 4;

        for (int32_t dim = 0; dim < 4; ++dim) {
            oprd->dims[dim] = (int32_t)avio_rl32(model_file_context);
            dnn_size += 4;
        }

        oprd->isNHWC = 1;
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

static int convolve(DnnOperand *operands, const int32_t *input_operand_indexes, int32_t output_operand_index, const ConvolutionalParams *conv_params)
{
    float *output;
    int32_t input_operand_index = input_operand_indexes[0];
    int number = operands[input_operand_index].dims[0];
    int height = operands[input_operand_index].dims[1];
    int width = operands[input_operand_index].dims[2];
    int channel = operands[input_operand_index].dims[3];
    const float *input = operands[input_operand_index].data;

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

static int depth_to_space(DnnOperand *operands, const int32_t *input_operand_indexes, int32_t output_operand_index, int block_size)
{
    float *output;
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
    output_operand->length = calculate_operand_data_length(output_operand);
    output_operand->data = av_realloc(output_operand->data, output_operand->length);
    if (!output_operand->data)
        return -1;
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

DNNReturnType ff_dnn_execute_model_native(const DNNModel *model, DNNData *outputs, uint32_t nb_output)
{
    ConvolutionalNetwork *network = (ConvolutionalNetwork *)model->model;
    int32_t layer;
    ConvolutionalParams *conv_params;
    DepthToSpaceParams *depth_to_space_params;
    LayerPadParams *pad_params;

    if (network->layers_num <= 0 || network->operands_num <= 0)
        return DNN_ERROR;
    if (!network->operands[0].data)
        return DNN_ERROR;

    for (layer = 0; layer < network->layers_num; ++layer){
        switch (network->layers[layer].type){
        case CONV:
            conv_params = (ConvolutionalParams *)network->layers[layer].params;
            convolve(network->operands, network->layers[layer].input_operand_indexes,
                     network->layers[layer].output_operand_index, conv_params);
            break;
        case DEPTH_TO_SPACE:
            depth_to_space_params = (DepthToSpaceParams *)network->layers[layer].params;
            depth_to_space(network->operands, network->layers[layer].input_operand_indexes,
                           network->layers[layer].output_operand_index, depth_to_space_params->block_size);
            break;
        case MIRROR_PAD:
            pad_params = (LayerPadParams *)network->layers[layer].params;
            dnn_execute_layer_pad(network->operands, network->layers[layer].input_operand_indexes,
                                  network->layers[layer].output_operand_index, pad_params);
            break;
        case INPUT:
            return DNN_ERROR;
        }
    }

    // native mode does not support multiple outputs yet
    if (nb_output > 1)
        return DNN_ERROR;

    /**
     * as the first step, suppose network->operands[network->operands_num - 1] is the output operand.
     */
    outputs[0].data = network->operands[network->operands_num - 1].data;
    outputs[0].height = network->operands[network->operands_num - 1].dims[1];
    outputs[0].width = network->operands[network->operands_num - 1].dims[2];
    outputs[0].channels = network->operands[network->operands_num - 1].dims[3];

    return DNN_SUCCESS;
}

int32_t calculate_operand_data_length(DnnOperand* operand)
{
    // currently, we just support DNN_FLOAT
    return operand->dims[0] * operand->dims[1] * operand->dims[2] * operand->dims[3] * sizeof(float);
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
            if (network->layers[layer].type == CONV){
                conv_params = (ConvolutionalParams *)network->layers[layer].params;
                av_freep(&conv_params->kernel);
                av_freep(&conv_params->biases);
            }
            av_freep(&network->layers[layer].params);
        }
        av_freep(&network->layers);

        for (uint32_t operand = 0; operand < network->operands_num; ++operand)
            av_freep(&network->operands[operand].data);
        av_freep(&network->operands);

        av_freep(&network);
        av_freep(model);
    }
}
