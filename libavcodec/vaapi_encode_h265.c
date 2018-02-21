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
#include <va/va_enc_hevc.h>

#include "libavutil/avassert.h"
#include "libavutil/common.h"
#include "libavutil/opt.h"

#include "avcodec.h"
#include "cbs.h"
#include "cbs_h265.h"
#include "hevc.h"
#include "internal.h"
#include "put_bits.h"
#include "vaapi_encode.h"


typedef struct VAAPIEncodeH265Context {
    unsigned int ctu_width;
    unsigned int ctu_height;

    int fixed_qp_idr;
    int fixed_qp_p;
    int fixed_qp_b;

    H265RawAUD aud;
    H265RawVPS vps;
    H265RawSPS sps;
    H265RawPPS pps;
    H265RawSlice slice;

    int64_t last_idr_frame;
    int pic_order_cnt;

    int slice_nal_unit;
    int slice_type;
    int pic_type;

    CodedBitstreamContext *cbc;
    CodedBitstreamFragment current_access_unit;
    int aud_needed;
} VAAPIEncodeH265Context;

typedef struct VAAPIEncodeH265Options {
    int qp;
    int aud;
    int profile;
    int level;
} VAAPIEncodeH265Options;


static int vaapi_encode_h265_write_access_unit(AVCodecContext *avctx,
                                               char *data, size_t *data_len,
                                               CodedBitstreamFragment *au)
{
    VAAPIEncodeContext      *ctx = avctx->priv_data;
    VAAPIEncodeH265Context *priv = ctx->priv_data;
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

static int vaapi_encode_h265_add_nal(AVCodecContext *avctx,
                                     CodedBitstreamFragment *au,
                                     void *nal_unit)
{
    VAAPIEncodeContext      *ctx = avctx->priv_data;
    VAAPIEncodeH265Context *priv = ctx->priv_data;
    H265RawNALUnitHeader *header = nal_unit;
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

static int vaapi_encode_h265_write_sequence_header(AVCodecContext *avctx,
                                                   char *data, size_t *data_len)
{
    VAAPIEncodeContext      *ctx = avctx->priv_data;
    VAAPIEncodeH265Context *priv = ctx->priv_data;
    CodedBitstreamFragment   *au = &priv->current_access_unit;
    int err;

    if (priv->aud_needed) {
        err = vaapi_encode_h265_add_nal(avctx, au, &priv->aud);
        if (err < 0)
            goto fail;
        priv->aud_needed = 0;
    }

    err = vaapi_encode_h265_add_nal(avctx, au, &priv->vps);
    if (err < 0)
        goto fail;

    err = vaapi_encode_h265_add_nal(avctx, au, &priv->sps);
    if (err < 0)
        goto fail;

    err = vaapi_encode_h265_add_nal(avctx, au, &priv->pps);
    if (err < 0)
        goto fail;

    err = vaapi_encode_h265_write_access_unit(avctx, data, data_len, au);
fail:
    ff_cbs_fragment_uninit(priv->cbc, au);
    return err;
}

static int vaapi_encode_h265_write_slice_header(AVCodecContext *avctx,
                                                VAAPIEncodePicture *pic,
                                                VAAPIEncodeSlice *slice,
                                                char *data, size_t *data_len)
{
    VAAPIEncodeContext      *ctx = avctx->priv_data;
    VAAPIEncodeH265Context *priv = ctx->priv_data;
    CodedBitstreamFragment   *au = &priv->current_access_unit;
    int err;

    if (priv->aud_needed) {
        err = vaapi_encode_h265_add_nal(avctx, au, &priv->aud);
        if (err < 0)
            goto fail;
        priv->aud_needed = 0;
    }

    err = vaapi_encode_h265_add_nal(avctx, au, &priv->slice);
    if (err < 0)
        goto fail;

    err = vaapi_encode_h265_write_access_unit(avctx, data, data_len, au);
fail:
    ff_cbs_fragment_uninit(priv->cbc, au);
    return err;
}

static int vaapi_encode_h265_init_sequence_params(AVCodecContext *avctx)
{
    VAAPIEncodeContext                *ctx = avctx->priv_data;
    VAAPIEncodeH265Context           *priv = ctx->priv_data;
    H265RawVPS                        *vps = &priv->vps;
    H265RawSPS                        *sps = &priv->sps;
    H265RawPPS                        *pps = &priv->pps;
    H265RawVUI                        *vui = &sps->vui;
    VAEncSequenceParameterBufferHEVC *vseq = ctx->codec_sequence_params;
    VAEncPictureParameterBufferHEVC  *vpic = ctx->codec_picture_params;
    int i;

    memset(&priv->current_access_unit, 0,
           sizeof(priv->current_access_unit));

    memset(vps, 0, sizeof(*vps));
    memset(sps, 0, sizeof(*sps));
    memset(pps, 0, sizeof(*pps));


    // VPS

    vps->nal_unit_header = (H265RawNALUnitHeader) {
        .nal_unit_type         = HEVC_NAL_VPS,
        .nuh_layer_id          = 0,
        .nuh_temporal_id_plus1 = 1,
    };

    vps->vps_video_parameter_set_id = 0;

    vps->vps_base_layer_internal_flag  = 1;
    vps->vps_base_layer_available_flag = 1;
    vps->vps_max_layers_minus1         = 0;
    vps->vps_max_sub_layers_minus1     = 0;
    vps->vps_temporal_id_nesting_flag  = 1;

    vps->profile_tier_level = (H265RawProfileTierLevel) {
        .general_profile_space = 0,
        .general_profile_idc   = avctx->profile,
        .general_tier_flag     = 0,

        .general_progressive_source_flag    = 1,
        .general_interlaced_source_flag     = 0,
        .general_non_packed_constraint_flag = 1,
        .general_frame_only_constraint_flag = 1,

        .general_level_idc     = avctx->level,
    };
    vps->profile_tier_level.general_profile_compatibility_flag[avctx->profile & 31] = 1;

    vps->vps_sub_layer_ordering_info_present_flag = 0;
    vps->vps_max_dec_pic_buffering_minus1[0]      = (ctx->b_per_p > 0) + 1;
    vps->vps_max_num_reorder_pics[0]              = (ctx->b_per_p > 0);
    vps->vps_max_latency_increase_plus1[0]        = 0;

    vps->vps_max_layer_id             = 0;
    vps->vps_num_layer_sets_minus1    = 0;
    vps->layer_id_included_flag[0][0] = 1;

    vps->vps_timing_info_present_flag = 1;
    if (avctx->framerate.num > 0 && avctx->framerate.den > 0) {
        vps->vps_num_units_in_tick  = avctx->framerate.den;
        vps->vps_time_scale         = avctx->framerate.num;
        vps->vps_poc_proportional_to_timing_flag = 1;
        vps->vps_num_ticks_poc_diff_one_minus1   = 0;
    } else {
        vps->vps_num_units_in_tick  = avctx->time_base.num;
        vps->vps_time_scale         = avctx->time_base.den;
        vps->vps_poc_proportional_to_timing_flag = 0;
    }
    vps->vps_num_hrd_parameters = 0;


    // SPS

    sps->nal_unit_header = (H265RawNALUnitHeader) {
        .nal_unit_type         = HEVC_NAL_SPS,
        .nuh_layer_id          = 0,
        .nuh_temporal_id_plus1 = 1,
    };

    sps->sps_video_parameter_set_id = vps->vps_video_parameter_set_id;

    sps->sps_max_sub_layers_minus1    = vps->vps_max_sub_layers_minus1;
    sps->sps_temporal_id_nesting_flag = vps->vps_temporal_id_nesting_flag;

    sps->profile_tier_level = vps->profile_tier_level;

    sps->sps_seq_parameter_set_id = 0;

    sps->chroma_format_idc          = 1; // YUV 4:2:0.
    sps->separate_colour_plane_flag = 0;

    sps->pic_width_in_luma_samples  = ctx->surface_width;
    sps->pic_height_in_luma_samples = ctx->surface_height;

    if (avctx->width  != ctx->surface_width ||
        avctx->height != ctx->surface_height) {
        sps->conformance_window_flag = 1;
        sps->conf_win_left_offset   = 0;
        sps->conf_win_right_offset  =
            (ctx->surface_width - avctx->width) / 2;
        sps->conf_win_top_offset    = 0;
        sps->conf_win_bottom_offset =
            (ctx->surface_height - avctx->height) / 2;
    } else {
        sps->conformance_window_flag = 0;
    }

    sps->bit_depth_luma_minus8 =
        avctx->profile == FF_PROFILE_HEVC_MAIN_10 ? 2 : 0;
    sps->bit_depth_chroma_minus8 = sps->bit_depth_luma_minus8;

    sps->log2_max_pic_order_cnt_lsb_minus4 = 8;

    sps->sps_sub_layer_ordering_info_present_flag =
        vps->vps_sub_layer_ordering_info_present_flag;
    for (i = 0; i <= sps->sps_max_sub_layers_minus1; i++) {
        sps->sps_max_dec_pic_buffering_minus1[i] =
            vps->vps_max_dec_pic_buffering_minus1[i];
        sps->sps_max_num_reorder_pics[i] =
            vps->vps_max_num_reorder_pics[i];
        sps->sps_max_latency_increase_plus1[i] =
            vps->vps_max_latency_increase_plus1[i];
    }

    // These have to come from the capabilities of the encoder.  We have no
    // way to query them, so just hardcode parameters which work on the Intel
    // driver.
    // CTB size from 8x8 to 32x32.
    sps->log2_min_luma_coding_block_size_minus3   = 0;
    sps->log2_diff_max_min_luma_coding_block_size = 2;
    // Transform size from 4x4 to 32x32.
    sps->log2_min_luma_transform_block_size_minus2   = 0;
    sps->log2_diff_max_min_luma_transform_block_size = 3;
    // Full transform hierarchy allowed (2-5).
    sps->max_transform_hierarchy_depth_inter = 3;
    sps->max_transform_hierarchy_depth_intra = 3;
    // AMP works.
    sps->amp_enabled_flag = 1;
    // SAO and temporal MVP do not work.
    sps->sample_adaptive_offset_enabled_flag = 0;
    sps->sps_temporal_mvp_enabled_flag       = 0;

    sps->pcm_enabled_flag = 0;

    // STRPSs should ideally be here rather than defined individually in
    // each slice, but the structure isn't completely fixed so for now
    // don't bother.
    sps->num_short_term_ref_pic_sets     = 0;
    sps->long_term_ref_pics_present_flag = 0;

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
                vui->aspect_ratio_idc = i;
                break;
            }
        }
        if (i >= FF_ARRAY_ELEMS(sar_idc)) {
            vui->aspect_ratio_idc = 255;
            vui->sar_width  = avctx->sample_aspect_ratio.num;
            vui->sar_height = avctx->sample_aspect_ratio.den;
        }
        vui->aspect_ratio_info_present_flag = 1;
    }

    if (avctx->color_range     != AVCOL_RANGE_UNSPECIFIED ||
        avctx->color_primaries != AVCOL_PRI_UNSPECIFIED ||
        avctx->color_trc       != AVCOL_TRC_UNSPECIFIED ||
        avctx->colorspace      != AVCOL_SPC_UNSPECIFIED) {
        vui->video_signal_type_present_flag = 1;
        vui->video_format      = 5; // Unspecified.
        vui->video_full_range_flag =
            avctx->color_range == AVCOL_RANGE_JPEG;

        if (avctx->color_primaries != AVCOL_PRI_UNSPECIFIED ||
            avctx->color_trc       != AVCOL_TRC_UNSPECIFIED ||
            avctx->colorspace      != AVCOL_SPC_UNSPECIFIED) {
            vui->colour_description_present_flag = 1;
            vui->colour_primaries         = avctx->color_primaries;
            vui->transfer_characteristics = avctx->color_trc;
            vui->matrix_coefficients      = avctx->colorspace;
        }
    } else {
        vui->video_format             = 5;
        vui->video_full_range_flag    = 0;
        vui->colour_primaries         = avctx->color_primaries;
        vui->transfer_characteristics = avctx->color_trc;
        vui->matrix_coefficients      = avctx->colorspace;
    }

    if (avctx->chroma_sample_location != AVCHROMA_LOC_UNSPECIFIED) {
        vui->chroma_loc_info_present_flag = 1;
        vui->chroma_sample_loc_type_top_field    =
        vui->chroma_sample_loc_type_bottom_field =
            avctx->chroma_sample_location - 1;
    }

    vui->vui_timing_info_present_flag        = 1;
    vui->vui_num_units_in_tick               = vps->vps_num_units_in_tick;
    vui->vui_time_scale                      = vps->vps_time_scale;
    vui->vui_poc_proportional_to_timing_flag = vps->vps_poc_proportional_to_timing_flag;
    vui->vui_num_ticks_poc_diff_one_minus1   = vps->vps_num_ticks_poc_diff_one_minus1;
    vui->vui_hrd_parameters_present_flag     = 0;

    vui->bitstream_restriction_flag    = 1;
    vui->motion_vectors_over_pic_boundaries_flag = 1;
    vui->restricted_ref_pic_lists_flag = 1;
    vui->max_bytes_per_pic_denom       = 0;
    vui->max_bits_per_min_cu_denom     = 0;
    vui->log2_max_mv_length_horizontal = 15;
    vui->log2_max_mv_length_vertical   = 15;


    // PPS

    pps->nal_unit_header = (H265RawNALUnitHeader) {
        .nal_unit_type         = HEVC_NAL_PPS,
        .nuh_layer_id          = 0,
        .nuh_temporal_id_plus1 = 1,
    };

    pps->pps_pic_parameter_set_id = 0;
    pps->pps_seq_parameter_set_id = sps->sps_seq_parameter_set_id;

    pps->num_ref_idx_l0_default_active_minus1 = 0;
    pps->num_ref_idx_l1_default_active_minus1 = 0;

    pps->init_qp_minus26 = priv->fixed_qp_idr - 26;

    pps->cu_qp_delta_enabled_flag = (ctx->va_rc_mode != VA_RC_CQP);
    pps->diff_cu_qp_delta_depth   = 0;

    pps->pps_loop_filter_across_slices_enabled_flag = 1;


    // Fill VAAPI parameter buffers.

    *vseq = (VAEncSequenceParameterBufferHEVC) {
        .general_profile_idc = vps->profile_tier_level.general_profile_idc,
        .general_level_idc   = vps->profile_tier_level.general_level_idc,
        .general_tier_flag   = vps->profile_tier_level.general_tier_flag,

        .intra_period     = avctx->gop_size,
        .intra_idr_period = avctx->gop_size,
        .ip_period        = ctx->b_per_p + 1,
        .bits_per_second   = avctx->bit_rate,

        .pic_width_in_luma_samples  = sps->pic_width_in_luma_samples,
        .pic_height_in_luma_samples = sps->pic_height_in_luma_samples,

        .seq_fields.bits = {
            .chroma_format_idc             = sps->chroma_format_idc,
            .separate_colour_plane_flag    = sps->separate_colour_plane_flag,
            .bit_depth_luma_minus8         = sps->bit_depth_luma_minus8,
            .bit_depth_chroma_minus8       = sps->bit_depth_chroma_minus8,
            .scaling_list_enabled_flag     = sps->scaling_list_enabled_flag,
            .strong_intra_smoothing_enabled_flag =
                sps->strong_intra_smoothing_enabled_flag,
            .amp_enabled_flag              = sps->amp_enabled_flag,
            .sample_adaptive_offset_enabled_flag =
                sps->sample_adaptive_offset_enabled_flag,
            .pcm_enabled_flag              = sps->pcm_enabled_flag,
            .pcm_loop_filter_disabled_flag = sps->pcm_loop_filter_disabled_flag,
            .sps_temporal_mvp_enabled_flag = sps->sps_temporal_mvp_enabled_flag,
        },

        .log2_min_luma_coding_block_size_minus3 =
            sps->log2_min_luma_coding_block_size_minus3,
        .log2_diff_max_min_luma_coding_block_size =
            sps->log2_diff_max_min_luma_coding_block_size,
        .log2_min_transform_block_size_minus2 =
            sps->log2_min_luma_transform_block_size_minus2,
        .log2_diff_max_min_transform_block_size =
            sps->log2_diff_max_min_luma_transform_block_size,
        .max_transform_hierarchy_depth_inter =
            sps->max_transform_hierarchy_depth_inter,
        .max_transform_hierarchy_depth_intra =
            sps->max_transform_hierarchy_depth_intra,

        .pcm_sample_bit_depth_luma_minus1 =
            sps->pcm_sample_bit_depth_luma_minus1,
        .pcm_sample_bit_depth_chroma_minus1 =
            sps->pcm_sample_bit_depth_chroma_minus1,
        .log2_min_pcm_luma_coding_block_size_minus3 =
            sps->log2_min_pcm_luma_coding_block_size_minus3,
        .log2_max_pcm_luma_coding_block_size_minus3 =
            sps->log2_min_pcm_luma_coding_block_size_minus3 +
            sps->log2_diff_max_min_pcm_luma_coding_block_size,

        .vui_parameters_present_flag = 0,
    };

    *vpic = (VAEncPictureParameterBufferHEVC) {
        .decoded_curr_pic = {
            .picture_id = VA_INVALID_ID,
            .flags      = VA_PICTURE_HEVC_INVALID,
        },

        .coded_buf = VA_INVALID_ID,

        .collocated_ref_pic_index = 0xff,

        .last_picture = 0,

        .pic_init_qp            = pps->init_qp_minus26 + 26,
        .diff_cu_qp_delta_depth = pps->diff_cu_qp_delta_depth,
        .pps_cb_qp_offset       = pps->pps_cb_qp_offset,
        .pps_cr_qp_offset       = pps->pps_cr_qp_offset,

        .num_tile_columns_minus1 = pps->num_tile_columns_minus1,
        .num_tile_rows_minus1    = pps->num_tile_rows_minus1,

        .log2_parallel_merge_level_minus2 = pps->log2_parallel_merge_level_minus2,
        .ctu_max_bitsize_allowed          = 0,

        .num_ref_idx_l0_default_active_minus1 =
            pps->num_ref_idx_l0_default_active_minus1,
        .num_ref_idx_l1_default_active_minus1 =
            pps->num_ref_idx_l1_default_active_minus1,

        .slice_pic_parameter_set_id = pps->pps_pic_parameter_set_id,

        .pic_fields.bits = {
            .sign_data_hiding_enabled_flag  = pps->sign_data_hiding_enabled_flag,
            .constrained_intra_pred_flag    = pps->constrained_intra_pred_flag,
            .transform_skip_enabled_flag    = pps->transform_skip_enabled_flag,
            .cu_qp_delta_enabled_flag       = pps->cu_qp_delta_enabled_flag,
            .weighted_pred_flag             = pps->weighted_pred_flag,
            .weighted_bipred_flag           = pps->weighted_bipred_flag,
            .transquant_bypass_enabled_flag = pps->transquant_bypass_enabled_flag,
            .tiles_enabled_flag             = pps->tiles_enabled_flag,
            .entropy_coding_sync_enabled_flag = pps->entropy_coding_sync_enabled_flag,
            .loop_filter_across_tiles_enabled_flag =
                pps->loop_filter_across_tiles_enabled_flag,
            .scaling_list_data_present_flag = (sps->sps_scaling_list_data_present_flag |
                                               pps->pps_scaling_list_data_present_flag),
            .screen_content_flag            = 0,
            .enable_gpu_weighted_prediction = 0,
            .no_output_of_prior_pics_flag   = 0,
        },
    };

    return 0;
}

