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

#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "libavutil/mem.h"

#include "cbs.h"
#include "cbs_h265.h"
#include "atsc_a53.h"
#include "libavutil/mastering_display_metadata.h"

#include "codec_internal.h"
#include "version.h"
#include "hw_base_encode_h265.h"

#include "vulkan_encode.h"

enum UnitElems {
    UNIT_AUD                     = 1 << 0,
    UNIT_SEI_MASTERING_DISPLAY   = 1 << 1,
    UNIT_SEI_CONTENT_LIGHT_LEVEL = 1 << 2,
    UNIT_SEI_A53_CC              = 1 << 3,
};

const FFVulkanEncodeDescriptor ff_vk_enc_h265_desc = {
    .codec_id         = AV_CODEC_ID_H265,
    .encode_extension = FF_VK_EXT_VIDEO_ENCODE_H265,
    .encode_op        = VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR,
    .ext_props = {
        .extensionName = VK_STD_VULKAN_VIDEO_CODEC_H265_ENCODE_EXTENSION_NAME,
        .specVersion   = VK_STD_VULKAN_VIDEO_CODEC_H265_ENCODE_SPEC_VERSION,
    },
};

typedef struct VulkanEncodeH265Picture {
    int frame_num;
    int64_t last_idr_frame;
    uint16_t idr_pic_id;
    int primary_pic_type;
    int slice_type;
    int pic_order_cnt;
    int pic_type;

    enum UnitElems units_needed;

    VkVideoEncodeH265RateControlInfoKHR vkrc_info;
    VkVideoEncodeH265RateControlLayerInfoKHR vkrc_layer_info;

    StdVideoEncodeH265PictureInfo   h265pic_info;
    VkVideoEncodeH265PictureInfoKHR vkh265pic_info;

    StdVideoEncodeH265WeightTable slice_wt;
    StdVideoEncodeH265SliceSegmentHeader slice_hdr;
    VkVideoEncodeH265NaluSliceSegmentInfoKHR vkslice;

    StdVideoEncodeH265ReferenceInfo h265dpb_info;
    VkVideoEncodeH265DpbSlotInfoKHR vkh265dpb_info;

    StdVideoEncodeH265ReferenceListsInfo ref_list_info;
    StdVideoEncodeH265LongTermRefPics l_rps;
    StdVideoH265ShortTermRefPicSet s_rps;
} VulkanEncodeH265Picture;

typedef struct VulkanEncodeH265Context {
    FFVulkanEncodeContext common;

    FFHWBaseEncodeH265 units;
    FFHWBaseEncodeH265Opts unit_opts;

    enum UnitElems unit_elems;

    uint8_t fixed_qp_idr;
    uint8_t fixed_qp_p;
    uint8_t fixed_qp_b;

    uint64_t hrd_buffer_size;
    uint64_t initial_buffer_fullness;

    VkVideoEncodeH265ProfileInfoKHR profile;

    VkVideoEncodeH265CapabilitiesKHR caps;
    VkVideoEncodeH265QualityLevelPropertiesKHR quality_props;

    CodedBitstreamContext *cbs;
    CodedBitstreamFragment current_access_unit;

    H265RawAUD                         raw_aud;

    SEIRawMasteringDisplayColourVolume sei_mastering_display;
    SEIRawContentLightLevelInfo        sei_content_light_level;
    SEIRawUserDataRegistered           sei_a53cc;
    void                              *sei_a53cc_data;
} VulkanEncodeH265Context;

static int init_pic_rc(AVCodecContext *avctx, FFHWBaseEncodePicture *pic,
                       VkVideoEncodeRateControlInfoKHR *rc_info,
                       VkVideoEncodeRateControlLayerInfoKHR *rc_layer)
{
    VulkanEncodeH265Context *enc = avctx->priv_data;
    FFVulkanEncodeContext   *ctx = &enc->common;
    VulkanEncodeH265Picture  *hp = pic->codec_priv;

    hp->vkrc_info = (VkVideoEncodeH265RateControlInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_RATE_CONTROL_INFO_KHR,
        .flags = VK_VIDEO_ENCODE_H265_RATE_CONTROL_REFERENCE_PATTERN_FLAT_BIT_KHR |
                 VK_VIDEO_ENCODE_H265_RATE_CONTROL_REGULAR_GOP_BIT_KHR,
        .idrPeriod = ctx->base.gop_size,
        .gopFrameCount = ctx->base.gop_size,
        .consecutiveBFrameCount = FFMAX(ctx->base.b_per_p - 1, 0),
        .subLayerCount = 0,
    };
    rc_info->pNext = &hp->vkrc_info;

    if (rc_info->rateControlMode > VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR) {
        rc_info->virtualBufferSizeInMs = (enc->hrd_buffer_size * 1000LL) / avctx->bit_rate;
        rc_info->initialVirtualBufferSizeInMs = (enc->initial_buffer_fullness * 1000LL) / avctx->bit_rate;

        hp->vkrc_layer_info = (VkVideoEncodeH265RateControlLayerInfoKHR) {
            .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_RATE_CONTROL_LAYER_INFO_KHR,

            .useMinQp  = avctx->qmin > 0,
            .minQp.qpI = avctx->qmin > 0 ? avctx->qmin : 0,
            .minQp.qpP = avctx->qmin > 0 ? avctx->qmin : 0,
            .minQp.qpB = avctx->qmin > 0 ? avctx->qmin : 0,

            .useMaxQp  = avctx->qmax > 0,
            .maxQp.qpI = avctx->qmax > 0 ? avctx->qmax : 0,
            .maxQp.qpP = avctx->qmax > 0 ? avctx->qmax : 0,
            .maxQp.qpB = avctx->qmax > 0 ? avctx->qmax : 0,

            .useMaxFrameSize = 0,
        };
        rc_layer->pNext = &hp->vkrc_layer_info;
        hp->vkrc_info.subLayerCount = 1;
    }

    return 0;
}

static int vk_enc_h265_update_pic_info(AVCodecContext *avctx,
                                       FFHWBaseEncodePicture *pic)
{
    VulkanEncodeH265Context   *enc = avctx->priv_data;
    VulkanEncodeH265Picture    *hp = pic->codec_priv;
    FFHWBaseEncodePicture    *prev = pic->prev;
    VulkanEncodeH265Picture *hprev = prev ? prev->codec_priv : NULL;

    if (pic->type == FF_HW_PICTURE_TYPE_IDR) {
        av_assert0(pic->display_order == pic->encode_order);

        hp->last_idr_frame = pic->display_order;

        hp->slice_type     = STD_VIDEO_H265_SLICE_TYPE_I;
        hp->pic_type       = STD_VIDEO_H265_PICTURE_TYPE_IDR;
    } else {
        av_assert0(prev);
        hp->last_idr_frame = hprev->last_idr_frame;

        if (pic->type == FF_HW_PICTURE_TYPE_I) {
            hp->slice_type     = STD_VIDEO_H265_SLICE_TYPE_I;
            hp->pic_type       = STD_VIDEO_H265_PICTURE_TYPE_I;
        } else if (pic->type == FF_HW_PICTURE_TYPE_P) {
            av_assert0(pic->refs[0]);
            hp->slice_type     = STD_VIDEO_H265_SLICE_TYPE_P;
            hp->pic_type       = STD_VIDEO_H265_PICTURE_TYPE_P;
        } else {
            FFHWBaseEncodePicture *irap_ref;
            av_assert0(pic->refs[0][0] && pic->refs[1][0]);
            for (irap_ref = pic; irap_ref; irap_ref = irap_ref->refs[1][0]) {
                if (irap_ref->type == FF_HW_PICTURE_TYPE_I)
                    break;
            }
            hp->slice_type = STD_VIDEO_H265_SLICE_TYPE_B;
            hp->pic_type   = STD_VIDEO_H265_PICTURE_TYPE_B;
        }
    }
    hp->pic_order_cnt = pic->display_order - hp->last_idr_frame;

    hp->units_needed = 0;

    if (enc->unit_elems & UNIT_AUD) {
        hp->units_needed |= UNIT_AUD;
        enc->raw_aud = (H265RawAUD) {
            .nal_unit_header = {
                .nal_unit_type         = HEVC_NAL_AUD,
                .nuh_layer_id          = 0,
                .nuh_temporal_id_plus1 = 1,
            },
            .pic_type = hp->pic_type,
        };
    }

    // Only look for the metadata on I/IDR frame on the output. We
    // may force an IDR frame on the output where the medadata gets
    // changed on the input frame.
    if ((enc->unit_elems & UNIT_SEI_MASTERING_DISPLAY) &&
        (pic->type == FF_HW_PICTURE_TYPE_I || pic->type == FF_HW_PICTURE_TYPE_IDR)) {
        AVFrameSideData *sd =
            av_frame_get_side_data(pic->input_image,
                                   AV_FRAME_DATA_MASTERING_DISPLAY_METADATA);

        if (sd) {
            AVMasteringDisplayMetadata *mdm = (AVMasteringDisplayMetadata *)sd->data;

            // SEI is needed when both the primaries and luminance are set
            if (mdm->has_primaries && mdm->has_luminance) {
                SEIRawMasteringDisplayColourVolume *mdcv =
                    &enc->sei_mastering_display;
                const int mapping[3] = {1, 2, 0};
                const int chroma_den = 50000;
                const int luma_den   = 10000;

                for (int i = 0; i < 3; i++) {
                    const int j = mapping[i];
                    mdcv->display_primaries_x[i] =
                        FFMIN(lrint(chroma_den *
                                    av_q2d(mdm->display_primaries[j][0])),
                              chroma_den);
                    mdcv->display_primaries_y[i] =
                        FFMIN(lrint(chroma_den *
                                    av_q2d(mdm->display_primaries[j][1])),
                              chroma_den);
                }

                mdcv->white_point_x =
                    FFMIN(lrint(chroma_den * av_q2d(mdm->white_point[0])),
                          chroma_den);
                mdcv->white_point_y =
                    FFMIN(lrint(chroma_den * av_q2d(mdm->white_point[1])),
                          chroma_den);

                mdcv->max_display_mastering_luminance =
                    lrint(luma_den * av_q2d(mdm->max_luminance));
                mdcv->min_display_mastering_luminance =
                    FFMIN(lrint(luma_den * av_q2d(mdm->min_luminance)),
                          mdcv->max_display_mastering_luminance);

                hp->units_needed |= UNIT_SEI_MASTERING_DISPLAY;
            }
        }
    }

    if ((enc->unit_elems & UNIT_SEI_CONTENT_LIGHT_LEVEL) &&
        (pic->type == FF_HW_PICTURE_TYPE_I || pic->type == FF_HW_PICTURE_TYPE_IDR)) {
        AVFrameSideData *sd = av_frame_get_side_data(pic->input_image,
                                                     AV_FRAME_DATA_CONTENT_LIGHT_LEVEL);

        if (sd) {
            AVContentLightMetadata *clm = (AVContentLightMetadata *)sd->data;
            SEIRawContentLightLevelInfo *clli = &enc->sei_content_light_level;

            clli->max_content_light_level     = FFMIN(clm->MaxCLL,  65535);
            clli->max_pic_average_light_level = FFMIN(clm->MaxFALL, 65535);

            hp->units_needed |= UNIT_SEI_CONTENT_LIGHT_LEVEL;
        }
    }

    if (enc->unit_elems & UNIT_SEI_A53_CC) {
        int err;
        size_t sei_a53cc_len;
        av_freep(&enc->sei_a53cc_data);
        err = ff_alloc_a53_sei(pic->input_image, 0, &enc->sei_a53cc_data, &sei_a53cc_len);
        if (err < 0)
            return err;
        if (enc->sei_a53cc_data != NULL) {
            enc->sei_a53cc.itu_t_t35_country_code = 181;
            enc->sei_a53cc.data = (uint8_t *)enc->sei_a53cc_data + 1;
            enc->sei_a53cc.data_length = sei_a53cc_len - 1;

            hp->units_needed |= UNIT_SEI_A53_CC;
        }
    }

    return 0;
}

