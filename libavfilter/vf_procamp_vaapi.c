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
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "vaapi_vpp.h"

// ProcAmp Min/Max/Default Values
#define BRIGHTNESS_MIN     -100.0F
#define BRIGHTNESS_MAX      100.0F
#define BRIGHTNESS_DEFAULT    0.0F

#define CONTRAST_MIN          0.0F
#define CONTRAST_MAX         10.0F
#define CONTRAST_DEFAULT      1.0F

#define HUE_MIN            -180.0F
#define HUE_MAX             180.0F
#define HUE_DEFAULT           0.0F

#define SATURATION_MIN        0.0F
#define SATURATION_MAX       10.0F
#define SATURATION_DEFAULT    1.0F

typedef struct ProcampVAAPIContext {
    VAAPIVPPContext vpp_ctx; // must be the first field

    float bright;
    float hue;
    float saturation;
    float contrast;
} ProcampVAAPIContext;

static float map(float x, float in_min, float in_max, float out_min, float out_max)
{
    double slope, output;

    slope = 1.0 * (out_max - out_min) / (in_max - in_min);
    output = out_min + slope * (x - in_min);

    return (float)output;
}

static int procamp_vaapi_build_filter_params(AVFilterContext *avctx)
{
    VAAPIVPPContext *vpp_ctx = avctx->priv;
    ProcampVAAPIContext *ctx = avctx->priv;
    VAStatus vas;
    VAProcFilterParameterBufferColorBalance procamp_params[4];
    VAProcFilterCapColorBalance procamp_caps[VAProcColorBalanceCount];
    int num_caps;
    int i = 0;

    memset(&procamp_params, 0, sizeof(procamp_params));
    memset(&procamp_caps, 0, sizeof(procamp_caps));

    num_caps = VAProcColorBalanceCount;
    vas = vaQueryVideoProcFilterCaps(vpp_ctx->hwctx->display, vpp_ctx->va_context,
                                     VAProcFilterColorBalance, &procamp_caps, &num_caps);

    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to query procamp "
               "filter caps: %d (%s).\n", vas, vaErrorStr(vas));
        return AVERROR(EIO);
    }

    /* brightness */
    procamp_params[i].type   = VAProcFilterColorBalance;
    procamp_params[i].attrib = VAProcColorBalanceBrightness;
    procamp_params[i].value  = map(ctx->bright, BRIGHTNESS_MIN, BRIGHTNESS_MAX,
                                   procamp_caps[VAProcColorBalanceBrightness-1].range.min_value,
                                   procamp_caps[VAProcColorBalanceBrightness-1].range.max_value);
    i++;

    /* contrast */
    procamp_params[i].type   = VAProcFilterColorBalance;
    procamp_params[i].attrib = VAProcColorBalanceContrast;
    procamp_params[i].value  = map(ctx->contrast, CONTRAST_MIN, CONTRAST_MAX,
                                   procamp_caps[VAProcColorBalanceContrast-1].range.min_value,
                                   procamp_caps[VAProcColorBalanceContrast-1].range.max_value);
    i++;

    /* hue */
    procamp_params[i].type   = VAProcFilterColorBalance;
    procamp_params[i].attrib = VAProcColorBalanceHue;
    procamp_params[i].value  = map(ctx->hue, HUE_MIN, HUE_MAX,
                                   procamp_caps[VAProcColorBalanceHue-1].range.min_value,
                                   procamp_caps[VAProcColorBalanceHue-1].range.max_value);
    i++;

    /* saturation */
    procamp_params[i].type   = VAProcFilterColorBalance;
    procamp_params[i].attrib = VAProcColorBalanceSaturation;
    procamp_params[i].value  = map(ctx->saturation, SATURATION_MIN, SATURATION_MAX,
                                   procamp_caps[VAProcColorBalanceSaturation-1].range.min_value,
                                   procamp_caps[VAProcColorBalanceSaturation-1].range.max_value);
    i++;

    return ff_vaapi_vpp_make_param_buffers(avctx,
                                           VAProcFilterParameterBufferType,
                                           &procamp_params,
                                           sizeof(procamp_params[0]),
                                           i);
}

