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

#ifndef AVCODEC_VULKAN_VIDEO_H
#define AVCODEC_VULKAN_VIDEO_H

#include "avcodec.h"
#include "libavutil/refstruct.h"
#include "libavutil/vulkan.h"

#include <vk_video/vulkan_video_codecs_common.h>

#define CODEC_VER_MAJ(ver) (ver >> 22)
#define CODEC_VER_MIN(ver) ((ver >> 12) & ((1 << 10) - 1))
#define CODEC_VER_PAT(ver) (ver & ((1 << 12) - 1))
#define CODEC_VER(ver) CODEC_VER_MAJ(ver), CODEC_VER_MIN(ver), CODEC_VER_PAT(ver)

/* DEDICATED-mode queue-exclusive DPB image */
typedef struct FFVkVideoDPBImage {
    VkImage img;
    VkDeviceMemory mem;
    VkImageView view;
    VkImageAspectFlags aspect;
    VkImageLayout layout;
} FFVkVideoDPBImage;

/* Internal DPB image pool; av_refstruct_pool_get()/av_refstruct_unref() */
typedef struct FFVkVideoDPB {
    /* Creation only; destruction uses the stashed handles below, as the
     * pool may outlive the context */
    FFVulkanContext *s;

    AVRefStructPool *img_pool; /* FFVkVideoDPBImage entries */

    VkDevice dev;
    const VkAllocationCallbacks *alloc;
    PFN_vkDestroyImageView destroy_image_view;
    PFN_vkDestroyImage destroy_image;
    PFN_vkFreeMemory free_memory;

    VkFormat format;
    VkImageUsageFlags usage;
    VkImageTiling tiling;
    void *create_pnext;
    int width, height, nb_layers;
} FFVkVideoDPB;

typedef struct FFVkVideoSession {
    VkVideoSessionKHR session;
    VkDeviceMemory *mem;
    uint32_t nb_mem;

    FFVkVideoDPB *dpb;
    int layered_dpb;
    FFVkVideoDPBImage *layered_img;
    VkImageView layered_view;
    VkImageAspectFlags layered_aspect;
} FFVkVideoCommon;

/**
 * Get pixfmt from a Vulkan format.
 */
enum AVPixelFormat ff_vk_pix_fmt_from_vkfmt(VkFormat vkf);

/**
 * Get aspect bits which include all planes from a VkFormat.
 */
VkImageAspectFlags ff_vk_aspect_bits_from_vkfmt(VkFormat vkf);

/**
 * Get Vulkan's chroma subsampling from a pixfmt descriptor.
 */
VkVideoChromaSubsamplingFlagBitsKHR ff_vk_subsampling_from_av_desc(const AVPixFmtDescriptor *desc);

/**
 * Get Vulkan's bit depth from an [8:12] integer.
 */
VkVideoComponentBitDepthFlagBitsKHR ff_vk_depth_from_av_depth(int depth);

/**
 * Convert level from Vulkan to AV.
 */
int ff_vk_h264_level_to_av(StdVideoH264LevelIdc level);
int ff_vk_h265_level_to_av(StdVideoH265LevelIdc level);

StdVideoH264LevelIdc ff_vk_h264_level_to_vk(int level_idc);
StdVideoH265LevelIdc ff_vk_h265_level_to_vk(int level_idc);
StdVideoAV1Level     ff_vk_av1_level_to_vk(int level);

/**
 * Convert profile from/to AV to Vulkan
 */
StdVideoH264ProfileIdc ff_vk_h264_profile_to_vk(int profile);
StdVideoH265ProfileIdc ff_vk_h265_profile_to_vk(int profile);
StdVideoAV1Profile     ff_vk_av1_profile_to_vk(int profile);

/**
 * Creates image views for video frames.
 */
int ff_vk_create_view(FFVulkanContext *s, VkImageView *view,
                      VkImageAspectFlags *aspect, VkImage img,
                      VkFormat vkf, VkImageUsageFlags usage, int layered);

/**
 * Initialize the internal DPB image pool.
 */
int ff_vk_video_dpb_init(FFVulkanContext *s, FFVkVideoCommon *common,
                         VkFormat format, VkImageUsageFlags usage,
                         VkImageTiling tiling, void *create_pnext,
                         int width, int height, int nb_layers);

/**
 * Initialize video session, allocating and binding necessary memory.
 */
int ff_vk_video_common_init(AVCodecContext *avctx, FFVulkanContext *s,
                            FFVkVideoCommon *common,
                            VkVideoSessionCreateInfoKHR *session_create);

/**
 * Free video session and required resources.
 */
void ff_vk_video_common_uninit(FFVulkanContext *s, FFVkVideoCommon *common);

/**
 * Packs fixed-stride segment slots back to back into a contiguous buffer.
 */
int ff_vk_seg_gather_init(FFVulkanContext *s, FFVkExecPool *pool,
                          FFVulkanShader *shd);

/**
 * Gathers nb_segs slots of slot_size bytes from sparse into compacted, with
 * the segment sizes given as nb_segs uint32_t in sizes at sizes_offset,
 * followed by one more that receives the packed size. Both are made visible
 * to the host.
 */
int ff_vk_seg_gather(FFVulkanContext *s, FFVkExecContext *exec, FFVulkanShader *shd,
                     FFVkBuffer *sizes, size_t sizes_offset, uint32_t nb_segs,
                     FFVkBuffer *sparse, uint32_t slot_size,
                     FFVkBuffer *compacted, size_t compacted_offset);

/**
 * Frame loop for compute encoders. Keeps up to pool_size frames in flight,
 * submitting each into its own execution context, and returns their packets
 * in order as soon as they complete, or once the pool is full.
 */
typedef struct FFVkEncodeLoop {
    FFVulkanContext *s;
    FFVkExecPool *pool;
    int (*submit_frame)(AVCodecContext *avctx, FFVkExecContext *exec, AVFrame *frame);
    int (*get_packet)(AVCodecContext *avctx, FFVkExecContext *exec, AVPacket *pkt);

    AVFrame *frame;
    AVPacket *pkt;
    struct {
        int64_t pts;
        int64_t duration;
        void *opaque;
        AVBufferRef *opaque_ref;
    } *frames;
    int head;
    int in_flight;
} FFVkEncodeLoop;

int ff_vk_encode_loop_init(FFVulkanContext *s, FFVkExecPool *pool, FFVkEncodeLoop *l,
                           int (*submit_frame)(AVCodecContext *avctx, FFVkExecContext *exec, AVFrame *frame),
                           int (*get_packet)(AVCodecContext *avctx, FFVkExecContext *exec, AVPacket *pkt));

/**
 * Call from FFCodec.cb.receive_packet; the packet metadata is carried from
 * the submitted frame to its packet.
 */
int ff_vk_encode_loop_receive_packet(AVCodecContext *avctx, FFVkEncodeLoop *l,
                                     AVPacket *pkt);

/**
 * Waits for and discards every frame in flight.
 */
void ff_vk_encode_loop_flush(AVCodecContext *avctx, FFVkEncodeLoop *l);

void ff_vk_encode_loop_uninit(FFVkEncodeLoop *l);

#endif /* AVCODEC_VULKAN_VIDEO_H */
