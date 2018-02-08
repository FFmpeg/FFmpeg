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

#include <va/va.h>
#include <va/va_enc_vp9.h>

#include "libavutil/avassert.h"
#include "libavutil/common.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "libavutil/pixfmt.h"

#include "avcodec.h"
#include "internal.h"
#include "vaapi_encode.h"

#define VP9_MAX_QUANT 255


typedef struct VAAPIEncodeVP9Context {
    int q_idx_idr;
    int q_idx_p;
    int q_idx_b;

    // Reference direction for B-like frames:
    // 0 - most recent P/IDR frame is last.
    // 1 - most recent P frame is golden.
    int last_ref_dir;
} VAAPIEncodeVP9Context;

typedef struct VAAPIEncodeVP9Options {
    int loop_filter_level;
    int loop_filter_sharpness;
} VAAPIEncodeVP9Options;


#define vseq_var(name)     vseq->name, name
#define vseq_field(name)   vseq->seq_fields.bits.name, name
#define vpic_var(name)     vpic->name, name
#define vpic_field(name)   vpic->pic_fields.bits.name, name


static int vaapi_encode_vp9_init_sequence_params(AVCodecContext *avctx)
{
    VAAPIEncodeContext               *ctx = avctx->priv_data;
    VAEncSequenceParameterBufferVP9 *vseq = ctx->codec_sequence_params;
    VAEncPictureParameterBufferVP9  *vpic = ctx->codec_picture_params;

    vseq->max_frame_width  = avctx->width;
    vseq->max_frame_height = avctx->height;

    vseq->kf_auto = 0;

    if (!(ctx->va_rc_mode & VA_RC_CQP)) {
        vseq->bits_per_second = avctx->bit_rate;
        vseq->intra_period    = avctx->gop_size;
    }

    vpic->frame_width_src  = avctx->width;
    vpic->frame_height_src = avctx->height;
    vpic->frame_width_dst  = avctx->width;
    vpic->frame_height_dst = avctx->height;

    return 0;
}

