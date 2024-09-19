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

static int FUNC(rbsp_trailing_bits) (CodedBitstreamContext *ctx,
                                     RWContext *rw)
{
    int err;

    fixed(1, rbsp_stop_one_bit, 1);
    while (byte_alignment(rw) != 0)
        fixed(1, rbsp_alignment_zero_bit, 0);
    return 0;
}

static int FUNC(nal_unit_header) (CodedBitstreamContext *ctx, RWContext *rw,
                                  H266RawNALUnitHeader *current,
                                  int expected_nal_unit_type)
{
    int err;

    fixed(1, forbidden_zero_bit, 0);
    flag(nuh_reserved_zero_bit);

    u(6, nuh_layer_id, 0, 55);

    if (expected_nal_unit_type >= 0)
        u(5, nal_unit_type, expected_nal_unit_type, expected_nal_unit_type);
    else
        ub(5, nal_unit_type);

    u(3, nuh_temporal_id_plus1, 1, 7);
    return 0;
}

static int FUNC(byte_alignment) (CodedBitstreamContext *ctx, RWContext *rw)
{
    int err;

    fixed(1, byte_alignment_bit_equal_to_one, 1);
    while (byte_alignment(rw) != 0)
        fixed(1, byte_alignment_bit_equal_to_zero, 0);
    return 0;
}

static int FUNC(general_constraints_info) (CodedBitstreamContext *ctx,
                                           RWContext *rw,
                                           H266GeneralConstraintsInfo *current)
{
    int err, i, num_additional_bits_used;

    flag(gci_present_flag);
    if (current->gci_present_flag) {
        /* general */
        flag(gci_intra_only_constraint_flag);
        flag(gci_all_layers_independent_constraint_flag);
        flag(gci_one_au_only_constraint_flag);

        /* picture format */
        u(4, gci_sixteen_minus_max_bitdepth_constraint_idc, 0, 8);
        ub(2, gci_three_minus_max_chroma_format_constraint_idc);

        /* NAL unit type related */
        flag(gci_no_mixed_nalu_types_in_pic_constraint_flag);
        flag(gci_no_trail_constraint_flag);
        flag(gci_no_stsa_constraint_flag);
        flag(gci_no_rasl_constraint_flag);
        flag(gci_no_radl_constraint_flag);
        flag(gci_no_idr_constraint_flag);
        flag(gci_no_cra_constraint_flag);
        flag(gci_no_gdr_constraint_flag);
        flag(gci_no_aps_constraint_flag);
        flag(gci_no_idr_rpl_constraint_flag);

        /* tile, slice, subpicture partitioning */
        flag(gci_one_tile_per_pic_constraint_flag);
        flag(gci_pic_header_in_slice_header_constraint_flag);
        flag(gci_one_slice_per_pic_constraint_flag);
        flag(gci_no_rectangular_slice_constraint_flag);
        flag(gci_one_slice_per_subpic_constraint_flag);
        flag(gci_no_subpic_info_constraint_flag);

        /* CTU and block partitioning */
        ub(2, gci_three_minus_max_log2_ctu_size_constraint_idc);
        flag(gci_no_partition_constraints_override_constraint_flag);
        flag(gci_no_mtt_constraint_flag);
        flag(gci_no_qtbtt_dual_tree_intra_constraint_flag);

        /* intra */
        flag(gci_no_palette_constraint_flag);
        flag(gci_no_ibc_constraint_flag);
        flag(gci_no_isp_constraint_flag);
        flag(gci_no_mrl_constraint_flag);
        flag(gci_no_mip_constraint_flag);
        flag(gci_no_cclm_constraint_flag);

        /* inter */
        flag(gci_no_ref_pic_resampling_constraint_flag);
        flag(gci_no_res_change_in_clvs_constraint_flag);
        flag(gci_no_weighted_prediction_constraint_flag);
        flag(gci_no_ref_wraparound_constraint_flag);
        flag(gci_no_temporal_mvp_constraint_flag);
        flag(gci_no_sbtmvp_constraint_flag);
        flag(gci_no_amvr_constraint_flag);
        flag(gci_no_bdof_constraint_flag);
        flag(gci_no_smvd_constraint_flag);
        flag(gci_no_dmvr_constraint_flag);
        flag(gci_no_mmvd_constraint_flag);
        flag(gci_no_affine_motion_constraint_flag);
        flag(gci_no_prof_constraint_flag);
        flag(gci_no_bcw_constraint_flag);
        flag(gci_no_ciip_constraint_flag);
        flag(gci_no_gpm_constraint_flag);

        /* transform, quantization, residual */
        flag(gci_no_luma_transform_size_64_constraint_flag);
        flag(gci_no_transform_skip_constraint_flag);
        flag(gci_no_bdpcm_constraint_flag);
        flag(gci_no_mts_constraint_flag);
        flag(gci_no_lfnst_constraint_flag);
        flag(gci_no_joint_cbcr_constraint_flag);
        flag(gci_no_sbt_constraint_flag);
        flag(gci_no_act_constraint_flag);
        flag(gci_no_explicit_scaling_list_constraint_flag);
        flag(gci_no_dep_quant_constraint_flag);
        flag(gci_no_sign_data_hiding_constraint_flag);
        flag(gci_no_cu_qp_delta_constraint_flag);
        flag(gci_no_chroma_qp_offset_constraint_flag);

        /* loop filter */
        flag(gci_no_sao_constraint_flag);
        flag(gci_no_alf_constraint_flag);
        flag(gci_no_ccalf_constraint_flag);
        flag(gci_no_lmcs_constraint_flag);
        flag(gci_no_ladf_constraint_flag);
        flag(gci_no_virtual_boundaries_constraint_flag);
        ub(8, gci_num_additional_bits);
        if (current->gci_num_additional_bits > 5) {
            flag(gci_all_rap_pictures_constraint_flag);
            flag(gci_no_extended_precision_processing_constraint_flag);
            flag(gci_no_ts_residual_coding_rice_constraint_flag);
            flag(gci_no_rrc_rice_extension_constraint_flag);
            flag(gci_no_persistent_rice_adaptation_constraint_flag);
            flag(gci_no_reverse_last_sig_coeff_constraint_flag);
            num_additional_bits_used = 6;
        } else {
            infer(gci_all_rap_pictures_constraint_flag, 0);
            infer(gci_no_extended_precision_processing_constraint_flag, 0);
            infer(gci_no_ts_residual_coding_rice_constraint_flag, 0);
            infer(gci_no_rrc_rice_extension_constraint_flag, 0);
            infer(gci_no_persistent_rice_adaptation_constraint_flag, 0);
            infer(gci_no_reverse_last_sig_coeff_constraint_flag, 0);
            num_additional_bits_used = 0;
        }

        for (i = 0; i < current->gci_num_additional_bits - num_additional_bits_used; i++)
            flags(gci_reserved_bit[i], 1, i);
    }
    while (byte_alignment(rw) != 0)
        fixed(1, gci_alignment_zero_bit, 0);
    return 0;
}

static int FUNC(profile_tier_level) (CodedBitstreamContext *ctx,
                                     RWContext *rw,
                                     H266RawProfileTierLevel *current,
                                     int profile_tier_present_flag,
                                     int max_num_sub_layers_minus1)
{
    int err, i;

    if (profile_tier_present_flag) {
        ub(7, general_profile_idc);
        flag(general_tier_flag);
    }
    ub(8, general_level_idc);
    flag(ptl_frame_only_constraint_flag);
    flag(ptl_multilayer_enabled_flag);
    if (profile_tier_present_flag) {
        CHECK(FUNC(general_constraints_info) (ctx, rw,
                                              &current->
                                              general_constraints_info));
    }
    for (i = max_num_sub_layers_minus1 - 1; i >= 0; i--)
        flags(ptl_sublayer_level_present_flag[i], 1, i);
    while (byte_alignment(rw) != 0)
        flag(ptl_reserved_zero_bit);
    for (i = max_num_sub_layers_minus1 - 1; i >= 0; i--)
        if (current->ptl_sublayer_level_present_flag[i])
            ubs(8, sublayer_level_idc[i], 1, i);
    if (profile_tier_present_flag) {
        ub(8, ptl_num_sub_profiles);
        for (i = 0; i < current->ptl_num_sub_profiles; i++)
            ubs(32, general_sub_profile_idc[i], 1, i);
    }
    return 0;
}

static int FUNC(vui_parameters_default) (CodedBitstreamContext *ctx,
                                         RWContext *rw, H266RawVUI *current)
{
    //defined in D.8
    infer(vui_progressive_source_flag, 0);
    infer(vui_interlaced_source_flag, 0);

    infer(vui_non_packed_constraint_flag, 0);
    infer(vui_non_projected_constraint_flag, 0);

    infer(vui_aspect_ratio_constant_flag, 0);
    infer(vui_aspect_ratio_idc, 0);

    infer(vui_overscan_info_present_flag, 0);

    infer(vui_colour_primaries, 2);
    infer(vui_transfer_characteristics, 2);
    infer(vui_matrix_coeffs, 2);
    infer(vui_full_range_flag, 0);

    infer(vui_chroma_sample_loc_type_frame, 6);
    infer(vui_chroma_sample_loc_type_top_field, 6);
    infer(vui_chroma_sample_loc_type_bottom_field, 6);
    return 0;
}

static int FUNC(vui_parameters) (CodedBitstreamContext *ctx, RWContext *rw,
                                 H266RawVUI *current,
                                 uint8_t chroma_format_idc)
{
    int err;

    flag(vui_progressive_source_flag);
    flag(vui_interlaced_source_flag);
    flag(vui_non_packed_constraint_flag);
    flag(vui_non_projected_constraint_flag);
    flag(vui_aspect_ratio_info_present_flag);
    if (current->vui_aspect_ratio_info_present_flag) {
        flag(vui_aspect_ratio_constant_flag);
        ub(8, vui_aspect_ratio_idc);
        if (current->vui_aspect_ratio_idc == 255) {
            ub(16, vui_sar_width);
            ub(16, vui_sar_height);
        }
    } else {
        infer(vui_aspect_ratio_constant_flag, 0);
        infer(vui_aspect_ratio_idc, 0);
    }
    flag(vui_overscan_info_present_flag);
    if (current->vui_overscan_info_present_flag)
        flag(vui_overscan_appropriate_flag);
    flag(vui_colour_description_present_flag);
    if (current->vui_colour_description_present_flag) {
        ub(8, vui_colour_primaries);
        av_log(ctx->log_ctx, AV_LOG_DEBUG, "vui_colour_primaries == %d \n",
               current->vui_colour_primaries);
        ub(8, vui_transfer_characteristics);
        av_log(ctx->log_ctx, AV_LOG_DEBUG,
               "vui_transfer_characteristics == %d \n",
               current->vui_transfer_characteristics);
        ub(8, vui_matrix_coeffs);
        av_log(ctx->log_ctx, AV_LOG_DEBUG, "vui_matrix_coeffs == %d \n",
               current->vui_matrix_coeffs);
        flag(vui_full_range_flag);
    } else {
        infer(vui_colour_primaries, 2);
        infer(vui_transfer_characteristics, 2);
        infer(vui_matrix_coeffs, 2);
        infer(vui_full_range_flag, 0);
    }
    flag(vui_chroma_loc_info_present_flag);
    if (chroma_format_idc != 1 && current->vui_chroma_loc_info_present_flag) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "chroma_format_idc == %d,"
               "vui_chroma_loc_info_present_flag can't not be true",
               chroma_format_idc);
        return AVERROR_INVALIDDATA;
    }
    if (current->vui_chroma_loc_info_present_flag) {
        if (current->vui_progressive_source_flag &&
            !current->vui_interlaced_source_flag) {
            ue(vui_chroma_sample_loc_type_frame, 0, 6);
        } else {
            ue(vui_chroma_sample_loc_type_top_field, 0, 6);
            ue(vui_chroma_sample_loc_type_bottom_field, 0, 6);
        }
    } else {
        if (chroma_format_idc == 1) {
            infer(vui_chroma_sample_loc_type_frame, 6);
            infer(vui_chroma_sample_loc_type_top_field,
                  current->vui_chroma_sample_loc_type_frame);
            infer(vui_chroma_sample_loc_type_bottom_field,
                  current->vui_chroma_sample_loc_type_frame);
        }
    }
    return 0;
}

static int FUNC(payload_extension) (CodedBitstreamContext *ctx, RWContext *rw,
                                    H266RawExtensionData *current,
                                    uint32_t payload_size, int cur_pos)
{
    int err;
    size_t byte_length, k;

#ifdef READ
    GetBitContext tmp;
    int bits_left, payload_zero_bits;

    if (!cbs_h265_payload_extension_present(rw, payload_size, cur_pos))
        return 0;

    bits_left = 8 * payload_size - cur_pos;
    tmp = *rw;
    if (bits_left > 8)
        skip_bits_long(&tmp, bits_left - 8);
    payload_zero_bits = get_bits(&tmp, FFMIN(bits_left, 8));
    if (!payload_zero_bits)
        return AVERROR_INVALIDDATA;
    payload_zero_bits = ff_ctz(payload_zero_bits);
    current->bit_length = bits_left - payload_zero_bits - 1;
    allocate(current->data, (current->bit_length + 7) / 8);
#endif

    byte_length = (current->bit_length + 7) / 8;
    for (k = 0; k < byte_length; k++) {
        int length = FFMIN(current->bit_length - k * 8, 8);
        xu(length, reserved_payload_extension_data, current->data[k],
           0, MAX_UINT_BITS(length), 0);
    }

    return 0;
}

static int FUNC(vui_payload) (CodedBitstreamContext *ctx, RWContext *rw,
                              H266RawVUI *current, uint16_t vui_payload_size,
                              uint8_t chroma_format_idc)
{
    int err;
    int start_position, current_position;

    start_position = bit_position(rw);
    CHECK(FUNC(vui_parameters) (ctx, rw, current, chroma_format_idc));
    current_position = bit_position(rw) - start_position;

    if (current_position < 8 * vui_payload_size) {
        CHECK(FUNC(payload_extension) (ctx, rw, &current->extension_data,
                                       vui_payload_size, current_position));
        fixed(1, vui_payload_bit_equal_to_one, 1);
        while (byte_alignment(rw) != 0)
            fixed(1, vui_payload_bit_equal_to_zero, 0);
    }
    return 0;
}

static int FUNC(extension_data) (CodedBitstreamContext *ctx, RWContext *rw,
                                 H266RawExtensionData *current)
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

static int FUNC(dpb_parameters) (CodedBitstreamContext *ctx, RWContext *rw,
                                 H266DpbParameters *current,
                                 uint8_t max_sublayers_minus1,
                                 uint8_t sublayer_info_flag)
{
    int err, i;
    for (i = (sublayer_info_flag ? 0 : max_sublayers_minus1);
         i <= max_sublayers_minus1; i++) {
        ues(dpb_max_dec_pic_buffering_minus1[i], 0, VVC_MAX_DPB_SIZE - 1, 1, i);
        ues(dpb_max_num_reorder_pics[i],
            0, current->dpb_max_dec_pic_buffering_minus1[i], 1, i);
        ues(dpb_max_latency_increase_plus1[i], 0, UINT32_MAX - 1, 1, i);
    }
    return 0;
}

static int FUNC(ref_pic_list_struct) (CodedBitstreamContext *ctx,
                                      RWContext *rw,
                                      H266RefPicListStruct *current,
                                      uint8_t list_idx, uint8_t rpls_idx,
                                      const H266RawSPS *sps)
{
    CodedBitstreamH266Context *h266 = ctx->priv_data;
    int err, i, j, general_layer_idx = -1, num_direct_ref_layers = 0;
    const H266RawVPS *vps = h266->vps[sps->sps_video_parameter_set_id];

    if (!vps) {
        av_log(ctx->log_ctx, AV_LOG_ERROR,
               "VPS id %d not available.\n", sps->sps_video_parameter_set_id);
        return AVERROR_INVALIDDATA;
    }
    //7.4.3.3 (29)
    for (i = 0; i <= vps->vps_max_layers_minus1; i++) {
        if (sps->nal_unit_header.nuh_layer_id == vps->vps_layer_id[i]) {
            general_layer_idx = i;
            break;
        }
    }
    if (general_layer_idx < 0) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "vps_layer_id %d not available.\n",
               sps->nal_unit_header.nuh_layer_id);
        return AVERROR_INVALIDDATA;
    }
    //7.4.3.3 (28)
    for (j = 0; j <= vps->vps_max_layers_minus1; j++) {
        if (vps->vps_direct_ref_layer_flag[general_layer_idx][j])
            num_direct_ref_layers++;
    }

    ue(num_ref_entries, 0, VVC_MAX_REF_ENTRIES);
    if (sps->sps_long_term_ref_pics_flag &&
        rpls_idx < sps->sps_num_ref_pic_lists[list_idx] &&
        current->num_ref_entries > 0)
        flag(ltrp_in_header_flag);
    if (sps->sps_long_term_ref_pics_flag &&
        rpls_idx == sps->sps_num_ref_pic_lists[list_idx])
        infer(ltrp_in_header_flag, 1);
    for (i = 0, j = 0; i < current->num_ref_entries; i++) {
        if (sps->sps_inter_layer_prediction_enabled_flag)
            flags(inter_layer_ref_pic_flag[i], 1, i);
        else
            infer(inter_layer_ref_pic_flag[i], 0);

        if (!current->inter_layer_ref_pic_flag[i]) {
            if (sps->sps_long_term_ref_pics_flag)
                flags(st_ref_pic_flag[i], 1, i);
            else
                infer(st_ref_pic_flag[i], 1);
            if (current->st_ref_pic_flag[i]) {
                int abs_delta_poc_st;
                ues(abs_delta_poc_st[i], 0, MAX_UINT_BITS(15), 1, i);
                if ((sps->sps_weighted_pred_flag ||
                     sps->sps_weighted_bipred_flag) && i != 0)
                    abs_delta_poc_st = current->abs_delta_poc_st[i];
                else
                    abs_delta_poc_st = current->abs_delta_poc_st[i] + 1;
                if (abs_delta_poc_st > 0)
                    flags(strp_entry_sign_flag[i], 1, i);
            } else {
                if (!current->ltrp_in_header_flag) {
                    uint8_t bits = sps->sps_log2_max_pic_order_cnt_lsb_minus4 + 4;
                    ubs(bits, rpls_poc_lsb_lt[j], 1, j);
                    j++;
                }
            }
        } else {
            if (num_direct_ref_layers == 0) {
                av_log(ctx->log_ctx, AV_LOG_ERROR,
                       "num_direct_ref_layers needs > 0.\n");
                return AVERROR_INVALIDDATA;
            }
            ues(ilrp_idx[i], 0, num_direct_ref_layers - 1, 1, i);
        }
    }
    return 0;
}

