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

#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/hwcontext.h"
#if CONFIG_D3D11VA
#include "libavutil/hwcontext_d3d11va.h"
#endif
#include "libavutil/mem.h"
#include "libavutil/pixdesc.h"
#include "libavutil/time.h"

#include "amfenc.h"
#include "internal.h"

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
    AV_PIX_FMT_NONE
};

typedef struct FormatMap {
    enum AVPixelFormat       av_format;
    enum AMF_SURFACE_FORMAT  amf_format;
} FormatMap;

static const FormatMap format_map[] =
{
    { AV_PIX_FMT_NONE,       AMF_SURFACE_UNKNOWN },
    { AV_PIX_FMT_NV12,       AMF_SURFACE_NV12 },
    { AV_PIX_FMT_BGR0,       AMF_SURFACE_BGRA },
    { AV_PIX_FMT_RGB0,       AMF_SURFACE_RGBA },
    { AV_PIX_FMT_GRAY8,      AMF_SURFACE_GRAY8 },
    { AV_PIX_FMT_YUV420P,    AMF_SURFACE_YUV420P },
    { AV_PIX_FMT_YUYV422,    AMF_SURFACE_YUY2 },
    { AV_PIX_FMT_D3D11,      AMF_SURFACE_NV12 },
};


static int is_hwaccel_pix_fmt(enum AVPixelFormat pix_fmt)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
    return desc->flags & AV_PIX_FMT_FLAG_HWACCEL;
}


static enum AMF_SURFACE_FORMAT amf_av_to_amf_format(enum AVPixelFormat fmt)
{
    int i;
    for (i = 0; i < amf_countof(format_map); i++) {
        if (format_map[i].av_format == fmt) {
            return format_map[i].amf_format;
        }
    }
    return AMF_SURFACE_UNKNOWN;
}

static void AMF_CDECL_CALL AMFTraceWriter_Write(AMFTraceWriter *pThis,
    const wchar_t *scope, const wchar_t *message)
{
    AmfTraceWriter *tracer = (AmfTraceWriter*)pThis;
    av_log(tracer->avctx, AV_LOG_DEBUG, "%ls: %ls", scope, message); // \n is provided from AMF
}

static void AMF_CDECL_CALL AMFTraceWriter_Flush(AMFTraceWriter *pThis)
{
}

static AMFTraceWriterVtbl tracer_vtbl =
{
    .Write = AMFTraceWriter_Write,
    .Flush = AMFTraceWriter_Flush,
};

static int amf_load_library(AVCodecContext *avctx)
{
    AmfContext             *ctx = avctx->priv_data;
    AMFInit_Fn              init_fun = NULL;
    AMFQueryVersion_Fn      version_fun = NULL;
    AMF_RESULT              res = AMF_OK;

    ctx->eof = 0;
    ctx->delayed_drain = 0;
    ctx->hw_frames_ctx = NULL;
    ctx->hw_device_ctx = NULL;
    ctx->delayed_surface = NULL;
    ctx->delayed_frame = av_frame_alloc();
    if (!ctx->delayed_frame) {
        return AVERROR(ENOMEM);
    }
    // hardcoded to current HW queue size - will realloc in timestamp_queue_enqueue() if too small
    ctx->timestamp_list = av_fifo_alloc((avctx->max_b_frames + 16) * sizeof(int64_t));
    if (!ctx->timestamp_list) {
        return AVERROR(ENOMEM);
    }
    ctx->dts_delay = 0;


    ctx->library = dlopen(AMF_DLL_NAMEA, RTLD_NOW | RTLD_LOCAL);
    AMF_RETURN_IF_FALSE(ctx, ctx->library != NULL,
        AVERROR_UNKNOWN, "DLL %s failed to open\n", AMF_DLL_NAMEA);

    init_fun = (AMFInit_Fn)dlsym(ctx->library, AMF_INIT_FUNCTION_NAME);
    AMF_RETURN_IF_FALSE(ctx, init_fun != NULL, AVERROR_UNKNOWN, "DLL %s failed to find function %s\n", AMF_DLL_NAMEA, AMF_INIT_FUNCTION_NAME);

    version_fun = (AMFQueryVersion_Fn)dlsym(ctx->library, AMF_QUERY_VERSION_FUNCTION_NAME);
    AMF_RETURN_IF_FALSE(ctx, version_fun != NULL, AVERROR_UNKNOWN, "DLL %s failed to find function %s\n", AMF_DLL_NAMEA, AMF_QUERY_VERSION_FUNCTION_NAME);

    res = version_fun(&ctx->version);
    AMF_RETURN_IF_FALSE(ctx, res == AMF_OK, AVERROR_UNKNOWN, "%s failed with error %d\n", AMF_QUERY_VERSION_FUNCTION_NAME, res);
    res = init_fun(AMF_FULL_VERSION, &ctx->factory);
    AMF_RETURN_IF_FALSE(ctx, res == AMF_OK, AVERROR_UNKNOWN, "%s failed with error %d\n", AMF_INIT_FUNCTION_NAME, res);
    res = ctx->factory->pVtbl->GetTrace(ctx->factory, &ctx->trace);
    AMF_RETURN_IF_FALSE(ctx, res == AMF_OK, AVERROR_UNKNOWN, "GetTrace() failed with error %d\n", res);
    res = ctx->factory->pVtbl->GetDebug(ctx->factory, &ctx->debug);
    AMF_RETURN_IF_FALSE(ctx, res == AMF_OK, AVERROR_UNKNOWN, "GetDebug() failed with error %d\n", res);
    return 0;
}

