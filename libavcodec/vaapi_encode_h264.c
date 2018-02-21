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

typedef struct VAAPIEncodeH264Context {
    int mb_width;
    int mb_height;

    int fixed_qp_idr;
    int fixed_qp_p;
    int fixed_qp_b;

    H264RawAUD aud;
    H264RawSPS sps;
    H264RawPPS pps;
    H264RawSEI sei;
    H264RawSlice slice;

    H264RawSEIBufferingPeriod buffering_period;
    H264RawSEIPicTiming pic_timing;
    H264RawSEIRecoveryPoint recovery_point;
    H264RawSEIUserDataUnregistered identifier;
    char *identifier_string;

    int frame_num;
    int pic_order_cnt;
    int next_frame_num;
    int64_t last_idr_frame;
    int64_t idr_pic_count;

    int primary_pic_type;
    int slice_type;

    int cpb_delay;
    int dpb_delay;

    CodedBitstreamContext *cbc;
    CodedBitstreamFragment current_access_unit;
    int aud_needed;
    int sei_needed;
    int sei_cbr_workaround_needed;
} VAAPIEncodeH264Context;

typedef struct VAAPIEncodeH264Options {
    int qp;
    int quality;
    int low_power;
    // Entropy encoder type.
    int coder;
    int aud;
    int sei;
    int profile;
    int level;
} VAAPIEncodeH264Options;


