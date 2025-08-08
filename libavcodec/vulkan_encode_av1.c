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
#include "cbs_av1.h"
#include "av1_levels.h"
#include "libavutil/mastering_display_metadata.h"

#include "codec_internal.h"
#include "vulkan_encode.h"

#include "libavutil/avassert.h"

const FFVulkanEncodeDescriptor ff_vk_enc_av1_desc = {
    .codec_id         = AV_CODEC_ID_AV1,
    .encode_extension = FF_VK_EXT_VIDEO_ENCODE_AV1,
    .encode_op        = VK_VIDEO_CODEC_OPERATION_ENCODE_AV1_BIT_KHR,
    .ext_props = {
        .extensionName = VK_STD_VULKAN_VIDEO_CODEC_AV1_ENCODE_EXTENSION_NAME,
        .specVersion   = VK_STD_VULKAN_VIDEO_CODEC_AV1_ENCODE_SPEC_VERSION,
    },
};

enum UnitElems {
    UNIT_MASTERING_DISPLAY   = 1 << 0,
    UNIT_CONTENT_LIGHT_LEVEL = 1 << 1,
};

typedef struct VulkanEncodeAV1Picture {
    int slot;
    int64_t last_idr_frame;

    enum UnitElems units_needed;

    StdVideoAV1TileInfo tile_info;
    StdVideoAV1Quantization quantization;
    StdVideoAV1Segmentation segmentation;
    StdVideoAV1LoopFilter loop_filter;
    StdVideoAV1CDEF cdef;
    StdVideoAV1LoopRestoration loop_restoration;
    StdVideoAV1GlobalMotion global_motion;

    StdVideoEncodeAV1PictureInfo av1pic_info;
    VkVideoEncodeAV1PictureInfoKHR vkav1pic_info;

    StdVideoEncodeAV1ExtensionHeader ext_header;
    StdVideoEncodeAV1ReferenceInfo av1dpb_info;
    VkVideoEncodeAV1DpbSlotInfoKHR vkav1dpb_info;

    VkVideoEncodeAV1RateControlInfoKHR vkrc_info;
    VkVideoEncodeAV1RateControlLayerInfoKHR vkrc_layer_info;
    VkVideoEncodeAV1GopRemainingFrameInfoKHR vkrc_remaining;
} VulkanEncodeAV1Picture;

typedef struct VulkanEncodeAV1Context {
    FFVulkanEncodeContext common;

    CodedBitstreamContext *cbs;
    CodedBitstreamFragment current_access_unit;

    enum UnitElems unit_elems;
    AV1RawOBU seq_hdr_obu;
    AV1RawOBU meta_cll_obu;
    AV1RawOBU meta_mastering_obu;

    VkVideoEncodeAV1ProfileInfoKHR profile;

    VkVideoEncodeAV1CapabilitiesKHR caps;
    VkVideoEncodeAV1QualityLevelPropertiesKHR quality_props;

    uint64_t hrd_buffer_size;
    uint64_t initial_buffer_fullness;

    int uniform_tile;
    int tile_cols;
    int tile_rows;

    int seq_tier;
    int seq_level_idx;

    int q_idx_idr;
    int q_idx_p;
    int q_idx_b;

    uint8_t *padding_payload;
} VulkanEncodeAV1Context;

static int init_pic_rc(AVCodecContext *avctx, FFHWBaseEncodePicture *pic,
                       VkVideoEncodeRateControlInfoKHR *rc_info,
                       VkVideoEncodeRateControlLayerInfoKHR *rc_layer)
{
    VulkanEncodeAV1Context *enc = avctx->priv_data;
    FFVulkanEncodeContext  *ctx = &enc->common;
    VulkanEncodeAV1Picture  *ap = pic->codec_priv;

    /* This can be easy to calculate */
    ap->vkrc_remaining = (VkVideoEncodeAV1GopRemainingFrameInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_AV1_GOP_REMAINING_FRAME_INFO_KHR,
        .useGopRemainingFrames = 0,
        .gopRemainingIntra = 0,
        .gopRemainingPredictive = 0,
        .gopRemainingBipredictive = 0,
    };

    ap->vkrc_info = (VkVideoEncodeAV1RateControlInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_AV1_RATE_CONTROL_INFO_KHR,
        .flags = VK_VIDEO_ENCODE_AV1_RATE_CONTROL_REFERENCE_PATTERN_FLAT_BIT_KHR |
                 VK_VIDEO_ENCODE_AV1_RATE_CONTROL_REGULAR_GOP_BIT_KHR,
        .gopFrameCount = ctx->base.gop_size,
        .keyFramePeriod = ctx->base.gop_size,
        .consecutiveBipredictiveFrameCount = FFMAX(ctx->base.b_per_p - 1, 0),
        .temporalLayerCount = 0,
    };
    rc_info->pNext = &ap->vkrc_info;

    if (rc_info->rateControlMode > VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR) {
        rc_info->virtualBufferSizeInMs = (enc->hrd_buffer_size * 1000LL) / avctx->bit_rate;
        rc_info->initialVirtualBufferSizeInMs = (enc->initial_buffer_fullness * 1000LL) / avctx->bit_rate;

        ap->vkrc_layer_info = (VkVideoEncodeAV1RateControlLayerInfoKHR) {
            .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_AV1_RATE_CONTROL_LAYER_INFO_KHR,

            .useMinQIndex  = avctx->qmin > 0,
            .minQIndex.intraQIndex = avctx->qmin > 0 ? avctx->qmin : 0,
            .minQIndex.predictiveQIndex = avctx->qmin > 0 ? avctx->qmin : 0,
            .minQIndex.bipredictiveQIndex = avctx->qmin > 0 ? avctx->qmin : 0,

            .useMaxQIndex  = avctx->qmax > 0,
            .maxQIndex.intraQIndex = avctx->qmax > 0 ? avctx->qmax : 0,
            .maxQIndex.predictiveQIndex = avctx->qmax > 0 ? avctx->qmax : 0,
            .maxQIndex.bipredictiveQIndex = avctx->qmax > 0 ? avctx->qmax : 0,

            .useMaxFrameSize = 0,
        };
        rc_layer->pNext = &ap->vkrc_layer_info;
        ap->vkrc_info.temporalLayerCount = 1;
    }

    return 0;
}

static void set_name_slot(int slot, int *slot_indices, uint32_t allowed_idx, int group)
{
    int from = group ? AV1_REF_FRAME_GOLDEN : 0;
    int to = group ? AV1_REFS_PER_FRAME : AV1_REF_FRAME_GOLDEN;

    for (int i = from; i < to; i++) {
        if ((slot_indices[i] == -1) && (allowed_idx & (1 << i))) {
            slot_indices[i] = slot;
            return;
        }
    }

    av_assert0(0);
}


