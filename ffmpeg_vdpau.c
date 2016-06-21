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

#include <stdint.h>

#include <vdpau/vdpau.h>
#include <vdpau/vdpau_x11.h>

#include <X11/Xlib.h>

#include "ffmpeg.h"

#include "libavcodec/vdpau.h"

#include "libavutil/buffer.h"
#include "libavutil/frame.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_vdpau.h"
#include "libavutil/pixfmt.h"

typedef struct VDPAUContext {
    AVBufferRef *hw_frames_ctx;
    AVFrame *tmp_frame;
} VDPAUContext;

typedef struct VDPAUHWDevicePriv {
    VdpDeviceDestroy *device_destroy;
    Display *dpy;
} VDPAUHWDevicePriv;

static void device_free(AVHWDeviceContext *ctx)
{
    AVVDPAUDeviceContext *hwctx = ctx->hwctx;
    VDPAUHWDevicePriv     *priv = ctx->user_opaque;

    if (priv->device_destroy)
        priv->device_destroy(hwctx->device);
    if (priv->dpy)
        XCloseDisplay(priv->dpy);
    av_freep(&priv);
}

static void vdpau_uninit(AVCodecContext *s)
{
    InputStream  *ist = s->opaque;
    VDPAUContext *ctx = ist->hwaccel_ctx;

    ist->hwaccel_uninit        = NULL;
    ist->hwaccel_get_buffer    = NULL;
    ist->hwaccel_retrieve_data = NULL;

    av_buffer_unref(&ctx->hw_frames_ctx);
    av_frame_free(&ctx->tmp_frame);

    av_freep(&ist->hwaccel_ctx);
    av_freep(&s->hwaccel_context);
}

static int vdpau_get_buffer(AVCodecContext *s, AVFrame *frame, int flags)
{
    InputStream         *ist = s->opaque;
    VDPAUContext        *ctx = ist->hwaccel_ctx;

    return av_hwframe_get_buffer(ctx->hw_frames_ctx, frame, 0);
}

static int vdpau_retrieve_data(AVCodecContext *s, AVFrame *frame)
{
    InputStream        *ist = s->opaque;
    VDPAUContext       *ctx = ist->hwaccel_ctx;
    int ret;

    ret = av_hwframe_transfer_data(ctx->tmp_frame, frame, 0);
    if (ret < 0)
        return ret;

    ret = av_frame_copy_props(ctx->tmp_frame, frame);
    if (ret < 0) {
        av_frame_unref(ctx->tmp_frame);
        return ret;
    }

    av_frame_unref(frame);
    av_frame_move_ref(frame, ctx->tmp_frame);

    return 0;
}

