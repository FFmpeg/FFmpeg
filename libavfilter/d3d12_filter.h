/*
 * D3D12VA filter helpers
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

#ifndef AVFILTER_D3D12_FILTER_H
#define AVFILTER_D3D12_FILTER_H

#include "config.h"

#include "libavutil/buffer.h"
#include "libavutil/fifo.h"
#include "libavutil/frame.h"
#include "libavutil/hwcontext_d3d12va.h"
#include "libavutil/pixfmt.h"

#include "avfilter.h"

/**
 * Maximum number of input frames (including reference frames) that one
 * submission can read.
 */
#define D3D12_FILTER_MAX_INPUTS 8

typedef struct D3D12FilterFormat {
    enum AVPixelFormat pix_fmt;
    DXGI_FORMAT        dxgi_format;
    int                is_rgb;
} D3D12FilterFormat;

const D3D12FilterFormat *ff_d3d12_filter_format_from_pix_fmt(enum AVPixelFormat pix_fmt);

const D3D12FilterFormat *ff_d3d12_filter_format_from_dxgi(DXGI_FORMAT dxgi_format);

/**
 * Iterate over all supported formats.
 *
 * @param opaque a pointer where state is stored, must point to NULL on the
 *               first call
 * @return the next format, or NULL when the iteration is finished
 */
const D3D12FilterFormat *ff_d3d12_filter_format_iterate(void **opaque);

/**
 * Map AVFrame color properties to a DXGI color space.
 *
 * For an input (is_output = 0), properties that no DXGI color space can
 * represent are rejected. For an output request (is_output = 1), they are
 * retagged to the nearest representable value where possible. spc and
 * primaries may be adjusted; range and trc are only inspected. The caller
 * must derive the final AVFrame color metadata, including any transfer
 * characteristic reconciliation required by a conversion.
 *
 * @param input_601_hint primaries of a BT.601 input, used to keep the
 *                       output's PAL/NTSC primaries consistent with it
 */
int ff_d3d12_filter_get_colorspace(AVFilterContext *ctx, int is_rgb, int is_output,
                                   enum AVColorSpace *spc,
                                   enum AVColorRange *range,
                                   enum AVColorPrimaries *primaries,
                                   enum AVColorTransferCharacteristic *trc,
                                   enum AVChromaLocation chroma_loc,
                                   enum AVColorPrimaries input_601_hint,
                                   DXGI_COLOR_SPACE_TYPE *out_space);

enum AVChromaLocation ff_d3d12_filter_chroma_location(DXGI_COLOR_SPACE_TYPE space);

AVRational ff_d3d12_filter_get_input_framerate(AVFilterContext *ctx, AVFilterLink *inlink,
                                               const AVFrame *in);

/**
 * Wait until fence reaches target, or until timeout_ms expires.
 *
 * @return 1 if the fence reached target, 0 otherwise
 */
int ff_d3d12_filter_wait_fence(ID3D12Fence *fence, UINT64 target,
                               HANDLE fence_event, DWORD timeout_ms);

/**
 * Append one transition barrier per plane of a D3D12 frame.
 */
void ff_d3d12_filter_add_frame_barriers(D3D12_RESOURCE_BARRIER *barriers, int *nb_barriers,
                                        const AVD3D12VAFrame *frame, int nb_planes,
                                        D3D12_RESOURCE_STATES state_before,
                                        D3D12_RESOURCE_STATES state_after);

/**
 * Allocate and initialize a pool of D3D12 frames that can be used as
 * render targets. ctx->extra_hw_frames is added to initial_pool_size.
 */
int ff_d3d12_filter_init_output_frames(AVFilterContext *ctx, AVBufferRef *device_ref,
                                       enum AVPixelFormat sw_format, int width, int height,
                                       int initial_pool_size, AVBufferRef **frames_ref);

/**
 * One command queue with its command list, a pool of command allocators
 * and a private fence. Each submission signals the next private fence
 * value; its command allocator and input references are recycled once the
 * fence has passed that value.
 */
typedef struct D3D12CommandContext {
    ID3D12Device *device;
    D3D12_COMMAND_LIST_TYPE type;
    ID3D12CommandQueue *queue;

    /* The member matching type is valid. */
    union {
        ID3D12GraphicsCommandList     *graphics;
        ID3D12VideoProcessCommandList *video_process;
#if HAVE_ID3D12VIDEOENCODECOMMANDLIST
        ID3D12VideoEncodeCommandList  *video_encode;
#endif
    } list;

    /* Owned between ff_d3d12_cmd_begin() and ff_d3d12_cmd_submit(). */
    ID3D12CommandAllocator *command_allocator;
    AVFifo *allocator_queue;

    ID3D12Fence *fence;
    /* Value signaled by the next submission. */
    UINT64 fence_value;
    HANDLE fence_event;

    /* Set by ff_d3d12_cmd_init(), cleared when the context is torn down,
     * including when a command list error tears it down. */
    int ready;
    int gpu_work_submitted;
} D3D12CommandContext;

/**
 * Create the command queue, the fence and a closed command list of the
 * given type, after releasing any previous configuration. type must be
 * DIRECT, VIDEO_PROCESS or, when available in the headers, VIDEO_ENCODE.
 * Releases everything on failure.
 */
int ff_d3d12_cmd_init(AVFilterContext *ctx, D3D12CommandContext *cmd,
                      ID3D12Device *device, D3D12_COMMAND_LIST_TYPE type);

/**
 * Get a command allocator the GPU is done with, and reset it together with
 * the command list for recording. If the command list cannot be reset, the
 * context is torn down and cmd->ready is cleared.
 */
int ff_d3d12_cmd_begin(AVFilterContext *ctx, D3D12CommandContext *cmd);

/**
 * Make the queue of cmd wait, on the GPU, until the latest submission of
 * other has completed. Must be called before the submission of cmd that
 * depends on it. Does nothing if other has no pending work.
 */
int ff_d3d12_cmd_wait_context(AVFilterContext *ctx, D3D12CommandContext *cmd,
                              const D3D12CommandContext *other);

/**
 * Close and execute the command list. The queue first waits for the fences
 * of all inputs and of the output, then signals the output frame's fence.
 * output may be NULL. References to the inputs are kept until the GPU has
 * finished with them. If the command list cannot be closed, the context is
 * torn down and cmd->ready is cleared.
 *
 * @param[out] frames_in_use set to 1 on failure if the GPU may still access
 *                           the frames, in which case the caller must not
 *                           free them
 */
int ff_d3d12_cmd_submit(AVFilterContext *ctx, D3D12CommandContext *cmd,
                        AVFrame *const *inputs, int nb_inputs, AVFrame *output,
                        int *frames_in_use);

/**
 * Wait on the CPU until the latest submission has completed.
 *
 * @return 0 on success, AVERROR_EXTERNAL on timeout or failure
 */
int ff_d3d12_cmd_sync(AVFilterContext *ctx, D3D12CommandContext *cmd, DWORD timeout_ms);

/**
 * Wait for the GPU to finish all submitted work, then release all D3D12
 * objects and clear cmd->ready.
 *
 * @return 0 on success, AVERROR_EXTERNAL if the GPU could not be confirmed
 *         idle, in which case nothing is released
 */
int ff_d3d12_cmd_uninit(AVFilterContext *ctx, D3D12CommandContext *cmd);

#endif /* AVFILTER_D3D12_FILTER_H */
