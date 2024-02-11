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
 * NPP sharpen video filter
 */

#include <nppi.h>
#include <nppi_filtering_functions.h>

#include "internal.h"
#include "libavutil/pixdesc.h"
#include "libavutil/cuda_check.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_cuda_internal.h"
#include "libavutil/opt.h"

#define CHECK_CU(x) FF_CUDA_CHECK_DL(ctx, device_hwctx->internal->cuda_dl, x)

static const enum AVPixelFormat supported_formats[] = {
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUV444P,
};

typedef struct NPPSharpenContext {
    const AVClass* class;

    AVBufferRef* frames_ctx;
    AVFrame* own_frame;
    AVFrame* tmp_frame;

    NppiBorderType border_type;
} NPPSharpenContext;

static int nppsharpen_init(AVFilterContext* ctx)
{
    NPPSharpenContext* s = ctx->priv;

    s->own_frame = av_frame_alloc();
    if (!s->own_frame)
        goto fail;

    s->tmp_frame = av_frame_alloc();
    if (!s->tmp_frame)
        goto fail;

    return 0;

fail:
    av_frame_free(&s->own_frame);
    av_frame_free(&s->tmp_frame);
    return AVERROR(ENOMEM);
}

static int nppsharpen_config(AVFilterContext* ctx, int width, int height)
{
    NPPSharpenContext* s = ctx->priv;
    AVHWFramesContext *out_ctx, *in_ctx;
    int i, ret, supported_format = 0;

    if (!ctx->inputs[0]->hw_frames_ctx) {
        av_log(ctx, AV_LOG_ERROR, "No hw context provided on input\n");
        goto fail;
    }

    in_ctx = (AVHWFramesContext*)ctx->inputs[0]->hw_frames_ctx->data;

    s->frames_ctx = av_hwframe_ctx_alloc(in_ctx->device_ref);
    if (!s->frames_ctx)
        goto fail;

    out_ctx = (AVHWFramesContext*)s->frames_ctx->data;
    out_ctx->format = AV_PIX_FMT_CUDA;
    out_ctx->sw_format = in_ctx->sw_format;
    out_ctx->width = FFALIGN(width, 32);
    out_ctx->height = FFALIGN(height, 32);

    for (i = 0; i < FF_ARRAY_ELEMS(supported_formats); i++) {
        if (in_ctx->sw_format == supported_formats[i]) {
            supported_format = 1;
            break;
        }
    }

    if (!supported_format) {
        av_log(ctx, AV_LOG_ERROR, "Unsupported pixel format\n");
        goto fail;
    }

    ret = av_hwframe_ctx_init(s->frames_ctx);
    if (ret < 0)
        goto fail;

    ret = av_hwframe_get_buffer(s->frames_ctx, s->own_frame, 0);
    if (ret < 0)
        goto fail;

    ctx->outputs[0]->hw_frames_ctx = av_buffer_ref(s->frames_ctx);
    if (!ctx->outputs[0]->hw_frames_ctx)
        goto fail;

    return 0;

fail:
    av_buffer_unref(&s->frames_ctx);
    return AVERROR(ENOMEM);
}

static void nppsharpen_uninit(AVFilterContext* ctx)
{
    NPPSharpenContext* s = ctx->priv;

    av_buffer_unref(&s->frames_ctx);
    av_frame_free(&s->own_frame);
    av_frame_free(&s->tmp_frame);
}

static int nppsharpen_config_props(AVFilterLink* outlink)
{
    AVFilterLink* inlink = outlink->src->inputs[0];

    outlink->w = inlink->w;
    outlink->h = inlink->h;

    if (inlink->sample_aspect_ratio.num)
        outlink->sample_aspect_ratio = av_mul_q(
            (AVRational){outlink->h * inlink->w, outlink->w * inlink->h},
            inlink->sample_aspect_ratio);
    else
        outlink->sample_aspect_ratio = inlink->sample_aspect_ratio;

    nppsharpen_config(outlink->src, inlink->w, inlink->h);

    return 0;
}

