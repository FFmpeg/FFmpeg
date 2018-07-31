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
#include "dnn_srcnn.h"
#include "dnn_espcn.h"
#include "libavformat/avio.h"

#include <tensorflow/c/c_api.h>

typedef struct TFModel{
    TF_Graph *graph;
    TF_Session *session;
    TF_Status *status;
    TF_Output input, output;
    TF_Tensor *input_tensor;
    DNNData *output_data;
} TFModel;

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
    graph_buf->data = (void *)graph_data;
    graph_buf->length = size;
    graph_buf->data_deallocator = free_buffer;

    return graph_buf;
}

static DNNReturnType set_input_output_tf(void *model, DNNData *input, DNNData *output)
{
    TFModel *tf_model = (TFModel *)model;
    int64_t input_dims[] = {1, input->height, input->width, input->channels};
    TF_SessionOptions *sess_opts;
    const TF_Operation *init_op = TF_GraphOperationByName(tf_model->graph, "init");
    TF_Tensor *output_tensor;

    // Input operation should be named 'x'
    tf_model->input.oper = TF_GraphOperationByName(tf_model->graph, "x");
    if (!tf_model->input.oper){
        return DNN_ERROR;
    }
    tf_model->input.index = 0;
    if (tf_model->input_tensor){
        TF_DeleteTensor(tf_model->input_tensor);
    }
    tf_model->input_tensor = TF_AllocateTensor(TF_FLOAT, input_dims, 4,
                                               input_dims[1] * input_dims[2] * input_dims[3] * sizeof(float));
    if (!tf_model->input_tensor){
        return DNN_ERROR;
    }
    input->data = (float *)TF_TensorData(tf_model->input_tensor);

    // Output operation should be named 'y'
    tf_model->output.oper = TF_GraphOperationByName(tf_model->graph, "y");
    if (!tf_model->output.oper){
        return DNN_ERROR;
    }
    tf_model->output.index = 0;

    if (tf_model->session){
        TF_CloseSession(tf_model->session, tf_model->status);
        TF_DeleteSession(tf_model->session, tf_model->status);
    }

    sess_opts = TF_NewSessionOptions();
    tf_model->session = TF_NewSession(tf_model->graph, sess_opts, tf_model->status);
    TF_DeleteSessionOptions(sess_opts);
    if (TF_GetCode(tf_model->status) != TF_OK)
    {
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
            return DNN_ERROR;
        }
    }

    // Execute network to get output height, width and number of channels
    TF_SessionRun(tf_model->session, NULL,
                  &tf_model->input, &tf_model->input_tensor, 1,
                  &tf_model->output, &output_tensor, 1,
                  NULL, 0, NULL, tf_model->status);
    if (TF_GetCode(tf_model->status) != TF_OK){
        return DNN_ERROR;
    }
    else{
        output->height = TF_Dim(output_tensor, 1);
        output->width = TF_Dim(output_tensor, 2);
        output->channels = TF_Dim(output_tensor, 3);
        output->data = av_malloc(output->height * output->width * output->channels * sizeof(float));
        if (!output->data){
            return DNN_ERROR;
        }
        tf_model->output_data = output;
        TF_DeleteTensor(output_tensor);
    }

    return DNN_SUCCESS;
}

DNNModel *ff_dnn_load_model_tf(const char *model_filename)
{
    DNNModel *model = NULL;
    TFModel *tf_model = NULL;
    TF_Buffer *graph_def;
    TF_ImportGraphDefOptions *graph_opts;

    model = av_malloc(sizeof(DNNModel));
    if (!model){
        return NULL;
    }

    tf_model = av_malloc(sizeof(TFModel));
    if (!tf_model){
        av_freep(&model);
        return NULL;
    }
    tf_model->session = NULL;
    tf_model->input_tensor = NULL;
    tf_model->output_data = NULL;

    graph_def = read_graph(model_filename);
    if (!graph_def){
        av_freep(&tf_model);
        av_freep(&model);
        return NULL;
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
        av_freep(&tf_model);
        av_freep(&model);
        return NULL;
    }

    model->model = (void *)tf_model;
    model->set_input_output = &set_input_output_tf;

    return model;
}

