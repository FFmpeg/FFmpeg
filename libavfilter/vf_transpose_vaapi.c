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
#include "transpose.h"
#include "vaapi_vpp.h"

typedef struct TransposeVAAPIContext {
    VAAPIVPPContext vpp_ctx; // must be the first field
    int passthrough;         // PassthroughType, landscape passthrough mode enabled
    int dir;                 // TransposeDir

    int rotation_state;
    int mirror_state;
} TransposeVAAPIContext;

static int transpose_vaapi_build_filter_params(AVFilterContext *avctx)
{
    VAAPIVPPContext *vpp_ctx   = avctx->priv;
    TransposeVAAPIContext *ctx = avctx->priv;
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

    if (!pipeline_caps.rotation_flags) {
        av_log(avctx, AV_LOG_ERROR, "VAAPI driver doesn't support transpose\n");
        return AVERROR(EINVAL);
    }

    switch (ctx->dir) {
    case TRANSPOSE_CCLOCK_FLIP:
        ctx->rotation_state = VA_ROTATION_270;
        ctx->mirror_state   = VA_MIRROR_VERTICAL;
        break;
    case TRANSPOSE_CLOCK:
        ctx->rotation_state = VA_ROTATION_90;
        ctx->mirror_state   = VA_MIRROR_NONE;
        break;
    case TRANSPOSE_CCLOCK:
        ctx->rotation_state = VA_ROTATION_270;
        ctx->mirror_state   = VA_MIRROR_NONE;
        break;
    case TRANSPOSE_CLOCK_FLIP:
        ctx->rotation_state = VA_ROTATION_90;
        ctx->mirror_state   = VA_MIRROR_VERTICAL;
        break;
    case TRANSPOSE_REVERSAL:
        ctx->rotation_state = VA_ROTATION_180;
        ctx->mirror_state   = VA_MIRROR_NONE;
        break;
    case TRANSPOSE_HFLIP:
        ctx->rotation_state = VA_ROTATION_NONE;
        ctx->mirror_state   = VA_MIRROR_HORIZONTAL;
        break;
    case TRANSPOSE_VFLIP:
        ctx->rotation_state = VA_ROTATION_NONE;
        ctx->mirror_state   = VA_MIRROR_VERTICAL;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Failed to set direction to %d\n", ctx->dir);
        return AVERROR(EINVAL);
    }

    if (VA_ROTATION_NONE != ctx->rotation_state) {
        support_flag = pipeline_caps.rotation_flags & (1 << ctx->rotation_state);
        if (!support_flag) {
            av_log(avctx, AV_LOG_ERROR, "VAAPI driver doesn't support rotation %d\n",
                   ctx->rotation_state);
            return AVERROR(EINVAL);
        }
    }

    if (VA_MIRROR_NONE != ctx->mirror_state) {
        support_flag = pipeline_caps.mirror_flags & ctx->mirror_state;
        if (!support_flag) {
            av_log(avctx, AV_LOG_ERROR, "VAAPI driver doesn't support mirror %d\n",
                   ctx->mirror_state);
            return AVERROR(EINVAL);
        }
    }

    return 0;
}