static int vaapi_encode_vp9_init_picture_params(AVCodecContext *avctx,
                                                VAAPIEncodePicture *pic)
{
    VAAPIEncodeContext              *ctx = avctx->priv_data;
    VAEncPictureParameterBufferVP9 *vpic = pic->codec_picture_params;
    VAAPIEncodeVP9Context          *priv = ctx->priv_data;
    VAAPIEncodeVP9Options           *opt = ctx->codec_options;
    int i;

    vpic->reconstructed_frame = pic->recon_surface;
    vpic->coded_buf = pic->output_buffer;

    switch (pic->type) {
    case PICTURE_TYPE_IDR:
        av_assert0(pic->nb_refs == 0);
        vpic->ref_flags.bits.force_kf = 1;
        vpic->refresh_frame_flags = 0x01;
        priv->last_ref_dir = 0;
        break;
    case PICTURE_TYPE_P:
        av_assert0(pic->nb_refs == 1);
        if (avctx->max_b_frames > 0) {
            if (priv->last_ref_dir) {
                vpic->ref_flags.bits.ref_frame_ctrl_l0  = 2;
                vpic->ref_flags.bits.ref_gf_idx         = 1;
                vpic->ref_flags.bits.ref_gf_sign_bias   = 1;
                vpic->refresh_frame_flags = 0x01;
            } else {
                vpic->ref_flags.bits.ref_frame_ctrl_l0  = 1;
                vpic->ref_flags.bits.ref_last_idx       = 0;
                vpic->ref_flags.bits.ref_last_sign_bias = 1;
                vpic->refresh_frame_flags = 0x02;
            }
        } else {
            vpic->ref_flags.bits.ref_frame_ctrl_l0  = 1;
            vpic->ref_flags.bits.ref_last_idx       = 0;
            vpic->ref_flags.bits.ref_last_sign_bias = 1;
            vpic->refresh_frame_flags = 0x01;
        }
        break;
    case PICTURE_TYPE_B:
        av_assert0(pic->nb_refs == 2);
        if (priv->last_ref_dir) {
            vpic->ref_flags.bits.ref_frame_ctrl_l0  = 1;
            vpic->ref_flags.bits.ref_frame_ctrl_l1  = 2;
            vpic->ref_flags.bits.ref_last_idx       = 0;
            vpic->ref_flags.bits.ref_last_sign_bias = 1;
            vpic->ref_flags.bits.ref_gf_idx         = 1;
            vpic->ref_flags.bits.ref_gf_sign_bias   = 0;
        } else {
            vpic->ref_flags.bits.ref_frame_ctrl_l0  = 2;
            vpic->ref_flags.bits.ref_frame_ctrl_l1  = 1;
            vpic->ref_flags.bits.ref_last_idx       = 0;
            vpic->ref_flags.bits.ref_last_sign_bias = 0;
            vpic->ref_flags.bits.ref_gf_idx         = 1;
            vpic->ref_flags.bits.ref_gf_sign_bias   = 1;
        }
        vpic->refresh_frame_flags = 0x00;
        break;
    default:
        av_assert0(0 && "invalid picture type");
    }

    for (i = 0; i < FF_ARRAY_ELEMS(vpic->reference_frames); i++)
        vpic->reference_frames[i] = VA_INVALID_SURFACE;
    if (pic->type == PICTURE_TYPE_P) {
        av_assert0(pic->refs[0]);
        vpic->reference_frames[priv->last_ref_dir] =
            pic->refs[0]->recon_surface;
    } else if (pic->type == PICTURE_TYPE_B) {
        av_assert0(pic->refs[0] && pic->refs[1]);
        vpic->reference_frames[!priv->last_ref_dir] =
            pic->refs[0]->recon_surface;
        vpic->reference_frames[priv->last_ref_dir] =
            pic->refs[1]->recon_surface;
    }

    vpic->pic_flags.bits.frame_type = (pic->type != PICTURE_TYPE_IDR);
    vpic->pic_flags.bits.show_frame = pic->display_order <= pic->encode_order;

    if (pic->type == PICTURE_TYPE_IDR)
        vpic->luma_ac_qindex     = priv->q_idx_idr;
    else if (pic->type == PICTURE_TYPE_P)
        vpic->luma_ac_qindex     = priv->q_idx_p;
    else
        vpic->luma_ac_qindex     = priv->q_idx_b;
    vpic->luma_dc_qindex_delta   = 0;
    vpic->chroma_ac_qindex_delta = 0;
    vpic->chroma_dc_qindex_delta = 0;

    vpic->filter_level    = opt->loop_filter_level;
    vpic->sharpness_level = opt->loop_filter_sharpness;

    if (avctx->max_b_frames > 0 && pic->type == PICTURE_TYPE_P)
        priv->last_ref_dir = !priv->last_ref_dir;

    return 0;
}

static av_cold int vaapi_encode_vp9_configure(AVCodecContext *avctx)
{
    VAAPIEncodeContext     *ctx = avctx->priv_data;
    VAAPIEncodeVP9Context *priv = ctx->priv_data;

    priv->q_idx_p = av_clip(avctx->global_quality, 0, VP9_MAX_QUANT);
    if (avctx->i_quant_factor > 0.0)
        priv->q_idx_idr = av_clip((avctx->global_quality *
                                   avctx->i_quant_factor +
                                   avctx->i_quant_offset) + 0.5,
                                  0, VP9_MAX_QUANT);
    else
        priv->q_idx_idr = priv->q_idx_p;
    if (avctx->b_quant_factor > 0.0)
        priv->q_idx_b = av_clip((avctx->global_quality *
                                 avctx->b_quant_factor +
                                 avctx->b_quant_offset) + 0.5,
                                0, VP9_MAX_QUANT);
    else
        priv->q_idx_b = priv->q_idx_p;

    return 0;
}

static const VAAPIEncodeType vaapi_encode_type_vp9 = {
    .configure             = &vaapi_encode_vp9_configure,

    .priv_data_size        = sizeof(VAAPIEncodeVP9Context),

    .sequence_params_size  = sizeof(VAEncSequenceParameterBufferVP9),
    .init_sequence_params  = &vaapi_encode_vp9_init_sequence_params,

    .picture_params_size   = sizeof(VAEncPictureParameterBufferVP9),
    .init_picture_params   = &vaapi_encode_vp9_init_picture_params,
};

