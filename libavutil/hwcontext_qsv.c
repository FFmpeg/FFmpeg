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
#include <string.h>

#include <mfx/mfxvideo.h>

#include "config.h"

#if HAVE_PTHREADS
#include <pthread.h>
#endif

#if CONFIG_VAAPI
#include "hwcontext_vaapi.h"
#endif
#if CONFIG_DXVA2
#include "hwcontext_dxva2.h"
#endif

#include "buffer.h"
#include "common.h"
#include "hwcontext.h"
#include "hwcontext_internal.h"
#include "hwcontext_qsv.h"
#include "mem.h"
#include "pixfmt.h"
#include "pixdesc.h"
#include "time.h"

typedef struct QSVDevicePriv {
    AVBufferRef *child_device_ctx;
} QSVDevicePriv;

typedef struct QSVDeviceContext {
    mfxHDL              handle;
    mfxHandleType       handle_type;
    mfxVersion          ver;
    mfxIMPL             impl;

    enum AVHWDeviceType child_device_type;
    enum AVPixelFormat  child_pix_fmt;
} QSVDeviceContext;

typedef struct QSVFramesContext {
    mfxSession session_download;
    int session_download_init;
    mfxSession session_upload;
    int session_upload_init;
#if HAVE_PTHREADS
    pthread_mutex_t session_lock;
    pthread_cond_t session_cond;
#endif

    AVBufferRef *child_frames_ref;
    mfxFrameSurface1 *surfaces_internal;
    int             nb_surfaces_used;

    // used in the frame allocator for non-opaque surfaces
    mfxMemId *mem_ids;
    // used in the opaque alloc request for opaque surfaces
    mfxFrameSurface1 **surface_ptrs;

    mfxExtOpaqueSurfaceAlloc opaque_alloc;
    mfxExtBuffer *ext_buffers[1];
} QSVFramesContext;

static const struct {
    mfxHandleType handle_type;
    enum AVHWDeviceType device_type;
    enum AVPixelFormat  pix_fmt;
} supported_handle_types[] = {
#if CONFIG_VAAPI
    { MFX_HANDLE_VA_DISPLAY,          AV_HWDEVICE_TYPE_VAAPI, AV_PIX_FMT_VAAPI },
#endif
#if CONFIG_DXVA2
    { MFX_HANDLE_D3D9_DEVICE_MANAGER, AV_HWDEVICE_TYPE_DXVA2, AV_PIX_FMT_DXVA2_VLD },
#endif
    { 0 },
};

static const struct {
    enum AVPixelFormat pix_fmt;
    uint32_t           fourcc;
} supported_pixel_formats[] = {
    { AV_PIX_FMT_NV12, MFX_FOURCC_NV12 },
    { AV_PIX_FMT_BGRA, MFX_FOURCC_RGB4 },
    { AV_PIX_FMT_P010, MFX_FOURCC_P010 },
    { AV_PIX_FMT_PAL8, MFX_FOURCC_P8   },
};

static uint32_t qsv_fourcc_from_pix_fmt(enum AVPixelFormat pix_fmt)
{
    int i;
    for (i = 0; i < FF_ARRAY_ELEMS(supported_pixel_formats); i++) {
        if (supported_pixel_formats[i].pix_fmt == pix_fmt)
            return supported_pixel_formats[i].fourcc;
    }
    return 0;
}

static int qsv_device_init(AVHWDeviceContext *ctx)
{
    AVQSVDeviceContext *hwctx = ctx->hwctx;
    QSVDeviceContext       *s = ctx->internal->priv;

    mfxStatus err;
    int i;

    for (i = 0; supported_handle_types[i].handle_type; i++) {
        err = MFXVideoCORE_GetHandle(hwctx->session, supported_handle_types[i].handle_type,
                                     &s->handle);
        if (err == MFX_ERR_NONE) {
            s->handle_type       = supported_handle_types[i].handle_type;
            s->child_device_type = supported_handle_types[i].device_type;
            s->child_pix_fmt     = supported_handle_types[i].pix_fmt;
            break;
        }
    }
    if (!s->handle) {
        av_log(ctx, AV_LOG_VERBOSE, "No supported hw handle could be retrieved "
               "from the session\n");
    }

    err = MFXQueryIMPL(hwctx->session, &s->impl);
    if (err == MFX_ERR_NONE)
        err = MFXQueryVersion(hwctx->session, &s->ver);
    if (err != MFX_ERR_NONE) {
        av_log(ctx, AV_LOG_ERROR, "Error querying the session attributes\n");
        return AVERROR_UNKNOWN;
    }

    return 0;
}

static void qsv_frames_uninit(AVHWFramesContext *ctx)
{
    QSVFramesContext *s = ctx->internal->priv;

    if (s->session_download) {
        MFXVideoVPP_Close(s->session_download);
        MFXClose(s->session_download);
    }
    s->session_download = NULL;
    s->session_download_init = 0;

    if (s->session_upload) {
        MFXVideoVPP_Close(s->session_upload);
        MFXClose(s->session_upload);
    }
    s->session_upload = NULL;
    s->session_upload_init = 0;

#if HAVE_PTHREADS
    pthread_mutex_destroy(&s->session_lock);
    pthread_cond_destroy(&s->session_cond);
#endif

    av_freep(&s->mem_ids);
    av_freep(&s->surface_ptrs);
    av_freep(&s->surfaces_internal);
    av_buffer_unref(&s->child_frames_ref);
}

static void qsv_pool_release_dummy(void *opaque, uint8_t *data)
{
}

static AVBufferRef *qsv_pool_alloc(void *opaque, int size)
{
    AVHWFramesContext    *ctx = (AVHWFramesContext*)opaque;
    QSVFramesContext       *s = ctx->internal->priv;
    AVQSVFramesContext *hwctx = ctx->hwctx;

    if (s->nb_surfaces_used < hwctx->nb_surfaces) {
        s->nb_surfaces_used++;
        return av_buffer_create((uint8_t*)(s->surfaces_internal + s->nb_surfaces_used - 1),
                                sizeof(*hwctx->surfaces), qsv_pool_release_dummy, NULL, 0);
    }

    return NULL;
}

