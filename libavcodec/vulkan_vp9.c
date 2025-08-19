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

#include "vp9dec.h"

#include "vulkan_decode.h"

const FFVulkanDecodeDescriptor ff_vk_dec_vp9_desc = {
    .codec_id         = AV_CODEC_ID_VP9,
    .decode_extension = FF_VK_EXT_VIDEO_DECODE_VP9,
    .queue_flags      = VK_QUEUE_VIDEO_DECODE_BIT_KHR,
    .decode_op        = VK_VIDEO_CODEC_OPERATION_DECODE_VP9_BIT_KHR,
    .ext_props = {
        .extensionName = VK_STD_VULKAN_VIDEO_CODEC_VP9_DECODE_EXTENSION_NAME,
        .specVersion   = VK_STD_VULKAN_VIDEO_CODEC_VP9_DECODE_SPEC_VERSION,
    },
};

typedef struct VP9VulkanDecodePicture {
    FFVulkanDecodePicture           vp;

    /* TODO: investigate if this can be removed to make decoding completely
     * independent. */
    FFVulkanDecodeContext          *dec;

    /* Current picture */
    StdVideoVP9ColorConfig color_config;
    StdVideoVP9Segmentation segmentation;
    StdVideoVP9LoopFilter loop_filter;
    StdVideoDecodeVP9PictureInfo std_pic_info;
    VkVideoDecodeVP9PictureInfoKHR vp9_pic_info;

    const VP9Frame *ref_src[8];

    uint8_t frame_id_set;
    uint8_t frame_id;
    uint8_t ref_frame_sign_bias_mask;
} VP9VulkanDecodePicture;

static int vk_vp9_fill_pict(AVCodecContext *avctx, const VP9Frame **ref_src,
                            VkVideoReferenceSlotInfoKHR *ref_slot,      /* Main structure */
                            VkVideoPictureResourceInfoKHR *ref,         /* Goes in ^ */
                            const VP9Frame *pic, int is_current)
{
    FFVulkanDecodeContext *dec = avctx->internal->hwaccel_priv_data;
    FFVulkanDecodeShared *ctx = dec->shared_ctx;
    VP9VulkanDecodePicture *hp = pic->hwaccel_picture_private;
    FFVulkanDecodePicture *vkpic = &hp->vp;

    int err = ff_vk_decode_prepare_frame(dec, pic->tf.f, vkpic, is_current,
                                         dec->dedicated_dpb);
    if (err < 0)
        return err;

    *ref = (VkVideoPictureResourceInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR,
        .codedOffset = (VkOffset2D){ 0, 0 },
        .codedExtent = (VkExtent2D){ pic->tf.f->width, pic->tf.f->height },
        .baseArrayLayer = (dec->dedicated_dpb && ctx->common.layered_dpb) ?
                          hp->frame_id : 0,
        .imageViewBinding = vkpic->view.ref[0],
    };

    *ref_slot = (VkVideoReferenceSlotInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR,
        .slotIndex = hp->frame_id,
        .pPictureResource = ref,
    };

    if (ref_src)
        *ref_src = pic;

    return 0;
}

static enum StdVideoVP9InterpolationFilter remap_interp(uint8_t is_filter_switchable,
                                                        uint8_t raw_interpolation_filter_type)
{
    static const enum StdVideoVP9InterpolationFilter remap[] = {
        STD_VIDEO_VP9_INTERPOLATION_FILTER_EIGHTTAP_SMOOTH,
        STD_VIDEO_VP9_INTERPOLATION_FILTER_EIGHTTAP,
        STD_VIDEO_VP9_INTERPOLATION_FILTER_EIGHTTAP_SHARP,
        STD_VIDEO_VP9_INTERPOLATION_FILTER_BILINEAR,
    };
    if (is_filter_switchable)
        return STD_VIDEO_VP9_INTERPOLATION_FILTER_SWITCHABLE;
    return remap[raw_interpolation_filter_type];
}

