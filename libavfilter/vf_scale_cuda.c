/*
* Copyright (c) 2017, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
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
#include "scale_eval.h"
#include "video.h"

#include "cuda/load_helper.h"
#include "vf_scale_cuda.h"

static const enum AVPixelFormat supported_formats[] = {
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_NV12,
    AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_P010,
    AV_PIX_FMT_P016,
    AV_PIX_FMT_YUV444P16,
    AV_PIX_FMT_0RGB32,
    AV_PIX_FMT_0BGR32,
};

#define DIV_UP(a, b) ( ((a) + (b) - 1) / (b) )
#define BLOCKX 32
#define BLOCKY 16

#define CHECK_CU(x) FF_CUDA_CHECK_DL(ctx, s->hwctx->internal->cuda_dl, x)

enum {
    INTERP_ALGO_DEFAULT,

    INTERP_ALGO_NEAREST,
    INTERP_ALGO_BILINEAR,
    INTERP_ALGO_BICUBIC,
    INTERP_ALGO_LANCZOS,

    INTERP_ALGO_COUNT
};

typedef struct CUDAScaleContext {
    const AVClass *class;

    AVCUDADeviceContext *hwctx;

    enum AVPixelFormat in_fmt, out_fmt;
    const AVPixFmtDescriptor *in_desc, *out_desc;
    int in_planes, out_planes;
    int in_plane_depths[4];
    int in_plane_channels[4];

    AVBufferRef *frames_ctx;
    AVFrame     *frame;

    AVFrame *tmp_frame;
    int passthrough;

    /**
     * Output sw format. AV_PIX_FMT_NONE for no conversion.
     */
    enum AVPixelFormat format;

    char *w_expr;               ///< width  expression string
    char *h_expr;               ///< height expression string

    int force_original_aspect_ratio;
    int force_divisible_by;

    CUcontext   cu_ctx;
    CUmodule    cu_module;
    CUfunction  cu_func;
    CUfunction  cu_func_uv;
    CUstream    cu_stream;

    int interp_algo;
    int interp_use_linear;
    int interp_as_integer;

    float param;
} CUDAScaleContext;

static av_cold int cudascale_init(AVFilterContext *ctx)
{
    CUDAScaleContext *s = ctx->priv;

    s->frame = av_frame_alloc();
    if (!s->frame)
        return AVERROR(ENOMEM);

    s->tmp_frame = av_frame_alloc();
    if (!s->tmp_frame)
        return AVERROR(ENOMEM);

    return 0;
}

static av_cold void cudascale_uninit(AVFilterContext *ctx)
{
    CUDAScaleContext *s = ctx->priv;

    if (s->hwctx && s->cu_module) {
        CudaFunctions *cu = s->hwctx->internal->cuda_dl;
        CUcontext dummy;

        CHECK_CU(cu->cuCtxPushCurrent(s->hwctx->cuda_ctx));
        CHECK_CU(cu->cuModuleUnload(s->cu_module));
        s->cu_module = NULL;
        CHECK_CU(cu->cuCtxPopCurrent(&dummy));
    }

    av_frame_free(&s->frame);
    av_buffer_unref(&s->frames_ctx);
    av_frame_free(&s->tmp_frame);
}

static av_cold int init_hwframe_ctx(CUDAScaleContext *s, AVBufferRef *device_ctx, int width, int height)
{
    AVBufferRef *out_ref = NULL;
    AVHWFramesContext *out_ctx;
    int ret;

    out_ref = av_hwframe_ctx_alloc(device_ctx);
    if (!out_ref)
        return AVERROR(ENOMEM);
    out_ctx = (AVHWFramesContext*)out_ref->data;

    out_ctx->format    = AV_PIX_FMT_CUDA;
    out_ctx->sw_format = s->out_fmt;
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
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(supported_formats); i++)
        if (supported_formats[i] == fmt)
            return 1;
    return 0;
}

static av_cold void set_format_info(AVFilterContext *ctx, enum AVPixelFormat in_format, enum AVPixelFormat out_format)
{
    CUDAScaleContext *s = ctx->priv;
    int i, p, d;

    s->in_fmt = in_format;
    s->out_fmt = out_format;

    s->in_desc  = av_pix_fmt_desc_get(s->in_fmt);
    s->out_desc = av_pix_fmt_desc_get(s->out_fmt);
    s->in_planes  = av_pix_fmt_count_planes(s->in_fmt);
    s->out_planes = av_pix_fmt_count_planes(s->out_fmt);

    // find maximum step of each component of each plane
    // For our subset of formats, this should accurately tell us how many channels CUDA needs
    // i.e. 1 for Y plane, 2 for UV plane of NV12, 4 for single plane of RGB0 formats

    for (i = 0; i < s->in_desc->nb_components; i++) {
        d = (s->in_desc->comp[i].depth + 7) / 8;
        p = s->in_desc->comp[i].plane;
        s->in_plane_channels[p] = FFMAX(s->in_plane_channels[p], s->in_desc->comp[i].step / d);

        s->in_plane_depths[p] = s->in_desc->comp[i].depth;
    }
}

