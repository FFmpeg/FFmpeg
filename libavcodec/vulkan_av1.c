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

#include "av1dec.h"

#include "vulkan_decode.h"

/* Maximum number of tiles specified by any defined level */
#define MAX_TILES 256

const FFVulkanDecodeDescriptor ff_vk_dec_av1_desc = {
    .codec_id         = AV_CODEC_ID_AV1,
    .decode_extension = FF_VK_EXT_VIDEO_DECODE_AV1,
    .decode_op        = VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR,
    .ext_props = {
        .extensionName = VK_STD_VULKAN_VIDEO_CODEC_AV1_DECODE_EXTENSION_NAME,
        .specVersion   = VK_STD_VULKAN_VIDEO_CODEC_AV1_DECODE_SPEC_VERSION,
    },
};

typedef struct AV1VulkanDecodePicture {
    FFVulkanDecodePicture           vp;

    /* TODO: investigate if this can be removed to make decoding completely
     * independent. */
    FFVulkanDecodeContext          *dec;

    uint32_t tile_sizes[MAX_TILES];

    /* Current picture */
    StdVideoDecodeAV1ReferenceInfo     std_ref;
    VkVideoDecodeAV1DpbSlotInfoKHR     vkav1_ref;
    uint16_t width_in_sbs_minus1[64];
    uint16_t height_in_sbs_minus1[64];
    uint16_t mi_col_starts[64];
    uint16_t mi_row_starts[64];
    StdVideoAV1TileInfo tile_info;
    StdVideoAV1Quantization quantization;
    StdVideoAV1Segmentation segmentation;
    StdVideoAV1LoopFilter loop_filter;
    StdVideoAV1CDEF cdef;
    StdVideoAV1LoopRestoration loop_restoration;
    StdVideoAV1GlobalMotion global_motion;
    StdVideoAV1FilmGrain film_grain;
    StdVideoDecodeAV1PictureInfo    std_pic_info;
    VkVideoDecodeAV1PictureInfoKHR     av1_pic_info;

    /* Picture refs */
    const AV1Frame                     *ref_src   [AV1_NUM_REF_FRAMES];
    StdVideoDecodeAV1ReferenceInfo      std_refs  [AV1_NUM_REF_FRAMES];
    VkVideoDecodeAV1DpbSlotInfoKHR      vkav1_refs[AV1_NUM_REF_FRAMES];

    uint8_t frame_id_set;
    uint8_t frame_id;
    uint8_t ref_frame_sign_bias_mask;
} AV1VulkanDecodePicture;

static int vk_av1_fill_pict(AVCodecContext *avctx, const AV1Frame **ref_src,
                            VkVideoReferenceSlotInfoKHR *ref_slot,      /* Main structure */
                            VkVideoPictureResourceInfoKHR *ref,         /* Goes in ^ */
                            StdVideoDecodeAV1ReferenceInfo *vkav1_std_ref,
                            VkVideoDecodeAV1DpbSlotInfoKHR *vkav1_ref, /* Goes in ^ */
                            const AV1Frame *pic, int is_current, int has_grain,
                            int *saved_order_hints)
{
    FFVulkanDecodeContext *dec = avctx->internal->hwaccel_priv_data;
    AV1VulkanDecodePicture *hp = pic->hwaccel_picture_private;
    FFVulkanDecodePicture *vkpic = &hp->vp;

    int err = ff_vk_decode_prepare_frame(dec, pic->f, vkpic, is_current,
                                         has_grain || dec->dedicated_dpb);
    if (err < 0)
        return err;

    *vkav1_std_ref = (StdVideoDecodeAV1ReferenceInfo) {
        .flags = (StdVideoDecodeAV1ReferenceInfoFlags) {
            .disable_frame_end_update_cdf = pic->raw_frame_header->disable_frame_end_update_cdf,
            .segmentation_enabled = pic->raw_frame_header->segmentation_enabled,
        },
        .frame_type = pic->raw_frame_header->frame_type,
        .OrderHint = pic->raw_frame_header->order_hint,
        .RefFrameSignBias = hp->ref_frame_sign_bias_mask,
    };

    if (saved_order_hints)
        for (int i = 0; i < AV1_TOTAL_REFS_PER_FRAME; i++)
            vkav1_std_ref->SavedOrderHints[i] = saved_order_hints[i];

    *vkav1_ref = (VkVideoDecodeAV1DpbSlotInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_AV1_DPB_SLOT_INFO_KHR,
        .pStdReferenceInfo = vkav1_std_ref,
    };

    vkav1_std_ref->flags.disable_frame_end_update_cdf = pic->raw_frame_header->disable_frame_end_update_cdf;
    vkav1_std_ref->flags.segmentation_enabled = pic->raw_frame_header->segmentation_enabled;
    vkav1_std_ref->frame_type = pic->raw_frame_header->frame_type;

    *ref = (VkVideoPictureResourceInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR,
        .codedOffset = (VkOffset2D){ 0, 0 },
        .codedExtent = (VkExtent2D){ pic->f->width, pic->f->height },
        .baseArrayLayer = ((has_grain || dec->dedicated_dpb) && dec->layered_dpb) ?
                          hp->frame_id : 0,
        .imageViewBinding = vkpic->img_view_ref,
    };

    *ref_slot = (VkVideoReferenceSlotInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR,
        .pNext = vkav1_ref,
        .slotIndex = hp->frame_id,
        .pPictureResource = ref,
    };

    if (ref_src)
        *ref_src = pic;

    return 0;
}

