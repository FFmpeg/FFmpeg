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

#include <windows.h>

#define DXVA2API_USE_BITFIELDS
#define COBJMACROS

#include <d3d9.h>
#include <dxva2api.h>
#include <initguid.h>

#include "avassert.h"
#include "common.h"
#include "hwcontext.h"
#include "hwcontext_dxva2.h"
#include "hwcontext_internal.h"
#include "imgutils.h"
#include "pixdesc.h"
#include "pixfmt.h"
#include "compat/w32dlfcn.h"

typedef IDirect3D9* WINAPI pDirect3DCreate9(UINT);
typedef HRESULT WINAPI pDirect3DCreate9Ex(UINT, IDirect3D9Ex **);
typedef HRESULT WINAPI pCreateDeviceManager9(UINT *, IDirect3DDeviceManager9 **);

#define FF_D3DCREATE_FLAGS (D3DCREATE_SOFTWARE_VERTEXPROCESSING | \
                            D3DCREATE_MULTITHREADED | \
                            D3DCREATE_FPU_PRESERVE)

static const D3DPRESENT_PARAMETERS dxva2_present_params = {
    .Windowed         = TRUE,
    .BackBufferWidth  = 640,
    .BackBufferHeight = 480,
    .BackBufferCount  = 0,
    .SwapEffect       = D3DSWAPEFFECT_DISCARD,
    .Flags            = D3DPRESENTFLAG_VIDEO,
};

typedef struct DXVA2Mapping {
    uint32_t palette_dummy[256];
} DXVA2Mapping;

typedef struct DXVA2FramesContext {
    IDirect3DSurface9 **surfaces_internal;
    int              nb_surfaces_used;

    HANDLE  device_handle;
    IDirectXVideoAccelerationService *service;

    D3DFORMAT format;
} DXVA2FramesContext;

typedef struct DXVA2DevicePriv {
    HMODULE d3dlib;
    HMODULE dxva2lib;

    HANDLE device_handle;

    IDirect3D9       *d3d9;
    IDirect3DDevice9 *d3d9device;
} DXVA2DevicePriv;

static const struct {
    D3DFORMAT d3d_format;
    enum AVPixelFormat pix_fmt;
} supported_formats[] = {
    { MKTAG('N', 'V', '1', '2'), AV_PIX_FMT_NV12 },
    { MKTAG('P', '0', '1', '0'), AV_PIX_FMT_P010 },
    { D3DFMT_P8,                 AV_PIX_FMT_PAL8 },
};

DEFINE_GUID(video_decoder_service,   0xfc51a551, 0xd5e7, 0x11d9, 0xaf, 0x55, 0x00, 0x05, 0x4e, 0x43, 0xff, 0x02);
DEFINE_GUID(video_processor_service, 0xfc51a552, 0xd5e7, 0x11d9, 0xaf, 0x55, 0x00, 0x05, 0x4e, 0x43, 0xff, 0x02);

static void dxva2_frames_uninit(AVHWFramesContext *ctx)
{
    AVDXVA2DeviceContext *device_hwctx = ctx->device_ctx->hwctx;
    AVDXVA2FramesContext *frames_hwctx = ctx->hwctx;
    DXVA2FramesContext *s = ctx->internal->priv;
    int i;

    if (frames_hwctx->decoder_to_release)
        IDirectXVideoDecoder_Release(frames_hwctx->decoder_to_release);

    if (s->surfaces_internal) {
        for (i = 0; i < frames_hwctx->nb_surfaces; i++) {
            if (s->surfaces_internal[i])
                IDirect3DSurface9_Release(s->surfaces_internal[i]);
        }
    }
    av_freep(&s->surfaces_internal);

    if (s->service) {
        IDirectXVideoAccelerationService_Release(s->service);
        s->service = NULL;
    }

    if (s->device_handle != INVALID_HANDLE_VALUE) {
        IDirect3DDeviceManager9_CloseDeviceHandle(device_hwctx->devmgr, s->device_handle);
        s->device_handle = INVALID_HANDLE_VALUE;
    }
}

static void dxva2_pool_release_dummy(void *opaque, uint8_t *data)
{
    // important not to free anything here--data is a surface object
    // associated with the call to CreateSurface(), and these surfaces are
    // released in dxva2_frames_uninit()
}

