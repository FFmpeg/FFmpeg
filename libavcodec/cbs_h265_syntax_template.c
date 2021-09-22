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

static int FUNC(rbsp_trailing_bits)(CodedBitstreamContext *ctx, RWContext *rw)
{
    int err;

    fixed(1, rbsp_stop_one_bit, 1);
    while (byte_alignment(rw) != 0)
        fixed(1, rbsp_alignment_zero_bit, 0);

    return 0;
}

static int FUNC(nal_unit_header)(CodedBitstreamContext *ctx, RWContext *rw,
                                 H265RawNALUnitHeader *current,
                                 int expected_nal_unit_type)
{
    int err;

    fixed(1, forbidden_zero_bit, 0);

    if (expected_nal_unit_type >= 0)
        u(6, nal_unit_type, expected_nal_unit_type,
                            expected_nal_unit_type);
    else
        ub(6, nal_unit_type);

    u(6, nuh_layer_id,          0, 62);
    u(3, nuh_temporal_id_plus1, 1,  7);

    return 0;
}

static int FUNC(byte_alignment)(CodedBitstreamContext *ctx, RWContext *rw)
{
    int err;

    fixed(1, alignment_bit_equal_to_one, 1);
    while (byte_alignment(rw) != 0)
        fixed(1, alignment_bit_equal_to_zero, 0);

    return 0;
}

static int FUNC(extension_data)(CodedBitstreamContext *ctx, RWContext *rw,
                                H265RawExtensionData *current)
{
    int err;
    size_t k;
#ifdef READ
    GetBitContext start;
    uint8_t bit;
    start = *rw;
    for (k = 0; cbs_h2645_read_more_rbsp_data(rw); k++)
        skip_bits(rw, 1);
    current->bit_length = k;
    if (k > 0) {
        *rw = start;
        allocate(current->data, (current->bit_length + 7) / 8);
        for (k = 0; k < current->bit_length; k++) {
            xu(1, extension_data, bit, 0, 1, 0);
            current->data[k / 8] |= bit << (7 - k % 8);
        }
    }
#else
    for (k = 0; k < current->bit_length; k++)
        xu(1, extension_data, current->data[k / 8] >> (7 - k % 8) & 1, 0, 1, 0);
#endif
    return 0;
}

static int FUNC(profile_tier_level)(CodedBitstreamContext *ctx, RWContext *rw,
                                    H265RawProfileTierLevel *current,
                                    int profile_present_flag,
                                    int max_num_sub_layers_minus1)
{
    int err, i, j;

    if (profile_present_flag) {
        u(2, general_profile_space, 0, 0);
        flag(general_tier_flag);
        ub(5, general_profile_idc);

        for (j = 0; j < 32; j++)
            flags(general_profile_compatibility_flag[j], 1, j);

        flag(general_progressive_source_flag);
        flag(general_interlaced_source_flag);
        flag(general_non_packed_constraint_flag);
        flag(general_frame_only_constraint_flag);

#define profile_compatible(x) (current->general_profile_idc == (x) || \
                               current->general_profile_compatibility_flag[x])
        if (profile_compatible(4) || profile_compatible(5) ||
            profile_compatible(6) || profile_compatible(7) ||
            profile_compatible(8) || profile_compatible(9) ||
            profile_compatible(10) || profile_compatible(11)) {
            flag(general_max_12bit_constraint_flag);
            flag(general_max_10bit_constraint_flag);
            flag(general_max_8bit_constraint_flag);
            flag(general_max_422chroma_constraint_flag);
            flag(general_max_420chroma_constraint_flag);
            flag(general_max_monochrome_constraint_flag);
            flag(general_intra_constraint_flag);
            flag(general_one_picture_only_constraint_flag);
            flag(general_lower_bit_rate_constraint_flag);

            if (profile_compatible(5) || profile_compatible(9) ||
                profile_compatible(10) || profile_compatible(11)) {
                flag(general_max_14bit_constraint_flag);
                fixed(24, general_reserved_zero_33bits, 0);
                fixed( 9, general_reserved_zero_33bits, 0);
            } else {
                fixed(24, general_reserved_zero_34bits, 0);
                fixed(10, general_reserved_zero_34bits, 0);
            }
        } else if (profile_compatible(2)) {
            fixed(7, general_reserved_zero_7bits, 0);
            flag(general_one_picture_only_constraint_flag);
            fixed(24, general_reserved_zero_35bits, 0);
            fixed(11, general_reserved_zero_35bits, 0);
        } else {
            fixed(24, general_reserved_zero_43bits, 0);
            fixed(19, general_reserved_zero_43bits, 0);
        }

        if (profile_compatible(1) || profile_compatible(2) ||
            profile_compatible(3) || profile_compatible(4) ||
            profile_compatible(5) || profile_compatible(9) ||
            profile_compatible(11)) {
            flag(general_inbld_flag);
        } else {
            fixed(1, general_reserved_zero_bit, 0);
        }
#undef profile_compatible
    }

    ub(8, general_level_idc);

    for (i = 0; i < max_num_sub_layers_minus1; i++) {
        flags(sub_layer_profile_present_flag[i], 1, i);
        flags(sub_layer_level_present_flag[i],   1, i);
    }

    if (max_num_sub_layers_minus1 > 0) {
        for (i = max_num_sub_layers_minus1; i < 8; i++)
            fixed(2, reserved_zero_2bits, 0);
    }

    for (i = 0; i < max_num_sub_layers_minus1; i++) {
        if (current->sub_layer_profile_present_flag[i]) {
            us(2, sub_layer_profile_space[i], 0, 0, 1, i);
            flags(sub_layer_tier_flag[i],           1, i);
            ubs(5, sub_layer_profile_idc[i], 1, i);

            for (j = 0; j < 32; j++)
                flags(sub_layer_profile_compatibility_flag[i][j], 2, i, j);

            flags(sub_layer_progressive_source_flag[i],    1, i);
            flags(sub_layer_interlaced_source_flag[i],     1, i);
            flags(sub_layer_non_packed_constraint_flag[i], 1, i);
            flags(sub_layer_frame_only_constraint_flag[i], 1, i);

#define profile_compatible(x) (current->sub_layer_profile_idc[i] == (x) ||   \
                               current->sub_layer_profile_compatibility_flag[i][x])
            if (profile_compatible(4) || profile_compatible(5) ||
                profile_compatible(6) || profile_compatible(7) ||
                profile_compatible(8) || profile_compatible(9) ||
                profile_compatible(10) || profile_compatible(11)) {
                flags(sub_layer_max_12bit_constraint_flag[i],        1, i);
                flags(sub_layer_max_10bit_constraint_flag[i],        1, i);
                flags(sub_layer_max_8bit_constraint_flag[i],         1, i);
                flags(sub_layer_max_422chroma_constraint_flag[i],    1, i);
                flags(sub_layer_max_420chroma_constraint_flag[i],    1, i);
                flags(sub_layer_max_monochrome_constraint_flag[i],   1, i);
                flags(sub_layer_intra_constraint_flag[i],            1, i);
                flags(sub_layer_one_picture_only_constraint_flag[i], 1, i);
                flags(sub_layer_lower_bit_rate_constraint_flag[i],   1, i);

                if (profile_compatible(5) || profile_compatible(9) ||
                    profile_compatible(10) || profile_compatible(11)) {
                    flags(sub_layer_max_14bit_constraint_flag[i], 1, i);
                    fixed(24, sub_layer_reserved_zero_33bits, 0);
                    fixed( 9, sub_layer_reserved_zero_33bits, 0);
                } else {
                    fixed(24, sub_layer_reserved_zero_34bits, 0);
                    fixed(10, sub_layer_reserved_zero_34bits, 0);
                }
            } else if (profile_compatible(2)) {
                fixed(7, sub_layer_reserved_zero_7bits, 0);
                flags(sub_layer_one_picture_only_constraint_flag[i], 1, i);
                fixed(24, sub_layer_reserved_zero_43bits, 0);
                fixed(11, sub_layer_reserved_zero_43bits, 0);
            } else {
                fixed(24, sub_layer_reserved_zero_43bits, 0);
                fixed(19, sub_layer_reserved_zero_43bits, 0);
            }

            if (profile_compatible(1) || profile_compatible(2) ||
                profile_compatible(3) || profile_compatible(4) ||
                profile_compatible(5) || profile_compatible(9) ||
                profile_compatible(11)) {
                flags(sub_layer_inbld_flag[i], 1, i);
            } else {
                fixed(1, sub_layer_reserved_zero_bit, 0);
            }
#undef profile_compatible
        }
        if (current->sub_layer_level_present_flag[i])
            ubs(8, sub_layer_level_idc[i], 1, i);
    }

    return 0;
}

static int FUNC(sub_layer_hrd_parameters)(CodedBitstreamContext *ctx, RWContext *rw,
                                          H265RawHRDParameters *hrd,
                                          int nal, int sub_layer_id)
{
    H265RawSubLayerHRDParameters *current;
    int err, i;

    if (nal)
        current = &hrd->nal_sub_layer_hrd_parameters[sub_layer_id];
    else
        current = &hrd->vcl_sub_layer_hrd_parameters[sub_layer_id];

    for (i = 0; i <= hrd->cpb_cnt_minus1[sub_layer_id]; i++) {
        ues(bit_rate_value_minus1[i], 0, UINT32_MAX - 1, 1, i);
        ues(cpb_size_value_minus1[i], 0, UINT32_MAX - 1, 1, i);
        if (hrd->sub_pic_hrd_params_present_flag) {
            ues(cpb_size_du_value_minus1[i], 0, UINT32_MAX - 1, 1, i);
            ues(bit_rate_du_value_minus1[i], 0, UINT32_MAX - 1, 1, i);
        }
        flags(cbr_flag[i], 1, i);
    }

    return 0;
}