static TF_Operation *add_pad_op(TFModel *tf_model, TF_Operation *input_op, int32_t pad)
{
    TF_OperationDescription *op_desc;
    TF_Operation *op;
    TF_Tensor *tensor;
    TF_Output input;
    int32_t *pads;
    int64_t pads_shape[] = {4, 2};

    op_desc = TF_NewOperation(tf_model->graph, "Const", "pads");
    TF_SetAttrType(op_desc, "dtype", TF_INT32);
    tensor = TF_AllocateTensor(TF_INT32, pads_shape, 2, 4 * 2 * sizeof(int32_t));
    pads = (int32_t *)TF_TensorData(tensor);
    pads[0] = 0;   pads[1] = 0;
    pads[2] = pad; pads[3] = pad;
    pads[4] = pad; pads[5] = pad;
    pads[6] = 0;   pads[7] = 0;
    TF_SetAttrTensor(op_desc, "value", tensor, tf_model->status);
    if (TF_GetCode(tf_model->status) != TF_OK){
        return NULL;
    }
    op = TF_FinishOperation(op_desc, tf_model->status);
    if (TF_GetCode(tf_model->status) != TF_OK){
        return NULL;
    }

    op_desc = TF_NewOperation(tf_model->graph, "MirrorPad", "mirror_pad");
    input.oper = input_op;
    input.index = 0;
    TF_AddInput(op_desc, input);
    input.oper = op;
    TF_AddInput(op_desc, input);
    TF_SetAttrType(op_desc, "T", TF_FLOAT);
    TF_SetAttrType(op_desc, "Tpaddings", TF_INT32);
    TF_SetAttrString(op_desc, "mode", "SYMMETRIC", 9);
    op = TF_FinishOperation(op_desc, tf_model->status);
    if (TF_GetCode(tf_model->status) != TF_OK){
        return NULL;
    }

    return op;
}

static TF_Operation *add_const_op(TFModel *tf_model, const float *values, const int64_t *dims, int dims_len, const char *name)
{
    int dim;
    TF_OperationDescription *op_desc;
    TF_Tensor *tensor;
    size_t len;

    op_desc = TF_NewOperation(tf_model->graph, "Const", name);
    TF_SetAttrType(op_desc, "dtype", TF_FLOAT);
    len = sizeof(float);
    for (dim = 0; dim < dims_len; ++dim){
        len *= dims[dim];
    }
    tensor = TF_AllocateTensor(TF_FLOAT, dims, dims_len, len);
    memcpy(TF_TensorData(tensor), values, len);
    TF_SetAttrTensor(op_desc, "value", tensor, tf_model->status);
    if (TF_GetCode(tf_model->status) != TF_OK){
        return NULL;
    }

    return TF_FinishOperation(op_desc, tf_model->status);
}

