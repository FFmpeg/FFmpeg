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

#include "vulkan_video.h"
#include "vulkan_decode.h"
#include "config_components.h"

#if CONFIG_H264_VULKAN_HWACCEL
extern const VkExtensionProperties ff_vk_dec_h264_ext;
#endif
#if CONFIG_HEVC_VULKAN_HWACCEL
extern const VkExtensionProperties ff_vk_dec_hevc_ext;
#endif
#if CONFIG_AV1_VULKAN_HWACCEL
extern const VkExtensionProperties ff_vk_dec_av1_ext;
#endif

static const VkExtensionProperties *dec_ext[] = {
#if CONFIG_H264_VULKAN_HWACCEL
    [AV_CODEC_ID_H264] = &ff_vk_dec_h264_ext,
#endif
#if CONFIG_HEVC_VULKAN_HWACCEL
    [AV_CODEC_ID_HEVC] = &ff_vk_dec_hevc_ext,
#endif
#if CONFIG_AV1_VULKAN_HWACCEL
    [AV_CODEC_ID_AV1] = &ff_vk_dec_av1_ext,
#endif
};

int ff_vk_update_thread_context(AVCodecContext *dst, const AVCodecContext *src)
{
    int err;
    FFVulkanDecodeContext *src_ctx = src->internal->hwaccel_priv_data;
    FFVulkanDecodeContext *dst_ctx = dst->internal->hwaccel_priv_data;

    err = av_buffer_replace(&dst_ctx->shared_ref, src_ctx->shared_ref);
    if (err < 0)
        return err;

    if (src_ctx->session_params) {
        err = av_buffer_replace(&dst_ctx->session_params, src_ctx->session_params);
        if (err < 0)
            return err;
    }

    dst_ctx->dedicated_dpb = src_ctx->dedicated_dpb;
    dst_ctx->layered_dpb = src_ctx->layered_dpb;
    dst_ctx->external_fg = src_ctx->external_fg;
    dst_ctx->frame_id_alloc_mask = src_ctx->frame_id_alloc_mask;

    return 0;
}

int ff_vk_params_invalidate(AVCodecContext *avctx, int t, const uint8_t *b, uint32_t s)
{
    FFVulkanDecodeContext *dec = avctx->internal->hwaccel_priv_data;
    av_buffer_unref(&dec->session_params);
    return 0;
}

static int vk_decode_create_view(FFVulkanDecodeShared *ctx, VkImageView *dst_view,
                                 VkImageAspectFlags *aspect, AVVkFrame *src,
                                 VkFormat vkf)
{
    VkResult ret;
    FFVulkanFunctions *vk = &ctx->s.vkfn;
    VkImageAspectFlags aspect_mask = ff_vk_aspect_bits_from_vkfmt(vkf);

    VkSamplerYcbcrConversionInfo yuv_sampler_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO,
        .conversion = ctx->yuv_sampler,
    };
    VkImageViewCreateInfo img_view_create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = &yuv_sampler_info,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = vkf,
        .image = src->img[0],
        .components = (VkComponentMapping) {
            .r = VK_COMPONENT_SWIZZLE_IDENTITY,
            .g = VK_COMPONENT_SWIZZLE_IDENTITY,
            .b = VK_COMPONENT_SWIZZLE_IDENTITY,
            .a = VK_COMPONENT_SWIZZLE_IDENTITY,
        },
        .subresourceRange = (VkImageSubresourceRange) {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseArrayLayer = 0,
            .layerCount     = VK_REMAINING_ARRAY_LAYERS,
            .levelCount     = 1,
        },
    };

    ret = vk->CreateImageView(ctx->s.hwctx->act_dev, &img_view_create_info,
                              ctx->s.hwctx->alloc, dst_view);
    if (ret != VK_SUCCESS)
        return AVERROR_EXTERNAL;

    *aspect = aspect_mask;

    return 0;
}

static AVFrame *vk_get_dpb_pool(FFVulkanDecodeShared *ctx)
{
    int err;
    AVFrame *avf = av_frame_alloc();
    if (!avf)
        return NULL;

    err = av_hwframe_get_buffer(ctx->dpb_hwfc_ref, avf, 0x0);
    if (err < 0)
        av_frame_free(&avf);

    return avf;
}

int ff_vk_decode_prepare_frame(FFVulkanDecodeContext *dec, AVFrame *pic,
                               FFVulkanDecodePicture *vkpic, int is_current,
                               int alloc_dpb)
{
    int err;
    FFVulkanDecodeShared *ctx = (FFVulkanDecodeShared *)dec->shared_ref->data;

    vkpic->slices_size = 0;

    /* If the decoder made a blank frame to make up for a missing ref, or the
     * frame is the current frame so it's missing one, create a re-representation */
    if (vkpic->img_view_ref)
        return 0;

    vkpic->dpb_frame     = NULL;
    vkpic->img_view_ref  = NULL;
    vkpic->img_view_out  = NULL;
    vkpic->img_view_dest = NULL;

    if (dec->layered_dpb && alloc_dpb) {
        vkpic->img_view_ref = ctx->layered_view;
        vkpic->img_aspect_ref = ctx->layered_aspect;
    } else if (alloc_dpb) {
        AVHWFramesContext *dpb_frames = (AVHWFramesContext *)ctx->dpb_hwfc_ref->data;
        AVVulkanFramesContext *dpb_hwfc = dpb_frames->hwctx;

        vkpic->dpb_frame = vk_get_dpb_pool(ctx);
        if (!vkpic->dpb_frame)
            return AVERROR(ENOMEM);

        err = vk_decode_create_view(ctx, &vkpic->img_view_ref,
                                    &vkpic->img_aspect_ref,
                                    (AVVkFrame *)vkpic->dpb_frame->data[0],
                                    dpb_hwfc->format[0]);
        if (err < 0)
            return err;

        vkpic->img_view_dest = vkpic->img_view_ref;
    }

    if (!alloc_dpb || is_current) {
        AVHWFramesContext *frames = (AVHWFramesContext *)pic->hw_frames_ctx->data;
        AVVulkanFramesContext *hwfc = frames->hwctx;

        err = vk_decode_create_view(ctx, &vkpic->img_view_out,
                                    &vkpic->img_aspect,
                                    (AVVkFrame *)pic->data[0],
                                    hwfc->format[0]);
        if (err < 0)
            return err;

        if (!alloc_dpb) {
            vkpic->img_view_ref = vkpic->img_view_out;
            vkpic->img_aspect_ref = vkpic->img_aspect;
        }
    }

    return 0;
}

