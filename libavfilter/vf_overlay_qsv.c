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
 * A hardware accelerated overlay filter based on Intel Quick Sync Video VPP
 */

#include "libavutil/opt.h"
#include "libavutil/common.h"
#include "libavutil/pixdesc.h"
#include "libavutil/eval.h"
#include "libavutil/hwcontext.h"
#include "libavutil/avstring.h"
#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/mathematics.h"

#include "internal.h"
#include "avfilter.h"
#include "formats.h"
#include "video.h"

#include "qsvvpp.h"

#define MAIN    0
#define OVERLAY 1

#define OFFSET(x) offsetof(QSVOverlayContext, x)
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM)

enum var_name {
    VAR_MAIN_iW,     VAR_MW,
    VAR_MAIN_iH,     VAR_MH,
    VAR_OVERLAY_iW,
    VAR_OVERLAY_iH,
    VAR_OVERLAY_X,  VAR_OX,
    VAR_OVERLAY_Y,  VAR_OY,
    VAR_OVERLAY_W,  VAR_OW,
    VAR_OVERLAY_H,  VAR_OH,
    VAR_VARS_NB
};

enum EOFAction {
    EOF_ACTION_REPEAT,
    EOF_ACTION_ENDALL
};

typedef struct QSVOverlayContext {
    const AVClass      *class;

    QSVVPPContext      *qsv;
    QSVVPPParam        qsv_param;
    mfxExtVPPComposite comp_conf;
    double             var_values[VAR_VARS_NB];

    char     *overlay_ox, *overlay_oy, *overlay_ow, *overlay_oh;
    uint16_t  overlay_alpha, overlay_pixel_alpha;

    enum EOFAction eof_action;  /* action to take on EOF from source */

    AVFrame *main;
    AVFrame *over_prev, *over_next;
} QSVOverlayContext;

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

static const AVOption options[] = {
    { "x", "Overlay x position", OFFSET(overlay_ox), AV_OPT_TYPE_STRING, { .str="0"}, 0, 255, .flags = FLAGS},
    { "y", "Overlay y position", OFFSET(overlay_oy), AV_OPT_TYPE_STRING, { .str="0"}, 0, 255, .flags = FLAGS},
    { "w", "Overlay width",      OFFSET(overlay_ow), AV_OPT_TYPE_STRING, { .str="overlay_iw"}, 0, 255, .flags = FLAGS},
    { "h", "Overlay height",     OFFSET(overlay_oh), AV_OPT_TYPE_STRING, { .str="overlay_ih*w/overlay_iw"}, 0, 255, .flags = FLAGS},
    { "alpha", "Overlay global alpha", OFFSET(overlay_alpha), AV_OPT_TYPE_INT, { .i64 = 255}, 0, 255, .flags = FLAGS},
    { "eof_action", "Action to take when encountering EOF from secondary input ",
        OFFSET(eof_action), AV_OPT_TYPE_INT, { .i64 = EOF_ACTION_REPEAT },
        EOF_ACTION_REPEAT, EOF_ACTION_ENDALL, .flags = FLAGS, "eof_action" },
        { "repeat", "Repeat the previous frame.", 0, AV_OPT_TYPE_CONST, { .i64 = EOF_ACTION_REPEAT }, .flags = FLAGS, "eof_action" },
        { "endall", "End both streams.",          0, AV_OPT_TYPE_CONST, { .i64 = EOF_ACTION_ENDALL }, .flags = FLAGS, "eof_action" },
    { NULL }
};

static int eval_expr(AVFilterContext *ctx)
{
    QSVOverlayContext *vpp = ctx->priv;
    double     *var_values = vpp->var_values;
    int                ret = 0;
    AVExpr *ox_expr = NULL, *oy_expr = NULL;
    AVExpr *ow_expr = NULL, *oh_expr = NULL;

#define PASS_EXPR(e, s) {\
    ret = av_expr_parse(&e, s, var_names, NULL, NULL, NULL, NULL, 0, ctx); \
    if (ret < 0) {\
        av_log(ctx, AV_LOG_ERROR, "Error when passing '%s'.\n", s);\
        goto release;\
    }\
}
    PASS_EXPR(ox_expr, vpp->overlay_ox);
    PASS_EXPR(oy_expr, vpp->overlay_oy);
    PASS_EXPR(ow_expr, vpp->overlay_ow);
    PASS_EXPR(oh_expr, vpp->overlay_oh);
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

