/*
 * Copyright (c) 2025 Lynne <dev@lynne.ee>
 *
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

#include "vulkan_decode.h"
#include "hwaccel_internal.h"

#include "dpx.h"
#include "libavutil/mem.h"

extern const unsigned char ff_dpx_unpack_comp_spv_data[];
extern const unsigned int ff_dpx_unpack_comp_spv_len;

extern const unsigned char ff_dpx_copy_comp_spv_data[];
extern const unsigned int ff_dpx_copy_comp_spv_len;

const FFVulkanDecodeDescriptor ff_vk_dec_dpx_desc = {
    .codec_id         = AV_CODEC_ID_DPX,
    .queue_flags      = VK_QUEUE_COMPUTE_BIT,
};

typedef struct DPXVulkanDecodePicture {
    FFVulkanDecodePicture vp;
} DPXVulkanDecodePicture;

typedef struct DPXVulkanDecodeContext {
    FFVulkanShader shader;
    AVBufferPool *frame_data_pool;
} DPXVulkanDecodeContext;

typedef struct DecodePushData {
    int bits_per_comp;
    int nb_comp;
    int nb_images;
    int stride;
    int need_align;
    int padded_10bit;
    int shift;
} DecodePushData;

static int host_upload_image(AVCodecContext *avctx,
                             FFVulkanDecodeContext *dec, DPXDecContext *dpx,
                             const uint8_t *src, uint32_t size)
{
    int err;
    VkImage temp;

    FFVulkanDecodeShared *ctx = dec->shared_ctx;
    DPXVulkanDecodeContext *dxv = ctx->sd_ctx;
    VkPhysicalDeviceLimits *limits = &ctx->s.props.properties.limits;
    FFVulkanFunctions *vk = &ctx->s.vkfn;

    DPXVulkanDecodePicture *pp = dpx->hwaccel_picture_private;
    FFVulkanDecodePicture *vp = &pp->vp;

    int unpack = (avctx->bits_per_raw_sample == 12 && !dpx->packing) ||
                 avctx->bits_per_raw_sample == 10;
    if (unpack)
        return 0;

    VkImageCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = avctx->bits_per_raw_sample == 8 ? VK_FORMAT_R8_UINT :
                  avctx->bits_per_raw_sample == 32 ? VK_FORMAT_R32_UINT :
                                                     VK_FORMAT_R16_UINT,
        .extent.width = dpx->frame->width*dpx->components,
        .extent.height = dpx->frame->height,
        .extent.depth = 1,
        .mipLevels = 1,
        .arrayLayers = 1,
        .tiling = VK_IMAGE_TILING_LINEAR,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_HOST_TRANSFER_BIT_EXT,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .pQueueFamilyIndices = &ctx->qf[0].idx,
        .queueFamilyIndexCount = 1,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    if (create_info.extent.width >= limits->maxImageDimension2D ||
        create_info.extent.height >= limits->maxImageDimension2D)
        return 0;

    vk->CreateImage(ctx->s.hwctx->act_dev, &create_info, ctx->s.hwctx->alloc,
                    &temp);

    err = ff_vk_get_pooled_buffer(&ctx->s, &dxv->frame_data_pool,
                                  &vp->slices_buf,
                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                  NULL, size,
                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    if (err < 0)
        return err;

    FFVkBuffer *vkb = (FFVkBuffer *)vp->slices_buf->data;
    VkBindImageMemoryInfo bind_info = {
        .sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO,
        .image = temp,
        .memory = vkb->mem,
    };
    vk->BindImageMemory2(ctx->s.hwctx->act_dev, 1, &bind_info);

    VkHostImageLayoutTransitionInfo layout_change = {
        .sType = VK_STRUCTURE_TYPE_HOST_IMAGE_LAYOUT_TRANSITION_INFO,
        .image = temp,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .subresourceRange.layerCount = 1,
        .subresourceRange.levelCount = 1,
    };
    vk->TransitionImageLayoutEXT(ctx->s.hwctx->act_dev, 1, &layout_change);

    VkMemoryToImageCopy copy_region = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_TO_IMAGE_COPY,
        .pHostPointer = src,
        .imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .imageSubresource.layerCount = 1,
        .imageExtent = (VkExtent3D){ dpx->frame->width*dpx->components,
                                     dpx->frame->height,
                                     1 },
    };
    VkCopyMemoryToImageInfo copy_info = {
        .sType = VK_STRUCTURE_TYPE_COPY_MEMORY_TO_IMAGE_INFO,
        .flags = VK_HOST_IMAGE_COPY_MEMCPY_EXT,
        .dstImage = temp,
        .dstImageLayout = VK_IMAGE_LAYOUT_GENERAL,
        .regionCount = 1,
        .pRegions = &copy_region,
    };
    vk->CopyMemoryToImageEXT(ctx->s.hwctx->act_dev, &copy_info);

    vk->DestroyImage(ctx->s.hwctx->act_dev, temp, ctx->s.hwctx->alloc);

    return 0;
}

static int vk_dpx_start_frame(AVCodecContext          *avctx,
                              const AVBufferRef       *buffer_ref,
                              av_unused const uint8_t *buffer,
                              av_unused uint32_t       size)
{
    int err;
    FFVulkanDecodeContext *dec = avctx->internal->hwaccel_priv_data;
    FFVulkanDecodeShared *ctx = dec->shared_ctx;
    DPXDecContext *dpx = avctx->priv_data;

    DPXVulkanDecodePicture *pp = dpx->hwaccel_picture_private;
    FFVulkanDecodePicture *vp = &pp->vp;

    if (ctx->s.extensions & FF_VK_EXT_HOST_IMAGE_COPY)
        host_upload_image(avctx, dec, dpx, buffer, size);

    /* Host map the frame data if supported */
    if (!vp->slices_buf &&
        ctx->s.extensions & FF_VK_EXT_EXTERNAL_HOST_MEMORY)
        ff_vk_host_map_buffer(&ctx->s, &vp->slices_buf, (uint8_t *)buffer,
                              buffer_ref,
                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                              VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);

    /* Prepare frame to be used */
    err = ff_vk_decode_prepare_frame_sdr(dec, dpx->frame, vp, 1,
                                         FF_VK_REP_NATIVE, 0);
    if (err < 0)
        return err;

    return 0;
}

