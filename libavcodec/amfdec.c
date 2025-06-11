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

#include "libavutil/hwcontext_amf.h"
#include "libavutil/hwcontext_amf_internal.h"
#include "amfdec.h"
#include "codec_internal.h"
#include "hwconfig.h"
#include "libavutil/imgutils.h"
#include "libavutil/mem.h"
#include "libavutil/time.h"
#include "decode.h"
#include "decode_bsf.h"
#include "libavutil/mastering_display_metadata.h"

#if CONFIG_D3D11VA
#include "libavutil/hwcontext_d3d11va.h"
#endif
#if CONFIG_DXVA2
#define COBJMACROS
#include "libavutil/hwcontext_dxva2.h"
#endif

#ifdef _WIN32
#include "compat/w32dlfcn.h"
#else
#include <dlfcn.h>
#endif
//will be in public headers soon
#define AMF_VIDEO_DECODER_OUTPUT_FORMAT                L"OutputDecodeFormat"

const enum AVPixelFormat amf_dec_pix_fmts[] = {
    AV_PIX_FMT_NV12,
    AV_PIX_FMT_P010,
    AV_PIX_FMT_P012,
    AV_PIX_FMT_AMF_SURFACE,
    AV_PIX_FMT_NONE
};

static const AVCodecHWConfigInternal *const amf_hw_configs[] = {
    &(const AVCodecHWConfigInternal) {
        .public = {
            .pix_fmt     = AV_PIX_FMT_AMF_SURFACE,
            .methods     = AV_CODEC_HW_CONFIG_METHOD_HW_FRAMES_CTX |
                           AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX,
            .device_type = AV_HWDEVICE_TYPE_AMF,
        },
        .hwaccel = NULL,
    },
    NULL
};

static void amf_free_amfsurface(void *opaque, uint8_t *data)
{
    AMFSurface *surface = (AMFSurface*)(data);
    surface->pVtbl->Release(surface);
}

static int amf_legacy_driver_no_bitness_detect(AVAMFDeviceContext    *amf_device_ctx)
{
    if( AMF_GET_MAJOR_VERSION(amf_device_ctx->version) <= 1 &&
        AMF_GET_MINOR_VERSION(amf_device_ctx->version) <= 4 &&
        AMF_GET_SUBMINOR_VERSION(amf_device_ctx->version) < 36)
        return 1;
    return 0;
}

