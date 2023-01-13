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
#include "framesync.h"
#include "formats.h"
#include "internal.h"
#include "vaapi_vpp.h"
#include "libavutil/eval.h"

enum var_name {
    VAR_MAIN_IW,     VAR_MW,
    VAR_MAIN_IH,     VAR_MH,
    VAR_OVERLAY_IW,
    VAR_OVERLAY_IH,
    VAR_OVERLAY_X,  VAR_OX,
    VAR_OVERLAY_Y,  VAR_OY,
    VAR_OVERLAY_W,  VAR_OW,
    VAR_OVERLAY_H,  VAR_OH,
    VAR_VARS_NB
};

typedef struct OverlayVAAPIContext {
    VAAPIVPPContext  vpp_ctx; /**< must be the first field */
    FFFrameSync      fs;

    double           var_values[VAR_VARS_NB];
    char             *overlay_ox;
    char             *overlay_oy;
    char             *overlay_ow;
    char             *overlay_oh;
    int              ox;
    int              oy;
    int              ow;
    int              oh;
    float            alpha;
    unsigned int     blend_flags;
    float            blend_alpha;
} OverlayVAAPIContext;

static const char *const var_names[] = {
    "main_w",     "W",   /* input width of the main layer */
    "main_h",     "H",   /* input height of the main layer */
    "overlay_iw",        /* input width of the overlay layer */
    "overlay_ih",        /* input height of the overlay layer */
    "overlay_x",  "x",   /* x position of the overlay layer inside of main */
    "overlay_y",  "y",   /* y position of the overlay layer inside of main */
    "overlay_w",  "w",   /* output width of overlay layer */
    "overlay_h",  "h",   /* output height of overlay layer */
    NULL
};

static int eval_expr(AVFilterContext *avctx)
{
    OverlayVAAPIContext *ctx = avctx->priv;
    double       *var_values = ctx->var_values;
    int                  ret = 0;
    AVExpr *ox_expr = NULL, *oy_expr = NULL;
    AVExpr *ow_expr = NULL, *oh_expr = NULL;

#define PARSE_EXPR(e, s) {\
    ret = av_expr_parse(&(e), s, var_names, NULL, NULL, NULL, NULL, 0, ctx); \
    if (ret < 0) {\
        av_log(ctx, AV_LOG_ERROR, "Error when parsing '%s'.\n", s);\
        goto release;\
    }\
}
    PARSE_EXPR(ox_expr, ctx->overlay_ox)
    PARSE_EXPR(oy_expr, ctx->overlay_oy)
    PARSE_EXPR(ow_expr, ctx->overlay_ow)
    PARSE_EXPR(oh_expr, ctx->overlay_oh)
#undef PASS_EXPR

    var_values[VAR_OVERLAY_W] =
    var_values[VAR_OW]        = av_expr_eval(ow_expr, var_values, NULL);
    var_values[VAR_OVERLAY_H] =
    var_values[VAR_OH]        = av_expr_eval(oh_expr, var_values, NULL);

    /* calc again in case ow is relative to oh */
    var_values[VAR_OVERLAY_W] =
    var_values[VAR_OW]        = av_expr_eval(ow_expr, var_values, NULL);

    var_values[VAR_OVERLAY_X] =
    var_values[VAR_OX]        = av_expr_eval(ox_expr, var_values, NULL);
    var_values[VAR_OVERLAY_Y] =
    var_values[VAR_OY]        = av_expr_eval(oy_expr, var_values, NULL);

    /* calc again in case ox is relative to oy */
    var_values[VAR_OVERLAY_X] =
    var_values[VAR_OX]        = av_expr_eval(ox_expr, var_values, NULL);

    /* calc overlay_w and overlay_h again incase relative to ox,oy */
    var_values[VAR_OVERLAY_W] =
    var_values[VAR_OW]        = av_expr_eval(ow_expr, var_values, NULL);
    var_values[VAR_OVERLAY_H] =
    var_values[VAR_OH]        = av_expr_eval(oh_expr, var_values, NULL);
    var_values[VAR_OVERLAY_W] =
    var_values[VAR_OW]        = av_expr_eval(ow_expr, var_values, NULL);

