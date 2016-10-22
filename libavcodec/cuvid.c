/*
 * Nvidia CUVID decoder
 * Copyright (c) 2016 Timo Rothenpieler <timo@rothenpieler.org>
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

#include "libavutil/buffer.h"
#include "libavutil/mathematics.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_cuda.h"
#include "libavutil/fifo.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"

#include "avcodec.h"
#include "internal.h"

#include "compat/cuda/nvcuvid.h"

#define MAX_FRAME_COUNT 25

typedef struct CuvidContext
{
    AVClass *avclass;

    CUvideodecoder cudecoder;
    CUvideoparser cuparser;

    char *cu_gpu;

    AVBufferRef *hwdevice;
    AVBufferRef *hwframe;

    AVBSFContext *bsf;

    AVFifoBuffer *frame_queue;

    int deint_mode;
    int64_t prev_pts;

    int internal_error;
    int decoder_flushing;

    cudaVideoCodec codec_type;
    cudaVideoChromaFormat chroma_format;

    CUVIDPARSERPARAMS cuparseinfo;
    CUVIDEOFORMATEX cuparse_ext;
} CuvidContext;

typedef struct CuvidParsedFrame
{
    CUVIDPARSERDISPINFO dispinfo;
    int second_field;
    int is_deinterlacing;
} CuvidParsedFrame;

static int check_cu(AVCodecContext *avctx, CUresult err, const char *func)
{
    const char *err_name;
    const char *err_string;

    av_log(avctx, AV_LOG_TRACE, "Calling %s\n", func);

    if (err == CUDA_SUCCESS)
        return 0;

    cuGetErrorName(err, &err_name);
    cuGetErrorString(err, &err_string);

    av_log(avctx, AV_LOG_ERROR, "%s failed", func);
    if (err_name && err_string)
        av_log(avctx, AV_LOG_ERROR, " -> %s: %s", err_name, err_string);
    av_log(avctx, AV_LOG_ERROR, "\n");

    return AVERROR_EXTERNAL;
}

#define CHECK_CU(x) check_cu(avctx, (x), #x)

static int CUDAAPI cuvid_handle_video_sequence(void *opaque, CUVIDEOFORMAT* format)
{
    AVCodecContext *avctx = opaque;
    CuvidContext *ctx = avctx->priv_data;
    AVHWFramesContext *hwframe_ctx = (AVHWFramesContext*)ctx->hwframe->data;
    CUVIDDECODECREATEINFO cuinfo;

    av_log(avctx, AV_LOG_TRACE, "pfnSequenceCallback, progressive_sequence=%d\n", format->progressive_sequence);

    ctx->internal_error = 0;

    avctx->width = format->display_area.right;
    avctx->height = format->display_area.bottom;

    ff_set_sar(avctx, av_div_q(
        (AVRational){ format->display_aspect_ratio.x, format->display_aspect_ratio.y },
        (AVRational){ avctx->width, avctx->height }));

    if (!format->progressive_sequence && ctx->deint_mode == cudaVideoDeinterlaceMode_Weave)
        avctx->flags |= AV_CODEC_FLAG_INTERLACED_DCT;
    else
        avctx->flags &= ~AV_CODEC_FLAG_INTERLACED_DCT;

    if (format->video_signal_description.video_full_range_flag)
        avctx->color_range = AVCOL_RANGE_JPEG;
    else
        avctx->color_range = AVCOL_RANGE_MPEG;

    avctx->color_primaries = format->video_signal_description.color_primaries;
    avctx->color_trc = format->video_signal_description.transfer_characteristics;
    avctx->colorspace = format->video_signal_description.matrix_coefficients;

    if (format->bitrate)
        avctx->bit_rate = format->bitrate;

    if (format->frame_rate.numerator && format->frame_rate.denominator) {
        avctx->framerate.num = format->frame_rate.numerator;
        avctx->framerate.den = format->frame_rate.denominator;
    }

    if (ctx->cudecoder
            && avctx->coded_width == format->coded_width
            && avctx->coded_height == format->coded_height
            && ctx->chroma_format == format->chroma_format
            && ctx->codec_type == format->codec)
        return 1;

    if (ctx->cudecoder) {
        av_log(avctx, AV_LOG_TRACE, "Re-initializing decoder\n");
        ctx->internal_error = CHECK_CU(cuvidDestroyDecoder(ctx->cudecoder));
        if (ctx->internal_error < 0)
            return 0;
        ctx->cudecoder = NULL;
    }

    if (hwframe_ctx->pool && (
            hwframe_ctx->width < avctx->width ||
            hwframe_ctx->height < avctx->height ||
            hwframe_ctx->format != AV_PIX_FMT_CUDA ||
            hwframe_ctx->sw_format != AV_PIX_FMT_NV12)) {
        av_log(avctx, AV_LOG_ERROR, "AVHWFramesContext is already initialized with incompatible parameters\n");
        ctx->internal_error = AVERROR(EINVAL);
        return 0;
    }

    if (format->chroma_format != cudaVideoChromaFormat_420) {
        av_log(avctx, AV_LOG_ERROR, "Chroma formats other than 420 are not supported\n");
        ctx->internal_error = AVERROR(EINVAL);
        return 0;
    }

    avctx->coded_width = format->coded_width;
    avctx->coded_height = format->coded_height;

    ctx->chroma_format = format->chroma_format;

    memset(&cuinfo, 0, sizeof(cuinfo));

    cuinfo.CodecType = ctx->codec_type = format->codec;
    cuinfo.ChromaFormat = format->chroma_format;
    cuinfo.OutputFormat = cudaVideoSurfaceFormat_NV12;

    cuinfo.ulWidth = avctx->coded_width;
    cuinfo.ulHeight = avctx->coded_height;
    cuinfo.ulTargetWidth = cuinfo.ulWidth;
    cuinfo.ulTargetHeight = cuinfo.ulHeight;

    cuinfo.target_rect.left = 0;
    cuinfo.target_rect.top = 0;
    cuinfo.target_rect.right = cuinfo.ulWidth;
    cuinfo.target_rect.bottom = cuinfo.ulHeight;

    cuinfo.ulNumDecodeSurfaces = MAX_FRAME_COUNT;
    cuinfo.ulNumOutputSurfaces = 1;
    cuinfo.ulCreationFlags = cudaVideoCreate_PreferCUVID;
    cuinfo.bitDepthMinus8 = format->bit_depth_luma_minus8;

    if (format->progressive_sequence) {
        ctx->deint_mode = cuinfo.DeinterlaceMode = cudaVideoDeinterlaceMode_Weave;
    } else {
        cuinfo.DeinterlaceMode = ctx->deint_mode;
    }

    if (ctx->deint_mode != cudaVideoDeinterlaceMode_Weave)
        avctx->framerate = av_mul_q(avctx->framerate, (AVRational){2, 1});

    ctx->internal_error = CHECK_CU(cuvidCreateDecoder(&ctx->cudecoder, &cuinfo));
    if (ctx->internal_error < 0)
        return 0;

    if (!hwframe_ctx->pool) {
        hwframe_ctx->format = AV_PIX_FMT_CUDA;
        hwframe_ctx->sw_format = AV_PIX_FMT_NV12;
        hwframe_ctx->width = avctx->width;
        hwframe_ctx->height = avctx->height;

        if ((ctx->internal_error = av_hwframe_ctx_init(ctx->hwframe)) < 0) {
            av_log(avctx, AV_LOG_ERROR, "av_hwframe_ctx_init failed\n");
            return 0;
        }
    }

    return 1;
}

static int CUDAAPI cuvid_handle_picture_decode(void *opaque, CUVIDPICPARAMS* picparams)
{
    AVCodecContext *avctx = opaque;
    CuvidContext *ctx = avctx->priv_data;

    av_log(avctx, AV_LOG_TRACE, "pfnDecodePicture\n");

    ctx->internal_error = CHECK_CU(cuvidDecodePicture(ctx->cudecoder, picparams));
    if (ctx->internal_error < 0)
        return 0;

    return 1;
}

static int CUDAAPI cuvid_handle_picture_display(void *opaque, CUVIDPARSERDISPINFO* dispinfo)
{
    AVCodecContext *avctx = opaque;
    CuvidContext *ctx = avctx->priv_data;
    CuvidParsedFrame parsed_frame = { *dispinfo, 0, 0 };

    ctx->internal_error = 0;

    if (ctx->deint_mode == cudaVideoDeinterlaceMode_Weave) {
        av_fifo_generic_write(ctx->frame_queue, &parsed_frame, sizeof(CuvidParsedFrame), NULL);
    } else {
        parsed_frame.is_deinterlacing = 1;
        av_fifo_generic_write(ctx->frame_queue, &parsed_frame, sizeof(CuvidParsedFrame), NULL);
        parsed_frame.second_field = 1;
        av_fifo_generic_write(ctx->frame_queue, &parsed_frame, sizeof(CuvidParsedFrame), NULL);
    }

    return 1;
}

static int cuvid_decode_packet(AVCodecContext *avctx, const AVPacket *avpkt)
{
    CuvidContext *ctx = avctx->priv_data;
    AVHWDeviceContext *device_ctx = (AVHWDeviceContext*)ctx->hwdevice->data;
    AVCUDADeviceContext *device_hwctx = device_ctx->hwctx;
    CUcontext dummy, cuda_ctx = device_hwctx->cuda_ctx;
    CUVIDSOURCEDATAPACKET cupkt;
    AVPacket filter_packet = { 0 };
    AVPacket filtered_packet = { 0 };
    int ret = 0, eret = 0, is_flush = ctx->decoder_flushing;

    av_log(avctx, AV_LOG_TRACE, "cuvid_decode_packet\n");

    if (is_flush && avpkt && avpkt->size)
        return AVERROR_EOF;

    if (av_fifo_size(ctx->frame_queue) / sizeof(CuvidParsedFrame) > MAX_FRAME_COUNT - 2 && avpkt && avpkt->size)
        return AVERROR(EAGAIN);

    if (ctx->bsf && avpkt && avpkt->size) {
        if ((ret = av_packet_ref(&filter_packet, avpkt)) < 0) {
            av_log(avctx, AV_LOG_ERROR, "av_packet_ref failed\n");
            return ret;
        }

        if ((ret = av_bsf_send_packet(ctx->bsf, &filter_packet)) < 0) {
            av_log(avctx, AV_LOG_ERROR, "av_bsf_send_packet failed\n");
            av_packet_unref(&filter_packet);
            return ret;
        }

        if ((ret = av_bsf_receive_packet(ctx->bsf, &filtered_packet)) < 0) {
            av_log(avctx, AV_LOG_ERROR, "av_bsf_receive_packet failed\n");
            return ret;
        }

        avpkt = &filtered_packet;
    }

    ret = CHECK_CU(cuCtxPushCurrent(cuda_ctx));
    if (ret < 0) {
        av_packet_unref(&filtered_packet);
        return ret;
    }

    memset(&cupkt, 0, sizeof(cupkt));

    if (avpkt && avpkt->size) {
        cupkt.payload_size = avpkt->size;
        cupkt.payload = avpkt->data;

        if (avpkt->pts != AV_NOPTS_VALUE) {
            cupkt.flags = CUVID_PKT_TIMESTAMP;
            if (avctx->pkt_timebase.num && avctx->pkt_timebase.den)
                cupkt.timestamp = av_rescale_q(avpkt->pts, avctx->pkt_timebase, (AVRational){1, 10000000});
            else
                cupkt.timestamp = avpkt->pts;
        }
    } else {
        cupkt.flags = CUVID_PKT_ENDOFSTREAM;
        ctx->decoder_flushing = 1;
    }

    ret = CHECK_CU(cuvidParseVideoData(ctx->cuparser, &cupkt));

    av_packet_unref(&filtered_packet);

    if (ret < 0)
        goto error;

    // cuvidParseVideoData doesn't return an error just because stuff failed...
    if (ctx->internal_error) {
        av_log(avctx, AV_LOG_ERROR, "cuvid decode callback error\n");
        ret = ctx->internal_error;
        goto error;
    }

error:
    eret = CHECK_CU(cuCtxPopCurrent(&dummy));

    if (eret < 0)
        return eret;
    else if (ret < 0)
        return ret;
    else if (is_flush)
        return AVERROR_EOF;
    else
        return 0;
}

static int cuvid_output_frame(AVCodecContext *avctx, AVFrame *frame)
{
    CuvidContext *ctx = avctx->priv_data;
    AVHWDeviceContext *device_ctx = (AVHWDeviceContext*)ctx->hwdevice->data;
    AVCUDADeviceContext *device_hwctx = device_ctx->hwctx;
    CUcontext dummy, cuda_ctx = device_hwctx->cuda_ctx;
    CUdeviceptr mapped_frame = 0;
    int ret = 0, eret = 0;

    av_log(avctx, AV_LOG_TRACE, "cuvid_output_frame\n");

    if (ctx->decoder_flushing) {
        ret = cuvid_decode_packet(avctx, NULL);
        if (ret < 0 && ret != AVERROR_EOF)
            return ret;
    }

    ret = CHECK_CU(cuCtxPushCurrent(cuda_ctx));
    if (ret < 0)
        return ret;

    if (av_fifo_size(ctx->frame_queue)) {
        CuvidParsedFrame parsed_frame;
        CUVIDPROCPARAMS params;
        unsigned int pitch = 0;
        int offset = 0;
        int i;

        av_fifo_generic_read(ctx->frame_queue, &parsed_frame, sizeof(CuvidParsedFrame), NULL);

        memset(&params, 0, sizeof(params));
        params.progressive_frame = parsed_frame.dispinfo.progressive_frame;
        params.second_field = parsed_frame.second_field;
        params.top_field_first = parsed_frame.dispinfo.top_field_first;

        ret = CHECK_CU(cuvidMapVideoFrame(ctx->cudecoder, parsed_frame.dispinfo.picture_index, &mapped_frame, &pitch, &params));
        if (ret < 0)
            goto error;

        if (avctx->pix_fmt == AV_PIX_FMT_CUDA) {
            ret = av_hwframe_get_buffer(ctx->hwframe, frame, 0);
            if (ret < 0) {
                av_log(avctx, AV_LOG_ERROR, "av_hwframe_get_buffer failed\n");
                goto error;
            }

            ret = ff_decode_frame_props(avctx, frame);
            if (ret < 0) {
                av_log(avctx, AV_LOG_ERROR, "ff_decode_frame_props failed\n");
                goto error;
            }

            for (i = 0; i < 2; i++) {
                CUDA_MEMCPY2D cpy = {
                    .srcMemoryType = CU_MEMORYTYPE_DEVICE,
                    .dstMemoryType = CU_MEMORYTYPE_DEVICE,
                    .srcDevice     = mapped_frame,
                    .dstDevice     = (CUdeviceptr)frame->data[i],
                    .srcPitch      = pitch,
                    .dstPitch      = frame->linesize[i],
                    .srcY          = offset,
                    .WidthInBytes  = FFMIN(pitch, frame->linesize[i]),
                    .Height        = avctx->height >> (i ? 1 : 0),
                };

                ret = CHECK_CU(cuMemcpy2D(&cpy));
                if (ret < 0)
                    goto error;

                offset += avctx->coded_height;
            }
        } else if (avctx->pix_fmt == AV_PIX_FMT_NV12) {
            AVFrame *tmp_frame = av_frame_alloc();
            if (!tmp_frame) {
                av_log(avctx, AV_LOG_ERROR, "av_frame_alloc failed\n");
                ret = AVERROR(ENOMEM);
                goto error;
            }

            tmp_frame->format        = AV_PIX_FMT_CUDA;
            tmp_frame->hw_frames_ctx = av_buffer_ref(ctx->hwframe);
            tmp_frame->data[0]       = (uint8_t*)mapped_frame;
            tmp_frame->linesize[0]   = pitch;
            tmp_frame->data[1]       = (uint8_t*)(mapped_frame + avctx->coded_height * pitch);
            tmp_frame->linesize[1]   = pitch;
            tmp_frame->width         = avctx->width;
            tmp_frame->height        = avctx->height;

            ret = ff_get_buffer(avctx, frame, 0);
            if (ret < 0) {
                av_log(avctx, AV_LOG_ERROR, "ff_get_buffer failed\n");
                av_frame_free(&tmp_frame);
                goto error;
            }

            ret = av_hwframe_transfer_data(frame, tmp_frame, 0);
            if (ret) {
                av_log(avctx, AV_LOG_ERROR, "av_hwframe_transfer_data failed\n");
                av_frame_free(&tmp_frame);
                goto error;
            }

            av_frame_free(&tmp_frame);
        } else {
            ret = AVERROR_BUG;
            goto error;
        }

        frame->width = avctx->width;
        frame->height = avctx->height;
        if (avctx->pkt_timebase.num && avctx->pkt_timebase.den)
            frame->pts = av_rescale_q(parsed_frame.dispinfo.timestamp, (AVRational){1, 10000000}, avctx->pkt_timebase);
        else
            frame->pts = parsed_frame.dispinfo.timestamp;

        if (parsed_frame.second_field) {
            if (ctx->prev_pts == INT64_MIN) {
                ctx->prev_pts = frame->pts;
                frame->pts += (avctx->pkt_timebase.den * avctx->framerate.den) / (avctx->pkt_timebase.num * avctx->framerate.num);
            } else {
                int pts_diff = (frame->pts - ctx->prev_pts) / 2;
                ctx->prev_pts = frame->pts;
                frame->pts += pts_diff;
            }
        }

        /* CUVIDs opaque reordering breaks the internal pkt logic.
         * So set pkt_pts and clear all the other pkt_ fields.
         */
