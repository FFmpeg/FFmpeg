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

#include "golomb.h"
#include "parser.h"
#include "evc.h"
#include "evc_parse.h"

#define EXTENDED_SAR            255

#define NUM_CHROMA_FORMATS      4   // @see ISO_IEC_23094-1 section 6.2 table 2

static const enum AVPixelFormat pix_fmts_8bit[NUM_CHROMA_FORMATS] = {
    AV_PIX_FMT_GRAY8, AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUV444P
};

static const enum AVPixelFormat pix_fmts_9bit[NUM_CHROMA_FORMATS] = {
    AV_PIX_FMT_GRAY9, AV_PIX_FMT_YUV420P9, AV_PIX_FMT_YUV422P9, AV_PIX_FMT_YUV444P9
};

static const enum AVPixelFormat pix_fmts_10bit[NUM_CHROMA_FORMATS] = {
    AV_PIX_FMT_GRAY10, AV_PIX_FMT_YUV420P10, AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV444P10
};

static const enum AVPixelFormat pix_fmts_12bit[NUM_CHROMA_FORMATS] = {
    AV_PIX_FMT_GRAY12, AV_PIX_FMT_YUV420P12, AV_PIX_FMT_YUV422P12, AV_PIX_FMT_YUV444P12
};

static const enum AVPixelFormat pix_fmts_14bit[NUM_CHROMA_FORMATS] = {
    AV_PIX_FMT_GRAY14, AV_PIX_FMT_YUV420P14, AV_PIX_FMT_YUV422P14, AV_PIX_FMT_YUV444P14
};

static const enum AVPixelFormat pix_fmts_16bit[NUM_CHROMA_FORMATS] = {
    AV_PIX_FMT_GRAY16, AV_PIX_FMT_YUV420P16, AV_PIX_FMT_YUV422P16, AV_PIX_FMT_YUV444P16
};

// nuh_temporal_id specifies a temporal identifier for the NAL unit
int ff_evc_get_temporal_id(const uint8_t *bits, int bits_size, void *logctx)
{
    int temporal_id = 0;
    uint16_t t = 0;

    if (bits_size < EVC_NALU_HEADER_SIZE) {
        av_log(logctx, AV_LOG_ERROR, "Can't read NAL unit header\n");
        return 0;
    }

    // forbidden_zero_bit
    if ((bits[0] & 0x80) != 0)
        return -1;

    t = AV_RB16(bits);

    temporal_id = (t >> 6) & 0x0007;

    return temporal_id;
}

// @see ISO_IEC_23094-1 (7.3.7 Reference picture list structure syntax)
static int ref_pic_list_struct(GetBitContext *gb, RefPicListStruct *rpl)
{
    uint32_t delta_poc_st, strp_entry_sign_flag = 0;
    rpl->ref_pic_num = get_ue_golomb(gb);
    if (rpl->ref_pic_num > 0) {
        delta_poc_st = get_ue_golomb(gb);

        rpl->ref_pics[0] = delta_poc_st;
        if (rpl->ref_pics[0] != 0) {
            strp_entry_sign_flag = get_bits(gb, 1);

            rpl->ref_pics[0] *= 1 - (strp_entry_sign_flag << 1);
        }
    }

    for (int i = 1; i < rpl->ref_pic_num; ++i) {
        delta_poc_st = get_ue_golomb(gb);
        if (delta_poc_st != 0)
            strp_entry_sign_flag = get_bits(gb, 1);
        rpl->ref_pics[i] = rpl->ref_pics[i - 1] + delta_poc_st * (1 - (strp_entry_sign_flag << 1));
    }

    return 0;
}

