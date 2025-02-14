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

#include "config.h"
#include "config_components.h"

#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_amf.h"
#include "libavutil/hwcontext_amf_internal.h"
#if CONFIG_D3D11VA
#include "libavutil/hwcontext_d3d11va.h"
#endif
#if CONFIG_DXVA2
#define COBJMACROS
#include "libavutil/hwcontext_dxva2.h"
#endif
#include "libavutil/mem.h"
#include "libavutil/pixdesc.h"
#include "libavutil/time.h"

#include "amfenc.h"
#include "encode.h"
#include "internal.h"
#include "libavutil/mastering_display_metadata.h"

#define AMF_AV_FRAME_REF L"av_frame_ref"

static int amf_save_hdr_metadata(AVCodecContext *avctx, const AVFrame *frame, AMFHDRMetadata *hdrmeta)
{
    AVFrameSideData            *sd_display;
    AVFrameSideData            *sd_light;
    AVMasteringDisplayMetadata *display_meta;
    AVContentLightMetadata     *light_meta;

    sd_display = av_frame_get_side_data(frame, AV_FRAME_DATA_MASTERING_DISPLAY_METADATA);
    if (sd_display) {
        display_meta = (AVMasteringDisplayMetadata *)sd_display->data;
        if (display_meta->has_luminance) {
            const unsigned int luma_den = 10000;
            hdrmeta->maxMasteringLuminance =
                (amf_uint32)(luma_den * av_q2d(display_meta->max_luminance));
            hdrmeta->minMasteringLuminance =
                FFMIN((amf_uint32)(luma_den * av_q2d(display_meta->min_luminance)), hdrmeta->maxMasteringLuminance);
        }
        if (display_meta->has_primaries) {
            const unsigned int chroma_den = 50000;
            hdrmeta->redPrimary[0] =
                FFMIN((amf_uint16)(chroma_den * av_q2d(display_meta->display_primaries[0][0])), chroma_den);
            hdrmeta->redPrimary[1] =
                FFMIN((amf_uint16)(chroma_den * av_q2d(display_meta->display_primaries[0][1])), chroma_den);
            hdrmeta->greenPrimary[0] =
                FFMIN((amf_uint16)(chroma_den * av_q2d(display_meta->display_primaries[1][0])), chroma_den);
            hdrmeta->greenPrimary[1] =
                FFMIN((amf_uint16)(chroma_den * av_q2d(display_meta->display_primaries[1][1])), chroma_den);
            hdrmeta->bluePrimary[0] =
                FFMIN((amf_uint16)(chroma_den * av_q2d(display_meta->display_primaries[2][0])), chroma_den);
            hdrmeta->bluePrimary[1] =
                FFMIN((amf_uint16)(chroma_den * av_q2d(display_meta->display_primaries[2][1])), chroma_den);
            hdrmeta->whitePoint[0] =
                FFMIN((amf_uint16)(chroma_den * av_q2d(display_meta->white_point[0])), chroma_den);
            hdrmeta->whitePoint[1] =
                FFMIN((amf_uint16)(chroma_den * av_q2d(display_meta->white_point[1])), chroma_den);
        }

        sd_light = av_frame_get_side_data(frame, AV_FRAME_DATA_CONTENT_LIGHT_LEVEL);
        if (sd_light) {
            light_meta = (AVContentLightMetadata *)sd_light->data;
            if (light_meta) {
                hdrmeta->maxContentLightLevel = (amf_uint16)light_meta->MaxCLL;
                hdrmeta->maxFrameAverageLightLevel = (amf_uint16)light_meta->MaxFALL;
            }
        }
        return 0;
    }
    return 1;
}

#if CONFIG_D3D11VA
#include <d3d11.h>
#endif

#ifdef _WIN32
#include "compat/w32dlfcn.h"
#else
#include <dlfcn.h>
#endif

#define FFMPEG_AMF_WRITER_ID L"ffmpeg_amf"

#define PTS_PROP L"PtsProp"

