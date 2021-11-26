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

#ifndef AVUTIL_VULKAN_FUNCTIONS_H
#define AVUTIL_VULKAN_FUNCTIONS_H

#define VK_NO_PROTOTYPES
#define VK_ENABLE_BETA_EXTENSIONS

#include "hwcontext.h"
#include "hwcontext_vulkan.h"

/* An enum of bitflags for every optional extension we need */
typedef enum FFVulkanExtensions {
    FF_VK_EXT_EXTERNAL_DMABUF_MEMORY = 1ULL <<  0, /* VK_EXT_external_memory_dma_buf */
    FF_VK_EXT_DRM_MODIFIER_FLAGS     = 1ULL <<  1, /* VK_EXT_image_drm_format_modifier */
    FF_VK_EXT_EXTERNAL_FD_MEMORY     = 1ULL <<  2, /* VK_KHR_external_memory_fd */
    FF_VK_EXT_EXTERNAL_FD_SEM        = 1ULL <<  3, /* VK_KHR_external_semaphore_fd */
    FF_VK_EXT_EXTERNAL_HOST_MEMORY   = 1ULL <<  4, /* VK_EXT_external_memory_host */
    FF_VK_EXT_DEBUG_UTILS            = 1ULL <<  5, /* VK_EXT_debug_utils */
#ifdef _WIN32
    FF_VK_EXT_EXTERNAL_WIN32_MEMORY  = 1ULL <<  6, /* VK_KHR_external_memory_win32 */
    FF_VK_EXT_EXTERNAL_WIN32_SEM     = 1ULL <<  7, /* VK_KHR_external_semaphore_win32 */
#endif

    FF_VK_EXT_NO_FLAG                = 1ULL << 63,
} FFVulkanExtensions;

