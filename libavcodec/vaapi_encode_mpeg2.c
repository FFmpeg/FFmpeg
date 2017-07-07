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
#include <va/va_enc_mpeg2.h>

#include "libavutil/avassert.h"
#include "libavutil/common.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "libavutil/pixfmt.h"

#include "avcodec.h"
#include "internal.h"
#include "mpegvideo.h"
#include "put_bits.h"
#include "vaapi_encode.h"

typedef struct VAAPIEncodeMPEG2Context {
    int mb_width;
    int mb_height;

    int quant_i;
    int quant_p;
    int quant_b;

    int64_t last_i_frame;

    unsigned int bit_rate;
    unsigned int vbv_buffer_size;
} VAAPIEncodeMPEG2Context;


#define vseq_var(name)      vseq->name, name
#define vseqext_field(name) vseq->sequence_extension.bits.name, name
#define vgop_field(name)    vseq->gop_header.bits.name, name
#define vpic_var(name)      vpic->name, name
#define vpcext_field(name)  vpic->picture_coding_extension.bits.name, name
#define vcomp_field(name)   vpic->composite_display.bits.name, name

#define u2(width, value, name) put_bits(&pbc, width, value)
#define u(width, ...) u2(width, __VA_ARGS__)

static int vaapi_encode_mpeg2_write_sequence_header(AVCodecContext *avctx,
                                                    char *data, size_t *data_len)
{
    VAAPIEncodeContext                 *ctx = avctx->priv_data;
    VAEncSequenceParameterBufferMPEG2 *vseq = ctx->codec_sequence_params;
    VAAPIEncodeMPEG2Context           *priv = ctx->priv_data;
    PutBitContext pbc;

    init_put_bits(&pbc, data, 8 * *data_len);

    u(32, SEQ_START_CODE, sequence_header_code);

    u(12, vseq->picture_width,  horizontal_size_value);
    u(12, vseq->picture_height, vertical_size_value);
    u(4, vseq_var(aspect_ratio_information));
    u(4, 8, frame_rate_code);
    u(18, priv->bit_rate & 0x3fff, bit_rate_value);
    u(1, 1, marker_bit);
    u(10, priv->vbv_buffer_size & 0x3ff, vbv_buffer_size_value);
    u(1, 0, constrained_parameters_flag);
    u(1, 0, load_intra_quantiser_matrix);
    // intra_quantiser_matrix[64]
    u(1, 0, load_non_intra_quantiser_matrix);
    // non_intra_quantiser_matrix[64]

    while (put_bits_count(&pbc) % 8)
        u(1, 0, zero_bit);

    u(32, EXT_START_CODE, extension_start_code);
    u(4, 1, extension_start_code_identifier);
    u(8, vseqext_field(profile_and_level_indication));
    u(1, vseqext_field(progressive_sequence));
    u(2, vseqext_field(chroma_format));
    u(2, 0, horizontal_size_extension);
    u(2, 0, vertical_size_extension);
    u(12, priv->bit_rate >> 18, bit_rate_extension);
    u(1, 1, marker_bit);
    u(8, priv->vbv_buffer_size >> 10, vbv_buffer_size_extension);
    u(1, vseqext_field(low_delay));
    u(2, vseqext_field(frame_rate_extension_n));
    u(2, vseqext_field(frame_rate_extension_d));

    while (put_bits_count(&pbc) % 8)
        u(1, 0, zero_bit);

    u(32, GOP_START_CODE, group_start_code);
    u(25, vgop_field(time_code));
    u(1, vgop_field(closed_gop));
    u(1, vgop_field(broken_link));

    while (put_bits_count(&pbc) % 8)
        u(1, 0, zero_bit);

    *data_len = put_bits_count(&pbc);
    flush_put_bits(&pbc);

    return 0;
}

