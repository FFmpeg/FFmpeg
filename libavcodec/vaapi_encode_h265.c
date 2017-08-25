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
#include <va/va_enc_hevc.h>

#include "libavutil/avassert.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "libavutil/pixfmt.h"

#include "avcodec.h"
#include "hevc.h"
#include "internal.h"
#include "put_bits.h"
#include "vaapi_encode.h"
#include "vaapi_encode_h26x.h"


#define MAX_ST_REF_PIC_SETS  32
#define MAX_DPB_PICS         16
#define MAX_LAYERS            1


typedef struct VAAPIEncodeH265STRPS {
    char inter_ref_pic_set_prediction_flag;

    unsigned int num_negative_pics;
    unsigned int num_positive_pics;

    unsigned int delta_poc_s0_minus1[MAX_DPB_PICS];
    char used_by_curr_pic_s0_flag[MAX_DPB_PICS];

    unsigned int delta_poc_s1_minus1[MAX_DPB_PICS];
    char used_by_curr_pic_s1_flag[MAX_DPB_PICS];
} VAAPIEncodeH265STRPS;

// This structure contains all possibly-useful per-sequence syntax elements
// which are not already contained in the various VAAPI structures.
typedef struct VAAPIEncodeH265MiscSequenceParams {

    // Parameter set IDs.
    unsigned int video_parameter_set_id;
    unsigned int seq_parameter_set_id;

    // Layering.
    unsigned int vps_max_layers_minus1;
    unsigned int vps_max_sub_layers_minus1;
    char vps_temporal_id_nesting_flag;
    unsigned int vps_max_layer_id;
    unsigned int vps_num_layer_sets_minus1;
    unsigned int sps_max_sub_layers_minus1;
    char sps_temporal_id_nesting_flag;
    char layer_id_included_flag[MAX_LAYERS][64];

    // Profile/tier/level parameters.
    char general_profile_compatibility_flag[32];
    char general_progressive_source_flag;
    char general_interlaced_source_flag;
    char general_non_packed_constraint_flag;
    char general_frame_only_constraint_flag;
    char general_inbld_flag;

    // Decode/display ordering parameters.
    unsigned int log2_max_pic_order_cnt_lsb_minus4;
    char vps_sub_layer_ordering_info_present_flag;
    unsigned int vps_max_dec_pic_buffering_minus1[MAX_LAYERS];
    unsigned int vps_max_num_reorder_pics[MAX_LAYERS];
    unsigned int vps_max_latency_increase_plus1[MAX_LAYERS];
    char sps_sub_layer_ordering_info_present_flag;
    unsigned int sps_max_dec_pic_buffering_minus1[MAX_LAYERS];
    unsigned int sps_max_num_reorder_pics[MAX_LAYERS];
    unsigned int sps_max_latency_increase_plus1[MAX_LAYERS];

    // Timing information.
    char vps_timing_info_present_flag;
    unsigned int vps_num_units_in_tick;
    unsigned int vps_time_scale;
    char vps_poc_proportional_to_timing_flag;
    unsigned int vps_num_ticks_poc_diff_minus1;

    // Cropping information.
    char conformance_window_flag;
    unsigned int conf_win_left_offset;
    unsigned int conf_win_right_offset;
    unsigned int conf_win_top_offset;
    unsigned int conf_win_bottom_offset;

    // Short-term reference picture sets.
    unsigned int num_short_term_ref_pic_sets;
    VAAPIEncodeH265STRPS st_ref_pic_set[MAX_ST_REF_PIC_SETS];

    // Long-term reference pictures.
    char long_term_ref_pics_present_flag;
    unsigned int num_long_term_ref_pics_sps;
    struct {
        unsigned int lt_ref_pic_poc_lsb_sps;
        char used_by_curr_pic_lt_sps_flag;
    } lt_ref_pic;

    // Deblocking filter control.
    char deblocking_filter_control_present_flag;
    char deblocking_filter_override_enabled_flag;
    char pps_deblocking_filter_disabled_flag;
    int pps_beta_offset_div2;
    int pps_tc_offset_div2;

    // Video Usability Information.
    char vui_parameters_present_flag;
    char aspect_ratio_info_present_flag;
    unsigned int aspect_ratio_idc;
    unsigned int sar_width;
    unsigned int sar_height;
    char video_signal_type_present_flag;
    unsigned int video_format;
    char video_full_range_flag;
    char colour_description_present_flag;
    unsigned int colour_primaries;
    unsigned int transfer_characteristics;
    unsigned int matrix_coeffs;

    // Oddments.
    char uniform_spacing_flag;
    char output_flag_present_flag;
    char cabac_init_present_flag;
    unsigned int num_extra_slice_header_bits;
    char lists_modification_present_flag;
    char pps_slice_chroma_qp_offsets_present_flag;
    char pps_slice_chroma_offset_list_enabled_flag;
} VAAPIEncodeH265MiscSequenceParams;

// This structure contains all possibly-useful per-slice syntax elements
// which are not already contained in the various VAAPI structures.
typedef struct VAAPIEncodeH265MiscSliceParams {
    // Slice segments.
    char first_slice_segment_in_pic_flag;

    // Short-term reference picture sets.
    char short_term_ref_pic_set_sps_flag;
    unsigned int short_term_ref_pic_idx;
    VAAPIEncodeH265STRPS st_ref_pic_set;

    // Deblocking filter.
    char deblocking_filter_override_flag;

    // Oddments.
    char slice_reserved_flag[8];
    char no_output_of_prior_pics_flag;
    char pic_output_flag;
} VAAPIEncodeH265MiscSliceParams;

typedef struct VAAPIEncodeH265Slice {
    VAAPIEncodeH265MiscSliceParams misc_slice_params;

    int64_t pic_order_cnt;
} VAAPIEncodeH265Slice;

typedef struct VAAPIEncodeH265Context {
    VAAPIEncodeH265MiscSequenceParams misc_sequence_params;

    unsigned int ctu_width;
    unsigned int ctu_height;

    int fixed_qp_idr;
    int fixed_qp_p;
    int fixed_qp_b;

    int64_t last_idr_frame;

    // Rate control configuration.
    struct {
        VAEncMiscParameterBuffer misc;
        VAEncMiscParameterRateControl rc;
    } rc_params;
    struct {
        VAEncMiscParameterBuffer misc;
        VAEncMiscParameterHRD hrd;
    } hrd_params;
} VAAPIEncodeH265Context;

