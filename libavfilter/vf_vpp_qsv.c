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

#include "config_components.h"

#include "libavutil/opt.h"
#include "libavutil/eval.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_qsv.h"
#include "libavutil/pixdesc.h"
#include "libavutil/mathematics.h"
#include "libavutil/mastering_display_metadata.h"

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

typedef struct VPPContext{
    QSVVPPContext qsv;

    /* Video Enhancement Algorithms */
    mfxExtVPPDeinterlacing  deinterlace_conf;
    mfxExtVPPFrameRateConversion frc_conf;
    mfxExtVPPDenoise denoise_conf;
    mfxExtVPPDetail detail_conf;
    mfxExtVPPProcAmp procamp_conf;
    mfxExtVPPRotation rotation_conf;
    mfxExtVPPMirroring mirroring_conf;
    mfxExtVPPScaling scale_conf;
#if QSV_ONEVPL
    /** Video signal info attached on the input frame */
    mfxExtVideoSignalInfo invsi_conf;
    /** Video signal info attached on the output frame */
    mfxExtVideoSignalInfo outvsi_conf;
    /** HDR parameters attached on the input frame */
    mfxExtMasteringDisplayColourVolume mdcv_conf;
    mfxExtContentLightLevelInfo clli_conf;
#endif

    /**
     * New dimensions. Special values are:
     *   0 = original width/height
     *  -1 = keep original aspect
     */
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

    /** The color properties for output */
    char *color_primaries_str;
    char *color_transfer_str;
    char *color_matrix_str;

    int color_range;
    enum AVColorPrimaries color_primaries;
    enum AVColorTransferCharacteristic color_transfer;
    enum AVColorSpace color_matrix;

    int has_passthrough;        /* apply pass through mode if possible */
    int field_rate;             /* Generate output at frame rate or field rate for deinterlace mode, 0: frame, 1: field */
    int tonemap;                /* 1: perform tonemapping if the input has HDR metadata, 0: always disable tonemapping */
} VPPContext;

static const char *const var_names[] = {
    "iw", "in_w",
    "ih", "in_h",
    "ow", "out_w", "w",
    "oh", "out_h", "h",
    "cw",
    "ch",
    "cx",
    "cy",
    "a", "dar",
    "sar",
    NULL
};

enum var_name {
    VAR_IW, VAR_IN_W,
    VAR_IH, VAR_IN_H,
    VAR_OW, VAR_OUT_W, VAR_W,
    VAR_OH, VAR_OUT_H, VAR_H,
    VAR_CW,
    VAR_CH,
    VAR_CX,
    VAR_CY,
    VAR_A, VAR_DAR,
    VAR_SAR,
    VAR_VARS_NB
};

static int eval_expr(AVFilterContext *ctx)
{
#define PASS_EXPR(e, s) {\
    if (s) {\
        ret = av_expr_parse(&e, s, var_names, NULL, NULL, NULL, NULL, 0, ctx); \
        if (ret < 0) {                                                  \
            av_log(ctx, AV_LOG_ERROR, "Error when passing '%s'.\n", s); \
            goto release;                                               \
        }                                                               \
    }\
}
#define CALC_EXPR(e, v, i, d) {\
    if (e)\
        i = v = av_expr_eval(e, var_values, NULL);      \
    else\
        i = v = d;\
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

    var_values[VAR_IW] =
    var_values[VAR_IN_W] = ctx->inputs[0]->w;

    var_values[VAR_IH] =
    var_values[VAR_IN_H] = ctx->inputs[0]->h;

    var_values[VAR_A] = (double)var_values[VAR_IN_W] / var_values[VAR_IN_H];
    var_values[VAR_SAR] = ctx->inputs[0]->sample_aspect_ratio.num ?
        (double)ctx->inputs[0]->sample_aspect_ratio.num / ctx->inputs[0]->sample_aspect_ratio.den : 1;
    var_values[VAR_DAR] = var_values[VAR_A] * var_values[VAR_SAR];

    /* crop params */
    CALC_EXPR(cw_expr, var_values[VAR_CW], vpp->crop_w, var_values[VAR_IW]);
    CALC_EXPR(ch_expr, var_values[VAR_CH], vpp->crop_h, var_values[VAR_IH]);

    /* calc again in case cw is relative to ch */
    CALC_EXPR(cw_expr, var_values[VAR_CW], vpp->crop_w, var_values[VAR_IW]);

    CALC_EXPR(w_expr,
            var_values[VAR_OUT_W] = var_values[VAR_OW] = var_values[VAR_W],
            vpp->out_width, var_values[VAR_CW]);
    CALC_EXPR(h_expr,
            var_values[VAR_OUT_H] = var_values[VAR_OH] = var_values[VAR_H],
            vpp->out_height, var_values[VAR_CH]);

    /* calc again in case ow is relative to oh */
    CALC_EXPR(w_expr,
            var_values[VAR_OUT_W] = var_values[VAR_OW] = var_values[VAR_W],
            vpp->out_width, var_values[VAR_CW]);

    CALC_EXPR(cx_expr, var_values[VAR_CX], vpp->crop_x, (var_values[VAR_IW] - var_values[VAR_OW]) / 2);
    CALC_EXPR(cy_expr, var_values[VAR_CY], vpp->crop_y, (var_values[VAR_IH] - var_values[VAR_OH]) / 2);

    /* calc again in case cx is relative to cy */
    CALC_EXPR(cx_expr, var_values[VAR_CX], vpp->crop_x, (var_values[VAR_IW] - var_values[VAR_OW]) / 2);

    if ((vpp->crop_w != var_values[VAR_IW]) || (vpp->crop_h != var_values[VAR_IH]))
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