static int vaapi_encode_h265_init_picture_params(AVCodecContext *avctx,
                                                 VAAPIEncodePicture *pic)
{
    VAAPIEncodeContext               *ctx = avctx->priv_data;
    VAAPIEncodeH265Context          *priv = ctx->priv_data;
    VAAPIEncodeH265Options           *opt = ctx->codec_options;
    VAEncPictureParameterBufferHEVC *vpic = pic->codec_picture_params;
    int i;

    if (pic->type == PICTURE_TYPE_IDR) {
        av_assert0(pic->display_order == pic->encode_order);

        priv->last_idr_frame = pic->display_order;

        priv->slice_nal_unit = HEVC_NAL_IDR_W_RADL;
        priv->slice_type     = HEVC_SLICE_I;
        priv->pic_type       = 0;
    } else {
        av_assert0(pic->encode_order > priv->last_idr_frame);

        if (pic->type == PICTURE_TYPE_I) {
            priv->slice_nal_unit = HEVC_NAL_CRA_NUT;
            priv->slice_type     = HEVC_SLICE_I;
            priv->pic_type       = 0;
        } else if (pic->type == PICTURE_TYPE_P) {
            av_assert0(pic->refs[0]);
            priv->slice_nal_unit = HEVC_NAL_TRAIL_R;
            priv->slice_type     = HEVC_SLICE_P;
            priv->pic_type       = 1;
        } else {
            av_assert0(pic->refs[0] && pic->refs[1]);
            if (pic->refs[1]->type == PICTURE_TYPE_I)
                priv->slice_nal_unit = HEVC_NAL_RASL_N;
            else
                priv->slice_nal_unit = HEVC_NAL_TRAIL_N;
            priv->slice_type = HEVC_SLICE_B;
            priv->pic_type   = 2;
        }
    }
    priv->pic_order_cnt = pic->display_order - priv->last_idr_frame;

    if (opt->aud) {
        priv->aud_needed = 1;
        priv->aud.nal_unit_header = (H265RawNALUnitHeader) {
            .nal_unit_type         = HEVC_NAL_AUD,
            .nuh_layer_id          = 0,
            .nuh_temporal_id_plus1 = 1,
        };
        priv->aud.pic_type = priv->pic_type;
    } else {
        priv->aud_needed = 0;
    }

    vpic->decoded_curr_pic = (VAPictureHEVC) {
        .picture_id    = pic->recon_surface,
        .pic_order_cnt = priv->pic_order_cnt,
        .flags         = 0,
    };

    for (i = 0; i < pic->nb_refs; i++) {
        VAAPIEncodePicture *ref = pic->refs[i];
        av_assert0(ref && ref->encode_order < pic->encode_order);

        vpic->reference_frames[i] = (VAPictureHEVC) {
            .picture_id    = ref->recon_surface,
            .pic_order_cnt = ref->display_order - priv->last_idr_frame,
            .flags = (ref->display_order < pic->display_order ?
                      VA_PICTURE_HEVC_RPS_ST_CURR_BEFORE : 0) |
                     (ref->display_order > pic->display_order ?
                      VA_PICTURE_HEVC_RPS_ST_CURR_AFTER  : 0),
        };
    }
    for (; i < FF_ARRAY_ELEMS(vpic->reference_frames); i++) {
        vpic->reference_frames[i] = (VAPictureHEVC) {
            .picture_id = VA_INVALID_ID,
            .flags      = VA_PICTURE_HEVC_INVALID,
        };
    }

    vpic->coded_buf = pic->output_buffer;

    vpic->nal_unit_type = priv->slice_nal_unit;

    switch (pic->type) {
    case PICTURE_TYPE_IDR:
        vpic->pic_fields.bits.idr_pic_flag       = 1;
        vpic->pic_fields.bits.coding_type        = 1;
        vpic->pic_fields.bits.reference_pic_flag = 1;
        break;
    case PICTURE_TYPE_I:
        vpic->pic_fields.bits.idr_pic_flag       = 0;
        vpic->pic_fields.bits.coding_type        = 1;
        vpic->pic_fields.bits.reference_pic_flag = 1;
        break;
    case PICTURE_TYPE_P:
        vpic->pic_fields.bits.idr_pic_flag       = 0;
        vpic->pic_fields.bits.coding_type        = 2;
        vpic->pic_fields.bits.reference_pic_flag = 1;
        break;
    case PICTURE_TYPE_B:
        vpic->pic_fields.bits.idr_pic_flag       = 0;
        vpic->pic_fields.bits.coding_type        = 3;
        vpic->pic_fields.bits.reference_pic_flag = 0;
        break;
    default:
        av_assert0(0 && "invalid picture type");
    }

    pic->nb_slices = 1;

    return 0;
}

