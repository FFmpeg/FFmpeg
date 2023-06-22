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

const VkExtensionProperties ff_vk_dec_av1_ext = {
    .extensionName = VK_STD_VULKAN_VIDEO_CODEC_AV1_DECODE_EXTENSION_NAME,
    .specVersion   = VK_STD_VULKAN_VIDEO_CODEC_AV1_DECODE_SPEC_VERSION,
};

typedef struct AV1VulkanDecodePicture {
    FFVulkanDecodePicture           vp;

    /* Workaround for a spec issue.
     *Can be removed once no longer needed, and threading can be enabled. */
    FFVulkanDecodeContext          *dec;

    StdVideoAV1MESATile            tiles[MAX_TILES];
    StdVideoAV1MESATileList        tile_list;
    const uint32_t                *tile_offsets;

    /* Current picture */
    VkVideoDecodeAV1DpbSlotInfoMESA    vkav1_ref;
    StdVideoAV1MESAFrameHeader         av1_frame_header;
    VkVideoDecodeAV1PictureInfoMESA    av1_pic_info;

    /* Picture refs */
    const AV1Frame                     *ref_src   [AV1_NUM_REF_FRAMES];
    VkVideoDecodeAV1DpbSlotInfoMESA     vkav1_refs[AV1_NUM_REF_FRAMES];

    uint8_t frame_id_set;
    uint8_t frame_id;
} AV1VulkanDecodePicture;

static int vk_av1_fill_pict(AVCodecContext *avctx, const AV1Frame **ref_src,
                            VkVideoReferenceSlotInfoKHR *ref_slot,      /* Main structure */
                            VkVideoPictureResourceInfoKHR *ref,         /* Goes in ^ */
                            VkVideoDecodeAV1DpbSlotInfoMESA *vkav1_ref, /* Goes in ^ */
                            const AV1Frame *pic, int is_current, int has_grain,
                            int dpb_slot_index)
{
    FFVulkanDecodeContext *dec = avctx->internal->hwaccel_priv_data;
    AV1VulkanDecodePicture *hp = pic->hwaccel_picture_private;
    FFVulkanDecodePicture *vkpic = &hp->vp;

    int err = ff_vk_decode_prepare_frame(dec, pic->f, vkpic, is_current,
                                         has_grain || dec->dedicated_dpb);
    if (err < 0)
        return err;

    *vkav1_ref = (VkVideoDecodeAV1DpbSlotInfoMESA) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_AV1_DPB_SLOT_INFO_MESA,
        .frameIdx = hp->frame_id,
    };

    for (unsigned i = 0; i < 7; i++) {
        const int idx = pic->raw_frame_header->ref_frame_idx[i];
        vkav1_ref->ref_order_hint[i] = pic->raw_frame_header->ref_order_hint[idx];
    }

    vkav1_ref->disable_frame_end_update_cdf = pic->raw_frame_header->disable_frame_end_update_cdf;

    *ref = (VkVideoPictureResourceInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR,
        .codedOffset = (VkOffset2D){ 0, 0 },
        .codedExtent = (VkExtent2D){ pic->f->width, pic->f->height },
        .baseArrayLayer = ((has_grain || dec->dedicated_dpb) && dec->layered_dpb) ?
                          dpb_slot_index : 0,
        .imageViewBinding = vkpic->img_view_ref,
    };

    *ref_slot = (VkVideoReferenceSlotInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR,
        .pNext = vkav1_ref,
        .slotIndex = dpb_slot_index,
        .pPictureResource = ref,
    };

    if (ref_src)
        *ref_src = pic;

    return 0;
}