static int amf_init_context(AVCodecContext *avctx)
{
    AmfContext         *ctx = avctx->priv_data;
    AMF_RESULT          res = AMF_OK;

    // confugure AMF logger
    // the return of these functions indicates old state and do not affect behaviour
    ctx->trace->pVtbl->EnableWriter(ctx->trace, AMF_TRACE_WRITER_DEBUG_OUTPUT, ctx->log_to_dbg != 0 );
    if (ctx->log_to_dbg)
        ctx->trace->pVtbl->SetWriterLevel(ctx->trace, AMF_TRACE_WRITER_DEBUG_OUTPUT, AMF_TRACE_TRACE);
    ctx->trace->pVtbl->EnableWriter(ctx->trace, AMF_TRACE_WRITER_CONSOLE, 0);
    ctx->trace->pVtbl->SetGlobalLevel(ctx->trace, AMF_TRACE_TRACE);

    // connect AMF logger to av_log
    ctx->tracer.vtbl = &tracer_vtbl;
    ctx->tracer.avctx = avctx;
    ctx->trace->pVtbl->RegisterWriter(ctx->trace, FFMPEG_AMF_WRITER_ID,(AMFTraceWriter*)&ctx->tracer, 1);
    ctx->trace->pVtbl->SetWriterLevel(ctx->trace, FFMPEG_AMF_WRITER_ID, AMF_TRACE_TRACE);

    res = ctx->factory->pVtbl->CreateContext(ctx->factory, &ctx->context);
    AMF_RETURN_IF_FALSE(ctx, res == AMF_OK, AVERROR_UNKNOWN, "CreateContext() failed with error %d\n", res);
    // try to reuse existing DX device
#if CONFIG_D3D11VA
    if (avctx->hw_frames_ctx) {
        AVHWFramesContext *device_ctx = (AVHWFramesContext*)avctx->hw_frames_ctx->data;
        if (device_ctx->device_ctx->type == AV_HWDEVICE_TYPE_D3D11VA) {
            if (amf_av_to_amf_format(device_ctx->sw_format) != AMF_SURFACE_UNKNOWN) {
                if (device_ctx->device_ctx->hwctx) {
                    AVD3D11VADeviceContext *device_d3d11 = (AVD3D11VADeviceContext *)device_ctx->device_ctx->hwctx;
                    res = ctx->context->pVtbl->InitDX11(ctx->context, device_d3d11->device, AMF_DX11_1);
                    if (res == AMF_OK) {
                        ctx->hw_frames_ctx = av_buffer_ref(avctx->hw_frames_ctx);
                        if (!ctx->hw_frames_ctx) {
                            return AVERROR(ENOMEM);
                        }
                    } else {
                        if(res == AMF_NOT_SUPPORTED)
                            av_log(avctx, AV_LOG_INFO, "avctx->hw_frames_ctx has D3D11 device which doesn't have D3D11VA interface, switching to default\n");
                        else
                            av_log(avctx, AV_LOG_INFO, "avctx->hw_frames_ctx has non-AMD device, switching to default\n");
                    }
                }
            } else {
                av_log(avctx, AV_LOG_INFO, "avctx->hw_frames_ctx has format not uspported by AMF, switching to default\n");
            }
        }
    } else if (avctx->hw_device_ctx) {
        AVHWDeviceContext *device_ctx = (AVHWDeviceContext*)(avctx->hw_device_ctx->data);
        if (device_ctx->type == AV_HWDEVICE_TYPE_D3D11VA) {
            if (device_ctx->hwctx) {
                AVD3D11VADeviceContext *device_d3d11 = (AVD3D11VADeviceContext *)device_ctx->hwctx;
                res = ctx->context->pVtbl->InitDX11(ctx->context, device_d3d11->device, AMF_DX11_1);
                if (res == AMF_OK) {
                    ctx->hw_device_ctx = av_buffer_ref(avctx->hw_device_ctx);
                    if (!ctx->hw_device_ctx) {
                        return AVERROR(ENOMEM);
                    }
                } else {
                    if (res == AMF_NOT_SUPPORTED)
                        av_log(avctx, AV_LOG_INFO, "avctx->hw_device_ctx has D3D11 device which doesn't have D3D11VA interface, switching to default\n");
                    else
                        av_log(avctx, AV_LOG_INFO, "avctx->hw_device_ctx has non-AMD device, switching to default\n");
                }
            }
        }
    }
#endif
    if (!ctx->hw_frames_ctx && !ctx->hw_device_ctx) {
        res = ctx->context->pVtbl->InitDX11(ctx->context, NULL, AMF_DX11_1);
        if (res != AMF_OK) {
            res = ctx->context->pVtbl->InitDX9(ctx->context, NULL);
            AMF_RETURN_IF_FALSE(ctx, res == AMF_OK, AVERROR_UNKNOWN, "InitDX9() failed with error %d\n", res);
        }
    }
    return 0;
}

