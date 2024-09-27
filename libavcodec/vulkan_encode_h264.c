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

#include "libavutil/opt.h"
#include "libavutil/mem.h"

#include "cbs.h"
#include "cbs_h264.h"
#include "atsc_a53.h"

#include "h264_levels.h"
#include "h2645data.h"
#include "codec_internal.h"
#include "version.h"
#include "hw_base_encode_h264.h"

#include "vulkan_encode.h"

enum UnitElems {
    UNIT_AUD            = 1 << 0,
    UNIT_SEI_TIMING     = 1 << 1,
    UNIT_SEI_IDENTIFIER = 1 << 2,
    UNIT_SEI_RECOVERY   = 1 << 3,
    UNIT_SEI_A53_CC     = 1 << 4,
};

const FFVulkanEncodeDescriptor ff_vk_enc_h264_desc = {
    .codec_id         = AV_CODEC_ID_H264,
    .encode_extension = FF_VK_EXT_VIDEO_ENCODE_H264,
    .encode_op        = VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR,
    .ext_props = {
        .extensionName = VK_STD_VULKAN_VIDEO_CODEC_H264_ENCODE_EXTENSION_NAME,
        .specVersion   = VK_STD_VULKAN_VIDEO_CODEC_H264_ENCODE_SPEC_VERSION,
    },
};

/* Random (version 4) ISO 11578 UUID. */
static const uint8_t vulkan_encode_h264_sei_identifier_uuid[16] = {
    0x03, 0xfd, 0xf2, 0x0a, 0x5d, 0x4c, 0x05, 0x48,
    0x20, 0x98, 0xca, 0x6b, 0x0c, 0x95, 0x30, 0x1c,
};

typedef struct VulkanEncodeH264Picture {
    int frame_num;
    int64_t last_idr_frame;
    uint16_t idr_pic_id;
    int primary_pic_type;
    int slice_type;
    int pic_order_cnt;

    enum UnitElems units_needed;

    VkVideoEncodeH264RateControlInfoKHR vkrc_info;
    VkVideoEncodeH264RateControlLayerInfoKHR vkrc_layer_info;
    VkVideoEncodeH264GopRemainingFrameInfoKHR vkrc_remaining;

    StdVideoEncodeH264WeightTable slice_wt;
    StdVideoEncodeH264SliceHeader slice_hdr;
    VkVideoEncodeH264NaluSliceInfoKHR vkslice;

    StdVideoEncodeH264PictureInfo   h264pic_info;
    VkVideoEncodeH264PictureInfoKHR vkh264pic_info;

    StdVideoEncodeH264ReferenceInfo h264dpb_info;
    VkVideoEncodeH264DpbSlotInfoKHR vkh264dpb_info;

    StdVideoEncodeH264RefListModEntry mods[MAX_REFERENCE_LIST_NUM][H264_MAX_RPLM_COUNT];
    StdVideoEncodeH264RefPicMarkingEntry mmco[H264_MAX_RPLM_COUNT];
    StdVideoEncodeH264ReferenceListsInfo ref_list_info;
} VulkanEncodeH264Picture;

typedef struct VulkanEncodeH264Context {
    FFVulkanEncodeContext common;

    FFHWBaseEncodeH264 units;
    FFHWBaseEncodeH264Opts unit_opts;

    enum UnitElems unit_elems;

    uint8_t fixed_qp_p;
    uint8_t fixed_qp_b;

    VkVideoEncodeH264ProfileInfoKHR profile;

    VkVideoEncodeH264CapabilitiesKHR caps;
    VkVideoEncodeH264QualityLevelPropertiesKHR quality_props;

    CodedBitstreamContext *cbs;
    CodedBitstreamFragment current_access_unit;

    H264RawAUD                  raw_aud;

    SEIRawUserDataUnregistered  sei_identifier;
    H264RawSEIPicTiming         sei_pic_timing;
    H264RawSEIRecoveryPoint     sei_recovery_point;
    SEIRawUserDataRegistered    sei_a53cc;
    void                       *sei_a53cc_data;
    char                       *sei_identifier_string;
} VulkanEncodeH264Context;