static av_cold int init_processing_chain(AVFilterContext *ctx, int in_width, int in_height,
                                         int out_width, int out_height)
{
    CUDAScaleContext *s = ctx->priv;

    AVHWFramesContext *in_frames_ctx;

    enum AVPixelFormat in_format;
    enum AVPixelFormat out_format;
    int ret;

    /* check that we have a hw context */
    if (!ctx->inputs[0]->hw_frames_ctx) {
        av_log(ctx, AV_LOG_ERROR, "No hw context provided on input\n");
        return AVERROR(EINVAL);
    }
    in_frames_ctx = (AVHWFramesContext*)ctx->inputs[0]->hw_frames_ctx->data;
    in_format     = in_frames_ctx->sw_format;
    out_format    = (s->format == AV_PIX_FMT_NONE) ? in_format : s->format;

    if (!format_is_supported(in_format)) {
        av_log(ctx, AV_LOG_ERROR, "Unsupported input format: %s\n",
               av_get_pix_fmt_name(in_format));
        return AVERROR(ENOSYS);
    }
    if (!format_is_supported(out_format)) {
        av_log(ctx, AV_LOG_ERROR, "Unsupported output format: %s\n",
               av_get_pix_fmt_name(out_format));
        return AVERROR(ENOSYS);
    }

    set_format_info(ctx, in_format, out_format);

    if (s->passthrough && in_width == out_width && in_height == out_height && in_format == out_format) {
        s->frames_ctx = av_buffer_ref(ctx->inputs[0]->hw_frames_ctx);
        if (!s->frames_ctx)
            return AVERROR(ENOMEM);
    } else {
        s->passthrough = 0;

        ret = init_hwframe_ctx(s, in_frames_ctx->device_ref, out_width, out_height);
        if (ret < 0)
            return ret;

        if (in_width == out_width && in_height == out_height &&
            in_format == out_format && s->interp_algo == INTERP_ALGO_DEFAULT)
            s->interp_algo = INTERP_ALGO_NEAREST;
    }

    ctx->outputs[0]->hw_frames_ctx = av_buffer_ref(s->frames_ctx);
    if (!ctx->outputs[0]->hw_frames_ctx)
        return AVERROR(ENOMEM);

    return 0;
}

static av_cold int cudascale_load_functions(AVFilterContext *ctx)
{
    CUDAScaleContext *s = ctx->priv;
    CUcontext dummy, cuda_ctx = s->hwctx->cuda_ctx;
    CudaFunctions *cu = s->hwctx->internal->cuda_dl;
    char buf[128];
    int ret;

    const char *in_fmt_name = av_get_pix_fmt_name(s->in_fmt);
    const char *out_fmt_name = av_get_pix_fmt_name(s->out_fmt);

    const char *function_infix = "";

    extern const unsigned char ff_vf_scale_cuda_ptx_data[];
    extern const unsigned int ff_vf_scale_cuda_ptx_len;

    switch(s->interp_algo) {
    case INTERP_ALGO_NEAREST:
        function_infix = "Nearest";
        s->interp_use_linear = 0;
        s->interp_as_integer = 1;
        break;
    case INTERP_ALGO_BILINEAR:
        function_infix = "Bilinear";
        s->interp_use_linear = 1;
        s->interp_as_integer = 1;
        break;
    case INTERP_ALGO_DEFAULT:
    case INTERP_ALGO_BICUBIC:
        function_infix = "Bicubic";
        s->interp_use_linear = 0;
        s->interp_as_integer = 0;
        break;
    case INTERP_ALGO_LANCZOS:
        function_infix = "Lanczos";
        s->interp_use_linear = 0;
        s->interp_as_integer = 0;
        break;
    default:
        av_log(ctx, AV_LOG_ERROR, "Unknown interpolation algorithm\n");
        return AVERROR_BUG;
    }

    ret = CHECK_CU(cu->cuCtxPushCurrent(cuda_ctx));
    if (ret < 0)
        return ret;

    ret = ff_cuda_load_module(ctx, s->hwctx, &s->cu_module,
                              ff_vf_scale_cuda_ptx_data, ff_vf_scale_cuda_ptx_len);
    if (ret < 0)
        goto fail;

    snprintf(buf, sizeof(buf), "Subsample_%s_%s_%s", function_infix, in_fmt_name, out_fmt_name);
    ret = CHECK_CU(cu->cuModuleGetFunction(&s->cu_func, s->cu_module, buf));
    if (ret < 0) {
        av_log(ctx, AV_LOG_FATAL, "Unsupported conversion: %s -> %s\n", in_fmt_name, out_fmt_name);
        ret = AVERROR(ENOSYS);
        goto fail;
    }

    snprintf(buf, sizeof(buf), "Subsample_%s_%s_%s_uv", function_infix, in_fmt_name, out_fmt_name);
    ret = CHECK_CU(cu->cuModuleGetFunction(&s->cu_func_uv, s->cu_module, buf));
    if (ret < 0)
        goto fail;

fail:
    CHECK_CU(cu->cuCtxPopCurrent(&dummy));

    return ret;
}

