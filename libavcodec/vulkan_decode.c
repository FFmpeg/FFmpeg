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

#include "libavutil/refstruct.h"
#include "vulkan_video.h"
#include "vulkan_decode.h"
#include "config_components.h"
#include "libavutil/avassert.h"
#include "libavutil/mem.h"
#include "libavutil/vulkan_loader.h"

#define DECODER_IS_SDR(codec_id) \
    ((codec_id) == AV_CODEC_ID_FFV1)

#if CONFIG_H264_VULKAN_HWACCEL
extern const FFVulkanDecodeDescriptor ff_vk_dec_h264_desc;
#endif
#if CONFIG_HEVC_VULKAN_HWACCEL
extern const FFVulkanDecodeDescriptor ff_vk_dec_hevc_desc;
#endif
#if CONFIG_AV1_VULKAN_HWACCEL
extern const FFVulkanDecodeDescriptor ff_vk_dec_av1_desc;
#endif
#if CONFIG_FFV1_VULKAN_HWACCEL
extern const FFVulkanDecodeDescriptor ff_vk_dec_ffv1_desc;
#endif

static const FFVulkanDecodeDescriptor *dec_descs[] = {
#if CONFIG_H264_VULKAN_HWACCEL
    &ff_vk_dec_h264_desc,
#endif
#if CONFIG_HEVC_VULKAN_HWACCEL
    &ff_vk_dec_hevc_desc,
#endif
#if CONFIG_AV1_VULKAN_HWACCEL
    &ff_vk_dec_av1_desc,
#endif
#if CONFIG_FFV1_VULKAN_HWACCEL
    &ff_vk_dec_ffv1_desc,
#endif
};

static const FFVulkanDecodeDescriptor *get_codecdesc(enum AVCodecID codec_id)
{
    for (size_t i = 0; i < FF_ARRAY_ELEMS(dec_descs); i++)
        if (dec_descs[i]->codec_id == codec_id)
            return dec_descs[i];
    av_assert1(!"no codec descriptor");
    return NULL;
}

static const VkVideoProfileInfoKHR *get_video_profile(FFVulkanDecodeShared *ctx, enum AVCodecID codec_id)
{
    const VkVideoProfileListInfoKHR *profile_list;

    VkStructureType profile_struct_type =
        codec_id == AV_CODEC_ID_H264 ? VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_PROFILE_INFO_KHR :
        codec_id == AV_CODEC_ID_HEVC ? VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_PROFILE_INFO_KHR :
        codec_id == AV_CODEC_ID_AV1  ? VK_STRUCTURE_TYPE_VIDEO_DECODE_AV1_PROFILE_INFO_KHR :
                                       VK_STRUCTURE_TYPE_MAX_ENUM;
    if (profile_struct_type == VK_STRUCTURE_TYPE_MAX_ENUM)
        return NULL;

    profile_list = ff_vk_find_struct(ctx->s.hwfc->create_pnext,
                                     VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR);
    if (!profile_list)
        return NULL;

    for (int i = 0; i < profile_list->profileCount; i++)
        if (ff_vk_find_struct(profile_list->pProfiles[i].pNext, profile_struct_type))
            return &profile_list->pProfiles[i];

    return NULL;
}