static int vk_dpx_decode_slice(AVCodecContext *avctx,
                               const uint8_t  *data,
                               uint32_t        size)
{
    DPXDecContext *dpx = avctx->priv_data;

    DPXVulkanDecodePicture *pp = dpx->hwaccel_picture_private;
    FFVulkanDecodePicture *vp = &pp->vp;

    if (!vp->slices_buf) {
        int err = ff_vk_decode_add_slice(avctx, vp, data, size, 0,
                                         NULL, NULL);
        if (err < 0)
            return err;
    }

    return 0;
}

static int vk_dpx_end_frame(AVCodecContext *avctx)
{
    int err;
    FFVulkanDecodeContext *dec = avctx->internal->hwaccel_priv_data;
    FFVulkanDecodeShared *ctx = dec->shared_ctx;
    FFVulkanFunctions *vk = &ctx->s.vkfn;

    DPXDecContext *dpx = avctx->priv_data;
    DPXVulkanDecodeContext *dxv = ctx->sd_ctx;

    DPXVulkanDecodePicture *pp = dpx->hwaccel_picture_private;
    FFVulkanDecodePicture *vp = &pp->vp;

    int unpack = (avctx->bits_per_raw_sample == 12 && !dpx->packing) ||
                 avctx->bits_per_raw_sample == 10;

    FFVkBuffer *slices_buf = (FFVkBuffer *)vp->slices_buf->data;

    VkImageMemoryBarrier2 img_bar[8];
    int nb_img_bar = 0;

    FFVkExecContext *exec = ff_vk_exec_get(&ctx->s, &ctx->exec_pool);
    ff_vk_exec_start(&ctx->s, exec);

    /* Prepare deps */
    RET(ff_vk_exec_add_dep_frame(&ctx->s, exec, dpx->frame,
                                 VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT));

    err = ff_vk_exec_mirror_sem_value(&ctx->s, exec, &vp->sem, &vp->sem_value,
                                      dpx->frame);
    if (err < 0)
        return err;

    RET(ff_vk_exec_add_dep_buf(&ctx->s, exec, &vp->slices_buf, 1, 0));
    vp->slices_buf = NULL;

    AVVkFrame *vkf = (AVVkFrame *)dpx->frame->data[0];
    for (int i = 0; i < 4; i++) {
        vkf->layout[i] = VK_IMAGE_LAYOUT_UNDEFINED;
        vkf->access[i] = VK_ACCESS_2_NONE;
    }

    ff_vk_frame_barrier(&ctx->s, exec, dpx->frame, img_bar, &nb_img_bar,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                        VK_IMAGE_LAYOUT_GENERAL,
                        VK_QUEUE_FAMILY_IGNORED);

    vk->CmdPipelineBarrier2(exec->buf, &(VkDependencyInfo) {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pImageMemoryBarriers = img_bar,
        .imageMemoryBarrierCount = nb_img_bar,
    });
    nb_img_bar = 0;

    FFVulkanShader *shd = &dxv->shader;
    ff_vk_shader_update_img_array(&ctx->s, exec, shd,
                                  dpx->frame, vp->view.out,
                                  0, 0,
                                  VK_IMAGE_LAYOUT_GENERAL,
                                  VK_NULL_HANDLE);
    ff_vk_shader_update_desc_buffer(&ctx->s, exec, shd,
                                    0, 1, 0,
                                    slices_buf,
                                    0, slices_buf->size,
                                    VK_FORMAT_UNDEFINED);
    if (!unpack) {
        ff_vk_shader_update_desc_buffer(&ctx->s, exec, shd,
                                        0, 2, 0,
                                        slices_buf,
                                        0, slices_buf->size,
                                        VK_FORMAT_UNDEFINED);
        ff_vk_shader_update_desc_buffer(&ctx->s, exec, shd,
                                        0, 3, 0,
                                        slices_buf,
                                        0, slices_buf->size,
                                        VK_FORMAT_UNDEFINED);
    }

    ff_vk_exec_bind_shader(&ctx->s, exec, shd);

    /* Update push data */
    DecodePushData pd = (DecodePushData) {
        .bits_per_comp = avctx->bits_per_raw_sample,
        .nb_comp = dpx->components,
        .nb_images = ff_vk_count_images(vkf),
        .stride = dpx->stride,
        .need_align = dpx->need_align,
        .padded_10bit = !dpx->unpadded_10bit,
        .shift = FFALIGN(avctx->bits_per_raw_sample, 8) -
                 avctx->bits_per_raw_sample,
    };

    ff_vk_shader_update_push_const(&ctx->s, exec, shd,
                                   VK_SHADER_STAGE_COMPUTE_BIT,
                                   0, sizeof(pd), &pd);

    vk->CmdDispatch(exec->buf,
                    FFALIGN(dpx->frame->width,  shd->lg_size[0])/shd->lg_size[0],
                    FFALIGN(dpx->frame->height, shd->lg_size[1])/shd->lg_size[1],
                    1);

    err = ff_vk_exec_submit(&ctx->s, exec);
    if (err < 0)
        return err;

fail:
    return 0;
}

