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

#include "libavutil/mem.h"
#include "hevc/hevcdec.h"
#include "hevc/data.h"
#include "hevc/ps.h"

#include "vulkan_decode.h"

const FFVulkanDecodeDescriptor ff_vk_dec_hevc_desc = {
    .codec_id         = AV_CODEC_ID_HEVC,
    .decode_extension = FF_VK_EXT_VIDEO_DECODE_H265,
    .queue_flags      = VK_QUEUE_VIDEO_DECODE_BIT_KHR,
    .decode_op        = VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR,
    .ext_props = {
        .extensionName = VK_STD_VULKAN_VIDEO_CODEC_H265_DECODE_EXTENSION_NAME,
        .specVersion   = VK_STD_VULKAN_VIDEO_CODEC_H265_DECODE_SPEC_VERSION,
    },
};

typedef struct HEVCHeaderSPS {
    StdVideoH265ScalingLists scaling;
    StdVideoH265HrdParameters vui_header;
    StdVideoH265SequenceParameterSetVui vui;
    StdVideoH265ProfileTierLevel ptl;
    StdVideoH265DecPicBufMgr dpbm;
    StdVideoH265PredictorPaletteEntries pal;
    StdVideoH265SubLayerHrdParameters nal_hdr[HEVC_MAX_SUB_LAYERS];
    StdVideoH265SubLayerHrdParameters vcl_hdr[HEVC_MAX_SUB_LAYERS];
    StdVideoH265ShortTermRefPicSet str[HEVC_MAX_SHORT_TERM_REF_PIC_SETS];
    StdVideoH265LongTermRefPicsSps ltr;
} HEVCHeaderSPS;

typedef struct HEVCHeaderPPS {
    StdVideoH265ScalingLists scaling;
    StdVideoH265PredictorPaletteEntries pal;
} HEVCHeaderPPS;

typedef struct HEVCHeaderVPSSet {
    StdVideoH265SubLayerHrdParameters nal_hdr[HEVC_MAX_SUB_LAYERS];
    StdVideoH265SubLayerHrdParameters vcl_hdr[HEVC_MAX_SUB_LAYERS];
} HEVCHeaderVPSSet;

typedef struct HEVCHeaderVPS {
    StdVideoH265ProfileTierLevel ptl;
    StdVideoH265DecPicBufMgr dpbm;
    StdVideoH265HrdParameters hdr[HEVC_MAX_LAYER_SETS];
    HEVCHeaderVPSSet *sls;
} HEVCHeaderVPS;

typedef struct HEVCHeaderSet {
    StdVideoH265SequenceParameterSet sps[HEVC_MAX_SPS_COUNT];
    HEVCHeaderSPS hsps[HEVC_MAX_SPS_COUNT];

    StdVideoH265PictureParameterSet pps[HEVC_MAX_PPS_COUNT];
    HEVCHeaderPPS hpps[HEVC_MAX_PPS_COUNT];

    StdVideoH265VideoParameterSet vps[HEVC_MAX_PPS_COUNT];
    HEVCHeaderVPS *hvps;
} HEVCHeaderSet;

static int alloc_hevc_header_structs(FFVulkanDecodeContext *s,
                                     int nb_vps,
                                     const int vps_list_idx[HEVC_MAX_VPS_COUNT],
                                     const HEVCVPS * const vps_list[HEVC_MAX_VPS_COUNT])
{
    uint8_t *data_ptr;
    HEVCHeaderSet *hdr;

    size_t buf_size = sizeof(HEVCHeaderSet) + nb_vps*sizeof(HEVCHeaderVPS);
    for (int i = 0; i < nb_vps; i++) {
        const HEVCVPS *vps = vps_list[vps_list_idx[i]];
        buf_size += sizeof(HEVCHeaderVPSSet)*vps->vps_num_hrd_parameters;
    }

    if (buf_size > s->hevc_headers_size) {
        av_freep(&s->hevc_headers);
        s->hevc_headers_size = 0;
        s->hevc_headers = av_mallocz(buf_size);
        if (!s->hevc_headers)
            return AVERROR(ENOMEM);
        s->hevc_headers_size = buf_size;
    }

    /* Setup struct pointers */
    hdr = s->hevc_headers;
    data_ptr = (uint8_t *)hdr;
    hdr->hvps = (HEVCHeaderVPS *)(data_ptr + sizeof(HEVCHeaderSet));
    data_ptr += sizeof(HEVCHeaderSet) + nb_vps*sizeof(HEVCHeaderVPS);
    for (int i = 0; i < nb_vps; i++) {
        const HEVCVPS *vps = vps_list[vps_list_idx[i]];
        hdr->hvps[i].sls = (HEVCHeaderVPSSet *)data_ptr;
        data_ptr += sizeof(HEVCHeaderVPSSet)*vps->vps_num_hrd_parameters;
    }

    return 0;
}

typedef struct HEVCVulkanDecodePicture {
    FFVulkanDecodePicture           vp;

    /* Current picture */
    StdVideoDecodeH265ReferenceInfo h265_ref;
    VkVideoDecodeH265DpbSlotInfoKHR vkh265_ref;

    /* Picture refs */
    HEVCFrame                      *ref_src    [HEVC_MAX_REFS];
    StdVideoDecodeH265ReferenceInfo h265_refs  [HEVC_MAX_REFS];
    VkVideoDecodeH265DpbSlotInfoKHR vkh265_refs[HEVC_MAX_REFS];

    /* Current picture (contd.) */
    StdVideoDecodeH265PictureInfo   h265pic;
    VkVideoDecodeH265PictureInfoKHR h265_pic_info;
} HEVCVulkanDecodePicture;

static int vk_hevc_fill_pict(AVCodecContext *avctx, HEVCFrame **ref_src,
                             VkVideoReferenceSlotInfoKHR *ref_slot,       /* Main structure */
                             VkVideoPictureResourceInfoKHR *ref,          /* Goes in ^ */
                             VkVideoDecodeH265DpbSlotInfoKHR *vkh265_ref, /* Goes in ^ */
                             StdVideoDecodeH265ReferenceInfo *h265_ref,   /* Goes in ^ */
                             HEVCFrame *pic, int is_current, int pic_id)
{
    FFVulkanDecodeContext *dec = avctx->internal->hwaccel_priv_data;
    FFVulkanDecodeShared *ctx = dec->shared_ctx;
    HEVCVulkanDecodePicture *hp = pic->hwaccel_picture_private;
    FFVulkanDecodePicture *vkpic = &hp->vp;

    int err = ff_vk_decode_prepare_frame(dec, pic->f, vkpic, is_current,
                                         dec->dedicated_dpb);
    if (err < 0)
        return err;

    *h265_ref = (StdVideoDecodeH265ReferenceInfo) {
        .flags = (StdVideoDecodeH265ReferenceInfoFlags) {
            .used_for_long_term_reference = pic->flags & HEVC_FRAME_FLAG_LONG_REF,
            .unused_for_reference = 0,
        },
        .PicOrderCntVal = pic->poc,
    };

    *vkh265_ref = (VkVideoDecodeH265DpbSlotInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_DPB_SLOT_INFO_KHR,
        .pStdReferenceInfo = h265_ref,
    };

    *ref = (VkVideoPictureResourceInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR,
        .codedOffset = (VkOffset2D){ 0, 0 },
        .codedExtent = (VkExtent2D){ pic->f->width, pic->f->height },
        .baseArrayLayer = ctx->common.layered_dpb ? pic_id : 0,
        .imageViewBinding = vkpic->view.ref[0],
    };

    *ref_slot = (VkVideoReferenceSlotInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR,
        .pNext = vkh265_ref,
        .slotIndex = pic_id,
        .pPictureResource = ref,
    };

    if (ref_src)
        *ref_src = pic;

    return 0;
}

