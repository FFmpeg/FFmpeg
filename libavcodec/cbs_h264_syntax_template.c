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
                                 H264RawNALUnitHeader *current,
                                 uint32_t valid_type_mask)
{
    int err;

    u(1, forbidden_zero_bit, 0, 0);
    u(2, nal_ref_idc,        0, 3);
    u(5, nal_unit_type,      0, 31);

    if (!(1 << current->nal_unit_type & valid_type_mask)) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid NAL unit type %d.\n",
               current->nal_unit_type);
        return AVERROR_INVALIDDATA;
    }

    if (current->nal_unit_type == 14 ||
        current->nal_unit_type == 20 ||
        current->nal_unit_type == 21) {
        if (current->nal_unit_type != 21)
            flag(svc_extension_flag);
        else
            flag(avc_3d_extension_flag);

        if (current->svc_extension_flag) {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "SVC not supported.\n");
            return AVERROR_PATCHWELCOME;

        } else if (current->avc_3d_extension_flag) {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "3DAVC not supported.\n");
            return AVERROR_PATCHWELCOME;

        } else {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "MVC not supported.\n");
            return AVERROR_PATCHWELCOME;
        }
    }

    return 0;
}

static int FUNC(scaling_list)(CodedBitstreamContext *ctx, RWContext *rw,
                              H264RawScalingList *current,
                              int size_of_scaling_list)
{
    int err, i, scale;

    scale = 8;
    for (i = 0; i < size_of_scaling_list; i++) {
        ses(delta_scale[i], -128, +127, 1, i);
        scale = (scale + current->delta_scale[i] + 256) % 256;
        if (scale == 0)
            break;
    }

    return 0;
}

static int FUNC(hrd_parameters)(CodedBitstreamContext *ctx, RWContext *rw,
                                H264RawHRD *current)
{
    int err, i;

    ue(cpb_cnt_minus1, 0, 31);
    u(4, bit_rate_scale, 0, 15);
    u(4, cpb_size_scale, 0, 15);

    for (i = 0; i <= current->cpb_cnt_minus1; i++) {
        ues(bit_rate_value_minus1[i], 0, UINT32_MAX - 1, 1, i);
        ues(cpb_size_value_minus1[i], 0, UINT32_MAX - 1, 1, i);
        flags(cbr_flag[i], 1, i);
    }

    u(5, initial_cpb_removal_delay_length_minus1, 0, 31);
    u(5, cpb_removal_delay_length_minus1,         0, 31);
    u(5, dpb_output_delay_length_minus1,          0, 31);
    u(5, time_offset_length,                      0, 31);

    return 0;
}

