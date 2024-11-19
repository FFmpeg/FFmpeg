/*
 * Copyright (c) 2014 Tim Walker <tdskywalker@gmail.com>
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

#include "libavcodec/get_bits.h"
#include "libavcodec/golomb.h"
#include "libavcodec/hevc/hevc.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "avc.h"
#include "avio.h"
#include "avio_internal.h"
#include "hevc.h"
#include "nal.h"

#define MAX_SPATIAL_SEGMENTATION 4096 // max. value of u(12) field

enum {
    VPS_INDEX,
    SPS_INDEX,
    PPS_INDEX,
    SEI_PREFIX_INDEX,
    SEI_SUFFIX_INDEX,
    NB_ARRAYS
};


#define FLAG_ARRAY_COMPLETENESS (1 << 0)
#define FLAG_IS_NALFF           (1 << 1)
#define FLAG_IS_LHVC            (1 << 2)

typedef struct HVCCNALUnit {
    uint8_t nuh_layer_id;
    uint8_t parameter_set_id;
    uint16_t nalUnitLength;
    const uint8_t *nalUnit;

    // VPS
    uint8_t vps_max_sub_layers_minus1;
} HVCCNALUnit;

typedef struct HVCCNALUnitArray {
    uint8_t  array_completeness;
    uint8_t  NAL_unit_type;
    uint16_t numNalus;
    HVCCNALUnit *nal;
} HVCCNALUnitArray;

typedef struct HEVCDecoderConfigurationRecord {
    uint8_t  configurationVersion;
    uint8_t  general_profile_space;
    uint8_t  general_tier_flag;
    uint8_t  general_profile_idc;
    uint32_t general_profile_compatibility_flags;
    uint64_t general_constraint_indicator_flags;
    uint8_t  general_level_idc;
    uint16_t min_spatial_segmentation_idc;
    uint8_t  parallelismType;
    uint8_t  chromaFormat;
    uint8_t  bitDepthLumaMinus8;
    uint8_t  bitDepthChromaMinus8;
    uint16_t avgFrameRate;
    uint8_t  constantFrameRate;
    uint8_t  numTemporalLayers;
    uint8_t  temporalIdNested;
    uint8_t  lengthSizeMinusOne;
    uint8_t  numOfArrays;
    HVCCNALUnitArray arrays[NB_ARRAYS];
} HEVCDecoderConfigurationRecord;

typedef struct HVCCProfileTierLevel {
    uint8_t  profile_space;
    uint8_t  tier_flag;
    uint8_t  profile_idc;
    uint32_t profile_compatibility_flags;
    uint64_t constraint_indicator_flags;
    uint8_t  level_idc;
} HVCCProfileTierLevel;

static void hvcc_update_ptl(HEVCDecoderConfigurationRecord *hvcc,
                            HVCCProfileTierLevel *ptl)
{
    /*
     * The value of general_profile_space in all the parameter sets must be
     * identical.
     */
    hvcc->general_profile_space = ptl->profile_space;

    /*
     * The level indication general_level_idc must indicate a level of
     * capability equal to or greater than the highest level indicated for the
     * highest tier in all the parameter sets.
     */
    if (hvcc->general_tier_flag < ptl->tier_flag)
        hvcc->general_level_idc = ptl->level_idc;
    else
        hvcc->general_level_idc = FFMAX(hvcc->general_level_idc, ptl->level_idc);

    /*
     * The tier indication general_tier_flag must indicate a tier equal to or
     * greater than the highest tier indicated in all the parameter sets.
     */
    hvcc->general_tier_flag = FFMAX(hvcc->general_tier_flag, ptl->tier_flag);

    /*
     * The profile indication general_profile_idc must indicate a profile to
     * which the stream associated with this configuration record conforms.
     *
     * If the sequence parameter sets are marked with different profiles, then
     * the stream may need examination to determine which profile, if any, the
     * entire stream conforms to. If the entire stream is not examined, or the
     * examination reveals that there is no profile to which the entire stream
     * conforms, then the entire stream must be split into two or more
     * sub-streams with separate configuration records in which these rules can
     * be met.
     *
     * Note: set the profile to the highest value for the sake of simplicity.
     */
    hvcc->general_profile_idc = FFMAX(hvcc->general_profile_idc, ptl->profile_idc);

    /*
     * Each bit in general_profile_compatibility_flags may only be set if all
     * the parameter sets set that bit.
     */
    hvcc->general_profile_compatibility_flags &= ptl->profile_compatibility_flags;

    /*
     * Each bit in general_constraint_indicator_flags may only be set if all
     * the parameter sets set that bit.
     */
    hvcc->general_constraint_indicator_flags &= ptl->constraint_indicator_flags;
}

static void hvcc_parse_ptl(GetBitContext *gb,
                           HEVCDecoderConfigurationRecord *hvcc,
                           unsigned int max_sub_layers_minus1)
{
    unsigned int i;
    HVCCProfileTierLevel general_ptl;
    uint8_t sub_layer_profile_present_flag[HEVC_MAX_SUB_LAYERS];
    uint8_t sub_layer_level_present_flag[HEVC_MAX_SUB_LAYERS];

    general_ptl.profile_space               = get_bits(gb, 2);
    general_ptl.tier_flag                   = get_bits1(gb);
    general_ptl.profile_idc                 = get_bits(gb, 5);
    general_ptl.profile_compatibility_flags = get_bits_long(gb, 32);
    general_ptl.constraint_indicator_flags  = get_bits64(gb, 48);
    general_ptl.level_idc                   = get_bits(gb, 8);
    hvcc_update_ptl(hvcc, &general_ptl);

    for (i = 0; i < max_sub_layers_minus1; i++) {
        sub_layer_profile_present_flag[i] = get_bits1(gb);
        sub_layer_level_present_flag[i]   = get_bits1(gb);
    }

    if (max_sub_layers_minus1 > 0)
        for (i = max_sub_layers_minus1; i < 8; i++)
            skip_bits(gb, 2); // reserved_zero_2bits[i]

    for (i = 0; i < max_sub_layers_minus1; i++) {
        if (sub_layer_profile_present_flag[i]) {
            /*
             * sub_layer_profile_space[i]                     u(2)
             * sub_layer_tier_flag[i]                         u(1)
             * sub_layer_profile_idc[i]                       u(5)
             * sub_layer_profile_compatibility_flag[i][0..31] u(32)
             * sub_layer_progressive_source_flag[i]           u(1)
             * sub_layer_interlaced_source_flag[i]            u(1)
             * sub_layer_non_packed_constraint_flag[i]        u(1)
             * sub_layer_frame_only_constraint_flag[i]        u(1)
             * sub_layer_reserved_zero_44bits[i]              u(44)
             */
            skip_bits_long(gb, 32);
            skip_bits_long(gb, 32);
            skip_bits     (gb, 24);
        }

        if (sub_layer_level_present_flag[i])
            skip_bits(gb, 8);
    }
}