static void setup_slices(AVCodecContext *avctx,
                         FFHWBaseEncodePicture *pic)
{
    VulkanEncodeH265Context *enc = avctx->priv_data;
    VulkanEncodeH265Picture *hp = pic->codec_priv;

    hp->slice_wt = (StdVideoEncodeH265WeightTable) {
        .flags = (StdVideoEncodeH265WeightTableFlags) {
            .luma_weight_l0_flag = 0,
            .chroma_weight_l0_flag = 0,
            .luma_weight_l1_flag = 0,
            .chroma_weight_l1_flag = 0,
        },
        .luma_log2_weight_denom = 0,
        .delta_chroma_log2_weight_denom = 0,
        .delta_luma_weight_l0 = { 0 },
        .luma_offset_l0 = { 0 },
        .delta_chroma_weight_l0 = { { 0 } },
        .delta_chroma_offset_l0 = { { 0 } },
        .delta_luma_weight_l1 = { 0 },
        .luma_offset_l1 = { 0 },
        .delta_chroma_weight_l1 = { { 0 } },
        .delta_chroma_offset_l1 = { { 0 } },
    };

    hp->slice_hdr = (StdVideoEncodeH265SliceSegmentHeader) {
        .flags = (StdVideoEncodeH265SliceSegmentHeaderFlags) {
            .first_slice_segment_in_pic_flag = 1,
            .dependent_slice_segment_flag = 0,
            .slice_sao_luma_flag = enc->units.raw_sps.sample_adaptive_offset_enabled_flag,
            .slice_sao_chroma_flag = enc->units.raw_sps.sample_adaptive_offset_enabled_flag,
            .num_ref_idx_active_override_flag = 0,
            .mvd_l1_zero_flag = 0,
            .cabac_init_flag = 0,
            .cu_chroma_qp_offset_enabled_flag = 0,
            .deblocking_filter_override_flag = 0,
            .slice_deblocking_filter_disabled_flag = 0,
            .collocated_from_l0_flag = 1,
            .slice_loop_filter_across_slices_enabled_flag = 0,
            /* Reserved */
        },
        .slice_type = hp->slice_type,
        .slice_segment_address = 0,
        .collocated_ref_idx = 0,
        .MaxNumMergeCand = 5,
        .slice_cb_qp_offset = 0,
        .slice_cr_qp_offset = 0,
        .slice_beta_offset_div2 = 0,
        .slice_tc_offset_div2 = 0,
        .slice_act_y_qp_offset = 0,
        .slice_act_cb_qp_offset = 0,
        .slice_act_cr_qp_offset = 0,
        .slice_qp_delta = 0, /* Filled in below */
        /* Reserved */
        .pWeightTable = NULL, // &hp->slice_wt,
    };

    hp->vkslice = (VkVideoEncodeH265NaluSliceSegmentInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_NALU_SLICE_SEGMENT_INFO_KHR,
        .pNext = NULL,
        .constantQp = pic->type == FF_HW_PICTURE_TYPE_B ? enc->fixed_qp_b :
                      pic->type == FF_HW_PICTURE_TYPE_P ? enc->fixed_qp_p :
                                                          enc->fixed_qp_idr,
        .pStdSliceSegmentHeader = &hp->slice_hdr,
    };

    if (enc->common.opts.rc_mode != VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR)
        hp->vkslice.constantQp = 0;

    hp->slice_hdr.slice_qp_delta = hp->vkslice.constantQp -
                                   (enc->units.raw_pps.init_qp_minus26 + 26);

    hp->vkh265pic_info.pNaluSliceSegmentEntries = &hp->vkslice;
    hp->vkh265pic_info.naluSliceSegmentEntryCount = 1;
}

static void setup_refs(AVCodecContext *avctx,
                       FFHWBaseEncodePicture *pic,
                       VkVideoEncodeInfoKHR *encode_info)
{
    int i, j;
    VulkanEncodeH265Context *enc = avctx->priv_data;
    VulkanEncodeH265Picture *hp = pic->codec_priv;

    hp->ref_list_info = (StdVideoEncodeH265ReferenceListsInfo) {
        .flags = (StdVideoEncodeH265ReferenceListsInfoFlags) {
            .ref_pic_list_modification_flag_l0 = 0,
            .ref_pic_list_modification_flag_l1 = 0,
            /* Reserved */
        },
        /* May be overridden during setup_slices() */
        .num_ref_idx_l0_active_minus1 = pic->nb_refs[0] - 1,
        .num_ref_idx_l1_active_minus1 = pic->nb_refs[1] - 1,
        /* Reserved */
        .list_entry_l0 = { 0 },
        .list_entry_l1 = { 0 },
    };

    for (i = 0; i < STD_VIDEO_H265_MAX_NUM_LIST_REF; i++)
        hp->ref_list_info.RefPicList0[i] = hp->ref_list_info.RefPicList1[i] = -1;

    /* Note: really not sure */
    for (i = 0; i < pic->nb_refs[0]; i++) {
        VkVideoReferenceSlotInfoKHR *slot_info;
        slot_info = (VkVideoReferenceSlotInfoKHR *)&encode_info->pReferenceSlots[i];
        hp->ref_list_info.RefPicList0[i] = slot_info->slotIndex;
    }

    /* Note: really not sure */
    for (i = 0; i < pic->nb_refs[1]; i++) {
        VkVideoReferenceSlotInfoKHR *slot_info;
        slot_info = (VkVideoReferenceSlotInfoKHR *)&encode_info->pReferenceSlots[pic->nb_refs[0] + i];
        hp->ref_list_info.RefPicList1[i] = slot_info->slotIndex;
    }

    hp->h265pic_info.pRefLists = &hp->ref_list_info;

    if (pic->type != FF_HW_PICTURE_TYPE_IDR) {
        StdVideoH265ShortTermRefPicSet *rps;
        VulkanEncodeH265Picture *strp;
        int rps_poc[MAX_DPB_SIZE];
        int rps_used[MAX_DPB_SIZE];
        int poc, rps_pics;

        hp->h265pic_info.flags.short_term_ref_pic_set_sps_flag = 0;

        rps = &hp->s_rps;
        memset(rps, 0, sizeof(*rps));

        rps_pics = 0;
        for (i = 0; i < MAX_REFERENCE_LIST_NUM; i++) {
            for (j = 0; j < pic->nb_refs[i]; j++) {
                strp = pic->refs[i][j]->codec_priv;
                rps_poc[rps_pics]  = strp->pic_order_cnt;
                rps_used[rps_pics] = 1;
                ++rps_pics;
            }
        }

        for (i = 0; i < pic->nb_dpb_pics; i++) {
            if (pic->dpb[i] == pic)
                continue;

            for (j = 0; j < pic->nb_refs[0]; j++) {
                if (pic->dpb[i] == pic->refs[0][j])
                    break;
            }
            if (j < pic->nb_refs[0])
                continue;

            for (j = 0; j < pic->nb_refs[1]; j++) {
                if (pic->dpb[i] == pic->refs[1][j])
                    break;
            }
            if (j < pic->nb_refs[1])
                continue;

            strp = pic->dpb[i]->codec_priv;
            rps_poc[rps_pics]  = strp->pic_order_cnt;
            rps_used[rps_pics] = 0;
            ++rps_pics;
        }

        for (i = 1; i < rps_pics; i++) {
            for (j = i; j > 0; j--) {
                if (rps_poc[j] > rps_poc[j - 1])
                    break;
                av_assert0(rps_poc[j] != rps_poc[j - 1]);
                FFSWAP(int, rps_poc[j],  rps_poc[j - 1]);
                FFSWAP(int, rps_used[j], rps_used[j - 1]);
            }
        }

        av_log(avctx, AV_LOG_DEBUG, "RPS for POC %d:", hp->pic_order_cnt);
        for (i = 0; i < rps_pics; i++)
            av_log(avctx, AV_LOG_DEBUG, " (%d,%d)", rps_poc[i], rps_used[i]);

        av_log(avctx, AV_LOG_DEBUG, "\n");

        for (i = 0; i < rps_pics; i++) {
            av_assert0(rps_poc[i] != hp->pic_order_cnt);
            if (rps_poc[i] > hp->pic_order_cnt)
                break;
        }

        rps->num_negative_pics = i;
        rps->used_by_curr_pic_s0_flag = 0x0;
        poc = hp->pic_order_cnt;
        for (j = i - 1; j >= 0; j--) {
            rps->delta_poc_s0_minus1[i - 1 - j] = poc - rps_poc[j] - 1;
            rps->used_by_curr_pic_s0_flag |= rps_used[j] << (i - 1 - j);
            poc = rps_poc[j];
        }

        rps->num_positive_pics = rps_pics - i;
        rps->used_by_curr_pic_s1_flag = 0x0;
        poc = hp->pic_order_cnt;
        for (j = i; j < rps_pics; j++) {
            rps->delta_poc_s1_minus1[j - i] = rps_poc[j] - poc - 1;
            rps->used_by_curr_pic_s1_flag |= rps_used[j] << (j - i);
            poc = rps_poc[j];
        }

        hp->l_rps.num_long_term_sps  = 0;
        hp->l_rps.num_long_term_pics = 0;

        // when this flag is not present, it is inerred to 1.
        hp->slice_hdr.flags.collocated_from_l0_flag = 1;
        hp->h265pic_info.flags.slice_temporal_mvp_enabled_flag =
            enc->units.raw_sps.sps_temporal_mvp_enabled_flag;
        if (hp->h265pic_info.flags.slice_temporal_mvp_enabled_flag) {
            if (hp->slice_hdr.slice_type == STD_VIDEO_H265_SLICE_TYPE_B)
                hp->slice_hdr.flags.collocated_from_l0_flag = 1;
            hp->slice_hdr.collocated_ref_idx = 0;
        }

        hp->slice_hdr.flags.num_ref_idx_active_override_flag = 0;
        hp->ref_list_info.num_ref_idx_l0_active_minus1 = enc->units.raw_pps.num_ref_idx_l0_default_active_minus1;
        hp->ref_list_info.num_ref_idx_l1_active_minus1 = enc->units.raw_pps.num_ref_idx_l1_default_active_minus1;
    }

    hp->h265pic_info.pShortTermRefPicSet = &hp->s_rps;
    hp->h265pic_info.pLongTermRefPics = &hp->l_rps;
}

