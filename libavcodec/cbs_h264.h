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

#ifndef AVCODEC_CBS_H264_H
#define AVCODEC_CBS_H264_H

#include <stddef.h>
#include <stdint.h>

#include "cbs.h"
#include "cbs_h2645.h"
#include "h264.h"


enum {
    // This limit is arbitrary - it is sufficient for one message of each
    // type plus some repeats, and will therefore easily cover all sane
    // streams.  However, it is possible to make technically-valid streams
    // for which it will fail (for example, by including a large number of
    // user-data-unregistered messages).
    H264_MAX_SEI_PAYLOADS = 64,
};


typedef struct H264RawNALUnitHeader {
    uint8_t nal_ref_idc;
    uint8_t nal_unit_type;

    uint8_t svc_extension_flag;
    uint8_t avc_3d_extension_flag;
} H264RawNALUnitHeader;

typedef struct H264RawScalingList {
    int8_t delta_scale[64];
} H264RawScalingList;

typedef struct H264RawHRD {
    uint8_t cpb_cnt_minus1;
    uint8_t bit_rate_scale;
    uint8_t cpb_size_scale;

    uint32_t bit_rate_value_minus1[H264_MAX_CPB_CNT];
    uint32_t cpb_size_value_minus1[H264_MAX_CPB_CNT];
    uint8_t cbr_flag[H264_MAX_CPB_CNT];

    uint8_t initial_cpb_removal_delay_length_minus1;
    uint8_t cpb_removal_delay_length_minus1;
    uint8_t dpb_output_delay_length_minus1;
    uint8_t time_offset_length;
} H264RawHRD;

typedef struct H264RawVUI {
    uint8_t aspect_ratio_info_present_flag;
    uint8_t aspect_ratio_idc;
    uint16_t sar_width;
    uint16_t sar_height;

    uint8_t overscan_info_present_flag;
    uint8_t overscan_appropriate_flag;

    uint8_t video_signal_type_present_flag;
    uint8_t video_format;
    uint8_t video_full_range_flag;
    uint8_t colour_description_present_flag;
    uint8_t colour_primaries;
    uint8_t transfer_characteristics;
    uint8_t matrix_coefficients;

    uint8_t chroma_loc_info_present_flag;
    uint8_t chroma_sample_loc_type_top_field;
    uint8_t chroma_sample_loc_type_bottom_field;

    uint8_t timing_info_present_flag;
    uint32_t num_units_in_tick;
    uint32_t time_scale;
    uint8_t fixed_frame_rate_flag;

    uint8_t nal_hrd_parameters_present_flag;
    H264RawHRD nal_hrd_parameters;
    uint8_t vcl_hrd_parameters_present_flag;
    H264RawHRD vcl_hrd_parameters;
    uint8_t low_delay_hrd_flag;

    uint8_t pic_struct_present_flag;

    uint8_t bitstream_restriction_flag;
    uint8_t motion_vectors_over_pic_boundaries_flag;
    uint8_t max_bytes_per_pic_denom;
    uint8_t max_bits_per_mb_denom;
    uint8_t log2_max_mv_length_horizontal;
    uint8_t log2_max_mv_length_vertical;
    uint8_t max_num_reorder_frames;
    uint8_t max_dec_frame_buffering;
} H264RawVUI;

typedef struct H264RawSPS {
    H264RawNALUnitHeader nal_unit_header;

    uint8_t profile_idc;
    uint8_t constraint_set0_flag;
    uint8_t constraint_set1_flag;
    uint8_t constraint_set2_flag;
    uint8_t constraint_set3_flag;
    uint8_t constraint_set4_flag;
    uint8_t constraint_set5_flag;
    uint8_t reserved_zero_2bits;
    uint8_t level_idc;

    uint8_t seq_parameter_set_id;

    uint8_t chroma_format_idc;
    uint8_t separate_colour_plane_flag;
    uint8_t bit_depth_luma_minus8;
    uint8_t bit_depth_chroma_minus8;
    uint8_t qpprime_y_zero_transform_bypass_flag;

    uint8_t seq_scaling_matrix_present_flag;
    uint8_t seq_scaling_list_present_flag[12];
    H264RawScalingList scaling_list_4x4[6];
    H264RawScalingList scaling_list_8x8[6];

    uint8_t log2_max_frame_num_minus4;
    uint8_t pic_order_cnt_type;
    uint8_t log2_max_pic_order_cnt_lsb_minus4;
    uint8_t delta_pic_order_always_zero_flag;
    int32_t offset_for_non_ref_pic;
    int32_t offset_for_top_to_bottom_field;
    uint8_t num_ref_frames_in_pic_order_cnt_cycle;
    int32_t offset_for_ref_frame[256];

    uint8_t max_num_ref_frames;
    uint8_t gaps_in_frame_num_allowed_flag;

    uint16_t pic_width_in_mbs_minus1;
    uint16_t pic_height_in_map_units_minus1;

    uint8_t frame_mbs_only_flag;
    uint8_t mb_adaptive_frame_field_flag;
    uint8_t direct_8x8_inference_flag;

    uint8_t frame_cropping_flag;
    uint16_t frame_crop_left_offset;
    uint16_t frame_crop_right_offset;
    uint16_t frame_crop_top_offset;
    uint16_t frame_crop_bottom_offset;

    uint8_t vui_parameters_present_flag;
    H264RawVUI vui;
} H264RawSPS;

