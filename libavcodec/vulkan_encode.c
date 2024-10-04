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
#include "libavutil/avassert.h"
#include "vulkan_encode.h"
#include "config.h"

#include "libavutil/vulkan_loader.h"

const AVCodecHWConfigInternal *const ff_vulkan_encode_hw_configs[] = {
    HW_CONFIG_ENCODER_FRAMES(VULKAN, VULKAN),
    NULL,
};

av_cold void ff_vulkan_encode_uninit(FFVulkanEncodeContext *ctx)
{
    FFVulkanContext *s = &ctx->s;
    FFVulkanFunctions *vk = &s->vkfn;

    /* Wait on and free execution pool */
    ff_vk_exec_pool_free(s, &ctx->enc_pool);

    /* Destroy the session params */
    if (ctx->session_params)
        vk->DestroyVideoSessionParametersKHR(s->hwctx->act_dev,
                                             ctx->session_params,
                                             s->hwctx->alloc);

    ff_hw_base_encode_close(&ctx->base);

    av_buffer_pool_uninit(&ctx->buf_pool);

    ff_vk_video_common_uninit(s, &ctx->common);

    ff_vk_uninit(s);
}

static int vulkan_encode_init(AVCodecContext *avctx, FFHWBaseEncodePicture *pic)
{
    int err;
    FFVulkanEncodeContext *ctx = avctx->priv_data;
    FFVulkanEncodePicture *vp = pic->priv;

    AVFrame *f = pic->input_image;
    AVHWFramesContext *hwfc = (AVHWFramesContext *)f->hw_frames_ctx->data;
    AVVulkanFramesContext *vkfc = hwfc->hwctx;
    AVVkFrame *vkf = (AVVkFrame *)f->data[0];

    if (ctx->codec->picture_priv_data_size > 0) {
        pic->codec_priv = av_mallocz(ctx->codec->picture_priv_data_size);
        if (!pic->codec_priv)
            return AVERROR(ENOMEM);
    }

    /* Input image view */
    err = ff_vk_create_view(&ctx->s, &ctx->common,
                            &vp->in.view, &vp->in.aspect,
                            vkf, vkfc->format[0], 0);
    if (err < 0)
        return err;

    /* Reference view */
    if (!ctx->common.layered_dpb) {
        AVFrame *rf = pic->recon_image;
        AVVkFrame *rvkf = (AVVkFrame *)rf->data[0];
        err = ff_vk_create_view(&ctx->s, &ctx->common,
                                &vp->dpb.view, &vp->dpb.aspect,
                                rvkf, ctx->pic_format, 1);
        if (err < 0)
            return err;
    } else {
        vp->dpb.view = ctx->common.layered_view;
        vp->dpb.aspect = ctx->common.layered_aspect;
    }

    return 0;
}

static int vulkan_encode_free(AVCodecContext *avctx, FFHWBaseEncodePicture *pic)
{
    FFVulkanEncodeContext *ctx = avctx->priv_data;
    FFVulkanFunctions *vk = &ctx->s.vkfn;

    FFVulkanEncodePicture *vp = pic->priv;

    if (vp->in.view)
        vk->DestroyImageView(ctx->s.hwctx->act_dev, vp->in.view,
                             ctx->s.hwctx->alloc);

    if (!ctx->common.layered_dpb && vp->dpb.view)
        vk->DestroyImageView(ctx->s.hwctx->act_dev, vp->dpb.view,
                             ctx->s.hwctx->alloc);

    ctx->slots[vp->dpb_slot.slotIndex] = NULL;

    return 0;
}

static int init_pic_rc(AVCodecContext *avctx, FFHWBaseEncodePicture *pic,
                       VkVideoEncodeRateControlInfoKHR *rc_info,
                       VkVideoEncodeRateControlLayerInfoKHR *rc_layer /* Goes in ^ */)
{
    FFVulkanEncodeContext *ctx = avctx->priv_data;

    *rc_info = (VkVideoEncodeRateControlInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_RATE_CONTROL_INFO_KHR,
        .rateControlMode = ctx->opts.rc_mode,
    };

    if (ctx->opts.rc_mode > VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR) {
        *rc_layer = (VkVideoEncodeRateControlLayerInfoKHR) {
            .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_RATE_CONTROL_LAYER_INFO_KHR,
            .averageBitrate = avctx->bit_rate,
            .maxBitrate = avctx->rc_max_rate ? avctx->rc_max_rate : avctx->bit_rate,
            .frameRateNumerator = avctx->framerate.num,
            .frameRateDenominator = avctx->framerate.den,
        };
        rc_info->layerCount++;
        rc_info->pLayers = rc_layer;
    }

    return ctx->codec->init_pic_rc(avctx, pic, rc_info, rc_layer);
}