release:
    av_expr_free(ox_expr);
    av_expr_free(oy_expr);
    av_expr_free(ow_expr);
    av_expr_free(oh_expr);

    return ret;
}

static int overlay_vaapi_build_filter_params(AVFilterContext *avctx)
{
    VAAPIVPPContext *vpp_ctx   = avctx->priv;
    VAStatus vas;
    int support_flag;
    VAProcPipelineCaps pipeline_caps;

    memset(&pipeline_caps, 0, sizeof(pipeline_caps));
    vas = vaQueryVideoProcPipelineCaps(vpp_ctx->hwctx->display,
                                       vpp_ctx->va_context,
                                       NULL, 0,
                                       &pipeline_caps);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to query pipeline "
               "caps: %d (%s).\n", vas, vaErrorStr(vas));
        return AVERROR(EIO);
    }

    if (!pipeline_caps.blend_flags) {
        av_log(avctx, AV_LOG_ERROR, "VAAPI driver doesn't support overlay\n");
        return AVERROR(EINVAL);
    }

    support_flag = pipeline_caps.blend_flags & VA_BLEND_GLOBAL_ALPHA;
    if (!support_flag) {
        av_log(avctx, AV_LOG_ERROR, "VAAPI driver doesn't support global alpha blending\n");
        return AVERROR(EINVAL);
    }

    return 0;
}

static int overlay_vaapi_blend(FFFrameSync *fs)
{
    AVFilterContext    *avctx = fs->parent;
    AVFilterLink     *outlink = avctx->outputs[0];
    OverlayVAAPIContext *ctx  = avctx->priv;
    VAAPIVPPContext *vpp_ctx  = avctx->priv;
    AVFrame *input_main, *input_overlay;
    AVFrame *output;
    VAProcPipelineParameterBuffer params[2];
    VABlendState blend_state = { 0 }; /**< Blend State */
    VARectangle overlay_region, output_region;
    int err;

    err = ff_framesync_get_frame(fs, 0, &input_main, 0);
    if (err < 0)
        return err;
    err = ff_framesync_get_frame(fs, 1, &input_overlay, 0);
    if (err < 0)
        return err;

    av_log(avctx, AV_LOG_DEBUG, "Filter main: %s, %ux%u (%"PRId64").\n",
           av_get_pix_fmt_name(input_main->format),
           input_main->width, input_main->height, input_main->pts);

    if (vpp_ctx->va_context == VA_INVALID_ID)
        return AVERROR(EINVAL);

    output = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!output) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    err = av_frame_copy_props(output, input_main);
    if (err < 0)
        goto fail;

    err = ff_vaapi_vpp_init_params(avctx, &params[0],
                                   input_main, output);
    if (err < 0)
        goto fail;

    output_region = (VARectangle) {
        .x      = 0,
        .y      = 0,
        .width  = output->width,
        .height = output->height,
    };

    params[0].output_region = &output_region;
    params[0].output_background_color = VAAPI_VPP_BACKGROUND_BLACK;

    if (input_overlay) {
        av_log(avctx, AV_LOG_DEBUG, "Filter overlay: %s, %ux%u (%"PRId64").\n",
               av_get_pix_fmt_name(input_overlay->format),
               input_overlay->width, input_overlay->height, input_overlay->pts);

        overlay_region = (VARectangle) {
            .x      = ctx->ox,
            .y      = ctx->oy,
            .width  = ctx->ow ? ctx->ow : input_overlay->width,
            .height = ctx->oh ? ctx->oh : input_overlay->height,
        };

        if (overlay_region.x + overlay_region.width > input_main->width ||
            overlay_region.y + overlay_region.height > input_main->height) {
            av_log(ctx, AV_LOG_WARNING,
                   "The overlay image exceeds the scope of the main image, "
                   "will crop the overlay image according based on the main image.\n");
        }

        memcpy(&params[1], &params[0], sizeof(params[0]));

        blend_state.flags         = ctx->blend_flags;
        blend_state.global_alpha  = ctx->blend_alpha;
        params[1].blend_state = &blend_state;

        params[1].surface       = (VASurfaceID)(uintptr_t)input_overlay->data[3];
        params[1].output_region = &overlay_region;
    }

    err = ff_vaapi_vpp_render_pictures(avctx, params, input_overlay ? 2 : 1, output);
    if (err < 0)
        goto fail;

    av_log(avctx, AV_LOG_DEBUG, "Filter output: %s, %ux%u (%"PRId64").\n",
           av_get_pix_fmt_name(output->format),
           output->width, output->height, output->pts);

    return ff_filter_frame(outlink, output);

