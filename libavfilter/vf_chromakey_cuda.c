/*
 * Copyright (c) 2022 Mohamed Khaled <Mohamed_Khaled_Kamal@outlook.com>
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

#include <float.h>
#include <stdio.h>
#include <string.h>

#include "libavutil/avstring.h"
#include "libavutil/common.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_cuda_internal.h"
#include "libavutil/cuda_check.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"
#include "cuda/load_helper.h"

static const enum AVPixelFormat supported_formats[] = {
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_NV12,
    AV_PIX_FMT_YUVA420P
};

#define DIV_UP(a, b) (((a) + (b)-1) / (b))
#define BLOCKX 32
#define BLOCKY 16
#define CHECK_CU(x) FF_CUDA_CHECK_DL(ctx, s->hwctx->internal->cuda_dl, x)

typedef struct ChromakeyCUDAContext {
    const AVClass *class;

    AVCUDADeviceContext *hwctx;

    enum AVPixelFormat in_fmt, out_fmt;
    const AVPixFmtDescriptor *in_desc, *out_desc;
    int in_planes, out_planes;
    int in_plane_depths[4];
    int in_plane_channels[4];

    uint8_t chromakey_rgba[4];
    uint16_t chromakey_uv[2];
    int is_yuv;
    float similarity;
    float blend;

    AVBufferRef *frames_ctx;
    AVFrame *frame;
    AVFrame *tmp_frame;

    CUcontext cu_ctx;
    CUmodule cu_module;
    CUfunction cu_func;
    CUfunction cu_func_uv;
    CUstream cu_stream;
} ChromakeyCUDAContext;

static av_cold int cudachromakey_init(AVFilterContext *ctx)
{
    ChromakeyCUDAContext *s = ctx->priv;

    s->frame = av_frame_alloc();
    if (!s->frame)
        return AVERROR(ENOMEM);

    s->tmp_frame = av_frame_alloc();
    if (!s->tmp_frame)
        return AVERROR(ENOMEM);

    return 0;
}

static av_cold void cudachromakey_uninit(AVFilterContext *ctx)
{
    ChromakeyCUDAContext *s = ctx->priv;

    if (s->hwctx && s->cu_module)
    {
        CudaFunctions *cu = s->hwctx->internal->cuda_dl;
        CUcontext context;

        CHECK_CU(cu->cuCtxPushCurrent(s->hwctx->cuda_ctx));
        CHECK_CU(cu->cuModuleUnload(s->cu_module));
        s->cu_module = NULL;
        CHECK_CU(cu->cuCtxPopCurrent(&context));
    }

    av_frame_free(&s->frame);
    av_buffer_unref(&s->frames_ctx);
    av_frame_free(&s->tmp_frame);
}

static av_cold int init_hwframe_ctx(ChromakeyCUDAContext *s, AVBufferRef *device_ctx, int width, int height)
{
    AVBufferRef *out_ref = NULL;
    AVHWFramesContext *out_ctx;
    int ret;

    out_ref = av_hwframe_ctx_alloc(device_ctx);
    if (!out_ref)
        return AVERROR(ENOMEM);
    out_ctx = (AVHWFramesContext *)out_ref->data;

    out_ctx->format     = AV_PIX_FMT_CUDA;
    out_ctx->sw_format  = s->out_fmt;
    out_ctx->width      = width;
    out_ctx->height     = height;

    ret = av_hwframe_ctx_init(out_ref);
    if (ret < 0)
        goto fail;

    av_frame_unref(s->frame);
    ret = av_hwframe_get_buffer(out_ref, s->frame, 0);
    if (ret < 0)
        goto fail;

    av_buffer_unref(&s->frames_ctx);
    s->frames_ctx = out_ref;

    return 0;
fail:
    av_buffer_unref(&out_ref);
    return ret;
}

static int format_is_supported(enum AVPixelFormat fmt)
{
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(supported_formats); i++)
        if (supported_formats[i] == fmt)
            return 1;
    return 0;
}

static av_cold void set_format_info(AVFilterContext *ctx, enum AVPixelFormat in_format, enum AVPixelFormat out_format)
{
    ChromakeyCUDAContext *s = ctx->priv;
    int i, p, d;

    s->in_fmt       = in_format;
    s->out_fmt      = out_format;

    s->in_desc      = av_pix_fmt_desc_get(s->in_fmt);
    s->out_desc     = av_pix_fmt_desc_get(s->out_fmt);
    s->in_planes    = av_pix_fmt_count_planes(s->in_fmt);
    s->out_planes   = av_pix_fmt_count_planes(s->out_fmt);

    // find maximum step of each component of each plane
    // For our subset of formats, this should accurately tell us how many channels CUDA needs
    // i.e. 1 for Y plane, 2 for UV plane of NV12, 4 for single plane of RGB0 formats

    for (i = 0; i < s->in_desc->nb_components; i++)
    {
        d = (s->in_desc->comp[i].depth + 7) / 8;
        p = s->in_desc->comp[i].plane;
        s->in_plane_channels[p] = FFMAX(s->in_plane_channels[p], s->in_desc->comp[i].step / d);

        s->in_plane_depths[p]   = s->in_desc->comp[i].depth;
    }
}

static av_cold int init_processing_chain(AVFilterContext *ctx, int width, int height)
{
    ChromakeyCUDAContext *s = ctx->priv;
    AVHWFramesContext *in_frames_ctx;
    int ret;

    /* check that we have a hw context */
    if (!ctx->inputs[0]->hw_frames_ctx)
    {
        av_log(ctx, AV_LOG_ERROR, "No hw context provided on input\n");
        return AVERROR(EINVAL);
    }
    in_frames_ctx = (AVHWFramesContext *)ctx->inputs[0]->hw_frames_ctx->data;

    if (!format_is_supported(in_frames_ctx->sw_format))
    {
        av_log(ctx, AV_LOG_ERROR, "Unsupported format: %s\n", av_get_pix_fmt_name(in_frames_ctx->sw_format));
        return AVERROR(ENOSYS);
    }

    set_format_info(ctx, in_frames_ctx->sw_format, AV_PIX_FMT_YUVA420P);

    ret = init_hwframe_ctx(s, in_frames_ctx->device_ref, width, height);
    if (ret < 0)
        return ret;

    ctx->outputs[0]->hw_frames_ctx = av_buffer_ref(s->frames_ctx);
    if (!ctx->outputs[0]->hw_frames_ctx)
        return AVERROR(ENOMEM);

    return 0;
}