static int vaapi_encode_h265_init_slice_params(AVCodecContext *avctx,
                                               VAAPIEncodePicture *pic,
                                               VAAPIEncodeSlice *slice)
{
    VAAPIEncodeContext                *ctx = avctx->priv_data;
    VAAPIEncodeH265Context           *priv = ctx->priv_data;
    const H265RawSPS                  *sps = &priv->sps;
    const H265RawPPS                  *pps = &priv->pps;
    H265RawSliceHeader                 *sh = &priv->slice.header;
    VAEncPictureParameterBufferHEVC  *vpic = pic->codec_picture_params;
    VAEncSliceParameterBufferHEVC  *vslice = slice->codec_slice_params;
    int i;

    sh->nal_unit_header = (H265RawNALUnitHeader) {
        .nal_unit_type         = priv->slice_nal_unit,
        .nuh_layer_id          = 0,
        .nuh_temporal_id_plus1 = 1,
    };

    sh->slice_pic_parameter_set_id      = pps->pps_pic_parameter_set_id;

    // Currently we only support one slice per frame.
    sh->first_slice_segment_in_pic_flag = 1;
    sh->slice_segment_address           = 0;

    sh->slice_type = priv->slice_type;

    sh->slice_pic_order_cnt_lsb = priv->pic_order_cnt &
        (1 << (sps->log2_max_pic_order_cnt_lsb_minus4 + 4)) - 1;

    if (pic->type != PICTURE_TYPE_IDR) {
        H265RawSTRefPicSet *rps;
        VAAPIEncodePicture *st;
        int used;

        sh->short_term_ref_pic_set_sps_flag = 0;

        rps = &sh->short_term_ref_pic_set;
        memset(rps, 0, sizeof(*rps));

        for (st = ctx->pic_start; st; st = st->next) {
            if (st->encode_order >= pic->encode_order) {
                // Not yet in DPB.
                continue;
            }
            used = 0;
            for (i = 0; i < pic->nb_refs; i++) {
                if (pic->refs[i] == st)
                    used = 1;
            }
            if (!used) {
                // Usually each picture always uses all of the others in the
                // DPB as references.  The one case we have to treat here is
                // a non-IDR IRAP picture, which may need to hold unused
                // references across itself to be used for the decoding of
                // following RASL pictures.  This looks for such an RASL
                // picture, and keeps the reference if there is one.
                VAAPIEncodePicture *rp;
                for (rp = ctx->pic_start; rp; rp = rp->next) {
                    if (rp->encode_order < pic->encode_order)
                        continue;
                    if (rp->type != PICTURE_TYPE_B)
                        continue;
                    if (rp->refs[0] == st && rp->refs[1] == pic)
                        break;
                }
                if (!rp)
                    continue;
            }
            // This only works for one instance of each (delta_poc_sN_minus1
            // is relative to the previous frame in the list, not relative to
            // the current frame directly).
            if (st->display_order < pic->display_order) {
                rps->delta_poc_s0_minus1[rps->num_negative_pics] =
                    pic->display_order - st->display_order - 1;
                rps->used_by_curr_pic_s0_flag[rps->num_negative_pics] = used;
                ++rps->num_negative_pics;
            } else {
                rps->delta_poc_s1_minus1[rps->num_positive_pics] =
                    st->display_order - pic->display_order - 1;
                rps->used_by_curr_pic_s1_flag[rps->num_positive_pics] = used;
                ++rps->num_positive_pics;
            }
        }

        sh->num_long_term_sps  = 0;
        sh->num_long_term_pics = 0;

        sh->slice_temporal_mvp_enabled_flag =
            sps->sps_temporal_mvp_enabled_flag;
        if (sh->slice_temporal_mvp_enabled_flag) {
            sh->collocated_from_l0_flag = sh->slice_type == HEVC_SLICE_B;
            sh->collocated_ref_idx      = 0;
        }

        sh->num_ref_idx_active_override_flag = 0;
        sh->num_ref_idx_l0_active_minus1 = pps->num_ref_idx_l0_default_active_minus1;
        sh->num_ref_idx_l1_active_minus1 = pps->num_ref_idx_l1_default_active_minus1;
    }

    sh->slice_sao_luma_flag = sh->slice_sao_chroma_flag =
        sps->sample_adaptive_offset_enabled_flag;

    if (pic->type == PICTURE_TYPE_B)
        sh->slice_qp_delta = priv->fixed_qp_b - (pps->init_qp_minus26 + 26);
    else if (pic->type == PICTURE_TYPE_P)
        sh->slice_qp_delta = priv->fixed_qp_p - (pps->init_qp_minus26 + 26);
    else
        sh->slice_qp_delta = priv->fixed_qp_idr - (pps->init_qp_minus26 + 26);


    *vslice = (VAEncSliceParameterBufferHEVC) {
        .slice_segment_address = sh->slice_segment_address,
        .num_ctu_in_slice      = priv->ctu_width * priv->ctu_height,

        .slice_type                 = sh->slice_type,
        .slice_pic_parameter_set_id = sh->slice_pic_parameter_set_id,

        .num_ref_idx_l0_active_minus1 = sh->num_ref_idx_l0_active_minus1,
        .num_ref_idx_l1_active_minus1 = sh->num_ref_idx_l1_active_minus1,

        .luma_log2_weight_denom         = sh->luma_log2_weight_denom,
        .delta_chroma_log2_weight_denom = sh->delta_chroma_log2_weight_denom,

        .max_num_merge_cand = 5 - sh->five_minus_max_num_merge_cand,

        .slice_qp_delta     = sh->slice_qp_delta,
        .slice_cb_qp_offset = sh->slice_cb_qp_offset,
        .slice_cr_qp_offset = sh->slice_cr_qp_offset,

        .slice_beta_offset_div2 = sh->slice_beta_offset_div2,
        .slice_tc_offset_div2   = sh->slice_tc_offset_div2,

        .slice_fields.bits = {
            .last_slice_of_pic_flag       = 1,
            .dependent_slice_segment_flag = sh->dependent_slice_segment_flag,
            .colour_plane_id              = sh->colour_plane_id,
            .slice_temporal_mvp_enabled_flag =
                sh->slice_temporal_mvp_enabled_flag,
            .slice_sao_luma_flag          = sh->slice_sao_luma_flag,
            .slice_sao_chroma_flag        = sh->slice_sao_chroma_flag,
            .num_ref_idx_active_override_flag =
                sh->num_ref_idx_active_override_flag,
            .mvd_l1_zero_flag             = sh->mvd_l1_zero_flag,
            .cabac_init_flag              = sh->cabac_init_flag,
            .slice_deblocking_filter_disabled_flag =
                sh->slice_deblocking_filter_disabled_flag,
            .slice_loop_filter_across_slices_enabled_flag =
                sh->slice_loop_filter_across_slices_enabled_flag,
            .collocated_from_l0_flag      = sh->collocated_from_l0_flag,
        },
    };

    for (i = 0; i < FF_ARRAY_ELEMS(vslice->ref_pic_list0); i++) {
        vslice->ref_pic_list0[i].picture_id = VA_INVALID_ID;
        vslice->ref_pic_list0[i].flags      = VA_PICTURE_HEVC_INVALID;
        vslice->ref_pic_list1[i].picture_id = VA_INVALID_ID;
        vslice->ref_pic_list1[i].flags      = VA_PICTURE_HEVC_INVALID;
    }

    av_assert0(pic->nb_refs <= 2);
    if (pic->nb_refs >= 1) {
        // Backward reference for P- or B-frame.
        av_assert0(pic->type == PICTURE_TYPE_P ||
                   pic->type == PICTURE_TYPE_B);
        vslice->ref_pic_list0[0] = vpic->reference_frames[0];
    }
    if (pic->nb_refs >= 2) {
        // Forward reference for B-frame.
        av_assert0(pic->type == PICTURE_TYPE_B);
        vslice->ref_pic_list1[0] = vpic->reference_frames[1];
    }

    return 0;
}