static int init_pic_params(AVCodecContext *avctx, FFHWBaseEncodePicture *pic,
                           VkVideoEncodeInfoKHR *encode_info)
{
    int err;
    VulkanEncodeH265Context *enc = avctx->priv_data;
    FFVulkanEncodePicture *vp = pic->priv;
    VulkanEncodeH265Picture *hp = pic->codec_priv;
    VkVideoReferenceSlotInfoKHR *ref_slot;

    err = vk_enc_h265_update_pic_info(avctx, pic);
    if (err < 0)
        return err;

    hp->vkh265pic_info = (VkVideoEncodeH265PictureInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_PICTURE_INFO_KHR,
        .pNext = NULL,
        .pNaluSliceSegmentEntries = NULL, // Filled in during setup_slices()
        .naluSliceSegmentEntryCount = 0, // Filled in during setup_slices()
        .pStdPictureInfo = &hp->h265pic_info,
    };

    hp->h265pic_info = (StdVideoEncodeH265PictureInfo) {
        .flags = (StdVideoEncodeH265PictureInfoFlags) {
            .is_reference = pic->is_reference,
            .IrapPicFlag = pic->type == FF_HW_PICTURE_TYPE_IDR,
            .used_for_long_term_reference = 0,
            .discardable_flag = 0,
            .cross_layer_bla_flag = 0,
            .pic_output_flag = 1,
            .no_output_of_prior_pics_flag = 0,
            .short_term_ref_pic_set_sps_flag = 0,
            .slice_temporal_mvp_enabled_flag = enc->units.raw_sps.sps_temporal_mvp_enabled_flag,
            /* Reserved */
        },
        .pic_type = hp->pic_type,
        .sps_video_parameter_set_id = 0,
        .pps_seq_parameter_set_id = 0,
        .pps_pic_parameter_set_id = 0,
        .short_term_ref_pic_set_idx = 0,
        .PicOrderCntVal = hp->pic_order_cnt,
        .TemporalId = 0,
        /* Reserved */
        .pRefLists = NULL, // Filled in during setup_refs
        .pShortTermRefPicSet = NULL,
        .pLongTermRefPics = NULL,
    };
    encode_info->pNext = &hp->vkh265pic_info;

    hp->h265dpb_info = (StdVideoEncodeH265ReferenceInfo) {
        .flags = (StdVideoEncodeH265ReferenceInfoFlags) {
            .used_for_long_term_reference = 0,
            .unused_for_reference = 0,
            /* Reserved */
        },
        .pic_type = hp->h265pic_info.pic_type,
        .PicOrderCntVal = hp->h265pic_info.PicOrderCntVal,
        .TemporalId = hp->h265pic_info.TemporalId,
    };
    hp->vkh265dpb_info = (VkVideoEncodeH265DpbSlotInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_DPB_SLOT_INFO_KHR,
        .pStdReferenceInfo = &hp->h265dpb_info,
    };

    vp->dpb_slot.pNext = &hp->vkh265dpb_info;

    ref_slot = (VkVideoReferenceSlotInfoKHR *)encode_info->pSetupReferenceSlot;
    ref_slot->pNext = &hp->vkh265dpb_info;

    setup_refs(avctx, pic, encode_info);

    setup_slices(avctx, pic);

    return 0;
}

static int init_profile(AVCodecContext *avctx,
                        VkVideoProfileInfoKHR *profile, void *pnext)
{
    VkResult ret;
    VulkanEncodeH265Context *enc = avctx->priv_data;
    FFVulkanEncodeContext *ctx = &enc->common;
    FFVulkanContext *s = &ctx->s;
    FFVulkanFunctions *vk = &ctx->s.vkfn;
    VkVideoEncodeH265CapabilitiesKHR h265_caps = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_CAPABILITIES_KHR,
    };
    VkVideoEncodeCapabilitiesKHR enc_caps = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_CAPABILITIES_KHR,
        .pNext = &h265_caps,
    };
    VkVideoCapabilitiesKHR caps = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_CAPABILITIES_KHR,
        .pNext = &enc_caps,
    };

    /* In order of preference */
    int last_supported = AV_PROFILE_UNKNOWN;
    static const int known_profiles[] = {
        AV_PROFILE_HEVC_MAIN,
        AV_PROFILE_HEVC_MAIN_10,
        AV_PROFILE_HEVC_REXT,
    };
    int nb_profiles = FF_ARRAY_ELEMS(known_profiles);

    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(s->frames->sw_format);
    if (!desc)
        return AVERROR(EINVAL);

    if (s->frames->sw_format == AV_PIX_FMT_NV12)
        nb_profiles = 1;
    else if (s->frames->sw_format == AV_PIX_FMT_P010)
        nb_profiles = 2;

    enc->profile = (VkVideoEncodeH265ProfileInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_PROFILE_INFO_KHR,
        .pNext = pnext,
        .stdProfileIdc = ff_vk_h265_profile_to_vk(avctx->profile),
    };
    profile->pNext = &enc->profile;

    /* Set level */
    if (avctx->level == AV_LEVEL_UNKNOWN)
        avctx->level = enc->common.opts.level;

    /* User has explicitly specified a profile. */
    if (avctx->profile != AV_PROFILE_UNKNOWN)
        return 0;

    av_log(avctx, AV_LOG_DEBUG, "Supported profiles:\n");
    for (int i = 0; i < nb_profiles; i++) {
        enc->profile.stdProfileIdc = ff_vk_h265_profile_to_vk(known_profiles[i]);
        ret = vk->GetPhysicalDeviceVideoCapabilitiesKHR(s->hwctx->phys_dev,
                                                        profile,
                                                        &caps);
        if (ret == VK_SUCCESS) {
            av_log(avctx, AV_LOG_DEBUG, "    %s\n",
                   avcodec_profile_name(avctx->codec_id, known_profiles[i]));
            last_supported = known_profiles[i];
        }
    }

    if (last_supported == AV_PROFILE_UNKNOWN) {
        av_log(avctx, AV_LOG_ERROR, "No supported profiles for given format\n");
        return AVERROR(ENOTSUP);
    }

    enc->profile.stdProfileIdc = ff_vk_h265_profile_to_vk(last_supported);
    av_log(avctx, AV_LOG_VERBOSE, "Using profile %s\n",
           avcodec_profile_name(avctx->codec_id, last_supported));
    avctx->profile = last_supported;

    return 0;
}

static int init_enc_options(AVCodecContext *avctx)
{
    VulkanEncodeH265Context *enc = avctx->priv_data;

    if (avctx->rc_buffer_size)
        enc->hrd_buffer_size = avctx->rc_buffer_size;
    else if (avctx->rc_max_rate > 0)
        enc->hrd_buffer_size = avctx->rc_max_rate;
    else
        enc->hrd_buffer_size = avctx->bit_rate;

    if (avctx->rc_initial_buffer_occupancy) {
        if (avctx->rc_initial_buffer_occupancy > enc->hrd_buffer_size) {
            av_log(avctx, AV_LOG_ERROR, "Invalid RC buffer settings: "
                                        "must have initial buffer size (%d) <= "
                                        "buffer size (%"PRId64").\n",
                   avctx->rc_initial_buffer_occupancy, enc->hrd_buffer_size);
            return AVERROR(EINVAL);
        }
        enc->initial_buffer_fullness = avctx->rc_initial_buffer_occupancy;
    } else {
        enc->initial_buffer_fullness = enc->hrd_buffer_size * 3 / 4;
    }

    if (enc->common.opts.rc_mode == VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR) {
        enc->fixed_qp_p = av_clip(enc->common.opts.qp,
                                  enc->caps.minQp, enc->caps.maxQp);

        if (avctx->i_quant_factor > 0.0)
            enc->fixed_qp_idr = av_clip((avctx->i_quant_factor * enc->fixed_qp_p +
                                         avctx->i_quant_offset) + 0.5,
                                        enc->caps.minQp, enc->caps.maxQp);
        else
            enc->fixed_qp_idr = enc->fixed_qp_p;

        if (avctx->b_quant_factor > 0.0)
            enc->fixed_qp_b = av_clip((avctx->b_quant_factor * enc->fixed_qp_p +
                                       avctx->b_quant_offset) + 0.5,
                                      enc->caps.minQp, enc->caps.maxQp);
        else
            enc->fixed_qp_b = enc->fixed_qp_p;

        av_log(avctx, AV_LOG_DEBUG, "Using fixed QP = "
               "%d / %d / %d for IDR- / P- / B-frames.\n",
               enc->fixed_qp_idr, enc->fixed_qp_p, enc->fixed_qp_b);
    } else {
        enc->fixed_qp_idr = 26;
        enc->fixed_qp_p = 26;
        enc->fixed_qp_b = 26;
    }

    return 0;
}

static av_cold int init_sequence_headers(AVCodecContext *avctx)
{
    int err;
    VulkanEncodeH265Context *enc = avctx->priv_data;
    FFVulkanEncodeContext *ctx = &enc->common;
    FFHWBaseEncodeContext *base_ctx = &ctx->base;

    FFHWBaseEncodeH265 *units = &enc->units;
    FFHWBaseEncodeH265Opts *unit_opts = &enc->unit_opts;

    int max_ctb_size;
    unsigned min_tb_size;
    unsigned max_tb_size;
    unsigned max_transform_hierarchy;

    unit_opts->tier = enc->common.opts.tier;
    unit_opts->fixed_qp_idr = enc->fixed_qp_idr;
    unit_opts->cu_qp_delta_enabled_flag = enc->common.opts.rc_mode != VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR;

    unit_opts->nb_slices = 1;

    unit_opts->slice_block_rows = (avctx->height + base_ctx->slice_block_height - 1) /
                                  base_ctx->slice_block_height;
    unit_opts->slice_block_cols = (avctx->width  + base_ctx->slice_block_width  - 1) /
                                  base_ctx->slice_block_width;

    /* cabac already set via an option */
    /* fixed_qp_idr initialized in init_enc_options() */
    /* hrd_buffer_size initialized in init_enc_options() */
    /* initial_buffer_fullness initialized in init_enc_options() */

    err = ff_hw_base_encode_init_params_h265(&enc->common.base, avctx,
                                             units, unit_opts);
    if (err < 0)
        return err;

    units->raw_sps.sample_adaptive_offset_enabled_flag =
      !!(enc->caps.stdSyntaxFlags & VK_VIDEO_ENCODE_H265_STD_SAMPLE_ADAPTIVE_OFFSET_ENABLED_FLAG_SET_BIT_KHR);
    units->raw_pps.transform_skip_enabled_flag =
      !!(enc->caps.stdSyntaxFlags & VK_VIDEO_ENCODE_H265_STD_TRANSFORM_SKIP_ENABLED_FLAG_SET_BIT_KHR);

    max_ctb_size = 16;

    /* coding blocks from 8x8 to max CTB size. */
    if (enc->caps.ctbSizes & VK_VIDEO_ENCODE_H265_CTB_SIZE_64_BIT_KHR)
        max_ctb_size = 64;
    else if (enc->caps.ctbSizes & VK_VIDEO_ENCODE_H265_CTB_SIZE_32_BIT_KHR)
        max_ctb_size = 32;

    min_tb_size = 0;
    max_tb_size = 0;
    if (enc->caps.transformBlockSizes & VK_VIDEO_ENCODE_H265_TRANSFORM_BLOCK_SIZE_4_BIT_KHR)
        min_tb_size = 4;
    else if (enc->caps.transformBlockSizes & VK_VIDEO_ENCODE_H265_TRANSFORM_BLOCK_SIZE_8_BIT_KHR)
        min_tb_size = 8;
    else if (enc->caps.transformBlockSizes & VK_VIDEO_ENCODE_H265_TRANSFORM_BLOCK_SIZE_16_BIT_KHR)
        min_tb_size = 16;
    else if (enc->caps.transformBlockSizes & VK_VIDEO_ENCODE_H265_TRANSFORM_BLOCK_SIZE_32_BIT_KHR)
        min_tb_size = 32;

    if (enc->caps.transformBlockSizes & VK_VIDEO_ENCODE_H265_TRANSFORM_BLOCK_SIZE_32_BIT_KHR)
        max_tb_size = 32;
    else if (enc->caps.transformBlockSizes & VK_VIDEO_ENCODE_H265_TRANSFORM_BLOCK_SIZE_16_BIT_KHR)
        max_tb_size = 16;
    else if (enc->caps.transformBlockSizes & VK_VIDEO_ENCODE_H265_TRANSFORM_BLOCK_SIZE_8_BIT_KHR)
        max_tb_size = 8;
    else if (enc->caps.transformBlockSizes & VK_VIDEO_ENCODE_H265_TRANSFORM_BLOCK_SIZE_4_BIT_KHR)
        max_tb_size = 4;

    units->raw_sps.log2_min_luma_coding_block_size_minus3 = 0;
    units->raw_sps.log2_diff_max_min_luma_coding_block_size = av_log2(max_ctb_size) - 3;
    units->raw_sps.log2_min_luma_transform_block_size_minus2 = av_log2(min_tb_size) - 2;
    units->raw_sps.log2_diff_max_min_luma_transform_block_size = av_log2(max_tb_size) - av_log2(min_tb_size);

    max_transform_hierarchy = av_log2(max_ctb_size) - av_log2(min_tb_size);
    units->raw_sps.max_transform_hierarchy_depth_intra = max_transform_hierarchy;
    units->raw_sps.max_transform_hierarchy_depth_intra = max_transform_hierarchy;

    units->raw_sps.vui.bitstream_restriction_flag = 0;
    units->raw_sps.vui.max_bytes_per_pic_denom = 2;
    units->raw_sps.vui.max_bits_per_min_cu_denom = 1;

    units->raw_sps.sps_temporal_mvp_enabled_flag = 0;

    if (base_ctx->gop_size & base_ctx->gop_size - 1 == 0)
        units->raw_sps.log2_max_pic_order_cnt_lsb_minus4 = FFMAX(av_log2(base_ctx->gop_size) - 4, 0);
    else
        units->raw_sps.log2_max_pic_order_cnt_lsb_minus4 = FFMAX(av_log2(base_ctx->gop_size) - 3, 0);

    return 0;
}

