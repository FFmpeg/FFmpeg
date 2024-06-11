/*
 * VVC CABAC decoder
 *
 * Copyright (C) 2021 Nuo Mi
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
#include "libavcodec/cabac_functions.h"

#include "cabac.h"
#include "ctu.h"
#include "data.h"

#define CABAC_MAX_BIN 31

#define CNU 35

enum SyntaxElement {
    ALF_CTB_FLAG                    =                                 0,
    ALF_USE_APS_FLAG                = ALF_CTB_FLAG                  + 9,
    ALF_CTB_CC_CB_IDC,
    ALF_CTB_CC_CR_IDC               = ALF_CTB_CC_CB_IDC             + 3,
    ALF_CTB_FILTER_ALT_IDX          = ALF_CTB_CC_CR_IDC             + 3,
    SAO_MERGE_FLAG                  = ALF_CTB_FILTER_ALT_IDX        + 2,
    SAO_TYPE_IDX,
    SPLIT_CU_FLAG,
    SPLIT_QT_FLAG                   = SPLIT_CU_FLAG                 + 9,
    MTT_SPLIT_CU_VERTICAL_FLAG      = SPLIT_QT_FLAG                 + 6,
    MTT_SPLIT_CU_BINARY_FLAG        = MTT_SPLIT_CU_VERTICAL_FLAG    + 5,
    NON_INTER_FLAG                  = MTT_SPLIT_CU_BINARY_FLAG      + 4,
    CU_SKIP_FLAG                    = NON_INTER_FLAG                + 2,
    PRED_MODE_IBC_FLAG              = CU_SKIP_FLAG                  + 3,
    PRED_MODE_FLAG                  = PRED_MODE_IBC_FLAG            + 3,
    PRED_MODE_PLT_FLAG              = PRED_MODE_FLAG                + 2,
    CU_ACT_ENABLED_FLAG,
    INTRA_BDPCM_LUMA_FLAG,
    INTRA_BDPCM_LUMA_DIR_FLAG,
    INTRA_MIP_FLAG,
    INTRA_LUMA_REF_IDX              = INTRA_MIP_FLAG                + 4,
    INTRA_SUBPARTITIONS_MODE_FLAG   = INTRA_LUMA_REF_IDX            + 2,
    INTRA_SUBPARTITIONS_SPLIT_FLAG,
    INTRA_LUMA_MPM_FLAG,
    INTRA_LUMA_NOT_PLANAR_FLAG,
    INTRA_BDPCM_CHROMA_FLAG         = INTRA_LUMA_NOT_PLANAR_FLAG    + 2,
    INTRA_BDPCM_CHROMA_DIR_FLAG,
    CCLM_MODE_FLAG,
    CCLM_MODE_IDX,
    INTRA_CHROMA_PRED_MODE,
    GENERAL_MERGE_FLAG,
    INTER_PRED_IDC,
    INTER_AFFINE_FLAG               = INTER_PRED_IDC                + 6,
    CU_AFFINE_TYPE_FLAG             = INTER_AFFINE_FLAG             + 3,
    SYM_MVD_FLAG,
    REF_IDX_LX,
    MVP_LX_FLAG                     = REF_IDX_LX                    + 2,
    AMVR_FLAG,
    AMVR_PRECISION_IDX              = AMVR_FLAG                     + 2,
    BCW_IDX                         = AMVR_PRECISION_IDX            + 3,
    CU_CODED_FLAG,
    CU_SBT_FLAG,
    CU_SBT_QUAD_FLAG                = CU_SBT_FLAG                   + 2,
    CU_SBT_HORIZONTAL_FLAG,
    CU_SBT_POS_FLAG                 = CU_SBT_HORIZONTAL_FLAG        + 3,
    LFNST_IDX,
    MTS_IDX                         = LFNST_IDX                     + 3,
    COPY_ABOVE_PALETTE_INDICES_FLAG = MTS_IDX                       + 4,
    PALETTE_TRANSPOSE_FLAG,
    RUN_COPY_FLAG,
    REGULAR_MERGE_FLAG              = RUN_COPY_FLAG                 + 8,
    MMVD_MERGE_FLAG                 = REGULAR_MERGE_FLAG            + 2,
    MMVD_CAND_FLAG,
    MMVD_DISTANCE_IDX,
    CIIP_FLAG,
    MERGE_SUBBLOCK_FLAG,
    MERGE_SUBBLOCK_IDX              = MERGE_SUBBLOCK_FLAG           + 3,
    MERGE_IDX,
    ABS_MVD_GREATER0_FLAG,
    ABS_MVD_GREATER1_FLAG,
    TU_Y_CODED_FLAG,
    TU_CB_CODED_FLAG                = TU_Y_CODED_FLAG               + 4,
    TU_CR_CODED_FLAG                = TU_CB_CODED_FLAG              + 2,
    CU_QP_DELTA_ABS                 = TU_CR_CODED_FLAG              + 3,
    CU_CHROMA_QP_OFFSET_FLAG        = CU_QP_DELTA_ABS               + 2,
    CU_CHROMA_QP_OFFSET_IDX,
    TRANSFORM_SKIP_FLAG,
    TU_JOINT_CBCR_RESIDUAL_FLAG     = TRANSFORM_SKIP_FLAG           + 2,
    LAST_SIG_COEFF_X_PREFIX         = TU_JOINT_CBCR_RESIDUAL_FLAG   + 3,
    LAST_SIG_COEFF_Y_PREFIX         = LAST_SIG_COEFF_X_PREFIX       +23,
    SB_CODED_FLAG                   = LAST_SIG_COEFF_Y_PREFIX       +23,
    SIG_COEFF_FLAG                  = SB_CODED_FLAG                 + 7,
    PAR_LEVEL_FLAG                  = SIG_COEFF_FLAG                +63,
    ABS_LEVEL_GTX_FLAG              = PAR_LEVEL_FLAG                +33,
    COEFF_SIGN_FLAG                 = ABS_LEVEL_GTX_FLAG            +72,
    SYNTAX_ELEMENT_LAST             = COEFF_SIGN_FLAG               + 6,
};

static const uint8_t init_values[4][SYNTAX_ELEMENT_LAST] = {
    {
        //alf_ctb_flag
        62,  39,  39,  54,  39,  39,  31,  39,  39,
        //alf_use_aps_flag
        46,
        //alf_ctb_cc_cb_idc
        18,  30,  31,
        //alf_ctb_cc_cr_idc
        18,  30,  31,
        //alf_ctb_filter_alt_idx
        11,  11,
        //sao_merge_left_flag and sao_merge_up_flag
        60,
        //sao_type_idx_luma and sao_type_idx_chroma
        13,
        //split_cu_flag
        19,  28,  38,  27,  29,  38,  20,  30,  31,
        //split_qt_flag
        27,   6,  15,  25,  19,  37,
        //mtt_split_cu_vertical_flag
        43,  42,  29,  27,  44,
        //mtt_split_cu_binary_flag
        36,  45,  36,  45,
        //non_inter_flag
        CNU, CNU,
        //cu_skip_flag
        0,  26,  28,
        //pred_mode_ibc_flag
        17,  42,  36,
        //pred_mode_flag
        CNU, CNU,
        //pred_mode_plt_flag
        25,
        //cu_act_enabled_flag
        52,
        //intra_bdpcm_luma_flag
        19,
        //intra_bdpcm_luma_dir_flag
        35,
        //intra_mip_flag
        33,  49,  50,  25,
        //intra_luma_ref_idx
        25,  60,
        //intra_subpartitions_mode_flag
        33,
        //intra_subpartitions_split_flag
        43,
        //intra_luma_mpm_flag
        45,
        //intra_luma_not_planar_flag
        13,  28,
        //intra_bdpcm_chroma_flag
         1,
        //intra_bdpcm_chroma_dir_flag
        27,
        //cclm_mode_flag
        59,
        //cclm_mode_idx
        27,
        //intra_chroma_pred_mode
        34,
        //general_merge_flag
        26,
        //inter_pred_idc
        CNU, CNU, CNU, CNU, CNU, CNU,
        //inter_affine_flag
        CNU, CNU, CNU,
        //cu_affine_type_flag
        CNU,
        //sym_mvd_flag
        CNU,
        //ref_idx_l0 and ref_idx_l1
        CNU, CNU,
        //mvp_l0_flag and mvp_l1_flag
        42,
        //amvr_flag
        CNU, CNU,
        //amvr_precision_idx
        35,  34,  35,
        //bcw_idx
        CNU,
        //cu_coded_flag
         6,
        //cu_sbt_flag
        CNU, CNU,
        //cu_sbt_quad_flag
        CNU,
        //cu_sbt_horizontal_flag
        CNU, CNU, CNU,
        //cu_sbt_pos_flag
        CNU,
        //lfnst_idx
        28,  52,  42,
        //mts_idx
        29,   0,  28,   0,
        //copy_above_palette_indices_flag
        42,
        //palette_transpose_flag
        42,
        //run_copy_flag
        50,  37,  45,  30,  46,  45,  38,  46,
        //regular_merge_flag
        CNU, CNU,
        //mmvd_merge_flag
        CNU,
        //mmvd_cand_flag
        CNU,
        //mmvd_distance_idx
        CNU,
        //ciip_flag
        CNU,
        //merge_subblock_flag
        CNU, CNU, CNU,
        //merge_subblock_idx
        CNU,
        //merge_idx, merge_gpm_idx0, and merge_gpm_idx1
        34,
        //abs_mvd_greater0_flag
        14,
        //abs_mvd_greater1_flag
        45,
        //tu_y_coded_flag
        15,  12,   5,   7,
        //tu_cb_coded_flag
        12,  21,
        //tu_cr_coded_flag
        33,  28,  36,
        //cu_qp_delta_abs
        CNU, CNU,
        //cu_chroma_qp_offset_flag
        CNU,
        //cu_chroma_qp_offset_idx
        CNU,
        //transform_skip_flag
        25,   9,
        //tu_joint_cbcr_residual_flag
        12,  21,  35,
        //last_sig_coeff_x_prefix
        13,   5,   4,  21,  14,   4,   6,  14,  21,  11,  14,   7,  14,   5,  11,  21,
        30,  22,  13,  42,  12,   4,   3,
        //last_sig_coeff_y_prefix
        13,   5,   4,   6,  13,  11,  14,   6,   5,   3,  14,  22,   6,   4,   3,   6,
        22,  29,  20,  34,  12,   4,   3,
        //sb_coded_flag
        18,  31,  25,  15,  18,  20,  38,
        //sig_coeff_flag
        25,  19,  28,  14,  25,  20,  29,  30,  19,  37,  30,  38,  11,  38,  46,  54,
        27,  39,  39,  39,  44,  39,  39,  39,  18,  39,  39,  39,  27,  39,  39,  39,
         0,  39,  39,  39,  25,  27,  28,  37,  34,  53,  53,  46,  19,  46,  38,  39,
        52,  39,  39,  39,  11,  39,  39,  39,  19,  39,  39,  39,  25,  28,  38,
        //par_level_flag
        33,  25,  18,  26,  34,  27,  25,  26,  19,  42,  35,  33,  19,  27,  35,  35,
        34,  42,  20,  43,  20,  33,  25,  26,  42,  19,  27,  26,  50,  35,  20,  43,
        11,
        //abs_level_gtx_flag
        25,  25,  11,  27,  20,  21,  33,  12,  28,  21,  22,  34,  28,  29,  29,  30,
        36,  29,  45,  30,  23,  40,  33,  27,  28,  21,  37,  36,  37,  45,  38,  46,
        25,   1,  40,  25,  33,  11,  17,  25,  25,  18,   4,  17,  33,  26,  19,  13,
        33,  19,  20,  28,  22,  40,   9,  25,  18,  26,  35,  25,  26,  35,  28,  37,
        11,   5,   5,  14,  10,   3,   3,   3,
        //coeff_sign_flag
        12,  17,  46,  28,  25,  46,
    },
    {
        //alf_ctb_flag
        13,  23,  46,   4,  61,  54,  19,  46,  54,
        //alf_use_aps_flag
        46,
        //alf_ctb_cc_cb_idc
        18,  21,  38,
        //alf_ctb_cc_cr_idc
        18,  21,  38,
        //alf_ctb_filter_alt_idx
        20,  12,
        //sao_merge_left_flag and sao_merge_up_flag
        60,
        //sao_type_idx_luma and sao_type_idx_chroma
        5,
        //split_cu_flag
        11,  35,  53,  12,   6,  30,  13,  15,  31,
        //split_qt_flag
        20,  14,  23,  18,  19,   6,
        //mtt_split_cu_vertical_flag
        43,  35,  37,  34,  52,
        //mtt_split_cu_binary_flag
        43,  37,  21,  22,
        //non_inter_flag
        25,  12,
        //cu_skip_flag
        57,  59,  45,
        //pred_mode_ibc_flag
         0,  57,  44,
        //pred_mode_flag
        40,  35,
        //pred_mode_plt_flag
        0,
        //cu_act_enabled_flag
        46,
        //intra_bdpcm_luma_flag
        40,
        //intra_bdpcm_luma_dir_flag
        36,
        //intra_mip_flag
        41,  57,  58,  26,
        //intra_luma_ref_idx
        25,  58,
        //intra_subpartitions_mode_flag
        33,
        //intra_subpartitions_split_flag
        36,
        //intra_luma_mpm_flag
        36,
        //intra_luma_not_planar_flag
        12,  20,
        //intra_bdpcm_chroma_flag
         0,
        //intra_bdpcm_chroma_dir_flag
        13,
        //cclm_mode_flag
        34,
        //cclm_mode_idx
        27,
        //intra_chroma_pred_mode
        25,
        //general_merge_flag
        21,
        //inter_pred_idc
         7,   6,   5,  12,   4,  40,
        //inter_affine_flag
        12,  13,  14,
        //cu_affine_type_flag
        35,
        //sym_mvd_flag
        28,
        //ref_idx_l0 and ref_idx_l1
        20,  35,
        //mvp_l0_flag and mvp_l1_flag
        34,
        //amvr_flag
        59,  58,
        //amvr_precision_idx
        60,  48,  60,
        //bcw_idx
         4,
        //cu_coded_flag
         5,
        //cu_sbt_flag
        56,  57,
        //cu_sbt_quad_flag
        42,
        //cu_sbt_horizontal_flag
        20,  43,  12,
        //cu_sbt_pos_flag
        28,
        //lfnst_idx
        37,  45,  27,
        //mts_idx
        45,  40,  27,   0,
        //copy_above_palette_indices_flag
        59,
        //palette_transpose_flag
        42,
        //run_copy_flag
        51,  30,  30,  38,  23,  38,  53,  46,
        //regular_merge_flag
        38,   7,
        //mmvd_merge_flag
        26,
        //mmvd_cand_flag
        43,
        //mmvd_distance_idx
        60,
        //ciip_flag
        57,
        //merge_subblock_flag
        48,  57,  44,
        //merge_subblock_idx
         5,
        //merge_idx, merge_gpm_idx0, and merge_gpm_idx1
        20,
        //abs_mvd_greater0_flag
        44,
        //abs_mvd_greater1_flag
        43,
        //tu_y_coded_flag
        23,   5,  20,   7,
        //tu_cb_coded_flag
        25,  28,
        //tu_cr_coded_flag
        25,  29,  45,
        //cu_qp_delta_abs
        CNU, CNU,
        //cu_chroma_qp_offset_flag
        CNU,
        //cu_chroma_qp_offset_idx
        CNU,
        //transform_skip_flag
        25,   9,
        //tu_joint_cbcr_residual_flag
        27,  36,  45,
        //last_sig_coeff_x_prefix
         6,  13,  12,   6,   6,  12,  14,  14,  13,  12,  29,   7,   6,  13,  36,  28,
        14,  13,   5,  26,  12,   4,  18,
        //last_sig_coeff_y_prefix
         5,   5,  12,   6,   6,   4,   6,  14,   5,  12,  14,   7,  13,   5,  13,  21,
        14,  20,  12,  34,  11,   4,  18,
        //sb_coded_flag
        25,  30,  25,  45,  18,  12,  29,
        //sig_coeff_flag
        17,  41,  42,  29,  25,  49,  43,  37,  33,  58,  51,  30,  19,  38,  38,  46,
        34,  54,  54,  39,   6,  39,  39,  39,  19,  39,  54,  39,  19,  39,  39,  39,
        56,  39,  39,  39,  17,  34,  35,  21,  41,  59,  60,  38,  35,  45,  53,  54,
        44,  39,  39,  39,  34,  38,  62,  39,  26,  39,  39,  39,  40,  35,  44,
        //par_level_flag
        18,  17,  33,  18,  26,  42,  25,  33,  26,  42,  27,  25,  34,  42,  42,  35,
        26,  27,  42,  20,  20,  25,  25,  26,  11,  19,  27,  33,  42,  35,  35,  43,
         3,
        //abs_level_gtx_flag
         0,  17,  26,  19,  35,  21,  25,  34,  20,  28,  29,  33,  27,  28,  29,  22,
        34,  28,  44,  37,  38,   0,  25,  19,  20,  13,  14,  57,  44,  30,  30,  23,
        17,   0,   1,  17,  25,  18,   0,   9,  25,  33,  34,   9,  25,  18,  26,  20,
        25,  18,  19,  27,  29,  17,   9,  25,  10,  18,   4,  17,  33,  19,  20,  29,
        18,  11,   4,  28,   2,  10,   3,   3,
        //coeff_sign_flag
         5,  10,  53,  43,  25,  46,
    },
    {
        //alf_ctb_flag
        33,  52,  46,  25,  61,  54,  25,  61,  54,
        //alf_use_aps_flag
        46,
        //alf_ctb_cc_cb_idc
        25,  35,  38,
        //alf_ctb_cc_cr_idc
        25,  28,  38,
        //alf_ctb_filter_alt_idx
        11,  26,
        //sao_merge_left_flag and sao_merge_up_flag
        2,
        //sao_type_idx_luma and sao_type_idx_chroma
        2,
        //split_cu_flag
        18,  27,  15,  18,  28,  45,  26,   7,  23,
        //split_qt_flag
        26,  36,  38,  18,  34,  21,
        //mtt_split_cu_vertical_flag
        43,  42,  37,  42,  44,
        //mtt_split_cu_binary_flag
        28,  29,  28,  29,
        //non_inter_flag
        25,  20,
        //cu_skip_flag
        57,  60,  46,
        //pred_mode_ibc_flag
         0,  43,  45,
        //pred_mode_flag
        40,  35,
        //pred_mode_plt_flag
        17,
        //cu_act_enabled_flag
        46,
        //intra_bdpcm_luma_flag
        19,
        //intra_bdpcm_luma_dir_flag
        21,
        //intra_mip_flag
        56,  57,  50,  26,
        //intra_luma_ref_idx
        25,  59,
        //intra_subpartitions_mode_flag
        33,
        //intra_subpartitions_split_flag
        43,
        //intra_luma_mpm_flag
        44,
        //intra_luma_not_planar_flag
        13,   6,
        //intra_bdpcm_chroma_flag
         0,
        //intra_bdpcm_chroma_dir_flag
        28,
        //cclm_mode_flag
        26,
        //cclm_mode_idx
        27,
        //intra_chroma_pred_mode
        25,
        //general_merge_flag
         6,
        //inter_pred_idc
        14,  13,   5,   4,   3,  40,
        //inter_affine_flag
        19,  13,   6,
        //cu_affine_type_flag
        35,
        //sym_mvd_flag
        28,
        //ref_idx_l0 and ref_idx_l1
         5,  35,
        //mvp_l0_flag and mvp_l1_flag
        34,
        //amvr_flag
        59,  50,
        //amvr_precision_idx
        38,  26,  60,
        //bcw_idx
         5,
        //cu_coded_flag
        12,
        //cu_sbt_flag
        41,  57,
        //cu_sbt_quad_flag
        42,
        //cu_sbt_horizontal_flag
        35,  51,  27,
        //cu_sbt_pos_flag
        28,
        //lfnst_idx
        52,  37,  27,
        //mts_idx
        45,  25,  27,   0,
        //copy_above_palette_indices_flag
        50,
        //palette_transpose_flag
        35,
        //run_copy_flag
        58,  45,  45,  30,  38,  45,  38,  46,
        //regular_merge_flag
        46,  15,
        //mmvd_merge_flag
        25,
        //mmvd_cand_flag
        43,
        //mmvd_distance_idx
        59,
        //ciip_flag
        57,
        //merge_subblock_flag
        25,  58,  45,
        //merge_subblock_idx
         4,
        //merge_idx, merge_gpm_idx0, and merge_gpm_idx1
        18,
        //abs_mvd_greater0_flag
        51,
        //abs_mvd_greater1_flag
        36,
        //tu_y_coded_flag
        15,   6,   5,  14,
        //tu_cb_coded_flag
        25,  37,
        //tu_cr_coded_flag
         9,  36,  45,
        //cu_qp_delta_abs
        CNU, CNU,
        //cu_chroma_qp_offset_flag
        CNU,
        //cu_chroma_qp_offset_idx
        CNU,
        //transform_skip_flag
        25,  17,
        //tu_joint_cbcr_residual_flag
        42,  43,  52,
        //last_sig_coeff_x_prefix
         6,   6,  12,  14,   6,   4,  14,   7,   6,   4,  29,   7,   6,   6,  12,  28,
         7,  13,  13,  35,  19,   5,   4,
        //last_sig_coeff_y_prefix
         5,   5,  20,  13,  13,  19,  21,   6,  12,  12,  14,  14,   5,   4,  12,  13,
         7,  13,  12,  41,  11,   5,  27,
        //sb_coded_flag
        25,  45,  25,  14,  18,  35,  45,
        //sig_coeff_flag
        17,  41,  49,  36,   1,  49,  50,  37,  48,  51,  58,  45,  26,  45,  53,  46,
        49,  54,  61,  39,  35,  39,  39,  39,  19,  54,  39,  39,  50,  39,  39,  39,
         0,  39,  39,  39,   9,  49,  50,  36,  48,  59,  59,  38,  34,  45,  38,  31,
        58,  39,  39,  39,  34,  38,  54,  39,  41,  39,  39,  39,  25,  50,  37,
        //par_level_flag
        33,  40,  25,  41,  26,  42,  25,  33,  26,  34,  27,  25,  41,  42,  42,  35,
        33,  27,  35,  42,  43,  33,  25,  26,  34,  19,  27,  33,  42,  43,  35,  43,
        11,
        //abs_level_gtx_flag
         0,   0,  33,  34,  35,  21,  25,  34,  35,  28,  29,  40,  42,  43,  29,  30,
        49,  36,  37,  45,  38,   0,  40,  34,  43,  36,  37,  57,  52,  45,  38,  46,
        25,   0,   0,  17,  25,  26,   0,   9,  25,  33,  19,   0,  25,  33,  26,  20,
        25,  33,  27,  35,  22,  25,   1,  25,  33,  26,  12,  25,  33,  27,  28,  37,
        19,  11,   4,   6,   3,   4,   4,   5,
        //coeff_sign_flag
        35,  25,  46,  28,  33,  38,
    },
    //shiftIdx
    {
        //alf_ctb_flag
         0,   0,   0,   4,   0,   0,   1,   0,   0,
        //alf_use_aps_flag
         0,
        //alf_ctb_cc_cb_idc
         4,   1,   4,
        //alf_ctb_cc_cr_idc
         4,   1,   4,
        //alf_ctb_filter_alt_idx
         0,   0,
        //sao_merge_left_flag and sao_merge_up_flag
         0,
        //sao_type_idx_luma and sao_type_idx_chroma
         4,
        //split_cu_flag
        12,  13,   8,   8,  13,  12,   5,   9,   9,
        //split_qt_flag
         0,   8,   8,  12,  12,   8,
        //mtt_split_cu_vertical_flag
         9,   8,   9,   8,   5,
        //mtt_split_cu_binary_flag
        12,  13,  12,  13,
        //non_inter_flag
         1,   0,
        //cu_skip_flag
         5,   4,   8,
        //pred_mode_ibc_flag
         1,   5,   8,
        //pred_mode_flag
         5,   1,
        //pred_mode_plt_flag
         1,
        //cu_act_enabled_flag
         1,
        //intra_bdpcm_luma_flag
         1,
        //intra_bdpcm_luma_dir_flag
         4,
        //intra_mip_flag
         9,  10,   9,   6,
        //intra_luma_ref_idx
         5,   8,
        //intra_subpartitions_mode_flag
         9,
        //intra_subpartitions_split_flag
         2,
        //intra_luma_mpm_flag
         6,
        //intra_luma_not_planar_flag
         1,   5,
        //intra_bdpcm_chroma_flag
         1,
        //intra_bdpcm_chroma_dir_flag
         0,
        //cclm_mode_flag
         4,
        //cclm_mode_idx
         9,
        //intra_chroma_pred_mode
         5,
        //general_merge_flag
         4,
        //inter_pred_idc
         0,   0,   1,   4,   4,   0,
        //inter_affine_flag
         4,   0,   0,
        //cu_affine_type_flag
         4,
        //sym_mvd_flag
         5,
        //ref_idx_l0 and ref_idx_l1
         0,   4,
        //mvp_l0_flag and mvp_l1_flag
        12,
        //amvr_flag
         0,   0,
        //amvr_precision_idx
         4,   5,  0,
        //bcw_idx
         1,
        //cu_coded_flag
         4,
        //cu_sbt_flag
         1,   5,
        //cu_sbt_quad_flag
        10,
        //cu_sbt_horizontal_flag
         8,   4,   1,
        //cu_sbt_pos_flag
        13,
        //lfnst_idx
         9,   9,  10,
        //mts_idx
         8,   0,   9,   0,
        //copy_above_palette_indices_flag
         9,
        //palette_transpose_flag
         5,
        //run_copy_flag
         9,   6,   9,  10,   5,   0,   9,   5,
        //regular_merge_flag
         5,   5,
        //mmvd_merge_flag
         4,
        //mmvd_cand_flag
        10,
        //mmvd_distance_idx
         0,
        //ciip_flag
         1,
        //merge_subblock_flag
         4,   4,   4,
        //merge_subblock_idx
         0,
        //merge_idx, merge_gpm_idx0, and merge_gpm_idx1
         4,
        //abs_mvd_greater0_flag
         9,
        //abs_mvd_greater1_flag
         5,
        //tu_y_coded_flag
         5,   1,   8,   9,
        //tu_cb_coded_flag
         5,   0,
        //tu_cr_coded_flag
         2,   1,   0,
        //cu_qp_delta_abs
         8,   8,
        //cu_chroma_qp_offset_flag
         8,
        //cu_chroma_qp_offset_idx
         8,
        //transform_skip_flag
         1,   1,
        //tu_joint_cbcr_residual_flag
         1,   1,   0,
        //last_sig_coeff_x_prefix
         8,   5,   4,   5,   4,   4,   5,   4,   1,   0,   4,   1,   0,   0,   0,   0,
         1,   0,   0,   0,   5,   4,   4,
        //last_sig_coeff_y_prefix
         8,   5,   8,   5,   5,   4,   5,   5,   4,   0,   5,   4,   1,   0,   0,   1,
         4,   0,   0,   0,   6,   5,   5,
        //sb_coded_flag
         8,   5,   5,   8,   5,   8,   8,
        //sig_coeff_flag
        12,   9,   9,  10,   9,   9,   9,  10,   8,   8,   8,  10,   9,  13,   8,   8,
         8,   8,   8,   5,   8,   0,   0,   0,   8,   8,   8,   8,   8,   0,   4,   4,
         0,   0,   0,   0,  12,  12,   9,  13,   4,   5,   8,   9,   8,  12,  12,   8,
         4,   0,   0,   0,   8,   8,   8,   8,   4,   0,   0,   0,  13,  13,   8,
        //par_level_flag
         8,   9,  12,  13,  13,  13,  10,  13,  13,  13,  13,  13,  13,  13,  13,  13,
        10,  13,  13,  13,  13,   8,  12,  12,  12,  13,  13,  13,  13,  13,  13,  13,
         6,
        //abs_level_gtx_flag
         9,   5,  10,  13,  13,  10,   9,  10,  13,  13,  13,   9,  10,  10,  10,  13,
         8,   9,  10,  10,  13,   8,   8,   9,  12,  12,  10,   5,   9,   9,   9,  13,
         1,   5,   9,   9,   9,   6,   5,   9,  10,  10,   9,   9,   9,   9,   9,   9,
         6,   8,   9,   9,  10,   1,   5,   8,   8,   9,   6,   6,   9,   8,   8,   9,
         4,   2,   1,   6,   1,   1,   1,   1,
        //coeff_sign_flag
         1,   4,   4,   5,   8,   8,
    }
};

#define MAX_SUB_BLOCKS 16
#define MAX_SUB_BLOCK_SIZE 4
#define MAX_TB_SIZE 64

typedef struct ResidualCoding {
    //common for ts and non ts
    TransformBlock *tb;

    int log2_sb_w;
    int log2_sb_h;
    int last_sub_block;
    int hist_value;
    int update_hist;
    int num_sb_coeff;
    int rem_bins_pass1;

    int width_in_sbs;
    int height_in_sbs;
    int nb_sbs;

    const uint8_t *sb_scan_x_off;
    const uint8_t *sb_scan_y_off;
    const uint8_t *scan_x_off;
    const uint8_t *scan_y_off;

    uint8_t sb_coded_flag[MAX_SUB_BLOCKS * MAX_SUB_BLOCKS];
    int sig_coeff_flag[MAX_TB_SIZE * MAX_TB_SIZE];
    int abs_level_pass1[MAX_TB_SIZE * MAX_TB_SIZE];              ///< AbsLevelPass1[][]
    int abs_level[MAX_TB_SIZE * MAX_TB_SIZE];

    //for ts only
    uint8_t infer_sb_cbf;
    int coeff_sign_level[MAX_TB_SIZE * MAX_TB_SIZE];             ///< CoeffSignLevel[][]

    //for non ts only
    int qstate;
    int last_scan_pos;
    int last_significant_coeff_x;
    int last_significant_coeff_y;
} ResidualCoding;

static int cabac_reinit(VVCLocalContext *lc)
{
    return skip_bytes(&lc->ep->cc, 0) == NULL ? AVERROR_INVALIDDATA : 0;
}

static void cabac_init_state(VVCLocalContext *lc)
{
    const VVCSPS *sps             = lc->fc->ps.sps;
    const H266RawSliceHeader *rsh = lc->sc->sh.r;
    const int qp                  = av_clip_uintp2(lc->sc->sh.slice_qp_y, 6);
    int init_type                 = 2 - rsh->sh_slice_type;

    av_assert0(VVC_CONTEXTS == SYNTAX_ELEMENT_LAST);

    ff_vvc_ep_init_stat_coeff(lc->ep, sps->bit_depth, sps->r->sps_persistent_rice_adaptation_enabled_flag);

    if (rsh->sh_cabac_init_flag && !IS_I(rsh))
        init_type ^= 3;

    for (int i = 0; i < VVC_CONTEXTS; i++) {
        VVCCabacState *state = &lc->ep->cabac_state[i];
        const int init_value = init_values[init_type][i];
        const int shift_idx  = init_values[3][i];
        const int m = (init_value >> 3) - 4;
        const int n = ((init_value & 7) * 18) + 1;
        const int pre = av_clip(((m * (qp - 16)) >> 1) + n, 1, 127);

        state->state[0] = pre << 3;
        state->state[1] = pre << 7;
        state->shift[0] = (shift_idx >> 2 ) + 2;
        state->shift[1] = (shift_idx & 3 ) + 3 + state->shift[0];
    }
}

int ff_vvc_cabac_init(VVCLocalContext *lc,
    const int ctu_idx, const int rx, const int ry)
{
    int ret = 0;
    const VVCPPS *pps            = lc->fc->ps.pps;
    const int first_ctb_in_slice = !ctu_idx;
    const int first_ctb_in_tile  = rx == pps->ctb_to_col_bd[rx] && ry == pps->ctb_to_row_bd[ry];

    if (first_ctb_in_slice|| first_ctb_in_tile) {
        if (lc->sc->nb_eps == 1 && !first_ctb_in_slice)
            ret = cabac_reinit(lc);
        if (!ret)
            cabac_init_state(lc);
    }
    return ret;
}

//fixme
static void vvc_refill2(CABACContext* c) {
    int i;
    unsigned x;
#if !HAVE_FAST_CLZ
    x = c->low ^ (c->low - 1);
    i = 7 - ff_h264_norm_shift[x >> (CABAC_BITS - 1)];
#else
    i = ff_ctz(c->low) - CABAC_BITS;
#endif

    x = -CABAC_MASK;

#if CABAC_BITS == 16
    x += (c->bytestream[0] << 9) + (c->bytestream[1] << 1);
#else
    x += c->bytestream[0] << 1;
#endif

    c->low += x << i;
#if !UNCHECKED_BITSTREAM_READER
    if (c->bytestream < c->bytestream_end)
#endif
        c->bytestream += CABAC_BITS / 8;
}

static int inline vvc_get_cabac(CABACContext *c, VVCCabacState* base, const int ctx)
{
    VVCCabacState *s = base + ctx;
    const int qRangeIdx = c->range >> 5;
    const int pState = s->state[1] + (s->state[0] << 4);
    const int valMps = pState >> 14;
    const int RangeLPS = (qRangeIdx * ((valMps ? 32767 - pState : pState) >> 9 ) >> 1) + 4;
    int bit, lps_mask;

    c->range -= RangeLPS;
    lps_mask = ((c->range<<(CABAC_BITS+1)) - c->low)>>31;

    c->low -= (c->range<<(CABAC_BITS+1)) & lps_mask;
    c->range += (RangeLPS - c->range) & lps_mask;

    bit = valMps ^ (lps_mask & 1);

    lps_mask = ff_h264_norm_shift[c->range];
    c->range <<= lps_mask;
    c->low  <<= lps_mask;

    if (!(c->low & CABAC_MASK))
        vvc_refill2(c);
    s->state[0] = s->state[0] - (s->state[0] >> s->shift[0]) + (1023 * bit >> s->shift[0]);
    s->state[1] = s->state[1] - (s->state[1] >> s->shift[1]) + (16383 * bit >> s->shift[1]);
    return bit;
}

#define GET_CABAC(ctx) vvc_get_cabac(&lc->ep->cc, lc->ep->cabac_state, ctx)

//9.3.3.4 Truncated binary (TB) binarization process
static int truncated_binary_decode(VVCLocalContext *lc, const int c_max)
{
    const int n = c_max + 1;
    const int k = av_log2(n);
    const int u = (1 << (k+1)) - n;
    int v = 0;
    for (int i = 0; i < k; i++)
        v = (v << 1) | get_cabac_bypass(&lc->ep->cc);
    if (v >= u) {
        v = (v << 1) | get_cabac_bypass(&lc->ep->cc);
        v -= u;
    }
    return v;
}

// 9.3.3.6 Limited k-th order Exp-Golomb binarization process
static int limited_kth_order_egk_decode(CABACContext *c, const int k, const int max_pre_ext_len, const int trunc_suffix_len)
{
    int pre_ext_len = 0;
    int escape_length;
    int val = 0;
    while ((pre_ext_len < max_pre_ext_len) && get_cabac_bypass(c))
        pre_ext_len++;
    if (pre_ext_len == max_pre_ext_len)
        escape_length = trunc_suffix_len;
    else
        escape_length = pre_ext_len + k;
    while (escape_length-- > 0) {
        val = (val << 1) + get_cabac_bypass(c);
    }
    val += ((1 << pre_ext_len) - 1) << k;
    return val;
}

static av_always_inline
void get_left_top(const VVCLocalContext *lc, uint8_t *left, uint8_t *top,
    const int x0, const int y0, const uint8_t *left_ctx, const uint8_t *top_ctx)
{
    const VVCFrameContext *fc = lc->fc;
    const VVCSPS *sps         = fc->ps.sps;
    const int min_cb_width    = fc->ps.pps->min_cb_width;
    const int x0b = av_zero_extend(x0, sps->ctb_log2_size_y);
    const int y0b = av_zero_extend(y0, sps->ctb_log2_size_y);
    const int x_cb = x0 >> sps->min_cb_log2_size_y;
    const int y_cb = y0 >> sps->min_cb_log2_size_y;

    if (lc->ctb_left_flag || x0b)
        *left = SAMPLE_CTB(left_ctx, x_cb - 1, y_cb);
    if (lc->ctb_up_flag || y0b)
        *top = SAMPLE_CTB(top_ctx, x_cb, y_cb - 1);
}

static av_always_inline
uint8_t get_inc(VVCLocalContext *lc, const uint8_t *ctx)
{
    uint8_t left = 0, top = 0;
    get_left_top(lc, &left, &top, lc->cu->x0, lc->cu->y0, ctx, ctx);
    return left + top;
}

int ff_vvc_sao_merge_flag_decode(VVCLocalContext *lc)
{
    return GET_CABAC(SAO_MERGE_FLAG);
}

int ff_vvc_sao_type_idx_decode(VVCLocalContext *lc)
{
    if (!GET_CABAC(SAO_TYPE_IDX))
        return SAO_NOT_APPLIED;

    if (!get_cabac_bypass(&lc->ep->cc))
        return SAO_BAND;
    return SAO_EDGE;
}

int ff_vvc_sao_band_position_decode(VVCLocalContext *lc)
{
    int value = get_cabac_bypass(&lc->ep->cc);

    for (int i = 0; i < 4; i++)
        value = (value << 1) | get_cabac_bypass(&lc->ep->cc);
    return value;
}

int ff_vvc_sao_offset_abs_decode(VVCLocalContext *lc)
{
    int i = 0;
    const int length = (1 << (FFMIN(lc->fc->ps.sps->bit_depth, 10) - 5)) - 1;

    while (i < length && get_cabac_bypass(&lc->ep->cc))
        i++;
    return i;
}

int ff_vvc_sao_offset_sign_decode(VVCLocalContext *lc)
{
    return get_cabac_bypass(&lc->ep->cc);
}

int ff_vvc_sao_eo_class_decode(VVCLocalContext *lc)
{
    int ret = get_cabac_bypass(&lc->ep->cc) << 1;
    ret    |= get_cabac_bypass(&lc->ep->cc);
    return ret;
}

int ff_vvc_alf_ctb_flag(VVCLocalContext *lc, const int rx, const int ry, const int c_idx)
{
    int inc = c_idx * 3;
    const VVCFrameContext *fc = lc->fc;
    if (lc->ctb_left_flag) {
        const ALFParams *left = &CTB(fc->tab.alf, rx - 1, ry);
        inc += left->ctb_flag[c_idx];
    }
    if (lc->ctb_up_flag) {
        const ALFParams *above = &CTB(fc->tab.alf, rx, ry - 1);
        inc += above->ctb_flag[c_idx];
    }
    return GET_CABAC(ALF_CTB_FLAG + inc);
}

int ff_vvc_alf_use_aps_flag(VVCLocalContext *lc)
{
    return GET_CABAC(ALF_USE_APS_FLAG);
}

int ff_vvc_alf_luma_prev_filter_idx(VVCLocalContext *lc)
{
    return truncated_binary_decode(lc, lc->sc->sh.r->sh_num_alf_aps_ids_luma - 1);
}

int ff_vvc_alf_luma_fixed_filter_idx(VVCLocalContext *lc)
{
    return truncated_binary_decode(lc, 15);
}

int ff_vvc_alf_ctb_filter_alt_idx(VVCLocalContext *lc, const int c_idx, const int num_chroma_filters)
{
    int i = 0;
    const int length = num_chroma_filters - 1;

    while (i < length && GET_CABAC(ALF_CTB_FILTER_ALT_IDX + c_idx - 1))
        i++;
    return i;
}

int ff_vvc_alf_ctb_cc_idc(VVCLocalContext *lc, const int rx, const int ry, const int idx, const int cc_filters_signalled)
{
    int inc = !idx ? ALF_CTB_CC_CB_IDC : ALF_CTB_CC_CR_IDC;
    int i = 0;
    const VVCFrameContext *fc = lc->fc;
    if (lc->ctb_left_flag) {
        const ALFParams *left = &CTB(fc->tab.alf, rx - 1, ry);
        inc += left->ctb_cc_idc[idx] != 0;
    }
    if (lc->ctb_up_flag) {
        const ALFParams *above = &CTB(fc->tab.alf, rx, ry - 1);
        inc += above->ctb_cc_idc[idx] != 0;
    }

    if (!GET_CABAC(inc))
        return 0;
    i++;
    while (i < cc_filters_signalled && get_cabac_bypass(&lc->ep->cc))
        i++;
    return i;
}

int ff_vvc_split_cu_flag(VVCLocalContext *lc, const int x0, const int y0,
    const int cb_width, const int cb_height, const int is_chroma, const VVCAllowedSplit *a)
{
    const VVCFrameContext *fc = lc->fc;
    const VVCPPS *pps         = fc->ps.pps;
    const int is_inside       = (x0 + cb_width <= pps->width) && (y0 + cb_height <= pps->height);

    if ((a->btv || a->bth || a->ttv || a->tth || a->qt) && is_inside)
    {
        uint8_t inc = 0, left_height = cb_height, top_width = cb_width;

        get_left_top(lc, &left_height, &top_width, x0, y0, fc->tab.cb_height[is_chroma], fc->tab.cb_width[is_chroma]);
        inc += left_height < cb_height;
        inc += top_width   < cb_width;
        inc += (a->btv + a->bth + a->ttv + a->tth + 2 * a->qt - 1) / 2 * 3;

        return GET_CABAC(SPLIT_CU_FLAG + inc);

    }
    return !is_inside;
}

static int split_qt_flag_decode(VVCLocalContext *lc, const int x0, const int y0, const int ch_type, const int cqt_depth)
{
    const VVCFrameContext *fc = lc->fc;
    int inc = 0;
    uint8_t depth_left = 0, depth_top = 0;

    get_left_top(lc,  &depth_left, &depth_top, x0, y0, fc->tab.cqt_depth[ch_type], fc->tab.cqt_depth[ch_type]);
    inc += depth_left > cqt_depth;
    inc += depth_top  > cqt_depth;
    inc += (cqt_depth >= 2) * 3;

    return GET_CABAC(SPLIT_QT_FLAG + inc);
}

static int mtt_split_cu_vertical_flag_decode(VVCLocalContext *lc, const int x0, const int y0,
    const int cb_width, const int cb_height, const int ch_type, const VVCAllowedSplit* a)
{
    if ((a->bth || a->tth) && (a->btv || a->ttv)) {
        int inc;
        const int v = a->btv + a->ttv;
        const int h = a->bth + a->tth;
        if (v > h)
            inc = 4;
        else if (v < h)
            inc = 3;
        else {
            const VVCFrameContext *fc = lc->fc;
            const VVCSPS *sps         = fc->ps.sps;
            const int min_cb_width    = fc->ps.pps->min_cb_width;
            const int x0b             = av_zero_extend(x0, sps->ctb_log2_size_y);
            const int y0b             = av_zero_extend(y0, sps->ctb_log2_size_y);
            const int x_cb            = x0 >> sps->min_cb_log2_size_y;
            const int y_cb            = y0 >> sps->min_cb_log2_size_y;
            const int available_a     = lc->ctb_up_flag || y0b;
            const int available_l     = lc->ctb_left_flag || x0b;
            const int da              = cb_width  / (available_a ? SAMPLE_CTB(fc->tab.cb_width[ch_type], x_cb, y_cb - 1) : 1);
            const int dl              = cb_height / (available_l ? SAMPLE_CTB(fc->tab.cb_height[ch_type], x_cb - 1, y_cb) : 1);

            if (da == dl || !available_a || !available_l)
                inc = 0;
            else if (da < dl)
                inc = 1;
            else
                inc = 2;
        }
        return GET_CABAC(MTT_SPLIT_CU_VERTICAL_FLAG + inc);
    }
    return !(a->bth || a->tth);
}

static int mtt_split_cu_binary_flag_decode(VVCLocalContext *lc, const int mtt_split_cu_vertical_flag, const int mtt_depth)
{
    const int inc = (2 * mtt_split_cu_vertical_flag) + ((mtt_depth <= 1) ? 1 : 0);
    return GET_CABAC(MTT_SPLIT_CU_BINARY_FLAG + inc);
}

VVCSplitMode ff_vvc_split_mode(VVCLocalContext *lc, const int x0, const int y0, const int cb_width, const int cb_height,
    const int cqt_depth, const int mtt_depth, const int ch_type, const VVCAllowedSplit *a)
{
    const int allow_no_qt = a->btv || a->bth || a->ttv || a->tth;
    int split_qt_flag;
    int mtt_split_cu_vertical_flag;
    int mtt_split_cu_binary_flag;
    const VVCSplitMode mtt_split_modes[] = {
        SPLIT_TT_HOR, SPLIT_BT_HOR, SPLIT_TT_VER, SPLIT_BT_VER,
    };
    if (allow_no_qt && a->qt) {
        split_qt_flag = split_qt_flag_decode(lc, x0, y0, ch_type, cqt_depth);
    } else {
        split_qt_flag = !allow_no_qt || a->qt;
    }
    if (split_qt_flag)
        return SPLIT_QT;
    mtt_split_cu_vertical_flag = mtt_split_cu_vertical_flag_decode(lc, x0, y0, cb_width, cb_height, ch_type, a);
    if ((a->btv && a->ttv && mtt_split_cu_vertical_flag) ||
        (a->bth && a->tth && !mtt_split_cu_vertical_flag)) {
        mtt_split_cu_binary_flag = mtt_split_cu_binary_flag_decode(lc, mtt_split_cu_vertical_flag, mtt_depth);
    } else {
        if (!a->btv && !a->bth)
            mtt_split_cu_binary_flag = 0;
        else if (!a->ttv && !a->tth)
            mtt_split_cu_binary_flag = 1;
        else if (a->bth && a->ttv)
            mtt_split_cu_binary_flag = 1 - mtt_split_cu_vertical_flag;
        else
            mtt_split_cu_binary_flag = mtt_split_cu_vertical_flag;
    }
    return mtt_split_modes[(mtt_split_cu_vertical_flag << 1) + mtt_split_cu_binary_flag];
}

int ff_vvc_non_inter_flag(VVCLocalContext *lc, const int x0, const int y0, const int ch_type)
{
    const VVCFrameContext *fc = lc->fc;
    uint8_t inc, left = MODE_INTER, top = MODE_INTER;

    get_left_top(lc, &left, &top, x0, y0, fc->tab.cpm[ch_type], fc->tab.cpm[ch_type]);
    inc = left == MODE_INTRA || top == MODE_INTRA;
    return GET_CABAC(NON_INTER_FLAG + inc);
}

int ff_vvc_pred_mode_flag(VVCLocalContext *lc, const int is_chroma)
{
    const VVCFrameContext *fc = lc->fc;
    const CodingUnit *cu      = lc->cu;
    uint8_t inc, left = MODE_INTER, top = MODE_INTER;

    get_left_top(lc, &left, &top, cu->x0, cu->y0, fc->tab.cpm[is_chroma], fc->tab.cpm[is_chroma]);
    inc = left == MODE_INTRA || top == MODE_INTRA;
    return GET_CABAC(PRED_MODE_FLAG + inc);
}

int ff_vvc_pred_mode_plt_flag(VVCLocalContext *lc)
{
    return GET_CABAC(PRED_MODE_PLT_FLAG);
}

int ff_vvc_intra_bdpcm_luma_flag(VVCLocalContext *lc)
{
    return GET_CABAC(INTRA_BDPCM_LUMA_FLAG);
}

int ff_vvc_intra_bdpcm_luma_dir_flag(VVCLocalContext *lc)
{
    return GET_CABAC(INTRA_BDPCM_LUMA_DIR_FLAG);
}

int ff_vvc_intra_bdpcm_chroma_flag(VVCLocalContext *lc)
{
    return GET_CABAC(INTRA_BDPCM_CHROMA_FLAG);
}

int ff_vvc_intra_bdpcm_chroma_dir_flag(VVCLocalContext *lc)
{
    return GET_CABAC(INTRA_BDPCM_CHROMA_DIR_FLAG);
}

int ff_vvc_cu_skip_flag(VVCLocalContext *lc, const uint8_t *cu_skip_flag)
{
    const int inc = get_inc(lc, cu_skip_flag);
    return GET_CABAC(CU_SKIP_FLAG + inc);
}

int ff_vvc_pred_mode_ibc_flag(VVCLocalContext *lc, const int is_chroma)
{
    const VVCFrameContext *fc = lc->fc;
    const CodingUnit *cu      = lc->cu;
    uint8_t left_mode = MODE_INTER, top_mode = MODE_INTER;
    int inc;

    get_left_top(lc, &left_mode, &top_mode, cu->x0, cu->y0, fc->tab.cpm[is_chroma], fc->tab.cpm[is_chroma]);
    inc = (left_mode == MODE_IBC) + (top_mode == MODE_IBC);
    return GET_CABAC(PRED_MODE_IBC_FLAG + inc);
}

int ff_vvc_intra_mip_flag(VVCLocalContext *lc, const uint8_t *intra_mip_flag)
{
    const int w   = lc->cu->cb_width;
    const int h   = lc->cu->cb_height;
    const int inc =  (w > h * 2 || h > w * 2) ? 3 : get_inc(lc, intra_mip_flag);
    return GET_CABAC(INTRA_MIP_FLAG + inc);
}

int ff_vvc_intra_mip_transposed_flag(VVCLocalContext *lc)
{
    return get_cabac_bypass(&lc->ep->cc);
}

int ff_vvc_intra_mip_mode(VVCLocalContext *lc)
{
    const int w     = lc->cu->cb_width;
    const int h     = lc->cu->cb_height;
    const int c_max = (w == 4 && h == 4) ? 15 :
        ((w == 4 || h == 4) || (w == 8 && h == 8)) ? 7: 5;
    return truncated_binary_decode(lc, c_max);
}

int ff_vvc_intra_luma_ref_idx(VVCLocalContext *lc)
{
    int i;
    for (i = 0; i < 2; i++) {
        if (!GET_CABAC(INTRA_LUMA_REF_IDX + i))
            return i;
    }
    return i;
}

int ff_vvc_intra_subpartitions_mode_flag(VVCLocalContext *lc)
{
    return GET_CABAC(INTRA_SUBPARTITIONS_MODE_FLAG);
}

enum IspType ff_vvc_isp_split_type(VVCLocalContext *lc, const int intra_subpartitions_mode_flag)
{
    if (!intra_subpartitions_mode_flag)
        return ISP_NO_SPLIT;
    return 1 + GET_CABAC(INTRA_SUBPARTITIONS_SPLIT_FLAG);
}

int ff_vvc_intra_luma_mpm_flag(VVCLocalContext *lc)
{
    return GET_CABAC(INTRA_LUMA_MPM_FLAG);
}

int ff_vvc_intra_luma_not_planar_flag(VVCLocalContext *lc, const int intra_subpartitions_mode_flag)
{
    return GET_CABAC(INTRA_LUMA_NOT_PLANAR_FLAG + !intra_subpartitions_mode_flag);
}

int ff_vvc_intra_luma_mpm_idx(VVCLocalContext *lc)
{
    int i;
    for (i = 0; i < 4 && get_cabac_bypass(&lc->ep->cc); i++)
        /* nothing */;
    return i;
}

