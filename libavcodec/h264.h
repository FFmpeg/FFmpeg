/*
 * H.26L/H.264/AVC/JVT/14496-10/... encoder/decoder
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
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

/**
 * @file h264.h
 * H.264 / AVC / MPEG4 part10 codec.
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#ifndef FFMPEG_H264_H
#define FFMPEG_H264_H

#include "dsputil.h"
#include "cabac.h"
#include "mpegvideo.h"
#include "h264pred.h"

#define interlaced_dct interlaced_dct_is_a_bad_name
#define mb_intra mb_intra_is_not_initialized_see_mb_type

#define LUMA_DC_BLOCK_INDEX   25
#define CHROMA_DC_BLOCK_INDEX 26

#define CHROMA_DC_COEFF_TOKEN_VLC_BITS 8
#define COEFF_TOKEN_VLC_BITS           8
#define TOTAL_ZEROS_VLC_BITS           9
#define CHROMA_DC_TOTAL_ZEROS_VLC_BITS 3
#define RUN_VLC_BITS                   3
#define RUN7_VLC_BITS                  6

#define MAX_SPS_COUNT 32
#define MAX_PPS_COUNT 256

#define MAX_MMCO_COUNT 66

/* Compiling in interlaced support reduces the speed
 * of progressive decoding by about 2%. */
#define ALLOW_INTERLACE

#ifdef ALLOW_INTERLACE
#define MB_MBAFF h->mb_mbaff
#define MB_FIELD h->mb_field_decoding_flag
#define FRAME_MBAFF h->mb_aff_frame
#define FIELD_PICTURE (s->picture_structure != PICT_FRAME)
#else
#define MB_MBAFF 0
#define MB_FIELD 0
#define FRAME_MBAFF 0
#define FIELD_PICTURE 0
#undef  IS_INTERLACED
#define IS_INTERLACED(mb_type) 0
#endif
#define FIELD_OR_MBAFF_PICTURE (FRAME_MBAFF || FIELD_PICTURE)

/**
 * Sequence parameter set
 */
typedef struct SPS{

    int profile_idc;
    int level_idc;
    int transform_bypass;              ///< qpprime_y_zero_transform_bypass_flag
    int log2_max_frame_num;            ///< log2_max_frame_num_minus4 + 4
    int poc_type;                      ///< pic_order_cnt_type
    int log2_max_poc_lsb;              ///< log2_max_pic_order_cnt_lsb_minus4
    int delta_pic_order_always_zero_flag;
    int offset_for_non_ref_pic;
    int offset_for_top_to_bottom_field;
    int poc_cycle_length;              ///< num_ref_frames_in_pic_order_cnt_cycle
    int ref_frame_count;               ///< num_ref_frames
    int gaps_in_frame_num_allowed_flag;
    int mb_width;                      ///< pic_width_in_mbs_minus1 + 1
    int mb_height;                     ///< pic_height_in_map_units_minus1 + 1
    int frame_mbs_only_flag;
    int mb_aff;                        ///<mb_adaptive_frame_field_flag
    int direct_8x8_inference_flag;
    int crop;                   ///< frame_cropping_flag
    int crop_left;              ///< frame_cropping_rect_left_offset
    int crop_right;             ///< frame_cropping_rect_right_offset
    int crop_top;               ///< frame_cropping_rect_top_offset
    int crop_bottom;            ///< frame_cropping_rect_bottom_offset
    int vui_parameters_present_flag;
    AVRational sar;
    int timing_info_present_flag;
    uint32_t num_units_in_tick;
    uint32_t time_scale;
    int fixed_frame_rate_flag;
    short offset_for_ref_frame[256]; //FIXME dyn aloc?
    int bitstream_restriction_flag;
    int num_reorder_frames;
    int scaling_matrix_present;
    uint8_t scaling_matrix4[6][16];
    uint8_t scaling_matrix8[2][64];
}SPS;

/**
 * Picture parameter set
 */
typedef struct PPS{
    unsigned int sps_id;
    int cabac;                  ///< entropy_coding_mode_flag
    int pic_order_present;      ///< pic_order_present_flag
    int slice_group_count;      ///< num_slice_groups_minus1 + 1
    int mb_slice_group_map_type;
    unsigned int ref_count[2];  ///< num_ref_idx_l0/1_active_minus1 + 1
    int weighted_pred;          ///< weighted_pred_flag
    int weighted_bipred_idc;
    int init_qp;                ///< pic_init_qp_minus26 + 26
    int init_qs;                ///< pic_init_qs_minus26 + 26
    int chroma_qp_index_offset[2];
    int deblocking_filter_parameters_present; ///< deblocking_filter_parameters_present_flag
    int constrained_intra_pred; ///< constrained_intra_pred_flag
    int redundant_pic_cnt_present; ///< redundant_pic_cnt_present_flag
    int transform_8x8_mode;     ///< transform_8x8_mode_flag
    uint8_t scaling_matrix4[6][16];
    uint8_t scaling_matrix8[2][64];
    uint8_t chroma_qp_table[2][256];  ///< pre-scaled (with chroma_qp_index_offset) version of qp_table
    int chroma_qp_diff;
}PPS;

