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

#include <string.h>

#include <va/va.h>
#include <va/va_enc_h264.h>

#include "libavutil/avassert.h"
#include "libavutil/common.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"

#include "avcodec.h"
#include "cbs.h"
#include "cbs_h264.h"
#include "h264.h"
#include "h264_levels.h"
#include "h264_sei.h"
#include "internal.h"
#include "vaapi_encode.h"

enum {
    SEI_TIMING         = 0x01,
    SEI_IDENTIFIER     = 0x02,
    SEI_RECOVERY_POINT = 0x04,
};

// Random (version 4) ISO 11578 UUID.
static const uint8_t vaapi_encode_h264_sei_identifier_uuid[16] = {
    0x59, 0x94, 0x8b, 0x28, 0x11, 0xec, 0x45, 0xaf,
    0x96, 0x75, 0x19, 0xd4, 0x1f, 0xea, 0xa9, 0x4d,
};

typedef struct VAAPIEncodeH264Picture {
    int frame_num;
    int pic_order_cnt;

    int64_t last_idr_frame;
    uint16_t idr_pic_id;

    int primary_pic_type;
    int slice_type;

    int cpb_delay;
    int dpb_delay;
} VAAPIEncodeH264Picture;

typedef struct VAAPIEncodeH264Context {
    VAAPIEncodeContext common;

    // User options.
    int qp;
    int quality;
    int coder;
    int aud;
    int sei;
    int profile;
    int level;

    // Derived settings.
    int mb_width;
    int mb_height;

    int fixed_qp_idr;
    int fixed_qp_p;
    int fixed_qp_b;

    int dpb_frames;

    // Writer structures.
    CodedBitstreamContext *cbc;
    CodedBitstreamFragment current_access_unit;

    H264RawAUD   raw_aud;
    H264RawSPS   raw_sps;
    H264RawPPS   raw_pps;
    H264RawSlice raw_slice;

    H264RawSEIBufferingPeriod      sei_buffering_period;
    H264RawSEIPicTiming            sei_pic_timing;
    H264RawSEIRecoveryPoint        sei_recovery_point;
    SEIRawUserDataUnregistered     sei_identifier;
    char                          *sei_identifier_string;

    int aud_needed;
    int sei_needed;
    int sei_cbr_workaround_needed;
} VAAPIEncodeH264Context;


static int vaapi_encode_h264_write_access_unit(AVCodecContext *avctx,
                                               char *data, size_t *data_len,
                                               CodedBitstreamFragment *au)
{
    VAAPIEncodeH264Context *priv = avctx->priv_data;
    int err;

    err = ff_cbs_write_fragment_data(priv->cbc, au);
    if (err < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to write packed header.\n");
        return err;
    }

    if (*data_len < 8 * au->data_size - au->data_bit_padding) {
        av_log(avctx, AV_LOG_ERROR, "Access unit too large: "
               "%zu < %zu.\n", *data_len,
               8 * au->data_size - au->data_bit_padding);
        return AVERROR(ENOSPC);
    }

    memcpy(data, au->data, au->data_size);
    *data_len = 8 * au->data_size - au->data_bit_padding;

    return 0;
}

static int vaapi_encode_h264_add_nal(AVCodecContext *avctx,
                                     CodedBitstreamFragment *au,
                                     void *nal_unit)
{
    H264RawNALUnitHeader *header = nal_unit;
    int err;

    err = ff_cbs_insert_unit_content(au, -1,
                                     header->nal_unit_type, nal_unit, NULL);
    if (err < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to add NAL unit: "
               "type = %d.\n", header->nal_unit_type);
        return err;
    }

    return 0;
}

static int vaapi_encode_h264_write_sequence_header(AVCodecContext *avctx,
                                                   char *data, size_t *data_len)
{
    VAAPIEncodeH264Context *priv = avctx->priv_data;
    CodedBitstreamFragment   *au = &priv->current_access_unit;
    int err;

    if (priv->aud_needed) {
        err = vaapi_encode_h264_add_nal(avctx, au, &priv->raw_aud);
        if (err < 0)
            goto fail;
        priv->aud_needed = 0;
    }

    err = vaapi_encode_h264_add_nal(avctx, au, &priv->raw_sps);
    if (err < 0)
        goto fail;

    err = vaapi_encode_h264_add_nal(avctx, au, &priv->raw_pps);
    if (err < 0)
        goto fail;

    err = vaapi_encode_h264_write_access_unit(avctx, data, data_len, au);
fail:
    ff_cbs_fragment_reset(au);
    return err;
}

static int vaapi_encode_h264_write_slice_header(AVCodecContext *avctx,
                                                VAAPIEncodePicture *pic,
                                                VAAPIEncodeSlice *slice,
                                                char *data, size_t *data_len)
{
    VAAPIEncodeH264Context *priv = avctx->priv_data;
    CodedBitstreamFragment   *au = &priv->current_access_unit;
    int err;

    if (priv->aud_needed) {
        err = vaapi_encode_h264_add_nal(avctx, au, &priv->raw_aud);
        if (err < 0)
            goto fail;
        priv->aud_needed = 0;
    }

    err = vaapi_encode_h264_add_nal(avctx, au, &priv->raw_slice);
    if (err < 0)
        goto fail;

    err = vaapi_encode_h264_write_access_unit(avctx, data, data_len, au);
fail:
    ff_cbs_fragment_reset(au);
    return err;
}

static int vaapi_encode_h264_write_extra_header(AVCodecContext *avctx,
                                                VAAPIEncodePicture *pic,
                                                int index, int *type,
                                                char *data, size_t *data_len)
{
    VAAPIEncodeH264Context *priv = avctx->priv_data;
    CodedBitstreamFragment   *au = &priv->current_access_unit;
    int err;

    if (priv->sei_needed) {
        if (priv->aud_needed) {
            err = vaapi_encode_h264_add_nal(avctx, au, &priv->raw_aud);
            if (err < 0)
                goto fail;
            priv->aud_needed = 0;
        }

        if (priv->sei_needed & SEI_IDENTIFIER) {
            err = ff_cbs_sei_add_message(priv->cbc, au, 1,
                                         SEI_TYPE_USER_DATA_UNREGISTERED,
                                         &priv->sei_identifier, NULL);
            if (err < 0)
                goto fail;
        }
        if (priv->sei_needed & SEI_TIMING) {
            if (pic->type == PICTURE_TYPE_IDR) {
                err = ff_cbs_sei_add_message(priv->cbc, au, 1,
                                             SEI_TYPE_BUFFERING_PERIOD,
                                             &priv->sei_buffering_period, NULL);
                if (err < 0)
                    goto fail;
            }
            err = ff_cbs_sei_add_message(priv->cbc, au, 1,
                                         SEI_TYPE_PIC_TIMING,
                                         &priv->sei_pic_timing, NULL);
            if (err < 0)
                goto fail;
        }
        if (priv->sei_needed & SEI_RECOVERY_POINT) {
            err = ff_cbs_sei_add_message(priv->cbc, au, 1,
                                         SEI_TYPE_RECOVERY_POINT,
                                         &priv->sei_recovery_point, NULL);
            if (err < 0)
                goto fail;
        }

        priv->sei_needed = 0;

        err = vaapi_encode_h264_write_access_unit(avctx, data, data_len, au);
        if (err < 0)
            goto fail;

        ff_cbs_fragment_reset(au);

        *type = VAEncPackedHeaderRawData;
        return 0;

#if !CONFIG_VAAPI_1
    } else if (priv->sei_cbr_workaround_needed) {
        // Insert a zero-length header using the old SEI type.  This is
        // required to avoid triggering broken behaviour on Intel platforms
        // in CBR mode where an invalid SEI message is generated by the
        // driver and inserted into the stream.
        *data_len = 0;
        *type = VAEncPackedHeaderH264_SEI;
        priv->sei_cbr_workaround_needed = 0;
        return 0;
#endif

    } else {
        return AVERROR_EOF;
    }

fail:
    ff_cbs_fragment_reset(au);
    return err;
}