static int init_pic_rc(AVCodecContext *avctx, FFHWBaseEncodePicture *pic,
                       VkVideoEncodeRateControlInfoKHR *rc_info,
                       VkVideoEncodeRateControlLayerInfoKHR *rc_layer)
{
    VulkanEncodeH264Context *enc = avctx->priv_data;
    FFVulkanEncodeContext   *ctx = &enc->common;
    VulkanEncodeH264Picture  *hp = pic->codec_priv;

    hp->vkrc_info = (VkVideoEncodeH264RateControlInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_RATE_CONTROL_INFO_KHR,
        .flags = VK_VIDEO_ENCODE_H264_RATE_CONTROL_REFERENCE_PATTERN_FLAT_BIT_KHR |
                 VK_VIDEO_ENCODE_H264_RATE_CONTROL_REGULAR_GOP_BIT_KHR,
        .idrPeriod = ctx->base.gop_size,
        .gopFrameCount = ctx->base.gop_size,
        .consecutiveBFrameCount = FFMAX(ctx->base.b_per_p - 1, 0),
        .temporalLayerCount = 0,
    };
    rc_info->pNext = &hp->vkrc_info;

    if (rc_info->rateControlMode > VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR) {
        rc_info->virtualBufferSizeInMs = (enc->unit_opts.hrd_buffer_size * 1000LL) / avctx->bit_rate;
        rc_info->initialVirtualBufferSizeInMs = (enc->unit_opts.initial_buffer_fullness * 1000LL) / avctx->bit_rate;

        hp->vkrc_layer_info = (VkVideoEncodeH264RateControlLayerInfoKHR) {
            .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_RATE_CONTROL_LAYER_INFO_KHR,

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
        hp->vkrc_info.temporalLayerCount = 1;
    }

    return 0;
}

static int vk_enc_h264_update_pic_info(AVCodecContext *avctx,
                                       FFHWBaseEncodePicture *pic)
{
    VulkanEncodeH264Context   *enc = avctx->priv_data;
    FFVulkanEncodeContext     *ctx = &enc->common;
    VulkanEncodeH264Picture    *hp = pic->codec_priv;
    FFHWBaseEncodePicture    *prev = pic->prev;
    VulkanEncodeH264Picture *hprev = prev ? prev->codec_priv : NULL;

    if (pic->type == FF_HW_PICTURE_TYPE_IDR) {
        av_assert0(pic->display_order == pic->encode_order);

        hp->frame_num      = 0;
        hp->last_idr_frame = pic->display_order;
        hp->idr_pic_id     = hprev ? hprev->idr_pic_id + 1 : 0;

        hp->primary_pic_type = 0;
        hp->slice_type       = STD_VIDEO_H264_SLICE_TYPE_I;
    } else {
        av_assert0(prev);

        hp->frame_num = hprev->frame_num + prev->is_reference;

        hp->last_idr_frame = hprev->last_idr_frame;
        hp->idr_pic_id     = hprev->idr_pic_id;

        if (pic->type == FF_HW_PICTURE_TYPE_I) {
            hp->slice_type       = STD_VIDEO_H264_SLICE_TYPE_I;
            hp->primary_pic_type = 0;
        } else if (pic->type == FF_HW_PICTURE_TYPE_P) {
            hp->slice_type       = STD_VIDEO_H264_SLICE_TYPE_P;
            hp->primary_pic_type = 1;
        } else {
            hp->slice_type       = STD_VIDEO_H264_SLICE_TYPE_B;
            hp->primary_pic_type = 2;
        }
    }

    hp->pic_order_cnt = pic->display_order - hp->last_idr_frame;
    if (enc->units.raw_sps.pic_order_cnt_type == 2)
        hp->pic_order_cnt *= 2;

    hp->units_needed = 0;

    if (enc->unit_elems & UNIT_SEI_IDENTIFIER && pic->encode_order == 0)
        hp->units_needed |= UNIT_SEI_IDENTIFIER;

    if (enc->unit_elems & UNIT_SEI_TIMING) {
        enc->sei_pic_timing = (H264RawSEIPicTiming) {
            .cpb_removal_delay = 2 * (pic->encode_order - hp->last_idr_frame),
            .dpb_output_delay  = 2 * (pic->display_order - pic->encode_order + ctx->base.max_b_depth),
        };

        hp->units_needed |= UNIT_SEI_TIMING;
    }

    if (enc->unit_elems & UNIT_SEI_RECOVERY && pic->type == FF_HW_PICTURE_TYPE_I) {
        enc->sei_recovery_point = (H264RawSEIRecoveryPoint) {
            .recovery_frame_cnt = 0,
            .exact_match_flag   = 1,
            .broken_link_flag   = ctx->base.b_per_p > 0,
        };

        hp->units_needed |= UNIT_SEI_RECOVERY;
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
    VulkanEncodeH264Context *enc = avctx->priv_data;
    VulkanEncodeH264Picture *hp = pic->codec_priv;

    hp->slice_wt = (StdVideoEncodeH264WeightTable) {
        .flags = (StdVideoEncodeH264WeightTableFlags) {
            .luma_weight_l0_flag = 0,
            .chroma_weight_l0_flag = 0,
            .luma_weight_l1_flag = 0,
            .chroma_weight_l1_flag = 0,
        },
        .luma_log2_weight_denom = 0,
        .chroma_log2_weight_denom = 0,
        .luma_weight_l0 = { 0 },
        .luma_offset_l0 = { 0 },
        .chroma_weight_l0 = { { 0 } },
        .chroma_offset_l0 = { { 0 } },
        .luma_weight_l1 = { 0 },
        .luma_offset_l1 = { 0 },
        .chroma_weight_l1 = { { 0 } },
        .chroma_offset_l1 = { { 0 } },
    };

    hp->slice_hdr = (StdVideoEncodeH264SliceHeader) {
        .flags = (StdVideoEncodeH264SliceHeaderFlags) {
            .direct_spatial_mv_pred_flag = 1,
            /* The vk_samples code does this */
            .num_ref_idx_active_override_flag =
                ((enc->units.raw_pps.num_ref_idx_l0_default_active_minus1) &&
                 (pic->type == FF_HW_PICTURE_TYPE_B)) ? 1 : 0,
        },
        .first_mb_in_slice = 1,
        .slice_type = hp->slice_type,
        .slice_alpha_c0_offset_div2 = 0,
        .slice_beta_offset_div2 = 0,
        .slice_qp_delta = 0, /* Filled in below */
        /* Reserved */
        .cabac_init_idc = 0,
        .disable_deblocking_filter_idc = 0,
        .pWeightTable = NULL, // &hp->slice_wt,
    };

    hp->vkslice = (VkVideoEncodeH264NaluSliceInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_NALU_SLICE_INFO_KHR,
        .pNext = NULL,
        .constantQp = pic->type == FF_HW_PICTURE_TYPE_B ? enc->fixed_qp_b :
                      pic->type == FF_HW_PICTURE_TYPE_P ? enc->fixed_qp_p :
                                                          enc->unit_opts.fixed_qp_idr,
        .pStdSliceHeader = &hp->slice_hdr,
    };

    if (enc->common.opts.rc_mode != VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR)
        hp->vkslice.constantQp = 0;

    hp->slice_hdr.slice_qp_delta = hp->vkslice.constantQp -
                                   (enc->units.raw_pps.pic_init_qp_minus26 + 26);

    hp->vkh264pic_info.pNaluSliceEntries = &hp->vkslice;
    hp->vkh264pic_info.naluSliceEntryCount = 1;
}

static void vk_enc_h264_default_ref_pic_list(AVCodecContext *avctx,
                                             FFHWBaseEncodePicture *pic,
                                             FFHWBaseEncodePicture **rpl0,
                                             FFHWBaseEncodePicture **rpl1,
                                             int *rpl_size)
{
    FFHWBaseEncodePicture *prev;
    VulkanEncodeH264Picture *hp, *hn, *hc;
    int i, j, n = 0;

    prev = pic->prev;
    av_assert0(prev);
    hp = pic->codec_priv;

    for (i = 0; i < pic->prev->nb_dpb_pics; i++) {
        hn = prev->dpb[i]->codec_priv;
        av_assert0(hn->frame_num < hp->frame_num);

        if (pic->type == FF_HW_PICTURE_TYPE_P) {
            for (j = n; j > 0; j--) {
                hc = rpl0[j - 1]->codec_priv;
                av_assert0(hc->frame_num != hn->frame_num);
                if (hc->frame_num > hn->frame_num)
                    break;
                rpl0[j] = rpl0[j - 1];
            }
            rpl0[j] = prev->dpb[i];

        } else if (pic->type == FF_HW_PICTURE_TYPE_B) {
            for (j = n; j > 0; j--) {
                hc = rpl0[j - 1]->codec_priv;
                av_assert0(hc->pic_order_cnt != hp->pic_order_cnt);
                if (hc->pic_order_cnt < hp->pic_order_cnt) {
                    if (hn->pic_order_cnt > hp->pic_order_cnt ||
                        hn->pic_order_cnt < hc->pic_order_cnt)
                        break;
                } else {
                    if (hn->pic_order_cnt > hc->pic_order_cnt)
                        break;
                }
                rpl0[j] = rpl0[j - 1];
            }
            rpl0[j] = prev->dpb[i];

            for (j = n; j > 0; j--) {
                hc = rpl1[j - 1]->codec_priv;
                av_assert0(hc->pic_order_cnt != hp->pic_order_cnt);
                if (hc->pic_order_cnt > hp->pic_order_cnt) {
                    if (hn->pic_order_cnt < hp->pic_order_cnt ||
                        hn->pic_order_cnt > hc->pic_order_cnt)
                        break;
                } else {
                    if (hn->pic_order_cnt < hc->pic_order_cnt)
                        break;
                }
                rpl1[j] = rpl1[j - 1];
            }
            rpl1[j] = prev->dpb[i];
        }

        ++n;
    }

    if (pic->type == FF_HW_PICTURE_TYPE_B) {
        for (i = 0; i < n; i++) {
            if (rpl0[i] != rpl1[i])
                break;
        }
        if (i == n)
            FFSWAP(FFHWBaseEncodePicture *, rpl1[0], rpl1[1]);
    }

    if (pic->type == FF_HW_PICTURE_TYPE_P ||
        pic->type == FF_HW_PICTURE_TYPE_B) {
        av_log(avctx, AV_LOG_DEBUG, "Default RefPicList0 for fn=%d/poc=%d:",
               hp->frame_num, hp->pic_order_cnt);
        for (i = 0; i < n; i++) {
            hn = rpl0[i]->codec_priv;
            av_log(avctx, AV_LOG_DEBUG, "  fn=%d/poc=%d",
                   hn->frame_num, hn->pic_order_cnt);
        }
        av_log(avctx, AV_LOG_DEBUG, "\n");
    }
    if (pic->type == FF_HW_PICTURE_TYPE_B) {
        av_log(avctx, AV_LOG_DEBUG, "Default RefPicList1 for fn=%d/poc=%d:",
               hp->frame_num, hp->pic_order_cnt);
        for (i = 0; i < n; i++) {
            hn = rpl1[i]->codec_priv;
            av_log(avctx, AV_LOG_DEBUG, "  fn=%d/poc=%d",
                   hn->frame_num, hn->pic_order_cnt);
        }
        av_log(avctx, AV_LOG_DEBUG, "\n");
    }

    *rpl_size = n;
}

static void setup_refs(AVCodecContext *avctx,
                       FFHWBaseEncodePicture *pic,
                       VkVideoEncodeInfoKHR *encode_info)
{
    int n, i, j;
    VulkanEncodeH264Context *enc = avctx->priv_data;
    VulkanEncodeH264Picture *hp = pic->codec_priv;
    FFHWBaseEncodePicture *prev = pic->prev;
    FFHWBaseEncodePicture *def_l0[MAX_DPB_SIZE], *def_l1[MAX_DPB_SIZE];
    VulkanEncodeH264Picture *href;

    hp->ref_list_info = (StdVideoEncodeH264ReferenceListsInfo) {
        .flags = (StdVideoEncodeH264ReferenceListsInfoFlags) {
            .ref_pic_list_modification_flag_l0 = 0,
            .ref_pic_list_modification_flag_l1 = 0,
            /* Reserved */
        },
        /* May be overridden during setup_slices() */
        .num_ref_idx_l0_active_minus1 = pic->nb_refs[0] - 1,
        .num_ref_idx_l1_active_minus1 = pic->nb_refs[1] - 1,
        /* .RefPicList0 is set in vk_enc_h264_default_ref_pic_list() */
        /* .RefPicList1 is set in vk_enc_h264_default_ref_pic_list() */
        /* Reserved */
        .pRefList0ModOperations = NULL, /* All set below */
        .refList0ModOpCount = 0,
        .pRefList1ModOperations = NULL,
        .refList1ModOpCount = 0,
        .pRefPicMarkingOperations = NULL,
        .refPicMarkingOpCount = 0,
    };

    for (i = 0; i < STD_VIDEO_H264_MAX_NUM_LIST_REF; i++)
        hp->ref_list_info.RefPicList0[i] = hp->ref_list_info.RefPicList1[i] = -1;

    /* Note: really not sure */
    for (int i = 0; i < pic->nb_refs[0]; i++) {
        VkVideoReferenceSlotInfoKHR *slot_info;
        slot_info = (VkVideoReferenceSlotInfoKHR *)&encode_info->pReferenceSlots[i];
        hp->ref_list_info.RefPicList0[i] = slot_info->slotIndex;
    }

    /* Note: really not sure */
    for (int i = 0; i < pic->nb_refs[1]; i++) {
        VkVideoReferenceSlotInfoKHR *slot_info;
        slot_info = (VkVideoReferenceSlotInfoKHR *)&encode_info->pReferenceSlots[pic->nb_refs[0] + i];
        hp->ref_list_info.RefPicList1[i] = slot_info->slotIndex;
    }

    hp->h264pic_info.pRefLists = &hp->ref_list_info;

    if (pic->is_reference && pic->type != FF_HW_PICTURE_TYPE_IDR) {
        FFHWBaseEncodePicture *discard_list[MAX_DPB_SIZE];
        int discard = 0, keep = 0;

        // Discard everything which is in the DPB of the previous frame but
        // not in the DPB of this one.
        for (i = 0; i < prev->nb_dpb_pics; i++) {
            for (j = 0; j < pic->nb_dpb_pics; j++) {
                if (prev->dpb[i] == pic->dpb[j])
                    break;
            }
            if (j == pic->nb_dpb_pics) {
                discard_list[discard] = prev->dpb[i];
                ++discard;
            } else {
                ++keep;
            }
        }
        av_assert0(keep <= enc->units.dpb_frames);

        if (discard == 0) {
            hp->h264pic_info.flags.adaptive_ref_pic_marking_mode_flag = 0;
        } else {
            hp->h264pic_info.flags.adaptive_ref_pic_marking_mode_flag = 1;
            for (i = 0; i < discard; i++) {
                VulkanEncodeH264Picture *old = discard_list[i]->codec_priv;
                av_assert0(old->frame_num < hp->frame_num);
                hp->mmco[i] = (StdVideoEncodeH264RefPicMarkingEntry) {
                    .memory_management_control_operation = 1,
                    .difference_of_pic_nums_minus1 = hp->frame_num - old->frame_num - 1,
                };
            }
            hp->mmco[i] = (StdVideoEncodeH264RefPicMarkingEntry) {
                .memory_management_control_operation = 0,
            };
            hp->ref_list_info.pRefPicMarkingOperations = hp->mmco;
            hp->ref_list_info.refPicMarkingOpCount = i + 1;
        }
    }

    if (pic->type == FF_HW_PICTURE_TYPE_I || pic->type == FF_HW_PICTURE_TYPE_IDR)
        return;

    // If the intended references are not the first entries of RefPicListN
    // by default, use ref-pic-list-modification to move them there.
    vk_enc_h264_default_ref_pic_list(avctx, pic,
                                     def_l0, def_l1, &n);

    if (pic->type == FF_HW_PICTURE_TYPE_P) {
        int need_rplm = 0;
        for (i = 0; i < pic->nb_refs[0]; i++) {
            av_assert0(pic->refs[0][i]);
            if (pic->refs[0][i] != (FFHWBaseEncodePicture *)def_l0[i])
                need_rplm = 1;
        }

        hp->ref_list_info.flags.ref_pic_list_modification_flag_l0 = need_rplm;
        if (need_rplm) {
            int pic_num = hp->frame_num;
            for (i = 0; i < pic->nb_refs[0]; i++) {
                href = pic->refs[0][i]->codec_priv;
                av_assert0(href->frame_num != pic_num);
                if (href->frame_num < pic_num) {
                    hp->mods[0][i] = (StdVideoEncodeH264RefListModEntry) {
                        .modification_of_pic_nums_idc = 0,
                        .abs_diff_pic_num_minus1 = pic_num - href->frame_num - 1,
                    };
                } else {
                    hp->mods[0][i] = (StdVideoEncodeH264RefListModEntry) {
                        .modification_of_pic_nums_idc = 1,
                        .abs_diff_pic_num_minus1 = href->frame_num - pic_num - 1,
                    };
                }
                pic_num = href->frame_num;
            }
            hp->ref_list_info.pRefList0ModOperations = hp->mods[0];
            hp->ref_list_info.refList0ModOpCount = i - 1;
        }
    } else {
        int need_rplm_l0 = 0, need_rplm_l1 = 0;
        int n0 = 0, n1 = 0;
        for (i = 0; i < pic->nb_refs[0]; i++) {
            av_assert0(pic->refs[0][i]);
            href = pic->refs[0][i]->codec_priv;
            av_assert0(href->pic_order_cnt < hp->pic_order_cnt);
            if (pic->refs[0][i] != (FFHWBaseEncodePicture *)def_l0[n0])
                need_rplm_l0 = 1;
            ++n0;
        }

        for (int i = 0; i < pic->nb_refs[1]; i++) {
            av_assert0(pic->refs[1][i]);
            href = pic->refs[1][i]->codec_priv;
            av_assert0(href->pic_order_cnt > hp->pic_order_cnt);
            if (pic->refs[1][i] != (FFHWBaseEncodePicture *)def_l1[n1])
                need_rplm_l1 = 1;
            ++n1;
        }

        hp->ref_list_info.flags.ref_pic_list_modification_flag_l0 = need_rplm_l0;
        if (need_rplm_l0) {
            int pic_num = hp->frame_num;
            for (i = j = 0; i < pic->nb_refs[0]; i++) {
                href = pic->refs[0][i]->codec_priv;
                av_assert0(href->frame_num != pic_num);
                if (href->frame_num < pic_num) {
                    hp->mods[0][j] = (StdVideoEncodeH264RefListModEntry) {
                        .modification_of_pic_nums_idc = 0,
                        .abs_diff_pic_num_minus1 = pic_num - href->frame_num - 1,
                    };
                } else {
                    hp->mods[0][j] = (StdVideoEncodeH264RefListModEntry) {
                        .modification_of_pic_nums_idc = 1,
                        .abs_diff_pic_num_minus1 = href->frame_num - pic_num - 1,
                    };
                }
                pic_num = href->frame_num;
                ++j;
            }
            hp->ref_list_info.pRefList0ModOperations = hp->mods[0];
            hp->ref_list_info.refList0ModOpCount = j - 1;
        }

        hp->ref_list_info.flags.ref_pic_list_modification_flag_l1 = need_rplm_l1;
        if (need_rplm_l1) {
            int pic_num = hp->frame_num;
            for (i = j = 0; i < pic->nb_refs[1]; i++) {
                href = pic->refs[1][i]->codec_priv;
                av_assert0(href->frame_num != pic_num);
                if (href->frame_num < pic_num) {
                    hp->mods[1][j] = (StdVideoEncodeH264RefListModEntry) {
                        .modification_of_pic_nums_idc = 0,
                        .abs_diff_pic_num_minus1 = pic_num - href->frame_num - 1,
                    };
                } else {
                    hp->mods[1][j] = (StdVideoEncodeH264RefListModEntry) {
                        .modification_of_pic_nums_idc = 1,
                        .abs_diff_pic_num_minus1 = href->frame_num - pic_num - 1,
                    };
                }
                pic_num = href->frame_num;
                ++j;
            }
            hp->ref_list_info.pRefList1ModOperations = hp->mods[1];
            hp->ref_list_info.refList1ModOpCount = j - 1;
        }
    }
}

static int init_pic_params(AVCodecContext *avctx, FFHWBaseEncodePicture *pic,
                           VkVideoEncodeInfoKHR *encode_info)
{
    int err;
    FFVulkanEncodePicture *vp = pic->priv;
    VulkanEncodeH264Picture *hp = pic->codec_priv;
    VkVideoReferenceSlotInfoKHR *ref_slot;

    err = vk_enc_h264_update_pic_info(avctx, pic);
    if (err < 0)
        return err;

    hp->vkh264pic_info = (VkVideoEncodeH264PictureInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_PICTURE_INFO_KHR,
        .pNext = NULL,
        .pNaluSliceEntries = NULL, // Filled in during setup_slices()
        .naluSliceEntryCount = 0, // Filled in during setup_slices()
        .pStdPictureInfo = &hp->h264pic_info,
    };

    hp->h264pic_info = (StdVideoEncodeH264PictureInfo) {
        .flags = (StdVideoEncodeH264PictureInfoFlags) {
            .IdrPicFlag = pic->type == FF_HW_PICTURE_TYPE_IDR,
            .is_reference = pic->is_reference,
            .no_output_of_prior_pics_flag = 0,
            .long_term_reference_flag = 0,
            .adaptive_ref_pic_marking_mode_flag = 0, // Filled in during setup_refs()
            /* Reserved */
        },
        .seq_parameter_set_id = 0,
        .pic_parameter_set_id = 0,
        .idr_pic_id = hp->idr_pic_id,
        .primary_pic_type = pic->type == FF_HW_PICTURE_TYPE_P ? STD_VIDEO_H264_PICTURE_TYPE_P :
                            pic->type == FF_HW_PICTURE_TYPE_B ? STD_VIDEO_H264_PICTURE_TYPE_B :
                            pic->type == FF_HW_PICTURE_TYPE_I ? STD_VIDEO_H264_PICTURE_TYPE_I :
                                                                STD_VIDEO_H264_PICTURE_TYPE_IDR,
        .frame_num = hp->frame_num,
        .PicOrderCnt = hp->pic_order_cnt,
        .temporal_id = 0, /* ? */
        /* Reserved */
        .pRefLists = NULL, // Filled in during setup_refs
    };
    encode_info->pNext = &hp->vkh264pic_info;

    hp->h264dpb_info = (StdVideoEncodeH264ReferenceInfo) {
        .flags = (StdVideoEncodeH264ReferenceInfoFlags) {
            .used_for_long_term_reference = 0,
            /* Reserved */
        },
        .primary_pic_type = hp->h264pic_info.primary_pic_type,
        .FrameNum = hp->h264pic_info.frame_num,
        .PicOrderCnt = hp->h264pic_info.PicOrderCnt,
        .long_term_pic_num = 0,
        .long_term_frame_idx = 0,
        .temporal_id = hp->h264pic_info.temporal_id,
    };
    hp->vkh264dpb_info = (VkVideoEncodeH264DpbSlotInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_DPB_SLOT_INFO_KHR,
        .pStdReferenceInfo = &hp->h264dpb_info,
    };

    vp->dpb_slot.pNext = &hp->vkh264dpb_info;

    ref_slot = (VkVideoReferenceSlotInfoKHR *)encode_info->pSetupReferenceSlot;
    ref_slot->pNext = &hp->vkh264dpb_info;

    setup_refs(avctx, pic, encode_info);

    setup_slices(avctx, pic);

    return 0;
}

static int init_profile(AVCodecContext *avctx,
                        VkVideoProfileInfoKHR *profile, void *pnext)
{
    VkResult ret;
    VulkanEncodeH264Context *enc = avctx->priv_data;
    FFVulkanEncodeContext *ctx = &enc->common;
    FFVulkanContext *s = &ctx->s;
    FFVulkanFunctions *vk = &ctx->s.vkfn;
    VkVideoEncodeH264CapabilitiesKHR h264_caps = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_CAPABILITIES_KHR,
    };
    VkVideoEncodeCapabilitiesKHR enc_caps = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_CAPABILITIES_KHR,
        .pNext = &h264_caps,
    };
    VkVideoCapabilitiesKHR caps = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_CAPABILITIES_KHR,
        .pNext = &enc_caps,
    };

    /* In order of preference */
    int last_supported = AV_PROFILE_UNKNOWN;
    static const int known_profiles[] = {
        AV_PROFILE_H264_CONSTRAINED_BASELINE,
        AV_PROFILE_H264_MAIN,
        AV_PROFILE_H264_HIGH,
        AV_PROFILE_H264_HIGH_10,
    };
    int nb_profiles = FF_ARRAY_ELEMS(known_profiles);

    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(s->frames->sw_format);
    if (!desc)
        return AVERROR(EINVAL);

    if (desc->comp[0].depth == 8)
        nb_profiles = 3;

    enc->profile = (VkVideoEncodeH264ProfileInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_PROFILE_INFO_KHR,
        .pNext = pnext,
        .stdProfileIdc = ff_vk_h264_profile_to_vk(avctx->profile),
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
        enc->profile.stdProfileIdc = ff_vk_h264_profile_to_vk(known_profiles[i]);
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

    enc->profile.stdProfileIdc = ff_vk_h264_profile_to_vk(last_supported);
    av_log(avctx, AV_LOG_VERBOSE, "Using profile %s\n",
           avcodec_profile_name(avctx->codec_id, last_supported));
    avctx->profile = last_supported;

    return 0;
}

