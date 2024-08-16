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

#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/common.h"
#include "libavutil/pixdesc.h"
#include "libavutil/eval.h"
#include "libavutil/hwcontext.h"
#include "libavutil/mathematics.h"

#include "filters.h"
#include "avfilter.h"
#include "formats.h"

#include "framesync.h"
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

typedef struct QSVOverlayContext {
    QSVVPPContext      qsv;

    FFFrameSync fs;
    QSVVPPParam        qsv_param;
    mfxExtVPPComposite comp_conf;
    double             var_values[VAR_VARS_NB];

    char     *overlay_ox, *overlay_oy, *overlay_ow, *overlay_oh;
    uint16_t  overlay_alpha, overlay_pixel_alpha;

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

static const AVOption overlay_qsv_options[] = {
    { "x", "Overlay x position", OFFSET(overlay_ox), AV_OPT_TYPE_STRING, { .str="0"}, 0, 255, .flags = FLAGS},
    { "y", "Overlay y position", OFFSET(overlay_oy), AV_OPT_TYPE_STRING, { .str="0"}, 0, 255, .flags = FLAGS},
    { "w", "Overlay width",      OFFSET(overlay_ow), AV_OPT_TYPE_STRING, { .str="overlay_iw"}, 0, 255, .flags = FLAGS},
    { "h", "Overlay height",     OFFSET(overlay_oh), AV_OPT_TYPE_STRING, { .str="overlay_ih*w/overlay_iw"}, 0, 255, .flags = FLAGS},
    { "alpha", "Overlay global alpha", OFFSET(overlay_alpha), AV_OPT_TYPE_INT, { .i64 = 255}, 0, 255, .flags = FLAGS},
    { "eof_action", "Action to take when encountering EOF from secondary input ",
        OFFSET(fs.opt_eof_action), AV_OPT_TYPE_INT, { .i64 = EOF_ACTION_REPEAT },
        EOF_ACTION_REPEAT, EOF_ACTION_PASS, .flags = FLAGS, .unit = "eof_action" },
        { "repeat", "Repeat the previous frame.",   0, AV_OPT_TYPE_CONST, { .i64 = EOF_ACTION_REPEAT }, .flags = FLAGS, .unit = "eof_action" },
        { "endall", "End both streams.",            0, AV_OPT_TYPE_CONST, { .i64 = EOF_ACTION_ENDALL }, .flags = FLAGS, .unit = "eof_action" },
        { "pass",   "Pass through the main input.", 0, AV_OPT_TYPE_CONST, { .i64 = EOF_ACTION_PASS },   .flags = FLAGS, .unit = "eof_action" },
    { "shortest", "force termination when the shortest input terminates", OFFSET(fs.opt_shortest), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, FLAGS },
    { "repeatlast", "repeat overlay of the last overlay frame", OFFSET(fs.opt_repeatlast), AV_OPT_TYPE_BOOL, {.i64=1}, 0, 1, FLAGS },
    { NULL }
};

FRAMESYNC_DEFINE_CLASS(overlay_qsv, QSVOverlayContext, fs);

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
    FilterLink              *l = ff_filter_link(link);
    enum AVPixelFormat pix_fmt = link->format;
    const AVPixFmtDescriptor *desc;
    AVHWFramesContext *fctx;

    if (link->format == AV_PIX_FMT_QSV) {
        fctx    = (AVHWFramesContext *)l->hw_frames_ctx->data;
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

static int process_frame(FFFrameSync *fs)
{
    AVFilterContext  *ctx = fs->parent;
    QSVVPPContext    *qsv = fs->opaque;
    AVFrame        *frame = NULL, *propref = NULL;
    int               ret = 0, i;

    for (i = 0; i < ctx->nb_inputs; i++) {
        ret = ff_framesync_get_frame(fs, i, &frame, 0);
        if (ret == 0) {
            if (i == 0)
                propref = frame;
            ret = ff_qsvvpp_filter_frame(qsv, ctx->inputs[i], frame, propref);
        }
        if (ret < 0 && ret != AVERROR(EAGAIN))
            break;
    }

    return ret;
}

static int init_framesync(AVFilterContext *ctx)
{
    QSVOverlayContext *s = ctx->priv;
    int ret, i;

    s->fs.on_event = process_frame;
    s->fs.opaque   = s;
    ret = ff_framesync_init(&s->fs, ctx, ctx->nb_inputs);
    if (ret < 0)
        return ret;

    for (i = 0; i < ctx->nb_inputs; i++) {
        FFFrameSyncIn *in = &s->fs.in[i];
        in->before    = EXT_STOP;
        in->after     = EXT_INFINITY;
        in->sync      = i ? 1 : 2;
        in->time_base = ctx->inputs[i]->time_base;
    }

    return ff_framesync_configure(&s->fs);
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext   *ctx = outlink->src;
    QSVOverlayContext *vpp = ctx->priv;
    AVFilterLink      *in0 = ctx->inputs[0];
    AVFilterLink      *in1 = ctx->inputs[1];
    FilterLink         *l0 = ff_filter_link(in0);
    FilterLink         *l1 = ff_filter_link(in1);
    FilterLink         *ol = ff_filter_link(outlink);
    int ret;

    av_log(ctx, AV_LOG_DEBUG, "Output is of %s.\n", av_get_pix_fmt_name(outlink->format));
    vpp->qsv_param.out_sw_format = in0->format;
    if ((in0->format == AV_PIX_FMT_QSV && in1->format != AV_PIX_FMT_QSV) ||
        (in0->format != AV_PIX_FMT_QSV && in1->format == AV_PIX_FMT_QSV)) {
        av_log(ctx, AV_LOG_ERROR, "Mixing hardware and software pixel formats is not supported.\n");
        return AVERROR(EINVAL);
    } else if (in0->format == AV_PIX_FMT_QSV) {
        AVHWFramesContext *hw_frame0 = (AVHWFramesContext *)l0->hw_frames_ctx->data;
        AVHWFramesContext *hw_frame1 = (AVHWFramesContext *)l1->hw_frames_ctx->data;

        if (hw_frame0->device_ctx != hw_frame1->device_ctx) {
            av_log(ctx, AV_LOG_ERROR, "Inputs with different underlying QSV devices are forbidden.\n");
            return AVERROR(EINVAL);
        }
        vpp->qsv_param.out_sw_format = hw_frame0->sw_format;
    }

    outlink->w          = vpp->var_values[VAR_MW];
    outlink->h          = vpp->var_values[VAR_MH];
    ol->frame_rate      = l0->frame_rate;
    outlink->time_base  = av_inv_q(ol->frame_rate);

    ret = init_framesync(ctx);
    if (ret < 0)
        return ret;

    return ff_qsvvpp_init(ctx, &vpp->qsv_param);
}

/*
 * Callback for qsvvpp
 * @Note: qsvvpp composition does not generate PTS for result frame.
 *        so we assign the PTS from framesync to the output frame.
 */

static int filter_callback(AVFilterLink *outlink, AVFrame *frame)
{
    QSVOverlayContext *s = outlink->src->priv;
    frame->pts = av_rescale_q(s->fs.pts,
                              s->fs.time_base, outlink->time_base);
    return ff_filter_frame(outlink, frame);
}


static int overlay_qsv_init(AVFilterContext *ctx)
{
    QSVOverlayContext *vpp = ctx->priv;

    /* fill composite config */
    vpp->comp_conf.Header.BufferId = MFX_EXTBUFF_VPP_COMPOSITE;
    vpp->comp_conf.Header.BufferSz = sizeof(vpp->comp_conf);
    vpp->comp_conf.NumInputStream  = ctx->nb_inputs;
    vpp->comp_conf.InputStream     = av_calloc(ctx->nb_inputs,
                                               sizeof(*vpp->comp_conf.InputStream));
    if (!vpp->comp_conf.InputStream)
        return AVERROR(ENOMEM);

    /* initialize QSVVPP params */
    vpp->qsv_param.filter_frame = filter_callback;
    vpp->qsv_param.ext_buf      = av_mallocz(sizeof(*vpp->qsv_param.ext_buf));
    if (!vpp->qsv_param.ext_buf)
        return AVERROR(ENOMEM);

    vpp->qsv_param.ext_buf[0]    = (mfxExtBuffer *)&vpp->comp_conf;
    vpp->qsv_param.num_ext_buf   = 1;
    vpp->qsv_param.out_sw_format = AV_PIX_FMT_NV12;
    vpp->qsv_param.num_crop      = 0;

    return 0;
}

static av_cold void overlay_qsv_uninit(AVFilterContext *ctx)
{
    QSVOverlayContext *vpp = ctx->priv;

    ff_qsvvpp_close(ctx);
    ff_framesync_uninit(&vpp->fs);
    av_freep(&vpp->comp_conf.InputStream);
    av_freep(&vpp->qsv_param.ext_buf);
}

static int activate(AVFilterContext *ctx)
{
    QSVOverlayContext *s = ctx->priv;
    return ff_framesync_activate(&s->fs);
}

static int overlay_qsv_query_formats(AVFilterContext *ctx)
{
    int i;
    int ret;

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

    for (i = 0; i < ctx->nb_inputs; i++) {
        ret = ff_formats_ref(ff_make_format_list(main_in_fmts), &ctx->inputs[i]->outcfg.formats);
        if (ret < 0)
            return ret;
    }

    ret = ff_formats_ref(ff_make_format_list(out_pix_fmts), &ctx->outputs[0]->incfg.formats);
    if (ret < 0)
        return ret;

    return 0;
}

static const AVFilterPad overlay_qsv_inputs[] = {
    {
        .name          = "main",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_main_input,
        .get_buffer.video = ff_qsvvpp_get_video_buffer,
    },
    {
        .name          = "overlay",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_overlay_input,
        .get_buffer.video = ff_qsvvpp_get_video_buffer,
    },
};

static const AVFilterPad overlay_qsv_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
};

const AVFilter ff_vf_overlay_qsv = {
    .name           = "overlay_qsv",
    .description    = NULL_IF_CONFIG_SMALL("Quick Sync Video overlay."),
    .priv_size      = sizeof(QSVOverlayContext),
    .preinit        = overlay_qsv_framesync_preinit,
    .init           = overlay_qsv_init,
    .uninit         = overlay_qsv_uninit,
    .activate       = activate,
    FILTER_INPUTS(overlay_qsv_inputs),
    FILTER_OUTPUTS(overlay_qsv_outputs),
    FILTER_QUERY_FUNC(overlay_qsv_query_formats),
    .priv_class     = &overlay_qsv_class,
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
    .flags          = AVFILTER_FLAG_HWDEVICE,
};