static int amf_init_decoder(AVCodecContext *avctx)
{
    AMFDecoderContext     *ctx = avctx->priv_data;
    AVHWDeviceContext     *hw_device_ctx = (AVHWDeviceContext*)ctx->device_ctx_ref->data;
    AVAMFDeviceContext    *amf_device_ctx = (AVAMFDeviceContext*)hw_device_ctx->hwctx;
    const wchar_t           *codec_id = NULL;
    AMF_RESULT              res;
    AMFBuffer               *buffer;
    amf_int64               color_profile;
    int                     pool_size = 36;

    ctx->drain = 0;
    ctx->resolution_changed = 0;

    switch (avctx->codec->id) {
        case AV_CODEC_ID_H264:
            codec_id = AMFVideoDecoderUVD_H264_AVC;
            break;
        case AV_CODEC_ID_HEVC: {
            codec_id = AMFVideoDecoderHW_H265_HEVC;
            // way-around for older drivers that don't support dynamic butness detection -
            // define HEVC 10-bit based on container info
            if(amf_legacy_driver_no_bitness_detect(amf_device_ctx)){
                if(avctx->pix_fmt == AV_PIX_FMT_YUV420P10)
                    codec_id = AMFVideoDecoderHW_H265_MAIN10;
            }

        } break;
        case AV_CODEC_ID_AV1:
            codec_id = AMFVideoDecoderHW_AV1;
            break;
        default:
            break;
    }
    AMF_RETURN_IF_FALSE(ctx, codec_id != NULL, AVERROR(EINVAL), "Codec %d is not supported\n", avctx->codec->id);

    res = amf_device_ctx->factory->pVtbl->CreateComponent(amf_device_ctx->factory, amf_device_ctx->context, codec_id, &ctx->decoder);
    AMF_RETURN_IF_FALSE(ctx, res == AMF_OK, AVERROR_ENCODER_NOT_FOUND, "CreateComponent(%ls) failed with error %d\n", codec_id, res);

    // Color Metadata
    /// Color Range (Support for older Drivers)
    if (avctx->color_range == AVCOL_RANGE_JPEG) {
        AMF_ASSIGN_PROPERTY_BOOL(res, ctx->decoder, AMF_VIDEO_DECODER_FULL_RANGE_COLOR, 1);
    } else if (avctx->color_range != AVCOL_RANGE_UNSPECIFIED) {
        AMF_ASSIGN_PROPERTY_BOOL(res, ctx->decoder, AMF_VIDEO_DECODER_FULL_RANGE_COLOR, 0);
    }
    color_profile = AMF_VIDEO_CONVERTER_COLOR_PROFILE_UNKNOWN;
    switch (avctx->colorspace) {
    case AVCOL_SPC_SMPTE170M:
        if (avctx->color_range == AVCOL_RANGE_JPEG) {
            color_profile = AMF_VIDEO_CONVERTER_COLOR_PROFILE_FULL_601;
        } else {
            color_profile = AMF_VIDEO_CONVERTER_COLOR_PROFILE_601;
        }
        break;
    case AVCOL_SPC_BT709:
        if (avctx->color_range == AVCOL_RANGE_JPEG) {
            color_profile = AMF_VIDEO_CONVERTER_COLOR_PROFILE_FULL_709;
        } else {
            color_profile = AMF_VIDEO_CONVERTER_COLOR_PROFILE_709;
        }
        break;
    case AVCOL_SPC_BT2020_NCL:
    case AVCOL_SPC_BT2020_CL:
        if (avctx->color_range == AVCOL_RANGE_JPEG) {
            color_profile = AMF_VIDEO_CONVERTER_COLOR_PROFILE_FULL_2020;
        } else {
            color_profile = AMF_VIDEO_CONVERTER_COLOR_PROFILE_2020;
        }
        break;
    }
    if (color_profile != AMF_VIDEO_CONVERTER_COLOR_PROFILE_UNKNOWN)
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->decoder, AMF_VIDEO_DECODER_COLOR_PROFILE, color_profile);
    if (avctx->color_trc != AVCOL_TRC_UNSPECIFIED)
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->decoder, AMF_VIDEO_DECODER_COLOR_TRANSFER_CHARACTERISTIC, (amf_int64)avctx->color_trc);

    if (avctx->color_primaries != AVCOL_PRI_UNSPECIFIED)
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->decoder, AMF_VIDEO_DECODER_COLOR_PRIMARIES, (amf_int64)avctx->color_primaries);

    if (ctx->timestamp_mode != -1)
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->decoder, AMF_TIMESTAMP_MODE, ctx->timestamp_mode);
    if (ctx->decoder_mode != -1)
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->decoder, AMF_VIDEO_DECODER_REORDER_MODE, ctx->decoder_mode);
    if (ctx->dpb_size != -1)
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->decoder, AMF_VIDEO_DECODER_DPB_SIZE, ctx->dpb_size);
    if (ctx->lowlatency != -1)
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->decoder, AMF_VIDEO_DECODER_LOW_LATENCY, ctx->lowlatency);
    if (ctx->smart_access_video != -1) {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->decoder, AMF_VIDEO_DECODER_ENABLE_SMART_ACCESS_VIDEO, ctx->smart_access_video != 0);
        if (res != AMF_OK) {
            av_log(avctx, AV_LOG_ERROR, "The Smart Access Video is not supported by AMF decoder.\n");
            return AVERROR(EINVAL);
        } else {
            av_log(avctx, AV_LOG_INFO, "The Smart Access Video (%d) is set.\n", ctx->smart_access_video);
            // Set low latency mode if Smart Access Video is enabled
            if (ctx->smart_access_video != 0) {
                AMF_ASSIGN_PROPERTY_INT64(res, ctx->decoder, AMF_VIDEO_DECODER_LOW_LATENCY, true);
                av_log(avctx, AV_LOG_INFO, "The Smart Access Video set low latency mode for decoder.\n");
            }
        }
    }
    if (ctx->skip_transfer_sav != -1)
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->decoder, AMF_VIDEO_DECODER_SKIP_TRANSFER_SMART_ACCESS_VIDEO, ctx->skip_transfer_sav);

    if (ctx->copy_output != -1)
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->decoder, AMF_VIDEO_DECODER_SURFACE_COPY, ctx->copy_output);

    if (avctx->extradata_size) {
        const uint8_t *extradata;
        int extradata_size;
        ff_decode_get_extradata(avctx, &extradata, &extradata_size);
        res = amf_device_ctx->context->pVtbl->AllocBuffer(amf_device_ctx->context, AMF_MEMORY_HOST, extradata_size, &buffer);
        if (res == AMF_OK) {
            memcpy(buffer->pVtbl->GetNative(buffer), extradata, extradata_size);
            AMF_ASSIGN_PROPERTY_INTERFACE(res,ctx->decoder, AMF_VIDEO_DECODER_EXTRADATA, buffer);
            buffer->pVtbl->Release(buffer);
            buffer = NULL;
        }
    }
    if (ctx->surface_pool_size == -1) {
        ctx->surface_pool_size = pool_size;
        if (avctx->extra_hw_frames > 0)
            ctx->surface_pool_size += avctx->extra_hw_frames;
        if (avctx->active_thread_type & FF_THREAD_FRAME)
            ctx->surface_pool_size += avctx->thread_count;
    }

    //at the moment, there is such a restriction in AMF.
    //when it is possible, I will remove this code
    if (ctx->surface_pool_size > 100)
        ctx->surface_pool_size = 100;

    AMF_ASSIGN_PROPERTY_INT64(res, ctx->decoder, AMF_VIDEO_DECODER_SURFACE_POOL_SIZE, ctx->surface_pool_size);
    res = ctx->decoder->pVtbl->Init(ctx->decoder, AMF_SURFACE_UNKNOWN, avctx->width, avctx->height);
    if (res != AMF_OK) {
        av_log(avctx, AV_LOG_ERROR, "Decoder initialization failed with error %d\n", res);
        return AVERROR(EINVAL);
    }
    return 0;
}