static int vaapi_encode_h264_init_sequence_params(AVCodecContext *avctx)
{
    VAAPIEncodeContext                *ctx = avctx->priv_data;
    VAAPIEncodeH264Context           *priv = avctx->priv_data;
    H264RawSPS                        *sps = &priv->raw_sps;
    H264RawPPS                        *pps = &priv->raw_pps;
    VAEncSequenceParameterBufferH264 *vseq = ctx->codec_sequence_params;
    VAEncPictureParameterBufferH264  *vpic = ctx->codec_picture_params;

    memset(sps, 0, sizeof(*sps));
    memset(pps, 0, sizeof(*pps));

    sps->nal_unit_header.nal_ref_idc   = 3;
    sps->nal_unit_header.nal_unit_type = H264_NAL_SPS;

    sps->profile_idc = avctx->profile & 0xff;

    if (avctx->profile == FF_PROFILE_H264_CONSTRAINED_BASELINE ||
        avctx->profile == FF_PROFILE_H264_MAIN)
        sps->constraint_set1_flag = 1;

    if (avctx->profile == FF_PROFILE_H264_HIGH)
        sps->constraint_set3_flag = ctx->gop_size == 1;

    if (avctx->profile == FF_PROFILE_H264_MAIN ||
        avctx->profile == FF_PROFILE_H264_HIGH) {
        sps->constraint_set4_flag = 1;
        sps->constraint_set5_flag = ctx->b_per_p == 0;
    }

    if (ctx->gop_size == 1)
        priv->dpb_frames = 0;
    else
        priv->dpb_frames = 1 + ctx->max_b_depth;

    if (avctx->level != FF_LEVEL_UNKNOWN) {
        sps->level_idc = avctx->level;
    } else {
        const H264LevelDescriptor *level;
        int framerate;

        if (avctx->framerate.num > 0 && avctx->framerate.den > 0)
            framerate = avctx->framerate.num / avctx->framerate.den;
        else
            framerate = 0;

        level = ff_h264_guess_level(sps->profile_idc,
                                    avctx->bit_rate,
                                    framerate,
                                    priv->mb_width  * 16,
                                    priv->mb_height * 16,
                                    priv->dpb_frames);
        if (level) {
            av_log(avctx, AV_LOG_VERBOSE, "Using level %s.\n", level->name);
            if (level->constraint_set3_flag)
                sps->constraint_set3_flag = 1;
            sps->level_idc = level->level_idc;
        } else {
            av_log(avctx, AV_LOG_WARNING, "Stream will not conform "
                   "to any level: using level 6.2.\n");
            sps->level_idc = 62;
        }
    }

    sps->seq_parameter_set_id = 0;
    sps->chroma_format_idc    = 1;

    sps->log2_max_frame_num_minus4 = 4;
    sps->pic_order_cnt_type        = 0;
    sps->log2_max_pic_order_cnt_lsb_minus4 = 4;

    sps->max_num_ref_frames = priv->dpb_frames;

    sps->pic_width_in_mbs_minus1        = priv->mb_width  - 1;
    sps->pic_height_in_map_units_minus1 = priv->mb_height - 1;

    sps->frame_mbs_only_flag = 1;
    sps->direct_8x8_inference_flag = 1;

    if (avctx->width  != 16 * priv->mb_width ||
        avctx->height != 16 * priv->mb_height) {
        sps->frame_cropping_flag = 1;

        sps->frame_crop_left_offset   = 0;
        sps->frame_crop_right_offset  =
            (16 * priv->mb_width - avctx->width) / 2;
        sps->frame_crop_top_offset    = 0;
        sps->frame_crop_bottom_offset =
            (16 * priv->mb_height - avctx->height) / 2;
    } else {
        sps->frame_cropping_flag = 0;
    }

    sps->vui_parameters_present_flag = 1;

    if (avctx->sample_aspect_ratio.num != 0 &&
        avctx->sample_aspect_ratio.den != 0) {
        static const AVRational sar_idc[] = {
            {   0,  0 },
            {   1,  1 }, {  12, 11 }, {  10, 11 }, {  16, 11 },
            {  40, 33 }, {  24, 11 }, {  20, 11 }, {  32, 11 },
            {  80, 33 }, {  18, 11 }, {  15, 11 }, {  64, 33 },
            { 160, 99 }, {   4,  3 }, {   3,  2 }, {   2,  1 },
        };
        int num, den, i;
        av_reduce(&num, &den, avctx->sample_aspect_ratio.num,
                  avctx->sample_aspect_ratio.den, 65535);
        for (i = 0; i < FF_ARRAY_ELEMS(sar_idc); i++) {
            if (num == sar_idc[i].num &&
                den == sar_idc[i].den) {
                sps->vui.aspect_ratio_idc = i;
                break;
            }
        }
        if (i >= FF_ARRAY_ELEMS(sar_idc)) {
            sps->vui.aspect_ratio_idc = 255;
            sps->vui.sar_width  = num;
            sps->vui.sar_height = den;
        }
        sps->vui.aspect_ratio_info_present_flag = 1;
    }

    // Unspecified video format, from table E-2.
    sps->vui.video_format             = 5;
    sps->vui.video_full_range_flag    =
        avctx->color_range == AVCOL_RANGE_JPEG;
    sps->vui.colour_primaries         = avctx->color_primaries;
    sps->vui.transfer_characteristics = avctx->color_trc;
    sps->vui.matrix_coefficients      = avctx->colorspace;
    if (avctx->color_primaries != AVCOL_PRI_UNSPECIFIED ||
        avctx->color_trc       != AVCOL_TRC_UNSPECIFIED ||
        avctx->colorspace      != AVCOL_SPC_UNSPECIFIED)
        sps->vui.colour_description_present_flag = 1;
    if (avctx->color_range     != AVCOL_RANGE_UNSPECIFIED ||
        sps->vui.colour_description_present_flag)
        sps->vui.video_signal_type_present_flag = 1;

    if (avctx->chroma_sample_location != AVCHROMA_LOC_UNSPECIFIED) {
        sps->vui.chroma_loc_info_present_flag = 1;
        sps->vui.chroma_sample_loc_type_top_field    =
        sps->vui.chroma_sample_loc_type_bottom_field =
            avctx->chroma_sample_location - 1;
    }

    sps->vui.timing_info_present_flag = 1;
    if (avctx->framerate.num > 0 && avctx->framerate.den > 0) {
        sps->vui.num_units_in_tick = avctx->framerate.den;
        sps->vui.time_scale        = 2 * avctx->framerate.num;
        sps->vui.fixed_frame_rate_flag = 1;
    } else {
        sps->vui.num_units_in_tick = avctx->time_base.num;
        sps->vui.time_scale        = 2 * avctx->time_base.den;
        sps->vui.fixed_frame_rate_flag = 0;
    }

    if (priv->sei & SEI_TIMING) {
        H264RawHRD *hrd = &sps->vui.nal_hrd_parameters;
        H264RawSEIBufferingPeriod *bp = &priv->sei_buffering_period;

        sps->vui.nal_hrd_parameters_present_flag = 1;

        hrd->cpb_cnt_minus1 = 0;

        // Try to scale these to a sensible range so that the
        // golomb encode of the value is not overlong.
        hrd->bit_rate_scale =
            av_clip_uintp2(av_log2(ctx->va_bit_rate) - 15 - 6, 4);
        hrd->bit_rate_value_minus1[0] =
            (ctx->va_bit_rate >> hrd->bit_rate_scale + 6) - 1;

        hrd->cpb_size_scale =
            av_clip_uintp2(av_log2(ctx->hrd_params.buffer_size) - 15 - 4, 4);
        hrd->cpb_size_value_minus1[0] =
            (ctx->hrd_params.buffer_size >> hrd->cpb_size_scale + 4) - 1;

        // CBR mode as defined for the HRD cannot be achieved without filler
        // data, so this flag cannot be set even with VAAPI CBR modes.
        hrd->cbr_flag[0] = 0;

        hrd->initial_cpb_removal_delay_length_minus1 = 23;
        hrd->cpb_removal_delay_length_minus1         = 23;
        hrd->dpb_output_delay_length_minus1          = 7;
        hrd->time_offset_length                      = 0;

        bp->seq_parameter_set_id = sps->seq_parameter_set_id;

        // This calculation can easily overflow 32 bits.
        bp->nal.initial_cpb_removal_delay[0] = 90000 *
            (uint64_t)ctx->hrd_params.initial_buffer_fullness /
            ctx->hrd_params.buffer_size;
        bp->nal.initial_cpb_removal_delay_offset[0] = 0;
    } else {
        sps->vui.nal_hrd_parameters_present_flag = 0;
        sps->vui.low_delay_hrd_flag = 1 - sps->vui.fixed_frame_rate_flag;
    }

    sps->vui.bitstream_restriction_flag    = 1;
    sps->vui.motion_vectors_over_pic_boundaries_flag = 1;
    sps->vui.log2_max_mv_length_horizontal = 15;
    sps->vui.log2_max_mv_length_vertical   = 15;
    sps->vui.max_num_reorder_frames        = ctx->max_b_depth;
    sps->vui.max_dec_frame_buffering       = ctx->max_b_depth + 1;

    pps->nal_unit_header.nal_ref_idc = 3;
    pps->nal_unit_header.nal_unit_type = H264_NAL_PPS;

    pps->pic_parameter_set_id = 0;
    pps->seq_parameter_set_id = 0;

    pps->entropy_coding_mode_flag =
        !(sps->profile_idc == FF_PROFILE_H264_BASELINE ||
          sps->profile_idc == FF_PROFILE_H264_EXTENDED ||
          sps->profile_idc == FF_PROFILE_H264_CAVLC_444);
    if (!priv->coder && pps->entropy_coding_mode_flag)
        pps->entropy_coding_mode_flag = 0;

    pps->num_ref_idx_l0_default_active_minus1 = 0;
    pps->num_ref_idx_l1_default_active_minus1 = 0;

    pps->pic_init_qp_minus26 = priv->fixed_qp_idr - 26;

    if (sps->profile_idc == FF_PROFILE_H264_BASELINE ||
        sps->profile_idc == FF_PROFILE_H264_EXTENDED ||
        sps->profile_idc == FF_PROFILE_H264_MAIN) {
        pps->more_rbsp_data = 0;
    } else {
        pps->more_rbsp_data = 1;

        pps->transform_8x8_mode_flag = 1;
    }

    *vseq = (VAEncSequenceParameterBufferH264) {
        .seq_parameter_set_id = sps->seq_parameter_set_id,
        .level_idc        = sps->level_idc,
        .intra_period     = ctx->gop_size,
        .intra_idr_period = ctx->gop_size,
        .ip_period        = ctx->b_per_p + 1,

        .bits_per_second       = ctx->va_bit_rate,
        .max_num_ref_frames    = sps->max_num_ref_frames,
        .picture_width_in_mbs  = sps->pic_width_in_mbs_minus1 + 1,
        .picture_height_in_mbs = sps->pic_height_in_map_units_minus1 + 1,

        .seq_fields.bits = {
            .chroma_format_idc                 = sps->chroma_format_idc,
            .frame_mbs_only_flag               = sps->frame_mbs_only_flag,
            .mb_adaptive_frame_field_flag      = sps->mb_adaptive_frame_field_flag,
            .seq_scaling_matrix_present_flag   = sps->seq_scaling_matrix_present_flag,
            .direct_8x8_inference_flag         = sps->direct_8x8_inference_flag,
            .log2_max_frame_num_minus4         = sps->log2_max_frame_num_minus4,
            .pic_order_cnt_type                = sps->pic_order_cnt_type,
            .log2_max_pic_order_cnt_lsb_minus4 = sps->log2_max_pic_order_cnt_lsb_minus4,
            .delta_pic_order_always_zero_flag  = sps->delta_pic_order_always_zero_flag,
        },

        .bit_depth_luma_minus8   = sps->bit_depth_luma_minus8,
        .bit_depth_chroma_minus8 = sps->bit_depth_chroma_minus8,

        .frame_cropping_flag      = sps->frame_cropping_flag,
        .frame_crop_left_offset   = sps->frame_crop_left_offset,
        .frame_crop_right_offset  = sps->frame_crop_right_offset,
        .frame_crop_top_offset    = sps->frame_crop_top_offset,
        .frame_crop_bottom_offset = sps->frame_crop_bottom_offset,

        .vui_parameters_present_flag = sps->vui_parameters_present_flag,

        .vui_fields.bits = {
            .aspect_ratio_info_present_flag = sps->vui.aspect_ratio_info_present_flag,
            .timing_info_present_flag       = sps->vui.timing_info_present_flag,
            .bitstream_restriction_flag     = sps->vui.bitstream_restriction_flag,
            .log2_max_mv_length_horizontal  = sps->vui.log2_max_mv_length_horizontal,
            .log2_max_mv_length_vertical    = sps->vui.log2_max_mv_length_vertical,
        },

        .aspect_ratio_idc  = sps->vui.aspect_ratio_idc,
        .sar_width         = sps->vui.sar_width,
        .sar_height        = sps->vui.sar_height,
        .num_units_in_tick = sps->vui.num_units_in_tick,
        .time_scale        = sps->vui.time_scale,
    };

    *vpic = (VAEncPictureParameterBufferH264) {
        .CurrPic = {
            .picture_id = VA_INVALID_ID,
            .flags      = VA_PICTURE_H264_INVALID,
        },

        .coded_buf = VA_INVALID_ID,

        .pic_parameter_set_id = pps->pic_parameter_set_id,
        .seq_parameter_set_id = pps->seq_parameter_set_id,

        .pic_init_qp                  = pps->pic_init_qp_minus26 + 26,
        .num_ref_idx_l0_active_minus1 = pps->num_ref_idx_l0_default_active_minus1,
        .num_ref_idx_l1_active_minus1 = pps->num_ref_idx_l1_default_active_minus1,

        .chroma_qp_index_offset        = pps->chroma_qp_index_offset,
        .second_chroma_qp_index_offset = pps->second_chroma_qp_index_offset,

        .pic_fields.bits = {
            .entropy_coding_mode_flag        = pps->entropy_coding_mode_flag,
            .weighted_pred_flag              = pps->weighted_pred_flag,
            .weighted_bipred_idc             = pps->weighted_bipred_idc,
            .constrained_intra_pred_flag     = pps->constrained_intra_pred_flag,
            .transform_8x8_mode_flag         = pps->transform_8x8_mode_flag,
            .deblocking_filter_control_present_flag =
                pps->deblocking_filter_control_present_flag,
            .redundant_pic_cnt_present_flag  = pps->redundant_pic_cnt_present_flag,
            .pic_order_present_flag          =
                pps->bottom_field_pic_order_in_frame_present_flag,
            .pic_scaling_matrix_present_flag = pps->pic_scaling_matrix_present_flag,
        },
    };

    return 0;
}