static int FUNC(vui_parameters)(CodedBitstreamContext *ctx, RWContext *rw,
                                H264RawVUI *current, H264RawSPS *sps)
{
    int err;

    flag(aspect_ratio_info_present_flag);
    if (current->aspect_ratio_info_present_flag) {
        u(8, aspect_ratio_idc, 0, 255);
        if (current->aspect_ratio_idc == 255) {
            u(16, sar_width,  0, 65535);
            u(16, sar_height, 0, 65535);
        }
    } else {
        infer(aspect_ratio_idc, 0);
    }

    flag(overscan_info_present_flag);
    if (current->overscan_info_present_flag)
        flag(overscan_appropriate_flag);

    flag(video_signal_type_present_flag);
    if (current->video_signal_type_present_flag) {
        u(3, video_format, 0, 7);
        flag(video_full_range_flag);
        flag(colour_description_present_flag);
        if (current->colour_description_present_flag) {
            u(8, colour_primaries,         0, 255);
            u(8, transfer_characteristics, 0, 255);
            u(8, matrix_coefficients,      0, 255);
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

    flag(timing_info_present_flag);
    if (current->timing_info_present_flag) {
        u(32, num_units_in_tick, 1, UINT32_MAX);
        u(32, time_scale,        1, UINT32_MAX);
        flag(fixed_frame_rate_flag);
    } else {
        infer(fixed_frame_rate_flag, 0);
    }

    flag(nal_hrd_parameters_present_flag);
    if (current->nal_hrd_parameters_present_flag)
        CHECK(FUNC(hrd_parameters)(ctx, rw, &current->nal_hrd_parameters));

    flag(vcl_hrd_parameters_present_flag);
    if (current->vcl_hrd_parameters_present_flag)
        CHECK(FUNC(hrd_parameters)(ctx, rw, &current->vcl_hrd_parameters));

    if (current->nal_hrd_parameters_present_flag ||
        current->vcl_hrd_parameters_present_flag)
        flag(low_delay_hrd_flag);
    else
        infer(low_delay_hrd_flag, 1 - current->fixed_frame_rate_flag);

    flag(pic_struct_present_flag);

    flag(bitstream_restriction_flag);
    if (current->bitstream_restriction_flag) {
        flag(motion_vectors_over_pic_boundaries_flag);
        ue(max_bytes_per_pic_denom, 0, 16);
        ue(max_bits_per_mb_denom,   0, 16);
        // The current version of the standard constrains this to be in
        // [0,15], but older versions allow 16.
        ue(log2_max_mv_length_horizontal, 0, 16);
        ue(log2_max_mv_length_vertical,   0, 16);
        ue(max_num_reorder_frames,  0, H264_MAX_DPB_FRAMES);
        ue(max_dec_frame_buffering, 0, H264_MAX_DPB_FRAMES);
    } else {
        infer(motion_vectors_over_pic_boundaries_flag, 1);
        infer(max_bytes_per_pic_denom, 2);
        infer(max_bits_per_mb_denom,   1);
        infer(log2_max_mv_length_horizontal, 15);
        infer(log2_max_mv_length_vertical,   15);

        if ((sps->profile_idc ==  44 || sps->profile_idc ==  86 ||
             sps->profile_idc == 100 || sps->profile_idc == 110 ||
             sps->profile_idc == 122 || sps->profile_idc == 244) &&
            sps->constraint_set3_flag) {
            infer(max_num_reorder_frames,  0);
            infer(max_dec_frame_buffering, 0);
        } else {
            infer(max_num_reorder_frames,  H264_MAX_DPB_FRAMES);
            infer(max_dec_frame_buffering, H264_MAX_DPB_FRAMES);
        }
    }

    return 0;
}

static int FUNC(vui_parameters_default)(CodedBitstreamContext *ctx,
                                        RWContext *rw, H264RawVUI *current,
                                        H264RawSPS *sps)
{
    infer(aspect_ratio_idc, 0);

    infer(video_format,             5);
    infer(video_full_range_flag,    0);
    infer(colour_primaries,         2);
    infer(transfer_characteristics, 2);
    infer(matrix_coefficients,      2);

    infer(chroma_sample_loc_type_top_field,    0);
    infer(chroma_sample_loc_type_bottom_field, 0);

    infer(fixed_frame_rate_flag, 0);
    infer(low_delay_hrd_flag,    1);

    infer(pic_struct_present_flag, 0);

    infer(motion_vectors_over_pic_boundaries_flag, 1);
    infer(max_bytes_per_pic_denom, 2);
    infer(max_bits_per_mb_denom,   1);
    infer(log2_max_mv_length_horizontal, 15);
    infer(log2_max_mv_length_vertical,   15);

    if ((sps->profile_idc ==  44 || sps->profile_idc ==  86 ||
         sps->profile_idc == 100 || sps->profile_idc == 110 ||
         sps->profile_idc == 122 || sps->profile_idc == 244) &&
        sps->constraint_set3_flag) {
        infer(max_num_reorder_frames,  0);
        infer(max_dec_frame_buffering, 0);
    } else {
        infer(max_num_reorder_frames,  H264_MAX_DPB_FRAMES);
        infer(max_dec_frame_buffering, H264_MAX_DPB_FRAMES);
    }

    return 0;
}

static int FUNC(sps)(CodedBitstreamContext *ctx, RWContext *rw,
                     H264RawSPS *current)
{
    int err, i;

    HEADER("Sequence Parameter Set");

    CHECK(FUNC(nal_unit_header)(ctx, rw, &current->nal_unit_header,
                                1 << H264_NAL_SPS));

    u(8, profile_idc, 0, 255);

    flag(constraint_set0_flag);
    flag(constraint_set1_flag);
    flag(constraint_set2_flag);
    flag(constraint_set3_flag);
    flag(constraint_set4_flag);
    flag(constraint_set5_flag);

    u(2, reserved_zero_2bits,  0, 0);

    u(8, level_idc, 0, 255);

    ue(seq_parameter_set_id, 0, 31);

    if (current->profile_idc == 100 || current->profile_idc == 110 ||
        current->profile_idc == 122 || current->profile_idc == 244 ||
        current->profile_idc ==  44 || current->profile_idc ==  83 ||
        current->profile_idc ==  86 || current->profile_idc == 118 ||
        current->profile_idc == 128 || current->profile_idc == 138) {
        ue(chroma_format_idc, 0, 3);

        if (current->chroma_format_idc == 3)
            flag(separate_colour_plane_flag);
        else
            infer(separate_colour_plane_flag, 0);

        ue(bit_depth_luma_minus8,   0, 6);
        ue(bit_depth_chroma_minus8, 0, 6);

        flag(qpprime_y_zero_transform_bypass_flag);

        flag(seq_scaling_matrix_present_flag);
        if (current->seq_scaling_matrix_present_flag) {
            for (i = 0; i < ((current->chroma_format_idc != 3) ? 8 : 12); i++) {
                flags(seq_scaling_list_present_flag[i], 1, i);
                if (current->seq_scaling_list_present_flag[i]) {
                    if (i < 6)
                        CHECK(FUNC(scaling_list)(ctx, rw,
                                                 &current->scaling_list_4x4[i],
                                                 16));
                    else
                        CHECK(FUNC(scaling_list)(ctx, rw,
                                                 &current->scaling_list_8x8[i - 6],
                                                 64));
                }
            }
        }
    } else {
        infer(chroma_format_idc, current->profile_idc == 183 ? 0 : 1);

        infer(separate_colour_plane_flag, 0);
        infer(bit_depth_luma_minus8,      0);
        infer(bit_depth_chroma_minus8,    0);
    }

    ue(log2_max_frame_num_minus4, 0, 12);
    ue(pic_order_cnt_type, 0, 2);

    if (current->pic_order_cnt_type == 0) {
        ue(log2_max_pic_order_cnt_lsb_minus4, 0, 12);
    } else if (current->pic_order_cnt_type == 1) {
        flag(delta_pic_order_always_zero_flag);
        se(offset_for_non_ref_pic,         INT32_MIN + 1, INT32_MAX);
        se(offset_for_top_to_bottom_field, INT32_MIN + 1, INT32_MAX);
        ue(num_ref_frames_in_pic_order_cnt_cycle, 0, 255);

        for (i = 0; i < current->num_ref_frames_in_pic_order_cnt_cycle; i++)
            ses(offset_for_ref_frame[i], INT32_MIN + 1, INT32_MAX, 1, i);
    }

    ue(max_num_ref_frames, 0, H264_MAX_DPB_FRAMES);
    flag(gaps_in_frame_num_allowed_flag);

    ue(pic_width_in_mbs_minus1,        0, H264_MAX_MB_WIDTH);
    ue(pic_height_in_map_units_minus1, 0, H264_MAX_MB_HEIGHT);

    flag(frame_mbs_only_flag);
    if (!current->frame_mbs_only_flag)
        flag(mb_adaptive_frame_field_flag);

    flag(direct_8x8_inference_flag);

    flag(frame_cropping_flag);
    if (current->frame_cropping_flag) {
        ue(frame_crop_left_offset,   0, H264_MAX_WIDTH);
        ue(frame_crop_right_offset,  0, H264_MAX_WIDTH);
        ue(frame_crop_top_offset,    0, H264_MAX_HEIGHT);
        ue(frame_crop_bottom_offset, 0, H264_MAX_HEIGHT);
    }

    flag(vui_parameters_present_flag);
    if (current->vui_parameters_present_flag)
        CHECK(FUNC(vui_parameters)(ctx, rw, &current->vui, current));
    else
        CHECK(FUNC(vui_parameters_default)(ctx, rw, &current->vui, current));

    CHECK(FUNC(rbsp_trailing_bits)(ctx, rw));

    return 0;
}

static int FUNC(sps_extension)(CodedBitstreamContext *ctx, RWContext *rw,
                               H264RawSPSExtension *current)
{
    int err;

    HEADER("Sequence Parameter Set Extension");

    CHECK(FUNC(nal_unit_header)(ctx, rw, &current->nal_unit_header,
                                1 << H264_NAL_SPS_EXT));

    ue(seq_parameter_set_id, 0, 31);

    ue(aux_format_idc, 0, 3);

    if (current->aux_format_idc != 0) {
        int bits;

        ue(bit_depth_aux_minus8, 0, 4);
        flag(alpha_incr_flag);

        bits = current->bit_depth_aux_minus8 + 9;
        u(bits, alpha_opaque_value,      0, MAX_UINT_BITS(bits));
        u(bits, alpha_transparent_value, 0, MAX_UINT_BITS(bits));
    }

    flag(additional_extension_flag);

    CHECK(FUNC(rbsp_trailing_bits)(ctx, rw));

    return 0;
}

static int FUNC(pps)(CodedBitstreamContext *ctx, RWContext *rw,
                     H264RawPPS *current)
{
    CodedBitstreamH264Context *h264 = ctx->priv_data;
    const H264RawSPS *sps;
    int err, i;

    HEADER("Picture Parameter Set");

    CHECK(FUNC(nal_unit_header)(ctx, rw, &current->nal_unit_header,
                                1 << H264_NAL_PPS));

    ue(pic_parameter_set_id, 0, 255);
    ue(seq_parameter_set_id, 0, 31);

    sps = h264->sps[current->seq_parameter_set_id];
    if (!sps) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "SPS id %d not available.\n",
               current->seq_parameter_set_id);
        return AVERROR_INVALIDDATA;
    }

    flag(entropy_coding_mode_flag);
    flag(bottom_field_pic_order_in_frame_present_flag);

    ue(num_slice_groups_minus1, 0, 7);
    if (current->num_slice_groups_minus1 > 0) {
        unsigned int pic_size;
        int iGroup;

        pic_size = (sps->pic_width_in_mbs_minus1 + 1) *
                   (sps->pic_height_in_map_units_minus1 + 1);

        ue(slice_group_map_type, 0, 6);

        if (current->slice_group_map_type == 0) {
            for (iGroup = 0; iGroup <= current->num_slice_groups_minus1; iGroup++)
                ues(run_length_minus1[iGroup], 0, pic_size - 1, 1, iGroup);

        } else if (current->slice_group_map_type == 2) {
            for (iGroup = 0; iGroup < current->num_slice_groups_minus1; iGroup++) {
                ues(top_left[iGroup],       0, pic_size - 1, 1, iGroup);
                ues(bottom_right[iGroup],
                    current->top_left[iGroup], pic_size - 1, 1, iGroup);
            }
        } else if (current->slice_group_map_type == 3 ||
                   current->slice_group_map_type == 4 ||
                   current->slice_group_map_type == 5) {
            flag(slice_group_change_direction_flag);
            ue(slice_group_change_rate_minus1, 0, pic_size - 1);
        } else if (current->slice_group_map_type == 6) {
            ue(pic_size_in_map_units_minus1, pic_size - 1, pic_size - 1);

            allocate(current->slice_group_id,
                     current->pic_size_in_map_units_minus1 + 1);
            for (i = 0; i <= current->pic_size_in_map_units_minus1; i++)
                us(av_log2(2 * current->num_slice_groups_minus1 + 1),
                   slice_group_id[i], 0, current->num_slice_groups_minus1, 1, i);
        }
    }

    ue(num_ref_idx_l0_default_active_minus1, 0, 31);
    ue(num_ref_idx_l1_default_active_minus1, 0, 31);

    flag(weighted_pred_flag);
    u(2, weighted_bipred_idc, 0, 2);

    se(pic_init_qp_minus26, -26 - 6 * sps->bit_depth_luma_minus8, +25);
    se(pic_init_qs_minus26, -26, +25);
    se(chroma_qp_index_offset, -12, +12);

    flag(deblocking_filter_control_present_flag);
    flag(constrained_intra_pred_flag);
    flag(redundant_pic_cnt_present_flag);

    if (more_rbsp_data(current->more_rbsp_data))
    {
        flag(transform_8x8_mode_flag);

        flag(pic_scaling_matrix_present_flag);
        if (current->pic_scaling_matrix_present_flag) {
            for (i = 0; i < 6 + (((sps->chroma_format_idc != 3) ? 2 : 6) *
                                 current->transform_8x8_mode_flag); i++) {
                flags(pic_scaling_list_present_flag[i], 1, i);
                if (current->pic_scaling_list_present_flag[i]) {
                    if (i < 6)
                        CHECK(FUNC(scaling_list)(ctx, rw,
                                                 &current->scaling_list_4x4[i],
                                                 16));
                    else
                        CHECK(FUNC(scaling_list)(ctx, rw,
                                                 &current->scaling_list_8x8[i - 6],
                                                 64));
                }
            }
        }

        se(second_chroma_qp_index_offset, -12, +12);
    } else {
        infer(transform_8x8_mode_flag, 0);
        infer(pic_scaling_matrix_present_flag, 0);
        infer(second_chroma_qp_index_offset, current->chroma_qp_index_offset);
    }

    CHECK(FUNC(rbsp_trailing_bits)(ctx, rw));

    return 0;
}

