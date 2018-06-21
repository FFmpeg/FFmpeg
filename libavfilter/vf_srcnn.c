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
 * Filter implementing image super-resolution using deep convolutional networks.
 * https://arxiv.org/abs/1501.00092
 */

#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "libavutil/opt.h"
#include "libavformat/avio.h"
#include "dnn_interface.h"

typedef struct SRCNNContext {
    const AVClass *class;

    char* model_filename;
    float* input_output_buf;
    DNNBackendType backend_type;
    DNNModule* dnn_module;
    DNNModel* model;
    DNNData input_output;
} SRCNNContext;

#define OFFSET(x) offsetof(SRCNNContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM
static const AVOption srcnn_options[] = {
    { "dnn_backend", "DNN backend used for model execution", OFFSET(backend_type), AV_OPT_TYPE_FLAGS, { .i64 = 0 }, 0, 1, FLAGS, "backend" },
    { "native", "native backend flag", 0, AV_OPT_TYPE_CONST, { .i64 = 0 }, 0, 0, FLAGS, "backend" },
#if (CONFIG_LIBTENSORFLOW == 1)
    { "tensorflow", "tensorflow backend flag", 0, AV_OPT_TYPE_CONST, { .i64 = 1 }, 0, 0, FLAGS, "backend" },
#endif
    { "model_filename", "path to model file specifying network architecture and its parameters", OFFSET(model_filename), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(srcnn);

static av_cold int init(AVFilterContext* context)
{
    SRCNNContext* srcnn_context = context->priv;

    srcnn_context->dnn_module = ff_get_dnn_module(srcnn_context->backend_type);
    if (!srcnn_context->dnn_module){
        av_log(context, AV_LOG_ERROR, "could not create DNN module for requested backend\n");
        return AVERROR(ENOMEM);
    }
    if (!srcnn_context->model_filename){
        av_log(context, AV_LOG_VERBOSE, "model file for network was not specified, using default network for x2 upsampling\n");
        srcnn_context->model = (srcnn_context->dnn_module->load_default_model)(DNN_SRCNN);
    }
    else{
        srcnn_context->model = (srcnn_context->dnn_module->load_model)(srcnn_context->model_filename);
    }
    if (!srcnn_context->model){
        av_log(context, AV_LOG_ERROR, "could not load DNN model\n");
        return AVERROR(EIO);
    }

    return 0;
}

static int query_formats(AVFilterContext* context)
{
    const enum AVPixelFormat pixel_formats[] = {AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUV444P,
                                                AV_PIX_FMT_YUV410P, AV_PIX_FMT_YUV411P, AV_PIX_FMT_GRAY8,
                                                AV_PIX_FMT_NONE};
    AVFilterFormats* formats_list;

    formats_list = ff_make_format_list(pixel_formats);
    if (!formats_list){
        av_log(context, AV_LOG_ERROR, "could not create formats list\n");
        return AVERROR(ENOMEM);
    }
    return ff_set_common_formats(context, formats_list);
}

static int config_props(AVFilterLink* inlink)
{
    AVFilterContext* context = inlink->dst;
    SRCNNContext* srcnn_context = context->priv;
    DNNReturnType result;

    srcnn_context->input_output_buf = av_malloc(inlink->h * inlink->w * sizeof(float));
    if (!srcnn_context->input_output_buf){
        av_log(context, AV_LOG_ERROR, "could not allocate memory for input/output buffer\n");
        return AVERROR(ENOMEM);
    }

    srcnn_context->input_output.data = srcnn_context->input_output_buf;
    srcnn_context->input_output.width = inlink->w;
    srcnn_context->input_output.height = inlink->h;
    srcnn_context->input_output.channels = 1;

    result = (srcnn_context->model->set_input_output)(srcnn_context->model->model, &srcnn_context->input_output, &srcnn_context->input_output);
    if (result != DNN_SUCCESS){
        av_log(context, AV_LOG_ERROR, "could not set input and output for the model\n");
        return AVERROR(EIO);
    }
    else{
        return 0;
    }
}

typedef struct ThreadData{
    uint8_t* out;
    int out_linesize, height, width;
} ThreadData;

static int uint8_to_float(AVFilterContext* context, void* arg, int jobnr, int nb_jobs)
{
    SRCNNContext* srcnn_context = context->priv;
    const ThreadData* td = arg;
    const int slice_start = (td->height *  jobnr     ) / nb_jobs;
    const int slice_end   = (td->height * (jobnr + 1)) / nb_jobs;
    const uint8_t* src = td->out + slice_start * td->out_linesize;
    float* dst = srcnn_context->input_output_buf + slice_start * td->width;
    int y, x;

    for (y = slice_start; y < slice_end; ++y){
        for (x = 0; x < td->width; ++x){
            dst[x] = (float)src[x] / 255.0f;
        }
        src += td->out_linesize;
        dst += td->width;
    }

    return 0;
}

static int float_to_uint8(AVFilterContext* context, void* arg, int jobnr, int nb_jobs)
{
    SRCNNContext* srcnn_context = context->priv;
    const ThreadData* td = arg;
    const int slice_start = (td->height *  jobnr     ) / nb_jobs;
    const int slice_end   = (td->height * (jobnr + 1)) / nb_jobs;
    const float* src = srcnn_context->input_output_buf + slice_start * td->width;
    uint8_t* dst = td->out + slice_start * td->out_linesize;
    int y, x;

    for (y = slice_start; y < slice_end; ++y){
        for (x = 0; x < td->width; ++x){
            dst[x] = (uint8_t)(255.0f * FFMIN(src[x], 1.0f));
        }
        src += td->width;
        dst += td->out_linesize;
    }

    return 0;
}

static int filter_frame(AVFilterLink* inlink, AVFrame* in)
{
    AVFilterContext* context = inlink->dst;
    SRCNNContext* srcnn_context = context->priv;
    AVFilterLink* outlink = context->outputs[0];
    AVFrame* out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    ThreadData td;
    int nb_threads;
    DNNReturnType dnn_result;

    if (!out){
        av_log(context, AV_LOG_ERROR, "could not allocate memory for output frame\n");
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);
    av_frame_copy(out, in);
    av_frame_free(&in);
    td.out = out->data[0];
    td.out_linesize = out->linesize[0];
    td.height = out->height;
    td.width = out->width;

    nb_threads = ff_filter_get_nb_threads(context);
    context->internal->execute(context, uint8_to_float, &td, NULL, FFMIN(td.height, nb_threads));

    dnn_result = (srcnn_context->dnn_module->execute_model)(srcnn_context->model);
    if (dnn_result != DNN_SUCCESS){
        av_log(context, AV_LOG_ERROR, "failed to execute loaded model\n");
        return AVERROR(EIO);
    }

    context->internal->execute(context, float_to_uint8, &td, NULL, FFMIN(td.height, nb_threads));

    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext* context)
{
    SRCNNContext* srcnn_context = context->priv;

    if (srcnn_context->dnn_module){
        (srcnn_context->dnn_module->free_model)(&srcnn_context->model);
        av_freep(&srcnn_context->dnn_module);
    }
    av_freep(&srcnn_context->input_output_buf);
}

static const AVFilterPad srcnn_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_props,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad srcnn_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_srcnn = {
    .name          = "srcnn",
    .description   = NULL_IF_CONFIG_SMALL("Apply super resolution convolutional neural network to the input. Use bicubic upsamping with corresponding scaling factor before."),
    .priv_size     = sizeof(SRCNNContext),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = srcnn_inputs,
    .outputs       = srcnn_outputs,
    .priv_class    = &srcnn_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
};

