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

#include "h264dec.h"
#include "h264_ps.h"

#include "vulkan_decode.h"

const FFVulkanDecodeDescriptor ff_vk_dec_h264_desc = {
    .codec_id         = AV_CODEC_ID_H264,
    .decode_extension = FF_VK_EXT_VIDEO_DECODE_H264,
    .decode_op        = VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR,
    .ext_props = {
        .extensionName = VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_EXTENSION_NAME,
        .specVersion   = VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_SPEC_VERSION,
    },
};

typedef struct H264VulkanDecodePicture {
    FFVulkanDecodePicture           vp;

    /* Current picture */
    StdVideoDecodeH264ReferenceInfo h264_ref;
    VkVideoDecodeH264DpbSlotInfoKHR vkh264_ref;

    /* Picture refs */
    H264Picture                    *ref_src    [H264_MAX_PICTURE_COUNT];
    StdVideoDecodeH264ReferenceInfo h264_refs  [H264_MAX_PICTURE_COUNT];
    VkVideoDecodeH264DpbSlotInfoKHR vkh264_refs[H264_MAX_PICTURE_COUNT];

    /* Current picture (contd.) */
    StdVideoDecodeH264PictureInfo   h264pic;
    VkVideoDecodeH264PictureInfoKHR h264_pic_info;
} H264VulkanDecodePicture;

const static int h264_scaling_list8_order[] = { 0, 3, 1, 4, 2, 5 };

static int vk_h264_fill_pict(AVCodecContext *avctx, H264Picture **ref_src,
                             VkVideoReferenceSlotInfoKHR *ref_slot,       /* Main structure */
                             VkVideoPictureResourceInfoKHR *ref,          /* Goes in ^ */
                             VkVideoDecodeH264DpbSlotInfoKHR *vkh264_ref, /* Goes in ^ */
                             StdVideoDecodeH264ReferenceInfo *h264_ref,   /* Goes in ^ */
                             H264Picture *pic, int is_current,
                             int is_field, int picture_structure,
                             int dpb_slot_index)
{
    FFVulkanDecodeContext *dec = avctx->internal->hwaccel_priv_data;
    FFVulkanDecodeShared *ctx = dec->shared_ctx;
    H264VulkanDecodePicture *hp = pic->hwaccel_picture_private;
    FFVulkanDecodePicture *vkpic = &hp->vp;

    int err = ff_vk_decode_prepare_frame(dec, pic->f, vkpic, is_current,
                                         dec->dedicated_dpb);
    if (err < 0)
        return err;

    *h264_ref = (StdVideoDecodeH264ReferenceInfo) {
        .FrameNum = pic->long_ref ? pic->pic_id : pic->frame_num,
        .PicOrderCnt = { pic->field_poc[0], pic->field_poc[1] },
        .flags = (StdVideoDecodeH264ReferenceInfoFlags) {
            .top_field_flag    = is_field ? !!(picture_structure & PICT_TOP_FIELD)    : 0,
            .bottom_field_flag = is_field ? !!(picture_structure & PICT_BOTTOM_FIELD) : 0,
            .used_for_long_term_reference = pic->reference && pic->long_ref,
            /*
             * flags.is_non_existing is used to indicate whether the picture is marked as
             * “non-existing” as defined in section 8.2.5.2 of the ITU-T H.264 Specification;
             * 8.2.5.2 Decoding process for gaps in frame_num
             * corresponds to the code in h264_slice.c:h264_field_start,
             * which sets the invalid_gap flag when decoding.
             */
            .is_non_existing = pic->invalid_gap,
        },
    };

    *vkh264_ref = (VkVideoDecodeH264DpbSlotInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_DPB_SLOT_INFO_KHR,
        .pStdReferenceInfo = h264_ref,
    };

    *ref = (VkVideoPictureResourceInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR,
        .codedOffset = (VkOffset2D){ 0, 0 },
        .codedExtent = (VkExtent2D){ pic->f->width, pic->f->height },
        .baseArrayLayer = ctx->common.layered_dpb ? dpb_slot_index : 0,
        .imageViewBinding = vkpic->img_view_ref,
    };

    *ref_slot = (VkVideoReferenceSlotInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR,
        .pNext = vkh264_ref,
        .slotIndex = dpb_slot_index,
        .pPictureResource = ref,
    };

    if (ref_src)
        *ref_src = pic;

    return 0;
}

