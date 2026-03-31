/*
 * Copyright (C) 2026 NyanMisaka
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

#include "libavutil/avassert.h"
#include "libavutil/common.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_cuda_internal.h"
#include "libavutil/cuda_check.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "avfilter.h"
#include "filters.h"
#include "transpose.h"
#include "video.h"

#include "cuda/load_helper.h"

#define DIV_UP(a, b) ( ((a) + (b) - 1) / (b) )
#define BLOCK_X 32
#define BLOCK_Y 16

#define CHECK_CU(x) FF_CUDA_CHECK_DL(ctx, s->hwctx->internal->cuda_dl, x)

static const enum AVPixelFormat supported_formats[] = {
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUV422P,
    AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_YUV420P10,
    AV_PIX_FMT_YUV422P10,
    AV_PIX_FMT_YUV444P10,
    AV_PIX_FMT_YUV444P16,
    AV_PIX_FMT_NV12,
    AV_PIX_FMT_NV16,
    AV_PIX_FMT_P010,
    AV_PIX_FMT_P210,
    AV_PIX_FMT_P016,
    AV_PIX_FMT_P216,
    AV_PIX_FMT_0RGB32,
    AV_PIX_FMT_0BGR32,
    AV_PIX_FMT_RGB32,
    AV_PIX_FMT_BGR32,
};

typedef struct TransposeCUDAContext {
    const AVClass *class;

    AVCUDADeviceContext *hwctx;
    AVBufferRef         *frames_ctx;
    AVFrame             *frame;
    AVFrame             *tmp_frame;

    const AVPixFmtDescriptor *pix_desc;

    CUcontext  cu_ctx;
    CUmodule   cu_module;
    CUfunction cu_func_uchar;
    CUfunction cu_func_ushort;
    CUfunction cu_func_uchar2;
    CUfunction cu_func_ushort2;
    CUfunction cu_func_uchar4;
    CUstream   cu_stream;

    int flip_wh;
    int passthrough;    ///< PassthroughType, landscape passthrough mode enabled
    int dir;            ///< TransposeDir
} TransposeCUDAContext;

static av_cold int cudatranspose_init(AVFilterContext *ctx)
{
    TransposeCUDAContext *s = ctx->priv;

    s->frame = av_frame_alloc();
    if (!s->frame)
        return AVERROR(ENOMEM);

    s->tmp_frame = av_frame_alloc();
    if (!s->tmp_frame)
        return AVERROR(ENOMEM);

    return 0;
}

static av_cold void cudatranspose_uninit(AVFilterContext *ctx)
{
    TransposeCUDAContext *s = ctx->priv;

    if (s->hwctx && s->cu_module) {
        CUcontext dummy;
        CudaFunctions *cu = s->hwctx->internal->cuda_dl;
        CHECK_CU(cu->cuCtxPushCurrent(s->cu_ctx));
        CHECK_CU(cu->cuModuleUnload(s->cu_module));
        CHECK_CU(cu->cuCtxPopCurrent(&dummy));
    }

    av_frame_free(&s->frame);
    av_buffer_unref(&s->frames_ctx);
    av_frame_free(&s->tmp_frame);
}

static av_cold int init_hwframe_ctx(TransposeCUDAContext *s,
                                    AVBufferRef *device_ctx,
                                    int width, int height,
                                    enum AVPixelFormat sw_format)
{
    AVBufferRef *out_ref = NULL;
    AVHWFramesContext *out_ctx;
    int ret;

    out_ref = av_hwframe_ctx_alloc(device_ctx);
    if (!out_ref)
        return AVERROR(ENOMEM);
    out_ctx = (AVHWFramesContext*)out_ref->data;

    out_ctx->format    = AV_PIX_FMT_CUDA;
    out_ctx->sw_format = sw_format;
    out_ctx->width     = FFALIGN(width,  32);
    out_ctx->height    = FFALIGN(height, 32);

    ret = av_hwframe_ctx_init(out_ref);
    if (ret < 0)
        goto fail;

    av_frame_unref(s->frame);
    ret = av_hwframe_get_buffer(out_ref, s->frame, 0);
    if (ret < 0)
        goto fail;

    s->frame->width  = width;
    s->frame->height = height;

    av_buffer_unref(&s->frames_ctx);
    s->frames_ctx = out_ref;

    return 0;
fail:
    av_buffer_unref(&out_ref);
    return ret;
}

static int format_is_supported(enum AVPixelFormat fmt)
{
    for (int i = 0; i < FF_ARRAY_ELEMS(supported_formats); i++)
        if (supported_formats[i] == fmt)
            return 1;
    return 0;
}

static int init_processing_chain(AVFilterContext *ctx,
                                 int out_width, int out_height)
{
    FilterLink *inl  = ff_filter_link(ctx->inputs[0]);
    FilterLink *outl = ff_filter_link(ctx->outputs[0]);
    TransposeCUDAContext *s = ctx->priv;
    AVHWFramesContext *in_frames_ctx;
    enum AVPixelFormat format;
    int ret;

    /* check that we have a hw context */
    if (!inl->hw_frames_ctx) {
        av_log(ctx, AV_LOG_ERROR, "No hw context provided on input\n");
        return AVERROR(EINVAL);
    }

    in_frames_ctx = (AVHWFramesContext*)inl->hw_frames_ctx->data;
    format        = in_frames_ctx->sw_format;
    s->pix_desc   = av_pix_fmt_desc_get(format);

    if (!format_is_supported(format)) {
        av_log(ctx, AV_LOG_ERROR, "Unsupported input format: %s\n",
               av_get_pix_fmt_name(format));
        return AVERROR(ENOSYS);
    }

    ret = init_hwframe_ctx(s, in_frames_ctx->device_ref,
                           out_width, out_height, format);
    if (ret < 0)
        return ret;

    s->hwctx = in_frames_ctx->device_ctx->hwctx;
    s->cu_stream = s->hwctx->stream;

    outl->hw_frames_ctx = av_buffer_ref(s->frames_ctx);
    if (!outl->hw_frames_ctx)
        return AVERROR(ENOMEM);

    return 0;
}