static int init_shader(AVCodecContext *avctx, FFVulkanContext *s,
                       FFVkExecPool *pool, FFVulkanShader *shd, int bits)
{
    int err;
    DPXDecContext *dpx = avctx->priv_data;
    AVHWFramesContext *dec_frames_ctx;
    dec_frames_ctx = (AVHWFramesContext *)avctx->hw_frames_ctx->data;

    int unpack = (avctx->bits_per_raw_sample == 12 && !dpx->packing) ||
                 avctx->bits_per_raw_sample == 10;

    SPEC_LIST_CREATE(sl, 2, 2*sizeof(uint32_t))
    SPEC_LIST_ADD(sl, 0, 32, dpx->endian && bits > 8); /* big endian */
    if (unpack)
        SPEC_LIST_ADD(sl, 1, 32, bits == 10); /* packed_10bit */
    else
        SPEC_LIST_ADD(sl, 1, 32, FFALIGN(bits, 8)); /* type_bits */

    ff_vk_shader_load(shd, VK_SHADER_STAGE_COMPUTE_BIT, sl,
                      (uint32_t []) { 512, 1, 1 }, 0);

    ff_vk_shader_add_push_const(shd, 0, sizeof(DecodePushData),
                                VK_SHADER_STAGE_COMPUTE_BIT);

    const FFVulkanDescriptorSetBinding desc_set[] = {
        {
            .name       = "dst",
            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
            .elems      = av_pix_fmt_count_planes(dec_frames_ctx->sw_format),
        },
        {
            .name        = "data_buf",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        {
            .name        = "data_buf16",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        {
            .name        = "data_buf32",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };
    ff_vk_shader_add_descriptor_set(s, shd, desc_set, 2 + (2*!unpack), 0, 0);

    const unsigned char *src = ff_dpx_copy_comp_spv_data;
    size_t src_len = ff_dpx_copy_comp_spv_len;
    if (unpack) {
        src = ff_dpx_unpack_comp_spv_data;
        src_len = ff_dpx_unpack_comp_spv_len;
    }
    RET(ff_vk_shader_link(s, shd, src, src_len, "main"));

    RET(ff_vk_shader_register_exec(s, pool, shd));

fail:
    return err;
}

static void vk_decode_dpx_uninit(FFVulkanDecodeShared *ctx)
{
    DPXVulkanDecodeContext *fv = ctx->sd_ctx;

    ff_vk_shader_free(&ctx->s, &fv->shader);

    av_buffer_pool_uninit(&fv->frame_data_pool);

    av_freep(&fv);
}

static int vk_decode_dpx_init(AVCodecContext *avctx)
{
    int err;
    DPXDecContext *dpx = avctx->priv_data;
    FFVulkanDecodeContext *dec = avctx->internal->hwaccel_priv_data;

    switch (dpx->pix_fmt) {
    case AV_PIX_FMT_GRAY10:
    case AV_PIX_FMT_GBRAP10:
    case AV_PIX_FMT_UYVY422:
    case AV_PIX_FMT_YUV444P:
    case AV_PIX_FMT_YUVA444P:
        return AVERROR(ENOTSUP);
    case AV_PIX_FMT_GBRP10:
        if (dpx->unpadded_10bit)
            return AVERROR(ENOTSUP);
    /* fallthrough */
    default:
        break;
    }

    err = ff_vk_decode_init(avctx);
    if (err < 0)
        return err;

    FFVulkanDecodeShared *ctx = dec->shared_ctx;
    DPXVulkanDecodeContext *dxv = ctx->sd_ctx = av_mallocz(sizeof(*dxv));
    if (!dxv)
        return AVERROR(ENOMEM);

    ctx->sd_ctx_free = &vk_decode_dpx_uninit;

    RET(init_shader(avctx, &ctx->s, &ctx->exec_pool,
                    &dxv->shader, avctx->bits_per_raw_sample));

fail:
    return err;
}

static void vk_dpx_free_frame_priv(AVRefStructOpaque _hwctx, void *data)
{
    AVHWDeviceContext *dev_ctx = _hwctx.nc;

    DPXVulkanDecodePicture *pp = data;
    FFVulkanDecodePicture *vp = &pp->vp;

    ff_vk_decode_free_frame(dev_ctx, vp);
}

const FFHWAccel ff_dpx_vulkan_hwaccel = {
    .p.name                = "dpx_vulkan",
    .p.type                = AVMEDIA_TYPE_VIDEO,
    .p.id                  = AV_CODEC_ID_DPX,
    .p.pix_fmt             = AV_PIX_FMT_VULKAN,
    .start_frame           = &vk_dpx_start_frame,
    .decode_slice          = &vk_dpx_decode_slice,
    .end_frame             = &vk_dpx_end_frame,
    .free_frame_priv       = &vk_dpx_free_frame_priv,
    .frame_priv_data_size  = sizeof(DPXVulkanDecodePicture),
    .init                  = &vk_decode_dpx_init,
    .update_thread_context = &ff_vk_update_thread_context,
    .uninit                = &ff_vk_decode_uninit,
    .frame_params          = &ff_vk_frame_params,
    .priv_data_size        = sizeof(FFVulkanDecodeContext),
    .caps_internal         = HWACCEL_CAP_ASYNC_SAFE | HWACCEL_CAP_THREAD_SAFE,
};
