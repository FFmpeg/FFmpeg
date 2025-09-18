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

#include "vsrc_gfxcapture_winrt.hpp"
#include "vsrc_gfxcapture_shader.h"

#include <dwmapi.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dispatcherqueue.h>
#include <windows.foundation.h>
#include <windows.graphics.capture.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.h>
#if HAVE_IDIRECT3DDXGIINTERFACEACCESS
#include <windows.graphics.directx.direct3d11.interop.h>
#endif

extern "C" {
#include "libavutil/avassert.h"
#include "libavutil/internal.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"
#include "libavutil/pixdesc.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_d3d11va.h"
#include "avfilter.h"
#include "filters.h"
#include "video.h"

#include "vsrc_gfxcapture.h"
}

#include <cinttypes>
#include <condition_variable>
#include <cwchar>
#include <memory>
#include <mutex>
#include <regex>
#include <string>
#include <thread>
#include <type_traits>

using namespace ABI::Windows::System;
using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::Graphics::Capture;
using namespace ABI::Windows::Graphics::DirectX::Direct3D11;
using namespace Windows::Graphics::DirectX::Direct3D11;
using Microsoft::WRL::ComPtr;
using ABI::Windows::Graphics::SizeInt32;
using ABI::Windows::Foundation::TimeSpan;
using ABI::Windows::Graphics::DirectX::DirectXPixelFormat;

#define TIMESPAN_RES 10000000
#define TIMESPAN_RES64 INT64_C(10000000)

#define CAPTURE_POOL_SIZE 2

enum {
    WM_WGC_THREAD_SHUTDOWN = WM_APP + 1
};

#define CCTX(ctx) static_cast<GfxCaptureContext*>(ctx)

typedef struct GfxCaptureFunctions {
    hmodule_ptr_t graphicscapture_handle;

    hmodule_ptr_t combase_handle;
    HRESULT (WINAPI *RoInitialize)(RO_INIT_TYPE initType);
    void (WINAPI *RoUninitialize)(void);
    HRESULT (WINAPI *RoGetActivationFactory)(HSTRING activatableClassId, REFIID iid, void **factory);
    HRESULT (WINAPI *WindowsCreateStringReference)(PCWSTR sourceString, UINT32 length, HSTRING_HEADER *hstringHeader, HSTRING *string);

    hmodule_ptr_t dwmapi_handle;
    HRESULT (WINAPI *DwmGetWindowAttribute)(HWND hwnd, DWORD dwAttribute, PVOID pvAttribute, DWORD cbAttribute);

    hmodule_ptr_t d3d11_handle;
    HRESULT (WINAPI *CreateDirect3D11DeviceFromDXGIDevice)(IDXGIDevice *dxgiDevice, IInspectable **graphicsDevice);

    hmodule_ptr_t coremsg_handle;
    HRESULT (WINAPI *CreateDispatcherQueueController)(DispatcherQueueOptions options, PDISPATCHERQUEUECONTROLLER *dispatcherQueueController);

    hmodule_ptr_t user32_handle;
    DPI_AWARENESS_CONTEXT (WINAPI *SetThreadDpiAwarenessContext)(DPI_AWARENESS_CONTEXT dpiContext);

    hmodule_ptr_t kernel32_handle;
    HRESULT (WINAPI *SetThreadDescription)(HANDLE hThread, PCWSTR lpThreadDescription);

    hmodule_ptr_t d3dcompiler_handle;
    HRESULT (WINAPI *D3DCompile)(LPCVOID pSrcData, SIZE_T SrcDataSize, LPCSTR pSourceName, const D3D10_SHADER_MACRO *pDefines, ID3DInclude *pInclude,
                                 LPCSTR pEntrypoint, LPCSTR pTarget, UINT Flags1, UINT Flags2, ID3DBlob **ppCode, ID3DBlob **ppErrorMsgs);
} GfxCaptureFunctions;

// This struct contains all data handled by the capture thread
struct GfxCaptureContextWgc {
    ComPtr<IDispatcherQueueController> dispatcher_queue_controller;
    ComPtr<IDispatcherQueue> dispatcher_queue;

    ComPtr<IGraphicsCaptureItem> capture_item;
    ComPtr<IDirect3DDevice> d3d_device;
    ComPtr<IDirect3D11CaptureFramePool> frame_pool;
    ComPtr<IGraphicsCaptureSession> capture_session;

    EventRegistrationToken frame_arrived_token { 0 };
    EventRegistrationToken closed_token { 0 };

    std::mutex frame_arrived_mutex;
    std::condition_variable frame_arrived_cond;
    bool window_closed { false };
    uint64_t frame_seq { 0 };

    SizeInt32 cap_size { 0, 0 };
    RECT client_area_offsets { 0, 0, 0, 0 };
};

struct GfxCaptureContextD3D {
    ComPtr<ID3D11VertexShader> vertex_shader;
    ComPtr<ID3D11PixelShader> pixel_shader;
    ComPtr<ID3D11SamplerState> sampler_state;
    ComPtr<ID3D11Buffer> shader_cb;
    ComPtr<ID3D11DeviceContext> deferred_ctx;
};

struct GfxCaptureContextCpp {
    GfxCaptureFunctions fn;
    std::unique_ptr<GfxCaptureContextWgc> wgc;
    std::unique_ptr<GfxCaptureContextD3D> d3d;

    std::thread wgc_thread;
    DWORD wgc_thread_id { 0 };
    std::mutex wgc_thread_init_mutex;
    std::condition_variable wgc_thread_init_cond;
    volatile int wgc_thread_init_res { INT_MAX };
    std::recursive_mutex wgc_thread_uninit_mutex;
    volatile int wgc_thread_res { 0 };
    std::shared_ptr<void> wgc_thread_cb_data;

    HWND capture_hwnd { nullptr };
    HMONITOR capture_hmonitor { nullptr };

    AVBufferRef *device_ref { nullptr };
    AVHWDeviceContext *device_ctx { nullptr };
    AVD3D11VADeviceContext *device_hwctx { nullptr };

    AVBufferRef *frames_ref { nullptr };
    AVHWFramesContext *frames_ctx { nullptr };
    AVD3D11VAFramesContext *frames_hwctx { nullptr };

    int64_t first_pts { 0 };
    int64_t last_pts { 0 };
};

template <typename T>
static HRESULT get_activation_factory(GfxCaptureContextCpp *ctx, PCWSTR clsid, T** factory) {
    HSTRING_HEADER hsheader = { 0 };
    HSTRING hs = NULL;

    HRESULT hr = ctx->fn.WindowsCreateStringReference(clsid, (UINT32)wcslen(clsid), &hsheader, &hs);
    if (FAILED(hr))
        return hr;

    return ctx->fn.RoGetActivationFactory(hs, IID_PPV_ARGS(factory));
}