static int FUNC(sei_buffering_period)(CodedBitstreamContext *ctx, RWContext *rw,
                                      H264RawSEIBufferingPeriod *current)
{
    CodedBitstreamH264Context *h264 = ctx->priv_data;
    const H264RawSPS *sps;
    int err, i, length;

    HEADER("Buffering Period");

    ue(seq_parameter_set_id, 0, 31);

    sps = h264->sps[current->seq_parameter_set_id];
    if (!sps) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "SPS id %d not available.\n",
               current->seq_parameter_set_id);
        return AVERROR_INVALIDDATA;
    }
    h264->active_sps = sps;

    if (sps->vui.nal_hrd_parameters_present_flag) {
        for (i = 0; i <= sps->vui.nal_hrd_parameters.cpb_cnt_minus1; i++) {
            length = sps->vui.nal_hrd_parameters.initial_cpb_removal_delay_length_minus1 + 1;
            xu(length, initial_cpb_removal_delay[SchedSelIdx],
               current->nal.initial_cpb_removal_delay[i],
               1, MAX_UINT_BITS(length), 1, i);
            xu(length, initial_cpb_removal_delay_offset[SchedSelIdx],
               current->nal.initial_cpb_removal_delay_offset[i],
               0, MAX_UINT_BITS(length), 1, i);
        }
    }

    if (sps->vui.vcl_hrd_parameters_present_flag) {
        for (i = 0; i <= sps->vui.vcl_hrd_parameters.cpb_cnt_minus1; i++) {
            length = sps->vui.vcl_hrd_parameters.initial_cpb_removal_delay_length_minus1 + 1;
            xu(length, initial_cpb_removal_delay[SchedSelIdx],
               current->vcl.initial_cpb_removal_delay[i],
               1, MAX_UINT_BITS(length), 1, i);
            xu(length, initial_cpb_removal_delay_offset[SchedSelIdx],
               current->vcl.initial_cpb_removal_delay_offset[i],
               0, MAX_UINT_BITS(length), 1, i);
        }
    }

    return 0;
}

