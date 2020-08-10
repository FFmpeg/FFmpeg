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

// Required for compatibility when building against libdrm < 2.4.83.
#ifndef DRM_FORMAT_MOD_INVALID
#define DRM_FORMAT_MOD_INVALID ((1ULL << 56) - 1)
#endif

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
    int fb2_available;

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
    int i;

    for (i = 0; i < desc->nb_objects; i++)
        close(desc->objects[i].fd);

    av_free(desc);
}

static void kmsgrab_free_frame(void *opaque, uint8_t *data)
{
    AVFrame *frame = (AVFrame*)data;

    av_frame_free(&frame);
}

static int kmsgrab_get_fb(AVFormatContext *avctx,
                          drmModePlane *plane,
                          AVDRMFrameDescriptor *desc)
{
    KMSGrabContext *ctx = avctx->priv_data;
    drmModeFB *fb = NULL;
    int err, fd;

    fb = drmModeGetFB(ctx->hwctx->fd, plane->fb_id);
    if (!fb) {
        err = errno;
        av_log(avctx, AV_LOG_ERROR, "Failed to get framebuffer "
               "%"PRIu32": %s.\n", plane->fb_id, strerror(err));
        err = AVERROR(err);
        goto fail;
    }
    if (fb->width != ctx->width || fb->height != ctx->height) {
        av_log(avctx, AV_LOG_ERROR, "Plane %"PRIu32" framebuffer "
               "dimensions changed: now %"PRIu32"x%"PRIu32".\n",
               ctx->plane_id, fb->width, fb->height);
        err = AVERROR(EIO);
        goto fail;
    }
    if (!fb->handle) {
        av_log(avctx, AV_LOG_ERROR, "No handle set on framebuffer.\n");
        err = AVERROR(EIO);
        goto fail;
    }

    err = drmPrimeHandleToFD(ctx->hwctx->fd, fb->handle, O_RDONLY, &fd);
    if (err < 0) {
        err = errno;
        av_log(avctx, AV_LOG_ERROR, "Failed to get PRIME fd from "
               "framebuffer handle: %s.\n", strerror(err));
        err = AVERROR(err);
        goto fail;
    }

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

    err = 0;
fail:
    drmModeFreeFB(fb);
    return err;
}

#if HAVE_LIBDRM_GETFB2
static int kmsgrab_get_fb2(AVFormatContext *avctx,
                           drmModePlane *plane,
                           AVDRMFrameDescriptor *desc)
{
    KMSGrabContext *ctx = avctx->priv_data;
    drmModeFB2 *fb;
    int err, i, nb_objects;

    fb = drmModeGetFB2(ctx->hwctx->fd, plane->fb_id);
    if (!fb) {
        err = errno;
        av_log(avctx, AV_LOG_ERROR, "Failed to get framebuffer "
               "%"PRIu32": %s.\n", plane->fb_id, strerror(err));
        return AVERROR(err);
    }
    if (fb->pixel_format != ctx->drm_format) {
        av_log(avctx, AV_LOG_ERROR, "Plane %"PRIu32" framebuffer "
               "format changed: now %"PRIx32".\n",
               ctx->plane_id, fb->pixel_format);
        err = AVERROR(EIO);
        goto fail;
    }
    if (fb->modifier != ctx->drm_format_modifier) {
        av_log(avctx, AV_LOG_ERROR, "Plane %"PRIu32" framebuffer "
               "format modifier changed: now %"PRIx64".\n",
               ctx->plane_id, fb->modifier);
        err = AVERROR(EIO);
        goto fail;
    }
    if (fb->width != ctx->width || fb->height != ctx->height) {
        av_log(avctx, AV_LOG_ERROR, "Plane %"PRIu32" framebuffer "
               "dimensions changed: now %"PRIu32"x%"PRIu32".\n",
               ctx->plane_id, fb->width, fb->height);
        err = AVERROR(EIO);
        goto fail;
    }
    if (!fb->handles[0]) {
        av_log(avctx, AV_LOG_ERROR, "No handle set on framebuffer.\n");
        err = AVERROR(EIO);
        goto fail;
    }

    *desc = (AVDRMFrameDescriptor) {
        .nb_layers = 1,
        .layers[0] = {
            .format = ctx->drm_format,
        },
    };

    nb_objects = 0;
    for (i = 0; i < 4 && fb->handles[i]; i++) {
        size_t size;
        int dup = 0, j, obj;

        size = fb->offsets[i] + fb->height * fb->pitches[i];

        for (j = 0; j < i; j++) {
            if (fb->handles[i] == fb->handles[j]) {
                dup = 1;
                break;
            }
        }
        if (dup) {
            obj = desc->layers[0].planes[j].object_index;

            if (desc->objects[j].size < size)
                desc->objects[j].size = size;

            desc->layers[0].planes[i] = (AVDRMPlaneDescriptor) {
                .object_index = obj,
                .offset       = fb->offsets[i],
                .pitch        = fb->pitches[i],
            };

        } else {
            int fd;
            err = drmPrimeHandleToFD(ctx->hwctx->fd, fb->handles[i],
                                     O_RDONLY, &fd);
            if (err < 0) {
                err = errno;
                av_log(avctx, AV_LOG_ERROR, "Failed to get PRIME fd from "
                       "framebuffer handle: %s.\n", strerror(err));
                err = AVERROR(err);
                goto fail;
            }

            obj = nb_objects++;
            desc->objects[obj] = (AVDRMObjectDescriptor) {
                .fd              = fd,
                .size            = size,
                .format_modifier = fb->modifier,
            };
            desc->layers[0].planes[i] = (AVDRMPlaneDescriptor) {
                .object_index = obj,
                .offset       = fb->offsets[i],
                .pitch        = fb->pitches[i],
            };
        }
    }
    desc->nb_objects = nb_objects;
    desc->layers[0].nb_planes = i;

    err = 0;
fail:
    drmModeFreeFB2(fb);
    return err;
}
#endif