int ff_vk_decode_add_slice(AVCodecContext *avctx, FFVulkanDecodePicture *vp,
                           const uint8_t *data, size_t size, int add_startcode,
                           uint32_t *nb_slices, const uint32_t **offsets)
{
    FFVulkanDecodeContext *dec = avctx->internal->hwaccel_priv_data;
    FFVulkanDecodeShared *ctx = (FFVulkanDecodeShared *)dec->shared_ref->data;

    static const uint8_t startcode_prefix[3] = { 0x0, 0x0, 0x1 };
    const size_t startcode_len = add_startcode ? sizeof(startcode_prefix) : 0;
    const int nb = *nb_slices;
    uint8_t *slices;
    uint32_t *slice_off;
    FFVkVideoBuffer *vkbuf;

    size_t new_size = vp->slices_size + startcode_len + size +
                      ctx->caps.minBitstreamBufferSizeAlignment;
    new_size = FFALIGN(new_size, ctx->caps.minBitstreamBufferSizeAlignment);

    slice_off = av_fast_realloc(dec->slice_off, &dec->slice_off_max,
                                (nb + 1)*sizeof(slice_off));
    if (!slice_off)
        return AVERROR(ENOMEM);

    *offsets = dec->slice_off = slice_off;
    slice_off[nb] = vp->slices_size;

    vkbuf = vp->slices_buf ? (FFVkVideoBuffer *)vp->slices_buf->data : NULL;
    if (!vkbuf || vkbuf->buf.size < new_size) {
        int err;
        AVBufferRef *new_ref;
        FFVkVideoBuffer *new_buf;
        err = ff_vk_video_get_buffer(&ctx->s, &ctx->common, &new_ref,
                                     VK_BUFFER_USAGE_VIDEO_DECODE_SRC_BIT_KHR,
                                     ctx->s.hwfc->create_pnext, new_size);
        if (err < 0)
            return err;

        new_buf = (FFVkVideoBuffer *)new_ref->data;

        /* Copy data from the old buffer */
        if (vkbuf) {
            memcpy(new_buf->mem, vkbuf->mem, vp->slices_size);
            av_buffer_unref(&vp->slices_buf);
        }

        vp->slices_buf = new_ref;
        vkbuf = new_buf;
    }
    slices = vkbuf->mem;

    /* Startcode */
    memcpy(slices + vp->slices_size, startcode_prefix, startcode_len);

    /* Slice data */
    memcpy(slices + vp->slices_size + startcode_len, data, size);

    *nb_slices = nb + 1;
    vp->slices_size += startcode_len + size;

    return 0;
}

void ff_vk_decode_flush(AVCodecContext *avctx)
{
    FFVulkanDecodeContext *dec = avctx->internal->hwaccel_priv_data;
    FFVulkanDecodeShared *ctx = (FFVulkanDecodeShared *)dec->shared_ref->data;

    FFVulkanFunctions *vk = &ctx->s.vkfn;
    VkVideoBeginCodingInfoKHR decode_start = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_BEGIN_CODING_INFO_KHR,
        .videoSession = ctx->common.session,
        .videoSessionParameters = ctx->empty_session_params,
    };
    VkVideoCodingControlInfoKHR decode_ctrl = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_CODING_CONTROL_INFO_KHR,
        .flags = VK_VIDEO_CODING_CONTROL_RESET_BIT_KHR,
    };
    VkVideoEndCodingInfoKHR decode_end = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_END_CODING_INFO_KHR,
    };

    VkCommandBuffer cmd_buf;
    FFVkExecContext *exec = ff_vk_exec_get(&ctx->exec_pool);
    ff_vk_exec_start(&ctx->s, exec);
    cmd_buf = exec->buf;

    vk->CmdBeginVideoCodingKHR(cmd_buf, &decode_start);
    vk->CmdControlVideoCodingKHR(cmd_buf, &decode_ctrl);
    vk->CmdEndVideoCodingKHR(cmd_buf, &decode_end);
    ff_vk_exec_submit(&ctx->s, exec);
}

