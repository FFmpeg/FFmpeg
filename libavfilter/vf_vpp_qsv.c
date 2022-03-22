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
 ** @file
 ** Hardware accelerated common filters based on Intel Quick Sync Video VPP
 **/

#include <float.h>

#include "libavutil/opt.h"
#include "libavutil/eval.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_qsv.h"
#include "libavutil/pixdesc.h"
#include "libavutil/mathematics.h"

#include "formats.h"
#include "internal.h"
#include "avfilter.h"
#include "filters.h"

#include "qsvvpp.h"
#include "transpose.h"

#define OFFSET(x) offsetof(VPPContext, x)
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM)

/* number of video enhancement filters */
#define ENH_FILTERS_COUNT (8)
#define QSV_HAVE_ROTATION       QSV_VERSION_ATLEAST(1, 17)
#define QSV_HAVE_MIRRORING      QSV_VERSION_ATLEAST(1, 19)
#define QSV_HAVE_SCALING_CONFIG QSV_VERSION_ATLEAST(1, 19)

typedef struct VPPContext{
    const AVClass *class;

    QSVVPPContext *qsv;

    /* Video Enhancement Algorithms */
    mfxExtVPPDeinterlacing  deinterlace_conf;
    mfxExtVPPFrameRateConversion frc_conf;
    mfxExtVPPDenoise denoise_conf;
    mfxExtVPPDetail detail_conf;
    mfxExtVPPProcAmp procamp_conf;
    mfxExtVPPRotation rotation_conf;
    mfxExtVPPMirroring mirroring_conf;
#ifdef QSV_HAVE_SCALING_CONFIG
    mfxExtVPPScaling scale_conf;
#endif

    int out_width;
    int out_height;
    /**
     * Output sw format. AV_PIX_FMT_NONE for no conversion.
     */
    enum AVPixelFormat out_format;

    AVRational framerate;       /* target framerate */
    int use_frc;                /* use framerate conversion */
    int deinterlace;            /* deinterlace mode : 0=off, 1=bob, 2=advanced */
    int denoise;                /* Enable Denoise algorithm. Value [0, 100] */
    int detail;                 /* Enable Detail Enhancement algorithm. */
                                /* Level is the optional, value [0, 100] */
    int use_crop;               /* 1 = use crop; 0=none */
    int crop_w;
    int crop_h;
    int crop_x;
    int crop_y;

    int transpose;
    int rotate;                 /* rotate angle : [0, 90, 180, 270] */
    int hflip;                  /* flip mode : 0 = off, 1 = HORIZONTAL flip */

    int scale_mode;             /* scale mode : 0 = auto, 1 = low power, 2 = high quality */

    /* param for the procamp */
    int    procamp;            /* enable procamp */
    float  hue;
    float  saturation;
    float  contrast;
    float  brightness;

    char *cx, *cy, *cw, *ch;
    char *ow, *oh;
    char *output_format_str;

    int async_depth;
    int eof;
} VPPContext;

