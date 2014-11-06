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

#include "libavutil/avassert.h"
#include "libavutil/buffer.h"
#include "libavutil/frame.h"
#include "libavutil/pixfmt.h"

typedef struct VDPAUContext {
    Display *dpy;

    VdpDevice  device;
    VdpDecoder decoder;
    VdpGetProcAddress *get_proc_address;

    VdpGetErrorString                               *get_error_string;
    VdpGetInformationString                         *get_information_string;
    VdpDeviceDestroy                                *device_destroy;
#if 1 // for ffmpegs older vdpau API, not the oldest though
    VdpDecoderCreate                                *decoder_create;
    VdpDecoderDestroy                               *decoder_destroy;
    VdpDecoderRender                                *decoder_render;
#endif
    VdpVideoSurfaceCreate                           *video_surface_create;
    VdpVideoSurfaceDestroy                          *video_surface_destroy;
    VdpVideoSurfaceGetBitsYCbCr                     *video_surface_get_bits;
    VdpVideoSurfaceGetParameters                    *video_surface_get_parameters;
    VdpVideoSurfaceQueryGetPutBitsYCbCrCapabilities *video_surface_query;

    AVFrame *tmp_frame;

    enum AVPixelFormat pix_fmt;
    VdpYCbCrFormat vdpau_format;
} VDPAUContext;

int vdpau_api_ver = 2;

static void vdpau_uninit(AVCodecContext *s)
{
    InputStream  *ist = s->opaque;
    VDPAUContext *ctx = ist->hwaccel_ctx;

    ist->hwaccel_uninit        = NULL;
    ist->hwaccel_get_buffer    = NULL;
    ist->hwaccel_retrieve_data = NULL;

    if (ctx->decoder_destroy)
        ctx->decoder_destroy(ctx->decoder);

    if (ctx->device_destroy)
        ctx->device_destroy(ctx->device);

    if (ctx->dpy)
        XCloseDisplay(ctx->dpy);

    av_frame_free(&ctx->tmp_frame);

    av_freep(&ist->hwaccel_ctx);
    av_freep(&s->hwaccel_context);
}

static void vdpau_release_buffer(void *opaque, uint8_t *data)
{
    VdpVideoSurface surface = *(VdpVideoSurface*)data;
    VDPAUContext *ctx = opaque;

    ctx->video_surface_destroy(surface);
    av_freep(&data);
}

static int vdpau_get_buffer(AVCodecContext *s, AVFrame *frame, int flags)
{
    InputStream         *ist = s->opaque;
    VDPAUContext        *ctx = ist->hwaccel_ctx;
    VdpVideoSurface *surface;
    VdpStatus err;

    av_assert0(frame->format == AV_PIX_FMT_VDPAU);

    surface = av_malloc(sizeof(*surface));
    if (!surface)
        return AVERROR(ENOMEM);

    frame->buf[0] = av_buffer_create((uint8_t*)surface, sizeof(*surface),
                                     vdpau_release_buffer, ctx,
                                     AV_BUFFER_FLAG_READONLY);
    if (!frame->buf[0]) {
        av_freep(&surface);
        return AVERROR(ENOMEM);
    }

    // properly we should keep a pool of surfaces instead of creating
    // them anew for each frame, but since we don't care about speed
    // much in this code, we don't bother
    err = ctx->video_surface_create(ctx->device, VDP_CHROMA_TYPE_420,
                                    frame->width, frame->height, surface);
    if (err != VDP_STATUS_OK) {
        av_log(NULL, AV_LOG_ERROR, "Error allocating a VDPAU video surface: %s\n",
               ctx->get_error_string(err));
        av_buffer_unref(&frame->buf[0]);
        return AVERROR_UNKNOWN;
    }

    frame->data[3] = (uint8_t*)(uintptr_t)*surface;

    return 0;
}

