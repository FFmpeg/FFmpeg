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

/**
 * @file
 * scale video filter - QSV
 */

#include <mfxvideo.h>

#include <stdio.h>
#include <string.h>

#include "libavutil/avstring.h"
#include "libavutil/common.h"
#include "libavutil/eval.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_qsv.h"
#include "libavutil/internal.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/time.h"
#include "libavfilter/qsvvpp.h"

#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

static const char *const var_names[] = {
    "in_w",   "iw",
    "in_h",   "ih",
    "out_w",  "ow",
    "out_h",  "oh",
    "a", "dar",
    "sar",
    NULL
};

enum var_name {
    VAR_IN_W,   VAR_IW,
    VAR_IN_H,   VAR_IH,
    VAR_OUT_W,  VAR_OW,
    VAR_OUT_H,  VAR_OH,
    VAR_A, VAR_DAR,
    VAR_SAR,
    VARS_NB
};

#define MFX_IMPL_VIA_MASK(impl) (0x0f00 & (impl))

typedef struct QSVScaleContext {
    QSVVPPContext qsv;

    mfxExtVPPScaling         scale_conf;
    int                      mode;

    /**
     * New dimensions. Special values are:
     *   0 = original width/height
     *  -1 = keep original aspect
     */
    int w, h;

    /**
     * Output sw format. AV_PIX_FMT_NONE for no conversion.
     */
    enum AVPixelFormat format;

    char *w_expr;               ///< width  expression string
    char *h_expr;               ///< height expression string
    char *format_str;
} QSVScaleContext;

static av_cold int qsvscale_init(AVFilterContext *ctx)
{
    QSVScaleContext *s = ctx->priv;

    if (!strcmp(s->format_str, "same")) {
        s->format = AV_PIX_FMT_NONE;
    } else {
        s->format = av_get_pix_fmt(s->format_str);
        if (s->format == AV_PIX_FMT_NONE) {
            av_log(ctx, AV_LOG_ERROR, "Unrecognized pixel format: %s\n", s->format_str);
            return AVERROR(EINVAL);
        }
    }

    return 0;
}

static av_cold void qsvscale_uninit(AVFilterContext *ctx)
{
    ff_qsvvpp_close(ctx);
}

static int qsvscale_config_props(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = outlink->src->inputs[0];
    QSVScaleContext   *s = ctx->priv;
    QSVVPPParam    param = { NULL };
    mfxExtBuffer    *ext_buf[1];
    int64_t w, h;
    double var_values[VARS_NB], res;
    char *expr;
    int ret;
    enum AVPixelFormat in_format;

    var_values[VAR_IN_W]  = var_values[VAR_IW] = inlink->w;
    var_values[VAR_IN_H]  = var_values[VAR_IH] = inlink->h;
    var_values[VAR_OUT_W] = var_values[VAR_OW] = NAN;
    var_values[VAR_OUT_H] = var_values[VAR_OH] = NAN;
    var_values[VAR_A]     = (double) inlink->w / inlink->h;
    var_values[VAR_SAR]   = inlink->sample_aspect_ratio.num ?
        (double) inlink->sample_aspect_ratio.num / inlink->sample_aspect_ratio.den : 1;
    var_values[VAR_DAR]   = var_values[VAR_A] * var_values[VAR_SAR];

    /* evaluate width and height */
    av_expr_parse_and_eval(&res, (expr = s->w_expr),
                           var_names, var_values,
                           NULL, NULL, NULL, NULL, NULL, 0, ctx);
    s->w = var_values[VAR_OUT_W] = var_values[VAR_OW] = res;
    if ((ret = av_expr_parse_and_eval(&res, (expr = s->h_expr),
                                      var_names, var_values,
                                      NULL, NULL, NULL, NULL, NULL, 0, ctx)) < 0)
        goto fail;
    s->h = var_values[VAR_OUT_H] = var_values[VAR_OH] = res;
    /* evaluate again the width, as it may depend on the output height */
    if ((ret = av_expr_parse_and_eval(&res, (expr = s->w_expr),
                                      var_names, var_values,
                                      NULL, NULL, NULL, NULL, NULL, 0, ctx)) < 0)
        goto fail;
    s->w = res;

    w = s->w;
    h = s->h;

    /* sanity check params */
    if (w <  -1 || h <  -1) {
        av_log(ctx, AV_LOG_ERROR, "Size values less than -1 are not acceptable.\n");
        return AVERROR(EINVAL);
    }
    if (w == -1 && h == -1)
        s->w = s->h = 0;

    if (!(w = s->w))
        w = inlink->w;
    if (!(h = s->h))
        h = inlink->h;
    if (w == -1)
        w = av_rescale(h, inlink->w, inlink->h);
    if (h == -1)
        h = av_rescale(w, inlink->h, inlink->w);

    if (w > INT_MAX || h > INT_MAX ||
        (h * inlink->w) > INT_MAX  ||
        (w * inlink->h) > INT_MAX)
        av_log(ctx, AV_LOG_ERROR, "Rescaled value for width or height is too big.\n");

    outlink->w = w;
    outlink->h = h;

    if (inlink->format == AV_PIX_FMT_QSV) {
        if (!inlink->hw_frames_ctx || !inlink->hw_frames_ctx->data)
            return AVERROR(EINVAL);
        else
            in_format = ((AVHWFramesContext*)inlink->hw_frames_ctx->data)->sw_format;
    } else
        in_format = inlink->format;

    if (s->format == AV_PIX_FMT_NONE)
        s->format = in_format;

    outlink->frame_rate = inlink->frame_rate;
    outlink->time_base = av_inv_q(inlink->frame_rate);
    param.out_sw_format = s->format;

    param.ext_buf                      = ext_buf;
    memset(&s->scale_conf, 0, sizeof(mfxExtVPPScaling));
    s->scale_conf.Header.BufferId      = MFX_EXTBUFF_VPP_SCALING;
    s->scale_conf.Header.BufferSz      = sizeof(mfxExtVPPScaling);
    s->scale_conf.ScalingMode          = s->mode;
    param.ext_buf[param.num_ext_buf++] = (mfxExtBuffer*)&s->scale_conf;
    av_log(ctx, AV_LOG_VERBOSE, "Scaling mode: %d\n", s->mode);

    ret = ff_qsvvpp_init(ctx, &param);
    if (ret < 0)
        return ret;

    av_log(ctx, AV_LOG_VERBOSE, "w:%d h:%d -> w:%d h:%d\n",
           inlink->w, inlink->h, outlink->w, outlink->h);

    if (inlink->sample_aspect_ratio.num)
        outlink->sample_aspect_ratio = av_mul_q((AVRational){outlink->h*inlink->w,
                                                             outlink->w*inlink->h},
                                                inlink->sample_aspect_ratio);
    else
        outlink->sample_aspect_ratio = inlink->sample_aspect_ratio;

    return 0;

fail:
    av_log(ctx, AV_LOG_ERROR,
           "Error when evaluating the expression '%s'\n", expr);
    return ret;
}

