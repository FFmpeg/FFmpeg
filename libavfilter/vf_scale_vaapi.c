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
#include "filters.h"
#include "scale_eval.h"
#include "video.h"
#include "vaapi_vpp.h"

typedef struct ScaleVAAPIContext {
    VAAPIVPPContext vpp_ctx; // must be the first field

    char *output_format_string;

    int   mode;

    char *w_expr;      // width expression string
    char *h_expr;      // height expression string

    int force_original_aspect_ratio;
    int force_divisible_by;

    char *colour_primaries_string;
    char *colour_transfer_string;
    char *colour_matrix_string;
    int   colour_range;
    char *chroma_location_string;

    enum AVColorPrimaries colour_primaries;
    enum AVColorTransferCharacteristic colour_transfer;
    enum AVColorSpace colour_matrix;
    enum AVChromaLocation chroma_location;
} ScaleVAAPIContext;

static const char *scale_vaapi_mode_name(int mode)
{
    switch (mode) {
#define D(name) case VA_FILTER_SCALING_ ## name: return #name
        D(DEFAULT);
        D(FAST);
        D(HQ);
        D(NL_ANAMORPHIC);
#undef D
    default:
        return "Invalid";
    }
}


static int scale_vaapi_config_output(AVFilterLink *outlink)
{
    AVFilterLink *inlink     = outlink->src->inputs[0];
    AVFilterContext *avctx   = outlink->src;
    VAAPIVPPContext *vpp_ctx = avctx->priv;
    ScaleVAAPIContext *ctx   = avctx->priv;
    int err;

    if ((err = ff_scale_eval_dimensions(ctx,
                                        ctx->w_expr, ctx->h_expr,
                                        inlink, outlink,
                                        &vpp_ctx->output_width, &vpp_ctx->output_height)) < 0)
        return err;

    ff_scale_adjust_dimensions(inlink, &vpp_ctx->output_width, &vpp_ctx->output_height,
                               ctx->force_original_aspect_ratio, ctx->force_divisible_by);

    if (inlink->w == vpp_ctx->output_width && inlink->h == vpp_ctx->output_height &&
        (vpp_ctx->input_frames->sw_format == vpp_ctx->output_format ||
         vpp_ctx->output_format == AV_PIX_FMT_NONE) &&
        ctx->colour_primaries == AVCOL_PRI_UNSPECIFIED &&
        ctx->colour_transfer == AVCOL_TRC_UNSPECIFIED &&
        ctx->colour_matrix == AVCOL_SPC_UNSPECIFIED &&
        ctx->colour_range == AVCOL_RANGE_UNSPECIFIED &&
        ctx->chroma_location == AVCHROMA_LOC_UNSPECIFIED)
        vpp_ctx->passthrough = 1;

    err = ff_vaapi_vpp_config_output(outlink);
    if (err < 0)
        return err;

    if (inlink->sample_aspect_ratio.num)
        outlink->sample_aspect_ratio = av_mul_q((AVRational){outlink->h * inlink->w, outlink->w * inlink->h}, inlink->sample_aspect_ratio);
    else
        outlink->sample_aspect_ratio = inlink->sample_aspect_ratio;

    return 0;
}

