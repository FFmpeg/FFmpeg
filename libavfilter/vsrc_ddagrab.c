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

#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0A00
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN

#include <windows.h>

#define COBJMACROS

#include <initguid.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#if HAVE_IDXGIOUTPUT5
#include <dxgi1_5.h>
#endif

#include "libavutil/opt.h"
#include "libavutil/time.h"
#include "libavutil/avstring.h"
#include "libavutil/avassert.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_d3d11va.h"
#include "compat/w32dlfcn.h"
#include "avfilter.h"
#include "internal.h"
#include "formats.h"
#include "video.h"

#include "vsrc_ddagrab_shaders.h"

// avutil/time.h takes and returns time in microseconds
#define TIMER_RES 1000000
#define TIMER_RES64 INT64_C(1000000)

typedef struct DdagrabContext {
    const AVClass *class;

    AVBufferRef *device_ref;
    AVHWDeviceContext *device_ctx;
    AVD3D11VADeviceContext *device_hwctx;

    AVBufferRef *frames_ref;
    AVHWFramesContext *frames_ctx;
    AVD3D11VAFramesContext *frames_hwctx;

    DXGI_OUTPUT_DESC output_desc;
    IDXGIOutputDuplication *dxgi_outdupl;
    AVFrame *last_frame;

    int mouse_x, mouse_y;
    ID3D11Texture2D *mouse_texture;
    ID3D11ShaderResourceView* mouse_resource_view ;

    AVRational time_base;
    int64_t time_frame;
    int64_t time_timeout;
    int64_t first_pts;

    DXGI_FORMAT raw_format;
    int raw_width;
    int raw_height;

    ID3D11Texture2D *probed_texture;

    ID3D11VertexShader *vertex_shader;
    ID3D11InputLayout *input_layout;
    ID3D11PixelShader *pixel_shader;
    ID3D11Buffer *const_buffer;
    ID3D11SamplerState *sampler_state;
    ID3D11BlendState *blend_state;

    int        output_idx;
    int        draw_mouse;
    AVRational framerate;
    int        width;
    int        height;
    int        offset_x;
    int        offset_y;
    int        out_fmt;
    int        allow_fallback;
    int        force_fmt;
} DdagrabContext;