static int qsvscale_filter_frame(AVFilterLink *link, AVFrame *in)
{
    int               ret = 0;
    AVFilterContext  *ctx = link->dst;
    QSVVPPContext    *qsv = ctx->priv;

    ret = ff_qsvvpp_filter_frame(qsv, link, in);
    av_frame_free(&in);
    return ret;
}

#define OFFSET(x) offsetof(QSVScaleContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
static const AVOption options[] = {
    { "w",      "Output video width",  OFFSET(w_expr),     AV_OPT_TYPE_STRING, { .str = "iw"   }, .flags = FLAGS },
    { "h",      "Output video height", OFFSET(h_expr),     AV_OPT_TYPE_STRING, { .str = "ih"   }, .flags = FLAGS },
    { "format", "Output pixel format", OFFSET(format_str), AV_OPT_TYPE_STRING, { .str = "same" }, .flags = FLAGS },

    { "mode",      "set scaling mode",    OFFSET(mode),    AV_OPT_TYPE_INT,    { .i64 = MFX_SCALING_MODE_DEFAULT}, MFX_SCALING_MODE_DEFAULT, MFX_SCALING_MODE_QUALITY, FLAGS, "mode"},
    { "low_power", "low power mode",        0,             AV_OPT_TYPE_CONST,  { .i64 = MFX_SCALING_MODE_LOWPOWER}, INT_MIN, INT_MAX, FLAGS, "mode"},
    { "hq",        "high quality mode",     0,             AV_OPT_TYPE_CONST,  { .i64 = MFX_SCALING_MODE_QUALITY},  INT_MIN, INT_MAX, FLAGS, "mode"},

    { NULL },
};

static const AVClass qsvscale_class = {
    .class_name = "scale_qsv",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVFilterPad qsvscale_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = qsvscale_filter_frame,
        .get_buffer.video = ff_qsvvpp_get_video_buffer,
    },
};

static const AVFilterPad qsvscale_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = qsvscale_config_props,
    },
};

const AVFilter ff_vf_scale_qsv = {
    .name      = "scale_qsv",
    .description = NULL_IF_CONFIG_SMALL("QuickSync video scaling and format conversion"),

    .init          = qsvscale_init,
    .uninit        = qsvscale_uninit,

    .priv_size = sizeof(QSVScaleContext),
    .priv_class = &qsvscale_class,

    FILTER_INPUTS(qsvscale_inputs),
    FILTER_OUTPUTS(qsvscale_outputs),

    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_QSV),

    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