static int vk_vp9_start_frame(AVCodecContext          *avctx,
                              av_unused const AVBufferRef *buffer_ref,
                              av_unused const uint8_t *buffer,
                              av_unused uint32_t       size)
{
    int err;
    int ref_count = 0;
    const VP9Context *priv = avctx->priv_data;
    const CodedBitstreamVP9Context *vp9 = priv->cbc->priv_data;
    const VP9SharedContext *s = &priv->s;
    uint32_t frame_id_alloc_mask = 0;

    const VP9Frame *pic = &s->frames[CUR_FRAME];
    FFVulkanDecodeContext *dec = avctx->internal->hwaccel_priv_data;
    uint8_t profile = (pic->frame_header->profile_high_bit << 1) | pic->frame_header->profile_low_bit;

    VP9VulkanDecodePicture *ap = pic->hwaccel_picture_private;
    FFVulkanDecodePicture *vp = &ap->vp;

    /* Use the current frame_ids in ref_frames[] to decide occupied frame_ids */
    for (int i = 0; i < STD_VIDEO_VP9_NUM_REF_FRAMES; i++) {
        const VP9VulkanDecodePicture* rp = s->ref_frames[i].hwaccel_picture_private;
        if (rp)
            frame_id_alloc_mask |= 1 << rp->frame_id;
    }

    if (!ap->frame_id_set) {
        unsigned slot_idx = 0;
        for (unsigned i = 0; i < 32; i++) {
            if (!(frame_id_alloc_mask & (1 << i))) {
                slot_idx = i;
                break;
            }
        }
        ap->frame_id = slot_idx;
        ap->frame_id_set = 1;
        frame_id_alloc_mask |= (1 << slot_idx);
    }

    for (int i = 0; i < STD_VIDEO_VP9_REFS_PER_FRAME; i++) {
        const int idx = pic->frame_header->ref_frame_idx[i];
        const VP9Frame *ref_frame = &s->ref_frames[idx];
        VP9VulkanDecodePicture *hp = ref_frame->hwaccel_picture_private;
        int found = 0;

        if (!ref_frame->tf.f)
            continue;

        for (int j = 0; j < ref_count; j++) {
            if (vp->ref_slots[j].slotIndex == hp->frame_id) {
                found = 1;
                break;
            }
        }
        if (found)
            continue;

        err = vk_vp9_fill_pict(avctx, &ap->ref_src[ref_count],
                               &vp->ref_slots[ref_count], &vp->refs[ref_count],
                               ref_frame, 0);
        if (err < 0)
            return err;

        ref_count++;
    }

    err = vk_vp9_fill_pict(avctx, NULL, &vp->ref_slot, &vp->ref,
                           pic, 1);
    if (err < 0)
        return err;

    ap->loop_filter = (StdVideoVP9LoopFilter) {
        .flags = (StdVideoVP9LoopFilterFlags) {
            .loop_filter_delta_enabled = pic->frame_header->loop_filter_delta_enabled,
            .loop_filter_delta_update = pic->frame_header->loop_filter_delta_update,
        },
        .loop_filter_level = pic->frame_header->loop_filter_level,
        .loop_filter_sharpness = pic->frame_header->loop_filter_sharpness,
        .update_ref_delta = 0x0,
        .update_mode_delta = 0x0,
    };

    for (int i = 0; i < STD_VIDEO_VP9_MAX_REF_FRAMES; i++) {
        ap->loop_filter.loop_filter_ref_deltas[i] = vp9->loop_filter_ref_deltas[i];
        ap->loop_filter.update_ref_delta |= pic->frame_header->update_ref_delta[i];
    }
    for (int i = 0; i < STD_VIDEO_VP9_LOOP_FILTER_ADJUSTMENTS; i++) {
        ap->loop_filter.loop_filter_mode_deltas[i] = vp9->loop_filter_mode_deltas[i];
        ap->loop_filter.update_mode_delta |= pic->frame_header->update_mode_delta[i];
    }

    ap->segmentation = (StdVideoVP9Segmentation) {
        .flags = (StdVideoVP9SegmentationFlags) {
            .segmentation_update_map = pic->frame_header->segmentation_update_map,
            .segmentation_temporal_update = pic->frame_header->segmentation_temporal_update,
            .segmentation_update_data = pic->frame_header->segmentation_update_data,
            .segmentation_abs_or_delta_update = pic->frame_header->segmentation_abs_or_delta_update,
        },
    };

    for (int i = 0; i < STD_VIDEO_VP9_MAX_SEGMENTATION_TREE_PROBS; i++)
        ap->segmentation.segmentation_tree_probs[i] = vp9->segmentation_tree_probs[i];
    for (int i = 0; i < STD_VIDEO_VP9_MAX_SEGMENTATION_PRED_PROB; i++)
        ap->segmentation.segmentation_pred_prob[i] = vp9->segmentation_pred_prob[i];
    for (int i = 0; i < STD_VIDEO_VP9_MAX_SEGMENTS; i++) {
        ap->segmentation.FeatureEnabled[i] = 0x0;
        for (int j = 0; j < STD_VIDEO_VP9_SEG_LVL_MAX; j++) {
            ap->segmentation.FeatureEnabled[i] |= vp9->feature_enabled[i][j] << j;
            ap->segmentation.FeatureData[i][j] = vp9->feature_sign[i][j] ?
                                                 -vp9->feature_value[i][j] :
                                                 +vp9->feature_value[i][j];
        }
    }

    ap->color_config = (StdVideoVP9ColorConfig) {
        .flags = (StdVideoVP9ColorConfigFlags) {
            .color_range = pic->frame_header->color_range,
        },
        .BitDepth = profile < 2 ? 8 :
                    pic->frame_header->ten_or_twelve_bit ? 12 : 10,
        .subsampling_x = pic->frame_header->subsampling_x,
        .subsampling_y = pic->frame_header->subsampling_y,
        .color_space = pic->frame_header->color_space,
    };

    ap->std_pic_info = (StdVideoDecodeVP9PictureInfo) {
        .flags = (StdVideoDecodeVP9PictureInfoFlags) {
           .error_resilient_mode = pic->frame_header->error_resilient_mode,
           .intra_only = pic->frame_header->intra_only,
           .allow_high_precision_mv = pic->frame_header->allow_high_precision_mv,
           .refresh_frame_context = pic->frame_header->refresh_frame_context,
           .frame_parallel_decoding_mode = pic->frame_header->frame_parallel_decoding_mode,
           .segmentation_enabled = pic->frame_header->segmentation_enabled,
           .show_frame = pic->frame_header->segmentation_enabled,
           .UsePrevFrameMvs = s->h.use_last_frame_mvs,
        },
        .profile = profile,
        .frame_type = pic->frame_header->frame_type,
        .frame_context_idx = pic->frame_header->frame_context_idx,
        .reset_frame_context = pic->frame_header->reset_frame_context,
        .refresh_frame_flags = pic->frame_header->refresh_frame_flags,
        .ref_frame_sign_bias_mask = 0x0,
        .interpolation_filter = remap_interp(pic->frame_header->is_filter_switchable,
                                             pic->frame_header->raw_interpolation_filter_type),
        .base_q_idx = pic->frame_header->base_q_idx,
        .delta_q_y_dc = pic->frame_header->delta_q_y_dc,
        .delta_q_uv_dc = pic->frame_header->delta_q_uv_dc,
        .delta_q_uv_ac = pic->frame_header->delta_q_uv_ac,
        .tile_cols_log2 = pic->frame_header->tile_cols_log2,
        .tile_rows_log2 = pic->frame_header->tile_rows_log2,
        /* Reserved */
        .pColorConfig = &ap->color_config,
        .pLoopFilter = &ap->loop_filter,
        .pSegmentation = &ap->segmentation,
    };

    for (int i = VP9_LAST_FRAME; i <= VP9_ALTREF_FRAME; i++)
        ap->std_pic_info.ref_frame_sign_bias_mask |= pic->frame_header->ref_frame_sign_bias[i] << i;

    ap->vp9_pic_info = (VkVideoDecodeVP9PictureInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_VP9_PICTURE_INFO_KHR,
        .pStdPictureInfo = &ap->std_pic_info,
        .uncompressedHeaderOffset = 0,
        .compressedHeaderOffset = s->h.uncompressed_header_size,
        .tilesOffset = s->h.uncompressed_header_size +
                       s->h.compressed_header_size,
    };

    for (int i = 0; i < STD_VIDEO_VP9_REFS_PER_FRAME; i++) {
        const int idx = pic->frame_header->ref_frame_idx[i];
        const VP9Frame *ref_frame = &s->ref_frames[idx];
        VP9VulkanDecodePicture *hp = ref_frame->hwaccel_picture_private;

        if (!ref_frame->tf.f)
            ap->vp9_pic_info.referenceNameSlotIndices[i] = -1;
        else
            ap->vp9_pic_info.referenceNameSlotIndices[i] = hp->frame_id;
    }

    vp->decode_info = (VkVideoDecodeInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_INFO_KHR,
        .pNext = &ap->vp9_pic_info,
        .flags = 0x0,
        .pSetupReferenceSlot = &vp->ref_slot,
        .referenceSlotCount = ref_count,
        .pReferenceSlots = vp->ref_slots,
        .dstPictureResource = (VkVideoPictureResourceInfoKHR) {
            .sType = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR,
            .codedOffset = (VkOffset2D){ 0, 0 },
            .codedExtent = (VkExtent2D){ pic->tf.f->width, pic->tf.f->height },
            .baseArrayLayer = 0,
            .imageViewBinding = vp->view.out[0],
        },
    };

    ap->dec = dec;

    return 0;
}

