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
#include "dnn_backend_common.h"

#define OFFSET(x) offsetof(NativeContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM
static const AVOption dnn_native_options[] = {
    { "conv2d_threads", "threads num for conv2d layer", OFFSET(options.conv2d_threads), AV_OPT_TYPE_INT,  { .i64 = 0 }, INT_MIN, INT_MAX, FLAGS },
    { "async",          "use DNN async inference",      OFFSET(options.async),          AV_OPT_TYPE_BOOL, { .i64 = 0 },       0,       1, FLAGS },
    { NULL },
};

static const AVClass dnn_native_class = {
    .class_name = "dnn_native",
    .item_name  = av_default_item_name,
    .option     = dnn_native_options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_FILTER,
};

static DNNReturnType execute_model_native(Queue *lltask_queue);

static DNNReturnType extract_lltask_from_task(TaskItem *task, Queue *lltask_queue)
{
    NativeModel *native_model = task->model;
    NativeContext *ctx = &native_model->ctx;
    LastLevelTaskItem *lltask = av_malloc(sizeof(*lltask));

    if (!lltask) {
        av_log(ctx, AV_LOG_ERROR, "Unable to allocate space for LastLevelTaskItem\n");
        return DNN_ERROR;
    }
    task->inference_todo = 1;
    task->inference_done = 0;
    lltask->task = task;

    if (ff_queue_push_back(lltask_queue, lltask) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to push back lltask_queue.\n");
        av_freep(&lltask);
        return DNN_ERROR;
    }
    return DNN_SUCCESS;
}

static DNNReturnType get_input_native(void *model, DNNData *input, const char *input_name)
{
    NativeModel *native_model = model;
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
    DNNReturnType ret = 0;
    NativeModel *native_model = model;
    NativeContext *ctx = &native_model->ctx;
    TaskItem task;
    DNNExecBaseParams exec_params = {
        .input_name     = input_name,
        .output_names   = &output_name,
        .nb_output      = 1,
        .in_frame       = NULL,
        .out_frame      = NULL,
    };

    if (ff_dnn_fill_gettingoutput_task(&task, &exec_params, native_model, input_height, input_width, ctx) != DNN_SUCCESS) {
        ret = DNN_ERROR;
        goto err;
    }

    if (extract_lltask_from_task(&task, native_model->lltask_queue) != DNN_SUCCESS) {
        av_log(ctx, AV_LOG_ERROR, "unable to extract last level task from task.\n");
        ret = DNN_ERROR;
        goto err;
    }

    ret = execute_model_native(native_model->lltask_queue);
    *output_width = task.out_frame->width;
    *output_height = task.out_frame->height;

err:
    av_frame_free(&task.out_frame);
    av_frame_free(&task.in_frame);
    return ret;
}

