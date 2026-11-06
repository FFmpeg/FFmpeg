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

#define COBJMACROS

#include "libavutil/log.h"
#include "libavutil/pixdesc.h"

#include "d3d12_vpp.h"

static void d3d12_vpp_release(D3D12VPPContext *vpp)
{
    if (vpp->video_processor) {
        ID3D12VideoProcessor_Release(vpp->video_processor);
        vpp->video_processor = NULL;
    }

    if (vpp->video_device) {
        ID3D12VideoDevice_Release(vpp->video_device);
        vpp->video_device = NULL;
    }
}

/* The command context tears itself down after a command list error; the
 * video processor then has to be configured again. */
static void d3d12_vpp_check_cmd(D3D12VPPContext *vpp)
{
    if (vpp->cmd.ready)
        return;

    vpp->configured = 0;
    if (!vpp->cmd.queue)
        d3d12_vpp_release(vpp);
}

int ff_d3d12_vpp_uninit(AVFilterContext *ctx, D3D12VPPContext *vpp)
{
    int ret;

    vpp->configured = 0;

    ret = ff_d3d12_cmd_uninit(ctx, &vpp->cmd);
    if (ret < 0)
        return ret;

    d3d12_vpp_release(vpp);

    return 0;
}

int ff_d3d12_vpp_init(AVFilterContext *ctx, D3D12VPPContext *vpp, ID3D12Device *device)
{
    HRESULT hr;
    int ret;

    if (ff_d3d12_vpp_uninit(ctx, vpp) < 0) {
        av_log(ctx, AV_LOG_ERROR,
               "Cannot configure a new D3D12 video processor\n");
        return AVERROR_EXTERNAL;
    }

    hr = ID3D12Device_QueryInterface(device, &IID_ID3D12VideoDevice, (void **)&vpp->video_device);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to get D3D12 video device interface: HRESULT 0x%lX\n", hr);

        return AVERROR_EXTERNAL;
    }

    ret = ff_d3d12_cmd_init(ctx, &vpp->cmd, device, D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS);
    if (ret < 0) {
        ff_d3d12_vpp_uninit(ctx, vpp);
        return ret;
    }

    return 0;
}

void ff_d3d12_vpp_report_output_support(AVFilterContext *ctx, D3D12VPPContext *vpp,
                                        const D3D12_FEATURE_DATA_VIDEO_PROCESS_SUPPORT *support)
{
    D3D12_FEATURE_DATA_VIDEO_PROCESS_SUPPORT probe;
    const D3D12FilterFormat *map;
    void *iter = NULL;

    if (av_log_get_level() < AV_LOG_DEBUG)
        return;

    while ((map = ff_d3d12_filter_format_iterate(&iter))) {
        DXGI_COLOR_SPACE_TYPE candidate_space;
        enum AVColorRange candidate_range = map->is_rgb ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
        enum AVColorSpace candidate_spc = AVCOL_SPC_UNSPECIFIED;
        enum AVColorPrimaries candidate_primaries = AVCOL_PRI_UNSPECIFIED;
        enum AVColorTransferCharacteristic candidate_trc = AVCOL_TRC_UNSPECIFIED;
        HRESULT hr;

        if (ff_d3d12_filter_get_colorspace(ctx, map->is_rgb, 1, &candidate_spc,
                                           &candidate_range, &candidate_primaries,
                                           &candidate_trc, AVCHROMA_LOC_UNSPECIFIED,
                                           AVCOL_PRI_UNSPECIFIED,
                                           &candidate_space) < 0)
            continue;

        probe = *support;
        probe.OutputFormat.Format     = map->dxgi_format;
        probe.OutputFormat.ColorSpace = candidate_space;

        hr = ID3D12VideoDevice_CheckFeatureSupport(vpp->video_device, D3D12_FEATURE_VIDEO_PROCESS_SUPPORT,
                                                   &probe, sizeof(probe));

        av_log(ctx, AV_LOG_DEBUG, "Probe output %s (DXGI %d, colorspace %d): %s\n",
               av_get_pix_fmt_name(map->pix_fmt), map->dxgi_format, candidate_space,
               (SUCCEEDED(hr) && (probe.SupportFlags & D3D12_VIDEO_PROCESS_SUPPORT_FLAG_SUPPORTED)) ?
               "supported" : "unsupported");
    }
}

int ff_d3d12_vpp_create_processor(AVFilterContext *ctx, D3D12VPPContext *vpp,
                                  const D3D12_VIDEO_PROCESS_OUTPUT_STREAM_DESC *output_desc,
                                  const D3D12_VIDEO_PROCESS_INPUT_STREAM_DESC *input_desc)
{
    HRESULT hr;

    hr = ID3D12VideoDevice_CreateVideoProcessor(
        vpp->video_device,
        0,
        output_desc,
        1,
        input_desc,
        &IID_ID3D12VideoProcessor,
        (void **)&vpp->video_processor
    );

    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create video processor: HRESULT 0x%lX\n", hr);
        ff_d3d12_vpp_uninit(ctx, vpp);
        return AVERROR_EXTERNAL;
    }

    vpp->configured = 1;

    return 0;
}

int ff_d3d12_vpp_begin(AVFilterContext *ctx, D3D12VPPContext *vpp)
{
    int ret = ff_d3d12_cmd_begin(ctx, &vpp->cmd);

    d3d12_vpp_check_cmd(vpp);

    return ret;
}

int ff_d3d12_vpp_submit(AVFilterContext *ctx, D3D12VPPContext *vpp,
                        AVFrame *const *inputs, int nb_inputs, AVFrame *output,
                        int *frames_in_use)
{
    int ret = ff_d3d12_cmd_submit(ctx, &vpp->cmd, inputs, nb_inputs, output,
                                  frames_in_use);

    d3d12_vpp_check_cmd(vpp);

    return ret;
}