static int vaapi_encode_mpeg2_write_picture_header(AVCodecContext *avctx,
                                                   VAAPIEncodePicture *pic,
                                                   char *data, size_t *data_len)
{
    VAEncPictureParameterBufferMPEG2 *vpic = pic->codec_picture_params;
    int picture_coding_type;
    PutBitContext pbc;

    init_put_bits(&pbc, data, 8 * *data_len);

    u(32, PICTURE_START_CODE, picture_start_code);
    u(10, vpic_var(temporal_reference));

    switch (vpic->picture_type) {
    case VAEncPictureTypeIntra:
        picture_coding_type = AV_PICTURE_TYPE_I;
        break;
    case VAEncPictureTypePredictive:
        picture_coding_type = AV_PICTURE_TYPE_P;
        break;
    case VAEncPictureTypeBidirectional:
        picture_coding_type = AV_PICTURE_TYPE_B;
        break;
    default:
        av_assert0(0 && "invalid picture_coding_type");
    }
    u(3, picture_coding_type, picture_coding_type);
    u(16, 0xffff, vbv_delay);
    if (picture_coding_type == 2 || picture_coding_type == 3) {
        u(1, 0, full_pel_forward_vector);
        u(3, 7, forward_f_code);
    }
    if (picture_coding_type == 3) {
        u(1, 0, full_pel_backward_vector);
        u(3, 7, backward_f_code);
    }
    u(1, 0, extra_bit_picture);

    while (put_bits_count(&pbc) % 8)
        u(1, 0, zero_bit);

    u(32, EXT_START_CODE, extension_start_code);
    u(4, 8, extension_start_code_identifier);
    u(4, vpic_var(f_code[0][0]));
    u(4, vpic_var(f_code[0][1]));
    u(4, vpic_var(f_code[1][0]));
    u(4, vpic_var(f_code[1][1]));
    u(2, vpcext_field(intra_dc_precision));
    u(2, vpcext_field(picture_structure));
    u(1, vpcext_field(top_field_first));
    u(1, vpcext_field(frame_pred_frame_dct));
    u(1, vpcext_field(concealment_motion_vectors));
    u(1, vpcext_field(q_scale_type));
    u(1, vpcext_field(intra_vlc_format));
    u(1, vpcext_field(alternate_scan));
    u(1, vpcext_field(repeat_first_field));
    u(1, 1, chroma_420_type);
    u(1, vpcext_field(progressive_frame));
    u(1, vpcext_field(composite_display_flag));
    if (vpic->picture_coding_extension.bits.composite_display_flag) {
        u(1, vcomp_field(v_axis));
        u(3, vcomp_field(field_sequence));
        u(1, vcomp_field(sub_carrier));
        u(7, vcomp_field(burst_amplitude));
        u(8, vcomp_field(sub_carrier_phase));
    }

    while (put_bits_count(&pbc) % 8)
        u(1, 0, zero_bit);

    *data_len = put_bits_count(&pbc);
    flush_put_bits(&pbc);

    return 0;
}

static int vaapi_encode_mpeg2_init_sequence_params(AVCodecContext *avctx)
{
    VAAPIEncodeContext                 *ctx = avctx->priv_data;
    VAEncSequenceParameterBufferMPEG2 *vseq = ctx->codec_sequence_params;
    VAEncPictureParameterBufferMPEG2  *vpic = ctx->codec_picture_params;
    VAAPIEncodeMPEG2Context           *priv = ctx->priv_data;

    vseq->intra_period   = avctx->gop_size;
    vseq->ip_period      = ctx->b_per_p + 1;

    vseq->picture_width  = avctx->width;
    vseq->picture_height = avctx->height;

    vseq->bits_per_second = avctx->bit_rate;
    if (avctx->framerate.num > 0 && avctx->framerate.den > 0)
        vseq->frame_rate = (float)avctx->framerate.num / avctx->framerate.den;
    else
        vseq->frame_rate = (float)avctx->time_base.num / avctx->time_base.den;

    vseq->aspect_ratio_information = 1;
    vseq->vbv_buffer_size = avctx->rc_buffer_size / (16 * 1024);

    vseq->sequence_extension.bits.profile_and_level_indication =
        avctx->profile << 4 | avctx->level;
    vseq->sequence_extension.bits.progressive_sequence   = 1;
    vseq->sequence_extension.bits.chroma_format          = 1;
    vseq->sequence_extension.bits.low_delay              = 0;
    vseq->sequence_extension.bits.frame_rate_extension_n = 0;
    vseq->sequence_extension.bits.frame_rate_extension_d = 0;

    vseq->new_gop_header              = 0;
    vseq->gop_header.bits.time_code   = 0;
    vseq->gop_header.bits.closed_gop  = 1;
    vseq->gop_header.bits.broken_link = 0;

    vpic->forward_reference_picture  = VA_INVALID_ID;
    vpic->backward_reference_picture = VA_INVALID_ID;
    vpic->reconstructed_picture      = VA_INVALID_ID;

    vpic->coded_buf = VA_INVALID_ID;

    vpic->temporal_reference = 0;
    vpic->f_code[0][0] = 15;
    vpic->f_code[0][1] = 15;
    vpic->f_code[1][0] = 15;
    vpic->f_code[1][1] = 15;

    vpic->picture_coding_extension.bits.intra_dc_precision     = 0;
    vpic->picture_coding_extension.bits.picture_structure      = 3;
    vpic->picture_coding_extension.bits.top_field_first        = 0;
    vpic->picture_coding_extension.bits.frame_pred_frame_dct   = 1;
    vpic->picture_coding_extension.bits.concealment_motion_vectors = 0;
    vpic->picture_coding_extension.bits.q_scale_type           = 0;
    vpic->picture_coding_extension.bits.intra_vlc_format       = 0;
    vpic->picture_coding_extension.bits.alternate_scan         = 0;
    vpic->picture_coding_extension.bits.repeat_first_field     = 0;
    vpic->picture_coding_extension.bits.progressive_frame      = 1;
    vpic->picture_coding_extension.bits.composite_display_flag = 0;

    priv->bit_rate = (avctx->bit_rate + 399) / 400;
    priv->vbv_buffer_size = avctx->rc_buffer_size / (16 * 1024);

    return 0;
}