typedef struct VAAPIEncodeH265Options {
    int qp;
} VAAPIEncodeH265Options;


#define vseq_var(name)     vseq->name, name
#define vseq_field(name)   vseq->seq_fields.bits.name, name
#define vpic_var(name)     vpic->name, name
#define vpic_field(name)   vpic->pic_fields.bits.name, name
#define vslice_var(name)   vslice->name, name
#define vslice_field(name) vslice->slice_fields.bits.name, name
#define mseq_var(name)     mseq->name, name
#define mslice_var(name)   mslice->name, name
#define mstrps_var(name)   mstrps->name, name

static void vaapi_encode_h265_write_nal_unit_header(PutBitContext *pbc,
                                                    int nal_unit_type)
{
    u(1, 0, forbidden_zero_bit);
    u(6, nal_unit_type, nal_unit_type);
    u(6, 0, nuh_layer_id);
    u(3, 1, nuh_temporal_id_plus1);
}

static void vaapi_encode_h265_write_rbsp_trailing_bits(PutBitContext *pbc)
{
    u(1, 1, rbsp_stop_one_bit);
    while (put_bits_count(pbc) & 7)
        u(1, 0, rbsp_alignment_zero_bit);
}

static void vaapi_encode_h265_write_profile_tier_level(PutBitContext *pbc,
                                                       VAAPIEncodeContext *ctx)
{
    VAEncSequenceParameterBufferHEVC  *vseq = ctx->codec_sequence_params;
    VAAPIEncodeH265Context            *priv = ctx->priv_data;
    VAAPIEncodeH265MiscSequenceParams *mseq = &priv->misc_sequence_params;
    int j;

    if (1) {
        u(2, 0, general_profile_space);
        u(1, vseq_var(general_tier_flag));
        u(5, vseq_var(general_profile_idc));

        for (j = 0; j < 32; j++) {
            u(1, mseq_var(general_profile_compatibility_flag[j]));
        }

        u(1, mseq_var(general_progressive_source_flag));
        u(1, mseq_var(general_interlaced_source_flag));
        u(1, mseq_var(general_non_packed_constraint_flag));
        u(1, mseq_var(general_frame_only_constraint_flag));

        if (0) {
            // Not main profile.
            // Lots of extra constraint flags.
        } else {
            // put_bits only handles up to 31 bits.
            u(23, 0, general_reserved_zero_43bits);
            u(20, 0, general_reserved_zero_43bits);
        }

        if (vseq->general_profile_idc >= 1 && vseq->general_profile_idc <= 5) {
            u(1, mseq_var(general_inbld_flag));
        } else {
            u(1, 0, general_reserved_zero_bit);
        }
    }

    u(8, vseq_var(general_level_idc));

    // No sublayers.
}

static void vaapi_encode_h265_write_vps(PutBitContext *pbc,
                                        VAAPIEncodeContext *ctx)
{
    VAAPIEncodeH265Context            *priv = ctx->priv_data;
    VAAPIEncodeH265MiscSequenceParams *mseq = &priv->misc_sequence_params;
    int i, j;

    vaapi_encode_h265_write_nal_unit_header(pbc, HEVC_NAL_VPS);

    u(4, mseq->video_parameter_set_id, vps_video_parameter_set_id);

    u(1, 1, vps_base_layer_internal_flag);
    u(1, 1, vps_base_layer_available_flag);
    u(6, mseq_var(vps_max_layers_minus1));
    u(3, mseq_var(vps_max_sub_layers_minus1));
    u(1, mseq_var(vps_temporal_id_nesting_flag));

    u(16, 0xffff, vps_reserved_0xffff_16bits);

    vaapi_encode_h265_write_profile_tier_level(pbc, ctx);

    u(1, mseq_var(vps_sub_layer_ordering_info_present_flag));
    for (i = (mseq->vps_sub_layer_ordering_info_present_flag ?
              0 : mseq->vps_max_sub_layers_minus1);
         i <= mseq->vps_max_sub_layers_minus1; i++) {
        ue(mseq_var(vps_max_dec_pic_buffering_minus1[i]));
        ue(mseq_var(vps_max_num_reorder_pics[i]));
        ue(mseq_var(vps_max_latency_increase_plus1[i]));
    }

    u(6, mseq_var(vps_max_layer_id));
    ue(mseq_var(vps_num_layer_sets_minus1));
    for (i = 1; i <= mseq->vps_num_layer_sets_minus1; i++) {
        for (j = 0; j < mseq->vps_max_layer_id; j++)
            u(1, mseq_var(layer_id_included_flag[i][j]));
    }

    u(1, mseq_var(vps_timing_info_present_flag));
    if (mseq->vps_timing_info_present_flag) {
        u(1, 0, put_bits_hack_zero_bit);
        u(31, mseq_var(vps_num_units_in_tick));
        u(1, 0, put_bits_hack_zero_bit);
        u(31, mseq_var(vps_time_scale));
        u(1, mseq_var(vps_poc_proportional_to_timing_flag));
        if (mseq->vps_poc_proportional_to_timing_flag) {
            ue(mseq_var(vps_num_ticks_poc_diff_minus1));
        }
        ue(0, vps_num_hrd_parameters);
    }

    u(1, 0, vps_extension_flag);

    vaapi_encode_h265_write_rbsp_trailing_bits(pbc);
}