static int qsv_init_child_ctx(AVHWFramesContext *ctx)
{
    AVQSVFramesContext     *hwctx = ctx->hwctx;
    QSVFramesContext           *s = ctx->internal->priv;
    QSVDeviceContext *device_priv = ctx->device_ctx->internal->priv;

    AVBufferRef *child_device_ref = NULL;
    AVBufferRef *child_frames_ref = NULL;

    AVHWDeviceContext *child_device_ctx;
    AVHWFramesContext *child_frames_ctx;

    int i, ret = 0;

    if (!device_priv->handle) {
        av_log(ctx, AV_LOG_ERROR,
               "Cannot create a non-opaque internal surface pool without "
               "a hardware handle\n");
        return AVERROR(EINVAL);
    }

    child_device_ref = av_hwdevice_ctx_alloc(device_priv->child_device_type);
    if (!child_device_ref)
        return AVERROR(ENOMEM);
    child_device_ctx   = (AVHWDeviceContext*)child_device_ref->data;

#if CONFIG_VAAPI
    if (child_device_ctx->type == AV_HWDEVICE_TYPE_VAAPI) {
        AVVAAPIDeviceContext *child_device_hwctx = child_device_ctx->hwctx;
        child_device_hwctx->display = (VADisplay)device_priv->handle;
    }
#endif
#if CONFIG_DXVA2
    if (child_device_ctx->type == AV_HWDEVICE_TYPE_DXVA2) {
        AVDXVA2DeviceContext *child_device_hwctx = child_device_ctx->hwctx;
        child_device_hwctx->devmgr = (IDirect3DDeviceManager9*)device_priv->handle;
    }
#endif

    ret = av_hwdevice_ctx_init(child_device_ref);
    if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Error initializing a child device context\n");
        goto fail;
    }

    child_frames_ref = av_hwframe_ctx_alloc(child_device_ref);
    if (!child_frames_ref) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    child_frames_ctx = (AVHWFramesContext*)child_frames_ref->data;

    child_frames_ctx->format            = device_priv->child_pix_fmt;
    child_frames_ctx->sw_format         = ctx->sw_format;
    child_frames_ctx->initial_pool_size = ctx->initial_pool_size;
    child_frames_ctx->width             = FFALIGN(ctx->width, 16);
    child_frames_ctx->height            = FFALIGN(ctx->height, 16);

#if CONFIG_DXVA2
    if (child_device_ctx->type == AV_HWDEVICE_TYPE_DXVA2) {
        AVDXVA2FramesContext *child_frames_hwctx = child_frames_ctx->hwctx;
        if (hwctx->frame_type & MFX_MEMTYPE_VIDEO_MEMORY_PROCESSOR_TARGET)
            child_frames_hwctx->surface_type = DXVA2_VideoProcessorRenderTarget;
        else
            child_frames_hwctx->surface_type = DXVA2_VideoDecoderRenderTarget;
    }
#endif

    ret = av_hwframe_ctx_init(child_frames_ref);
    if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Error initializing a child frames context\n");
        goto fail;
    }

#if CONFIG_VAAPI
    if (child_device_ctx->type == AV_HWDEVICE_TYPE_VAAPI) {
        AVVAAPIFramesContext *child_frames_hwctx = child_frames_ctx->hwctx;
        for (i = 0; i < ctx->initial_pool_size; i++)
            s->surfaces_internal[i].Data.MemId = child_frames_hwctx->surface_ids + i;
        hwctx->frame_type = MFX_MEMTYPE_VIDEO_MEMORY_DECODER_TARGET;
    }
#endif
#if CONFIG_DXVA2
    if (child_device_ctx->type == AV_HWDEVICE_TYPE_DXVA2) {
        AVDXVA2FramesContext *child_frames_hwctx = child_frames_ctx->hwctx;
        for (i = 0; i < ctx->initial_pool_size; i++)
            s->surfaces_internal[i].Data.MemId = (mfxMemId)child_frames_hwctx->surfaces[i];
        if (child_frames_hwctx->surface_type == DXVA2_VideoProcessorRenderTarget)
            hwctx->frame_type = MFX_MEMTYPE_VIDEO_MEMORY_PROCESSOR_TARGET;
        else
            hwctx->frame_type = MFX_MEMTYPE_VIDEO_MEMORY_DECODER_TARGET;
    }
#endif

    s->child_frames_ref       = child_frames_ref;
    child_frames_ref          = NULL;

fail:
    av_buffer_unref(&child_device_ref);
    av_buffer_unref(&child_frames_ref);
    return ret;
}

static int qsv_init_surface(AVHWFramesContext *ctx, mfxFrameSurface1 *surf)
{
    const AVPixFmtDescriptor *desc;
    uint32_t fourcc;

    desc = av_pix_fmt_desc_get(ctx->sw_format);
    if (!desc)
        return AVERROR(EINVAL);

    fourcc = qsv_fourcc_from_pix_fmt(ctx->sw_format);
    if (!fourcc)
        return AVERROR(EINVAL);

    surf->Info.BitDepthLuma   = desc->comp[0].depth;
    surf->Info.BitDepthChroma = desc->comp[0].depth;
    surf->Info.Shift          = desc->comp[0].depth > 8;

    if (desc->log2_chroma_w && desc->log2_chroma_h)
        surf->Info.ChromaFormat   = MFX_CHROMAFORMAT_YUV420;
    else if (desc->log2_chroma_w)
        surf->Info.ChromaFormat   = MFX_CHROMAFORMAT_YUV422;
    else
        surf->Info.ChromaFormat   = MFX_CHROMAFORMAT_YUV444;

    surf->Info.FourCC         = fourcc;
    surf->Info.Width          = FFALIGN(ctx->width, 16);
    surf->Info.CropW          = ctx->width;
    surf->Info.Height         = FFALIGN(ctx->height, 16);
    surf->Info.CropH          = ctx->height;
    surf->Info.FrameRateExtN  = 25;
    surf->Info.FrameRateExtD  = 1;
    surf->Info.PicStruct      = MFX_PICSTRUCT_PROGRESSIVE;

    return 0;
}