static int have_alpha_planar(AVFilterLink *link)
{
    enum AVPixelFormat pix_fmt;
    const AVPixFmtDescriptor *desc;
    AVHWFramesContext *fctx;

    if (link->format == AV_PIX_FMT_QSV) {
        fctx    = (AVHWFramesContext *)link->hw_frames_ctx->data;
        pix_fmt = fctx->sw_format;
    }

    desc = av_pix_fmt_desc_get(pix_fmt);
    if (!desc)
        return 0;

    return !!(desc->flags & AV_PIX_FMT_FLAG_ALPHA);
}

static int config_main_input(AVFilterLink *inlink)
{
    AVFilterContext      *ctx = inlink->dst;
    QSVOverlayContext    *vpp = ctx->priv;
    mfxVPPCompInputStream *st = &vpp->comp_conf.InputStream[0];

    av_log(ctx, AV_LOG_DEBUG, "Input[%d] is of %s.\n", FF_INLINK_IDX(inlink),
           av_get_pix_fmt_name(inlink->format));

    vpp->var_values[VAR_MAIN_iW] =
    vpp->var_values[VAR_MW]      = inlink->w;
    vpp->var_values[VAR_MAIN_iH] =
    vpp->var_values[VAR_MH]      = inlink->h;

    st->DstX              = 0;
    st->DstY              = 0;
    st->DstW              = inlink->w;
    st->DstH              = inlink->h;
    st->GlobalAlphaEnable = 0;
    st->PixelAlphaEnable  = 0;

    return 0;
}

static int config_overlay_input(AVFilterLink *inlink)
{
    AVFilterContext       *ctx = inlink->dst;
    QSVOverlayContext     *vpp = ctx->priv;
    mfxVPPCompInputStream *st  = &vpp->comp_conf.InputStream[1];
    int                    ret = 0;

    av_log(ctx, AV_LOG_DEBUG, "Input[%d] is of %s.\n", FF_INLINK_IDX(inlink),
           av_get_pix_fmt_name(inlink->format));

    vpp->var_values[VAR_OVERLAY_iW] = inlink->w;
    vpp->var_values[VAR_OVERLAY_iH] = inlink->h;

    ret = eval_expr(ctx);
    if (ret < 0)
        return ret;

    st->DstX              = vpp->var_values[VAR_OX];
    st->DstY              = vpp->var_values[VAR_OY];
    st->DstW              = vpp->var_values[VAR_OW];
    st->DstH              = vpp->var_values[VAR_OH];
    st->GlobalAlpha       = vpp->overlay_alpha;
    st->GlobalAlphaEnable = (st->GlobalAlpha < 255);
    st->PixelAlphaEnable  = have_alpha_planar(inlink);

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext   *ctx = outlink->src;
    QSVOverlayContext *vpp = ctx->priv;
    AVFilterLink      *in0 = ctx->inputs[0];
    AVFilterLink      *in1 = ctx->inputs[1];

    av_log(ctx, AV_LOG_DEBUG, "Output is of %s.\n", av_get_pix_fmt_name(outlink->format));
    if ((in0->format == AV_PIX_FMT_QSV && in1->format != AV_PIX_FMT_QSV) ||
        (in0->format != AV_PIX_FMT_QSV && in1->format == AV_PIX_FMT_QSV)) {
        av_log(ctx, AV_LOG_ERROR, "Mixing hardware and software pixel formats is not supported.\n");
        return AVERROR(EINVAL);
    } else if (in0->format == AV_PIX_FMT_QSV) {
        AVHWFramesContext *hw_frame0 = (AVHWFramesContext *)in0->hw_frames_ctx->data;
        AVHWFramesContext *hw_frame1 = (AVHWFramesContext *)in1->hw_frames_ctx->data;

        if (hw_frame0->device_ctx != hw_frame1->device_ctx) {
            av_log(ctx, AV_LOG_ERROR, "Inputs with different underlying QSV devices are forbidden.\n");
            return AVERROR(EINVAL);
        }
    }

    outlink->w          = vpp->var_values[VAR_MW];
    outlink->h          = vpp->var_values[VAR_MH];
    outlink->frame_rate = in0->frame_rate;
    outlink->time_base  = av_inv_q(outlink->frame_rate);

    return ff_qsvvpp_create(ctx, &vpp->qsv, &vpp->qsv_param);
}

