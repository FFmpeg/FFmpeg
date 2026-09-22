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
#include "encode.h"
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

    /* Single plane 422 at 8, 10, 12 and 16 bits */
    { VK_FORMAT_G8B8G8R8_422_UNORM,                     AV_PIX_FMT_YUYV422, VK_IMAGE_ASPECT_COLOR_BIT },
    { VK_FORMAT_B8G8R8G8_422_UNORM,                     AV_PIX_FMT_UYVY422, VK_IMAGE_ASPECT_COLOR_BIT },
    { VK_FORMAT_G10X6B10X6G10X6R10X6_422_UNORM_4PACK16, AV_PIX_FMT_Y210,    VK_IMAGE_ASPECT_COLOR_BIT },
    { VK_FORMAT_G12X4B12X4G12X4R12X4_422_UNORM_4PACK16, AV_PIX_FMT_Y212,    VK_IMAGE_ASPECT_COLOR_BIT },
    { VK_FORMAT_G16B16G16R16_422_UNORM,                 AV_PIX_FMT_Y216,    VK_IMAGE_ASPECT_COLOR_BIT },

    /* Single plane 444 at 10 and 12 bits */
    { VK_FORMAT_A2R10G10B10_UNORM_PACK32,               AV_PIX_FMT_XV30,    VK_IMAGE_ASPECT_COLOR_BIT },
    { VK_FORMAT_R12X4G12X4B12X4A12X4_UNORM_4PACK16,     AV_PIX_FMT_XV36,    VK_IMAGE_ASPECT_COLOR_BIT },
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
    else if (desc->log2_chroma_w == 1 && !desc->log2_chroma_h)
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

StdVideoAV1Level ff_vk_av1_level_to_vk(int level)
{
    switch (level) {
    case 20: return STD_VIDEO_AV1_LEVEL_2_0;
    case 21: return STD_VIDEO_AV1_LEVEL_2_1;
    case 22: return STD_VIDEO_AV1_LEVEL_2_2;
    case 23: return STD_VIDEO_AV1_LEVEL_2_3;
    case 30: return STD_VIDEO_AV1_LEVEL_3_0;
    case 31: return STD_VIDEO_AV1_LEVEL_3_1;
    case 32: return STD_VIDEO_AV1_LEVEL_3_2;
    case 33: return STD_VIDEO_AV1_LEVEL_3_3;
    case 40: return STD_VIDEO_AV1_LEVEL_4_0;
    case 41: return STD_VIDEO_AV1_LEVEL_4_1;
    case 42: return STD_VIDEO_AV1_LEVEL_4_2;
    case 43: return STD_VIDEO_AV1_LEVEL_4_3;
    case 50: return STD_VIDEO_AV1_LEVEL_5_0;
    case 51: return STD_VIDEO_AV1_LEVEL_5_1;
    case 52: return STD_VIDEO_AV1_LEVEL_5_2;
    case 53: return STD_VIDEO_AV1_LEVEL_5_3;
    case 60: return STD_VIDEO_AV1_LEVEL_6_0;
    case 61: return STD_VIDEO_AV1_LEVEL_6_1;
    case 62: return STD_VIDEO_AV1_LEVEL_6_2;
    case 63: return STD_VIDEO_AV1_LEVEL_6_3;
    case 70: return STD_VIDEO_AV1_LEVEL_7_0;
    case 71: return STD_VIDEO_AV1_LEVEL_7_1;
    case 72: return STD_VIDEO_AV1_LEVEL_7_2;
    default:
    case 73: return STD_VIDEO_AV1_LEVEL_7_3;
    }
}

