/*
 * H.266/VVC helper functions for muxers
 *
 * Copyright (C) 2022, Thomas Siedel
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
#include "libavcodec/put_bits.h"
#include "libavcodec/golomb.h"
#include "libavcodec/vvc.h"
#include "libavutil/avassert.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "avc.h"
#include "avio.h"
#include "avio_internal.h"
#include "nal.h"
#include "vvc.h"

enum {
    OPI_INDEX,
    VPS_INDEX,
    SPS_INDEX,
    PPS_INDEX,
    SEI_PREFIX_INDEX,
    SEI_SUFFIX_INDEX,
    NB_ARRAYS
};

typedef struct VVCCNALUnitArray {
    uint8_t array_completeness;
    uint8_t NAL_unit_type;
    uint16_t num_nalus;
    uint16_t *nal_unit_length;
    uint8_t **nal_unit;
} VVCCNALUnitArray;

typedef struct VVCPTLRecord {
    uint8_t num_bytes_constraint_info;
    uint8_t general_profile_idc;
    uint8_t general_tier_flag;
    uint8_t general_level_idc;
    uint8_t ptl_frame_only_constraint_flag;
    uint8_t ptl_multilayer_enabled_flag;
    uint8_t general_constraint_info[9];
    uint8_t ptl_sublayer_level_present_flag[VVC_MAX_SUBLAYERS - 1];
    uint8_t sublayer_level_idc[VVC_MAX_SUBLAYERS - 1];
    uint8_t ptl_num_sub_profiles;
    uint32_t general_sub_profile_idc[VVC_MAX_SUB_PROFILES];
} VVCPTLRecord;

typedef struct VVCDecoderConfigurationRecord {
    uint8_t lengthSizeMinusOne;
    uint8_t ptl_present_flag;
    uint16_t ols_idx;
    uint8_t num_sublayers;
    uint8_t constant_frame_rate;
    uint8_t chroma_format_idc;
    uint8_t bit_depth_minus8;
    VVCPTLRecord ptl;
    uint16_t max_picture_width;
    uint16_t max_picture_height;
    uint16_t avg_frame_rate;
    uint8_t num_of_arrays;
    VVCCNALUnitArray arrays[NB_ARRAYS];
} VVCDecoderConfigurationRecord;

static void vvcc_update_ptl(VVCDecoderConfigurationRecord *vvcc,
                            VVCPTLRecord *ptl)
{
    /*
     * The level indication general_level_idc must indicate a level of
     * capability equal to or greater than the highest level indicated for the
     * highest tier in all the parameter sets.
     */
    if (vvcc->ptl.general_tier_flag < ptl->general_tier_flag)
        vvcc->ptl.general_level_idc = ptl->general_level_idc;
    else
        vvcc->ptl.general_level_idc =
            FFMAX(vvcc->ptl.general_level_idc, ptl->general_level_idc);

    /*
     * The tier indication general_tier_flag must indicate a tier equal to or
     * greater than the highest tier indicated in all the parameter sets.
     */
    vvcc->ptl.general_tier_flag =
        FFMAX(vvcc->ptl.general_tier_flag, ptl->general_tier_flag);

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
    vvcc->ptl.general_profile_idc =
        FFMAX(vvcc->ptl.general_profile_idc, ptl->general_profile_idc);

    /*
     * Each bit in flags may only be set if all
     * the parameter sets set that bit.
     */
    vvcc->ptl.ptl_frame_only_constraint_flag &=
        ptl->ptl_frame_only_constraint_flag;
    vvcc->ptl.ptl_multilayer_enabled_flag &= ptl->ptl_multilayer_enabled_flag;

    /*
     * Constraints Info
     */
    if (ptl->num_bytes_constraint_info) {
        vvcc->ptl.num_bytes_constraint_info = ptl->num_bytes_constraint_info;
        memcpy(&vvcc->ptl.general_constraint_info[0],
               &ptl->general_constraint_info[0], ptl->num_bytes_constraint_info);
    } else {
        vvcc->ptl.num_bytes_constraint_info = 1;
        memset(&vvcc->ptl.general_constraint_info[0], 0, sizeof(vvcc->ptl.general_constraint_info));
    }

    /*
     * Each bit in flags may only be set if one of
     * the parameter sets set that bit.
     */
    memset(vvcc->ptl.ptl_sublayer_level_present_flag, 0,
           sizeof(uint8_t) * vvcc->num_sublayers - 1);
    memset(vvcc->ptl.sublayer_level_idc, 0,
           sizeof(uint8_t) * vvcc->num_sublayers - 1);

    for (int i = vvcc->num_sublayers - 2; i >= 0; i--) {
        vvcc->ptl.ptl_sublayer_level_present_flag[i] |=
            ptl->ptl_sublayer_level_present_flag[i];
        if (vvcc->ptl.ptl_sublayer_level_present_flag[i]) {
            vvcc->ptl.sublayer_level_idc[i] =
                FFMAX(vvcc->ptl.sublayer_level_idc[i],
                      ptl->sublayer_level_idc[i]);
        } else {
            if (i == vvcc->num_sublayers - 1) {
                vvcc->ptl.sublayer_level_idc[i] = vvcc->ptl.general_level_idc;
            } else {
                vvcc->ptl.sublayer_level_idc[i] =
                    vvcc->ptl.sublayer_level_idc[i + 1];
            }
        }
    }

    vvcc->ptl.ptl_num_sub_profiles =
        FFMAX(vvcc->ptl.ptl_num_sub_profiles, ptl->ptl_num_sub_profiles);
    if (vvcc->ptl.ptl_num_sub_profiles) {
        for (int i = 0; i < vvcc->ptl.ptl_num_sub_profiles; i++) {
            vvcc->ptl.general_sub_profile_idc[i] =
                ptl->general_sub_profile_idc[i];
        }
    }
}

