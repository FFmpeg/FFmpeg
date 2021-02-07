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

#include "dnn_filter_common.h"

int ff_dnn_init(DnnContext *ctx, DNNFunctionType func_type, AVFilterContext *filter_ctx)
{
    if (!ctx->model_filename) {
        av_log(filter_ctx, AV_LOG_ERROR, "model file for network is not specified\n");
        return AVERROR(EINVAL);
    }
    if (!ctx->model_inputname) {
        av_log(filter_ctx, AV_LOG_ERROR, "input name of the model network is not specified\n");
        return AVERROR(EINVAL);
    }
    if (!ctx->model_outputname) {
        av_log(filter_ctx, AV_LOG_ERROR, "output name of the model network is not specified\n");
        return AVERROR(EINVAL);
    }

    ctx->dnn_module = ff_get_dnn_module(ctx->backend_type);
    if (!ctx->dnn_module) {
        av_log(filter_ctx, AV_LOG_ERROR, "could not create DNN module for requested backend\n");
        return AVERROR(ENOMEM);
    }
    if (!ctx->dnn_module->load_model) {
        av_log(filter_ctx, AV_LOG_ERROR, "load_model for network is not specified\n");
        return AVERROR(EINVAL);
    }

    ctx->model = (ctx->dnn_module->load_model)(ctx->model_filename, func_type, ctx->backend_options, filter_ctx);
    if (!ctx->model) {
        av_log(filter_ctx, AV_LOG_ERROR, "could not load DNN model\n");
        return AVERROR(EINVAL);
    }

    if (!ctx->dnn_module->execute_model_async && ctx->async) {
        ctx->async = 0;
        av_log(filter_ctx, AV_LOG_WARNING, "this backend does not support async execution, roll back to sync.\n");
    }

#if !HAVE_PTHREAD_CANCEL
    if (ctx->async) {
        ctx->async = 0;
        av_log(filter_ctx, AV_LOG_WARNING, "pthread is not supported, roll back to sync.\n");
    }
#endif

    return 0;
}

DNNReturnType ff_dnn_get_input(DnnContext *ctx, DNNData *input)
{
    return ctx->model->get_input(ctx->model->model, input, ctx->model_inputname);
}

DNNReturnType ff_dnn_get_output(DnnContext *ctx, int input_width, int input_height, int *output_width, int *output_height)
{
    return ctx->model->get_output(ctx->model->model, ctx->model_inputname, input_width, input_height,
                                    ctx->model_outputname, output_width, output_height);
}

DNNReturnType ff_dnn_execute_model(DnnContext *ctx, AVFrame *in_frame, AVFrame *out_frame)
{
    return (ctx->dnn_module->execute_model)(ctx->model, ctx->model_inputname, in_frame,
                                            (const char **)&ctx->model_outputname, 1, out_frame);
}

DNNReturnType ff_dnn_execute_model_async(DnnContext *ctx, AVFrame *in_frame, AVFrame *out_frame)
{
    return (ctx->dnn_module->execute_model_async)(ctx->model, ctx->model_inputname, in_frame,
                                                  (const char **)&ctx->model_outputname, 1, out_frame);
}

DNNAsyncStatusType ff_dnn_get_async_result(DnnContext *ctx, AVFrame **in_frame, AVFrame **out_frame)
{
    return (ctx->dnn_module->get_async_result)(ctx->model, in_frame, out_frame);
}

DNNReturnType ff_dnn_flush(DnnContext *ctx)
{
    return (ctx->dnn_module->flush)(ctx->model);
}

void ff_dnn_uninit(DnnContext *ctx)
{
    if (ctx->dnn_module) {
        (ctx->dnn_module->free_model)(&ctx->model);
        av_freep(&ctx->dnn_module);
    }
}