static int blend_frame(AVFilterContext *ctx, AVFrame *mpic, AVFrame *opic)
{
    int                ret = 0;
    QSVOverlayContext *vpp = ctx->priv;
    AVFrame     *opic_copy = NULL;

    ret = ff_qsvvpp_filter_frame(vpp->qsv, ctx->inputs[0], mpic);
    if (ret == 0 || ret == AVERROR(EAGAIN)) {
        /* Reference the overlay frame. Because:
         * 1. ff_qsvvpp_filter_frame will take control of the given frame
         * 2. We need to repeat the overlay frame when 2nd input goes into EOF
         */
        opic_copy = av_frame_clone(opic);
        if (!opic_copy)
            return AVERROR(ENOMEM);

        ret = ff_qsvvpp_filter_frame(vpp->qsv, ctx->inputs[1], opic_copy);
    }

    return ret;
}

static int handle_overlay_eof(AVFilterContext *ctx)
{
    int              ret = 0;
    QSVOverlayContext *s = ctx->priv;
    /* Repeat previous frame on secondary input */
    if (s->over_prev && s->eof_action == EOF_ACTION_REPEAT)
        ret = blend_frame(ctx, s->main, s->over_prev);
    /* End both streams */
    else if (s->eof_action == EOF_ACTION_ENDALL)
        return AVERROR_EOF;

    s->main = NULL;

    return ret;
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    QSVOverlayContext *s = ctx->priv;
    AVRational   tb_main = ctx->inputs[MAIN]->time_base;
    AVRational   tb_over = ctx->inputs[OVERLAY]->time_base;
    int              ret = 0;

    /* get a frame on the main input */
    if (!s->main) {
        ret = ff_request_frame(ctx->inputs[MAIN]);
        if (ret < 0)
            return ret;
    }

    /* get a new frame on the overlay input, on EOF check setting 'eof_action' */
    if (!s->over_next) {
        ret = ff_request_frame(ctx->inputs[OVERLAY]);
        if (ret == AVERROR_EOF)
            return handle_overlay_eof(ctx);
        else if (ret < 0)
            return ret;
    }

    while (s->main->pts != AV_NOPTS_VALUE &&
           s->over_next->pts != AV_NOPTS_VALUE &&
           av_compare_ts(s->over_next->pts, tb_over, s->main->pts, tb_main) < 0) {
        av_frame_free(&s->over_prev);
        FFSWAP(AVFrame*, s->over_prev, s->over_next);

        ret = ff_request_frame(ctx->inputs[OVERLAY]);
        if (ret == AVERROR_EOF)
            return handle_overlay_eof(ctx);
        else if (ret < 0)
            return ret;
    }

    if (s->main->pts == AV_NOPTS_VALUE ||
        s->over_next->pts == AV_NOPTS_VALUE ||
        !av_compare_ts(s->over_next->pts, tb_over, s->main->pts, tb_main)) {
        ret = blend_frame(ctx, s->main, s->over_next);
        av_frame_free(&s->over_prev);
        FFSWAP(AVFrame*, s->over_prev, s->over_next);
    } else if (s->over_prev) {
        ret = blend_frame(ctx, s->main, s->over_prev);
    } else {
        av_frame_free(&s->main);
        ret = AVERROR(EAGAIN);
    }

    s->main = NULL;

    return ret;
}

static int filter_frame_main(AVFilterLink *inlink, AVFrame *frame)
{
    QSVOverlayContext *s = inlink->dst->priv;

    av_assert0(!s->main);
    s->main = frame;

    return 0;
}