#if FF_API_PKT_PTS
FF_DISABLE_DEPRECATION_WARNINGS
        frame->pkt_pts = frame->pts;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
        av_frame_set_pkt_pos(frame, -1);
        av_frame_set_pkt_duration(frame, 0);
        av_frame_set_pkt_size(frame, -1);

        frame->interlaced_frame = !parsed_frame.is_deinterlacing && !parsed_frame.dispinfo.progressive_frame;

        if (frame->interlaced_frame)
            frame->top_field_first = parsed_frame.dispinfo.top_field_first;
    } else if (ctx->decoder_flushing) {
        ret = AVERROR_EOF;
    } else {
        ret = AVERROR(EAGAIN);
    }

error:
    if (mapped_frame)
        eret = CHECK_CU(cuvidUnmapVideoFrame(ctx->cudecoder, mapped_frame));

    eret = CHECK_CU(cuCtxPopCurrent(&dummy));

    if (eret < 0)
        return eret;
    else
        return ret;
}

static int cuvid_decode_frame(AVCodecContext *avctx, void *data, int *got_frame, AVPacket *avpkt)
{
    CuvidContext *ctx = avctx->priv_data;
    AVFrame *frame = data;
    int ret = 0;

    av_log(avctx, AV_LOG_TRACE, "cuvid_decode_frame\n");

    if (ctx->deint_mode != cudaVideoDeinterlaceMode_Weave) {
        av_log(avctx, AV_LOG_ERROR, "Deinterlacing is not supported via the old API\n");
        return AVERROR(EINVAL);
    }

    if (!ctx->decoder_flushing) {
        ret = cuvid_decode_packet(avctx, avpkt);
        if (ret < 0)
            return ret;
    }

    ret = cuvid_output_frame(avctx, frame);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        *got_frame = 0;
    } else if (ret < 0) {
        return ret;
    } else {
        *got_frame = 1;
    }

    return 0;
}