typedef struct VulkanH265Units {
    StdVideoH265SequenceParameterSet    sps;
    StdVideoH265ShortTermRefPicSet      str[STD_VIDEO_H265_SUBLAYERS_LIST_SIZE];
    StdVideoH265LongTermRefPicsSps      ltr;
    StdVideoH265ProfileTierLevel        ptl_sps;
    StdVideoH265DecPicBufMgr            dpbm_sps;

    StdVideoH265HrdParameters           vui_header_sps;
    StdVideoH265SequenceParameterSetVui vui_sps;

    StdVideoH265SubLayerHrdParameters   slhdrnal[HEVC_MAX_SUB_LAYERS];
    StdVideoH265SubLayerHrdParameters   slhdrvcl[HEVC_MAX_SUB_LAYERS];

    StdVideoH265PictureParameterSet     pps;
    StdVideoH265ScalingLists            pps_scaling;

    StdVideoH265VideoParameterSet       vps;
    StdVideoH265ProfileTierLevel        ptl_vps;
    StdVideoH265DecPicBufMgr            dpbm_vps;
    StdVideoH265HrdParameters           vui_header_vps;
} VulkanH265Units;

static av_cold int base_unit_to_vk(AVCodecContext *avctx,
                                   VulkanH265Units *vk_units)
{
    VulkanEncodeH265Context *enc = avctx->priv_data;

    H265RawSPS                           *sps = &enc->units.raw_sps;
    StdVideoH265SequenceParameterSet   *vksps = &vk_units->sps;
    StdVideoH265ShortTermRefPicSet       *str =  vk_units->str;
    StdVideoH265LongTermRefPicsSps       *ltr = &vk_units->ltr;
    StdVideoH265ProfileTierLevel         *ptl_sps = &vk_units->ptl_sps;
    StdVideoH265DecPicBufMgr            *dpbm_sps = &vk_units->dpbm_sps;

    StdVideoH265HrdParameters           *vui_header_sps = &vk_units->vui_header_sps;
    StdVideoH265SequenceParameterSetVui *vui_sps = &vk_units->vui_sps;

    StdVideoH265SubLayerHrdParameters *slhdrnal = vk_units->slhdrnal;
    StdVideoH265SubLayerHrdParameters *slhdrvcl = vk_units->slhdrvcl;

    H265RawPPS                          *pps = &enc->units.raw_pps;
    StdVideoH265PictureParameterSet   *vkpps = &vk_units->pps;

    H265RawVPS                           *vps = &enc->units.raw_vps;
    StdVideoH265VideoParameterSet      *vkvps = &vk_units->vps;
    StdVideoH265ProfileTierLevel     *ptl_vps = &vk_units->ptl_vps;
    StdVideoH265DecPicBufMgr        *dpbm_vps = &vk_units->dpbm_vps;
    StdVideoH265HrdParameters *vui_header_vps = &vk_units->vui_header_vps;

    /* SPS */
    for (int i = 0; i < HEVC_MAX_SUB_LAYERS; i++) {
        memcpy(&slhdrnal[i], &sps->vui.hrd_parameters.nal_sub_layer_hrd_parameters[i], sizeof(*slhdrnal));
        memcpy(&slhdrvcl[i], &sps->vui.hrd_parameters.vcl_sub_layer_hrd_parameters[i], sizeof(*slhdrvcl));
        slhdrnal[i].cbr_flag = 0x0;
        slhdrvcl[i].cbr_flag = 0x0;
        for (int j = 0; j < HEVC_MAX_CPB_CNT; j++) {
            slhdrnal[i].cbr_flag |= sps->vui.hrd_parameters.nal_sub_layer_hrd_parameters[i].cbr_flag[j] << i;
            slhdrvcl[i].cbr_flag |= sps->vui.hrd_parameters.vcl_sub_layer_hrd_parameters[i].cbr_flag[j] << i;
        }
    }

    *vui_header_sps = (StdVideoH265HrdParameters) {
        .flags = (StdVideoH265HrdFlags) {
            .nal_hrd_parameters_present_flag = sps->vui.hrd_parameters.nal_hrd_parameters_present_flag,
            .vcl_hrd_parameters_present_flag = sps->vui.hrd_parameters.vcl_hrd_parameters_present_flag,
            .sub_pic_hrd_params_present_flag = sps->vui.hrd_parameters.sub_pic_hrd_params_present_flag,
            .sub_pic_cpb_params_in_pic_timing_sei_flag = sps->vui.hrd_parameters.sub_pic_cpb_params_in_pic_timing_sei_flag,
            .fixed_pic_rate_general_flag = 0x0,
            .fixed_pic_rate_within_cvs_flag = 0x0,
            .low_delay_hrd_flag = 0x0,
        },
        .tick_divisor_minus2 = sps->vui.hrd_parameters.tick_divisor_minus2,
        .du_cpb_removal_delay_increment_length_minus1 = sps->vui.hrd_parameters.du_cpb_removal_delay_increment_length_minus1,
        .dpb_output_delay_du_length_minus1 = sps->vui.hrd_parameters.dpb_output_delay_du_length_minus1,
        .bit_rate_scale = sps->vui.hrd_parameters.bit_rate_scale,
        .cpb_size_scale = sps->vui.hrd_parameters.cpb_size_scale,
        .cpb_size_du_scale = sps->vui.hrd_parameters.cpb_size_du_scale,
        .initial_cpb_removal_delay_length_minus1 = sps->vui.hrd_parameters.initial_cpb_removal_delay_length_minus1,
        .au_cpb_removal_delay_length_minus1 = sps->vui.hrd_parameters.au_cpb_removal_delay_length_minus1,
        .dpb_output_delay_length_minus1 = sps->vui.hrd_parameters.dpb_output_delay_length_minus1,
        /* Reserved - 3*16 bits */
        .pSubLayerHrdParametersNal = slhdrnal,
        .pSubLayerHrdParametersVcl = slhdrvcl,
    };

    for (int i = 0; i < HEVC_MAX_SUB_LAYERS; i++) {
        vui_header_sps->flags.fixed_pic_rate_general_flag |= sps->vui.hrd_parameters.fixed_pic_rate_general_flag[i] << i;
        vui_header_sps->flags.fixed_pic_rate_within_cvs_flag |= sps->vui.hrd_parameters.fixed_pic_rate_within_cvs_flag[i] << i;
        vui_header_sps->flags.low_delay_hrd_flag |= sps->vui.hrd_parameters.low_delay_hrd_flag[i] << i;
    }

    for (int i = 0; i < STD_VIDEO_H265_SUBLAYERS_LIST_SIZE; i++) {
        dpbm_sps->max_latency_increase_plus1[i] = sps->sps_max_latency_increase_plus1[i];
        dpbm_sps->max_dec_pic_buffering_minus1[i] = sps->sps_max_dec_pic_buffering_minus1[i];
        dpbm_sps->max_num_reorder_pics[i] = sps->sps_max_num_reorder_pics[i];
    }

    *ptl_sps = (StdVideoH265ProfileTierLevel) {
        .flags = (StdVideoH265ProfileTierLevelFlags) {
            .general_tier_flag = sps->profile_tier_level.general_tier_flag,
            .general_progressive_source_flag = sps->profile_tier_level.general_progressive_source_flag,
            .general_interlaced_source_flag = sps->profile_tier_level.general_interlaced_source_flag,
            .general_non_packed_constraint_flag = sps->profile_tier_level.general_non_packed_constraint_flag,
            .general_frame_only_constraint_flag = sps->profile_tier_level.general_frame_only_constraint_flag,
        },
        .general_profile_idc = ff_vk_h265_profile_to_vk(sps->profile_tier_level.general_profile_idc),
        .general_level_idc = ff_vk_h265_level_to_vk(sps->profile_tier_level.general_level_idc),
    };

    for (int i = 0; i < STD_VIDEO_H265_MAX_SHORT_TERM_REF_PIC_SETS; i++) {
        const H265RawSTRefPicSet *st_rps = &sps->st_ref_pic_set[i];

        str[i] = (StdVideoH265ShortTermRefPicSet) {
            .flags = (StdVideoH265ShortTermRefPicSetFlags) {
                .inter_ref_pic_set_prediction_flag = st_rps->inter_ref_pic_set_prediction_flag,
                .delta_rps_sign = st_rps->delta_rps_sign,
            },
            .delta_idx_minus1 = st_rps->delta_idx_minus1,
            .use_delta_flag = 0x0,
            .abs_delta_rps_minus1 = st_rps->abs_delta_rps_minus1,
            .used_by_curr_pic_flag    = 0x0,
            .used_by_curr_pic_s0_flag = 0x0,
            .used_by_curr_pic_s1_flag = 0x0,
            /* Reserved */
            /* Reserved */
            /* Reserved */
            .num_negative_pics = st_rps->num_negative_pics,
            .num_positive_pics = st_rps->num_positive_pics,
        };

        for (int j = 0; j < HEVC_MAX_REFS; j++) {
            str[i].use_delta_flag |= st_rps->use_delta_flag[j] << i;
            str[i].used_by_curr_pic_flag |= st_rps->used_by_curr_pic_flag[j] << i;
            str[i].used_by_curr_pic_s0_flag |= st_rps->used_by_curr_pic_s0_flag[j] << i;
            str[i].used_by_curr_pic_s1_flag |= st_rps->used_by_curr_pic_s1_flag[j] << i;
            str[i].delta_poc_s0_minus1[j] = st_rps->delta_poc_s0_minus1[j];
            str[i].delta_poc_s1_minus1[j] = st_rps->delta_poc_s1_minus1[j];
        }
    }

    ltr->used_by_curr_pic_lt_sps_flag = 0;
    for (int i = 0; i < STD_VIDEO_H265_MAX_LONG_TERM_REF_PICS_SPS; i++) {
        ltr->used_by_curr_pic_lt_sps_flag |= sps->lt_ref_pic_poc_lsb_sps[i] << i;
        ltr->lt_ref_pic_poc_lsb_sps[i] = sps->lt_ref_pic_poc_lsb_sps[i];
    }

    *vksps = (StdVideoH265SequenceParameterSet) {
        .flags = (StdVideoH265SpsFlags) {
            .sps_temporal_id_nesting_flag = sps->sps_temporal_id_nesting_flag,
            .separate_colour_plane_flag = sps->separate_colour_plane_flag,
            .conformance_window_flag = sps->conformance_window_flag,
            .sps_sub_layer_ordering_info_present_flag = sps->sps_sub_layer_ordering_info_present_flag,
            .scaling_list_enabled_flag = sps->scaling_list_enabled_flag,
            .sps_scaling_list_data_present_flag = sps->sps_scaling_list_data_present_flag,
            .amp_enabled_flag = sps->amp_enabled_flag,
            .sample_adaptive_offset_enabled_flag = sps->sample_adaptive_offset_enabled_flag,
            .pcm_enabled_flag = sps->pcm_enabled_flag,
            .pcm_loop_filter_disabled_flag = sps->pcm_loop_filter_disabled_flag,
            .long_term_ref_pics_present_flag = sps->long_term_ref_pics_present_flag,
            .sps_temporal_mvp_enabled_flag = sps->sps_temporal_mvp_enabled_flag,
            .strong_intra_smoothing_enabled_flag = sps->strong_intra_smoothing_enabled_flag,
            .vui_parameters_present_flag = sps->vui_parameters_present_flag,
            .sps_extension_present_flag = sps->sps_extension_present_flag,
            .sps_range_extension_flag = sps->sps_range_extension_flag,
            .transform_skip_rotation_enabled_flag = sps->transform_skip_rotation_enabled_flag,
            .transform_skip_context_enabled_flag = sps->transform_skip_context_enabled_flag,
            .implicit_rdpcm_enabled_flag = sps->implicit_rdpcm_enabled_flag,
            .explicit_rdpcm_enabled_flag = sps->explicit_rdpcm_enabled_flag,
            .extended_precision_processing_flag = sps->extended_precision_processing_flag,
            .intra_smoothing_disabled_flag = sps->intra_smoothing_disabled_flag,
            .high_precision_offsets_enabled_flag = sps->high_precision_offsets_enabled_flag,
            .persistent_rice_adaptation_enabled_flag = sps->persistent_rice_adaptation_enabled_flag,
            .cabac_bypass_alignment_enabled_flag = sps->cabac_bypass_alignment_enabled_flag,
            .sps_scc_extension_flag = sps->sps_scc_extension_flag,
            .sps_curr_pic_ref_enabled_flag = sps->sps_curr_pic_ref_enabled_flag,
            .palette_mode_enabled_flag = sps->palette_mode_enabled_flag,
            .sps_palette_predictor_initializers_present_flag = sps->sps_palette_predictor_initializer_present_flag,
            .intra_boundary_filtering_disabled_flag = sps->intra_boundary_filtering_disable_flag,
        },
        .chroma_format_idc = sps->chroma_format_idc,
        .pic_width_in_luma_samples = sps->pic_width_in_luma_samples,
        .pic_height_in_luma_samples = sps->pic_height_in_luma_samples,
        .sps_video_parameter_set_id = sps->sps_video_parameter_set_id,
        .sps_max_sub_layers_minus1 = sps->sps_max_sub_layers_minus1,
        .sps_seq_parameter_set_id = sps->sps_seq_parameter_set_id,
        .bit_depth_luma_minus8 = sps->bit_depth_luma_minus8,
        .bit_depth_chroma_minus8 = sps->bit_depth_chroma_minus8,
        .log2_max_pic_order_cnt_lsb_minus4 = sps->log2_max_pic_order_cnt_lsb_minus4,
        .log2_min_luma_coding_block_size_minus3 = sps->log2_min_luma_coding_block_size_minus3,
        .log2_diff_max_min_luma_coding_block_size = sps->log2_diff_max_min_luma_coding_block_size,
        .log2_min_luma_transform_block_size_minus2 = sps->log2_min_luma_transform_block_size_minus2,
        .log2_diff_max_min_luma_transform_block_size = sps->log2_diff_max_min_luma_transform_block_size,
        .max_transform_hierarchy_depth_inter = sps->max_transform_hierarchy_depth_inter,
        .max_transform_hierarchy_depth_intra = sps->max_transform_hierarchy_depth_intra,
        .num_short_term_ref_pic_sets = sps->num_short_term_ref_pic_sets,
        .num_long_term_ref_pics_sps = sps->num_long_term_ref_pics_sps,
        .pcm_sample_bit_depth_luma_minus1 = sps->pcm_sample_bit_depth_luma_minus1,
        .pcm_sample_bit_depth_chroma_minus1 = sps->pcm_sample_bit_depth_chroma_minus1,
        .log2_min_pcm_luma_coding_block_size_minus3 = sps->log2_min_pcm_luma_coding_block_size_minus3,
        .log2_diff_max_min_pcm_luma_coding_block_size = sps->log2_diff_max_min_pcm_luma_coding_block_size,
        /* Reserved */
        /* Reserved */
        .palette_max_size = sps->palette_max_size,
        .delta_palette_max_predictor_size = sps->delta_palette_max_predictor_size,
        .motion_vector_resolution_control_idc = sps->motion_vector_resolution_control_idc,
        .sps_num_palette_predictor_initializers_minus1 = sps->sps_num_palette_predictor_initializer_minus1,
        .conf_win_left_offset = sps->conf_win_left_offset,
        .conf_win_right_offset = sps->conf_win_right_offset,
        .conf_win_top_offset = sps->conf_win_top_offset,
        .conf_win_bottom_offset = sps->conf_win_bottom_offset,
        .pProfileTierLevel = ptl_sps,
        .pDecPicBufMgr = dpbm_sps,
        .pScalingLists = NULL,
        .pShortTermRefPicSet = str,
        .pLongTermRefPicsSps = ltr,
        .pSequenceParameterSetVui = vui_sps,
        .pPredictorPaletteEntries = NULL,
    };

    /* PPS */
    *vkpps = (StdVideoH265PictureParameterSet) {
        .flags = (StdVideoH265PpsFlags) {
            .dependent_slice_segments_enabled_flag = pps->dependent_slice_segments_enabled_flag,
            .output_flag_present_flag = pps->output_flag_present_flag,
            .sign_data_hiding_enabled_flag = pps->sign_data_hiding_enabled_flag,
            .cabac_init_present_flag = pps->cabac_init_present_flag,
            .constrained_intra_pred_flag = pps->constrained_intra_pred_flag,
            .transform_skip_enabled_flag = pps->transform_skip_enabled_flag,
            .cu_qp_delta_enabled_flag = pps->cu_qp_delta_enabled_flag,
            .pps_slice_chroma_qp_offsets_present_flag = pps->pps_slice_chroma_qp_offsets_present_flag,
            .weighted_pred_flag = pps->weighted_pred_flag,
            .weighted_bipred_flag = pps->weighted_bipred_flag,
            .transquant_bypass_enabled_flag = pps->transquant_bypass_enabled_flag,
            .tiles_enabled_flag = pps->tiles_enabled_flag,
            .entropy_coding_sync_enabled_flag = pps->entropy_coding_sync_enabled_flag,
            .uniform_spacing_flag = pps->uniform_spacing_flag,
            .loop_filter_across_tiles_enabled_flag = pps->loop_filter_across_tiles_enabled_flag,
            .pps_loop_filter_across_slices_enabled_flag = pps->pps_loop_filter_across_slices_enabled_flag,
            .deblocking_filter_control_present_flag = pps->deblocking_filter_control_present_flag,
            .deblocking_filter_override_enabled_flag = pps->deblocking_filter_override_enabled_flag,
            .pps_deblocking_filter_disabled_flag = pps->pps_deblocking_filter_disabled_flag,
            .pps_scaling_list_data_present_flag = pps->pps_scaling_list_data_present_flag,
            .lists_modification_present_flag = pps->lists_modification_present_flag,
            .slice_segment_header_extension_present_flag = pps->slice_segment_header_extension_present_flag,
            .pps_extension_present_flag = pps->pps_extension_present_flag,
            .cross_component_prediction_enabled_flag = pps->cross_component_prediction_enabled_flag,
            .chroma_qp_offset_list_enabled_flag = pps->chroma_qp_offset_list_enabled_flag,
            .pps_curr_pic_ref_enabled_flag = pps->pps_curr_pic_ref_enabled_flag,
            .residual_adaptive_colour_transform_enabled_flag = pps->residual_adaptive_colour_transform_enabled_flag,
            .pps_slice_act_qp_offsets_present_flag = pps->pps_slice_act_qp_offsets_present_flag,
            .pps_palette_predictor_initializers_present_flag = pps->pps_palette_predictor_initializer_present_flag,
            .monochrome_palette_flag = pps->monochrome_palette_flag,
            .pps_range_extension_flag = pps->pps_range_extension_flag,
        },
        .pps_pic_parameter_set_id = pps->pps_pic_parameter_set_id,
        .pps_seq_parameter_set_id = pps->pps_seq_parameter_set_id,
        .sps_video_parameter_set_id = sps->sps_video_parameter_set_id,
        .num_extra_slice_header_bits = pps->num_extra_slice_header_bits,
        .num_ref_idx_l0_default_active_minus1 = pps->num_ref_idx_l0_default_active_minus1,
        .num_ref_idx_l1_default_active_minus1 = pps->num_ref_idx_l1_default_active_minus1,
        .init_qp_minus26 = pps->init_qp_minus26,
        .diff_cu_qp_delta_depth = pps->diff_cu_qp_delta_depth,
        .pps_cb_qp_offset = pps->pps_cb_qp_offset,
        .pps_cr_qp_offset = pps->pps_cr_qp_offset,
        .pps_beta_offset_div2 = pps->pps_beta_offset_div2,
        .pps_tc_offset_div2 = pps->pps_tc_offset_div2,
        .log2_parallel_merge_level_minus2 = pps->log2_parallel_merge_level_minus2,
        .log2_max_transform_skip_block_size_minus2 = pps->log2_max_transform_skip_block_size_minus2,
        .diff_cu_chroma_qp_offset_depth = pps->diff_cu_chroma_qp_offset_depth,
        .chroma_qp_offset_list_len_minus1 = pps->chroma_qp_offset_list_len_minus1,
        .log2_sao_offset_scale_luma = pps->log2_sao_offset_scale_luma,
        .log2_sao_offset_scale_chroma = pps->log2_sao_offset_scale_chroma,
        .pps_act_y_qp_offset_plus5 = pps->pps_act_y_qp_offset_plus5,
        .pps_act_cb_qp_offset_plus5 = pps->pps_act_cb_qp_offset_plus5,
        .pps_act_cr_qp_offset_plus3 = pps->pps_act_cr_qp_offset_plus3,
        .pps_num_palette_predictor_initializers = pps->pps_num_palette_predictor_initializer,
        .luma_bit_depth_entry_minus8 = pps->luma_bit_depth_entry_minus8,
        .chroma_bit_depth_entry_minus8 = pps->chroma_bit_depth_entry_minus8,
        .num_tile_columns_minus1 = pps->num_tile_columns_minus1,
        .num_tile_rows_minus1 = pps->num_tile_rows_minus1,
        .pScalingLists = NULL,
        .pPredictorPaletteEntries = NULL,
    };

    for (int i = 0; i < pps->num_tile_columns_minus1; i++)
        vkpps->column_width_minus1[i] = pps->column_width_minus1[i];

    for (int i = 0; i < pps->num_tile_rows_minus1; i++)
        vkpps->row_height_minus1[i] = pps->row_height_minus1[i];

    for (int i = 0; i <= pps->chroma_qp_offset_list_len_minus1; i++) {
        vkpps->cb_qp_offset_list[i] = pps->cb_qp_offset_list[i];
        vkpps->cr_qp_offset_list[i] = pps->cr_qp_offset_list[i];
    }

    /* VPS */
    for (int i = 0; i < STD_VIDEO_H265_SUBLAYERS_LIST_SIZE; i++) {
        dpbm_vps->max_latency_increase_plus1[i] = vps->vps_max_latency_increase_plus1[i];
        dpbm_vps->max_dec_pic_buffering_minus1[i] = vps->vps_max_dec_pic_buffering_minus1[i];
        dpbm_vps->max_num_reorder_pics[i] = vps->vps_max_num_reorder_pics[i];
    }

    *ptl_vps = (StdVideoH265ProfileTierLevel) {
        .flags = (StdVideoH265ProfileTierLevelFlags) {
            .general_tier_flag = vps->profile_tier_level.general_tier_flag,
            .general_progressive_source_flag = vps->profile_tier_level.general_progressive_source_flag,
            .general_interlaced_source_flag = vps->profile_tier_level.general_interlaced_source_flag,
            .general_non_packed_constraint_flag = vps->profile_tier_level.general_non_packed_constraint_flag,
            .general_frame_only_constraint_flag = vps->profile_tier_level.general_frame_only_constraint_flag,
        },
        .general_profile_idc = ff_vk_h265_profile_to_vk(vps->profile_tier_level.general_profile_idc),
        .general_level_idc = ff_vk_h265_level_to_vk(vps->profile_tier_level.general_level_idc),
    };

    *vkvps = (StdVideoH265VideoParameterSet) {
        .flags = (StdVideoH265VpsFlags) {
            .vps_temporal_id_nesting_flag = vps->vps_temporal_id_nesting_flag,
            .vps_sub_layer_ordering_info_present_flag = vps->vps_sub_layer_ordering_info_present_flag,
            .vps_timing_info_present_flag = vps->vps_timing_info_present_flag,
            .vps_poc_proportional_to_timing_flag = vps->vps_poc_proportional_to_timing_flag,
        },
        .vps_video_parameter_set_id = vps->vps_video_parameter_set_id,
        .vps_max_sub_layers_minus1 = vps->vps_max_sub_layers_minus1,
        /* Reserved */
        /* Reserved */
        .vps_num_units_in_tick = vps->vps_num_units_in_tick,
        .vps_time_scale = vps->vps_time_scale,
        .vps_num_ticks_poc_diff_one_minus1 = vps->vps_num_ticks_poc_diff_one_minus1,
        /* Reserved */
        .pDecPicBufMgr = dpbm_vps,
        .pHrdParameters = vui_header_vps,
        .pProfileTierLevel = ptl_vps,
    };

    return 0;
}