#define CHECK_HR(fcall, action) \
    do { \
        HRESULT fhr = fcall; \
        if (FAILED(fhr)) { \
            av_log(avctx, AV_LOG_ERROR, #fcall " failed: 0x%08lX\n", fhr); \
            action; \
        } \
    } while (0)
#define CHECK_HR_RET(...) CHECK_HR((__VA_ARGS__), return AVERROR_EXTERNAL)
#define CHECK_HR_FAIL(...) CHECK_HR((__VA_ARGS__), ret = AVERROR_EXTERNAL; goto fail)
#define CHECK_HR_LOG(...) CHECK_HR((__VA_ARGS__), (void)0)

/****************************************************
 * Windows Graphics Capture Worker Thread           *
 * All wgc_* functions must run only on WGC thread! *
 ****************************************************/

static void wgc_frame_arrived_handler(const std::unique_ptr<GfxCaptureContextWgc> &wgctx) {
    {
        std::lock_guard lock(wgctx->frame_arrived_mutex);
        wgctx->frame_seq += 1;
    }
    wgctx->frame_arrived_cond.notify_one();
}

static void wgc_closed_handler(const std::unique_ptr<GfxCaptureContextWgc> &wgctx) {
    {
        std::lock_guard lock(wgctx->frame_arrived_mutex);
        wgctx->window_closed = true;
    }
    wgctx->frame_arrived_cond.notify_one();
}

static void wgc_stop_capture_session(AVFilterContext *avctx) noexcept
{
    GfxCaptureContext *cctx = CCTX(avctx->priv);
    GfxCaptureContextCpp *ctx = cctx->ctx;
    std::unique_ptr<GfxCaptureContextWgc> &wgctx = ctx->wgc;

    if (wgctx->closed_token.value && wgctx->capture_item) {
        CHECK_HR_LOG(wgctx->capture_item->remove_Closed(wgctx->closed_token));
        wgctx->closed_token.value = 0;
    }

    if (wgctx->frame_arrived_token.value && wgctx->frame_pool) {
        CHECK_HR_LOG(wgctx->frame_pool->remove_FrameArrived(wgctx->frame_arrived_token));
        wgctx->frame_arrived_token.value = 0;
    }

    if (wgctx->capture_session) {
        ComPtr<IClosable> closable;
        if (SUCCEEDED(wgctx->capture_session.As(&closable))) {
            CHECK_HR_LOG(closable->Close());
        } else {
            av_log(avctx, AV_LOG_ERROR, "Failed to get capture session IClosable interface\n");
        }
    }

    if (wgctx->frame_pool) {
        ComPtr<IClosable> closable;
        if (SUCCEEDED(wgctx->frame_pool.As(&closable))) {
            CHECK_HR_LOG(closable->Close());
        } else {
            av_log(avctx, AV_LOG_ERROR, "Failed to get frame pool IClosable interface\n");
        }
    }

    wgctx->capture_session.Reset();
    wgctx->frame_pool.Reset();
    wgctx->capture_item.Reset();
    wgctx->d3d_device.Reset();
}

static int wgc_calculate_client_area(AVFilterContext *avctx)
{
    GfxCaptureContext *cctx = CCTX(avctx->priv);
    GfxCaptureContextCpp *ctx = cctx->ctx;
    std::unique_ptr<GfxCaptureContextWgc> &wgctx = ctx->wgc;

    if (!ctx->capture_hwnd) {
        wgctx->client_area_offsets.left = 0;
        wgctx->client_area_offsets.top = 0;
        wgctx->client_area_offsets.right = 0;
        wgctx->client_area_offsets.bottom = 0;
        return 0;
    }

    RECT client_rect = {};
    RECT frame_bounds = {};
    RECT window_rect = {};

    if (IsIconic(ctx->capture_hwnd)) {
        av_log(avctx, AV_LOG_VERBOSE, "Capture window is iconic, no client area\n");
        return 0;
    }

    if (!GetClientRect(ctx->capture_hwnd, &client_rect)) {
        av_log(avctx, AV_LOG_ERROR, "GetClientRect failed\n");
        return AVERROR_EXTERNAL;
    }

    SetLastError(0);
    if (!MapWindowPoints(ctx->capture_hwnd, nullptr, (POINT*)&client_rect, 2) && GetLastError()) {
        av_log(avctx, AV_LOG_ERROR, "MapWindowPoints failed\n");
        return AVERROR_EXTERNAL;
    }

    if (FAILED(ctx->fn.DwmGetWindowAttribute(ctx->capture_hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &frame_bounds, sizeof(window_rect))))
        av_log(avctx, AV_LOG_DEBUG, "DwmGetWindowAttribute failed\n");

    if (!GetWindowRect(ctx->capture_hwnd, &window_rect))
        av_log(avctx, AV_LOG_DEBUG, "GetWindowRect failed\n");

    if (wgctx->cap_size.Width == frame_bounds.right - frame_bounds.left ||
        wgctx->cap_size.Height == frame_bounds.bottom - frame_bounds.top) {
        av_log(avctx, AV_LOG_DEBUG, "Using window rect from DWMWA_EXTENDED_FRAME_BOUNDS\n");
    } else if (wgctx->cap_size.Width == window_rect.right - window_rect.left ||
               wgctx->cap_size.Height == window_rect.bottom - window_rect.top) {
        av_log(avctx, AV_LOG_DEBUG, "Using window rect from GetWindowRect\n");
        frame_bounds = window_rect;
    } else {
        if ((frame_bounds.top == frame_bounds.bottom || frame_bounds.left == frame_bounds.right) &&
            (window_rect.top == window_rect.bottom || window_rect.left == window_rect.right))
        {
            av_log(avctx, AV_LOG_ERROR, "No valid window rect found\n");
            return AVERROR_EXTERNAL;
        }
        av_log(avctx, AV_LOG_VERBOSE, "Failed to get valid window rect, client area may be inaccurate\n");
        return 0;
    }

    wgctx->client_area_offsets.left = FFMAX(client_rect.left - frame_bounds.left, 0);
    wgctx->client_area_offsets.top = FFMAX(client_rect.top - frame_bounds.top, 0);
    wgctx->client_area_offsets.right = FFMAX(frame_bounds.right - client_rect.right, 0);
    wgctx->client_area_offsets.bottom = FFMAX(frame_bounds.bottom - client_rect.bottom, 0);

    av_log(avctx, AV_LOG_DEBUG, "Client area offsets: left=%ld top=%ld right=%ld bottom=%ld\n",
           wgctx->client_area_offsets.left, wgctx->client_area_offsets.top,
           wgctx->client_area_offsets.right, wgctx->client_area_offsets.bottom);

    return 0;
}

static int wgc_setup_gfxcapture_session(AVFilterContext *avctx)
{
    GfxCaptureContext *cctx = CCTX(avctx->priv);
    GfxCaptureContextCpp *ctx = cctx->ctx;
    std::unique_ptr<GfxCaptureContextWgc> &wgctx = ctx->wgc;
    int ret;

    ComPtr<IDirect3D11CaptureFramePoolStatics2> frame_pool_statics;
    ComPtr<ID3D11Device> d3d11_device = ctx->device_hwctx->device;
    ComPtr<ID3D10Multithread> d3d10_multithread;
    ComPtr<IDXGIDevice> dxgi_device;
    ComPtr<IGraphicsCaptureSession2> session2;
    ComPtr<IGraphicsCaptureSession3> session3;
    ComPtr<IGraphicsCaptureSession5> session5;

    DirectXPixelFormat fmt = DirectXPixelFormat::DirectXPixelFormat_B8G8R8A8UIntNormalized;
    if (cctx->out_fmt != AV_PIX_FMT_BGRA)
        fmt = DirectXPixelFormat::DirectXPixelFormat_R16G16B16A16Float;

    CHECK_HR_RET(wgctx->capture_item->get_Size(&wgctx->cap_size));
    ret = wgc_calculate_client_area(avctx);
    if (ret < 0)
        return ret;

    CHECK_HR_RET(d3d11_device.As(&d3d10_multithread));
    d3d10_multithread->SetMultithreadProtected(TRUE);

    CHECK_HR_RET(d3d11_device.As(&dxgi_device));
    CHECK_HR_RET(ctx->fn.CreateDirect3D11DeviceFromDXGIDevice(dxgi_device.Get(), &wgctx->d3d_device));

    CHECK_HR_RET(get_activation_factory<IDirect3D11CaptureFramePoolStatics2>(ctx, RuntimeClass_Windows_Graphics_Capture_Direct3D11CaptureFramePool, &frame_pool_statics));
    CHECK_HR_RET(frame_pool_statics->CreateFreeThreaded(wgctx->d3d_device.Get(), fmt, CAPTURE_POOL_SIZE, wgctx->cap_size, &wgctx->frame_pool));
    CHECK_HR_RET(wgctx->frame_pool->CreateCaptureSession(wgctx->capture_item.Get(), &wgctx->capture_session));

    if (SUCCEEDED(wgctx->capture_session.As(&session2))) {
        if (FAILED(session2->put_IsCursorCaptureEnabled(cctx->capture_cursor))) {
            av_log(avctx, AV_LOG_WARNING, "Failed setting cursor capture mode\n");
        }
    } else {
        av_log(avctx, AV_LOG_WARNING, "Cursor capture unavailable\n");
    }

    if (SUCCEEDED(wgctx->capture_session.As(&session3))) {
        // this one is weird, it can return failure but still work
        if (FAILED(session3->put_IsBorderRequired(cctx->display_border))) {
            av_log(avctx, AV_LOG_WARNING, "Failed setting border drawing mode\n");
        }
    } else {
        av_log(avctx, AV_LOG_WARNING, "Disabling border drawing unavailable\n");
    }

    if (SUCCEEDED(wgctx->capture_session.As(&session5))) {
        TimeSpan ivl = { av_rescale_q(1, av_inv_q(cctx->frame_rate), AVRational{1, TIMESPAN_RES}) };
        if (FAILED(session5->put_MinUpdateInterval(ivl))) {
            av_log(avctx, AV_LOG_WARNING, "Failed setting minimum update interval, framerate may be limited\n");
        }
    } else {
        av_log(avctx, AV_LOG_WARNING, "Setting minimum update interval unavailable, framerate may be limited\n");
    }

    wgctx->window_closed = 0;

    CHECK_HR_RET(wgctx->capture_item->add_Closed(
        create_cb_handler<ITypedEventHandler<GraphicsCaptureItem*,IInspectable*>, IGraphicsCaptureItem*, IInspectable*>(
            [avctx, ctx](auto, auto) {
                av_log(avctx, AV_LOG_INFO, "Capture item closed\n");
                wgc_closed_handler(ctx->wgc);
                return S_OK;
            }).Get(), &wgctx->closed_token));

    CHECK_HR_RET(wgctx->frame_pool->add_FrameArrived(
        create_cb_handler<ITypedEventHandler<Direct3D11CaptureFramePool*,IInspectable*>, IDirect3D11CaptureFramePool*, IInspectable*>(
            [avctx, ctx](auto, auto) {
                av_log(avctx, AV_LOG_TRACE, "Frame arrived\n");
                wgc_frame_arrived_handler(ctx->wgc);
                return S_OK;
            }).Get(), &wgctx->frame_arrived_token));

    return 0;
}

static int wgc_setup_gfxcapture_capture(AVFilterContext *avctx)
{
    GfxCaptureContext *cctx = CCTX(avctx->priv);
    GfxCaptureContextCpp *ctx = cctx->ctx;
    std::unique_ptr<GfxCaptureContextWgc> &wgctx = ctx->wgc;
    HRESULT hr;
    int ret;

    ComPtr<IGraphicsCaptureItemInterop> capture_item_interop;
    CHECK_HR_RET(get_activation_factory<IGraphicsCaptureItemInterop>(ctx, RuntimeClass_Windows_Graphics_Capture_GraphicsCaptureItem, &capture_item_interop));

    if (ctx->capture_hmonitor) {
        hr = capture_item_interop->CreateForMonitor(ctx->capture_hmonitor, IID_PPV_ARGS(&wgctx->capture_item));
        if (FAILED(hr)) {
            av_log(avctx, AV_LOG_ERROR, "Failed to setup graphics capture for monitor (0x%08lX)\n", hr);
            return AVERROR_EXTERNAL;
        }
    } else if (ctx->capture_hwnd) {
        hr = capture_item_interop->CreateForWindow(ctx->capture_hwnd, IID_PPV_ARGS(&wgctx->capture_item));
        if (FAILED(hr)) {
            av_log(avctx, AV_LOG_ERROR, "Failed to setup graphics capture for window (0x%08lX)\n", hr);
            return AVERROR_EXTERNAL;
        }
    }

    ret = wgc_setup_gfxcapture_session(avctx);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to setup graphics capture pool\n");
        return ret;
    }

    hr = ctx->wgc->capture_session->StartCapture();
    if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR, "Failed to start graphics capture session (0x%08lX)\n", hr);
        return AVERROR_EXTERNAL;
    }

    return 0;
}