static av_cold int cudachromakey_load_functions(AVFilterContext *ctx)
{
    ChromakeyCUDAContext *s   = ctx->priv;
    CUcontext context, cuda_ctx = s->hwctx->cuda_ctx;
    CudaFunctions *cu = s->hwctx->internal->cuda_dl;
    int ret;

    extern const unsigned char ff_vf_chromakey_cuda_ptx_data[];
    extern const unsigned int ff_vf_chromakey_cuda_ptx_len;

    ret = CHECK_CU(cu->cuCtxPushCurrent(cuda_ctx));
    if (ret < 0)
        return ret;

    ret = ff_cuda_load_module(ctx, s->hwctx, &s->cu_module,
                              ff_vf_chromakey_cuda_ptx_data, ff_vf_chromakey_cuda_ptx_len);
    if (ret < 0)
        goto fail;

    ret = CHECK_CU(cu->cuModuleGetFunction(&s->cu_func, s->cu_module, "Process_uchar"));
    if (ret < 0)
    {
        av_log(ctx, AV_LOG_FATAL, "Failed loading Process_uchar\n");
        goto fail;
    }

    ret = CHECK_CU(cu->cuModuleGetFunction(&s->cu_func_uv, s->cu_module, "Process_uchar2"));
    if (ret < 0)
    {
        av_log(ctx, AV_LOG_FATAL, "Failed loading Process_uchar2\n");
        goto fail;
    }

fail:
    CHECK_CU(cu->cuCtxPopCurrent(&context));

    return ret;
}

#define FIXNUM(x) lrint((x) * (1 << 10))
#define RGB_TO_U(rgb) (((-FIXNUM(0.16874) * rgb[0] - FIXNUM(0.33126) * rgb[1] + FIXNUM(0.50000) * rgb[2] + (1 << 9) - 1) >> 10) + 128)
#define RGB_TO_V(rgb) (((FIXNUM(0.50000) * rgb[0] - FIXNUM(0.41869) * rgb[1] - FIXNUM(0.08131) * rgb[2] + (1 << 9) - 1) >> 10) + 128)

