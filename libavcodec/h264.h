/*
 * H.26L/H.264/AVC/JVT/14496-10/... encoder/decoder
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * H.264 / AVC / MPEG4 part10 codec.
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#ifndef AVCODEC_H264_H
#define AVCODEC_H264_H

#include "libavutil/intreadwrite.h"
#include "dsputil.h"
#include "cabac.h"
#include "mpegvideo.h"
#include "h264dsp.h"
#include "h264pred.h"
#include "rectangle.h"

#define interlaced_dct interlaced_dct_is_a_bad_name
#define mb_intra       mb_intra_is_not_initialized_see_mb_type

#define MAX_SPS_COUNT          32
#define MAX_PPS_COUNT         256

#define MAX_MMCO_COUNT         66

#define MAX_DELAYED_PIC_COUNT  16

/* Compiling in interlaced support reduces the speed
 * of progressive decoding by about 2%. */
#define ALLOW_INTERLACE

#define FMO 0

/**
 * The maximum number of slices supported by the decoder.
 * must be a power of 2
 */
#define MAX_SLICES 16

#ifdef ALLOW_INTERLACE
#define MB_MBAFF    h->mb_mbaff
#define MB_FIELD    h->mb_field_decoding_flag
#define FRAME_MBAFF h->mb_aff_frame
#define FIELD_PICTURE (s->picture_structure != PICT_FRAME)
#define LEFT_MBS 2
#define LTOP     0
#define LBOT     1
#define LEFT(i)  (i)
#else
#define MB_MBAFF      0
#define MB_FIELD      0
#define FRAME_MBAFF   0
#define FIELD_PICTURE 0
#undef  IS_INTERLACED
#define IS_INTERLACED(mb_type) 0
#define LEFT_MBS 1
#define LTOP     0
#define LBOT     0
#define LEFT(i)  0
#endif
#define FIELD_OR_MBAFF_PICTURE (FRAME_MBAFF || FIELD_PICTURE)

#ifndef CABAC
#define CABAC h->pps.cabac
#endif

#define CHROMA422 (h->sps.chroma_format_idc == 2)
#define CHROMA444 (h->sps.chroma_format_idc == 3)

#define EXTENDED_SAR       255

#define MB_TYPE_REF0       MB_TYPE_ACPRED // dirty but it fits in 16 bit
#define MB_TYPE_8x8DCT     0x01000000
#define IS_REF0(a)         ((a) & MB_TYPE_REF0)
#define IS_8x8DCT(a)       ((a) & MB_TYPE_8x8DCT)

/**
 * Value of Picture.reference when Picture is not a reference picture, but
 * is held for delayed output.
 */
#define DELAYED_PIC_REF 4

#define QP_MAX_NUM (51 + 2 * 6)           // The maximum supported qp

/* NAL unit types */
enum {
    NAL_SLICE = 1,
    NAL_DPA,
    NAL_DPB,
    NAL_DPC,
    NAL_IDR_SLICE,
    NAL_SEI,
    NAL_SPS,
    NAL_PPS,
    NAL_AUD,
    NAL_END_SEQUENCE,
    NAL_END_STREAM,
    NAL_FILLER_DATA,
    NAL_SPS_EXT,
    NAL_AUXILIARY_SLICE = 19
};

/**
 * SEI message types
 */
typedef enum {
    SEI_BUFFERING_PERIOD            = 0,   ///< buffering period (H.264, D.1.1)
    SEI_TYPE_PIC_TIMING             = 1,   ///< picture timing
    SEI_TYPE_USER_DATA_UNREGISTERED = 5,   ///< unregistered user data
    SEI_TYPE_RECOVERY_POINT         = 6    ///< recovery point (frame # to decoder sync)
} SEI_Type;

/**
 * pic_struct in picture timing SEI message
 */
