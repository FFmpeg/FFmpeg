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

#define COBJMACROS

#include <stdint.h>
#include <string.h>

#include "libavutil/avassert.h"
#include "libavutil/fifo.h"
#include "libavutil/frame.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_d3d12va.h"
#include "libavutil/pixdesc.h"

#include "d3d12_filter.h"
#include "filters.h"

typedef struct D3D12CommandAllocator {
    ID3D12CommandAllocator *command_allocator;
    UINT64 fence_value;
    AVFrame *input_refs[D3D12_FILTER_MAX_INPUTS];
    int nb_input_refs;
} D3D12CommandAllocator;

static const D3D12FilterFormat d3d12_filter_formats[] = {
    { AV_PIX_FMT_NV12,    DXGI_FORMAT_NV12,               0 },
    { AV_PIX_FMT_P010,    DXGI_FORMAT_P010,               0 },
    { AV_PIX_FMT_P012,    DXGI_FORMAT_P016,               0 },
    { AV_PIX_FMT_BGRA,    DXGI_FORMAT_B8G8R8A8_UNORM,     1 },
    { AV_PIX_FMT_RGBA,    DXGI_FORMAT_R8G8B8A8_UNORM,     1 },
    { AV_PIX_FMT_X2BGR10, DXGI_FORMAT_R10G10B10A2_UNORM,  1 },
    { AV_PIX_FMT_RGBAF16, DXGI_FORMAT_R16G16B16A16_FLOAT, 1 },
};

const D3D12FilterFormat *ff_d3d12_filter_format_from_pix_fmt(enum AVPixelFormat fmt)
{
    for (int i = 0; i < FF_ARRAY_ELEMS(d3d12_filter_formats); i++)
        if (d3d12_filter_formats[i].pix_fmt == fmt)
            return &d3d12_filter_formats[i];

    return NULL;
}

const D3D12FilterFormat *ff_d3d12_filter_format_from_dxgi(DXGI_FORMAT fmt)
{
    for (int i = 0; i < FF_ARRAY_ELEMS(d3d12_filter_formats); i++)
        if (d3d12_filter_formats[i].dxgi_format == fmt)
            return &d3d12_filter_formats[i];

    return NULL;
}

const D3D12FilterFormat *ff_d3d12_filter_format_iterate(void **opaque)
{
    uintptr_t i = (uintptr_t)*opaque;

    if (i >= FF_ARRAY_ELEMS(d3d12_filter_formats))
        return NULL;

    *opaque = (void *)(i + 1);

    return &d3d12_filter_formats[i];
}

static int d3d12_filter_known_matrix(enum AVColorSpace spc)
{
    switch (spc) {
    case AVCOL_SPC_UNSPECIFIED:
    case AVCOL_SPC_BT709:
    case AVCOL_SPC_BT470BG:
    case AVCOL_SPC_SMPTE170M:
    case AVCOL_SPC_BT2020_NCL:
    case AVCOL_SPC_BT2020_CL:
        return 1;

    default:
        return 0;
    }
}

static int d3d12_filter_known_sdr_trc(enum AVColorTransferCharacteristic trc)
{
    switch (trc) {
    case AVCOL_TRC_UNSPECIFIED:
    case AVCOL_TRC_BT709:
    case AVCOL_TRC_GAMMA22:
    case AVCOL_TRC_GAMMA28:
    case AVCOL_TRC_SMPTE170M:
    case AVCOL_TRC_SMPTE240M:
    case AVCOL_TRC_IEC61966_2_1:
    case AVCOL_TRC_BT2020_10:
    case AVCOL_TRC_BT2020_12:
        return 1;

    default:
        return 0;
    }
}

static int d3d12_filter_reconcile_primaries(AVFilterContext *ctx, int is_output,
                                         enum AVColorPrimaries *primaries,
                                         enum AVColorPrimaries expected_primaries,
                                         const char *space_description)
{
    if (*primaries != AVCOL_PRI_UNSPECIFIED && *primaries != expected_primaries) {
        const char *old_name = av_color_primaries_name(*primaries);
        const char *new_name = av_color_primaries_name(expected_primaries);
        if (is_output) {
            av_log(ctx, AV_LOG_WARNING,
                   "%s forces fixed primaries; retagging output from "
                   "%s to %s\n",
                   space_description,
                   old_name ? old_name : "unknown", new_name ? new_name : "unknown");
        } else {
            av_log(ctx, AV_LOG_ERROR,
                   "%s forces fixed primaries, which disagrees with "
                   "this input's %s primaries tag\n",
                   space_description, old_name ? old_name : "unknown");

            return AVERROR(EINVAL);
        }
    }

    *primaries = expected_primaries;
    return 0;
}

static int d3d12_filter_reject_unrepresentable_nonleft(AVFilterContext *ctx, int is_output,
                                                     enum AVChromaLocation chroma_loc)
{
    if (chroma_loc == AVCHROMA_LOC_UNSPECIFIED || chroma_loc == AVCHROMA_LOC_LEFT)
        return 0;

    if (is_output) {
        av_log(ctx, AV_LOG_WARNING,
               "This DXGI color space has no chroma entry other than "
               "left-sited; the output will actually be left-sited\n");

        return 0;
    }

    av_log(ctx, AV_LOG_ERROR,
           "This DXGI color space has no chroma entry other than "
           "left-sited, but this input is tagged with a different siting\n");

    return AVERROR(EINVAL);
}

