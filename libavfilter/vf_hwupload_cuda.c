/*
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/buffer.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_cuda.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"

#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

typedef struct CudaUploadContext {
    const AVClass *class;
    int device_idx;

    AVBufferRef *hwdevice;
    AVBufferRef *hwframe;
} CudaUploadContext;

static void cudaupload_ctx_free(AVHWDeviceContext *ctx)
{
    AVCUDADeviceContext *hwctx = ctx->hwctx;
    cuCtxDestroy(hwctx->cuda_ctx);
}

static av_cold int cudaupload_init(AVFilterContext *ctx)
{
    CudaUploadContext *s = ctx->priv;

    AVHWDeviceContext   *device_ctx;
    AVCUDADeviceContext *device_hwctx;
    CUdevice device;
    CUcontext cuda_ctx = NULL, dummy;
    CUresult err;
    int ret;

    err = cuInit(0);
    if (err != CUDA_SUCCESS) {
        av_log(ctx, AV_LOG_ERROR, "Could not initialize the CUDA driver API\n");
        return AVERROR_UNKNOWN;
    }

    err = cuDeviceGet(&device, s->device_idx);
    if (err != CUDA_SUCCESS) {
        av_log(ctx, AV_LOG_ERROR, "Could not get the device number %d\n", s->device_idx);
        return AVERROR_UNKNOWN;
    }

    err = cuCtxCreate(&cuda_ctx, 0, device);
    if (err != CUDA_SUCCESS) {
        av_log(ctx, AV_LOG_ERROR, "Error creating a CUDA context\n");
        return AVERROR_UNKNOWN;
    }

    cuCtxPopCurrent(&dummy);

    s->hwdevice = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_CUDA);
    if (!s->hwdevice) {
        cuCtxDestroy(cuda_ctx);
        return AVERROR(ENOMEM);
    }

    device_ctx       = (AVHWDeviceContext*)s->hwdevice->data;
    device_ctx->free = cudaupload_ctx_free;

    device_hwctx = device_ctx->hwctx;
    device_hwctx->cuda_ctx = cuda_ctx;

    ret = av_hwdevice_ctx_init(s->hwdevice);
    if (ret < 0)
        return ret;

    return 0;
}

static av_cold void cudaupload_uninit(AVFilterContext *ctx)
{
    CudaUploadContext *s = ctx->priv;

    av_buffer_unref(&s->hwframe);
    av_buffer_unref(&s->hwdevice);
}

static int cudaupload_query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat input_pix_fmts[] = {
        AV_PIX_FMT_NV12, AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV444P,
        AV_PIX_FMT_NONE,
    };
    static const enum AVPixelFormat output_pix_fmts[] = {
        AV_PIX_FMT_CUDA, AV_PIX_FMT_NONE,
    };
    AVFilterFormats *in_fmts  = ff_make_format_list(input_pix_fmts);
    AVFilterFormats *out_fmts = ff_make_format_list(output_pix_fmts);

    ff_formats_ref(in_fmts,  &ctx->inputs[0]->out_formats);
    ff_formats_ref(out_fmts, &ctx->outputs[0]->in_formats);

    return 0;
}

static int cudaupload_config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = ctx->inputs[0];
    CudaUploadContext *s = ctx->priv;

    AVHWFramesContext *hwframe_ctx;
    int ret;

    av_buffer_unref(&s->hwframe);
    s->hwframe = av_hwframe_ctx_alloc(s->hwdevice);
    if (!s->hwframe)
        return AVERROR(ENOMEM);

    hwframe_ctx            = (AVHWFramesContext*)s->hwframe->data;
    hwframe_ctx->format    = AV_PIX_FMT_CUDA;
    hwframe_ctx->sw_format = inlink->format;
    hwframe_ctx->width     = FFALIGN(inlink->w, 16);
    hwframe_ctx->height    = FFALIGN(inlink->h, 16);

    ret = av_hwframe_ctx_init(s->hwframe);
    if (ret < 0)
        return ret;

    outlink->hw_frames_ctx = av_buffer_ref(s->hwframe);
    if (!outlink->hw_frames_ctx)
        return AVERROR(ENOMEM);

    return 0;
}

static int cudaupload_filter_frame(AVFilterLink *link, AVFrame *in)
{
    AVFilterContext   *ctx = link->dst;
    CudaUploadContext   *s = ctx->priv;

    AVFrame *out = NULL;
    int ret;

    out = av_frame_alloc();
    if (!out) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    ret = av_hwframe_get_buffer(s->hwframe, out, 0);
    if (ret < 0)
        goto fail;

    out->width  = in->width;
    out->height = in->height;

    ret = av_hwframe_transfer_data(out, in, 0);
    if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Error transferring data to the GPU\n");
        goto fail;
    }

    ret = av_frame_copy_props(out, in);
    if (ret < 0)
        goto fail;

    av_frame_free(&in);

    return ff_filter_frame(ctx->outputs[0], out);
fail:
    av_frame_free(&in);
    av_frame_free(&out);
    return ret;
}

#define OFFSET(x) offsetof(CudaUploadContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM
static const AVOption options[] = {
    { "device", "Number of the device to use", OFFSET(device_idx), AV_OPT_TYPE_INT, { .i64 = 0 }, .flags = FLAGS },
    { NULL },
};

static const AVClass cudaupload_class = {
    .class_name = "cudaupload",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVFilterPad cudaupload_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = cudaupload_filter_frame,
    },
    { NULL }
};

static const AVFilterPad cudaupload_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = cudaupload_config_output,
    },
    { NULL }
};

AVFilter ff_vf_hwupload_cuda = {
    .name        = "hwupload_cuda",
    .description = NULL_IF_CONFIG_SMALL("Upload a system memory frame to a CUDA device"),

    .init      = cudaupload_init,
    .uninit    = cudaupload_uninit,

    .query_formats = cudaupload_query_formats,

    .priv_size  = sizeof(CudaUploadContext),
    .priv_class = &cudaupload_class,

    .inputs    = cudaupload_inputs,
    .outputs   = cudaupload_outputs,
};