static AVBufferRef *dxva2_pool_alloc(void *opaque, int size)
{
    AVHWFramesContext      *ctx = (AVHWFramesContext*)opaque;
    DXVA2FramesContext       *s = ctx->internal->priv;
    AVDXVA2FramesContext *hwctx = ctx->hwctx;

    if (s->nb_surfaces_used < hwctx->nb_surfaces) {
        s->nb_surfaces_used++;
        return av_buffer_create((uint8_t*)s->surfaces_internal[s->nb_surfaces_used - 1],
                                sizeof(*hwctx->surfaces), dxva2_pool_release_dummy, 0, 0);
    }

    return NULL;
}

static int dxva2_init_pool(AVHWFramesContext *ctx)
{
    AVDXVA2FramesContext *frames_hwctx = ctx->hwctx;
    AVDXVA2DeviceContext *device_hwctx = ctx->device_ctx->hwctx;
    DXVA2FramesContext              *s = ctx->internal->priv;
    int decode = (frames_hwctx->surface_type == DXVA2_VideoDecoderRenderTarget);

    int i;
    HRESULT hr;

    if (ctx->initial_pool_size <= 0)
        return 0;

    hr = IDirect3DDeviceManager9_OpenDeviceHandle(device_hwctx->devmgr, &s->device_handle);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to open device handle\n");
        return AVERROR_UNKNOWN;
    }

    hr = IDirect3DDeviceManager9_GetVideoService(device_hwctx->devmgr,
                                                 s->device_handle,
                                                 decode ? &video_decoder_service : &video_processor_service,
                                                 (void **)&s->service);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create the video service\n");
        return AVERROR_UNKNOWN;
    }

    for (i = 0; i < FF_ARRAY_ELEMS(supported_formats); i++) {
        if (ctx->sw_format == supported_formats[i].pix_fmt) {
            s->format = supported_formats[i].d3d_format;
            break;
        }
    }
    if (i == FF_ARRAY_ELEMS(supported_formats)) {
        av_log(ctx, AV_LOG_ERROR, "Unsupported pixel format: %s\n",
               av_get_pix_fmt_name(ctx->sw_format));
        return AVERROR(EINVAL);
    }

    s->surfaces_internal = av_mallocz_array(ctx->initial_pool_size,
                                            sizeof(*s->surfaces_internal));
    if (!s->surfaces_internal)
        return AVERROR(ENOMEM);

    hr = IDirectXVideoAccelerationService_CreateSurface(s->service,
                                                        ctx->width, ctx->height,
                                                        ctx->initial_pool_size - 1,
                                                        s->format, D3DPOOL_DEFAULT, 0,
                                                        frames_hwctx->surface_type,
                                                        s->surfaces_internal, NULL);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Could not create the surfaces\n");
        return AVERROR_UNKNOWN;
    }

    ctx->internal->pool_internal = av_buffer_pool_init2(sizeof(*s->surfaces_internal),
                                                        ctx, dxva2_pool_alloc, NULL);
    if (!ctx->internal->pool_internal)
        return AVERROR(ENOMEM);

    frames_hwctx->surfaces    = s->surfaces_internal;
    frames_hwctx->nb_surfaces = ctx->initial_pool_size;

    return 0;
}

static int dxva2_frames_init(AVHWFramesContext *ctx)
{
    AVDXVA2FramesContext *hwctx = ctx->hwctx;
    DXVA2FramesContext       *s = ctx->internal->priv;
    int ret;

    if (hwctx->surface_type != DXVA2_VideoDecoderRenderTarget &&
        hwctx->surface_type != DXVA2_VideoProcessorRenderTarget) {
        av_log(ctx, AV_LOG_ERROR, "Unknown surface type: %lu\n",
               hwctx->surface_type);
        return AVERROR(EINVAL);
    }

    s->device_handle = INVALID_HANDLE_VALUE;

    /* init the frame pool if the caller didn't provide one */
    if (!ctx->pool) {
        ret = dxva2_init_pool(ctx);
        if (ret < 0) {
            av_log(ctx, AV_LOG_ERROR, "Error creating an internal frame pool\n");
            return ret;
        }
    }

    return 0;
}