static int cudatranspose_config_props(AVFilterLink *outlink)
{
    extern const unsigned char ff_vf_transpose_cuda_ptx_data[];
    extern const unsigned int ff_vf_transpose_cuda_ptx_len;
    FilterLink     *outl = ff_filter_link(outlink);
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = ctx->inputs[0];
    FilterLink      *inl = ff_filter_link(inlink);
    TransposeCUDAContext *s = ctx->priv;
    CUcontext dummy, cuda_ctx;
    CudaFunctions *cu;
    int ret = 0;

    if ((inlink->w >= inlink->h && s->passthrough == TRANSPOSE_PT_TYPE_LANDSCAPE) ||
        (inlink->w <= inlink->h && s->passthrough == TRANSPOSE_PT_TYPE_PORTRAIT)) {
        if (inl->hw_frames_ctx) {
            outl->hw_frames_ctx = av_buffer_ref(inl->hw_frames_ctx);
            if (!outl->hw_frames_ctx)
                return AVERROR(ENOMEM);
        }

        av_log(ctx, AV_LOG_VERBOSE,
               "w:%d h:%d -> w:%d h:%d (passthrough mode)\n",
               inlink->w, inlink->h, inlink->w, inlink->h);
        return 0;
    } else {
        s->passthrough = TRANSPOSE_PT_TYPE_NONE;
    }

    switch (s->dir) {
    case TRANSPOSE_CCLOCK_FLIP:
    case TRANSPOSE_CCLOCK:
    case TRANSPOSE_CLOCK:
    case TRANSPOSE_CLOCK_FLIP:
        outlink->w = inlink->h;
        outlink->h = inlink->w;
        s->flip_wh = 1;
        break;
    default:
        outlink->w = inlink->w;
        outlink->h = inlink->h;
        s->flip_wh = 0;
        break;
    }

    if (s->flip_wh && inlink->sample_aspect_ratio.num)
        outlink->sample_aspect_ratio = av_inv_q(inlink->sample_aspect_ratio);
    else
        outlink->sample_aspect_ratio = inlink->sample_aspect_ratio;

    ret = init_processing_chain(ctx, outlink->w, outlink->h);
    if (ret < 0)
        return ret;

    cuda_ctx = s->cu_ctx = s->hwctx->cuda_ctx;
    cu = s->hwctx->internal->cuda_dl;

    ret = CHECK_CU(cu->cuCtxPushCurrent(cuda_ctx));
    if (ret < 0)
        return ret;

    ret = ff_cuda_load_module(ctx, s->hwctx, &s->cu_module,
                              ff_vf_transpose_cuda_ptx_data, ff_vf_transpose_cuda_ptx_len);
    if (ret < 0)
        goto exit;

    ret = CHECK_CU(cu->cuModuleGetFunction(&s->cu_func_uchar, s->cu_module, "Transpose_Cuda_uchar"));
    if (ret < 0)
        goto exit;

    ret = CHECK_CU(cu->cuModuleGetFunction(&s->cu_func_ushort, s->cu_module, "Transpose_Cuda_ushort"));
    if (ret < 0)
        goto exit;

    ret = CHECK_CU(cu->cuModuleGetFunction(&s->cu_func_uchar2, s->cu_module, "Transpose_Cuda_uchar2"));
    if (ret < 0)
        goto exit;

    ret = CHECK_CU(cu->cuModuleGetFunction(&s->cu_func_ushort2, s->cu_module, "Transpose_Cuda_ushort2"));
    if (ret < 0)
        goto exit;

    ret = CHECK_CU(cu->cuModuleGetFunction(&s->cu_func_uchar4, s->cu_module, "Transpose_Cuda_uchar4"));
    if (ret < 0)
        goto exit;

    av_log(ctx, AV_LOG_VERBOSE,
           "w:%d h:%d dir:%d -> w:%d h:%d\n",
           inlink->w, inlink->h, s->dir, outlink->w, outlink->h);
exit:
    CHECK_CU(cu->cuCtxPopCurrent(&dummy));

    return ret;
}