static int init_enc_options(AVCodecContext *avctx)
{
    VulkanEncodeH264Context *enc = avctx->priv_data;
    FFHWBaseEncodeH264Opts *unit_opts = &enc->unit_opts;

    if (avctx->rc_buffer_size)
        unit_opts->hrd_buffer_size = avctx->rc_buffer_size;
    else if (avctx->rc_max_rate > 0)
        unit_opts->hrd_buffer_size = avctx->rc_max_rate;
    else
        unit_opts->hrd_buffer_size = avctx->bit_rate;

    if (avctx->rc_initial_buffer_occupancy) {
        if (avctx->rc_initial_buffer_occupancy > unit_opts->hrd_buffer_size) {
            av_log(avctx, AV_LOG_ERROR, "Invalid RC buffer settings: "
                                        "must have initial buffer size (%d) <= "
                                        "buffer size (%"PRId64").\n",
                   avctx->rc_initial_buffer_occupancy, unit_opts->hrd_buffer_size);
            return AVERROR(EINVAL);
        }
        unit_opts->initial_buffer_fullness = avctx->rc_initial_buffer_occupancy;
    } else {
        unit_opts->initial_buffer_fullness = unit_opts->hrd_buffer_size * 3 / 4;
    }

    if (enc->common.opts.rc_mode == VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR) {
        /* HRD info is required for timing */
        enc->unit_elems &= ~UNIT_SEI_TIMING;

        enc->fixed_qp_p = av_clip(enc->common.explicit_qp,
                                  enc->caps.minQp, enc->caps.maxQp);
        if (avctx->i_quant_factor > 0.0)
            unit_opts->fixed_qp_idr = av_clip((avctx->i_quant_factor * enc->fixed_qp_p +
                                               avctx->i_quant_offset) + 0.5,
                                              enc->caps.minQp, enc->caps.maxQp);
        else
            unit_opts->fixed_qp_idr = enc->fixed_qp_p;

        if (avctx->b_quant_factor > 0.0)
            enc->fixed_qp_b = av_clip((avctx->b_quant_factor * enc->fixed_qp_p +
                                       avctx->b_quant_offset) + 0.5,
                                      enc->caps.minQp, enc->caps.maxQp);
        else
            enc->fixed_qp_b = enc->fixed_qp_p;

        av_log(avctx, AV_LOG_DEBUG, "Using fixed QP = "
               "%d / %d / %d for IDR- / P- / B-frames.\n",
               unit_opts->fixed_qp_idr, enc->fixed_qp_p, enc->fixed_qp_b);
    } else {
        unit_opts->fixed_qp_idr = 26;
        enc->fixed_qp_p = 26;
        enc->fixed_qp_b = 26;
    }

    return 0;
}