static av_cold int cuvid_decode_end(AVCodecContext *avctx)
{
    CuvidContext *ctx = avctx->priv_data;

    av_fifo_freep(&ctx->frame_queue);

    if (ctx->bsf)
        av_bsf_free(&ctx->bsf);

    if (ctx->cuparser)
        cuvidDestroyVideoParser(ctx->cuparser);

    if (ctx->cudecoder)
        cuvidDestroyDecoder(ctx->cudecoder);

    av_buffer_unref(&ctx->hwframe);
    av_buffer_unref(&ctx->hwdevice);

    return 0;
}

static int cuvid_test_dummy_decoder(AVCodecContext *avctx, CUVIDPARSERPARAMS *cuparseinfo)
{
    CUVIDDECODECREATEINFO cuinfo;
    CUvideodecoder cudec = 0;
    int ret = 0;

    memset(&cuinfo, 0, sizeof(cuinfo));

    cuinfo.CodecType = cuparseinfo->CodecType;
    cuinfo.ChromaFormat = cudaVideoChromaFormat_420;
    cuinfo.OutputFormat = cudaVideoSurfaceFormat_NV12;

    cuinfo.ulWidth = 1280;
    cuinfo.ulHeight = 720;
    cuinfo.ulTargetWidth = cuinfo.ulWidth;
    cuinfo.ulTargetHeight = cuinfo.ulHeight;

    cuinfo.target_rect.left = 0;
    cuinfo.target_rect.top = 0;
    cuinfo.target_rect.right = cuinfo.ulWidth;
    cuinfo.target_rect.bottom = cuinfo.ulHeight;

    cuinfo.ulNumDecodeSurfaces = MAX_FRAME_COUNT;
    cuinfo.ulNumOutputSurfaces = 1;
    cuinfo.ulCreationFlags = cudaVideoCreate_PreferCUVID;
    cuinfo.bitDepthMinus8 = 0;

    cuinfo.DeinterlaceMode = cudaVideoDeinterlaceMode_Weave;

    ret = CHECK_CU(cuvidCreateDecoder(&cudec, &cuinfo));
    if (ret < 0)
        return ret;

    ret = CHECK_CU(cuvidDestroyDecoder(cudec));
    if (ret < 0)
        return ret;

    return 0;
}