typedef struct H264RawSPSExtension {
    H264RawNALUnitHeader nal_unit_header;

    uint8_t seq_parameter_set_id;

    uint8_t aux_format_idc;
    uint8_t bit_depth_aux_minus8;
    uint8_t alpha_incr_flag;
    uint16_t alpha_opaque_value;
    uint16_t alpha_transparent_value;

    uint8_t additional_extension_flag;
} H264RawSPSExtension;

typedef struct H264RawPPS {
    H264RawNALUnitHeader nal_unit_header;

    uint8_t pic_parameter_set_id;
    uint8_t seq_parameter_set_id;

    uint8_t entropy_coding_mode_flag;
    uint8_t bottom_field_pic_order_in_frame_present_flag;

    uint8_t num_slice_groups_minus1;
    uint8_t slice_group_map_type;
    uint16_t run_length_minus1[H264_MAX_SLICE_GROUPS];
    uint16_t top_left[H264_MAX_SLICE_GROUPS];
    uint16_t bottom_right[H264_MAX_SLICE_GROUPS];
    uint8_t slice_group_change_direction_flag;
    uint16_t slice_group_change_rate_minus1;
    uint16_t pic_size_in_map_units_minus1;

    uint8_t *slice_group_id;
    AVBufferRef *slice_group_id_ref;

    uint8_t num_ref_idx_l0_default_active_minus1;
    uint8_t num_ref_idx_l1_default_active_minus1;

    uint8_t weighted_pred_flag;
    uint8_t weighted_bipred_idc;

    int8_t pic_init_qp_minus26;
    int8_t pic_init_qs_minus26;
    int8_t chroma_qp_index_offset;

    uint8_t deblocking_filter_control_present_flag;
    uint8_t constrained_intra_pred_flag;

    uint8_t more_rbsp_data;

    uint8_t redundant_pic_cnt_present_flag;
    uint8_t transform_8x8_mode_flag;

    uint8_t pic_scaling_matrix_present_flag;
    uint8_t pic_scaling_list_present_flag[12];
    H264RawScalingList scaling_list_4x4[6];
    H264RawScalingList scaling_list_8x8[6];

    int8_t second_chroma_qp_index_offset;
} H264RawPPS;

typedef struct H264RawAUD {
    H264RawNALUnitHeader nal_unit_header;

    uint8_t primary_pic_type;
} H264RawAUD;

typedef struct H264RawSEIBufferingPeriod {
    uint8_t seq_parameter_set_id;
    struct {
        uint32_t initial_cpb_removal_delay[H264_MAX_CPB_CNT];
        uint32_t initial_cpb_removal_delay_offset[H264_MAX_CPB_CNT];
    } nal, vcl;
} H264RawSEIBufferingPeriod;

typedef struct H264RawSEIPicTimestamp {
    uint8_t ct_type;
    uint8_t nuit_field_based_flag;
    uint8_t counting_type;
    uint8_t full_timestamp_flag;
    uint8_t discontinuity_flag;
    uint8_t cnt_dropped_flag;
    uint8_t n_frames;
    uint8_t seconds_flag;
    uint8_t seconds_value;
    uint8_t minutes_flag;
    uint8_t minutes_value;
    uint8_t hours_flag;
    uint8_t hours_value;
    int32_t time_offset;
} H264RawSEIPicTimestamp;

