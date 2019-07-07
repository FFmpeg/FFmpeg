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
#include <va/va_enc_vp8.h>

#include "libavutil/avassert.h"
#include "libavutil/common.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "libavutil/pixfmt.h"

#include "avcodec.h"
#include "internal.h"
#include "vaapi_encode.h"
#include "vp8.h"


typedef struct VAAPIEncodeVP8Context {
    VAAPIEncodeContext common;

    // User options.
    int loop_filter_level;
    int loop_filter_sharpness;

    // Derived settings.
    int q_index_i;
    int q_index_p;
} VAAPIEncodeVP8Context;


#define vseq_var(name)     vseq->name, name
#define vseq_field(name)   vseq->seq_fields.bits.name, name
#define vpic_var(name)     vpic->name, name
#define vpic_field(name)   vpic->pic_fields.bits.name, name


static int vaapi_encode_vp8_init_sequence_params(AVCodecContext *avctx)
{
    VAAPIEncodeContext               *ctx = avctx->priv_data;
    VAEncSequenceParameterBufferVP8 *vseq = ctx->codec_sequence_params;

    vseq->frame_width  = avctx->width;
    vseq->frame_height = avctx->height;

    vseq->frame_width_scale  = 0;
    vseq->frame_height_scale = 0;

    vseq->error_resilient = 0;
    vseq->kf_auto = 0;

    if (!(ctx->va_rc_mode & VA_RC_CQP)) {
        vseq->bits_per_second = ctx->va_bit_rate;
        vseq->intra_period    = ctx->gop_size;
    }

    return 0;
}

static int vaapi_encode_vp8_init_picture_params(AVCodecContext *avctx,
                                                VAAPIEncodePicture *pic)
{
    VAAPIEncodeVP8Context          *priv = avctx->priv_data;
    VAEncPictureParameterBufferVP8 *vpic = pic->codec_picture_params;
    int i;

    vpic->reconstructed_frame = pic->recon_surface;

    vpic->coded_buf = pic->output_buffer;

    switch (pic->type) {
    case PICTURE_TYPE_IDR:
    case PICTURE_TYPE_I:
        av_assert0(pic->nb_refs == 0);
        vpic->ref_flags.bits.force_kf = 1;
        vpic->ref_last_frame =
        vpic->ref_gf_frame   =
        vpic->ref_arf_frame  =
            VA_INVALID_SURFACE;
        break;
    case PICTURE_TYPE_P:
        av_assert0(pic->nb_refs == 1);
        vpic->ref_flags.bits.no_ref_last = 0;
        vpic->ref_flags.bits.no_ref_gf   = 1;
        vpic->ref_flags.bits.no_ref_arf  = 1;
        vpic->ref_last_frame =
        vpic->ref_gf_frame   =
        vpic->ref_arf_frame  =
            pic->refs[0]->recon_surface;
        break;
    default:
        av_assert0(0 && "invalid picture type");
    }

    vpic->pic_flags.bits.frame_type = (pic->type != PICTURE_TYPE_IDR);
    vpic->pic_flags.bits.show_frame = 1;

    vpic->pic_flags.bits.refresh_last            = 1;
    vpic->pic_flags.bits.refresh_golden_frame    = 1;
    vpic->pic_flags.bits.refresh_alternate_frame = 1;

    vpic->pic_flags.bits.version = 0;
    vpic->pic_flags.bits.loop_filter_type = 0;
    for (i = 0; i < 4; i++)
        vpic->loop_filter_level[i] = priv->loop_filter_level;
    vpic->sharpness_level = priv->loop_filter_sharpness;

    vpic->clamp_qindex_low  = 0;
    vpic->clamp_qindex_high = 127;

    return 0;
}

static int vaapi_encode_vp8_write_quant_table(AVCodecContext *avctx,
                                              VAAPIEncodePicture *pic,
                                              int index, int *type,
                                              char *data, size_t *data_len)
{
    VAAPIEncodeVP8Context *priv = avctx->priv_data;
    VAQMatrixBufferVP8 quant;
    int i, q;

    if (index > 0)
        return AVERROR_EOF;

    if (*data_len < sizeof(quant))
        return AVERROR(EINVAL);
    *type     = VAQMatrixBufferType;
    *data_len = sizeof(quant);

    memset(&quant, 0, sizeof(quant));

    if (pic->type == PICTURE_TYPE_P)
        q = priv->q_index_p;
    else
        q = priv->q_index_i;

    for (i = 0; i < 4; i++)
        quant.quantization_index[i] = q;
    for (i = 0; i < 5; i++)
        quant.quantization_index_delta[i] = 0;

    memcpy(data, &quant, sizeof(quant));
    return 0;
}

