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
#include "dnn_io_proc.h"

#define OFFSET(x) offsetof(NativeContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM
static const AVOption dnn_native_options[] = {
    { "conv2d_threads", "threads num for conv2d layer", OFFSET(options.conv2d_threads), AV_OPT_TYPE_INT,  { .i64 = 0 }, INT_MIN, INT_MAX, FLAGS },
    { NULL },
};

const AVClass dnn_native_class = {
    .class_name = "dnn_native",
    .item_name  = av_default_item_name,
    .option     = dnn_native_options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_FILTER,
};

static DNNReturnType execute_model_native(const DNNModel *model, const char *input_name, AVFrame *in_frame,
                                          const char **output_names, uint32_t nb_output, AVFrame *out_frame,
                                          int do_ioproc);

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

static DNNReturnType get_output_native(void *model, const char *input_name, int input_width, int input_height,
                                       const char *output_name, int *output_width, int *output_height)
{
    DNNReturnType ret;
    NativeModel *native_model = (NativeModel *)model;
    NativeContext *ctx = &native_model->ctx;
    AVFrame *in_frame = av_frame_alloc();
    AVFrame *out_frame = NULL;

    if (!in_frame) {
        av_log(ctx, AV_LOG_ERROR, "Could not allocate memory for input frame\n");
        return DNN_ERROR;
    }

    out_frame = av_frame_alloc();

    if (!out_frame) {
        av_log(ctx, AV_LOG_ERROR, "Could not allocate memory for output frame\n");
        av_frame_free(&in_frame);
        return DNN_ERROR;
    }

    in_frame->width = input_width;
    in_frame->height = input_height;

    ret = execute_model_native(native_model->model, input_name, in_frame, &output_name, 1, out_frame, 0);
    *output_width = out_frame->width;
    *output_height = out_frame->height;

    av_frame_free(&out_frame);
    av_frame_free(&in_frame);
    return ret;
}

// Loads model and its parameters that are stored in a binary file with following structure:
// layers_num,layer_type,layer_parameterss,layer_type,layer_parameters...
// For CONV layer: activation_function, input_num, output_num, kernel_size, kernel, biases
// For DEPTH_TO_SPACE layer: block_size
DNNModel *ff_dnn_load_model_native(const char *model_filename, const char *options, AVFilterContext *filter_ctx)
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
    model->options = options;
    if (av_opt_set_from_string(&native_model->ctx, model->options, NULL, "=", "&") < 0)
        goto fail;
    model->model = (void *)native_model;
    native_model->model = model;

#if !HAVE_PTHREAD_CANCEL
    if (native_model->ctx.options.conv2d_threads > 1){
        av_log(&native_model->ctx, AV_LOG_WARNING, "'conv2d_threads' option was set but it is not supported "
                       "on this build (pthread support is required)\n");
    }
#endif

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

    model->get_input = &get_input_native;
    model->get_output = &get_output_native;
    model->filter_ctx = filter_ctx;

    return model;

fail:
    ff_dnn_free_model_native(&model);
    avio_closep(&model_file_context);
    return NULL;
}

static DNNReturnType execute_model_native(const DNNModel *model, const char *input_name, AVFrame *in_frame,
                                          const char **output_names, uint32_t nb_output, AVFrame *out_frame,
                                          int do_ioproc)
{
    NativeModel *native_model = (NativeModel *)model->model;
    NativeContext *ctx = &native_model->ctx;
    int32_t layer;
    DNNData input, output;
    DnnOperand *oprd = NULL;

    if (native_model->layers_num <= 0 || native_model->operands_num <= 0) {
        av_log(ctx, AV_LOG_ERROR, "No operands or layers in model\n");
        return DNN_ERROR;
    }

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

    oprd->dims[1] = in_frame->height;
    oprd->dims[2] = in_frame->width;

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

    input.height = oprd->dims[1];
    input.width = oprd->dims[2];
    input.channels = oprd->dims[3];
    input.data = oprd->data;
    input.dt = oprd->data_type;
    if (do_ioproc) {
        if (native_model->model->pre_proc != NULL) {
            native_model->model->pre_proc(in_frame, &input, native_model->model->filter_ctx);
        } else {
            proc_from_frame_to_dnn(in_frame, &input, ctx);
        }
    }

    if (nb_output != 1) {
        // currently, the filter does not need multiple outputs,
        // so we just pending the support until we really need it.
        av_log(ctx, AV_LOG_ERROR, "do not support multiple outputs\n");
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

        output.data = oprd->data;
        output.height = oprd->dims[1];
        output.width = oprd->dims[2];
        output.channels = oprd->dims[3];
        output.dt = oprd->data_type;

        if (do_ioproc) {
            if (native_model->model->post_proc != NULL) {
                native_model->model->post_proc(out_frame, &output, native_model->model->filter_ctx);
            } else {
                proc_from_dnn_to_frame(out_frame, &output, ctx);
            }
        } else {
            out_frame->width = output.width;
            out_frame->height = output.height;
        }
    }

    return DNN_SUCCESS;
}

DNNReturnType ff_dnn_execute_model_native(const DNNModel *model, const char *input_name, AVFrame *in_frame,
                                          const char **output_names, uint32_t nb_output, AVFrame *out_frame)
{
    NativeModel *native_model = (NativeModel *)model->model;
    NativeContext *ctx = &native_model->ctx;

    if (!in_frame) {
        av_log(ctx, AV_LOG_ERROR, "in frame is NULL when execute model.\n");
        return DNN_ERROR;
    }

    if (!out_frame) {
        av_log(ctx, AV_LOG_ERROR, "out frame is NULL when execute model.\n");
        return DNN_ERROR;
    }

    return execute_model_native(model, input_name, in_frame, output_names, nb_output, out_frame, 1);
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
