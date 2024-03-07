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

#define ASPECT_2PLANE (VK_IMAGE_ASPECT_PLANE_0_BIT | VK_IMAGE_ASPECT_PLANE_1_BIT)
#define ASPECT_3PLANE (VK_IMAGE_ASPECT_PLANE_0_BIT | VK_IMAGE_ASPECT_PLANE_1_BIT | VK_IMAGE_ASPECT_PLANE_2_BIT)

static const struct FFVkFormatMapEntry {
    VkFormat vkf;
    enum AVPixelFormat pixfmt;
    VkImageAspectFlags aspect;
} vk_format_map[] = {
    /* Gray formats */
    { VK_FORMAT_R8_UNORM,   AV_PIX_FMT_GRAY8,   VK_IMAGE_ASPECT_COLOR_BIT },
    { VK_FORMAT_R16_UNORM,  AV_PIX_FMT_GRAY16,  VK_IMAGE_ASPECT_COLOR_BIT },
    { VK_FORMAT_R32_SFLOAT, AV_PIX_FMT_GRAYF32, VK_IMAGE_ASPECT_COLOR_BIT },

    /* RGB formats */
    { VK_FORMAT_R16G16B16A16_UNORM,       AV_PIX_FMT_XV36,    VK_IMAGE_ASPECT_COLOR_BIT },
    { VK_FORMAT_B8G8R8A8_UNORM,           AV_PIX_FMT_BGRA,    VK_IMAGE_ASPECT_COLOR_BIT },
    { VK_FORMAT_R8G8B8A8_UNORM,           AV_PIX_FMT_RGBA,    VK_IMAGE_ASPECT_COLOR_BIT },
    { VK_FORMAT_R8G8B8_UNORM,             AV_PIX_FMT_RGB24,   VK_IMAGE_ASPECT_COLOR_BIT },
    { VK_FORMAT_B8G8R8_UNORM,             AV_PIX_FMT_BGR24,   VK_IMAGE_ASPECT_COLOR_BIT },
    { VK_FORMAT_R16G16B16_UNORM,          AV_PIX_FMT_RGB48,   VK_IMAGE_ASPECT_COLOR_BIT },
    { VK_FORMAT_R16G16B16A16_UNORM,       AV_PIX_FMT_RGBA64,  VK_IMAGE_ASPECT_COLOR_BIT },
    { VK_FORMAT_R5G6B5_UNORM_PACK16,      AV_PIX_FMT_RGB565,  VK_IMAGE_ASPECT_COLOR_BIT },
    { VK_FORMAT_B5G6R5_UNORM_PACK16,      AV_PIX_FMT_BGR565,  VK_IMAGE_ASPECT_COLOR_BIT },
    { VK_FORMAT_B8G8R8A8_UNORM,           AV_PIX_FMT_BGR0,    VK_IMAGE_ASPECT_COLOR_BIT },
    { VK_FORMAT_R8G8B8A8_UNORM,           AV_PIX_FMT_RGB0,    VK_IMAGE_ASPECT_COLOR_BIT },
    { VK_FORMAT_A2R10G10B10_UNORM_PACK32, AV_PIX_FMT_X2RGB10, VK_IMAGE_ASPECT_COLOR_BIT },

    /* Planar RGB */
    { VK_FORMAT_R8_UNORM,   AV_PIX_FMT_GBRAP,    VK_IMAGE_ASPECT_COLOR_BIT },
    { VK_FORMAT_R16_UNORM,  AV_PIX_FMT_GBRAP16,  VK_IMAGE_ASPECT_COLOR_BIT },
    { VK_FORMAT_R32_SFLOAT, AV_PIX_FMT_GBRPF32,  VK_IMAGE_ASPECT_COLOR_BIT },
    { VK_FORMAT_R32_SFLOAT, AV_PIX_FMT_GBRAPF32, VK_IMAGE_ASPECT_COLOR_BIT },

    /* Two-plane 420 YUV at 8, 10, 12 and 16 bits */
    { VK_FORMAT_G8_B8R8_2PLANE_420_UNORM,                  AV_PIX_FMT_NV12, ASPECT_2PLANE },
    { VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16, AV_PIX_FMT_P010, ASPECT_2PLANE },
    { VK_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16, AV_PIX_FMT_P012, ASPECT_2PLANE },
    { VK_FORMAT_G16_B16R16_2PLANE_420_UNORM,               AV_PIX_FMT_P016, ASPECT_2PLANE },

    /* Two-plane 422 YUV at 8, 10 and 16 bits */
    { VK_FORMAT_G8_B8R8_2PLANE_422_UNORM,                  AV_PIX_FMT_NV16, ASPECT_2PLANE },
    { VK_FORMAT_G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16, AV_PIX_FMT_P210, ASPECT_2PLANE },
    { VK_FORMAT_G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16, AV_PIX_FMT_P212, ASPECT_2PLANE },
    { VK_FORMAT_G16_B16R16_2PLANE_422_UNORM,               AV_PIX_FMT_P216, ASPECT_2PLANE },

    /* Two-plane 444 YUV at 8, 10 and 16 bits */
    { VK_FORMAT_G8_B8R8_2PLANE_444_UNORM,                  AV_PIX_FMT_NV24, ASPECT_2PLANE },
    { VK_FORMAT_G10X6_B10X6R10X6_2PLANE_444_UNORM_3PACK16, AV_PIX_FMT_P410, ASPECT_2PLANE },
    { VK_FORMAT_G12X4_B12X4R12X4_2PLANE_444_UNORM_3PACK16, AV_PIX_FMT_P412, ASPECT_2PLANE },
    { VK_FORMAT_G16_B16R16_2PLANE_444_UNORM,               AV_PIX_FMT_P416, ASPECT_2PLANE },

    /* Three-plane 420, 422, 444 at 8, 10, 12 and 16 bits */
    { VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM,    AV_PIX_FMT_YUV420P,   ASPECT_3PLANE },
    { VK_FORMAT_G16_B16_R16_3PLANE_420_UNORM, AV_PIX_FMT_YUV420P10, ASPECT_3PLANE },
    { VK_FORMAT_G16_B16_R16_3PLANE_420_UNORM, AV_PIX_FMT_YUV420P12, ASPECT_3PLANE },
    { VK_FORMAT_G16_B16_R16_3PLANE_420_UNORM, AV_PIX_FMT_YUV420P16, ASPECT_3PLANE },
    { VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM,    AV_PIX_FMT_YUV422P,   ASPECT_3PLANE },
    { VK_FORMAT_G16_B16_R16_3PLANE_422_UNORM, AV_PIX_FMT_YUV422P10, ASPECT_3PLANE },
    { VK_FORMAT_G16_B16_R16_3PLANE_422_UNORM, AV_PIX_FMT_YUV422P12, ASPECT_3PLANE },
    { VK_FORMAT_G16_B16_R16_3PLANE_422_UNORM, AV_PIX_FMT_YUV422P16, ASPECT_3PLANE },
    { VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM,    AV_PIX_FMT_YUV444P,   ASPECT_3PLANE },
    { VK_FORMAT_G16_B16_R16_3PLANE_444_UNORM, AV_PIX_FMT_YUV444P10, ASPECT_3PLANE },
    { VK_FORMAT_G16_B16_R16_3PLANE_444_UNORM, AV_PIX_FMT_YUV444P12, ASPECT_3PLANE },
    { VK_FORMAT_G16_B16_R16_3PLANE_444_UNORM, AV_PIX_FMT_YUV444P16, ASPECT_3PLANE },

    /* Single plane 422 at 8, 10 and 12 bits */
    { VK_FORMAT_G8B8G8R8_422_UNORM,                     AV_PIX_FMT_YUYV422, VK_IMAGE_ASPECT_COLOR_BIT },
    { VK_FORMAT_B8G8R8G8_422_UNORM,                     AV_PIX_FMT_UYVY422, VK_IMAGE_ASPECT_COLOR_BIT },
    { VK_FORMAT_G10X6B10X6G10X6R10X6_422_UNORM_4PACK16, AV_PIX_FMT_Y210,    VK_IMAGE_ASPECT_COLOR_BIT },
    { VK_FORMAT_G12X4B12X4G12X4R12X4_422_UNORM_4PACK16, AV_PIX_FMT_Y212,    VK_IMAGE_ASPECT_COLOR_BIT },
};
static const int nb_vk_format_map = FF_ARRAY_ELEMS(vk_format_map);