static int FUNC(hrd_parameters)(CodedBitstreamContext *ctx, RWContext *rw,
                                H265RawHRDParameters *current, int common_inf_present_flag,
                                int max_num_sub_layers_minus1)
{
    int err, i;

    if (common_inf_present_flag) {
        flag(nal_hrd_parameters_present_flag);
        flag(vcl_hrd_parameters_present_flag);

        if (current->nal_hrd_parameters_present_flag ||
            current->vcl_hrd_parameters_present_flag) {
            flag(sub_pic_hrd_params_present_flag);
            if (current->sub_pic_hrd_params_present_flag) {
                ub(8, tick_divisor_minus2);
                ub(5, du_cpb_removal_delay_increment_length_minus1);
                flag(sub_pic_cpb_params_in_pic_timing_sei_flag);
                ub(5, dpb_output_delay_du_length_minus1);
            }

            ub(4, bit_rate_scale);
            ub(4, cpb_size_scale);
            if (current->sub_pic_hrd_params_present_flag)
                ub(4, cpb_size_du_scale);

            ub(5, initial_cpb_removal_delay_length_minus1);
            ub(5, au_cpb_removal_delay_length_minus1);
            ub(5, dpb_output_delay_length_minus1);
        } else {
            infer(sub_pic_hrd_params_present_flag, 0);

            infer(initial_cpb_removal_delay_length_minus1, 23);
            infer(au_cpb_removal_delay_length_minus1,      23);
            infer(dpb_output_delay_length_minus1,          23);
        }
    }

    for (i = 0; i <= max_num_sub_layers_minus1; i++) {
        flags(fixed_pic_rate_general_flag[i], 1, i);

        if (!current->fixed_pic_rate_general_flag[i])
            flags(fixed_pic_rate_within_cvs_flag[i], 1, i);
        else
            infer(fixed_pic_rate_within_cvs_flag[i], 1);

        if (current->fixed_pic_rate_within_cvs_flag[i]) {
            ues(elemental_duration_in_tc_minus1[i], 0, 2047, 1, i);
            infer(low_delay_hrd_flag[i], 0);
        } else
            flags(low_delay_hrd_flag[i], 1, i);

        if (!current->low_delay_hrd_flag[i])
            ues(cpb_cnt_minus1[i], 0, 31, 1, i);
        else
            infer(cpb_cnt_minus1[i], 0);

        if (current->nal_hrd_parameters_present_flag)
            CHECK(FUNC(sub_layer_hrd_parameters)(ctx, rw, current, 0, i));
        if (current->vcl_hrd_parameters_present_flag)
            CHECK(FUNC(sub_layer_hrd_parameters)(ctx, rw, current, 1, i));
    }

    return 0;
}

static int FUNC(vui_parameters)(CodedBitstreamContext *ctx, RWContext *rw,
                                H265RawVUI *current, const H265RawSPS *sps)
{
    int err;

    flag(aspect_ratio_info_present_flag);
    if (current->aspect_ratio_info_present_flag) {
        ub(8, aspect_ratio_idc);
        if (current->aspect_ratio_idc == 255) {
            ub(16, sar_width);
            ub(16, sar_height);
        }
    } else {
        infer(aspect_ratio_idc, 0);
    }

    flag(overscan_info_present_flag);
    if (current->overscan_info_present_flag)
        flag(overscan_appropriate_flag);

    flag(video_signal_type_present_flag);
    if (current->video_signal_type_present_flag) {
        ub(3, video_format);
        flag(video_full_range_flag);
        flag(colour_description_present_flag);
        if (current->colour_description_present_flag) {
            ub(8, colour_primaries);
            ub(8, transfer_characteristics);
            ub(8, matrix_coefficients);
        } else {
            infer(colour_primaries,         2);
            infer(transfer_characteristics, 2);
            infer(matrix_coefficients,      2);
        }
    } else {
        infer(video_format,             5);
        infer(video_full_range_flag,    0);
        infer(colour_primaries,         2);
        infer(transfer_characteristics, 2);
        infer(matrix_coefficients,      2);
    }

    flag(chroma_loc_info_present_flag);
    if (current->chroma_loc_info_present_flag) {
        ue(chroma_sample_loc_type_top_field,    0, 5);
        ue(chroma_sample_loc_type_bottom_field, 0, 5);
    } else {
        infer(chroma_sample_loc_type_top_field,    0);
        infer(chroma_sample_loc_type_bottom_field, 0);
    }

    flag(neutral_chroma_indication_flag);
    flag(field_seq_flag);
    flag(frame_field_info_present_flag);

    flag(default_display_window_flag);
    if (current->default_display_window_flag) {
        ue(def_disp_win_left_offset,   0, 16384);
        ue(def_disp_win_right_offset,  0, 16384);
        ue(def_disp_win_top_offset,    0, 16384);
        ue(def_disp_win_bottom_offset, 0, 16384);
    }

    flag(vui_timing_info_present_flag);
    if (current->vui_timing_info_present_flag) {
        u(32, vui_num_units_in_tick, 1, UINT32_MAX);
        u(32, vui_time_scale,        1, UINT32_MAX);
        flag(vui_poc_proportional_to_timing_flag);
        if (current->vui_poc_proportional_to_timing_flag)
            ue(vui_num_ticks_poc_diff_one_minus1, 0, UINT32_MAX - 1);

        flag(vui_hrd_parameters_present_flag);
        if (current->vui_hrd_parameters_present_flag) {
            CHECK(FUNC(hrd_parameters)(ctx, rw, &current->hrd_parameters,
                                       1, sps->sps_max_sub_layers_minus1));
        }
    }

    flag(bitstream_restriction_flag);
    if (current->bitstream_restriction_flag) {
        flag(tiles_fixed_structure_flag);
        flag(motion_vectors_over_pic_boundaries_flag);
        flag(restricted_ref_pic_lists_flag);
        ue(min_spatial_segmentation_idc,  0, 4095);
        ue(max_bytes_per_pic_denom,       0, 16);
        ue(max_bits_per_min_cu_denom,     0, 16);
        ue(log2_max_mv_length_horizontal, 0, 16);
        ue(log2_max_mv_length_vertical,   0, 16);
    } else {
        infer(tiles_fixed_structure_flag,    0);
        infer(motion_vectors_over_pic_boundaries_flag, 1);
        infer(min_spatial_segmentation_idc,  0);
        infer(max_bytes_per_pic_denom,       2);
        infer(max_bits_per_min_cu_denom,     1);
        infer(log2_max_mv_length_horizontal, 15);
        infer(log2_max_mv_length_vertical,   15);
    }

    return 0;
}

static int FUNC(vps)(CodedBitstreamContext *ctx, RWContext *rw,
                     H265RawVPS *current)
{
    int err, i, j;

    HEADER("Video Parameter Set");

    CHECK(FUNC(nal_unit_header)(ctx, rw, &current->nal_unit_header, HEVC_NAL_VPS));

    ub(4, vps_video_parameter_set_id);

    flag(vps_base_layer_internal_flag);
    flag(vps_base_layer_available_flag);
    u(6, vps_max_layers_minus1,     0, HEVC_MAX_LAYERS - 1);
    u(3, vps_max_sub_layers_minus1, 0, HEVC_MAX_SUB_LAYERS - 1);
    flag(vps_temporal_id_nesting_flag);

    if (current->vps_max_sub_layers_minus1 == 0 &&
        current->vps_temporal_id_nesting_flag != 1) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid stream: "
               "vps_temporal_id_nesting_flag must be 1 if "
               "vps_max_sub_layers_minus1 is 0.\n");
        return AVERROR_INVALIDDATA;
    }

    fixed(16, vps_reserved_0xffff_16bits, 0xffff);

    CHECK(FUNC(profile_tier_level)(ctx, rw, &current->profile_tier_level,
                                   1, current->vps_max_sub_layers_minus1));

    flag(vps_sub_layer_ordering_info_present_flag);
    for (i = (current->vps_sub_layer_ordering_info_present_flag ?
              0 : current->vps_max_sub_layers_minus1);
         i <= current->vps_max_sub_layers_minus1; i++) {
        ues(vps_max_dec_pic_buffering_minus1[i],
            0, HEVC_MAX_DPB_SIZE - 1,                        1, i);
        ues(vps_max_num_reorder_pics[i],
            0, current->vps_max_dec_pic_buffering_minus1[i], 1, i);
        ues(vps_max_latency_increase_plus1[i],
            0, UINT32_MAX - 1,                               1, i);
    }
    if (!current->vps_sub_layer_ordering_info_present_flag) {
        for (i = 0; i < current->vps_max_sub_layers_minus1; i++) {
            infer(vps_max_dec_pic_buffering_minus1[i],
                  current->vps_max_dec_pic_buffering_minus1[current->vps_max_sub_layers_minus1]);
            infer(vps_max_num_reorder_pics[i],
                  current->vps_max_num_reorder_pics[current->vps_max_sub_layers_minus1]);
            infer(vps_max_latency_increase_plus1[i],
                  current->vps_max_latency_increase_plus1[current->vps_max_sub_layers_minus1]);
        }
    }

    u(6, vps_max_layer_id,        0, HEVC_MAX_LAYERS - 1);
    ue(vps_num_layer_sets_minus1, 0, HEVC_MAX_LAYER_SETS - 1);
    for (i = 1; i <= current->vps_num_layer_sets_minus1; i++) {
        for (j = 0; j <= current->vps_max_layer_id; j++)
            flags(layer_id_included_flag[i][j], 2, i, j);
    }
    for (j = 0; j <= current->vps_max_layer_id; j++)
        infer(layer_id_included_flag[0][j], j == 0);

    flag(vps_timing_info_present_flag);
    if (current->vps_timing_info_present_flag) {
        u(32, vps_num_units_in_tick, 1, UINT32_MAX);
        u(32, vps_time_scale,        1, UINT32_MAX);
        flag(vps_poc_proportional_to_timing_flag);
        if (current->vps_poc_proportional_to_timing_flag)
            ue(vps_num_ticks_poc_diff_one_minus1, 0, UINT32_MAX - 1);
        ue(vps_num_hrd_parameters, 0, current->vps_num_layer_sets_minus1 + 1);
        for (i = 0; i < current->vps_num_hrd_parameters; i++) {
            ues(hrd_layer_set_idx[i],
                current->vps_base_layer_internal_flag ? 0 : 1,
                current->vps_num_layer_sets_minus1, 1, i);
            if (i > 0)
                flags(cprms_present_flag[i], 1, i);
            else
                infer(cprms_present_flag[0], 1);

            CHECK(FUNC(hrd_parameters)(ctx, rw, &current->hrd_parameters[i],
                                       current->cprms_present_flag[i],
                                       current->vps_max_sub_layers_minus1));
        }
    }

    flag(vps_extension_flag);
    if (current->vps_extension_flag)
        CHECK(FUNC(extension_data)(ctx, rw, &current->extension_data));

    CHECK(FUNC(rbsp_trailing_bits)(ctx, rw));

    return 0;
}