const enum AVPixelFormat ff_amf_pix_fmts[] = {
    AV_PIX_FMT_NV12,
    AV_PIX_FMT_YUV420P,
#if CONFIG_D3D11VA
    AV_PIX_FMT_D3D11,
#endif
#if CONFIG_DXVA2
    AV_PIX_FMT_DXVA2_VLD,
#endif
    AV_PIX_FMT_P010,
    AV_PIX_FMT_AMF_SURFACE,
    AV_PIX_FMT_BGR0,
    AV_PIX_FMT_RGB0,
    AV_PIX_FMT_BGRA,
    AV_PIX_FMT_ARGB,
    AV_PIX_FMT_RGBA,
    AV_PIX_FMT_X2BGR10,
    AV_PIX_FMT_RGBAF16,
    AV_PIX_FMT_NONE
};

static int amf_init_encoder(AVCodecContext *avctx)
{
    AMFEncoderContext  *ctx = avctx->priv_data;
    const wchar_t      *codec_id = NULL;
    AMF_RESULT          res;
    enum AVPixelFormat  pix_fmt;
    AVHWDeviceContext  *hw_device_ctx = (AVHWDeviceContext*)ctx->device_ctx_ref->data;
    AVAMFDeviceContext *amf_device_ctx = (AVAMFDeviceContext *)hw_device_ctx->hwctx;

    switch (avctx->codec->id) {
        case AV_CODEC_ID_H264:
            codec_id = AMFVideoEncoderVCE_AVC;
            break;
        case AV_CODEC_ID_HEVC:
            codec_id = AMFVideoEncoder_HEVC;
            break;
        case AV_CODEC_ID_AV1 :
            codec_id = AMFVideoEncoder_AV1;
            break;
        default:
            break;
    }
    AMF_RETURN_IF_FALSE(ctx, codec_id != NULL, AVERROR(EINVAL), "Codec %d is not supported\n", avctx->codec->id);

    if (avctx->hw_frames_ctx)
        pix_fmt = ((AVHWFramesContext*)avctx->hw_frames_ctx->data)->sw_format;
    else
        pix_fmt = avctx->pix_fmt;

    if (pix_fmt == AV_PIX_FMT_P010) {
        AMF_RETURN_IF_FALSE(ctx, amf_device_ctx->version >= AMF_MAKE_FULL_VERSION(1, 4, 32, 0), AVERROR_UNKNOWN, "10-bit encoder is not supported by AMD GPU drivers versions lower than 23.30.\n");
    }

    ctx->format = av_av_to_amf_format(pix_fmt);
    AMF_RETURN_IF_FALSE(ctx, ctx->format != AMF_SURFACE_UNKNOWN, AVERROR(EINVAL),
                        "Format %s is not supported\n", av_get_pix_fmt_name(pix_fmt));

    res = amf_device_ctx->factory->pVtbl->CreateComponent(amf_device_ctx->factory, amf_device_ctx->context, codec_id, &ctx->encoder);
    AMF_RETURN_IF_FALSE(ctx, res == AMF_OK, AVERROR_ENCODER_NOT_FOUND, "CreateComponent(%ls) failed with error %d\n", codec_id, res);

    ctx->submitted_frame = 0;
    ctx->encoded_frame = 0;
    ctx->eof = 0;

    return 0;
}

int av_cold ff_amf_encode_close(AVCodecContext *avctx)
{
    AMFEncoderContext *ctx = avctx->priv_data;

    if (ctx->encoder) {
        ctx->encoder->pVtbl->Terminate(ctx->encoder);
        ctx->encoder->pVtbl->Release(ctx->encoder);
        ctx->encoder = NULL;
    }

    av_buffer_unref(&ctx->device_ctx_ref);
    av_fifo_freep2(&ctx->timestamp_list);

    return 0;
}

static int amf_copy_surface(AVCodecContext *avctx, const AVFrame *frame,
    AMFSurface* surface)
{
    AMFPlane *plane;
    uint8_t  *dst_data[4] = {0};
    int       dst_linesize[4] = {0};
    int       planes;
    int       i;

    planes = (int)surface->pVtbl->GetPlanesCount(surface);
    av_assert0(planes < FF_ARRAY_ELEMS(dst_data));

    for (i = 0; i < planes; i++) {
        plane = surface->pVtbl->GetPlaneAt(surface, i);
        dst_data[i] = plane->pVtbl->GetNative(plane);
        dst_linesize[i] = plane->pVtbl->GetHPitch(plane);
    }
    av_image_copy2(dst_data, dst_linesize,
                   frame->data, frame->linesize, frame->format,
                   avctx->width, avctx->height);

    return 0;
}