static int vaapi_encode_h264_init_picture_params(AVCodecContext *avctx,
                                                 VAAPIEncodePicture *pic)
{
    VAAPIEncodeContext               *ctx = avctx->priv_data;
    VAAPIEncodeH264Context          *priv = avctx->priv_data;
    VAAPIEncodeH264Picture          *hpic = pic->priv_data;
    VAAPIEncodePicture              *prev = pic->prev;
    VAAPIEncodeH264Picture         *hprev = prev ? prev->priv_data : NULL;
    VAEncPictureParameterBufferH264 *vpic = pic->codec_picture_params;
    int i;

    if (pic->type == PICTURE_TYPE_IDR) {
        av_assert0(pic->display_order == pic->encode_order);

        hpic->frame_num      = 0;
        hpic->last_idr_frame = pic->display_order;
        hpic->idr_pic_id     = hprev ? hprev->idr_pic_id + 1 : 0;

        hpic->primary_pic_type = 0;
        hpic->slice_type       = 7;
    } else {
        av_assert0(prev);

        hpic->frame_num = hprev->frame_num + prev->is_reference;

        hpic->last_idr_frame = hprev->last_idr_frame;
        hpic->idr_pic_id     = hprev->idr_pic_id;

        if (pic->type == PICTURE_TYPE_I) {
            hpic->slice_type       = 7;
            hpic->primary_pic_type = 0;
        } else if (pic->type == PICTURE_TYPE_P) {
            hpic->slice_type       = 5;
            hpic->primary_pic_type = 1;
        } else {
            hpic->slice_type       = 6;
            hpic->primary_pic_type = 2;
        }
    }
    hpic->pic_order_cnt = pic->display_order - hpic->last_idr_frame;
    hpic->dpb_delay     = pic->display_order - pic->encode_order + ctx->max_b_depth;
    hpic->cpb_delay     = pic->encode_order - hpic->last_idr_frame;

    if (priv->aud) {
        priv->aud_needed = 1;
        priv->raw_aud = (H264RawAUD) {
            .nal_unit_header = {
                .nal_unit_type = H264_NAL_AUD,
            },
            .primary_pic_type  = hpic->primary_pic_type,
        };
    } else {
        priv->aud_needed = 0;
    }

    priv->sei_needed = 0;

    if (priv->sei & SEI_IDENTIFIER && pic->encode_order == 0)
        priv->sei_needed |= SEI_IDENTIFIER;
#if !CONFIG_VAAPI_1
    if (ctx->va_rc_mode == VA_RC_CBR)
        priv->sei_cbr_workaround_needed = 1;
#endif

    if (priv->sei & SEI_TIMING) {
        priv->sei_pic_timing = (H264RawSEIPicTiming) {
            .cpb_removal_delay = 2 * hpic->cpb_delay,
            .dpb_output_delay  = 2 * hpic->dpb_delay,
        };

        priv->sei_needed |= SEI_TIMING;
    }

    if (priv->sei & SEI_RECOVERY_POINT && pic->type == PICTURE_TYPE_I) {
        priv->sei_recovery_point = (H264RawSEIRecoveryPoint) {
            .recovery_frame_cnt = 0,
            .exact_match_flag   = 1,
            .broken_link_flag   = ctx->b_per_p > 0,
        };

        priv->sei_needed |= SEI_RECOVERY_POINT;
    }

    vpic->CurrPic = (VAPictureH264) {
        .picture_id          = pic->recon_surface,
        .frame_idx           = hpic->frame_num,
        .flags               = 0,
        .TopFieldOrderCnt    = hpic->pic_order_cnt,
        .BottomFieldOrderCnt = hpic->pic_order_cnt,
    };

    for (i = 0; i < pic->nb_refs; i++) {
        VAAPIEncodePicture      *ref = pic->refs[i];
        VAAPIEncodeH264Picture *href;

        av_assert0(ref && ref->encode_order < pic->encode_order);
        href = ref->priv_data;

        vpic->ReferenceFrames[i] = (VAPictureH264) {
            .picture_id          = ref->recon_surface,
            .frame_idx           = href->frame_num,
            .flags               = VA_PICTURE_H264_SHORT_TERM_REFERENCE,
            .TopFieldOrderCnt    = href->pic_order_cnt,
            .BottomFieldOrderCnt = href->pic_order_cnt,
        };
    }
    for (; i < FF_ARRAY_ELEMS(vpic->ReferenceFrames); i++) {
        vpic->ReferenceFrames[i] = (VAPictureH264) {
            .picture_id = VA_INVALID_ID,
            .flags      = VA_PICTURE_H264_INVALID,
        };
    }

    vpic->coded_buf = pic->output_buffer;

    vpic->frame_num = hpic->frame_num;

    vpic->pic_fields.bits.idr_pic_flag       = (pic->type == PICTURE_TYPE_IDR);
    vpic->pic_fields.bits.reference_pic_flag = (pic->type != PICTURE_TYPE_B);

    return 0;
}

