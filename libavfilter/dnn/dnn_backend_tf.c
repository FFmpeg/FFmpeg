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
 * DNN tensorflow backend implementation.
 */

#include "libavformat/avio.h"
#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/cpu.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavcodec/defs.h"
#include "dnn_io_proc.h"
#include "dnn_backend_common.h"
#include "safe_queue.h"
#include <tensorflow/c/c_api.h>

typedef struct TFModel {
    DNNModel model;
    DnnContext *ctx;
    TF_Graph *graph;
    TF_Session *session;
    TF_Status *status;
    SafeQueue *request_queue;
    Queue *lltask_queue;
    Queue *task_queue;
} TFModel;

/**
 * Stores execution parameters for single
 * call to the TensorFlow C API
 */
typedef struct TFInferRequest {
    TF_Output *tf_outputs;
    TF_Tensor **output_tensors;
    TF_Output *tf_input;
    TF_Tensor *input_tensor;
} TFInferRequest;

typedef struct TFRequestItem {
    TFInferRequest *infer_request;
    LastLevelTaskItem *lltask;
    TF_Status *status;
    DNNAsyncExecModule exec_module;
} TFRequestItem;

#define OFFSET(x) offsetof(TFOptions, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM
static const AVOption dnn_tensorflow_options[] = {
    { "sess_config", "config for SessionOptions", OFFSET(sess_config), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, FLAGS },
    { NULL }
};


static int execute_model_tf(TFRequestItem *request, Queue *lltask_queue);
static void infer_completion_callback(void *args);
static inline void destroy_request_item(TFRequestItem **arg);

static void free_buffer(void *data, size_t length)
{
    av_freep(&data);
}

/**
 * Free the contents of TensorFlow inference request.
 * It does not free the TFInferRequest instance.
 *
 * @param request pointer to TFInferRequest instance.
 * NULL pointer is allowed.
 */
static void tf_free_request(TFInferRequest *request)
{
    if (!request)
        return;
    if (request->input_tensor) {
        TF_DeleteTensor(request->input_tensor);
        request->input_tensor = NULL;
    }
    av_freep(&request->tf_input);
    av_freep(&request->tf_outputs);
    if (request->output_tensors) {
        int nb_output = sizeof(*request->output_tensors)/sizeof(request->output_tensors[0]);
        for (uint32_t i = 0; i < nb_output; ++i) {
            if (request->output_tensors[i]) {
                TF_DeleteTensor(request->output_tensors[i]);
                request->output_tensors[i] = NULL;
            }
        }
        av_freep(&request->output_tensors);
    }
}

/**
 * Create a TensorFlow inference request. All properties
 * are initially unallocated and set as NULL.
 *
 * @return pointer to the allocated TFInferRequest instance.
 */
static TFInferRequest *tf_create_inference_request(void)
{
    TFInferRequest *infer_request = av_malloc(sizeof(TFInferRequest));
    if (!infer_request) {
        return NULL;
    }
    infer_request->tf_outputs = NULL;
    infer_request->tf_input = NULL;
    infer_request->input_tensor = NULL;
    infer_request->output_tensors = NULL;
    return infer_request;
}

/**
 * Start synchronous inference for the TensorFlow model.
 *
 * @param request pointer to the TFRequestItem for inference
 * @retval 0 if execution is successful
 * @retval AVERROR(EINVAL) if request is NULL
 * @retval DNN_GENERIC_ERROR if execution fails
 */
static int tf_start_inference(void *args)
{
    TFRequestItem *request = args;
    TFInferRequest *infer_request = request->infer_request;
    LastLevelTaskItem *lltask = request->lltask;
    TaskItem *task = lltask->task;
    TFModel *tf_model = task->model;

    if (!request) {
        av_log(tf_model->ctx, AV_LOG_ERROR, "TFRequestItem is NULL\n");
        return AVERROR(EINVAL);
    }

    TF_SessionRun(tf_model->session, NULL,
                  infer_request->tf_input, &infer_request->input_tensor, 1,
                  infer_request->tf_outputs, infer_request->output_tensors,
                  task->nb_output, NULL, 0, NULL,
                  request->status);
    if (TF_GetCode(request->status) != TF_OK) {
        av_log(tf_model->ctx, AV_LOG_ERROR, "%s", TF_Message(request->status));
        return DNN_GENERIC_ERROR;
    }
    return 0;
}