static int amf_copy_buffer(AVCodecContext *avctx, AVPacket *pkt, AMFBuffer *buffer)
{
    AMFEncoderContext *ctx = avctx->priv_data;
    int              ret;
    AMFVariantStruct var = {0};
    int64_t          timestamp = AV_NOPTS_VALUE;
    int64_t          size = buffer->pVtbl->GetSize(buffer);

    if ((ret = ff_get_encode_buffer(avctx, pkt, size, 0)) < 0) {
        return ret;
    }
    memcpy(pkt->data, buffer->pVtbl->GetNative(buffer), size);

    switch (avctx->codec->id) {
        case AV_CODEC_ID_H264:
            buffer->pVtbl->GetProperty(buffer, AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE, &var);
            if(var.int64Value == AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE_IDR) {
                pkt->flags = AV_PKT_FLAG_KEY;
            }
            break;
        case AV_CODEC_ID_HEVC:
            buffer->pVtbl->GetProperty(buffer, AMF_VIDEO_ENCODER_HEVC_OUTPUT_DATA_TYPE, &var);
            if (var.int64Value == AMF_VIDEO_ENCODER_HEVC_OUTPUT_DATA_TYPE_IDR) {
                pkt->flags = AV_PKT_FLAG_KEY;
            }
            break;
        case AV_CODEC_ID_AV1:
            buffer->pVtbl->GetProperty(buffer, AMF_VIDEO_ENCODER_AV1_OUTPUT_FRAME_TYPE, &var);
            if (var.int64Value == AMF_VIDEO_ENCODER_AV1_OUTPUT_FRAME_TYPE_KEY) {
                pkt->flags = AV_PKT_FLAG_KEY;
            }
        default:
            break;
    }

    buffer->pVtbl->GetProperty(buffer, PTS_PROP, &var);

    pkt->pts = var.int64Value; // original pts

    AMF_RETURN_IF_FALSE(ctx, av_fifo_read(ctx->timestamp_list, &timestamp, 1) >= 0,
                        AVERROR_UNKNOWN, "timestamp_list is empty\n");

    // calc dts shift if max_b_frames > 0
    if ((ctx->max_b_frames > 0 || ((ctx->pa_adaptive_mini_gop == 1) ? true : false)) && ctx->dts_delay == 0) {
        int64_t timestamp_last = AV_NOPTS_VALUE;
        size_t can_read = av_fifo_can_read(ctx->timestamp_list);

        AMF_RETURN_IF_FALSE(ctx, can_read > 0, AVERROR_UNKNOWN,
            "timestamp_list is empty while max_b_frames = %d\n", avctx->max_b_frames);
        av_fifo_peek(ctx->timestamp_list, &timestamp_last, 1, can_read - 1);
        if (timestamp < 0 || timestamp_last < AV_NOPTS_VALUE) {
            return AVERROR(ERANGE);
        }
        ctx->dts_delay = timestamp_last - timestamp;
    }
    pkt->dts = timestamp - ctx->dts_delay;
    return 0;
}