static void vaapi_encode_h264_default_ref_pic_list(AVCodecContext *avctx,
                                                   VAAPIEncodePicture *pic,
                                                   VAAPIEncodePicture **rpl0,
                                                   VAAPIEncodePicture **rpl1,
                                                   int *rpl_size)
{
    VAAPIEncodePicture *prev;
    VAAPIEncodeH264Picture *hp, *hn, *hc;
    int i, j, n = 0;

    prev = pic->prev;
    av_assert0(prev);
    hp = pic->priv_data;

    for (i = 0; i < pic->prev->nb_dpb_pics; i++) {
        hn = prev->dpb[i]->priv_data;
        av_assert0(hn->frame_num < hp->frame_num);

        if (pic->type == PICTURE_TYPE_P) {
            for (j = n; j > 0; j--) {
                hc = rpl0[j - 1]->priv_data;
                av_assert0(hc->frame_num != hn->frame_num);
                if (hc->frame_num > hn->frame_num)
                    break;
                rpl0[j] = rpl0[j - 1];
            }
            rpl0[j] = prev->dpb[i];

        } else if (pic->type == PICTURE_TYPE_B) {
            for (j = n; j > 0; j--) {
                hc = rpl0[j - 1]->priv_data;
                av_assert0(hc->pic_order_cnt != hp->pic_order_cnt);
                if (hc->pic_order_cnt < hp->pic_order_cnt) {
                    if (hn->pic_order_cnt > hp->pic_order_cnt ||
                        hn->pic_order_cnt < hc->pic_order_cnt)
                        break;
                } else {
                    if (hn->pic_order_cnt > hc->pic_order_cnt)
                        break;
                }
                rpl0[j] = rpl0[j - 1];
            }
            rpl0[j] = prev->dpb[i];

            for (j = n; j > 0; j--) {
                hc = rpl1[j - 1]->priv_data;
                av_assert0(hc->pic_order_cnt != hp->pic_order_cnt);
                if (hc->pic_order_cnt > hp->pic_order_cnt) {
                    if (hn->pic_order_cnt < hp->pic_order_cnt ||
                        hn->pic_order_cnt > hc->pic_order_cnt)
                        break;
                } else {
                    if (hn->pic_order_cnt < hc->pic_order_cnt)
                        break;
                }
                rpl1[j] = rpl1[j - 1];
            }
            rpl1[j] = prev->dpb[i];
        }

        ++n;
    }

    if (pic->type == PICTURE_TYPE_B) {
        for (i = 0; i < n; i++) {
            if (rpl0[i] != rpl1[i])
                break;
        }
        if (i == n)
            FFSWAP(VAAPIEncodePicture*, rpl1[0], rpl1[1]);
    }

    if (pic->type == PICTURE_TYPE_P ||
        pic->type == PICTURE_TYPE_B) {
        av_log(avctx, AV_LOG_DEBUG, "Default RefPicList0 for fn=%d/poc=%d:",
               hp->frame_num, hp->pic_order_cnt);
        for (i = 0; i < n; i++) {
            hn = rpl0[i]->priv_data;
            av_log(avctx, AV_LOG_DEBUG, "  fn=%d/poc=%d",
                   hn->frame_num, hn->pic_order_cnt);
        }
        av_log(avctx, AV_LOG_DEBUG, "\n");
    }
    if (pic->type == PICTURE_TYPE_B) {
        av_log(avctx, AV_LOG_DEBUG, "Default RefPicList1 for fn=%d/poc=%d:",
               hp->frame_num, hp->pic_order_cnt);
        for (i = 0; i < n; i++) {
            hn = rpl1[i]->priv_data;
            av_log(avctx, AV_LOG_DEBUG, "  fn=%d/poc=%d",
                   hn->frame_num, hn->pic_order_cnt);
        }
        av_log(avctx, AV_LOG_DEBUG, "\n");
    }

    *rpl_size = n;
}