/**
 * Free the TFRequestItem completely.
 *
 * @param arg Address of the TFInferRequest instance.
 */
static inline void destroy_request_item(TFRequestItem **arg) {
    TFRequestItem *request;
    if (!arg) {
        return;
    }
    request = *arg;
    tf_free_request(request->infer_request);
    av_freep(&request->infer_request);
    av_freep(&request->lltask);
    TF_DeleteStatus(request->status);
    ff_dnn_async_module_cleanup(&request->exec_module);
    av_freep(arg);
}

static int extract_lltask_from_task(TaskItem *task, Queue *lltask_queue)
{
    TFModel *tf_model = task->model;
    DnnContext *ctx = tf_model->ctx;
    LastLevelTaskItem *lltask = av_malloc(sizeof(*lltask));
    if (!lltask) {
        av_log(ctx, AV_LOG_ERROR, "Unable to allocate space for LastLevelTaskItem\n");
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

static TF_Buffer *read_graph(const char *model_filename)
{
    TF_Buffer *graph_buf;
    unsigned char *graph_data = NULL;
    AVIOContext *model_file_context;
    long size, bytes_read;

    if (avio_open(&model_file_context, model_filename, AVIO_FLAG_READ) < 0){
        return NULL;
    }

    size = avio_size(model_file_context);

    graph_data = av_malloc(size);
    if (!graph_data){
        avio_closep(&model_file_context);
        return NULL;
    }
    bytes_read = avio_read(model_file_context, graph_data, size);
    avio_closep(&model_file_context);
    if (bytes_read != size){
        av_freep(&graph_data);
        return NULL;
    }

    graph_buf = TF_NewBuffer();
    graph_buf->data = graph_data;
    graph_buf->length = size;
    graph_buf->data_deallocator = free_buffer;

    return graph_buf;
}

static TF_Tensor *allocate_input_tensor(const DNNData *input)
{
    TF_DataType dt;
    size_t size;
    int64_t input_dims[4] = { 0 };

    input_dims[0] = 1;
    input_dims[1] = input->dims[dnn_get_height_idx_by_layout(input->layout)];
    input_dims[2] = input->dims[dnn_get_width_idx_by_layout(input->layout)];
    input_dims[3] = input->dims[dnn_get_channel_idx_by_layout(input->layout)];
    switch (input->dt) {
    case DNN_FLOAT:
        dt = TF_FLOAT;
        size = sizeof(float);
        break;
    case DNN_UINT8:
        dt = TF_UINT8;
        size = 1;
        break;
    default:
        av_assert0(!"should not reach here");
    }

    return TF_AllocateTensor(dt, input_dims, 4,
                             input_dims[1] * input_dims[2] * input_dims[3] * size);
}

static int get_input_tf(DNNModel *model, DNNData *input, const char *input_name)
{
    TFModel *tf_model = (TFModel *)model;
    DnnContext *ctx = tf_model->ctx;
    TF_Status *status;
    TF_DataType dt;
    int64_t dims[4];

    TF_Output tf_output;
    tf_output.oper = TF_GraphOperationByName(tf_model->graph, input_name);
    if (!tf_output.oper) {
        av_log(ctx, AV_LOG_ERROR, "Could not find \"%s\" in model\n", input_name);
        return AVERROR(EINVAL);
    }

    tf_output.index = 0;
    dt = TF_OperationOutputType(tf_output);
    switch (dt) {
    case TF_FLOAT:
        input->dt = DNN_FLOAT;
        break;
    case TF_UINT8:
        input->dt = DNN_UINT8;
        break;
    default:
        av_log(ctx, AV_LOG_ERROR, "Unsupported output type %d in model\n", dt);
        return AVERROR(EINVAL);
    }
    input->order = DCO_RGB;

    status = TF_NewStatus();
    TF_GraphGetTensorShape(tf_model->graph, tf_output, dims, 4, status);
    if (TF_GetCode(status) != TF_OK){
        TF_DeleteStatus(status);
        av_log(ctx, AV_LOG_ERROR, "Failed to get input tensor shape: number of dimension incorrect\n");
        return DNN_GENERIC_ERROR;
    }
    TF_DeleteStatus(status);

    // currently only NHWC is supported
    av_assert0(dims[0] == 1 || dims[0] == -1);
    for (int i = 0; i < 4; i++)
        input->dims[i] = dims[i];
    input->layout = DL_NHWC;

    return 0;
}

static int get_output_tf(DNNModel *model, const char *input_name, int input_width, int input_height,
                                   const char *output_name, int *output_width, int *output_height)
{
    int ret;
    TFModel *tf_model = (TFModel *)model;
    DnnContext *ctx = tf_model->ctx;
    TaskItem task;
    TFRequestItem *request;
    DNNExecBaseParams exec_params = {
        .input_name     = input_name,
        .output_names   = &output_name,
        .nb_output      = 1,
        .in_frame       = NULL,
        .out_frame      = NULL,
    };

    ret = ff_dnn_fill_gettingoutput_task(&task, &exec_params, tf_model, input_height, input_width, ctx);
    if (ret != 0) {
        goto err;
    }

    ret = extract_lltask_from_task(&task, tf_model->lltask_queue);
    if (ret != 0) {
        av_log(ctx, AV_LOG_ERROR, "unable to extract inference from task.\n");
        goto err;
    }

    request = ff_safe_queue_pop_front(tf_model->request_queue);
    if (!request) {
        av_log(ctx, AV_LOG_ERROR, "unable to get infer request.\n");
        ret = AVERROR(EINVAL);
        goto err;
    }

    ret = execute_model_tf(request, tf_model->lltask_queue);
    *output_width = task.out_frame->width;
    *output_height = task.out_frame->height;

err:
    av_frame_free(&task.out_frame);
    av_frame_free(&task.in_frame);
    return ret;
}

#define SPACE_CHARS " \t\r\n"
static int hex_to_data(uint8_t *data, const char *p)
{
    int c, len, v;

    len = 0;
    v   = 1;
    for (;;) {
        p += strspn(p, SPACE_CHARS);
        if (*p == '\0')
            break;
        c = av_toupper((unsigned char) *p++);
        if (c >= '0' && c <= '9')
            c = c - '0';
        else if (c >= 'A' && c <= 'F')
            c = c - 'A' + 10;
        else
            break;
        v = (v << 4) | c;
        if (v & 0x100) {
            if (data) {
                data[len] = v;
            }
            len++;
            v = 1;
        }
    }
    return len;
}

static int load_tf_model(TFModel *tf_model, const char *model_filename)
{
    DnnContext *ctx = tf_model->ctx;
    TF_Buffer *graph_def;
    TF_ImportGraphDefOptions *graph_opts;
    TF_SessionOptions *sess_opts;
    const TF_Operation *init_op;
    uint8_t *sess_config = NULL;
    int sess_config_length = 0;

    // prepare the sess config data
    if (ctx->tf_option.sess_config != NULL) {
        const char *config;
        /*
        tf_model->ctx.options.sess_config is hex to present the serialized proto
        required by TF_SetConfig below, so we need to first generate the serialized
        proto in a python script, tools/python/tf_sess_config.py is a script example
        to generate the configs of sess_config.
        */
        if (strncmp(ctx->tf_option.sess_config, "0x", 2) != 0) {
            av_log(ctx, AV_LOG_ERROR, "sess_config should start with '0x'\n");
            return AVERROR(EINVAL);
        }
        config = ctx->tf_option.sess_config + 2;
        sess_config_length = hex_to_data(NULL, config);

        sess_config = av_mallocz(sess_config_length + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!sess_config) {
            av_log(ctx, AV_LOG_ERROR, "failed to allocate memory\n");
            return AVERROR(ENOMEM);
        }
        if (hex_to_data(sess_config, config) < 0) {
            av_log(ctx, AV_LOG_ERROR, "failed to convert hex to data\n");
            return AVERROR(EINVAL);
        }
    }

    graph_def = read_graph(model_filename);
    if (!graph_def){
        av_log(ctx, AV_LOG_ERROR, "Failed to read model \"%s\" graph\n", model_filename);
        av_freep(&sess_config);
        return AVERROR(EINVAL);
    }
    tf_model->graph = TF_NewGraph();
    tf_model->status = TF_NewStatus();
    graph_opts = TF_NewImportGraphDefOptions();
    TF_GraphImportGraphDef(tf_model->graph, graph_def, graph_opts, tf_model->status);
    TF_DeleteImportGraphDefOptions(graph_opts);
    TF_DeleteBuffer(graph_def);
    if (TF_GetCode(tf_model->status) != TF_OK){
        av_log(ctx, AV_LOG_ERROR, "Failed to import serialized graph to model graph\n");
        av_freep(&sess_config);
        return DNN_GENERIC_ERROR;
    }

    init_op = TF_GraphOperationByName(tf_model->graph, "init");
    sess_opts = TF_NewSessionOptions();

    if (sess_config) {
        TF_SetConfig(sess_opts, sess_config, sess_config_length,tf_model->status);
        av_freep(&sess_config);
        if (TF_GetCode(tf_model->status) != TF_OK) {
            TF_DeleteSessionOptions(sess_opts);
            av_log(ctx, AV_LOG_ERROR, "Failed to set config for sess options with %s\n",
                                      ctx->tf_option.sess_config);
            return DNN_GENERIC_ERROR;
        }
    }

    tf_model->session = TF_NewSession(tf_model->graph, sess_opts, tf_model->status);
    TF_DeleteSessionOptions(sess_opts);
    if (TF_GetCode(tf_model->status) != TF_OK)
    {
        av_freep(&sess_config);
        av_log(ctx, AV_LOG_ERROR, "Failed to create new session with model graph\n");
        return DNN_GENERIC_ERROR;
    }

    // Run initialization operation with name "init" if it is present in graph
    if (init_op){
        TF_SessionRun(tf_model->session, NULL,
                      NULL, NULL, 0,
                      NULL, NULL, 0,
                      &init_op, 1, NULL, tf_model->status);
        if (TF_GetCode(tf_model->status) != TF_OK)
        {
            av_freep(&sess_config);
            av_log(ctx, AV_LOG_ERROR, "Failed to run session when initializing\n");
            return DNN_GENERIC_ERROR;
        }
    }

    return 0;
}

static void dnn_free_model_tf(DNNModel **model)
{
    TFModel *tf_model;

    if (!model || !*model)
        return;

    tf_model = (TFModel *)(*model);
    while (ff_safe_queue_size(tf_model->request_queue) != 0) {
        TFRequestItem *item = ff_safe_queue_pop_front(tf_model->request_queue);
        destroy_request_item(&item);
    }
    ff_safe_queue_destroy(tf_model->request_queue);

    while (ff_queue_size(tf_model->lltask_queue) != 0) {
        LastLevelTaskItem *item = ff_queue_pop_front(tf_model->lltask_queue);
        av_freep(&item);
    }
    ff_queue_destroy(tf_model->lltask_queue);

    while (ff_queue_size(tf_model->task_queue) != 0) {
        TaskItem *item = ff_queue_pop_front(tf_model->task_queue);
        av_frame_free(&item->in_frame);
        av_frame_free(&item->out_frame);
        av_freep(&item);
    }
    ff_queue_destroy(tf_model->task_queue);

    if (tf_model->graph){
        TF_DeleteGraph(tf_model->graph);
    }
    if (tf_model->session){
        TF_CloseSession(tf_model->session, tf_model->status);
        TF_DeleteSession(tf_model->session, tf_model->status);
    }
    if (tf_model->status){
        TF_DeleteStatus(tf_model->status);
    }
    av_freep(&tf_model);
    *model = NULL;
}

static DNNModel *dnn_load_model_tf(DnnContext *ctx, DNNFunctionType func_type, AVFilterContext *filter_ctx)
{
    DNNModel *model = NULL;
    TFModel *tf_model = NULL;

    tf_model = av_mallocz(sizeof(TFModel));
    if (!tf_model)
        return NULL;
    model = &tf_model->model;
    tf_model->ctx = ctx;

    if (load_tf_model(tf_model, ctx->model_filename) != 0){
        av_log(ctx, AV_LOG_ERROR, "Failed to load TensorFlow model: \"%s\"\n", ctx->model_filename);
        goto err;
    }

    if (ctx->nireq <= 0) {
        ctx->nireq = av_cpu_count() / 2 + 1;
    }

#if !HAVE_PTHREAD_CANCEL
    if (ctx->options.async) {
        ctx->options.async = 0;
        av_log(filter_ctx, AV_LOG_WARNING, "pthread is not supported, roll back to sync.\n");
    }
#endif

    tf_model->request_queue = ff_safe_queue_create();
    if (!tf_model->request_queue) {
        goto err;
    }

    for (int i = 0; i < ctx->nireq; i++) {
        TFRequestItem *item = av_mallocz(sizeof(*item));
        if (!item) {
            goto err;
        }
        item->lltask = NULL;
        item->infer_request = tf_create_inference_request();
        if (!item->infer_request) {
            av_log(ctx, AV_LOG_ERROR, "Failed to allocate memory for TensorFlow inference request\n");
            av_freep(&item);
            goto err;
        }
        item->status = TF_NewStatus();
        item->exec_module.start_inference = &tf_start_inference;
        item->exec_module.callback = &infer_completion_callback;
        item->exec_module.args = item;

        if (ff_safe_queue_push_back(tf_model->request_queue, item) < 0) {
            destroy_request_item(&item);
            goto err;
        }
    }

    tf_model->lltask_queue = ff_queue_create();
    if (!tf_model->lltask_queue) {
        goto err;
    }

    tf_model->task_queue = ff_queue_create();
    if (!tf_model->task_queue) {
        goto err;
    }

    model->get_input = &get_input_tf;
    model->get_output = &get_output_tf;
    model->filter_ctx = filter_ctx;
    model->func_type = func_type;

    return model;
err:
    dnn_free_model_tf(&model);
    return NULL;
}

static int fill_model_input_tf(TFModel *tf_model, TFRequestItem *request) {
    DNNData input = { 0 };
    LastLevelTaskItem *lltask;
    TaskItem *task;
    TFInferRequest *infer_request = NULL;
    DnnContext *ctx = tf_model->ctx;
    int ret = 0;

    lltask = ff_queue_pop_front(tf_model->lltask_queue);
    av_assert0(lltask);
    task = lltask->task;
    request->lltask = lltask;

    ret = get_input_tf(&tf_model->model, &input, task->input_name);
    if (ret != 0) {
        goto err;
    }

    infer_request = request->infer_request;
    input.dims[1] = task->in_frame->height;
    input.dims[2] = task->in_frame->width;

    infer_request->tf_input = av_malloc(sizeof(TF_Output));
    if (!infer_request->tf_input) {
        av_log(ctx, AV_LOG_ERROR, "Failed to allocate memory for input tensor\n");
        ret = AVERROR(ENOMEM);
        goto err;
    }

    infer_request->tf_input->oper = TF_GraphOperationByName(tf_model->graph, task->input_name);
    if (!infer_request->tf_input->oper){
        av_log(ctx, AV_LOG_ERROR, "Could not find \"%s\" in model\n", task->input_name);
        ret = DNN_GENERIC_ERROR;
        goto err;
    }
    infer_request->tf_input->index = 0;

    infer_request->input_tensor = allocate_input_tensor(&input);
    if (!infer_request->input_tensor){
        av_log(ctx, AV_LOG_ERROR, "Failed to allocate memory for input tensor\n");
        ret = AVERROR(ENOMEM);
        goto err;
    }
    input.data = (float *)TF_TensorData(infer_request->input_tensor);

    switch (tf_model->model.func_type) {
    case DFT_PROCESS_FRAME:
        if (task->do_ioproc) {
            if (tf_model->model.frame_pre_proc != NULL) {
                tf_model->model.frame_pre_proc(task->in_frame, &input, tf_model->model.filter_ctx);
            } else {
                ff_proc_from_frame_to_dnn(task->in_frame, &input, ctx);
            }
        }
        break;
    case DFT_ANALYTICS_DETECT:
        ff_frame_to_dnn_detect(task->in_frame, &input, ctx);
        break;
    default:
        avpriv_report_missing_feature(ctx, "model function type %d", tf_model->model.func_type);
        break;
    }

    infer_request->tf_outputs = av_malloc_array(task->nb_output, sizeof(TF_Output));
    if (infer_request->tf_outputs == NULL) {
        av_log(ctx, AV_LOG_ERROR, "Failed to allocate memory for *tf_outputs\n");
        ret = AVERROR(ENOMEM);
        goto err;
    }

    infer_request->output_tensors = av_calloc(task->nb_output, sizeof(*infer_request->output_tensors));
    if (!infer_request->output_tensors) {
        av_log(ctx, AV_LOG_ERROR, "Failed to allocate memory for output tensor\n");
        ret = AVERROR(ENOMEM);
        goto err;
    }

    for (int i = 0; i < task->nb_output; ++i) {
        infer_request->output_tensors[i] = NULL;
        infer_request->tf_outputs[i].oper = TF_GraphOperationByName(tf_model->graph, task->output_names[i]);
        if (!infer_request->tf_outputs[i].oper) {
            av_log(ctx, AV_LOG_ERROR, "Could not find output \"%s\" in model\n", task->output_names[i]);
            ret = DNN_GENERIC_ERROR;
            goto err;
        }
        infer_request->tf_outputs[i].index = 0;
    }

    return 0;
err:
    tf_free_request(infer_request);
    return ret;
}

static void infer_completion_callback(void *args) {
    TFRequestItem *request = args;
    LastLevelTaskItem *lltask = request->lltask;
    TaskItem *task = lltask->task;
    DNNData *outputs;
    TFInferRequest *infer_request = request->infer_request;
    TFModel *tf_model = task->model;
    DnnContext *ctx = tf_model->ctx;

    outputs = av_calloc(task->nb_output, sizeof(*outputs));
    if (!outputs) {
        av_log(ctx, AV_LOG_ERROR, "Failed to allocate memory for *outputs\n");
        goto err;
    }

    for (uint32_t i = 0; i < task->nb_output; ++i) {
        outputs[i].dims[dnn_get_height_idx_by_layout(outputs[i].layout)] =
            TF_Dim(infer_request->output_tensors[i], 1);
        outputs[i].dims[dnn_get_width_idx_by_layout(outputs[i].layout)] =
            TF_Dim(infer_request->output_tensors[i], 2);
        outputs[i].dims[dnn_get_channel_idx_by_layout(outputs[i].layout)] =
            TF_Dim(infer_request->output_tensors[i], 3);
        outputs[i].data = TF_TensorData(infer_request->output_tensors[i]);
        outputs[i].dt = (DNNDataType)TF_TensorType(infer_request->output_tensors[i]);
    }
    switch (tf_model->model.func_type) {
    case DFT_PROCESS_FRAME:
        //it only support 1 output if it's frame in & frame out
        if (task->do_ioproc) {
            if (tf_model->model.frame_post_proc != NULL) {
                tf_model->model.frame_post_proc(task->out_frame, outputs, tf_model->model.filter_ctx);
            } else {
                ff_proc_from_dnn_to_frame(task->out_frame, outputs, ctx);
            }
        } else {
            task->out_frame->width =
                outputs[0].dims[dnn_get_width_idx_by_layout(outputs[0].layout)];
            task->out_frame->height =
                outputs[0].dims[dnn_get_height_idx_by_layout(outputs[0].layout)];
        }
        break;
    case DFT_ANALYTICS_DETECT:
        if (!tf_model->model.detect_post_proc) {
            av_log(ctx, AV_LOG_ERROR, "Detect filter needs provide post proc\n");
            return;
        }
        tf_model->model.detect_post_proc(task->in_frame, outputs, task->nb_output, tf_model->model.filter_ctx);
        break;
    default:
        av_log(ctx, AV_LOG_ERROR, "Tensorflow backend does not support this kind of dnn filter now\n");
        goto err;
    }
    task->inference_done++;
err:
    tf_free_request(infer_request);
    av_freep(&outputs);

    if (ff_safe_queue_push_back(tf_model->request_queue, request) < 0) {
        destroy_request_item(&request);
        av_log(ctx, AV_LOG_ERROR, "Failed to push back request_queue.\n");
    }
}

static int execute_model_tf(TFRequestItem *request, Queue *lltask_queue)
{
    TFModel *tf_model;
    DnnContext *ctx;
    LastLevelTaskItem *lltask;
    TaskItem *task;
    int ret = 0;

    if (ff_queue_size(lltask_queue) == 0) {
        destroy_request_item(&request);
        return 0;
    }

    lltask = ff_queue_peek_front(lltask_queue);
    task = lltask->task;
    tf_model = task->model;
    ctx = tf_model->ctx;

    ret = fill_model_input_tf(tf_model, request);
    if (ret != 0) {
        goto err;
    }

    if (task->async) {
        if (ff_dnn_start_inference_async(ctx, &request->exec_module) != 0) {
            goto err;
        }
        return 0;
    }
    else {
        ret = tf_start_inference(request);
        if (ret != 0) {
            goto err;
        }
        infer_completion_callback(request);
        return (task->inference_done == task->inference_todo) ? 0 : DNN_GENERIC_ERROR;
    }
err:
    tf_free_request(request->infer_request);
    if (ff_safe_queue_push_back(tf_model->request_queue, request) < 0) {
        destroy_request_item(&request);
    }

    return ret;
}

static int dnn_execute_model_tf(const DNNModel *model, DNNExecBaseParams *exec_params)
{
    TFModel *tf_model = (TFModel *)model;
    DnnContext *ctx = tf_model->ctx;
    TaskItem *task;
    TFRequestItem *request;
    int ret = 0;

    ret = ff_check_exec_params(ctx, DNN_TF, model->func_type, exec_params);
    if (ret != 0) {
        return ret;
    }

    task = av_malloc(sizeof(*task));
    if (!task) {
        av_log(ctx, AV_LOG_ERROR, "unable to alloc memory for task item.\n");
        return AVERROR(ENOMEM);
    }

    ret = ff_dnn_fill_task(task, exec_params, tf_model, ctx->async, 1);
    if (ret != 0) {
        av_log(ctx, AV_LOG_ERROR, "Fill task with invalid parameter(s).\n");
        av_freep(&task);
        return ret;
    }

    if (ff_queue_push_back(tf_model->task_queue, task) < 0) {
        av_freep(&task);
        av_log(ctx, AV_LOG_ERROR, "unable to push back task_queue.\n");
        return AVERROR(ENOMEM);
    }

    ret = extract_lltask_from_task(task, tf_model->lltask_queue);
    if (ret != 0) {
        av_freep(&task);
        av_log(ctx, AV_LOG_ERROR, "unable to extract last level task from task.\n");
        return ret;
    }

    request = ff_safe_queue_pop_front(tf_model->request_queue);
    if (!request) {
        av_freep(&task);
        av_log(ctx, AV_LOG_ERROR, "unable to get infer request.\n");
        return AVERROR(EINVAL);
    }
    return execute_model_tf(request, tf_model->lltask_queue);
}

static DNNAsyncStatusType dnn_get_result_tf(const DNNModel *model, AVFrame **in, AVFrame **out)
{
    TFModel *tf_model = (TFModel *)model;
    return ff_dnn_get_result_common(tf_model->task_queue, in, out);
}

static int dnn_flush_tf(const DNNModel *model)
{
    TFModel *tf_model = (TFModel *)model;
    DnnContext *ctx = tf_model->ctx;
    TFRequestItem *request;
    int ret;

    if (ff_queue_size(tf_model->lltask_queue) == 0) {
        // no pending task need to flush
        return 0;
    }

    request = ff_safe_queue_pop_front(tf_model->request_queue);
    if (!request) {
        av_log(ctx, AV_LOG_ERROR, "unable to get infer request.\n");
        return AVERROR(EINVAL);
    }

    ret = fill_model_input_tf(tf_model, request);
    if (ret != 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to fill model input.\n");
        if (ff_safe_queue_push_back(tf_model->request_queue, request) < 0) {
            destroy_request_item(&request);
        }
        return ret;
    }

    return ff_dnn_start_inference_async(ctx, &request->exec_module);
}

const DNNModule ff_dnn_backend_tf = {
    .clazz          = DNN_DEFINE_CLASS(dnn_tensorflow),
    .type           = DNN_TF,
    .load_model     = dnn_load_model_tf,
    .execute_model  = dnn_execute_model_tf,
    .get_result     = dnn_get_result_tf,
    .flush          = dnn_flush_tf,
    .free_model     = dnn_free_model_tf,
};