static int procamp_vaapi_filter_frame(AVFilterLink *inlink, AVFrame *input_frame)
{
    AVFilterContext *avctx   = inlink->dst;
    AVFilterLink *outlink    = avctx->outputs[0];
    VAAPIVPPContext *vpp_ctx = avctx->priv;
    AVFrame *output_frame = NULL;
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

    params.filters     = &vpp_ctx->filter_buffers[0];
    params.num_filters = 1;

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

static av_cold int procamp_vaapi_init(AVFilterContext *avctx)
{
    VAAPIVPPContext *vpp_ctx = avctx->priv;

    ff_vaapi_vpp_ctx_init(avctx);
    vpp_ctx->pipeline_uninit     = ff_vaapi_vpp_pipeline_uninit;
    vpp_ctx->build_filter_params = procamp_vaapi_build_filter_params;
    vpp_ctx->output_format       = AV_PIX_FMT_NONE;

    return 0;
}

#define OFFSET(x) offsetof(ProcampVAAPIContext, x)
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM)
static const AVOption procamp_vaapi_options[] = {
    { "b", "Output video brightness",
      OFFSET(bright),  AV_OPT_TYPE_FLOAT, { .dbl = BRIGHTNESS_DEFAULT }, BRIGHTNESS_MIN, BRIGHTNESS_MAX, .flags = FLAGS },
    { "brightness", "Output video brightness",
      OFFSET(bright),  AV_OPT_TYPE_FLOAT, { .dbl = BRIGHTNESS_DEFAULT }, BRIGHTNESS_MIN, BRIGHTNESS_MAX, .flags = FLAGS },
    { "s", "Output video saturation",
      OFFSET(saturation), AV_OPT_TYPE_FLOAT, { .dbl = SATURATION_DEFAULT }, SATURATION_MIN, SATURATION_MAX, .flags = FLAGS },
    { "saturatio", "Output video saturation",
      OFFSET(saturation), AV_OPT_TYPE_FLOAT, { .dbl = SATURATION_DEFAULT }, SATURATION_MIN, SATURATION_MAX, .flags = FLAGS },
    { "c", "Output video contrast",
      OFFSET(contrast),  AV_OPT_TYPE_FLOAT, { .dbl = CONTRAST_DEFAULT }, CONTRAST_MIN, CONTRAST_MAX, .flags = FLAGS },
    { "contrast", "Output video contrast",
      OFFSET(contrast),  AV_OPT_TYPE_FLOAT, { .dbl = CONTRAST_DEFAULT }, CONTRAST_MIN, CONTRAST_MAX, .flags = FLAGS },
    { "h", "Output video hue",
      OFFSET(hue), AV_OPT_TYPE_FLOAT, { .dbl = HUE_DEFAULT }, HUE_MIN, HUE_MAX, .flags = FLAGS },
    { "hue", "Output video hue",
      OFFSET(hue), AV_OPT_TYPE_FLOAT, { .dbl = HUE_DEFAULT }, HUE_MIN, HUE_MAX, .flags = FLAGS },
    { NULL },
};

AVFILTER_DEFINE_CLASS(procamp_vaapi);

static const AVFilterPad procamp_vaapi_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = &procamp_vaapi_filter_frame,
        .config_props = &ff_vaapi_vpp_config_input,
    },
    { NULL }
};

static const AVFilterPad procamp_vaapi_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = &ff_vaapi_vpp_config_output,
    },
    { NULL }
};

AVFilter ff_vf_procamp_vaapi = {
    .name          = "procamp_vaapi",
    .description   = NULL_IF_CONFIG_SMALL("ProcAmp (color balance) adjustments for hue, saturation, brightness, contrast"),
    .priv_size     = sizeof(ProcampVAAPIContext),
    .init          = &procamp_vaapi_init,
    .uninit        = &ff_vaapi_vpp_ctx_uninit,
    .query_formats = &ff_vaapi_vpp_query_formats,
    .inputs        = procamp_vaapi_inputs,
    .outputs       = procamp_vaapi_outputs,
    .priv_class    = &procamp_vaapi_class,
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
