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

#include "avcodec.h"
#include "cbs.h"
#include "cbs_mpeg2.h"
#include "mpeg12.h"
#include "vaapi_encode.h"

typedef struct VAAPIEncodeMPEG2Context {
    VAAPIEncodeContext common;

    // User options.
    int profile;
    int level;

    // Derived settings.
    int mb_width;
    int mb_height;

    int quant_i;
    int quant_p;
    int quant_b;

    unsigned int bit_rate;
    unsigned int vbv_buffer_size;

    AVRational frame_rate;

    unsigned int f_code_horizontal;
    unsigned int f_code_vertical;

    // Stream state.
    int64_t last_i_frame;

    // Writer structures.
    MPEG2RawSequenceHeader sequence_header;
    MPEG2RawExtensionData  sequence_extension;
    MPEG2RawExtensionData  sequence_display_extension;
    MPEG2RawGroupOfPicturesHeader gop_header;
    MPEG2RawPictureHeader  picture_header;
    MPEG2RawExtensionData  picture_coding_extension;

    CodedBitstreamContext *cbc;
    CodedBitstreamFragment current_fragment;
} VAAPIEncodeMPEG2Context;


static int vaapi_encode_mpeg2_write_fragment(AVCodecContext *avctx,
                                             char *data, size_t *data_len,
                                             CodedBitstreamFragment *frag)
{
    VAAPIEncodeMPEG2Context *priv = avctx->priv_data;
    int err;

    err = ff_cbs_write_fragment_data(priv->cbc, frag);
    if (err < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to write packed header.\n");
        return err;
    }

    if (*data_len < 8 * frag->data_size - frag->data_bit_padding) {
        av_log(avctx, AV_LOG_ERROR, "Access unit too large: "
               "%zu < %zu.\n", *data_len,
               8 * frag->data_size - frag->data_bit_padding);
        return AVERROR(ENOSPC);
    }

    memcpy(data, frag->data, frag->data_size);
    *data_len = 8 * frag->data_size - frag->data_bit_padding;

    return 0;
}

static int vaapi_encode_mpeg2_add_header(AVCodecContext *avctx,
                                         CodedBitstreamFragment *frag,
                                         int type, void *header)
{
    VAAPIEncodeMPEG2Context *priv = avctx->priv_data;
    int err;

    err = ff_cbs_insert_unit_content(priv->cbc, frag, -1, type, header, NULL);
    if (err < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to add header: "
               "type = %d.\n", type);
        return err;
    }

    return 0;
}

static int vaapi_encode_mpeg2_write_sequence_header(AVCodecContext *avctx,
                                                    char *data, size_t *data_len)
{
    VAAPIEncodeMPEG2Context *priv = avctx->priv_data;
    CodedBitstreamFragment  *frag = &priv->current_fragment;
    int err;

    err = vaapi_encode_mpeg2_add_header(avctx, frag, MPEG2_START_SEQUENCE_HEADER,
                                        &priv->sequence_header);
    if (err < 0)
        goto fail;

    err = vaapi_encode_mpeg2_add_header(avctx, frag, MPEG2_START_EXTENSION,
                                        &priv->sequence_extension);
    if (err < 0)
        goto fail;

    err = vaapi_encode_mpeg2_add_header(avctx, frag, MPEG2_START_EXTENSION,
                                        &priv->sequence_display_extension);
    if (err < 0)
        goto fail;

    err = vaapi_encode_mpeg2_add_header(avctx, frag, MPEG2_START_GROUP,
                                        &priv->gop_header);
    if (err < 0)
        goto fail;

    err = vaapi_encode_mpeg2_write_fragment(avctx, data, data_len, frag);
fail:
    ff_cbs_fragment_uninit(priv->cbc, frag);
    return 0;
}

