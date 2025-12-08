/**
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

#define COBJMACROS

#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "compat/w32dlfcn.h"

#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_d3d12va.h"
#include "libavutil/hwcontext_d3d12va_internal.h"

#include "filters.h"
#include "scale_eval.h"
#include "video.h"

typedef struct ScaleD3D12Context {
    const AVClass *classCtx;
    char *w_expr;
    char *h_expr;
    enum AVPixelFormat format;

    /* D3D12 objects */
    ID3D12Device *device;
    ID3D12VideoDevice *video_device;
    ID3D12VideoProcessor *video_processor;
    ID3D12CommandQueue *command_queue;
    ID3D12VideoProcessCommandList *command_list;
    ID3D12CommandAllocator *command_allocator;

    /* Synchronization */
    ID3D12Fence *fence;
    UINT64 fence_value;
    HANDLE fence_event;

    /* Buffer references */
    AVBufferRef *hw_device_ctx;
    AVBufferRef *hw_frames_ctx_out;

    /* Dimensions and formats */
    int width, height;
    int input_width, input_height;
    DXGI_FORMAT input_format;
    DXGI_FORMAT output_format;

    /* Color space and frame rate */
    DXGI_COLOR_SPACE_TYPE input_colorspace;
    AVRational input_framerate;

    /* Video processor capabilities */
    D3D12_FEATURE_DATA_VIDEO_PROCESS_SUPPORT process_support;
} ScaleD3D12Context;

static av_cold int scale_d3d12_init(AVFilterContext *ctx) {
    return 0;
}

static void release_d3d12_resources(ScaleD3D12Context *s) {
    UINT64 fence_value;
    HRESULT hr;
    /* Wait for all GPU operations to complete before releasing resources */
    if (s->command_queue && s->fence && s->fence_event) {
        fence_value = s->fence_value - 1;
        hr = ID3D12CommandQueue_Signal(s->command_queue, s->fence, fence_value);
        if (SUCCEEDED(hr)) {
            UINT64 completed = ID3D12Fence_GetCompletedValue(s->fence);
            if (completed < fence_value) {
                hr = ID3D12Fence_SetEventOnCompletion(s->fence, fence_value, s->fence_event);
                if (SUCCEEDED(hr)) {
                    WaitForSingleObject(s->fence_event, INFINITE);
                }
            }
        }
    }

    if (s->fence_event) {
        CloseHandle(s->fence_event);
        s->fence_event = NULL;
    }

    if (s->fence) {
        ID3D12Fence_Release(s->fence);
        s->fence = NULL;
    }

    if (s->command_list) {
        ID3D12VideoProcessCommandList_Release(s->command_list);
        s->command_list = NULL;
    }

    if (s->command_allocator) {
        ID3D12CommandAllocator_Release(s->command_allocator);
        s->command_allocator = NULL;
    }

    if (s->video_processor) {
        ID3D12VideoProcessor_Release(s->video_processor);
        s->video_processor = NULL;
    }

    if (s->video_device) {
        ID3D12VideoDevice_Release(s->video_device);
        s->video_device = NULL;
    }

    if (s->command_queue) {
        ID3D12CommandQueue_Release(s->command_queue);
        s->command_queue = NULL;
    }
}

static DXGI_COLOR_SPACE_TYPE get_dxgi_colorspace(enum AVColorSpace colorspace, enum AVColorTransferCharacteristic trc, int is_10bit)
{
    /* Map FFmpeg color space to DXGI color space */
    if (is_10bit) {
        /* 10-bit formats (P010) */
        if (colorspace == AVCOL_SPC_BT2020_NCL || colorspace == AVCOL_SPC_BT2020_CL) {
            if (trc == AVCOL_TRC_SMPTE2084) {
                return DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_LEFT_P2020;      ///< HDR10
            } else if (trc == AVCOL_TRC_ARIB_STD_B67) {
                return DXGI_COLOR_SPACE_YCBCR_STUDIO_GHLG_TOPLEFT_P2020;    ///< HLG
            } else {
                return DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P2020;
            }
        } else {
            return DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709;             ///< Rec.709 10-bit
        }
    } else {
        /* 8-bit formats (NV12) */
        if (colorspace == AVCOL_SPC_BT2020_NCL || colorspace == AVCOL_SPC_BT2020_CL) {
            return DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P2020;
        } else if (colorspace == AVCOL_SPC_BT470BG || colorspace == AVCOL_SPC_SMPTE170M) {
            return DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P601;
        } else {
            return DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709;             ///< Default to Rec.709
        }
    }
}

