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

#include <cuda.h>
#include <stdio.h>
#include <string.h>

#include "libavutil/avstring.h"
#include "libavutil/common.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_cuda.h"
#include "libavutil/cuda_check.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "scale.h"
#include "video.h"

static const enum AVPixelFormat supported_formats[] = {
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_NV12,
    AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_P010,
    AV_PIX_FMT_P016
};

#define DIV_UP(a, b) ( ((a) + (b) - 1) / (b) )
#define ALIGN_UP(a, b) (((a) + (b) - 1) & ~((b) - 1))
#define NUM_BUFFERS 2
#define BLOCKX 32
#define BLOCKY 16

#define CHECK_CU(x) FF_CUDA_CHECK(ctx, x)

typedef struct CUDAScaleContext {
    const AVClass *class;
    enum AVPixelFormat in_fmt;
    enum AVPixelFormat out_fmt;

    struct {
        int width;
        int height;
    } planes_in[3], planes_out[3];

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

    CUcontext   cu_ctx;
    CUevent     cu_event;
    CUmodule    cu_module;
    CUfunction  cu_func_uchar;
    CUfunction  cu_func_uchar2;
    CUfunction  cu_func_uchar4;
    CUfunction  cu_func_ushort;
    CUfunction  cu_func_ushort2;
    CUfunction  cu_func_ushort4;
    CUtexref    cu_tex_uchar;
    CUtexref    cu_tex_uchar2;
    CUtexref    cu_tex_uchar4;
    CUtexref    cu_tex_ushort;
    CUtexref    cu_tex_ushort2;
    CUtexref    cu_tex_ushort4;

    CUdeviceptr srcBuffer;
    CUdeviceptr dstBuffer;
    int         tex_alignment;
} CUDAScaleContext;

static av_cold int cudascale_init(AVFilterContext *ctx)
{
    CUDAScaleContext *s = ctx->priv;

    s->format = AV_PIX_FMT_NONE;
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

    av_frame_free(&s->frame);
    av_buffer_unref(&s->frames_ctx);
    av_frame_free(&s->tmp_frame);
}

static int cudascale_query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pixel_formats[] = {
        AV_PIX_FMT_CUDA, AV_PIX_FMT_NONE,
    };
    AVFilterFormats *pix_fmts = ff_make_format_list(pixel_formats);

    return ff_set_common_formats(ctx, pix_fmts);
}

static av_cold int init_stage(CUDAScaleContext *s, AVBufferRef *device_ctx)
{
    AVBufferRef *out_ref = NULL;
    AVHWFramesContext *out_ctx;
    int in_sw, in_sh, out_sw, out_sh;
    int ret, i;

    av_pix_fmt_get_chroma_sub_sample(s->in_fmt,  &in_sw,  &in_sh);
    av_pix_fmt_get_chroma_sub_sample(s->out_fmt, &out_sw, &out_sh);
    if (!s->planes_out[0].width) {
        s->planes_out[0].width  = s->planes_in[0].width;
        s->planes_out[0].height = s->planes_in[0].height;
    }

    for (i = 1; i < FF_ARRAY_ELEMS(s->planes_in); i++) {
        s->planes_in[i].width   = s->planes_in[0].width   >> in_sw;
        s->planes_in[i].height  = s->planes_in[0].height  >> in_sh;
        s->planes_out[i].width  = s->planes_out[0].width  >> out_sw;
        s->planes_out[i].height = s->planes_out[0].height >> out_sh;
    }

    out_ref = av_hwframe_ctx_alloc(device_ctx);
    if (!out_ref)
        return AVERROR(ENOMEM);
    out_ctx = (AVHWFramesContext*)out_ref->data;

    out_ctx->format    = AV_PIX_FMT_CUDA;
    out_ctx->sw_format = s->out_fmt;
    out_ctx->width     = FFALIGN(s->planes_out[0].width,  32);
    out_ctx->height    = FFALIGN(s->planes_out[0].height, 32);

    ret = av_hwframe_ctx_init(out_ref);
    if (ret < 0)
        goto fail;

    av_frame_unref(s->frame);
    ret = av_hwframe_get_buffer(out_ref, s->frame, 0);
    if (ret < 0)
        goto fail;

    s->frame->width  = s->planes_out[0].width;
    s->frame->height = s->planes_out[0].height;

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

    if (in_width == out_width && in_height == out_height)
        s->passthrough = 1;

    s->in_fmt = in_format;
    s->out_fmt = out_format;

    s->planes_in[0].width   = in_width;
    s->planes_in[0].height  = in_height;
    s->planes_out[0].width  = out_width;
    s->planes_out[0].height = out_height;

    ret = init_stage(s, in_frames_ctx->device_ref);
    if (ret < 0)
        return ret;

    ctx->outputs[0]->hw_frames_ctx = av_buffer_ref(s->frames_ctx);
    if (!ctx->outputs[0]->hw_frames_ctx)
        return AVERROR(ENOMEM);

    return 0;
}

