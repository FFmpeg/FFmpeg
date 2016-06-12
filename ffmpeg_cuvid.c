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

#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_cuda.h"

#include "ffmpeg.h"

#include <cuda.h>
#include <nvcuvid.h>

typedef struct CUVIDContext {
    AVBufferRef *hw_frames_ctx;
} CUVIDContext;

static void cuvid_uninit(AVCodecContext *avctx)
{
    InputStream  *ist = avctx->opaque;
    CUVIDContext *ctx = ist->hwaccel_ctx;

    if (ctx) {
        av_buffer_unref(&ctx->hw_frames_ctx);
        av_freep(&ctx);
    }

    av_buffer_unref(&ist->hw_frames_ctx);

    ist->hwaccel_ctx = 0;
    ist->hwaccel_uninit = 0;
}

int cuvid_init(AVCodecContext *avctx)
{
    InputStream  *ist = avctx->opaque;
    CUVIDContext *ctx = ist->hwaccel_ctx;

    av_log(NULL, AV_LOG_TRACE, "Initializing cuvid hwaccel\n");

    if (!ctx) {
        av_log(NULL, AV_LOG_ERROR, "CUVID transcoding is not initialized. "
               "-hwaccel cuvid should only be used for one-to-one CUVID transcoding "
               "with no (software) filters.\n");
        return AVERROR(EINVAL);
    }

    return 0;
}

static void cuvid_ctx_free(AVHWDeviceContext *ctx)
{
    AVCUDADeviceContext *hwctx = ctx->hwctx;
    cuCtxDestroy(hwctx->cuda_ctx);
}

