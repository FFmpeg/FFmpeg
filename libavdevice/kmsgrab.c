/*
 * KMS/DRM input device
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

#include <fcntl.h>
#include <unistd.h>

#include <drm.h>
#include <drm_fourcc.h>
#include <drm_mode.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_drm.h"
#include "libavutil/internal.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/pixfmt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/time.h"

#include "libavformat/avformat.h"
#include "libavformat/internal.h"

typedef struct KMSGrabContext {
    const AVClass *class;

    AVBufferRef *device_ref;
    AVHWDeviceContext *device;
    AVDRMDeviceContext *hwctx;

    AVBufferRef *frames_ref;
    AVHWFramesContext *frames;

    uint32_t plane_id;
    uint32_t drm_format;
    unsigned int width;
    unsigned int height;

    int64_t frame_delay;
    int64_t frame_last;

    const char *device_path;
    enum AVPixelFormat format;
    int64_t drm_format_modifier;
    int64_t source_plane;
    int64_t source_crtc;
    AVRational framerate;
} KMSGrabContext;

static void kmsgrab_free_desc(void *opaque, uint8_t *data)
{
    AVDRMFrameDescriptor *desc = (AVDRMFrameDescriptor*)data;

    close(desc->objects[0].fd);

    av_free(desc);
}

static void kmsgrab_free_frame(void *opaque, uint8_t *data)
{
    AVFrame *frame = (AVFrame*)data;

    av_frame_free(&frame);
}

static int kmsgrab_read_packet(AVFormatContext *avctx, AVPacket *pkt)
{
    KMSGrabContext *ctx = avctx->priv_data;
    drmModePlane *plane;
    drmModeFB *fb;
    AVDRMFrameDescriptor *desc;
    AVFrame *frame;
    int64_t now;
    int err, fd;

    now = av_gettime();
    if (ctx->frame_last) {
        int64_t delay;
        while (1) {
            delay = ctx->frame_last + ctx->frame_delay - now;
            if (delay <= 0)
                break;
            av_usleep(delay);
            now = av_gettime();
        }
    }
    ctx->frame_last = now;

    plane = drmModeGetPlane(ctx->hwctx->fd, ctx->plane_id);
    if (!plane) {
        av_log(avctx, AV_LOG_ERROR, "Failed to get plane "
               "%"PRIu32".\n", ctx->plane_id);
        return AVERROR(EIO);
    }
    if (!plane->fb_id) {
        av_log(avctx, AV_LOG_ERROR, "Plane %"PRIu32" no longer has "
               "an associated framebuffer.\n", ctx->plane_id);
        return AVERROR(EIO);
    }

    fb = drmModeGetFB(ctx->hwctx->fd, plane->fb_id);
    if (!fb) {
        av_log(avctx, AV_LOG_ERROR, "Failed to get framebuffer "
               "%"PRIu32".\n", plane->fb_id);
        return AVERROR(EIO);
    }
    if (fb->width != ctx->width || fb->height != ctx->height) {
        av_log(avctx, AV_LOG_ERROR, "Plane %"PRIu32" framebuffer "
               "dimensions changed: now %"PRIu32"x%"PRIu32".\n",
               ctx->plane_id, fb->width, fb->height);
        return AVERROR(EIO);
    }
    if (!fb->handle) {
        av_log(avctx, AV_LOG_ERROR, "No handle set on framebuffer.\n");
        return AVERROR(EIO);
    }

    err = drmPrimeHandleToFD(ctx->hwctx->fd, fb->handle, O_RDONLY, &fd);
    if (err < 0) {
        err = errno;
        av_log(avctx, AV_LOG_ERROR, "Failed to get PRIME fd from "
               "framebuffer handle: %s.\n", strerror(errno));
        return AVERROR(err);
    }

    desc = av_mallocz(sizeof(*desc));
    if (!desc)
        return AVERROR(ENOMEM);

    *desc = (AVDRMFrameDescriptor) {
        .nb_objects = 1,
        .objects[0] = {
            .fd               = fd,
            .size             = fb->height * fb->pitch,
            .format_modifier  = ctx->drm_format_modifier,
        },
        .nb_layers = 1,
        .layers[0] = {
            .format           = ctx->drm_format,
            .nb_planes        = 1,
            .planes[0] = {
                .object_index = 0,
                .offset       = 0,
                .pitch        = fb->pitch,
            },
        },
    };

    frame = av_frame_alloc();
    if (!frame)
        return AVERROR(ENOMEM);

    frame->hw_frames_ctx = av_buffer_ref(ctx->frames_ref);
    if (!frame->hw_frames_ctx)
        return AVERROR(ENOMEM);

    frame->buf[0] = av_buffer_create((uint8_t*)desc, sizeof(*desc),
                                     &kmsgrab_free_desc, avctx, 0);
    if (!frame->buf[0])
        return AVERROR(ENOMEM);

    frame->data[0] = (uint8_t*)desc;
    frame->format  = AV_PIX_FMT_DRM_PRIME;
    frame->width   = fb->width;
    frame->height  = fb->height;

    drmModeFreeFB(fb);
    drmModeFreePlane(plane);

    pkt->buf = av_buffer_create((uint8_t*)frame, sizeof(*frame),
                                &kmsgrab_free_frame, avctx, 0);
    if (!pkt->buf)
        return AVERROR(ENOMEM);

    pkt->data   = (uint8_t*)frame;
    pkt->size   = sizeof(*frame);
    pkt->pts    = now;
    pkt->flags |= AV_PKT_FLAG_TRUSTED;

    return 0;
}

static const struct {
    enum AVPixelFormat pixfmt;
    uint32_t drm_format;
} kmsgrab_formats[] = {
#ifdef DRM_FORMAT_R8
    { AV_PIX_FMT_GRAY8,    DRM_FORMAT_R8       },
#endif
#ifdef DRM_FORMAT_R16
    { AV_PIX_FMT_GRAY16LE, DRM_FORMAT_R16      },
    { AV_PIX_FMT_GRAY16BE, DRM_FORMAT_R16      | DRM_FORMAT_BIG_ENDIAN },
#endif
    { AV_PIX_FMT_BGR8,     DRM_FORMAT_BGR233   },
    { AV_PIX_FMT_RGB555LE, DRM_FORMAT_XRGB1555 },
    { AV_PIX_FMT_RGB555BE, DRM_FORMAT_XRGB1555 | DRM_FORMAT_BIG_ENDIAN },
    { AV_PIX_FMT_BGR555LE, DRM_FORMAT_XBGR1555 },
    { AV_PIX_FMT_BGR555BE, DRM_FORMAT_XBGR1555 | DRM_FORMAT_BIG_ENDIAN },
    { AV_PIX_FMT_RGB565LE, DRM_FORMAT_RGB565   },
    { AV_PIX_FMT_RGB565BE, DRM_FORMAT_RGB565   | DRM_FORMAT_BIG_ENDIAN },
    { AV_PIX_FMT_BGR565LE, DRM_FORMAT_BGR565   },
    { AV_PIX_FMT_BGR565BE, DRM_FORMAT_BGR565   | DRM_FORMAT_BIG_ENDIAN },
    { AV_PIX_FMT_RGB24,    DRM_FORMAT_RGB888   },
    { AV_PIX_FMT_BGR24,    DRM_FORMAT_BGR888   },
    { AV_PIX_FMT_0RGB,     DRM_FORMAT_BGRX8888 },
    { AV_PIX_FMT_0BGR,     DRM_FORMAT_RGBX8888 },
    { AV_PIX_FMT_RGB0,     DRM_FORMAT_XBGR8888 },
    { AV_PIX_FMT_BGR0,     DRM_FORMAT_XRGB8888 },
    { AV_PIX_FMT_ARGB,     DRM_FORMAT_BGRA8888 },
    { AV_PIX_FMT_ABGR,     DRM_FORMAT_RGBA8888 },
    { AV_PIX_FMT_RGBA,     DRM_FORMAT_ABGR8888 },
    { AV_PIX_FMT_BGRA,     DRM_FORMAT_ARGB8888 },
    { AV_PIX_FMT_YUYV422,  DRM_FORMAT_YUYV     },
    { AV_PIX_FMT_YVYU422,  DRM_FORMAT_YVYU     },
    { AV_PIX_FMT_UYVY422,  DRM_FORMAT_UYVY     },
};

static av_cold int kmsgrab_read_header(AVFormatContext *avctx)
{
    KMSGrabContext *ctx = avctx->priv_data;
    drmModePlaneRes *plane_res = NULL;
    drmModePlane *plane = NULL;
    drmModeFB *fb = NULL;
    AVStream *stream;
    int err, i;

    for (i = 0; i < FF_ARRAY_ELEMS(kmsgrab_formats); i++) {
        if (kmsgrab_formats[i].pixfmt == ctx->format) {
            ctx->drm_format = kmsgrab_formats[i].drm_format;
            break;
        }
    }
    if (i >= FF_ARRAY_ELEMS(kmsgrab_formats)) {
        av_log(avctx, AV_LOG_ERROR, "Unsupported format %s.\n",
               av_get_pix_fmt_name(ctx->format));
        return AVERROR(EINVAL);
    }

    err = av_hwdevice_ctx_create(&ctx->device_ref, AV_HWDEVICE_TYPE_DRM,
                                 ctx->device_path, NULL, 0);
    if (err < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to open DRM device.\n");
        return err;
    }
    ctx->device = (AVHWDeviceContext*) ctx->device_ref->data;
    ctx->hwctx  = (AVDRMDeviceContext*)ctx->device->hwctx;

    err = drmSetClientCap(ctx->hwctx->fd,
                          DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
    if (err < 0) {
        av_log(avctx, AV_LOG_WARNING, "Failed to set universal planes "
               "capability: primary planes will not be usable.\n");
    }

    if (ctx->source_plane > 0) {
        plane = drmModeGetPlane(ctx->hwctx->fd, ctx->source_plane);
        if (!plane) {
            err = errno;
            av_log(avctx, AV_LOG_ERROR, "Failed to get plane %"PRId64": "
                   "%s.\n", ctx->source_plane, strerror(err));
            err = AVERROR(err);
            goto fail;
        }

        if (plane->fb_id == 0) {
            av_log(avctx, AV_LOG_ERROR, "Plane %"PRId64" does not have "
                   "an attached framebuffer.\n", ctx->source_plane);
            err = AVERROR(EINVAL);
            goto fail;
        }
    } else {
        plane_res = drmModeGetPlaneResources(ctx->hwctx->fd);
        if (!plane_res) {
            av_log(avctx, AV_LOG_ERROR, "Failed to get plane "
                   "resources: %s.\n", strerror(errno));
            err = AVERROR(EINVAL);
            goto fail;
        }

        for (i = 0; i < plane_res->count_planes; i++) {
            plane = drmModeGetPlane(ctx->hwctx->fd,
                                    plane_res->planes[i]);
            if (!plane) {
                err = errno;
                av_log(avctx, AV_LOG_VERBOSE, "Failed to get "
                       "plane %"PRIu32": %s.\n",
                       plane_res->planes[i], strerror(err));
                continue;
            }

            av_log(avctx, AV_LOG_DEBUG, "Plane %"PRIu32": "
                   "CRTC %"PRIu32" FB %"PRIu32".\n",
                   plane->plane_id, plane->crtc_id, plane->fb_id);

            if ((ctx->source_crtc > 0 &&
                 plane->crtc_id != ctx->source_crtc) ||
                plane->fb_id == 0) {
                // Either not connected to the target source CRTC
                // or not active.
                drmModeFreePlane(plane);
                plane = NULL;
                continue;
            }

            break;
        }

        if (i == plane_res->count_planes) {
            if (ctx->source_crtc > 0) {
                av_log(avctx, AV_LOG_ERROR, "No usable planes found on "
                       "CRTC %"PRId64".\n", ctx->source_crtc);
            } else {
                av_log(avctx, AV_LOG_ERROR, "No usable planes found.\n");
            }
            err = AVERROR(EINVAL);
            goto fail;
        }

        av_log(avctx, AV_LOG_INFO, "Using plane %"PRIu32" to "
               "locate framebuffers.\n", plane->plane_id);
    }

    ctx->plane_id = plane->plane_id;

    fb = drmModeGetFB(ctx->hwctx->fd, plane->fb_id);
    if (!fb) {
        err = errno;
        av_log(avctx, AV_LOG_ERROR, "Failed to get "
               "framebuffer %"PRIu32": %s.\n",
               plane->fb_id, strerror(err));
        err = AVERROR(err);
        goto fail;
    }

    av_log(avctx, AV_LOG_INFO, "Template framebuffer is %"PRIu32": "
           "%"PRIu32"x%"PRIu32" %"PRIu32"bpp %"PRIu32"b depth.\n",
           fb->fb_id, fb->width, fb->height, fb->bpp, fb->depth);

    ctx->width  = fb->width;
    ctx->height = fb->height;

    if (!fb->handle) {
        av_log(avctx, AV_LOG_ERROR, "No handle set on framebuffer: "
               "maybe you need some additional capabilities?\n");
        err = AVERROR(EINVAL);
        goto fail;
    }

    stream = avformat_new_stream(avctx, NULL);
    if (!stream) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    stream->codecpar->codec_id   = AV_CODEC_ID_WRAPPED_AVFRAME;
    stream->codecpar->width      = fb->width;
    stream->codecpar->height     = fb->height;
    stream->codecpar->format     = AV_PIX_FMT_DRM_PRIME;

    avpriv_set_pts_info(stream, 64, 1, 1000000);

    ctx->frames_ref = av_hwframe_ctx_alloc(ctx->device_ref);
    if (!ctx->frames_ref) {
        err = AVERROR(ENOMEM);
        goto fail;
    }
    ctx->frames = (AVHWFramesContext*)ctx->frames_ref->data;

    ctx->frames->format    = AV_PIX_FMT_DRM_PRIME;
    ctx->frames->sw_format = ctx->format,
    ctx->frames->width     = fb->width;
    ctx->frames->height    = fb->height;

    err = av_hwframe_ctx_init(ctx->frames_ref);
    if (err < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to initialise "
               "hardware frames context: %d.\n", err);
        goto fail;
    }

    ctx->frame_delay = av_rescale_q(1, (AVRational) { ctx->framerate.den,
                ctx->framerate.num }, AV_TIME_BASE_Q);

    err = 0;
fail:
    if (plane_res)
        drmModeFreePlaneResources(plane_res);
    if (plane)
        drmModeFreePlane(plane);
    if (fb)
        drmModeFreeFB(fb);

    return err;
}

static av_cold int kmsgrab_read_close(AVFormatContext *avctx)
{
    KMSGrabContext *ctx = avctx->priv_data;

    av_buffer_unref(&ctx->frames_ref);
    av_buffer_unref(&ctx->device_ref);

    return 0;
}

#define OFFSET(x) offsetof(KMSGrabContext, x)
#define FLAGS AV_OPT_FLAG_DECODING_PARAM
static const AVOption options[] = {
    { "device", "DRM device path",
      OFFSET(device_path), AV_OPT_TYPE_STRING,
      { .str = "/dev/dri/card0" }, 0, 0, FLAGS },
    { "format", "Pixel format for framebuffer",
      OFFSET(format), AV_OPT_TYPE_PIXEL_FMT,
      { .i64 = AV_PIX_FMT_BGR0 }, 0, UINT32_MAX, FLAGS },
    { "format_modifier", "DRM format modifier for framebuffer",
      OFFSET(drm_format_modifier), AV_OPT_TYPE_INT64,
      { .i64 = DRM_FORMAT_MOD_NONE }, 0, INT64_MAX, FLAGS },
    { "crtc_id", "CRTC ID to define capture source",
      OFFSET(source_crtc), AV_OPT_TYPE_INT64,
      { .i64 = 0 }, 0, UINT32_MAX, FLAGS },
    { "plane_id", "Plane ID to define capture source",
      OFFSET(source_plane), AV_OPT_TYPE_INT64,
      { .i64 = 0 }, 0, UINT32_MAX, FLAGS },
    { "framerate", "Framerate to capture at",
      OFFSET(framerate), AV_OPT_TYPE_RATIONAL,
      { .dbl = 30.0 }, 0, 1000, FLAGS },
    { NULL },
};

static const AVClass kmsgrab_class = {
    .class_name = "kmsgrab indev",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVInputFormat ff_kmsgrab_demuxer = {
    .name           = "kmsgrab",
    .long_name      = NULL_IF_CONFIG_SMALL("KMS screen capture"),
    .priv_data_size = sizeof(KMSGrabContext),
    .read_header    = &kmsgrab_read_header,
    .read_packet    = &kmsgrab_read_packet,
    .read_close     = &kmsgrab_read_close,
    .flags          = AVFMT_NOFILE,
    .priv_class     = &kmsgrab_class,
};