static CUresult call_kernel(AVFilterContext *ctx,
                            CUfunction cu_func,
                            CUarray_format cu_format,
                            int channels,
                            int is_422_uv,    // Dst* & Src* are 4:2:2 UV planes
                            CUdeviceptr dst0,
                            CUdeviceptr dst1, // Dst1 is for fully planar V, optional
                            int dst_width,    // Width is pixels per channel
                            int dst_height,   // Height is pixels per channel
                            int dst_pitch,    // Pitch is elements per channel
                            CUdeviceptr src0,
                            CUdeviceptr src1, // Src1 is for fully planar V, optional
                            int src_width,    // Width is pixels per channel
                            int src_height,   // Height is pixels per channel
                            int src_pitch)
{
    TransposeCUDAContext *s = ctx->priv;
    CudaFunctions *cu = s->hwctx->internal->cuda_dl;
    CUtexObject src0_tex = 0, src1_tex = 0;
    int ret;

    void *kernel_args[] = {
        &dst0, &dst1, &dst_width, &dst_height, &dst_pitch,
        &src0_tex, &src1_tex, &s->dir,
    };

    CUDA_TEXTURE_DESC tex_desc = {
        .addressMode = { CU_TR_ADDRESS_MODE_CLAMP,
                         CU_TR_ADDRESS_MODE_CLAMP },
        .filterMode  = is_422_uv ? CU_TR_FILTER_MODE_LINEAR
                                 : CU_TR_FILTER_MODE_POINT,
        .flags       = 2 /* CU_TRSF_NORMALIZED_COORDINATES */
    };
    CUDA_RESOURCE_DESC res_desc = {
        .resType                  = CU_RESOURCE_TYPE_PITCH2D,
        .res.pitch2D.format       = cu_format,
        .res.pitch2D.numChannels  = channels,
        .res.pitch2D.pitchInBytes = src_pitch,
        .res.pitch2D.width        = src_width,
        .res.pitch2D.height       = src_height
    };

    res_desc.res.pitch2D.devPtr = (CUdeviceptr)src0;
    ret = CHECK_CU(cu->cuTexObjectCreate(&src0_tex, &res_desc, &tex_desc, NULL));
    if (ret < 0)
        goto exit;

    if (src1) {
        res_desc.res.pitch2D.devPtr = (CUdeviceptr)src1;
        ret = CHECK_CU(cu->cuTexObjectCreate(&src1_tex, &res_desc, &tex_desc, NULL));
        if (ret < 0)
            goto exit;
    }

    ret = CHECK_CU(cu->cuLaunchKernel(cu_func,
                                      DIV_UP(dst_width, BLOCK_X), DIV_UP(dst_height, BLOCK_Y), 1,
                                      BLOCK_X, BLOCK_Y, 1, 0, s->cu_stream, kernel_args, NULL));
exit:
    if (src0_tex)
        CHECK_CU(cu->cuTexObjectDestroy(src0_tex));
    if (src1_tex)
        CHECK_CU(cu->cuTexObjectDestroy(src1_tex));

    return ret;
}