static void skip_sub_layer_hrd_parameters(GetBitContext *gb,
                                          unsigned int cpb_cnt_minus1,
                                          uint8_t sub_pic_hrd_params_present_flag)
{
    unsigned int i;

    for (i = 0; i <= cpb_cnt_minus1; i++) {
        get_ue_golomb_long(gb); // bit_rate_value_minus1
        get_ue_golomb_long(gb); // cpb_size_value_minus1

        if (sub_pic_hrd_params_present_flag) {
            get_ue_golomb_long(gb); // cpb_size_du_value_minus1
            get_ue_golomb_long(gb); // bit_rate_du_value_minus1
        }

        skip_bits1(gb); // cbr_flag
    }
}

static int skip_hrd_parameters(GetBitContext *gb, uint8_t cprms_present_flag,
                                unsigned int max_sub_layers_minus1)
{
    unsigned int i;
    uint8_t sub_pic_hrd_params_present_flag = 0;
    uint8_t nal_hrd_parameters_present_flag = 0;
    uint8_t vcl_hrd_parameters_present_flag = 0;

    if (cprms_present_flag) {
        nal_hrd_parameters_present_flag = get_bits1(gb);
        vcl_hrd_parameters_present_flag = get_bits1(gb);

        if (nal_hrd_parameters_present_flag ||
            vcl_hrd_parameters_present_flag) {
            sub_pic_hrd_params_present_flag = get_bits1(gb);

            if (sub_pic_hrd_params_present_flag)
                /*
                 * tick_divisor_minus2                          u(8)
                 * du_cpb_removal_delay_increment_length_minus1 u(5)
                 * sub_pic_cpb_params_in_pic_timing_sei_flag    u(1)
                 * dpb_output_delay_du_length_minus1            u(5)
                 */
                skip_bits(gb, 19);

            /*
             * bit_rate_scale u(4)
             * cpb_size_scale u(4)
             */
            skip_bits(gb, 8);

            if (sub_pic_hrd_params_present_flag)
                skip_bits(gb, 4); // cpb_size_du_scale

            /*
             * initial_cpb_removal_delay_length_minus1 u(5)
             * au_cpb_removal_delay_length_minus1      u(5)
             * dpb_output_delay_length_minus1          u(5)
             */
            skip_bits(gb, 15);
        }
    }

    for (i = 0; i <= max_sub_layers_minus1; i++) {
        unsigned int cpb_cnt_minus1            = 0;
        uint8_t low_delay_hrd_flag             = 0;
        uint8_t fixed_pic_rate_within_cvs_flag = 0;
        uint8_t fixed_pic_rate_general_flag    = get_bits1(gb);

        if (!fixed_pic_rate_general_flag)
            fixed_pic_rate_within_cvs_flag = get_bits1(gb);

        if (fixed_pic_rate_within_cvs_flag)
            get_ue_golomb_long(gb); // elemental_duration_in_tc_minus1
        else
            low_delay_hrd_flag = get_bits1(gb);

        if (!low_delay_hrd_flag) {
            cpb_cnt_minus1 = get_ue_golomb_long(gb);
            if (cpb_cnt_minus1 > 31)
                return AVERROR_INVALIDDATA;
        }

        if (nal_hrd_parameters_present_flag)
            skip_sub_layer_hrd_parameters(gb, cpb_cnt_minus1,
                                          sub_pic_hrd_params_present_flag);

        if (vcl_hrd_parameters_present_flag)
            skip_sub_layer_hrd_parameters(gb, cpb_cnt_minus1,
                                          sub_pic_hrd_params_present_flag);
    }

    return 0;
}

static void skip_timing_info(GetBitContext *gb)
{
    skip_bits_long(gb, 32); // num_units_in_tick
    skip_bits_long(gb, 32); // time_scale

    if (get_bits1(gb))          // poc_proportional_to_timing_flag
        get_ue_golomb_long(gb); // num_ticks_poc_diff_one_minus1
}

static void hvcc_parse_vui(GetBitContext *gb,
                           HEVCDecoderConfigurationRecord *hvcc,
                           unsigned int max_sub_layers_minus1)
{
    unsigned int min_spatial_segmentation_idc;

    if (get_bits1(gb))              // aspect_ratio_info_present_flag
        if (get_bits(gb, 8) == 255) // aspect_ratio_idc
            skip_bits_long(gb, 32); // sar_width u(16), sar_height u(16)

    if (get_bits1(gb))  // overscan_info_present_flag
        skip_bits1(gb); // overscan_appropriate_flag

    if (get_bits1(gb)) {  // video_signal_type_present_flag
        skip_bits(gb, 4); // video_format u(3), video_full_range_flag u(1)

        if (get_bits1(gb)) // colour_description_present_flag
            /*
             * colour_primaries         u(8)
             * transfer_characteristics u(8)
             * matrix_coeffs            u(8)
             */
            skip_bits(gb, 24);
    }

    if (get_bits1(gb)) {        // chroma_loc_info_present_flag
        get_ue_golomb_long(gb); // chroma_sample_loc_type_top_field
        get_ue_golomb_long(gb); // chroma_sample_loc_type_bottom_field
    }

    /*
     * neutral_chroma_indication_flag u(1)
     * field_seq_flag                 u(1)
     * frame_field_info_present_flag  u(1)
     */
    skip_bits(gb, 3);

    if (get_bits1(gb)) {        // default_display_window_flag
        get_ue_golomb_long(gb); // def_disp_win_left_offset
        get_ue_golomb_long(gb); // def_disp_win_right_offset
        get_ue_golomb_long(gb); // def_disp_win_top_offset
        get_ue_golomb_long(gb); // def_disp_win_bottom_offset
    }

    if (get_bits1(gb)) { // vui_timing_info_present_flag
        skip_timing_info(gb);

        if (get_bits1(gb)) // vui_hrd_parameters_present_flag
            skip_hrd_parameters(gb, 1, max_sub_layers_minus1);
    }

    if (get_bits1(gb)) { // bitstream_restriction_flag
        /*
         * tiles_fixed_structure_flag              u(1)
         * motion_vectors_over_pic_boundaries_flag u(1)
         * restricted_ref_pic_lists_flag           u(1)
         */
        skip_bits(gb, 3);

        min_spatial_segmentation_idc = get_ue_golomb_long(gb);

        /*
         * unsigned int(12) min_spatial_segmentation_idc;
         *
         * The min_spatial_segmentation_idc indication must indicate a level of
         * spatial segmentation equal to or less than the lowest level of
         * spatial segmentation indicated in all the parameter sets.
         */
        hvcc->min_spatial_segmentation_idc = FFMIN(hvcc->min_spatial_segmentation_idc,
                                                   min_spatial_segmentation_idc);

        get_ue_golomb_long(gb); // max_bytes_per_pic_denom
        get_ue_golomb_long(gb); // max_bits_per_min_cu_denom
        get_ue_golomb_long(gb); // log2_max_mv_length_horizontal
        get_ue_golomb_long(gb); // log2_max_mv_length_vertical
    }
}

