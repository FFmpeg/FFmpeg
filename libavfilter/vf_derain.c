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
#include "dnn_filter_common.h"
#include "formats.h"
#include "internal.h"

typedef struct DRContext {
    const AVClass *class;
    DnnContext dnnctx;
    int                filter_type;
} DRContext;

#define OFFSET(x) offsetof(DRContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM
static const AVOption derain_options[] = {
    { "filter_type", "filter type(derain/dehaze)",  OFFSET(filter_type),    AV_OPT_TYPE_INT,    { .i64 = 0 },    0, 1, FLAGS, "type" },
    { "derain",      "derain filter flag",          0,                      AV_OPT_TYPE_CONST,  { .i64 = 0 },    0, 0, FLAGS, "type" },
    { "dehaze",      "dehaze filter flag",          0,                      AV_OPT_TYPE_CONST,  { .i64 = 1 },    0, 0, FLAGS, "type" },
    { "dnn_backend", "DNN backend",                 OFFSET(dnnctx.backend_type),   AV_OPT_TYPE_INT,    { .i64 = 0 },    0, 1, FLAGS, "backend" },
    { "native",      "native backend flag",         0,                      AV_OPT_TYPE_CONST,  { .i64 = 0 },    0, 0, FLAGS, "backend" },
#if (CONFIG_LIBTENSORFLOW == 1)
    { "tensorflow",  "tensorflow backend flag",     0,                      AV_OPT_TYPE_CONST,  { .i64 = 1 },    0, 0, FLAGS, "backend" },
#endif
    { "model",       "path to model file",          OFFSET(dnnctx.model_filename),   AV_OPT_TYPE_STRING,    { .str = NULL }, 0, 0, FLAGS },
    { "input",       "input name of the model",     OFFSET(dnnctx.model_inputname),  AV_OPT_TYPE_STRING,    { .str = "x" },  0, 0, FLAGS },
    { "output",      "output name of the model",    OFFSET(dnnctx.model_outputnames_string), AV_OPT_TYPE_STRING,    { .str = "y" },  0, 0, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(derain);

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    DNNAsyncStatusType async_state = 0;
    AVFilterContext *ctx  = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    DRContext *dr_context = ctx->priv;
    DNNReturnType dnn_result;
    AVFrame *out;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_log(ctx, AV_LOG_ERROR, "could not allocate memory for output frame\n");
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

    dnn_result = ff_dnn_execute_model(&dr_context->dnnctx, in, out);
    if (dnn_result != DNN_SUCCESS){
        av_log(ctx, AV_LOG_ERROR, "failed to execute model\n");
        av_frame_free(&in);
        return AVERROR(EIO);
    }
    do {
        async_state = ff_dnn_get_result(&dr_context->dnnctx, &in, &out);
    } while (async_state == DAST_NOT_READY);

    if (async_state != DAST_SUCCESS)
        return AVERROR(EINVAL);

    av_frame_free(&in);

    return ff_filter_frame(outlink, out);
}

static av_cold int init(AVFilterContext *ctx)
{
    DRContext *dr_context = ctx->priv;
    return ff_dnn_init(&dr_context->dnnctx, DFT_PROCESS_FRAME, ctx);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    DRContext *dr_context = ctx->priv;
    ff_dnn_uninit(&dr_context->dnnctx);
}

static const AVFilterPad derain_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
};

static const AVFilterPad derain_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
};

const AVFilter ff_vf_derain = {
    .name          = "derain",
    .description   = NULL_IF_CONFIG_SMALL("Apply derain filter to the input."),
    .priv_size     = sizeof(DRContext),
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(derain_inputs),
    FILTER_OUTPUTS(derain_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_RGB24),
    .priv_class    = &derain_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