static int amf_decode_close(AVCodecContext *avctx)
{
    AMFDecoderContext *ctx = avctx->priv_data;

    if (ctx->decoder) {
        ctx->decoder->pVtbl->Terminate(ctx->decoder);
        ctx->decoder->pVtbl->Release(ctx->decoder);
        ctx->decoder = NULL;
    }

    av_buffer_unref(&ctx->device_ctx_ref);
    av_packet_free(&ctx->in_pkt);

    return 0;
}

static int amf_init_frames_context(AVCodecContext *avctx, int sw_format, int new_width, int new_height)
{
    int ret;
    AVHWDeviceContext *hwdev_ctx;
    AVHWFramesContext *hwframes_ctx;
    AMFDecoderContext *ctx;
    if (!avctx->hw_frames_ctx || !avctx->hw_device_ctx)
        return 0;
    hwdev_ctx = (AVHWDeviceContext*)avctx->hw_device_ctx->data;
    hwframes_ctx = (AVHWFramesContext*)avctx->hw_frames_ctx->data;
    ctx = avctx->priv_data;

    if (hwdev_ctx->type != AV_HWDEVICE_TYPE_AMF)
        return 0;

    hwframes_ctx->width             = new_width;
    hwframes_ctx->height            = new_height;
    hwframes_ctx->format            = AV_PIX_FMT_AMF_SURFACE;
    hwframes_ctx->sw_format         = sw_format;
    hwframes_ctx->initial_pool_size = ctx->surface_pool_size + 8;

    ret = av_hwframe_ctx_init(avctx->hw_frames_ctx);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error initializing a AMF frame pool\n");
        av_buffer_unref(&avctx->hw_frames_ctx);
        return ret;
    }
    return 0;
}