static av_cold int cudascale_config_props(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = outlink->src->inputs[0];
    CUDAScaleContext *s  = ctx->priv;
    AVHWFramesContext     *frames_ctx = (AVHWFramesContext*)inlink->hw_frames_ctx->data;
    AVCUDADeviceContext *device_hwctx = frames_ctx->device_ctx->hwctx;
    CUcontext dummy, cuda_ctx = device_hwctx->cuda_ctx;
    int w, h;
    int ret;

    extern char vf_scale_cuda_ptx[];

    ret = CHECK_CU(cuCtxPushCurrent(cuda_ctx));
    if (ret < 0)
        goto fail;

    ret = CHECK_CU(cuModuleLoadData(&s->cu_module, vf_scale_cuda_ptx));
    if (ret < 0)
        goto fail;

    CHECK_CU(cuModuleGetFunction(&s->cu_func_uchar, s->cu_module, "Subsample_Bilinear_uchar"));
    CHECK_CU(cuModuleGetFunction(&s->cu_func_uchar2, s->cu_module, "Subsample_Bilinear_uchar2"));
    CHECK_CU(cuModuleGetFunction(&s->cu_func_uchar4, s->cu_module, "Subsample_Bilinear_uchar4"));
    CHECK_CU(cuModuleGetFunction(&s->cu_func_ushort, s->cu_module, "Subsample_Bilinear_ushort"));
    CHECK_CU(cuModuleGetFunction(&s->cu_func_ushort2, s->cu_module, "Subsample_Bilinear_ushort2"));
    CHECK_CU(cuModuleGetFunction(&s->cu_func_ushort4, s->cu_module, "Subsample_Bilinear_ushort4"));

    CHECK_CU(cuModuleGetTexRef(&s->cu_tex_uchar, s->cu_module, "uchar_tex"));
    CHECK_CU(cuModuleGetTexRef(&s->cu_tex_uchar2, s->cu_module, "uchar2_tex"));
    CHECK_CU(cuModuleGetTexRef(&s->cu_tex_uchar4, s->cu_module, "uchar4_tex"));
    CHECK_CU(cuModuleGetTexRef(&s->cu_tex_ushort, s->cu_module, "ushort_tex"));
    CHECK_CU(cuModuleGetTexRef(&s->cu_tex_ushort2, s->cu_module, "ushort2_tex"));
    CHECK_CU(cuModuleGetTexRef(&s->cu_tex_ushort4, s->cu_module, "ushort4_tex"));

    CHECK_CU(cuTexRefSetFlags(s->cu_tex_uchar, CU_TRSF_READ_AS_INTEGER));
    CHECK_CU(cuTexRefSetFlags(s->cu_tex_uchar2, CU_TRSF_READ_AS_INTEGER));
    CHECK_CU(cuTexRefSetFlags(s->cu_tex_uchar4, CU_TRSF_READ_AS_INTEGER));
    CHECK_CU(cuTexRefSetFlags(s->cu_tex_ushort, CU_TRSF_READ_AS_INTEGER));
    CHECK_CU(cuTexRefSetFlags(s->cu_tex_ushort2, CU_TRSF_READ_AS_INTEGER));
    CHECK_CU(cuTexRefSetFlags(s->cu_tex_ushort4, CU_TRSF_READ_AS_INTEGER));

    CHECK_CU(cuTexRefSetFilterMode(s->cu_tex_uchar, CU_TR_FILTER_MODE_LINEAR));
    CHECK_CU(cuTexRefSetFilterMode(s->cu_tex_uchar2, CU_TR_FILTER_MODE_LINEAR));
    CHECK_CU(cuTexRefSetFilterMode(s->cu_tex_uchar4, CU_TR_FILTER_MODE_LINEAR));
    CHECK_CU(cuTexRefSetFilterMode(s->cu_tex_ushort, CU_TR_FILTER_MODE_LINEAR));
    CHECK_CU(cuTexRefSetFilterMode(s->cu_tex_ushort2, CU_TR_FILTER_MODE_LINEAR));
    CHECK_CU(cuTexRefSetFilterMode(s->cu_tex_ushort4, CU_TR_FILTER_MODE_LINEAR));

    CHECK_CU(cuCtxPopCurrent(&dummy));

    if ((ret = ff_scale_eval_dimensions(s,
                                        s->w_expr, s->h_expr,
                                        inlink, outlink,
                                        &w, &h)) < 0)
        goto fail;

    if (((int64_t)h * inlink->w) > INT_MAX  ||
        ((int64_t)w * inlink->h) > INT_MAX)
        av_log(ctx, AV_LOG_ERROR, "Rescaled value for width or height is too big.\n");

    outlink->w = w;
    outlink->h = h;

    ret = init_processing_chain(ctx, inlink->w, inlink->h, w, h);
    if (ret < 0)
        return ret;

    av_log(ctx, AV_LOG_VERBOSE, "w:%d h:%d -> w:%d h:%d\n",
           inlink->w, inlink->h, outlink->w, outlink->h);

    if (inlink->sample_aspect_ratio.num) {
        outlink->sample_aspect_ratio = av_mul_q((AVRational){outlink->h*inlink->w,
                                                             outlink->w*inlink->h},
                                                inlink->sample_aspect_ratio);
    } else {
        outlink->sample_aspect_ratio = inlink->sample_aspect_ratio;
    }

    return 0;

fail:
    return ret;
}