static int vaapi_encode_h264_init_slice_params(AVCodecContext *avctx,
                                               VAAPIEncodePicture *pic,
                                               VAAPIEncodeSlice *slice)
{
    VAAPIEncodeH264Context          *priv = avctx->priv_data;
    VAAPIEncodeH264Picture          *hpic = pic->priv_data;
    VAAPIEncodePicture              *prev = pic->prev;
    H264RawSPS                       *sps = &priv->raw_sps;
    H264RawPPS                       *pps = &priv->raw_pps;
    H264RawSliceHeader                *sh = &priv->raw_slice.header;
    VAEncPictureParameterBufferH264 *vpic = pic->codec_picture_params;
    VAEncSliceParameterBufferH264 *vslice = slice->codec_slice_params;
    int i, j;

    if (pic->type == PICTURE_TYPE_IDR) {
        sh->nal_unit_header.nal_unit_type = H264_NAL_IDR_SLICE;
        sh->nal_unit_header.nal_ref_idc   = 3;
    } else {
        sh->nal_unit_header.nal_unit_type = H264_NAL_SLICE;
        sh->nal_unit_header.nal_ref_idc   = pic->is_reference;
    }

    sh->first_mb_in_slice = slice->block_start;
    sh->slice_type        = hpic->slice_type;

    sh->pic_parameter_set_id = pps->pic_parameter_set_id;

    sh->frame_num = hpic->frame_num &
        ((1 << (4 + sps->log2_max_frame_num_minus4)) - 1);
    sh->idr_pic_id = hpic->idr_pic_id;
    sh->pic_order_cnt_lsb = hpic->pic_order_cnt &
        ((1 << (4 + sps->log2_max_pic_order_cnt_lsb_minus4)) - 1);

    sh->direct_spatial_mv_pred_flag = 1;

    if (pic->type == PICTURE_TYPE_B)
        sh->slice_qp_delta = priv->fixed_qp_b - (pps->pic_init_qp_minus26 + 26);
    else if (pic->type == PICTURE_TYPE_P)
        sh->slice_qp_delta = priv->fixed_qp_p - (pps->pic_init_qp_minus26 + 26);
    else
        sh->slice_qp_delta = priv->fixed_qp_idr - (pps->pic_init_qp_minus26 + 26);

    if (pic->is_reference && pic->type != PICTURE_TYPE_IDR) {
        VAAPIEncodePicture *discard_list[MAX_DPB_SIZE];
        int discard = 0, keep = 0;

        // Discard everything which is in the DPB of the previous frame but
        // not in the DPB of this one.
        for (i = 0; i < prev->nb_dpb_pics; i++) {
            for (j = 0; j < pic->nb_dpb_pics; j++) {
                if (prev->dpb[i] == pic->dpb[j])
                    break;
            }
            if (j == pic->nb_dpb_pics) {
                discard_list[discard] = prev->dpb[i];
                ++discard;
            } else {
                ++keep;
            }
        }
        av_assert0(keep <= priv->dpb_frames);

        if (discard == 0) {
            sh->adaptive_ref_pic_marking_mode_flag = 0;
        } else {
            sh->adaptive_ref_pic_marking_mode_flag = 1;
            for (i = 0; i < discard; i++) {
                VAAPIEncodeH264Picture *old = discard_list[i]->priv_data;
                av_assert0(old->frame_num < hpic->frame_num);
                sh->mmco[i].memory_management_control_operation = 1;
                sh->mmco[i].difference_of_pic_nums_minus1 =
                    hpic->frame_num - old->frame_num - 1;
            }
            sh->mmco[i].memory_management_control_operation = 0;
        }
    }

    // If the intended references are not the first entries of RefPicListN
    // by default, use ref-pic-list-modification to move them there.
    if (pic->type == PICTURE_TYPE_P || pic->type == PICTURE_TYPE_B) {
        VAAPIEncodePicture *def_l0[MAX_DPB_SIZE], *def_l1[MAX_DPB_SIZE];
        VAAPIEncodeH264Picture *href;
        int n;

        vaapi_encode_h264_default_ref_pic_list(avctx, pic,
                                               def_l0, def_l1, &n);

        if (pic->type == PICTURE_TYPE_P) {
            int need_rplm = 0;
            for (i = 0; i < pic->nb_refs; i++) {
                av_assert0(pic->refs[i]);
                if (pic->refs[i] != def_l0[i])
                    need_rplm = 1;
            }

            sh->ref_pic_list_modification_flag_l0 = need_rplm;
            if (need_rplm) {
                int pic_num = hpic->frame_num;
                for (i = 0; i < pic->nb_refs; i++) {
                    href = pic->refs[i]->priv_data;
                    av_assert0(href->frame_num != pic_num);
                    if (href->frame_num < pic_num) {
                        sh->rplm_l0[i].modification_of_pic_nums_idc = 0;
                        sh->rplm_l0[i].abs_diff_pic_num_minus1 =
                            pic_num - href->frame_num - 1;
                    } else {
                        sh->rplm_l0[i].modification_of_pic_nums_idc = 1;
                        sh->rplm_l0[i].abs_diff_pic_num_minus1 =
                            href->frame_num - pic_num - 1;
                    }
                    pic_num = href->frame_num;
                }
                sh->rplm_l0[i].modification_of_pic_nums_idc = 3;
            }

        } else {
            int need_rplm_l0 = 0, need_rplm_l1 = 0;
            int n0 = 0, n1 = 0;
            for (i = 0; i < pic->nb_refs; i++) {
                av_assert0(pic->refs[i]);
                href = pic->refs[i]->priv_data;
                av_assert0(href->pic_order_cnt != hpic->pic_order_cnt);
                if (href->pic_order_cnt < hpic->pic_order_cnt) {
                    if (pic->refs[i] != def_l0[n0])
                        need_rplm_l0 = 1;
                    ++n0;
                } else {
                    if (pic->refs[i] != def_l1[n1])
                        need_rplm_l1 = 1;
                    ++n1;
                }
            }

            sh->ref_pic_list_modification_flag_l0 = need_rplm_l0;
            if (need_rplm_l0) {
                int pic_num = hpic->frame_num;
                for (i = j = 0; i < pic->nb_refs; i++) {
                    href = pic->refs[i]->priv_data;
                    if (href->pic_order_cnt > hpic->pic_order_cnt)
                        continue;
                    av_assert0(href->frame_num != pic_num);
                    if (href->frame_num < pic_num) {
                        sh->rplm_l0[j].modification_of_pic_nums_idc = 0;
                        sh->rplm_l0[j].abs_diff_pic_num_minus1 =
                            pic_num - href->frame_num - 1;
                    } else {
                        sh->rplm_l0[j].modification_of_pic_nums_idc = 1;
                        sh->rplm_l0[j].abs_diff_pic_num_minus1 =
                            href->frame_num - pic_num - 1;
                    }
                    pic_num = href->frame_num;
                    ++j;
                }
                av_assert0(j == n0);
                sh->rplm_l0[j].modification_of_pic_nums_idc = 3;
            }

            sh->ref_pic_list_modification_flag_l1 = need_rplm_l1;
            if (need_rplm_l1) {
                int pic_num = hpic->frame_num;
                for (i = j = 0; i < pic->nb_refs; i++) {
                    href = pic->refs[i]->priv_data;
                    if (href->pic_order_cnt < hpic->pic_order_cnt)
                        continue;
                    av_assert0(href->frame_num != pic_num);
                    if (href->frame_num < pic_num) {
                        sh->rplm_l1[j].modification_of_pic_nums_idc = 0;
                        sh->rplm_l1[j].abs_diff_pic_num_minus1 =
                            pic_num - href->frame_num - 1;
                    } else {
                        sh->rplm_l1[j].modification_of_pic_nums_idc = 1;
                        sh->rplm_l1[j].abs_diff_pic_num_minus1 =
                            href->frame_num - pic_num - 1;
                    }
                    pic_num = href->frame_num;
                    ++j;
                }
                av_assert0(j == n1);
                sh->rplm_l1[j].modification_of_pic_nums_idc = 3;
            }
        }
    }

    vslice->macroblock_address = slice->block_start;
    vslice->num_macroblocks    = slice->block_size;

    vslice->macroblock_info = VA_INVALID_ID;

    vslice->slice_type           = sh->slice_type % 5;
    vslice->pic_parameter_set_id = sh->pic_parameter_set_id;
    vslice->idr_pic_id           = sh->idr_pic_id;

    vslice->pic_order_cnt_lsb = sh->pic_order_cnt_lsb;

    vslice->direct_spatial_mv_pred_flag = sh->direct_spatial_mv_pred_flag;

    for (i = 0; i < FF_ARRAY_ELEMS(vslice->RefPicList0); i++) {
        vslice->RefPicList0[i].picture_id = VA_INVALID_ID;
        vslice->RefPicList0[i].flags      = VA_PICTURE_H264_INVALID;
        vslice->RefPicList1[i].picture_id = VA_INVALID_ID;
        vslice->RefPicList1[i].flags      = VA_PICTURE_H264_INVALID;
    }

    av_assert0(pic->nb_refs <= 2);
    if (pic->nb_refs >= 1) {
        // Backward reference for P- or B-frame.
        av_assert0(pic->type == PICTURE_TYPE_P ||
                   pic->type == PICTURE_TYPE_B);
        vslice->RefPicList0[0] = vpic->ReferenceFrames[0];
    }
    if (pic->nb_refs >= 2) {
        // Forward reference for B-frame.
        av_assert0(pic->type == PICTURE_TYPE_B);
        vslice->RefPicList1[0] = vpic->ReferenceFrames[1];
    }

    vslice->slice_qp_delta = sh->slice_qp_delta;

    return 0;
}

