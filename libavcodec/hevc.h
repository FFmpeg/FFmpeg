/*
 * HEVC video Decoder
 *
 * Copyright (C) 2012 - 2013 Guillaume Martres
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

#ifndef AVCODEC_HEVC_H
#define AVCODEC_HEVC_H

#include "libavutil/buffer.h"
#include "libavutil/md5.h"

#include "avcodec.h"
#include "cabac.h"
#include "dsputil.h"
#include "get_bits.h"
#include "hevcpred.h"
#include "hevcdsp.h"
#include "internal.h"
#include "thread.h"
#include "videodsp.h"

#define MAX_DPB_SIZE 16 // A.4.1
#define MAX_REFS 16

#define MAX_NB_THREADS 16
#define SHIFT_CTB_WPP 2

/**
 * 7.4.2.1
 */
#define MAX_SUB_LAYERS 7
#define MAX_VPS_COUNT 16
#define MAX_SPS_COUNT 32
#define MAX_PPS_COUNT 256
#define MAX_SHORT_TERM_RPS_COUNT 64
#define MAX_CU_SIZE 128

//TODO: check if this is really the maximum
#define MAX_TRANSFORM_DEPTH 5

#define MAX_TB_SIZE 32
#define MAX_PB_SIZE 64
#define MAX_LOG2_CTB_SIZE 6
#define MAX_QP 51
#define DEFAULT_INTRA_TC_OFFSET 2

#define HEVC_CONTEXTS 183

#define MRG_MAX_NUM_CANDS     5

#define L0 0
#define L1 1

#define EPEL_EXTRA_BEFORE 1
#define EPEL_EXTRA_AFTER  2
#define EPEL_EXTRA        3

/**
 * Value of the luma sample at position (x, y) in the 2D array tab.
 */
#define SAMPLE(tab, x, y) ((tab)[(y) * s->sps->width + (x)])
#define SAMPLE_CTB(tab, x, y) ((tab)[(y) * min_cb_width + (x)])
#define SAMPLE_CBF(tab, x, y) ((tab)[((y) & ((1<<log2_trafo_size)-1)) * MAX_CU_SIZE + ((x) & ((1<<log2_trafo_size)-1))])

#define IS_IDR(s) (s->nal_unit_type == NAL_IDR_W_RADL || s->nal_unit_type == NAL_IDR_N_LP)
#define IS_BLA(s) (s->nal_unit_type == NAL_BLA_W_RADL || s->nal_unit_type == NAL_BLA_W_LP || \
                   s->nal_unit_type == NAL_BLA_N_LP)

/**
 * Table 7-3: NAL unit type codes
 */
enum NALUnitType {
    NAL_TRAIL_N     =  0,
    NAL_TRAIL_R     =  1,
    NAL_TSA_N       =  2,
    NAL_TSA_R       =  3,
    NAL_STSA_N      =  4,
    NAL_STSA_R      =  5,
    NAL_RADL_N      =  6,
    NAL_RADL_R      =  7,
    NAL_RASL_N      =  8,
    NAL_RASL_R      =  9,
    NAL_BLA_W_LP    = 16,
    NAL_BLA_W_RADL  = 17,
    NAL_BLA_N_LP    = 18,
    NAL_IDR_W_RADL  = 19,
    NAL_IDR_N_LP    = 20,
    NAL_CRA_NUT     = 21,
    NAL_VPS         = 32,
    NAL_SPS         = 33,
    NAL_PPS         = 34,
    NAL_AUD         = 35,
    NAL_EOS_NUT     = 36,
    NAL_EOB_NUT     = 37,
    NAL_FD_NUT      = 38,
    NAL_SEI_PREFIX  = 39,
    NAL_SEI_SUFFIX  = 40,
};

enum RPSType {
    ST_CURR_BEF = 0,
    ST_CURR_AFT,
    ST_FOLL,
    LT_CURR,
    LT_FOLL,
    NB_RPS_TYPE,
};

enum SliceType {
    B_SLICE = 0,
    P_SLICE = 1,
    I_SLICE = 2,
};