static av_cold int vaapi_encode_h265_configure(AVCodecContext *avctx)
{
    VAAPIEncodeContext      *ctx = avctx->priv_data;
    VAAPIEncodeH265Context *priv = ctx->priv_data;
    VAAPIEncodeH265Options  *opt = ctx->codec_options;
    int err;

    err = ff_cbs_init(&priv->cbc, AV_CODEC_ID_HEVC, avctx);
    if (err < 0)
        return err;

    priv->ctu_width     = FFALIGN(ctx->surface_width,  32) / 32;
    priv->ctu_height    = FFALIGN(ctx->surface_height, 32) / 32;

    av_log(avctx, AV_LOG_VERBOSE, "Input %ux%u -> Surface %ux%u -> CTU %ux%u.\n",
           avctx->width, avctx->height, ctx->surface_width,
           ctx->surface_height, priv->ctu_width, priv->ctu_height);

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

        av_log(avctx, AV_LOG_DEBUG, "Using fixed QP = "
               "%d / %d / %d for IDR- / P- / B-frames.\n",
               priv->fixed_qp_idr, priv->fixed_qp_p, priv->fixed_qp_b);

    } else if (ctx->va_rc_mode == VA_RC_CBR ||
               ctx->va_rc_mode == VA_RC_VBR) {
        // These still need to be  set for pic_init_qp/slice_qp_delta.
        priv->fixed_qp_idr = 30;
        priv->fixed_qp_p   = 30;
        priv->fixed_qp_b   = 30;

        av_log(avctx, AV_LOG_DEBUG, "Using %s-bitrate = %"PRId64" bps.\n",
               ctx->va_rc_mode == VA_RC_CBR ? "constant" : "variable",
               avctx->bit_rate);

    } else {
        av_assert0(0 && "Invalid RC mode.");
    }

    return 0;
}