static int create_session_params(AVCodecContext *avctx)
{
    int err;
    VulkanEncodeH265Context *enc = avctx->priv_data;
    FFVulkanEncodeContext *ctx = &enc->common;
    FFVulkanContext *s = &ctx->s;
    FFVulkanFunctions *vk = &ctx->s.vkfn;

    VulkanH265Units vk_units = { 0 };

    VkVideoEncodeH265SessionParametersAddInfoKHR h265_params_info;
    VkVideoEncodeH265SessionParametersCreateInfoKHR h265_params;

    /* Convert it to Vulkan */
    err = base_unit_to_vk(avctx, &vk_units);
    if (err < 0) {
        av_log(avctx, AV_LOG_ERROR, "Unable to convert SPS/PPS units to Vulkan: %s\n",
               av_err2str(err));
        return err;
    }

    /* Destroy the session params */
    if (ctx->session_params)
        vk->DestroyVideoSessionParametersKHR(s->hwctx->act_dev,
                                             ctx->session_params,
                                             s->hwctx->alloc);

    h265_params_info = (VkVideoEncodeH265SessionParametersAddInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_SESSION_PARAMETERS_ADD_INFO_KHR,
        .pStdSPSs = &vk_units.sps,
        .stdSPSCount = 1,
        .pStdPPSs = &vk_units.pps,
        .stdPPSCount = 1,
        .pStdVPSs = &vk_units.vps,
        .stdVPSCount = 1,
    };
    h265_params = (VkVideoEncodeH265SessionParametersCreateInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_SESSION_PARAMETERS_CREATE_INFO_KHR,
        .maxStdSPSCount = 1,
        .maxStdPPSCount = 1,
        .maxStdVPSCount = 1,
        .pParametersAddInfo = &h265_params_info,
    };

    return ff_vulkan_encode_create_session_params(avctx, ctx, &h265_params);
}