static int FUNC(ref_pic_lists) (CodedBitstreamContext *ctx, RWContext *rw,
                                const H266RawSPS *sps, const H266RawPPS *pps,
                                H266RefPicLists *current) {
    const H266RefPicListStruct * ref_list;
    int err, i, j, num_ltrp_entries;
    for (i = 0; i < 2; i++) {
        if (sps->sps_num_ref_pic_lists[i] > 0 &&
            (i == 0 || (i == 1 && pps->pps_rpl1_idx_present_flag))) {
            flags(rpl_sps_flag[i], 1, i);
        } else {
            if (sps->sps_num_ref_pic_lists[i] == 0) {
                infer(rpl_sps_flag[i], 0);
            } else {
                if (!pps->pps_rpl1_idx_present_flag && i == 1)
                    infer(rpl_sps_flag[1], current->rpl_sps_flag[0]);
            }
        }
        if (current->rpl_sps_flag[i]) {
            if (sps->sps_num_ref_pic_lists[i] > 1 &&
                (i == 0 || (i == 1 && pps->pps_rpl1_idx_present_flag))) {
                uint8_t bits = av_ceil_log2(sps->sps_num_ref_pic_lists[i]);
                us(bits, rpl_idx[i], 0, sps->sps_num_ref_pic_lists[i] - 1, 1, i);
            } else if (sps->sps_num_ref_pic_lists[i] == 1) {
                infer(rpl_idx[i], 0);
            } else if (i == 1 && !pps->pps_rpl1_idx_present_flag) {
                infer(rpl_idx[1], current->rpl_idx[0]);
            } else {
                //how to handle this? or never happpend?
                av_log(ctx->log_ctx, AV_LOG_ERROR,
                       "can't infer the rpl_idx[i]\n");
                return AVERROR_PATCHWELCOME;
            }
            memcpy(&current->rpl_ref_list[i],
                   &sps->sps_ref_pic_list_struct[i][current->rpl_idx[i]],
                   sizeof(current->rpl_ref_list[i]));
        } else {
            CHECK(FUNC(ref_pic_list_struct) (ctx, rw, &current->rpl_ref_list[i],
                                             i, sps->sps_num_ref_pic_lists[i],
                                             sps));
        }
        ref_list = &current->rpl_ref_list[i];

        num_ltrp_entries = 0;
        for (int k = 0; k < ref_list->num_ref_entries; k++) {
            if (!ref_list->inter_layer_ref_pic_flag[k]) {
                if (!ref_list->st_ref_pic_flag[k]) {
                    num_ltrp_entries++;
                }
            }
        }

        for (j = 0; j < num_ltrp_entries; j++) {
            if (ref_list->ltrp_in_header_flag) {
                ubs(sps->sps_log2_max_pic_order_cnt_lsb_minus4 + 4,
                    poc_lsb_lt[i][j], 2, i, j);
            }
            flags(delta_poc_msb_cycle_present_flag[i][j], 2, i, j);
            if (current->delta_poc_msb_cycle_present_flag[i][j]) {
                uint32_t max =
                    1 << (32 - sps->sps_log2_max_pic_order_cnt_lsb_minus4 - 4);
                ues(delta_poc_msb_cycle_lt[i][j], 0, max, 2, i, j);
            }
        }
    }
    return 0;
}

static int FUNC(general_timing_hrd_parameters)(CodedBitstreamContext *ctx,
                                    RWContext *rw,
                                    H266RawGeneralTimingHrdParameters *current)
{
    int err;
    ub(32, num_units_in_tick);
    u(32, time_scale, 1, MAX_UINT_BITS(32));
    flag(general_nal_hrd_params_present_flag);
    flag(general_vcl_hrd_params_present_flag);

    if (current->general_nal_hrd_params_present_flag ||
        current->general_vcl_hrd_params_present_flag) {
        flag(general_same_pic_timing_in_all_ols_flag);
        flag(general_du_hrd_params_present_flag);
        if (current->general_du_hrd_params_present_flag)
            ub(8, tick_divisor_minus2);
        ub(4, bit_rate_scale);
        ub(4, cpb_size_scale);
        if (current->general_du_hrd_params_present_flag)
            ub(4, cpb_size_du_scale);
        ue(hrd_cpb_cnt_minus1, 0, 31);
    } else {
        //infer general_same_pic_timing_in_all_ols_flag?
        infer(general_du_hrd_params_present_flag, 0);
    }
    return 0;
}

static int FUNC(sublayer_hrd_parameters) (CodedBitstreamContext *ctx,
                              RWContext *rw,
                              H266RawSubLayerHRDParameters *current,
                              int sublayer_id,
                              const H266RawGeneralTimingHrdParameters *general)
{
    int err, i;
    for (i = 0; i <= general->hrd_cpb_cnt_minus1; i++) {
        ues(bit_rate_value_minus1[sublayer_id][i], 0, UINT32_MAX - 1, 2,
            sublayer_id, i);
        ues(cpb_size_value_minus1[sublayer_id][i], 0, UINT32_MAX - 1, 2,
            sublayer_id, i);
        if (general->general_du_hrd_params_present_flag) {
            ues(cpb_size_du_value_minus1[sublayer_id][i],
                0, UINT32_MAX - 1, 2, sublayer_id, i);
            ues(bit_rate_du_value_minus1[sublayer_id][i],
                0, UINT32_MAX - 1, 2, sublayer_id, i);
        }
        flags(cbr_flag[sublayer_id][i], 2, sublayer_id, i);
    }
    return 0;
}

static int FUNC(ols_timing_hrd_parameters) (CodedBitstreamContext *ctx,
                RWContext *rw, H266RawOlsTimingHrdParameters *current,
                uint8_t first_sublayer, uint8_t max_sublayers_minus1,
                const H266RawGeneralTimingHrdParameters *general)
{
    int err, i;
    for (i = first_sublayer; i <= max_sublayers_minus1; i++) {
        flags(fixed_pic_rate_general_flag[i], 1, i);
        if (!current->fixed_pic_rate_general_flag[i])
            flags(fixed_pic_rate_within_cvs_flag[i], 1, i);
        else
            infer(fixed_pic_rate_within_cvs_flag[i], 1);
        if (current->fixed_pic_rate_within_cvs_flag[i]) {
            ues(elemental_duration_in_tc_minus1[i], 0, 2047, 1, i);
            infer(low_delay_hrd_flag[i], 0);
        } else if ((general->general_nal_hrd_params_present_flag ||
                    general->general_vcl_hrd_params_present_flag) &&
                   general->hrd_cpb_cnt_minus1 == 0) {
            flags(low_delay_hrd_flag[i], 1, i);
        } else {
            infer(low_delay_hrd_flag[i], 0);
        }
        if (general->general_nal_hrd_params_present_flag)
            CHECK(FUNC(sublayer_hrd_parameters) (ctx, rw,
                                        &current->nal_sub_layer_hrd_parameters,
                                        i, general));
        if (general->general_vcl_hrd_params_present_flag)
            CHECK(FUNC(sublayer_hrd_parameters) (ctx, rw,
                                        &current->nal_sub_layer_hrd_parameters,
                                        i, general));
    }
    return 0;
}

static int FUNC(opi)(CodedBitstreamContext *ctx, RWContext *rw,
                     H266RawOPI *current)
{
    int err;

    HEADER("Operating point information");

    CHECK(FUNC(nal_unit_header)(ctx, rw,
                                &current->nal_unit_header, VVC_OPI_NUT));

    flag(opi_ols_info_present_flag);
    flag(opi_htid_info_present_flag);

    if(current->opi_ols_info_present_flag)
        ue(opi_ols_idx, 0, VVC_MAX_TOTAL_NUM_OLSS - 1);

    if(current->opi_htid_info_present_flag)
        ub(3, opi_htid_plus1);

    flag(opi_extension_flag);
    if (current->opi_extension_flag)
        CHECK(FUNC(extension_data) (ctx, rw, &current->extension_data));
    CHECK(FUNC(rbsp_trailing_bits) (ctx, rw));

    return 0;
}

static int FUNC(dci)(CodedBitstreamContext *ctx, RWContext *rw,
                     H266RawDCI *current)
{
    int err, i;

    HEADER("Decoding capability information");

    CHECK(FUNC(nal_unit_header)(ctx, rw,
                                &current->nal_unit_header, VVC_DCI_NUT));

    ub(4, dci_reserved_zero_4bits);
    ub(4, dci_num_ptls_minus1);
    for (i = 0; i <= current->dci_num_ptls_minus1; i++)
        CHECK(FUNC(profile_tier_level)(ctx, rw,
                                       current->dci_profile_tier_level + i, 1, 0));

    flag(dci_extension_flag);
    if (current->dci_extension_flag)
        CHECK(FUNC(extension_data)(ctx, rw, &current->extension_data));
    CHECK(FUNC(rbsp_trailing_bits)(ctx, rw));

    return 0;
}

static int FUNC(vps) (CodedBitstreamContext *ctx, RWContext *rw,
                     H266RawVPS *current)
{
    int err, i, j, k;
    uint16_t total_num_olss = 0;
    uint8_t ols_mode_idc = 0;
    uint16_t num_multi_layer_olss = 0;
    uint8_t layer_included_in_ols_flag[VVC_MAX_TOTAL_NUM_OLSS][VVC_MAX_LAYERS];
    uint8_t num_ref_layers[VVC_MAX_LAYERS];
    uint8_t reference_layer_idx[VVC_MAX_LAYERS][VVC_MAX_LAYERS];

    HEADER("Video Parameter Set");

    CHECK(FUNC(nal_unit_header) (ctx, rw,
                                 &current->nal_unit_header, VVC_VPS_NUT));

    u(4, vps_video_parameter_set_id, 1, VVC_MAX_VPS_COUNT - 1);
    ub(6, vps_max_layers_minus1);
    u(3, vps_max_sublayers_minus1, 0, 6);
    if (current->vps_max_layers_minus1 > 0
        && current->vps_max_sublayers_minus1 > 0)
        flag(vps_default_ptl_dpb_hrd_max_tid_flag);
    else
        infer(vps_default_ptl_dpb_hrd_max_tid_flag, 1);

    if (current->vps_max_layers_minus1 > 0)
        flag(vps_all_independent_layers_flag);
    else
        infer(vps_all_independent_layers_flag, 1);

    for (i = 0; i <= current->vps_max_layers_minus1; i++) {
        ubs(6, vps_layer_id[i], 1, i);
        if (i > 0 && current->vps_layer_id[i] <= current->vps_layer_id[i - 1]) {
            av_log(ctx->log_ctx, AV_LOG_ERROR,
                   "vps_layer_id[%d](%d) should > vps_layer_id[%d](%d).\n",
                   i, current->vps_layer_id[i], i - 1,
                   current->vps_layer_id[i - 1]);
            return AVERROR_INVALIDDATA;
        }
        if (i > 0 && !current->vps_all_independent_layers_flag) {
            flags(vps_independent_layer_flag[i], 1, i);
            if (!current->vps_independent_layer_flag[i]) {
                flags(vps_max_tid_ref_present_flag[i], 1, i);
                for (j = 0; j < i; j++) {
                    flags(vps_direct_ref_layer_flag[i][j], 2, i, j);
                    if (current->vps_max_tid_ref_present_flag[i] &&
                        current->vps_direct_ref_layer_flag[i][j]) {
                        ubs(3, vps_max_tid_il_ref_pics_plus1[i][j], 2, i, j);
                    } else {
                        infer(vps_max_tid_il_ref_pics_plus1[i][j],
                              current->vps_max_sublayers_minus1 + 1);
                    }
                }
            } else {
                for (j = 0; j < i; j++) {
                    infer(vps_direct_ref_layer_flag[i][j], 0);
                }
            }
        } else {
            infer(vps_independent_layer_flag[i], 1);
            for (j = 0; j < i; j++) {
                infer(vps_direct_ref_layer_flag[i][j], 0);
            }
        }
    }

    if (current->vps_max_layers_minus1 > 0) {
        if (current->vps_all_independent_layers_flag)
            flag(vps_each_layer_is_an_ols_flag);
        else
            infer(vps_each_layer_is_an_ols_flag, 0);
        if (!current->vps_each_layer_is_an_ols_flag) {
            if (!current->vps_all_independent_layers_flag)
                ub(2, vps_ols_mode_idc);
            else
                infer(vps_ols_mode_idc, 2);
            if (current->vps_ols_mode_idc == 2) {
                ub(8, vps_num_output_layer_sets_minus2);
                for (i = 1; i <= current->vps_num_output_layer_sets_minus2 + 1;
                     i++)
                    for (j = 0; j <= current->vps_max_layers_minus1; j++)
                        flags(vps_ols_output_layer_flag[i][j], 2, i, j);
            }
            ols_mode_idc = current->vps_ols_mode_idc;
        } else {
            ols_mode_idc = 4;
        }
        if (ols_mode_idc == 4 || ols_mode_idc == 0 || ols_mode_idc == 1)
            total_num_olss = current->vps_max_layers_minus1 + 1;
        else if (ols_mode_idc == 2)
            total_num_olss = current->vps_num_output_layer_sets_minus2 + 2;
        else
            av_log(ctx->log_ctx, AV_LOG_ERROR,
                   "ols_mode_idc == 3, patch welcome");
        u(8, vps_num_ptls_minus1, 0, total_num_olss - 1);
    } else {
        infer(vps_each_layer_is_an_ols_flag, 1);
        infer(vps_num_ptls_minus1, 0);
    }

    for (i = 0; i <= current->vps_num_ptls_minus1; i++) {
        if (i > 0)
            flags(vps_pt_present_flag[i], 1, i);
        else
            infer(vps_pt_present_flag[i], 1);

        if (!current->vps_default_ptl_dpb_hrd_max_tid_flag)
            us(3, vps_ptl_max_tid[i], 0, current->vps_max_sublayers_minus1, 1, i);
        else
            infer(vps_ptl_max_tid[i], current->vps_max_sublayers_minus1);
    }
    while (byte_alignment(rw) != 0)
        fixed(1, vps_ptl_alignment_zero_bit, 0);

    {
        //calc NumMultiLayerOlss
        int m;
        uint8_t dependency_flag[VVC_MAX_LAYERS][VVC_MAX_LAYERS];
        uint16_t num_output_layers_in_ols[VVC_MAX_TOTAL_NUM_OLSS];
        uint8_t num_sub_layers_in_layer_in_ols[VVC_MAX_TOTAL_NUM_OLSS][VVC_MAX_TOTAL_NUM_OLSS];
        uint8_t output_layer_idx[VVC_MAX_TOTAL_NUM_OLSS][VVC_MAX_LAYERS];

        //7.4.3.3 vps_direct_ref_layer_flag section
        for (i = 0; i <= current->vps_max_layers_minus1; i++) {
            for (j = 0; j <= current->vps_max_layers_minus1; j++) {
                dependency_flag[i][j] = current->vps_direct_ref_layer_flag[i][j];
                for (k = 0; k < i; k++) {
                    if (current->vps_direct_ref_layer_flag[i][k] &&
                        dependency_flag[k][j])
                        dependency_flag[i][j] = 1;
                }
            }
        }
        for (i = 0; i <= current->vps_max_layers_minus1; i++) {
            int r;
            for (j = 0, r = 0; j <= current->vps_max_layers_minus1; j++) {
                if (dependency_flag[i][j])
                    reference_layer_idx[i][r++] = j;
            }
            num_ref_layers[i] = r;
        }

        //7.4.3.3 vps_ols_output_layer_flag section
        num_output_layers_in_ols[0] = 1;
        num_sub_layers_in_layer_in_ols[0][0] =
            current->vps_ptl_max_tid[current->vps_ols_ptl_idx[0]] + 1;
        for (i = 1; i < total_num_olss; i++) {
            if (ols_mode_idc == 4 || ols_mode_idc == 0) {
                num_output_layers_in_ols[i] = 1;
                if (current->vps_each_layer_is_an_ols_flag) {
                    num_sub_layers_in_layer_in_ols[i][0] =
                        current->vps_ptl_max_tid[current->vps_ols_ptl_idx[i]] + 1;
                } else {
                    num_sub_layers_in_layer_in_ols[i][i] =
                        current->vps_ptl_max_tid[current->vps_ols_ptl_idx[i]] + 1;
                    for (k = i - 1; k >= 0; k--) {
                        num_sub_layers_in_layer_in_ols[i][k] = 0;
                        for (m = k + 1; m <= i; m++) {
                            uint8_t max_sublayer_needed =
                                FFMIN(num_sub_layers_in_layer_in_ols[i][m],
                                      current->vps_max_tid_il_ref_pics_plus1[m][k]);
                            if (current->vps_direct_ref_layer_flag[m][k] &&
                                num_sub_layers_in_layer_in_ols[i][k] < max_sublayer_needed)
                                num_sub_layers_in_layer_in_ols[i][k] = max_sublayer_needed;
                        }
                    }
                }
            } else if (current->vps_ols_mode_idc == 1) {
                num_output_layers_in_ols[i] = i + 1;
                for (j = 0; j < num_output_layers_in_ols[i]; j++) {
                    num_sub_layers_in_layer_in_ols[i][j] =
                        current->vps_ptl_max_tid[current->vps_ols_ptl_idx[i]] + 1;
                }
            } else if (current->vps_ols_mode_idc == 2) {
                uint8_t highest_included_layer = 0;
                for (j = 0; j <= current->vps_max_layers_minus1; j++) {
                    layer_included_in_ols_flag[i][j] = 0;
                    num_sub_layers_in_layer_in_ols[i][j] = 0;
                }
                for (k = 0, j = 0; k <= current->vps_max_layers_minus1; k++) {
                    if (current->vps_ols_output_layer_flag[i][k]) {
                        layer_included_in_ols_flag[i][k] = 1;
                        highest_included_layer = k;
                        output_layer_idx[i][j] = k;
                        num_sub_layers_in_layer_in_ols[i][k] =
                            current->vps_ptl_max_tid[current->
                                                     vps_ols_ptl_idx[i]] + 1;
                        j++;
                    }
                }
                num_output_layers_in_ols[i] = j;
                for (j = 0; j < num_output_layers_in_ols[i]; j++) {
                    int idx = output_layer_idx[i][j];
                    for (k = 0; k < num_ref_layers[idx]; k++) {
                        if (!layer_included_in_ols_flag[i][reference_layer_idx[idx][k]])
                            layer_included_in_ols_flag[i][reference_layer_idx[idx][k]] = 1;
                    }
                }
                for (k = highest_included_layer - 1; k >= 0; k--) {
                    if (layer_included_in_ols_flag[i][k] &&
                        !current->vps_ols_output_layer_flag[i][k]) {
                        for (m = k + 1; m <= highest_included_layer; m++) {
                            uint8_t max_sublayer_needed =
                                FFMIN(num_sub_layers_in_layer_in_ols[i][m],
                                      current->vps_max_tid_il_ref_pics_plus1[m][k]);
                            if (current->vps_direct_ref_layer_flag[m][k] &&
                                layer_included_in_ols_flag[i][m] &&
                                num_sub_layers_in_layer_in_ols[i][k] <
                                max_sublayer_needed)
                                num_sub_layers_in_layer_in_ols[i][k] =
                                    max_sublayer_needed;
                        }
                    }
                }
            }
            if (!num_output_layers_in_ols[i])
                return AVERROR_INVALIDDATA;
        }
        for (i = 1; i < total_num_olss; i++) {
            int num_layers_in_ols = 0;
            if (current->vps_each_layer_is_an_ols_flag) {
                num_layers_in_ols = 1;
            } else if (current->vps_ols_mode_idc == 0 ||
                       current->vps_ols_mode_idc == 1) {
                num_layers_in_ols = i + 1;
            } else if (current->vps_ols_mode_idc == 2) {
                for (k = 0, j = 0; k <= current->vps_max_layers_minus1; k++)
                    if (layer_included_in_ols_flag[i][k])
                        j++;
                num_layers_in_ols = j;
            }
            if (num_layers_in_ols > 1) {
                num_multi_layer_olss++;
            }
        }
        if (!current->vps_each_layer_is_an_ols_flag && num_multi_layer_olss == 0)
            return AVERROR_INVALIDDATA;
    }

    for (i = 0; i <= current->vps_num_ptls_minus1; i++) {
        CHECK(FUNC(profile_tier_level) (ctx, rw,
                                        current->vps_profile_tier_level + i,
                                        current->vps_pt_present_flag[i],
                                        current->vps_ptl_max_tid[i]));
    }
    for (i = 0; i < total_num_olss; i++) {
        if (current->vps_num_ptls_minus1 > 0 &&
            current->vps_num_ptls_minus1 + 1 != total_num_olss) {
            us(8, vps_ols_ptl_idx[i], 0, current->vps_num_ptls_minus1, 1, i);
        } else if (current->vps_num_ptls_minus1 == 0) {
            infer(vps_ols_ptl_idx[i], 0);
        } else {
            infer(vps_ols_ptl_idx[i], i);
        }
    }

    if (!current->vps_each_layer_is_an_ols_flag) {
        uint16_t vps_num_dpb_params;
        ue(vps_num_dpb_params_minus1, 0, num_multi_layer_olss - 1);
        if (current->vps_each_layer_is_an_ols_flag)
            vps_num_dpb_params = 0;
        else
            vps_num_dpb_params = current->vps_num_dpb_params_minus1 + 1;

        if (current->vps_max_sublayers_minus1 > 0)
            flag(vps_sublayer_dpb_params_present_flag);
        else
            infer(vps_sublayer_dpb_params_present_flag, 0);

        for (i = 0; i < vps_num_dpb_params; i++) {
            if (!current->vps_default_ptl_dpb_hrd_max_tid_flag)
                us(3, vps_dpb_max_tid[i], 0, current->vps_max_sublayers_minus1,
                   1, i);
            else
                infer(vps_dpb_max_tid[i], current->vps_max_sublayers_minus1);
            CHECK(FUNC(dpb_parameters) (ctx, rw, current->vps_dpb_params + i,
                                        current->vps_dpb_max_tid[i],
                                        current->
                                        vps_sublayer_dpb_params_present_flag));
        }
        for (i = 0; i < num_multi_layer_olss; i++) {
            ues(vps_ols_dpb_pic_width[i], 0, UINT16_MAX, 1, i);
            ues(vps_ols_dpb_pic_height[i], 0, UINT16_MAX, 1, i);
            ubs(2, vps_ols_dpb_chroma_format[i], 1, i);
            ues(vps_ols_dpb_bitdepth_minus8[i], 0, 8, 1, i);
            if (vps_num_dpb_params > 1
                && vps_num_dpb_params != num_multi_layer_olss)
                ues(vps_ols_dpb_params_idx[i], 0, vps_num_dpb_params - 1, 1, i);
            else if (vps_num_dpb_params == 1)
                infer(vps_ols_dpb_params_idx[i], 0);
            else
                infer(vps_ols_dpb_params_idx[i], i);
        }
        flag(vps_timing_hrd_params_present_flag);
        if (current->vps_timing_hrd_params_present_flag) {
            CHECK(FUNC(general_timing_hrd_parameters) (ctx, rw,
                                                       &current->
                                                       vps_general_timing_hrd_parameters));
            if (current->vps_max_sublayers_minus1 > 0)
                flag(vps_sublayer_cpb_params_present_flag);
            else
                infer(vps_sublayer_cpb_params_present_flag, 0);
            ue(vps_num_ols_timing_hrd_params_minus1, 0,
               num_multi_layer_olss - 1);
            for (i = 0; i <= current->vps_num_ols_timing_hrd_params_minus1; i++) {
                uint8_t first_sublayer;
                if (!current->vps_default_ptl_dpb_hrd_max_tid_flag)
                    us(3, vps_hrd_max_tid[i], 0,
                       current->vps_max_sublayers_minus1, 1, i);
                else
                    infer(vps_hrd_max_tid[i],
                          current->vps_max_sublayers_minus1);
                first_sublayer = current->vps_sublayer_cpb_params_present_flag ?
                                 0 : current->vps_hrd_max_tid[i];
                CHECK(FUNC(ols_timing_hrd_parameters)
                      (ctx, rw, &current->vps_ols_timing_hrd_parameters,
                       first_sublayer, current->vps_max_sublayers_minus1,
                       &current->vps_general_timing_hrd_parameters));

            }
            if (current->vps_num_ols_timing_hrd_params_minus1 > 0 &&
                current->vps_num_ols_timing_hrd_params_minus1 + 1 !=
                num_multi_layer_olss) {
                for (i = 0; i < num_multi_layer_olss; i++) {
                    ues(vps_ols_timing_hrd_idx[i], 0,
                        current->vps_num_ols_timing_hrd_params_minus1, 1, i);
                }
            } else if (current->vps_num_ols_timing_hrd_params_minus1 == 0) {
                for (i = 0; i < num_multi_layer_olss; i++)
                    infer(vps_ols_timing_hrd_idx[i], 0);
            } else {
                for (i = 0; i < num_multi_layer_olss; i++)
                    infer(vps_ols_timing_hrd_idx[i], i);
            }
        }
    }

    flag(vps_extension_flag);
    if (current->vps_extension_flag)
        CHECK(FUNC(extension_data) (ctx, rw, &current->extension_data));
    CHECK(FUNC(rbsp_trailing_bits) (ctx, rw));

    return 0;
}