int ff_vk_update_thread_context(AVCodecContext *dst, const AVCodecContext *src)
{
    int err;
    FFVulkanDecodeContext *src_ctx = src->internal->hwaccel_priv_data;
    FFVulkanDecodeContext *dst_ctx = dst->internal->hwaccel_priv_data;

    av_refstruct_replace(&dst_ctx->shared_ctx, src_ctx->shared_ctx);

    err = av_buffer_replace(&dst_ctx->session_params, src_ctx->session_params);
    if (err < 0)
        return err;

    dst_ctx->dedicated_dpb = src_ctx->dedicated_dpb;
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

static AVFrame *vk_get_dpb_pool(FFVulkanDecodeShared *ctx)
{
    int err;
    AVFrame *avf = av_frame_alloc();
    if (!avf)
        return NULL;

    err = av_hwframe_get_buffer(ctx->common.dpb_hwfc_ref, avf, 0x0);
    if (err < 0)
        av_frame_free(&avf);

    return avf;
}

static void init_frame(FFVulkanDecodeContext *dec, FFVulkanDecodePicture *vkpic)
{
    FFVulkanDecodeShared *ctx = dec->shared_ctx;
    FFVulkanFunctions *vk = &ctx->s.vkfn;

    vkpic->dpb_frame     = NULL;
    for (int i = 0; i < AV_NUM_DATA_POINTERS; i++) {
        vkpic->view.ref[i]  = VK_NULL_HANDLE;
        vkpic->view.out[i]  = VK_NULL_HANDLE;
        vkpic->view.dst[i]  = VK_NULL_HANDLE;
    }

    vkpic->destroy_image_view = vk->DestroyImageView;
    vkpic->wait_semaphores = vk->WaitSemaphores;
    vkpic->invalidate_memory_ranges = vk->InvalidateMappedMemoryRanges;
}

int ff_vk_decode_prepare_frame(FFVulkanDecodeContext *dec, AVFrame *pic,
                               FFVulkanDecodePicture *vkpic, int is_current,
                               int alloc_dpb)
{
    int err;
    FFVulkanDecodeShared *ctx = dec->shared_ctx;

    vkpic->slices_size = 0;

    /* If the decoder made a blank frame to make up for a missing ref, or the
     * frame is the current frame so it's missing one, create a re-representation */
    if (vkpic->view.ref[0])
        return 0;

    init_frame(dec, vkpic);

    if (ctx->common.layered_dpb && alloc_dpb) {
        vkpic->view.ref[0] = ctx->common.layered_view;
        vkpic->view.aspect_ref[0] = ctx->common.layered_aspect;
    } else if (alloc_dpb) {
        AVHWFramesContext *dpb_frames = (AVHWFramesContext *)ctx->common.dpb_hwfc_ref->data;
        AVVulkanFramesContext *dpb_hwfc = dpb_frames->hwctx;

        vkpic->dpb_frame = vk_get_dpb_pool(ctx);
        if (!vkpic->dpb_frame)
            return AVERROR(ENOMEM);

        err = ff_vk_create_view(&ctx->s, &ctx->common,
                                &vkpic->view.ref[0], &vkpic->view.aspect_ref[0],
                                (AVVkFrame *)vkpic->dpb_frame->data[0],
                                dpb_hwfc->format[0], !is_current);
        if (err < 0)
            return err;

        vkpic->view.dst[0] = vkpic->view.ref[0];
    }

    if (!alloc_dpb || is_current) {
        AVHWFramesContext *frames = (AVHWFramesContext *)pic->hw_frames_ctx->data;
        AVVulkanFramesContext *hwfc = frames->hwctx;

        err = ff_vk_create_view(&ctx->s, &ctx->common,
                                &vkpic->view.out[0], &vkpic->view.aspect[0],
                                (AVVkFrame *)pic->data[0],
                                hwfc->format[0], !is_current);
        if (err < 0)
            return err;

        if (!alloc_dpb) {
            vkpic->view.ref[0] = vkpic->view.out[0];
            vkpic->view.aspect_ref[0] = vkpic->view.aspect[0];
        }
    }

    return 0;
}

int ff_vk_decode_prepare_frame_sdr(FFVulkanDecodeContext *dec, AVFrame *pic,
                                   FFVulkanDecodePicture *vkpic, int is_current,
                                   enum FFVkShaderRepFormat rep_fmt, int alloc_dpb)
{
    int err;
    FFVulkanDecodeShared *ctx = dec->shared_ctx;
    AVHWFramesContext *frames = (AVHWFramesContext *)pic->hw_frames_ctx->data;

    vkpic->slices_size = 0;

    if (vkpic->view.ref[0])
        return 0;

    init_frame(dec, vkpic);

    for (int i = 0; i < av_pix_fmt_count_planes(frames->sw_format); i++) {
        if (alloc_dpb) {
            vkpic->dpb_frame = vk_get_dpb_pool(ctx);
            if (!vkpic->dpb_frame)
                return AVERROR(ENOMEM);

            err = ff_vk_create_imageview(&ctx->s,
                                         &vkpic->view.ref[i], &vkpic->view.aspect_ref[i],
                                         vkpic->dpb_frame, i, rep_fmt);
            if (err < 0)
                return err;

            vkpic->view.dst[i] = vkpic->view.ref[i];
        }

        if (!alloc_dpb || is_current) {
            err = ff_vk_create_imageview(&ctx->s,
                                         &vkpic->view.out[i], &vkpic->view.aspect[i],
                                         pic, i, rep_fmt);
            if (err < 0)
                return err;

            if (!alloc_dpb) {
                vkpic->view.ref[i] = vkpic->view.out[i];
                vkpic->view.aspect_ref[i] = vkpic->view.aspect[i];
            }
        }
    }

    return 0;
}

int ff_vk_decode_add_slice(AVCodecContext *avctx, FFVulkanDecodePicture *vp,
                           const uint8_t *data, size_t size, int add_startcode,
                           uint32_t *nb_slices, const uint32_t **offsets)
{
    FFVulkanDecodeContext *dec = avctx->internal->hwaccel_priv_data;
    FFVulkanDecodeShared *ctx = dec->shared_ctx;

    static const uint8_t startcode_prefix[3] = { 0x0, 0x0, 0x1 };
    const size_t startcode_len = add_startcode ? sizeof(startcode_prefix) : 0;
    const int nb = nb_slices ? *nb_slices : 0;
    uint8_t *slices;
    uint32_t *slice_off;
    FFVkBuffer *vkbuf;

    size_t new_size = vp->slices_size + startcode_len + size +
                      ctx->caps.minBitstreamBufferSizeAlignment;
    new_size = FFALIGN(new_size, ctx->caps.minBitstreamBufferSizeAlignment);

    if (offsets) {
        slice_off = av_fast_realloc(dec->slice_off, &dec->slice_off_max,
                                    (nb + 1)*sizeof(slice_off));
        if (!slice_off)
            return AVERROR(ENOMEM);

        *offsets = dec->slice_off = slice_off;

        slice_off[nb] = vp->slices_size;
    }

    vkbuf = vp->slices_buf ? (FFVkBuffer *)vp->slices_buf->data : NULL;
    if (!vkbuf || vkbuf->size < new_size) {
        int err;
        AVBufferRef *new_ref;
        FFVkBuffer *new_buf;

        /* No point in requesting anything smaller. */
        size_t buf_size = FFMAX(new_size, 1024*1024);

        /* Align buffer to nearest power of two. Makes fragmentation management
         * easier, and gives us ample headroom. */
        buf_size = 2 << av_log2(buf_size);

        err = ff_vk_get_pooled_buffer(&ctx->s, &ctx->buf_pool, &new_ref,
                                      DECODER_IS_SDR(avctx->codec_id) ?
                                      (VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                       VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) :
                                      VK_BUFFER_USAGE_VIDEO_DECODE_SRC_BIT_KHR,
                                      ctx->s.hwfc->create_pnext, buf_size,
                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                      (DECODER_IS_SDR(avctx->codec_id) ?
                                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT : 0x0));
        if (err < 0)
            return err;

        new_buf = (FFVkBuffer *)new_ref->data;

        /* Copy data from the old buffer */
        if (vkbuf) {
            memcpy(new_buf->mapped_mem, vkbuf->mapped_mem, vp->slices_size);
            av_buffer_unref(&vp->slices_buf);
        }

        vp->slices_buf = new_ref;
        vkbuf = new_buf;
    }
    slices = vkbuf->mapped_mem;

    /* Startcode */
    memcpy(slices + vp->slices_size, startcode_prefix, startcode_len);

    /* Slice data */
    memcpy(slices + vp->slices_size + startcode_len, data, size);

    if (nb_slices)
        *nb_slices = nb + 1;

    vp->slices_size += startcode_len + size;

    return 0;
}

void ff_vk_decode_flush(AVCodecContext *avctx)
{
    FFVulkanDecodeContext *dec = avctx->internal->hwaccel_priv_data;
    FFVulkanDecodeShared *ctx = dec->shared_ctx;

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
    FFVkExecContext *exec;

    /* Non-video queues do not need to be reset */
    if (!(get_codecdesc(avctx->codec_id)->decode_op))
        return;

    exec = ff_vk_exec_get(&ctx->s, &ctx->exec_pool);
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
    FFVkBuffer *sd_buf;

    FFVulkanDecodeContext *dec = avctx->internal->hwaccel_priv_data;
    FFVulkanDecodeShared *ctx = dec->shared_ctx;
    FFVulkanFunctions *vk = &ctx->s.vkfn;

    /* Output */
    AVVkFrame *vkf = (AVVkFrame *)pic->buf[0]->data;

    /* Quirks */
    const int layered_dpb = ctx->common.layered_dpb;

    VkVideoBeginCodingInfoKHR decode_start = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_BEGIN_CODING_INFO_KHR,
        .videoSession = ctx->common.session,
        .videoSessionParameters = dec->session_params ?
                                  *((VkVideoSessionParametersKHR *)dec->session_params->data) :
                                  VK_NULL_HANDLE,
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

    FFVkExecContext *exec = ff_vk_exec_get(&ctx->s, &ctx->exec_pool);

    /* The current decoding reference has to be bound as an inactive reference */
    VkVideoReferenceSlotInfoKHR *cur_vk_ref;
    cur_vk_ref = (void *)&decode_start.pReferenceSlots[decode_start.referenceSlotCount];
    cur_vk_ref[0] = vp->ref_slot;
    cur_vk_ref[0].slotIndex = -1;
    decode_start.referenceSlotCount++;

    sd_buf = (FFVkBuffer *)vp->slices_buf->data;

    /* Flush if needed */
    if (!(sd_buf->flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        VkMappedMemoryRange flush_buf = {
            .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .memory = sd_buf->mem,
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

    vp->decode_info.srcBuffer       = sd_buf->buf;
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
        .newLayout = (layered_dpb || vp->dpb_frame) ?
                     VK_IMAGE_LAYOUT_VIDEO_DECODE_DST_KHR :
                     VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR, /* Spec, 07252 utter madness */
        .srcQueueFamilyIndex = vkf->queue_family[0],
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = vkf->img[0],
        .subresourceRange = (VkImageSubresourceRange) {
            .aspectMask = vp->view.aspect[0],
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
                        .aspectMask = rvp->view.aspect_ref[0],
                        .layerCount = 1,
                        .levelCount = 1,
                    },
                };
                ff_vk_exec_update_frame(&ctx->s, exec, ref,
                                        &img_bar[nb_img_bar], &nb_img_bar);
            }
        }
    } else if (vp->decode_info.referenceSlotCount ||
               vp->view.out[0] != vp->view.ref[0]) {
        /* Single barrier for a single layered ref */
        err = ff_vk_exec_add_dep_frame(&ctx->s, exec, ctx->common.layered_frame,
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
    vk->CmdDecodeVideoKHR(cmd_buf, &vp->decode_info);
    vk->CmdEndVideoCodingKHR(cmd_buf, &decode_end);

    /* End recording and submit for execution */
    return ff_vk_exec_submit(&ctx->s, exec);
}

void ff_vk_decode_free_frame(AVHWDeviceContext *dev_ctx, FFVulkanDecodePicture *vp)
{
    AVVulkanDeviceContext *hwctx = dev_ctx->hwctx;

    VkSemaphoreWaitInfo sem_wait = (VkSemaphoreWaitInfo) {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
        .pSemaphores = &vp->sem,
        .pValues = &vp->sem_value,
        .semaphoreCount = 1,
    };

    /* We do not have to lock the frame here because we're not interested
     * in the actual current semaphore value, but only that it's later than
     * the time we submitted the image for decoding. */
    if (vp->sem)
        vp->wait_semaphores(hwctx->act_dev, &sem_wait, UINT64_MAX);

    /* Free slices data */
    av_buffer_unref(&vp->slices_buf);

    /* Destroy image view (out) */
    for (int i = 0; i < AV_NUM_DATA_POINTERS; i++) {
        if (vp->view.out[i] && vp->view.out[i] != vp->view.dst[i])
            vp->destroy_image_view(hwctx->act_dev, vp->view.out[i], hwctx->alloc);

        /* Destroy image view (ref, unlayered) */
        if (vp->view.dst[i])
            vp->destroy_image_view(hwctx->act_dev, vp->view.dst[i], hwctx->alloc);
    }

    av_frame_free(&vp->dpb_frame);
}

static void free_common(AVRefStructOpaque unused, void *obj)
{
    FFVulkanDecodeShared *ctx = obj;
    FFVulkanContext *s = &ctx->s;
    FFVulkanFunctions *vk = &ctx->s.vkfn;

    /* Wait on and free execution pool */
    ff_vk_exec_pool_free(&ctx->s, &ctx->exec_pool);

    /* This also frees all references from this pool */
    av_frame_free(&ctx->common.layered_frame);

    /* Destroy parameters */
    if (ctx->empty_session_params)
        vk->DestroyVideoSessionParametersKHR(s->hwctx->act_dev,
                                             ctx->empty_session_params,
                                             s->hwctx->alloc);

    av_buffer_pool_uninit(&ctx->buf_pool);

    ff_vk_video_common_uninit(s, &ctx->common);

    if (ctx->sd_ctx_free)
        ctx->sd_ctx_free(ctx);

    ff_vk_uninit(s);
}

static int vulkan_decode_bootstrap(AVCodecContext *avctx, AVBufferRef *frames_ref)
{
    int err;
    FFVulkanDecodeContext *dec = avctx->internal->hwaccel_priv_data;
    const FFVulkanDecodeDescriptor *vk_desc = get_codecdesc(avctx->codec_id);
    AVHWFramesContext *frames = (AVHWFramesContext *)frames_ref->data;
    AVHWDeviceContext *device = (AVHWDeviceContext *)frames->device_ref->data;
    AVVulkanDeviceContext *hwctx = device->hwctx;
    FFVulkanDecodeShared *ctx;

    if (dec->shared_ctx)
        return 0;

    dec->shared_ctx = av_refstruct_alloc_ext(sizeof(*ctx), 0, NULL,
                                             free_common);
    if (!dec->shared_ctx)
        return AVERROR(ENOMEM);

    ctx = dec->shared_ctx;

    ctx->s.extensions = ff_vk_extensions_to_mask(hwctx->enabled_dev_extensions,
                                                 hwctx->nb_enabled_dev_extensions);

    if (vk_desc->queue_flags & VK_QUEUE_VIDEO_DECODE_BIT_KHR) {
        if (!(ctx->s.extensions & FF_VK_EXT_VIDEO_DECODE_QUEUE)) {
            av_log(avctx, AV_LOG_ERROR, "Device does not support the %s extension!\n",
                   VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME);
            av_refstruct_unref(&dec->shared_ctx);
            return AVERROR(ENOSYS);
        }
    }

    err = ff_vk_load_functions(device, &ctx->s.vkfn, ctx->s.extensions, 1, 1);
    if (err < 0) {
        av_refstruct_unref(&dec->shared_ctx);
        return err;
    }

    return 0;
}

static VkResult vulkan_setup_profile(AVCodecContext *avctx,
                                     FFVulkanDecodeProfileData *prof,
                                     AVVulkanDeviceContext *hwctx,
                                     FFVulkanFunctions *vk,
                                     const FFVulkanDecodeDescriptor *vk_desc,
                                     VkVideoDecodeH264CapabilitiesKHR *h264_caps,
                                     VkVideoDecodeH265CapabilitiesKHR *h265_caps,
                                     VkVideoDecodeAV1CapabilitiesKHR *av1_caps,
                                     VkVideoCapabilitiesKHR *caps,
                                     VkVideoDecodeCapabilitiesKHR *dec_caps,
                                     int cur_profile)
{
    VkVideoDecodeUsageInfoKHR *usage = &prof->usage;
    VkVideoProfileInfoKHR *profile = &prof->profile;
    VkVideoProfileListInfoKHR *profile_list = &prof->profile_list;

    VkVideoDecodeH264ProfileInfoKHR *h264_profile = &prof->h264_profile;
    VkVideoDecodeH265ProfileInfoKHR *h265_profile = &prof->h265_profile;
    VkVideoDecodeAV1ProfileInfoKHR *av1_profile  = &prof->av1_profile;

    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(avctx->sw_pix_fmt);
    if (!desc)
        return AVERROR(EINVAL);

    if (avctx->codec_id == AV_CODEC_ID_H264) {
        dec_caps->pNext = h264_caps;
        usage->pNext = h264_profile;
        h264_profile->sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_PROFILE_INFO_KHR;

        /* Vulkan transmits all the constrant_set flags, rather than wanting them
         * merged in the profile IDC */
        h264_profile->stdProfileIdc = cur_profile & ~(AV_PROFILE_H264_CONSTRAINED |
                                                      AV_PROFILE_H264_INTRA);

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
        av1_profile->sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_AV1_PROFILE_INFO_KHR;
        av1_profile->stdProfile = cur_profile;
        av1_profile->filmGrainSupport = !(avctx->export_side_data & AV_CODEC_EXPORT_DATA_FILM_GRAIN);
    }

    usage->sType           = VK_STRUCTURE_TYPE_VIDEO_DECODE_USAGE_INFO_KHR;
    usage->videoUsageHints = VK_VIDEO_DECODE_USAGE_DEFAULT_KHR;

    profile->sType               = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR;
    profile->pNext               = usage;
    profile->videoCodecOperation = vk_desc->decode_op;
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
    const FFVulkanDecodeDescriptor *vk_desc = get_codecdesc(avctx->codec_id);
    AVHWFramesContext *frames = (AVHWFramesContext *)frames_ref->data;
    AVHWDeviceContext *device = (AVHWDeviceContext *)frames->device_ref->data;
    AVVulkanDeviceContext *hwctx = device->hwctx;
    enum AVPixelFormat source_format;
    enum AVPixelFormat best_format;
    VkFormat best_vkfmt;

    FFVulkanDecodeContext *dec = avctx->internal->hwaccel_priv_data;
    FFVulkanDecodeShared *ctx = dec->shared_ctx;
    FFVulkanFunctions *vk = &ctx->s.vkfn;

    VkVideoCapabilitiesKHR *caps = &ctx->caps;
    VkVideoDecodeCapabilitiesKHR *dec_caps = &ctx->dec_caps;

    VkVideoDecodeH264CapabilitiesKHR h264_caps = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_CAPABILITIES_KHR,
    };
    VkVideoDecodeH265CapabilitiesKHR h265_caps = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_CAPABILITIES_KHR,
    };
    VkVideoDecodeAV1CapabilitiesKHR av1_caps = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_AV1_CAPABILITIES_KHR,
    };

    VkPhysicalDeviceVideoFormatInfoKHR fmt_info = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_FORMAT_INFO_KHR,
        .pNext = &prof->profile_list,
    };
    VkVideoFormatPropertiesKHR *ret_info;
    uint32_t nb_out_fmts = 0;

    if (!(vk_desc->decode_extension & ctx->s.extensions)) {
        av_log(avctx, AV_LOG_ERROR, "Device does not support decoding %s!\n",
               avcodec_get_name(avctx->codec_id));
        return AVERROR(ENOSYS);
    }

    cur_profile = avctx->profile;
    base_profile = avctx->codec_id == AV_CODEC_ID_H264 ? AV_PROFILE_H264_CONSTRAINED_BASELINE :
                   avctx->codec_id == AV_CODEC_ID_H265 ? AV_PROFILE_HEVC_MAIN :
                   avctx->codec_id == AV_CODEC_ID_AV1  ? STD_VIDEO_AV1_PROFILE_MAIN :
                   0;

    ret = vulkan_setup_profile(avctx, prof, hwctx, vk, vk_desc,
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
        ret = vulkan_setup_profile(avctx, prof, hwctx, vk, vk_desc,
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

    max_level = avctx->codec_id == AV_CODEC_ID_H264 ? ff_vk_h264_level_to_av(h264_caps.maxLevelIdc) :
                avctx->codec_id == AV_CODEC_ID_H265 ? ff_vk_h265_level_to_av(h265_caps.maxLevelIdc) :
                avctx->codec_id == AV_CODEC_ID_AV1  ? av1_caps.maxLevel :
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
    av_log(avctx, AV_LOG_VERBOSE, "    Codec header name: '%s' (driver), '%s' (compiled)\n",
           caps->stdHeaderVersion.extensionName,
           vk_desc->ext_props.extensionName);
    av_log(avctx, AV_LOG_VERBOSE, "    Codec header version: %i.%i.%i (driver), %i.%i.%i (compiled)\n",
           CODEC_VER(caps->stdHeaderVersion.specVersion),
           CODEC_VER(vk_desc->ext_props.specVersion));
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
    if (avctx->coded_width  < caps->minCodedExtent.width   ||
        avctx->coded_height < caps->minCodedExtent.height  ||
        avctx->coded_width  > caps->maxCodedExtent.width   ||
        avctx->coded_height > caps->maxCodedExtent.height)
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
    }

    dec->dedicated_dpb = !(dec_caps->flags & VK_VIDEO_DECODE_CAPABILITY_DPB_AND_OUTPUT_COINCIDE_BIT_KHR);
    ctx->common.layered_dpb = !dec->dedicated_dpb ? 0 :
                              !(caps->flags & VK_VIDEO_CAPABILITY_SEPARATE_REFERENCE_IMAGES_BIT_KHR);

    if (dec->dedicated_dpb) {
        fmt_info.imageUsage = VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR;
    } else {
        fmt_info.imageUsage = VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR |
                              VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR |
                              VK_IMAGE_USAGE_TRANSFER_SRC_BIT         |
                              VK_IMAGE_USAGE_SAMPLED_BIT;

        if (ctx->s.extensions & (FF_VK_EXT_VIDEO_ENCODE_QUEUE |
                                 FF_VK_EXT_VIDEO_MAINTENANCE_1))
            fmt_info.imageUsage |= VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR;
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
    VkFormat vkfmt = VK_FORMAT_UNDEFINED;
    int err, dedicated_dpb;
    AVHWFramesContext *frames_ctx = (AVHWFramesContext*)hw_frames_ctx->data;
    AVVulkanFramesContext *hwfc = frames_ctx->hwctx;
    FFVulkanDecodeContext *dec = avctx->internal->hwaccel_priv_data;
    FFVulkanDecodeProfileData *prof = NULL;

    err = vulkan_decode_bootstrap(avctx, hw_frames_ctx);
    if (err < 0)
        return err;

    frames_ctx->sw_format = avctx->sw_pix_fmt;

    if (!DECODER_IS_SDR(avctx->codec_id)) {
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

        hwfc->create_pnext = &prof->profile_list;
    } else {
        switch (frames_ctx->sw_format) {
        case AV_PIX_FMT_GBRAP16:
            /* This should be more efficient for downloading and using */
            frames_ctx->sw_format = AV_PIX_FMT_RGBA64;
            break;
        case AV_PIX_FMT_GBRP10:
            /* This saves memory bandwidth when downloading */
            frames_ctx->sw_format = AV_PIX_FMT_X2BGR10;
            break;
        case AV_PIX_FMT_BGR0:
            /* mpv has issues with bgr0 mapping, so just remap it */
            frames_ctx->sw_format = AV_PIX_FMT_RGB0;
            break;
        default:
            break;
        }
    }

    frames_ctx->width  = avctx->coded_width;
    frames_ctx->height = avctx->coded_height;
    frames_ctx->format = AV_PIX_FMT_VULKAN;

    hwfc->format[0]    = vkfmt;
    hwfc->tiling       = VK_IMAGE_TILING_OPTIMAL;
    hwfc->usage        = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                         VK_IMAGE_USAGE_STORAGE_BIT      |
                         VK_IMAGE_USAGE_SAMPLED_BIT;

    if (prof) {
        FFVulkanDecodeShared *ctx;

        hwfc->usage |= VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR;
        if (!dec->dedicated_dpb)
            hwfc->usage |= VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR;

        ctx = dec->shared_ctx;
        if (ctx->s.extensions & (FF_VK_EXT_VIDEO_ENCODE_QUEUE |
                                 FF_VK_EXT_VIDEO_MAINTENANCE_1))
            hwfc->usage |= VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR;
    }

    return err;
}

static void vk_decode_free_params(void *opaque, uint8_t *data)
{
    FFVulkanDecodeShared *ctx = opaque;
    FFVulkanFunctions *vk = &ctx->s.vkfn;
    VkVideoSessionParametersKHR *par = (VkVideoSessionParametersKHR *)data;
    vk->DestroyVideoSessionParametersKHR(ctx->s.hwctx->act_dev, *par,
                                         ctx->s.hwctx->alloc);
    av_free(par);
}

int ff_vk_decode_create_params(AVBufferRef **par_ref, void *logctx, FFVulkanDecodeShared *ctx,
                               const VkVideoSessionParametersCreateInfoKHR *session_params_create)
{
    VkVideoSessionParametersKHR *par = av_malloc(sizeof(*par));
    const FFVulkanFunctions *vk = &ctx->s.vkfn;
    VkResult ret;

    if (!par)
        return AVERROR(ENOMEM);

    /* Create session parameters */
    ret = vk->CreateVideoSessionParametersKHR(ctx->s.hwctx->act_dev, session_params_create,
                                              ctx->s.hwctx->alloc, par);
    if (ret != VK_SUCCESS) {
        av_log(logctx, AV_LOG_ERROR, "Unable to create Vulkan video session parameters: %s!\n",
               ff_vk_ret2str(ret));
        av_free(par);
        return AVERROR_EXTERNAL;
    }
    *par_ref = av_buffer_create((uint8_t *)par, sizeof(*par),
                                vk_decode_free_params, ctx, 0);
    if (!*par_ref) {
        vk_decode_free_params(ctx, (uint8_t *)par);
        return AVERROR(ENOMEM);
    }

    return 0;
}

int ff_vk_decode_uninit(AVCodecContext *avctx)
{
    FFVulkanDecodeContext *dec = avctx->internal->hwaccel_priv_data;

    av_freep(&dec->hevc_headers);
    av_buffer_unref(&dec->session_params);
    av_refstruct_unref(&dec->shared_ctx);
    av_freep(&dec->slice_off);
    return 0;
}

static int create_empty_session_parameters(AVCodecContext *avctx,
                                           FFVulkanDecodeShared *ctx)
{
    VkResult ret;
    FFVulkanContext *s = &ctx->s;
    FFVulkanFunctions *vk = &s->vkfn;

    VkVideoDecodeH264SessionParametersCreateInfoKHR h264_params = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_SESSION_PARAMETERS_CREATE_INFO_KHR,
    };
    VkVideoDecodeH265SessionParametersCreateInfoKHR h265_params = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_SESSION_PARAMETERS_CREATE_INFO_KHR,
    };
    StdVideoAV1SequenceHeader av1_empty_seq = { 0 };
    VkVideoDecodeAV1SessionParametersCreateInfoKHR av1_params = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_AV1_SESSION_PARAMETERS_CREATE_INFO_KHR,
        .pStdSequenceHeader = &av1_empty_seq,
    };
    VkVideoSessionParametersCreateInfoKHR session_params_create = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_PARAMETERS_CREATE_INFO_KHR,
        .pNext = avctx->codec_id == AV_CODEC_ID_H264 ? (void *)&h264_params :
                 avctx->codec_id == AV_CODEC_ID_HEVC ? (void *)&h265_params :
                 avctx->codec_id == AV_CODEC_ID_AV1  ? (void *)&av1_params  :
                 NULL,
        .videoSession = ctx->common.session,
    };

    ret = vk->CreateVideoSessionParametersKHR(s->hwctx->act_dev, &session_params_create,
                                              s->hwctx->alloc, &ctx->empty_session_params);
    if (ret != VK_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Unable to create empty Vulkan video session parameters: %s!\n",
               ff_vk_ret2str(ret));
        return AVERROR_EXTERNAL;
    }

    return 0;
}