static av_cold int cuvid_decode_init(AVCodecContext *avctx)
{
    CuvidContext *ctx = avctx->priv_data;
    AVCUDADeviceContext *device_hwctx;
    AVHWDeviceContext *device_ctx;
    AVHWFramesContext *hwframe_ctx;
    CUVIDSOURCEDATAPACKET seq_pkt;
    CUcontext cuda_ctx = NULL;
    CUcontext dummy;
    const AVBitStreamFilter *bsf;
    int ret = 0;

    enum AVPixelFormat pix_fmts[3] = { AV_PIX_FMT_CUDA,
                                       AV_PIX_FMT_NV12,
                                       AV_PIX_FMT_NONE };

    ret = ff_get_format(avctx, pix_fmts);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "ff_get_format failed: %d\n", ret);
        return ret;
    }

    ctx->frame_queue = av_fifo_alloc(MAX_FRAME_COUNT * sizeof(CuvidParsedFrame));
    if (!ctx->frame_queue) {
        ret = AVERROR(ENOMEM);
        goto error;
    }

    avctx->pix_fmt = ret;

    if (avctx->hw_frames_ctx) {
        ctx->hwframe = av_buffer_ref(avctx->hw_frames_ctx);
        if (!ctx->hwframe) {
            ret = AVERROR(ENOMEM);
            goto error;
        }

        hwframe_ctx = (AVHWFramesContext*)ctx->hwframe->data;

        ctx->hwdevice = av_buffer_ref(hwframe_ctx->device_ref);
        if (!ctx->hwdevice) {
            ret = AVERROR(ENOMEM);
            goto error;
        }
    } else {
        ret = av_hwdevice_ctx_create(&ctx->hwdevice, AV_HWDEVICE_TYPE_CUDA, ctx->cu_gpu, NULL, 0);
        if (ret < 0)
            goto error;

        ctx->hwframe = av_hwframe_ctx_alloc(ctx->hwdevice);
        if (!ctx->hwframe) {
            av_log(avctx, AV_LOG_ERROR, "av_hwframe_ctx_alloc failed\n");
            ret = AVERROR(ENOMEM);
            goto error;
        }

        hwframe_ctx = (AVHWFramesContext*)ctx->hwframe->data;
    }

    device_ctx = hwframe_ctx->device_ctx;
    device_hwctx = device_ctx->hwctx;
    cuda_ctx = device_hwctx->cuda_ctx;

    memset(&ctx->cuparseinfo, 0, sizeof(ctx->cuparseinfo));
    memset(&ctx->cuparse_ext, 0, sizeof(ctx->cuparse_ext));
    memset(&seq_pkt, 0, sizeof(seq_pkt));

    ctx->cuparseinfo.pExtVideoInfo = &ctx->cuparse_ext;

    switch (avctx->codec->id) {
#if CONFIG_H263_CUVID_DECODER
    case AV_CODEC_ID_H263:
        ctx->cuparseinfo.CodecType = cudaVideoCodec_MPEG4;
        break;
#endif
#if CONFIG_H264_CUVID_DECODER
    case AV_CODEC_ID_H264:
        ctx->cuparseinfo.CodecType = cudaVideoCodec_H264;
        break;
#endif
#if CONFIG_HEVC_CUVID_DECODER
    case AV_CODEC_ID_HEVC:
        ctx->cuparseinfo.CodecType = cudaVideoCodec_HEVC;
        break;
#endif
#if CONFIG_MJPEG_CUVID_DECODER
    case AV_CODEC_ID_MJPEG:
        ctx->cuparseinfo.CodecType = cudaVideoCodec_JPEG;
        break;
#endif
#if CONFIG_MPEG1_CUVID_DECODER
    case AV_CODEC_ID_MPEG1VIDEO:
        ctx->cuparseinfo.CodecType = cudaVideoCodec_MPEG1;
        break;
#endif
#if CONFIG_MPEG2_CUVID_DECODER
    case AV_CODEC_ID_MPEG2VIDEO:
        ctx->cuparseinfo.CodecType = cudaVideoCodec_MPEG2;
        break;
#endif
#if CONFIG_MPEG4_CUVID_DECODER
    case AV_CODEC_ID_MPEG4:
        ctx->cuparseinfo.CodecType = cudaVideoCodec_MPEG4;
        break;
#endif
#if CONFIG_VP8_CUVID_DECODER
    case AV_CODEC_ID_VP8:
        ctx->cuparseinfo.CodecType = cudaVideoCodec_VP8;
        break;
#endif
#if CONFIG_VP9_CUVID_DECODER
    case AV_CODEC_ID_VP9:
        ctx->cuparseinfo.CodecType = cudaVideoCodec_VP9;
        break;
#endif
#if CONFIG_VC1_CUVID_DECODER
    case AV_CODEC_ID_VC1:
        ctx->cuparseinfo.CodecType = cudaVideoCodec_VC1;
        break;
#endif
    default:
        av_log(avctx, AV_LOG_ERROR, "Invalid CUVID codec!\n");
        return AVERROR_BUG;
    }

    if (avctx->codec->id == AV_CODEC_ID_H264 || avctx->codec->id == AV_CODEC_ID_HEVC) {
        if (avctx->codec->id == AV_CODEC_ID_H264)
            bsf = av_bsf_get_by_name("h264_mp4toannexb");
        else
            bsf = av_bsf_get_by_name("hevc_mp4toannexb");

        if (!bsf) {
            ret = AVERROR_BSF_NOT_FOUND;
            goto error;
        }
        if (ret = av_bsf_alloc(bsf, &ctx->bsf)) {
            goto error;
        }
        if (((ret = avcodec_parameters_from_context(ctx->bsf->par_in, avctx)) < 0) || ((ret = av_bsf_init(ctx->bsf)) < 0)) {
            av_bsf_free(&ctx->bsf);
            goto error;
        }

        ctx->cuparse_ext.format.seqhdr_data_length = ctx->bsf->par_out->extradata_size;
        memcpy(ctx->cuparse_ext.raw_seqhdr_data,
               ctx->bsf->par_out->extradata,
               FFMIN(sizeof(ctx->cuparse_ext.raw_seqhdr_data), ctx->bsf->par_out->extradata_size));
    } else if (avctx->extradata_size > 0) {
        ctx->cuparse_ext.format.seqhdr_data_length = avctx->extradata_size;
        memcpy(ctx->cuparse_ext.raw_seqhdr_data,
               avctx->extradata,
               FFMIN(sizeof(ctx->cuparse_ext.raw_seqhdr_data), avctx->extradata_size));
    }

    ctx->cuparseinfo.ulMaxNumDecodeSurfaces = MAX_FRAME_COUNT;
    ctx->cuparseinfo.ulMaxDisplayDelay = 4;
    ctx->cuparseinfo.pUserData = avctx;
    ctx->cuparseinfo.pfnSequenceCallback = cuvid_handle_video_sequence;
    ctx->cuparseinfo.pfnDecodePicture = cuvid_handle_picture_decode;
    ctx->cuparseinfo.pfnDisplayPicture = cuvid_handle_picture_display;

    ret = CHECK_CU(cuCtxPushCurrent(cuda_ctx));
    if (ret < 0)
        goto error;

    ret = cuvid_test_dummy_decoder(avctx, &ctx->cuparseinfo);
    if (ret < 0)
        goto error;

    ret = CHECK_CU(cuvidCreateVideoParser(&ctx->cuparser, &ctx->cuparseinfo));
    if (ret < 0)
        goto error;

    seq_pkt.payload = ctx->cuparse_ext.raw_seqhdr_data;
    seq_pkt.payload_size = ctx->cuparse_ext.format.seqhdr_data_length;

    if (seq_pkt.payload && seq_pkt.payload_size) {
        ret = CHECK_CU(cuvidParseVideoData(ctx->cuparser, &seq_pkt));
        if (ret < 0)
            goto error;
    }

    ret = CHECK_CU(cuCtxPopCurrent(&dummy));
    if (ret < 0)
        goto error;

    ctx->prev_pts = INT64_MIN;

    if (!avctx->pkt_timebase.num || !avctx->pkt_timebase.den)
        av_log(avctx, AV_LOG_WARNING, "Invalid pkt_timebase, passing timestamps as-is.\n");

    return 0;