static TF_Operation* add_conv_layers(TFModel *tf_model, const float **consts, const int64_t **consts_dims,
                                     const int *consts_dims_len, const char **activations,
                                     TF_Operation *input_op, int layers_num)
{
    int i;
    TF_OperationDescription *op_desc;
    TF_Operation *op;
    TF_Operation *transpose_op;
    TF_Output input;
    int64_t strides[] = {1, 1, 1, 1};
    int32_t *transpose_perm;
    TF_Tensor *tensor;
    int64_t transpose_perm_shape[] = {4};
    #define NAME_BUFF_SIZE 256
    char name_buffer[NAME_BUFF_SIZE];

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
        return NULL;
    }
    transpose_op = TF_FinishOperation(op_desc, tf_model->status);
    if (TF_GetCode(tf_model->status) != TF_OK){
        return NULL;
    }

    input.index = 0;
    for (i = 0; i < layers_num; ++i){
        snprintf(name_buffer, NAME_BUFF_SIZE, "conv_kernel%d", i);
        op = add_const_op(tf_model, consts[i << 1], consts_dims[i << 1], consts_dims_len[i << 1], name_buffer);
        if (TF_GetCode(tf_model->status) != TF_OK || op == NULL){
            return NULL;
        }

        snprintf(name_buffer, NAME_BUFF_SIZE, "transpose%d", i);
        op_desc = TF_NewOperation(tf_model->graph, "Transpose", name_buffer);
        input.oper = op;
        TF_AddInput(op_desc, input);
        input.oper = transpose_op;
        TF_AddInput(op_desc, input);
        TF_SetAttrType(op_desc, "T", TF_FLOAT);
        TF_SetAttrType(op_desc, "Tperm", TF_INT32);
        op = TF_FinishOperation(op_desc, tf_model->status);
        if (TF_GetCode(tf_model->status) != TF_OK){
            return NULL;
        }

        snprintf(name_buffer, NAME_BUFF_SIZE, "conv2d%d", i);
        op_desc = TF_NewOperation(tf_model->graph, "Conv2D", name_buffer);
        input.oper = input_op;
        TF_AddInput(op_desc, input);
        input.oper = op;
        TF_AddInput(op_desc, input);
        TF_SetAttrType(op_desc, "T", TF_FLOAT);
        TF_SetAttrIntList(op_desc, "strides", strides, 4);
        TF_SetAttrString(op_desc, "padding", "VALID", 5);
        input_op = TF_FinishOperation(op_desc, tf_model->status);
        if (TF_GetCode(tf_model->status) != TF_OK){
            return NULL;
        }

        snprintf(name_buffer, NAME_BUFF_SIZE, "conv_biases%d", i);
        op = add_const_op(tf_model, consts[(i << 1) + 1], consts_dims[(i << 1) + 1], consts_dims_len[(i << 1) + 1], name_buffer);
        if (TF_GetCode(tf_model->status) != TF_OK || op == NULL){
            return NULL;
        }

        snprintf(name_buffer, NAME_BUFF_SIZE, "bias_add%d", i);
        op_desc = TF_NewOperation(tf_model->graph, "BiasAdd", name_buffer);
        input.oper = input_op;
        TF_AddInput(op_desc, input);
        input.oper = op;
        TF_AddInput(op_desc, input);
        TF_SetAttrType(op_desc, "T", TF_FLOAT);
        input_op = TF_FinishOperation(op_desc, tf_model->status);
        if (TF_GetCode(tf_model->status) != TF_OK){
            return NULL;
        }

        snprintf(name_buffer, NAME_BUFF_SIZE, "activation%d", i);
        op_desc = TF_NewOperation(tf_model->graph, activations[i], name_buffer);
        input.oper = input_op;
        TF_AddInput(op_desc, input);
        TF_SetAttrType(op_desc, "T", TF_FLOAT);
        input_op = TF_FinishOperation(op_desc, tf_model->status);
        if (TF_GetCode(tf_model->status) != TF_OK){
            return NULL;
        }
    }

    return input_op;
}

