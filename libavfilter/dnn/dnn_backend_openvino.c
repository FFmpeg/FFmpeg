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
 * DNN OpenVINO backend implementation.
 */

#include "dnn_backend_openvino.h"
#include "dnn_io_proc.h"
#include "libavformat/avio.h"
#include "libavutil/avassert.h"
#include "libavutil/opt.h"
#include "libavutil/avstring.h"
#include "../internal.h"
#include "queue.h"
#include "safe_queue.h"
#include <c_api/ie_c_api.h>

typedef struct OVOptions{
    char *device_type;
    int nireq;
    int batch_size;
    int input_resizable;
} OVOptions;

typedef struct OVContext {
    const AVClass *class;
    OVOptions options;
} OVContext;

typedef struct OVModel{
    OVContext ctx;
    DNNModel *model;
    ie_core_t *core;
    ie_network_t *network;
    ie_executable_network_t *exe_network;
    ie_infer_request_t *infer_request;

    /* for async execution */
    FFSafeQueue *request_queue;   // holds RequestItem
    FFQueue *task_queue;          // holds TaskItem
} OVModel;

typedef struct TaskItem {
    OVModel *ov_model;
    const char *input_name;
    AVFrame *in_frame;
    const char *output_name;
    AVFrame *out_frame;
    int do_ioproc;
    int async;
    int done;
} TaskItem;

typedef struct RequestItem {
    ie_infer_request_t *infer_request;
    TaskItem **tasks;
    int task_count;
    ie_complete_call_back_t callback;
} RequestItem;

#define APPEND_STRING(generated_string, iterate_string)                                            \
    generated_string = generated_string ? av_asprintf("%s %s", generated_string, iterate_string) : \
                                          av_asprintf("%s", iterate_string);

