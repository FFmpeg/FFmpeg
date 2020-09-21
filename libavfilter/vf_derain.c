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

    int                filter_type;
    char              *model_filename;
    DNNBackendType     backend_type;
    DNNModule         *dnn_module;
    DNNModel          *model;
} DRContext;

#define OFFSET(x) offsetof(DRContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM
static const AVOption derain_options[] = {
    { "filter_type", "filter type(derain/dehaze)",  OFFSET(filter_type),    AV_OPT_TYPE_INT,    { .i64 = 0 },    0, 1, FLAGS, "type" },
    { "derain",      "derain filter flag",          0,                      AV_OPT_TYPE_CONST,  { .i64 = 0 },    0, 0, FLAGS, "type" },
    { "dehaze",      "dehaze filter flag",          0,                      AV_OPT_TYPE_CONST,  { .i64 = 1 },    0, 0, FLAGS, "type" },
    { "dnn_backend", "DNN backend",                 OFFSET(backend_type),   AV_OPT_TYPE_INT,    { .i64 = 0 },    0, 1, FLAGS, "backend" },
    { "native",      "native backend flag",         0,                      AV_OPT_TYPE_CONST,  { .i64 = 0 },    0, 0, FLAGS, "backend" },
#if (CONFIG_LIBTENSORFLOW == 1)
    { "tensorflow",  "tensorflow backend flag",     0,                      AV_OPT_TYPE_CONST,  { .i64 = 1 },    0, 0, FLAGS, "backend" },
#endif
    { "model",       "path to model file",          OFFSET(model_filename), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, FLAGS },
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

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx  = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    DRContext *dr_context = ctx->priv;
    DNNReturnType dnn_result;
    const char *model_output_name = "y";
    AVFrame *out;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_log(ctx, AV_LOG_ERROR, "could not allocate memory for output frame\n");
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

    dnn_result = (dr_context->dnn_module->execute_model)(dr_context->model, "x", in, &model_output_name, 1, out);
    if (dnn_result != DNN_SUCCESS){
        av_log(ctx, AV_LOG_ERROR, "failed to execute model\n");
        av_frame_free(&in);
        return AVERROR(EIO);
    }

    av_frame_free(&in);

    return ff_filter_frame(outlink, out);
}

static av_cold int init(AVFilterContext *ctx)
{
    DRContext *dr_context = ctx->priv;

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

    dr_context->model = (dr_context->dnn_module->load_model)(dr_context->model_filename, NULL, NULL);
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