// @see  ISO_IEC_23094-1 (E.2.2 HRD parameters syntax)
static int hrd_parameters(GetBitContext *gb, HRDParameters *hrd)
{
    hrd->cpb_cnt_minus1 = get_ue_golomb(gb);
    hrd->bit_rate_scale = get_bits(gb, 4);
    hrd->cpb_size_scale = get_bits(gb, 4);
    for (int SchedSelIdx = 0; SchedSelIdx <= hrd->cpb_cnt_minus1; SchedSelIdx++) {
        hrd->bit_rate_value_minus1[SchedSelIdx] = get_ue_golomb(gb);
        hrd->cpb_size_value_minus1[SchedSelIdx] = get_ue_golomb(gb);
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
        vui->chroma_sample_loc_type_top_field = get_ue_golomb(gb);
        vui->chroma_sample_loc_type_bottom_field = get_ue_golomb(gb);
    }
    vui->neutral_chroma_indication_flag = get_bits(gb, 1);

    vui->field_seq_flag = get_bits(gb, 1);

    vui->timing_info_present_flag = get_bits(gb, 1);
    if (vui->timing_info_present_flag) {
        vui->num_units_in_tick = get_bits(gb, 32);
        vui->time_scale = get_bits(gb, 32);
        vui->fixed_pic_rate_flag = get_bits(gb, 1);
    }
    vui->nal_hrd_parameters_present_flag = get_bits(gb, 1);
    if (vui->nal_hrd_parameters_present_flag)
        hrd_parameters(gb, &vui->hrd_parameters);
    vui->vcl_hrd_parameters_present_flag = get_bits(gb, 1);
    if (vui->vcl_hrd_parameters_present_flag)
        hrd_parameters(gb, &vui->hrd_parameters);
    if (vui->nal_hrd_parameters_present_flag || vui->vcl_hrd_parameters_present_flag)
        vui->low_delay_hrd_flag = get_bits(gb, 1);
    vui->pic_struct_present_flag = get_bits(gb, 1);
    vui->bitstream_restriction_flag = get_bits(gb, 1);
    if (vui->bitstream_restriction_flag) {
        vui->motion_vectors_over_pic_boundaries_flag = get_bits(gb, 1);
        vui->max_bytes_per_pic_denom = get_ue_golomb(gb);
        vui->max_bits_per_mb_denom = get_ue_golomb(gb);
        vui->log2_max_mv_length_horizontal = get_ue_golomb(gb);
        vui->log2_max_mv_length_vertical = get_ue_golomb(gb);
        vui->num_reorder_pics = get_ue_golomb(gb);
        vui->max_dec_pic_buffering = get_ue_golomb(gb);
    }

    return 0;
}