static int vulkan_encode_issue(AVCodecContext *avctx,
                               FFHWBaseEncodePicture *base_pic)
{
    FFVulkanEncodeContext *ctx = avctx->priv_data;
    FFVulkanFunctions *vk = &ctx->s.vkfn;

    const size_t size_align = ctx->caps.minBitstreamBufferSizeAlignment;

    FFVulkanEncodePicture *vp = base_pic->priv;
    AVFrame *src = (AVFrame *)base_pic->input_image;
    AVVkFrame *vkf = (AVVkFrame *)src->data[0];

    int err, max_pkt_size;

    FFVkBuffer *sd_buf;

    int slot_index = -1;
    FFVkExecContext *exec;
    VkCommandBuffer cmd_buf;
    VkImageMemoryBarrier2 img_bar[37];
    int nb_img_bar = 0;

    /* Coding start/end */
    VkVideoBeginCodingInfoKHR encode_start;
    VkVideoEndCodingInfoKHR encode_end = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_END_CODING_INFO_KHR,
    };

    VkVideoEncodeRateControlLayerInfoKHR rc_layer;
    VkVideoEncodeRateControlInfoKHR rc_info;
    VkVideoEncodeQualityLevelInfoKHR q_info;
    VkVideoCodingControlInfoKHR encode_ctrl;

    VkVideoReferenceSlotInfoKHR ref_slot[37];
    VkVideoEncodeInfoKHR encode_info;

    /* Create packet data buffer */
    max_pkt_size = FFALIGN(3 * ctx->base.surface_width * ctx->base.surface_height + (1 << 16),
                           ctx->caps.minBitstreamBufferSizeAlignment);

    err = ff_vk_get_pooled_buffer(&ctx->s, &ctx->buf_pool, &vp->pkt_buf,
                                  VK_BUFFER_USAGE_VIDEO_ENCODE_DST_BIT_KHR,
                                  &ctx->profile_list, max_pkt_size,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                  VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
    if (err < 0)
        return err;

    sd_buf = (FFVkBuffer *)vp->pkt_buf->data;

    /* Setup rate control */
    err = init_pic_rc(avctx, base_pic, &rc_info, &rc_layer);
    if (err < 0)
        return err;

    q_info = (VkVideoEncodeQualityLevelInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_QUALITY_LEVEL_INFO_KHR,
        .pNext = &rc_info,
        .qualityLevel = ctx->opts.quality,
    };
    encode_ctrl = (VkVideoCodingControlInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_CODING_CONTROL_INFO_KHR,
        .pNext = &q_info,
        .flags = VK_VIDEO_CODING_CONTROL_ENCODE_QUALITY_LEVEL_BIT_KHR |
                 VK_VIDEO_CODING_CONTROL_ENCODE_RATE_CONTROL_BIT_KHR |
                 VK_VIDEO_CODING_CONTROL_RESET_BIT_KHR,
    };

    for (int i = 0; i < ctx->caps.maxDpbSlots; i++) {
        if (ctx->slots[i] == NULL) {
            slot_index = i;
            ctx->slots[i] = base_pic;
            break;
        }
    }
    av_assert0(slot_index >= 0);

    /* Current picture's ref slot */
    vp->dpb_res = (VkVideoPictureResourceInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR,
        .pNext = NULL,
        .codedOffset = { 0 },
        .codedExtent = (VkExtent2D){ avctx->width, avctx->height },
        .baseArrayLayer = ctx->common.layered_dpb ? slot_index : 0,
        .imageViewBinding = vp->dpb.view,
    };

    vp->dpb_slot = (VkVideoReferenceSlotInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR,
        .pNext = NULL,  // Set later
        .slotIndex = slot_index,
        .pPictureResource = &vp->dpb_res,
    };

    encode_info = (VkVideoEncodeInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_INFO_KHR,
        .pNext = NULL, // Set later
        .flags = 0x0,
        .srcPictureResource = (VkVideoPictureResourceInfoKHR) {
            .sType = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR,
            .pNext = NULL,
            .codedOffset = { 0, 0 },
            .codedExtent = (VkExtent2D){ base_pic->input_image->width,
                                         base_pic->input_image->height },
            .baseArrayLayer = 0,
            .imageViewBinding = vp->in.view,
        },
        .pSetupReferenceSlot = &vp->dpb_slot,
        .referenceSlotCount = 0,
        .pReferenceSlots = ref_slot,
        .dstBuffer = sd_buf->buf,
        .dstBufferOffset = 0,
        .dstBufferRange = sd_buf->size,
        .precedingExternallyEncodedBytes = 0,
    };

    for (int i = 0; i < MAX_REFERENCE_LIST_NUM; i++) {
        for (int j = 0; j < base_pic->nb_refs[i]; j++) {
            FFHWBaseEncodePicture *ref = base_pic->refs[i][j];
            FFVulkanEncodePicture *rvp = ref->priv;
            ref_slot[encode_info.referenceSlotCount++] = rvp->dpb_slot;
        }
    }

    /* Calling vkCmdBeginVideoCodingKHR requires to declare all references
     * being enabled upfront, including the current frame's output ref. */
    ref_slot[encode_info.referenceSlotCount] = vp->dpb_slot;
    ref_slot[encode_info.referenceSlotCount].slotIndex = -1;

    /* Setup picture parameters */
    err = ctx->codec->init_pic_params(avctx, base_pic,
                                      &encode_info);
    if (err < 0)
        return err;

    encode_start = (VkVideoBeginCodingInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_BEGIN_CODING_INFO_KHR,
        .pNext = !base_pic->force_idr ? &rc_info : NULL,
        .videoSession = ctx->common.session,
        .videoSessionParameters = ctx->session_params,
        .referenceSlotCount = encode_info.referenceSlotCount + 1,
        .pReferenceSlots = ref_slot,
    };

    /* Write header */
    if (base_pic->type == FF_HW_PICTURE_TYPE_IDR) {
        uint8_t *hdr_dst = sd_buf->mapped_mem + encode_info.dstBufferOffset;
        size_t data_size = encode_info.dstBufferRange;
        err = ctx->codec->write_sequence_headers(avctx, base_pic, hdr_dst, &data_size);
        if (err < 0)
            goto fail;
        encode_info.dstBufferOffset += data_size;
        encode_info.dstBufferRange  -= data_size;
    }

    /* Write extra units */
    if (ctx->codec->write_extra_headers) {
        uint8_t *hdr_dst = sd_buf->mapped_mem + encode_info.dstBufferOffset;
        size_t data_size = encode_info.dstBufferRange;
        err = ctx->codec->write_extra_headers(avctx, base_pic, hdr_dst, &data_size);
        if (err < 0)
            goto fail;
        encode_info.dstBufferOffset += data_size;
        encode_info.dstBufferRange  -= data_size;
    }

    /* Align buffer offset to the required value with filler units */
    if (ctx->codec->write_filler) {
        uint8_t *hdr_dst = sd_buf->mapped_mem + encode_info.dstBufferOffset;
        size_t data_size = encode_info.dstBufferRange;

        uint32_t offset = encode_info.dstBufferOffset;
        size_t offset_align = ctx->caps.minBitstreamBufferOffsetAlignment;

        uint32_t filler_data = FFALIGN(offset, offset_align) - offset;

        if (filler_data) {
            while (filler_data < ctx->codec->filler_header_size)
                filler_data += offset_align;

            filler_data -= ctx->codec->filler_header_size;

            err = ctx->codec->write_filler(avctx, filler_data,
                                           hdr_dst, &data_size);
            if (err < 0)
                goto fail;

            encode_info.dstBufferOffset += data_size;
            encode_info.dstBufferRange  -= data_size;
        }
    }

    vp->slices_offset = encode_info.dstBufferOffset;

    /* Align buffer size to the nearest lower alignment requirement. */
    encode_info.dstBufferRange -= size_align;
    encode_info.dstBufferRange = FFALIGN(encode_info.dstBufferRange,
                                         size_align);

    /* Start command buffer recording */
    exec = vp->exec = ff_vk_exec_get(&ctx->s, &ctx->enc_pool);
    ff_vk_exec_start(&ctx->s, exec);
    cmd_buf = exec->buf;

    /* Output packet buffer */
    err = ff_vk_exec_add_dep_buf(&ctx->s, exec, &vp->pkt_buf, 1, 1);
    if (err < 0)
        goto fail;

    /* Source image */
    err = ff_vk_exec_add_dep_frame(&ctx->s, exec, src,
                                   VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                   VK_PIPELINE_STAGE_2_VIDEO_ENCODE_BIT_KHR);
    if (err < 0)
        goto fail;

    /* Source image layout conversion */
    img_bar[nb_img_bar] = (VkImageMemoryBarrier2) {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .pNext = NULL,
        .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .srcAccessMask = vkf->access[0],
        .dstStageMask = VK_PIPELINE_STAGE_2_VIDEO_ENCODE_BIT_KHR,
        .dstAccessMask = VK_ACCESS_2_VIDEO_ENCODE_READ_BIT_KHR,
        .oldLayout = vkf->layout[0],
        .newLayout = VK_IMAGE_LAYOUT_VIDEO_ENCODE_SRC_KHR,
        .srcQueueFamilyIndex = vkf->queue_family[0],
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = vkf->img[0],
        .subresourceRange = (VkImageSubresourceRange) {
            .aspectMask = vp->in.aspect,
            .layerCount = 1,
            .levelCount = 1,
        },
    };
    ff_vk_exec_update_frame(&ctx->s, exec, src,
                            &img_bar[nb_img_bar], &nb_img_bar);

    if (!ctx->common.layered_dpb) {
        /* Source image's ref slot.
         * No need to do a layout conversion, since the frames which are allocated
         * with a DPB usage are automatically converted. */
        err = ff_vk_exec_add_dep_frame(&ctx->s, exec, base_pic->recon_image,
                                       VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                       VK_PIPELINE_STAGE_2_VIDEO_ENCODE_BIT_KHR);
        if (err < 0)
            return err;

        /* All references */
        for (int i = 0; i < MAX_REFERENCE_LIST_NUM; i++) {
            for (int j = 0; j < base_pic->nb_refs[i]; j++) {
                FFHWBaseEncodePicture *ref = base_pic->refs[i][j];
                err = ff_vk_exec_add_dep_frame(&ctx->s, exec, ref->recon_image,
                                               VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                               VK_PIPELINE_STAGE_2_VIDEO_ENCODE_BIT_KHR);
                if (err < 0)
                    return err;
            }
        }
    } else {
        err = ff_vk_exec_add_dep_frame(&ctx->s, exec, ctx->common.layered_frame,
                                       VK_PIPELINE_STAGE_2_VIDEO_ENCODE_BIT_KHR,
                                       VK_PIPELINE_STAGE_2_VIDEO_ENCODE_BIT_KHR);
        if (err < 0)
            return err;
    }

    /* Change image layout */
    vk->CmdPipelineBarrier2(cmd_buf, &(VkDependencyInfo) {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pImageMemoryBarriers = img_bar,
            .imageMemoryBarrierCount = nb_img_bar,
        });

    /* Start, use parameters */
    vk->CmdBeginVideoCodingKHR(cmd_buf, &encode_start);

    /* Send control data */
    if (!ctx->session_reset) {
        vk->CmdControlVideoCodingKHR(cmd_buf, &encode_ctrl);
        ctx->session_reset++;
    }

    /* Encode */
    vk->CmdBeginQuery(cmd_buf, ctx->enc_pool.query_pool, exec->query_idx + 0, 0);
    vk->CmdEncodeVideoKHR(cmd_buf, &encode_info);
    vk->CmdEndQuery(cmd_buf, ctx->enc_pool.query_pool, exec->query_idx + 0);

    /* End encoding */
    vk->CmdEndVideoCodingKHR(cmd_buf, &encode_end);

    /* End recording and submit for execution */
    err = ff_vk_exec_submit(&ctx->s, vp->exec);
    if (err < 0)
        goto fail;

    /* We don't need to keep the input image any longer, its already ref'd */
    av_frame_free(&base_pic->input_image);

    return 0;

fail:
    return err;
}