static av_cold int vaapi_encode_h264_configure(AVCodecContext *avctx)
{
    VAAPIEncodeContext      *ctx = avctx->priv_data;
    VAAPIEncodeH264Context *priv = avctx->priv_data;
    int err;

    err = ff_cbs_init(&priv->cbc, AV_CODEC_ID_H264, avctx);
    if (err < 0)
        return err;

    priv->mb_width  = FFALIGN(avctx->width,  16) / 16;
    priv->mb_height = FFALIGN(avctx->height, 16) / 16;

    if (ctx->va_rc_mode == VA_RC_CQP) {
        priv->fixed_qp_p = av_clip(ctx->rc_quality, 1, 51);
        if (avctx->i_quant_factor > 0.0)
            priv->fixed_qp_idr =
                av_clip((avctx->i_quant_factor * priv->fixed_qp_p +
                         avctx->i_quant_offset) + 0.5, 1, 51);
        else
            priv->fixed_qp_idr = priv->fixed_qp_p;
        if (avctx->b_quant_factor > 0.0)
            priv->fixed_qp_b =
                av_clip((avctx->b_quant_factor * priv->fixed_qp_p +
                         avctx->b_quant_offset) + 0.5, 1, 51);
        else
            priv->fixed_qp_b = priv->fixed_qp_p;

        av_log(avctx, AV_LOG_DEBUG, "Using fixed QP = "
               "%d / %d / %d for IDR- / P- / B-frames.\n",
               priv->fixed_qp_idr, priv->fixed_qp_p, priv->fixed_qp_b);

    } else {
        // These still need to be  set for pic_init_qp/slice_qp_delta.
        priv->fixed_qp_idr = 26;
        priv->fixed_qp_p   = 26;
        priv->fixed_qp_b   = 26;
    }

    if (!ctx->rc_mode->hrd) {
        // Timing SEI requires a mode respecting HRD parameters.
        priv->sei &= ~SEI_TIMING;
    }

    if (priv->sei & SEI_IDENTIFIER) {
        const char *lavc  = LIBAVCODEC_IDENT;
        const char *vaapi = VA_VERSION_S;
        const char *driver;
        int len;

        memcpy(priv->sei_identifier.uuid_iso_iec_11578,
               vaapi_encode_h264_sei_identifier_uuid,
               sizeof(priv->sei_identifier.uuid_iso_iec_11578));

        driver = vaQueryVendorString(ctx->hwctx->display);
        if (!driver)
            driver = "unknown driver";

        len = snprintf(NULL, 0, "%s / VAAPI %s / %s", lavc, vaapi, driver);
        if (len >= 0) {
            priv->sei_identifier_string = av_malloc(len + 1);
            if (!priv->sei_identifier_string)
                return AVERROR(ENOMEM);

            snprintf(priv->sei_identifier_string, len + 1,
                     "%s / VAAPI %s / %s", lavc, vaapi, driver);

            priv->sei_identifier.data        = priv->sei_identifier_string;
            priv->sei_identifier.data_length = len + 1;
        }
    }

    ctx->roi_quant_range = 51 + 6 * (ctx->profile->depth - 8);

    return 0;
}