static int FUNC(sps_range_extension)(CodedBitstreamContext *ctx, RWContext *rw,
                                     H266RawSPS *current)
{
    int err;

    flag(sps_extended_precision_flag);
    if (current->sps_transform_skip_enabled_flag)
        flag(sps_ts_residual_coding_rice_present_in_sh_flag);
    else
        infer(sps_ts_residual_coding_rice_present_in_sh_flag, 0);
    flag(sps_rrc_rice_extension_flag);
    flag(sps_persistent_rice_adaptation_enabled_flag);
    flag(sps_reverse_last_sig_coeff_enabled_flag);

    return 0;
}

static int FUNC(sps)(CodedBitstreamContext *ctx, RWContext *rw,
                     H266RawSPS *current)
{
    CodedBitstreamH266Context *h266 = ctx->priv_data;
    int err, i, j;
    unsigned int ctb_log2_size_y, min_cb_log2_size_y,
                 min_qt_log2_size_intra_y, min_qt_log2_size_inter_y,
                 ctb_size_y, max_num_merge_cand, tmp_width_val, tmp_height_val;
    uint8_t qp_bd_offset, sub_width_c, sub_height_c;

    static const uint8_t h266_sub_width_c[] = {
        1, 2, 2, 1
    };
    static const uint8_t h266_sub_height_c[] = {
        1, 2, 1, 1
    };

    HEADER("Sequence Parameter Set");

    CHECK(FUNC(nal_unit_header) (ctx, rw,
                                 &current->nal_unit_header, VVC_SPS_NUT));

    ub(4, sps_seq_parameter_set_id);
    ub(4, sps_video_parameter_set_id);
    if (current->sps_video_parameter_set_id == 0 && !h266->vps[0]) {
        H266RawVPS *vps = ff_refstruct_allocz(sizeof(*vps));
        if (!vps)
            return AVERROR(ENOMEM);
        vps->vps_max_layers_minus1 = 0;
        vps->vps_independent_layer_flag[0] = 1;
        vps->vps_layer_id[0] = current->nal_unit_header.nuh_layer_id;
        h266->vps[0] = vps;
    }

    u(3, sps_max_sublayers_minus1, 0, VVC_MAX_SUBLAYERS - 1);
    u(2, sps_chroma_format_idc, 0, 3);
    sub_width_c = h266_sub_width_c[current->sps_chroma_format_idc];
    sub_height_c = h266_sub_height_c[current->sps_chroma_format_idc];

    u(2, sps_log2_ctu_size_minus5, 0, 3);
    ctb_log2_size_y = current->sps_log2_ctu_size_minus5 + 5;
    ctb_size_y = 1 << ctb_log2_size_y;

    flag(sps_ptl_dpb_hrd_params_present_flag);
    if (current->sps_ptl_dpb_hrd_params_present_flag) {
        CHECK(FUNC(profile_tier_level) (ctx, rw, &current->profile_tier_level,
                                        1, current->sps_max_sublayers_minus1));
    }
    flag(sps_gdr_enabled_flag);
    flag(sps_ref_pic_resampling_enabled_flag);
    if (current->sps_ref_pic_resampling_enabled_flag)
        flag(sps_res_change_in_clvs_allowed_flag);
    else
        infer(sps_res_change_in_clvs_allowed_flag, 0);

    ue(sps_pic_width_max_in_luma_samples, 1, VVC_MAX_WIDTH);
    ue(sps_pic_height_max_in_luma_samples, 1, VVC_MAX_HEIGHT);

    flag(sps_conformance_window_flag);
    if (current->sps_conformance_window_flag) {
        uint16_t width = current->sps_pic_width_max_in_luma_samples / sub_width_c;
        uint16_t height = current->sps_pic_height_max_in_luma_samples / sub_height_c;
        ue(sps_conf_win_left_offset, 0, width);
        ue(sps_conf_win_right_offset, 0, width - current->sps_conf_win_left_offset);
        ue(sps_conf_win_top_offset, 0, height);
        ue(sps_conf_win_bottom_offset, 0, height - current->sps_conf_win_top_offset);
    } else {
        infer(sps_conf_win_left_offset, 0);
        infer(sps_conf_win_right_offset, 0);
        infer(sps_conf_win_top_offset, 0);
        infer(sps_conf_win_bottom_offset, 0);
    }

    tmp_width_val = AV_CEIL_RSHIFT(current->sps_pic_width_max_in_luma_samples,
                    ctb_log2_size_y);
    tmp_height_val = AV_CEIL_RSHIFT(current->sps_pic_height_max_in_luma_samples,
                    ctb_log2_size_y);

    flag(sps_subpic_info_present_flag);
    if (current->sps_subpic_info_present_flag) {
        ue(sps_num_subpics_minus1, 0, VVC_MAX_SLICES - 1);
        if (current->sps_num_subpics_minus1 > 0) {
            flag(sps_independent_subpics_flag);
            flag(sps_subpic_same_size_flag);
        }

        if (current->sps_num_subpics_minus1 > 0) {
            int wlen = av_ceil_log2(tmp_width_val);
            int hlen = av_ceil_log2(tmp_height_val);
            infer(sps_subpic_ctu_top_left_x[0], 0);
            infer(sps_subpic_ctu_top_left_y[0], 0);
            if (current->sps_pic_width_max_in_luma_samples > ctb_size_y)
                ubs(wlen, sps_subpic_width_minus1[0], 1, 0);
            else
                infer(sps_subpic_width_minus1[0], tmp_width_val - 1);
            if (current->sps_pic_height_max_in_luma_samples > ctb_size_y)
                ubs(hlen, sps_subpic_height_minus1[0], 1, 0);
            else
                infer(sps_subpic_height_minus1[0], tmp_height_val - 1);
            if (!current->sps_independent_subpics_flag) {
                flags(sps_subpic_treated_as_pic_flag[0], 1, 0);
                flags(sps_loop_filter_across_subpic_enabled_flag[0], 1, 0);
            } else {
                infer(sps_subpic_treated_as_pic_flag[0], 1);
                infer(sps_loop_filter_across_subpic_enabled_flag[0], 1);
            }
            for (i = 1; i <= current->sps_num_subpics_minus1; i++) {
                if (!current->sps_subpic_same_size_flag) {
                    if (current->sps_pic_width_max_in_luma_samples > ctb_size_y) {
                        const unsigned int win_right_edge =
                            current->sps_pic_width_max_in_luma_samples
                          - current->sps_conf_win_right_offset * sub_width_c;
                        us(wlen, sps_subpic_ctu_top_left_x[i], 0,
                           AV_CEIL_RSHIFT(win_right_edge, ctb_log2_size_y) - 1,
                           1, i);
                    } else
                        infer(sps_subpic_ctu_top_left_x[i], 0);
                    if (current->sps_pic_height_max_in_luma_samples >
                        ctb_size_y) {
                        const unsigned int win_bottom_edge =
                            current->sps_pic_height_max_in_luma_samples
                          - current->sps_conf_win_bottom_offset * sub_height_c;
                        us(hlen, sps_subpic_ctu_top_left_y[i], 0,
                           AV_CEIL_RSHIFT(win_bottom_edge, ctb_log2_size_y) - 1,
                           1, i);
                    } else
                        infer(sps_subpic_ctu_top_left_y[i], 0);
                    if (i < current->sps_num_subpics_minus1 &&
                        current->sps_pic_width_max_in_luma_samples >
                        ctb_size_y) {
                        const unsigned int win_left_edge =
                            current->sps_conf_win_left_offset * sub_width_c;
                        const unsigned int win_left_edge_ctus =
                            AV_CEIL_RSHIFT(win_left_edge, ctb_log2_size_y);
                        us(wlen, sps_subpic_width_minus1[i],
                           win_left_edge_ctus > current->sps_subpic_ctu_top_left_x[i]
                               ? win_left_edge_ctus - current->sps_subpic_ctu_top_left_x[i]
                               : 0,
                           MAX_UINT_BITS(wlen), 1, i);
                    } else {
                        infer(sps_subpic_width_minus1[i],
                              tmp_width_val -
                              current->sps_subpic_ctu_top_left_x[i] - 1);
                    }
                    if (i < current->sps_num_subpics_minus1 &&
                        current->sps_pic_height_max_in_luma_samples >
                        ctb_size_y) {
                        const unsigned int win_top_edge =
                            current->sps_conf_win_top_offset * sub_height_c;
                        const unsigned int win_top_edge_ctus =
                            AV_CEIL_RSHIFT(win_top_edge, ctb_log2_size_y);
                        us(hlen, sps_subpic_height_minus1[i],
                           win_top_edge_ctus > current->sps_subpic_ctu_top_left_y[i]
                               ? win_top_edge_ctus - current->sps_subpic_ctu_top_left_y[i]
                               : 0,
                           MAX_UINT_BITS(hlen), 1, i);
                    } else {
                        infer(sps_subpic_height_minus1[i],
                              tmp_height_val -
                              current->sps_subpic_ctu_top_left_y[i] - 1);
                    }
                } else {
                    int num_subpic_cols = tmp_width_val /
                                     (current->sps_subpic_width_minus1[0] + 1);
                    if (tmp_width_val % (current->sps_subpic_width_minus1[0] + 1) ||
                        tmp_height_val % (current->sps_subpic_width_minus1[0] + 1) ||
                        current->sps_num_subpics_minus1 !=
                        (num_subpic_cols * tmp_height_val /
                         (current->sps_subpic_height_minus1[0] + 1) - 1))
                        return AVERROR_INVALIDDATA;
                    infer(sps_subpic_ctu_top_left_x[i],
                          (i % num_subpic_cols) *
                          (current->sps_subpic_width_minus1[0] + 1));
                    infer(sps_subpic_ctu_top_left_y[i],
                          (i / num_subpic_cols) *
                          (current->sps_subpic_height_minus1[0] + 1));
                    infer(sps_subpic_width_minus1[i],
                          current->sps_subpic_width_minus1[0]);
                    infer(sps_subpic_height_minus1[i],
                          current->sps_subpic_height_minus1[0]);
                }
                if (!current->sps_independent_subpics_flag) {
                    flags(sps_subpic_treated_as_pic_flag[i], 1, i);
                    flags(sps_loop_filter_across_subpic_enabled_flag[i], 1, i);
                } else {
                    infer(sps_subpic_treated_as_pic_flag[i], 1);
                    infer(sps_loop_filter_across_subpic_enabled_flag[i], 0);
                }
            }
        } else {
            infer(sps_subpic_ctu_top_left_x[0], 0);
            infer(sps_subpic_ctu_top_left_y[0], 0);
            infer(sps_subpic_width_minus1[0], tmp_width_val - 1);
            infer(sps_subpic_height_minus1[0], tmp_height_val - 1);
        }
        ue(sps_subpic_id_len_minus1, 0, 15);
        if ((1 << (current->sps_subpic_id_len_minus1 + 1)) <
            current->sps_num_subpics_minus1 + 1) {
            av_log(ctx->log_ctx, AV_LOG_ERROR,
                   "sps_subpic_id_len_minus1(%d) is too small\n",
                   current->sps_subpic_id_len_minus1);
            return AVERROR_INVALIDDATA;
        }
        flag(sps_subpic_id_mapping_explicitly_signalled_flag);
        if (current->sps_subpic_id_mapping_explicitly_signalled_flag) {
            flag(sps_subpic_id_mapping_present_flag);
            if (current->sps_subpic_id_mapping_present_flag) {
                for (i = 0; i <= current->sps_num_subpics_minus1; i++) {
                    ubs(current->sps_subpic_id_len_minus1 + 1,
                        sps_subpic_id[i], 1, i);
                }
            }
        }
    } else {
        infer(sps_num_subpics_minus1, 0);
        infer(sps_independent_subpics_flag, 1);
        infer(sps_subpic_same_size_flag, 0);
        infer(sps_subpic_id_mapping_explicitly_signalled_flag, 0);
        infer(sps_subpic_ctu_top_left_x[0], 0);
        infer(sps_subpic_ctu_top_left_y[0], 0);
        infer(sps_subpic_width_minus1[0], tmp_width_val - 1);
        infer(sps_subpic_height_minus1[0], tmp_height_val - 1);
    }


    ue(sps_bitdepth_minus8, 0, 8);
    qp_bd_offset = 6 * current->sps_bitdepth_minus8;

    flag(sps_entropy_coding_sync_enabled_flag);
    flag(sps_entry_point_offsets_present_flag);

    u(4, sps_log2_max_pic_order_cnt_lsb_minus4, 0, 12);
    flag(sps_poc_msb_cycle_flag);
    if (current->sps_poc_msb_cycle_flag)
        ue(sps_poc_msb_cycle_len_minus1,
           0, 32 - current->sps_log2_max_pic_order_cnt_lsb_minus4 - 5);

    u(2, sps_num_extra_ph_bytes, 0, 2);
    for (i = 0; i < (current->sps_num_extra_ph_bytes * 8); i++) {
        flags(sps_extra_ph_bit_present_flag[i], 1, i);
    }

    u(2, sps_num_extra_sh_bytes, 0, 2);
    for (i = 0; i < (current->sps_num_extra_sh_bytes * 8); i++) {
        flags(sps_extra_sh_bit_present_flag[i], 1, i);
    }

    if (current->sps_ptl_dpb_hrd_params_present_flag) {
        if (current->sps_max_sublayers_minus1 > 0)
            flag(sps_sublayer_dpb_params_flag);
        else
            infer(sps_sublayer_dpb_params_flag, 0);
        CHECK(FUNC(dpb_parameters) (ctx, rw, &current->sps_dpb_params,
                                    current->sps_max_sublayers_minus1,
                                    current->sps_sublayer_dpb_params_flag));
    }

    ue(sps_log2_min_luma_coding_block_size_minus2,
       0, FFMIN(4, current->sps_log2_ctu_size_minus5 + 3));
    min_cb_log2_size_y =
        current->sps_log2_min_luma_coding_block_size_minus2 + 2;

    flag(sps_partition_constraints_override_enabled_flag);

    ue(sps_log2_diff_min_qt_min_cb_intra_slice_luma,
       0, FFMIN(6, ctb_log2_size_y) - min_cb_log2_size_y);
    min_qt_log2_size_intra_y =
        current->sps_log2_diff_min_qt_min_cb_intra_slice_luma +
        min_cb_log2_size_y;

    ue(sps_max_mtt_hierarchy_depth_intra_slice_luma,
       0, 2 * (ctb_log2_size_y - min_cb_log2_size_y));

    if (current->sps_max_mtt_hierarchy_depth_intra_slice_luma != 0) {
        ue(sps_log2_diff_max_bt_min_qt_intra_slice_luma,
           0, ctb_log2_size_y - min_qt_log2_size_intra_y);
        ue(sps_log2_diff_max_tt_min_qt_intra_slice_luma,
           0, FFMIN(6, ctb_log2_size_y) - min_qt_log2_size_intra_y);
    } else {
        infer(sps_log2_diff_max_bt_min_qt_intra_slice_luma, 0);
        infer(sps_log2_diff_max_tt_min_qt_intra_slice_luma, 0);
    }

    if (current->sps_chroma_format_idc != 0) {
        flag(sps_qtbtt_dual_tree_intra_flag);
    } else {
        infer(sps_qtbtt_dual_tree_intra_flag, 0);
    }

    if (current->sps_qtbtt_dual_tree_intra_flag) {
        ue(sps_log2_diff_min_qt_min_cb_intra_slice_chroma,
           0, FFMIN(6, ctb_log2_size_y) - min_cb_log2_size_y);
        ue(sps_max_mtt_hierarchy_depth_intra_slice_chroma,
           0, 2 * (ctb_log2_size_y - min_cb_log2_size_y));
        if (current->sps_max_mtt_hierarchy_depth_intra_slice_chroma != 0) {
            unsigned int min_qt_log2_size_intra_c =
                current->sps_log2_diff_min_qt_min_cb_intra_slice_chroma +
                min_cb_log2_size_y;
            ue(sps_log2_diff_max_bt_min_qt_intra_slice_chroma,
               0, FFMIN(6, ctb_log2_size_y) - min_qt_log2_size_intra_c);
            ue(sps_log2_diff_max_tt_min_qt_intra_slice_chroma,
               0, FFMIN(6, ctb_log2_size_y) - min_qt_log2_size_intra_c);
        }
    } else {
        infer(sps_log2_diff_min_qt_min_cb_intra_slice_chroma, 0);
        infer(sps_max_mtt_hierarchy_depth_intra_slice_chroma, 0);
    }
    if (current->sps_max_mtt_hierarchy_depth_intra_slice_chroma == 0) {
        infer(sps_log2_diff_max_bt_min_qt_intra_slice_chroma, 0);
        infer(sps_log2_diff_max_tt_min_qt_intra_slice_chroma, 0);
    }

    ue(sps_log2_diff_min_qt_min_cb_inter_slice,
       0, FFMIN(6, ctb_log2_size_y) - min_cb_log2_size_y);
    min_qt_log2_size_inter_y =
        current->sps_log2_diff_min_qt_min_cb_inter_slice + min_cb_log2_size_y;

    ue(sps_max_mtt_hierarchy_depth_inter_slice,
       0, 2 * (ctb_log2_size_y - min_cb_log2_size_y));
    if (current->sps_max_mtt_hierarchy_depth_inter_slice != 0) {
        ue(sps_log2_diff_max_bt_min_qt_inter_slice,
           0, ctb_log2_size_y - min_qt_log2_size_inter_y);
        ue(sps_log2_diff_max_tt_min_qt_inter_slice,
           0, FFMIN(6, ctb_log2_size_y) - min_qt_log2_size_inter_y);
    } else {
        infer(sps_log2_diff_max_bt_min_qt_inter_slice, 0);
        infer(sps_log2_diff_max_tt_min_qt_inter_slice, 0);
    }

    if (ctb_size_y > 32)
        flag(sps_max_luma_transform_size_64_flag);
    else
        infer(sps_max_luma_transform_size_64_flag, 0);

    flag(sps_transform_skip_enabled_flag);
    if (current->sps_transform_skip_enabled_flag) {
        ue(sps_log2_transform_skip_max_size_minus2, 0, 3);
        flag(sps_bdpcm_enabled_flag);
    }

    flag(sps_mts_enabled_flag);
    if (current->sps_mts_enabled_flag) {
        flag(sps_explicit_mts_intra_enabled_flag);
        flag(sps_explicit_mts_inter_enabled_flag);
    } else {
        infer(sps_explicit_mts_intra_enabled_flag, 0);
        infer(sps_explicit_mts_inter_enabled_flag, 0);
    }

    flag(sps_lfnst_enabled_flag);

    if (current->sps_chroma_format_idc != 0) {
        uint8_t num_qp_tables;
        flag(sps_joint_cbcr_enabled_flag);
        flag(sps_same_qp_table_for_chroma_flag);
        num_qp_tables = current->sps_same_qp_table_for_chroma_flag ?
            1 : (current->sps_joint_cbcr_enabled_flag ? 3 : 2);
        for (i = 0; i < num_qp_tables; i++) {
            ses(sps_qp_table_start_minus26[i], -26 - qp_bd_offset, 36, 1, i);
            ues(sps_num_points_in_qp_table_minus1[i],
                0, 36 - current->sps_qp_table_start_minus26[i], 1, i);
            for (j = 0; j <= current->sps_num_points_in_qp_table_minus1[i]; j++) {
                uint8_t max = MAX_UINT_BITS(8);
                ues(sps_delta_qp_in_val_minus1[i][j], 0, max, 2, i, j);
                ues(sps_delta_qp_diff_val[i][j], 0, max, 2, i, j);
            }
        }
    } else {
        infer(sps_joint_cbcr_enabled_flag, 0);
        infer(sps_same_qp_table_for_chroma_flag, 0);
    }

    flag(sps_sao_enabled_flag);
    flag(sps_alf_enabled_flag);
    if (current->sps_alf_enabled_flag && current->sps_chroma_format_idc)
        flag(sps_ccalf_enabled_flag);
    else
        infer(sps_ccalf_enabled_flag, 0);
    flag(sps_lmcs_enabled_flag);
    flag(sps_weighted_pred_flag);
    flag(sps_weighted_bipred_flag);
    flag(sps_long_term_ref_pics_flag);
    if (current->sps_video_parameter_set_id > 0)
        flag(sps_inter_layer_prediction_enabled_flag);
    else
        infer(sps_inter_layer_prediction_enabled_flag, 0);
    flag(sps_idr_rpl_present_flag);
    flag(sps_rpl1_same_as_rpl0_flag);

    for (i = 0; i < (current->sps_rpl1_same_as_rpl0_flag ? 1 : 2); i++) {
        ues(sps_num_ref_pic_lists[i], 0, VVC_MAX_REF_PIC_LISTS, 1, i);
        for (j = 0; j < current->sps_num_ref_pic_lists[i]; j++)
            CHECK(FUNC(ref_pic_list_struct) (ctx, rw,
                                             &current->
                                             sps_ref_pic_list_struct[i][j], i,
                                             j, current));
    }

    if (current->sps_rpl1_same_as_rpl0_flag) {
        current->sps_num_ref_pic_lists[1] = current->sps_num_ref_pic_lists[0];
        for (j = 0; j < current->sps_num_ref_pic_lists[0]; j++)
            memcpy(&current->sps_ref_pic_list_struct[1][j],
                   &current->sps_ref_pic_list_struct[0][j],
                   sizeof(current->sps_ref_pic_list_struct[0][j]));
    }

    flag(sps_ref_wraparound_enabled_flag);

    flag(sps_temporal_mvp_enabled_flag);
    if (current->sps_temporal_mvp_enabled_flag)
        flag(sps_sbtmvp_enabled_flag);
    else
        infer(sps_sbtmvp_enabled_flag, 0);

    flag(sps_amvr_enabled_flag);
    flag(sps_bdof_enabled_flag);
    if (current->sps_bdof_enabled_flag)
        flag(sps_bdof_control_present_in_ph_flag);
    else
        infer(sps_bdof_control_present_in_ph_flag, 0);

    flag(sps_smvd_enabled_flag);
    flag(sps_dmvr_enabled_flag);
    if (current->sps_dmvr_enabled_flag)
        flag(sps_dmvr_control_present_in_ph_flag);
    else
        infer(sps_dmvr_control_present_in_ph_flag, 0);

    flag(sps_mmvd_enabled_flag);
    if (current->sps_mmvd_enabled_flag)
        flag(sps_mmvd_fullpel_only_enabled_flag);
    else
        infer(sps_mmvd_fullpel_only_enabled_flag, 0);

    ue(sps_six_minus_max_num_merge_cand, 0, 5);
    max_num_merge_cand = 6 - current->sps_six_minus_max_num_merge_cand;

    flag(sps_sbt_enabled_flag);

    flag(sps_affine_enabled_flag);
    if (current->sps_affine_enabled_flag) {
        ue(sps_five_minus_max_num_subblock_merge_cand,
           0, 5 - current->sps_sbtmvp_enabled_flag);
        flag(sps_6param_affine_enabled_flag);
        if (current->sps_amvr_enabled_flag)
            flag(sps_affine_amvr_enabled_flag);
        else
            infer(sps_affine_amvr_enabled_flag, 0);
        flag(sps_affine_prof_enabled_flag);
        if (current->sps_affine_prof_enabled_flag)
            flag(sps_prof_control_present_in_ph_flag);
        else
            infer(sps_prof_control_present_in_ph_flag, 0);
    } else {
        infer(sps_6param_affine_enabled_flag, 0);
        infer(sps_affine_amvr_enabled_flag, 0);
        infer(sps_affine_prof_enabled_flag, 0);
        infer(sps_prof_control_present_in_ph_flag, 0);
    }

    flag(sps_bcw_enabled_flag);
    flag(sps_ciip_enabled_flag);

    if (max_num_merge_cand >= 2) {
        flag(sps_gpm_enabled_flag);
        if (current->sps_gpm_enabled_flag && max_num_merge_cand >= 3)
            ue(sps_max_num_merge_cand_minus_max_num_gpm_cand,
               0, max_num_merge_cand - 2);
    } else {
        infer(sps_gpm_enabled_flag, 0);
    }

    ue(sps_log2_parallel_merge_level_minus2, 0, ctb_log2_size_y - 2);

    flag(sps_isp_enabled_flag);
    flag(sps_mrl_enabled_flag);
    flag(sps_mip_enabled_flag);

    if (current->sps_chroma_format_idc != 0)
        flag(sps_cclm_enabled_flag);
    else
        infer(sps_cclm_enabled_flag, 0);
    if (current->sps_chroma_format_idc == 1) {
        flag(sps_chroma_horizontal_collocated_flag);
        flag(sps_chroma_vertical_collocated_flag);
    } else {
        infer(sps_chroma_horizontal_collocated_flag, 1);
        infer(sps_chroma_vertical_collocated_flag, 1);
    }

    flag(sps_palette_enabled_flag);
    if (current->sps_chroma_format_idc == 3 &&
        !current->sps_max_luma_transform_size_64_flag)
        flag(sps_act_enabled_flag);
    else
        infer(sps_act_enabled_flag, 0);
    if (current->sps_transform_skip_enabled_flag ||
        current->sps_palette_enabled_flag)
        ue(sps_min_qp_prime_ts, 0, 8);

    flag(sps_ibc_enabled_flag);
    if (current->sps_ibc_enabled_flag)
        ue(sps_six_minus_max_num_ibc_merge_cand, 0, 5);

    flag(sps_ladf_enabled_flag);
    if (current->sps_ladf_enabled_flag) {
        ub(2, sps_num_ladf_intervals_minus2);
        se(sps_ladf_lowest_interval_qp_offset, -63, 63);
        for (i = 0; i < current->sps_num_ladf_intervals_minus2 + 1; i++) {
            ses(sps_ladf_qp_offset[i], -63, 63, 1, i);
            ues(sps_ladf_delta_threshold_minus1[i],
                0, (2 << (8 + current->sps_bitdepth_minus8)) - 3, 1, i);
        }
    }

    flag(sps_explicit_scaling_list_enabled_flag);
    if (current->sps_lfnst_enabled_flag &&
        current->sps_explicit_scaling_list_enabled_flag)
        flag(sps_scaling_matrix_for_lfnst_disabled_flag);

    if (current->sps_act_enabled_flag &&
        current->sps_explicit_scaling_list_enabled_flag)
        flag(sps_scaling_matrix_for_alternative_colour_space_disabled_flag);
    else
        infer(sps_scaling_matrix_for_alternative_colour_space_disabled_flag, 0);
    if (current->sps_scaling_matrix_for_alternative_colour_space_disabled_flag)
        flag(sps_scaling_matrix_designated_colour_space_flag);

    flag(sps_dep_quant_enabled_flag);
    flag(sps_sign_data_hiding_enabled_flag);

    flag(sps_virtual_boundaries_enabled_flag);
    if (current->sps_virtual_boundaries_enabled_flag) {
        flag(sps_virtual_boundaries_present_flag);
        if (current->sps_virtual_boundaries_present_flag) {
            ue(sps_num_ver_virtual_boundaries,
               0, current->sps_pic_width_max_in_luma_samples <= 8 ? 0 : VVC_MAX_VBS);
            for (i = 0; i < current->sps_num_ver_virtual_boundaries; i++)
                ues(sps_virtual_boundary_pos_x_minus1[i],
                    0, (current->sps_pic_width_max_in_luma_samples + 7) / 8 - 2,
                    1, i);
            ue(sps_num_hor_virtual_boundaries,
               0, current->sps_pic_height_max_in_luma_samples <= 8 ? 0 : VVC_MAX_VBS);
            for (i = 0; i < current->sps_num_hor_virtual_boundaries; i++)
                ues(sps_virtual_boundary_pos_y_minus1[i],
                    0, (current->sps_pic_height_max_in_luma_samples + 7) /
                    8 - 2, 1, i);
        }
    } else {
        infer(sps_virtual_boundaries_present_flag, 0);
        infer(sps_num_ver_virtual_boundaries, 0);
        infer(sps_num_hor_virtual_boundaries, 0);
    }

    if (current->sps_ptl_dpb_hrd_params_present_flag) {
        flag(sps_timing_hrd_params_present_flag);
        if (current->sps_timing_hrd_params_present_flag) {
            uint8_t first_sublayer;
            CHECK(FUNC(general_timing_hrd_parameters) (ctx, rw,
                &current->sps_general_timing_hrd_parameters));
            if (current->sps_max_sublayers_minus1 > 0)
                flag(sps_sublayer_cpb_params_present_flag);
            else
                infer(sps_sublayer_cpb_params_present_flag, 0);
            first_sublayer = current->sps_sublayer_cpb_params_present_flag ?
                0 : current->sps_max_sublayers_minus1;
            CHECK(FUNC(ols_timing_hrd_parameters) (ctx, rw,
                &current->sps_ols_timing_hrd_parameters, first_sublayer,
                current->sps_max_sublayers_minus1,
                &current->sps_general_timing_hrd_parameters));
        }
    }

    flag(sps_field_seq_flag);
    flag(sps_vui_parameters_present_flag);
    if (current->sps_vui_parameters_present_flag) {
        ue(sps_vui_payload_size_minus1, 0, 1023);
        while (byte_alignment(rw) != 0)
            fixed(1, sps_vui_alignment_zero_bit, 0);
        CHECK(FUNC(vui_payload) (ctx, rw, &current->vui,
                                 current->sps_vui_payload_size_minus1 + 1,
                                 current->sps_chroma_format_idc));
    } else {
        CHECK(FUNC(vui_parameters_default) (ctx, rw, &current->vui));
    }

    flag(sps_extension_flag);
    if (current->sps_extension_flag) {
        flag(sps_range_extension_flag);
        ub(7, sps_extension_7bits);

        if (current->sps_range_extension_flag) {
            if (current->sps_bitdepth_minus8 <= 10 - 8)
                return AVERROR_INVALIDDATA;
            CHECK(FUNC(sps_range_extension)(ctx, rw, current));
        } else {
            infer(sps_extended_precision_flag, 0);
            infer(sps_ts_residual_coding_rice_present_in_sh_flag, 0);
            infer(sps_rrc_rice_extension_flag, 0);
            infer(sps_persistent_rice_adaptation_enabled_flag, 0);
            infer(sps_reverse_last_sig_coeff_enabled_flag, 0);
        }
    } else {
        infer(sps_range_extension_flag, 0);
        infer(sps_extension_7bits, 0);
        infer(sps_extended_precision_flag, 0);
        infer(sps_ts_residual_coding_rice_present_in_sh_flag, 0);
        infer(sps_rrc_rice_extension_flag, 0);
        infer(sps_persistent_rice_adaptation_enabled_flag, 0);
        infer(sps_reverse_last_sig_coeff_enabled_flag, 0);
    }

    if (current->sps_extension_7bits)
        CHECK(FUNC(extension_data)(ctx, rw, &current->extension_data));

    CHECK(FUNC(rbsp_trailing_bits) (ctx, rw));

    return 0;
}