static av_cold int cudachromakey_config_props(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = outlink->src->inputs[0];
    ChromakeyCUDAContext *s = ctx->priv;
    AVHWFramesContext *frames_ctx = (AVHWFramesContext *)inlink->hw_frames_ctx->data;
    AVCUDADeviceContext *device_hwctx = frames_ctx->device_ctx->hwctx;
    int ret;

    s->hwctx = device_hwctx;
    s->cu_stream = s->hwctx->stream;

    if (s->is_yuv) {
        s->chromakey_uv[0] = s->chromakey_rgba[1];
        s->chromakey_uv[1] = s->chromakey_rgba[2];
    } else {
        s->chromakey_uv[0] = RGB_TO_U(s->chromakey_rgba);
        s->chromakey_uv[1] = RGB_TO_V(s->chromakey_rgba);
    }

    ret = init_processing_chain(ctx, inlink->w, inlink->h);
    if (ret < 0)
        return ret;

    outlink->sample_aspect_ratio = inlink->sample_aspect_ratio;

    ret = cudachromakey_load_functions(ctx);
    if (ret < 0)
        return ret;

    return 0;
}

static int call_cuda_kernel(AVFilterContext *ctx, CUfunction func,
                            CUtexObject src_tex[3], AVFrame *out_frame,
                            int width, int height, int pitch,
                            int width_uv, int height_uv, int pitch_uv,
                            float u_key, float v_key, float similarity,
                            float blend)
{
    ChromakeyCUDAContext *s = ctx->priv;
    CudaFunctions *cu = s->hwctx->internal->cuda_dl;

    CUdeviceptr dst_devptr[4] = {
        (CUdeviceptr)out_frame->data[0], (CUdeviceptr)out_frame->data[1],
        (CUdeviceptr)out_frame->data[2], (CUdeviceptr)out_frame->data[3]
    };

    void *args_uchar[] = {
        &src_tex[0], &src_tex[1], &src_tex[2],
        &dst_devptr[0], &dst_devptr[1], &dst_devptr[2], &dst_devptr[3],
        &width, &height, &pitch,
        &width_uv, &height_uv, &pitch_uv,
        &u_key, &v_key, &similarity, &blend
    };

    return CHECK_CU(cu->cuLaunchKernel(func,
                                       DIV_UP(width, BLOCKX), DIV_UP(height, BLOCKY), 1,
                                       BLOCKX, BLOCKY, 1, 0, s->cu_stream, args_uchar, NULL));
}

static int cudachromakey_process_internal(AVFilterContext *ctx,
                                          AVFrame *out, AVFrame *in)
{
    ChromakeyCUDAContext *s = ctx->priv;
    CudaFunctions *cu       = s->hwctx->internal->cuda_dl;
    CUcontext context, cuda_ctx = s->hwctx->cuda_ctx;
    int i, ret;

    CUtexObject tex[3] = {0, 0, 0};

    ret = CHECK_CU(cu->cuCtxPushCurrent(cuda_ctx));
    if (ret < 0)
        return ret;

    for (i = 0; i < s->in_planes; i++)
    {
        CUDA_TEXTURE_DESC tex_desc = {
            .filterMode = CU_TR_FILTER_MODE_LINEAR,
            .flags = 0, // CU_TRSF_READ_AS_INTEGER to get raw ints instead of normalized floats from tex2D
        };

        CUDA_RESOURCE_DESC res_desc = {
            .resType = CU_RESOURCE_TYPE_PITCH2D,
            .res.pitch2D.format   = CU_AD_FORMAT_UNSIGNED_INT8,
            .res.pitch2D.numChannels = s->in_plane_channels[i],
            .res.pitch2D.pitchInBytes = in->linesize[i],
            .res.pitch2D.devPtr = (CUdeviceptr)in->data[i],
        };

        if (i == 1 || i == 2)
        {
            res_desc.res.pitch2D.width  = AV_CEIL_RSHIFT(in->width, s->in_desc->log2_chroma_w);
            res_desc.res.pitch2D.height = AV_CEIL_RSHIFT(in->height, s->in_desc->log2_chroma_h);
        }
        else
        {
            res_desc.res.pitch2D.width  = in->width;
            res_desc.res.pitch2D.height = in->height;
        }

        ret = CHECK_CU(cu->cuTexObjectCreate(&tex[i], &res_desc, &tex_desc, NULL));
        if (ret < 0)
            goto exit;
    }

    ret = call_cuda_kernel(ctx, (s->in_plane_channels[1] > 1) ? s->cu_func_uv : s->cu_func,
                           tex, out,
                           out->width, out->height, out->linesize[0],
                           AV_CEIL_RSHIFT(out->width, s->out_desc->log2_chroma_w),
                           AV_CEIL_RSHIFT(out->height, s->out_desc->log2_chroma_h),
                           out->linesize[1],
                           s->chromakey_uv[0], s->chromakey_uv[1],
                           s->similarity, s->blend);
    if (ret < 0)
        goto exit;

exit:
    for (i = 0; i < s->in_planes; i++)
        if (tex[i])
            CHECK_CU(cu->cuTexObjectDestroy(tex[i]));

    CHECK_CU(cu->cuCtxPopCurrent(&context));

    return ret;
}