static int init_pic_params(AVCodecContext *avctx, FFHWBaseEncodePicture *pic,
                           VkVideoEncodeInfoKHR *encode_info)
{
    VulkanEncodeAV1Context *enc = avctx->priv_data;
    FFVulkanEncodeContext *ctx = &enc->common;
    FFHWBaseEncodeContext *base_ctx = &ctx->base;

    VulkanEncodeAV1Picture *ap = pic->codec_priv;
    FFHWBaseEncodePicture *ref;
    VulkanEncodeAV1Picture *ap_ref;
    VkVideoReferenceSlotInfoKHR *ref_slot;

    uint32_t ref_name_mask = 0x0;
    int name_slots[STD_VIDEO_AV1_REFS_PER_FRAME];

    StdVideoAV1Segmentation *segmentation = &ap->segmentation;
    StdVideoAV1LoopFilter  *loop_filter = &ap->loop_filter;
    StdVideoAV1Quantization *quantization = &ap->quantization;
    StdVideoAV1CDEF *cdef = &ap->cdef;
    StdVideoAV1LoopRestoration *loop_restoration = &ap->loop_restoration;
    StdVideoAV1GlobalMotion *global_motion = &ap->global_motion;
    StdVideoAV1TileInfo *tile_info = &ap->tile_info;
    static const int8_t default_loop_filter_ref_deltas[STD_VIDEO_AV1_TOTAL_REFS_PER_FRAME] =
        { 1, 0, 0, 0, -1, 0, -1, -1 };

    VkVideoEncodeAV1PredictionModeKHR pred_mode;
    VkVideoEncodeAV1RateControlGroupKHR rc_group;
    int lr_unit_shift = 0;
    int lr_uv_shift = 0;

    ap->ext_header = (StdVideoEncodeAV1ExtensionHeader) {
        .temporal_id = 0,
        .spatial_id = 0,
    };

    *tile_info = (StdVideoAV1TileInfo) {
        .flags = (StdVideoAV1TileInfoFlags) {
            .uniform_tile_spacing_flag = enc->uniform_tile,
        },
        .TileCols = enc->tile_cols,
        .TileRows = enc->tile_rows,
        .context_update_tile_id = 0,
        .tile_size_bytes_minus_1 = 0,
    };

    for (int i = 0; i < STD_VIDEO_AV1_TOTAL_REFS_PER_FRAME; i++) {
        global_motion->GmType[i] = 0;
        for (int j = 0; j < STD_VIDEO_AV1_GLOBAL_MOTION_PARAMS; j++) {
            global_motion->gm_params[i][j] = 0;
        }
    }

    for (int i = 0; i < STD_VIDEO_AV1_REFS_PER_FRAME; i++)
        name_slots[i] = -1;

    *loop_restoration = (StdVideoAV1LoopRestoration) {
        .FrameRestorationType[0] = STD_VIDEO_AV1_FRAME_RESTORATION_TYPE_NONE,
        .FrameRestorationType[1] = STD_VIDEO_AV1_FRAME_RESTORATION_TYPE_NONE,
        .FrameRestorationType[2] = STD_VIDEO_AV1_FRAME_RESTORATION_TYPE_NONE,
        .LoopRestorationSize[0] = 1 + lr_unit_shift,
        .LoopRestorationSize[1] = 1 + lr_unit_shift - lr_uv_shift,
        .LoopRestorationSize[2] = 1 + lr_unit_shift - lr_uv_shift,
    };

    *cdef = (StdVideoAV1CDEF) {
        .cdef_damping_minus_3 = 0,
        .cdef_bits = 0,
    };

    for (int i = 0; i < STD_VIDEO_AV1_MAX_SEGMENTS; i++) {
        segmentation->FeatureEnabled[i] = 0x0;
        for (int j = 0; j < STD_VIDEO_AV1_SEG_LVL_MAX; j++) {
            segmentation->FeatureEnabled[i] |= 0x0;
            segmentation->FeatureData[i][j] = 0;
        }
    }

    *loop_filter = (StdVideoAV1LoopFilter) {
        .flags = (StdVideoAV1LoopFilterFlags) {
            .loop_filter_delta_enabled = 0,
            .loop_filter_delta_update = 0,
        },
        .loop_filter_level = { 0 },
        .loop_filter_sharpness = 0,
        .update_ref_delta = 0,
        .loop_filter_ref_deltas = { 0 },
        .update_mode_delta = 0,
        .loop_filter_mode_deltas = { 0 },
    };
    loop_filter->update_mode_delta = 1;
    memcpy(loop_filter->loop_filter_ref_deltas, default_loop_filter_ref_deltas,
           STD_VIDEO_AV1_TOTAL_REFS_PER_FRAME * sizeof(int8_t));

    *quantization = (StdVideoAV1Quantization) {
        .flags = (StdVideoAV1QuantizationFlags) {
            .using_qmatrix = 0,
            .diff_uv_delta = 0,
            /* Reserved */
        },
        .base_q_idx = 0, /* Set later */
        .DeltaQYDc = 0,
        .DeltaQUDc = 0,
        .DeltaQUAc = 0,
        .DeltaQVDc = 0,
        .DeltaQVAc = 0,
        .qm_y = 0,
        .qm_u = 0,
        .qm_v = 0,
    };

    ref_slot = (VkVideoReferenceSlotInfoKHR *)encode_info->pSetupReferenceSlot;
    ap->av1pic_info = (StdVideoEncodeAV1PictureInfo) {
        .flags = (StdVideoEncodeAV1PictureInfoFlags) {
            .error_resilient_mode = (pic->type == FF_HW_PICTURE_TYPE_I ||
                                     pic->type == FF_HW_PICTURE_TYPE_IDR) &&
                                    (pic->display_order <= pic->encode_order),
            .disable_cdf_update = 0,
            .use_superres = 0,
            .render_and_frame_size_different = 0,
            .allow_screen_content_tools = 0,
            .is_filter_switchable = 0,
            .force_integer_mv = 0,
            .frame_size_override_flag = 0,
            .buffer_removal_time_present_flag = 0,
            .allow_intrabc = 0,
            .frame_refs_short_signaling = 0,
            .allow_high_precision_mv = 0,
            .is_motion_mode_switchable = 0,
            .use_ref_frame_mvs = 0,
            .disable_frame_end_update_cdf = 0,
            .allow_warped_motion = 0,
            .reduced_tx_set = 0,
            .skip_mode_present = 0,
            .delta_q_present = 0,
            .delta_lf_present = 0,
            .delta_lf_multi = 0,
            .segmentation_enabled = 0,
            .segmentation_update_map = 0,
            .segmentation_temporal_update = 0,
            .segmentation_update_data = 0,
            .UsesLr = 0,
            .usesChromaLr = 0,
            .show_frame = pic->display_order <= pic->encode_order,
            .showable_frame = 0,
            /* Reserved */
        },
        .frame_type = 0, // set later
        .frame_presentation_time = 0,
        .current_frame_id = ref_slot->slotIndex,
        .order_hint = 0, // set later
        .primary_ref_frame = 0, // set later
        .refresh_frame_flags = 0x0, // set later
        .coded_denom = 0,
        .render_width_minus_1 = base_ctx->surface_width - 1,
        .render_height_minus_1 = base_ctx->surface_height - 1,
        .interpolation_filter = 0,
        .TxMode = STD_VIDEO_AV1_TX_MODE_SELECT,
        .delta_q_res = 0,
        .delta_lf_res = 0,
        .ref_order_hint = { 0 }, // set later
        .ref_frame_idx = { 0 }, // set later
        /* Reserved */
        .delta_frame_id_minus_1 = { 0 },

//        .pTileInfo = tile_info, TODO FIX
        .pQuantization = quantization,
        .pSegmentation = segmentation,
        .pLoopFilter = loop_filter,
        .pCDEF = cdef,
        .pLoopRestoration = loop_restoration,
        .pGlobalMotion = global_motion,
        .pExtensionHeader = &ap->ext_header,
        .pBufferRemovalTimes = NULL,
    };

    switch (pic->type) {
    case FF_HW_PICTURE_TYPE_I:
    case FF_HW_PICTURE_TYPE_IDR:
        av_assert0(pic->nb_refs[0] == 0 || pic->nb_refs[1]);
        ap->av1pic_info.frame_type = STD_VIDEO_AV1_FRAME_TYPE_KEY;
        ap->av1pic_info.refresh_frame_flags = 0xFF;
        quantization->base_q_idx = enc->q_idx_idr;
        ap->slot = 0;
        ap->last_idr_frame = pic->display_order;
        pred_mode = VK_VIDEO_ENCODE_AV1_PREDICTION_MODE_INTRA_ONLY_KHR;
        rc_group = VK_VIDEO_ENCODE_AV1_RATE_CONTROL_GROUP_INTRA_KHR;
        break;
    case FF_HW_PICTURE_TYPE_P:
        ref = pic->refs[0][pic->nb_refs[0] - 1];
        ap_ref = ref->codec_priv;

        ap->av1pic_info.frame_type = STD_VIDEO_AV1_FRAME_TYPE_INTER;
        quantization->base_q_idx = enc->q_idx_p;

        ap->last_idr_frame = ap_ref->last_idr_frame;
        ap->slot = !ap_ref->slot;

        ap->av1pic_info.refresh_frame_flags = 1 << ap->slot;

        /** set the nearest frame in L0 as all reference frame. */
        for (int i = 0; i < AV1_REFS_PER_FRAME; i++)
            ap->av1pic_info.ref_frame_idx[i] = ap_ref->slot;

        ap->av1pic_info.primary_ref_frame = ap_ref->slot;
        ap->av1pic_info.ref_order_hint[ap_ref->slot] = ref->display_order - ap_ref->last_idr_frame;
        rc_group = VK_VIDEO_ENCODE_AV1_RATE_CONTROL_GROUP_PREDICTIVE_KHR;
        pred_mode = VK_VIDEO_ENCODE_AV1_PREDICTION_MODE_SINGLE_REFERENCE_KHR;
        ref_name_mask = enc->caps.singleReferenceNameMask;
        set_name_slot(ap_ref->av1pic_info.current_frame_id, name_slots, ref_name_mask, 0);

//        vpic->ref_frame_ctrl_l0.fields.search_idx0 = AV1_REF_FRAME_LAST;

        /** set the 2nd nearest frame in L0 as Golden frame. */
        if ((pic->nb_refs[0] > 1) &&
            ((enc->caps.maxSingleReferenceCount > 1) ||
             (enc->caps.maxUnidirectionalCompoundReferenceCount > 0))) {
            if (enc->caps.maxUnidirectionalCompoundReferenceCount) {
                pred_mode = VK_VIDEO_ENCODE_AV1_PREDICTION_MODE_UNIDIRECTIONAL_COMPOUND_KHR;
                ref_name_mask = enc->caps.unidirectionalCompoundReferenceNameMask;
            }
            ref = pic->refs[0][pic->nb_refs[0] - 2];
            ap_ref = ref->codec_priv;
            ap->av1pic_info.ref_frame_idx[3] = ap_ref->slot;
            ap->av1pic_info.ref_order_hint[ap_ref->slot] = ref->display_order - ap_ref->last_idr_frame;
//            vpic->ref_frame_ctrl_l0.fields.search_idx1 = AV1_REF_FRAME_GOLDEN;
            set_name_slot(ap_ref->av1pic_info.current_frame_id, name_slots, ref_name_mask, 0);
        }
        break;
    case FF_HW_PICTURE_TYPE_B:
        ap->av1pic_info.frame_type = STD_VIDEO_AV1_FRAME_TYPE_INTER;
        quantization->base_q_idx = enc->q_idx_b;
        ap->av1pic_info.refresh_frame_flags = 0x0;

        rc_group = VK_VIDEO_ENCODE_AV1_RATE_CONTROL_GROUP_BIPREDICTIVE_KHR;
        pred_mode = VK_VIDEO_ENCODE_AV1_PREDICTION_MODE_BIDIRECTIONAL_COMPOUND_KHR;
        ref_name_mask = enc->caps.bidirectionalCompoundReferenceNameMask;

//        fh->reference_select = 1;
        /** B frame will not be referenced, disable its recon frame. */
//        vpic->picture_flags.bits.disable_frame_recon = 1;

        /** Use LAST_FRAME and BWDREF_FRAME for reference. */
//        vpic->ref_frame_ctrl_l0.fields.search_idx0 = AV1_REF_FRAME_LAST;
//        vpic->ref_frame_ctrl_l1.fields.search_idx0 = AV1_REF_FRAME_BWDREF;

        ref = pic->refs[0][pic->nb_refs[0] - 1];
        ap_ref = ref->codec_priv;
        ap->last_idr_frame = ap_ref->last_idr_frame;
        ap->av1pic_info.primary_ref_frame = ap_ref->slot;
        ap->av1pic_info.ref_order_hint[ap_ref->slot] = ref->display_order - ap_ref->last_idr_frame;
        for (int i = 0; i < AV1_REF_FRAME_GOLDEN; i++)
            ap->av1pic_info.ref_frame_idx[i] = ap_ref->slot;
        set_name_slot(ap_ref->av1pic_info.current_frame_id, name_slots, ref_name_mask, 0);

        ref = pic->refs[1][pic->nb_refs[1] - 1];
        ap_ref = ref->codec_priv;
        ap->av1pic_info.ref_order_hint[ap_ref->slot] = ref->display_order - ap_ref->last_idr_frame;
        for (int i = AV1_REF_FRAME_GOLDEN; i < AV1_REFS_PER_FRAME; i++)
            ap->av1pic_info.ref_frame_idx[i] = ap_ref->slot;
        set_name_slot(ap_ref->av1pic_info.current_frame_id, name_slots, ref_name_mask, 1);
        break;
    }

    ap->av1pic_info.flags.showable_frame = ap->av1pic_info.frame_type != STD_VIDEO_AV1_FRAME_TYPE_KEY;
    ap->av1pic_info.order_hint = pic->display_order - ap->last_idr_frame;

    ap->vkav1pic_info = (VkVideoEncodeAV1PictureInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_AV1_PICTURE_INFO_KHR,
        .pNext = NULL,
        .predictionMode = pred_mode,
        .rateControlGroup = rc_group,
        .constantQIndex = quantization->base_q_idx,
        .pStdPictureInfo = &ap->av1pic_info,
        // .referenceNameSlotIndices is set below
        .primaryReferenceCdfOnly = 0,
        .generateObuExtensionHeader = 0,
    };
    encode_info->pNext = &ap->vkav1pic_info;

    for (int i = 0; i < FF_ARRAY_ELEMS(ap->vkav1pic_info.referenceNameSlotIndices); i++)
        ap->vkav1pic_info.referenceNameSlotIndices[i] = name_slots[i];

    ref_slot = (VkVideoReferenceSlotInfoKHR *)encode_info->pSetupReferenceSlot;
    ref_slot->pNext = &ap->vkav1dpb_info;

    ap->av1dpb_info = (StdVideoEncodeAV1ReferenceInfo) {
        .flags = (StdVideoEncodeAV1ReferenceInfoFlags) {
            .disable_frame_end_update_cdf = 0,
            .segmentation_enabled = 0,
            /* Reserved */
        },
        .RefFrameId = ref_slot->slotIndex,
        .frame_type = ap->av1pic_info.frame_type,
        .OrderHint = pic->display_order - ap->last_idr_frame,
        /* Reserved */
        .pExtensionHeader = &ap->ext_header,
    };

    ap->vkav1dpb_info = (VkVideoEncodeAV1DpbSlotInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_AV1_DPB_SLOT_INFO_KHR,
        .pStdReferenceInfo = &ap->av1dpb_info,
    };

    ap->units_needed = 0;
    if (pic->type == FF_HW_PICTURE_TYPE_IDR) {
        AVFrameSideData *sd = NULL;
        if (enc->unit_elems & UNIT_MASTERING_DISPLAY)
            sd = av_frame_get_side_data(pic->input_image,
                                        AV_FRAME_DATA_MASTERING_DISPLAY_METADATA);
        if (sd) {
            AVMasteringDisplayMetadata *mdm =
                (AVMasteringDisplayMetadata *)sd->data;
            if (mdm->has_primaries && mdm->has_luminance) {
                AV1RawOBU              *obu = &enc->meta_mastering_obu;
                AV1RawMetadata          *md = &obu->obu.metadata;
                AV1RawMetadataHDRMDCV *mdcv = &md->metadata.hdr_mdcv;
                const int        chroma_den = 1 << 16;
                const int      max_luma_den = 1 << 8;
                const int      min_luma_den = 1 << 14;

                memset(obu, 0, sizeof(*obu));
                obu->header.obu_type = AV1_OBU_METADATA;
                md->metadata_type = AV1_METADATA_TYPE_HDR_MDCV;

                for (int i = 0; i < 3; i++) {
                    mdcv->primary_chromaticity_x[i] =
                        av_rescale(mdm->display_primaries[i][0].num, chroma_den,
                                   mdm->display_primaries[i][0].den);
                    mdcv->primary_chromaticity_y[i] =
                        av_rescale(mdm->display_primaries[i][1].num, chroma_den,
                                   mdm->display_primaries[i][1].den);
                }

                mdcv->white_point_chromaticity_x =
                    av_rescale(mdm->white_point[0].num, chroma_den,
                               mdm->white_point[0].den);
                mdcv->white_point_chromaticity_y =
                    av_rescale(mdm->white_point[1].num, chroma_den,
                               mdm->white_point[1].den);

                mdcv->luminance_max =
                    av_rescale(mdm->max_luminance.num, max_luma_den,
                               mdm->max_luminance.den);
                mdcv->luminance_min =
                    av_rescale(mdm->min_luminance.num, min_luma_den,
                               mdm->min_luminance.den);
                ap->units_needed |= UNIT_MASTERING_DISPLAY;
            }
        }

        if (enc->unit_elems & UNIT_CONTENT_LIGHT_LEVEL)
            sd = av_frame_get_side_data(pic->input_image,
                                        AV_FRAME_DATA_CONTENT_LIGHT_LEVEL);
        if (sd) {
            AVContentLightMetadata *cllm = (AVContentLightMetadata *)sd->data;
            AV1RawOBU               *obu = &enc->meta_cll_obu;
            AV1RawMetadata           *md = &obu->obu.metadata;
            AV1RawMetadataHDRCLL    *cll = &md->metadata.hdr_cll;

            memset(obu, 0, sizeof(*obu));
            obu->header.obu_type = AV1_OBU_METADATA;
            md->metadata_type    = AV1_METADATA_TYPE_HDR_CLL;
            cll->max_cll         = cllm->MaxCLL;
            cll->max_fall        = cllm->MaxFALL;

            ap->units_needed |= UNIT_CONTENT_LIGHT_LEVEL;
        }
    }

    return 0;
}