static const AVOption options[] = {
    { "deinterlace", "deinterlace mode: 0=off, 1=bob, 2=advanced", OFFSET(deinterlace), AV_OPT_TYPE_INT,      { .i64 = 0 }, 0, MFX_DEINTERLACING_ADVANCED, .flags = FLAGS, "deinterlace" },
    { "bob",         "Bob deinterlace mode.",                      0,                   AV_OPT_TYPE_CONST,    { .i64 = MFX_DEINTERLACING_BOB },            .flags = FLAGS, "deinterlace" },
    { "advanced",    "Advanced deinterlace mode. ",                0,                   AV_OPT_TYPE_CONST,    { .i64 = MFX_DEINTERLACING_ADVANCED },       .flags = FLAGS, "deinterlace" },

    { "denoise",     "denoise level [0, 100]",       OFFSET(denoise),     AV_OPT_TYPE_INT,      { .i64 = 0 }, 0, 100, .flags = FLAGS },
    { "detail",      "enhancement level [0, 100]",   OFFSET(detail),      AV_OPT_TYPE_INT,      { .i64 = 0 }, 0, 100, .flags = FLAGS },
    { "framerate",   "output framerate",             OFFSET(framerate),   AV_OPT_TYPE_RATIONAL, { .dbl = 0.0 },0, DBL_MAX, .flags = FLAGS },
    { "procamp",     "Enable ProcAmp",               OFFSET(procamp),     AV_OPT_TYPE_INT,      { .i64 = 0 }, 0, 1, .flags = FLAGS},
    { "hue",         "ProcAmp hue",                  OFFSET(hue),         AV_OPT_TYPE_FLOAT,    { .dbl = 0.0 }, -180.0, 180.0, .flags = FLAGS},
    { "saturation",  "ProcAmp saturation",           OFFSET(saturation),  AV_OPT_TYPE_FLOAT,    { .dbl = 1.0 }, 0.0, 10.0, .flags = FLAGS},
    { "contrast",    "ProcAmp contrast",             OFFSET(contrast),    AV_OPT_TYPE_FLOAT,    { .dbl = 1.0 }, 0.0, 10.0, .flags = FLAGS},
    { "brightness",  "ProcAmp brightness",           OFFSET(brightness),  AV_OPT_TYPE_FLOAT,    { .dbl = 0.0 }, -100.0, 100.0, .flags = FLAGS},

    { "transpose",  "set transpose direction",       OFFSET(transpose),   AV_OPT_TYPE_INT,      { .i64 = -1 }, -1, 6, FLAGS, "transpose"},
        { "cclock_hflip",  "rotate counter-clockwise with horizontal flip",  0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_CCLOCK_FLIP }, .flags=FLAGS, .unit = "transpose" },
        { "clock",         "rotate clockwise",                               0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_CLOCK       }, .flags=FLAGS, .unit = "transpose" },
        { "cclock",        "rotate counter-clockwise",                       0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_CCLOCK      }, .flags=FLAGS, .unit = "transpose" },
        { "clock_hflip",   "rotate clockwise with horizontal flip",          0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_CLOCK_FLIP  }, .flags=FLAGS, .unit = "transpose" },
        { "reversal",      "rotate by half-turn",                            0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_REVERSAL    }, .flags=FLAGS, .unit = "transpose" },
        { "hflip",         "flip horizontally",                              0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_HFLIP       }, .flags=FLAGS, .unit = "transpose" },
        { "vflip",         "flip vertically",                                0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_VFLIP       }, .flags=FLAGS, .unit = "transpose" },

    { "cw",   "set the width crop area expression",   OFFSET(cw), AV_OPT_TYPE_STRING, { .str = "iw" }, 0, 0, FLAGS },
    { "ch",   "set the height crop area expression",  OFFSET(ch), AV_OPT_TYPE_STRING, { .str = "ih" }, 0, 0, FLAGS },
    { "cx",   "set the x crop area expression",       OFFSET(cx), AV_OPT_TYPE_STRING, { .str = "(in_w-out_w)/2" }, 0, 0, FLAGS },
    { "cy",   "set the y crop area expression",       OFFSET(cy), AV_OPT_TYPE_STRING, { .str = "(in_h-out_h)/2" }, 0, 0, FLAGS },

    { "w",      "Output video width",  OFFSET(ow), AV_OPT_TYPE_STRING, { .str="cw" }, 0, 255, .flags = FLAGS },
    { "width",  "Output video width",  OFFSET(ow), AV_OPT_TYPE_STRING, { .str="cw" }, 0, 255, .flags = FLAGS },
    { "h",      "Output video height", OFFSET(oh), AV_OPT_TYPE_STRING, { .str="w*ch/cw" }, 0, 255, .flags = FLAGS },
    { "height", "Output video height", OFFSET(oh), AV_OPT_TYPE_STRING, { .str="w*ch/cw" }, 0, 255, .flags = FLAGS },
    { "format", "Output pixel format", OFFSET(output_format_str), AV_OPT_TYPE_STRING, { .str = "same" }, .flags = FLAGS },
    { "async_depth", "Internal parallelization depth, the higher the value the higher the latency.", OFFSET(async_depth), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, .flags = FLAGS },