fail:
    av_frame_free(&output);
    return err;
}

static int have_alpha_planar(AVFilterLink *link)
{
    enum AVPixelFormat pix_fmt = link->format;
    const AVPixFmtDescriptor *desc;
    AVHWFramesContext *fctx;

    if (link->format == AV_PIX_FMT_VAAPI) {
        fctx    = (AVHWFramesContext *)link->hw_frames_ctx->data;
        pix_fmt = fctx->sw_format;
    }

    desc = av_pix_fmt_desc_get(pix_fmt);
    if (!desc)
        return 0;

    return !!(desc->flags & AV_PIX_FMT_FLAG_ALPHA);
}

static int overlay_vaapi_config_input_main(AVFilterLink *inlink)
{
    AVFilterContext  *avctx  = inlink->dst;
    OverlayVAAPIContext *ctx = avctx->priv;

    ctx->var_values[VAR_MAIN_IW] =
    ctx->var_values[VAR_MW]      = inlink->w;
    ctx->var_values[VAR_MAIN_IH] =
    ctx->var_values[VAR_MH]      = inlink->h;

    return ff_vaapi_vpp_config_input(inlink);
}

static int overlay_vaapi_config_input_overlay(AVFilterLink *inlink)
{
    AVFilterContext  *avctx  = inlink->dst;
    OverlayVAAPIContext *ctx = avctx->priv;
    int ret;

    ctx->var_values[VAR_OVERLAY_IW] = inlink->w;
    ctx->var_values[VAR_OVERLAY_IH] = inlink->h;

    ret = eval_expr(avctx);
    if (ret < 0)
        return ret;

    ctx->ox = (int)ctx->var_values[VAR_OX];
    ctx->oy = (int)ctx->var_values[VAR_OY];
    ctx->ow = (int)ctx->var_values[VAR_OW];
    ctx->oh = (int)ctx->var_values[VAR_OH];

    ctx->blend_flags = 0;
    ctx->blend_alpha = 1.0f;

    if (ctx->alpha < 1.0f) {
        ctx->blend_flags |= VA_BLEND_GLOBAL_ALPHA;
        ctx->blend_alpha  = ctx->alpha;
    }

    if (have_alpha_planar(inlink))
        ctx->blend_flags |= VA_BLEND_PREMULTIPLIED_ALPHA;

    return 0;
}

static int overlay_vaapi_config_output(AVFilterLink *outlink)
{
    AVFilterContext  *avctx  = outlink->src;
    OverlayVAAPIContext *ctx = avctx->priv;
    VAAPIVPPContext *vpp_ctx = avctx->priv;
    int err;

    outlink->time_base = avctx->inputs[0]->time_base;
    vpp_ctx->output_width  = avctx->inputs[0]->w;
    vpp_ctx->output_height = avctx->inputs[0]->h;

    err = ff_vaapi_vpp_config_output(outlink);
    if (err < 0)
        return err;

    err = overlay_vaapi_build_filter_params(avctx);
    if (err < 0)
        return err;

    err = ff_framesync_init_dualinput(&ctx->fs, avctx);
    if (err < 0)
        return err;

    ctx->fs.on_event = overlay_vaapi_blend;
    ctx->fs.time_base = outlink->time_base;

    return ff_framesync_configure(&ctx->fs);
}

static av_cold int overlay_vaapi_init(AVFilterContext *avctx)
{
    VAAPIVPPContext *vpp_ctx = avctx->priv;

    ff_vaapi_vpp_ctx_init(avctx);
    vpp_ctx->output_format = AV_PIX_FMT_NONE;

    return 0;
}