static const VAAPIEncodeProfile vaapi_encode_h264_profiles[] = {
    { FF_PROFILE_H264_HIGH, 8, 3, 1, 1, VAProfileH264High },
    { FF_PROFILE_H264_MAIN, 8, 3, 1, 1, VAProfileH264Main },
    { FF_PROFILE_H264_CONSTRAINED_BASELINE,
                            8, 3, 1, 1, VAProfileH264ConstrainedBaseline },
    { FF_PROFILE_UNKNOWN }
};

static const VAAPIEncodeType vaapi_encode_type_h264 = {
    .profiles              = vaapi_encode_h264_profiles,

    .flags                 = FLAG_SLICE_CONTROL |
                             FLAG_B_PICTURES |
                             FLAG_B_PICTURE_REFERENCES |
                             FLAG_NON_IDR_KEY_PICTURES,

    .default_quality       = 20,

    .configure             = &vaapi_encode_h264_configure,

    .picture_priv_data_size = sizeof(VAAPIEncodeH264Picture),

    .sequence_params_size  = sizeof(VAEncSequenceParameterBufferH264),
    .init_sequence_params  = &vaapi_encode_h264_init_sequence_params,

    .picture_params_size   = sizeof(VAEncPictureParameterBufferH264),
    .init_picture_params   = &vaapi_encode_h264_init_picture_params,

    .slice_params_size     = sizeof(VAEncSliceParameterBufferH264),
    .init_slice_params     = &vaapi_encode_h264_init_slice_params,

    .sequence_header_type  = VAEncPackedHeaderSequence,
    .write_sequence_header = &vaapi_encode_h264_write_sequence_header,

    .slice_header_type     = VAEncPackedHeaderH264_Slice,
    .write_slice_header    = &vaapi_encode_h264_write_slice_header,

    .write_extra_header    = &vaapi_encode_h264_write_extra_header,
};

static av_cold int vaapi_encode_h264_init(AVCodecContext *avctx)
{
    VAAPIEncodeContext      *ctx = avctx->priv_data;
    VAAPIEncodeH264Context *priv = avctx->priv_data;

    ctx->codec = &vaapi_encode_type_h264;

    if (avctx->profile == FF_PROFILE_UNKNOWN)
        avctx->profile = priv->profile;
    if (avctx->level == FF_LEVEL_UNKNOWN)
        avctx->level = priv->level;
    if (avctx->compression_level == FF_COMPRESSION_DEFAULT)
        avctx->compression_level = priv->quality;

    // Reject unsupported profiles.
    switch (avctx->profile) {
    case FF_PROFILE_H264_BASELINE:
        av_log(avctx, AV_LOG_WARNING, "H.264 baseline profile is not "
               "supported, using constrained baseline profile instead.\n");
        avctx->profile = FF_PROFILE_H264_CONSTRAINED_BASELINE;
        break;
    case FF_PROFILE_H264_EXTENDED:
        av_log(avctx, AV_LOG_ERROR, "H.264 extended profile "
               "is not supported.\n");
        return AVERROR_PATCHWELCOME;
    case FF_PROFILE_H264_HIGH_10:
    case FF_PROFILE_H264_HIGH_10_INTRA:
        av_log(avctx, AV_LOG_ERROR, "H.264 10-bit profiles "
               "are not supported.\n");
        return AVERROR_PATCHWELCOME;
    case FF_PROFILE_H264_HIGH_422:
    case FF_PROFILE_H264_HIGH_422_INTRA:
    case FF_PROFILE_H264_HIGH_444:
    case FF_PROFILE_H264_HIGH_444_PREDICTIVE:
    case FF_PROFILE_H264_HIGH_444_INTRA:
    case FF_PROFILE_H264_CAVLC_444:
        av_log(avctx, AV_LOG_ERROR, "H.264 non-4:2:0 profiles "
               "are not supported.\n");
        return AVERROR_PATCHWELCOME;
    }

    if (avctx->level != FF_LEVEL_UNKNOWN && avctx->level & ~0xff) {
        av_log(avctx, AV_LOG_ERROR, "Invalid level %d: must fit "
               "in 8-bit unsigned integer.\n", avctx->level);
        return AVERROR(EINVAL);
    }

    ctx->desired_packed_headers =
        VA_ENC_PACKED_HEADER_SEQUENCE | // SPS and PPS.
        VA_ENC_PACKED_HEADER_SLICE    | // Slice headers.
        VA_ENC_PACKED_HEADER_MISC;      // SEI.

    ctx->surface_width  = FFALIGN(avctx->width,  16);
    ctx->surface_height = FFALIGN(avctx->height, 16);

    ctx->slice_block_height = ctx->slice_block_width = 16;

    if (priv->qp > 0)
        ctx->explicit_qp = priv->qp;

    return ff_vaapi_encode_init(avctx);
}