static void skip_sub_layer_ordering_info(GetBitContext *gb)
{
    get_ue_golomb_long(gb); // max_dec_pic_buffering_minus1
    get_ue_golomb_long(gb); // max_num_reorder_pics
    get_ue_golomb_long(gb); // max_latency_increase_plus1
}

static int hvcc_parse_vps(GetBitContext *gb, HVCCNALUnit *nal,
                          HEVCDecoderConfigurationRecord *hvcc)
{
    nal->parameter_set_id = get_bits(gb, 4);
    /*
     * vps_reserved_three_2bits   u(2)
     * vps_max_layers_minus1      u(6)
     */
    skip_bits(gb, 8);

    nal->vps_max_sub_layers_minus1 = get_bits(gb, 3);

    /*
     * numTemporalLayers greater than 1 indicates that the stream to which this
     * configuration record applies is temporally scalable and the contained
     * number of temporal layers (also referred to as temporal sub-layer or
     * sub-layer in ISO/IEC 23008-2) is equal to numTemporalLayers. Value 1
     * indicates that the stream is not temporally scalable. Value 0 indicates
     * that it is unknown whether the stream is temporally scalable.
     */
    hvcc->numTemporalLayers = FFMAX(hvcc->numTemporalLayers,
                                    nal->vps_max_sub_layers_minus1 + 1);

    /*
     * vps_temporal_id_nesting_flag u(1)
     * vps_reserved_0xffff_16bits   u(16)
     */
    skip_bits(gb, 17);

    hvcc_parse_ptl(gb, hvcc, nal->vps_max_sub_layers_minus1);

    /* nothing useful for hvcC past this point */
    return 0;
}

static void skip_scaling_list_data(GetBitContext *gb)
{
    int i, j, k, num_coeffs;

    for (i = 0; i < 4; i++)
        for (j = 0; j < (i == 3 ? 2 : 6); j++)
            if (!get_bits1(gb))         // scaling_list_pred_mode_flag[i][j]
                get_ue_golomb_long(gb); // scaling_list_pred_matrix_id_delta[i][j]
            else {
                num_coeffs = FFMIN(64, 1 << (4 + (i << 1)));

                if (i > 1)
                    get_se_golomb_long(gb); // scaling_list_dc_coef_minus8[i-2][j]

                for (k = 0; k < num_coeffs; k++)
                    get_se_golomb_long(gb); // scaling_list_delta_coef
            }
}

static int parse_rps(GetBitContext *gb, unsigned int rps_idx,
                     unsigned int num_rps,
                     unsigned int num_delta_pocs[HEVC_MAX_SHORT_TERM_REF_PIC_SETS])
{
    unsigned int i;

    if (rps_idx && get_bits1(gb)) { // inter_ref_pic_set_prediction_flag
        /* this should only happen for slice headers, and this isn't one */
        if (rps_idx >= num_rps)
            return AVERROR_INVALIDDATA;

        skip_bits1        (gb); // delta_rps_sign
        get_ue_golomb_long(gb); // abs_delta_rps_minus1

        num_delta_pocs[rps_idx] = 0;

        /*
         * From libavcodec/hevc_ps.c:
         *
         * if (is_slice_header) {
         *    //foo
         * } else
         *     rps_ridx = &sps->st_rps[rps - sps->st_rps - 1];
         *
         * where:
         * rps:             &sps->st_rps[rps_idx]
         * sps->st_rps:     &sps->st_rps[0]
         * is_slice_header: rps_idx == num_rps
         *
         * thus:
         * if (num_rps != rps_idx)
         *     rps_ridx = &sps->st_rps[rps_idx - 1];
         *
         * NumDeltaPocs[RefRpsIdx]: num_delta_pocs[rps_idx - 1]
         */
        for (i = 0; i <= num_delta_pocs[rps_idx - 1]; i++) {
            uint8_t use_delta_flag = 0;
            uint8_t used_by_curr_pic_flag = get_bits1(gb);
            if (!used_by_curr_pic_flag)
                use_delta_flag = get_bits1(gb);

            if (used_by_curr_pic_flag || use_delta_flag)
                num_delta_pocs[rps_idx]++;
        }
    } else {
        unsigned int num_negative_pics = get_ue_golomb_long(gb);
        unsigned int num_positive_pics = get_ue_golomb_long(gb);

        if ((num_positive_pics + (uint64_t)num_negative_pics) * 2 > get_bits_left(gb))
            return AVERROR_INVALIDDATA;

        num_delta_pocs[rps_idx] = num_negative_pics + num_positive_pics;

        for (i = 0; i < num_negative_pics; i++) {
            get_ue_golomb_long(gb); // delta_poc_s0_minus1[rps_idx]
            skip_bits1        (gb); // used_by_curr_pic_s0_flag[rps_idx]
        }

        for (i = 0; i < num_positive_pics; i++) {
            get_ue_golomb_long(gb); // delta_poc_s1_minus1[rps_idx]
            skip_bits1        (gb); // used_by_curr_pic_s1_flag[rps_idx]
        }
    }

    return 0;
}