static int d3d12_filter_reject_unrepresentable_left(AVFilterContext *ctx, int is_output,
                                                  enum AVChromaLocation chroma_loc)
{
    if (chroma_loc == AVCHROMA_LOC_UNSPECIFIED || chroma_loc == AVCHROMA_LOC_TOPLEFT)
        return 0;

    if (is_output) {
        av_log(ctx, AV_LOG_WARNING,
               "HLG has no DXGI entry with any chroma siting other than "
               "top-left; the output will actually be top-left-sited\n");

        return 0;
    }

    av_log(ctx, AV_LOG_ERROR,
           "HLG has no DXGI entry with any chroma siting other than "
           "top-left, but this input is tagged with a different siting\n");

    return AVERROR(EINVAL);
}

static int d3d12_filter_reject_unrepresentable_siting_2020(AVFilterContext *ctx, int is_output,
                                                         enum AVChromaLocation chroma_loc)
{
    if (chroma_loc == AVCHROMA_LOC_UNSPECIFIED ||
        chroma_loc == AVCHROMA_LOC_TOPLEFT ||
        chroma_loc == AVCHROMA_LOC_LEFT)
        return 0;

    if (is_output) {
        av_log(ctx, AV_LOG_WARNING,
               "DXGI BT.2020 only supports top-left/left chroma "
               "siting; output will use one of those instead\n");

        return 0;
    }

    av_log(ctx, AV_LOG_ERROR,
           "DXGI BT.2020 only supports top-left/left chroma siting, "
           "but this input is tagged with a different siting\n");

    return AVERROR(EINVAL);
}