static void vvcc_parse_ptl(GetBitContext *gb,
                           VVCDecoderConfigurationRecord *vvcc,
                           unsigned int profileTierPresentFlag,
                           unsigned int max_sub_layers_minus1)
{
    VVCPTLRecord general_ptl = { 0 };

    if (profileTierPresentFlag) {
        general_ptl.general_profile_idc = get_bits(gb, 7);
        general_ptl.general_tier_flag = get_bits1(gb);
    }
    general_ptl.general_level_idc = get_bits(gb, 8);

    general_ptl.ptl_frame_only_constraint_flag = get_bits1(gb);
    general_ptl.ptl_multilayer_enabled_flag = get_bits1(gb);
    if (profileTierPresentFlag) {       // parse constraint info
        general_ptl.num_bytes_constraint_info = get_bits1(gb); // gci_present_flag
        if (general_ptl.num_bytes_constraint_info) {
            int gci_num_reserved_bits, j;
            for (j = 0; j < 8; j++)
                general_ptl.general_constraint_info[j] = get_bits(gb, 8);
            general_ptl.general_constraint_info[j++] = get_bits(gb, 7);

            gci_num_reserved_bits = get_bits(gb, 8);
            general_ptl.num_bytes_constraint_info = j;
            skip_bits(gb, gci_num_reserved_bits);
        }
        align_get_bits(gb);
    }

    for (int i = max_sub_layers_minus1 - 1; i >= 0; i--)
        general_ptl.ptl_sublayer_level_present_flag[i] = get_bits1(gb);

    align_get_bits(gb);

    for (int i = max_sub_layers_minus1 - 1; i >= 0; i--) {
        if (general_ptl.ptl_sublayer_level_present_flag[i])
            general_ptl.sublayer_level_idc[i] = get_bits(gb, 8);
    }

    if (profileTierPresentFlag) {
        general_ptl.ptl_num_sub_profiles = get_bits(gb, 8);
        if (general_ptl.ptl_num_sub_profiles) {
            for (int i = 0; i < general_ptl.ptl_num_sub_profiles; i++)
                general_ptl.general_sub_profile_idc[i] = get_bits_long(gb, 32);
        }
    }

    vvcc_update_ptl(vvcc, &general_ptl);
}