#define OFFSET(x) offsetof(DdagrabContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
static const AVOption ddagrab_options[] = {
    { "output_idx", "dda output index to capture", OFFSET(output_idx), AV_OPT_TYPE_INT,        { .i64 = 0    },       0, INT_MAX, FLAGS },
    { "draw_mouse", "draw the mouse pointer",      OFFSET(draw_mouse), AV_OPT_TYPE_BOOL,       { .i64 = 1    },       0,       1, FLAGS },
    { "framerate",  "set video frame rate",        OFFSET(framerate),  AV_OPT_TYPE_VIDEO_RATE, { .str = "30" },       0, INT_MAX, FLAGS },
    { "video_size", "set video frame size",        OFFSET(width),      AV_OPT_TYPE_IMAGE_SIZE, { .str = NULL },       0,       0, FLAGS },
    { "offset_x",   "capture area x offset",       OFFSET(offset_x),   AV_OPT_TYPE_INT,        { .i64 = 0    }, INT_MIN, INT_MAX, FLAGS },
    { "offset_y",   "capture area y offset",       OFFSET(offset_y),   AV_OPT_TYPE_INT,        { .i64 = 0    }, INT_MIN, INT_MAX, FLAGS },
    { "output_fmt", "desired output format",       OFFSET(out_fmt),    AV_OPT_TYPE_INT,        { .i64 = DXGI_FORMAT_B8G8R8A8_UNORM },    0, INT_MAX, FLAGS, "output_fmt" },
    { "auto",       "let dda pick its preferred format", 0,            AV_OPT_TYPE_CONST,      { .i64 = 0 },                             0, INT_MAX, FLAGS, "output_fmt" },
    { "8bit",       "only output default 8 Bit format",  0,            AV_OPT_TYPE_CONST,      { .i64 = DXGI_FORMAT_B8G8R8A8_UNORM },    0, INT_MAX, FLAGS, "output_fmt" },
    { "bgra",       "only output 8 Bit BGRA",            0,            AV_OPT_TYPE_CONST,      { .i64 = DXGI_FORMAT_B8G8R8A8_UNORM },    0, INT_MAX, FLAGS, "output_fmt" },
    { "10bit",      "only output default 10 Bit format", 0,            AV_OPT_TYPE_CONST,      { .i64 = DXGI_FORMAT_R10G10B10A2_UNORM }, 0, INT_MAX, FLAGS, "output_fmt" },
    { "x2bgr10",    "only output 10 Bit X2BGR10",        0,            AV_OPT_TYPE_CONST,      { .i64 = DXGI_FORMAT_R10G10B10A2_UNORM }, 0, INT_MAX, FLAGS, "output_fmt" },
    { "16bit",      "only output default 16 Bit format", 0,            AV_OPT_TYPE_CONST,      { .i64 = DXGI_FORMAT_R16G16B16A16_FLOAT },0, INT_MAX, FLAGS, "output_fmt" },
    { "rgbaf16",    "only output 16 Bit RGBAF16",        0,            AV_OPT_TYPE_CONST,      { .i64 = DXGI_FORMAT_R16G16B16A16_FLOAT },0, INT_MAX, FLAGS, "output_fmt" },
    { "allow_fallback", "don't error on fallback to default 8 Bit format",
                                                   OFFSET(allow_fallback), AV_OPT_TYPE_BOOL,   { .i64 = 0    },       0,       1, FLAGS },
    { "force_fmt",  "exclude BGRA from format list (experimental, discouraged by Microsoft)",
                                                   OFFSET(force_fmt),  AV_OPT_TYPE_BOOL,       { .i64 = 0    },       0,       1, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(ddagrab);

static inline void release_resource(void *resource)
{
    IUnknown **resp = (IUnknown**)resource;
    if (*resp) {
        IUnknown_Release(*resp);
        *resp = NULL;
    }
}

static av_cold void ddagrab_uninit(AVFilterContext *avctx)
{
    DdagrabContext *dda = avctx->priv;

    release_resource(&dda->blend_state);
    release_resource(&dda->sampler_state);
    release_resource(&dda->pixel_shader);
    release_resource(&dda->input_layout);
    release_resource(&dda->vertex_shader);
    release_resource(&dda->const_buffer);

    release_resource(&dda->probed_texture);

    release_resource(&dda->dxgi_outdupl);
    release_resource(&dda->mouse_resource_view);
    release_resource(&dda->mouse_texture);

    av_frame_free(&dda->last_frame);
    av_buffer_unref(&dda->frames_ref);
    av_buffer_unref(&dda->device_ref);
}

static av_cold int init_dxgi_dda(AVFilterContext *avctx)
{
    DdagrabContext *dda = avctx->priv;
    IDXGIDevice *dxgi_device = NULL;
    IDXGIAdapter *dxgi_adapter = NULL;
    IDXGIOutput *dxgi_output = NULL;
    IDXGIOutput1 *dxgi_output1 = NULL;
#if HAVE_IDXGIOUTPUT5 && HAVE_DPI_AWARENESS_CONTEXT
    IDXGIOutput5 *dxgi_output5 = NULL;

    typedef DPI_AWARENESS_CONTEXT (*set_thread_dpi_t)(DPI_AWARENESS_CONTEXT);
    set_thread_dpi_t set_thread_dpi;
    HMODULE user32_module;
#endif
    int w, h;
    HRESULT hr;

    hr = ID3D11Device_QueryInterface(dda->device_hwctx->device, &IID_IDXGIDevice, (void**)&dxgi_device);
    if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR, "Failed querying IDXGIDevice\n");
        return AVERROR_EXTERNAL;
    }

    hr = IDXGIDevice_GetParent(dxgi_device, &IID_IDXGIAdapter, (void**)&dxgi_adapter);
    IDXGIDevice_Release(dxgi_device);
    dxgi_device = NULL;
    if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR, "Failed getting parent IDXGIAdapter\n");
        return AVERROR_EXTERNAL;
    }

    hr = IDXGIAdapter_EnumOutputs(dxgi_adapter, dda->output_idx, &dxgi_output);
    IDXGIAdapter_Release(dxgi_adapter);
    dxgi_adapter = NULL;
    if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR, "Failed to enumerate DXGI output %d\n", dda->output_idx);
        return AVERROR_EXTERNAL;
    }

    hr = IDXGIOutput_GetDesc(dxgi_output, &dda->output_desc);
    if (FAILED(hr)) {
        IDXGIOutput_Release(dxgi_output);
        av_log(avctx, AV_LOG_ERROR, "Failed getting output description\n");
        return AVERROR_EXTERNAL;
    }

