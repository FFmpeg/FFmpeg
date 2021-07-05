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

#include "dnn_backend_tf.h"
#include "dnn_backend_native.h"
#include "dnn_backend_native_layer_conv2d.h"
#include "dnn_backend_native_layer_depth2space.h"
#include "libavformat/avio.h"
#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "../internal.h"
#include "dnn_backend_native_layer_pad.h"
#include "dnn_backend_native_layer_maximum.h"
#include "dnn_io_proc.h"
#include "dnn_backend_common.h"
#include "safe_queue.h"
#include "queue.h"
#include <tensorflow/c/c_api.h>

typedef struct TFOptions{
    char *sess_config;
    uint32_t nireq;
} TFOptions;

typedef struct TFContext {
    const AVClass *class;
    TFOptions options;
} TFContext;

typedef struct TFModel{
    TFContext ctx;
    DNNModel *model;
    TF_Graph *graph;
    TF_Session *session;
    TF_Status *status;
    SafeQueue *request_queue;
    Queue *inference_queue;
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
    InferenceItem *inference;
    // further properties will be added later for async
} TFRequestItem;

#define OFFSET(x) offsetof(TFContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM
static const AVOption dnn_tensorflow_options[] = {
    { "sess_config", "config for SessionOptions", OFFSET(options.sess_config), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, FLAGS },
    DNN_BACKEND_COMMON_OPTIONS
    { NULL }
};

AVFILTER_DEFINE_CLASS(dnn_tensorflow);

static DNNReturnType execute_model_tf(TFRequestItem *request, Queue *inference_queue);

static void free_buffer(void *data, size_t length)
{
    av_freep(&data);
}

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

static TFInferRequest *tf_create_inference_request(void)
{
    TFInferRequest *infer_request = av_malloc(sizeof(TFInferRequest));
    infer_request->tf_outputs = NULL;
    infer_request->tf_input = NULL;
    infer_request->input_tensor = NULL;
    infer_request->output_tensors = NULL;
    return infer_request;
}