static int call_resize_kernel(CUDAScaleContext *ctx, CUfunction func, CUtexref tex, int channels,
                              uint8_t *src_dptr, int src_width, int src_height, int src_pitch,
                              uint8_t *dst_dptr, int dst_width, int dst_height, int dst_pitch,
                              int pixel_size)
{
    CUdeviceptr src_devptr = (CUdeviceptr)src_dptr;
    CUdeviceptr dst_devptr = (CUdeviceptr)dst_dptr;
    void *args_uchar[] = { &dst_devptr, &dst_width, &dst_height, &dst_pitch, &src_width, &src_height };
    CUDA_ARRAY_DESCRIPTOR desc;

    desc.Width  = src_width;
    desc.Height = src_height;
    desc.NumChannels = channels;
    if (pixel_size == 1) {
        desc.Format = CU_AD_FORMAT_UNSIGNED_INT8;
    } else {
        desc.Format = CU_AD_FORMAT_UNSIGNED_INT16;
    }

    CHECK_CU(cuTexRefSetAddress2D_v3(tex, &desc, src_devptr, src_pitch * pixel_size));
    CHECK_CU(cuLaunchKernel(func, DIV_UP(dst_width, BLOCKX), DIV_UP(dst_height, BLOCKY), 1,
                            BLOCKX, BLOCKY, 1, 0, 0, args_uchar, NULL));

    return 0;
}