static int vaapi_encode_mpeg2_write_picture_header(AVCodecContext *avctx,
                                                   VAAPIEncodePicture *pic,
                                                   char *data, size_t *data_len)
{
    VAAPIEncodeMPEG2Context *priv = avctx->priv_data;
    CodedBitstreamFragment  *frag = &priv->current_fragment;
    int err;

    err = vaapi_encode_mpeg2_add_header(avctx, frag, MPEG2_START_PICTURE,
                                        &priv->picture_header);
    if (err < 0)
        goto fail;

    err = vaapi_encode_mpeg2_add_header(avctx, frag, MPEG2_START_EXTENSION,
                                        &priv->picture_coding_extension);
    if (err < 0)
        goto fail;

    err = vaapi_encode_mpeg2_write_fragment(avctx, data, data_len, frag);
fail:
    ff_cbs_fragment_uninit(priv->cbc, frag);
    return 0;
}

static int vaapi_encode_mpeg2_init_sequence_params(AVCodecContext *avctx)
{
    VAAPIEncodeContext                 *ctx = avctx->priv_data;
    VAAPIEncodeMPEG2Context           *priv = avctx->priv_data;
    MPEG2RawSequenceHeader              *sh = &priv->sequence_header;
    MPEG2RawSequenceExtension           *se = &priv->sequence_extension.data.sequence;
    MPEG2RawSequenceDisplayExtension   *sde = &priv->sequence_display_extension.data.sequence_display;
    MPEG2RawGroupOfPicturesHeader     *goph = &priv->gop_header;
    MPEG2RawPictureHeader               *ph = &priv->picture_header;
    MPEG2RawPictureCodingExtension     *pce = &priv->picture_coding_extension.data.picture_coding;
    VAEncSequenceParameterBufferMPEG2 *vseq = ctx->codec_sequence_params;
    VAEncPictureParameterBufferMPEG2  *vpic = ctx->codec_picture_params;
    int code, ext_n, ext_d;

    memset(sh,   0, sizeof(*sh));
    memset(se,   0, sizeof(*se));
    memset(sde,  0, sizeof(*sde));
    memset(goph, 0, sizeof(*goph));
    memset(ph,   0, sizeof(*ph));
    memset(pce,  0, sizeof(*pce));


    if (ctx->va_bit_rate > 0) {
        priv->bit_rate = (ctx->va_bit_rate + 399) / 400;
    } else {
        // Unknown (not a bitrate-targetting mode), so just use the
        // highest value.
        priv->bit_rate = 0x3fffffff;
    }
    if (avctx->rc_buffer_size > 0) {
        priv->vbv_buffer_size = (avctx->rc_buffer_size + (1 << 14) - 1) >> 14;
    } else {
        // Unknown, so guess a value from the bitrate.
        priv->vbv_buffer_size = priv->bit_rate >> 14;
    }

    switch (avctx->level) {
    case 4: // High.
    case 6: // High 1440.
        priv->f_code_horizontal = 9;
        priv->f_code_vertical   = 5;
        break;
    case 8: // Main.
        priv->f_code_horizontal = 8;
        priv->f_code_vertical   = 5;
        break;
    case 10: // Low.
    default:
        priv->f_code_horizontal = 7;
        priv->f_code_vertical   = 4;
        break;
    }


    // Sequence header

    sh->sequence_header_code = MPEG2_START_SEQUENCE_HEADER;

    sh->horizontal_size_value = avctx->width  & 0xfff;
    sh->vertical_size_value   = avctx->height & 0xfff;

    if (avctx->sample_aspect_ratio.num != 0 &&
        avctx->sample_aspect_ratio.den != 0) {
        AVRational dar = av_div_q(avctx->sample_aspect_ratio,
                                  (AVRational) { avctx->width, avctx->height });

        if (av_cmp_q(avctx->sample_aspect_ratio, (AVRational) { 1, 1 }) == 0) {
            sh->aspect_ratio_information = 1;
        } else if (av_cmp_q(dar, (AVRational) { 3, 4 }) == 0) {
            sh->aspect_ratio_information = 2;
        } else if (av_cmp_q(dar, (AVRational) { 9, 16 }) == 0) {
            sh->aspect_ratio_information = 3;
        } else if (av_cmp_q(dar, (AVRational) { 100, 221 }) == 0) {
            sh->aspect_ratio_information = 4;
        } else {
            av_log(avctx, AV_LOG_WARNING, "Sample aspect ratio %d:%d is not "
                   "representable, signalling square pixels instead.\n",
                   avctx->sample_aspect_ratio.num,
                   avctx->sample_aspect_ratio.den);
            sh->aspect_ratio_information = 1;
        }
    } else {
        // Unknown - assume square pixels.
        sh->aspect_ratio_information = 1;
    }

    if (avctx->framerate.num > 0 && avctx->framerate.den > 0)
        priv->frame_rate = avctx->framerate;
    else
        priv->frame_rate = av_inv_q(avctx->time_base);
    ff_mpeg12_find_best_frame_rate(priv->frame_rate,
                                   &code, &ext_n, &ext_d, 0);
    sh->frame_rate_code = code;

    sh->bit_rate_value        = priv->bit_rate & 0x3ffff;
    sh->vbv_buffer_size_value = priv->vbv_buffer_size & 0x3ff;

    sh->constrained_parameters_flag     = 0;
    sh->load_intra_quantiser_matrix     = 0;
    sh->load_non_intra_quantiser_matrix = 0;


    // Sequence extension

    priv->sequence_extension.extension_start_code = MPEG2_START_EXTENSION;
    priv->sequence_extension.extension_start_code_identifier =
        MPEG2_EXTENSION_SEQUENCE;

    se->profile_and_level_indication = avctx->profile << 4 | avctx->level;
    se->progressive_sequence = 1;
    se->chroma_format        = 1;

    se->horizontal_size_extension = avctx->width  >> 12;
    se->vertical_size_extension   = avctx->height >> 12;

    se->bit_rate_extension        = priv->bit_rate >> 18;
    se->vbv_buffer_size_extension = priv->vbv_buffer_size >> 10;
    se->low_delay                 = ctx->b_per_p == 0;

    se->frame_rate_extension_n = ext_n;
    se->frame_rate_extension_d = ext_d;


    // Sequence display extension

    priv->sequence_display_extension.extension_start_code =
        MPEG2_START_EXTENSION;
    priv->sequence_display_extension.extension_start_code_identifier =
        MPEG2_EXTENSION_SEQUENCE_DISPLAY;

    sde->video_format = 5;
    if (avctx->color_primaries != AVCOL_PRI_UNSPECIFIED ||
        avctx->color_trc       != AVCOL_TRC_UNSPECIFIED ||
        avctx->colorspace      != AVCOL_SPC_UNSPECIFIED) {
        sde->colour_description       = 1;
        sde->colour_primaries         = avctx->color_primaries;
        sde->transfer_characteristics = avctx->color_trc;
        sde->matrix_coefficients      = avctx->colorspace;
    } else {
        sde->colour_description = 0;
    }

    sde->display_horizontal_size = avctx->width;
    sde->display_vertical_size   = avctx->height;


    // GOP header

    goph->group_start_code = MPEG2_START_GROUP;

    goph->time_code   = 0;
    goph->closed_gop  = 1;
    goph->broken_link = 0;


    // Defaults for picture header

    ph->picture_start_code = MPEG2_START_PICTURE;

    ph->vbv_delay = 0xffff; // Not currently calculated.

    ph->full_pel_forward_vector  = 0;
    ph->forward_f_code           = 7;
    ph->full_pel_backward_vector = 0;
    ph->forward_f_code           = 7;


    // Defaults for picture coding extension

    priv->picture_coding_extension.extension_start_code =
        MPEG2_START_EXTENSION;
    priv->picture_coding_extension.extension_start_code_identifier =
        MPEG2_EXTENSION_PICTURE_CODING;

    pce->intra_dc_precision         = 0;
    pce->picture_structure          = 3;
    pce->top_field_first            = 0;
    pce->frame_pred_frame_dct       = 1;
    pce->concealment_motion_vectors = 0;
    pce->q_scale_type               = 0;
    pce->intra_vlc_format           = 0;
    pce->alternate_scan             = 0;
    pce->repeat_first_field         = 0;
    pce->progressive_frame          = 1;
    pce->composite_display_flag     = 0;



    *vseq = (VAEncSequenceParameterBufferMPEG2) {
        .intra_period = ctx->gop_size,
        .ip_period    = ctx->b_per_p + 1,

        .picture_width  = avctx->width,
        .picture_height = avctx->height,

        .bits_per_second          = ctx->va_bit_rate,
        .frame_rate               = av_q2d(priv->frame_rate),
        .aspect_ratio_information = sh->aspect_ratio_information,
        .vbv_buffer_size          = priv->vbv_buffer_size,

        .sequence_extension.bits = {
            .profile_and_level_indication = se->profile_and_level_indication,
            .progressive_sequence         = se->progressive_sequence,
            .chroma_format                = se->chroma_format,
            .low_delay                    = se->low_delay,
            .frame_rate_extension_n       = se->frame_rate_extension_n,
            .frame_rate_extension_d       = se->frame_rate_extension_d,
        },

        .new_gop_header = 1,
        .gop_header.bits = {
            .time_code   = goph->time_code,
            .closed_gop  = goph->closed_gop,
            .broken_link = goph->broken_link,
        },
    };

    *vpic = (VAEncPictureParameterBufferMPEG2) {
        .forward_reference_picture  = VA_INVALID_ID,
        .backward_reference_picture = VA_INVALID_ID,
        .reconstructed_picture      = VA_INVALID_ID,
        .coded_buf                  = VA_INVALID_ID,

        .vbv_delay = 0xffff,
        .f_code    = { { 15, 15 }, { 15, 15 } },

        .picture_coding_extension.bits = {
            .intra_dc_precision         = pce->intra_dc_precision,
            .picture_structure          = pce->picture_structure,
            .top_field_first            = pce->top_field_first,
            .frame_pred_frame_dct       = pce->frame_pred_frame_dct,
            .concealment_motion_vectors = pce->concealment_motion_vectors,
            .q_scale_type               = pce->q_scale_type,
            .intra_vlc_format           = pce->intra_vlc_format,
            .alternate_scan             = pce->alternate_scan,
            .repeat_first_field         = pce->repeat_first_field,
            .progressive_frame          = pce->progressive_frame,
            .composite_display_flag     = pce->composite_display_flag,
        },

        .composite_display.bits = {
            .v_axis            = pce->v_axis,
            .field_sequence    = pce->field_sequence,
            .sub_carrier       = pce->sub_carrier,
            .burst_amplitude   = pce->burst_amplitude,
            .sub_carrier_phase = pce->sub_carrier_phase,
        },
    };

    return 0;
}

