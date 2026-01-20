/*
 * Copyright (c) 2026 Advanced Micro Devices, Inc.
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
 * DNN ONNX Runtime backend implementation.
 */

#include "libavutil/opt.h"
#include "libavutil/avassert.h"
#include "libavutil/mem.h"
#include "libavutil/avstring.h"
#include "libavutil/thread.h"
#include "libavutil/wchar_filename.h"
#include "../filters.h"
#include "dnn_io_proc.h"
#include "dnn_backend_common.h"
#include "queue.h"
#include "safe_queue.h"
#include <onnxruntime_c_api.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

typedef struct ONNXModel {
    DNNModel model;
    DnnContext *ctx;
    OrtEnv *env;
    OrtSession *session;
    OrtSessionOptions *session_options;
    OrtAllocator *allocator;
    SafeQueue *request_queue;
    Queue *task_queue;
    Queue *lltask_queue;
    DNNData input_info;
    int     input_resolved;
    int     output_resolved;
} ONNXModel;

typedef struct ONNXInferRequest {
    OrtValue *input_tensor;
    OrtValue *output_tensor;
    void     *input_data;
} ONNXInferRequest;

typedef struct ONNXRequestItem {
    ONNXInferRequest *infer_request;
    LastLevelTaskItem *lltask;
    DNNAsyncExecModule exec_module;
} ONNXRequestItem;