#ifdef QSV_HAVE_SCALING_CONFIG
    { "scale_mode", "scale mode: 0=auto, 1=low power, 2=high quality", OFFSET(scale_mode), AV_OPT_TYPE_INT, { .i64 = MFX_SCALING_MODE_DEFAULT }, MFX_SCALING_MODE_DEFAULT, MFX_SCALING_MODE_QUALITY, .flags = FLAGS, "scale mode" },
#endif
    { NULL }
};

static const char *const var_names[] = {
    "iw", "in_w",
    "ih", "in_h",
    "ow", "out_w", "w",
    "oh", "out_h", "h",
    "cw",
    "ch",
    "cx",
    "cy",
    NULL
};

enum var_name {
    VAR_iW, VAR_IN_W,
    VAR_iH, VAR_IN_H,
    VAR_oW, VAR_OUT_W, VAR_W,
    VAR_oH, VAR_OUT_H, VAR_H,
    CW,
    CH,
    CX,
    CY,
    VAR_VARS_NB
};

static int eval_expr(AVFilterContext *ctx)
{
#define PASS_EXPR(e, s) {\
    ret = av_expr_parse(&e, s, var_names, NULL, NULL, NULL, NULL, 0, ctx); \
    if (ret < 0) {\
        av_log(ctx, AV_LOG_ERROR, "Error when passing '%s'.\n", s);\
        goto release;\
    }\
}
#define CALC_EXPR(e, v, i) {\
    i = v = av_expr_eval(e, var_values, NULL); \
}
    VPPContext *vpp = ctx->priv;
    double  var_values[VAR_VARS_NB] = { NAN };
    AVExpr *w_expr  = NULL, *h_expr  = NULL;
    AVExpr *cw_expr = NULL, *ch_expr = NULL;
    AVExpr *cx_expr = NULL, *cy_expr = NULL;
    int     ret = 0;

    PASS_EXPR(cw_expr, vpp->cw);
    PASS_EXPR(ch_expr, vpp->ch);

    PASS_EXPR(w_expr, vpp->ow);
    PASS_EXPR(h_expr, vpp->oh);

    PASS_EXPR(cx_expr, vpp->cx);
    PASS_EXPR(cy_expr, vpp->cy);

    var_values[VAR_iW] =
    var_values[VAR_IN_W] = ctx->inputs[0]->w;

    var_values[VAR_iH] =
    var_values[VAR_IN_H] = ctx->inputs[0]->h;

    /* crop params */
    CALC_EXPR(cw_expr, var_values[CW], vpp->crop_w);
    CALC_EXPR(ch_expr, var_values[CH], vpp->crop_h);

    /* calc again in case cw is relative to ch */
    CALC_EXPR(cw_expr, var_values[CW], vpp->crop_w);

    CALC_EXPR(w_expr,
            var_values[VAR_OUT_W] = var_values[VAR_oW] = var_values[VAR_W],
            vpp->out_width);
    CALC_EXPR(h_expr,
            var_values[VAR_OUT_H] = var_values[VAR_oH] = var_values[VAR_H],
            vpp->out_height);

    /* calc again in case ow is relative to oh */
    CALC_EXPR(w_expr,
            var_values[VAR_OUT_W] = var_values[VAR_oW] = var_values[VAR_W],
            vpp->out_width);


    CALC_EXPR(cx_expr, var_values[CX], vpp->crop_x);
    CALC_EXPR(cy_expr, var_values[CY], vpp->crop_y);

    /* calc again in case cx is relative to cy */
    CALC_EXPR(cx_expr, var_values[CX], vpp->crop_x);

    if ((vpp->crop_w != var_values[VAR_iW]) || (vpp->crop_h != var_values[VAR_iH]))
        vpp->use_crop = 1;

release:
    av_expr_free(w_expr);
    av_expr_free(h_expr);
    av_expr_free(cw_expr);
    av_expr_free(ch_expr);
    av_expr_free(cx_expr);
    av_expr_free(cy_expr);