static void vulkan_encode_wait(AVCodecContext *avctx,
                               FFHWBaseEncodePicture *base_pic)
{
    FFVulkanEncodeContext *ctx = avctx->priv_data;
    FFVulkanEncodePicture *vp = base_pic->priv;

    av_assert0(base_pic->encode_issued);

    if (base_pic->encode_complete)
        return;

    ff_vk_exec_wait(&ctx->s, vp->exec);
    base_pic->encode_complete = 1;
}

static int vulkan_encode_output(AVCodecContext *avctx,
                                FFHWBaseEncodePicture *base_pic, AVPacket *pkt)
{
    VkResult ret;
    FFVulkanEncodePicture *vp = base_pic->priv;
    FFVulkanEncodeContext *ctx = avctx->priv_data;
    FFVkBuffer *sd_buf = (FFVkBuffer *)vp->pkt_buf->data;
    uint32_t *query_data;

    vulkan_encode_wait(avctx, base_pic);

    ret = ff_vk_exec_get_query(&ctx->s, vp->exec, (void **)&query_data, 0);
    if (ret == VK_NOT_READY) {
        av_log(avctx, AV_LOG_ERROR, "Unable to perform query: %s!\n",
               ff_vk_ret2str(ret));
        return AVERROR(EINVAL);
    }

    if (ret != VK_NOT_READY && ret != VK_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Unable to perform query: %s!\n",
               ff_vk_ret2str(ret));
        return AVERROR_EXTERNAL;
    }

    if (query_data[2] != VK_QUERY_RESULT_STATUS_COMPLETE_KHR) {
        av_log(avctx, AV_LOG_ERROR, "Unable to encode: %u\n", query_data[2]);
        return AVERROR_EXTERNAL;
    }

    /* Invalidate buffer if needed */
    if (!(sd_buf->flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        FFVulkanFunctions *vk = &ctx->s.vkfn;
        VkMappedMemoryRange invalidate_buf;

        int offs = vp->slices_offset;
        /* If the non-coherent alignment is greater than the bitstream buffer
         * offset's alignment, and the offs value is not aligned already,
         * align it to the previous alignment point. */
        if (ctx->s.props.properties.limits.nonCoherentAtomSize >
            ctx->caps.minBitstreamBufferOffsetAlignment && offs &&
            (FFALIGN(offs, ctx->s.props.properties.limits.nonCoherentAtomSize) != offs)) {
            offs -= ctx->s.props.properties.limits.nonCoherentAtomSize;
            offs = FFALIGN(FFMAX(offs, 0), ctx->s.props.properties.limits.nonCoherentAtomSize);
        }

        invalidate_buf = (VkMappedMemoryRange) {
            .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .memory = sd_buf->mem,
            .offset = offs,
            .size = VK_WHOLE_SIZE,
        };

        vk->FlushMappedMemoryRanges(ctx->s.hwctx->act_dev, 1, &invalidate_buf);
    }

    pkt->data = sd_buf->mapped_mem;
    pkt->size = vp->slices_offset + /* base offset */
                query_data[0]       /* secondary offset */ +
                query_data[1]       /* size */;

    /* Move reference */
    pkt->buf = vp->pkt_buf;
    vp->pkt_buf = NULL;

    av_log(avctx, AV_LOG_DEBUG, "Frame %"PRId64"/%"PRId64 " encoded\n",
           base_pic->display_order, base_pic->encode_order);

    return ff_hw_base_encode_set_output_property(&ctx->base, avctx,
                                                 base_pic, pkt,
                                                 ctx->codec->flags & VK_ENC_FLAG_NO_DELAY);
}