static void copy_scaling_list(const ScalingList *sl, StdVideoH265ScalingLists *vksl)
{
    for (int i = 0; i < STD_VIDEO_H265_SCALING_LIST_4X4_NUM_LISTS; i++) {
        for (int j = 0; j < STD_VIDEO_H265_SCALING_LIST_4X4_NUM_ELEMENTS; j++) {
            uint8_t pos = 4 * ff_hevc_diag_scan4x4_y[j] + ff_hevc_diag_scan4x4_x[j];
            vksl->ScalingList4x4[i][j] = sl->sl[0][i][pos];
        }
    }

    for (int i = 0; i < STD_VIDEO_H265_SCALING_LIST_8X8_NUM_LISTS; i++) {
        for (int j = 0; j < STD_VIDEO_H265_SCALING_LIST_8X8_NUM_ELEMENTS; j++) {
            uint8_t pos = 8 * ff_hevc_diag_scan8x8_y[j] + ff_hevc_diag_scan8x8_x[j];
            vksl->ScalingList8x8[i][j] = sl->sl[1][i][pos];
        }
    }

    for (int i = 0; i < STD_VIDEO_H265_SCALING_LIST_16X16_NUM_LISTS; i++) {
        for (int j = 0; j < STD_VIDEO_H265_SCALING_LIST_16X16_NUM_ELEMENTS; j++) {
            uint8_t pos = 8 * ff_hevc_diag_scan8x8_y[j] + ff_hevc_diag_scan8x8_x[j];
            vksl->ScalingList16x16[i][j] = sl->sl[2][i][pos];
        }
    }

    for (int i = 0; i < STD_VIDEO_H265_SCALING_LIST_32X32_NUM_LISTS; i++) {
        for (int j = 0; j < STD_VIDEO_H265_SCALING_LIST_32X32_NUM_ELEMENTS; j++) {
            uint8_t pos = 8 * ff_hevc_diag_scan8x8_y[j] + ff_hevc_diag_scan8x8_x[j];
            vksl->ScalingList32x32[i][j] = sl->sl[3][i * 3][pos];
        }
    }

    memcpy(vksl->ScalingListDCCoef16x16, sl->sl_dc[0],
           STD_VIDEO_H265_SCALING_LIST_16X16_NUM_LISTS * sizeof(*vksl->ScalingListDCCoef16x16));

    for (int i = 0; i <  STD_VIDEO_H265_SCALING_LIST_32X32_NUM_LISTS; i++)
        vksl->ScalingListDCCoef32x32[i] = sl->sl_dc[1][i * 3];
}