enum SyntaxElement {
    SAO_MERGE_FLAG = 0,
    SAO_TYPE_IDX,
    SAO_EO_CLASS,
    SAO_BAND_POSITION,
    SAO_OFFSET_ABS,
    SAO_OFFSET_SIGN,
    END_OF_SLICE_FLAG,
    SPLIT_CODING_UNIT_FLAG,
    CU_TRANSQUANT_BYPASS_FLAG,
    SKIP_FLAG,
    CU_QP_DELTA,
    PRED_MODE_FLAG,
    PART_MODE,
    PCM_FLAG,
    PREV_INTRA_LUMA_PRED_FLAG,
    MPM_IDX,
    REM_INTRA_LUMA_PRED_MODE,
    INTRA_CHROMA_PRED_MODE,
    MERGE_FLAG,
    MERGE_IDX,
    INTER_PRED_IDC,
    REF_IDX_L0,
    REF_IDX_L1,
    ABS_MVD_GREATER0_FLAG,
    ABS_MVD_GREATER1_FLAG,
    ABS_MVD_MINUS2,
    MVD_SIGN_FLAG,
    MVP_LX_FLAG,
    NO_RESIDUAL_DATA_FLAG,
    SPLIT_TRANSFORM_FLAG,
    CBF_LUMA,
    CBF_CB_CR,
    TRANSFORM_SKIP_FLAG,
    LAST_SIGNIFICANT_COEFF_X_PREFIX,
    LAST_SIGNIFICANT_COEFF_Y_PREFIX,
    LAST_SIGNIFICANT_COEFF_X_SUFFIX,
    LAST_SIGNIFICANT_COEFF_Y_SUFFIX,
    SIGNIFICANT_COEFF_GROUP_FLAG,
    SIGNIFICANT_COEFF_FLAG,
    COEFF_ABS_LEVEL_GREATER1_FLAG,
    COEFF_ABS_LEVEL_GREATER2_FLAG,
    COEFF_ABS_LEVEL_REMAINING,
    COEFF_SIGN_FLAG,
};

enum PartMode {
    PART_2Nx2N = 0,
    PART_2NxN  = 1,
    PART_Nx2N  = 2,
    PART_NxN   = 3,
    PART_2NxnU = 4,
    PART_2NxnD = 5,
    PART_nLx2N = 6,
    PART_nRx2N = 7,
};

enum PredMode {
    MODE_INTER = 0,
    MODE_INTRA,
    MODE_SKIP,
};

enum InterPredIdc {
    PRED_L0 = 0,
    PRED_L1,
    PRED_BI,
};

enum IntraPredMode {
    INTRA_PLANAR = 0,
    INTRA_DC,
    INTRA_ANGULAR_2,
    INTRA_ANGULAR_3,
    INTRA_ANGULAR_4,
    INTRA_ANGULAR_5,
    INTRA_ANGULAR_6,
    INTRA_ANGULAR_7,
    INTRA_ANGULAR_8,
    INTRA_ANGULAR_9,
    INTRA_ANGULAR_10,
    INTRA_ANGULAR_11,
    INTRA_ANGULAR_12,
    INTRA_ANGULAR_13,
    INTRA_ANGULAR_14,
    INTRA_ANGULAR_15,
    INTRA_ANGULAR_16,
    INTRA_ANGULAR_17,
    INTRA_ANGULAR_18,
    INTRA_ANGULAR_19,
    INTRA_ANGULAR_20,
    INTRA_ANGULAR_21,
    INTRA_ANGULAR_22,
    INTRA_ANGULAR_23,
    INTRA_ANGULAR_24,
    INTRA_ANGULAR_25,
    INTRA_ANGULAR_26,
    INTRA_ANGULAR_27,
    INTRA_ANGULAR_28,
    INTRA_ANGULAR_29,
    INTRA_ANGULAR_30,
    INTRA_ANGULAR_31,
    INTRA_ANGULAR_32,
    INTRA_ANGULAR_33,
    INTRA_ANGULAR_34,
};

enum SAOType {
    SAO_NOT_APPLIED = 0,
    SAO_BAND,
    SAO_EDGE,
};

enum SAOEOClass {
    SAO_EO_HORIZ = 0,
    SAO_EO_VERT,
    SAO_EO_135D,
    SAO_EO_45D,
};

enum ScanType {
    SCAN_DIAG = 0,
    SCAN_HORIZ,
    SCAN_VERT,
};

typedef struct ShortTermRPS {
    int num_negative_pics;
    int num_delta_pocs;
    int32_t delta_poc[32];
    uint8_t used[32];
} ShortTermRPS;