static int amf_decode_init(AVCodecContext *avctx)
{
    AMFDecoderContext *ctx = avctx->priv_data;
    int ret;
    ctx->in_pkt = av_packet_alloc();
    if (!ctx->in_pkt)
        return AVERROR(ENOMEM);

    if  (avctx->hw_device_ctx && !avctx->hw_frames_ctx) {
        AVHWDeviceContext   *hwdev_ctx;
        hwdev_ctx = (AVHWDeviceContext*)avctx->hw_device_ctx->data;
        if (hwdev_ctx->type == AV_HWDEVICE_TYPE_AMF)
        {
            ctx->device_ctx_ref = av_buffer_ref(avctx->hw_device_ctx);
            avctx->hw_frames_ctx = av_hwframe_ctx_alloc(avctx->hw_device_ctx);

            AMF_GOTO_FAIL_IF_FALSE(avctx, !!avctx->hw_frames_ctx, AVERROR(ENOMEM), "av_hwframe_ctx_alloc failed\n");
        } else {
            ret = av_hwdevice_ctx_create_derived(&ctx->device_ctx_ref, AV_HWDEVICE_TYPE_AMF, avctx->hw_device_ctx, 0);
            AMF_GOTO_FAIL_IF_FALSE(avctx, ret == 0, ret, "Failed to create derived AMF device context: %s\n", av_err2str(ret));
        }
    } else {
        ret = av_hwdevice_ctx_create(&ctx->device_ctx_ref, AV_HWDEVICE_TYPE_AMF, NULL, NULL, 0);
        AMF_GOTO_FAIL_IF_FALSE(avctx, ret == 0, ret, "Failed to create  hardware device context (AMF) : %s\n", av_err2str(ret));
    }
    if ((ret = amf_init_decoder(avctx)) == 0) {
        AVHWDeviceContext     *hw_device_ctx = (AVHWDeviceContext*)ctx->device_ctx_ref->data;
        AVAMFDeviceContext    *amf_device_ctx = (AVAMFDeviceContext*)hw_device_ctx->hwctx;
        enum AVPixelFormat    surf_pix_fmt = AV_PIX_FMT_NONE;

        if(amf_legacy_driver_no_bitness_detect(amf_device_ctx)){
            // if bitness detection is not supported in legacy driver use format from container
            switch (avctx->pix_fmt) {
            case AV_PIX_FMT_YUV420P:
            case AV_PIX_FMT_YUVJ420P:
                surf_pix_fmt = AV_PIX_FMT_NV12; break;
            case AV_PIX_FMT_YUV420P10:
                surf_pix_fmt = AV_PIX_FMT_P010; break;
            }
        }else{
            AMFVariantStruct format_var = {0};

            ret = ctx->decoder->pVtbl->GetProperty(ctx->decoder, AMF_VIDEO_DECODER_OUTPUT_FORMAT, &format_var);
            AMF_GOTO_FAIL_IF_FALSE(avctx, ret == AMF_OK, AVERROR(EINVAL), "Failed to get output format (AMF) : %d\n", ret);

            surf_pix_fmt = av_amf_to_av_format(format_var.int64Value);
        }
        if(avctx->hw_frames_ctx)
        {
            // this values should be set for avcodec_open2
            // will be updated after header decoded if not true.
            if(surf_pix_fmt == AV_PIX_FMT_NONE)
                surf_pix_fmt = AV_PIX_FMT_NV12; // for older drivers
            if (!avctx->coded_width)
                avctx->coded_width = 1280;
            if (!avctx->coded_height)
                avctx->coded_height = 720;
            ret = amf_init_frames_context(avctx, surf_pix_fmt, avctx->coded_width, avctx->coded_height);
            AMF_GOTO_FAIL_IF_FALSE(avctx, ret == 0, ret, "Failed to init frames context (AMF) : %s\n", av_err2str(ret));
        }
        else
            avctx->pix_fmt = surf_pix_fmt;

        return 0;
    }
fail:
    amf_decode_close(avctx);
    return ret;
}

static AMF_RESULT amf_get_property_buffer(AMFData *object, const wchar_t *name, AMFBuffer **val)
{
    AMF_RESULT res;
    AMFVariantStruct var;
    res = AMFVariantInit(&var);
    if (res == AMF_OK) {
        res = object->pVtbl->GetProperty(object, name, &var);
        if (res == AMF_OK) {
            if (var.type == AMF_VARIANT_INTERFACE) {
                AMFGuid guid_AMFBuffer = IID_AMFBuffer();
                AMFInterface *amf_interface = AMFVariantInterface(&var);
                res = amf_interface->pVtbl->QueryInterface(amf_interface, &guid_AMFBuffer, (void**)val);
            } else {
                res = AMF_INVALID_DATA_TYPE;
            }
        }
        AMFVariantClear(&var);
    }
    return res;
}