static int scale_vaapi_filter_frame(AVFilterLink *inlink, AVFrame *input_frame)
{
    AVFilterContext *avctx   = inlink->dst;
    AVFilterLink *outlink    = avctx->outputs[0];
    VAAPIVPPContext *vpp_ctx = avctx->priv;
    ScaleVAAPIContext *ctx   = avctx->priv;
    AVFrame *output_frame    = NULL;
    VAProcPipelineParameterBuffer params;
    int err;

    av_log(avctx, AV_LOG_DEBUG, "Filter input: %s, %ux%u (%"PRId64").\n",
           av_get_pix_fmt_name(input_frame->format),
           input_frame->width, input_frame->height, input_frame->pts);

   if (vpp_ctx->passthrough)
       return ff_filter_frame(outlink, input_frame);

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

    if (ctx->colour_primaries != AVCOL_PRI_UNSPECIFIED)
        output_frame->color_primaries = ctx->colour_primaries;
    if (ctx->colour_transfer != AVCOL_TRC_UNSPECIFIED)
        output_frame->color_trc = ctx->colour_transfer;
    if (ctx->colour_matrix != AVCOL_SPC_UNSPECIFIED)
        output_frame->colorspace = ctx->colour_matrix;
    if (ctx->colour_range != AVCOL_RANGE_UNSPECIFIED)
        output_frame->color_range = ctx->colour_range;
    if (ctx->chroma_location != AVCHROMA_LOC_UNSPECIFIED)
        output_frame->chroma_location = ctx->chroma_location;

    err = ff_vaapi_vpp_init_params(avctx, &params,
                                   input_frame, output_frame);
    if (err < 0)
        goto fail;

    params.filter_flags |= ctx->mode;

    err = ff_vaapi_vpp_render_picture(avctx, &params, output_frame);
    if (err < 0)
        goto fail;

    av_frame_free(&input_frame);

    av_log(avctx, AV_LOG_DEBUG, "Filter output: %s, %ux%u (%"PRId64"), mode: %s.\n",
           av_get_pix_fmt_name(output_frame->format),
           output_frame->width, output_frame->height, output_frame->pts,
           scale_vaapi_mode_name(ctx->mode));

    return ff_filter_frame(outlink, output_frame);

fail:
    av_frame_free(&input_frame);
    av_frame_free(&output_frame);
    return err;
}

static av_cold int scale_vaapi_init(AVFilterContext *avctx)
{
    VAAPIVPPContext *vpp_ctx = avctx->priv;
    ScaleVAAPIContext *ctx   = avctx->priv;

    ff_vaapi_vpp_ctx_init(avctx);
    vpp_ctx->pipeline_uninit = ff_vaapi_vpp_pipeline_uninit;

    if (ctx->output_format_string) {
        vpp_ctx->output_format = av_get_pix_fmt(ctx->output_format_string);
        if (vpp_ctx->output_format == AV_PIX_FMT_NONE) {
            av_log(avctx, AV_LOG_ERROR, "Invalid output format.\n");
            return AVERROR(EINVAL);
        }
    } else {
        // Use the input format once that is configured.
        vpp_ctx->output_format = AV_PIX_FMT_NONE;
    }

#define STRING_OPTION(var_name, func_name, default_value) do { \
        if (ctx->var_name ## _string) { \
            int var = av_ ## func_name ## _from_name(ctx->var_name ## _string); \
            if (var < 0) { \
                av_log(avctx, AV_LOG_ERROR, "Invalid %s.\n", #var_name); \
                return AVERROR(EINVAL); \
            } \
            ctx->var_name = var; \
        } else { \
            ctx->var_name = default_value; \
        } \
    } while (0)

    STRING_OPTION(colour_primaries, color_primaries, AVCOL_PRI_UNSPECIFIED);
    STRING_OPTION(colour_transfer,  color_transfer,  AVCOL_TRC_UNSPECIFIED);
    STRING_OPTION(colour_matrix,    color_space,     AVCOL_SPC_UNSPECIFIED);
    STRING_OPTION(chroma_location,  chroma_location, AVCHROMA_LOC_UNSPECIFIED);

    return 0;
}