static void vaapi_encode_h265_write_st_ref_pic_set(PutBitContext *pbc,
                                                   int st_rps_idx,
                                                   VAAPIEncodeH265STRPS *mstrps)
{
    int i;

    if (st_rps_idx != 0)
       u(1, mstrps_var(inter_ref_pic_set_prediction_flag));

    if (mstrps->inter_ref_pic_set_prediction_flag) {
        av_assert0(0 && "inter ref pic set prediction not supported");
    } else {
        ue(mstrps_var(num_negative_pics));
        ue(mstrps_var(num_positive_pics));

        for (i = 0; i < mstrps->num_negative_pics; i++) {
            ue(mstrps_var(delta_poc_s0_minus1[i]));
            u(1, mstrps_var(used_by_curr_pic_s0_flag[i]));
        }
        for (i = 0; i < mstrps->num_positive_pics; i++) {
            ue(mstrps_var(delta_poc_s1_minus1[i]));
            u(1, mstrps_var(used_by_curr_pic_s1_flag[i]));
        }
    }
}

static void vaapi_encode_h265_write_vui_parameters(PutBitContext *pbc,
                                                   VAAPIEncodeContext *ctx)
{
    VAAPIEncodeH265Context            *priv = ctx->priv_data;
    VAAPIEncodeH265MiscSequenceParams *mseq = &priv->misc_sequence_params;

    u(1, mseq_var(aspect_ratio_info_present_flag));
    if (mseq->aspect_ratio_info_present_flag) {
        u(8, mseq_var(aspect_ratio_idc));
        if (mseq->aspect_ratio_idc == 255) {
            u(16, mseq_var(sar_width));
            u(16, mseq_var(sar_height));
        }
    }

    u(1, 0, overscan_info_present_flag);

    u(1, mseq_var(video_signal_type_present_flag));
    if (mseq->video_signal_type_present_flag) {
        u(3, mseq_var(video_format));
        u(1, mseq_var(video_full_range_flag));
        u(1, mseq_var(colour_description_present_flag));
        if (mseq->colour_description_present_flag) {
            u(8, mseq_var(colour_primaries));
            u(8, mseq_var(transfer_characteristics));
            u(8, mseq_var(matrix_coeffs));
        }
    }

    u(1, 0, chroma_loc_info_present_flag);
    u(1, 0, neutral_chroma_indication_flag);
    u(1, 0, field_seq_flag);
    u(1, 0, frame_field_info_present_flag);
    u(1, 0, default_display_window_flag);
    u(1, 0, vui_timing_info_present_flag);
    u(1, 0, bitstream_restriction_flag_flag);
}

static void vaapi_encode_h265_write_sps(PutBitContext *pbc,
                                        VAAPIEncodeContext *ctx)
{
    VAEncSequenceParameterBufferHEVC  *vseq = ctx->codec_sequence_params;
    VAAPIEncodeH265Context            *priv = ctx->priv_data;
    VAAPIEncodeH265MiscSequenceParams *mseq = &priv->misc_sequence_params;
    int i;

    vaapi_encode_h265_write_nal_unit_header(pbc, HEVC_NAL_SPS);

    u(4, mseq->video_parameter_set_id, sps_video_parameter_set_id);

    u(3, mseq_var(sps_max_sub_layers_minus1));
    u(1, mseq_var(sps_temporal_id_nesting_flag));

    vaapi_encode_h265_write_profile_tier_level(pbc, ctx);

    ue(mseq->seq_parameter_set_id, sps_seq_parameter_set_id);
    ue(vseq_field(chroma_format_idc));
    if (vseq->seq_fields.bits.chroma_format_idc == 3)
        u(1, 0, separate_colour_plane_flag);

    ue(vseq_var(pic_width_in_luma_samples));
    ue(vseq_var(pic_height_in_luma_samples));

    u(1, mseq_var(conformance_window_flag));
    if (mseq->conformance_window_flag) {
        ue(mseq_var(conf_win_left_offset));
        ue(mseq_var(conf_win_right_offset));
        ue(mseq_var(conf_win_top_offset));
        ue(mseq_var(conf_win_bottom_offset));
    }

    ue(vseq_field(bit_depth_luma_minus8));
    ue(vseq_field(bit_depth_chroma_minus8));

    ue(mseq_var(log2_max_pic_order_cnt_lsb_minus4));

    u(1, mseq_var(sps_sub_layer_ordering_info_present_flag));
    for (i = (mseq->sps_sub_layer_ordering_info_present_flag ?
              0 : mseq->sps_max_sub_layers_minus1);
         i <= mseq->sps_max_sub_layers_minus1; i++) {
        ue(mseq_var(sps_max_dec_pic_buffering_minus1[i]));
        ue(mseq_var(sps_max_num_reorder_pics[i]));
        ue(mseq_var(sps_max_latency_increase_plus1[i]));
    }

    ue(vseq_var(log2_min_luma_coding_block_size_minus3));
    ue(vseq_var(log2_diff_max_min_luma_coding_block_size));
    ue(vseq_var(log2_min_transform_block_size_minus2));
    ue(vseq_var(log2_diff_max_min_transform_block_size));
    ue(vseq_var(max_transform_hierarchy_depth_inter));
    ue(vseq_var(max_transform_hierarchy_depth_intra));

    u(1, vseq_field(scaling_list_enabled_flag));
    if (vseq->seq_fields.bits.scaling_list_enabled_flag) {
        u(1, 0, sps_scaling_list_data_present_flag);
    }

    u(1, vseq_field(amp_enabled_flag));
    u(1, vseq_field(sample_adaptive_offset_enabled_flag));

    u(1, vseq_field(pcm_enabled_flag));
    if (vseq->seq_fields.bits.pcm_enabled_flag) {
        u(4, vseq_var(pcm_sample_bit_depth_luma_minus1));
        u(4, vseq_var(pcm_sample_bit_depth_chroma_minus1));
        ue(vseq_var(log2_min_pcm_luma_coding_block_size_minus3));
        ue(vseq->log2_max_pcm_luma_coding_block_size_minus3 -
           vseq->log2_min_pcm_luma_coding_block_size_minus3,
           log2_diff_max_min_pcm_luma_coding_block_size);
        u(1, vseq_field(pcm_loop_filter_disabled_flag));
    }

    ue(mseq_var(num_short_term_ref_pic_sets));
    for (i = 0; i < mseq->num_short_term_ref_pic_sets; i++)
        vaapi_encode_h265_write_st_ref_pic_set(pbc, i,
                                               &mseq->st_ref_pic_set[i]);

    u(1, mseq_var(long_term_ref_pics_present_flag));
    if (mseq->long_term_ref_pics_present_flag) {
        ue(0, num_long_term_ref_pics_sps);
    }

    u(1, vseq_field(sps_temporal_mvp_enabled_flag));
    u(1, vseq_field(strong_intra_smoothing_enabled_flag));

    u(1, mseq_var(vui_parameters_present_flag));
    if (mseq->vui_parameters_present_flag) {
        vaapi_encode_h265_write_vui_parameters(pbc, ctx);
    }

    u(1, 0, sps_extension_present_flag);

    vaapi_encode_h265_write_rbsp_trailing_bits(pbc);
}

