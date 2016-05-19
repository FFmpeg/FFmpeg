/*
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "config.h"

#include <stdint.h>
#include <string.h>

#include <vdpau/vdpau.h>

#include "buffer.h"
#include "common.h"
#include "hwcontext.h"
#include "hwcontext_internal.h"
#include "hwcontext_vdpau.h"
#include "mem.h"
#include "pixfmt.h"
#include "pixdesc.h"

typedef struct VDPAUDeviceContext {
    VdpVideoSurfaceQueryGetPutBitsYCbCrCapabilities *get_transfer_caps;
    VdpVideoSurfaceGetBitsYCbCr                     *get_data;
    VdpVideoSurfacePutBitsYCbCr                     *put_data;
    VdpVideoSurfaceCreate                           *surf_create;
    VdpVideoSurfaceDestroy                          *surf_destroy;

    enum AVPixelFormat *pix_fmts[3];
    int              nb_pix_fmts[3];
} VDPAUDeviceContext;

typedef struct VDPAUFramesContext {
    VdpVideoSurfaceGetBitsYCbCr *get_data;
    VdpVideoSurfacePutBitsYCbCr *put_data;
    VdpChromaType chroma_type;
    int chroma_idx;

    const enum AVPixelFormat *pix_fmts;
    int                       nb_pix_fmts;
} VDPAUFramesContext;

typedef struct VDPAUPixFmtMap {
    VdpYCbCrFormat vdpau_fmt;
    enum AVPixelFormat pix_fmt;
} VDPAUPixFmtMap;

static const VDPAUPixFmtMap pix_fmts_420[] = {
    { VDP_YCBCR_FORMAT_NV12, AV_PIX_FMT_NV12    },
    { VDP_YCBCR_FORMAT_YV12, AV_PIX_FMT_YUV420P },
    { 0,                     AV_PIX_FMT_NONE,   },
};

static const VDPAUPixFmtMap pix_fmts_422[] = {
    { VDP_YCBCR_FORMAT_NV12, AV_PIX_FMT_NV16    },
    { VDP_YCBCR_FORMAT_YV12, AV_PIX_FMT_YUV422P },
    { VDP_YCBCR_FORMAT_UYVY, AV_PIX_FMT_UYVY422 },
    { VDP_YCBCR_FORMAT_YUYV, AV_PIX_FMT_YUYV422 },
    { 0,                     AV_PIX_FMT_NONE,   },
};

static const VDPAUPixFmtMap pix_fmts_444[] = {
    { VDP_YCBCR_FORMAT_YV12, AV_PIX_FMT_YUV444P },
    { 0,                     AV_PIX_FMT_NONE,   },
};

static const struct {
    VdpChromaType chroma_type;
    const VDPAUPixFmtMap *map;
} vdpau_pix_fmts[] = {
    { VDP_CHROMA_TYPE_420, pix_fmts_420 },
    { VDP_CHROMA_TYPE_422, pix_fmts_422 },
    { VDP_CHROMA_TYPE_444, pix_fmts_444 },
};

static int count_pixfmts(const VDPAUPixFmtMap *map)
{
    int count = 0;
    while (map->pix_fmt != AV_PIX_FMT_NONE) {
        map++;
        count++;
    }
    return count;
}

static int vdpau_init_pixmfts(AVHWDeviceContext *ctx)
{
    AVVDPAUDeviceContext *hwctx = ctx->hwctx;
    VDPAUDeviceContext    *priv = ctx->internal->priv;
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(priv->pix_fmts); i++) {
        const VDPAUPixFmtMap *map = vdpau_pix_fmts[i].map;
        int nb_pix_fmts;

        nb_pix_fmts = count_pixfmts(map);
        priv->pix_fmts[i] = av_malloc_array(nb_pix_fmts + 1, sizeof(*priv->pix_fmts[i]));
        if (!priv->pix_fmts[i])
            return AVERROR(ENOMEM);

        nb_pix_fmts = 0;
        while (map->pix_fmt != AV_PIX_FMT_NONE) {
            VdpBool supported;
            VdpStatus err = priv->get_transfer_caps(hwctx->device, vdpau_pix_fmts[i].chroma_type,
                                                    map->vdpau_fmt, &supported);
            if (err == VDP_STATUS_OK && supported)
                priv->pix_fmts[i][nb_pix_fmts++] = map->pix_fmt;
            map++;
        }
        priv->pix_fmts[i][nb_pix_fmts++] = AV_PIX_FMT_NONE;
        priv->nb_pix_fmts[i]             = nb_pix_fmts;
    }

    return 0;
}

static int vdpau_device_init(AVHWDeviceContext *ctx)
{
    AVVDPAUDeviceContext *hwctx = ctx->hwctx;
    VDPAUDeviceContext   *priv  = ctx->internal->priv;
    VdpStatus             err;
    int                   ret;

#define GET_CALLBACK(id, result)                                                \
do {                                                                            \
    void *tmp;                                                                  \
    err = hwctx->get_proc_address(hwctx->device, id, &tmp);                     \
    if (err != VDP_STATUS_OK) {                                                 \
        av_log(ctx, AV_LOG_ERROR, "Error getting the " #id " callback.\n");     \
        return AVERROR_UNKNOWN;                                                 \
    }                                                                           \
    priv->result = tmp;                                                         \
} while (0)

    GET_CALLBACK(VDP_FUNC_ID_VIDEO_SURFACE_QUERY_GET_PUT_BITS_Y_CB_CR_CAPABILITIES,
                 get_transfer_caps);
    GET_CALLBACK(VDP_FUNC_ID_VIDEO_SURFACE_GET_BITS_Y_CB_CR, get_data);
    GET_CALLBACK(VDP_FUNC_ID_VIDEO_SURFACE_PUT_BITS_Y_CB_CR, put_data);
    GET_CALLBACK(VDP_FUNC_ID_VIDEO_SURFACE_CREATE,           surf_create);
    GET_CALLBACK(VDP_FUNC_ID_VIDEO_SURFACE_DESTROY,          surf_destroy);

    ret = vdpau_init_pixmfts(ctx);
    if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Error querying the supported pixel formats\n");
        return ret;
    }

    return 0;
}

static void vdpau_device_uninit(AVHWDeviceContext *ctx)
{
    VDPAUDeviceContext *priv = ctx->internal->priv;
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(priv->pix_fmts); i++)
        av_freep(&priv->pix_fmts[i]);
}

static void vdpau_buffer_free(void *opaque, uint8_t *data)
{
    AVHWFramesContext          *ctx = opaque;
    VDPAUDeviceContext *device_priv = ctx->device_ctx->internal->priv;
    VdpVideoSurface            surf = (VdpVideoSurface)(uintptr_t)data;

    device_priv->surf_destroy(surf);
}

static AVBufferRef *vdpau_pool_alloc(void *opaque, int size)
{
    AVHWFramesContext             *ctx = opaque;
    VDPAUFramesContext           *priv = ctx->internal->priv;
    AVVDPAUDeviceContext *device_hwctx = ctx->device_ctx->hwctx;
    VDPAUDeviceContext    *device_priv = ctx->device_ctx->internal->priv;

    AVBufferRef *ret;
    VdpVideoSurface surf;
    VdpStatus err;

    err = device_priv->surf_create(device_hwctx->device, priv->chroma_type,
                                   ctx->width, ctx->height, &surf);
    if (err != VDP_STATUS_OK) {
        av_log(ctx, AV_LOG_ERROR, "Error allocating a VDPAU video surface\n");
        return NULL;
    }

    ret = av_buffer_create((uint8_t*)(uintptr_t)surf, sizeof(surf),
                           vdpau_buffer_free, ctx, AV_BUFFER_FLAG_READONLY);
    if (!ret) {
        device_priv->surf_destroy(surf);
        return NULL;
    }

    return ret;
}

static int vdpau_frames_init(AVHWFramesContext *ctx)
{
    VDPAUDeviceContext *device_priv = ctx->device_ctx->internal->priv;
    VDPAUFramesContext        *priv = ctx->internal->priv;

    int i;

    switch (ctx->sw_format) {
    case AV_PIX_FMT_YUV420P: priv->chroma_type = VDP_CHROMA_TYPE_420; break;
    case AV_PIX_FMT_YUV422P: priv->chroma_type = VDP_CHROMA_TYPE_422; break;
    case AV_PIX_FMT_YUV444P: priv->chroma_type = VDP_CHROMA_TYPE_444; break;
    default:
        av_log(ctx, AV_LOG_ERROR, "Unsupported data layout: %s\n",
               av_get_pix_fmt_name(ctx->sw_format));
        return AVERROR(ENOSYS);
    }

    for (i = 0; i < FF_ARRAY_ELEMS(vdpau_pix_fmts); i++) {
        if (vdpau_pix_fmts[i].chroma_type == priv->chroma_type) {
            priv->chroma_idx  = i;
            priv->pix_fmts    = device_priv->pix_fmts[i];
            priv->nb_pix_fmts = device_priv->nb_pix_fmts[i];
            break;
        }
    }
    if (!priv->pix_fmts) {
        av_log(ctx, AV_LOG_ERROR, "Unsupported chroma type: %d\n", priv->chroma_type);
        return AVERROR(ENOSYS);
    }

    if (!ctx->pool) {
        ctx->internal->pool_internal = av_buffer_pool_init2(sizeof(VdpVideoSurface), ctx,
                                                            vdpau_pool_alloc, NULL);
        if (!ctx->internal->pool_internal)
            return AVERROR(ENOMEM);
    }

    priv->get_data = device_priv->get_data;
    priv->put_data = device_priv->put_data;

    return 0;
}

static int vdpau_get_buffer(AVHWFramesContext *ctx, AVFrame *frame)
{
    frame->buf[0] = av_buffer_pool_get(ctx->pool);
    if (!frame->buf[0])
        return AVERROR(ENOMEM);

    frame->data[3] = frame->buf[0]->data;
    frame->format  = AV_PIX_FMT_VDPAU;
    frame->width   = ctx->width;
    frame->height  = ctx->height;

    return 0;
}

static int vdpau_transfer_get_formats(AVHWFramesContext *ctx,
                                      enum AVHWFrameTransferDirection dir,
                                      enum AVPixelFormat **formats)
{
    VDPAUFramesContext *priv  = ctx->internal->priv;

    enum AVPixelFormat *fmts;

    if (priv->nb_pix_fmts == 1) {
        av_log(ctx, AV_LOG_ERROR,
               "No target formats are supported for this chroma type\n");
        return AVERROR(ENOSYS);
    }

    fmts = av_malloc_array(priv->nb_pix_fmts, sizeof(*fmts));
    if (!fmts)
        return AVERROR(ENOMEM);

    memcpy(fmts, priv->pix_fmts, sizeof(*fmts) * (priv->nb_pix_fmts));
    *formats = fmts;

    return 0;
}

static int vdpau_transfer_data_from(AVHWFramesContext *ctx, AVFrame *dst,
                                    const AVFrame *src)
{
    VDPAUFramesContext *priv = ctx->internal->priv;
    VdpVideoSurface     surf = (VdpVideoSurface)(uintptr_t)src->data[3];

    void *data[3];
    uint32_t linesize[3];

    const VDPAUPixFmtMap *map;
    VdpYCbCrFormat vdpau_format;
    VdpStatus err;
    int i;

    for (i = 0; i< FF_ARRAY_ELEMS(data) && dst->data[i]; i++) {
        data[i] = dst->data[i];
        if (dst->linesize[i] < 0 || (uint64_t)dst->linesize > UINT32_MAX) {
            av_log(ctx, AV_LOG_ERROR,
                   "The linesize %d cannot be represented as uint32\n",
                   dst->linesize[i]);
            return AVERROR(ERANGE);
        }
        linesize[i] = dst->linesize[i];
    }

    map = vdpau_pix_fmts[priv->chroma_idx].map;
    for (i = 0; map[i].pix_fmt != AV_PIX_FMT_NONE; i++) {
        if (map[i].pix_fmt == dst->format) {
            vdpau_format = map[i].vdpau_fmt;
            break;
        }
    }
    if (map[i].pix_fmt == AV_PIX_FMT_NONE) {
        av_log(ctx, AV_LOG_ERROR,
               "Unsupported target pixel format: %s\n",
               av_get_pix_fmt_name(dst->format));
        return AVERROR(EINVAL);
    }

    if (vdpau_format == VDP_YCBCR_FORMAT_YV12)
        FFSWAP(void*, data[1], data[2]);

    err = priv->get_data(surf, vdpau_format, data, linesize);
    if (err != VDP_STATUS_OK) {
        av_log(ctx, AV_LOG_ERROR, "Error retrieving the data from a VDPAU surface\n");
        return AVERROR_UNKNOWN;
    }

    return 0;
}

static int vdpau_transfer_data_to(AVHWFramesContext *ctx, AVFrame *dst,
                                  const AVFrame *src)
{
    VDPAUFramesContext *priv = ctx->internal->priv;
    VdpVideoSurface     surf = (VdpVideoSurface)(uintptr_t)dst->data[3];

    const void *data[3];
    uint32_t linesize[3];

    const VDPAUPixFmtMap *map;
    VdpYCbCrFormat vdpau_format;
    VdpStatus err;
    int i;

    for (i = 0; i< FF_ARRAY_ELEMS(data) && src->data[i]; i++) {
        data[i] = src->data[i];
        if (src->linesize[i] < 0 || (uint64_t)src->linesize > UINT32_MAX) {
            av_log(ctx, AV_LOG_ERROR,
                   "The linesize %d cannot be represented as uint32\n",
                   src->linesize[i]);
            return AVERROR(ERANGE);
        }
        linesize[i] = src->linesize[i];
    }

    map = vdpau_pix_fmts[priv->chroma_idx].map;
    for (i = 0; map[i].pix_fmt != AV_PIX_FMT_NONE; i++) {
        if (map[i].pix_fmt == src->format) {
            vdpau_format = map[i].vdpau_fmt;
            break;
        }
    }
    if (map[i].pix_fmt == AV_PIX_FMT_NONE) {
        av_log(ctx, AV_LOG_ERROR,
               "Unsupported source pixel format: %s\n",
               av_get_pix_fmt_name(src->format));
        return AVERROR(EINVAL);
    }

    if (vdpau_format == VDP_YCBCR_FORMAT_YV12)
        FFSWAP(const void*, data[1], data[2]);

    err = priv->put_data(surf, vdpau_format, data, linesize);
    if (err != VDP_STATUS_OK) {
        av_log(ctx, AV_LOG_ERROR, "Error uploading the data to a VDPAU surface\n");
        return AVERROR_UNKNOWN;
    }

    return 0;
}

#if HAVE_VDPAU_X11
#include <vdpau/vdpau_x11.h>
#include <X11/Xlib.h>

typedef struct VDPAUDevicePriv {
    VdpDeviceDestroy *device_destroy;
    Display *dpy;
} VDPAUDevicePriv;

static void vdpau_device_free(AVHWDeviceContext *ctx)
{
    AVVDPAUDeviceContext *hwctx = ctx->hwctx;
    VDPAUDevicePriv       *priv = ctx->user_opaque;

    if (priv->device_destroy)
        priv->device_destroy(hwctx->device);
    if (priv->dpy)
        XCloseDisplay(priv->dpy);
    av_freep(&priv);
}

static int vdpau_device_create(AVHWDeviceContext *ctx, const char *device,
                               AVDictionary *opts, int flags)
{
    AVVDPAUDeviceContext *hwctx = ctx->hwctx;

    VDPAUDevicePriv *priv;
    VdpStatus err;
    VdpGetInformationString *get_information_string;
    const char *display, *vendor;

    priv = av_mallocz(sizeof(*priv));
    if (!priv)
        return AVERROR(ENOMEM);

    ctx->user_opaque = priv;
    ctx->free        = vdpau_device_free;

    priv->dpy = XOpenDisplay(device);
    if (!priv->dpy) {
        av_log(ctx, AV_LOG_ERROR, "Cannot open the X11 display %s.\n",
               XDisplayName(device));
        return AVERROR_UNKNOWN;
    }
    display = XDisplayString(priv->dpy);

    err = vdp_device_create_x11(priv->dpy, XDefaultScreen(priv->dpy),
                                &hwctx->device, &hwctx->get_proc_address);
    if (err != VDP_STATUS_OK) {
        av_log(ctx, AV_LOG_ERROR, "VDPAU device creation on X11 display %s failed.\n",
               display);
        return AVERROR_UNKNOWN;
    }

#define GET_CALLBACK(id, result)                                                \
do {                                                                            \
    void *tmp;                                                                  \
    err = hwctx->get_proc_address(hwctx->device, id, &tmp);                     \
    if (err != VDP_STATUS_OK) {                                                 \
        av_log(ctx, AV_LOG_ERROR, "Error getting the " #id " callback.\n");     \
        return AVERROR_UNKNOWN;                                                 \
    }                                                                           \
    result = tmp;                                                               \
} while (0)

    GET_CALLBACK(VDP_FUNC_ID_GET_INFORMATION_STRING, get_information_string);
    GET_CALLBACK(VDP_FUNC_ID_DEVICE_DESTROY,         priv->device_destroy);

    get_information_string(&vendor);
    av_log(ctx, AV_LOG_VERBOSE, "Successfully created a VDPAU device (%s) on "
           "X11 display %s\n", vendor, display);

    return 0;
}
#endif

const HWContextType ff_hwcontext_type_vdpau = {
    .type                 = AV_HWDEVICE_TYPE_VDPAU,
    .name                 = "VDPAU",

    .device_hwctx_size    = sizeof(AVVDPAUDeviceContext),
    .device_priv_size     = sizeof(VDPAUDeviceContext),
    .frames_priv_size     = sizeof(VDPAUFramesContext),

#if HAVE_VDPAU_X11
    .device_create        = vdpau_device_create,
#endif
    .device_init          = vdpau_device_init,
    .device_uninit        = vdpau_device_uninit,
    .frames_init          = vdpau_frames_init,
    .frames_get_buffer    = vdpau_get_buffer,
    .transfer_get_formats = vdpau_transfer_get_formats,
    .transfer_data_to     = vdpau_transfer_data_to,
    .transfer_data_from   = vdpau_transfer_data_from,

    .pix_fmts = (const enum AVPixelFormat[]){ AV_PIX_FMT_VDPAU, AV_PIX_FMT_NONE },
};