static int amf_amfsurface_to_avframe(AVCodecContext *avctx, AMFSurface* surface, AVFrame *frame)
{
    AMFVariantStruct    var = {0};
    AMFPlane            *plane;
    int                 i;
    int                 ret;
    int                 format_amf;

    if (avctx->hw_device_ctx && ((AVHWDeviceContext*)avctx->hw_device_ctx->data)->type == AV_HWDEVICE_TYPE_AMF) {
        // prepare frame similar to  ff_get_buffer(avctx, frame, AV_GET_BUFFER_FLAG_REF);

        ret = ff_decode_frame_props(avctx, frame);
        if (ret < 0)
            return ret;

        avctx->sw_pix_fmt = avctx->pix_fmt;

        ret = ff_attach_decode_data(frame);
        if (ret < 0)
            return ret;
        frame->width  = avctx->width;
        frame->height = avctx->height;

        ////
        frame->buf[0] = av_buffer_create((uint8_t *)surface, sizeof(surface),
                                    amf_free_amfsurface, (void*)avctx,
                                    AV_BUFFER_FLAG_READONLY);
        AMF_RETURN_IF_FALSE(avctx, !!frame->buf[0], AVERROR(ENOMEM), "av_buffer_create for amf surface failed.");

        frame->data[0] = (uint8_t *)surface;
        frame->format =  AV_PIX_FMT_AMF_SURFACE;
        format_amf = surface->pVtbl->GetFormat(surface);
        avctx->sw_pix_fmt = av_amf_to_av_format(format_amf);
        frame->hw_frames_ctx = av_buffer_ref(avctx->hw_frames_ctx);
    } else {
        ret = surface->pVtbl->Convert(surface, AMF_MEMORY_HOST);
        AMF_RETURN_IF_FALSE(avctx, ret == AMF_OK, AVERROR_UNKNOWN, "Convert(amf::AMF_MEMORY_HOST) failed with error %d\n", ret);

        for (i = 0; i < surface->pVtbl->GetPlanesCount(surface); i++) {
            plane = surface->pVtbl->GetPlaneAt(surface, i);
            frame->data[i] = plane->pVtbl->GetNative(plane);
            frame->linesize[i] = plane->pVtbl->GetHPitch(plane);
        }

        frame->buf[0] = av_buffer_create((uint8_t *)surface, sizeof(surface),
                                     amf_free_amfsurface, (void*)avctx,
                                     AV_BUFFER_FLAG_READONLY);
        AMF_RETURN_IF_FALSE(avctx, !!frame->buf[0], AVERROR(ENOMEM), "av_buffer_create for amf surface failed.");

        format_amf = surface->pVtbl->GetFormat(surface);
        frame->format = av_amf_to_av_format(format_amf);
    }

    frame->width  = avctx->width;
    frame->height = avctx->height;

    frame->pts = surface->pVtbl->GetPts(surface);

    surface->pVtbl->GetProperty(surface, L"FFMPEG:dts", &var);
    frame->pkt_dts = var.int64Value;

    frame->duration = surface->pVtbl->GetDuration(surface);
    if (frame->duration < 0)
        frame->duration = 0;

    frame->color_range = avctx->color_range;
    frame->colorspace = avctx->colorspace;
    frame->color_trc = avctx->color_trc;
    frame->color_primaries = avctx->color_primaries;

    if (frame->color_trc == AVCOL_TRC_SMPTE2084) {
        AMFBuffer * hdrmeta_buffer = NULL;
        ret = amf_get_property_buffer((AMFData *)surface, AMF_VIDEO_DECODER_HDR_METADATA, &hdrmeta_buffer);
        if (hdrmeta_buffer != NULL) {
            AMFHDRMetadata * hdrmeta = (AMFHDRMetadata*)hdrmeta_buffer->pVtbl->GetNative(hdrmeta_buffer);
            if (ret != AMF_OK)
                return ret;
            if (hdrmeta != NULL) {
                AVMasteringDisplayMetadata *mastering = av_mastering_display_metadata_create_side_data(frame);
                const int chroma_den = 50000;
                const int luma_den = 10000;

                if (!mastering)
                    return AVERROR(ENOMEM);

                mastering->display_primaries[0][0] = av_make_q(hdrmeta->redPrimary[0], chroma_den);
                mastering->display_primaries[0][1] = av_make_q(hdrmeta->redPrimary[1], chroma_den);

                mastering->display_primaries[1][0] = av_make_q(hdrmeta->greenPrimary[0], chroma_den);
                mastering->display_primaries[1][1] = av_make_q(hdrmeta->greenPrimary[1], chroma_den);

                mastering->display_primaries[2][0] = av_make_q(hdrmeta->bluePrimary[0], chroma_den);
                mastering->display_primaries[2][1] = av_make_q(hdrmeta->bluePrimary[1], chroma_den);

                mastering->white_point[0] = av_make_q(hdrmeta->whitePoint[0], chroma_den);
                mastering->white_point[1] = av_make_q(hdrmeta->whitePoint[1], chroma_den);

                mastering->max_luminance = av_make_q(hdrmeta->maxMasteringLuminance, luma_den);
                mastering->min_luminance = av_make_q(hdrmeta->maxMasteringLuminance, luma_den);

                mastering->has_luminance = 1;
                mastering->has_primaries = 1;
                if (hdrmeta->maxContentLightLevel) {
                   AVContentLightMetadata *light = av_content_light_metadata_create_side_data(frame);

                    if (!light)
                        return AVERROR(ENOMEM);

                    light->MaxCLL  = hdrmeta->maxContentLightLevel;
                    light->MaxFALL = hdrmeta->maxFrameAverageLightLevel;
                }
            }
        }
    }
    return 0;
}