static av_cold int cudascale_config_props(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = outlink->src->inputs[0];
    CUDAScaleContext *s  = ctx->priv;
    AVHWFramesContext     *frames_ctx = (AVHWFramesContext*)inlink->hw_frames_ctx->data;
    AVCUDADeviceContext *device_hwctx = frames_ctx->device_ctx->hwctx;
    int w, h;
    int ret;

    s->hwctx = device_hwctx;
    s->cu_stream = s->hwctx->stream;

    if ((ret = ff_scale_eval_dimensions(s,
                                        s->w_expr, s->h_expr,
                                        inlink, outlink,
                                        &w, &h)) < 0)
        goto fail;

    ff_scale_adjust_dimensions(inlink, &w, &h,
                               s->force_original_aspect_ratio, s->force_divisible_by);

    if (((int64_t)h * inlink->w) > INT_MAX  ||
        ((int64_t)w * inlink->h) > INT_MAX)
        av_log(ctx, AV_LOG_ERROR, "Rescaled value for width or height is too big.\n");

    outlink->w = w;
    outlink->h = h;

    ret = init_processing_chain(ctx, inlink->w, inlink->h, w, h);
    if (ret < 0)
        return ret;

    if (inlink->sample_aspect_ratio.num) {
        outlink->sample_aspect_ratio = av_mul_q((AVRational){outlink->h*inlink->w,
                                                             outlink->w*inlink->h},
                                                inlink->sample_aspect_ratio);
    } else {
        outlink->sample_aspect_ratio = inlink->sample_aspect_ratio;
    }

    av_log(ctx, AV_LOG_VERBOSE, "w:%d h:%d fmt:%s -> w:%d h:%d fmt:%s%s\n",
           inlink->w, inlink->h, av_get_pix_fmt_name(s->in_fmt),
           outlink->w, outlink->h, av_get_pix_fmt_name(s->out_fmt),
           s->passthrough ? " (passthrough)" : "");

    ret = cudascale_load_functions(ctx);
    if (ret < 0)
        return ret;

    return 0;

fail:
    return ret;
}

static int call_resize_kernel(AVFilterContext *ctx, CUfunction func,
                              CUtexObject src_tex[4], int src_width, int src_height,
                              AVFrame *out_frame, int dst_width, int dst_height, int dst_pitch)
{
    CUDAScaleContext *s = ctx->priv;
    CudaFunctions *cu = s->hwctx->internal->cuda_dl;

    CUdeviceptr dst_devptr[4] = {
        (CUdeviceptr)out_frame->data[0], (CUdeviceptr)out_frame->data[1],
        (CUdeviceptr)out_frame->data[2], (CUdeviceptr)out_frame->data[3]
    };

    void *args_uchar[] = {
        &src_tex[0], &src_tex[1], &src_tex[2], &src_tex[3],
        &dst_devptr[0], &dst_devptr[1], &dst_devptr[2], &dst_devptr[3],
        &dst_width, &dst_height, &dst_pitch,
        &src_width, &src_height, &s->param
    };

    return CHECK_CU(cu->cuLaunchKernel(func,
                                       DIV_UP(dst_width, BLOCKX), DIV_UP(dst_height, BLOCKY), 1,
                                       BLOCKX, BLOCKY, 1, 0, s->cu_stream, args_uchar, NULL));
}

