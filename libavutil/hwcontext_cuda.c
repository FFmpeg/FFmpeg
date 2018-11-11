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
#include "hwcontext_cuda_internal.h"
#include "cuda_check.h"
#include "mem.h"
#include "pixdesc.h"
#include "pixfmt.h"
#include "imgutils.h"

#define CUDA_FRAME_ALIGNMENT 256

typedef struct CUDAFramesContext {
    int shift_width, shift_height;
} CUDAFramesContext;

static const enum AVPixelFormat supported_formats[] = {
    AV_PIX_FMT_NV12,
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_P010,
    AV_PIX_FMT_P016,
    AV_PIX_FMT_YUV444P16,
    AV_PIX_FMT_0RGB32,
    AV_PIX_FMT_0BGR32,
};

#define CHECK_CU(x) FF_CUDA_CHECK_DL(device_ctx, cu, x)

static int cuda_frames_get_constraints(AVHWDeviceContext *ctx,
                                       const void *hwconfig,
                                       AVHWFramesConstraints *constraints)
{
    int i;

    constraints->valid_sw_formats = av_malloc_array(FF_ARRAY_ELEMS(supported_formats) + 1,
                                                    sizeof(*constraints->valid_sw_formats));
    if (!constraints->valid_sw_formats)
        return AVERROR(ENOMEM);

    for (i = 0; i < FF_ARRAY_ELEMS(supported_formats); i++)
        constraints->valid_sw_formats[i] = supported_formats[i];
    constraints->valid_sw_formats[FF_ARRAY_ELEMS(supported_formats)] = AV_PIX_FMT_NONE;

    constraints->valid_hw_formats = av_malloc_array(2, sizeof(*constraints->valid_hw_formats));
    if (!constraints->valid_hw_formats)
        return AVERROR(ENOMEM);

    constraints->valid_hw_formats[0] = AV_PIX_FMT_CUDA;
    constraints->valid_hw_formats[1] = AV_PIX_FMT_NONE;

    return 0;
}

static void cuda_buffer_free(void *opaque, uint8_t *data)
{
    AVHWFramesContext        *ctx = opaque;
    AVHWDeviceContext *device_ctx = ctx->device_ctx;
    AVCUDADeviceContext    *hwctx = device_ctx->hwctx;
    CudaFunctions             *cu = hwctx->internal->cuda_dl;

    CUcontext dummy;

    CHECK_CU(cu->cuCtxPushCurrent(hwctx->cuda_ctx));

    CHECK_CU(cu->cuMemFree((CUdeviceptr)data));

    CHECK_CU(cu->cuCtxPopCurrent(&dummy));
}

static AVBufferRef *cuda_pool_alloc(void *opaque, int size)
{
    AVHWFramesContext        *ctx = opaque;
    AVHWDeviceContext *device_ctx = ctx->device_ctx;
    AVCUDADeviceContext    *hwctx = device_ctx->hwctx;
    CudaFunctions             *cu = hwctx->internal->cuda_dl;

    AVBufferRef *ret = NULL;
    CUcontext dummy = NULL;
    CUdeviceptr data;
    int err;

    err = CHECK_CU(cu->cuCtxPushCurrent(hwctx->cuda_ctx));
    if (err < 0)
        return NULL;

    err = CHECK_CU(cu->cuMemAlloc(&data, size));
    if (err < 0)
        goto fail;

    ret = av_buffer_create((uint8_t*)data, size, cuda_buffer_free, ctx, 0);
    if (!ret) {
        CHECK_CU(cu->cuMemFree(data));
        goto fail;
    }

fail:
    CHECK_CU(cu->cuCtxPopCurrent(&dummy));
    return ret;
}

static int cuda_frames_init(AVHWFramesContext *ctx)
{
    CUDAFramesContext *priv = ctx->internal->priv;
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
        int size = av_image_get_buffer_size(ctx->sw_format, ctx->width, ctx->height, CUDA_FRAME_ALIGNMENT);
        if (size < 0)
            return size;

        ctx->internal->pool_internal = av_buffer_pool_init2(size, ctx, cuda_pool_alloc, NULL);
        if (!ctx->internal->pool_internal)
            return AVERROR(ENOMEM);
    }

    return 0;
}

static int cuda_get_buffer(AVHWFramesContext *ctx, AVFrame *frame)
{
    int res;

    frame->buf[0] = av_buffer_pool_get(ctx->pool);
    if (!frame->buf[0])
        return AVERROR(ENOMEM);

    res = av_image_fill_arrays(frame->data, frame->linesize, frame->buf[0]->data,
                               ctx->sw_format, ctx->width, ctx->height, CUDA_FRAME_ALIGNMENT);
    if (res < 0)
        return res;

    // YUV420P is a special case.
    // Nvenc expects the U/V planes in swapped order from how ffmpeg expects them, also chroma is half-aligned
    if (ctx->sw_format == AV_PIX_FMT_YUV420P) {
        frame->linesize[1] = frame->linesize[2] = frame->linesize[0] / 2;
        frame->data[2]     = frame->data[1];
        frame->data[1]     = frame->data[2] + frame->linesize[2] * ctx->height / 2;
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
    CUDAFramesContext       *priv = ctx->internal->priv;
    AVHWDeviceContext *device_ctx = ctx->device_ctx;
    AVCUDADeviceContext    *hwctx = device_ctx->hwctx;
    CudaFunctions             *cu = hwctx->internal->cuda_dl;

    CUcontext dummy;
    int i, ret;

    ret = CHECK_CU(cu->cuCtxPushCurrent(hwctx->cuda_ctx));
    if (ret < 0)
        return ret;

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

        ret = CHECK_CU(cu->cuMemcpy2DAsync(&cpy, hwctx->stream));
        if (ret < 0)
            goto exit;
    }

    ret = CHECK_CU(cu->cuStreamSynchronize(hwctx->stream));
    if (ret < 0)
        goto exit;

exit:
    CHECK_CU(cu->cuCtxPopCurrent(&dummy));

    return 0;
}