static int qsv_init_pool(AVHWFramesContext *ctx, uint32_t fourcc)
{
    QSVFramesContext              *s = ctx->internal->priv;
    AVQSVFramesContext *frames_hwctx = ctx->hwctx;

    int i, ret = 0;

    if (ctx->initial_pool_size <= 0) {
        av_log(ctx, AV_LOG_ERROR, "QSV requires a fixed frame pool size\n");
        return AVERROR(EINVAL);
    }

    s->surfaces_internal = av_mallocz_array(ctx->initial_pool_size,
                                            sizeof(*s->surfaces_internal));
    if (!s->surfaces_internal)
        return AVERROR(ENOMEM);

    for (i = 0; i < ctx->initial_pool_size; i++) {
        ret = qsv_init_surface(ctx, &s->surfaces_internal[i]);
        if (ret < 0)
            return ret;
    }

    if (!(frames_hwctx->frame_type & MFX_MEMTYPE_OPAQUE_FRAME)) {
        ret = qsv_init_child_ctx(ctx);
        if (ret < 0)
            return ret;
    }

    ctx->internal->pool_internal = av_buffer_pool_init2(sizeof(mfxFrameSurface1),
                                                        ctx, qsv_pool_alloc, NULL);
    if (!ctx->internal->pool_internal)
        return AVERROR(ENOMEM);

    frames_hwctx->surfaces    = s->surfaces_internal;
    frames_hwctx->nb_surfaces = ctx->initial_pool_size;

    return 0;
}

static mfxStatus frame_alloc(mfxHDL pthis, mfxFrameAllocRequest *req,
                             mfxFrameAllocResponse *resp)
{
    AVHWFramesContext    *ctx = pthis;
    QSVFramesContext       *s = ctx->internal->priv;
    AVQSVFramesContext *hwctx = ctx->hwctx;
    mfxFrameInfo *i  = &req->Info;
    mfxFrameInfo *i1 = &hwctx->surfaces[0].Info;

    if (!(req->Type & MFX_MEMTYPE_VIDEO_MEMORY_PROCESSOR_TARGET) ||
        !(req->Type & (MFX_MEMTYPE_FROM_VPPIN | MFX_MEMTYPE_FROM_VPPOUT)) ||
        !(req->Type & MFX_MEMTYPE_EXTERNAL_FRAME))
        return MFX_ERR_UNSUPPORTED;
    if (i->Width  != i1->Width || i->Height != i1->Height ||
        i->FourCC != i1->FourCC || i->ChromaFormat != i1->ChromaFormat) {
        av_log(ctx, AV_LOG_ERROR, "Mismatching surface properties in an "
               "allocation request: %dx%d %d %d vs %dx%d %d %d\n",
               i->Width,  i->Height,  i->FourCC,  i->ChromaFormat,
               i1->Width, i1->Height, i1->FourCC, i1->ChromaFormat);
        return MFX_ERR_UNSUPPORTED;
    }

    resp->mids           = s->mem_ids;
    resp->NumFrameActual = hwctx->nb_surfaces;

    return MFX_ERR_NONE;
}

static mfxStatus frame_free(mfxHDL pthis, mfxFrameAllocResponse *resp)
{
    return MFX_ERR_NONE;
}

static mfxStatus frame_lock(mfxHDL pthis, mfxMemId mid, mfxFrameData *ptr)
{
    return MFX_ERR_UNSUPPORTED;
}

static mfxStatus frame_unlock(mfxHDL pthis, mfxMemId mid, mfxFrameData *ptr)
{
    return MFX_ERR_UNSUPPORTED;
}

static mfxStatus frame_get_hdl(mfxHDL pthis, mfxMemId mid, mfxHDL *hdl)
{
    *hdl = mid;
    return MFX_ERR_NONE;
}

static int qsv_init_internal_session(AVHWFramesContext *ctx,
                                     mfxSession *session, int upload)
{
    QSVFramesContext              *s = ctx->internal->priv;
    AVQSVFramesContext *frames_hwctx = ctx->hwctx;
    QSVDeviceContext   *device_priv  = ctx->device_ctx->internal->priv;
    int opaque = !!(frames_hwctx->frame_type & MFX_MEMTYPE_OPAQUE_FRAME);

    mfxFrameAllocator frame_allocator = {
        .pthis  = ctx,
        .Alloc  = frame_alloc,
        .Lock   = frame_lock,
        .Unlock = frame_unlock,
        .GetHDL = frame_get_hdl,
        .Free   = frame_free,
    };

    mfxVideoParam par;
    mfxStatus err;

    err = MFXInit(device_priv->impl, &device_priv->ver, session);
    if (err != MFX_ERR_NONE) {
        av_log(ctx, AV_LOG_ERROR, "Error initializing an internal session\n");
        return AVERROR_UNKNOWN;
    }

    if (device_priv->handle) {
        err = MFXVideoCORE_SetHandle(*session, device_priv->handle_type,
                                     device_priv->handle);
        if (err != MFX_ERR_NONE)
            return AVERROR_UNKNOWN;
    }

    if (!opaque) {
        err = MFXVideoCORE_SetFrameAllocator(*session, &frame_allocator);
        if (err != MFX_ERR_NONE)
            return AVERROR_UNKNOWN;
    }

    memset(&par, 0, sizeof(par));

    if (opaque) {
        par.ExtParam    = s->ext_buffers;
        par.NumExtParam = FF_ARRAY_ELEMS(s->ext_buffers);
        par.IOPattern   = upload ? MFX_IOPATTERN_OUT_OPAQUE_MEMORY :
                                   MFX_IOPATTERN_IN_OPAQUE_MEMORY;
    } else {
        par.IOPattern = upload ? MFX_IOPATTERN_OUT_VIDEO_MEMORY :
                                 MFX_IOPATTERN_IN_VIDEO_MEMORY;
    }

    par.IOPattern |= upload ? MFX_IOPATTERN_IN_SYSTEM_MEMORY :
                              MFX_IOPATTERN_OUT_SYSTEM_MEMORY;
    par.AsyncDepth = 1;

    par.vpp.In = frames_hwctx->surfaces[0].Info;

    /* Apparently VPP requires the frame rate to be set to some value, otherwise
     * init will fail (probably for the framerate conversion filter). Since we
     * are only doing data upload/download here, we just invent an arbitrary
     * value */
    par.vpp.In.FrameRateExtN = 25;
    par.vpp.In.FrameRateExtD = 1;
    par.vpp.Out = par.vpp.In;

    err = MFXVideoVPP_Init(*session, &par);
    if (err != MFX_ERR_NONE) {
        av_log(ctx, AV_LOG_VERBOSE, "Error opening the internal VPP session."
               "Surface upload/download will not be possible\n");
        MFXClose(*session);
        *session = NULL;
    }

    return 0;
}

