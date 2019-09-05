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


typedef struct VAAPIEncodeVP9Picture {
    int slot;
} VAAPIEncodeVP9Picture;

typedef struct VAAPIEncodeVP9Context {
    VAAPIEncodeContext common;

    // User options.
    int loop_filter_level;
    int loop_filter_sharpness;

    // Derived settings.
    int q_idx_idr;
    int q_idx_p;
    int q_idx_b;
} VAAPIEncodeVP9Context;


static int vaapi_encode_vp9_init_sequence_params(AVCodecContext *avctx)
{
    VAAPIEncodeContext               *ctx = avctx->priv_data;
    VAEncSequenceParameterBufferVP9 *vseq = ctx->codec_sequence_params;
    VAEncPictureParameterBufferVP9  *vpic = ctx->codec_picture_params;

    vseq->max_frame_width  = avctx->width;
    vseq->max_frame_height = avctx->height;

    vseq->kf_auto = 0;

    if (!(ctx->va_rc_mode & VA_RC_CQP)) {
        vseq->bits_per_second = ctx->va_bit_rate;
        vseq->intra_period    = ctx->gop_size;
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
    VAAPIEncodeVP9Context          *priv = avctx->priv_data;
    VAAPIEncodeVP9Picture          *hpic = pic->priv_data;
    VAEncPictureParameterBufferVP9 *vpic = pic->codec_picture_params;
    int i;

    vpic->reconstructed_frame = pic->recon_surface;
    vpic->coded_buf = pic->output_buffer;

    switch (pic->type) {
    case PICTURE_TYPE_IDR:
        av_assert0(pic->nb_refs == 0);
        vpic->ref_flags.bits.force_kf = 1;
        vpic->refresh_frame_flags = 0xff;
        hpic->slot = 0;
        break;
    case PICTURE_TYPE_P:
        av_assert0(pic->nb_refs == 1);
        {
            VAAPIEncodeVP9Picture *href = pic->refs[0]->priv_data;
            av_assert0(href->slot == 0 || href->slot == 1);

            if (ctx->max_b_depth > 0) {
                hpic->slot = !href->slot;
                vpic->refresh_frame_flags = 1 << hpic->slot | 0xfc;
            } else {
                hpic->slot = 0;
                vpic->refresh_frame_flags = 0xff;
            }
            vpic->ref_flags.bits.ref_frame_ctrl_l0  = 1;
            vpic->ref_flags.bits.ref_last_idx       = href->slot;
            vpic->ref_flags.bits.ref_last_sign_bias = 1;
        }
        break;
    case PICTURE_TYPE_B:
        av_assert0(pic->nb_refs == 2);
        {
            VAAPIEncodeVP9Picture *href0 = pic->refs[0]->priv_data,
                                  *href1 = pic->refs[1]->priv_data;
            av_assert0(href0->slot < pic->b_depth + 1 &&
                       href1->slot < pic->b_depth + 1);

            if (pic->b_depth == ctx->max_b_depth) {
                // Unreferenced frame.
                vpic->refresh_frame_flags = 0x00;
                hpic->slot = 8;
            } else {
                vpic->refresh_frame_flags = 0xfe << pic->b_depth & 0xff;
                hpic->slot = 1 + pic->b_depth;
            }
            vpic->ref_flags.bits.ref_frame_ctrl_l0  = 1;
            vpic->ref_flags.bits.ref_frame_ctrl_l1  = 2;
            vpic->ref_flags.bits.ref_last_idx       = href0->slot;
            vpic->ref_flags.bits.ref_last_sign_bias = 1;
            vpic->ref_flags.bits.ref_gf_idx         = href1->slot;
            vpic->ref_flags.bits.ref_gf_sign_bias   = 0;
        }
        break;
    default:
        av_assert0(0 && "invalid picture type");
    }
    if (vpic->refresh_frame_flags == 0x00) {
        av_log(avctx, AV_LOG_DEBUG, "Pic %"PRId64" not stored.\n",
               pic->display_order);
    } else {
        av_log(avctx, AV_LOG_DEBUG, "Pic %"PRId64" stored in slot %d.\n",
               pic->display_order, hpic->slot);
    }

    for (i = 0; i < FF_ARRAY_ELEMS(vpic->reference_frames); i++)
        vpic->reference_frames[i] = VA_INVALID_SURFACE;

    for (i = 0; i < pic->nb_refs; i++) {
        VAAPIEncodePicture *ref_pic = pic->refs[i];
        int slot;
        slot = ((VAAPIEncodeVP9Picture*)ref_pic->priv_data)->slot;
        av_assert0(vpic->reference_frames[slot] == VA_INVALID_SURFACE);
        vpic->reference_frames[slot] = ref_pic->recon_surface;
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

    vpic->filter_level    = priv->loop_filter_level;
    vpic->sharpness_level = priv->loop_filter_sharpness;

    return 0;
}

static av_cold int vaapi_encode_vp9_configure(AVCodecContext *avctx)
{
    VAAPIEncodeContext     *ctx = avctx->priv_data;
    VAAPIEncodeVP9Context *priv = avctx->priv_data;

    if (ctx->rc_mode->quality) {
        priv->q_idx_p = av_clip(ctx->rc_quality, 0, VP9_MAX_QUANT);
        if (avctx->i_quant_factor > 0.0)
            priv->q_idx_idr =
                av_clip((avctx->i_quant_factor * priv->q_idx_p  +
                         avctx->i_quant_offset) + 0.5,
                        0, VP9_MAX_QUANT);
        else
            priv->q_idx_idr = priv->q_idx_p;
        if (avctx->b_quant_factor > 0.0)
            priv->q_idx_b =
                av_clip((avctx->b_quant_factor * priv->q_idx_p  +
                         avctx->b_quant_offset) + 0.5,
                        0, VP9_MAX_QUANT);
        else
            priv->q_idx_b = priv->q_idx_p;
    } else {
        // Arbitrary value.
        priv->q_idx_idr = priv->q_idx_p = priv->q_idx_b = 100;
    }

    ctx->roi_quant_range = VP9_MAX_QUANT;

    return 0;
}

static const VAAPIEncodeProfile vaapi_encode_vp9_profiles[] = {
    { FF_PROFILE_VP9_0,  8, 3, 1, 1, VAProfileVP9Profile0 },
    { FF_PROFILE_VP9_2, 10, 3, 1, 1, VAProfileVP9Profile2 },
    { FF_PROFILE_UNKNOWN }
};

static const VAAPIEncodeType vaapi_encode_type_vp9 = {
    .profiles              = vaapi_encode_vp9_profiles,

    .flags                 = FLAG_B_PICTURES |
                             FLAG_B_PICTURE_REFERENCES,

    .default_quality       = 100,

    .picture_priv_data_size = sizeof(VAAPIEncodeVP9Picture),

    .configure             = &vaapi_encode_vp9_configure,

    .sequence_params_size  = sizeof(VAEncSequenceParameterBufferVP9),
    .init_sequence_params  = &vaapi_encode_vp9_init_sequence_params,

    .picture_params_size   = sizeof(VAEncPictureParameterBufferVP9),
    .init_picture_params   = &vaapi_encode_vp9_init_picture_params,
};

static av_cold int vaapi_encode_vp9_init(AVCodecContext *avctx)
{
    VAAPIEncodeContext *ctx = avctx->priv_data;

    ctx->codec = &vaapi_encode_type_vp9;

    // No packed headers are currently desired.  They could be written,
    // but there isn't any reason to do so - the one usable driver (i965)
    // can write its own headers and there is no metadata to include.
    ctx->desired_packed_headers = 0;

    // Surfaces must be aligned to superblock boundaries.
    ctx->surface_width  = FFALIGN(avctx->width,  64);
    ctx->surface_height = FFALIGN(avctx->height, 64);

    return ff_vaapi_encode_init(avctx);
}

#define OFFSET(x) offsetof(VAAPIEncodeVP9Context, x)
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM)
static const AVOption vaapi_encode_vp9_options[] = {
    VAAPI_ENCODE_COMMON_OPTIONS,
    VAAPI_ENCODE_RC_OPTIONS,

    { "loop_filter_level", "Loop filter level",
      OFFSET(loop_filter_level), AV_OPT_TYPE_INT, { .i64 = 16 }, 0, 63, FLAGS },
    { "loop_filter_sharpness", "Loop filter sharpness",
      OFFSET(loop_filter_sharpness), AV_OPT_TYPE_INT, { .i64 = 4 }, 0, 15, FLAGS },
    { NULL },
};

static const AVCodecDefault vaapi_encode_vp9_defaults[] = {
    { "b",              "0"   },
    { "bf",             "0"   },
    { "g",              "250" },
    { "qmin",           "-1"  },
    { "qmax",           "-1"  },
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
    .priv_data_size = sizeof(VAAPIEncodeVP9Context),
    .init           = &vaapi_encode_vp9_init,
    .send_frame     = &ff_vaapi_encode_send_frame,
    .receive_packet = &ff_vaapi_encode_receive_packet,
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