static int FUNC(pps) (CodedBitstreamContext *ctx, RWContext *rw,
                      H266RawPPS *current)
{
    CodedBitstreamH266Context *h266 = ctx->priv_data;
    const H266RawSPS *sps;
    int err, i;
    unsigned int min_cb_size_y, divisor, ctb_size_y,
        pic_width_in_ctbs_y, pic_height_in_ctbs_y;
    uint8_t sub_width_c, sub_height_c, qp_bd_offset;

    static const uint8_t h266_sub_width_c[] = {
        1, 2, 2, 1
    };
    static const uint8_t h266_sub_height_c[] = {
        1, 2, 1, 1
    };

    HEADER("Picture Parameter Set");

    CHECK(FUNC(nal_unit_header) (ctx, rw,
                                 &current->nal_unit_header, VVC_PPS_NUT));

    ub(6, pps_pic_parameter_set_id);
    ub(4, pps_seq_parameter_set_id);
    sps = h266->sps[current->pps_seq_parameter_set_id];
    if (!sps) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "SPS id %d not available.\n",
               current->pps_seq_parameter_set_id);
        return AVERROR_INVALIDDATA;
    }

    flag(pps_mixed_nalu_types_in_pic_flag);
    ue(pps_pic_width_in_luma_samples,
       1, sps->sps_pic_width_max_in_luma_samples);
    ue(pps_pic_height_in_luma_samples,
       1, sps->sps_pic_height_max_in_luma_samples);

    min_cb_size_y = 1 << (sps->sps_log2_min_luma_coding_block_size_minus2 + 2);
    divisor = FFMAX(min_cb_size_y, 8);
    if (current->pps_pic_width_in_luma_samples % divisor ||
        current->pps_pic_height_in_luma_samples % divisor) {
        av_log(ctx->log_ctx, AV_LOG_ERROR,
               "Invalid dimensions: %ux%u not divisible "
               "by %u, MinCbSizeY = %u.\n",
               current->pps_pic_width_in_luma_samples,
               current->pps_pic_height_in_luma_samples, divisor, min_cb_size_y);
        return AVERROR_INVALIDDATA;
    }
    if (!sps->sps_res_change_in_clvs_allowed_flag &&
        (current->pps_pic_width_in_luma_samples !=
         sps->sps_pic_width_max_in_luma_samples ||
         current->pps_pic_height_in_luma_samples !=
         sps->sps_pic_height_max_in_luma_samples)) {
        av_log(ctx->log_ctx, AV_LOG_ERROR,
               "Resoltuion change is not allowed, "
               "in max resolution (%ux%u) mismatched with pps(%ux%u).\n",
               sps->sps_pic_width_max_in_luma_samples,
               sps->sps_pic_height_max_in_luma_samples,
               current->pps_pic_width_in_luma_samples,
               current->pps_pic_height_in_luma_samples);
        return AVERROR_INVALIDDATA;
    }

    ctb_size_y = 1 << (sps->sps_log2_ctu_size_minus5 + 5);
    if (sps->sps_ref_wraparound_enabled_flag) {
        if ((ctb_size_y / min_cb_size_y + 1) >
            (current->pps_pic_width_in_luma_samples / min_cb_size_y - 1)) {
            av_log(ctx->log_ctx, AV_LOG_ERROR,
                   "Invalid width(%u), ctb_size_y = %u, min_cb_size_y = %u.\n",
                   current->pps_pic_width_in_luma_samples,
                   ctb_size_y, min_cb_size_y);
            return AVERROR_INVALIDDATA;
        }
    }

    flag(pps_conformance_window_flag);
    if (current->pps_pic_width_in_luma_samples ==
        sps->sps_pic_width_max_in_luma_samples &&
        current->pps_pic_height_in_luma_samples ==
        sps->sps_pic_height_max_in_luma_samples &&
        current->pps_conformance_window_flag) {
        av_log(ctx->log_ctx, AV_LOG_ERROR,
               "Conformance window flag should not true.\n");
        return AVERROR_INVALIDDATA;
    }

    sub_width_c = h266_sub_width_c[sps->sps_chroma_format_idc];
    sub_height_c = h266_sub_height_c[sps->sps_chroma_format_idc];
    if (current->pps_conformance_window_flag) {
        ue(pps_conf_win_left_offset, 0, current->pps_pic_width_in_luma_samples);
        ue(pps_conf_win_right_offset,
           0, current->pps_pic_width_in_luma_samples);
        ue(pps_conf_win_top_offset, 0, current->pps_pic_height_in_luma_samples);
        ue(pps_conf_win_bottom_offset,
           0, current->pps_pic_height_in_luma_samples);
        if (sub_width_c *
            (current->pps_conf_win_left_offset +
             current->pps_conf_win_right_offset) >=
            current->pps_pic_width_in_luma_samples ||
            sub_height_c *
            (current->pps_conf_win_top_offset +
             current->pps_conf_win_bottom_offset) >=
            current->pps_pic_height_in_luma_samples) {
            av_log(ctx->log_ctx, AV_LOG_ERROR,
                   "Invalid pps conformance window: (%u, %u, %u, %u), "
                   "resolution is %ux%u, sub wxh is %ux%u.\n",
                   current->pps_conf_win_left_offset,
                   current->pps_conf_win_right_offset,
                   current->pps_conf_win_top_offset,
                   current->pps_conf_win_bottom_offset,
                   current->pps_pic_width_in_luma_samples,
                   current->pps_pic_height_in_luma_samples,
                   sub_width_c, sub_height_c);
            return AVERROR_INVALIDDATA;
        }
    } else {
        if (current->pps_pic_width_in_luma_samples ==
            sps->sps_pic_width_max_in_luma_samples &&
            current->pps_pic_height_in_luma_samples ==
            sps->sps_pic_height_max_in_luma_samples) {
            infer(pps_conf_win_left_offset, sps->sps_conf_win_left_offset);
            infer(pps_conf_win_right_offset, sps->sps_conf_win_right_offset);
            infer(pps_conf_win_top_offset, sps->sps_conf_win_top_offset);
            infer(pps_conf_win_bottom_offset, sps->sps_conf_win_bottom_offset);
        } else {
            infer(pps_conf_win_left_offset, 0);
            infer(pps_conf_win_right_offset, 0);
            infer(pps_conf_win_top_offset, 0);
            infer(pps_conf_win_bottom_offset, 0);
        }

    }

    flag(pps_scaling_window_explicit_signalling_flag);
    if (!sps->sps_ref_pic_resampling_enabled_flag &&
        current->pps_scaling_window_explicit_signalling_flag) {
        av_log(ctx->log_ctx, AV_LOG_ERROR,
               "Invalid data: sps_ref_pic_resampling_enabled_flag is false, "
               "but pps_scaling_window_explicit_signalling_flag is true.\n");
        return AVERROR_INVALIDDATA;
    }
    if (current->pps_scaling_window_explicit_signalling_flag) {
        se(pps_scaling_win_left_offset,
           -current->pps_pic_width_in_luma_samples * 15 / sub_width_c,
           current->pps_pic_width_in_luma_samples / sub_width_c);
        se(pps_scaling_win_right_offset,
           -current->pps_pic_width_in_luma_samples * 15 / sub_width_c,
           current->pps_pic_width_in_luma_samples / sub_width_c);
        se(pps_scaling_win_top_offset,
           -current->pps_pic_height_in_luma_samples * 15 / sub_height_c,
           current->pps_pic_height_in_luma_samples / sub_height_c);
        se(pps_scaling_win_bottom_offset,
           -current->pps_pic_height_in_luma_samples * 15 / sub_height_c,
           current->pps_pic_height_in_luma_samples / sub_height_c);
    } else {
        infer(pps_scaling_win_left_offset, current->pps_conf_win_left_offset);
        infer(pps_scaling_win_right_offset, current->pps_conf_win_right_offset);
        infer(pps_scaling_win_top_offset, current->pps_conf_win_top_offset);
        infer(pps_scaling_win_bottom_offset, current->pps_conf_win_bottom_offset);
    }

    flag(pps_output_flag_present_flag);
    flag(pps_no_pic_partition_flag);
    flag(pps_subpic_id_mapping_present_flag);

    if (current->pps_subpic_id_mapping_present_flag) {
        if (!current->pps_no_pic_partition_flag) {
            ue(pps_num_subpics_minus1,
               sps->sps_num_subpics_minus1, sps->sps_num_subpics_minus1);
        } else {
            infer(pps_num_subpics_minus1, 0);
        }
        ue(pps_subpic_id_len_minus1, sps->sps_subpic_id_len_minus1,
           sps->sps_subpic_id_len_minus1);
        for (i = 0; i <= current->pps_num_subpics_minus1; i++) {
            ubs(sps->sps_subpic_id_len_minus1 + 1, pps_subpic_id[i], 1, i);
        }
    }

    for (i = 0; i <= sps->sps_num_subpics_minus1; i++) {
        if (sps->sps_subpic_id_mapping_explicitly_signalled_flag)
            current->sub_pic_id_val[i] = current->pps_subpic_id_mapping_present_flag
                                       ? current->pps_subpic_id[i]
                                       : sps->sps_subpic_id[i];
        else
            current->sub_pic_id_val[i] = i;
    }

    pic_width_in_ctbs_y = AV_CEIL_RSHIFT
        (current->pps_pic_width_in_luma_samples, (sps->sps_log2_ctu_size_minus5 + 5));
    pic_height_in_ctbs_y = AV_CEIL_RSHIFT(
        current->pps_pic_height_in_luma_samples,(sps->sps_log2_ctu_size_minus5 + 5));
    if (!current->pps_no_pic_partition_flag) {
        unsigned int exp_tile_width = 0, exp_tile_height = 0;
        unsigned int unified_size, remaining_size;

        u(2, pps_log2_ctu_size_minus5,
          sps->sps_log2_ctu_size_minus5, sps->sps_log2_ctu_size_minus5);
        ue(pps_num_exp_tile_columns_minus1,
           0, FFMIN(pic_width_in_ctbs_y - 1, VVC_MAX_TILE_COLUMNS - 1));
        ue(pps_num_exp_tile_rows_minus1,
           0, FFMIN(pic_height_in_ctbs_y - 1, VVC_MAX_TILE_ROWS - 1));

        for (i = 0; i <= current->pps_num_exp_tile_columns_minus1; i++) {
            ues(pps_tile_column_width_minus1[i],
                0, pic_width_in_ctbs_y - exp_tile_width - 1, 1, i);
            exp_tile_width += current->pps_tile_column_width_minus1[i] + 1;
        }
        for (i = 0; i <= current->pps_num_exp_tile_rows_minus1; i++) {
            ues(pps_tile_row_height_minus1[i],
                0, pic_height_in_ctbs_y - exp_tile_height - 1, 1, i);
            exp_tile_height += current->pps_tile_row_height_minus1[i] + 1;
        }

        remaining_size = pic_width_in_ctbs_y;
        for (i = 0; i <= current->pps_num_exp_tile_columns_minus1; i++) {
          if (current->pps_tile_column_width_minus1[i] >= remaining_size) {
              av_log(ctx->log_ctx, AV_LOG_ERROR,
                     "Tile column width(%d) exceeds picture width\n",i);
              return AVERROR_INVALIDDATA;
          }
          current->col_width_val[i] = current->pps_tile_column_width_minus1[i] + 1;
          remaining_size -= (current->pps_tile_column_width_minus1[i] + 1);
        }
        unified_size = current->pps_tile_column_width_minus1[i - 1] + 1;
        while (remaining_size > 0) {
            if (current->num_tile_columns > VVC_MAX_TILE_COLUMNS) {
                av_log(ctx->log_ctx, AV_LOG_ERROR,
                       "NumTileColumns(%d) > than VVC_MAX_TILE_COLUMNS(%d)\n",
                       current->num_tile_columns, VVC_MAX_TILE_COLUMNS);
                return AVERROR_INVALIDDATA;
            }
            unified_size = FFMIN(remaining_size, unified_size);
            current->col_width_val[i] = unified_size;
            remaining_size -= unified_size;
            i++;
        }
        current->num_tile_columns = i;
        if (current->num_tile_columns > VVC_MAX_TILE_COLUMNS) {
            av_log(ctx->log_ctx, AV_LOG_ERROR,
                   "NumTileColumns(%d) > than VVC_MAX_TILE_COLUMNS(%d)\n",
                   current->num_tile_columns, VVC_MAX_TILE_COLUMNS);
            return AVERROR_INVALIDDATA;
        }

        remaining_size = pic_height_in_ctbs_y;
        for (i = 0; i <= current->pps_num_exp_tile_rows_minus1; i++) {
          if (current->pps_tile_row_height_minus1[i] >= remaining_size) {
              av_log(ctx->log_ctx, AV_LOG_ERROR,
                     "Tile row height(%d) exceeds picture height\n",i);
              return AVERROR_INVALIDDATA;
          }
          current->row_height_val[i] = current->pps_tile_row_height_minus1[i] + 1;
          remaining_size -= (current->pps_tile_row_height_minus1[i] + 1);
        }
        unified_size = current->pps_tile_row_height_minus1[i - 1] + 1;

        while (remaining_size > 0) {
            unified_size = FFMIN(remaining_size, unified_size);
            current->row_height_val[i] = unified_size;
            remaining_size -= unified_size;
            i++;
        }
        current->num_tile_rows=i;
        if (current->num_tile_rows > VVC_MAX_TILE_ROWS) {
            av_log(ctx->log_ctx, AV_LOG_ERROR,
                   "NumTileRows(%d) > than VVC_MAX_TILE_ROWS(%d)\n",
                   current->num_tile_rows, VVC_MAX_TILE_ROWS);
            return AVERROR_INVALIDDATA;
        }

        current->num_tiles_in_pic = current->num_tile_columns *
                                    current->num_tile_rows;
        if (current->num_tiles_in_pic > VVC_MAX_TILES_PER_AU) {
            av_log(ctx->log_ctx, AV_LOG_ERROR,
                   "NumTilesInPic(%d) > than VVC_MAX_TILES_PER_AU(%d)\n",
                   current->num_tiles_in_pic, VVC_MAX_TILES_PER_AU);
            return AVERROR_INVALIDDATA;
        }

        if (current->num_tiles_in_pic > 1) {
            flag(pps_loop_filter_across_tiles_enabled_flag);
            flag(pps_rect_slice_flag);
        } else {
            infer(pps_loop_filter_across_tiles_enabled_flag, 0);
            infer(pps_rect_slice_flag, 1);
        }
        if (current->pps_rect_slice_flag)
            flag(pps_single_slice_per_subpic_flag);
        else
            infer(pps_single_slice_per_subpic_flag, 1);
        if (current->pps_rect_slice_flag &&
            !current->pps_single_slice_per_subpic_flag) {
            int j;
            uint16_t tile_idx = 0, tile_x, tile_y, ctu_x, ctu_y;
            uint16_t slice_top_left_ctu_x[VVC_MAX_SLICES];
            uint16_t slice_top_left_ctu_y[VVC_MAX_SLICES];
            ue(pps_num_slices_in_pic_minus1, 0, VVC_MAX_SLICES - 1);
            if (current->pps_num_slices_in_pic_minus1 > 1)
                flag(pps_tile_idx_delta_present_flag);
            else
                infer(pps_tile_idx_delta_present_flag, 0);
            for (i = 0; i < current->pps_num_slices_in_pic_minus1; i++) {
                tile_x = tile_idx % current->num_tile_columns;
                tile_y = tile_idx / current->num_tile_columns;
                if (tile_x != current->num_tile_columns - 1) {
                    ues(pps_slice_width_in_tiles_minus1[i],
                        0, current->num_tile_columns - 1, 1, i);
                } else {
                    infer(pps_slice_width_in_tiles_minus1[i], 0);
                }
                if (tile_y != current->num_tile_rows - 1 &&
                    (current->pps_tile_idx_delta_present_flag || tile_x == 0)) {
                    ues(pps_slice_height_in_tiles_minus1[i],
                        0, current->num_tile_rows - 1, 1, i);
                } else {
                    if (tile_y == current->num_tile_rows - 1)
                        infer(pps_slice_height_in_tiles_minus1[i], 0);
                    else
                        infer(pps_slice_height_in_tiles_minus1[i],
                              current->pps_slice_height_in_tiles_minus1[i - 1]);
                }

                ctu_x = ctu_y = 0;
                for (j = 0; j < tile_x; j++) {
                    ctu_x += current->col_width_val[j];
                }
                for (j = 0; j < tile_y; j++) {
                    ctu_y += current->row_height_val[j];
                }
                if (current->pps_slice_width_in_tiles_minus1[i] == 0 &&
                    current->pps_slice_height_in_tiles_minus1[i] == 0 &&
                    current->row_height_val[tile_y] > 1) {
                    int num_slices_in_tile,
                        uniform_slice_height, remaining_height_in_ctbs_y;
                    remaining_height_in_ctbs_y =
                        current->row_height_val[tile_y];
                    ues(pps_num_exp_slices_in_tile[i],
                        0, current->row_height_val[tile_y] - 1, 1, i);
                    if (current->pps_num_exp_slices_in_tile[i] == 0) {
                        num_slices_in_tile = 1;
                        current->slice_height_in_ctus[i] = current->row_height_val[tile_y];
                        slice_top_left_ctu_x[i] = ctu_x;
                        slice_top_left_ctu_y[i] = ctu_y;
                    } else {
                        uint16_t slice_height_in_ctus;
                        for (j = 0; j < current->pps_num_exp_slices_in_tile[i];
                             j++) {
                            ues(pps_exp_slice_height_in_ctus_minus1[i][j], 0,
                                current->row_height_val[tile_y] - 1, 2,
                                i, j);
                            slice_height_in_ctus =
                                current->
                                pps_exp_slice_height_in_ctus_minus1[i][j] + 1;

                            current->slice_height_in_ctus[i + j] =
                                slice_height_in_ctus;
                            slice_top_left_ctu_x[i + j] = ctu_x;
                            slice_top_left_ctu_y[i + j] = ctu_y;
                            ctu_y += slice_height_in_ctus;

                            remaining_height_in_ctbs_y -= slice_height_in_ctus;
                        }
                        uniform_slice_height = 1 +
                            (j == 0 ? current->row_height_val[tile_y] - 1:
                            current->pps_exp_slice_height_in_ctus_minus1[i][j-1]);
                        while (remaining_height_in_ctbs_y > uniform_slice_height) {
                            current->slice_height_in_ctus[i + j] =
                                                          uniform_slice_height;
                            slice_top_left_ctu_x[i + j] = ctu_x;
                            slice_top_left_ctu_y[i + j] = ctu_y;
                            ctu_y += uniform_slice_height;

                            remaining_height_in_ctbs_y -= uniform_slice_height;
                            j++;
                        }
                        if (remaining_height_in_ctbs_y > 0) {
                            current->slice_height_in_ctus[i + j] =
                                remaining_height_in_ctbs_y;
                            slice_top_left_ctu_x[i + j] = ctu_x;
                            slice_top_left_ctu_y[i + j] = ctu_y;
                            j++;
                        }
                        num_slices_in_tile = j;
                    }
                    i += num_slices_in_tile - 1;
                } else {
                    uint16_t height = 0;
                    infer(pps_num_exp_slices_in_tile[i], 0);
                    for (j = 0;
                         j <= current->pps_slice_height_in_tiles_minus1[i];
                         j++) {
                        height +=
                           current->row_height_val[tile_y + j];
                    }
                    current->slice_height_in_ctus[i] = height;

                    slice_top_left_ctu_x[i] = ctu_x;
                    slice_top_left_ctu_y[i] = ctu_y;
                }
                if (i < current->pps_num_slices_in_pic_minus1) {
                    if (current->pps_tile_idx_delta_present_flag) {
                        // Two conditions must be met:
                        // 1. −NumTilesInPic + 1 <= pps_tile_idx_delta_val[i] <= NumTilesInPic − 1
                        // 2. 0 <= tile_idx + pps_tile_idx_delta_val[i] <= NumTilesInPic − 1
                        // Combining these conditions yields: -tile_idx <= pps_tile_idx_delta_val[i] <= NumTilesInPic - 1 - tile_idx
                        ses(pps_tile_idx_delta_val[i],
                            -tile_idx, current->num_tiles_in_pic - 1 - tile_idx, 1, i);
                        if (current->pps_tile_idx_delta_val[i] == 0) {
                            av_log(ctx->log_ctx, AV_LOG_ERROR,
                                   "pps_tile_idx_delta_val[i] shall not be equal to 0.\n");
                        }
                        tile_idx += current->pps_tile_idx_delta_val[i];
                    } else {
                        infer(pps_tile_idx_delta_val[i], 0);
                        tile_idx +=
                            current->pps_slice_width_in_tiles_minus1[i] + 1;
                        if (tile_idx % current->num_tile_columns == 0) {
                            tile_idx +=
                                current->pps_slice_height_in_tiles_minus1[i] *
                                current->num_tile_columns;
                        }
                    }
                }
            }
            if (i == current->pps_num_slices_in_pic_minus1) {
                uint16_t height = 0;

                tile_x = tile_idx % current->num_tile_columns;
                tile_y = tile_idx / current->num_tile_columns;
                if (tile_y >= current->num_tile_rows)
                    return AVERROR_INVALIDDATA;

                ctu_x = 0, ctu_y = 0;
                for (j = 0; j < tile_x; j++) {
                    ctu_x += current->col_width_val[j];
                }
                for (j = 0; j < tile_y; j++) {
                    ctu_y += current->row_height_val[j];
                }
                slice_top_left_ctu_x[i] = ctu_x;
                slice_top_left_ctu_y[i] = ctu_y;

                current->pps_slice_width_in_tiles_minus1[i] =
                    current->num_tile_columns - tile_x - 1;
                current->pps_slice_height_in_tiles_minus1[i] =
                    current->num_tile_rows - tile_y - 1;

                for (j = 0; j <= current->pps_slice_height_in_tiles_minus1[i];
                     j++) {
                    height +=
                        current->row_height_val[tile_y + j];
                }
                current->slice_height_in_ctus[i] = height;

                infer(pps_num_exp_slices_in_tile[i], 0);
            }
            //now, we got all slice information, let's resolve NumSlicesInSubpic
            for (i = 0; i <= sps->sps_num_subpics_minus1; i++) {
                current->num_slices_in_subpic[i] = 0;
                for (j = 0; j <= current->pps_num_slices_in_pic_minus1; j++) {
                    uint16_t pos_x = 0, pos_y = 0;
                    pos_x = slice_top_left_ctu_x[j];
                    pos_y = slice_top_left_ctu_y[j];
                    if ((pos_x >= sps->sps_subpic_ctu_top_left_x[i]) &&
                        (pos_x <
                         sps->sps_subpic_ctu_top_left_x[i] +
                         sps->sps_subpic_width_minus1[i] + 1) &&
                         (pos_y >= sps->sps_subpic_ctu_top_left_y[i]) &&
                         (pos_y < sps->sps_subpic_ctu_top_left_y[i] +
                            sps->sps_subpic_height_minus1[i] + 1)) {
                        current->num_slices_in_subpic[i]++;
                    }
                }
            }
        } else {
            if (current->pps_no_pic_partition_flag)
                infer(pps_num_slices_in_pic_minus1, 0);
            else if (current->pps_single_slice_per_subpic_flag) {
                for (i = 0; i <= sps->sps_num_subpics_minus1; i++)
                    current->num_slices_in_subpic[i] = 1;
                infer(pps_num_slices_in_pic_minus1,
                      sps->sps_num_subpics_minus1);
            }
            // else?
        }
        if (!current->pps_rect_slice_flag ||
            current->pps_single_slice_per_subpic_flag ||
            current->pps_num_slices_in_pic_minus1 > 0)
            flag(pps_loop_filter_across_slices_enabled_flag);
        else
            infer(pps_loop_filter_across_slices_enabled_flag, 0);
    } else {
        infer(pps_num_exp_tile_columns_minus1, 0);
        infer(pps_tile_column_width_minus1[0], pic_width_in_ctbs_y - 1);
        infer(pps_num_exp_tile_rows_minus1, 0);
        infer(pps_tile_row_height_minus1[0], pic_height_in_ctbs_y - 1);
        current->col_width_val[0] = pic_width_in_ctbs_y;
        current->row_height_val[0] = pic_height_in_ctbs_y;
        current->num_tile_columns = 1;
        current->num_tile_rows = 1;
        current->num_tiles_in_pic = 1;
    }

    flag(pps_cabac_init_present_flag);
    for (i = 0; i < 2; i++)
        ues(pps_num_ref_idx_default_active_minus1[i], 0, 14, 1, i);
    flag(pps_rpl1_idx_present_flag);
    flag(pps_weighted_pred_flag);
    flag(pps_weighted_bipred_flag);
    flag(pps_ref_wraparound_enabled_flag);
    if (current->pps_ref_wraparound_enabled_flag) {
        ue(pps_pic_width_minus_wraparound_offset,
           0, (current->pps_pic_width_in_luma_samples / min_cb_size_y)
           - (ctb_size_y / min_cb_size_y) - 2);
    }

    qp_bd_offset = 6 * sps->sps_bitdepth_minus8;
    se(pps_init_qp_minus26, -(26 + qp_bd_offset), 37);
    flag(pps_cu_qp_delta_enabled_flag);
    flag(pps_chroma_tool_offsets_present_flag);
    if (current->pps_chroma_tool_offsets_present_flag) {
        se(pps_cb_qp_offset, -12, 12);
        se(pps_cr_qp_offset, -12, 12);
        flag(pps_joint_cbcr_qp_offset_present_flag);
        if (current->pps_joint_cbcr_qp_offset_present_flag)
            se(pps_joint_cbcr_qp_offset_value, -12, 12);
        else
            infer(pps_joint_cbcr_qp_offset_value, 0);
        flag(pps_slice_chroma_qp_offsets_present_flag);
        flag(pps_cu_chroma_qp_offset_list_enabled_flag);
        if (current->pps_cu_chroma_qp_offset_list_enabled_flag) {
            ue(pps_chroma_qp_offset_list_len_minus1, 0, 5);
            for (i = 0; i <= current->pps_chroma_qp_offset_list_len_minus1; i++) {
                ses(pps_cb_qp_offset_list[i], -12, 12, 1, i);
                ses(pps_cr_qp_offset_list[i], -12, 12, 1, i);
                if (current->pps_joint_cbcr_qp_offset_present_flag)
                    ses(pps_joint_cbcr_qp_offset_list[i], -12, 12, 1, i);
                else
                    infer(pps_joint_cbcr_qp_offset_list[i], 0);
            }
        }
    } else {
        infer(pps_cb_qp_offset, 0);
        infer(pps_cr_qp_offset, 0);
        infer(pps_joint_cbcr_qp_offset_present_flag, 0);
        infer(pps_joint_cbcr_qp_offset_value, 0);
        infer(pps_slice_chroma_qp_offsets_present_flag, 0);
        infer(pps_cu_chroma_qp_offset_list_enabled_flag, 0);
    }
    flag(pps_deblocking_filter_control_present_flag);
    if (current->pps_deblocking_filter_control_present_flag) {
        flag(pps_deblocking_filter_override_enabled_flag);
        flag(pps_deblocking_filter_disabled_flag);
        if (!current->pps_no_pic_partition_flag &&
            current->pps_deblocking_filter_override_enabled_flag)
            flag(pps_dbf_info_in_ph_flag);
        else
            infer(pps_dbf_info_in_ph_flag, 0);
        if (!current->pps_deblocking_filter_disabled_flag) {
            se(pps_luma_beta_offset_div2, -12, 12);
            se(pps_luma_tc_offset_div2, -12, 12);
            if (current->pps_chroma_tool_offsets_present_flag) {
                se(pps_cb_beta_offset_div2, -12, 12);
                se(pps_cb_tc_offset_div2, -12, 12);
                se(pps_cr_beta_offset_div2, -12, 12);
                se(pps_cr_tc_offset_div2, -12, 12);
            } else {
                infer(pps_cb_beta_offset_div2,
                      current->pps_luma_beta_offset_div2);
                infer(pps_cb_tc_offset_div2, current->pps_luma_tc_offset_div2);
                infer(pps_cr_beta_offset_div2,
                      current->pps_luma_beta_offset_div2);
                infer(pps_cr_tc_offset_div2, current->pps_luma_tc_offset_div2);
            }
        } else {
            infer(pps_luma_beta_offset_div2, 0);
            infer(pps_luma_tc_offset_div2, 0);
            infer(pps_cb_beta_offset_div2, 0);
            infer(pps_cb_tc_offset_div2, 0);
            infer(pps_cr_beta_offset_div2, 0);
            infer(pps_cr_tc_offset_div2, 0);
        }
    } else {
        infer(pps_deblocking_filter_override_enabled_flag, 0);
        infer(pps_deblocking_filter_disabled_flag, 0);
        infer(pps_dbf_info_in_ph_flag, 0);
        infer(pps_luma_beta_offset_div2, 0);
        infer(pps_luma_tc_offset_div2, 0);
        infer(pps_cb_beta_offset_div2, 0);
        infer(pps_cb_tc_offset_div2, 0);
        infer(pps_cr_beta_offset_div2, 0);
        infer(pps_cr_tc_offset_div2, 0);
    }

    if (!current->pps_no_pic_partition_flag) {
        flag(pps_rpl_info_in_ph_flag);
        flag(pps_sao_info_in_ph_flag);
        flag(pps_alf_info_in_ph_flag);
        if ((current->pps_weighted_pred_flag ||
             current->pps_weighted_bipred_flag) &&
            current->pps_rpl_info_in_ph_flag)
            flag(pps_wp_info_in_ph_flag);
        flag(pps_qp_delta_info_in_ph_flag);
    }
    flag(pps_picture_header_extension_present_flag);
    flag(pps_slice_header_extension_present_flag);

    flag(pps_extension_flag);
    if (current->pps_extension_flag)
        CHECK(FUNC(extension_data) (ctx, rw, &current->extension_data));

    CHECK(FUNC(rbsp_trailing_bits) (ctx, rw));
    return 0;
}

