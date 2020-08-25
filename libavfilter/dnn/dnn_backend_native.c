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

static const AVClass dnn_native_class = {
    .class_name = "dnn_native",
    .item_name  = av_default_item_name,
    .option     = NULL,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_FILTER,
};

static DNNReturnType get_input_native(void *model, DNNData *input, const char *input_name)
{
    NativeModel *native_model = (NativeModel *)model;
    NativeContext *ctx = &native_model->ctx;

    for (int i = 0; i < native_model->operands_num; ++i) {
        DnnOperand *oprd = &native_model->operands[i];
        if (strcmp(oprd->name, input_name) == 0) {
            if (oprd->type != DOT_INPUT) {
                av_log(ctx, AV_LOG_ERROR, "Found \"%s\" in model, but it is not input node\n", input_name);
                return DNN_ERROR;
            }
            input->dt = oprd->data_type;
            av_assert0(oprd->dims[0] == 1);
            input->height = oprd->dims[1];
            input->width = oprd->dims[2];
            input->channels = oprd->dims[3];
            return DNN_SUCCESS;
        }
    }

    // do not find the input operand
    av_log(ctx, AV_LOG_ERROR, "Could not find \"%s\" in model\n", input_name);
    return DNN_ERROR;
}

static DNNReturnType set_input_native(void *model, DNNData *input, const char *input_name)
{
    NativeModel *native_model = (NativeModel *)model;
    NativeContext *ctx = &native_model->ctx;
    DnnOperand *oprd = NULL;

    if (native_model->layers_num <= 0 || native_model->operands_num <= 0) {
        av_log(ctx, AV_LOG_ERROR, "No operands or layers in model\n");
        return DNN_ERROR;
    }

    /* inputs */
    for (int i = 0; i < native_model->operands_num; ++i) {
        oprd = &native_model->operands[i];
        if (strcmp(oprd->name, input_name) == 0) {
            if (oprd->type != DOT_INPUT) {
                av_log(ctx, AV_LOG_ERROR, "Found \"%s\" in model, but it is not input node\n", input_name);
                return DNN_ERROR;
            }
            break;
        }
        oprd = NULL;
    }
    if (!oprd) {
        av_log(ctx, AV_LOG_ERROR, "Could not find \"%s\" in model\n", input_name);
        return DNN_ERROR;
    }

    oprd->dims[0] = 1;
    oprd->dims[1] = input->height;
    oprd->dims[2] = input->width;
    oprd->dims[3] = input->channels;

    av_freep(&oprd->data);
    oprd->length = calculate_operand_data_length(oprd);
    if (oprd->length <= 0) {
        av_log(ctx, AV_LOG_ERROR, "The input data length overflow\n");
        return DNN_ERROR;
    }
    oprd->data = av_malloc(oprd->length);
    if (!oprd->data) {
        av_log(ctx, AV_LOG_ERROR, "Failed to malloc memory for input data\n");
        return DNN_ERROR;
    }

    input->data = oprd->data;

    return DNN_SUCCESS;
}