// @see ISO_IEC_23094-1 (7.3.2.1 SPS RBSP syntax)
EVCParserSPS *ff_evc_parse_sps(EVCParserContext *ctx, const uint8_t *bs, int bs_size)
{
    GetBitContext gb;
    EVCParserSPS *sps;
    int sps_seq_parameter_set_id;

    if (init_get_bits8(&gb, bs, bs_size) < 0)
        return NULL;

    sps_seq_parameter_set_id = get_ue_golomb(&gb);

    if (sps_seq_parameter_set_id >= EVC_MAX_SPS_COUNT)
        return NULL;

    if(!ctx->sps[sps_seq_parameter_set_id]) {
        if((ctx->sps[sps_seq_parameter_set_id] = av_malloc(sizeof(EVCParserSPS))) == NULL)
            return NULL;
    }

    sps = ctx->sps[sps_seq_parameter_set_id];
    memset(sps, 0, sizeof(*sps));

    sps->sps_seq_parameter_set_id = sps_seq_parameter_set_id;

    // the Baseline profile is indicated by profile_idc eqal to 0
    // the Main profile is indicated by profile_idc eqal to 1
    sps->profile_idc = get_bits(&gb, 8);

    sps->level_idc = get_bits(&gb, 8);

    skip_bits_long(&gb, 32); /* skip toolset_idc_h */
    skip_bits_long(&gb, 32); /* skip toolset_idc_l */

    // 0 - monochrome
    // 1 - 4:2:0
    // 2 - 4:2:2
    // 3 - 4:4:4
    sps->chroma_format_idc = get_ue_golomb(&gb);

    sps->pic_width_in_luma_samples = get_ue_golomb(&gb);
    sps->pic_height_in_luma_samples = get_ue_golomb(&gb);

    sps->bit_depth_luma_minus8 = get_ue_golomb(&gb);
    sps->bit_depth_chroma_minus8 = get_ue_golomb(&gb);

    sps->sps_btt_flag = get_bits(&gb, 1);
    if (sps->sps_btt_flag) {
        sps->log2_ctu_size_minus5 = get_ue_golomb(&gb);
        sps->log2_min_cb_size_minus2 = get_ue_golomb(&gb);
        sps->log2_diff_ctu_max_14_cb_size = get_ue_golomb(&gb);
        sps->log2_diff_ctu_max_tt_cb_size = get_ue_golomb(&gb);
        sps->log2_diff_min_cb_min_tt_cb_size_minus2 = get_ue_golomb(&gb);
    }

    sps->sps_suco_flag = get_bits(&gb, 1);
    if (sps->sps_suco_flag) {
        sps->log2_diff_ctu_size_max_suco_cb_size = get_ue_golomb(&gb);
        sps->log2_diff_max_suco_min_suco_cb_size = get_ue_golomb(&gb);
    }

    sps->sps_admvp_flag = get_bits(&gb, 1);
    if (sps->sps_admvp_flag) {
        sps->sps_affine_flag = get_bits(&gb, 1);
        sps->sps_amvr_flag = get_bits(&gb, 1);
        sps->sps_dmvr_flag = get_bits(&gb, 1);
        sps->sps_mmvd_flag = get_bits(&gb, 1);
        sps->sps_hmvp_flag = get_bits(&gb, 1);
    }

    sps->sps_eipd_flag =  get_bits(&gb, 1);
    if (sps->sps_eipd_flag) {
        sps->sps_ibc_flag = get_bits(&gb, 1);
        if (sps->sps_ibc_flag)
            sps->log2_max_ibc_cand_size_minus2 = get_ue_golomb(&gb);
    }

    sps->sps_cm_init_flag = get_bits(&gb, 1);
    if (sps->sps_cm_init_flag)
        sps->sps_adcc_flag = get_bits(&gb, 1);

    sps->sps_iqt_flag = get_bits(&gb, 1);
    if (sps->sps_iqt_flag)
        sps->sps_ats_flag = get_bits(&gb, 1);

    sps->sps_addb_flag = get_bits(&gb, 1);
    sps->sps_alf_flag = get_bits(&gb, 1);
    sps->sps_htdf_flag = get_bits(&gb, 1);
    sps->sps_rpl_flag = get_bits(&gb, 1);
    sps->sps_pocs_flag = get_bits(&gb, 1);
    sps->sps_dquant_flag = get_bits(&gb, 1);
    sps->sps_dra_flag = get_bits(&gb, 1);

    if (sps->sps_pocs_flag)
        sps->log2_max_pic_order_cnt_lsb_minus4 = get_ue_golomb(&gb);

    if (!sps->sps_pocs_flag || !sps->sps_rpl_flag) {
        sps->log2_sub_gop_length = get_ue_golomb(&gb);
        if (sps->log2_sub_gop_length == 0)
            sps->log2_ref_pic_gap_length = get_ue_golomb(&gb);
    }

    if (!sps->sps_rpl_flag)
        sps->max_num_tid0_ref_pics = get_ue_golomb(&gb);
    else {
        sps->sps_max_dec_pic_buffering_minus1 = get_ue_golomb(&gb);
        sps->long_term_ref_pic_flag = get_bits(&gb, 1);
        sps->rpl1_same_as_rpl0_flag = get_bits(&gb, 1);
        sps->num_ref_pic_list_in_sps[0] = get_ue_golomb(&gb);

        for (int i = 0; i < sps->num_ref_pic_list_in_sps[0]; ++i)
            ref_pic_list_struct(&gb, &sps->rpls[0][i]);

        if (!sps->rpl1_same_as_rpl0_flag) {
            sps->num_ref_pic_list_in_sps[1] = get_ue_golomb(&gb);
            for (int i = 0; i < sps->num_ref_pic_list_in_sps[1]; ++i)
                ref_pic_list_struct(&gb, &sps->rpls[1][i]);
        }
    }

    sps->picture_cropping_flag = get_bits(&gb, 1);

    if (sps->picture_cropping_flag) {
        sps->picture_crop_left_offset = get_ue_golomb(&gb);
        sps->picture_crop_right_offset = get_ue_golomb(&gb);
        sps->picture_crop_top_offset = get_ue_golomb(&gb);
        sps->picture_crop_bottom_offset = get_ue_golomb(&gb);
    }

    if (sps->chroma_format_idc != 0) {
        sps->chroma_qp_table_struct.chroma_qp_table_present_flag = get_bits(&gb, 1);

        if (sps->chroma_qp_table_struct.chroma_qp_table_present_flag) {
            sps->chroma_qp_table_struct.same_qp_table_for_chroma = get_bits(&gb, 1);
            sps->chroma_qp_table_struct.global_offset_flag = get_bits(&gb, 1);
            for (int i = 0; i < (sps->chroma_qp_table_struct.same_qp_table_for_chroma ? 1 : 2); i++) {
                sps->chroma_qp_table_struct.num_points_in_qp_table_minus1[i] = get_ue_golomb(&gb);;
                for (int j = 0; j <= sps->chroma_qp_table_struct.num_points_in_qp_table_minus1[i]; j++) {
                    sps->chroma_qp_table_struct.delta_qp_in_val_minus1[i][j] = get_bits(&gb, 6);
                    sps->chroma_qp_table_struct.delta_qp_out_val[i][j] = get_se_golomb(&gb);
                }
            }
        }
    }

    sps->vui_parameters_present_flag = get_bits(&gb, 1);
    if (sps->vui_parameters_present_flag)
        vui_parameters(&gb, &(sps->vui_parameters));

    // @note
    // If necessary, add the missing fields to the EVCParserSPS structure
    // and then extend parser implementation

    return sps;
}