static AMF_RESULT amf_receive_frame(AVCodecContext *avctx, AVFrame *frame)
{
    AMFDecoderContext *ctx = avctx->priv_data;
    AMF_RESULT          ret = AMF_OK;
    AMFSurface          *surface = NULL;
    AMFData             *data_out = NULL;

    ret = ctx->decoder->pVtbl->QueryOutput(ctx->decoder, &data_out);
    if (ret != AMF_OK && ret != AMF_REPEAT) {
        return ret;
    }
    if (data_out == NULL) {
        return AMF_REPEAT;
    }

    if (data_out) {
        AMFGuid guid = IID_AMFSurface();
        data_out->pVtbl->QueryInterface(data_out, &guid, (void**)&surface); // query for buffer interface
        data_out->pVtbl->Release(data_out);
        data_out = NULL;
    }

    ret = amf_amfsurface_to_avframe(avctx, surface, frame);
    AMF_GOTO_FAIL_IF_FALSE(avctx, ret >= 0, AMF_FAIL, "Failed to convert AMFSurface to AVFrame = %d\n", ret);
    return AMF_OK;
fail:

    if (surface) {
        surface->pVtbl->Release(surface);
        surface = NULL;
    }
    return ret;
}

static AMF_RESULT amf_update_buffer_properties(AVCodecContext *avctx, AMFBuffer* buffer, const AVPacket* pkt)
{
    AMF_RESULT          res;

    AMF_RETURN_IF_FALSE(avctx, buffer != NULL, AMF_INVALID_ARG, "update_buffer_properties() - buffer not passed in");
    AMF_RETURN_IF_FALSE(avctx, pkt != NULL, AMF_INVALID_ARG, "update_buffer_properties() - packet not passed in");
    buffer->pVtbl->SetPts(buffer, pkt->pts);
    buffer->pVtbl->SetDuration(buffer, pkt->duration);
    AMF_ASSIGN_PROPERTY_INT64(res, buffer, L"FFMPEG:dts", pkt->dts);
    if (res != AMF_OK)
        av_log(avctx, AV_LOG_VERBOSE, "Failed to assign dts value.");
    return AMF_OK;
}