static int vvcc_parse_vps(GetBitContext *gb,
                          VVCDecoderConfigurationRecord *vvcc)
{
    unsigned int vps_max_layers_minus1;
    unsigned int vps_max_sublayers_minus1;
    unsigned int vps_default_ptl_dpb_hrd_max_tid_flag;
    unsigned int vps_all_independent_layers_flag;

    unsigned int vps_pt_present_flag[VVC_MAX_PTLS];
    unsigned int vps_ptl_max_tid[VVC_MAX_PTLS];
    unsigned int vps_num_ptls_minus1 = 0;

    /*
     * vps_video_parameter_set_id u(4)
     */
    skip_bits(gb, 4);

    vps_max_layers_minus1 = get_bits(gb, 6);
    vps_max_sublayers_minus1 = get_bits(gb, 3);

    /*
     * numTemporalLayers greater than 1 indicates that the stream to which this
     * configuration record applies is temporally scalable and the contained
     * number of temporal layers (also referred to as temporal sub-layer or
     * sub-layer in ISO/IEC 23008-2) is equal to numTemporalLayers. Value 1
     * indicates that the stream is not temporally scalable. Value 0 indicates
     * that it is unknown whether the stream is temporally scalable.
     */
    vvcc->num_sublayers = FFMAX(vvcc->num_sublayers,
                                vps_max_sublayers_minus1 + 1);

    if (vps_max_layers_minus1 > 0 && vps_max_sublayers_minus1 > 0)
        vps_default_ptl_dpb_hrd_max_tid_flag = get_bits1(gb);
    else
        vps_default_ptl_dpb_hrd_max_tid_flag = 0;
    if (vps_max_layers_minus1 > 0)
        vps_all_independent_layers_flag = get_bits1(gb);
    else
        vps_all_independent_layers_flag = 1;

    for (int i = 0; i <= vps_max_layers_minus1; i++) {
        skip_bits(gb, 6);    //vps_layer_id[i]
        if (i > 0 && !vps_all_independent_layers_flag) {
            if (!get_bits1(gb)) {   // vps_independent_layer_flag[i]
                unsigned int vps_max_tid_ref_present_flag = get_bits1(gb);
                for (int j = 0; j < i; j++) {
                    unsigned int vps_direct_ref_layer_flag = get_bits1(gb);
                    if (vps_max_tid_ref_present_flag && vps_direct_ref_layer_flag)
                        skip_bits(gb, 3);                               // vps_max_tid_il_ref_pics_plus1
                }
            }
        }
    }

    if (vps_max_layers_minus1 > 0) {
        unsigned int vps_each_layer_is_an_ols_flag;
        if (vps_all_independent_layers_flag)
            vps_each_layer_is_an_ols_flag = get_bits1(gb);
        else
            vps_each_layer_is_an_ols_flag = 0;
        if (!vps_each_layer_is_an_ols_flag) {
            unsigned int vps_ols_mode_idc;
            if (!vps_all_independent_layers_flag)
                vps_ols_mode_idc = get_bits(gb, 2);
            else
                vps_ols_mode_idc = 2;
            if (vps_ols_mode_idc == 2) {
                unsigned int vps_num_output_layer_sets_minus2 = get_bits(gb, 8);
                for (int i = 1; i <= vps_num_output_layer_sets_minus2 + 1; i++) {
                    for (int j = 0; j <= vps_max_layers_minus1; j++) {
                        skip_bits1(gb); // vps_ols_output_layer_flag[i][j]
                    }
                }
            }
        }
        vps_num_ptls_minus1 = get_bits(gb, 8);
    }

    for (int i = 0; i <= vps_num_ptls_minus1; i++) {
        if (i > 0)
            vps_pt_present_flag[i] = get_bits1(gb);
        else
            vps_pt_present_flag[i] = 1;

        if (!vps_default_ptl_dpb_hrd_max_tid_flag)
            vps_ptl_max_tid[i] = get_bits(gb, 3);
        else
            vps_ptl_max_tid[i] = vps_max_sublayers_minus1;
    }

    align_get_bits(gb);

    for (int i = 0; i <= vps_num_ptls_minus1; i++)
        vvcc_parse_ptl(gb, vvcc, vps_pt_present_flag[i], vps_ptl_max_tid[i]);
    vvcc->ptl_present_flag = 1;

    /* nothing useful for vvcc past this point */
    return 0;
}