typedef enum {
    SEI_PIC_STRUCT_FRAME             = 0, ///<  0: %frame
    SEI_PIC_STRUCT_TOP_FIELD         = 1, ///<  1: top field
    SEI_PIC_STRUCT_BOTTOM_FIELD      = 2, ///<  2: bottom field
    SEI_PIC_STRUCT_TOP_BOTTOM        = 3, ///<  3: top field, bottom field, in that order
    SEI_PIC_STRUCT_BOTTOM_TOP        = 4, ///<  4: bottom field, top field, in that order
    SEI_PIC_STRUCT_TOP_BOTTOM_TOP    = 5, ///<  5: top field, bottom field, top field repeated, in that order
    SEI_PIC_STRUCT_BOTTOM_TOP_BOTTOM = 6, ///<  6: bottom field, top field, bottom field repeated, in that order
    SEI_PIC_STRUCT_FRAME_DOUBLING    = 7, ///<  7: %frame doubling
    SEI_PIC_STRUCT_FRAME_TRIPLING    = 8  ///<  8: %frame tripling
} SEI_PicStructType;

/**
 * Sequence parameter set
 */
typedef struct SPS {
    int profile_idc;
    int level_idc;
    int chroma_format_idc;
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
    int mb_aff;                        ///< mb_adaptive_frame_field_flag
    int direct_8x8_inference_flag;
    int crop;                          ///< frame_cropping_flag
    unsigned int crop_left;            ///< frame_cropping_rect_left_offset
    unsigned int crop_right;           ///< frame_cropping_rect_right_offset
    unsigned int crop_top;             ///< frame_cropping_rect_top_offset
    unsigned int crop_bottom;          ///< frame_cropping_rect_bottom_offset
    int vui_parameters_present_flag;
    AVRational sar;
    int video_signal_type_present_flag;
    int full_range;
    int colour_description_present_flag;
    enum AVColorPrimaries color_primaries;
    enum AVColorTransferCharacteristic color_trc;
    enum AVColorSpace colorspace;
    int timing_info_present_flag;
    uint32_t num_units_in_tick;
    uint32_t time_scale;
    int fixed_frame_rate_flag;
    short offset_for_ref_frame[256]; // FIXME dyn aloc?
    int bitstream_restriction_flag;
    int num_reorder_frames;
    int scaling_matrix_present;
    uint8_t scaling_matrix4[6][16];
    uint8_t scaling_matrix8[6][64];
    int nal_hrd_parameters_present_flag;
    int vcl_hrd_parameters_present_flag;
    int pic_struct_present_flag;
    int time_offset_length;
    int cpb_cnt;                          ///< See H.264 E.1.2
    int initial_cpb_removal_delay_length; ///< initial_cpb_removal_delay_length_minus1 + 1
    int cpb_removal_delay_length;         ///< cpb_removal_delay_length_minus1 + 1
    int dpb_output_delay_length;          ///< dpb_output_delay_length_minus1 + 1
    int bit_depth_luma;                   ///< bit_depth_luma_minus8 + 8
    int bit_depth_chroma;                 ///< bit_depth_chroma_minus8 + 8
    int residual_color_transform_flag;    ///< residual_colour_transform_flag
    int constraint_set_flags;             ///< constraint_set[0-3]_flag
} SPS;

/**
 * Picture parameter set
 */
typedef struct PPS {
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
    int constrained_intra_pred;     ///< constrained_intra_pred_flag
    int redundant_pic_cnt_present;  ///< redundant_pic_cnt_present_flag
    int transform_8x8_mode;         ///< transform_8x8_mode_flag
    uint8_t scaling_matrix4[6][16];
    uint8_t scaling_matrix8[6][64];
    uint8_t chroma_qp_table[2][64]; ///< pre-scaled (with chroma_qp_index_offset) version of qp_table
    int chroma_qp_diff;
} PPS;

/**
 * Memory management control operation opcode.
 */