static int FUNC(st_ref_pic_set)(CodedBitstreamContext *ctx, RWContext *rw,
                                H265RawSTRefPicSet *current, int st_rps_idx,
                                const H265RawSPS *sps)
{
    int err, i, j;

    if (st_rps_idx != 0)
        flag(inter_ref_pic_set_prediction_flag);
    else
        infer(inter_ref_pic_set_prediction_flag, 0);

    if (current->inter_ref_pic_set_prediction_flag) {
        unsigned int ref_rps_idx, num_delta_pocs, num_ref_pics;
        const H265RawSTRefPicSet *ref;
        int delta_rps, d_poc;
        int ref_delta_poc_s0[HEVC_MAX_REFS], ref_delta_poc_s1[HEVC_MAX_REFS];
        int delta_poc_s0[HEVC_MAX_REFS], delta_poc_s1[HEVC_MAX_REFS];
        uint8_t used_by_curr_pic_s0[HEVC_MAX_REFS],
                used_by_curr_pic_s1[HEVC_MAX_REFS];

        if (st_rps_idx == sps->num_short_term_ref_pic_sets)
            ue(delta_idx_minus1, 0, st_rps_idx - 1);
        else
            infer(delta_idx_minus1, 0);

        ref_rps_idx = st_rps_idx - (current->delta_idx_minus1 + 1);
        ref = &sps->st_ref_pic_set[ref_rps_idx];
        num_delta_pocs = ref->num_negative_pics + ref->num_positive_pics;
        av_assert0(num_delta_pocs < HEVC_MAX_DPB_SIZE);

        flag(delta_rps_sign);
        ue(abs_delta_rps_minus1, 0, INT16_MAX);
        delta_rps = (1 - 2 * current->delta_rps_sign) *
            (current->abs_delta_rps_minus1 + 1);

        num_ref_pics = 0;
        for (j = 0; j <= num_delta_pocs; j++) {
            flags(used_by_curr_pic_flag[j], 1, j);
            if (!current->used_by_curr_pic_flag[j])
                flags(use_delta_flag[j], 1, j);
            else
                infer(use_delta_flag[j], 1);
            if (current->use_delta_flag[j])
                ++num_ref_pics;
        }
        if (num_ref_pics >= HEVC_MAX_DPB_SIZE) {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid stream: "
                   "short-term ref pic set %d "
                   "contains too many pictures.\n", st_rps_idx);
            return AVERROR_INVALIDDATA;
        }

        // Since the stored form of an RPS here is actually the delta-step
        // form used when inter_ref_pic_set_prediction_flag is not set, we
        // need to reconstruct that here in order to be able to refer to
        // the RPS later (which is required for parsing, because we don't
        // even know what syntax elements appear without it).  Therefore,
        // this code takes the delta-step form of the reference set, turns
        // it into the delta-array form, applies the prediction process of
        // 7.4.8, converts the result back to the delta-step form, and
        // stores that as the current set for future use.  Note that the
        // inferences here mean that writers using prediction will need
        // to fill in the delta-step values correctly as well - since the
        // whole RPS prediction process is somewhat overly sophisticated,
        // this hopefully forms a useful check for them to ensure their
        // predicted form actually matches what was intended rather than
        // an onerous additional requirement.

        d_poc = 0;
        for (i = 0; i < ref->num_negative_pics; i++) {
            d_poc -= ref->delta_poc_s0_minus1[i] + 1;
            ref_delta_poc_s0[i] = d_poc;
        }
        d_poc = 0;
        for (i = 0; i < ref->num_positive_pics; i++) {
            d_poc += ref->delta_poc_s1_minus1[i] + 1;
            ref_delta_poc_s1[i] = d_poc;
        }

        i = 0;
        for (j = ref->num_positive_pics - 1; j >= 0; j--) {
            d_poc = ref_delta_poc_s1[j] + delta_rps;
            if (d_poc < 0 && current->use_delta_flag[ref->num_negative_pics + j]) {
                delta_poc_s0[i] = d_poc;
                used_by_curr_pic_s0[i++] =
                    current->used_by_curr_pic_flag[ref->num_negative_pics + j];
            }
        }
        if (delta_rps < 0 && current->use_delta_flag[num_delta_pocs]) {
            delta_poc_s0[i] = delta_rps;
            used_by_curr_pic_s0[i++] =
                current->used_by_curr_pic_flag[num_delta_pocs];
        }
        for (j = 0; j < ref->num_negative_pics; j++) {
            d_poc = ref_delta_poc_s0[j] + delta_rps;
            if (d_poc < 0 && current->use_delta_flag[j]) {
                delta_poc_s0[i] = d_poc;
                used_by_curr_pic_s0[i++] = current->used_by_curr_pic_flag[j];
            }
        }

        infer(num_negative_pics, i);
        for (i = 0; i < current->num_negative_pics; i++) {
            infer(delta_poc_s0_minus1[i],
                  -(delta_poc_s0[i] - (i == 0 ? 0 : delta_poc_s0[i - 1])) - 1);
            infer(used_by_curr_pic_s0_flag[i], used_by_curr_pic_s0[i]);
        }

        i = 0;
        for (j = ref->num_negative_pics - 1; j >= 0; j--) {
            d_poc = ref_delta_poc_s0[j] + delta_rps;
            if (d_poc > 0 && current->use_delta_flag[j]) {
                delta_poc_s1[i] = d_poc;
                used_by_curr_pic_s1[i++] = current->used_by_curr_pic_flag[j];
            }
        }
        if (delta_rps > 0 && current->use_delta_flag[num_delta_pocs]) {
            delta_poc_s1[i] = delta_rps;
            used_by_curr_pic_s1[i++] =
                current->used_by_curr_pic_flag[num_delta_pocs];
        }
        for (j = 0; j < ref->num_positive_pics; j++) {
            d_poc = ref_delta_poc_s1[j] + delta_rps;
            if (d_poc > 0 && current->use_delta_flag[ref->num_negative_pics + j]) {
                delta_poc_s1[i] = d_poc;
                used_by_curr_pic_s1[i++] =
                    current->used_by_curr_pic_flag[ref->num_negative_pics + j];
            }
        }

        infer(num_positive_pics, i);
        for (i = 0; i < current->num_positive_pics; i++) {
            infer(delta_poc_s1_minus1[i],
                  delta_poc_s1[i] - (i == 0 ? 0 : delta_poc_s1[i - 1]) - 1);
            infer(used_by_curr_pic_s1_flag[i], used_by_curr_pic_s1[i]);
        }

    } else {
        ue(num_negative_pics, 0, 15);
        ue(num_positive_pics, 0, 15 - current->num_negative_pics);

        for (i = 0; i < current->num_negative_pics; i++) {
            ues(delta_poc_s0_minus1[i], 0, INT16_MAX, 1, i);
            flags(used_by_curr_pic_s0_flag[i],        1, i);
        }

        for (i = 0; i < current->num_positive_pics; i++) {
            ues(delta_poc_s1_minus1[i], 0, INT16_MAX, 1, i);
            flags(used_by_curr_pic_s1_flag[i],        1, i);
        }
    }

    return 0;
}

static int FUNC(scaling_list_data)(CodedBitstreamContext *ctx, RWContext *rw,
                                   H265RawScalingList *current)
{
    int sizeId, matrixId;
    int err, n, i;

    for (sizeId = 0; sizeId < 4; sizeId++) {
        for (matrixId = 0; matrixId < 6; matrixId += (sizeId == 3 ? 3 : 1)) {
            flags(scaling_list_pred_mode_flag[sizeId][matrixId],
                  2, sizeId, matrixId);
            if (!current->scaling_list_pred_mode_flag[sizeId][matrixId]) {
                ues(scaling_list_pred_matrix_id_delta[sizeId][matrixId],
                    0, sizeId == 3 ? matrixId / 3 : matrixId,
                    2, sizeId, matrixId);
            } else {
                n = FFMIN(64, 1 << (4 + (sizeId << 1)));
                if (sizeId > 1) {
                    ses(scaling_list_dc_coef_minus8[sizeId - 2][matrixId], -7, +247,
                        2, sizeId - 2, matrixId);
                }
                for (i = 0; i < n; i++) {
                    ses(scaling_list_delta_coeff[sizeId][matrixId][i],
                        -128, +127, 3, sizeId, matrixId, i);
                }
            }
        }
    }

    return 0;
}

static int FUNC(sps_range_extension)(CodedBitstreamContext *ctx, RWContext *rw,
                                     H265RawSPS *current)
{
    int err;

    flag(transform_skip_rotation_enabled_flag);
    flag(transform_skip_context_enabled_flag);
    flag(implicit_rdpcm_enabled_flag);
    flag(explicit_rdpcm_enabled_flag);
    flag(extended_precision_processing_flag);
    flag(intra_smoothing_disabled_flag);
    flag(high_precision_offsets_enabled_flag);
    flag(persistent_rice_adaptation_enabled_flag);
    flag(cabac_bypass_alignment_enabled_flag);

    return 0;
}

static int FUNC(sps_scc_extension)(CodedBitstreamContext *ctx, RWContext *rw,
                                   H265RawSPS *current)
{
    int err, comp, i;

    flag(sps_curr_pic_ref_enabled_flag);

    flag(palette_mode_enabled_flag);
    if (current->palette_mode_enabled_flag) {
        ue(palette_max_size, 0, 64);
        ue(delta_palette_max_predictor_size, 0, 128);

        flag(sps_palette_predictor_initializer_present_flag);
        if (current->sps_palette_predictor_initializer_present_flag) {
            ue(sps_num_palette_predictor_initializer_minus1, 0, 127);
            for (comp = 0; comp < (current->chroma_format_idc ? 3 : 1); comp++) {
                int bit_depth = comp == 0 ? current->bit_depth_luma_minus8 + 8
                                          : current->bit_depth_chroma_minus8 + 8;
                for (i = 0; i <= current->sps_num_palette_predictor_initializer_minus1; i++)
                    ubs(bit_depth, sps_palette_predictor_initializers[comp][i], 2, comp, i);
            }
        }
    }

    u(2, motion_vector_resolution_control_idc, 0, 2);
    flag(intra_boundary_filtering_disable_flag);

    return 0;
}

static int FUNC(vui_parameters_default)(CodedBitstreamContext *ctx,
                                        RWContext *rw, H265RawVUI *current,
                                        H265RawSPS *sps)
{
    infer(aspect_ratio_idc, 0);

    infer(video_format,             5);
    infer(video_full_range_flag,    0);
    infer(colour_primaries,         2);
    infer(transfer_characteristics, 2);
    infer(matrix_coefficients,      2);

    infer(chroma_sample_loc_type_top_field,    0);
    infer(chroma_sample_loc_type_bottom_field, 0);

    infer(tiles_fixed_structure_flag,    0);
    infer(motion_vectors_over_pic_boundaries_flag, 1);
    infer(min_spatial_segmentation_idc,  0);
    infer(max_bytes_per_pic_denom,       2);
    infer(max_bits_per_min_cu_denom,     1);
    infer(log2_max_mv_length_horizontal, 15);
    infer(log2_max_mv_length_vertical,   15);

    return 0;
}

