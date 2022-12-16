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
#include "libavutil/cpu.h"
#include "libavutil/opt.h"
#include "libavutil/avstring.h"
#include "libavutil/detection_bbox.h"
#include "../internal.h"
#include "safe_queue.h"
#include <c_api/ie_c_api.h>
#include "dnn_backend_common.h"

typedef struct OVOptions{
    char *device_type;
    int nireq;
    uint8_t async;
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
    SafeQueue *request_queue;   // holds OVRequestItem
    Queue *task_queue;          // holds TaskItem
    Queue *lltask_queue;     // holds LastLevelTaskItem
    const char *all_input_names;
    const char *all_output_names;
} OVModel;

// one request for one call to openvino
typedef struct OVRequestItem {
    ie_infer_request_t *infer_request;
    LastLevelTaskItem **lltasks;
    uint32_t lltask_count;
    ie_complete_call_back_t callback;
} OVRequestItem;

#define APPEND_STRING(generated_string, iterate_string)                                            \
    generated_string = generated_string ? av_asprintf("%s %s", generated_string, iterate_string) : \
                                          av_asprintf("%s", iterate_string);

#define OFFSET(x) offsetof(OVContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM
static const AVOption dnn_openvino_options[] = {
    { "device", "device to run model", OFFSET(options.device_type), AV_OPT_TYPE_STRING, { .str = "CPU" }, 0, 0, FLAGS },
    DNN_BACKEND_COMMON_OPTIONS
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
    case U8:
        return DNN_UINT8;
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
    case DNN_UINT8:
        return sizeof(uint8_t);
    default:
        av_assert0(!"not supported yet.");
        return 1;
    }
}

static int fill_model_input_ov(OVModel *ov_model, OVRequestItem *request)
{
    dimensions_t dims;
    precision_e precision;
    ie_blob_buffer_t blob_buffer;
    OVContext *ctx = &ov_model->ctx;
    IEStatusCode status;
    DNNData input;
    ie_blob_t *input_blob = NULL;
    LastLevelTaskItem *lltask;
    TaskItem *task;

    lltask = ff_queue_peek_front(ov_model->lltask_queue);
    av_assert0(lltask);
    task = lltask->task;

    status = ie_infer_request_get_blob(request->infer_request, task->input_name, &input_blob);
    if (status != OK) {
        av_log(ctx, AV_LOG_ERROR, "Failed to get input blob with name %s\n", task->input_name);
        return DNN_GENERIC_ERROR;
    }

    status |= ie_blob_get_dims(input_blob, &dims);
    status |= ie_blob_get_precision(input_blob, &precision);
    if (status != OK) {
        ie_blob_free(&input_blob);
        av_log(ctx, AV_LOG_ERROR, "Failed to get input blob dims/precision\n");
        return DNN_GENERIC_ERROR;
    }

    status = ie_blob_get_buffer(input_blob, &blob_buffer);
    if (status != OK) {
        ie_blob_free(&input_blob);
        av_log(ctx, AV_LOG_ERROR, "Failed to get input blob buffer\n");
        return DNN_GENERIC_ERROR;
    }

    input.height = dims.dims[2];
    input.width = dims.dims[3];
    input.channels = dims.dims[1];
    input.data = blob_buffer.buffer;
    input.dt = precision_to_datatype(precision);
    // all models in openvino open model zoo use BGR as input,
    // change to be an option when necessary.
    input.order = DCO_BGR;

    for (int i = 0; i < ctx->options.batch_size; ++i) {
        lltask = ff_queue_pop_front(ov_model->lltask_queue);
        if (!lltask) {
            break;
        }
        request->lltasks[i] = lltask;
        request->lltask_count = i + 1;
        task = lltask->task;
        switch (ov_model->model->func_type) {
        case DFT_PROCESS_FRAME:
            if (task->do_ioproc) {
                if (ov_model->model->frame_pre_proc != NULL) {
                    ov_model->model->frame_pre_proc(task->in_frame, &input, ov_model->model->filter_ctx);
                } else {
                    ff_proc_from_frame_to_dnn(task->in_frame, &input, ctx);
                }
            }
            break;
        case DFT_ANALYTICS_DETECT:
            ff_frame_to_dnn_detect(task->in_frame, &input, ctx);
            break;
        case DFT_ANALYTICS_CLASSIFY:
            ff_frame_to_dnn_classify(task->in_frame, &input, lltask->bbox_index, ctx);
            break;
        default:
            av_assert0(!"should not reach here");
            break;
        }
        input.data = (uint8_t *)input.data
                     + input.width * input.height * input.channels * get_datatype_size(input.dt);
    }
    ie_blob_free(&input_blob);

    return 0;
}