static int kmsgrab_read_packet(AVFormatContext *avctx, AVPacket *pkt)
{
    KMSGrabContext *ctx = avctx->priv_data;
    drmModePlane *plane = NULL;
    AVDRMFrameDescriptor *desc = NULL;
    AVFrame *frame = NULL;
    int64_t now;
    int err;

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
        err = errno;
        av_log(avctx, AV_LOG_ERROR, "Failed to get plane "
               "%"PRIu32": %s.\n", ctx->plane_id, strerror(err));
        err = AVERROR(err);
        goto fail;
    }
    if (!plane->fb_id) {
        av_log(avctx, AV_LOG_ERROR, "Plane %"PRIu32" no longer has "
               "an associated framebuffer.\n", ctx->plane_id);
        err = AVERROR(EIO);
        goto fail;
    }

    desc = av_mallocz(sizeof(*desc));
    if (!desc) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

#if HAVE_LIBDRM_GETFB2
    if (ctx->fb2_available)
        err = kmsgrab_get_fb2(avctx, plane, desc);
    else
#endif
        err = kmsgrab_get_fb(avctx, plane, desc);
    if (err < 0)
        goto fail;

    frame = av_frame_alloc();
    if (!frame) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    frame->hw_frames_ctx = av_buffer_ref(ctx->frames_ref);
    if (!frame->hw_frames_ctx) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    frame->buf[0] = av_buffer_create((uint8_t*)desc, sizeof(*desc),
                                     &kmsgrab_free_desc, avctx, 0);
    if (!frame->buf[0]) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    frame->data[0] = (uint8_t*)desc;
    frame->format  = AV_PIX_FMT_DRM_PRIME;
    frame->width   = ctx->width;
    frame->height  = ctx->height;

    drmModeFreePlane(plane);
    plane = NULL;
    desc  = NULL;

    pkt->buf = av_buffer_create((uint8_t*)frame, sizeof(*frame),
                                &kmsgrab_free_frame, avctx, 0);
    if (!pkt->buf) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    pkt->data   = (uint8_t*)frame;
    pkt->size   = sizeof(*frame);
    pkt->pts    = now;
    pkt->flags |= AV_PKT_FLAG_TRUSTED;

    return 0;

fail:
    drmModeFreePlane(plane);
    av_freep(&desc);
    av_frame_free(&frame);
    return err;
}