#undef PASS_EXPR
#undef CALC_EXPR

    return ret;
}

static av_cold int vpp_init(AVFilterContext *ctx)
{
    VPPContext  *vpp  = ctx->priv;

    if (!strcmp(vpp->output_format_str, "same")) {
        vpp->out_format = AV_PIX_FMT_NONE;
    } else {
        vpp->out_format = av_get_pix_fmt(vpp->output_format_str);
        if (vpp->out_format == AV_PIX_FMT_NONE) {
            av_log(ctx, AV_LOG_ERROR, "Unrecognized output pixel format: %s\n", vpp->output_format_str);
            return AVERROR(EINVAL);
        }
    }

    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    VPPContext      *vpp = ctx->priv;
    int              ret;

    if (vpp->framerate.den == 0 || vpp->framerate.num == 0)
        vpp->framerate = inlink->frame_rate;

    if (av_cmp_q(vpp->framerate, inlink->frame_rate))
        vpp->use_frc = 1;

    ret = eval_expr(ctx);
    if (ret != 0) {
        av_log(ctx, AV_LOG_ERROR, "Fail to eval expr.\n");
        return ret;
    }

    if (vpp->out_height == 0 || vpp->out_width == 0) {
        vpp->out_width  = inlink->w;
        vpp->out_height = inlink->h;
    }

    if (vpp->use_crop) {
        vpp->crop_x = FFMAX(vpp->crop_x, 0);
        vpp->crop_y = FFMAX(vpp->crop_y, 0);

        if(vpp->crop_w + vpp->crop_x > inlink->w)
           vpp->crop_x = inlink->w - vpp->crop_w;
        if(vpp->crop_h + vpp->crop_y > inlink->h)
           vpp->crop_y = inlink->h - vpp->crop_h;
    }

    return 0;
}