static void infer_completion_callback(void *args)
{
    dimensions_t dims;
    precision_e precision;
    IEStatusCode status;
    OVRequestItem *request = args;
    LastLevelTaskItem *lltask = request->lltasks[0];
    TaskItem *task = lltask->task;
    OVModel *ov_model = task->model;
    SafeQueue *requestq = ov_model->request_queue;
    ie_blob_t *output_blob = NULL;
    ie_blob_buffer_t blob_buffer;
    DNNData output;
    OVContext *ctx = &ov_model->ctx;

    status = ie_infer_request_get_blob(request->infer_request, task->output_names[0], &output_blob);
    if (status != OK) {
        av_log(ctx, AV_LOG_ERROR,
               "output \"%s\" may not correct, all output(s) are: \"%s\"\n",
               task->output_names[0], ov_model->all_output_names);
        return;
    }

    status = ie_blob_get_buffer(output_blob, &blob_buffer);
    if (status != OK) {
        ie_blob_free(&output_blob);
        av_log(ctx, AV_LOG_ERROR, "Failed to access output memory\n");
        return;
    }

    status |= ie_blob_get_dims(output_blob, &dims);
    status |= ie_blob_get_precision(output_blob, &precision);
    if (status != OK) {
        ie_blob_free(&output_blob);
        av_log(ctx, AV_LOG_ERROR, "Failed to get dims or precision of output\n");
        return;
    }

    output.channels = dims.dims[1];
    output.height   = dims.dims[2];
    output.width    = dims.dims[3];
    output.dt       = precision_to_datatype(precision);
    output.data     = blob_buffer.buffer;

    av_assert0(request->lltask_count <= dims.dims[0]);
    av_assert0(request->lltask_count >= 1);
    for (int i = 0; i < request->lltask_count; ++i) {
        task = request->lltasks[i]->task;

        switch (ov_model->model->func_type) {
        case DFT_PROCESS_FRAME:
            if (task->do_ioproc) {
                if (ov_model->model->frame_post_proc != NULL) {
                    ov_model->model->frame_post_proc(task->out_frame, &output, ov_model->model->filter_ctx);
                } else {
                    ff_proc_from_dnn_to_frame(task->out_frame, &output, ctx);
                }
            } else {
                task->out_frame->width = output.width;
                task->out_frame->height = output.height;
            }
            break;
        case DFT_ANALYTICS_DETECT:
            if (!ov_model->model->detect_post_proc) {
                av_log(ctx, AV_LOG_ERROR, "detect filter needs to provide post proc\n");
                return;
            }
            ov_model->model->detect_post_proc(task->in_frame, &output, 1, ov_model->model->filter_ctx);
            break;
        case DFT_ANALYTICS_CLASSIFY:
            if (!ov_model->model->classify_post_proc) {
                av_log(ctx, AV_LOG_ERROR, "classify filter needs to provide post proc\n");
                return;
            }
            ov_model->model->classify_post_proc(task->in_frame, &output, request->lltasks[i]->bbox_index, ov_model->model->filter_ctx);
            break;
        default:
            av_assert0(!"should not reach here");
            break;
        }

        task->inference_done++;
        av_freep(&request->lltasks[i]);
        output.data = (uint8_t *)output.data
                      + output.width * output.height * output.channels * get_datatype_size(output.dt);
    }
    ie_blob_free(&output_blob);

    request->lltask_count = 0;
    if (ff_safe_queue_push_back(requestq, request) < 0) {
        ie_infer_request_free(&request->infer_request);
        av_freep(&request);
        av_log(ctx, AV_LOG_ERROR, "Failed to push back request_queue.\n");
        return;
    }
}