int ff_vk_decode_init(AVCodecContext *avctx)
{
    int err;
    FFVulkanDecodeContext *dec = avctx->internal->hwaccel_priv_data;
    FFVulkanDecodeShared *ctx;
    FFVulkanContext *s;
    int async_depth;
    const VkVideoProfileInfoKHR *profile;
    const FFVulkanDecodeDescriptor *vk_desc;
    const VkPhysicalDeviceDriverProperties *driver_props;

    VkVideoSessionCreateInfoKHR session_create = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_CREATE_INFO_KHR,
    };

    err = ff_decode_get_hw_frames_ctx(avctx, AV_HWDEVICE_TYPE_VULKAN);
    if (err < 0)
        return err;

    /* Initialize contexts */
    ctx = dec->shared_ctx;
    s = &ctx->s;

    err = ff_vk_init(s, avctx, NULL, avctx->hw_frames_ctx);
    if (err < 0)
        return err;

    vk_desc = get_codecdesc(avctx->codec_id);

    profile = get_video_profile(ctx, avctx->codec_id);
    if ((vk_desc->queue_flags & VK_QUEUE_VIDEO_DECODE_BIT_KHR) && !profile) {
        av_log(avctx, AV_LOG_ERROR, "Video profile missing from frames context!");
        return AVERROR(EINVAL);
    }

    /* Create queue context */
    vk_desc = get_codecdesc(avctx->codec_id);
    ctx->qf = ff_vk_qf_find(s, vk_desc->queue_flags, vk_desc->decode_op);
    if (!ctx->qf) {
        av_log(avctx, AV_LOG_ERROR, "Decoding of %s is not supported by this device\n",
               avcodec_get_name(avctx->codec_id));
        return err;
    }

    session_create.queueFamilyIndex = ctx->qf->idx;
    session_create.maxCodedExtent = ctx->caps.maxCodedExtent;
    session_create.maxDpbSlots = ctx->caps.maxDpbSlots;
    session_create.maxActiveReferencePictures = ctx->caps.maxActiveReferencePictures;
    session_create.pictureFormat = s->hwfc->format[0];
    session_create.referencePictureFormat = session_create.pictureFormat;
    session_create.pStdHeaderVersion = &vk_desc->ext_props;
    session_create.pVideoProfile = profile;
