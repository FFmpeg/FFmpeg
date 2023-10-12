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

#include "get_bits.h"
#include "golomb.h"
#include "evc.h"
#include "evc_ps.h"

#define EXTENDED_SAR 255

// @see ISO_IEC_23094-1 (7.3.7 Reference picture list structure syntax)
static int ref_pic_list_struct(const EVCParserSPS *sps, GetBitContext *gb, RefPicListStruct *rpl)
{
    uint32_t delta_poc_st, strp_entry_sign_flag = 0;
    rpl->ref_pic_num = get_ue_golomb_long(gb);

    if ((unsigned)rpl->ref_pic_num  > sps->sps_max_dec_pic_buffering_minus1)
        return AVERROR_INVALIDDATA;

    if (rpl->ref_pic_num > 0) {
        delta_poc_st = get_ue_golomb_long(gb);

        rpl->ref_pics[0] = delta_poc_st;
        if (rpl->ref_pics[0] != 0) {
            strp_entry_sign_flag = get_bits(gb, 1);

            rpl->ref_pics[0] *= 1 - (strp_entry_sign_flag << 1);
        }
    }

    for (int i = 1; i < rpl->ref_pic_num; ++i) {
        delta_poc_st = get_ue_golomb_long(gb);
        if (delta_poc_st != 0)
            strp_entry_sign_flag = get_bits(gb, 1);
        rpl->ref_pics[i] = rpl->ref_pics[i - 1] + delta_poc_st * (1 - (strp_entry_sign_flag << 1));
    }

    return 0;
}

// @see  ISO_IEC_23094-1 (E.2.2 HRD parameters syntax)
static int hrd_parameters(GetBitContext *gb, HRDParameters *hrd)
{
    hrd->cpb_cnt_minus1 = get_ue_golomb_31(gb);
    if (hrd->cpb_cnt_minus1 >= FF_ARRAY_ELEMS(hrd->cpb_size_value_minus1))
        return AVERROR_INVALIDDATA;

    hrd->bit_rate_scale = get_bits(gb, 4);
    hrd->cpb_size_scale = get_bits(gb, 4);
    for (int SchedSelIdx = 0; SchedSelIdx <= hrd->cpb_cnt_minus1; SchedSelIdx++) {
        hrd->bit_rate_value_minus1[SchedSelIdx] = get_ue_golomb_long(gb);
        hrd->cpb_size_value_minus1[SchedSelIdx] = get_ue_golomb_long(gb);
        hrd->cbr_flag[SchedSelIdx] = get_bits(gb, 1);
    }
    hrd->initial_cpb_removal_delay_length_minus1 = get_bits(gb, 5);
    hrd->cpb_removal_delay_length_minus1 = get_bits(gb, 5);
    hrd->cpb_removal_delay_length_minus1 = get_bits(gb, 5);
    hrd->time_offset_length = get_bits(gb, 5);

    return 0;
}