error:
    cuvid_decode_end(avctx);
    return ret;
}

static void cuvid_flush(AVCodecContext *avctx)
{
    CuvidContext *ctx = avctx->priv_data;
    AVHWDeviceContext *device_ctx = (AVHWDeviceContext*)ctx->hwdevice->data;
    AVCUDADeviceContext *device_hwctx = device_ctx->hwctx;
    CUcontext dummy, cuda_ctx = device_hwctx->cuda_ctx;
    CUVIDSOURCEDATAPACKET seq_pkt = { 0 };
    int ret;

    ret = CHECK_CU(cuCtxPushCurrent(cuda_ctx));
    if (ret < 0)
        goto error;

    av_fifo_freep(&ctx->frame_queue);

    ctx->frame_queue = av_fifo_alloc(MAX_FRAME_COUNT * sizeof(CuvidParsedFrame));
    if (!ctx->frame_queue) {
        av_log(avctx, AV_LOG_ERROR, "Failed to recreate frame queue on flush\n");
        return;
    }

    if (ctx->cudecoder) {
        cuvidDestroyDecoder(ctx->cudecoder);
        ctx->cudecoder = NULL;
    }

    if (ctx->cuparser) {
        cuvidDestroyVideoParser(ctx->cuparser);
        ctx->cuparser = NULL;
    }

    ret = CHECK_CU(cuvidCreateVideoParser(&ctx->cuparser, &ctx->cuparseinfo));
    if (ret < 0)
        goto error;

    seq_pkt.payload = ctx->cuparse_ext.raw_seqhdr_data;
    seq_pkt.payload_size = ctx->cuparse_ext.format.seqhdr_data_length;

    if (seq_pkt.payload && seq_pkt.payload_size) {
        ret = CHECK_CU(cuvidParseVideoData(ctx->cuparser, &seq_pkt));
        if (ret < 0)
            goto error;
    }

    ret = CHECK_CU(cuCtxPopCurrent(&dummy));
    if (ret < 0)
        goto error;

    ctx->prev_pts = INT64_MIN;
    ctx->decoder_flushing = 0;

    return;
 error:
    av_log(avctx, AV_LOG_ERROR, "CUDA reinit on flush failed\n");
}