static int FUNC(sei_pic_timestamp)(CodedBitstreamContext *ctx, RWContext *rw,
                                   H264RawSEIPicTimestamp *current,
                                   const H264RawSPS *sps)
{
    uint8_t time_offset_length;
    int err;

    u(2, ct_type, 0, 2);
    flag(nuit_field_based_flag);
    u(5, counting_type, 0, 6);
    flag(full_timestamp_flag);
    flag(discontinuity_flag);
    flag(cnt_dropped_flag);
    u(8, n_frames, 0, 255);
    if (current->full_timestamp_flag) {
            u(6, seconds_value, 0, 59);
            u(6, minutes_value, 0, 59);
            u(5, hours_value,   0, 23);
    } else {
        flag(seconds_flag);
        if (current->seconds_flag) {
            u(6, seconds_value, 0, 59);
            flag(minutes_flag);
            if (current->minutes_flag) {
                u(6, minutes_value, 0, 59);
                flag(hours_flag);
                if (current->hours_flag)
                    u(5, hours_value, 0, 23);
            }
        }
    }

    if (sps->vui.nal_hrd_parameters_present_flag)
        time_offset_length = sps->vui.nal_hrd_parameters.time_offset_length;
    else if (sps->vui.vcl_hrd_parameters_present_flag)
        time_offset_length = sps->vui.vcl_hrd_parameters.time_offset_length;
    else
        time_offset_length = 24;

    if (time_offset_length > 0)
        u(time_offset_length, time_offset,
          0, MAX_UINT_BITS(time_offset_length));
    else
        infer(time_offset, 0);

    return 0;
}

static int FUNC(sei_pic_timing)(CodedBitstreamContext *ctx, RWContext *rw,
                                H264RawSEIPicTiming *current)
{
    CodedBitstreamH264Context *h264 = ctx->priv_data;
    const H264RawSPS *sps;
    int err;

    HEADER("Picture Timing");

    sps = h264->active_sps;
    if (!sps) {
        // If there is exactly one possible SPS but it is not yet active
        // then just assume that it should be the active one.
        int i, k = -1;
        for (i = 0; i < H264_MAX_SPS_COUNT; i++) {
            if (h264->sps[i]) {
                if (k >= 0) {
                    k = -1;
                    break;
                }
                k = i;
            }
        }
        if (k >= 0)
            sps = h264->sps[k];
    }
    if (!sps) {
        av_log(ctx->log_ctx, AV_LOG_ERROR,
               "No active SPS for pic_timing.\n");
        return AVERROR_INVALIDDATA;
    }

    if (sps->vui.nal_hrd_parameters_present_flag ||
        sps->vui.vcl_hrd_parameters_present_flag) {
        const H264RawHRD *hrd;

        if (sps->vui.nal_hrd_parameters_present_flag)
            hrd = &sps->vui.nal_hrd_parameters;
        else if (sps->vui.vcl_hrd_parameters_present_flag)
            hrd = &sps->vui.vcl_hrd_parameters;
        else {
            av_log(ctx->log_ctx, AV_LOG_ERROR,
                   "No HRD parameters for pic_timing.\n");
            return AVERROR_INVALIDDATA;
        }

        u(hrd->cpb_removal_delay_length_minus1 + 1, cpb_removal_delay,
          0, MAX_UINT_BITS(hrd->cpb_removal_delay_length_minus1 + 1));
        u(hrd->dpb_output_delay_length_minus1 + 1, dpb_output_delay,
          0, MAX_UINT_BITS(hrd->dpb_output_delay_length_minus1 + 1));
    }

    if (sps->vui.pic_struct_present_flag) {
        static const int num_clock_ts[9] = {
            1, 1, 1, 2, 2, 3, 3, 2, 3
        };
        int i;

        u(4, pic_struct, 0, 8);
        if (current->pic_struct > 8)
            return AVERROR_INVALIDDATA;

        for (i = 0; i < num_clock_ts[current->pic_struct]; i++) {
            flags(clock_timestamp_flag[i], 1, i);
            if (current->clock_timestamp_flag[i])
                CHECK(FUNC(sei_pic_timestamp)(ctx, rw,
                                              &current->timestamp[i], sps));
        }
    }

    return 0;
}

static int FUNC(sei_pan_scan_rect)(CodedBitstreamContext *ctx, RWContext *rw,
                                   H264RawSEIPanScanRect *current)
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

        ue(pan_scan_rect_repetition_period, 0, 16384);
    }

    return 0;
}

static int FUNC(sei_user_data_registered)(CodedBitstreamContext *ctx, RWContext *rw,
                                          H264RawSEIUserDataRegistered *current,
                                          uint32_t *payload_size)
{
    int err, i, j;

    HEADER("User Data Registered ITU-T T.35");

    u(8, itu_t_t35_country_code, 0x00, 0xff);
    if (current->itu_t_t35_country_code != 0xff)
        i = 1;
    else {
        u(8, itu_t_t35_country_code_extension_byte, 0x00, 0xff);
        i = 2;
    }

#ifdef READ
    if (*payload_size < i) {
        av_log(ctx->log_ctx, AV_LOG_ERROR,
               "Invalid SEI user data registered payload.\n");
        return AVERROR_INVALIDDATA;
    }
    current->data_length = *payload_size - i;
#else
    *payload_size = i + current->data_length;
#endif

    allocate(current->data, current->data_length);
    for (j = 0; j < current->data_length; j++)
        xu(8, itu_t_t35_payload_byte[i], current->data[j], 0x00, 0xff, 1, i + j);

    return 0;
}