static int vvcc_parse_sps(GetBitContext *gb,
                          VVCDecoderConfigurationRecord *vvcc)
{
    unsigned int sps_max_sublayers_minus1, sps_log2_ctu_size_minus5;
    unsigned int sps_subpic_same_size_flag, sps_pic_height_max_in_luma_samples,
        sps_pic_width_max_in_luma_samples;
    unsigned int sps_independent_subpics_flag;

    skip_bits(gb, 8);  // sps_seq_parameter_set_id && sps_video_parameter_set_id
    sps_max_sublayers_minus1 = get_bits(gb, 3);

    /*
     * numTemporalLayers greater than 1 indicates that the stream to which this
     * configuration record applies is temporally scalable and the contained
     * number of temporal layers (also referred to as temporal sub-layer or
     * sub-layer in ISO/IEC 23008-2) is equal to numTemporalLayers. Value 1
     * indicates that the stream is not temporally scalable. Value 0 indicates
     * that it is unknown whether the stream is temporally scalable.
     */
    vvcc->num_sublayers = FFMAX(vvcc->num_sublayers,
                                sps_max_sublayers_minus1 + 1);

    vvcc->chroma_format_idc = get_bits(gb, 2);
    sps_log2_ctu_size_minus5 = get_bits(gb, 2);

    if (get_bits1(gb)) {        // sps_ptl_dpb_hrd_params_present_flag
        vvcc->ptl_present_flag = 1;
        vvcc_parse_ptl(gb, vvcc, 1, sps_max_sublayers_minus1);
    }

    skip_bits1(gb);             // sps_gdr_enabled_flag
    if (get_bits(gb, 1))        // sps_ref_pic_resampling_enabled_flag
        skip_bits1(gb);         // sps_res_change_in_clvs_allowed_flag

    sps_pic_width_max_in_luma_samples = get_ue_golomb_long(gb);
    vvcc->max_picture_width =
        FFMAX(vvcc->max_picture_width, sps_pic_width_max_in_luma_samples);
    sps_pic_height_max_in_luma_samples = get_ue_golomb_long(gb);
    vvcc->max_picture_height =
        FFMAX(vvcc->max_picture_height, sps_pic_height_max_in_luma_samples);

    if (get_bits1(gb)) {
        get_ue_golomb_long(gb); // sps_conf_win_left_offset
        get_ue_golomb_long(gb); // sps_conf_win_right_offset
        get_ue_golomb_long(gb); // sps_conf_win_top_offset
        get_ue_golomb_long(gb); // sps_conf_win_bottom_offset
    }

    if (get_bits1(gb)) {        // sps_subpic_info_present_flag
        const unsigned int sps_num_subpics_minus1 = get_ue_golomb_long(gb);
        const int ctb_log2_size_y = sps_log2_ctu_size_minus5 + 5;
        const int ctb_size_y      = 1 << ctb_log2_size_y;
        const int tmp_width_val   = AV_CEIL_RSHIFT(sps_pic_width_max_in_luma_samples,  ctb_log2_size_y);
        const int tmp_height_val  = AV_CEIL_RSHIFT(sps_pic_height_max_in_luma_samples, ctb_log2_size_y);
        const int wlen            = av_ceil_log2(tmp_width_val);
        const int hlen            = av_ceil_log2(tmp_height_val);
        unsigned int sps_subpic_id_len;
        if (sps_num_subpics_minus1 > 0) {       // sps_num_subpics_minus1
            sps_independent_subpics_flag = get_bits1(gb);
            sps_subpic_same_size_flag = get_bits1(gb);
        }
        for (int i = 0; sps_num_subpics_minus1 > 0 && i <= sps_num_subpics_minus1; i++) {
            if (!sps_subpic_same_size_flag || i == 0) {
                if (i > 0 && sps_pic_width_max_in_luma_samples > ctb_size_y)
                    skip_bits(gb, wlen);
                if (i > 0 && sps_pic_height_max_in_luma_samples > ctb_size_y)
                    skip_bits(gb, hlen);
                if (i < sps_num_subpics_minus1 && sps_pic_width_max_in_luma_samples > ctb_size_y)
                    skip_bits(gb, wlen);
                if (i < sps_num_subpics_minus1 && sps_pic_height_max_in_luma_samples > ctb_size_y)
                    skip_bits(gb, hlen);
            }
            if (!sps_independent_subpics_flag) {
                skip_bits(gb, 2);       // sps_subpic_treated_as_pic_flag && sps_loop_filter_across_subpic_enabled_flag
            }
        }
        sps_subpic_id_len = get_ue_golomb_long(gb) + 1;
        if (get_bits1(gb)) {    // sps_subpic_id_mapping_explicitly_signalled_flag
            if (get_bits1(gb))  // sps_subpic_id_mapping_present_flag
                for (int i = 0; i <= sps_num_subpics_minus1; i++) {
                    skip_bits_long(gb, sps_subpic_id_len); // sps_subpic_id[i]
                }
        }
    }
    vvcc->bit_depth_minus8 = get_ue_golomb_long(gb);

    /* nothing useful for vvcc past this point */
    return 0;
}

static int vvcc_parse_pps(GetBitContext *gb,
                          VVCDecoderConfigurationRecord *vvcc)
{

    // Nothing of importance to parse in PPS
    /* nothing useful for vvcc past this point */
    return 0;
}

static void nal_unit_parse_header(GetBitContext *gb, uint8_t *nal_type)
{
    /*
     * forbidden_zero_bit    u(1)
     * nuh_reserved_zero_bit u(1)
     * nuh_layer_id          u(6)
     */
    skip_bits(gb, 8);
    *nal_type = get_bits(gb, 5);

    /*
     * nuh_temporal_id_plus1 u(3)
     */
    skip_bits(gb, 3);
}

