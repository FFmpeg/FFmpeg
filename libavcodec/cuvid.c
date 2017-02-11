/*
 * HW decode acceleration through CUVID
 *
 * Copyright (c) 2016 Anton Khirnov
 *
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
 * License along with Libav; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <cuda.h>
#include <cuviddec.h>

#include "config.h"

#include "libavutil/common.h"
#include "libavutil/error.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_cuda.h"
#include "libavutil/pixdesc.h"
#include "libavutil/pixfmt.h"

#include "avcodec.h"
#include "decode.h"
#include "cuvid.h"
#include "internal.h"

typedef struct CUVIDDecoder {
    CUvideodecoder decoder;

    AVBufferRef *hw_device_ref;
    CUcontext    cuda_ctx;
} CUVIDDecoder;

typedef struct CUVIDFramePool {
    unsigned int dpb_size;
    unsigned int nb_allocated;
} CUVIDFramePool;

static int map_avcodec_id(enum AVCodecID id)
{
    switch (id) {
    case AV_CODEC_ID_H264: return cudaVideoCodec_H264;
    case AV_CODEC_ID_HEVC: return cudaVideoCodec_HEVC;
    }
    return -1;
}

static int map_chroma_format(enum AVPixelFormat pix_fmt)
{
    int shift_h = 0, shift_v = 0;

    av_pix_fmt_get_chroma_sub_sample(pix_fmt, &shift_h, &shift_v);

    if (shift_h == 1 && shift_v == 1)
        return cudaVideoChromaFormat_420;
    else if (shift_h == 1 && shift_v == 0)
        return cudaVideoChromaFormat_422;
    else if (shift_h == 0 && shift_v == 0)
        return cudaVideoChromaFormat_444;

    return -1;
}

static void cuvid_decoder_free(void *opaque, uint8_t *data)
{
    CUVIDDecoder *decoder = (CUVIDDecoder*)data;

    if (decoder->decoder)
        cuvidDestroyDecoder(decoder->decoder);

    av_buffer_unref(&decoder->hw_device_ref);

    av_freep(&decoder);
}

static int cuvid_decoder_create(AVBufferRef **out, AVBufferRef *hw_device_ref,
                                CUVIDDECODECREATEINFO *params, void *logctx)
{
    AVHWDeviceContext  *hw_device_ctx = (AVHWDeviceContext*)hw_device_ref->data;
    AVCUDADeviceContext *device_hwctx = hw_device_ctx->hwctx;

    AVBufferRef *decoder_ref;
    CUVIDDecoder *decoder;

    CUcontext dummy;
    CUresult err;
    int ret;

    decoder = av_mallocz(sizeof(*decoder));
    if (!decoder)
        return AVERROR(ENOMEM);

    decoder_ref = av_buffer_create((uint8_t*)decoder, sizeof(*decoder),
                                   cuvid_decoder_free, NULL, AV_BUFFER_FLAG_READONLY);
    if (!decoder_ref) {
        av_freep(&decoder);
        return AVERROR(ENOMEM);
    }

    decoder->hw_device_ref = av_buffer_ref(hw_device_ref);
    if (!decoder->hw_device_ref) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    decoder->cuda_ctx = device_hwctx->cuda_ctx;

    err = cuCtxPushCurrent(decoder->cuda_ctx);
    if (err != CUDA_SUCCESS) {
        ret = AVERROR_UNKNOWN;
        goto fail;
    }

    err = cuvidCreateDecoder(&decoder->decoder, params);

    cuCtxPopCurrent(&dummy);

    if (err != CUDA_SUCCESS) {
        av_log(logctx, AV_LOG_ERROR, "Error creating a CUVID decoder: %d\n", err);
        ret = AVERROR_UNKNOWN;
        goto fail;
    }

    *out = decoder_ref;

    return 0;
fail:
    av_buffer_unref(&decoder_ref);
    return ret;
}

static AVBufferRef *cuvid_decoder_frame_alloc(void *opaque, int size)
{
    CUVIDFramePool *pool = opaque;
    AVBufferRef *ret;

    if (pool->nb_allocated >= pool->dpb_size)
        return NULL;

    ret = av_buffer_alloc(sizeof(unsigned int));
    if (!ret)
        return NULL;

    *(unsigned int*)ret->data = pool->nb_allocated++;

    return ret;
}

int ff_cuvid_decode_uninit(AVCodecContext *avctx)
{
    CUVIDContext *ctx = avctx->internal->hwaccel_priv_data;

    av_freep(&ctx->bitstream);
    ctx->bitstream_len       = 0;
    ctx->bitstream_allocated = 0;

    av_freep(&ctx->slice_offsets);
    ctx->nb_slices               = 0;
    ctx->slice_offsets_allocated = 0;

    av_buffer_unref(&ctx->decoder_ref);
    av_buffer_pool_uninit(&ctx->decoder_pool);

    return 0;
}

int ff_cuvid_decode_init(AVCodecContext *avctx, unsigned int dpb_size)
{
    CUVIDContext *ctx = avctx->internal->hwaccel_priv_data;

    CUVIDFramePool      *pool;
    AVHWFramesContext   *frames_ctx;
    const AVPixFmtDescriptor *sw_desc;

    CUVIDDECODECREATEINFO params = { 0 };

    int cuvid_codec_type, cuvid_chroma_format;
    int ret = 0;

    sw_desc = av_pix_fmt_desc_get(avctx->sw_pix_fmt);
    if (!sw_desc)
        return AVERROR_BUG;

    cuvid_codec_type = map_avcodec_id(avctx->codec_id);
    if (cuvid_codec_type < 0) {
        av_log(avctx, AV_LOG_ERROR, "Unsupported codec ID\n");
        return AVERROR_BUG;
    }

    cuvid_chroma_format = map_chroma_format(avctx->sw_pix_fmt);
    if (cuvid_chroma_format < 0) {
        av_log(avctx, AV_LOG_ERROR, "Unsupported chroma format\n");
        return AVERROR(ENOSYS);
    }

    if (avctx->thread_type & FF_THREAD_FRAME)
        dpb_size += avctx->thread_count;

    if (!avctx->hw_frames_ctx) {
        AVHWFramesContext *frames_ctx;

        if (!avctx->hw_device_ctx) {
            av_log(avctx, AV_LOG_ERROR, "A hardware device or frames context "
                   "is required for CUVID decoding.\n");
            return AVERROR(EINVAL);
        }

        avctx->hw_frames_ctx = av_hwframe_ctx_alloc(avctx->hw_device_ctx);
        if (!avctx->hw_frames_ctx)
            return AVERROR(ENOMEM);
        frames_ctx = (AVHWFramesContext*)avctx->hw_frames_ctx->data;

        frames_ctx->format            = AV_PIX_FMT_CUDA;
        frames_ctx->width             = avctx->coded_width;
        frames_ctx->height            = avctx->coded_height;
        frames_ctx->sw_format         = AV_PIX_FMT_NV12;
        frames_ctx->sw_format         = sw_desc->comp[0].depth > 8 && HAVE_CUVIDDECODECREATEINFO_BITDEPTHMINUS8 ?
                                        AV_PIX_FMT_P010 : AV_PIX_FMT_NV12;
        frames_ctx->initial_pool_size = dpb_size;

        ret = av_hwframe_ctx_init(avctx->hw_frames_ctx);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "Error initializing internal frames context\n");
            return ret;
        }
    }
    frames_ctx = (AVHWFramesContext*)avctx->hw_frames_ctx->data;

    params.ulWidth             = avctx->coded_width;
    params.ulHeight            = avctx->coded_height;
    params.ulTargetWidth       = avctx->coded_width;
    params.ulTargetHeight      = avctx->coded_height;
#if HAVE_CUVIDDECODECREATEINFO_BITDEPTHMINUS8
    params.bitDepthMinus8      = sw_desc->comp[0].depth - 8;
    params.OutputFormat        = params.bitDepthMinus8 ?
                                 cudaVideoSurfaceFormat_P016 : cudaVideoSurfaceFormat_NV12;
#else
    params.OutputFormat        = cudaVideoSurfaceFormat_NV12;
#endif
    params.CodecType           = cuvid_codec_type;
    params.ChromaFormat        = cuvid_chroma_format;
    params.ulNumDecodeSurfaces = dpb_size;
    params.ulNumOutputSurfaces = 1;

    ret = cuvid_decoder_create(&ctx->decoder_ref, frames_ctx->device_ref, &params, avctx);
    if (ret < 0)
        return ret;

    pool = av_mallocz(sizeof(*pool));
    if (!pool) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    pool->dpb_size = dpb_size;

    ctx->decoder_pool = av_buffer_pool_init2(sizeof(int), pool,
                                             cuvid_decoder_frame_alloc, av_free);
    if (!ctx->decoder_pool) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    return 0;
fail:
    ff_cuvid_decode_uninit(avctx);
    return ret;
}

static void cuvid_fdd_priv_free(void *priv)
{
    CUVIDFrame *cf = priv;

    if (!cf)
        return;

    av_buffer_unref(&cf->idx_ref);
    av_buffer_unref(&cf->decoder_ref);

    av_freep(&priv);
}

static int cuvid_retrieve_data(void *logctx, AVFrame *frame)
{
    FrameDecodeData  *fdd = (FrameDecodeData*)frame->opaque_ref->data;
    CUVIDFrame        *cf = (CUVIDFrame*)fdd->hwaccel_priv;
    CUVIDDecoder *decoder = (CUVIDDecoder*)cf->decoder_ref->data;

    CUVIDPROCPARAMS vpp = { .progressive_frame = 1 };

    CUresult err;
    CUcontext dummy;
    CUdeviceptr devptr;

    unsigned int pitch, i;
    unsigned int offset = 0;
    int ret = 0;

    err = cuCtxPushCurrent(decoder->cuda_ctx);
    if (err != CUDA_SUCCESS)
        return AVERROR_UNKNOWN;

    err = cuvidMapVideoFrame(decoder->decoder, cf->idx, &devptr, &pitch, &vpp);
    if (err != CUDA_SUCCESS) {
        av_log(logctx, AV_LOG_ERROR, "Error mapping a picture with CUVID: %d\n",
               err);
        ret = AVERROR_UNKNOWN;
        goto finish;
    }

    for (i = 0; frame->data[i]; i++) {
        CUDA_MEMCPY2D cpy = {
            .srcMemoryType = CU_MEMORYTYPE_DEVICE,
            .dstMemoryType = CU_MEMORYTYPE_DEVICE,
            .srcDevice     = devptr,
            .dstDevice     = (CUdeviceptr)frame->data[i],
            .srcPitch      = pitch,
            .dstPitch      = frame->linesize[i],
            .srcY          = offset,
            .WidthInBytes  = FFMIN(pitch, frame->linesize[i]),
            .Height        = frame->height >> (i ? 1 : 0),
        };

        err = cuMemcpy2D(&cpy);
        if (err != CUDA_SUCCESS) {
            av_log(logctx, AV_LOG_ERROR, "Error copying decoded frame: %d\n",
                   err);
            ret = AVERROR_UNKNOWN;
            goto copy_fail;
        }

        offset += cpy.Height;
    }

copy_fail:
    cuvidUnmapVideoFrame(decoder->decoder, devptr);

finish:
    cuCtxPopCurrent(&dummy);
    return ret;
}

int ff_cuvid_start_frame(AVCodecContext *avctx, AVFrame *frame)
{
    CUVIDContext *ctx = avctx->internal->hwaccel_priv_data;
    FrameDecodeData *fdd = (FrameDecodeData*)frame->opaque_ref->data;
    CUVIDFrame *cf = NULL;
    int ret;

    ctx->bitstream_len = 0;
    ctx->nb_slices     = 0;

    if (fdd->hwaccel_priv)
        return 0;

    cf = av_mallocz(sizeof(*cf));
    if (!cf)
        return AVERROR(ENOMEM);

    cf->decoder_ref = av_buffer_ref(ctx->decoder_ref);
    if (!cf->decoder_ref)
        goto fail;

    cf->idx_ref = av_buffer_pool_get(ctx->decoder_pool);
    if (!cf->idx_ref) {
        av_log(avctx, AV_LOG_ERROR, "No decoder surfaces left\n");
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    cf->idx = *(unsigned int*)cf->idx_ref->data;

    fdd->hwaccel_priv      = cf;
    fdd->hwaccel_priv_free = cuvid_fdd_priv_free;
    fdd->post_process      = cuvid_retrieve_data;

    return 0;
fail:
    cuvid_fdd_priv_free(cf);
    return ret;

}

int ff_cuvid_end_frame(AVCodecContext *avctx)
{
    CUVIDContext     *ctx = avctx->internal->hwaccel_priv_data;
    CUVIDDecoder *decoder = (CUVIDDecoder*)ctx->decoder_ref->data;
    CUVIDPICPARAMS    *pp = &ctx->pic_params;

    CUresult err;
    CUcontext dummy;

    int ret = 0;

    pp->nBitstreamDataLen = ctx->bitstream_len;
    pp->pBitstreamData    = ctx->bitstream;
    pp->nNumSlices        = ctx->nb_slices;
    pp->pSliceDataOffsets = ctx->slice_offsets;

    err = cuCtxPushCurrent(decoder->cuda_ctx);
    if (err != CUDA_SUCCESS)
        return AVERROR_UNKNOWN;

    err = cuvidDecodePicture(decoder->decoder, &ctx->pic_params);
    if (err != CUDA_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Error decoding a picture with CUVID: %d\n",
               err);
        ret = AVERROR_UNKNOWN;
        goto finish;
    }

finish:
    cuCtxPopCurrent(&dummy);

    return ret;
}