static AVRational get_input_framerate(AVFilterContext *ctx, AVFilterLink *inlink, AVFrame *in)
{
    AVRational framerate = {0, 0};

    if (in->duration > 0 && inlink->time_base.num > 0 && inlink->time_base.den > 0) {
        /*
        * Calculate framerate from frame duration and timebase
        * framerate = 1 / (duration * timebase)
        */
        av_reduce(&framerate.num, &framerate.den,
                  inlink->time_base.den, in->duration * inlink->time_base.num,
                  INT_MAX);
    } else if (inlink->time_base.num > 0 && inlink->time_base.den > 0) {
        /* Estimate from timebase (inverse of timebase is often the framerate) */
        framerate.num = inlink->time_base.den;
        framerate.den = inlink->time_base.num;
    } else {
        /* Default to 30fps if framerate cannot be determined */
        framerate.num = 30;
        framerate.den = 1;
        av_log(ctx, AV_LOG_WARNING, "Input framerate not determinable, defaulting to 30fps\n");
    }

    return framerate;
}

static int scale_d3d12_configure_processor(ScaleD3D12Context *s, AVFilterContext *ctx) {
    HRESULT hr;

    if (s->output_format == DXGI_FORMAT_UNKNOWN) {
        av_log(ctx, AV_LOG_ERROR, "Output format not initialized\n");
        return AVERROR(EINVAL);
    }

    AVHWDeviceContext *hwctx = (AVHWDeviceContext *)s->hw_device_ctx->data;
    AVD3D12VADeviceContext *d3d12_hwctx = (AVD3D12VADeviceContext *)hwctx->hwctx;
    s->device = d3d12_hwctx->device;

    av_log(ctx, AV_LOG_VERBOSE, "Configuring D3D12 video processor: %dx%d -> %dx%d\n",
           s->input_width, s->input_height, s->width, s->height);

    hr = ID3D12Device_QueryInterface(s->device, &IID_ID3D12VideoDevice, (void **)&s->video_device);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to get D3D12 video device interface: HRESULT 0x%lX\n", hr);
        return AVERROR_EXTERNAL;
    }

    D3D12_COMMAND_QUEUE_DESC queue_desc = {
        .Type = D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS,
        .Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
        .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
        .NodeMask = 0
    };

    hr = ID3D12Device_CreateCommandQueue(s->device, &queue_desc, &IID_ID3D12CommandQueue, (void **)&s->command_queue);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create command queue: HRESULT 0x%lX\n", hr);
        return AVERROR_EXTERNAL;
    }

    s->process_support.NodeIndex = 0;

    s->process_support.InputSample.Format.Format     = s->input_format;
    s->process_support.InputSample.Format.ColorSpace = s->input_colorspace;
    s->process_support.InputSample.Width             = s->input_width;
    s->process_support.InputSample.Height            = s->input_height;
    s->process_support.InputFrameRate.Numerator      = s->input_framerate.num;
    s->process_support.InputFrameRate.Denominator    = s->input_framerate.den;
    s->process_support.InputFieldType                = D3D12_VIDEO_FIELD_TYPE_NONE;
    s->process_support.InputStereoFormat             = D3D12_VIDEO_FRAME_STEREO_FORMAT_NONE;

    s->process_support.OutputFormat.Format           = s->output_format;
    s->process_support.OutputFormat.ColorSpace       = s->input_colorspace;
    s->process_support.OutputFrameRate.Numerator     = s->input_framerate.num;
    s->process_support.OutputFrameRate.Denominator   = s->input_framerate.den;
    s->process_support.OutputStereoFormat            = D3D12_VIDEO_FRAME_STEREO_FORMAT_NONE;

    hr = ID3D12VideoDevice_CheckFeatureSupport(
        s->video_device,
        D3D12_FEATURE_VIDEO_PROCESS_SUPPORT,
        &s->process_support,
        sizeof(s->process_support)
    );

    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Video process feature not supported: HRESULT 0x%lX\n", hr);
        return AVERROR_EXTERNAL;
    }

    if (!(s->process_support.SupportFlags & D3D12_VIDEO_PROCESS_SUPPORT_FLAG_SUPPORTED)) {
        av_log(ctx, AV_LOG_ERROR, "Video process configuration not supported by hardware\n");
        return AVERROR_EXTERNAL;
    }

    D3D12_VIDEO_PROCESS_OUTPUT_STREAM_DESC processor_output_desc = {
        .Format                         = s->output_format,
        .ColorSpace                     = s->input_colorspace,
        .AlphaFillMode                  = D3D12_VIDEO_PROCESS_ALPHA_FILL_MODE_OPAQUE,
        .AlphaFillModeSourceStreamIndex = 0,
        .BackgroundColor                = { 0.0f, 0.0f, 0.0f, 1.0f },
        .FrameRate                      = { s->input_framerate.num, s->input_framerate.den },
        .EnableStereo                   = FALSE,
    };

    D3D12_VIDEO_PROCESS_INPUT_STREAM_DESC processor_input_desc = {
        .Format                 = s->input_format,
        .ColorSpace             = s->input_colorspace,
        .SourceAspectRatio      = { s->input_width, s->input_height },
        .DestinationAspectRatio = { s->width, s->height },
        .FrameRate              = { s->input_framerate.num, s->input_framerate.den },
        .StereoFormat           = D3D12_VIDEO_FRAME_STEREO_FORMAT_NONE,
        .FieldType              = D3D12_VIDEO_FIELD_TYPE_NONE,
        .DeinterlaceMode        = D3D12_VIDEO_PROCESS_DEINTERLACE_FLAG_NONE,
        .EnableOrientation      = FALSE,
        .FilterFlags            = D3D12_VIDEO_PROCESS_FILTER_FLAG_NONE,
        .SourceSizeRange        = {
            .MaxWidth  = s->input_width,
            .MaxHeight = s->input_height,
            .MinWidth  = s->input_width,
            .MinHeight = s->input_height
        },
        .DestinationSizeRange  = {
            .MaxWidth  = s->width,
            .MaxHeight = s->height,
            .MinWidth  = s->width,
            .MinHeight = s->height
        },
        .EnableAlphaBlending   = FALSE,
        .LumaKey               = { .Enable = FALSE, .Lower = 0.0f, .Upper = 1.0f },
        .NumPastFrames         = 0,
        .NumFutureFrames       = 0,
        .EnableAutoProcessing  = FALSE,
    };

    /* If pixel aspect ratio adjustment is not supported, set to 1:1 and warn */
    if (!(s->process_support.FeatureSupport & D3D12_VIDEO_PROCESS_FEATURE_FLAG_PIXEL_ASPECT_RATIO)) {
        processor_input_desc.SourceAspectRatio.Numerator        = 1;
        processor_input_desc.SourceAspectRatio.Denominator      = 1;
        processor_input_desc.DestinationAspectRatio.Numerator   = 1;
        processor_input_desc.DestinationAspectRatio.Denominator = 1;
        av_log(ctx, AV_LOG_WARNING, "Pixel aspect ratio adjustment not supported by hardware\n");
    }

    hr = ID3D12VideoDevice_CreateVideoProcessor(
        s->video_device,
        0,
        &processor_output_desc,
        1,
        &processor_input_desc,
        &IID_ID3D12VideoProcessor,
        (void **)&s->video_processor
    );

    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create video processor: HRESULT 0x%lX\n", hr);
        return AVERROR_EXTERNAL;
    }

    hr = ID3D12Device_CreateCommandAllocator(
        s->device,
        D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS,
        &IID_ID3D12CommandAllocator,
        (void **)&s->command_allocator
    );

    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create command allocator: HRESULT 0x%lX\n", hr);
        return AVERROR_EXTERNAL;
    }

    hr = ID3D12Device_CreateCommandList(
        s->device,
        0,
        D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS,
        s->command_allocator,
        NULL,
        &IID_ID3D12VideoProcessCommandList,
        (void **)&s->command_list
    );

    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create command list: HRESULT 0x%lX\n", hr);
        return AVERROR_EXTERNAL;
    }

    ID3D12VideoProcessCommandList_Close(s->command_list);

    hr = ID3D12Device_CreateFence(s->device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&s->fence);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create fence: HRESULT 0x%lX\n", hr);
        return AVERROR_EXTERNAL;
    }

    s->fence_value = 1;
    s->fence_event = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!s->fence_event) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create fence event\n");
        return AVERROR_EXTERNAL;
    }

    av_log(ctx, AV_LOG_VERBOSE, "D3D12 video processor successfully configured\n");
    return 0;
}