// @see ISO_IEC_23094-1 (7.3.2.2 SPS RBSP syntax)
//
// @note
// The current implementation of parse_sps function doesn't handle VUI parameters parsing.
// If it will be needed, parse_sps function could be extended to handle VUI parameters parsing
// to initialize fields of the AVCodecContex i.e. color_primaries, color_trc,color_range
//
EVCParserPPS *ff_evc_parse_pps(EVCParserContext *ctx, const uint8_t *bs, int bs_size)
{
    GetBitContext gb;
    EVCParserPPS *pps;

    int pps_pic_parameter_set_id;

    if (init_get_bits8(&gb, bs, bs_size) < 0)
        return NULL;

    pps_pic_parameter_set_id = get_ue_golomb(&gb);
    if (pps_pic_parameter_set_id > EVC_MAX_PPS_COUNT)
        return NULL;

    if(!ctx->pps[pps_pic_parameter_set_id]) {
        if ((ctx->pps[pps_pic_parameter_set_id] = av_malloc(sizeof(EVCParserPPS))) == NULL)
            return NULL;
    }

    pps = ctx->pps[pps_pic_parameter_set_id];
    memset(pps, 0, sizeof(*pps));

    pps->pps_pic_parameter_set_id = pps_pic_parameter_set_id;

    pps->pps_seq_parameter_set_id = get_ue_golomb(&gb);
    if (pps->pps_seq_parameter_set_id >= EVC_MAX_SPS_COUNT) {
        av_freep(&ctx->pps[pps_pic_parameter_set_id]);
        return NULL;
    }

    pps->num_ref_idx_default_active_minus1[0] = get_ue_golomb(&gb);
    pps->num_ref_idx_default_active_minus1[1] = get_ue_golomb(&gb);
    pps->additional_lt_poc_lsb_len = get_ue_golomb(&gb);
    pps->rpl1_idx_present_flag = get_bits(&gb, 1);
    pps->single_tile_in_pic_flag = get_bits(&gb, 1);

    if (!pps->single_tile_in_pic_flag) {
        pps->num_tile_columns_minus1 = get_ue_golomb(&gb);
        pps->num_tile_rows_minus1 = get_ue_golomb(&gb);
        pps->uniform_tile_spacing_flag = get_bits(&gb, 1);

        if (!pps->uniform_tile_spacing_flag) {
            for (int i = 0; i < pps->num_tile_columns_minus1; i++)
                pps->tile_column_width_minus1[i] = get_ue_golomb(&gb);

            for (int i = 0; i < pps->num_tile_rows_minus1; i++)
                pps->tile_row_height_minus1[i] = get_ue_golomb(&gb);
        }
        pps->loop_filter_across_tiles_enabled_flag = get_bits(&gb, 1);
        pps->tile_offset_len_minus1 = get_ue_golomb(&gb);
    }

    pps->tile_id_len_minus1 = get_ue_golomb(&gb);
    pps->explicit_tile_id_flag = get_bits(&gb, 1);

    if (pps->explicit_tile_id_flag) {
        for (int i = 0; i <= pps->num_tile_rows_minus1; i++) {
            for (int j = 0; j <= pps->num_tile_columns_minus1; j++)
                pps->tile_id_val[i][j] = get_bits(&gb, pps->tile_id_len_minus1 + 1);
        }
    }

    pps->pic_dra_enabled_flag = 0;
    pps->pic_dra_enabled_flag = get_bits(&gb, 1);

    if (pps->pic_dra_enabled_flag)
        pps->pic_dra_aps_id = get_bits(&gb, 5);

    pps->arbitrary_slice_present_flag = get_bits(&gb, 1);
    pps->constrained_intra_pred_flag = get_bits(&gb, 1);
    pps->cu_qp_delta_enabled_flag = get_bits(&gb, 1);

    if (pps->cu_qp_delta_enabled_flag)
        pps->log2_cu_qp_delta_area_minus6 = get_ue_golomb(&gb);

    return pps;
}