int cuvid_transcode_init(OutputStream *ost)
{
    InputStream *ist;
    const enum AVPixelFormat *pix_fmt;
    AVCUDADeviceContext *device_hwctx;
    AVHWDeviceContext *device_ctx;
    AVHWFramesContext *hwframe_ctx;
    CUVIDContext *ctx = NULL;
    CUdevice device;
    CUcontext cuda_ctx = NULL;
    CUcontext dummy;
    CUresult err;
    int ret = 0;

    av_log(NULL, AV_LOG_TRACE, "Initializing cuvid transcoding\n");

    if (ost->source_index < 0)
        return 0;

    ist = input_streams[ost->source_index];

    /* check if the encoder supports CUVID */
    if (!ost->enc->pix_fmts)
        goto cancel;
    for (pix_fmt = ost->enc->pix_fmts; *pix_fmt != AV_PIX_FMT_NONE; pix_fmt++)
        if (*pix_fmt == AV_PIX_FMT_CUDA)
            break;
    if (*pix_fmt == AV_PIX_FMT_NONE)
        goto cancel;

    /* check if the decoder supports CUVID */
    if (ist->hwaccel_id != HWACCEL_CUVID || !ist->dec || !ist->dec->pix_fmts)
        goto cancel;
    for (pix_fmt = ist->dec->pix_fmts; *pix_fmt != AV_PIX_FMT_NONE; pix_fmt++)
        if (*pix_fmt == AV_PIX_FMT_CUDA)
            break;
    if (*pix_fmt == AV_PIX_FMT_NONE)
        goto cancel;

    av_log(NULL, AV_LOG_VERBOSE, "Setting up CUVID transcoding\n");

    if (ist->hwaccel_ctx) {
        ctx = ist->hwaccel_ctx;
    } else {
        ctx = av_mallocz(sizeof(*ctx));
        if (!ctx) {
            ret = AVERROR(ENOMEM);
            goto error;
        }
    }

    if (!hw_device_ctx) {
        hw_device_ctx = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_CUDA);
        if (!hw_device_ctx) {
            av_log(NULL, AV_LOG_ERROR, "av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_CUDA) failed\n");
            ret = AVERROR(ENOMEM);
            goto error;
        }

        err = cuInit(0);
        if (err != CUDA_SUCCESS) {
            av_log(NULL, AV_LOG_ERROR, "Could not initialize the CUDA driver API\n");
            ret = AVERROR_UNKNOWN;
            goto error;
        }

        err = cuDeviceGet(&device, 0); ///TODO: Make device index configurable
        if (err != CUDA_SUCCESS) {
            av_log(NULL, AV_LOG_ERROR, "Could not get the device number %d\n", 0);
            ret = AVERROR_UNKNOWN;
            goto error;
        }

        err = cuCtxCreate(&cuda_ctx, CU_CTX_SCHED_BLOCKING_SYNC, device);
        if (err != CUDA_SUCCESS) {
            av_log(NULL, AV_LOG_ERROR, "Error creating a CUDA context\n");
            ret = AVERROR_UNKNOWN;
            goto error;
        }

        device_ctx = (AVHWDeviceContext*)hw_device_ctx->data;
        device_ctx->free = cuvid_ctx_free;

        device_hwctx = device_ctx->hwctx;
        device_hwctx->cuda_ctx = cuda_ctx;

        err = cuCtxPopCurrent(&dummy);
        if (err != CUDA_SUCCESS) {
            av_log(NULL, AV_LOG_ERROR, "cuCtxPopCurrent failed\n");
            ret = AVERROR_UNKNOWN;
            goto error;
        }

        ret = av_hwdevice_ctx_init(hw_device_ctx);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "av_hwdevice_ctx_init failed\n");
            goto error;
        }
    } else {
        device_ctx = (AVHWDeviceContext*)hw_device_ctx->data;
        device_hwctx = device_ctx->hwctx;
        cuda_ctx = device_hwctx->cuda_ctx;
    }

    if (device_ctx->type != AV_HWDEVICE_TYPE_CUDA) {
        av_log(NULL, AV_LOG_ERROR, "Hardware device context is already initialized for a diffrent hwaccel.\n");
        ret = AVERROR(EINVAL);
        goto error;
    }

    if (!ctx->hw_frames_ctx) {
        ctx->hw_frames_ctx = av_hwframe_ctx_alloc(hw_device_ctx);
        if (!ctx->hw_frames_ctx) {
            av_log(NULL, AV_LOG_ERROR, "av_hwframe_ctx_alloc failed\n");
            ret = AVERROR(ENOMEM);
            goto error;
        }
    }

    /* This is a bit hacky, av_hwframe_ctx_init is called by the cuvid decoder
     * once it has probed the neccesary format information. But as filters/nvenc
     * need to know the format/sw_format, set them here so they are happy.
     * This is fine as long as CUVID doesn't add another supported pix_fmt.
     */
    hwframe_ctx = (AVHWFramesContext*)ctx->hw_frames_ctx->data;
    hwframe_ctx->format = AV_PIX_FMT_CUDA;
    hwframe_ctx->sw_format = AV_PIX_FMT_NV12;

    ost->hwaccel_ctx = ctx;
    ost->enc_ctx->hw_frames_ctx = av_buffer_ref(ctx->hw_frames_ctx);
    ost->enc_ctx->pix_fmt = AV_PIX_FMT_CUDA;

    if (!ost->enc_ctx->hw_frames_ctx) {
        av_log(NULL, AV_LOG_ERROR, "av_buffer_ref failed\n");
        ret = AVERROR(ENOMEM);
        goto error;
    }

    if (!ist->hwaccel_ctx) {
        ist->hwaccel_ctx = ctx;
        ist->hw_frames_ctx = av_buffer_ref(ctx->hw_frames_ctx);
        ist->dec_ctx->hw_frames_ctx = av_buffer_ref(ctx->hw_frames_ctx);
        ist->dec_ctx->pix_fmt = AV_PIX_FMT_CUDA;
        ist->resample_pix_fmt = AV_PIX_FMT_CUDA;

        ist->hwaccel_uninit = cuvid_uninit;

        if (!ist->hw_frames_ctx || !ist->dec_ctx->hw_frames_ctx) {
            av_log(NULL, AV_LOG_ERROR, "av_buffer_ref failed\n");
            ret = AVERROR(ENOMEM);
            goto error;
        }
    }

    return 0;

error:
    av_freep(&ctx);
    return ret;

cancel:
    if (ist->hwaccel_id == HWACCEL_CUVID) {
        av_log(NULL, AV_LOG_ERROR, "CUVID hwaccel requested, but impossible to achive.\n");
        return AVERROR(EINVAL);
    }

    return 0;
}