static const VAAPIEncodeType vaapi_encode_type_h265 = {
    .priv_data_size        = sizeof(VAAPIEncodeH265Context),

    .configure             = &vaapi_encode_h265_configure,

    .sequence_params_size  = sizeof(VAEncSequenceParameterBufferHEVC),
    .init_sequence_params  = &vaapi_encode_h265_init_sequence_params,

    .picture_params_size   = sizeof(VAEncPictureParameterBufferHEVC),
    .init_picture_params   = &vaapi_encode_h265_init_picture_params,

    .slice_params_size     = sizeof(VAEncSliceParameterBufferHEVC),
    .init_slice_params     = &vaapi_encode_h265_init_slice_params,

    .sequence_header_type  = VAEncPackedHeaderSequence,
    .write_sequence_header = &vaapi_encode_h265_write_sequence_header,

    .slice_header_type     = VAEncPackedHeaderHEVC_Slice,
    .write_slice_header    = &vaapi_encode_h265_write_slice_header,
};

static av_cold int vaapi_encode_h265_init(AVCodecContext *avctx)
{
    VAAPIEncodeContext     *ctx = avctx->priv_data;
    VAAPIEncodeH265Options *opt =
        (VAAPIEncodeH265Options*)ctx->codec_options_data;

    ctx->codec = &vaapi_encode_type_h265;

    if (avctx->profile == FF_PROFILE_UNKNOWN)
        avctx->profile = opt->profile;
    if (avctx->level == FF_LEVEL_UNKNOWN)
        avctx->level = opt->level;

    switch (avctx->profile) {
    case FF_PROFILE_HEVC_MAIN:
    case FF_PROFILE_UNKNOWN:
        ctx->va_profile = VAProfileHEVCMain;
        ctx->va_rt_format = VA_RT_FORMAT_YUV420;
        break;
    case FF_PROFILE_HEVC_MAIN_10:
#ifdef VA_RT_FORMAT_YUV420_10BPP
        ctx->va_profile = VAProfileHEVCMain10;
        ctx->va_rt_format = VA_RT_FORMAT_YUV420_10BPP;
        break;
#else
        av_log(avctx, AV_LOG_ERROR, "10-bit encoding is not "
               "supported with this VAAPI version.\n");
        return AVERROR(ENOSYS);
#endif
    default:
        av_log(avctx, AV_LOG_ERROR, "Unknown H.265 profile %d.\n",
               avctx->profile);
        return AVERROR(EINVAL);
    }
    ctx->va_entrypoint = VAEntrypointEncSlice;

    if (avctx->bit_rate > 0) {
        if (avctx->rc_max_rate == avctx->bit_rate)
            ctx->va_rc_mode = VA_RC_CBR;
        else
            ctx->va_rc_mode = VA_RC_VBR;
    } else
        ctx->va_rc_mode = VA_RC_CQP;

    ctx->va_packed_headers =
        VA_ENC_PACKED_HEADER_SEQUENCE | // VPS, SPS and PPS.
        VA_ENC_PACKED_HEADER_SLICE;     // Slice headers.

    ctx->surface_width  = FFALIGN(avctx->width,  16);
    ctx->surface_height = FFALIGN(avctx->height, 16);

    return ff_vaapi_encode_init(avctx);
}