int ff_d3d12_filter_get_colorspace(AVFilterContext *ctx, int is_rgb, int is_output,
                                   enum AVColorSpace *spc,
                                   enum AVColorRange *range,
                                   enum AVColorPrimaries *primaries,
                                   enum AVColorTransferCharacteristic *trc,
                                   enum AVChromaLocation chroma_loc,
                                   enum AVColorPrimaries input_601_hint,
                                   DXGI_COLOR_SPACE_TYPE *out_space)
{
    int full_range = (*range == AVCOL_RANGE_JPEG);
    int ret;

    if (!is_rgb && *spc == AVCOL_SPC_BT2020_CL) {
        if (is_output) {
            av_log(ctx, AV_LOG_WARNING,
                   "BT.2020 constant-luminance has no DXGI color space; "
                   "retagging output as bt2020nc\n");
            *spc = AVCOL_SPC_BT2020_NCL;
        } else {
            av_log(ctx, AV_LOG_ERROR,
                   "BT.2020 constant-luminance matrix has no DXGI color "
                   "space; cannot process this input without misinterpreting\n");

            return AVERROR(EINVAL);
        }
    }

    if (!is_rgb && !d3d12_filter_known_matrix(*spc)) {
        const char *name = av_color_space_name(*spc);
        av_log(ctx, AV_LOG_ERROR,
               "%s matrix has no DXGI YCbCr color space; only BT.601, "
               "BT.709 or BT.2020 are supported\n", name ? name : "this");

        return AVERROR(EINVAL);
    }

    if (!is_rgb && *trc != AVCOL_TRC_SMPTE2084 && *trc != AVCOL_TRC_ARIB_STD_B67 &&
        !d3d12_filter_known_sdr_trc(*trc)) {
        const char *name = av_color_transfer_name(*trc);
        av_log(ctx, AV_LOG_ERROR,
               "%s transfer has no DXGI YCbCr color space; only SDR "
               "gamma, smpte2084 or HLG (arib-std-b67) are "
               "supported\n",
               name ? name : "this");

        return AVERROR(EINVAL);
    }

    int primaries_is_2020 = (*primaries == AVCOL_PRI_BT2020);
    int is_2020    = (*spc == AVCOL_SPC_BT2020_NCL || *spc == AVCOL_SPC_BT2020_CL ||
                      (*spc == AVCOL_SPC_UNSPECIFIED && primaries_is_2020));
    int is_601     = (*spc == AVCOL_SPC_BT470BG || *spc == AVCOL_SPC_SMPTE170M ||
                      (*spc == AVCOL_SPC_UNSPECIFIED &&
                       (*primaries == AVCOL_PRI_BT470BG || *primaries == AVCOL_PRI_SMPTE170M)));

    if (is_rgb) {
        enum AVColorPrimaries expected_primaries;

        switch (*trc) {
        case AVCOL_TRC_SMPTE2084:
            *out_space = full_range ? DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020
                                    : DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020;
            expected_primaries = AVCOL_PRI_BT2020;
            break;

        case AVCOL_TRC_ARIB_STD_B67:
            if (is_output)
                av_log(ctx, AV_LOG_ERROR,
                       "HLG has no RGB DXGI color space; use a YUV "
                       "output format or a supported out_color_transfer\n");
            else
                av_log(ctx, AV_LOG_ERROR,
                       "HLG transfer characteristic has no RGB DXGI color "
                       "space; this RGB input cannot be processed\n");
            return AVERROR(EINVAL);

        case AVCOL_TRC_LINEAR:
            if (!full_range) {
                if (is_output)
                    av_log(ctx, AV_LOG_ERROR,
                           "Studio range linear RGB has no DXGI color "
                           "space; use out_range=full, or a non-linear "
                           "out_color_transfer (e.g. bt709)\n");
                else
                    av_log(ctx, AV_LOG_ERROR,
                           "Studio range linear RGB has no DXGI color "
                           "space; this input cannot be processed\n");

                return AVERROR(EINVAL);
            }
            *out_space = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
            expected_primaries = AVCOL_PRI_BT709;
            break;

        default:
            if (!d3d12_filter_known_sdr_trc(*trc)) {
                const char *name = av_color_transfer_name(*trc);
                av_log(ctx, AV_LOG_ERROR,
                       "%s transfer has no RGB DXGI color space; only "
                       "SDR gamma, linear or smpte2084 are "
                       "supported\n", name ? name : "this");

                return AVERROR(EINVAL);
            }
            if (primaries_is_2020) {
                *out_space = full_range ? DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020
                                        : DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P2020;
                expected_primaries = AVCOL_PRI_BT2020;
            } else {
                *out_space = full_range ? DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709
                                        : DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709;
                expected_primaries = AVCOL_PRI_BT709;
            }
            break;
        }

        return d3d12_filter_reconcile_primaries(ctx, is_output, primaries, expected_primaries,
                                             "this RGB transfer characteristic");
    }

    /* YCbCr family. */
    if (is_2020) {
        int want_topleft = (chroma_loc == AVCHROMA_LOC_TOPLEFT);
        ret = d3d12_filter_reject_unrepresentable_siting_2020(ctx, is_output, chroma_loc);
        if (ret < 0)
            return ret;

        if (*trc == AVCOL_TRC_SMPTE2084) {
            if (full_range) {
                av_log(ctx, AV_LOG_ERROR,
                       "No full-range smpte2084 YCbCr DXGI color space "
                       "exists; cannot process this %s\n",
                       is_output ? "output request" : "input");

                return AVERROR(EINVAL);
            }
            *out_space = want_topleft ? DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_TOPLEFT_P2020
                                      : DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_LEFT_P2020;
        } else if (*trc == AVCOL_TRC_ARIB_STD_B67) {
            ret = d3d12_filter_reject_unrepresentable_left(ctx, is_output, chroma_loc);
            if (ret < 0)
                return ret;
            *out_space = full_range ? DXGI_COLOR_SPACE_YCBCR_FULL_GHLG_TOPLEFT_P2020
                                    : DXGI_COLOR_SPACE_YCBCR_STUDIO_GHLG_TOPLEFT_P2020;
        } else {
            if (want_topleft && full_range) {
                if (!is_output) {
                    av_log(ctx, AV_LOG_ERROR,
                           "Full-range BT.2020 SDR has no top-left-sited "
                           "DXGI YCbCr color space\n");

                    return AVERROR(EINVAL);
                }
                av_log(ctx, AV_LOG_WARNING,
                       "Full-range BT.2020 SDR has no top-left-sited DXGI "
                       "YCbCr color space; falling back to left-sited\n");
                want_topleft = 0;
            }
            if (want_topleft)
                *out_space = DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_TOPLEFT_P2020;
            else
                *out_space = full_range ? DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P2020
                                        : DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P2020;
        }

        ret = d3d12_filter_reconcile_primaries(ctx, is_output, primaries, AVCOL_PRI_BT2020,
                                            "a BT.2020 matrix");
        if (ret < 0)
            return ret;
    } else if (is_601) {
        if (*trc == AVCOL_TRC_SMPTE2084 || *trc == AVCOL_TRC_ARIB_STD_B67) {
            if (is_output)
                av_log(ctx, AV_LOG_ERROR,
                       "%s transfer has no BT.601 DXGI color space; use "
                       "out_color_matrix=bt2020nc or an SDR transfer\n",
                       *trc == AVCOL_TRC_SMPTE2084 ? "smpte2084" : "HLG");
            else
                av_log(ctx, AV_LOG_ERROR,
                       "%s transfer has no BT.601 DXGI color space; this "
                       "input cannot be processed.\n",
                       *trc == AVCOL_TRC_SMPTE2084 ? "smpte2084" : "HLG");

            return AVERROR(EINVAL);
        }
        ret = d3d12_filter_reject_unrepresentable_nonleft(ctx, is_output, chroma_loc);
        if (ret < 0)
            return ret;
        *out_space = full_range ? DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P601
                                : DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P601;

        enum AVColorPrimaries this_601_primaries =
            *spc == AVCOL_SPC_BT470BG ? AVCOL_PRI_BT470BG :
            *spc == AVCOL_SPC_SMPTE170M ? AVCOL_PRI_SMPTE170M :
            *primaries;

        if (is_output && input_601_hint != AVCOL_PRI_UNSPECIFIED) {
            if (*primaries != AVCOL_PRI_UNSPECIFIED && *primaries != input_601_hint) {
                const char *in_name  = av_color_primaries_name(input_601_hint);
                const char *out_name = av_color_primaries_name(*primaries);
                av_log(ctx, AV_LOG_ERROR,
                       "DXGI's BT.601 color spaces cannot distinguish "
                       "PAL/NTSC primaries (%s input, %s output)\n",
                       in_name ? in_name : "unknown", out_name ? out_name : "unknown");

                return AVERROR(EINVAL);
            }
            if (*primaries != input_601_hint) {
                const char *new_name = av_color_primaries_name(input_601_hint);
                av_log(ctx, AV_LOG_WARNING,
                       "BT.601 cannot change primaries; tagging output "
                       "as %s\n",
                       new_name ? new_name : "unknown");
            }
            *primaries = input_601_hint;
        } else if (!is_output) {
            if (*primaries != AVCOL_PRI_UNSPECIFIED &&
                *primaries != AVCOL_PRI_BT470BG && *primaries != AVCOL_PRI_SMPTE170M) {
                const char *old_name = av_color_primaries_name(*primaries);
                av_log(ctx, AV_LOG_ERROR,
                       "BT.601 only represents PAL/NTSC primaries, "
                       "which disagrees with this input's %s tag\n",
                       old_name ? old_name : "unknown");

                return AVERROR(EINVAL);
            }
            if (*primaries == AVCOL_PRI_UNSPECIFIED)
                *primaries = this_601_primaries;
        } else {
            ret = d3d12_filter_reconcile_primaries(ctx, is_output, primaries, this_601_primaries,
                                                "this BT.601 matrix");
            if (ret < 0)
                return ret;
        }
    } else {
        /* Same gap as the BT.601 case above: no smpte2084/HLG DXGI color
         * space exists paired with a BT.709 matrix either. */
        if (*trc == AVCOL_TRC_SMPTE2084 || *trc == AVCOL_TRC_ARIB_STD_B67) {
            if (is_output)
                av_log(ctx, AV_LOG_ERROR,
                       "%s has no BT.709 DXGI space; use "
                       "out_color_matrix=bt2020nc or SDR\n",
                       *trc == AVCOL_TRC_SMPTE2084 ? "smpte2084" : "HLG");
            else
                av_log(ctx, AV_LOG_ERROR,
                       "%s has no BT.709 DXGI space; retag/convert to "
                       "BT.2020 or SDR before %s\n",
                       *trc == AVCOL_TRC_SMPTE2084 ? "smpte2084" : "HLG",
                       ctx->filter->name);

            return AVERROR(EINVAL);
        }
        ret = d3d12_filter_reject_unrepresentable_nonleft(ctx, is_output, chroma_loc);
        if (ret < 0)
            return ret;
        *out_space = full_range ? DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P709
                                : DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709;             ///< Default to Rec.709

        ret = d3d12_filter_reconcile_primaries(ctx, is_output, primaries, AVCOL_PRI_BT709,
                                            "the default BT.709 matrix");
        if (ret < 0)
            return ret;
    }

    return 0;
}

