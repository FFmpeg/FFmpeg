/*
 * D3D12 Hardware-Accelerated Motion Estimation Filter
 *
 * Copyright (c) 2025 Advanced Micro Devices, Inc.
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

#include "libavutil/avassert.h"
#include "libavutil/buffer.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_d3d12va_internal.h"
#include "libavutil/hwcontext_d3d12va.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "libavutil/motion_vector.h"
#include "libavutil/mem.h"
#include "avfilter.h"
#include "filters.h"
#include "video.h"


typedef struct MEstimateD3D12Context {
    const AVClass *class;

    AVBufferRef *hw_device_ref;
    AVBufferRef *hw_frames_ref;

    AVD3D12VADeviceContext *device_ctx;
    AVD3D12VAFramesContext *frames_ctx;

    ID3D12Device *device;
    ID3D12VideoDevice1 *video_device;
    ID3D12VideoMotionEstimator *motion_estimator;
    ID3D12VideoMotionVectorHeap *motion_vector_heap;
    ID3D12VideoEncodeCommandList *command_list;
    ID3D12CommandQueue *command_queue;
    ID3D12CommandAllocator *command_allocator;

    // Graphics command list and queue for copy operations
    ID3D12GraphicsCommandList *copy_command_list;
    ID3D12CommandAllocator *copy_command_allocator;
    ID3D12CommandQueue *copy_command_queue;

    // Synchronization
    ID3D12Fence *fence;
    HANDLE fence_event;
    uint64_t fence_value;

    // Motion estimation parameters
    int block_size;                     // 8 or 16
    D3D12_VIDEO_MOTION_ESTIMATOR_SEARCH_BLOCK_SIZE d3d12_block_size;
    D3D12_VIDEO_MOTION_ESTIMATOR_VECTOR_PRECISION precision;

    // Frame buffer
    AVFrame *prev_frame;
    AVFrame *cur_frame;
    AVFrame *next_frame;

    // Output textures for resolved motion vectors (GPU-side, DEFAULT heap)
    ID3D12Resource *resolved_mv_texture_back;
    ID3D12Resource *resolved_mv_texture_fwd;

    // Readback buffers for CPU access (READBACK heap)
    ID3D12Resource *readback_buffer_back;
    ID3D12Resource *readback_buffer_fwd;
    size_t readback_buffer_size;

    int initialized;
} MEstimateD3D12Context;

static int mestimate_d3d12_init(AVFilterContext *ctx)
{
    MEstimateD3D12Context *s = ctx->priv;

    s->initialized = 0;
    s->fence_value = 0;

    // Validate block size - only 8 and 16 are valid
    if (s->block_size != 8 && s->block_size != 16) {
        av_log(ctx, AV_LOG_ERROR, "Invalid block_size %d. Only 8 and 16 are supported.\n", s->block_size);
        return AVERROR(EINVAL);
    }

    // Set D3D12 block size based on user option
    if (s->block_size == 8)
        s->d3d12_block_size = D3D12_VIDEO_MOTION_ESTIMATOR_SEARCH_BLOCK_SIZE_8X8;
    else
        s->d3d12_block_size = D3D12_VIDEO_MOTION_ESTIMATOR_SEARCH_BLOCK_SIZE_16X16;

    // Use quarter-pel precision
    s->precision = D3D12_VIDEO_MOTION_ESTIMATOR_VECTOR_PRECISION_QUARTER_PEL;

    return 0;
}

static int mestimate_d3d12_create_objects(AVFilterContext *ctx)
{
    MEstimateD3D12Context *s = ctx->priv;
    HRESULT hr;
    D3D12_COMMAND_QUEUE_DESC queue_desc = {
        .Type     = D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE,
        .Priority = 0,
        .Flags    = D3D12_COMMAND_QUEUE_FLAG_NONE,
        .NodeMask = 0,
    };

    // Create fence for synchronization
    hr = ID3D12Device_CreateFence(s->device, 0, D3D12_FENCE_FLAG_NONE,
                                  &IID_ID3D12Fence, (void **)&s->fence);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create fence\n");
        return AVERROR(EINVAL);
    }

    s->fence_event = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!s->fence_event) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create fence event\n");
        return AVERROR(EINVAL);
    }

    // Create command queue
    hr = ID3D12Device_CreateCommandQueue(s->device, &queue_desc,
                                         &IID_ID3D12CommandQueue, (void **)&s->command_queue);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create command queue\n");
        return AVERROR(EINVAL);
    }

    // Create command allocator
    hr = ID3D12Device_CreateCommandAllocator(s->device, D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE,
                                             &IID_ID3D12CommandAllocator, (void **)&s->command_allocator);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create command allocator\n");
        return AVERROR(EINVAL);
    }

    // Create command list
    hr = ID3D12Device_CreateCommandList(s->device, 0, D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE,
                                        s->command_allocator, NULL, &IID_ID3D12VideoEncodeCommandList,
                                        (void **)&s->command_list);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create command list\n");
        return AVERROR(EINVAL);
    }

    hr = ID3D12VideoEncodeCommandList_Close(s->command_list);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to close command list\n");
        return AVERROR(EINVAL);
    }

    return 0;
}

static int mestimate_d3d12_create_motion_estimator(AVFilterContext *ctx, int width, int height)
{
    MEstimateD3D12Context *s = ctx->priv;
    HRESULT hr;
    D3D12_FEATURE_DATA_VIDEO_MOTION_ESTIMATOR feature_data = {0};
    D3D12_VIDEO_MOTION_ESTIMATOR_DESC me_desc              = {0};
    D3D12_VIDEO_MOTION_VECTOR_HEAP_DESC heap_desc          = {0};

    // Check if motion estimation is supported
    // Set the input parameters for what we want to query
    feature_data.NodeIndex      = 0;
    feature_data.InputFormat    = s->frames_ctx->format;
    feature_data.BlockSizeFlags = 0;  // Will be filled by CheckFeatureSupport with supported flags
    feature_data.PrecisionFlags = 0;  // Will be filled by CheckFeatureSupport with supported flags
    feature_data.SizeRange.MaxWidth  = width;
    feature_data.SizeRange.MaxHeight = height;
    feature_data.SizeRange.MinWidth  = width;
    feature_data.SizeRange.MinHeight = height;

    hr = ID3D12VideoDevice1_CheckFeatureSupport(s->video_device,
                                                D3D12_FEATURE_VIDEO_MOTION_ESTIMATOR,
                                                &feature_data, sizeof(feature_data));
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to check motion estimator support (hr=0x%lx)\n", (long)hr);
        return AVERROR(EINVAL);
    }

    // Verify the requested features are actually supported (check returned flags)
    D3D12_VIDEO_MOTION_ESTIMATOR_SEARCH_BLOCK_SIZE_FLAGS requested_block_flag =
        (s->d3d12_block_size == D3D12_VIDEO_MOTION_ESTIMATOR_SEARCH_BLOCK_SIZE_8X8) ?
        D3D12_VIDEO_MOTION_ESTIMATOR_SEARCH_BLOCK_SIZE_FLAG_8X8 :
        D3D12_VIDEO_MOTION_ESTIMATOR_SEARCH_BLOCK_SIZE_FLAG_16X16;

    if (!(feature_data.BlockSizeFlags & requested_block_flag)) {
        av_log(ctx, AV_LOG_ERROR, "Requested block size (%dx%d) not supported by device (supported flags: 0x%x)\n",
               s->block_size, s->block_size, feature_data.BlockSizeFlags);
        return AVERROR(ENOSYS);
    }

    if (!(feature_data.PrecisionFlags & D3D12_VIDEO_MOTION_ESTIMATOR_VECTOR_PRECISION_FLAG_QUARTER_PEL)) {
        av_log(ctx, AV_LOG_ERROR, "Quarter-pel precision not supported by device (supported flags: 0x%x)\n",
               feature_data.PrecisionFlags);
        return AVERROR(ENOSYS);
    }

    av_log(ctx, AV_LOG_VERBOSE, "Motion estimator support confirmed: block_size=%dx%d, precision=quarter-pel\n",
           s->block_size, s->block_size);

    // Create motion estimator
    me_desc.NodeMask    = 0;
    me_desc.InputFormat = s->frames_ctx->format;
    me_desc.BlockSize   = s->d3d12_block_size;
    me_desc.Precision   = s->precision;
    me_desc.SizeRange   = feature_data.SizeRange;

    hr = ID3D12VideoDevice1_CreateVideoMotionEstimator(s->video_device, &me_desc, NULL,
                                                       &IID_ID3D12VideoMotionEstimator,
                                                       (void **)&s->motion_estimator);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create motion estimator\n");
        return AVERROR(EINVAL);
    }

    // Create motion vector heap
    heap_desc.NodeMask    = 0;
    heap_desc.InputFormat = s->frames_ctx->format;
    heap_desc.BlockSize   = s->d3d12_block_size;
    heap_desc.Precision   = s->precision;
    heap_desc.SizeRange   = feature_data.SizeRange;

    hr = ID3D12VideoDevice1_CreateVideoMotionVectorHeap(s->video_device, &heap_desc, NULL,
                                                        &IID_ID3D12VideoMotionVectorHeap,
                                                        (void **)&s->motion_vector_heap);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create motion vector heap\n");
        return AVERROR(EINVAL);
    }

    // Create resolved motion vector textures in DEFAULT heap (GPU writable)
    // ResolveMotionVectorHeap outputs to TEXTURE2D with DXGI_FORMAT_R16G16_SINT
    int mb_width  = (width + s->block_size - 1) / s->block_size;
    int mb_height = (height + s->block_size - 1) / s->block_size;

    D3D12_HEAP_PROPERTIES heap_props_default = {.Type = D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC texture_desc = {
        .Dimension  = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
        .Alignment        = 0,
        .Width            = mb_width,
        .Height           = mb_height,
        .DepthOrArraySize = 1,
        .MipLevels        = 1,
        .Format           = DXGI_FORMAT_R16G16_SINT,  // Motion vector format: signed 16-bit X,Y
        .SampleDesc       = {.Count = 1, .Quality = 0},
        .Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN,
        .Flags            = D3D12_RESOURCE_FLAG_NONE,
    };

    hr = ID3D12Device_CreateCommittedResource(s->device, &heap_props_default, D3D12_HEAP_FLAG_NONE,
                                              &texture_desc, D3D12_RESOURCE_STATE_COMMON, NULL,
                                              &IID_ID3D12Resource, (void **)&s->resolved_mv_texture_back);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create backward motion vector texture (hr=0x%lx)\n", (long)hr);
        return AVERROR(EINVAL);
    }

    hr = ID3D12Device_CreateCommittedResource(s->device, &heap_props_default, D3D12_HEAP_FLAG_NONE,
                                              &texture_desc, D3D12_RESOURCE_STATE_COMMON, NULL,
                                              &IID_ID3D12Resource, (void **)&s->resolved_mv_texture_fwd);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create forward motion vector texture (hr=0x%lx)\n", (long)hr);
        return AVERROR(EINVAL);
    }

    // Create READBACK buffers for CPU access
    // Need to calculate proper size accounting for D3D12 row pitch alignment
    // Get the footprint to determine the actual required buffer size
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT temp_layout;
    UINT64 temp_total_size;

    ID3D12Device_GetCopyableFootprints(s->device, &texture_desc, 0, 1, 0,
                                       &temp_layout, NULL, NULL, &temp_total_size);

    s->readback_buffer_size = temp_total_size;

    av_log(ctx, AV_LOG_DEBUG, "Readback buffer size: %llu bytes (texture: %dx%d, pitch: %u)\n",
           (unsigned long long)s->readback_buffer_size, mb_width, mb_height, temp_layout.Footprint.RowPitch);

    D3D12_HEAP_PROPERTIES heap_props_readback = {.Type = D3D12_HEAP_TYPE_READBACK};
    D3D12_RESOURCE_DESC buffer_desc = {
        .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
        .Alignment        = 0,
        .Width            = s->readback_buffer_size,
        .Height           = 1,
        .DepthOrArraySize = 1,
        .MipLevels        = 1,
        .Format           = DXGI_FORMAT_UNKNOWN,
        .SampleDesc       = {.Count = 1, .Quality = 0},
        .Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
        .Flags            = D3D12_RESOURCE_FLAG_NONE,
    };

    hr = ID3D12Device_CreateCommittedResource(s->device, &heap_props_readback, D3D12_HEAP_FLAG_NONE,
                                              &buffer_desc, D3D12_RESOURCE_STATE_COPY_DEST, NULL,
                                              &IID_ID3D12Resource, (void **)&s->readback_buffer_back);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create backward readback buffer (hr=0x%lx)\n", (long)hr);
        return AVERROR(EINVAL);
    }

    hr = ID3D12Device_CreateCommittedResource(s->device, &heap_props_readback, D3D12_HEAP_FLAG_NONE,
                                              &buffer_desc, D3D12_RESOURCE_STATE_COPY_DEST, NULL,
                                              &IID_ID3D12Resource, (void **)&s->readback_buffer_fwd);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create forward readback buffer (hr=0x%lx)\n", (long)hr);
        return AVERROR(EINVAL);
    }

    // Create graphics command queue, allocator and list for copy operations
    D3D12_COMMAND_QUEUE_DESC copy_queue_desc = {
        .Type     = D3D12_COMMAND_LIST_TYPE_DIRECT,
        .Priority = 0,
        .Flags    = D3D12_COMMAND_QUEUE_FLAG_NONE,
        .NodeMask = 0,
    };

    hr = ID3D12Device_CreateCommandQueue(s->device, &copy_queue_desc,
                                         &IID_ID3D12CommandQueue, (void **)&s->copy_command_queue);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create copy command queue\n");
        return AVERROR(EINVAL);
    }

    hr = ID3D12Device_CreateCommandAllocator(s->device, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                             &IID_ID3D12CommandAllocator, (void **)&s->copy_command_allocator);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create copy command allocator\n");
        return AVERROR(EINVAL);
    }

    hr = ID3D12Device_CreateCommandList(s->device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                        s->copy_command_allocator, NULL, &IID_ID3D12GraphicsCommandList,
                                        (void **)&s->copy_command_list);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create copy command list\n");
        return AVERROR(EINVAL);
    }

    hr = ID3D12GraphicsCommandList_Close(s->copy_command_list);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to close copy command list\n");
        return AVERROR(EINVAL);
    }

    return 0;
}

static int mestimate_d3d12_config_props(AVFilterLink *outlink)
{
    AVFilterContext     *ctx = outlink->src;
    AVFilterLink     *inlink = ctx->inputs[0];
    FilterLink          *inl = ff_filter_link(inlink);
    FilterLink         *outl = ff_filter_link(outlink);
    MEstimateD3D12Context *s = ctx->priv;
    AVHWFramesContext *hw_frames_ctx;
    HRESULT hr;
    int err;

    if (!inl->hw_frames_ctx) {
        av_log(ctx, AV_LOG_ERROR, "D3D12 hardware frames context required\n");
        return AVERROR(EINVAL);
    }

    hw_frames_ctx = (AVHWFramesContext *)inl->hw_frames_ctx->data;
    if (hw_frames_ctx->format != AV_PIX_FMT_D3D12) {
        av_log(ctx, AV_LOG_ERROR, "Input must be D3D12 frames\n");
        return AVERROR(EINVAL);
    }

    s->hw_frames_ref = av_buffer_ref(inl->hw_frames_ctx);
    if (!s->hw_frames_ref)
        return AVERROR(ENOMEM);

    s->frames_ctx = hw_frames_ctx->hwctx;
    s->hw_device_ref = av_buffer_ref(hw_frames_ctx->device_ref);
    if (!s->hw_device_ref)
        return AVERROR(ENOMEM);

    s->device_ctx = ((AVHWDeviceContext *)s->hw_device_ref->data)->hwctx;
    s->device = s->device_ctx->device;

    // Propagate hardware frames context to output
    outl->hw_frames_ctx = av_buffer_ref(inl->hw_frames_ctx);
    if (!outl->hw_frames_ctx)
        return AVERROR(ENOMEM);

    // Query for ID3D12VideoDevice1 interface from the base video device
    hr = ID3D12VideoDevice_QueryInterface(s->device_ctx->video_device, &IID_ID3D12VideoDevice1,
                                          (void **)&s->video_device);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "ID3D12VideoDevice1 interface not supported\n");
        return AVERROR(ENOSYS);
    }

    err = mestimate_d3d12_create_objects(ctx);
    if (err < 0)
        return err;

    err = mestimate_d3d12_create_motion_estimator(ctx, inlink->w, inlink->h);
    if (err < 0)
        return err;

    s->initialized = 1;

    return 0;
}

static int mestimate_d3d12_sync_gpu(MEstimateD3D12Context *s)
{
    uint64_t completion = ID3D12Fence_GetCompletedValue(s->fence);

    if (completion < s->fence_value) {
        if (FAILED(ID3D12Fence_SetEventOnCompletion(s->fence, s->fence_value, s->fence_event)))
            return AVERROR(EINVAL);
        WaitForSingleObjectEx(s->fence_event, INFINITE, FALSE);
    }

    return 0;
}

static inline void d3d12_barrier_transition(D3D12_RESOURCE_BARRIER *barrier,
                                            ID3D12Resource *resource,
                                            D3D12_RESOURCE_STATES state_before,
                                            D3D12_RESOURCE_STATES state_after)
{
    barrier->Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier->Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier->Transition.pResource   = resource;
    barrier->Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier->Transition.StateBefore = state_before;
    barrier->Transition.StateAfter  = state_after;
}

static void add_mv_data(AVMotionVector *mv, int mb_size,
                        int x, int y, int x_mv, int y_mv, int dir)
{
    mv->w      = mb_size;
    mv->h      = mb_size;
    mv->dst_x  = x + (mb_size >> 1);
    mv->dst_y  = y + (mb_size >> 1);
    mv->src_x  = x_mv + (mb_size >> 1);
    mv->src_y  = y_mv + (mb_size >> 1);
    mv->source = dir ? 1 : -1;
    mv->flags  = 0;
    mv->motion_x = x_mv - x;
    mv->motion_y = y_mv - y;
    mv->motion_scale = 1;
}

static int mestimate_d3d12_read_motion_vectors(AVFilterContext *ctx, AVFrame *out, int direction)
{
    MEstimateD3D12Context *s = ctx->priv;
    uint8_t *mapped_data = NULL;
    HRESULT hr;
    int err = 0;
    AVFrameSideData *sd;
    AVMotionVector *mvs;
    int mb_x, mb_y, mv_idx;
    int mb_width, mb_height;
    int16_t *d3d12_mvs;
    ID3D12Resource *buffer = (direction == 0) ? s->readback_buffer_back : s->readback_buffer_fwd;

    // Map the readback buffer
    hr = ID3D12Resource_Map(buffer, 0, NULL, (void **)&mapped_data);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to map readback buffer (dir=%d, hr=0x%lx)\n", direction, (long)hr);
        return AVERROR(EINVAL);
    }

    // Get the motion vector side data
    sd = av_frame_get_side_data(out, AV_FRAME_DATA_MOTION_VECTORS);
    if (!sd) {
        av_log(ctx, AV_LOG_ERROR, "No motion vector side data found\n");
        ID3D12Resource_Unmap(buffer, 0, NULL);
        return AVERROR(EINVAL);
    }

    mvs       = (AVMotionVector *)sd->data;
    mb_width  = (out->width + s->block_size - 1) / s->block_size;
    mb_height = (out->height + s->block_size - 1) / s->block_size;

    // Calculate offset for this direction (0 = backward, 1 = forward)
    mv_idx = direction * mb_width * mb_height;

    // Parse D3D12 motion vector format
    // According to Microsoft documentation:
    // - Format: DXGI_FORMAT_R16G16_SINT (2D texture)
    // - Data: Signed 16-bit integers
    // - Units: Quarter-PEL (quarter pixel precision)
    // - Layout: X component in R channel, Y component in G channel
    // - Storage: 2D array matching block layout
    //
    // Each motion vector is stored as two int16_t values (X, Y) in quarter-pel units
    // The buffer is organized as a 2D array: [mb_height][mb_width][2]

    d3d12_mvs = (int16_t *)mapped_data;

    for (mb_y = 0; mb_y < mb_height; mb_y++) {
        for (mb_x = 0; mb_x < mb_width; mb_x++) {
            const int x_mb = mb_x * s->block_size;
            const int y_mb = mb_y * s->block_size;
            const int mv_offset = (mb_y * mb_width + mb_x) * 2;

            // Read motion vector components in quarter-pel units
            // R component (index 0) = X motion
            // G component (index 1) = Y motion
            int16_t mv_x_qpel = d3d12_mvs[mv_offset + 0];
            int16_t mv_y_qpel = d3d12_mvs[mv_offset + 1];

            // Convert from quarter-pel to full pixel coordinates
            // Quarter-pel means the value is 4x the actual pixel displacement
            // So divide by 4 to get pixel displacement
            int src_x = x_mb + (mv_x_qpel / 4);
            int src_y = y_mb + (mv_y_qpel / 4);

            // Store the motion vector data
            // This will set dst (current position) and src (where it came from)
            add_mv_data(&mvs[mv_idx++], s->block_size, x_mb, y_mb, src_x, src_y, direction);

            av_log(ctx, AV_LOG_TRACE, "Block[%d,%d] dir=%d: MV=(%d,%d) qpel -> (%d,%d) pixels\n",
                   mb_x, mb_y, direction, mv_x_qpel, mv_y_qpel,
                   mv_x_qpel / 4, mv_y_qpel / 4);
        }
    }

    ID3D12Resource_Unmap(buffer, 0, NULL);

    av_log(ctx, AV_LOG_DEBUG, "Parsed %d motion vectors for direction %d\n",
           mb_width * mb_height, direction);

    return err;
}

static int mestimate_d3d12_filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext     *ctx = inlink->dst;
    MEstimateD3D12Context *s = ctx->priv;
    AVFrame *out;
    AVFrameSideData *sd;
    AVD3D12VAFrame *cur_hwframe, *prev_hwframe, *next_hwframe = NULL;
    HRESULT hr;
    int err;
    int mb_width, mb_height, mb_count;

    if (!s->initialized) {
        err = mestimate_d3d12_config_props(ctx->outputs[0]);
        if (err < 0) {
            av_frame_free(&frame);
            return err;
        }
    }

    // Manage frame buffer
    av_frame_free(&s->prev_frame);
    s->prev_frame = s->cur_frame;
    s->cur_frame  = s->next_frame;
    s->next_frame = frame;

    if (!s->cur_frame) {
        s->cur_frame = av_frame_clone(frame);
        if (!s->cur_frame)
            return AVERROR(ENOMEM);
    }

    if (!s->prev_frame)
        return 0;

    // Clone current frame for output
    out = av_frame_clone(s->cur_frame);
    if (!out)
        return AVERROR(ENOMEM);

    mb_width  = (frame->width + s->block_size - 1) / s->block_size;
    mb_height = (frame->height + s->block_size - 1) / s->block_size;
    mb_count  = mb_width * mb_height;

    // Allocate side data for motion vectors (2 directions)
    sd = av_frame_new_side_data(out, AV_FRAME_DATA_MOTION_VECTORS,
                                2 * mb_count * sizeof(AVMotionVector));
    if (!sd) {
        av_frame_free(&out);
        return AVERROR(ENOMEM);
    }

    // Get hardware frame pointers
    cur_hwframe = (AVD3D12VAFrame *)s->cur_frame->data[0];
    prev_hwframe = (AVD3D12VAFrame *)s->prev_frame->data[0];
    if (s->next_frame)
        next_hwframe = (AVD3D12VAFrame *)s->next_frame->data[0];

    // Reset command allocator and list ONCE for both estimations
    hr = ID3D12CommandAllocator_Reset(s->command_allocator);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to reset command allocator\n");
        av_frame_free(&out);
        return AVERROR(EINVAL);
    }

    hr = ID3D12VideoEncodeCommandList_Reset(s->command_list, s->command_allocator);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to reset command list\n");
        av_frame_free(&out);
        return AVERROR(EINVAL);
    }

    // Transition current and previous frames to VIDEO_ENCODE_READ
    D3D12_RESOURCE_BARRIER barriers[3];
    int barrier_count = 2;

    d3d12_barrier_transition(&barriers[0], cur_hwframe->texture,
                            D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ);
    d3d12_barrier_transition(&barriers[1], prev_hwframe->texture,
                            D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ);

    if (next_hwframe) {
        d3d12_barrier_transition(&barriers[2], next_hwframe->texture,
                                D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ);
        barrier_count = 3;
    }

    ID3D12VideoEncodeCommandList_ResourceBarrier(s->command_list, barrier_count, barriers);

    // Backward motion estimation (cur -> prev)
    D3D12_VIDEO_MOTION_ESTIMATOR_INPUT input_back = {
        .pInputTexture2D           = cur_hwframe->texture,
        .InputSubresourceIndex     = 0,
        .pReferenceTexture2D       = prev_hwframe->texture,
        .ReferenceSubresourceIndex = 0,
        .pHintMotionVectorHeap     = NULL,
    };

    D3D12_VIDEO_MOTION_ESTIMATOR_OUTPUT output = {
        .pMotionVectorHeap = s->motion_vector_heap,
    };

    ID3D12VideoEncodeCommandList_EstimateMotion(s->command_list, s->motion_estimator,
                                                &output, &input_back);

    D3D12_RESOLVE_VIDEO_MOTION_VECTOR_HEAP_INPUT resolve_input = {
        .pMotionVectorHeap = s->motion_vector_heap,
        .PixelWidth        = s->cur_frame->width,
        .PixelHeight       = s->cur_frame->height,
    };

    D3D12_RESOLVE_VIDEO_MOTION_VECTOR_HEAP_OUTPUT resolve_output_back = {
        .pMotionVectorTexture2D = s->resolved_mv_texture_back,
        .MotionVectorCoordinate = {.X = 0, .Y = 0, .Z = 0, .SubresourceIndex = 0},
    };

    ID3D12VideoEncodeCommandList_ResolveMotionVectorHeap(s->command_list,
                                                         &resolve_output_back, &resolve_input);

    // Copy resolved texture to readback buffer for CPU access
    // CopyTextureRegion is not available on video encode command list
    // We'll need to read directly from the resolved texture after GPU sync

    // Forward motion estimation (cur -> next) if next frame exists
    if (next_hwframe) {
        D3D12_VIDEO_MOTION_ESTIMATOR_INPUT input_fwd = {
            .pInputTexture2D           = cur_hwframe->texture,
            .InputSubresourceIndex     = 0,
            .pReferenceTexture2D       = next_hwframe->texture,
            .ReferenceSubresourceIndex = 0,
            .pHintMotionVectorHeap     = NULL,
        };

        ID3D12VideoEncodeCommandList_EstimateMotion(s->command_list, s->motion_estimator,
                                                    &output, &input_fwd);

        D3D12_RESOLVE_VIDEO_MOTION_VECTOR_HEAP_OUTPUT resolve_output_fwd = {
            .pMotionVectorTexture2D = s->resolved_mv_texture_fwd,
            .MotionVectorCoordinate = {.X = 0, .Y = 0, .Z = 0, .SubresourceIndex = 0},
        };

        ID3D12VideoEncodeCommandList_ResolveMotionVectorHeap(s->command_list,
                                                             &resolve_output_fwd, &resolve_input);

        // Copy will be done after command list execution
    }

    // Transition resources back to COMMON (reuse barriers by swapping states)
    for (int i = 0; i < barrier_count; i++)
        FFSWAP(D3D12_RESOURCE_STATES, barriers[i].Transition.StateBefore, barriers[i].Transition.StateAfter);

    ID3D12VideoEncodeCommandList_ResourceBarrier(s->command_list, barrier_count, barriers);

    // Close command list ONCE
    hr = ID3D12VideoEncodeCommandList_Close(s->command_list);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to close command list (hr=0x%lx)\n", (long)hr);
        av_frame_free(&out);
        return AVERROR(EINVAL);
    }

    // Wait for input frame sync
    hr = ID3D12CommandQueue_Wait(s->command_queue, cur_hwframe->sync_ctx.fence,
                                 cur_hwframe->sync_ctx.fence_value);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to wait for current frame\n");
        av_frame_free(&out);
        return AVERROR(EINVAL);
    }

    hr = ID3D12CommandQueue_Wait(s->command_queue, prev_hwframe->sync_ctx.fence,
                                 prev_hwframe->sync_ctx.fence_value);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to wait for previous frame\n");
        av_frame_free(&out);
        return AVERROR(EINVAL);
    }

    if (next_hwframe) {
        hr = ID3D12CommandQueue_Wait(s->command_queue, next_hwframe->sync_ctx.fence,
                                     next_hwframe->sync_ctx.fence_value);
        if (FAILED(hr)) {
            av_log(ctx, AV_LOG_ERROR, "Failed to wait for next frame\n");
            av_frame_free(&out);
            return AVERROR(EINVAL);
        }
    }

    // Execute command list ONCE
    ID3D12CommandQueue_ExecuteCommandLists(s->command_queue, 1, (ID3D12CommandList **)&s->command_list);

    // Signal completion
    hr = ID3D12CommandQueue_Signal(s->command_queue, s->fence, ++s->fence_value);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to signal fence\n");
        av_frame_free(&out);
        return AVERROR(EINVAL);
    }

    // Wait for GPU to complete
    err = mestimate_d3d12_sync_gpu(s);
    if (err < 0) {
        av_frame_free(&out);
        return err;
    }

    // Now copy the resolved textures to readback buffers using graphics command list
    hr = ID3D12CommandAllocator_Reset(s->copy_command_allocator);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to reset copy command allocator\n");
        av_frame_free(&out);
        return AVERROR(EINVAL);
    }

    hr = ID3D12GraphicsCommandList_Reset(s->copy_command_list, s->copy_command_allocator, NULL);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to reset copy command list\n");
        av_frame_free(&out);
        return AVERROR(EINVAL);
    }

    // Transition resolved textures to COPY_SOURCE state
    D3D12_RESOURCE_BARRIER copy_barriers[2];
    int copy_barrier_count = 1;

    d3d12_barrier_transition(&copy_barriers[0], s->resolved_mv_texture_back,
                            D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);

    if (s->next_frame) {
        d3d12_barrier_transition(&copy_barriers[1], s->resolved_mv_texture_fwd,
                                D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
        copy_barrier_count = 2;
    }

    ID3D12GraphicsCommandList_ResourceBarrier(s->copy_command_list, copy_barrier_count, copy_barriers);

    // Get texture layout for backward copy
    D3D12_RESOURCE_DESC texture_desc_back;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout_back;
    UINT64 row_size_back, total_size_back;
    UINT num_rows_back;

    // Get the resource description for backward texture
    s->resolved_mv_texture_back->lpVtbl->GetDesc(s->resolved_mv_texture_back, &texture_desc_back);

    av_log(ctx, AV_LOG_DEBUG, "Back texture desc: Width=%llu, Height=%u, Format=%d\n",
           (unsigned long long)texture_desc_back.Width, texture_desc_back.Height, texture_desc_back.Format);

    // Get the copyable footprints for the backward texture
    ID3D12Device_GetCopyableFootprints(s->device, &texture_desc_back, 0, 1, 0,
                                       &layout_back, &num_rows_back, &row_size_back, &total_size_back);

    av_log(ctx, AV_LOG_DEBUG, "Back layout: Offset=%llu, Width=%u, Height=%u, Depth=%u, RowPitch=%u\n",
           (unsigned long long)layout_back.Offset, layout_back.Footprint.Width, layout_back.Footprint.Height,
           layout_back.Footprint.Depth, layout_back.Footprint.RowPitch);

    // Copy backward motion vectors
    D3D12_TEXTURE_COPY_LOCATION src_back = {
        .pResource = s->resolved_mv_texture_back,
        .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
        .SubresourceIndex = 0
    };

    D3D12_TEXTURE_COPY_LOCATION dst_back = {
        .pResource = s->readback_buffer_back,
        .Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
        .PlacedFootprint = {
            .Offset = 0,
            .Footprint = layout_back.Footprint
        }
    };

    av_log(ctx, AV_LOG_DEBUG, "Copying backward MVs...\n");
    ID3D12GraphicsCommandList_CopyTextureRegion(s->copy_command_list, &dst_back, 0, 0, 0, &src_back, NULL);

    // Copy forward motion vectors if available
    if (s->next_frame) {
        // Get texture layout for forward copy
        D3D12_RESOURCE_DESC texture_desc_fwd;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout_fwd;
        UINT64 row_size_fwd, total_size_fwd;
        UINT num_rows_fwd;

        // Get the resource description for forward texture
        s->resolved_mv_texture_fwd->lpVtbl->GetDesc(s->resolved_mv_texture_fwd, &texture_desc_fwd);

        av_log(ctx, AV_LOG_DEBUG, "Fwd texture desc: Width=%llu, Height=%u, Format=%d\n",
               (unsigned long long)texture_desc_fwd.Width, texture_desc_fwd.Height, texture_desc_fwd.Format);

        // Get the copyable footprints for the forward texture
        ID3D12Device_GetCopyableFootprints(s->device, &texture_desc_fwd, 0, 1, 0,
                                           &layout_fwd, &num_rows_fwd, &row_size_fwd, &total_size_fwd);

        av_log(ctx, AV_LOG_DEBUG, "Fwd layout: Offset=%llu, Width=%u, Height=%u, Depth=%u, RowPitch=%u\n",
               (unsigned long long)layout_fwd.Offset, layout_fwd.Footprint.Width, layout_fwd.Footprint.Height,
               layout_fwd.Footprint.Depth, layout_fwd.Footprint.RowPitch);

        D3D12_TEXTURE_COPY_LOCATION src_fwd = {
            .pResource = s->resolved_mv_texture_fwd,
            .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
            .SubresourceIndex = 0
        };

        D3D12_TEXTURE_COPY_LOCATION dst_fwd = {
            .pResource = s->readback_buffer_fwd,
            .Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
            .PlacedFootprint = {
                .Offset = 0,
                .Footprint = layout_fwd.Footprint
            }
        };

        av_log(ctx, AV_LOG_DEBUG, "Copying forward MVs...\n");
        ID3D12GraphicsCommandList_CopyTextureRegion(s->copy_command_list, &dst_fwd, 0, 0, 0, &src_fwd, NULL);
    }

    // Transition back to COMMON state (reuse barriers by swapping states)
    for (int i = 0; i < copy_barrier_count; i++)
        FFSWAP(D3D12_RESOURCE_STATES, copy_barriers[i].Transition.StateBefore, copy_barriers[i].Transition.StateAfter);

    ID3D12GraphicsCommandList_ResourceBarrier(s->copy_command_list, copy_barrier_count, copy_barriers);

    hr = ID3D12GraphicsCommandList_Close(s->copy_command_list);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to close copy command list (hr=0x%lx)\n", (long)hr);
        av_frame_free(&out);
        return AVERROR(EINVAL);
    }

    // Execute copy command list on the copy queue
    ID3D12CommandQueue_ExecuteCommandLists(s->copy_command_queue, 1, (ID3D12CommandList **)&s->copy_command_list);

    // Signal and wait for copy completion
    hr = ID3D12CommandQueue_Signal(s->copy_command_queue, s->fence, ++s->fence_value);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to signal fence for copy\n");
        av_frame_free(&out);
        return AVERROR(EINVAL);
    }

    err = mestimate_d3d12_sync_gpu(s);
    if (err < 0) {
        av_frame_free(&out);
        return err;
    }

    // Read motion vectors for both directions
    err = mestimate_d3d12_read_motion_vectors(ctx, out, 0);
    if (err < 0) {
        av_frame_free(&out);
        return err;
    }

    if (s->next_frame) {
        err = mestimate_d3d12_read_motion_vectors(ctx, out, 1);
        if (err < 0) {
            av_frame_free(&out);
            return err;
        }
    }

    return ff_filter_frame(ctx->outputs[0], out);
}

static av_cold void mestimate_d3d12_uninit(AVFilterContext *ctx)
{
    MEstimateD3D12Context *s = ctx->priv;

    av_frame_free(&s->prev_frame);
    av_frame_free(&s->cur_frame);
    av_frame_free(&s->next_frame);

    D3D12_OBJECT_RELEASE(s->copy_command_list);
    D3D12_OBJECT_RELEASE(s->copy_command_allocator);
    D3D12_OBJECT_RELEASE(s->copy_command_queue);
    D3D12_OBJECT_RELEASE(s->readback_buffer_back);
    D3D12_OBJECT_RELEASE(s->readback_buffer_fwd);
    D3D12_OBJECT_RELEASE(s->resolved_mv_texture_back);
    D3D12_OBJECT_RELEASE(s->resolved_mv_texture_fwd);
    D3D12_OBJECT_RELEASE(s->motion_vector_heap);
    D3D12_OBJECT_RELEASE(s->motion_estimator);
    D3D12_OBJECT_RELEASE(s->command_list);
    D3D12_OBJECT_RELEASE(s->command_allocator);
    D3D12_OBJECT_RELEASE(s->command_queue);
    D3D12_OBJECT_RELEASE(s->fence);

    if (s->fence_event)
        CloseHandle(s->fence_event);

    av_buffer_unref(&s->hw_frames_ref);
    av_buffer_unref(&s->hw_device_ref);
}

static const AVFilterPad mestimate_d3d12_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = mestimate_d3d12_filter_frame,
    },
};

static const AVFilterPad mestimate_d3d12_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = mestimate_d3d12_config_props,
    },
};

#define OFFSET(x) offsetof(MEstimateD3D12Context, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption mestimate_d3d12_options[] = {
    { "mb_size", "macroblock size", OFFSET(block_size), AV_OPT_TYPE_INT, {.i64 = 16}, 8, 16, FLAGS, .unit = "mb_size" },
    { "8",  "8x8 blocks",   0, AV_OPT_TYPE_CONST, {.i64 = 8},  0, 0, FLAGS, .unit = "mb_size" },
    { "16", "16x16 blocks", 0, AV_OPT_TYPE_CONST, {.i64 = 16}, 0, 0, FLAGS, .unit = "mb_size" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(mestimate_d3d12);

const FFFilter ff_vf_mestimate_d3d12 = {
    .p.name         = "mestimate_d3d12",
    .p.description  = NULL_IF_CONFIG_SMALL("Generate motion vectors using D3D12 hardware acceleration."),
    .p.priv_class   = &mestimate_d3d12_class,
    .p.flags        = AVFILTER_FLAG_METADATA_ONLY | AVFILTER_FLAG_HWDEVICE,
    .priv_size      = sizeof(MEstimateD3D12Context),
    .init           = mestimate_d3d12_init,
    .uninit         = mestimate_d3d12_uninit,
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
    FILTER_INPUTS(mestimate_d3d12_inputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_D3D12),
    FILTER_OUTPUTS(mestimate_d3d12_outputs),
};