static int qsv_frames_init(AVHWFramesContext *ctx)
{
    QSVFramesContext              *s = ctx->internal->priv;
    AVQSVFramesContext *frames_hwctx = ctx->hwctx;

    int opaque = !!(frames_hwctx->frame_type & MFX_MEMTYPE_OPAQUE_FRAME);

    uint32_t fourcc;
    int i, ret;

    fourcc = qsv_fourcc_from_pix_fmt(ctx->sw_format);
    if (!fourcc) {
        av_log(ctx, AV_LOG_ERROR, "Unsupported pixel format\n");
        return AVERROR(ENOSYS);
    }

    if (!ctx->pool) {
        ret = qsv_init_pool(ctx, fourcc);
        if (ret < 0) {
            av_log(ctx, AV_LOG_ERROR, "Error creating an internal frame pool\n");
            return ret;
        }
    }

    if (opaque) {
        s->surface_ptrs = av_mallocz_array(frames_hwctx->nb_surfaces,
                                           sizeof(*s->surface_ptrs));
        if (!s->surface_ptrs)
            return AVERROR(ENOMEM);

        for (i = 0; i < frames_hwctx->nb_surfaces; i++)
            s->surface_ptrs[i] = frames_hwctx->surfaces + i;

        s->opaque_alloc.In.Surfaces   = s->surface_ptrs;
        s->opaque_alloc.In.NumSurface = frames_hwctx->nb_surfaces;
        s->opaque_alloc.In.Type       = frames_hwctx->frame_type;

        s->opaque_alloc.Out = s->opaque_alloc.In;

        s->opaque_alloc.Header.BufferId = MFX_EXTBUFF_OPAQUE_SURFACE_ALLOCATION;
        s->opaque_alloc.Header.BufferSz = sizeof(s->opaque_alloc);

        s->ext_buffers[0] = (mfxExtBuffer*)&s->opaque_alloc;
    } else {
        s->mem_ids = av_mallocz_array(frames_hwctx->nb_surfaces, sizeof(*s->mem_ids));
        if (!s->mem_ids)
            return AVERROR(ENOMEM);

        for (i = 0; i < frames_hwctx->nb_surfaces; i++)
            s->mem_ids[i] = frames_hwctx->surfaces[i].Data.MemId;
    }

    s->session_download = NULL;
    s->session_upload   = NULL;

    s->session_download_init = 0;
    s->session_upload_init   = 0;

#if HAVE_PTHREADS
    pthread_mutex_init(&s->session_lock, NULL);
    pthread_cond_init(&s->session_cond, NULL);
#endif

    return 0;
}

static int qsv_get_buffer(AVHWFramesContext *ctx, AVFrame *frame)
{
    frame->buf[0] = av_buffer_pool_get(ctx->pool);
    if (!frame->buf[0])
        return AVERROR(ENOMEM);

    frame->data[3] = frame->buf[0]->data;
    frame->format  = AV_PIX_FMT_QSV;
    frame->width   = ctx->width;
    frame->height  = ctx->height;

    return 0;
}

static int qsv_transfer_get_formats(AVHWFramesContext *ctx,
                                    enum AVHWFrameTransferDirection dir,
                                    enum AVPixelFormat **formats)
{
    enum AVPixelFormat *fmts;

    fmts = av_malloc_array(2, sizeof(*fmts));
    if (!fmts)
        return AVERROR(ENOMEM);

    fmts[0] = ctx->sw_format;
    fmts[1] = AV_PIX_FMT_NONE;

    *formats = fmts;

    return 0;
}

static int qsv_frames_derive_from(AVHWFramesContext *dst_ctx,
                                  AVHWFramesContext *src_ctx, int flags)
{
    AVQSVFramesContext *src_hwctx = src_ctx->hwctx;
    int i;

    switch (dst_ctx->device_ctx->type) {
#if CONFIG_VAAPI
    case AV_HWDEVICE_TYPE_VAAPI:
        {
            AVVAAPIFramesContext *dst_hwctx = dst_ctx->hwctx;
            dst_hwctx->surface_ids = av_mallocz_array(src_hwctx->nb_surfaces,
                                                      sizeof(*dst_hwctx->surface_ids));
            if (!dst_hwctx->surface_ids)
                return AVERROR(ENOMEM);
            for (i = 0; i < src_hwctx->nb_surfaces; i++)
                dst_hwctx->surface_ids[i] =
                    *(VASurfaceID*)src_hwctx->surfaces[i].Data.MemId;
            dst_hwctx->nb_surfaces = src_hwctx->nb_surfaces;
        }
        break;
#endif
#if CONFIG_DXVA2
    case AV_HWDEVICE_TYPE_DXVA2:
        {
            AVDXVA2FramesContext *dst_hwctx = dst_ctx->hwctx;
            dst_hwctx->surfaces = av_mallocz_array(src_hwctx->nb_surfaces,
                                                   sizeof(*dst_hwctx->surfaces));
            if (!dst_hwctx->surfaces)
                return AVERROR(ENOMEM);
            for (i = 0; i < src_hwctx->nb_surfaces; i++)
                dst_hwctx->surfaces[i] =
                    (IDirect3DSurface9*)src_hwctx->surfaces[i].Data.MemId;
            dst_hwctx->nb_surfaces = src_hwctx->nb_surfaces;
            if (src_hwctx->frame_type == MFX_MEMTYPE_VIDEO_MEMORY_DECODER_TARGET)
                dst_hwctx->surface_type = DXVA2_VideoDecoderRenderTarget;
            else
                dst_hwctx->surface_type = DXVA2_VideoProcessorRenderTarget;
        }
        break;
#endif
    default:
        return AVERROR(ENOSYS);
    }

    return 0;
}