// amfenc API implementation
int ff_amf_encode_init(AVCodecContext *avctx)
{
    int ret;
    AMFEncoderContext *ctx = avctx->priv_data;
    AVHWDeviceContext   *hwdev_ctx = NULL;

    // hardcoded to current HW queue size - will auto-realloc if too small
    ctx->timestamp_list = av_fifo_alloc2(avctx->max_b_frames + 16, sizeof(int64_t),
                                         AV_FIFO_FLAG_AUTO_GROW);
    if (!ctx->timestamp_list) {
        return AVERROR(ENOMEM);
    }
    ctx->dts_delay = 0;

    ctx->hwsurfaces_in_queue = 0;

    if (avctx->hw_device_ctx) {
        hwdev_ctx = (AVHWDeviceContext*)avctx->hw_device_ctx->data;
        if (hwdev_ctx->type == AV_HWDEVICE_TYPE_AMF)
        {
            ctx->device_ctx_ref = av_buffer_ref(avctx->hw_device_ctx);
        }
        else {
            ret = av_hwdevice_ctx_create_derived(&ctx->device_ctx_ref, AV_HWDEVICE_TYPE_AMF, avctx->hw_device_ctx, 0);
            AMF_RETURN_IF_FALSE(avctx, ret == 0, ret, "Failed to create derived AMF device context: %s\n", av_err2str(ret));
        }
    } else if (avctx->hw_frames_ctx) {
        AVHWFramesContext *frames_ctx = (AVHWFramesContext*)avctx->hw_frames_ctx->data;
        if (frames_ctx->device_ref ) {
            if (frames_ctx->format == AV_PIX_FMT_AMF_SURFACE) {
                ctx->device_ctx_ref = av_buffer_ref(frames_ctx->device_ref);
            }
            else {
                ret = av_hwdevice_ctx_create_derived(&ctx->device_ctx_ref, AV_HWDEVICE_TYPE_AMF, frames_ctx->device_ref, 0);
                AMF_RETURN_IF_FALSE(avctx, ret == 0, ret, "Failed to create derived AMF device context: %s\n", av_err2str(ret));
            }
        }
    }
    else {
        ret = av_hwdevice_ctx_create(&ctx->device_ctx_ref, AV_HWDEVICE_TYPE_AMF, NULL, NULL, 0);
        AMF_RETURN_IF_FALSE(avctx, ret == 0, ret, "Failed to create  hardware device context (AMF) : %s\n", av_err2str(ret));
    }

    if ((ret = amf_init_encoder(avctx)) == 0) {
        return 0;
    }

    ff_amf_encode_close(avctx);
    return ret;
}

static AMF_RESULT amf_set_property_buffer(AMFSurface *object, const wchar_t *name, AMFBuffer *val)
{
    AMF_RESULT res;
    AMFVariantStruct var;
    res = AMFVariantInit(&var);
    if (res == AMF_OK) {
        AMFGuid guid_AMFInterface = IID_AMFInterface();
        AMFInterface *amf_interface;
        res = val->pVtbl->QueryInterface(val, &guid_AMFInterface, (void**)&amf_interface);

        if (res == AMF_OK) {
            res = AMFVariantAssignInterface(&var, amf_interface);
            amf_interface->pVtbl->Release(amf_interface);
        }
        if (res == AMF_OK) {
            res = object->pVtbl->SetProperty(object, name, var);
        }
        AMFVariantClear(&var);
    }
    return res;
}

static AMF_RESULT amf_store_attached_frame_ref(const AVFrame *frame, AMFSurface *surface)
{
    AMF_RESULT res = AMF_FAIL;
    int64_t data;
    AVFrame *frame_ref = av_frame_clone(frame);
    if (frame_ref) {
        memcpy(&data, &frame_ref, sizeof(frame_ref)); // store pointer in 8 bytes
        AMF_ASSIGN_PROPERTY_INT64(res, surface, AMF_AV_FRAME_REF, data);
    }
    return res;
}

static AMF_RESULT amf_release_attached_frame_ref(AMFBuffer *buffer)
{
    AMFVariantStruct var = {0};
    AMF_RESULT res = buffer->pVtbl->GetProperty(buffer, AMF_AV_FRAME_REF, &var);
    if(res == AMF_OK && var.int64Value){
        AVFrame *frame_ref;
        memcpy(&frame_ref, &var.int64Value, sizeof(frame_ref));
        av_frame_free(&frame_ref);
    }
    return res;
}

