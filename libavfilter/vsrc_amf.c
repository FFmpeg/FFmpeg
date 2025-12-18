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

#include "libavutil/pixdesc.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"
#include "libavutil/avstring.h"
#include "libavutil/avassert.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_amf.h"
#include "libavutil/hwcontext_amf_internal.h"
#include "compat/w32dlfcn.h"
#include "avfilter.h"
#include "filters.h"
#include "video.h"

#include <AMF/core/Factory.h>
#include <AMF/core/Surface.h>
#include <AMF/components/ColorSpace.h>
#include <AMF/components/DisplayCapture.h>

typedef struct AMFGrabContext {
    AVClass            *avclass;

    int                 monitor_index;
    AVRational          framerate;
    amf_bool            duplicate_output;
    int                 capture_mode;

    AVBufferRef        *device_ctx_ref;

    AMFComponent       *capture;
    amf_bool           eof;
    AMF_SURFACE_FORMAT format;
    void               *winmmdll;
    amf_uint32          timerPrecision;
} AMFGrabContext;

#define OFFSET(x) offsetof(AMFGrabContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption amf_capture_options[] = {
    { "monitor_index", "Index of display monitor to capture", OFFSET(monitor_index),     AV_OPT_TYPE_INT, {.i64 = 0}, 0, 8, FLAGS },
    { "framerate", "Capture framerate", OFFSET(framerate),      AV_OPT_TYPE_VIDEO_RATE, {.str = "60"}, 0, INT_MAX, FLAGS },
    { "duplicate_output", "Use display output duplication for screen capture", OFFSET(duplicate_output),      AV_OPT_TYPE_BOOL, {.i64 = 1}, 0, 1, FLAGS },

    { "capture_mode", "Capture synchronization mode", OFFSET(capture_mode),  AV_OPT_TYPE_INT, {.i64 = AMF_DISPLAYCAPTURE_MODE_KEEP_FRAMERATE}, 0, 2, FLAGS, "mode" },
    { "keep_framerate", "Capture component maintains the frame rate", 0, AV_OPT_TYPE_CONST,        {.i64 = AMF_DISPLAYCAPTURE_MODE_KEEP_FRAMERATE}, 0, 0, FLAGS, "mode" },
    { "wait_for_present", "Capture component waits for flip (present) event", 0, AV_OPT_TYPE_CONST,       {.i64 = AMF_DISPLAYCAPTURE_MODE_WAIT_FOR_PRESENT}, 0, 0, FLAGS, "mode" },
    { "get_current", "Returns current visible surface immediately", 0, AV_OPT_TYPE_CONST,       {.i64 = AMF_DISPLAYCAPTURE_MODE_GET_CURRENT_SURFACE}, 0, 0, FLAGS, "mode" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(amf_capture);

// need to increase precision for capture timing accuracy
#if defined (_WIN32)

#include <timeapi.h>

typedef WINMMAPI MMRESULT (WINAPI *timeBeginPeriod_fn)( UINT uPeriod);
typedef WINMMAPI MMRESULT (WINAPI *timeEndPeriod_fn)(UINT uPeriod);

static void amf_increase_timer_precision(AMFGrabContext *ctx)
{
    ctx->winmmdll = dlopen("Winmm.dll", 0);
    if(ctx->winmmdll){
        timeBeginPeriod_fn fn = (timeBeginPeriod_fn)dlsym(ctx->winmmdll, "timeBeginPeriod");
        if(fn){
            ctx->timerPrecision = 1;
            while (fn(ctx->timerPrecision) == TIMERR_NOCANDO)
            {
                ++ctx->timerPrecision;
            }
        }
    }
}
static void amf_restore_timer_precision(AMFGrabContext *ctx)
{
    if(ctx->winmmdll){
        timeEndPeriod_fn fn = (timeEndPeriod_fn)dlsym(ctx->winmmdll, "timeEndPeriod");
        if(fn)
            fn(ctx->timerPrecision);
        dlclose(ctx->winmmdll);
        ctx->winmmdll = 0;
    }
}
#endif

static void amf_release_surface(void *opaque, uint8_t *data)
{
    int ref = 0;
    if(!!data){
        AMFInterface *surface = (AMFInterface*)(data);
        if (surface && surface->pVtbl)
            ref = surface->pVtbl->Release(surface);
    }
}

static av_cold void amf_uninit(AVFilterContext *avctx)
{
    AMFGrabContext *ctx = avctx->priv;

    if (ctx->capture) {
        ctx->capture->pVtbl->Drain(ctx->capture);
        ctx->capture->pVtbl->Terminate(ctx->capture);
        ctx->capture->pVtbl->Release(ctx->capture);
        ctx->capture = NULL;
    }

    av_buffer_unref(&ctx->device_ctx_ref);
#if defined (_WIN32)
    amf_restore_timer_precision(ctx);
#endif
}

static av_cold int amf_init(AVFilterContext *avctx)
{
    AMFGrabContext *ctx = avctx->priv;
#if defined (_WIN32)
    amf_increase_timer_precision(ctx);
#endif
    ctx->eof = 0;
    av_log(avctx, AV_LOG_VERBOSE, "Initializing AMF screen capture\n");

    return 0;
}

static int amf_init_vsrc(AVFilterLink *outlink)
{
    FilterLink            *link = ff_filter_link(outlink);
    AVFilterContext       *avctx = outlink->src;
    AMFGrabContext        *ctx = avctx->priv;
    AVHWDeviceContext     *hw_device_ctx = (AVHWDeviceContext*)ctx->device_ctx_ref->data;
    AVAMFDeviceContext    *amf_device_ctx = (AVAMFDeviceContext*)hw_device_ctx->hwctx;
    AMF_RESULT            res;
    AMFRate               framerate;
    AMFVariantStruct var = {0};
    AMFSize resolution = {0};

    res = amf_device_ctx->factory->pVtbl->CreateComponent(amf_device_ctx->factory,
                                                          amf_device_ctx->context,
                                                          AMFDisplayCapture,
                                                          &ctx->capture);
    AMF_RETURN_IF_FALSE(ctx, res == AMF_OK, AVERROR_FILTER_NOT_FOUND, "CreateComponent(%ls) failed with error %d\n", AMFDisplayCapture, res);

    AMF_ASSIGN_PROPERTY_INT64(res, ctx->capture, AMF_DISPLAYCAPTURE_MONITOR_INDEX, ctx->monitor_index);
    if (res != AMF_OK) {
        av_log(avctx, AV_LOG_ERROR, "Failed to set monitor index: %d\n", res);
        return AVERROR_EXTERNAL;
    }

    if (ctx->framerate.num > 0 && ctx->framerate.den > 0)
        framerate = AMFConstructRate(ctx->framerate.num, ctx->framerate.den);
    else
        framerate = AMFConstructRate(30, 1);

    AMF_ASSIGN_PROPERTY_BOOL(res, ctx->capture, AMF_DISPLAYCAPTURE_DUPLICATEOUTPUT, ctx->duplicate_output);
    if (res != AMF_OK) {
        av_log(avctx, AV_LOG_ERROR, "Failed to set AMF_DISPLAYCAPTURE_DUPLICATEOUTPUT: %d\n", res);
        return AVERROR_EXTERNAL;
    }

    AMF_ASSIGN_PROPERTY_RATE(res, ctx->capture, AMF_DISPLAYCAPTURE_FRAMERATE, framerate);
    if (res != AMF_OK) {
        av_log(avctx, AV_LOG_ERROR, "Failed to set framerate: %d\n", res);
        return AVERROR_EXTERNAL;
    }

    AMF_ASSIGN_PROPERTY_INT64(res, ctx->capture, AMF_DISPLAYCAPTURE_MODE, ctx->capture_mode);
    if (res != AMF_OK) {
        av_log(avctx, AV_LOG_WARNING, "Failed to set capture mode: %d\n", res);
    }

    res = ctx->capture->pVtbl->Init(ctx->capture, AMF_SURFACE_UNKNOWN, 0, 0);
    if (res != AMF_OK) {
        av_log(avctx, AV_LOG_ERROR, "Failed to initialize capture component: %d\n", res);
        return AVERROR_EXTERNAL;
    }

    res = ctx->capture->pVtbl->GetProperty(ctx->capture, AMF_DISPLAYCAPTURE_RESOLUTION, &var);
    if (res == AMF_OK && var.type == AMF_VARIANT_SIZE) {
        resolution = var.sizeValue;
        outlink->w = resolution.width;
        outlink->h = resolution.height;

        av_log(avctx, AV_LOG_INFO, "Capture resolution: %dx%d\n",
               outlink->w, outlink->h);
    } else {
        av_log(avctx, AV_LOG_ERROR, "Failed to get capture resolution from AMF\n");
        AMFVariantClear(&var);
        return AVERROR_EXTERNAL;
    }

    res = ctx->capture->pVtbl->GetProperty(ctx->capture, AMF_DISPLAYCAPTURE_FORMAT, &var);
    if (res == AMF_OK && var.type == AMF_VARIANT_INT64) {
        ctx->format = (AMF_SURFACE_FORMAT)var.int64Value;
        av_log(avctx, AV_LOG_INFO, "Capture format: %d\n", ctx->format);
    } else {
        ctx->format = AMF_SURFACE_BGRA;
        av_log(avctx, AV_LOG_WARNING, "Failed to get format, assuming BGRA\n");
    }


    outlink->time_base = (AVRational){framerate.den, framerate.num};
    link->frame_rate = (AVRational){framerate.num, framerate.den};
    AMFVariantClear(&var);
    return 0;
}

static int amf_config_props(AVFilterLink *outlink)
{
    FilterLink *link = ff_filter_link(outlink);
    AVFilterContext *avctx = outlink->src;
    AMFGrabContext *ctx = avctx->priv;
    AVHWDeviceContext *device_ctx;
    int ret;
    int pool_size = 1;

    av_buffer_unref(&ctx->device_ctx_ref);

    if (avctx->hw_device_ctx) {
        device_ctx = (AVHWDeviceContext*)avctx->hw_device_ctx->data;
        if (device_ctx->type == AV_HWDEVICE_TYPE_AMF)
        {
            ctx->device_ctx_ref = av_buffer_ref(avctx->hw_device_ctx);
        } else {
            ret = av_hwdevice_ctx_create_derived(&ctx->device_ctx_ref, AV_HWDEVICE_TYPE_AMF, avctx->hw_device_ctx, 0);
            AMF_GOTO_FAIL_IF_FALSE(avctx, ret == 0, ret, "Failed to create derived AMF device context: %s\n", av_err2str(ret));
        }
    } else {
        ret = av_hwdevice_ctx_create(&ctx->device_ctx_ref, AV_HWDEVICE_TYPE_AMF, NULL, NULL, 0);
        AMF_GOTO_FAIL_IF_FALSE(avctx, ret == 0, ret, "Failed to create  hardware device context (AMF) : %s\n", av_err2str(ret));
    }
    if ((ret = amf_init_vsrc(outlink)) == 0) {
        AVHWDeviceContext *device_ctx = (AVHWDeviceContext*)ctx->device_ctx_ref->data;
        if (device_ctx->type == AV_HWDEVICE_TYPE_AMF) {
            AVHWFramesContext *frames_ctx;
            link->hw_frames_ctx = av_hwframe_ctx_alloc(ctx->device_ctx_ref);
            AMF_GOTO_FAIL_IF_FALSE(avctx, !!link->hw_frames_ctx, AVERROR(ENOMEM), "av_hwframe_ctx_alloc failed\n");

            frames_ctx = (AVHWFramesContext*)link->hw_frames_ctx->data;
            frames_ctx->format = AV_PIX_FMT_AMF_SURFACE;
            frames_ctx->sw_format = av_amf_to_av_format(ctx->format);
            frames_ctx->initial_pool_size = pool_size;
            if (avctx->extra_hw_frames > 0)
                frames_ctx->initial_pool_size += avctx->extra_hw_frames;

            frames_ctx->width = outlink->w;
            frames_ctx->height = outlink->h;

            ret = av_hwframe_ctx_init(link->hw_frames_ctx);
            if (ret < 0) {
                av_log(avctx, AV_LOG_ERROR, "Failed to initialize hardware frames context: %s\n",
                       av_err2str(ret));

                return ret;
            }

            if (!link->hw_frames_ctx)
                return AVERROR(ENOMEM);
        }
        return 0;
    }
fail:
    amf_uninit(avctx);
    return ret;
}

static int amf_capture_frame(AVFilterLink *outlink)
{
    AVFilterContext *avctx = outlink->src;
    AMFGrabContext *ctx = avctx->priv;
    AMFSurface *surface = NULL;
    AVFrame *frame = NULL;
    AMF_RESULT res;
    AMFData *data_out = NULL;
    FilterLink *fl = ff_filter_link(outlink);
    int                 format_amf;
    int                 i;
    int                 ret;
    AMFPlane            *plane;

    if (ctx->eof)
        return AVERROR_EOF;

    res = ctx->capture->pVtbl->QueryOutput(ctx->capture, &data_out);

    if (res == AMF_REPEAT) {
        av_log(0, AV_LOG_DEBUG, "AMF capture returned res = AMF_REPEAT\n");
        return AVERROR(EAGAIN);
    }

    if (res == AMF_EOF) {
        ctx->eof = 1;
        av_log(avctx, AV_LOG_DEBUG, "Capture reached EOF\n");
        return AVERROR_EOF;
    }

    if (res != AMF_OK || !data_out) {
        if (res != AMF_OK)
            av_log(avctx, AV_LOG_WARNING, "QueryOutput failed: %d\n", res);

        return AVERROR(EAGAIN);
    }

    AMFGuid guid = IID_AMFSurface();
    ret = data_out->pVtbl->QueryInterface(data_out, &guid, (void**)&surface);
    data_out->pVtbl->Release(data_out);
    if (ret != AMF_OK || !surface) {
        av_log(avctx, AV_LOG_ERROR, "QueryInterface(IID_AMFSurface) failed: %d\n", ret);
        return AVERROR(EAGAIN);
    }

    frame = av_frame_alloc();
    if (!frame) {
        surface->pVtbl->Release(surface);
        return AVERROR(ENOMEM);
    }
    frame->format = outlink->format;
    frame->width = outlink->w;
    frame->height = outlink->h;
    frame->sample_aspect_ratio = (AVRational){1, 1};

    amf_pts pts = surface->pVtbl->GetPts(surface);
    frame->pts = av_rescale_q(pts, AMF_TIME_BASE_Q, outlink->time_base);

    if (fl->hw_frames_ctx) {
        frame->format =  AV_PIX_FMT_AMF_SURFACE;
        frame->data[0] = (uint8_t*)surface;
        frame->buf[0] = av_buffer_create((uint8_t*)surface, sizeof(surface),
                                         amf_release_surface, NULL, 0);
        frame->hw_frames_ctx = av_buffer_ref(fl->hw_frames_ctx);
        if (!frame->buf[0]) {
            av_frame_free(&frame);
            surface->pVtbl->Release(surface);
            return AVERROR(ENOMEM);
        }
    } else {
        ret = surface->pVtbl->Convert(surface, AMF_MEMORY_HOST);
        AMF_RETURN_IF_FALSE(avctx, ret == AMF_OK, AVERROR_UNKNOWN, "Convert(amf::AMF_MEMORY_HOST) failed with error %d\n", ret);

        for (i = 0; i < surface->pVtbl->GetPlanesCount(surface); i++) {
            plane = surface->pVtbl->GetPlaneAt(surface, i);
            frame->data[i] = plane->pVtbl->GetNative(plane);
            frame->linesize[i] = plane->pVtbl->GetHPitch(plane);
        }

        frame->buf[0] = av_buffer_create((uint8_t *)surface, sizeof(surface),
                                         amf_release_surface, (void*)avctx,
                                         AV_BUFFER_FLAG_READONLY);
        AMF_RETURN_IF_FALSE(avctx, !!frame->buf[0], AVERROR(ENOMEM), "av_buffer_create for amf surface failed.");

        format_amf = surface->pVtbl->GetFormat(surface);
        frame->format = av_amf_to_av_format(format_amf);
    }

    return ff_filter_frame(outlink, frame);
}

static const AVFilterPad amf_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = amf_capture_frame,
        .config_props  = amf_config_props,
    },
};

const FFFilter ff_vsrc_amf_capture = {
    .p.name        = "vsrc_amf",
    .p.description = NULL_IF_CONFIG_SMALL("AMD AMF screen capture"),
    .p.priv_class  = &amf_capture_class,
    .p.inputs      = NULL,
    .p.flags       = AVFILTER_FLAG_HWDEVICE,
    .priv_size     = sizeof(AMFGrabContext),
    .init          = amf_init,
    .uninit        = amf_uninit,
    FILTER_OUTPUTS(amf_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_AMF_SURFACE),
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