static int vvcc_array_add_nal_unit(uint8_t *nal_buf, uint32_t nal_size,
                                   uint8_t nal_type, int ps_array_completeness,
                                   VVCCNALUnitArray *array)
{
    int ret;
    uint16_t num_nalus;

    num_nalus = array->num_nalus;

    ret = av_reallocp_array(&array->nal_unit, num_nalus + 1, sizeof(uint8_t *));
    if (ret < 0)
        return ret;

    ret =
        av_reallocp_array(&array->nal_unit_length, num_nalus + 1,
                          sizeof(uint16_t));
    if (ret < 0)
        return ret;

    array->nal_unit[num_nalus] = nal_buf;
    array->nal_unit_length[num_nalus] = nal_size;
    array->NAL_unit_type = nal_type;
    array->num_nalus++;

    /*
    * When the sample entry name is 'vvc1', the following applies:
    * • The value of array_completeness shall be equal to 1 for arrays of SPS,
    *   and PPS NAL units.
    * • If a VVC bitstream includes DCI NAL unit(s), the value of
    *   array_completeness shall be equal to 1 for the array of DCI units.
    *   Otherwise, NAL_unit_type shall not indicate DCI NAL units.
    * • If a VVC bitstream includes VPS NAL unit(s), the value of
    *   array_completeness shall be equal to 1 for the array of VPS NAL units.
    *   Otherwise, NAL_unit_type shall not indicate VPS NAL units.
    * When the value of array_completeness is equal to 1 for an array of a
    * particular NAL_unit_type value, NAL units of that NAL_unit_type value
    * cannot be updated without causing a different sample entry to be used.
    * When the sample entry name is 'vvi1', the value of array_completeness
    * of at least one of the following arrays shall be equal to 0:
      • The array of DCI NAL units, if present.
      • The array of VPS NAL units, if present.
      • The array of SPS NAL units
      • The array of PPS NAL units.
    */
    if (nal_type == VVC_VPS_NUT || nal_type == VVC_SPS_NUT ||
        nal_type == VVC_PPS_NUT || nal_type == VVC_DCI_NUT )
        array->array_completeness = ps_array_completeness;

    return 0;
}

static int vvcc_add_nal_unit(uint8_t *nal_buf, uint32_t nal_size,
                             int ps_array_completeness,
                             VVCDecoderConfigurationRecord *vvcc,
                             unsigned array_idx)
{
    int ret = 0;
    GetBitContext gbc;
    uint8_t nal_type;
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

    nal_unit_parse_header(&gbc, &nal_type);

    /*
     * Note: only 'declarative' SEI messages are allowed in
     * vvcc. Perhaps the SEI playload type should be checked
     * and non-declarative SEI messages discarded?
     */
    ret = vvcc_array_add_nal_unit(nal_buf, nal_size, nal_type,
                                  ps_array_completeness,
                                  &vvcc->arrays[array_idx]);
    if (ret < 0)
        goto end;
    if (vvcc->arrays[array_idx].num_nalus == 1)
        vvcc->num_of_arrays++;

    if (nal_type == VVC_VPS_NUT)
        ret = vvcc_parse_vps(&gbc, vvcc);
    else if (nal_type == VVC_SPS_NUT)
        ret = vvcc_parse_sps(&gbc, vvcc);
    else if (nal_type == VVC_PPS_NUT)
        ret = vvcc_parse_pps(&gbc, vvcc);
    else if (nal_type == VVC_OPI_NUT) {
        // not yet supported
    }
    if (ret < 0)
        goto end;

  end:
    av_free(rbsp_buf);
    return ret;
}

static void vvcc_init(VVCDecoderConfigurationRecord *vvcc)
{
    memset(vvcc, 0, sizeof(VVCDecoderConfigurationRecord));
    vvcc->lengthSizeMinusOne = 3;       // 4 bytes
    vvcc->ptl.ptl_frame_only_constraint_flag =
    vvcc->ptl.ptl_multilayer_enabled_flag = 1;
}

static void vvcc_close(VVCDecoderConfigurationRecord *vvcc)
{
    for (unsigned i = 0; i < FF_ARRAY_ELEMS(vvcc->arrays); i++) {
        VVCCNALUnitArray *const array = &vvcc->arrays[i];

        array->num_nalus = 0;
        av_freep(&array->nal_unit);
        av_freep(&array->nal_unit_length);
    }

    vvcc->num_of_arrays = 0;
}