#define OFFSET(x) offsetof(ONNXOptions, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM
static const AVOption dnn_onnx_options[] = {
    { "threads_per_operation", "number of CPU threads per ORT operator (device=cpu only)",
      OFFSET(num_threads),       AV_OPT_TYPE_INT,    { .i64 = 0 },    0, INT_MAX, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(dnn_onnx);

static const OrtApi *g_ort = NULL;
static AVOnce g_ort_init_once = AV_ONCE_INIT;

static void init_ort_api(void)
{
    g_ort = OrtGetApiBase()->GetApi(ORT_API_VERSION);
}

#define ORT_ABORT_ON_ERROR(expr)                                \
    do {                                                        \
        OrtStatus *status = (expr);                             \
        if (status != NULL) {                                   \
            const char *msg = g_ort->GetErrorMessage(status);   \
            av_log(ctx, AV_LOG_ERROR, "ONNX Runtime error: %s\n", msg); \
            g_ort->ReleaseStatus(status);                       \
            goto err;                                           \
        }                                                       \
    } while (0)

static int extract_lltask_from_task(TaskItem *task, Queue *lltask_queue)
{
    ONNXModel     *onnx_model = (ONNXModel *)task->model;
    DnnContext           *ctx = onnx_model->ctx;
    LastLevelTaskItem *lltask = av_malloc(sizeof(*lltask));

    if (!lltask) {
        av_log(ctx, AV_LOG_ERROR, "Failed to allocate memory for LastLevelTaskItem\n");
        return AVERROR(ENOMEM);
    }
    task->inference_todo = 1;
    task->inference_done = 0;
    lltask->task = task;
    if (ff_queue_push_back(lltask_queue, lltask) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to push back lltask_queue.\n");
        av_freep(&lltask);
        return AVERROR(ENOMEM);
    }
    return 0;
}

static void onnx_free_request(ONNXInferRequest *request)
{
    if (!request)
        return;
    if (request->input_tensor) {
        g_ort->ReleaseValue(request->input_tensor);
        request->input_tensor = NULL;
    }
    av_freep(&request->input_data);
    if (request->output_tensor) {
        g_ort->ReleaseValue(request->output_tensor);
        request->output_tensor = NULL;
    }
}

static inline void destroy_request_item(ONNXRequestItem **arg)
{
    ONNXRequestItem *item;
    if (!arg || !*arg)
        return;
    item = *arg;
    onnx_free_request(item->infer_request);
    av_freep(&item->infer_request);
    av_freep(&item->lltask);
    ff_dnn_async_module_cleanup(&item->exec_module);
    av_freep(arg);
}

static void dnn_free_model_onnx(DNNModel **model)
{
    ONNXModel *onnx_model;
    if (!model || !*model)
        return;

    onnx_model = (ONNXModel *)(*model);

    while (ff_safe_queue_size(onnx_model->request_queue) != 0) {
        ONNXRequestItem *item = (ONNXRequestItem *)ff_safe_queue_pop_front(onnx_model->request_queue);
        destroy_request_item(&item);
    }
    ff_safe_queue_destroy(onnx_model->request_queue);

    while (ff_queue_size(onnx_model->lltask_queue) != 0) {
        LastLevelTaskItem *item = (LastLevelTaskItem *)ff_queue_pop_front(onnx_model->lltask_queue);
        av_freep(&item);
    }
    ff_queue_destroy(onnx_model->lltask_queue);

    while (ff_queue_size(onnx_model->task_queue) != 0) {
        TaskItem *item = (TaskItem *)ff_queue_pop_front(onnx_model->task_queue);
        av_frame_free(&item->in_frame);
        av_frame_free(&item->out_frame);
        av_freep(&item);
    }
    ff_queue_destroy(onnx_model->task_queue);

    if (onnx_model->session)
        g_ort->ReleaseSession(onnx_model->session);
    if (onnx_model->session_options)
        g_ort->ReleaseSessionOptions(onnx_model->session_options);
    if (onnx_model->env)
        g_ort->ReleaseEnv(onnx_model->env);

    av_freep(&onnx_model);
    *model = NULL;
}

static int get_input_onnx(DNNModel *model, DNNData *input, const char *input_name)
{
    ONNXModel  *onnx_model = (ONNXModel *)model;
    DnnContext        *ctx = onnx_model->ctx;
    OrtTypeInfo *type_info = NULL;
    const OrtTensorTypeAndShapeInfo *tensor_info = NULL;
    size_t num_dims;
    size_t input_count = 0;
    size_t input_index = 0;
    int    found_input = 0;
    int64_t *dims;
    ONNXTensorElementDataType tensor_type;
    OrtStatus *status;

    if (!input_name || !*input_name) {
        av_log(ctx, AV_LOG_ERROR, "ONNX input name is not specified\n");
        return AVERROR(EINVAL);
    }

    if (onnx_model->input_resolved) {
        *input = onnx_model->input_info;
        return 0;
    }

    status = g_ort->SessionGetInputCount(onnx_model->session, &input_count);
    if (status != NULL) {
        const char *msg = g_ort->GetErrorMessage(status);
        av_log(ctx, AV_LOG_ERROR, "Failed to get input count: %s\n", msg);
        g_ort->ReleaseStatus(status);
        return AVERROR(EINVAL);
    }

    for (size_t i = 0; i < input_count; i++) {
        char *name = NULL;
        status = g_ort->SessionGetInputName(onnx_model->session, i,
                                            onnx_model->allocator, &name);
        if (status != NULL) {
            g_ort->ReleaseStatus(status);
            continue;
        }
        if (!strcmp(name, input_name)) {
            input_index = i;
            found_input = 1;
        }
        onnx_model->allocator->Free(onnx_model->allocator, name);
        if (found_input)
            break;
    }

    if (!found_input) {
        av_log(ctx, AV_LOG_ERROR, "Input name '%s' not found in ONNX model\n",
               input_name);
        return AVERROR(EINVAL);
    }

    status = g_ort->SessionGetInputTypeInfo(onnx_model->session, input_index,
                                            &type_info);
    if (status != NULL) {
        const char *msg = g_ort->GetErrorMessage(status);
        av_log(ctx, AV_LOG_ERROR, "Failed to get input type info: %s\n", msg);
        g_ort->ReleaseStatus(status);
        return AVERROR(EINVAL);
    }

    status = g_ort->CastTypeInfoToTensorInfo(type_info, &tensor_info);
    if (status != NULL) {
        g_ort->ReleaseTypeInfo(type_info);
        g_ort->ReleaseStatus(status);
        return AVERROR(EINVAL);
    }

    status = g_ort->GetDimensionsCount(tensor_info, &num_dims);
    if (status != NULL) {
        g_ort->ReleaseTypeInfo(type_info);
        g_ort->ReleaseStatus(status);
        return AVERROR(EINVAL);
    }

    if (num_dims != 4) {
        avpriv_report_missing_feature(ctx, "Support for %zu dimensional input", num_dims);
        g_ort->ReleaseTypeInfo(type_info);
        return AVERROR(ENOSYS);
    }

    dims = av_malloc(num_dims * sizeof(int64_t));
    if (!dims) {
        g_ort->ReleaseTypeInfo(type_info);
        return AVERROR(ENOMEM);
    }

    g_ort->GetDimensions(tensor_info, dims, num_dims);
    g_ort->GetTensorElementType(tensor_info, &tensor_type);

    if (dims[0] > 1) {
        av_log(ctx, AV_LOG_ERROR,
               "ONNX model has fixed batch size %"PRId64", but the backend "
               "only supports a batch size of 1\n", dims[0]);
        av_free(dims);
        g_ort->ReleaseTypeInfo(type_info);
        return AVERROR(ENOSYS);
    }

    /*
     * The ONNX backend assumes a 4-D NCHW input tensor (the rank check
     * above already rejects anything else).
     */
    input->layout = DL_NCHW;
    input->dims[0] = dims[0] > 0 ? dims[0] : 1;
    input->dims[1] = dims[1] > 0 ? dims[1] : 3;
    input->dims[2] = dims[2] > 0 ? dims[2] : -1;
    input->dims[3] = dims[3] > 0 ? dims[3] : -1;

    if (tensor_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
        input->dt = DNN_FLOAT;
    } else {
        av_log(ctx, AV_LOG_ERROR, "Unsupported input tensor data type, only float is supported\n");
        av_free(dims);
        g_ort->ReleaseTypeInfo(type_info);
        return AVERROR(ENOSYS);
    }

    /*
     * The DCO_RGB setting below is only consulted by the dnn_detect and dnn_classify;
     * the dnn_processing path lets the source AVFrame pixel format determine the
     * tensor channel order, so both RGB24 and BGR24 inputs work transparently
     * for that flow.
     */
    input->order = DCO_RGB;
    av_free(dims);
    g_ort->ReleaseTypeInfo(type_info);

    onnx_model->input_info = *input;
    onnx_model->input_resolved = 1;
    return 0;
}

static int fill_model_input_onnx(ONNXModel *onnx_model, ONNXRequestItem *request)
{
    LastLevelTaskItem       *lltask = NULL;
    TaskItem                  *task = NULL;
    ONNXInferRequest *infer_request = NULL;
    DNNData                   input = { 0 };
    DnnContext                 *ctx = onnx_model->ctx;
    int ret, width_idx, height_idx, channel_idx;
    int64_t input_shape[4];
    size_t input_tensor_size;
    OrtMemoryInfo *memory_info;
    OrtStatus *status;

    lltask = (LastLevelTaskItem *)ff_queue_pop_front(onnx_model->lltask_queue);
    if (!lltask) {
        ret = AVERROR(EINVAL);
        goto err;
    }
    request->lltask = lltask;
    task = lltask->task;
    infer_request = request->infer_request;

    ret = get_input_onnx(&onnx_model->model, &input, task->input_name);
    if (ret != 0) {
        goto err;
    }

    width_idx   = dnn_get_width_idx_by_layout(input.layout);
    height_idx  = dnn_get_height_idx_by_layout(input.layout);
    channel_idx = dnn_get_channel_idx_by_layout(input.layout);

    input.dims[height_idx] = task->in_frame->height;
    input.dims[width_idx]  = task->in_frame->width;

    input_shape[0] = input.dims[0];
    input_shape[1] = input.dims[channel_idx];
    input_shape[2] = input.dims[height_idx];
    input_shape[3] = input.dims[width_idx];

    input_tensor_size = input_shape[0] * input_shape[1] * input_shape[2] * input_shape[3];
    input_tensor_size *= sizeof(float);

    input.data = av_malloc(input_tensor_size);
    if (!input.data) {
        ret = AVERROR(ENOMEM);
        goto err;
    }
    infer_request->input_data = input.data;

    switch (onnx_model->model.func_type) {
    case DFT_PROCESS_FRAME:
        input.scale = 255;
        if (task->do_ioproc) {
            if (onnx_model->model.frame_pre_proc != NULL) {
                onnx_model->model.frame_pre_proc(task->in_frame, &input, onnx_model->model.filter_ctx);
            } else {
                ff_proc_from_frame_to_dnn(task->in_frame, &input, ctx);
            }
        }
        break;
    case DFT_ANALYTICS_DETECT:
        ff_frame_to_dnn_detect(task->in_frame, &input, ctx);
        break;
    default:
        avpriv_report_missing_feature(ctx, "model function type %d", onnx_model->model.func_type);
        ret = AVERROR(ENOSYS);
        goto err;
    }

    status = g_ort->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &memory_info);
    if (status != NULL) {
        ret = AVERROR(ENOMEM);
        goto err;
    }

    status = g_ort->CreateTensorWithDataAsOrtValue(
        memory_info, input.data, input_tensor_size,
        input_shape, 4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
        &infer_request->input_tensor);

    g_ort->ReleaseMemoryInfo(memory_info);

    if (status != NULL) {
        const char *msg = g_ort->GetErrorMessage(status);
        av_log(ctx, AV_LOG_ERROR, "Failed to create input tensor: %s\n", msg);
        g_ort->ReleaseStatus(status);
        ret = AVERROR(ENOMEM);
        goto err;
    }

    return 0;

err:
    onnx_free_request(infer_request);
    return ret;
}

static int onnx_start_inference(void *args)
{
    ONNXRequestItem        *request = (ONNXRequestItem *)args;
    ONNXInferRequest *infer_request = NULL;
    LastLevelTaskItem       *lltask = NULL;
    TaskItem                  *task = NULL;
    ONNXModel           *onnx_model = NULL;
    DnnContext                 *ctx = NULL;
    OrtStatus *status;
    const char  *input_names[1];
    const char *output_names[1];

    if (!request) {
        av_log(NULL, AV_LOG_ERROR, "ONNXRequestItem is NULL\n");
        return AVERROR(EINVAL);
    }

    infer_request = request->infer_request;
    lltask = request->lltask;
    task = lltask->task;
    onnx_model = (ONNXModel *)task->model;
    ctx = onnx_model->ctx;

    if (task->nb_output > 1) {
        avpriv_report_missing_feature(ctx,
            "Multiple output tensors (%u) for ONNX backend", task->nb_output);
        return AVERROR(ENOSYS);
    }

    if (!task->input_name || !task->output_names || !task->output_names[0]) {
        av_log(ctx, AV_LOG_ERROR,
               "ONNX backend: input/output tensor name was not resolved at load time\n");
        return AVERROR(EINVAL);
    }

    if (!infer_request->input_tensor) {
        av_log(ctx, AV_LOG_ERROR, "Input tensor is NULL\n");
        return DNN_GENERIC_ERROR;
    }

    if (!onnx_model->output_resolved) {
        size_t output_count = 0;
        int    found_output = 0;

        status = g_ort->SessionGetOutputCount(onnx_model->session, &output_count);
        if (status != NULL) {
            const char *msg = g_ort->GetErrorMessage(status);
            av_log(ctx, AV_LOG_ERROR, "Failed to get output count: %s\n", msg);
            g_ort->ReleaseStatus(status);
            return AVERROR(EINVAL);
        }

        for (size_t i = 0; i < output_count; i++) {
            char *name = NULL;
            status = g_ort->SessionGetOutputName(onnx_model->session, i,
                                                 onnx_model->allocator, &name);
            if (status != NULL) {
                g_ort->ReleaseStatus(status);
                continue;
            }
            if (!strcmp(name, task->output_names[0]))
                found_output = 1;
            onnx_model->allocator->Free(onnx_model->allocator, name);
            if (found_output)
                break;
        }

        if (!found_output) {
            av_log(ctx, AV_LOG_ERROR,
                   "Output name '%s' not found in ONNX model\n",
                   task->output_names[0]);
            return AVERROR(EINVAL);
        }

        onnx_model->output_resolved = 1;
    }

    input_names[0]  = task->input_name;
    output_names[0] = task->output_names[0];

    status = g_ort->Run(onnx_model->session, NULL,
                        input_names, (const OrtValue *const *)&infer_request->input_tensor, 1,
                        output_names, 1, &infer_request->output_tensor);

    if (status != NULL) {
        const char *msg = g_ort->GetErrorMessage(status);
        av_log(ctx, AV_LOG_ERROR, "ONNX inference failed: %s\n", msg);
        g_ort->ReleaseStatus(status);
        return DNN_GENERIC_ERROR;
    }

    return 0;
}

static void infer_completion_callback(void *args)
{
    ONNXRequestItem  *request = (ONNXRequestItem *)args;
    LastLevelTaskItem *lltask = request->lltask;
    TaskItem            *task = lltask->task;
    DNNData           outputs = { 0 };
    ONNXInferRequest *infer_request = request->infer_request;
    ONNXModel           *onnx_model = (ONNXModel *)task->model;
    DnnContext                 *ctx = onnx_model->ctx;
    OrtTensorTypeAndShapeInfo *tensor_info;
    ONNXTensorElementDataType tensor_type;
    size_t num_dims;
    int64_t *dims;
    void *output_data;
    OrtStatus *status;

    if (!infer_request->output_tensor) {
        av_log(ctx, AV_LOG_ERROR, "Output tensor is NULL\n");
        goto err;
    }

    status = g_ort->GetTensorTypeAndShape(infer_request->output_tensor, &tensor_info);
    if (status != NULL) {
        av_log(ctx, AV_LOG_ERROR, "Failed to get output tensor info\n");
        g_ort->ReleaseStatus(status);
        goto err;
    }

    g_ort->GetDimensionsCount(tensor_info, &num_dims);
    dims = av_malloc(num_dims * sizeof(int64_t));
    if (!dims) {
        av_log(ctx, AV_LOG_ERROR, "Failed to allocate memory for dimensions\n");
        g_ort->ReleaseTensorTypeAndShapeInfo(tensor_info);
        goto err;
    }
    g_ort->GetDimensions(tensor_info, dims, num_dims);

    /* Output is interpreted as NCHW, matching the input assumption. */
    outputs.layout = DL_NCHW;
    outputs.order = DCO_RGB;

    g_ort->GetTensorElementType(tensor_info, &tensor_type);
    if (tensor_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
        outputs.dt = DNN_FLOAT;
    } else {
        av_log(ctx, AV_LOG_ERROR, "Unsupported output tensor data type, only float is supported\n");
        av_free(dims);
        g_ort->ReleaseTensorTypeAndShapeInfo(tensor_info);
        goto err;
    }

    if (num_dims == 4) {
        outputs.dims[0] = dims[0];
        outputs.dims[1] = dims[1];
        outputs.dims[2] = dims[2];
        outputs.dims[3] = dims[3];
    } else {
        avpriv_report_missing_feature(ctx, "Support for %zu dimensional output", num_dims);
        av_free(dims);
        g_ort->ReleaseTensorTypeAndShapeInfo(tensor_info);
        goto err;
    }

    status = g_ort->GetTensorMutableData(infer_request->output_tensor, &output_data);
    if (status != NULL) {
        av_log(ctx, AV_LOG_ERROR, "Failed to get tensor data\n");
        g_ort->ReleaseStatus(status);
        av_free(dims);
        g_ort->ReleaseTensorTypeAndShapeInfo(tensor_info);
        goto err;
    }

    outputs.data = output_data;

    switch (onnx_model->model.func_type) {
    case DFT_PROCESS_FRAME:
        if (task->do_ioproc) {
            outputs.scale = 255;
            if (onnx_model->model.frame_post_proc != NULL) {
                onnx_model->model.frame_post_proc(task->out_frame, &outputs, onnx_model->model.filter_ctx);
            } else {
                ff_proc_from_dnn_to_frame(task->out_frame, &outputs, ctx);
            }
        } else {
            task->out_frame->width = outputs.dims[dnn_get_width_idx_by_layout(outputs.layout)];
            task->out_frame->height = outputs.dims[dnn_get_height_idx_by_layout(outputs.layout)];
        }
        break;
    default:
        avpriv_report_missing_feature(ctx, "model function type %d", onnx_model->model.func_type);
        av_free(dims);
        g_ort->ReleaseTensorTypeAndShapeInfo(tensor_info);
        goto err;
    }

    av_free(dims);
    g_ort->ReleaseTensorTypeAndShapeInfo(tensor_info);
    task->inference_done++;

err:
    av_freep(&request->lltask);
    onnx_free_request(infer_request);
    if (ff_safe_queue_push_back(onnx_model->request_queue, request) < 0) {
        destroy_request_item(&request);
        av_log(ctx, AV_LOG_ERROR, "Unable to push back request_queue.\n");
    }
}

static int execute_model_onnx(ONNXRequestItem *request, Queue *lltask_queue)
{
    ONNXModel *onnx_model = NULL;
    LastLevelTaskItem *lltask;
    TaskItem *task = NULL;
    int ret = 0;

    if (ff_queue_size(lltask_queue) == 0) {
        destroy_request_item(&request);
        return 0;
    }

    lltask = (LastLevelTaskItem *)ff_queue_peek_front(lltask_queue);
    if (lltask == NULL) {
        av_log(NULL, AV_LOG_ERROR, "Failed to get LastLevelTaskItem\n");
        destroy_request_item(&request);
        return AVERROR(EINVAL);
    }
    task = lltask->task;
    onnx_model = (ONNXModel *)task->model;

    ret = fill_model_input_onnx(onnx_model, request);
    if (ret != 0) {
        goto err;
    }

    if (task->async) {
        avpriv_report_missing_feature(onnx_model->ctx, "ONNX async inference");
        ret = AVERROR(ENOSYS);
        goto err;
    } else {
        ret = onnx_start_inference((void *)request);
        if (ret != 0) {
            goto err;
        }
        infer_completion_callback(request);
        return (task->inference_done == task->inference_todo) ? 0 : DNN_GENERIC_ERROR;
    }

err:
    av_freep(&request->lltask);
    onnx_free_request(request->infer_request);
    if (ff_safe_queue_push_back(onnx_model->request_queue, request) < 0) {
        destroy_request_item(&request);
    }
    return ret;
}

static int get_output_onnx(DNNModel *model, const char *input_name, int input_width, int input_height,
                           const char *output_name, int *output_width, int *output_height)
{
    int ret = 0;
    ONNXModel    *onnx_model = (ONNXModel *)model;
    DnnContext          *ctx = onnx_model->ctx;
    TaskItem            task = { 0 };
    ONNXRequestItem *request = NULL;
    DNNExecBaseParams exec_params = {
        .input_name   = input_name,
        .output_names = &output_name,
        .nb_output    = 1,
        .in_frame     = NULL,
        .out_frame    = NULL,
    };

    ret = ff_dnn_fill_gettingoutput_task(&task, &exec_params, onnx_model, input_height, input_width, ctx);
    if (ret != 0) {
        goto err;
    }

    ret = extract_lltask_from_task(&task, onnx_model->lltask_queue);
    if (ret != 0) {
        av_log(ctx, AV_LOG_ERROR, "Unable to extract last level task from task.\n");
        goto err;
    }

    request = (ONNXRequestItem *)ff_safe_queue_pop_front(onnx_model->request_queue);
    if (!request) {
        av_log(ctx, AV_LOG_ERROR, "Unable to get infer request.\n");
        ret = AVERROR(EINVAL);
        goto err;
    }

    ret = execute_model_onnx(request, onnx_model->lltask_queue);
    *output_width = task.out_frame->width;
    *output_height = task.out_frame->height;

err:
    av_frame_free(&task.out_frame);
    av_frame_free(&task.in_frame);
    return ret;
}

static ONNXInferRequest *onnx_create_inference_request(void)
{
    ONNXInferRequest *request = av_malloc(sizeof(ONNXInferRequest));
    if (!request)
        return NULL;
    request->input_tensor  = NULL;
    request->output_tensor = NULL;
    request->input_data    = NULL;
    return request;
}

static DNNModel *dnn_load_model_onnx(DnnContext *ctx, DNNFunctionType func_type, AVFilterContext *filter_ctx)
{
    DNNModel       *model = NULL;
    ONNXModel *onnx_model = NULL;
    ONNXRequestItem *item = NULL;
    ONNXOptions  *options = &ctx->onnx_option;
    OrtStatus *status;

    ff_thread_once(&g_ort_init_once, init_ort_api);
    if (!g_ort) {
        av_log(ctx, AV_LOG_ERROR, "Failed to get ONNX Runtime API\n");
        return NULL;
    }

    onnx_model = av_mallocz(sizeof(ONNXModel));
    if (!onnx_model)
        return NULL;

    model = &onnx_model->model;
    onnx_model->ctx = ctx;

    status = g_ort->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "FFmpeg", &onnx_model->env);
    if (status != NULL) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create ONNX Runtime environment\n");
        goto fail;
    }

    status = g_ort->CreateSessionOptions(&onnx_model->session_options);
    if (status != NULL) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create session options\n");
        goto fail;
    }

    if (options->num_threads > 0 &&
        (!ctx->device || av_strcasecmp(ctx->device, "cpu") == 0)) {
        g_ort->SetIntraOpNumThreads(onnx_model->session_options, options->num_threads);
    }
    g_ort->SetSessionGraphOptimizationLevel(onnx_model->session_options, ORT_ENABLE_ALL);

    if (ctx->device && av_strcasecmp(ctx->device, "cpu") != 0) {
        if (av_strcasecmp(ctx->device, "cuda") == 0) {
            if (g_ort->SessionOptionsAppendExecutionProvider_CUDA) {
                OrtCUDAProviderOptions cuda_options;
                memset(&cuda_options, 0, sizeof(cuda_options));
                cuda_options.device_id = ctx->device_id;

                status = g_ort->SessionOptionsAppendExecutionProvider_CUDA(
                    onnx_model->session_options, &cuda_options);
                if (status != NULL) {
                    const char *msg = g_ort->GetErrorMessage(status);
                    av_log(ctx, AV_LOG_WARNING, "Failed to enable CUDA (device %d): %s. Falling back to CPU\n",
                           ctx->device_id, msg);
                    g_ort->ReleaseStatus(status);
                } else {
                    av_log(ctx, AV_LOG_INFO, "Using CUDA execution provider on device %d\n", ctx->device_id);
                }
            } else {
                av_log(ctx, AV_LOG_WARNING, "CUDA provider function not available in this ONNX Runtime API version. Falling back to CPU\n");
            }
        } else if (av_strcasecmp(ctx->device, "dml") == 0) {
#ifdef _WIN32
            const char* dml_options_keys[] = {"device_id"};
            const char* dml_options_values[] = {NULL};
            char device_id_str[32];
            snprintf(device_id_str, sizeof(device_id_str), "%d", ctx->device_id);
            dml_options_values[0] = device_id_str;

            /* DirectML cannot use ORT's memory-pattern optimizer and only
             * supports sequential execution. */
            status = g_ort->SetSessionExecutionMode(onnx_model->session_options, ORT_SEQUENTIAL);
            if (status)
                g_ort->ReleaseStatus(status);
            status = g_ort->DisableMemPattern(onnx_model->session_options);
            if (status)
                g_ort->ReleaseStatus(status);

            if (g_ort->SessionOptionsAppendExecutionProvider) {
                status = g_ort->SessionOptionsAppendExecutionProvider(
                    onnx_model->session_options, "DML",
                    dml_options_keys, dml_options_values, 1);
                if (status != NULL) {
                    const char *msg = g_ort->GetErrorMessage(status);
                    av_log(ctx, AV_LOG_WARNING, "Failed to enable DirectML (device %d): %s. Falling back to CPU\n",
                           ctx->device_id, msg);
                    g_ort->ReleaseStatus(status);
                } else {
                    av_log(ctx, AV_LOG_INFO, "Using DirectML execution provider on device %d\n", ctx->device_id);
                }
            } else {
                av_log(ctx, AV_LOG_WARNING, "DirectML provider function not available in this ONNX Runtime API version. Falling back to CPU\n");
            }
#else
            av_log(ctx, AV_LOG_WARNING, "DirectML is only available on Windows. Falling back to CPU\n");
#endif
        } else if (av_strcasecmp(ctx->device, "vitisai") == 0) {
            if (g_ort->SessionOptionsAppendExecutionProvider) {
                status = g_ort->SessionOptionsAppendExecutionProvider(
                    onnx_model->session_options, "VitisAI",
                    NULL, NULL, 0);
                if (status != NULL) {
                    const char *msg = g_ort->GetErrorMessage(status);
                    av_log(ctx, AV_LOG_WARNING,
                           "Failed to enable VitisAI EP: %s. Falling back to CPU\n", msg);
                    g_ort->ReleaseStatus(status);
                } else {
                    av_log(ctx, AV_LOG_INFO, "Using VitisAI execution provider (AMD Ryzen AI NPU)\n");
                }
            } else {
                av_log(ctx, AV_LOG_WARNING,
                       "VitisAI provider function not available in this ONNX Runtime API version. Falling back to CPU.\n");
            }
        } else {
#ifdef _WIN32
            av_log(ctx, AV_LOG_WARNING,
                   "Unknown device '%s'. Supported: cpu, cuda, dml, vitisai. Using CPU\n",
                   ctx->device);
#else
            av_log(ctx, AV_LOG_WARNING,
                   "Unknown device '%s'. Supported: cpu, cuda, vitisai. Using CPU\n",
                   ctx->device);
#endif
        }
    } else {
        av_log(ctx, AV_LOG_INFO, "Using CPU execution provider\n");
    }