static int scalecuda_resize(AVFilterContext *ctx,
                            AVFrame *out, AVFrame *in)
{
    CUDAScaleContext *s = ctx->priv;
    CudaFunctions *cu = s->hwctx->internal->cuda_dl;
    CUcontext dummy, cuda_ctx = s->hwctx->cuda_ctx;
    int i, ret;

    CUtexObject tex[4] = { 0, 0, 0, 0 };

    ret = CHECK_CU(cu->cuCtxPushCurrent(cuda_ctx));
    if (ret < 0)
        return ret;

    for (i = 0; i < s->in_planes; i++) {
        CUDA_TEXTURE_DESC tex_desc = {
            .filterMode = s->interp_use_linear ?
                          CU_TR_FILTER_MODE_LINEAR :
                          CU_TR_FILTER_MODE_POINT,
            .flags = s->interp_as_integer ? CU_TRSF_READ_AS_INTEGER : 0,
        };

        CUDA_RESOURCE_DESC res_desc = {
            .resType = CU_RESOURCE_TYPE_PITCH2D,
            .res.pitch2D.format = s->in_plane_depths[i] <= 8 ?
                                  CU_AD_FORMAT_UNSIGNED_INT8 :
                                  CU_AD_FORMAT_UNSIGNED_INT16,
            .res.pitch2D.numChannels = s->in_plane_channels[i],
            .res.pitch2D.pitchInBytes = in->linesize[i],
            .res.pitch2D.devPtr = (CUdeviceptr)in->data[i],
        };

        if (i == 1 || i == 2) {
            res_desc.res.pitch2D.width = AV_CEIL_RSHIFT(in->width, s->in_desc->log2_chroma_w);
            res_desc.res.pitch2D.height = AV_CEIL_RSHIFT(in->height, s->in_desc->log2_chroma_h);
        } else {
            res_desc.res.pitch2D.width = in->width;
            res_desc.res.pitch2D.height = in->height;
        }

        ret = CHECK_CU(cu->cuTexObjectCreate(&tex[i], &res_desc, &tex_desc, NULL));
        if (ret < 0)
            goto exit;
    }

    // scale primary plane(s). Usually Y (and A), or single plane of RGB frames.
    ret = call_resize_kernel(ctx, s->cu_func,
                             tex, in->width, in->height,
                             out, out->width, out->height, out->linesize[0]);
    if (ret < 0)
        goto exit;

    if (s->out_planes > 1) {
        // scale UV plane. Scale function sets both U and V plane, or singular interleaved plane.
        ret = call_resize_kernel(ctx, s->cu_func_uv, tex,
                                 AV_CEIL_RSHIFT(in->width, s->in_desc->log2_chroma_w),
                                 AV_CEIL_RSHIFT(in->height, s->in_desc->log2_chroma_h),
                                 out,
                                 AV_CEIL_RSHIFT(out->width, s->out_desc->log2_chroma_w),
                                 AV_CEIL_RSHIFT(out->height, s->out_desc->log2_chroma_h),
                                 out->linesize[1]);
        if (ret < 0)
            goto exit;
    }

exit:
    for (i = 0; i < s->in_planes; i++)
        if (tex[i])
            CHECK_CU(cu->cuTexObjectDestroy(tex[i]));

    CHECK_CU(cu->cuCtxPopCurrent(&dummy));

    return ret;
}

static int cudascale_scale(AVFilterContext *ctx, AVFrame *out, AVFrame *in)
{
    CUDAScaleContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *src = in;
    int ret;

    ret = scalecuda_resize(ctx, s->frame, src);
    if (ret < 0)
        return ret;

    src = s->frame;
    ret = av_hwframe_get_buffer(src->hw_frames_ctx, s->tmp_frame, 0);
    if (ret < 0)
        return ret;

    av_frame_move_ref(out, s->frame);
    av_frame_move_ref(s->frame, s->tmp_frame);

    s->frame->width  = outlink->w;
    s->frame->height = outlink->h;

    ret = av_frame_copy_props(out, in);
    if (ret < 0)
        return ret;

    return 0;
}