static int vk_vp9_decode_slice(AVCodecContext *avctx,
                               const uint8_t  *data,
                               uint32_t        size)
{
    int err;
    const VP9SharedContext *s = avctx->priv_data;
    VP9VulkanDecodePicture *ap = s->frames[CUR_FRAME].hwaccel_picture_private;
    FFVulkanDecodePicture *vp = &ap->vp;

    err = ff_vk_decode_add_slice(avctx, vp, data, size, 0, NULL, NULL);
    if (err < 0)
        return err;

    return 0;
}

static int vk_vp9_end_frame(AVCodecContext *avctx)
{
    const VP9SharedContext *s = avctx->priv_data;

    const VP9Frame *pic = &s->frames[CUR_FRAME];
    VP9VulkanDecodePicture *ap = pic->hwaccel_picture_private;
    FFVulkanDecodePicture *vp = &ap->vp;
    FFVulkanDecodePicture *rvp[STD_VIDEO_VP9_REFS_PER_FRAME] = { 0 };
    AVFrame *rav[STD_VIDEO_VP9_REFS_PER_FRAME] = { 0 };

    for (int i = 0; i < vp->decode_info.referenceSlotCount; i++) {
        const VP9Frame *rp = ap->ref_src[i];
        VP9VulkanDecodePicture *rhp = rp->hwaccel_picture_private;

        rvp[i] = &rhp->vp;
        rav[i] = ap->ref_src[i]->tf.f;
    }

    av_log(avctx, AV_LOG_VERBOSE, "Decoding frame, %"SIZE_SPECIFIER" bytes\n",
           vp->slices_size);

    return ff_vk_decode_frame(avctx, pic->tf.f, vp, rav, rvp);
}