static av_cold int vaapi_encode_h265_close(AVCodecContext *avctx)
{
    VAAPIEncodeContext *ctx = avctx->priv_data;
    VAAPIEncodeH265Context *priv = ctx->priv_data;

    if (priv)
        ff_cbs_close(&priv->cbc);

    return ff_vaapi_encode_close(avctx);
}

#define OFFSET(x) (offsetof(VAAPIEncodeContext, codec_options_data) + \
                   offsetof(VAAPIEncodeH265Options, x))
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM)
static const AVOption vaapi_encode_h265_options[] = {
    { "qp", "Constant QP (for P-frames; scaled by qfactor/qoffset for I/B)",
      OFFSET(qp), AV_OPT_TYPE_INT, { .i64 = 25 }, 0, 52, FLAGS },

    { "aud", "Include AUD",
      OFFSET(aud), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, FLAGS },

    { "profile", "Set profile (general_profile_idc)",
      OFFSET(profile), AV_OPT_TYPE_INT,
      { .i64 = FF_PROFILE_HEVC_MAIN }, 0x00, 0xff, FLAGS, "profile" },

#define PROFILE(name, value)  name, NULL, 0, AV_OPT_TYPE_CONST, \
      { .i64 = value }, 0, 0, FLAGS, "profile"
    { PROFILE("main",               FF_PROFILE_HEVC_MAIN) },
    { PROFILE("main10",             FF_PROFILE_HEVC_MAIN_10) },
#undef PROFILE

    { "level", "Set level (general_level_idc)",
      OFFSET(level), AV_OPT_TYPE_INT,
      { .i64 = 153 }, 0x00, 0xff, FLAGS, "level" },

#define LEVEL(name, value) name, NULL, 0, AV_OPT_TYPE_CONST, \
      { .i64 = value }, 0, 0, FLAGS, "level"
    { LEVEL("1",    30) },
    { LEVEL("2",    60) },
    { LEVEL("2.1",  63) },
    { LEVEL("3",    90) },
    { LEVEL("3.1",  93) },
    { LEVEL("4",   120) },
    { LEVEL("4.1", 123) },
    { LEVEL("5",   150) },
    { LEVEL("5.1", 153) },
    { LEVEL("5.2", 156) },
    { LEVEL("6",   180) },
    { LEVEL("6.1", 183) },
    { LEVEL("6.2", 186) },
#undef LEVEL

    { NULL },
};