/* Macro containing every function that we utilize in our codebase */
#define FN_LIST(MACRO)                                                                   \
    /* Instance */                                                                       \
    MACRO(0, 0, FF_VK_EXT_NO_FLAG,              EnumerateInstanceExtensionProperties)    \
    MACRO(0, 0, FF_VK_EXT_NO_FLAG,              EnumerateInstanceLayerProperties)        \
    MACRO(0, 0, FF_VK_EXT_NO_FLAG,              CreateInstance)                          \
    MACRO(1, 0, FF_VK_EXT_NO_FLAG,              DestroyInstance)                         \
                                                                                         \
    /* Debug */                                                                          \
    MACRO(1, 0, FF_VK_EXT_NO_FLAG,              CreateDebugUtilsMessengerEXT)            \
    MACRO(1, 0, FF_VK_EXT_NO_FLAG,              DestroyDebugUtilsMessengerEXT)           \
                                                                                         \
    /* Device */                                                                         \
    MACRO(1, 0, FF_VK_EXT_NO_FLAG,              GetDeviceProcAddr)                       \
    MACRO(1, 0, FF_VK_EXT_NO_FLAG,              CreateDevice)                            \
    MACRO(1, 0, FF_VK_EXT_NO_FLAG,              GetPhysicalDeviceFeatures2)              \
    MACRO(1, 0, FF_VK_EXT_NO_FLAG,              GetPhysicalDeviceProperties)             \
    MACRO(1, 0, FF_VK_EXT_NO_FLAG,              DeviceWaitIdle)                          \
    MACRO(1, 0, FF_VK_EXT_NO_FLAG,              DestroyDevice)                           \
                                                                                         \
    MACRO(1, 0, FF_VK_EXT_NO_FLAG,              EnumeratePhysicalDevices)                \
    MACRO(1, 0, FF_VK_EXT_NO_FLAG,              EnumerateDeviceExtensionProperties)      \
                                                                                         \
    MACRO(1, 0, FF_VK_EXT_NO_FLAG,              GetPhysicalDeviceProperties2)            \
    MACRO(1, 0, FF_VK_EXT_NO_FLAG,              GetPhysicalDeviceMemoryProperties)       \
    MACRO(1, 0, FF_VK_EXT_NO_FLAG,              GetPhysicalDeviceFormatProperties2)      \
    MACRO(1, 0, FF_VK_EXT_NO_FLAG,              GetPhysicalDeviceImageFormatProperties2) \
    MACRO(1, 0, FF_VK_EXT_NO_FLAG,              GetPhysicalDeviceQueueFamilyProperties)  \
                                                                                         \
    /* Command pool */                                                                   \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              CreateCommandPool)                       \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              DestroyCommandPool)                      \
                                                                                         \
    /* Command buffer */                                                                 \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              AllocateCommandBuffers)                  \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              BeginCommandBuffer)                      \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              EndCommandBuffer)                        \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              FreeCommandBuffers)                      \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              CmdDispatch)                             \
                                                                                         \
    /* Queue */                                                                          \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              GetDeviceQueue)                          \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              QueueSubmit)                             \
                                                                                         \
    /* Fences */                                                                         \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              CreateFence)                             \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              WaitForFences)                           \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              ResetFences)                             \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              DestroyFence)                            \
                                                                                         \
    /* Semaphores */                                                                     \
    MACRO(1, 1, FF_VK_EXT_EXTERNAL_FD_SEM,      GetSemaphoreFdKHR)                       \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              CreateSemaphore)                         \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              WaitSemaphores)                          \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              DestroySemaphore)                        \
                                                                                         \
    /* Memory */                                                                         \
    MACRO(1, 1, FF_VK_EXT_EXTERNAL_FD_MEMORY,   GetMemoryFdKHR)                          \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              GetMemoryFdPropertiesKHR)                \
    MACRO(1, 1, FF_VK_EXT_EXTERNAL_HOST_MEMORY, GetMemoryHostPointerPropertiesEXT)       \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              AllocateMemory)                          \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              MapMemory)                               \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              FlushMappedMemoryRanges)                 \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              InvalidateMappedMemoryRanges)            \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              UnmapMemory)                             \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              FreeMemory)                              \
                                                                                         \
    /* Commands */                                                                       \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              CmdBindDescriptorSets)                   \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              CmdPushConstants)                        \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              CmdBindPipeline)                         \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              CmdPipelineBarrier)                      \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              CmdCopyBufferToImage)                    \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              CmdCopyImageToBuffer)                    \
                                                                                         \
    /* Buffer */                                                                         \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              GetBufferMemoryRequirements2)            \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              CreateBuffer)                            \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              BindBufferMemory)                        \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              DestroyBuffer)                           \
                                                                                         \
    /* Image */                                                                          \
    MACRO(1, 1, FF_VK_EXT_DRM_MODIFIER_FLAGS,   GetImageDrmFormatModifierPropertiesEXT)  \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              GetImageMemoryRequirements2)             \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              CreateImage)                             \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              BindImageMemory2)                        \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              GetImageSubresourceLayout)               \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              DestroyImage)                            \
                                                                                         \
    /* ImageView */                                                                      \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              CreateImageView)                         \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              DestroyImageView)                        \
                                                                                         \
    /* DescriptorSet */                                                                  \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              CreateDescriptorSetLayout)               \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              AllocateDescriptorSets)                  \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              CreateDescriptorPool)                    \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              DestroyDescriptorPool)                   \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              DestroyDescriptorSetLayout)              \
                                                                                         \
    /* DescriptorUpdateTemplate */                                                       \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              UpdateDescriptorSetWithTemplate)         \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              CreateDescriptorUpdateTemplate)          \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              DestroyDescriptorUpdateTemplate)         \
                                                                                         \
    /* Pipeline */                                                                       \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              CreatePipelineLayout)                    \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              DestroyPipelineLayout)                   \
                                                                                         \
    /* PipelineLayout */                                                                 \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              CreateComputePipelines)                  \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              DestroyPipeline)                         \
                                                                                         \
    /* Sampler */                                                                        \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              CreateSampler)                           \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              DestroySampler)                          \
                                                                                         \
    /* Shaders */                                                                        \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              CreateShaderModule)                      \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG,              DestroyShaderModule)

/* Macro containing every win32 specific function that we utilize in our codebase */
#define FN_LIST_WIN32(MACRO)                                                             \
    MACRO(1, 1, FF_VK_EXT_EXTERNAL_WIN32_SEM,    GetSemaphoreWin32HandleKHR)             \
    MACRO(1, 1, FF_VK_EXT_EXTERNAL_WIN32_MEMORY, GetMemoryWin32HandleKHR)

/* Macro to turn a function name into a definition */
#define PFN_DEF(req_inst, req_dev, ext_flag, name) \
    PFN_vk##name name;

/* Structure with the definition of all listed functions */
typedef struct FFVulkanFunctions {
    FN_LIST(PFN_DEF)
#ifdef _WIN32
    FN_LIST_WIN32(PFN_DEF)
#endif
} FFVulkanFunctions;

#endif /* AVUTIL_VULKAN_FUNCTIONS_H */