static void set_sps(const HEVCSPS *sps, int sps_idx,
                    StdVideoH265ScalingLists *vksps_scaling,
                    StdVideoH265HrdParameters *vksps_vui_header,
                    StdVideoH265SequenceParameterSetVui *vksps_vui,
                    StdVideoH265SequenceParameterSet *vksps,
                    StdVideoH265SubLayerHrdParameters *slhdrnal,
                    StdVideoH265SubLayerHrdParameters *slhdrvcl,
                    StdVideoH265ProfileTierLevel *ptl,
                    StdVideoH265DecPicBufMgr *dpbm,
                    StdVideoH265PredictorPaletteEntries *pal,
                    StdVideoH265ShortTermRefPicSet *str,
                    StdVideoH265LongTermRefPicsSps *ltr)
{
    copy_scaling_list(&sps->scaling_list, vksps_scaling);

    *vksps_vui_header = (StdVideoH265HrdParameters) {
        .flags = (StdVideoH265HrdFlags) {
            .nal_hrd_parameters_present_flag = sps->hdr.nal_hrd_parameters_present_flag,
            .vcl_hrd_parameters_present_flag = sps->hdr.vcl_hrd_parameters_present_flag,
            .sub_pic_hrd_params_present_flag = sps->hdr.sub_pic_hrd_params_present_flag,
            .sub_pic_cpb_params_in_pic_timing_sei_flag = sps->hdr.sub_pic_cpb_params_in_pic_timing_sei_flag,
            .fixed_pic_rate_general_flag = sps->hdr.flags.fixed_pic_rate_general_flag,
            .fixed_pic_rate_within_cvs_flag = sps->hdr.flags.fixed_pic_rate_within_cvs_flag,
            .low_delay_hrd_flag = sps->hdr.flags.low_delay_hrd_flag,
        },
        .tick_divisor_minus2 = sps->hdr.tick_divisor_minus2,
        .du_cpb_removal_delay_increment_length_minus1 = sps->hdr.du_cpb_removal_delay_increment_length_minus1,
        .dpb_output_delay_du_length_minus1 = sps->hdr.dpb_output_delay_du_length_minus1,
        .bit_rate_scale = sps->hdr.bit_rate_scale,
        .cpb_size_scale = sps->hdr.cpb_size_scale,
        .cpb_size_du_scale = sps->hdr.cpb_size_du_scale,
        .initial_cpb_removal_delay_length_minus1 = sps->hdr.initial_cpb_removal_delay_length_minus1,
        .au_cpb_removal_delay_length_minus1 = sps->hdr.au_cpb_removal_delay_length_minus1,
        .dpb_output_delay_length_minus1 = sps->hdr.dpb_output_delay_length_minus1,
        /* Reserved - 3*16 bits */
        .pSubLayerHrdParametersNal = slhdrnal,
        .pSubLayerHrdParametersVcl = slhdrvcl,
    };

    memcpy(vksps_vui_header->cpb_cnt_minus1, sps->hdr.cpb_cnt_minus1,
           STD_VIDEO_H265_SUBLAYERS_LIST_SIZE*sizeof(*vksps_vui_header->cpb_cnt_minus1));
    memcpy(vksps_vui_header->elemental_duration_in_tc_minus1, sps->hdr.elemental_duration_in_tc_minus1,
           STD_VIDEO_H265_SUBLAYERS_LIST_SIZE*sizeof(*vksps_vui_header->elemental_duration_in_tc_minus1));

    memcpy(slhdrnal, sps->hdr.nal_params, HEVC_MAX_SUB_LAYERS*sizeof(*slhdrnal));
    memcpy(slhdrvcl, sps->hdr.vcl_params, HEVC_MAX_SUB_LAYERS*sizeof(*slhdrvcl));

    *vksps_vui = (StdVideoH265SequenceParameterSetVui) {
        .flags = (StdVideoH265SpsVuiFlags) {
            .aspect_ratio_info_present_flag = sps->vui.common.aspect_ratio_info_present_flag,
            .overscan_info_present_flag = sps->vui.common.overscan_info_present_flag,
            .overscan_appropriate_flag = sps->vui.common.overscan_appropriate_flag,
            .video_signal_type_present_flag = sps->vui.common.video_signal_type_present_flag,
            .video_full_range_flag = sps->vui.common.video_full_range_flag,
            .colour_description_present_flag = sps->vui.common.colour_description_present_flag,
            .chroma_loc_info_present_flag = sps->vui.common.chroma_loc_info_present_flag,
            .neutral_chroma_indication_flag = sps->vui.neutra_chroma_indication_flag,
            .field_seq_flag = sps->vui.field_seq_flag,
            .frame_field_info_present_flag = sps->vui.frame_field_info_present_flag,
            .default_display_window_flag = sps->vui.default_display_window_flag,
            .vui_timing_info_present_flag = sps->vui.vui_timing_info_present_flag,
            .vui_poc_proportional_to_timing_flag = sps->vui.vui_poc_proportional_to_timing_flag,
            .vui_hrd_parameters_present_flag = sps->vui.vui_hrd_parameters_present_flag,
            .bitstream_restriction_flag = sps->vui.bitstream_restriction_flag,
            .tiles_fixed_structure_flag = sps->vui.tiles_fixed_structure_flag,
            .motion_vectors_over_pic_boundaries_flag = sps->vui.motion_vectors_over_pic_boundaries_flag,
            .restricted_ref_pic_lists_flag = sps->vui.restricted_ref_pic_lists_flag,
        },
        .aspect_ratio_idc = sps->vui.common.aspect_ratio_idc,
        .sar_width = sps->vui.common.sar.num,
        .sar_height = sps->vui.common.sar.den,
        .video_format = sps->vui.common.video_format,
        .colour_primaries = sps->vui.common.colour_primaries,
        .transfer_characteristics = sps->vui.common.transfer_characteristics,
        .matrix_coeffs = sps->vui.common.matrix_coeffs,
        .chroma_sample_loc_type_top_field = sps->vui.common.chroma_sample_loc_type_top_field,
        .chroma_sample_loc_type_bottom_field = sps->vui.common.chroma_sample_loc_type_bottom_field,
        /* Reserved */
        /* Reserved */
        .def_disp_win_left_offset = sps->vui.def_disp_win.left_offset,
        .def_disp_win_right_offset = sps->vui.def_disp_win.right_offset,
        .def_disp_win_top_offset = sps->vui.def_disp_win.top_offset,
        .def_disp_win_bottom_offset = sps->vui.def_disp_win.bottom_offset,
        .vui_num_units_in_tick = sps->vui.vui_num_units_in_tick,
        .vui_time_scale = sps->vui.vui_time_scale,
        .vui_num_ticks_poc_diff_one_minus1 = sps->vui.vui_num_ticks_poc_diff_one_minus1,
        .min_spatial_segmentation_idc = sps->vui.min_spatial_segmentation_idc,
        .max_bytes_per_pic_denom = sps->vui.max_bytes_per_pic_denom,
        .max_bits_per_min_cu_denom = sps->vui.max_bits_per_min_cu_denom,
        .log2_max_mv_length_horizontal = sps->vui.log2_max_mv_length_horizontal,
        .log2_max_mv_length_vertical = sps->vui.log2_max_mv_length_vertical,
        .pHrdParameters = vksps_vui_header,
    };

    *ptl = (StdVideoH265ProfileTierLevel) {
        .flags = (StdVideoH265ProfileTierLevelFlags) {
            .general_tier_flag = sps->ptl.general_ptl.tier_flag,
            .general_progressive_source_flag = sps->ptl.general_ptl.progressive_source_flag,
            .general_interlaced_source_flag = sps->ptl.general_ptl.interlaced_source_flag,
            .general_non_packed_constraint_flag = sps->ptl.general_ptl.non_packed_constraint_flag,
            .general_frame_only_constraint_flag = sps->ptl.general_ptl.frame_only_constraint_flag,
        },
        .general_profile_idc = sps->ptl.general_ptl.profile_idc,
        .general_level_idc = ff_vk_h265_level_to_vk(sps->ptl.general_ptl.level_idc),
    };

    for (int i = 0; i < sps->max_sub_layers; i++) {
        dpbm->max_latency_increase_plus1[i] = sps->temporal_layer[i].max_latency_increase + 1;
        dpbm->max_dec_pic_buffering_minus1[i] = sps->temporal_layer[i].max_dec_pic_buffering - 1;
        dpbm->max_num_reorder_pics[i] = sps->temporal_layer[i].num_reorder_pics;
    }

    for (int i = 0; i < (sps->chroma_format_idc ? 3 : 1); i++)
        for (int j = 0; j < sps->sps_num_palette_predictor_initializers; j++)
            pal->PredictorPaletteEntries[i][j] = sps->sps_palette_predictor_initializer[i][j];

    for (int i = 0; i < sps->nb_st_rps; i++) {
        const ShortTermRPS *st_rps = &sps->st_rps[i];

        str[i] = (StdVideoH265ShortTermRefPicSet) {
            .flags = (StdVideoH265ShortTermRefPicSetFlags) {
                .inter_ref_pic_set_prediction_flag = sps->st_rps[i].rps_predict,
                .delta_rps_sign = sps->st_rps[i].delta_rps_sign,
            },
            .delta_idx_minus1 = sps->st_rps[i].delta_idx - 1,
            .use_delta_flag = sps->st_rps[i].use_delta,
            .abs_delta_rps_minus1 = sps->st_rps[i].abs_delta_rps - 1,
            .used_by_curr_pic_flag    = 0x0,
            .used_by_curr_pic_s0_flag = 0x0,
            .used_by_curr_pic_s1_flag = 0x0,
            /* Reserved */
            /* Reserved */
            /* Reserved */
            .num_negative_pics = sps->st_rps[i].num_negative_pics,
            .num_positive_pics = sps->st_rps[i].num_delta_pocs - sps->st_rps[i].num_negative_pics,
        };

        /* NOTE: This is the predicted, and *reordered* version.
         * Probably incorrect, but the spec doesn't say which version to use. */
        str[i].used_by_curr_pic_flag = st_rps->used;
        str[i].used_by_curr_pic_s0_flag = av_zero_extend(st_rps->used, str[i].num_negative_pics);
        str[i].used_by_curr_pic_s1_flag = st_rps->used >> str[i].num_negative_pics;

        for (int j = 0; j < str[i].num_negative_pics; j++)
            str[i].delta_poc_s0_minus1[j] = st_rps->delta_poc[j] - (j ? st_rps->delta_poc[j - 1] : 0) - 1;

        for (int j = 0; j < str[i].num_positive_pics; j++)
            str[i].delta_poc_s1_minus1[j] = st_rps->delta_poc[st_rps->num_negative_pics + j] -
                                            (j ? st_rps->delta_poc[st_rps->num_negative_pics + j - 1] : 0) - 1;
    }

    *ltr = (StdVideoH265LongTermRefPicsSps) {
        .used_by_curr_pic_lt_sps_flag = sps->used_by_curr_pic_lt,
    };

    for (int i = 0; i < sps->num_long_term_ref_pics_sps; i++) {
        ltr->lt_ref_pic_poc_lsb_sps[i]     = sps->lt_ref_pic_poc_lsb_sps[i];
    }

    *vksps = (StdVideoH265SequenceParameterSet) {
        .flags = (StdVideoH265SpsFlags) {
            .sps_temporal_id_nesting_flag = sps->temporal_id_nesting,
            .separate_colour_plane_flag = sps->separate_colour_plane,
            .conformance_window_flag = sps->conformance_window,
            .sps_sub_layer_ordering_info_present_flag = sps->sublayer_ordering_info,
            .scaling_list_enabled_flag = sps->scaling_list_enabled,
            .sps_scaling_list_data_present_flag = sps->scaling_list_enabled,
            .amp_enabled_flag = sps->amp_enabled,
            .sample_adaptive_offset_enabled_flag = sps->sao_enabled,
            .pcm_enabled_flag = sps->pcm_enabled,
            .pcm_loop_filter_disabled_flag = sps->pcm_loop_filter_disabled,
            .long_term_ref_pics_present_flag = sps->long_term_ref_pics_present,
            .sps_temporal_mvp_enabled_flag = sps->temporal_mvp_enabled,
            .strong_intra_smoothing_enabled_flag = sps->strong_intra_smoothing_enabled,
            .vui_parameters_present_flag = sps->vui_present,
            .sps_extension_present_flag = sps->extension_present,
            .sps_range_extension_flag = sps->range_extension,
            .transform_skip_rotation_enabled_flag = sps->transform_skip_rotation_enabled,
            .transform_skip_context_enabled_flag = sps->transform_skip_context_enabled,
            .implicit_rdpcm_enabled_flag = sps->implicit_rdpcm_enabled,
            .explicit_rdpcm_enabled_flag = sps->explicit_rdpcm_enabled,
            .extended_precision_processing_flag = sps->extended_precision_processing,
            .intra_smoothing_disabled_flag = sps->intra_smoothing_disabled,
            .high_precision_offsets_enabled_flag = sps->high_precision_offsets_enabled,
            .persistent_rice_adaptation_enabled_flag = sps->persistent_rice_adaptation_enabled,
            .cabac_bypass_alignment_enabled_flag = sps->cabac_bypass_alignment_enabled,
            .sps_scc_extension_flag = sps->scc_extension,
            .sps_curr_pic_ref_enabled_flag = sps->curr_pic_ref_enabled,
            .palette_mode_enabled_flag = sps->palette_mode_enabled,
            .sps_palette_predictor_initializers_present_flag = sps->palette_predictor_initializers_present,
            .intra_boundary_filtering_disabled_flag = sps->intra_boundary_filtering_disabled,
        },
        .chroma_format_idc = sps->chroma_format_idc,
        .pic_width_in_luma_samples = sps->width,
        .pic_height_in_luma_samples = sps->height,
        .sps_video_parameter_set_id = sps->vps_id,
        .sps_max_sub_layers_minus1 = sps->max_sub_layers - 1,
        .sps_seq_parameter_set_id = sps_idx,
        .bit_depth_luma_minus8 = sps->bit_depth - 8,
        .bit_depth_chroma_minus8 = sps->bit_depth_chroma - 8,
        .log2_max_pic_order_cnt_lsb_minus4 = sps->log2_max_poc_lsb - 4,
        .log2_min_luma_coding_block_size_minus3 = sps->log2_min_cb_size - 3,
        .log2_diff_max_min_luma_coding_block_size = sps->log2_diff_max_min_coding_block_size,
        .log2_min_luma_transform_block_size_minus2 = sps->log2_min_tb_size - 2,
        .log2_diff_max_min_luma_transform_block_size = sps->log2_diff_max_min_transform_block_size,
        .max_transform_hierarchy_depth_inter = sps->max_transform_hierarchy_depth_inter,
        .max_transform_hierarchy_depth_intra = sps->max_transform_hierarchy_depth_intra,
        .num_short_term_ref_pic_sets = sps->nb_st_rps,
        .num_long_term_ref_pics_sps = sps->num_long_term_ref_pics_sps,
        .pcm_sample_bit_depth_luma_minus1 = sps->pcm.bit_depth - 1,
        .pcm_sample_bit_depth_chroma_minus1 = sps->pcm.bit_depth_chroma - 1,
        .log2_min_pcm_luma_coding_block_size_minus3 = sps->pcm.log2_min_pcm_cb_size - 3,
        .log2_diff_max_min_pcm_luma_coding_block_size = sps->pcm.log2_max_pcm_cb_size - sps->pcm.log2_min_pcm_cb_size,
        /* Reserved */
        /* Reserved */
        .palette_max_size = sps->palette_max_size,
        .delta_palette_max_predictor_size = sps->delta_palette_max_predictor_size,
        .motion_vector_resolution_control_idc = sps->motion_vector_resolution_control_idc,
        .sps_num_palette_predictor_initializers_minus1 = sps->sps_num_palette_predictor_initializers - 1,
        .conf_win_left_offset = sps->pic_conf_win.left_offset,
        .conf_win_right_offset = sps->pic_conf_win.right_offset,
        .conf_win_top_offset = sps->pic_conf_win.top_offset,
        .conf_win_bottom_offset = sps->pic_conf_win.bottom_offset,
        .pProfileTierLevel = ptl,
        .pDecPicBufMgr = dpbm,
        .pScalingLists = vksps_scaling,
        .pShortTermRefPicSet = str,
        .pLongTermRefPicsSps = ltr,
        .pSequenceParameterSetVui = vksps_vui,
        .pPredictorPaletteEntries = pal,
    };
}

