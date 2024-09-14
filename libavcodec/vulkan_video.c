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

StdVideoH264LevelIdc ff_vk_h264_level_to_vk(int level_idc)
{
    switch (level_idc) {
    case 10: return STD_VIDEO_H264_LEVEL_IDC_1_0;
    case 11: return STD_VIDEO_H264_LEVEL_IDC_1_1;
    case 12: return STD_VIDEO_H264_LEVEL_IDC_1_2;
    case 13: return STD_VIDEO_H264_LEVEL_IDC_1_3;
    case 20: return STD_VIDEO_H264_LEVEL_IDC_2_0;
    case 21: return STD_VIDEO_H264_LEVEL_IDC_2_1;
    case 22: return STD_VIDEO_H264_LEVEL_IDC_2_2;
    case 30: return STD_VIDEO_H264_LEVEL_IDC_3_0;
    case 31: return STD_VIDEO_H264_LEVEL_IDC_3_1;
    case 32: return STD_VIDEO_H264_LEVEL_IDC_3_2;
    case 40: return STD_VIDEO_H264_LEVEL_IDC_4_0;
    case 41: return STD_VIDEO_H264_LEVEL_IDC_4_1;
    case 42: return STD_VIDEO_H264_LEVEL_IDC_4_2;
    case 50: return STD_VIDEO_H264_LEVEL_IDC_5_0;
    case 51: return STD_VIDEO_H264_LEVEL_IDC_5_1;
    case 52: return STD_VIDEO_H264_LEVEL_IDC_5_2;
    case 60: return STD_VIDEO_H264_LEVEL_IDC_6_0;
    case 61: return STD_VIDEO_H264_LEVEL_IDC_6_1;
    default:
    case 62: return STD_VIDEO_H264_LEVEL_IDC_6_2;
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

StdVideoH265LevelIdc ff_vk_h265_level_to_vk(int level_idc)
{
    switch (level_idc) {
    case 10: return STD_VIDEO_H265_LEVEL_IDC_1_0;
    case 20: return STD_VIDEO_H265_LEVEL_IDC_2_0;
    case 21: return STD_VIDEO_H265_LEVEL_IDC_2_1;
    case 30: return STD_VIDEO_H265_LEVEL_IDC_3_0;
    case 31: return STD_VIDEO_H265_LEVEL_IDC_3_1;
    case 40: return STD_VIDEO_H265_LEVEL_IDC_4_0;
    case 41: return STD_VIDEO_H265_LEVEL_IDC_4_1;
    case 50: return STD_VIDEO_H265_LEVEL_IDC_5_0;
    case 51: return STD_VIDEO_H265_LEVEL_IDC_5_1;
    case 60: return STD_VIDEO_H265_LEVEL_IDC_6_0;
    case 61: return STD_VIDEO_H265_LEVEL_IDC_6_1;
    default:
    case 62: return STD_VIDEO_H265_LEVEL_IDC_6_2;
    }
}

StdVideoH264ProfileIdc ff_vk_h264_profile_to_vk(int profile)
{
    switch (profile) {
    case AV_PROFILE_H264_CONSTRAINED_BASELINE: return STD_VIDEO_H264_PROFILE_IDC_BASELINE;
    case AV_PROFILE_H264_MAIN: return STD_VIDEO_H264_PROFILE_IDC_MAIN;
    case AV_PROFILE_H264_HIGH: return STD_VIDEO_H264_PROFILE_IDC_HIGH;
    case AV_PROFILE_H264_HIGH_444_PREDICTIVE: return STD_VIDEO_H264_PROFILE_IDC_HIGH_444_PREDICTIVE;
    default: return STD_VIDEO_H264_PROFILE_IDC_INVALID;
    }
}

StdVideoH265ProfileIdc ff_vk_h265_profile_to_vk(int profile)
{
    switch (profile) {
    case AV_PROFILE_HEVC_MAIN:    return STD_VIDEO_H265_PROFILE_IDC_MAIN;
    case AV_PROFILE_HEVC_MAIN_10: return STD_VIDEO_H265_PROFILE_IDC_MAIN_10;
    case AV_PROFILE_HEVC_REXT:    return STD_VIDEO_H265_PROFILE_IDC_FORMAT_RANGE_EXTENSIONS;
    default: return STD_VIDEO_H265_PROFILE_IDC_INVALID;
    }
}

int ff_vk_h264_profile_to_av(StdVideoH264ProfileIdc profile)
{
    switch (profile) {
    case STD_VIDEO_H264_PROFILE_IDC_BASELINE: return AV_PROFILE_H264_CONSTRAINED_BASELINE;
    case STD_VIDEO_H264_PROFILE_IDC_MAIN: return AV_PROFILE_H264_MAIN;
    case STD_VIDEO_H264_PROFILE_IDC_HIGH: return AV_PROFILE_H264_HIGH;
    case STD_VIDEO_H264_PROFILE_IDC_HIGH_444_PREDICTIVE: return AV_PROFILE_H264_HIGH_444_PREDICTIVE;
    default: return AV_PROFILE_UNKNOWN;
    }
}

int ff_vk_h265_profile_to_av(StdVideoH264ProfileIdc profile)
{
    switch (profile) {
    case STD_VIDEO_H265_PROFILE_IDC_MAIN: return AV_PROFILE_HEVC_MAIN;
    case STD_VIDEO_H265_PROFILE_IDC_MAIN_10: return AV_PROFILE_HEVC_MAIN_10;
    case STD_VIDEO_H265_PROFILE_IDC_FORMAT_RANGE_EXTENSIONS: return AV_PROFILE_HEVC_REXT;
    default: return AV_PROFILE_UNKNOWN;
    }
}

int ff_vk_video_qf_init(FFVulkanContext *s, FFVkQueueFamilyCtx *qf,
                        VkQueueFlagBits family, VkVideoCodecOperationFlagBitsKHR caps)
{
    for (int i = 0; i < s->hwctx->nb_qf; i++) {
        if ((s->hwctx->qf[i].flags & family) &&
            (s->hwctx->qf[i].video_caps & caps)) {
            qf->queue_family = s->hwctx->qf[i].idx;
            qf->nb_queues = s->hwctx->qf[i].num;
            return 0;
        }
    }
    return AVERROR(ENOTSUP);
}

int ff_vk_create_view(FFVulkanContext *s, FFVkVideoCommon *common,
                      VkImageView *view, VkImageAspectFlags *aspect,
                      AVVkFrame *src, VkFormat vkf, int is_dpb)
{
    VkResult ret;
    FFVulkanFunctions *vk = &s->vkfn;
    VkImageAspectFlags aspect_mask = ff_vk_aspect_bits_from_vkfmt(vkf);

    VkSamplerYcbcrConversionInfo yuv_sampler_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO,
        .conversion = common->yuv_sampler,
    };
    VkImageViewCreateInfo img_view_create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = &yuv_sampler_info,
        .viewType = common->layered_dpb && is_dpb ?
                    VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D,
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
            .layerCount     = common->layered_dpb && is_dpb ?
                              VK_REMAINING_ARRAY_LAYERS : 1,
            .levelCount     = 1,
        },
    };

    ret = vk->CreateImageView(s->hwctx->act_dev, &img_view_create_info,
                              s->hwctx->alloc, view);
    if (ret != VK_SUCCESS)
        return AVERROR_EXTERNAL;

    *aspect = aspect_mask;

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

    if (common->layered_view)
        vk->DestroyImageView(s->hwctx->act_dev, common->layered_view,
                             s->hwctx->alloc);

    av_frame_free(&common->layered_frame);

    av_buffer_unref(&common->dpb_hwfc_ref);

    if (common->yuv_sampler)
        vk->DestroySamplerYcbcrConversion(s->hwctx->act_dev, common->yuv_sampler,
                                          s->hwctx->alloc);
}