static int init_model_ov(OVModel *ov_model, const char *input_name, const char *output_name)
{
    int ret = 0;
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
        if (status != OK) {
            ret = DNN_GENERIC_ERROR;
            goto err;
        }
        for (int i = 0; i < input_shapes.shape_num; i++)
            input_shapes.shapes[i].shape.dims[0] = ctx->options.batch_size;
        status = ie_network_reshape(ov_model->network, input_shapes);
        ie_network_input_shapes_free(&input_shapes);
        if (status != OK) {
            ret = DNN_GENERIC_ERROR;
            goto err;
        }
    }

    // The order of dims in the openvino is fixed and it is always NCHW for 4-D data.
    // while we pass NHWC data from FFmpeg to openvino
    status = ie_network_set_input_layout(ov_model->network, input_name, NHWC);
    if (status != OK) {
        if (status == NOT_FOUND) {
            av_log(ctx, AV_LOG_ERROR, "Could not find \"%s\" in model, failed to set input layout as NHWC, "\
                                      "all input(s) are: \"%s\"\n", input_name, ov_model->all_input_names);
        } else{
            av_log(ctx, AV_LOG_ERROR, "Failed to set layout as NHWC for input %s\n", input_name);
        }
        ret = DNN_GENERIC_ERROR;
        goto err;
    }
    status = ie_network_set_output_layout(ov_model->network, output_name, NHWC);
    if (status != OK) {
        if (status == NOT_FOUND) {
            av_log(ctx, AV_LOG_ERROR, "Could not find \"%s\" in model, failed to set output layout as NHWC, "\
                                      "all output(s) are: \"%s\"\n", input_name, ov_model->all_output_names);
        } else{
            av_log(ctx, AV_LOG_ERROR, "Failed to set layout as NHWC for output %s\n", output_name);
        }
        ret = DNN_GENERIC_ERROR;
        goto err;
    }

    // all models in openvino open model zoo use BGR with range [0.0f, 255.0f] as input,
    // we don't have a AVPixelFormat to describe it, so we'll use AV_PIX_FMT_BGR24 and
    // ask openvino to do the conversion internally.
    // the current supported SR model (frame processing) is generated from tensorflow model,
    // and its input is Y channel as float with range [0.0f, 1.0f], so do not set for this case.
    // TODO: we need to get a final clear&general solution with all backends/formats considered.
    if (ov_model->model->func_type != DFT_PROCESS_FRAME) {
        status = ie_network_set_input_precision(ov_model->network, input_name, U8);
        if (status != OK) {
            av_log(ctx, AV_LOG_ERROR, "Failed to set input precision as U8 for %s\n", input_name);
            ret = DNN_GENERIC_ERROR;
            goto err;
        }
    }

    status = ie_core_load_network(ov_model->core, ov_model->network, ctx->options.device_type, &config, &ov_model->exe_network);
    if (status != OK) {
        av_log(ctx, AV_LOG_ERROR, "Failed to load OpenVINO model network\n");
        status = ie_core_get_available_devices(ov_model->core, &a_dev);
        if (status != OK) {
            av_log(ctx, AV_LOG_ERROR, "Failed to get available devices\n");
            ret = DNN_GENERIC_ERROR;
            goto err;
        }
        for (int i = 0; i < a_dev.num_devices; i++) {
            APPEND_STRING(all_dev_names, a_dev.devices[i])
        }
        av_log(ctx, AV_LOG_ERROR,"device %s may not be supported, all available devices are: \"%s\"\n",
               ctx->options.device_type, all_dev_names);
        ret = AVERROR(ENODEV);
        goto err;
    }

    // create infer_requests for async execution
    if (ctx->options.nireq <= 0) {
        // the default value is a rough estimation
        ctx->options.nireq = av_cpu_count() / 2 + 1;
    }

    ov_model->request_queue = ff_safe_queue_create();
    if (!ov_model->request_queue) {
        ret = AVERROR(ENOMEM);
        goto err;
    }

    for (int i = 0; i < ctx->options.nireq; i++) {
        OVRequestItem *item = av_mallocz(sizeof(*item));
        if (!item) {
            ret = AVERROR(ENOMEM);
            goto err;
        }

        item->callback.completeCallBackFunc = infer_completion_callback;
        item->callback.args = item;
        if (ff_safe_queue_push_back(ov_model->request_queue, item) < 0) {
            av_freep(&item);
            ret = AVERROR(ENOMEM);
            goto err;
        }

        status = ie_exec_network_create_infer_request(ov_model->exe_network, &item->infer_request);
        if (status != OK) {
            ret = DNN_GENERIC_ERROR;
            goto err;
        }

        item->lltasks = av_malloc_array(ctx->options.batch_size, sizeof(*item->lltasks));
        if (!item->lltasks) {
            ret = AVERROR(ENOMEM);
            goto err;
        }
        item->lltask_count = 0;
    }

    ov_model->task_queue = ff_queue_create();
    if (!ov_model->task_queue) {
        ret = AVERROR(ENOMEM);
        goto err;
    }

    ov_model->lltask_queue = ff_queue_create();
    if (!ov_model->lltask_queue) {
        ret = AVERROR(ENOMEM);
        goto err;
    }

    return 0;