static int wgc_try_get_next_frame(AVFilterContext *avctx, ComPtr<IDirect3D11CaptureFrame> &capture_frame)
{
    GfxCaptureContext *cctx = CCTX(avctx->priv);
    GfxCaptureContextCpp *ctx = cctx->ctx;
    std::unique_ptr<GfxCaptureContextWgc> &wgctx = ctx->wgc;

    ComPtr<IDirect3DSurface> capture_surface;
    ComPtr<IDirect3DDxgiInterfaceAccess> dxgi_interface_access;
    ComPtr<ID3D11Texture2D> frame_texture;
    SizeInt32 frame_size = { 0, 0 };

    CHECK_HR_RET(wgctx->frame_pool->TryGetNextFrame(&capture_frame));
    if (!capture_frame)
        return AVERROR(EAGAIN);

    CHECK_HR_RET(capture_frame->get_ContentSize(&frame_size));
    if (frame_size.Width != wgctx->cap_size.Width || frame_size.Height != wgctx->cap_size.Height) {
        av_log(avctx, AV_LOG_VERBOSE, "Capture size changed to %dx%d\n", frame_size.Width, frame_size.Height);

        DirectXPixelFormat fmt = DirectXPixelFormat::DirectXPixelFormat_B8G8R8A8UIntNormalized;
        if (cctx->out_fmt != AV_PIX_FMT_BGRA)
            fmt = DirectXPixelFormat::DirectXPixelFormat_R16G16B16A16Float;

        CHECK_HR_RET(wgctx->frame_pool->Recreate(wgctx->d3d_device.Get(), fmt, CAPTURE_POOL_SIZE, frame_size));
        wgctx->cap_size = frame_size;

        int ret = wgc_calculate_client_area(avctx);
        if (ret < 0)
            return ret;
    }

    return 0;
}

static int wgc_setup_winrt(AVFilterContext *avctx)
{
    GfxCaptureContext *cctx = CCTX(avctx->priv);
    GfxCaptureContextCpp *ctx = cctx->ctx;
    std::unique_ptr<GfxCaptureContextWgc> &wgctx = ctx->wgc;
    MSG msg;

    // pre-create the message-queue
    PeekMessage(&msg, nullptr, 0, 0, PM_NOREMOVE);

    DispatcherQueueOptions options = { 0 };
    options.dwSize = sizeof(DispatcherQueueOptions);
    options.threadType = DISPATCHERQUEUE_THREAD_TYPE::DQTYPE_THREAD_CURRENT;
    options.apartmentType = DISPATCHERQUEUE_THREAD_APARTMENTTYPE::DQTAT_COM_NONE;

    CHECK_HR_RET(ctx->fn.CreateDispatcherQueueController(options, &wgctx->dispatcher_queue_controller));
    CHECK_HR_RET(wgctx->dispatcher_queue_controller->get_DispatcherQueue(&wgctx->dispatcher_queue));

    return 0;
}

static void wgc_thread_uninit(AVFilterContext *avctx) noexcept
{
    GfxCaptureContext *cctx = CCTX(avctx->priv);
    GfxCaptureContextCpp *ctx = cctx->ctx;

    wgc_stop_capture_session(avctx);

    ctx->wgc.reset();
    ctx->fn.RoUninitialize();
}

static int wgc_thread_init(AVFilterContext *avctx)
{
    GfxCaptureContext *cctx = CCTX(avctx->priv);
    GfxCaptureContextCpp *ctx = cctx->ctx;
    HRESULT hr;
    int ret;

    ctx->wgc = std::make_unique<GfxCaptureContextWgc>();

    ctx->fn.SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    hr = ctx->fn.RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR, "Failed to initialize WinRT\n");
        ctx->wgc.reset();
        return AVERROR_EXTERNAL;
    }

    ret = wgc_setup_winrt(avctx);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to setup WinRT\n");
        goto fail;
    }

    ret = wgc_setup_gfxcapture_capture(avctx);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to setup graphics capture\n");
        goto fail;
    }

    return 0;

fail:
    wgc_thread_uninit(avctx);
    return ret;
}

