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

/**
 * @file
 * EVC decoder/parser shared code
 */

#ifndef AVCODEC_EVC_PARSE_H
#define AVCODEC_EVC_PARSE_H

#include <stdint.h>

#include "libavutil/intreadwrite.h"
#include "libavutil/log.h"
#include "libavutil/rational.h"
#include "evc.h"

#define EVC_MAX_QP_TABLE_SIZE   58
#define NUM_CPB                 32

// rpl structure
typedef struct RefPicListStruct {
    int poc;
    int tid;
    int ref_pic_num;
    int ref_pic_active_num;
    int ref_pics[EVC_MAX_NUM_REF_PICS];
    char pic_type;

} RefPicListStruct;

// chromaQP table structure to be signalled in SPS
typedef struct ChromaQpTable {
    int chroma_qp_table_present_flag;       // u(1)
    int same_qp_table_for_chroma;           // u(1)
    int global_offset_flag;                 // u(1)
    int num_points_in_qp_table_minus1[2];   // ue(v)
    int delta_qp_in_val_minus1[2][EVC_MAX_QP_TABLE_SIZE];   // u(6)
    int delta_qp_out_val[2][EVC_MAX_QP_TABLE_SIZE];         // se(v)
} ChromaQpTable;

// Hypothetical Reference Decoder (HRD) parameters, part of VUI
typedef struct HRDParameters {
    int cpb_cnt_minus1;                             // ue(v)
    int bit_rate_scale;                             // u(4)
    int cpb_size_scale;                             // u(4)
    int bit_rate_value_minus1[NUM_CPB];             // ue(v)
    int cpb_size_value_minus1[NUM_CPB];             // ue(v)
    int cbr_flag[NUM_CPB];                          // u(1)
    int initial_cpb_removal_delay_length_minus1;    // u(5)
    int cpb_removal_delay_length_minus1;            // u(5)
    int dpb_output_delay_length_minus1;             // u(5)
    int time_offset_length;                         // u(5)
} HRDParameters;

// video usability information (VUI) part of SPS
typedef struct VUIParameters {
    int aspect_ratio_info_present_flag;             // u(1)
    int aspect_ratio_idc;                           // u(8)
    int sar_width;                                  // u(16)
    int sar_height;                                 // u(16)
    int overscan_info_present_flag;                 // u(1)
    int overscan_appropriate_flag;                  // u(1)
    int video_signal_type_present_flag;             // u(1)
    int video_format;                               // u(3)
    int video_full_range_flag;                      // u(1)
    int colour_description_present_flag;            // u(1)
    int colour_primaries;                           // u(8)
    int transfer_characteristics;                   // u(8)
    int matrix_coefficients;                        // u(8)
    int chroma_loc_info_present_flag;               // u(1)
    int chroma_sample_loc_type_top_field;           // ue(v)
    int chroma_sample_loc_type_bottom_field;        // ue(v)
    int neutral_chroma_indication_flag;             // u(1)
    int field_seq_flag;                             // u(1)
    int timing_info_present_flag;                   // u(1)
    int num_units_in_tick;                          // u(32)
    int time_scale;                                 // u(32)
    int fixed_pic_rate_flag;                        // u(1)
    int nal_hrd_parameters_present_flag;            // u(1)
    int vcl_hrd_parameters_present_flag;            // u(1)
    int low_delay_hrd_flag;                         // u(1)
    int pic_struct_present_flag;                    // u(1)
    int bitstream_restriction_flag;                 // u(1)
    int motion_vectors_over_pic_boundaries_flag;    // u(1)
    int max_bytes_per_pic_denom;                    // ue(v)
    int max_bits_per_mb_denom;                      // ue(v)
    int log2_max_mv_length_horizontal;              // ue(v)
    int log2_max_mv_length_vertical;                // ue(v)
    int num_reorder_pics;                           // ue(v)
    int max_dec_pic_buffering;                      // ue(v)

    HRDParameters hrd_parameters;
} VUIParameters;