static int dxva2_get_buffer(AVHWFramesContext *ctx, AVFrame *frame)
{
    frame->buf[0] = av_buffer_pool_get(ctx->pool);
    if (!frame->buf[0])
        return AVERROR(ENOMEM);

    frame->data[3] = frame->buf[0]->data;
    frame->format  = AV_PIX_FMT_DXVA2_VLD;
    frame->width   = ctx->width;
    frame->height  = ctx->height;

    return 0;
}

static int dxva2_transfer_get_formats(AVHWFramesContext *ctx,
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

static void dxva2_unmap_frame(AVHWFramesContext *ctx, HWMapDescriptor *hwmap)
{
    IDirect3DSurface9 *surface = (IDirect3DSurface9*)hwmap->source->data[3];
    IDirect3DSurface9_UnlockRect(surface);
    av_freep(&hwmap->priv);
}

static int dxva2_map_frame(AVHWFramesContext *ctx, AVFrame *dst, const AVFrame *src,
                           int flags)
{
    IDirect3DSurface9 *surface = (IDirect3DSurface9*)src->data[3];
    DXVA2Mapping      *map;
    D3DSURFACE_DESC    surfaceDesc;
    D3DLOCKED_RECT     LockedRect;
    HRESULT            hr;
    int i, err, nb_planes;
    int lock_flags = 0;

    nb_planes = av_pix_fmt_count_planes(dst->format);

    hr = IDirect3DSurface9_GetDesc(surface, &surfaceDesc);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Error getting a surface description\n");
        return AVERROR_UNKNOWN;
    }

    if (!(flags & AV_HWFRAME_MAP_WRITE))
        lock_flags |= D3DLOCK_READONLY;
    if (flags & AV_HWFRAME_MAP_OVERWRITE)
        lock_flags |= D3DLOCK_DISCARD;

    hr = IDirect3DSurface9_LockRect(surface, &LockedRect, NULL, lock_flags);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Unable to lock DXVA2 surface\n");
        return AVERROR_UNKNOWN;
    }

    map = av_mallocz(sizeof(*map));
    if (!map) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    err = ff_hwframe_map_create(src->hw_frames_ctx, dst, src,
                                dxva2_unmap_frame, map);
    if (err < 0) {
        av_freep(&map);
        goto fail;
    }

    for (i = 0; i < nb_planes; i++)
        dst->linesize[i] = LockedRect.Pitch;

    av_image_fill_pointers(dst->data, dst->format, surfaceDesc.Height,
                           (uint8_t*)LockedRect.pBits, dst->linesize);

    if (dst->format == AV_PIX_FMT_PAL8)
        dst->data[1] = (uint8_t*)map->palette_dummy;

    return 0;
fail:
    IDirect3DSurface9_UnlockRect(surface);
    return err;
}

static int dxva2_transfer_data_to(AVHWFramesContext *ctx, AVFrame *dst,
                                  const AVFrame *src)
{
    AVFrame *map;
    int ret;

    if (src->format != ctx->sw_format)
        return AVERROR(ENOSYS);

    map = av_frame_alloc();
    if (!map)
        return AVERROR(ENOMEM);
    map->format = dst->format;

    ret = dxva2_map_frame(ctx, map, dst, AV_HWFRAME_MAP_WRITE | AV_HWFRAME_MAP_OVERWRITE);
    if (ret < 0)
        goto fail;

    av_image_copy(map->data, map->linesize, (const uint8_t **)src->data, src->linesize,
                  ctx->sw_format, src->width, src->height);

fail:
    av_frame_free(&map);
    return ret;
}

static int dxva2_transfer_data_from(AVHWFramesContext *ctx, AVFrame *dst,
                                    const AVFrame *src)
{
    AVFrame *map;
    ptrdiff_t src_linesize[4], dst_linesize[4];
    int ret, i;

    if (dst->format != ctx->sw_format)
        return AVERROR(ENOSYS);

    map = av_frame_alloc();
    if (!map)
        return AVERROR(ENOMEM);
    map->format = dst->format;

    ret = dxva2_map_frame(ctx, map, src, AV_HWFRAME_MAP_READ);
    if (ret < 0)
        goto fail;

    for (i = 0; i < 4; i++) {
        dst_linesize[i] = dst->linesize[i];
        src_linesize[i] = map->linesize[i];
    }
    av_image_copy_uc_from(dst->data, dst_linesize, (const uint8_t **)map->data, src_linesize,
                          ctx->sw_format, src->width, src->height);
fail:
    av_frame_free(&map);
    return ret;
}