static int wgc_thread_worker(AVFilterContext *avctx)
{
    GfxCaptureContext *cctx = CCTX(avctx->priv);
    GfxCaptureContextCpp *ctx = cctx->ctx;
    std::unique_ptr<GfxCaptureContextWgc> &wgctx = ctx->wgc;
    ComPtr<IAsyncAction> async;
    MSG msg;

    av_log(avctx, AV_LOG_DEBUG, "Starting message loop\n");

    while (BOOL res = GetMessage(&msg, NULL, 0, 0)) {
        if (res == -1) {
            av_log(avctx, AV_LOG_ERROR, "Failed to get message\n");
            return AVERROR(EIO);
        }

        if (!msg.hwnd && msg.message == WM_WGC_THREAD_SHUTDOWN) {
            av_log(avctx, AV_LOG_DEBUG, "Initializing WGC thread shutdown\n");

            wgc_stop_capture_session(avctx);

            if (FAILED(wgctx->dispatcher_queue_controller->ShutdownQueueAsync(&async))) {
                av_log(avctx, AV_LOG_ERROR, "Failed to shutdown dispatcher queue\n");
                return AVERROR_EXTERNAL;
            }
            async->put_Completed(create_cb_handler<IAsyncActionCompletedHandler, IAsyncAction*, AsyncStatus>(
                [avctx, ctx](auto, auto status) {
                    PostThreadMessage(ctx->wgc_thread_id, WM_QUIT, 0, 0);
                    av_log(avctx, AV_LOG_DEBUG, "WGC thread async shutdown completed: %d\n", (int)status);
                    return S_OK;
                }).Get());
            continue;
        }

        av_log(avctx, AV_LOG_TRACE, "Got message: %u\n", msg.message);

        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (!async) {
        av_log(avctx, AV_LOG_ERROR, "WGC Thread message loop ended without proper shutdown\n");
        return AVERROR_EXTERNAL;
    }

    av_log(avctx, AV_LOG_DEBUG, "Message loop ended\n");

    return msg.wParam;
}

static void wgc_thread_entry(AVFilterContext *avctx) noexcept
{
    GfxCaptureContext *cctx = CCTX(avctx->priv);
    GfxCaptureContextCpp *ctx = cctx->ctx;

    {
        static const wchar_t name_prefix[] = L"wgc_winrt@0x";
        wchar_t thread_name[FF_ARRAY_ELEMS(name_prefix) + sizeof(void*) * 2] = { 0 };
        swprintf(thread_name, FF_ARRAY_ELEMS(thread_name), L"%ls%" PRIxPTR, name_prefix, (uintptr_t)avctx);
        ctx->fn.SetThreadDescription(GetCurrentThread(), thread_name);

        std::lock_guard init_lock(ctx->wgc_thread_init_mutex);
        ctx->wgc_thread_id = GetCurrentThreadId();

        try {
            ctx->wgc_thread_init_res = wgc_thread_init(avctx);
        } catch (const std::bad_alloc &) {
            ctx->wgc_thread_init_res = AVERROR(ENOMEM);
        } catch (const std::exception &e) {
            av_log(avctx, AV_LOG_ERROR, "unhandled exception in WGC thread init: %s\n", e.what());
            ctx->wgc_thread_init_res = AVERROR_BUG;
        } catch (...) {
            av_log(avctx, AV_LOG_ERROR, "Unhandled exception in WGC thread init\n");
            ctx->wgc_thread_init_res = AVERROR_BUG;
        }

        ctx->wgc_thread_init_cond.notify_all();
        if (ctx->wgc_thread_init_res < 0) {
            ctx->wgc_thread_res = ctx->wgc_thread_init_res;
            return;
        }
    }

    int ret;

    try {
        ret = wgc_thread_worker(avctx);
    } catch (const std::bad_alloc &) {
        ret = AVERROR(ENOMEM);
    } catch (const std::exception &e) {
        av_log(avctx, AV_LOG_ERROR, "unhandled exception in WGC thread worker: %s\n", e.what());
        ret = AVERROR_BUG;
    } catch (...) {
        av_log(avctx, AV_LOG_ERROR, "Unhandled exception in WGC thread worker\n");
        ret = AVERROR_BUG;
    }

    std::lock_guard uninit_lock(ctx->wgc_thread_uninit_mutex);
    wgc_thread_uninit(avctx);

    ctx->wgc_thread_res = ret;
}

/***********************************
 * WGC Thread Management Functions *
 ***********************************/

static int stop_wgc_thread(AVFilterContext *avctx)
{
    GfxCaptureContext *cctx = CCTX(avctx->priv);
    GfxCaptureContextCpp *ctx = cctx->ctx;
    int ret = 0;

    if (ctx->wgc_thread.joinable()) {
        if (ctx->wgc_thread_id && !PostThreadMessage(ctx->wgc_thread_id, WM_WGC_THREAD_SHUTDOWN, 0, 0))
            av_log(avctx, AV_LOG_ERROR, "Failed to post shutdown message to WGC thread\n");

        ctx->wgc_thread.join();
        ret = ctx->wgc_thread_res;

        ctx->wgc_thread_id = 0;
    }

    return ret;
}

static int start_wgc_thread(AVFilterContext *avctx)
{
    GfxCaptureContext *cctx = CCTX(avctx->priv);
    GfxCaptureContextCpp *ctx = cctx->ctx;

    if (ctx->wgc_thread.joinable() || ctx->wgc_thread_id) {
        av_log(avctx, AV_LOG_ERROR, "Double-creation of WGC thread\n");
        return AVERROR_BUG;
    }

    std::unique_lock wgc_lock(ctx->wgc_thread_init_mutex);
    ctx->wgc_thread_init_res = INT_MAX;

    try {
        ctx->wgc_thread = std::thread(wgc_thread_entry, avctx);
    } catch (const std::system_error &e) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create WGC thread: %s\n", e.what());
        return AVERROR_EXTERNAL;
    }

    if (!ctx->wgc_thread_init_cond.wait_for(wgc_lock, std::chrono::seconds(1), [&]() {
        return ctx->wgc_thread_init_res != INT_MAX;
    })) {
        av_log(avctx, AV_LOG_ERROR, "WGC thread init timed out\n");
        return AVERROR(ETIMEDOUT);
    }

    return ctx->wgc_thread_init_res;
}

template <typename F>
static int run_on_wgc_thread(AVFilterContext *avctx, F &&cb)
{
    GfxCaptureContext *cctx = CCTX(avctx->priv);
    GfxCaptureContextCpp *ctx = cctx->ctx;
    std::unique_ptr<GfxCaptureContextWgc> &wgctx = ctx->wgc;

    std::lock_guard uninit_lock(ctx->wgc_thread_uninit_mutex);
    if (!wgctx) {
        av_log(avctx, AV_LOG_ERROR, "WGC thread not initialized\n");
        return AVERROR(ENOSYS);
    }

    struct CBData {
        std::mutex mutex;
        std::condition_variable cond;
        bool done;
        bool cancel;
        int ret;
    };
    auto cbdata = ctx->wgc_thread_cb_data ?
                  std::static_pointer_cast<CBData>(ctx->wgc_thread_cb_data) :
                  std::make_shared<CBData>();
    ctx->wgc_thread_cb_data = cbdata;

    cbdata->done = cbdata->cancel = false;
    cbdata->ret = AVERROR_BUG;

    boolean res = 0;
    CHECK_HR_RET(wgctx->dispatcher_queue->TryEnqueue(
        create_cb_handler<IDispatcherQueueHandler>(
            [cb = std::forward<F>(cb), cbdata]() {
                {
                    std::lock_guard lock(cbdata->mutex);
                    if (cbdata->cancel)
                        return S_OK;

                    try {
                        cbdata->ret = cb();
                    } catch (const std::bad_alloc &) {
                        cbdata->ret = AVERROR(ENOMEM);
                    } catch (...) {
                        cbdata->ret = AVERROR_BUG;
                    }

                    cbdata->done = true;
                }

                cbdata->cond.notify_one();
                return S_OK;
            }).Get(), &res));
    if (!res) {
        av_log(avctx, AV_LOG_ERROR, "Failed to enqueue WGC thread callback\n");
        return AVERROR_EXTERNAL;
    }

    std::unique_lock cblock(cbdata->mutex);
    if (!cbdata->cond.wait_for(cblock, std::chrono::seconds(1), [&]() { return cbdata->done; })) {
        cbdata->cancel = true;
        av_log(avctx, AV_LOG_ERROR, "WGC thread callback timed out\n");
        return AVERROR(ETIMEDOUT);
    }

    return cbdata->ret;
}

