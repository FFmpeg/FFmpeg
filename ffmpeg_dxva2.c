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

#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
#endif
#define _WIN32_WINNT 0x0600
#define DXVA2API_USE_BITFIELDS
#define COBJMACROS

#include <stdint.h>

#include <d3d9.h>
#include <dxva2api.h>

#include "ffmpeg.h"

#include "libavcodec/dxva2.h"

#include "libavutil/avassert.h"
#include "libavutil/buffer.h"
#include "libavutil/frame.h"
#include "libavutil/imgutils.h"
#include "libavutil/pixfmt.h"

/* define all the GUIDs used directly here,
   to avoid problems with inconsistent dxva2api.h versions in mingw-w64 and different MSVC version */
#include <initguid.h>
DEFINE_GUID(IID_IDirectXVideoDecoderService, 0xfc51a551,0xd5e7,0x11d9,0xaf,0x55,0x00,0x05,0x4e,0x43,0xff,0x02);

DEFINE_GUID(DXVA2_ModeMPEG2_VLD,      0xee27417f, 0x5e28,0x4e65,0xbe,0xea,0x1d,0x26,0xb5,0x08,0xad,0xc9);
DEFINE_GUID(DXVA2_ModeMPEG2and1_VLD,  0x86695f12, 0x340e,0x4f04,0x9f,0xd3,0x92,0x53,0xdd,0x32,0x74,0x60);
DEFINE_GUID(DXVA2_ModeH264_E,         0x1b81be68, 0xa0c7,0x11d3,0xb9,0x84,0x00,0xc0,0x4f,0x2e,0x73,0xc5);
DEFINE_GUID(DXVA2_ModeH264_F,         0x1b81be69, 0xa0c7,0x11d3,0xb9,0x84,0x00,0xc0,0x4f,0x2e,0x73,0xc5);
DEFINE_GUID(DXVADDI_Intel_ModeH264_E, 0x604F8E68, 0x4951,0x4C54,0x88,0xFE,0xAB,0xD2,0x5C,0x15,0xB3,0xD6);
DEFINE_GUID(DXVA2_ModeVC1_D,          0x1b81beA3, 0xa0c7,0x11d3,0xb9,0x84,0x00,0xc0,0x4f,0x2e,0x73,0xc5);
DEFINE_GUID(DXVA2_ModeVC1_D2010,      0x1b81beA4, 0xa0c7,0x11d3,0xb9,0x84,0x00,0xc0,0x4f,0x2e,0x73,0xc5);
DEFINE_GUID(DXVA2_NoEncrypt,          0x1b81beD0, 0xa0c7,0x11d3,0xb9,0x84,0x00,0xc0,0x4f,0x2e,0x73,0xc5);
DEFINE_GUID(GUID_NULL,                0x00000000, 0x0000,0x0000,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);

typedef IDirect3D9* WINAPI pDirect3DCreate9(UINT);
typedef HRESULT WINAPI pCreateDeviceManager9(UINT *, IDirect3DDeviceManager9 **);

typedef struct dxva2_mode {
  const GUID     *guid;
  enum AVCodecID codec;
} dxva2_mode;

static const dxva2_mode dxva2_modes[] = {
    /* MPEG-2 */
    { &DXVA2_ModeMPEG2_VLD,      AV_CODEC_ID_MPEG2VIDEO },
    { &DXVA2_ModeMPEG2and1_VLD,  AV_CODEC_ID_MPEG2VIDEO },

    /* H.264 */
    { &DXVA2_ModeH264_F,         AV_CODEC_ID_H264 },
    { &DXVA2_ModeH264_E,         AV_CODEC_ID_H264 },
    /* Intel specific H.264 mode */
    { &DXVADDI_Intel_ModeH264_E, AV_CODEC_ID_H264 },

    /* VC-1 / WMV3 */
    { &DXVA2_ModeVC1_D2010,      AV_CODEC_ID_VC1  },
    { &DXVA2_ModeVC1_D2010,      AV_CODEC_ID_WMV3 },
    { &DXVA2_ModeVC1_D,          AV_CODEC_ID_VC1  },
    { &DXVA2_ModeVC1_D,          AV_CODEC_ID_WMV3 },

    { NULL,                      0 },
};