typedef struct LongTermRPS {
    int     poc[32];
    uint8_t used[32];
    uint8_t nb_refs;
} LongTermRPS;

typedef struct RefPicList {
    struct HEVCFrame *ref[MAX_REFS];
    int list[MAX_REFS];
    int isLongTerm[MAX_REFS];
    int nb_refs;
} RefPicList;

typedef struct RefPicListTab {
    RefPicList refPicList[2];
} RefPicListTab;

typedef struct HEVCWindow {
    int left_offset;
    int right_offset;
    int top_offset;
    int bottom_offset;
} HEVCWindow;

typedef struct VUI {
    AVRational sar;

    int overscan_info_present_flag;
    int overscan_appropriate_flag;

    int video_signal_type_present_flag;
    int video_format;
    int video_full_range_flag;
    int colour_description_present_flag;
    uint8_t colour_primaries;
    uint8_t transfer_characteristic;
    uint8_t matrix_coeffs;

    int chroma_loc_info_present_flag;
    int chroma_sample_loc_type_top_field;
    int chroma_sample_loc_type_bottom_field;
    int neutra_chroma_indication_flag;

    int field_seq_flag;
    int frame_field_info_present_flag;

    int default_display_window_flag;
    HEVCWindow def_disp_win;

    int vui_timing_info_present_flag;
    uint32_t vui_num_units_in_tick;
    uint32_t vui_time_scale;
    int vui_poc_proportional_to_timing_flag;
    int vui_num_ticks_poc_diff_one_minus1;
    int vui_hrd_parameters_present_flag;

    int bitstream_restriction_flag;
    int tiles_fixed_structure_flag;
    int motion_vectors_over_pic_boundaries_flag;
    int restricted_ref_pic_lists_flag;
    int min_spatial_segmentation_idc;
    int max_bytes_per_pic_denom;
    int max_bits_per_min_cu_denom;
    int log2_max_mv_length_horizontal;
    int log2_max_mv_length_vertical;
} VUI;

typedef struct PTL {
    int general_profile_space;
    uint8_t general_tier_flag;
    int general_profile_idc;
    int general_profile_compatibility_flag[32];
    int general_level_idc;

    uint8_t sub_layer_profile_present_flag[MAX_SUB_LAYERS];
    uint8_t sub_layer_level_present_flag[MAX_SUB_LAYERS];

    int sub_layer_profile_space[MAX_SUB_LAYERS];
    uint8_t sub_layer_tier_flag[MAX_SUB_LAYERS];
    int sub_layer_profile_idc[MAX_SUB_LAYERS];
    uint8_t sub_layer_profile_compatibility_flags[MAX_SUB_LAYERS][32];
    int sub_layer_level_idc[MAX_SUB_LAYERS];
} PTL;

typedef struct VPS {
    uint8_t vps_temporal_id_nesting_flag;
    int vps_max_layers;
    int vps_max_sub_layers; ///< vps_max_temporal_layers_minus1 + 1

    PTL ptl;
    int vps_sub_layer_ordering_info_present_flag;
    unsigned int vps_max_dec_pic_buffering[MAX_SUB_LAYERS];
    unsigned int vps_num_reorder_pics[MAX_SUB_LAYERS];
    unsigned int vps_max_latency_increase[MAX_SUB_LAYERS];
    int vps_max_layer_id;
    int vps_num_layer_sets; ///< vps_num_layer_sets_minus1 + 1
    uint8_t vps_timing_info_present_flag;
    uint32_t vps_num_units_in_tick;
    uint32_t vps_time_scale;
    uint8_t vps_poc_proportional_to_timing_flag;
    int vps_num_ticks_poc_diff_one; ///< vps_num_ticks_poc_diff_one_minus1 + 1
    int vps_num_hrd_parameters;
} VPS;

typedef struct ScalingList {
    // This is a little wasteful, since sizeID 0 only needs 8 coeffs, and size ID 3 only has 2 arrays, not 6.
    uint8_t sl[4][6][64];
    uint8_t sl_dc[2][6];
} ScalingList;