int ff_vvc_intra_luma_mpm_remainder(VVCLocalContext *lc)
{
    return truncated_binary_decode(lc, 60);
}

int ff_vvc_cclm_mode_flag(VVCLocalContext *lc)
{
    return GET_CABAC(CCLM_MODE_FLAG);
}

int ff_vvc_cclm_mode_idx(VVCLocalContext *lc)
{
    if (!GET_CABAC(CCLM_MODE_IDX))
        return 0;
    return get_cabac_bypass(&lc->ep->cc) + 1;
}

int ff_vvc_intra_chroma_pred_mode(VVCLocalContext *lc)
{
    if (!GET_CABAC(INTRA_CHROMA_PRED_MODE))
        return 4;
    return (get_cabac_bypass(&lc->ep->cc) << 1) | get_cabac_bypass(&lc->ep->cc);
}

int ff_vvc_general_merge_flag(VVCLocalContext *lc)
{
    return GET_CABAC(GENERAL_MERGE_FLAG);
}

static int get_inter_flag_inc(VVCLocalContext *lc, const int x0, const int y0)
{
    uint8_t left_merge = 0,  top_merge = 0;
    uint8_t left_affine = 0, top_affine = 0;
    const VVCFrameContext *fc = lc->fc;

    get_left_top(lc, &left_merge, &top_merge, x0, y0, fc->tab.msf, fc->tab.msf);
    get_left_top(lc, &left_affine, &top_affine, x0, y0, fc->tab.iaf, fc->tab.iaf);
    return (left_merge || left_affine) + (top_merge + top_affine);
}