enum AVChromaLocation ff_d3d12_filter_chroma_location(DXGI_COLOR_SPACE_TYPE space)
{
    switch (space) {
    case DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_TOPLEFT_P2020:
    case DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_TOPLEFT_P2020:
    case DXGI_COLOR_SPACE_YCBCR_STUDIO_GHLG_TOPLEFT_P2020:
    case DXGI_COLOR_SPACE_YCBCR_FULL_GHLG_TOPLEFT_P2020:
    case DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_TOPLEFT_P2020:
        return AVCHROMA_LOC_TOPLEFT;

    default:
        return AVCHROMA_LOC_LEFT;
    }
}

AVRational ff_d3d12_filter_get_input_framerate(AVFilterContext *ctx, AVFilterLink *inlink,
                                               const AVFrame *in)
{
    AVRational framerate = {0, 0};
    FilterLink *inl = ff_filter_link(inlink);

    if (inl->frame_rate.num > 0 && inl->frame_rate.den > 0) {
        framerate = inl->frame_rate;
    } else if (in->duration > 0 && inlink->time_base.num > 0 && inlink->time_base.den > 0) {
        av_reduce(&framerate.num, &framerate.den,
                  inlink->time_base.den, in->duration * inlink->time_base.num,
                  INT_MAX);
    } else {
        framerate.num = 30;
        framerate.den = 1;
        av_log(ctx, AV_LOG_WARNING, "Input framerate not determinable, defaulting to 30fps\n");
    }

    return framerate;
}

int ff_d3d12_filter_wait_fence(ID3D12Fence *fence, UINT64 target,
                               HANDLE fence_event, DWORD timeout_ms)
{
    int has_deadline = (timeout_ms != INFINITE);
    ULONGLONG deadline = 0;

    if (ID3D12Fence_GetCompletedValue(fence) >= target)
        return 1;

    if (FAILED(ID3D12Fence_SetEventOnCompletion(fence, target, fence_event)))
        return 0;

    if (has_deadline)
        deadline = GetTickCount64() + timeout_ms;

    for (;;) {
        DWORD wait_result = WaitForSingleObject(fence_event, timeout_ms);

        if (wait_result == WAIT_OBJECT_0) {
            if (ID3D12Fence_GetCompletedValue(fence) >= target)
                return 1;
            if (has_deadline) {
                ULONGLONG now = GetTickCount64();
                if (now >= deadline)
                    return 0;
                timeout_ms = (DWORD)(deadline - now);
            }
            continue;
        }

        return 0;
    }
}

void ff_d3d12_filter_add_frame_barriers(D3D12_RESOURCE_BARRIER *barriers, int *nb_barriers,
                                        const AVD3D12VAFrame *frame, int nb_planes,
                                        D3D12_RESOURCE_STATES state_before,
                                        D3D12_RESOURCE_STATES state_after)
{
    ID3D12Resource *resource = frame->texture;
    D3D12_RESOURCE_DESC desc;

    resource->lpVtbl->GetDesc(resource, &desc);

    for (int p = 0; p < nb_planes; p++) {
        D3D12_RESOURCE_BARRIER b = {
            .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
            .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
            .Transition = {
                .pResource   = resource,
                .Subresource = frame->subresource_index + p * desc.DepthOrArraySize,
                .StateBefore = state_before,
                .StateAfter  = state_after
            }
        };
        barriers[(*nb_barriers)++] = b;
    }
}