static int transpose_vaapi_filter_frame(AVFilterLink *inlink, AVFrame *input_frame)
{
    AVFilterContext *avctx     = inlink->dst;
    AVFilterLink *outlink      = avctx->outputs[0];
    VAAPIVPPContext *vpp_ctx   = avctx->priv;
    TransposeVAAPIContext *ctx = avctx->priv;
    AVFrame *output_frame      = NULL;
    VASurfaceID input_surface, output_surface;
    VARectangle input_region, output_region;

    VAProcPipelineParameterBuffer params;
    int err;

    if (ctx->passthrough)
        return ff_filter_frame(outlink, input_frame);

    av_log(avctx, AV_LOG_DEBUG, "Filter input: %s, %ux%u (%"PRId64").\n",
           av_get_pix_fmt_name(input_frame->format),
           input_frame->width, input_frame->height, input_frame->pts);

    if (vpp_ctx->va_context == VA_INVALID_ID)
        return AVERROR(EINVAL);

    input_surface = (VASurfaceID)(uintptr_t)input_frame->data[3];
    av_log(avctx, AV_LOG_DEBUG, "Using surface %#x for transpose vpp input.\n",
           input_surface);

    output_frame = ff_get_video_buffer(outlink, vpp_ctx->output_width,
                                       vpp_ctx->output_height);
    if (!output_frame) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    output_surface = (VASurfaceID)(uintptr_t)output_frame->data[3];
    av_log(avctx, AV_LOG_DEBUG, "Using surface %#x for transpose vpp output.\n",
           output_surface);
    memset(&params, 0, sizeof(params));
    input_region = (VARectangle) {
        .x      = 0,
        .y      = 0,
        .width  = input_frame->width,
        .height = input_frame->height,
    };

    output_region = (VARectangle) {
        .x      = 0,
        .y      = 0,
        .width  = output_frame->width,
        .height = output_frame->height,
    };

    params.rotation_state = ctx->rotation_state;
    params.mirror_state = ctx->mirror_state;

    params.filters     = &vpp_ctx->filter_buffers[0];
    params.num_filters = vpp_ctx->nb_filter_buffers;

    params.surface = input_surface;
    params.surface_region = &input_region;
    params.surface_color_standard =
        ff_vaapi_vpp_colour_standard(input_frame->colorspace);

    params.output_region = &output_region;
    params.output_background_color = VAAPI_VPP_BACKGROUND_BLACK;
    params.output_color_standard = params.surface_color_standard;

    err = ff_vaapi_vpp_render_picture(avctx, &params, output_surface);
    if (err < 0)
        goto fail;

    err = av_frame_copy_props(output_frame, input_frame);
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

static av_cold int transpose_vaapi_init(AVFilterContext *avctx)
{
    VAAPIVPPContext *vpp_ctx = avctx->priv;

    ff_vaapi_vpp_ctx_init(avctx);
    vpp_ctx->pipeline_uninit     = ff_vaapi_vpp_pipeline_uninit;
    vpp_ctx->build_filter_params = transpose_vaapi_build_filter_params;
    vpp_ctx->output_format       = AV_PIX_FMT_NONE;

    return 0;
}

static int transpose_vaapi_vpp_config_output(AVFilterLink *outlink)
{
    AVFilterContext *avctx     = outlink->src;
    VAAPIVPPContext *vpp_ctx   = avctx->priv;
    TransposeVAAPIContext *ctx = avctx->priv;
    AVFilterLink *inlink       = avctx->inputs[0];

    if ((inlink->w >= inlink->h && ctx->passthrough == TRANSPOSE_PT_TYPE_LANDSCAPE) ||
        (inlink->w <= inlink->h && ctx->passthrough == TRANSPOSE_PT_TYPE_PORTRAIT)) {
        outlink->hw_frames_ctx = av_buffer_ref(inlink->hw_frames_ctx);
        if (!outlink->hw_frames_ctx)
            return AVERROR(ENOMEM);
        av_log(avctx, AV_LOG_VERBOSE,
               "w:%d h:%d -> w:%d h:%d (passthrough mode)\n",
               inlink->w, inlink->h, inlink->w, inlink->h);
        return 0;
    }

    ctx->passthrough = TRANSPOSE_PT_TYPE_NONE;

    switch (ctx->dir) {
    case TRANSPOSE_CCLOCK_FLIP:
    case TRANSPOSE_CCLOCK:
    case TRANSPOSE_CLOCK:
    case TRANSPOSE_CLOCK_FLIP:
        vpp_ctx->output_width  = avctx->inputs[0]->h;
        vpp_ctx->output_height = avctx->inputs[0]->w;
        av_log(avctx, AV_LOG_DEBUG, "swap width and height for clock/cclock rotation\n");
        break;
    default:
        break;
    }

    return ff_vaapi_vpp_config_output(outlink);
}

static AVFrame *get_video_buffer(AVFilterLink *inlink, int w, int h)
{
    TransposeVAAPIContext *ctx = inlink->dst->priv;

    return ctx->passthrough ?
        ff_null_get_video_buffer(inlink, w, h) :
        ff_default_get_video_buffer(inlink, w, h);
}

#define OFFSET(x) offsetof(TransposeVAAPIContext, x)
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM)
static const AVOption transpose_vaapi_options[] = {
    { "dir", "set transpose direction", OFFSET(dir), AV_OPT_TYPE_INT, { .i64 = TRANSPOSE_CCLOCK_FLIP }, 0, 6, FLAGS, "dir" },
        { "cclock_flip",   "rotate counter-clockwise with vertical flip", 0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_CCLOCK_FLIP }, .flags=FLAGS, .unit = "dir" },
        { "clock",         "rotate clockwise",                            0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_CLOCK       }, .flags=FLAGS, .unit = "dir" },
        { "cclock",        "rotate counter-clockwise",                    0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_CCLOCK      }, .flags=FLAGS, .unit = "dir" },
        { "clock_flip",    "rotate clockwise with vertical flip",         0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_CLOCK_FLIP  }, .flags=FLAGS, .unit = "dir" },
        { "reversal",      "rotate by half-turn",                         0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_REVERSAL    }, .flags=FLAGS, .unit = "dir" },
        { "hflip",         "flip horizontally",                           0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_HFLIP       }, .flags=FLAGS, .unit = "dir" },
        { "vflip",         "flip vertically",                             0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_VFLIP       }, .flags=FLAGS, .unit = "dir" },

    { "passthrough", "do not apply transposition if the input matches the specified geometry",
      OFFSET(passthrough), AV_OPT_TYPE_INT, {.i64=TRANSPOSE_PT_TYPE_NONE},  0, INT_MAX, FLAGS, "passthrough" },
        { "none",      "always apply transposition",   0, AV_OPT_TYPE_CONST, {.i64=TRANSPOSE_PT_TYPE_NONE},      INT_MIN, INT_MAX, FLAGS, "passthrough" },
        { "portrait",  "preserve portrait geometry",   0, AV_OPT_TYPE_CONST, {.i64=TRANSPOSE_PT_TYPE_PORTRAIT},  INT_MIN, INT_MAX, FLAGS, "passthrough" },
        { "landscape", "preserve landscape geometry",  0, AV_OPT_TYPE_CONST, {.i64=TRANSPOSE_PT_TYPE_LANDSCAPE}, INT_MIN, INT_MAX, FLAGS, "passthrough" },

    { NULL }
};


AVFILTER_DEFINE_CLASS(transpose_vaapi);

static const AVFilterPad transpose_vaapi_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = &transpose_vaapi_filter_frame,
        .get_video_buffer = get_video_buffer,
        .config_props = &ff_vaapi_vpp_config_input,
    },
    { NULL }
};

static const AVFilterPad transpose_vaapi_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = &transpose_vaapi_vpp_config_output,
    },
    { NULL }
};

AVFilter ff_vf_transpose_vaapi = {
    .name           = "transpose_vaapi",
    .description    = NULL_IF_CONFIG_SMALL("VAAPI VPP for transpose"),
    .priv_size      = sizeof(TransposeVAAPIContext),
    .init           = &transpose_vaapi_init,
    .uninit         = &ff_vaapi_vpp_ctx_uninit,
    .query_formats  = &ff_vaapi_vpp_query_formats,
    .inputs         = transpose_vaapi_inputs,
    .outputs        = transpose_vaapi_outputs,
    .priv_class     = &transpose_vaapi_class,
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
