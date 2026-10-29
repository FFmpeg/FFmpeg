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

#include "libavutil/fifo.h"
#include "libavutil/frame.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "compat/w32dlfcn.h"

#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_d3d12va.h"
#include "libavutil/hwcontext_d3d12va_internal.h"

#include "filters.h"
#include "formats.h"
#include "scale_eval.h"
#include "video.h"

typedef struct D3D12ScaleCommandAllocator {
    ID3D12CommandAllocator *command_allocator;
    UINT64 fence_value;
    AVFrame *input_ref;
} D3D12ScaleCommandAllocator;

typedef struct D3D12ScaleFormatMap {
    enum AVPixelFormat  pix_fmt;
    DXGI_FORMAT         dxgi_format;
    int                 is_rgb;
} D3D12ScaleFormatMap;

static const D3D12ScaleFormatMap d3d12_scale_format_map[] = {
    { AV_PIX_FMT_NV12,    DXGI_FORMAT_NV12,               0 },
    { AV_PIX_FMT_P010,    DXGI_FORMAT_P010,               0 },
    { AV_PIX_FMT_P012,    DXGI_FORMAT_P016,               0 },
    { AV_PIX_FMT_BGRA,    DXGI_FORMAT_B8G8R8A8_UNORM,     1 },
    { AV_PIX_FMT_RGBA,    DXGI_FORMAT_R8G8B8A8_UNORM,     1 },
    { AV_PIX_FMT_X2BGR10, DXGI_FORMAT_R10G10B10A2_UNORM,  1 },
    { AV_PIX_FMT_RGBAF16, DXGI_FORMAT_R16G16B16A16_FLOAT, 1 },
};

static const D3D12ScaleFormatMap *d3d12_scale_map_from_pixfmt(enum AVPixelFormat fmt)
{
    for (int i = 0; i < FF_ARRAY_ELEMS(d3d12_scale_format_map); i++)
        if (d3d12_scale_format_map[i].pix_fmt == fmt)
            return &d3d12_scale_format_map[i];

    return NULL;
}

static const D3D12ScaleFormatMap *d3d12_scale_map_from_dxgi(DXGI_FORMAT fmt)
{
    for (int i = 0; i < FF_ARRAY_ELEMS(d3d12_scale_format_map); i++)
        if (d3d12_scale_format_map[i].dxgi_format == fmt)
            return &d3d12_scale_format_map[i];

    return NULL;
}

typedef struct ScaleD3D12Context {
    const AVClass *classCtx;
    char *w_expr;
    char *h_expr;
    enum AVPixelFormat format;

    char *color_matrix_string;
    char *color_transfer_string;
    char *color_primaries_string;
    int   color_range;
    enum AVColorSpace color_matrix;
    enum AVColorTransferCharacteristic color_transfer;
    enum AVColorPrimaries color_primaries;

    /* D3D12 objects */
    ID3D12Device *device;
    ID3D12VideoDevice *video_device;
    ID3D12VideoProcessor *video_processor;
    ID3D12CommandQueue *command_queue;
    ID3D12VideoProcessCommandList *command_list;

    AVFifo *allocator_queue;

    int configured;
    int gpu_work_submitted;

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
    int input_is_rgb;
    int output_is_rgb;
    int input_has_alpha;
    int output_has_alpha;
    int alpha_passthrough;

    /* Color space and frame rate */
    DXGI_COLOR_SPACE_TYPE input_colorspace;
    DXGI_COLOR_SPACE_TYPE output_colorspace;
    AVRational input_framerate;

    /* AVFrame-facing output color properties, derived once from the first
     * frame plus the out_* options and then reused for every frame. */
    enum AVColorSpace output_av_colorspace;
    enum AVColorRange output_av_color_range;
    enum AVColorPrimaries output_av_color_primaries;
    enum AVColorTransferCharacteristic output_av_color_trc;

    enum AVColorPrimaries input_av_color_primaries;
    enum AVColorTransferCharacteristic input_av_color_trc;

    /* Video processor capabilities */
    D3D12_FEATURE_DATA_VIDEO_PROCESS_SUPPORT process_support;
} ScaleD3D12Context;

static av_cold int scale_d3d12_init(AVFilterContext *ctx) {
    ScaleD3D12Context *s = ctx->priv;

#define STRING_OPTION(var_name, func_name, opt_name, default_value) do {      \
        if (s->var_name ## _string) {                                         \
            int var = av_ ## func_name ## _from_name(s->var_name ## _string); \
            if (var < 0) {                                                    \
                av_log(ctx, AV_LOG_ERROR, "Invalid %s.\n", opt_name);         \
                return AVERROR(EINVAL);                                       \
            }                                                                 \
            s->var_name = var;                                                \
        } else {                                                              \
            s->var_name = default_value;                                      \
        }                                                                     \
    } while (0)

    STRING_OPTION(color_matrix,    color_space,     "out_color_matrix",    AVCOL_SPC_UNSPECIFIED);
    STRING_OPTION(color_transfer,  color_transfer,  "out_color_transfer",  AVCOL_TRC_UNSPECIFIED);
    STRING_OPTION(color_primaries, color_primaries, "out_color_primaries", AVCOL_PRI_UNSPECIFIED);

#undef STRING_OPTION

    return 0;
}

static int scale_d3d12_wait_fence_exact(ID3D12Fence *fence, UINT64 target,
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

static int scale_d3d12_get_valid_command_allocator(ScaleD3D12Context *s, AVFilterContext *ctx,
                                                    ID3D12CommandAllocator **ppAllocator)
{
    HRESULT hr;
    D3D12ScaleCommandAllocator allocator;

    if (av_fifo_peek(s->allocator_queue, &allocator, 1, 0) >= 0) {
        UINT64 completed = ID3D12Fence_GetCompletedValue(s->fence);
        if (completed >= allocator.fence_value) {
            *ppAllocator = allocator.command_allocator;
            av_fifo_read(s->allocator_queue, &allocator, 1);
            av_frame_free(&allocator.input_ref);
            return 0;
        }
    }

    hr = ID3D12Device_CreateCommandAllocator(s->device, D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS,
                                             &IID_ID3D12CommandAllocator, (void **)ppAllocator);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create a new command allocator: HRESULT 0x%lX\n", hr);
        return AVERROR_EXTERNAL;
    }

    return 0;
}