#if HAVE_IDXGIOUTPUT5 && HAVE_DPI_AWARENESS_CONTEXT
    user32_module = dlopen("user32.dll", 0);
    if (!user32_module) {
        av_log(avctx, AV_LOG_ERROR, "Failed loading user32.dll\n");
        return AVERROR_EXTERNAL;
    }

    set_thread_dpi = (set_thread_dpi_t)dlsym(user32_module, "SetThreadDpiAwarenessContext");

    if (set_thread_dpi)
        hr = IDXGIOutput_QueryInterface(dxgi_output, &IID_IDXGIOutput5, (void**)&dxgi_output5);

    if (set_thread_dpi && SUCCEEDED(hr)) {
        DPI_AWARENESS_CONTEXT prev_dpi_ctx;
        DXGI_FORMAT formats[] = {
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            DXGI_FORMAT_R10G10B10A2_UNORM,
            DXGI_FORMAT_B8G8R8A8_UNORM
        };
        int nb_formats = FF_ARRAY_ELEMS(formats);

        if(dda->out_fmt == DXGI_FORMAT_B8G8R8A8_UNORM) {
            formats[0] = DXGI_FORMAT_B8G8R8A8_UNORM;
            nb_formats = 1;
        } else if (dda->out_fmt) {
            formats[0] = dda->out_fmt;
            formats[1] = DXGI_FORMAT_B8G8R8A8_UNORM;
            nb_formats = dda->force_fmt ? 1 : 2;
        }

        IDXGIOutput_Release(dxgi_output);
        dxgi_output = NULL;

        prev_dpi_ctx = set_thread_dpi(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        if (!prev_dpi_ctx)
            av_log(avctx, AV_LOG_WARNING, "Failed enabling DPI awareness for DDA\n");

        hr = IDXGIOutput5_DuplicateOutput1(dxgi_output5,
            (IUnknown*)dda->device_hwctx->device,
            0,
            nb_formats,
            formats,
            &dda->dxgi_outdupl);
        IDXGIOutput5_Release(dxgi_output5);
        dxgi_output5 = NULL;

        if (prev_dpi_ctx)
            set_thread_dpi(prev_dpi_ctx);

        dlclose(user32_module);
        user32_module = NULL;
        set_thread_dpi = NULL;

        av_log(avctx, AV_LOG_DEBUG, "Using IDXGIOutput5 interface\n");
    } else {
        dlclose(user32_module);
        user32_module = NULL;
        set_thread_dpi = NULL;

        av_log(avctx, AV_LOG_DEBUG, "Falling back to IDXGIOutput1\n");
#else
    {
#endif
        if (dda->out_fmt && dda->out_fmt != DXGI_FORMAT_B8G8R8A8_UNORM && (!dda->allow_fallback || dda->force_fmt)) {
            av_log(avctx, AV_LOG_ERROR, "Only 8 bit output supported with legacy API\n");
            return AVERROR(ENOTSUP);
        }

        hr = IDXGIOutput_QueryInterface(dxgi_output, &IID_IDXGIOutput1, (void**)&dxgi_output1);
        IDXGIOutput_Release(dxgi_output);
        dxgi_output = NULL;
        if (FAILED(hr)) {
            av_log(avctx, AV_LOG_ERROR, "Failed querying IDXGIOutput1\n");
            return AVERROR_EXTERNAL;
        }

        hr = IDXGIOutput1_DuplicateOutput(dxgi_output1,
            (IUnknown*)dda->device_hwctx->device,
            &dda->dxgi_outdupl);
        IDXGIOutput1_Release(dxgi_output1);
        dxgi_output1 = NULL;
    }

    if (hr == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE) {
        av_log(avctx, AV_LOG_ERROR, "Too many open duplication sessions\n");
        return AVERROR(EBUSY);
    } else if (hr == DXGI_ERROR_UNSUPPORTED) {
        av_log(avctx, AV_LOG_ERROR, "Selected output not supported\n");
        return AVERROR_EXTERNAL;
    } else if (hr == E_INVALIDARG) {
        av_log(avctx, AV_LOG_ERROR, "Invalid output duplication argument\n");
        return AVERROR(EINVAL);
    } else if (hr == E_ACCESSDENIED) {
        av_log(avctx, AV_LOG_ERROR, "Desktop duplication access denied\n");
        return AVERROR(EPERM);
    } else if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR, "Failed duplicating output\n");
        return AVERROR_EXTERNAL;
    }

    w = dda->output_desc.DesktopCoordinates.right - dda->output_desc.DesktopCoordinates.left;
    h = dda->output_desc.DesktopCoordinates.bottom - dda->output_desc.DesktopCoordinates.top;
    av_log(avctx, AV_LOG_VERBOSE, "Opened dxgi output %d with dimensions %dx%d\n", dda->output_idx, w, h);

    return 0;
}

typedef struct ConstBufferData
{
    float width;
    float height;

    uint64_t padding;
} ConstBufferData;

static const D3D11_INPUT_ELEMENT_DESC vertex_shader_input_layout[] =
{
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
};

