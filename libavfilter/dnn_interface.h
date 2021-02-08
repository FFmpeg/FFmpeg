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
 * DNN inference engine interface.
 */

#ifndef AVFILTER_DNN_INTERFACE_H
#define AVFILTER_DNN_INTERFACE_H

#include <stdint.h>
#include "libavutil/frame.h"
#include "avfilter.h"

typedef enum {DNN_SUCCESS, DNN_ERROR} DNNReturnType;

typedef enum {DNN_NATIVE, DNN_TF, DNN_OV} DNNBackendType;

typedef enum {DNN_FLOAT = 1, DNN_UINT8 = 4} DNNDataType;

typedef enum {
    DCO_NONE,
    DCO_BGR,
} DNNColorOrder;

typedef enum {
    DAST_FAIL,              // something wrong
    DAST_EMPTY_QUEUE,       // no more inference result to get
    DAST_NOT_READY,         // all queued inferences are not finished
    DAST_SUCCESS            // got a result frame successfully
} DNNAsyncStatusType;

typedef enum {
    DFT_NONE,
    DFT_PROCESS_FRAME,      // process the whole frame
    DFT_ANALYTICS_DETECT,   // detect from the whole frame
    // we can add more such as detect_from_crop, classify_from_bbox, etc.
}DNNFunctionType;

typedef struct DNNData{
    void *data;
    int width, height, channels;
    // dt and order together decide the color format
    DNNDataType dt;
    DNNColorOrder order;
} DNNData;

typedef struct DNNModel{
    // Stores model that can be different for different backends.
    void *model;
    // Stores options when the model is executed by the backend
    const char *options;
    // Stores FilterContext used for the interaction between AVFrame and DNNData
    AVFilterContext *filter_ctx;
    // Stores function type of the model
    DNNFunctionType func_type;
    // Gets model input information
    // Just reuse struct DNNData here, actually the DNNData.data field is not needed.
    DNNReturnType (*get_input)(void *model, DNNData *input, const char *input_name);
    // Gets model output width/height with given input w/h
    DNNReturnType (*get_output)(void *model, const char *input_name, int input_width, int input_height,
                                const char *output_name, int *output_width, int *output_height);
    // set the pre process to transfer data from AVFrame to DNNData
    // the default implementation within DNN is used if it is not provided by the filter
    int (*pre_proc)(AVFrame *frame_in, DNNData *model_input, AVFilterContext *filter_ctx);
    // set the post process to transfer data from DNNData to AVFrame
    // the default implementation within DNN is used if it is not provided by the filter
    int (*post_proc)(AVFrame *frame_out, DNNData *model_output, AVFilterContext *filter_ctx);
} DNNModel;

// Stores pointers to functions for loading, executing, freeing DNN models for one of the backends.
typedef struct DNNModule{
    // Loads model and parameters from given file. Returns NULL if it is not possible.
    DNNModel *(*load_model)(const char *model_filename, DNNFunctionType func_type, const char *options, AVFilterContext *filter_ctx);
    // Executes model with specified input and output. Returns DNN_ERROR otherwise.
    DNNReturnType (*execute_model)(const DNNModel *model, const char *input_name, AVFrame *in_frame,
                                   const char **output_names, uint32_t nb_output, AVFrame *out_frame);
    // Executes model with specified input and output asynchronously. Returns DNN_ERROR otherwise.
    DNNReturnType (*execute_model_async)(const DNNModel *model, const char *input_name, AVFrame *in_frame,
                                         const char **output_names, uint32_t nb_output, AVFrame *out_frame);
    // Retrieve inference result.
    DNNAsyncStatusType (*get_async_result)(const DNNModel *model, AVFrame **in, AVFrame **out);
    // Flush all the pending tasks.
    DNNReturnType (*flush)(const DNNModel *model);
    // Frees memory allocated for model.
    void (*free_model)(DNNModel **model);
} DNNModule;

// Initializes DNNModule depending on chosen backend.
DNNModule *ff_get_dnn_module(DNNBackendType backend_type);

#endif