static AMF_RESULT amf_buffer_from_packet(AVCodecContext *avctx, const AVPacket* pkt, AMFBuffer** buffer)
{
    AMFDecoderContext *ctx = avctx->priv_data;
    AVHWDeviceContext *hw_device_ctx = (AVHWDeviceContext*)ctx->device_ctx_ref->data;
    AVAMFDeviceContext *amf_device_ctx = (AVAMFDeviceContext *)hw_device_ctx->hwctx;
    AMFContext          *ctxt = amf_device_ctx->context;
    void                *mem;
    AMF_RESULT          err;
    AMFBuffer           *buf = NULL;

    AMF_RETURN_IF_FALSE(ctxt, pkt != NULL, AMF_INVALID_ARG, "amf_buffer_from_packet() - packet not passed in");
    AMF_RETURN_IF_FALSE(ctxt, buffer != NULL, AMF_INVALID_ARG, "amf_buffer_from_packet() - buffer pointer not passed in");

    err = ctxt->pVtbl->AllocBuffer(ctxt, AMF_MEMORY_HOST, pkt->size + AV_INPUT_BUFFER_PADDING_SIZE, buffer);
    AMF_RETURN_IF_FALSE(ctxt, err == AMF_OK, err, "amf_buffer_from_packet() -   failed");
    buf = *buffer;
    err = buf->pVtbl->SetSize(buf, pkt->size);
    AMF_RETURN_IF_FALSE(ctxt, err == AMF_OK, err, "amf_buffer_from_packet() - SetSize failed");
    // get the memory location and check the buffer was indeed allocated
    mem = buf->pVtbl->GetNative(buf);
    AMF_RETURN_IF_FALSE(ctxt, mem != NULL, AMF_INVALID_POINTER, "amf_buffer_from_packet() - GetNative failed");

    // copy the packet memory and clear data padding
    memcpy(mem, pkt->data, pkt->size);
    memset((amf_int8*)(mem)+pkt->size, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    return amf_update_buffer_properties(avctx, buf, pkt);
}

static int amf_decode_frame(AVCodecContext *avctx, struct AVFrame *frame)
{
    AMFDecoderContext *ctx = avctx->priv_data;
    AMFBuffer           *buf;
    AMF_RESULT          res;
    int                 got_frame = 0;
    AVPacket            *avpkt = ctx->in_pkt;

    if (!ctx->decoder)
        return AVERROR(EINVAL);

    // get packet if needed
    if(!ctx->drain){
        if(ctx->resolution_changed)
            ctx->resolution_changed = 0;
        else{
            int ret;
            av_packet_unref(avpkt);
            ret = ff_decode_get_packet(avctx, avpkt);
            if (ret < 0 && ret != AVERROR_EOF)
                return ret;
            if (ret == AVERROR_EOF) {
                //nothing to consume, start external drain
                ctx->decoder->pVtbl->Drain(ctx->decoder);
                ctx->drain = 1;
            }
        }
    }

    if(!ctx->drain){
        // submit frame
        res = amf_buffer_from_packet(avctx, avpkt, &buf);
        AMF_RETURN_IF_FALSE(avctx, res == AMF_OK, 0, "Cannot convert AVPacket to AMFbuffer");
        do{
            res = ctx->decoder->pVtbl->SubmitInput(ctx->decoder, (AMFData*) buf);
            if(res == AMF_DECODER_NO_FREE_SURFACES)
            {
                av_usleep(100);
            }
        } while (res == AMF_DECODER_NO_FREE_SURFACES);

        buf->pVtbl->Release(buf);

        if(res == AMF_DECODER_NO_FREE_SURFACES) {
            // input is not consumed, need to QueryOutput and submit again
            av_log(avctx, AV_LOG_VERBOSE, "SubmitInput() returned NO_FREE_SURFACES and came out of loop - should never happen\n");
            res = AMF_OK;
        } else if (res == AMF_RESOLUTION_CHANGED) {
            //input is not consumed, start internal drain
            ctx->decoder->pVtbl->Drain(ctx->decoder);
            ctx->drain = 1;
            // process resolution_changed when internal drain is complete
            ctx->resolution_changed = 1;
            res = AMF_OK;
        } else if (res != AMF_OK && res != AMF_NEED_MORE_INPUT && res != AMF_REPEAT) {
            av_log(avctx, AV_LOG_ERROR, "SubmitInput() returned error %d\n", res);
            return AVERROR(EINVAL);
        }
    }

    res = amf_receive_frame(avctx, frame);
    if (res == AMF_OK)
        got_frame = 1;
    else if (res == AMF_REPEAT)
        // decoder has no output yet
        res = AMF_OK;
    else if (res == AMF_EOF) {
        // drain is complete
        ctx->drain = 0;
        if(ctx->resolution_changed){
            // re-initialze decoder
            AMFVariantStruct    size_var = {0};
            AMFVariantStruct    format_var = {0};
            res = ctx->decoder->pVtbl->GetProperty(ctx->decoder, AMF_VIDEO_DECODER_CURRENT_SIZE, &size_var);
            if (res != AMF_OK) {
                return AVERROR(EINVAL);
            }

            avctx->width = size_var.sizeValue.width;
            avctx->height = size_var.sizeValue.height;
            avctx->coded_width  = size_var.sizeValue.width;
            avctx->coded_height = size_var.sizeValue.height;
            res = ctx->decoder->pVtbl->ReInit(ctx->decoder, avctx->width, avctx->height);
            if (res != AMF_OK) {
                av_log(avctx, AV_LOG_ERROR, "ReInit() returned %d\n", res);
                return AVERROR(EINVAL);
            }
            res = ctx->decoder->pVtbl->GetProperty(ctx->decoder, AMF_VIDEO_DECODER_OUTPUT_FORMAT, &format_var);
            if (res == AMF_OK) {
                res = amf_init_frames_context(avctx, av_amf_to_av_format(format_var.int64Value), avctx->coded_width, avctx->coded_height);
            }

            if (res < 0)
                return AVERROR(EINVAL);
        }else
            return AVERROR_EOF;
    } else {
        av_log(avctx, AV_LOG_ERROR, "Unkown result from QueryOutput %d\n", res);
    }
    return got_frame ? 0 : AVERROR(EAGAIN);
}

static void amf_decode_flush(AVCodecContext *avctx)
{
    AMFDecoderContext *ctx = avctx->priv_data;
    ctx->decoder->pVtbl->Flush(ctx->decoder);
}

#define OFFSET(x) offsetof(AMFDecoderContext, x)
#define VD AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_DECODING_PARAM

static const AVOption options[] = {
    // Decoder mode
    { "decoder_mode",       "Decoder mode",                                                 OFFSET(decoder_mode),       AV_OPT_TYPE_INT,   { .i64 = -1  }, -1, AMF_VIDEO_DECODER_MODE_LOW_LATENCY, VD, "decoder_mode" },
    { "regular",            "DPB delay is based on number of reference frames + 1",         0,                          AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_DECODER_MODE_REGULAR      }, 0, 0, VD, "decoder_mode" },
    { "compliant",          "DPB delay is based on profile - up to 16",                     0,                          AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_DECODER_MODE_COMPLIANT    }, 0, 0, VD, "decoder_mode" },
    { "low_latency",        "DPB delay is 0",                                               0,                          AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_DECODER_MODE_LOW_LATENCY  }, 0, 0, VD, "decoder_mode" },

    // Timestamp mode
    { "timestamp_mode",     "Timestamp mode",                                               OFFSET(timestamp_mode),     AV_OPT_TYPE_INT,   { .i64 = AMF_TS_SORT }, -1, AMF_TS_DECODE, VD, "timestamp_mode" },
    { "presentation",       "Preserve timestamps from input to output",                     0,                          AV_OPT_TYPE_CONST, { .i64 = AMF_TS_PRESENTATION }, 0, 0, VD, "timestamp_mode" },
    { "sort",               "Resort PTS list",                                              0,                          AV_OPT_TYPE_CONST, { .i64 = AMF_TS_SORT         }, 0, 0, VD, "timestamp_mode" },
    { "decode",             "Decode order",                                                 0,                          AV_OPT_TYPE_CONST, { .i64 = AMF_TS_DECODE       }, 0, 0, VD, "timestamp_mode" },

    // Reference frame management
    { "surface_pool_size",  "Number of surfaces in the decode pool",                        OFFSET(surface_pool_size),  AV_OPT_TYPE_INT,  { .i64 = -1 }, -1, INT_MAX, VD, NULL },
    { "dpb_size",           "Minimum number of surfaces for reordering",                    OFFSET(dpb_size),           AV_OPT_TYPE_INT,  { .i64 = -1 }, -1, 32, VD, NULL },

    { "lowlatency",         "Low latency",                                                  OFFSET(lowlatency),         AV_OPT_TYPE_INT, { .i64 = -1 }, -1, 1, VD, NULL },
    { "smart_access_video", "Smart Access Video",                                           OFFSET(smart_access_video), AV_OPT_TYPE_INT, { .i64 = -1 }, -1, 1, VD, NULL },
    { "skip_transfer_sav",  "Skip transfer on another GPU when SAV enabled",                OFFSET(skip_transfer_sav),  AV_OPT_TYPE_INT, { .i64 = -1 }, -1, 1, VD, NULL },
    { "copy_output",        "Copy Output",                                                  OFFSET(copy_output),        AV_OPT_TYPE_INT, { .i64 = -1 }, -1, 1, VD, NULL },

    { NULL }
};