static int qsv_map_from(AVHWFramesContext *ctx,
                        AVFrame *dst, const AVFrame *src, int flags)
{
    QSVFramesContext *s = ctx->internal->priv;
    mfxFrameSurface1 *surf = (mfxFrameSurface1*)src->data[3];
    AVHWFramesContext *child_frames_ctx;
    const AVPixFmtDescriptor *desc;
    uint8_t *child_data;
    AVFrame *dummy;
    int ret = 0;

    if (!s->child_frames_ref)
        return AVERROR(ENOSYS);
    child_frames_ctx = (AVHWFramesContext*)s->child_frames_ref->data;

    switch (child_frames_ctx->device_ctx->type) {
#if CONFIG_VAAPI
    case AV_HWDEVICE_TYPE_VAAPI:
        child_data = (uint8_t*)(intptr_t)*(VASurfaceID*)surf->Data.MemId;
        break;
#endif
#if CONFIG_DXVA2
    case AV_HWDEVICE_TYPE_DXVA2:
        child_data = surf->Data.MemId;
        break;
#endif
    default:
        return AVERROR(ENOSYS);
    }

    if (dst->format == child_frames_ctx->format) {
        ret = ff_hwframe_map_create(s->child_frames_ref,
                                    dst, src, NULL, NULL);
        if (ret < 0)
            return ret;

        dst->width   = src->width;
        dst->height  = src->height;
        dst->data[3] = child_data;

        return 0;
    }

    desc = av_pix_fmt_desc_get(dst->format);
    if (desc && desc->flags & AV_PIX_FMT_FLAG_HWACCEL) {
        // This only supports mapping to software.
        return AVERROR(ENOSYS);
    }

    dummy = av_frame_alloc();
    if (!dummy)
        return AVERROR(ENOMEM);

    dummy->buf[0]        = av_buffer_ref(src->buf[0]);
    dummy->hw_frames_ctx = av_buffer_ref(s->child_frames_ref);
    if (!dummy->buf[0] || !dummy->hw_frames_ctx)
        goto fail;

    dummy->format        = child_frames_ctx->format;
    dummy->width         = src->width;
    dummy->height        = src->height;
    dummy->data[3]       = child_data;

    ret = av_hwframe_map(dst, dummy, flags);

fail:
    av_frame_free(&dummy);

    return ret;
}

static int qsv_transfer_data_child(AVHWFramesContext *ctx, AVFrame *dst,
                                   const AVFrame *src)
{
    QSVFramesContext *s = ctx->internal->priv;
    AVHWFramesContext *child_frames_ctx = (AVHWFramesContext*)s->child_frames_ref->data;
    int download = !!src->hw_frames_ctx;
    mfxFrameSurface1 *surf = (mfxFrameSurface1*)(download ? src->data[3] : dst->data[3]);

    AVFrame *dummy;
    int ret;

    dummy = av_frame_alloc();
    if (!dummy)
        return AVERROR(ENOMEM);

    dummy->format        = child_frames_ctx->format;
    dummy->width         = src->width;
    dummy->height        = src->height;
    dummy->buf[0]        = download ? src->buf[0] : dst->buf[0];
    dummy->data[3]       = surf->Data.MemId;
    dummy->hw_frames_ctx = s->child_frames_ref;

    ret = download ? av_hwframe_transfer_data(dst, dummy, 0) :
                     av_hwframe_transfer_data(dummy, src, 0);

    dummy->buf[0]        = NULL;
    dummy->data[3]       = NULL;
    dummy->hw_frames_ctx = NULL;

    av_frame_free(&dummy);

    return ret;
}

static int map_frame_to_surface(const AVFrame *frame, mfxFrameSurface1 *surface)
{
    switch (frame->format) {
    case AV_PIX_FMT_NV12:
    case AV_PIX_FMT_P010:
        surface->Data.Y  = frame->data[0];
        surface->Data.UV = frame->data[1];
        break;

    case AV_PIX_FMT_YUV420P:
        surface->Data.Y = frame->data[0];
        surface->Data.U = frame->data[1];
        surface->Data.V = frame->data[2];
        break;

    case AV_PIX_FMT_BGRA:
        surface->Data.B = frame->data[0];
        surface->Data.G = frame->data[0] + 1;
        surface->Data.R = frame->data[0] + 2;
        surface->Data.A = frame->data[0] + 3;
        break;

    default:
        return MFX_ERR_UNSUPPORTED;
    }
    surface->Data.Pitch     = frame->linesize[0];
    surface->Data.TimeStamp = frame->pts;

    return 0;
}