int ff_amf_receive_packet(AVCodecContext *avctx, AVPacket *avpkt)
{
    AMFEncoderContext     *ctx = avctx->priv_data;
    AVHWDeviceContext     *hw_device_ctx = (AVHWDeviceContext*)ctx->device_ctx_ref->data;
    AVAMFDeviceContext    *amf_device_ctx = (AVAMFDeviceContext *)hw_device_ctx->hwctx;
    AMFSurface *surface;
    AMF_RESULT  res;
    int         ret;
    AMF_RESULT  res_query;
    AMFData    *data = NULL;
    AVFrame    *frame = av_frame_alloc();
    int         block_and_wait;
    int         input_full = 0;
    int         hw_surface = 0;
    int64_t     pts = 0;
    int max_b_frames = ctx->max_b_frames < 0 ? 0 : ctx->max_b_frames;

    if (!ctx->encoder){
        av_frame_free(&frame);
        return AVERROR(EINVAL);
    }
    ret = ff_encode_get_frame(avctx, frame);
    if(ret < 0){
        if(ret != AVERROR_EOF){
            av_frame_free(&frame);
            if(ret == AVERROR(EAGAIN)){
                if(ctx->submitted_frame <= ctx->encoded_frame + max_b_frames + 1) // too soon to poll
                    return ret;
            }
        }
    }
    if(ret != AVERROR(EAGAIN)){
        if (!frame->buf[0]) { // submit drain
            if (!ctx->eof) { // submit drain one time only
                if(!ctx->delayed_drain) {
                    res = ctx->encoder->pVtbl->Drain(ctx->encoder);
                    if (res == AMF_INPUT_FULL) {
                        ctx->delayed_drain = 1; // input queue is full: resubmit Drain() in receive loop
                    } else {
                        if (res == AMF_OK) {
                            ctx->eof = 1; // drain started
                        }
                        AMF_RETURN_IF_FALSE(ctx, res == AMF_OK, AVERROR_UNKNOWN, "Drain() failed with error %d\n", res);
                    }
                }
            }
        } else { // submit frame

            // prepare surface from frame
            switch (frame->format) {
    #if CONFIG_D3D11VA
            case AV_PIX_FMT_D3D11:
                {
                    static const GUID AMFTextureArrayIndexGUID = { 0x28115527, 0xe7c3, 0x4b66, { 0x99, 0xd3, 0x4f, 0x2a, 0xe6, 0xb4, 0x7f, 0xaf } };
                    ID3D11Texture2D *texture = (ID3D11Texture2D*)frame->data[0]; // actual texture
                    int index = (intptr_t)frame->data[1]; // index is a slice in texture array is - set to tell AMF which slice to use

                    av_assert0(frame->hw_frames_ctx       && avctx->hw_frames_ctx &&
                            frame->hw_frames_ctx->data == avctx->hw_frames_ctx->data);

                    texture->lpVtbl->SetPrivateData(texture, &AMFTextureArrayIndexGUID, sizeof(index), &index);

                    res = amf_device_ctx->context->pVtbl->CreateSurfaceFromDX11Native(amf_device_ctx->context, texture, &surface, NULL); // wrap to AMF surface
                    AMF_RETURN_IF_FALSE(ctx, res == AMF_OK, AVERROR(ENOMEM), "CreateSurfaceFromDX11Native() failed  with error %d\n", res);

                    hw_surface = 1;
                }
                break;
    #endif
    #if CONFIG_DXVA2
            case AV_PIX_FMT_DXVA2_VLD:
                {
                    IDirect3DSurface9 *texture = (IDirect3DSurface9 *)frame->data[3]; // actual texture

                    res = amf_device_ctx->context->pVtbl->CreateSurfaceFromDX9Native(amf_device_ctx->context, texture, &surface, NULL); // wrap to AMF surface
                    AMF_RETURN_IF_FALSE(ctx, res == AMF_OK, AVERROR(ENOMEM), "CreateSurfaceFromDX9Native() failed  with error %d\n", res);

                    hw_surface = 1;
                }
                break;
    #endif
            case AV_PIX_FMT_AMF_SURFACE:
                {
                    surface = (AMFSurface*)frame->data[0];
                    surface->pVtbl->Acquire(surface);
                    hw_surface = 1;
                }
                break;
            default:
                {
                    res = amf_device_ctx->context->pVtbl->AllocSurface(amf_device_ctx->context, AMF_MEMORY_HOST, ctx->format, avctx->width, avctx->height, &surface);
                    AMF_RETURN_IF_FALSE(ctx, res == AMF_OK, AVERROR(ENOMEM), "AllocSurface() failed  with error %d\n", res);
                    amf_copy_surface(avctx, frame, surface);
                }
                break;
            }

            if (hw_surface) {
                amf_store_attached_frame_ref(frame, surface);
                ctx->hwsurfaces_in_queue++;
                // input HW surfaces can be vertically aligned by 16; tell AMF the real size
                surface->pVtbl->SetCrop(surface, 0, 0, frame->width, frame->height);
            }

            // HDR10 metadata
            if (frame->color_trc == AVCOL_TRC_SMPTE2084) {
                AMFBuffer * hdrmeta_buffer = NULL;
                res = amf_device_ctx->context->pVtbl->AllocBuffer(amf_device_ctx->context, AMF_MEMORY_HOST, sizeof(AMFHDRMetadata), &hdrmeta_buffer);
                if (res == AMF_OK) {
                    AMFHDRMetadata * hdrmeta = (AMFHDRMetadata*)hdrmeta_buffer->pVtbl->GetNative(hdrmeta_buffer);
                    if (amf_save_hdr_metadata(avctx, frame, hdrmeta) == 0) {
                        switch (avctx->codec->id) {
                        case AV_CODEC_ID_H264:
                            AMF_ASSIGN_PROPERTY_INTERFACE(res, ctx->encoder, AMF_VIDEO_ENCODER_INPUT_HDR_METADATA, hdrmeta_buffer); break;
                        case AV_CODEC_ID_HEVC:
                            AMF_ASSIGN_PROPERTY_INTERFACE(res, ctx->encoder, AMF_VIDEO_ENCODER_HEVC_INPUT_HDR_METADATA, hdrmeta_buffer); break;
                        case AV_CODEC_ID_AV1:
                            AMF_ASSIGN_PROPERTY_INTERFACE(res, ctx->encoder, AMF_VIDEO_ENCODER_AV1_INPUT_HDR_METADATA, hdrmeta_buffer); break;
                        }
                        res = amf_set_property_buffer(surface, L"av_frame_hdrmeta", hdrmeta_buffer);
                        AMF_RETURN_IF_FALSE(avctx, res == AMF_OK, AVERROR_UNKNOWN, "SetProperty failed for \"av_frame_hdrmeta\" with error %d\n", res);
                    }
                    hdrmeta_buffer->pVtbl->Release(hdrmeta_buffer);
                }
            }

            surface->pVtbl->SetPts(surface, frame->pts);
            AMF_ASSIGN_PROPERTY_INT64(res, surface, PTS_PROP, frame->pts);

            switch (avctx->codec->id) {
            case AV_CODEC_ID_H264:
                AMF_ASSIGN_PROPERTY_INT64(res, surface, AMF_VIDEO_ENCODER_INSERT_AUD, !!ctx->aud);
                switch (frame->pict_type) {
                case AV_PICTURE_TYPE_I:
                    if (ctx->forced_idr) {
                        AMF_ASSIGN_PROPERTY_INT64(res, surface, AMF_VIDEO_ENCODER_INSERT_SPS, 1);
                        AMF_ASSIGN_PROPERTY_INT64(res, surface, AMF_VIDEO_ENCODER_INSERT_PPS, 1);
                        AMF_ASSIGN_PROPERTY_INT64(res, surface, AMF_VIDEO_ENCODER_FORCE_PICTURE_TYPE, AMF_VIDEO_ENCODER_PICTURE_TYPE_IDR);
                    } else {
                        AMF_ASSIGN_PROPERTY_INT64(res, surface, AMF_VIDEO_ENCODER_FORCE_PICTURE_TYPE, AMF_VIDEO_ENCODER_PICTURE_TYPE_I);
                    }
                    break;
                case AV_PICTURE_TYPE_P:
                    AMF_ASSIGN_PROPERTY_INT64(res, surface, AMF_VIDEO_ENCODER_FORCE_PICTURE_TYPE, AMF_VIDEO_ENCODER_PICTURE_TYPE_P);
                    break;
                case AV_PICTURE_TYPE_B:
                    AMF_ASSIGN_PROPERTY_INT64(res, surface, AMF_VIDEO_ENCODER_FORCE_PICTURE_TYPE, AMF_VIDEO_ENCODER_PICTURE_TYPE_B);
                    break;
                }
                break;
            case AV_CODEC_ID_HEVC:
                AMF_ASSIGN_PROPERTY_INT64(res, surface, AMF_VIDEO_ENCODER_HEVC_INSERT_AUD, !!ctx->aud);
                switch (frame->pict_type) {
                case AV_PICTURE_TYPE_I:
                    if (ctx->forced_idr) {
                        AMF_ASSIGN_PROPERTY_INT64(res, surface, AMF_VIDEO_ENCODER_HEVC_INSERT_HEADER, 1);
                        AMF_ASSIGN_PROPERTY_INT64(res, surface, AMF_VIDEO_ENCODER_HEVC_FORCE_PICTURE_TYPE, AMF_VIDEO_ENCODER_HEVC_PICTURE_TYPE_IDR);
                    } else {
                        AMF_ASSIGN_PROPERTY_INT64(res, surface, AMF_VIDEO_ENCODER_HEVC_FORCE_PICTURE_TYPE, AMF_VIDEO_ENCODER_HEVC_PICTURE_TYPE_I);
                    }
                    break;
                case AV_PICTURE_TYPE_P:
                    AMF_ASSIGN_PROPERTY_INT64(res, surface, AMF_VIDEO_ENCODER_HEVC_FORCE_PICTURE_TYPE, AMF_VIDEO_ENCODER_HEVC_PICTURE_TYPE_P);
                    break;
                }
                break;
            case AV_CODEC_ID_AV1:
                if (frame->pict_type == AV_PICTURE_TYPE_I) {
                    if (ctx->forced_idr) {
                        AMF_ASSIGN_PROPERTY_INT64(res, surface, AMF_VIDEO_ENCODER_AV1_FORCE_INSERT_SEQUENCE_HEADER, 1);
                        AMF_ASSIGN_PROPERTY_INT64(res, surface, AMF_VIDEO_ENCODER_AV1_FORCE_FRAME_TYPE, AMF_VIDEO_ENCODER_AV1_FORCE_FRAME_TYPE_KEY);
                    } else {
                        AMF_ASSIGN_PROPERTY_INT64(res, surface, AMF_VIDEO_ENCODER_AV1_FORCE_FRAME_TYPE, AMF_VIDEO_ENCODER_AV1_FORCE_FRAME_TYPE_INTRA_ONLY);
                    }
                }
                break;
            default:
                break;
            }
            pts = frame->pts;
            // submit surface
            res = ctx->encoder->pVtbl->SubmitInput(ctx->encoder, (AMFData*)surface);
            av_frame_free(&frame);

            if (res == AMF_INPUT_FULL) { // handle full queue
                //store surface for later submission
                input_full = 1;
            } else {
                surface->pVtbl->Release(surface);
                AMF_RETURN_IF_FALSE(ctx, res == AMF_OK, AVERROR_UNKNOWN, "SubmitInput() failed with error %d\n", res);

                ctx->submitted_frame++;
                ret = av_fifo_write(ctx->timestamp_list, &pts, 1);

                if (ret < 0)
                    return ret;
                if(ctx->submitted_frame <= ctx->encoded_frame + max_b_frames + 1)
                    return AVERROR(EAGAIN); // if frame just submiited - don't poll or wait
            }
        }
    }
    av_frame_free(&frame);

    do {
        block_and_wait = 0;
        // poll data

        res_query = ctx->encoder->pVtbl->QueryOutput(ctx->encoder, &data);
        if (data) {
            // copy data to packet
            AMFBuffer *buffer;
            AMFGuid guid = IID_AMFBuffer();
            data->pVtbl->QueryInterface(data, &guid, (void**)&buffer); // query for buffer interface
            ret = amf_copy_buffer(avctx, avpkt, buffer);
            if (amf_release_attached_frame_ref(buffer) == AMF_OK) {
                ctx->hwsurfaces_in_queue--;
            }
            ctx->encoded_frame++;
            buffer->pVtbl->Release(buffer);
            data->pVtbl->Release(data);

            AMF_RETURN_IF_FALSE(ctx, ret >= 0, ret, "amf_copy_buffer() failed with error %d\n", ret);

            if (ctx->delayed_drain) { // try to resubmit drain
                res = ctx->encoder->pVtbl->Drain(ctx->encoder);
                if (res != AMF_INPUT_FULL) {
                    ctx->delayed_drain = 0;
                    ctx->eof = 1; // drain started
                    AMF_RETURN_IF_FALSE(ctx, res == AMF_OK, AVERROR_UNKNOWN, "Repeated Drain() failed with error %d\n", res);
                } else {
                    av_log(avctx, AV_LOG_WARNING, "Data acquired but delayed drain submission got AMF_INPUT_FULL- should not happen\n");
                }
            }
        } else if (ctx->delayed_drain || (ctx->eof && res_query != AMF_EOF) || (ctx->hwsurfaces_in_queue >= ctx->hwsurfaces_in_queue_max) || input_full) {
            block_and_wait = 1;
            // Only sleep if the driver doesn't support waiting in QueryOutput()
            // or if we already have output data so we will skip calling it.
            if (!ctx->query_timeout_supported || avpkt->data || avpkt->buf) {
                av_usleep(1000);
            }
        }
    } while (block_and_wait);

    if (res_query == AMF_EOF) {
        ret = AVERROR_EOF;
    } else if (data == NULL) {
        ret = AVERROR(EAGAIN);
    } else {
        if(input_full) {
            // resubmit surface
            res = ctx->encoder->pVtbl->SubmitInput(ctx->encoder, (AMFData*)surface);
            surface->pVtbl->Release(surface);
            if (res == AMF_INPUT_FULL) {
                av_log(avctx, AV_LOG_WARNING, "Data acquired but delayed SubmitInput returned AMF_INPUT_FULL- should not happen\n");
            } else {
                AMF_RETURN_IF_FALSE(ctx, res == AMF_OK, AVERROR_UNKNOWN, "SubmitInput() failed with error %d\n", res);

                ret = av_fifo_write(ctx->timestamp_list, &pts, 1);

                ctx->submitted_frame++;

                if (ret < 0)
                    return ret;
            }
        }
        ret = 0;
    }
    return ret;
}