static int init_profile(AVCodecContext *avctx,
                        VkVideoProfileInfoKHR *profile, void *pnext)
{
    VkResult ret;
    VulkanEncodeAV1Context *enc = avctx->priv_data;
    FFVulkanEncodeContext *ctx = &enc->common;
    FFVulkanContext *s = &ctx->s;
    FFVulkanFunctions *vk = &ctx->s.vkfn;
    FFHWBaseEncodeContext *base_ctx = &ctx->base;

    VkVideoEncodeAV1CapabilitiesKHR av1_caps = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_AV1_CAPABILITIES_KHR,
    };
    VkVideoEncodeCapabilitiesKHR enc_caps = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_CAPABILITIES_KHR,
        .pNext = &av1_caps,
    };
    VkVideoCapabilitiesKHR caps = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_CAPABILITIES_KHR,
        .pNext = &enc_caps,
    };

    /* In order of preference */
    int last_supported = AV_PROFILE_UNKNOWN;
    static const int known_profiles[] = {
        AV_PROFILE_AV1_MAIN,
        AV_PROFILE_AV1_HIGH,
        AV_PROFILE_AV1_PROFESSIONAL,
    };
    int nb_profiles = FF_ARRAY_ELEMS(known_profiles);

    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(s->frames->sw_format);
    if (!desc)
        return AVERROR(EINVAL);

    if (s->frames->sw_format == AV_PIX_FMT_NV12 ||
        s->frames->sw_format == AV_PIX_FMT_P010)
        nb_profiles = 1;

    enc->profile = (VkVideoEncodeAV1ProfileInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_AV1_PROFILE_INFO_KHR,
        .pNext = pnext,
        .stdProfile = ff_vk_av1_profile_to_vk(avctx->profile),
    };
    profile->pNext = &enc->profile;

    /* Set level */
    if (avctx->level == AV_LEVEL_UNKNOWN) {
        const AV1LevelDescriptor *level;
        float framerate = 0.0;

        if (avctx->framerate.num > 0 && avctx->framerate.den > 0)
            framerate = av_q2d(avctx->framerate);

        level = ff_av1_guess_level(avctx->bit_rate, enc->seq_tier,
                                   base_ctx->surface_width, base_ctx->surface_height,
                                   enc->tile_rows * enc->tile_cols,
                                   enc->tile_cols, framerate);
        if (level) {
            av_log(avctx, AV_LOG_VERBOSE, "Using level %s.\n", level->name);
            enc->seq_level_idx = ff_vk_av1_level_to_vk(level->level_idx);
        } else {
            av_log(avctx, AV_LOG_VERBOSE, "Stream will not conform to "
                   "any normal level, using level 7.3 by default.\n");
            enc->seq_level_idx = STD_VIDEO_AV1_LEVEL_7_3;
            enc->seq_tier = 1;
        }
    } else {
        enc->seq_level_idx = ff_vk_av1_level_to_vk(avctx->level);
    }

    /* User has explicitly specified a profile. */
    if (avctx->profile != AV_PROFILE_UNKNOWN)
        return 0;

    av_log(avctx, AV_LOG_DEBUG, "Supported profiles:\n");
    for (int i = 0; i < nb_profiles; i++) {
        enc->profile.stdProfile = ff_vk_av1_profile_to_vk(known_profiles[i]);
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

    enc->profile.stdProfile = ff_vk_av1_profile_to_vk(last_supported);
    av_log(avctx, AV_LOG_VERBOSE, "Using profile %s\n",
           avcodec_profile_name(avctx->codec_id, last_supported));
    avctx->profile = last_supported;

    return 0;
}