static void set_pps(const HEVCPPS *pps, const HEVCSPS *sps,
                    StdVideoH265ScalingLists *vkpps_scaling,
                    StdVideoH265PictureParameterSet *vkpps,
                    StdVideoH265PredictorPaletteEntries *pal)
{
    copy_scaling_list(&pps->scaling_list, vkpps_scaling);

    *vkpps = (StdVideoH265PictureParameterSet) {
        .flags = (StdVideoH265PpsFlags) {
            .dependent_slice_segments_enabled_flag = pps->dependent_slice_segments_enabled_flag,
            .output_flag_present_flag = pps->output_flag_present_flag,
            .sign_data_hiding_enabled_flag = pps->sign_data_hiding_flag,
            .cabac_init_present_flag = pps->cabac_init_present_flag,
            .constrained_intra_pred_flag = pps->constrained_intra_pred_flag,
            .transform_skip_enabled_flag = pps->transform_skip_enabled_flag,
            .cu_qp_delta_enabled_flag = pps->cu_qp_delta_enabled_flag,
            .pps_slice_chroma_qp_offsets_present_flag = pps->pic_slice_level_chroma_qp_offsets_present_flag,
            .weighted_pred_flag = pps->weighted_pred_flag,
            .weighted_bipred_flag = pps->weighted_bipred_flag,
            .transquant_bypass_enabled_flag = pps->transquant_bypass_enable_flag,
            .tiles_enabled_flag = pps->tiles_enabled_flag,
            .entropy_coding_sync_enabled_flag = pps->entropy_coding_sync_enabled_flag,
            .uniform_spacing_flag = pps->uniform_spacing_flag,
            .loop_filter_across_tiles_enabled_flag = pps->loop_filter_across_tiles_enabled_flag,
            .pps_loop_filter_across_slices_enabled_flag = pps->seq_loop_filter_across_slices_enabled_flag,
            .deblocking_filter_control_present_flag = pps->deblocking_filter_control_present_flag,
            .deblocking_filter_override_enabled_flag = pps->deblocking_filter_override_enabled_flag,
            .pps_deblocking_filter_disabled_flag = pps->disable_dbf,
            .pps_scaling_list_data_present_flag = pps->scaling_list_data_present_flag,
            .lists_modification_present_flag = pps->lists_modification_present_flag,
            .slice_segment_header_extension_present_flag = pps->slice_header_extension_present_flag,
            .pps_extension_present_flag = pps->pps_extension_present_flag,
            .cross_component_prediction_enabled_flag = pps->cross_component_prediction_enabled_flag,
            .chroma_qp_offset_list_enabled_flag = pps->chroma_qp_offset_list_enabled_flag,
            .pps_curr_pic_ref_enabled_flag = pps->pps_curr_pic_ref_enabled_flag,
            .residual_adaptive_colour_transform_enabled_flag = pps->residual_adaptive_colour_transform_enabled_flag,
            .pps_slice_act_qp_offsets_present_flag = pps->pps_slice_act_qp_offsets_present_flag,
            .pps_palette_predictor_initializers_present_flag = pps->pps_palette_predictor_initializers_present_flag,
            .monochrome_palette_flag = pps->monochrome_palette_flag,
            .pps_range_extension_flag = pps->pps_range_extensions_flag,
        },
        .pps_pic_parameter_set_id = pps->pps_id,
        .pps_seq_parameter_set_id = pps->sps_id,
        .sps_video_parameter_set_id = sps->vps_id,
        .num_extra_slice_header_bits = pps->num_extra_slice_header_bits,
        .num_ref_idx_l0_default_active_minus1 = pps->num_ref_idx_l0_default_active - 1,
        .num_ref_idx_l1_default_active_minus1 = pps->num_ref_idx_l1_default_active - 1,
        .init_qp_minus26 = pps->pic_init_qp_minus26,
        .diff_cu_qp_delta_depth = pps->diff_cu_qp_delta_depth,
        .pps_cb_qp_offset = pps->cb_qp_offset,
        .pps_cr_qp_offset = pps->cr_qp_offset,
        .pps_beta_offset_div2 = pps->beta_offset >> 1,
        .pps_tc_offset_div2 = pps->tc_offset >> 1,
        .log2_parallel_merge_level_minus2 = pps->log2_parallel_merge_level - 2,
        .log2_max_transform_skip_block_size_minus2 = pps->log2_max_transform_skip_block_size - 2,
        .diff_cu_chroma_qp_offset_depth = pps->diff_cu_chroma_qp_offset_depth,
        .chroma_qp_offset_list_len_minus1 = pps->chroma_qp_offset_list_len_minus1,
        .log2_sao_offset_scale_luma = pps->log2_sao_offset_scale_luma,
        .log2_sao_offset_scale_chroma = pps->log2_sao_offset_scale_chroma,
        .pps_act_y_qp_offset_plus5 = pps->pps_act_y_qp_offset + 5,
        .pps_act_cb_qp_offset_plus5 = pps->pps_act_cb_qp_offset + 5,
        .pps_act_cr_qp_offset_plus3 = pps->pps_act_cr_qp_offset + 3,
        .pps_num_palette_predictor_initializers = pps->pps_num_palette_predictor_initializers,
        .luma_bit_depth_entry_minus8 = pps->luma_bit_depth_entry - 8,
        .chroma_bit_depth_entry_minus8 = pps->chroma_bit_depth_entry - 8,
        .num_tile_columns_minus1 = pps->num_tile_columns - 1,
        .num_tile_rows_minus1 = pps->num_tile_rows - 1,
        .pScalingLists = vkpps_scaling,
        .pPredictorPaletteEntries = pal,
    };

    for (int i = 0; i < (pps->monochrome_palette_flag ? 1 : 3); i++) {
        for (int j = 0; j < pps->pps_num_palette_predictor_initializers; j++)
            pal->PredictorPaletteEntries[i][j] = pps->pps_palette_predictor_initializer[i][j];
    }

    for (int i = 0; i < pps->num_tile_columns - 1; i++)
        vkpps->column_width_minus1[i] = pps->column_width[i] - 1;

    for (int i = 0; i < pps->num_tile_rows - 1; i++)
        vkpps->row_height_minus1[i] = pps->row_height[i] - 1;

    for (int i = 0; i <= pps->chroma_qp_offset_list_len_minus1; i++) {
        vkpps->cb_qp_offset_list[i] = pps->cb_qp_offset_list[i];
        vkpps->cr_qp_offset_list[i] = pps->cr_qp_offset_list[i];
    }
}