static const AVCodecDefault vaapi_encode_h265_defaults[] = {
    { "b",              "0"   },
    { "bf",             "2"   },
    { "g",              "120" },
    { "i_qfactor",      "1"   },
    { "i_qoffset",      "0"   },
    { "b_qfactor",      "6/5" },
    { "b_qoffset",      "0"   },
    { NULL },
};

static const AVClass vaapi_encode_h265_class = {
    .class_name = "h265_vaapi",
    .item_name  = av_default_item_name,
    .option     = vaapi_encode_h265_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_hevc_vaapi_encoder = {
    .name           = "hevc_vaapi",
    .long_name      = NULL_IF_CONFIG_SMALL("H.265/HEVC (VAAPI)"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_HEVC,
    .priv_data_size = (sizeof(VAAPIEncodeContext) +
                       sizeof(VAAPIEncodeH265Options)),
    .init           = &vaapi_encode_h265_init,
    .encode2        = &ff_vaapi_encode2,
    .close          = &vaapi_encode_h265_close,
    .priv_class     = &vaapi_encode_h265_class,
    .capabilities   = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_HARDWARE,
    .defaults       = vaapi_encode_h265_defaults,
    .pix_fmts = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_VAAPI,
        AV_PIX_FMT_NONE,
    },
    .wrapper_name   = "vaapi",
};