#ifdef _WIN32
    {
        wchar_t *wfilename = NULL;
        if (utf8towchar(ctx->model_filename, &wfilename)) {
            av_log(ctx, AV_LOG_ERROR, "Failed to convert model filename to UTF-16\n");
            goto fail;
        }
        if (!wfilename) {
            av_log(ctx, AV_LOG_ERROR, "Failed to convert model filename to UTF-16\n");
            goto fail;
        }

        status = g_ort->CreateSession(onnx_model->env, wfilename,
                                      onnx_model->session_options, &onnx_model->session);
        av_free(wfilename);
    }
#else
    status = g_ort->CreateSession(onnx_model->env, ctx->model_filename,
                                  onnx_model->session_options, &onnx_model->session);
#endif
    if (status != NULL) {
        const char *msg = g_ort->GetErrorMessage(status);
        av_log(ctx, AV_LOG_ERROR, "Failed to create ONNX session: %s\n", msg);
        g_ort->ReleaseStatus(status);
        goto fail;
    }

    status = g_ort->GetAllocatorWithDefaultOptions(&onnx_model->allocator);
    if (status != NULL) {
        av_log(ctx, AV_LOG_ERROR, "Failed to get allocator\n");
        goto fail;
    }

    /*
     * The ONNX backend binds exactly one input tensor to Run(), so only
     * single-input models are supported.
     */
    {
        size_t input_count = 0;
        status = g_ort->SessionGetInputCount(onnx_model->session, &input_count);
        if (status != NULL) {
            const char *msg = g_ort->GetErrorMessage(status);
            av_log(ctx, AV_LOG_ERROR, "Failed to get model input count: %s\n", msg);
            g_ort->ReleaseStatus(status);
            goto fail;
        }
        if (input_count == 0) {
            av_log(ctx, AV_LOG_ERROR, "ONNX model exposes no input tensors\n");
            goto fail;
        }
        if (input_count > 1) {
            av_log(ctx, AV_LOG_ERROR,
                   "ONNX model exposes %zu input tensors; the ONNX backend "
                   "supports single-input models only.\n",
                   input_count);
            goto fail;
        }
    }

    /* Auto-detect the input tensor name when the user did not pass input=NAME. */
    if (!ctx->model_inputname || !*ctx->model_inputname) {
        char *name = NULL;
        status = g_ort->SessionGetInputName(onnx_model->session, 0,
                                            onnx_model->allocator, &name);
        if (status != NULL) {
            const char *msg = g_ort->GetErrorMessage(status);
            av_log(ctx, AV_LOG_ERROR, "Failed to get model input name: %s\n", msg);
            g_ort->ReleaseStatus(status);
            goto fail;
        }
        av_freep(&ctx->model_inputname);
        ctx->model_inputname = av_strdup(name);
        onnx_model->allocator->Free(onnx_model->allocator, name);
        if (!ctx->model_inputname)
            goto fail;
        av_log(ctx, AV_LOG_INFO, "Auto-detected ONNX input tensor '%s'\n",
               ctx->model_inputname);
    }

    /* Auto-detect the output tensor name when the user did not pass output=NAME. */
    if (!ctx->model_outputnames) {
        size_t output_count = 0;
        char *name = NULL;
        status = g_ort->SessionGetOutputCount(onnx_model->session, &output_count);
        if (status != NULL) {
            const char *msg = g_ort->GetErrorMessage(status);
            av_log(ctx, AV_LOG_ERROR, "Failed to get model output count: %s\n", msg);
            g_ort->ReleaseStatus(status);
            goto fail;
        }
        if (output_count == 0) {
            av_log(ctx, AV_LOG_ERROR, "ONNX model exposes no output tensors\n");
            goto fail;
        }
        status = g_ort->SessionGetOutputName(onnx_model->session, 0,
                                             onnx_model->allocator, &name);
        if (status != NULL) {
            const char *msg = g_ort->GetErrorMessage(status);
            av_log(ctx, AV_LOG_ERROR, "Failed to get model output name: %s\n", msg);
            g_ort->ReleaseStatus(status);
            goto fail;
        }
        ctx->model_outputnames = av_calloc(1, sizeof(*ctx->model_outputnames));
        if (!ctx->model_outputnames) {
            onnx_model->allocator->Free(onnx_model->allocator, name);
            goto fail;
        }
        ctx->model_outputnames[0] = av_strdup(name);
        onnx_model->allocator->Free(onnx_model->allocator, name);
        if (!ctx->model_outputnames[0]) {
            av_freep(&ctx->model_outputnames);
            goto fail;
        }
        ctx->nb_outputs = 1;
        if (output_count == 1) {
            av_log(ctx, AV_LOG_INFO, "Auto-detected ONNX output tensor '%s'\n",
                   ctx->model_outputnames[0]);
        } else {
            av_log(ctx, AV_LOG_WARNING,
                   "ONNX model exposes %zu output tensors; auto-using index 0 ('%s'). "
                   "Specify output=NAME to choose a different one.\n",
                   output_count, ctx->model_outputnames[0]);
        }
    }

    onnx_model->request_queue = ff_safe_queue_create();
    if (!onnx_model->request_queue) {
        goto fail;
    }

    item = av_mallocz(sizeof(ONNXRequestItem));
    if (!item) {
        goto fail;
    }
    item->lltask = NULL;
    item->infer_request = onnx_create_inference_request();
    if (!item->infer_request) {
        av_log(ctx, AV_LOG_ERROR, "Failed to allocate memory for ONNX inference request\n");
        goto fail;
    }
    item->exec_module.start_inference = &onnx_start_inference;
    item->exec_module.callback = &infer_completion_callback;
    item->exec_module.args = item;

    if (ff_safe_queue_push_back(onnx_model->request_queue, item) < 0) {
        goto fail;
    }
    item = NULL;

    onnx_model->task_queue = ff_queue_create();
    if (!onnx_model->task_queue) {
        goto fail;
    }

    onnx_model->lltask_queue = ff_queue_create();
    if (!onnx_model->lltask_queue) {
        goto fail;
    }

    model->get_input  = &get_input_onnx;
    model->get_output = &get_output_onnx;
    model->filter_ctx = filter_ctx;
    model->func_type  = func_type;

    return model;

