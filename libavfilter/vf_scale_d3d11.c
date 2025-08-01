/*
 * Copyright (C) 2025 MulticorewWare, Inc.
 *
 * Authors: Dash Santosh <dash.sathanatayanan@multicorewareinc.com>
 *          Sachin <sachin.prakash@multicorewareinc.com>
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

#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "compat/w32dlfcn.h"

#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_d3d11va.h"

#include "filters.h"
#include "scale_eval.h"
#include "video.h"

typedef struct ScaleD3D11Context {
    const AVClass *classCtx;
    char *w_expr;
    char *h_expr;
    enum AVPixelFormat format;

    ///< D3D11 objects
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    ID3D11VideoDevice *videoDevice;
    ID3D11VideoProcessor *processor;
    ID3D11VideoProcessorEnumerator *enumerator;
    ID3D11VideoProcessorOutputView *outputView;
    ID3D11VideoProcessorInputView *inputView;

    ///< Buffer references
    AVBufferRef *hw_device_ctx;
    AVBufferRef *hw_frames_ctx_out;

    ///< Dimensions and formats
    int width, height;
    int inputWidth, inputHeight;
    DXGI_FORMAT input_format;
    DXGI_FORMAT output_format;
} ScaleD3D11Context;

static av_cold int scale_d3d11_init(AVFilterContext *ctx) {
    ///< all real work is done in config_props and filter_frame
    return 0;
}

static void release_d3d11_resources(ScaleD3D11Context *s) {
    if (s->outputView) {
        s->outputView->lpVtbl->Release(s->outputView);
        s->outputView = NULL;
    }

    if (s->processor) {
        s->processor->lpVtbl->Release(s->processor);
        s->processor = NULL;
    }

    if (s->enumerator) {
        s->enumerator->lpVtbl->Release(s->enumerator);
        s->enumerator = NULL;
    }

    if (s->videoDevice) {
        s->videoDevice->lpVtbl->Release(s->videoDevice);
        s->videoDevice = NULL;
    }
}

static int scale_d3d11_configure_processor(ScaleD3D11Context *s, AVFilterContext *ctx) {
    HRESULT hr;

    switch (s->format) {
        case AV_PIX_FMT_NV12:
            s->output_format = DXGI_FORMAT_NV12;
            break;
        case AV_PIX_FMT_P010:
            s->output_format = DXGI_FORMAT_P010;
            break;
        default:
            av_log(ctx, AV_LOG_ERROR, "Invalid output format specified\n");
            return AVERROR(EINVAL);
    }

    ///< Get D3D11 device and context from hardware device context
    AVHWDeviceContext *hwctx = (AVHWDeviceContext *)s->hw_device_ctx->data;
    AVD3D11VADeviceContext *d3d11_hwctx = (AVD3D11VADeviceContext *)hwctx->hwctx;
    s->device = d3d11_hwctx->device;
    s->context = d3d11_hwctx->device_context;

    av_log(ctx, AV_LOG_VERBOSE, "Configuring D3D11 video processor: %dx%d -> %dx%d\n",
           s->inputWidth, s->inputHeight, s->width, s->height);

    ///< Define the video processor content description
    D3D11_VIDEO_PROCESSOR_CONTENT_DESC contentDesc = {
        .InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE,
        .InputWidth = s->inputWidth,
        .InputHeight = s->inputHeight,
        .OutputWidth = s->width,
        .OutputHeight = s->height,
        .Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL,
    };

    ///< Query video device interface
    hr = s->device->lpVtbl->QueryInterface(s->device, &IID_ID3D11VideoDevice, (void **)&s->videoDevice);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to get D3D11 video device interface: HRESULT 0x%lX\n", hr);
        return AVERROR_EXTERNAL;
    }

    ///< Create video processor enumerator
    hr = s->videoDevice->lpVtbl->CreateVideoProcessorEnumerator(s->videoDevice, &contentDesc, &s->enumerator);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create video processor enumerator: HRESULT 0x%lX\n", hr);
        return AVERROR_EXTERNAL;
    }

    ///< Create the video processor
    hr = s->videoDevice->lpVtbl->CreateVideoProcessor(s->videoDevice, s->enumerator, 0, &s->processor);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create video processor: HRESULT 0x%lX\n", hr);
        return AVERROR_EXTERNAL;
    }

    av_log(ctx, AV_LOG_VERBOSE, "D3D11 video processor successfully configured\n");
    return 0;
}

static int scale_d3d11_filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    ScaleD3D11Context *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    ID3D11VideoProcessorInputView *inputView = NULL;
    ID3D11VideoContext *videoContext = NULL;
    AVFrame *out = NULL;
    int ret = 0;
    HRESULT hr;

    ///< Validate input frame
    if (!in) {
        av_log(ctx, AV_LOG_ERROR, "Null input frame\n");
        return AVERROR(EINVAL);
    }

    if (!in->hw_frames_ctx) {
        av_log(ctx, AV_LOG_ERROR, "No hardware frames context in input frame\n");
        av_frame_free(&in);
        return AVERROR(EINVAL);
    }

    ///< Verify hardware device contexts
    AVHWFramesContext *frames_ctx = (AVHWFramesContext *)in->hw_frames_ctx->data;

    if (!s->hw_device_ctx) {
        av_log(ctx, AV_LOG_ERROR, "Filter hardware device context is uninitialized\n");
        av_frame_free(&in);
        return AVERROR(EINVAL);
    }

    AVHWDeviceContext *input_device_ctx = (AVHWDeviceContext *)frames_ctx->device_ref->data;
    AVHWDeviceContext *filter_device_ctx = (AVHWDeviceContext *)s->hw_device_ctx->data;

    if (input_device_ctx->type != filter_device_ctx->type) {
        av_log(ctx, AV_LOG_ERROR, "Mismatch between input and filter hardware device types\n");
        av_frame_free(&in);
        return AVERROR(EINVAL);
    }

    ///< Allocate output frame
    out = av_frame_alloc();
    if (!out) {
        av_log(ctx, AV_LOG_ERROR, "Failed to allocate output frame\n");
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }

    ret = av_hwframe_get_buffer(s->hw_frames_ctx_out, out, 0);
    if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to get output frame from pool\n");
        goto fail;
    }

    ///< Configure the D3D11 video processor if not already configured
    if (!s->processor) {
        ///< Get info from input texture
        D3D11_TEXTURE2D_DESC textureDesc;
        ID3D11Texture2D *input_texture = (ID3D11Texture2D *)in->data[0];
        input_texture->lpVtbl->GetDesc(input_texture, &textureDesc);

        s->inputWidth = textureDesc.Width;
        s->inputHeight = textureDesc.Height;
        s->input_format = textureDesc.Format;

        ret = scale_d3d11_configure_processor(s, ctx);
        if (ret < 0) {
            av_log(ctx, AV_LOG_ERROR, "Failed to configure processor\n");
            goto fail;
        }
    }

    ///< Get input texture and prepare input view
    ID3D11Texture2D *d3d11_texture = (ID3D11Texture2D *)in->data[0];
    int subIdx = (int)(intptr_t)in->data[1];

    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputViewDesc = {
        .FourCC = s->input_format,
        .ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D,
        .Texture2D.ArraySlice = subIdx
    };

    hr = s->videoDevice->lpVtbl->CreateVideoProcessorInputView(
        s->videoDevice, (ID3D11Resource *)d3d11_texture, s->enumerator, &inputViewDesc, &inputView);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create input view: HRESULT 0x%lX\n", hr);
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

    ///< Create output view for current texture
    ID3D11Texture2D *output_texture = (ID3D11Texture2D *)out->data[0];
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outputViewDesc = {
        .ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D,
        .Texture2D = { .MipSlice = 0 },
    };

    hr = s->videoDevice->lpVtbl->CreateVideoProcessorOutputView(
        s->videoDevice, (ID3D11Resource *)output_texture, s->enumerator, &outputViewDesc, &s->outputView);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create output view: HRESULT 0x%lX\n", hr);
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

    ///< Set up processing stream
    D3D11_VIDEO_PROCESSOR_STREAM stream = {
        .Enable = TRUE,
        .pInputSurface = inputView,
        .OutputIndex = 0
    };

    ///< Get video context
    hr = s->context->lpVtbl->QueryInterface(s->context, &IID_ID3D11VideoContext, (void **)&videoContext);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to get video context: HRESULT 0x%lX\n", hr);
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

    ///< Process the frame
    hr = videoContext->lpVtbl->VideoProcessorBlt(videoContext, s->processor, s->outputView, 0, 1, &stream);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "VideoProcessorBlt failed: HRESULT 0x%lX\n", hr);
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

    ///< Set up output frame
    ret = av_frame_copy_props(out, in);
    if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to copy frame properties\n");
        goto fail;
    }

    out->data[0] = (uint8_t *)output_texture;
    out->data[1] = (uint8_t *)(intptr_t)0;
    out->width = s->width;
    out->height = s->height;
    out->format = AV_PIX_FMT_D3D11;

    ///< Clean up resources
    inputView->lpVtbl->Release(inputView);
    videoContext->lpVtbl->Release(videoContext);
    if (s->outputView) {
        s->outputView->lpVtbl->Release(s->outputView);
        s->outputView = NULL;
    }
    av_frame_free(&in);

    ///< Forward the frame
    return ff_filter_frame(outlink, out);

fail:
    if (inputView)
        inputView->lpVtbl->Release(inputView);
    if (videoContext)
        videoContext->lpVtbl->Release(videoContext);
    if (s->outputView) {
        s->outputView->lpVtbl->Release(s->outputView);
        s->outputView = NULL;
    }
    av_frame_free(&in);
    av_frame_free(&out);
    return ret;
}

static int scale_d3d11_config_props(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    ScaleD3D11Context *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    FilterLink *inl = ff_filter_link(inlink);
    FilterLink *outl = ff_filter_link(outlink);
    int ret;

    ///< Clean up any previous resources
    release_d3d11_resources(s);

    ///< Evaluate output dimensions
    ret = ff_scale_eval_dimensions(s, s->w_expr, s->h_expr, inlink, outlink, &s->width, &s->height);
    if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to evaluate dimensions\n");
        return ret;
    }

    outlink->w = s->width;
    outlink->h = s->height;

    ///< Validate input hw_frames_ctx
    if (!inl->hw_frames_ctx) {
        av_log(ctx, AV_LOG_ERROR, "No hw_frames_ctx available on input link\n");
        return AVERROR(EINVAL);
    }

    ///< Propagate hw_frames_ctx to output
    outl->hw_frames_ctx = av_buffer_ref(inl->hw_frames_ctx);
    if (!outl->hw_frames_ctx) {
        av_log(ctx, AV_LOG_ERROR, "Failed to propagate hw_frames_ctx to output\n");
        return AVERROR(ENOMEM);
    }

    ///< Initialize filter's hardware device context
    if (!s->hw_device_ctx) {
        AVHWFramesContext *in_frames_ctx = (AVHWFramesContext *)inl->hw_frames_ctx->data;
        s->hw_device_ctx = av_buffer_ref(in_frames_ctx->device_ref);
        if (!s->hw_device_ctx) {
            av_log(ctx, AV_LOG_ERROR, "Failed to initialize filter hardware device context\n");
            return AVERROR(ENOMEM);
        }
    }

    ///< Get D3D11 device and context (but don't initialize processor yet - done in filter_frame)
    AVHWDeviceContext *hwctx = (AVHWDeviceContext *)s->hw_device_ctx->data;
    AVD3D11VADeviceContext *d3d11_hwctx = (AVD3D11VADeviceContext *)hwctx->hwctx;

    s->device = d3d11_hwctx->device;
    s->context = d3d11_hwctx->device_context;

    if (!s->device || !s->context) {
        av_log(ctx, AV_LOG_ERROR, "Failed to get valid D3D11 device or context\n");
        return AVERROR(EINVAL);
    }

    ///< Create new hardware frames context for output
    s->hw_frames_ctx_out = av_hwframe_ctx_alloc(s->hw_device_ctx);
    if (!s->hw_frames_ctx_out)
        return AVERROR(ENOMEM);

    AVHWFramesContext *frames_ctx = (AVHWFramesContext *)s->hw_frames_ctx_out->data;
    frames_ctx->format = AV_PIX_FMT_D3D11;
    frames_ctx->sw_format = s->format;
    frames_ctx->width = s->width;
    frames_ctx->height = s->height;
    frames_ctx->initial_pool_size = 10;

    if (ctx->extra_hw_frames > 0)
        frames_ctx->initial_pool_size += ctx->extra_hw_frames;

    AVD3D11VAFramesContext *frames_hwctx = frames_ctx->hwctx;
    frames_hwctx->MiscFlags = 0;
    frames_hwctx->BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_VIDEO_ENCODER;

    ret = av_hwframe_ctx_init(s->hw_frames_ctx_out);
    if (ret < 0) {
        av_buffer_unref(&s->hw_frames_ctx_out);
        return ret;
    }

    outl->hw_frames_ctx = av_buffer_ref(s->hw_frames_ctx_out);
    if (!outl->hw_frames_ctx)
        return AVERROR(ENOMEM);

    av_log(ctx, AV_LOG_VERBOSE, "D3D11 scale config: %dx%d -> %dx%d\n",
           inlink->w, inlink->h, outlink->w, outlink->h);
    return 0;
}

static av_cold void scale_d3d11_uninit(AVFilterContext *ctx) {
    ScaleD3D11Context *s = ctx->priv;

    ///< Release D3D11 resources
    release_d3d11_resources(s);

    ///< Free the hardware device context reference
    av_buffer_unref(&s->hw_frames_ctx_out);
    av_buffer_unref(&s->hw_device_ctx);

    ///< Free option strings
    av_freep(&s->w_expr);
    av_freep(&s->h_expr);
}

static const AVFilterPad scale_d3d11_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = scale_d3d11_filter_frame,
    },
};

static const AVFilterPad scale_d3d11_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = scale_d3d11_config_props,
    },
};

#define OFFSET(x) offsetof(ScaleD3D11Context, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)

static const AVOption scale_d3d11_options[] = {
    { "width",  "Output video width",  OFFSET(w_expr), AV_OPT_TYPE_STRING, {.str = "iw"}, .flags = FLAGS },
    { "height", "Output video height", OFFSET(h_expr), AV_OPT_TYPE_STRING, {.str = "ih"}, .flags = FLAGS },
    { "format", "Output video pixel format", OFFSET(format), AV_OPT_TYPE_PIXEL_FMT, { .i64 = AV_PIX_FMT_NONE }, INT_MIN, INT_MAX, .flags=FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(scale_d3d11);

const FFFilter ff_vf_scale_d3d11 = {
    .p.name           = "scale_d3d11",
    .p.description    = NULL_IF_CONFIG_SMALL("Scale video using Direct3D11"),
    .priv_size        = sizeof(ScaleD3D11Context),
    .p.priv_class     = &scale_d3d11_class,
    .init             = scale_d3d11_init,
    .uninit           = scale_d3d11_uninit,
    FILTER_INPUTS(scale_d3d11_inputs),
    FILTER_OUTPUTS(scale_d3d11_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_D3D11),
    .p.flags          = AVFILTER_FLAG_HWDEVICE,
    .flags_internal   = FF_FILTER_FLAG_HWFRAME_AWARE,
};