static int vaapi_encode_mpeg2_init_picture_params(AVCodecContext *avctx,
                                                 VAAPIEncodePicture *pic)
{
    VAAPIEncodeContext                *ctx = avctx->priv_data;
    VAEncPictureParameterBufferMPEG2 *vpic = pic->codec_picture_params;
    VAAPIEncodeMPEG2Context          *priv = ctx->priv_data;
    int fch, fcv;

    switch (avctx->level) {
    case 4: // High.
    case 6: // High 1440.
        fch = 9;
        fcv = 5;
        break;
    case 8: // Main.
        fch = 8;
        fcv = 5;
        break;
    case 10: // Low.
    default:
        fch = 7;
        fcv = 4;
        break;
    }

    switch (pic->type) {
    case PICTURE_TYPE_IDR:
    case PICTURE_TYPE_I:
        vpic->picture_type = VAEncPictureTypeIntra;
        priv->last_i_frame = pic->display_order;
        break;
    case PICTURE_TYPE_P:
        vpic->picture_type = VAEncPictureTypePredictive;
        vpic->forward_reference_picture = pic->refs[0]->recon_surface;
        vpic->f_code[0][0] = fch;
        vpic->f_code[0][1] = fcv;
        break;
    case PICTURE_TYPE_B:
        vpic->picture_type = VAEncPictureTypeBidirectional;
        vpic->forward_reference_picture  = pic->refs[0]->recon_surface;
        vpic->backward_reference_picture = pic->refs[1]->recon_surface;
        vpic->f_code[0][0] = fch;
        vpic->f_code[0][1] = fcv;
        vpic->f_code[1][0] = fch;
        vpic->f_code[1][1] = fcv;
        break;
    default:
        av_assert0(0 && "invalid picture type");
    }

    vpic->reconstructed_picture = pic->recon_surface;
    vpic->coded_buf = pic->output_buffer;

    vpic->temporal_reference = pic->display_order - priv->last_i_frame;

    pic->nb_slices = priv->mb_height;

    return 0;
}

static int vaapi_encode_mpeg2_init_slice_params(AVCodecContext *avctx,
                                               VAAPIEncodePicture *pic,
                                               VAAPIEncodeSlice *slice)
{
    VAAPIEncodeContext                  *ctx = avctx->priv_data;
    VAEncSliceParameterBufferMPEG2   *vslice = slice->codec_slice_params;
    VAAPIEncodeMPEG2Context            *priv = ctx->priv_data;
    int qp;

    vslice->macroblock_address = priv->mb_width * slice->index;
    vslice->num_macroblocks    = priv->mb_width;

    switch (pic->type) {
    case PICTURE_TYPE_IDR:
    case PICTURE_TYPE_I:
        qp = priv->quant_i;
        break;
    case PICTURE_TYPE_P:
        qp = priv->quant_p;
        break;
    case PICTURE_TYPE_B:
        qp = priv->quant_b;
        break;
    default:
        av_assert0(0 && "invalid picture type");
    }

    vslice->quantiser_scale_code = qp;
    vslice->is_intra_slice = (pic->type == PICTURE_TYPE_IDR ||
                              pic->type == PICTURE_TYPE_I);

    return 0;
}