#define OFFSET(x) offsetof(OVContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM
static const AVOption dnn_openvino_options[] = {
    { "device", "device to run model", OFFSET(options.device_type), AV_OPT_TYPE_STRING, { .str = "CPU" }, 0, 0, FLAGS },
    { "nireq",  "number of request",   OFFSET(options.nireq),       AV_OPT_TYPE_INT,    { .i64 = 0 },     0, INT_MAX, FLAGS },
    { "batch_size",  "batch size per request", OFFSET(options.batch_size),  AV_OPT_TYPE_INT,    { .i64 = 1 },     1, 1000, FLAGS},
    { "input_resizable", "can input be resizable or not", OFFSET(options.input_resizable), AV_OPT_TYPE_BOOL,   { .i64 = 0 },     0, 1, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(dnn_openvino);

static DNNDataType precision_to_datatype(precision_e precision)
{
    switch (precision)
    {
    case FP32:
        return DNN_FLOAT;
    default:
        av_assert0(!"not supported yet.");
        return DNN_FLOAT;
    }
}

static int get_datatype_size(DNNDataType dt)
{
    switch (dt)
    {
    case DNN_FLOAT:
        return sizeof(float);
    default:
        av_assert0(!"not supported yet.");
        return 1;
    }
}

static DNNReturnType fill_model_input_ov(OVModel *ov_model, RequestItem *request)
{
    dimensions_t dims;
    precision_e precision;
    ie_blob_buffer_t blob_buffer;
    OVContext *ctx = &ov_model->ctx;
    IEStatusCode status;
    DNNData input;
    ie_blob_t *input_blob = NULL;
    TaskItem *task = request->tasks[0];

    status = ie_infer_request_get_blob(request->infer_request, task->input_name, &input_blob);
    if (status != OK) {
        av_log(ctx, AV_LOG_ERROR, "Failed to get input blob with name %s\n", task->input_name);
        return DNN_ERROR;
    }

    status |= ie_blob_get_dims(input_blob, &dims);
    status |= ie_blob_get_precision(input_blob, &precision);
    if (status != OK) {
        av_log(ctx, AV_LOG_ERROR, "Failed to get input blob dims/precision\n");
        return DNN_ERROR;
    }

    status = ie_blob_get_buffer(input_blob, &blob_buffer);
    if (status != OK) {
        av_log(ctx, AV_LOG_ERROR, "Failed to get input blob buffer\n");
        return DNN_ERROR;
    }

    input.height = dims.dims[2];
    input.width = dims.dims[3];
    input.channels = dims.dims[1];
    input.data = blob_buffer.buffer;
    input.dt = precision_to_datatype(precision);

    av_assert0(request->task_count <= dims.dims[0]);
    for (int i = 0; i < request->task_count; ++i) {
        task = request->tasks[i];
        if (task->do_ioproc) {
            if (ov_model->model->pre_proc != NULL) {
                ov_model->model->pre_proc(task->in_frame, &input, ov_model->model->filter_ctx);
            } else {
                proc_from_frame_to_dnn(task->in_frame, &input, ctx);
            }
        }
        input.data = (uint8_t *)input.data
                     + input.width * input.height * input.channels * get_datatype_size(input.dt);
    }
    ie_blob_free(&input_blob);

    return DNN_SUCCESS;
}

static void infer_completion_callback(void *args)
{
    dimensions_t dims;
    precision_e precision;
    IEStatusCode status;
    RequestItem *request = args;
    TaskItem *task = request->tasks[0];
    ie_blob_t *output_blob = NULL;
    ie_blob_buffer_t blob_buffer;
    DNNData output;
    OVContext *ctx = &task->ov_model->ctx;

    status = ie_infer_request_get_blob(request->infer_request, task->output_name, &output_blob);
    if (status != OK) {
        //incorrect output name
        char *model_output_name = NULL;
        char *all_output_names = NULL;
        size_t model_output_count = 0;
        av_log(ctx, AV_LOG_ERROR, "Failed to get model output data\n");
        status = ie_network_get_outputs_number(task->ov_model->network, &model_output_count);
        for (size_t i = 0; i < model_output_count; i++) {
            status = ie_network_get_output_name(task->ov_model->network, i, &model_output_name);
            APPEND_STRING(all_output_names, model_output_name)
        }
        av_log(ctx, AV_LOG_ERROR,
               "output \"%s\" may not correct, all output(s) are: \"%s\"\n",
               task->output_name, all_output_names);
        return;
    }

    status = ie_blob_get_buffer(output_blob, &blob_buffer);
    if (status != OK) {
        av_log(ctx, AV_LOG_ERROR, "Failed to access output memory\n");
        return;
    }

    status |= ie_blob_get_dims(output_blob, &dims);
    status |= ie_blob_get_precision(output_blob, &precision);
    if (status != OK) {
        av_log(ctx, AV_LOG_ERROR, "Failed to get dims or precision of output\n");
        return;
    }

    output.channels = dims.dims[1];
    output.height   = dims.dims[2];
    output.width    = dims.dims[3];
    output.dt       = precision_to_datatype(precision);
    output.data     = blob_buffer.buffer;

    av_assert0(request->task_count <= dims.dims[0]);
    av_assert0(request->task_count >= 1);
    for (int i = 0; i < request->task_count; ++i) {
        task = request->tasks[i];
        if (task->do_ioproc) {
            if (task->ov_model->model->post_proc != NULL) {
                task->ov_model->model->post_proc(task->out_frame, &output, task->ov_model->model->filter_ctx);
            } else {
                proc_from_dnn_to_frame(task->out_frame, &output, ctx);
            }
        } else {
            task->out_frame->width = output.width;
            task->out_frame->height = output.height;
        }
        task->done = 1;
        output.data = (uint8_t *)output.data
                      + output.width * output.height * output.channels * get_datatype_size(output.dt);
    }
    ie_blob_free(&output_blob);

    request->task_count = 0;

    if (task->async) {
        if (ff_safe_queue_push_back(task->ov_model->request_queue, request) < 0) {
            av_log(ctx, AV_LOG_ERROR, "Failed to push back request_queue.\n");
            return;
        }
    }
}

static DNNReturnType init_model_ov(OVModel *ov_model)
{
    OVContext *ctx = &ov_model->ctx;
    IEStatusCode status;
    ie_available_devices_t a_dev;
    ie_config_t config = {NULL, NULL, NULL};
    char *all_dev_names = NULL;

    // batch size
    if (ctx->options.batch_size <= 0) {
        ctx->options.batch_size = 1;
    }

    if (ctx->options.batch_size > 1) {
        input_shapes_t input_shapes;
        status = ie_network_get_input_shapes(ov_model->network, &input_shapes);
        if (status != OK)
            goto err;
        for (int i = 0; i < input_shapes.shape_num; i++)
            input_shapes.shapes[i].shape.dims[0] = ctx->options.batch_size;
        status = ie_network_reshape(ov_model->network, input_shapes);
        ie_network_input_shapes_free(&input_shapes);
        if (status != OK)
            goto err;
    }

    status = ie_core_load_network(ov_model->core, ov_model->network, ctx->options.device_type, &config, &ov_model->exe_network);
    if (status != OK) {
        av_log(ctx, AV_LOG_ERROR, "Failed to load OpenVINO model network\n");
        status = ie_core_get_available_devices(ov_model->core, &a_dev);
        if (status != OK) {
            av_log(ctx, AV_LOG_ERROR, "Failed to get available devices\n");
            goto err;
        }
        for (int i = 0; i < a_dev.num_devices; i++) {
            APPEND_STRING(all_dev_names, a_dev.devices[i])
        }
        av_log(ctx, AV_LOG_ERROR,"device %s may not be supported, all available devices are: \"%s\"\n",
               ctx->options.device_type, all_dev_names);
        goto err;
    }

    // create infer_request for sync execution
    status = ie_exec_network_create_infer_request(ov_model->exe_network, &ov_model->infer_request);
    if (status != OK)
        goto err;

    // create infer_requests for async execution
    if (ctx->options.nireq <= 0) {
        // the default value is a rough estimation
        ctx->options.nireq = av_cpu_count() / 2 + 1;
    }

    ov_model->request_queue = ff_safe_queue_create();
    if (!ov_model->request_queue) {
        goto err;
    }

    for (int i = 0; i < ctx->options.nireq; i++) {
        RequestItem *item = av_mallocz(sizeof(*item));
        if (!item) {
            goto err;
        }

        status = ie_exec_network_create_infer_request(ov_model->exe_network, &item->infer_request);
        if (status != OK) {
            av_freep(&item);
            goto err;
        }

        item->tasks = av_malloc_array(ctx->options.batch_size, sizeof(*item->tasks));
        if (!item->tasks) {
            av_freep(&item);
            goto err;
        }
        item->task_count = 0;

        item->callback.completeCallBackFunc = infer_completion_callback;
        item->callback.args = item;
        if (ff_safe_queue_push_back(ov_model->request_queue, item) < 0) {
            av_freep(&item);
            goto err;
        }
    }

    ov_model->task_queue = ff_queue_create();
    if (!ov_model->task_queue) {
        goto err;
    }

    return DNN_SUCCESS;

err:
    ff_dnn_free_model_ov(&ov_model->model);
    return DNN_ERROR;
}

static DNNReturnType execute_model_ov(RequestItem *request)
{
    IEStatusCode status;
    DNNReturnType ret;
    TaskItem *task = request->tasks[0];
    OVContext *ctx = &task->ov_model->ctx;

    if (task->async) {
        if (request->task_count < ctx->options.batch_size) {
            if (ff_safe_queue_push_front(task->ov_model->request_queue, request) < 0) {
                av_log(ctx, AV_LOG_ERROR, "Failed to push back request_queue.\n");
                return DNN_ERROR;
            }
            return DNN_SUCCESS;
        }
        ret = fill_model_input_ov(task->ov_model, request);
        if (ret != DNN_SUCCESS) {
            return ret;
        }
        status = ie_infer_set_completion_callback(request->infer_request, &request->callback);
        if (status != OK) {
            av_log(ctx, AV_LOG_ERROR, "Failed to set completion callback for inference\n");
            return DNN_ERROR;
        }
        status = ie_infer_request_infer_async(request->infer_request);
        if (status != OK) {
            av_log(ctx, AV_LOG_ERROR, "Failed to start async inference\n");
            return DNN_ERROR;
        }
        return DNN_SUCCESS;
    } else {
        ret = fill_model_input_ov(task->ov_model, request);
        if (ret != DNN_SUCCESS) {
            return ret;
        }
        status = ie_infer_request_infer(request->infer_request);
        if (status != OK) {
            av_log(ctx, AV_LOG_ERROR, "Failed to start synchronous model inference\n");
            return DNN_ERROR;
        }
        infer_completion_callback(request);
        return task->done ? DNN_SUCCESS : DNN_ERROR;
    }
}

static DNNReturnType get_input_ov(void *model, DNNData *input, const char *input_name)
{
    OVModel *ov_model = (OVModel *)model;
    OVContext *ctx = &ov_model->ctx;
    char *model_input_name = NULL;
    char *all_input_names = NULL;
    IEStatusCode status;
    size_t model_input_count = 0;
    dimensions_t dims;
    precision_e precision;
    int input_resizable = ctx->options.input_resizable;

    status = ie_network_get_inputs_number(ov_model->network, &model_input_count);
    if (status != OK) {
        av_log(ctx, AV_LOG_ERROR, "Failed to get input count\n");
        return DNN_ERROR;
    }

    for (size_t i = 0; i < model_input_count; i++) {
        status = ie_network_get_input_name(ov_model->network, i, &model_input_name);
        if (status != OK) {
            av_log(ctx, AV_LOG_ERROR, "Failed to get No.%d input's name\n", (int)i);
            return DNN_ERROR;
        }
        if (strcmp(model_input_name, input_name) == 0) {
            ie_network_name_free(&model_input_name);
            status |= ie_network_get_input_dims(ov_model->network, input_name, &dims);
            status |= ie_network_get_input_precision(ov_model->network, input_name, &precision);
            if (status != OK) {
                av_log(ctx, AV_LOG_ERROR, "Failed to get No.%d input's dims or precision\n", (int)i);
                return DNN_ERROR;
            }

            input->channels = dims.dims[1];
            input->height   = input_resizable ? -1 : dims.dims[2];
            input->width    = input_resizable ? -1 : dims.dims[3];
            input->dt       = precision_to_datatype(precision);
            return DNN_SUCCESS;
        } else {
            //incorrect input name
            APPEND_STRING(all_input_names, model_input_name)
        }

        ie_network_name_free(&model_input_name);
    }

    av_log(ctx, AV_LOG_ERROR, "Could not find \"%s\" in model, all input(s) are: \"%s\"\n", input_name, all_input_names);
    return DNN_ERROR;
}

static DNNReturnType get_output_ov(void *model, const char *input_name, int input_width, int input_height,
                                   const char *output_name, int *output_width, int *output_height)
{
    DNNReturnType ret;
    OVModel *ov_model = (OVModel *)model;
    OVContext *ctx = &ov_model->ctx;
    TaskItem task;
    RequestItem request;
    AVFrame *in_frame = av_frame_alloc();
    AVFrame *out_frame = NULL;
    TaskItem *ptask = &task;
    IEStatusCode status;
    input_shapes_t input_shapes;

    if (!in_frame) {
        av_log(ctx, AV_LOG_ERROR, "Failed to allocate memory for input frame\n");
        return DNN_ERROR;
    }
    out_frame = av_frame_alloc();
    if (!out_frame) {
        av_log(ctx, AV_LOG_ERROR, "Failed to allocate memory for output frame\n");
        av_frame_free(&in_frame);
        return DNN_ERROR;
    }
    in_frame->width = input_width;
    in_frame->height = input_height;

    if (ctx->options.input_resizable) {
        status = ie_network_get_input_shapes(ov_model->network, &input_shapes);
        input_shapes.shapes->shape.dims[2] = input_height;
        input_shapes.shapes->shape.dims[3] = input_width;
        status |= ie_network_reshape(ov_model->network, input_shapes);
        ie_network_input_shapes_free(&input_shapes);
        if (status != OK) {
            av_log(ctx, AV_LOG_ERROR, "Failed to reshape input size for %s\n", input_name);
            return DNN_ERROR;
        }
    }

    if (!ov_model->exe_network) {
        if (init_model_ov(ov_model) != DNN_SUCCESS) {
            av_log(ctx, AV_LOG_ERROR, "Failed init OpenVINO exectuable network or inference request\n");
            return DNN_ERROR;
        };
    }

    task.done = 0;
    task.do_ioproc = 0;
    task.async = 0;
    task.input_name = input_name;
    task.in_frame = in_frame;
    task.output_name = output_name;
    task.out_frame = out_frame;
    task.ov_model = ov_model;

    request.infer_request = ov_model->infer_request;
    request.task_count = 1;
    request.tasks = &ptask;

    ret = execute_model_ov(&request);
    *output_width = out_frame->width;
    *output_height = out_frame->height;

    av_frame_free(&out_frame);
    av_frame_free(&in_frame);
    return ret;
}

DNNModel *ff_dnn_load_model_ov(const char *model_filename, const char *options, AVFilterContext *filter_ctx)
{
    DNNModel *model = NULL;
    OVModel *ov_model = NULL;
    OVContext *ctx = NULL;
    IEStatusCode status;

    model = av_mallocz(sizeof(DNNModel));
    if (!model){
        return NULL;
    }

    ov_model = av_mallocz(sizeof(OVModel));
    if (!ov_model) {
        av_freep(&model);
        return NULL;
    }
    model->model = (void *)ov_model;
    ov_model->model = model;
    ov_model->ctx.class = &dnn_openvino_class;
    ctx = &ov_model->ctx;

    //parse options
    av_opt_set_defaults(ctx);
    if (av_opt_set_from_string(ctx, options, NULL, "=", "&") < 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to parse options \"%s\"\n", options);
        goto err;
    }

    status = ie_core_create("", &ov_model->core);
    if (status != OK)
        goto err;

    status = ie_core_read_network(ov_model->core, model_filename, NULL, &ov_model->network);
    if (status != OK)
        goto err;

    model->get_input = &get_input_ov;
    model->get_output = &get_output_ov;
    model->options = options;
    model->filter_ctx = filter_ctx;

    return model;

err:
    ff_dnn_free_model_ov(&model);
    return NULL;
}

DNNReturnType ff_dnn_execute_model_ov(const DNNModel *model, const char *input_name, AVFrame *in_frame,
                                      const char **output_names, uint32_t nb_output, AVFrame *out_frame)
{
    OVModel *ov_model = (OVModel *)model->model;
    OVContext *ctx = &ov_model->ctx;
    TaskItem task;
    RequestItem request;
    TaskItem *ptask = &task;

    if (!in_frame) {
        av_log(ctx, AV_LOG_ERROR, "in frame is NULL when execute model.\n");
        return DNN_ERROR;
    }

    if (!out_frame) {
        av_log(ctx, AV_LOG_ERROR, "out frame is NULL when execute model.\n");
        return DNN_ERROR;
    }

    if (nb_output != 1) {
        // currently, the filter does not need multiple outputs,
        // so we just pending the support until we really need it.
        av_log(ctx, AV_LOG_ERROR, "do not support multiple outputs\n");
        return DNN_ERROR;
    }

    if (ctx->options.batch_size > 1) {
        av_log(ctx, AV_LOG_ERROR, "do not support batch mode for sync execution.\n");
        return DNN_ERROR;
    }

    if (!ov_model->exe_network) {
        if (init_model_ov(ov_model) != DNN_SUCCESS) {
            av_log(ctx, AV_LOG_ERROR, "Failed init OpenVINO exectuable network or inference request\n");
            return DNN_ERROR;
        };
    }

    task.done = 0;
    task.do_ioproc = 1;
    task.async = 0;
    task.input_name = input_name;
    task.in_frame = in_frame;
    task.output_name = output_names[0];
    task.out_frame = out_frame;
    task.ov_model = ov_model;

    request.infer_request = ov_model->infer_request;
    request.task_count = 1;
    request.tasks = &ptask;

    return execute_model_ov(&request);
}

DNNReturnType ff_dnn_execute_model_async_ov(const DNNModel *model, const char *input_name, AVFrame *in_frame,
                                            const char **output_names, uint32_t nb_output, AVFrame *out_frame)
{
    OVModel *ov_model = (OVModel *)model->model;
    OVContext *ctx = &ov_model->ctx;
    RequestItem *request;
    TaskItem *task;

    if (!in_frame) {
        av_log(ctx, AV_LOG_ERROR, "in frame is NULL when async execute model.\n");
        return DNN_ERROR;
    }

    if (!out_frame) {
        av_log(ctx, AV_LOG_ERROR, "out frame is NULL when async execute model.\n");
        return DNN_ERROR;
    }

    task = av_malloc(sizeof(*task));
    if (!task) {
        av_log(ctx, AV_LOG_ERROR, "unable to alloc memory for task item.\n");
        return DNN_ERROR;
    }

    if (!ov_model->exe_network) {
        if (init_model_ov(ov_model) != DNN_SUCCESS) {
            av_log(ctx, AV_LOG_ERROR, "Failed init OpenVINO exectuable network or inference request\n");
            return DNN_ERROR;
        };
    }

    task->done = 0;
    task->do_ioproc = 1;
    task->async = 1;
    task->input_name = input_name;
    task->in_frame = in_frame;
    task->output_name = output_names[0];
    task->out_frame = out_frame;
    task->ov_model = ov_model;
    if (ff_queue_push_back(ov_model->task_queue, task) < 0) {
        av_freep(&task);
        av_log(ctx, AV_LOG_ERROR, "unable to push back task_queue.\n");
        return DNN_ERROR;
    }

    request = ff_safe_queue_pop_front(ov_model->request_queue);
    if (!request) {
        av_log(ctx, AV_LOG_ERROR, "unable to get infer request.\n");
        return DNN_ERROR;
    }

    request->tasks[request->task_count++] = task;
    return execute_model_ov(request);
}

DNNAsyncStatusType ff_dnn_get_async_result_ov(const DNNModel *model, AVFrame **in, AVFrame **out)
{
    OVModel *ov_model = (OVModel *)model->model;
    TaskItem *task = ff_queue_peek_front(ov_model->task_queue);

    if (!task) {
        return DAST_EMPTY_QUEUE;
    }

    if (!task->done) {
        return DAST_NOT_READY;
    }

    *in = task->in_frame;
    *out = task->out_frame;
    ff_queue_pop_front(ov_model->task_queue);
    av_freep(&task);

    return DAST_SUCCESS;
}

DNNReturnType ff_dnn_flush_ov(const DNNModel *model)
{
    OVModel *ov_model = (OVModel *)model->model;
    OVContext *ctx = &ov_model->ctx;
    RequestItem *request;
    IEStatusCode status;
    DNNReturnType ret;

    request = ff_safe_queue_pop_front(ov_model->request_queue);
    if (!request) {
        av_log(ctx, AV_LOG_ERROR, "unable to get infer request.\n");
        return DNN_ERROR;
    }

    if (request->task_count == 0) {
        // no pending task need to flush
        if (ff_safe_queue_push_back(ov_model->request_queue, request) < 0) {
            av_log(ctx, AV_LOG_ERROR, "Failed to push back request_queue.\n");
            return DNN_ERROR;
        }
        return DNN_SUCCESS;
    }

    ret = fill_model_input_ov(ov_model, request);
    if (ret != DNN_SUCCESS) {
        av_log(ctx, AV_LOG_ERROR, "Failed to fill model input.\n");
        return ret;
    }
    status = ie_infer_set_completion_callback(request->infer_request, &request->callback);
    if (status != OK) {
        av_log(ctx, AV_LOG_ERROR, "Failed to set completion callback for inference\n");
        return DNN_ERROR;
    }
    status = ie_infer_request_infer_async(request->infer_request);
    if (status != OK) {
        av_log(ctx, AV_LOG_ERROR, "Failed to start async inference\n");
        return DNN_ERROR;
    }

    return DNN_SUCCESS;
}

void ff_dnn_free_model_ov(DNNModel **model)
{
    if (*model){
        OVModel *ov_model = (OVModel *)(*model)->model;
        while (ff_safe_queue_size(ov_model->request_queue) != 0) {
            RequestItem *item = ff_safe_queue_pop_front(ov_model->request_queue);
            if (item && item->infer_request) {
                ie_infer_request_free(&item->infer_request);
            }
            av_freep(&item->tasks);
            av_freep(&item);
        }
        ff_safe_queue_destroy(ov_model->request_queue);

        while (ff_queue_size(ov_model->task_queue) != 0) {
            TaskItem *item = ff_queue_pop_front(ov_model->task_queue);
            av_frame_free(&item->in_frame);
            av_frame_free(&item->out_frame);
            av_freep(&item);
        }
        ff_queue_destroy(ov_model->task_queue);

        if (ov_model->infer_request)
            ie_infer_request_free(&ov_model->infer_request);
        if (ov_model->exe_network)
            ie_exec_network_free(&ov_model->exe_network);
        if (ov_model->network)
            ie_network_free(&ov_model->network);
        if (ov_model->core)
            ie_core_free(&ov_model->core);
        av_freep(&ov_model);
        av_freep(model);
    }
}