static void set_vps(const HEVCVPS *vps,
                    StdVideoH265VideoParameterSet *vkvps,
                    StdVideoH265ProfileTierLevel *ptl,
                    StdVideoH265DecPicBufMgr *dpbm,
                    StdVideoH265HrdParameters *sls_hdr,
                    HEVCHeaderVPSSet sls[])
{
    for (int i = 0; i < vps->vps_num_hrd_parameters; i++) {
        const HEVCHdrParams *src = &vps->hdr[i];

        sls_hdr[i] = (StdVideoH265HrdParameters) {
            .flags = (StdVideoH265HrdFlags) {
                .nal_hrd_parameters_present_flag = src->nal_hrd_parameters_present_flag,
                .vcl_hrd_parameters_present_flag = src->vcl_hrd_parameters_present_flag,
                .sub_pic_hrd_params_present_flag = src->sub_pic_hrd_params_present_flag,
                .sub_pic_cpb_params_in_pic_timing_sei_flag = src->sub_pic_cpb_params_in_pic_timing_sei_flag,
                .fixed_pic_rate_general_flag = src->flags.fixed_pic_rate_general_flag,
                .fixed_pic_rate_within_cvs_flag = src->flags.fixed_pic_rate_within_cvs_flag,
                .low_delay_hrd_flag = src->flags.low_delay_hrd_flag,
            },
            .tick_divisor_minus2 = src->tick_divisor_minus2,
            .du_cpb_removal_delay_increment_length_minus1 = src->du_cpb_removal_delay_increment_length_minus1,
            .dpb_output_delay_du_length_minus1 = src->dpb_output_delay_du_length_minus1,
            .bit_rate_scale = src->bit_rate_scale,
            .cpb_size_scale = src->cpb_size_scale,
            .cpb_size_du_scale = src->cpb_size_du_scale,
            .initial_cpb_removal_delay_length_minus1 = src->initial_cpb_removal_delay_length_minus1,
            .au_cpb_removal_delay_length_minus1 = src->au_cpb_removal_delay_length_minus1,
            .dpb_output_delay_length_minus1 = src->dpb_output_delay_length_minus1,
            /* Reserved - 3*16 bits */
            .pSubLayerHrdParametersNal = sls[i].nal_hdr,
            .pSubLayerHrdParametersVcl = sls[i].vcl_hdr,
        };

        memcpy(sls_hdr[i].cpb_cnt_minus1, src->cpb_cnt_minus1,
               STD_VIDEO_H265_SUBLAYERS_LIST_SIZE*sizeof(*sls_hdr[i].cpb_cnt_minus1));
        memcpy(sls_hdr[i].elemental_duration_in_tc_minus1, src->elemental_duration_in_tc_minus1,
               STD_VIDEO_H265_SUBLAYERS_LIST_SIZE*sizeof(*sls_hdr[i].elemental_duration_in_tc_minus1));

        memcpy(sls[i].nal_hdr, src->nal_params, HEVC_MAX_SUB_LAYERS*sizeof(*sls[i].nal_hdr));
        memcpy(sls[i].vcl_hdr, src->vcl_params, HEVC_MAX_SUB_LAYERS*sizeof(*sls[i].vcl_hdr));
    }

    *ptl = (StdVideoH265ProfileTierLevel) {
        .flags = (StdVideoH265ProfileTierLevelFlags) {
            .general_tier_flag = vps->ptl.general_ptl.tier_flag,
            .general_progressive_source_flag = vps->ptl.general_ptl.progressive_source_flag,
            .general_interlaced_source_flag = vps->ptl.general_ptl.interlaced_source_flag,
            .general_non_packed_constraint_flag = vps->ptl.general_ptl.non_packed_constraint_flag,
            .general_frame_only_constraint_flag = vps->ptl.general_ptl.frame_only_constraint_flag,
        },
        .general_profile_idc = ff_vk_h265_profile_to_vk(vps->ptl.general_ptl.profile_idc),
        .general_level_idc = ff_vk_h265_level_to_vk(vps->ptl.general_ptl.level_idc),
    };

    for (int i = 0; i < vps->vps_max_sub_layers; i++) {
        dpbm->max_latency_increase_plus1[i] = vps->vps_max_latency_increase[i] + 1;
        dpbm->max_dec_pic_buffering_minus1[i] = vps->vps_max_dec_pic_buffering[i] - 1;
        dpbm->max_num_reorder_pics[i] = vps->vps_num_reorder_pics[i];
    }

    *vkvps = (StdVideoH265VideoParameterSet) {
        .flags = (StdVideoH265VpsFlags) {
            .vps_temporal_id_nesting_flag = vps->vps_temporal_id_nesting_flag,
            .vps_sub_layer_ordering_info_present_flag = vps->vps_sub_layer_ordering_info_present_flag,
            .vps_timing_info_present_flag = vps->vps_timing_info_present_flag,
            .vps_poc_proportional_to_timing_flag = vps->vps_poc_proportional_to_timing_flag,
        },
        .vps_video_parameter_set_id = vps->vps_id,
        .vps_max_sub_layers_minus1 = vps->vps_max_sub_layers - 1,
        /* Reserved */
        /* Reserved */
        .vps_num_units_in_tick = vps->vps_num_units_in_tick,
        .vps_time_scale = vps->vps_time_scale,
        .vps_num_ticks_poc_diff_one_minus1 = vps->vps_num_ticks_poc_diff_one - 1,
        /* Reserved */
        .pDecPicBufMgr = dpbm,
        .pHrdParameters = sls_hdr,
        .pProfileTierLevel = ptl,
    };
}