typedef enum MMCOOpcode {
    MMCO_END = 0,
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
typedef struct MMCO {
    MMCOOpcode opcode;
    int short_pic_num;  ///< pic_num without wrapping (pic_num & max_pic_num)
    int long_arg;       ///< index, pic_num, or num long refs depending on opcode
} MMCO;

/**
 * H264Context
 */
typedef struct H264Context {
    MpegEncContext s;
    H264DSPContext h264dsp;
    int pixel_shift;    ///< 0 for 8-bit H264, 1 for high-bit-depth H264
    int chroma_qp[2];   // QPc

    int qp_thresh;      ///< QP threshold to skip loopfilter

    int prev_mb_skipped;
    int next_mb_skipped;

    // prediction stuff
    int chroma_pred_mode;
    int intra16x16_pred_mode;

    int topleft_mb_xy;
    int top_mb_xy;
    int topright_mb_xy;
    int left_mb_xy[LEFT_MBS];

    int topleft_type;
    int top_type;
    int topright_type;
    int left_type[LEFT_MBS];

    const uint8_t *left_block;
    int topleft_partition;

    int8_t intra4x4_pred_mode_cache[5 * 8];
    int8_t(*intra4x4_pred_mode);
    H264PredContext hpc;
    unsigned int topleft_samples_available;
    unsigned int top_samples_available;
    unsigned int topright_samples_available;
    unsigned int left_samples_available;
    uint8_t (*top_borders[2])[(16 * 3) * 2];

    /**
     * non zero coeff count cache.
     * is 64 if not available.
     */
    DECLARE_ALIGNED(8, uint8_t, non_zero_count_cache)[15 * 8];

    uint8_t (*non_zero_count)[48];

    /**
     * Motion vector cache.
     */
    DECLARE_ALIGNED(16, int16_t, mv_cache)[2][5 * 8][2];
    DECLARE_ALIGNED(8, int8_t, ref_cache)[2][5 * 8];
#define LIST_NOT_USED -1 // FIXME rename?
#define PART_NOT_AVAILABLE -2

    /**
     * number of neighbors (top and/or left) that used 8x8 dct
     */
    int neighbor_transform_size;

    /**
     * block_offset[ 0..23] for frame macroblocks
     * block_offset[24..47] for field macroblocks
     */
    int block_offset[2 * (16 * 3)];

    uint32_t *mb2b_xy;  // FIXME are these 4 a good idea?
    uint32_t *mb2br_xy;
    int b_stride;       // FIXME use s->b4_stride

    int mb_linesize;    ///< may be equal to s->linesize or s->linesize * 2, for mbaff
    int mb_uvlinesize;

    int emu_edge_width;
    int emu_edge_height;

    SPS sps; ///< current sps

    /**
     * current pps
     */
    PPS pps; // FIXME move to Picture perhaps? (->no) do we need that?

    uint32_t dequant4_buffer[6][QP_MAX_NUM + 1][16]; // FIXME should these be moved down?
    uint32_t dequant8_buffer[6][QP_MAX_NUM + 1][64];
    uint32_t(*dequant4_coeff[6])[16];
    uint32_t(*dequant8_coeff[6])[64];

    int slice_num;
    uint16_t *slice_table;      ///< slice_table_base + 2*mb_stride + 1
    int slice_type;
    int slice_type_nos;         ///< S free slice type (SI/SP are remapped to I/P)
    int slice_type_fixed;

    // interlacing specific flags
    int mb_aff_frame;
    int mb_field_decoding_flag;
    int mb_mbaff;               ///< mb_aff_frame && mb_field_decoding_flag

    DECLARE_ALIGNED(8, uint16_t, sub_mb_type)[4];

    // Weighted pred stuff
    int use_weight;
    int use_weight_chroma;
    int luma_log2_weight_denom;
    int chroma_log2_weight_denom;
    // The following 2 can be changed to int8_t but that causes 10cpu cycles speedloss
    int luma_weight[48][2][2];
    int chroma_weight[48][2][2][2];
    int implicit_weight[48][48][2];

    int direct_spatial_mv_pred;
    int col_parity;
    int col_fieldoff;
    int dist_scale_factor[16];
    int dist_scale_factor_field[2][32];
    int map_col_to_list0[2][16 + 32];
    int map_col_to_list0_field[2][2][16 + 32];

    /**
     * num_ref_idx_l0/1_active_minus1 + 1
     */
    unsigned int ref_count[2];          ///< counts frames or fields, depending on current mb mode
    unsigned int list_count;
    uint8_t *list_counts;               ///< Array of list_count per MB specifying the slice type
    Picture ref_list[2][48];            /**< 0..15: frame refs, 16..47: mbaff field refs.
                                         *   Reordered version of default_ref_list
                                         *   according to picture reordering in slice header */
    int ref2frm[MAX_SLICES][2][64];     ///< reference to frame number lists, used in the loop filter, the first 2 are for -2,-1

    // data partitioning
    GetBitContext intra_gb;
    GetBitContext inter_gb;
    GetBitContext *intra_gb_ptr;
    GetBitContext *inter_gb_ptr;

    DECLARE_ALIGNED(16, DCTELEM, mb)[16 * 48 * 2]; ///< as a dct coeffecient is int32_t in high depth, we need to reserve twice the space.
    DECLARE_ALIGNED(16, DCTELEM, mb_luma_dc)[3][16 * 2];
    DCTELEM mb_padding[256 * 2];        ///< as mb is addressed by scantable[i] and scantable is uint8_t we can either check that i is not too large or ensure that there is some unused stuff after mb

    /**
     * Cabac
     */
    CABACContext cabac;
    uint8_t cabac_state[1024];

    /* 0x100 -> non null luma_dc, 0x80/0x40 -> non null chroma_dc (cb/cr), 0x?0 -> chroma_cbp(0, 1, 2), 0x0? luma_cbp */
    uint16_t *cbp_table;
    int cbp;
    int top_cbp;
    int left_cbp;
    /* chroma_pred_mode for i4x4 or i16x16, else 0 */
    uint8_t *chroma_pred_mode_table;
    int last_qscale_diff;
    uint8_t (*mvd_table[2])[2];
    DECLARE_ALIGNED(16, uint8_t, mvd_cache)[2][5 * 8][2];
    uint8_t *direct_table;
    uint8_t direct_cache[5 * 8];

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

    int mb_xy;

    int is_complex;

    // deblock
    int deblocking_filter;          ///< disable_deblocking_filter_idc with 1 <-> 0
    int slice_alpha_c0_offset;
    int slice_beta_offset;

    // =============================================================
    // Things below are not used in the MB or more inner code

    int nal_ref_idc;
    int nal_unit_type;
    uint8_t *rbsp_buffer[2];
    unsigned int rbsp_buffer_size[2];

    /**
     * Used to parse AVC variant of h264
     */
    int is_avc;           ///< this flag is != 0 if codec is avc1
    int nal_length_size;  ///< Number of bytes used for nal length (1, 2 or 4)
    int got_first;        ///< this flag is != 0 if we've parsed a frame

    SPS *sps_buffers[MAX_SPS_COUNT];
    PPS *pps_buffers[MAX_PPS_COUNT];

    int dequant_coeff_pps;      ///< reinit tables when pps changes

    uint16_t *slice_table_base;

    // POC stuff
    int poc_lsb;
    int poc_msb;
    int delta_poc_bottom;
    int delta_poc[2];
    int frame_num;
    int prev_poc_msb;           ///< poc_msb of the last reference pic for POC type 0
    int prev_poc_lsb;           ///< poc_lsb of the last reference pic for POC type 0
    int frame_num_offset;       ///< for POC type 2
    int prev_frame_num_offset;  ///< for POC type 2
    int prev_frame_num;         ///< frame_num of the last pic for POC type 1/2

    /**
     * frame_num for frames or 2 * frame_num + 1 for field pics.
     */
    int curr_pic_num;

    /**
     * max_frame_num or 2 * max_frame_num for field pics.
     */
    int max_pic_num;

    int redundant_pic_count;

    Picture *short_ref[32];
    Picture *long_ref[32];
    Picture default_ref_list[2][32]; ///< base reference list for all slices of a coded picture
    Picture *delayed_pic[MAX_DELAYED_PIC_COUNT + 2]; // FIXME size?
    int last_pocs[MAX_DELAYED_PIC_COUNT];
    Picture *next_output_pic;
    int outputed_poc;
    int next_outputed_poc;

    /**
     * memory management control operations buffer.
     */
    MMCO mmco[MAX_MMCO_COUNT];
    int mmco_index;
    int mmco_reset;

    int long_ref_count;     ///< number of actual long term references
    int short_ref_count;    ///< number of actual short term references

    int cabac_init_idc;

    /**
     * @name Members for slice based multithreading
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

    /**
     * pic_struct in picture timing SEI message
     */
    SEI_PicStructType sei_pic_struct;

    /**
     * Complement sei_pic_struct
     * SEI_PIC_STRUCT_TOP_BOTTOM and SEI_PIC_STRUCT_BOTTOM_TOP indicate interlaced frames.
     * However, soft telecined frames may have these values.
     * This is used in an attempt to flag soft telecine progressive.
     */
    int prev_interlaced_frame;

    /**
     * Bit set of clock types for fields/frames in picture timing SEI message.
     * For each found ct_type, appropriate bit is set (e.g., bit 1 for
     * interlaced).
     */
    int sei_ct_type;

    /**
     * dpb_output_delay in picture timing SEI message, see H.264 C.2.2
     */
    int sei_dpb_output_delay;

    /**
     * cpb_removal_delay in picture timing SEI message, see H.264 C.1.2
     */
    int sei_cpb_removal_delay;

    /**
     * recovery_frame_cnt from SEI message
     *
     * Set to -1 if no recovery point SEI message found or to number of frames
     * before playback synchronizes. Frames having recovery point are key
     * frames.
     */
    int sei_recovery_frame_cnt;

    int luma_weight_flag[2];    ///< 7.4.3.2 luma_weight_lX_flag
    int chroma_weight_flag[2];  ///< 7.4.3.2 chroma_weight_lX_flag

    // Timestamp stuff
    int sei_buffering_period_present;   ///< Buffering period SEI flag
    int initial_cpb_removal_delay[32];  ///< Initial timestamps for CPBs

    int cur_chroma_format_idc;
} H264Context;

extern const uint8_t ff_h264_chroma_qp[3][QP_MAX_NUM + 1]; ///< One chroma qp table for each supported bit depth (8, 9, 10).
extern const uint16_t ff_h264_mb_sizes[4];

/**
 * Decode SEI
 */
int ff_h264_decode_sei(H264Context *h);

/**
 * Decode SPS
 */
int ff_h264_decode_seq_parameter_set(H264Context *h);

/**
 * compute profile from sps
 */
int ff_h264_get_profile(SPS *sps);

/**
 * Decode PPS
 */
int ff_h264_decode_picture_parameter_set(H264Context *h, int bit_length);

/**
 * Decode a network abstraction layer unit.
 * @param consumed is the number of bytes used as input
 * @param length is the length of the array
 * @param dst_length is the number of decoded bytes FIXME here
 *                   or a decode rbsp tailing?
 * @return decoded bytes, might be src+1 if no escapes
 */
const uint8_t *ff_h264_decode_nal(H264Context *h, const uint8_t *src,
                                  int *dst_length, int *consumed, int length);

/**
 * Free any data that may have been allocated in the H264 context
 * like SPS, PPS etc.
 */
av_cold void ff_h264_free_context(H264Context *h);

/**
 * Reconstruct bitstream slice_type.
 */
int ff_h264_get_slice_type(const H264Context *h);

/**
 * Allocate tables.
 * needs width/height
 */
int ff_h264_alloc_tables(H264Context *h);

/**
 * Fill the default_ref_list.
 */
int ff_h264_fill_default_ref_list(H264Context *h);

int ff_h264_decode_ref_pic_list_reordering(H264Context *h);
void ff_h264_fill_mbaff_ref_list(H264Context *h);
void ff_h264_remove_all_refs(H264Context *h);

/**
 * Execute the reference picture marking (memory management control operations).
 */
int ff_h264_execute_ref_pic_marking(H264Context *h, MMCO *mmco, int mmco_count);

int ff_h264_decode_ref_pic_marking(H264Context *h, GetBitContext *gb);

void ff_generate_sliding_window_mmcos(H264Context *h);

/**
 * Check if the top & left blocks are available if needed & change the
 * dc mode so it only uses the available blocks.
 */
int ff_h264_check_intra4x4_pred_mode(H264Context *h);

/**
 * Check if the top & left blocks are available if needed & change the
 * dc mode so it only uses the available blocks.
 */
int ff_h264_check_intra_pred_mode(H264Context *h, int mode, int is_chroma);

void ff_h264_hl_decode_mb(H264Context *h);
int ff_h264_frame_start(H264Context *h);
int ff_h264_decode_extradata(H264Context *h);
av_cold int ff_h264_decode_init(AVCodecContext *avctx);
av_cold void ff_h264_decode_init_vlc(void);

/**
 * Decode a macroblock
 * @return 0 if OK, ER_AC_ERROR / ER_DC_ERROR / ER_MV_ERROR on error
 */
int ff_h264_decode_mb_cavlc(H264Context *h);

/**
 * Decode a CABAC coded macroblock
 * @return 0 if OK, ER_AC_ERROR / ER_DC_ERROR / ER_MV_ERROR on error
 */
int ff_h264_decode_mb_cabac(H264Context *h);

void ff_h264_init_cabac_states(H264Context *h);

void ff_h264_direct_dist_scale_factor(H264Context *const h);
void ff_h264_direct_ref_list_init(H264Context *const h);
void ff_h264_pred_direct_motion(H264Context *const h, int *mb_type);

void ff_h264_filter_mb_fast(H264Context *h, int mb_x, int mb_y,
                            uint8_t *img_y, uint8_t *img_cb, uint8_t *img_cr,
                            unsigned int linesize, unsigned int uvlinesize);
void ff_h264_filter_mb(H264Context *h, int mb_x, int mb_y,
                       uint8_t *img_y, uint8_t *img_cb, uint8_t *img_cr,
                       unsigned int linesize, unsigned int uvlinesize);

/**
 * Reset SEI values at the beginning of the frame.
 *
 * @param h H.264 context.
 */
void ff_h264_reset_sei(H264Context *h);

/*
 * o-o o-o
 *  / / /
 * o-o o-o
 *  ,---'
 * o-o o-o
 *  / / /
 * o-o o-o
 */

/* Scan8 organization:
 *    0 1 2 3 4 5 6 7
 * 0  DY    y y y y y
 * 1        y Y Y Y Y
 * 2        y Y Y Y Y
 * 3        y Y Y Y Y
 * 4        y Y Y Y Y
 * 5  DU    u u u u u
 * 6        u U U U U
 * 7        u U U U U
 * 8        u U U U U
 * 9        u U U U U
 * 10 DV    v v v v v
 * 11       v V V V V
 * 12       v V V V V
 * 13       v V V V V
 * 14       v V V V V
 * DY/DU/DV are for luma/chroma DC.
 */

#define LUMA_DC_BLOCK_INDEX   48
#define CHROMA_DC_BLOCK_INDEX 49

// This table must be here because scan8[constant] must be known at compiletime
static const uint8_t scan8[16 * 3 + 3] = {
    4 +  1 * 8, 5 +  1 * 8, 4 +  2 * 8, 5 +  2 * 8,
    6 +  1 * 8, 7 +  1 * 8, 6 +  2 * 8, 7 +  2 * 8,
    4 +  3 * 8, 5 +  3 * 8, 4 +  4 * 8, 5 +  4 * 8,
    6 +  3 * 8, 7 +  3 * 8, 6 +  4 * 8, 7 +  4 * 8,
    4 +  6 * 8, 5 +  6 * 8, 4 +  7 * 8, 5 +  7 * 8,
    6 +  6 * 8, 7 +  6 * 8, 6 +  7 * 8, 7 +  7 * 8,
    4 +  8 * 8, 5 +  8 * 8, 4 +  9 * 8, 5 +  9 * 8,
    6 +  8 * 8, 7 +  8 * 8, 6 +  9 * 8, 7 +  9 * 8,
    4 + 11 * 8, 5 + 11 * 8, 4 + 12 * 8, 5 + 12 * 8,
    6 + 11 * 8, 7 + 11 * 8, 6 + 12 * 8, 7 + 12 * 8,
    4 + 13 * 8, 5 + 13 * 8, 4 + 14 * 8, 5 + 14 * 8,
    6 + 13 * 8, 7 + 13 * 8, 6 + 14 * 8, 7 + 14 * 8,
    0 +  0 * 8, 0 +  5 * 8, 0 + 10 * 8
};

static av_always_inline uint32_t pack16to32(int a, int b)
{
#if HAVE_BIGENDIAN
    return (b & 0xFFFF) + (a << 16);
#else
    return (a & 0xFFFF) + (b << 16);
#endif
}

static av_always_inline uint16_t pack8to16(int a, int b)
{
#if HAVE_BIGENDIAN
    return (b & 0xFF) + (a << 8);
#else
    return (a & 0xFF) + (b << 8);
#endif
}

/**
 * Get the chroma qp.
 */
static av_always_inline int get_chroma_qp(H264Context *h, int t, int qscale)
{
    return h->pps.chroma_qp_table[t][qscale];
}

/**
 * Get the predicted intra4x4 prediction mode.
 */
static av_always_inline int pred_intra_mode(H264Context *h, int n)
{
    const int index8 = scan8[n];
    const int left   = h->intra4x4_pred_mode_cache[index8 - 1];
    const int top    = h->intra4x4_pred_mode_cache[index8 - 8];
    const int min    = FFMIN(left, top);

    tprintf(h->s.avctx, "mode:%d %d min:%d\n", left, top, min);

    if (min < 0)
        return DC_PRED;
    else
        return min;
}

static av_always_inline void write_back_intra_pred_mode(H264Context *h)
{
    int8_t *i4x4       = h->intra4x4_pred_mode + h->mb2br_xy[h->mb_xy];
    int8_t *i4x4_cache = h->intra4x4_pred_mode_cache;

    AV_COPY32(i4x4, i4x4_cache + 4 + 8 * 4);
    i4x4[4] = i4x4_cache[7 + 8 * 3];
    i4x4[5] = i4x4_cache[7 + 8 * 2];
    i4x4[6] = i4x4_cache[7 + 8 * 1];
}

static av_always_inline void write_back_non_zero_count(H264Context *h)
{
    const int mb_xy    = h->mb_xy;
    uint8_t *nnz       = h->non_zero_count[mb_xy];
    uint8_t *nnz_cache = h->non_zero_count_cache;

    AV_COPY32(&nnz[ 0], &nnz_cache[4 + 8 * 1]);
    AV_COPY32(&nnz[ 4], &nnz_cache[4 + 8 * 2]);
    AV_COPY32(&nnz[ 8], &nnz_cache[4 + 8 * 3]);
    AV_COPY32(&nnz[12], &nnz_cache[4 + 8 * 4]);
    AV_COPY32(&nnz[16], &nnz_cache[4 + 8 * 6]);
    AV_COPY32(&nnz[20], &nnz_cache[4 + 8 * 7]);
    AV_COPY32(&nnz[32], &nnz_cache[4 + 8 * 11]);
    AV_COPY32(&nnz[36], &nnz_cache[4 + 8 * 12]);

    if (!h->s.chroma_y_shift) {
        AV_COPY32(&nnz[24], &nnz_cache[4 + 8 * 8]);
        AV_COPY32(&nnz[28], &nnz_cache[4 + 8 * 9]);
        AV_COPY32(&nnz[40], &nnz_cache[4 + 8 * 13]);
        AV_COPY32(&nnz[44], &nnz_cache[4 + 8 * 14]);
    }
}

static av_always_inline void write_back_motion_list(H264Context *h,
                                                    MpegEncContext *const s,
                                                    int b_stride,
                                                    int b_xy, int b8_xy,
                                                    int mb_type, int list)
{
    int16_t(*mv_dst)[2] = &s->current_picture.f.motion_val[list][b_xy];
    int16_t(*mv_src)[2] = &h->mv_cache[list][scan8[0]];
    AV_COPY128(mv_dst + 0 * b_stride, mv_src + 8 * 0);
    AV_COPY128(mv_dst + 1 * b_stride, mv_src + 8 * 1);
    AV_COPY128(mv_dst + 2 * b_stride, mv_src + 8 * 2);
    AV_COPY128(mv_dst + 3 * b_stride, mv_src + 8 * 3);
    if (CABAC) {
        uint8_t (*mvd_dst)[2] = &h->mvd_table[list][FMO ? 8 * h->mb_xy
                                                        : h->mb2br_xy[h->mb_xy]];
        uint8_t(*mvd_src)[2]  = &h->mvd_cache[list][scan8[0]];
        if (IS_SKIP(mb_type)) {
            AV_ZERO128(mvd_dst);
        } else {
            AV_COPY64(mvd_dst, mvd_src + 8 * 3);
            AV_COPY16(mvd_dst + 3 + 3, mvd_src + 3 + 8 * 0);
            AV_COPY16(mvd_dst + 3 + 2, mvd_src + 3 + 8 * 1);
            AV_COPY16(mvd_dst + 3 + 1, mvd_src + 3 + 8 * 2);
        }
    }

    {
        int8_t *ref_index = &s->current_picture.f.ref_index[list][b8_xy];
        int8_t *ref_cache = h->ref_cache[list];
        ref_index[0 + 0 * 2] = ref_cache[scan8[0]];
        ref_index[1 + 0 * 2] = ref_cache[scan8[4]];
        ref_index[0 + 1 * 2] = ref_cache[scan8[8]];
        ref_index[1 + 1 * 2] = ref_cache[scan8[12]];
    }
}

static av_always_inline void write_back_motion(H264Context *h, int mb_type)
{
    MpegEncContext *const s = &h->s;
    const int b_stride      = h->b_stride;
    const int b_xy  = 4 * s->mb_x + 4 * s->mb_y * h->b_stride; // try mb2b(8)_xy
    const int b8_xy = 4 * h->mb_xy;

    if (USES_LIST(mb_type, 0)) {
        write_back_motion_list(h, s, b_stride, b_xy, b8_xy, mb_type, 0);
    } else {
        fill_rectangle(&s->current_picture.f.ref_index[0][b8_xy],
                       2, 2, 2, (uint8_t)LIST_NOT_USED, 1);
    }
    if (USES_LIST(mb_type, 1))
        write_back_motion_list(h, s, b_stride, b_xy, b8_xy, mb_type, 1);

    if (h->slice_type_nos == AV_PICTURE_TYPE_B && CABAC) {
        if (IS_8X8(mb_type)) {
            uint8_t *direct_table = &h->direct_table[4 * h->mb_xy];
            direct_table[1] = h->sub_mb_type[1] >> 1;
            direct_table[2] = h->sub_mb_type[2] >> 1;
            direct_table[3] = h->sub_mb_type[3] >> 1;
        }
    }
}

static av_always_inline int get_dct8x8_allowed(H264Context *h)
{
    if (h->sps.direct_8x8_inference_flag)
        return !(AV_RN64A(h->sub_mb_type) &
                 ((MB_TYPE_16x8 | MB_TYPE_8x16 | MB_TYPE_8x8) *
                  0x0001000100010001ULL));
    else
        return !(AV_RN64A(h->sub_mb_type) &
                 ((MB_TYPE_16x8 | MB_TYPE_8x16 | MB_TYPE_8x8 | MB_TYPE_DIRECT2) *
                  0x0001000100010001ULL));
}

#endif /* AVCODEC_H264_H */