enum AVPixelFormat ff_vk_pix_fmt_from_vkfmt(VkFormat vkf)
{
    for (int i = 0; i < nb_vk_format_map; i++)
        if (vk_format_map[i].vkf == vkf)
            return vk_format_map[i].pixfmt;
    return AV_PIX_FMT_NONE;
}

VkImageAspectFlags ff_vk_aspect_bits_from_vkfmt(VkFormat vkf)
{
    for (int i = 0; i < nb_vk_format_map; i++)
        if (vk_format_map[i].vkf == vkf)
            return vk_format_map[i].aspect;
    return VK_IMAGE_ASPECT_NONE;
}

VkVideoChromaSubsamplingFlagBitsKHR ff_vk_subsampling_from_av_desc(const AVPixFmtDescriptor *desc)
{
    if (desc->nb_components == 1)
        return VK_VIDEO_CHROMA_SUBSAMPLING_MONOCHROME_BIT_KHR;
    else if (!desc->log2_chroma_w && !desc->log2_chroma_h)
        return VK_VIDEO_CHROMA_SUBSAMPLING_444_BIT_KHR;
    else if (!desc->log2_chroma_w && desc->log2_chroma_h == 1)
        return VK_VIDEO_CHROMA_SUBSAMPLING_422_BIT_KHR;
    else if (desc->log2_chroma_w == 1 && desc->log2_chroma_h == 1)
        return VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR;
    return VK_VIDEO_CHROMA_SUBSAMPLING_INVALID_KHR;
}