static int vk_av1_create_params(AVCodecContext *avctx, AVBufferRef **buf)
{
    const AV1DecContext *s = avctx->priv_data;
    FFVulkanDecodeContext *dec = avctx->internal->hwaccel_priv_data;
    FFVulkanDecodeShared *ctx = dec->shared_ctx;

    const AV1RawSequenceHeader *seq = s->raw_seq;

    StdVideoAV1SequenceHeader av1_sequence_header;
    StdVideoAV1TimingInfo av1_timing_info;
    StdVideoAV1ColorConfig av1_color_config;
    VkVideoDecodeAV1SessionParametersCreateInfoKHR av1_params;
    VkVideoSessionParametersCreateInfoKHR session_params_create;

    int err;

    av1_timing_info = (StdVideoAV1TimingInfo) {
        .flags = (StdVideoAV1TimingInfoFlags) {
            .equal_picture_interval = seq->timing_info.equal_picture_interval,
        },
        .num_units_in_display_tick = seq->timing_info.num_units_in_display_tick,
        .time_scale = seq->timing_info.time_scale,
        .num_ticks_per_picture_minus_1 = seq->timing_info.num_ticks_per_picture_minus_1,
    };

    av1_color_config = (StdVideoAV1ColorConfig) {
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

    av1_sequence_header = (StdVideoAV1SequenceHeader) {
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
        .pTimingInfo = &av1_timing_info,
        .pColorConfig = &av1_color_config,
    };

    av1_params = (VkVideoDecodeAV1SessionParametersCreateInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_AV1_SESSION_PARAMETERS_CREATE_INFO_KHR,
        .pStdSequenceHeader = &av1_sequence_header,
    };
    session_params_create = (VkVideoSessionParametersCreateInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_PARAMETERS_CREATE_INFO_KHR,
        .pNext = &av1_params,
        .videoSession = ctx->common.session,
        .videoSessionParametersTemplate = VK_NULL_HANDLE,
    };

    err = ff_vk_decode_create_params(buf, avctx, ctx, &session_params_create);
    if (err < 0)
        return err;

    av_log(avctx, AV_LOG_DEBUG, "Created frame parameters\n");

    return 0;
}

static int vk_av1_start_frame(AVCodecContext          *avctx,
                              av_unused const uint8_t *buffer,
                              av_unused uint32_t       size)
{
    int err;
    int ref_count = 0;
    AV1DecContext *s = avctx->priv_data;
    const AV1Frame *pic = &s->cur_frame;
    FFVulkanDecodeContext *dec = avctx->internal->hwaccel_priv_data;
    AV1VulkanDecodePicture *ap = pic->hwaccel_picture_private;
    FFVulkanDecodePicture *vp = &ap->vp;

    const AV1RawFrameHeader *frame_header = s->raw_frame_header;
    const AV1RawFilmGrainParams *film_grain = &s->cur_frame.film_grain;
    CodedBitstreamAV1Context *cbs_ctx = (CodedBitstreamAV1Context *)(s->cbc->priv_data);

    const int apply_grain = !(avctx->export_side_data & AV_CODEC_EXPORT_DATA_FILM_GRAIN) &&
                            film_grain->apply_grain;
    StdVideoAV1FrameRestorationType remap_lr_type[4] = { STD_VIDEO_AV1_FRAME_RESTORATION_TYPE_NONE,
                                                         STD_VIDEO_AV1_FRAME_RESTORATION_TYPE_SWITCHABLE,
                                                         STD_VIDEO_AV1_FRAME_RESTORATION_TYPE_WIENER,
                                                         STD_VIDEO_AV1_FRAME_RESTORATION_TYPE_SGRPROJ };

    if (!dec->session_params) {
        err = vk_av1_create_params(avctx, &dec->session_params);
        if (err < 0)
            return err;
    }

    if (!ap->frame_id_set) {
        unsigned slot_idx = 0;
        for (unsigned i = 0; i < 32; i++) {
            if (!(dec->frame_id_alloc_mask & (1 << i))) {
                slot_idx = i;
                break;
            }
        }
        ap->frame_id = slot_idx;
        ap->frame_id_set = 1;
        dec->frame_id_alloc_mask |= (1 << slot_idx);
    }

    ap->ref_frame_sign_bias_mask = 0x0;
    for (int i = 0; i < STD_VIDEO_AV1_TOTAL_REFS_PER_FRAME; i++)
        ap->ref_frame_sign_bias_mask |= cbs_ctx->ref_frame_sign_bias[i] << i;

    for (int i = 0; i < STD_VIDEO_AV1_REFS_PER_FRAME; i++) {
        const int idx = pic->raw_frame_header->ref_frame_idx[i];
        const AV1Frame *ref_frame = &s->ref[idx];
        AV1VulkanDecodePicture *hp = ref_frame->hwaccel_picture_private;
        int found = 0;

        if (ref_frame->f->pict_type == AV_PICTURE_TYPE_NONE)
            continue;

        for (int j = 0; j < ref_count; j++) {
            if (vp->ref_slots[j].slotIndex == hp->frame_id) {
                found = 1;
                break;
            }
        }
        if (found)
            continue;

        err = vk_av1_fill_pict(avctx, &ap->ref_src[ref_count], &vp->ref_slots[ref_count],
                               &vp->refs[ref_count], &ap->std_refs[ref_count], &ap->vkav1_refs[ref_count],
                               ref_frame, 0, 0, cbs_ctx->ref[idx].saved_order_hints);
        if (err < 0)
            return err;

        ref_count++;
    }

    err = vk_av1_fill_pict(avctx, NULL, &vp->ref_slot, &vp->ref,
                           &ap->std_ref,
                           &ap->vkav1_ref,
                           pic, 1, apply_grain, NULL);
    if (err < 0)
        return err;

    ap->av1_pic_info = (VkVideoDecodeAV1PictureInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_AV1_PICTURE_INFO_KHR,
        .pStdPictureInfo = &ap->std_pic_info,
        .frameHeaderOffset = 0,
        .tileCount = 0,
        .pTileOffsets = NULL,
        .pTileSizes = ap->tile_sizes,
    };

    for (int i = 0; i < STD_VIDEO_AV1_REFS_PER_FRAME; i++) {
        const int idx = pic->raw_frame_header->ref_frame_idx[i];
        const AV1Frame *ref_frame = &s->ref[idx];
        AV1VulkanDecodePicture *hp = ref_frame->hwaccel_picture_private;

        if (ref_frame->f->pict_type == AV_PICTURE_TYPE_NONE)
            ap->av1_pic_info.referenceNameSlotIndices[i] = AV1_REF_FRAME_NONE;
        else
            ap->av1_pic_info.referenceNameSlotIndices[i] = hp->frame_id;
    }

    vp->decode_info = (VkVideoDecodeInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_INFO_KHR,
        .pNext = &ap->av1_pic_info,
        .flags = 0x0,
        .pSetupReferenceSlot = &vp->ref_slot,
        .referenceSlotCount = ref_count,
        .pReferenceSlots = vp->ref_slots,
        .dstPictureResource = (VkVideoPictureResourceInfoKHR) {
            .sType = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR,
            .codedOffset = (VkOffset2D){ 0, 0 },
            .codedExtent = (VkExtent2D){ pic->f->width, pic->f->height },
            .baseArrayLayer = 0,
            .imageViewBinding = vp->img_view_out,
        },
    };

    ap->tile_info = (StdVideoAV1TileInfo) {
        .flags = (StdVideoAV1TileInfoFlags) {
            .uniform_tile_spacing_flag = frame_header->uniform_tile_spacing_flag,
        },
        .TileCols = frame_header->tile_cols,
        .TileRows = frame_header->tile_rows,
        .context_update_tile_id = frame_header->context_update_tile_id,
        .tile_size_bytes_minus_1 = frame_header->tile_size_bytes_minus1,
        .pWidthInSbsMinus1 = ap->width_in_sbs_minus1,
        .pHeightInSbsMinus1 = ap->height_in_sbs_minus1,
        .pMiColStarts = ap->mi_col_starts,
        .pMiRowStarts = ap->mi_row_starts,
    };

    ap->quantization = (StdVideoAV1Quantization) {
        .flags.using_qmatrix = frame_header->using_qmatrix,
        .flags.diff_uv_delta = frame_header->diff_uv_delta,
        .base_q_idx = frame_header->base_q_idx,
        .DeltaQYDc = frame_header->delta_q_y_dc,
        .DeltaQUDc = frame_header->delta_q_u_dc,
        .DeltaQUAc = frame_header->delta_q_u_ac,
        .DeltaQVDc = frame_header->delta_q_v_dc,
        .DeltaQVAc = frame_header->delta_q_v_ac,
        .qm_y = frame_header->qm_y,
        .qm_u = frame_header->qm_u,
        .qm_v = frame_header->qm_v,
    };

    ap->loop_filter = (StdVideoAV1LoopFilter) {
        .flags = (StdVideoAV1LoopFilterFlags) {
            .loop_filter_delta_enabled = frame_header->loop_filter_delta_enabled,
            .loop_filter_delta_update = frame_header->loop_filter_delta_update,
        },
        .loop_filter_sharpness = frame_header->loop_filter_sharpness,
    };

    for (int i = 0; i < STD_VIDEO_AV1_MAX_LOOP_FILTER_STRENGTHS; i++)
        ap->loop_filter.loop_filter_level[i] = frame_header->loop_filter_level[i];
    for (int i = 0; i < STD_VIDEO_AV1_LOOP_FILTER_ADJUSTMENTS; i++)
        ap->loop_filter.loop_filter_mode_deltas[i] = frame_header->loop_filter_mode_deltas[i];

    ap->cdef = (StdVideoAV1CDEF) {
        .cdef_damping_minus_3 = frame_header->cdef_damping_minus_3,
        .cdef_bits = frame_header->cdef_bits,
    };

    ap->loop_restoration = (StdVideoAV1LoopRestoration) {
        .FrameRestorationType[0] = remap_lr_type[frame_header->lr_type[0]],
        .FrameRestorationType[1] = remap_lr_type[frame_header->lr_type[1]],
        .FrameRestorationType[2] = remap_lr_type[frame_header->lr_type[2]],
        .LoopRestorationSize[0] = 1 + frame_header->lr_unit_shift,
        .LoopRestorationSize[1] = 1 + frame_header->lr_unit_shift - frame_header->lr_uv_shift,
        .LoopRestorationSize[2] = 1 + frame_header->lr_unit_shift - frame_header->lr_uv_shift,
    };

    ap->film_grain = (StdVideoAV1FilmGrain) {
        .flags = (StdVideoAV1FilmGrainFlags) {
            .chroma_scaling_from_luma = film_grain->chroma_scaling_from_luma,
            .overlap_flag = film_grain->overlap_flag,
            .clip_to_restricted_range = film_grain->clip_to_restricted_range,
        },
        .grain_scaling_minus_8 = film_grain->grain_scaling_minus_8,
        .ar_coeff_lag = film_grain->ar_coeff_lag,
        .ar_coeff_shift_minus_6 = film_grain->ar_coeff_shift_minus_6,
        .grain_scale_shift = film_grain->grain_scale_shift,
        .grain_seed = film_grain->grain_seed,
        .film_grain_params_ref_idx = film_grain->film_grain_params_ref_idx,
        .num_y_points = film_grain->num_y_points,
        .num_cb_points = film_grain->num_cb_points,
        .num_cr_points = film_grain->num_cr_points,
        .cb_mult = film_grain->cb_mult,
        .cb_luma_mult = film_grain->cb_luma_mult,
        .cb_offset = film_grain->cb_offset,
        .cr_mult = film_grain->cr_mult,
        .cr_luma_mult = film_grain->cr_luma_mult,
        .cr_offset = film_grain->cr_offset,
    };

    /* Setup frame header */
    ap->std_pic_info = (StdVideoDecodeAV1PictureInfo) {
        .flags = (StdVideoDecodeAV1PictureInfoFlags) {
            .error_resilient_mode = frame_header->error_resilient_mode,
            .disable_cdf_update = frame_header->disable_cdf_update,
            .use_superres = frame_header->use_superres,
            .render_and_frame_size_different = frame_header->render_and_frame_size_different,
            .allow_screen_content_tools = frame_header->allow_screen_content_tools,
            .is_filter_switchable = frame_header->is_filter_switchable,
            .force_integer_mv = frame_header->force_integer_mv,
            .frame_size_override_flag = frame_header->frame_size_override_flag,
            .buffer_removal_time_present_flag = frame_header->buffer_removal_time_present_flag,
            .allow_intrabc = frame_header->allow_intrabc,
            .frame_refs_short_signaling = frame_header->frame_refs_short_signaling,
            .allow_high_precision_mv = frame_header->allow_high_precision_mv,
            .is_motion_mode_switchable = frame_header->is_motion_mode_switchable,
            .use_ref_frame_mvs = frame_header->use_ref_frame_mvs,
            .disable_frame_end_update_cdf = frame_header->disable_frame_end_update_cdf,
            .allow_warped_motion = frame_header->allow_warped_motion,
            .reduced_tx_set = frame_header->reduced_tx_set,
            .reference_select = frame_header->reference_select,
            .skip_mode_present = frame_header->skip_mode_present,
            .delta_q_present = frame_header->delta_q_present,
            .delta_lf_present = frame_header->delta_lf_present,
            .delta_lf_multi = frame_header->delta_lf_multi,
            .segmentation_enabled = frame_header->segmentation_enabled,
            .segmentation_update_map = frame_header->segmentation_update_map,
            .segmentation_temporal_update = frame_header->segmentation_temporal_update,
            .segmentation_update_data = frame_header->segmentation_update_data,
            .UsesLr = frame_header->lr_type[0] || frame_header->lr_type[1] || frame_header->lr_type[2],
            .apply_grain = apply_grain,
        },
        .frame_type = frame_header->frame_type,
        .current_frame_id = frame_header->current_frame_id,
        .OrderHint = frame_header->order_hint,
        .primary_ref_frame = frame_header->primary_ref_frame,
        .refresh_frame_flags = frame_header->refresh_frame_flags,
        .interpolation_filter = frame_header->interpolation_filter,
        .TxMode = frame_header->tx_mode,
        .delta_q_res = frame_header->delta_q_res,
        .delta_lf_res = frame_header->delta_lf_res,
        .SkipModeFrame[0] = s->cur_frame.skip_mode_frame_idx[0],
        .SkipModeFrame[1] = s->cur_frame.skip_mode_frame_idx[1],
        .coded_denom = frame_header->coded_denom,
        .pTileInfo = &ap->tile_info,
        .pQuantization = &ap->quantization,
        .pSegmentation = &ap->segmentation,
        .pLoopFilter = &ap->loop_filter,
        .pCDEF = &ap->cdef,
        .pLoopRestoration = &ap->loop_restoration,
        .pGlobalMotion = &ap->global_motion,
        .pFilmGrain = apply_grain ? &ap->film_grain : NULL,
    };

    for (int i = 0; i < 64; i++) {
        ap->width_in_sbs_minus1[i] = frame_header->width_in_sbs_minus_1[i];
        ap->height_in_sbs_minus1[i] = frame_header->height_in_sbs_minus_1[i];
        ap->mi_col_starts[i] = frame_header->tile_start_col_sb[i];
        ap->mi_row_starts[i] = frame_header->tile_start_row_sb[i];
    }

    for (int i = 0; i < STD_VIDEO_AV1_MAX_SEGMENTS; i++) {
        ap->segmentation.FeatureEnabled[i] = 0x0;
        for (int j = 0; j < STD_VIDEO_AV1_SEG_LVL_MAX; j++) {
            ap->segmentation.FeatureEnabled[i] |= (frame_header->feature_enabled[i][j] << j);
            ap->segmentation.FeatureData[i][j] = frame_header->feature_value[i][j];
        }
    }

    for (int i = 0; i < STD_VIDEO_AV1_TOTAL_REFS_PER_FRAME; i++)
        ap->loop_filter.loop_filter_ref_deltas[i] = frame_header->loop_filter_ref_deltas[i];

    for (int i = 0; i < STD_VIDEO_AV1_MAX_CDEF_FILTER_STRENGTHS; i++) {
        ap->cdef.cdef_y_pri_strength[i] = frame_header->cdef_y_pri_strength[i];
        ap->cdef.cdef_y_sec_strength[i] = frame_header->cdef_y_sec_strength[i];
        ap->cdef.cdef_uv_pri_strength[i] = frame_header->cdef_uv_pri_strength[i];
        ap->cdef.cdef_uv_sec_strength[i] = frame_header->cdef_uv_sec_strength[i];
    }

    for (int i = 0; i < STD_VIDEO_AV1_NUM_REF_FRAMES; i++) {
        ap->std_pic_info.OrderHints[i] = frame_header->ref_order_hint[i];
        ap->global_motion.GmType[i] = s->cur_frame.gm_type[i];
        for (int j = 0; j < STD_VIDEO_AV1_GLOBAL_MOTION_PARAMS; j++) {
            ap->global_motion.gm_params[i][j] = s->cur_frame.gm_params[i][j];
        }
    }

    if (apply_grain) {
        for (int i = 0; i < STD_VIDEO_AV1_MAX_NUM_Y_POINTS; i++) {
            ap->film_grain.point_y_value[i] = film_grain->point_y_value[i];
            ap->film_grain.point_y_scaling[i] = film_grain->point_y_scaling[i];
        }

        for (int i = 0; i < STD_VIDEO_AV1_MAX_NUM_CB_POINTS; i++) {
            ap->film_grain.point_cb_value[i] = film_grain->point_cb_value[i];
            ap->film_grain.point_cb_scaling[i] = film_grain->point_cb_scaling[i];
            ap->film_grain.point_cr_value[i] = film_grain->point_cr_value[i];
            ap->film_grain.point_cr_scaling[i] = film_grain->point_cr_scaling[i];
        }

        for (int i = 0; i < STD_VIDEO_AV1_MAX_NUM_POS_LUMA; i++)
            ap->film_grain.ar_coeffs_y_plus_128[i] = film_grain->ar_coeffs_y_plus_128[i];

        for (int i = 0; i < STD_VIDEO_AV1_MAX_NUM_POS_CHROMA; i++) {
            ap->film_grain.ar_coeffs_cb_plus_128[i] = film_grain->ar_coeffs_cb_plus_128[i];
            ap->film_grain.ar_coeffs_cr_plus_128[i] = film_grain->ar_coeffs_cr_plus_128[i];
        }
    }

    ap->dec = dec;

    return 0;
}

static int vk_av1_decode_slice(AVCodecContext *avctx,
                               const uint8_t  *data,
                               uint32_t        size)
{
    int err;
    const AV1DecContext *s = avctx->priv_data;
    AV1VulkanDecodePicture *ap = s->cur_frame.hwaccel_picture_private;
    FFVulkanDecodePicture *vp = &ap->vp;

    /* Too many tiles, exceeding all defined levels in the AV1 spec */
    if (ap->av1_pic_info.tileCount > MAX_TILES)
        return AVERROR(ENOSYS);

    for (int i = s->tg_start; i <= s->tg_end; i++) {
        ap->tile_sizes[ap->av1_pic_info.tileCount] = s->tile_group_info[i].tile_size;

        err = ff_vk_decode_add_slice(avctx, vp,
                                     data + s->tile_group_info[i].tile_offset,
                                     s->tile_group_info[i].tile_size, 0,
                                     &ap->av1_pic_info.tileCount,
                                     &ap->av1_pic_info.pTileOffsets);
        if (err < 0)
            return err;
    }

    return 0;
}

static int vk_av1_end_frame(AVCodecContext *avctx)
{
    const AV1DecContext *s = avctx->priv_data;
    FFVulkanDecodeContext *dec = avctx->internal->hwaccel_priv_data;
    const AV1Frame *pic = &s->cur_frame;
    AV1VulkanDecodePicture *ap = pic->hwaccel_picture_private;
    FFVulkanDecodePicture *vp = &ap->vp;
    FFVulkanDecodePicture *rvp[AV1_NUM_REF_FRAMES] = { 0 };
    AVFrame *rav[AV1_NUM_REF_FRAMES] = { 0 };

    if (!ap->av1_pic_info.tileCount)
        return 0;

    if (!dec->session_params) {
        int err = vk_av1_create_params(avctx, &dec->session_params);
        if (err < 0)
            return err;
    }

    for (int i = 0; i < vp->decode_info.referenceSlotCount; i++) {
        const AV1Frame *rp = ap->ref_src[i];
        AV1VulkanDecodePicture *rhp = rp->hwaccel_picture_private;

        rvp[i] = &rhp->vp;
        rav[i] = ap->ref_src[i]->f;
    }

    av_log(avctx, AV_LOG_VERBOSE, "Decoding frame, %"SIZE_SPECIFIER" bytes, %i tiles\n",
           vp->slices_size, ap->av1_pic_info.tileCount);

    return ff_vk_decode_frame(avctx, pic->f, vp, rav, rvp);
}

static void vk_av1_free_frame_priv(FFRefStructOpaque _hwctx, void *data)
{
    AVHWDeviceContext *hwctx = _hwctx.nc;
    AV1VulkanDecodePicture *ap = data;

    /* Workaround for a spec issue. */
    if (ap->frame_id_set)
        ap->dec->frame_id_alloc_mask &= ~(1 << ap->frame_id);

    /* Free frame resources, this also destroys the session parameters. */
    ff_vk_decode_free_frame(hwctx, &ap->vp);
}

const FFHWAccel ff_av1_vulkan_hwaccel = {
    .p.name                = "av1_vulkan",
    .p.type                = AVMEDIA_TYPE_VIDEO,
    .p.id                  = AV_CODEC_ID_AV1,
    .p.pix_fmt             = AV_PIX_FMT_VULKAN,
    .start_frame           = &vk_av1_start_frame,
    .decode_slice          = &vk_av1_decode_slice,
    .end_frame             = &vk_av1_end_frame,
    .free_frame_priv       = &vk_av1_free_frame_priv,
    .frame_priv_data_size  = sizeof(AV1VulkanDecodePicture),
    .init                  = &ff_vk_decode_init,
    .update_thread_context = &ff_vk_update_thread_context,
    .decode_params         = &ff_vk_params_invalidate,
    .flush                 = &ff_vk_decode_flush,
    .uninit                = &ff_vk_decode_uninit,
    .frame_params          = &ff_vk_frame_params,
    .priv_data_size        = sizeof(FFVulkanDecodeContext),

    /* NOTE: Threading is intentionally disabled here. Due to the design of Vulkan,
     * where frames are opaque to users, and mostly opaque for driver developers,
     * there's an issue with current hardware accelerator implementations of AV1,
     * where they require an internal index. With regular hwaccel APIs, this index
     * is given to users as an opaque handle directly. With Vulkan, due to increased
     * flexibility, this index cannot be present anywhere.
     * The current implementation tracks the index for the driver and submits it
     * as necessary information. Due to needing to modify the decoding context,
     * which is not thread-safe, on frame free, threading is disabled. */
    .caps_internal         = HWACCEL_CAP_ASYNC_SAFE,
};