static int scalecuda_resize(AVFilterContext *ctx,
                            AVFrame *out, AVFrame *in)
{
    AVHWFramesContext *in_frames_ctx = (AVHWFramesContext*)in->hw_frames_ctx->data;
    CUDAScaleContext *s = ctx->priv;

    switch (in_frames_ctx->sw_format) {
    case AV_PIX_FMT_YUV420P:
        call_resize_kernel(s, s->cu_func_uchar, s->cu_tex_uchar, 1,
                           in->data[0], in->width, in->height, in->linesize[0],
                           out->data[0], out->width, out->height, out->linesize[0],
                           1);
        call_resize_kernel(s, s->cu_func_uchar, s->cu_tex_uchar, 1,
                           in->data[0]+in->linesize[0]*in->height, in->width/2, in->height/2, in->linesize[0]/2,
                           out->data[0]+out->linesize[0]*out->height, out->width/2, out->height/2, out->linesize[0]/2,
                           1);
        call_resize_kernel(s, s->cu_func_uchar, s->cu_tex_uchar, 1,
                           in->data[0]+ ALIGN_UP((in->linesize[0]*in->height*5)/4, s->tex_alignment), in->width/2, in->height/2, in->linesize[0]/2,
                           out->data[0]+(out->linesize[0]*out->height*5)/4, out->width/2, out->height/2, out->linesize[0]/2,
                           1);
        break;
    case AV_PIX_FMT_YUV444P:
        call_resize_kernel(s, s->cu_func_uchar, s->cu_tex_uchar, 1,
                           in->data[0], in->width, in->height, in->linesize[0],
                           out->data[0], out->width, out->height, out->linesize[0],
                           1);
        call_resize_kernel(s, s->cu_func_uchar, s->cu_tex_uchar, 1,
                           in->data[0]+in->linesize[0]*in->height, in->width, in->height, in->linesize[0],
                           out->data[0]+out->linesize[0]*out->height, out->width, out->height, out->linesize[0],
                           1);
        call_resize_kernel(s, s->cu_func_uchar, s->cu_tex_uchar, 1,
                           in->data[0]+in->linesize[0]*in->height*2, in->width, in->height, in->linesize[0],
                           out->data[0]+out->linesize[0]*out->height*2, out->width, out->height, out->linesize[0],
                           1);
        break;
    case AV_PIX_FMT_NV12:
        call_resize_kernel(s, s->cu_func_uchar, s->cu_tex_uchar, 1,
                           in->data[0], in->width, in->height, in->linesize[0],
                           out->data[0], out->width, out->height, out->linesize[0],
                           1);
        call_resize_kernel(s, s->cu_func_uchar2, s->cu_tex_uchar2, 2,
                           in->data[1], in->width/2, in->height/2, in->linesize[1],
                           out->data[0] + out->linesize[0] * ((out->height + 31) & ~0x1f), out->width/2, out->height/2, out->linesize[1]/2,
                           1);
        break;
    case AV_PIX_FMT_P010LE:
        call_resize_kernel(s, s->cu_func_ushort, s->cu_tex_ushort, 1,
                           in->data[0], in->width, in->height, in->linesize[0]/2,
                           out->data[0], out->width, out->height, out->linesize[0]/2,
                           2);
        call_resize_kernel(s, s->cu_func_ushort2, s->cu_tex_ushort2, 2,
                           in->data[1], in->width / 2, in->height / 2, in->linesize[1]/2,
                           out->data[0] + out->linesize[0] * ((out->height + 31) & ~0x1f), out->width / 2, out->height / 2, out->linesize[1] / 4,
                           2);
        break;
    case AV_PIX_FMT_P016LE:
        call_resize_kernel(s, s->cu_func_ushort, s->cu_tex_ushort, 1,
                           in->data[0], in->width, in->height, in->linesize[0] / 2,
                           out->data[0], out->width, out->height, out->linesize[0] / 2,
                           2);
        call_resize_kernel(s, s->cu_func_ushort2, s->cu_tex_ushort2, 2,
                           in->data[1], in->width / 2, in->height / 2, in->linesize[1] / 2,
                           out->data[0] + out->linesize[0] * ((out->height + 31) & ~0x1f), out->width / 2, out->height / 2, out->linesize[1] / 4,
                           2);
        break;
    default:
        return AVERROR_BUG;
    }

    return 0;
}

static int cudascale_scale(AVFilterContext *ctx, AVFrame *out, AVFrame *in)
{
    CUDAScaleContext *s = ctx->priv;
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

    ret = av_frame_copy_props(out, in);
    if (ret < 0)
        return ret;

    return 0;
}

static int cudascale_filter_frame(AVFilterLink *link, AVFrame *in)
{
    AVFilterContext              *ctx = link->dst;
    CUDAScaleContext               *s = ctx->priv;
    AVFilterLink             *outlink = ctx->outputs[0];
    AVHWFramesContext     *frames_ctx = (AVHWFramesContext*)s->frames_ctx->data;
    AVCUDADeviceContext *device_hwctx = frames_ctx->device_ctx->hwctx;

    AVFrame *out = NULL;
    CUcontext dummy;
    int ret = 0;

    out = av_frame_alloc();
    if (!out) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    ret = CHECK_CU(cuCtxPushCurrent(device_hwctx->cuda_ctx));
    if (ret < 0)
        goto fail;

    ret = cudascale_scale(ctx, out, in);

    CHECK_CU(cuCtxPopCurrent(&dummy));
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

#define OFFSET(x) offsetof(CUDAScaleContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM)
static const AVOption options[] = {
    { "w",      "Output video width",  OFFSET(w_expr),     AV_OPT_TYPE_STRING, { .str = "iw"   }, .flags = FLAGS },
    { "h",      "Output video height", OFFSET(h_expr),     AV_OPT_TYPE_STRING, { .str = "ih"   }, .flags = FLAGS },
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
    },
    { NULL }
};

static const AVFilterPad cudascale_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = cudascale_config_props,
    },
    { NULL }
};

AVFilter ff_vf_scale_cuda = {
    .name      = "scale_cuda",
    .description = NULL_IF_CONFIG_SMALL("GPU accelerated video resizer"),

    .init          = cudascale_init,
    .uninit        = cudascale_uninit,
    .query_formats = cudascale_query_formats,

    .priv_size = sizeof(CUDAScaleContext),
    .priv_class = &cudascale_class,

    .inputs    = cudascale_inputs,
    .outputs   = cudascale_outputs,

    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