VkVideoComponentBitDepthFlagBitsKHR ff_vk_depth_from_av_depth(int depth)
{
    switch (depth) {
    case  8: return VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
    case 10: return VK_VIDEO_COMPONENT_BIT_DEPTH_10_BIT_KHR;
    case 12: return VK_VIDEO_COMPONENT_BIT_DEPTH_12_BIT_KHR;
    default: break;
    }
    return VK_VIDEO_COMPONENT_BIT_DEPTH_INVALID_KHR;
}

int ff_vk_h264_level_to_av(StdVideoH264LevelIdc level)
{
    switch (level) {
    case STD_VIDEO_H264_LEVEL_IDC_1_0: return 10;
    case STD_VIDEO_H264_LEVEL_IDC_1_1: return 11;
    case STD_VIDEO_H264_LEVEL_IDC_1_2: return 12;
    case STD_VIDEO_H264_LEVEL_IDC_1_3: return 13;
    case STD_VIDEO_H264_LEVEL_IDC_2_0: return 20;
    case STD_VIDEO_H264_LEVEL_IDC_2_1: return 21;
    case STD_VIDEO_H264_LEVEL_IDC_2_2: return 22;
    case STD_VIDEO_H264_LEVEL_IDC_3_0: return 30;
    case STD_VIDEO_H264_LEVEL_IDC_3_1: return 31;
    case STD_VIDEO_H264_LEVEL_IDC_3_2: return 32;
    case STD_VIDEO_H264_LEVEL_IDC_4_0: return 40;
    case STD_VIDEO_H264_LEVEL_IDC_4_1: return 41;
    case STD_VIDEO_H264_LEVEL_IDC_4_2: return 42;
    case STD_VIDEO_H264_LEVEL_IDC_5_0: return 50;
    case STD_VIDEO_H264_LEVEL_IDC_5_1: return 51;
    case STD_VIDEO_H264_LEVEL_IDC_5_2: return 52;
    case STD_VIDEO_H264_LEVEL_IDC_6_0: return 60;
    case STD_VIDEO_H264_LEVEL_IDC_6_1: return 61;
    default:
    case STD_VIDEO_H264_LEVEL_IDC_6_2: return 62;
    }
}

