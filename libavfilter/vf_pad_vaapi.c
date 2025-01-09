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

#include "libavutil/colorspace.h"
#include "libavutil/eval.h"
#include "libavutil/opt.h"

#include "avfilter.h"
#include "filters.h"
#include "vaapi_vpp.h"
#include "video.h"

static const char *const var_names[] = {
    "in_w",   "iw",
    "in_h",   "ih",
    "out_w",  "ow",
    "out_h",  "oh",
    "x",
    "y",
    "a",
    "sar",
    "dar",
    NULL
};

enum var_name {
    VAR_IN_W,   VAR_IW,
    VAR_IN_H,   VAR_IH,
    VAR_OUT_W,  VAR_OW,
    VAR_OUT_H,  VAR_OH,
    VAR_X,
    VAR_Y,
    VAR_A,
    VAR_SAR,
    VAR_DAR,
    VARS_NB
};

typedef struct PadVAAPIContext {
    VAAPIVPPContext vpp_ctx; // must be the first field
    VARectangle rect;

    char *w_expr;
    char *h_expr;
    char *x_expr;
    char *y_expr;
    AVRational aspect;

    int w, h;
    int x, y;
    uint8_t pad_rgba[4];
} PadVAAPIContext;

static int pad_vaapi_config_output(AVFilterLink *outlink)
{
    AVFilterContext *avctx = outlink->src;
    AVFilterLink *inlink = avctx->inputs[0];
    PadVAAPIContext *ctx = avctx->priv;
    VAAPIVPPContext *vpp_ctx = avctx->priv;
    AVRational adjusted_aspect = ctx->aspect;
    double var_values[VARS_NB], res;
    int err, ret;
    char *expr;

    var_values[VAR_IN_W]  = var_values[VAR_IW] = inlink->w;
    var_values[VAR_IN_H]  = var_values[VAR_IH] = inlink->h;
    var_values[VAR_OUT_W] = var_values[VAR_OW] = NAN;
    var_values[VAR_OUT_H] = var_values[VAR_OH] = NAN;
    var_values[VAR_A]     = (double) inlink->w / inlink->h;
    var_values[VAR_SAR]   = inlink->sample_aspect_ratio.num ?
        (double) inlink->sample_aspect_ratio.num / inlink->sample_aspect_ratio.den : 1;
    var_values[VAR_DAR]   = var_values[VAR_A] * var_values[VAR_SAR];

    av_expr_parse_and_eval(&res, (expr = ctx->w_expr),
                           var_names, var_values,
                           NULL, NULL, NULL, NULL, NULL, 0, ctx);
    ctx->w = var_values[VAR_OUT_W] = var_values[VAR_OW] = res;
    if ((ret = av_expr_parse_and_eval(&res, (expr = ctx->h_expr),
                                      var_names, var_values,
                                      NULL, NULL, NULL, NULL, NULL, 0, ctx)) < 0)
        return ret;
    ctx->h = var_values[VAR_OUT_H] = var_values[VAR_OH] = res;
    if (!ctx->h)
        var_values[VAR_OUT_H] = var_values[VAR_OH] = ctx->h = inlink->h;

    /* evaluate the width again, as it may depend on the evaluated output height */
    if ((ret = av_expr_parse_and_eval(&res, (expr = ctx->w_expr),
                                      var_names, var_values,
                                      NULL, NULL, NULL, NULL, NULL, 0, ctx)) < 0)
        return ret;
    ctx->w = var_values[VAR_OUT_W] = var_values[VAR_OW] = res;
    if (!ctx->w)
        var_values[VAR_OUT_W] = var_values[VAR_OW] = ctx->w = inlink->w;

    if (adjusted_aspect.num && adjusted_aspect.den) {
        adjusted_aspect = av_div_q(adjusted_aspect, inlink->sample_aspect_ratio);
        if (ctx->h < av_rescale(ctx->w, adjusted_aspect.den, adjusted_aspect.num)) {
            ctx->h = var_values[VAR_OUT_H] = var_values[VAR_OH] = av_rescale(ctx->w, adjusted_aspect.den, adjusted_aspect.num);
        } else {
            ctx->w = var_values[VAR_OUT_W] = var_values[VAR_OW] = av_rescale(ctx->h, adjusted_aspect.num, adjusted_aspect.den);
        }
    }

    /* evaluate x and y */
    av_expr_parse_and_eval(&res, (expr = ctx->x_expr),
                           var_names, var_values,
                           NULL, NULL, NULL, NULL, NULL, 0, ctx);
    ctx->x = var_values[VAR_X] = res;
    if ((ret = av_expr_parse_and_eval(&res, (expr = ctx->y_expr),
                                      var_names, var_values,
                                      NULL, NULL, NULL, NULL, NULL, 0, ctx)) < 0)
        return ret;
    ctx->y = var_values[VAR_Y] = res;
    /* evaluate x again, as it may depend on the evaluated y value */
    if ((ret = av_expr_parse_and_eval(&res, (expr = ctx->x_expr),
                                      var_names, var_values,
                                      NULL, NULL, NULL, NULL, NULL, 0, ctx)) < 0)
        return ret;
    ctx->x = var_values[VAR_X] = res;

    if (ctx->x < 0 || ctx->x + inlink->w > ctx->w)
        ctx->x = var_values[VAR_X] = (ctx->w - inlink->w) / 2;
    if (ctx->y < 0 || ctx->y + inlink->h > ctx->h)
        ctx->y = var_values[VAR_Y] = (ctx->h - inlink->h) / 2;

    /* sanity check params */
    if (ctx->w < inlink->w || ctx->h < inlink->h) {
        av_log(ctx, AV_LOG_ERROR, "Padded dimensions cannot be smaller than input dimensions.\n");
        return AVERROR(EINVAL);
    }

    if (ctx->w > avctx->inputs[0]->w) {
        vpp_ctx->output_width  = ctx->w;
    } else {
        vpp_ctx->output_width  = avctx->inputs[0]->w;
    }

    if (ctx->h > avctx->inputs[0]->h) {
        vpp_ctx->output_height = ctx->h;
    } else {
        vpp_ctx->output_height = avctx->inputs[0]->h;
    }

    if (ctx->x + avctx->inputs[0]->w > vpp_ctx->output_width ||
        ctx->y + avctx->inputs[0]->h > vpp_ctx->output_height) {
        return AVERROR(EINVAL);
    }

    err = ff_vaapi_vpp_config_output(outlink);
    if (err < 0)
        return err;

    return 0;
}