static int qsv_transfer_data_from(AVHWFramesContext *ctx, AVFrame *dst,
                                  const AVFrame *src)
{
    QSVFramesContext  *s = ctx->internal->priv;
    mfxFrameSurface1 out = {{ 0 }};
    mfxFrameSurface1 *in = (mfxFrameSurface1*)src->data[3];

    mfxSyncPoint sync = NULL;
    mfxStatus err;
    int ret = 0;

    while (!s->session_download_init && !s->session_download && !ret) {
#if HAVE_PTHREADS
        if (pthread_mutex_trylock(&s->session_lock) == 0) {
#endif
            if (!s->session_download_init) {
                ret = qsv_init_internal_session(ctx, &s->session_download, 0);
                if (s->session_download)
                    s->session_download_init = 1;
            }
#if HAVE_PTHREADS
            pthread_mutex_unlock(&s->session_lock);
            pthread_cond_signal(&s->session_cond);
        } else {
            pthread_mutex_lock(&s->session_lock);
            while (!s->session_download_init && !s->session_download) {
                pthread_cond_wait(&s->session_cond, &s->session_lock);
            }
            pthread_mutex_unlock(&s->session_lock);
        }
#endif
    }

    if (ret < 0)
        return ret;

    if (!s->session_download) {
        if (s->child_frames_ref)
            return qsv_transfer_data_child(ctx, dst, src);

        av_log(ctx, AV_LOG_ERROR, "Surface download not possible\n");
        return AVERROR(ENOSYS);
    }

    out.Info = in->Info;
    map_frame_to_surface(dst, &out);

    do {
        err = MFXVideoVPP_RunFrameVPPAsync(s->session_download, in, &out, NULL, &sync);
        if (err == MFX_WRN_DEVICE_BUSY)
            av_usleep(1);
    } while (err == MFX_WRN_DEVICE_BUSY);

    if (err < 0 || !sync) {
        av_log(ctx, AV_LOG_ERROR, "Error downloading the surface\n");
        return AVERROR_UNKNOWN;
    }

    do {
        err = MFXVideoCORE_SyncOperation(s->session_download, sync, 1000);
    } while (err == MFX_WRN_IN_EXECUTION);
    if (err < 0) {
        av_log(ctx, AV_LOG_ERROR, "Error synchronizing the operation: %d\n", err);
        return AVERROR_UNKNOWN;
    }

    return 0;
}

static int qsv_transfer_data_to(AVHWFramesContext *ctx, AVFrame *dst,
                                const AVFrame *src)
{
    QSVFramesContext   *s = ctx->internal->priv;
    mfxFrameSurface1   in = {{ 0 }};
    mfxFrameSurface1 *out = (mfxFrameSurface1*)dst->data[3];

    mfxSyncPoint sync = NULL;
    mfxStatus err;
    int ret = 0;
    /* make a copy if the input is not padded as libmfx requires */
    AVFrame tmp_frame, *src_frame;
    int realigned = 0;


    while (!s->session_upload_init && !s->session_upload && !ret) {
#if HAVE_PTHREADS
        if (pthread_mutex_trylock(&s->session_lock) == 0) {
#endif
            if (!s->session_upload_init) {
                ret = qsv_init_internal_session(ctx, &s->session_upload, 1);
                if (s->session_upload)
                    s->session_upload_init = 1;
            }
#if HAVE_PTHREADS
            pthread_mutex_unlock(&s->session_lock);
            pthread_cond_signal(&s->session_cond);
        } else {
            pthread_mutex_lock(&s->session_lock);
            while (!s->session_upload_init && !s->session_upload) {
                pthread_cond_wait(&s->session_cond, &s->session_lock);
            }
            pthread_mutex_unlock(&s->session_lock);
        }
#endif
    }
    if (ret < 0)
        return ret;


    if (src->height & 16 || src->linesize[0] & 16) {
        realigned = 1;
        memset(&tmp_frame, 0, sizeof(tmp_frame));
        tmp_frame.format         = src->format;
        tmp_frame.width          = FFALIGN(src->width, 16);
        tmp_frame.height         = FFALIGN(src->height, 16);
        ret = av_frame_get_buffer(&tmp_frame, 32);
        if (ret < 0)
            return ret;

        ret = av_frame_copy(&tmp_frame, src);
        if (ret < 0) {
            av_frame_unref(&tmp_frame);
            return ret;
        }
    }

    src_frame = realigned ? &tmp_frame : src;

    if (!s->session_upload) {
        if (s->child_frames_ref)
            return qsv_transfer_data_child(ctx, dst, src_frame);

        av_log(ctx, AV_LOG_ERROR, "Surface upload not possible\n");
        return AVERROR(ENOSYS);
    }

    in.Info = out->Info;
    map_frame_to_surface(src_frame, &in);

    do {
        err = MFXVideoVPP_RunFrameVPPAsync(s->session_upload, &in, out, NULL, &sync);
        if (err == MFX_WRN_DEVICE_BUSY)
            av_usleep(1);
    } while (err == MFX_WRN_DEVICE_BUSY);

    if (err < 0 || !sync) {
        av_log(ctx, AV_LOG_ERROR, "Error uploading the surface\n");
        return AVERROR_UNKNOWN;
    }

    do {
        err = MFXVideoCORE_SyncOperation(s->session_upload, sync, 1000);
    } while (err == MFX_WRN_IN_EXECUTION);
    if (err < 0) {
        av_log(ctx, AV_LOG_ERROR, "Error synchronizing the operation\n");
        return AVERROR_UNKNOWN;
    }

    if (realigned)
        av_frame_unref(&tmp_frame);

    return 0;
}