static mfxStatus get_mfx_version(const AVFilterContext *ctx, mfxVersion *mfx_version)
{
    const AVFilterLink *inlink = ctx->inputs[0];
    AVBufferRef *device_ref;
    AVHWDeviceContext *device_ctx;
    AVQSVDeviceContext *device_hwctx;

    if (inlink->hw_frames_ctx) {
        AVHWFramesContext *frames_ctx = (AVHWFramesContext *)inlink->hw_frames_ctx->data;
        device_ref = frames_ctx->device_ref;
    } else if (ctx->hw_device_ctx) {
        device_ref = ctx->hw_device_ctx;
    } else {
        // Unavailable hw context doesn't matter in pass-through mode,
        // so don't error here but let runtime version checks fail by setting to 0.0
        mfx_version->Major = 0;
        mfx_version->Minor = 0;
        return MFX_ERR_NONE;
    }

    device_ctx   = (AVHWDeviceContext *)device_ref->data;
    device_hwctx = device_ctx->hwctx;

    return MFXQueryVersion(device_hwctx->session, mfx_version);
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    VPPContext      *vpp = ctx->priv;
    QSVVPPParam     param = { NULL };
    QSVVPPCrop      crop  = { 0 };
    mfxExtBuffer    *ext_buf[ENH_FILTERS_COUNT];
    mfxVersion      mfx_version;
    AVFilterLink    *inlink = ctx->inputs[0];
    enum AVPixelFormat in_format;

    outlink->w          = vpp->out_width;
    outlink->h          = vpp->out_height;
    outlink->frame_rate = vpp->framerate;
    outlink->time_base  = inlink->time_base;

    param.filter_frame  = NULL;
    param.num_ext_buf   = 0;
    param.ext_buf       = ext_buf;
    param.async_depth   = vpp->async_depth;

    if (get_mfx_version(ctx, &mfx_version) != MFX_ERR_NONE) {
        av_log(ctx, AV_LOG_ERROR, "Failed to query mfx version.\n");
        return AVERROR(EINVAL);
    }

    if (inlink->format == AV_PIX_FMT_QSV) {
         if (!inlink->hw_frames_ctx || !inlink->hw_frames_ctx->data)
             return AVERROR(EINVAL);
         else
             in_format = ((AVHWFramesContext*)inlink->hw_frames_ctx->data)->sw_format;
    } else
        in_format = inlink->format;

    if (vpp->out_format == AV_PIX_FMT_NONE)
        vpp->out_format = in_format;
    param.out_sw_format  = vpp->out_format;

    if (vpp->use_crop) {
        crop.in_idx = 0;
        crop.x = vpp->crop_x;
        crop.y = vpp->crop_y;
        crop.w = vpp->crop_w;
        crop.h = vpp->crop_h;

        param.num_crop = 1;
        param.crop     = &crop;
    }

    if (vpp->deinterlace) {
        memset(&vpp->deinterlace_conf, 0, sizeof(mfxExtVPPDeinterlacing));
        vpp->deinterlace_conf.Header.BufferId = MFX_EXTBUFF_VPP_DEINTERLACING;
        vpp->deinterlace_conf.Header.BufferSz = sizeof(mfxExtVPPDeinterlacing);
        vpp->deinterlace_conf.Mode = vpp->deinterlace == 1 ?
                                     MFX_DEINTERLACING_BOB : MFX_DEINTERLACING_ADVANCED;

        param.ext_buf[param.num_ext_buf++] = (mfxExtBuffer*)&vpp->deinterlace_conf;
    }

    if (vpp->use_frc) {
        memset(&vpp->frc_conf, 0, sizeof(mfxExtVPPFrameRateConversion));
        vpp->frc_conf.Header.BufferId = MFX_EXTBUFF_VPP_FRAME_RATE_CONVERSION;
        vpp->frc_conf.Header.BufferSz = sizeof(mfxExtVPPFrameRateConversion);
        vpp->frc_conf.Algorithm = MFX_FRCALGM_DISTRIBUTED_TIMESTAMP;

        param.ext_buf[param.num_ext_buf++] = (mfxExtBuffer*)&vpp->frc_conf;
    }

    if (vpp->denoise) {
        memset(&vpp->denoise_conf, 0, sizeof(mfxExtVPPDenoise));
        vpp->denoise_conf.Header.BufferId = MFX_EXTBUFF_VPP_DENOISE;
        vpp->denoise_conf.Header.BufferSz = sizeof(mfxExtVPPDenoise);
        vpp->denoise_conf.DenoiseFactor   = vpp->denoise;

        param.ext_buf[param.num_ext_buf++] = (mfxExtBuffer*)&vpp->denoise_conf;
    }

    if (vpp->detail) {
        memset(&vpp->detail_conf, 0, sizeof(mfxExtVPPDetail));
        vpp->detail_conf.Header.BufferId  = MFX_EXTBUFF_VPP_DETAIL;
        vpp->detail_conf.Header.BufferSz  = sizeof(mfxExtVPPDetail);
        vpp->detail_conf.DetailFactor = vpp->detail;

        param.ext_buf[param.num_ext_buf++] = (mfxExtBuffer*)&vpp->detail_conf;
    }

    if (vpp->procamp) {
        memset(&vpp->procamp_conf, 0, sizeof(mfxExtVPPProcAmp));
        vpp->procamp_conf.Header.BufferId  = MFX_EXTBUFF_VPP_PROCAMP;
        vpp->procamp_conf.Header.BufferSz  = sizeof(mfxExtVPPProcAmp);
        vpp->procamp_conf.Hue              = vpp->hue;
        vpp->procamp_conf.Saturation       = vpp->saturation;
        vpp->procamp_conf.Contrast         = vpp->contrast;
        vpp->procamp_conf.Brightness       = vpp->brightness;

        param.ext_buf[param.num_ext_buf++] = (mfxExtBuffer*)&vpp->procamp_conf;
    }

    if (vpp->transpose >= 0) {
#ifdef QSV_HAVE_ROTATION
        switch (vpp->transpose) {
        case TRANSPOSE_CCLOCK_FLIP:
            vpp->rotate = MFX_ANGLE_270;
            vpp->hflip  = MFX_MIRRORING_HORIZONTAL;
            break;
        case TRANSPOSE_CLOCK:
            vpp->rotate = MFX_ANGLE_90;
            vpp->hflip  = MFX_MIRRORING_DISABLED;
            break;
        case TRANSPOSE_CCLOCK:
            vpp->rotate = MFX_ANGLE_270;
            vpp->hflip  = MFX_MIRRORING_DISABLED;
            break;
        case TRANSPOSE_CLOCK_FLIP:
            vpp->rotate = MFX_ANGLE_90;
            vpp->hflip  = MFX_MIRRORING_HORIZONTAL;
            break;
        case TRANSPOSE_REVERSAL:
            vpp->rotate = MFX_ANGLE_180;
            vpp->hflip  = MFX_MIRRORING_DISABLED;
            break;
        case TRANSPOSE_HFLIP:
            vpp->rotate = MFX_ANGLE_0;
            vpp->hflip  = MFX_MIRRORING_HORIZONTAL;
            break;
        case TRANSPOSE_VFLIP:
            vpp->rotate = MFX_ANGLE_180;
            vpp->hflip  = MFX_MIRRORING_HORIZONTAL;
            break;
        default:
            av_log(ctx, AV_LOG_ERROR, "Failed to set transpose mode to %d.\n", vpp->transpose);
            return AVERROR(EINVAL);
        }
#else
        av_log(ctx, AV_LOG_WARNING, "The QSV VPP transpose option is "
            "not supported with this MSDK version.\n");
        vpp->transpose = 0;
#endif
    }

    if (vpp->rotate) {
#ifdef QSV_HAVE_ROTATION
        memset(&vpp->rotation_conf, 0, sizeof(mfxExtVPPRotation));
        vpp->rotation_conf.Header.BufferId  = MFX_EXTBUFF_VPP_ROTATION;
        vpp->rotation_conf.Header.BufferSz  = sizeof(mfxExtVPPRotation);
        vpp->rotation_conf.Angle = vpp->rotate;

        if (MFX_ANGLE_90 == vpp->rotate || MFX_ANGLE_270 == vpp->rotate) {
            FFSWAP(int, vpp->out_width, vpp->out_height);
            FFSWAP(int, outlink->w, outlink->h);
            av_log(ctx, AV_LOG_DEBUG, "Swap width and height for clock/cclock rotation.\n");
        }

        param.ext_buf[param.num_ext_buf++] = (mfxExtBuffer*)&vpp->rotation_conf;
#else
        av_log(ctx, AV_LOG_WARNING, "The QSV VPP rotate option is "
            "not supported with this MSDK version.\n");
        vpp->rotate = 0;
#endif
    }

    if (vpp->hflip) {
#ifdef QSV_HAVE_MIRRORING
        memset(&vpp->mirroring_conf, 0, sizeof(mfxExtVPPMirroring));
        vpp->mirroring_conf.Header.BufferId = MFX_EXTBUFF_VPP_MIRRORING;
        vpp->mirroring_conf.Header.BufferSz = sizeof(mfxExtVPPMirroring);
        vpp->mirroring_conf.Type = vpp->hflip;

        param.ext_buf[param.num_ext_buf++] = (mfxExtBuffer*)&vpp->mirroring_conf;
#else
        av_log(ctx, AV_LOG_WARNING, "The QSV VPP hflip option is "
            "not supported with this MSDK version.\n");
        vpp->hflip = 0;
#endif
    }

#ifdef QSV_HAVE_SCALING_CONFIG
    if (inlink->w != outlink->w || inlink->h != outlink->h) {
        if (QSV_RUNTIME_VERSION_ATLEAST(mfx_version, 1, 19)) {
            memset(&vpp->scale_conf, 0, sizeof(mfxExtVPPScaling));
            vpp->scale_conf.Header.BufferId    = MFX_EXTBUFF_VPP_SCALING;
            vpp->scale_conf.Header.BufferSz    = sizeof(mfxExtVPPScaling);
            vpp->scale_conf.ScalingMode        = vpp->scale_mode;

            param.ext_buf[param.num_ext_buf++] = (mfxExtBuffer*)&vpp->scale_conf;
        } else
            av_log(ctx, AV_LOG_WARNING, "The QSV VPP Scale option is "
                "not supported with this MSDK version.\n");
    }
#endif

    if (vpp->use_frc || vpp->use_crop || vpp->deinterlace || vpp->denoise ||
        vpp->detail || vpp->procamp || vpp->rotate || vpp->hflip ||
        inlink->w != outlink->w || inlink->h != outlink->h || in_format != vpp->out_format)
        return ff_qsvvpp_create(ctx, &vpp->qsv, &param);
    else {
        av_log(ctx, AV_LOG_VERBOSE, "qsv vpp pass through mode.\n");
        if (inlink->hw_frames_ctx)
            outlink->hw_frames_ctx = av_buffer_ref(inlink->hw_frames_ctx);
    }

    return 0;
}