static int dxva2_map_from(AVHWFramesContext *ctx,
                          AVFrame *dst, const AVFrame *src, int flags)
{
    int err;

    if (dst->format != AV_PIX_FMT_NONE && dst->format != ctx->sw_format)
        return AVERROR(ENOSYS);
    dst->format = ctx->sw_format;

    err = dxva2_map_frame(ctx, dst, src, flags);
    if (err < 0)
        return err;

    err = av_frame_copy_props(dst, src);
    if (err < 0)
        return err;

    return 0;
}

static void dxva2_device_free(AVHWDeviceContext *ctx)
{
    AVDXVA2DeviceContext *hwctx = ctx->hwctx;
    DXVA2DevicePriv       *priv = ctx->user_opaque;

    if (hwctx->devmgr && priv->device_handle != INVALID_HANDLE_VALUE)
        IDirect3DDeviceManager9_CloseDeviceHandle(hwctx->devmgr, priv->device_handle);

    if (hwctx->devmgr)
        IDirect3DDeviceManager9_Release(hwctx->devmgr);

    if (priv->d3d9device)
        IDirect3DDevice9_Release(priv->d3d9device);

    if (priv->d3d9)
        IDirect3D9_Release(priv->d3d9);

    if (priv->d3dlib)
        dlclose(priv->d3dlib);

    if (priv->dxva2lib)
        dlclose(priv->dxva2lib);

    av_freep(&ctx->user_opaque);
}

static int dxva2_device_create9(AVHWDeviceContext *ctx, UINT adapter)
{
    DXVA2DevicePriv *priv = ctx->user_opaque;
    D3DPRESENT_PARAMETERS d3dpp = dxva2_present_params;
    D3DDISPLAYMODE d3ddm;
    HRESULT hr;
    pDirect3DCreate9 *createD3D = (pDirect3DCreate9 *)dlsym(priv->d3dlib, "Direct3DCreate9");
    if (!createD3D) {
        av_log(ctx, AV_LOG_ERROR, "Failed to locate Direct3DCreate9\n");
        return AVERROR_UNKNOWN;
    }

    priv->d3d9 = createD3D(D3D_SDK_VERSION);
    if (!priv->d3d9) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create IDirect3D object\n");
        return AVERROR_UNKNOWN;
    }

    IDirect3D9_GetAdapterDisplayMode(priv->d3d9, adapter, &d3ddm);

    d3dpp.BackBufferFormat = d3ddm.Format;

    hr = IDirect3D9_CreateDevice(priv->d3d9, adapter, D3DDEVTYPE_HAL, GetDesktopWindow(),
                                 FF_D3DCREATE_FLAGS,
                                 &d3dpp, &priv->d3d9device);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create Direct3D device\n");
        return AVERROR_UNKNOWN;
    }

    return 0;
}

static int dxva2_device_create9ex(AVHWDeviceContext *ctx, UINT adapter)
{
    DXVA2DevicePriv *priv = ctx->user_opaque;
    D3DPRESENT_PARAMETERS d3dpp = dxva2_present_params;
    D3DDISPLAYMODEEX modeex = {0};
    IDirect3D9Ex *d3d9ex = NULL;
    IDirect3DDevice9Ex *exdev = NULL;
    HRESULT hr;
    pDirect3DCreate9Ex *createD3DEx = (pDirect3DCreate9Ex *)dlsym(priv->d3dlib, "Direct3DCreate9Ex");
    if (!createD3DEx)
        return AVERROR(ENOSYS);

    hr = createD3DEx(D3D_SDK_VERSION, &d3d9ex);
    if (FAILED(hr))
        return AVERROR_UNKNOWN;

    modeex.Size = sizeof(D3DDISPLAYMODEEX);
    hr = IDirect3D9Ex_GetAdapterDisplayModeEx(d3d9ex, adapter, &modeex, NULL);
    if (FAILED(hr)) {
        IDirect3D9Ex_Release(d3d9ex);
        return AVERROR_UNKNOWN;
    }

    d3dpp.BackBufferFormat = modeex.Format;

    hr = IDirect3D9Ex_CreateDeviceEx(d3d9ex, adapter, D3DDEVTYPE_HAL, GetDesktopWindow(),
                                     FF_D3DCREATE_FLAGS,
                                     &d3dpp, NULL, &exdev);
    if (FAILED(hr)) {
        IDirect3D9Ex_Release(d3d9ex);
        return AVERROR_UNKNOWN;
    }

    av_log(ctx, AV_LOG_VERBOSE, "Using D3D9Ex device.\n");
    priv->d3d9 = (IDirect3D9 *)d3d9ex;
    priv->d3d9device = (IDirect3DDevice9 *)exdev;
    return 0;
}