static int qsv_frames_derive_to(AVHWFramesContext *dst_ctx,
                                AVHWFramesContext *src_ctx, int flags)
{
    QSVFramesContext *s = dst_ctx->internal->priv;
    AVQSVFramesContext *dst_hwctx = dst_ctx->hwctx;
    int i;

    switch (src_ctx->device_ctx->type) {
#if CONFIG_VAAPI
    case AV_HWDEVICE_TYPE_VAAPI:
        {
            AVVAAPIFramesContext *src_hwctx = src_ctx->hwctx;
            s->surfaces_internal = av_mallocz_array(src_hwctx->nb_surfaces,
                                                    sizeof(*s->surfaces_internal));
            if (!s->surfaces_internal)
                return AVERROR(ENOMEM);
            for (i = 0; i < src_hwctx->nb_surfaces; i++) {
                qsv_init_surface(dst_ctx, &s->surfaces_internal[i]);
                s->surfaces_internal[i].Data.MemId = src_hwctx->surface_ids + i;
            }
            dst_hwctx->nb_surfaces = src_hwctx->nb_surfaces;
            dst_hwctx->frame_type  = MFX_MEMTYPE_VIDEO_MEMORY_DECODER_TARGET;
        }
        break;
#endif
#if CONFIG_DXVA2
    case AV_HWDEVICE_TYPE_DXVA2:
        {
            AVDXVA2FramesContext *src_hwctx = src_ctx->hwctx;
            s->surfaces_internal = av_mallocz_array(src_hwctx->nb_surfaces,
                                                    sizeof(*s->surfaces_internal));
            if (!s->surfaces_internal)
                return AVERROR(ENOMEM);
            for (i = 0; i < src_hwctx->nb_surfaces; i++) {
                qsv_init_surface(dst_ctx, &s->surfaces_internal[i]);
                s->surfaces_internal[i].Data.MemId = (mfxMemId)src_hwctx->surfaces[i];
            }
            dst_hwctx->nb_surfaces = src_hwctx->nb_surfaces;
            if (src_hwctx->surface_type == DXVA2_VideoProcessorRenderTarget)
                dst_hwctx->frame_type = MFX_MEMTYPE_VIDEO_MEMORY_PROCESSOR_TARGET;
            else
                dst_hwctx->frame_type = MFX_MEMTYPE_VIDEO_MEMORY_DECODER_TARGET;
        }
        break;
#endif
    default:
        return AVERROR(ENOSYS);
    }

    dst_hwctx->surfaces = s->surfaces_internal;

    return 0;
}

static int qsv_map_to(AVHWFramesContext *dst_ctx,
                      AVFrame *dst, const AVFrame *src, int flags)
{
    AVQSVFramesContext *hwctx = dst_ctx->hwctx;
    int i, err;

    for (i = 0; i < hwctx->nb_surfaces; i++) {
#if CONFIG_VAAPI
        if (*(VASurfaceID*)hwctx->surfaces[i].Data.MemId ==
            (VASurfaceID)(uintptr_t)src->data[3])
            break;
#endif
#if CONFIG_DXVA2
        if ((IDirect3DSurface9*)hwctx->surfaces[i].Data.MemId ==
            (IDirect3DSurface9*)(uintptr_t)src->data[3])
            break;
#endif
    }
    if (i >= hwctx->nb_surfaces) {
        av_log(dst_ctx, AV_LOG_ERROR, "Trying to map from a surface which "
               "is not in the mapped frames context.\n");
        return AVERROR(EINVAL);
    }

    err = ff_hwframe_map_create(dst->hw_frames_ctx,
                                dst, src, NULL, NULL);
    if (err)
        return err;

    dst->width   = src->width;
    dst->height  = src->height;
    dst->data[3] = (uint8_t*)&hwctx->surfaces[i];

    return 0;
}

static int qsv_frames_get_constraints(AVHWDeviceContext *ctx,
                                      const void *hwconfig,
                                      AVHWFramesConstraints *constraints)
{
    int i;

    constraints->valid_sw_formats = av_malloc_array(FF_ARRAY_ELEMS(supported_pixel_formats) + 1,
                                                    sizeof(*constraints->valid_sw_formats));
    if (!constraints->valid_sw_formats)
        return AVERROR(ENOMEM);

    for (i = 0; i < FF_ARRAY_ELEMS(supported_pixel_formats); i++)
        constraints->valid_sw_formats[i] = supported_pixel_formats[i].pix_fmt;
    constraints->valid_sw_formats[FF_ARRAY_ELEMS(supported_pixel_formats)] = AV_PIX_FMT_NONE;

    constraints->valid_hw_formats = av_malloc_array(2, sizeof(*constraints->valid_hw_formats));
    if (!constraints->valid_hw_formats)
        return AVERROR(ENOMEM);

    constraints->valid_hw_formats[0] = AV_PIX_FMT_QSV;
    constraints->valid_hw_formats[1] = AV_PIX_FMT_NONE;

    return 0;
}

static void qsv_device_free(AVHWDeviceContext *ctx)
{
    AVQSVDeviceContext *hwctx = ctx->hwctx;
    QSVDevicePriv       *priv = ctx->user_opaque;

    if (hwctx->session)
        MFXClose(hwctx->session);

    av_buffer_unref(&priv->child_device_ctx);
    av_freep(&priv);
}

static mfxIMPL choose_implementation(const char *device)
{
    static const struct {
        const char *name;
        mfxIMPL     impl;
    } impl_map[] = {
        { "auto",     MFX_IMPL_AUTO         },
        { "sw",       MFX_IMPL_SOFTWARE     },
        { "hw",       MFX_IMPL_HARDWARE     },
        { "auto_any", MFX_IMPL_AUTO_ANY     },
        { "hw_any",   MFX_IMPL_HARDWARE_ANY },
        { "hw2",      MFX_IMPL_HARDWARE2    },
        { "hw3",      MFX_IMPL_HARDWARE3    },
        { "hw4",      MFX_IMPL_HARDWARE4    },
    };

    mfxIMPL impl = MFX_IMPL_AUTO_ANY;
    int i;

    if (device) {
        for (i = 0; i < FF_ARRAY_ELEMS(impl_map); i++)
            if (!strcmp(device, impl_map[i].name)) {
                impl = impl_map[i].impl;
                break;
            }
        if (i == FF_ARRAY_ELEMS(impl_map))
            impl = strtol(device, NULL, 0);
    }

    return impl;
}