DNNModel *ff_dnn_load_default_model_tf(DNNDefaultModel model_type)
{
    DNNModel *model = NULL;
    TFModel *tf_model = NULL;
    TF_OperationDescription *op_desc;
    TF_Operation *op;
    TF_Output input;
    static const int64_t input_shape[] = {1, -1, -1, 1};
    static const char tanh[] = "Tanh";
    static const char sigmoid[] = "Sigmoid";
    static const char relu[] = "Relu";

    static const float *srcnn_consts[] = {
        srcnn_conv1_kernel,
        srcnn_conv1_bias,
        srcnn_conv2_kernel,
        srcnn_conv2_bias,
        srcnn_conv3_kernel,
        srcnn_conv3_bias
    };
    static const long int *srcnn_consts_dims[] = {
        srcnn_conv1_kernel_dims,
        srcnn_conv1_bias_dims,
        srcnn_conv2_kernel_dims,
        srcnn_conv2_bias_dims,
        srcnn_conv3_kernel_dims,
        srcnn_conv3_bias_dims
    };
    static const int srcnn_consts_dims_len[] = {
        4,
        1,
        4,
        1,
        4,
        1
    };
    static const char *srcnn_activations[] = {
        relu,
        relu,
        relu
    };

    static const float *espcn_consts[] = {
        espcn_conv1_kernel,
        espcn_conv1_bias,
        espcn_conv2_kernel,
        espcn_conv2_bias,
        espcn_conv3_kernel,
        espcn_conv3_bias
    };
    static const long int *espcn_consts_dims[] = {
        espcn_conv1_kernel_dims,
        espcn_conv1_bias_dims,
        espcn_conv2_kernel_dims,
        espcn_conv2_bias_dims,
        espcn_conv3_kernel_dims,
        espcn_conv3_bias_dims
    };
    static const int espcn_consts_dims_len[] = {
        4,
        1,
        4,
        1,
        4,
        1
    };
    static const char *espcn_activations[] = {
        tanh,
        tanh,
        sigmoid
    };

    input.index = 0;

    model = av_malloc(sizeof(DNNModel));
    if (!model){
        return NULL;
    }

    tf_model = av_malloc(sizeof(TFModel));
    if (!tf_model){
        av_freep(&model);
        return NULL;
    }
    tf_model->session = NULL;
    tf_model->input_tensor = NULL;
    tf_model->output_data = NULL;

    tf_model->graph = TF_NewGraph();
    tf_model->status = TF_NewStatus();

    #define CLEANUP_ON_ERROR(tf_model, model) { \
        TF_DeleteGraph(tf_model->graph); \
        TF_DeleteStatus(tf_model->status); \
        av_freep(&tf_model); \
        av_freep(&model); \
        return NULL; \
    }

    op_desc = TF_NewOperation(tf_model->graph, "Placeholder", "x");
    TF_SetAttrType(op_desc, "dtype", TF_FLOAT);
    TF_SetAttrShape(op_desc, "shape", input_shape, 4);
    op = TF_FinishOperation(op_desc, tf_model->status);
    if (TF_GetCode(tf_model->status) != TF_OK){
        CLEANUP_ON_ERROR(tf_model, model);
    }

    switch (model_type){
    case DNN_SRCNN:
        op = add_pad_op(tf_model, op, 6);
        if (!op){
            CLEANUP_ON_ERROR(tf_model, model);
        }
        op = add_conv_layers(tf_model, srcnn_consts,
                             srcnn_consts_dims, srcnn_consts_dims_len,
                             srcnn_activations, op, 3);
        if (!op){
            CLEANUP_ON_ERROR(tf_model, model);
        }
        break;
    case DNN_ESPCN:
        op = add_pad_op(tf_model, op, 4);
        if (!op){
            CLEANUP_ON_ERROR(tf_model, model);
        }
        op = add_conv_layers(tf_model, espcn_consts,
                             espcn_consts_dims, espcn_consts_dims_len,
                             espcn_activations, op, 3);
        if (!op){
            CLEANUP_ON_ERROR(tf_model, model);
        }

        op_desc = TF_NewOperation(tf_model->graph, "DepthToSpace", "depth_to_space");
        input.oper = op;
        TF_AddInput(op_desc, input);
        TF_SetAttrType(op_desc, "T", TF_FLOAT);
        TF_SetAttrInt(op_desc, "block_size", 2);
        op = TF_FinishOperation(op_desc, tf_model->status);
        if (TF_GetCode(tf_model->status) != TF_OK){
            CLEANUP_ON_ERROR(tf_model, model);
        }
        break;
    default:
        CLEANUP_ON_ERROR(tf_model, model);
    }

    op_desc = TF_NewOperation(tf_model->graph, "Identity", "y");
    input.oper = op;
    TF_AddInput(op_desc, input);
    TF_FinishOperation(op_desc, tf_model->status);
    if (TF_GetCode(tf_model->status) != TF_OK){
        CLEANUP_ON_ERROR(tf_model, model);
    }

    model->model = (void *)tf_model;
    model->set_input_output = &set_input_output_tf;

    return model;
}

DNNReturnType ff_dnn_execute_model_tf(const DNNModel *model)
{
    TFModel *tf_model = (TFModel *)model->model;
    TF_Tensor *output_tensor;

    TF_SessionRun(tf_model->session, NULL,
                  &tf_model->input, &tf_model->input_tensor, 1,
                  &tf_model->output, &output_tensor, 1,
                  NULL, 0, NULL, tf_model->status);

    if (TF_GetCode(tf_model->status) != TF_OK){
        return DNN_ERROR;
    }
    else{
        memcpy(tf_model->output_data->data, TF_TensorData(output_tensor),
               tf_model->output_data->height * tf_model->output_data->width *
               tf_model->output_data->channels * sizeof(float));
        TF_DeleteTensor(output_tensor);

        return DNN_SUCCESS;
    }
}

void ff_dnn_free_model_tf(DNNModel **model)
{
    TFModel *tf_model;

    if (*model){
        tf_model = (TFModel *)(*model)->model;
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
        if (tf_model->input_tensor){
            TF_DeleteTensor(tf_model->input_tensor);
        }
        if (tf_model->output_data){
            av_freep(&(tf_model->output_data->data));
        }
        av_freep(&tf_model);
        av_freep(model);
    }
}