static av_cold int vpp_preinit(AVFilterContext *ctx)
{
    VPPContext  *vpp  = ctx->priv;
    /* For AV_OPT_TYPE_STRING options, NULL is handled in other way so
     * we needn't set default value here
     */
    vpp->saturation = 1.0;
    vpp->contrast = 1.0;
    vpp->transpose = -1;

    vpp->color_range = AVCOL_RANGE_UNSPECIFIED;
    vpp->color_primaries = AVCOL_PRI_UNSPECIFIED;
    vpp->color_transfer = AVCOL_TRC_UNSPECIFIED;
    vpp->color_matrix = AVCOL_SPC_UNSPECIFIED;

    vpp->has_passthrough = 1;

    return 0;
}

static av_cold int vpp_init(AVFilterContext *ctx)
{
    VPPContext  *vpp  = ctx->priv;

    if (!vpp->output_format_str || !strcmp(vpp->output_format_str, "same")) {
        vpp->out_format = AV_PIX_FMT_NONE;
    } else {
        vpp->out_format = av_get_pix_fmt(vpp->output_format_str);
        if (vpp->out_format == AV_PIX_FMT_NONE) {
            av_log(ctx, AV_LOG_ERROR, "Unrecognized output pixel format: %s\n", vpp->output_format_str);
            return AVERROR(EINVAL);
        }
    }

#define STRING_OPTION(var_name, func_name, default_value) do {          \
        if (vpp->var_name ## _str) {                                    \
            int var = av_ ## func_name ## _from_name(vpp->var_name ## _str); \
            if (var < 0) {                                              \
                av_log(ctx, AV_LOG_ERROR, "Invalid %s.\n", #var_name);  \
                return AVERROR(EINVAL);                                 \
            }                                                           \
            vpp->var_name = var;                                        \
        } else {                                                        \
            vpp->var_name = default_value;                              \
        }                                                               \
    } while (0)

    STRING_OPTION(color_primaries, color_primaries, AVCOL_PRI_UNSPECIFIED);
    STRING_OPTION(color_transfer,  color_transfer,  AVCOL_TRC_UNSPECIFIED);
    STRING_OPTION(color_matrix,    color_space,     AVCOL_SPC_UNSPECIFIED);

#undef STRING_OPTION
    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    VPPContext      *vpp = ctx->priv;
    int              ret;
    int64_t          ow, oh;

    if (vpp->framerate.den == 0 || vpp->framerate.num == 0) {
        vpp->framerate = inlink->frame_rate;

        if (vpp->deinterlace && vpp->field_rate)
            vpp->framerate = av_mul_q(inlink->frame_rate,
                                      (AVRational){ 2, 1 });
    }

    if (av_cmp_q(vpp->framerate, inlink->frame_rate))
        vpp->use_frc = 1;

    ret = eval_expr(ctx);
    if (ret != 0) {
        av_log(ctx, AV_LOG_ERROR, "Fail to eval expr.\n");
        return ret;
    }

    ow = vpp->out_width;
    oh = vpp->out_height;

    /* sanity check params */
    if (ow <  -1 || oh <  -1) {
        av_log(ctx, AV_LOG_ERROR, "Size values less than -1 are not acceptable.\n");
        return AVERROR(EINVAL);
    }

    if (ow == -1 && oh == -1)
        vpp->out_width = vpp->out_height = 0;

    if (!(ow = vpp->out_width))
        ow = inlink->w;

    if (!(oh = vpp->out_height))
        oh = inlink->h;

    if (ow == -1)
        ow = av_rescale(oh, inlink->w, inlink->h);

    if (oh == -1)
        oh = av_rescale(ow, inlink->h, inlink->w);

    if (ow > INT_MAX || oh > INT_MAX ||
        (oh * inlink->w) > INT_MAX  ||
        (ow * inlink->h) > INT_MAX)
        av_log(ctx, AV_LOG_ERROR, "Rescaled value for width or height is too big.\n");

    vpp->out_width = ow;
    vpp->out_height = oh;

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

static int vpp_set_frame_ext_params(AVFilterContext *ctx, const AVFrame *in, AVFrame *out,  QSVVPPFrameParam *fp)
{
#if QSV_ONEVPL
    VPPContext *vpp = ctx->priv;
    QSVVPPContext *qsvvpp = &vpp->qsv;
    mfxExtVideoSignalInfo invsi_conf, outvsi_conf;
    mfxExtMasteringDisplayColourVolume mdcv_conf;
    mfxExtContentLightLevelInfo clli_conf;
    AVFrameSideData *sd;
    int tm = 0;

    fp->num_ext_buf = 0;

    if (!in || !out ||
        !QSV_RUNTIME_VERSION_ATLEAST(qsvvpp->ver, 2, 0))
        return 0;

    memset(&invsi_conf, 0, sizeof(mfxExtVideoSignalInfo));
    invsi_conf.Header.BufferId          = MFX_EXTBUFF_VIDEO_SIGNAL_INFO_IN;
    invsi_conf.Header.BufferSz          = sizeof(mfxExtVideoSignalInfo);
    invsi_conf.VideoFullRange           = (in->color_range == AVCOL_RANGE_JPEG);
    invsi_conf.ColourPrimaries          = (in->color_primaries == AVCOL_PRI_UNSPECIFIED) ? AVCOL_PRI_BT709 : in->color_primaries;
    invsi_conf.TransferCharacteristics  = (in->color_trc == AVCOL_TRC_UNSPECIFIED) ? AVCOL_TRC_BT709 : in->color_trc;
    invsi_conf.MatrixCoefficients       = (in->colorspace == AVCOL_SPC_UNSPECIFIED) ? AVCOL_SPC_BT709 : in->colorspace;
    invsi_conf.ColourDescriptionPresent = 1;

    memset(&mdcv_conf, 0, sizeof(mfxExtMasteringDisplayColourVolume));
    sd = av_frame_get_side_data(in, AV_FRAME_DATA_MASTERING_DISPLAY_METADATA);
    if (vpp->tonemap && sd) {
        AVMasteringDisplayMetadata *mdm = (AVMasteringDisplayMetadata *)sd->data;

        if (mdm->has_primaries && mdm->has_luminance) {
            const int mapping[3] = {1, 2, 0};
            const int chroma_den = 50000;
            const int luma_den   = 10000;
            int i;

            mdcv_conf.Header.BufferId         = MFX_EXTBUFF_MASTERING_DISPLAY_COLOUR_VOLUME_IN;
            mdcv_conf.Header.BufferSz         = sizeof(mfxExtMasteringDisplayColourVolume);

            for (i = 0; i < 3; i++) {
                const int j = mapping[i];

                mdcv_conf.DisplayPrimariesX[i] =
                    FFMIN(lrint(chroma_den *
                                av_q2d(mdm->display_primaries[j][0])),
                          chroma_den);
                mdcv_conf.DisplayPrimariesY[i] =
                    FFMIN(lrint(chroma_den *
                                av_q2d(mdm->display_primaries[j][1])),
                          chroma_den);
            }

            mdcv_conf.WhitePointX =
                FFMIN(lrint(chroma_den * av_q2d(mdm->white_point[0])),
                      chroma_den);
            mdcv_conf.WhitePointY =
                FFMIN(lrint(chroma_den * av_q2d(mdm->white_point[1])),
                      chroma_den);

            /* MaxDisplayMasteringLuminance is in the unit of 1 nits however
             * MinDisplayMasteringLuminance is in the unit of 0.0001 nits
             */
            mdcv_conf.MaxDisplayMasteringLuminance =
                lrint(av_q2d(mdm->max_luminance));
            mdcv_conf.MinDisplayMasteringLuminance =
                lrint(luma_den * av_q2d(mdm->min_luminance));
            tm = 1;
        }
    }

    memset(&clli_conf, 0, sizeof(mfxExtContentLightLevelInfo));
    sd = av_frame_get_side_data(in, AV_FRAME_DATA_CONTENT_LIGHT_LEVEL);
    if (vpp->tonemap && sd) {
        AVContentLightMetadata *clm = (AVContentLightMetadata *)sd->data;

        clli_conf.Header.BufferId         = MFX_EXTBUFF_CONTENT_LIGHT_LEVEL_INFO;
        clli_conf.Header.BufferSz         = sizeof(mfxExtContentLightLevelInfo);
        clli_conf.MaxContentLightLevel    = FFMIN(clm->MaxCLL,  65535);
        clli_conf.MaxPicAverageLightLevel = FFMIN(clm->MaxFALL, 65535);
        tm = 1;
    }

    if (tm) {
        av_frame_remove_side_data(out, AV_FRAME_DATA_CONTENT_LIGHT_LEVEL);
        av_frame_remove_side_data(out, AV_FRAME_DATA_MASTERING_DISPLAY_METADATA);

        out->color_primaries = AVCOL_PRI_BT709;
        out->color_trc = AVCOL_TRC_BT709;
        out->colorspace = AVCOL_SPC_BT709;
        out->color_range = AVCOL_RANGE_MPEG;
    }

    if (vpp->color_range != AVCOL_RANGE_UNSPECIFIED)
        out->color_range = vpp->color_range;
    if (vpp->color_primaries != AVCOL_PRI_UNSPECIFIED)
        out->color_primaries = vpp->color_primaries;
    if (vpp->color_transfer != AVCOL_TRC_UNSPECIFIED)
        out->color_trc = vpp->color_transfer;
    if (vpp->color_matrix != AVCOL_SPC_UNSPECIFIED)
        out->colorspace = vpp->color_matrix;

    memset(&outvsi_conf, 0, sizeof(mfxExtVideoSignalInfo));
    outvsi_conf.Header.BufferId          = MFX_EXTBUFF_VIDEO_SIGNAL_INFO_OUT;
    outvsi_conf.Header.BufferSz          = sizeof(mfxExtVideoSignalInfo);
    outvsi_conf.VideoFullRange           = (out->color_range == AVCOL_RANGE_JPEG);
    outvsi_conf.ColourPrimaries          = (out->color_primaries == AVCOL_PRI_UNSPECIFIED) ? AVCOL_PRI_BT709 : out->color_primaries;
    outvsi_conf.TransferCharacteristics  = (out->color_trc == AVCOL_TRC_UNSPECIFIED) ? AVCOL_TRC_BT709 : out->color_trc;
    outvsi_conf.MatrixCoefficients       = (out->colorspace == AVCOL_SPC_UNSPECIFIED) ? AVCOL_SPC_BT709 : out->colorspace;
    outvsi_conf.ColourDescriptionPresent = 1;

    if (memcmp(&vpp->invsi_conf, &invsi_conf, sizeof(mfxExtVideoSignalInfo)) ||
        memcmp(&vpp->mdcv_conf, &mdcv_conf, sizeof(mfxExtMasteringDisplayColourVolume)) ||
        memcmp(&vpp->clli_conf, &clli_conf, sizeof(mfxExtContentLightLevelInfo)) ||
        memcmp(&vpp->outvsi_conf, &outvsi_conf, sizeof(mfxExtVideoSignalInfo))) {
        vpp->invsi_conf                 = invsi_conf;
        fp->ext_buf[fp->num_ext_buf++]  = (mfxExtBuffer*)&vpp->invsi_conf;

        vpp->outvsi_conf                = outvsi_conf;
        fp->ext_buf[fp->num_ext_buf++]  = (mfxExtBuffer*)&vpp->outvsi_conf;

        vpp->mdcv_conf                     = mdcv_conf;
        if (mdcv_conf.Header.BufferId)
            fp->ext_buf[fp->num_ext_buf++] = (mfxExtBuffer*)&vpp->mdcv_conf;

        vpp->clli_conf                     = clli_conf;
        if (clli_conf.Header.BufferId)
            fp->ext_buf[fp->num_ext_buf++] = (mfxExtBuffer*)&vpp->clli_conf;
    }
#endif

    return 0;
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
    if (vpp->framerate.num == 0 || vpp->framerate.den == 0)
        outlink->time_base = inlink->time_base;
    else
        outlink->time_base = av_inv_q(vpp->framerate);

    param.filter_frame  = NULL;
    param.set_frame_ext_params = vpp_set_frame_ext_params;
    param.num_ext_buf   = 0;
    param.ext_buf       = ext_buf;

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

#define INIT_MFX_EXTBUF(extbuf, id) do { \
        memset(&vpp->extbuf, 0, sizeof(vpp->extbuf)); \
        vpp->extbuf.Header.BufferId = id; \
        vpp->extbuf.Header.BufferSz = sizeof(vpp->extbuf); \
        param.ext_buf[param.num_ext_buf++] = (mfxExtBuffer*)&vpp->extbuf; \
    } while (0)

#define SET_MFX_PARAM_FIELD(extbuf, field, value) do { \
        vpp->extbuf.field = value; \
    } while (0)

    if (vpp->deinterlace) {
        INIT_MFX_EXTBUF(deinterlace_conf, MFX_EXTBUFF_VPP_DEINTERLACING);
        SET_MFX_PARAM_FIELD(deinterlace_conf, Mode, (vpp->deinterlace == 1 ?
                            MFX_DEINTERLACING_BOB : MFX_DEINTERLACING_ADVANCED));
    }

    if (vpp->use_frc) {
        INIT_MFX_EXTBUF(frc_conf, MFX_EXTBUFF_VPP_FRAME_RATE_CONVERSION);
        SET_MFX_PARAM_FIELD(frc_conf, Algorithm, MFX_FRCALGM_DISTRIBUTED_TIMESTAMP);
    }

    if (vpp->denoise) {
        INIT_MFX_EXTBUF(denoise_conf, MFX_EXTBUFF_VPP_DENOISE);
        SET_MFX_PARAM_FIELD(denoise_conf, DenoiseFactor, vpp->denoise);
    }

    if (vpp->detail) {
        INIT_MFX_EXTBUF(detail_conf, MFX_EXTBUFF_VPP_DETAIL);
        SET_MFX_PARAM_FIELD(detail_conf, DetailFactor, vpp->detail);
    }

    if (vpp->procamp) {
        INIT_MFX_EXTBUF(procamp_conf, MFX_EXTBUFF_VPP_PROCAMP);
        SET_MFX_PARAM_FIELD(procamp_conf, Hue, vpp->hue);
        SET_MFX_PARAM_FIELD(procamp_conf, Saturation, vpp->saturation);
        SET_MFX_PARAM_FIELD(procamp_conf, Contrast, vpp->contrast);
        SET_MFX_PARAM_FIELD(procamp_conf, Brightness, vpp->brightness);
    }

    if (vpp->transpose >= 0) {
        if (QSV_RUNTIME_VERSION_ATLEAST(mfx_version, 1, 17)) {
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
        } else {
            av_log(ctx, AV_LOG_WARNING, "The QSV VPP transpose option is "
                   "not supported with this MSDK version.\n");
            vpp->transpose = 0;
        }
    }

    if (vpp->rotate) {
        if (QSV_RUNTIME_VERSION_ATLEAST(mfx_version, 1, 17)) {
            INIT_MFX_EXTBUF(rotation_conf, MFX_EXTBUFF_VPP_ROTATION);
            SET_MFX_PARAM_FIELD(rotation_conf, Angle, vpp->rotate);

            if (MFX_ANGLE_90 == vpp->rotate || MFX_ANGLE_270 == vpp->rotate) {
                FFSWAP(int, vpp->out_width, vpp->out_height);
                FFSWAP(int, outlink->w, outlink->h);
                av_log(ctx, AV_LOG_DEBUG, "Swap width and height for clock/cclock rotation.\n");
            }
        } else {
            av_log(ctx, AV_LOG_WARNING, "The QSV VPP rotate option is "
                   "not supported with this MSDK version.\n");
            vpp->rotate = 0;
        }
    }

    if (vpp->hflip) {
        if (QSV_RUNTIME_VERSION_ATLEAST(mfx_version, 1, 19)) {
            INIT_MFX_EXTBUF(mirroring_conf, MFX_EXTBUFF_VPP_MIRRORING);
            SET_MFX_PARAM_FIELD(mirroring_conf, Type, vpp->hflip);
        } else {
            av_log(ctx, AV_LOG_WARNING, "The QSV VPP hflip option is "
                   "not supported with this MSDK version.\n");
            vpp->hflip = 0;
        }
    }

    if (inlink->w != outlink->w || inlink->h != outlink->h || in_format != vpp->out_format) {
        if (QSV_RUNTIME_VERSION_ATLEAST(mfx_version, 1, 19)) {
            int mode = vpp->scale_mode;

#if QSV_ONEVPL
            if (mode > 2)
                mode = MFX_SCALING_MODE_VENDOR + mode - 2;
#endif

            INIT_MFX_EXTBUF(scale_conf, MFX_EXTBUFF_VPP_SCALING);
            SET_MFX_PARAM_FIELD(scale_conf, ScalingMode, mode);
        } else
            av_log(ctx, AV_LOG_WARNING, "The QSV VPP Scale & format conversion "
                   "option is not supported with this MSDK version.\n");
    }