static int vaapi_encode_h264_write_access_unit(AVCodecContext *avctx,
                                               char *data, size_t *data_len,
                                               CodedBitstreamFragment *au)
{
    VAAPIEncodeContext      *ctx = avctx->priv_data;
    VAAPIEncodeH264Context *priv = ctx->priv_data;
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
    VAAPIEncodeContext      *ctx = avctx->priv_data;
    VAAPIEncodeH264Context *priv = ctx->priv_data;
    H264RawNALUnitHeader *header = nal_unit;
    int err;

    err = ff_cbs_insert_unit_content(priv->cbc, au, -1,
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
    VAAPIEncodeContext      *ctx = avctx->priv_data;
    VAAPIEncodeH264Context *priv = ctx->priv_data;
    CodedBitstreamFragment   *au = &priv->current_access_unit;
    int err;

    if (priv->aud_needed) {
        err = vaapi_encode_h264_add_nal(avctx, au, &priv->aud);
        if (err < 0)
            goto fail;
        priv->aud_needed = 0;
    }

    err = vaapi_encode_h264_add_nal(avctx, au, &priv->sps);
    if (err < 0)
        goto fail;

    err = vaapi_encode_h264_add_nal(avctx, au, &priv->pps);
    if (err < 0)
        goto fail;

    err = vaapi_encode_h264_write_access_unit(avctx, data, data_len, au);
fail:
    ff_cbs_fragment_uninit(priv->cbc, au);
    return err;
}

static int vaapi_encode_h264_write_slice_header(AVCodecContext *avctx,
                                                VAAPIEncodePicture *pic,
                                                VAAPIEncodeSlice *slice,
                                                char *data, size_t *data_len)
{
    VAAPIEncodeContext      *ctx = avctx->priv_data;
    VAAPIEncodeH264Context *priv = ctx->priv_data;
    CodedBitstreamFragment   *au = &priv->current_access_unit;
    int err;

    if (priv->aud_needed) {
        err = vaapi_encode_h264_add_nal(avctx, au, &priv->aud);
        if (err < 0)
            goto fail;
        priv->aud_needed = 0;
    }

    err = vaapi_encode_h264_add_nal(avctx, au, &priv->slice);
    if (err < 0)
        goto fail;

    err = vaapi_encode_h264_write_access_unit(avctx, data, data_len, au);
fail:
    ff_cbs_fragment_uninit(priv->cbc, au);
    return err;
}

static int vaapi_encode_h264_write_extra_header(AVCodecContext *avctx,
                                                VAAPIEncodePicture *pic,
                                                int index, int *type,
                                                char *data, size_t *data_len)
{
    VAAPIEncodeContext      *ctx = avctx->priv_data;
    VAAPIEncodeH264Context *priv = ctx->priv_data;
    VAAPIEncodeH264Options  *opt = ctx->codec_options;
    CodedBitstreamFragment   *au = &priv->current_access_unit;
    int err, i;

    if (priv->sei_needed) {
        if (priv->aud_needed) {
            err = vaapi_encode_h264_add_nal(avctx, au, &priv->aud);
            if (err < 0)
                goto fail;
            priv->aud_needed = 0;
        }

        memset(&priv->sei, 0, sizeof(priv->sei));
        priv->sei.nal_unit_header.nal_unit_type = H264_NAL_SEI;

        i = 0;
        if (pic->encode_order == 0 && opt->sei & SEI_IDENTIFIER) {
            priv->sei.payload[i].payload_type = H264_SEI_TYPE_USER_DATA_UNREGISTERED;
            priv->sei.payload[i].payload.user_data_unregistered = priv->identifier;
            ++i;
        }
        if (opt->sei & SEI_TIMING) {
            if (pic->type == PICTURE_TYPE_IDR) {
                priv->sei.payload[i].payload_type = H264_SEI_TYPE_BUFFERING_PERIOD;
                priv->sei.payload[i].payload.buffering_period = priv->buffering_period;
                ++i;
            }
            priv->sei.payload[i].payload_type = H264_SEI_TYPE_PIC_TIMING;
            priv->sei.payload[i].payload.pic_timing = priv->pic_timing;
            ++i;
        }
        if (opt->sei & SEI_RECOVERY_POINT && pic->type == PICTURE_TYPE_I) {
            priv->sei.payload[i].payload_type = H264_SEI_TYPE_RECOVERY_POINT;
            priv->sei.payload[i].payload.recovery_point = priv->recovery_point;
            ++i;
        }

        priv->sei.payload_count = i;
        av_assert0(priv->sei.payload_count > 0);

        err = vaapi_encode_h264_add_nal(avctx, au, &priv->sei);
        if (err < 0)
            goto fail;
        priv->sei_needed = 0;

        err = vaapi_encode_h264_write_access_unit(avctx, data, data_len, au);
        if (err < 0)
            goto fail;

        ff_cbs_fragment_uninit(priv->cbc, au);

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
    ff_cbs_fragment_uninit(priv->cbc, au);
    return err;
}

static int vaapi_encode_h264_init_sequence_params(AVCodecContext *avctx)
{
    VAAPIEncodeContext                *ctx = avctx->priv_data;
    VAAPIEncodeH264Context           *priv = ctx->priv_data;
    VAAPIEncodeH264Options            *opt = ctx->codec_options;
    H264RawSPS                        *sps = &priv->sps;
    H264RawPPS                        *pps = &priv->pps;
    VAEncSequenceParameterBufferH264 *vseq = ctx->codec_sequence_params;
    VAEncPictureParameterBufferH264  *vpic = ctx->codec_picture_params;

    memset(&priv->current_access_unit, 0,
           sizeof(priv->current_access_unit));

    memset(sps, 0, sizeof(*sps));
    memset(pps, 0, sizeof(*pps));

    sps->nal_unit_header.nal_ref_idc   = 3;
    sps->nal_unit_header.nal_unit_type = H264_NAL_SPS;

    sps->profile_idc = avctx->profile & 0xff;
    sps->constraint_set1_flag =
        !!(avctx->profile & FF_PROFILE_H264_CONSTRAINED);
    sps->constraint_set3_flag =
        !!(avctx->profile & FF_PROFILE_H264_INTRA);

    sps->level_idc = avctx->level;

    sps->seq_parameter_set_id = 0;
    sps->chroma_format_idc    = 1;

    sps->log2_max_frame_num_minus4 = 4;
    sps->pic_order_cnt_type        = 0;
    sps->log2_max_pic_order_cnt_lsb_minus4 =
        av_clip(av_log2(ctx->b_per_p + 1) - 2, 0, 12);

    sps->max_num_ref_frames =
        (avctx->profile & FF_PROFILE_H264_INTRA) ? 0 :
        1 + (ctx->b_per_p > 0);

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
        int i;
        for (i = 0; i < FF_ARRAY_ELEMS(sar_idc); i++) {
            if (avctx->sample_aspect_ratio.num == sar_idc[i].num &&
                avctx->sample_aspect_ratio.den == sar_idc[i].den) {
                sps->vui.aspect_ratio_idc = i;
                break;
            }
        }
        if (i >= FF_ARRAY_ELEMS(sar_idc)) {
            sps->vui.aspect_ratio_idc = 255;
            sps->vui.sar_width  = avctx->sample_aspect_ratio.num;
            sps->vui.sar_height = avctx->sample_aspect_ratio.den;
        }
        sps->vui.aspect_ratio_info_present_flag = 1;
    }

    if (avctx->color_range     != AVCOL_RANGE_UNSPECIFIED ||
        avctx->color_primaries != AVCOL_PRI_UNSPECIFIED ||
        avctx->color_trc       != AVCOL_TRC_UNSPECIFIED ||
        avctx->colorspace      != AVCOL_SPC_UNSPECIFIED) {
        sps->vui.video_signal_type_present_flag = 1;
        sps->vui.video_format      = 5; // Unspecified.
        sps->vui.video_full_range_flag =
            avctx->color_range == AVCOL_RANGE_JPEG;

        if (avctx->color_primaries != AVCOL_PRI_UNSPECIFIED ||
            avctx->color_trc       != AVCOL_TRC_UNSPECIFIED ||
            avctx->colorspace      != AVCOL_SPC_UNSPECIFIED) {
            sps->vui.colour_description_present_flag = 1;
            sps->vui.colour_primaries         = avctx->color_primaries;
            sps->vui.transfer_characteristics = avctx->color_trc;
            sps->vui.matrix_coefficients      = avctx->colorspace;
        }
    } else {
        sps->vui.video_format             = 5;
        sps->vui.video_full_range_flag    = 0;
        sps->vui.colour_primaries         = avctx->color_primaries;
        sps->vui.transfer_characteristics = avctx->color_trc;
        sps->vui.matrix_coefficients      = avctx->colorspace;
    }

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

    if (opt->sei & SEI_TIMING) {
        H264RawHRD *hrd = &sps->vui.nal_hrd_parameters;

        sps->vui.nal_hrd_parameters_present_flag = 1;

        hrd->cpb_cnt_minus1 = 0;

        // Try to scale these to a sensible range so that the
        // golomb encode of the value is not overlong.
        hrd->bit_rate_scale =
            av_clip_uintp2(av_log2(avctx->bit_rate) - 15 - 6, 4);
        hrd->bit_rate_value_minus1[0] =
            (avctx->bit_rate >> hrd->bit_rate_scale + 6) - 1;

        hrd->cpb_size_scale =
            av_clip_uintp2(av_log2(ctx->hrd_params.hrd.buffer_size) - 15 - 4, 4);
        hrd->cpb_size_value_minus1[0] =
            (ctx->hrd_params.hrd.buffer_size >> hrd->cpb_size_scale + 4) - 1;

        // CBR mode as defined for the HRD cannot be achieved without filler
        // data, so this flag cannot be set even with VAAPI CBR modes.
        hrd->cbr_flag[0] = 0;

        hrd->initial_cpb_removal_delay_length_minus1 = 23;
        hrd->cpb_removal_delay_length_minus1         = 23;
        hrd->dpb_output_delay_length_minus1          = 7;
        hrd->time_offset_length                      = 0;

        priv->buffering_period.seq_parameter_set_id = sps->seq_parameter_set_id;

        // This calculation can easily overflow 32 bits.
        priv->buffering_period.nal.initial_cpb_removal_delay[0] = 90000 *
            (uint64_t)ctx->hrd_params.hrd.initial_buffer_fullness /
            ctx->hrd_params.hrd.buffer_size;
        priv->buffering_period.nal.initial_cpb_removal_delay_offset[0] = 0;
    } else {
        sps->vui.nal_hrd_parameters_present_flag = 0;
        sps->vui.low_delay_hrd_flag = 1 - sps->vui.fixed_frame_rate_flag;
    }

    sps->vui.bitstream_restriction_flag    = 1;
    sps->vui.motion_vectors_over_pic_boundaries_flag = 1;
    sps->vui.log2_max_mv_length_horizontal = 16;
    sps->vui.log2_max_mv_length_vertical   = 16;
    sps->vui.max_num_reorder_frames        = (ctx->b_per_p > 0);
    sps->vui.max_dec_frame_buffering       = sps->max_num_ref_frames;

    pps->nal_unit_header.nal_ref_idc = 3;
    pps->nal_unit_header.nal_unit_type = H264_NAL_PPS;

    pps->pic_parameter_set_id = 0;
    pps->seq_parameter_set_id = 0;

    pps->entropy_coding_mode_flag =
        !(sps->profile_idc == FF_PROFILE_H264_BASELINE ||
          sps->profile_idc == FF_PROFILE_H264_EXTENDED ||
          sps->profile_idc == FF_PROFILE_H264_CAVLC_444);
    if (!opt->coder && pps->entropy_coding_mode_flag)
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
        .intra_period     = avctx->gop_size,
        .intra_idr_period = avctx->gop_size,
        .ip_period        = ctx->b_per_p + 1,

        .bits_per_second       = avctx->bit_rate,
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
    VAAPIEncodeH264Context          *priv = ctx->priv_data;
    VAAPIEncodeH264Options           *opt = ctx->codec_options;
    H264RawSPS                       *sps = &priv->sps;
    VAEncPictureParameterBufferH264 *vpic = pic->codec_picture_params;
    int i;

    memset(&priv->current_access_unit, 0,
           sizeof(priv->current_access_unit));

    if (pic->type == PICTURE_TYPE_IDR) {
        av_assert0(pic->display_order == pic->encode_order);
        priv->frame_num      = 0;
        priv->next_frame_num = 1;
        priv->cpb_delay      = 0;
        priv->last_idr_frame = pic->display_order;
        ++priv->idr_pic_count;

        priv->slice_type       = 7;
        priv->primary_pic_type = 0;
    } else {
        priv->frame_num      = priv->next_frame_num;

        if (pic->type != PICTURE_TYPE_B) {
            // Reference picture, so frame_num advances.
            priv->next_frame_num = (priv->frame_num + 1) &
                ((1 << (4 + sps->log2_max_frame_num_minus4)) - 1);
        }
        ++priv->cpb_delay;

        if (pic->type == PICTURE_TYPE_I) {
            priv->slice_type       = 7;
            priv->primary_pic_type = 0;
        } else if (pic->type == PICTURE_TYPE_P) {
            priv->slice_type       = 5;
            priv->primary_pic_type = 1;
        } else {
            priv->slice_type       = 6;
            priv->primary_pic_type = 2;
        }
    }
    priv->pic_order_cnt = pic->display_order - priv->last_idr_frame;
    priv->dpb_delay     = pic->display_order - pic->encode_order + 1;

    if (opt->aud) {
        priv->aud_needed = 1;
        priv->aud.nal_unit_header.nal_unit_type = H264_NAL_AUD;
        priv->aud.primary_pic_type = priv->primary_pic_type;
    } else {
        priv->aud_needed = 0;
    }

    if (opt->sei & SEI_IDENTIFIER && pic->encode_order == 0)
        priv->sei_needed = 1;
#if !CONFIG_VAAPI_1
    if (ctx->va_rc_mode == VA_RC_CBR)
        priv->sei_cbr_workaround_needed = 1;
#endif

    if (opt->sei & SEI_TIMING) {
        memset(&priv->pic_timing, 0, sizeof(priv->pic_timing));

        priv->pic_timing.cpb_removal_delay = 2 * priv->cpb_delay;
        priv->pic_timing.dpb_output_delay  = 2 * priv->dpb_delay;

        priv->sei_needed = 1;
    }

    if (opt->sei & SEI_RECOVERY_POINT && pic->type == PICTURE_TYPE_I) {
        priv->recovery_point.recovery_frame_cnt = 0;
        priv->recovery_point.exact_match_flag   = 1;
        priv->recovery_point.broken_link_flag   = ctx->b_per_p > 0;

        priv->sei_needed = 1;
    }

    vpic->CurrPic = (VAPictureH264) {
        .picture_id          = pic->recon_surface,
        .frame_idx           = priv->frame_num,
        .flags               = 0,
        .TopFieldOrderCnt    = priv->pic_order_cnt,
        .BottomFieldOrderCnt = priv->pic_order_cnt,
    };

    for (i = 0; i < pic->nb_refs; i++) {
        VAAPIEncodePicture *ref = pic->refs[i];
        unsigned int frame_num = (ref->encode_order - priv->last_idr_frame) &
            ((1 << (4 + sps->log2_max_frame_num_minus4)) - 1);
        unsigned int pic_order_cnt = ref->display_order - priv->last_idr_frame;

        av_assert0(ref && ref->encode_order < pic->encode_order);
        vpic->ReferenceFrames[i] = (VAPictureH264) {
            .picture_id          = ref->recon_surface,
            .frame_idx           = frame_num,
            .flags               = VA_PICTURE_H264_SHORT_TERM_REFERENCE,
            .TopFieldOrderCnt    = pic_order_cnt,
            .BottomFieldOrderCnt = pic_order_cnt,
        };
    }
    for (; i < FF_ARRAY_ELEMS(vpic->ReferenceFrames); i++) {
        vpic->ReferenceFrames[i] = (VAPictureH264) {
            .picture_id = VA_INVALID_ID,
            .flags      = VA_PICTURE_H264_INVALID,
        };
    }

    vpic->coded_buf = pic->output_buffer;

    vpic->frame_num = priv->frame_num;

    vpic->pic_fields.bits.idr_pic_flag       = (pic->type == PICTURE_TYPE_IDR);
    vpic->pic_fields.bits.reference_pic_flag = (pic->type != PICTURE_TYPE_B);

    pic->nb_slices = 1;

    return 0;
}

static int vaapi_encode_h264_init_slice_params(AVCodecContext *avctx,
                                               VAAPIEncodePicture *pic,
                                               VAAPIEncodeSlice *slice)
{
    VAAPIEncodeContext               *ctx = avctx->priv_data;
    VAAPIEncodeH264Context          *priv = ctx->priv_data;
    H264RawSPS                       *sps = &priv->sps;
    H264RawPPS                       *pps = &priv->pps;
    H264RawSliceHeader                *sh = &priv->slice.header;
    VAEncPictureParameterBufferH264 *vpic = pic->codec_picture_params;
    VAEncSliceParameterBufferH264 *vslice = slice->codec_slice_params;
    int i;

    if (pic->type == PICTURE_TYPE_IDR) {
        sh->nal_unit_header.nal_unit_type = H264_NAL_IDR_SLICE;
        sh->nal_unit_header.nal_ref_idc   = 3;
    } else {
        sh->nal_unit_header.nal_unit_type = H264_NAL_SLICE;
        sh->nal_unit_header.nal_ref_idc   = pic->type != PICTURE_TYPE_B;
    }

    // Only one slice per frame.
    sh->first_mb_in_slice = 0;
    sh->slice_type        = priv->slice_type;

    sh->pic_parameter_set_id = pps->pic_parameter_set_id;

    sh->frame_num  = priv->frame_num;
    sh->idr_pic_id = priv->idr_pic_count;

    sh->pic_order_cnt_lsb = priv->pic_order_cnt &
        ((1 << (4 + sps->log2_max_pic_order_cnt_lsb_minus4)) - 1);

    sh->direct_spatial_mv_pred_flag = 1;

    if (pic->type == PICTURE_TYPE_B)
        sh->slice_qp_delta = priv->fixed_qp_b - (pps->pic_init_qp_minus26 + 26);
    else if (pic->type == PICTURE_TYPE_P)
        sh->slice_qp_delta = priv->fixed_qp_p - (pps->pic_init_qp_minus26 + 26);
    else
        sh->slice_qp_delta = priv->fixed_qp_idr - (pps->pic_init_qp_minus26 + 26);


    vslice->macroblock_address = sh->first_mb_in_slice;
    vslice->num_macroblocks    = priv->mb_width * priv->mb_height;

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
    VAAPIEncodeH264Context *priv = ctx->priv_data;
    VAAPIEncodeH264Options  *opt = ctx->codec_options;
    int err;

    err = ff_cbs_init(&priv->cbc, AV_CODEC_ID_H264, avctx);
    if (err < 0)
        return err;

    priv->mb_width  = FFALIGN(avctx->width,  16) / 16;
    priv->mb_height = FFALIGN(avctx->height, 16) / 16;

    if (ctx->va_rc_mode == VA_RC_CQP) {
        priv->fixed_qp_p = opt->qp;
        if (avctx->i_quant_factor > 0.0)
            priv->fixed_qp_idr = (int)((priv->fixed_qp_p * avctx->i_quant_factor +
                                        avctx->i_quant_offset) + 0.5);
        else
            priv->fixed_qp_idr = priv->fixed_qp_p;
        if (avctx->b_quant_factor > 0.0)
            priv->fixed_qp_b = (int)((priv->fixed_qp_p * avctx->b_quant_factor +
                                      avctx->b_quant_offset) + 0.5);
        else
            priv->fixed_qp_b = priv->fixed_qp_p;

        opt->sei &= ~SEI_TIMING;

        av_log(avctx, AV_LOG_DEBUG, "Using fixed QP = "
               "%d / %d / %d for IDR- / P- / B-frames.\n",
               priv->fixed_qp_idr, priv->fixed_qp_p, priv->fixed_qp_b);

    } else if (ctx->va_rc_mode == VA_RC_CBR ||
               ctx->va_rc_mode == VA_RC_VBR) {
        // These still need to be  set for pic_init_qp/slice_qp_delta.
        priv->fixed_qp_idr = 26;
        priv->fixed_qp_p   = 26;
        priv->fixed_qp_b   = 26;

        av_log(avctx, AV_LOG_DEBUG, "Using %s-bitrate = %"PRId64" bps.\n",
               ctx->va_rc_mode == VA_RC_CBR ? "constant" : "variable",
               avctx->bit_rate);

    } else {
        av_assert0(0 && "Invalid RC mode.");
    }

    if (avctx->compression_level == FF_COMPRESSION_DEFAULT)
        avctx->compression_level = opt->quality;

    if (opt->sei & SEI_IDENTIFIER) {
        const char *lavc  = LIBAVCODEC_IDENT;
        const char *vaapi = VA_VERSION_S;
        const char *driver;
        int len;

        memcpy(priv->identifier.uuid_iso_iec_11578,
               vaapi_encode_h264_sei_identifier_uuid,
               sizeof(priv->identifier.uuid_iso_iec_11578));

        driver = vaQueryVendorString(ctx->hwctx->display);
        if (!driver)
            driver = "unknown driver";

        len = snprintf(NULL, 0, "%s / VAAPI %s / %s", lavc, vaapi, driver);
        if (len >= 0) {
            priv->identifier_string = av_malloc(len + 1);
            if (!priv->identifier_string)
                return AVERROR(ENOMEM);

            snprintf(priv->identifier_string, len + 1,
                     "%s / VAAPI %s / %s", lavc, vaapi, driver);

            priv->identifier.data = priv->identifier_string;
            priv->identifier.data_length = len + 1;
        }
    }

    return 0;
}

static const VAAPIEncodeType vaapi_encode_type_h264 = {
    .priv_data_size        = sizeof(VAAPIEncodeH264Context),

    .configure             = &vaapi_encode_h264_configure,

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
    VAAPIEncodeContext     *ctx = avctx->priv_data;
    VAAPIEncodeH264Options *opt =
        (VAAPIEncodeH264Options*)ctx->codec_options_data;

    ctx->codec = &vaapi_encode_type_h264;

    if (avctx->profile == FF_PROFILE_UNKNOWN)
        avctx->profile = opt->profile;
    if (avctx->level == FF_LEVEL_UNKNOWN)
        avctx->level = opt->level;

    switch (avctx->profile) {
    case FF_PROFILE_H264_BASELINE:
        av_log(avctx, AV_LOG_WARNING, "H.264 baseline profile is not "
               "supported, using constrained baseline profile instead.\n");
        avctx->profile = FF_PROFILE_H264_CONSTRAINED_BASELINE;
    case FF_PROFILE_H264_CONSTRAINED_BASELINE:
        ctx->va_profile = VAProfileH264ConstrainedBaseline;
        if (avctx->max_b_frames != 0) {
            avctx->max_b_frames = 0;
            av_log(avctx, AV_LOG_WARNING, "H.264 constrained baseline profile "
                   "doesn't support encoding with B frames, disabling them.\n");
        }
        break;
    case FF_PROFILE_H264_MAIN:
        ctx->va_profile = VAProfileH264Main;
        break;
    case FF_PROFILE_H264_EXTENDED:
        av_log(avctx, AV_LOG_ERROR, "H.264 extended profile "
               "is not supported.\n");
        return AVERROR_PATCHWELCOME;
    case FF_PROFILE_UNKNOWN:
    case FF_PROFILE_H264_HIGH:
        ctx->va_profile = VAProfileH264High;
        break;
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
    default:
        av_log(avctx, AV_LOG_ERROR, "Unknown H.264 profile %d.\n",
               avctx->profile);
        return AVERROR(EINVAL);
    }
    if (opt->low_power) {
#if VA_CHECK_VERSION(0, 39, 2)
        ctx->va_entrypoint = VAEntrypointEncSliceLP;
#else
        av_log(avctx, AV_LOG_ERROR, "Low-power encoding is not "
               "supported with this VAAPI version.\n");
        return AVERROR(EINVAL);
#endif
    } else {
        ctx->va_entrypoint = VAEntrypointEncSlice;
    }

    // Only 8-bit encode is supported.
    ctx->va_rt_format = VA_RT_FORMAT_YUV420;

    if (avctx->bit_rate > 0) {
        if (avctx->rc_max_rate == avctx->bit_rate)
            ctx->va_rc_mode = VA_RC_CBR;
        else
            ctx->va_rc_mode = VA_RC_VBR;
    } else
        ctx->va_rc_mode = VA_RC_CQP;

    ctx->va_packed_headers =
        VA_ENC_PACKED_HEADER_SEQUENCE | // SPS and PPS.
        VA_ENC_PACKED_HEADER_SLICE    | // Slice headers.
        VA_ENC_PACKED_HEADER_MISC;      // SEI.

    ctx->surface_width  = FFALIGN(avctx->width,  16);
    ctx->surface_height = FFALIGN(avctx->height, 16);

    return ff_vaapi_encode_init(avctx);
}

static av_cold int vaapi_encode_h264_close(AVCodecContext *avctx)
{
    VAAPIEncodeContext *ctx = avctx->priv_data;
    VAAPIEncodeH264Context *priv = ctx->priv_data;

    if (priv) {
        ff_cbs_close(&priv->cbc);
        av_freep(&priv->identifier_string);
    }

    return ff_vaapi_encode_close(avctx);
}

#define OFFSET(x) (offsetof(VAAPIEncodeContext, codec_options_data) + \
                   offsetof(VAAPIEncodeH264Options, x))
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM)
static const AVOption vaapi_encode_h264_options[] = {
    { "qp", "Constant QP (for P-frames; scaled by qfactor/qoffset for I/B)",
      OFFSET(qp), AV_OPT_TYPE_INT, { .i64 = 20 }, 0, 52, FLAGS },
    { "quality", "Set encode quality (trades off against speed, higher is faster)",
      OFFSET(quality), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 8, FLAGS },
    { "low_power", "Use low-power encoding mode (experimental: only supported "
      "on some platforms, does not support all features)",
      OFFSET(low_power), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, FLAGS },
    { "coder", "Entropy coder type",
      OFFSET(coder), AV_OPT_TYPE_INT, { .i64 = 1 }, 0, 1, FLAGS, "coder" },
        { "cavlc", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 0 }, INT_MIN, INT_MAX, FLAGS, "coder" },
        { "cabac", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 1 }, INT_MIN, INT_MAX, FLAGS, "coder" },
        { "vlc",   NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 0 }, INT_MIN, INT_MAX, FLAGS, "coder" },
        { "ac",    NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 1 }, INT_MIN, INT_MAX, FLAGS, "coder" },

    { "aud", "Include AUD",
      OFFSET(aud), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, FLAGS },

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
      { .i64 = FF_PROFILE_H264_HIGH }, 0x0000, 0xffff, FLAGS, "profile" },