int ff_vk_decode_frame(AVCodecContext *avctx,
                       AVFrame *pic,    FFVulkanDecodePicture *vp,
                       AVFrame *rpic[], FFVulkanDecodePicture *rvkp[])
{
    int err;
    VkResult ret;
    VkCommandBuffer cmd_buf;
    FFVkVideoBuffer *sd_buf;

    FFVulkanDecodeContext *dec = avctx->internal->hwaccel_priv_data;
    FFVulkanDecodeShared *ctx = (FFVulkanDecodeShared *)dec->shared_ref->data;
    FFVulkanFunctions *vk = &ctx->s.vkfn;

    /* Output */
    AVVkFrame *vkf = (AVVkFrame *)pic->buf[0]->data;

    /* Quirks */
    const int layered_dpb = dec->layered_dpb;

    VkVideoSessionParametersKHR *par = (VkVideoSessionParametersKHR *)dec->session_params->data;
    VkVideoBeginCodingInfoKHR decode_start = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_BEGIN_CODING_INFO_KHR,
        .videoSession = ctx->common.session,
        .videoSessionParameters = *par,
        .referenceSlotCount = vp->decode_info.referenceSlotCount,
        .pReferenceSlots = vp->decode_info.pReferenceSlots,
    };
    VkVideoEndCodingInfoKHR decode_end = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_END_CODING_INFO_KHR,
    };

    VkImageMemoryBarrier2 img_bar[37];
    int nb_img_bar = 0;
    size_t data_size = FFALIGN(vp->slices_size,
                               ctx->caps.minBitstreamBufferSizeAlignment);

    FFVkExecContext *exec = ff_vk_exec_get(&ctx->exec_pool);

    /* The current decoding reference has to be bound as an inactive reference */
    VkVideoReferenceSlotInfoKHR *cur_vk_ref;
    cur_vk_ref = (void *)&decode_start.pReferenceSlots[decode_start.referenceSlotCount];
    cur_vk_ref[0] = vp->ref_slot;
    cur_vk_ref[0].slotIndex = -1;
    decode_start.referenceSlotCount++;

    if (ctx->exec_pool.nb_queries) {
        int64_t prev_sub_res = 0;
        ff_vk_exec_wait(&ctx->s, exec);
        ret = ff_vk_exec_get_query(&ctx->s, exec, NULL, &prev_sub_res);
        if (ret != VK_NOT_READY && ret != VK_SUCCESS) {
            av_log(avctx, AV_LOG_ERROR, "Unable to perform query: %s!\n",
                   ff_vk_ret2str(ret));
            return AVERROR_EXTERNAL;
        }

        if (ret == VK_SUCCESS)
            av_log(avctx, prev_sub_res < 0 ? AV_LOG_ERROR : AV_LOG_DEBUG,
                   "Result of previous frame decoding: %"PRId64"\n", prev_sub_res);
    }

    sd_buf = (FFVkVideoBuffer *)vp->slices_buf->data;

    /* Flush if needed */
    if (!(sd_buf->buf.flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        VkMappedMemoryRange flush_buf = {
            .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .memory = sd_buf->buf.mem,
            .offset = 0,
            .size = FFALIGN(vp->slices_size,
                            ctx->s.props.properties.limits.nonCoherentAtomSize),
        };

        ret = vk->FlushMappedMemoryRanges(ctx->s.hwctx->act_dev, 1, &flush_buf);
        if (ret != VK_SUCCESS) {
            av_log(avctx, AV_LOG_ERROR, "Failed to flush memory: %s\n",
                   ff_vk_ret2str(ret));
            return AVERROR_EXTERNAL;
        }
    }

    vp->decode_info.srcBuffer       = sd_buf->buf.buf;
    vp->decode_info.srcBufferOffset = 0;
    vp->decode_info.srcBufferRange  = data_size;

    /* Start command buffer recording */
    err = ff_vk_exec_start(&ctx->s, exec);
    if (err < 0)
        return err;
    cmd_buf = exec->buf;

    /* Slices */
    err = ff_vk_exec_add_dep_buf(&ctx->s, exec, &vp->slices_buf, 1, 0);
    if (err < 0)
        return err;
    vp->slices_buf = NULL; /* Owned by the exec buffer from now on */

    /* Parameters */
    err = ff_vk_exec_add_dep_buf(&ctx->s, exec, &dec->session_params, 1, 1);
    if (err < 0)
        return err;

    err = ff_vk_exec_add_dep_frame(&ctx->s, exec, pic,
                                   VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR,
                                   VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR);
    if (err < 0)
        return err;

    err = ff_vk_exec_mirror_sem_value(&ctx->s, exec, &vp->sem, &vp->sem_value,
                                      pic);
    if (err < 0)
        return err;

    /* Output image - change layout, as it comes from a pool */
    img_bar[nb_img_bar] = (VkImageMemoryBarrier2) {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .pNext = NULL,
        .srcStageMask = VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR,
        .dstStageMask = VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR,
        .srcAccessMask = VK_ACCESS_2_NONE,
        .dstAccessMask = VK_ACCESS_2_VIDEO_DECODE_WRITE_BIT_KHR,
        .oldLayout = vkf->layout[0],
        .newLayout = vp->dpb_frame ? VK_IMAGE_LAYOUT_VIDEO_DECODE_DST_KHR :
                     VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR, /* Spec, 07252 utter madness */
        .srcQueueFamilyIndex = vkf->queue_family[0],
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = vkf->img[0],
        .subresourceRange = (VkImageSubresourceRange) {
            .aspectMask = vp->img_aspect,
            .layerCount = 1,
            .levelCount = 1,
        },
    };
    ff_vk_exec_update_frame(&ctx->s, exec, pic,
                            &img_bar[nb_img_bar], &nb_img_bar);

    /* Reference for the current image, if existing and not layered */
    if (vp->dpb_frame) {
        err = ff_vk_exec_add_dep_frame(&ctx->s, exec, vp->dpb_frame,
                                       VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR,
                                       VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR);
        if (err < 0)
            return err;
    }

    if (!layered_dpb) {
        /* All references (apart from the current) for non-layered refs */

        for (int i = 0; i < vp->decode_info.referenceSlotCount; i++) {
            AVFrame *ref_frame = rpic[i];
            FFVulkanDecodePicture *rvp = rvkp[i];
            AVFrame *ref = rvp->dpb_frame ? rvp->dpb_frame : ref_frame;

            err = ff_vk_exec_add_dep_frame(&ctx->s, exec, ref,
                                           VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR,
                                           VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR);
            if (err < 0)
                return err;

            if (err == 0) {
                err = ff_vk_exec_mirror_sem_value(&ctx->s, exec,
                                                  &rvp->sem, &rvp->sem_value,
                                                  ref);
                if (err < 0)
                    return err;
            }

            if (!rvp->dpb_frame) {
                AVVkFrame *rvkf = (AVVkFrame *)ref->data[0];

                img_bar[nb_img_bar] = (VkImageMemoryBarrier2) {
                    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                    .pNext = NULL,
                    .srcStageMask = VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR,
                    .dstStageMask = VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR,
                    .srcAccessMask = VK_ACCESS_2_NONE,
                    .dstAccessMask = VK_ACCESS_2_VIDEO_DECODE_READ_BIT_KHR |
                                     VK_ACCESS_2_VIDEO_DECODE_WRITE_BIT_KHR,
                    .oldLayout = rvkf->layout[0],
                    .newLayout = VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR,
                    .srcQueueFamilyIndex = rvkf->queue_family[0],
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image = rvkf->img[0],
                    .subresourceRange = (VkImageSubresourceRange) {
                        .aspectMask = rvp->img_aspect_ref,
                        .layerCount = 1,
                        .levelCount = 1,
                    },
                };
                ff_vk_exec_update_frame(&ctx->s, exec, ref,
                                        &img_bar[nb_img_bar], &nb_img_bar);
            }
        }
    } else if (vp->decode_info.referenceSlotCount ||
               vp->img_view_out != vp->img_view_ref) {
        /* Single barrier for a single layered ref */
        err = ff_vk_exec_add_dep_frame(&ctx->s, exec, ctx->layered_frame,
                                       VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR,
                                       VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR);
        if (err < 0)
            return err;
    }

    /* Change image layout */
    vk->CmdPipelineBarrier2(cmd_buf, &(VkDependencyInfo) {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
            .pImageMemoryBarriers = img_bar,
            .imageMemoryBarrierCount = nb_img_bar,
        });

    /* Start, use parameters, decode and end decoding */
    vk->CmdBeginVideoCodingKHR(cmd_buf, &decode_start);

    /* Start status query */
    if (ctx->exec_pool.nb_queries)
        vk->CmdBeginQuery(cmd_buf, ctx->exec_pool.query_pool, exec->query_idx + 0, 0);

    vk->CmdDecodeVideoKHR(cmd_buf, &vp->decode_info);

    /* End status query */
    if (ctx->exec_pool.nb_queries)
        vk->CmdEndQuery(cmd_buf, ctx->exec_pool.query_pool, exec->query_idx + 0);

    vk->CmdEndVideoCodingKHR(cmd_buf, &decode_end);

    /* End recording and submit for execution */
    return ff_vk_exec_submit(&ctx->s, exec);
}