#define OFFSET(x) offsetof(CuvidContext, x)
#define VD AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_DECODING_PARAM
static const AVOption options[] = {
    { "deint",    "Set deinterlacing mode", OFFSET(deint_mode), AV_OPT_TYPE_INT,   { .i64 = cudaVideoDeinterlaceMode_Weave    }, cudaVideoDeinterlaceMode_Weave, cudaVideoDeinterlaceMode_Adaptive, VD, "deint" },
    { "weave",    "Weave deinterlacing (do nothing)",        0, AV_OPT_TYPE_CONST, { .i64 = cudaVideoDeinterlaceMode_Weave    }, 0, 0, VD, "deint" },
    { "bob",      "Bob deinterlacing",                       0, AV_OPT_TYPE_CONST, { .i64 = cudaVideoDeinterlaceMode_Bob      }, 0, 0, VD, "deint" },
    { "adaptive", "Adaptive deinterlacing",                  0, AV_OPT_TYPE_CONST, { .i64 = cudaVideoDeinterlaceMode_Adaptive }, 0, 0, VD, "deint" },
    { "gpu",      "GPU to be used for decoding", OFFSET(cu_gpu), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, VD },
    { NULL }
};

#define DEFINE_CUVID_CODEC(x, X) \
    static const AVClass x##_cuvid_class = { \
        .class_name = #x "_cuvid", \
        .item_name = av_default_item_name, \
        .option = options, \
        .version = LIBAVUTIL_VERSION_INT, \
    }; \
    AVHWAccel ff_##x##_cuvid_hwaccel = { \
        .name           = #x "_cuvid", \
        .type           = AVMEDIA_TYPE_VIDEO, \
        .id             = AV_CODEC_ID_##X, \
        .pix_fmt        = AV_PIX_FMT_CUDA, \
    }; \
    AVCodec ff_##x##_cuvid_decoder = { \
        .name           = #x "_cuvid", \
        .long_name      = NULL_IF_CONFIG_SMALL("Nvidia CUVID " #X " decoder"), \
        .type           = AVMEDIA_TYPE_VIDEO, \
        .id             = AV_CODEC_ID_##X, \
        .priv_data_size = sizeof(CuvidContext), \
        .priv_class     = &x##_cuvid_class, \
        .init           = cuvid_decode_init, \
        .close          = cuvid_decode_end, \
        .decode         = cuvid_decode_frame, \
        .send_packet    = cuvid_decode_packet, \
        .receive_frame  = cuvid_output_frame, \
        .flush          = cuvid_flush, \
        .capabilities   = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_AVOID_PROBING, \
        .pix_fmts       = (const enum AVPixelFormat[]){ AV_PIX_FMT_CUDA, \
                                                        AV_PIX_FMT_NV12, \
                                                        AV_PIX_FMT_NONE }, \
    };

