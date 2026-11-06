/*
 * D3D12VA video processor helpers
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

#ifndef AVFILTER_D3D12_VPP_H
#define AVFILTER_D3D12_VPP_H

#include "libavutil/frame.h"
#include "libavutil/hwcontext_d3d12va.h"

#include "avfilter.h"
#include "d3d12_filter.h"

/**
 * Video processor state shared by the D3D12 filters. All work is submitted
 * to the video process queue of cmd.
 */
typedef struct D3D12VPPContext {
    D3D12CommandContext cmd;

    ID3D12VideoDevice *video_device;
    ID3D12VideoProcessor *video_processor;

    int configured;
} D3D12VPPContext;

/**
 * Create the video device and the video process command context on device,
 * after releasing any previous configuration.
 */
int ff_d3d12_vpp_init(AVFilterContext *ctx, D3D12VPPContext *vpp, ID3D12Device *device);

/**
 * Log, at debug level, which output formats the video processor supports
 * for the input described in support.
 */
void ff_d3d12_vpp_report_output_support(AVFilterContext *ctx, D3D12VPPContext *vpp,
                                        const D3D12_FEATURE_DATA_VIDEO_PROCESS_SUPPORT *support);

/**
 * Create the video processor and mark vpp as configured. Must be called
 * after ff_d3d12_vpp_init(). Releases everything on failure.
 */
int ff_d3d12_vpp_create_processor(AVFilterContext *ctx, D3D12VPPContext *vpp,
                                  const D3D12_VIDEO_PROCESS_OUTPUT_STREAM_DESC *output_desc,
                                  const D3D12_VIDEO_PROCESS_INPUT_STREAM_DESC *input_desc);

/**
 * Start recording into vpp->cmd.list.video_process, see ff_d3d12_cmd_begin().
 * Clears vpp->configured if the command context was torn down.
 */
int ff_d3d12_vpp_begin(AVFilterContext *ctx, D3D12VPPContext *vpp);

/**
 * Execute the recorded commands, see ff_d3d12_cmd_submit(). Clears
 * vpp->configured if the command context was torn down.
 */
int ff_d3d12_vpp_submit(AVFilterContext *ctx, D3D12VPPContext *vpp,
                        AVFrame *const *inputs, int nb_inputs, AVFrame *output,
                        int *frames_in_use);

/**
 * Wait for the GPU to finish all submitted work, then release all D3D12
 * objects and clear vpp->configured.
 *
 * @return 0 on success, AVERROR_EXTERNAL if the GPU could not be confirmed
 *         idle, in which case nothing is released
 */
int ff_d3d12_vpp_uninit(AVFilterContext *ctx, D3D12VPPContext *vpp);

#endif /* AVFILTER_D3D12_VPP_H */