static int amf_init_encoder(AVCodecContext *avctx)
{
    AmfContext          *ctx = avctx->priv_data;
    const wchar_t       *codec_id = NULL;
    AMF_RESULT           res = AMF_OK;

    switch (avctx->codec->id) {
        case AV_CODEC_ID_H264:
            codec_id = AMFVideoEncoderVCE_AVC;
            break;
        case AV_CODEC_ID_HEVC:
            codec_id = AMFVideoEncoder_HEVC;
            break;
        default:
            break;
    }
    AMF_RETURN_IF_FALSE(ctx, codec_id != NULL, AVERROR(EINVAL), "Codec %d is not supported\n", avctx->codec->id);

    ctx->format = amf_av_to_amf_format(avctx->pix_fmt);
    AMF_RETURN_IF_FALSE(ctx, ctx->format != AMF_SURFACE_UNKNOWN, AVERROR(EINVAL), "Format %d is not supported\n", avctx->pix_fmt);

    res = ctx->factory->pVtbl->CreateComponent(ctx->factory, ctx->context, codec_id, &ctx->encoder);
    AMF_RETURN_IF_FALSE(ctx, res == AMF_OK, AVERROR_ENCODER_NOT_FOUND, "CreateComponent(%ls) failed with error %d\n", codec_id, res);

    return 0;
}

int av_cold ff_amf_encode_close(AVCodecContext *avctx)
{
    AmfContext      *ctx = avctx->priv_data;
    if (ctx->delayed_surface)
    {
        ctx->delayed_surface->pVtbl->Release(ctx->delayed_surface);
        ctx->delayed_surface = NULL;
    }

    if (ctx->encoder) {
        ctx->encoder->pVtbl->Terminate(ctx->encoder);
        ctx->encoder->pVtbl->Release(ctx->encoder);
        ctx->encoder = NULL;
    }

    if (ctx->context) {
        ctx->context->pVtbl->Terminate(ctx->context);
        ctx->context->pVtbl->Release(ctx->context);
        ctx->context = NULL;
    }
    av_buffer_unref(&ctx->hw_device_ctx);
    av_buffer_unref(&ctx->hw_frames_ctx);

    if (ctx->trace) {
        ctx->trace->pVtbl->UnregisterWriter(ctx->trace, FFMPEG_AMF_WRITER_ID);
    }
    if (ctx->library) {
        dlclose(ctx->library);
        ctx->library = NULL;
    }
    ctx->trace = NULL;
    ctx->debug = NULL;
    ctx->factory = NULL;
    ctx->version = 0;
    ctx->delayed_drain = 0;
    av_frame_free(&ctx->delayed_frame);
    av_fifo_freep(&ctx->timestamp_list);

    return 0;
}