static int vdpau_retrieve_data(AVCodecContext *s, AVFrame *frame)
{
    VdpVideoSurface surface = (VdpVideoSurface)(uintptr_t)frame->data[3];
    InputStream        *ist = s->opaque;
    VDPAUContext       *ctx = ist->hwaccel_ctx;
    VdpStatus err;
    int ret, chroma_type;

    err = ctx->video_surface_get_parameters(surface, &chroma_type,
                                            &ctx->tmp_frame->width,
                                            &ctx->tmp_frame->height);
    if (err != VDP_STATUS_OK) {
        av_log(NULL, AV_LOG_ERROR, "Error getting surface parameters: %s\n",
               ctx->get_error_string(err));
        return AVERROR_UNKNOWN;
    }
    ctx->tmp_frame->format = ctx->pix_fmt;

    ret = av_frame_get_buffer(ctx->tmp_frame, 32);
    if (ret < 0)
        return ret;

    ctx->tmp_frame->width  = frame->width;
    ctx->tmp_frame->height = frame->height;

    err = ctx->video_surface_get_bits(surface, ctx->vdpau_format,
                                      (void * const *)ctx->tmp_frame->data,
                                      ctx->tmp_frame->linesize);
    if (err != VDP_STATUS_OK) {
        av_log(NULL, AV_LOG_ERROR, "Error retrieving frame data from VDPAU: %s\n",
               ctx->get_error_string(err));
        ret = AVERROR_UNKNOWN;
        goto fail;
    }

    if (ctx->vdpau_format == VDP_YCBCR_FORMAT_YV12)
        FFSWAP(uint8_t*, ctx->tmp_frame->data[1], ctx->tmp_frame->data[2]);

    ret = av_frame_copy_props(ctx->tmp_frame, frame);
    if (ret < 0)
        goto fail;

    av_frame_unref(frame);
    av_frame_move_ref(frame, ctx->tmp_frame);
    return 0;

fail:
    av_frame_unref(ctx->tmp_frame);
    return ret;
}

static const int vdpau_formats[][2] = {
    { VDP_YCBCR_FORMAT_YV12, AV_PIX_FMT_YUV420P },
    { VDP_YCBCR_FORMAT_NV12, AV_PIX_FMT_NV12 },
    { VDP_YCBCR_FORMAT_YUYV, AV_PIX_FMT_YUYV422 },
    { VDP_YCBCR_FORMAT_UYVY, AV_PIX_FMT_UYVY422 },
};