int ff_d3d12_filter_init_output_frames(AVFilterContext *ctx, AVBufferRef *device_ref,
                                       enum AVPixelFormat sw_format, int width, int height,
                                       int initial_pool_size, AVBufferRef **frames_ref)
{
    const D3D12FilterFormat *map = ff_d3d12_filter_format_from_pix_fmt(sw_format);
    AVHWFramesContext *frames_ctx;
    AVD3D12VAFramesContext *frames_hwctx;
    AVBufferRef *ref;
    int ret;

    if (!map) {
        av_log(ctx, AV_LOG_ERROR, "Unsupported output format: %s\n",
               av_get_pix_fmt_name(sw_format));
        return AVERROR(EINVAL);
    }

    ref = av_hwframe_ctx_alloc(device_ref);
    if (!ref)
        return AVERROR(ENOMEM);

    frames_ctx = (AVHWFramesContext *)ref->data;
    frames_ctx->format            = AV_PIX_FMT_D3D12;
    frames_ctx->sw_format         = sw_format;
    frames_ctx->width             = width;
    frames_ctx->height            = height;
    frames_ctx->initial_pool_size = initial_pool_size;
    if (ctx->extra_hw_frames > 0)
        frames_ctx->initial_pool_size += ctx->extra_hw_frames;

    frames_hwctx = frames_ctx->hwctx;
    frames_hwctx->format         = map->dxgi_format;
    frames_hwctx->resource_flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    frames_hwctx->heap_flags     = D3D12_HEAP_FLAG_NONE;

    ret = av_hwframe_ctx_init(ref);
    if (ret < 0) {
        av_buffer_unref(&ref);
        return ret;
    }

    *frames_ref = ref;

    return 0;
}

static ID3D12CommandList *d3d12_cmd_get_list(const D3D12CommandContext *cmd)
{
    switch (cmd->type) {
    case D3D12_COMMAND_LIST_TYPE_DIRECT:
        return (ID3D12CommandList *)cmd->list.graphics;
    case D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS:
        return (ID3D12CommandList *)cmd->list.video_process;
#if HAVE_ID3D12VIDEOENCODECOMMANDLIST
    case D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE:
        return (ID3D12CommandList *)cmd->list.video_encode;
#endif
    default:
        return NULL;
    }
}

static HRESULT d3d12_cmd_reset_list(D3D12CommandContext *cmd, ID3D12CommandAllocator *allocator)
{
    switch (cmd->type) {
    case D3D12_COMMAND_LIST_TYPE_DIRECT:
        return ID3D12GraphicsCommandList_Reset(cmd->list.graphics, allocator, NULL);
    case D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS:
        return ID3D12VideoProcessCommandList_Reset(cmd->list.video_process, allocator);
#if HAVE_ID3D12VIDEOENCODECOMMANDLIST
    case D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE:
        return ID3D12VideoEncodeCommandList_Reset(cmd->list.video_encode, allocator);
#endif
    default:
        return E_INVALIDARG;
    }
}

static HRESULT d3d12_cmd_close_list(D3D12CommandContext *cmd)
{
    switch (cmd->type) {
    case D3D12_COMMAND_LIST_TYPE_DIRECT:
        return ID3D12GraphicsCommandList_Close(cmd->list.graphics);
    case D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS:
        return ID3D12VideoProcessCommandList_Close(cmd->list.video_process);
#if HAVE_ID3D12VIDEOENCODECOMMANDLIST
    case D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE:
        return ID3D12VideoEncodeCommandList_Close(cmd->list.video_encode);
#endif
    default:
        return E_INVALIDARG;
    }
}

static void d3d12_cmd_free_input_refs(D3D12CommandAllocator *allocator)
{
    for (int i = 0; i < allocator->nb_input_refs; i++)
        av_frame_free(&allocator->input_refs[i]);
    allocator->nb_input_refs = 0;
}

static int d3d12_cmd_get_valid_command_allocator(D3D12CommandContext *cmd, AVFilterContext *ctx,
                                                 ID3D12CommandAllocator **ppAllocator)
{
    HRESULT hr;
    D3D12CommandAllocator allocator;

    if (av_fifo_peek(cmd->allocator_queue, &allocator, 1, 0) >= 0) {
        UINT64 completed = ID3D12Fence_GetCompletedValue(cmd->fence);
        if (completed >= allocator.fence_value) {
            *ppAllocator = allocator.command_allocator;
            av_fifo_read(cmd->allocator_queue, &allocator, 1);
            d3d12_cmd_free_input_refs(&allocator);
            return 0;
        }
    }

    hr = ID3D12Device_CreateCommandAllocator(cmd->device, cmd->type,
                                             &IID_ID3D12CommandAllocator, (void **)ppAllocator);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create a new command allocator: HRESULT 0x%lX\n", hr);
        return AVERROR_EXTERNAL;
    }

    return 0;
}