int ff_vk_h265_level_to_av(StdVideoH265LevelIdc level)
{
    switch (level) {
    case STD_VIDEO_H265_LEVEL_IDC_1_0: return 10;
    case STD_VIDEO_H265_LEVEL_IDC_2_0: return 20;
    case STD_VIDEO_H265_LEVEL_IDC_2_1: return 21;
    case STD_VIDEO_H265_LEVEL_IDC_3_0: return 30;
    case STD_VIDEO_H265_LEVEL_IDC_3_1: return 31;
    case STD_VIDEO_H265_LEVEL_IDC_4_0: return 40;
    case STD_VIDEO_H265_LEVEL_IDC_4_1: return 41;
    case STD_VIDEO_H265_LEVEL_IDC_5_0: return 50;
    case STD_VIDEO_H265_LEVEL_IDC_5_1: return 51;
    case STD_VIDEO_H265_LEVEL_IDC_6_0: return 60;
    case STD_VIDEO_H265_LEVEL_IDC_6_1: return 61;
    default:
    case STD_VIDEO_H265_LEVEL_IDC_6_2: return 62;
    }
}

static void free_data_buf(void *opaque, uint8_t *data)
{
    FFVulkanContext *ctx = opaque;
    FFVkVideoBuffer *buf = (FFVkVideoBuffer *)data;
    ff_vk_unmap_buffer(ctx, &buf->buf, 0);
    ff_vk_free_buf(ctx, &buf->buf);
    av_free(data);
}

static AVBufferRef *alloc_data_buf(void *opaque, size_t size)
{
    AVBufferRef *ref;
    uint8_t *buf = av_mallocz(size);
    if (!buf)
        return NULL;

    ref = av_buffer_create(buf, size, free_data_buf, opaque, 0);
    if (!ref)
        av_free(buf);
    return ref;
}