static int scale_d3d12_discard_command_allocator(ScaleD3D12Context *s, AVFilterContext *ctx,
                                                  ID3D12CommandAllocator *pAllocator,
                                                  UINT64 fence_value, AVFrame *input_ref)
{
    D3D12ScaleCommandAllocator allocator = {
        .command_allocator = pAllocator,
        .fence_value       = fence_value,
        .input_ref         = input_ref,
    };
    int ret = av_fifo_write(s->allocator_queue, &allocator, 1);

    if (ret < 0) {
        int confirmed_done = s->fence && s->fence_event &&
            scale_d3d12_wait_fence_exact(s->fence, fence_value, s->fence_event, INFINITE);

        if (confirmed_done) {
            ID3D12CommandAllocator_Release(pAllocator);
            av_frame_free(&input_ref);
        } else {
            av_log(ctx, AV_LOG_ERROR,
                   "Failed to queue a command allocator for later reuse\n");
        }
    }

    return ret;
}

static int release_d3d12_resources(ScaleD3D12Context *s, AVFilterContext *ctx) {
    int gpu_confirmed_done = 1;

    s->configured = 0;

    if (s->gpu_work_submitted && s->command_queue && s->fence && s->fence_event) {
        UINT64 drain_value = s->fence_value + 1;

        gpu_confirmed_done = 0;

        if (SUCCEEDED(ID3D12CommandQueue_Signal(s->command_queue, s->fence, drain_value))) {
            UINT64 completed;
            /* this gives one more value as defense in depth, while cost is almost nothing */
            s->fence_value = drain_value + 1;
            completed = ID3D12Fence_GetCompletedValue(s->fence);
            if (completed >= drain_value)
                gpu_confirmed_done = 1;
            else
                gpu_confirmed_done = scale_d3d12_wait_fence_exact(s->fence, drain_value,
                                                                  s->fence_event, INFINITE);
        }

        if (!gpu_confirmed_done) {
            HRESULT removed_reason = ID3D12Device_GetDeviceRemovedReason(s->device);
            gpu_confirmed_done = FAILED(removed_reason);
            if (gpu_confirmed_done)
                av_log(ctx, AV_LOG_ERROR,
                       "D3D12 device removed (HRESULT 0x%lX) while tearing "
                       "down scale_d3d12\n", removed_reason);
        }
    }

    if (!gpu_confirmed_done) {
        av_log(ctx, AV_LOG_ERROR,
               "Could not confirm the GPU finished all scale_d3d12 work "
               "during teardown\n");

        return 0;
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

    if (s->allocator_queue) {
        D3D12ScaleCommandAllocator allocator;
        while (av_fifo_read(s->allocator_queue, &allocator, 1) >= 0) {
            ID3D12CommandAllocator_Release(allocator.command_allocator);
            av_frame_free(&allocator.input_ref);
        }
        av_fifo_freep2(&s->allocator_queue);
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

    s->gpu_work_submitted = 0;

    return 1;
}

static int d3d12_scale_known_matrix(enum AVColorSpace spc)
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

static int d3d12_scale_known_sdr_trc(enum AVColorTransferCharacteristic trc)
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

static int d3d12_scale_trc_group(enum AVColorTransferCharacteristic trc)
{
    switch (trc) {
    case AVCOL_TRC_BT709:
    case AVCOL_TRC_SMPTE170M:
    case AVCOL_TRC_BT2020_10:
    case AVCOL_TRC_BT2020_12:
        return 0; /* trc_bt709 */

    case AVCOL_TRC_GAMMA22:
        return 1; /* trc_gamma22 */

    case AVCOL_TRC_GAMMA28:
        return 2; /* trc_gamma28 */

    case AVCOL_TRC_SMPTE240M:
        return 3; /* trc_smpte240M */

    case AVCOL_TRC_IEC61966_2_1:
        return 4; /* trc_iec61966_2_1 (sRGB) */

    default:
        return -1;
    }
}

static int d3d12_scale_finalize_output_trc(AVFilterContext *ctx,
                                           DXGI_COLOR_SPACE_TYPE input_space,
                                           DXGI_COLOR_SPACE_TYPE output_space,
                                           int explicit_request,
                                           enum AVColorTransferCharacteristic input_trc,
                                           enum AVColorTransferCharacteristic *trc)
{
    enum AVColorTransferCharacteristic canonical =
        (output_space == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709) ?
            AVCOL_TRC_IEC61966_2_1 : AVCOL_TRC_BT709;
    enum AVColorTransferCharacteristic canonical_input =
        (input_trc == AVCOL_TRC_SMPTE2084 ||
         input_trc == AVCOL_TRC_ARIB_STD_B67 ||
         input_trc == AVCOL_TRC_LINEAR) ? input_trc :
        (input_space == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709) ?
            AVCOL_TRC_IEC61966_2_1 : AVCOL_TRC_BT709;

    if (canonical_input != canonical) {
        if (*trc != canonical) {
            const char *old_name = av_color_transfer_name(*trc);
            const char *new_name = av_color_transfer_name(canonical);
            av_log(ctx, AV_LOG_WARNING,
                   "No exact DXGI color space; retagging transfer "
                   "characteristic from %s to %s\n",
                   old_name ? old_name : "unknown", new_name ? new_name : "unknown");
            *trc = canonical;
        }

        return 0;
    }

    if (!explicit_request)
        return 0;

    if (d3d12_scale_trc_group(*trc) == d3d12_scale_trc_group(input_trc))
        return 0;

    const char *req_name = av_color_transfer_name(*trc);

    if (input_trc == AVCOL_TRC_UNSPECIFIED) {
        av_log(ctx, AV_LOG_ERROR,
               "Cannot verify requested transfer characteristic (%s); "
               "input's curve is unspecified/unknown\n",
               req_name ? req_name : "requested value");
    } else {
        const char *old_name = av_color_transfer_name(input_trc);
        av_log(ctx, AV_LOG_ERROR,
               "Requested transfer characteristic (%s) differs from "
               "input's (%s); same DXGI color space means no curve "
               "change applies\n",
               req_name ? req_name : "requested value",
               old_name ? old_name : "transfer characteristic");
    }

    return AVERROR(EINVAL);
}

static int d3d12_scale_reconcile_primaries(AVFilterContext *ctx, int is_output,
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

static int d3d12_scale_reject_unrepresentable_nonleft(AVFilterContext *ctx, int is_output,
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

static int d3d12_scale_reject_unrepresentable_left(AVFilterContext *ctx, int is_output,
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

static int d3d12_scale_reject_unrepresentable_siting_2020(AVFilterContext *ctx, int is_output,
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

static int d3d12_scale_get_colorspace(AVFilterContext *ctx, int is_rgb, int is_output,
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

    if (!is_rgb && !d3d12_scale_known_matrix(*spc)) {
        const char *name = av_color_space_name(*spc);
        av_log(ctx, AV_LOG_ERROR,
               "%s matrix has no DXGI YCbCr color space; only BT.601, "
               "BT.709 or BT.2020 are supported\n", name ? name : "this");

        return AVERROR(EINVAL);
    }

    if (!is_rgb && *trc != AVCOL_TRC_SMPTE2084 && *trc != AVCOL_TRC_ARIB_STD_B67 &&
        !d3d12_scale_known_sdr_trc(*trc)) {
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
            if (!d3d12_scale_known_sdr_trc(*trc)) {
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

        return d3d12_scale_reconcile_primaries(ctx, is_output, primaries, expected_primaries,
                                               "this RGB transfer characteristic");
    }

    /* YCbCr family. */
    if (is_2020) {
        int want_topleft = (chroma_loc == AVCHROMA_LOC_TOPLEFT);
        ret = d3d12_scale_reject_unrepresentable_siting_2020(ctx, is_output, chroma_loc);
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
            ret = d3d12_scale_reject_unrepresentable_left(ctx, is_output, chroma_loc);
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

        ret = d3d12_scale_reconcile_primaries(ctx, is_output, primaries, AVCOL_PRI_BT2020,
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
        ret = d3d12_scale_reject_unrepresentable_nonleft(ctx, is_output, chroma_loc);
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
            ret = d3d12_scale_reconcile_primaries(ctx, is_output, primaries, this_601_primaries,
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
                       "BT.2020 or SDR before scale_d3d12\n",
                       *trc == AVCOL_TRC_SMPTE2084 ? "smpte2084" : "HLG");

            return AVERROR(EINVAL);
        }
        ret = d3d12_scale_reject_unrepresentable_nonleft(ctx, is_output, chroma_loc);
        if (ret < 0)
            return ret;
        *out_space = full_range ? DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P709
                                : DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709;             ///< Default to Rec.709

        ret = d3d12_scale_reconcile_primaries(ctx, is_output, primaries, AVCOL_PRI_BT709,
                                              "the default BT.709 matrix");
        if (ret < 0)
            return ret;
    }

    return 0;
}

static enum AVChromaLocation d3d12_scale_dxgi_chroma_location(DXGI_COLOR_SPACE_TYPE space)
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

static void scale_d3d12_report_output_support(ScaleD3D12Context *s, AVFilterContext *ctx)
{
    D3D12_FEATURE_DATA_VIDEO_PROCESS_SUPPORT probe;

    if (av_log_get_level() < AV_LOG_DEBUG)
        return;

    for (int i = 0; i < FF_ARRAY_ELEMS(d3d12_scale_format_map); i++) {
        const D3D12ScaleFormatMap *map = &d3d12_scale_format_map[i];
        DXGI_COLOR_SPACE_TYPE candidate_space;
        enum AVColorRange candidate_range = map->is_rgb ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
        enum AVColorSpace candidate_spc = AVCOL_SPC_UNSPECIFIED;
        enum AVColorPrimaries candidate_primaries = AVCOL_PRI_UNSPECIFIED;
        enum AVColorTransferCharacteristic candidate_trc = AVCOL_TRC_UNSPECIFIED;
        HRESULT hr;

        if (d3d12_scale_get_colorspace(ctx, map->is_rgb, 1, &candidate_spc,
                                       &candidate_range, &candidate_primaries,
                                       &candidate_trc, AVCHROMA_LOC_UNSPECIFIED,
                                       AVCOL_PRI_UNSPECIFIED,
                                       &candidate_space) < 0)
            continue;

        probe = s->process_support;
        probe.OutputFormat.Format     = map->dxgi_format;
        probe.OutputFormat.ColorSpace = candidate_space;

        hr = ID3D12VideoDevice_CheckFeatureSupport(s->video_device, D3D12_FEATURE_VIDEO_PROCESS_SUPPORT,
                                                   &probe, sizeof(probe));

        av_log(ctx, AV_LOG_DEBUG, "Probe output %s (DXGI %d, colorspace %d): %s\n",
               av_get_pix_fmt_name(map->pix_fmt), map->dxgi_format, candidate_space,
               (SUCCEEDED(hr) && (probe.SupportFlags & D3D12_VIDEO_PROCESS_SUPPORT_FLAG_SUPPORTED)) ?
               "supported" : "unsupported");
    }
}

static AVRational get_input_framerate(AVFilterContext *ctx, AVFilterLink *inlink, AVFrame *in)
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

static int scale_d3d12_configure_processor(ScaleD3D12Context *s, AVFilterContext *ctx) {
    HRESULT hr;
    int ret;
    ID3D12CommandAllocator *command_allocator = NULL;

    if (s->video_device || s->command_queue || s->fence || s->fence_event ||
        s->command_list || s->video_processor || s->allocator_queue) {
        if (!release_d3d12_resources(s, ctx)) {
            av_log(ctx, AV_LOG_ERROR,
                   "Cannot configure a new D3D12 video processor\n");
            return AVERROR_EXTERNAL;
        }
    }

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
        release_d3d12_resources(s, ctx);

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
    s->process_support.OutputFormat.ColorSpace       = s->output_colorspace;
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
        release_d3d12_resources(s, ctx);

        return AVERROR_EXTERNAL;
    }

    if (!(s->process_support.SupportFlags & D3D12_VIDEO_PROCESS_SUPPORT_FLAG_SUPPORTED)) {
        av_log(ctx, AV_LOG_ERROR,
               "D3D12 video processor does not support converting %s (colorspace %d) to %s "
               "(colorspace %d) on this device\n",
               av_get_pix_fmt_name(d3d12_scale_map_from_dxgi(s->input_format) ?
                                   d3d12_scale_map_from_dxgi(s->input_format)->pix_fmt : AV_PIX_FMT_NONE),
               s->input_colorspace,
               av_get_pix_fmt_name(d3d12_scale_map_from_dxgi(s->output_format) ?
                                   d3d12_scale_map_from_dxgi(s->output_format)->pix_fmt : AV_PIX_FMT_NONE),
               s->output_colorspace);
        scale_d3d12_report_output_support(s, ctx);
        release_d3d12_resources(s, ctx);

        return AVERROR(ENOSYS);
    }

    int want_alpha_passthrough = s->input_has_alpha && s->output_has_alpha;
    if (want_alpha_passthrough &&
        !(s->process_support.FeatureSupport & D3D12_VIDEO_PROCESS_FEATURE_FLAG_ALPHA_FILL)) {
        av_log(ctx, AV_LOG_WARNING,
               "Device lacks ALPHA_FILL support; output alpha will be "
               "forced opaque instead of preserved\n");
        want_alpha_passthrough = 0;
    }
    s->alpha_passthrough = want_alpha_passthrough;

    D3D12_VIDEO_PROCESS_OUTPUT_STREAM_DESC processor_output_desc = {
        .Format                         = s->output_format,
        .ColorSpace                     = s->output_colorspace,
        .AlphaFillMode                  = want_alpha_passthrough ? D3D12_VIDEO_PROCESS_ALPHA_FILL_MODE_SOURCE_STREAM
                                                                  : D3D12_VIDEO_PROCESS_ALPHA_FILL_MODE_OPAQUE,
        .AlphaFillModeSourceStreamIndex = 0,
        .BackgroundColor                = { 0.0f, 0.0f, 0.0f, 1.0f },
        .FrameRate                      = { s->input_framerate.num, s->input_framerate.den },
        .EnableStereo                   = FALSE,
    };

    D3D12_VIDEO_PROCESS_INPUT_STREAM_DESC processor_input_desc = {
        .Format                 = s->input_format,
        .ColorSpace             = s->input_colorspace,
        .SourceAspectRatio      = { 1, 1 },
        .DestinationAspectRatio = { 1, 1 },
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
        release_d3d12_resources(s, ctx);
        return AVERROR_EXTERNAL;
    }

    hr = ID3D12Device_CreateFence(s->device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&s->fence);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create fence: HRESULT 0x%lX\n", hr);
        release_d3d12_resources(s, ctx);
        return AVERROR_EXTERNAL;
    }

    s->fence_value = 1;
    s->fence_event = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!s->fence_event) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create fence event\n");
        release_d3d12_resources(s, ctx);

        return AVERROR_EXTERNAL;
    }

    s->allocator_queue = av_fifo_alloc2(2, sizeof(D3D12ScaleCommandAllocator), AV_FIFO_FLAG_AUTO_GROW);
    if (!s->allocator_queue) {
        release_d3d12_resources(s, ctx);
        return AVERROR(ENOMEM);
    }

    ret = scale_d3d12_get_valid_command_allocator(s, ctx, &command_allocator);
    if (ret < 0) {
        release_d3d12_resources(s, ctx);
        return ret;
    }

    hr = ID3D12Device_CreateCommandList(
        s->device,
        0,
        D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS,
        command_allocator,
        NULL,
        &IID_ID3D12VideoProcessCommandList,
        (void **)&s->command_list
    );

    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create command list: HRESULT 0x%lX\n", hr);
        ID3D12CommandAllocator_Release(command_allocator);
        release_d3d12_resources(s, ctx);

        return AVERROR_EXTERNAL;
    }

    hr = ID3D12VideoProcessCommandList_Close(s->command_list);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to close command list: HRESULT 0x%lX\n", hr);
        if (release_d3d12_resources(s, ctx))
            ID3D12CommandAllocator_Release(command_allocator);

        return AVERROR_EXTERNAL;
    }

    ret = scale_d3d12_discard_command_allocator(s, ctx, command_allocator, 0, NULL);
    if (ret < 0) {
        release_d3d12_resources(s, ctx);
        return ret;
    }

    s->configured = 1;

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
    ID3D12CommandAllocator *command_allocator = NULL;
    HRESULT removed_reason;
    int gpu_confirmed_done;

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

    AVD3D12VADeviceContext *input_d3d12_hwctx  = (AVD3D12VADeviceContext *)input_device_ctx->hwctx;
    AVD3D12VADeviceContext *filter_d3d12_hwctx = (AVD3D12VADeviceContext *)filter_device_ctx->hwctx;
    if (input_d3d12_hwctx->device != filter_d3d12_hwctx->device) {
        av_log(ctx, AV_LOG_ERROR,
               "Input frame's hw_frames_ctx belongs to a different "
               "ID3D12Device than this filter's own device\n");
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

    if (!s->configured) {
        AVHWFramesContext *input_frames_ctx = (AVHWFramesContext *)in->hw_frames_ctx->data;
        const D3D12ScaleFormatMap *in_map, *out_map;
        enum AVColorSpace out_spc;
        enum AVColorRange out_range;
        enum AVColorPrimaries out_primaries;
        enum AVColorTransferCharacteristic out_trc;

        s->input_width  = in->width;
        s->input_height = in->height;

        AVD3D12VAFramesContext *input_hwctx = (AVD3D12VAFramesContext *)input_frames_ctx->hwctx;
        s->input_format = input_hwctx->format;

        if (s->input_format == DXGI_FORMAT_UNKNOWN) {
            in_map = d3d12_scale_map_from_pixfmt(input_frames_ctx->sw_format);
            if (!in_map) {
                av_log(ctx, AV_LOG_ERROR, "Unsupported input format: %s\n",
                       av_get_pix_fmt_name(input_frames_ctx->sw_format));
                ret = AVERROR(EINVAL);
                goto fail;
            }
            s->input_format = in_map->dxgi_format;
        }

        in_map = d3d12_scale_map_from_dxgi(s->input_format);
        s->input_is_rgb = in_map ? in_map->is_rgb : 0;
        const AVPixFmtDescriptor *in_desc = in_map ? av_pix_fmt_desc_get(in_map->pix_fmt) : NULL;
        s->input_has_alpha = in_desc && (in_desc->flags & AV_PIX_FMT_FLAG_ALPHA);

        enum AVColorRange in_range = in->color_range != AVCOL_RANGE_UNSPECIFIED ? in->color_range
                                     : (s->input_is_rgb ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG);
        enum AVColorSpace in_spc = in->colorspace;
        enum AVColorPrimaries in_primaries = in->color_primaries;
        enum AVColorTransferCharacteristic in_trc = in->color_trc;

        ret = d3d12_scale_get_colorspace(ctx, s->input_is_rgb, 0, &in_spc, &in_range,
                                         &in_primaries, &in_trc, in->chroma_location,
                                         AVCOL_PRI_UNSPECIFIED, &s->input_colorspace);
        if (ret < 0)
            goto fail;

        s->input_av_color_primaries = in_primaries;
        s->input_av_color_trc       = in_trc;

        enum AVColorPrimaries input_601_hint = AVCOL_PRI_UNSPECIFIED;
        if (s->input_colorspace == DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P601 ||
            s->input_colorspace == DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P601)
            input_601_hint = in_primaries;

        /* Output color properties: options override, otherwise inherit
         * from the input. RGB output defaults to full range. */
        out_map = d3d12_scale_map_from_dxgi(s->output_format);
        s->output_is_rgb = out_map ? out_map->is_rgb : 0;
        const AVPixFmtDescriptor *out_desc = out_map ? av_pix_fmt_desc_get(out_map->pix_fmt) : NULL;
        s->output_has_alpha = out_desc && (out_desc->flags & AV_PIX_FMT_FLAG_ALPHA);

        out_spc       = s->color_matrix    != AVCOL_SPC_UNSPECIFIED ? s->color_matrix    :
                        s->color_primaries != AVCOL_PRI_UNSPECIFIED ? AVCOL_SPC_UNSPECIFIED :
                        in->colorspace;
        out_primaries = s->color_primaries != AVCOL_PRI_UNSPECIFIED ? s->color_primaries : in_primaries;
        out_trc       = s->color_transfer  != AVCOL_TRC_UNSPECIFIED ? s->color_transfer  : in->color_trc;
        if (s->color_transfer == AVCOL_TRC_UNSPECIFIED && in->color_trc == AVCOL_TRC_UNSPECIFIED &&
            out_map && out_map->pix_fmt == AV_PIX_FMT_RGBAF16)
            out_trc = AVCOL_TRC_LINEAR;
        /* AVCOL_SPC_RGB has no YCbCr matrix of its own, so when converting
         * an RGB input frame back to a YUV output format Treat it as unspecified
         * instead */
        if (!s->output_is_rgb && out_spc == AVCOL_SPC_RGB)
            out_spc = AVCOL_SPC_UNSPECIFIED;

        if (s->color_range != AVCOL_RANGE_UNSPECIFIED)
            out_range = s->color_range;
        else if (out_trc == AVCOL_TRC_LINEAR)
            /* Linear RGB has no studio-range DXGI color space either  default
             * to full range and let the processor do the limited->full expansion
             * instead. */
            out_range = AVCOL_RANGE_JPEG;
        else if (in->color_range != AVCOL_RANGE_UNSPECIFIED)
            out_range = in->color_range;
        else
            out_range = s->output_is_rgb ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;

        ret = d3d12_scale_get_colorspace(ctx, s->output_is_rgb, 1, &out_spc, &out_range,
                                         &out_primaries, &out_trc, in->chroma_location,
                                         input_601_hint, &s->output_colorspace);
        if (ret < 0)
            goto fail;

        if (out_trc != AVCOL_TRC_SMPTE2084 && out_trc != AVCOL_TRC_ARIB_STD_B67 &&
            out_trc != AVCOL_TRC_LINEAR) {
            ret = d3d12_scale_finalize_output_trc(ctx, s->input_colorspace, s->output_colorspace,
                                                  s->color_transfer != AVCOL_TRC_UNSPECIFIED,
                                                  in->color_trc, &out_trc);
            if (ret < 0)
                goto fail;
        }

        if (s->output_is_rgb)
            s->output_av_colorspace = AVCOL_SPC_RGB;
        else if (out_spc != AVCOL_SPC_UNSPECIFIED)
            s->output_av_colorspace = out_spc;
        else if (out_primaries == AVCOL_PRI_BT2020)
            s->output_av_colorspace = AVCOL_SPC_BT2020_NCL;
        else if (out_primaries == AVCOL_PRI_BT470BG || out_primaries == AVCOL_PRI_SMPTE170M)
            s->output_av_colorspace = AVCOL_SPC_SMPTE170M;
        else
            s->output_av_colorspace = AVCOL_SPC_BT709;
        s->output_av_color_range     = out_range;
        s->output_av_color_primaries = out_primaries;
        s->output_av_color_trc       = out_trc;

        /* outlink's colorspace/color_range/chroma_location describe a YUV,
         * subsampled surface; they do not constrain an RGB output, so these
         * checks only apply when the output is YUV. */
        if (!s->output_is_rgb && outlink->colorspace != AVCOL_SPC_UNSPECIFIED &&
            outlink->colorspace != s->output_av_colorspace) {
            av_log(ctx, AV_LOG_ERROR,
                   "Negotiated output color space (%s) does not match the "
                   "%s produced from this input; set an explicit "
                   "out_color_matrix\n",
                   av_color_space_name(outlink->colorspace),
                   av_color_space_name(s->output_av_colorspace));
            ret = AVERROR(EINVAL);
            goto fail;
        }
        if (!s->output_is_rgb && outlink->color_range != AVCOL_RANGE_UNSPECIFIED &&
            outlink->color_range != s->output_av_color_range) {
            av_log(ctx, AV_LOG_ERROR,
                   "Negotiated output color range (%s) does not match the "
                   "%s produced from this input; set an explicit out_range\n",
                   av_color_range_name(outlink->color_range),
                   av_color_range_name(s->output_av_color_range));
            ret = AVERROR(EINVAL);
            goto fail;
        }
        if (!s->output_is_rgb) {
            enum AVChromaLocation out_chroma_loc = d3d12_scale_dxgi_chroma_location(s->output_colorspace);
            if (outlink->chroma_location != AVCHROMA_LOC_UNSPECIFIED &&
                outlink->chroma_location != out_chroma_loc) {
                av_log(ctx, AV_LOG_ERROR,
                       "Negotiated output chroma siting does not match the "
                       "siting produced from this input\n");
                ret = AVERROR(EINVAL);
                goto fail;
            }
        }

        s->input_framerate = get_input_framerate(ctx, inlink, in);

        av_log(ctx, AV_LOG_VERBOSE,
               "Input format: %dx%d, DXGI format: %d, colorspace: %d -> "
               "Output DXGI format: %d, colorspace: %d, framerate: %d/%d\n",
               s->input_width, s->input_height, s->input_format, s->input_colorspace,
               s->output_format, s->output_colorspace,
               s->input_framerate.num, s->input_framerate.den);

        ret = scale_d3d12_configure_processor(s, ctx);
        if (ret < 0) {
            av_log(ctx, AV_LOG_ERROR, "Failed to configure processor\n");
            goto fail;
        }
    }

    /* alpha_mode is a per-frame AVFrame property, not a fixed property of
     * the negotiated pixel formats, so this must be checked for every
     * frame, not just the first one. */
    if (s->input_has_alpha && in->alpha_mode == AVALPHA_MODE_PREMULTIPLIED &&
        (!s->output_has_alpha || !s->alpha_passthrough ||
         s->input_colorspace != s->output_colorspace)) {
        av_log(ctx, AV_LOG_ERROR,
               "Input has premultiplied alpha, which this conversion "
               "cannot preserve; unpremultiply before scale_d3d12\n");
        ret = AVERROR(EINVAL);
        goto fail;
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

    ret = scale_d3d12_get_valid_command_allocator(s, ctx, &command_allocator);
    if (ret < 0)
        goto fail;

    hr = ID3D12CommandAllocator_Reset(command_allocator);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to reset command allocator: HRESULT 0x%lX\n", hr);
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

    hr = ID3D12VideoProcessCommandList_Reset(s->command_list, command_allocator);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to reset command list: HRESULT 0x%lX\n", hr);
        ret = AVERROR_EXTERNAL;
        goto force_reconfigure;
    }

    D3D12_RESOURCE_DESC input_res_desc, output_res_desc;
    input_resource->lpVtbl->GetDesc(input_resource,   &input_res_desc);
    output_resource->lpVtbl->GetDesc(output_resource, &output_res_desc);

    int input_nb_planes  = s->input_is_rgb  ? 1 : 2;
    int output_nb_planes = s->output_is_rgb ? 1 : 2;

    D3D12_RESOURCE_BARRIER barriers[4];
    int nb_barriers = 0;

    for (int p = 0; p < input_nb_planes; p++) {
        D3D12_RESOURCE_BARRIER b = {
            .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
            .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
            .Transition = {
                .pResource   = input_resource,
                .Subresource = input_frame->subresource_index + p * input_res_desc.DepthOrArraySize,
                .StateBefore = D3D12_RESOURCE_STATE_COMMON,
                .StateAfter  = D3D12_RESOURCE_STATE_VIDEO_PROCESS_READ
            }
        };
        barriers[nb_barriers++] = b;
    }
    for (int p = 0; p < output_nb_planes; p++) {
        D3D12_RESOURCE_BARRIER b = {
            .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
            .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
            .Transition = {
                .pResource   = output_resource,
                .Subresource = output_frame->subresource_index + p * output_res_desc.DepthOrArraySize,
                .StateBefore = D3D12_RESOURCE_STATE_COMMON,
                .StateAfter  = D3D12_RESOURCE_STATE_VIDEO_PROCESS_WRITE
            }
        };
        barriers[nb_barriers++] = b;
    }

    ID3D12VideoProcessCommandList_ResourceBarrier(s->command_list, nb_barriers, barriers);

    D3D12_VIDEO_PROCESS_INPUT_STREAM_ARGUMENTS input_args = {0};

    /* subresource_index is 0 for a plain texture, but for a texture-array
     * input (AV_D3D12VA_FRAME_FLAG_TEXTURE_ARRAY) it is the real array
     * slice and must be passed through as-is. */
    input_args.InputStream[0].pTexture2D  = input_resource;
    input_args.InputStream[0].Subresource = input_frame->subresource_index;
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

    /* Same reasoning as input_args.InputStream[0].Subresource above. */
    output_args.OutputStream[0].pTexture2D  = output_resource;
    output_args.OutputStream[0].Subresource = output_frame->subresource_index;
    output_args.TargetRectangle.right  = s->width;
    output_args.TargetRectangle.bottom = s->height;

    ID3D12VideoProcessCommandList_ProcessFrames(
        s->command_list,
        s->video_processor,
        &output_args,
        1,
        &input_args
    );

    for (int i = 0; i < nb_barriers; i++) {
        FFSWAP(D3D12_RESOURCE_STATES, barriers[i].Transition.StateBefore, barriers[i].Transition.StateAfter);
    }
    ID3D12VideoProcessCommandList_ResourceBarrier(s->command_list, nb_barriers, barriers);

    hr = ID3D12VideoProcessCommandList_Close(s->command_list);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to close command list: HRESULT 0x%lX\n", hr);
        ret = AVERROR_EXTERNAL;
        goto force_reconfigure;
    }

    int wait_enqueued = 0;

    /* Wait for the input frame's fence so prior GPU work on this
     * surface completes before the video processor reads it. */
    if (input_frame->sync_ctx.fence && input_frame->sync_ctx.fence_value > 0) {
        UINT64 completed = ID3D12Fence_GetCompletedValue(input_frame->sync_ctx.fence);
        if (completed < input_frame->sync_ctx.fence_value) {
            hr = ID3D12CommandQueue_Wait(s->command_queue, input_frame->sync_ctx.fence, input_frame->sync_ctx.fence_value);
            if (FAILED(hr)) {
                av_log(ctx, AV_LOG_ERROR, "Failed to wait for input fence: HRESULT 0x%lX\n", hr);
                ret = AVERROR_EXTERNAL;
                goto fail;
            }
            wait_enqueued = 1;
            s->gpu_work_submitted = 1;
        }
    }

    if (output_frame->sync_ctx.fence && output_frame->sync_ctx.fence_value > 0) {
        UINT64 completed = ID3D12Fence_GetCompletedValue(output_frame->sync_ctx.fence);
        if (completed < output_frame->sync_ctx.fence_value) {
            hr = ID3D12CommandQueue_Wait(s->command_queue, output_frame->sync_ctx.fence, output_frame->sync_ctx.fence_value);
            if (FAILED(hr)) {
                av_log(ctx, AV_LOG_ERROR, "Failed to wait for output frame fence: HRESULT 0x%lX\n", hr);
                ret = AVERROR_EXTERNAL;
                if (wait_enqueued)
                    goto submit_fail;
                goto fail;
            }
            s->gpu_work_submitted = 1;
        }
    }

    ID3D12CommandList *cmd_lists[] = { (ID3D12CommandList *)s->command_list };
    ID3D12CommandQueue_ExecuteCommandLists(s->command_queue, 1, cmd_lists);
    s->gpu_work_submitted = 1;

    UINT64 next_output_fence_value  = output_frame->sync_ctx.fence_value + 1;
    UINT64 next_private_fence_value = s->fence_value;

    hr = ID3D12CommandQueue_Signal(s->command_queue, output_frame->sync_ctx.fence,
                                   next_output_fence_value);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to signal output frame fence: HRESULT 0x%lX\n", hr);
        ret = AVERROR_EXTERNAL;
        goto submit_fail;
    }
    output_frame->sync_ctx.fence_value = next_output_fence_value;

    hr = ID3D12CommandQueue_Signal(s->command_queue, s->fence, next_private_fence_value);
    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to signal fence: HRESULT 0x%lX\n", hr);
        ret = AVERROR_EXTERNAL;
        goto submit_fail;
    }

    s->fence_value++;

    AVFrame *input_ref = av_frame_clone(in);
    if (!input_ref) {
        ret = AVERROR(ENOMEM);
        goto submit_fail;
    }

    ret = scale_d3d12_discard_command_allocator(s, ctx, command_allocator, next_private_fence_value,
                                                input_ref);
    command_allocator = NULL;
    if (ret < 0)
        goto fail;

    ret = av_frame_copy_props(out, in);
    if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to copy frame properties\n");
        goto fail;
    }

    int in_width  = in->width;
    int in_height = in->height;

    out->width = s->width;
    out->height = s->height;
    out->format = AV_PIX_FMT_D3D12;

    if (out->width != in_width || out->height != in_height)
        av_reduce(&out->sample_aspect_ratio.num, &out->sample_aspect_ratio.den,
                 (int64_t)in->sample_aspect_ratio.num * out->height * in_width,
                 (int64_t)in->sample_aspect_ratio.den * out->width * in_height,
                 INT_MAX);

    if (out->width != in_width || out->height != in_height) {
        av_frame_side_data_remove_by_props(&out->side_data, &out->nb_side_data,
                                           AV_SIDE_DATA_PROP_SIZE_DEPENDENT);
        out->crop_left = out->crop_right = out->crop_top = out->crop_bottom = 0;
    }

    out->colorspace      = s->output_av_colorspace;
    out->color_range     = s->output_av_color_range;
    out->color_primaries = s->output_av_color_primaries;
    out->color_trc       = s->output_av_color_trc;

    if (out->color_primaries != s->input_av_color_primaries || out->color_trc != s->input_av_color_trc)
        av_frame_side_data_remove_by_props(&out->side_data, &out->nb_side_data,
                                           AV_SIDE_DATA_PROP_COLOR_DEPENDENT);

    if (!s->output_has_alpha)
        out->alpha_mode = AVALPHA_MODE_UNSPECIFIED;

    if (s->output_is_rgb) {
        out->chroma_location = AVCHROMA_LOC_UNSPECIFIED;
    } else {
        out->chroma_location = d3d12_scale_dxgi_chroma_location(s->output_colorspace);
    }

    av_frame_free(&in);

    return ff_filter_frame(outlink, out);

