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
    "in_h", "ih",
    "in_w", "iw",
    "x",
    "y",
    "h",
    "w",
    "t",
    "fill",
    NULL
};

enum var_name {
    VAR_IN_H, VAR_IH,
    VAR_IN_W, VAR_IW,
    VAR_X,
    VAR_Y,
    VAR_H,
    VAR_W,
    VAR_T,
    VAR_MAX,
    VARS_NB
};

static const int NUM_EXPR_EVALS = 5;

typedef struct DrawboxVAAPIContext {
    VAAPIVPPContext vpp_ctx; // must be the first field
    VARectangle outer_rect, inner_rect;

    /* The hardware frame context containing the frames for outer_rect. */
    AVBufferRef *outer_frames_ref;
    AVHWFramesContext *outer_frames;
    AVFrame *outer_frame;

    char *x_expr;
    char *y_expr;
    char *w_expr;
    char *h_expr;
    char *t_expr;

    int w, h;
    int x, y;
    int replace;
    uint32_t thickness;
    uint8_t drawbox_rgba[4];

    int fill;

} DrawboxVAAPIContext;

static int drawbox_vaapi_config_output(AVFilterLink *outlink)
{
    AVFilterContext *avctx = outlink->src;
    AVFilterLink *inlink = avctx->inputs[0];
    DrawboxVAAPIContext *ctx = avctx->priv;
    VAAPIVPPContext *vpp_ctx = avctx->priv;
    double var_values[VARS_NB], res;
    int ret, i;
    char *expr;

    var_values[VAR_IN_W] = var_values[VAR_IW] = inlink->w;
    var_values[VAR_IN_H] = var_values[VAR_IH] = inlink->h;
    var_values[VAR_X] = NAN;
    var_values[VAR_Y] = NAN;
    var_values[VAR_H] = NAN;
    var_values[VAR_W] = NAN;
    var_values[VAR_T] = NAN;

    for (i = 0; i <= NUM_EXPR_EVALS; i++) {
        /* evaluate expressions, fail on last iteration */
        var_values[VAR_MAX] = inlink->w;
        if ((ret = av_expr_parse_and_eval(&res, (expr = ctx->x_expr),
                                          var_names, var_values,
                                          NULL, NULL, NULL, NULL, NULL, 0, ctx)) < 0 && i == NUM_EXPR_EVALS)
            goto fail;
        ctx->x = var_values[VAR_X] = res;

        var_values[VAR_MAX] = inlink->h;
        if ((ret = av_expr_parse_and_eval(&res, (expr = ctx->y_expr),
                                          var_names, var_values,
                                          NULL, NULL, NULL, NULL, NULL, 0, ctx)) < 0 && i == NUM_EXPR_EVALS)
            goto fail;
        ctx->y = var_values[VAR_Y] = res;

        var_values[VAR_MAX] = inlink->w - ctx->x;
        if ((ret = av_expr_parse_and_eval(&res, (expr = ctx->w_expr),
                                          var_names, var_values,
                                          NULL, NULL, NULL, NULL, NULL, 0, ctx)) < 0 && i == NUM_EXPR_EVALS)
            goto fail;
        ctx->w = var_values[VAR_W] = res;

        var_values[VAR_MAX] = inlink->h - ctx->y;
        if ((ret = av_expr_parse_and_eval(&res, (expr = ctx->h_expr),
                                          var_names, var_values,
                                          NULL, NULL, NULL, NULL, NULL, 0, ctx)) < 0 && i == NUM_EXPR_EVALS)
            goto fail;
        ctx->h = var_values[VAR_H] = res;

        var_values[VAR_MAX] = INT_MAX;
        if ((ret = av_expr_parse_and_eval(&res, (expr = ctx->t_expr),
                                          var_names, var_values,
                                          NULL, NULL, NULL, NULL, NULL, 0, ctx)) < 0 && i == NUM_EXPR_EVALS)
            goto fail;
        ctx->thickness = var_values[VAR_T] = res;
    }

    /* Sanity check */
    ctx->w = (ctx->w > 0) ? ctx->w : inlink->w;
    ctx->h = (ctx->h > 0) ? ctx->h : inlink->h;
    if (ctx->x + ctx->w > inlink->w)
        ctx->w = inlink->w - ctx->x;
    if (ctx->y + ctx->h > inlink->h)
        ctx->h = inlink->h - ctx->y;

    ctx->outer_rect.x = ctx->x;
    ctx->outer_rect.y = ctx->y;
    ctx->outer_rect.width = ctx->w;
    ctx->outer_rect.height = ctx->h;

    if (ctx->outer_rect.width <= ctx->thickness * 2 ||
        ctx->outer_rect.height <= ctx->thickness * 2) {
        ctx->fill = 1;
    } else {
        ctx->fill = 0;
        ctx->inner_rect.x = ctx->outer_rect.x + ctx->thickness;
        ctx->inner_rect.y = ctx->outer_rect.y + ctx->thickness;
        ctx->inner_rect.width = ctx->outer_rect.width - ctx->thickness * 2;
        ctx->inner_rect.height = ctx->outer_rect.height - ctx->thickness * 2;
    }

    vpp_ctx->output_width = inlink->w;
    vpp_ctx->output_height = inlink->h;

    ret = ff_vaapi_vpp_config_output(outlink);
    if (ret < 0)
        return ret;

    ctx->outer_frames_ref = av_hwframe_ctx_alloc(vpp_ctx->device_ref);
    if (!ctx->outer_frames_ref) {
        return AVERROR(ENOMEM);
    }

    ctx->outer_frames = (AVHWFramesContext*)ctx->outer_frames_ref->data;

    ctx->outer_frames->format    = AV_PIX_FMT_VAAPI;
    ctx->outer_frames->sw_format = vpp_ctx->input_frames->sw_format;
    ctx->outer_frames->width     = ctx->outer_rect.width;
    ctx->outer_frames->height    = ctx->outer_rect.height;

    return av_hwframe_ctx_init(ctx->outer_frames_ref);

fail:
    av_log(avctx, AV_LOG_ERROR,
           "Error when evaluating the expression '%s'.\n",
           expr);
    return ret;
}

