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
#include <c_api/ie_c_api.h>

typedef struct OVOptions{
    char *device_type;
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
} OVModel;

typedef struct TaskItem {
    OVModel *ov_model;
    const char *input_name;
    AVFrame *in_frame;
    const char *output_name;
    AVFrame *out_frame;
    int do_ioproc;
    int done;
} TaskItem;

typedef struct RequestItem {
    ie_infer_request_t *infer_request;
    TaskItem *task;
} RequestItem;

#define APPEND_STRING(generated_string, iterate_string)                                            \
    generated_string = generated_string ? av_asprintf("%s %s", generated_string, iterate_string) : \
                                          av_asprintf("%s", iterate_string);

#define OFFSET(x) offsetof(OVContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM
static const AVOption dnn_openvino_options[] = {
    { "device", "device to run model", OFFSET(options.device_type), AV_OPT_TYPE_STRING, { .str = "CPU" }, 0, 0, FLAGS },
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

static DNNReturnType fill_model_input_ov(OVModel *ov_model, TaskItem *task, RequestItem *request)
{
    dimensions_t dims;
    precision_e precision;
    ie_blob_buffer_t blob_buffer;
    OVContext *ctx = &ov_model->ctx;
    IEStatusCode status;
    DNNData input;
    ie_blob_t *input_blob = NULL;

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
    if (task->do_ioproc) {
        if (ov_model->model->pre_proc != NULL) {
            ov_model->model->pre_proc(task->in_frame, &input, ov_model->model->userdata);
        } else {
            proc_from_frame_to_dnn(task->in_frame, &input, ctx);
        }
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
    TaskItem *task = request->task;
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
    if (task->do_ioproc) {
        if (task->ov_model->model->post_proc != NULL) {
            task->ov_model->model->post_proc(task->out_frame, &output, task->ov_model->model->userdata);
        } else {
            proc_from_dnn_to_frame(task->out_frame, &output, ctx);
        }
    } else {
        task->out_frame->width = output.width;
        task->out_frame->height = output.height;
    }
    ie_blob_free(&output_blob);
    task->done = 1;
}

static DNNReturnType execute_model_ov(TaskItem *task, RequestItem *request)
{
    IEStatusCode status;
    OVContext *ctx = &task->ov_model->ctx;

    DNNReturnType ret = fill_model_input_ov(task->ov_model, task, request);
    if (ret != DNN_SUCCESS) {
        return ret;
    }

    status = ie_infer_request_infer(request->infer_request);
    if (status != OK) {
        av_log(ctx, AV_LOG_ERROR, "Failed to start synchronous model inference\n");
        return DNN_ERROR;
    }

    request->task = task;
    infer_completion_callback(request);

    return task->done ? DNN_SUCCESS : DNN_ERROR;
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

            // The order of dims in the openvino is fixed and it is always NCHW for 4-D data.
            // while we pass NHWC data from FFmpeg to openvino
            status = ie_network_set_input_layout(ov_model->network, input_name, NHWC);
            if (status != OK) {
                av_log(ctx, AV_LOG_ERROR, "Input \"%s\" does not match layout NHWC\n", input_name);
                return DNN_ERROR;
            }

            input->channels = dims.dims[1];
            input->height   = dims.dims[2];
            input->width    = dims.dims[3];
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

    task.done = 0;
    task.do_ioproc = 0;
    task.input_name = input_name;
    task.in_frame = in_frame;
    task.output_name = output_name;
    task.out_frame = out_frame;
    task.ov_model = ov_model;

    request.infer_request = ov_model->infer_request;

    ret = execute_model_ov(&task, &request);
    *output_width = out_frame->width;
    *output_height = out_frame->height;

    av_frame_free(&out_frame);
    av_frame_free(&in_frame);
    return ret;
}

DNNModel *ff_dnn_load_model_ov(const char *model_filename, const char *options, void *userdata)
{
    char *all_dev_names = NULL;
    DNNModel *model = NULL;
    OVModel *ov_model = NULL;
    OVContext *ctx = NULL;
    IEStatusCode status;
    ie_config_t config = {NULL, NULL, NULL};
    ie_available_devices_t a_dev;

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

    status = ie_core_load_network(ov_model->core, ov_model->network, ctx->options.device_type, &config, &ov_model->exe_network);
    if (status != OK) {
        av_log(ctx, AV_LOG_ERROR, "Failed to init OpenVINO model\n");
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

    status = ie_exec_network_create_infer_request(ov_model->exe_network, &ov_model->infer_request);
    if (status != OK)
        goto err;

    model->get_input = &get_input_ov;
    model->get_output = &get_output_ov;
    model->options = options;
    model->userdata = userdata;

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

    task.done = 0;
    task.do_ioproc = 1;
    task.input_name = input_name;
    task.in_frame = in_frame;
    task.output_name = output_names[0];
    task.out_frame = out_frame;
    task.ov_model = ov_model;

    request.infer_request = ov_model->infer_request;

    return execute_model_ov(&task, &request);
}

void ff_dnn_free_model_ov(DNNModel **model)
{
    if (*model){
        OVModel *ov_model = (OVModel *)(*model)->model;
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