typedef struct HEVCSPS {
    int vps_id;
    int chroma_format_idc;
    uint8_t separate_colour_plane_flag;

    ///< output (i.e. cropped) values
    int output_width, output_height;
    HEVCWindow output_window;

    HEVCWindow pic_conf_win;

    int bit_depth;
    int pixel_shift;
    enum AVPixelFormat pix_fmt;

    unsigned int log2_max_poc_lsb;
    int pcm_enabled_flag;

    int max_sub_layers;
    struct {
        int max_dec_pic_buffering;
        int num_reorder_pics;
        int max_latency_increase;
    } temporal_layer[MAX_SUB_LAYERS];

    VUI vui;
    PTL ptl;

    uint8_t scaling_list_enable_flag;
    ScalingList scaling_list;

    unsigned int nb_st_rps;
    ShortTermRPS st_rps[MAX_SHORT_TERM_RPS_COUNT];

    uint8_t amp_enabled_flag;
    uint8_t sao_enabled;

    uint8_t long_term_ref_pics_present_flag;
    uint16_t lt_ref_pic_poc_lsb_sps[32];
    uint8_t used_by_curr_pic_lt_sps_flag[32];
    uint8_t num_long_term_ref_pics_sps;

    struct {
        uint8_t bit_depth;
        uint8_t bit_depth_chroma;
        unsigned int log2_min_pcm_cb_size;
        unsigned int log2_max_pcm_cb_size;
        uint8_t loop_filter_disable_flag;
    } pcm;
    uint8_t sps_temporal_mvp_enabled_flag;
    uint8_t sps_strong_intra_smoothing_enable_flag;

    unsigned int log2_min_cb_size;
    unsigned int log2_diff_max_min_coding_block_size;
    unsigned int log2_min_tb_size;
    unsigned int log2_max_trafo_size;
    unsigned int log2_ctb_size;
    unsigned int log2_min_pu_size;

    int max_transform_hierarchy_depth_inter;
    int max_transform_hierarchy_depth_intra;

    ///< coded frame dimension in various units
    int width;
    int height;
    int ctb_width;
    int ctb_height;
    int ctb_size;
    int min_cb_width;
    int min_cb_height;
    int min_tb_width;
    int min_tb_height;
    int min_pu_width;
    int min_pu_height;

    int hshift[3];
    int vshift[3];

    int qp_bd_offset;
} HEVCSPS;

typedef struct HEVCPPS {
    int sps_id; ///< seq_parameter_set_id

    uint8_t sign_data_hiding_flag;

    uint8_t cabac_init_present_flag;

    int num_ref_idx_l0_default_active; ///< num_ref_idx_l0_default_active_minus1 + 1
    int num_ref_idx_l1_default_active; ///< num_ref_idx_l1_default_active_minus1 + 1
    int pic_init_qp_minus26;

    uint8_t constrained_intra_pred_flag;
    uint8_t transform_skip_enabled_flag;

    uint8_t cu_qp_delta_enabled_flag;
    int diff_cu_qp_delta_depth;

    int cb_qp_offset;
    int cr_qp_offset;
    uint8_t pic_slice_level_chroma_qp_offsets_present_flag;
    uint8_t weighted_pred_flag;
    uint8_t weighted_bipred_flag;
    uint8_t output_flag_present_flag;
    uint8_t transquant_bypass_enable_flag;

    uint8_t dependent_slice_segments_enabled_flag;
    uint8_t tiles_enabled_flag;
    uint8_t entropy_coding_sync_enabled_flag;

    int num_tile_columns; ///< num_tile_columns_minus1 + 1
    int num_tile_rows; ///< num_tile_rows_minus1 + 1
    uint8_t uniform_spacing_flag;
    uint8_t loop_filter_across_tiles_enabled_flag;

    uint8_t seq_loop_filter_across_slices_enabled_flag;

    uint8_t deblocking_filter_control_present_flag;
    uint8_t deblocking_filter_override_enabled_flag;
    uint8_t disable_dbf;
    int beta_offset; ///< beta_offset_div2 * 2
    int tc_offset; ///< tc_offset_div2 * 2

    int pps_scaling_list_data_present_flag;
    ScalingList scaling_list;

    uint8_t lists_modification_present_flag;
    int log2_parallel_merge_level; ///< log2_parallel_merge_level_minus2 + 2
    int num_extra_slice_header_bits;
    uint8_t slice_header_extension_present_flag;

    uint8_t pps_extension_flag;
    uint8_t pps_extension_data_flag;

    // Inferred parameters
    int *column_width; ///< ColumnWidth
    int *row_height; ///< RowHeight
    int *col_bd; ///< ColBd
    int *row_bd; ///< RowBd
    int *col_idxX;

    int *ctb_addr_rs_to_ts; ///< CtbAddrRSToTS
    int *ctb_addr_ts_to_rs; ///< CtbAddrTSToRS
    int *tile_id; ///< TileId
    int *tile_pos_rs; ///< TilePosRS
    int *min_cb_addr_zs; ///< MinCbAddrZS
    int *min_tb_addr_zs; ///< MinTbAddrZS
} HEVCPPS;