static int activate(AVFilterContext *ctx)
{
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    VPPContext *s =ctx->priv;
    QSVVPPContext *qsv = s->qsv;
    AVFrame *in = NULL;
    int ret, status = 0;
    int64_t pts = AV_NOPTS_VALUE;

    FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink);

    if (!s->eof) {
        ret = ff_inlink_consume_frame(inlink, &in);
        if (ret < 0)
            return ret;

        if (ff_inlink_acknowledge_status(inlink, &status, &pts)) {
            if (status == AVERROR_EOF) {
                s->eof = 1;
            }
        }
    }

    if (qsv) {
        if (in || s->eof) {
            qsv->eof = s->eof;
            ret = ff_qsvvpp_filter_frame(qsv, inlink, in);
            av_frame_free(&in);

            if (s->eof) {
                ff_outlink_set_status(outlink, status, pts);
                return 0;
            }

            if (qsv->got_frame) {
                qsv->got_frame = 0;
                return ret;
            }
        }
    } else {
        if (in) {
            if (in->pts != AV_NOPTS_VALUE)
                in->pts = av_rescale_q(in->pts, inlink->time_base, outlink->time_base);

            ret = ff_filter_frame(outlink, in);
            return ret;
        }
    }

    if (s->eof) {
        ff_outlink_set_status(outlink, status, pts);
        return 0;
    } else {
        FF_FILTER_FORWARD_WANTED(outlink, inlink);
    }

    return FFERROR_NOT_READY;
}