static void vaapi_encode_h265_write_pps(PutBitContext *pbc,
                                        VAAPIEncodeContext *ctx)
{
    VAEncPictureParameterBufferHEVC   *vpic = ctx->codec_picture_params;
    VAAPIEncodeH265Context            *priv = ctx->priv_data;
    VAAPIEncodeH265MiscSequenceParams *mseq = &priv->misc_sequence_params;
    int i;

    vaapi_encode_h265_write_nal_unit_header(pbc, HEVC_NAL_PPS);

    ue(vpic->slice_pic_parameter_set_id, pps_pic_parameter_set_id);
    ue(mseq->seq_parameter_set_id, pps_seq_parameter_set_id);

    u(1, vpic_field(dependent_slice_segments_enabled_flag));
    u(1, mseq_var(output_flag_present_flag));
    u(3, mseq_var(num_extra_slice_header_bits));
    u(1, vpic_field(sign_data_hiding_enabled_flag));
    u(1, mseq_var(cabac_init_present_flag));

    ue(vpic_var(num_ref_idx_l0_default_active_minus1));
    ue(vpic_var(num_ref_idx_l1_default_active_minus1));

    se(vpic->pic_init_qp - 26, init_qp_minus26);

    u(1, vpic_field(constrained_intra_pred_flag));
    u(1, vpic_field(transform_skip_enabled_flag));

    u(1, vpic_field(cu_qp_delta_enabled_flag));
    if (vpic->pic_fields.bits.cu_qp_delta_enabled_flag)
        ue(vpic_var(diff_cu_qp_delta_depth));

    se(vpic_var(pps_cb_qp_offset));
    se(vpic_var(pps_cr_qp_offset));

    u(1, mseq_var(pps_slice_chroma_qp_offsets_present_flag));
    u(1, vpic_field(weighted_pred_flag));
    u(1, vpic_field(weighted_bipred_flag));
    u(1, vpic_field(transquant_bypass_enabled_flag));
    u(1, vpic_field(tiles_enabled_flag));
    u(1, vpic_field(entropy_coding_sync_enabled_flag));

    if (vpic->pic_fields.bits.tiles_enabled_flag) {
        ue(vpic_var(num_tile_columns_minus1));
        ue(vpic_var(num_tile_rows_minus1));
        u(1, mseq_var(uniform_spacing_flag));
        if (!mseq->uniform_spacing_flag) {
            for (i = 0; i < vpic->num_tile_columns_minus1; i++)
                ue(vpic_var(column_width_minus1[i]));
            for (i = 0; i < vpic->num_tile_rows_minus1; i++)
                ue(vpic_var(row_height_minus1[i]));
        }
        u(1, vpic_field(loop_filter_across_tiles_enabled_flag));
    }

    u(1, vpic_field(pps_loop_filter_across_slices_enabled_flag));
    u(1, mseq_var(deblocking_filter_control_present_flag));
    if (mseq->deblocking_filter_control_present_flag) {
        u(1, mseq_var(deblocking_filter_override_enabled_flag));
        u(1, mseq_var(pps_deblocking_filter_disabled_flag));
        if (!mseq->pps_deblocking_filter_disabled_flag) {
            se(mseq_var(pps_beta_offset_div2));
            se(mseq_var(pps_tc_offset_div2));
        }
    }

    u(1, 0, pps_scaling_list_data_present_flag);
    // No scaling list data.

    u(1, mseq_var(lists_modification_present_flag));
    ue(vpic_var(log2_parallel_merge_level_minus2));
    u(1, 0, slice_segment_header_extension_present_flag);
    u(1, 0, pps_extension_present_flag);

    vaapi_encode_h265_write_rbsp_trailing_bits(pbc);
}