static int vdpau_alloc(AVCodecContext *s)
{
    InputStream  *ist = s->opaque;
    int loglevel = (ist->hwaccel_id == HWACCEL_AUTO) ? AV_LOG_VERBOSE : AV_LOG_ERROR;
    AVVDPAUContext *vdpau_ctx;
    VDPAUContext *ctx;
    const char *display, *vendor;
    VdpStatus err;
    int i;

    ctx = av_mallocz(sizeof(*ctx));
    if (!ctx)
        return AVERROR(ENOMEM);

    ist->hwaccel_ctx           = ctx;
    ist->hwaccel_uninit        = vdpau_uninit;
    ist->hwaccel_get_buffer    = vdpau_get_buffer;
    ist->hwaccel_retrieve_data = vdpau_retrieve_data;

    ctx->tmp_frame = av_frame_alloc();
    if (!ctx->tmp_frame)
        goto fail;

    ctx->dpy = XOpenDisplay(ist->hwaccel_device);
    if (!ctx->dpy) {
        av_log(NULL, loglevel, "Cannot open the X11 display %s.\n",
               XDisplayName(ist->hwaccel_device));
        goto fail;
    }
    display = XDisplayString(ctx->dpy);

    err = vdp_device_create_x11(ctx->dpy, XDefaultScreen(ctx->dpy), &ctx->device,
                                &ctx->get_proc_address);
    if (err != VDP_STATUS_OK) {
        av_log(NULL, loglevel, "VDPAU device creation on X11 display %s failed.\n",
               display);
        goto fail;
    }

#define GET_CALLBACK(id, result)                                                \
do {                                                                            \
    void *tmp;                                                                  \
    err = ctx->get_proc_address(ctx->device, id, &tmp);                         \
    if (err != VDP_STATUS_OK) {                                                 \
        av_log(NULL, loglevel, "Error getting the " #id " callback.\n");        \
        goto fail;                                                              \
    }                                                                           \
    ctx->result = tmp;                                                          \
} while (0)

    GET_CALLBACK(VDP_FUNC_ID_GET_ERROR_STRING,               get_error_string);
    GET_CALLBACK(VDP_FUNC_ID_GET_INFORMATION_STRING,         get_information_string);
    GET_CALLBACK(VDP_FUNC_ID_DEVICE_DESTROY,                 device_destroy);
    if (vdpau_api_ver == 1) {
        GET_CALLBACK(VDP_FUNC_ID_DECODER_CREATE,                 decoder_create);
        GET_CALLBACK(VDP_FUNC_ID_DECODER_DESTROY,                decoder_destroy);
        GET_CALLBACK(VDP_FUNC_ID_DECODER_RENDER,                 decoder_render);
    }
    GET_CALLBACK(VDP_FUNC_ID_VIDEO_SURFACE_CREATE,           video_surface_create);
    GET_CALLBACK(VDP_FUNC_ID_VIDEO_SURFACE_DESTROY,          video_surface_destroy);
    GET_CALLBACK(VDP_FUNC_ID_VIDEO_SURFACE_GET_BITS_Y_CB_CR, video_surface_get_bits);
    GET_CALLBACK(VDP_FUNC_ID_VIDEO_SURFACE_GET_PARAMETERS,   video_surface_get_parameters);
    GET_CALLBACK(VDP_FUNC_ID_VIDEO_SURFACE_QUERY_GET_PUT_BITS_Y_CB_CR_CAPABILITIES,
                 video_surface_query);

    for (i = 0; i < FF_ARRAY_ELEMS(vdpau_formats); i++) {
        VdpBool supported;
        err = ctx->video_surface_query(ctx->device, VDP_CHROMA_TYPE_420,
                                       vdpau_formats[i][0], &supported);
        if (err != VDP_STATUS_OK) {
            av_log(NULL, loglevel,
                   "Error querying VDPAU surface capabilities: %s\n",
                   ctx->get_error_string(err));
            goto fail;
        }
        if (supported)
            break;
    }
    if (i == FF_ARRAY_ELEMS(vdpau_formats)) {
        av_log(NULL, loglevel,
               "No supported VDPAU format for retrieving the data.\n");
        return AVERROR(EINVAL);
    }
    ctx->vdpau_format = vdpau_formats[i][0];
    ctx->pix_fmt      = vdpau_formats[i][1];

    if (vdpau_api_ver == 1) {
        vdpau_ctx = av_vdpau_alloc_context();
        if (!vdpau_ctx)
            goto fail;
        vdpau_ctx->render = ctx->decoder_render;

        s->hwaccel_context = vdpau_ctx;
    } else
    if (av_vdpau_bind_context(s, ctx->device, ctx->get_proc_address, 0))
        goto fail;

    ctx->get_information_string(&vendor);
    av_log(NULL, AV_LOG_VERBOSE, "Using VDPAU -- %s -- on X11 display %s, "
           "to decode input stream #%d:%d.\n", vendor,
           display, ist->file_index, ist->st->index);

    return 0;

fail:
    av_log(NULL, loglevel, "VDPAU init failed for stream #%d:%d.\n",
           ist->file_index, ist->st->index);
    vdpau_uninit(s);
    return AVERROR(EINVAL);
}

static int vdpau_old_init(AVCodecContext *s)
{
    InputStream *ist = s->opaque;
    int loglevel = (ist->hwaccel_id == HWACCEL_AUTO) ? AV_LOG_VERBOSE : AV_LOG_ERROR;
    AVVDPAUContext *vdpau_ctx;
    VDPAUContext *ctx;
    VdpStatus err;
    int profile, ret;

    if (!ist->hwaccel_ctx) {
        ret = vdpau_alloc(s);
        if (ret < 0)
            return ret;
    }
    ctx       = ist->hwaccel_ctx;
    vdpau_ctx = s->hwaccel_context;

    ret = av_vdpau_get_profile(s, &profile);
    if (ret < 0) {
        av_log(NULL, loglevel, "No known VDPAU decoder profile for this stream.\n");
        return AVERROR(EINVAL);
    }

    if (ctx->decoder)
        ctx->decoder_destroy(ctx->decoder);

    err = ctx->decoder_create(ctx->device, profile,
                              s->coded_width, s->coded_height,
                              16, &ctx->decoder);
    if (err != VDP_STATUS_OK) {
        av_log(NULL, loglevel, "Error creating the VDPAU decoder: %s\n",
               ctx->get_error_string(err));
        return AVERROR_UNKNOWN;
    }

    vdpau_ctx->decoder = ctx->decoder;

    ist->hwaccel_get_buffer    = vdpau_get_buffer;
    ist->hwaccel_retrieve_data = vdpau_retrieve_data;

    return 0;
}

int vdpau_init(AVCodecContext *s)
{
    InputStream *ist = s->opaque;

    if (vdpau_api_ver == 1)
        return vdpau_old_init(s);

    if (!ist->hwaccel_ctx) {
        int ret = vdpau_alloc(s);
        if (ret < 0)
            return ret;
    }

    ist->hwaccel_get_buffer    = vdpau_get_buffer;
    ist->hwaccel_retrieve_data = vdpau_retrieve_data;

    return 0;
}