// The sturcture reflects SPS RBSP(raw byte sequence payload) layout
// @see ISO_IEC_23094-1 section 7.3.2.1
//
// The following descriptors specify the parsing process of each element
// u(n) - unsigned integer using n bits
// ue(v) - unsigned integer 0-th order Exp_Golomb-coded syntax element with the left bit first
typedef struct EVCParserSPS {
    int sps_seq_parameter_set_id;   // ue(v)
    int profile_idc;                // u(8)
    int level_idc;                  // u(8)
    int toolset_idc_h;              // u(32)
    int toolset_idc_l;              // u(32)
    int chroma_format_idc;          // ue(v)
    int pic_width_in_luma_samples;  // ue(v)
    int pic_height_in_luma_samples; // ue(v)
    int bit_depth_luma_minus8;      // ue(v)
    int bit_depth_chroma_minus8;    // ue(v)

    int sps_btt_flag;                           // u(1)
    int log2_ctu_size_minus5;                   // ue(v)
    int log2_min_cb_size_minus2;                // ue(v)
    int log2_diff_ctu_max_14_cb_size;           // ue(v)
    int log2_diff_ctu_max_tt_cb_size;           // ue(v)
    int log2_diff_min_cb_min_tt_cb_size_minus2; // ue(v)

    int sps_suco_flag;                       // u(1)
    int log2_diff_ctu_size_max_suco_cb_size; // ue(v)
    int log2_diff_max_suco_min_suco_cb_size; // ue(v)

    int sps_admvp_flag;     // u(1)
    int sps_affine_flag;    // u(1)
    int sps_amvr_flag;      // u(1)
    int sps_dmvr_flag;      // u(1)
    int sps_mmvd_flag;      // u(1)
    int sps_hmvp_flag;      // u(1)

    int sps_eipd_flag;                 // u(1)
    int sps_ibc_flag;                  // u(1)
    int log2_max_ibc_cand_size_minus2; // ue(v)

    int sps_cm_init_flag; // u(1)
    int sps_adcc_flag;    // u(1)

    int sps_iqt_flag; // u(1)
    int sps_ats_flag; // u(1)

    int sps_addb_flag;   // u(1)
    int sps_alf_flag;    // u(1)
    int sps_htdf_flag;   // u(1)
    int sps_rpl_flag;    // u(1)
    int sps_pocs_flag;   // u(1)
    int sps_dquant_flag; // u(1)
    int sps_dra_flag;    // u(1)

    int log2_max_pic_order_cnt_lsb_minus4; // ue(v)
    int log2_sub_gop_length;               // ue(v)
    int log2_ref_pic_gap_length;           // ue(v)

    int max_num_tid0_ref_pics; // ue(v)

    int sps_max_dec_pic_buffering_minus1; // ue(v)
    int long_term_ref_pic_flag;           // u(1)
    int rpl1_same_as_rpl0_flag;           // u(1)
    int num_ref_pic_list_in_sps[2];       // ue(v)
    struct RefPicListStruct rpls[2][EVC_MAX_NUM_RPLS];

    int picture_cropping_flag;      // u(1)
    int picture_crop_left_offset;   // ue(v)
    int picture_crop_right_offset;  // ue(v)
    int picture_crop_top_offset;    // ue(v)
    int picture_crop_bottom_offset; // ue(v)

    struct ChromaQpTable chroma_qp_table_struct;

    int vui_parameters_present_flag;    // u(1)

    struct VUIParameters vui_parameters;

} EVCParserSPS;

typedef struct EVCParserPPS {
    int pps_pic_parameter_set_id;                           // ue(v)
    int pps_seq_parameter_set_id;                           // ue(v)
    int num_ref_idx_default_active_minus1[2];               // ue(v)
    int additional_lt_poc_lsb_len;                          // ue(v)
    int rpl1_idx_present_flag;                              // u(1)
    int single_tile_in_pic_flag;                            // u(1)
    int num_tile_columns_minus1;                            // ue(v)
    int num_tile_rows_minus1;                               // ue(v)
    int uniform_tile_spacing_flag;                          // u(1)
    int tile_column_width_minus1[EVC_MAX_TILE_ROWS];        // ue(v)
    int tile_row_height_minus1[EVC_MAX_TILE_COLUMNS];          // ue(v)
    int loop_filter_across_tiles_enabled_flag;              // u(1)
    int tile_offset_len_minus1;                             // ue(v)
    int tile_id_len_minus1;                                 // ue(v)
    int explicit_tile_id_flag;                              // u(1)
    int tile_id_val[EVC_MAX_TILE_ROWS][EVC_MAX_TILE_COLUMNS];  // u(v)
    int pic_dra_enabled_flag;                               // u(1)
    int pic_dra_aps_id;                                     // u(5)
    int arbitrary_slice_present_flag;                       // u(1)
    int constrained_intra_pred_flag;                        // u(1)
    int cu_qp_delta_enabled_flag;                           // u(1)
    int log2_cu_qp_delta_area_minus6;                       // ue(v)

} EVCParserPPS;