static void vaapi_encode_h265_write_slice_header2(PutBitContext *pbc,
                                                  VAAPIEncodeContext *ctx,
                                                  VAAPIEncodePicture *pic,
                                                  VAAPIEncodeSlice *slice)
{
    VAEncSequenceParameterBufferHEVC  *vseq = ctx->codec_sequence_params;
    VAEncPictureParameterBufferHEVC   *vpic = pic->codec_picture_params;
    VAEncSliceParameterBufferHEVC   *vslice = slice->codec_slice_params;
    VAAPIEncodeH265Context            *priv = ctx->priv_data;
    VAAPIEncodeH265MiscSequenceParams *mseq = &priv->misc_sequence_params;
    VAAPIEncodeH265Slice            *pslice = slice->priv_data;
    VAAPIEncodeH265MiscSliceParams  *mslice = &pslice->misc_slice_params;
    int i;

    vaapi_encode_h265_write_nal_unit_header(pbc, vpic->nal_unit_type);

    u(1, mslice_var(first_slice_segment_in_pic_flag));
    if (vpic->nal_unit_type >= HEVC_NAL_BLA_W_LP &&
       vpic->nal_unit_type <= 23)
        u(1, mslice_var(no_output_of_prior_pics_flag));

    ue(vslice_var(slice_pic_parameter_set_id));

    if (!mslice->first_slice_segment_in_pic_flag) {
        if (vpic->pic_fields.bits.dependent_slice_segments_enabled_flag)
            u(1, vslice_field(dependent_slice_segment_flag));
        u(av_log2((priv->ctu_width * priv->ctu_height) - 1) + 1,
          vslice_var(slice_segment_address));
    }
    if (!vslice->slice_fields.bits.dependent_slice_segment_flag) {
        for (i = 0; i < mseq->num_extra_slice_header_bits; i++)
            u(1, mslice_var(slice_reserved_flag[i]));

        ue(vslice_var(slice_type));
        if (mseq->output_flag_present_flag)
            u(1, 1, pic_output_flag);
        if (vseq->seq_fields.bits.separate_colour_plane_flag)
            u(2, vslice_field(colour_plane_id));
        if (vpic->nal_unit_type != HEVC_NAL_IDR_W_RADL &&
           vpic->nal_unit_type != HEVC_NAL_IDR_N_LP) {
            u(4 + mseq->log2_max_pic_order_cnt_lsb_minus4,
              (pslice->pic_order_cnt &
               ((1 << (mseq->log2_max_pic_order_cnt_lsb_minus4 + 4)) - 1)),
              slice_pic_order_cnt_lsb);

            u(1, mslice_var(short_term_ref_pic_set_sps_flag));
            if (!mslice->short_term_ref_pic_set_sps_flag) {
                vaapi_encode_h265_write_st_ref_pic_set(pbc, mseq->num_short_term_ref_pic_sets,
                                                       &mslice->st_ref_pic_set);
            } else if (mseq->num_short_term_ref_pic_sets > 1) {
                u(av_log2(mseq->num_short_term_ref_pic_sets - 1) + 1,
                  mslice_var(short_term_ref_pic_idx));
            }

            if (mseq->long_term_ref_pics_present_flag) {
                av_assert0(0);
            }
        }

        if (vseq->seq_fields.bits.sps_temporal_mvp_enabled_flag) {
            u(1, vslice_field(slice_temporal_mvp_enabled_flag));
        }

        if (vseq->seq_fields.bits.sample_adaptive_offset_enabled_flag) {
            u(1, vslice_field(slice_sao_luma_flag));
            if (!vseq->seq_fields.bits.separate_colour_plane_flag &&
                vseq->seq_fields.bits.chroma_format_idc != 0) {
                u(1, vslice_field(slice_sao_chroma_flag));
            }
        }

        if (vslice->slice_type == HEVC_SLICE_P || vslice->slice_type == HEVC_SLICE_B) {
            u(1, vslice_field(num_ref_idx_active_override_flag));
            if (vslice->slice_fields.bits.num_ref_idx_active_override_flag) {
                ue(vslice_var(num_ref_idx_l0_active_minus1));
                if (vslice->slice_type == HEVC_SLICE_B) {
                    ue(vslice_var(num_ref_idx_l1_active_minus1));
                }
            }

            if (mseq->lists_modification_present_flag) {
                av_assert0(0);
                // ref_pic_lists_modification()
            }
            if (vslice->slice_type == HEVC_SLICE_B) {
                u(1, vslice_field(mvd_l1_zero_flag));
            }
            if (mseq->cabac_init_present_flag) {
                u(1, vslice_field(cabac_init_flag));
            }
            if (vslice->slice_fields.bits.slice_temporal_mvp_enabled_flag) {
                if (vslice->slice_type == HEVC_SLICE_B)
                    u(1, vslice_field(collocated_from_l0_flag));
                ue(vpic->collocated_ref_pic_index, collocated_ref_idx);
            }
            if ((vpic->pic_fields.bits.weighted_pred_flag &&
                 vslice->slice_type == HEVC_SLICE_P) ||
                (vpic->pic_fields.bits.weighted_bipred_flag &&
                 vslice->slice_type == HEVC_SLICE_B)) {
                av_assert0(0);
                // pred_weight_table()
            }
            ue(5 - vslice->max_num_merge_cand, five_minus_max_num_merge_cand);
        }

        se(vslice_var(slice_qp_delta));
        if (mseq->pps_slice_chroma_qp_offsets_present_flag) {
            se(vslice_var(slice_cb_qp_offset));
            se(vslice_var(slice_cr_qp_offset));
        }
        if (mseq->pps_slice_chroma_offset_list_enabled_flag) {
            u(1, 0, cu_chroma_qp_offset_enabled_flag);
        }
        if (mseq->deblocking_filter_override_enabled_flag) {
            u(1, mslice_var(deblocking_filter_override_flag));
        }
        if (mslice->deblocking_filter_override_flag) {
            u(1, vslice_field(slice_deblocking_filter_disabled_flag));
            if (!vslice->slice_fields.bits.slice_deblocking_filter_disabled_flag) {
                se(vslice_var(slice_beta_offset_div2));
                se(vslice_var(slice_tc_offset_div2));
            }
        }
        if (vpic->pic_fields.bits.pps_loop_filter_across_slices_enabled_flag &&
            (vslice->slice_fields.bits.slice_sao_luma_flag ||
             vslice->slice_fields.bits.slice_sao_chroma_flag ||
             vslice->slice_fields.bits.slice_deblocking_filter_disabled_flag)) {
            u(1, vslice_field(slice_loop_filter_across_slices_enabled_flag));
        }

        if (vpic->pic_fields.bits.tiles_enabled_flag ||
            vpic->pic_fields.bits.entropy_coding_sync_enabled_flag) {
            // num_entry_point_offsets
        }

        if (0) {
            // slice_segment_header_extension_length
        }
    }

    u(1, 1, alignment_bit_equal_to_one);
    while (put_bits_count(pbc) & 7)
        u(1, 0, alignment_bit_equal_to_zero);
}

