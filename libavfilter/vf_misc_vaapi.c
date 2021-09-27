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

#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "vaapi_vpp.h"

// Denoise min/max/default Values
#define DENOISE_MIN            0
#define DENOISE_MAX            64
#define DENOISE_DEFAULT        0

// Sharpness min/max/default values
#define SHARPNESS_MIN          0
#define SHARPNESS_MAX          64
#define SHARPNESS_DEFAULT      44

typedef struct DenoiseVAAPIContext {
    VAAPIVPPContext vpp_ctx; // must be the first field

    int denoise;         // enable denoise algo.
} DenoiseVAAPIContext;

typedef struct SharpnessVAAPIContext {
    VAAPIVPPContext vpp_ctx; // must be the first field

    int sharpness;       // enable sharpness.
} SharpnessVAAPIContext;

static float map(int x, int in_min, int in_max, float out_min, float out_max)
{
    double slope, output;

    slope = 1.0 * (out_max - out_min) / (in_max - in_min);
    output = out_min + slope * (x - in_min);

    return (float)output;
}

static int denoise_vaapi_build_filter_params(AVFilterContext *avctx)
{
    VAAPIVPPContext *vpp_ctx = avctx->priv;
    DenoiseVAAPIContext *ctx = avctx->priv;

    VAProcFilterCap caps;

    VAStatus vas;
    uint32_t num_caps = 1;

    VAProcFilterParameterBuffer denoise;

    vas = vaQueryVideoProcFilterCaps(vpp_ctx->hwctx->display, vpp_ctx->va_context,
                                     VAProcFilterNoiseReduction,
                                     &caps, &num_caps);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to query denoise caps "
               "context: %d (%s).\n", vas, vaErrorStr(vas));
        return AVERROR(EIO);
    }

    denoise.type  = VAProcFilterNoiseReduction;
    denoise.value =  map(ctx->denoise, DENOISE_MIN, DENOISE_MAX,
                         caps.range.min_value,
                         caps.range.max_value);
    return ff_vaapi_vpp_make_param_buffers(avctx,
                                           VAProcFilterParameterBufferType,
                                           &denoise, sizeof(denoise), 1);
}

static int sharpness_vaapi_build_filter_params(AVFilterContext *avctx)
{
    VAAPIVPPContext *vpp_ctx   = avctx->priv;
    SharpnessVAAPIContext *ctx = avctx->priv;

    VAProcFilterCap caps;

    VAStatus vas;
    uint32_t num_caps = 1;

    VAProcFilterParameterBuffer sharpness;

    vas = vaQueryVideoProcFilterCaps(vpp_ctx->hwctx->display, vpp_ctx->va_context,
                                     VAProcFilterSharpening,
                                     &caps, &num_caps);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to query sharpness caps "
               "context: %d (%s).\n", vas, vaErrorStr(vas));
        return AVERROR(EIO);
    }

    sharpness.type  = VAProcFilterSharpening;
    sharpness.value = map(ctx->sharpness,
                          SHARPNESS_MIN, SHARPNESS_MAX,
                          caps.range.min_value,
                          caps.range.max_value);
    return ff_vaapi_vpp_make_param_buffers(avctx,
                                           VAProcFilterParameterBufferType,
                                           &sharpness, sizeof(sharpness), 1);
}

static int misc_vaapi_filter_frame(AVFilterLink *inlink, AVFrame *input_frame)
{
    AVFilterContext *avctx   = inlink->dst;
    AVFilterLink *outlink    = avctx->outputs[0];
    VAAPIVPPContext *vpp_ctx = avctx->priv;
    AVFrame *output_frame    = NULL;
    VAProcPipelineParameterBuffer params;
    int err;

    av_log(avctx, AV_LOG_DEBUG, "Filter input: %s, %ux%u (%"PRId64").\n",
           av_get_pix_fmt_name(input_frame->format),
           input_frame->width, input_frame->height, input_frame->pts);

    if (vpp_ctx->va_context == VA_INVALID_ID)
        return AVERROR(EINVAL);

    output_frame = ff_get_video_buffer(outlink, vpp_ctx->output_width,
                                       vpp_ctx->output_height);
    if (!output_frame) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    err = av_frame_copy_props(output_frame, input_frame);
    if (err < 0)
        goto fail;

    err = ff_vaapi_vpp_init_params(avctx, &params,
                                   input_frame, output_frame);
    if (err < 0)
        goto fail;

    if (vpp_ctx->nb_filter_buffers) {
        params.filters     = &vpp_ctx->filter_buffers[0];
        params.num_filters = vpp_ctx->nb_filter_buffers;
    }

    err = ff_vaapi_vpp_render_picture(avctx, &params, output_frame);
    if (err < 0)
        goto fail;

    av_frame_free(&input_frame);

    av_log(avctx, AV_LOG_DEBUG, "Filter output: %s, %ux%u (%"PRId64").\n",
           av_get_pix_fmt_name(output_frame->format),
           output_frame->width, output_frame->height, output_frame->pts);

    return ff_filter_frame(outlink, output_frame);

fail:
    av_frame_free(&input_frame);
    av_frame_free(&output_frame);
    return err;
}