static int init_enc_options(AVCodecContext *avctx)
{
    VulkanEncodeAV1Context *enc = avctx->priv_data;

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
        enc->q_idx_p = av_clip(enc->common.opts.qp,
                               enc->caps.minQIndex, enc->caps.maxQIndex);
        if (fabs(avctx->i_quant_factor) > 0.0)
            enc->q_idx_idr =
                av_clip((fabs(avctx->i_quant_factor) * enc->q_idx_p  +
                         avctx->i_quant_offset) + 0.5,
                        0, 255);
        else
            enc->q_idx_idr = enc->q_idx_p;

        if (fabs(avctx->b_quant_factor) > 0.0)
            enc->q_idx_b =
                av_clip((fabs(avctx->b_quant_factor) * enc->q_idx_p  +
                         avctx->b_quant_offset) + 0.5,
                        0, 255);
        else
            enc->q_idx_b = enc->q_idx_p;
    } else {
        /** Arbitrary value */
        enc->q_idx_idr = enc->q_idx_p = enc->q_idx_b = 128;
    }

    return 0;
}

static av_cold int init_sequence_headers(AVCodecContext *avctx)
{
    VulkanEncodeAV1Context *enc = avctx->priv_data;
    FFVulkanEncodeContext *ctx = &enc->common;
    FFVulkanContext *s = &ctx->s;
    FFHWBaseEncodeContext *base_ctx = &ctx->base;

    AV1RawOBU *seq_obu = &enc->seq_hdr_obu;
    AV1RawSequenceHeader *seq = &seq_obu->obu.sequence_header;

    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(s->frames->sw_format);
    if (!desc)
        return AVERROR(EINVAL);

    seq_obu->header.obu_type = AV1_OBU_SEQUENCE_HEADER;
    *seq = (AV1RawSequenceHeader) {
        .seq_profile = avctx->profile,
        .seq_force_integer_mv = seq->seq_force_screen_content_tools ?
                                AV1_SELECT_SCREEN_CONTENT_TOOLS :
                                AV1_SELECT_INTEGER_MV,
        .frame_width_bits_minus_1 = av_log2(base_ctx->surface_width),
        .frame_height_bits_minus_1 = av_log2(base_ctx->surface_height),
        .max_frame_width_minus_1 = base_ctx->surface_width - 1,
        .max_frame_height_minus_1 = base_ctx->surface_height - 1,
        .enable_order_hint = 1,
        .order_hint_bits_minus_1 = av_clip_intp2(av_log2(ctx->base.gop_size), 3),
        .use_128x128_superblock = !!(enc->caps.superblockSizes & VK_VIDEO_ENCODE_AV1_SUPERBLOCK_SIZE_128_BIT_KHR),
        .color_config = (AV1RawColorConfig) {
            .high_bitdepth = desc->comp[0].depth > 8,
            .color_primaries                = avctx->color_primaries,
            .transfer_characteristics       = avctx->color_trc,
            .matrix_coefficients            = avctx->colorspace,
            .color_description_present_flag = (avctx->color_primaries != AVCOL_PRI_UNSPECIFIED ||
                                               avctx->color_trc       != AVCOL_TRC_UNSPECIFIED ||
                                               avctx->colorspace      != AVCOL_SPC_UNSPECIFIED),
            .subsampling_x                  = desc->log2_chroma_w,
            .subsampling_y                  = desc->log2_chroma_h,
            .chroma_sample_position         = avctx->chroma_sample_location == AVCHROMA_LOC_LEFT ?
                                              AV1_CSP_VERTICAL :
                                              avctx->chroma_sample_location == AVCHROMA_LOC_TOPLEFT ?
                                              AV1_CSP_COLOCATED :
                                              AV1_CSP_UNKNOWN,
        },

        /* Operating point */
        .seq_tier = { enc->seq_tier },
        .seq_level_idx = { enc->seq_level_idx },
        .decoder_buffer_delay = { base_ctx->decode_delay },
        .encoder_buffer_delay = { base_ctx->output_delay },
        .operating_points_cnt_minus_1 = 1 - 1,
    };

    return 0;
}