fail:
    if (item) {
        destroy_request_item(&item);
    }
    dnn_free_model_onnx(&model);
    return NULL;
}

static int dnn_execute_model_onnx(const DNNModel *model, DNNExecBaseParams *exec_params)
{
    ONNXModel *onnx_model = (ONNXModel *)model;
    DnnContext *ctx = onnx_model->ctx;
    TaskItem *task;
    ONNXRequestItem *request;
    int ret = 0;

    ret = ff_check_exec_params(ctx, DNN_ONNX, model->func_type, exec_params);
    if (ret != 0) {
        av_log(ctx, AV_LOG_ERROR, "Exec parameter checking failed.\n");
        return ret;
    }

    task = av_malloc(sizeof(TaskItem));
    if (!task) {
        av_log(ctx, AV_LOG_ERROR, "Unable to alloc memory for task item.\n");
        return AVERROR(ENOMEM);
    }

    ret = ff_dnn_fill_task(task, exec_params, onnx_model, 0, 1);
    if (ret != 0) {
        av_freep(&task);
        av_log(ctx, AV_LOG_ERROR, "Unable to fill task.\n");
        return ret;
    }

    ret = ff_queue_push_back(onnx_model->task_queue, task);
    if (ret < 0) {
        av_freep(&task);
        av_log(ctx, AV_LOG_ERROR, "Unable to push back task_queue.\n");
        return ret;
    }

    ret = extract_lltask_from_task(task, onnx_model->lltask_queue);
    if (ret != 0) {
        av_log(ctx, AV_LOG_ERROR, "Unable to extract last level task from task.\n");
        return ret;
    }

    request = (ONNXRequestItem *)ff_safe_queue_pop_front(onnx_model->request_queue);
    if (!request) {
        av_log(ctx, AV_LOG_ERROR, "Unable to get infer request.\n");
        return AVERROR(EINVAL);
    }

    return execute_model_onnx(request, onnx_model->lltask_queue);
}

static DNNAsyncStatusType dnn_get_result_onnx(const DNNModel *model, AVFrame **in, AVFrame **out)
{
    ONNXModel *onnx_model = (ONNXModel *)model;
    return ff_dnn_get_result_common(onnx_model->task_queue, in, out);
}

static int dnn_flush_onnx(const DNNModel *model)
{
    ONNXModel *onnx_model = (ONNXModel *)model;
    ONNXRequestItem *request;

    if (ff_queue_size(onnx_model->lltask_queue) == 0)
        return 0;

    request = (ONNXRequestItem *)ff_safe_queue_pop_front(onnx_model->request_queue);
    if (!request) {
        av_log(onnx_model->ctx, AV_LOG_ERROR, "Unable to get infer request.\n");
        return AVERROR(EINVAL);
    }

    return execute_model_onnx(request, onnx_model->lltask_queue);
}

const DNNModule ff_dnn_backend_onnx = {
    .clazz = DNN_DEFINE_CLASS(dnn_onnx),
    .type = DNN_ONNX,
    .load_model = dnn_load_model_onnx,
    .execute_model = dnn_execute_model_onnx,
    .get_result = dnn_get_result_onnx,
    .flush = dnn_flush_onnx,
    .free_model = dnn_free_model_onnx,
};