/*******************************
 * Standard AVFilter functions *
 *******************************/

static int build_regex(AVFilterContext *avctx, const char *pattern, std::regex *out)
{
    if (!pattern)
        return 0;

    std::string pat(pattern);

    auto flags = std::regex::ECMAScript | std::regex::optimize;
    if (pat.rfind("(?i)", 0) == 0 || pat.rfind("(?I)", 0) == 0) {
        pat.erase(0, 4);
        flags |= std::regex::icase;
    } else if(pat.rfind("(?c)", 0) == 0 || pat.rfind("(?C)", 0) == 0) {
        pat.erase(0, 4);
    }

    try {
        *out = std::regex(pat, flags);
    } catch (const std::regex_error &e) {
        av_log(avctx, AV_LOG_ERROR, "Failed to compile regex '%s': %s\n", pat.c_str(), e.what());
        return AVERROR(EINVAL);
    }

    av_log(avctx, AV_LOG_DEBUG, "Built regex: %s\n", pattern);

    return 0;
}

static int wstring_to_utf8(const wchar_t *in, std::string *out)
{
    int utf8size = WideCharToMultiByte(CP_UTF8, 0, in, -1, nullptr, 0, nullptr, nullptr);
    if (utf8size <= 0)
        return AVERROR(EINVAL);

    // over-writing std::string by one is valid in C++17 according to 27.4.3.6 if and only if it's overwritten with 0
    out->resize(utf8size - 1);

    if (WideCharToMultiByte(CP_UTF8, 0, in, -1, out->data(), utf8size, nullptr, nullptr) != utf8size)
        return AVERROR_EXTERNAL;

    return 0;
}

static int get_window_exe_name(HWND hwnd, std::string *out)
{
    out->clear();

    DWORD pid = 0;
    if (!GetWindowThreadProcessId(hwnd, &pid))
        return AVERROR(ENOENT);

    handle_ptr_t proc(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    if (!proc)
        return AVERROR(EACCES);

    std::wstring image_name;
    DWORD image_name_size = 512;

    for (;;) {
        DWORD len = image_name_size;
        image_name.resize(len);
        if (QueryFullProcessImageNameW(proc.get(), 0, image_name.data(), &len)) {
            image_name.resize(len);
            break;
        }
        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
            image_name_size *= 2;
            continue;
        }
        return AVERROR_EXTERNAL;
    }

    if (image_name.empty())
        return AVERROR_EXTERNAL;

    const wchar_t *base = image_name.c_str();
    size_t pos = image_name.find_last_of(L"\\/");
    if (pos != std::string::npos)
        base += pos + 1;

    return wstring_to_utf8(base, out);
}

static int find_capture_source(AVFilterContext *avctx)
{
    GfxCaptureContext *cctx = CCTX(avctx->priv);
    GfxCaptureContextCpp *ctx = cctx->ctx;
    int cur_idx = 0;

    ctx->capture_hwnd = NULL;
    ctx->capture_hmonitor = NULL;

    if (cctx->user_hmonitor) {
        ctx->capture_hmonitor = (HMONITOR)(uintptr_t)cctx->user_hmonitor;
        return 0;
    } else if (cctx->user_hwnd) {
        ctx->capture_hwnd = (HWND)(uintptr_t)cctx->user_hwnd;
        return 0;
    } else if (cctx->monitor_idx >= 0) {
        auto cb = make_win32_callback([&](HMONITOR hmonitor, HDC, LPRECT) {
            if (cur_idx++ == cctx->monitor_idx) {
                av_log(avctx, AV_LOG_DEBUG, "Found capture monitor: %d\n", cctx->monitor_idx);
                ctx->capture_hmonitor = hmonitor;
                return FALSE;
            }
            return TRUE;
        });
        if (EnumDisplayMonitors(NULL, NULL, cb->proc, cb->lparam) || !ctx->capture_hmonitor)
            return AVERROR(ENOENT);
        return 0;
    } else if (cctx->window_text || cctx->window_class || cctx->window_exe) {
        std::regex text_regex;
        if (build_regex(avctx, cctx->window_text, &text_regex) < 0)
            return AVERROR(EINVAL);

        std::regex class_regex;
        if (build_regex(avctx, cctx->window_class, &class_regex) < 0)
            return AVERROR(EINVAL);

        std::regex exe_regex;
        if (build_regex(avctx, cctx->window_exe, &exe_regex) < 0)
            return AVERROR(EINVAL);

        std::string window_text;
        std::wstring window_text_w;
        std::string window_class;
        std::wstring window_class_w;
        std::string window_exe;
        auto cb = make_win32_callback([&](HWND hwnd) {
            RECT r = { 0 };
            if (!GetWindowRect(hwnd, &r) || r.right <= r.left || r.bottom <= r.top || !IsWindowVisible(hwnd))
                return TRUE;

            window_text_w.resize(GetWindowTextLengthW(hwnd) + 1);
            int len = GetWindowTextW(hwnd, window_text_w.data(), (int)window_text_w.size());
            if (len >= 0) {
                window_text_w.resize(len);
                if (wstring_to_utf8(window_text_w.c_str(), &window_text) < 0)
                    window_text.clear();
            } else {
                window_text.clear();
            }

            window_class_w.resize(256);
            len = GetClassNameW(hwnd, window_class_w.data(), (int)window_class_w.size());
            if (len >= 0) {
                window_class_w.resize(len);
                if (wstring_to_utf8(window_class_w.c_str(), &window_class) < 0)
                    window_class.clear();
            } else {
                window_class.clear();
            }

            get_window_exe_name(hwnd, &window_exe);

            av_log(avctx, AV_LOG_TRACE, "Checking window: hwnd=%p text=%s class=%s exe=%s\n",
                   hwnd, window_text.c_str(), window_class.c_str(), window_exe.c_str());

            if (cctx->window_text) {
                if (window_text.empty() || !std::regex_search(window_text, text_regex))
                    return TRUE;
            }

            if (cctx->window_class) {
                if (window_class.empty() || !std::regex_search(window_class, class_regex))
                    return TRUE;
            }

            if (cctx->window_exe) {
                if (window_exe.empty() || !std::regex_search(window_exe, exe_regex))
                    return TRUE;
            }

            av_log(avctx, AV_LOG_VERBOSE, "Found capture window: %s (Class: %s, Exe: %s)\n",
                   window_text.c_str(), window_class.c_str(), window_exe.c_str());
            ctx->capture_hwnd = hwnd;
            return FALSE;
        });
        if (EnumWindows(cb->proc, cb->lparam) || !ctx->capture_hwnd)
            return AVERROR(ENOENT);

        if (cctx->monitor_idx == GFX_MONITOR_IDX_WINDOW) {
            ctx->capture_hmonitor = MonitorFromWindow(ctx->capture_hwnd, MONITOR_DEFAULTTONEAREST);
            ctx->capture_hwnd = NULL;
            if (!ctx->capture_hmonitor) {
                av_log(avctx, AV_LOG_ERROR, "Failed to get monitor for capture window\n");
                return AVERROR(ENOENT);
            }
        }

        return 0;
    }

    av_log(avctx, AV_LOG_ERROR, "No capture source specified\n");
    return AVERROR(EINVAL);
}

static av_cold void gfxcapture_uninit(AVFilterContext *avctx)
{
    GfxCaptureContext *cctx = CCTX(avctx->priv);
    GfxCaptureContextCpp *ctx = cctx->ctx;

    if (!ctx)
        return;

    stop_wgc_thread(avctx);

    ctx->d3d.reset();

    av_buffer_unref(&ctx->frames_ref);
    av_buffer_unref(&ctx->device_ref);

    delete ctx;
    cctx->ctx = nullptr;
}