static StdVideoH264LevelIdc convert_to_vk_level_idc(int level_idc)
{
    switch (level_idc) {
    case 10: return STD_VIDEO_H264_LEVEL_IDC_1_0;
    case 11: return STD_VIDEO_H264_LEVEL_IDC_1_1;
    case 12: return STD_VIDEO_H264_LEVEL_IDC_1_2;
    case 13: return STD_VIDEO_H264_LEVEL_IDC_1_3;
    case 20: return STD_VIDEO_H264_LEVEL_IDC_2_0;
    case 21: return STD_VIDEO_H264_LEVEL_IDC_2_1;
    case 22: return STD_VIDEO_H264_LEVEL_IDC_2_2;
    case 30: return STD_VIDEO_H264_LEVEL_IDC_3_0;
    case 31: return STD_VIDEO_H264_LEVEL_IDC_3_1;
    case 32: return STD_VIDEO_H264_LEVEL_IDC_3_2;
    case 40: return STD_VIDEO_H264_LEVEL_IDC_4_0;
    case 41: return STD_VIDEO_H264_LEVEL_IDC_4_1;
    case 42: return STD_VIDEO_H264_LEVEL_IDC_4_2;
    case 50: return STD_VIDEO_H264_LEVEL_IDC_5_0;
    case 51: return STD_VIDEO_H264_LEVEL_IDC_5_1;
    case 52: return STD_VIDEO_H264_LEVEL_IDC_5_2;
    case 60: return STD_VIDEO_H264_LEVEL_IDC_6_0;
    case 61: return STD_VIDEO_H264_LEVEL_IDC_6_1;
    default:
    case 62: return STD_VIDEO_H264_LEVEL_IDC_6_2;
    }
}