static int FUNC(alf_data)(CodedBitstreamContext *ctx, RWContext *rw,
                          H266RawAPS *current)
{
    int err, j, k;

    flag(alf_luma_filter_signal_flag);

    if (current->aps_chroma_present_flag) {
        flag(alf_chroma_filter_signal_flag);
        flag(alf_cc_cb_filter_signal_flag);
        flag(alf_cc_cr_filter_signal_flag);
    } else {
        infer(alf_chroma_filter_signal_flag, 0);
        infer(alf_cc_cb_filter_signal_flag, 0);
        infer(alf_cc_cr_filter_signal_flag, 0);
    }

    if (current->alf_luma_filter_signal_flag) {
        flag(alf_luma_clip_flag);
        ue(alf_luma_num_filters_signalled_minus1, 0, VVC_NUM_ALF_FILTERS - 1);
        if (current->alf_luma_num_filters_signalled_minus1 > 0) {
            unsigned int bits = av_ceil_log2(current->alf_luma_num_filters_signalled_minus1 + 1);
            for (int filt_idx = 0; filt_idx < VVC_NUM_ALF_FILTERS; filt_idx++)
                us(bits, alf_luma_coeff_delta_idx[filt_idx],
                   0, current->alf_luma_num_filters_signalled_minus1,
                   1, filt_idx);
        }
        for (int sf_idx = 0; sf_idx <= current->alf_luma_num_filters_signalled_minus1; sf_idx++)
            for (j = 0; j < 12; j++) {
                ues(alf_luma_coeff_abs[sf_idx][j], 0, 128, 2, sf_idx, j);
                if (current->alf_luma_coeff_abs[sf_idx][j])
                    ubs(1, alf_luma_coeff_sign[sf_idx][j], 2, sf_idx, j);
                else
                    infer(alf_luma_coeff_sign[sf_idx][j], 0);
            }
    } else {
        infer(alf_luma_clip_flag, 0);
        infer(alf_luma_num_filters_signalled_minus1, 0);
    }
    for (int sf_idx = 0; sf_idx <= current->alf_luma_num_filters_signalled_minus1; sf_idx++) {
        for (j = 0; j < 12; j++) {
            if (current->alf_luma_clip_flag)
                ubs(2, alf_luma_clip_idx[sf_idx][j], 2, sf_idx, j);
            else
                infer(alf_luma_clip_idx[sf_idx][j], 0);
        }
    }

    if (current->alf_chroma_filter_signal_flag) {
        flag(alf_chroma_clip_flag);
        ue(alf_chroma_num_alt_filters_minus1, 0, 7);
    } else {
        infer(alf_chroma_clip_flag, 0);
        infer(alf_chroma_num_alt_filters_minus1, 0);
    }
    for (int alt_idx = 0; alt_idx <= current->alf_chroma_num_alt_filters_minus1; alt_idx++) {
        for (j = 0; j < 6; j++) {
            if (current->alf_chroma_filter_signal_flag)
                ues(alf_chroma_coeff_abs[alt_idx][j], 0, 128, 2, alt_idx, j);
            else
                infer(alf_chroma_coeff_abs[alt_idx][j], 0);
            if (current->alf_chroma_coeff_abs[alt_idx][j] > 0)
                ubs(1, alf_chroma_coeff_sign[alt_idx][j], 2, alt_idx, j);
            else
                infer(alf_chroma_coeff_sign[alt_idx][j], 0);
        }
        for (j = 0; j < 6; j++) {
            if (current->alf_chroma_clip_flag)
                ubs(2, alf_chroma_clip_idx[alt_idx][j], 2, alt_idx, j);
            else
                infer(alf_chroma_clip_idx[alt_idx][j], 0);
        }
    }

    if (current->alf_cc_cb_filter_signal_flag)
        ue(alf_cc_cb_filters_signalled_minus1, 0, 3);
    else
        infer(alf_cc_cb_filters_signalled_minus1, 0);
    for (k = 0; k <= current->alf_cc_cb_filters_signalled_minus1; k++) {
        for (j = 0; j < 7; j++) {
            if (current->alf_cc_cb_filter_signal_flag)
                ubs(3, alf_cc_cb_mapped_coeff_abs[k][j], 2, k, j);
            else
                infer(alf_cc_cb_mapped_coeff_abs[k][j], 0);
            if (current->alf_cc_cb_mapped_coeff_abs[k][j])
                ubs(1, alf_cc_cb_coeff_sign[k][j], 2, k, j);
            else
                infer(alf_cc_cb_coeff_sign[k][j], 0);
        }
    }

    if (current->alf_cc_cr_filter_signal_flag)
        ue(alf_cc_cr_filters_signalled_minus1, 0, 3);
    else
        infer(alf_cc_cr_filters_signalled_minus1, 0);
    for (k = 0; k < current->alf_cc_cr_filters_signalled_minus1 + 1; k++) {
        for (j = 0; j < 7; j++) {
            if (current->alf_cc_cr_filter_signal_flag)
                ubs(3, alf_cc_cr_mapped_coeff_abs[k][j], 2, k, j);
            else
                infer(alf_cc_cr_mapped_coeff_abs[k][j], 0);
            if (current->alf_cc_cr_mapped_coeff_abs[k][j])
                ubs(1, alf_cc_cr_coeff_sign[k][j], 2, k, j);
            else
                infer(alf_cc_cr_coeff_sign[k][j], 0);
        }
    }

    return 0;
}