static int FUNC(sei_user_data_unregistered)(CodedBitstreamContext *ctx, RWContext *rw,
                                            H264RawSEIUserDataUnregistered *current,
                                            uint32_t *payload_size)
{
    int err, i;

    HEADER("User Data Unregistered");

#ifdef READ
    if (*payload_size < 16) {
        av_log(ctx->log_ctx, AV_LOG_ERROR,
               "Invalid SEI user data unregistered payload.\n");
        return AVERROR_INVALIDDATA;
    }
    current->data_length = *payload_size - 16;
#else
    *payload_size = 16 + current->data_length;
#endif

    for (i = 0; i < 16; i++)
        us(8, uuid_iso_iec_11578[i], 0x00, 0xff, 1, i);

    allocate(current->data, current->data_length);

    for (i = 0; i < current->data_length; i++)
        xu(8, user_data_payload_byte[i], current->data[i], 0x00, 0xff, 1, i);

    return 0;
}

static int FUNC(sei_recovery_point)(CodedBitstreamContext *ctx, RWContext *rw,
                                    H264RawSEIRecoveryPoint *current)
{
    int err;

    HEADER("Recovery Point");

    ue(recovery_frame_cnt, 0, 65535);
    flag(exact_match_flag);
    flag(broken_link_flag);
    u(2, changing_slice_group_idc, 0, 2);

    return 0;
}

static int FUNC(sei_display_orientation)(CodedBitstreamContext *ctx, RWContext *rw,
                                         H264RawSEIDisplayOrientation *current)
{
    int err;

    HEADER("Display Orientation");

    flag(display_orientation_cancel_flag);
    if (!current->display_orientation_cancel_flag) {
        flag(hor_flip);
        flag(ver_flip);
        u(16, anticlockwise_rotation, 0, 65535);
        ue(display_orientation_repetition_period, 0, 16384);
        flag(display_orientation_extension_flag);
    }

    return 0;
}

static int FUNC(sei_mastering_display_colour_volume)(CodedBitstreamContext *ctx, RWContext *rw,
                                                     H264RawSEIMasteringDisplayColourVolume *current)
{
    int err, c;

    HEADER("Mastering Display Colour Volume");

    for (c = 0; c < 3; c++) {
        us(16, display_primaries_x[c], 0, 50000, 1, c);
        us(16, display_primaries_y[c], 0, 50000, 1, c);
    }

    u(16, white_point_x, 0, 50000);
    u(16, white_point_y, 0, 50000);

    u(32, max_display_mastering_luminance, 1, MAX_UINT_BITS(32));
    u(32, min_display_mastering_luminance, 0, current->max_display_mastering_luminance - 1);

    return 0;
}

static int FUNC(sei_payload)(CodedBitstreamContext *ctx, RWContext *rw,
                             H264RawSEIPayload *current)
{
    int err, i;
    int start_position, end_position;

#ifdef READ
    start_position = get_bits_count(rw);
#else
    start_position = put_bits_count(rw);
#endif

    switch (current->payload_type) {
    case H264_SEI_TYPE_BUFFERING_PERIOD:
        CHECK(FUNC(sei_buffering_period)
              (ctx, rw, &current->payload.buffering_period));
        break;
    case H264_SEI_TYPE_PIC_TIMING:
        CHECK(FUNC(sei_pic_timing)
              (ctx, rw, &current->payload.pic_timing));
        break;
    case H264_SEI_TYPE_PAN_SCAN_RECT:
        CHECK(FUNC(sei_pan_scan_rect)
              (ctx, rw, &current->payload.pan_scan_rect));
        break;
    case H264_SEI_TYPE_FILLER_PAYLOAD:
        {
            for (i = 0; i  < current->payload_size; i++)
                fixed(8, ff_byte, 0xff);
        }
        break;
    case H264_SEI_TYPE_USER_DATA_REGISTERED:
        CHECK(FUNC(sei_user_data_registered)
              (ctx, rw, &current->payload.user_data_registered, &current->payload_size));
        break;
    case H264_SEI_TYPE_USER_DATA_UNREGISTERED:
        CHECK(FUNC(sei_user_data_unregistered)
              (ctx, rw, &current->payload.user_data_unregistered, &current->payload_size));
        break;
    case H264_SEI_TYPE_RECOVERY_POINT:
        CHECK(FUNC(sei_recovery_point)
              (ctx, rw, &current->payload.recovery_point));
        break;
    case H264_SEI_TYPE_DISPLAY_ORIENTATION:
        CHECK(FUNC(sei_display_orientation)
              (ctx, rw, &current->payload.display_orientation));
        break;
    case H264_SEI_TYPE_MASTERING_DISPLAY_COLOUR_VOLUME:
        CHECK(FUNC(sei_mastering_display_colour_volume)
              (ctx, rw, &current->payload.mastering_display_colour_volume));
        break;
    default:
        {
#ifdef READ
            current->payload.other.data_length = current->payload_size;
#endif
            allocate(current->payload.other.data, current->payload.other.data_length);
            for (i = 0; i < current->payload.other.data_length; i++)
                xu(8, payload_byte[i], current->payload.other.data[i], 0, 255, 1, i);
        }
    }

    if (byte_alignment(rw)) {
        fixed(1, bit_equal_to_one, 1);
        while (byte_alignment(rw))
            fixed(1, bit_equal_to_zero, 0);
    }

#ifdef READ
    end_position = get_bits_count(rw);
    if (end_position < start_position + 8 * current->payload_size) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "Incorrect SEI payload length: "
               "header %"PRIu32" bits, actually %d bits.\n",
               8 * current->payload_size,
               end_position - start_position);
        return AVERROR_INVALIDDATA;
    }
#else
    end_position = put_bits_count(rw);
    current->payload_size = (end_position - start_position) / 8;
#endif

    return 0;
}