static av_cold int init_sequence_headers(AVCodecContext *avctx)
{
    int err;
    VulkanEncodeH264Context *enc = avctx->priv_data;

    FFHWBaseEncodeH264 *units = &enc->units;
    FFHWBaseEncodeH264Opts *unit_opts = &enc->unit_opts;

    unit_opts->bit_rate  = avctx->bit_rate;
    unit_opts->mb_width  = FFALIGN(avctx->width,  16) / 16;
    unit_opts->mb_height = FFALIGN(avctx->height, 16) / 16;
    unit_opts->flags     = enc->unit_elems & UNIT_SEI_TIMING ? FF_HW_H264_SEI_TIMING : 0;

    /* cabac already set via an option */
    /* fixed_qp_idr initialized in init_enc_options() */
    /* hrd_buffer_size initialized in init_enc_options() */
    /* initial_buffer_fullness initialized in init_enc_options() */

    err = ff_hw_base_encode_init_params_h264(&enc->common.base, avctx,
                                             units, unit_opts);
    if (err < 0)
        return err;

    units->raw_sps.seq_scaling_matrix_present_flag =
        !!(enc->caps.stdSyntaxFlags & VK_VIDEO_ENCODE_H264_STD_SCALING_MATRIX_PRESENT_FLAG_SET_BIT_KHR);
    units->raw_pps.pic_scaling_matrix_present_flag =
        !!(enc->caps.stdSyntaxFlags & VK_VIDEO_ENCODE_H264_STD_SCALING_MATRIX_PRESENT_FLAG_SET_BIT_KHR);
    units->raw_pps.transform_8x8_mode_flag =
        !!(enc->caps.stdSyntaxFlags & VK_VIDEO_ENCODE_H264_STD_TRANSFORM_8X8_MODE_FLAG_SET_BIT_KHR);

    return 0;
}