typedef struct VulkanAV1Units {
    StdVideoAV1SequenceHeader seq_hdr;
    StdVideoAV1TimingInfo timing_info;
    StdVideoAV1ColorConfig color_config;

    StdVideoEncodeAV1DecoderModelInfo decoder_model;
    StdVideoEncodeAV1OperatingPointInfo operating_points[AV1_MAX_OPERATING_POINTS];
    int nb_operating_points;
} VulkanAV1Units;

static av_cold int base_unit_to_vk(AVCodecContext *avctx, VulkanAV1Units *vk_units)
{
    VulkanEncodeAV1Context *enc = avctx->priv_data;

    AV1RawOBU *seq_obu = &enc->seq_hdr_obu;
    AV1RawSequenceHeader *seq = &seq_obu->obu.sequence_header;

    StdVideoAV1SequenceHeader *seq_hdr = &vk_units->seq_hdr;
    StdVideoAV1TimingInfo *timing_info = &vk_units->timing_info;
    StdVideoAV1ColorConfig *color_config = &vk_units->color_config;

    StdVideoEncodeAV1OperatingPointInfo *operating_points = vk_units->operating_points;

    *timing_info = (StdVideoAV1TimingInfo) {
        .flags = (StdVideoAV1TimingInfoFlags) {
            .equal_picture_interval = seq->timing_info.equal_picture_interval,
        },
        .num_units_in_display_tick = seq->timing_info.num_units_in_display_tick,
        .time_scale = seq->timing_info.time_scale,
        .num_ticks_per_picture_minus_1 = seq->timing_info.num_ticks_per_picture_minus_1,
    };

    *color_config = (StdVideoAV1ColorConfig) {
        .flags = (StdVideoAV1ColorConfigFlags) {
            .mono_chrome = seq->color_config.mono_chrome,
            .color_range = seq->color_config.color_range,
            .separate_uv_delta_q = seq->color_config.separate_uv_delta_q,
        },
        .BitDepth = seq->color_config.twelve_bit    ? 12 :
                    seq->color_config.high_bitdepth ? 10 : 8,
        .subsampling_x = seq->color_config.subsampling_x,
        .subsampling_y = seq->color_config.subsampling_y,
        .color_primaries = seq->color_config.color_primaries,
        .transfer_characteristics = seq->color_config.transfer_characteristics,
        .matrix_coefficients = seq->color_config.matrix_coefficients,
    };

    *seq_hdr = (StdVideoAV1SequenceHeader) {
        .flags = (StdVideoAV1SequenceHeaderFlags) {
            .still_picture = seq->still_picture,
            .reduced_still_picture_header = seq->reduced_still_picture_header,
            .use_128x128_superblock = seq->use_128x128_superblock,
            .enable_filter_intra = seq->enable_filter_intra,
            .enable_intra_edge_filter = seq->enable_intra_edge_filter,
            .enable_interintra_compound = seq->enable_interintra_compound,
            .enable_masked_compound = seq->enable_masked_compound,
            .enable_warped_motion = seq->enable_warped_motion,
            .enable_dual_filter = seq->enable_dual_filter,
            .enable_order_hint = seq->enable_order_hint,
            .enable_jnt_comp = seq->enable_jnt_comp,
            .enable_ref_frame_mvs = seq->enable_ref_frame_mvs,
            .frame_id_numbers_present_flag = seq->frame_id_numbers_present_flag,
            .enable_superres = seq->enable_superres,
            .enable_cdef = seq->enable_cdef,
            .enable_restoration = seq->enable_restoration,
            .film_grain_params_present = seq->film_grain_params_present,
            .timing_info_present_flag = seq->timing_info_present_flag,
            .initial_display_delay_present_flag = seq->initial_display_delay_present_flag,
        },
        .seq_profile = seq->seq_profile,
        .frame_width_bits_minus_1 = seq->frame_width_bits_minus_1,
        .frame_height_bits_minus_1 = seq->frame_height_bits_minus_1,
        .max_frame_width_minus_1 = seq->max_frame_width_minus_1,
        .max_frame_height_minus_1 = seq->max_frame_height_minus_1,
        .delta_frame_id_length_minus_2 = seq->delta_frame_id_length_minus_2,
        .additional_frame_id_length_minus_1 = seq->additional_frame_id_length_minus_1,
        .order_hint_bits_minus_1 = seq->order_hint_bits_minus_1,
        .seq_force_integer_mv = seq->seq_force_integer_mv,
        .seq_force_screen_content_tools = seq->seq_force_screen_content_tools,
        .pTimingInfo = timing_info,
        .pColorConfig = color_config,
    };

    for (int i = 0; i <= seq->operating_points_cnt_minus_1; i++) {
        operating_points[i] = (StdVideoEncodeAV1OperatingPointInfo) {
            .flags = (StdVideoEncodeAV1OperatingPointInfoFlags) {
                .decoder_model_present_for_this_op = seq->decoder_model_present_for_this_op[i],
                .low_delay_mode_flag = seq->low_delay_mode_flag[i],
                .initial_display_delay_present_for_this_op = seq->initial_display_delay_present_for_this_op[i],
                /* Reserved */
            },
            .operating_point_idc = seq->operating_point_idc[i],
            .seq_level_idx = seq->seq_level_idx[i],
            .seq_tier = seq->seq_tier[i],
            .decoder_buffer_delay = seq->decoder_buffer_delay[i],
            .encoder_buffer_delay = seq->encoder_buffer_delay[i],
            .initial_display_delay_minus_1 = seq->initial_display_delay_minus_1[i],
        };
    }
    vk_units->nb_operating_points = seq->operating_points_cnt_minus_1 + 1;

    return 0;
}