#undef INIT_MFX_EXTBUF
#undef SET_MFX_PARAM_FIELD

    if (vpp->use_frc || vpp->use_crop || vpp->deinterlace || vpp->denoise ||
        vpp->detail || vpp->procamp || vpp->rotate || vpp->hflip ||
        inlink->w != outlink->w || inlink->h != outlink->h || in_format != vpp->out_format ||
        vpp->color_range != AVCOL_RANGE_UNSPECIFIED ||
        vpp->color_primaries != AVCOL_PRI_UNSPECIFIED ||
        vpp->color_transfer != AVCOL_TRC_UNSPECIFIED ||
        vpp->color_matrix != AVCOL_SPC_UNSPECIFIED ||
        vpp->tonemap ||
        !vpp->has_passthrough)
        return ff_qsvvpp_init(ctx, &param);
    else {
        /* No MFX session is created in this case */
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
    QSVVPPContext *qsv = ctx->priv;
    AVFrame *in = NULL;
    int ret, status = 0;
    int64_t pts = AV_NOPTS_VALUE;

    FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink);

    if (!qsv->eof) {
        ret = ff_inlink_consume_frame(inlink, &in);
        if (ret < 0)
            return ret;

        if (ff_inlink_acknowledge_status(inlink, &status, &pts)) {
            if (status == AVERROR_EOF) {
                qsv->eof = 1;
            }
        }
    }

    if (qsv->session) {
        if (in || qsv->eof) {
            ret = ff_qsvvpp_filter_frame(qsv, inlink, in);
            av_frame_free(&in);
            if (ret == AVERROR(EAGAIN))
                goto not_ready;
            else if (ret < 0)
                return ret;

            if (qsv->eof)
                goto eof;

            if (qsv->got_frame) {
                qsv->got_frame = 0;
                return 0;
            }
        }
    } else {
        /* No MFX session is created in pass-through mode */
        if (in) {
            if (in->pts != AV_NOPTS_VALUE)
                in->pts = av_rescale_q(in->pts, inlink->time_base, outlink->time_base);

            if (outlink->frame_rate.num && outlink->frame_rate.den)
                in->duration = av_rescale_q(1, av_inv_q(outlink->frame_rate), outlink->time_base);
            else
                in->duration = 0;

            ret = ff_filter_frame(outlink, in);
            if (ret < 0)
                return ret;

            if (qsv->eof)
                goto eof;

            return 0;
        }
    }