static void set_sps(const SPS *sps,
                    StdVideoH264ScalingLists *vksps_scaling,
                    StdVideoH264HrdParameters *vksps_vui_header,
                    StdVideoH264SequenceParameterSetVui *vksps_vui,
                    StdVideoH264SequenceParameterSet *vksps)
{
    *vksps_scaling = (StdVideoH264ScalingLists) {
        .scaling_list_present_mask = sps->scaling_matrix_present_mask,
        .use_default_scaling_matrix_mask = 0, /* We already fill in the default matrix */
    };

    for (int i = 0; i < STD_VIDEO_H264_SCALING_LIST_4X4_NUM_LISTS; i++)
        for (int j = 0; j < STD_VIDEO_H264_SCALING_LIST_4X4_NUM_ELEMENTS; j++)
            vksps_scaling->ScalingList4x4[i][j] = sps->scaling_matrix4[i][ff_zigzag_scan[j]];

    for (int i = 0; i < STD_VIDEO_H264_SCALING_LIST_8X8_NUM_LISTS; i++)
        for (int j = 0; j < STD_VIDEO_H264_SCALING_LIST_8X8_NUM_ELEMENTS; j++)
            vksps_scaling->ScalingList8x8[i][j] =
                sps->scaling_matrix8[h264_scaling_list8_order[i]][ff_zigzag_direct[j]];

    *vksps_vui_header = (StdVideoH264HrdParameters) {
        .cpb_cnt_minus1 = sps->cpb_cnt - 1,
        .bit_rate_scale = sps->bit_rate_scale,
        .initial_cpb_removal_delay_length_minus1 = sps->initial_cpb_removal_delay_length - 1,
        .cpb_removal_delay_length_minus1 = sps->cpb_removal_delay_length - 1,
        .dpb_output_delay_length_minus1 = sps->dpb_output_delay_length - 1,
        .time_offset_length = sps->time_offset_length,
    };

    for (int i = 0; i < sps->cpb_cnt; i++) {
        vksps_vui_header->bit_rate_value_minus1[i] = sps->bit_rate_value[i] - 1;
        vksps_vui_header->cpb_size_value_minus1[i] = sps->cpb_size_value[i] - 1;
        vksps_vui_header->cbr_flag[i] = (sps->cpr_flag >> i) & 0x1;
    }

    *vksps_vui = (StdVideoH264SequenceParameterSetVui) {
        .aspect_ratio_idc = sps->vui.aspect_ratio_idc,
        .sar_width = sps->vui.sar.num,
        .sar_height = sps->vui.sar.den,
        .video_format = sps->vui.video_format,
        .colour_primaries = sps->vui.colour_primaries,
        .transfer_characteristics = sps->vui.transfer_characteristics,
        .matrix_coefficients = sps->vui.matrix_coeffs,
        .num_units_in_tick = sps->num_units_in_tick,
        .time_scale = sps->time_scale,
        .pHrdParameters = vksps_vui_header,
        .max_num_reorder_frames = sps->num_reorder_frames,
        .max_dec_frame_buffering = sps->max_dec_frame_buffering,
        .flags = (StdVideoH264SpsVuiFlags) {
            .aspect_ratio_info_present_flag = sps->vui.aspect_ratio_info_present_flag,
            .overscan_info_present_flag = sps->vui.overscan_info_present_flag,
            .overscan_appropriate_flag = sps->vui.overscan_appropriate_flag,
            .video_signal_type_present_flag = sps->vui.video_signal_type_present_flag,
            .video_full_range_flag = sps->vui.video_full_range_flag,
            .color_description_present_flag = sps->vui.colour_description_present_flag,
            .chroma_loc_info_present_flag = sps->vui.chroma_location,
            .timing_info_present_flag = sps->timing_info_present_flag,
            .fixed_frame_rate_flag = sps->fixed_frame_rate_flag,
            .bitstream_restriction_flag = sps->bitstream_restriction_flag,
            .nal_hrd_parameters_present_flag = sps->nal_hrd_parameters_present_flag,
            .vcl_hrd_parameters_present_flag = sps->vcl_hrd_parameters_present_flag,
        },
    };

    *vksps = (StdVideoH264SequenceParameterSet) {
        .profile_idc = sps->profile_idc,
        .level_idc = convert_to_vk_level_idc(sps->level_idc),
        .seq_parameter_set_id = sps->sps_id,
        .chroma_format_idc = sps->chroma_format_idc,
        .bit_depth_luma_minus8 = sps->bit_depth_luma - 8,
        .bit_depth_chroma_minus8 = sps->bit_depth_chroma - 8,
        .log2_max_frame_num_minus4 = sps->log2_max_frame_num - 4,
        .pic_order_cnt_type = sps->poc_type,
        .log2_max_pic_order_cnt_lsb_minus4 = sps->poc_type ? 0 : sps->log2_max_poc_lsb - 4,
        .offset_for_non_ref_pic = sps->offset_for_non_ref_pic,
        .offset_for_top_to_bottom_field = sps->offset_for_top_to_bottom_field,
        .num_ref_frames_in_pic_order_cnt_cycle = sps->poc_cycle_length,
        .max_num_ref_frames = sps->ref_frame_count,
        .pic_width_in_mbs_minus1 = sps->mb_width - 1,
        .pic_height_in_map_units_minus1 = (sps->mb_height/(2 - sps->frame_mbs_only_flag)) - 1,
        .frame_crop_left_offset = sps->crop_left,
        .frame_crop_right_offset = sps->crop_right,
        .frame_crop_top_offset = sps->crop_top,
        .frame_crop_bottom_offset = sps->crop_bottom,
        .flags = (StdVideoH264SpsFlags) {
            .constraint_set0_flag = (sps->constraint_set_flags >> 0) & 0x1,
            .constraint_set1_flag = (sps->constraint_set_flags >> 1) & 0x1,
            .constraint_set2_flag = (sps->constraint_set_flags >> 2) & 0x1,
            .constraint_set3_flag = (sps->constraint_set_flags >> 3) & 0x1,
            .constraint_set4_flag = (sps->constraint_set_flags >> 4) & 0x1,
            .constraint_set5_flag = (sps->constraint_set_flags >> 5) & 0x1,
            .direct_8x8_inference_flag = sps->direct_8x8_inference_flag,
            .mb_adaptive_frame_field_flag = sps->mb_aff,
            .frame_mbs_only_flag = sps->frame_mbs_only_flag,
            .delta_pic_order_always_zero_flag = sps->delta_pic_order_always_zero_flag,
            .separate_colour_plane_flag = sps->residual_color_transform_flag,
            .gaps_in_frame_num_value_allowed_flag = sps->gaps_in_frame_num_allowed_flag,
            .qpprime_y_zero_transform_bypass_flag = sps->transform_bypass,
            .frame_cropping_flag = sps->crop,
            .seq_scaling_matrix_present_flag = sps->scaling_matrix_present,
            .vui_parameters_present_flag = sps->vui_parameters_present_flag,
        },
        .pOffsetForRefFrame = sps->offset_for_ref_frame,
        .pScalingLists = vksps_scaling,
        .pSequenceParameterSetVui = vksps_vui,
    };
}

