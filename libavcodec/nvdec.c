/*
 * HW decode acceleration through NVDEC
 *
 * Copyright (c) 2016 Anton Khirnov
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

#include "config.h"
#include "config_components.h"

#include "libavutil/common.h"
#include "libavutil/error.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_cuda_internal.h"
#include "libavutil/cuda_check.h"
#include "libavutil/mem.h"
#include "libavutil/pixdesc.h"
#include "libavutil/pixfmt.h"

#include "avcodec.h"
#include "decode.h"
#include "nvdec.h"
#include "internal.h"
#include "refstruct.h"

#if !NVDECAPI_CHECK_VERSION(9, 0)
#define cudaVideoSurfaceFormat_YUV444 2
#define cudaVideoSurfaceFormat_YUV444_16Bit 3
#endif

typedef struct NVDECDecoder {
    CUvideodecoder decoder;

    AVBufferRef *hw_device_ref;
    AVBufferRef *real_hw_frames_ref;
    CUcontext    cuda_ctx;
    CUstream     stream;

    CudaFunctions *cudl;
    CuvidFunctions *cvdl;

    int unsafe_output;
} NVDECDecoder;

typedef struct NVDECFramePool {
    unsigned int dpb_size;
    unsigned int nb_allocated;
} NVDECFramePool;

#define CHECK_CU(x) FF_CUDA_CHECK_DL(logctx, decoder->cudl, x)

static int map_avcodec_id(enum AVCodecID id)
{
    switch (id) {
#if CONFIG_AV1_NVDEC_HWACCEL
    case AV_CODEC_ID_AV1:        return cudaVideoCodec_AV1;
#endif
    case AV_CODEC_ID_H264:       return cudaVideoCodec_H264;
    case AV_CODEC_ID_HEVC:       return cudaVideoCodec_HEVC;
    case AV_CODEC_ID_MJPEG:      return cudaVideoCodec_JPEG;
    case AV_CODEC_ID_MPEG1VIDEO: return cudaVideoCodec_MPEG1;
    case AV_CODEC_ID_MPEG2VIDEO: return cudaVideoCodec_MPEG2;
    case AV_CODEC_ID_MPEG4:      return cudaVideoCodec_MPEG4;
    case AV_CODEC_ID_VC1:        return cudaVideoCodec_VC1;
    case AV_CODEC_ID_VP8:        return cudaVideoCodec_VP8;
    case AV_CODEC_ID_VP9:        return cudaVideoCodec_VP9;
    case AV_CODEC_ID_WMV3:       return cudaVideoCodec_VC1;
    }
    return -1;
}

static int map_chroma_format(enum AVPixelFormat pix_fmt)
{
    int shift_h = 0, shift_v = 0;

    if (av_pix_fmt_count_planes(pix_fmt) == 1)
        return cudaVideoChromaFormat_Monochrome;

    av_pix_fmt_get_chroma_sub_sample(pix_fmt, &shift_h, &shift_v);

    if (shift_h == 1 && shift_v == 1)
        return cudaVideoChromaFormat_420;
    else if (shift_h == 1 && shift_v == 0)
        return cudaVideoChromaFormat_422;
    else if (shift_h == 0 && shift_v == 0)
        return cudaVideoChromaFormat_444;

    return -1;
}

static int nvdec_test_capabilities(NVDECDecoder *decoder,
                                   CUVIDDECODECREATEINFO *params, void *logctx)
{
    int ret;
    CUVIDDECODECAPS caps = { 0 };

    caps.eCodecType      = params->CodecType;
    caps.eChromaFormat   = params->ChromaFormat;
    caps.nBitDepthMinus8 = params->bitDepthMinus8;

    if (!decoder->cvdl->cuvidGetDecoderCaps) {
        av_log(logctx, AV_LOG_WARNING, "Used Nvidia driver is too old to perform a capability check.\n");
        av_log(logctx, AV_LOG_WARNING, "The minimum required version is "
#if defined(_WIN32) || defined(__CYGWIN__)
            "378.66"
#else
            "378.13"
#endif
            ". Continuing blind.\n");
        return 0;
    }

    ret = CHECK_CU(decoder->cvdl->cuvidGetDecoderCaps(&caps));
    if (ret < 0)
        return ret;

    av_log(logctx, AV_LOG_VERBOSE, "NVDEC capabilities:\n");
    av_log(logctx, AV_LOG_VERBOSE, "format supported: %s, max_mb_count: %d\n",
           caps.bIsSupported ? "yes" : "no", caps.nMaxMBCount);
    av_log(logctx, AV_LOG_VERBOSE, "min_width: %d, max_width: %d\n",
           caps.nMinWidth, caps.nMaxWidth);
    av_log(logctx, AV_LOG_VERBOSE, "min_height: %d, max_height: %d\n",
           caps.nMinHeight, caps.nMaxHeight);

    if (!caps.bIsSupported) {
        av_log(logctx, AV_LOG_ERROR, "Hardware is lacking required capabilities\n");
        return AVERROR(EINVAL);
    }

    if (params->ulWidth > caps.nMaxWidth || params->ulWidth < caps.nMinWidth) {
        av_log(logctx, AV_LOG_ERROR, "Video width %d not within range from %d to %d\n",
               (int)params->ulWidth, caps.nMinWidth, caps.nMaxWidth);
        return AVERROR(EINVAL);
    }

    if (params->ulHeight > caps.nMaxHeight || params->ulHeight < caps.nMinHeight) {
        av_log(logctx, AV_LOG_ERROR, "Video height %d not within range from %d to %d\n",
               (int)params->ulHeight, caps.nMinHeight, caps.nMaxHeight);
        return AVERROR(EINVAL);
    }

    if ((params->ulWidth * params->ulHeight) / 256 > caps.nMaxMBCount) {
        av_log(logctx, AV_LOG_ERROR, "Video macroblock count %d exceeds maximum of %d\n",
               (int)(params->ulWidth * params->ulHeight) / 256, caps.nMaxMBCount);
        return AVERROR(EINVAL);
    }

    return 0;
}

static void nvdec_decoder_free(FFRefStructOpaque unused, void *obj)
{
    NVDECDecoder *decoder = obj;

    if (decoder->decoder) {
        void *logctx = decoder->hw_device_ref->data;
        CUcontext dummy;
        CHECK_CU(decoder->cudl->cuCtxPushCurrent(decoder->cuda_ctx));
        CHECK_CU(decoder->cvdl->cuvidDestroyDecoder(decoder->decoder));
        CHECK_CU(decoder->cudl->cuCtxPopCurrent(&dummy));
    }

    av_buffer_unref(&decoder->real_hw_frames_ref);
    av_buffer_unref(&decoder->hw_device_ref);

    cuvid_free_functions(&decoder->cvdl);
}

static int nvdec_decoder_create(NVDECDecoder **out, AVBufferRef *hw_device_ref,
                                CUVIDDECODECREATEINFO *params, void *logctx)
{
    AVHWDeviceContext  *hw_device_ctx = (AVHWDeviceContext*)hw_device_ref->data;
    AVCUDADeviceContext *device_hwctx = hw_device_ctx->hwctx;

    NVDECDecoder *decoder;

    CUcontext dummy;
    int ret;

    decoder = ff_refstruct_alloc_ext(sizeof(*decoder), 0,
                                     NULL, nvdec_decoder_free);
    if (!decoder)
        return AVERROR(ENOMEM);

    decoder->hw_device_ref = av_buffer_ref(hw_device_ref);
    if (!decoder->hw_device_ref) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    decoder->cuda_ctx = device_hwctx->cuda_ctx;
    decoder->cudl = device_hwctx->internal->cuda_dl;
    decoder->stream = device_hwctx->stream;

    ret = cuvid_load_functions(&decoder->cvdl, logctx);
    if (ret < 0) {
        av_log(logctx, AV_LOG_ERROR, "Failed loading nvcuvid.\n");
        goto fail;
    }

    ret = CHECK_CU(decoder->cudl->cuCtxPushCurrent(decoder->cuda_ctx));
    if (ret < 0)
        goto fail;

    ret = nvdec_test_capabilities(decoder, params, logctx);
    if (ret < 0) {
        CHECK_CU(decoder->cudl->cuCtxPopCurrent(&dummy));
        goto fail;
    }

    ret = CHECK_CU(decoder->cvdl->cuvidCreateDecoder(&decoder->decoder, params));

    CHECK_CU(decoder->cudl->cuCtxPopCurrent(&dummy));

    if (ret < 0) {
        goto fail;
    }

    *out = decoder;

    return 0;
fail:
    ff_refstruct_unref(&decoder);
    return ret;
}

static int nvdec_decoder_frame_init(FFRefStructOpaque opaque, void *obj)
{
    NVDECFramePool *pool = opaque.nc;
    unsigned int *intp = obj;

    if (pool->nb_allocated >= pool->dpb_size)
        return AVERROR(ENOMEM);

    *intp = pool->nb_allocated++;

    return 0;
}

static void nvdec_decoder_frame_pool_free(FFRefStructOpaque opaque)
{
    av_free(opaque.nc);
}

int ff_nvdec_decode_uninit(AVCodecContext *avctx)
{
    NVDECContext *ctx = avctx->internal->hwaccel_priv_data;

    av_freep(&ctx->bitstream_internal);
    ctx->bitstream           = NULL;
    ctx->bitstream_len       = 0;
    ctx->bitstream_allocated = 0;

    av_freep(&ctx->slice_offsets);
    ctx->nb_slices               = 0;
    ctx->slice_offsets_allocated = 0;

    ff_refstruct_unref(&ctx->decoder);
    ff_refstruct_pool_uninit(&ctx->decoder_pool);

    return 0;
}

static void nvdec_free_dummy(struct AVHWFramesContext *ctx)
{
    av_buffer_pool_uninit(&ctx->pool);
}

static AVBufferRef *nvdec_alloc_dummy(size_t size)
{
    return av_buffer_create(NULL, 0, NULL, NULL, 0);
}

static int nvdec_init_hwframes(AVCodecContext *avctx, AVBufferRef **out_frames_ref, int dummy)
{
    AVHWFramesContext *frames_ctx;
    int ret;

    ret = avcodec_get_hw_frames_parameters(avctx,
                                           avctx->hw_device_ctx,
                                           avctx->hwaccel->pix_fmt,
                                           out_frames_ref);
    if (ret < 0)
        return ret;

    frames_ctx = (AVHWFramesContext*)(*out_frames_ref)->data;

    if (dummy) {
        // Copied from ff_decode_get_hw_frames_ctx for compatibility
        frames_ctx->initial_pool_size += 3;

        frames_ctx->free = nvdec_free_dummy;
        frames_ctx->pool = av_buffer_pool_init(0, nvdec_alloc_dummy);

        if (!frames_ctx->pool) {
            av_buffer_unref(out_frames_ref);
            return AVERROR(ENOMEM);
        }
    } else {
        // This is normally not used to actually allocate frames from
        frames_ctx->initial_pool_size = 0;
    }

    ret = av_hwframe_ctx_init(*out_frames_ref);
    if (ret < 0) {
        av_buffer_unref(out_frames_ref);
        return ret;
    }

    return 0;
}

int ff_nvdec_decode_init(AVCodecContext *avctx)
{
    NVDECContext *ctx = avctx->internal->hwaccel_priv_data;

    NVDECDecoder        *decoder;
    AVBufferRef         *real_hw_frames_ref;
    NVDECFramePool      *pool;
    AVHWFramesContext   *frames_ctx;
    const AVPixFmtDescriptor *sw_desc;

    CUVIDDECODECREATEINFO params = { 0 };

    cudaVideoSurfaceFormat output_format;
    int cuvid_codec_type, cuvid_chroma_format, chroma_444;
    int ret = 0;

    int unsafe_output = !!(avctx->hwaccel_flags & AV_HWACCEL_FLAG_UNSAFE_OUTPUT);

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
    chroma_444 = ctx->supports_444 && cuvid_chroma_format == cudaVideoChromaFormat_444;

    if (!avctx->hw_frames_ctx) {
        ret = nvdec_init_hwframes(avctx, &avctx->hw_frames_ctx, 1);
        if (ret < 0)
            return ret;

        ret = nvdec_init_hwframes(avctx, &real_hw_frames_ref, 0);
        if (ret < 0)
            return ret;
    } else {
        real_hw_frames_ref = av_buffer_ref(avctx->hw_frames_ctx);
        if (!real_hw_frames_ref)
            return AVERROR(ENOMEM);
    }

    switch (sw_desc->comp[0].depth) {
    case 8:
        output_format = chroma_444 ? cudaVideoSurfaceFormat_YUV444 :
                                     cudaVideoSurfaceFormat_NV12;
        break;
    case 10:
    case 12:
        output_format = chroma_444 ? cudaVideoSurfaceFormat_YUV444_16Bit :
                                     cudaVideoSurfaceFormat_P016;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unsupported bit depth\n");
        av_buffer_unref(&real_hw_frames_ref);
        return AVERROR(ENOSYS);
    }

    frames_ctx = (AVHWFramesContext*)avctx->hw_frames_ctx->data;

    params.ulWidth             = avctx->coded_width;
    params.ulHeight            = avctx->coded_height;
    params.ulTargetWidth       = avctx->coded_width;
    params.ulTargetHeight      = avctx->coded_height;
    params.bitDepthMinus8      = sw_desc->comp[0].depth - 8;
    params.OutputFormat        = output_format;
    params.CodecType           = cuvid_codec_type;
    params.ChromaFormat        = cuvid_chroma_format;
    params.ulNumDecodeSurfaces = frames_ctx->initial_pool_size;
    params.ulNumOutputSurfaces = unsafe_output ? frames_ctx->initial_pool_size : 1;

    ret = nvdec_decoder_create(&ctx->decoder, frames_ctx->device_ref, &params, avctx);
    if (ret < 0) {
        if (params.ulNumDecodeSurfaces > 32) {
            av_log(avctx, AV_LOG_WARNING, "Using more than 32 (%d) decode surfaces might cause nvdec to fail.\n",
                   (int)params.ulNumDecodeSurfaces);
            av_log(avctx, AV_LOG_WARNING, "Try lowering the amount of threads. Using %d right now.\n",
                   avctx->thread_count);
        }
        av_buffer_unref(&real_hw_frames_ref);
        return ret;
    }

    decoder = ctx->decoder;
    decoder->unsafe_output = unsafe_output;
    decoder->real_hw_frames_ref = real_hw_frames_ref;
    real_hw_frames_ref = NULL;

    pool = av_mallocz(sizeof(*pool));
    if (!pool) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    pool->dpb_size = frames_ctx->initial_pool_size;

    ctx->decoder_pool = ff_refstruct_pool_alloc_ext(sizeof(unsigned int), 0, pool,
                                                    nvdec_decoder_frame_init,
                                                    NULL, NULL, nvdec_decoder_frame_pool_free);
    if (!ctx->decoder_pool) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    return 0;
fail:
    ff_nvdec_decode_uninit(avctx);
    return ret;
}

static void nvdec_fdd_priv_free(void *priv)
{
    NVDECFrame *cf = priv;

    if (!cf)
        return;

    ff_refstruct_unref(&cf->idx_ref);
    ff_refstruct_unref(&cf->ref_idx_ref);
    ff_refstruct_unref(&cf->decoder);

    av_freep(&priv);
}

static void nvdec_unmap_mapped_frame(void *opaque, uint8_t *data)
{
    NVDECFrame *unmap_data = (NVDECFrame*)data;
    NVDECDecoder *decoder = unmap_data->decoder;
    void *logctx = decoder->hw_device_ref->data;
    CUdeviceptr devptr = (CUdeviceptr)opaque;
    int ret;
    CUcontext dummy;

    ret = CHECK_CU(decoder->cudl->cuCtxPushCurrent(decoder->cuda_ctx));
    if (ret < 0)
        goto finish;

    CHECK_CU(decoder->cvdl->cuvidUnmapVideoFrame(decoder->decoder, devptr));

    CHECK_CU(decoder->cudl->cuCtxPopCurrent(&dummy));

finish:
    ff_refstruct_unref(&unmap_data->idx_ref);
    ff_refstruct_unref(&unmap_data->ref_idx_ref);
    ff_refstruct_unref(&unmap_data->decoder);
    av_free(unmap_data);
}

static int nvdec_retrieve_data(void *logctx, AVFrame *frame)
{
    FrameDecodeData  *fdd = (FrameDecodeData*)frame->private_ref->data;
    NVDECFrame        *cf = (NVDECFrame*)fdd->hwaccel_priv;
    NVDECDecoder *decoder = cf->decoder;

    AVHWFramesContext *hwctx = (AVHWFramesContext *)frame->hw_frames_ctx->data;

    CUVIDPROCPARAMS vpp = { 0 };
    NVDECFrame *unmap_data = NULL;

    CUcontext dummy;
    CUdeviceptr devptr;

    unsigned int pitch, i;
    unsigned int offset = 0;
    int shift_h = 0, shift_v = 0;
    int ret = 0;

    vpp.progressive_frame = 1;
    vpp.output_stream = decoder->stream;

    ret = CHECK_CU(decoder->cudl->cuCtxPushCurrent(decoder->cuda_ctx));
    if (ret < 0)
        return ret;

    ret = CHECK_CU(decoder->cvdl->cuvidMapVideoFrame(decoder->decoder,
                                                     cf->idx, &devptr,
                                                     &pitch, &vpp));
    if (ret < 0)
        goto finish;

    unmap_data = av_mallocz(sizeof(*unmap_data));
    if (!unmap_data) {
        ret = AVERROR(ENOMEM);
        goto copy_fail;
    }

    frame->buf[1] = av_buffer_create((uint8_t *)unmap_data, sizeof(*unmap_data),
                                     nvdec_unmap_mapped_frame, (void*)devptr,
                                     AV_BUFFER_FLAG_READONLY);
    if (!frame->buf[1]) {
        ret = AVERROR(ENOMEM);
        goto copy_fail;
    }

    ret = av_buffer_replace(&frame->hw_frames_ctx, decoder->real_hw_frames_ref);
    if (ret < 0)
        goto copy_fail;

    unmap_data->idx = cf->idx;
    unmap_data->idx_ref = ff_refstruct_ref(cf->idx_ref);
    unmap_data->decoder = ff_refstruct_ref(cf->decoder);

    av_pix_fmt_get_chroma_sub_sample(hwctx->sw_format, &shift_h, &shift_v);
    for (i = 0; frame->linesize[i]; i++) {
        frame->data[i] = (uint8_t*)(devptr + offset);
        frame->linesize[i] = pitch;
        offset += pitch * (frame->height >> (i ? shift_v : 0));
    }

    goto finish;

copy_fail:
    if (!frame->buf[1]) {
        CHECK_CU(decoder->cvdl->cuvidUnmapVideoFrame(decoder->decoder, devptr));
        av_freep(&unmap_data);
    } else {
        av_buffer_unref(&frame->buf[1]);
    }

finish:
    CHECK_CU(decoder->cudl->cuCtxPopCurrent(&dummy));

    if (ret < 0 || decoder->unsafe_output)
        return ret;

    return av_frame_make_writable(frame);
}

int ff_nvdec_start_frame(AVCodecContext *avctx, AVFrame *frame)
{
    NVDECContext *ctx = avctx->internal->hwaccel_priv_data;
    FrameDecodeData *fdd = (FrameDecodeData*)frame->private_ref->data;
    NVDECFrame *cf = NULL;
    int ret;

    ctx->bitstream_len = 0;
    ctx->nb_slices     = 0;

    if (fdd->hwaccel_priv)
        return 0;

    cf = av_mallocz(sizeof(*cf));
    if (!cf)
        return AVERROR(ENOMEM);

    cf->decoder = ff_refstruct_ref(ctx->decoder);

    cf->idx_ref = ff_refstruct_pool_get(ctx->decoder_pool);
    if (!cf->idx_ref) {
        av_log(avctx, AV_LOG_ERROR, "No decoder surfaces left\n");
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    cf->ref_idx = cf->idx = *cf->idx_ref;

    fdd->hwaccel_priv      = cf;
    fdd->hwaccel_priv_free = nvdec_fdd_priv_free;
    fdd->post_process      = nvdec_retrieve_data;

    return 0;
fail:
    nvdec_fdd_priv_free(cf);
    return ret;

}

int ff_nvdec_start_frame_sep_ref(AVCodecContext *avctx, AVFrame *frame, int has_sep_ref)
{
    NVDECContext *ctx = avctx->internal->hwaccel_priv_data;
    FrameDecodeData *fdd = (FrameDecodeData*)frame->private_ref->data;
    NVDECFrame *cf;
    int ret;

    ret = ff_nvdec_start_frame(avctx, frame);
    if (ret < 0)
        return ret;

    cf = fdd->hwaccel_priv;

    if (has_sep_ref) {
        if (!cf->ref_idx_ref) {
            cf->ref_idx_ref = ff_refstruct_pool_get(ctx->decoder_pool);
            if (!cf->ref_idx_ref) {
                av_log(avctx, AV_LOG_ERROR, "No decoder surfaces left\n");
                ret = AVERROR(ENOMEM);
                goto fail;
            }
        }
        cf->ref_idx = *cf->ref_idx_ref;
    } else {
        ff_refstruct_unref(&cf->ref_idx_ref);
        cf->ref_idx = cf->idx;
    }

    return 0;
fail:
    nvdec_fdd_priv_free(cf);
    return ret;
}

int ff_nvdec_end_frame(AVCodecContext *avctx)
{
    NVDECContext     *ctx = avctx->internal->hwaccel_priv_data;
    NVDECDecoder *decoder = ctx->decoder;
    void *logctx          = avctx;
    CUVIDPICPARAMS    *pp = &ctx->pic_params;

    CUcontext dummy;

    int ret = 0;

    pp->nBitstreamDataLen = ctx->bitstream_len;
    pp->pBitstreamData    = ctx->bitstream;
    pp->nNumSlices        = ctx->nb_slices;
    pp->pSliceDataOffsets = ctx->slice_offsets;

    ret = CHECK_CU(decoder->cudl->cuCtxPushCurrent(decoder->cuda_ctx));
    if (ret < 0)
        return ret;

    ret = CHECK_CU(decoder->cvdl->cuvidDecodePicture(decoder->decoder, &ctx->pic_params));
    if (ret < 0)
        goto finish;

finish:
    CHECK_CU(decoder->cudl->cuCtxPopCurrent(&dummy));

    return ret;
}

int ff_nvdec_simple_end_frame(AVCodecContext *avctx)
{
    NVDECContext *ctx = avctx->internal->hwaccel_priv_data;
    int ret = ff_nvdec_end_frame(avctx);
    ctx->bitstream = NULL;
    ctx->bitstream_len = 0;
    ctx->nb_slices = 0;
    return ret;
}

int ff_nvdec_simple_decode_slice(AVCodecContext *avctx, const uint8_t *buffer,
                                 uint32_t size)
{
    NVDECContext *ctx = avctx->internal->hwaccel_priv_data;
    void *tmp;

    tmp = av_fast_realloc(ctx->slice_offsets, &ctx->slice_offsets_allocated,
                          (ctx->nb_slices + 1) * sizeof(*ctx->slice_offsets));
    if (!tmp)
        return AVERROR(ENOMEM);
    ctx->slice_offsets = tmp;

    if (!ctx->bitstream)
        ctx->bitstream = buffer;

    ctx->slice_offsets[ctx->nb_slices] = buffer - ctx->bitstream;
    ctx->bitstream_len += size;
    ctx->nb_slices++;

    return 0;
}

int ff_nvdec_frame_params(AVCodecContext *avctx,
                          AVBufferRef *hw_frames_ctx,
                          int dpb_size,
                          int supports_444)
{
    AVHWFramesContext *frames_ctx = (AVHWFramesContext*)hw_frames_ctx->data;
    const AVPixFmtDescriptor *sw_desc;
    int cuvid_codec_type, cuvid_chroma_format, chroma_444;

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
        av_log(avctx, AV_LOG_VERBOSE, "Unsupported chroma format\n");
        return AVERROR(EINVAL);
    }
    chroma_444 = supports_444 && cuvid_chroma_format == cudaVideoChromaFormat_444;

    frames_ctx->format            = AV_PIX_FMT_CUDA;
    frames_ctx->width             = (avctx->coded_width + 1) & ~1;
    frames_ctx->height            = (avctx->coded_height + 1) & ~1;
    /*
     * We add two extra frames to the pool to account for deinterlacing filters
     * holding onto their frames.
     */
    frames_ctx->initial_pool_size = dpb_size + 2;

    switch (sw_desc->comp[0].depth) {
    case 8:
        frames_ctx->sw_format = chroma_444 ? AV_PIX_FMT_YUV444P : AV_PIX_FMT_NV12;
        break;
    case 10:
        frames_ctx->sw_format = chroma_444 ? AV_PIX_FMT_YUV444P16 : AV_PIX_FMT_P010;
        break;
    case 12:
        frames_ctx->sw_format = chroma_444 ? AV_PIX_FMT_YUV444P16 : AV_PIX_FMT_P016;
        break;
    default:
        return AVERROR(EINVAL);
    }

    return 0;
}

int ff_nvdec_get_ref_idx(AVFrame *frame)
{
    FrameDecodeData *fdd;
    NVDECFrame *cf;

    if (!frame || !frame->private_ref)
        return -1;

    fdd = (FrameDecodeData*)frame->private_ref->data;
    cf  = (NVDECFrame*)fdd->hwaccel_priv;
    if (!cf)
        return -1;

    return cf->ref_idx;
}