typedef struct SliceHeader {
    int pps_id;

    ///< address (in raster order) of the first block in the current slice segment
    unsigned int   slice_segment_addr;
    ///< address (in raster order) of the first block in the current slice
    unsigned int   slice_addr;

    enum SliceType slice_type;

    int pic_order_cnt_lsb;

    uint8_t first_slice_in_pic_flag;
    uint8_t dependent_slice_segment_flag;
    uint8_t pic_output_flag;
    uint8_t colour_plane_id;

    ///< RPS coded in the slice header itself is stored here
    ShortTermRPS slice_rps;
    const ShortTermRPS *short_term_rps;
    LongTermRPS long_term_rps;
    unsigned int list_entry_lx[2][32];

    uint8_t rpl_modification_flag[2];
    uint8_t no_output_of_prior_pics_flag;
    uint8_t slice_temporal_mvp_enabled_flag;

    unsigned int nb_refs[2];

    uint8_t slice_sample_adaptive_offset_flag[3];
    uint8_t mvd_l1_zero_flag;

    uint8_t cabac_init_flag;
    uint8_t disable_deblocking_filter_flag; ///< slice_header_disable_deblocking_filter_flag
    uint8_t slice_loop_filter_across_slices_enabled_flag;
    uint8_t collocated_list;

    unsigned int collocated_ref_idx;

    int slice_qp_delta;
    int slice_cb_qp_offset;
    int slice_cr_qp_offset;

    int beta_offset; ///< beta_offset_div2 * 2
    int tc_offset; ///< tc_offset_div2 * 2

    int max_num_merge_cand; ///< 5 - 5_minus_max_num_merge_cand


    int *entry_point_offset;
    int * offset;
    int * size;
    int num_entry_point_offsets;

    int8_t slice_qp;

    uint8_t luma_log2_weight_denom;
    int16_t chroma_log2_weight_denom;

    int16_t luma_weight_l0[16];
    int16_t chroma_weight_l0[16][2];
    int16_t chroma_weight_l1[16][2];
    int16_t luma_weight_l1[16];

    int16_t luma_offset_l0[16];
    int16_t chroma_offset_l0[16][2];

    int16_t luma_offset_l1[16];
    int16_t chroma_offset_l1[16][2];

    int    slice_ctb_addr_rs;
} SliceHeader;

typedef struct CodingTree {
    int depth; ///< ctDepth
} CodingTree;

typedef struct CodingUnit {
    int x;
    int y;

    enum PredMode pred_mode; ///< PredMode
    enum PartMode part_mode; ///< PartMode

    uint8_t rqt_root_cbf;

    uint8_t pcm_flag;

    // Inferred parameters
    uint8_t intra_split_flag; ///< IntraSplitFlag
    uint8_t max_trafo_depth; ///< MaxTrafoDepth
    uint8_t cu_transquant_bypass_flag;
} CodingUnit;

typedef struct Mv {
    int16_t x;     ///< horizontal component of motion vector
    int16_t y;     ///< vertical component of motion vector
} Mv;

typedef struct MvField {
    Mv  mv[2];
    int8_t ref_idx[2];
    int8_t pred_flag[2];
    uint8_t is_intra;
} MvField;

typedef struct NeighbourAvailable {
    int cand_bottom_left;
    int cand_left;
    int cand_up;
    int cand_up_left;
    int cand_up_right;
    int cand_up_right_sap;
} NeighbourAvailable;

typedef struct PredictionUnit {
    int mpm_idx;
    int rem_intra_luma_pred_mode;
    uint8_t intra_pred_mode[4];
    Mv mvd;
    uint8_t merge_flag;
    uint8_t intra_pred_mode_c;
} PredictionUnit;

typedef struct TransformTree {
    uint8_t cbf_cb[MAX_TRANSFORM_DEPTH][MAX_CU_SIZE * MAX_CU_SIZE];
    uint8_t cbf_cr[MAX_TRANSFORM_DEPTH][MAX_CU_SIZE * MAX_CU_SIZE];
    uint8_t cbf_luma;

    // Inferred parameters
    uint8_t inter_split_flag;
} TransformTree;