static void set_pps(const PPS *pps, const SPS *sps,
                    StdVideoH264ScalingLists *vkpps_scaling,
                    StdVideoH264PictureParameterSet *vkpps)
{
    *vkpps_scaling = (StdVideoH264ScalingLists) {
        .scaling_list_present_mask = pps->pic_scaling_matrix_present_mask,
        .use_default_scaling_matrix_mask = 0, /* We already fill in the default matrix */
    };

    for (int i = 0; i < STD_VIDEO_H264_SCALING_LIST_4X4_NUM_LISTS; i++)
        for (int j = 0; j < STD_VIDEO_H264_SCALING_LIST_4X4_NUM_ELEMENTS; j++)
            vkpps_scaling->ScalingList4x4[i][j] = pps->scaling_matrix4[i][ff_zigzag_scan[j]];

    for (int i = 0; i < STD_VIDEO_H264_SCALING_LIST_8X8_NUM_LISTS; i++)
        for (int j = 0; j < STD_VIDEO_H264_SCALING_LIST_8X8_NUM_ELEMENTS; j++)
            vkpps_scaling->ScalingList8x8[i][j] =
                pps->scaling_matrix8[h264_scaling_list8_order[i]][ff_zigzag_direct[j]];

    *vkpps = (StdVideoH264PictureParameterSet) {
        .seq_parameter_set_id = pps->sps_id,
        .pic_parameter_set_id = pps->pps_id,
        .num_ref_idx_l0_default_active_minus1 = pps->ref_count[0] - 1,
        .num_ref_idx_l1_default_active_minus1 = pps->ref_count[1] - 1,
        .weighted_bipred_idc = pps->weighted_bipred_idc,
        .pic_init_qp_minus26 = pps->init_qp - 26,
        .pic_init_qs_minus26 = pps->init_qs - 26,
        .chroma_qp_index_offset = pps->chroma_qp_index_offset[0],
        .second_chroma_qp_index_offset = pps->chroma_qp_index_offset[1],
        .flags = (StdVideoH264PpsFlags) {
            .transform_8x8_mode_flag = pps->transform_8x8_mode,
            .redundant_pic_cnt_present_flag = pps->redundant_pic_cnt_present,
            .constrained_intra_pred_flag = pps->constrained_intra_pred,
            .deblocking_filter_control_present_flag = pps->deblocking_filter_parameters_present,
            .weighted_pred_flag = pps->weighted_pred,
            .bottom_field_pic_order_in_frame_present_flag = pps->pic_order_present,
            .entropy_coding_mode_flag = pps->cabac,
            .pic_scaling_matrix_present_flag = pps->pic_scaling_matrix_present_flag,
        },
        .pScalingLists = vkpps_scaling,
    };
}