static av_cold int denoise_vaapi_init(AVFilterContext *avctx)
{
    VAAPIVPPContext *vpp_ctx = avctx->priv;

    ff_vaapi_vpp_ctx_init(avctx);
    vpp_ctx->pipeline_uninit     = ff_vaapi_vpp_pipeline_uninit;
    vpp_ctx->build_filter_params = denoise_vaapi_build_filter_params;
    vpp_ctx->output_format       = AV_PIX_FMT_NONE;

    return 0;
}

static av_cold int sharpness_vaapi_init(AVFilterContext *avctx)
{
    VAAPIVPPContext *vpp_ctx = avctx->priv;

    ff_vaapi_vpp_ctx_init(avctx);
    vpp_ctx->pipeline_uninit     = ff_vaapi_vpp_pipeline_uninit;
    vpp_ctx->build_filter_params = sharpness_vaapi_build_filter_params;
    vpp_ctx->output_format       = AV_PIX_FMT_NONE;

    return 0;
}

#define DOFFSET(x) offsetof(DenoiseVAAPIContext, x)
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM)
static const AVOption denoise_vaapi_options[] = {
    { "denoise", "denoise level",
      DOFFSET(denoise), AV_OPT_TYPE_INT, { .i64 = DENOISE_DEFAULT }, DENOISE_MIN, DENOISE_MAX, .flags = FLAGS },
    { NULL },
};

#define SOFFSET(x) offsetof(SharpnessVAAPIContext, x)
static const AVOption sharpness_vaapi_options[] = {
    { "sharpness", "sharpness level",
      SOFFSET(sharpness), AV_OPT_TYPE_INT, { .i64 = SHARPNESS_DEFAULT }, SHARPNESS_MIN, SHARPNESS_MAX, .flags = FLAGS },
    { NULL },
};

AVFILTER_DEFINE_CLASS(denoise_vaapi);
AVFILTER_DEFINE_CLASS(sharpness_vaapi);

static const AVFilterPad misc_vaapi_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = &misc_vaapi_filter_frame,
        .config_props = &ff_vaapi_vpp_config_input,
    },
};

static const AVFilterPad misc_vaapi_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = &ff_vaapi_vpp_config_output,
    },
};

const AVFilter ff_vf_denoise_vaapi = {
    .name          = "denoise_vaapi",
    .description   = NULL_IF_CONFIG_SMALL("VAAPI VPP for de-noise"),
    .priv_size     = sizeof(DenoiseVAAPIContext),
    .init          = &denoise_vaapi_init,
    .uninit        = &ff_vaapi_vpp_ctx_uninit,
    FILTER_INPUTS(misc_vaapi_inputs),
    FILTER_OUTPUTS(misc_vaapi_outputs),
    FILTER_QUERY_FUNC(&ff_vaapi_vpp_query_formats),
    .priv_class    = &denoise_vaapi_class,
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};

const AVFilter ff_vf_sharpness_vaapi = {
    .name          = "sharpness_vaapi",
    .description   = NULL_IF_CONFIG_SMALL("VAAPI VPP for sharpness"),
    .priv_size     = sizeof(SharpnessVAAPIContext),
    .init          = &sharpness_vaapi_init,
    .uninit        = &ff_vaapi_vpp_ctx_uninit,
    FILTER_INPUTS(misc_vaapi_inputs),
    FILTER_OUTPUTS(misc_vaapi_outputs),
    FILTER_QUERY_FUNC(&ff_vaapi_vpp_query_formats),
    .priv_class    = &sharpness_vaapi_class,
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
