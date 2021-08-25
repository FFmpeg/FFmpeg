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
 * DNN inference functions interface for native backend.
 */


#ifndef AVFILTER_DNN_DNN_BACKEND_NATIVE_H
#define AVFILTER_DNN_DNN_BACKEND_NATIVE_H

#include "../dnn_interface.h"
#include "libavformat/avio.h"
#include "libavutil/opt.h"
#include "queue.h"

/**
 * the enum value of DNNLayerType should not be changed,
 * the same values are used in convert_from_tensorflow.py
 * and, it is used to index the layer execution/load function pointer.
 */
typedef enum {
    DLT_INPUT = 0,
    DLT_CONV2D = 1,
    DLT_DEPTH_TO_SPACE = 2,
    DLT_MIRROR_PAD = 3,
    DLT_MAXIMUM = 4,
    DLT_MATH_BINARY = 5,
    DLT_MATH_UNARY = 6,
    DLT_AVG_POOL = 7,
    DLT_DENSE = 8,
    DLT_COUNT
} DNNLayerType;

typedef enum {DOT_INPUT = 1, DOT_OUTPUT = 2, DOT_INTERMEDIATE = DOT_INPUT | DOT_OUTPUT} DNNOperandType;
typedef enum {VALID, SAME, SAME_CLAMP_TO_EDGE} DNNPaddingParam;
typedef enum {RELU, TANH, SIGMOID, NONE, LEAKY_RELU} DNNActivationFunc;

typedef struct Layer{
    DNNLayerType type;
    /**
     * a layer can have multiple inputs and one output.
     * 4 is just a big enough number for input operands (increase it if necessary),
     * do not use 'int32_t *input_operand_indexes', so we don't worry about mem leaks.
     */
    int32_t input_operand_indexes[4];
    int32_t output_operand_index;
    void *params;
} Layer;

typedef struct DnnOperand{
    /**
     * there are two memory layouts, NHWC or NCHW, so we use dims,
     * dims[0] is Number.
     */
    int32_t dims[4];

    /**
     * input/output/intermediate operand of the network
     */
    DNNOperandType type;

    /**
     * support different kinds of data type such as float, half float, int8 etc,
     * first support float now.
     */
    DNNDataType data_type;

    /**
     * NHWC if 1, otherwise NCHW.
     * let's first support NHWC only, this flag is for extensive usage.
     */
    int8_t isNHWC;

    /**
     * to avoid possible memory leak, do not use char *name
     */
    char name[128];

    /**
     * data pointer with data length in bytes.
     * usedNumbersLeft is only valid for intermediate operand,
     * it means how many layers still depend on this operand,
     * todo: the memory can be reused when usedNumbersLeft is zero.
     */
    void *data;
    int32_t length;
    int32_t usedNumbersLeft;
}DnnOperand;

typedef struct InputParams{
    int height, width, channels;
} InputParams;

typedef struct NativeOptions{
    uint8_t async;
    uint32_t conv2d_threads;
} NativeOptions;

typedef struct NativeContext {
    const AVClass *class;
    NativeOptions options;
} NativeContext;

// Represents simple feed-forward convolutional network.
typedef struct NativeModel{
    NativeContext ctx;
    DNNModel *model;
    Layer *layers;
    int32_t layers_num;
    DnnOperand *operands;
    int32_t operands_num;
    Queue *task_queue;
    Queue *lltask_queue;
} NativeModel;

DNNModel *ff_dnn_load_model_native(const char *model_filename, DNNFunctionType func_type, const char *options, AVFilterContext *filter_ctx);

DNNReturnType ff_dnn_execute_model_native(const DNNModel *model, DNNExecBaseParams *exec_params);

DNNAsyncStatusType ff_dnn_get_result_native(const DNNModel *model, AVFrame **in, AVFrame **out);

DNNReturnType ff_dnn_flush_native(const DNNModel *model);

void ff_dnn_free_model_native(DNNModel **model);

// NOTE: User must check for error (return value <= 0) to handle
// case like integer overflow.
int32_t ff_calculate_operand_data_length(const DnnOperand *oprd);
int32_t ff_calculate_operand_dims_count(const DnnOperand *oprd);
#endif