typedef struct TransformUnit {
    int cu_qp_delta;

    // Inferred parameters;
    int cur_intra_pred_mode;
    uint8_t is_cu_qp_delta_coded;
} TransformUnit;

typedef struct SAOParams {
    int offset_abs[3][4]; ///< sao_offset_abs
    int offset_sign[3][4]; ///< sao_offset_sign

    int band_position[3]; ///< sao_band_position

    int eo_class[3]; ///< sao_eo_class

    int offset_val[3][5]; ///<SaoOffsetVal

    uint8_t type_idx[3]; ///< sao_type_idx
} SAOParams;

typedef struct DBParams {
    int beta_offset;
    int tc_offset;
} DBParams;

#define HEVC_FRAME_FLAG_OUTPUT    (1 << 0)
#define HEVC_FRAME_FLAG_SHORT_REF (1 << 1)
#define HEVC_FRAME_FLAG_LONG_REF  (1 << 2)

typedef struct HEVCFrame {
    AVFrame *frame;
    ThreadFrame tf;
    MvField *tab_mvf;
    RefPicList *refPicList;
    RefPicListTab **rpl_tab;
    int ctb_count;
    int poc;
    struct HEVCFrame *collocated_ref;

    HEVCWindow window;

    AVBufferRef *tab_mvf_buf;
    AVBufferRef *rpl_tab_buf;
    AVBufferRef *rpl_buf;

    /**
     * A sequence counter, so that old frames are output first
     * after a POC reset
     */
    uint16_t sequence;

    /**
     * A combination of HEVC_FRAME_FLAG_*
     */
    uint8_t flags;
} HEVCFrame;

typedef struct HEVCNAL {
    uint8_t *rbsp_buffer;
    int rbsp_buffer_size;

    int size;
    const uint8_t *data;
} HEVCNAL;

typedef struct HEVCLocalContext {
    DECLARE_ALIGNED(16, int16_t, mc_buffer[(MAX_PB_SIZE + 7) * MAX_PB_SIZE]);
    uint8_t cabac_state[HEVC_CONTEXTS];

    uint8_t first_qp_group;

    GetBitContext gb;
    CABACContext cc;
    TransformTree tt;

    int8_t qp_y;
    int8_t curr_qp_y;

    TransformUnit tu;

    uint8_t ctb_left_flag;
    uint8_t ctb_up_flag;
    uint8_t ctb_up_right_flag;
    uint8_t ctb_up_left_flag;
    int     start_of_tiles_x;
    int     end_of_tiles_x;
    int     end_of_tiles_y;
    uint8_t *edge_emu_buffer;
    int      edge_emu_buffer_size;
    CodingTree ct;
    CodingUnit cu;
    PredictionUnit pu;
    NeighbourAvailable na;

    uint8_t slice_or_tiles_left_boundary;
    uint8_t slice_or_tiles_up_boundary;
} HEVCLocalContext;