static int create_session_params(AVCodecContext *avctx)
{
    int err;
    VulkanEncodeAV1Context *enc = avctx->priv_data;
    FFVulkanEncodeContext *ctx = &enc->common;
    FFVulkanContext *s = &ctx->s;
    FFVulkanFunctions *vk = &ctx->s.vkfn;

    VulkanAV1Units vk_units = { 0 };

    VkVideoEncodeAV1SessionParametersCreateInfoKHR av1_params;

    /* Convert it to Vulkan */
    err = base_unit_to_vk(avctx, &vk_units);
    if (err < 0) {
        av_log(avctx, AV_LOG_ERROR, "Unable to convert sequence header to Vulkan: %s\n",
               av_err2str(err));
        return err;
    }

    /* Destroy the session params */
    if (ctx->session_params)
        vk->DestroyVideoSessionParametersKHR(s->hwctx->act_dev,
                                             ctx->session_params,
                                             s->hwctx->alloc);

    av1_params = (VkVideoEncodeAV1SessionParametersCreateInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_AV1_SESSION_PARAMETERS_CREATE_INFO_KHR,
        .pStdSequenceHeader = &vk_units.seq_hdr,
        .pStdDecoderModelInfo = &vk_units.decoder_model,
        .pStdOperatingPoints = vk_units.operating_points,
        .stdOperatingPointCount = vk_units.nb_operating_points,
    };

    return ff_vulkan_encode_create_session_params(avctx, ctx, &av1_params);
}

static int parse_feedback_units(AVCodecContext *avctx,
                                const uint8_t *data, size_t size)
{
    int err;
    VulkanEncodeAV1Context *enc = avctx->priv_data;
    AV1RawOBU *seq_obu = &enc->seq_hdr_obu;
    AV1RawSequenceHeader *seq = &seq_obu->obu.sequence_header;

    CodedBitstreamContext *cbs;
    CodedBitstreamFragment obu = { 0 };

    err = ff_cbs_init(&cbs, AV_CODEC_ID_AV1, avctx);
    if (err < 0)
        return err;

    err = ff_cbs_read(cbs, &obu, NULL, data, size);
    if (err < 0) {
        av_log(avctx, AV_LOG_ERROR, "Unable to parse feedback units, bad drivers: %s\n",
               av_err2str(err));
        return err;
    }

    /* If PPS has an override, just copy it entirely. */
    for (int i = 0; i < obu.nb_units; i++) {
        if (obu.units[i].type == AV1_OBU_SEQUENCE_HEADER) {
            AV1RawOBU *f_seq_obu = obu.units[i].content;
            AV1RawSequenceHeader *f_seq = &f_seq_obu->obu.sequence_header;
            seq->frame_width_bits_minus_1 = f_seq->frame_width_bits_minus_1;
            seq->frame_height_bits_minus_1 = f_seq->frame_height_bits_minus_1;
            seq->max_frame_width_minus_1 = f_seq->max_frame_width_minus_1;
            seq->max_frame_height_minus_1 = f_seq->max_frame_height_minus_1;
            seq->seq_choose_screen_content_tools = f_seq->seq_choose_screen_content_tools;
            seq->seq_force_screen_content_tools = f_seq->seq_force_screen_content_tools;
            seq->seq_choose_integer_mv = f_seq->seq_choose_integer_mv;
            seq->seq_force_integer_mv = f_seq->seq_force_integer_mv;
        }
    }

    ff_cbs_fragment_free(&obu);
    ff_cbs_close(&cbs);

    return 0;
}

static int init_base_units(AVCodecContext *avctx)
{
    int err;
    VkResult ret;
    VulkanEncodeAV1Context *enc = avctx->priv_data;
    FFVulkanEncodeContext *ctx = &enc->common;
    FFVulkanContext *s = &ctx->s;
    FFVulkanFunctions *vk = &ctx->s.vkfn;

    VkVideoEncodeSessionParametersGetInfoKHR params_info;
    VkVideoEncodeSessionParametersFeedbackInfoKHR params_feedback;

    void *data = NULL;
    size_t data_size = 0;

    /* Generate SPS/PPS unit info */
    err = init_sequence_headers(avctx);
    if (err < 0) {
        av_log(avctx, AV_LOG_ERROR, "Unable to initialize sequence header: %s\n",
               av_err2str(err));
        return err;
    }

    /* Create session parameters from them */
    err = create_session_params(avctx);
    if (err < 0)
        return err;

    params_info = (VkVideoEncodeSessionParametersGetInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_SESSION_PARAMETERS_GET_INFO_KHR,
        .videoSessionParameters = ctx->session_params,
    };
    params_feedback = (VkVideoEncodeSessionParametersFeedbackInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_SESSION_PARAMETERS_FEEDBACK_INFO_KHR,
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
        av_log(avctx, AV_LOG_ERROR, "Unable to get feedback for AV1 sequence header = %"SIZE_SPECIFIER"\n",
               data_size);
        return err;
    }

    ret = vk->GetEncodedVideoSessionParametersKHR(s->hwctx->act_dev, &params_info,
                                                  &params_feedback,
                                                  &data_size, data);
    if (ret != VK_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Error writing feedback units\n");
        return err;
    }

    av_log(avctx, AV_LOG_VERBOSE, "Feedback units written, overrides: %i\n",
           params_feedback.hasOverrides);

    params_feedback.hasOverrides = 1;

    /* No need to sync any overrides */
    if (!params_feedback.hasOverrides)
        return 0;

    /* Parse back tne units and override */
    err = parse_feedback_units(avctx, data, data_size);
    if (err < 0)
        return err;

    /* Create final session parameters */
    err = create_session_params(avctx);
    if (err < 0)
        return err;

    return 0;
}

static int vulkan_encode_av1_add_obu(AVCodecContext *avctx,
                                     CodedBitstreamFragment *au,
                                     uint8_t type, void *obu_unit)
{
    int err;

    err = ff_cbs_insert_unit_content(au, -1,
                                     type, obu_unit, NULL);
    if (err < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to add OBU unit: "
               "type = %d.\n", type);
        return err;
    }

    return err;
}

static int vulkan_encode_av1_write_obu(AVCodecContext *avctx,
                                       uint8_t *data, size_t *data_len,
                                       CodedBitstreamFragment *obu)
{
    VulkanEncodeAV1Context *enc = avctx->priv_data;
    int ret;

    ret = ff_cbs_write_fragment_data(enc->cbs, obu);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to write packed header.\n");
        return ret;
    }

    memcpy(data, obu->data, obu->data_size);
    *data_len = obu->data_size;

    return 0;
}

static int write_sequence_header(AVCodecContext *avctx,
                                 FFHWBaseEncodePicture *base_pic,
                                 uint8_t *data, size_t *data_len)
{
    int err;
    VulkanEncodeAV1Context *enc = avctx->priv_data;
    CodedBitstreamFragment *obu = &enc->current_access_unit;

    err = vulkan_encode_av1_add_obu(avctx, obu,
                                    AV1_OBU_SEQUENCE_HEADER, &enc->seq_hdr_obu);
    if (err < 0)
        goto fail;

    err = vulkan_encode_av1_write_obu(avctx, data, data_len, obu);

fail:
    ff_cbs_fragment_reset(obu);
    return err;
}

static int write_extra_headers(AVCodecContext *avctx,
                               FFHWBaseEncodePicture *base_pic,
                               uint8_t *data, size_t *data_len)
{
    int err;
    VulkanEncodeAV1Context *enc = avctx->priv_data;
    VulkanEncodeAV1Picture  *ap = base_pic->codec_priv;
    CodedBitstreamFragment *obu = &enc->current_access_unit;

    if (ap->units_needed & AV_FRAME_DATA_MASTERING_DISPLAY_METADATA) {
        err = vulkan_encode_av1_add_obu(avctx, obu,
                                        AV1_OBU_METADATA,
                                        &enc->meta_mastering_obu);
        if (err < 0)
            goto fail;
    }

    if (ap->units_needed & UNIT_CONTENT_LIGHT_LEVEL) {
        err = vulkan_encode_av1_add_obu(avctx, obu,
                                        AV1_OBU_METADATA,
                                        &enc->meta_cll_obu);
        if (err < 0)
            goto fail;
    }

    if (ap->units_needed) {
        err = vulkan_encode_av1_write_obu(avctx, data, data_len, obu);
        if (err < 0)
            goto fail;
    } else {
        err = 0;
        *data_len = 0;
    }

fail:
    ff_cbs_fragment_reset(obu);
    return err;
}