static av_cold int init_render_resources(AVFilterContext *avctx)
{
    DdagrabContext *dda = avctx->priv;
    ID3D11Device *dev = dda->device_hwctx->device;
    D3D11_SAMPLER_DESC sampler_desc = { 0 };
    D3D11_BLEND_DESC blend_desc = { 0 };
    D3D11_BUFFER_DESC buffer_desc = { 0 };
    D3D11_SUBRESOURCE_DATA buffer_data = { 0 };
    ConstBufferData const_data = { 0 };
    HRESULT hr;

    hr = ID3D11Device_CreateVertexShader(dev,
        vertex_shader_bytes,
        FF_ARRAY_ELEMS(vertex_shader_bytes),
        NULL,
        &dda->vertex_shader);
    if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR, "CreateVertexShader failed: %lx\n", hr);
        return AVERROR_EXTERNAL;
    }

    hr = ID3D11Device_CreateInputLayout(dev,
        vertex_shader_input_layout,
        FF_ARRAY_ELEMS(vertex_shader_input_layout),
        vertex_shader_bytes,
        FF_ARRAY_ELEMS(vertex_shader_bytes),
        &dda->input_layout);
    if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR, "CreateInputLayout failed: %lx\n", hr);
        return AVERROR_EXTERNAL;
    }

    hr = ID3D11Device_CreatePixelShader(dev,
        pixel_shader_bytes,
        FF_ARRAY_ELEMS(pixel_shader_bytes),
        NULL,
        &dda->pixel_shader);
    if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR, "CreatePixelShader failed: %lx\n", hr);
        return AVERROR_EXTERNAL;
    }

    const_data = (ConstBufferData){ dda->width, dda->height };

    buffer_data.pSysMem = &const_data;
    buffer_desc.ByteWidth = sizeof(const_data);
    buffer_desc.Usage = D3D11_USAGE_IMMUTABLE;
    buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    hr = ID3D11Device_CreateBuffer(dev,
        &buffer_desc,
        &buffer_data,
        &dda->const_buffer);
    if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR, "CreateBuffer const buffer failed: %lx\n", hr);
        return AVERROR_EXTERNAL;
    }

    sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    hr = ID3D11Device_CreateSamplerState(dev,
        &sampler_desc,
        &dda->sampler_state);
    if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR, "CreateSamplerState failed: %lx\n", hr);
        return AVERROR_EXTERNAL;
    }

    blend_desc.AlphaToCoverageEnable = FALSE;
    blend_desc.IndependentBlendEnable = FALSE;
    blend_desc.RenderTarget[0].BlendEnable = TRUE;
    blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    hr = ID3D11Device_CreateBlendState(dev,
        &blend_desc,
        &dda->blend_state);
    if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR, "CreateBlendState failed: %lx\n", hr);
        return AVERROR_EXTERNAL;
    }

    return 0;
}

static av_cold int ddagrab_init(AVFilterContext *avctx)
{
    DdagrabContext *dda = avctx->priv;

    dda->last_frame = av_frame_alloc();
    if (!dda->last_frame)
        return AVERROR(ENOMEM);

    dda->mouse_x = -1;
    dda->mouse_y = -1;

    return 0;
}

static int create_d3d11_pointer_tex(AVFilterContext *avctx,
                                    uint8_t *buf,
                                    DXGI_OUTDUPL_POINTER_SHAPE_INFO *shape_info,
                                    ID3D11Texture2D **out_tex,
                                    ID3D11ShaderResourceView **res_view)
{
    DdagrabContext *dda = avctx->priv;
    D3D11_TEXTURE2D_DESC desc = { 0 };
    D3D11_SUBRESOURCE_DATA init_data = { 0 };
    D3D11_SHADER_RESOURCE_VIEW_DESC resource_desc = { 0 };
    HRESULT hr;

    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    desc.Width = shape_info->Width;
    desc.Height = shape_info->Height;

    init_data.pSysMem = buf;
    init_data.SysMemPitch = shape_info->Pitch;

    resource_desc.Format = desc.Format;
    resource_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    resource_desc.Texture2D.MostDetailedMip = 0;
    resource_desc.Texture2D.MipLevels = 1;

    hr = ID3D11Device_CreateTexture2D(dda->device_hwctx->device,
        &desc,
        &init_data,
        out_tex);
    if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR, "Failed creating pointer texture\n");
        return AVERROR_EXTERNAL;
    }

    hr = ID3D11Device_CreateShaderResourceView(dda->device_hwctx->device,
        (ID3D11Resource*)dda->mouse_texture,
        &resource_desc,
        res_view);
    if (FAILED(hr)) {
        release_resource(out_tex);
        av_log(avctx, AV_LOG_ERROR, "CreateShaderResourceView for mouse failed: %lx\n", hr);
        return AVERROR_EXTERNAL;
    }

    return 0;
}

static uint8_t *convert_mono_buffer(uint8_t *input, int *_width, int *_height, int *_pitch)
{
    int width = *_width, height = *_height, pitch = *_pitch;
    int real_height = height / 2;
    uint8_t *output = av_malloc(real_height * width * 4);
    int y, x;

    if (!output)
        return NULL;

    // This simulates drawing the cursor on a full black surface
    // i.e. ignore the AND mask, turn XOR mask into all 4 color channels
    for (y = 0; y < real_height; y++) {
        for (x = 0; x < width; x++) {
            int v = input[(real_height + y) * pitch + (x / 8)];
            v = (v >> (7 - (x % 8))) & 1;
            memset(&output[4 * ((y*width) + x)], v ? 0xFF : 0, 4);
        }
    }

    *_pitch = width * 4;
    *_height = real_height;

    return output;
}

