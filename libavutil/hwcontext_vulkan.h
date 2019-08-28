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

#ifndef AVUTIL_HWCONTEXT_VULKAN_H
#define AVUTIL_HWCONTEXT_VULKAN_H

#include <vulkan/vulkan.h>

/**
 * @file
 * API-specific header for AV_HWDEVICE_TYPE_VULKAN.
 *
 * For user-allocated pools, AVHWFramesContext.pool must return AVBufferRefs
 * with the data pointer set to an AVVkFrame.
 */

/**
 * Main Vulkan context, allocated as AVHWDeviceContext.hwctx.
 * All of these can be set before init to change what the context uses
 */
typedef struct AVVulkanDeviceContext {
    /**
     * Custom memory allocator, else NULL
     */
    const VkAllocationCallbacks *alloc;
    /**
     * Instance
     */
    VkInstance inst;
    /**
     * Physical device
     */
    VkPhysicalDevice phys_dev;
    /**
     * Active device
     */
    VkDevice act_dev;
    /**
     * Queue family index for graphics
     * @note av_hwdevice_create() will set all 3 queue indices if unset
     * If there is no dedicated queue for compute or transfer operations,
     * they will be set to the graphics queue index which can handle both.
     */
    int queue_family_index;
    /**
     * Queue family index for transfer ops only
     */
    int queue_family_tx_index;
    /**
     * Queue family index for compute ops
     */
    int queue_family_comp_index;
} AVVulkanDeviceContext;

/**
 * Allocated as AVHWFramesContext.hwctx, used to set pool-specific options
 */
typedef struct AVVulkanFramesContext {
    /**
     * Controls the tiling of output frames.
     */
    VkImageTiling tiling;
    /**
     * Defines extra usage of output frames. This is bitwise OR'd with the
     * standard usage flags (SAMPLED, STORAGE, TRANSFER_SRC and TRANSFER_DST).
     */
    VkImageUsageFlagBits usage;
    /**
     * Extension data for image creation. By default, if the extension is
     * available, this will be chained to a VkImageFormatListCreateInfoKHR.
     */
    void *create_pnext;
    /**
     * Extension data for memory allocation. Must have as many entries as
     * the number of planes of the sw_format.
     * This will be chained to VkExportMemoryAllocateInfo, which is used
     * to make all pool images exportable to other APIs.
     */
    void *alloc_pnext[AV_NUM_DATA_POINTERS];
} AVVulkanFramesContext;

/*
 * Frame structure, the VkFormat of the image will always match
 * the pool's sw_format.
 * All frames, imported or allocated, will be created with the
 * VK_IMAGE_CREATE_ALIAS_BIT flag set, so the memory may be aliased if needed.
 *
 * @note the size of this structure is not part of the ABI, to allocate
 * you must use @av_vk_frame_alloc().
 */
typedef struct AVVkFrame {
    /**
     * Vulkan images to which the memory is bound to.
     */
    VkImage img[AV_NUM_DATA_POINTERS];

    /**
     * Same tiling must be used for all images.
     */
    VkImageTiling tiling;

    /**
     * Memory backing the images. Could be less than the amount of images
     * if importing from a DRM or VAAPI frame.
     */
    VkDeviceMemory mem[AV_NUM_DATA_POINTERS];
    size_t size[AV_NUM_DATA_POINTERS];

    /**
     * OR'd flags for all memory allocated
     */
    VkMemoryPropertyFlagBits flags;

    /**
     * Updated after every barrier
     */
    VkAccessFlagBits access[AV_NUM_DATA_POINTERS];
    VkImageLayout layout[AV_NUM_DATA_POINTERS];

    /**
     * Per-image semaphores. Must not be freed manually. Must be waited on
     * and signalled at every queue submission.
     */
    VkSemaphore sem[AV_NUM_DATA_POINTERS];

    /**
     * Internal data.
     */
    struct AVVkFrameInternal *internal;
} AVVkFrame;

/**
 * Allocates a single AVVkFrame and initializes everything as 0.
 * @note Must be freed via av_free()
 */
AVVkFrame *av_vk_frame_alloc(void);

/**
 * Returns the format of each image up to the number of planes for a given sw_format.
 */
const VkFormat *av_vkfmt_from_pixfmt(enum AVPixelFormat p);

#endif /* AVUTIL_HWCONTEXT_VULKAN_H */