static int vk_h264_create_params(AVCodecContext *avctx, AVBufferRef **buf)
{
    int err;
    FFVulkanDecodeContext *dec = avctx->internal->hwaccel_priv_data;
    FFVulkanDecodeShared *ctx = dec->shared_ctx;
    const H264Context *h = avctx->priv_data;

    /* SPS */
    StdVideoH264ScalingLists vksps_scaling[MAX_SPS_COUNT];
    StdVideoH264HrdParameters vksps_vui_header[MAX_SPS_COUNT];
    StdVideoH264SequenceParameterSetVui vksps_vui[MAX_SPS_COUNT];
    StdVideoH264SequenceParameterSet vksps[MAX_SPS_COUNT];

    /* PPS */
    StdVideoH264ScalingLists vkpps_scaling[MAX_PPS_COUNT];
    StdVideoH264PictureParameterSet vkpps[MAX_PPS_COUNT];

    VkVideoDecodeH264SessionParametersAddInfoKHR h264_params_info = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_SESSION_PARAMETERS_ADD_INFO_KHR,
        .pStdSPSs = vksps,
        .stdSPSCount = 0,
        .pStdPPSs = vkpps,
        .stdPPSCount = 0,
    };
    VkVideoDecodeH264SessionParametersCreateInfoKHR h264_params = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_SESSION_PARAMETERS_CREATE_INFO_KHR,
        .pParametersAddInfo = &h264_params_info,
    };
    VkVideoSessionParametersCreateInfoKHR session_params_create = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_PARAMETERS_CREATE_INFO_KHR,
        .pNext = &h264_params,
        .videoSession = ctx->common.session,
        .videoSessionParametersTemplate = VK_NULL_HANDLE,
    };

    /* SPS list */
    for (int i = 0; i < FF_ARRAY_ELEMS(h->ps.sps_list); i++) {
        if (h->ps.sps_list[i]) {
            const SPS *sps_l = h->ps.sps_list[i];
            int idx = h264_params_info.stdSPSCount;
            set_sps(sps_l, &vksps_scaling[idx], &vksps_vui_header[idx], &vksps_vui[idx], &vksps[idx]);
            h264_params_info.stdSPSCount++;
        }
    }

    /* PPS list */
    for (int i = 0; i < FF_ARRAY_ELEMS(h->ps.pps_list); i++) {
        if (h->ps.pps_list[i]) {
            const PPS *pps_l = h->ps.pps_list[i];
            int idx = h264_params_info.stdPPSCount;
            set_pps(pps_l, pps_l->sps, &vkpps_scaling[idx], &vkpps[idx]);
            h264_params_info.stdPPSCount++;
        }
    }

    h264_params.maxStdSPSCount = h264_params_info.stdSPSCount;
    h264_params.maxStdPPSCount = h264_params_info.stdPPSCount;

    err = ff_vk_decode_create_params(buf, avctx, ctx, &session_params_create);
    if (err < 0)
        return err;

    av_log(avctx, AV_LOG_DEBUG, "Created frame parameters: %i SPS %i PPS\n",
           h264_params_info.stdSPSCount, h264_params_info.stdPPSCount);

    return 0;
}