static void vk_vp9_free_frame_priv(AVRefStructOpaque _hwctx, void *data)
{
    AVHWDeviceContext *hwctx = _hwctx.nc;
    VP9VulkanDecodePicture *ap = data;

    /* Free frame resources, this also destroys the session parameters. */
    ff_vk_decode_free_frame(hwctx, &ap->vp);
}

const FFHWAccel ff_vp9_vulkan_hwaccel = {
    .p.name                = "vp9_vulkan",
    .p.type                = AVMEDIA_TYPE_VIDEO,
    .p.id                  = AV_CODEC_ID_VP9,
    .p.pix_fmt             = AV_PIX_FMT_VULKAN,
    .start_frame           = &vk_vp9_start_frame,
    .decode_slice          = &vk_vp9_decode_slice,
    .end_frame             = &vk_vp9_end_frame,
    .free_frame_priv       = &vk_vp9_free_frame_priv,
    .frame_priv_data_size  = sizeof(VP9VulkanDecodePicture),
    .init                  = &ff_vk_decode_init,
    .update_thread_context = &ff_vk_update_thread_context,
    .flush                 = &ff_vk_decode_flush,
    .uninit                = &ff_vk_decode_uninit,
    .frame_params          = &ff_vk_frame_params,
    .priv_data_size        = sizeof(FFVulkanDecodeContext),
    .caps_internal         = HWACCEL_CAP_ASYNC_SAFE,
};