err:
    ff_dnn_free_model_ov(&ov_model->model);
    return ret;
}

static int execute_model_ov(OVRequestItem *request, Queue *inferenceq)
{
    IEStatusCode status;
    LastLevelTaskItem *lltask;
    int ret = 0;
    TaskItem *task;
    OVContext *ctx;
    OVModel *ov_model;

    if (ff_queue_size(inferenceq) == 0) {
        ie_infer_request_free(&request->infer_request);
        av_freep(&request);
        return 0;
    }

    lltask = ff_queue_peek_front(inferenceq);
    task = lltask->task;
    ov_model = task->model;
    ctx = &ov_model->ctx;

    if (task->async) {
        ret = fill_model_input_ov(ov_model, request);
        if (ret != 0) {
            goto err;
        }
        status = ie_infer_set_completion_callback(request->infer_request, &request->callback);
        if (status != OK) {
            av_log(ctx, AV_LOG_ERROR, "Failed to set completion callback for inference\n");
            ret = DNN_GENERIC_ERROR;
            goto err;
        }
        status = ie_infer_request_infer_async(request->infer_request);
        if (status != OK) {
            av_log(ctx, AV_LOG_ERROR, "Failed to start async inference\n");
            ret = DNN_GENERIC_ERROR;
            goto err;
        }
        return 0;
    } else {
        ret = fill_model_input_ov(ov_model, request);
        if (ret != 0) {
            goto err;
        }
        status = ie_infer_request_infer(request->infer_request);
        if (status != OK) {
            av_log(ctx, AV_LOG_ERROR, "Failed to start synchronous model inference\n");
            ret = DNN_GENERIC_ERROR;
            goto err;
        }
        infer_completion_callback(request);
        return (task->inference_done == task->inference_todo) ? 0 : DNN_GENERIC_ERROR;
    }
err:
    if (ff_safe_queue_push_back(ov_model->request_queue, request) < 0) {
        ie_infer_request_free(&request->infer_request);
        av_freep(&request);
    }
    return ret;
}