// @see ISO_IEC_23094-1 (7.3.2.6 Slice layer RBSP syntax)
static int evc_parse_slice_header(EVCParserContext *ctx, EVCParserSliceHeader *sh, const uint8_t *bs, int bs_size)
{
    GetBitContext gb;
    EVCParserPPS *pps;
    EVCParserSPS *sps;

    int num_tiles_in_slice = 0;
    int slice_pic_parameter_set_id;
    int ret;

    if ((ret = init_get_bits8(&gb, bs, bs_size)) < 0)
        return ret;

    slice_pic_parameter_set_id = get_ue_golomb(&gb);

    if (slice_pic_parameter_set_id < 0 || slice_pic_parameter_set_id >= EVC_MAX_PPS_COUNT)
        return AVERROR_INVALIDDATA;

    pps = ctx->pps[slice_pic_parameter_set_id];
    if(!pps)
        return AVERROR_INVALIDDATA;

    sps = ctx->sps[pps->pps_seq_parameter_set_id];
    if(!sps)
        return AVERROR_INVALIDDATA;

    memset(sh, 0, sizeof(*sh));
    sh->slice_pic_parameter_set_id = slice_pic_parameter_set_id;

    if (!pps->single_tile_in_pic_flag) {
        sh->single_tile_in_slice_flag = get_bits(&gb, 1);
        sh->first_tile_id = get_bits(&gb, pps->tile_id_len_minus1 + 1);
    } else
        sh->single_tile_in_slice_flag = 1;

    if (!sh->single_tile_in_slice_flag) {
        if (pps->arbitrary_slice_present_flag)
            sh->arbitrary_slice_flag = get_bits(&gb, 1);

        if (!sh->arbitrary_slice_flag)
            sh->last_tile_id = get_bits(&gb, pps->tile_id_len_minus1 + 1);
        else {
            sh->num_remaining_tiles_in_slice_minus1 = get_ue_golomb(&gb);
            num_tiles_in_slice = sh->num_remaining_tiles_in_slice_minus1 + 2;
            for (int i = 0; i < num_tiles_in_slice - 1; ++i)
                sh->delta_tile_id_minus1[i] = get_ue_golomb(&gb);
        }
    }

    sh->slice_type = get_ue_golomb(&gb);

    if (ctx->nalu_type == EVC_IDR_NUT)
        sh->no_output_of_prior_pics_flag = get_bits(&gb, 1);

    if (sps->sps_mmvd_flag && ((sh->slice_type == EVC_SLICE_TYPE_B) || (sh->slice_type == EVC_SLICE_TYPE_P)))
        sh->mmvd_group_enable_flag = get_bits(&gb, 1);
    else
        sh->mmvd_group_enable_flag = 0;

    if (sps->sps_alf_flag) {
        int ChromaArrayType = sps->chroma_format_idc;

        sh->slice_alf_enabled_flag = get_bits(&gb, 1);

        if (sh->slice_alf_enabled_flag) {
            sh->slice_alf_luma_aps_id = get_bits(&gb, 5);
            sh->slice_alf_map_flag = get_bits(&gb, 1);
            sh->slice_alf_chroma_idc = get_bits(&gb, 2);

            if ((ChromaArrayType == 1 || ChromaArrayType == 2) && sh->slice_alf_chroma_idc > 0)
                sh->slice_alf_chroma_aps_id =  get_bits(&gb, 5);
        }
        if (ChromaArrayType == 3) {
            int sliceChromaAlfEnabledFlag = 0;
            int sliceChroma2AlfEnabledFlag = 0;

            if (sh->slice_alf_chroma_idc == 1) { // @see ISO_IEC_23094-1 (7.4.5)
                sliceChromaAlfEnabledFlag = 1;
                sliceChroma2AlfEnabledFlag = 0;
            } else if (sh->slice_alf_chroma_idc == 2) {
                sliceChromaAlfEnabledFlag = 0;
                sliceChroma2AlfEnabledFlag = 1;
            } else if (sh->slice_alf_chroma_idc == 3) {
                sliceChromaAlfEnabledFlag = 1;
                sliceChroma2AlfEnabledFlag = 1;
            } else {
                sliceChromaAlfEnabledFlag = 0;
                sliceChroma2AlfEnabledFlag = 0;
            }

            if (!sh->slice_alf_enabled_flag)
                sh->slice_alf_chroma_idc = get_bits(&gb, 2);

            if (sliceChromaAlfEnabledFlag) {
                sh->slice_alf_chroma_aps_id = get_bits(&gb, 5);
                sh->slice_alf_chroma_map_flag = get_bits(&gb, 1);
            }

            if (sliceChroma2AlfEnabledFlag) {
                sh->slice_alf_chroma2_aps_id = get_bits(&gb, 5);
                sh->slice_alf_chroma2_map_flag = get_bits(&gb, 1);
            }
        }
    }

    if (ctx->nalu_type != EVC_IDR_NUT) {
        if (sps->sps_pocs_flag)
            sh->slice_pic_order_cnt_lsb = get_bits(&gb, sps->log2_max_pic_order_cnt_lsb_minus4 + 4);
    }

    // @note
    // If necessary, add the missing fields to the EVCParserSliceHeader structure
    // and then extend parser implementation

    return 0;
}

