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
#include "libavutil/avstring.h"

#define MAX_SUPPORTED_OUTPUTS_NB 4

static char **separate_output_names(const char *expr, const char *val_sep, int *separated_nb)
{
    char *val, **parsed_vals = NULL;
    int val_num = 0;
    if (!expr || !val_sep || !separated_nb) {
        return NULL;
    }

    parsed_vals = av_calloc(MAX_SUPPORTED_OUTPUTS_NB, sizeof(*parsed_vals));
    if (!parsed_vals) {
        return NULL;
    }

    do {
        val = av_get_token(&expr, val_sep);
        if(val) {
            parsed_vals[val_num] = val;
            val_num++;
        }
        if (*expr) {
            expr++;
        }
    } while(*expr);

    parsed_vals[val_num] = NULL;
    *separated_nb = val_num;

    return parsed_vals;
}

int ff_dnn_init(DnnContext *ctx, DNNFunctionType func_type, AVFilterContext *filter_ctx)
{
    DNNBackendType backend = ctx->backend_type;

    if (!ctx->model_filename) {
        av_log(filter_ctx, AV_LOG_ERROR, "model file for network is not specified\n");
        return AVERROR(EINVAL);
    }

    if (backend == DNN_TH) {
        if (ctx->model_inputname)
            av_log(filter_ctx, AV_LOG_WARNING, "LibTorch backend do not require inputname, "\
                                               "inputname will be ignored.\n");
        if (ctx->model_outputnames)
            av_log(filter_ctx, AV_LOG_WARNING, "LibTorch backend do not require outputname(s), "\
                                               "all outputname(s) will be ignored.\n");
        ctx->nb_outputs = 1;
    } else if (backend == DNN_TF) {
        if (!ctx->model_inputname) {
            av_log(filter_ctx, AV_LOG_ERROR, "input name of the model network is not specified\n");
            return AVERROR(EINVAL);
        }
        ctx->model_outputnames = separate_output_names(ctx->model_outputnames_string, "&", &ctx->nb_outputs);
        if (!ctx->model_outputnames) {
            av_log(filter_ctx, AV_LOG_ERROR, "could not parse model output names\n");
            return AVERROR(EINVAL);
        }
    }

    ctx->dnn_module = ff_get_dnn_module(ctx->backend_type, filter_ctx);
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

    return 0;
}

int ff_dnn_set_frame_proc(DnnContext *ctx, FramePrePostProc pre_proc, FramePrePostProc post_proc)
{
    ctx->model->frame_pre_proc = pre_proc;
    ctx->model->frame_post_proc = post_proc;
    return 0;
}

int ff_dnn_set_detect_post_proc(DnnContext *ctx, DetectPostProc post_proc)
{
    ctx->model->detect_post_proc = post_proc;
    return 0;
}

int ff_dnn_set_classify_post_proc(DnnContext *ctx, ClassifyPostProc post_proc)
{
    ctx->model->classify_post_proc = post_proc;
    return 0;
}

int ff_dnn_get_input(DnnContext *ctx, DNNData *input)
{
    return ctx->model->get_input(ctx->model->model, input, ctx->model_inputname);
}

int ff_dnn_get_output(DnnContext *ctx, int input_width, int input_height, int *output_width, int *output_height)
{
    char * output_name = ctx->model_outputnames && ctx->backend_type != DNN_TH ?
                         ctx->model_outputnames[0] : NULL;
    return ctx->model->get_output(ctx->model->model, ctx->model_inputname, input_width, input_height,
                                    (const char *)output_name, output_width, output_height);
}

int ff_dnn_execute_model(DnnContext *ctx, AVFrame *in_frame, AVFrame *out_frame)
{
    DNNExecBaseParams exec_params = {
        .input_name     = ctx->model_inputname,
        .output_names   = (const char **)ctx->model_outputnames,
        .nb_output      = ctx->nb_outputs,
        .in_frame       = in_frame,
        .out_frame      = out_frame,
    };
    return (ctx->dnn_module->execute_model)(ctx->model, &exec_params);
}

int ff_dnn_execute_model_classification(DnnContext *ctx, AVFrame *in_frame, AVFrame *out_frame, const char *target)
{
    DNNExecClassificationParams class_params = {
        {
            .input_name     = ctx->model_inputname,
            .output_names   = (const char **)ctx->model_outputnames,
            .nb_output      = ctx->nb_outputs,
            .in_frame       = in_frame,
            .out_frame      = out_frame,
        },
        .target = target,
    };
    return (ctx->dnn_module->execute_model)(ctx->model, &class_params.base);
}

DNNAsyncStatusType ff_dnn_get_result(DnnContext *ctx, AVFrame **in_frame, AVFrame **out_frame)
{
    return (ctx->dnn_module->get_result)(ctx->model, in_frame, out_frame);
}

int ff_dnn_flush(DnnContext *ctx)
{
    return (ctx->dnn_module->flush)(ctx->model);
}

void ff_dnn_uninit(DnnContext *ctx)
{
    if (ctx->dnn_module) {
        (ctx->dnn_module->free_model)(&ctx->model);
    }
    if (ctx->model_outputnames) {
        for (int i = 0; i < ctx->nb_outputs; i++)
            av_free(ctx->model_outputnames[i]);

        av_freep(&ctx->model_outputnames);
    }
}