static int get_input_ov(void *model, DNNData *input, const char *input_name)
{
    OVModel *ov_model = model;
    OVContext *ctx = &ov_model->ctx;
    char *model_input_name = NULL;
    IEStatusCode status;
    size_t model_input_count = 0;
    dimensions_t dims;
    precision_e precision;
    int input_resizable = ctx->options.input_resizable;

    status = ie_network_get_inputs_number(ov_model->network, &model_input_count);
    if (status != OK) {
        av_log(ctx, AV_LOG_ERROR, "Failed to get input count\n");
        return DNN_GENERIC_ERROR;
    }

    for (size_t i = 0; i < model_input_count; i++) {
        status = ie_network_get_input_name(ov_model->network, i, &model_input_name);
        if (status != OK) {
            av_log(ctx, AV_LOG_ERROR, "Failed to get No.%d input's name\n", (int)i);
            return DNN_GENERIC_ERROR;
        }
        if (strcmp(model_input_name, input_name) == 0) {
            ie_network_name_free(&model_input_name);
            status |= ie_network_get_input_dims(ov_model->network, input_name, &dims);
            status |= ie_network_get_input_precision(ov_model->network, input_name, &precision);
            if (status != OK) {
                av_log(ctx, AV_LOG_ERROR, "Failed to get No.%d input's dims or precision\n", (int)i);
                return DNN_GENERIC_ERROR;
            }

            input->channels = dims.dims[1];
            input->height   = input_resizable ? -1 : dims.dims[2];
            input->width    = input_resizable ? -1 : dims.dims[3];
            input->dt       = precision_to_datatype(precision);
            return 0;
        }

        ie_network_name_free(&model_input_name);
    }

    av_log(ctx, AV_LOG_ERROR, "Could not find \"%s\" in model, all input(s) are: \"%s\"\n", input_name, ov_model->all_input_names);
    return AVERROR(EINVAL);
}

static int contain_valid_detection_bbox(AVFrame *frame)
{
    AVFrameSideData *sd;
    const AVDetectionBBoxHeader *header;
    const AVDetectionBBox *bbox;

    sd = av_frame_get_side_data(frame, AV_FRAME_DATA_DETECTION_BBOXES);
    if (!sd) { // this frame has nothing detected
        return 0;
    }

    if (!sd->size) {
        return 0;
    }

    header = (const AVDetectionBBoxHeader *)sd->data;
    if (!header->nb_bboxes) {
        return 0;
    }

    for (uint32_t i = 0; i < header->nb_bboxes; i++) {
        bbox = av_get_detection_bbox(header, i);
        if (bbox->x < 0 || bbox->w < 0 || bbox->x + bbox->w >= frame->width) {
            return 0;
        }
        if (bbox->y < 0 || bbox->h < 0 || bbox->y + bbox->h >= frame->width) {
            return 0;
        }

        if (bbox->classify_count == AV_NUM_DETECTION_BBOX_CLASSIFY) {
            return 0;
        }
    }

    return 1;
}