static int drawbox_vaapi_filter_frame(AVFilterLink *link, AVFrame *input_frame)
{
    AVFilterContext *avctx = link->dst;
    AVFilterLink *outlink = avctx->outputs[0];
    VAAPIVPPContext *vpp_ctx = avctx->priv;
    DrawboxVAAPIContext *drawbox_ctx = avctx->priv;
    AVFrame *output_frame = NULL;
    VAProcPipelineParameterBuffer box_params;
    VAProcPipelineParameterBuffer params[3];
    VABlendState blend_state = {
        .flags = VA_BLEND_GLOBAL_ALPHA,
    };
    VARectangle box[4];
    int err, nb_params = 0;

    if (!input_frame->hw_frames_ctx ||
        vpp_ctx->va_context == VA_INVALID_ID) {
        err = AVERROR(EINVAL);
        goto fail;
    }

    if (!drawbox_ctx->outer_frame) {
        drawbox_ctx->outer_frame = av_frame_alloc();
        if (!drawbox_ctx->outer_frame) {
            err = AVERROR(ENOMEM);
            goto fail;
        }

        err = av_hwframe_get_buffer(drawbox_ctx->outer_frames_ref, drawbox_ctx->outer_frame, 0);
        if (err < 0) {
            err = AVERROR(ENOMEM);
            goto fail;
        }

        /* Create image for the outer rect */
        err = ff_vaapi_vpp_init_params(avctx, &box_params,
                                       input_frame, drawbox_ctx->outer_frame);
        if (err < 0)
            goto fail;

        blend_state.global_alpha = 0.0f;
        box_params.surface_region = &drawbox_ctx->outer_rect;
        box_params.blend_state = &blend_state;
        box_params.output_background_color = (drawbox_ctx->drawbox_rgba[3] << 24 |
                                              drawbox_ctx->drawbox_rgba[0] << 16 |
                                              drawbox_ctx->drawbox_rgba[1] << 8 |
                                              drawbox_ctx->drawbox_rgba[2]);

        err = ff_vaapi_vpp_render_picture(avctx, &box_params, drawbox_ctx->outer_frame);
        if (err < 0)
            goto fail;
    }

    /* Draw outer & inner rects on the input video, then we can get a box*/
    output_frame = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!output_frame) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    err = av_frame_copy_props(output_frame, input_frame);
    if (err < 0)
        goto fail;

    err = ff_vaapi_vpp_init_params(avctx, &params[nb_params],
                                   input_frame, output_frame);
    if (err < 0)
        goto fail;

    box[0].x = 0;
    box[0].y = 0;
    box[0].width = link->w;
    box[0].height = link->h;
    params[nb_params].surface_region = &box[0];
    params[nb_params].output_background_color = 0;
    nb_params++;

    err = ff_vaapi_vpp_init_params(avctx, &params[nb_params],
                                   drawbox_ctx->outer_frame, output_frame);
    if (err < 0)
        goto fail;

    box[1] = drawbox_ctx->outer_rect;
    if (drawbox_ctx->drawbox_rgba[3] != 255 && !drawbox_ctx->replace) {
        blend_state.global_alpha = (float)drawbox_ctx->drawbox_rgba[3] / 255;
        params[nb_params].blend_state = &blend_state;
    }
    params[nb_params].output_region = &box[1];
    params[nb_params].output_background_color = 0;
    nb_params++;

    if (!drawbox_ctx->fill) {
        box[3] = box[2] = drawbox_ctx->inner_rect;
        params[nb_params] = params[0];
        params[nb_params].surface_region = &box[2];
        params[nb_params].output_region = &box[3];
        params[nb_params].output_background_color = 0;
        nb_params++;
    }

    err = ff_vaapi_vpp_render_pictures(avctx, params, nb_params, output_frame);
    if (err < 0)
        goto fail;

    av_frame_free(&input_frame);

    return ff_filter_frame(outlink, output_frame);