static const struct {
    enum AVPixelFormat pixfmt;
    uint32_t drm_format;
} kmsgrab_formats[] = {
    // Monochrome.
#ifdef DRM_FORMAT_R8
    { AV_PIX_FMT_GRAY8,    DRM_FORMAT_R8       },
#endif
#ifdef DRM_FORMAT_R16
    { AV_PIX_FMT_GRAY16LE, DRM_FORMAT_R16      },
    { AV_PIX_FMT_GRAY16BE, DRM_FORMAT_R16      | DRM_FORMAT_BIG_ENDIAN },
#endif
    // <8-bit RGB.
    { AV_PIX_FMT_BGR8,     DRM_FORMAT_BGR233   },
    { AV_PIX_FMT_RGB555LE, DRM_FORMAT_XRGB1555 },
    { AV_PIX_FMT_RGB555BE, DRM_FORMAT_XRGB1555 | DRM_FORMAT_BIG_ENDIAN },
    { AV_PIX_FMT_BGR555LE, DRM_FORMAT_XBGR1555 },
    { AV_PIX_FMT_BGR555BE, DRM_FORMAT_XBGR1555 | DRM_FORMAT_BIG_ENDIAN },
    { AV_PIX_FMT_RGB565LE, DRM_FORMAT_RGB565   },
    { AV_PIX_FMT_RGB565BE, DRM_FORMAT_RGB565   | DRM_FORMAT_BIG_ENDIAN },
    { AV_PIX_FMT_BGR565LE, DRM_FORMAT_BGR565   },
    { AV_PIX_FMT_BGR565BE, DRM_FORMAT_BGR565   | DRM_FORMAT_BIG_ENDIAN },
    // 8-bit RGB.
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
    // 10-bit RGB.
    { AV_PIX_FMT_X2RGB10LE, DRM_FORMAT_XRGB2101010 },
    { AV_PIX_FMT_X2RGB10BE, DRM_FORMAT_XRGB2101010 | DRM_FORMAT_BIG_ENDIAN },
    // 8-bit YUV 4:2:0.
    { AV_PIX_FMT_NV12,     DRM_FORMAT_NV12     },
    // 8-bit YUV 4:2:2.
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
#if HAVE_LIBDRM_GETFB2
    drmModeFB2 *fb2 = NULL;
#endif
    AVStream *stream;
    int err, i;

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
            err = errno;
            av_log(avctx, AV_LOG_ERROR, "Failed to get plane "
                   "resources: %s.\n", strerror(err));
            err = AVERROR(err);
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

#if HAVE_LIBDRM_GETFB2
    fb2 = drmModeGetFB2(ctx->hwctx->fd, plane->fb_id);
    if (!fb2 && errno == ENOSYS) {
        av_log(avctx, AV_LOG_INFO, "GETFB2 not supported, "
               "will try to use GETFB instead.\n");
    } else if (!fb2) {
        err = errno;
        av_log(avctx, AV_LOG_ERROR, "Failed to get "
               "framebuffer %"PRIu32": %s.\n",
               plane->fb_id, strerror(err));
        err = AVERROR(err);
        goto fail;
    } else {
        av_log(avctx, AV_LOG_INFO, "Template framebuffer is "
               "%"PRIu32": %"PRIu32"x%"PRIu32" "
               "format %"PRIx32" modifier %"PRIx64" flags %"PRIx32".\n",
               fb2->fb_id, fb2->width, fb2->height,
               fb2->pixel_format, fb2->modifier, fb2->flags);

        ctx->width  = fb2->width;
        ctx->height = fb2->height;

        if (!fb2->handles[0]) {
            av_log(avctx, AV_LOG_ERROR, "No handle set on framebuffer: "
                   "maybe you need some additional capabilities?\n");
            err = AVERROR(EINVAL);
            goto fail;
        }

        for (i = 0; i < FF_ARRAY_ELEMS(kmsgrab_formats); i++) {
            if (kmsgrab_formats[i].drm_format == fb2->pixel_format) {
                if (ctx->format != AV_PIX_FMT_NONE &&
                    ctx->format != kmsgrab_formats[i].pixfmt) {
                    av_log(avctx, AV_LOG_ERROR, "Framebuffer pixel format "
                           "%"PRIx32" does not match expected format.\n",
                           fb2->pixel_format);
                    err = AVERROR(EINVAL);
                    goto fail;
                }
                ctx->drm_format = fb2->pixel_format;
                ctx->format     = kmsgrab_formats[i].pixfmt;
                break;
            }
        }
        if (i == FF_ARRAY_ELEMS(kmsgrab_formats)) {
            av_log(avctx, AV_LOG_ERROR, "Framebuffer pixel format "
                   "%"PRIx32" is not a known supported format.\n",
                   fb2->pixel_format);
            err = AVERROR(EINVAL);
            goto fail;
        }
        if (ctx->drm_format_modifier != DRM_FORMAT_MOD_INVALID &&
            ctx->drm_format_modifier != fb2->modifier) {
            av_log(avctx, AV_LOG_ERROR, "Framebuffer format modifier "
                   "%"PRIx64" does not match expected modifier.\n",
                   fb2->modifier);
            err = AVERROR(EINVAL);
            goto fail;
        } else {
            ctx->drm_format_modifier = fb2->modifier;
        }
        av_log(avctx, AV_LOG_VERBOSE, "Format is %s, from "
               "DRM format %"PRIx32" modifier %"PRIx64".\n",
               av_get_pix_fmt_name(ctx->format),
               ctx->drm_format, ctx->drm_format_modifier);

        ctx->fb2_available = 1;
    }
#endif

    if (!ctx->fb2_available) {
        if (ctx->format == AV_PIX_FMT_NONE) {
            // Backward compatibility: assume BGR0 if no format supplied.
            ctx->format = AV_PIX_FMT_BGR0;
        }
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
    }

    stream = avformat_new_stream(avctx, NULL);
    if (!stream) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    stream->codecpar->codec_id   = AV_CODEC_ID_WRAPPED_AVFRAME;
    stream->codecpar->width      = ctx->width;
    stream->codecpar->height     = ctx->height;
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
    ctx->frames->width     = ctx->width;
    ctx->frames->height    = ctx->height;

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
    drmModeFreePlaneResources(plane_res);
    drmModeFreePlane(plane);
    drmModeFreeFB(fb);
#if HAVE_LIBDRM_GETFB2
    drmModeFreeFB2(fb2);
#endif
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
      { .i64 = AV_PIX_FMT_NONE }, -1, INT32_MAX, FLAGS },
    { "format_modifier", "DRM format modifier for framebuffer",
      OFFSET(drm_format_modifier), AV_OPT_TYPE_INT64,
      { .i64 = DRM_FORMAT_MOD_INVALID }, 0, INT64_MAX, FLAGS },
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
    .category   = AV_CLASS_CATEGORY_DEVICE_VIDEO_INPUT,
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