static int cudatranspose_rotate(AVFilterContext *ctx,
                                AVFrame *out, AVFrame *in)
{
    TransposeCUDAContext *s = ctx->priv;
    int ret;

    for (int c = 0; c < s->pix_desc->nb_components; c++) {
        const AVComponentDescriptor *comp = &s->pix_desc->comp[c];
        const int p = comp->plane;
        int pix_size, channels;
        int is_planar_u, is_planar_v, is_422_uv;
        CUfunction func;
        CUarray_format format;

        pix_size = (comp->depth + 7) / 8;
        channels = comp->step / pix_size;
        if (pix_size > 2 || channels > 4)
            av_unreachable("Unsupported pixel format!");

        is_planar_u = p == 1 && channels == 1;
        is_planar_v = p == 2 && channels == 1;
        is_422_uv = p && s->pix_desc->log2_chroma_w == 1 && !s->pix_desc->log2_chroma_h;

        if (comp->plane < c || is_planar_v) {
            // We process planes as a whole, so don't reprocess
            // them for additional components
            continue;
        }

        switch (pix_size) {
        case 1:
            func = channels == 4 ? s->cu_func_uchar4 :
                   channels == 2 ? s->cu_func_uchar2 : s->cu_func_uchar;
            format = CU_AD_FORMAT_UNSIGNED_INT8;
            break;
        case 2:
            func = channels == 2 ? s->cu_func_ushort2 : s->cu_func_ushort;
            format = CU_AD_FORMAT_UNSIGNED_INT16;
            break;
        default:
            av_unreachable("Unsupported pixel format!");
        }

        ret = call_kernel(ctx, func, format, channels, is_422_uv,
                          (CUdeviceptr)out->data[p],
                          (CUdeviceptr)(is_planar_u ? out->data[p+1] : NULL),
                          AV_CEIL_RSHIFT(out->width, p ? s->pix_desc->log2_chroma_w : 0),
                          AV_CEIL_RSHIFT(out->height, p ? s->pix_desc->log2_chroma_h : 0),
                          out->linesize[p] / comp->step,
                          (CUdeviceptr)in->data[p],
                          (CUdeviceptr)(is_planar_u ? in->data[p+1] : NULL),
                          AV_CEIL_RSHIFT(in->width, p ? s->pix_desc->log2_chroma_w : 0),
                          AV_CEIL_RSHIFT(in->height, p ? s->pix_desc->log2_chroma_h : 0),
                          in->linesize[p]);
        if (ret < 0)
            return ret;
    }

    return 0;
}

static int cudatranspose_transpose(AVFilterContext *ctx,
                                   AVFrame *out, AVFrame *in)
{
    TransposeCUDAContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    int ret;

    ret = cudatranspose_rotate(ctx, s->frame, in);
    if (ret < 0)
        return ret;

    ret = av_hwframe_get_buffer(s->frame->hw_frames_ctx, s->tmp_frame, 0);
    if (ret < 0)
        return ret;

    av_frame_move_ref(out, s->frame);
    av_frame_move_ref(s->frame, s->tmp_frame);

    s->frame->width  = outlink->w;
    s->frame->height = outlink->h;

    ret = av_frame_copy_props(out, in);
    if (ret < 0)
        return ret;

    if (s->flip_wh && in->sample_aspect_ratio.num)
        out->sample_aspect_ratio = av_inv_q(in->sample_aspect_ratio);
    else
        out->sample_aspect_ratio = in->sample_aspect_ratio;

    return 0;
}