/* Takes ownership of the command allocator and input references. */
static int d3d12_cmd_discard_command_allocator(D3D12CommandContext *cmd, AVFilterContext *ctx,
                                               D3D12CommandAllocator *allocator)
{
    int ret = av_fifo_write(cmd->allocator_queue, allocator, 1);

    if (ret < 0) {
        int confirmed_done = cmd->fence && cmd->fence_event &&
            ff_d3d12_filter_wait_fence(cmd->fence, allocator->fence_value, cmd->fence_event, INFINITE);

        if (confirmed_done) {
            ID3D12CommandAllocator_Release(allocator->command_allocator);
            d3d12_cmd_free_input_refs(allocator);
        } else {
            av_log(ctx, AV_LOG_ERROR,
                   "Failed to queue a command allocator for later reuse\n");
        }
    }

    return ret;
}

int ff_d3d12_cmd_uninit(AVFilterContext *ctx, D3D12CommandContext *cmd)
{
    int gpu_confirmed_done = 1;

    cmd->ready = 0;

    if (cmd->gpu_work_submitted && cmd->queue && cmd->fence && cmd->fence_event) {
        UINT64 drain_value = cmd->fence_value + 1;

        gpu_confirmed_done = 0;

        if (SUCCEEDED(ID3D12CommandQueue_Signal(cmd->queue, cmd->fence, drain_value))) {
            UINT64 completed;
            /* this gives one more value as defense in depth, while cost is almost nothing */
            cmd->fence_value = drain_value + 1;
            completed = ID3D12Fence_GetCompletedValue(cmd->fence);
            if (completed >= drain_value)
                gpu_confirmed_done = 1;
            else
                gpu_confirmed_done = ff_d3d12_filter_wait_fence(cmd->fence, drain_value,
                                                                cmd->fence_event, INFINITE);
        }

        if (!gpu_confirmed_done) {
            HRESULT removed_reason = ID3D12Device_GetDeviceRemovedReason(cmd->device);
            gpu_confirmed_done = FAILED(removed_reason);
            if (gpu_confirmed_done)
                av_log(ctx, AV_LOG_ERROR,
                       "D3D12 device removed (HRESULT 0x%lX) while tearing "
                       "down %s\n", removed_reason, ctx->filter->name);
        }
    }

    if (!gpu_confirmed_done) {
        av_log(ctx, AV_LOG_ERROR,
               "Could not confirm the GPU finished all %s work "
               "during teardown\n", ctx->filter->name);

        return AVERROR_EXTERNAL;
    }

    if (cmd->fence_event) {
        CloseHandle(cmd->fence_event);
        cmd->fence_event = NULL;
    }

    if (cmd->fence) {
        ID3D12Fence_Release(cmd->fence);
        cmd->fence = NULL;
    }

    if (d3d12_cmd_get_list(cmd)) {
        ID3D12CommandList_Release(d3d12_cmd_get_list(cmd));
        memset(&cmd->list, 0, sizeof(cmd->list));
    }

    if (cmd->command_allocator) {
        ID3D12CommandAllocator_Release(cmd->command_allocator);
        cmd->command_allocator = NULL;
    }

    if (cmd->allocator_queue) {
        D3D12CommandAllocator allocator;
        while (av_fifo_read(cmd->allocator_queue, &allocator, 1) >= 0) {
            ID3D12CommandAllocator_Release(allocator.command_allocator);
            d3d12_cmd_free_input_refs(&allocator);
        }
        av_fifo_freep2(&cmd->allocator_queue);
    }

    if (cmd->queue) {
        ID3D12CommandQueue_Release(cmd->queue);
        cmd->queue = NULL;
    }

    cmd->gpu_work_submitted = 0;

    return 0;
}

int ff_d3d12_cmd_init(AVFilterContext *ctx, D3D12CommandContext *cmd,
                      ID3D12Device *device, D3D12_COMMAND_LIST_TYPE type)
{
    D3D12CommandAllocator allocator = { 0 };
    const IID *list_iid;
    HRESULT hr;
    int ret;

    switch (type) {
    case D3D12_COMMAND_LIST_TYPE_DIRECT:
        list_iid = &IID_ID3D12GraphicsCommandList;
        break;
    case D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS:
        list_iid = &IID_ID3D12VideoProcessCommandList;
        break;
#if HAVE_ID3D12VIDEOENCODECOMMANDLIST
    case D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE:
        list_iid = &IID_ID3D12VideoEncodeCommandList;
        break;
#endif
    default:
        av_log(ctx, AV_LOG_ERROR, "Unsupported command list type %d\n", type);
        return AVERROR(EINVAL);
    }

    if (ff_d3d12_cmd_uninit(ctx, cmd) < 0) {
        av_log(ctx, AV_LOG_ERROR,
               "Cannot configure a new D3D12 command queue\n");
        return AVERROR_EXTERNAL;
    }

    cmd->device = device;
    cmd->type   = type;

    D3D12_COMMAND_QUEUE_DESC queue_desc = {
        .Type = type,
        .Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
        .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
        .NodeMask = 0
    };

    hr = ID3D12Device_CreateCommandQueue(cmd->device, &queue_desc, &IID_ID3D12CommandQueue, (void **)&cmd->queue);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create command queue: HRESULT 0x%lX\n", hr);
        ff_d3d12_cmd_uninit(ctx, cmd);

        return AVERROR_EXTERNAL;
    }

    hr = ID3D12Device_CreateFence(cmd->device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&cmd->fence);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create fence: HRESULT 0x%lX\n", hr);
        ff_d3d12_cmd_uninit(ctx, cmd);
        return AVERROR_EXTERNAL;
    }

    cmd->fence_value = 1;
    cmd->fence_event = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!cmd->fence_event) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create fence event\n");
        ff_d3d12_cmd_uninit(ctx, cmd);

        return AVERROR_EXTERNAL;
    }

    cmd->allocator_queue = av_fifo_alloc2(2, sizeof(D3D12CommandAllocator), AV_FIFO_FLAG_AUTO_GROW);
    if (!cmd->allocator_queue) {
        ff_d3d12_cmd_uninit(ctx, cmd);
        return AVERROR(ENOMEM);
    }

    ret = d3d12_cmd_get_valid_command_allocator(cmd, ctx, &allocator.command_allocator);
    if (ret < 0) {
        ff_d3d12_cmd_uninit(ctx, cmd);
        return ret;
    }

    hr = ID3D12Device_CreateCommandList(
        cmd->device,
        0,
        cmd->type,
        allocator.command_allocator,
        NULL,
        list_iid,
        (void **)&cmd->list
    );

    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create command list: HRESULT 0x%lX\n", hr);
        ID3D12CommandAllocator_Release(allocator.command_allocator);
        ff_d3d12_cmd_uninit(ctx, cmd);

        return AVERROR_EXTERNAL;
    }

    hr = d3d12_cmd_close_list(cmd);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to close command list: HRESULT 0x%lX\n", hr);
        if (ff_d3d12_cmd_uninit(ctx, cmd) >= 0)
            ID3D12CommandAllocator_Release(allocator.command_allocator);

        return AVERROR_EXTERNAL;
    }

    ret = d3d12_cmd_discard_command_allocator(cmd, ctx, &allocator);
    if (ret < 0) {
        ff_d3d12_cmd_uninit(ctx, cmd);
        return ret;
    }

    cmd->ready = 1;

    return 0;
}