typedef struct H264RawSEIPicTiming {
    uint32_t cpb_removal_delay;
    uint32_t dpb_output_delay;
    uint8_t pic_struct;
    uint8_t clock_timestamp_flag[3];
    H264RawSEIPicTimestamp timestamp[3];
} H264RawSEIPicTiming;

typedef struct H264RawSEIPanScanRect {
    uint32_t pan_scan_rect_id;
    uint8_t  pan_scan_rect_cancel_flag;
    uint8_t  pan_scan_cnt_minus1;
    int32_t  pan_scan_rect_left_offset[3];
    int32_t  pan_scan_rect_right_offset[3];
    int32_t  pan_scan_rect_top_offset[3];
    int32_t  pan_scan_rect_bottom_offset[3];
    uint16_t pan_scan_rect_repetition_period;
} H264RawSEIPanScanRect;

typedef struct H264RawSEIUserDataRegistered {
    uint8_t itu_t_t35_country_code;
    uint8_t itu_t_t35_country_code_extension_byte;
    uint8_t *data;
    size_t data_length;
    AVBufferRef *data_ref;
} H264RawSEIUserDataRegistered;

typedef struct H264RawSEIUserDataUnregistered {
    uint8_t uuid_iso_iec_11578[16];
    uint8_t *data;
    size_t data_length;
    AVBufferRef *data_ref;
} H264RawSEIUserDataUnregistered;

typedef struct H264RawSEIRecoveryPoint {
    uint16_t recovery_frame_cnt;
    uint8_t exact_match_flag;
    uint8_t broken_link_flag;
    uint8_t changing_slice_group_idc;
} H264RawSEIRecoveryPoint;

typedef struct H264RawSEIDisplayOrientation {
    uint8_t display_orientation_cancel_flag;
    uint8_t hor_flip;
    uint8_t ver_flip;
    uint16_t anticlockwise_rotation;
    uint16_t display_orientation_repetition_period;
    uint8_t display_orientation_extension_flag;
} H264RawSEIDisplayOrientation;

typedef struct H264RawSEIMasteringDisplayColourVolume {
    uint16_t display_primaries_x[3];
    uint16_t display_primaries_y[3];
    uint16_t white_point_x;
    uint16_t white_point_y;
    uint32_t max_display_mastering_luminance;
    uint32_t min_display_mastering_luminance;
} H264RawSEIMasteringDisplayColourVolume;

typedef struct H264RawSEIAlternativeTransferCharacteristics {
    uint8_t preferred_transfer_characteristics;
} H264RawSEIAlternativeTransferCharacteristics;

typedef struct H264RawSEIPayload {
    uint32_t payload_type;
    uint32_t payload_size;
    union {
        H264RawSEIBufferingPeriod buffering_period;
        H264RawSEIPicTiming pic_timing;
        H264RawSEIPanScanRect pan_scan_rect;
        // H264RawSEIFiller filler -> no fields.
        H264RawSEIUserDataRegistered user_data_registered;
        H264RawSEIUserDataUnregistered user_data_unregistered;
        H264RawSEIRecoveryPoint recovery_point;
        H264RawSEIDisplayOrientation display_orientation;
        H264RawSEIMasteringDisplayColourVolume mastering_display_colour_volume;
        H264RawSEIAlternativeTransferCharacteristics
            alternative_transfer_characteristics;
        struct {
            uint8_t *data;
            size_t data_length;
            AVBufferRef *data_ref;
        } other;
    } payload;
} H264RawSEIPayload;

typedef struct H264RawSEI {
    H264RawNALUnitHeader nal_unit_header;

    H264RawSEIPayload payload[H264_MAX_SEI_PAYLOADS];
    uint8_t payload_count;
} H264RawSEI;

