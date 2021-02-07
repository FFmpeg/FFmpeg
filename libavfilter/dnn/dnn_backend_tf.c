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
#include "../internal.h"
#include "dnn_backend_native_layer_pad.h"
#include "dnn_backend_native_layer_maximum.h"
#include "dnn_io_proc.h"

#include <tensorflow/c/c_api.h>

typedef struct TFOptions{
    char *sess_config;
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
} TFModel;

#define OFFSET(x) offsetof(TFContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM
static const AVOption dnn_tensorflow_options[] = {
    { "sess_config", "config for SessionOptions", OFFSET(options.sess_config), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(dnn_tensorflow);

static DNNReturnType execute_model_tf(const DNNModel *model, const char *input_name, AVFrame *in_frame,
                                      const char **output_names, uint32_t nb_output, AVFrame *out_frame,
                                      int do_ioproc);

static void free_buffer(void *data, size_t length)
{
    av_freep(&data);
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

    status = TF_NewStatus();
    TF_GraphGetTensorShape(tf_model->graph, tf_output, dims, 4, status);
    if (TF_GetCode(status) != TF_OK){
        TF_DeleteStatus(status);
        av_log(ctx, AV_LOG_ERROR, "Failed to get input tensor shape: number of dimension incorrect\n");
        return DNN_ERROR;
    }
    TF_DeleteStatus(status);

    // currently only NHWC is supported
    av_assert0(dims[0] == 1);
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

    ret = execute_model_tf(tf_model->model, input_name, in_frame, &output_name, 1, out_frame, 0);
    *output_width = out_frame->width;
    *output_height = out_frame->height;

    av_frame_free(&out_frame);
    av_frame_free(&in_frame);
    return ret;
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
        /*
        tf_model->ctx.options.sess_config is hex to present the serialized proto
        required by TF_SetConfig below, so we need to first generate the serialized
        proto in a python script, the following is a script example to generate
        serialized proto which specifies one GPU, we can change the script to add
        more options.

        import tensorflow as tf
        gpu_options = tf.GPUOptions(visible_device_list='0')
        config = tf.ConfigProto(gpu_options=gpu_options)
        s = config.SerializeToString()
        b = ''.join("%02x" % int(ord(b)) for b in s[::-1])
        print('0x%s' % b)

        the script output looks like: 0xab...cd, and then pass 0xab...cd to sess_config.
        */
        char tmp[3];
        tmp[2] = '\0';

        if (strncmp(tf_model->ctx.options.sess_config, "0x", 2) != 0) {
            av_log(ctx, AV_LOG_ERROR, "sess_config should start with '0x'\n");
            return DNN_ERROR;
        }

        sess_config_length = strlen(tf_model->ctx.options.sess_config);
        if (sess_config_length % 2 != 0) {
            av_log(ctx, AV_LOG_ERROR, "the length of sess_config is not even (%s), "
                                      "please re-generate the config.\n",
                                      tf_model->ctx.options.sess_config);
            return DNN_ERROR;
        }

        sess_config_length -= 2; //ignore the first '0x'
        sess_config_length /= 2; //get the data length in byte

        sess_config = av_malloc(sess_config_length);
        if (!sess_config) {
            av_log(ctx, AV_LOG_ERROR, "failed to allocate memory\n");
            return DNN_ERROR;
        }

        for (int i = 0; i < sess_config_length; i++) {
            int index = 2 + (sess_config_length - 1 - i) * 2;
            tmp[0] = tf_model->ctx.options.sess_config[index];
            tmp[1] = tf_model->ctx.options.sess_config[index + 1];
            sess_config[i] = strtol(tmp, NULL, 16);
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
            av_log(ctx, AV_LOG_ERROR, "Failed to set config for sess options with %s\n",
                                      tf_model->ctx.options.sess_config);
            return DNN_ERROR;
        }
    }

    tf_model->session = TF_NewSession(tf_model->graph, sess_opts, tf_model->status);
    TF_DeleteSessionOptions(sess_opts);
    if (TF_GetCode(tf_model->status) != TF_OK)
    {
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
    TF_Tensor *tensor;
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
    tensor = TF_AllocateTensor(TF_FLOAT, dims, dims_len, size * sizeof(float));
    memcpy(TF_TensorData(tensor), params->kernel, size * sizeof(float));
    TF_SetAttrTensor(op_desc, "value", tensor, tf_model->status);
    if (TF_GetCode(tf_model->status) != TF_OK){
        av_log(ctx, AV_LOG_ERROR, "Failed to set value for kernel of conv layer %d\n", layer);
        return DNN_ERROR;
    }
    op = TF_FinishOperation(op_desc, tf_model->status);
    if (TF_GetCode(tf_model->status) != TF_OK){
        av_log(ctx, AV_LOG_ERROR, "Failed to add kernel to conv layer %d\n", layer);
        return DNN_ERROR;
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
        av_log(ctx, AV_LOG_ERROR, "Failed to add transpose to conv layer %d\n", layer);
        return DNN_ERROR;
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
        av_log(ctx, AV_LOG_ERROR, "Failed to add conv2d to conv layer %d\n", layer);
        return DNN_ERROR;
    }

    snprintf(name_buffer, NAME_BUFFER_SIZE, "conv_biases%d", layer);
    op_desc = TF_NewOperation(tf_model->graph, "Const", name_buffer);
    TF_SetAttrType(op_desc, "dtype", TF_FLOAT);
    dims[0] = params->output_num;
    dims_len = 1;
    tensor = TF_AllocateTensor(TF_FLOAT, dims, dims_len, params->output_num * sizeof(float));
    memcpy(TF_TensorData(tensor), params->biases, params->output_num * sizeof(float));
    TF_SetAttrTensor(op_desc, "value", tensor, tf_model->status);
    if (TF_GetCode(tf_model->status) != TF_OK){
        av_log(ctx, AV_LOG_ERROR, "Failed to set value for conv_biases of conv layer %d\n", layer);
        return DNN_ERROR;
    }
    op = TF_FinishOperation(op_desc, tf_model->status);
    if (TF_GetCode(tf_model->status) != TF_OK){
        av_log(ctx, AV_LOG_ERROR, "Failed to add conv_biases to conv layer %d\n", layer);
        return DNN_ERROR;
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
        av_log(ctx, AV_LOG_ERROR, "Failed to add bias_add to conv layer %d\n", layer);
        return DNN_ERROR;
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
        av_log(ctx, AV_LOG_ERROR, "Failed to add activation function to conv layer %d\n", layer);
        return DNN_ERROR;
    }

    return DNN_SUCCESS;
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
        av_log(ctx, AV_LOG_ERROR, "Failed to set value for pad of layer %d\n", layer);
        return DNN_ERROR;
    }
    op = TF_FinishOperation(op_desc, tf_model->status);
    if (TF_GetCode(tf_model->status) != TF_OK){
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
        av_log(ctx, AV_LOG_ERROR, "Failed to set value for maximum/y of layer %d", layer);
        return DNN_ERROR;
    }
    op = TF_FinishOperation(op_desc, tf_model->status);
    if (TF_GetCode(tf_model->status) != TF_OK){
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
    TF_Tensor *tensor;
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

    model = av_mallocz(sizeof(DNNModel));
    if (!model){
        return NULL;
    }

    tf_model = av_mallocz(sizeof(TFModel));
    if (!tf_model){
        av_freep(&model);
        return NULL;
    }
    tf_model->ctx.class = &dnn_tensorflow_class;
    tf_model->model = model;

    //parse options
    av_opt_set_defaults(&tf_model->ctx);
    if (av_opt_set_from_string(&tf_model->ctx, options, NULL, "=", "&") < 0) {
        av_log(&tf_model->ctx, AV_LOG_ERROR, "Failed to parse options \"%s\"\n", options);
        av_freep(&tf_model);
        av_freep(&model);
        return NULL;
    }

    if (load_tf_model(tf_model, model_filename) != DNN_SUCCESS){
        if (load_native_model(tf_model, model_filename) != DNN_SUCCESS){
            av_freep(&tf_model);
            av_freep(&model);

            return NULL;
        }
    }

    model->model = tf_model;
    model->get_input = &get_input_tf;
    model->get_output = &get_output_tf;
    model->options = options;
    model->filter_ctx = filter_ctx;
    model->func_type = func_type;

    return model;
}

static DNNReturnType execute_model_tf(const DNNModel *model, const char *input_name, AVFrame *in_frame,
                                      const char **output_names, uint32_t nb_output, AVFrame *out_frame,
                                      int do_ioproc)
{
    TF_Output *tf_outputs;
    TFModel *tf_model = model->model;
    TFContext *ctx = &tf_model->ctx;
    DNNData input, output;
    TF_Tensor **output_tensors;
    TF_Output tf_input;
    TF_Tensor *input_tensor;

    if (get_input_tf(tf_model, &input, input_name) != DNN_SUCCESS)
        return DNN_ERROR;
    input.height = in_frame->height;
    input.width = in_frame->width;

    tf_input.oper = TF_GraphOperationByName(tf_model->graph, input_name);
    if (!tf_input.oper){
        av_log(ctx, AV_LOG_ERROR, "Could not find \"%s\" in model\n", input_name);
        return DNN_ERROR;
    }
    tf_input.index = 0;
    input_tensor = allocate_input_tensor(&input);
    if (!input_tensor){
        av_log(ctx, AV_LOG_ERROR, "Failed to allocate memory for input tensor\n");
        return DNN_ERROR;
    }
    input.data = (float *)TF_TensorData(input_tensor);

    if (do_ioproc) {
        if (tf_model->model->pre_proc != NULL) {
            tf_model->model->pre_proc(in_frame, &input, tf_model->model->filter_ctx);
        } else {
            ff_proc_from_frame_to_dnn(in_frame, &input, tf_model->model->func_type, ctx);
        }
    }

    if (nb_output != 1) {
        // currently, the filter does not need multiple outputs,
        // so we just pending the support until we really need it.
        avpriv_report_missing_feature(ctx, "multiple outputs");
        return DNN_ERROR;
    }

    tf_outputs = av_malloc_array(nb_output, sizeof(*tf_outputs));
    if (tf_outputs == NULL) {
        av_log(ctx, AV_LOG_ERROR, "Failed to allocate memory for *tf_outputs\n"); \
        return DNN_ERROR;
    }

    output_tensors = av_mallocz_array(nb_output, sizeof(*output_tensors));
    if (!output_tensors) {
        av_freep(&tf_outputs);
        av_log(ctx, AV_LOG_ERROR, "Failed to allocate memory for output tensor\n"); \
        return DNN_ERROR;
    }

    for (int i = 0; i < nb_output; ++i) {
        tf_outputs[i].oper = TF_GraphOperationByName(tf_model->graph, output_names[i]);
        if (!tf_outputs[i].oper) {
            av_freep(&tf_outputs);
            av_freep(&output_tensors);
            av_log(ctx, AV_LOG_ERROR, "Could not find output \"%s\" in model\n", output_names[i]); \
            return DNN_ERROR;
        }
        tf_outputs[i].index = 0;
    }

    TF_SessionRun(tf_model->session, NULL,
                  &tf_input, &input_tensor, 1,
                  tf_outputs, output_tensors, nb_output,
                  NULL, 0, NULL, tf_model->status);
    if (TF_GetCode(tf_model->status) != TF_OK) {
        av_freep(&tf_outputs);
        av_freep(&output_tensors);
        av_log(ctx, AV_LOG_ERROR, "Failed to run session when executing model\n");
        return DNN_ERROR;
    }

    for (uint32_t i = 0; i < nb_output; ++i) {
        output.height = TF_Dim(output_tensors[i], 1);
        output.width = TF_Dim(output_tensors[i], 2);
        output.channels = TF_Dim(output_tensors[i], 3);
        output.data = TF_TensorData(output_tensors[i]);
        output.dt = TF_TensorType(output_tensors[i]);

        if (do_ioproc) {
            if (tf_model->model->post_proc != NULL) {
                tf_model->model->post_proc(out_frame, &output, tf_model->model->filter_ctx);
            } else {
                ff_proc_from_dnn_to_frame(out_frame, &output, ctx);
            }
        } else {
            out_frame->width = output.width;
            out_frame->height = output.height;
        }
    }

    for (uint32_t i = 0; i < nb_output; ++i) {
        if (output_tensors[i]) {
            TF_DeleteTensor(output_tensors[i]);
        }
    }
    TF_DeleteTensor(input_tensor);
    av_freep(&output_tensors);
    av_freep(&tf_outputs);
    return DNN_SUCCESS;
}

DNNReturnType ff_dnn_execute_model_tf(const DNNModel *model, const char *input_name, AVFrame *in_frame,
                                      const char **output_names, uint32_t nb_output, AVFrame *out_frame)
{
    TFModel *tf_model = model->model;
    TFContext *ctx = &tf_model->ctx;

    if (!in_frame) {
        av_log(ctx, AV_LOG_ERROR, "in frame is NULL when execute model.\n");
        return DNN_ERROR;
    }

    if (!out_frame) {
        av_log(ctx, AV_LOG_ERROR, "out frame is NULL when execute model.\n");
        return DNN_ERROR;
    }

    return execute_model_tf(model, input_name, in_frame, output_names, nb_output, out_frame, 1);
}

void ff_dnn_free_model_tf(DNNModel **model)
{
    TFModel *tf_model;

    if (*model){
        tf_model = (*model)->model;
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