// The sturcture reflects Slice Header RBSP(raw byte sequence payload) layout
// @see ISO_IEC_23094-1 section 7.3.2.6
//
// The following descriptors specify the parsing process of each element
// u(n)  - unsigned integer using n bits
// ue(v) - unsigned integer 0-th order Exp_Golomb-coded syntax element with the left bit first
// u(n)  - unsigned integer using n bits.
//         When n is "v" in the syntax table, the number of bits varies in a manner dependent on the value of other syntax elements.
typedef struct EVCParserSliceHeader {
    int slice_pic_parameter_set_id;                                     // ue(v)
    int single_tile_in_slice_flag;                                      // u(1)
    int first_tile_id;                                                  // u(v)
    int arbitrary_slice_flag;                                           // u(1)
    int last_tile_id;                                                   // u(v)
    int num_remaining_tiles_in_slice_minus1;                            // ue(v)
    int delta_tile_id_minus1[EVC_MAX_TILE_ROWS * EVC_MAX_TILE_COLUMNS]; // ue(v)

    int slice_type;                                                     // ue(v)
    int no_output_of_prior_pics_flag;                                   // u(1)
    int mmvd_group_enable_flag;                                         // u(1)
    int slice_alf_enabled_flag;                                         // u(1)

    int slice_alf_luma_aps_id;                                          // u(5)
    int slice_alf_map_flag;                                             // u(1)
    int slice_alf_chroma_idc;                                           // u(2)
    int slice_alf_chroma_aps_id;                                        // u(5)
    int slice_alf_chroma_map_flag;                                      // u(1)
    int slice_alf_chroma2_aps_id;                                       // u(5)
    int slice_alf_chroma2_map_flag;                                     // u(1)
    int slice_pic_order_cnt_lsb;                                        // u(v)

    // @note
    // Currently the structure does not reflect the entire Slice Header RBSP layout.
    // It contains only the fields that are necessary to read from the NAL unit all the values
    // necessary for the correct initialization of the AVCodecContext structure.

    // @note
    // If necessary, add the missing fields to the structure to reflect
    // the contents of the entire NAL unit of the SPS type

} EVCParserSliceHeader;

// picture order count of the current picture
typedef struct EVCParserPoc {
    int PicOrderCntVal;     // current picture order count value
    int prevPicOrderCntVal; // the picture order count of the previous Tid0 picture
    int DocOffset;          // the decoding order count of the previous picture
} EVCParserPoc;

typedef struct EVCParserContext {
    //ParseContext pc;
    EVCParserSPS *sps[EVC_MAX_SPS_COUNT];
    EVCParserPPS *pps[EVC_MAX_PPS_COUNT];

    EVCParserPoc poc;

    int nuh_temporal_id;            // the value of TemporalId (shall be the same for all VCL NAL units of an Access Unit)
    int nalu_type;                  // the current NALU type

    // Dimensions of the decoded video intended for presentation.
    int width;
    int height;

    // Dimensions of the coded video.
    int coded_width;
    int coded_height;

    // The format of the coded data, corresponds to enum AVPixelFormat
    int format;

    // AV_PICTURE_TYPE_I, EVC_SLICE_TYPE_P, AV_PICTURE_TYPE_B
    int pict_type;

    // Set by parser to 1 for key frames and 0 for non-key frames
    int key_frame;

    // Picture number incremented in presentation or output order.
    // This corresponds to EVCEVCParserPoc::PicOrderCntVal
    int output_picture_number;

    // profile
    // 0: FF_PROFILE_EVC_BASELINE
    // 1: FF_PROFILE_EVC_MAIN
    int profile;

    // Framerate value in the compressed bitstream
    AVRational framerate;

    // Number of pictures in a group of pictures
    int gop_size;

    // Number of frames the decoded output will be delayed relative to the encoded input
    int delay;

    int parsed_extradata;

} EVCParserContext;

static inline int evc_get_nalu_type(const uint8_t *bits, int bits_size, void *logctx)
{
    int unit_type_plus1 = 0;

    if (bits_size >= EVC_NALU_HEADER_SIZE) {
        unsigned char *p = (unsigned char *)bits;
        // forbidden_zero_bit
        if ((p[0] & 0x80) != 0) {
            av_log(logctx, AV_LOG_ERROR, "Invalid NAL unit header\n");
            return -1;
        }

        // nal_unit_type
        unit_type_plus1 = (p[0] >> 1) & 0x3F;
    }

    return unit_type_plus1 - 1;
}

static inline uint32_t evc_read_nal_unit_length(const uint8_t *bits, int bits_size, void *logctx)
{
    uint32_t nalu_len = 0;

    if (bits_size < EVC_NALU_LENGTH_PREFIX_SIZE) {
        av_log(logctx, AV_LOG_ERROR, "Can't read NAL unit length\n");
        return 0;
    }

    nalu_len = AV_RB32(bits);

    return nalu_len;
}

// nuh_temporal_id specifies a temporal identifier for the NAL unit
int ff_evc_get_temporal_id(const uint8_t *bits, int bits_size, void *logctx);

// @see ISO_IEC_23094-1 (7.3.2.1 SPS RBSP syntax)
EVCParserSPS *ff_evc_parse_sps(EVCParserContext *ctx, const uint8_t *bs, int bs_size);

// @see ISO_IEC_23094-1 (7.3.2.2 SPS RBSP syntax)
EVCParserPPS *ff_evc_parse_pps(EVCParserContext *ctx, const uint8_t *bs, int bs_size);

int ff_evc_parse_nal_unit(EVCParserContext *ctx, const uint8_t *buf, int buf_size, void *logctx);

void ff_evc_parse_free(EVCParserContext *ctx);

#endif /* AVCODEC_EVC_PARSE_H */