static av_cold int vaapi_encode_mpeg2_configure(AVCodecContext *avctx)
{
    VAAPIEncodeContext       *ctx = avctx->priv_data;
    VAAPIEncodeMPEG2Context *priv = ctx->priv_data;

    priv->mb_width  = FFALIGN(avctx->width,  16) / 16;
    priv->mb_height = FFALIGN(avctx->height, 16) / 16;

    if (ctx->va_rc_mode == VA_RC_CQP) {
        priv->quant_p = av_clip(avctx->global_quality, 1, 31);
        if (avctx->i_quant_factor > 0.0)
            priv->quant_i = av_clip((avctx->global_quality *
                                     avctx->i_quant_factor +
                                     avctx->i_quant_offset) + 0.5,
                                    1, 31);
        else
            priv->quant_i = priv->quant_p;
        if (avctx->b_quant_factor > 0.0)
            priv->quant_b = av_clip((avctx->global_quality *
                                     avctx->b_quant_factor +
                                     avctx->b_quant_offset) + 0.5,
                                    1, 31);
        else
            priv->quant_b = priv->quant_p;

        av_log(avctx, AV_LOG_DEBUG, "Using fixed quantiser "
               "%d / %d / %d for I- / P- / B-frames.\n",
               priv->quant_i, priv->quant_p, priv->quant_b);

    } else {
        av_assert0(0 && "Invalid RC mode.");
    }

    return 0;
}

static const VAAPIEncodeType vaapi_encode_type_mpeg2 = {
    .priv_data_size        = sizeof(VAAPIEncodeMPEG2Context),

    .configure             = &vaapi_encode_mpeg2_configure,

    .sequence_params_size  = sizeof(VAEncSequenceParameterBufferMPEG2),
    .init_sequence_params  = &vaapi_encode_mpeg2_init_sequence_params,

    .picture_params_size   = sizeof(VAEncPictureParameterBufferMPEG2),
    .init_picture_params   = &vaapi_encode_mpeg2_init_picture_params,

    .slice_params_size     = sizeof(VAEncSliceParameterBufferMPEG2),
    .init_slice_params     = &vaapi_encode_mpeg2_init_slice_params,

    .sequence_header_type  = VAEncPackedHeaderSequence,
    .write_sequence_header = &vaapi_encode_mpeg2_write_sequence_header,

    .picture_header_type   = VAEncPackedHeaderPicture,
    .write_picture_header  = &vaapi_encode_mpeg2_write_picture_header,
};

static av_cold int vaapi_encode_mpeg2_init(AVCodecContext *avctx)
{
    VAAPIEncodeContext *ctx = avctx->priv_data;

    ctx->codec = &vaapi_encode_type_mpeg2;

    switch (avctx->profile) {
    case FF_PROFILE_MPEG2_SIMPLE:
        ctx->va_profile = VAProfileMPEG2Simple;
        break;
    case FF_PROFILE_MPEG2_MAIN:
        ctx->va_profile = VAProfileMPEG2Main;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unknown MPEG-2 profile %d.\n",
               avctx->profile);
        return AVERROR(EINVAL);
    }

    ctx->va_entrypoint = VAEntrypointEncSlice;
    ctx->va_rt_format  = VA_RT_FORMAT_YUV420;
    ctx->va_rc_mode    = VA_RC_CQP;

    ctx->va_packed_headers = VA_ENC_PACKED_HEADER_SEQUENCE |
                             VA_ENC_PACKED_HEADER_PICTURE;

    ctx->surface_width  = FFALIGN(avctx->width,  16);
    ctx->surface_height = FFALIGN(avctx->height, 16);

    return ff_vaapi_encode_init(avctx);
}

static const AVCodecDefault vaapi_encode_mpeg2_defaults[] = {
    { "profile",        "4"   },
    { "level",          "4"   },
    { "bf",             "1"   },
    { "g",              "120" },
    { "i_qfactor",      "1"   },
    { "i_qoffset",      "0"   },
    { "b_qfactor",      "6/5" },
    { "b_qoffset",      "0"   },
    { "global_quality", "10"  },
    { NULL },
};

AVCodec ff_mpeg2_vaapi_encoder = {
    .name           = "mpeg2_vaapi",
    .long_name      = NULL_IF_CONFIG_SMALL("MPEG-2 (VAAPI)"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_MPEG2VIDEO,
    .priv_data_size = sizeof(VAAPIEncodeContext),
    .init           = &vaapi_encode_mpeg2_init,
    .encode2        = &ff_vaapi_encode2,
    .close          = &ff_vaapi_encode_close,
    .capabilities   = AV_CODEC_CAP_DELAY,
    .defaults       = vaapi_encode_mpeg2_defaults,
    .pix_fmts = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_VAAPI,
        AV_PIX_FMT_NONE,
    },
};