static int extract_lltask_from_task(DNNFunctionType func_type, TaskItem *task, Queue *lltask_queue, DNNExecBaseParams *exec_params)
{
    switch (func_type) {
    case DFT_PROCESS_FRAME:
    case DFT_ANALYTICS_DETECT:
    {
        LastLevelTaskItem *lltask = av_malloc(sizeof(*lltask));
        if (!lltask) {
            return AVERROR(ENOMEM);
        }
        task->inference_todo = 1;
        task->inference_done = 0;
        lltask->task = task;
        if (ff_queue_push_back(lltask_queue, lltask) < 0) {
            av_freep(&lltask);
            return AVERROR(ENOMEM);
        }
        return 0;
    }
    case DFT_ANALYTICS_CLASSIFY:
    {
        const AVDetectionBBoxHeader *header;
        AVFrame *frame = task->in_frame;
        AVFrameSideData *sd;
        DNNExecClassificationParams *params = (DNNExecClassificationParams *)exec_params;

        task->inference_todo = 0;
        task->inference_done = 0;

        if (!contain_valid_detection_bbox(frame)) {
            return 0;
        }

        sd = av_frame_get_side_data(frame, AV_FRAME_DATA_DETECTION_BBOXES);
        header = (const AVDetectionBBoxHeader *)sd->data;

        for (uint32_t i = 0; i < header->nb_bboxes; i++) {
            LastLevelTaskItem *lltask;
            const AVDetectionBBox *bbox = av_get_detection_bbox(header, i);

            if (params->target) {
                if (av_strncasecmp(bbox->detect_label, params->target, sizeof(bbox->detect_label)) != 0) {
                    continue;
                }
            }

            lltask = av_malloc(sizeof(*lltask));
            if (!lltask) {
                return AVERROR(ENOMEM);
            }
            task->inference_todo++;
            lltask->task = task;
            lltask->bbox_index = i;
            if (ff_queue_push_back(lltask_queue, lltask) < 0) {
                av_freep(&lltask);
                return AVERROR(ENOMEM);
            }
        }
        return 0;
    }
    default:
        av_assert0(!"should not reach here");
        return AVERROR(EINVAL);
    }
}

static int get_output_ov(void *model, const char *input_name, int input_width, int input_height,
                                   const char *output_name, int *output_width, int *output_height)
{
    int ret;
    OVModel *ov_model = model;
    OVContext *ctx = &ov_model->ctx;
    TaskItem task;
    OVRequestItem *request;
    IEStatusCode status;
    input_shapes_t input_shapes;
    DNNExecBaseParams exec_params = {
        .input_name     = input_name,
        .output_names   = &output_name,
        .nb_output      = 1,
        .in_frame       = NULL,
        .out_frame      = NULL,
    };

    if (ov_model->model->func_type != DFT_PROCESS_FRAME) {
        av_log(ctx, AV_LOG_ERROR, "Get output dim only when processing frame.\n");
        return AVERROR(EINVAL);
    }

    if (ctx->options.input_resizable) {
        status = ie_network_get_input_shapes(ov_model->network, &input_shapes);
        input_shapes.shapes->shape.dims[2] = input_height;
        input_shapes.shapes->shape.dims[3] = input_width;
        status |= ie_network_reshape(ov_model->network, input_shapes);
        ie_network_input_shapes_free(&input_shapes);
        if (status != OK) {
            av_log(ctx, AV_LOG_ERROR, "Failed to reshape input size for %s\n", input_name);
            return DNN_GENERIC_ERROR;
        }
    }

    if (!ov_model->exe_network) {
        ret = init_model_ov(ov_model, input_name, output_name);
        if (ret != 0) {
            av_log(ctx, AV_LOG_ERROR, "Failed init OpenVINO exectuable network or inference request\n");
            return ret;
        }
    }

    ret = ff_dnn_fill_gettingoutput_task(&task, &exec_params, ov_model, input_height, input_width, ctx);
    if (ret != 0) {
        goto err;
    }

    ret = extract_lltask_from_task(ov_model->model->func_type, &task, ov_model->lltask_queue, NULL);
    if (ret != 0) {
        av_log(ctx, AV_LOG_ERROR, "unable to extract inference from task.\n");
        goto err;
    }

    request = ff_safe_queue_pop_front(ov_model->request_queue);
    if (!request) {
        av_log(ctx, AV_LOG_ERROR, "unable to get infer request.\n");
        ret = AVERROR(EINVAL);
        goto err;
    }

    ret = execute_model_ov(request, ov_model->lltask_queue);
    *output_width = task.out_frame->width;
    *output_height = task.out_frame->height;
err:
    av_frame_free(&task.out_frame);
    av_frame_free(&task.in_frame);
    return ret;
}