template<typename T>
static av_cold void GetProcAddressTyped(const hmodule_ptr_t &hModule, LPCSTR lpProcName, T *out) {
    *out = reinterpret_cast<T>(GetProcAddress(hModule.get(), lpProcName));
}

static av_cold int load_functions(AVFilterContext *avctx)
{
    GfxCaptureContext *cctx = CCTX(avctx->priv);
    GfxCaptureContextCpp *ctx = cctx->ctx;

#define LOAD_DLL(handle, name) \
    handle = hmodule_ptr_t(LoadLibraryExW(L##name, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32)); \
    if (!handle) { \
        av_log(avctx, AV_LOG_ERROR, "Failed opening " #name "\n"); \
        return AVERROR(ENOSYS); \
    }

#define LOAD_FUNC(handle, name) \
    GetProcAddressTyped(handle, #name, &ctx->fn.name); \
    if (!ctx->fn.name) { \
        av_log(avctx, AV_LOG_ERROR, "Failed loading " #name "\n"); \
        return AVERROR(ENOSYS); \
    }

    // this handle is not used anywhere, but letting it get auto-freed during RoUninit causes crashes
    LOAD_DLL(ctx->fn.graphicscapture_handle, "graphicscapture.dll");

    LOAD_DLL(ctx->fn.combase_handle, "combase.dll");
    LOAD_DLL(ctx->fn.dwmapi_handle, "dwmapi.dll");
    LOAD_DLL(ctx->fn.d3d11_handle, "d3d11.dll");
    LOAD_DLL(ctx->fn.coremsg_handle, "coremessaging.dll");
    LOAD_DLL(ctx->fn.user32_handle, "user32.dll");
    LOAD_DLL(ctx->fn.kernel32_handle, "kernel32.dll");
    LOAD_DLL(ctx->fn.d3dcompiler_handle, "d3dcompiler_47.dll");

    LOAD_FUNC(ctx->fn.combase_handle, RoInitialize);
    LOAD_FUNC(ctx->fn.combase_handle, RoUninitialize);
    LOAD_FUNC(ctx->fn.combase_handle, RoGetActivationFactory);
    LOAD_FUNC(ctx->fn.combase_handle, WindowsCreateStringReference);

    LOAD_FUNC(ctx->fn.dwmapi_handle, DwmGetWindowAttribute);

    LOAD_FUNC(ctx->fn.d3d11_handle, CreateDirect3D11DeviceFromDXGIDevice);

    LOAD_FUNC(ctx->fn.coremsg_handle, CreateDispatcherQueueController);

    LOAD_FUNC(ctx->fn.user32_handle, SetThreadDpiAwarenessContext);

    LOAD_FUNC(ctx->fn.kernel32_handle, SetThreadDescription);

    LOAD_FUNC(ctx->fn.d3dcompiler_handle, D3DCompile);

#undef LOAD_FUNC
#undef LOAD_DLL
    return 0;
}

static av_cold int gfxcapture_init(AVFilterContext *avctx)
{
    GfxCaptureContext *cctx = CCTX(avctx->priv);
    int ret = 0;

    GfxCaptureContextCpp *ctx = cctx->ctx = new GfxCaptureContextCpp();
    ctx->d3d = std::make_unique<GfxCaptureContextD3D>();

    ret = load_functions(avctx);
    if (ret < 0) {
        ctx->fn.RoUninitialize = nullptr;
        goto fail;
    }

    return 0;

fail:
    gfxcapture_uninit(avctx);
    return ret;
}

static int init_hwframes_ctx(AVFilterContext *avctx)
{
    GfxCaptureContext *cctx = CCTX(avctx->priv);
    GfxCaptureContextCpp *ctx = cctx->ctx;
    int ret = 0;

    ctx->frames_ref = av_hwframe_ctx_alloc(ctx->device_ref);
    if (!ctx->frames_ref)
        return AVERROR(ENOMEM);
    ctx->frames_ctx = (AVHWFramesContext*)ctx->frames_ref->data;
    ctx->frames_hwctx = (AVD3D11VAFramesContext*)ctx->frames_ctx->hwctx;

    ctx->frames_ctx->format    = AV_PIX_FMT_D3D11;
    ctx->frames_ctx->width     = cctx->canvas_width;
    ctx->frames_ctx->height    = cctx->canvas_height;
    ctx->frames_ctx->sw_format = (AVPixelFormat)cctx->out_fmt;
    if (avctx->extra_hw_frames > 0)
        ctx->frames_ctx->initial_pool_size = 8 + avctx->extra_hw_frames;

    ctx->frames_hwctx->BindFlags = D3D11_BIND_RENDER_TARGET;

    ret = av_hwframe_ctx_init(ctx->frames_ref);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to initialise hardware frames context: %d.\n", ret);
        goto fail;
    }

    return 0;
fail:
    av_buffer_unref(&ctx->frames_ref);
    return ret;
}

static int setup_gfxcapture_capture(AVFilterContext *avctx)
{
    GfxCaptureContext *cctx = CCTX(avctx->priv);
    GfxCaptureContextCpp *ctx = cctx->ctx;
    std::unique_ptr<GfxCaptureContextWgc> &wgctx = ctx->wgc;
    int ret = 0;

    stop_wgc_thread(avctx);

    ret = find_capture_source(avctx);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to find capture source\n");
        return ret;
    }

    ret = start_wgc_thread(avctx);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to start WGC thread\n");
        return ret;
    }

    int cap_w = wgctx->cap_size.Width - cctx->crop_left - cctx->crop_right;
    int cap_h = wgctx->cap_size.Height - cctx->crop_top - cctx->crop_bottom;

    if (!cctx->capture_border) {
        cap_w -= wgctx->client_area_offsets.left + wgctx->client_area_offsets.right;
        cap_h -= wgctx->client_area_offsets.top + wgctx->client_area_offsets.bottom;
    }

    if (cctx->canvas_width == 0)
        cctx->canvas_width = cap_w;
    else if (cctx->canvas_width < 0)
        cctx->canvas_width = (cap_w / cctx->canvas_width) * cctx->canvas_width;

    if (cctx->canvas_height == 0)
        cctx->canvas_height = cap_h;
    else if (cctx->canvas_height < 0)
        cctx->canvas_height = (cap_h / cctx->canvas_height) * cctx->canvas_height;

    return 0;
}

static int prepare_render_resources(AVFilterContext *avctx)
{
    GfxCaptureContext *cctx = CCTX(avctx->priv);
    GfxCaptureContextCpp *ctx = cctx->ctx;
    std::unique_ptr<GfxCaptureContextD3D> &d3dctx = ctx->d3d;
    HRESULT hr;

    ComPtr<ID3DBlob> vs_blob, ps_blob, err_blob;
    CD3D11_SAMPLER_DESC sampler_desc(CD3D11_DEFAULT{});
    UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;

    hr = ctx->fn.D3DCompile(render_shader_src, sizeof(render_shader_src) - 1, NULL, NULL, NULL, "main_vs", "vs_4_0", flags, 0, &vs_blob, &err_blob);
    if (FAILED(hr)) {
        if (err_blob) {
            av_log(avctx, AV_LOG_ERROR, "Failed compiling vertex shader: %.*s\n", (int)err_blob->GetBufferSize(), (char*)err_blob->GetBufferPointer());
        } else {
            av_log(avctx, AV_LOG_ERROR, "Failed compiling vertex shader: 0x%08lX\n", hr);
        }
        return AVERROR_EXTERNAL;
    }

    const char *ps_entry = "main_ps_bicubic";
    if (cctx->resize_mode == GFX_RESIZE_CROP || cctx->scale_mode == GFX_SCALE_POINT) {
        ps_entry = "main_ps";
        sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    }

    hr = ctx->fn.D3DCompile(render_shader_src, sizeof(render_shader_src) - 1, NULL, NULL, NULL, ps_entry, "ps_4_0", flags, 0, &ps_blob, &err_blob);
    if (FAILED(hr)) {
        if (err_blob) {
            av_log(avctx, AV_LOG_ERROR, "Failed compiling pixel shader: %.*s\n", (int)err_blob->GetBufferSize(), (char*)err_blob->GetBufferPointer());
        } else {
            av_log(avctx, AV_LOG_ERROR, "Failed compiling pixel shader: 0x%08lX\n", hr);
        }
        return AVERROR_EXTERNAL;
    }

    CHECK_HR_RET(ctx->device_hwctx->device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), NULL, &d3dctx->vertex_shader));
    CHECK_HR_RET(ctx->device_hwctx->device->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), NULL, &d3dctx->pixel_shader));

    CHECK_HR_RET(ctx->device_hwctx->device->CreateSamplerState(&sampler_desc, &d3dctx->sampler_state));

    D3D11_BUFFER_DESC cb_desc = { 0 };
    cb_desc.ByteWidth = 48;
    cb_desc.Usage = D3D11_USAGE_DYNAMIC;
    cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    CHECK_HR_RET(ctx->device_hwctx->device->CreateBuffer(&cb_desc, NULL, &d3dctx->shader_cb));

    CHECK_HR_RET(ctx->device_hwctx->device->CreateDeferredContext(0, &d3dctx->deferred_ctx));

    return 0;
}