static int vk_h264_start_frame(AVCodecContext          *avctx,
                               av_unused const uint8_t *buffer,
                               av_unused uint32_t       size)
{
    int err;
    int dpb_slot_index = 0;
    H264Context *h = avctx->priv_data;
    H264Picture *pic = h->cur_pic_ptr;
    FFVulkanDecodeContext *dec = avctx->internal->hwaccel_priv_data;
    H264VulkanDecodePicture *hp = pic->hwaccel_picture_private;
    FFVulkanDecodePicture *vp = &hp->vp;

    if (!dec->session_params) {
        err = vk_h264_create_params(avctx, &dec->session_params);
        if (err < 0)
            return err;
    }

    /* Fill in main slot */
    dpb_slot_index = 0;
    for (unsigned slot = 0; slot < H264_MAX_PICTURE_COUNT; slot++) {
        if (pic == &h->DPB[slot]) {
            dpb_slot_index = slot;
            break;
        }
    }

    err = vk_h264_fill_pict(avctx, NULL, &vp->ref_slot, &vp->ref,
                            &hp->vkh264_ref, &hp->h264_ref, pic, 1,
                            h->DPB[dpb_slot_index].field_picture,
                            h->DPB[dpb_slot_index].reference,
                            dpb_slot_index);
    if (err < 0)
        return err;

    /* Fill in short-term references */
    for (int i = 0; i < h->short_ref_count; i++) {
        dpb_slot_index = 0;
        for (unsigned slot = 0; slot < H264_MAX_PICTURE_COUNT; slot++) {
            if (h->short_ref[i] == &h->DPB[slot]) {
                dpb_slot_index = slot;
                break;
            }
        }
        err = vk_h264_fill_pict(avctx, &hp->ref_src[i], &vp->ref_slots[i],
                                &vp->refs[i], &hp->vkh264_refs[i],
                                &hp->h264_refs[i], h->short_ref[i], 0,
                                h->DPB[dpb_slot_index].field_picture,
                                h->DPB[dpb_slot_index].reference,
                                dpb_slot_index);
        if (err < 0)
            return err;
    }

    /* Fill in long-term refs */
    for (int r = 0, i = h->short_ref_count; r < H264_MAX_DPB_FRAMES &&
         i < h->short_ref_count + h->long_ref_count; r++) {
        if (!h->long_ref[r])
            continue;

        dpb_slot_index = 0;
        for (unsigned slot = 0; slot < 16; slot++) {
            if (h->long_ref[r] == &h->DPB[slot]) {
                dpb_slot_index = slot;
                break;
            }
        }
        err = vk_h264_fill_pict(avctx, &hp->ref_src[i], &vp->ref_slots[i],
                                &vp->refs[i], &hp->vkh264_refs[i],
                                &hp->h264_refs[i], h->long_ref[r], 0,
                                h->DPB[dpb_slot_index].field_picture,
                                h->DPB[dpb_slot_index].reference,
                                dpb_slot_index);
        if (err < 0)
            return err;
        i++;
    }

    hp->h264pic = (StdVideoDecodeH264PictureInfo) {
        .seq_parameter_set_id = pic->pps->sps_id,
        .pic_parameter_set_id = pic->pps->pps_id,
        .frame_num = 0,  /* Set later */
        .idr_pic_id = 0, /* Set later */
        .PicOrderCnt[0] = pic->field_poc[0],
        .PicOrderCnt[1] = pic->field_poc[1],
        .flags = (StdVideoDecodeH264PictureInfoFlags) {
            .field_pic_flag = FIELD_PICTURE(h),
            .is_intra = 1, /* Set later */
            .IdrPicFlag = h->picture_idr,
            .bottom_field_flag = h->picture_structure != PICT_FRAME &&
                                 h->picture_structure & PICT_BOTTOM_FIELD,
            .is_reference = h->nal_ref_idc != 0,
            .complementary_field_pair = h->first_field && FIELD_PICTURE(h),
        },
    };

    hp->h264_pic_info = (VkVideoDecodeH264PictureInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_PICTURE_INFO_KHR,
        .pStdPictureInfo = &hp->h264pic,
    };

    vp->decode_info = (VkVideoDecodeInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_INFO_KHR,
        .pNext = &hp->h264_pic_info,
        .flags = 0x0,
        .pSetupReferenceSlot = &vp->ref_slot,
        .referenceSlotCount = h->short_ref_count + h->long_ref_count,
        .pReferenceSlots = vp->ref_slots,
        .dstPictureResource = (VkVideoPictureResourceInfoKHR) {
            .sType = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR,
            .codedOffset = (VkOffset2D){ 0, 0 },
            .codedExtent = (VkExtent2D){ pic->f->width, pic->f->height },
            .baseArrayLayer = 0,
            .imageViewBinding = vp->img_view_out,
        },
    };

    return 0;
}