int ff_amf_get_color_profile(AVCodecContext *avctx)
{
    amf_int64 color_profile = AMF_VIDEO_CONVERTER_COLOR_PROFILE_UNKNOWN;
    if (avctx->color_range == AVCOL_RANGE_JPEG) {
        /// Color Space for Full (JPEG) Range
        switch (avctx->colorspace) {
        case AVCOL_SPC_SMPTE170M:
            color_profile = AMF_VIDEO_CONVERTER_COLOR_PROFILE_FULL_601;
            break;
        case AVCOL_SPC_BT709:
            color_profile = AMF_VIDEO_CONVERTER_COLOR_PROFILE_FULL_709;
            break;
        case AVCOL_SPC_BT2020_NCL:
        case AVCOL_SPC_BT2020_CL:
            color_profile = AMF_VIDEO_CONVERTER_COLOR_PROFILE_FULL_2020;
            break;
        }
    } else {
        /// Color Space for Limited (MPEG) range
        switch (avctx->colorspace) {
        case AVCOL_SPC_SMPTE170M:
            color_profile = AMF_VIDEO_CONVERTER_COLOR_PROFILE_601;
            break;
        case AVCOL_SPC_BT709:
            color_profile = AMF_VIDEO_CONVERTER_COLOR_PROFILE_709;
            break;
        case AVCOL_SPC_BT2020_NCL:
        case AVCOL_SPC_BT2020_CL:
            color_profile = AMF_VIDEO_CONVERTER_COLOR_PROFILE_2020;
            break;
        }
    }
    return color_profile;
}

const AVCodecHWConfigInternal *const ff_amfenc_hw_configs[] = {
#if CONFIG_D3D11VA
    HW_CONFIG_ENCODER_FRAMES(D3D11, D3D11VA),
    HW_CONFIG_ENCODER_DEVICE(NONE,  D3D11VA),
#endif
#if CONFIG_DXVA2
    HW_CONFIG_ENCODER_FRAMES(DXVA2_VLD, DXVA2),
    HW_CONFIG_ENCODER_DEVICE(NONE,      DXVA2),
#endif
    HW_CONFIG_ENCODER_FRAMES(AMF_SURFACE,   AMF),
    HW_CONFIG_ENCODER_DEVICE(NONE,          AMF),
    NULL,
};