static int hvcc_parse_sps(GetBitContext *gb, HVCCNALUnit *nal,
                          HEVCDecoderConfigurationRecord *hvcc)
{
    unsigned int i, sps_max_sub_layers_minus1, log2_max_pic_order_cnt_lsb_minus4;
    unsigned int num_short_term_ref_pic_sets, num_delta_pocs[HEVC_MAX_SHORT_TERM_REF_PIC_SETS];
    unsigned int sps_ext_or_max_sub_layers_minus1, multi_layer_ext_sps_flag;

    unsigned int sps_video_parameter_set_id = get_bits(gb, 4);

    if (nal->nuh_layer_id == 0) {
        sps_ext_or_max_sub_layers_minus1 = 0;
        sps_max_sub_layers_minus1 = get_bits(gb, 3);
    } else {
        sps_ext_or_max_sub_layers_minus1 = get_bits(gb, 3);
        if (sps_ext_or_max_sub_layers_minus1 == 7) {
            const HVCCNALUnitArray *array = &hvcc->arrays[VPS_INDEX];
            const HVCCNALUnit *vps = NULL;

            for (i = 0; i < array->numNalus; i++)
                if (sps_video_parameter_set_id == array->nal[i].parameter_set_id) {
                    vps = &array->nal[i];
                    break;
                }
            if (!vps)
                return AVERROR_INVALIDDATA;

            sps_max_sub_layers_minus1 = vps->vps_max_sub_layers_minus1;
        } else
            sps_max_sub_layers_minus1 = sps_ext_or_max_sub_layers_minus1;
    }
    multi_layer_ext_sps_flag = nal->nuh_layer_id &&
                               sps_ext_or_max_sub_layers_minus1 == 7;

    /*
     * numTemporalLayers greater than 1 indicates that the stream to which this
     * configuration record applies is temporally scalable and the contained
     * number of temporal layers (also referred to as temporal sub-layer or
     * sub-layer in ISO/IEC 23008-2) is equal to numTemporalLayers. Value 1
     * indicates that the stream is not temporally scalable. Value 0 indicates
     * that it is unknown whether the stream is temporally scalable.
     */
    hvcc->numTemporalLayers = FFMAX(hvcc->numTemporalLayers,
                                    sps_max_sub_layers_minus1 + 1);

    if (!multi_layer_ext_sps_flag) {
        hvcc->temporalIdNested = get_bits1(gb);
        hvcc_parse_ptl(gb, hvcc, sps_max_sub_layers_minus1);
    }

    nal->parameter_set_id = get_ue_golomb_long(gb);

    if (multi_layer_ext_sps_flag) {
        if (get_bits1(gb)) // update_rep_format_flag
            skip_bits(gb, 8); // sps_rep_format_idx
    } else {
        hvcc->chromaFormat = get_ue_golomb_long(gb);

        if (hvcc->chromaFormat == 3)
            skip_bits1(gb); // separate_colour_plane_flag

        get_ue_golomb_long(gb); // pic_width_in_luma_samples
        get_ue_golomb_long(gb); // pic_height_in_luma_samples

        if (get_bits1(gb)) {        // conformance_window_flag
            get_ue_golomb_long(gb); // conf_win_left_offset
            get_ue_golomb_long(gb); // conf_win_right_offset
            get_ue_golomb_long(gb); // conf_win_top_offset
            get_ue_golomb_long(gb); // conf_win_bottom_offset
        }

        hvcc->bitDepthLumaMinus8          = get_ue_golomb_long(gb);
        hvcc->bitDepthChromaMinus8        = get_ue_golomb_long(gb);
    }
    log2_max_pic_order_cnt_lsb_minus4 = get_ue_golomb_long(gb);

    if (!multi_layer_ext_sps_flag) {
        /* sps_sub_layer_ordering_info_present_flag */
        i = get_bits1(gb) ? 0 : sps_max_sub_layers_minus1;
        for (; i <= sps_max_sub_layers_minus1; i++)
            skip_sub_layer_ordering_info(gb);
    }

    get_ue_golomb_long(gb); // log2_min_luma_coding_block_size_minus3
    get_ue_golomb_long(gb); // log2_diff_max_min_luma_coding_block_size
    get_ue_golomb_long(gb); // log2_min_transform_block_size_minus2
    get_ue_golomb_long(gb); // log2_diff_max_min_transform_block_size
    get_ue_golomb_long(gb); // max_transform_hierarchy_depth_inter
    get_ue_golomb_long(gb); // max_transform_hierarchy_depth_intra

    if (get_bits1(gb)) { // scaling_list_enabled_flag
        int sps_infer_scaling_list_flag = 0;
        if (multi_layer_ext_sps_flag)
            sps_infer_scaling_list_flag = get_bits1(gb);
        if (sps_infer_scaling_list_flag)
            skip_bits(gb, 6);   // sps_scaling_list_ref_layer_id
        else if (get_bits1(gb)) // sps_scaling_list_data_present_flag
            skip_scaling_list_data(gb);
    }

    skip_bits1(gb); // amp_enabled_flag
    skip_bits1(gb); // sample_adaptive_offset_enabled_flag

    if (get_bits1(gb)) {           // pcm_enabled_flag
        skip_bits         (gb, 4); // pcm_sample_bit_depth_luma_minus1
        skip_bits         (gb, 4); // pcm_sample_bit_depth_chroma_minus1
        get_ue_golomb_long(gb);    // log2_min_pcm_luma_coding_block_size_minus3
        get_ue_golomb_long(gb);    // log2_diff_max_min_pcm_luma_coding_block_size
        skip_bits1        (gb);    // pcm_loop_filter_disabled_flag
    }

    num_short_term_ref_pic_sets = get_ue_golomb_long(gb);
    if (num_short_term_ref_pic_sets > HEVC_MAX_SHORT_TERM_REF_PIC_SETS)
        return AVERROR_INVALIDDATA;

    for (i = 0; i < num_short_term_ref_pic_sets; i++) {
        int ret = parse_rps(gb, i, num_short_term_ref_pic_sets, num_delta_pocs);
        if (ret < 0)
            return ret;
    }

    if (get_bits1(gb)) {                               // long_term_ref_pics_present_flag
        unsigned num_long_term_ref_pics_sps = get_ue_golomb_long(gb);
        if (num_long_term_ref_pics_sps > 31U)
            return AVERROR_INVALIDDATA;
        for (i = 0; i < num_long_term_ref_pics_sps; i++) { // num_long_term_ref_pics_sps
            int len = FFMIN(log2_max_pic_order_cnt_lsb_minus4 + 4, 16);
            skip_bits (gb, len); // lt_ref_pic_poc_lsb_sps[i]
            skip_bits1(gb);      // used_by_curr_pic_lt_sps_flag[i]
        }
    }

    skip_bits1(gb); // sps_temporal_mvp_enabled_flag
    skip_bits1(gb); // strong_intra_smoothing_enabled_flag

    if (get_bits1(gb)) // vui_parameters_present_flag
        hvcc_parse_vui(gb, hvcc, sps_max_sub_layers_minus1);

    /* nothing useful for hvcC past this point */
    return 0;
}