// @see  ISO_IEC_23094-1 (E.2.1 VUI parameters syntax)
static int vui_parameters(GetBitContext *gb, VUIParameters *vui)
{
    int ret;

    vui->aspect_ratio_info_present_flag = get_bits(gb, 1);
    if (vui->aspect_ratio_info_present_flag) {
        vui->aspect_ratio_idc = get_bits(gb, 8);
        if (vui->aspect_ratio_idc == EXTENDED_SAR) {
            vui->sar_width = get_bits(gb, 16);
            vui->sar_height = get_bits(gb, 16);
        }
    }
    vui->overscan_info_present_flag = get_bits(gb, 1);
    if (vui->overscan_info_present_flag)
        vui->overscan_appropriate_flag = get_bits(gb, 1);
    vui->video_signal_type_present_flag = get_bits(gb, 1);
    if (vui->video_signal_type_present_flag) {
        vui->video_format = get_bits(gb, 3);
        vui->video_full_range_flag = get_bits(gb, 1);
        vui->colour_description_present_flag = get_bits(gb, 1);
        if (vui->colour_description_present_flag) {
            vui->colour_primaries = get_bits(gb, 8);
            vui->transfer_characteristics = get_bits(gb, 8);
            vui->matrix_coefficients = get_bits(gb, 8);
        }
    }
    vui->chroma_loc_info_present_flag = get_bits(gb, 1);
    if (vui->chroma_loc_info_present_flag) {
        vui->chroma_sample_loc_type_top_field = get_ue_golomb_31(gb);
        vui->chroma_sample_loc_type_bottom_field = get_ue_golomb_31(gb);
    }
    vui->neutral_chroma_indication_flag = get_bits(gb, 1);

    vui->field_seq_flag = get_bits(gb, 1);

    vui->timing_info_present_flag = get_bits(gb, 1);
    if (vui->timing_info_present_flag) {
        vui->num_units_in_tick = get_bits_long(gb, 32);
        vui->time_scale = get_bits_long(gb, 32);
        vui->fixed_pic_rate_flag = get_bits(gb, 1);
    }
    vui->nal_hrd_parameters_present_flag = get_bits(gb, 1);
    if (vui->nal_hrd_parameters_present_flag) {
        ret = hrd_parameters(gb, &vui->hrd_parameters);
        if (ret < 0)
            return ret;
    }

    vui->vcl_hrd_parameters_present_flag = get_bits(gb, 1);
    if (vui->vcl_hrd_parameters_present_flag) {
        ret = hrd_parameters(gb, &vui->hrd_parameters);
        if (ret < 0)
            return ret;
    }
    if (vui->nal_hrd_parameters_present_flag || vui->vcl_hrd_parameters_present_flag)
        vui->low_delay_hrd_flag = get_bits(gb, 1);
    vui->pic_struct_present_flag = get_bits(gb, 1);
    vui->bitstream_restriction_flag = get_bits(gb, 1);
    if (vui->bitstream_restriction_flag) {
        vui->motion_vectors_over_pic_boundaries_flag = get_bits(gb, 1);
        vui->max_bytes_per_pic_denom = get_ue_golomb_31(gb);
        vui->max_bits_per_mb_denom = get_ue_golomb_31(gb);
        vui->log2_max_mv_length_horizontal = get_ue_golomb_31(gb);
        vui->log2_max_mv_length_vertical = get_ue_golomb_31(gb);
        vui->num_reorder_pics = get_ue_golomb_long(gb);
        vui->max_dec_pic_buffering = get_ue_golomb_long(gb);
    }

    return 0;
}

