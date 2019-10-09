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
#include "dnn_backend_native_layer_conv2d.h"
#include "dnn_backend_native_layers.h"

static DNNReturnType set_input_output_native(void *model, DNNInputData *input, const char *input_name, const char **output_names, uint32_t nb_output)
{
    ConvolutionalNetwork *network = (ConvolutionalNetwork *)model;
    DnnOperand *oprd = NULL;

    if (network->layers_num <= 0 || network->operands_num <= 0)
        return DNN_ERROR;

    /* inputs */
    av_assert0(input->dt == DNN_FLOAT);
    for (int i = 0; i < network->operands_num; ++i) {
        oprd = &network->operands[i];
        if (strcmp(oprd->name, input_name) == 0) {
            if (oprd->type != DOT_INPUT)
                return DNN_ERROR;
            break;
        }
        oprd = NULL;
    }

    if (!oprd)
        return DNN_ERROR;

    oprd->dims[0] = 1;
    oprd->dims[1] = input->height;
    oprd->dims[2] = input->width;
    oprd->dims[3] = input->channels;

    av_freep(&oprd->data);
    oprd->length = calculate_operand_data_length(oprd);
    oprd->data = av_malloc(oprd->length);
    if (!oprd->data)
        return DNN_ERROR;

    input->data = oprd->data;

    /* outputs */
    network->nb_output = 0;
    av_freep(&network->output_indexes);
    network->output_indexes = av_mallocz_array(nb_output, sizeof(*network->output_indexes));
    if (!network->output_indexes)
        return DNN_ERROR;

    for (uint32_t i = 0; i < nb_output; ++i) {
        const char *output_name = output_names[i];
        for (int j = 0; j < network->operands_num; ++j) {
            oprd = &network->operands[j];
            if (strcmp(oprd->name, output_name) == 0) {
                network->output_indexes[network->nb_output++] = j;
                break;
            }
        }
    }

    if (network->nb_output != nb_output)
        return DNN_ERROR;

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
    int file_size, dnn_size, parsed_size;
    int32_t layer;
    DNNLayerType layer_type;

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

        if (layer_type >= DLT_COUNT) {
            avio_closep(&model_file_context);
            ff_dnn_free_model_native(&model);
            return NULL;
        }

        network->layers[layer].type = layer_type;
        parsed_size = layer_funcs[layer_type].pf_load(&network->layers[layer], model_file_context, file_size);
        if (!parsed_size) {
            avio_closep(&model_file_context);
            ff_dnn_free_model_native(&model);
            return NULL;
        }
        dnn_size += parsed_size;
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

DNNReturnType ff_dnn_execute_model_native(const DNNModel *model, DNNData *outputs, uint32_t nb_output)
{
    ConvolutionalNetwork *network = (ConvolutionalNetwork *)model->model;
    int32_t layer;
    uint32_t nb = FFMIN(nb_output, network->nb_output);

    if (network->layers_num <= 0 || network->operands_num <= 0)
        return DNN_ERROR;
    if (!network->operands[0].data)
        return DNN_ERROR;

    for (layer = 0; layer < network->layers_num; ++layer){
        DNNLayerType layer_type = network->layers[layer].type;
        layer_funcs[layer_type].pf_exec(network->operands,
                                  network->layers[layer].input_operand_indexes,
                                  network->layers[layer].output_operand_index,
                                  network->layers[layer].params);
    }

    for (uint32_t i = 0; i < nb; ++i) {
        DnnOperand *oprd = &network->operands[network->output_indexes[i]];
        outputs[i].data = oprd->data;
        outputs[i].height = oprd->dims[1];
        outputs[i].width = oprd->dims[2];
        outputs[i].channels = oprd->dims[3];
    }

    return DNN_SUCCESS;
}

int32_t calculate_operand_dims_count(const DnnOperand *oprd)
{
    int32_t result = 1;
    for (int i = 0; i < 4; ++i)
        result *= oprd->dims[i];

    return result;
}

int32_t calculate_operand_data_length(const DnnOperand* oprd)
{
    // currently, we just support DNN_FLOAT
    return oprd->dims[0] * oprd->dims[1] * oprd->dims[2] * oprd->dims[3] * sizeof(float);
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
            if (network->layers[layer].type == DLT_CONV2D){
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

        av_freep(&network->output_indexes);
        av_freep(&network);
        av_freep(model);
    }
}