typedef struct surface_info {
    int used;
    uint64_t age;
} surface_info;

typedef struct DXVA2Context {
    HMODULE d3dlib;
    HMODULE dxva2lib;

    HANDLE  deviceHandle;

    IDirect3D9                  *d3d9;
    IDirect3DDevice9            *d3d9device;
    IDirect3DDeviceManager9     *d3d9devmgr;
    IDirectXVideoDecoderService *decoder_service;
    IDirectXVideoDecoder        *decoder;

    GUID                        decoder_guid;
    DXVA2_ConfigPictureDecode   decoder_config;

    LPDIRECT3DSURFACE9          *surfaces;
    surface_info                *surface_infos;
    uint32_t                    num_surfaces;
    uint64_t                    surface_age;

    AVFrame                     *tmp_frame;
} DXVA2Context;

typedef struct DXVA2SurfaceWrapper {
    DXVA2Context         *ctx;
    LPDIRECT3DSURFACE9   surface;
    IDirectXVideoDecoder *decoder;
} DXVA2SurfaceWrapper;

static void dxva2_destroy_decoder(AVCodecContext *s)
{
    InputStream  *ist = s->opaque;
    DXVA2Context *ctx = ist->hwaccel_ctx;
    int i;

    if (ctx->surfaces) {
        for (i = 0; i < ctx->num_surfaces; i++) {
            if (ctx->surfaces[i])
                IDirect3DSurface9_Release(ctx->surfaces[i]);
        }
    }
    av_freep(&ctx->surfaces);
    av_freep(&ctx->surface_infos);
    ctx->num_surfaces = 0;
    ctx->surface_age  = 0;

    if (ctx->decoder) {
        IDirectXVideoDecoder_Release(ctx->decoder);
        ctx->decoder = NULL;
    }
}

static void dxva2_uninit(AVCodecContext *s)
{
    InputStream  *ist = s->opaque;
    DXVA2Context *ctx = ist->hwaccel_ctx;

    ist->hwaccel_uninit        = NULL;
    ist->hwaccel_get_buffer    = NULL;
    ist->hwaccel_retrieve_data = NULL;

    if (ctx->decoder)
        dxva2_destroy_decoder(s);

    if (ctx->decoder_service)
        IDirectXVideoDecoderService_Release(ctx->decoder_service);

    if (ctx->d3d9devmgr && ctx->deviceHandle != INVALID_HANDLE_VALUE)
        IDirect3DDeviceManager9_CloseDeviceHandle(ctx->d3d9devmgr, ctx->deviceHandle);

    if (ctx->d3d9devmgr)
        IDirect3DDeviceManager9_Release(ctx->d3d9devmgr);

    if (ctx->d3d9device)
        IDirect3DDevice9_Release(ctx->d3d9device);

    if (ctx->d3d9)
        IDirect3D9_Release(ctx->d3d9);

    if (ctx->d3dlib)
        FreeLibrary(ctx->d3dlib);

    if (ctx->dxva2lib)
        FreeLibrary(ctx->dxva2lib);

    av_frame_free(&ctx->tmp_frame);

    av_freep(&ist->hwaccel_ctx);
    av_freep(&s->hwaccel_context);
}

static void dxva2_release_buffer(void *opaque, uint8_t *data)
{
    DXVA2SurfaceWrapper *w   = opaque;
    DXVA2Context        *ctx = w->ctx;
    int i;

    for (i = 0; i < ctx->num_surfaces; i++) {
        if (ctx->surfaces[i] == w->surface) {
            ctx->surface_infos[i].used = 0;
            break;
        }
    }
    IDirect3DSurface9_Release(w->surface);
    IDirectXVideoDecoder_Release(w->decoder);
    av_free(w);
}