static int vk_hevc_create_params(AVCodecContext *avctx, AVBufferRef **buf)
{
    int err;
    const HEVCContext *h = avctx->priv_data;
    FFVulkanDecodeContext *dec = avctx->internal->hwaccel_priv_data;
    FFVulkanDecodeShared *ctx = dec->shared_ctx;

    VkVideoDecodeH265SessionParametersAddInfoKHR h265_params_info = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_SESSION_PARAMETERS_ADD_INFO_KHR,
        .stdSPSCount = 0,
        .stdPPSCount = 0,
        .stdVPSCount = 0,
    };
    VkVideoDecodeH265SessionParametersCreateInfoKHR h265_params = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_SESSION_PARAMETERS_CREATE_INFO_KHR,
        .pParametersAddInfo = &h265_params_info,
    };
    VkVideoSessionParametersCreateInfoKHR session_params_create = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_PARAMETERS_CREATE_INFO_KHR,
        .pNext = &h265_params,
        .videoSession = ctx->common.session,
        .videoSessionParametersTemplate = VK_NULL_HANDLE,
    };

    HEVCHeaderSet *hdr;
    int nb_vps = 0;
    int vps_list_idx[HEVC_MAX_VPS_COUNT];

    for (int i = 0; i < HEVC_MAX_VPS_COUNT; i++)
        if (h->ps.vps_list[i])
            vps_list_idx[nb_vps++] = i;

    err = alloc_hevc_header_structs(dec, nb_vps, vps_list_idx, h->ps.vps_list);
    if (err < 0)
        return err;

    hdr = dec->hevc_headers;

    h265_params_info.pStdSPSs = hdr->sps;
    h265_params_info.pStdPPSs = hdr->pps;
    h265_params_info.pStdVPSs = hdr->vps;

    /* SPS list */
    for (int i = 0; i < HEVC_MAX_SPS_COUNT; i++) {
        if (h->ps.sps_list[i]) {
            const HEVCSPS *sps_l = h->ps.sps_list[i];
            int idx = h265_params_info.stdSPSCount++;
            set_sps(sps_l, i, &hdr->hsps[idx].scaling, &hdr->hsps[idx].vui_header,
                    &hdr->hsps[idx].vui, &hdr->sps[idx], hdr->hsps[idx].nal_hdr,
                    hdr->hsps[idx].vcl_hdr, &hdr->hsps[idx].ptl, &hdr->hsps[idx].dpbm,
                    &hdr->hsps[idx].pal, hdr->hsps[idx].str, &hdr->hsps[idx].ltr);
        }
    }

    /* PPS list */
    for (int i = 0; i < HEVC_MAX_PPS_COUNT; i++) {
        if (h->ps.pps_list[i]) {
            const HEVCPPS *pps_l = h->ps.pps_list[i];
            const HEVCSPS *sps_l = h->ps.sps_list[pps_l->sps_id];
            int idx = h265_params_info.stdPPSCount++;
            set_pps(pps_l, sps_l, &hdr->hpps[idx].scaling,
                    &hdr->pps[idx], &hdr->hpps[idx].pal);
        }
    }

    /* VPS list */
    for (int i = 0; i < nb_vps; i++) {
        const HEVCVPS *vps_l = h->ps.vps_list[vps_list_idx[i]];
        set_vps(vps_l, &hdr->vps[i], &hdr->hvps[i].ptl, &hdr->hvps[i].dpbm,
                hdr->hvps[i].hdr, hdr->hvps[i].sls);
        h265_params_info.stdVPSCount++;
    }

    h265_params.maxStdSPSCount = h265_params_info.stdSPSCount;
    h265_params.maxStdPPSCount = h265_params_info.stdPPSCount;
    h265_params.maxStdVPSCount = h265_params_info.stdVPSCount;

    err = ff_vk_decode_create_params(buf, avctx, ctx, &session_params_create);
    if (err < 0)
        return err;

    av_log(avctx, AV_LOG_DEBUG, "Created frame parameters: %i SPS %i PPS %i VPS\n",
           h265_params_info.stdSPSCount, h265_params_info.stdPPSCount,
           h265_params_info.stdVPSCount);

    return 0;
}