typedef struct HEVCContext {
    const AVClass *c;  // needed by private avoptions
    AVCodecContext      *avctx;

    struct HEVCContext  *sList[MAX_NB_THREADS];

    HEVCLocalContext    *HEVClcList[MAX_NB_THREADS];
    HEVCLocalContext    *HEVClc;

    uint8_t             threads_type;
    uint8_t             threads_number;

    int                 width;
    int                 height;

    uint8_t *cabac_state;

    /** 1 if the independent slice segment header was successfully parsed */
    uint8_t slice_initialized;

    AVFrame *frame;
    AVFrame *sao_frame;
    AVFrame *tmp_frame;
    AVFrame *output_frame;
    VPS *vps;
    const HEVCSPS *sps;
    HEVCPPS *pps;
    VPS *vps_list[MAX_VPS_COUNT];
    AVBufferRef *sps_list[MAX_SPS_COUNT];
    AVBufferRef *pps_list[MAX_PPS_COUNT];

    AVBufferPool *tab_mvf_pool;
    AVBufferPool *rpl_tab_pool;

    ///< candidate references for the current frame
    RefPicList rps[5];

    SliceHeader sh;
    SAOParams *sao;
    DBParams *deblock;
    enum NALUnitType nal_unit_type;
    int temporal_id;  ///< temporal_id_plus1 - 1
    HEVCFrame *ref;
    HEVCFrame DPB[32];
    int poc;
    int pocTid0;
    int slice_idx; ///< number of the slice being currently decoded
    int eos;       ///< current packet contains an EOS/EOB NAL
    int max_ra;
    int bs_width;
    int bs_height;

    int is_decoded;

    HEVCPredContext hpc;
    HEVCDSPContext hevcdsp;
    VideoDSPContext vdsp;
    DSPContext dsp;
    int8_t *qp_y_tab;
    uint8_t *split_cu_flag;
    uint8_t *horizontal_bs;
    uint8_t *vertical_bs;

    int32_t *tab_slice_address;

    //  CU
    uint8_t *skip_flag;
    uint8_t *tab_ct_depth;
    // PU
    uint8_t *tab_ipm;


    uint8_t *cbf_luma; // cbf_luma of colocated TU
    uint8_t *is_pcm;

    // CTB-level flags affecting loop filter operation
    uint8_t *filter_slice_edges;

    /** used on BE to byteswap the lines for checksumming */
    uint8_t *checksum_buf;
    int      checksum_buf_size;

    /**
     * Sequence counters for decoded and output frames, so that old
     * frames are output first after a POC reset
     */
    uint16_t seq_decode;
    uint16_t seq_output;

    int enable_parallel_tiles;
    int wpp_err;
    int skipped_bytes;
    int *skipped_bytes_pos;
    int skipped_bytes_pos_size;

    int *skipped_bytes_nal;
    int **skipped_bytes_pos_nal;
    int *skipped_bytes_pos_size_nal;

    uint8_t *data;

    HEVCNAL *nals;
    int nb_nals;
    int nals_allocated;

    // for checking the frame checksums
    struct AVMD5 *md5_ctx;
    uint8_t       md5[3][16];
    uint8_t is_md5;

    int context_initialized;
    int is_nalff;         ///< this flag is != 0 if bitstream is encapsulated
                          ///< as a format defined in 14496-15
    int strict_def_disp_win;

    int nal_length_size;  ///< Number of bytes used for nal length (1, 2 or 4)
    int nuh_layer_id;

} HEVCContext;

int ff_hevc_decode_short_term_rps(HEVCContext *s, ShortTermRPS *rps,
                                  const HEVCSPS *sps, int is_slice_header);
int ff_hevc_decode_nal_vps(HEVCContext *s);
int ff_hevc_decode_nal_sps(HEVCContext *s);
int ff_hevc_decode_nal_pps(HEVCContext *s);
int ff_hevc_decode_nal_sei(HEVCContext *s);

int ff_hevc_extract_rbsp(HEVCContext *s, const uint8_t *src, int length,
                         HEVCNAL *nal);

/**
 * Mark all frames in DPB as unused for reference.
 */
void ff_hevc_clear_refs(HEVCContext *s);

/**
 * Drop all frames currently in DPB.
 */
void ff_hevc_flush_dpb(HEVCContext *s);

/**
 * Compute POC of the current frame and return it.
 */
int ff_hevc_compute_poc(HEVCContext *s, int poc_lsb);

RefPicList* ff_hevc_get_ref_list(HEVCContext *s, HEVCFrame *frame, int x0, int y0);

/**
 * Construct the reference picture sets for the current frame.
 */
int ff_hevc_frame_rps(HEVCContext *s);

/**
 * Construct the reference picture list(s) for the current slice.
 */
int ff_hevc_slice_rpl(HEVCContext *s);