static int dxva2_get_buffer(AVCodecContext *s, AVFrame *frame, int flags)
{
    InputStream  *ist = s->opaque;
    DXVA2Context *ctx = ist->hwaccel_ctx;
    int i, old_unused = -1;
    LPDIRECT3DSURFACE9 surface;
    DXVA2SurfaceWrapper *w = NULL;

    av_assert0(frame->format == AV_PIX_FMT_DXVA2_VLD);

    for (i = 0; i < ctx->num_surfaces; i++) {
        surface_info *info = &ctx->surface_infos[i];
        if (!info->used && (old_unused == -1 || info->age < ctx->surface_infos[old_unused].age))
            old_unused = i;
    }
    if (old_unused == -1) {
        av_log(NULL, AV_LOG_ERROR, "No free DXVA2 surface!\n");
        return AVERROR(ENOMEM);
    }
    i = old_unused;

    surface = ctx->surfaces[i];

    w = av_mallocz(sizeof(*w));
    if (!w)
        return AVERROR(ENOMEM);

    frame->buf[0] = av_buffer_create((uint8_t*)surface, 0,
                                     dxva2_release_buffer, w,
                                     AV_BUFFER_FLAG_READONLY);
    if (!frame->buf[0]) {
        av_free(w);
        return AVERROR(ENOMEM);
    }

    w->ctx     = ctx;
    w->surface = surface;
    IDirect3DSurface9_AddRef(w->surface);
    w->decoder = ctx->decoder;
    IDirectXVideoDecoder_AddRef(w->decoder);

    ctx->surface_infos[i].used = 1;
    ctx->surface_infos[i].age  = ctx->surface_age++;

    frame->data[3] = (uint8_t *)surface;

    return 0;
}