int ff_vk_video_get_buffer(FFVulkanContext *ctx, FFVkVideoCommon *s,
                           AVBufferRef **buf, VkBufferUsageFlags usage,
                           void *create_pNext, size_t size)
{
    int err;
    AVBufferRef *ref;
    FFVkVideoBuffer *data;

    if (!s->buf_pool) {
        s->buf_pool = av_buffer_pool_init2(sizeof(FFVkVideoBuffer), ctx,
                                           alloc_data_buf, NULL);
        if (!s->buf_pool)
            return AVERROR(ENOMEM);
    }

    *buf = ref = av_buffer_pool_get(s->buf_pool);
    if (!ref)
        return AVERROR(ENOMEM);

    data = (FFVkVideoBuffer *)ref->data;

    if (data->buf.size >= size)
        return 0;

    /* No point in requesting anything smaller. */
    size = FFMAX(size, 1024*1024);

    /* Align buffer to nearest power of two. Makes fragmentation management
     * easier, and gives us ample headroom. */
    size--;
    size |= size >>  1;
    size |= size >>  2;
    size |= size >>  4;
    size |= size >>  8;
    size |= size >> 16;
    size++;

    ff_vk_free_buf(ctx, &data->buf);
    memset(data, 0, sizeof(FFVkVideoBuffer));

    err = ff_vk_create_buf(ctx, &data->buf, size,
                           create_pNext, NULL, usage,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    if (err < 0) {
        av_buffer_unref(&ref);
        return err;
    }

    /* Map the buffer */
    err = ff_vk_map_buffer(ctx, &data->buf, &data->mem, 0);
    if (err < 0) {
        av_buffer_unref(&ref);
        return err;
    }

    return 0;
}

av_cold void ff_vk_video_common_uninit(FFVulkanContext *s,
                                       FFVkVideoCommon *common)
{
    FFVulkanFunctions *vk = &s->vkfn;

    if (common->session) {
        vk->DestroyVideoSessionKHR(s->hwctx->act_dev, common->session,
                                   s->hwctx->alloc);
        common->session = VK_NULL_HANDLE;
    }

    if (common->nb_mem && common->mem)
        for (int i = 0; i < common->nb_mem; i++)
            vk->FreeMemory(s->hwctx->act_dev, common->mem[i], s->hwctx->alloc);

    av_freep(&common->mem);

    av_buffer_pool_uninit(&common->buf_pool);
}

av_cold int ff_vk_video_common_init(void *log, FFVulkanContext *s,
                                    FFVkVideoCommon *common,
                                    VkVideoSessionCreateInfoKHR *session_create)
{
    int err;
    VkResult ret;
    FFVulkanFunctions *vk = &s->vkfn;
    VkVideoSessionMemoryRequirementsKHR *mem = NULL;
    VkBindVideoSessionMemoryInfoKHR *bind_mem = NULL;

    /* Create session */
    ret = vk->CreateVideoSessionKHR(s->hwctx->act_dev, session_create,
                                    s->hwctx->alloc, &common->session);
    if (ret != VK_SUCCESS)
        return AVERROR_EXTERNAL;

    /* Get memory requirements */
    ret = vk->GetVideoSessionMemoryRequirementsKHR(s->hwctx->act_dev,
                                                   common->session,
                                                   &common->nb_mem,
                                                   NULL);
    if (ret != VK_SUCCESS) {
        err = AVERROR_EXTERNAL;
        goto fail;
    }

    /* Allocate all memory needed to actually allocate memory */
    common->mem = av_mallocz(sizeof(*common->mem)*common->nb_mem);
    if (!common->mem) {
        err = AVERROR(ENOMEM);
        goto fail;
    }
    mem = av_mallocz(sizeof(*mem)*common->nb_mem);
    if (!mem) {
        err = AVERROR(ENOMEM);
        goto fail;
    }
    bind_mem = av_mallocz(sizeof(*bind_mem)*common->nb_mem);
    if (!bind_mem) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    /* Set the needed fields to get the memory requirements */
    for (int i = 0; i < common->nb_mem; i++) {
        mem[i] = (VkVideoSessionMemoryRequirementsKHR) {
            .sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_MEMORY_REQUIREMENTS_KHR,
        };
    }

    /* Finally get the memory requirements */
    ret = vk->GetVideoSessionMemoryRequirementsKHR(s->hwctx->act_dev,
                                                   common->session, &common->nb_mem,
                                                   mem);
    if (ret != VK_SUCCESS) {
        err = AVERROR_EXTERNAL;
        goto fail;
    }

    /* Now allocate each requested memory.
     * For ricing, could pool together memory that ends up in the same index. */
    for (int i = 0; i < common->nb_mem; i++) {
        err = ff_vk_alloc_mem(s, &mem[i].memoryRequirements,
                              UINT32_MAX, NULL, NULL, &common->mem[i]);
        if (err < 0)
            goto fail;

        bind_mem[i] = (VkBindVideoSessionMemoryInfoKHR) {
            .sType = VK_STRUCTURE_TYPE_BIND_VIDEO_SESSION_MEMORY_INFO_KHR,
            .memory = common->mem[i],
            .memoryBindIndex = mem[i].memoryBindIndex,
            .memoryOffset = 0,
            .memorySize = mem[i].memoryRequirements.size,
        };

        av_log(log, AV_LOG_VERBOSE, "Allocating %"PRIu64" bytes in bind index %i for video session\n",
               bind_mem[i].memorySize, bind_mem[i].memoryBindIndex);
    }

    /* Bind the allocated memory */
    ret = vk->BindVideoSessionMemoryKHR(s->hwctx->act_dev, common->session,
                                        common->nb_mem, bind_mem);
    if (ret != VK_SUCCESS) {
        err = AVERROR_EXTERNAL;
        goto fail;
    }

    av_freep(&mem);
    av_freep(&bind_mem);

    return 0;

fail:
    av_freep(&mem);
    av_freep(&bind_mem);

    ff_vk_video_common_uninit(s, common);
    return err;
}