static int vk_hevc_start_frame(AVCodecContext          *avctx,
                               av_unused const AVBufferRef *buffer_ref,
                               av_unused const uint8_t *buffer,
                               av_unused uint32_t       size)
{
    int err;
    HEVCContext *h = avctx->priv_data;
    HEVCLayerContext *l = &h->layers[h->cur_layer];

    HEVCFrame *pic = h->cur_frame;
    HEVCVulkanDecodePicture *hp = pic->hwaccel_picture_private;
    FFVulkanDecodePicture *vp = &hp->vp;
    const HEVCPPS *pps = h->pps;
    const HEVCSPS *sps = pps->sps;
    int nb_refs = 0;

    hp->h265pic = (StdVideoDecodeH265PictureInfo) {
        .flags = (StdVideoDecodeH265PictureInfoFlags) {
            .IrapPicFlag = IS_IRAP(h),
            .IdrPicFlag = IS_IDR(h),
            .IsReference = h->nal_unit_type < 16 ? h->nal_unit_type & 1 : 1,
            .short_term_ref_pic_set_sps_flag = h->sh.short_term_ref_pic_set_sps_flag,
        },
        .sps_video_parameter_set_id = sps->vps_id,
        .pps_seq_parameter_set_id = pps->sps_id,
        .pps_pic_parameter_set_id = pps->pps_id,
        .NumDeltaPocsOfRefRpsIdx = h->sh.short_term_rps ? h->sh.short_term_rps->rps_idx_num_delta_pocs : 0,
        .PicOrderCntVal = h->poc,
        .NumBitsForSTRefPicSetInSlice = !h->sh.short_term_ref_pic_set_sps_flag ?
                                         h->sh.short_term_ref_pic_set_size : 0,
    };

    /* Fill in references */
    for (int i = 0; i < FF_ARRAY_ELEMS(l->DPB); i++) {
        const HEVCFrame *ref = &l->DPB[i];
        int idx = nb_refs;

        if (!(ref->flags & (HEVC_FRAME_FLAG_SHORT_REF | HEVC_FRAME_FLAG_LONG_REF)))
            continue;

        if (ref == pic) {
            err = vk_hevc_fill_pict(avctx, NULL, &vp->ref_slot, &vp->ref,
                                    &hp->vkh265_ref, &hp->h265_ref, pic, 1, i);
            if (err < 0)
                return err;

            continue;
        }

        err = vk_hevc_fill_pict(avctx, &hp->ref_src[idx], &vp->ref_slots[idx],
                                &vp->refs[idx], &hp->vkh265_refs[idx],
                                &hp->h265_refs[idx], (HEVCFrame *)ref, 0, i);
        if (err < 0)
            return err;

        nb_refs++;
    }

    memset(hp->h265pic.RefPicSetStCurrBefore, 0xff, 8);
    for (int i = 0; i < h->rps[ST_CURR_BEF].nb_refs; i++) {
        HEVCFrame *frame = h->rps[ST_CURR_BEF].ref[i];
        for (int j = 0; j < FF_ARRAY_ELEMS(l->DPB); j++) {
            const HEVCFrame *ref = &l->DPB[j];
            if (ref == frame) {
                hp->h265pic.RefPicSetStCurrBefore[i] = j;
                break;
            }
        }
    }
    memset(hp->h265pic.RefPicSetStCurrAfter, 0xff, 8);
    for (int i = 0; i < h->rps[ST_CURR_AFT].nb_refs; i++) {
        HEVCFrame *frame = h->rps[ST_CURR_AFT].ref[i];
        for (int j = 0; j < FF_ARRAY_ELEMS(l->DPB); j++) {
            const HEVCFrame *ref = &l->DPB[j];
            if (ref == frame) {
                hp->h265pic.RefPicSetStCurrAfter[i] = j;
                break;
            }
        }
    }
    memset(hp->h265pic.RefPicSetLtCurr, 0xff, 8);
    for (int i = 0; i < h->rps[LT_CURR].nb_refs; i++) {
        HEVCFrame *frame = h->rps[LT_CURR].ref[i];
        for (int j = 0; j < FF_ARRAY_ELEMS(l->DPB); j++) {
            const HEVCFrame *ref = &l->DPB[j];
            if (ref == frame) {
                hp->h265pic.RefPicSetLtCurr[i] = j;
                break;
            }
        }
    }

    hp->h265_pic_info = (VkVideoDecodeH265PictureInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_PICTURE_INFO_KHR,
        .pStdPictureInfo = &hp->h265pic,
        .sliceSegmentCount = 0,
    };

    vp->decode_info = (VkVideoDecodeInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_INFO_KHR,
        .pNext = &hp->h265_pic_info,
        .flags = 0x0,
        .pSetupReferenceSlot = &vp->ref_slot,
        .referenceSlotCount = nb_refs,
        .pReferenceSlots = vp->ref_slots,
        .dstPictureResource = (VkVideoPictureResourceInfoKHR) {
            .sType = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR,
            .codedOffset = (VkOffset2D){ 0, 0 },
            .codedExtent = (VkExtent2D){ pic->f->width, pic->f->height },
            .baseArrayLayer = 0,
            .imageViewBinding = vp->view.out[0],
        },
    };

    return 0;
}