static int FUNC(sei)(CodedBitstreamContext *ctx, RWContext *rw,
                     H264RawSEI *current)
{
    int err, k;

    HEADER("Supplemental Enhancement Information");

    CHECK(FUNC(nal_unit_header)(ctx, rw, &current->nal_unit_header,
                                1 << H264_NAL_SEI));

#ifdef READ
    for (k = 0; k < H264_MAX_SEI_PAYLOADS; k++) {
        uint32_t payload_type = 0;
        uint32_t payload_size = 0;
        uint32_t tmp;

        while (show_bits(rw, 8) == 0xff) {
            fixed(8, ff_byte, 0xff);
            payload_type += 255;
        }
        xu(8, last_payload_type_byte, tmp, 0, 254, 0);
        payload_type += tmp;

        while (show_bits(rw, 8) == 0xff) {
            fixed(8, ff_byte, 0xff);
            payload_size += 255;
        }
        xu(8, last_payload_size_byte, tmp, 0, 254, 0);
        payload_size += tmp;

        current->payload[k].payload_type = payload_type;
        current->payload[k].payload_size = payload_size;

        CHECK(FUNC(sei_payload)(ctx, rw, &current->payload[k]));

        if (!cbs_h2645_read_more_rbsp_data(rw))
            break;
    }
    if (k >= H264_MAX_SEI_PAYLOADS) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "Too many payloads in "
               "SEI message: found %d.\n", k);
        return AVERROR_INVALIDDATA;
    }
    current->payload_count = k + 1;
#else
    for (k = 0; k < current->payload_count; k++) {
        PutBitContext start_state;
        uint32_t tmp;
        int need_size, i;

        // Somewhat clumsy: we write the payload twice when
        // we don't know the size in advance.  This will mess
        // with trace output, but is otherwise harmless.
        start_state = *rw;
        need_size = !current->payload[k].payload_size;
        for (i = 0; i < 1 + need_size; i++) {
            *rw = start_state;

            tmp = current->payload[k].payload_type;
            while (tmp >= 255) {
                fixed(8, ff_byte, 0xff);
                tmp -= 255;
            }
            xu(8, last_payload_type_byte, tmp, 0, 254, 0);

            tmp = current->payload[k].payload_size;
            while (tmp >= 255) {
                fixed(8, ff_byte, 0xff);
                tmp -= 255;
            }
            xu(8, last_payload_size_byte, tmp, 0, 254, 0);

            CHECK(FUNC(sei_payload)(ctx, rw, &current->payload[k]));
        }
    }
#endif

    CHECK(FUNC(rbsp_trailing_bits)(ctx, rw));

    return 0;
}

static int FUNC(aud)(CodedBitstreamContext *ctx, RWContext *rw,
                     H264RawAUD *current)
{
    int err;

    HEADER("Access Unit Delimiter");

    CHECK(FUNC(nal_unit_header)(ctx, rw, &current->nal_unit_header,
                                1 << H264_NAL_AUD));

    u(3, primary_pic_type, 0, 7);

    CHECK(FUNC(rbsp_trailing_bits)(ctx, rw));

    return 0;
}

static int FUNC(ref_pic_list_modification)(CodedBitstreamContext *ctx, RWContext *rw,
                                           H264RawSliceHeader *current)
{
    CodedBitstreamH264Context *h264 = ctx->priv_data;
    const H264RawSPS *sps = h264->active_sps;
    int err, i, mopn;

    if (current->slice_type % 5 != 2 &&
        current->slice_type % 5 != 4) {
        flag(ref_pic_list_modification_flag_l0);
        if (current->ref_pic_list_modification_flag_l0) {
            for (i = 0; i < H264_MAX_RPLM_COUNT; i++) {
                xue(modification_of_pic_nums_idc,
                    current->rplm_l0[i].modification_of_pic_nums_idc, 0, 3, 0);

                mopn = current->rplm_l0[i].modification_of_pic_nums_idc;
                if (mopn == 3)
                    break;

                if (mopn == 0 || mopn == 1)
                    xue(abs_diff_pic_num_minus1,
                        current->rplm_l0[i].abs_diff_pic_num_minus1,
                        0, (1 + current->field_pic_flag) *
                        (1 << (sps->log2_max_frame_num_minus4 + 4)), 0);
                else if (mopn == 2)
                    xue(long_term_pic_num,
                        current->rplm_l0[i].long_term_pic_num,
                        0, sps->max_num_ref_frames - 1, 0);
            }
        }
    }

    if (current->slice_type % 5 == 1) {
        flag(ref_pic_list_modification_flag_l1);
        if (current->ref_pic_list_modification_flag_l1) {
            for (i = 0; i < H264_MAX_RPLM_COUNT; i++) {
                xue(modification_of_pic_nums_idc,
                    current->rplm_l1[i].modification_of_pic_nums_idc, 0, 3, 0);

                mopn = current->rplm_l1[i].modification_of_pic_nums_idc;
                if (mopn == 3)
                    break;

                if (mopn == 0 || mopn == 1)
                    xue(abs_diff_pic_num_minus1,
                        current->rplm_l1[i].abs_diff_pic_num_minus1,
                        0, (1 + current->field_pic_flag) *
                        (1 << (sps->log2_max_frame_num_minus4 + 4)), 0);
                else if (mopn == 2)
                    xue(long_term_pic_num,
                        current->rplm_l1[i].long_term_pic_num,
                        0, sps->max_num_ref_frames - 1, 0);
            }
        }
    }

    return 0;
}