static int FUNC(sps)(CodedBitstreamContext *ctx, RWContext *rw,
                     H265RawSPS *current)
{
    CodedBitstreamH265Context *h265 = ctx->priv_data;
    const H265RawVPS *vps;
    int err, i;
    unsigned int min_cb_log2_size_y, ctb_log2_size_y,
                 min_cb_size_y,   min_tb_log2_size_y;

    HEADER("Sequence Parameter Set");

    CHECK(FUNC(nal_unit_header)(ctx, rw, &current->nal_unit_header, HEVC_NAL_SPS));

    ub(4, sps_video_parameter_set_id);
    h265->active_vps = vps = h265->vps[current->sps_video_parameter_set_id];

    u(3, sps_max_sub_layers_minus1, 0, HEVC_MAX_SUB_LAYERS - 1);
    flag(sps_temporal_id_nesting_flag);
    if (vps) {
        if (vps->vps_max_sub_layers_minus1 > current->sps_max_sub_layers_minus1) {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid stream: "
                   "sps_max_sub_layers_minus1 (%d) must be less than or equal to "
                   "vps_max_sub_layers_minus1 (%d).\n",
                   vps->vps_max_sub_layers_minus1,
                   current->sps_max_sub_layers_minus1);
            return AVERROR_INVALIDDATA;
        }
        if (vps->vps_temporal_id_nesting_flag &&
            !current->sps_temporal_id_nesting_flag) {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid stream: "
                   "sps_temporal_id_nesting_flag must be 1 if "
                   "vps_temporal_id_nesting_flag is 1.\n");
            return AVERROR_INVALIDDATA;
        }
    }

    CHECK(FUNC(profile_tier_level)(ctx, rw, &current->profile_tier_level,
                                   1, current->sps_max_sub_layers_minus1));

    ue(sps_seq_parameter_set_id, 0, 15);

    ue(chroma_format_idc, 0, 3);
    if (current->chroma_format_idc == 3)
        flag(separate_colour_plane_flag);
    else
        infer(separate_colour_plane_flag, 0);

    ue(pic_width_in_luma_samples,  1, HEVC_MAX_WIDTH);
    ue(pic_height_in_luma_samples, 1, HEVC_MAX_HEIGHT);

    flag(conformance_window_flag);
    if (current->conformance_window_flag) {
        ue(conf_win_left_offset,   0, current->pic_width_in_luma_samples);
        ue(conf_win_right_offset,  0, current->pic_width_in_luma_samples);
        ue(conf_win_top_offset,    0, current->pic_height_in_luma_samples);
        ue(conf_win_bottom_offset, 0, current->pic_height_in_luma_samples);
    } else {
        infer(conf_win_left_offset,   0);
        infer(conf_win_right_offset,  0);
        infer(conf_win_top_offset,    0);
        infer(conf_win_bottom_offset, 0);
    }

    ue(bit_depth_luma_minus8,   0, 8);
    ue(bit_depth_chroma_minus8, 0, 8);

    ue(log2_max_pic_order_cnt_lsb_minus4, 0, 12);

    flag(sps_sub_layer_ordering_info_present_flag);
    for (i = (current->sps_sub_layer_ordering_info_present_flag ?
              0 : current->sps_max_sub_layers_minus1);
         i <= current->sps_max_sub_layers_minus1; i++) {
        ues(sps_max_dec_pic_buffering_minus1[i],
            0, HEVC_MAX_DPB_SIZE - 1,                        1, i);
        ues(sps_max_num_reorder_pics[i],
            0, current->sps_max_dec_pic_buffering_minus1[i], 1, i);
        ues(sps_max_latency_increase_plus1[i],
            0, UINT32_MAX - 1,                               1, i);
    }
    if (!current->sps_sub_layer_ordering_info_present_flag) {
        for (i = 0; i < current->sps_max_sub_layers_minus1; i++) {
            infer(sps_max_dec_pic_buffering_minus1[i],
                  current->sps_max_dec_pic_buffering_minus1[current->sps_max_sub_layers_minus1]);
            infer(sps_max_num_reorder_pics[i],
                  current->sps_max_num_reorder_pics[current->sps_max_sub_layers_minus1]);
            infer(sps_max_latency_increase_plus1[i],
                  current->sps_max_latency_increase_plus1[current->sps_max_sub_layers_minus1]);
        }
    }

    ue(log2_min_luma_coding_block_size_minus3,   0, 3);
    min_cb_log2_size_y = current->log2_min_luma_coding_block_size_minus3 + 3;

    ue(log2_diff_max_min_luma_coding_block_size, 0, 3);
    ctb_log2_size_y = min_cb_log2_size_y +
        current->log2_diff_max_min_luma_coding_block_size;

    min_cb_size_y = 1 << min_cb_log2_size_y;
    if (current->pic_width_in_luma_samples  % min_cb_size_y ||
        current->pic_height_in_luma_samples % min_cb_size_y) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid dimensions: %ux%u not divisible "
               "by MinCbSizeY = %u.\n", current->pic_width_in_luma_samples,
               current->pic_height_in_luma_samples, min_cb_size_y);
        return AVERROR_INVALIDDATA;
    }

    ue(log2_min_luma_transform_block_size_minus2, 0, min_cb_log2_size_y - 3);
    min_tb_log2_size_y = current->log2_min_luma_transform_block_size_minus2 + 2;

    ue(log2_diff_max_min_luma_transform_block_size,
       0, FFMIN(ctb_log2_size_y, 5) - min_tb_log2_size_y);

    ue(max_transform_hierarchy_depth_inter,
       0, ctb_log2_size_y - min_tb_log2_size_y);
    ue(max_transform_hierarchy_depth_intra,
       0, ctb_log2_size_y - min_tb_log2_size_y);

    flag(scaling_list_enabled_flag);
    if (current->scaling_list_enabled_flag) {
        flag(sps_scaling_list_data_present_flag);
        if (current->sps_scaling_list_data_present_flag)
            CHECK(FUNC(scaling_list_data)(ctx, rw, &current->scaling_list));
    } else {
        infer(sps_scaling_list_data_present_flag, 0);
    }

    flag(amp_enabled_flag);
    flag(sample_adaptive_offset_enabled_flag);

    flag(pcm_enabled_flag);
    if (current->pcm_enabled_flag) {
        u(4, pcm_sample_bit_depth_luma_minus1,
          0, current->bit_depth_luma_minus8 + 8 - 1);
        u(4, pcm_sample_bit_depth_chroma_minus1,
          0, current->bit_depth_chroma_minus8 + 8 - 1);

        ue(log2_min_pcm_luma_coding_block_size_minus3,
           FFMIN(min_cb_log2_size_y, 5) - 3, FFMIN(ctb_log2_size_y, 5) - 3);
        ue(log2_diff_max_min_pcm_luma_coding_block_size,
           0, FFMIN(ctb_log2_size_y, 5) - (current->log2_min_pcm_luma_coding_block_size_minus3 + 3));

        flag(pcm_loop_filter_disabled_flag);
    }

    ue(num_short_term_ref_pic_sets, 0, HEVC_MAX_SHORT_TERM_REF_PIC_SETS);
    for (i = 0; i < current->num_short_term_ref_pic_sets; i++)
        CHECK(FUNC(st_ref_pic_set)(ctx, rw, &current->st_ref_pic_set[i], i, current));

    flag(long_term_ref_pics_present_flag);
    if (current->long_term_ref_pics_present_flag) {
        ue(num_long_term_ref_pics_sps, 0, HEVC_MAX_LONG_TERM_REF_PICS);
        for (i = 0; i < current->num_long_term_ref_pics_sps; i++) {
            ubs(current->log2_max_pic_order_cnt_lsb_minus4 + 4,
                lt_ref_pic_poc_lsb_sps[i], 1, i);
            flags(used_by_curr_pic_lt_sps_flag[i], 1, i);
        }
    }

    flag(sps_temporal_mvp_enabled_flag);
    flag(strong_intra_smoothing_enabled_flag);

    flag(vui_parameters_present_flag);
    if (current->vui_parameters_present_flag)
        CHECK(FUNC(vui_parameters)(ctx, rw, &current->vui, current));
    else
        CHECK(FUNC(vui_parameters_default)(ctx, rw, &current->vui, current));

    flag(sps_extension_present_flag);
    if (current->sps_extension_present_flag) {
        flag(sps_range_extension_flag);
        flag(sps_multilayer_extension_flag);
        flag(sps_3d_extension_flag);
        flag(sps_scc_extension_flag);
        ub(4, sps_extension_4bits);
    }

    if (current->sps_range_extension_flag)
        CHECK(FUNC(sps_range_extension)(ctx, rw, current));
    if (current->sps_multilayer_extension_flag)
        return AVERROR_PATCHWELCOME;
    if (current->sps_3d_extension_flag)
        return AVERROR_PATCHWELCOME;
    if (current->sps_scc_extension_flag)
        CHECK(FUNC(sps_scc_extension)(ctx, rw, current));
    if (current->sps_extension_4bits)
        CHECK(FUNC(extension_data)(ctx, rw, &current->extension_data));

    CHECK(FUNC(rbsp_trailing_bits)(ctx, rw));

    return 0;
}

static int FUNC(pps_range_extension)(CodedBitstreamContext *ctx, RWContext *rw,
                                     H265RawPPS *current)
{
    CodedBitstreamH265Context *h265 = ctx->priv_data;
    const H265RawSPS *sps = h265->active_sps;
    int err, i;

    if (current->transform_skip_enabled_flag)
        ue(log2_max_transform_skip_block_size_minus2, 0, 3);
    flag(cross_component_prediction_enabled_flag);

    flag(chroma_qp_offset_list_enabled_flag);
    if (current->chroma_qp_offset_list_enabled_flag) {
        ue(diff_cu_chroma_qp_offset_depth,
           0, sps->log2_diff_max_min_luma_coding_block_size);
        ue(chroma_qp_offset_list_len_minus1, 0, 5);
        for (i = 0; i <= current->chroma_qp_offset_list_len_minus1; i++) {
            ses(cb_qp_offset_list[i], -12, +12, 1, i);
            ses(cr_qp_offset_list[i], -12, +12, 1, i);
        }
    }

    ue(log2_sao_offset_scale_luma,   0, FFMAX(0, sps->bit_depth_luma_minus8   - 2));
    ue(log2_sao_offset_scale_chroma, 0, FFMAX(0, sps->bit_depth_chroma_minus8 - 2));

    return 0;
}

static int FUNC(pps_scc_extension)(CodedBitstreamContext *ctx, RWContext *rw,
                                   H265RawPPS *current)
{
    int err, comp, i;

    flag(pps_curr_pic_ref_enabled_flag);

    flag(residual_adaptive_colour_transform_enabled_flag);
    if (current->residual_adaptive_colour_transform_enabled_flag) {
        flag(pps_slice_act_qp_offsets_present_flag);
        se(pps_act_y_qp_offset_plus5,  -7, +17);
        se(pps_act_cb_qp_offset_plus5, -7, +17);
        se(pps_act_cr_qp_offset_plus3, -9, +15);
    } else {
        infer(pps_slice_act_qp_offsets_present_flag, 0);
        infer(pps_act_y_qp_offset_plus5,  0);
        infer(pps_act_cb_qp_offset_plus5, 0);
        infer(pps_act_cr_qp_offset_plus3, 0);
    }

    flag(pps_palette_predictor_initializer_present_flag);
    if (current->pps_palette_predictor_initializer_present_flag) {
        ue(pps_num_palette_predictor_initializer, 0, 128);
        if (current->pps_num_palette_predictor_initializer > 0) {
            flag(monochrome_palette_flag);
            ue(luma_bit_depth_entry_minus8, 0, 8);
            if (!current->monochrome_palette_flag)
                ue(chroma_bit_depth_entry_minus8, 0, 8);
            for (comp = 0; comp < (current->monochrome_palette_flag ? 1 : 3); comp++) {
                int bit_depth = comp == 0 ? current->luma_bit_depth_entry_minus8 + 8
                                          : current->chroma_bit_depth_entry_minus8 + 8;
                for (i = 0; i < current->pps_num_palette_predictor_initializer; i++)
                    ubs(bit_depth, pps_palette_predictor_initializers[comp][i], 2, comp, i);
            }
        }
    }

    return 0;
}