static int vk_hevc_decode_slice(AVCodecContext *avctx,
                                const uint8_t  *data,
                                uint32_t        size)
{
    const HEVCContext *h = avctx->priv_data;
    HEVCVulkanDecodePicture *hp = h->cur_frame->hwaccel_picture_private;
    FFVulkanDecodePicture *vp = &hp->vp;

    int err = ff_vk_decode_add_slice(avctx, vp, data, size, 1,
                                     &hp->h265_pic_info.sliceSegmentCount,
                                     &hp->h265_pic_info.pSliceSegmentOffsets);
    if (err < 0)
        return err;

    return 0;
}

static int vk_hevc_end_frame(AVCodecContext *avctx)
{
    const HEVCContext *h = avctx->priv_data;
    FFVulkanDecodeContext *dec = avctx->internal->hwaccel_priv_data;
    FFVulkanDecodeShared *ctx = dec->shared_ctx;

    HEVCFrame *pic = h->cur_frame;
    HEVCVulkanDecodePicture *hp = pic->hwaccel_picture_private;
    FFVulkanDecodePicture *vp = &hp->vp;
    FFVulkanDecodePicture *rvp[HEVC_MAX_REFS] = { 0 };
    AVFrame *rav[HEVC_MAX_REFS] = { 0 };
    int err;

    const HEVCPPS *pps = h->pps;
    const HEVCSPS *sps = pps->sps;

#ifdef VK_KHR_video_maintenance2
    HEVCHeaderPPS vkpps_p;
    StdVideoH265PictureParameterSet vkpps;
    HEVCHeaderSPS vksps_p;
    StdVideoH265SequenceParameterSet vksps;
    HEVCHeaderVPSSet vkvps_ps[HEVC_MAX_SUB_LAYERS];
    HEVCHeaderVPS vkvps_p;
    StdVideoH265VideoParameterSet vkvps;
    VkVideoDecodeH265InlineSessionParametersInfoKHR h265_params;

    if (ctx->s.extensions & FF_VK_EXT_VIDEO_MAINTENANCE_2) {
        set_pps(pps, sps, &vkpps_p.scaling, &vkpps, &vkpps_p.pal);
        set_sps(sps, pps->sps_id, &vksps_p.scaling, &vksps_p.vui_header,
                &vksps_p.vui, &vksps, vksps_p.nal_hdr,
                vksps_p.vcl_hdr, &vksps_p.ptl, &vksps_p.dpbm,
                &vksps_p.pal, vksps_p.str, &vksps_p.ltr);

        vkvps_p.sls = vkvps_ps;
        set_vps(sps->vps, &vkvps, &vkvps_p.ptl, &vkvps_p.dpbm,
                vkvps_p.hdr, vkvps_p.sls);

        h265_params = (VkVideoDecodeH265InlineSessionParametersInfoKHR) {
            .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_INLINE_SESSION_PARAMETERS_INFO_KHR,
            .pStdSPS = &vksps,
            .pStdPPS = &vkpps,
            .pStdVPS = &vkvps,
        };
        hp->h265_pic_info.pNext = &h265_params;
    }
#endif

    if (!hp->h265_pic_info.sliceSegmentCount)
        return 0;

    if (!dec->session_params &&
        !(ctx->s.extensions & FF_VK_EXT_VIDEO_MAINTENANCE_2)) {
        if (!pps) {
            unsigned int pps_id = h->sh.pps_id;
            if (pps_id < HEVC_MAX_PPS_COUNT && h->ps.pps_list[pps_id] != NULL)
                pps = h->ps.pps_list[pps_id];
        }

        if (!pps) {
            av_log(avctx, AV_LOG_ERROR,
                   "Encountered frame without a valid active PPS reference.\n");
            return AVERROR_INVALIDDATA;
        }

        err = vk_hevc_create_params(avctx, &dec->session_params);
        if (err < 0)
            return err;

        hp->h265pic.sps_video_parameter_set_id = sps->vps_id;
        hp->h265pic.pps_seq_parameter_set_id = pps->sps_id;
        hp->h265pic.pps_pic_parameter_set_id = pps->pps_id;
    }

    for (int i = 0; i < vp->decode_info.referenceSlotCount; i++) {
        HEVCVulkanDecodePicture *rfhp = hp->ref_src[i]->hwaccel_picture_private;
        rav[i] = hp->ref_src[i]->f;
        rvp[i] = &rfhp->vp;
    }

    av_log(avctx, AV_LOG_DEBUG, "Decoding frame, %"SIZE_SPECIFIER" bytes, %i slices\n",
           vp->slices_size, hp->h265_pic_info.sliceSegmentCount);

    return ff_vk_decode_frame(avctx, pic->f, vp, rav, rvp);
}

static void vk_hevc_free_frame_priv(AVRefStructOpaque _hwctx, void *data)
{
    AVHWDeviceContext *hwctx = _hwctx.nc;
    HEVCVulkanDecodePicture *hp = data;

    /* Free frame resources */
    ff_vk_decode_free_frame(hwctx, &hp->vp);
}

const FFHWAccel ff_hevc_vulkan_hwaccel = {
    .p.name                = "hevc_vulkan",
    .p.type                = AVMEDIA_TYPE_VIDEO,
    .p.id                  = AV_CODEC_ID_HEVC,
    .p.pix_fmt             = AV_PIX_FMT_VULKAN,
    .start_frame           = &vk_hevc_start_frame,
    .decode_slice          = &vk_hevc_decode_slice,
    .end_frame             = &vk_hevc_end_frame,
    .free_frame_priv       = &vk_hevc_free_frame_priv,
    .frame_priv_data_size  = sizeof(HEVCVulkanDecodePicture),
    .init                  = &ff_vk_decode_init,
    .update_thread_context = &ff_vk_update_thread_context,
    .decode_params         = &ff_vk_params_invalidate,
    .flush                 = &ff_vk_decode_flush,
    .uninit                = &ff_vk_decode_uninit,
    .frame_params          = &ff_vk_frame_params,
    .priv_data_size        = sizeof(FFVulkanDecodeContext),
    .caps_internal         = HWACCEL_CAP_ASYNC_SAFE | HWACCEL_CAP_THREAD_SAFE,
};