static const FFHWEncodePictureOperation vulkan_base_encode_ops = {
    .priv_size = sizeof(FFVulkanEncodePicture),
    .init   = &vulkan_encode_init,
    .issue  = &vulkan_encode_issue,
    .output = &vulkan_encode_output,
    .free   = &vulkan_encode_free,
};

int ff_vulkan_encode_receive_packet(AVCodecContext *avctx, AVPacket *pkt)
{
    FFVulkanEncodeContext *ctx = avctx->priv_data;
    return ff_hw_base_encode_receive_packet(&ctx->base, avctx, pkt);
}

static int vulkan_encode_create_dpb(AVCodecContext *avctx, FFVulkanEncodeContext *ctx)
{
    int err;
    FFHWBaseEncodeContext *base_ctx = &ctx->base;
    AVVulkanFramesContext *hwfc;

    enum AVPixelFormat dpb_format;
    err = ff_hw_base_get_recon_format(base_ctx, NULL, &dpb_format);
    if (err < 0)
        return err;

    base_ctx->recon_frames_ref = av_hwframe_ctx_alloc(base_ctx->device_ref);
    if (!base_ctx->recon_frames_ref)
        return AVERROR(ENOMEM);

    base_ctx->recon_frames = (AVHWFramesContext *)base_ctx->recon_frames_ref->data;
    hwfc = (AVVulkanFramesContext *)base_ctx->recon_frames->hwctx;

    base_ctx->recon_frames->format    = AV_PIX_FMT_VULKAN;
    base_ctx->recon_frames->sw_format = dpb_format;
    base_ctx->recon_frames->width     = avctx->width;
    base_ctx->recon_frames->height    = avctx->height;

    hwfc->format[0]    = ctx->pic_format;
    hwfc->create_pnext = &ctx->profile_list;
    hwfc->tiling       = VK_IMAGE_TILING_OPTIMAL;
    hwfc->usage        = VK_IMAGE_USAGE_SAMPLED_BIT              |
                         VK_IMAGE_USAGE_VIDEO_ENCODE_DPB_BIT_KHR;

    if (ctx->common.layered_dpb)
        hwfc->nb_layers = ctx->caps.maxDpbSlots;

    err = av_hwframe_ctx_init(base_ctx->recon_frames_ref);
    if (err < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to initialise DPB frame context: %s\n",
               av_err2str(err));
        return err;
    }

    if (ctx->common.layered_dpb) {
        ctx->common.layered_frame = av_frame_alloc();
        if (!ctx->common.layered_frame)
            return AVERROR(ENOMEM);

        err = av_hwframe_get_buffer(base_ctx->recon_frames_ref,
                                    ctx->common.layered_frame, 0);
        if (err < 0)
            return AVERROR(ENOMEM);

        err = ff_vk_create_view(&ctx->s, &ctx->common,
                                &ctx->common.layered_view,
                                &ctx->common.layered_aspect,
                                (AVVkFrame *)ctx->common.layered_frame->data[0],
                                hwfc->format[0], 1);
        if (err < 0)
            return err;

        av_buffer_unref(&base_ctx->recon_frames_ref);
    }

    return 0;
}