static int parse_feedback_units(AVCodecContext *avctx,
                                const uint8_t *data, size_t size,
                                int sps_override, int pps_override)
{
    int err;
    VulkanEncodeH265Context *enc = avctx->priv_data;

    CodedBitstreamContext *cbs;
    CodedBitstreamFragment au = { 0 };

    err = ff_cbs_init(&cbs, AV_CODEC_ID_HEVC, avctx);
    if (err < 0)
        return err;

    err = ff_cbs_read(cbs, &au, data, size);
    if (err < 0) {
        av_log(avctx, AV_LOG_ERROR, "Unable to parse feedback units, bad drivers: %s\n",
               av_err2str(err));
        return err;
    }

    if (sps_override) {
        for (int i = 0; i < au.nb_units; i++) {
            if (au.units[i].type == HEVC_NAL_SPS) {
                H265RawSPS *sps = au.units[i].content;
                enc->units.raw_sps.pic_width_in_luma_samples = sps->pic_width_in_luma_samples;
                enc->units.raw_sps.pic_height_in_luma_samples = sps->pic_height_in_luma_samples;
                enc->units.raw_sps.log2_diff_max_min_luma_coding_block_size = sps->log2_diff_max_min_luma_coding_block_size;
                enc->units.raw_sps.max_transform_hierarchy_depth_inter = sps->max_transform_hierarchy_depth_inter;
                enc->units.raw_sps.max_transform_hierarchy_depth_intra = sps->max_transform_hierarchy_depth_intra;
            }
        }
    }

    /* If PPS has an override, just copy it entirely. */
    if (pps_override) {
        for (int i = 0; i < au.nb_units; i++) {
            if (au.units[i].type == HEVC_NAL_PPS) {
                H265RawPPS *pps = au.units[i].content;
                memcpy(&enc->units.raw_pps, pps, sizeof(*pps));
                enc->fixed_qp_idr = pps->init_qp_minus26 + 26;
                break;
            }
        }
    }

    ff_cbs_fragment_free(&au);
    ff_cbs_close(&cbs);

    return 0;
}

static int init_base_units(AVCodecContext *avctx)
{
    int err;
    VkResult ret;
    VulkanEncodeH265Context *enc = avctx->priv_data;
    FFVulkanEncodeContext *ctx = &enc->common;
    FFVulkanContext *s = &ctx->s;
    FFVulkanFunctions *vk = &ctx->s.vkfn;

    VkVideoEncodeH265SessionParametersGetInfoKHR h265_params_info;
    VkVideoEncodeSessionParametersGetInfoKHR params_info;
    VkVideoEncodeH265SessionParametersFeedbackInfoKHR h265_params_feedback;
    VkVideoEncodeSessionParametersFeedbackInfoKHR params_feedback;

    void *data = NULL;
    size_t data_size = 0;

    /* Generate SPS/PPS unit info */
    err = init_sequence_headers(avctx);
    if (err < 0) {
        av_log(avctx, AV_LOG_ERROR, "Unable to initialize SPS/PPS units: %s\n",
               av_err2str(err));
        return err;
    }

    /* Create session parameters from them */
    err = create_session_params(avctx);
    if (err < 0)
        return err;

    h265_params_info = (VkVideoEncodeH265SessionParametersGetInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_SESSION_PARAMETERS_GET_INFO_KHR,
        .writeStdSPS = 1,
        .writeStdPPS = 1,
        .writeStdVPS = 1,
        .stdSPSId = enc->units.raw_sps.sps_seq_parameter_set_id,
        .stdPPSId = enc->units.raw_pps.pps_pic_parameter_set_id,
        .stdVPSId = enc->units.raw_vps.vps_video_parameter_set_id,
    };
    params_info = (VkVideoEncodeSessionParametersGetInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_SESSION_PARAMETERS_GET_INFO_KHR,
        .pNext = &h265_params_info,
        .videoSessionParameters = ctx->session_params,
    };

    h265_params_feedback = (VkVideoEncodeH265SessionParametersFeedbackInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_SESSION_PARAMETERS_FEEDBACK_INFO_KHR,
    };
    params_feedback = (VkVideoEncodeSessionParametersFeedbackInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_SESSION_PARAMETERS_FEEDBACK_INFO_KHR,
        .pNext = &h265_params_feedback,
    };

    ret = vk->GetEncodedVideoSessionParametersKHR(s->hwctx->act_dev, &params_info,
                                                  &params_feedback,
                                                  &data_size, data);
    if (ret == VK_INCOMPLETE ||
        (ret == VK_SUCCESS) && (data_size > 0)) {
        data = av_mallocz(data_size);
        if (!data)
            return AVERROR(ENOMEM);
    } else {
        av_log(avctx, AV_LOG_ERROR, "Unable to get feedback for H.265 units = %"SIZE_SPECIFIER"\n", data_size);
        return err;
    }

    ret = vk->GetEncodedVideoSessionParametersKHR(s->hwctx->act_dev, &params_info,
                                                  &params_feedback,
                                                  &data_size, data);
    if (ret != VK_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Error writing feedback units\n");
        return err;
    }

    av_log(avctx, AV_LOG_VERBOSE, "Feedback units written, overrides: %i (SPS: %i PPS: %i VPS: %i)\n",
           params_feedback.hasOverrides,
           h265_params_feedback.hasStdSPSOverrides,
           h265_params_feedback.hasStdPPSOverrides,
           h265_params_feedback.hasStdVPSOverrides);

    params_feedback.hasOverrides = 1;
    h265_params_feedback.hasStdSPSOverrides = 1;
    h265_params_feedback.hasStdPPSOverrides = 1;

    /* No need to sync any overrides */
    if (!params_feedback.hasOverrides)
        return 0;

    /* Parse back tne units and override */
    err = parse_feedback_units(avctx, data, data_size,
                               h265_params_feedback.hasStdSPSOverrides,
                               h265_params_feedback.hasStdPPSOverrides);
    if (err < 0)
        return err;

    /* Create final session parameters */
    err = create_session_params(avctx);
    if (err < 0)
        return err;

    return 0;
}

static int vulkan_encode_h265_add_nal(AVCodecContext *avctx,
                                      CodedBitstreamFragment *au,
                                      void *nal_unit)
{
    H265RawNALUnitHeader *header = nal_unit;

    int err = ff_cbs_insert_unit_content(au, -1,
                                         header->nal_unit_type, nal_unit, NULL);
    if (err < 0)
        av_log(avctx, AV_LOG_ERROR, "Failed to add NAL unit: "
               "type = %d.\n", header->nal_unit_type);

    return err;
}

