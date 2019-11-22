/*
 * Copyright (c) 2019 Guo Yejun
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
 * implementing a generic image processing filter using deep learning networks.
 */

#include "libavformat/avio.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/avassert.h"
#include "avfilter.h"
#include "dnn_interface.h"
#include "formats.h"
#include "internal.h"

typedef struct DnnProcessingContext {
    const AVClass *class;

    char *model_filename;
    DNNBackendType backend_type;
    enum AVPixelFormat fmt;
    char *model_inputname;
    char *model_outputname;

    DNNModule *dnn_module;
    DNNModel *model;

    // input & output of the model at execution time
    DNNData input;
    DNNData output;
} DnnProcessingContext;

#define OFFSET(x) offsetof(DnnProcessingContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM
static const AVOption dnn_processing_options[] = {
    { "dnn_backend", "DNN backend",                OFFSET(backend_type),     AV_OPT_TYPE_INT,       { .i64 = 0 },    0, 1, FLAGS, "backend" },
    { "native",      "native backend flag",        0,                        AV_OPT_TYPE_CONST,     { .i64 = 0 },    0, 0, FLAGS, "backend" },
#if (CONFIG_LIBTENSORFLOW == 1)
    { "tensorflow",  "tensorflow backend flag",    0,                        AV_OPT_TYPE_CONST,     { .i64 = 1 },    0, 0, FLAGS, "backend" },
#endif
    { "model",       "path to model file",         OFFSET(model_filename),   AV_OPT_TYPE_STRING,    { .str = NULL }, 0, 0, FLAGS },
    { "input",       "input name of the model",    OFFSET(model_inputname),  AV_OPT_TYPE_STRING,    { .str = NULL }, 0, 0, FLAGS },
    { "output",      "output name of the model",   OFFSET(model_outputname), AV_OPT_TYPE_STRING,    { .str = NULL }, 0, 0, FLAGS },
    { "fmt",         "AVPixelFormat of the frame", OFFSET(fmt),              AV_OPT_TYPE_PIXEL_FMT, { .i64=AV_PIX_FMT_RGB24 }, AV_PIX_FMT_NONE, AV_PIX_FMT_NB - 1, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(dnn_processing);

static av_cold int init(AVFilterContext *context)
{
    DnnProcessingContext *ctx = context->priv;
    int supported = 0;
    // as the first step, only rgb24 and bgr24 are supported
    const enum AVPixelFormat supported_pixel_fmts[] = {
        AV_PIX_FMT_RGB24,
        AV_PIX_FMT_BGR24,
    };
    for (int i = 0; i < sizeof(supported_pixel_fmts) / sizeof(enum AVPixelFormat); ++i) {
        if (supported_pixel_fmts[i] == ctx->fmt) {
            supported = 1;
            break;
        }
    }
    if (!supported) {
        av_log(context, AV_LOG_ERROR, "pixel fmt %s not supported yet\n",
                                       av_get_pix_fmt_name(ctx->fmt));
        return AVERROR(AVERROR_INVALIDDATA);
    }

    if (!ctx->model_filename) {
        av_log(ctx, AV_LOG_ERROR, "model file for network is not specified\n");
        return AVERROR(EINVAL);
    }
    if (!ctx->model_inputname) {
        av_log(ctx, AV_LOG_ERROR, "input name of the model network is not specified\n");
        return AVERROR(EINVAL);
    }
    if (!ctx->model_outputname) {
        av_log(ctx, AV_LOG_ERROR, "output name of the model network is not specified\n");
        return AVERROR(EINVAL);
    }

    ctx->dnn_module = ff_get_dnn_module(ctx->backend_type);
    if (!ctx->dnn_module) {
        av_log(ctx, AV_LOG_ERROR, "could not create DNN module for requested backend\n");
        return AVERROR(ENOMEM);
    }
    if (!ctx->dnn_module->load_model) {
        av_log(ctx, AV_LOG_ERROR, "load_model for network is not specified\n");
        return AVERROR(EINVAL);
    }

    ctx->model = (ctx->dnn_module->load_model)(ctx->model_filename);
    if (!ctx->model) {
        av_log(ctx, AV_LOG_ERROR, "could not load DNN model\n");
        return AVERROR(EINVAL);
    }

    return 0;
}

static int query_formats(AVFilterContext *context)
{
    AVFilterFormats *formats;
    DnnProcessingContext *ctx = context->priv;
    enum AVPixelFormat pixel_fmts[2];
    pixel_fmts[0] = ctx->fmt;
    pixel_fmts[1] = AV_PIX_FMT_NONE;

    formats = ff_make_format_list(pixel_fmts);
    return ff_set_common_formats(context, formats);
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *context     = inlink->dst;
    DnnProcessingContext *ctx = context->priv;
    DNNReturnType result;
    DNNData model_input;

    result = ctx->model->get_input(ctx->model->model, &model_input, ctx->model_inputname);
    if (result != DNN_SUCCESS) {
        av_log(ctx, AV_LOG_ERROR, "could not get input from the model\n");
        return AVERROR(EIO);
    }

    // the design is to add explicit scale filter before this filter
    if (model_input.height != -1 && model_input.height != inlink->h) {
        av_log(ctx, AV_LOG_ERROR, "the model requires frame height %d but got %d\n",
                                   model_input.height, inlink->h);
        return AVERROR(EIO);
    }
    if (model_input.width != -1 && model_input.width != inlink->w) {
        av_log(ctx, AV_LOG_ERROR, "the model requires frame width %d but got %d\n",
                                   model_input.width, inlink->w);
        return AVERROR(EIO);
    }

    if (model_input.channels != 3) {
        av_log(ctx, AV_LOG_ERROR, "the model requires input channels %d\n",
                                   model_input.channels);
        return AVERROR(EIO);
    }
    if (model_input.dt != DNN_FLOAT && model_input.dt != DNN_UINT8) {
        av_log(ctx, AV_LOG_ERROR, "only support dnn models with input data type as float32 and uint8.\n");
        return AVERROR(EIO);
    }

    ctx->input.width    = inlink->w;
    ctx->input.height   = inlink->h;
    ctx->input.channels = model_input.channels;
    ctx->input.dt = model_input.dt;

    result = (ctx->model->set_input_output)(ctx->model->model,
                                        &ctx->input, ctx->model_inputname,
                                        (const char **)&ctx->model_outputname, 1);
    if (result != DNN_SUCCESS) {
        av_log(ctx, AV_LOG_ERROR, "could not set input and output for the model\n");
        return AVERROR(EIO);
    }

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *context = outlink->src;
    DnnProcessingContext *ctx = context->priv;
    DNNReturnType result;

    // have a try run in case that the dnn model resize the frame
    result = (ctx->dnn_module->execute_model)(ctx->model, &ctx->output, 1);
    if (result != DNN_SUCCESS){
        av_log(ctx, AV_LOG_ERROR, "failed to execute model\n");
        return AVERROR(EIO);
    }

    outlink->w = ctx->output.width;
    outlink->h = ctx->output.height;

    return 0;
}

static int copy_from_frame_to_dnn(DNNData *dnn_input, const AVFrame *frame)
{
    // extend this function to support more formats
    av_assert0(frame->format == AV_PIX_FMT_RGB24 || frame->format == AV_PIX_FMT_BGR24);

    if (dnn_input->dt == DNN_FLOAT) {
        float *dnn_input_data = dnn_input->data;
        for (int i = 0; i < frame->height; i++) {
            for(int j = 0; j < frame->width * 3; j++) {
                int k = i * frame->linesize[0] + j;
                int t = i * frame->width * 3 + j;
                dnn_input_data[t] = frame->data[0][k] / 255.0f;
            }
        }
    } else {
        uint8_t *dnn_input_data = dnn_input->data;
        av_assert0(dnn_input->dt == DNN_UINT8);
        for (int i = 0; i < frame->height; i++) {
            for(int j = 0; j < frame->width * 3; j++) {
                int k = i * frame->linesize[0] + j;
                int t = i * frame->width * 3 + j;
                dnn_input_data[t] = frame->data[0][k];
            }
        }
    }

    return 0;
}

static int copy_from_dnn_to_frame(AVFrame *frame, const DNNData *dnn_output)
{
    // extend this function to support more formats
    av_assert0(frame->format == AV_PIX_FMT_RGB24 || frame->format == AV_PIX_FMT_BGR24);

    if (dnn_output->dt == DNN_FLOAT) {
        float *dnn_output_data = dnn_output->data;
        for (int i = 0; i < frame->height; i++) {
            for(int j = 0; j < frame->width * 3; j++) {
                int k = i * frame->linesize[0] + j;
                int t = i * frame->width * 3 + j;
                frame->data[0][k] = av_clip_uintp2((int)(dnn_output_data[t] * 255.0f), 8);
            }
        }
    } else {
        uint8_t *dnn_output_data = dnn_output->data;
        av_assert0(dnn_output->dt == DNN_UINT8);
        for (int i = 0; i < frame->height; i++) {
            for(int j = 0; j < frame->width * 3; j++) {
                int k = i * frame->linesize[0] + j;
                int t = i * frame->width * 3 + j;
                frame->data[0][k] = dnn_output_data[t];
            }
        }
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *context  = inlink->dst;
    AVFilterLink *outlink = context->outputs[0];
    DnnProcessingContext *ctx = context->priv;
    DNNReturnType dnn_result;
    AVFrame *out;

    copy_from_frame_to_dnn(&ctx->input, in);

    dnn_result = (ctx->dnn_module->execute_model)(ctx->model, &ctx->output, 1);
    if (dnn_result != DNN_SUCCESS){
        av_log(ctx, AV_LOG_ERROR, "failed to execute model\n");
        av_frame_free(&in);
        return AVERROR(EIO);
    }
    av_assert0(ctx->output.channels == 3);

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }

    av_frame_copy_props(out, in);
    copy_from_dnn_to_frame(out, &ctx->output);
    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    DnnProcessingContext *context = ctx->priv;

    if (context->dnn_module)
        (context->dnn_module->free_model)(&context->model);

    av_freep(&context->dnn_module);
}

static const AVFilterPad dnn_processing_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad dnn_processing_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
    { NULL }
};

AVFilter ff_vf_dnn_processing = {
    .name          = "dnn_processing",
    .description   = NULL_IF_CONFIG_SMALL("Apply DNN processing filter to the input."),
    .priv_size     = sizeof(DnnProcessingContext),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = dnn_processing_inputs,
    .outputs       = dnn_processing_outputs,
    .priv_class    = &dnn_processing_class,
};
