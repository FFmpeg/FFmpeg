/*
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
 * common functions for the dnn based filters
 */

#ifndef AVFILTER_DNN_FILTER_COMMON_H
#define AVFILTER_DNN_FILTER_COMMON_H

#include "dnn_interface.h"

typedef struct DnnContext {
    char *model_filename;
    DNNBackendType backend_type;
    char *model_inputname;
    char *model_outputname;
    char *backend_options;
    int async;

    DNNModule *dnn_module;
    DNNModel *model;
} DnnContext;

#define DNN_COMMON_OPTIONS \
    { "model",              "path to model file",         OFFSET(model_filename),   AV_OPT_TYPE_STRING,    { .str = NULL }, 0, 0, FLAGS },\
    { "input",              "input name of the model",    OFFSET(model_inputname),  AV_OPT_TYPE_STRING,    { .str = NULL }, 0, 0, FLAGS },\
    { "output",             "output name of the model",   OFFSET(model_outputname), AV_OPT_TYPE_STRING,    { .str = NULL }, 0, 0, FLAGS },\
    { "backend_configs",    "backend configs",            OFFSET(backend_options),  AV_OPT_TYPE_STRING,    { .str = NULL }, 0, 0, FLAGS },\
    { "options",            "backend configs",            OFFSET(backend_options),  AV_OPT_TYPE_STRING,    { .str = NULL }, 0, 0, FLAGS },\
    { "async",              "use DNN async inference",    OFFSET(async),            AV_OPT_TYPE_BOOL,      { .i64 = 1},     0, 1, FLAGS},


int ff_dnn_init(DnnContext *ctx, DNNFunctionType func_type, AVFilterContext *filter_ctx);
DNNReturnType ff_dnn_get_input(DnnContext *ctx, DNNData *input);
DNNReturnType ff_dnn_get_output(DnnContext *ctx, int input_width, int input_height, int *output_width, int *output_height);
DNNReturnType ff_dnn_execute_model(DnnContext *ctx, AVFrame *in_frame, AVFrame *out_frame);
DNNReturnType ff_dnn_execute_model_async(DnnContext *ctx, AVFrame *in_frame, AVFrame *out_frame);
DNNAsyncStatusType ff_dnn_get_async_result(DnnContext *ctx, AVFrame **in_frame, AVFrame **out_frame);
DNNReturnType ff_dnn_flush(DnnContext *ctx);
void ff_dnn_uninit(DnnContext *ctx);

#endif