static const AVClass amf_decode_class = {
    .class_name = "amf",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

#define DEFINE_AMF_DECODER(x, X, bsf_name) \
const FFCodec ff_##x##_amf_decoder = { \
    .p.name         = #x "_amf", \
    CODEC_LONG_NAME(#X " AMD AMF video decoder"), \
    .priv_data_size = sizeof(AMFDecoderContext), \
    .p.type         = AVMEDIA_TYPE_VIDEO, \
    .p.id           = AV_CODEC_ID_##X, \
    .init           = amf_decode_init, \
    FF_CODEC_RECEIVE_FRAME_CB(amf_decode_frame), \
    .flush          = amf_decode_flush, \
    .close          = amf_decode_close, \
    .bsfs           = bsf_name, \
    .p.capabilities = AV_CODEC_CAP_HARDWARE | AV_CODEC_CAP_DELAY | AV_CODEC_CAP_AVOID_PROBING, \
    .p.priv_class   = &amf_decode_class, \
    CODEC_PIXFMTS_ARRAY(amf_dec_pix_fmts), \
    .hw_configs     = amf_hw_configs, \
    .p.wrapper_name = "amf", \
    .caps_internal  = FF_CODEC_CAP_NOT_INIT_THREADSAFE, \
}; \

DEFINE_AMF_DECODER(h264, H264, "h264_mp4toannexb")
DEFINE_AMF_DECODER(hevc, HEVC, NULL)
DEFINE_AMF_DECODER(av1, AV1, NULL)