static int vvcc_write(AVIOContext *pb, VVCDecoderConfigurationRecord *vvcc)
{
    uint16_t vps_count = 0, sps_count = 0, pps_count = 0;
    /*
     * It's unclear how to properly compute these fields, so
     * let's always set them to values meaning 'unspecified'.
     */
    vvcc->avg_frame_rate = 0;
    vvcc->constant_frame_rate = 1;

    av_log(NULL, AV_LOG_TRACE,
           "lengthSizeMinusOne:                  %" PRIu8 "\n",
           vvcc->lengthSizeMinusOne);
    av_log(NULL, AV_LOG_TRACE,
           "ptl_present_flag:                    %" PRIu8 "\n",
           vvcc->ptl_present_flag);
    av_log(NULL, AV_LOG_TRACE,
           "ols_idx:                             %" PRIu16 "\n", vvcc->ols_idx);
    av_log(NULL, AV_LOG_TRACE,
           "num_sublayers:                       %" PRIu8 "\n",
           vvcc->num_sublayers);
    av_log(NULL, AV_LOG_TRACE,
           "constant_frame_rate:                 %" PRIu8 "\n",
           vvcc->constant_frame_rate);
    av_log(NULL, AV_LOG_TRACE,
           "chroma_format_idc:                   %" PRIu8 "\n",
           vvcc->chroma_format_idc);

    av_log(NULL, AV_LOG_TRACE,
           "bit_depth_minus8:                    %" PRIu8 "\n",
           vvcc->bit_depth_minus8);
    av_log(NULL, AV_LOG_TRACE,
           "num_bytes_constraint_info:           %" PRIu8 "\n",
           vvcc->ptl.num_bytes_constraint_info);
    av_log(NULL, AV_LOG_TRACE,
           "general_profile_idc:                 %" PRIu8 "\n",
           vvcc->ptl.general_profile_idc);
    av_log(NULL, AV_LOG_TRACE,
           "general_tier_flag:                   %" PRIu8 "\n",
           vvcc->ptl.general_tier_flag);
    av_log(NULL, AV_LOG_TRACE,
           "general_level_idc:                   %" PRIu8 "\n",
           vvcc->ptl.general_level_idc);
    av_log(NULL, AV_LOG_TRACE,
           "ptl_frame_only_constraint_flag:      %" PRIu8 "\n",
           vvcc->ptl.ptl_frame_only_constraint_flag);
    av_log(NULL, AV_LOG_TRACE,
           "ptl_multilayer_enabled_flag:         %" PRIu8 "\n",
           vvcc->ptl.ptl_multilayer_enabled_flag);
    for (int i = 0; i < vvcc->ptl.num_bytes_constraint_info; i++) {
        av_log(NULL, AV_LOG_TRACE,
               "general_constraint_info[%d]:          %" PRIu8 "\n", i,
               vvcc->ptl.general_constraint_info[i]);
    }

    for (int i = 0; i < vvcc->num_sublayers - 1; i++) {
        av_log(NULL, AV_LOG_TRACE,
               "ptl_sublayer_level_present_flag[%d]:  %" PRIu8 "\n", i,
               vvcc->ptl.ptl_sublayer_level_present_flag[i]);
        av_log(NULL, AV_LOG_TRACE,
               "sublayer_level_idc[%d]: %" PRIu8 "\n", i,
               vvcc->ptl.sublayer_level_idc[i]);
    }

    av_log(NULL, AV_LOG_TRACE,
           "num_sub_profiles:                    %" PRIu8 "\n",
           vvcc->ptl.ptl_num_sub_profiles);

    for (unsigned i = 0; i < vvcc->ptl.ptl_num_sub_profiles; i++) {
        av_log(NULL, AV_LOG_TRACE,
               "general_sub_profile_idc[%u]:         %" PRIx32 "\n", i,
               vvcc->ptl.general_sub_profile_idc[i]);
    }

    av_log(NULL, AV_LOG_TRACE,
           "max_picture_width:                   %" PRIu16 "\n",
           vvcc->max_picture_width);
    av_log(NULL, AV_LOG_TRACE,
           "max_picture_height:                  %" PRIu16 "\n",
           vvcc->max_picture_height);
    av_log(NULL, AV_LOG_TRACE,
           "avg_frame_rate:                      %" PRIu16 "\n",
           vvcc->avg_frame_rate);

    av_log(NULL, AV_LOG_TRACE,
           "num_of_arrays:                       %" PRIu8 "\n",
           vvcc->num_of_arrays);
    for (unsigned i = 0; i < FF_ARRAY_ELEMS(vvcc->arrays); i++) {
        const VVCCNALUnitArray *const array = &vvcc->arrays[i];

        if (array->num_nalus == 0)
            continue;

        av_log(NULL, AV_LOG_TRACE,
               "array_completeness[%u]:               %" PRIu8 "\n", i,
               array->array_completeness);
        av_log(NULL, AV_LOG_TRACE,
               "NAL_unit_type[%u]:                    %" PRIu8 "\n", i,
               array->NAL_unit_type);
        av_log(NULL, AV_LOG_TRACE,
               "num_nalus[%u]:                        %" PRIu16 "\n", i,
               array->num_nalus);
        for (unsigned j = 0; j < array->num_nalus; j++)
            av_log(NULL, AV_LOG_TRACE,
                   "nal_unit_length[%u][%u]:               %"
                   PRIu16 "\n", i, j, array->nal_unit_length[j]);
    }

    /*
     * We need at least one of each: SPS and PPS.
     */
    vps_count = vvcc->arrays[VPS_INDEX].num_nalus;
    sps_count = vvcc->arrays[SPS_INDEX].num_nalus;
    pps_count = vvcc->arrays[PPS_INDEX].num_nalus;
    if (vps_count > VVC_MAX_VPS_COUNT)
        return AVERROR_INVALIDDATA;
    if (!sps_count || sps_count > VVC_MAX_SPS_COUNT)
        return AVERROR_INVALIDDATA;
    if (!pps_count || pps_count > VVC_MAX_PPS_COUNT)
        return AVERROR_INVALIDDATA;

    /* bit(5) reserved = ‘11111’b;
       unsigned int (2) LengthSizeMinusOne
       unsigned int (1) ptl_present_flag */
    avio_w8(pb, vvcc->lengthSizeMinusOne << 1 | vvcc->ptl_present_flag | 0xf8);

    if (vvcc->ptl_present_flag) {
        uint8_t buf[64];
        PutBitContext pbc;

        init_put_bits(&pbc, buf, sizeof(buf));
        /*
         * unsigned int(9) ols_idx;
         * unsigned int(3) num_sublayers;
         * unsigned int(2) constant_frame_rate;
         * unsigned int(2) chroma_format_idc;     */
        avio_wb16(pb,
                  vvcc->ols_idx << 7 | vvcc->num_sublayers << 4 | vvcc->
                  constant_frame_rate << 2 | vvcc->chroma_format_idc);

        /* unsigned int(3) bit_depth_minus8;
           bit(5) reserved = ‘11111’b; */
        avio_w8(pb, vvcc->bit_depth_minus8 << 5 | 0x1f);

        //VVCPTLRecord

        /* bit(2) reserved = ‘00’b;
           unsigned int (6) num_bytes_constraint_info */
        avio_w8(pb, vvcc->ptl.num_bytes_constraint_info & 0x3f);

        /* unsigned int (7) general_profile_idc
           unsigned int (1) general_tier_flag */
        avio_w8(pb,
                vvcc->ptl.general_profile_idc << 1 | vvcc->ptl.general_tier_flag);

        /* unsigned int (8) general_level_idc */
        avio_w8(pb, vvcc->ptl.general_level_idc);

        /*
         * unsigned int (1) ptl_frame_only_constraint_flag
         * unsigned int (1) ptl_multilayer_enabled_flag
         * unsigned int (8*num_bytes_constraint_info -2) general_constraint_info */
        put_bits(&pbc, 1, vvcc->ptl.ptl_frame_only_constraint_flag);
        put_bits(&pbc, 1, vvcc->ptl.ptl_multilayer_enabled_flag);
        av_assert0(vvcc->ptl.num_bytes_constraint_info);
        for (int i = 0; i < vvcc->ptl.num_bytes_constraint_info - 1; i++)
            put_bits(&pbc, 8, vvcc->ptl.general_constraint_info[i]);
        put_bits(&pbc, 6, vvcc->ptl.general_constraint_info[vvcc->ptl.num_bytes_constraint_info - 1] & 0x3f);
        flush_put_bits(&pbc);
        avio_write(pb, buf, put_bytes_output(&pbc));

        if (vvcc->num_sublayers > 1) {
            uint8_t ptl_sublayer_level_present_flags = 0;
            for (int i = vvcc->num_sublayers - 2; i >= 0; i--) {
                ptl_sublayer_level_present_flags =
                    (ptl_sublayer_level_present_flags << 1 | vvcc->ptl.
                     ptl_sublayer_level_present_flag[i]);
            }
            avio_w8(pb, ptl_sublayer_level_present_flags);
        }

        for (int i = vvcc->num_sublayers - 2; i >= 0; i--) {
            if (vvcc->ptl.ptl_sublayer_level_present_flag[i])
                avio_w8(pb, vvcc->ptl.sublayer_level_idc[i]);
        }

        /* unsigned int(8) num_sub_profiles; */
        avio_w8(pb, vvcc->ptl.ptl_num_sub_profiles);

        for (int j = 0; j < vvcc->ptl.ptl_num_sub_profiles; j++) {
            /* unsigned int(32) general_sub_profile_idc[j]; */
            avio_wb32(pb, vvcc->ptl.general_sub_profile_idc[j]);
        }

        //End of VvcPTLRecord

        /*
         * unsigned int(16) max_picture_width;*/
        avio_wb16(pb, vvcc->max_picture_width);

        /*
         * unsigned int(16) max_picture_height;*/
        avio_wb16(pb, vvcc->max_picture_height);

        /*
         * unsigned int(16) avg_frame_rate; */
        avio_wb16(pb, vvcc->avg_frame_rate);
    }

    /* unsigned int(8) num_of_arrays; */
    avio_w8(pb, vvcc->num_of_arrays);

    for (unsigned i = 0; i < FF_ARRAY_ELEMS(vvcc->arrays); i++) {
        const VVCCNALUnitArray *const array = &vvcc->arrays[i];

        if (!array->num_nalus)
            continue;
        /*
         * bit(1) array_completeness;
         * unsigned int(2) reserved = 0;
         * unsigned int(5) NAL_unit_type;
         */
        avio_w8(pb, array->array_completeness << 7 |
                array->NAL_unit_type & 0x1f);
        /* unsigned int(16) num_nalus; */
        if (array->NAL_unit_type != VVC_DCI_NUT &&
            array->NAL_unit_type != VVC_OPI_NUT)
            avio_wb16(pb, array->num_nalus);
        for (int j = 0; j < array->num_nalus; j++) {
            /* unsigned int(16) nal_unit_length; */
            avio_wb16(pb, array->nal_unit_length[j]);

            /* bit(8*nal_unit_length) nal_unit; */
            avio_write(pb, array->nal_unit[j],
                       array->nal_unit_length[j]);
        }
    }

    return 0;
}