static int FUNC(pps)(CodedBitstreamContext *ctx, RWContext *rw,
                     H265RawPPS *current)
{
    CodedBitstreamH265Context *h265 = ctx->priv_data;
    const H265RawSPS *sps;
    int err, i;

    HEADER("Picture Parameter Set");

    CHECK(FUNC(nal_unit_header)(ctx, rw, &current->nal_unit_header, HEVC_NAL_PPS));

    ue(pps_pic_parameter_set_id, 0, 63);
    ue(pps_seq_parameter_set_id, 0, 15);
    sps = h265->sps[current->pps_seq_parameter_set_id];
    if (!sps) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "SPS id %d not available.\n",
               current->pps_seq_parameter_set_id);
        return AVERROR_INVALIDDATA;
    }
    h265->active_sps = sps;

    flag(dependent_slice_segments_enabled_flag);
    flag(output_flag_present_flag);
    ub(3, num_extra_slice_header_bits);
    flag(sign_data_hiding_enabled_flag);
    flag(cabac_init_present_flag);

    ue(num_ref_idx_l0_default_active_minus1, 0, 14);
    ue(num_ref_idx_l1_default_active_minus1, 0, 14);

    se(init_qp_minus26, -(26 + 6 * sps->bit_depth_luma_minus8), +25);

    flag(constrained_intra_pred_flag);
    flag(transform_skip_enabled_flag);
    flag(cu_qp_delta_enabled_flag);
    if (current->cu_qp_delta_enabled_flag)
        ue(diff_cu_qp_delta_depth,
           0, sps->log2_diff_max_min_luma_coding_block_size);
    else
        infer(diff_cu_qp_delta_depth, 0);

    se(pps_cb_qp_offset, -12, +12);
    se(pps_cr_qp_offset, -12, +12);
    flag(pps_slice_chroma_qp_offsets_present_flag);

    flag(weighted_pred_flag);
    flag(weighted_bipred_flag);

    flag(transquant_bypass_enabled_flag);
    flag(tiles_enabled_flag);
    flag(entropy_coding_sync_enabled_flag);

    if (current->tiles_enabled_flag) {
        ue(num_tile_columns_minus1, 0, HEVC_MAX_TILE_COLUMNS);
        ue(num_tile_rows_minus1,    0, HEVC_MAX_TILE_ROWS);
        flag(uniform_spacing_flag);
        if (!current->uniform_spacing_flag) {
            for (i = 0; i < current->num_tile_columns_minus1; i++)
                ues(column_width_minus1[i], 0, sps->pic_width_in_luma_samples,  1, i);
            for (i = 0; i < current->num_tile_rows_minus1; i++)
                ues(row_height_minus1[i],   0, sps->pic_height_in_luma_samples, 1, i);
        }
        flag(loop_filter_across_tiles_enabled_flag);
    } else {
        infer(num_tile_columns_minus1, 0);
        infer(num_tile_rows_minus1,    0);
    }

    flag(pps_loop_filter_across_slices_enabled_flag);
    flag(deblocking_filter_control_present_flag);
    if (current->deblocking_filter_control_present_flag) {
        flag(deblocking_filter_override_enabled_flag);
        flag(pps_deblocking_filter_disabled_flag);
        if (!current->pps_deblocking_filter_disabled_flag) {
            se(pps_beta_offset_div2, -6, +6);
            se(pps_tc_offset_div2,   -6, +6);
        } else {
            infer(pps_beta_offset_div2, 0);
            infer(pps_tc_offset_div2,   0);
        }
    } else {
        infer(deblocking_filter_override_enabled_flag, 0);
        infer(pps_deblocking_filter_disabled_flag,     0);
        infer(pps_beta_offset_div2, 0);
        infer(pps_tc_offset_div2,   0);
    }

    flag(pps_scaling_list_data_present_flag);
    if (current->pps_scaling_list_data_present_flag)
        CHECK(FUNC(scaling_list_data)(ctx, rw, &current->scaling_list));

    flag(lists_modification_present_flag);

    ue(log2_parallel_merge_level_minus2,
       0, (sps->log2_min_luma_coding_block_size_minus3 + 3 +
           sps->log2_diff_max_min_luma_coding_block_size - 2));

    flag(slice_segment_header_extension_present_flag);

    flag(pps_extension_present_flag);
    if (current->pps_extension_present_flag) {
        flag(pps_range_extension_flag);
        flag(pps_multilayer_extension_flag);
        flag(pps_3d_extension_flag);
        flag(pps_scc_extension_flag);
        ub(4, pps_extension_4bits);
    }
    if (current->pps_range_extension_flag)
        CHECK(FUNC(pps_range_extension)(ctx, rw, current));
    if (current->pps_multilayer_extension_flag)
        return AVERROR_PATCHWELCOME;
    if (current->pps_3d_extension_flag)
        return AVERROR_PATCHWELCOME;
    if (current->pps_scc_extension_flag)
        CHECK(FUNC(pps_scc_extension)(ctx, rw, current));
    if (current->pps_extension_4bits)
        CHECK(FUNC(extension_data)(ctx, rw, &current->extension_data));

    CHECK(FUNC(rbsp_trailing_bits)(ctx, rw));

    return 0;
}

static int FUNC(aud)(CodedBitstreamContext *ctx, RWContext *rw,
                     H265RawAUD *current)
{
    int err;

    HEADER("Access Unit Delimiter");

    CHECK(FUNC(nal_unit_header)(ctx, rw, &current->nal_unit_header, HEVC_NAL_AUD));

    u(3, pic_type, 0, 2);

    CHECK(FUNC(rbsp_trailing_bits)(ctx, rw));

    return 0;
}

static int FUNC(ref_pic_lists_modification)(CodedBitstreamContext *ctx, RWContext *rw,
                                            H265RawSliceHeader *current,
                                            unsigned int num_pic_total_curr)
{
    unsigned int entry_size;
    int err, i;

    entry_size = av_log2(num_pic_total_curr - 1) + 1;

    flag(ref_pic_list_modification_flag_l0);
    if (current->ref_pic_list_modification_flag_l0) {
        for (i = 0; i <= current->num_ref_idx_l0_active_minus1; i++)
            us(entry_size, list_entry_l0[i], 0, num_pic_total_curr - 1, 1, i);
    }

    if (current->slice_type == HEVC_SLICE_B) {
        flag(ref_pic_list_modification_flag_l1);
        if (current->ref_pic_list_modification_flag_l1) {
            for (i = 0; i <= current->num_ref_idx_l1_active_minus1; i++)
                us(entry_size, list_entry_l1[i], 0, num_pic_total_curr - 1, 1, i);
        }
    }

    return 0;
}

static int FUNC(pred_weight_table)(CodedBitstreamContext *ctx, RWContext *rw,
                                   H265RawSliceHeader *current)
{
    CodedBitstreamH265Context *h265 = ctx->priv_data;
    const H265RawSPS *sps = h265->active_sps;
    int err, i, j;
    int chroma = !sps->separate_colour_plane_flag &&
                  sps->chroma_format_idc != 0;

    ue(luma_log2_weight_denom, 0, 7);
    if (chroma)
        se(delta_chroma_log2_weight_denom, -7, 7);
    else
        infer(delta_chroma_log2_weight_denom, 0);

    for (i = 0; i <= current->num_ref_idx_l0_active_minus1; i++) {
        if (1 /* is not same POC and same layer_id */)
            flags(luma_weight_l0_flag[i], 1, i);
        else
            infer(luma_weight_l0_flag[i], 0);
    }
    if (chroma) {
        for (i = 0; i <= current->num_ref_idx_l0_active_minus1; i++) {
            if (1 /* is not same POC and same layer_id */)
                flags(chroma_weight_l0_flag[i], 1, i);
            else
                infer(chroma_weight_l0_flag[i], 0);
        }
    }

    for (i = 0; i <= current->num_ref_idx_l0_active_minus1; i++) {
        if (current->luma_weight_l0_flag[i]) {
            ses(delta_luma_weight_l0[i], -128, +127, 1, i);
            ses(luma_offset_l0[i],
                -(1 << (sps->bit_depth_luma_minus8 + 8 - 1)),
                ((1 << (sps->bit_depth_luma_minus8 + 8 - 1)) - 1), 1, i);
        } else {
            infer(delta_luma_weight_l0[i], 0);
            infer(luma_offset_l0[i],       0);
        }
        if (current->chroma_weight_l0_flag[i]) {
            for (j = 0; j < 2; j++) {
                ses(delta_chroma_weight_l0[i][j], -128, +127, 2, i, j);
                ses(chroma_offset_l0[i][j],
                    -(4 << (sps->bit_depth_chroma_minus8 + 8 - 1)),
                    ((4 << (sps->bit_depth_chroma_minus8 + 8 - 1)) - 1), 2, i, j);
            }
        } else {
            for (j = 0; j < 2; j++) {
                infer(delta_chroma_weight_l0[i][j], 0);
                infer(chroma_offset_l0[i][j],       0);
            }
        }
    }

    if (current->slice_type == HEVC_SLICE_B) {
        for (i = 0; i <= current->num_ref_idx_l1_active_minus1; i++) {
            if (1 /* RefPicList1[i] is not CurrPic, nor is it in a different layer */)
                flags(luma_weight_l1_flag[i], 1, i);
            else
                infer(luma_weight_l1_flag[i], 0);
        }
        if (chroma) {
            for (i = 0; i <= current->num_ref_idx_l1_active_minus1; i++) {
                if (1 /* RefPicList1[i] is not CurrPic, nor is it in a different layer */)
                    flags(chroma_weight_l1_flag[i], 1, i);
                else
                    infer(chroma_weight_l1_flag[i], 0);
            }
        }

        for (i = 0; i <= current->num_ref_idx_l1_active_minus1; i++) {
            if (current->luma_weight_l1_flag[i]) {
                ses(delta_luma_weight_l1[i], -128, +127, 1, i);
                ses(luma_offset_l1[i],
                    -(1 << (sps->bit_depth_luma_minus8 + 8 - 1)),
                    ((1 << (sps->bit_depth_luma_minus8 + 8 - 1)) - 1), 1, i);
            } else {
                infer(delta_luma_weight_l1[i], 0);
                infer(luma_offset_l1[i],       0);
            }
            if (current->chroma_weight_l1_flag[i]) {
                for (j = 0; j < 2; j++) {
                    ses(delta_chroma_weight_l1[i][j], -128, +127, 2, i, j);
                    ses(chroma_offset_l1[i][j],
                        -(4 << (sps->bit_depth_chroma_minus8 + 8 - 1)),
                        ((4 << (sps->bit_depth_chroma_minus8 + 8 - 1)) - 1), 2, i, j);
                }
            } else {
                for (j = 0; j < 2; j++) {
                    infer(delta_chroma_weight_l1[i][j], 0);
                    infer(chroma_offset_l1[i][j],       0);
                }
            }
        }
    }

    return 0;
}