static int vk_av1_create_params(AVCodecContext *avctx, AVBufferRef **buf)
{
    VkResult ret;

    const AV1DecContext *s = avctx->priv_data;
    FFVulkanDecodeContext *dec = avctx->internal->hwaccel_priv_data;
    FFVulkanDecodeShared *ctx = (FFVulkanDecodeShared *)dec->shared_ref->data;
    FFVulkanFunctions *vk = &ctx->s.vkfn;

    const AV1RawSequenceHeader *seq = s->raw_seq;

    StdVideoAV1MESASequenceHeader av1_sequence_header;
    VkVideoDecodeAV1SessionParametersAddInfoMESA av1_params_info;
    VkVideoDecodeAV1SessionParametersCreateInfoMESA av1_params;
    VkVideoSessionParametersCreateInfoKHR session_params_create;

    AVBufferRef *tmp;
    VkVideoSessionParametersKHR *par = av_malloc(sizeof(*par));
    if (!par)
        return AVERROR(ENOMEM);

    av1_sequence_header = (StdVideoAV1MESASequenceHeader) {
        .flags = (StdVideoAV1MESASequenceHeaderFlags) {
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
        .timing_info = (StdVideoAV1MESATimingInfo) {
            .flags = (StdVideoAV1MESATimingInfoFlags) {
                .equal_picture_interval = seq->timing_info.equal_picture_interval,
            },
            .num_units_in_display_tick = seq->timing_info.num_units_in_display_tick,
            .time_scale = seq->timing_info.time_scale,
            .num_ticks_per_picture_minus_1 = seq->timing_info.num_ticks_per_picture_minus_1,
        },
        .color_config = (StdVideoAV1MESAColorConfig) {
            .flags = (StdVideoAV1MESAColorConfigFlags) {
                .mono_chrome = seq->color_config.mono_chrome,
                .color_range = seq->color_config.color_range,
                .separate_uv_delta_q = seq->color_config.separate_uv_delta_q,
            },
            .bit_depth = seq->color_config.twelve_bit ? 12 :
                         seq->color_config.high_bitdepth ? 10 : 8,
            .subsampling_x = seq->color_config.subsampling_x,
            .subsampling_y = seq->color_config.subsampling_y,
        },
    };

    av1_params_info = (VkVideoDecodeAV1SessionParametersAddInfoMESA) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_AV1_SESSION_PARAMETERS_ADD_INFO_MESA,
        .sequence_header = &av1_sequence_header,
    };
    av1_params = (VkVideoDecodeAV1SessionParametersCreateInfoMESA) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_AV1_SESSION_PARAMETERS_CREATE_INFO_MESA,
        .pParametersAddInfo = &av1_params_info,
    };
    session_params_create = (VkVideoSessionParametersCreateInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_PARAMETERS_CREATE_INFO_KHR,
        .pNext = &av1_params,
        .videoSession = ctx->common.session,
        .videoSessionParametersTemplate = NULL,
    };

    /* Create session parameters */
    ret = vk->CreateVideoSessionParametersKHR(ctx->s.hwctx->act_dev, &session_params_create,
                                              ctx->s.hwctx->alloc, par);
    if (ret != VK_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Unable to create Vulkan video session parameters: %s!\n",
               ff_vk_ret2str(ret));
        return AVERROR_EXTERNAL;
    }

    tmp = av_buffer_create((uint8_t *)par, sizeof(*par), ff_vk_decode_free_params,
                           ctx, 0);
    if (!tmp) {
        ff_vk_decode_free_params(ctx, (uint8_t *)par);
        return AVERROR(ENOMEM);
    }

    av_log(avctx, AV_LOG_DEBUG, "Created frame parameters\n");

    *buf = tmp;

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
    const int apply_grain = !(avctx->export_side_data & AV_CODEC_EXPORT_DATA_FILM_GRAIN) &&
                            film_grain->apply_grain;

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

    /* Fill in references */
    for (int i = 0; i < AV1_NUM_REF_FRAMES; i++) {
        const AV1Frame *ref_frame = &s->ref[i];
        if (s->ref[i].f->pict_type == AV_PICTURE_TYPE_NONE)
            continue;

        err = vk_av1_fill_pict(avctx, &ap->ref_src[i], &vp->ref_slots[i],
                               &vp->refs[i], &ap->vkav1_refs[i],
                               ref_frame, 0, 0, i);
        if (err < 0)
            return err;

        ref_count++;
    }

    err = vk_av1_fill_pict(avctx, NULL, &vp->ref_slot, &vp->ref,
                           &ap->vkav1_ref,
                           pic, 1, apply_grain, 8);
    if (err < 0)
        return err;

    ap->tile_list.nb_tiles = 0;
    ap->tile_list.tile_list = ap->tiles;

    ap->av1_pic_info = (VkVideoDecodeAV1PictureInfoMESA) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_AV1_PICTURE_INFO_MESA,
        .frame_header = &ap->av1_frame_header,
        .tile_list = &ap->tile_list,
    };

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

    /* Setup frame header */
    ap->av1_frame_header = (StdVideoAV1MESAFrameHeader) {
        .flags = (StdVideoAV1MESAFrameHeaderFlags) {
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
        },
        .frame_to_show_map_idx = frame_header->frame_to_show_map_idx,
        .frame_presentation_time = frame_header->frame_presentation_time,
        .display_frame_id = frame_header->display_frame_id,
        .frame_type = frame_header->frame_type,
        .current_frame_id = frame_header->current_frame_id,
        .order_hint = frame_header->order_hint,
        .primary_ref_frame = frame_header->primary_ref_frame,
        .frame_width_minus_1 = frame_header->frame_width_minus_1,
        .frame_height_minus_1 = frame_header->frame_height_minus_1,
        .coded_denom = frame_header->coded_denom,
        .render_width_minus_1 = frame_header->render_width_minus_1,
        .render_height_minus_1 = frame_header->render_height_minus_1,
        .refresh_frame_flags = frame_header->refresh_frame_flags,
        .interpolation_filter = frame_header->interpolation_filter,
        .tx_mode = frame_header->tx_mode,
        .tiling = (StdVideoAV1MESATileInfo) {
            .flags = (StdVideoAV1MESATileInfoFlags) {
                .uniform_tile_spacing_flag = frame_header->uniform_tile_spacing_flag,
            },
            .tile_cols = frame_header->tile_cols,
            .tile_rows = frame_header->tile_rows,
            .context_update_tile_id = frame_header->context_update_tile_id,
            .tile_size_bytes_minus1 = frame_header->tile_size_bytes_minus1,
        },
        .quantization = (StdVideoAV1MESAQuantization) {
            .flags.using_qmatrix = frame_header->using_qmatrix,
            .base_q_idx = frame_header->base_q_idx,
            .delta_q_y_dc = frame_header->delta_q_y_dc,
            .diff_uv_delta = frame_header->diff_uv_delta,
            .delta_q_u_dc = frame_header->delta_q_u_dc,
            .delta_q_u_ac = frame_header->delta_q_u_ac,
            .delta_q_v_dc = frame_header->delta_q_v_dc,
            .delta_q_v_ac = frame_header->delta_q_v_ac,
            .qm_y = frame_header->qm_y,
            .qm_u = frame_header->qm_u,
            .qm_v = frame_header->qm_v,
        },
        .delta_q = (StdVideoAV1MESADeltaQ) {
            .flags = (StdVideoAV1MESADeltaQFlags) {
                .delta_lf_present = frame_header->delta_lf_present,
                .delta_lf_multi = frame_header->delta_lf_multi,
            },
            .delta_q_res = frame_header->delta_q_res,
            .delta_lf_res = frame_header->delta_lf_res,
        },
        .loop_filter = (StdVideoAV1MESALoopFilter) {
            .flags = (StdVideoAV1MESALoopFilterFlags) {
                .delta_enabled = frame_header->loop_filter_delta_enabled,
                .delta_update = frame_header->loop_filter_delta_update,
            },
            .level = {
                frame_header->loop_filter_level[0], frame_header->loop_filter_level[1],
                frame_header->loop_filter_level[2], frame_header->loop_filter_level[3],
            },
            .sharpness = frame_header->loop_filter_sharpness,
            .mode_deltas = {
                frame_header->loop_filter_mode_deltas[0], frame_header->loop_filter_mode_deltas[1],
            },
        },
        .cdef = (StdVideoAV1MESACDEF) {
            .damping_minus_3 = frame_header->cdef_damping_minus_3,
            .bits = frame_header->cdef_bits,
        },
        .lr = (StdVideoAV1MESALoopRestoration) {
            .lr_unit_shift = frame_header->lr_unit_shift,
            .lr_uv_shift = frame_header->lr_uv_shift,
            .lr_type = { frame_header->lr_type[0], frame_header->lr_type[1], frame_header->lr_type[2] },
        },
        .segmentation = (StdVideoAV1MESASegmentation) {
            .flags = (StdVideoAV1MESASegmentationFlags) {
                .enabled = frame_header->segmentation_enabled,
                .update_map = frame_header->segmentation_update_map,
                .temporal_update = frame_header->segmentation_temporal_update,
                .update_data = frame_header->segmentation_update_data,
            },
        },
        .film_grain = (StdVideoAV1MESAFilmGrainParameters) {
            .flags = (StdVideoAV1MESAFilmGrainFlags) {
                .apply_grain = apply_grain,
                .chroma_scaling_from_luma = film_grain->chroma_scaling_from_luma,
                .overlap_flag = film_grain->overlap_flag,
                .clip_to_restricted_range = film_grain->clip_to_restricted_range,
            },
            .grain_scaling_minus_8 = film_grain->grain_scaling_minus_8,
            .ar_coeff_lag = film_grain->ar_coeff_lag,
            .ar_coeff_shift_minus_6 = film_grain->ar_coeff_shift_minus_6,
            .grain_scale_shift = film_grain->grain_scale_shift,
            .grain_seed = film_grain->grain_seed,
            .num_y_points = film_grain->num_y_points,
            .num_cb_points = film_grain->num_cb_points,
            .num_cr_points = film_grain->num_cr_points,
            .cb_mult = film_grain->cb_mult,
            .cb_luma_mult = film_grain->cb_luma_mult,
            .cb_offset = film_grain->cb_offset,
            .cr_mult = film_grain->cr_mult,
            .cr_luma_mult = film_grain->cr_luma_mult,
            .cr_offset = film_grain->cr_offset,
        },
    };

    for (int i = 0; i < 64; i++) {
        ap->av1_frame_header.tiling.width_in_sbs_minus_1[i] = frame_header->width_in_sbs_minus_1[i];
        ap->av1_frame_header.tiling.height_in_sbs_minus_1[i] = frame_header->height_in_sbs_minus_1[i];
        ap->av1_frame_header.tiling.tile_start_col_sb[i] = frame_header->tile_start_col_sb[i];
        ap->av1_frame_header.tiling.tile_start_row_sb[i] = frame_header->tile_start_row_sb[i];
    }

    for (int i = 0; i < 8; i++) {
        ap->av1_frame_header.segmentation.feature_enabled_bits[i] = 0;
        for (int j = 0; j < 8; j++) {
            ap->av1_frame_header.segmentation.feature_enabled_bits[i] |= (frame_header->feature_enabled[i][j] << j);
            ap->av1_frame_header.segmentation.feature_data[i][j] = frame_header->feature_value[i][j];
        }

        ap->av1_frame_header.loop_filter.ref_deltas[i] = frame_header->loop_filter_ref_deltas[i];

        ap->av1_frame_header.cdef.y_pri_strength[i] = frame_header->cdef_y_pri_strength[i];
        ap->av1_frame_header.cdef.y_sec_strength[i] = frame_header->cdef_y_sec_strength[i];
        ap->av1_frame_header.cdef.uv_pri_strength[i] = frame_header->cdef_uv_pri_strength[i];
        ap->av1_frame_header.cdef.uv_sec_strength[i] = frame_header->cdef_uv_sec_strength[i];

        ap->av1_frame_header.ref_order_hint[i] = frame_header->ref_order_hint[i];
        ap->av1_frame_header.global_motion[i] = (StdVideoAV1MESAGlobalMotion) {
            .flags = (StdVideoAV1MESAGlobalMotionFlags) {
                .gm_invalid = s->cur_frame.gm_invalid[i],
            },
            .gm_type = s->cur_frame.gm_type[i],
            .gm_params = {
                s->cur_frame.gm_params[i][0], s->cur_frame.gm_params[i][1],
                s->cur_frame.gm_params[i][2], s->cur_frame.gm_params[i][3],
                s->cur_frame.gm_params[i][4], s->cur_frame.gm_params[i][5],
            },
        };
    }

    for (int i = 0; i < 7; i++) {
        ap->av1_frame_header.ref_frame_idx[i] = frame_header->ref_frame_idx[i];
        ap->av1_frame_header.delta_frame_id_minus1[i] = frame_header->delta_frame_id_minus1[i];
    }

    ap->av1_pic_info.skip_mode_frame_idx[0] = s->cur_frame.skip_mode_frame_idx[0];
    ap->av1_pic_info.skip_mode_frame_idx[1] = s->cur_frame.skip_mode_frame_idx[1];

    if (apply_grain) {
        for (int i = 0; i < 14; i++) {
            ap->av1_frame_header.film_grain.point_y_value[i] = film_grain->point_y_value[i];
            ap->av1_frame_header.film_grain.point_y_scaling[i] = film_grain->point_y_scaling[i];
        }

        for (int i = 0; i < 10; i++) {
            ap->av1_frame_header.film_grain.point_cb_value[i] = film_grain->point_cb_value[i];
            ap->av1_frame_header.film_grain.point_cb_scaling[i] = film_grain->point_cb_scaling[i];
            ap->av1_frame_header.film_grain.point_cr_value[i] = film_grain->point_cr_value[i];
            ap->av1_frame_header.film_grain.point_cr_scaling[i] = film_grain->point_cr_scaling[i];
        }

        for (int i = 0; i < 24; i++) {
            ap->av1_frame_header.film_grain.ar_coeffs_y_plus_128[i] = film_grain->ar_coeffs_y_plus_128[i];
            ap->av1_frame_header.film_grain.ar_coeffs_cb_plus_128[i] = film_grain->ar_coeffs_cb_plus_128[i];
            ap->av1_frame_header.film_grain.ar_coeffs_cr_plus_128[i] = film_grain->ar_coeffs_cr_plus_128[i];
        }

        ap->av1_frame_header.film_grain.ar_coeffs_cb_plus_128[24] = film_grain->ar_coeffs_cb_plus_128[24];
        ap->av1_frame_header.film_grain.ar_coeffs_cr_plus_128[24] = film_grain->ar_coeffs_cr_plus_128[24];
    }

    av_log(avctx, AV_LOG_DEBUG, "Created frame parameters");

    /* Workaround for a spec issue. */
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

    for (int i = s->tg_start; i <= s->tg_end; i++) {
        ap->tiles[ap->tile_list.nb_tiles] = (StdVideoAV1MESATile) {
            .size     = s->tile_group_info[i].tile_size,
            .offset   = s->tile_group_info[i].tile_offset,
            .row      = s->tile_group_info[i].tile_row,
            .column   = s->tile_group_info[i].tile_column,
            .tg_start = s->tg_start,
            .tg_end   = s->tg_end,
        };

        err = ff_vk_decode_add_slice(avctx, vp,
                                     data + s->tile_group_info[i].tile_offset,
                                     s->tile_group_info[i].tile_size, 0,
                                     &ap->tile_list.nb_tiles,
                                     &ap->tile_offsets);
        if (err < 0)
            return err;

        ap->tiles[ap->tile_list.nb_tiles - 1].offset = ap->tile_offsets[ap->tile_list.nb_tiles - 1];
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

    if (!ap->tile_list.nb_tiles)
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
           vp->slices_size, ap->tile_list.nb_tiles);

    return ff_vk_decode_frame(avctx, pic->f, vp, rav, rvp);
}

static void vk_av1_free_frame_priv(void *_hwctx, uint8_t *data)
{
    AVHWDeviceContext *hwctx = _hwctx;
    AV1VulkanDecodePicture *ap = (AV1VulkanDecodePicture *)data;

    /* Workaround for a spec issue. */
    if (ap->frame_id_set)
        ap->dec->frame_id_alloc_mask &= ~(1 << ap->frame_id);

    /* Free frame resources, this also destroys the session parameters. */
    ff_vk_decode_free_frame(hwctx, &ap->vp);

    /* Free frame context */
    av_free(ap);
}

const AVHWAccel ff_av1_vulkan_hwaccel = {
    .name                  = "av1_vulkan",
    .type                  = AVMEDIA_TYPE_VIDEO,
    .id                    = AV_CODEC_ID_AV1,
    .pix_fmt               = AV_PIX_FMT_VULKAN,
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
     * which is not thread-safe, on frame free, threading is disabled.
     * In the future, once this is fixed in the spec, the workarounds may be removed
     * and threading enabled. */
    .caps_internal         = HWACCEL_CAP_ASYNC_SAFE,
};