static av_cold int vaapi_encode_h264_close(AVCodecContext *avctx)
{
    VAAPIEncodeH264Context *priv = avctx->priv_data;

    ff_cbs_fragment_free(&priv->current_access_unit);
    ff_cbs_close(&priv->cbc);
    av_freep(&priv->sei_identifier_string);

    return ff_vaapi_encode_close(avctx);
}

#define OFFSET(x) offsetof(VAAPIEncodeH264Context, x)
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM)
static const AVOption vaapi_encode_h264_options[] = {
    VAAPI_ENCODE_COMMON_OPTIONS,
    VAAPI_ENCODE_RC_OPTIONS,

    { "qp", "Constant QP (for P-frames; scaled by qfactor/qoffset for I/B)",
      OFFSET(qp), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 52, FLAGS },
    { "quality", "Set encode quality (trades off against speed, higher is faster)",
      OFFSET(quality), AV_OPT_TYPE_INT, { .i64 = -1 }, -1, INT_MAX, FLAGS },
    { "coder", "Entropy coder type",
      OFFSET(coder), AV_OPT_TYPE_INT, { .i64 = 1 }, 0, 1, FLAGS, "coder" },
        { "cavlc", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 0 }, INT_MIN, INT_MAX, FLAGS, "coder" },
        { "cabac", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 1 }, INT_MIN, INT_MAX, FLAGS, "coder" },
        { "vlc",   NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 0 }, INT_MIN, INT_MAX, FLAGS, "coder" },
        { "ac",    NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 1 }, INT_MIN, INT_MAX, FLAGS, "coder" },

    { "aud", "Include AUD",
      OFFSET(aud), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, FLAGS },

    { "sei", "Set SEI to include",
      OFFSET(sei), AV_OPT_TYPE_FLAGS,
      { .i64 = SEI_IDENTIFIER | SEI_TIMING | SEI_RECOVERY_POINT },
      0, INT_MAX, FLAGS, "sei" },
    { "identifier", "Include encoder version identifier",
      0, AV_OPT_TYPE_CONST, { .i64 = SEI_IDENTIFIER },
      INT_MIN, INT_MAX, FLAGS, "sei" },
    { "timing", "Include timing parameters (buffering_period and pic_timing)",
      0, AV_OPT_TYPE_CONST, { .i64 = SEI_TIMING },
      INT_MIN, INT_MAX, FLAGS, "sei" },
    { "recovery_point", "Include recovery points where appropriate",
      0, AV_OPT_TYPE_CONST, { .i64 = SEI_RECOVERY_POINT },
      INT_MIN, INT_MAX, FLAGS, "sei" },

    { "profile", "Set profile (profile_idc and constraint_set*_flag)",
      OFFSET(profile), AV_OPT_TYPE_INT,
      { .i64 = FF_PROFILE_UNKNOWN }, FF_PROFILE_UNKNOWN, 0xffff, FLAGS, "profile" },

#define PROFILE(name, value)  name, NULL, 0, AV_OPT_TYPE_CONST, \
      { .i64 = value }, 0, 0, FLAGS, "profile"
    { PROFILE("constrained_baseline", FF_PROFILE_H264_CONSTRAINED_BASELINE) },
    { PROFILE("main",                 FF_PROFILE_H264_MAIN) },
    { PROFILE("high",                 FF_PROFILE_H264_HIGH) },
#undef PROFILE

    { "level", "Set level (level_idc)",
      OFFSET(level), AV_OPT_TYPE_INT,
      { .i64 = FF_LEVEL_UNKNOWN }, FF_LEVEL_UNKNOWN, 0xff, FLAGS, "level" },

#define LEVEL(name, value) name, NULL, 0, AV_OPT_TYPE_CONST, \
      { .i64 = value }, 0, 0, FLAGS, "level"
    { LEVEL("1",   10) },
    { LEVEL("1.1", 11) },
    { LEVEL("1.2", 12) },
    { LEVEL("1.3", 13) },
    { LEVEL("2",   20) },
    { LEVEL("2.1", 21) },
    { LEVEL("2.2", 22) },
    { LEVEL("3",   30) },
    { LEVEL("3.1", 31) },
    { LEVEL("3.2", 32) },
    { LEVEL("4",   40) },
    { LEVEL("4.1", 41) },
    { LEVEL("4.2", 42) },
    { LEVEL("5",   50) },
    { LEVEL("5.1", 51) },
    { LEVEL("5.2", 52) },
    { LEVEL("6",   60) },
    { LEVEL("6.1", 61) },
    { LEVEL("6.2", 62) },
#undef LEVEL

    { NULL },
};

static const AVCodecDefault vaapi_encode_h264_defaults[] = {
    { "b",              "0"   },
    { "bf",             "2"   },
    { "g",              "120" },
    { "i_qfactor",      "1"   },
    { "i_qoffset",      "0"   },
    { "b_qfactor",      "6/5" },
    { "b_qoffset",      "0"   },
    { "qmin",           "-1"  },
    { "qmax",           "-1"  },
    { NULL },
};

static const AVClass vaapi_encode_h264_class = {
    .class_name = "h264_vaapi",
    .item_name  = av_default_item_name,
    .option     = vaapi_encode_h264_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const AVCodec ff_h264_vaapi_encoder = {
    .name           = "h264_vaapi",
    .long_name      = NULL_IF_CONFIG_SMALL("H.264/AVC (VAAPI)"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_H264,
    .priv_data_size = sizeof(VAAPIEncodeH264Context),
    .init           = &vaapi_encode_h264_init,
    .receive_packet = &ff_vaapi_encode_receive_packet,
    .close          = &vaapi_encode_h264_close,
    .priv_class     = &vaapi_encode_h264_class,
    .capabilities   = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_HARDWARE |
                      AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
    .defaults       = vaapi_encode_h264_defaults,
    .pix_fmts = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_VAAPI,
        AV_PIX_FMT_NONE,
    },
    .hw_configs     = ff_vaapi_encode_hw_configs,
    .wrapper_name   = "vaapi",
};
