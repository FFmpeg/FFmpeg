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

#include "buffer.h"
#include "common.h"
#include "hwcontext.h"
#include "hwcontext_internal.h"
#include "hwcontext_cuda.h"
#include "mem.h"
#include "pixdesc.h"
#include "pixfmt.h"

#define CUDA_FRAME_ALIGNMENT 256

typedef struct CUDAFramesContext {
    int shift_width, shift_height;
} CUDAFramesContext;

static const enum AVPixelFormat supported_formats[] = {
    AV_PIX_FMT_NV12,
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUV444P,
};

static void cuda_buffer_free(void *opaque, uint8_t *data)
{
    AVHWFramesContext *ctx = opaque;
    AVCUDADeviceContext *hwctx = ctx->device_ctx->hwctx;

    CUcontext dummy;

    cuCtxPushCurrent(hwctx->cuda_ctx);

    cuMemFree((CUdeviceptr)data);

    cuCtxPopCurrent(&dummy);
}

static AVBufferRef *cuda_pool_alloc(void *opaque, int size)
{
    AVHWFramesContext     *ctx = opaque;
    AVCUDADeviceContext *hwctx = ctx->device_ctx->hwctx;

    AVBufferRef *ret = NULL;
    CUcontext dummy = NULL;
    CUdeviceptr data;
    CUresult err;

    err = cuCtxPushCurrent(hwctx->cuda_ctx);
    if (err != CUDA_SUCCESS) {
        av_log(ctx, AV_LOG_ERROR, "Error setting current CUDA context\n");
        return NULL;
    }

    err = cuMemAlloc(&data, size);
    if (err != CUDA_SUCCESS)
        goto fail;

    ret = av_buffer_create((uint8_t*)data, size, cuda_buffer_free, ctx, 0);
    if (!ret) {
        cuMemFree(data);
        goto fail;
    }

fail:
    cuCtxPopCurrent(&dummy);
    return ret;
}

static int cuda_frames_init(AVHWFramesContext *ctx)
{
    CUDAFramesContext *priv = ctx->internal->priv;
    int aligned_width = FFALIGN(ctx->width, CUDA_FRAME_ALIGNMENT);
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(supported_formats); i++) {
        if (ctx->sw_format == supported_formats[i])
            break;
    }
    if (i == FF_ARRAY_ELEMS(supported_formats)) {
        av_log(ctx, AV_LOG_ERROR, "Pixel format '%s' is not supported\n",
               av_get_pix_fmt_name(ctx->sw_format));
        return AVERROR(ENOSYS);
    }

    av_pix_fmt_get_chroma_sub_sample(ctx->sw_format, &priv->shift_width, &priv->shift_height);

    if (!ctx->pool) {
        int size;

        switch (ctx->sw_format) {
        case AV_PIX_FMT_NV12:
        case AV_PIX_FMT_YUV420P:
            size = aligned_width * ctx->height * 3 / 2;
            break;
        case AV_PIX_FMT_YUV444P:
            size = aligned_width * ctx->height * 3;
            break;
        }

        ctx->internal->pool_internal = av_buffer_pool_init2(size, ctx, cuda_pool_alloc, NULL);
        if (!ctx->internal->pool_internal)
            return AVERROR(ENOMEM);
    }

    return 0;
}

static int cuda_get_buffer(AVHWFramesContext *ctx, AVFrame *frame)
{
    int aligned_width = FFALIGN(ctx->width, CUDA_FRAME_ALIGNMENT);

    frame->buf[0] = av_buffer_pool_get(ctx->pool);
    if (!frame->buf[0])
        return AVERROR(ENOMEM);

    switch (ctx->sw_format) {
    case AV_PIX_FMT_NV12:
        frame->data[0]     = frame->buf[0]->data;
        frame->data[1]     = frame->data[0] + aligned_width * ctx->height;
        frame->linesize[0] = aligned_width;
        frame->linesize[1] = aligned_width;
        break;
    case AV_PIX_FMT_YUV420P:
        frame->data[0]     = frame->buf[0]->data;
        frame->data[2]     = frame->data[0] + aligned_width * ctx->height;
        frame->data[1]     = frame->data[2] + aligned_width * ctx->height / 4;
        frame->linesize[0] = aligned_width;
        frame->linesize[1] = aligned_width / 2;
        frame->linesize[2] = aligned_width / 2;
        break;
    case AV_PIX_FMT_YUV444P:
        frame->data[0]     = frame->buf[0]->data;
        frame->data[1]     = frame->data[0] + aligned_width * ctx->height;
        frame->data[2]     = frame->data[1] + aligned_width * ctx->height;
        frame->linesize[0] = aligned_width;
        frame->linesize[1] = aligned_width;
        frame->linesize[2] = aligned_width;
        break;
    default:
        av_frame_unref(frame);
        return AVERROR_BUG;
    }

    frame->format = AV_PIX_FMT_CUDA;
    frame->width  = ctx->width;
    frame->height = ctx->height;

    return 0;
}

static int cuda_transfer_get_formats(AVHWFramesContext *ctx,
                                     enum AVHWFrameTransferDirection dir,
                                     enum AVPixelFormat **formats)
{
    enum AVPixelFormat *fmts;

    fmts = av_malloc_array(2, sizeof(*fmts));
    if (!fmts)
        return AVERROR(ENOMEM);