#define PROFILE(name, value)  name, NULL, 0, AV_OPT_TYPE_CONST, \
      { .i64 = value }, 0, 0, FLAGS, "profile"
    { PROFILE("constrained_baseline", FF_PROFILE_H264_CONSTRAINED_BASELINE) },
    { PROFILE("main",                 FF_PROFILE_H264_MAIN) },
    { PROFILE("high",                 FF_PROFILE_H264_HIGH) },
#undef PROFILE

    { "level", "Set level (level_idc)",
      OFFSET(level), AV_OPT_TYPE_INT,
      { .i64 = 51 }, 0x00, 0xff, FLAGS, "level" },

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
    { "qmin",           "0"   },
    { NULL },
};

static const AVClass vaapi_encode_h264_class = {
    .class_name = "h264_vaapi",
    .item_name  = av_default_item_name,
    .option     = vaapi_encode_h264_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_h264_vaapi_encoder = {
    .name           = "h264_vaapi",
    .long_name      = NULL_IF_CONFIG_SMALL("H.264/AVC (VAAPI)"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_H264,
    .priv_data_size = (sizeof(VAAPIEncodeContext) +
                       sizeof(VAAPIEncodeH264Options)),
    .init           = &vaapi_encode_h264_init,
    .encode2        = &ff_vaapi_encode2,
    .close          = &vaapi_encode_h264_close,
    .priv_class     = &vaapi_encode_h264_class,
    .capabilities   = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_HARDWARE,
    .defaults       = vaapi_encode_h264_defaults,
    .pix_fmts = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_VAAPI,
        AV_PIX_FMT_NONE,
    },
    .wrapper_name   = "vaapi",
};