static int vaapi_encode_mpeg2_init_picture_params(AVCodecContext *avctx,
                                                 VAAPIEncodePicture *pic)
{
    VAAPIEncodeMPEG2Context          *priv = avctx->priv_data;
    MPEG2RawPictureHeader              *ph = &priv->picture_header;
    MPEG2RawPictureCodingExtension    *pce = &priv->picture_coding_extension.data.picture_coding;
    VAEncPictureParameterBufferMPEG2 *vpic = pic->codec_picture_params;

    if (pic->type == PICTURE_TYPE_IDR || pic->type == PICTURE_TYPE_I) {
        ph->temporal_reference  = 0;
        ph->picture_coding_type = 1;
        priv->last_i_frame = pic->display_order;
    } else {
        ph->temporal_reference = pic->display_order - priv->last_i_frame;
        ph->picture_coding_type = pic->type == PICTURE_TYPE_B ? 3 : 2;
    }

    if (pic->type == PICTURE_TYPE_P || pic->type == PICTURE_TYPE_B) {
        pce->f_code[0][0] = priv->f_code_horizontal;
        pce->f_code[0][1] = priv->f_code_vertical;
    } else {
        pce->f_code[0][0] = 15;
        pce->f_code[0][1] = 15;
    }
    if (pic->type == PICTURE_TYPE_B) {
        pce->f_code[1][0] = priv->f_code_horizontal;
        pce->f_code[1][1] = priv->f_code_vertical;
    } else {
        pce->f_code[1][0] = 15;
        pce->f_code[1][1] = 15;
    }

    vpic->reconstructed_picture = pic->recon_surface;
    vpic->coded_buf             = pic->output_buffer;

    switch (pic->type) {
    case PICTURE_TYPE_IDR:
    case PICTURE_TYPE_I:
        vpic->picture_type = VAEncPictureTypeIntra;
        break;
    case PICTURE_TYPE_P:
        vpic->picture_type = VAEncPictureTypePredictive;
        vpic->forward_reference_picture = pic->refs[0]->recon_surface;
        break;
    case PICTURE_TYPE_B:
        vpic->picture_type = VAEncPictureTypeBidirectional;
        vpic->forward_reference_picture  = pic->refs[0]->recon_surface;
        vpic->backward_reference_picture = pic->refs[1]->recon_surface;
        break;
    default:
        av_assert0(0 && "invalid picture type");
    }

    vpic->temporal_reference = ph->temporal_reference;
    vpic->f_code[0][0]       = pce->f_code[0][0];
    vpic->f_code[0][1]       = pce->f_code[0][1];
    vpic->f_code[1][0]       = pce->f_code[1][0];
    vpic->f_code[1][1]       = pce->f_code[1][1];

    pic->nb_slices = priv->mb_height;

    return 0;
}