static int vdpau_alloc(AVCodecContext *s)
{
    InputStream  *ist = s->opaque;
    int loglevel = (ist->hwaccel_id == HWACCEL_AUTO) ? AV_LOG_VERBOSE : AV_LOG_ERROR;
    VDPAUContext *ctx;
    const char *display, *vendor;
    VdpStatus err;
    int ret;

    VdpDevice                device;
    VdpGetProcAddress       *get_proc_address;
    VdpGetInformationString *get_information_string;

    VDPAUHWDevicePriv    *device_priv = NULL;
    AVBufferRef          *device_ref = NULL;
    AVHWDeviceContext    *device_ctx;
    AVVDPAUDeviceContext *device_hwctx;
    AVHWFramesContext    *frames_ctx;

    ctx = av_mallocz(sizeof(*ctx));
    if (!ctx)
        return AVERROR(ENOMEM);

    device_priv = av_mallocz(sizeof(*device_priv));
    if (!device_priv) {
        av_freep(&ctx);
        goto fail;
    }

    ist->hwaccel_ctx           = ctx;
    ist->hwaccel_uninit        = vdpau_uninit;
    ist->hwaccel_get_buffer    = vdpau_get_buffer;
    ist->hwaccel_retrieve_data = vdpau_retrieve_data;

    ctx->tmp_frame = av_frame_alloc();
    if (!ctx->tmp_frame)
        goto fail;

    device_priv->dpy = XOpenDisplay(ist->hwaccel_device);
    if (!device_priv->dpy) {
        av_log(NULL, loglevel, "Cannot open the X11 display %s.\n",
               XDisplayName(ist->hwaccel_device));
        goto fail;
    }
    display = XDisplayString(device_priv->dpy);

    err = vdp_device_create_x11(device_priv->dpy, XDefaultScreen(device_priv->dpy),
                                &device, &get_proc_address);
    if (err != VDP_STATUS_OK) {
        av_log(NULL, loglevel, "VDPAU device creation on X11 display %s failed.\n",
               display);
        goto fail;
    }

#define GET_CALLBACK(id, result)                                                \
do {                                                                            \
    void *tmp;                                                                  \
    err = get_proc_address(device, id, &tmp);                                   \
    if (err != VDP_STATUS_OK) {                                                 \
        av_log(NULL, loglevel, "Error getting the " #id " callback.\n");        \
        goto fail;                                                              \
    }                                                                           \
    result = tmp;                                                               \
} while (0)

    GET_CALLBACK(VDP_FUNC_ID_GET_INFORMATION_STRING, get_information_string);
    GET_CALLBACK(VDP_FUNC_ID_DEVICE_DESTROY,         device_priv->device_destroy);

    device_ref = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_VDPAU);
    if (!device_ref)
        goto fail;
    device_ctx                     = (AVHWDeviceContext*)device_ref->data;
    device_hwctx                   = device_ctx->hwctx;
    device_ctx->user_opaque        = device_priv;
    device_ctx->free               = device_free;
    device_hwctx->device           = device;
    device_hwctx->get_proc_address = get_proc_address;

    device_priv = NULL;

    ret = av_hwdevice_ctx_init(device_ref);
    if (ret < 0)
        goto fail;

    ctx->hw_frames_ctx = av_hwframe_ctx_alloc(device_ref);
    if (!ctx->hw_frames_ctx)
        goto fail;
    av_buffer_unref(&device_ref);

    frames_ctx            = (AVHWFramesContext*)ctx->hw_frames_ctx->data;
    frames_ctx->format    = AV_PIX_FMT_VDPAU;
    frames_ctx->sw_format = s->sw_pix_fmt;
    frames_ctx->width     = s->coded_width;
    frames_ctx->height    = s->coded_height;

    ret = av_hwframe_ctx_init(ctx->hw_frames_ctx);
    if (ret < 0)
        goto fail;

    if (av_vdpau_bind_context(s, device, get_proc_address, 0))
        goto fail;

    get_information_string(&vendor);
    av_log(NULL, AV_LOG_VERBOSE, "Using VDPAU -- %s -- on X11 display %s, "
           "to decode input stream #%d:%d.\n", vendor,
           display, ist->file_index, ist->st->index);

    return 0;

fail:
    av_log(NULL, loglevel, "VDPAU init failed for stream #%d:%d.\n",
           ist->file_index, ist->st->index);
    if (device_priv) {
        if (device_priv->device_destroy)
            device_priv->device_destroy(device);
        if (device_priv->dpy)
            XCloseDisplay(device_priv->dpy);
    }
    av_freep(&device_priv);
    av_buffer_unref(&device_ref);
    vdpau_uninit(s);
    return AVERROR(EINVAL);
}

int vdpau_init(AVCodecContext *s)
{
    InputStream *ist = s->opaque;

    if (!ist->hwaccel_ctx) {
        int ret = vdpau_alloc(s);
        if (ret < 0)
            return ret;
    }

    ist->hwaccel_get_buffer    = vdpau_get_buffer;
    ist->hwaccel_retrieve_data = vdpau_retrieve_data;

    return 0;
}