int ff_evc_parse_nal_unit(EVCParserContext *ctx, const uint8_t *buf, int buf_size, void *logctx)
{
    int nalu_type, nalu_size;
    int tid;
    const uint8_t *data = buf;
    int data_size = buf_size;

    // ctx->picture_structure = AV_PICTURE_STRUCTURE_FRAME;
    ctx->key_frame = -1;

    nalu_size = buf_size;
    if (nalu_size <= 0) {
        av_log(logctx, AV_LOG_ERROR, "Invalid NAL unit size: (%d)\n", nalu_size);
        return AVERROR_INVALIDDATA;
    }

    // @see ISO_IEC_23094-1_2020, 7.4.2.2 NAL unit header semantic (Table 4 - NAL unit type codes and NAL unit type classes)
    // @see enum EVCNALUnitType in evc.h
    nalu_type = evc_get_nalu_type(data, data_size, logctx);
    if (nalu_type < EVC_NOIDR_NUT || nalu_type > EVC_UNSPEC_NUT62) {
        av_log(logctx, AV_LOG_ERROR, "Invalid NAL unit type: (%d)\n", nalu_type);
        return AVERROR_INVALIDDATA;
    }
    ctx->nalu_type = nalu_type;

    tid = ff_evc_get_temporal_id(data, data_size, logctx);
    if (tid < 0) {
        av_log(logctx, AV_LOG_ERROR, "Invalid temporial id: (%d)\n", tid);
        return AVERROR_INVALIDDATA;
    }
    ctx->nuh_temporal_id = tid;

    data += EVC_NALU_HEADER_SIZE;
    data_size -= EVC_NALU_HEADER_SIZE;

    switch(nalu_type) {
    case EVC_SPS_NUT: {
        EVCParserSPS *sps;
        int SubGopLength;
        int bit_depth;

        sps = ff_evc_parse_sps(ctx, data, nalu_size);
        if (!sps) {
            av_log(logctx, AV_LOG_ERROR, "SPS parsing error\n");
            return AVERROR_INVALIDDATA;
        }

        ctx->coded_width         = sps->pic_width_in_luma_samples;
        ctx->coded_height        = sps->pic_height_in_luma_samples;

        if(sps->picture_cropping_flag) {
            ctx->width           = sps->pic_width_in_luma_samples  - sps->picture_crop_left_offset - sps->picture_crop_right_offset;
            ctx->height          = sps->pic_height_in_luma_samples - sps->picture_crop_top_offset  - sps->picture_crop_bottom_offset;
        } else {
            ctx->width           = sps->pic_width_in_luma_samples;
            ctx->height          = sps->pic_height_in_luma_samples;
        }

        SubGopLength = (int)pow(2.0, sps->log2_sub_gop_length);
        ctx->gop_size = SubGopLength;

        ctx->delay = (sps->sps_max_dec_pic_buffering_minus1) ? sps->sps_max_dec_pic_buffering_minus1 - 1 : SubGopLength + sps->max_num_tid0_ref_pics - 1;

        if (sps->profile_idc == 1) ctx->profile = FF_PROFILE_EVC_MAIN;
        else ctx->profile = FF_PROFILE_EVC_BASELINE;

        if (sps->vui_parameters_present_flag && sps->vui_parameters.timing_info_present_flag) {
            int64_t num = sps->vui_parameters.num_units_in_tick;
            int64_t den = sps->vui_parameters.time_scale;
            if (num != 0 && den != 0)
                av_reduce(&ctx->framerate.den, &ctx->framerate.num, num, den, 1 << 30);
        } else
            ctx->framerate = (AVRational) { 0, 1 };

        bit_depth = sps->bit_depth_chroma_minus8 + 8;
        ctx->format = AV_PIX_FMT_NONE;

        switch (bit_depth) {
        case 8:
            ctx->format = pix_fmts_8bit[sps->chroma_format_idc];
            break;
        case 9:
            ctx->format = pix_fmts_9bit[sps->chroma_format_idc];
            break;
        case 10:
            ctx->format = pix_fmts_10bit[sps->chroma_format_idc];
            break;
        case 12:
            ctx->format = pix_fmts_12bit[sps->chroma_format_idc];
            break;
        case 14:
            ctx->format = pix_fmts_14bit[sps->chroma_format_idc];
            break;
        case 16:
            ctx->format = pix_fmts_16bit[sps->chroma_format_idc];
            break;
        }
        av_assert0(ctx->format != AV_PIX_FMT_NONE);

        break;
    }
    case EVC_PPS_NUT: {
        EVCParserPPS *pps;

        pps = ff_evc_parse_pps(ctx, data, nalu_size);
        if (!pps) {
            av_log(logctx, AV_LOG_ERROR, "PPS parsing error\n");
            return AVERROR_INVALIDDATA;
        }
        break;
    }
    case EVC_SEI_NUT:   // Supplemental Enhancement Information
    case EVC_APS_NUT:   // Adaptation parameter set
    case EVC_FD_NUT:    // Filler data
        break;
    case EVC_IDR_NUT:   // Coded slice of a IDR or non-IDR picture
    case EVC_NOIDR_NUT: {
        EVCParserSliceHeader sh;
        const EVCParserSPS *sps;
        const EVCParserPPS *pps;
        int ret;

        ret = evc_parse_slice_header(ctx, &sh, data, nalu_size);
        if (ret < 0) {
            av_log(logctx, AV_LOG_ERROR, "Slice header parsing error\n");
            return ret;
        }

        switch (sh.slice_type) {
        case EVC_SLICE_TYPE_B: {
            ctx->pict_type =  AV_PICTURE_TYPE_B;
            break;
        }
        case EVC_SLICE_TYPE_P: {
            ctx->pict_type =  AV_PICTURE_TYPE_P;
            break;
        }
        case EVC_SLICE_TYPE_I: {
            ctx->pict_type =  AV_PICTURE_TYPE_I;
            break;
        }
        default: {
            ctx->pict_type =  AV_PICTURE_TYPE_NONE;
        }
        }

        ctx->key_frame = (nalu_type == EVC_IDR_NUT) ? 1 : 0;

        // POC (picture order count of the current picture) derivation
        // @see ISO/IEC 23094-1:2020(E) 8.3.1 Decoding process for picture order count
        pps = ctx->pps[sh.slice_pic_parameter_set_id];
        sps = ctx->sps[pps->pps_seq_parameter_set_id];
        av_assert0(sps && pps);

        if (sps->sps_pocs_flag) {

            int PicOrderCntMsb = 0;
            ctx->poc.prevPicOrderCntVal = ctx->poc.PicOrderCntVal;

            if (nalu_type == EVC_IDR_NUT)
                PicOrderCntMsb = 0;
            else {
                int MaxPicOrderCntLsb = 1 << (sps->log2_max_pic_order_cnt_lsb_minus4 + 4);

                int prevPicOrderCntLsb = ctx->poc.PicOrderCntVal & (MaxPicOrderCntLsb - 1);
                int prevPicOrderCntMsb = ctx->poc.PicOrderCntVal - prevPicOrderCntLsb;


                if ((sh.slice_pic_order_cnt_lsb < prevPicOrderCntLsb) &&
                    ((prevPicOrderCntLsb - sh.slice_pic_order_cnt_lsb) >= (MaxPicOrderCntLsb / 2)))

                    PicOrderCntMsb = prevPicOrderCntMsb + MaxPicOrderCntLsb;

                else if ((sh.slice_pic_order_cnt_lsb > prevPicOrderCntLsb) &&
                         ((sh.slice_pic_order_cnt_lsb - prevPicOrderCntLsb) > (MaxPicOrderCntLsb / 2)))

                    PicOrderCntMsb = prevPicOrderCntMsb - MaxPicOrderCntLsb;

                else
                    PicOrderCntMsb = prevPicOrderCntMsb;
            }
            ctx->poc.PicOrderCntVal = PicOrderCntMsb + sh.slice_pic_order_cnt_lsb;

        } else {
            if (nalu_type == EVC_IDR_NUT) {
                ctx->poc.PicOrderCntVal = 0;
                ctx->poc.DocOffset = -1;
            } else {
                int SubGopLength = (int)pow(2.0, sps->log2_sub_gop_length);
                if (tid == 0) {
                    ctx->poc.PicOrderCntVal = ctx->poc.prevPicOrderCntVal + SubGopLength;
                    ctx->poc.DocOffset = 0;
                    ctx->poc.prevPicOrderCntVal = ctx->poc.PicOrderCntVal;
                } else {
                    int ExpectedTemporalId;
                    int PocOffset;
                    int prevDocOffset = ctx->poc.DocOffset;

                    ctx->poc.DocOffset = (prevDocOffset + 1) % SubGopLength;
                    if (ctx->poc.DocOffset == 0) {
                        ctx->poc.prevPicOrderCntVal += SubGopLength;
                        ExpectedTemporalId = 0;
                    } else
                        ExpectedTemporalId = 1 + (int)log2(ctx->poc.DocOffset);
                    while (tid != ExpectedTemporalId) {
                        ctx->poc.DocOffset = (ctx->poc.DocOffset + 1) % SubGopLength;
                        if (ctx->poc.DocOffset == 0)
                            ExpectedTemporalId = 0;
                        else
                            ExpectedTemporalId = 1 + (int)log2(ctx->poc.DocOffset);
                    }
                    PocOffset = (int)(SubGopLength * ((2.0 * ctx->poc.DocOffset + 1) / (int)pow(2.0, tid) - 2));
                    ctx->poc.PicOrderCntVal = ctx->poc.prevPicOrderCntVal + PocOffset;
                }
            }
        }

        ctx->output_picture_number = ctx->poc.PicOrderCntVal;
        ctx->key_frame = (nalu_type == EVC_IDR_NUT) ? 1 : 0;

        break;
    }
    }

    return 0;
}

void ff_evc_parse_free(EVCParserContext *ctx) {
    for (int i = 0; i < EVC_MAX_SPS_COUNT; i++)
        av_freep(&ctx->sps[i]);

    for (int i = 0; i < EVC_MAX_PPS_COUNT; i++)
        av_freep(&ctx->pps[i]);
}