// Loads model and its parameters that are stored in a binary file with following structure:
// layers_num,layer_type,layer_parameterss,layer_type,layer_parameters...
// For CONV layer: activation_function, input_num, output_num, kernel_size, kernel, biases
// For DEPTH_TO_SPACE layer: block_size
DNNModel *ff_dnn_load_model_native(const char *model_filename, DNNFunctionType func_type, const char *options, AVFilterContext *filter_ctx)
{
#define DNN_NATIVE_MAGIC "FFMPEGDNNNATIVE"
    DNNModel *model = NULL;
    // sizeof - 1 to skip the terminating '\0' which is not written in the file
    char buf[sizeof(DNN_NATIVE_MAGIC) - 1];
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
    if (avio_read(model_file_context, buf, sizeof(buf)) != sizeof(buf) ||
        memcmp(buf, DNN_NATIVE_MAGIC, sizeof(buf)))
        goto fail;
    dnn_size = sizeof(buf);

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
    model->model = native_model;

    native_model->ctx.class = &dnn_native_class;
    model->options = options;
    if (av_opt_set_from_string(&native_model->ctx, model->options, NULL, "=", "&") < 0)
        goto fail;
    native_model->model = model;

    if (native_model->ctx.options.async) {
        av_log(&native_model->ctx, AV_LOG_WARNING, "Async not supported. Rolling back to sync\n");
        native_model->ctx.options.async = 0;
    }

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

    native_model->task_queue = ff_queue_create();
    if (!native_model->task_queue) {
        goto fail;
    }

    native_model->lltask_queue = ff_queue_create();
    if (!native_model->lltask_queue) {
        goto fail;
    }

    for (layer = 0; layer < native_model->layers_num; ++layer){
        layer_type = (int32_t)avio_rl32(model_file_context);
        dnn_size += 4;

        if (layer_type >= DLT_COUNT) {
            goto fail;
        }

        native_model->layers[layer].type = layer_type;
        parsed_size = ff_layer_funcs[layer_type].pf_load(&native_model->layers[layer], model_file_context, file_size, native_model->operands_num);
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
        if (oprd->type == DOT_INPUT && oprd->dims[0] != 1)
            goto fail;

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
    model->func_type = func_type;

    return model;

fail:
    ff_dnn_free_model_native(&model);
    avio_closep(&model_file_context);
    return NULL;
}

static DNNReturnType execute_model_native(Queue *lltask_queue)
{
    NativeModel *native_model = NULL;
    NativeContext *ctx = NULL;
    int32_t layer;
    DNNData input, output;
    DnnOperand *oprd = NULL;
    LastLevelTaskItem *lltask = NULL;
    TaskItem *task = NULL;
    DNNReturnType ret = 0;

    lltask = ff_queue_pop_front(lltask_queue);
    if (!lltask) {
        av_log(NULL, AV_LOG_ERROR, "Failed to get LastLevelTaskItem\n");
        ret = DNN_ERROR;
        goto err;
    }
    task = lltask->task;
    native_model = task->model;
    ctx = &native_model->ctx;

    if (native_model->layers_num <= 0 || native_model->operands_num <= 0) {
        av_log(ctx, AV_LOG_ERROR, "No operands or layers in model\n");
        ret = DNN_ERROR;
        goto err;
    }

    for (int i = 0; i < native_model->operands_num; ++i) {
        oprd = &native_model->operands[i];
        if (strcmp(oprd->name, task->input_name) == 0) {
            if (oprd->type != DOT_INPUT) {
                av_log(ctx, AV_LOG_ERROR, "Found \"%s\" in model, but it is not input node\n", task->input_name);
                ret = DNN_ERROR;
                goto err;
            }
            break;
        }
        oprd = NULL;
    }
    if (!oprd) {
        av_log(ctx, AV_LOG_ERROR, "Could not find \"%s\" in model\n", task->input_name);
        ret = DNN_ERROR;
        goto err;
    }

    oprd->dims[1] = task->in_frame->height;
    oprd->dims[2] = task->in_frame->width;

    av_freep(&oprd->data);
    oprd->length = ff_calculate_operand_data_length(oprd);
    if (oprd->length <= 0) {
        av_log(ctx, AV_LOG_ERROR, "The input data length overflow\n");
        ret = DNN_ERROR;
        goto err;
    }
    oprd->data = av_malloc(oprd->length);
    if (!oprd->data) {
        av_log(ctx, AV_LOG_ERROR, "Failed to malloc memory for input data\n");
        ret = DNN_ERROR;
        goto err;
    }

    input.height = oprd->dims[1];
    input.width = oprd->dims[2];
    input.channels = oprd->dims[3];
    input.data = oprd->data;
    input.dt = oprd->data_type;
    if (task->do_ioproc) {
        if (native_model->model->frame_pre_proc != NULL) {
            native_model->model->frame_pre_proc(task->in_frame, &input, native_model->model->filter_ctx);
        } else {
            ff_proc_from_frame_to_dnn(task->in_frame, &input, ctx);
        }
    }

    if (task->nb_output != 1) {
        // currently, the filter does not need multiple outputs,
        // so we just pending the support until we really need it.
        avpriv_report_missing_feature(ctx, "multiple outputs");
        ret = DNN_ERROR;
        goto err;
    }

    for (layer = 0; layer < native_model->layers_num; ++layer){
        DNNLayerType layer_type = native_model->layers[layer].type;
        if (ff_layer_funcs[layer_type].pf_exec(native_model->operands,
                                            native_model->layers[layer].input_operand_indexes,
                                            native_model->layers[layer].output_operand_index,
                                            native_model->layers[layer].params,
                                            &native_model->ctx) == DNN_ERROR) {
            av_log(ctx, AV_LOG_ERROR, "Failed to execute model\n");
            ret = DNN_ERROR;
            goto err;
        }
    }

    for (uint32_t i = 0; i < task->nb_output; ++i) {
        DnnOperand *oprd = NULL;
        const char *output_name = task->output_names[i];
        for (int j = 0; j < native_model->operands_num; ++j) {
            if (strcmp(native_model->operands[j].name, output_name) == 0) {
                oprd = &native_model->operands[j];
                break;
            }
        }

        if (oprd == NULL) {
            av_log(ctx, AV_LOG_ERROR, "Could not find output in model\n");
            ret = DNN_ERROR;
            goto err;
        }

        output.data = oprd->data;
        output.height = oprd->dims[1];
        output.width = oprd->dims[2];
        output.channels = oprd->dims[3];
        output.dt = oprd->data_type;

        if (task->do_ioproc) {
            if (native_model->model->frame_post_proc != NULL) {
                native_model->model->frame_post_proc(task->out_frame, &output, native_model->model->filter_ctx);
            } else {
                ff_proc_from_dnn_to_frame(task->out_frame, &output, ctx);
            }
        } else {
            task->out_frame->width = output.width;
            task->out_frame->height = output.height;
        }
    }
    task->inference_done++;
err:
    av_freep(&lltask);
    return ret;
}

DNNReturnType ff_dnn_execute_model_native(const DNNModel *model, DNNExecBaseParams *exec_params)
{
    NativeModel *native_model = model->model;
    NativeContext *ctx = &native_model->ctx;
    TaskItem *task;

    if (ff_check_exec_params(ctx, DNN_NATIVE, model->func_type, exec_params) != 0) {
        return DNN_ERROR;
    }

    task = av_malloc(sizeof(*task));
    if (!task) {
        av_log(ctx, AV_LOG_ERROR, "unable to alloc memory for task item.\n");
        return DNN_ERROR;
    }

    if (ff_dnn_fill_task(task, exec_params, native_model, ctx->options.async, 1) != DNN_SUCCESS) {
        av_freep(&task);
        return DNN_ERROR;
    }

    if (ff_queue_push_back(native_model->task_queue, task) < 0) {
        av_freep(&task);
        av_log(ctx, AV_LOG_ERROR, "unable to push back task_queue.\n");
        return DNN_ERROR;
    }

    if (extract_lltask_from_task(task, native_model->lltask_queue) != DNN_SUCCESS) {
        av_log(ctx, AV_LOG_ERROR, "unable to extract last level task from task.\n");
        return DNN_ERROR;
    }

    return execute_model_native(native_model->lltask_queue);
}

DNNReturnType ff_dnn_flush_native(const DNNModel *model)
{
    NativeModel *native_model = model->model;

    if (ff_queue_size(native_model->lltask_queue) == 0) {
        // no pending task need to flush
        return DNN_SUCCESS;
    }

    // for now, use sync node with flush operation
    // Switch to async when it is supported
    return execute_model_native(native_model->lltask_queue);
}

DNNAsyncStatusType ff_dnn_get_result_native(const DNNModel *model, AVFrame **in, AVFrame **out)
{
    NativeModel *native_model = model->model;
    return ff_dnn_get_result_common(native_model->task_queue, in, out);
}

int32_t ff_calculate_operand_dims_count(const DnnOperand *oprd)
{
    int32_t result = 1;
    for (int i = 0; i < 4; ++i)
        result *= oprd->dims[i];

    return result;
}

int32_t ff_calculate_operand_data_length(const DnnOperand* oprd)
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
            native_model = (*model)->model;
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

            while (ff_queue_size(native_model->lltask_queue) != 0) {
                LastLevelTaskItem *item = ff_queue_pop_front(native_model->lltask_queue);
                av_freep(&item);
            }
            ff_queue_destroy(native_model->lltask_queue);

            while (ff_queue_size(native_model->task_queue) != 0) {
                TaskItem *item = ff_queue_pop_front(native_model->task_queue);
                av_frame_free(&item->in_frame);
                av_frame_free(&item->out_frame);
                av_freep(&item);
            }
            ff_queue_destroy(native_model->task_queue);

            av_freep(&native_model);
        }
        av_freep(model);
    }
}