static int hvcc_parse_pps(GetBitContext *gb, HVCCNALUnit *nal,
                          HEVCDecoderConfigurationRecord *hvcc)
{
    uint8_t tiles_enabled_flag, entropy_coding_sync_enabled_flag;

    nal->parameter_set_id = get_ue_golomb_long(gb); // pps_pic_parameter_set_id
    get_ue_golomb_long(gb); // pps_seq_parameter_set_id

    /*
     * dependent_slice_segments_enabled_flag u(1)
     * output_flag_present_flag              u(1)
     * num_extra_slice_header_bits           u(3)
     * sign_data_hiding_enabled_flag         u(1)
     * cabac_init_present_flag               u(1)
     */
    skip_bits(gb, 7);

    get_ue_golomb_long(gb); // num_ref_idx_l0_default_active_minus1
    get_ue_golomb_long(gb); // num_ref_idx_l1_default_active_minus1
    get_se_golomb_long(gb); // init_qp_minus26

    /*
     * constrained_intra_pred_flag u(1)
     * transform_skip_enabled_flag u(1)
     */
    skip_bits(gb, 2);

    if (get_bits1(gb))          // cu_qp_delta_enabled_flag
        get_ue_golomb_long(gb); // diff_cu_qp_delta_depth

    get_se_golomb_long(gb); // pps_cb_qp_offset
    get_se_golomb_long(gb); // pps_cr_qp_offset

    /*
     * pps_slice_chroma_qp_offsets_present_flag u(1)
     * weighted_pred_flag               u(1)
     * weighted_bipred_flag             u(1)
     * transquant_bypass_enabled_flag   u(1)
     */
    skip_bits(gb, 4);

    tiles_enabled_flag               = get_bits1(gb);
    entropy_coding_sync_enabled_flag = get_bits1(gb);

    if (entropy_coding_sync_enabled_flag && tiles_enabled_flag)
        hvcc->parallelismType = 0; // mixed-type parallel decoding
    else if (entropy_coding_sync_enabled_flag)
        hvcc->parallelismType = 3; // wavefront-based parallel decoding
    else if (tiles_enabled_flag)
        hvcc->parallelismType = 2; // tile-based parallel decoding
    else
        hvcc->parallelismType = 1; // slice-based parallel decoding

    /* nothing useful for hvcC past this point */
    return 0;
}

static void nal_unit_parse_header(GetBitContext *gb, uint8_t *nal_type,
                                  uint8_t *nuh_layer_id)
{
    skip_bits1(gb); // forbidden_zero_bit

    *nal_type = get_bits(gb, 6);
    *nuh_layer_id = get_bits(gb, 6);

    /*
     * nuh_temporal_id_plus1 u(3)
     */
    skip_bits(gb, 3);
}

static int hvcc_array_add_nal_unit(const uint8_t *nal_buf, uint32_t nal_size,
                                   HVCCNALUnitArray *array)
{
    HVCCNALUnit *nal;
    int ret;
    uint16_t numNalus = array->numNalus;

    ret = av_reallocp_array(&array->nal, numNalus + 1, sizeof(*array->nal));
    if (ret < 0)
        return ret;

    nal = &array->nal[numNalus];
    nal->nalUnit       = nal_buf;
    nal->nalUnitLength = nal_size;
    array->numNalus++;

    return 0;
}

static int hvcc_add_nal_unit(const uint8_t *nal_buf, uint32_t nal_size,
                             HEVCDecoderConfigurationRecord *hvcc,
                             int flags, unsigned array_idx)
{
    int ret = 0;
    int is_nalff = !!(flags & FLAG_IS_NALFF);
    int is_lhvc = !!(flags & FLAG_IS_LHVC);
    int ps_array_completeness = !!(flags & FLAG_ARRAY_COMPLETENESS);
    HVCCNALUnitArray *const array = &hvcc->arrays[array_idx];
    HVCCNALUnit *nal;
    GetBitContext gbc;
    uint8_t nal_type, nuh_layer_id;
    uint8_t *rbsp_buf;
    uint32_t rbsp_size;

    rbsp_buf = ff_nal_unit_extract_rbsp(nal_buf, nal_size, &rbsp_size, 2);
    if (!rbsp_buf) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    ret = init_get_bits8(&gbc, rbsp_buf, rbsp_size);
    if (ret < 0)
        goto end;

    nal_unit_parse_header(&gbc, &nal_type, &nuh_layer_id);
    if (!is_lhvc && nuh_layer_id > 0)
        goto end;

    /*
     * Note: only 'declarative' SEI messages are allowed in
     * hvcC. Perhaps the SEI playload type should be checked
     * and non-declarative SEI messages discarded?
     */
    ret = hvcc_array_add_nal_unit(nal_buf, nal_size, array);
    if (ret < 0)
        goto end;
    if (array->numNalus == 1) {
        hvcc->numOfArrays++;
        array->NAL_unit_type = nal_type;

        /*
         * When the sample entry name is ‘hvc1’, the default and mandatory value of
         * array_completeness is 1 for arrays of all types of parameter sets, and 0
         * for all other arrays. When the sample entry name is ‘hev1’, the default
         * value of array_completeness is 0 for all arrays.
         */
        if (nal_type == HEVC_NAL_VPS || nal_type == HEVC_NAL_SPS ||
            nal_type == HEVC_NAL_PPS)
            array->array_completeness = ps_array_completeness;
    }

    nal = &array->nal[array->numNalus-1];
    nal->nuh_layer_id = nuh_layer_id;

    /* Don't parse parameter sets. We already have the needed information*/
    if (is_nalff)
        goto end;

    if (nal_type == HEVC_NAL_VPS)
        ret = hvcc_parse_vps(&gbc, nal, hvcc);
    else if (nal_type == HEVC_NAL_SPS)
        ret = hvcc_parse_sps(&gbc, nal, hvcc);
    else if (nal_type == HEVC_NAL_PPS)
        ret = hvcc_parse_pps(&gbc, nal, hvcc);
    if (ret < 0)
        goto end;

end:
    av_free(rbsp_buf);
    return ret;
}