DNNModel *ff_dnn_load_model_ov(const char *model_filename, DNNFunctionType func_type, const char *options, AVFilterContext *filter_ctx)
{
    DNNModel *model = NULL;
    OVModel *ov_model = NULL;
    OVContext *ctx = NULL;
    IEStatusCode status;
    size_t node_count = 0;
    char *node_name = NULL;

    model = av_mallocz(sizeof(DNNModel));
    if (!model){
        return NULL;
    }

    ov_model = av_mallocz(sizeof(OVModel));
    if (!ov_model) {
        av_freep(&model);
        return NULL;
    }
    model->model = ov_model;
    ov_model->model = model;
    ov_model->ctx.class = &dnn_openvino_class;
    ctx = &ov_model->ctx;
    ov_model->all_input_names = NULL;
    ov_model->all_output_names = NULL;

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
    if (status != OK) {
        ie_version_t ver;
        ver = ie_c_api_version();
        av_log(ctx, AV_LOG_ERROR, "Failed to read the network from model file %s,\n"
                                  "Please check if the model version matches the runtime OpenVINO %s\n",
                                   model_filename, ver.api_version);
        ie_version_free(&ver);
        goto err;
    }

    //get all the input and output names
    status = ie_network_get_inputs_number(ov_model->network, &node_count);
    if (status != OK) {
        av_log(ctx, AV_LOG_ERROR, "Failed to get input count\n");
        goto err;
    }
    for (size_t i = 0; i < node_count; i++) {
        status = ie_network_get_input_name(ov_model->network, i, &node_name);
        if (status != OK) {
            av_log(ctx, AV_LOG_ERROR, "Failed to get No.%d input's name\n", (int)i);
            goto err;
        }
        APPEND_STRING(ov_model->all_input_names, node_name)
    }
    status = ie_network_get_outputs_number(ov_model->network, &node_count);
    if (status != OK) {
        av_log(ctx, AV_LOG_ERROR, "Failed to get output count\n");
        goto err;
    }
    for (size_t i = 0; i < node_count; i++) {
        status = ie_network_get_output_name(ov_model->network, i, &node_name);
        if (status != OK) {
            av_log(ctx, AV_LOG_ERROR, "Failed to get No.%d output's name\n", (int)i);
            goto err;
        }
        APPEND_STRING(ov_model->all_output_names, node_name)
    }

    model->get_input = &get_input_ov;
    model->get_output = &get_output_ov;
    model->options = options;
    model->filter_ctx = filter_ctx;
    model->func_type = func_type;

    return model;

err:
    ff_dnn_free_model_ov(&model);
    return NULL;
}

int ff_dnn_execute_model_ov(const DNNModel *model, DNNExecBaseParams *exec_params)
{
    OVModel *ov_model = model->model;
    OVContext *ctx = &ov_model->ctx;
    OVRequestItem *request;
    TaskItem *task;
    int ret;

    ret = ff_check_exec_params(ctx, DNN_OV, model->func_type, exec_params);
    if (ret != 0) {
        return ret;
    }

    if (!ov_model->exe_network) {
        ret = init_model_ov(ov_model, exec_params->input_name, exec_params->output_names[0]);
        if (ret != 0) {
            av_log(ctx, AV_LOG_ERROR, "Failed init OpenVINO exectuable network or inference request\n");
            return ret;
        }
    }

    task = av_malloc(sizeof(*task));
    if (!task) {
        av_log(ctx, AV_LOG_ERROR, "unable to alloc memory for task item.\n");
        return AVERROR(ENOMEM);
    }

    ret = ff_dnn_fill_task(task, exec_params, ov_model, ctx->options.async, 1);
    if (ret != 0) {
        av_freep(&task);
        return ret;
    }

    if (ff_queue_push_back(ov_model->task_queue, task) < 0) {
        av_freep(&task);
        av_log(ctx, AV_LOG_ERROR, "unable to push back task_queue.\n");
        return AVERROR(ENOMEM);
    }

    ret = extract_lltask_from_task(model->func_type, task, ov_model->lltask_queue, exec_params);
    if (ret != 0) {
        av_log(ctx, AV_LOG_ERROR, "unable to extract inference from task.\n");
        return ret;
    }

    if (ctx->options.async) {
        while (ff_queue_size(ov_model->lltask_queue) >= ctx->options.batch_size) {
            request = ff_safe_queue_pop_front(ov_model->request_queue);
            if (!request) {
                av_log(ctx, AV_LOG_ERROR, "unable to get infer request.\n");
                return AVERROR(EINVAL);
            }

            ret = execute_model_ov(request, ov_model->lltask_queue);
            if (ret != 0) {
                return ret;
            }
        }

        return 0;
    }
    else {
        if (model->func_type == DFT_ANALYTICS_CLASSIFY) {
            // Classification filter has not been completely
            // tested with the sync mode. So, do not support now.
            avpriv_report_missing_feature(ctx, "classify for sync execution");
            return AVERROR(ENOSYS);
        }

        if (ctx->options.batch_size > 1) {
            avpriv_report_missing_feature(ctx, "batch mode for sync execution");
            return AVERROR(ENOSYS);
        }

        request = ff_safe_queue_pop_front(ov_model->request_queue);
        if (!request) {
            av_log(ctx, AV_LOG_ERROR, "unable to get infer request.\n");
            return AVERROR(EINVAL);
        }
        return execute_model_ov(request, ov_model->lltask_queue);
    }
}