static int FUNC(lmcs_data)(CodedBitstreamContext *ctx, RWContext *rw,
                           H266RawAPS *current)
{
    int err, i, lmcs_max_bin_idx;

    ue(lmcs_min_bin_idx, 0, 15);
    ue(lmcs_delta_max_bin_idx, 0, 15);
    ue(lmcs_delta_cw_prec_minus1, 0, 14);

    lmcs_max_bin_idx = 15 - current->lmcs_delta_max_bin_idx;

    if (lmcs_max_bin_idx < current->lmcs_min_bin_idx)
        return AVERROR_INVALIDDATA;

    for (i = current->lmcs_min_bin_idx; i <= lmcs_max_bin_idx; i++) {
        ubs(current->lmcs_delta_cw_prec_minus1 + 1, lmcs_delta_abs_cw[i], 1, i);
        if (current->lmcs_delta_abs_cw[i] > 0)
            flags(lmcs_delta_sign_cw_flag[i], 1, i);
        else
            infer(lmcs_delta_sign_cw_flag[i], 0);
    }

    if (current->aps_chroma_present_flag) {
        ub(3, lmcs_delta_abs_crs);
        if (current->lmcs_delta_abs_crs > 0)
            flag(lmcs_delta_sign_crs_flag);
        else
            infer(lmcs_delta_sign_crs_flag, 0);
    } else {
        infer(lmcs_delta_abs_crs, 0);
        infer(lmcs_delta_sign_crs_flag, 0);
    }

    return 0;
}

static int FUNC(scaling_list_data)(CodedBitstreamContext *ctx, RWContext *rw,
                                   H266RawAPS *current)
{
    // 7.4.3.4, deriving DiagScanOrder
    static const uint8_t diag_scan_order[64][2] = {
        { 0,  0, }, { 0,  1, }, { 1,  0, }, { 0,  2, }, { 1,  1, }, { 2,  0, }, { 0,  3, }, { 1,  2, },
        { 2,  1, }, { 3,  0, }, { 0,  4, }, { 1,  3, }, { 2,  2, }, { 3,  1, }, { 4,  0, }, { 0,  5, },
        { 1,  4, }, { 2,  3, }, { 3,  2, }, { 4,  1, }, { 5,  0, }, { 0,  6, }, { 1,  5, }, { 2,  4, },
        { 3,  3, }, { 4,  2, }, { 5,  1, }, { 6,  0, }, { 0,  7, }, { 1,  6, }, { 2,  5, }, { 3,  4, },
        { 4,  3, }, { 5,  2, }, { 6,  1, }, { 7,  0, }, { 1,  7, }, { 2,  6, }, { 3,  5, }, { 4,  4, },
        { 5,  3, }, { 6,  2, }, { 7,  1, }, { 2,  7, }, { 3,  6, }, { 4,  5, }, { 5,  4, }, { 6,  3, },
        { 7,  2, }, { 3,  7, }, { 4,  6, }, { 5,  5, }, { 6,  4, }, { 7,  3, }, { 4,  7, }, { 5,  6, },
        { 6,  5, }, { 7,  4, }, { 5,  7, }, { 6,  6, }, { 7,  5, }, { 6,  7, }, { 7,  6, }, { 7,  7, }, };
    int err;

    for (int id = 0; id < 28; id ++) {
        if (current->aps_chroma_present_flag || id % 3 == 2 || id == 27) {
            flags(scaling_list_copy_mode_flag[id], 1, id);
            if (!current->scaling_list_copy_mode_flag[id])
                flags(scaling_list_pred_mode_flag[id], 1, id);
            else
                infer(scaling_list_pred_mode_flag[id], 0);
            if ((current->scaling_list_copy_mode_flag[id] ||
                 current->scaling_list_pred_mode_flag[id]) &&
                 id != 0 && id != 2 && id != 8) {
                int max_id_delta = (id < 2) ? id : ((id < 8) ? (id - 2) : (id - 8));
                ues(scaling_list_pred_id_delta[id], 0, max_id_delta, 1, id);
            }
            if (!current->scaling_list_copy_mode_flag[id]) {
                int matrix_size = id < 2 ? 2 : (id < 8 ? 4 : 8);
                if (id > 13) {
                    int idx = id - 14;
                    ses(scaling_list_dc_coef[idx], -128, 127, 1, idx);
                }
                for (int i = 0; i < matrix_size * matrix_size; i++) {
                    int x = diag_scan_order[i][0];
                    int y = diag_scan_order[i][1];
                    if (!(id > 25 && x >= 4 && y >= 4))
                        ses(scaling_list_delta_coef[id][i], -128, 127, 2, id, i);
                }
            } else if (id > 13) {
                int idx = id - 14;
                infer(scaling_list_dc_coef[idx], 0);
            }
        } else {
            infer(scaling_list_copy_mode_flag[id], 1);
            infer(scaling_list_pred_mode_flag[id], 0);
        }
    }

    return 0;
}

static int FUNC(aps)(CodedBitstreamContext *ctx, RWContext *rw,
                     H266RawAPS *current, int prefix)
{
    int aps_id_max = MAX_UINT_BITS(5);
    int err;

    if (prefix)
        HEADER("Prefix Adaptation parameter set");
    else
        HEADER("Suffix Adaptation parameter set");

    CHECK(FUNC(nal_unit_header)(ctx, rw, &current->nal_unit_header,
                                prefix ? VVC_PREFIX_APS_NUT
                                       : VVC_SUFFIX_APS_NUT));

    ub(3, aps_params_type);
    if (current->aps_params_type == VVC_ASP_TYPE_ALF ||
        current->aps_params_type == VVC_ASP_TYPE_SCALING)
        aps_id_max = 7;
    else if (current->aps_params_type == VVC_ASP_TYPE_LMCS)
        aps_id_max = 3;
    u(5, aps_adaptation_parameter_set_id, 0, aps_id_max);
    flag(aps_chroma_present_flag);
    if (current->aps_params_type == VVC_ASP_TYPE_ALF)
        CHECK(FUNC(alf_data)(ctx, rw, current));
    else if(current->aps_params_type == VVC_ASP_TYPE_LMCS)
        CHECK(FUNC(lmcs_data)(ctx, rw, current));
    else if (current->aps_params_type == VVC_ASP_TYPE_SCALING)
        CHECK(FUNC(scaling_list_data)(ctx, rw, current));
    flag(aps_extension_flag);
    if (current->aps_extension_flag)
        CHECK(FUNC(extension_data) (ctx, rw, &current->extension_data));
    CHECK(FUNC(rbsp_trailing_bits) (ctx, rw));

    return 0;
}

static int FUNC(aud) (CodedBitstreamContext *ctx, RWContext *rw,
                     H266RawAUD *current)
{
    int err;

    HEADER("Access Unit Delimiter");

    CHECK(FUNC(nal_unit_header) (ctx, rw,
                                 &current->nal_unit_header, VVC_AUD_NUT));

    flag(aud_irap_or_gdr_flag);
    u(3, aud_pic_type, 0, 2);

    CHECK(FUNC(rbsp_trailing_bits) (ctx, rw));
    return 0;
}

static int FUNC(pred_weight_table) (CodedBitstreamContext *ctx, RWContext *rw,
                                    const H266RawSPS *sps,
                                    const H266RawPPS *pps,
                                    const H266RefPicLists *ref_lists,
                                    uint8_t num_ref_idx_active[2],
                                    H266RawPredWeightTable *current)
{
    int err, i, j;
    ue(luma_log2_weight_denom, 0, 7);
    if (sps->sps_chroma_format_idc != 0) {
        se(delta_chroma_log2_weight_denom,
           -current->luma_log2_weight_denom,
           7 - current->luma_log2_weight_denom);
    } else {
        infer(delta_chroma_log2_weight_denom, 0);
    }
    if (pps->pps_wp_info_in_ph_flag) {
        ue(num_l0_weights, 0,
           FFMIN(15, ref_lists->rpl_ref_list[0].num_ref_entries));
        infer(num_weights_l0, current->num_l0_weights);
    } else {
        infer(num_weights_l0, num_ref_idx_active[0]);
    }
    for (i = 0; i < current->num_weights_l0; i++) {
        flags(luma_weight_l0_flag[i], 1, i);
    }
    if (sps->sps_chroma_format_idc != 0) {
        for (i = 0; i < current->num_weights_l0; i++)
            flags(chroma_weight_l0_flag[i], 1, i);
    }
    for (i = 0; i < current->num_weights_l0; i++) {
        if (current->luma_weight_l0_flag[i]) {
            ses(delta_luma_weight_l0[i], -128, 127, 1, i);
            ses(luma_offset_l0[i], -128, 127, 1, i);
        } else {
            infer(delta_luma_weight_l0[i], 0);
            infer(luma_offset_l0[i], 0);
        }
        if (current->chroma_weight_l0_flag[i]) {
            for (j = 0; j < 2; j++) {
                ses(delta_chroma_weight_l0[i][j], -128, 127, 2, i, j);
                ses(delta_chroma_offset_l0[i][j], -4 * 128, 4 * 127, 2, i, j);
            }
        }
    }

    if (pps->pps_weighted_bipred_flag &&
        ref_lists->rpl_ref_list[1].num_ref_entries > 0) {
        if (pps->pps_wp_info_in_ph_flag) {
            ue(num_l1_weights, 0,
               FFMIN(15, ref_lists->rpl_ref_list[1].num_ref_entries));
            infer(num_weights_l1, current->num_l1_weights);
        } else {
            infer(num_weights_l1, num_ref_idx_active[1]);
        }
    } else {
        infer(num_weights_l1, 0);
    }

    for (i = 0; i < current->num_weights_l1; i++)
        flags(luma_weight_l1_flag[i], 1, i);
    if (sps->sps_chroma_format_idc != 0) {
        for (i = 0; i < current->num_weights_l1; i++)
            flags(chroma_weight_l1_flag[i], 1, i);
    }
    for (i = 0; i < current->num_weights_l1; i++) {
        if (current->luma_weight_l1_flag[i]) {
            ses(delta_luma_weight_l1[i], -128, 127, 1, i);
            ses(luma_offset_l1[i], -128, 127, 1, i);
        } else {
            infer(delta_luma_weight_l1[i], 0);
            infer(luma_offset_l1[i], 0);
        }
        if (current->chroma_weight_l1_flag[i]) {
            for (j = 0; j < 2; j++) {
                ses(delta_chroma_weight_l1[i][j], -128, 127, 2, i, j);
                ses(delta_chroma_offset_l1[i][j], -4 * 128, 4 * 127, 2, i, j);
            }
        }
    }
    return 0;
}