static int filter_frame_overlay(AVFilterLink *inlink, AVFrame *frame)
{
    QSVOverlayContext *s = inlink->dst->priv;

    av_assert0(!s->over_next);
    s->over_next = frame;

    return 0;
}

static int overlay_qsv_init(AVFilterContext *ctx)
{
    QSVOverlayContext *vpp = ctx->priv;

    /* fill composite config */
    vpp->comp_conf.Header.BufferId = MFX_EXTBUFF_VPP_COMPOSITE;
    vpp->comp_conf.Header.BufferSz = sizeof(vpp->comp_conf);
    vpp->comp_conf.NumInputStream  = ctx->nb_inputs;
    vpp->comp_conf.InputStream     = av_mallocz_array(ctx->nb_inputs,
                                                      sizeof(*vpp->comp_conf.InputStream));
    if (!vpp->comp_conf.InputStream)
        return AVERROR(ENOMEM);

    /* initialize QSVVPP params */
    vpp->qsv_param.filter_frame = NULL;
    vpp->qsv_param.ext_buf      = av_mallocz(sizeof(*vpp->qsv_param.ext_buf));
    if (!vpp->qsv_param.ext_buf)
        return AVERROR(ENOMEM);

    vpp->qsv_param.ext_buf[0]    = (mfxExtBuffer *)&vpp->comp_conf;
    vpp->qsv_param.num_ext_buf   = 1;
    vpp->qsv_param.out_sw_format = AV_PIX_FMT_NV12;
    vpp->qsv_param.num_crop      = 0;

    return 0;
}

static void overlay_qsv_uninit(AVFilterContext *ctx)
{
    QSVOverlayContext *vpp = ctx->priv;

    av_frame_free(&vpp->main);
    av_frame_free(&vpp->over_prev);
    av_frame_free(&vpp->over_next);
    ff_qsvvpp_free(&vpp->qsv);
    av_freep(&vpp->comp_conf.InputStream);
    av_freep(&vpp->qsv_param.ext_buf);
}

static int overlay_qsv_query_formats(AVFilterContext *ctx)
{
    int i;

    static const enum AVPixelFormat main_in_fmts[] = {
        AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_NV12,
        AV_PIX_FMT_YUYV422,
        AV_PIX_FMT_RGB32,
        AV_PIX_FMT_QSV,
        AV_PIX_FMT_NONE
    };
    static const enum AVPixelFormat out_pix_fmts[] = {
        AV_PIX_FMT_NV12,
        AV_PIX_FMT_QSV,
        AV_PIX_FMT_NONE
    };

    for (i = 0; i < ctx->nb_inputs; i++)
        ff_formats_ref(ff_make_format_list(main_in_fmts), &ctx->inputs[i]->out_formats);

    ff_formats_ref(ff_make_format_list(out_pix_fmts), &ctx->outputs[0]->in_formats);

    return 0;
}

static const AVClass overlay_qsv_class = {
    .class_name = "overlay_qsv",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVFilterPad overlay_qsv_inputs[] = {
    {
        .name          = "main",
        .type          = AVMEDIA_TYPE_VIDEO,
        .filter_frame  = filter_frame_main,
        .config_props  = config_main_input,
        .needs_fifo    = 1,
    },
    {
        .name          = "overlay",
        .type          = AVMEDIA_TYPE_VIDEO,
        .filter_frame  = filter_frame_overlay,
        .config_props  = config_overlay_input,
        .needs_fifo    = 1,
    },
    { NULL }
};

static const AVFilterPad overlay_qsv_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
        .request_frame = request_frame,
    },
    { NULL }
};

AVFilter ff_vf_overlay_qsv = {
    .name           = "overlay_qsv",
    .description    = NULL_IF_CONFIG_SMALL("Quick Sync Video overlay."),
    .priv_size      = sizeof(QSVOverlayContext),
    .query_formats  = overlay_qsv_query_formats,
    .init           = overlay_qsv_init,
    .uninit         = overlay_qsv_uninit,
    .inputs         = overlay_qsv_inputs,
    .outputs        = overlay_qsv_outputs,
    .priv_class     = &overlay_qsv_class,
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
