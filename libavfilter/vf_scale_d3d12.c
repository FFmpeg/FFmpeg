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

#include "libavutil/frame.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "compat/w32dlfcn.h"

#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_d3d12va.h"
#include "libavutil/hwcontext_d3d12va_internal.h"

#include "d3d12_vpp.h"
#include "filters.h"
#include "formats.h"
#include "scale_eval.h"
#include "video.h"

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

    ID3D12Device *device;
    D3D12VPPContext vpp;

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

static int scale_d3d12_configure_processor(ScaleD3D12Context *s, AVFilterContext *ctx) {
    HRESULT hr;
    int ret;

    if (s->output_format == DXGI_FORMAT_UNKNOWN) {
        av_log(ctx, AV_LOG_ERROR, "Output format not initialized\n");
        return AVERROR(EINVAL);
    }

    av_log(ctx, AV_LOG_VERBOSE, "Configuring D3D12 video processor: %dx%d -> %dx%d\n",
           s->input_width, s->input_height, s->width, s->height);

    ret = ff_d3d12_vpp_init(ctx, &s->vpp, s->device);
    if (ret < 0)
        return ret;

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
        s->vpp.video_device,
        D3D12_FEATURE_VIDEO_PROCESS_SUPPORT,
        &s->process_support,
        sizeof(s->process_support)
    );

    if (FAILED(hr)) {
        av_log(ctx, AV_LOG_ERROR, "Video process feature not supported: HRESULT 0x%lX\n", hr);
        ff_d3d12_vpp_uninit(ctx, &s->vpp);

        return AVERROR_EXTERNAL;
    }

    if (!(s->process_support.SupportFlags & D3D12_VIDEO_PROCESS_SUPPORT_FLAG_SUPPORTED)) {
        av_log(ctx, AV_LOG_ERROR,
               "D3D12 video processor does not support converting %s (colorspace %d) to %s "
               "(colorspace %d) on this device\n",
               av_get_pix_fmt_name(ff_d3d12_filter_format_from_dxgi(s->input_format) ?
                                   ff_d3d12_filter_format_from_dxgi(s->input_format)->pix_fmt : AV_PIX_FMT_NONE),
               s->input_colorspace,
               av_get_pix_fmt_name(ff_d3d12_filter_format_from_dxgi(s->output_format) ?
                                   ff_d3d12_filter_format_from_dxgi(s->output_format)->pix_fmt : AV_PIX_FMT_NONE),
               s->output_colorspace);
        ff_d3d12_vpp_report_output_support(ctx, &s->vpp, &s->process_support);
        ff_d3d12_vpp_uninit(ctx, &s->vpp);

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

    ret = ff_d3d12_vpp_create_processor(ctx, &s->vpp, &processor_output_desc, &processor_input_desc);
    if (ret < 0)
        return ret;

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
    int frames_in_use;

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

    if (!s->vpp.configured) {
        AVHWFramesContext *input_frames_ctx = (AVHWFramesContext *)in->hw_frames_ctx->data;
        const D3D12FilterFormat *in_map, *out_map;
        enum AVColorSpace out_spc;
        enum AVColorRange out_range;
        enum AVColorPrimaries out_primaries;
        enum AVColorTransferCharacteristic out_trc;

        s->input_width  = in->width;
        s->input_height = in->height;

        AVD3D12VAFramesContext *input_hwctx = (AVD3D12VAFramesContext *)input_frames_ctx->hwctx;
        s->input_format = input_hwctx->format;

        if (s->input_format == DXGI_FORMAT_UNKNOWN) {
            in_map = ff_d3d12_filter_format_from_pix_fmt(input_frames_ctx->sw_format);
            if (!in_map) {
                av_log(ctx, AV_LOG_ERROR, "Unsupported input format: %s\n",
                       av_get_pix_fmt_name(input_frames_ctx->sw_format));
                ret = AVERROR(EINVAL);
                goto fail;
            }
            s->input_format = in_map->dxgi_format;
        }

        in_map = ff_d3d12_filter_format_from_dxgi(s->input_format);
        s->input_is_rgb = in_map ? in_map->is_rgb : 0;
        const AVPixFmtDescriptor *in_desc = in_map ? av_pix_fmt_desc_get(in_map->pix_fmt) : NULL;
        s->input_has_alpha = in_desc && (in_desc->flags & AV_PIX_FMT_FLAG_ALPHA);

        enum AVColorRange in_range = in->color_range != AVCOL_RANGE_UNSPECIFIED ? in->color_range
                                     : (s->input_is_rgb ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG);
        enum AVColorSpace in_spc = in->colorspace;
        enum AVColorPrimaries in_primaries = in->color_primaries;
        enum AVColorTransferCharacteristic in_trc = in->color_trc;

        ret = ff_d3d12_filter_get_colorspace(ctx, s->input_is_rgb, 0, &in_spc, &in_range,
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
        out_map = ff_d3d12_filter_format_from_dxgi(s->output_format);
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

        ret = ff_d3d12_filter_get_colorspace(ctx, s->output_is_rgb, 1, &out_spc, &out_range,
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
            enum AVChromaLocation out_chroma_loc = ff_d3d12_filter_chroma_location(s->output_colorspace);
            if (outlink->chroma_location != AVCHROMA_LOC_UNSPECIFIED &&
                outlink->chroma_location != out_chroma_loc) {
                av_log(ctx, AV_LOG_ERROR,
                       "Negotiated output chroma siting does not match the "
                       "siting produced from this input\n");
                ret = AVERROR(EINVAL);
                goto fail;
            }
        }

        s->input_framerate = ff_d3d12_filter_get_input_framerate(ctx, inlink, in);

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

    ret = ff_d3d12_vpp_begin(ctx, &s->vpp);
    if (ret < 0)
        goto fail;

    D3D12_RESOURCE_BARRIER barriers[4];
    int nb_barriers = 0;

    ff_d3d12_filter_add_frame_barriers(barriers, &nb_barriers, input_frame, s->input_is_rgb ? 1 : 2,
                                       D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_VIDEO_PROCESS_READ);
    ff_d3d12_filter_add_frame_barriers(barriers, &nb_barriers, output_frame, s->output_is_rgb ? 1 : 2,
                                       D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_VIDEO_PROCESS_WRITE);

    ID3D12VideoProcessCommandList_ResourceBarrier(s->vpp.cmd.list.video_process, nb_barriers, barriers);

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
        s->vpp.cmd.list.video_process,
        s->vpp.video_processor,
        &output_args,
        1,
        &input_args
    );

    for (int i = 0; i < nb_barriers; i++) {
        FFSWAP(D3D12_RESOURCE_STATES, barriers[i].Transition.StateBefore, barriers[i].Transition.StateAfter);
    }
    ID3D12VideoProcessCommandList_ResourceBarrier(s->vpp.cmd.list.video_process, nb_barriers, barriers);

    ret = ff_d3d12_vpp_submit(ctx, &s->vpp, &in, 1, out, &frames_in_use);
    if (ret < 0) {
        if (frames_in_use)
            return ret;
        goto fail;
    }

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
        out->chroma_location = ff_d3d12_filter_chroma_location(s->output_colorspace);
    }

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

    if (ff_d3d12_vpp_uninit(ctx, &s->vpp) < 0) {
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

    AVHWFramesContext *in_frames_ctx = (AVHWFramesContext *)inl->hw_frames_ctx->data;

    enum AVPixelFormat resolved_format = s->format;
    if (resolved_format == AV_PIX_FMT_NONE) {
        /* If format is not specified, use the same format as input */
        resolved_format = in_frames_ctx->sw_format;
        av_log(ctx, AV_LOG_VERBOSE, "D3D12 scale output format not specified, using input format: %s\n",
               av_get_pix_fmt_name(resolved_format));
    }

    /* Set output format based on sw_format */
    const D3D12FilterFormat *out_map = ff_d3d12_filter_format_from_pix_fmt(resolved_format);
    if (!out_map) {
        av_log(ctx, AV_LOG_ERROR, "Unsupported output format: %s\n",
               av_get_pix_fmt_name(resolved_format));

        return AVERROR(EINVAL);
    }
    s->output_format = out_map->dxgi_format;
    s->output_is_rgb = out_map->is_rgb;

    const D3D12FilterFormat *in_map = ff_d3d12_filter_format_from_pix_fmt(in_frames_ctx->sw_format);
    int input_is_rgb = in_map ? in_map->is_rgb : 0;

    if (s->color_primaries != AVCOL_PRI_UNSPECIFIED ||
        s->color_transfer  != AVCOL_TRC_UNSPECIFIED ||
        s->color_matrix    != AVCOL_SPC_UNSPECIFIED ||
        input_is_rgb       != s->output_is_rgb ||
        resolved_format == AV_PIX_FMT_RGBAF16 ||
        (input_is_rgb && s->color_range != AVCOL_RANGE_UNSPECIFIED))
        av_frame_side_data_remove_by_props(&outlink->side_data, &outlink->nb_side_data,
                                           AV_SIDE_DATA_PROP_COLOR_DEPENDENT);

    ret = ff_d3d12_filter_init_output_frames(ctx, s->hw_device_ctx, resolved_format,
                                             s->width, s->height, 4, &s->hw_frames_ctx_out);
    if (ret < 0)
        return ret;

    outl->hw_frames_ctx = av_buffer_ref(s->hw_frames_ctx_out);
    if (!outl->hw_frames_ctx)
        return AVERROR(ENOMEM);

    av_log(ctx, AV_LOG_VERBOSE, "D3D12 scale config: %dx%d -> %dx%d\n",
           inlink->w, inlink->h, outlink->w, outlink->h);

    return 0;
}

static av_cold void scale_d3d12_uninit(AVFilterContext *ctx) {
    ScaleD3D12Context *s = ctx->priv;

    if (ff_d3d12_vpp_uninit(ctx, &s->vpp) >= 0) {
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