typedef struct VulkanH264Units {
    StdVideoH264SequenceParameterSet    vksps;
    StdVideoH264ScalingLists            vksps_scaling;
    StdVideoH264HrdParameters           vksps_vui_header;
    StdVideoH264SequenceParameterSetVui vksps_vui;

    StdVideoH264PictureParameterSet     vkpps;
    StdVideoH264ScalingLists            vkpps_scaling;
} VulkanH264Units;

static av_cold int base_unit_to_vk(AVCodecContext *avctx,
                                   VulkanH264Units *vk_units)
{
    VulkanEncodeH264Context *enc = avctx->priv_data;

    FFHWBaseEncodeH264 *units = &enc->units;

    H264RawSPS                          *sps = &units->raw_sps;
    H264RawHRD                          *hrd = &sps->vui.nal_hrd_parameters;
    StdVideoH264ScalingLists            *vksps_scaling = &vk_units->vksps_scaling;
    StdVideoH264HrdParameters           *vksps_vui_header = &vk_units->vksps_vui_header;
    StdVideoH264SequenceParameterSetVui *vksps_vui = &vk_units->vksps_vui;
    StdVideoH264SequenceParameterSet    *vksps = &vk_units->vksps;

    H264RawPPS                          *pps = &units->raw_pps;
    StdVideoH264ScalingLists            *vkpps_scaling = &vk_units->vkpps_scaling;
    StdVideoH264PictureParameterSet     *vkpps = &vk_units->vkpps;

    *vksps_scaling = (StdVideoH264ScalingLists) {
        .scaling_list_present_mask = 0x0, // mask
        .use_default_scaling_matrix_mask = 1,
    };

    *vksps_vui_header = (StdVideoH264HrdParameters) {
        .cpb_cnt_minus1 = hrd->cpb_cnt_minus1,
        .bit_rate_scale = hrd->bit_rate_scale,
        .cpb_size_scale = hrd->cpb_size_scale,
        /* Reserved */
        /* bit_rate/cpb_size/cbr_flag set below */
        .initial_cpb_removal_delay_length_minus1 = hrd->initial_cpb_removal_delay_length_minus1,
        .cpb_removal_delay_length_minus1 = hrd->cpb_removal_delay_length_minus1,
        .dpb_output_delay_length_minus1 = hrd->dpb_output_delay_length_minus1,
        .time_offset_length = hrd->time_offset_length,
    };

    for (int i = 0; i < H264_MAX_CPB_CNT; i++) {
        vksps_vui_header->bit_rate_value_minus1[i] = hrd->bit_rate_value_minus1[i];
        vksps_vui_header->cpb_size_value_minus1[i] = hrd->cpb_size_value_minus1[i];
        vksps_vui_header->cbr_flag[i] = hrd->cbr_flag[i];
    }

    *vksps_vui = (StdVideoH264SequenceParameterSetVui) {
        .flags = (StdVideoH264SpsVuiFlags) {
            .aspect_ratio_info_present_flag = sps->vui.aspect_ratio_info_present_flag,
            .overscan_info_present_flag = sps->vui.overscan_info_present_flag,
            .overscan_appropriate_flag = sps->vui.overscan_appropriate_flag,
            .video_signal_type_present_flag = sps->vui.video_signal_type_present_flag,
            .video_full_range_flag = sps->vui.video_full_range_flag,
            .color_description_present_flag = sps->vui.colour_description_present_flag,
            .chroma_loc_info_present_flag = sps->vui.chroma_loc_info_present_flag,
            .timing_info_present_flag = sps->vui.timing_info_present_flag,
            .fixed_frame_rate_flag = sps->vui.fixed_frame_rate_flag,
            .bitstream_restriction_flag = sps->vui.bitstream_restriction_flag,
            .nal_hrd_parameters_present_flag = sps->vui.nal_hrd_parameters_present_flag,
            .vcl_hrd_parameters_present_flag = sps->vui.vcl_hrd_parameters_present_flag,
        },
        .aspect_ratio_idc = sps->vui.aspect_ratio_idc,
        .sar_width = sps->vui.sar_width,
        .sar_height = sps->vui.sar_height,
        .video_format = sps->vui.video_format,
        .colour_primaries = sps->vui.colour_primaries,
        .transfer_characteristics = sps->vui.transfer_characteristics,
        .matrix_coefficients = sps->vui.matrix_coefficients,
        .num_units_in_tick = sps->vui.num_units_in_tick,
        .time_scale = sps->vui.time_scale,
        .max_num_reorder_frames = sps->vui.max_num_reorder_frames,
        .max_dec_frame_buffering = sps->vui.max_dec_frame_buffering,
        .chroma_sample_loc_type_top_field = sps->vui.chroma_sample_loc_type_top_field,
        .chroma_sample_loc_type_bottom_field = sps->vui.chroma_sample_loc_type_bottom_field,
        /* Reserved */
        .pHrdParameters = vksps_vui_header,
    };

    *vksps = (StdVideoH264SequenceParameterSet) {
        .flags = (StdVideoH264SpsFlags) {
            .constraint_set0_flag = sps->constraint_set0_flag,
            .constraint_set1_flag = sps->constraint_set1_flag,
            .constraint_set2_flag = sps->constraint_set2_flag,
            .constraint_set3_flag = sps->constraint_set3_flag,
            .constraint_set4_flag = sps->constraint_set4_flag,
            .constraint_set5_flag = sps->constraint_set5_flag,
            .direct_8x8_inference_flag = sps->direct_8x8_inference_flag,
            .mb_adaptive_frame_field_flag = sps->mb_adaptive_frame_field_flag,
            .frame_mbs_only_flag = sps->frame_mbs_only_flag,
            .delta_pic_order_always_zero_flag = sps->delta_pic_order_always_zero_flag,
            .separate_colour_plane_flag = sps->separate_colour_plane_flag,
            .gaps_in_frame_num_value_allowed_flag = sps->gaps_in_frame_num_allowed_flag,
            .qpprime_y_zero_transform_bypass_flag = sps->qpprime_y_zero_transform_bypass_flag,
            .frame_cropping_flag = sps->frame_cropping_flag,
            .seq_scaling_matrix_present_flag = sps->seq_scaling_matrix_present_flag,
            .vui_parameters_present_flag = sps->vui_parameters_present_flag,
        },
        .profile_idc = ff_vk_h264_profile_to_vk(sps->profile_idc),
        .level_idc = ff_vk_h264_level_to_vk(sps->level_idc),
        .chroma_format_idc = sps->chroma_format_idc,
        .seq_parameter_set_id = sps->seq_parameter_set_id,
        .bit_depth_luma_minus8 = sps->bit_depth_luma_minus8,
        .bit_depth_chroma_minus8 = sps->bit_depth_chroma_minus8,
        .log2_max_frame_num_minus4 = sps->log2_max_frame_num_minus4,
        .pic_order_cnt_type = sps->pic_order_cnt_type,
        .offset_for_non_ref_pic = sps->offset_for_non_ref_pic,
        .offset_for_top_to_bottom_field = sps->offset_for_top_to_bottom_field,
        .log2_max_pic_order_cnt_lsb_minus4 = sps->log2_max_pic_order_cnt_lsb_minus4,
        .num_ref_frames_in_pic_order_cnt_cycle = sps->num_ref_frames_in_pic_order_cnt_cycle,
        .max_num_ref_frames = sps->max_num_ref_frames,
        /* Reserved */
        .pic_width_in_mbs_minus1 = sps->pic_width_in_mbs_minus1,
        .pic_height_in_map_units_minus1 = sps->pic_height_in_map_units_minus1,
        .frame_crop_left_offset = sps->frame_crop_left_offset,
        .frame_crop_right_offset = sps->frame_crop_right_offset,
        .frame_crop_top_offset = sps->frame_crop_top_offset,
        .frame_crop_bottom_offset = sps->frame_crop_bottom_offset,
        /* Reserved */
        .pOffsetForRefFrame = sps->offset_for_ref_frame,
        .pScalingLists = vksps_scaling,
        .pSequenceParameterSetVui = vksps_vui,
    };

    *vkpps_scaling = (StdVideoH264ScalingLists) {
        .scaling_list_present_mask = 0x0, // mask
        .use_default_scaling_matrix_mask = 1,
    };

    *vkpps = (StdVideoH264PictureParameterSet) {
        .flags = (StdVideoH264PpsFlags) {
            .transform_8x8_mode_flag = pps->transform_8x8_mode_flag,
            .redundant_pic_cnt_present_flag = pps->redundant_pic_cnt_present_flag,
            .constrained_intra_pred_flag = pps->constrained_intra_pred_flag,
            .deblocking_filter_control_present_flag = pps->deblocking_filter_control_present_flag,
            .weighted_pred_flag = pps->weighted_pred_flag,
            .bottom_field_pic_order_in_frame_present_flag = pps->bottom_field_pic_order_in_frame_present_flag,
            .entropy_coding_mode_flag = pps->entropy_coding_mode_flag,
            .pic_scaling_matrix_present_flag = pps->pic_scaling_matrix_present_flag,
        },
        .seq_parameter_set_id = pps->seq_parameter_set_id,
        .pic_parameter_set_id = pps->pic_parameter_set_id,
        .num_ref_idx_l0_default_active_minus1 = pps->num_ref_idx_l0_default_active_minus1,
        .num_ref_idx_l1_default_active_minus1 = pps->num_ref_idx_l1_default_active_minus1,
        .weighted_bipred_idc = pps->weighted_bipred_idc,
        .pic_init_qp_minus26 = pps->pic_init_qp_minus26,
        .pic_init_qs_minus26 = pps->pic_init_qs_minus26,
        .chroma_qp_index_offset = pps->chroma_qp_index_offset,
        .second_chroma_qp_index_offset = pps->second_chroma_qp_index_offset,
        .pScalingLists = vkpps_scaling,
    };

    return 0;
}