static int qsv_device_derive_from_child(AVHWDeviceContext *ctx,
                                        mfxIMPL implementation,
                                        AVHWDeviceContext *child_device_ctx,
                                        int flags)
{
    AVQSVDeviceContext *hwctx = ctx->hwctx;

    mfxVersion    ver = { { 3, 1 } };
    mfxHDL        handle;
    mfxHandleType handle_type;
    mfxStatus     err;
    int ret;

    switch (child_device_ctx->type) {
#if CONFIG_VAAPI
    case AV_HWDEVICE_TYPE_VAAPI:
        {
            AVVAAPIDeviceContext *child_device_hwctx = child_device_ctx->hwctx;
            handle_type = MFX_HANDLE_VA_DISPLAY;
            handle = (mfxHDL)child_device_hwctx->display;
        }
        break;
#endif
#if CONFIG_DXVA2
    case AV_HWDEVICE_TYPE_DXVA2:
        {
            AVDXVA2DeviceContext *child_device_hwctx = child_device_ctx->hwctx;
            handle_type = MFX_HANDLE_D3D9_DEVICE_MANAGER;
            handle = (mfxHDL)child_device_hwctx->devmgr;
        }
        break;
#endif
    default:
        ret = AVERROR(ENOSYS);
        goto fail;
    }

    err = MFXInit(implementation, &ver, &hwctx->session);
    if (err != MFX_ERR_NONE) {
        av_log(ctx, AV_LOG_ERROR, "Error initializing an MFX session: "
               "%d.\n", err);
        ret = AVERROR_UNKNOWN;
        goto fail;
    }

    err = MFXQueryVersion(hwctx->session, &ver);
    if (err != MFX_ERR_NONE) {
        av_log(ctx, AV_LOG_ERROR, "Error querying an MFX session: %d.\n", err);
        ret = AVERROR_UNKNOWN;
        goto fail;
    }

    av_log(ctx, AV_LOG_VERBOSE,
           "Initialize MFX session: API version is %d.%d, implementation version is %d.%d\n",
           MFX_VERSION_MAJOR, MFX_VERSION_MINOR, ver.Major, ver.Minor);

    MFXClose(hwctx->session);

    err = MFXInit(implementation, &ver, &hwctx->session);
    if (err != MFX_ERR_NONE) {
        av_log(ctx, AV_LOG_ERROR,
               "Error initializing an MFX session: %d.\n", err);
        ret = AVERROR_UNKNOWN;
        goto fail;
    }

    err = MFXVideoCORE_SetHandle(hwctx->session, handle_type, handle);
    if (err != MFX_ERR_NONE) {
        av_log(ctx, AV_LOG_ERROR, "Error setting child device handle: "
               "%d\n", err);
        ret = AVERROR_UNKNOWN;
        goto fail;
    }

    ret = MFXQueryVersion(hwctx->session,&ver);
    if (ret == MFX_ERR_NONE) {
        av_log(ctx, AV_LOG_VERBOSE, "MFX compile/runtime API: %d.%d/%d.%d\n",
               MFX_VERSION_MAJOR, MFX_VERSION_MINOR, ver.Major, ver.Minor);
    }
    return 0;

fail:
    if (hwctx->session)
        MFXClose(hwctx->session);
    return ret;
}

static int qsv_device_derive(AVHWDeviceContext *ctx,
                             AVHWDeviceContext *child_device_ctx, int flags)
{
    return qsv_device_derive_from_child(ctx, MFX_IMPL_HARDWARE_ANY,
                                        child_device_ctx, flags);
}

static int qsv_device_create(AVHWDeviceContext *ctx, const char *device,
                             AVDictionary *opts, int flags)
{
    QSVDevicePriv *priv;
    enum AVHWDeviceType child_device_type;
    AVHWDeviceContext *child_device;
    AVDictionaryEntry *e;

    mfxIMPL impl;
    int ret;

    priv = av_mallocz(sizeof(*priv));
    if (!priv)
        return AVERROR(ENOMEM);

    ctx->user_opaque = priv;
    ctx->free        = qsv_device_free;

    e = av_dict_get(opts, "child_device", NULL, 0);

    if (CONFIG_VAAPI)
        child_device_type = AV_HWDEVICE_TYPE_VAAPI;
    else if (CONFIG_DXVA2)
        child_device_type = AV_HWDEVICE_TYPE_DXVA2;
    else {
        av_log(ctx, AV_LOG_ERROR, "No supported child device type is enabled\n");
        return AVERROR(ENOSYS);
    }

    ret = av_hwdevice_ctx_create(&priv->child_device_ctx, child_device_type,
                                 e ? e->value : NULL, NULL, 0);
    if (ret < 0)
        return ret;

    child_device = (AVHWDeviceContext*)priv->child_device_ctx->data;

    impl = choose_implementation(device);

    return qsv_device_derive_from_child(ctx, impl, child_device, 0);
}

const HWContextType ff_hwcontext_type_qsv = {
    .type                   = AV_HWDEVICE_TYPE_QSV,
    .name                   = "QSV",

    .device_hwctx_size      = sizeof(AVQSVDeviceContext),
    .device_priv_size       = sizeof(QSVDeviceContext),
    .frames_hwctx_size      = sizeof(AVQSVFramesContext),
    .frames_priv_size       = sizeof(QSVFramesContext),

    .device_create          = qsv_device_create,
    .device_derive          = qsv_device_derive,
    .device_init            = qsv_device_init,
    .frames_get_constraints = qsv_frames_get_constraints,
    .frames_init            = qsv_frames_init,
    .frames_uninit          = qsv_frames_uninit,
    .frames_get_buffer      = qsv_get_buffer,
    .transfer_get_formats   = qsv_transfer_get_formats,
    .transfer_data_to       = qsv_transfer_data_to,
    .transfer_data_from     = qsv_transfer_data_from,
    .map_to                 = qsv_map_to,
    .map_from               = qsv_map_from,
    .frames_derive_to       = qsv_frames_derive_to,
    .frames_derive_from     = qsv_frames_derive_from,

    .pix_fmts = (const enum AVPixelFormat[]){ AV_PIX_FMT_QSV, AV_PIX_FMT_NONE },
};