static av_cold int vaapi_encode_vp8_configure(AVCodecContext *avctx)
{
    VAAPIEncodeContext     *ctx = avctx->priv_data;
    VAAPIEncodeVP8Context *priv = avctx->priv_data;

    priv->q_index_p = av_clip(ctx->rc_quality, 0, VP8_MAX_QUANT);
    if (avctx->i_quant_factor > 0.0)
        priv->q_index_i =
            av_clip((avctx->i_quant_factor * priv->q_index_p  +
                     avctx->i_quant_offset) + 0.5,
                    0, VP8_MAX_QUANT);
    else
        priv->q_index_i = priv->q_index_p;

    ctx->roi_quant_range = VP8_MAX_QUANT;

    return 0;
}

static const VAAPIEncodeProfile vaapi_encode_vp8_profiles[] = {
    { 0 /* VP8 has no profiles */, 8, 3, 1, 1, VAProfileVP8Version0_3 },
    { FF_PROFILE_UNKNOWN }
};

static const VAAPIEncodeType vaapi_encode_type_vp8 = {
    .profiles              = vaapi_encode_vp8_profiles,

    .configure             = &vaapi_encode_vp8_configure,

    .default_quality       = 40,

    .sequence_params_size  = sizeof(VAEncSequenceParameterBufferVP8),
    .init_sequence_params  = &vaapi_encode_vp8_init_sequence_params,

    .picture_params_size   = sizeof(VAEncPictureParameterBufferVP8),
    .init_picture_params   = &vaapi_encode_vp8_init_picture_params,

    .write_extra_buffer    = &vaapi_encode_vp8_write_quant_table,
};

static av_cold int vaapi_encode_vp8_init(AVCodecContext *avctx)
{
    VAAPIEncodeContext *ctx = avctx->priv_data;

    ctx->codec = &vaapi_encode_type_vp8;

    // No packed headers are currently desired.  VP8 has no metadata
    // which would be useful to write, and no existing driver supports
    // adding them anyway.
    ctx->desired_packed_headers = 0;

    ctx->surface_width  = FFALIGN(avctx->width,  16);
    ctx->surface_height = FFALIGN(avctx->height, 16);

    return ff_vaapi_encode_init(avctx);
}

#define OFFSET(x) offsetof(VAAPIEncodeVP8Context, x)
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM)
static const AVOption vaapi_encode_vp8_options[] = {
    VAAPI_ENCODE_COMMON_OPTIONS,
    VAAPI_ENCODE_RC_OPTIONS,

    { "loop_filter_level", "Loop filter level",
      OFFSET(loop_filter_level), AV_OPT_TYPE_INT, { .i64 = 16 }, 0, 63, FLAGS },
    { "loop_filter_sharpness", "Loop filter sharpness",
      OFFSET(loop_filter_sharpness), AV_OPT_TYPE_INT, { .i64 = 4 }, 0, 15, FLAGS },
    { NULL },
};

static const AVCodecDefault vaapi_encode_vp8_defaults[] = {
    { "b",              "0"   },
    { "bf",             "0"   },
    { "g",              "120" },
    { "qmin",           "-1"  },
    { "qmax",           "-1"  },
    { NULL },
};

static const AVClass vaapi_encode_vp8_class = {
    .class_name = "vp8_vaapi",
    .item_name  = av_default_item_name,
    .option     = vaapi_encode_vp8_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_vp8_vaapi_encoder = {
    .name           = "vp8_vaapi",
    .long_name      = NULL_IF_CONFIG_SMALL("VP8 (VAAPI)"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_VP8,
    .priv_data_size = sizeof(VAAPIEncodeVP8Context),
    .init           = &vaapi_encode_vp8_init,
    .send_frame     = &ff_vaapi_encode_send_frame,
    .receive_packet = &ff_vaapi_encode_receive_packet,
    .close          = &ff_vaapi_encode_close,
    .priv_class     = &vaapi_encode_vp8_class,
    .capabilities   = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_HARDWARE,
    .defaults       = vaapi_encode_vp8_defaults,
    .pix_fmts = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_VAAPI,
        AV_PIX_FMT_NONE,
    },
    .wrapper_name   = "vaapi",
};
