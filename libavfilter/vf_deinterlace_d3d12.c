/*
 * D3D12VA deinterlacing filter
 *
 * Copyright (c) 2026 Advanced Micro Devices, Inc.
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

#define COBJMACROS

#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_d3d12va.h"

#include "filters.h"
#include "video.h"

#define MAX_REFERENCES 8

/**
 * Deinterlace mode enumeration
 * Maps to D3D12_VIDEO_PROCESS_DEINTERLACE_FLAG values
 */
enum DeinterlaceD3D12Mode {
    DEINT_D3D12_MODE_DEFAULT        = 0,  // Use best available mode
    DEINT_D3D12_MODE_BOB            = 1,  // Bob deinterlacing (simple field interpolation)
    DEINT_D3D12_MODE_CUSTOM         = 2,  // Driver-defined advanced deinterlacing
};

typedef struct DeinterlaceD3D12Context {
    const AVClass *classCtx;

    /* Filter options */
    int mode;           // Deinterlace mode (default, bob, custom)
    int field_rate;     // Output field rate (1 = frame rate, 2 = field rate)
    int auto_enable;    // Only deinterlace interlaced frames

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
    DXGI_FORMAT input_format;

    /* Color space and frame rate */
    DXGI_COLOR_SPACE_TYPE input_colorspace;
    AVRational input_framerate;

    /* Video processor capabilities */
    D3D12_FEATURE_DATA_VIDEO_PROCESS_SUPPORT process_support;
    D3D12_VIDEO_PROCESS_DEINTERLACE_FLAGS supported_deint_flags;

    /* Frame queue for temporal references */
    int queue_depth;
    int queue_count;
    AVFrame *frame_queue[MAX_REFERENCES];

    /* State tracking */
    int eof;
    int64_t prev_pts;
    int num_past_frames;
    int num_future_frames;
    int current_frame_index;
    int extra_delay_for_timestamps;
    int initial_fill_done;

    /* Processor configured flag */
    int processor_configured;
} DeinterlaceD3D12Context;

static av_cold int deint_d3d12_init(AVFilterContext *ctx)
{
    DeinterlaceD3D12Context *s = ctx->priv;
    s->fence_value = 1;
    s->processor_configured = 0;
    return 0;
}