static int nppsharpen_sharpen(AVFilterContext* ctx, AVFrame* out, AVFrame* in)
{
    AVHWFramesContext* in_ctx =
        (AVHWFramesContext*)ctx->inputs[0]->hw_frames_ctx->data;
    NPPSharpenContext* s = ctx->priv;

    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(in_ctx->sw_format);

    for (int i = 0; i < FF_ARRAY_ELEMS(in->data) && in->data[i]; i++) {
        int ow = AV_CEIL_RSHIFT(in->width, (i == 1 || i == 2) ? desc->log2_chroma_w : 0);
        int oh = AV_CEIL_RSHIFT(in->height, (i == 1 || i == 2) ? desc->log2_chroma_h : 0);

        NppStatus err = nppiFilterSharpenBorder_8u_C1R(
            in->data[i], in->linesize[i], (NppiSize){ow, oh}, (NppiPoint){0, 0},
            out->data[i], out->linesize[i], (NppiSize){ow, oh}, s->border_type);
        if (err != NPP_SUCCESS) {
            av_log(ctx, AV_LOG_ERROR, "NPP sharpen error: %d\n", err);
            return AVERROR_EXTERNAL;
        }
    }

    return 0;
}

static int nppsharpen_filter_frame(AVFilterLink* link, AVFrame* in)
{
    AVFilterContext* ctx = link->dst;
    NPPSharpenContext* s = ctx->priv;
    AVFilterLink* outlink = ctx->outputs[0];
    AVHWFramesContext* frames_ctx =
        (AVHWFramesContext*)outlink->hw_frames_ctx->data;
    AVCUDADeviceContext* device_hwctx = frames_ctx->device_ctx->hwctx;

    AVFrame* out = NULL;
    CUcontext dummy;
    int ret = 0;

    out = av_frame_alloc();
    if (!out) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    ret = CHECK_CU(device_hwctx->internal->cuda_dl->cuCtxPushCurrent(
        device_hwctx->cuda_ctx));
    if (ret < 0)
        goto fail;

    ret = nppsharpen_sharpen(ctx, s->own_frame, in);
    if (ret < 0)
        goto pop_ctx;

    ret = av_hwframe_get_buffer(s->own_frame->hw_frames_ctx, s->tmp_frame, 0);
    if (ret < 0)
        goto pop_ctx;

    av_frame_move_ref(out, s->own_frame);
    av_frame_move_ref(s->own_frame, s->tmp_frame);

    ret = av_frame_copy_props(out, in);
    if (ret < 0)
        goto pop_ctx;

    av_frame_free(&in);

pop_ctx:
    CHECK_CU(device_hwctx->internal->cuda_dl->cuCtxPopCurrent(&dummy));
    if (!ret)
        return ff_filter_frame(outlink, out);
fail:
    av_frame_free(&in);
    av_frame_free(&out);
    return ret;
}

#define OFFSET(x) offsetof(NPPSharpenContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)
static const AVOption options[] = {
    { "border_type", "Type of operation to be performed on image border", OFFSET(border_type), AV_OPT_TYPE_INT, { .i64 = NPP_BORDER_REPLICATE }, NPP_BORDER_REPLICATE, NPP_BORDER_REPLICATE, FLAGS, .unit = "border_type" },
    { "replicate", "replicate pixels", 0, AV_OPT_TYPE_CONST, { .i64 = NPP_BORDER_REPLICATE }, 0, 0, FLAGS, .unit = "border_type" },
    {NULL},
};

static const AVClass nppsharpen_class = {
    .class_name = "nppsharpen",
    .item_name = av_default_item_name,
    .option = options,
    .version = LIBAVUTIL_VERSION_INT,
};

static const AVFilterPad nppsharpen_inputs[] = {{
    .name = "default",
    .type = AVMEDIA_TYPE_VIDEO,
    .filter_frame = nppsharpen_filter_frame,
}};

static const AVFilterPad nppsharpen_outputs[] = {{
    .name = "default",
    .type = AVMEDIA_TYPE_VIDEO,
    .config_props = nppsharpen_config_props,
}};

const AVFilter ff_vf_sharpen_npp = {
    .name = "sharpen_npp",
    .description = NULL_IF_CONFIG_SMALL("NVIDIA Performance Primitives video "
                                        "sharpening filter."),

    .init = nppsharpen_init,
    .uninit = nppsharpen_uninit,

    .priv_size = sizeof(NPPSharpenContext),
    .priv_class = &nppsharpen_class,

    FILTER_INPUTS(nppsharpen_inputs),
    FILTER_OUTPUTS(nppsharpen_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_CUDA),

    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