static int overlay_vaapi_activate(AVFilterContext *avctx)
{
    OverlayVAAPIContext *ctx = avctx->priv;

    return ff_framesync_activate(&ctx->fs);
}

static av_cold void overlay_vaapi_uninit(AVFilterContext *avctx)
{
    OverlayVAAPIContext *ctx = avctx->priv;

    ff_framesync_uninit(&ctx->fs);
    ff_vaapi_vpp_ctx_uninit(avctx);
}

#define OFFSET(x) offsetof(OverlayVAAPIContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)
static const AVOption overlay_vaapi_options[] = {
    { "x", "Overlay x position", OFFSET(overlay_ox),   AV_OPT_TYPE_STRING, { .str="0"}, 0, 255,          .flags = FLAGS},
    { "y", "Overlay y position", OFFSET(overlay_oy),   AV_OPT_TYPE_STRING, { .str="0"}, 0, 255,          .flags = FLAGS},
    { "w", "Overlay width",      OFFSET(overlay_ow),   AV_OPT_TYPE_STRING, { .str="overlay_iw"}, 0, 255, .flags = FLAGS},
    { "h", "Overlay height",     OFFSET(overlay_oh),   AV_OPT_TYPE_STRING, { .str="overlay_ih*w/overlay_iw"}, 0, 255, .flags = FLAGS},
    { "alpha", "Overlay global alpha", OFFSET(alpha),  AV_OPT_TYPE_FLOAT,  { .dbl = 1.0 }, 0.0, 1.0,      .flags = FLAGS },
    { "eof_action", "Action to take when encountering EOF from secondary input ",
        OFFSET(fs.opt_eof_action), AV_OPT_TYPE_INT, { .i64 = EOF_ACTION_REPEAT },
        EOF_ACTION_REPEAT, EOF_ACTION_PASS, .flags = FLAGS, "eof_action" },
        { "repeat", "Repeat the previous frame.",   0, AV_OPT_TYPE_CONST, { .i64 = EOF_ACTION_REPEAT }, .flags = FLAGS, "eof_action" },
        { "endall", "End both streams.",            0, AV_OPT_TYPE_CONST, { .i64 = EOF_ACTION_ENDALL }, .flags = FLAGS, "eof_action" },
        { "pass",   "Pass through the main input.", 0, AV_OPT_TYPE_CONST, { .i64 = EOF_ACTION_PASS },   .flags = FLAGS, "eof_action" },
    { "shortest", "force termination when the shortest input terminates", OFFSET(fs.opt_shortest),   AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, FLAGS },
    { "repeatlast", "repeat overlay of the last overlay frame",           OFFSET(fs.opt_repeatlast), AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1, FLAGS },
    { NULL },
};

FRAMESYNC_DEFINE_CLASS(overlay_vaapi, OverlayVAAPIContext, fs);

static const AVFilterPad overlay_vaapi_inputs[] = {
    {
        .name             = "main",
        .type             = AVMEDIA_TYPE_VIDEO,
        .config_props     = overlay_vaapi_config_input_main,
    },
    {
        .name             = "overlay",
        .type             = AVMEDIA_TYPE_VIDEO,
        .config_props     = overlay_vaapi_config_input_overlay,
    },
};

static const AVFilterPad overlay_vaapi_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = &overlay_vaapi_config_output,
    },
};

const AVFilter ff_vf_overlay_vaapi = {
    .name            = "overlay_vaapi",
    .description     = NULL_IF_CONFIG_SMALL("Overlay one video on top of another"),
    .priv_size       = sizeof(OverlayVAAPIContext),
    .priv_class      = &overlay_vaapi_class,
    .init            = &overlay_vaapi_init,
    .uninit          = &overlay_vaapi_uninit,
    .activate        = &overlay_vaapi_activate,
    .preinit         = overlay_vaapi_framesync_preinit,
    FILTER_INPUTS(overlay_vaapi_inputs),
    FILTER_OUTPUTS(overlay_vaapi_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_VAAPI),
    .flags_internal  = FF_FILTER_FLAG_HWFRAME_AWARE,
};