static int scale_d3d12_filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx  = inlink->dst;
    ScaleD3D12Context *s  = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame          *out = NULL;
    int ret = 0;
    HRESULT hr;

    if (!in) {
        av_log(ctx, AV_LOG_ERROR, "Null input frame\n");
        return AVERROR(EINVAL);
    }

    if (!in->hw_frames_ctx) {
        av_log(ctx, AV_LOG_ERROR, "No hardware frames context in input frame\n");
        av_frame_free(&in);
        return AVERROR(EINVAL);
    }

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

    if (!s->video_processor) {
        AVHWFramesContext *input_frames_ctx = (AVHWFramesContext *)in->hw_frames_ctx->data;

        s->input_width = input_frames_ctx->width;
        s->input_height = input_frames_ctx->height;

        AVD3D12VAFramesContext *input_hwctx = (AVD3D12VAFramesContext *)input_frames_ctx->hwctx;
        s->input_format = input_hwctx->format;

        if (s->input_format == DXGI_FORMAT_UNKNOWN) {
            switch (input_frames_ctx->sw_format) {
                case AV_PIX_FMT_NV12:
                    s->input_format = DXGI_FORMAT_NV12;
                    break;
                case AV_PIX_FMT_P010:
                    s->input_format = DXGI_FORMAT_P010;
                    break;
                default:
                    av_log(ctx, AV_LOG_ERROR, "Unsupported input format\n");
                    ret = AVERROR(EINVAL);
                    goto fail;
            }
        }

        int is_10bit = (s->input_format == DXGI_FORMAT_P010);
        s->input_colorspace = get_dxgi_colorspace(in->colorspace, in->color_trc, is_10bit);

        s->input_framerate = get_input_framerate(ctx, inlink, in);

        av_log(ctx, AV_LOG_VERBOSE, "Input format: %dx%d, DXGI format: %d, colorspace: %d, framerate: %d/%d\n",
               s->input_width, s->input_height, s->input_format, s->input_colorspace,
               s->input_framerate.num, s->input_framerate.den);

        ret = scale_d3d12_configure_processor(s, ctx);
        if (ret < 0) {
            av_log(ctx, AV_LOG_ERROR, "Failed to configure processor\n");
            goto fail;
        }
    }

    AVD3D12VAFrame *input_frame  = (AVD3D12VAFrame *)in->data[0];
    AVD3D12VAFrame *output_frame = (AVD3D12VAFrame *)out->data[0];

    if (!input_frame || !output_frame) {
        av_log(ctx, AV_LOG_ERROR, "Invalid frame pointers\n");
        ret = AVERROR(EINVAL);
        goto fail;
    }

    ID3D12Resource *input_resource  = input_frame->texture;
    ID3D12Resource *output_resource = output_frame->texture;

    if (!input_resource || !output_resource) {
        av_log(ctx, AV_LOG_ERROR, "Invalid D3D12 resources in frames\n");
        ret = AVERROR(EINVAL);
        goto fail;
    }

    /* Wait for input frame's fence before accessing it */
    if (input_frame->sync_ctx.fence && input_frame->sync_ctx.fence_value > 0) {
        UINT64 completed = ID3D12Fence_GetCompletedValue(input_frame->sync_ctx.fence);
        if (completed < input_frame->sync_ctx.fence_value) {
            hr = ID3D12CommandQueue_Wait(s->command_queue, input_frame->sync_ctx.fence, input_frame->sync_ctx.fence_value);
            if (FAILED(hr)) {
                av_log(ctx, AV_LOG_ERROR, "Failed to wait for input fence: HRESULT 0x%lX\n", hr);
                ret = AVERROR_EXTERNAL;
                goto fail;
            }
        }
    }

    hr = ID3D12CommandAllocator_Reset(s->command_allocator);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to reset command allocator: HRESULT 0x%lX\n", hr);
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

    hr = ID3D12VideoProcessCommandList_Reset(s->command_list, s->command_allocator);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to reset command list: HRESULT 0x%lX\n", hr);
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

    D3D12_RESOURCE_BARRIER barriers[2] = {
        {
            .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
            .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
            .Transition = {
                .pResource   = input_resource,
                .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                .StateBefore = D3D12_RESOURCE_STATE_COMMON,
                .StateAfter  = D3D12_RESOURCE_STATE_VIDEO_PROCESS_READ
            }
        },
        {
            .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
            .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
            .Transition = {
                .pResource   = output_resource,
                .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                .StateBefore = D3D12_RESOURCE_STATE_COMMON,
                .StateAfter  = D3D12_RESOURCE_STATE_VIDEO_PROCESS_WRITE
            }
        }
    };

    ID3D12VideoProcessCommandList_ResourceBarrier(s->command_list, 2, barriers);

    D3D12_VIDEO_PROCESS_INPUT_STREAM_ARGUMENTS input_args = {0};

    input_args.InputStream[0].pTexture2D = input_resource;
    input_args.Transform.SourceRectangle.right       = s->input_width;
    input_args.Transform.SourceRectangle.bottom      = s->input_height;
    input_args.Transform.DestinationRectangle.right  = s->width;
    input_args.Transform.DestinationRectangle.bottom = s->height;
    input_args.Transform.Orientation = D3D12_VIDEO_PROCESS_ORIENTATION_DEFAULT;

    input_args.Flags = D3D12_VIDEO_PROCESS_INPUT_STREAM_FLAG_NONE;

    input_args.RateInfo.OutputIndex = 0;
    input_args.RateInfo.InputFrameOrField = 0;

    memset(input_args.FilterLevels, 0, sizeof(input_args.FilterLevels));

    input_args.AlphaBlending.Enable = FALSE;
    input_args.AlphaBlending.Alpha = 1.0f;

    D3D12_VIDEO_PROCESS_OUTPUT_STREAM_ARGUMENTS output_args = {0};

    output_args.OutputStream[0].pTexture2D = output_resource;
    output_args.TargetRectangle.right  = s->width;
    output_args.TargetRectangle.bottom = s->height;

    ID3D12VideoProcessCommandList_ProcessFrames(
        s->command_list,
        s->video_processor,
        &output_args,
        1,
        &input_args
    );

    for (int i = 0; i < 2; i++) {
        FFSWAP(D3D12_RESOURCE_STATES, barriers[i].Transition.StateBefore, barriers[i].Transition.StateAfter);
    }
    ID3D12VideoProcessCommandList_ResourceBarrier(s->command_list, 2, barriers);

    hr = ID3D12VideoProcessCommandList_Close(s->command_list);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to close command list: HRESULT 0x%lX\n", hr);
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

    ID3D12CommandList *cmd_lists[] = { (ID3D12CommandList *)s->command_list };
    ID3D12CommandQueue_ExecuteCommandLists(s->command_queue, 1, cmd_lists);

    hr = ID3D12CommandQueue_Signal(s->command_queue, s->fence, s->fence_value);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to signal fence: HRESULT 0x%lX\n", hr);
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

    output_frame->sync_ctx.fence = s->fence;
    output_frame->sync_ctx.fence_value = s->fence_value;
    ID3D12Fence_AddRef(s->fence);  ///< Increment reference count

    s->fence_value++;

    ret = av_frame_copy_props(out, in);
    if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to copy frame properties\n");
        goto fail;
    }

    out->width = s->width;
    out->height = s->height;
    out->format = AV_PIX_FMT_D3D12;

    av_frame_free(&in);

    return ff_filter_frame(outlink, out);