// @see ISO_IEC_23094-1 (7.3.2.1 SPS RBSP syntax)
int ff_evc_parse_sps(GetBitContext *gb, EVCParamSets *ps)
{
    EVCParserSPS *sps;
    unsigned sps_seq_parameter_set_id;
    int ret;

    sps_seq_parameter_set_id = get_ue_golomb(gb);

    if (sps_seq_parameter_set_id >= EVC_MAX_SPS_COUNT)
        return AVERROR_INVALIDDATA;

    sps = av_mallocz(sizeof(*sps));
    if (!sps)
        return AVERROR(ENOMEM);

    sps->sps_seq_parameter_set_id = sps_seq_parameter_set_id;

    // the Baseline profile is indicated by profile_idc eqal to 0
    // the Main profile is indicated by profile_idc eqal to 1
    sps->profile_idc = get_bits(gb, 8);

    sps->level_idc = get_bits(gb, 8);

    skip_bits_long(gb, 32); /* skip toolset_idc_h */
    skip_bits_long(gb, 32); /* skip toolset_idc_l */

    // 0 - monochrome
    // 1 - 4:2:0
    // 2 - 4:2:2
    // 3 - 4:4:4
    sps->chroma_format_idc = get_ue_golomb_31(gb);
    if (sps->chroma_format_idc > 3) {
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    sps->pic_width_in_luma_samples = get_ue_golomb_long(gb);
    sps->pic_height_in_luma_samples = get_ue_golomb_long(gb);

    sps->bit_depth_luma_minus8 = get_ue_golomb_31(gb);
    sps->bit_depth_chroma_minus8 = get_ue_golomb_31(gb);

    sps->sps_btt_flag = get_bits1(gb);
    if (sps->sps_btt_flag) {
        sps->log2_ctu_size_minus2 = get_ue_golomb_long(gb);
        sps->log2_min_cb_size_minus2 = get_ue_golomb_long(gb);
        sps->log2_diff_ctu_max_14_cb_size = get_ue_golomb_long(gb);
        sps->log2_diff_ctu_max_tt_cb_size = get_ue_golomb_long(gb);
        sps->log2_diff_min_cb_min_tt_cb_size_minus2 = get_ue_golomb_long(gb);
    }

    sps->sps_suco_flag = get_bits1(gb);
    if (sps->sps_suco_flag) {
        sps->log2_diff_ctu_size_max_suco_cb_size = get_ue_golomb_long(gb);
        sps->log2_diff_max_suco_min_suco_cb_size = get_ue_golomb_long(gb);
    }

    sps->sps_admvp_flag = get_bits1(gb);
    if (sps->sps_admvp_flag) {
        sps->sps_affine_flag = get_bits1(gb);
        sps->sps_amvr_flag = get_bits1(gb);
        sps->sps_dmvr_flag = get_bits1(gb);
        sps->sps_mmvd_flag = get_bits1(gb);
        sps->sps_hmvp_flag = get_bits1(gb);
    }

    sps->sps_eipd_flag =  get_bits1(gb);
    if (sps->sps_eipd_flag) {
        sps->sps_ibc_flag = get_bits1(gb);
        if (sps->sps_ibc_flag)
            sps->log2_max_ibc_cand_size_minus2 = get_ue_golomb(gb);
    }

    sps->sps_cm_init_flag = get_bits1(gb);
    if (sps->sps_cm_init_flag)
        sps->sps_adcc_flag = get_bits1(gb);

    sps->sps_iqt_flag = get_bits1(gb);
    if (sps->sps_iqt_flag)
        sps->sps_ats_flag = get_bits1(gb);

    sps->sps_addb_flag = get_bits1(gb);
    sps->sps_alf_flag = get_bits1(gb);
    sps->sps_htdf_flag = get_bits1(gb);
    sps->sps_rpl_flag = get_bits1(gb);
    sps->sps_pocs_flag = get_bits1(gb);
    sps->sps_dquant_flag = get_bits1(gb);
    sps->sps_dra_flag = get_bits1(gb);

    if (sps->sps_pocs_flag) {
        sps->log2_max_pic_order_cnt_lsb_minus4 = get_ue_golomb(gb);
        if (sps->log2_max_pic_order_cnt_lsb_minus4 > 12U) {
            ret = AVERROR_INVALIDDATA;
            goto fail;
        }
    }

    if (!sps->sps_pocs_flag || !sps->sps_rpl_flag) {
        sps->log2_sub_gop_length = get_ue_golomb(gb);
        if (sps->log2_sub_gop_length > 5U) {
            ret = AVERROR_INVALIDDATA;
            goto fail;
        }
        if (sps->log2_sub_gop_length == 0)
            sps->log2_ref_pic_gap_length = get_ue_golomb(gb);
    }

    if (!sps->sps_rpl_flag)
        sps->max_num_tid0_ref_pics = get_ue_golomb_31(gb);
    else {
        sps->sps_max_dec_pic_buffering_minus1 = get_ue_golomb_long(gb);
        if ((unsigned)sps->sps_max_dec_pic_buffering_minus1 > 16 - 1) {
            ret = AVERROR_INVALIDDATA;
            goto fail;
        }
        sps->long_term_ref_pic_flag = get_bits1(gb);
        sps->rpl1_same_as_rpl0_flag = get_bits1(gb);
        sps->num_ref_pic_list_in_sps[0] = get_ue_golomb(gb);

        if ((unsigned)sps->num_ref_pic_list_in_sps[0] >= EVC_MAX_NUM_RPLS) {
            ret = AVERROR_INVALIDDATA;
            goto fail;
        }

        for (int i = 0; i < sps->num_ref_pic_list_in_sps[0]; ++i) {
            ret = ref_pic_list_struct(sps, gb, &sps->rpls[0][i]);
            if (ret < 0)
                goto fail;
        }

        if (!sps->rpl1_same_as_rpl0_flag) {
            sps->num_ref_pic_list_in_sps[1] = get_ue_golomb(gb);
            if ((unsigned)sps->num_ref_pic_list_in_sps[1] >= EVC_MAX_NUM_RPLS) {
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }
            for (int i = 0; i < sps->num_ref_pic_list_in_sps[1]; ++i) {
                ret = ref_pic_list_struct(sps, gb, &sps->rpls[1][i]);
                if (ret < 0)
                    goto fail;
            }
        }
    }

    sps->picture_cropping_flag = get_bits1(gb);

    if (sps->picture_cropping_flag) {
        sps->picture_crop_left_offset = get_ue_golomb_long(gb);
        sps->picture_crop_right_offset = get_ue_golomb_long(gb);
        sps->picture_crop_top_offset = get_ue_golomb_long(gb);
        sps->picture_crop_bottom_offset = get_ue_golomb_long(gb);
    }

    if (sps->chroma_format_idc != 0) {
        sps->chroma_qp_table_struct.chroma_qp_table_present_flag = get_bits1(gb);

        if (sps->chroma_qp_table_struct.chroma_qp_table_present_flag) {
            sps->chroma_qp_table_struct.same_qp_table_for_chroma = get_bits1(gb);
            sps->chroma_qp_table_struct.global_offset_flag = get_bits1(gb);
            for (int i = 0; i < (sps->chroma_qp_table_struct.same_qp_table_for_chroma ? 1 : 2); i++) {
                sps->chroma_qp_table_struct.num_points_in_qp_table_minus1[i] = get_ue_golomb(gb);
                if (sps->chroma_qp_table_struct.num_points_in_qp_table_minus1[i] >= EVC_MAX_QP_TABLE_SIZE) {
                    ret = AVERROR_INVALIDDATA;
                    goto fail;
                }
                for (int j = 0; j <= sps->chroma_qp_table_struct.num_points_in_qp_table_minus1[i]; j++) {
                    sps->chroma_qp_table_struct.delta_qp_in_val_minus1[i][j] = get_bits(gb, 6);
                    sps->chroma_qp_table_struct.delta_qp_out_val[i][j] = get_se_golomb_long(gb);
                }
            }
        }
    }

    sps->vui_parameters_present_flag = get_bits1(gb);
    if (sps->vui_parameters_present_flag) {
        ret = vui_parameters(gb, &(sps->vui_parameters));
        if (ret < 0)
            goto fail;
    }

    // @note
    // If necessary, add the missing fields to the EVCParserSPS structure
    // and then extend parser implementation

    av_freep(&ps->sps[sps_seq_parameter_set_id]);
    ps->sps[sps_seq_parameter_set_id] = sps;

    return 0;
fail:
    av_free(sps);
    return ret;
}

// @see ISO_IEC_23094-1 (7.3.2.2 SPS RBSP syntax)
//
// @note
// The current implementation of parse_sps function doesn't handle VUI parameters parsing.
// If it will be needed, parse_sps function could be extended to handle VUI parameters parsing
// to initialize fields of the AVCodecContex i.e. color_primaries, color_trc,color_range
//
int ff_evc_parse_pps(GetBitContext *gb, EVCParamSets *ps)
{
    EVCParserPPS *pps;
    unsigned pps_pic_parameter_set_id;
    int ret;

    pps_pic_parameter_set_id = get_ue_golomb(gb);
    if (pps_pic_parameter_set_id >= EVC_MAX_PPS_COUNT)
        return AVERROR_INVALIDDATA;

    pps = av_mallocz(sizeof(*pps));
    if (!pps)
        return AVERROR(ENOMEM);

    pps->pps_pic_parameter_set_id = pps_pic_parameter_set_id;

    pps->pps_seq_parameter_set_id = get_ue_golomb(gb);
    if (pps->pps_seq_parameter_set_id >= EVC_MAX_SPS_COUNT) {
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    pps->num_ref_idx_default_active_minus1[0] = get_ue_golomb(gb);
    pps->num_ref_idx_default_active_minus1[1] = get_ue_golomb(gb);
    pps->additional_lt_poc_lsb_len = get_ue_golomb(gb);
    pps->rpl1_idx_present_flag = get_bits1(gb);
    pps->single_tile_in_pic_flag = get_bits1(gb);

    if (!pps->single_tile_in_pic_flag) {
        pps->num_tile_columns_minus1 = get_ue_golomb(gb);
        pps->num_tile_rows_minus1 = get_ue_golomb(gb);
        if (pps->num_tile_columns_minus1 >= EVC_MAX_TILE_COLUMNS ||
            pps->num_tile_rows_minus1 >= EVC_MAX_TILE_ROWS) {
            ret = AVERROR_INVALIDDATA;
            goto fail;
        }
        pps->uniform_tile_spacing_flag = get_bits1(gb);

        if (!pps->uniform_tile_spacing_flag) {
            for (int i = 0; i < pps->num_tile_columns_minus1; i++)
                pps->tile_column_width_minus1[i] = get_ue_golomb(gb);

            for (int i = 0; i < pps->num_tile_rows_minus1; i++)
                pps->tile_row_height_minus1[i] = get_ue_golomb(gb);
        }
        pps->loop_filter_across_tiles_enabled_flag = get_bits1(gb);
        pps->tile_offset_len_minus1 = get_ue_golomb(gb);
    }

    pps->tile_id_len_minus1 = get_ue_golomb(gb);
    if (pps->tile_id_len_minus1 > 15U) {
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }
    pps->explicit_tile_id_flag = get_bits1(gb);

    if (pps->explicit_tile_id_flag) {
        for (int i = 0; i <= pps->num_tile_rows_minus1; i++) {
            for (int j = 0; j <= pps->num_tile_columns_minus1; j++)
                pps->tile_id_val[i][j] = get_bits(gb, pps->tile_id_len_minus1 + 1);
        }
    }

    pps->pic_dra_enabled_flag = 0;
    pps->pic_dra_enabled_flag = get_bits1(gb);

    if (pps->pic_dra_enabled_flag)
        pps->pic_dra_aps_id = get_bits(gb, 5);

    pps->arbitrary_slice_present_flag = get_bits1(gb);
    pps->constrained_intra_pred_flag = get_bits1(gb);
    pps->cu_qp_delta_enabled_flag = get_bits1(gb);

    if (pps->cu_qp_delta_enabled_flag)
        pps->log2_cu_qp_delta_area_minus6 = get_ue_golomb(gb);

    av_freep(&ps->pps[pps_pic_parameter_set_id]);
    ps->pps[pps_pic_parameter_set_id] = pps;

    return 0;
fail:
    av_free(pps);
    return ret;
}

void ff_evc_ps_free(EVCParamSets *ps) {
    for (int i = 0; i < EVC_MAX_SPS_COUNT; i++)
        av_freep(&ps->sps[i]);

    for (int i = 0; i < EVC_MAX_PPS_COUNT; i++)
        av_freep(&ps->pps[i]);
}