static void hvcc_init(HEVCDecoderConfigurationRecord *hvcc)
{
    memset(hvcc, 0, sizeof(HEVCDecoderConfigurationRecord));
    hvcc->configurationVersion = 1;
    hvcc->lengthSizeMinusOne   = 3; // 4 bytes

    /*
     * The following fields have all their valid bits set by default,
     * the ProfileTierLevel parsing code will unset them when needed.
     */
    hvcc->general_profile_compatibility_flags = 0xffffffff;
    hvcc->general_constraint_indicator_flags  = 0xffffffffffff;

    /*
     * Initialize this field with an invalid value which can be used to detect
     * whether we didn't see any VUI (in which case it should be reset to zero).
     */
    hvcc->min_spatial_segmentation_idc = MAX_SPATIAL_SEGMENTATION + 1;
}

static void hvcc_close(HEVCDecoderConfigurationRecord *hvcc)
{
    for (unsigned i = 0; i < FF_ARRAY_ELEMS(hvcc->arrays); i++) {
        HVCCNALUnitArray *const array = &hvcc->arrays[i];
        array->numNalus = 0;
        av_freep(&array->nal);
    }
}

static int hvcc_write(AVIOContext *pb, HEVCDecoderConfigurationRecord *hvcc,
                      int flags)
{
    uint16_t numNalus[NB_ARRAYS] = { 0 };
    int is_lhvc = !!(flags & FLAG_IS_LHVC);
    int numOfArrays = 0;

    /*
     * We only support writing HEVCDecoderConfigurationRecord version 1.
     */
    hvcc->configurationVersion = 1;

    /*
     * If min_spatial_segmentation_idc is invalid, reset to 0 (unspecified).
     */
    if (hvcc->min_spatial_segmentation_idc > MAX_SPATIAL_SEGMENTATION)
        hvcc->min_spatial_segmentation_idc = 0;

    /*
     * parallelismType indicates the type of parallelism that is used to meet
     * the restrictions imposed by min_spatial_segmentation_idc when the value
     * of min_spatial_segmentation_idc is greater than 0.
     */
    if (!hvcc->min_spatial_segmentation_idc)
        hvcc->parallelismType = 0;

    /*
     * It's unclear how to properly compute these fields, so
     * let's always set them to values meaning 'unspecified'.
     */
    hvcc->avgFrameRate      = 0;
    /*
     * lhvC doesn't store this field. It instead reserves the bits, setting them
     * to '11'b.
     */
    hvcc->constantFrameRate = is_lhvc * 0x3;

    /*
     * Skip all NALUs with nuh_layer_id == 0 if writing lhvC. We do it here and
     * not before parsing them as some parameter sets with nuh_layer_id > 0
     * may reference base layer parameters sets.
     */
    for (unsigned i = 0; i < FF_ARRAY_ELEMS(hvcc->arrays); i++) {
        const HVCCNALUnitArray *const array = &hvcc->arrays[i];

        if (array->numNalus == 0)
            continue;

        for (unsigned j = 0; j < array->numNalus; j++)
            numNalus[i] += !is_lhvc || (array->nal[j].nuh_layer_id != 0);
        numOfArrays += (numNalus[i] > 0);
    }

    av_log(NULL, AV_LOG_TRACE,  "%s\n", is_lhvc ? "lhvC" : "hvcC");
    av_log(NULL, AV_LOG_TRACE,  "configurationVersion:                %"PRIu8"\n",
            hvcc->configurationVersion);
    if (!is_lhvc) {
        av_log(NULL, AV_LOG_TRACE,  "general_profile_space:               %"PRIu8"\n",
                hvcc->general_profile_space);
        av_log(NULL, AV_LOG_TRACE,  "general_tier_flag:                   %"PRIu8"\n",
                hvcc->general_tier_flag);
        av_log(NULL, AV_LOG_TRACE,  "general_profile_idc:                 %"PRIu8"\n",
                hvcc->general_profile_idc);
        av_log(NULL, AV_LOG_TRACE, "general_profile_compatibility_flags: 0x%08"PRIx32"\n",
                hvcc->general_profile_compatibility_flags);
        av_log(NULL, AV_LOG_TRACE, "general_constraint_indicator_flags:  0x%012"PRIx64"\n",
                hvcc->general_constraint_indicator_flags);
        av_log(NULL, AV_LOG_TRACE,  "general_level_idc:                   %"PRIu8"\n",
                hvcc->general_level_idc);
    }
    av_log(NULL, AV_LOG_TRACE,  "min_spatial_segmentation_idc:        %"PRIu16"\n",
            hvcc->min_spatial_segmentation_idc);
    av_log(NULL, AV_LOG_TRACE,  "parallelismType:                     %"PRIu8"\n",
            hvcc->parallelismType);
    if (!is_lhvc) {
        av_log(NULL, AV_LOG_TRACE,  "chromaFormat:                        %"PRIu8"\n",
                hvcc->chromaFormat);
        av_log(NULL, AV_LOG_TRACE,  "bitDepthLumaMinus8:                  %"PRIu8"\n",
                hvcc->bitDepthLumaMinus8);
        av_log(NULL, AV_LOG_TRACE,  "bitDepthChromaMinus8:                %"PRIu8"\n",
                hvcc->bitDepthChromaMinus8);
        av_log(NULL, AV_LOG_TRACE,  "avgFrameRate:                        %"PRIu16"\n",
                hvcc->avgFrameRate);
        av_log(NULL, AV_LOG_TRACE,  "constantFrameRate:                   %"PRIu8"\n",
                hvcc->constantFrameRate);
    }
    av_log(NULL, AV_LOG_TRACE,  "numTemporalLayers:                   %"PRIu8"\n",
            hvcc->numTemporalLayers);
    av_log(NULL, AV_LOG_TRACE,  "temporalIdNested:                    %"PRIu8"\n",
            hvcc->temporalIdNested);
    av_log(NULL, AV_LOG_TRACE,  "lengthSizeMinusOne:                  %"PRIu8"\n",
            hvcc->lengthSizeMinusOne);
    av_log(NULL, AV_LOG_TRACE,  "numOfArrays:                         %"PRIu8"\n",
            numOfArrays);
    for (unsigned i = 0, j = 0; i < FF_ARRAY_ELEMS(hvcc->arrays); i++) {
        const HVCCNALUnitArray *const array = &hvcc->arrays[i];

        if (numNalus[i] == 0)
            continue;

        av_log(NULL, AV_LOG_TRACE, "array_completeness[%u]:               %"PRIu8"\n",
               j, array->array_completeness);
        av_log(NULL, AV_LOG_TRACE, "NAL_unit_type[%u]:                    %"PRIu8"\n",
               j, array->NAL_unit_type);
        av_log(NULL, AV_LOG_TRACE, "numNalus[%u]:                         %"PRIu16"\n",
               j, numNalus[i]);
        for (unsigned k = 0; k < array->numNalus; k++) {
            if (is_lhvc && array->nal[k].nuh_layer_id == 0)
                continue;

            av_log(NULL, AV_LOG_TRACE,
                    "nalUnitLength[%u][%u]:                 %"PRIu16"\n",
                   j, k, array->nal[k].nalUnitLength);
        }
        j++;
    }

    /*
     * We need at least one of each: VPS, SPS and PPS.
     */
    if ((flags & FLAG_ARRAY_COMPLETENESS) &&
        (!numNalus[VPS_INDEX] || numNalus[VPS_INDEX] > HEVC_MAX_VPS_COUNT) && !is_lhvc)
        return AVERROR_INVALIDDATA;
    if ((flags & FLAG_ARRAY_COMPLETENESS) &&
        (!numNalus[SPS_INDEX] || numNalus[SPS_INDEX] > HEVC_MAX_SPS_COUNT ||
         !numNalus[PPS_INDEX] || numNalus[PPS_INDEX] > HEVC_MAX_PPS_COUNT))
        return AVERROR_INVALIDDATA;

    /* unsigned int(8) configurationVersion = 1; */
    avio_w8(pb, hvcc->configurationVersion);

    if (!is_lhvc) {
        /*
         * unsigned int(2) general_profile_space;
         * unsigned int(1) general_tier_flag;
         * unsigned int(5) general_profile_idc;
         */
        avio_w8(pb, hvcc->general_profile_space << 6 |
                    hvcc->general_tier_flag     << 5 |
                    hvcc->general_profile_idc);

        /* unsigned int(32) general_profile_compatibility_flags; */
        avio_wb32(pb, hvcc->general_profile_compatibility_flags);

        /* unsigned int(48) general_constraint_indicator_flags; */
        avio_wb32(pb, hvcc->general_constraint_indicator_flags >> 16);
        avio_wb16(pb, hvcc->general_constraint_indicator_flags);

        /* unsigned int(8) general_level_idc; */
        avio_w8(pb, hvcc->general_level_idc);
    }

    /*
     * bit(4) reserved = '1111'b;
     * unsigned int(12) min_spatial_segmentation_idc;
     */
    avio_wb16(pb, hvcc->min_spatial_segmentation_idc | 0xf000);

    /*
     * bit(6) reserved = '111111'b;
     * unsigned int(2) parallelismType;
     */
    avio_w8(pb, hvcc->parallelismType | 0xfc);

    if (!is_lhvc) {
        /*
         * bit(6) reserved = '111111'b;
         * unsigned int(2) chromaFormat;
         */
        avio_w8(pb, hvcc->chromaFormat | 0xfc);

        /*
         * bit(5) reserved = '11111'b;
         * unsigned int(3) bitDepthLumaMinus8;
         */
        avio_w8(pb, hvcc->bitDepthLumaMinus8 | 0xf8);

        /*
         * bit(5) reserved = '11111'b;
         * unsigned int(3) bitDepthChromaMinus8;
         */
        avio_w8(pb, hvcc->bitDepthChromaMinus8 | 0xf8);

        /* bit(16) avgFrameRate; */
        avio_wb16(pb, hvcc->avgFrameRate);
    }

    /*
     * if (!is_lhvc)
     *     bit(2) constantFrameRate;
     * else
     *     bit(2) reserved = '11'b;
     * bit(3) numTemporalLayers;
     * bit(1) temporalIdNested;
     * unsigned int(2) lengthSizeMinusOne;
     */
    avio_w8(pb, hvcc->constantFrameRate << 6 |
                hvcc->numTemporalLayers << 3 |
                hvcc->temporalIdNested  << 2 |
                hvcc->lengthSizeMinusOne);

    /* unsigned int(8) numOfArrays; */
    avio_w8(pb, numOfArrays);

    for (unsigned i = 0; i < FF_ARRAY_ELEMS(hvcc->arrays); i++) {
        const HVCCNALUnitArray *const array = &hvcc->arrays[i];

        if (!numNalus[i])
            continue;
        /*
         * bit(1) array_completeness;
         * unsigned int(1) reserved = 0;
         * unsigned int(6) NAL_unit_type;
         */
        avio_w8(pb, array->array_completeness << 7 |
                    array->NAL_unit_type & 0x3f);

        /* unsigned int(16) numNalus; */
        avio_wb16(pb, numNalus[i]);

        for (unsigned j = 0; j < array->numNalus; j++) {
            HVCCNALUnit *nal = &array->nal[j];

            if (is_lhvc && nal->nuh_layer_id == 0)
                continue;

            /* unsigned int(16) nalUnitLength; */
            avio_wb16(pb, nal->nalUnitLength);

            /* bit(8*nalUnitLength) nalUnit; */
            avio_write(pb, nal->nalUnit, nal->nalUnitLength);
        }
    }

    return 0;
}