    fmts[0] = ctx->sw_format;
    fmts[1] = AV_PIX_FMT_NONE;

    *formats = fmts;

    return 0;
}

static int cuda_transfer_data_from(AVHWFramesContext *ctx, AVFrame *dst,
                                   const AVFrame *src)
{
    CUDAFramesContext           *priv = ctx->internal->priv;
    AVCUDADeviceContext *device_hwctx = ctx->device_ctx->hwctx;

    CUcontext dummy;
    CUresult err;
    int i;

    err = cuCtxPushCurrent(device_hwctx->cuda_ctx);
    if (err != CUDA_SUCCESS)
        return AVERROR_UNKNOWN;

    for (i = 0; i < FF_ARRAY_ELEMS(src->data) && src->data[i]; i++) {
        CUDA_MEMCPY2D cpy = {
            .srcMemoryType = CU_MEMORYTYPE_DEVICE,
            .dstMemoryType = CU_MEMORYTYPE_HOST,
            .srcDevice     = (CUdeviceptr)src->data[i],
            .dstHost       = dst->data[i],
            .srcPitch      = src->linesize[i],
            .dstPitch      = dst->linesize[i],
            .WidthInBytes  = FFMIN(src->linesize[i], dst->linesize[i]),
            .Height        = src->height >> (i ? priv->shift_height : 0),
        };

        err = cuMemcpy2D(&cpy);
        if (err != CUDA_SUCCESS) {
            av_log(ctx, AV_LOG_ERROR, "Error transferring the data from the CUDA frame\n");
            return AVERROR_UNKNOWN;
        }
    }

    cuCtxPopCurrent(&dummy);

    return 0;
}

static int cuda_transfer_data_to(AVHWFramesContext *ctx, AVFrame *dst,
                                 const AVFrame *src)
{
    CUDAFramesContext           *priv = ctx->internal->priv;
    AVCUDADeviceContext *device_hwctx = ctx->device_ctx->hwctx;

    CUcontext dummy;
    CUresult err;
    int i;

    err = cuCtxPushCurrent(device_hwctx->cuda_ctx);
    if (err != CUDA_SUCCESS)
        return AVERROR_UNKNOWN;

    for (i = 0; i < FF_ARRAY_ELEMS(src->data) && src->data[i]; i++) {
        CUDA_MEMCPY2D cpy = {
            .srcMemoryType = CU_MEMORYTYPE_HOST,
            .dstMemoryType = CU_MEMORYTYPE_DEVICE,
            .srcHost       = src->data[i],
            .dstDevice     = (CUdeviceptr)dst->data[i],
            .srcPitch      = src->linesize[i],
            .dstPitch      = dst->linesize[i],
            .WidthInBytes  = FFMIN(src->linesize[i], dst->linesize[i]),
            .Height        = src->height >> (i ? priv->shift_height : 0),
        };

        err = cuMemcpy2D(&cpy);
        if (err != CUDA_SUCCESS) {
            av_log(ctx, AV_LOG_ERROR, "Error transferring the data from the CUDA frame\n");
            return AVERROR_UNKNOWN;
        }
    }

    cuCtxPopCurrent(&dummy);

    return 0;
}

static void cuda_device_free(AVHWDeviceContext *ctx)
{
    AVCUDADeviceContext *hwctx = ctx->hwctx;
    cuCtxDestroy(hwctx->cuda_ctx);
}

static int cuda_device_create(AVHWDeviceContext *ctx, const char *device,
                              AVDictionary *opts, int flags)
{
    AVCUDADeviceContext *hwctx = ctx->hwctx;
    CUdevice cu_device;
    CUcontext dummy;
    CUresult err;
    int device_idx = 0;

    if (device)
        device_idx = strtol(device, NULL, 0);

    err = cuInit(0);
    if (err != CUDA_SUCCESS) {
        av_log(ctx, AV_LOG_ERROR, "Could not initialize the CUDA driver API\n");
        return AVERROR_UNKNOWN;
    }

    err = cuDeviceGet(&cu_device, device_idx);
    if (err != CUDA_SUCCESS) {
        av_log(ctx, AV_LOG_ERROR, "Could not get the device number %d\n", device_idx);
        return AVERROR_UNKNOWN;
    }

    err = cuCtxCreate(&hwctx->cuda_ctx, CU_CTX_SCHED_BLOCKING_SYNC, cu_device);
    if (err != CUDA_SUCCESS) {
        av_log(ctx, AV_LOG_ERROR, "Error creating a CUDA context\n");
        return AVERROR_UNKNOWN;
    }

    cuCtxPopCurrent(&dummy);

    ctx->free = cuda_device_free;

    return 0;
}

const HWContextType ff_hwcontext_type_cuda = {
    .type                 = AV_HWDEVICE_TYPE_CUDA,
    .name                 = "CUDA",

    .device_hwctx_size    = sizeof(AVCUDADeviceContext),
    .frames_priv_size     = sizeof(CUDAFramesContext),

    .device_create        = cuda_device_create,
    .frames_init          = cuda_frames_init,
    .frames_get_buffer    = cuda_get_buffer,
    .transfer_get_formats = cuda_transfer_get_formats,
    .transfer_data_to     = cuda_transfer_data_to,
    .transfer_data_from   = cuda_transfer_data_from,

    .pix_fmts             = (const enum AVPixelFormat[]){ AV_PIX_FMT_CUDA, AV_PIX_FMT_NONE },
};