#ifdef VK_KHR_video_maintenance2
    if (ctx->s.extensions & FF_VK_EXT_VIDEO_MAINTENANCE_2)
        session_create.flags = VK_VIDEO_SESSION_CREATE_INLINE_SESSION_PARAMETERS_BIT_KHR;
#endif

    /* Create decode exec context for this specific main thread.
     * 2 async contexts per thread was experimentally determined to be optimal
     * for a majority of streams. */
    async_depth = 2*ctx->qf->num;
    /* We don't need more than 2 per thread context */
    async_depth = FFMIN(async_depth, 2*avctx->thread_count);
    /* Make sure there are enough async contexts for each thread */
    async_depth = FFMAX(async_depth, avctx->thread_count);

    err = ff_vk_exec_pool_init(s, ctx->qf, &ctx->exec_pool,
                               async_depth, 0, 0, 0, profile);
    if (err < 0)
        goto fail;

    if (!DECODER_IS_SDR(avctx->codec_id)) {
        err = ff_vk_video_common_init(avctx, s, &ctx->common, &session_create);
        if (err < 0)
            goto fail;
    }

    /* If doing an out-of-place decoding, create a DPB pool */
    if (dec->dedicated_dpb || avctx->codec_id == AV_CODEC_ID_AV1) {
        AVHWFramesContext *dpb_frames;
        AVVulkanFramesContext *dpb_hwfc;

        ctx->common.dpb_hwfc_ref = av_hwframe_ctx_alloc(s->frames->device_ref);
        if (!ctx->common.dpb_hwfc_ref) {
            err = AVERROR(ENOMEM);
            goto fail;
        }

        dpb_frames = (AVHWFramesContext *)ctx->common.dpb_hwfc_ref->data;
        dpb_frames->format    = s->frames->format;
        dpb_frames->sw_format = s->frames->sw_format;
        dpb_frames->width     = avctx->coded_width;
        dpb_frames->height    = avctx->coded_height;

        dpb_hwfc = dpb_frames->hwctx;
        dpb_hwfc->create_pnext = (void *)ff_vk_find_struct(ctx->s.hwfc->create_pnext,
                                                           VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR);
        dpb_hwfc->format[0]    = s->hwfc->format[0];
        dpb_hwfc->tiling       = VK_IMAGE_TILING_OPTIMAL;
        dpb_hwfc->usage        = VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR |
                                 VK_IMAGE_USAGE_SAMPLED_BIT; /* Shuts validator up. */

        if (ctx->common.layered_dpb)
            dpb_hwfc->nb_layers = ctx->caps.maxDpbSlots;

        err = av_hwframe_ctx_init(ctx->common.dpb_hwfc_ref);
        if (err < 0)
            goto fail;

        if (ctx->common.layered_dpb) {
            ctx->common.layered_frame = vk_get_dpb_pool(ctx);
            if (!ctx->common.layered_frame) {
                err = AVERROR(ENOMEM);
                goto fail;
            }

            err = ff_vk_create_view(&ctx->s, &ctx->common,
                                    &ctx->common.layered_view,
                                    &ctx->common.layered_aspect,
                                    (AVVkFrame *)ctx->common.layered_frame->data[0],
                                    s->hwfc->format[0], 1);
            if (err < 0)
                goto fail;
        }
    }

    if (!DECODER_IS_SDR(avctx->codec_id)) {
        if (!(ctx->s.extensions & FF_VK_EXT_VIDEO_MAINTENANCE_2)) {
            err = create_empty_session_parameters(avctx, ctx);
            if (err < 0)
                return err;
        }
    } else {
        /* For SDR decoders, this alignment value will be 0. Since this will make
         * add_slice() malfunction, set it to a sane default value. */
        ctx->caps.minBitstreamBufferSizeAlignment = AV_INPUT_BUFFER_PADDING_SIZE;
    }

    driver_props = &dec->shared_ctx->s.driver_props;
    if (driver_props->driverID == VK_DRIVER_ID_NVIDIA_PROPRIETARY &&
        driver_props->conformanceVersion.major == 1 &&
        driver_props->conformanceVersion.minor == 3 &&
        driver_props->conformanceVersion.subminor == 8 &&
        driver_props->conformanceVersion.patch < 3)
        dec->quirk_av1_offset = 1;

    ff_vk_decode_flush(avctx);

    av_log(avctx, AV_LOG_VERBOSE, "Vulkan decoder initialization sucessful\n");

    return 0;

fail:
    ff_vk_decode_uninit(avctx);

    return err;
}