int ff_hevc_annexb2mp4(AVIOContext *pb, const uint8_t *buf_in,
                       int size, int filter_ps, int *ps_count)
{
    int num_ps = 0, ret = 0;
    uint8_t *buf, *end, *start = NULL;

    if (!filter_ps) {
        ret = ff_nal_parse_units(pb, buf_in, size);
        goto end;
    }

    ret = ff_nal_parse_units_buf(buf_in, &start, &size);
    if (ret < 0)
        goto end;

    ret = 0;
    buf = start;
    end = start + size;

    while (end - buf > 4) {
        uint32_t len = FFMIN(AV_RB32(buf), end - buf - 4);
        uint8_t type = (buf[4] >> 1) & 0x3f;

        buf += 4;

        switch (type) {
        case HEVC_NAL_VPS:
        case HEVC_NAL_SPS:
        case HEVC_NAL_PPS:
            num_ps++;
            break;
        default:
            ret += 4 + len;
            avio_wb32(pb, len);
            avio_write(pb, buf, len);
            break;
        }

        buf += len;
    }

end:
    av_free(start);
    if (ps_count)
        *ps_count = num_ps;
    return ret;
}

int ff_hevc_annexb2mp4_buf(const uint8_t *buf_in, uint8_t **buf_out,
                           int *size, int filter_ps, int *ps_count)
{
    AVIOContext *pb;
    int ret;

    ret = avio_open_dyn_buf(&pb);
    if (ret < 0)
        return ret;

    ret   = ff_hevc_annexb2mp4(pb, buf_in, *size, filter_ps, ps_count);
    if (ret < 0) {
        ffio_free_dyn_buf(&pb);
        return ret;
    }

    *size = avio_close_dyn_buf(pb, buf_out);

    return 0;
}