static int vaapi_encode_mpeg2_init_slice_params(AVCodecContext *avctx,
                                               VAAPIEncodePicture *pic,
                                               VAAPIEncodeSlice *slice)
{
    VAAPIEncodeMPEG2Context            *priv = avctx->priv_data;
    VAEncSliceParameterBufferMPEG2   *vslice = slice->codec_slice_params;
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
    VAAPIEncodeMPEG2Context *priv = avctx->priv_data;
    int err;

    err = ff_cbs_init(&priv->cbc, AV_CODEC_ID_MPEG2VIDEO, avctx);
    if (err < 0)
        return err;

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

static const VAAPIEncodeProfile vaapi_encode_mpeg2_profiles[] = {
    { FF_PROFILE_MPEG2_MAIN,   8, 3, 1, 1, VAProfileMPEG2Main   },
    { FF_PROFILE_MPEG2_SIMPLE, 8, 3, 1, 1, VAProfileMPEG2Simple },
    { FF_PROFILE_UNKNOWN }
};

static const VAAPIEncodeType vaapi_encode_type_mpeg2 = {
    .profiles              = vaapi_encode_mpeg2_profiles,

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
    VAAPIEncodeContext       *ctx = avctx->priv_data;
    VAAPIEncodeMPEG2Context *priv = avctx->priv_data;

    ctx->codec = &vaapi_encode_type_mpeg2;

    if (avctx->profile == FF_PROFILE_UNKNOWN)
        avctx->profile = priv->profile;
    if (avctx->level == FF_LEVEL_UNKNOWN)
        avctx->level = priv->level;

    // Reject unknown levels (these are required to set f_code for
    // motion vector encoding).
    switch (avctx->level) {
    case 4: // High
    case 6: // High 1440
    case 8: // Main
    case 10: // Low
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unknown MPEG-2 level %d.\n",
               avctx->level);
        return AVERROR(EINVAL);
    }

    if (avctx->height % 4096 == 0 || avctx->width % 4096 == 0) {
        av_log(avctx, AV_LOG_ERROR, "MPEG-2 does not support picture "
               "height or width divisible by 4096.\n");
        return AVERROR(EINVAL);
    }

    ctx->desired_packed_headers = VA_ENC_PACKED_HEADER_SEQUENCE |
                                  VA_ENC_PACKED_HEADER_PICTURE;

    ctx->surface_width  = FFALIGN(avctx->width,  16);
    ctx->surface_height = FFALIGN(avctx->height, 16);

    return ff_vaapi_encode_init(avctx);
}