static void fixup_color_mask(uint8_t *buf, int width, int height, int pitch)
{
    int x, y;
    // There is no good way to replicate XOR'ig parts of the texture with the screen
    // best effort is rendering the non-masked parts, and make the rest transparent
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            int pos = (y*pitch) + (4*x) + 3;
            buf[pos] = buf[pos] ? 0 : 0xFF;
        }
    }
}

static int update_mouse_pointer(AVFilterContext *avctx, DXGI_OUTDUPL_FRAME_INFO *frame_info)
{
    DdagrabContext *dda = avctx->priv;
    HRESULT hr;
    int ret;

    if (frame_info->LastMouseUpdateTime.QuadPart == 0)
        return 0;

    if (frame_info->PointerPosition.Visible) {
        dda->mouse_x = frame_info->PointerPosition.Position.x;
        dda->mouse_y = frame_info->PointerPosition.Position.y;
    } else {
        dda->mouse_x = dda->mouse_y = -1;
    }

    if (frame_info->PointerShapeBufferSize) {
        UINT size = frame_info->PointerShapeBufferSize;
        DXGI_OUTDUPL_POINTER_SHAPE_INFO shape_info;
        uint8_t *buf = av_malloc(size);
        if (!buf)
            return AVERROR(ENOMEM);

        hr = IDXGIOutputDuplication_GetFramePointerShape(dda->dxgi_outdupl,
            size,
            buf,
            &size,
            &shape_info);
        if (FAILED(hr)) {
            av_free(buf);
            av_log(avctx, AV_LOG_ERROR, "Failed getting pointer shape: %lx\n", hr);
            return AVERROR_EXTERNAL;
        }

        if (shape_info.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME) {
            uint8_t *new_buf = convert_mono_buffer(buf, &shape_info.Width, &shape_info.Height, &shape_info.Pitch);
            av_free(buf);
            if (!new_buf)
                return AVERROR(ENOMEM);
            buf = new_buf;
        } else if (shape_info.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MASKED_COLOR) {
            fixup_color_mask(buf, shape_info.Width, shape_info.Height, shape_info.Pitch);
        } else if (shape_info.Type != DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR) {
            av_log(avctx, AV_LOG_WARNING, "Unsupported pointer shape type: %d\n", (int)shape_info.Type);
            av_free(buf);
            return 0;
        }

        release_resource(&dda->mouse_resource_view);
        release_resource(&dda->mouse_texture);

        ret = create_d3d11_pointer_tex(avctx, buf, &shape_info, &dda->mouse_texture, &dda->mouse_resource_view);
        av_freep(&buf);
        if (ret < 0)
            return ret;

        av_log(avctx, AV_LOG_VERBOSE, "Updated pointer shape texture\n");
    }

    return 0;
}

static int next_frame_internal(AVFilterContext *avctx, ID3D11Texture2D **desktop_texture)
{
    DXGI_OUTDUPL_FRAME_INFO frame_info;
    DdagrabContext *dda = avctx->priv;
    IDXGIResource *desktop_resource = NULL;
    HRESULT hr;
    int ret;

    hr = IDXGIOutputDuplication_AcquireNextFrame(
        dda->dxgi_outdupl,
        dda->time_timeout,
        &frame_info,
        &desktop_resource);
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        return AVERROR(EAGAIN);
    } else if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR, "AcquireNextFrame failed: %lx\n", hr);
        return AVERROR_EXTERNAL;
    }

    if (dda->draw_mouse) {
        ret = update_mouse_pointer(avctx, &frame_info);
        if (ret < 0)
            return ret;
    }

    hr = IDXGIResource_QueryInterface(desktop_resource, &IID_ID3D11Texture2D, (void**)desktop_texture);
    IDXGIResource_Release(desktop_resource);
    desktop_resource = NULL;
    if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR, "DXGIResource QueryInterface failed\n");
        return AVERROR_EXTERNAL;
    }

    return 0;
}

static int probe_output_format(AVFilterContext *avctx)
{
    DdagrabContext *dda = avctx->priv;
    D3D11_TEXTURE2D_DESC desc;
    int ret;

    av_assert1(!dda->probed_texture);

    do {
        ret = next_frame_internal(avctx, &dda->probed_texture);
    } while(ret == AVERROR(EAGAIN));
    if (ret < 0)
        return ret;

    ID3D11Texture2D_GetDesc(dda->probed_texture, &desc);

    dda->raw_format = desc.Format;
    dda->raw_width = desc.Width;
    dda->raw_height = desc.Height;

    if (dda->width <= 0)
        dda->width = dda->raw_width;
    if (dda->height <= 0)
        dda->height = dda->raw_height;

    return 0;
}