static int FUNC(pred_weight_table)(CodedBitstreamContext *ctx, RWContext *rw,
                                   H264RawSliceHeader *current)
{
    CodedBitstreamH264Context *h264 = ctx->priv_data;
    const H264RawSPS *sps = h264->active_sps;
    int chroma;
    int err, i, j;

    ue(luma_log2_weight_denom, 0, 7);

    chroma = !sps->separate_colour_plane_flag && sps->chroma_format_idc != 0;
    if (chroma)
        ue(chroma_log2_weight_denom, 0, 7);

    for (i = 0; i <= current->num_ref_idx_l0_active_minus1; i++) {
        flags(luma_weight_l0_flag[i], 1, i);
        if (current->luma_weight_l0_flag[i]) {
            ses(luma_weight_l0[i], -128, +127, 1, i);
            ses(luma_offset_l0[i], -128, +127, 1, i);
        }
        if (chroma) {
            flags(chroma_weight_l0_flag[i], 1, i);
            if (current->chroma_weight_l0_flag[i]) {
                for (j = 0; j < 2; j++) {
                    ses(chroma_weight_l0[i][j], -128, +127, 2, i, j);
                    ses(chroma_offset_l0[i][j], -128, +127, 2, i, j);
                }
            }
        }
    }

    if (current->slice_type % 5 == 1) {
        for (i = 0; i <= current->num_ref_idx_l1_active_minus1; i++) {
            flags(luma_weight_l1_flag[i], 1, i);
            if (current->luma_weight_l1_flag[i]) {
                ses(luma_weight_l1[i], -128, +127, 1, i);
                ses(luma_offset_l1[i], -128, +127, 1, i);
            }
            if (chroma) {
                flags(chroma_weight_l1_flag[i], 1, i);
                if (current->chroma_weight_l1_flag[i]) {
                    for (j = 0; j < 2; j++) {
                        ses(chroma_weight_l1[i][j], -128, +127, 2, i, j);
                        ses(chroma_offset_l1[i][j], -128, +127, 2, i, j);
                    }
                }
            }
        }
    }

    return 0;
}

static int FUNC(dec_ref_pic_marking)(CodedBitstreamContext *ctx, RWContext *rw,
                                     H264RawSliceHeader *current, int idr_pic_flag)
{
    CodedBitstreamH264Context *h264 = ctx->priv_data;
    const H264RawSPS *sps = h264->active_sps;
    int err, i;
    uint32_t mmco;

    if (idr_pic_flag) {
        flag(no_output_of_prior_pics_flag);
        flag(long_term_reference_flag);
    } else {
        flag(adaptive_ref_pic_marking_mode_flag);
        if (current->adaptive_ref_pic_marking_mode_flag) {
            for (i = 0; i < H264_MAX_MMCO_COUNT; i++) {
                xue(memory_management_control_operation,
                    current->mmco[i].memory_management_control_operation,
                    0, 6, 0);

                mmco = current->mmco[i].memory_management_control_operation;
                if (mmco == 0)
                    break;

                if (mmco == 1 || mmco == 3)
                    xue(difference_of_pic_nums_minus1,
                        current->mmco[i].difference_of_pic_nums_minus1,
                        0, INT32_MAX, 0);
                if (mmco == 2)
                    xue(long_term_pic_num,
                        current->mmco[i].long_term_pic_num,
                        0, sps->max_num_ref_frames - 1, 0);
                if (mmco == 3 || mmco == 6)
                    xue(long_term_frame_idx,
                        current->mmco[i].long_term_frame_idx,
                        0, sps->max_num_ref_frames - 1, 0);
                if (mmco == 4)
                    xue(max_long_term_frame_idx_plus1,
                        current->mmco[i].max_long_term_frame_idx_plus1,
                        0, sps->max_num_ref_frames, 0);
            }
            if (i == H264_MAX_MMCO_COUNT) {
                av_log(ctx->log_ctx, AV_LOG_ERROR, "Too many "
                       "memory management control operations.\n");
                return AVERROR_INVALIDDATA;
            }
        }
    }

    return 0;
}