not_ready:
    if (qsv->eof)
        goto eof;

    FF_FILTER_FORWARD_WANTED(outlink, inlink);

    return FFERROR_NOT_READY;

eof:
    pts = av_rescale_q(pts, inlink->time_base, outlink->time_base);
    ff_outlink_set_status(outlink, status, pts);
    return 0;
}

static av_cold void vpp_uninit(AVFilterContext *ctx)
{
    ff_qsvvpp_close(ctx);
}

static const AVFilterPad vpp_inputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_input,
        .get_buffer.video = ff_qsvvpp_get_video_buffer,
    },
};

static const AVFilterPad vpp_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
};

#define DEFINE_QSV_FILTER(x, sn, ln, fmts) \
static const AVClass x##_class = { \
    .class_name = #sn "_qsv", \
    .item_name  = av_default_item_name, \
    .option     = x##_options, \
    .version    = LIBAVUTIL_VERSION_INT, \
}; \
const AVFilter ff_vf_##sn##_qsv = { \
    .name           = #sn "_qsv", \
    .description    = NULL_IF_CONFIG_SMALL("Quick Sync Video " #ln), \
    .preinit        = x##_preinit, \
    .init           = vpp_init, \
    .uninit         = vpp_uninit, \
    .priv_size      = sizeof(VPPContext), \
    .priv_class     = &x##_class, \
    FILTER_INPUTS(vpp_inputs), \
    FILTER_OUTPUTS(vpp_outputs), \
    fmts, \
    .activate       = activate, \
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE, \
    .flags          = AVFILTER_FLAG_HWDEVICE,       \
};