static DNNReturnType extract_inference_from_task(TaskItem *task, Queue *inference_queue)
{
    TFModel *tf_model = task->model;
    TFContext *ctx = &tf_model->ctx;
    InferenceItem *inference = av_malloc(sizeof(*inference));
    if (!inference) {
        av_log(ctx, AV_LOG_ERROR, "Unable to allocate space for InferenceItem\n");
        return DNN_ERROR;
    }
    task->inference_todo = 1;
    task->inference_done = 0;
    inference->task = task;
    if (ff_queue_push_back(inference_queue, inference) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to push back inference_queue.\n");
        av_freep(&inference);
        return DNN_ERROR;
    }
    return DNN_SUCCESS;
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
    int64_t input_dims[] = {1, input->height, input->width, input->channels};
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

static DNNReturnType get_input_tf(void *model, DNNData *input, const char *input_name)
{
    TFModel *tf_model = model;
    TFContext *ctx = &tf_model->ctx;
    TF_Status *status;
    int64_t dims[4];

    TF_Output tf_output;
    tf_output.oper = TF_GraphOperationByName(tf_model->graph, input_name);
    if (!tf_output.oper) {
        av_log(ctx, AV_LOG_ERROR, "Could not find \"%s\" in model\n", input_name);
        return DNN_ERROR;
    }

    tf_output.index = 0;
    input->dt = TF_OperationOutputType(tf_output);
    input->order = DCO_RGB;

    status = TF_NewStatus();
    TF_GraphGetTensorShape(tf_model->graph, tf_output, dims, 4, status);
    if (TF_GetCode(status) != TF_OK){
        TF_DeleteStatus(status);
        av_log(ctx, AV_LOG_ERROR, "Failed to get input tensor shape: number of dimension incorrect\n");
        return DNN_ERROR;
    }
    TF_DeleteStatus(status);

    // currently only NHWC is supported
    av_assert0(dims[0] == 1 || dims[0] == -1);
    input->height = dims[1];
    input->width = dims[2];
    input->channels = dims[3];

    return DNN_SUCCESS;
}

static DNNReturnType get_output_tf(void *model, const char *input_name, int input_width, int input_height,
                                   const char *output_name, int *output_width, int *output_height)
{
    DNNReturnType ret;
    TFModel *tf_model = model;
    TFContext *ctx = &tf_model->ctx;
    AVFrame *in_frame = av_frame_alloc();
    AVFrame *out_frame = NULL;
    TaskItem task;
    TFRequestItem *request;

    if (!in_frame) {
        av_log(ctx, AV_LOG_ERROR, "Failed to allocate memory for input frame\n");
        ret = DNN_ERROR;
        goto err;
    }

    out_frame = av_frame_alloc();
    if (!out_frame) {
        av_log(ctx, AV_LOG_ERROR, "Failed to allocate memory for output frame\n");
        ret = DNN_ERROR;
        goto err;
    }

    in_frame->width = input_width;
    in_frame->height = input_height;

    task.do_ioproc = 0;
    task.async = 0;
    task.input_name = input_name;
    task.in_frame = in_frame;
    task.output_names = &output_name;
    task.out_frame = out_frame;
    task.model = tf_model;
    task.nb_output = 1;

    if (extract_inference_from_task(&task, tf_model->inference_queue) != DNN_SUCCESS) {
        av_log(ctx, AV_LOG_ERROR, "unable to extract inference from task.\n");
        ret = DNN_ERROR;
        goto err;
    }

    request = ff_safe_queue_pop_front(tf_model->request_queue);
    if (!request) {
        av_log(ctx, AV_LOG_ERROR, "unable to get infer request.\n");
        ret = DNN_ERROR;
        goto err;
    }

    ret = execute_model_tf(request, tf_model->inference_queue);
    *output_width = out_frame->width;
    *output_height = out_frame->height;

err:
    av_frame_free(&out_frame);
    av_frame_free(&in_frame);
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

static DNNReturnType load_tf_model(TFModel *tf_model, const char *model_filename)
{
    TFContext *ctx = &tf_model->ctx;
    TF_Buffer *graph_def;
    TF_ImportGraphDefOptions *graph_opts;
    TF_SessionOptions *sess_opts;
    const TF_Operation *init_op;
    uint8_t *sess_config = NULL;
    int sess_config_length = 0;

    // prepare the sess config data
    if (tf_model->ctx.options.sess_config != NULL) {
        const char *config;
        /*
        tf_model->ctx.options.sess_config is hex to present the serialized proto
        required by TF_SetConfig below, so we need to first generate the serialized
        proto in a python script, tools/python/tf_sess_config.py is a script example
        to generate the configs of sess_config.
        */
        if (strncmp(tf_model->ctx.options.sess_config, "0x", 2) != 0) {
            av_log(ctx, AV_LOG_ERROR, "sess_config should start with '0x'\n");
            return DNN_ERROR;
        }
        config = tf_model->ctx.options.sess_config + 2;
        sess_config_length = hex_to_data(NULL, config);

        sess_config = av_mallocz(sess_config_length + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!sess_config) {
            av_log(ctx, AV_LOG_ERROR, "failed to allocate memory\n");
            return DNN_ERROR;
        }
        if (hex_to_data(sess_config, config) < 0) {
            av_log(ctx, AV_LOG_ERROR, "failed to convert hex to data\n");
            return DNN_ERROR;
        }
    }

    graph_def = read_graph(model_filename);
    if (!graph_def){
        av_log(ctx, AV_LOG_ERROR, "Failed to read model \"%s\" graph\n", model_filename);
        av_freep(&sess_config);
        return DNN_ERROR;
    }
    tf_model->graph = TF_NewGraph();
    tf_model->status = TF_NewStatus();
    graph_opts = TF_NewImportGraphDefOptions();
    TF_GraphImportGraphDef(tf_model->graph, graph_def, graph_opts, tf_model->status);
    TF_DeleteImportGraphDefOptions(graph_opts);
    TF_DeleteBuffer(graph_def);
    if (TF_GetCode(tf_model->status) != TF_OK){
        TF_DeleteGraph(tf_model->graph);
        TF_DeleteStatus(tf_model->status);
        av_log(ctx, AV_LOG_ERROR, "Failed to import serialized graph to model graph\n");
        av_freep(&sess_config);
        return DNN_ERROR;
    }

    init_op = TF_GraphOperationByName(tf_model->graph, "init");
    sess_opts = TF_NewSessionOptions();

    if (sess_config) {
        TF_SetConfig(sess_opts, sess_config, sess_config_length,tf_model->status);
        av_freep(&sess_config);
        if (TF_GetCode(tf_model->status) != TF_OK) {
            TF_DeleteGraph(tf_model->graph);
            TF_DeleteStatus(tf_model->status);
            TF_DeleteSessionOptions(sess_opts);
            av_log(ctx, AV_LOG_ERROR, "Failed to set config for sess options with %s\n",
                                      tf_model->ctx.options.sess_config);
            return DNN_ERROR;
        }
    }

    tf_model->session = TF_NewSession(tf_model->graph, sess_opts, tf_model->status);
    TF_DeleteSessionOptions(sess_opts);
    if (TF_GetCode(tf_model->status) != TF_OK)
    {
        TF_DeleteGraph(tf_model->graph);
        TF_DeleteStatus(tf_model->status);
        av_log(ctx, AV_LOG_ERROR, "Failed to create new session with model graph\n");
        return DNN_ERROR;
    }

    // Run initialization operation with name "init" if it is present in graph
    if (init_op){
        TF_SessionRun(tf_model->session, NULL,
                      NULL, NULL, 0,
                      NULL, NULL, 0,
                      &init_op, 1, NULL, tf_model->status);
        if (TF_GetCode(tf_model->status) != TF_OK)
        {
            TF_DeleteSession(tf_model->session, tf_model->status);
            TF_DeleteGraph(tf_model->graph);
            TF_DeleteStatus(tf_model->status);
            av_log(ctx, AV_LOG_ERROR, "Failed to run session when initializing\n");
            return DNN_ERROR;
        }
    }

    return DNN_SUCCESS;
}

#define NAME_BUFFER_SIZE 256

static DNNReturnType add_conv_layer(TFModel *tf_model, TF_Operation *transpose_op, TF_Operation **cur_op,
                                    ConvolutionalParams* params, const int layer)
{
    TFContext *ctx = &tf_model->ctx;
    TF_Operation *op;
    TF_OperationDescription *op_desc;
    TF_Output input;
    int64_t strides[] = {1, 1, 1, 1};
    TF_Tensor *kernel_tensor = NULL, *biases_tensor = NULL;
    int64_t dims[4];
    int dims_len;
    char name_buffer[NAME_BUFFER_SIZE];
    int32_t size;

    size = params->input_num * params->output_num * params->kernel_size * params->kernel_size;
    input.index = 0;

    snprintf(name_buffer, NAME_BUFFER_SIZE, "conv_kernel%d", layer);
    op_desc = TF_NewOperation(tf_model->graph, "Const", name_buffer);
    TF_SetAttrType(op_desc, "dtype", TF_FLOAT);
    dims[0] = params->output_num;
    dims[1] = params->kernel_size;
    dims[2] = params->kernel_size;
    dims[3] = params->input_num;
    dims_len = 4;
    kernel_tensor = TF_AllocateTensor(TF_FLOAT, dims, dims_len, size * sizeof(float));
    memcpy(TF_TensorData(kernel_tensor), params->kernel, size * sizeof(float));
    TF_SetAttrTensor(op_desc, "value", kernel_tensor, tf_model->status);
    if (TF_GetCode(tf_model->status) != TF_OK){
        goto err;
    }
    op = TF_FinishOperation(op_desc, tf_model->status);
    if (TF_GetCode(tf_model->status) != TF_OK){
        goto err;
    }

    snprintf(name_buffer, NAME_BUFFER_SIZE, "transpose%d", layer);
    op_desc = TF_NewOperation(tf_model->graph, "Transpose", name_buffer);
    input.oper = op;
    TF_AddInput(op_desc, input);
    input.oper = transpose_op;
    TF_AddInput(op_desc, input);
    TF_SetAttrType(op_desc, "T", TF_FLOAT);
    TF_SetAttrType(op_desc, "Tperm", TF_INT32);
    op = TF_FinishOperation(op_desc, tf_model->status);
    if (TF_GetCode(tf_model->status) != TF_OK){
        goto err;
    }

    snprintf(name_buffer, NAME_BUFFER_SIZE, "conv2d%d", layer);
    op_desc = TF_NewOperation(tf_model->graph, "Conv2D", name_buffer);
    input.oper = *cur_op;
    TF_AddInput(op_desc, input);
    input.oper = op;
    TF_AddInput(op_desc, input);
    TF_SetAttrType(op_desc, "T", TF_FLOAT);
    TF_SetAttrIntList(op_desc, "strides", strides, 4);
    TF_SetAttrString(op_desc, "padding", "VALID", 5);
    *cur_op = TF_FinishOperation(op_desc, tf_model->status);
    if (TF_GetCode(tf_model->status) != TF_OK){
        goto err;
    }

    snprintf(name_buffer, NAME_BUFFER_SIZE, "conv_biases%d", layer);
    op_desc = TF_NewOperation(tf_model->graph, "Const", name_buffer);
    TF_SetAttrType(op_desc, "dtype", TF_FLOAT);
    dims[0] = params->output_num;
    dims_len = 1;
    biases_tensor = TF_AllocateTensor(TF_FLOAT, dims, dims_len, params->output_num * sizeof(float));
    memcpy(TF_TensorData(biases_tensor), params->biases, params->output_num * sizeof(float));
    TF_SetAttrTensor(op_desc, "value", biases_tensor, tf_model->status);
    if (TF_GetCode(tf_model->status) != TF_OK){
        goto err;
    }
    op = TF_FinishOperation(op_desc, tf_model->status);
    if (TF_GetCode(tf_model->status) != TF_OK){
        goto err;
    }

    snprintf(name_buffer, NAME_BUFFER_SIZE, "bias_add%d", layer);
    op_desc = TF_NewOperation(tf_model->graph, "BiasAdd", name_buffer);
    input.oper = *cur_op;
    TF_AddInput(op_desc, input);
    input.oper = op;
    TF_AddInput(op_desc, input);
    TF_SetAttrType(op_desc, "T", TF_FLOAT);
    *cur_op = TF_FinishOperation(op_desc, tf_model->status);
    if (TF_GetCode(tf_model->status) != TF_OK){
        goto err;
    }

    snprintf(name_buffer, NAME_BUFFER_SIZE, "activation%d", layer);
    switch (params->activation){
    case RELU:
        op_desc = TF_NewOperation(tf_model->graph, "Relu", name_buffer);
        break;
    case TANH:
        op_desc = TF_NewOperation(tf_model->graph, "Tanh", name_buffer);
        break;
    case SIGMOID:
        op_desc = TF_NewOperation(tf_model->graph, "Sigmoid", name_buffer);
        break;
    default:
        avpriv_report_missing_feature(ctx, "convolutional activation function %d", params->activation);
        return DNN_ERROR;
    }
    input.oper = *cur_op;
    TF_AddInput(op_desc, input);
    TF_SetAttrType(op_desc, "T", TF_FLOAT);
    *cur_op = TF_FinishOperation(op_desc, tf_model->status);
    if (TF_GetCode(tf_model->status) != TF_OK){
        goto err;
    }

    return DNN_SUCCESS;
err:
    TF_DeleteTensor(kernel_tensor);
    TF_DeleteTensor(biases_tensor);
    av_log(ctx, AV_LOG_ERROR, "Failed to add conv layer %d\n", layer);
    return DNN_ERROR;
}

static DNNReturnType add_depth_to_space_layer(TFModel *tf_model, TF_Operation **cur_op,
                                              DepthToSpaceParams *params, const int layer)
{
    TFContext *ctx = &tf_model->ctx;
    TF_OperationDescription *op_desc;
    TF_Output input;
    char name_buffer[NAME_BUFFER_SIZE];

    snprintf(name_buffer, NAME_BUFFER_SIZE, "depth_to_space%d", layer);
    op_desc = TF_NewOperation(tf_model->graph, "DepthToSpace", name_buffer);
    input.oper = *cur_op;
    input.index = 0;
    TF_AddInput(op_desc, input);
    TF_SetAttrType(op_desc, "T", TF_FLOAT);
    TF_SetAttrInt(op_desc, "block_size", params->block_size);
    *cur_op = TF_FinishOperation(op_desc, tf_model->status);
    if (TF_GetCode(tf_model->status) != TF_OK){
        av_log(ctx, AV_LOG_ERROR, "Failed to add depth_to_space to layer %d\n", layer);
        return DNN_ERROR;
    }

    return DNN_SUCCESS;
}

static DNNReturnType add_pad_layer(TFModel *tf_model, TF_Operation **cur_op,
                                              LayerPadParams *params, const int layer)
{
    TFContext *ctx = &tf_model->ctx;
    TF_Operation *op;
    TF_Tensor *tensor;
    TF_OperationDescription *op_desc;
    TF_Output input;
    int32_t *pads;
    int64_t pads_shape[] = {4, 2};

    char name_buffer[NAME_BUFFER_SIZE];
    snprintf(name_buffer, NAME_BUFFER_SIZE, "pad%d", layer);

    op_desc = TF_NewOperation(tf_model->graph, "Const", name_buffer);
    TF_SetAttrType(op_desc, "dtype", TF_INT32);
    tensor = TF_AllocateTensor(TF_INT32, pads_shape, 2, 4 * 2 * sizeof(int32_t));
    pads = (int32_t *)TF_TensorData(tensor);
    pads[0] = params->paddings[0][0];
    pads[1] = params->paddings[0][1];
    pads[2] = params->paddings[1][0];
    pads[3] = params->paddings[1][1];
    pads[4] = params->paddings[2][0];
    pads[5] = params->paddings[2][1];
    pads[6] = params->paddings[3][0];
    pads[7] = params->paddings[3][1];
    TF_SetAttrTensor(op_desc, "value", tensor, tf_model->status);
    if (TF_GetCode(tf_model->status) != TF_OK){
        TF_DeleteTensor(tensor);
        av_log(ctx, AV_LOG_ERROR, "Failed to set value for pad of layer %d\n", layer);
        return DNN_ERROR;
    }
    op = TF_FinishOperation(op_desc, tf_model->status);
    if (TF_GetCode(tf_model->status) != TF_OK){
        TF_DeleteTensor(tensor);
        av_log(ctx, AV_LOG_ERROR, "Failed to add pad to layer %d\n", layer);
        return DNN_ERROR;
    }

    op_desc = TF_NewOperation(tf_model->graph, "MirrorPad", "mirror_pad");
    input.oper = *cur_op;
    input.index = 0;
    TF_AddInput(op_desc, input);
    input.oper = op;
    TF_AddInput(op_desc, input);
    TF_SetAttrType(op_desc, "T", TF_FLOAT);
    TF_SetAttrType(op_desc, "Tpaddings", TF_INT32);
    TF_SetAttrString(op_desc, "mode", "SYMMETRIC", 9);
    *cur_op = TF_FinishOperation(op_desc, tf_model->status);
    if (TF_GetCode(tf_model->status) != TF_OK){
        TF_DeleteTensor(tensor);
        av_log(ctx, AV_LOG_ERROR, "Failed to add mirror_pad to layer %d\n", layer);
        return DNN_ERROR;
    }

    return DNN_SUCCESS;
}

static DNNReturnType add_maximum_layer(TFModel *tf_model, TF_Operation **cur_op,
                                       DnnLayerMaximumParams *params, const int layer)
{
    TFContext *ctx = &tf_model->ctx;
    TF_Operation *op;
    TF_Tensor *tensor;
    TF_OperationDescription *op_desc;
    TF_Output input;
    float *y;

    char name_buffer[NAME_BUFFER_SIZE];
    snprintf(name_buffer, NAME_BUFFER_SIZE, "maximum/y%d", layer);

    op_desc = TF_NewOperation(tf_model->graph, "Const", name_buffer);
    TF_SetAttrType(op_desc, "dtype", TF_FLOAT);
    tensor = TF_AllocateTensor(TF_FLOAT, NULL, 0, TF_DataTypeSize(TF_FLOAT));
    y = (float *)TF_TensorData(tensor);
    *y = params->val.y;
    TF_SetAttrTensor(op_desc, "value", tensor, tf_model->status);
    if (TF_GetCode(tf_model->status) != TF_OK){
        TF_DeleteTensor(tensor);
        av_log(ctx, AV_LOG_ERROR, "Failed to set value for maximum/y of layer %d", layer);
        return DNN_ERROR;
    }
    op = TF_FinishOperation(op_desc, tf_model->status);
    if (TF_GetCode(tf_model->status) != TF_OK){
        TF_DeleteTensor(tensor);
        av_log(ctx, AV_LOG_ERROR, "Failed to add maximum/y to layer %d\n", layer);
        return DNN_ERROR;
    }

    snprintf(name_buffer, NAME_BUFFER_SIZE, "maximum%d", layer);
    op_desc = TF_NewOperation(tf_model->graph, "Maximum", name_buffer);
    input.oper = *cur_op;
    input.index = 0;
    TF_AddInput(op_desc, input);
    input.oper = op;
    TF_AddInput(op_desc, input);
    TF_SetAttrType(op_desc, "T", TF_FLOAT);
    *cur_op = TF_FinishOperation(op_desc, tf_model->status);
    if (TF_GetCode(tf_model->status) != TF_OK){
        TF_DeleteTensor(tensor);
        av_log(ctx, AV_LOG_ERROR, "Failed to add maximum to layer %d\n", layer);
        return DNN_ERROR;
    }

    return DNN_SUCCESS;
}

static DNNReturnType load_native_model(TFModel *tf_model, const char *model_filename)
{
    TFContext *ctx = &tf_model->ctx;
    int32_t layer;
    TF_OperationDescription *op_desc;
    TF_Operation *op;
    TF_Operation *transpose_op;
    TF_Tensor *tensor = NULL;
    TF_Output input;
    int32_t *transpose_perm;
    int64_t transpose_perm_shape[] = {4};
    int64_t input_shape[] = {1, -1, -1, -1};
    DNNReturnType layer_add_res;
    DNNModel *model = NULL;
    NativeModel *native_model;

    model = ff_dnn_load_model_native(model_filename, DFT_PROCESS_FRAME, NULL, NULL);
    if (!model){
        av_log(ctx, AV_LOG_ERROR, "Failed to load native model\n");
        return DNN_ERROR;
    }

    native_model = model->model;
    tf_model->graph = TF_NewGraph();
    tf_model->status = TF_NewStatus();

#define CLEANUP_ON_ERROR(tf_model) \
    { \
        TF_DeleteTensor(tensor); \
        TF_DeleteGraph(tf_model->graph); \
        TF_DeleteStatus(tf_model->status); \
        av_log(ctx, AV_LOG_ERROR, "Failed to set value or add operator to layer\n"); \
        return DNN_ERROR; \
    }

    op_desc = TF_NewOperation(tf_model->graph, "Placeholder", "x");
    TF_SetAttrType(op_desc, "dtype", TF_FLOAT);
    TF_SetAttrShape(op_desc, "shape", input_shape, 4);
    op = TF_FinishOperation(op_desc, tf_model->status);
    if (TF_GetCode(tf_model->status) != TF_OK){
        CLEANUP_ON_ERROR(tf_model);
    }

    op_desc = TF_NewOperation(tf_model->graph, "Const", "transpose_perm");
    TF_SetAttrType(op_desc, "dtype", TF_INT32);
    tensor = TF_AllocateTensor(TF_INT32, transpose_perm_shape, 1, 4 * sizeof(int32_t));
    transpose_perm = (int32_t *)TF_TensorData(tensor);
    transpose_perm[0] = 1;
    transpose_perm[1] = 2;
    transpose_perm[2] = 3;
    transpose_perm[3] = 0;
    TF_SetAttrTensor(op_desc, "value", tensor, tf_model->status);
    if (TF_GetCode(tf_model->status) != TF_OK){
        CLEANUP_ON_ERROR(tf_model);
    }
    transpose_op = TF_FinishOperation(op_desc, tf_model->status);
    if (TF_GetCode(tf_model->status) != TF_OK){
        CLEANUP_ON_ERROR(tf_model);
    }

    for (layer = 0; layer < native_model->layers_num; ++layer){
        switch (native_model->layers[layer].type){
        case DLT_INPUT:
            layer_add_res = DNN_SUCCESS;
            break;
        case DLT_CONV2D:
            layer_add_res = add_conv_layer(tf_model, transpose_op, &op,
                                           (ConvolutionalParams *)native_model->layers[layer].params, layer);
            break;
        case DLT_DEPTH_TO_SPACE:
            layer_add_res = add_depth_to_space_layer(tf_model, &op,
                                                     (DepthToSpaceParams *)native_model->layers[layer].params, layer);
            break;
        case DLT_MIRROR_PAD:
            layer_add_res = add_pad_layer(tf_model, &op,
                                          (LayerPadParams *)native_model->layers[layer].params, layer);
            break;
        case DLT_MAXIMUM:
            layer_add_res = add_maximum_layer(tf_model, &op,
                                          (DnnLayerMaximumParams *)native_model->layers[layer].params, layer);
            break;
        default:
            CLEANUP_ON_ERROR(tf_model);
        }

        if (layer_add_res != DNN_SUCCESS){
            CLEANUP_ON_ERROR(tf_model);
        }
    }

    op_desc = TF_NewOperation(tf_model->graph, "Identity", "y");
    input.oper = op;
    input.index = 0;
    TF_AddInput(op_desc, input);
    TF_FinishOperation(op_desc, tf_model->status);
    if (TF_GetCode(tf_model->status) != TF_OK){
        CLEANUP_ON_ERROR(tf_model);
    }

    ff_dnn_free_model_native(&model);

    return DNN_SUCCESS;
}

DNNModel *ff_dnn_load_model_tf(const char *model_filename, DNNFunctionType func_type, const char *options, AVFilterContext *filter_ctx)
{
    DNNModel *model = NULL;
    TFModel *tf_model = NULL;
    TFContext *ctx = NULL;

    model = av_mallocz(sizeof(DNNModel));
    if (!model){
        return NULL;
    }

    tf_model = av_mallocz(sizeof(TFModel));
    if (!tf_model){
        av_freep(&model);
        return NULL;
    }
    tf_model->model = model;
    ctx = &tf_model->ctx;
    ctx->class = &dnn_tensorflow_class;

    //parse options
    av_opt_set_defaults(ctx);
    if (av_opt_set_from_string(ctx, options, NULL, "=", "&") < 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to parse options \"%s\"\n", options);
        goto err;
    }

    if (load_tf_model(tf_model, model_filename) != DNN_SUCCESS){
        if (load_native_model(tf_model, model_filename) != DNN_SUCCESS){
            goto err;
        }
    }

    if (ctx->options.nireq <= 0) {
        ctx->options.nireq = av_cpu_count() / 2 + 1;
    }

    tf_model->request_queue = ff_safe_queue_create();
    if (!tf_model->request_queue) {
        goto err;
    }

    for (int i = 0; i < ctx->options.nireq; i++) {
        TFRequestItem *item = av_mallocz(sizeof(*item));
        if (!item) {
            goto err;
        }
        item->infer_request = tf_create_inference_request();
        if (!item->infer_request) {
            av_log(ctx, AV_LOG_ERROR, "Failed to allocate memory for TensorFlow inference request\n");
            av_freep(&item);
            goto err;
        }

        if (ff_safe_queue_push_back(tf_model->request_queue, item) < 0) {
            av_freep(&item->infer_request);
            av_freep(&item);
            goto err;
        }
    }

    tf_model->inference_queue = ff_queue_create();
    if (!tf_model->inference_queue) {
        goto err;
    }

    model->model = tf_model;
    model->get_input = &get_input_tf;
    model->get_output = &get_output_tf;
    model->options = options;
    model->filter_ctx = filter_ctx;
    model->func_type = func_type;

    return model;
err:
    ff_dnn_free_model_tf(&model);
    return NULL;
}

static DNNReturnType fill_model_input_tf(TFModel *tf_model, TFRequestItem *request) {
    DNNData input;
    InferenceItem *inference;
    TaskItem *task;
    TFInferRequest *infer_request;
    TFContext *ctx = &tf_model->ctx;

    inference = ff_queue_pop_front(tf_model->inference_queue);
    av_assert0(inference);
    task = inference->task;
    request->inference = inference;

    if (get_input_tf(tf_model, &input, task->input_name) != DNN_SUCCESS) {
        goto err;
    }

    infer_request = request->infer_request;
    input.height = task->in_frame->height;
    input.width = task->in_frame->width;

    infer_request->tf_input = av_malloc(sizeof(TF_Output));
    if (!infer_request->tf_input) {
        av_log(ctx, AV_LOG_ERROR, "Failed to allocate memory for input tensor\n");
        goto err;
    }

    infer_request->tf_input->oper = TF_GraphOperationByName(tf_model->graph, task->input_name);
    if (!infer_request->tf_input->oper){
        av_log(ctx, AV_LOG_ERROR, "Could not find \"%s\" in model\n", task->input_name);
        goto err;
    }
    infer_request->tf_input->index = 0;

    infer_request->input_tensor = allocate_input_tensor(&input);
    if (!infer_request->input_tensor){
        av_log(ctx, AV_LOG_ERROR, "Failed to allocate memory for input tensor\n");
        goto err;
    }
    input.data = (float *)TF_TensorData(infer_request->input_tensor);

    switch (tf_model->model->func_type) {
    case DFT_PROCESS_FRAME:
        if (task->do_ioproc) {
            if (tf_model->model->frame_pre_proc != NULL) {
                tf_model->model->frame_pre_proc(task->in_frame, &input, tf_model->model->filter_ctx);
            } else {
                ff_proc_from_frame_to_dnn(task->in_frame, &input, ctx);
            }
        }
        break;
    case DFT_ANALYTICS_DETECT:
        ff_frame_to_dnn_detect(task->in_frame, &input, ctx);
        break;
    default:
        avpriv_report_missing_feature(ctx, "model function type %d", tf_model->model->func_type);
        break;
    }

    infer_request->tf_outputs = av_malloc_array(task->nb_output, sizeof(TF_Output));
    if (infer_request->tf_outputs == NULL) {
        av_log(ctx, AV_LOG_ERROR, "Failed to allocate memory for *tf_outputs\n");
        goto err;
    }

    infer_request->output_tensors = av_mallocz_array(task->nb_output, sizeof(*infer_request->output_tensors));
    if (!infer_request->output_tensors) {
        av_log(ctx, AV_LOG_ERROR, "Failed to allocate memory for output tensor\n");
        goto err;
    }

    for (int i = 0; i < task->nb_output; ++i) {
        infer_request->output_tensors[i] = NULL;
        infer_request->tf_outputs[i].oper = TF_GraphOperationByName(tf_model->graph, task->output_names[i]);
        if (!infer_request->tf_outputs[i].oper) {
            av_log(ctx, AV_LOG_ERROR, "Could not find output \"%s\" in model\n", task->output_names[i]);
            goto err;
        }
        infer_request->tf_outputs[i].index = 0;
    }

    return DNN_SUCCESS;
err:
    tf_free_request(infer_request);
    return DNN_ERROR;
}

static void infer_completion_callback(void *args) {
    TFRequestItem *request = args;
    InferenceItem *inference = request->inference;
    TaskItem *task = inference->task;
    DNNData *outputs;
    TFInferRequest *infer_request = request->infer_request;
    TFModel *tf_model = task->model;
    TFContext *ctx = &tf_model->ctx;

    outputs = av_malloc_array(task->nb_output, sizeof(*outputs));
    if (!outputs) {
        av_log(ctx, AV_LOG_ERROR, "Failed to allocate memory for *outputs\n");
        goto err;
    }

    for (uint32_t i = 0; i < task->nb_output; ++i) {
        outputs[i].height = TF_Dim(infer_request->output_tensors[i], 1);
        outputs[i].width = TF_Dim(infer_request->output_tensors[i], 2);
        outputs[i].channels = TF_Dim(infer_request->output_tensors[i], 3);
        outputs[i].data = TF_TensorData(infer_request->output_tensors[i]);
        outputs[i].dt = TF_TensorType(infer_request->output_tensors[i]);
    }
    switch (tf_model->model->func_type) {
    case DFT_PROCESS_FRAME:
        //it only support 1 output if it's frame in & frame out
        if (task->do_ioproc) {
            if (tf_model->model->frame_post_proc != NULL) {
                tf_model->model->frame_post_proc(task->out_frame, outputs, tf_model->model->filter_ctx);
            } else {
                ff_proc_from_dnn_to_frame(task->out_frame, outputs, ctx);
            }
        } else {
            task->out_frame->width = outputs[0].width;
            task->out_frame->height = outputs[0].height;
        }
        break;
    case DFT_ANALYTICS_DETECT:
        if (!tf_model->model->detect_post_proc) {
            av_log(ctx, AV_LOG_ERROR, "Detect filter needs provide post proc\n");
            return;
        }
        tf_model->model->detect_post_proc(task->out_frame, outputs, task->nb_output, tf_model->model->filter_ctx);
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
        av_freep(&request->infer_request);
        av_freep(&request);
        av_log(ctx, AV_LOG_ERROR, "Failed to push back request_queue.\n");
    }
}

static DNNReturnType execute_model_tf(TFRequestItem *request, Queue *inference_queue)
{
    TFModel *tf_model;
    TFContext *ctx;
    TFInferRequest *infer_request;
    InferenceItem *inference;
    TaskItem *task;

    inference = ff_queue_peek_front(inference_queue);
    if (!inference) {
        av_log(NULL, AV_LOG_ERROR, "Failed to get inference item\n");
        return DNN_ERROR;
    }
    task = inference->task;
    tf_model = task->model;
    ctx = &tf_model->ctx;

    if (task->async) {
        avpriv_report_missing_feature(ctx, "Async execution not supported");
        return DNN_ERROR;
    } else {
        if (fill_model_input_tf(tf_model, request) != DNN_SUCCESS) {
            return DNN_ERROR;
        }

        infer_request = request->infer_request;
        TF_SessionRun(tf_model->session, NULL,
                      infer_request->tf_input, &infer_request->input_tensor, 1,
                      infer_request->tf_outputs, infer_request->output_tensors,
                      task->nb_output, NULL, 0, NULL,
                      tf_model->status);
        if (TF_GetCode(tf_model->status) != TF_OK) {
            tf_free_request(infer_request);
            av_log(ctx, AV_LOG_ERROR, "Failed to run session when executing model\n");
            return DNN_ERROR;
        }
        infer_completion_callback(request);
        return (task->inference_done == task->inference_todo) ? DNN_SUCCESS : DNN_ERROR;
    }
}

DNNReturnType ff_dnn_execute_model_tf(const DNNModel *model, DNNExecBaseParams *exec_params)
{
    TFModel *tf_model = model->model;
    TFContext *ctx = &tf_model->ctx;
    TaskItem task;
    TFRequestItem *request;

    if (ff_check_exec_params(ctx, DNN_TF, model->func_type, exec_params) != 0) {
        return DNN_ERROR;
    }

    if (ff_dnn_fill_task(&task, exec_params, tf_model, 0, 1) != DNN_SUCCESS) {
        return DNN_ERROR;
    }

    if (extract_inference_from_task(&task, tf_model->inference_queue) != DNN_SUCCESS) {
        av_log(ctx, AV_LOG_ERROR, "unable to extract inference from task.\n");
        return DNN_ERROR;
    }

    request = ff_safe_queue_pop_front(tf_model->request_queue);
    if (!request) {
        av_log(ctx, AV_LOG_ERROR, "unable to get infer request.\n");
        return DNN_ERROR;
    }

    return execute_model_tf(request, tf_model->inference_queue);
}

void ff_dnn_free_model_tf(DNNModel **model)
{
    TFModel *tf_model;

    if (*model){
        tf_model = (*model)->model;
        while (ff_safe_queue_size(tf_model->request_queue) != 0) {
            TFRequestItem *item = ff_safe_queue_pop_front(tf_model->request_queue);
            tf_free_request(item->infer_request);
            av_freep(&item->infer_request);
            av_freep(&item);
        }
        ff_safe_queue_destroy(tf_model->request_queue);

        while (ff_queue_size(tf_model->inference_queue) != 0) {
            InferenceItem *item = ff_queue_pop_front(tf_model->inference_queue);
            av_freep(&item);
        }
        ff_queue_destroy(tf_model->inference_queue);

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
        av_freep(model);
    }
}