static int FUNC(slice_header)(CodedBitstreamContext *ctx, RWContext *rw,
                              H264RawSliceHeader *current)
{
    CodedBitstreamH264Context *h264 = ctx->priv_data;
    const H264RawSPS *sps;
    const H264RawPPS *pps;
    int err;
    int idr_pic_flag;
    int slice_type_i, slice_type_p, slice_type_b;
    int slice_type_si, slice_type_sp;

    HEADER("Slice Header");

    CHECK(FUNC(nal_unit_header)(ctx, rw, &current->nal_unit_header,
                                1 << H264_NAL_SLICE     |
                                1 << H264_NAL_IDR_SLICE |
                                1 << H264_NAL_AUXILIARY_SLICE));

    if (current->nal_unit_header.nal_unit_type == H264_NAL_AUXILIARY_SLICE) {
        if (!h264->last_slice_nal_unit_type) {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "Auxiliary slice "
                   "is not decodable without the main picture "
                   "in the same access unit.\n");
            return AVERROR_INVALIDDATA;
        }
        idr_pic_flag = h264->last_slice_nal_unit_type == H264_NAL_IDR_SLICE;
    } else {
        idr_pic_flag = current->nal_unit_header.nal_unit_type == H264_NAL_IDR_SLICE;
    }

    ue(first_mb_in_slice, 0, H264_MAX_MB_PIC_SIZE - 1);
    ue(slice_type, 0, 9);

    slice_type_i  = current->slice_type % 5 == 2;
    slice_type_p  = current->slice_type % 5 == 0;
    slice_type_b  = current->slice_type % 5 == 1;
    slice_type_si = current->slice_type % 5 == 4;
    slice_type_sp = current->slice_type % 5 == 3;

    if (idr_pic_flag && !(slice_type_i || slice_type_si)) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid slice type %d "
               "for IDR picture.\n", current->slice_type);
        return AVERROR_INVALIDDATA;
    }

    ue(pic_parameter_set_id, 0, 255);

    pps = h264->pps[current->pic_parameter_set_id];
    if (!pps) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "PPS id %d not available.\n",
               current->pic_parameter_set_id);
        return AVERROR_INVALIDDATA;
    }
    h264->active_pps = pps;

    sps = h264->sps[pps->seq_parameter_set_id];
    if (!sps) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "SPS id %d not available.\n",
               pps->seq_parameter_set_id);
        return AVERROR_INVALIDDATA;
    }
    h264->active_sps = sps;

    if (sps->separate_colour_plane_flag)
        u(2, colour_plane_id, 0, 2);

    u(sps->log2_max_frame_num_minus4 + 4, frame_num,
      0, MAX_UINT_BITS(sps->log2_max_frame_num_minus4 + 4));

    if (!sps->frame_mbs_only_flag) {
        flag(field_pic_flag);
        if (current->field_pic_flag)
            flag(bottom_field_flag);
        else
            infer(bottom_field_flag, 0);
    } else {
        infer(field_pic_flag,    0);
        infer(bottom_field_flag, 0);
    }

    if (idr_pic_flag)
        ue(idr_pic_id, 0, 65535);

    if (sps->pic_order_cnt_type == 0) {
        u(sps->log2_max_pic_order_cnt_lsb_minus4 + 4, pic_order_cnt_lsb,
          0, MAX_UINT_BITS(sps->log2_max_pic_order_cnt_lsb_minus4 + 4));
        if (pps->bottom_field_pic_order_in_frame_present_flag &&
            !current->field_pic_flag)
            se(delta_pic_order_cnt_bottom, INT32_MIN + 1, INT32_MAX);

    } else if (sps->pic_order_cnt_type == 1) {
        if (!sps->delta_pic_order_always_zero_flag) {
            se(delta_pic_order_cnt[0], INT32_MIN + 1, INT32_MAX);
            if (pps->bottom_field_pic_order_in_frame_present_flag &&
                !current->field_pic_flag)
                se(delta_pic_order_cnt[1], INT32_MIN + 1, INT32_MAX);
            else
                infer(delta_pic_order_cnt[1], 0);
        } else {
            infer(delta_pic_order_cnt[0], 0);
            infer(delta_pic_order_cnt[1], 0);
        }
    }

    if (pps->redundant_pic_cnt_present_flag)
        ue(redundant_pic_cnt, 0, 127);
    else
        infer(redundant_pic_cnt, 0);

    if (current->nal_unit_header.nal_unit_type != H264_NAL_AUXILIARY_SLICE
        && !current->redundant_pic_cnt)
        h264->last_slice_nal_unit_type =
            current->nal_unit_header.nal_unit_type;

    if (slice_type_b)
        flag(direct_spatial_mv_pred_flag);

    if (slice_type_p || slice_type_sp || slice_type_b) {
        flag(num_ref_idx_active_override_flag);
        if (current->num_ref_idx_active_override_flag) {
            ue(num_ref_idx_l0_active_minus1, 0, 31);
            if (slice_type_b)
                ue(num_ref_idx_l1_active_minus1, 0, 31);
        } else {
            infer(num_ref_idx_l0_active_minus1,
                  pps->num_ref_idx_l0_default_active_minus1);
            infer(num_ref_idx_l1_active_minus1,
                  pps->num_ref_idx_l1_default_active_minus1);
        }
    }

    if (current->nal_unit_header.nal_unit_type == 20 ||
        current->nal_unit_header.nal_unit_type == 21) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "MVC / 3DAVC not supported.\n");
        return AVERROR_PATCHWELCOME;
    } else {
        CHECK(FUNC(ref_pic_list_modification)(ctx, rw, current));
    }

    if ((pps->weighted_pred_flag && (slice_type_p || slice_type_sp)) ||
        (pps->weighted_bipred_idc == 1 && slice_type_b)) {
        CHECK(FUNC(pred_weight_table)(ctx, rw, current));
    }

    if (current->nal_unit_header.nal_ref_idc != 0) {
        CHECK(FUNC(dec_ref_pic_marking)(ctx, rw, current, idr_pic_flag));
    }

    if (pps->entropy_coding_mode_flag &&
        !slice_type_i && !slice_type_si) {
        ue(cabac_init_idc, 0, 2);
    }

    se(slice_qp_delta, - 51 - 6 * sps->bit_depth_luma_minus8,
                       + 51 + 6 * sps->bit_depth_luma_minus8);
    if (slice_type_sp || slice_type_si) {
        if (slice_type_sp)
            flag(sp_for_switch_flag);
        se(slice_qs_delta, -51, +51);
    }

    if (pps->deblocking_filter_control_present_flag) {
        ue(disable_deblocking_filter_idc, 0, 2);
        if (current->disable_deblocking_filter_idc != 1) {
            se(slice_alpha_c0_offset_div2, -6, +6);
            se(slice_beta_offset_div2,     -6, +6);
        } else {
            infer(slice_alpha_c0_offset_div2, 0);
            infer(slice_beta_offset_div2,     0);
        }
    } else {
        infer(disable_deblocking_filter_idc, 0);
        infer(slice_alpha_c0_offset_div2,    0);
        infer(slice_beta_offset_div2,        0);
    }

    if (pps->num_slice_groups_minus1 > 0 &&
        pps->slice_group_map_type >= 3 &&
        pps->slice_group_map_type <= 5) {
        unsigned int pic_size, max, bits;

        pic_size = (sps->pic_width_in_mbs_minus1 + 1) *
                   (sps->pic_height_in_map_units_minus1 + 1);
        max = (pic_size + pps->slice_group_change_rate_minus1) /
              (pps->slice_group_change_rate_minus1 + 1);
        bits = av_log2(2 * max - 1);

        u(bits, slice_group_change_cycle, 0, max);
    }

    if (pps->entropy_coding_mode_flag) {
        while (byte_alignment(rw))
            fixed(1, cabac_alignment_one_bit, 1);
    }

    return 0;
}

static int FUNC(filler)(CodedBitstreamContext *ctx, RWContext *rw,
                        H264RawFiller *current)
{
    int err;

    HEADER("Filler Data");

    CHECK(FUNC(nal_unit_header)(ctx, rw, &current->nal_unit_header,
                                1 << H264_NAL_FILLER_DATA));

#ifdef READ
    while (show_bits(rw, 8) == 0xff) {
        fixed(8, ff_byte, 0xff);
        ++current->filler_size;
    }
#else
    {
        uint32_t i;
        for (i = 0; i < current->filler_size; i++)
            fixed(8, ff_byte, 0xff);
    }
#endif

    CHECK(FUNC(rbsp_trailing_bits)(ctx, rw));

    return 0;
}

static int FUNC(end_of_sequence)(CodedBitstreamContext *ctx, RWContext *rw,
                                 H264RawNALUnitHeader *current)
{
    HEADER("End of Sequence");

    return FUNC(nal_unit_header)(ctx, rw, current,
                                 1 << H264_NAL_END_SEQUENCE);
}

static int FUNC(end_of_stream)(CodedBitstreamContext *ctx, RWContext *rw,
                               H264RawNALUnitHeader *current)
{
    HEADER("End of Stream");

    return FUNC(nal_unit_header)(ctx, rw, current,
                                 1 << H264_NAL_END_STREAM);
}
