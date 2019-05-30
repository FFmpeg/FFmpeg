/*
 * Copyright (c) 2019 Xuewei Meng
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
 * Filter implementing image derain filter using deep convolutional networks.
 * http://openaccess.thecvf.com/content_ECCV_2018/html/Xia_Li_Recurrent_Squeeze-and-Excitation_Context_ECCV_2018_paper.html
 */

#include "libavformat/avio.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "dnn_interface.h"
#include "formats.h"
#include "internal.h"

typedef struct DRContext {
    const AVClass *class;

    char              *model_filename;
    DNNBackendType     backend_type;
    DNNModule         *dnn_module;
    DNNModel          *model;
    DNNInputData       input;
    DNNData            output;
} DRContext;

#define CLIP(x, min, max) (x < min ? min : (x > max ? max : x))
#define OFFSET(x) offsetof(DRContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM
static const AVOption derain_options[] = {
    { "dnn_backend", "DNN backend",             OFFSET(backend_type),   AV_OPT_TYPE_INT,    { .i64 = 0 },    0, 1, FLAGS, "backend" },
    { "native",      "native backend flag",     0,                      AV_OPT_TYPE_CONST,  { .i64 = 0 },    0, 0, FLAGS, "backend" },
#if (CONFIG_LIBTENSORFLOW == 1)
    { "tensorflow",  "tensorflow backend flag", 0,                      AV_OPT_TYPE_CONST,  { .i64 = 1 },    0, 0, FLAGS, "backend" },
#endif
    { "model",       "path to model file",      OFFSET(model_filename), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(derain);

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats;
    const enum AVPixelFormat pixel_fmts[] = {
        AV_PIX_FMT_RGB24,
        AV_PIX_FMT_NONE
    };

    formats = ff_make_format_list(pixel_fmts);

    return ff_set_common_formats(ctx, formats);
}

static int config_inputs(AVFilterLink *inlink)
{
    AVFilterContext *ctx          = inlink->dst;
    DRContext *dr_context         = ctx->priv;
    const char *model_output_name = "y";
    DNNReturnType result;

    dr_context->input.width    = inlink->w;
    dr_context->input.height   = inlink->h;
    dr_context->input.channels = 3;

    result = (dr_context->model->set_input_output)(dr_context->model->model, &dr_context->input, "x", &model_output_name, 1);
    if (result != DNN_SUCCESS) {
        av_log(ctx, AV_LOG_ERROR, "could not set input and output for the model\n");
        return AVERROR(EIO);
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx  = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    DRContext *dr_context = ctx->priv;
    DNNReturnType dnn_result;
    int pad_size;

    AVFrame *out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_log(ctx, AV_LOG_ERROR, "could not allocate memory for output frame\n");
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }

    av_frame_copy_props(out, in);

    for (int i = 0; i < in->height; i++){
        for(int j = 0; j < in->width * 3; j++){
            int k = i * in->linesize[0] + j;
            int t = i * in->width * 3 + j;
            ((float *)dr_context->input.data)[t] = in->data[0][k] / 255.0;
        }
    }

    dnn_result = (dr_context->dnn_module->execute_model)(dr_context->model, &dr_context->output, 1);
    if (dnn_result != DNN_SUCCESS){
        av_log(ctx, AV_LOG_ERROR, "failed to execute model\n");
        return AVERROR(EIO);
    }

    out->height = dr_context->output.height;
    out->width  = dr_context->output.width;
    outlink->h  = dr_context->output.height;
    outlink->w  = dr_context->output.width;
    pad_size    = (in->height - out->height) >> 1;

    for (int i = 0; i < out->height; i++){
        for(int j = 0; j < out->width * 3; j++){
            int k = i * out->linesize[0] + j;
            int t = i * out->width * 3 + j;

            int t_in =  (i + pad_size) * in->width * 3 + j + pad_size * 3;
            out->data[0][k] = CLIP((int)((((float *)dr_context->input.data)[t_in] - dr_context->output.data[t]) * 255), 0, 255);
        }
    }

    av_frame_free(&in);

    return ff_filter_frame(outlink, out);
}

static av_cold int init(AVFilterContext *ctx)
{
    DRContext *dr_context = ctx->priv;

    dr_context->input.dt = DNN_FLOAT;
    dr_context->dnn_module = ff_get_dnn_module(dr_context->backend_type);
    if (!dr_context->dnn_module) {
        av_log(ctx, AV_LOG_ERROR, "could not create DNN module for requested backend\n");
        return AVERROR(ENOMEM);
    }
    if (!dr_context->model_filename) {
        av_log(ctx, AV_LOG_ERROR, "model file for network is not specified\n");
        return AVERROR(EINVAL);
    }
    if (!dr_context->dnn_module->load_model) {
        av_log(ctx, AV_LOG_ERROR, "load_model for network is not specified\n");
        return AVERROR(EINVAL);
    }

    dr_context->model = (dr_context->dnn_module->load_model)(dr_context->model_filename);
    if (!dr_context->model) {
        av_log(ctx, AV_LOG_ERROR, "could not load DNN model\n");
        return AVERROR(EINVAL);
    }

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    DRContext *dr_context = ctx->priv;

    if (dr_context->dnn_module) {
        (dr_context->dnn_module->free_model)(&dr_context->model);
        av_freep(&dr_context->dnn_module);
    }
}

static const AVFilterPad derain_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_inputs,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad derain_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_derain = {
    .name          = "derain",
    .description   = NULL_IF_CONFIG_SMALL("Apply derain filter to the input."),
    .priv_size     = sizeof(DRContext),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = derain_inputs,
    .outputs       = derain_outputs,
    .priv_class    = &derain_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
