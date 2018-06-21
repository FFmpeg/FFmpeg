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
#include "libavformat/avio.h"

#include <tensorflow/c/c_api.h>

typedef struct TFModel{
    TF_Graph* graph;
    TF_Session* session;
    TF_Status* status;
    TF_Output input, output;
    TF_Tensor* input_tensor;
    TF_Tensor* output_tensor;
    const DNNData* input_data;
    const DNNData* output_data;
} TFModel;

static void free_buffer(void* data, size_t length)
{
    av_freep(&data);
}

static TF_Buffer* read_graph(const char* model_filename)
{
    TF_Buffer* graph_buf;
    unsigned char* graph_data = NULL;
    AVIOContext* model_file_context;
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
    graph_buf->data = (void*)graph_data;
    graph_buf->length = size;
    graph_buf->data_deallocator = free_buffer;

    return graph_buf;
}

static DNNReturnType set_input_output_tf(void* model, const DNNData* input, const DNNData* output)
{
    TFModel* tf_model = (TFModel*)model;
    int64_t input_dims[] = {1, input->height, input->width, input->channels};
    int64_t output_dims[] = {1, output->height, output->width, output->channels};
    TF_SessionOptions* sess_opts;
    const TF_Operation* init_op = TF_GraphOperationByName(tf_model->graph, "init");

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

    // Output operation should be named 'y'
    tf_model->output.oper = TF_GraphOperationByName(tf_model->graph, "y");
    if (!tf_model->output.oper){
        return DNN_ERROR;
    }
    tf_model->output.index = 0;
    if (tf_model->output_tensor){
        TF_DeleteTensor(tf_model->output_tensor);
    }
    tf_model->output_tensor = TF_AllocateTensor(TF_FLOAT, output_dims, 4,
                                                output_dims[1] * output_dims[2] * output_dims[3] * sizeof(float));
    if (!tf_model->output_tensor){
        return DNN_ERROR;
    }

    tf_model->input_data = input;
    tf_model->output_data = output;

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

    return DNN_SUCCESS;
}

DNNModel* ff_dnn_load_model_tf(const char* model_filename)
{
    DNNModel* model = NULL;
    TFModel* tf_model = NULL;
    TF_Buffer* graph_def;
    TF_ImportGraphDefOptions* graph_opts;

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
    tf_model->output_tensor = NULL;

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

    model->model = (void*)tf_model;
    model->set_input_output = &set_input_output_tf;

    return model;
}

DNNModel* ff_dnn_load_default_model_tf(DNNDefaultModel model_type)
{
    DNNModel* model = NULL;
    TFModel* tf_model = NULL;
    TF_Buffer* graph_def;
    unsigned char* graph_data = NULL;
    TF_ImportGraphDefOptions* graph_opts;

    graph_def = TF_NewBuffer();
    switch (model_type){
    case DNN_SRCNN:
        graph_data = av_malloc(srcnn_tf_size);
        if (!graph_data){
            TF_DeleteBuffer(graph_def);
            return NULL;
        }
        memcpy(graph_data, srcnn_tf_model, srcnn_tf_size);
        graph_def->data = (void*)graph_data;
        graph_def->length = srcnn_tf_size;
        graph_def->data_deallocator = free_buffer;
        break;
    default:
        TF_DeleteBuffer(graph_def);
        return NULL;
    }

    model = av_malloc(sizeof(DNNModel));
    if (!model){
        TF_DeleteBuffer(graph_def);
        return NULL;
    }

    tf_model = av_malloc(sizeof(TFModel));
    if (!tf_model){
        TF_DeleteBuffer(graph_def);
        av_freep(&model);
        return NULL;
    }
    tf_model->session = NULL;
    tf_model->input_tensor = NULL;
    tf_model->output_tensor = NULL;

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

    model->model = (void*)tf_model;
    model->set_input_output = &set_input_output_tf;

    return model;
}

DNNReturnType ff_dnn_execute_model_tf(const DNNModel* model)
{
    TFModel* tf_model = (TFModel*)model->model;

    memcpy(TF_TensorData(tf_model->input_tensor), tf_model->input_data->data,
           tf_model->input_data->height * tf_model->input_data->width *
           tf_model->input_data->channels * sizeof(float));

    TF_SessionRun(tf_model->session, NULL,
                  &tf_model->input, &tf_model->input_tensor, 1,
                  &tf_model->output, &tf_model->output_tensor, 1,
                  NULL, 0, NULL, tf_model->status);

    if (TF_GetCode(tf_model->status) != TF_OK){
        return DNN_ERROR;
    }
    else{
        memcpy(tf_model->output_data->data, TF_TensorData(tf_model->output_tensor),
           tf_model->output_data->height * tf_model->output_data->width *
           tf_model->output_data->channels * sizeof(float));

        return DNN_SUCCESS;
    }
}

void ff_dnn_free_model_tf(DNNModel** model)
{
    TFModel* tf_model;

    if (*model){
        tf_model = (TFModel*)(*model)->model;
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
        if (tf_model->output_tensor){
            TF_DeleteTensor(tf_model->output_tensor);
        }
        av_freep(&tf_model);
        av_freep(model);
    }
}