static int FUNC(slice_segment_header)(CodedBitstreamContext *ctx, RWContext *rw,
                                      H265RawSliceHeader *current)
{
    CodedBitstreamH265Context *h265 = ctx->priv_data;
    const H265RawSPS *sps;
    const H265RawPPS *pps;
    unsigned int min_cb_log2_size_y, ctb_log2_size_y, ctb_size_y;
    unsigned int pic_width_in_ctbs_y, pic_height_in_ctbs_y, pic_size_in_ctbs_y;
    unsigned int num_pic_total_curr = 0;
    int err, i;

    HEADER("Slice Segment Header");

    CHECK(FUNC(nal_unit_header)(ctx, rw, &current->nal_unit_header, -1));

    flag(first_slice_segment_in_pic_flag);

    if (current->nal_unit_header.nal_unit_type >= HEVC_NAL_BLA_W_LP &&
        current->nal_unit_header.nal_unit_type <= HEVC_NAL_RSV_IRAP_VCL23)
        flag(no_output_of_prior_pics_flag);

    ue(slice_pic_parameter_set_id, 0, 63);

    pps = h265->pps[current->slice_pic_parameter_set_id];
    if (!pps) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "PPS id %d not available.\n",
               current->slice_pic_parameter_set_id);
        return AVERROR_INVALIDDATA;
    }
    h265->active_pps = pps;

    sps = h265->sps[pps->pps_seq_parameter_set_id];
    if (!sps) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "SPS id %d not available.\n",
               pps->pps_seq_parameter_set_id);
        return AVERROR_INVALIDDATA;
    }
    h265->active_sps = sps;

    min_cb_log2_size_y = sps->log2_min_luma_coding_block_size_minus3 + 3;
    ctb_log2_size_y = min_cb_log2_size_y + sps->log2_diff_max_min_luma_coding_block_size;
    ctb_size_y = 1 << ctb_log2_size_y;
    pic_width_in_ctbs_y =
        (sps->pic_width_in_luma_samples + ctb_size_y - 1) / ctb_size_y;
    pic_height_in_ctbs_y =
        (sps->pic_height_in_luma_samples + ctb_size_y - 1) / ctb_size_y;
    pic_size_in_ctbs_y = pic_width_in_ctbs_y * pic_height_in_ctbs_y;

    if (!current->first_slice_segment_in_pic_flag) {
        unsigned int address_size = av_log2(pic_size_in_ctbs_y - 1) + 1;
        if (pps->dependent_slice_segments_enabled_flag)
            flag(dependent_slice_segment_flag);
        else
            infer(dependent_slice_segment_flag, 0);
        u(address_size, slice_segment_address, 0, pic_size_in_ctbs_y - 1);
    } else {
        infer(dependent_slice_segment_flag, 0);
    }

    if (!current->dependent_slice_segment_flag) {
        for (i = 0; i < pps->num_extra_slice_header_bits; i++)
            flags(slice_reserved_flag[i], 1, i);

        ue(slice_type, 0, 2);

        if (pps->output_flag_present_flag)
            flag(pic_output_flag);

        if (sps->separate_colour_plane_flag)
            u(2, colour_plane_id, 0, 2);

        if (current->nal_unit_header.nal_unit_type != HEVC_NAL_IDR_W_RADL &&
            current->nal_unit_header.nal_unit_type != HEVC_NAL_IDR_N_LP) {
            const H265RawSTRefPicSet *rps;
            int dpb_slots_remaining;

            ub(sps->log2_max_pic_order_cnt_lsb_minus4 + 4, slice_pic_order_cnt_lsb);

            flag(short_term_ref_pic_set_sps_flag);
            if (!current->short_term_ref_pic_set_sps_flag) {
                CHECK(FUNC(st_ref_pic_set)(ctx, rw, &current->short_term_ref_pic_set,
                                           sps->num_short_term_ref_pic_sets, sps));
                rps = &current->short_term_ref_pic_set;
            } else if (sps->num_short_term_ref_pic_sets > 1) {
                unsigned int idx_size = av_log2(sps->num_short_term_ref_pic_sets - 1) + 1;
                u(idx_size, short_term_ref_pic_set_idx,
                  0, sps->num_short_term_ref_pic_sets - 1);
                rps = &sps->st_ref_pic_set[current->short_term_ref_pic_set_idx];
            } else {
                infer(short_term_ref_pic_set_idx, 0);
                rps = &sps->st_ref_pic_set[0];
            }

            dpb_slots_remaining = HEVC_MAX_DPB_SIZE - 1 -
                rps->num_negative_pics - rps->num_positive_pics;
            if (pps->pps_curr_pic_ref_enabled_flag &&
                (sps->sample_adaptive_offset_enabled_flag ||
                 !pps->pps_deblocking_filter_disabled_flag ||
                 pps->deblocking_filter_override_enabled_flag)) {
                // This picture will occupy two DPB slots.
                if (dpb_slots_remaining == 0) {
                    av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid stream: "
                           "short-term ref pic set contains too many pictures "
                           "to use with current picture reference enabled.\n");
                    return AVERROR_INVALIDDATA;
                }
                --dpb_slots_remaining;
            }

            num_pic_total_curr = 0;
            for (i = 0; i < rps->num_negative_pics; i++)
                if (rps->used_by_curr_pic_s0_flag[i])
                    ++num_pic_total_curr;
            for (i = 0; i < rps->num_positive_pics; i++)
                if (rps->used_by_curr_pic_s1_flag[i])
                    ++num_pic_total_curr;

            if (sps->long_term_ref_pics_present_flag) {
                unsigned int idx_size;

                if (sps->num_long_term_ref_pics_sps > 0) {
                    ue(num_long_term_sps, 0, FFMIN(sps->num_long_term_ref_pics_sps,
                                                   dpb_slots_remaining));
                    idx_size = av_log2(sps->num_long_term_ref_pics_sps - 1) + 1;
                    dpb_slots_remaining -= current->num_long_term_sps;
                } else {
                    infer(num_long_term_sps, 0);
                    idx_size = 0;
                }
                ue(num_long_term_pics, 0, dpb_slots_remaining);

                for (i = 0; i < current->num_long_term_sps +
                                current->num_long_term_pics; i++) {
                    if (i < current->num_long_term_sps) {
                        if (sps->num_long_term_ref_pics_sps > 1)
                            us(idx_size, lt_idx_sps[i],
                               0, sps->num_long_term_ref_pics_sps - 1, 1, i);
                        if (sps->used_by_curr_pic_lt_sps_flag[current->lt_idx_sps[i]])
                            ++num_pic_total_curr;
                    } else {
                        ubs(sps->log2_max_pic_order_cnt_lsb_minus4 + 4, poc_lsb_lt[i], 1, i);
                        flags(used_by_curr_pic_lt_flag[i], 1, i);
                        if (current->used_by_curr_pic_lt_flag[i])
                            ++num_pic_total_curr;
                    }
                    flags(delta_poc_msb_present_flag[i], 1, i);
                    if (current->delta_poc_msb_present_flag[i])
                        ues(delta_poc_msb_cycle_lt[i], 0, UINT32_MAX - 1, 1, i);
                    else
                        infer(delta_poc_msb_cycle_lt[i], 0);
                }
            }

            if (sps->sps_temporal_mvp_enabled_flag)
                flag(slice_temporal_mvp_enabled_flag);
            else
                infer(slice_temporal_mvp_enabled_flag, 0);

            if (pps->pps_curr_pic_ref_enabled_flag)
                ++num_pic_total_curr;
        }

        if (sps->sample_adaptive_offset_enabled_flag) {
            flag(slice_sao_luma_flag);
            if (!sps->separate_colour_plane_flag && sps->chroma_format_idc != 0)
                flag(slice_sao_chroma_flag);
            else
                infer(slice_sao_chroma_flag, 0);
        } else {
            infer(slice_sao_luma_flag,   0);
            infer(slice_sao_chroma_flag, 0);
        }

        if (current->slice_type == HEVC_SLICE_P ||
            current->slice_type == HEVC_SLICE_B) {
            flag(num_ref_idx_active_override_flag);
            if (current->num_ref_idx_active_override_flag) {
                ue(num_ref_idx_l0_active_minus1, 0, 14);
                if (current->slice_type == HEVC_SLICE_B)
                    ue(num_ref_idx_l1_active_minus1, 0, 14);
                else
                    infer(num_ref_idx_l1_active_minus1, pps->num_ref_idx_l1_default_active_minus1);
            } else {
                infer(num_ref_idx_l0_active_minus1, pps->num_ref_idx_l0_default_active_minus1);
                infer(num_ref_idx_l1_active_minus1, pps->num_ref_idx_l1_default_active_minus1);
            }

            if (pps->lists_modification_present_flag && num_pic_total_curr > 1)
                CHECK(FUNC(ref_pic_lists_modification)(ctx, rw, current,
                                                       num_pic_total_curr));

            if (current->slice_type == HEVC_SLICE_B)
                flag(mvd_l1_zero_flag);
            if (pps->cabac_init_present_flag)
                flag(cabac_init_flag);
            else
                infer(cabac_init_flag, 0);
            if (current->slice_temporal_mvp_enabled_flag) {
                if (current->slice_type == HEVC_SLICE_B)
                    flag(collocated_from_l0_flag);
                else
                    infer(collocated_from_l0_flag, 1);
                if (current->collocated_from_l0_flag) {
                    if (current->num_ref_idx_l0_active_minus1 > 0)
                        ue(collocated_ref_idx, 0, current->num_ref_idx_l0_active_minus1);
                    else
                        infer(collocated_ref_idx, 0);
                } else {
                    if (current->num_ref_idx_l1_active_minus1 > 0)
                        ue(collocated_ref_idx, 0, current->num_ref_idx_l1_active_minus1);
                    else
                        infer(collocated_ref_idx, 0);
                }
            }

            if ((pps->weighted_pred_flag   && current->slice_type == HEVC_SLICE_P) ||
                (pps->weighted_bipred_flag && current->slice_type == HEVC_SLICE_B))
                CHECK(FUNC(pred_weight_table)(ctx, rw, current));

            ue(five_minus_max_num_merge_cand, 0, 4);
            if (sps->motion_vector_resolution_control_idc == 2)
                flag(use_integer_mv_flag);
            else
                infer(use_integer_mv_flag, sps->motion_vector_resolution_control_idc);
        }

        se(slice_qp_delta,
           - 6 * sps->bit_depth_luma_minus8 - (pps->init_qp_minus26 + 26),
           + 51 - (pps->init_qp_minus26 + 26));
        if (pps->pps_slice_chroma_qp_offsets_present_flag) {
            se(slice_cb_qp_offset, -12, +12);
            se(slice_cr_qp_offset, -12, +12);
        } else {
            infer(slice_cb_qp_offset, 0);
            infer(slice_cr_qp_offset, 0);
        }
        if (pps->pps_slice_act_qp_offsets_present_flag) {
            se(slice_act_y_qp_offset,
               -12 - (pps->pps_act_y_qp_offset_plus5 - 5),
               +12 - (pps->pps_act_y_qp_offset_plus5 - 5));
            se(slice_act_cb_qp_offset,
               -12 - (pps->pps_act_cb_qp_offset_plus5 - 5),
               +12 - (pps->pps_act_cb_qp_offset_plus5 - 5));
            se(slice_act_cr_qp_offset,
               -12 - (pps->pps_act_cr_qp_offset_plus3 - 3),
               +12 - (pps->pps_act_cr_qp_offset_plus3 - 3));
        } else {
            infer(slice_act_y_qp_offset,  0);
            infer(slice_act_cb_qp_offset, 0);
            infer(slice_act_cr_qp_offset, 0);
        }
        if (pps->chroma_qp_offset_list_enabled_flag)
            flag(cu_chroma_qp_offset_enabled_flag);
        else
            infer(cu_chroma_qp_offset_enabled_flag, 0);

        if (pps->deblocking_filter_override_enabled_flag)
            flag(deblocking_filter_override_flag);
        else
            infer(deblocking_filter_override_flag, 0);
        if (current->deblocking_filter_override_flag) {
            flag(slice_deblocking_filter_disabled_flag);
            if (!current->slice_deblocking_filter_disabled_flag) {
                se(slice_beta_offset_div2, -6, +6);
                se(slice_tc_offset_div2,   -6, +6);
            } else {
                infer(slice_beta_offset_div2, pps->pps_beta_offset_div2);
                infer(slice_tc_offset_div2,   pps->pps_tc_offset_div2);
            }
        } else {
            infer(slice_deblocking_filter_disabled_flag,
                  pps->pps_deblocking_filter_disabled_flag);
            infer(slice_beta_offset_div2, pps->pps_beta_offset_div2);
            infer(slice_tc_offset_div2,   pps->pps_tc_offset_div2);
        }
        if (pps->pps_loop_filter_across_slices_enabled_flag &&
            (current->slice_sao_luma_flag || current->slice_sao_chroma_flag ||
             !current->slice_deblocking_filter_disabled_flag))
            flag(slice_loop_filter_across_slices_enabled_flag);
        else
            infer(slice_loop_filter_across_slices_enabled_flag,
                  pps->pps_loop_filter_across_slices_enabled_flag);
    }

    if (pps->tiles_enabled_flag || pps->entropy_coding_sync_enabled_flag) {
        unsigned int num_entry_point_offsets_limit;
        if (!pps->tiles_enabled_flag && pps->entropy_coding_sync_enabled_flag)
            num_entry_point_offsets_limit = pic_height_in_ctbs_y - 1;
        else if (pps->tiles_enabled_flag && !pps->entropy_coding_sync_enabled_flag)
            num_entry_point_offsets_limit =
                (pps->num_tile_columns_minus1 + 1) * (pps->num_tile_rows_minus1 + 1);
        else
            num_entry_point_offsets_limit =
                (pps->num_tile_columns_minus1 + 1) * pic_height_in_ctbs_y - 1;
        ue(num_entry_point_offsets, 0, num_entry_point_offsets_limit);

        if (current->num_entry_point_offsets > HEVC_MAX_ENTRY_POINT_OFFSETS) {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "Too many entry points: "
                   "%"PRIu16".\n", current->num_entry_point_offsets);
            return AVERROR_PATCHWELCOME;
        }

        if (current->num_entry_point_offsets > 0) {
            ue(offset_len_minus1, 0, 31);
            for (i = 0; i < current->num_entry_point_offsets; i++)
                ubs(current->offset_len_minus1 + 1, entry_point_offset_minus1[i], 1, i);
        }
    }

    if (pps->slice_segment_header_extension_present_flag) {
        ue(slice_segment_header_extension_length, 0, 256);
        for (i = 0; i < current->slice_segment_header_extension_length; i++)
            us(8, slice_segment_header_extension_data_byte[i], 0x00, 0xff, 1, i);
    }

    CHECK(FUNC(byte_alignment)(ctx, rw));

    return 0;
}