StdVideoH264ProfileIdc ff_vk_h264_profile_to_vk(int profile)
{
    switch (profile) {
    case AV_PROFILE_H264_BASELINE:
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

StdVideoAV1Profile ff_vk_av1_profile_to_vk(int profile)
{
    switch (profile) {
    case AV_PROFILE_AV1_MAIN: return STD_VIDEO_AV1_PROFILE_MAIN;
    case AV_PROFILE_AV1_HIGH: return STD_VIDEO_AV1_PROFILE_HIGH;
    case AV_PROFILE_AV1_PROFESSIONAL: return STD_VIDEO_AV1_PROFILE_PROFESSIONAL;
    default: return STD_VIDEO_AV1_PROFILE_INVALID;
    }
}

int ff_vk_create_view(FFVulkanContext *s, VkImageView *view,
                      VkImageAspectFlags *aspect, VkImage img,
                      VkFormat vkf, VkImageUsageFlags usage, int layered)
{
    VkResult ret;
    FFVulkanFunctions *vk = &s->vkfn;
    VkImageAspectFlags aspect_mask = ff_vk_aspect_bits_from_vkfmt(vkf);

    VkImageViewUsageCreateInfo usage_create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO,
        .usage = usage,
    };
    VkImageViewCreateInfo img_view_create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = &usage_create_info,
        .viewType = layered ? VK_IMAGE_VIEW_TYPE_2D_ARRAY :
                              VK_IMAGE_VIEW_TYPE_2D,
        .format = vkf,
        .image = img,
        .components = (VkComponentMapping) {
            .r = VK_COMPONENT_SWIZZLE_IDENTITY,
            .g = VK_COMPONENT_SWIZZLE_IDENTITY,
            .b = VK_COMPONENT_SWIZZLE_IDENTITY,
            .a = VK_COMPONENT_SWIZZLE_IDENTITY,
        },
        .subresourceRange = (VkImageSubresourceRange) {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseArrayLayer = 0,
            .layerCount     = layered ? VK_REMAINING_ARRAY_LAYERS : 1,
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

static void dpb_image_free(AVRefStructOpaque opaque, void *obj)
{
    FFVkVideoDPB *dpb = opaque.nc;
    FFVkVideoDPBImage *di = obj;

    if (di->view)
        dpb->destroy_image_view(dpb->dev, di->view, dpb->alloc);
    if (di->img)
        dpb->destroy_image(dpb->dev, di->img, dpb->alloc);
    if (di->mem)
        dpb->free_memory(dpb->dev, di->mem, dpb->alloc);
}

static int dpb_image_init(AVRefStructOpaque opaque, void *obj)
{
    int err;
    FFVkVideoDPB *dpb = opaque.nc;
    FFVkVideoDPBImage *di = obj;

    err = ff_vk_image_create(dpb->s, &di->img, &di->mem,
                             dpb->width, dpb->height, dpb->format,
                             dpb->nb_layers, dpb->tiling, dpb->usage,
                             0x0, dpb->create_pnext);
    if (err < 0)
        return err;

    err = ff_vk_create_view(dpb->s, &di->view, &di->aspect, di->img,
                            dpb->format, dpb->usage, dpb->nb_layers > 1);
    if (err < 0) {
        ff_vk_image_free(dpb->s, &di->img, &di->mem);
        return err;
    }

    di->layout = VK_IMAGE_LAYOUT_UNDEFINED;

    return 0;
}

static void dpb_pool_free(AVRefStructOpaque opaque)
{
    av_free(opaque.nc);
}

av_cold int ff_vk_video_dpb_init(FFVulkanContext *s, FFVkVideoCommon *common,
                                 VkFormat format, VkImageUsageFlags usage,
                                 VkImageTiling tiling, void *create_pnext,
                                 int width, int height, int nb_layers)
{
    FFVulkanFunctions *vk = &s->vkfn;
    FFVkVideoDPB *dpb = av_mallocz(sizeof(*dpb));
    if (!dpb)
        return AVERROR(ENOMEM);

    dpb->s            = s;
    dpb->format       = format;
    dpb->usage        = usage;
    dpb->tiling       = tiling;
    dpb->create_pnext = create_pnext;
    dpb->width        = width;
    dpb->height       = height;
    dpb->nb_layers    = nb_layers;

    dpb->dev                = s->hwctx->act_dev;
    dpb->alloc              = s->hwctx->alloc;
    dpb->destroy_image_view = vk->DestroyImageView;
    dpb->destroy_image      = vk->DestroyImage;
    dpb->free_memory        = vk->FreeMemory;

    dpb->img_pool = av_refstruct_pool_alloc_ext(sizeof(FFVkVideoDPBImage), 0,
                                                dpb, dpb_image_init, NULL,
                                                dpb_image_free, dpb_pool_free);
    if (!dpb->img_pool) {
        av_free(dpb);
        return AVERROR(ENOMEM);
    }

    common->dpb = dpb;

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

    /* The layered view is owned by the pool entry */
    common->layered_view = VK_NULL_HANDLE;
    av_refstruct_unref(&common->layered_img);

    if (common->dpb) {
        /* The pool frees the FFVkVideoDPB once its last entry returns */
        av_refstruct_pool_uninit(&common->dpb->img_pool);
        common->dpb = NULL;
    }
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

    /* Create session */
    ret = vk->CreateVideoSessionKHR(s->hwctx->act_dev, session_create,
                                    s->hwctx->alloc, &common->session);
    if (ret != VK_SUCCESS) {
        err = AVERROR_EXTERNAL;
        goto fail;
    }

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

typedef struct SegGatherPushData {
    VkDeviceAddress sparse;
    VkDeviceAddress compacted;
    uint32_t        slot_size;
} SegGatherPushData;

extern const unsigned char ff_seg_gather_comp_spv_data[];
extern const unsigned int ff_seg_gather_comp_spv_len;

int ff_vk_seg_gather_init(FFVulkanContext *s, FFVkExecPool *pool,
                          FFVulkanShader *shd)
{
    int err;
    FFVulkanDescriptorSetBinding desc_set[] = {
        {
            .name   = "sizes_buf",
            .type   = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };

    ff_vk_shader_load(shd, VK_SHADER_STAGE_COMPUTE_BIT, NULL,
                      (uint32_t []) { 256, 1, 1 }, 0);
    ff_vk_shader_add_push_const(shd, 0, sizeof(SegGatherPushData),
                                VK_SHADER_STAGE_COMPUTE_BIT);
    ff_vk_shader_add_descriptor_set(s, shd, desc_set, 1, 0);

    RET(ff_vk_shader_link(s, shd, ff_seg_gather_comp_spv_data,
                          ff_seg_gather_comp_spv_len, "main"));
    RET(ff_vk_shader_register_exec(s, pool, shd));

fail:
    return err;
}

int ff_vk_seg_gather(FFVulkanContext *s, FFVkExecContext *exec, FFVulkanShader *shd,
                     FFVkBuffer *sizes, size_t sizes_offset, uint32_t nb_segs,
                     FFVkBuffer *sparse, uint32_t slot_size,
                     FFVkBuffer *compacted, size_t compacted_offset)
{
    int err;
    FFVulkanFunctions *vk = &s->vkfn;
    SegGatherPushData pd = {
        .sparse    = sparse->address,
        .compacted = compacted->address + compacted_offset,
        .slot_size = slot_size,
    };

    vk->CmdPipelineBarrier2(exec->buf, &(VkDependencyInfo) {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pMemoryBarriers = &(VkMemoryBarrier2) {
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT |
                             VK_ACCESS_2_SHADER_WRITE_BIT,
        },
        .memoryBarrierCount = 1,
    });

    RET(ff_vk_shader_update_desc_buffer(s, exec, shd, 0, 0, 0,
                                        sizes, sizes_offset, (nb_segs + 1)*sizeof(uint32_t),
                                        VK_FORMAT_UNDEFINED));
    ff_vk_exec_bind_shader(s, exec, shd);
    ff_vk_shader_update_push_const(s, exec, shd, VK_SHADER_STAGE_COMPUTE_BIT,
                                   0, sizeof(pd), &pd);
    vk->CmdDispatch(exec->buf, nb_segs, 1, 1);

    /* For the host to read the output and the packed size */
    vk->CmdPipelineBarrier2(exec->buf, &(VkDependencyInfo) {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pMemoryBarriers = &(VkMemoryBarrier2) {
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT,
            .dstAccessMask = VK_ACCESS_2_HOST_READ_BIT,
        },
        .memoryBarrierCount = 1,
    });

fail:
    return err;
}

int ff_vk_encode_loop_init(FFVulkanContext *s, FFVkExecPool *pool, FFVkEncodeLoop *l,
                           int (*submit_frame)(AVCodecContext *avctx, FFVkExecContext *exec, AVFrame *frame),
                           int (*get_packet)(AVCodecContext *avctx, FFVkExecContext *exec, AVPacket *pkt))
{
    l->s            = s;
    l->pool         = pool;
    l->submit_frame = submit_frame;
    l->get_packet   = get_packet;
    l->head         = 0;
    l->in_flight    = 0;

    l->frames = av_calloc(pool->pool_size, sizeof(*l->frames));
    l->frame  = av_frame_alloc();
    l->pkt    = av_packet_alloc();
    if (!l->frames || !l->frame || !l->pkt)
        return AVERROR(ENOMEM);

    return 0;
}

static int loop_get_packet(AVCodecContext *avctx, FFVkEncodeLoop *l, AVPacket *pkt)
{
    int err;
    int idx = (l->head + l->pool->pool_size - l->in_flight) % l->pool->pool_size;

    l->in_flight--;

    err = l->get_packet(avctx, &l->pool->contexts[idx], pkt);
    if (err < 0) {
        av_buffer_unref(&l->frames[idx].opaque_ref);
        return err;
    }

    pkt->pts      = l->frames[idx].pts;
    pkt->dts      = l->frames[idx].pts;
    pkt->duration = l->frames[idx].duration;
    if (avctx->flags & AV_CODEC_FLAG_COPY_OPAQUE) {
        pkt->opaque     = l->frames[idx].opaque;
        pkt->opaque_ref = l->frames[idx].opaque_ref;
        l->frames[idx].opaque_ref = NULL;
    }

    return 0;
}

static int loop_oldest_done(FFVkEncodeLoop *l)
{
    FFVulkanFunctions *vk = &l->s->vkfn;
    int idx = (l->head + l->pool->pool_size - l->in_flight) % l->pool->pool_size;
    FFVkExecContext *e = &l->pool->contexts[idx];
    uint64_t val;

    return vk->GetSemaphoreCounterValue(l->s->hwctx->act_dev, e->sem, &val) == VK_SUCCESS &&
           val >= e->sem_value;
}

int ff_vk_encode_loop_receive_packet(AVCodecContext *avctx, FFVkEncodeLoop *l,
                                     AVPacket *pkt)
{
    int err, eof = 0;
    FFVkExecPool *pool = l->pool;

    err = ff_encode_get_frame(avctx, l->frame);
    if (err == AVERROR_EOF) {
        eof = 1;
    } else if (err >= 0) {
        l->frames[l->head].pts      = l->frame->pts;
        l->frames[l->head].duration = l->frame->duration;
        if (avctx->flags & AV_CODEC_FLAG_COPY_OPAQUE) {
            l->frames[l->head].opaque     = l->frame->opaque;
            l->frames[l->head].opaque_ref = l->frame->opaque_ref;
            l->frame->opaque_ref = NULL;
        }

        err = l->submit_frame(avctx, &pool->contexts[l->head], l->frame);
        av_frame_unref(l->frame);
        if (err < 0) {
            av_buffer_unref(&l->frames[l->head].opaque_ref);
            return err;
        }

        l->head = (l->head + 1) % pool->pool_size;
        l->in_flight++;
    } else if (err != AVERROR(EAGAIN)) {
        return err;
    }

    if (!l->in_flight)
        return eof ? AVERROR_EOF : AVERROR(EAGAIN);
    if (l->in_flight < pool->pool_size && !eof && !loop_oldest_done(l))
        return AVERROR(EAGAIN);

    return loop_get_packet(avctx, l, pkt);
}

void ff_vk_encode_loop_flush(AVCodecContext *avctx, FFVkEncodeLoop *l)
{
    while (l->in_flight) {
        if (loop_get_packet(avctx, l, l->pkt) >= 0)
            av_packet_unref(l->pkt);
    }
    l->head = 0;
}

void ff_vk_encode_loop_uninit(FFVkEncodeLoop *l)
{
    if (l->frames) {
        for (int i = 0; i < l->pool->pool_size; i++)
            av_buffer_unref(&l->frames[i].opaque_ref);
        av_freep(&l->frames);
    }
    av_frame_free(&l->frame);
    av_packet_free(&l->pkt);
}