void ff_hevc_save_states(HEVCContext *s, int ctb_addr_ts);
void ff_hevc_cabac_init(HEVCContext *s, int ctb_addr_ts);
int ff_hevc_sao_merge_flag_decode(HEVCContext *s);
int ff_hevc_sao_type_idx_decode(HEVCContext *s);
int ff_hevc_sao_band_position_decode(HEVCContext *s);
int ff_hevc_sao_offset_abs_decode(HEVCContext *s);
int ff_hevc_sao_offset_sign_decode(HEVCContext *s);
int ff_hevc_sao_eo_class_decode(HEVCContext *s);
int ff_hevc_end_of_slice_flag_decode(HEVCContext *s);
int ff_hevc_cu_transquant_bypass_flag_decode(HEVCContext *s);
int ff_hevc_skip_flag_decode(HEVCContext *s, int x0, int y0, int x_cb, int y_cb);
int ff_hevc_pred_mode_decode(HEVCContext *s);
int ff_hevc_split_coding_unit_flag_decode(HEVCContext *s, int ct_depth, int x0, int y0);
int ff_hevc_part_mode_decode(HEVCContext *s, int log2_cb_size);
int ff_hevc_pcm_flag_decode(HEVCContext *s);
int ff_hevc_prev_intra_luma_pred_flag_decode(HEVCContext *s);
int ff_hevc_mpm_idx_decode(HEVCContext *s);
int ff_hevc_rem_intra_luma_pred_mode_decode(HEVCContext *s);
int ff_hevc_intra_chroma_pred_mode_decode(HEVCContext *s);
int ff_hevc_merge_idx_decode(HEVCContext *s);
int ff_hevc_merge_flag_decode(HEVCContext *s);
int ff_hevc_inter_pred_idc_decode(HEVCContext *s, int nPbW, int nPbH);
int ff_hevc_ref_idx_lx_decode(HEVCContext *s, int num_ref_idx_lx);
int ff_hevc_mvp_lx_flag_decode(HEVCContext *s);
int ff_hevc_no_residual_syntax_flag_decode(HEVCContext *s);
int ff_hevc_split_transform_flag_decode(HEVCContext *s, int log2_trafo_size);
int ff_hevc_cbf_cb_cr_decode(HEVCContext *s, int trafo_depth);
int ff_hevc_cbf_luma_decode(HEVCContext *s, int trafo_depth);
int ff_hevc_transform_skip_flag_decode(HEVCContext *s, int c_idx);

/**
 * Get the number of candidate references for the current frame.
 */
int ff_hevc_frame_nb_refs(HEVCContext *s);

int ff_hevc_set_new_ref(HEVCContext *s, AVFrame **frame, int poc);

/**
 * Find next frame in output order and put a reference to it in frame.
 * @return 1 if a frame was output, 0 otherwise
 */
int ff_hevc_output_frame(HEVCContext *s, AVFrame *frame, int flush);

void ff_hevc_unref_frame(HEVCContext *s, HEVCFrame *frame, int flags);

void ff_hevc_set_neighbour_available(HEVCContext *s, int x0, int y0, int nPbW, int nPbH);
void ff_hevc_luma_mv_merge_mode(HEVCContext *s, int x0, int y0, int nPbW, int nPbH, int log2_cb_size, int part_idx, int merge_idx, MvField *mv);
void ff_hevc_luma_mv_mvp_mode(HEVCContext *s, int x0, int y0, int nPbW, int nPbH, int log2_cb_size, int part_idx, int merge_idx, MvField *mv , int mvp_lx_flag, int LX);
void ff_hevc_set_qPy(HEVCContext *s, int xC, int yC, int xBase, int yBase, int log2_cb_size);
void ff_hevc_deblocking_boundary_strengths(HEVCContext *s, int x0, int y0, int log2_trafo_size,
                                           int slice_or_tiles_up_boundary, int slice_or_tiles_left_boundary);
int ff_hevc_cu_qp_delta_sign_flag(HEVCContext *s);
int ff_hevc_cu_qp_delta_abs(HEVCContext *s);
void ff_hevc_hls_filter(HEVCContext *s, int x, int y);
void ff_hevc_hls_filters(HEVCContext *s, int x_ctb, int y_ctb, int ctb_size);
void ff_hevc_hls_residual_coding(HEVCContext *s, int x0, int y0,
                                 int log2_trafo_size, enum ScanType scan_idx,
                                 int c_idx);

void ff_hevc_hls_mvd_coding(HEVCContext *s, int x0, int y0, int log2_cb_size);

void ff_hevc_pps_free(HEVCPPS **ppps);

extern const uint8_t ff_hevc_qpel_extra_before[4];
extern const uint8_t ff_hevc_qpel_extra_after[4];
extern const uint8_t ff_hevc_qpel_extra[4];

extern const uint8_t ff_hevc_diag_scan4x4_x[16];
extern const uint8_t ff_hevc_diag_scan4x4_y[16];
extern const uint8_t ff_hevc_diag_scan8x8_x[64];
extern const uint8_t ff_hevc_diag_scan8x8_y[64];

#endif // AVCODEC_HEVC_H