static int write_access_unit(AVCodecContext *avctx,
                             uint8_t *data, size_t *data_len,
                             CodedBitstreamFragment *au)
{
    VulkanEncodeH265Context *enc = avctx->priv_data;

    int err = ff_cbs_write_fragment_data(enc->cbs, au);
    if (err < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to write packed header.\n");
        return err;
    }

    if (*data_len < au->data_size) {
        av_log(avctx, AV_LOG_ERROR, "Access unit too large: %zu < %zu.\n",
               *data_len, au->data_size);
        return AVERROR(ENOSPC);
    }

    memcpy(data, au->data, au->data_size);
    *data_len = au->data_size;

    return 0;
}

static int write_sequence_headers(AVCodecContext *avctx,
                                  FFHWBaseEncodePicture *base_pic,
                                  uint8_t *data, size_t *data_len)
{
    int err;
    VulkanEncodeH265Context *enc = avctx->priv_data;
    VulkanEncodeH265Picture  *hp = base_pic ? base_pic->codec_priv : NULL;
    CodedBitstreamFragment   *au = &enc->current_access_unit;

    if (hp && hp->units_needed & UNIT_AUD) {
        err = vulkan_encode_h265_add_nal(avctx, au, &enc->raw_aud);
        if (err < 0)
            goto fail;
        hp->units_needed &= ~UNIT_AUD;
    }

    err = vulkan_encode_h265_add_nal(avctx, au, &enc->units.raw_vps);
    if (err < 0)
        goto fail;

    err = vulkan_encode_h265_add_nal(avctx, au, &enc->units.raw_sps);
    if (err < 0)
        goto fail;

    err = vulkan_encode_h265_add_nal(avctx, au, &enc->units.raw_pps);
    if (err < 0)
        goto fail;

    err = write_access_unit(avctx, data, data_len, au);
fail:
    ff_cbs_fragment_reset(au);
    return err;
}

static int write_extra_headers(AVCodecContext *avctx,
                               FFHWBaseEncodePicture *base_pic,
                               uint8_t *data, size_t *data_len)
{
    int err;
    VulkanEncodeH265Context *enc = avctx->priv_data;
    VulkanEncodeH265Picture  *hp = base_pic->codec_priv;
    CodedBitstreamFragment   *au = &enc->current_access_unit;

    if (hp->units_needed & UNIT_AUD) {
        err = vulkan_encode_h265_add_nal(avctx, au, &enc->raw_aud);
        if (err < 0)
            goto fail;
    }

    if (hp->units_needed & UNIT_SEI_MASTERING_DISPLAY) {
        err = ff_cbs_sei_add_message(enc->cbs, au, 1,
                                     SEI_TYPE_MASTERING_DISPLAY_COLOUR_VOLUME,
                                     &enc->sei_mastering_display, NULL);
        if (err < 0)
            goto fail;
    }

    if (hp->units_needed & UNIT_SEI_CONTENT_LIGHT_LEVEL) {
        err = ff_cbs_sei_add_message(enc->cbs, au, 1,
                                     SEI_TYPE_CONTENT_LIGHT_LEVEL_INFO,
                                     &enc->sei_content_light_level, NULL);
        if (err < 0)
            goto fail;
    }
    if (hp->units_needed & UNIT_SEI_A53_CC) {
        err = ff_cbs_sei_add_message(enc->cbs, au, 1,
                                     SEI_TYPE_USER_DATA_REGISTERED_ITU_T_T35,
                                     &enc->sei_a53cc, NULL);
        if (err < 0)
            goto fail;
    }

    if (hp->units_needed) {
        err = write_access_unit(avctx, data, data_len, au);
        if (err < 0)
            goto fail;
    } else {
        *data_len = 0;
    }

fail:
    ff_cbs_fragment_reset(au);
    return err;
}

static int write_filler(AVCodecContext *avctx, uint32_t filler,
                        uint8_t *data, size_t *data_len)
{
    int err;
    VulkanEncodeH265Context *enc = avctx->priv_data;
    CodedBitstreamFragment   *au = &enc->current_access_unit;

    H265RawFiller raw_filler = {
        .nal_unit_header =
        {
            .nal_unit_type = HEVC_NAL_FD_NUT,
            .nuh_temporal_id_plus1 = 1,
        },
        .filler_size = filler,
    };

    err = vulkan_encode_h265_add_nal(avctx, au, &raw_filler);
    if (err < 0)
        goto fail;

    err = write_access_unit(avctx, data, data_len, au);
fail:
    ff_cbs_fragment_reset(au);
    return err;
}

static const FFVulkanCodec enc_cb = {
    .flags = FF_HW_FLAG_B_PICTURES |
             FF_HW_FLAG_B_PICTURE_REFERENCES |
             FF_HW_FLAG_NON_IDR_KEY_PICTURES,
    .picture_priv_data_size = sizeof(VulkanEncodeH265Picture),
    .filler_header_size = 7,
    .init_profile = init_profile,
    .init_pic_rc = init_pic_rc,
    .init_pic_params = init_pic_params,
    .write_sequence_headers = write_sequence_headers,
    .write_extra_headers = write_extra_headers,
    .write_filler = write_filler,
};

static av_cold int vulkan_encode_h265_init(AVCodecContext *avctx)
{
    int err, ref_l0, ref_l1;
    VulkanEncodeH265Context *enc = avctx->priv_data;
    FFVulkanEncodeContext *ctx = &enc->common;
    FFHWBaseEncodeContext *base_ctx = &ctx->base;
    int flags;

    if (avctx->profile == AV_PROFILE_UNKNOWN)
        avctx->profile = enc->common.opts.profile;

    enc->caps = (VkVideoEncodeH265CapabilitiesKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_CAPABILITIES_KHR,
    };

    enc->quality_props = (VkVideoEncodeH265QualityLevelPropertiesKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_QUALITY_LEVEL_PROPERTIES_KHR,
    };

    err = ff_vulkan_encode_init(avctx, &enc->common,
                                &ff_vk_enc_h265_desc, &enc_cb,
                                &enc->caps, &enc->quality_props);
    if (err < 0)
        return err;

    av_log(avctx, AV_LOG_VERBOSE, "H265 encoder capabilities:\n");
    av_log(avctx, AV_LOG_VERBOSE, "    Standard capability flags:\n");
    av_log(avctx, AV_LOG_VERBOSE, "        separate_color_plane: %i\n",
           !!(enc->caps.stdSyntaxFlags & VK_VIDEO_ENCODE_H265_STD_SEPARATE_COLOR_PLANE_FLAG_SET_BIT_KHR));
    av_log(avctx, AV_LOG_VERBOSE, "        sample_adaptive_offset: %i\n",
           !!(enc->caps.stdSyntaxFlags & VK_VIDEO_ENCODE_H265_STD_SAMPLE_ADAPTIVE_OFFSET_ENABLED_FLAG_SET_BIT_KHR));
    av_log(avctx, AV_LOG_VERBOSE, "        scaling_lists: %i\n",
           !!(enc->caps.stdSyntaxFlags & VK_VIDEO_ENCODE_H265_STD_SCALING_LIST_DATA_PRESENT_FLAG_SET_BIT_KHR));
    av_log(avctx, AV_LOG_VERBOSE, "        pcm: %i\n",
           !!(enc->caps.stdSyntaxFlags & VK_VIDEO_ENCODE_H265_STD_PCM_ENABLED_FLAG_SET_BIT_KHR));
    av_log(avctx, AV_LOG_VERBOSE, "        temporal_mvp: %i\n",
           !!(enc->caps.stdSyntaxFlags & VK_VIDEO_ENCODE_H265_STD_SPS_TEMPORAL_MVP_ENABLED_FLAG_SET_BIT_KHR));
    av_log(avctx, AV_LOG_VERBOSE, "        init_qp: %i\n",
           !!(enc->caps.stdSyntaxFlags & VK_VIDEO_ENCODE_H265_STD_INIT_QP_MINUS26_BIT_KHR));
    av_log(avctx, AV_LOG_VERBOSE, "        weighted:%s%s\n",
           enc->caps.stdSyntaxFlags & VK_VIDEO_ENCODE_H265_STD_WEIGHTED_PRED_FLAG_SET_BIT_KHR ?
               " pred" : "",
           enc->caps.stdSyntaxFlags & VK_VIDEO_ENCODE_H265_STD_WEIGHTED_BIPRED_FLAG_SET_BIT_KHR ?
               " bipred" : "");
    av_log(avctx, AV_LOG_VERBOSE, "        parallel_merge_level: %i\n",
           !!(enc->caps.stdSyntaxFlags & VK_VIDEO_ENCODE_H265_STD_LOG2_PARALLEL_MERGE_LEVEL_MINUS2_BIT_KHR));
    av_log(avctx, AV_LOG_VERBOSE, "        sign_data_hiding: %i\n",
           !!(enc->caps.stdSyntaxFlags & VK_VIDEO_ENCODE_H265_STD_SIGN_DATA_HIDING_ENABLED_FLAG_SET_BIT_KHR));
    av_log(avctx, AV_LOG_VERBOSE, "        transform_skip:%s%s\n",
           enc->caps.stdSyntaxFlags & VK_VIDEO_ENCODE_H265_STD_TRANSFORM_SKIP_ENABLED_FLAG_SET_BIT_KHR ?
           " set" : "",
           enc->caps.stdSyntaxFlags & VK_VIDEO_ENCODE_H265_STD_TRANSFORM_SKIP_ENABLED_FLAG_UNSET_BIT_KHR ?
           " unset" : "");
    av_log(avctx, AV_LOG_VERBOSE, "        slice_chroma_qp_offsets: %i\n",
           !!(enc->caps.stdSyntaxFlags & VK_VIDEO_ENCODE_H265_STD_PPS_SLICE_CHROMA_QP_OFFSETS_PRESENT_FLAG_SET_BIT_KHR));
    av_log(avctx, AV_LOG_VERBOSE, "        transquant_bypass: %i\n",
           !!(enc->caps.stdSyntaxFlags & VK_VIDEO_ENCODE_H265_STD_TRANSQUANT_BYPASS_ENABLED_FLAG_SET_BIT_KHR));
    av_log(avctx, AV_LOG_VERBOSE, "        constrained_intra_pred: %i\n",
           !!(enc->caps.stdSyntaxFlags & VK_VIDEO_ENCODE_H265_STD_CONSTRAINED_INTRA_PRED_FLAG_SET_BIT_KHR));
    av_log(avctx, AV_LOG_VERBOSE, "        entrypy_coding_sync: %i\n",
           !!(enc->caps.stdSyntaxFlags & VK_VIDEO_ENCODE_H265_STD_ENTROPY_CODING_SYNC_ENABLED_FLAG_SET_BIT_KHR));
    av_log(avctx, AV_LOG_VERBOSE, "        dependent_slice_segment:%s%s\n",
           enc->caps.stdSyntaxFlags & VK_VIDEO_ENCODE_H265_STD_DEPENDENT_SLICE_SEGMENTS_ENABLED_FLAG_SET_BIT_KHR ?
               " enabled" : "",
           enc->caps.stdSyntaxFlags & VK_VIDEO_ENCODE_H265_STD_DEPENDENT_SLICE_SEGMENT_FLAG_SET_BIT_KHR ?
               " set" : "");
    av_log(avctx, AV_LOG_VERBOSE, "        slice_qp_delta: %i\n",
           !!(enc->caps.stdSyntaxFlags & VK_VIDEO_ENCODE_H265_STD_SLICE_QP_DELTA_BIT_KHR));
    av_log(avctx, AV_LOG_VERBOSE, "        different_slice_qp_delta: %i\n",
           !!(enc->caps.stdSyntaxFlags & VK_VIDEO_ENCODE_H265_STD_DIFFERENT_SLICE_QP_DELTA_BIT_KHR));

    av_log(avctx, AV_LOG_VERBOSE, "    Capability flags:\n");
    av_log(avctx, AV_LOG_VERBOSE, "        hdr_compliance: %i\n",
           !!(enc->caps.flags & VK_VIDEO_ENCODE_H264_CAPABILITY_HRD_COMPLIANCE_BIT_KHR));
    av_log(avctx, AV_LOG_VERBOSE, "        pred_weight_table_generated: %i\n",
           !!(enc->caps.flags & VK_VIDEO_ENCODE_H264_CAPABILITY_PREDICTION_WEIGHT_TABLE_GENERATED_BIT_KHR));
    av_log(avctx, AV_LOG_VERBOSE, "        row_unaligned_slice: %i\n",
           !!(enc->caps.flags & VK_VIDEO_ENCODE_H264_CAPABILITY_ROW_UNALIGNED_SLICE_BIT_KHR));
    av_log(avctx, AV_LOG_VERBOSE, "        different_slice_type: %i\n",
           !!(enc->caps.flags & VK_VIDEO_ENCODE_H264_CAPABILITY_DIFFERENT_SLICE_TYPE_BIT_KHR));
    av_log(avctx, AV_LOG_VERBOSE, "        b_frame_in_l0_list: %i\n",
           !!(enc->caps.flags & VK_VIDEO_ENCODE_H264_CAPABILITY_B_FRAME_IN_L0_LIST_BIT_KHR));
    av_log(avctx, AV_LOG_VERBOSE, "        b_frame_in_l1_list: %i\n",
           !!(enc->caps.flags & VK_VIDEO_ENCODE_H264_CAPABILITY_B_FRAME_IN_L1_LIST_BIT_KHR));
    av_log(avctx, AV_LOG_VERBOSE, "        per_pict_type_min_max_qp: %i\n",
           !!(enc->caps.flags & VK_VIDEO_ENCODE_H264_CAPABILITY_PER_PICTURE_TYPE_MIN_MAX_QP_BIT_KHR));
    av_log(avctx, AV_LOG_VERBOSE, "        per_slice_constant_qp: %i\n",
           !!(enc->caps.flags & VK_VIDEO_ENCODE_H264_CAPABILITY_PER_SLICE_CONSTANT_QP_BIT_KHR));
    av_log(avctx, AV_LOG_VERBOSE, "        generate_prefix_nalu: %i\n",
           !!(enc->caps.flags & VK_VIDEO_ENCODE_H264_CAPABILITY_GENERATE_PREFIX_NALU_BIT_KHR));

    av_log(avctx, AV_LOG_VERBOSE, "    Capabilities:\n");
    av_log(avctx, AV_LOG_VERBOSE, "        maxLevelIdc: %i\n",
           enc->caps.maxLevelIdc);
    av_log(avctx, AV_LOG_VERBOSE, "        maxSliceCount: %i\n",
           enc->caps.maxSliceSegmentCount);
    av_log(avctx, AV_LOG_VERBOSE, "        maxTiles: %ix%i\n",
           enc->caps.maxTiles.width, enc->caps.maxTiles.height);
    av_log(avctx, AV_LOG_VERBOSE, "        cbtSizes: 0x%x\n",
           enc->caps.ctbSizes);
    av_log(avctx, AV_LOG_VERBOSE, "        transformBlockSizes: 0x%x\n",
           enc->caps.transformBlockSizes);
    av_log(avctx, AV_LOG_VERBOSE, "        max(P/B)PictureL0ReferenceCount: %i P's; %i B's\n",
           enc->caps.maxPPictureL0ReferenceCount,
           enc->caps.maxBPictureL0ReferenceCount);
    av_log(avctx, AV_LOG_VERBOSE, "        maxL1ReferenceCount: %i\n",
           enc->caps.maxL1ReferenceCount);
    av_log(avctx, AV_LOG_VERBOSE, "        maxSubLayerCount: %i\n",
           enc->caps.maxSubLayerCount);
    av_log(avctx, AV_LOG_VERBOSE, "        expectDyadicTemporalLayerPattern: %i\n",
           enc->caps.expectDyadicTemporalSubLayerPattern);
    av_log(avctx, AV_LOG_VERBOSE, "        min/max Qp: [%i, %i]\n",
           enc->caps.minQp, enc->caps.maxQp);
    av_log(avctx, AV_LOG_VERBOSE, "        prefersGopRemainingFrames: %i\n",
           enc->caps.prefersGopRemainingFrames);
    av_log(avctx, AV_LOG_VERBOSE, "        requiresGopRemainingFrames: %i\n",
           enc->caps.requiresGopRemainingFrames);

    err = init_enc_options(avctx);
    if (err < 0)
        return err;

    flags = ctx->codec->flags;
    if (!enc->caps.maxPPictureL0ReferenceCount &&
        !enc->caps.maxBPictureL0ReferenceCount &&
        !enc->caps.maxL1ReferenceCount) {
        /* Intra-only */
        flags |= FF_HW_FLAG_INTRA_ONLY;
        ref_l0 = ref_l1 = 0;
    } else if (!enc->caps.maxPPictureL0ReferenceCount) {
        /* No P-frames? How. */
        base_ctx->p_to_gpb = 1;
        ref_l0 = enc->caps.maxBPictureL0ReferenceCount;
        ref_l1 = enc->caps.maxL1ReferenceCount;
    } else if (!enc->caps.maxBPictureL0ReferenceCount &&
               !enc->caps.maxL1ReferenceCount) {
        /* No B-frames */
        flags &= ~(FF_HW_FLAG_B_PICTURES | FF_HW_FLAG_B_PICTURE_REFERENCES);
        ref_l0 = enc->caps.maxPPictureL0ReferenceCount;
        ref_l1 = 0;
    } else {
        /* P and B frames */
        ref_l0 = FFMIN(enc->caps.maxPPictureL0ReferenceCount,
                       enc->caps.maxBPictureL0ReferenceCount);
        ref_l1 = enc->caps.maxL1ReferenceCount;
    }

    err = ff_hw_base_init_gop_structure(base_ctx, avctx, ref_l0, ref_l1,
                                        flags, 0);
    if (err < 0)
        return err;

    base_ctx->output_delay = base_ctx->b_per_p;
    base_ctx->decode_delay = base_ctx->max_b_depth;

    /* Init CBS */
    err = ff_cbs_init(&enc->cbs, AV_CODEC_ID_HEVC, avctx);
    if (err < 0)
        return err;

    /* Create units and session parameters */
    err = init_base_units(avctx);
    if (err < 0)
        return err;

    /* Write out extradata */
    err = ff_vulkan_write_global_header(avctx, &enc->common);
    if (err < 0)
        return err;

    return 0;
}