#define OFFSET(x) offsetof(ScaleVAAPIContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM)
static const AVOption scale_vaapi_options[] = {
    { "w", "Output video width",
      OFFSET(w_expr), AV_OPT_TYPE_STRING, {.str = "iw"}, .flags = FLAGS },
    { "h", "Output video height",
      OFFSET(h_expr), AV_OPT_TYPE_STRING, {.str = "ih"}, .flags = FLAGS },
    { "format", "Output video format (software format of hardware frames)",
      OFFSET(output_format_string), AV_OPT_TYPE_STRING, .flags = FLAGS },
    { "mode", "Scaling mode",
      OFFSET(mode), AV_OPT_TYPE_INT, { .i64 = VA_FILTER_SCALING_HQ },
      0, VA_FILTER_SCALING_NL_ANAMORPHIC, FLAGS, .unit = "mode" },
        { "default", "Use the default (depend on the driver) scaling algorithm",
          0, AV_OPT_TYPE_CONST, { .i64 = VA_FILTER_SCALING_DEFAULT }, 0, 0, FLAGS, .unit = "mode" },
        { "fast", "Use fast scaling algorithm",
          0, AV_OPT_TYPE_CONST, { .i64 = VA_FILTER_SCALING_FAST }, 0, 0, FLAGS, .unit = "mode" },
        { "hq", "Use high quality scaling algorithm",
          0, AV_OPT_TYPE_CONST, { .i64 = VA_FILTER_SCALING_HQ }, 0, 0, FLAGS,  .unit = "mode" },
        { "nl_anamorphic", "Use nolinear anamorphic scaling algorithm",
          0, AV_OPT_TYPE_CONST, { .i64 = VA_FILTER_SCALING_NL_ANAMORPHIC }, 0, 0, FLAGS,  .unit = "mode" },

    // These colour properties match the ones of the same name in vf_scale.
    { "out_color_matrix", "Output colour matrix coefficient set",
      OFFSET(colour_matrix_string), AV_OPT_TYPE_STRING, { .str = NULL }, .flags = FLAGS },
    { "out_range", "Output colour range",
      OFFSET(colour_range), AV_OPT_TYPE_INT, { .i64 = AVCOL_RANGE_UNSPECIFIED },
      AVCOL_RANGE_UNSPECIFIED, AVCOL_RANGE_JPEG, FLAGS, .unit = "range" },
    { "full",    "Full range",
      0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_RANGE_JPEG }, 0, 0, FLAGS, .unit = "range" },
    { "limited", "Limited range",
      0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_RANGE_MPEG }, 0, 0, FLAGS, .unit = "range" },
    { "jpeg",    "Full range",
      0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_RANGE_JPEG }, 0, 0, FLAGS, .unit = "range" },
    { "mpeg",    "Limited range",
      0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_RANGE_MPEG }, 0, 0, FLAGS, .unit = "range" },
    { "tv",      "Limited range",
      0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_RANGE_MPEG }, 0, 0, FLAGS, .unit = "range" },
    { "pc",      "Full range",
      0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_RANGE_JPEG }, 0, 0, FLAGS, .unit = "range" },
    // These colour properties are new here.
    { "out_color_primaries", "Output colour primaries",
      OFFSET(colour_primaries_string), AV_OPT_TYPE_STRING,
      { .str = NULL }, .flags = FLAGS },
    { "out_color_transfer", "Output colour transfer characteristics",
      OFFSET(colour_transfer_string),  AV_OPT_TYPE_STRING,
      { .str = NULL }, .flags = FLAGS },
    { "out_chroma_location", "Output chroma sample location",
      OFFSET(chroma_location_string),  AV_OPT_TYPE_STRING,
      { .str = NULL }, .flags = FLAGS },
    { "force_original_aspect_ratio", "decrease or increase w/h if necessary to keep the original AR", OFFSET(force_original_aspect_ratio), AV_OPT_TYPE_INT, { .i64 = 0}, 0, 2, FLAGS, .unit = "force_oar" },
    { "disable",  NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 0 }, 0, 0, FLAGS, .unit = "force_oar" },
    { "decrease", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 1 }, 0, 0, FLAGS, .unit = "force_oar" },
    { "increase", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 2 }, 0, 0, FLAGS, .unit = "force_oar" },
    { "force_divisible_by", "enforce that the output resolution is divisible by a defined integer when force_original_aspect_ratio is used", OFFSET(force_divisible_by), AV_OPT_TYPE_INT, { .i64 = 1}, 1, 256, FLAGS },

    { NULL },
};

AVFILTER_DEFINE_CLASS(scale_vaapi);

static const AVFilterPad scale_vaapi_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = &scale_vaapi_filter_frame,
        .config_props = &ff_vaapi_vpp_config_input,
    },
};

static const AVFilterPad scale_vaapi_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = &scale_vaapi_config_output,
    },
};

const AVFilter ff_vf_scale_vaapi = {
    .name          = "scale_vaapi",
    .description   = NULL_IF_CONFIG_SMALL("Scale to/from VAAPI surfaces."),
    .priv_size     = sizeof(ScaleVAAPIContext),
    .init          = &scale_vaapi_init,
    .uninit        = &ff_vaapi_vpp_ctx_uninit,
    FILTER_INPUTS(scale_vaapi_inputs),
    FILTER_OUTPUTS(scale_vaapi_outputs),
    FILTER_QUERY_FUNC(&ff_vaapi_vpp_query_formats),
    .priv_class    = &scale_vaapi_class,
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