static int cudachromakey_process(AVFilterContext *ctx, AVFrame *out, AVFrame *in)
{
    ChromakeyCUDAContext *s = ctx->priv;
    AVFrame *src = in;
    int ret;

    ret = cudachromakey_process_internal(ctx, s->frame, src);
    if (ret < 0)
        return ret;

    src = s->frame;
    ret = av_hwframe_get_buffer(src->hw_frames_ctx, s->tmp_frame, 0);
    if (ret < 0)
        return ret;

    av_frame_move_ref(out, s->frame);
    av_frame_move_ref(s->frame, s->tmp_frame);

    ret = av_frame_copy_props(out, in);
    if (ret < 0)
        return ret;

    return 0;
}

static int cudachromakey_filter_frame(AVFilterLink *link, AVFrame *in)
{
    AVFilterContext *ctx    = link->dst;
    ChromakeyCUDAContext *s = ctx->priv;
    AVFilterLink *outlink   = ctx->outputs[0];
    CudaFunctions *cu = s->hwctx->internal->cuda_dl;

    AVFrame *out   = NULL;
    CUcontext context;
    int ret = 0;

    out = av_frame_alloc();
    if (!out)
    {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    ret = CHECK_CU(cu->cuCtxPushCurrent(s->hwctx->cuda_ctx));
    if (ret < 0)
        goto fail;

    ret = cudachromakey_process(ctx, out, in);

    CHECK_CU(cu->cuCtxPopCurrent(&context));
    if (ret < 0)
        goto fail;

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
fail:
    av_frame_free(&in);
    av_frame_free(&out);
    return ret;
}

#define OFFSET(x) offsetof(ChromakeyCUDAContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)
static const AVOption options[] = {
    {"color", "set the chromakey key color", OFFSET(chromakey_rgba), AV_OPT_TYPE_COLOR, {.str = "black"}, 0, 0, FLAGS},
    {"similarity", "set the chromakey similarity value", OFFSET(similarity), AV_OPT_TYPE_FLOAT, {.dbl = 0.01}, 0.01, 1.0, FLAGS},
    {"blend", "set the chromakey key blend value", OFFSET(blend), AV_OPT_TYPE_FLOAT, {.dbl = 0.0}, 0.0, 1.0, FLAGS},
    {"yuv", "color parameter is in yuv instead of rgb", OFFSET(is_yuv), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, FLAGS},
    {NULL},
};

static const AVClass cudachromakey_class = {
    .class_name = "cudachromakey",
    .item_name = av_default_item_name,
    .option = options,
    .version = LIBAVUTIL_VERSION_INT,
};

static const AVFilterPad cudachromakey_inputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .filter_frame = cudachromakey_filter_frame,
    },
};

static const AVFilterPad cudachromakey_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = cudachromakey_config_props,
    },
};

const AVFilter ff_vf_chromakey_cuda = {
    .name = "chromakey_cuda",
    .description = NULL_IF_CONFIG_SMALL("GPU accelerated chromakey filter"),

    .init = cudachromakey_init,
    .uninit = cudachromakey_uninit,

    .priv_size = sizeof(ChromakeyCUDAContext),
    .priv_class = &cudachromakey_class,

    FILTER_INPUTS(cudachromakey_inputs),
    FILTER_OUTPUTS(cudachromakey_outputs),

    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_CUDA),

    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