static int cudatranspose_filter_frame(AVFilterLink *link, AVFrame *in)
{
    AVFilterContext      *ctx = link->dst;
    TransposeCUDAContext *s = ctx->priv;
    AVFilterLink         *outlink = ctx->outputs[0];
    CudaFunctions *cu;
    AVFrame *out = NULL;
    CUcontext dummy;
    int ret = 0;

    if (s->passthrough)
        return ff_filter_frame(outlink, in);

    out = av_frame_alloc();
    if (!out) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    cu = s->hwctx->internal->cuda_dl;

    ret = CHECK_CU(cu->cuCtxPushCurrent(s->cu_ctx));
    if (ret < 0)
        goto fail;

    ret = cudatranspose_transpose(ctx, out, in);

    CHECK_CU(cu->cuCtxPopCurrent(&dummy));
    if (ret < 0)
        goto fail;

    av_frame_free(&in);

    return ff_filter_frame(outlink, out);

fail:
    av_frame_free(&in);
    av_frame_free(&out);
    return ret;
}

static AVFrame *cudatranspose_get_video_buffer(AVFilterLink *inlink, int w, int h)
{
    TransposeCUDAContext *s = inlink->dst->priv;

    return s->passthrough ?
        ff_null_get_video_buffer   (inlink, w, h) :
        ff_default_get_video_buffer(inlink, w, h);
}

#define OFFSET(x) offsetof(TransposeCUDAContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)

static const AVOption cudatranspose_options[] = {
    { "dir", "set transpose direction", OFFSET(dir), AV_OPT_TYPE_INT, { .i64 = TRANSPOSE_CCLOCK_FLIP }, 0, 6, FLAGS, .unit = "dir" },
        { "cclock_flip", "rotate counter-clockwise with vertical flip", 0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_CCLOCK_FLIP }, 0, 0, FLAGS, .unit = "dir" },
        { "clock",       "rotate clockwise",                            0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_CLOCK       }, 0, 0, FLAGS, .unit = "dir" },
        { "cclock",      "rotate counter-clockwise",                    0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_CCLOCK      }, 0, 0, FLAGS, .unit = "dir" },
        { "clock_flip",  "rotate clockwise with vertical flip",         0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_CLOCK_FLIP  }, 0, 0, FLAGS, .unit = "dir" },
        { "reversal",    "rotate by half-turn",                         0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_REVERSAL    }, 0, 0, FLAGS, .unit = "dir" },
        { "hflip",       "flip horizontally",                           0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_HFLIP       }, 0, 0, FLAGS, .unit = "dir" },
        { "vflip",       "flip vertically",                             0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_VFLIP       }, 0, 0, FLAGS, .unit = "dir" },

    { "passthrough", "do not apply transposition if the input matches the specified geometry", OFFSET(passthrough), AV_OPT_TYPE_INT, { .i64 = TRANSPOSE_PT_TYPE_NONE },  0, 2, FLAGS, .unit = "passthrough" },
        { "none",      "always apply transposition",  0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_PT_TYPE_NONE },      0, 0, FLAGS, .unit = "passthrough" },
        { "landscape", "preserve landscape geometry", 0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_PT_TYPE_LANDSCAPE }, 0, 0, FLAGS, .unit = "passthrough" },
        { "portrait",  "preserve portrait geometry",  0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_PT_TYPE_PORTRAIT },  0, 0, FLAGS, .unit = "passthrough" },

    { NULL },
};

AVFILTER_DEFINE_CLASS(cudatranspose);

static const AVFilterPad cudatranspose_inputs[] = {
    {
        .name             = "default",
        .type             = AVMEDIA_TYPE_VIDEO,
        .filter_frame     = cudatranspose_filter_frame,
        .get_buffer.video = cudatranspose_get_video_buffer,
    },
};

static const AVFilterPad cudatranspose_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = cudatranspose_config_props,
    },
};

const FFFilter ff_vf_transpose_cuda = {
    .p.name         = "transpose_cuda",
    .p.description  = NULL_IF_CONFIG_SMALL("Transpose input video using CUDA"),
    .p.priv_class   = &cudatranspose_class,
    .init           = cudatranspose_init,
    .uninit         = cudatranspose_uninit,
    .priv_size      = sizeof(TransposeCUDAContext),
    FILTER_INPUTS(cudatranspose_inputs),
    FILTER_OUTPUTS(cudatranspose_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_CUDA),
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