av_cold int ff_vk_video_common_init(AVCodecContext *avctx, FFVulkanContext *s,
                                    FFVkVideoCommon *common,
                                    VkVideoSessionCreateInfoKHR *session_create)
{
    int err;
    VkResult ret;
    FFVulkanFunctions *vk = &s->vkfn;
    VkVideoSessionMemoryRequirementsKHR *mem = NULL;
    VkBindVideoSessionMemoryInfoKHR *bind_mem = NULL;

    int cxpos = 0, cypos = 0;
    VkSamplerYcbcrConversionCreateInfo yuv_sampler_info = {
        .sType      = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO,
        .components = ff_comp_identity_map,
        .ycbcrModel = VK_SAMPLER_YCBCR_MODEL_CONVERSION_RGB_IDENTITY,
        .ycbcrRange = avctx->color_range == AVCOL_RANGE_MPEG, /* Ignored */
        .format     = session_create->pictureFormat,
    };

    /* Create identity YUV sampler
     * (VkImageViews of YUV image formats require it, even if it does nothing) */
    av_chroma_location_enum_to_pos(&cxpos, &cypos, avctx->chroma_sample_location);
    yuv_sampler_info.xChromaOffset = cxpos >> 7;
    yuv_sampler_info.yChromaOffset = cypos >> 7;
    ret = vk->CreateSamplerYcbcrConversion(s->hwctx->act_dev, &yuv_sampler_info,
                                           s->hwctx->alloc, &common->yuv_sampler);
    if (ret != VK_SUCCESS)
        return AVERROR_EXTERNAL;

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

        av_log(avctx, AV_LOG_VERBOSE, "Allocating %"PRIu64" bytes in bind index %i for video session\n",
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