static int vaapi_encode_h265_write_sequence_header(AVCodecContext *avctx,
                                                   char *data, size_t *data_len)
{
    VAAPIEncodeContext *ctx = avctx->priv_data;
    PutBitContext pbc;
    char tmp[256];
    int err;
    size_t nal_len, bit_len, bit_pos, next_len;

    bit_len = *data_len;
    bit_pos = 0;

    init_put_bits(&pbc, tmp, sizeof(tmp));
    vaapi_encode_h265_write_vps(&pbc, ctx);
    nal_len = put_bits_count(&pbc);
    flush_put_bits(&pbc);

    next_len = bit_len - bit_pos;
    err = ff_vaapi_encode_h26x_nal_unit_to_byte_stream(data + bit_pos / 8,
                                                       &next_len,
                                                       tmp, nal_len);
    if (err < 0)
        return err;
    bit_pos += next_len;

    init_put_bits(&pbc, tmp, sizeof(tmp));
    vaapi_encode_h265_write_sps(&pbc, ctx);
    nal_len = put_bits_count(&pbc);
    flush_put_bits(&pbc);

    next_len = bit_len - bit_pos;
    err = ff_vaapi_encode_h26x_nal_unit_to_byte_stream(data + bit_pos / 8,
                                                       &next_len,
                                                       tmp, nal_len);
    if (err < 0)
        return err;
    bit_pos += next_len;

    init_put_bits(&pbc, tmp, sizeof(tmp));
    vaapi_encode_h265_write_pps(&pbc, ctx);
    nal_len = put_bits_count(&pbc);
    flush_put_bits(&pbc);

    next_len = bit_len - bit_pos;
    err = ff_vaapi_encode_h26x_nal_unit_to_byte_stream(data + bit_pos / 8,
                                                       &next_len,
                                                       tmp, nal_len);
    if (err < 0)
        return err;
    bit_pos += next_len;

    *data_len = bit_pos;
    return 0;
}

static int vaapi_encode_h265_write_slice_header(AVCodecContext *avctx,
                                                VAAPIEncodePicture *pic,
                                                VAAPIEncodeSlice *slice,
                                                char *data, size_t *data_len)
{
    VAAPIEncodeContext *ctx = avctx->priv_data;
    PutBitContext pbc;
    char tmp[256];
    size_t header_len;

    init_put_bits(&pbc, tmp, sizeof(tmp));
    vaapi_encode_h265_write_slice_header2(&pbc, ctx, pic, slice);
    header_len = put_bits_count(&pbc);
    flush_put_bits(&pbc);

    return ff_vaapi_encode_h26x_nal_unit_to_byte_stream(data, data_len,
                                                        tmp, header_len);
}

static int vaapi_encode_h265_init_sequence_params(AVCodecContext *avctx)
{
    VAAPIEncodeContext                 *ctx = avctx->priv_data;
    VAEncSequenceParameterBufferHEVC  *vseq = ctx->codec_sequence_params;
    VAEncPictureParameterBufferHEVC   *vpic = ctx->codec_picture_params;
    VAAPIEncodeH265Context            *priv = ctx->priv_data;
    VAAPIEncodeH265MiscSequenceParams *mseq = &priv->misc_sequence_params;
    int i;

    {
        // general_profile_space == 0.
        vseq->general_profile_idc = 1; // Main profile (ctx->codec_profile?)
        vseq->general_tier_flag = 0;

        vseq->general_level_idc = avctx->level * 3;

        vseq->intra_period = 0;
        vseq->intra_idr_period = 0;
        vseq->ip_period = 0;

        vseq->pic_width_in_luma_samples  = ctx->surface_width;
        vseq->pic_height_in_luma_samples = ctx->surface_height;

        vseq->seq_fields.bits.chroma_format_idc = 1; // 4:2:0.
        vseq->seq_fields.bits.separate_colour_plane_flag = 0;
        vseq->seq_fields.bits.bit_depth_luma_minus8 =
            avctx->profile == FF_PROFILE_HEVC_MAIN_10 ? 2 : 0;
        vseq->seq_fields.bits.bit_depth_chroma_minus8 =
            avctx->profile == FF_PROFILE_HEVC_MAIN_10 ? 2 : 0;
        // Other misc flags all zero.

        // These have to come from the capabilities of the encoder.  We have
        // no way to query it, so just hardcode ones which worked for me...
        // CTB size from 8x8 to 32x32.
        vseq->log2_min_luma_coding_block_size_minus3 = 0;
        vseq->log2_diff_max_min_luma_coding_block_size = 2;
        // Transform size from 4x4 to 32x32.
        vseq->log2_min_transform_block_size_minus2 = 0;
        vseq->log2_diff_max_min_transform_block_size = 3;
        // Full transform hierarchy allowed (2-5).
        vseq->max_transform_hierarchy_depth_inter = 3;
        vseq->max_transform_hierarchy_depth_intra = 3;

        vseq->vui_parameters_present_flag = 0;

        vseq->bits_per_second = avctx->bit_rate;
        if (avctx->framerate.num > 0 && avctx->framerate.den > 0) {
            vseq->vui_num_units_in_tick = avctx->framerate.den;
            vseq->vui_time_scale        = avctx->framerate.num;
        } else {
            vseq->vui_num_units_in_tick = avctx->time_base.num;
            vseq->vui_time_scale        = avctx->time_base.den;
        }

        vseq->intra_period     = avctx->gop_size;
        vseq->intra_idr_period = avctx->gop_size;
        vseq->ip_period        = ctx->b_per_p + 1;
    }

    {
        vpic->decoded_curr_pic.picture_id = VA_INVALID_ID;
        vpic->decoded_curr_pic.flags      = VA_PICTURE_HEVC_INVALID;

        for (i = 0; i < FF_ARRAY_ELEMS(vpic->reference_frames); i++) {
            vpic->reference_frames[i].picture_id = VA_INVALID_ID;
            vpic->reference_frames[i].flags      = VA_PICTURE_HEVC_INVALID;
        }

        vpic->collocated_ref_pic_index = 0xff;

        vpic->last_picture = 0;

        vpic->pic_init_qp = priv->fixed_qp_idr;

        vpic->diff_cu_qp_delta_depth = 0;
        vpic->pps_cb_qp_offset = 0;
        vpic->pps_cr_qp_offset = 0;

        // tiles_enabled_flag == 0, so ignore num_tile_(rows|columns)_minus1.

        vpic->log2_parallel_merge_level_minus2 = 0;

        // No limit on size.
        vpic->ctu_max_bitsize_allowed = 0;

        vpic->num_ref_idx_l0_default_active_minus1 = 0;
        vpic->num_ref_idx_l1_default_active_minus1 = 0;

        vpic->slice_pic_parameter_set_id = 0;

        vpic->pic_fields.bits.screen_content_flag = 0;
        vpic->pic_fields.bits.enable_gpu_weighted_prediction = 0;

        // Per-CU QP changes are required for non-constant-QP modes.
        vpic->pic_fields.bits.cu_qp_delta_enabled_flag =
            ctx->va_rc_mode != VA_RC_CQP;
    }

    {
        mseq->video_parameter_set_id = 5;
        mseq->seq_parameter_set_id = 5;

        mseq->vps_max_layers_minus1 = 0;
        mseq->vps_max_sub_layers_minus1 = 0;
        mseq->vps_temporal_id_nesting_flag = 1;
        mseq->sps_max_sub_layers_minus1 = 0;
        mseq->sps_temporal_id_nesting_flag = 1;

        for (i = 0; i < 32; i++) {
            mseq->general_profile_compatibility_flag[i] =
                (i == vseq->general_profile_idc);
        }

        mseq->general_progressive_source_flag    = 1;
        mseq->general_interlaced_source_flag     = 0;
        mseq->general_non_packed_constraint_flag = 0;
        mseq->general_frame_only_constraint_flag = 1;
        mseq->general_inbld_flag = 0;

        mseq->log2_max_pic_order_cnt_lsb_minus4 = 8;
        mseq->vps_sub_layer_ordering_info_present_flag = 0;
        mseq->vps_max_dec_pic_buffering_minus1[0] = (avctx->max_b_frames > 0) + 1;
        mseq->vps_max_num_reorder_pics[0]         = (avctx->max_b_frames > 0);
        mseq->vps_max_latency_increase_plus1[0]   = 0;
        mseq->sps_sub_layer_ordering_info_present_flag = 0;
        mseq->sps_max_dec_pic_buffering_minus1[0] = (avctx->max_b_frames > 0) + 1;
        mseq->sps_max_num_reorder_pics[0]         = (avctx->max_b_frames > 0);
        mseq->sps_max_latency_increase_plus1[0]   = 0;

        mseq->vps_timing_info_present_flag = 1;
        mseq->vps_num_units_in_tick = avctx->time_base.num;
        mseq->vps_time_scale        = avctx->time_base.den;
        mseq->vps_poc_proportional_to_timing_flag = 1;
        mseq->vps_num_ticks_poc_diff_minus1 = 0;

        if (avctx->width  != ctx->surface_width ||
            avctx->height != ctx->surface_height) {
            mseq->conformance_window_flag = 1;
            mseq->conf_win_left_offset   = 0;
            mseq->conf_win_right_offset  =
                (ctx->surface_width - avctx->width) / 2;
            mseq->conf_win_top_offset    = 0;
            mseq->conf_win_bottom_offset =
                (ctx->surface_height - avctx->height) / 2;
        } else {
            mseq->conformance_window_flag = 0;
        }

        mseq->num_short_term_ref_pic_sets = 0;
        // STRPSs should ideally be here rather than repeated in each slice.

        mseq->vui_parameters_present_flag = 1;
        if (avctx->sample_aspect_ratio.num != 0) {
            mseq->aspect_ratio_info_present_flag = 1;
            if (avctx->sample_aspect_ratio.num ==
                avctx->sample_aspect_ratio.den) {
                mseq->aspect_ratio_idc = 1;
            } else {
                mseq->aspect_ratio_idc = 255; // Extended SAR.
                mseq->sar_width  = avctx->sample_aspect_ratio.num;
                mseq->sar_height = avctx->sample_aspect_ratio.den;
            }
        }
        if (1) {
            // Should this be conditional on some of these being set?
            mseq->video_signal_type_present_flag = 1;
            mseq->video_format = 5; // Unspecified.
            mseq->video_full_range_flag = 0;
            mseq->colour_description_present_flag = 1;
            mseq->colour_primaries = avctx->color_primaries;
            mseq->transfer_characteristics = avctx->color_trc;
            mseq->matrix_coeffs = avctx->colorspace;
        }
    }

    return 0;
}