/**
 * Memory management control operation opcode.
 */
typedef enum MMCOOpcode{
    MMCO_END=0,
    MMCO_SHORT2UNUSED,
    MMCO_LONG2UNUSED,
    MMCO_SHORT2LONG,
    MMCO_SET_MAX_LONG,
    MMCO_RESET,
    MMCO_LONG,
} MMCOOpcode;

/**
 * Memory management control operation.
 */
typedef struct MMCO{
    MMCOOpcode opcode;
    int short_pic_num;  ///< pic_num without wrapping (pic_num & max_pic_num)
    int long_arg;       ///< index, pic_num, or num long refs depending on opcode
} MMCO;

/**
 * H264Context
 */
typedef struct H264Context{
    MpegEncContext s;
    int nal_ref_idc;
    int nal_unit_type;
    uint8_t *rbsp_buffer[2];
    unsigned int rbsp_buffer_size[2];

    /**
      * Used to parse AVC variant of h264
      */
    int is_avc; ///< this flag is != 0 if codec is avc1
    int got_avcC; ///< flag used to parse avcC data only once
    int nal_length_size; ///< Number of bytes used for nal length (1, 2 or 4)

    int chroma_qp[2]; //QPc

    int prev_mb_skipped;
    int next_mb_skipped;

    //prediction stuff
    int chroma_pred_mode;
    int intra16x16_pred_mode;

    int top_mb_xy;
    int left_mb_xy[2];

    int8_t intra4x4_pred_mode_cache[5*8];
    int8_t (*intra4x4_pred_mode)[8];
    H264PredContext hpc;
    unsigned int topleft_samples_available;
    unsigned int top_samples_available;
    unsigned int topright_samples_available;
    unsigned int left_samples_available;
    uint8_t (*top_borders[2])[16+2*8];
    uint8_t left_border[2*(17+2*9)];

    /**
     * non zero coeff count cache.
     * is 64 if not available.
     */
    DECLARE_ALIGNED_8(uint8_t, non_zero_count_cache[6*8]);
    uint8_t (*non_zero_count)[16];

    /**
     * Motion vector cache.
     */
    DECLARE_ALIGNED_8(int16_t, mv_cache[2][5*8][2]);
    DECLARE_ALIGNED_8(int8_t, ref_cache[2][5*8]);
#define LIST_NOT_USED -1 //FIXME rename?
#define PART_NOT_AVAILABLE -2

    /**
     * is 1 if the specific list MV&references are set to 0,0,-2.
     */
    int mv_cache_clean[2];

    /**
     * number of neighbors (top and/or left) that used 8x8 dct
     */
    int neighbor_transform_size;

    /**
     * block_offset[ 0..23] for frame macroblocks
     * block_offset[24..47] for field macroblocks
     */
    int block_offset[2*(16+8)];

    uint32_t *mb2b_xy; //FIXME are these 4 a good idea?
    uint32_t *mb2b8_xy;
    int b_stride; //FIXME use s->b4_stride
    int b8_stride;

    int mb_linesize;   ///< may be equal to s->linesize or s->linesize*2, for mbaff
    int mb_uvlinesize;

    int emu_edge_width;
    int emu_edge_height;

    int halfpel_flag;
    int thirdpel_flag;

    int unknown_svq3_flag;
    int next_slice_index;

    SPS *sps_buffers[MAX_SPS_COUNT];
    SPS sps; ///< current sps

    PPS *pps_buffers[MAX_PPS_COUNT];
    /**
     * current pps
     */
    PPS pps; //FIXME move to Picture perhaps? (->no) do we need that?

    uint32_t dequant4_buffer[6][52][16];
    uint32_t dequant8_buffer[2][52][64];
    uint32_t (*dequant4_coeff[6])[16];
    uint32_t (*dequant8_coeff[2])[64];
    int dequant_coeff_pps;     ///< reinit tables when pps changes

    int slice_num;
    uint8_t *slice_table_base;
    uint8_t *slice_table;      ///< slice_table_base + 2*mb_stride + 1
    int slice_type;
    int slice_type_fixed;

    //interlacing specific flags
    int mb_aff_frame;
    int mb_field_decoding_flag;
    int mb_mbaff;              ///< mb_aff_frame && mb_field_decoding_flag

    unsigned int sub_mb_type[4];

    //POC stuff
    int poc_lsb;
    int poc_msb;
    int delta_poc_bottom;
    int delta_poc[2];
    int frame_num;
    int prev_poc_msb;             ///< poc_msb of the last reference pic for POC type 0
    int prev_poc_lsb;             ///< poc_lsb of the last reference pic for POC type 0
    int frame_num_offset;         ///< for POC type 2
    int prev_frame_num_offset;    ///< for POC type 2
    int prev_frame_num;           ///< frame_num of the last pic for POC type 1/2

    /**
     * frame_num for frames or 2*frame_num+1 for field pics.
     */
    int curr_pic_num;

    /**
     * max_frame_num or 2*max_frame_num for field pics.
     */
    int max_pic_num;

    //Weighted pred stuff
    int use_weight;
    int use_weight_chroma;
    int luma_log2_weight_denom;
    int chroma_log2_weight_denom;
    int luma_weight[2][48];
    int luma_offset[2][48];
    int chroma_weight[2][48][2];
    int chroma_offset[2][48][2];
    int implicit_weight[48][48];

    //deblock
    int deblocking_filter;         ///< disable_deblocking_filter_idc with 1<->0
    int slice_alpha_c0_offset;
    int slice_beta_offset;

    int redundant_pic_count;

    int direct_spatial_mv_pred;
    int dist_scale_factor[16];
    int dist_scale_factor_field[32];
    int map_col_to_list0[2][16];
    int map_col_to_list0_field[2][32];

    /**
     * num_ref_idx_l0/1_active_minus1 + 1
     */
    unsigned int ref_count[2];   ///< counts frames or fields, depending on current mb mode
    unsigned int list_count;
    Picture *short_ref[32];
    Picture *long_ref[32];
    Picture default_ref_list[2][32]; ///< base reference list for all slices of a coded picture
    Picture ref_list[2][48];         /**< 0..15: frame refs, 16..47: mbaff field refs.
                                          Reordered version of default_ref_list
                                          according to picture reordering in slice header */
    Picture *delayed_pic[18]; //FIXME size?
    Picture *delayed_output_pic;

    /**
     * memory management control operations buffer.
     */
    MMCO mmco[MAX_MMCO_COUNT];
    int mmco_index;

    int long_ref_count;  ///< number of actual long term references
    int short_ref_count; ///< number of actual short term references

    //data partitioning
    GetBitContext intra_gb;
    GetBitContext inter_gb;
    GetBitContext *intra_gb_ptr;
    GetBitContext *inter_gb_ptr;

    DECLARE_ALIGNED_16(DCTELEM, mb[16*24]);
    DCTELEM mb_padding[256];        ///< as mb is addressed by scantable[i] and scantable is uint8_t we can either check that i is not too large or ensure that there is some unused stuff after mb

    /**
     * Cabac
     */
    CABACContext cabac;
    uint8_t      cabac_state[460];
    int          cabac_init_idc;

    /* 0x100 -> non null luma_dc, 0x80/0x40 -> non null chroma_dc (cb/cr), 0x?0 -> chroma_cbp(0,1,2), 0x0? luma_cbp */
    uint16_t     *cbp_table;
    int cbp;
    int top_cbp;
    int left_cbp;
    /* chroma_pred_mode for i4x4 or i16x16, else 0 */
    uint8_t     *chroma_pred_mode_table;
    int         last_qscale_diff;
    int16_t     (*mvd_table[2])[2];
    DECLARE_ALIGNED_8(int16_t, mvd_cache[2][5*8][2]);
    uint8_t     *direct_table;
    uint8_t     direct_cache[5*8];

    uint8_t zigzag_scan[16];
    uint8_t zigzag_scan8x8[64];
    uint8_t zigzag_scan8x8_cavlc[64];
    uint8_t field_scan[16];
    uint8_t field_scan8x8[64];
    uint8_t field_scan8x8_cavlc[64];
    const uint8_t *zigzag_scan_q0;
    const uint8_t *zigzag_scan8x8_q0;
    const uint8_t *zigzag_scan8x8_cavlc_q0;
    const uint8_t *field_scan_q0;
    const uint8_t *field_scan8x8_q0;
    const uint8_t *field_scan8x8_cavlc_q0;

    int x264_build;

    /**
     * @defgroup multithreading Members for slice based multithreading
     * @{
     */
    struct H264Context *thread_context[MAX_THREADS];

    /**
     * current slice number, used to initalize slice_num of each thread/context
     */
    int current_slice;

    /**
     * Max number of threads / contexts.
     * This is equal to AVCodecContext.thread_count unless
     * multithreaded decoding is impossible, in which case it is
     * reduced to 1.
     */
    int max_contexts;

    /**
     *  1 if the single thread fallback warning has already been
     *  displayed, 0 otherwise.
     */
    int single_decode_warning;

    int last_slice_type;
    /** @} */

}H264Context;

#endif /* FFMPEG_H264_H */