/* Called after a command list error: release everything so the caller
 * configures a new context. */
static void d3d12_cmd_force_reconfigure(AVFilterContext *ctx, D3D12CommandContext *cmd,
                                        ID3D12CommandAllocator *command_allocator)
{
    if (ff_d3d12_cmd_uninit(ctx, cmd) >= 0)
        ID3D12CommandAllocator_Release(command_allocator);
}

int ff_d3d12_cmd_begin(AVFilterContext *ctx, D3D12CommandContext *cmd)
{
    ID3D12CommandAllocator *command_allocator = NULL;
    HRESULT hr;
    int ret;

    ret = d3d12_cmd_get_valid_command_allocator(cmd, ctx, &command_allocator);
    if (ret < 0)
        return ret;

    hr = ID3D12CommandAllocator_Reset(command_allocator);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to reset command allocator: HRESULT 0x%lX\n", hr);
        ID3D12CommandAllocator_Release(command_allocator);
        return AVERROR_EXTERNAL;
    }

    hr = d3d12_cmd_reset_list(cmd, command_allocator);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to reset command list: HRESULT 0x%lX\n", hr);
        d3d12_cmd_force_reconfigure(ctx, cmd, command_allocator);
        return AVERROR_EXTERNAL;
    }

    cmd->command_allocator = command_allocator;

    return 0;
}

int ff_d3d12_cmd_wait_context(AVFilterContext *ctx, D3D12CommandContext *cmd,
                              const D3D12CommandContext *other)
{
    UINT64 last_value;
    HRESULT hr;

    if (!other->fence || other->fence_value <= 1)
        return 0;

    last_value = other->fence_value - 1;
    if (ID3D12Fence_GetCompletedValue(other->fence) >= last_value)
        return 0;

    hr = ID3D12CommandQueue_Wait(cmd->queue, other->fence, last_value);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to wait for another command queue: HRESULT 0x%lX\n", hr);
        return AVERROR_EXTERNAL;
    }
    cmd->gpu_work_submitted = 1;

    return 0;
}

int ff_d3d12_cmd_sync(AVFilterContext *ctx, D3D12CommandContext *cmd, DWORD timeout_ms)
{
    if (!cmd->fence || cmd->fence_value <= 1)
        return 0;

    if (!ff_d3d12_filter_wait_fence(cmd->fence, cmd->fence_value - 1,
                                    cmd->fence_event, timeout_ms)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to wait for %s GPU work\n", ctx->filter->name);
        return AVERROR_EXTERNAL;
    }

    return 0;
}

/* Make the queue wait for prior GPU work on frame before this submission
 * accesses it. Returns 1 if a wait was enqueued, 0 if none was needed. */
static int d3d12_cmd_wait_frame(AVFilterContext *ctx, D3D12CommandContext *cmd,
                                const AVFrame *frame, const char *fence_name)
{
    AVD3D12VAFrame *d3d12_frame = (AVD3D12VAFrame *)frame->data[0];
    HRESULT hr;

    if (!d3d12_frame->sync_ctx.fence || !d3d12_frame->sync_ctx.fence_value)
        return 0;

    if (ID3D12Fence_GetCompletedValue(d3d12_frame->sync_ctx.fence) >= d3d12_frame->sync_ctx.fence_value)
        return 0;

    hr = ID3D12CommandQueue_Wait(cmd->queue, d3d12_frame->sync_ctx.fence,
                                 d3d12_frame->sync_ctx.fence_value);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to wait for %s: HRESULT 0x%lX\n", fence_name, hr);
        return AVERROR_EXTERNAL;
    }
    cmd->gpu_work_submitted = 1;

    return 1;
}

/* After a failure with work possibly queued, try to confirm the GPU is idle
 * so the frames and the command allocator can be released. */