#if CONFIG_VPP_QSV_FILTER

static const AVOption vpp_options[] = {
    { "deinterlace", "deinterlace mode: 0=off, 1=bob, 2=advanced", OFFSET(deinterlace), AV_OPT_TYPE_INT,      { .i64 = 0 }, 0, MFX_DEINTERLACING_ADVANCED, .flags = FLAGS, .unit = "deinterlace" },
    { "bob",         "Bob deinterlace mode.",                      0,                   AV_OPT_TYPE_CONST,    { .i64 = MFX_DEINTERLACING_BOB },            .flags = FLAGS, .unit = "deinterlace" },
    { "advanced",    "Advanced deinterlace mode. ",                0,                   AV_OPT_TYPE_CONST,    { .i64 = MFX_DEINTERLACING_ADVANCED },       .flags = FLAGS, .unit = "deinterlace" },

    { "denoise",     "denoise level [0, 100]",       OFFSET(denoise),     AV_OPT_TYPE_INT,      { .i64 = 0 }, 0, 100, .flags = FLAGS },
    { "detail",      "enhancement level [0, 100]",   OFFSET(detail),      AV_OPT_TYPE_INT,      { .i64 = 0 }, 0, 100, .flags = FLAGS },
    { "framerate",   "output framerate",             OFFSET(framerate),   AV_OPT_TYPE_RATIONAL, { .dbl = 0.0 },0, DBL_MAX, .flags = FLAGS },
    { "procamp",     "Enable ProcAmp",               OFFSET(procamp),     AV_OPT_TYPE_INT,      { .i64 = 0 }, 0, 1, .flags = FLAGS},
    { "hue",         "ProcAmp hue",                  OFFSET(hue),         AV_OPT_TYPE_FLOAT,    { .dbl = 0.0 }, -180.0, 180.0, .flags = FLAGS},
    { "saturation",  "ProcAmp saturation",           OFFSET(saturation),  AV_OPT_TYPE_FLOAT,    { .dbl = 1.0 }, 0.0, 10.0, .flags = FLAGS},
    { "contrast",    "ProcAmp contrast",             OFFSET(contrast),    AV_OPT_TYPE_FLOAT,    { .dbl = 1.0 }, 0.0, 10.0, .flags = FLAGS},
    { "brightness",  "ProcAmp brightness",           OFFSET(brightness),  AV_OPT_TYPE_FLOAT,    { .dbl = 0.0 }, -100.0, 100.0, .flags = FLAGS},

    { "transpose",  "set transpose direction",       OFFSET(transpose),   AV_OPT_TYPE_INT,      { .i64 = -1 }, -1, 6, FLAGS, .unit = "transpose"},
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

    { "w",      "Output video width(0=input video width, -1=keep input video aspect)",  OFFSET(ow), AV_OPT_TYPE_STRING, { .str="cw" }, 0, 255, .flags = FLAGS },
    { "width",  "Output video width(0=input video width, -1=keep input video aspect)",  OFFSET(ow), AV_OPT_TYPE_STRING, { .str="cw" }, 0, 255, .flags = FLAGS },
    { "h",      "Output video height(0=input video height, -1=keep input video aspect)", OFFSET(oh), AV_OPT_TYPE_STRING, { .str="w*ch/cw" }, 0, 255, .flags = FLAGS },
    { "height", "Output video height(0=input video height, -1=keep input video aspect)", OFFSET(oh), AV_OPT_TYPE_STRING, { .str="w*ch/cw" }, 0, 255, .flags = FLAGS },
    { "format", "Output pixel format", OFFSET(output_format_str), AV_OPT_TYPE_STRING, { .str = "same" }, .flags = FLAGS },
    { "async_depth", "Internal parallelization depth, the higher the value the higher the latency.", OFFSET(qsv.async_depth), AV_OPT_TYPE_INT, { .i64 = 4 }, 0, INT_MAX, .flags = FLAGS },
#if QSV_ONEVPL
    { "scale_mode", "scaling & format conversion mode (mode compute(3), vd(4) and ve(5) are only available on some platforms)", OFFSET(scale_mode), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 5, .flags = FLAGS, .unit = "scale mode" },
#else
    { "scale_mode", "scaling & format conversion mode", OFFSET(scale_mode), AV_OPT_TYPE_INT, { .i64 = MFX_SCALING_MODE_DEFAULT }, MFX_SCALING_MODE_DEFAULT, MFX_SCALING_MODE_QUALITY, .flags = FLAGS, .unit = "scale mode" },
#endif
    { "auto",      "auto mode",             0,    AV_OPT_TYPE_CONST,  { .i64 = MFX_SCALING_MODE_DEFAULT},  INT_MIN, INT_MAX, FLAGS, .unit = "scale mode"},
    { "low_power", "low power mode",        0,    AV_OPT_TYPE_CONST,  { .i64 = MFX_SCALING_MODE_LOWPOWER}, INT_MIN, INT_MAX, FLAGS, .unit = "scale mode"},
    { "hq",        "high quality mode",     0,    AV_OPT_TYPE_CONST,  { .i64 = MFX_SCALING_MODE_QUALITY},  INT_MIN, INT_MAX, FLAGS, .unit = "scale mode"},
#if QSV_ONEVPL
    { "compute",   "compute",               0,    AV_OPT_TYPE_CONST,  { .i64 = 3},  INT_MIN, INT_MAX, FLAGS, .unit = "scale mode"},
    { "vd",        "vd",                    0,    AV_OPT_TYPE_CONST,  { .i64 = 4},  INT_MIN, INT_MAX, FLAGS, .unit = "scale mode"},
    { "ve",        "ve",                    0,    AV_OPT_TYPE_CONST,  { .i64 = 5},  INT_MIN, INT_MAX, FLAGS, .unit = "scale mode"},
#endif

    { "rate", "Generate output at frame rate or field rate, available only for deinterlace mode",
      OFFSET(field_rate), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, FLAGS, .unit = "rate" },
    { "frame", "Output at frame rate (one frame of output for each field-pair)",
      0, AV_OPT_TYPE_CONST, { .i64 = 0 }, 0, 0, FLAGS, .unit = "rate" },
    { "field", "Output at field rate (one frame of output for each field)",
      0, AV_OPT_TYPE_CONST, { .i64 = 1 }, 0, 0, FLAGS, .unit = "rate" },

    { "out_range", "Output color range",
      OFFSET(color_range), AV_OPT_TYPE_INT, { .i64 = AVCOL_RANGE_UNSPECIFIED },
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
    { "out_color_matrix", "Output color matrix coefficient set",
      OFFSET(color_matrix_str), AV_OPT_TYPE_STRING, { .str = NULL }, .flags = FLAGS },
    { "out_color_primaries", "Output color primaries",
      OFFSET(color_primaries_str), AV_OPT_TYPE_STRING, { .str = NULL }, .flags = FLAGS },
    { "out_color_transfer", "Output color transfer characteristics",
      OFFSET(color_transfer_str),  AV_OPT_TYPE_STRING, { .str = NULL }, .flags = FLAGS },

    {"tonemap", "Perform tonemapping (0=disable tonemapping, 1=perform tonemapping if the input has HDR metadata)", OFFSET(tonemap), AV_OPT_TYPE_INT, {.i64 = 0 }, 0, 1, .flags = FLAGS},

    { NULL }
};

static int vpp_query_formats(AVFilterContext *ctx)
{
    VPPContext *vpp = ctx->priv;
    int ret, i = 0;
    static const enum AVPixelFormat in_pix_fmts[] = {
        AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_NV12,
        AV_PIX_FMT_YUYV422,
        AV_PIX_FMT_RGB32,
        AV_PIX_FMT_P010,
#if CONFIG_VAAPI
        AV_PIX_FMT_UYVY422,
#endif
        AV_PIX_FMT_QSV,
        AV_PIX_FMT_NONE
    };
    static enum AVPixelFormat out_pix_fmts[4];

    ret = ff_formats_ref(ff_make_format_list(in_pix_fmts),
                         &ctx->inputs[0]->outcfg.formats);
    if (ret < 0)
        return ret;

    /* User specifies the output format */
    if (vpp->out_format == AV_PIX_FMT_NV12 ||
        vpp->out_format == AV_PIX_FMT_P010)
        out_pix_fmts[i++] = vpp->out_format;
    else {
        out_pix_fmts[i++] = AV_PIX_FMT_NV12;
        out_pix_fmts[i++] = AV_PIX_FMT_P010;
    }

    out_pix_fmts[i++] = AV_PIX_FMT_QSV;
    out_pix_fmts[i++] = AV_PIX_FMT_NONE;

    return ff_formats_ref(ff_make_format_list(out_pix_fmts),
                          &ctx->outputs[0]->incfg.formats);
}

DEFINE_QSV_FILTER(vpp, vpp, "VPP", FILTER_QUERY_FUNC(vpp_query_formats));

#endif

#if CONFIG_SCALE_QSV_FILTER

static const AVOption qsvscale_options[] = {
    { "w",      "Output video width(0=input video width, -1=keep input video aspect)",  OFFSET(ow), AV_OPT_TYPE_STRING, { .str = "iw"   }, .flags = FLAGS },
    { "h",      "Output video height(0=input video height, -1=keep input video aspect)", OFFSET(oh), AV_OPT_TYPE_STRING, { .str = "ih"   }, .flags = FLAGS },
    { "format", "Output pixel format", OFFSET(output_format_str), AV_OPT_TYPE_STRING, { .str = "same" }, .flags = FLAGS },

#if QSV_ONEVPL
    { "mode",      "scaling & format conversion mode (mode compute(3), vd(4) and ve(5) are only available on some platforms)",    OFFSET(scale_mode),    AV_OPT_TYPE_INT,    { .i64 = 0}, 0, 5, FLAGS, .unit = "mode"},
#else
    { "mode",      "scaling & format conversion mode",    OFFSET(scale_mode),    AV_OPT_TYPE_INT,    { .i64 = MFX_SCALING_MODE_DEFAULT}, MFX_SCALING_MODE_DEFAULT, MFX_SCALING_MODE_QUALITY, FLAGS, .unit = "mode"},
#endif
    { "low_power", "low power mode",        0,             AV_OPT_TYPE_CONST,  { .i64 = MFX_SCALING_MODE_LOWPOWER}, INT_MIN, INT_MAX, FLAGS, .unit = "mode"},
    { "hq",        "high quality mode",     0,             AV_OPT_TYPE_CONST,  { .i64 = MFX_SCALING_MODE_QUALITY},  INT_MIN, INT_MAX, FLAGS, .unit = "mode"},
#if QSV_ONEVPL
    { "compute",   "compute",               0,             AV_OPT_TYPE_CONST,  { .i64 = 3},  INT_MIN, INT_MAX, FLAGS, .unit = "mode"},
    { "vd",        "vd",                    0,             AV_OPT_TYPE_CONST,  { .i64 = 4},  INT_MIN, INT_MAX, FLAGS, .unit = "mode"},
    { "ve",        "ve",                    0,             AV_OPT_TYPE_CONST,  { .i64 = 5},  INT_MIN, INT_MAX, FLAGS, .unit = "mode"},
#endif

    { NULL },
};

static av_cold int qsvscale_preinit(AVFilterContext *ctx)
{
    VPPContext  *vpp  = ctx->priv;

    vpp_preinit(ctx);
    vpp->has_passthrough = 0;

    return 0;
}

DEFINE_QSV_FILTER(qsvscale, scale, "scaling and format conversion", FILTER_SINGLE_PIXFMT(AV_PIX_FMT_QSV));

#endif

#if CONFIG_DEINTERLACE_QSV_FILTER

static const AVOption qsvdeint_options[] = {
    { "mode", "set deinterlace mode", OFFSET(deinterlace),   AV_OPT_TYPE_INT, {.i64 = MFX_DEINTERLACING_ADVANCED}, MFX_DEINTERLACING_BOB, MFX_DEINTERLACING_ADVANCED, FLAGS, .unit = "mode"},
    { "bob",   "bob algorithm",                  0, AV_OPT_TYPE_CONST,      {.i64 = MFX_DEINTERLACING_BOB}, MFX_DEINTERLACING_BOB, MFX_DEINTERLACING_ADVANCED, FLAGS, .unit = "mode"},
    { "advanced", "Motion adaptive algorithm",   0, AV_OPT_TYPE_CONST, {.i64 = MFX_DEINTERLACING_ADVANCED}, MFX_DEINTERLACING_BOB, MFX_DEINTERLACING_ADVANCED, FLAGS, .unit = "mode"},

    { NULL },
};

static av_cold int qsvdeint_preinit(AVFilterContext *ctx)
{
    VPPContext  *vpp  = ctx->priv;

    vpp_preinit(ctx);
    vpp->has_passthrough = 0;
    vpp->field_rate = 1;

    return 0;
}

DEFINE_QSV_FILTER(qsvdeint, deinterlace, "deinterlacing", FILTER_SINGLE_PIXFMT(AV_PIX_FMT_QSV))

#endif