int ff_vvc_merge_subblock_flag(VVCLocalContext *lc)
{
    const int inc = get_inter_flag_inc(lc, lc->cu->x0, lc->cu->y0);
    return GET_CABAC(MERGE_SUBBLOCK_FLAG + inc);
}

int ff_vvc_merge_subblock_idx(VVCLocalContext *lc, const int max_num_subblock_merge_cand)
{
    int i;
    if (!GET_CABAC(MERGE_SUBBLOCK_IDX))
        return 0;
    for (i = 1; i < max_num_subblock_merge_cand - 1 && get_cabac_bypass(&lc->ep->cc); i++)
        /* nothing */;
    return i;
}

int ff_vvc_regular_merge_flag(VVCLocalContext *lc, const int cu_skip_flag)
{
    int inc = !cu_skip_flag;
    return GET_CABAC(REGULAR_MERGE_FLAG + inc);
}

int ff_vvc_mmvd_merge_flag(VVCLocalContext *lc)
{
    return GET_CABAC(MMVD_MERGE_FLAG);
}

int ff_vvc_mmvd_cand_flag(VVCLocalContext *lc)
{
    return GET_CABAC(MMVD_CAND_FLAG);
}

static int mmvd_distance_idx_decode(VVCLocalContext *lc)
{
    int i;
    if (!GET_CABAC(MMVD_DISTANCE_IDX))
        return 0;
    for (i = 1; i < 7 && get_cabac_bypass(&lc->ep->cc); i++)
        /* nothing */;
    return i;
}