static int vk_h264_decode_slice(AVCodecContext *avctx,
                                const uint8_t  *data,
                                uint32_t        size)
{
    const H264Context *h = avctx->priv_data;
    const H264SliceContext *sl  = &h->slice_ctx[0];
    H264VulkanDecodePicture *hp = h->cur_pic_ptr->hwaccel_picture_private;
    FFVulkanDecodePicture *vp = &hp->vp;

    int err = ff_vk_decode_add_slice(avctx, vp, data, size, 1,
                                     &hp->h264_pic_info.sliceCount,
                                     &hp->h264_pic_info.pSliceOffsets);
    if (err < 0)
        return err;

    hp->h264pic.frame_num = sl->frame_num;
    hp->h264pic.idr_pic_id = sl->idr_pic_id;

    /* Frame is only intra of all slices are marked as intra */
    if (sl->slice_type != AV_PICTURE_TYPE_I && sl->slice_type != AV_PICTURE_TYPE_SI)
        hp->h264pic.flags.is_intra = 0;

    return 0;
}

static int vk_h264_end_frame(AVCodecContext *avctx)
{
    const H264Context *h = avctx->priv_data;
    H264Picture *pic = h->cur_pic_ptr;
    H264VulkanDecodePicture *hp = pic->hwaccel_picture_private;
    FFVulkanDecodeContext *dec = avctx->internal->hwaccel_priv_data;
    FFVulkanDecodePicture *vp = &hp->vp;
    FFVulkanDecodePicture *rvp[H264_MAX_PICTURE_COUNT] = { 0 };
    AVFrame *rav[H264_MAX_PICTURE_COUNT] = { 0 };

    if (!hp->h264_pic_info.sliceCount)
        return 0;

    if (!vp->slices_buf)
        return AVERROR(EINVAL);

    if (!dec->session_params) {
        int err = vk_h264_create_params(avctx, &dec->session_params);
        if (err < 0)
            return err;

        hp->h264pic.seq_parameter_set_id = pic->pps->sps_id;
        hp->h264pic.pic_parameter_set_id = pic->pps->pps_id;
    }

    for (int i = 0; i < vp->decode_info.referenceSlotCount; i++) {
        H264Picture *rp = hp->ref_src[i];
        H264VulkanDecodePicture *rhp = rp->hwaccel_picture_private;

        rvp[i] = &rhp->vp;
        rav[i] = hp->ref_src[i]->f;
    }

    av_log(avctx, AV_LOG_VERBOSE, "Decoding frame, %"SIZE_SPECIFIER" bytes, %i slices\n",
           vp->slices_size, hp->h264_pic_info.sliceCount);

    return ff_vk_decode_frame(avctx, pic->f, vp, rav, rvp);
}

static void vk_h264_free_frame_priv(FFRefStructOpaque _hwctx, void *data)
{
    AVHWDeviceContext *hwctx = _hwctx.nc;
    H264VulkanDecodePicture *hp = data;

    /* Free frame resources, this also destroys the session parameters. */
    ff_vk_decode_free_frame(hwctx, &hp->vp);
}

const FFHWAccel ff_h264_vulkan_hwaccel = {
    .p.name                = "h264_vulkan",
    .p.type                = AVMEDIA_TYPE_VIDEO,
    .p.id                  = AV_CODEC_ID_H264,
    .p.pix_fmt             = AV_PIX_FMT_VULKAN,
    .start_frame           = &vk_h264_start_frame,
    .decode_slice          = &vk_h264_decode_slice,
    .end_frame             = &vk_h264_end_frame,
    .free_frame_priv       = &vk_h264_free_frame_priv,
    .frame_priv_data_size  = sizeof(H264VulkanDecodePicture),
    .init                  = &ff_vk_decode_init,
    .update_thread_context = &ff_vk_update_thread_context,
    .decode_params         = &ff_vk_params_invalidate,
    .flush                 = &ff_vk_decode_flush,
    .uninit                = &ff_vk_decode_uninit,
    .frame_params          = &ff_vk_frame_params,
    .priv_data_size        = sizeof(FFVulkanDecodeContext),
    .caps_internal         = HWACCEL_CAP_ASYNC_SAFE | HWACCEL_CAP_THREAD_SAFE,
};