static int vaapi_encode_h265_init_picture_params(AVCodecContext *avctx,
                                                 VAAPIEncodePicture *pic)
{
    VAAPIEncodeContext               *ctx = avctx->priv_data;
    VAEncPictureParameterBufferHEVC *vpic = pic->codec_picture_params;
    VAAPIEncodeH265Context          *priv = ctx->priv_data;
    int i;

    if (pic->type == PICTURE_TYPE_IDR) {
        av_assert0(pic->display_order == pic->encode_order);
        priv->last_idr_frame = pic->display_order;
    } else {
        av_assert0(pic->encode_order > priv->last_idr_frame);
        // Display order need not be if we have RA[SD]L pictures, though.
    }

    vpic->decoded_curr_pic.picture_id    = pic->recon_surface;
    vpic->decoded_curr_pic.pic_order_cnt =
        pic->display_order - priv->last_idr_frame;
    vpic->decoded_curr_pic.flags         = 0;

    for (i = 0; i < pic->nb_refs; i++) {
        VAAPIEncodePicture *ref = pic->refs[i];
        av_assert0(ref);
        vpic->reference_frames[i].picture_id    = ref->recon_surface;
        vpic->reference_frames[i].pic_order_cnt =
            ref->display_order - priv->last_idr_frame;
        vpic->reference_frames[i].flags =
            (ref->display_order < pic->display_order ?
             VA_PICTURE_HEVC_RPS_ST_CURR_BEFORE : 0) |
            (ref->display_order > pic->display_order ?
             VA_PICTURE_HEVC_RPS_ST_CURR_AFTER  : 0);
    }
    for (; i < FF_ARRAY_ELEMS(vpic->reference_frames); i++) {
        vpic->reference_frames[i].picture_id = VA_INVALID_ID;
        vpic->reference_frames[i].flags      = VA_PICTURE_HEVC_INVALID;
    }

    vpic->coded_buf = pic->output_buffer;

    switch (pic->type) {
    case PICTURE_TYPE_IDR:
        vpic->nal_unit_type = HEVC_NAL_IDR_W_RADL;
        vpic->pic_fields.bits.idr_pic_flag = 1;
        vpic->pic_fields.bits.coding_type  = 1;
        vpic->pic_fields.bits.reference_pic_flag = 1;
        break;
    case PICTURE_TYPE_I:
        vpic->nal_unit_type = HEVC_NAL_TRAIL_R;
        vpic->pic_fields.bits.idr_pic_flag = 0;
        vpic->pic_fields.bits.coding_type  = 1;
        vpic->pic_fields.bits.reference_pic_flag = 1;
        break;
    case PICTURE_TYPE_P:
        vpic->nal_unit_type = HEVC_NAL_TRAIL_R;
        vpic->pic_fields.bits.idr_pic_flag = 0;
        vpic->pic_fields.bits.coding_type  = 2;
        vpic->pic_fields.bits.reference_pic_flag = 1;
        break;
    case PICTURE_TYPE_B:
        vpic->nal_unit_type = HEVC_NAL_TRAIL_R;
        vpic->pic_fields.bits.idr_pic_flag = 0;
        vpic->pic_fields.bits.coding_type  = 3;
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
    VAEncPictureParameterBufferHEVC  *vpic = pic->codec_picture_params;
    VAEncSliceParameterBufferHEVC  *vslice = slice->codec_slice_params;
    VAAPIEncodeH265Context           *priv = ctx->priv_data;
    VAAPIEncodeH265Slice           *pslice;
    VAAPIEncodeH265MiscSliceParams *mslice;
    int i;

    slice->priv_data = av_mallocz(sizeof(*pslice));
    if (!slice->priv_data)
        return AVERROR(ENOMEM);
    pslice = slice->priv_data;
    mslice = &pslice->misc_slice_params;

    // Currently we only support one slice per frame.
    vslice->slice_segment_address = 0;
    vslice->num_ctu_in_slice = priv->ctu_width * priv->ctu_height;

    switch (pic->type) {
    case PICTURE_TYPE_IDR:
    case PICTURE_TYPE_I:
        vslice->slice_type = HEVC_SLICE_I;
        break;
    case PICTURE_TYPE_P:
        vslice->slice_type = HEVC_SLICE_P;
        break;
    case PICTURE_TYPE_B:
        vslice->slice_type = HEVC_SLICE_B;
        break;
    default:
        av_assert0(0 && "invalid picture type");
    }

    vslice->slice_pic_parameter_set_id = vpic->slice_pic_parameter_set_id;

    pslice->pic_order_cnt = pic->display_order - priv->last_idr_frame;

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

        vslice->num_ref_idx_l0_active_minus1 = 0;
        vslice->ref_pic_list0[0] = vpic->reference_frames[0];
    }
    if (pic->nb_refs >= 2) {
        // Forward reference for B-frame.
        av_assert0(pic->type == PICTURE_TYPE_B);

        vslice->num_ref_idx_l1_active_minus1 = 0;
        vslice->ref_pic_list1[0] = vpic->reference_frames[1];
    }

    vslice->max_num_merge_cand = 5;

    if (pic->type == PICTURE_TYPE_B)
        vslice->slice_qp_delta = priv->fixed_qp_b  - vpic->pic_init_qp;
    else if (pic->type == PICTURE_TYPE_P)
        vslice->slice_qp_delta = priv->fixed_qp_p - vpic->pic_init_qp;
    else
        vslice->slice_qp_delta = priv->fixed_qp_idr - vpic->pic_init_qp;

    vslice->slice_fields.bits.last_slice_of_pic_flag = 1;

    mslice->first_slice_segment_in_pic_flag = 1;

    if (pic->type == PICTURE_TYPE_IDR) {
        // No reference pictures.
    } else if (0) {
        mslice->short_term_ref_pic_set_sps_flag = 1;
        mslice->short_term_ref_pic_idx = 0;
    } else {
        VAAPIEncodePicture *st;
        int used;

        mslice->short_term_ref_pic_set_sps_flag = 0;
        mslice->st_ref_pic_set.inter_ref_pic_set_prediction_flag = 0;

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
                // Currently true, but need not be.
                continue;
            }
            // This only works for one instance of each (delta_poc_sN_minus1
            // is relative to the previous frame in the list, not relative to
            // the current frame directly).
            if (st->display_order < pic->display_order) {
                i = mslice->st_ref_pic_set.num_negative_pics;
                mslice->st_ref_pic_set.delta_poc_s0_minus1[i] =
                    pic->display_order - st->display_order - 1;
                mslice->st_ref_pic_set.used_by_curr_pic_s0_flag[i] = used;
                ++mslice->st_ref_pic_set.num_negative_pics;
            } else {
                i = mslice->st_ref_pic_set.num_positive_pics;
                mslice->st_ref_pic_set.delta_poc_s1_minus1[i] =
                    st->display_order - pic->display_order - 1;
                mslice->st_ref_pic_set.used_by_curr_pic_s1_flag[i] = used;
                ++mslice->st_ref_pic_set.num_positive_pics;
            }
        }
    }

    return 0;
}