static av_cold int vulkan_encode_h265_close(AVCodecContext *avctx)
{
    VulkanEncodeH265Context *enc = avctx->priv_data;
    ff_vulkan_encode_uninit(&enc->common);
    return 0;
}

#define OFFSET(x) offsetof(VulkanEncodeH265Context, x)
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM)
static const AVOption vulkan_encode_h265_options[] = {
    HW_BASE_ENCODE_COMMON_OPTIONS,
    VULKAN_ENCODE_COMMON_OPTIONS,

    { "profile", "Set profile (profile_idc and constraint_set*_flag)",
      OFFSET(common.opts.profile), AV_OPT_TYPE_INT,
      { .i64 = AV_PROFILE_UNKNOWN }, AV_PROFILE_UNKNOWN, 0xffff, FLAGS, .unit = "profile" },

#define PROFILE(name, value)  name, NULL, 0, AV_OPT_TYPE_CONST, \
      { .i64 = value }, 0, 0, FLAGS, .unit = "profile"
    { PROFILE("main",               AV_PROFILE_HEVC_MAIN) },
    { PROFILE("main10",             AV_PROFILE_HEVC_MAIN_10) },
    { PROFILE("rext",               AV_PROFILE_HEVC_REXT) },
#undef PROFILE

    { "tier", "Set tier (general_tier_flag)", OFFSET(common.opts.tier), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, FLAGS, .unit = "tier" },
        { "main", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 0 }, 0, 0, FLAGS, .unit = "tier" },
        { "high", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 1 }, 0, 0, FLAGS, .unit = "tier" },

    { "level", "Set level (general_level_idc)",
      OFFSET(common.opts.level), AV_OPT_TYPE_INT,
      { .i64 = AV_LEVEL_UNKNOWN }, AV_LEVEL_UNKNOWN, 0xff, FLAGS, .unit = "level" },

#define LEVEL(name, value) name, NULL, 0, AV_OPT_TYPE_CONST, \
      { .i64 = value }, 0, 0, FLAGS, .unit = "level"
    { LEVEL("1",    30) },
    { LEVEL("2",    60) },
    { LEVEL("2.1",  63) },
    { LEVEL("3",    90) },
    { LEVEL("3.1",  93) },
    { LEVEL("4",   120) },
    { LEVEL("4.1", 123) },
    { LEVEL("5",   150) },
    { LEVEL("5.1", 153) },
    { LEVEL("5.2", 156) },
    { LEVEL("6",   180) },
    { LEVEL("6.1", 183) },
    { LEVEL("6.2", 186) },
#undef LEVEL

    { "units", "Set units to include", OFFSET(unit_elems), AV_OPT_TYPE_FLAGS, { .i64 = UNIT_SEI_MASTERING_DISPLAY | UNIT_SEI_CONTENT_LIGHT_LEVEL | UNIT_SEI_A53_CC }, 0, INT_MAX, FLAGS, "units" },
        { "hdr",        "Include HDR metadata for mastering display colour volume and content light level information", 0, AV_OPT_TYPE_CONST, { .i64 = UNIT_SEI_MASTERING_DISPLAY | UNIT_SEI_CONTENT_LIGHT_LEVEL }, INT_MIN, INT_MAX, FLAGS, "units" },
        { "a53_cc",     "Include A/53 caption data", 0, AV_OPT_TYPE_CONST, { .i64 = UNIT_SEI_A53_CC }, INT_MIN, INT_MAX, FLAGS, "units" },

    { NULL },
};

static const FFCodecDefault vulkan_encode_h265_defaults[] = {
    { "b",              "0"   },
    { "bf",             "2"   },
    { "g",              "300" },
    { "i_qfactor",      "1"   },
    { "i_qoffset",      "0"   },
    { "b_qfactor",      "6/5" },
    { "b_qoffset",      "0"   },
    { "qmin",           "-1"  },
    { "qmax",           "-1"  },
    { NULL },
};

static const AVClass vulkan_encode_h265_class = {
    .class_name = "hevc_vulkan",
    .item_name  = av_default_item_name,
    .option     = vulkan_encode_h265_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_hevc_vulkan_encoder = {
    .p.name         = "hevc_vulkan",
    CODEC_LONG_NAME("H.265/HEVC (Vulkan)"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_HEVC,
    .priv_data_size = sizeof(VulkanEncodeH265Context),
    .init           = &vulkan_encode_h265_init,
    FF_CODEC_RECEIVE_PACKET_CB(&ff_vulkan_encode_receive_packet),
    .close          = &vulkan_encode_h265_close,
    .p.priv_class   = &vulkan_encode_h265_class,
    .p.capabilities = AV_CODEC_CAP_DELAY |
                      AV_CODEC_CAP_HARDWARE |
                      AV_CODEC_CAP_DR1 |
                      AV_CODEC_CAP_ENCODER_FLUSH |
                      AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
    .defaults       = vulkan_encode_h265_defaults,
    .p.pix_fmts = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_VULKAN,
        AV_PIX_FMT_NONE,
    },
    .hw_configs     = ff_vulkan_encode_hw_configs,
    .p.wrapper_name = "vulkan",
};