DNNAsyncStatusType ff_dnn_get_result_ov(const DNNModel *model, AVFrame **in, AVFrame **out)
{
    OVModel *ov_model = model->model;
    return ff_dnn_get_result_common(ov_model->task_queue, in, out);
}

int ff_dnn_flush_ov(const DNNModel *model)
{
    OVModel *ov_model = model->model;
    OVContext *ctx = &ov_model->ctx;
    OVRequestItem *request;
    IEStatusCode status;
    int ret;

    if (ff_queue_size(ov_model->lltask_queue) == 0) {
        // no pending task need to flush
        return 0;
    }

    request = ff_safe_queue_pop_front(ov_model->request_queue);
    if (!request) {
        av_log(ctx, AV_LOG_ERROR, "unable to get infer request.\n");
        return AVERROR(EINVAL);
    }

    ret = fill_model_input_ov(ov_model, request);
    if (ret != 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to fill model input.\n");
        return ret;
    }
    status = ie_infer_set_completion_callback(request->infer_request, &request->callback);
    if (status != OK) {
        av_log(ctx, AV_LOG_ERROR, "Failed to set completion callback for inference\n");
        return DNN_GENERIC_ERROR;
    }
    status = ie_infer_request_infer_async(request->infer_request);
    if (status != OK) {
        av_log(ctx, AV_LOG_ERROR, "Failed to start async inference\n");
        return DNN_GENERIC_ERROR;
    }

    return 0;
}

void ff_dnn_free_model_ov(DNNModel **model)
{
    if (*model){
        OVModel *ov_model = (*model)->model;
        while (ff_safe_queue_size(ov_model->request_queue) != 0) {
            OVRequestItem *item = ff_safe_queue_pop_front(ov_model->request_queue);
            if (item && item->infer_request) {
                ie_infer_request_free(&item->infer_request);
            }
            av_freep(&item->lltasks);
            av_freep(&item);
        }
        ff_safe_queue_destroy(ov_model->request_queue);

        while (ff_queue_size(ov_model->lltask_queue) != 0) {
            LastLevelTaskItem *item = ff_queue_pop_front(ov_model->lltask_queue);
            av_freep(&item);
        }
        ff_queue_destroy(ov_model->lltask_queue);

        while (ff_queue_size(ov_model->task_queue) != 0) {
            TaskItem *item = ff_queue_pop_front(ov_model->task_queue);
            av_frame_free(&item->in_frame);
            av_frame_free(&item->out_frame);
            av_freep(&item);
        }
        ff_queue_destroy(ov_model->task_queue);

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