static int FUNC(sei_buffering_period)
    (CodedBitstreamContext *ctx, RWContext *rw,
     H265RawSEIBufferingPeriod *current, SEIMessageState *sei)
{
    CodedBitstreamH265Context *h265 = ctx->priv_data;
    const H265RawSPS *sps;
    const H265RawHRDParameters *hrd;
    int err, i, length;

#ifdef READ
    int start_pos, end_pos;
    start_pos = get_bits_count(rw);
#endif

    HEADER("Buffering Period");

    ue(bp_seq_parameter_set_id, 0, HEVC_MAX_SPS_COUNT - 1);

    sps = h265->sps[current->bp_seq_parameter_set_id];
    if (!sps) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "SPS id %d not available.\n",
               current->bp_seq_parameter_set_id);
        return AVERROR_INVALIDDATA;
    }
    h265->active_sps = sps;

    if (!sps->vui_parameters_present_flag ||
        !sps->vui.vui_hrd_parameters_present_flag) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "Buffering period SEI requires "
               "HRD parameters to be present in SPS.\n");
        return AVERROR_INVALIDDATA;
    }
    hrd = &sps->vui.hrd_parameters;
    if (!hrd->nal_hrd_parameters_present_flag &&
        !hrd->vcl_hrd_parameters_present_flag) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "Buffering period SEI requires "
               "NAL or VCL HRD parameters to be present.\n");
        return AVERROR_INVALIDDATA;
    }

    if (!hrd->sub_pic_hrd_params_present_flag)
        flag(irap_cpb_params_present_flag);
    else
        infer(irap_cpb_params_present_flag, 0);
    if (current->irap_cpb_params_present_flag) {
        length = hrd->au_cpb_removal_delay_length_minus1 + 1;
        ub(length, cpb_delay_offset);
        length = hrd->dpb_output_delay_length_minus1 + 1;
        ub(length, dpb_delay_offset);
    } else {
        infer(cpb_delay_offset, 0);
        infer(dpb_delay_offset, 0);
    }

    flag(concatenation_flag);

    length = hrd->au_cpb_removal_delay_length_minus1 + 1;
    ub(length, au_cpb_removal_delay_delta_minus1);

    if (hrd->nal_hrd_parameters_present_flag) {
        for (i = 0; i <= hrd->cpb_cnt_minus1[0]; i++) {
            length = hrd->initial_cpb_removal_delay_length_minus1 + 1;

            ubs(length, nal_initial_cpb_removal_delay[i], 1, i);
            ubs(length, nal_initial_cpb_removal_offset[i], 1, i);

            if (hrd->sub_pic_hrd_params_present_flag ||
                current->irap_cpb_params_present_flag) {
                ubs(length, nal_initial_alt_cpb_removal_delay[i], 1, i);
                ubs(length, nal_initial_alt_cpb_removal_offset[i], 1, i);
            }
        }
    }
    if (hrd->vcl_hrd_parameters_present_flag) {
        for (i = 0; i <= hrd->cpb_cnt_minus1[0]; i++) {
            length = hrd->initial_cpb_removal_delay_length_minus1 + 1;

            ubs(length, vcl_initial_cpb_removal_delay[i], 1, i);
            ubs(length, vcl_initial_cpb_removal_offset[i], 1, i);

            if (hrd->sub_pic_hrd_params_present_flag ||
                current->irap_cpb_params_present_flag) {
                ubs(length, vcl_initial_alt_cpb_removal_delay[i], 1, i);
                ubs(length, vcl_initial_alt_cpb_removal_offset[i], 1, i);
            }
        }
    }

#ifdef READ
    end_pos = get_bits_count(rw);
    if (cbs_h265_payload_extension_present(rw, sei->payload_size,
                                           end_pos - start_pos))
        flag(use_alt_cpb_params_flag);
    else
        infer(use_alt_cpb_params_flag, 0);
#else
    // If unknown extension data exists, then use_alt_cpb_params_flag is
    // coded in the bitstream and must be written even if it's 0.
    if (current->use_alt_cpb_params_flag || sei->extension_present) {
        flag(use_alt_cpb_params_flag);
        // Ensure this bit is not the last in the payload by making the
        // more_data_in_payload() check evaluate to true, so it may not
        // be mistaken as something else by decoders.
        sei->extension_present = 1;
    }
#endif

    return 0;
}

static int FUNC(sei_pic_timing)
    (CodedBitstreamContext *ctx, RWContext *rw,
     H265RawSEIPicTiming *current, SEIMessageState *sei)
{
    CodedBitstreamH265Context *h265 = ctx->priv_data;
    const H265RawSPS *sps;
    const H265RawHRDParameters *hrd;
    int err, expected_source_scan_type, i, length;

    HEADER("Picture Timing");

    sps = h265->active_sps;
    if (!sps) {
        av_log(ctx->log_ctx, AV_LOG_ERROR,
               "No active SPS for pic_timing.\n");
        return AVERROR_INVALIDDATA;
    }

    expected_source_scan_type = 2 -
        2 * sps->profile_tier_level.general_interlaced_source_flag -
        sps->profile_tier_level.general_progressive_source_flag;

    if (sps->vui.frame_field_info_present_flag) {
        u(4, pic_struct, 0, 12);
        u(2, source_scan_type,
          expected_source_scan_type >= 0 ? expected_source_scan_type : 0,
          expected_source_scan_type >= 0 ? expected_source_scan_type : 2);
        flag(duplicate_flag);
    } else {
        infer(pic_struct, 0);
        infer(source_scan_type,
              expected_source_scan_type >= 0 ? expected_source_scan_type : 2);
        infer(duplicate_flag, 0);
    }

    if (sps->vui_parameters_present_flag &&
        sps->vui.vui_hrd_parameters_present_flag)
        hrd = &sps->vui.hrd_parameters;
    else
        hrd = NULL;
    if (hrd && (hrd->nal_hrd_parameters_present_flag ||
                hrd->vcl_hrd_parameters_present_flag)) {
        length = hrd->au_cpb_removal_delay_length_minus1 + 1;
        ub(length, au_cpb_removal_delay_minus1);

        length = hrd->dpb_output_delay_length_minus1 + 1;
        ub(length, pic_dpb_output_delay);

        if (hrd->sub_pic_hrd_params_present_flag) {
            length = hrd->dpb_output_delay_du_length_minus1 + 1;
            ub(length, pic_dpb_output_du_delay);
        }

        if (hrd->sub_pic_hrd_params_present_flag &&
            hrd->sub_pic_cpb_params_in_pic_timing_sei_flag) {
            // Each decoding unit must contain at least one slice segment.
            ue(num_decoding_units_minus1, 0, HEVC_MAX_SLICE_SEGMENTS);
            flag(du_common_cpb_removal_delay_flag);

            length = hrd->du_cpb_removal_delay_increment_length_minus1 + 1;
            if (current->du_common_cpb_removal_delay_flag)
                ub(length, du_common_cpb_removal_delay_increment_minus1);

            for (i = 0; i <= current->num_decoding_units_minus1; i++) {
                ues(num_nalus_in_du_minus1[i],
                    0, HEVC_MAX_SLICE_SEGMENTS, 1, i);
                if (!current->du_common_cpb_removal_delay_flag &&
                    i < current->num_decoding_units_minus1)
                    ubs(length, du_cpb_removal_delay_increment_minus1[i], 1, i);
            }
        }
    }

    return 0;
}