static void release_d3d12_resources(DeinterlaceD3D12Context *s)
{
    UINT64 fence_value;
    HRESULT hr;

    /* Wait for all GPU operations to complete before releasing resources */
    if (s->command_queue && s->fence && s->fence_event) {
        fence_value = s->fence_value;
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

static void deint_d3d12_clear_queue(DeinterlaceD3D12Context *s)
{
    for (int i = 0; i < s->queue_count; i++) {
        av_frame_free(&s->frame_queue[i]);
    }
    s->queue_count = 0;
}

static DXGI_COLOR_SPACE_TYPE get_dxgi_colorspace(enum AVColorSpace colorspace,
                                                  enum AVColorTransferCharacteristic trc,
                                                  int is_10bit)
{
    /* Map FFmpeg color space to DXGI color space */
    if (is_10bit) {
        /* 10-bit formats (P010) */
        if (colorspace == AVCOL_SPC_BT2020_NCL || colorspace == AVCOL_SPC_BT2020_CL) {
            if (trc == AVCOL_TRC_SMPTE2084) {
                return DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_LEFT_P2020;      // HDR10
            } else if (trc == AVCOL_TRC_ARIB_STD_B67) {
                return DXGI_COLOR_SPACE_YCBCR_STUDIO_GHLG_TOPLEFT_P2020;    // HLG
            } else {
                return DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P2020;
            }
        } else {
            return DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709;             // Rec.709 10-bit
        }
    } else {
        /* 8-bit formats (NV12) */
        if (colorspace == AVCOL_SPC_BT2020_NCL || colorspace == AVCOL_SPC_BT2020_CL) {
            return DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P2020;
        } else if (colorspace == AVCOL_SPC_BT470BG || colorspace == AVCOL_SPC_SMPTE170M) {
            return DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P601;
        } else {
            return DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709;             // Default to Rec.709
        }
    }
}

static AVRational get_input_framerate(AVFilterContext *ctx, AVFilterLink *inlink, AVFrame *in)
{
    AVRational framerate = {0, 0};

    if (in->duration > 0 && inlink->time_base.num > 0 && inlink->time_base.den > 0) {
        av_reduce(&framerate.num, &framerate.den,
                  inlink->time_base.den, in->duration * inlink->time_base.num,
                  INT_MAX);
    } else if (inlink->time_base.num > 0 && inlink->time_base.den > 0) {
        framerate.num = inlink->time_base.den;
        framerate.den = inlink->time_base.num;
    } else {
        framerate.num = 30;
        framerate.den = 1;
        av_log(ctx, AV_LOG_WARNING, "Input framerate not determinable, defaulting to 30fps\n");
    }

    return framerate;
}

static D3D12_VIDEO_PROCESS_DEINTERLACE_FLAGS get_deint_mode(DeinterlaceD3D12Context *s,
                                                             AVFilterContext *ctx)
{
    D3D12_VIDEO_PROCESS_DEINTERLACE_FLAGS mode_flag;

    switch (s->mode) {
    case DEINT_D3D12_MODE_BOB:
        mode_flag = D3D12_VIDEO_PROCESS_DEINTERLACE_FLAG_BOB;
        break;
    case DEINT_D3D12_MODE_CUSTOM:
        mode_flag = D3D12_VIDEO_PROCESS_DEINTERLACE_FLAG_CUSTOM;
        break;
    case DEINT_D3D12_MODE_DEFAULT:
    default:
        /* Select best available mode */
        if (s->supported_deint_flags & D3D12_VIDEO_PROCESS_DEINTERLACE_FLAG_CUSTOM) {
            mode_flag = D3D12_VIDEO_PROCESS_DEINTERLACE_FLAG_CUSTOM;
            av_log(ctx, AV_LOG_VERBOSE, "Using custom (driver-defined) deinterlacing\n");
        } else if (s->supported_deint_flags & D3D12_VIDEO_PROCESS_DEINTERLACE_FLAG_BOB) {
            mode_flag = D3D12_VIDEO_PROCESS_DEINTERLACE_FLAG_BOB;
            av_log(ctx, AV_LOG_VERBOSE, "Using bob deinterlacing\n");
        } else {
            mode_flag = D3D12_VIDEO_PROCESS_DEINTERLACE_FLAG_BOB;
            av_log(ctx, AV_LOG_WARNING, "No deinterlacing modes reported, trying bob\n");
        }
        break;
    }

    /* Verify requested mode is supported */
    if (mode_flag != D3D12_VIDEO_PROCESS_DEINTERLACE_FLAG_BOB &&
        !(s->supported_deint_flags & mode_flag)) {
        av_log(ctx, AV_LOG_WARNING, "Requested deinterlace mode not supported, falling back to bob\n");
        mode_flag = D3D12_VIDEO_PROCESS_DEINTERLACE_FLAG_BOB;
    }

    return mode_flag;
}

static int deint_d3d12_configure_processor(DeinterlaceD3D12Context *s,
                                            AVFilterContext *ctx,
                                            AVFrame *in)
{
    HRESULT hr;
    AVHWDeviceContext *hwctx = (AVHWDeviceContext *)s->hw_device_ctx->data;
    AVD3D12VADeviceContext *d3d12_hwctx = (AVD3D12VADeviceContext *)hwctx->hwctx;
    D3D12_VIDEO_PROCESS_DEINTERLACE_FLAGS deint_mode;
    D3D12_VIDEO_FIELD_TYPE field_type;

    s->device = d3d12_hwctx->device;

    av_log(ctx, AV_LOG_VERBOSE, "Configuring D3D12 deinterlace processor: %dx%d\n",
           s->width, s->height);

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

    /* Determine field type from input frame */
    if (in->flags & AV_FRAME_FLAG_TOP_FIELD_FIRST) {
        field_type = D3D12_VIDEO_FIELD_TYPE_INTERLACED_TOP_FIELD_FIRST;
        av_log(ctx, AV_LOG_VERBOSE, "Input field order: Top Field First\n");
    } else {
        field_type = D3D12_VIDEO_FIELD_TYPE_INTERLACED_BOTTOM_FIELD_FIRST;
        av_log(ctx, AV_LOG_VERBOSE, "Input field order: Bottom Field First\n");
    }

    /* Check deinterlacing support */
    s->process_support.NodeIndex = 0;
    s->process_support.InputSample.Format.Format     = s->input_format;
    s->process_support.InputSample.Format.ColorSpace = s->input_colorspace;
    s->process_support.InputSample.Width             = s->width;
    s->process_support.InputSample.Height            = s->height;
    s->process_support.InputFrameRate.Numerator      = s->input_framerate.num;
    s->process_support.InputFrameRate.Denominator    = s->input_framerate.den;
    s->process_support.InputFieldType                = field_type;
    s->process_support.InputStereoFormat             = D3D12_VIDEO_FRAME_STEREO_FORMAT_NONE;

    s->process_support.OutputFormat.Format           = s->input_format;
    s->process_support.OutputFormat.ColorSpace       = s->input_colorspace;
    s->process_support.OutputFrameRate.Numerator     = s->input_framerate.num * s->field_rate;
    s->process_support.OutputFrameRate.Denominator   = s->input_framerate.den;
    s->process_support.OutputStereoFormat            = D3D12_VIDEO_FRAME_STEREO_FORMAT_NONE;

    hr = ID3D12VideoDevice_CheckFeatureSupport(
        s->video_device,
        D3D12_FEATURE_VIDEO_PROCESS_SUPPORT,
        &s->process_support,
        sizeof(s->process_support)
    );

    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Video process feature check failed: HRESULT 0x%lX\n", hr);
        return AVERROR_EXTERNAL;
    }

    if (!(s->process_support.SupportFlags & D3D12_VIDEO_PROCESS_SUPPORT_FLAG_SUPPORTED)) {
        av_log(ctx, AV_LOG_ERROR, "Video process configuration not supported by hardware\n");
        return AVERROR(ENOSYS);
    }

    /* Store supported deinterlace flags */
    s->supported_deint_flags = s->process_support.DeinterlaceSupport;

    av_log(ctx, AV_LOG_VERBOSE, "Deinterlace support flags: 0x%X\n", s->supported_deint_flags);

    if (!(s->supported_deint_flags & (D3D12_VIDEO_PROCESS_DEINTERLACE_FLAG_BOB |
                                       D3D12_VIDEO_PROCESS_DEINTERLACE_FLAG_CUSTOM))) {
        av_log(ctx, AV_LOG_ERROR, "No deinterlacing modes supported by hardware\n");
        return AVERROR(ENOSYS);
    }

    deint_mode = get_deint_mode(s, ctx);

    /* Query reference frame requirements from hardware */
#if CONFIG_D3D12_VIDEO_PROCESS_REFERENCE_INFO
    D3D12_FEATURE_DATA_VIDEO_PROCESS_REFERENCE_INFO ref_info = {
        .NodeIndex          = 0,
        .DeinterlaceMode    = deint_mode,
        .Filters            = D3D12_VIDEO_PROCESS_FILTER_FLAG_NONE,
        .FeatureSupport     = D3D12_VIDEO_PROCESS_FEATURE_FLAG_NONE,
        .InputFrameRate     = { s->input_framerate.num, s->input_framerate.den },
        .OutputFrameRate    = { s->input_framerate.num * s->field_rate, s->input_framerate.den },
        .EnableAutoProcessing = FALSE,
    };

    hr = ID3D12VideoDevice_CheckFeatureSupport(
        s->video_device,
        D3D12_FEATURE_VIDEO_PROCESS_REFERENCE_INFO,
        &ref_info,
        sizeof(ref_info)
    );

    if (SUCCEEDED(hr)) {
        s->num_past_frames   = ref_info.PastFrames;
        s->num_future_frames = ref_info.FutureFrames;
        av_log(ctx, AV_LOG_VERBOSE,
                "Reference frames from hardware: past=%d, future=%d\n",
                s->num_past_frames, s->num_future_frames);
    } else {
        av_log(ctx, AV_LOG_WARNING,
                "Failed to query reference info (HRESULT 0x%lX), using defaults\n", hr);
        s->num_past_frames   = (deint_mode == D3D12_VIDEO_PROCESS_DEINTERLACE_FLAG_CUSTOM) ? 1 : 0;
        s->num_future_frames = 0;
    }
#else
    av_log(ctx, AV_LOG_VERBOSE,
           "Reference info query not available in SDK, using defaults\n");
    s->num_past_frames   = (deint_mode == D3D12_VIDEO_PROCESS_DEINTERLACE_FLAG_CUSTOM) ? 1 : 0;
    s->num_future_frames = 0;
#endif

    /* May need 1 extra slot for PTS calculation.*/
    s->extra_delay_for_timestamps = (s->field_rate == 2 && s->num_future_frames == 0) ? 1 : 0;

    s->queue_depth = s->num_past_frames + s->num_future_frames + s->extra_delay_for_timestamps + 1;

    if (s->queue_depth > MAX_REFERENCES) {
        av_log(ctx, AV_LOG_ERROR, "Required queue depth (%d) exceeds maximum (%d)\n",
               s->queue_depth, MAX_REFERENCES);
        return AVERROR(ENOSYS);
    }

    s->current_frame_index = s->num_past_frames;

    av_log(ctx, AV_LOG_VERBOSE, "Queue depth: %d (past: %d, future: %d, extra: %d)\n",
           s->queue_depth, s->num_past_frames, s->num_future_frames,
           s->extra_delay_for_timestamps);

    D3D12_VIDEO_PROCESS_OUTPUT_STREAM_DESC processor_output_desc = {
        .Format                         = s->input_format,
        .ColorSpace                     = s->input_colorspace,
        .AlphaFillMode                  = D3D12_VIDEO_PROCESS_ALPHA_FILL_MODE_OPAQUE,
        .AlphaFillModeSourceStreamIndex = 0,
        .BackgroundColor                = { 0.0f, 0.0f, 0.0f, 1.0f },
        .FrameRate                      = { s->input_framerate.num * s->field_rate, s->input_framerate.den },
        .EnableStereo                   = FALSE,
    };

    D3D12_VIDEO_PROCESS_INPUT_STREAM_DESC processor_input_desc = {
        .Format                 = s->input_format,
        .ColorSpace             = s->input_colorspace,
        .SourceAspectRatio      = { s->width, s->height },
        .DestinationAspectRatio = { s->width, s->height },
        .FrameRate              = { s->input_framerate.num, s->input_framerate.den },
        .StereoFormat           = D3D12_VIDEO_FRAME_STEREO_FORMAT_NONE,
        .FieldType              = field_type,
        .DeinterlaceMode        = deint_mode,
        .EnableOrientation      = FALSE,
        .FilterFlags            = D3D12_VIDEO_PROCESS_FILTER_FLAG_NONE,
        .SourceSizeRange        = {
            .MaxWidth  = s->width,
            .MaxHeight = s->height,
            .MinWidth  = s->width,
            .MinHeight = s->height
        },
        .DestinationSizeRange  = {
            .MaxWidth  = s->width,
            .MaxHeight = s->height,
            .MinWidth  = s->width,
            .MinHeight = s->height
        },
        .EnableAlphaBlending   = FALSE,
        .LumaKey               = { .Enable = FALSE, .Lower = 0.0f, .Upper = 1.0f },
        .NumPastFrames         = s->num_past_frames,
        .NumFutureFrames       = s->num_future_frames,
        .EnableAutoProcessing  = FALSE,
    };

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

    s->processor_configured = 1;
    av_log(ctx, AV_LOG_VERBOSE, "D3D12 deinterlace processor successfully configured\n");
    return 0;
}

static void add_resource_barrier(D3D12_RESOURCE_BARRIER *barriers, int *count,
                                         ID3D12Resource *resource,
                                         D3D12_RESOURCE_STATES before,
                                         D3D12_RESOURCE_STATES after)
{
    barriers[(*count)++] = (D3D12_RESOURCE_BARRIER) {
        .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
        .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
        .Transition = {
            .pResource   = resource,
            .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
            .StateBefore = before,
            .StateAfter  = after
        }
    };
}

static int deint_d3d12_process_frame(AVFilterContext *ctx,
                                      AVFilterLink *outlink,
                                      AVFrame *input_frame,
                                      int field,
                                      int queue_idx)
{
    DeinterlaceD3D12Context *s = ctx->priv;
    AVFrame *out = NULL;
    int ret = 0;
    int i;
    HRESULT hr;

    AVD3D12VAFrame *in_d3d12_frame = (AVD3D12VAFrame *)input_frame->data[0];

    out = av_frame_alloc();
    if (!out) {
        av_log(ctx, AV_LOG_ERROR, "Failed to allocate output frame\n");
        return AVERROR(ENOMEM);
    }

    ret = av_hwframe_get_buffer(s->hw_frames_ctx_out, out, 0);
    if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to get output frame from pool\n");
        av_frame_free(&out);
        return ret;
    }

    AVD3D12VAFrame *out_d3d12_frame = (AVD3D12VAFrame *)out->data[0];

    ID3D12Resource *input_resource  = in_d3d12_frame->texture;
    ID3D12Resource *output_resource = out_d3d12_frame->texture;

    /* Build past/future reference frame arrays from queue */
    ID3D12Resource *past_textures[MAX_REFERENCES];
    UINT past_subresources[MAX_REFERENCES];
    int actual_past = 0;

    ID3D12Resource *future_textures[MAX_REFERENCES];
    UINT future_subresources[MAX_REFERENCES];
    int actual_future = 0;

    if (queue_idx >= 0) {
        /* Collect past reference textures from the queue, walking
         * backwards from the current frame position. */
        for (i = 0; i < s->num_past_frames && (queue_idx - 1 - i) >= 0; i++) {
            AVFrame *past_frame = s->frame_queue[queue_idx - 1 - i];
            if (past_frame) {
                AVD3D12VAFrame *past_d3d12 = (AVD3D12VAFrame *)past_frame->data[0];
                past_textures[actual_past] = past_d3d12->texture;
                past_subresources[actual_past] = 0;
                actual_past++;
            }
        }

        /* Collect future reference textures from the queue, walking
         * forwards from the current frame position. */
        for (i = 0; i < s->num_future_frames && (queue_idx + 1 + i) < s->queue_count; i++) {
            AVFrame *future_frame = s->frame_queue[queue_idx + 1 + i];
            if (future_frame) {
                AVD3D12VAFrame *future_d3d12 = (AVD3D12VAFrame *)future_frame->data[0];
                future_textures[actual_future] = future_d3d12->texture;
                future_subresources[actual_future] = 0;
                actual_future++;
            }
        }

        av_log(ctx, AV_LOG_DEBUG,
               "Reference frames: past=%d/%d, future=%d/%d, queue_idx=%d\n",
               actual_past, s->num_past_frames, actual_future, s->num_future_frames,
               queue_idx);
    }

    /* Wait for input frame's fence before accessing it */
    if (in_d3d12_frame->sync_ctx.fence && in_d3d12_frame->sync_ctx.fence_value > 0) {
        UINT64 completed = ID3D12Fence_GetCompletedValue(in_d3d12_frame->sync_ctx.fence);
        if (completed < in_d3d12_frame->sync_ctx.fence_value) {
            hr = ID3D12CommandQueue_Wait(s->command_queue, in_d3d12_frame->sync_ctx.fence,
                                          in_d3d12_frame->sync_ctx.fence_value);
            if (FAILED(hr)) {
                av_log(ctx, AV_LOG_ERROR, "Failed to wait for input fence: HRESULT 0x%lX\n", hr);
                ret = AVERROR_EXTERNAL;
                goto fail;
            }
        }
    }

    /* Wait for past and future reference frame fences before accessing them*/
    for (i = 0; i < actual_past; i++) {
        AVFrame *past_frame = s->frame_queue[queue_idx - 1 - i];
        AVD3D12VAFrame *past_d3d12 = (AVD3D12VAFrame *)past_frame->data[0];
        if (past_d3d12->sync_ctx.fence && past_d3d12->sync_ctx.fence_value > 0) {
            hr = ID3D12CommandQueue_Wait(s->command_queue, past_d3d12->sync_ctx.fence,
                                          past_d3d12->sync_ctx.fence_value);
            if (FAILED(hr)) {
                av_log(ctx, AV_LOG_ERROR, "Failed to wait for past frame fence: HRESULT 0x%lX\n", hr);
                ret = AVERROR_EXTERNAL;
                goto fail;
            }
        }
    }

    for (i = 0; i < actual_future; i++) {
        AVFrame *future_frame = s->frame_queue[queue_idx + 1 + i];
        AVD3D12VAFrame *future_d3d12 = (AVD3D12VAFrame *)future_frame->data[0];
        if (future_d3d12->sync_ctx.fence && future_d3d12->sync_ctx.fence_value > 0) {
            hr = ID3D12CommandQueue_Wait(s->command_queue, future_d3d12->sync_ctx.fence,
                                          future_d3d12->sync_ctx.fence_value);
            if (FAILED(hr)) {
                av_log(ctx, AV_LOG_ERROR, "Failed to wait for future frame fence: HRESULT 0x%lX\n", hr);
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

    /* Resource barriers: input + output + past refs + future refs */
    D3D12_RESOURCE_BARRIER barriers[2 + MAX_REFERENCES * 2];
    int num_barriers = 0;

    add_resource_barrier(barriers, &num_barriers, input_resource,
                         D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_VIDEO_PROCESS_READ);
    add_resource_barrier(barriers, &num_barriers, output_resource,
                         D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_VIDEO_PROCESS_WRITE);

    for (i = 0; i < actual_past; i++)
        add_resource_barrier(barriers, &num_barriers, past_textures[i],
                             D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_VIDEO_PROCESS_READ);

    for (i = 0; i < actual_future; i++)
        add_resource_barrier(barriers, &num_barriers, future_textures[i],
                             D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_VIDEO_PROCESS_READ);

    ID3D12VideoProcessCommandList_ResourceBarrier(s->command_list, num_barriers, barriers);

    /* Setup input stream arguments */
    D3D12_VIDEO_PROCESS_INPUT_STREAM_ARGUMENTS input_args = {0};

    input_args.InputStream[0].pTexture2D = input_resource;

    /* Populate reference set with past/future frames */
    input_args.InputStream[0].ReferenceSet.NumPastFrames       = actual_past;
    input_args.InputStream[0].ReferenceSet.ppPastFrames        = actual_past > 0 ? past_textures : NULL;
    input_args.InputStream[0].ReferenceSet.pPastSubresources   = actual_past > 0 ? past_subresources : NULL;
    input_args.InputStream[0].ReferenceSet.NumFutureFrames     = actual_future;
    input_args.InputStream[0].ReferenceSet.ppFutureFrames      = actual_future > 0 ? future_textures : NULL;
    input_args.InputStream[0].ReferenceSet.pFutureSubresources = actual_future > 0 ? future_subresources : NULL;

    input_args.Transform.SourceRectangle.right       = s->width;
    input_args.Transform.SourceRectangle.bottom      = s->height;
    input_args.Transform.DestinationRectangle.right  = s->width;
    input_args.Transform.DestinationRectangle.bottom = s->height;
    input_args.Transform.Orientation = D3D12_VIDEO_PROCESS_ORIENTATION_DEFAULT;

    input_args.Flags = D3D12_VIDEO_PROCESS_INPUT_STREAM_FLAG_NONE;

    input_args.RateInfo.OutputIndex = field;
    input_args.RateInfo.InputFrameOrField = 0;

    memset(input_args.FilterLevels, 0, sizeof(input_args.FilterLevels));

    input_args.AlphaBlending.Enable = FALSE;
    input_args.AlphaBlending.Alpha = 1.0f;

    /* Setup output stream arguments */
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

    /* Reverse barriers */
    for (i = 0; i < num_barriers; i++) {
        FFSWAP(D3D12_RESOURCE_STATES, barriers[i].Transition.StateBefore, barriers[i].Transition.StateAfter);
    }
    ID3D12VideoProcessCommandList_ResourceBarrier(s->command_list, num_barriers, barriers);

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

    out_d3d12_frame->sync_ctx.fence = s->fence;
    out_d3d12_frame->sync_ctx.fence_value = s->fence_value;
    ID3D12Fence_AddRef(s->fence);

    s->fence_value++;

    ret = av_frame_copy_props(out, input_frame);
    if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to copy frame properties\n");
        goto fail;
    }

    out->width  = s->width;
    out->height = s->height;
    out->format = AV_PIX_FMT_D3D12;
    out->flags &= ~AV_FRAME_FLAG_INTERLACED;

    /* Calculate output PTS for field rate output */
    if (s->field_rate == 2 && queue_idx >= 0) {
        AVFrame *next_frame = (queue_idx + 1 < s->queue_count) ?
                              s->frame_queue[queue_idx + 1] : NULL;

        if (field == 0) {
            out->pts = 2 * input_frame->pts;
        } else if (s->eof || !next_frame) {
            out->pts = 3 * input_frame->pts - s->prev_pts;
        } else {
            out->pts = input_frame->pts + next_frame->pts;
        }
    }

    av_log(ctx, AV_LOG_DEBUG, "Deinterlace output: %dx%d, pts=%"PRId64", field=%d\n",
           out->width, out->height, out->pts, field);

    return ff_filter_frame(outlink, out);

fail:
    av_frame_free(&out);
    return ret;
}

static int deint_d3d12_filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext       *ctx = inlink->dst;
    DeinterlaceD3D12Context *s = ctx->priv;
    AVFilterLink      *outlink = ctx->outputs[0];
    int ret = 0;
    int field;
    AVFrame *input_frame;

    /* Phase 4: EOF flush - process remaining frames in queue.
     * Two sub-cases:
     *   a) Short stream: queue never filled, process all buffered frames
     *   b) Normal stream: process frames after current_frame_index
     *      (future refs and extra delay frames not yet output) */
    if (!in) {
        if (s->eof && s->queue_count > 0) {
            int flush_idx;

            if (!s->initial_fill_done) {
                /* Short stream: queue never reached full depth.
                 * Process ALL buffered frames with whatever references are available. */
                flush_idx = 0;
                av_log(ctx, AV_LOG_DEBUG,
                       "EOF flush (short stream): processing all %d buffered frames\n",
                       s->queue_count);
            } else {
                /* Normal stream: process remaining frames after the last
                 * normally-processed one (future refs, extra delay). */
                flush_idx = s->current_frame_index + 1;
                av_log(ctx, AV_LOG_DEBUG,
                       "EOF flush: processing frames %d..%d in queue (count=%d)\n",
                       flush_idx, s->queue_count - 1, s->queue_count);
            }

            while (flush_idx < s->queue_count) {
                input_frame = s->frame_queue[flush_idx];
                if (input_frame) {
                    for (field = 0; field < s->field_rate; field++) {
                        ret = deint_d3d12_process_frame(ctx, outlink, input_frame, field, flush_idx);
                        if (ret < 0)
                            return ret;
                    }
                    s->prev_pts = input_frame->pts;
                }
                flush_idx++;
            }
            return AVERROR_EOF;
        }
        return AVERROR_EOF;
    }

    if (!in->hw_frames_ctx) {
        av_log(ctx, AV_LOG_ERROR, "No hardware frames context in input frame\n");
        av_frame_free(&in);
        return AVERROR(EINVAL);
    }

    av_log(ctx, AV_LOG_DEBUG, "Input frame: %dx%d, pts=%"PRId64", interlaced=%d\n",
           in->width, in->height, in->pts,
           !!(in->flags & AV_FRAME_FLAG_INTERLACED));

    /* Initialize processor on first frame */
    if (!s->processor_configured) {
        AVHWFramesContext *frames_ctx = (AVHWFramesContext *)in->hw_frames_ctx->data;
        AVD3D12VAFramesContext *input_hwctx = (AVD3D12VAFramesContext *)frames_ctx->hwctx;

        s->width = frames_ctx->width;
        s->height = frames_ctx->height;
        s->input_format = input_hwctx->format;

        if (s->input_format == DXGI_FORMAT_UNKNOWN) {
            switch (frames_ctx->sw_format) {
                case AV_PIX_FMT_NV12:
                    s->input_format = DXGI_FORMAT_NV12;
                    break;
                case AV_PIX_FMT_P010:
                    s->input_format = DXGI_FORMAT_P010;
                    break;
                default:
                    av_log(ctx, AV_LOG_ERROR, "Unsupported input format: %s\n",
                           av_get_pix_fmt_name(frames_ctx->sw_format));
                    av_frame_free(&in);
                    return AVERROR(EINVAL);
            }
        }

        int is_10bit = (s->input_format == DXGI_FORMAT_P010);
        s->input_colorspace = get_dxgi_colorspace(in->colorspace, in->color_trc, is_10bit);
        s->input_framerate = get_input_framerate(ctx, inlink, in);

        ret = deint_d3d12_configure_processor(s, ctx, in);
        if (ret < 0) {
            av_log(ctx, AV_LOG_ERROR, "Failed to configure processor\n");
            av_frame_free(&in);
            return ret;
        }
    }

    /* Auto mode: pass through progressive frames by processing them as-is */
    if (s->auto_enable && !(in->flags & AV_FRAME_FLAG_INTERLACED)) {
        av_log(ctx, AV_LOG_DEBUG, "Progressive frame, processing as pass-through\n");
        ret = deint_d3d12_process_frame(ctx, outlink, in, 0, -1);
        av_frame_free(&in);
        return ret;
    }

    /* Queue management and frame processing.
     *
     * For bob mode, the hardware typically needs no reference frames
     * (past=0, future=0), so queue_depth=1 and every input frame is
     * processed immediately -- simple frame-in, frame-out.
     *
     * For custom (driver-defined) mode, the hardware uses temporal
     * reference frames for higher-quality motion-adaptive deinterlacing.
     * The queue possible holds past, current, and future reference frames:
     * The queue is managed in four phases:
     *   1. Filling: buffer frames until queue reaches queue_depth
     *   2. Initial fill: queue just became full, process ALL buffered
     *      frames (0..current_frame_index) with degraded references
     *      for the earliest frames
     *   3. Steady state: slide queue forward by one position per input,
     *      process the frame at current_frame_index with full references
     *   4. EOF flush: process remaining frames after current_frame_index,
     *      or all buffered frames if the queue never filled (short stream)
     *
     * When queue_depth=1 (bob mode), phases 1, 2, and 4 are effectively
     * skipped, and only the steady-state path executes.
     */

    if (s->queue_count < s->queue_depth) {
        /* Phase 1: Filling - buffer incoming frames until we have enough
         * past and future references to begin processing. */
        s->frame_queue[s->queue_count++] = in;
        if (s->queue_count < s->queue_depth)
            return 0;

        /* Phase 2: Initial fill complete - process all frames from the
         * start of the queue through current_frame_index. The first
         * frames will have fewer past references (graceful degradation),
         * but the D3D12 video processor handles partial reference sets. */
        for (int i = 0; i <= s->current_frame_index; i++) {
            input_frame = s->frame_queue[i];
            if (!input_frame)
                continue;
            for (field = 0; field < s->field_rate; field++) {
                ret = deint_d3d12_process_frame(ctx, outlink, input_frame, field, i);
                if (ret < 0)
                    return ret;
            }
            s->prev_pts = input_frame->pts;
        }
        s->initial_fill_done = 1;
        return ret;
    }

    /* Phase 3: Steady state - slide the queue forward by removing the
     * oldest frame and appending the new one at the end. The frame at
     * current_frame_index always has full past and future references. */
    av_frame_free(&s->frame_queue[0]);
    for (int i = 0; i + 1 < s->queue_count; i++)
        s->frame_queue[i] = s->frame_queue[i + 1];
    s->frame_queue[s->queue_count - 1] = in;

    input_frame = s->frame_queue[s->current_frame_index];
    if (!input_frame)
        return 0;

    for (field = 0; field < s->field_rate; field++) {
        ret = deint_d3d12_process_frame(ctx, outlink, input_frame, field, s->current_frame_index);
        if (ret < 0)
            break;
    }

    s->prev_pts = input_frame->pts;

    return ret;
}

static int deint_d3d12_request_frame(AVFilterLink *link)
{
    AVFilterContext       *ctx = link->src;
    DeinterlaceD3D12Context *s = ctx->priv;
    int ret;

    if (s->eof)
        return AVERROR_EOF;

    ret = ff_request_frame(ctx->inputs[0]);
    if (ret == AVERROR_EOF && s->queue_count > 0) {
        s->eof = 1;
        /* Flush remaining frames in queue (future frames, extra delay,
         * or short stream where queue never fully filled) */
        return deint_d3d12_filter_frame(ctx->inputs[0], NULL);
    }

    return ret;
}

static int deint_d3d12_config_output(AVFilterLink *outlink)
{
    AVFilterContext       *ctx = outlink->src;
    DeinterlaceD3D12Context *s = ctx->priv;
    AVFilterLink       *inlink = ctx->inputs[0];
    FilterLink            *inl = ff_filter_link(inlink);
    FilterLink           *outl = ff_filter_link(outlink);
    int ret;

    release_d3d12_resources(s);
    deint_d3d12_clear_queue(s);

    av_buffer_unref(&s->hw_frames_ctx_out);
    av_buffer_unref(&s->hw_device_ctx);

    s->processor_configured = 0;

    outlink->w = inlink->w;
    outlink->h = inlink->h;
    s->width   = inlink->w;
    s->height  = inlink->h;

    /* Adjust time base and frame rate for field rate output */
    outlink->time_base = av_mul_q(inlink->time_base, (AVRational){ 1, s->field_rate });
    outl->frame_rate   = av_mul_q(inl->frame_rate, (AVRational){ s->field_rate, 1 });

    if (!inl->hw_frames_ctx) {
        av_log(ctx, AV_LOG_ERROR, "No hw_frames_ctx available on input link\n");
        return AVERROR(EINVAL);
    }

    AVHWFramesContext *in_frames_ctx = (AVHWFramesContext *)inl->hw_frames_ctx->data;
    s->hw_device_ctx = av_buffer_ref(in_frames_ctx->device_ref);
    if (!s->hw_device_ctx) {
        av_log(ctx, AV_LOG_ERROR, "Failed to reference device context\n");
        return AVERROR(ENOMEM);
    }

    s->hw_frames_ctx_out = av_hwframe_ctx_alloc(s->hw_device_ctx);
    if (!s->hw_frames_ctx_out)
        return AVERROR(ENOMEM);

    AVHWFramesContext *frames_ctx = (AVHWFramesContext *)s->hw_frames_ctx_out->data;

    frames_ctx->format    = AV_PIX_FMT_D3D12;
    frames_ctx->sw_format = in_frames_ctx->sw_format;
    frames_ctx->width     = s->width;
    frames_ctx->height    = s->height;
    frames_ctx->initial_pool_size = 10;

    if (ctx->extra_hw_frames > 0)
        frames_ctx->initial_pool_size += ctx->extra_hw_frames;

    AVD3D12VAFramesContext    *frames_hwctx = frames_ctx->hwctx;
    AVD3D12VAFramesContext *in_frames_hwctx = in_frames_ctx->hwctx;

    frames_hwctx->format = in_frames_hwctx->format;
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

    av_log(ctx, AV_LOG_VERBOSE, "D3D12 deinterlace config: %dx%d, field_rate=%d\n",
           outlink->w, outlink->h, s->field_rate);

    return 0;
}

static av_cold void deint_d3d12_uninit(AVFilterContext *ctx)
{
    DeinterlaceD3D12Context *s = ctx->priv;

    release_d3d12_resources(s);
    deint_d3d12_clear_queue(s);

    av_buffer_unref(&s->hw_frames_ctx_out);
    av_buffer_unref(&s->hw_device_ctx);
}

static const AVFilterPad deint_d3d12_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = deint_d3d12_filter_frame,
    },
};