void ff_vk_decode_free_frame(AVHWDeviceContext *dev_ctx, FFVulkanDecodePicture *vp)
{
    AVVulkanDeviceContext *hwctx = dev_ctx->hwctx;
    PFN_vkGetDeviceProcAddr device_proc_addr;
    PFN_vkWaitSemaphores wait_semaphores;
    PFN_vkDestroyImageView destroy_image_view;

    VkSemaphoreWaitInfo sem_wait = (VkSemaphoreWaitInfo) {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
        .pSemaphores = &vp->sem,
        .pValues = &vp->sem_value,
        .semaphoreCount = 1,
    };

    /* Guaranteed to exist */
    device_proc_addr = (PFN_vkGetDeviceProcAddr)hwctx->get_proc_addr(hwctx->inst, "vkGetDeviceProcAddr");
    destroy_image_view = (PFN_vkDestroyImageView)device_proc_addr(hwctx->act_dev, "vkDestroyImageView");
    wait_semaphores = (PFN_vkWaitSemaphores)device_proc_addr(hwctx->act_dev, "vkWaitSemaphores");

    /* We do not have to lock the frame here because we're not interested
     * in the actual current semaphore value, but only that it's later than
     * the time we submitted the image for decoding. */
    if (vp->sem)
        wait_semaphores(hwctx->act_dev, &sem_wait, UINT64_MAX);

    /* Free slices data */
    av_buffer_unref(&vp->slices_buf);

    /* Destroy image view (out) */
    if (vp->img_view_out && vp->img_view_out != vp->img_view_dest)
        destroy_image_view(hwctx->act_dev, vp->img_view_out, hwctx->alloc);

    /* Destroy image view (ref, unlayered) */
    if (vp->img_view_dest)
        destroy_image_view(hwctx->act_dev, vp->img_view_dest, hwctx->alloc);

    av_frame_free(&vp->dpb_frame);
}

static void free_common(void *opaque, uint8_t *data)
{
    FFVulkanDecodeShared *ctx = (FFVulkanDecodeShared *)data;
    FFVulkanContext *s = &ctx->s;
    FFVulkanFunctions *vk = &ctx->s.vkfn;

    /* Wait on and free execution pool */
    ff_vk_exec_pool_free(s, &ctx->exec_pool);

    /* Destroy layered view */
    if (ctx->layered_view)
        vk->DestroyImageView(s->hwctx->act_dev, ctx->layered_view, s->hwctx->alloc);

    /* This also frees all references from this pool */
    av_frame_free(&ctx->layered_frame);
    av_buffer_unref(&ctx->dpb_hwfc_ref);

    /* Destroy parameters */
    if (ctx->empty_session_params)
        vk->DestroyVideoSessionParametersKHR(s->hwctx->act_dev,
                                             ctx->empty_session_params,
                                             s->hwctx->alloc);

    ff_vk_video_common_uninit(s, &ctx->common);

    if (ctx->yuv_sampler)
        vk->DestroySamplerYcbcrConversion(s->hwctx->act_dev, ctx->yuv_sampler,
                                          s->hwctx->alloc);

    ff_vk_uninit(s);

    av_free(ctx);
}

static int vulkan_decode_bootstrap(AVCodecContext *avctx, AVBufferRef *frames_ref)
{
    int err;
    FFVulkanDecodeContext *dec = avctx->internal->hwaccel_priv_data;
    AVHWFramesContext *frames = (AVHWFramesContext *)frames_ref->data;
    AVHWDeviceContext *device = (AVHWDeviceContext *)frames->device_ref->data;
    AVVulkanDeviceContext *hwctx = device->hwctx;
    FFVulkanDecodeShared *ctx;

    if (dec->shared_ref)
        return 0;

    ctx = av_mallocz(sizeof(*ctx));
    if (!ctx)
        return AVERROR(ENOMEM);

    dec->shared_ref = av_buffer_create((uint8_t *)ctx, sizeof(*ctx),
                                       free_common, NULL, 0);
    if (!dec->shared_ref) {
        av_free(ctx);
        return AVERROR(ENOMEM);
    }

    ctx = (FFVulkanDecodeShared *)dec->shared_ref->data;

    ctx->s.extensions = ff_vk_extensions_to_mask(hwctx->enabled_dev_extensions,
                                                 hwctx->nb_enabled_dev_extensions);

    if (!(ctx->s.extensions & FF_VK_EXT_VIDEO_DECODE_QUEUE)) {
        av_log(avctx, AV_LOG_ERROR, "Device does not support the %s extension!\n",
               VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME);
        av_buffer_unref(&dec->shared_ref);
        return AVERROR(ENOSYS);
    }

    err = ff_vk_load_functions(device, &ctx->s.vkfn, ctx->s.extensions, 1, 1);
    if (err < 0) {
        av_buffer_unref(&dec->shared_ref);
        return err;
    }

    return 0;
}

static VkResult vulkan_setup_profile(AVCodecContext *avctx,
                                     FFVulkanDecodeProfileData *prof,
                                     AVVulkanDeviceContext *hwctx,
                                     FFVulkanFunctions *vk,
                                     const struct FFVkCodecMap *vk_codec,
                                     VkVideoDecodeH264CapabilitiesKHR *h264_caps,
                                     VkVideoDecodeH265CapabilitiesKHR *h265_caps,
                                     VkVideoDecodeAV1CapabilitiesMESA *av1_caps,
                                     VkVideoCapabilitiesKHR *caps,
                                     VkVideoDecodeCapabilitiesKHR *dec_caps,
                                     int cur_profile)
{
    VkVideoDecodeUsageInfoKHR *usage = &prof->usage;
    VkVideoProfileInfoKHR *profile = &prof->profile;
    VkVideoProfileListInfoKHR *profile_list = &prof->profile_list;

    VkVideoDecodeH264ProfileInfoKHR *h264_profile = &prof->h264_profile;
    VkVideoDecodeH264ProfileInfoKHR *h265_profile = &prof->h265_profile;
    VkVideoDecodeAV1ProfileInfoMESA *av1_profile  = &prof->av1_profile;

    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(avctx->sw_pix_fmt);
    if (!desc)
        return AVERROR(EINVAL);

    if (avctx->codec_id == AV_CODEC_ID_H264) {
        dec_caps->pNext = h264_caps;
        usage->pNext = h264_profile;
        h264_profile->sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_PROFILE_INFO_KHR;

        /* Vulkan transmits all the constrant_set flags, rather than wanting them
         * merged in the profile IDC */
        h264_profile->stdProfileIdc = cur_profile & ~(FF_PROFILE_H264_CONSTRAINED |
                                                      FF_PROFILE_H264_INTRA);

        h264_profile->pictureLayout = avctx->field_order == AV_FIELD_UNKNOWN ||
                                      avctx->field_order == AV_FIELD_PROGRESSIVE ?
                                      VK_VIDEO_DECODE_H264_PICTURE_LAYOUT_PROGRESSIVE_KHR :
                                      VK_VIDEO_DECODE_H264_PICTURE_LAYOUT_INTERLACED_INTERLEAVED_LINES_BIT_KHR;
    } else if (avctx->codec_id == AV_CODEC_ID_H265) {
        dec_caps->pNext = h265_caps;
        usage->pNext = h265_profile;
        h265_profile->sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_PROFILE_INFO_KHR;
        h265_profile->stdProfileIdc = cur_profile;
    } else if (avctx->codec_id == AV_CODEC_ID_AV1) {
        dec_caps->pNext = av1_caps;
        usage->pNext = av1_profile;
        av1_profile->sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_AV1_PROFILE_INFO_MESA;
        av1_profile->stdProfileIdc = cur_profile;
    }

    usage->sType           = VK_STRUCTURE_TYPE_VIDEO_DECODE_USAGE_INFO_KHR;
    usage->videoUsageHints = VK_VIDEO_DECODE_USAGE_DEFAULT_KHR;

    profile->sType               = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR;
    profile->pNext               = usage;
    profile->videoCodecOperation = vk_codec->decode_op;
    profile->chromaSubsampling   = ff_vk_subsampling_from_av_desc(desc);
    profile->lumaBitDepth        = ff_vk_depth_from_av_depth(desc->comp[0].depth);
    profile->chromaBitDepth      = profile->lumaBitDepth;

    profile_list->sType        = VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR;
    profile_list->profileCount = 1;
    profile_list->pProfiles    = profile;

    /* Get the capabilities of the decoder for the given profile */
    caps->sType = VK_STRUCTURE_TYPE_VIDEO_CAPABILITIES_KHR;
    caps->pNext = dec_caps;
    dec_caps->sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_CAPABILITIES_KHR;
    /* dec_caps->pNext already filled in */

    return vk->GetPhysicalDeviceVideoCapabilitiesKHR(hwctx->phys_dev, profile,
                                                     caps);
}