static av_cold int init_hwframes_ctx(AVFilterContext *avctx)
{
    DdagrabContext *dda = avctx->priv;
    int ret = 0;

    dda->frames_ref = av_hwframe_ctx_alloc(dda->device_ref);
    if (!dda->frames_ref)
        return AVERROR(ENOMEM);
    dda->frames_ctx = (AVHWFramesContext*)dda->frames_ref->data;
    dda->frames_hwctx = (AVD3D11VAFramesContext*)dda->frames_ctx->hwctx;

    dda->frames_ctx->format    = AV_PIX_FMT_D3D11;
    dda->frames_ctx->width     = dda->width;
    dda->frames_ctx->height    = dda->height;

    switch (dda->raw_format) {
    case DXGI_FORMAT_B8G8R8A8_UNORM:
        av_log(avctx, AV_LOG_VERBOSE, "Probed 8 bit RGB frame format\n");
        dda->frames_ctx->sw_format = AV_PIX_FMT_BGRA;
        break;
    case DXGI_FORMAT_R10G10B10A2_UNORM:
        av_log(avctx, AV_LOG_VERBOSE, "Probed 10 bit RGB frame format\n");
        dda->frames_ctx->sw_format = AV_PIX_FMT_X2BGR10;
        break;
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
        av_log(avctx, AV_LOG_VERBOSE, "Probed 16 bit float RGB frame format\n");
        dda->frames_ctx->sw_format = AV_PIX_FMT_RGBAF16;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unexpected texture output format!\n");
        return AVERROR_BUG;
    }

    if (dda->draw_mouse)
        dda->frames_hwctx->BindFlags |= D3D11_BIND_RENDER_TARGET;

    ret = av_hwframe_ctx_init(dda->frames_ref);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to initialise hardware frames context: %d.\n", ret);
        goto fail;
    }

    return 0;
fail:
    av_buffer_unref(&dda->frames_ref);
    return ret;
}

static int ddagrab_config_props(AVFilterLink *outlink)
{
    AVFilterContext *avctx = outlink->src;
    DdagrabContext *dda = avctx->priv;
    int ret;

    if (avctx->hw_device_ctx) {
        dda->device_ctx = (AVHWDeviceContext*)avctx->hw_device_ctx->data;

        if (dda->device_ctx->type != AV_HWDEVICE_TYPE_D3D11VA) {
            av_log(avctx, AV_LOG_ERROR, "Non-D3D11VA input hw_device_ctx\n");
            return AVERROR(EINVAL);
        }

        dda->device_ref = av_buffer_ref(avctx->hw_device_ctx);
        if (!dda->device_ref)
            return AVERROR(ENOMEM);

        av_log(avctx, AV_LOG_VERBOSE, "Using provided hw_device_ctx\n");
    } else {
        ret = av_hwdevice_ctx_create(&dda->device_ref, AV_HWDEVICE_TYPE_D3D11VA, NULL, NULL, 0);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "Failed to create D3D11VA device.\n");
            return ret;
        }

        dda->device_ctx = (AVHWDeviceContext*)dda->device_ref->data;

        av_log(avctx, AV_LOG_VERBOSE, "Created internal hw_device_ctx\n");
    }

    dda->device_hwctx = (AVD3D11VADeviceContext*)dda->device_ctx->hwctx;

    ret = init_dxgi_dda(avctx);
    if (ret < 0)
        return ret;

    ret = probe_output_format(avctx);
    if (ret < 0)
        return ret;

    if (dda->out_fmt && dda->raw_format != dda->out_fmt && (!dda->allow_fallback || dda->force_fmt)) {
        av_log(avctx, AV_LOG_ERROR, "Requested output format unavailable.\n");
        return AVERROR(ENOTSUP);
    }

    dda->width -= FFMAX(dda->width - dda->raw_width + dda->offset_x, 0);
    dda->height -= FFMAX(dda->height - dda->raw_height + dda->offset_y, 0);

    dda->time_base  = av_inv_q(dda->framerate);
    dda->time_frame = av_gettime_relative() / av_q2d(dda->time_base);
    dda->time_timeout = av_rescale_q(1, dda->time_base, (AVRational) { 1, 1000 }) / 2;

    if (dda->draw_mouse) {
        ret = init_render_resources(avctx);
        if (ret < 0)
            return ret;
    }

    ret = init_hwframes_ctx(avctx);
    if (ret < 0)
        return ret;

    outlink->hw_frames_ctx = av_buffer_ref(dda->frames_ref);
    if (!outlink->hw_frames_ctx)
        return AVERROR(ENOMEM);

    outlink->w = dda->width;
    outlink->h = dda->height;
    outlink->time_base = (AVRational){1, TIMER_RES};
    outlink->frame_rate = dda->framerate;

    return 0;
}