static av_cold int vaapi_encode_mpeg2_close(AVCodecContext *avctx)
{
    VAAPIEncodeMPEG2Context *priv = avctx->priv_data;

    ff_cbs_close(&priv->cbc);

    return ff_vaapi_encode_close(avctx);
}

#define OFFSET(x) offsetof(VAAPIEncodeMPEG2Context, x)
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM)
static const AVOption vaapi_encode_mpeg2_options[] = {
    VAAPI_ENCODE_COMMON_OPTIONS,

    { "profile", "Set profile (in profile_and_level_indication)",
      OFFSET(profile), AV_OPT_TYPE_INT,
      { .i64 = FF_PROFILE_UNKNOWN }, FF_PROFILE_UNKNOWN, 7, FLAGS, "profile" },

#define PROFILE(name, value)  name, NULL, 0, AV_OPT_TYPE_CONST, \
      { .i64 = value }, 0, 0, FLAGS, "profile"
    { PROFILE("simple", FF_PROFILE_MPEG2_SIMPLE) },
    { PROFILE("main",   FF_PROFILE_MPEG2_MAIN)   },
#undef PROFILE

    { "level", "Set level (in profile_and_level_indication)",
      OFFSET(level), AV_OPT_TYPE_INT,
      { .i64 = 4 }, 0, 15, FLAGS, "level" },

#define LEVEL(name, value) name, NULL, 0, AV_OPT_TYPE_CONST, \
      { .i64 = value }, 0, 0, FLAGS, "level"
    { LEVEL("low",       10) },
    { LEVEL("main",       8) },
    { LEVEL("high_1440",  6) },
    { LEVEL("high",       4) },
#undef LEVEL

    { NULL },
};