submit_fail:
    removed_reason = ID3D12Device_GetDeviceRemovedReason(s->device);
    gpu_confirmed_done = FAILED(removed_reason);

    if (gpu_confirmed_done) {
        av_log(ctx, AV_LOG_ERROR,
               "D3D12 device removed (HRESULT 0x%lX); frame "
               "resources are safe to release\n", removed_reason);
    } else if (s->command_queue && s->fence && s->fence_event) {
        UINT64 last_resort = s->fence_value + 1;
        if (SUCCEEDED(ID3D12CommandQueue_Signal(s->command_queue, s->fence, last_resort))) {
            UINT64 completed;

            if (last_resort >= s->fence_value)
                s->fence_value = last_resort + 1;

            completed = ID3D12Fence_GetCompletedValue(s->fence);
            if (completed >= last_resort) {
                gpu_confirmed_done = 1;
            } else {
                gpu_confirmed_done = scale_d3d12_wait_fence_exact(s->fence, last_resort,
                                                                  s->fence_event, 5000);
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

    if (!gpu_confirmed_done) {
        av_log(ctx, AV_LOG_ERROR,
               "Abandoning this frame's input/output surfaces and "
               "command allocator without releasing them\n");
        return ret;
    }

    goto fail;

force_reconfigure:
    s->configured = 0;
    if (!release_d3d12_resources(s, ctx))
        command_allocator = NULL;

fail:
    if (command_allocator)
        ID3D12CommandAllocator_Release(command_allocator);
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

    if (!release_d3d12_resources(s, ctx)) {
        av_log(ctx, AV_LOG_ERROR,
               "Cannot (re)configure scale_d3d12: a prior configuration's "
               "D3D12 resources could not be confirmed released\n");

        return AVERROR_EXTERNAL;
    }
    av_buffer_unref(&s->hw_frames_ctx_out);
    av_buffer_unref(&s->hw_device_ctx);

    ret = ff_scale_eval_dimensions(s, s->w_expr, s->h_expr, inlink, outlink, &s->width, &s->height);
    if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to evaluate dimensions\n");
        return ret;
    }

    /* Adjust dimensions to meet codec/hardware alignment requirements */
    ret = ff_scale_adjust_dimensions(inlink, &s->width, &s->height, 0, 1, 1.f);
    if (ret < 0)
        return ret;

    outlink->w = s->width;
    outlink->h = s->height;

    if (inlink->sample_aspect_ratio.num) {
        AVRational q = av_div_q((AVRational){ inlink->w, inlink->h },
                                (AVRational){ outlink->w, outlink->h });
        outlink->sample_aspect_ratio = av_mul_q(q, inlink->sample_aspect_ratio);
    } else {
        outlink->sample_aspect_ratio = inlink->sample_aspect_ratio;
    }

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

    enum AVPixelFormat resolved_format = s->format;
    if (resolved_format == AV_PIX_FMT_NONE) {
        /* If format is not specified, use the same format as input */
        resolved_format = in_frames_ctx->sw_format;
        av_log(ctx, AV_LOG_VERBOSE, "D3D12 scale output format not specified, using input format: %s\n",
               av_get_pix_fmt_name(resolved_format));
    }
    frames_ctx->sw_format = resolved_format;

    /* Set output format based on sw_format */
    const D3D12ScaleFormatMap *out_map = d3d12_scale_map_from_pixfmt(frames_ctx->sw_format);
    if (!out_map) {
        av_log(ctx, AV_LOG_ERROR, "Unsupported output format: %s\n",
               av_get_pix_fmt_name(frames_ctx->sw_format));
        av_buffer_unref(&s->hw_frames_ctx_out);

        return AVERROR(EINVAL);
    }
    s->output_format = out_map->dxgi_format;
    s->output_is_rgb = out_map->is_rgb;

    const D3D12ScaleFormatMap *in_map = d3d12_scale_map_from_pixfmt(in_frames_ctx->sw_format);
    int input_is_rgb = in_map ? in_map->is_rgb : 0;

    if (s->color_primaries != AVCOL_PRI_UNSPECIFIED ||
        s->color_transfer  != AVCOL_TRC_UNSPECIFIED ||
        s->color_matrix    != AVCOL_SPC_UNSPECIFIED ||
        input_is_rgb       != s->output_is_rgb ||
        frames_ctx->sw_format == AV_PIX_FMT_RGBAF16 ||
        (input_is_rgb && s->color_range != AVCOL_RANGE_UNSPECIFIED))
        av_frame_side_data_remove_by_props(&outlink->side_data, &outlink->nb_side_data,
                                           AV_SIDE_DATA_PROP_COLOR_DEPENDENT);

    frames_ctx->width  = s->width;
    frames_ctx->height = s->height;
    frames_ctx->format = AV_PIX_FMT_D3D12;
    frames_ctx->initial_pool_size = 4;

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

    if (release_d3d12_resources(s, ctx)) {
        av_buffer_unref(&s->hw_frames_ctx_out);
        av_buffer_unref(&s->hw_device_ctx);
    }

    av_freep(&s->w_expr);
    av_freep(&s->h_expr);

    av_freep(&s->color_matrix_string);
    av_freep(&s->color_transfer_string);
    av_freep(&s->color_primaries_string);
}

static enum AVColorSpace d3d12_scale_matrix_from_primaries(enum AVColorPrimaries primaries)
{
    if (primaries == AVCOL_PRI_BT2020)
        return AVCOL_SPC_BT2020_NCL;
    if (primaries == AVCOL_PRI_BT470BG || primaries == AVCOL_PRI_SMPTE170M)
        return AVCOL_SPC_SMPTE170M;
    return AVCOL_SPC_BT709;
}

static int scale_d3d12_query_formats(const AVFilterContext *avctx,
                                     AVFilterFormatsConfig **cfg_in,
                                     AVFilterFormatsConfig **cfg_out)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_D3D12, AV_PIX_FMT_NONE,
    };
    static const int out_chroma_locs_any[] = {
        AVCHROMA_LOC_UNSPECIFIED, AVCHROMA_LOC_LEFT, AVCHROMA_LOC_TOPLEFT, -1,
    };
    static const int out_chroma_locs_left[] = {
        AVCHROMA_LOC_LEFT, -1,
    };
    const ScaleD3D12Context *s = avctx->priv;
    AVFilterFormats *color_spaces, *color_ranges;
    enum AVColorSpace out_matrix;
    int out_matrix_known;
    int ret;

    ret = ff_set_pixel_formats_from_list2(avctx, cfg_in, cfg_out, pix_fmts);
    if (ret < 0)
        return ret;

    out_matrix_known = s->color_matrix != AVCOL_SPC_UNSPECIFIED ||
                      s->color_primaries != AVCOL_PRI_UNSPECIFIED;
    if (s->color_matrix != AVCOL_SPC_UNSPECIFIED)
        out_matrix = s->color_matrix == AVCOL_SPC_BT2020_CL ? AVCOL_SPC_BT2020_NCL : s->color_matrix;
    else if (s->color_primaries != AVCOL_PRI_UNSPECIFIED)
        out_matrix = d3d12_scale_matrix_from_primaries(s->color_primaries);
    else
        out_matrix = AVCOL_SPC_UNSPECIFIED;

    color_spaces = ff_all_color_spaces();
    if ((ret = ff_formats_ref(color_spaces, &cfg_in[0]->color_spaces)) < 0)
        return ret;
    if (out_matrix_known)
        color_spaces = ff_make_formats_list_singleton(out_matrix);
    if ((ret = ff_formats_ref(color_spaces, &cfg_out[0]->color_spaces)) < 0)
        return ret;

    color_ranges = ff_all_color_ranges();
    if ((ret = ff_formats_ref(color_ranges, &cfg_in[0]->color_ranges)) < 0)
        return ret;
    if (s->color_range != AVCOL_RANGE_UNSPECIFIED)
        color_ranges = ff_make_formats_list_singleton(s->color_range);
    if ((ret = ff_formats_ref(color_ranges, &cfg_out[0]->color_ranges)) < 0)
        return ret;

    if ((ret = ff_formats_ref(ff_all_chroma_locations(), &cfg_in[0]->chroma_locations)) < 0)
        return ret;
    if (out_matrix_known && out_matrix != AVCOL_SPC_BT2020_NCL) {
        if ((ret = ff_formats_ref(ff_make_format_list(out_chroma_locs_left), &cfg_out[0]->chroma_locations)) < 0)
            return ret;
    } else {
        if ((ret = ff_formats_ref(ff_make_format_list(out_chroma_locs_any), &cfg_out[0]->chroma_locations)) < 0)
            return ret;
    }

    return 0;
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

    /* These color properties match the identically named ones in vf_scale_vaapi. */
    { "out_color_matrix", "Output color matrix coefficient set",
      OFFSET(color_matrix_string), AV_OPT_TYPE_STRING, { .str = NULL }, .flags = FLAGS },
    { "out_range", "Output color range",
      OFFSET(color_range), AV_OPT_TYPE_INT, { .i64 = AVCOL_RANGE_UNSPECIFIED },
      AVCOL_RANGE_UNSPECIFIED, AVCOL_RANGE_JPEG, FLAGS, .unit = "range" },
    { "full",    "Full range",    0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_RANGE_JPEG }, 0, 0, FLAGS, .unit = "range" },
    { "limited", "Limited range", 0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_RANGE_MPEG }, 0, 0, FLAGS, .unit = "range" },
    { "jpeg",    "Full range",    0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_RANGE_JPEG }, 0, 0, FLAGS, .unit = "range" },
    { "mpeg",    "Limited range", 0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_RANGE_MPEG }, 0, 0, FLAGS, .unit = "range" },
    { "tv",      "Limited range", 0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_RANGE_MPEG }, 0, 0, FLAGS, .unit = "range" },
    { "pc",      "Full range",    0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_RANGE_JPEG }, 0, 0, FLAGS, .unit = "range" },
    { "out_color_primaries", "Output color primaries",
      OFFSET(color_primaries_string), AV_OPT_TYPE_STRING, { .str = NULL }, .flags = FLAGS },
    { "out_color_transfer", "Output color transfer characteristics",
      OFFSET(color_transfer_string), AV_OPT_TYPE_STRING, { .str = NULL }, .flags = FLAGS },

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
    FILTER_QUERY_FUNC2(scale_d3d12_query_formats),
    .p.flags          = AVFILTER_FLAG_HWDEVICE,
    .flags_internal   = FF_FILTER_FLAG_HWFRAME_AWARE,
};