#if CONFIG_HEVC_CUVID_DECODER
DEFINE_CUVID_CODEC(hevc, HEVC)
#endif

#if CONFIG_H263_CUVID_DECODER
DEFINE_CUVID_CODEC(h263, H263)
#endif

#if CONFIG_H264_CUVID_DECODER
DEFINE_CUVID_CODEC(h264, H264)
#endif

#if CONFIG_MJPEG_CUVID_DECODER
DEFINE_CUVID_CODEC(mjpeg, MJPEG)
#endif

#if CONFIG_MPEG1_CUVID_DECODER
DEFINE_CUVID_CODEC(mpeg1, MPEG1VIDEO)
#endif

#if CONFIG_MPEG2_CUVID_DECODER
DEFINE_CUVID_CODEC(mpeg2, MPEG2VIDEO)
#endif

#if CONFIG_MPEG4_CUVID_DECODER
DEFINE_CUVID_CODEC(mpeg4, MPEG4)
#endif

#if CONFIG_VP8_CUVID_DECODER
DEFINE_CUVID_CODEC(vp8, VP8)
#endif

#if CONFIG_VP9_CUVID_DECODER
DEFINE_CUVID_CODEC(vp9, VP9)
#endif

#if CONFIG_VC1_CUVID_DECODER
DEFINE_CUVID_CODEC(vc1, VC1)
#endif