static av_cold int init_rc(AVCodecContext *avctx, FFVulkanEncodeContext *ctx)
{
    if (ctx->opts.qp) {
        ctx->explicit_qp = ctx->opts.qp;
    } else if (avctx->global_quality > 0) {
        if (avctx->flags & AV_CODEC_FLAG_QSCALE)
            ctx->explicit_qp = avctx->global_quality / FF_QP2LAMBDA;
        else
            ctx->explicit_qp = avctx->global_quality;
    }

    if (ctx->opts.rc_mode == FF_VK_RC_MODE_AUTO) {
        if (ctx->explicit_qp >= 0) {
            ctx->opts.rc_mode = VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR;
        } else if (avctx->global_quality > 0) {
            if (avctx->flags & AV_CODEC_FLAG_QSCALE)
                ctx->explicit_qp = avctx->global_quality / FF_QP2LAMBDA;
            else
                ctx->explicit_qp = avctx->global_quality;
            ctx->opts.rc_mode = VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR;
        } else if (avctx->bit_rate) {
            if (ctx->enc_caps.rateControlModes & VK_VIDEO_ENCODE_RATE_CONTROL_MODE_VBR_BIT_KHR)
                ctx->opts.rc_mode = VK_VIDEO_ENCODE_RATE_CONTROL_MODE_VBR_BIT_KHR;
            else if (ctx->enc_caps.rateControlModes & VK_VIDEO_ENCODE_RATE_CONTROL_MODE_CBR_BIT_KHR)
                ctx->opts.rc_mode = VK_VIDEO_ENCODE_RATE_CONTROL_MODE_CBR_BIT_KHR;
            else
                ctx->opts.rc_mode = VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DEFAULT_KHR;
        } else {
            ctx->explicit_qp = 18;
            ctx->opts.rc_mode = VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR;
            av_log(avctx, AV_LOG_WARNING, "No rate control settings specified, using fixed QP = %i\n",
                   ctx->explicit_qp);
        }
    } else if (ctx->opts.rc_mode != VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR &&
               !avctx->bit_rate) {
        av_log(avctx, AV_LOG_WARNING, "No bitrate specified!\n");
        return AVERROR(EINVAL);
    }

    if (ctx->opts.rc_mode && !(ctx->enc_caps.rateControlModes & ctx->opts.rc_mode)) {
        static const char *rc_modes[] = {
            [VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DEFAULT_KHR] = "default",
            [VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR] = "cqp",
            [VK_VIDEO_ENCODE_RATE_CONTROL_MODE_CBR_BIT_KHR] = "cbr",
            [VK_VIDEO_ENCODE_RATE_CONTROL_MODE_VBR_BIT_KHR] = "vbr",
        };
        av_log(avctx, AV_LOG_ERROR, "Unsupported rate control mode %s, supported are:\n",
               rc_modes[FFMIN(FF_ARRAY_ELEMS(rc_modes), ctx->opts.rc_mode)]);
        av_log(avctx, AV_LOG_ERROR, "    %s\n", rc_modes[0]);
        for (int i = VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR;
                 i <= VK_VIDEO_ENCODE_RATE_CONTROL_MODE_VBR_BIT_KHR; i <<= 1) {
            if (!(ctx->enc_caps.rateControlModes & i))
                continue;
            av_log(avctx, AV_LOG_ERROR, "    %s\n", rc_modes[i]);
        }
        return AVERROR(ENOTSUP);
    }

    return 0;
}