static int dxva2_device_create(AVHWDeviceContext *ctx, const char *device,
                               AVDictionary *opts, int flags)
{
    AVDXVA2DeviceContext *hwctx = ctx->hwctx;
    DXVA2DevicePriv *priv;
    pCreateDeviceManager9 *createDeviceManager = NULL;
    unsigned resetToken = 0;
    UINT adapter = D3DADAPTER_DEFAULT;
    HRESULT hr;
    int err;

    if (device)
        adapter = atoi(device);

    priv = av_mallocz(sizeof(*priv));
    if (!priv)
        return AVERROR(ENOMEM);

    ctx->user_opaque = priv;
    ctx->free        = dxva2_device_free;

    priv->device_handle = INVALID_HANDLE_VALUE;

    priv->d3dlib = dlopen("d3d9.dll", 0);
    if (!priv->d3dlib) {
        av_log(ctx, AV_LOG_ERROR, "Failed to load D3D9 library\n");
        return AVERROR_UNKNOWN;
    }
    priv->dxva2lib = dlopen("dxva2.dll", 0);
    if (!priv->dxva2lib) {
        av_log(ctx, AV_LOG_ERROR, "Failed to load DXVA2 library\n");
        return AVERROR_UNKNOWN;
    }

    createDeviceManager = (pCreateDeviceManager9 *)dlsym(priv->dxva2lib,
                                                         "DXVA2CreateDirect3DDeviceManager9");
    if (!createDeviceManager) {
        av_log(ctx, AV_LOG_ERROR, "Failed to locate DXVA2CreateDirect3DDeviceManager9\n");
        return AVERROR_UNKNOWN;
    }

    if (dxva2_device_create9ex(ctx, adapter) < 0) {
        // Retry with "classic" d3d9
        err = dxva2_device_create9(ctx, adapter);
        if (err < 0)
            return err;
    }

    hr = createDeviceManager(&resetToken, &hwctx->devmgr);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create Direct3D device manager\n");
        return AVERROR_UNKNOWN;
    }

    hr = IDirect3DDeviceManager9_ResetDevice(hwctx->devmgr, priv->d3d9device, resetToken);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to bind Direct3D device to device manager\n");
        return AVERROR_UNKNOWN;
    }

    hr = IDirect3DDeviceManager9_OpenDeviceHandle(hwctx->devmgr, &priv->device_handle);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to open device handle\n");
        return AVERROR_UNKNOWN;
    }

    return 0;
}

const HWContextType ff_hwcontext_type_dxva2 = {
    .type                 = AV_HWDEVICE_TYPE_DXVA2,
    .name                 = "DXVA2",

    .device_hwctx_size    = sizeof(AVDXVA2DeviceContext),
    .frames_hwctx_size    = sizeof(AVDXVA2FramesContext),
    .frames_priv_size     = sizeof(DXVA2FramesContext),

    .device_create        = dxva2_device_create,
    .frames_init          = dxva2_frames_init,
    .frames_uninit        = dxva2_frames_uninit,
    .frames_get_buffer    = dxva2_get_buffer,
    .transfer_get_formats = dxva2_transfer_get_formats,
    .transfer_data_to     = dxva2_transfer_data_to,
    .transfer_data_from   = dxva2_transfer_data_from,
    .map_from             = dxva2_map_from,

    .pix_fmts             = (const enum AVPixelFormat[]){ AV_PIX_FMT_DXVA2_VLD, AV_PIX_FMT_NONE },
};