static int cudascale_filter_frame(AVFilterLink *link, AVFrame *in)
{
    AVFilterContext       *ctx = link->dst;
    CUDAScaleContext        *s = ctx->priv;
    AVFilterLink      *outlink = ctx->outputs[0];
    CudaFunctions          *cu = s->hwctx->internal->cuda_dl;

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

    ret = CHECK_CU(cu->cuCtxPushCurrent(s->hwctx->cuda_ctx));
    if (ret < 0)
        goto fail;

    ret = cudascale_scale(ctx, out, in);

    CHECK_CU(cu->cuCtxPopCurrent(&dummy));
    if (ret < 0)
        goto fail;

    av_reduce(&out->sample_aspect_ratio.num, &out->sample_aspect_ratio.den,
              (int64_t)in->sample_aspect_ratio.num * outlink->h * link->w,
              (int64_t)in->sample_aspect_ratio.den * outlink->w * link->h,
              INT_MAX);

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
fail:
    av_frame_free(&in);
    av_frame_free(&out);
    return ret;
}

static AVFrame *cudascale_get_video_buffer(AVFilterLink *inlink, int w, int h)
{
    CUDAScaleContext *s = inlink->dst->priv;

    return s->passthrough ?
        ff_null_get_video_buffer   (inlink, w, h) :
        ff_default_get_video_buffer(inlink, w, h);
}

#define OFFSET(x) offsetof(CUDAScaleContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM)
static const AVOption options[] = {
    { "w", "Output video width",  OFFSET(w_expr), AV_OPT_TYPE_STRING, { .str = "iw" }, .flags = FLAGS },
    { "h", "Output video height", OFFSET(h_expr), AV_OPT_TYPE_STRING, { .str = "ih" }, .flags = FLAGS },
    { "interp_algo", "Interpolation algorithm used for resizing", OFFSET(interp_algo), AV_OPT_TYPE_INT, { .i64 = INTERP_ALGO_DEFAULT }, 0, INTERP_ALGO_COUNT - 1, FLAGS, "interp_algo" },
        { "nearest",  "nearest neighbour", 0, AV_OPT_TYPE_CONST, { .i64 = INTERP_ALGO_NEAREST }, 0, 0, FLAGS, "interp_algo" },
        { "bilinear", "bilinear", 0, AV_OPT_TYPE_CONST, { .i64 = INTERP_ALGO_BILINEAR }, 0, 0, FLAGS, "interp_algo" },
        { "bicubic",  "bicubic",  0, AV_OPT_TYPE_CONST, { .i64 = INTERP_ALGO_BICUBIC  }, 0, 0, FLAGS, "interp_algo" },
        { "lanczos",  "lanczos",  0, AV_OPT_TYPE_CONST, { .i64 = INTERP_ALGO_LANCZOS  }, 0, 0, FLAGS, "interp_algo" },
    { "format", "Output video pixel format", OFFSET(format), AV_OPT_TYPE_PIXEL_FMT, { .i64 = AV_PIX_FMT_NONE }, INT_MIN, INT_MAX, .flags=FLAGS },
    { "passthrough", "Do not process frames at all if parameters match", OFFSET(passthrough), AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1, FLAGS },
    { "param", "Algorithm-Specific parameter", OFFSET(param), AV_OPT_TYPE_FLOAT, { .dbl = SCALE_CUDA_PARAM_DEFAULT }, -FLT_MAX, FLT_MAX, FLAGS },
    { "force_original_aspect_ratio", "decrease or increase w/h if necessary to keep the original AR", OFFSET(force_original_aspect_ratio), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 2, FLAGS, "force_oar" },
        { "disable",  NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 0 }, 0, 0, FLAGS, "force_oar" },
        { "decrease", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 1 }, 0, 0, FLAGS, "force_oar" },
        { "increase", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 2 }, 0, 0, FLAGS, "force_oar" },
    { "force_divisible_by", "enforce that the output resolution is divisible by a defined integer when force_original_aspect_ratio is used", OFFSET(force_divisible_by), AV_OPT_TYPE_INT, { .i64 = 1 }, 1, 256, FLAGS },
    { NULL },
};

static const AVClass cudascale_class = {
    .class_name = "cudascale",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVFilterPad cudascale_inputs[] = {
    {
        .name        = "default",
        .type        = AVMEDIA_TYPE_VIDEO,
        .filter_frame = cudascale_filter_frame,
        .get_buffer.video = cudascale_get_video_buffer,
    },
};

static const AVFilterPad cudascale_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = cudascale_config_props,
    },
};

const AVFilter ff_vf_scale_cuda = {
    .name      = "scale_cuda",
    .description = NULL_IF_CONFIG_SMALL("GPU accelerated video resizer"),

    .init          = cudascale_init,
    .uninit        = cudascale_uninit,

    .priv_size = sizeof(CUDAScaleContext),
    .priv_class = &cudascale_class,

    FILTER_INPUTS(cudascale_inputs),
    FILTER_OUTPUTS(cudascale_outputs),

    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_CUDA),

    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