static const AVCodecDefault vaapi_encode_mpeg2_defaults[] = {
    { "b",              "0"   },
    { "bf",             "1"   },
    { "g",              "120" },
    { "i_qfactor",      "1"   },
    { "i_qoffset",      "0"   },
    { "b_qfactor",      "6/5" },
    { "b_qoffset",      "0"   },
    { "global_quality", "10"  },
    { "qmin",           "-1"  },
    { "qmax",           "-1"  },
    { NULL },
};

static const AVClass vaapi_encode_mpeg2_class = {
    .class_name = "mpeg2_vaapi",
    .item_name  = av_default_item_name,
    .option     = vaapi_encode_mpeg2_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_mpeg2_vaapi_encoder = {
    .name           = "mpeg2_vaapi",
    .long_name      = NULL_IF_CONFIG_SMALL("MPEG-2 (VAAPI)"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_MPEG2VIDEO,
    .priv_data_size = sizeof(VAAPIEncodeMPEG2Context),
    .init           = &vaapi_encode_mpeg2_init,
    .encode2        = &ff_vaapi_encode2,
    .close          = &vaapi_encode_mpeg2_close,
    .priv_class     = &vaapi_encode_mpeg2_class,
    .capabilities   = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_HARDWARE,
    .defaults       = vaapi_encode_mpeg2_defaults,
    .pix_fmts = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_VAAPI,
        AV_PIX_FMT_NONE,
    },
    .wrapper_name   = "vaapi",
};