av_cold int ff_vulkan_write_global_header(AVCodecContext *avctx,
                                          FFVulkanEncodeContext *ctx)
{
    int err;

    /* Write extradata if needed */
    if (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) {
        uint8_t data[4096];
        size_t data_len = sizeof(data);

        err = ctx->codec->write_sequence_headers(avctx, NULL, data, &data_len);
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

    return 0;
}

av_cold int ff_vulkan_encode_init(AVCodecContext *avctx, FFVulkanEncodeContext *ctx,
                                  const FFVulkanEncodeDescriptor *vk_desc,
                                  const FFVulkanCodec *codec,
                                  void *codec_caps, void *quality_pnext)
{
    int i, err;
    VkResult ret;
    FFVulkanFunctions *vk = &ctx->s.vkfn;
    FFVulkanContext *s = &ctx->s;
    FFHWBaseEncodeContext *base_ctx = &ctx->base;

    const AVPixFmtDescriptor *desc;

    VkVideoFormatPropertiesKHR *ret_info;
    uint32_t nb_out_fmts = 0;

    VkPhysicalDeviceVideoEncodeQualityLevelInfoKHR quality_info;

    VkQueryPoolVideoEncodeFeedbackCreateInfoKHR query_create;

    VkVideoSessionCreateInfoKHR session_create = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_CREATE_INFO_KHR,
    };
    VkPhysicalDeviceVideoFormatInfoKHR fmt_info = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_FORMAT_INFO_KHR,
        .pNext = &ctx->profile_list,
    };

    if (!avctx->hw_frames_ctx) {
        av_log(avctx, AV_LOG_ERROR, "A hardware frames reference is "
               "required to associate the encoding device.\n");
        return AVERROR(EINVAL);
    }

    ctx->base.op = &vulkan_base_encode_ops;
    ctx->codec = codec;

    s->frames_ref = av_buffer_ref(avctx->hw_frames_ctx);
    s->frames = (AVHWFramesContext *)s->frames_ref->data;
    s->hwfc = s->frames->hwctx;

    s->device = (AVHWDeviceContext *)s->frames->device_ref->data;
    s->hwctx = s->device->hwctx;

    desc = av_pix_fmt_desc_get(avctx->sw_pix_fmt);
    if (!desc)
        return AVERROR(EINVAL);

    s->extensions = ff_vk_extensions_to_mask(s->hwctx->enabled_dev_extensions,
                                             s->hwctx->nb_enabled_dev_extensions);

    if (!(s->extensions & FF_VK_EXT_VIDEO_ENCODE_QUEUE)) {
        av_log(avctx, AV_LOG_ERROR, "Device does not support the %s extension!\n",
               VK_KHR_VIDEO_ENCODE_QUEUE_EXTENSION_NAME);
        return AVERROR(ENOSYS);
    } else if (!(s->extensions & FF_VK_EXT_VIDEO_MAINTENANCE_1)) {
        av_log(avctx, AV_LOG_ERROR, "Device does not support the %s extension!\n",
               VK_KHR_VIDEO_MAINTENANCE_1_EXTENSION_NAME);
        return AVERROR(ENOSYS);
    } else if (!(s->extensions & vk_desc->encode_extension)) {
        av_log(avctx, AV_LOG_ERROR, "Device does not support encoding %s!\n",
               avcodec_get_name(avctx->codec_id));
        return AVERROR(ENOSYS);
    }

    /* Load functions */
    err = ff_vk_load_functions(s->device, vk, s->extensions, 1, 1);
    if (err < 0)
        return err;

    /* Create queue context */
    err = ff_vk_video_qf_init(s, &ctx->qf_enc,
                              VK_QUEUE_VIDEO_ENCODE_BIT_KHR,
                              vk_desc->encode_op);
    if (err < 0) {
        av_log(avctx, AV_LOG_ERROR, "Encoding of %s is not supported by this device\n",
               avcodec_get_name(avctx->codec_id));
        return err;
    }

    /* Load all properties */
    err = ff_vk_load_props(s);
    if (err < 0)
        return err;

    /* Set tuning */
    ctx->usage_info = (VkVideoEncodeUsageInfoKHR) {
        .sType             = VK_STRUCTURE_TYPE_VIDEO_ENCODE_USAGE_INFO_KHR,
        .videoUsageHints   = ctx->opts.usage,
        .videoContentHints = ctx->opts.content,
        .tuningMode        = ctx->opts.tune,
    };

    /* Load up the profile now, needed for caps and to create a query pool */
    ctx->profile.sType               = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR;
    ctx->profile.pNext               = &ctx->usage_info;
    ctx->profile.videoCodecOperation = vk_desc->encode_op;
    ctx->profile.chromaSubsampling   = ff_vk_subsampling_from_av_desc(desc);
    ctx->profile.lumaBitDepth        = ff_vk_depth_from_av_depth(desc->comp[0].depth);
    ctx->profile.chromaBitDepth      = ctx->profile.lumaBitDepth;

    /* Setup a profile */
    err = codec->init_profile(avctx, &ctx->profile, &ctx->usage_info);
    if (err < 0)
        return err;

    ctx->profile_list.sType        = VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR;
    ctx->profile_list.profileCount = 1;
    ctx->profile_list.pProfiles    = &ctx->profile;

    /* Get the capabilities of the encoder for the given profile */
    ctx->enc_caps.sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_CAPABILITIES_KHR;
    ctx->enc_caps.pNext = codec_caps;
    ctx->caps.sType = VK_STRUCTURE_TYPE_VIDEO_CAPABILITIES_KHR;
    ctx->caps.pNext = &ctx->enc_caps;

    ret = vk->GetPhysicalDeviceVideoCapabilitiesKHR(s->hwctx->phys_dev,
                                                    &ctx->profile,
                                                    &ctx->caps);
    if (ret == VK_ERROR_VIDEO_PROFILE_OPERATION_NOT_SUPPORTED_KHR) {
        av_log(avctx, AV_LOG_ERROR, "Unable to initialize encoding: "
               "%s profile \"%s\" not supported!\n",
               avcodec_get_name(avctx->codec_id),
               avcodec_profile_name(avctx->codec_id, avctx->profile));
        return AVERROR(EINVAL);
    } else if (ret == VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR) {
        av_log(avctx, AV_LOG_ERROR, "Unable to initialize encoding: "
               "format (%s) not supported!\n",
               av_get_pix_fmt_name(avctx->sw_pix_fmt));
        return AVERROR(EINVAL);
    } else if (ret == VK_ERROR_FEATURE_NOT_PRESENT ||
               ret == VK_ERROR_FORMAT_NOT_SUPPORTED) {
        return AVERROR(EINVAL);
    } else if (ret != VK_SUCCESS) {
        return AVERROR_EXTERNAL;
    }

    err = init_rc(avctx, ctx);
    if (err < 0)
        return err;

    /* Create command and query pool */
    query_create = (VkQueryPoolVideoEncodeFeedbackCreateInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_QUERY_POOL_VIDEO_ENCODE_FEEDBACK_CREATE_INFO_KHR,
        .pNext = &ctx->profile,
        .encodeFeedbackFlags = ctx->enc_caps.supportedEncodeFeedbackFlags &
                               (~VK_VIDEO_ENCODE_FEEDBACK_BITSTREAM_HAS_OVERRIDES_BIT_KHR),
    };
    err = ff_vk_exec_pool_init(s, &ctx->qf_enc, &ctx->enc_pool, base_ctx->async_depth,
                               1, VK_QUERY_TYPE_VIDEO_ENCODE_FEEDBACK_KHR, 0,
                               &query_create);
    if (err < 0)
        return err;

    if (ctx->opts.quality > ctx->enc_caps.maxQualityLevels) {
        av_log(avctx, AV_LOG_ERROR, "Invalid quality level %i: allowed range is "
                                    "0 to %i\n",
               ctx->opts.quality, ctx->enc_caps.maxQualityLevels);
        return AVERROR(EINVAL);
    }

    /* Get quality properties for the profile and quality level */
    quality_info = (VkPhysicalDeviceVideoEncodeQualityLevelInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_ENCODE_QUALITY_LEVEL_INFO_KHR,
        .pVideoProfile = &ctx->profile,
        .qualityLevel = ctx->opts.quality,
    };
    ctx->quality_props = (VkVideoEncodeQualityLevelPropertiesKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_QUALITY_LEVEL_PROPERTIES_KHR,
        .pNext = quality_pnext,
    };
    ret = vk->GetPhysicalDeviceVideoEncodeQualityLevelPropertiesKHR(s->hwctx->phys_dev,
                                                                    &quality_info,
                                                                    &ctx->quality_props);
    if (ret != VK_SUCCESS)
        return AVERROR_EXTERNAL;

    /* Printout informative properties */
    av_log(avctx, AV_LOG_VERBOSE, "Encoder capabilities for %s profile \"%s\":\n",
           avcodec_get_name(avctx->codec_id),
           avcodec_profile_name(avctx->codec_id, avctx->profile));
    av_log(avctx, AV_LOG_VERBOSE, "    Width: from %i to %i\n",
           ctx->caps.minCodedExtent.width, ctx->caps.maxCodedExtent.width);
    av_log(avctx, AV_LOG_VERBOSE, "    Height: from %i to %i\n",
           ctx->caps.minCodedExtent.height, ctx->caps.maxCodedExtent.height);
    av_log(avctx, AV_LOG_VERBOSE, "    Width alignment: %i\n",
           ctx->caps.pictureAccessGranularity.width);
    av_log(avctx, AV_LOG_VERBOSE, "    Height alignment: %i\n",
           ctx->caps.pictureAccessGranularity.height);
    av_log(avctx, AV_LOG_VERBOSE, "    Bitstream offset alignment: %"PRIu64"\n",
           ctx->caps.minBitstreamBufferOffsetAlignment);
    av_log(avctx, AV_LOG_VERBOSE, "    Bitstream size alignment: %"PRIu64"\n",
           ctx->caps.minBitstreamBufferSizeAlignment);
    av_log(avctx, AV_LOG_VERBOSE, "    Maximum references: %u\n",
           ctx->caps.maxDpbSlots);
    av_log(avctx, AV_LOG_VERBOSE, "    Maximum active references: %u\n",
           ctx->caps.maxActiveReferencePictures);
    av_log(avctx, AV_LOG_VERBOSE, "    Codec header version: %i.%i.%i (driver), %i.%i.%i (compiled)\n",
           CODEC_VER(ctx->caps.stdHeaderVersion.specVersion),
           CODEC_VER(vk_desc->ext_props.specVersion));
    av_log(avctx, AV_LOG_VERBOSE, "    Encoder max quality: %i\n",
           ctx->enc_caps.maxQualityLevels);
    av_log(avctx, AV_LOG_VERBOSE, "    Encoder image width alignment: %i\n",
           ctx->enc_caps.encodeInputPictureGranularity.width);
    av_log(avctx, AV_LOG_VERBOSE, "    Encoder image height alignment: %i\n",
           ctx->enc_caps.encodeInputPictureGranularity.height);
    av_log(avctx, AV_LOG_VERBOSE, "    Capability flags:%s%s%s\n",
           ctx->caps.flags ? "" :
               " none",
           ctx->caps.flags & VK_VIDEO_CAPABILITY_PROTECTED_CONTENT_BIT_KHR ?
               " protected" : "",
           ctx->caps.flags & VK_VIDEO_CAPABILITY_SEPARATE_REFERENCE_IMAGES_BIT_KHR ?
               " separate_references" : "");

    /* Setup width/height alignment */
    base_ctx->surface_width = avctx->coded_width =
        FFALIGN(avctx->width, ctx->enc_caps.encodeInputPictureGranularity.width);
    base_ctx->surface_height = avctx->coded_height =
        FFALIGN(avctx->height, ctx->enc_caps.encodeInputPictureGranularity.height);

    /* Setup slice width/height */
    base_ctx->slice_block_width = ctx->enc_caps.encodeInputPictureGranularity.width;
    base_ctx->slice_block_height = ctx->enc_caps.encodeInputPictureGranularity.height;

    /* Check if encoding is possible with the given parameters */
    if (avctx->coded_width  < ctx->caps.minCodedExtent.width   ||
        avctx->coded_height < ctx->caps.minCodedExtent.height  ||
        avctx->coded_width  > ctx->caps.maxCodedExtent.width   ||
        avctx->coded_height > ctx->caps.maxCodedExtent.height) {
        av_log(avctx, AV_LOG_ERROR, "Input of %ix%i too large for encoder limits: %ix%i max\n",
               avctx->coded_width, avctx->coded_height,
               ctx->caps.minCodedExtent.width, ctx->caps.minCodedExtent.height);
        return AVERROR(EINVAL);
    }

    fmt_info.imageUsage = VK_IMAGE_USAGE_VIDEO_ENCODE_DPB_BIT_KHR |
                          VK_IMAGE_USAGE_VIDEO_ENCODE_DST_BIT_KHR;

    ctx->common.layered_dpb = !(ctx->caps.flags & VK_VIDEO_CAPABILITY_SEPARATE_REFERENCE_IMAGES_BIT_KHR);

    /* Get the supported image formats */
    ret = vk->GetPhysicalDeviceVideoFormatPropertiesKHR(s->hwctx->phys_dev,
                                                        &fmt_info,
                                                        &nb_out_fmts, NULL);
    if (ret == VK_ERROR_FORMAT_NOT_SUPPORTED ||
        (!nb_out_fmts && ret == VK_SUCCESS)) {
        return AVERROR(EINVAL);
    } else if (ret != VK_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Unable to get Vulkan format properties: %s!\n",
               ff_vk_ret2str(ret));
        return AVERROR_EXTERNAL;
    }

    ret_info = av_mallocz(sizeof(*ret_info)*nb_out_fmts);
    if (!ret_info)
        return AVERROR(ENOMEM);

    for (int i = 0; i < nb_out_fmts; i++)
        ret_info[i].sType = VK_STRUCTURE_TYPE_VIDEO_FORMAT_PROPERTIES_KHR;

    ret = vk->GetPhysicalDeviceVideoFormatPropertiesKHR(s->hwctx->phys_dev,
                                                        &fmt_info,
                                                        &nb_out_fmts, ret_info);
    if (ret == VK_ERROR_FORMAT_NOT_SUPPORTED ||
        (!nb_out_fmts && ret == VK_SUCCESS)) {
        av_free(ret_info);
        return AVERROR(EINVAL);
    } else if (ret != VK_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Unable to get Vulkan format properties: %s!\n",
               ff_vk_ret2str(ret));
        av_free(ret_info);
        return AVERROR_EXTERNAL;
    }

    av_log(avctx, AV_LOG_VERBOSE, "Supported input formats:\n");
    for (i = 0; i < nb_out_fmts; i++)
        av_log(avctx, AV_LOG_VERBOSE, "    %i: %i\n", i, ret_info[i].format);

    for (i = 0; i < nb_out_fmts; i++) {
        if (ff_vk_pix_fmt_from_vkfmt(ret_info[i].format) == s->frames->sw_format) {
            ctx->pic_format = ret_info[i].format;
            break;
        }
    }

    av_free(ret_info);

    if (i == nb_out_fmts) {
        av_log(avctx, AV_LOG_ERROR, "Pixel format %s of input frames not supported!\n",
               av_get_pix_fmt_name(s->frames->sw_format));
        return AVERROR(EINVAL);
    }

    /* Create session */
    session_create.pVideoProfile = &ctx->profile;
    session_create.flags = 0x0;
    session_create.queueFamilyIndex = ctx->qf_enc.queue_family;
    session_create.maxCodedExtent = ctx->caps.maxCodedExtent;
    session_create.maxDpbSlots = ctx->caps.maxDpbSlots;
    session_create.maxActiveReferencePictures = ctx->caps.maxActiveReferencePictures;
    session_create.pictureFormat = ctx->pic_format;
    session_create.referencePictureFormat = session_create.pictureFormat;
    session_create.pStdHeaderVersion = &vk_desc->ext_props;

    err = ff_vk_video_common_init(avctx, s, &ctx->common, &session_create);
    if (err < 0)
        return err;

    err = ff_hw_base_encode_init(avctx, &ctx->base);
    if (err < 0)
        return err;

    err = vulkan_encode_create_dpb(avctx, ctx);
    if (err < 0)
        return err;

    base_ctx->async_encode = 1;
    base_ctx->encode_fifo = av_fifo_alloc2(base_ctx->async_depth,
                                           sizeof(FFVulkanEncodePicture *), 0);
    if (!base_ctx->encode_fifo)
        return AVERROR(ENOMEM);

    return 0;
}