typedef struct H264RawSliceHeader {
    H264RawNALUnitHeader nal_unit_header;

    uint32_t first_mb_in_slice;
    uint8_t slice_type;

    uint8_t pic_parameter_set_id;

    uint8_t colour_plane_id;

    uint16_t frame_num;
    uint8_t field_pic_flag;
    uint8_t bottom_field_flag;

    uint16_t idr_pic_id;

    uint16_t pic_order_cnt_lsb;
    int32_t delta_pic_order_cnt_bottom;
    int32_t delta_pic_order_cnt[2];

    uint8_t redundant_pic_cnt;
    uint8_t direct_spatial_mv_pred_flag;

    uint8_t num_ref_idx_active_override_flag;
    uint8_t num_ref_idx_l0_active_minus1;
    uint8_t num_ref_idx_l1_active_minus1;

    uint8_t ref_pic_list_modification_flag_l0;
    uint8_t ref_pic_list_modification_flag_l1;
    struct {
        uint8_t modification_of_pic_nums_idc;
        int32_t abs_diff_pic_num_minus1;
        uint8_t long_term_pic_num;
    } rplm_l0[H264_MAX_RPLM_COUNT], rplm_l1[H264_MAX_RPLM_COUNT];

    uint8_t luma_log2_weight_denom;
    uint8_t chroma_log2_weight_denom;

    uint8_t luma_weight_l0_flag[H264_MAX_REFS];
    int8_t luma_weight_l0[H264_MAX_REFS];
    int8_t luma_offset_l0[H264_MAX_REFS];
    uint8_t chroma_weight_l0_flag[H264_MAX_REFS];
    int8_t chroma_weight_l0[H264_MAX_REFS][2];
    int8_t chroma_offset_l0[H264_MAX_REFS][2];

    uint8_t luma_weight_l1_flag[H264_MAX_REFS];
    int8_t luma_weight_l1[H264_MAX_REFS];
    int8_t luma_offset_l1[H264_MAX_REFS];
    uint8_t chroma_weight_l1_flag[H264_MAX_REFS];
    int8_t chroma_weight_l1[H264_MAX_REFS][2];
    int8_t chroma_offset_l1[H264_MAX_REFS][2];

    uint8_t no_output_of_prior_pics_flag;
    uint8_t long_term_reference_flag;

    uint8_t adaptive_ref_pic_marking_mode_flag;
    struct {
        uint8_t memory_management_control_operation;
        int32_t difference_of_pic_nums_minus1;
        uint8_t long_term_pic_num;
        uint8_t long_term_frame_idx;
        uint8_t max_long_term_frame_idx_plus1;
    } mmco[H264_MAX_MMCO_COUNT];

    uint8_t cabac_init_idc;

    int8_t slice_qp_delta;

    uint8_t sp_for_switch_flag;
    int8_t slice_qs_delta;

    uint8_t disable_deblocking_filter_idc;
    int8_t slice_alpha_c0_offset_div2;
    int8_t slice_beta_offset_div2;

    uint16_t slice_group_change_cycle;
} H264RawSliceHeader;

typedef struct H264RawSlice {
    H264RawSliceHeader header;

    uint8_t *data;
    size_t   data_size;
    int      data_bit_start;
    AVBufferRef *data_ref;
} H264RawSlice;

typedef struct H264RawFiller {
    H264RawNALUnitHeader nal_unit_header;

    uint32_t filler_size;
} H264RawFiller;


typedef struct CodedBitstreamH264Context {
    // Reader/writer context in common with the H.265 implementation.
    CodedBitstreamH2645Context common;

    // All currently available parameter sets.  These are updated when
    // any parameter set NAL unit is read/written with this context.
    AVBufferRef *sps_ref[H264_MAX_SPS_COUNT];
    AVBufferRef *pps_ref[H264_MAX_PPS_COUNT];
    H264RawSPS *sps[H264_MAX_SPS_COUNT];
    H264RawPPS *pps[H264_MAX_PPS_COUNT];

    // The currently active parameter sets.  These are updated when any
    // NAL unit refers to the relevant parameter set.  These pointers
    // must also be present in the arrays above.
    const H264RawSPS *active_sps;
    const H264RawPPS *active_pps;

    // The NAL unit type of the most recent normal slice.  This is required
    // to be able to read/write auxiliary slices, because IdrPicFlag is
    // otherwise unknown.
    uint8_t last_slice_nal_unit_type;
} CodedBitstreamH264Context;


/**
 * Add an SEI message to an access unit.
 *
 * On success, the payload will be owned by a unit in access_unit;
 * on failure, the content of the payload will be freed.
 */
int ff_cbs_h264_add_sei_message(CodedBitstreamFragment *access_unit,
                                H264RawSEIPayload *payload);

/**
 * Delete an SEI message from an access unit.
 *
 * Deletes from nal_unit, which must be an SEI NAL unit.  If this is the
 * last message in nal_unit, also deletes it from access_unit.
 *
 * Requires nal_unit to be a unit in access_unit and position to be >= 0
 * and < the payload count of the SEI nal_unit.
 */
void ff_cbs_h264_delete_sei_message(CodedBitstreamFragment *access_unit,
                                    CodedBitstreamUnit *nal_unit,
                                    int position);

#endif /* AVCODEC_CBS_H264_H */