static int FUNC(picture_header) (CodedBitstreamContext *ctx, RWContext *rw,
                                 H266RawPictureHeader *current) {
    CodedBitstreamH266Context *h266 = ctx->priv_data;
    const H266RawVPS *vps;
    const H266RawSPS *sps;
    const H266RawPPS *pps;
    int err, i;
    unsigned int ctb_log2_size_y, min_cb_log2_size_y,
        min_qt_log2_size_intra_y, min_qt_log2_size_inter_y;
    uint8_t qp_bd_offset;

    flag(ph_gdr_or_irap_pic_flag);
    flag(ph_non_ref_pic_flag);
    if (current->ph_gdr_or_irap_pic_flag)
        flag(ph_gdr_pic_flag);
    else
        infer(ph_gdr_pic_flag, 0);
    flag(ph_inter_slice_allowed_flag);
    if (current->ph_inter_slice_allowed_flag)
        flag(ph_intra_slice_allowed_flag);
    else
        infer(ph_intra_slice_allowed_flag, 1);
    ue(ph_pic_parameter_set_id, 0, VVC_MAX_PPS_COUNT - 1);
    pps = h266->pps[current->ph_pic_parameter_set_id];
    if (!pps) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "PPS id %d not available.\n",
               current->ph_pic_parameter_set_id);
        return AVERROR_INVALIDDATA;
    }
    sps = h266->sps[pps->pps_seq_parameter_set_id];
    if (!sps) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "SPS id %d not available.\n",
               pps->pps_seq_parameter_set_id);
        return AVERROR_INVALIDDATA;
    }
    vps = h266->vps[sps->sps_video_parameter_set_id];
    if (!vps) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "VPS id %d not available.\n",
               sps->sps_video_parameter_set_id);
        return AVERROR_INVALIDDATA;
    }

    ub(sps->sps_log2_max_pic_order_cnt_lsb_minus4 + 4, ph_pic_order_cnt_lsb);
    if (current->ph_gdr_pic_flag)
        ue(ph_recovery_poc_cnt, 0,
           1 << (sps->sps_log2_max_pic_order_cnt_lsb_minus4 + 4));

    for (i = 0; i < sps->sps_num_extra_ph_bytes * 8; i++) {
        if (sps->sps_extra_ph_bit_present_flag[i])
            flags(ph_extra_bit[i], 1, i);
    }
    if (sps->sps_poc_msb_cycle_flag) {
        flag(ph_poc_msb_cycle_present_flag);
        if (current->ph_poc_msb_cycle_present_flag)
            ub(sps->sps_poc_msb_cycle_len_minus1 + 1, ph_poc_msb_cycle_val);
    }
    if (sps->sps_alf_enabled_flag && pps->pps_alf_info_in_ph_flag) {
        flag(ph_alf_enabled_flag);
        if (current->ph_alf_enabled_flag) {

            ub(3, ph_num_alf_aps_ids_luma);
            for (i = 0; i < current->ph_num_alf_aps_ids_luma; i++)
                ubs(3, ph_alf_aps_id_luma[i], 1, i);

            if (sps->sps_chroma_format_idc != 0) {
                flag(ph_alf_cb_enabled_flag);
                flag(ph_alf_cr_enabled_flag);
            } else {
                infer(ph_alf_cb_enabled_flag, 0);
                infer(ph_alf_cr_enabled_flag, 0);
            }

            if (current->ph_alf_cb_enabled_flag
                || current->ph_alf_cr_enabled_flag) {
                ub(3, ph_alf_aps_id_chroma);
            }

            if (sps->sps_ccalf_enabled_flag) {
                flag(ph_alf_cc_cb_enabled_flag);
                if (current->ph_alf_cc_cb_enabled_flag)
                    ub(3, ph_alf_cc_cb_aps_id);
                flag(ph_alf_cc_cr_enabled_flag);
                if (current->ph_alf_cc_cr_enabled_flag)
                    ub(3, ph_alf_cc_cr_aps_id);
            }
        }
    } else {
        infer(ph_alf_enabled_flag, 0);
    }
    if (sps->sps_lmcs_enabled_flag) {
        flag(ph_lmcs_enabled_flag);
        if (current->ph_lmcs_enabled_flag) {
            ub(2, ph_lmcs_aps_id);
            if (sps->sps_chroma_format_idc != 0)
                flag(ph_chroma_residual_scale_flag);
            else
                infer(ph_chroma_residual_scale_flag, 0);
        }
    } else {
        infer(ph_lmcs_enabled_flag, 0);
        infer(ph_chroma_residual_scale_flag, 0);
    }

    if (sps->sps_explicit_scaling_list_enabled_flag) {
        flag(ph_explicit_scaling_list_enabled_flag);
        if (current->ph_explicit_scaling_list_enabled_flag) {
            //todo: check the ph_scaling_list_aps_id range, when aps ready
            ub(3, ph_scaling_list_aps_id);
        }
    } else {
        infer(ph_explicit_scaling_list_enabled_flag, 0);
    }
    if (sps->sps_virtual_boundaries_enabled_flag &&
        !sps->sps_virtual_boundaries_present_flag) {
        flag(ph_virtual_boundaries_present_flag);
        if (current->ph_virtual_boundaries_present_flag) {
            ue(ph_num_ver_virtual_boundaries,
               0, pps->pps_pic_width_in_luma_samples <= 8 ? 0 : VVC_MAX_VBS);
            for (i = 0; i < current->ph_num_ver_virtual_boundaries; i++) {
                ues(ph_virtual_boundary_pos_x_minus1[i],
                    0, (pps->pps_pic_width_in_luma_samples + 7) / 8 - 2, 1, i);
            }
            ue(ph_num_hor_virtual_boundaries,
               0, pps->pps_pic_height_in_luma_samples <= 8 ? 0 : VVC_MAX_VBS);
            for (i = 0; i < current->ph_num_hor_virtual_boundaries; i++) {
                ues(ph_virtual_boundary_pos_y_minus1[i],
                    0, (pps->pps_pic_height_in_luma_samples + 7) / 8 - 2, 1, i);
            }
        } else {
            infer(ph_num_ver_virtual_boundaries, 0);
            infer(ph_num_hor_virtual_boundaries, 0);
        }
    }
    if (pps->pps_output_flag_present_flag && !current->ph_non_ref_pic_flag)
        flag(ph_pic_output_flag);
    else
        infer(ph_pic_output_flag, 1);
    if (pps->pps_rpl_info_in_ph_flag) {
        CHECK(FUNC(ref_pic_lists)
              (ctx, rw, sps, pps, &current->ph_ref_pic_lists));
    }
    if (sps->sps_partition_constraints_override_enabled_flag)
        flag(ph_partition_constraints_override_flag);
    else
        infer(ph_partition_constraints_override_flag, 0);

    ctb_log2_size_y = sps->sps_log2_ctu_size_minus5 + 5;
    min_cb_log2_size_y = sps->sps_log2_min_luma_coding_block_size_minus2 + 2;
    if (current->ph_intra_slice_allowed_flag) {
        if (current->ph_partition_constraints_override_flag) {
            ue(ph_log2_diff_min_qt_min_cb_intra_slice_luma,
               0, FFMIN(6, ctb_log2_size_y) - min_cb_log2_size_y);
            ue(ph_max_mtt_hierarchy_depth_intra_slice_luma,
               0, 2 * (ctb_log2_size_y - min_cb_log2_size_y));
            if (current->ph_max_mtt_hierarchy_depth_intra_slice_luma != 0) {
                min_qt_log2_size_intra_y =
                    current->ph_log2_diff_min_qt_min_cb_intra_slice_luma +
                    min_cb_log2_size_y;
                ue(ph_log2_diff_max_bt_min_qt_intra_slice_luma,
                   0, (sps->sps_qtbtt_dual_tree_intra_flag ?
                       FFMIN(6, ctb_log2_size_y) :
                       ctb_log2_size_y) - min_qt_log2_size_intra_y);
                ue(ph_log2_diff_max_tt_min_qt_intra_slice_luma,
                   0, FFMIN(6, ctb_log2_size_y) - min_qt_log2_size_intra_y);
            } else {
                infer(ph_log2_diff_max_bt_min_qt_intra_slice_luma,
                      sps->sps_log2_diff_max_bt_min_qt_intra_slice_luma);
                infer(ph_log2_diff_max_tt_min_qt_intra_slice_luma,
                      sps->sps_log2_diff_max_tt_min_qt_intra_slice_luma);
            }
            if (sps->sps_qtbtt_dual_tree_intra_flag) {
                ue(ph_log2_diff_min_qt_min_cb_intra_slice_chroma,
                   0, FFMIN(6, ctb_log2_size_y) - min_cb_log2_size_y);
                ue(ph_max_mtt_hierarchy_depth_intra_slice_chroma,
                   0, 2 * (ctb_log2_size_y - min_cb_log2_size_y));
                if (sps->sps_max_mtt_hierarchy_depth_intra_slice_chroma != 0) {
                    unsigned int min_qt_log2_size_intra_c =
                        sps->sps_log2_diff_min_qt_min_cb_intra_slice_chroma +
                        min_cb_log2_size_y;
                    ue(ph_log2_diff_max_bt_min_qt_intra_slice_chroma,
                       0, FFMIN(6, ctb_log2_size_y) - min_qt_log2_size_intra_c);
                    ue(ph_log2_diff_max_tt_min_qt_intra_slice_chroma,
                       0, FFMIN(6, ctb_log2_size_y) - min_qt_log2_size_intra_c);
                } else {
                    infer(ph_log2_diff_max_bt_min_qt_intra_slice_chroma,
                          sps->sps_log2_diff_max_bt_min_qt_intra_slice_chroma);
                    infer(ph_log2_diff_max_tt_min_qt_intra_slice_chroma,
                          sps->sps_log2_diff_max_tt_min_qt_intra_slice_chroma);
                }
            }
        } else {
            infer(ph_log2_diff_min_qt_min_cb_intra_slice_luma,
                  sps->sps_log2_diff_min_qt_min_cb_intra_slice_luma);
            infer(ph_max_mtt_hierarchy_depth_intra_slice_luma,
                  sps->sps_max_mtt_hierarchy_depth_intra_slice_luma);
            infer(ph_log2_diff_max_bt_min_qt_intra_slice_luma,
                  sps->sps_log2_diff_max_bt_min_qt_intra_slice_luma);
            infer(ph_log2_diff_max_tt_min_qt_intra_slice_luma,
                  sps->sps_log2_diff_max_tt_min_qt_intra_slice_luma);
            infer(ph_log2_diff_min_qt_min_cb_intra_slice_chroma,
                  sps->sps_log2_diff_min_qt_min_cb_intra_slice_chroma);
            infer(ph_max_mtt_hierarchy_depth_intra_slice_chroma,
                  sps->sps_max_mtt_hierarchy_depth_intra_slice_chroma);
            infer(ph_log2_diff_max_bt_min_qt_intra_slice_chroma,
                  sps->sps_log2_diff_max_bt_min_qt_intra_slice_chroma);
            infer(ph_log2_diff_max_tt_min_qt_intra_slice_chroma,
                  sps->sps_log2_diff_max_tt_min_qt_intra_slice_chroma);
        }

        min_qt_log2_size_intra_y =
            current->ph_log2_diff_min_qt_min_cb_intra_slice_luma +
            min_cb_log2_size_y;
        if (pps->pps_cu_qp_delta_enabled_flag)
            ue(ph_cu_qp_delta_subdiv_intra_slice, 0,
               2 * (ctb_log2_size_y - min_qt_log2_size_intra_y +
                    current->ph_max_mtt_hierarchy_depth_intra_slice_luma));
        else
            infer(ph_cu_qp_delta_subdiv_intra_slice, 0);

        if (pps->pps_cu_chroma_qp_offset_list_enabled_flag)
            ue(ph_cu_chroma_qp_offset_subdiv_intra_slice, 0,
               2 * (ctb_log2_size_y - min_qt_log2_size_intra_y +
                    current->ph_max_mtt_hierarchy_depth_intra_slice_luma));
        else
            infer(ph_cu_chroma_qp_offset_subdiv_intra_slice, 0);
    }
    if (current->ph_inter_slice_allowed_flag) {
        if (current->ph_partition_constraints_override_flag) {
            ue(ph_log2_diff_min_qt_min_cb_inter_slice,
               0, FFMIN(6, ctb_log2_size_y) - min_cb_log2_size_y);
            min_qt_log2_size_inter_y =
                current->ph_log2_diff_min_qt_min_cb_inter_slice +
                min_cb_log2_size_y;
            ue(ph_max_mtt_hierarchy_depth_inter_slice, 0,
               2 * (ctb_log2_size_y - min_cb_log2_size_y));
            if (current->ph_max_mtt_hierarchy_depth_inter_slice != 0) {
                ue(ph_log2_diff_max_bt_min_qt_inter_slice,
                   0, ctb_log2_size_y - min_qt_log2_size_inter_y);
                ue(ph_log2_diff_max_tt_min_qt_inter_slice,
                   0, FFMIN(6, ctb_log2_size_y) - min_qt_log2_size_inter_y);
            }
        } else {
            infer(ph_log2_diff_min_qt_min_cb_inter_slice,
                  sps->sps_log2_diff_min_qt_min_cb_inter_slice);
            min_qt_log2_size_inter_y =
                current->ph_log2_diff_min_qt_min_cb_inter_slice +
                min_cb_log2_size_y;
            infer(ph_max_mtt_hierarchy_depth_inter_slice,
                  sps->sps_max_mtt_hierarchy_depth_inter_slice);
            infer(ph_log2_diff_max_bt_min_qt_inter_slice,
                  sps->sps_log2_diff_max_bt_min_qt_inter_slice);
            infer(ph_log2_diff_max_tt_min_qt_inter_slice,
                  sps->sps_log2_diff_max_tt_min_qt_inter_slice);
        }

        if (pps->pps_cu_qp_delta_enabled_flag)
            ue(ph_cu_qp_delta_subdiv_inter_slice, 0,
               2 * (ctb_log2_size_y - min_qt_log2_size_inter_y +
                    current->ph_max_mtt_hierarchy_depth_inter_slice));
        else
            infer(ph_cu_qp_delta_subdiv_inter_slice, 0);

        if (pps->pps_cu_chroma_qp_offset_list_enabled_flag)
            ue(ph_cu_chroma_qp_offset_subdiv_inter_slice, 0,
               2 * (ctb_log2_size_y - min_qt_log2_size_inter_y +
                    current->ph_max_mtt_hierarchy_depth_inter_slice));
        else
            infer(ph_cu_chroma_qp_offset_subdiv_inter_slice, 0);
        if (sps->sps_temporal_mvp_enabled_flag) {
            flag(ph_temporal_mvp_enabled_flag);
            if (current->ph_temporal_mvp_enabled_flag &&
                pps->pps_rpl_info_in_ph_flag) {
                if (current->ph_ref_pic_lists.rpl_ref_list[1].num_ref_entries > 0)
                    flag(ph_collocated_from_l0_flag);
                else
                    infer(ph_collocated_from_l0_flag, 1);
                if ((current->ph_collocated_from_l0_flag &&
                     current->ph_ref_pic_lists.rpl_ref_list[0].num_ref_entries > 1)
                     || (!current->ph_collocated_from_l0_flag &&
                         current->ph_ref_pic_lists.rpl_ref_list[1].num_ref_entries > 1)) {
                    unsigned int idx =
                        current->ph_collocated_from_l0_flag ? 0 : 1;
                    ue(ph_collocated_ref_idx, 0,
                       current->ph_ref_pic_lists.rpl_ref_list[idx].
                       num_ref_entries - 1);
                } else {
                    infer(ph_collocated_ref_idx, 0);
                }
            }
        }
        if (sps->sps_mmvd_fullpel_only_enabled_flag)
            flag(ph_mmvd_fullpel_only_flag);
        else
            infer(ph_mmvd_fullpel_only_flag, 0);
        if (!pps->pps_rpl_info_in_ph_flag ||
            current->ph_ref_pic_lists.rpl_ref_list[1].num_ref_entries > 0) {
            flag(ph_mvd_l1_zero_flag);
            if (sps->sps_bdof_control_present_in_ph_flag) {
                flag(ph_bdof_disabled_flag);
            } else {
                if (!sps->sps_bdof_control_present_in_ph_flag)
                    infer(ph_bdof_disabled_flag,
                          1 - sps->sps_bdof_enabled_flag);
                else
                    infer(ph_bdof_disabled_flag, 1);
            }
            if (sps->sps_dmvr_control_present_in_ph_flag) {
                flag(ph_dmvr_disabled_flag);
            } else {
                if (!sps->sps_dmvr_control_present_in_ph_flag)
                    infer(ph_dmvr_disabled_flag,
                          1 - sps->sps_dmvr_enabled_flag);
                else
                    infer(ph_dmvr_disabled_flag, 1);
            }
        } else {
            infer(ph_mvd_l1_zero_flag, 1);
        }
        if (sps->sps_prof_control_present_in_ph_flag)
            flag(ph_prof_disabled_flag);
        else
            infer(ph_prof_disabled_flag, !sps->sps_affine_prof_enabled_flag);
        if ((pps->pps_weighted_pred_flag ||
             pps->pps_weighted_bipred_flag) && pps->pps_wp_info_in_ph_flag) {

            // if pps->pps_wp_info_in_ph_fla == 1
            // pred_weight_table will not use num_ref_idx_active
            uint8_t num_ref_idx_active[2] = { 0, 0 };
            CHECK(FUNC(pred_weight_table)
                  (ctx, rw, sps, pps, &current->ph_ref_pic_lists,
                   num_ref_idx_active, &current->ph_pred_weight_table));
        }
    }

    qp_bd_offset = 6 * sps->sps_bitdepth_minus8;
    if (pps->pps_qp_delta_info_in_ph_flag)
        se(ph_qp_delta, -qp_bd_offset - (26 + pps->pps_init_qp_minus26),
           63 - (26 + pps->pps_init_qp_minus26));

    if (sps->sps_joint_cbcr_enabled_flag)
        flag(ph_joint_cbcr_sign_flag);
    else
        infer(ph_joint_cbcr_sign_flag, 0);
    if (sps->sps_sao_enabled_flag && pps->pps_sao_info_in_ph_flag) {
        flag(ph_sao_luma_enabled_flag);
        if (sps->sps_chroma_format_idc != 0)
            flag(ph_sao_chroma_enabled_flag);
        else
            infer(ph_sao_chroma_enabled_flag, 0);
    } else {
        infer(ph_sao_luma_enabled_flag, 0);
        infer(ph_sao_chroma_enabled_flag, 0);
    }

    if (pps->pps_dbf_info_in_ph_flag)
        flag(ph_deblocking_params_present_flag);
    else
        infer(ph_deblocking_params_present_flag, 0);

    if (current->ph_deblocking_params_present_flag) {
        if (!pps->pps_deblocking_filter_disabled_flag) {
            flag(ph_deblocking_filter_disabled_flag);
            if (!current->ph_deblocking_filter_disabled_flag) {
                se(ph_luma_beta_offset_div2, -12, 12);
                se(ph_luma_tc_offset_div2, -12, 12);
                if (pps->pps_chroma_tool_offsets_present_flag) {
                    se(ph_cb_beta_offset_div2, -12, 12);
                    se(ph_cb_tc_offset_div2, -12, 12);
                    se(ph_cr_beta_offset_div2, -12, 12);
                    se(ph_cr_tc_offset_div2, -12, 12);
                } else {
                    infer(ph_cb_beta_offset_div2,
                          current->ph_luma_beta_offset_div2);
                    infer(ph_cb_tc_offset_div2,
                          current->ph_luma_tc_offset_div2);
                    infer(ph_cr_beta_offset_div2,
                          current->ph_luma_beta_offset_div2);
                    infer(ph_cr_tc_offset_div2,
                          current->ph_luma_tc_offset_div2);
                }
            }
        } else {
            infer(ph_deblocking_filter_disabled_flag, 0);
        }
    } else {
        infer(ph_deblocking_filter_disabled_flag, pps->pps_deblocking_filter_disabled_flag);
        if (!current->ph_deblocking_filter_disabled_flag) {
            infer(ph_luma_beta_offset_div2, pps->pps_luma_beta_offset_div2);
            infer(ph_luma_tc_offset_div2, pps->pps_luma_tc_offset_div2);
            infer(ph_cb_beta_offset_div2, pps->pps_cb_beta_offset_div2);
            infer(ph_cb_tc_offset_div2, pps->pps_cb_tc_offset_div2);
            infer(ph_cr_beta_offset_div2, pps->pps_cr_beta_offset_div2);
            infer(ph_cr_tc_offset_div2, pps->pps_cr_tc_offset_div2);
        }
    }

    if (pps->pps_picture_header_extension_present_flag) {
        ue(ph_extension_length, 0, 256);
        for (i = 0; i < current->ph_extension_length; i++)
            us(8, ph_extension_data_byte[i], 0x00, 0xff, 1, i);
    }

    return 0;
}

static int FUNC(ph) (CodedBitstreamContext *ctx, RWContext *rw,
                     H266RawPH *current)
{
    int err;

    HEADER("Picture Header");

    CHECK(FUNC(nal_unit_header) (ctx, rw, &current->nal_unit_header, VVC_PH_NUT));
    CHECK(FUNC(picture_header) (ctx, rw, &current->ph_picture_header));
    CHECK(FUNC(rbsp_trailing_bits) (ctx, rw));
    return 0;
}