static const AVFilterPad deint_d3d12_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = deint_d3d12_request_frame,
        .config_props  = deint_d3d12_config_output,
    },
};

#define OFFSET(x) offsetof(DeinterlaceD3D12Context, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)

static const AVOption deinterlace_d3d12_options[] = {
    { "mode", "Deinterlacing mode",
      OFFSET(mode), AV_OPT_TYPE_INT, { .i64 = DEINT_D3D12_MODE_DEFAULT },
      DEINT_D3D12_MODE_DEFAULT, DEINT_D3D12_MODE_CUSTOM, FLAGS, .unit = "mode" },
    { "default", "Use best available deinterlacing mode",
      0, AV_OPT_TYPE_CONST, { .i64 = DEINT_D3D12_MODE_DEFAULT }, 0, 0, FLAGS, .unit = "mode" },
    { "bob", "Bob deinterlacing (simple field interpolation)",
      0, AV_OPT_TYPE_CONST, { .i64 = DEINT_D3D12_MODE_BOB }, 0, 0, FLAGS, .unit = "mode" },
    { "custom", "Driver-defined advanced deinterlacing",
      0, AV_OPT_TYPE_CONST, { .i64 = DEINT_D3D12_MODE_CUSTOM }, 0, 0, FLAGS, .unit = "mode" },

    { "rate", "Generate output at frame rate or field rate",
      OFFSET(field_rate), AV_OPT_TYPE_INT, { .i64 = 1 }, 1, 2, FLAGS, .unit = "rate" },
    { "frame", "Output at frame rate (one frame for each field-pair)",
      0, AV_OPT_TYPE_CONST, { .i64 = 1 }, 0, 0, FLAGS, .unit = "rate" },
    { "field", "Output at field rate (one frame for each field)",
      0, AV_OPT_TYPE_CONST, { .i64 = 2 }, 0, 0, FLAGS, .unit = "rate" },

    { "auto", "Only deinterlace interlaced frames, pass through progressive",
      OFFSET(auto_enable), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, FLAGS },

    { NULL }
};

AVFILTER_DEFINE_CLASS(deinterlace_d3d12);

const FFFilter ff_vf_deinterlace_d3d12 = {
    .p.name           = "deinterlace_d3d12",
    .p.description    = NULL_IF_CONFIG_SMALL("Deinterlacing using Direct3D12 Video Processor"),
    .priv_size        = sizeof(DeinterlaceD3D12Context),
    .p.priv_class     = &deinterlace_d3d12_class,
    .init             = deint_d3d12_init,
    .uninit           = deint_d3d12_uninit,
    FILTER_INPUTS(deint_d3d12_inputs),
    FILTER_OUTPUTS(deint_d3d12_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_D3D12),
    .p.flags          = AVFILTER_FLAG_HWDEVICE,
    .flags_internal   = FF_FILTER_FLAG_HWFRAME_AWARE,
};
