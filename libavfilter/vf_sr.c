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
 * https://arxiv.org/abs/1609.05158
 */

#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "libavutil/opt.h"
#include "libavformat/avio.h"
#include "libswscale/swscale.h"
#include "dnn_interface.h"

typedef enum {SRCNN, ESPCN} SRModel;

typedef struct SRContext {
    const AVClass *class;

    SRModel model_type;
    char *model_filename;
    DNNBackendType backend_type;
    DNNModule *dnn_module;
    DNNModel *model;
    DNNData input, output;
    int scale_factor;
    struct SwsContext *sws_contexts[3];
    int sws_slice_h, sws_input_linesize, sws_output_linesize;
} SRContext;

#define OFFSET(x) offsetof(SRContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM
static const AVOption sr_options[] = {
    { "model", "specifies what DNN model to use", OFFSET(model_type), AV_OPT_TYPE_FLAGS, { .i64 = 0 }, 0, 1, FLAGS, "model_type" },
    { "srcnn", "Super-Resolution Convolutional Neural Network model (scale factor should be specified for custom SRCNN model)", 0, AV_OPT_TYPE_CONST, { .i64 = 0 }, 0, 0, FLAGS, "model_type" },
    { "espcn", "Efficient Sub-Pixel Convolutional Neural Network model", 0, AV_OPT_TYPE_CONST, { .i64 = 1 }, 0, 0, FLAGS, "model_type" },
    { "dnn_backend", "DNN backend used for model execution", OFFSET(backend_type), AV_OPT_TYPE_FLAGS, { .i64 = 0 }, 0, 1, FLAGS, "backend" },
    { "native", "native backend flag", 0, AV_OPT_TYPE_CONST, { .i64 = 0 }, 0, 0, FLAGS, "backend" },
#if (CONFIG_LIBTENSORFLOW == 1)
    { "tensorflow", "tensorflow backend flag", 0, AV_OPT_TYPE_CONST, { .i64 = 1 }, 0, 0, FLAGS, "backend" },
#endif
    {"scale_factor", "scale factor for SRCNN model", OFFSET(scale_factor), AV_OPT_TYPE_INT, { .i64 = 2 }, 2, 4, FLAGS},
    { "model_filename", "path to model file specifying network architecture and its parameters", OFFSET(model_filename), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(sr);

static av_cold int init(AVFilterContext *context)
{
    SRContext *sr_context = context->priv;

    sr_context->dnn_module = ff_get_dnn_module(sr_context->backend_type);
    if (!sr_context->dnn_module){
        av_log(context, AV_LOG_ERROR, "could not create DNN module for requested backend\n");
        return AVERROR(ENOMEM);
    }
    if (!sr_context->model_filename){
        av_log(context, AV_LOG_VERBOSE, "model file for network was not specified, using default network for x2 upsampling\n");
        sr_context->scale_factor = 2;
        switch (sr_context->model_type){
        case SRCNN:
            sr_context->model = (sr_context->dnn_module->load_default_model)(DNN_SRCNN);
            break;
        case ESPCN:
            sr_context->model = (sr_context->dnn_module->load_default_model)(DNN_ESPCN);
        }
    }
    else{
        sr_context->model = (sr_context->dnn_module->load_model)(sr_context->model_filename);
    }
    if (!sr_context->model){
        av_log(context, AV_LOG_ERROR, "could not load DNN model\n");
        return AVERROR(EIO);
    }

    sr_context->sws_contexts[0] = NULL;
    sr_context->sws_contexts[1] = NULL;
    sr_context->sws_contexts[2] = NULL;

    return 0;
}

static int query_formats(AVFilterContext *context)
{
    const enum AVPixelFormat pixel_formats[] = {AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUV444P,
                                                AV_PIX_FMT_YUV410P, AV_PIX_FMT_YUV411P, AV_PIX_FMT_GRAY8,
                                                AV_PIX_FMT_NONE};
    AVFilterFormats *formats_list;

    formats_list = ff_make_format_list(pixel_formats);
    if (!formats_list){
        av_log(context, AV_LOG_ERROR, "could not create formats list\n");
        return AVERROR(ENOMEM);
    }

    return ff_set_common_formats(context, formats_list);
}

static int config_props(AVFilterLink *inlink)
{
    AVFilterContext *context = inlink->dst;
    SRContext *sr_context = context->priv;
    AVFilterLink *outlink = context->outputs[0];
    DNNReturnType result;
    int sws_src_h, sws_src_w, sws_dst_h, sws_dst_w;

    switch (sr_context->model_type){
    case SRCNN:
        sr_context->input.width = inlink->w * sr_context->scale_factor;
        sr_context->input.height = inlink->h * sr_context->scale_factor;
        break;
    case ESPCN:
        sr_context->input.width = inlink->w;
        sr_context->input.height = inlink->h;
    }
    sr_context->input.channels = 1;

    result = (sr_context->model->set_input_output)(sr_context->model->model, &sr_context->input, &sr_context->output);
    if (result != DNN_SUCCESS){
        av_log(context, AV_LOG_ERROR, "could not set input and output for the model\n");
        return AVERROR(EIO);
    }
    else{
        outlink->h = sr_context->output.height;
        outlink->w = sr_context->output.width;
        sr_context->sws_contexts[1] = sws_getContext(sr_context->input.width, sr_context->input.height, AV_PIX_FMT_GRAY8,
                                                     sr_context->input.width, sr_context->input.height, AV_PIX_FMT_GRAYF32,
                                                     0, NULL, NULL, NULL);
        sr_context->sws_input_linesize = sr_context->input.width << 2;
        sr_context->sws_contexts[2] = sws_getContext(sr_context->output.width, sr_context->output.height, AV_PIX_FMT_GRAYF32,
                                                     sr_context->output.width, sr_context->output.height, AV_PIX_FMT_GRAY8,
                                                     0, NULL, NULL, NULL);
        sr_context->sws_output_linesize = sr_context->output.width << 2;
        if (!sr_context->sws_contexts[1] || !sr_context->sws_contexts[2]){
            av_log(context, AV_LOG_ERROR, "could not create SwsContext for conversions\n");
            return AVERROR(ENOMEM);
        }
        switch (sr_context->model_type){
        case SRCNN:
            sr_context->sws_contexts[0] = sws_getContext(inlink->w, inlink->h, inlink->format,
                                                         outlink->w, outlink->h, outlink->format,
                                                         SWS_BICUBIC, NULL, NULL, NULL);
            if (!sr_context->sws_contexts[0]){
                av_log(context, AV_LOG_ERROR, "could not create SwsContext for scaling\n");
                return AVERROR(ENOMEM);
            }
            sr_context->sws_slice_h = inlink->h;
            break;
        case ESPCN:
            if (inlink->format != AV_PIX_FMT_GRAY8){
                sws_src_h = sr_context->input.height;
                sws_src_w = sr_context->input.width;
                sws_dst_h = sr_context->output.height;
                sws_dst_w = sr_context->output.width;

                switch (inlink->format){
                case AV_PIX_FMT_YUV420P:
                    sws_src_h = AV_CEIL_RSHIFT(sws_src_h, 1);
                    sws_src_w = AV_CEIL_RSHIFT(sws_src_w, 1);
                    sws_dst_h = AV_CEIL_RSHIFT(sws_dst_h, 1);
                    sws_dst_w = AV_CEIL_RSHIFT(sws_dst_w, 1);
                    break;
                case AV_PIX_FMT_YUV422P:
                    sws_src_w = AV_CEIL_RSHIFT(sws_src_w, 1);
                    sws_dst_w = AV_CEIL_RSHIFT(sws_dst_w, 1);
                    break;
                case AV_PIX_FMT_YUV444P:
                    break;
                case AV_PIX_FMT_YUV410P:
                    sws_src_h = AV_CEIL_RSHIFT(sws_src_h, 2);
                    sws_src_w = AV_CEIL_RSHIFT(sws_src_w, 2);
                    sws_dst_h = AV_CEIL_RSHIFT(sws_dst_h, 2);
                    sws_dst_w = AV_CEIL_RSHIFT(sws_dst_w, 2);
                    break;
                case AV_PIX_FMT_YUV411P:
                    sws_src_w = AV_CEIL_RSHIFT(sws_src_w, 2);
                    sws_dst_w = AV_CEIL_RSHIFT(sws_dst_w, 2);
                    break;
                default:
                    av_log(context, AV_LOG_ERROR, "could not create SwsContext for scaling for given input pixel format");
                    return AVERROR(EIO);
                }
                sr_context->sws_contexts[0] = sws_getContext(sws_src_w, sws_src_h, AV_PIX_FMT_GRAY8,
                                                             sws_dst_w, sws_dst_h, AV_PIX_FMT_GRAY8,
                                                             SWS_BICUBIC, NULL, NULL, NULL);
                if (!sr_context->sws_contexts[0]){
                    av_log(context, AV_LOG_ERROR, "could not create SwsContext for scaling\n");
                    return AVERROR(ENOMEM);
                }
                sr_context->sws_slice_h = sws_src_h;
            }
        }

        return 0;
    }
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *context = inlink->dst;
    SRContext *sr_context = context->priv;
    AVFilterLink *outlink = context->outputs[0];
    AVFrame *out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    DNNReturnType dnn_result;

    if (!out){
        av_log(context, AV_LOG_ERROR, "could not allocate memory for output frame\n");
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);
    out->height = sr_context->output.height;
    out->width = sr_context->output.width;
    switch (sr_context->model_type){
    case SRCNN:
        sws_scale(sr_context->sws_contexts[0], (const uint8_t **)in->data, in->linesize,
                  0, sr_context->sws_slice_h, out->data, out->linesize);

        sws_scale(sr_context->sws_contexts[1], (const uint8_t **)out->data, out->linesize,
                  0, out->height, (uint8_t * const*)(&sr_context->input.data), &sr_context->sws_input_linesize);
        break;
    case ESPCN:
        if (sr_context->sws_contexts[0]){
            sws_scale(sr_context->sws_contexts[0], (const uint8_t **)(in->data + 1), in->linesize + 1,
                      0, sr_context->sws_slice_h, out->data + 1, out->linesize + 1);
            sws_scale(sr_context->sws_contexts[0], (const uint8_t **)(in->data + 2), in->linesize + 2,
                      0, sr_context->sws_slice_h, out->data + 2, out->linesize + 2);
        }

        sws_scale(sr_context->sws_contexts[1], (const uint8_t **)in->data, in->linesize,
                  0, in->height, (uint8_t * const*)(&sr_context->input.data), &sr_context->sws_input_linesize);
    }
    av_frame_free(&in);

    dnn_result = (sr_context->dnn_module->execute_model)(sr_context->model);
    if (dnn_result != DNN_SUCCESS){
        av_log(context, AV_LOG_ERROR, "failed to execute loaded model\n");
        return AVERROR(EIO);
    }

    sws_scale(sr_context->sws_contexts[2], (const uint8_t **)(&sr_context->output.data), &sr_context->sws_output_linesize,
              0, out->height, (uint8_t * const*)out->data, out->linesize);

    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *context)
{
    int i;
    SRContext *sr_context = context->priv;

    if (sr_context->dnn_module){
        (sr_context->dnn_module->free_model)(&sr_context->model);
        av_freep(&sr_context->dnn_module);
    }

    for (i = 0; i < 3; ++i){
        if (sr_context->sws_contexts[i]){
            sws_freeContext(sr_context->sws_contexts[i]);
        }
    }
}

static const AVFilterPad sr_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_props,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad sr_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_sr = {
    .name          = "sr",
    .description   = NULL_IF_CONFIG_SMALL("Apply DNN-based image super resolution to the input."),
    .priv_size     = sizeof(SRContext),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = sr_inputs,
    .outputs       = sr_outputs,
    .priv_class    = &sr_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
};