static int cuda_transfer_data_to(AVHWFramesContext *ctx, AVFrame *dst,
                                 const AVFrame *src)
{
    CUDAFramesContext       *priv = ctx->internal->priv;
    AVHWDeviceContext *device_ctx = ctx->device_ctx;
    AVCUDADeviceContext    *hwctx = device_ctx->hwctx;
    CudaFunctions             *cu = hwctx->internal->cuda_dl;

    CUcontext dummy;
    int i, ret;

    ret = CHECK_CU(cu->cuCtxPushCurrent(hwctx->cuda_ctx));
    if (ret < 0)
        return ret;

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

        ret = CHECK_CU(cu->cuMemcpy2DAsync(&cpy, hwctx->stream));
        if (ret < 0)
            goto exit;
    }

    ret = CHECK_CU(cu->cuStreamSynchronize(hwctx->stream));
    if (ret < 0)
        goto exit;

exit:
    CHECK_CU(cu->cuCtxPopCurrent(&dummy));

    return 0;
}

static void cuda_device_uninit(AVHWDeviceContext *device_ctx)
{
    AVCUDADeviceContext *hwctx = device_ctx->hwctx;

    if (hwctx->internal) {
        CudaFunctions *cu = hwctx->internal->cuda_dl;
        if (hwctx->internal->is_allocated && hwctx->cuda_ctx) {
            CHECK_CU(cu->cuCtxDestroy(hwctx->cuda_ctx));
            hwctx->cuda_ctx = NULL;
        }
        cuda_free_functions(&hwctx->internal->cuda_dl);
    }

    av_freep(&hwctx->internal);
}

static int cuda_device_init(AVHWDeviceContext *ctx)
{
    AVCUDADeviceContext *hwctx = ctx->hwctx;
    int ret;

    if (!hwctx->internal) {
        hwctx->internal = av_mallocz(sizeof(*hwctx->internal));
        if (!hwctx->internal)
            return AVERROR(ENOMEM);
    }

    if (!hwctx->internal->cuda_dl) {
        ret = cuda_load_functions(&hwctx->internal->cuda_dl, ctx);
        if (ret < 0) {
            av_log(ctx, AV_LOG_ERROR, "Could not dynamically load CUDA\n");
            goto error;
        }
    }

    return 0;

error:
    cuda_device_uninit(ctx);
    return ret;
}

static int cuda_device_create(AVHWDeviceContext *device_ctx,
                              const char *device,
                              AVDictionary *opts, int flags)
{
    AVCUDADeviceContext *hwctx = device_ctx->hwctx;
    CudaFunctions *cu;
    CUdevice cu_device;
    CUcontext dummy;
    int ret, device_idx = 0;

    if (device)
        device_idx = strtol(device, NULL, 0);

    if (cuda_device_init(device_ctx) < 0)
        goto error;

    cu = hwctx->internal->cuda_dl;

    ret = CHECK_CU(cu->cuInit(0));
    if (ret < 0)
        goto error;

    ret = CHECK_CU(cu->cuDeviceGet(&cu_device, device_idx));
    if (ret < 0)
        goto error;

    ret = CHECK_CU(cu->cuCtxCreate(&hwctx->cuda_ctx, CU_CTX_SCHED_BLOCKING_SYNC, cu_device));
    if (ret < 0)
        goto error;

    // Setting stream to NULL will make functions automatically use the default CUstream
    hwctx->stream = NULL;

    CHECK_CU(cu->cuCtxPopCurrent(&dummy));

    hwctx->internal->is_allocated = 1;

    return 0;

error:
    cuda_device_uninit(device_ctx);
    return AVERROR_UNKNOWN;
}

const HWContextType ff_hwcontext_type_cuda = {
    .type                 = AV_HWDEVICE_TYPE_CUDA,
    .name                 = "CUDA",

    .device_hwctx_size    = sizeof(AVCUDADeviceContext),
    .frames_priv_size     = sizeof(CUDAFramesContext),

    .device_create        = cuda_device_create,
    .device_init          = cuda_device_init,
    .device_uninit        = cuda_device_uninit,
    .frames_get_constraints = cuda_frames_get_constraints,
    .frames_init          = cuda_frames_init,
    .frames_get_buffer    = cuda_get_buffer,
    .transfer_get_formats = cuda_transfer_get_formats,
    .transfer_data_to     = cuda_transfer_data_to,
    .transfer_data_from   = cuda_transfer_data_from,

    .pix_fmts             = (const enum AVPixelFormat[]){ AV_PIX_FMT_CUDA, AV_PIX_FMT_NONE },
};