static int write_padding(AVCodecContext *avctx, uint32_t padding,
                         uint8_t *data, size_t *data_len)
{
    int err;
    VulkanEncodeAV1Context *enc = avctx->priv_data;
    CodedBitstreamFragment *obu = &enc->current_access_unit;

    AV1RawOBU padding_obu = { 0 };
    AV1RawPadding *raw_padding = &padding_obu.obu.padding;

    if (!padding)
        padding = 16;

    /* 2 byte header + 1 byte trailing bits */
    padding_obu.header.obu_type = AV1_OBU_PADDING;
    *raw_padding = (AV1RawPadding) {
        .payload = enc->padding_payload,
        .payload_size = padding,
    };

    err = vulkan_encode_av1_add_obu(avctx, obu, AV1_OBU_PADDING, &padding_obu);
    if (err < 0)
        goto fail;

    err = vulkan_encode_av1_write_obu(avctx, data, data_len, obu);
fail:
    ff_cbs_fragment_reset(obu);
    return err;
}

static const FFVulkanCodec enc_cb = {
    .flags = FF_HW_FLAG_B_PICTURES |
             FF_HW_FLAG_B_PICTURE_REFERENCES |
             VK_ENC_FLAG_NO_DELAY |
             FF_HW_FLAG_SLICE_CONTROL,
    .picture_priv_data_size = sizeof(VulkanEncodeAV1Picture),
    .filler_header_size = 4,
    .init_profile = init_profile,
    .init_pic_rc = init_pic_rc,
    .init_pic_params = init_pic_params,
    .write_sequence_headers = write_sequence_header,
    .write_extra_headers = write_extra_headers,
    .write_filler = write_padding,
};

static av_cold int vulkan_encode_av1_init(AVCodecContext *avctx)
{
    int err;
    VulkanEncodeAV1Context *enc = avctx->priv_data;
    FFVulkanEncodeContext *ctx = &enc->common;
    FFHWBaseEncodeContext *base_ctx = &ctx->base;
    int flags;

    if (avctx->profile == AV_PROFILE_UNKNOWN)
        avctx->profile = enc->common.opts.profile;

    enc->caps = (VkVideoEncodeAV1CapabilitiesKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_AV1_CAPABILITIES_KHR,
    };

    enc->quality_props = (VkVideoEncodeAV1QualityLevelPropertiesKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_AV1_QUALITY_LEVEL_PROPERTIES_KHR,
    };

    err = ff_vulkan_encode_init(avctx, &enc->common,
                                &ff_vk_enc_av1_desc, &enc_cb,
                                &enc->caps, &enc->quality_props);
    if (err < 0)
        return err;

    av_log(avctx, AV_LOG_VERBOSE, "AV1 encoder capabilities:\n");
    av_log(avctx, AV_LOG_VERBOSE, "    Standard capability flags:\n");
    av_log(avctx, AV_LOG_VERBOSE, "        per_rate_control_group_min_max_q_index: %i\n",
           !!(enc->caps.flags & VK_VIDEO_ENCODE_AV1_CAPABILITY_PER_RATE_CONTROL_GROUP_MIN_MAX_Q_INDEX_BIT_KHR));
    av_log(avctx, AV_LOG_VERBOSE, "        generate_obu_extension_header: %i\n",
           !!(enc->caps.flags & VK_VIDEO_ENCODE_AV1_CAPABILITY_GENERATE_OBU_EXTENSION_HEADER_BIT_KHR));
    av_log(avctx, AV_LOG_VERBOSE, "        primary_reference_cdf_only: %i\n",
           !!(enc->caps.flags & VK_VIDEO_ENCODE_AV1_CAPABILITY_PRIMARY_REFERENCE_CDF_ONLY_BIT_KHR));
    av_log(avctx, AV_LOG_VERBOSE, "        frame_size_override: %i\n",
           !!(enc->caps.flags & VK_VIDEO_ENCODE_AV1_CAPABILITY_FRAME_SIZE_OVERRIDE_BIT_KHR));
    av_log(avctx, AV_LOG_VERBOSE, "        motion_vector_scaling: %i\n",
           !!(enc->caps.flags & VK_VIDEO_ENCODE_AV1_CAPABILITY_MOTION_VECTOR_SCALING_BIT_KHR));
    av_log(avctx, AV_LOG_VERBOSE, "    Capabilities:\n");
    av_log(avctx, AV_LOG_VERBOSE, "        64x64 superblocks: %i\n",
           !!(enc->caps.superblockSizes & VK_VIDEO_ENCODE_AV1_SUPERBLOCK_SIZE_64_BIT_KHR));
    av_log(avctx, AV_LOG_VERBOSE, "        128x128 superblocks: %i\n",
           !!(enc->caps.superblockSizes & VK_VIDEO_ENCODE_AV1_SUPERBLOCK_SIZE_128_BIT_KHR));
    av_log(avctx, AV_LOG_VERBOSE, "        maxSingleReferenceCount: %i\n",
           enc->caps.maxSingleReferenceCount);
    av_log(avctx, AV_LOG_VERBOSE, "        singleReferenceNameMask: 0x%x\n",
           enc->caps.singleReferenceNameMask);
    av_log(avctx, AV_LOG_VERBOSE, "        maxUnidirectionalCompoundReferenceCount: %i\n",
           enc->caps.maxUnidirectionalCompoundReferenceCount);
    av_log(avctx, AV_LOG_VERBOSE, "        maxUnidirectionalCompoundGroup1ReferenceCount: %i\n",
           enc->caps.maxUnidirectionalCompoundGroup1ReferenceCount);
    av_log(avctx, AV_LOG_VERBOSE, "        unidirectionalCompoundReferenceNameMask: 0x%x\n",
           enc->caps.unidirectionalCompoundReferenceNameMask);
    av_log(avctx, AV_LOG_VERBOSE, "        maxBidirectionalCompoundReferenceCount: %i\n",
           enc->caps.maxBidirectionalCompoundReferenceCount);
    av_log(avctx, AV_LOG_VERBOSE, "        maxBidirectionalCompoundGroup1ReferenceCount: %i\n",
           enc->caps.maxBidirectionalCompoundGroup1ReferenceCount);
    av_log(avctx, AV_LOG_VERBOSE, "        maxBidirectionalCompoundGroup2ReferenceCount: %i\n",
           enc->caps.maxBidirectionalCompoundGroup2ReferenceCount);
    av_log(avctx, AV_LOG_VERBOSE, "        bidirectionalCompoundReferenceNameMask: 0x%x\n",
           enc->caps.bidirectionalCompoundReferenceNameMask);
    av_log(avctx, AV_LOG_VERBOSE, "        maxTemporalLayerCount: %i\n",
           enc->caps.maxTemporalLayerCount);
    av_log(avctx, AV_LOG_VERBOSE, "        maxSpatialLayerCount: %i\n",
           enc->caps.maxSpatialLayerCount);
    av_log(avctx, AV_LOG_VERBOSE, "        maxOperatingPoints: %i\n",
           enc->caps.maxOperatingPoints);
    av_log(avctx, AV_LOG_VERBOSE, "        min/max Qindex: [%i, %i]\n",
           enc->caps.minQIndex, enc->caps.maxQIndex);
    av_log(avctx, AV_LOG_VERBOSE, "        prefersGopRemainingFrames: %i\n",
           enc->caps.prefersGopRemainingFrames);
    av_log(avctx, AV_LOG_VERBOSE, "        requiresGopRemainingFrames: %i\n",
           enc->caps.requiresGopRemainingFrames);
    av_log(avctx, AV_LOG_VERBOSE, "        maxLevel: %i\n",
           enc->caps.maxLevel);
    av_log(avctx, AV_LOG_VERBOSE, "        codedPictureAlignment: %ix%i\n",
           enc->caps.codedPictureAlignment.width, enc->caps.codedPictureAlignment.height);
    av_log(avctx, AV_LOG_VERBOSE, "        maxTiles: %ix%i\n",
           enc->caps.maxTiles.width, enc->caps.maxTiles.height);
    av_log(avctx, AV_LOG_VERBOSE, "        Tile size: %ix%i to %ix%i\n",
           enc->caps.minTileSize.width, enc->caps.minTileSize.height,
           enc->caps.maxTileSize.width, enc->caps.maxTileSize.height);

    err = init_enc_options(avctx);
    if (err < 0)
        return err;

    flags = ctx->codec->flags;
    err = ff_hw_base_init_gop_structure(base_ctx, avctx,
                                        ctx->caps.maxDpbSlots,
                                        enc->caps.maxBidirectionalCompoundReferenceCount,
                                        flags, 0);
    if (err < 0)
        return err;

    base_ctx->output_delay = base_ctx->b_per_p;
    base_ctx->decode_delay = base_ctx->max_b_depth;

    /* Create units and session parameters */
    err = init_base_units(avctx);
    if (err < 0)
        return err;

    /* Init CBS */
    err = ff_cbs_init(&enc->cbs, AV_CODEC_ID_AV1, avctx);
    if (err < 0)
        return err;

    if (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) {
        uint8_t data[4096];
        size_t data_len = sizeof(data);

        err = write_sequence_header(avctx, NULL, data, &data_len);
        if (err < 0) {
            av_log(avctx, AV_LOG_ERROR, "Failed to write sequence header "
                   "for extradata: %d.\n", err);
            return err;
        } else {
            avctx->extradata_size = data_len;
            avctx->extradata = av_mallocz(avctx->extradata_size +
                                          AV_INPUT_BUFFER_PADDING_SIZE);
            if (!avctx->extradata) {
                err = AVERROR(ENOMEM);
                return err;
            }
            memcpy(avctx->extradata, data, avctx->extradata_size);
        }
    }

    enc->padding_payload = av_mallocz(2*ctx->caps.minBitstreamBufferOffsetAlignment);
    if (!enc->padding_payload)
        return AVERROR(ENOMEM);

    memset(enc->padding_payload, 0xaa, 2*ctx->caps.minBitstreamBufferOffsetAlignment);

    return 0;
}