static int FUNC(slice_header) (CodedBitstreamContext *ctx, RWContext *rw,
                               H266RawSliceHeader *current)
{
    CodedBitstreamH266Context *h266 = ctx->priv_data;
    const H266RawSPS *sps;
    const H266RawPPS *pps;
    const H266RawPictureHeader *ph;
    const H266RefPicLists *ref_pic_lists;
    int err, i;
    uint8_t nal_unit_type, qp_bd_offset;
    uint16_t num_slices_in_subpic;

    HEADER("Slice Header");

    CHECK(FUNC(nal_unit_header) (ctx, rw, &current->nal_unit_header, -1));

    flag(sh_picture_header_in_slice_header_flag);
    if (current->sh_picture_header_in_slice_header_flag) {
        // 7.4.8 if sh_picture_header_in_slice_header_flag is true, we do not have a PH NAL unit
        CHECK(FUNC(picture_header) (ctx, rw, &current->sh_picture_header));
        ph = &current->sh_picture_header;
    } else {
        ph = h266->ph;
        if (!ph) {
            av_log(ctx->log_ctx, AV_LOG_ERROR,
                   "Picture header not available.\n");
            return AVERROR_INVALIDDATA;
        }
    }

    pps = h266->pps[ph->ph_pic_parameter_set_id];
    if (!pps) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "PPS id %d not available.\n",
               ph->ph_pic_parameter_set_id);
        return AVERROR_INVALIDDATA;
    }
    sps = h266->sps[pps->pps_seq_parameter_set_id];
    if (!sps) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "SPS id %d not available.\n",
               pps->pps_seq_parameter_set_id);
        return AVERROR_INVALIDDATA;
    }

    if (sps->sps_subpic_info_present_flag) {
        ub(sps->sps_subpic_id_len_minus1 + 1, sh_subpic_id);
        for (i = 0; i <= sps->sps_num_subpics_minus1; i++) {
            if (pps->sub_pic_id_val[i] == current->sh_subpic_id) {
                current->curr_subpic_idx = i;
                break;
            }
        }
        if (i > sps->sps_num_subpics_minus1) {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "invalid CurrSubpicIdx %d\n", i);
            return AVERROR_INVALIDDATA;
        }
    } else {
        current->curr_subpic_idx = 0;
    }

    num_slices_in_subpic = pps->num_slices_in_subpic[current->curr_subpic_idx];

    if ((pps->pps_rect_slice_flag && num_slices_in_subpic > 1) ||
        (!pps->pps_rect_slice_flag && pps->num_tiles_in_pic > 1)) {
        unsigned int bits, max;
        if (!pps->pps_rect_slice_flag) {
            bits = av_ceil_log2(pps->num_tiles_in_pic);
            max = pps->num_tiles_in_pic - 1;
        } else {
            bits = av_ceil_log2(num_slices_in_subpic);
            max = num_slices_in_subpic - 1;
        }
        u(bits, sh_slice_address, 0, max);
    } else {
        infer(sh_slice_address, 0);
    }

    for (i = 0; i < sps->sps_num_extra_sh_bytes * 8; i++) {
        if (sps->sps_extra_sh_bit_present_flag[i])
            flags(sh_extra_bit[i], 1, i);
    }

    if (!pps->pps_rect_slice_flag &&
        pps->num_tiles_in_pic - current->sh_slice_address > 1)
        ue(sh_num_tiles_in_slice_minus1, 0, pps->num_tiles_in_pic - 1);
    else
        infer(sh_num_tiles_in_slice_minus1, 0);

    if (ph->ph_inter_slice_allowed_flag)
        ue(sh_slice_type, 0, 2);
    else
        infer(sh_slice_type, 2);

    nal_unit_type = current->nal_unit_header.nal_unit_type;
    if (nal_unit_type == VVC_IDR_W_RADL || nal_unit_type == VVC_IDR_N_LP ||
        nal_unit_type == VVC_CRA_NUT || nal_unit_type == VVC_GDR_NUT)
        flag(sh_no_output_of_prior_pics_flag);

    if (sps->sps_alf_enabled_flag) {
        if (!pps->pps_alf_info_in_ph_flag) {
            flag(sh_alf_enabled_flag);
            if (current->sh_alf_enabled_flag) {
                ub(3, sh_num_alf_aps_ids_luma);
                for (i = 0; i < current->sh_num_alf_aps_ids_luma; i++)
                    ubs(3, sh_alf_aps_id_luma[i], 1, i);

                if (sps->sps_chroma_format_idc != 0) {
                    flag(sh_alf_cb_enabled_flag);
                    flag(sh_alf_cr_enabled_flag);
                }
                if (current->sh_alf_cb_enabled_flag ||
                    current->sh_alf_cr_enabled_flag) {
                    ub(3, sh_alf_aps_id_chroma);
                }

                if (sps->sps_ccalf_enabled_flag) {
                    flag(sh_alf_cc_cb_enabled_flag);
                    if (current->sh_alf_cc_cb_enabled_flag)
                        ub(3, sh_alf_cc_cb_aps_id);

                    flag(sh_alf_cc_cr_enabled_flag);
                    if (current->sh_alf_cc_cr_enabled_flag)
                        ub(3, sh_alf_cc_cr_aps_id);
                }
            }
        } else {
            infer(sh_alf_enabled_flag, ph->ph_alf_enabled_flag);
            if (current->sh_alf_enabled_flag) {
                infer(sh_num_alf_aps_ids_luma, ph->ph_num_alf_aps_ids_luma);
                for (i = 0; i < current->sh_num_alf_aps_ids_luma; i++)
                    infer(sh_alf_aps_id_luma[i], ph->ph_alf_aps_id_luma[i]);

                infer(sh_alf_cb_enabled_flag, ph->ph_alf_cb_enabled_flag);
                infer(sh_alf_cr_enabled_flag, ph->ph_alf_cr_enabled_flag);
                if (current->sh_alf_cb_enabled_flag ||current->sh_alf_cr_enabled_flag)
                    infer(sh_alf_aps_id_chroma, ph->ph_alf_aps_id_chroma);

                if (sps->sps_ccalf_enabled_flag) {
                    infer(sh_alf_cc_cb_enabled_flag, ph->ph_alf_cc_cb_enabled_flag);
                    if (current->sh_alf_cc_cb_enabled_flag)
                        infer(sh_alf_cc_cb_aps_id, ph->ph_alf_cc_cb_aps_id);

                    infer(sh_alf_cc_cr_enabled_flag, ph->ph_alf_cc_cr_enabled_flag);
                    if (current->sh_alf_cc_cr_enabled_flag)
                        infer(sh_alf_cc_cr_aps_id, ph->ph_alf_cc_cr_aps_id);
                }
            }
        }
    }

    if (current->sh_picture_header_in_slice_header_flag) {
        infer(sh_lmcs_used_flag, ph->ph_lmcs_enabled_flag);
        infer(sh_explicit_scaling_list_used_flag,
            ph->ph_explicit_scaling_list_enabled_flag);
    } else {
        if (ph->ph_lmcs_enabled_flag)
            flag(sh_lmcs_used_flag);
        else
            infer(sh_lmcs_used_flag, 0);

        if (ph->ph_explicit_scaling_list_enabled_flag)
            flag(sh_explicit_scaling_list_used_flag);
        else
            infer(sh_explicit_scaling_list_used_flag, 0);
    }

    if (!pps->pps_rpl_info_in_ph_flag &&
        ((nal_unit_type != VVC_IDR_W_RADL &&
          nal_unit_type != VVC_IDR_N_LP) || sps->sps_idr_rpl_present_flag)) {
        CHECK(FUNC(ref_pic_lists)
              (ctx, rw, sps, pps, &current->sh_ref_pic_lists));
        ref_pic_lists = &current->sh_ref_pic_lists;
    } else {
        ref_pic_lists = &ph->ph_ref_pic_lists;
    }
    if ((current->sh_slice_type != VVC_SLICE_TYPE_I &&
         ref_pic_lists->rpl_ref_list[0].num_ref_entries > 1) ||
        (current->sh_slice_type == VVC_SLICE_TYPE_B &&
         ref_pic_lists->rpl_ref_list[1].num_ref_entries > 1)) {
        flag(sh_num_ref_idx_active_override_flag);
        if (current->sh_num_ref_idx_active_override_flag) {
            for (i = 0;
                 i < (current->sh_slice_type == VVC_SLICE_TYPE_B ? 2 : 1); i++)
                if (ref_pic_lists->rpl_ref_list[i].num_ref_entries > 1)
                    ues(sh_num_ref_idx_active_minus1[i], 0, 14, 1, i);
                else
                    infer(sh_num_ref_idx_active_minus1[i], 0);
        }
    } else {
        infer(sh_num_ref_idx_active_override_flag, 1);
    }

    for (i = 0; i < 2; i++) {
        if (current->sh_slice_type == VVC_SLICE_TYPE_B ||
            (current->sh_slice_type == VVC_SLICE_TYPE_P && i == 0)) {
            if (current->sh_num_ref_idx_active_override_flag) {
                current->num_ref_idx_active[i] = current->sh_num_ref_idx_active_minus1[i] + 1;
            } else {
                current->num_ref_idx_active[i] =
                    FFMIN(ref_pic_lists->rpl_ref_list[i].num_ref_entries,
                        pps->pps_num_ref_idx_default_active_minus1[i] + 1);
            }
        } else {
            current->num_ref_idx_active[i] = 0;
        }
    }

    if (current->sh_slice_type != VVC_SLICE_TYPE_I) {
        if (pps->pps_cabac_init_present_flag)
            flag(sh_cabac_init_flag);
        else
            infer(sh_cabac_init_flag, 0);
        if (ph->ph_temporal_mvp_enabled_flag) {
            if (!pps->pps_rpl_info_in_ph_flag) {
                if (current->sh_slice_type == VVC_SLICE_TYPE_B)
                    flag(sh_collocated_from_l0_flag);
                else
                    infer(sh_collocated_from_l0_flag, 1);
                if ((current->sh_collocated_from_l0_flag &&
                    current->num_ref_idx_active[0] > 1) ||
                    (!current->sh_collocated_from_l0_flag &&
                    current->num_ref_idx_active[1] > 1)) {
                    unsigned int idx = current->sh_collocated_from_l0_flag ? 0 : 1;
                    ue(sh_collocated_ref_idx, 0, current->num_ref_idx_active[idx] - 1);
                } else {
                    infer(sh_collocated_ref_idx, 0);
                }
            } else {
                if (current->sh_slice_type == VVC_SLICE_TYPE_B)
                    infer(sh_collocated_from_l0_flag, ph->ph_collocated_from_l0_flag);
                else
                    infer(sh_collocated_from_l0_flag, 1);
                infer(sh_collocated_ref_idx, ph->ph_collocated_ref_idx);
            }
        }
        if (!pps->pps_wp_info_in_ph_flag &&
            ((pps->pps_weighted_pred_flag &&
            current->sh_slice_type == VVC_SLICE_TYPE_P) ||
            (pps->pps_weighted_bipred_flag &&
            current->sh_slice_type == VVC_SLICE_TYPE_B))) {
            CHECK(FUNC(pred_weight_table) (ctx, rw, sps, pps, ref_pic_lists,
                                           current->num_ref_idx_active,
                                           &current->sh_pred_weight_table));
        }
    }
    qp_bd_offset = 6 * sps->sps_bitdepth_minus8;
    if (!pps->pps_qp_delta_info_in_ph_flag)
        se(sh_qp_delta, -qp_bd_offset - (26 + pps->pps_init_qp_minus26),
           63 - (26 + pps->pps_init_qp_minus26));
    if (pps->pps_slice_chroma_qp_offsets_present_flag) {
        int8_t off;

        se(sh_cb_qp_offset, -12, 12);
        off = pps->pps_cb_qp_offset + current->sh_cb_qp_offset;
        if (off < -12 || off > 12) {
            av_log(ctx->log_ctx, AV_LOG_ERROR,
                   "pps_cb_qp_offset + sh_cb_qp_offset (%d) not in range [-12, 12].\n",
                   off);
            return AVERROR_INVALIDDATA;
        }

        se(sh_cr_qp_offset, -12, 12);
        off = pps->pps_cr_qp_offset + current->sh_cr_qp_offset;
        if (off < -12 || off > 12) {
            av_log(ctx->log_ctx, AV_LOG_ERROR,
                   "pps_cr_qp_offset + sh_cr_qp_offset (%d) not in range [-12, 12].\n",
                   off);
            return AVERROR_INVALIDDATA;
        }

        if (sps->sps_joint_cbcr_enabled_flag) {
            se(sh_joint_cbcr_qp_offset, -12, 12);
            off =
                pps->pps_joint_cbcr_qp_offset_value +
                current->sh_joint_cbcr_qp_offset;
            if (off < -12 || off > 12) {
                av_log(ctx->log_ctx, AV_LOG_ERROR,
                       "pps_joint_cbcr_qp_offset_value + sh_joint_cbcr_qp_offset (%d)"
                       "not in range [-12, 12]. \n", off);
                return AVERROR_INVALIDDATA;
            }
        } else {
            infer(sh_joint_cbcr_qp_offset, 0);
        }
    } else {
        infer(sh_cb_qp_offset, 0);
        infer(sh_cr_qp_offset, 0);
        infer(sh_joint_cbcr_qp_offset, 0);
    }
    if (pps->pps_cu_chroma_qp_offset_list_enabled_flag)
        flag(sh_cu_chroma_qp_offset_enabled_flag);
    else
        infer(sh_cu_chroma_qp_offset_enabled_flag, 0);
    if (sps->sps_sao_enabled_flag && !pps->pps_sao_info_in_ph_flag) {
        flag(sh_sao_luma_used_flag);
        if (sps->sps_chroma_format_idc != 0)
            flag(sh_sao_chroma_used_flag);
        else
            infer(sh_sao_chroma_used_flag, ph->ph_sao_chroma_enabled_flag);
    } else {
        infer(sh_sao_luma_used_flag, ph->ph_sao_luma_enabled_flag);
        infer(sh_sao_chroma_used_flag, ph->ph_sao_chroma_enabled_flag);
    }

    if (pps->pps_deblocking_filter_override_enabled_flag &&
        !pps->pps_dbf_info_in_ph_flag)
        flag(sh_deblocking_params_present_flag);
    else
        infer(sh_deblocking_params_present_flag, 0);
    if (current->sh_deblocking_params_present_flag) {
        if (!pps->pps_deblocking_filter_disabled_flag)
            flag(sh_deblocking_filter_disabled_flag);
        else
            infer(sh_deblocking_filter_disabled_flag, 0);
        if (!current->sh_deblocking_filter_disabled_flag) {
            se(sh_luma_beta_offset_div2, -12, 12);
            se(sh_luma_tc_offset_div2, -12, 12);
            if (pps->pps_chroma_tool_offsets_present_flag) {
                se(sh_cb_beta_offset_div2, -12, 12);
                se(sh_cb_tc_offset_div2, -12, 12);
                se(sh_cr_beta_offset_div2, -12, 12);
                se(sh_cr_tc_offset_div2, -12, 12);
            } else {
                infer(sh_cb_beta_offset_div2,
                      current->sh_luma_beta_offset_div2);
                infer(sh_cb_tc_offset_div2, current->sh_luma_tc_offset_div2);
                infer(sh_cr_beta_offset_div2,
                      current->sh_luma_beta_offset_div2);
                infer(sh_cr_tc_offset_div2, current->sh_luma_tc_offset_div2);
            }
        }
    } else {
        infer(sh_deblocking_filter_disabled_flag, ph->ph_deblocking_filter_disabled_flag);
        if (!current->sh_deblocking_filter_disabled_flag) {
            infer(sh_luma_beta_offset_div2, ph->ph_luma_beta_offset_div2);
            infer(sh_luma_tc_offset_div2, ph->ph_luma_tc_offset_div2);
            infer(sh_cb_beta_offset_div2, ph->ph_cb_beta_offset_div2);
            infer(sh_cb_tc_offset_div2, ph->ph_cb_tc_offset_div2);
            infer(sh_cr_beta_offset_div2, ph->ph_cr_beta_offset_div2);
            infer(sh_cr_tc_offset_div2, ph->ph_cr_tc_offset_div2);
        }
    }

    if (sps->sps_dep_quant_enabled_flag)
        flag(sh_dep_quant_used_flag);
    else
        infer(sh_dep_quant_used_flag, 0);

    if (sps->sps_sign_data_hiding_enabled_flag &&
        !current->sh_dep_quant_used_flag)
        flag(sh_sign_data_hiding_used_flag);
    else
        infer(sh_sign_data_hiding_used_flag, 0);

    if (sps->sps_transform_skip_enabled_flag &&
        !current->sh_dep_quant_used_flag &&
        !current->sh_sign_data_hiding_used_flag)
        flag(sh_ts_residual_coding_disabled_flag);
    else
        infer(sh_ts_residual_coding_disabled_flag, 0);

    if (!current->sh_ts_residual_coding_disabled_flag &&
        sps->sps_ts_residual_coding_rice_present_in_sh_flag)
        ub(3, sh_ts_residual_coding_rice_idx_minus1);
    else
        infer(sh_ts_residual_coding_rice_idx_minus1, 0);

    if (sps->sps_reverse_last_sig_coeff_enabled_flag)
        flag(sh_reverse_last_sig_coeff_flag);
    else
        infer(sh_reverse_last_sig_coeff_flag, 0);

    if (pps->pps_slice_header_extension_present_flag) {
        ue(sh_slice_header_extension_length, 0, 256);
        for (i = 0; i < current->sh_slice_header_extension_length; i++)
            us(8, sh_slice_header_extension_data_byte[i], 0x00, 0xff, 1, i);
    }

    current->num_entry_points = 0;
    if (sps->sps_entry_point_offsets_present_flag) {
        uint8_t entropy_sync = sps->sps_entropy_coding_sync_enabled_flag;
        int height;
        if (pps->pps_rect_slice_flag) {
            int width_in_tiles;
            int slice_idx = current->sh_slice_address;
            for (i = 0; i < current->curr_subpic_idx; i++) {
                slice_idx += pps->num_slices_in_subpic[i];
            }
            width_in_tiles =
                pps->pps_slice_width_in_tiles_minus1[slice_idx] + 1;

            if (entropy_sync)
                height = pps->slice_height_in_ctus[slice_idx];
            else
                height = pps->pps_slice_height_in_tiles_minus1[slice_idx] + 1;

            current->num_entry_points = width_in_tiles * height;
        } else {
            int tile_idx;
            int tile_y;
            for (tile_idx = current->sh_slice_address;
                 tile_idx <=
                 current->sh_slice_address +
                 current->sh_num_tiles_in_slice_minus1; tile_idx++) {
                tile_y = tile_idx / pps->num_tile_rows;
                height = pps->row_height_val[tile_y];
                current->num_entry_points += (entropy_sync ? height : 1);
            }
        }
        current->num_entry_points--;
        if (current->num_entry_points > VVC_MAX_ENTRY_POINTS) {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "Too many entry points: "
                   "%" PRIu32 ".\n", current->num_entry_points);
            return AVERROR_PATCHWELCOME;
        }
        if (current->num_entry_points > 0) {
            ue(sh_entry_offset_len_minus1, 0, 31);
            for (i = 0; i < current->num_entry_points; i++) {
                ubs(current->sh_entry_offset_len_minus1 + 1,
                    sh_entry_point_offset_minus1[i], 1, i);
            }
        }
    }
    CHECK(FUNC(byte_alignment) (ctx, rw));

    return 0;
}

static int FUNC(sei) (CodedBitstreamContext *ctx, RWContext *rw,
                      H266RawSEI *current, int prefix)
{
    int err;

    if (prefix)
        HEADER("Prefix Supplemental Enhancement Information");
    else
        HEADER("Suffix Supplemental Enhancement Information");

    CHECK(FUNC(nal_unit_header) (ctx, rw, &current->nal_unit_header,
                                 prefix ? VVC_PREFIX_SEI_NUT
                                 : VVC_SUFFIX_SEI_NUT));

    CHECK(FUNC_SEI(message_list) (ctx, rw, &current->message_list, prefix));

    CHECK(FUNC(rbsp_trailing_bits) (ctx, rw));

    return 0;
}