static av_cold int vaapi_encode_vp9_init(AVCodecContext *avctx)
{
    VAAPIEncodeContext *ctx = avctx->priv_data;

    ctx->codec = &vaapi_encode_type_vp9;

    switch (avctx->profile) {
    case FF_PROFILE_VP9_0:
    case FF_PROFILE_UNKNOWN:
        ctx->va_profile = VAProfileVP9Profile0;
        ctx->va_rt_format = VA_RT_FORMAT_YUV420;
        break;
    case FF_PROFILE_VP9_1:
        av_log(avctx, AV_LOG_ERROR, "VP9 profile 1 is not "
               "supported.\n");
        return AVERROR_PATCHWELCOME;
    case FF_PROFILE_VP9_2:
        ctx->va_profile = VAProfileVP9Profile2;
        ctx->va_rt_format = VA_RT_FORMAT_YUV420_10BPP;
        break;
    case FF_PROFILE_VP9_3:
        av_log(avctx, AV_LOG_ERROR, "VP9 profile 3 is not "
               "supported.\n");
        return AVERROR_PATCHWELCOME;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unknown VP9 profile %d.\n",
               avctx->profile);
        return AVERROR(EINVAL);
    }
    ctx->va_entrypoint = VAEntrypointEncSlice;

    if (avctx->flags & AV_CODEC_FLAG_QSCALE) {
        ctx->va_rc_mode = VA_RC_CQP;
    } else if (avctx->bit_rate > 0) {
        if (avctx->bit_rate == avctx->rc_max_rate)
            ctx->va_rc_mode = VA_RC_CBR;
        else
            ctx->va_rc_mode = VA_RC_VBR;
    } else {
        ctx->va_rc_mode = VA_RC_CQP;
    }

    // Packed headers are not currently supported.
    ctx->va_packed_headers = 0;

    // Surfaces must be aligned to superblock boundaries.
    ctx->surface_width  = FFALIGN(avctx->width,  64);
    ctx->surface_height = FFALIGN(avctx->height, 64);

    return ff_vaapi_encode_init(avctx);
}

#define OFFSET(x) (offsetof(VAAPIEncodeContext, codec_options_data) + \
                   offsetof(VAAPIEncodeVP9Options, x))
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM)
static const AVOption vaapi_encode_vp9_options[] = {
    { "loop_filter_level", "Loop filter level",
      OFFSET(loop_filter_level), AV_OPT_TYPE_INT, { .i64 = 16 }, 0, 63, FLAGS },
    { "loop_filter_sharpness", "Loop filter sharpness",
      OFFSET(loop_filter_sharpness), AV_OPT_TYPE_INT, { .i64 = 4 }, 0, 15, FLAGS },
    { NULL },
};

static const AVCodecDefault vaapi_encode_vp9_defaults[] = {
    { "profile",        "0"   },
    { "b",              "0"   },
    { "bf",             "0"   },
    { "g",              "250" },
    { "global_quality", "100" },
    { NULL },
};

static const AVClass vaapi_encode_vp9_class = {
    .class_name = "vp9_vaapi",
    .item_name  = av_default_item_name,
    .option     = vaapi_encode_vp9_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_vp9_vaapi_encoder = {
    .name           = "vp9_vaapi",
    .long_name      = NULL_IF_CONFIG_SMALL("VP9 (VAAPI)"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_VP9,
    .priv_data_size = (sizeof(VAAPIEncodeContext) +
                       sizeof(VAAPIEncodeVP9Options)),
    .init           = &vaapi_encode_vp9_init,
    .encode2        = &ff_vaapi_encode2,
    .close          = &ff_vaapi_encode_close,
    .priv_class     = &vaapi_encode_vp9_class,
    .capabilities   = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_HARDWARE,
    .defaults       = vaapi_encode_vp9_defaults,
    .pix_fmts = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_VAAPI,
        AV_PIX_FMT_NONE,
    },
    .wrapper_name   = "vaapi",
};