int ff_vvc_annexb2mp4(AVIOContext *pb, const uint8_t *buf_in,
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
        uint8_t type = (buf[5] >> 3);

        buf += 4;

        switch (type) {
        case VVC_VPS_NUT:
        case VVC_SPS_NUT:
        case VVC_PPS_NUT:
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

int ff_vvc_annexb2mp4_buf(const uint8_t *buf_in, uint8_t **buf_out,
                          int *size, int filter_ps, int *ps_count)
{
    AVIOContext *pb;
    int ret;

    ret = avio_open_dyn_buf(&pb);
    if (ret < 0)
        return ret;

    ret = ff_vvc_annexb2mp4(pb, buf_in, *size, filter_ps, ps_count);
    if (ret < 0) {
        ffio_free_dyn_buf(&pb);
        return ret;
    }

    *size = avio_close_dyn_buf(pb, buf_out);

    return 0;
}

int ff_isom_write_vvcc(AVIOContext *pb, const uint8_t *data,
                       int size, int ps_array_completeness)
{
    VVCDecoderConfigurationRecord vvcc;
    uint8_t *buf, *end, *start;
    int ret;

    if (size < 6) {
        /* We can't write a valid vvcc from the provided data */
        return AVERROR_INVALIDDATA;
    } else if ((*data & 0xf8) == 0xf8) {
        /* Data is already vvcc-formatted */
        avio_write(pb, data, size);
        return 0;
    } else if (!(AV_RB24(data) == 1 || AV_RB32(data) == 1)) {
        /* Not a valid Annex B start code prefix */
        return AVERROR_INVALIDDATA;
    }

    ret = ff_nal_parse_units_buf(data, &start, &size);
    if (ret < 0)
        return ret;

    vvcc_init(&vvcc);

    buf = start;
    end = start + size;

    while (end - buf > 4) {
        uint32_t len = FFMIN(AV_RB32(buf), end - buf - 4);
        uint8_t type = (buf[5] >> 3);

        buf += 4;

        for (unsigned i = 0; i < FF_ARRAY_ELEMS(vvcc.arrays); i++) {
            static const uint8_t array_idx_to_type[] =
                { VVC_OPI_NUT, VVC_VPS_NUT, VVC_SPS_NUT,
                  VVC_PPS_NUT, VVC_PREFIX_SEI_NUT, VVC_SUFFIX_SEI_NUT };

            if (type == array_idx_to_type[i]) {
                ret = vvcc_add_nal_unit(buf, len, ps_array_completeness,
                                        &vvcc, i);
                if (ret < 0)
                    goto end;
                break;
            }
        }

        buf += len;
    }

    ret = vvcc_write(pb, &vvcc);

  end:
    vvcc_close(&vvcc);
    av_free(start);
    return ret;
}