static av_cold int vulkan_encode_av1_close(AVCodecContext *avctx)
{
    VulkanEncodeAV1Context *enc = avctx->priv_data;
    av_free(enc->padding_payload);
    ff_vulkan_encode_uninit(&enc->common);
    return 0;
}

#define OFFSET(x) offsetof(VulkanEncodeAV1Context, x)
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM)
static const AVOption vulkan_encode_av1_options[] = {
    HW_BASE_ENCODE_COMMON_OPTIONS,
    VULKAN_ENCODE_COMMON_OPTIONS,

    { "profile", "Set profile",
      OFFSET(common.opts.profile), AV_OPT_TYPE_INT,
      { .i64 = AV_PROFILE_UNKNOWN }, AV_PROFILE_UNKNOWN, 0xffff, FLAGS, .unit = "profile" },

#define PROFILE(name, value)  name, NULL, 0, AV_OPT_TYPE_CONST, \
      { .i64 = value }, 0, 0, FLAGS, .unit = "profile"
    { PROFILE("main",                 AV_PROFILE_AV1_MAIN) },
    { PROFILE("high",                 AV_PROFILE_AV1_HIGH) },
    { PROFILE("professional",         AV_PROFILE_AV1_PROFESSIONAL) },
#undef PROFILE

    { "tier", "Set tier (seq_tier)",
      OFFSET(common.opts.tier), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, FLAGS, .unit = "tier" },
        { "main", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 0 }, 0, 0, FLAGS, .unit = "tier" },
        { "high", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 1 }, 0, 0, FLAGS, .unit = "tier" },

    { "level", "Set level (level_idc)",
      OFFSET(common.opts.level), AV_OPT_TYPE_INT,
      { .i64 = AV_LEVEL_UNKNOWN }, AV_LEVEL_UNKNOWN, 0xff, FLAGS, .unit = "level" },

#define LEVEL(name, value) name, NULL, 0, AV_OPT_TYPE_CONST, \
      { .i64 = value }, 0, 0, FLAGS, .unit = "level"
    { LEVEL("20", 0) },
    { LEVEL("21", 1) },
    { LEVEL("22", 2) },
    { LEVEL("23", 3) },
    { LEVEL("30", 4) },
    { LEVEL("31", 5) },
    { LEVEL("32", 6) },
    { LEVEL("33", 7) },
    { LEVEL("40", 8) },
    { LEVEL("41", 9) },
    { LEVEL("42", 10) },
    { LEVEL("43", 11) },
    { LEVEL("50", 12) },
    { LEVEL("51", 13) },
    { LEVEL("52", 14) },
    { LEVEL("53", 15) },
    { LEVEL("60", 16) },
    { LEVEL("61", 17) },
    { LEVEL("62", 18) },
    { LEVEL("63", 19) },
    { LEVEL("70", 20) },
    { LEVEL("71", 21) },
    { LEVEL("72", 22) },
    { LEVEL("73", 23) },
#undef LEVEL

    { "units", "Set units to include", OFFSET(unit_elems), AV_OPT_TYPE_FLAGS, { .i64 = UNIT_MASTERING_DISPLAY | UNIT_CONTENT_LIGHT_LEVEL }, 0, INT_MAX, FLAGS, "units" },
        { "hdr",        "Include HDR metadata for mastering display colour volume and content light level information", 0, AV_OPT_TYPE_CONST, { .i64 = UNIT_MASTERING_DISPLAY | UNIT_CONTENT_LIGHT_LEVEL }, INT_MIN, INT_MAX, FLAGS, "units" },

    { NULL },
};

static const FFCodecDefault vulkan_encode_av1_defaults[] = {
    { "b",              "0"   },
    { "bf",             "2"   },
    { "g",              "300" },
    { "qmin",           "1"   },
    { "qmax",           "255" },
    { NULL },
};

static const AVClass vulkan_encode_av1_class = {
    .class_name = "av1_vulkan",
    .item_name  = av_default_item_name,
    .option     = vulkan_encode_av1_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_av1_vulkan_encoder = {
    .p.name         = "av1_vulkan",
    CODEC_LONG_NAME("AV1 (Vulkan)"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_AV1,
    .priv_data_size = sizeof(VulkanEncodeAV1Context),
    .init           = &vulkan_encode_av1_init,
    FF_CODEC_RECEIVE_PACKET_CB(&ff_vulkan_encode_receive_packet),
    .close          = &vulkan_encode_av1_close,
    .p.priv_class   = &vulkan_encode_av1_class,
    .p.capabilities = AV_CODEC_CAP_DELAY |
                      AV_CODEC_CAP_HARDWARE |
                      AV_CODEC_CAP_DR1 |
                      AV_CODEC_CAP_ENCODER_FLUSH |
                      AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
    .defaults       = vulkan_encode_av1_defaults,
    CODEC_PIXFMTS(AV_PIX_FMT_VULKAN),
    .hw_configs     = ff_vulkan_encode_hw_configs,
    .p.wrapper_name = "vulkan",
};