static int query_formats(AVFilterContext *ctx)
{
    int ret;
    static const enum AVPixelFormat in_pix_fmts[] = {
        AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_NV12,
        AV_PIX_FMT_YUYV422,
        AV_PIX_FMT_RGB32,
        AV_PIX_FMT_QSV,
        AV_PIX_FMT_NONE
    };
    static const enum AVPixelFormat out_pix_fmts[] = {
        AV_PIX_FMT_NV12,
        AV_PIX_FMT_P010,
        AV_PIX_FMT_QSV,
        AV_PIX_FMT_NONE
    };

    ret = ff_formats_ref(ff_make_format_list(in_pix_fmts),
                         &ctx->inputs[0]->outcfg.formats);
    if (ret < 0)
        return ret;
    return ff_formats_ref(ff_make_format_list(out_pix_fmts),
                          &ctx->outputs[0]->incfg.formats);
}

static av_cold void vpp_uninit(AVFilterContext *ctx)
{
    VPPContext *vpp = ctx->priv;

    ff_qsvvpp_free(&vpp->qsv);
}

static const AVClass vpp_class = {
    .class_name = "vpp_qsv",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVFilterPad vpp_inputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_input,
    },
};

static const AVFilterPad vpp_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
};

const AVFilter ff_vf_vpp_qsv = {
    .name          = "vpp_qsv",
    .description   = NULL_IF_CONFIG_SMALL("Quick Sync Video VPP."),
    .priv_size     = sizeof(VPPContext),
    .init          = vpp_init,
    .uninit        = vpp_uninit,
    FILTER_INPUTS(vpp_inputs),
    FILTER_OUTPUTS(vpp_outputs),
    FILTER_QUERY_FUNC(query_formats),
    .activate      = activate,
    .priv_class    = &vpp_class,
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