fail:
    av_frame_free(&input_frame);
    av_frame_free(&output_frame);
    return err;
}

static av_cold int drawbox_vaapi_init(AVFilterContext *avctx)
{
    VAAPIVPPContext *vpp_ctx = avctx->priv;

    ff_vaapi_vpp_ctx_init(avctx);
    vpp_ctx->pipeline_uninit = ff_vaapi_vpp_pipeline_uninit;
    vpp_ctx->output_format = AV_PIX_FMT_NONE;

    return 0;
}

static av_cold void drawbox_vaapi_uninit(AVFilterContext *avctx)
{
    DrawboxVAAPIContext *ctx = avctx->priv;

    av_frame_free(&ctx->outer_frame);
    av_buffer_unref(&ctx->outer_frames_ref);
    ff_vaapi_vpp_ctx_uninit(avctx);
}

#define OFFSET(x) offsetof(DrawboxVAAPIContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption drawbox_vaapi_options[] = {
    { "x",         "set horizontal position of the left box edge", OFFSET(x_expr),       AV_OPT_TYPE_STRING, { .str="0" },       0, 0, FLAGS },
    { "y",         "set vertical position of the top box edge",    OFFSET(y_expr),       AV_OPT_TYPE_STRING, { .str="0" },       0, 0, FLAGS },
    { "width",     "set width of the box",                         OFFSET(w_expr),       AV_OPT_TYPE_STRING, { .str="0" },       0, 0, FLAGS },
    { "w",         "set width of the box",                         OFFSET(w_expr),       AV_OPT_TYPE_STRING, { .str="0" },       0, 0, FLAGS },
    { "height",    "set height of the box",                        OFFSET(h_expr),       AV_OPT_TYPE_STRING, { .str="0" },       0, 0, FLAGS },
    { "h",         "set height of the box",                        OFFSET(h_expr),       AV_OPT_TYPE_STRING, { .str="0" },       0, 0, FLAGS },
    { "color",     "set color of the box",                         OFFSET(drawbox_rgba), AV_OPT_TYPE_COLOR,  { .str = "black" }, 0, 0, FLAGS },
    { "c",         "set color of the box",                         OFFSET(drawbox_rgba), AV_OPT_TYPE_COLOR,  { .str = "black" }, 0, 0, FLAGS },
    { "thickness", "set the box thickness",                        OFFSET(t_expr),       AV_OPT_TYPE_STRING, { .str="3" },       0, 0, FLAGS },
    { "t",         "set the box thickness",                        OFFSET(t_expr),       AV_OPT_TYPE_STRING, { .str="3" },       0, 0, FLAGS },
    { "replace",   "replace color",                                OFFSET(replace),      AV_OPT_TYPE_BOOL,   { .i64=0   },       0, 1, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(drawbox_vaapi);

static const AVFilterPad drawbox_vaapi_inputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .filter_frame = drawbox_vaapi_filter_frame,
        .config_props = &ff_vaapi_vpp_config_input,
    },
};

static const AVFilterPad drawbox_vaapi_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = &drawbox_vaapi_config_output,
    },
};

const AVFilter ff_vf_drawbox_vaapi = {
    .name           = "drawbox_vaapi",
    .description    = NULL_IF_CONFIG_SMALL("Draw a colored box on the input video."),
    .priv_size      = sizeof(DrawboxVAAPIContext),
    .priv_class     = &drawbox_vaapi_class,
    .init           = &drawbox_vaapi_init,
    .uninit         = &drawbox_vaapi_uninit,
    FILTER_INPUTS(drawbox_vaapi_inputs),
    FILTER_OUTPUTS(drawbox_vaapi_outputs),
    FILTER_QUERY_FUNC(&ff_vaapi_vpp_query_formats),
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