static int mmvd_direction_idx_decode(VVCLocalContext *lc)
{
    return (get_cabac_bypass(&lc->ep->cc) << 1) | get_cabac_bypass(&lc->ep->cc);
}

void ff_vvc_mmvd_offset_coding(VVCLocalContext *lc, Mv *mmvd_offset, const int ph_mmvd_fullpel_only_flag)
{
    const int shift = ph_mmvd_fullpel_only_flag ? 4 : 2;
    const int mmvd_distance = 1 << (mmvd_distance_idx_decode(lc) + shift);
    const int mmvd_direction_idx = mmvd_direction_idx_decode(lc);
    const int mmvd_signs[][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
    mmvd_offset->x = mmvd_distance * mmvd_signs[mmvd_direction_idx][0];
    mmvd_offset->y = mmvd_distance * mmvd_signs[mmvd_direction_idx][1];
}

static PredMode get_luma_pred_mode(VVCLocalContext *lc)
{
    const VVCFrameContext *fc = lc->fc;
    const CodingUnit *cu      = lc->cu;
    PredMode pred_mode;

    if (cu->tree_type != DUAL_TREE_CHROMA) {
        pred_mode = cu->pred_mode;
    } else {
        const int x_cb         = cu->x0 >> fc->ps.sps->min_cb_log2_size_y;
        const int y_cb         = cu->y0 >> fc->ps.sps->min_cb_log2_size_y;
        const int min_cb_width = fc->ps.pps->min_cb_width;
        pred_mode = SAMPLE_CTB(fc->tab.cpm[0], x_cb, y_cb);
    }
    return pred_mode;
}

int ff_vvc_merge_idx(VVCLocalContext *lc)
{
    const VVCSPS *sps = lc->fc->ps.sps;
    const int is_ibc = get_luma_pred_mode(lc) == MODE_IBC;
    const int c_max = (is_ibc ? sps->max_num_ibc_merge_cand : sps->max_num_merge_cand) - 1;
    int i;

    if (!GET_CABAC(MERGE_IDX))
        return 0;

    for (i = 1; i < c_max && get_cabac_bypass(&lc->ep->cc); i++)
        /* nothing */;
    return i;
}

int ff_vvc_merge_gpm_partition_idx(VVCLocalContext *lc)
{
    int i = 0;

    for (int j = 0; j < 6; j++)
        i = (i << 1) | get_cabac_bypass(&lc->ep->cc);

    return i;
}

int ff_vvc_merge_gpm_idx(VVCLocalContext *lc, const int idx)
{
    const int c_max = lc->fc->ps.sps->max_num_gpm_merge_cand - idx - 1;
    int i;

    if (!GET_CABAC(MERGE_IDX))
        return 0;

    for (i = 1; i < c_max && get_cabac_bypass(&lc->ep->cc); i++)
        /* nothing */;

    return i;
}

int ff_vvc_ciip_flag(VVCLocalContext *lc)
{
    return GET_CABAC(CIIP_FLAG);
}

PredFlag ff_vvc_pred_flag(VVCLocalContext *lc, const int is_b)
{
    const int w = lc->cu->cb_width;
    const int h = lc->cu->cb_height;
    if (!is_b)
        return  PF_L0;
    if (w + h > 12) {
        const int log2 = av_log2(w) + av_log2(h);
        const int inc = 7 - ((1 + log2)>>1);
        if (GET_CABAC(INTER_PRED_IDC + inc))
            return PF_BI;
    }
    return PF_L0 + GET_CABAC(INTER_PRED_IDC + 5);
}

int ff_vvc_inter_affine_flag(VVCLocalContext *lc)
{
    const int inc = get_inter_flag_inc(lc, lc->cu->x0, lc->cu->y0);
    return GET_CABAC(INTER_AFFINE_FLAG + inc);
}

int ff_vvc_cu_affine_type_flag(VVCLocalContext *lc)
{
    return GET_CABAC(CU_AFFINE_TYPE_FLAG);
}

int ff_vvc_sym_mvd_flag(VVCLocalContext *lc)
{
    return GET_CABAC(SYM_MVD_FLAG);
}

int ff_vvc_ref_idx_lx(VVCLocalContext *lc, const uint8_t nb_refs)
{
    const int c_max = nb_refs - 1;
    const int max_ctx = FFMIN(c_max, 2);
    int i = 0;

    while (i < max_ctx && GET_CABAC(REF_IDX_LX + i))
        i++;
    if (i == 2) {
        while (i < c_max && get_cabac_bypass(&lc->ep->cc))
            i++;
    }
    return i;
}

int ff_vvc_abs_mvd_greater0_flag(VVCLocalContext *lc)
{
    return GET_CABAC(ABS_MVD_GREATER0_FLAG);
}

int ff_vvc_abs_mvd_greater1_flag(VVCLocalContext *lc)
{
    return GET_CABAC(ABS_MVD_GREATER1_FLAG);
}

int ff_vvc_abs_mvd_minus2(VVCLocalContext *lc)
{
    return limited_kth_order_egk_decode(&lc->ep->cc, 1, 15, 17);
}

int ff_vvc_mvd_sign_flag(VVCLocalContext *lc)
{
    return get_cabac_bypass(&lc->ep->cc);
}

int ff_vvc_mvp_lx_flag(VVCLocalContext *lc)
{
    return GET_CABAC(MVP_LX_FLAG);
}

static int amvr_flag(VVCLocalContext *lc, const int inter_affine_flag)
{
    return GET_CABAC(AMVR_FLAG + inter_affine_flag);
}

static int amvr_precision_idx(VVCLocalContext *lc, const int inc, const int c_max)
{
    int i = 0;
    if (!GET_CABAC(AMVR_PRECISION_IDX + inc))
        return 0;
    i++;
    if (i < c_max && GET_CABAC(AMVR_PRECISION_IDX + 1))
        i++;
    return i;
}

int ff_vvc_amvr_shift(VVCLocalContext *lc, const int inter_affine_flag,
    const PredMode pred_mode, const int has_amvr_flag)
{
    int amvr_shift = 2;
    if (has_amvr_flag) {
        if (pred_mode == MODE_IBC || amvr_flag(lc, inter_affine_flag)) {
            int idx;
            if (inter_affine_flag) {
                idx = amvr_precision_idx(lc, 2, 1);
                amvr_shift = idx * 4;
            } else if (pred_mode == MODE_IBC) {
                idx = amvr_precision_idx(lc, 1, 1);
                amvr_shift = 4 + idx * 2;
            } else {
                static const int shifts[] = {3, 4, 6};
                idx = amvr_precision_idx(lc, 0, 2);
                amvr_shift = shifts[idx];
            }
        }
    }
    return amvr_shift;
}

int ff_vvc_bcw_idx(VVCLocalContext *lc, const int no_backward_pred_flag)
{
    const int c_max = no_backward_pred_flag ? 4 : 2;
    int i = 1;
    if (!GET_CABAC(BCW_IDX))
        return 0;
    while (i < c_max && get_cabac_bypass(&lc->ep->cc))
        i++;
    return i;
}

int ff_vvc_tu_cb_coded_flag(VVCLocalContext *lc)
{
    return GET_CABAC(TU_CB_CODED_FLAG + lc->cu->bdpcm_flag[1]);
}

int ff_vvc_tu_cr_coded_flag(VVCLocalContext *lc, int tu_cb_coded_flag)
{
    return GET_CABAC(TU_CR_CODED_FLAG + (lc->cu->bdpcm_flag[1] ? 2 : tu_cb_coded_flag));
}

int ff_vvc_tu_y_coded_flag(VVCLocalContext *lc)
{
    const CodingUnit *cu = lc->cu;
    int inc;
    if (cu->bdpcm_flag[0])
        inc = 1;
    else if (cu->isp_split_type == ISP_NO_SPLIT)
        inc = 0;
    else
        inc = 2 + lc->parse.prev_tu_cbf_y;
    lc->parse.prev_tu_cbf_y = GET_CABAC(TU_Y_CODED_FLAG + inc);
    return lc->parse.prev_tu_cbf_y;
}

int ff_vvc_cu_qp_delta_abs(VVCLocalContext *lc)
{
    int v, i, k;
    if (!GET_CABAC(CU_QP_DELTA_ABS))
        return 0;

    // prefixVal
    for (v = 1; v < 5 && GET_CABAC(CU_QP_DELTA_ABS + 1); v++)
        /* nothing */;
    if (v < 5)
        return v;

    // 9.3.3.5 k-th order Exp-Golomb binarization process
    // suffixVal

    // CuQpDeltaVal shall in the range of −( 32 + QpBdOffset / 2 ) to +( 31 + QpBdOffset / 2 )
    // so k = 6 should enough
    for (k = 0; k < 6 && get_cabac_bypass(&lc->ep->cc); k++)
        /* nothing */;
    i = (1 << k) - 1;
    v = 0;
    while (k--)
        v = (v << 1) + get_cabac_bypass(&lc->ep->cc);
    v += i;

    return v + 5;
}

int ff_vvc_cu_qp_delta_sign_flag(VVCLocalContext *lc)
{
    return get_cabac_bypass(&lc->ep->cc);
}

int ff_vvc_cu_chroma_qp_offset_flag(VVCLocalContext *lc)
{
    return GET_CABAC(CU_CHROMA_QP_OFFSET_FLAG);
}

int ff_vvc_cu_chroma_qp_offset_idx(VVCLocalContext *lc)
{
    const int c_max = lc->fc->ps.pps->r->pps_chroma_qp_offset_list_len_minus1;
    int i;
    for (i = 0; i < c_max && GET_CABAC(CU_CHROMA_QP_OFFSET_IDX); i++)
        /* nothing */;
    return i;
}

static av_always_inline int last_significant_coeff_xy_prefix(VVCLocalContext *lc,
    const int log2_tb_size, const int log2_zo_tb_size, const int c_idx, const int ctx)
{
    int i = 0;
    int max = (log2_zo_tb_size << 1) - 1;
    int ctx_offset, ctx_shift;
    if (!log2_tb_size)
        return 0;
    if (!c_idx) {
        const int offset_y[] = {0, 0, 3, 6, 10, 15};
        ctx_offset = offset_y[log2_tb_size - 1];
        ctx_shift  = (log2_tb_size + 1) >> 2;
    } else {
        const int shifts[] = {0, 0, 0, 1, 2, 2, 2};
        ctx_offset = 20;
        ctx_shift  = shifts[log2_tb_size];
    }
    while (i < max && GET_CABAC(ctx + (i >> ctx_shift) + ctx_offset))
        i++;
    return i;
}

static av_always_inline int last_significant_coeff_x_prefix_decode(VVCLocalContext *lc,
    const int log2_tb_width, const int log2_zo_tb_width, const int c_idx)
{
    return last_significant_coeff_xy_prefix(lc, log2_tb_width, log2_zo_tb_width, c_idx, LAST_SIG_COEFF_X_PREFIX);
}

static av_always_inline int last_significant_coeff_y_prefix_decode(VVCLocalContext *lc,
    const int log2_tb_height, const int log2_zo_tb_height, const int c_idx)
{
    return last_significant_coeff_xy_prefix(lc, log2_tb_height, log2_zo_tb_height, c_idx, LAST_SIG_COEFF_Y_PREFIX);
}

static av_always_inline int last_sig_coeff_suffix_decode(VVCLocalContext *lc,
    const int last_significant_coeff_y_prefix)
{
    const int length = (last_significant_coeff_y_prefix >> 1) - 1;
    int value = get_cabac_bypass(&lc->ep->cc);

    for (int i = 1; i < length; i++)
        value = (value << 1) | get_cabac_bypass(&lc->ep->cc);
    return value;
}

int ff_vvc_tu_joint_cbcr_residual_flag(VVCLocalContext *lc, const int tu_cb_coded_flag, const int tu_cr_coded_flag)
{
    return GET_CABAC(TU_JOINT_CBCR_RESIDUAL_FLAG + 2 * tu_cb_coded_flag + tu_cr_coded_flag - 1);
}

int ff_vvc_transform_skip_flag(VVCLocalContext *lc, const int inc)
{
    return GET_CABAC(TRANSFORM_SKIP_FLAG + inc);
}

//9.3.4.2.7 Derivation process for the variables locNumSig, locSumAbsPass1
static int get_local_sum(const int *level, const int w, const int h,
    const int xc, const int yc, const int hist_value)
{
    int loc_sum = 3 * hist_value;
    level += w * yc + xc;
    if (xc < w - 1) {
        loc_sum += level[1];
        if (xc < w - 2)
            loc_sum += level[2] - hist_value;
        if (yc < h - 1)
            loc_sum += level[w + 1] - hist_value;
    }
    if (yc < h - 1) {
        loc_sum += level[w];
        if (yc < h - 2)
            loc_sum += level[w << 1] - hist_value;
    }
    return loc_sum;
}

//9.3.4.2.7 Derivation process for the variables locNumSig, locSumAbsPass1
static int get_local_sum_ts(const int *level, const int w, const int h, const int xc, const int yc)
{
    int loc_sum = 0;
    level += w * yc + xc;
    if (xc > 0)
        loc_sum += level[-1];
    if (yc > 0)
        loc_sum += level[-w];
    return loc_sum;
}

static int get_gtx_flag_inc(const ResidualCoding* rc, const int xc, const int yc, const int last)
{
    const TransformBlock *tb = rc->tb;
    int inc;
    if (last) {
        const int incs[] = {0, 21, 21};
        inc =  incs[tb->c_idx];
    } else {
        const int d = xc + yc;
        const int local_sum_sig = get_local_sum(rc->sig_coeff_flag,
                tb->tb_width,tb->tb_height, xc, yc, rc->hist_value);
        const int loc_sum_abs_pass1 = get_local_sum(rc->abs_level_pass1,
                tb->tb_width, tb->tb_height, xc, yc, rc->hist_value);
        const int offset = FFMIN(loc_sum_abs_pass1 - local_sum_sig, 4);

        if (!tb->c_idx)
            inc =  1 + offset + (!d ? 15 : (d < 3 ? 10 : (d < 10 ? 5 : 0)));
        else
            inc = 22 + offset + (!d ? 5 : 0);
    }
    return inc;
}

static int abs_level_gtx_flag_decode(VVCLocalContext *lc, const int inc)
{
    return GET_CABAC(ABS_LEVEL_GTX_FLAG + inc);
}

static int par_level_flag_decode(VVCLocalContext *lc, const int inc)
{
    return GET_CABAC(PAR_LEVEL_FLAG + inc);
}

static int par_level_flag_ts_decode(VVCLocalContext *lc)
{
    const int inc = 32;
    return GET_CABAC(PAR_LEVEL_FLAG + inc);
}

static int sb_coded_flag_decode(VVCLocalContext *lc, const uint8_t *sb_coded_flag,
    const ResidualCoding *rc, const int xs, const int ys)
{
    const H266RawSliceHeader *rsh = lc->sc->sh.r;
    const TransformBlock *tb      = rc->tb;
    const int w                   = rc->width_in_sbs;
    const int h                   = rc->height_in_sbs;
    int inc;

    if (tb->ts && !rsh->sh_ts_residual_coding_disabled_flag) {
        const int left  = xs > 0 ? sb_coded_flag[-1] : 0;
        const int above = ys > 0 ? sb_coded_flag[-w] : 0;
        inc = left + above + 4;
    } else {
        const int right  = (xs < w - 1) ? sb_coded_flag[1] : 0;
        const int bottom = (ys < h - 1) ? sb_coded_flag[w] : 0;
        inc = (right | bottom) + (tb->c_idx ? 2 : 0);
    }
    return GET_CABAC(SB_CODED_FLAG + inc);
}

static int sig_coeff_flag_decode(VVCLocalContext *lc, const ResidualCoding* rc, const int xc, const int yc)
{
    const H266RawSliceHeader *rsh = lc->sc->sh.r;
    const TransformBlock *tb      = rc->tb;
    int inc;

    if (tb->ts && !rsh->sh_ts_residual_coding_disabled_flag) {
        const int local_num_sig = get_local_sum_ts(rc->sig_coeff_flag, tb->tb_width, tb->tb_height, xc, yc);
        inc = 60 + local_num_sig;
    } else {
        const int d = xc + yc;
        const int loc_sum_abs_pass1 = get_local_sum(rc->abs_level_pass1,
                tb->tb_width, tb->tb_height, xc, yc, 0);

        if (!tb->c_idx) {
            inc = 12 * FFMAX(0, rc->qstate - 1) + FFMIN((loc_sum_abs_pass1 + 1) >> 1, 3) + ((d < 2) ? 8 : (d < 5 ? 4 : 0));
        } else {
            inc = 36 + 8 * FFMAX(0, rc->qstate - 1) + FFMIN((loc_sum_abs_pass1 + 1) >> 1, 3) + (d < 2 ? 4 : 0);
        }
    }
    return GET_CABAC(SIG_COEFF_FLAG + inc);
}

static int abs_get_rice_param(VVCLocalContext *lc, const ResidualCoding* rc,
                              const int xc, const int yc, const int base_level)
{
    const VVCSPS *sps = lc->fc->ps.sps;
    const TransformBlock* tb = rc->tb;
    const int rice_params[] = {
        0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 2, 2,
        2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3,
    };
    int loc_sum_abs;
    int shift_val;

    loc_sum_abs = get_local_sum(rc->abs_level, tb->tb_width, tb->tb_height, xc,
            yc, rc->hist_value);

    if (!sps->r->sps_rrc_rice_extension_flag) {
        shift_val = 0;
    } else {
        shift_val = (av_log2(FFMAX(FFMIN(loc_sum_abs, 2048), 8)) - 3) & ~1;
    }

    loc_sum_abs = av_clip_uintp2((loc_sum_abs >> shift_val) - base_level * 5, 5);

    return rice_params[loc_sum_abs] + shift_val;
}

static int abs_decode(VVCLocalContext *lc, const int c_rice_param)
{
    const VVCSPS *sps = lc->fc->ps.sps;
    const int MAX_BIN = 6;
    int prefix = 0;
    int suffix = 0;

    while (prefix < MAX_BIN && get_cabac_bypass(&lc->ep->cc))
        prefix++;
    if (prefix < MAX_BIN) {
        for (int i = 0; i < c_rice_param; i++) {
            suffix = (suffix << 1) | get_cabac_bypass(&lc->ep->cc);
        }
    } else {
        suffix = limited_kth_order_egk_decode(&lc->ep->cc,
                                              c_rice_param + 1,
                                              26 - sps->log2_transform_range,
                                              sps->log2_transform_range);
    }
    return suffix + (prefix << c_rice_param);
}

static int abs_remainder_decode(VVCLocalContext *lc, const ResidualCoding* rc, const int xc, const int yc)
{
    const VVCSPS *sps             = lc->fc->ps.sps;
    const H266RawSliceHeader *rsh = lc->sc->sh.r;
    const int base_level[][2][2]  = {
        { {4, 4}, {4, 4} },
        { {3, 2}, {2, 1} }
    };
    const int c_rice_param = abs_get_rice_param(lc, rc, xc, yc,
        base_level[sps->r->sps_rrc_rice_extension_flag][sps->bit_depth > 12][IS_I(rsh)]);
    const int rem = abs_decode(lc, c_rice_param);

    return rem;
}

static int abs_remainder_ts_decode(VVCLocalContext *lc, const ResidualCoding* rc, const int xc, const int yc)
{
    const H266RawSliceHeader *rsh = lc->sc->sh.r;
    const int c_rice_param = rsh->sh_ts_residual_coding_rice_idx_minus1 + 1;
    const int rem = abs_decode(lc, c_rice_param);

    return rem;
}

static int coeff_sign_flag_decode(VVCLocalContext *lc)
{
    return get_cabac_bypass(&lc->ep->cc);
}

//9.3.4.2.10 Derivation process of ctxInc for the syntax element coeff_sign_flag for transform skip mode
static int coeff_sign_flag_ts_decode(VVCLocalContext *lc, const CodingUnit *cu, const ResidualCoding *rc, const int xc, const int yc)
{
    const TransformBlock *tb = rc->tb;
    const int w              = tb->tb_width;
    const int *level         = rc->coeff_sign_level + yc * w + xc;
    const int left_sign      = xc ? level[-1] : 0;
    const int above_sign     = yc ? level[-w] : 0;
    const int bdpcm_flag     = cu->bdpcm_flag[tb->c_idx];
    int inc;

    if (left_sign == -above_sign)
        inc = bdpcm_flag ? 3 : 0;
    else if (left_sign >= 0 && above_sign >= 0)
        inc = bdpcm_flag ? 4 : 1;
    else
        inc = bdpcm_flag ? 5 : 2;
    return GET_CABAC(COEFF_SIGN_FLAG + inc);
}

static int abs_level_gt1_flag_ts_decode(VVCLocalContext *lc, const CodingUnit *cu, const ResidualCoding *rc, const int xc, const int yc)
{
    const TransformBlock *tb = rc->tb;
    const int *sig_coeff_flag = rc->sig_coeff_flag + yc * tb->tb_width + xc;
    int inc;

    if (cu->bdpcm_flag[tb->c_idx]) {
        inc = 67;
    } else {
        const int l = xc > 0 ? sig_coeff_flag[-1] : 0;
        const int a = yc > 0 ? sig_coeff_flag[-tb->tb_width] : 0;
        inc = 64 + a + l;
    }
    return GET_CABAC(ABS_LEVEL_GTX_FLAG + inc);
}

static int abs_level_gtx_flag_ts_decode(VVCLocalContext *lc, const int j)
{
    const int inc = 67 + j;
    return GET_CABAC(ABS_LEVEL_GTX_FLAG + inc);
}

static const uint8_t qstate_translate_table[][2] = {
    { 0, 2 }, { 2, 0 }, { 1, 3 }, { 3, 1 }
};

static int dec_abs_level_decode(VVCLocalContext *lc, const ResidualCoding *rc,
    const int xc, const int yc, int *abs_level)
{
    const int c_rice_param  = abs_get_rice_param(lc, rc, xc, yc, 0);
    const int dec_abs_level =  abs_decode(lc, c_rice_param);
    const int zero_pos      = (rc->qstate < 2 ? 1 : 2) << c_rice_param;

    *abs_level = 0;
    if (dec_abs_level != zero_pos) {
        *abs_level = dec_abs_level;
        if (dec_abs_level < zero_pos)
            *abs_level += 1;
    }
    return dec_abs_level;
}

static void ep_update_hist(EntryPoint *ep, ResidualCoding *rc,
    const int remainder, const int addin)
{
    int *stat = ep->stat_coeff + rc->tb->c_idx;
    if (rc->update_hist && remainder > 0) {
        *stat = (*stat + av_log2(remainder) + addin) >> 1;
        rc->update_hist = 0;
    }
}

static void init_residual_coding(const VVCLocalContext *lc, ResidualCoding *rc,
    const int log2_zo_tb_width, const int log2_zo_tb_height,
    TransformBlock *tb)
{
    const VVCSPS *sps = lc->fc->ps.sps;
    int log2_sb_w     = (FFMIN(log2_zo_tb_width, log2_zo_tb_height ) < 2 ? 1 : 2 );
    int log2_sb_h     = log2_sb_w;

    if ( log2_zo_tb_width + log2_zo_tb_height > 3 ) {
        if ( log2_zo_tb_width < 2 ) {
            log2_sb_w = log2_zo_tb_width;
            log2_sb_h = 4 - log2_sb_w;
        } else if ( log2_zo_tb_height < 2 ) {
            log2_sb_h = log2_zo_tb_height;
            log2_sb_w = 4 - log2_sb_h;
        }
    }
    rc->log2_sb_w = log2_sb_w;
    rc->log2_sb_h = log2_sb_h;
    rc->num_sb_coeff   = 1 << (log2_sb_w + log2_sb_h);
    rc->last_sub_block = ( 1 << ( log2_zo_tb_width + log2_zo_tb_height - (log2_sb_w + log2_sb_h))) - 1;
    rc->hist_value     = sps->r->sps_persistent_rice_adaptation_enabled_flag ? (1 << lc->ep->stat_coeff[tb->c_idx]) : 0;
    rc->update_hist    = sps->r->sps_persistent_rice_adaptation_enabled_flag ? 1 : 0;
    rc->rem_bins_pass1 = (( 1 << ( log2_zo_tb_width + log2_zo_tb_height)) * 7 ) >> 2;


    rc->sb_scan_x_off = ff_vvc_diag_scan_x[log2_zo_tb_width - log2_sb_w][log2_zo_tb_height - log2_sb_h];
    rc->sb_scan_y_off = ff_vvc_diag_scan_y[log2_zo_tb_width - log2_sb_w][log2_zo_tb_height - log2_sb_h];

    rc->scan_x_off = ff_vvc_diag_scan_x[log2_sb_w][log2_sb_h];
    rc->scan_y_off = ff_vvc_diag_scan_y[log2_sb_w][log2_sb_h];

    rc->infer_sb_cbf = 1;

    rc->width_in_sbs  = (1 << (log2_zo_tb_width - log2_sb_w));
    rc->height_in_sbs = (1 << (log2_zo_tb_height - log2_sb_h));
    rc->nb_sbs        = rc->width_in_sbs * rc->height_in_sbs;

    rc->last_scan_pos = rc->num_sb_coeff;
    rc->qstate        = 0;

    rc->tb = tb;
}

static int residual_ts_coding_subblock(VVCLocalContext *lc, ResidualCoding* rc, const int i)
{
    const CodingUnit *cu   = lc->cu;
    TransformBlock *tb     = rc->tb;
    const int bdpcm_flag   = cu->bdpcm_flag[tb->c_idx];
    const int xs           = rc->sb_scan_x_off[i];
    const int ys           = rc->sb_scan_y_off[i];
    uint8_t *sb_coded_flag = rc->sb_coded_flag + ys * rc->width_in_sbs + xs;
    int infer_sb_sig_coeff_flag = 1;
    int last_scan_pos_pass1 = -1, last_scan_pos_pass2 = -1, n;
    int abs_level_gtx_flag[MAX_SUB_BLOCK_SIZE * MAX_SUB_BLOCK_SIZE];
    int abs_level_pass2[MAX_SUB_BLOCK_SIZE * MAX_SUB_BLOCK_SIZE];       ///< AbsLevelPass2

    if (i != rc->last_sub_block || !rc->infer_sb_cbf)
        *sb_coded_flag = sb_coded_flag_decode(lc, sb_coded_flag, rc, xs, ys);
    else
        *sb_coded_flag = 1;
    if (*sb_coded_flag && i < rc->last_sub_block)
        rc->infer_sb_cbf = 0;

    //first scan pass
    for (n = 0; n < rc->num_sb_coeff && rc->rem_bins_pass1 >= 4; n++) {
        const int xc = (xs << rc->log2_sb_w) + rc->scan_x_off[n];
        const int yc = (ys << rc->log2_sb_h) + rc->scan_y_off[n];
        const int off = yc * tb->tb_width + xc;
        int *sig_coeff_flag   = rc->sig_coeff_flag + off;
        int *abs_level_pass1  = rc->abs_level_pass1 + off;
        int *coeff_sign_level = rc->coeff_sign_level + off;
        int par_level_flag    = 0;

        abs_level_gtx_flag[n] = 0;
        last_scan_pos_pass1 = n;
        if (*sb_coded_flag && (n != rc->num_sb_coeff - 1 || !infer_sb_sig_coeff_flag)) {
            *sig_coeff_flag = sig_coeff_flag_decode(lc, rc, xc, yc);
            rc->rem_bins_pass1--;
            if (*sig_coeff_flag)
                infer_sb_sig_coeff_flag = 0;
        } else {
            *sig_coeff_flag = (n == rc->num_sb_coeff - 1) && infer_sb_sig_coeff_flag && *sb_coded_flag;
        }
        *coeff_sign_level = 0;
        if (*sig_coeff_flag) {
            *coeff_sign_level = 1 - 2 * coeff_sign_flag_ts_decode(lc, cu, rc, xc, yc);
            abs_level_gtx_flag[n] = abs_level_gt1_flag_ts_decode(lc, cu, rc, xc, yc);
            rc->rem_bins_pass1 -= 2;
            if (abs_level_gtx_flag[n]) {
                par_level_flag = par_level_flag_ts_decode(lc);
                rc->rem_bins_pass1--;
            }
        }
        *abs_level_pass1 = *sig_coeff_flag + par_level_flag + abs_level_gtx_flag[n];
    }

    //greater than x scan pass
    for (n = 0; n < rc->num_sb_coeff && rc->rem_bins_pass1 >= 4; n++) {
        const int xc  = (xs << rc->log2_sb_w) + rc->scan_x_off[n];
        const int yc  = (ys << rc->log2_sb_h) + rc->scan_y_off[n];
        const int off = yc * tb->tb_width + xc;

        abs_level_pass2[n] = rc->abs_level_pass1[off];
        for (int j = 1; j < 5 && abs_level_gtx_flag[n]; j++) {
            abs_level_gtx_flag[n] = abs_level_gtx_flag_ts_decode(lc, j);
            abs_level_pass2[n] += abs_level_gtx_flag[n] << 1;
            rc->rem_bins_pass1--;
        }
        last_scan_pos_pass2 = n;
    }

    /* remainder scan pass */
    for (n = 0; n < rc->num_sb_coeff; n++) {
        const int xc  = (xs << rc->log2_sb_w) + rc->scan_x_off[n];
        const int yc  = (ys << rc->log2_sb_h) + rc->scan_y_off[n];
        const int off = yc * tb->tb_width + xc;
        const int *abs_level_pass1 = rc->abs_level_pass1 + off;
        int *abs_level             = rc->abs_level + off;
        int *coeff_sign_level      = rc->coeff_sign_level + off;
        int abs_remainder          = 0;

        if ((n <= last_scan_pos_pass2 && abs_level_pass2[n] >= 10) ||
            (n > last_scan_pos_pass2 && n <= last_scan_pos_pass1 &&
            *abs_level_pass1 >= 2) ||
            (n > last_scan_pos_pass1 &&  *sb_coded_flag))
            abs_remainder = abs_remainder_ts_decode(lc, rc, xc, yc);
        if (n <= last_scan_pos_pass2) {
            *abs_level = abs_level_pass2[n] + 2 * abs_remainder;
        } else if (n <= last_scan_pos_pass1) {
            *abs_level = *abs_level_pass1 + 2 * abs_remainder;
        } else {
            *abs_level = abs_remainder;
            if (abs_remainder) {
                //n > lastScanPosPass1
                *coeff_sign_level = 1 - 2 * coeff_sign_flag_decode(lc);
            }
        }
        if (!bdpcm_flag && n <= last_scan_pos_pass1) {
            const int left  = xc > 0 ? abs_level[-1] : 0;
            const int above = yc > 0 ? abs_level[-tb->tb_width] : 0;
            const int pred  = FFMAX(left, above);

            if (*abs_level == 1 && pred > 0)
                *abs_level = pred;
            else if (*abs_level > 0 && *abs_level <= pred)
                (*abs_level)--;
        }
        if (*abs_level) {
            tb->coeffs[off] = *coeff_sign_level * *abs_level;
            tb->max_scan_x = FFMAX(xc, tb->max_scan_x);
            tb->max_scan_y = FFMAX(yc, tb->max_scan_y);
            tb->min_scan_x = FFMIN(xc, tb->min_scan_x);
            tb->min_scan_y = FFMIN(yc, tb->min_scan_y);
        } else {
            tb->coeffs[off] = 0;
        }
    }

    return 0;
}

static int hls_residual_ts_coding(VVCLocalContext *lc, TransformBlock *tb)
{
    ResidualCoding rc;
    tb->min_scan_x = tb->min_scan_y = INT_MAX;
    init_residual_coding(lc, &rc, tb->log2_tb_width, tb->log2_tb_height, tb);
    for (int i = 0; i <= rc.last_sub_block; i++) {
        int ret = residual_ts_coding_subblock(lc, &rc, i);
        if (ret < 0)
            return ret;
    }

    return 0;
}

static inline int residual_coding_subblock(VVCLocalContext *lc, ResidualCoding *rc, const int i)
{
    const H266RawSliceHeader *rsh = lc->sc->sh.r;
    TransformBlock *tb            = rc->tb;
    int first_sig_scan_pos_sb, last_sig_scan_pos_sb;
    int first_pos_mode0, first_pos_mode1;
    int infer_sb_dc_sig_coeff_flag = 0;
    int n, sig_hidden_flag, sum = 0;
    int abs_level_gt2_flag[MAX_SUB_BLOCK_SIZE * MAX_SUB_BLOCK_SIZE];
    const int start_qstate_sb = rc->qstate;
    const int xs = rc->sb_scan_x_off[i];
    const int ys = rc->sb_scan_y_off[i];
    uint8_t *sb_coded_flag = rc->sb_coded_flag + ys * rc->width_in_sbs + xs;


    av_assert0(rc->num_sb_coeff <= MAX_SUB_BLOCK_SIZE * MAX_SUB_BLOCK_SIZE);
    if (i < rc->last_sub_block && i > 0) {
        *sb_coded_flag = sb_coded_flag_decode(lc, sb_coded_flag, rc, xs, ys);
        infer_sb_dc_sig_coeff_flag = 1;
    } else {
        *sb_coded_flag = 1;
    }
    if (*sb_coded_flag && (xs > 3 || ys > 3) && !tb->c_idx)
        lc->parse.mts_zero_out_sig_coeff_flag = 0;

    if (!*sb_coded_flag)
        return 0;

    first_sig_scan_pos_sb = rc->num_sb_coeff;
    last_sig_scan_pos_sb = -1;
    first_pos_mode0 = (i == rc->last_sub_block ? rc->last_scan_pos : rc->num_sb_coeff -1);
    first_pos_mode1 = first_pos_mode0;
    for (n = first_pos_mode0; n >= 0 && rc->rem_bins_pass1 >= 4; n--) {
        const int xc   = (xs << rc->log2_sb_w) + rc->scan_x_off[n];
        const int yc   = (ys << rc->log2_sb_h) + rc->scan_y_off[n];
        const int last = (xc == rc->last_significant_coeff_x && yc == rc->last_significant_coeff_y);
        int *abs_level_pass1 = rc->abs_level_pass1 + yc * tb->tb_width + xc;
        int *sig_coeff_flag  = rc->sig_coeff_flag + yc * tb->tb_width + xc;

        if ((n > 0 || !infer_sb_dc_sig_coeff_flag ) && !last) {
            *sig_coeff_flag = sig_coeff_flag_decode(lc, rc, xc, yc);
            rc->rem_bins_pass1--;
            if (*sig_coeff_flag)
                infer_sb_dc_sig_coeff_flag = 0;
        } else {
            *sig_coeff_flag = last || (!rc->scan_x_off[n] && !rc ->scan_y_off[n] &&
                infer_sb_dc_sig_coeff_flag);
        }
        *abs_level_pass1 = 0;
        if (*sig_coeff_flag) {
            int abs_level_gt1_flag, par_level_flag = 0;
            const int inc = get_gtx_flag_inc(rc, xc, yc, last);
            abs_level_gt1_flag = abs_level_gtx_flag_decode(lc, inc);
            rc->rem_bins_pass1--;
            if (abs_level_gt1_flag) {
                par_level_flag = par_level_flag_decode(lc, inc);
                abs_level_gt2_flag[n] = abs_level_gtx_flag_decode(lc, inc + 32);
                rc->rem_bins_pass1 -= 2;
            } else {
                abs_level_gt2_flag[n] = 0;
            }
            if (last_sig_scan_pos_sb == -1)
                last_sig_scan_pos_sb = n;
            first_sig_scan_pos_sb = n;

            *abs_level_pass1 =
                1  + par_level_flag + abs_level_gt1_flag + (abs_level_gt2_flag[n] << 1);
        } else {
            abs_level_gt2_flag[n] = 0;
        }

        if (rsh->sh_dep_quant_used_flag)
            rc->qstate = qstate_translate_table[rc->qstate][*abs_level_pass1 & 1];

        first_pos_mode1 = n - 1;
    }
    for (n = first_pos_mode0; n > first_pos_mode1; n--) {
        const int xc = (xs << rc->log2_sb_w) + rc->scan_x_off[n];
        const int yc = (ys << rc->log2_sb_h) + rc->scan_y_off[n];
        const int *abs_level_pass1 = rc->abs_level_pass1 + yc * tb->tb_width + xc;
        int *abs_level             = rc->abs_level + yc * tb->tb_width + xc;

        *abs_level = *abs_level_pass1;
        if (abs_level_gt2_flag[n]) {
            const int abs_remainder = abs_remainder_decode(lc, rc, xc, yc);
            ep_update_hist(lc->ep, rc, abs_remainder, 2);
            *abs_level += 2 * abs_remainder;
        }
    }
    for (n = first_pos_mode1; n >= 0; n--) {
        const int xc   = (xs << rc->log2_sb_w) + rc->scan_x_off[n];
        const int yc   = (ys << rc->log2_sb_h) + rc->scan_y_off[n];
        int *abs_level = rc->abs_level + yc * tb->tb_width + xc;

        if (*sb_coded_flag) {
            const int dec_abs_level = dec_abs_level_decode(lc, rc, xc, yc, abs_level);
            ep_update_hist(lc->ep, rc, dec_abs_level, 0);
        }
        if (*abs_level > 0) {
            if (last_sig_scan_pos_sb == -1)
                last_sig_scan_pos_sb = n;
            first_sig_scan_pos_sb = n;
        }
        if (rsh->sh_dep_quant_used_flag)
            rc->qstate = qstate_translate_table[rc->qstate][*abs_level & 1];
    }
    sig_hidden_flag = rsh->sh_sign_data_hiding_used_flag &&
        (last_sig_scan_pos_sb - first_sig_scan_pos_sb > 3 ? 1 : 0);

    if (rsh->sh_dep_quant_used_flag)
        rc->qstate = start_qstate_sb;
    n = (i == rc->last_sub_block ? rc->last_scan_pos : rc->num_sb_coeff -1);
    for (/* nothing */; n >= 0; n--) {
        int trans_coeff_level;
        const int xc  = (xs << rc->log2_sb_w) + rc->scan_x_off[n];
        const int yc  = (ys << rc->log2_sb_h) + rc->scan_y_off[n];
        const int off = yc * tb->tb_width + xc;
        const int *abs_level = rc->abs_level + off;

        if (*abs_level > 0) {
            int sign = 1;
            if (!sig_hidden_flag || (n != first_sig_scan_pos_sb))
                sign = 1 - 2 * coeff_sign_flag_decode(lc);
            if (rsh->sh_dep_quant_used_flag) {
                trans_coeff_level = (2 * *abs_level - (rc->qstate > 1)) * sign;
            } else {
                trans_coeff_level = *abs_level * sign;
                if (sig_hidden_flag) {
                    sum += *abs_level;
                    if (n == first_sig_scan_pos_sb && (sum % 2))
                        trans_coeff_level = -trans_coeff_level;
                }
            }
            tb->coeffs[off] = trans_coeff_level;
            tb->max_scan_x = FFMAX(xc, tb->max_scan_x);
            tb->max_scan_y = FFMAX(yc, tb->max_scan_y);
        }
        if (rsh->sh_dep_quant_used_flag)
            rc->qstate = qstate_translate_table[rc->qstate][*abs_level & 1];
    }

    return 0;
}

static void derive_last_scan_pos(ResidualCoding *rc)
{
    int xc, yc, xs, ys;
    do {
        if (!rc->last_scan_pos) {
            rc->last_scan_pos = rc->num_sb_coeff;
            rc->last_sub_block--;
        }
        rc->last_scan_pos--;
        xs = rc->sb_scan_x_off[rc->last_sub_block];
        ys = rc->sb_scan_y_off[rc->last_sub_block];
        xc = (xs << rc->log2_sb_w) + rc->scan_x_off[rc->last_scan_pos];
        yc = (ys << rc->log2_sb_h) + rc->scan_y_off[rc->last_scan_pos];
    } while ((xc != rc->last_significant_coeff_x) || (yc != rc->last_significant_coeff_y));
}

static void last_significant_coeff_x_y_decode(ResidualCoding *rc, VVCLocalContext *lc,
    const int log2_zo_tb_width, const int log2_zo_tb_height)
{
    const H266RawSliceHeader *rsh = lc->sc->sh.r;
    const TransformBlock *tb      = rc->tb;
    int last_significant_coeff_x, last_significant_coeff_y;

    last_significant_coeff_x = last_significant_coeff_x_prefix_decode(lc,
            tb->log2_tb_width, log2_zo_tb_width, tb->c_idx);

    last_significant_coeff_y = last_significant_coeff_y_prefix_decode(lc,
        tb->log2_tb_height, log2_zo_tb_height, tb->c_idx);

    if (last_significant_coeff_x > 3) {
        int suffix = last_sig_coeff_suffix_decode(lc, last_significant_coeff_x);
        last_significant_coeff_x = (1 << ((last_significant_coeff_x >> 1) - 1)) *
            (2 + (last_significant_coeff_x & 1)) + suffix;
    }
    if (last_significant_coeff_y > 3) {
        int suffix = last_sig_coeff_suffix_decode(lc, last_significant_coeff_y);
        last_significant_coeff_y = (1 << ((last_significant_coeff_y >> 1) - 1)) *
            (2 + (last_significant_coeff_y & 1)) + suffix;
    }
    if (rsh->sh_reverse_last_sig_coeff_flag) {
        last_significant_coeff_x = (1 << log2_zo_tb_width) - 1 - last_significant_coeff_x;
        last_significant_coeff_y = (1 << log2_zo_tb_height) - 1 - last_significant_coeff_y;
    }
    rc->last_significant_coeff_x = last_significant_coeff_x;
    rc->last_significant_coeff_y = last_significant_coeff_y;
}

static int hls_residual_coding(VVCLocalContext *lc, TransformBlock *tb)
{
    const VVCSPS *sps        = lc->fc->ps.sps;
    const CodingUnit *cu     = lc->cu;
    const int log2_tb_width  = tb->log2_tb_width;
    const int log2_tb_height = tb->log2_tb_height;
    const int c_idx          = tb->c_idx;
    int log2_zo_tb_width, log2_zo_tb_height;
    ResidualCoding rc;

    if (sps->r->sps_mts_enabled_flag && cu->sbt_flag && !c_idx && log2_tb_width == 5 && log2_tb_height < 6)
        log2_zo_tb_width = 4;
    else
        log2_zo_tb_width = FFMIN(log2_tb_width, 5 );

    if (sps->r->sps_mts_enabled_flag && cu->sbt_flag && !c_idx && log2_tb_width < 6 && log2_tb_height == 5 )
        log2_zo_tb_height = 4;
    else
        log2_zo_tb_height = FFMIN(log2_tb_height, 5);

    init_residual_coding(lc, &rc, log2_zo_tb_width, log2_zo_tb_height, tb);
    last_significant_coeff_x_y_decode(&rc, lc, log2_zo_tb_width, log2_zo_tb_height);
    derive_last_scan_pos(&rc);

    if (!rc.last_sub_block && log2_tb_width >= 2 && log2_tb_height >= 2 && !tb->ts && rc.last_scan_pos > 0)
        lc->parse.lfnst_dc_only = 0;
    if ((rc.last_sub_block > 0 && log2_tb_width >= 2 && log2_tb_height >= 2 ) ||
         (rc.last_scan_pos > 7 && (log2_tb_width == 2 || log2_tb_width == 3 ) &&
         log2_tb_width == log2_tb_height))
        lc->parse.lfnst_zero_out_sig_coeff_flag = 0;
    if ((rc.last_sub_block > 0 || rc.last_scan_pos > 0 ) && !c_idx)
        lc->parse.mts_dc_only = 0;

    memset(tb->coeffs, 0, tb->tb_width * tb->tb_height * sizeof(*tb->coeffs));
    memset(rc.abs_level, 0, tb->tb_width * tb->tb_height * sizeof(rc.abs_level[0]));
    memset(rc.sb_coded_flag, 0, rc.nb_sbs);
    memset(rc.abs_level_pass1, 0, tb->tb_width * tb->tb_height * sizeof(rc.abs_level_pass1[0]));
    memset(rc.sig_coeff_flag, 0, tb->tb_width * tb->tb_height * sizeof(rc.sig_coeff_flag[0]));

    for (int i = rc.last_sub_block; i >= 0; i--) {
        int ret = residual_coding_subblock(lc, &rc, i);
        if (ret < 0)
            return ret;
    }

    return 0;
}

int ff_vvc_residual_coding(VVCLocalContext *lc, TransformBlock *tb)
{
    const H266RawSliceHeader *rsh = lc->sc->sh.r;
    const int ts                  = !rsh->sh_ts_residual_coding_disabled_flag && tb->ts;

    return ts ? hls_residual_ts_coding(lc, tb) : hls_residual_coding(lc, tb);
}

int ff_vvc_cu_coded_flag(VVCLocalContext *lc)
{
    return GET_CABAC(CU_CODED_FLAG);
}

int ff_vvc_sbt_flag(VVCLocalContext *lc)
{
    const int w   = lc->cu->cb_width;
    const int h   = lc->cu->cb_height;
    const int inc = w * h <= 256;
    return GET_CABAC(CU_SBT_FLAG + inc);
}

int ff_vvc_sbt_quad_flag(VVCLocalContext *lc)
{
    return GET_CABAC(CU_SBT_QUAD_FLAG);
}

int ff_vvc_sbt_horizontal_flag(VVCLocalContext *lc)
{
    const int w = lc->cu->cb_width;
    const int h = lc->cu->cb_height;
    const int inc = (w == h) ? 0 : ((w < h) ? 1 : 2);
    return GET_CABAC(CU_SBT_HORIZONTAL_FLAG + inc);
}

int ff_vvc_sbt_pos_flag(VVCLocalContext *lc)
{
    return GET_CABAC(CU_SBT_POS_FLAG);
}

int ff_vvc_lfnst_idx(VVCLocalContext *lc, const int inc)
{
    if (!GET_CABAC(LFNST_IDX + inc))
        return 0;
    if (!GET_CABAC(LFNST_IDX + 2))
        return 1;
    return 2;
}

int ff_vvc_mts_idx(VVCLocalContext *lc)
{
    int i;
    for (i = 0; i < 4; i++) {
        if (!GET_CABAC(MTS_IDX + i))
            return i;
    }
    return i;
}

int ff_vvc_end_of_slice_flag_decode(VVCLocalContext *lc)
{
    return get_cabac_terminate(&lc->ep->cc);
}

int ff_vvc_end_of_tile_one_bit(VVCLocalContext *lc)
{
    return get_cabac_terminate(&lc->ep->cc);
}

int ff_vvc_end_of_subset_one_bit(VVCLocalContext *lc)
{
    return get_cabac_terminate(&lc->ep->cc);
}