static int create_session_params(AVCodecContext *avctx)
{
    int err;
    VulkanEncodeH264Context *enc = avctx->priv_data;
    FFVulkanEncodeContext *ctx = &enc->common;
    FFVulkanContext *s = &ctx->s;
    FFVulkanFunctions *vk = &ctx->s.vkfn;

    VulkanH264Units vk_units = { 0 };

    VkVideoEncodeH264SessionParametersAddInfoKHR h264_params_info;
    VkVideoEncodeH264SessionParametersCreateInfoKHR h264_params;

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

    h264_params_info = (VkVideoEncodeH264SessionParametersAddInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_SESSION_PARAMETERS_ADD_INFO_KHR,
        .pStdSPSs = &vk_units.vksps,
        .stdSPSCount = 1,
        .pStdPPSs = &vk_units.vkpps,
        .stdPPSCount = 1,
    };
    h264_params = (VkVideoEncodeH264SessionParametersCreateInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_SESSION_PARAMETERS_CREATE_INFO_KHR,
        .maxStdSPSCount = 1,
        .maxStdPPSCount = 1,
        .pParametersAddInfo = &h264_params_info,
    };

    return ff_vulkan_encode_create_session_params(avctx, ctx, &h264_params);
}

static int parse_feedback_units(AVCodecContext *avctx,
                                const uint8_t *data, size_t size,
                                int sps_override, int pps_override)
{
    int err;
    VulkanEncodeH264Context *enc = avctx->priv_data;

    CodedBitstreamContext *cbs;
    CodedBitstreamFragment au = { 0 };

    err = ff_cbs_init(&cbs, AV_CODEC_ID_H264, avctx);
    if (err < 0)
        return err;

    err = ff_cbs_read(cbs, &au, data, size);
    if (err < 0) {
        av_log(avctx, AV_LOG_ERROR, "Unable to parse feedback units, bad drivers: %s\n",
               av_err2str(err));
        return err;
    }

    /* If PPS has an override, just copy it entirely. */
    if (pps_override) {
        for (int i = 0; i < au.nb_units; i++) {
            if (au.units[i].type == H264_NAL_PPS) {
                H264RawPPS *pps = au.units[i].content;
                memcpy(&enc->units.raw_pps, pps, sizeof(*pps));
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
    VulkanEncodeH264Context *enc = avctx->priv_data;
    FFVulkanEncodeContext *ctx = &enc->common;
    FFVulkanContext *s = &ctx->s;
    FFVulkanFunctions *vk = &ctx->s.vkfn;

    VkVideoEncodeH264SessionParametersGetInfoKHR h264_params_info;
    VkVideoEncodeSessionParametersGetInfoKHR params_info;
    VkVideoEncodeH264SessionParametersFeedbackInfoKHR h264_params_feedback;
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

    h264_params_info = (VkVideoEncodeH264SessionParametersGetInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_SESSION_PARAMETERS_GET_INFO_KHR,
        .writeStdSPS = 1,
        .writeStdPPS = 1,
        .stdSPSId = enc->units.raw_sps.seq_parameter_set_id,
        .stdPPSId = enc->units.raw_pps.pic_parameter_set_id,
    };
    params_info = (VkVideoEncodeSessionParametersGetInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_SESSION_PARAMETERS_GET_INFO_KHR,
        .pNext = &h264_params_info,
        .videoSessionParameters = ctx->session_params,
    };

    h264_params_feedback = (VkVideoEncodeH264SessionParametersFeedbackInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_SESSION_PARAMETERS_FEEDBACK_INFO_KHR,
    };
    params_feedback = (VkVideoEncodeSessionParametersFeedbackInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_SESSION_PARAMETERS_FEEDBACK_INFO_KHR,
        .pNext = &h264_params_feedback,
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
        av_log(avctx, AV_LOG_ERROR, "Unable to get feedback for H.264 units = %lu\n", data_size);
        return err;
    }

    ret = vk->GetEncodedVideoSessionParametersKHR(s->hwctx->act_dev, &params_info,
                                                  &params_feedback,
                                                  &data_size, data);
    if (ret != VK_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Error writing feedback units\n");
        return err;
    }

    av_log(avctx, AV_LOG_VERBOSE, "Feedback units written, overrides: %i (SPS: %i PPS: %i)\n",
           params_feedback.hasOverrides,
           h264_params_feedback.hasStdSPSOverrides,
           h264_params_feedback.hasStdPPSOverrides);

    params_feedback.hasOverrides = 1;
    h264_params_feedback.hasStdPPSOverrides = 1;

    /* No need to sync any overrides */
    if (!params_feedback.hasOverrides)
        return 0;

    /* Parse back tne units and override */
    err = parse_feedback_units(avctx, data, data_size,
                               h264_params_feedback.hasStdSPSOverrides,
                               h264_params_feedback.hasStdPPSOverrides);
    if (err < 0)
        return err;

    /* Create final session parameters */
    err = create_session_params(avctx);
    if (err < 0)
        return err;

    return 0;
}

static int vulkan_encode_h264_add_nal(AVCodecContext *avctx,
                                      CodedBitstreamFragment *au,
                                      void *nal_unit)
{
    H264RawNALUnitHeader *header = nal_unit;

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
    VulkanEncodeH264Context *enc = avctx->priv_data;

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
    VulkanEncodeH264Context *enc = avctx->priv_data;
    VulkanEncodeH264Picture  *hp = base_pic ? base_pic->codec_priv : NULL;
    CodedBitstreamFragment   *au = &enc->current_access_unit;

    if (hp && hp->units_needed & UNIT_AUD) {
        err = vulkan_encode_h264_add_nal(avctx, au, &enc->raw_aud);
        if (err < 0)
            goto fail;
    }

    err = vulkan_encode_h264_add_nal(avctx, au, &enc->units.raw_sps);
    if (err < 0)
        goto fail;

    err = vulkan_encode_h264_add_nal(avctx, au, &enc->units.raw_pps);
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
    VulkanEncodeH264Context *enc = avctx->priv_data;
    VulkanEncodeH264Picture  *hp = base_pic->codec_priv;
    CodedBitstreamFragment   *au = &enc->current_access_unit;

    if (hp->units_needed & UNIT_AUD) {
        err = vulkan_encode_h264_add_nal(avctx, au, &enc->raw_aud);
        if (err < 0)
            goto fail;
    }

    if (hp->units_needed & UNIT_SEI_IDENTIFIER) {
        err = ff_cbs_sei_add_message(enc->cbs, au, 1,
                                     SEI_TYPE_USER_DATA_UNREGISTERED,
                                     &enc->sei_identifier, NULL);
        if (err < 0)
            goto fail;
    }

    if (hp->units_needed & UNIT_SEI_TIMING) {
        if (base_pic->type == FF_HW_PICTURE_TYPE_IDR) {
            err = ff_cbs_sei_add_message(enc->cbs, au, 1,
                                         SEI_TYPE_BUFFERING_PERIOD,
                                         &enc->units.sei_buffering_period, NULL);
            if (err < 0)
                goto fail;
        }
        err = ff_cbs_sei_add_message(enc->cbs, au, 1,
                                     SEI_TYPE_PIC_TIMING,
                                     &enc->sei_pic_timing, NULL);
        if (err < 0)
            goto fail;
    }

    if (hp->units_needed & UNIT_SEI_RECOVERY) {
        err = ff_cbs_sei_add_message(enc->cbs, au, 1,
                                     SEI_TYPE_RECOVERY_POINT,
                                     &enc->sei_recovery_point, NULL);
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
    VulkanEncodeH264Context *enc = avctx->priv_data;
    CodedBitstreamFragment   *au = &enc->current_access_unit;

    H264RawFiller raw_filler = {
        .nal_unit_header = {
            .nal_unit_type = H264_NAL_FILLER_DATA,
        },
        .filler_size = filler,
    };

    err = vulkan_encode_h264_add_nal(avctx, au, &raw_filler);
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
    .picture_priv_data_size = sizeof(VulkanEncodeH264Picture),
    .filler_header_size = 6,
    .init_profile = init_profile,
    .init_pic_rc = init_pic_rc,
    .init_pic_params = init_pic_params,
    .write_sequence_headers = write_sequence_headers,
    .write_extra_headers = write_extra_headers,
    .write_filler = write_filler,
};

static av_cold int vulkan_encode_h264_init(AVCodecContext *avctx)
{
    int err, ref_l0, ref_l1;
    VulkanEncodeH264Context *enc = avctx->priv_data;
    FFVulkanEncodeContext *ctx = &enc->common;
    FFVulkanContext *s = &ctx->s;
    FFHWBaseEncodeContext *base_ctx = &ctx->base;
    int flags;

    if (avctx->profile == AV_PROFILE_UNKNOWN)
        avctx->profile = enc->common.opts.profile;

    enc->caps = (VkVideoEncodeH264CapabilitiesKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_CAPABILITIES_KHR,
    };

    enc->quality_props = (VkVideoEncodeH264QualityLevelPropertiesKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_QUALITY_LEVEL_PROPERTIES_KHR,
    };

    err = ff_vulkan_encode_init(avctx, &enc->common,
                                &ff_vk_enc_h264_desc, &enc_cb,
                                &enc->caps, &enc->quality_props);
    if (err < 0)
        return err;

    av_log(avctx, AV_LOG_VERBOSE, "H264 encoder capabilities:\n");
    av_log(avctx, AV_LOG_VERBOSE, "    Standard capability flags:\n");
    av_log(avctx, AV_LOG_VERBOSE, "        separate_color_plane: %i\n",
           !!(enc->caps.stdSyntaxFlags & VK_VIDEO_ENCODE_H264_STD_SEPARATE_COLOR_PLANE_FLAG_SET_BIT_KHR));
    av_log(avctx, AV_LOG_VERBOSE, "        qprime_y_zero_transform_bypass: %i\n",
           !!(enc->caps.stdSyntaxFlags & VK_VIDEO_ENCODE_H264_STD_QPPRIME_Y_ZERO_TRANSFORM_BYPASS_FLAG_SET_BIT_KHR));
    av_log(avctx, AV_LOG_VERBOSE, "        scaling_lists: %i\n",
           !!(enc->caps.stdSyntaxFlags & VK_VIDEO_ENCODE_H264_STD_SCALING_MATRIX_PRESENT_FLAG_SET_BIT_KHR));
    av_log(avctx, AV_LOG_VERBOSE, "        chroma_qp_index_offset: %i\n",
           !!(enc->caps.stdSyntaxFlags & VK_VIDEO_ENCODE_H264_STD_CHROMA_QP_INDEX_OFFSET_BIT_KHR));
    av_log(avctx, AV_LOG_VERBOSE, "        second_chroma_qp_index_offset: %i\n",
           !!(enc->caps.stdSyntaxFlags & VK_VIDEO_ENCODE_H264_STD_SECOND_CHROMA_QP_INDEX_OFFSET_BIT_KHR));
    av_log(avctx, AV_LOG_VERBOSE, "        pic_init_qp: %i\n",
           !!(enc->caps.stdSyntaxFlags & VK_VIDEO_ENCODE_H264_STD_PIC_INIT_QP_MINUS26_BIT_KHR));
    av_log(avctx, AV_LOG_VERBOSE, "        weighted:%s%s%s\n",
           enc->caps.stdSyntaxFlags & VK_VIDEO_ENCODE_H264_STD_WEIGHTED_PRED_FLAG_SET_BIT_KHR ?
               " pred" : "",
           enc->caps.stdSyntaxFlags & VK_VIDEO_ENCODE_H264_STD_WEIGHTED_BIPRED_IDC_EXPLICIT_BIT_KHR ?
               " bipred_explicit" : "",
           enc->caps.stdSyntaxFlags & VK_VIDEO_ENCODE_H264_STD_WEIGHTED_BIPRED_IDC_IMPLICIT_BIT_KHR ?
               " bipred_implicit" : "");
    av_log(avctx, AV_LOG_VERBOSE, "        8x8_transforms: %i\n",
           !!(enc->caps.stdSyntaxFlags & VK_VIDEO_ENCODE_H264_STD_TRANSFORM_8X8_MODE_FLAG_SET_BIT_KHR));
    av_log(avctx, AV_LOG_VERBOSE, "        disable_direct_spatial_mv_pred: %i\n",
           !!(enc->caps.stdSyntaxFlags & VK_VIDEO_ENCODE_H264_STD_DIRECT_SPATIAL_MV_PRED_FLAG_UNSET_BIT_KHR));
    av_log(avctx, AV_LOG_VERBOSE, "        coder:%s%s\n",
           enc->caps.stdSyntaxFlags & VK_VIDEO_ENCODE_H264_STD_ENTROPY_CODING_MODE_FLAG_UNSET_BIT_KHR ?
               " cabac" : "",
           enc->caps.stdSyntaxFlags & VK_VIDEO_ENCODE_H264_STD_ENTROPY_CODING_MODE_FLAG_SET_BIT_KHR ?
               " cavlc" : "");
    av_log(avctx, AV_LOG_VERBOSE, "        direct_8x8_inference: %i\n",
           !!(enc->caps.stdSyntaxFlags & VK_VIDEO_ENCODE_H264_STD_DIRECT_8X8_INFERENCE_FLAG_UNSET_BIT_KHR));
    av_log(avctx, AV_LOG_VERBOSE, "        constrained_intra_pred: %i\n",
           !!(enc->caps.stdSyntaxFlags & VK_VIDEO_ENCODE_H264_STD_CONSTRAINED_INTRA_PRED_FLAG_SET_BIT_KHR));
    av_log(avctx, AV_LOG_VERBOSE, "        deblock:%s%s%s\n",
           enc->caps.stdSyntaxFlags & VK_VIDEO_ENCODE_H264_STD_DEBLOCKING_FILTER_DISABLED_BIT_KHR ?
               " filter_disabling" : "",
           enc->caps.stdSyntaxFlags & VK_VIDEO_ENCODE_H264_STD_DEBLOCKING_FILTER_ENABLED_BIT_KHR ?
               " filter_enabling" : "",
           enc->caps.stdSyntaxFlags & VK_VIDEO_ENCODE_H264_STD_DEBLOCKING_FILTER_PARTIAL_BIT_KHR ?
               " filter_partial" : "");

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
           enc->caps.maxSliceCount);
    av_log(avctx, AV_LOG_VERBOSE, "        max(P/B)PictureL0ReferenceCount: %i P's; %i B's\n",
           enc->caps.maxPPictureL0ReferenceCount,
           enc->caps.maxBPictureL0ReferenceCount);
    av_log(avctx, AV_LOG_VERBOSE, "        maxL1ReferenceCount: %i\n",
           enc->caps.maxL1ReferenceCount);
    av_log(avctx, AV_LOG_VERBOSE, "        maxTemporalLayerCount: %i\n",
           enc->caps.maxTemporalLayerCount);
    av_log(avctx, AV_LOG_VERBOSE, "        expectDyadicTemporalLayerPattern: %i\n",
           enc->caps.expectDyadicTemporalLayerPattern);
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

    /* Prepare SEI */
    if (enc->unit_elems & UNIT_SEI_IDENTIFIER) {
        int len;

        memcpy(enc->sei_identifier.uuid_iso_iec_11578,
               vulkan_encode_h264_sei_identifier_uuid,
               sizeof(enc->sei_identifier.uuid_iso_iec_11578));

        len = snprintf(NULL, 0,
                       "%s / Vulkan video %i.%i.%i / %s %i.%i.%i / %s",
                       LIBAVCODEC_IDENT,
                       CODEC_VER(ff_vk_enc_h264_desc.ext_props.specVersion),
                       s->driver_props.driverName,
                       CODEC_VER(s->props.properties.driverVersion),
                       s->props.properties.deviceName);

        if (len >= 0) {
            enc->sei_identifier_string = av_malloc(len + 1);
            if (!enc->sei_identifier_string)
                return AVERROR(ENOMEM);

            len = snprintf(enc->sei_identifier_string, len + 1,
                           "%s / Vulkan video %i.%i.%i / %s %i.%i.%i / %s",
                           LIBAVCODEC_IDENT,
                           CODEC_VER(ff_vk_enc_h264_desc.ext_props.specVersion),
                           s->driver_props.driverName,
                           CODEC_VER(s->props.properties.driverVersion),
                           s->props.properties.deviceName);

            enc->sei_identifier.data        = enc->sei_identifier_string;
            enc->sei_identifier.data_length = len + 1;
        }
    }

    /* Init CBS */
    err = ff_cbs_init(&enc->cbs, AV_CODEC_ID_H264, avctx);
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

static av_cold int vulkan_encode_h264_close(AVCodecContext *avctx)
{
    VulkanEncodeH264Context *enc = avctx->priv_data;
    ff_vulkan_encode_uninit(&enc->common);
    return 0;
}

#define OFFSET(x) offsetof(VulkanEncodeH264Context, x)
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM)
static const AVOption vulkan_encode_h264_options[] = {
    HW_BASE_ENCODE_COMMON_OPTIONS,
    VULKAN_ENCODE_COMMON_OPTIONS,

    { "profile", "Set profile (profile_idc and constraint_set*_flag)",
      OFFSET(common.opts.profile), AV_OPT_TYPE_INT,
      { .i64 = AV_PROFILE_UNKNOWN }, AV_PROFILE_UNKNOWN, 0xffff, FLAGS, .unit = "profile" },

#define PROFILE(name, value)  name, NULL, 0, AV_OPT_TYPE_CONST, \
      { .i64 = value }, 0, 0, FLAGS, .unit = "profile"
    { PROFILE("constrained_baseline", AV_PROFILE_H264_CONSTRAINED_BASELINE) },
    { PROFILE("main",                 AV_PROFILE_H264_MAIN) },
    { PROFILE("high",                 AV_PROFILE_H264_HIGH) },
    { PROFILE("high444p",             AV_PROFILE_H264_HIGH_10) },
#undef PROFILE

    { "level", "Set level (level_idc)",
      OFFSET(common.opts.level), AV_OPT_TYPE_INT,
      { .i64 = AV_LEVEL_UNKNOWN }, AV_LEVEL_UNKNOWN, 0xff, FLAGS, .unit = "level" },

#define LEVEL(name, value) name, NULL, 0, AV_OPT_TYPE_CONST, \
      { .i64 = value }, 0, 0, FLAGS, .unit = "level"
    { LEVEL("1",   10) },
    { LEVEL("1.1", 11) },
    { LEVEL("1.2", 12) },
    { LEVEL("1.3", 13) },
    { LEVEL("2",   20) },
    { LEVEL("2.1", 21) },
    { LEVEL("2.2", 22) },
    { LEVEL("3",   30) },
    { LEVEL("3.1", 31) },
    { LEVEL("3.2", 32) },
    { LEVEL("4",   40) },
    { LEVEL("4.1", 41) },
    { LEVEL("4.2", 42) },
    { LEVEL("5",   50) },
    { LEVEL("5.1", 51) },
    { LEVEL("5.2", 52) },
    { LEVEL("6",   60) },
    { LEVEL("6.1", 61) },
    { LEVEL("6.2", 62) },
#undef LEVEL

    { "coder", "Entropy coder type", OFFSET(unit_opts.cabac), AV_OPT_TYPE_INT, { .i64 = 1 }, 0, 1, FLAGS, "coder" },
        { "cabac", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 1 }, INT_MIN, INT_MAX, FLAGS, "coder" },
        { "vlc",   NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 0 }, INT_MIN, INT_MAX, FLAGS, "coder" },

    { "units", "Set units to include", OFFSET(unit_elems), AV_OPT_TYPE_FLAGS, { .i64 = UNIT_AUD | UNIT_SEI_IDENTIFIER | UNIT_SEI_RECOVERY | UNIT_SEI_TIMING | UNIT_SEI_A53_CC }, 0, INT_MAX, FLAGS, "units" },
        { "aud",        "Include AUD units", 0, AV_OPT_TYPE_CONST, { .i64 = UNIT_AUD }, INT_MIN, INT_MAX, FLAGS, "units" },
        { "identifier", "Include encoder version identifier", 0, AV_OPT_TYPE_CONST, { .i64 = UNIT_SEI_IDENTIFIER }, INT_MIN, INT_MAX, FLAGS, "units" },
        { "timing",     "Include timing parameters (buffering_period and pic_timing)", 0, AV_OPT_TYPE_CONST, { .i64 = UNIT_SEI_TIMING }, INT_MIN, INT_MAX, FLAGS, "units" },
        { "recovery",   "Include recovery points where appropriate", 0, AV_OPT_TYPE_CONST, { .i64 = UNIT_SEI_RECOVERY }, INT_MIN, INT_MAX, FLAGS, "units" },
        { "a53_cc",     "Include A/53 caption data", 0, AV_OPT_TYPE_CONST, { .i64 = UNIT_SEI_A53_CC }, INT_MIN, INT_MAX, FLAGS, "units" },

    { NULL },
};

static const FFCodecDefault vulkan_encode_h264_defaults[] = {
    { "b",              "0"   },
    { "bf",             "2"   },
    { "g",              "300" },
    { "i_qfactor",      "1"   },
    { "i_qoffset",      "0"   },
    { "b_qfactor",      "1"   },
    { "b_qoffset",      "0"   },
    { "qmin",           "-1"  },
    { "qmax",           "-1"  },
    { NULL },
};

static const AVClass vulkan_encode_h264_class = {
    .class_name = "h264_vulkan",
    .item_name  = av_default_item_name,
    .option     = vulkan_encode_h264_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_h264_vulkan_encoder = {
    .p.name         = "h264_vulkan",
    CODEC_LONG_NAME("H.264/AVC (Vulkan)"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_H264,
    .priv_data_size = sizeof(VulkanEncodeH264Context),
    .init           = &vulkan_encode_h264_init,
    FF_CODEC_RECEIVE_PACKET_CB(&ff_vulkan_encode_receive_packet),
    .close          = &vulkan_encode_h264_close,
    .p.priv_class   = &vulkan_encode_h264_class,
    .p.capabilities = AV_CODEC_CAP_DELAY |
                      AV_CODEC_CAP_HARDWARE |
                      AV_CODEC_CAP_DR1 |
                      AV_CODEC_CAP_ENCODER_FLUSH |
                      AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
    .defaults       = vulkan_encode_h264_defaults,
    .p.pix_fmts = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_VULKAN,
        AV_PIX_FMT_NONE,
    },
    .hw_configs     = ff_vulkan_encode_hw_configs,
    .p.wrapper_name = "vulkan",
};