// Loads model and its parameters that are stored in a binary file with following structure:
// layers_num,layer_type,layer_parameterss,layer_type,layer_parameters...
// For CONV layer: activation_function, input_num, output_num, kernel_size, kernel, biases
// For DEPTH_TO_SPACE layer: block_size
DNNModel *ff_dnn_load_model_native(const char *model_filename, const char *options)
{
    DNNModel *model = NULL;
    char header_expected[] = "FFMPEGDNNNATIVE";
    char *buf;
    size_t size;
    int version, header_size, major_version_expected = 1;
    NativeModel *native_model = NULL;
    AVIOContext *model_file_context;
    int file_size, dnn_size, parsed_size;
    int32_t layer;
    DNNLayerType layer_type;

    if (avio_open(&model_file_context, model_filename, AVIO_FLAG_READ) < 0){
        return NULL;
    }
    file_size = avio_size(model_file_context);

    model = av_mallocz(sizeof(DNNModel));
    if (!model){
        goto fail;
    }

    /**
     * check file header with string and version
     */
    size = sizeof(header_expected);
    buf = av_malloc(size);
    if (!buf) {
        goto fail;
    }

    // size - 1 to skip the ending '\0' which is not saved in file
    avio_get_str(model_file_context, size - 1, buf, size);
    dnn_size = size - 1;
    if (strncmp(buf, header_expected, size) != 0) {
        av_freep(&buf);
        goto fail;
    }
    av_freep(&buf);

    version = (int32_t)avio_rl32(model_file_context);
    dnn_size += 4;
    if (version != major_version_expected) {
        goto fail;
    }

    // currently no need to check minor version
    version = (int32_t)avio_rl32(model_file_context);
    dnn_size += 4;
    header_size = dnn_size;

    native_model = av_mallocz(sizeof(NativeModel));
    if (!native_model){
        goto fail;
    }

    native_model->ctx.class = &dnn_native_class;
    model->model = (void *)native_model;

    avio_seek(model_file_context, file_size - 8, SEEK_SET);
    native_model->layers_num = (int32_t)avio_rl32(model_file_context);
    native_model->operands_num = (int32_t)avio_rl32(model_file_context);
    dnn_size += 8;
    avio_seek(model_file_context, header_size, SEEK_SET);

    native_model->layers = av_mallocz(native_model->layers_num * sizeof(Layer));
    if (!native_model->layers){
        goto fail;
    }

    native_model->operands = av_mallocz(native_model->operands_num * sizeof(DnnOperand));
    if (!native_model->operands){
        goto fail;
    }

    for (layer = 0; layer < native_model->layers_num; ++layer){
        layer_type = (int32_t)avio_rl32(model_file_context);
        dnn_size += 4;

        if (layer_type >= DLT_COUNT) {
            goto fail;
        }

        native_model->layers[layer].type = layer_type;
        parsed_size = layer_funcs[layer_type].pf_load(&native_model->layers[layer], model_file_context, file_size, native_model->operands_num);
        if (!parsed_size) {
            goto fail;
        }
        dnn_size += parsed_size;
    }

    for (int32_t i = 0; i < native_model->operands_num; ++i){
        DnnOperand *oprd;
        int32_t name_len;
        int32_t operand_index = (int32_t)avio_rl32(model_file_context);
        dnn_size += 4;

        if (operand_index >= native_model->operands_num) {
            goto fail;
        }

        oprd = &native_model->operands[operand_index];
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

    model->set_input = &set_input_native;
    model->get_input = &get_input_native;
    model->options = options;

    return model;

fail:
    ff_dnn_free_model_native(&model);
    avio_closep(&model_file_context);
    return NULL;
}

DNNReturnType ff_dnn_execute_model_native(const DNNModel *model, DNNData *outputs, const char **output_names, uint32_t nb_output)
{
    NativeModel *native_model = (NativeModel *)model->model;
    NativeContext *ctx = &native_model->ctx;
    int32_t layer;

    if (native_model->layers_num <= 0 || native_model->operands_num <= 0) {
        av_log(ctx, AV_LOG_ERROR, "No operands or layers in model\n");
        return DNN_ERROR;
    }
    if (!native_model->operands[0].data) {
        av_log(ctx, AV_LOG_ERROR, "Empty model input data\n");
        return DNN_ERROR;
    }

    for (layer = 0; layer < native_model->layers_num; ++layer){
        DNNLayerType layer_type = native_model->layers[layer].type;
        if (layer_funcs[layer_type].pf_exec(native_model->operands,
                                            native_model->layers[layer].input_operand_indexes,
                                            native_model->layers[layer].output_operand_index,
                                            native_model->layers[layer].params,
                                            &native_model->ctx) == DNN_ERROR) {
            av_log(ctx, AV_LOG_ERROR, "Failed to execuet model\n");
            return DNN_ERROR;
        }
    }

    for (uint32_t i = 0; i < nb_output; ++i) {
        DnnOperand *oprd = NULL;
        const char *output_name = output_names[i];
        for (int j = 0; j < native_model->operands_num; ++j) {
            if (strcmp(native_model->operands[j].name, output_name) == 0) {
                oprd = &native_model->operands[j];
                break;
            }
        }

        if (oprd == NULL) {
            av_log(ctx, AV_LOG_ERROR, "Could not find output in model\n");
            return DNN_ERROR;
        }

        outputs[i].data = oprd->data;
        outputs[i].height = oprd->dims[1];
        outputs[i].width = oprd->dims[2];
        outputs[i].channels = oprd->dims[3];
        outputs[i].dt = oprd->data_type;
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
    uint64_t len = sizeof(float);
    for (int i = 0; i < 4; i++) {
        len *= oprd->dims[i];
        if (len > INT32_MAX)
            return 0;
    }
    return len;
}

void ff_dnn_free_model_native(DNNModel **model)
{
    NativeModel *native_model;
    ConvolutionalParams *conv_params;
    int32_t layer;

    if (*model)
    {
        if ((*model)->model) {
            native_model = (NativeModel *)(*model)->model;
            if (native_model->layers) {
                for (layer = 0; layer < native_model->layers_num; ++layer){
                    if (native_model->layers[layer].type == DLT_CONV2D){
                        conv_params = (ConvolutionalParams *)native_model->layers[layer].params;
                        av_freep(&conv_params->kernel);
                        av_freep(&conv_params->biases);
                    }
                    av_freep(&native_model->layers[layer].params);
                }
                av_freep(&native_model->layers);
            }

            if (native_model->operands) {
                for (uint32_t operand = 0; operand < native_model->operands_num; ++operand)
                    av_freep(&native_model->operands[operand].data);
                av_freep(&native_model->operands);
            }

            av_freep(&native_model);
        }
        av_freep(model);
    }
}