static av_cold int vaapi_encode_h265_configure(AVCodecContext *avctx)
{
    VAAPIEncodeContext      *ctx = avctx->priv_data;
    VAAPIEncodeH265Context *priv = ctx->priv_data;
    VAAPIEncodeH265Options  *opt = ctx->codec_options;

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
    VAAPIEncodeContext *ctx = avctx->priv_data;

    ctx->codec = &vaapi_encode_type_h265;

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

#define OFFSET(x) (offsetof(VAAPIEncodeContext, codec_options_data) + \
                   offsetof(VAAPIEncodeH265Options, x))
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM)
static const AVOption vaapi_encode_h265_options[] = {
    { "qp", "Constant QP (for P-frames; scaled by qfactor/qoffset for I/B)",
      OFFSET(qp), AV_OPT_TYPE_INT, { .i64 = 25 }, 0, 52, FLAGS },
    { NULL },
};

static const AVCodecDefault vaapi_encode_h265_defaults[] = {
    { "profile",        "1"   },
    { "level",          "51"  },
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
    .close          = &ff_vaapi_encode_close,
    .priv_class     = &vaapi_encode_h265_class,
    .capabilities   = AV_CODEC_CAP_DELAY,
    .defaults       = vaapi_encode_h265_defaults,
    .pix_fmts = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_VAAPI,
        AV_PIX_FMT_NONE,
    },
};