static int dxva2_retrieve_data(AVCodecContext *s, AVFrame *frame)
{
    LPDIRECT3DSURFACE9 surface =  (LPDIRECT3DSURFACE9)frame->data[3];
    InputStream        *ist = s->opaque;
    DXVA2Context       *ctx = ist->hwaccel_ctx;
    D3DSURFACE_DESC    surfaceDesc;
    D3DLOCKED_RECT     LockedRect;
    HRESULT            hr;
    int                ret;

    IDirect3DSurface9_GetDesc(surface, &surfaceDesc);

    ctx->tmp_frame->width  = frame->width;
    ctx->tmp_frame->height = frame->height;
    ctx->tmp_frame->format = AV_PIX_FMT_NV12;

    ret = av_frame_get_buffer(ctx->tmp_frame, 32);
    if (ret < 0)
        return ret;

    hr = IDirect3DSurface9_LockRect(surface, &LockedRect, NULL, D3DLOCK_READONLY);
    if (FAILED(hr)) {
        av_log(NULL, AV_LOG_ERROR, "Unable to lock DXVA2 surface\n");
        return AVERROR_UNKNOWN;
    }

    av_image_copy_plane(ctx->tmp_frame->data[0], ctx->tmp_frame->linesize[0],
                        (uint8_t*)LockedRect.pBits,
                        LockedRect.Pitch, frame->width, frame->height);

    av_image_copy_plane(ctx->tmp_frame->data[1], ctx->tmp_frame->linesize[1],
                        (uint8_t*)LockedRect.pBits + LockedRect.Pitch * surfaceDesc.Height,
                        LockedRect.Pitch, frame->width, frame->height / 2);

    IDirect3DSurface9_UnlockRect(surface);

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

static int dxva2_alloc(AVCodecContext *s)
{
    InputStream  *ist = s->opaque;
    int loglevel = (ist->hwaccel_id == HWACCEL_AUTO) ? AV_LOG_VERBOSE : AV_LOG_ERROR;
    DXVA2Context *ctx;
    pDirect3DCreate9      *createD3D = NULL;
    pCreateDeviceManager9 *createDeviceManager = NULL;
    HRESULT hr;
    D3DPRESENT_PARAMETERS d3dpp = {0};
    D3DDISPLAYMODE        d3ddm;
    unsigned resetToken = 0;
    UINT adapter = D3DADAPTER_DEFAULT;

    ctx = av_mallocz(sizeof(*ctx));
    if (!ctx)
        return AVERROR(ENOMEM);

    ctx->deviceHandle = INVALID_HANDLE_VALUE;

    ist->hwaccel_ctx           = ctx;
    ist->hwaccel_uninit        = dxva2_uninit;
    ist->hwaccel_get_buffer    = dxva2_get_buffer;
    ist->hwaccel_retrieve_data = dxva2_retrieve_data;

    ctx->d3dlib = LoadLibrary("d3d9.dll");
    if (!ctx->d3dlib) {
        av_log(NULL, loglevel, "Failed to load D3D9 library\n");
        goto fail;
    }
    ctx->dxva2lib = LoadLibrary("dxva2.dll");
    if (!ctx->dxva2lib) {
        av_log(NULL, loglevel, "Failed to load DXVA2 library\n");
        goto fail;
    }

    createD3D = (pDirect3DCreate9 *)GetProcAddress(ctx->d3dlib, "Direct3DCreate9");
    if (!createD3D) {
        av_log(NULL, loglevel, "Failed to locate Direct3DCreate9\n");
        goto fail;
    }
    createDeviceManager = (pCreateDeviceManager9 *)GetProcAddress(ctx->dxva2lib, "DXVA2CreateDirect3DDeviceManager9");
    if (!createDeviceManager) {
        av_log(NULL, loglevel, "Failed to locate DXVA2CreateDirect3DDeviceManager9\n");
        goto fail;
    }

    ctx->d3d9 = createD3D(D3D_SDK_VERSION);
    if (!ctx->d3d9) {
        av_log(NULL, loglevel, "Failed to create IDirect3D object\n");
        goto fail;
    }

    if (ist->hwaccel_device) {
        adapter = atoi(ist->hwaccel_device);
        av_log(NULL, AV_LOG_INFO, "Using HWAccel device %d\n", adapter);
    }

    IDirect3D9_GetAdapterDisplayMode(ctx->d3d9, adapter, &d3ddm);
    d3dpp.Windowed         = TRUE;
    d3dpp.BackBufferWidth  = 640;
    d3dpp.BackBufferHeight = 480;
    d3dpp.BackBufferCount  = 0;
    d3dpp.BackBufferFormat = d3ddm.Format;
    d3dpp.SwapEffect       = D3DSWAPEFFECT_DISCARD;
    d3dpp.Flags            = D3DPRESENTFLAG_VIDEO;

    hr = IDirect3D9_CreateDevice(ctx->d3d9, adapter, D3DDEVTYPE_HAL, GetShellWindow(),
                                 D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED | D3DCREATE_FPU_PRESERVE,
                                 &d3dpp, &ctx->d3d9device);
    if (FAILED(hr)) {
        av_log(NULL, loglevel, "Failed to create Direct3D device\n");
        goto fail;
    }

    hr = createDeviceManager(&resetToken, &ctx->d3d9devmgr);
    if (FAILED(hr)) {
        av_log(NULL, loglevel, "Failed to create Direct3D device manager\n");
        goto fail;
    }

    hr = IDirect3DDeviceManager9_ResetDevice(ctx->d3d9devmgr, ctx->d3d9device, resetToken);
    if (FAILED(hr)) {
        av_log(NULL, loglevel, "Failed to bind Direct3D device to device manager\n");
        goto fail;
    }

    hr = IDirect3DDeviceManager9_OpenDeviceHandle(ctx->d3d9devmgr, &ctx->deviceHandle);
    if (FAILED(hr)) {
        av_log(NULL, loglevel, "Failed to open device handle\n");
        goto fail;
    }

    hr = IDirect3DDeviceManager9_GetVideoService(ctx->d3d9devmgr, ctx->deviceHandle, &IID_IDirectXVideoDecoderService, (void **)&ctx->decoder_service);
    if (FAILED(hr)) {
        av_log(NULL, loglevel, "Failed to create IDirectXVideoDecoderService\n");
        goto fail;
    }

    ctx->tmp_frame = av_frame_alloc();
    if (!ctx->tmp_frame)
        goto fail;

    s->hwaccel_context = av_mallocz(sizeof(struct dxva_context));
    if (!s->hwaccel_context)
        goto fail;

    return 0;
fail:
    dxva2_uninit(s);
    return AVERROR(EINVAL);
}

static int dxva2_get_decoder_configuration(AVCodecContext *s, const GUID *device_guid,
                                           const DXVA2_VideoDesc *desc,
                                           DXVA2_ConfigPictureDecode *config)
{
    InputStream  *ist = s->opaque;
    int loglevel = (ist->hwaccel_id == HWACCEL_AUTO) ? AV_LOG_VERBOSE : AV_LOG_ERROR;
    DXVA2Context *ctx = ist->hwaccel_ctx;
    unsigned cfg_count = 0, best_score = 0;
    DXVA2_ConfigPictureDecode *cfg_list = NULL;
    DXVA2_ConfigPictureDecode best_cfg = {{0}};
    HRESULT hr;
    int i;

    hr = IDirectXVideoDecoderService_GetDecoderConfigurations(ctx->decoder_service, device_guid, desc, NULL, &cfg_count, &cfg_list);
    if (FAILED(hr)) {
        av_log(NULL, loglevel, "Unable to retrieve decoder configurations\n");
        return AVERROR(EINVAL);
    }

    for (i = 0; i < cfg_count; i++) {
        DXVA2_ConfigPictureDecode *cfg = &cfg_list[i];

        unsigned score;
        if (cfg->ConfigBitstreamRaw == 1)
            score = 1;
        else if (s->codec_id == AV_CODEC_ID_H264 && cfg->ConfigBitstreamRaw == 2)
            score = 2;
        else
            continue;
        if (IsEqualGUID(&cfg->guidConfigBitstreamEncryption, &DXVA2_NoEncrypt))
            score += 16;
        if (score > best_score) {
            best_score = score;
            best_cfg   = *cfg;
        }
    }
    CoTaskMemFree(cfg_list);

    if (!best_score) {
        av_log(NULL, loglevel, "No valid decoder configuration available\n");
        return AVERROR(EINVAL);
    }

    *config = best_cfg;
    return 0;
}

static int dxva2_create_decoder(AVCodecContext *s)
{
    InputStream  *ist = s->opaque;
    int loglevel = (ist->hwaccel_id == HWACCEL_AUTO) ? AV_LOG_VERBOSE : AV_LOG_ERROR;
    DXVA2Context *ctx = ist->hwaccel_ctx;
    struct dxva_context *dxva_ctx = s->hwaccel_context;
    GUID *guid_list = NULL;
    unsigned guid_count = 0, i, j;
    GUID device_guid = GUID_NULL;
    D3DFORMAT target_format = 0;
    DXVA2_VideoDesc desc = { 0 };
    DXVA2_ConfigPictureDecode config;
    HRESULT hr;
    int surface_alignment;
    int ret;

    hr = IDirectXVideoDecoderService_GetDecoderDeviceGuids(ctx->decoder_service, &guid_count, &guid_list);
    if (FAILED(hr)) {
        av_log(NULL, loglevel, "Failed to retrieve decoder device GUIDs\n");
        goto fail;
    }

    for (i = 0; dxva2_modes[i].guid; i++) {
        D3DFORMAT *target_list = NULL;
        unsigned target_count = 0;
        const dxva2_mode *mode = &dxva2_modes[i];
        if (mode->codec != s->codec_id)
            continue;

        for (j = 0; j < guid_count; j++) {
            if (IsEqualGUID(mode->guid, &guid_list[j]))
                break;
        }
        if (j == guid_count)
            continue;

        hr = IDirectXVideoDecoderService_GetDecoderRenderTargets(ctx->decoder_service, mode->guid, &target_count, &target_list);
        if (FAILED(hr)) {
            continue;
        }
        for (j = 0; j < target_count; j++) {
            const D3DFORMAT format = target_list[j];
            if (format == MKTAG('N','V','1','2')) {
                target_format = format;
                break;
            }
        }
        CoTaskMemFree(target_list);
        if (target_format) {
            device_guid = *mode->guid;
            break;
        }
    }
    CoTaskMemFree(guid_list);

    if (IsEqualGUID(&device_guid, &GUID_NULL)) {
        av_log(NULL, loglevel, "No decoder device for codec found\n");
        goto fail;
    }

    desc.SampleWidth  = s->coded_width;
    desc.SampleHeight = s->coded_height;
    desc.Format       = target_format;

    ret = dxva2_get_decoder_configuration(s, &device_guid, &desc, &config);
    if (ret < 0) {
        goto fail;
    }

    /* decoding MPEG-2 requires additional alignment on some Intel GPUs,
       but it causes issues for H.264 on certain AMD GPUs..... */
    if (s->codec_id == AV_CODEC_ID_MPEG2VIDEO)
        surface_alignment = 32;
    else
        surface_alignment = 16;

    /* 4 base work surfaces */
    ctx->num_surfaces = 4;

    /* add surfaces based on number of possible refs */
    if (s->codec_id == AV_CODEC_ID_H264)
        ctx->num_surfaces += 16;
    else
        ctx->num_surfaces += 2;

    /* add extra surfaces for frame threading */
    if (s->active_thread_type & FF_THREAD_FRAME)
        ctx->num_surfaces += s->thread_count;

    ctx->surfaces      = av_mallocz(ctx->num_surfaces * sizeof(*ctx->surfaces));
    ctx->surface_infos = av_mallocz(ctx->num_surfaces * sizeof(*ctx->surface_infos));

    if (!ctx->surfaces || !ctx->surface_infos) {
        av_log(NULL, loglevel, "Unable to allocate surface arrays\n");
        goto fail;
    }

    hr = IDirectXVideoDecoderService_CreateSurface(ctx->decoder_service,
                                                   FFALIGN(s->coded_width, surface_alignment),
                                                   FFALIGN(s->coded_height, surface_alignment),
                                                   ctx->num_surfaces - 1,
                                                   target_format, D3DPOOL_DEFAULT, 0,
                                                   DXVA2_VideoDecoderRenderTarget,
                                                   ctx->surfaces, NULL);
    if (FAILED(hr)) {
        av_log(NULL, loglevel, "Failed to create %d video surfaces\n", ctx->num_surfaces);
        goto fail;
    }

    hr = IDirectXVideoDecoderService_CreateVideoDecoder(ctx->decoder_service, &device_guid,
                                                        &desc, &config, ctx->surfaces,
                                                        ctx->num_surfaces, &ctx->decoder);
    if (FAILED(hr)) {
        av_log(NULL, loglevel, "Failed to create DXVA2 video decoder\n");
        goto fail;
    }

    ctx->decoder_guid   = device_guid;
    ctx->decoder_config = config;

    dxva_ctx->cfg           = &ctx->decoder_config;
    dxva_ctx->decoder       = ctx->decoder;
    dxva_ctx->surface       = ctx->surfaces;
    dxva_ctx->surface_count = ctx->num_surfaces;

    if (IsEqualGUID(&ctx->decoder_guid, &DXVADDI_Intel_ModeH264_E))
        dxva_ctx->workaround |= FF_DXVA2_WORKAROUND_INTEL_CLEARVIDEO;

    return 0;
fail:
    dxva2_destroy_decoder(s);
    return AVERROR(EINVAL);
}

int dxva2_init(AVCodecContext *s)
{
    InputStream *ist = s->opaque;
    int loglevel = (ist->hwaccel_id == HWACCEL_AUTO) ? AV_LOG_VERBOSE : AV_LOG_ERROR;
    DXVA2Context *ctx;
    int ret;

    if (!ist->hwaccel_ctx) {
        ret = dxva2_alloc(s);
        if (ret < 0)
            return ret;
    }
    ctx = ist->hwaccel_ctx;

    if (s->codec_id == AV_CODEC_ID_H264 &&
        (s->profile & ~FF_PROFILE_H264_CONSTRAINED) > FF_PROFILE_H264_HIGH) {
        av_log(NULL, loglevel, "Unsupported H.264 profile for DXVA2 HWAccel: %d\n", s->profile);
        return AVERROR(EINVAL);
    }

    if (ctx->decoder)
        dxva2_destroy_decoder(s);

    ret = dxva2_create_decoder(s);
    if (ret < 0) {
        av_log(NULL, loglevel, "Error creating the DXVA2 decoder\n");
        return ret;
    }

    return 0;
}