static int hvcc_parse_nal_unit(const uint8_t *buf, uint32_t len, int type,
                               HEVCDecoderConfigurationRecord *hvcc,
                               int flags)
{
    for (unsigned i = 0; i < FF_ARRAY_ELEMS(hvcc->arrays); i++) {
        static const uint8_t array_idx_to_type[] =
            { HEVC_NAL_VPS, HEVC_NAL_SPS, HEVC_NAL_PPS,
              HEVC_NAL_SEI_PREFIX, HEVC_NAL_SEI_SUFFIX };

        if (type == array_idx_to_type[i]) {
            int ret = hvcc_add_nal_unit(buf, len, hvcc, flags, i);
            if (ret < 0)
                return ret;
            break;
        }
    }

    return 0;
}

static int write_configuration_record(AVIOContext *pb, const uint8_t *data,
                                      int size, int flags)
{
    HEVCDecoderConfigurationRecord hvcc;
    uint8_t *buf, *end, *start = NULL;
    int ret;

    if (size < 6) {
        /* We can't write a valid hvcC from the provided data */
        return AVERROR_INVALIDDATA;
    } else if (*data == 1) {
        /* Data is already hvcC-formatted. Parse the arrays to skip any NALU
           with nuh_layer_id > 0 */
        GetBitContext gbc;
        int num_arrays;

        if (size < 23)
            return AVERROR_INVALIDDATA;

        ret = init_get_bits8(&gbc, data, size);
        if (ret < 0)
            return ret;

        hvcc_init(&hvcc);
        skip_bits(&gbc, 8); // hvcc.configurationVersion
        hvcc.general_profile_space = get_bits(&gbc, 2);
        hvcc.general_tier_flag = get_bits1(&gbc);
        hvcc.general_profile_idc = get_bits(&gbc, 5);
        hvcc.general_profile_compatibility_flags = get_bits_long(&gbc, 32);
        hvcc.general_constraint_indicator_flags = get_bits64(&gbc, 48);
        hvcc.general_level_idc = get_bits(&gbc, 8);
        skip_bits(&gbc, 4); // reserved
        hvcc.min_spatial_segmentation_idc = get_bits(&gbc, 12);
        skip_bits(&gbc, 6); // reserved
        hvcc.parallelismType = get_bits(&gbc, 2);
        skip_bits(&gbc, 6); // reserved
        hvcc.chromaFormat = get_bits(&gbc, 2);
        skip_bits(&gbc, 5); // reserved
        hvcc.bitDepthLumaMinus8 = get_bits(&gbc, 3);
        skip_bits(&gbc, 5); // reserved
        hvcc.bitDepthChromaMinus8 = get_bits(&gbc, 3);
        hvcc.avgFrameRate = get_bits(&gbc, 16);
        hvcc.constantFrameRate = get_bits(&gbc, 2);
        hvcc.numTemporalLayers = get_bits(&gbc, 3);
        hvcc.temporalIdNested = get_bits1(&gbc);
        hvcc.lengthSizeMinusOne = get_bits(&gbc, 2);

        flags |= FLAG_IS_NALFF;

        num_arrays = get_bits(&gbc, 8);
        for (int i = 0; i < num_arrays; i++) {
            int type, num_nalus;

            skip_bits(&gbc, 2);
            type = get_bits(&gbc, 6);
            num_nalus = get_bits(&gbc, 16);
            for (int j = 0; j < num_nalus; j++) {
                int len = get_bits(&gbc, 16);

                if (len > (get_bits_left(&gbc) / 8))
                    goto end;

                ret = hvcc_parse_nal_unit(data + get_bits_count(&gbc) / 8,
                                          len, type, &hvcc, flags);
                if (ret < 0)
                    goto end;

                skip_bits_long(&gbc, len * 8);
            }
        }

        ret = hvcc_write(pb, &hvcc, flags);
        goto end;
    } else if (!(AV_RB24(data) == 1 || AV_RB32(data) == 1)) {
        /* Not a valid Annex B start code prefix */
        return AVERROR_INVALIDDATA;
    }

    ret = ff_nal_parse_units_buf(data, &start, &size);
    if (ret < 0)
        return ret;

    hvcc_init(&hvcc);

    buf = start;
    end = start + size;

    while (end - buf > 4) {
        uint32_t len = FFMIN(AV_RB32(buf), end - buf - 4);
        uint8_t type = (buf[4] >> 1) & 0x3f;

        buf += 4;

        ret = hvcc_parse_nal_unit(buf, len, type, &hvcc, flags);
        if (ret < 0)
            goto end;

        buf += len;
    }

    ret = hvcc_write(pb, &hvcc, flags);

end:
    hvcc_close(&hvcc);
    av_free(start);
    return ret;
}

int ff_isom_write_hvcc(AVIOContext *pb, const uint8_t *data,
                       int size, int ps_array_completeness)
{
    return write_configuration_record(pb, data, size,
                                      !!ps_array_completeness * FLAG_ARRAY_COMPLETENESS);
}

int ff_isom_write_lhvc(AVIOContext *pb, const uint8_t *data,
                       int size, int ps_array_completeness)
{
    return write_configuration_record(pb, data, size,
                                      (!!ps_array_completeness * FLAG_ARRAY_COMPLETENESS) | FLAG_IS_LHVC);
}