static int FUNC(sei_pan_scan_rect)
    (CodedBitstreamContext *ctx, RWContext *rw,
     H265RawSEIPanScanRect *current, SEIMessageState *sei)
{
    int err, i;

    HEADER("Pan-Scan Rectangle");

    ue(pan_scan_rect_id, 0, UINT32_MAX - 1);
    flag(pan_scan_rect_cancel_flag);

    if (!current->pan_scan_rect_cancel_flag) {
        ue(pan_scan_cnt_minus1, 0, 2);

        for (i = 0; i <= current->pan_scan_cnt_minus1; i++) {
            ses(pan_scan_rect_left_offset[i],   INT32_MIN + 1, INT32_MAX, 1, i);
            ses(pan_scan_rect_right_offset[i],  INT32_MIN + 1, INT32_MAX, 1, i);
            ses(pan_scan_rect_top_offset[i],    INT32_MIN + 1, INT32_MAX, 1, i);
            ses(pan_scan_rect_bottom_offset[i], INT32_MIN + 1, INT32_MAX, 1, i);
        }

        flag(pan_scan_rect_persistence_flag);
    }

    return 0;
}

static int FUNC(sei_recovery_point)
    (CodedBitstreamContext *ctx, RWContext *rw,
     H265RawSEIRecoveryPoint *current, SEIMessageState *sei)
{
    int err;

    HEADER("Recovery Point");

    se(recovery_poc_cnt, -32768, 32767);

    flag(exact_match_flag);
    flag(broken_link_flag);

    return 0;
}

static int FUNC(film_grain_characteristics)(CodedBitstreamContext *ctx, RWContext *rw,
                                            H265RawFilmGrainCharacteristics *current,
                                            SEIMessageState *state)
{
    CodedBitstreamH265Context *h265 = ctx->priv_data;
    const H265RawSPS *sps = h265->active_sps;
    int err, c, i, j;

    HEADER("Film Grain Characteristics");

    flag(film_grain_characteristics_cancel_flag);
    if (!current->film_grain_characteristics_cancel_flag) {
        int filmGrainBitDepth[3];

        u(2, film_grain_model_id, 0, 1);
        flag(separate_colour_description_present_flag);
        if (current->separate_colour_description_present_flag) {
            ub(3, film_grain_bit_depth_luma_minus8);
            ub(3, film_grain_bit_depth_chroma_minus8);
            flag(film_grain_full_range_flag);
            ub(8, film_grain_colour_primaries);
            ub(8, film_grain_transfer_characteristics);
            ub(8, film_grain_matrix_coeffs);
        } else {
            if (!sps) {
                av_log(ctx->log_ctx, AV_LOG_ERROR,
                       "No active SPS for film_grain_characteristics.\n");
                return AVERROR_INVALIDDATA;
            }
            infer(film_grain_bit_depth_luma_minus8, sps->bit_depth_luma_minus8);
            infer(film_grain_bit_depth_chroma_minus8, sps->bit_depth_chroma_minus8);
            infer(film_grain_full_range_flag, sps->vui.video_full_range_flag);
            infer(film_grain_colour_primaries, sps->vui.colour_primaries);
            infer(film_grain_transfer_characteristics, sps->vui.transfer_characteristics);
            infer(film_grain_matrix_coeffs, sps->vui.matrix_coefficients);
        }

        filmGrainBitDepth[0] = current->film_grain_bit_depth_luma_minus8 + 8;
        filmGrainBitDepth[1] =
        filmGrainBitDepth[2] = current->film_grain_bit_depth_chroma_minus8 + 8;

        u(2, blending_mode_id, 0, 1);
        ub(4, log2_scale_factor);
        for (c = 0; c < 3; c++)
            flags(comp_model_present_flag[c], 1, c);
        for (c = 0; c < 3; c++) {
            if (current->comp_model_present_flag[c]) {
                ubs(8, num_intensity_intervals_minus1[c], 1, c);
                us(3, num_model_values_minus1[c], 0, 5, 1, c);
                for (i = 0; i <= current->num_intensity_intervals_minus1[c]; i++) {
                    ubs(8, intensity_interval_lower_bound[c][i], 2, c, i);
                    ubs(8, intensity_interval_upper_bound[c][i], 2, c, i);
                    for (j = 0; j <= current->num_model_values_minus1[c]; j++)
                        ses(comp_model_value[c][i][j],      0 - current->film_grain_model_id * (1 << (filmGrainBitDepth[c] - 1)),
                            ((1 << filmGrainBitDepth[c]) - 1) - current->film_grain_model_id * (1 << (filmGrainBitDepth[c] - 1)),
                            3, c, i, j);
                }
            }
        }
        flag(film_grain_characteristics_persistence_flag);
    }

    return 0;
}

static int FUNC(sei_display_orientation)
    (CodedBitstreamContext *ctx, RWContext *rw,
     H265RawSEIDisplayOrientation *current, SEIMessageState *sei)
{
    int err;

    HEADER("Display Orientation");

    flag(display_orientation_cancel_flag);
    if (!current->display_orientation_cancel_flag) {
        flag(hor_flip);
        flag(ver_flip);
        ub(16, anticlockwise_rotation);
        flag(display_orientation_persistence_flag);
    }

    return 0;
}

static int FUNC(sei_active_parameter_sets)
    (CodedBitstreamContext *ctx, RWContext *rw,
     H265RawSEIActiveParameterSets *current, SEIMessageState *sei)
{
    CodedBitstreamH265Context *h265 = ctx->priv_data;
    const H265RawVPS *vps;
    int err, i;

    HEADER("Active Parameter Sets");

    u(4, active_video_parameter_set_id, 0, HEVC_MAX_VPS_COUNT);
    vps = h265->vps[current->active_video_parameter_set_id];
    if (!vps) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "VPS id %d not available for active "
               "parameter sets.\n", current->active_video_parameter_set_id);
        return AVERROR_INVALIDDATA;
    }
    h265->active_vps = vps;

    flag(self_contained_cvs_flag);
    flag(no_parameter_set_update_flag);

    ue(num_sps_ids_minus1, 0, HEVC_MAX_SPS_COUNT - 1);
    for (i = 0; i <= current->num_sps_ids_minus1; i++)
        ues(active_seq_parameter_set_id[i], 0, HEVC_MAX_SPS_COUNT - 1, 1, i);

    for (i = vps->vps_base_layer_internal_flag;
         i <= FFMIN(62, vps->vps_max_layers_minus1); i++) {
        ues(layer_sps_idx[i], 0, current->num_sps_ids_minus1, 1, i);

        if (i == 0)
            h265->active_sps = h265->sps[current->active_seq_parameter_set_id[current->layer_sps_idx[0]]];
    }

    return 0;
}

static int FUNC(sei_decoded_picture_hash)
    (CodedBitstreamContext *ctx, RWContext *rw,
     H265RawSEIDecodedPictureHash *current, SEIMessageState *sei)
{
    CodedBitstreamH265Context *h265 = ctx->priv_data;
    const H265RawSPS *sps = h265->active_sps;
    int err, c, i;

    HEADER("Decoded Picture Hash");

    if (!sps) {
        av_log(ctx->log_ctx, AV_LOG_ERROR,
               "No active SPS for decoded picture hash.\n");
        return AVERROR_INVALIDDATA;
    }

    u(8, hash_type, 0, 2);

    for (c = 0; c < (sps->chroma_format_idc == 0 ? 1 : 3); c++) {
        if (current->hash_type == 0) {
            for (i = 0; i < 16; i++)
                us(8, picture_md5[c][i], 0x00, 0xff, 2, c, i);
        } else if (current->hash_type == 1) {
            us(16, picture_crc[c], 0x0000, 0xffff, 1, c);
        } else if (current->hash_type == 2) {
            us(32, picture_checksum[c], 0x00000000, 0xffffffff, 1, c);
        }
    }

    return 0;
}

static int FUNC(sei_time_code)
    (CodedBitstreamContext *ctx, RWContext *rw,
     H265RawSEITimeCode *current, SEIMessageState *sei)
{
    int err, i;

    HEADER("Time Code");

    u(2, num_clock_ts, 1, 3);

    for (i = 0; i < current->num_clock_ts; i++) {
        flags(clock_timestamp_flag[i],   1, i);

        if (current->clock_timestamp_flag[i]) {
            flags(units_field_based_flag[i], 1, i);
            us(5, counting_type[i], 0, 6,    1, i);
            flags(full_timestamp_flag[i],    1, i);
            flags(discontinuity_flag[i],     1, i);
            flags(cnt_dropped_flag[i],       1, i);

            ubs(9, n_frames[i], 1, i);

            if (current->full_timestamp_flag[i]) {
                us(6, seconds_value[i], 0, 59, 1, i);
                us(6, minutes_value[i], 0, 59, 1, i);
                us(5, hours_value[i],   0, 23, 1, i);
            } else {
                flags(seconds_flag[i], 1, i);
                if (current->seconds_flag[i]) {
                    us(6, seconds_value[i], 0, 59, 1, i);
                    flags(minutes_flag[i], 1, i);
                    if (current->minutes_flag[i]) {
                        us(6, minutes_value[i], 0, 59, 1, i);
                        flags(hours_flag[i], 1, i);
                        if (current->hours_flag[i])
                            us(5, hours_value[i], 0, 23, 1, i);
                    }
                }
            }

            ubs(5, time_offset_length[i], 1, i);
            if (current->time_offset_length[i] > 0)
                ibs(current->time_offset_length[i], time_offset_value[i], 1, i);
            else
                infer(time_offset_value[i], 0);
        }
    }

    return 0;
}

static int FUNC(sei_alpha_channel_info)
    (CodedBitstreamContext *ctx, RWContext *rw,
     H265RawSEIAlphaChannelInfo *current, SEIMessageState *sei)
{
    int err, length;

    HEADER("Alpha Channel Information");

    flag(alpha_channel_cancel_flag);
    if (!current->alpha_channel_cancel_flag) {
        ub(3, alpha_channel_use_idc);
        ub(3, alpha_channel_bit_depth_minus8);
        length = current->alpha_channel_bit_depth_minus8 + 9;
        ub(length, alpha_transparent_value);
        ub(length, alpha_opaque_value);
        flag(alpha_channel_incr_flag);
        flag(alpha_channel_clip_flag);
        if (current->alpha_channel_clip_flag)
            flag(alpha_channel_clip_type_flag);
    } else {
       infer(alpha_channel_use_idc,   2);
       infer(alpha_channel_incr_flag, 0);
       infer(alpha_channel_clip_flag, 0);
    }

    return 0;
}

static int FUNC(sei)(CodedBitstreamContext *ctx, RWContext *rw,
                     H265RawSEI *current, int prefix)
{
    int err;

    if (prefix)
        HEADER("Prefix Supplemental Enhancement Information");
    else
        HEADER("Suffix Supplemental Enhancement Information");

    CHECK(FUNC(nal_unit_header)(ctx, rw, &current->nal_unit_header,
                                prefix ? HEVC_NAL_SEI_PREFIX
                                       : HEVC_NAL_SEI_SUFFIX));

    CHECK(FUNC_SEI(message_list)(ctx, rw, &current->message_list, prefix));

    CHECK(FUNC(rbsp_trailing_bits)(ctx, rw));

    return 0;
}