int ff_vulkan_encode_create_session_params(AVCodecContext *avctx, FFVulkanEncodeContext *ctx,
                                           void *codec_params_pnext)
{
    VkResult ret;
    FFVulkanFunctions *vk = &ctx->s.vkfn;
    FFVulkanContext *s = &ctx->s;

    VkVideoEncodeQualityLevelInfoKHR q_info;
    VkVideoSessionParametersCreateInfoKHR session_params_create;

    q_info = (VkVideoEncodeQualityLevelInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_QUALITY_LEVEL_INFO_KHR,
        .pNext = codec_params_pnext,
        .qualityLevel = ctx->opts.quality,
    };
    session_params_create = (VkVideoSessionParametersCreateInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_PARAMETERS_CREATE_INFO_KHR,
        .pNext = &q_info,
        .videoSession = ctx->common.session,
        .videoSessionParametersTemplate = VK_NULL_HANDLE,
    };

    /* Create session parameters */
    ret = vk->CreateVideoSessionParametersKHR(s->hwctx->act_dev, &session_params_create,
                                              s->hwctx->alloc, &ctx->session_params);
    if (ret != VK_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Unable to create Vulkan video session parameters: %s!\n",
               ff_vk_ret2str(ret));
        return AVERROR_EXTERNAL;
    }

    return 0;
}