static int amf_copy_surface(AVCodecContext *avctx, const AVFrame *frame,
    AMFSurface* surface)
{
    AVFrame        *sw_frame = NULL;
    AMFPlane       *plane = NULL;
    uint8_t        *dst_data[4];
    int             dst_linesize[4];
    int             ret = 0;
    int             planes;
    int             i;

    if (frame->hw_frames_ctx && is_hwaccel_pix_fmt(frame->format)) {
        if (!(sw_frame = av_frame_alloc())) {
            av_log(avctx, AV_LOG_ERROR, "Can not alloc frame\n");
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        if ((ret = av_hwframe_transfer_data(sw_frame, frame, 0)) < 0) {
            av_log(avctx, AV_LOG_ERROR, "Error transferring the data to system memory\n");
            goto fail;
        }
        frame = sw_frame;
    }
    planes = (int)surface->pVtbl->GetPlanesCount(surface);
    if (planes > amf_countof(dst_data)) {
        av_log(avctx, AV_LOG_ERROR, "Invalid number of planes %d in surface\n", planes);
        ret = AVERROR(EINVAL);
        goto fail;
    }

    for (i = 0; i < planes; i++) {
        plane = surface->pVtbl->GetPlaneAt(surface, i);
        dst_data[i] = plane->pVtbl->GetNative(plane);
        dst_linesize[i] = plane->pVtbl->GetHPitch(plane);
    }
    av_image_copy(dst_data, dst_linesize,
        (const uint8_t**)frame->data, frame->linesize, frame->format,
        avctx->width, avctx->height);

fail:
    if (sw_frame) {
        av_frame_free(&sw_frame);
    }
    return ret;
}

static inline int timestamp_queue_enqueue(AVCodecContext *avctx, int64_t timestamp)
{
    AmfContext         *ctx = avctx->priv_data;
    if (av_fifo_space(ctx->timestamp_list) < sizeof(timestamp)) {
        if (av_fifo_grow(ctx->timestamp_list, sizeof(timestamp)) < 0) {
            return AVERROR(ENOMEM);
        }
    }
    av_fifo_generic_write(ctx->timestamp_list, &timestamp, sizeof(timestamp), NULL);
    return 0;
}

static int amf_copy_buffer(AVCodecContext *avctx, AVPacket *pkt, AMFBuffer *buffer)
{
    AmfContext             *ctx = avctx->priv_data;
    int                     ret;
    AMFVariantStruct        var = {0};
    int64_t                 timestamp = AV_NOPTS_VALUE;
    int64_t                 size = buffer->pVtbl->GetSize(buffer);

    if ((ret = ff_alloc_packet2(avctx, pkt, size, 0)) < 0) {
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
        default:
            break;
    }

    buffer->pVtbl->GetProperty(buffer, PTS_PROP, &var);

    pkt->pts = var.int64Value; // original pts


    AMF_RETURN_IF_FALSE(ctx, av_fifo_size(ctx->timestamp_list) > 0, AVERROR_UNKNOWN, "timestamp_list is empty\n");

    av_fifo_generic_read(ctx->timestamp_list, &timestamp, sizeof(timestamp), NULL);

    // calc dts shift if max_b_frames > 0
    if (avctx->max_b_frames > 0 && ctx->dts_delay == 0) {
        int64_t timestamp_last = AV_NOPTS_VALUE;
        AMF_RETURN_IF_FALSE(ctx, av_fifo_size(ctx->timestamp_list) > 0, AVERROR_UNKNOWN,
            "timestamp_list is empty while max_b_frames = %d\n", avctx->max_b_frames);
        av_fifo_generic_peek_at(
            ctx->timestamp_list,
            &timestamp_last,
            (av_fifo_size(ctx->timestamp_list) / sizeof(timestamp) - 1) * sizeof(timestamp_last),
            sizeof(timestamp_last),
            NULL);
        if (timestamp < 0 || timestamp_last < AV_NOPTS_VALUE) {
            return AVERROR(ERANGE);
        }
        ctx->dts_delay = timestamp_last - timestamp;
    }
    pkt->dts = timestamp - ctx->dts_delay;
    return 0;
}

// amfenc API implmentation
int ff_amf_encode_init(AVCodecContext *avctx)
{
    AmfContext     *ctx = avctx->priv_data;
    int             ret;

    ctx->factory = NULL;
    ctx->debug = NULL;
    ctx->trace = NULL;
    ctx->context = NULL;
    ctx->encoder = NULL;
    ctx->library = NULL;
    ctx->version = 0;
    ctx->eof = 0;
    ctx->format = 0;
    ctx->tracer.vtbl = NULL;
    ctx->tracer.avctx = NULL;

    if ((ret = amf_load_library(avctx)) == 0) {
        if ((ret = amf_init_context(avctx)) == 0) {
            if ((ret = amf_init_encoder(avctx)) == 0) {
                return 0;
            }
        }
    }
    ff_amf_encode_close(avctx);
    return ret;
}


int ff_amf_send_frame(AVCodecContext *avctx, const AVFrame *frame)
{
    AMF_RESULT      res = AMF_OK;
    AmfContext     *ctx = avctx->priv_data;
    AMFSurface     *surface = NULL;
    int             ret;

    if (!ctx->encoder)
        return AVERROR(EINVAL);

    if (!frame) { // submit drain
        if (!ctx->eof) { // submit drain one time only
            if (ctx->delayed_surface != NULL) {
                ctx->delayed_drain = 1; // input queue is full: resubmit Drain() in ff_amf_receive_packet
            } else if(!ctx->delayed_drain) {
                res = ctx->encoder->pVtbl->Drain(ctx->encoder);
                if (res == AMF_INPUT_FULL) {
                    ctx->delayed_drain = 1; // input queue is full: resubmit Drain() in ff_amf_receive_packet
                } else {
                    if (res == AMF_OK) {
                        ctx->eof = 1; // drain started
                    }
                    AMF_RETURN_IF_FALSE(ctx, res == AMF_OK, AVERROR_UNKNOWN, "Drain() failed with error %d\n", res);
                }
            }
        } else{
            return AVERROR_EOF;
        }
    } else { // submit frame
        if (ctx->delayed_surface != NULL) {
            return AVERROR(EAGAIN); // should not happen when called from ffmpeg, other clients may resubmit
        }
        // prepare surface from frame
        if (frame->hw_frames_ctx && ( // HW frame detected
            // check if the same hw_frames_ctx as used in initialization
            (ctx->hw_frames_ctx && frame->hw_frames_ctx->data == ctx->hw_frames_ctx->data) ||
            // check if the same hw_device_ctx as used in initialization
            (ctx->hw_device_ctx && ((AVHWFramesContext*)frame->hw_frames_ctx->data)->device_ctx ==
            (AVHWDeviceContext*)ctx->hw_device_ctx->data)
        )) {
#if CONFIG_D3D11VA
            static const GUID AMFTextureArrayIndexGUID = { 0x28115527, 0xe7c3, 0x4b66, { 0x99, 0xd3, 0x4f, 0x2a, 0xe6, 0xb4, 0x7f, 0xaf } };
            ID3D11Texture2D *texture = (ID3D11Texture2D*)frame->data[0]; // actual texture
            int index = (int)(size_t)frame->data[1]; // index is a slice in texture array is - set to tell AMF which slice to use
            texture->lpVtbl->SetPrivateData(texture, &AMFTextureArrayIndexGUID, sizeof(index), &index);

            res = ctx->context->pVtbl->CreateSurfaceFromDX11Native(ctx->context, texture, &surface, NULL); // wrap to AMF surface
            AMF_RETURN_IF_FALSE(ctx, res == AMF_OK, AVERROR(ENOMEM), "CreateSurfaceFromDX11Native() failed  with error %d\n", res);

            // input HW surfaces can be vertically aligned by 16; tell AMF the real size
            surface->pVtbl->SetCrop(surface, 0, 0, frame->width, frame->height);
#endif
        } else {
            res = ctx->context->pVtbl->AllocSurface(ctx->context, AMF_MEMORY_HOST, ctx->format, avctx->width, avctx->height, &surface);
            AMF_RETURN_IF_FALSE(ctx, res == AMF_OK, AVERROR(ENOMEM), "AllocSurface() failed  with error %d\n", res);
            amf_copy_surface(avctx, frame, surface);
        }
        surface->pVtbl->SetPts(surface, frame->pts);
        AMF_ASSIGN_PROPERTY_INT64(res, surface, PTS_PROP, frame->pts);

        switch (avctx->codec->id) {
        case AV_CODEC_ID_H264:
            AMF_ASSIGN_PROPERTY_INT64(res, surface, AMF_VIDEO_ENCODER_INSERT_AUD, !!ctx->aud);
            break;
        case AV_CODEC_ID_HEVC:
            AMF_ASSIGN_PROPERTY_INT64(res, surface, AMF_VIDEO_ENCODER_HEVC_INSERT_AUD, !!ctx->aud);
            break;
        default:
            break;
        }


        // submit surface
        res = ctx->encoder->pVtbl->SubmitInput(ctx->encoder, (AMFData*)surface);
        if (res == AMF_INPUT_FULL) { // handle full queue
            //store surface for later submission
            ctx->delayed_surface = surface;
            if (surface->pVtbl->GetMemoryType(surface) == AMF_MEMORY_DX11) {
                av_frame_ref(ctx->delayed_frame, frame);
            }
        } else {
            surface->pVtbl->Release(surface);
            AMF_RETURN_IF_FALSE(ctx, res == AMF_OK, AVERROR_UNKNOWN, "SubmitInput() failed with error %d\n", res);

            if ((ret = timestamp_queue_enqueue(avctx, frame->pts)) < 0) {
                return ret;
            }

        }
    }
    return 0;
}
int ff_amf_receive_packet(AVCodecContext *avctx, AVPacket *avpkt)
{
    int             ret;
    AMF_RESULT      res;
    AMF_RESULT      res_query;
    AmfContext     *ctx = avctx->priv_data;
    AMFData        *data = NULL;
    int             block_and_wait;

    if (!ctx->encoder)
        return AVERROR(EINVAL);

    do {
        block_and_wait = 0;
        // poll data
        res_query = ctx->encoder->pVtbl->QueryOutput(ctx->encoder, &data);
        if (data) {
            // copy data to packet
            AMFBuffer* buffer;
            AMFGuid guid = IID_AMFBuffer();
            data->pVtbl->QueryInterface(data, &guid, (void**)&buffer); // query for buffer interface
            ret = amf_copy_buffer(avctx, avpkt, buffer);

            buffer->pVtbl->Release(buffer);
            data->pVtbl->Release(data);

            AMF_RETURN_IF_FALSE(ctx, ret >= 0, ret, "amf_copy_buffer() failed with error %d\n", ret);

            if (ctx->delayed_surface != NULL) { // try to resubmit frame
                res = ctx->encoder->pVtbl->SubmitInput(ctx->encoder, (AMFData*)ctx->delayed_surface);
                if (res != AMF_INPUT_FULL) {
                    int64_t pts = ctx->delayed_surface->pVtbl->GetPts(ctx->delayed_surface);
                    ctx->delayed_surface->pVtbl->Release(ctx->delayed_surface);
                    ctx->delayed_surface = NULL;
                    av_frame_unref(ctx->delayed_frame);
                    AMF_RETURN_IF_FALSE(ctx, res == AMF_OK, AVERROR_UNKNOWN, "Repeated SubmitInput() failed with error %d\n", res);

                    if ((ret = timestamp_queue_enqueue(avctx, pts)) < 0) {
                        return ret;
                    }
                } else {
                    av_log(avctx, AV_LOG_WARNING, "Data acquired but delayed frame submission got AMF_INPUT_FULL- should not happen\n");
                }
            } else if (ctx->delayed_drain) { // try to resubmit drain
                res = ctx->encoder->pVtbl->Drain(ctx->encoder);
                if (res != AMF_INPUT_FULL) {
                    ctx->delayed_drain = 0;
                    ctx->eof = 1; // drain started
                    AMF_RETURN_IF_FALSE(ctx, res == AMF_OK, AVERROR_UNKNOWN, "Repeated Drain() failed with error %d\n", res);
                } else {
                    av_log(avctx, AV_LOG_WARNING, "Data acquired but delayed drain submission got AMF_INPUT_FULL- should not happen\n");
                }
            }
        } else if (ctx->delayed_surface != NULL || ctx->delayed_drain || (ctx->eof && res_query != AMF_EOF)) {
            block_and_wait = 1;
            av_usleep(1000); // wait and poll again
        }
    } while (block_and_wait);

    if (res_query == AMF_EOF) {
        ret = AVERROR_EOF;
    } else if (data == NULL) {
        ret = AVERROR(EAGAIN);
    } else {
        ret = 0;
    }
    return ret;
}
