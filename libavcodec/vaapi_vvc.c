/*
 * VVC HW decode acceleration through VA API
 *
 * Copyright (c) 2024 Intel Corporation
 *
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
#include <va/va_dec_vvc.h>

#include "vvc/dec.h"
#include "vvc/refs.h"
#include "hwaccel_internal.h"
#include "vaapi_decode.h"

typedef struct VAAPIDecodePictureVVC {
    VAAPIDecodePicture          pic;
    VAPictureParameterBufferVVC pic_param;
    VASliceParameterBufferVVC   slice_param;
    int                         decode_issued;
} VAAPIDecodePictureVVC;

static void init_vaapi_pic(VAPictureVVC *va_pic)
{
    va_pic->picture_id    = VA_INVALID_ID;
    va_pic->flags         = VA_PICTURE_VVC_INVALID;
    va_pic->pic_order_cnt = 0;
}

static void fill_vaapi_pic(VAPictureVVC *va_pic, const VVCFrame *pic)
{
    va_pic->picture_id    = ff_vaapi_get_surface_id(pic->frame);
    va_pic->pic_order_cnt = pic->poc;
    va_pic->flags         = 0;

    if (pic->flags & VVC_FRAME_FLAG_LONG_REF)
        va_pic->flags |= VA_PICTURE_VVC_LONG_TERM_REFERENCE;
}

static void fill_vaapi_reference_frames(const VVCFrameContext *h, VAPictureParameterBufferVVC *pp)
{
    const VVCFrame *current_picture = h->ref;
    int i, j;

    for (i = 0, j = 0; i < FF_ARRAY_ELEMS(pp->ReferenceFrames); i++) {
        const VVCFrame *frame = NULL;

        while (!frame && j < FF_ARRAY_ELEMS(h->DPB)) {
            if ((&h->DPB[j] != current_picture ) &&
                (h->DPB[j].flags & (VVC_FRAME_FLAG_LONG_REF | VVC_FRAME_FLAG_SHORT_REF)))
                frame = &h->DPB[j];
            j++;
        }

        init_vaapi_pic(&pp->ReferenceFrames[i]);

        if (frame) {
            VAAPIDecodePictureVVC *pic;
            fill_vaapi_pic(&pp->ReferenceFrames[i], frame);
            pic = frame->hwaccel_picture_private;
            if (!pic->decode_issued)
               pp->ReferenceFrames[i].flags |= VA_PICTURE_VVC_UNAVAILABLE_REFERENCE;
        }
    }
}

static int vaapi_vvc_start_frame(AVCodecContext          *avctx,
                                 av_unused const uint8_t *buffer,
                                 av_unused uint32_t       size)
{
    const VVCContext                    *h = avctx->priv_data;
    VVCFrameContext                    *fc = &h->fcs[(h->nb_frames + h->nb_fcs) % h->nb_fcs];
    const H266RawSPS                  *sps = fc->ps.sps->r;
    const H266RawPPS                  *pps = fc->ps.pps->r;
    const H266RawPictureHeader         *ph = fc->ps.ph.r;
    VAAPIDecodePictureVVC             *pic = fc->ref->hwaccel_picture_private;
    VAPictureParameterBufferVVC *pic_param = &pic->pic_param;
    uint16_t tile_dim, exp_slice_height_in_ctus[VVC_MAX_SLICES] = {0};
    int i, j, k, err;

    pic->pic.output_surface = ff_vaapi_get_surface_id(fc->ref->frame);

    *pic_param = (VAPictureParameterBufferVVC) {
        .pps_pic_width_in_luma_samples                 = pps->pps_pic_width_in_luma_samples,
        .pps_pic_height_in_luma_samples                = pps->pps_pic_height_in_luma_samples,
        .sps_num_subpics_minus1                        = sps->sps_num_subpics_minus1,
        .sps_chroma_format_idc                         = sps->sps_chroma_format_idc,
        .sps_bitdepth_minus8                           = sps->sps_bitdepth_minus8,
        .sps_log2_ctu_size_minus5                      = sps->sps_log2_ctu_size_minus5,
        .sps_log2_min_luma_coding_block_size_minus2    = sps->sps_log2_min_luma_coding_block_size_minus2,
        .sps_log2_transform_skip_max_size_minus2       = sps->sps_log2_transform_skip_max_size_minus2,
        .sps_six_minus_max_num_merge_cand              = sps->sps_six_minus_max_num_merge_cand,
        .sps_five_minus_max_num_subblock_merge_cand    = sps->sps_five_minus_max_num_subblock_merge_cand,
        .sps_max_num_merge_cand_minus_max_num_gpm_cand = sps->sps_max_num_merge_cand_minus_max_num_gpm_cand,
        .sps_log2_parallel_merge_level_minus2          = sps->sps_log2_parallel_merge_level_minus2,
        .sps_min_qp_prime_ts                           = sps->sps_min_qp_prime_ts,
        .sps_six_minus_max_num_ibc_merge_cand          = sps->sps_six_minus_max_num_ibc_merge_cand,
        .sps_num_ladf_intervals_minus2                 = sps->sps_num_ladf_intervals_minus2,
        .sps_ladf_lowest_interval_qp_offset            = sps->sps_ladf_lowest_interval_qp_offset,
        .sps_flags.bits = {
            .sps_subpic_info_present_flag                                  = sps->sps_subpic_info_present_flag,
            .sps_independent_subpics_flag                                  = sps->sps_independent_subpics_flag,
            .sps_subpic_same_size_flag                                     = sps->sps_subpic_same_size_flag,
            .sps_entropy_coding_sync_enabled_flag                          = sps->sps_entropy_coding_sync_enabled_flag,
            .sps_qtbtt_dual_tree_intra_flag                                = sps->sps_qtbtt_dual_tree_intra_flag,
            .sps_max_luma_transform_size_64_flag                           = sps->sps_max_luma_transform_size_64_flag,
            .sps_transform_skip_enabled_flag                               = sps->sps_transform_skip_enabled_flag,
            .sps_bdpcm_enabled_flag                                        = sps->sps_bdpcm_enabled_flag,
            .sps_mts_enabled_flag                                          = sps->sps_mts_enabled_flag,
            .sps_explicit_mts_intra_enabled_flag                           = sps->sps_explicit_mts_intra_enabled_flag,
            .sps_explicit_mts_inter_enabled_flag                           = sps->sps_explicit_mts_inter_enabled_flag,
            .sps_lfnst_enabled_flag                                        = sps->sps_lfnst_enabled_flag,
            .sps_joint_cbcr_enabled_flag                                   = sps->sps_joint_cbcr_enabled_flag,
            .sps_same_qp_table_for_chroma_flag                             = sps->sps_same_qp_table_for_chroma_flag,
            .sps_sao_enabled_flag                                          = sps->sps_sao_enabled_flag,
            .sps_alf_enabled_flag                                          = sps->sps_alf_enabled_flag,
            .sps_ccalf_enabled_flag                                        = sps->sps_ccalf_enabled_flag,
            .sps_lmcs_enabled_flag                                         = sps->sps_lmcs_enabled_flag,
            .sps_sbtmvp_enabled_flag                                       = sps->sps_sbtmvp_enabled_flag,
            .sps_amvr_enabled_flag                                         = sps->sps_amvr_enabled_flag,
            .sps_smvd_enabled_flag                                         = sps->sps_smvd_enabled_flag,
            .sps_mmvd_enabled_flag                                         = sps->sps_mmvd_enabled_flag,
            .sps_sbt_enabled_flag                                          = sps->sps_sbt_enabled_flag,
            .sps_affine_enabled_flag                                       = sps->sps_affine_enabled_flag,
            .sps_6param_affine_enabled_flag                                = sps->sps_6param_affine_enabled_flag,
            .sps_affine_amvr_enabled_flag                                  = sps->sps_affine_amvr_enabled_flag,
            .sps_affine_prof_enabled_flag                                  = sps->sps_affine_prof_enabled_flag,
            .sps_bcw_enabled_flag                                          = sps->sps_bcw_enabled_flag,
            .sps_ciip_enabled_flag                                         = sps->sps_ciip_enabled_flag,
            .sps_gpm_enabled_flag                                          = sps->sps_gpm_enabled_flag,
            .sps_isp_enabled_flag                                          = sps->sps_isp_enabled_flag,
            .sps_mrl_enabled_flag                                          = sps->sps_mrl_enabled_flag,
            .sps_mip_enabled_flag                                          = sps->sps_mip_enabled_flag,
            .sps_cclm_enabled_flag                                         = sps->sps_cclm_enabled_flag,
            .sps_chroma_horizontal_collocated_flag                         = sps->sps_chroma_horizontal_collocated_flag,
            .sps_chroma_vertical_collocated_flag                           = sps->sps_chroma_vertical_collocated_flag,
            .sps_palette_enabled_flag                                      = sps->sps_palette_enabled_flag,
            .sps_act_enabled_flag                                          = sps->sps_act_enabled_flag,
            .sps_ibc_enabled_flag                                          = sps->sps_ibc_enabled_flag,
            .sps_ladf_enabled_flag                                         = sps->sps_ladf_enabled_flag,
            .sps_explicit_scaling_list_enabled_flag                        = sps->sps_explicit_scaling_list_enabled_flag,
            .sps_scaling_matrix_for_lfnst_disabled_flag                    = sps->sps_scaling_matrix_for_lfnst_disabled_flag,
            .sps_scaling_matrix_for_alternative_colour_space_disabled_flag = sps->sps_scaling_matrix_for_alternative_colour_space_disabled_flag,
            .sps_scaling_matrix_designated_colour_space_flag               = sps->sps_scaling_matrix_designated_colour_space_flag,
            .sps_virtual_boundaries_enabled_flag                           = sps->sps_virtual_boundaries_enabled_flag,
            .sps_virtual_boundaries_present_flag                           = sps->sps_virtual_boundaries_present_flag,
        },
        .NumVerVirtualBoundaries               = sps->sps_virtual_boundaries_present_flag ?
                                                 sps->sps_num_ver_virtual_boundaries :
                                                 ph->ph_num_ver_virtual_boundaries,
        .NumHorVirtualBoundaries               = sps->sps_virtual_boundaries_present_flag ?
                                                 sps->sps_num_hor_virtual_boundaries :
                                                 ph->ph_num_hor_virtual_boundaries,
        .pps_scaling_win_left_offset           = pps->pps_scaling_win_left_offset,
        .pps_scaling_win_right_offset          = pps->pps_scaling_win_right_offset,
        .pps_scaling_win_top_offset            = pps->pps_scaling_win_top_offset,
        .pps_scaling_win_bottom_offset         = pps->pps_scaling_win_bottom_offset,
        .pps_num_exp_tile_columns_minus1       = pps->pps_num_exp_tile_columns_minus1,
        .pps_num_exp_tile_rows_minus1          = pps->pps_num_exp_tile_rows_minus1,
        .pps_num_slices_in_pic_minus1          = pps->pps_num_slices_in_pic_minus1,
        .pps_pic_width_minus_wraparound_offset = pps->pps_pic_width_minus_wraparound_offset,
        .pps_cb_qp_offset                      = pps->pps_cb_qp_offset,
        .pps_cr_qp_offset                      = pps->pps_cr_qp_offset,
        .pps_joint_cbcr_qp_offset_value        = pps->pps_joint_cbcr_qp_offset_value,
        .pps_chroma_qp_offset_list_len_minus1  = pps->pps_chroma_qp_offset_list_len_minus1,
        .pps_flags.bits = {
            .pps_loop_filter_across_tiles_enabled_flag   = pps->pps_loop_filter_across_tiles_enabled_flag,
            .pps_rect_slice_flag                         = pps->pps_rect_slice_flag,
            .pps_single_slice_per_subpic_flag            = pps->pps_single_slice_per_subpic_flag,
            .pps_loop_filter_across_slices_enabled_flag  = pps->pps_loop_filter_across_slices_enabled_flag,
            .pps_weighted_pred_flag                      = pps->pps_weighted_pred_flag,
            .pps_weighted_bipred_flag                    = pps->pps_weighted_bipred_flag,
            .pps_ref_wraparound_enabled_flag             = pps->pps_ref_wraparound_enabled_flag,
            .pps_cu_qp_delta_enabled_flag                = pps->pps_cu_qp_delta_enabled_flag,
            .pps_cu_chroma_qp_offset_list_enabled_flag   = pps->pps_cu_chroma_qp_offset_list_enabled_flag,
            .pps_deblocking_filter_override_enabled_flag = pps->pps_deblocking_filter_override_enabled_flag,
            .pps_deblocking_filter_disabled_flag         = pps->pps_deblocking_filter_disabled_flag,
            .pps_dbf_info_in_ph_flag                     = pps->pps_dbf_info_in_ph_flag,
            .pps_sao_info_in_ph_flag                     = pps->pps_sao_info_in_ph_flag,
            .pps_alf_info_in_ph_flag                     = pps->pps_alf_info_in_ph_flag,
        },
        .ph_lmcs_aps_id                                = ph->ph_lmcs_aps_id,
        .ph_scaling_list_aps_id                        = ph->ph_scaling_list_aps_id,
        .ph_log2_diff_min_qt_min_cb_intra_slice_luma   = ph->ph_log2_diff_min_qt_min_cb_intra_slice_luma,
        .ph_max_mtt_hierarchy_depth_intra_slice_luma   = ph->ph_max_mtt_hierarchy_depth_intra_slice_luma,
        .ph_log2_diff_max_bt_min_qt_intra_slice_luma   = ph->ph_log2_diff_max_bt_min_qt_intra_slice_luma,
        .ph_log2_diff_max_tt_min_qt_intra_slice_luma   = ph->ph_log2_diff_max_tt_min_qt_intra_slice_luma,
        .ph_log2_diff_min_qt_min_cb_intra_slice_chroma = ph->ph_log2_diff_min_qt_min_cb_intra_slice_chroma,
        .ph_max_mtt_hierarchy_depth_intra_slice_chroma = ph->ph_max_mtt_hierarchy_depth_intra_slice_chroma,
        .ph_log2_diff_max_bt_min_qt_intra_slice_chroma = ph->ph_log2_diff_max_bt_min_qt_intra_slice_chroma,
        .ph_log2_diff_max_tt_min_qt_intra_slice_chroma = ph->ph_log2_diff_max_tt_min_qt_intra_slice_chroma,
        .ph_cu_qp_delta_subdiv_intra_slice             = ph->ph_cu_qp_delta_subdiv_intra_slice,
        .ph_cu_chroma_qp_offset_subdiv_intra_slice     = ph->ph_cu_chroma_qp_offset_subdiv_intra_slice,
        .ph_log2_diff_min_qt_min_cb_inter_slice        = ph->ph_log2_diff_min_qt_min_cb_inter_slice,
        .ph_max_mtt_hierarchy_depth_inter_slice        = ph->ph_max_mtt_hierarchy_depth_inter_slice,
        .ph_log2_diff_max_bt_min_qt_inter_slice        = ph->ph_log2_diff_max_bt_min_qt_inter_slice,
        .ph_log2_diff_max_tt_min_qt_inter_slice        = ph->ph_log2_diff_max_tt_min_qt_inter_slice,
        .ph_cu_qp_delta_subdiv_inter_slice             = ph->ph_cu_qp_delta_subdiv_inter_slice,
        .ph_cu_chroma_qp_offset_subdiv_inter_slice     = ph->ph_cu_chroma_qp_offset_subdiv_inter_slice,
        .ph_flags.bits= {
            .ph_non_ref_pic_flag                   = ph->ph_non_ref_pic_flag,
            .ph_alf_enabled_flag                   = ph->ph_alf_enabled_flag,
            .ph_alf_cb_enabled_flag                = ph->ph_alf_cb_enabled_flag,
            .ph_alf_cr_enabled_flag                = ph->ph_alf_cr_enabled_flag,
            .ph_alf_cc_cb_enabled_flag             = ph->ph_alf_cc_cb_enabled_flag,
            .ph_alf_cc_cr_enabled_flag             = ph->ph_alf_cc_cr_enabled_flag,
            .ph_lmcs_enabled_flag                  = ph->ph_lmcs_enabled_flag,
            .ph_chroma_residual_scale_flag         = ph->ph_chroma_residual_scale_flag,
            .ph_explicit_scaling_list_enabled_flag = ph->ph_explicit_scaling_list_enabled_flag,
            .ph_virtual_boundaries_present_flag    = ph->ph_virtual_boundaries_present_flag,
            .ph_temporal_mvp_enabled_flag          = ph->ph_temporal_mvp_enabled_flag,
            .ph_mmvd_fullpel_only_flag             = ph->ph_mmvd_fullpel_only_flag,
            .ph_mvd_l1_zero_flag                   = ph->ph_mvd_l1_zero_flag,
            .ph_bdof_disabled_flag                 = ph->ph_bdof_disabled_flag,
            .ph_dmvr_disabled_flag                 = ph->ph_dmvr_disabled_flag,
            .ph_prof_disabled_flag                 = ph->ph_prof_disabled_flag,
            .ph_joint_cbcr_sign_flag               = ph->ph_joint_cbcr_sign_flag,
            .ph_sao_luma_enabled_flag              = ph->ph_sao_luma_enabled_flag,
            .ph_sao_chroma_enabled_flag            = ph->ph_sao_chroma_enabled_flag,
            .ph_deblocking_filter_disabled_flag    = ph->ph_deblocking_filter_disabled_flag,
        },
        .PicMiscFlags.fields = {
            .IntraPicFlag = pps->pps_mixed_nalu_types_in_pic_flag ? 0 : IS_IRAP(h) ? 1 : 0,
        }
    };

    fill_vaapi_pic(&pic_param->CurrPic, fc->ref);
    fill_vaapi_reference_frames(fc, pic_param);

    for (i = 0; i < VVC_MAX_SAMPLE_ARRAYS; i++)
        for (j = 0; j < VVC_MAX_POINTS_IN_QP_TABLE; j++)
            pic_param->ChromaQpTable[i][j] = fc->ps.sps->chroma_qp_table[i][j];
    for (i = 0; i < 4; i++) {
        pic_param->sps_ladf_qp_offset[i]              = sps->sps_ladf_qp_offset[i];
        pic_param->sps_ladf_delta_threshold_minus1[i] = sps->sps_ladf_delta_threshold_minus1[i];
    }

    for (i = 0; i < (sps->sps_virtual_boundaries_present_flag ? sps->sps_num_ver_virtual_boundaries : ph->ph_num_ver_virtual_boundaries); i++) {
        pic_param->VirtualBoundaryPosX[i] = (sps->sps_virtual_boundaries_present_flag ?
                                            (sps->sps_virtual_boundary_pos_x_minus1[i] + 1) :
                                            (ph->ph_virtual_boundary_pos_x_minus1[i] + 1)) * 8;
    }

    for (i = 0; i < (sps->sps_virtual_boundaries_present_flag ? sps->sps_num_hor_virtual_boundaries : ph->ph_num_hor_virtual_boundaries); i++) {
        pic_param->VirtualBoundaryPosY[i] = (sps->sps_virtual_boundaries_present_flag ?
                                            (sps->sps_virtual_boundary_pos_y_minus1[i] + 1) :
                                            (ph->ph_virtual_boundary_pos_y_minus1[i] + 1)) * 8;
    }

    for (i = 0; i < 6; i++) {
        pic_param->pps_cb_qp_offset_list[i]         = pps->pps_cb_qp_offset_list[i];
        pic_param->pps_cr_qp_offset_list[i]         = pps->pps_cr_qp_offset_list[i];
        pic_param->pps_joint_cbcr_qp_offset_list[i] = pps->pps_joint_cbcr_qp_offset_list[i];
    }

    err = ff_vaapi_decode_make_param_buffer(avctx, &pic->pic,
                                            VAPictureParameterBufferType,
                                            &pic->pic_param, sizeof(VAPictureParameterBufferVVC));
    if (err < 0)
        goto fail;

    for (i = 0; i <= sps->sps_num_subpics_minus1 && sps->sps_subpic_info_present_flag; i++) {
        VASubPicVVC subpic_param = {
            .sps_subpic_ctu_top_left_x = sps->sps_subpic_ctu_top_left_x[i],
            .sps_subpic_ctu_top_left_y = sps->sps_subpic_ctu_top_left_y[i],
            .sps_subpic_width_minus1   = sps->sps_subpic_width_minus1[i],
            .sps_subpic_height_minus1  = sps->sps_subpic_height_minus1[i],
            .SubpicIdVal               = pps->sub_pic_id_val[i],
            .subpic_flags.bits = {
                .sps_subpic_treated_as_pic_flag             = sps->sps_subpic_treated_as_pic_flag[i],
                .sps_loop_filter_across_subpic_enabled_flag = sps->sps_loop_filter_across_subpic_enabled_flag[i],
            }
        };
        err = ff_vaapi_decode_make_param_buffer(avctx, &pic->pic,
                                                VASubPicBufferType,
                                                &subpic_param, sizeof(VASubPicVVC));
        if (err < 0)
            goto fail;
    }

    for (i = 0; i < VVC_MAX_ALF_COUNT; i++) {
        const VVCALF *alf_list = h->ps.alf_list[i];
        if (alf_list) {
            const H266RawAPS *alf = alf_list->r;
            VAAlfDataVVC alf_param = {
                .aps_adaptation_parameter_set_id = i,
                .alf_luma_num_filters_signalled_minus1 = alf->alf_luma_num_filters_signalled_minus1,
                .alf_chroma_num_alt_filters_minus1     = alf->alf_chroma_num_alt_filters_minus1,
                .alf_cc_cb_filters_signalled_minus1    = alf->alf_cc_cb_filters_signalled_minus1,
                .alf_cc_cr_filters_signalled_minus1    = alf->alf_cc_cr_filters_signalled_minus1,
                .alf_flags.bits = {
                    .alf_luma_filter_signal_flag   = alf->alf_luma_filter_signal_flag,
                    .alf_chroma_filter_signal_flag = alf->alf_chroma_filter_signal_flag,
                    .alf_cc_cb_filter_signal_flag  = alf->alf_cc_cb_filter_signal_flag,
                    .alf_cc_cr_filter_signal_flag  = alf->alf_cc_cr_filter_signal_flag,
                    .alf_luma_clip_flag            = alf->alf_luma_clip_flag,
                    .alf_chroma_clip_flag          = alf->alf_chroma_clip_flag,
                }
            };

            for (j = 0; j < 25; j++)
                alf_param.alf_luma_coeff_delta_idx[j] = alf->alf_luma_coeff_delta_idx[j];

            for (j = 0; j < 25; j++) {
                for (k = 0; k < 12; k++) {
                    alf_param.filtCoeff[j][k]         = alf->alf_luma_coeff_abs[j][k] * (1 - 2 * alf->alf_luma_coeff_sign[j][k]);
                    alf_param.alf_luma_clip_idx[j][k] = alf->alf_luma_clip_idx[j][k];
                }
            }

            for (j = 0; j < 8; j++) {
                for (k = 0; k < 6; k++) {
                    alf_param.AlfCoeffC[j][k]           = alf->alf_chroma_coeff_abs[j][k] * (1 - 2 * alf->alf_chroma_coeff_sign[j][k]);
                    alf_param.alf_chroma_clip_idx[j][k] = alf->alf_chroma_clip_idx[j][k];
                }
            }

            for (j = 0; j < 4; j++) {
                for (k = 0; k < 7; k++) {
                    if (alf->alf_cc_cb_mapped_coeff_abs[j][k])
                        alf_param.CcAlfApsCoeffCb[j][k] = (1 - 2 * alf->alf_cc_cb_coeff_sign[j][k]) * (1 << (alf->alf_cc_cb_mapped_coeff_abs[j][k] - 1));
                    if (alf->alf_cc_cr_mapped_coeff_abs[j][k])
                        alf_param.CcAlfApsCoeffCr[j][k] = (1 - 2 * alf->alf_cc_cr_coeff_sign[j][k]) * (1 << (alf->alf_cc_cr_mapped_coeff_abs[j][k] - 1));
                }
            }

            err = ff_vaapi_decode_make_param_buffer(avctx, &pic->pic,
                                                    VAAlfBufferType,
                                                    &alf_param, sizeof(VAAlfDataVVC));
            if (err < 0)
                goto fail;
        }
    }

    for (i = 0; i < VVC_MAX_LMCS_COUNT; i++) {
        const H266RawAPS *lmcs = h->ps.lmcs_list[i];
        if (lmcs) {
            VALmcsDataVVC lmcs_param = {
                .aps_adaptation_parameter_set_id = i,
                .lmcs_min_bin_idx                = lmcs->lmcs_min_bin_idx,
                .lmcs_delta_max_bin_idx          = lmcs->lmcs_delta_max_bin_idx,
                .lmcsDeltaCrs                    = (1 - 2 * lmcs->lmcs_delta_sign_crs_flag) * lmcs->lmcs_delta_abs_crs,
            };

            for (j = lmcs->lmcs_min_bin_idx; j <= 15 - lmcs->lmcs_delta_max_bin_idx; j++)
                lmcs_param.lmcsDeltaCW[j] = (1 - 2 * lmcs->lmcs_delta_sign_cw_flag[j]) * lmcs->lmcs_delta_abs_cw[j];

            err = ff_vaapi_decode_make_param_buffer(avctx, &pic->pic,
                                                    VALmcsBufferType,
                                                    &lmcs_param, sizeof(VALmcsDataVVC));
            if (err < 0)
                goto fail;
        }
    }

    for (i = 0; i < VVC_MAX_SL_COUNT; i++) {
        const VVCScalingList *sl = h->ps.scaling_list[i];
        if (sl) {
            int l;

            VAScalingListVVC sl_param = {
                .aps_adaptation_parameter_set_id = i,
            };

            for (j = 0; j < 14; j++)
                sl_param.ScalingMatrixDCRec[j] = sl->scaling_matrix_dc_rec[j];

            for (j = 0; j < 2; j++)
                for (k = 0; k < 2; k++)
                    for (l = 0; l < 2; l++)
                        sl_param.ScalingMatrixRec2x2[j][k][l] = sl->scaling_matrix_rec[j][l * 2 + k];

            for (j = 2; j < 8; j++)
                for (k = 0; k < 4; k++)
                    for (l = 0; l < 4; l++)
                        sl_param.ScalingMatrixRec4x4[j - 2][k][l] = sl->scaling_matrix_rec[j][l * 4 + k];

            for (j = 8; j < 28; j++)
                for (k = 0; k < 8; k++)
                    for (l = 0; l < 8; l++)
                        sl_param.ScalingMatrixRec8x8[j - 8][k][l] = sl->scaling_matrix_rec[j][l * 8 + k];

            err = ff_vaapi_decode_make_param_buffer(avctx, &pic->pic,
                                                    VAIQMatrixBufferType,
                                                    &sl_param, sizeof(VAScalingListVVC));
            if (err < 0)
                goto fail;
        }
    }

    for (i = 0; i <= pps->pps_num_exp_tile_columns_minus1; i++) {
        tile_dim = pps->pps_tile_column_width_minus1[i];
        err = ff_vaapi_decode_make_param_buffer(avctx, &pic->pic,
                                                VATileBufferType,
                                                &tile_dim, sizeof(tile_dim));
        if (err < 0)
            goto fail;
    }

    for (i = 0; i <= pps->pps_num_exp_tile_rows_minus1; i++) {
        tile_dim = pps->pps_tile_row_height_minus1[i];
        err = ff_vaapi_decode_make_param_buffer(avctx, &pic->pic,
                                                VATileBufferType,
                                                &tile_dim, sizeof(tile_dim));
        if (err < 0)
            goto fail;
    }

    if (!pps->pps_no_pic_partition_flag && pps->pps_rect_slice_flag && !pps->pps_single_slice_per_subpic_flag) {
        for (i = 0; i <= pps->pps_num_slices_in_pic_minus1; i++) {
            for (j = 0; j < pps->pps_num_exp_slices_in_tile[i]; j++) {
                exp_slice_height_in_ctus[i + j] = pps->pps_exp_slice_height_in_ctus_minus1[i][j] + 1;
            }
        }
        for (i = 0; i <= pps->pps_num_slices_in_pic_minus1; i++) {
            VASliceStructVVC ss_param = {
                .SliceTopLeftTileIdx              = pps->slice_top_left_tile_idx[i],
                .pps_slice_width_in_tiles_minus1  = pps->pps_slice_width_in_tiles_minus1[i],
                .pps_slice_height_in_tiles_minus1 = pps->pps_slice_height_in_tiles_minus1[i],
            };

            if (pps->pps_slice_width_in_tiles_minus1[i] > 0 || pps->pps_slice_height_in_tiles_minus1[i] > 0)
                ss_param.pps_exp_slice_height_in_ctus_minus1 = 0;
            else {
                if (pps->num_slices_in_tile[i] == 1)
                    ss_param.pps_exp_slice_height_in_ctus_minus1 = pps->row_height_val[pps->slice_top_left_tile_idx[i] / pps->num_tile_columns] - 1;
                else if (exp_slice_height_in_ctus[i])
                    ss_param.pps_exp_slice_height_in_ctus_minus1 = exp_slice_height_in_ctus[i] - 1;
                else
                    continue;
            }

            err = ff_vaapi_decode_make_param_buffer(avctx, &pic->pic,
                                                    VASliceStructBufferType,
                                                    &ss_param, sizeof(VASliceStructVVC));
            if (err < 0)
                goto fail;
        }
    }

    return 0;

fail:
    ff_vaapi_decode_cancel(avctx, &pic->pic);
    return err;
}

static uint8_t get_ref_pic_index(const VVCContext *h, const VVCFrame *frame)
{
    VVCFrameContext             *fc = &h->fcs[(h->nb_frames + h->nb_fcs) % h->nb_fcs];
    VAAPIDecodePictureVVC      *pic = fc->ref->hwaccel_picture_private;
    VAPictureParameterBufferVVC *pp = (VAPictureParameterBufferVVC *)&pic->pic_param;
    uint8_t i;

    if (!frame)
        return 0xFF;

    for (i = 0; i < FF_ARRAY_ELEMS(pp->ReferenceFrames); i++) {
        VASurfaceID pid = pp->ReferenceFrames[i].picture_id;
        int poc = pp->ReferenceFrames[i].pic_order_cnt;
        if (pid != VA_INVALID_ID && pid == ff_vaapi_get_surface_id(frame->frame) && poc == frame->poc)
            return i;
    }

    return 0xFF;
}

static int get_slice_data_byte_offset(const uint8_t *buffer, uint32_t size, const SliceContext* sc)
{
    const H266RawSlice *slice = sc->ref;
    int num_identical_bytes   = slice->data_size < 32 ? slice->data_size : 32;

    for (int i = 0; i < size; i++) {
        int skip_bytes = 0;
        if (i >=2 && buffer[i] == 0x03 && !buffer[i - 1] && !buffer[i - 2])
            continue;

        for (int j = 0; j < num_identical_bytes; j++) {
            if (i >= 2 && buffer[i + j + skip_bytes] == 0x03 && !buffer[i + j + skip_bytes - 1] && !buffer[i + j + skip_bytes - 2])
                skip_bytes++;

            if (buffer[i + j + skip_bytes] != slice->data[j])
                break;

            if (j + 1 == num_identical_bytes)
                return i;
        }
    }

    return 0;
}

static int vaapi_vvc_decode_slice(AVCodecContext *avctx,
                                   const uint8_t *buffer,
                                   uint32_t       size)
{
    const VVCContext                    *h = avctx->priv_data;
    VVCFrameContext                    *fc = &h->fcs[(h->nb_frames + h->nb_fcs) % h->nb_fcs];
    const SliceContext                 *sc = fc->slices[fc->nb_slices];
    const H266RawPPS                  *pps = fc->ps.pps->r;
    const H266RawPictureHeader         *ph = fc->ps.ph.r;
    const H266RawSliceHeader           *sh = sc->sh.r;
    VAAPIDecodePictureVVC             *pic = fc->ref->hwaccel_picture_private;
    VASliceParameterBufferVVC *slice_param = &pic->slice_param;
    int nb_list, i, err;

    *slice_param = (VASliceParameterBufferVVC) {
        .slice_data_size                 = size,
        .slice_data_offset               = 0,
        .slice_data_flag                 = VA_SLICE_DATA_FLAG_ALL,
        .slice_data_byte_offset          = get_slice_data_byte_offset(buffer, size, sc),
        .sh_subpic_id                    = sh->sh_subpic_id,
        .sh_slice_address                = sh->sh_slice_address,
        .sh_num_tiles_in_slice_minus1    = sh->sh_num_tiles_in_slice_minus1,
        .sh_slice_type                   = sh->sh_slice_type,
        .sh_num_alf_aps_ids_luma         = sh->sh_num_alf_aps_ids_luma,
        .sh_alf_aps_id_chroma            = sh->sh_alf_aps_id_chroma,
        .sh_alf_cc_cb_aps_id             = sh->sh_alf_cc_cb_aps_id,
        .sh_alf_cc_cr_aps_id             = sh->sh_alf_cc_cr_aps_id,
        .NumRefIdxActive[0]              = sh->num_ref_idx_active[0],
        .NumRefIdxActive[1]              = sh->num_ref_idx_active[1],
        .sh_collocated_ref_idx           = sh->sh_collocated_ref_idx,
        .SliceQpY                        = pps->pps_qp_delta_info_in_ph_flag ?
                                           26 + pps->pps_init_qp_minus26 + ph->ph_qp_delta :
                                           26 + pps->pps_init_qp_minus26 + sh->sh_qp_delta,
        .sh_cb_qp_offset                 = sh->sh_cb_qp_offset,
        .sh_cr_qp_offset                 = sh->sh_cr_qp_offset,
        .sh_joint_cbcr_qp_offset         = sh->sh_joint_cbcr_qp_offset,
        .sh_luma_beta_offset_div2        = sh->sh_luma_beta_offset_div2,
        .sh_luma_tc_offset_div2          = sh->sh_luma_tc_offset_div2,
        .sh_cb_beta_offset_div2          = sh->sh_cb_beta_offset_div2,
        .sh_cb_tc_offset_div2            = sh->sh_cb_tc_offset_div2,
        .sh_cr_beta_offset_div2          = sh->sh_cr_beta_offset_div2,
        .sh_cr_tc_offset_div2            = sh->sh_cr_tc_offset_div2,
        .WPInfo = {
            .luma_log2_weight_denom         = sh->sh_pred_weight_table.luma_log2_weight_denom,
            .delta_chroma_log2_weight_denom = sh->sh_pred_weight_table.delta_chroma_log2_weight_denom,
            .num_l0_weights                 = sh->sh_pred_weight_table.num_l0_weights,
            .num_l1_weights                 = sh->sh_pred_weight_table.num_l1_weights,
        },
        .sh_flags.bits = {
            .sh_alf_enabled_flag                 = sh->sh_alf_enabled_flag,
            .sh_alf_cb_enabled_flag              = sh->sh_alf_cb_enabled_flag,
            .sh_alf_cr_enabled_flag              = sh->sh_alf_cr_enabled_flag,
            .sh_alf_cc_cb_enabled_flag           = sh->sh_alf_cc_cb_enabled_flag,
            .sh_alf_cc_cr_enabled_flag           = sh->sh_alf_cc_cr_enabled_flag,
            .sh_lmcs_used_flag                   = sh->sh_lmcs_used_flag,
            .sh_explicit_scaling_list_used_flag  = sh->sh_explicit_scaling_list_used_flag,
            .sh_cabac_init_flag                  = sh->sh_cabac_init_flag,
            .sh_collocated_from_l0_flag          = sh->sh_collocated_from_l0_flag,
            .sh_cu_chroma_qp_offset_enabled_flag = sh->sh_cu_chroma_qp_offset_enabled_flag,
            .sh_sao_luma_used_flag               = sh->sh_sao_luma_used_flag,
            .sh_sao_chroma_used_flag             = sh->sh_sao_chroma_used_flag,
            .sh_deblocking_filter_disabled_flag  = sh->sh_deblocking_filter_disabled_flag,
            .sh_dep_quant_used_flag              = sh->sh_dep_quant_used_flag,
            .sh_sign_data_hiding_used_flag       = sh->sh_sign_data_hiding_used_flag,
            .sh_ts_residual_coding_disabled_flag = sh->sh_ts_residual_coding_disabled_flag,
        },
    };

    memset(&slice_param->RefPicList, 0xFF, sizeof(slice_param->RefPicList));

    nb_list = (sh->sh_slice_type == VVC_SLICE_TYPE_B) ?
              2 : (sh->sh_slice_type == VVC_SLICE_TYPE_I ? 0 : 1);
    for (int list_idx = 0; list_idx < nb_list; list_idx++) {
        RefPicList *rpl = &sc->rpl[list_idx];

        for (i = 0; i < rpl->nb_refs; i++)
            slice_param->RefPicList[list_idx][i] = get_ref_pic_index(h, rpl->refs[i].ref);
    }

    for (i = 0; i < 7; i++)
        slice_param->sh_alf_aps_id_luma[i] = sh->sh_alf_aps_id_luma[i];

    for (i = 0; i < 15; i++) {
        slice_param->WPInfo.luma_weight_l0_flag[i]   = sh->sh_pred_weight_table.luma_weight_l0_flag[i];
        slice_param->WPInfo.chroma_weight_l0_flag[i] = sh->sh_pred_weight_table.chroma_weight_l0_flag[i];
        slice_param->WPInfo.delta_luma_weight_l0[i]  = sh->sh_pred_weight_table.delta_luma_weight_l0[i];
        slice_param->WPInfo.luma_offset_l0[i]        = sh->sh_pred_weight_table.luma_offset_l0[i];
        slice_param->WPInfo.luma_weight_l1_flag[i]   = sh->sh_pred_weight_table.luma_weight_l1_flag[i];
        slice_param->WPInfo.chroma_weight_l1_flag[i] = sh->sh_pred_weight_table.chroma_weight_l1_flag[i];
        slice_param->WPInfo.delta_luma_weight_l1[i]  = sh->sh_pred_weight_table.delta_luma_weight_l1[i];
        slice_param->WPInfo.luma_offset_l1[i]        = sh->sh_pred_weight_table.luma_offset_l1[i];
    }

    for (i = 0; i < 15; i++) {
        for (int j = 0; j < 2; j++) {
            slice_param->WPInfo.delta_chroma_weight_l0[i][j] = sh->sh_pred_weight_table.delta_chroma_weight_l0[i][j];
            slice_param->WPInfo.delta_chroma_offset_l0[i][j] = sh->sh_pred_weight_table.delta_chroma_offset_l0[i][j];
            slice_param->WPInfo.delta_chroma_weight_l1[i][j] = sh->sh_pred_weight_table.delta_chroma_weight_l1[i][j];
            slice_param->WPInfo.delta_chroma_offset_l1[i][j] = sh->sh_pred_weight_table.delta_chroma_offset_l1[i][j];
        }
    }

    err = ff_vaapi_decode_make_slice_buffer(avctx, &pic->pic,
                                            &pic->slice_param, 1,
                                            sizeof(VASliceParameterBufferVVC),
                                            buffer, size);
    if (err) {
        ff_vaapi_decode_cancel(avctx, &pic->pic);
        return err;
    }

    return 0;
}

static int vaapi_vvc_end_frame(AVCodecContext *avctx)
{

    const VVCContext        *h = avctx->priv_data;
    VVCFrameContext        *fc = &h->fcs[(h->nb_frames + h->nb_fcs) % h->nb_fcs];
    VAAPIDecodePictureVVC *pic = fc->ref->hwaccel_picture_private;
    int ret;

    ret = ff_vaapi_decode_issue(avctx, &pic->pic);
    if (ret < 0)
        goto fail;

    pic->decode_issued = 1;

    return 0;

fail:
    ff_vaapi_decode_cancel(avctx, &pic->pic);
    return ret;
}

const FFHWAccel ff_vvc_vaapi_hwaccel = {
    .p.name               = "vvc_vaapi",
    .p.type               = AVMEDIA_TYPE_VIDEO,
    .p.id                 = AV_CODEC_ID_VVC,
    .p.pix_fmt            = AV_PIX_FMT_VAAPI,
    .start_frame          = &vaapi_vvc_start_frame,
    .end_frame            = &vaapi_vvc_end_frame,
    .decode_slice         = &vaapi_vvc_decode_slice,
    .frame_priv_data_size = sizeof(VAAPIDecodePictureVVC),
    .init                 = &ff_vaapi_decode_init,
    .uninit               = &ff_vaapi_decode_uninit,
    .frame_params         = &ff_vaapi_common_frame_params,
    .priv_data_size       = sizeof(VAAPIDecodeContext),
    .caps_internal        = HWACCEL_CAP_ASYNC_SAFE,
};