static int vulkan_decode_get_profile(AVCodecContext *avctx, AVBufferRef *frames_ref,
                                     enum AVPixelFormat *pix_fmt, VkFormat *vk_fmt,
                                     FFVulkanDecodeProfileData *prof,
                                     int *dpb_dedicate)
{
    VkResult ret;
    int max_level, base_profile, cur_profile;
    const struct FFVkCodecMap *vk_codec = &ff_vk_codec_map[avctx->codec_id];
    AVHWFramesContext *frames = (AVHWFramesContext *)frames_ref->data;
    AVHWDeviceContext *device = (AVHWDeviceContext *)frames->device_ref->data;
    AVVulkanDeviceContext *hwctx = device->hwctx;
    enum AVPixelFormat source_format;
    enum AVPixelFormat best_format;
    VkFormat best_vkfmt;

    FFVulkanDecodeContext *dec = avctx->internal->hwaccel_priv_data;
    FFVulkanDecodeShared *ctx = (FFVulkanDecodeShared *)dec->shared_ref->data;
    FFVulkanFunctions *vk = &ctx->s.vkfn;

    VkVideoCapabilitiesKHR *caps = &ctx->caps;
    VkVideoDecodeCapabilitiesKHR *dec_caps = &ctx->dec_caps;

    VkVideoDecodeH264CapabilitiesKHR h264_caps = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_CAPABILITIES_KHR,
    };
    VkVideoDecodeH265CapabilitiesKHR h265_caps = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_CAPABILITIES_KHR,
    };
    VkVideoDecodeAV1CapabilitiesMESA av1_caps = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_AV1_CAPABILITIES_MESA,
    };

    VkPhysicalDeviceVideoFormatInfoKHR fmt_info = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_FORMAT_INFO_KHR,
        .pNext = &prof->profile_list,
    };
    VkVideoFormatPropertiesKHR *ret_info;
    uint32_t nb_out_fmts = 0;

    if (!vk_codec->decode_op || !vk_codec->decode_extension) {
        av_log(avctx, AV_LOG_ERROR, "Unsupported codec for Vulkan decoding: %s!\n",
               avcodec_get_name(avctx->codec_id));
        return AVERROR(ENOSYS);
    } else if (!(vk_codec->decode_extension & ctx->s.extensions)) {
        av_log(avctx, AV_LOG_ERROR, "Device does not support decoding %s!\n",
               avcodec_get_name(avctx->codec_id));
        return AVERROR(ENOSYS);
    }

    cur_profile = avctx->profile;
    base_profile = avctx->codec_id == AV_CODEC_ID_H264 ? FF_PROFILE_H264_CONSTRAINED_BASELINE :
                   avctx->codec_id == AV_CODEC_ID_H265 ? FF_PROFILE_HEVC_MAIN :
                   avctx->codec_id == AV_CODEC_ID_AV1  ? STD_VIDEO_AV1_MESA_PROFILE_MAIN :
                   0;

    ret = vulkan_setup_profile(avctx, prof, hwctx, vk, vk_codec,
                               &h264_caps,
                               &h265_caps,
                               &av1_caps,
                               caps,
                               dec_caps,
                               cur_profile);
    if (ret == VK_ERROR_VIDEO_PROFILE_OPERATION_NOT_SUPPORTED_KHR &&
        avctx->flags & AV_HWACCEL_FLAG_ALLOW_PROFILE_MISMATCH &&
        avctx->profile != base_profile) {
        av_log(avctx, AV_LOG_VERBOSE, "%s profile %s not supported, attempting "
               "again with profile %s\n",
               avcodec_get_name(avctx->codec_id),
               avcodec_profile_name(avctx->codec_id, cur_profile),
               avcodec_profile_name(avctx->codec_id, base_profile));
        cur_profile = base_profile;
        ret = vulkan_setup_profile(avctx, prof, hwctx, vk, vk_codec,
                                   &h264_caps,
                                   &h265_caps,
                                   &av1_caps,
                                   caps,
                                   dec_caps,
                                   cur_profile);
    }

    if (ret == VK_ERROR_VIDEO_PROFILE_OPERATION_NOT_SUPPORTED_KHR) {
        av_log(avctx, AV_LOG_VERBOSE, "Unable to initialize video session: "
               "%s profile \"%s\" not supported!\n",
               avcodec_get_name(avctx->codec_id),
               avcodec_profile_name(avctx->codec_id, cur_profile));
        return AVERROR(EINVAL);
    } else if (ret == VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR) {
        av_log(avctx, AV_LOG_VERBOSE, "Unable to initialize video session: "
               "format (%s) not supported!\n",
               av_get_pix_fmt_name(avctx->sw_pix_fmt));
        return AVERROR(EINVAL);
    } else if (ret == VK_ERROR_FEATURE_NOT_PRESENT ||
               ret == VK_ERROR_FORMAT_NOT_SUPPORTED) {
        return AVERROR(EINVAL);
    } else if (ret != VK_SUCCESS) {
        return AVERROR_EXTERNAL;
    }

    max_level = avctx->codec_id == AV_CODEC_ID_H264 ? h264_caps.maxLevelIdc :
                avctx->codec_id == AV_CODEC_ID_H265 ? h265_caps.maxLevelIdc :
                avctx->codec_id == AV_CODEC_ID_AV1  ? av1_caps.maxLevelIdc  :
                0;

    av_log(avctx, AV_LOG_VERBOSE, "Decoder capabilities for %s profile \"%s\":\n",
           avcodec_get_name(avctx->codec_id),
           avcodec_profile_name(avctx->codec_id, cur_profile));
    av_log(avctx, AV_LOG_VERBOSE, "    Maximum level: %i (stream %i)\n",
           max_level, avctx->level);
    av_log(avctx, AV_LOG_VERBOSE, "    Width: from %i to %i\n",
           caps->minCodedExtent.width, caps->maxCodedExtent.width);
    av_log(avctx, AV_LOG_VERBOSE, "    Height: from %i to %i\n",
           caps->minCodedExtent.height, caps->maxCodedExtent.height);
    av_log(avctx, AV_LOG_VERBOSE, "    Width alignment: %i\n",
           caps->pictureAccessGranularity.width);
    av_log(avctx, AV_LOG_VERBOSE, "    Height alignment: %i\n",
           caps->pictureAccessGranularity.height);
    av_log(avctx, AV_LOG_VERBOSE, "    Bitstream offset alignment: %"PRIu64"\n",
           caps->minBitstreamBufferOffsetAlignment);
    av_log(avctx, AV_LOG_VERBOSE, "    Bitstream size alignment: %"PRIu64"\n",
           caps->minBitstreamBufferSizeAlignment);
    av_log(avctx, AV_LOG_VERBOSE, "    Maximum references: %u\n",
           caps->maxDpbSlots);
    av_log(avctx, AV_LOG_VERBOSE, "    Maximum active references: %u\n",
           caps->maxActiveReferencePictures);
    av_log(avctx, AV_LOG_VERBOSE, "    Codec header version: %i.%i.%i (driver), %i.%i.%i (compiled)\n",
           CODEC_VER(caps->stdHeaderVersion.specVersion),
           CODEC_VER(dec_ext[avctx->codec_id]->specVersion));
    av_log(avctx, AV_LOG_VERBOSE, "    Decode modes:%s%s%s\n",
           dec_caps->flags ? "" :
               " invalid",
           dec_caps->flags & VK_VIDEO_DECODE_CAPABILITY_DPB_AND_OUTPUT_COINCIDE_BIT_KHR ?
               " reuse_dst_dpb" : "",
           dec_caps->flags & VK_VIDEO_DECODE_CAPABILITY_DPB_AND_OUTPUT_DISTINCT_BIT_KHR ?
               " dedicated_dpb" : "");
    av_log(avctx, AV_LOG_VERBOSE, "    Capability flags:%s%s%s\n",
           caps->flags ? "" :
               " none",
           caps->flags & VK_VIDEO_CAPABILITY_PROTECTED_CONTENT_BIT_KHR ?
               " protected" : "",
           caps->flags & VK_VIDEO_CAPABILITY_SEPARATE_REFERENCE_IMAGES_BIT_KHR ?
               " separate_references" : "");

    /* Check if decoding is possible with the given parameters */
    if (avctx->width  < caps->minCodedExtent.width   ||
        avctx->height < caps->minCodedExtent.height  ||
        avctx->width  > caps->maxCodedExtent.width   ||
        avctx->height > caps->maxCodedExtent.height)
        return AVERROR(EINVAL);

    if (!(avctx->hwaccel_flags & AV_HWACCEL_FLAG_IGNORE_LEVEL) &&
        avctx->level > max_level)
        return AVERROR(EINVAL);

    /* Some basic sanity checking */
    if (!(dec_caps->flags & (VK_VIDEO_DECODE_CAPABILITY_DPB_AND_OUTPUT_COINCIDE_BIT_KHR |
                             VK_VIDEO_DECODE_CAPABILITY_DPB_AND_OUTPUT_DISTINCT_BIT_KHR))) {
        av_log(avctx, AV_LOG_ERROR, "Buggy driver signals invalid decoding mode: neither "
               "VK_VIDEO_DECODE_CAPABILITY_DPB_AND_OUTPUT_COINCIDE_BIT_KHR nor "
               "VK_VIDEO_DECODE_CAPABILITY_DPB_AND_OUTPUT_DISTINCT_BIT_KHR are set!\n");
        return AVERROR_EXTERNAL;
    } else if ((dec_caps->flags & (VK_VIDEO_DECODE_CAPABILITY_DPB_AND_OUTPUT_COINCIDE_BIT_KHR |
                                   VK_VIDEO_DECODE_CAPABILITY_DPB_AND_OUTPUT_DISTINCT_BIT_KHR) ==
                                   VK_VIDEO_DECODE_CAPABILITY_DPB_AND_OUTPUT_COINCIDE_BIT_KHR) &&
               !(caps->flags & VK_VIDEO_CAPABILITY_SEPARATE_REFERENCE_IMAGES_BIT_KHR)) {
        av_log(avctx, AV_LOG_ERROR, "Cannot initialize Vulkan decoding session, buggy driver: "
               "VK_VIDEO_DECODE_CAPABILITY_DPB_AND_OUTPUT_COINCIDE_BIT_KHR set "
               "but VK_VIDEO_CAPABILITY_SEPARATE_REFERENCE_IMAGES_BIT_KHR is unset!\n");
        return AVERROR_EXTERNAL;
    } else if (!(dec_caps->flags & VK_VIDEO_DECODE_CAPABILITY_DPB_AND_OUTPUT_DISTINCT_BIT_KHR) &&
               avctx->codec_id == AV_CODEC_ID_AV1) {
        av_log(avctx, AV_LOG_ERROR, "Cannot initialize Vulkan decoding session, buggy driver: "
               "codec is AV1, but VK_VIDEO_DECODE_CAPABILITY_DPB_AND_OUTPUT_DISTINCT_BIT_KHR isn't set!\n");
        return AVERROR_EXTERNAL;
    }

    /* TODO: make dedicated_dpb tunable */
    dec->dedicated_dpb = !(dec_caps->flags & VK_VIDEO_DECODE_CAPABILITY_DPB_AND_OUTPUT_COINCIDE_BIT_KHR);
    dec->layered_dpb = !(caps->flags & VK_VIDEO_CAPABILITY_SEPARATE_REFERENCE_IMAGES_BIT_KHR);
    dec->external_fg = av1_caps.flags & VK_VIDEO_DECODE_AV1_CAPABILITY_EXTERNAL_FILM_GRAIN_MESA;

    if (dec->dedicated_dpb) {
        fmt_info.imageUsage = VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR;
    } else {
        fmt_info.imageUsage = VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR |
                              VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR |
                              VK_IMAGE_USAGE_TRANSFER_SRC_BIT         |
                              VK_IMAGE_USAGE_SAMPLED_BIT;
    }

    /* Get the format of the images necessary */
    ret = vk->GetPhysicalDeviceVideoFormatPropertiesKHR(hwctx->phys_dev,
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

    ret = vk->GetPhysicalDeviceVideoFormatPropertiesKHR(hwctx->phys_dev,
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

    /* Find a format to use */
    *pix_fmt = best_format = AV_PIX_FMT_NONE;
    *vk_fmt  = best_vkfmt = VK_FORMAT_UNDEFINED;
    source_format = avctx->sw_pix_fmt;

    av_log(avctx, AV_LOG_DEBUG, "Choosing best pixel format for decoding from %i:\n", nb_out_fmts);
    for (int i = 0; i < nb_out_fmts; i++) {
        enum AVPixelFormat tmp = ff_vk_pix_fmt_from_vkfmt(ret_info[i].format);
        if (tmp == AV_PIX_FMT_NONE) {
            av_log(avctx, AV_LOG_WARNING, "Invalid/unknown Vulkan format %i!\n", ret_info[i].format);
            continue;
        }

        best_format = av_find_best_pix_fmt_of_2(tmp, best_format, source_format, 0, NULL);
        if (tmp == best_format)
            best_vkfmt = ret_info[i].format;

        av_log(avctx, AV_LOG_DEBUG, "    %s%s (Vulkan ID: %i)\n",
               av_get_pix_fmt_name(tmp), tmp == best_format ? "*" : "",
               ret_info[i].format);
    }

    av_free(ret_info);

    if (best_format == AV_PIX_FMT_NONE) {
        av_log(avctx, AV_LOG_ERROR, "No valid/compatible pixel format found for decoding!\n");
        return AVERROR(EINVAL);
    } else {
        av_log(avctx, AV_LOG_VERBOSE, "Chosen frame pixfmt: %s (Vulkan ID: %i)\n",
               av_get_pix_fmt_name(best_format), best_vkfmt);
    }

    *pix_fmt = best_format;
    *vk_fmt = best_vkfmt;

    *dpb_dedicate = dec->dedicated_dpb;

    return 0;
}

static void free_profile_data(AVHWFramesContext *hwfc)
{
    av_free(hwfc->user_opaque);
}

int ff_vk_frame_params(AVCodecContext *avctx, AVBufferRef *hw_frames_ctx)
{
    VkFormat vkfmt;
    int err, dedicated_dpb;
    AVHWFramesContext *frames_ctx = (AVHWFramesContext*)hw_frames_ctx->data;
    AVVulkanFramesContext *hwfc = frames_ctx->hwctx;
    FFVulkanDecodeContext *dec = avctx->internal->hwaccel_priv_data;
    FFVulkanDecodeProfileData *prof;

    frames_ctx->sw_format = AV_PIX_FMT_NONE;

    err = vulkan_decode_bootstrap(avctx, hw_frames_ctx);
    if (err < 0)
        return err;

    prof = av_mallocz(sizeof(FFVulkanDecodeProfileData));
    if (!prof)
        return AVERROR(ENOMEM);

    err = vulkan_decode_get_profile(avctx, hw_frames_ctx,
                                    &frames_ctx->sw_format, &vkfmt,
                                    prof, &dedicated_dpb);
    if (err < 0) {
        av_free(prof);
        return err;
    }

    frames_ctx->user_opaque = prof;
    frames_ctx->free        = free_profile_data;

    frames_ctx->width  = avctx->width;
    frames_ctx->height = avctx->height;
    frames_ctx->format = AV_PIX_FMT_VULKAN;

    hwfc->format[0]    = vkfmt;
    hwfc->create_pnext = &prof->profile_list;
    hwfc->tiling       = VK_IMAGE_TILING_OPTIMAL;
    hwfc->usage        = VK_IMAGE_USAGE_TRANSFER_SRC_BIT         |
                         VK_IMAGE_USAGE_SAMPLED_BIT              |
                         VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR;

    if (!dec->dedicated_dpb)
        hwfc->usage |= VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR;

    return err;
}

void ff_vk_decode_free_params(void *opaque, uint8_t *data)
{
    FFVulkanDecodeShared *ctx = opaque;
    FFVulkanFunctions *vk = &ctx->s.vkfn;
    VkVideoSessionParametersKHR *par = (VkVideoSessionParametersKHR *)data;
    vk->DestroyVideoSessionParametersKHR(ctx->s.hwctx->act_dev, *par,
                                         ctx->s.hwctx->alloc);
    av_free(par);
}

int ff_vk_decode_uninit(AVCodecContext *avctx)
{
    FFVulkanDecodeContext *dec = avctx->internal->hwaccel_priv_data;
    av_buffer_pool_uninit(&dec->tmp_pool);
    av_buffer_unref(&dec->session_params);
    av_buffer_unref(&dec->shared_ref);
    av_freep(&dec->slice_off);
    return 0;
}

int ff_vk_decode_init(AVCodecContext *avctx)
{
    int err, qf, cxpos = 0, cypos = 0, nb_q = 0;
    VkResult ret;
    FFVulkanDecodeContext *dec = avctx->internal->hwaccel_priv_data;
    FFVulkanDecodeShared *ctx;
    FFVulkanContext *s;
    FFVulkanFunctions *vk;
    FFVkQueueFamilyCtx qf_dec;
    const VkVideoProfileListInfoKHR *profile_list;

    VkVideoDecodeH264SessionParametersCreateInfoKHR h264_params = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_SESSION_PARAMETERS_CREATE_INFO_KHR,
    };
    VkVideoDecodeH265SessionParametersCreateInfoKHR h265_params = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_SESSION_PARAMETERS_CREATE_INFO_KHR,
    };
    VkVideoDecodeAV1SessionParametersCreateInfoMESA av1_params = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_AV1_SESSION_PARAMETERS_CREATE_INFO_MESA,
    };
    VkVideoSessionParametersCreateInfoKHR session_params_create = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_PARAMETERS_CREATE_INFO_KHR,
        .pNext = avctx->codec_id == AV_CODEC_ID_H264 ? (void *)&h264_params :
                 avctx->codec_id == AV_CODEC_ID_HEVC ? (void *)&h265_params :
                 avctx->codec_id == AV_CODEC_ID_AV1  ? (void *)&av1_params  :
                 NULL,
    };
    VkVideoSessionCreateInfoKHR session_create = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_CREATE_INFO_KHR,
    };
    VkSamplerYcbcrConversionCreateInfo yuv_sampler_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO,
        .components = ff_comp_identity_map,
        .ycbcrModel = VK_SAMPLER_YCBCR_MODEL_CONVERSION_RGB_IDENTITY,
        .ycbcrRange = avctx->color_range == AVCOL_RANGE_MPEG, /* Ignored */
    };

    err = ff_decode_get_hw_frames_ctx(avctx, AV_HWDEVICE_TYPE_VULKAN);
    if (err < 0)
        return err;

    /* Initialize contexts */
    ctx = (FFVulkanDecodeShared *)dec->shared_ref->data;
    s = &ctx->s;
    vk = &ctx->s.vkfn;

    s->frames_ref = av_buffer_ref(avctx->hw_frames_ctx);
    s->frames = (AVHWFramesContext *)s->frames_ref->data;
    s->hwfc = s->frames->hwctx;

    s->device = (AVHWDeviceContext *)s->frames->device_ref->data;
    s->hwctx = s->device->hwctx;

    profile_list = ff_vk_find_struct(s->hwfc->create_pnext,
                                     VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR);
    if (!profile_list) {
        av_log(avctx, AV_LOG_ERROR, "Profile list missing from frames context!");
        return AVERROR(EINVAL);
    }

    err = ff_vk_load_props(s);
    if (err < 0)
        goto fail;

    /* Create queue context */
    qf = ff_vk_qf_init(s, &qf_dec, VK_QUEUE_VIDEO_DECODE_BIT_KHR);

    /* Check for support */
    if (!(s->video_props[qf].videoCodecOperations &
          ff_vk_codec_map[avctx->codec_id].decode_op)) {
        av_log(avctx, AV_LOG_ERROR, "Decoding %s not supported on the given "
               "queue family %i!\n", avcodec_get_name(avctx->codec_id), qf);
        return AVERROR(EINVAL);
    }

    /* Enable queries if supported */
    if (s->query_props[qf].queryResultStatusSupport)
        nb_q = 1;

    session_create.flags = 0x0;
    session_create.queueFamilyIndex = s->hwctx->queue_family_decode_index;
    session_create.maxCodedExtent = ctx->caps.maxCodedExtent;
    session_create.maxDpbSlots = ctx->caps.maxDpbSlots;
    session_create.maxActiveReferencePictures = ctx->caps.maxActiveReferencePictures;
    session_create.pictureFormat = s->hwfc->format[0];
    session_create.referencePictureFormat = session_create.pictureFormat;
    session_create.pStdHeaderVersion = dec_ext[avctx->codec_id];
    session_create.pVideoProfile = &profile_list->pProfiles[0];

    /* Create decode exec context.
     * 2 async contexts per thread was experimentally determined to be optimal
     * for a majority of streams. */
    err = ff_vk_exec_pool_init(s, &qf_dec, &ctx->exec_pool, 2*avctx->thread_count,
                               nb_q, VK_QUERY_TYPE_RESULT_STATUS_ONLY_KHR, 0,
                               session_create.pVideoProfile);
    if (err < 0)
        goto fail;

    err = ff_vk_video_common_init(avctx, s, &ctx->common, &session_create);
    if (err < 0)
        goto fail;

    /* Get sampler */
    av_chroma_location_enum_to_pos(&cxpos, &cypos, avctx->chroma_sample_location);
    yuv_sampler_info.xChromaOffset = cxpos >> 7;
    yuv_sampler_info.yChromaOffset = cypos >> 7;
    yuv_sampler_info.format = s->hwfc->format[0];
    ret = vk->CreateSamplerYcbcrConversion(s->hwctx->act_dev, &yuv_sampler_info,
                                           s->hwctx->alloc, &ctx->yuv_sampler);
    if (ret != VK_SUCCESS) {
        err = AVERROR_EXTERNAL;
        goto fail;
    }

    /* If doing an out-of-place decoding, create a DPB pool */
    if (dec->dedicated_dpb || avctx->codec_id == AV_CODEC_ID_AV1) {
        AVHWFramesContext *dpb_frames;
        AVVulkanFramesContext *dpb_hwfc;

        ctx->dpb_hwfc_ref = av_hwframe_ctx_alloc(s->frames->device_ref);
        if (!ctx->dpb_hwfc_ref) {
            err = AVERROR(ENOMEM);
            goto fail;
        }

        dpb_frames = (AVHWFramesContext *)ctx->dpb_hwfc_ref->data;
        dpb_frames->format    = s->frames->format;
        dpb_frames->sw_format = s->frames->sw_format;
        dpb_frames->width     = s->frames->width;
        dpb_frames->height    = s->frames->height;

        dpb_hwfc = dpb_frames->hwctx;
        dpb_hwfc->create_pnext = (void *)profile_list;
        dpb_hwfc->format[0]    = s->hwfc->format[0];
        dpb_hwfc->tiling       = VK_IMAGE_TILING_OPTIMAL;
        dpb_hwfc->usage        = VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR |
                                 VK_IMAGE_USAGE_SAMPLED_BIT; /* Shuts validator up. */

        if (dec->layered_dpb)
            dpb_hwfc->nb_layers = ctx->caps.maxDpbSlots;

        err = av_hwframe_ctx_init(ctx->dpb_hwfc_ref);
        if (err < 0)
            goto fail;

        if (dec->layered_dpb) {
            ctx->layered_frame = vk_get_dpb_pool(ctx);
            if (!ctx->layered_frame) {
                err = AVERROR(ENOMEM);
                goto fail;
            }

            err = vk_decode_create_view(ctx, &ctx->layered_view, &ctx->layered_aspect,
                                        (AVVkFrame *)ctx->layered_frame->data[0],
                                        s->hwfc->format[0]);
            if (err < 0)
                goto fail;
        }
    }

    session_params_create.videoSession = ctx->common.session;
    ret = vk->CreateVideoSessionParametersKHR(s->hwctx->act_dev, &session_params_create,
                                              s->hwctx->alloc, &ctx->empty_session_params);
    if (ret != VK_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Unable to create empty Vulkan video session parameters: %s!\n",
               ff_vk_ret2str(ret));
        return AVERROR_EXTERNAL;
    }

    ff_vk_decode_flush(avctx);

    av_log(avctx, AV_LOG_VERBOSE, "Vulkan decoder initialization sucessful\n");

    return 0;

fail:
    ff_vk_decode_uninit(avctx);

    return err;
}