static int pad_vaapi_filter_frame(AVFilterLink *link, AVFrame *input_frame)
{
    AVFilterContext *avctx = link->dst;
    AVFilterLink *outlink = avctx->outputs[0];
    VAAPIVPPContext *vpp_ctx = avctx->priv;
    PadVAAPIContext *pad_ctx = avctx->priv;
    AVFrame *output_frame = NULL;
    VAProcPipelineParameterBuffer params;
    int err;

    if (!input_frame->hw_frames_ctx ||
        vpp_ctx->va_context == VA_INVALID_ID) {
        err = AVERROR(EINVAL);
        goto fail;
    }

    output_frame = ff_get_video_buffer(outlink, outlink->w, outlink->h);
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

    pad_ctx->rect.x = pad_ctx->x;
    pad_ctx->rect.y = pad_ctx->y;
    pad_ctx->rect.width = link->w;
    pad_ctx->rect.height = link->h;
    params.output_region = &pad_ctx->rect;

    params.output_background_color = (pad_ctx->pad_rgba[3] << 24 |
                                      pad_ctx->pad_rgba[0] << 16 |
                                      pad_ctx->pad_rgba[1] << 8 |
                                      pad_ctx->pad_rgba[2]);

    err = ff_vaapi_vpp_render_picture(avctx, &params, output_frame);
    if (err < 0)
        goto fail;

    av_frame_free(&input_frame);

    return ff_filter_frame(outlink, output_frame);

fail:
    av_frame_free(&input_frame);
    av_frame_free(&output_frame);
    return err;
}

static av_cold int pad_vaapi_init(AVFilterContext *avctx)
{
    VAAPIVPPContext *vpp_ctx = avctx->priv;

    ff_vaapi_vpp_ctx_init(avctx);
    vpp_ctx->pipeline_uninit = ff_vaapi_vpp_pipeline_uninit;
    vpp_ctx->output_format = AV_PIX_FMT_NONE;

    return 0;
}

#define OFFSET(x) offsetof(PadVAAPIContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption pad_vaapi_options[] = {
    { "width",  "set the pad area width",       OFFSET(w_expr), AV_OPT_TYPE_STRING, {.str = "iw"}, 0, 0, FLAGS },
    { "w",      "set the pad area width",       OFFSET(w_expr), AV_OPT_TYPE_STRING, {.str = "iw"}, 0, 0, FLAGS },
    { "height", "set the pad area height",      OFFSET(h_expr), AV_OPT_TYPE_STRING, {.str = "ih"}, 0, 0, FLAGS },
    { "h",      "set the pad area height",      OFFSET(h_expr), AV_OPT_TYPE_STRING, {.str = "ih"}, 0, 0, FLAGS },
    { "x",      "set the x offset for the input image position", OFFSET(x_expr), AV_OPT_TYPE_STRING, {.str = "0"}, 0, INT16_MAX, FLAGS },
    { "y",      "set the y offset for the input image position", OFFSET(y_expr), AV_OPT_TYPE_STRING, {.str = "0"}, 0, INT16_MAX, FLAGS },
    { "color", "set the color of the padded area border", OFFSET(pad_rgba), AV_OPT_TYPE_COLOR, { .str = "black" }, 0, 0, FLAGS },
    { "aspect",  "pad to fit an aspect instead of a resolution", OFFSET(aspect), AV_OPT_TYPE_RATIONAL, {.dbl = 0}, 0, INT16_MAX, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(pad_vaapi);

static const AVFilterPad pad_vaapi_inputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .filter_frame = pad_vaapi_filter_frame,
        .config_props = &ff_vaapi_vpp_config_input,
    },
};

static const AVFilterPad pad_vaapi_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = &pad_vaapi_config_output,
    },
};

const FFFilter ff_vf_pad_vaapi = {
    .p.name         = "pad_vaapi",
    .p.description  = NULL_IF_CONFIG_SMALL("Pad the input video."),
    .p.priv_class   = &pad_vaapi_class,
    .priv_size      = sizeof(PadVAAPIContext),
    .init           = &pad_vaapi_init,
    .uninit         = &ff_vaapi_vpp_ctx_uninit,
    FILTER_INPUTS(pad_vaapi_inputs),
    FILTER_OUTPUTS(pad_vaapi_outputs),
    FILTER_QUERY_FUNC2(&ff_vaapi_vpp_query_formats),
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