static int draw_mouse_pointer(AVFilterContext *avctx, AVFrame *frame)
{
    DdagrabContext *dda = avctx->priv;
    ID3D11DeviceContext *devctx = dda->device_hwctx->device_context;
    ID3D11Texture2D *frame_tex = (ID3D11Texture2D*)frame->data[0];
    D3D11_RENDER_TARGET_VIEW_DESC target_desc = { 0 };
    ID3D11RenderTargetView* target_view = NULL;
    ID3D11Buffer *mouse_vertex_buffer = NULL;
    D3D11_TEXTURE2D_DESC tex_desc;
    int num_vertices = 0;
    int x, y;
    HRESULT hr;
    int ret = 0;

    if (!dda->mouse_texture || dda->mouse_x < 0 || dda->mouse_y < 0)
        return 0;

    ID3D11Texture2D_GetDesc(dda->mouse_texture, &tex_desc);

    x = dda->mouse_x - dda->offset_x;
    y = dda->mouse_y - dda->offset_y;

    if (x >= dda->width || y >= dda->height ||
        -x >= (int)tex_desc.Width || -y >= (int)tex_desc.Height)
        return 0;

    target_desc.Format = dda->raw_format;
    target_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    target_desc.Texture2D.MipSlice = 0;

    hr = ID3D11Device_CreateRenderTargetView(dda->device_hwctx->device,
        (ID3D11Resource*)frame_tex,
        &target_desc,
        &target_view);
    if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR, "CreateRenderTargetView failed: %lx\n", hr);
        ret = AVERROR_EXTERNAL;
        goto end;
    }

    ID3D11DeviceContext_ClearState(devctx);

    {
        D3D11_VIEWPORT viewport = { 0 };
        viewport.Width = dda->width;
        viewport.Height = dda->height;
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;

        ID3D11DeviceContext_RSSetViewports(devctx, 1, &viewport);
    }

    {
        FLOAT vertices[] = {
            // x, y, z,  u, v
            x                 , y + tex_desc.Height, 0.0f,  0.0f, 1.0f,
            x                 , y                  , 0.0f,  0.0f, 0.0f,
            x + tex_desc.Width, y + tex_desc.Height, 0.0f,  1.0f, 1.0f,
            x + tex_desc.Width, y                  , 0.0f,  1.0f, 0.0f,
        };
        UINT stride = sizeof(FLOAT) * 5;
        UINT offset = 0;

        D3D11_SUBRESOURCE_DATA init_data = { 0 };
        D3D11_BUFFER_DESC buf_desc = { 0 };

        num_vertices = sizeof(vertices) / (sizeof(FLOAT) * 5);

        buf_desc.Usage = D3D11_USAGE_DEFAULT;
        buf_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        buf_desc.ByteWidth = sizeof(vertices);
        init_data.pSysMem = vertices;

        hr = ID3D11Device_CreateBuffer(dda->device_hwctx->device,
            &buf_desc,
            &init_data,
            &mouse_vertex_buffer);
        if (FAILED(hr)) {
            av_log(avctx, AV_LOG_ERROR, "CreateBuffer failed: %lx\n", hr);
            ret = AVERROR_EXTERNAL;
            goto end;
        }

        ID3D11DeviceContext_IASetVertexBuffers(devctx, 0, 1, &mouse_vertex_buffer, &stride, &offset);
        ID3D11DeviceContext_IASetInputLayout(devctx, dda->input_layout);
        ID3D11DeviceContext_IASetPrimitiveTopology(devctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    }

    ID3D11DeviceContext_VSSetShader(devctx, dda->vertex_shader, NULL, 0);
    ID3D11DeviceContext_VSSetConstantBuffers(devctx, 0, 1, &dda->const_buffer);
    ID3D11DeviceContext_PSSetSamplers(devctx, 0, 1, &dda->sampler_state);
    ID3D11DeviceContext_PSSetShaderResources(devctx, 0, 1, &dda->mouse_resource_view);
    ID3D11DeviceContext_PSSetShader(devctx, dda->pixel_shader, NULL, 0);

    ID3D11DeviceContext_OMSetBlendState(devctx, dda->blend_state, NULL, 0xFFFFFFFF);
    ID3D11DeviceContext_OMSetRenderTargets(devctx, 1, &target_view, NULL);

    ID3D11DeviceContext_Draw(devctx, num_vertices, 0);

end:
    release_resource(&mouse_vertex_buffer);
    release_resource(&target_view);

    return ret;
}

