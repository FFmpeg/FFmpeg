/*
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

#include <string.h>

#include "libavutil/avassert.h"
#include "libavutil/mem.h"
#include "libavutil/pixdesc.h"

#include "filters.h"
#include "formats.h"
#include "vaapi_vpp.h"

int ff_vaapi_vpp_query_formats(AVFilterContext *avctx)
{
    enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_VAAPI, AV_PIX_FMT_NONE,
    };
    int err;

    if ((err = ff_formats_ref(ff_make_format_list(pix_fmts),
                              &avctx->inputs[0]->outcfg.formats)) < 0)
        return err;
    if ((err = ff_formats_ref(ff_make_format_list(pix_fmts),
                              &avctx->outputs[0]->incfg.formats)) < 0)
        return err;

    if ((err = ff_set_common_all_color_spaces(avctx)) < 0 ||
        (err = ff_set_common_all_color_ranges(avctx)) < 0)
        return err;

    return 0;
}

void ff_vaapi_vpp_pipeline_uninit(AVFilterContext *avctx)
{
    VAAPIVPPContext *ctx   = avctx->priv;
    int i;
    for (i = 0; i < ctx->nb_filter_buffers; i++) {
        if (ctx->filter_buffers[i] != VA_INVALID_ID) {
            vaDestroyBuffer(ctx->hwctx->display, ctx->filter_buffers[i]);
            ctx->filter_buffers[i] = VA_INVALID_ID;
        }
    }
    ctx->nb_filter_buffers = 0;

    if (ctx->va_context != VA_INVALID_ID) {
        vaDestroyContext(ctx->hwctx->display, ctx->va_context);
        ctx->va_context = VA_INVALID_ID;
    }

    if (ctx->va_config != VA_INVALID_ID) {
        vaDestroyConfig(ctx->hwctx->display, ctx->va_config);
        ctx->va_config = VA_INVALID_ID;
    }

    av_buffer_unref(&ctx->device_ref);
    ctx->hwctx = NULL;
}

int ff_vaapi_vpp_config_input(AVFilterLink *inlink)
{
    FilterLink          *l = ff_filter_link(inlink);
    AVFilterContext *avctx = inlink->dst;
    VAAPIVPPContext *ctx   = avctx->priv;

    if (ctx->pipeline_uninit)
        ctx->pipeline_uninit(avctx);

    if (!l->hw_frames_ctx) {
        av_log(avctx, AV_LOG_ERROR, "A hardware frames reference is "
               "required to associate the processing device.\n");
        return AVERROR(EINVAL);
    }

    ctx->input_frames_ref = av_buffer_ref(l->hw_frames_ctx);
    if (!ctx->input_frames_ref) {
        av_log(avctx, AV_LOG_ERROR, "A input frames reference create "
               "failed.\n");
        return AVERROR(ENOMEM);
    }
    ctx->input_frames = (AVHWFramesContext*)ctx->input_frames_ref->data;

    return 0;
}

int ff_vaapi_vpp_config_output(AVFilterLink *outlink)
{
    FilterLink       *outl = ff_filter_link(outlink);
    AVFilterContext *avctx = outlink->src;
    AVFilterLink   *inlink = avctx->inputs[0];
    FilterLink        *inl = ff_filter_link(inlink);
    VAAPIVPPContext *ctx   = avctx->priv;
    AVVAAPIHWConfig *hwconfig = NULL;
    AVHWFramesConstraints *constraints = NULL;
    AVHWFramesContext *output_frames;
    AVVAAPIFramesContext *va_frames;
    VAStatus vas;
    int err, i;

    if (ctx->pipeline_uninit)
        ctx->pipeline_uninit(avctx);

    if (!ctx->output_width)
        ctx->output_width  = avctx->inputs[0]->w;
    if (!ctx->output_height)
        ctx->output_height = avctx->inputs[0]->h;

    outlink->w = ctx->output_width;
    outlink->h = ctx->output_height;

    if (ctx->passthrough) {
        if (inl->hw_frames_ctx)
            outl->hw_frames_ctx = av_buffer_ref(inl->hw_frames_ctx);
        av_log(ctx, AV_LOG_VERBOSE, "Using VAAPI filter passthrough mode.\n");

        return 0;
    }

    av_assert0(ctx->input_frames);
    ctx->device_ref = av_buffer_ref(ctx->input_frames->device_ref);
    if (!ctx->device_ref) {
        av_log(avctx, AV_LOG_ERROR, "A device reference create "
               "failed.\n");
        return AVERROR(ENOMEM);
    }
    ctx->hwctx = ((AVHWDeviceContext*)ctx->device_ref->data)->hwctx;

    av_assert0(ctx->va_config == VA_INVALID_ID);
    vas = vaCreateConfig(ctx->hwctx->display, VAProfileNone,
                         VAEntrypointVideoProc, NULL, 0, &ctx->va_config);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create processing pipeline "
               "config: %d (%s).\n", vas, vaErrorStr(vas));
        err = AVERROR(EIO);
        goto fail;
    }

    hwconfig = av_hwdevice_hwconfig_alloc(ctx->device_ref);
    if (!hwconfig) {
        err = AVERROR(ENOMEM);
        goto fail;
    }
    hwconfig->config_id = ctx->va_config;

    constraints = av_hwdevice_get_hwframe_constraints(ctx->device_ref,
                                                      hwconfig);
    if (!constraints) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    if (ctx->output_format == AV_PIX_FMT_NONE)
        ctx->output_format = ctx->input_frames->sw_format;
    if (constraints->valid_sw_formats) {
        for (i = 0; constraints->valid_sw_formats[i] != AV_PIX_FMT_NONE; i++) {
            if (ctx->output_format == constraints->valid_sw_formats[i])
                break;
        }
        if (constraints->valid_sw_formats[i] == AV_PIX_FMT_NONE) {
            av_log(avctx, AV_LOG_ERROR, "Hardware does not support output "
                   "format %s.\n", av_get_pix_fmt_name(ctx->output_format));
            err = AVERROR(EINVAL);
            goto fail;
        }
    }

    if (ctx->output_width  < constraints->min_width  ||
        ctx->output_height < constraints->min_height ||
        ctx->output_width  > constraints->max_width  ||
        ctx->output_height > constraints->max_height) {
        av_log(avctx, AV_LOG_ERROR, "Hardware does not support scaling to "
               "size %dx%d (constraints: width %d-%d height %d-%d).\n",
               ctx->output_width, ctx->output_height,
               constraints->min_width,  constraints->max_width,
               constraints->min_height, constraints->max_height);
        err = AVERROR(EINVAL);
        goto fail;
    }

    outl->hw_frames_ctx = av_hwframe_ctx_alloc(ctx->device_ref);
    if (!outl->hw_frames_ctx) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create HW frame context "
               "for output.\n");
        err = AVERROR(ENOMEM);
        goto fail;
    }

    output_frames = (AVHWFramesContext*)outl->hw_frames_ctx->data;

    output_frames->format    = AV_PIX_FMT_VAAPI;
    output_frames->sw_format = ctx->output_format;
    output_frames->width     = ctx->output_width;
    output_frames->height    = ctx->output_height;

    if (CONFIG_VAAPI_1)
        output_frames->initial_pool_size = 0;
    else
        output_frames->initial_pool_size = 4;

    err = ff_filter_init_hw_frames(avctx, outlink, 10);
    if (err < 0)
        goto fail;

    err = av_hwframe_ctx_init(outl->hw_frames_ctx);
    if (err < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to initialise VAAPI frame "
               "context for output: %d\n", err);
        goto fail;
    }

    va_frames = output_frames->hwctx;

    av_assert0(ctx->va_context == VA_INVALID_ID);
    av_assert0(output_frames->initial_pool_size ||
               (va_frames->surface_ids == NULL && va_frames->nb_surfaces == 0));
    vas = vaCreateContext(ctx->hwctx->display, ctx->va_config,
                          ctx->output_width, ctx->output_height,
                          VA_PROGRESSIVE,
                          va_frames->surface_ids, va_frames->nb_surfaces,
                          &ctx->va_context);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create processing pipeline "
               "context: %d (%s).\n", vas, vaErrorStr(vas));
        return AVERROR(EIO);
    }

    if (ctx->build_filter_params) {
        err = ctx->build_filter_params(avctx);
        if (err < 0)
            goto fail;
    }

    av_freep(&hwconfig);
    av_hwframe_constraints_free(&constraints);
    return 0;

fail:
    av_buffer_unref(&outl->hw_frames_ctx);
    av_freep(&hwconfig);
    av_hwframe_constraints_free(&constraints);
    return err;
}

typedef struct VAAPIColourProperties {
    VAProcColorStandardType va_color_standard;

    enum AVColorPrimaries color_primaries;
    enum AVColorTransferCharacteristic color_trc;
    enum AVColorSpace colorspace;

    uint8_t va_chroma_sample_location;
    uint8_t va_color_range;

    enum AVColorRange color_range;
    enum AVChromaLocation chroma_sample_location;
} VAAPIColourProperties;

static const VAAPIColourProperties vaapi_colour_standard_map[] = {
    { VAProcColorStandardBT601,       5,  6,  5 },
    { VAProcColorStandardBT601,       6,  6,  6 },
    { VAProcColorStandardBT709,       1,  1,  1 },
    { VAProcColorStandardBT470M,      4,  4,  4 },
    { VAProcColorStandardBT470BG,     5,  5,  5 },
    { VAProcColorStandardSMPTE170M,   6,  6,  6 },
    { VAProcColorStandardSMPTE240M,   7,  7,  7 },
    { VAProcColorStandardGenericFilm, 8,  1,  1 },
#if VA_CHECK_VERSION(1, 1, 0)
    { VAProcColorStandardSRGB,        1, 13,  0 },
    { VAProcColorStandardXVYCC601,    1, 11,  5 },
    { VAProcColorStandardXVYCC709,    1, 11,  1 },
    { VAProcColorStandardBT2020,      9, 14,  9 },
#endif
};

static void vaapi_vpp_fill_colour_standard(VAAPIColourProperties *props,
                                           VAProcColorStandardType *vacs,
                                           int nb_vacs)
{
    const VAAPIColourProperties *t;
    int i, j, score, best_score, worst_score;
    VAProcColorStandardType best_standard;

#if VA_CHECK_VERSION(1, 3, 0)
    // If the driver supports explicit use of the standard values then just
    // use them and avoid doing any mapping.  (The driver may not support
    // some particular code point, but it still has enough information to
    // make a better fallback choice than we do in that case.)
    for (i = 0; i < nb_vacs; i++) {
        if (vacs[i] == VAProcColorStandardExplicit) {
            props->va_color_standard = VAProcColorStandardExplicit;
            return;
        }
    }
#endif

    // Give scores to the possible options and choose the lowest one.
    // An exact match will score zero and therefore always be chosen, as
    // will a partial match where all unmatched elements are explicitly
    // unspecified.  If no options match at all then just pass "none" to
    // the driver and let it make its own choice.
    best_standard = VAProcColorStandardNone;
    best_score = -1;
    worst_score = 4 * (props->colorspace != AVCOL_SPC_UNSPECIFIED &&
                       props->colorspace != AVCOL_SPC_RGB) +
                  2 * (props->color_trc != AVCOL_TRC_UNSPECIFIED) +
                      (props->color_primaries != AVCOL_PRI_UNSPECIFIED);

    if (worst_score == 0) {
        // No properties are specified, so we aren't going to be able to
        // make a useful choice.
        props->va_color_standard = VAProcColorStandardNone;
        return;
    }

    for (i = 0; i < nb_vacs; i++) {
        for (j = 0; j < FF_ARRAY_ELEMS(vaapi_colour_standard_map); j++) {
            t = &vaapi_colour_standard_map[j];
            if (t->va_color_standard != vacs[i])
                continue;

            score = 0;
            if (props->colorspace != AVCOL_SPC_UNSPECIFIED &&
                props->colorspace != AVCOL_SPC_RGB)
                score += 4 * (props->colorspace != t->colorspace);
            if (props->color_trc != AVCOL_TRC_UNSPECIFIED)
                score += 2 * (props->color_trc != t->color_trc);
            if (props->color_primaries != AVCOL_PRI_UNSPECIFIED)
                score += (props->color_primaries != t->color_primaries);

            // Only include choices which matched something.
            if (score < worst_score &&
                (best_score == -1 || score < best_score)) {
                best_score    = score;
                best_standard = t->va_color_standard;
            }
        }
    }
    props->va_color_standard = best_standard;
}

static void vaapi_vpp_fill_chroma_sample_location(VAAPIColourProperties *props)
{
#if VA_CHECK_VERSION(1, 1, 0)
    static const struct {
        enum AVChromaLocation av;
        uint8_t va;
    } csl_map[] = {
        { AVCHROMA_LOC_UNSPECIFIED, VA_CHROMA_SITING_UNKNOWN },
        { AVCHROMA_LOC_LEFT,        VA_CHROMA_SITING_VERTICAL_CENTER |
                                    VA_CHROMA_SITING_HORIZONTAL_LEFT },
        { AVCHROMA_LOC_CENTER,      VA_CHROMA_SITING_VERTICAL_CENTER |
                                    VA_CHROMA_SITING_HORIZONTAL_CENTER },
        { AVCHROMA_LOC_TOPLEFT,     VA_CHROMA_SITING_VERTICAL_TOP |
                                    VA_CHROMA_SITING_HORIZONTAL_LEFT },
        { AVCHROMA_LOC_TOP,         VA_CHROMA_SITING_VERTICAL_TOP |
                                    VA_CHROMA_SITING_HORIZONTAL_CENTER },
        { AVCHROMA_LOC_BOTTOMLEFT,  VA_CHROMA_SITING_VERTICAL_BOTTOM |
                                    VA_CHROMA_SITING_HORIZONTAL_LEFT },
        { AVCHROMA_LOC_BOTTOM,      VA_CHROMA_SITING_VERTICAL_BOTTOM |
                                    VA_CHROMA_SITING_HORIZONTAL_CENTER },
    };
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(csl_map); i++) {
        if (props->chroma_sample_location == csl_map[i].av) {
            props->va_chroma_sample_location = csl_map[i].va;
            return;
        }
    }
    props->va_chroma_sample_location = VA_CHROMA_SITING_UNKNOWN;
#else
    props->va_chroma_sample_location = 0;
#endif
}

static void vaapi_vpp_fill_colour_range(VAAPIColourProperties *props)
{
#if VA_CHECK_VERSION(1, 1, 0)
    switch (props->color_range) {
    case AVCOL_RANGE_MPEG:
        props->va_color_range = VA_SOURCE_RANGE_REDUCED;
        break;
    case AVCOL_RANGE_JPEG:
        props->va_color_range = VA_SOURCE_RANGE_FULL;
        break;
    case AVCOL_RANGE_UNSPECIFIED:
    default:
        props->va_color_range = VA_SOURCE_RANGE_UNKNOWN;
    }
#else
    props->va_color_range = 0;
#endif
}

static void vaapi_vpp_fill_colour_properties(AVFilterContext *avctx,
                                             VAAPIColourProperties *props,
                                             VAProcColorStandardType *vacs,
                                             int nb_vacs)
{
    vaapi_vpp_fill_colour_standard(props, vacs, nb_vacs);
    vaapi_vpp_fill_chroma_sample_location(props);
    vaapi_vpp_fill_colour_range(props);

    av_log(avctx, AV_LOG_DEBUG, "Mapped colour properties %s %s/%s/%s %s "
           "to VA standard %d chroma siting %#x range %#x.\n",
           av_color_range_name(props->color_range),
           av_color_space_name(props->colorspace),
           av_color_primaries_name(props->color_primaries),
           av_color_transfer_name(props->color_trc),
           av_chroma_location_name(props->chroma_sample_location),
           props->va_color_standard,
           props->va_chroma_sample_location, props->va_color_range);
}

static int vaapi_vpp_frame_is_rgb(const AVFrame *frame)
{
    const AVHWFramesContext *hwfc;
    const AVPixFmtDescriptor *desc;
    av_assert0(frame->format == AV_PIX_FMT_VAAPI &&
               frame->hw_frames_ctx);
    hwfc = (const AVHWFramesContext*)frame->hw_frames_ctx->data;
    desc = av_pix_fmt_desc_get(hwfc->sw_format);
    av_assert0(desc);
    return !!(desc->flags & AV_PIX_FMT_FLAG_RGB);
}

static int vaapi_vpp_colour_properties(AVFilterContext *avctx,
                                       VAProcPipelineParameterBuffer *params,
                                       const AVFrame *input_frame,
                                       AVFrame *output_frame)
{
    VAAPIVPPContext *ctx = avctx->priv;
    VAAPIColourProperties input_props, output_props;
    VAProcPipelineCaps caps;
    VAStatus vas;

    vas = vaQueryVideoProcPipelineCaps(ctx->hwctx->display, ctx->va_context,
                                       ctx->filter_buffers, ctx->nb_filter_buffers,
                                       &caps);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to query capabilities for "
               "colour standard support: %d (%s).\n", vas, vaErrorStr(vas));
        return AVERROR_EXTERNAL;
    }

    input_props = (VAAPIColourProperties) {
        .colorspace = vaapi_vpp_frame_is_rgb(input_frame)
                ? AVCOL_SPC_RGB : input_frame->colorspace,
        .color_primaries        = input_frame->color_primaries,
        .color_trc              = input_frame->color_trc,
        .color_range            = input_frame->color_range,
        .chroma_sample_location = input_frame->chroma_location,
    };

    vaapi_vpp_fill_colour_properties(avctx, &input_props,
                                     caps.input_color_standards,
                                     caps.num_input_color_standards);

    output_props = (VAAPIColourProperties) {
        .colorspace = vaapi_vpp_frame_is_rgb(output_frame)
                ? AVCOL_SPC_RGB : output_frame->colorspace,
        .color_primaries        = output_frame->color_primaries,
        .color_trc              = output_frame->color_trc,
        .color_range            = output_frame->color_range,
        .chroma_sample_location = output_frame->chroma_location,
    };
    vaapi_vpp_fill_colour_properties(avctx, &output_props,
                                     caps.output_color_standards,
                                     caps.num_output_color_standards);

    // If the properties weren't filled completely in the output frame and
    // we chose a fixed standard then fill the known values in here.
#if VA_CHECK_VERSION(1, 3, 0)
    if (output_props.va_color_standard != VAProcColorStandardExplicit)
#endif
    {
        const VAAPIColourProperties *output_standard = NULL;
        int i;

        for (i = 0; i < FF_ARRAY_ELEMS(vaapi_colour_standard_map); i++) {
            if (output_props.va_color_standard ==
                vaapi_colour_standard_map[i].va_color_standard) {
                output_standard = &vaapi_colour_standard_map[i];
                break;
            }
        }
        if (output_standard) {
            output_frame->colorspace = vaapi_vpp_frame_is_rgb(output_frame)
                          ? AVCOL_SPC_RGB : output_standard->colorspace;
            output_frame->color_primaries = output_standard->color_primaries;
            output_frame->color_trc       = output_standard->color_trc;
        }
    }

    params->surface_color_standard = input_props.va_color_standard;
    params->output_color_standard = output_props.va_color_standard;

#if VA_CHECK_VERSION(1, 1, 0)
    params->input_color_properties = (VAProcColorProperties) {
        .chroma_sample_location   = input_props.va_chroma_sample_location,
        .color_range              = input_props.va_color_range,
#if VA_CHECK_VERSION(1, 3, 0)
        .colour_primaries         = input_props.color_primaries,
        .transfer_characteristics = input_props.color_trc,
        .matrix_coefficients      = input_props.colorspace,
#endif
    };
    params->output_color_properties = (VAProcColorProperties) {
        .chroma_sample_location   = output_props.va_chroma_sample_location,
        .color_range              = output_props.va_color_range,
#if VA_CHECK_VERSION(1, 3, 0)
        .colour_primaries         = output_props.color_primaries,
        .transfer_characteristics = output_props.color_trc,
        .matrix_coefficients      = output_props.colorspace,
#endif
    };
#endif

    return 0;
}

int ff_vaapi_vpp_init_params(AVFilterContext *avctx,
                             VAProcPipelineParameterBuffer *params,
                             const AVFrame *input_frame,
                             AVFrame *output_frame)
{
    VAAPIVPPContext *ctx = avctx->priv;
    int err;

    ctx->input_region = (VARectangle) {
        .x      = input_frame->crop_left,
        .y      = input_frame->crop_top,
        .width  = input_frame->width -
                 (input_frame->crop_left + input_frame->crop_right),
        .height = input_frame->height -
                 (input_frame->crop_top + input_frame->crop_bottom),
    };
    output_frame->crop_top    = 0;
    output_frame->crop_bottom = 0;
    output_frame->crop_left   = 0;
    output_frame->crop_right  = 0;

    *params = (VAProcPipelineParameterBuffer) {
        .surface                 = ff_vaapi_vpp_get_surface_id(input_frame),
        .surface_region          = &ctx->input_region,
        .output_region           = NULL,
        .output_background_color = VAAPI_VPP_BACKGROUND_BLACK,
        .pipeline_flags          = 0,
        .filter_flags            = VA_FRAME_PICTURE,

        // Filter and reference data filled by the filter itself.

#if VA_CHECK_VERSION(1, 1, 0)
        .rotation_state = VA_ROTATION_NONE,
        .mirror_state   = VA_MIRROR_NONE,
#endif
    };

    err = vaapi_vpp_colour_properties(avctx, params,
                                      input_frame, output_frame);
    if (err < 0)
        return err;

    av_log(avctx, AV_LOG_DEBUG, "Filter frame from surface %#x to %#x.\n",
           ff_vaapi_vpp_get_surface_id(input_frame),
           ff_vaapi_vpp_get_surface_id(output_frame));

    return 0;
}

int ff_vaapi_vpp_make_param_buffers(AVFilterContext *avctx,
                                    int type,
                                    const void *data,
                                    size_t size,
                                    int count)
{
    VAStatus vas;
    VABufferID buffer;
    VAAPIVPPContext *ctx   = avctx->priv;

    av_assert0(ctx->nb_filter_buffers + 1 <= VAProcFilterCount);

    vas = vaCreateBuffer(ctx->hwctx->display, ctx->va_context,
                         type, size, count, (void*)data, &buffer);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create parameter "
               "buffer (type %d): %d (%s).\n",
               type, vas, vaErrorStr(vas));
        return AVERROR(EIO);
    }

    ctx->filter_buffers[ctx->nb_filter_buffers++] = buffer;

    av_log(avctx, AV_LOG_DEBUG, "Param buffer (type %d, %zu bytes, count %d) "
           "is %#x.\n", type, size, count, buffer);
    return 0;
}

static int vaapi_vpp_render_single_pipeline_buffer(AVFilterContext *avctx,
                                                   VAProcPipelineParameterBuffer *params,
                                                   VABufferID *params_id)
{
    VAAPIVPPContext *ctx = avctx->priv;
    VAStatus vas;

    vas = vaCreateBuffer(ctx->hwctx->display, ctx->va_context,
                         VAProcPipelineParameterBufferType,
                         sizeof(*params), 1, params, params_id);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create parameter buffer: "
               "%d (%s).\n", vas, vaErrorStr(vas));
        *params_id = VA_INVALID_ID;

        return AVERROR(EIO);
    }
    av_log(avctx, AV_LOG_DEBUG, "Pipeline parameter buffer is %#x.\n", *params_id);

    vas = vaRenderPicture(ctx->hwctx->display, ctx->va_context, params_id, 1);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to render parameter buffer: "
               "%d (%s).\n", vas, vaErrorStr(vas));
        return AVERROR(EIO);
    }

    return 0;
}

int ff_vaapi_vpp_render_pictures(AVFilterContext *avctx,
                                 VAProcPipelineParameterBuffer *params_list,
                                 int cout,
                                 AVFrame *output_frame)
{
    VAAPIVPPContext *ctx = avctx->priv;
    VABufferID *params_ids;
    VAStatus vas;
    int err;

    params_ids = (VABufferID *)av_malloc_array(cout, sizeof(VABufferID));
    if (!params_ids)
        return AVERROR(ENOMEM);

    for (int i = 0; i < cout; i++)
        params_ids[i] = VA_INVALID_ID;

    vas = vaBeginPicture(ctx->hwctx->display,
                         ctx->va_context, ff_vaapi_vpp_get_surface_id(output_frame));
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to attach new picture: "
               "%d (%s).\n", vas, vaErrorStr(vas));
        err = AVERROR(EIO);
        goto fail;
    }

    for (int i = 0; i < cout; i++) {
        err = vaapi_vpp_render_single_pipeline_buffer(avctx, &params_list[i], &params_ids[i]);
        if (err)
            goto fail_after_begin;
    }

    vas = vaEndPicture(ctx->hwctx->display, ctx->va_context);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to start picture processing: "
               "%d (%s).\n", vas, vaErrorStr(vas));
        err = AVERROR(EIO);
        goto fail_after_render;
    }

    if (CONFIG_VAAPI_1 || ctx->hwctx->driver_quirks &
        AV_VAAPI_DRIVER_QUIRK_RENDER_PARAM_BUFFERS) {
        for (int i = 0; i < cout && params_ids[i] != VA_INVALID_ID; i++) {
            vas = vaDestroyBuffer(ctx->hwctx->display, params_ids[i]);
            if (vas != VA_STATUS_SUCCESS) {
                av_log(avctx, AV_LOG_ERROR, "Failed to free parameter buffer: "
                       "%d (%s).\n", vas, vaErrorStr(vas));
                // And ignore.
            }
        }
    }

    av_freep(&params_ids);
    return 0;

    // We want to make sure that if vaBeginPicture has been called, we also
    // call vaRenderPicture and vaEndPicture.  These calls may well fail or
    // do something else nasty, but once we're in this failure case there
    // isn't much else we can do.
fail_after_begin:
    vaRenderPicture(ctx->hwctx->display, ctx->va_context, &params_ids[0], 1);
fail_after_render:
    vaEndPicture(ctx->hwctx->display, ctx->va_context);
fail:
    av_freep(&params_ids);
    return err;
}

int ff_vaapi_vpp_render_picture(AVFilterContext *avctx,
                                VAProcPipelineParameterBuffer *params,
                                AVFrame *output_frame)
{
    return ff_vaapi_vpp_render_pictures(avctx, params, 1, output_frame);
}

void ff_vaapi_vpp_ctx_init(AVFilterContext *avctx)
{
    int i;
    VAAPIVPPContext *ctx   = avctx->priv;

    ctx->va_config  = VA_INVALID_ID;
    ctx->va_context = VA_INVALID_ID;
    ctx->valid_ids  = 1;

    for (i = 0; i < VAProcFilterCount; i++)
        ctx->filter_buffers[i] = VA_INVALID_ID;
    ctx->nb_filter_buffers = 0;
}

void ff_vaapi_vpp_ctx_uninit(AVFilterContext *avctx)
{
    VAAPIVPPContext *ctx   = avctx->priv;
    if (ctx->valid_ids && ctx->pipeline_uninit)
        ctx->pipeline_uninit(avctx);

    av_buffer_unref(&ctx->input_frames_ref);
    av_buffer_unref(&ctx->device_ref);
}