static int gfxcapture_config_props(AVFilterLink *outlink)
{
    AVFilterContext *avctx = outlink->src;
    GfxCaptureContext *cctx = CCTX(avctx->priv);
    GfxCaptureContextCpp *ctx = cctx->ctx;

    FilterLink *link = ff_filter_link(outlink);
    int ret;

    if (avctx->hw_device_ctx) {
        ctx->device_ctx = (AVHWDeviceContext*)avctx->hw_device_ctx->data;

        if (ctx->device_ctx->type != AV_HWDEVICE_TYPE_D3D11VA) {
            av_log(avctx, AV_LOG_ERROR, "Non-D3D11VA input hw_device_ctx\n");
            return AVERROR(EINVAL);
        }

        ctx->device_ref = av_buffer_ref(avctx->hw_device_ctx);
        if (!ctx->device_ref)
            return AVERROR(ENOMEM);

        av_log(avctx, AV_LOG_VERBOSE, "Using provided hw_device_ctx\n");
    } else {
        ret = av_hwdevice_ctx_create(&ctx->device_ref, AV_HWDEVICE_TYPE_D3D11VA, NULL, NULL, 0);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "Failed to create D3D11VA device.\n");
            return ret;
        }

        ctx->device_ctx = (AVHWDeviceContext*)ctx->device_ref->data;

        av_log(avctx, AV_LOG_VERBOSE, "Created internal hw_device_ctx\n");
    }

    ctx->device_hwctx = (AVD3D11VADeviceContext*)ctx->device_ctx->hwctx;

    ret = prepare_render_resources(avctx);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to prepare render resources\n");
        return ret;
    }

    ret = setup_gfxcapture_capture(avctx);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to setup graphics capture\n");
        return ret;
    }

    ret = init_hwframes_ctx(avctx);
    if (ret < 0)
        return ret;

    link->hw_frames_ctx = av_buffer_ref(ctx->frames_ref);
    if (!link->hw_frames_ctx)
        return AVERROR(ENOMEM);

    std::lock_guard wgc_lock(ctx->wgc_thread_uninit_mutex);
    if (!ctx->wgc) {
        av_log(avctx, AV_LOG_ERROR, "WGC thread died prematurely\n");
        return AVERROR(ENOSYS);
    }

    outlink->w = ctx->frames_ctx->width;
    outlink->h = ctx->frames_ctx->height;
    outlink->time_base = AVRational{1, TIMESPAN_RES};
    outlink->alpha_mode = cctx->premult_alpha ? AVALPHA_MODE_PREMULTIPLIED : AVALPHA_MODE_STRAIGHT;
    link->frame_rate = cctx->frame_rate;

    av_log(avctx, AV_LOG_DEBUG, "Capture setup with res %dx%d\n", outlink->w, outlink->h);

    return 0;
}

static int render_capture_to_frame(AVFilterContext *avctx, AVFrame *frame, const ComPtr<ID3D11Texture2D> &src_tex)
{
    GfxCaptureContext *cctx = CCTX(avctx->priv);
    GfxCaptureContextCpp *ctx = cctx->ctx;
    std::unique_ptr<GfxCaptureContextD3D> &d3dctx = ctx->d3d;
    std::unique_ptr<GfxCaptureContextWgc> &wgctx = ctx->wgc;

    ID3D11Device *dev = ctx->device_hwctx->device;
    ID3D11DeviceContext *dev_ctx = ctx->device_hwctx->device_context;
    ComPtr<ID3D11DeviceContext> &def_ctx = d3dctx->deferred_ctx;

    D3D11_TEXTURE2D_DESC dst_tex_desc;
    reinterpret_cast<ID3D11Texture2D*>(frame->data[0])->GetDesc(&dst_tex_desc);

    D3D11_TEXTURE2D_DESC src_tex_desc;
    src_tex->GetDesc(&src_tex_desc);

    D3D11_RENDER_TARGET_VIEW_DESC target_desc = {};
    target_desc.Format = dst_tex_desc.Format;

    if (dst_tex_desc.ArraySize > 1) {
        target_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
        target_desc.Texture2DArray.ArraySize = 1;
        target_desc.Texture2DArray.FirstArraySlice = (uintptr_t)frame->data[1];
        target_desc.Texture2DArray.MipSlice = 0;
    } else {
        target_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        target_desc.Texture2D.MipSlice = 0;
    }

    ComPtr<ID3D11RenderTargetView> rtv;
    CHECK_HR_RET(dev->CreateRenderTargetView(
        reinterpret_cast<ID3D11Resource*>(frame->data[0]), &target_desc, &rtv));

    ComPtr<ID3D11ShaderResourceView> srv;
    CHECK_HR_RET(dev->CreateShaderResourceView(src_tex.Get(), nullptr, &srv));

    int crop_left = cctx->crop_left;
    int crop_top = cctx->crop_top;
    int crop_right = cctx->crop_right;
    int crop_bottom = cctx->crop_bottom;

    if (!cctx->capture_border) {
        crop_left   += wgctx->client_area_offsets.left;
        crop_top    += wgctx->client_area_offsets.top;
        crop_right  += wgctx->client_area_offsets.right;
        crop_bottom += wgctx->client_area_offsets.bottom;
    }

    // Using the actual capture frame size here adjusts for jank that can happen during rapid
    // resizing of the source window. The capture frame pool is only recreated once a frame
    // of changed size came out of it, so we need to cut/pad such frames to fit.
    // Just discarding such frames can lead to visible stutter if the source window is being
    // resized continuously, so this code does its best to adjust them instead. With the risk
    // of slight clamping artifacts when enlarging rapidly.
    int cropped_w = wgctx->cap_size.Width - crop_left - crop_right;
    int cropped_h = wgctx->cap_size.Height - crop_top - crop_bottom;

    D3D11_VIEWPORT viewport = { 0 };
    viewport.MinDepth = 0.f;
    viewport.MaxDepth = 1.f;

    switch (cctx->resize_mode) {
    case GFX_RESIZE_CROP:
        viewport.Width  = (float)cropped_w;
        viewport.Height = (float)cropped_h;
        break;
    case GFX_RESIZE_SCALE:
        viewport.Width  = dst_tex_desc.Width;
        viewport.Height = dst_tex_desc.Height;
        break;
    case GFX_RESIZE_SCALE_ASPECT: {
        float scale = FFMIN(dst_tex_desc.Width / (float)cropped_w,
                            dst_tex_desc.Height / (float)cropped_h);
        viewport.Width  = cropped_w * scale;
        viewport.Height = cropped_h * scale;
        break;
    }
    default:
        av_log(avctx, AV_LOG_ERROR, "Invalid scaling mode\n");
        return AVERROR_BUG;
    };

    def_ctx->RSSetViewports(1, &viewport);

    D3D11_MAPPED_SUBRESOURCE map;
    CHECK_HR_RET(def_ctx->Map(d3dctx->shader_cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map));
    {
        float *cb_f = static_cast<float*>(map.pData);
        uint32_t *cb_u = static_cast<uint32_t*>(map.pData);
        cb_f[0] = (float)cropped_w;
        cb_f[1] = (float)cropped_h;
        cb_f[2] = viewport.Width;
        cb_f[3] = viewport.Height;
        cb_f[4] = crop_left / (float)src_tex_desc.Width; // min_u
        cb_f[5] = crop_top / (float)src_tex_desc.Height; // min_v
        cb_f[6] = (crop_left + cropped_w) / (float)src_tex_desc.Width; // max_u
        cb_f[7] = (crop_top + cropped_h) / (float)src_tex_desc.Height; // max_v
        cb_u[8] = !cctx->premult_alpha; // to_unpremult
        cb_u[9] = src_tex_desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT &&
                  dst_tex_desc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT; // to_srgb
    }
    def_ctx->Unmap(d3dctx->shader_cb.Get(), 0);

    def_ctx->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr);

    const float clear_color[4] = {0.f, 0.f, 0.f, 1.f};
    def_ctx->ClearRenderTargetView(rtv.Get(), clear_color);

    def_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    def_ctx->VSSetShader(d3dctx->vertex_shader.Get(), nullptr, 0);
    def_ctx->VSSetConstantBuffers(0, 1, d3dctx->shader_cb.GetAddressOf());
    def_ctx->PSSetShader(d3dctx->pixel_shader.Get(), nullptr, 0);
    def_ctx->PSSetSamplers(0, 1, d3dctx->sampler_state.GetAddressOf());
    def_ctx->PSSetShaderResources(0, 1, srv.GetAddressOf());
    def_ctx->PSSetConstantBuffers(0, 1, d3dctx->shader_cb.GetAddressOf());

    def_ctx->Draw(3, 0);

    ComPtr<ID3D11CommandList> cmd_list;
    CHECK_HR_RET(def_ctx->FinishCommandList(FALSE, &cmd_list));
    dev_ctx->ExecuteCommandList(cmd_list.Get(), FALSE);

    return 0;
}