static int ddagrab_request_frame(AVFilterLink *outlink)
{
    AVFilterContext *avctx = outlink->src;
    DdagrabContext *dda = avctx->priv;

    ID3D11Texture2D *cur_texture = NULL;
    D3D11_TEXTURE2D_DESC desc = { 0 };
    D3D11_BOX box = { 0 };

    int64_t time_frame = dda->time_frame;
    int64_t now, delay;
    AVFrame *frame = NULL;
    HRESULT hr;
    int ret;

    /* time_frame is in units of microseconds divided by the time_base.
     * This means that adding a clean 1M to it is the equivalent of adding
     * 1M*time_base microseconds to it, except it avoids all rounding error.
     * The only time rounding error occurs is when multiplying to calculate
     * the delay. So any rounding error there corrects itself over time.
     */
    time_frame += TIMER_RES64;
    for (;;) {
        now = av_gettime_relative();
        delay = time_frame * av_q2d(dda->time_base) - now;
        if (delay <= 0) {
            if (delay < -TIMER_RES64 * av_q2d(dda->time_base)) {
                time_frame += TIMER_RES64;
            }
            break;
        }
        av_usleep(delay);
    }

    if (!dda->first_pts)
        dda->first_pts = now;
    now -= dda->first_pts;

    if (!dda->probed_texture) {
        ret = next_frame_internal(avctx, &cur_texture);
    } else {
        cur_texture = dda->probed_texture;
        dda->probed_texture = NULL;
        ret = 0;
    }

    if (ret == AVERROR(EAGAIN) && dda->last_frame->buf[0]) {
        frame = av_frame_alloc();
        if (!frame)
            return AVERROR(ENOMEM);

        ret = av_frame_ref(frame, dda->last_frame);
        if (ret < 0) {
            av_frame_free(&frame);
            return ret;
        }

        av_log(avctx, AV_LOG_DEBUG, "Duplicated output frame\n");

        goto frame_done;
    } else if (ret == AVERROR(EAGAIN)) {
        av_log(avctx, AV_LOG_VERBOSE, "Initial DDA AcquireNextFrame timeout!\n");
        return AVERROR(EAGAIN);
    } else if (ret < 0) {
        return ret;
    }

    // AcquireNextFrame sometimes has bursts of delay.
    // This increases accuracy of the timestamp, but might upset consumers due to more jittery framerate?
    now = av_gettime_relative() - dda->first_pts;

    ID3D11Texture2D_GetDesc(cur_texture, &desc);
    if (desc.Format != dda->raw_format ||
        (int)desc.Width != dda->raw_width ||
        (int)desc.Height != dda->raw_height) {
        av_log(avctx, AV_LOG_ERROR, "Output parameters changed!");
        ret = AVERROR_OUTPUT_CHANGED;
        goto fail;
    }

    frame = ff_get_video_buffer(outlink, dda->width, dda->height);
    if (!frame) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    box.left = dda->offset_x;
    box.top = dda->offset_y;
    box.right = box.left + dda->width;
    box.bottom = box.top + dda->height;
    box.front = 0;
    box.back = 1;

    ID3D11DeviceContext_CopySubresourceRegion(
        dda->device_hwctx->device_context,
        (ID3D11Resource*)frame->data[0], (UINT)(intptr_t)frame->data[1],
        0, 0, 0,
        (ID3D11Resource*)cur_texture, 0,
        &box);

    release_resource(&cur_texture);

    hr = IDXGIOutputDuplication_ReleaseFrame(dda->dxgi_outdupl);
    if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR, "DDA ReleaseFrame failed!\n");
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

    if (dda->draw_mouse) {
        ret = draw_mouse_pointer(avctx, frame);
        if (ret < 0)
            goto fail;
    }

    frame->sample_aspect_ratio = (AVRational){1, 1};

    if (desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM ||
        desc.Format == DXGI_FORMAT_R10G10B10A2_UNORM) {
        // According to MSDN, all integer formats contain sRGB image data
        frame->color_range     = AVCOL_RANGE_JPEG;
        frame->color_primaries = AVCOL_PRI_BT709;
        frame->color_trc       = AVCOL_TRC_IEC61966_2_1;
        frame->colorspace      = AVCOL_SPC_RGB;
    } else if(desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
        // According to MSDN, all floating point formats contain sRGB image data with linear 1.0 gamma.
        frame->color_range     = AVCOL_RANGE_JPEG;
        frame->color_primaries = AVCOL_PRI_BT709;
        frame->color_trc       = AVCOL_TRC_LINEAR;
        frame->colorspace      = AVCOL_SPC_RGB;
    } else {
        ret = AVERROR_BUG;
        goto fail;
    }

    av_frame_unref(dda->last_frame);
    ret = av_frame_ref(dda->last_frame, frame);
    if (ret < 0)
        return ret;

frame_done:
    frame->pts = now;
    dda->time_frame = time_frame;

    return ff_filter_frame(outlink, frame);

fail:
    if (frame)
        av_frame_free(&frame);

    if (cur_texture)
        IDXGIOutputDuplication_ReleaseFrame(dda->dxgi_outdupl);

    release_resource(&cur_texture);
    return ret;
}

static const AVFilterPad ddagrab_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = ddagrab_request_frame,
        .config_props  = ddagrab_config_props,
    },
};

const AVFilter ff_vsrc_ddagrab = {
    .name          = "ddagrab",
    .description   = NULL_IF_CONFIG_SMALL("Grab Windows Desktop images using Desktop Duplication API"),
    .priv_size     = sizeof(DdagrabContext),
    .priv_class    = &ddagrab_class,
    .init          = ddagrab_init,
    .uninit        = ddagrab_uninit,
    .inputs        = NULL,
    FILTER_OUTPUTS(ddagrab_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_D3D11),
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