fail:
    av_frame_free(&in);
    av_frame_free(&out);
    return ret;
}

static int scale_d3d12_config_props(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    ScaleD3D12Context *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    FilterLink      *inl = ff_filter_link(inlink);
    FilterLink     *outl = ff_filter_link(outlink);
    int ret;

    release_d3d12_resources(s);

    av_buffer_unref(&s->hw_frames_ctx_out);
    av_buffer_unref(&s->hw_device_ctx);

    ret = ff_scale_eval_dimensions(s, s->w_expr, s->h_expr, inlink, outlink, &s->width, &s->height);
    if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to evaluate dimensions\n");
        return ret;
    }

    /* Adjust dimensions to meet codec/hardware alignment requirements */
    ff_scale_adjust_dimensions(inlink, &s->width, &s->height, 0, 1, 1.f);

    outlink->w = s->width;
    outlink->h = s->height;

    if (!inl->hw_frames_ctx) {
        av_log(ctx, AV_LOG_ERROR, "No hw_frames_ctx available on input link\n");
        return AVERROR(EINVAL);
    }

    if (!s->hw_device_ctx) {
        AVHWFramesContext *in_frames_ctx = (AVHWFramesContext *)inl->hw_frames_ctx->data;
        s->hw_device_ctx = av_buffer_ref(in_frames_ctx->device_ref);
        if (!s->hw_device_ctx) {
            av_log(ctx, AV_LOG_ERROR, "Failed to initialize filter hardware device context\n");
            return AVERROR(ENOMEM);
        }
    }

    AVHWDeviceContext *hwctx = (AVHWDeviceContext *)s->hw_device_ctx->data;
    AVD3D12VADeviceContext *d3d12_hwctx = (AVD3D12VADeviceContext *)hwctx->hwctx;

    s->device = d3d12_hwctx->device;

    if (!s->device) {
        av_log(ctx, AV_LOG_ERROR, "Failed to get valid D3D12 device\n");
        return AVERROR(EINVAL);
    }

    s->hw_frames_ctx_out = av_hwframe_ctx_alloc(s->hw_device_ctx);
    if (!s->hw_frames_ctx_out)
        return AVERROR(ENOMEM);

    AVHWFramesContext *frames_ctx = (AVHWFramesContext *)s->hw_frames_ctx_out->data;
    AVHWFramesContext *in_frames_ctx = (AVHWFramesContext *)inl->hw_frames_ctx->data;

    if (s->format == AV_PIX_FMT_NONE) {
        /* If format is not specified, use the same format as input */
        frames_ctx->sw_format = in_frames_ctx->sw_format;
        s->format = in_frames_ctx->sw_format;
        av_log(ctx, AV_LOG_VERBOSE, "D3D12 scale output format not specified, using input format: %s\n",
               av_get_pix_fmt_name(s->format));
    } else {
        frames_ctx->sw_format = s->format;
    }

    /* Set output format based on sw_format */
    switch (frames_ctx->sw_format) {
        case AV_PIX_FMT_NV12:
            s->output_format = DXGI_FORMAT_NV12;
            break;
        case AV_PIX_FMT_P010:
            s->output_format = DXGI_FORMAT_P010;
            break;
        default:
            av_log(ctx, AV_LOG_ERROR, "Unsupported output format: %s\n",
                   av_get_pix_fmt_name(frames_ctx->sw_format));
            av_buffer_unref(&s->hw_frames_ctx_out);
            return AVERROR(EINVAL);
    }

    frames_ctx->width  = s->width;
    frames_ctx->height = s->height;
    frames_ctx->format = AV_PIX_FMT_D3D12;
    frames_ctx->initial_pool_size = 10;

    if (ctx->extra_hw_frames > 0)
        frames_ctx->initial_pool_size += ctx->extra_hw_frames;

    AVD3D12VAFramesContext *frames_hwctx = frames_ctx->hwctx;

    /*
    * Set D3D12 resource flags for video processing
    * ALLOW_RENDER_TARGET is needed for video processor output
    */
    frames_hwctx->format = s->output_format;
    frames_hwctx->resource_flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    frames_hwctx->heap_flags = D3D12_HEAP_FLAG_NONE;

    ret = av_hwframe_ctx_init(s->hw_frames_ctx_out);
    if (ret < 0) {
        av_buffer_unref(&s->hw_frames_ctx_out);
        return ret;
    }

    outl->hw_frames_ctx = av_buffer_ref(s->hw_frames_ctx_out);
    if (!outl->hw_frames_ctx)
        return AVERROR(ENOMEM);

    av_log(ctx, AV_LOG_VERBOSE, "D3D12 scale config: %dx%d -> %dx%d\n",
           inlink->w, inlink->h, outlink->w, outlink->h);
    return 0;
}

