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
#include "filters.h"
#include "video.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libswscale/swscale.h"
#include "dnn_filter_common.h"

typedef struct SRContext {
    const AVClass *class;
    DnnContext dnnctx;
    int scale_factor;
    struct SwsContext *sws_uv_scale;
    int sws_uv_height;
    struct SwsContext *sws_pre_scale;
} SRContext;

#define OFFSET(x) offsetof(SRContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM
static const AVOption sr_options[] = {
    { "dnn_backend", "DNN backend used for model execution", OFFSET(dnnctx.backend_type), AV_OPT_TYPE_INT, { .i64 = 1 }, 0, 1, FLAGS, .unit = "backend" },
#if (CONFIG_LIBTENSORFLOW == 1)
    { "tensorflow", "tensorflow backend flag", 0, AV_OPT_TYPE_CONST, { .i64 = 1 }, 0, 0, FLAGS, .unit = "backend" },
#endif
    { "scale_factor", "scale factor for SRCNN model", OFFSET(scale_factor), AV_OPT_TYPE_INT, { .i64 = 2 }, 2, 4, FLAGS },
    { NULL }
};

AVFILTER_DNN_DEFINE_CLASS(sr, DNN_TF);

static av_cold int init(AVFilterContext *context)
{
    SRContext *sr_context = context->priv;
    return ff_dnn_init(&sr_context->dnnctx, DFT_PROCESS_FRAME, context);
}

static const enum AVPixelFormat pixel_formats[] = {
    AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_YUV410P, AV_PIX_FMT_YUV411P, AV_PIX_FMT_GRAY8,
    AV_PIX_FMT_NONE
};

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *context = outlink->src;
    SRContext *ctx = context->priv;
    int result;
    AVFilterLink *inlink = context->inputs[0];
    int out_width, out_height;

    // have a try run in case that the dnn model resize the frame
    result = ff_dnn_get_output(&ctx->dnnctx, inlink->w, inlink->h, &out_width, &out_height);
    if (result != 0) {
        av_log(ctx, AV_LOG_ERROR, "could not get output from the model\n");
        return result;
    }

    if (inlink->w != out_width || inlink->h != out_height) {
        //espcn
        outlink->w = out_width;
        outlink->h = out_height;
        if (inlink->format != AV_PIX_FMT_GRAY8){
            const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
            int sws_src_h = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
            int sws_src_w = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
            int sws_dst_h = AV_CEIL_RSHIFT(outlink->h, desc->log2_chroma_h);
            int sws_dst_w = AV_CEIL_RSHIFT(outlink->w, desc->log2_chroma_w);
            ctx->sws_uv_scale = sws_getContext(sws_src_w, sws_src_h, AV_PIX_FMT_GRAY8,
                                               sws_dst_w, sws_dst_h, AV_PIX_FMT_GRAY8,
                                               SWS_BICUBIC, NULL, NULL, NULL);
            ctx->sws_uv_height = sws_src_h;
        }
    } else {
        //srcnn
        outlink->w = out_width * ctx->scale_factor;
        outlink->h = out_height * ctx->scale_factor;
        ctx->sws_pre_scale = sws_getContext(inlink->w, inlink->h, inlink->format,
                                        outlink->w, outlink->h, outlink->format,
                                        SWS_BICUBIC, NULL, NULL, NULL);
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    DNNAsyncStatusType async_state = 0;
    AVFilterContext *context = inlink->dst;
    SRContext *ctx = context->priv;
    AVFilterLink *outlink = context->outputs[0];
    AVFrame *out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    int dnn_result;

    if (!out){
        av_log(context, AV_LOG_ERROR, "could not allocate memory for output frame\n");
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

    if (ctx->sws_pre_scale) {
        sws_scale(ctx->sws_pre_scale,
                    (const uint8_t **)in->data, in->linesize, 0, in->height,
                    out->data, out->linesize);
        dnn_result = ff_dnn_execute_model(&ctx->dnnctx, out, out);
    } else {
        dnn_result = ff_dnn_execute_model(&ctx->dnnctx, in, out);
    }

    if (dnn_result != 0){
        av_log(ctx, AV_LOG_ERROR, "failed to execute loaded model\n");
        av_frame_free(&in);
        av_frame_free(&out);
        return dnn_result;
    }

    do {
        async_state = ff_dnn_get_result(&ctx->dnnctx, &in, &out);
    } while (async_state == DAST_NOT_READY);

    if (async_state != DAST_SUCCESS)
        return AVERROR(EINVAL);

    if (ctx->sws_uv_scale) {
        sws_scale(ctx->sws_uv_scale, (const uint8_t **)(in->data + 1), in->linesize + 1,
                  0, ctx->sws_uv_height, out->data + 1, out->linesize + 1);
        sws_scale(ctx->sws_uv_scale, (const uint8_t **)(in->data + 2), in->linesize + 2,
                  0, ctx->sws_uv_height, out->data + 2, out->linesize + 2);
    }
    if (in != out) {
        av_frame_free(&in);
    }
    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *context)
{
    SRContext *sr_context = context->priv;

    ff_dnn_uninit(&sr_context->dnnctx);
    sws_freeContext(sr_context->sws_uv_scale);
    sws_freeContext(sr_context->sws_pre_scale);
}

static const AVFilterPad sr_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
};

static const AVFilterPad sr_outputs[] = {
    {
        .name = "default",
        .config_props = config_output,
        .type = AVMEDIA_TYPE_VIDEO,
    },
};

const AVFilter ff_vf_sr = {
    .name          = "sr",
    .description   = NULL_IF_CONFIG_SMALL("Apply DNN-based image super resolution to the input."),
    .priv_size     = sizeof(SRContext),
    .preinit       = ff_dnn_filter_init_child_class,
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(sr_inputs),
    FILTER_OUTPUTS(sr_outputs),
    FILTER_PIXFMTS_ARRAY(pixel_formats),
    .priv_class    = &sr_class,
};