static int d3d12_cmd_sync_after_failure(AVFilterContext *ctx, D3D12CommandContext *cmd)
{
    HRESULT removed_reason = ID3D12Device_GetDeviceRemovedReason(cmd->device);
    int gpu_confirmed_done = FAILED(removed_reason);

    if (gpu_confirmed_done) {
        av_log(ctx, AV_LOG_ERROR,
               "D3D12 device removed (HRESULT 0x%lX); frame "
               "resources are safe to release\n", removed_reason);
    } else if (cmd->queue && cmd->fence && cmd->fence_event) {
        UINT64 last_resort = cmd->fence_value + 1;
        if (SUCCEEDED(ID3D12CommandQueue_Signal(cmd->queue, cmd->fence, last_resort))) {
            UINT64 completed;

            if (last_resort >= cmd->fence_value)
                cmd->fence_value = last_resort + 1;

            completed = ID3D12Fence_GetCompletedValue(cmd->fence);
            if (completed >= last_resort) {
                gpu_confirmed_done = 1;
            } else {
                gpu_confirmed_done = ff_d3d12_filter_wait_fence(cmd->fence, last_resort,
                                                                cmd->fence_event, 5000);
                if (!gpu_confirmed_done)
                    av_log(ctx, AV_LOG_ERROR,
                           "Post-failure GPU wait timed out; frame "
                           "resources cannot be safely recycled\n");
            }
        } else {
            av_log(ctx, AV_LOG_ERROR,
                   "Best-effort post-failure GPU sync Signal also "
                   "failed; frame resources cannot be safely "
                   "recycled\n");
        }
    } else {
        av_log(ctx, AV_LOG_ERROR,
               "No fence/queue for post-failure GPU sync; frame "
               "resources cannot be safely recycled\n");
    }

    return gpu_confirmed_done;
}

int ff_d3d12_cmd_submit(AVFilterContext *ctx, D3D12CommandContext *cmd,
                        AVFrame *const *inputs, int nb_inputs, AVFrame *output,
                        int *frames_in_use)
{
    D3D12CommandAllocator allocator = {
        .command_allocator = cmd->command_allocator,
    };
    AVD3D12VAFrame *output_frame = output ? (AVD3D12VAFrame *)output->data[0] : NULL;
    UINT64 next_output_fence_value, next_private_fence_value;
    int wait_enqueued = 0;
    HRESULT hr;
    int ret;

    av_assert0(nb_inputs <= D3D12_FILTER_MAX_INPUTS);

    cmd->command_allocator = NULL;
    *frames_in_use = 0;

    hr = d3d12_cmd_close_list(cmd);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to close command list: HRESULT 0x%lX\n", hr);
        d3d12_cmd_force_reconfigure(ctx, cmd, allocator.command_allocator);
        return AVERROR_EXTERNAL;
    }

    /* Wait for the input frames' fences so prior GPU work on these
     * surfaces completes before the command list reads them. */
    for (int i = 0; i < nb_inputs; i++) {
        ret = d3d12_cmd_wait_frame(ctx, cmd, inputs[i], "input fence");
        if (ret < 0)
            goto wait_fail;
        wait_enqueued |= ret;
    }

    if (output) {
        ret = d3d12_cmd_wait_frame(ctx, cmd, output, "output frame fence");
        if (ret < 0)
            goto wait_fail;
    }

    ID3D12CommandList *cmd_lists[] = { d3d12_cmd_get_list(cmd) };
    ID3D12CommandQueue_ExecuteCommandLists(cmd->queue, 1, cmd_lists);
    cmd->gpu_work_submitted = 1;

    next_private_fence_value = cmd->fence_value;

    if (output_frame) {
        next_output_fence_value = output_frame->sync_ctx.fence_value + 1;

        hr = ID3D12CommandQueue_Signal(cmd->queue, output_frame->sync_ctx.fence,
                                       next_output_fence_value);
        if (FAILED(hr)) {
            av_log(ctx, AV_LOG_ERROR, "Failed to signal output frame fence: HRESULT 0x%lX\n", hr);
            ret = AVERROR_EXTERNAL;
            goto submit_fail;
        }
        output_frame->sync_ctx.fence_value = next_output_fence_value;
    }

    hr = ID3D12CommandQueue_Signal(cmd->queue, cmd->fence, next_private_fence_value);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to signal fence: HRESULT 0x%lX\n", hr);
        ret = AVERROR_EXTERNAL;
        goto submit_fail;
    }

    cmd->fence_value++;

    allocator.fence_value = next_private_fence_value;
    for (int i = 0; i < nb_inputs; i++) {
        allocator.input_refs[i] = av_frame_clone(inputs[i]);
        if (!allocator.input_refs[i]) {
            ret = AVERROR(ENOMEM);
            goto submit_fail;
        }
        allocator.nb_input_refs++;
    }

    return d3d12_cmd_discard_command_allocator(cmd, ctx, &allocator);

wait_fail:
    if (!wait_enqueued)
        goto fail;

submit_fail:
    if (!d3d12_cmd_sync_after_failure(ctx, cmd)) {
        av_log(ctx, AV_LOG_ERROR,
               "Abandoning this frame's input/output surfaces and "
               "command allocator without releasing them\n");
        *frames_in_use = 1;
        return ret;
    }

fail:
    ID3D12CommandAllocator_Release(allocator.command_allocator);
    d3d12_cmd_free_input_refs(&allocator);
    return ret;
}