static av_cold void scale_d3d12_uninit(AVFilterContext *ctx) {
    ScaleD3D12Context *s = ctx->priv;

    release_d3d12_resources(s);

    av_buffer_unref(&s->hw_frames_ctx_out);
    av_buffer_unref(&s->hw_device_ctx);

    av_freep(&s->w_expr);
    av_freep(&s->h_expr);
}

static const AVFilterPad scale_d3d12_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = scale_d3d12_filter_frame,
    },
};

static const AVFilterPad scale_d3d12_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = scale_d3d12_config_props,
    },
};

#define OFFSET(x) offsetof(ScaleD3D12Context, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)

static const AVOption scale_d3d12_options[] = {
    { "w",  "Output video width",       OFFSET(w_expr), AV_OPT_TYPE_STRING,    {.str = "iw"}, .flags = FLAGS },
    { "h", "Output video height",       OFFSET(h_expr), AV_OPT_TYPE_STRING,    {.str = "ih"}, .flags = FLAGS },
    { "format", "Output video pixel format", OFFSET(format), AV_OPT_TYPE_PIXEL_FMT, { .i64 = AV_PIX_FMT_NONE }, INT_MIN, INT_MAX, .flags=FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(scale_d3d12);

const FFFilter ff_vf_scale_d3d12 = {
    .p.name           = "scale_d3d12",
    .p.description    = NULL_IF_CONFIG_SMALL("Scale video using Direct3D12"),
    .priv_size        = sizeof(ScaleD3D12Context),
    .p.priv_class     = &scale_d3d12_class,
    .init             = scale_d3d12_init,
    .uninit           = scale_d3d12_uninit,
    FILTER_INPUTS(scale_d3d12_inputs),
    FILTER_OUTPUTS(scale_d3d12_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_D3D12),
    .p.flags          = AVFILTER_FLAG_HWDEVICE,
    .flags_internal   = FF_FILTER_FLAG_HWFRAME_AWARE,
};