static int process_frame_if_exists(AVFilterLink *outlink)
{
    AVFilterContext *avctx = outlink->src;
    GfxCaptureContext *cctx = CCTX(avctx->priv);
    GfxCaptureContextCpp *ctx = cctx->ctx;
    int ret;

    AVFrame *frame = nullptr;

    ret = run_on_wgc_thread(avctx, [&]() {
        ComPtr<IDirect3D11CaptureFrame> capture_frame;
        ComPtr<IDirect3DSurface> capture_surface;
        ComPtr<IDirect3DDxgiInterfaceAccess> dxgi_interface_access;
        ComPtr<ID3D11Texture2D> frame_texture;
        TimeSpan frame_time = { 0 };

        int res = wgc_try_get_next_frame(avctx, capture_frame);
        if (res < 0)
            return res;

        CHECK_HR_RET(capture_frame->get_SystemRelativeTime(&frame_time));

        CHECK_HR_RET(capture_frame->get_Surface(&capture_surface));
        CHECK_HR_RET(capture_surface.As(&dxgi_interface_access));
        CHECK_HR_RET(dxgi_interface_access->GetInterface(IID_PPV_ARGS(&frame_texture)));

        if (!frame_texture)
            return AVERROR(EAGAIN);

        frame = ff_get_video_buffer(outlink, cctx->canvas_width, cctx->canvas_height);
        if (!frame)
            return AVERROR(ENOMEM);

        frame->pts = frame_time.Duration;

        return render_capture_to_frame(avctx, frame, frame_texture);
    });
    if (ret < 0)
        return ret;

    frame->sample_aspect_ratio = AVRational{1, 1};

    if (ctx->frames_ctx->sw_format == AV_PIX_FMT_RGBAF16) {
        // According to MSDN, all floating point formats contain sRGB image data with linear 1.0 gamma.
        frame->color_range     = AVCOL_RANGE_JPEG;
        frame->color_primaries = AVCOL_PRI_BT709;
        frame->color_trc       = AVCOL_TRC_LINEAR;
        frame->colorspace      = AVCOL_SPC_RGB;
    } else {
        // According to MSDN, all integer formats contain sRGB image data
        frame->color_range     = AVCOL_RANGE_JPEG;
        frame->color_primaries = AVCOL_PRI_BT709;
        frame->color_trc       = AVCOL_TRC_IEC61966_2_1;
        frame->colorspace      = AVCOL_SPC_RGB;
    }

    ctx->last_pts = frame->pts;

    if (!ctx->first_pts)
        ctx->first_pts = frame->pts;
    frame->pts -= ctx->first_pts;

    return ff_filter_frame(outlink, frame);
}

static int gfxcapture_activate(AVFilterContext *avctx)
{
    AVFilterLink *outlink = avctx->outputs[0];
    GfxCaptureContext *cctx = CCTX(avctx->priv);
    GfxCaptureContextCpp *ctx = cctx->ctx;
    std::unique_ptr<GfxCaptureContextWgc> &wgctx = ctx->wgc;

    std::lock_guard wgc_lock(ctx->wgc_thread_uninit_mutex);
    if (!wgctx) {
        av_log(avctx, AV_LOG_ERROR, "WGC thread not initialized\n");
        return AVERROR(ENOSYS);
    }

    if (!ff_outlink_frame_wanted(outlink))
        return FFERROR_NOT_READY;

    for (;;) {
        uint64_t last_seq = wgctx->frame_seq;

        int ret = process_frame_if_exists(outlink);
        if (ret != AVERROR(EAGAIN))
            return ret;

        std::unique_lock frame_lock(wgctx->frame_arrived_mutex);

        if (wgctx->window_closed && wgctx->frame_seq == last_seq) {
            ff_outlink_set_status(outlink, AVERROR_EOF, ctx->last_pts - ctx->first_pts + 1);
            break;
        }

        if (!wgctx->frame_arrived_cond.wait_for(frame_lock, std::chrono::seconds(1), [&]() {
            return wgctx->frame_seq != last_seq || wgctx->window_closed;
        }))
            break;
    }

    return 0;
}

av_cold void ff_gfxcapture_uninit(AVFilterContext *avctx) noexcept
{
    try {
        gfxcapture_uninit(avctx);
    } catch (const std::exception &e) {
        av_log(avctx, AV_LOG_ERROR, "unhandled exception during uninit: %s\n", e.what());
    } catch (...) {
        av_log(avctx, AV_LOG_ERROR, "unhandled exception during uninit\n");
    }
}

av_cold int ff_gfxcapture_init(AVFilterContext *avctx) noexcept
{
    try {
        return gfxcapture_init(avctx);
    } catch (const std::bad_alloc&) {
        return AVERROR(ENOMEM);
    } catch (const std::exception &e) {
        av_log(avctx, AV_LOG_ERROR, "unhandled exception during init: %s\n", e.what());
        return AVERROR_BUG;
    } catch (...) {
        av_log(avctx, AV_LOG_ERROR, "unhandled exception during init\n");
        return AVERROR_BUG;
    }
}

int ff_gfxcapture_activate(AVFilterContext *avctx) noexcept
{
    try {
        return gfxcapture_activate(avctx);
    } catch (const std::bad_alloc&) {
        return AVERROR(ENOMEM);
    } catch (const std::exception &e) {
        av_log(avctx, AV_LOG_ERROR, "unhandled exception during activate: %s\n", e.what());
        return AVERROR_BUG;
    } catch (...) {
        av_log(avctx, AV_LOG_ERROR, "unhandled exception during activate\n");
        return AVERROR_BUG;
    }
}

int ff_gfxcapture_config_props(AVFilterLink *outlink) noexcept
{
    AVFilterContext *avctx = outlink->src;

    try {
        return gfxcapture_config_props(outlink);
    } catch (const std::bad_alloc&) {
        return AVERROR(ENOMEM);
    } catch (const std::exception &e) {
        av_log(avctx, AV_LOG_ERROR, "unhandled exception during config_props: %s\n", e.what());
        return AVERROR_BUG;
    } catch (...) {
        av_log(avctx, AV_LOG_ERROR, "unhandled exception during config_props\n");
        return AVERROR_BUG;
    }
}
