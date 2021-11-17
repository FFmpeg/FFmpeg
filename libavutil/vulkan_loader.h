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

#ifndef AVUTIL_VULKAN_LOADER_H
#define AVUTIL_VULKAN_LOADER_H

#include "vulkan_functions.h"

/* Macro to turn a function name into a loader struct */
#define PFN_LOAD_INFO(req_inst, req_dev, ext_flag, name) \
    {                                                    \
        req_inst,                                        \
        req_dev,                                         \
        offsetof(FFVulkanFunctions, name),               \
        ext_flag,                                        \
        { "vk"#name, "vk"#name"EXT", "vk"#name"KHR" }    \
    },

static inline uint64_t ff_vk_extensions_to_mask(const char * const *extensions,
                                                int nb_extensions)
{
    static const struct ExtensionMap {
        const char *name;
        FFVulkanExtensions flag;
    } extension_map[] = {
        { VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,   FF_VK_EXT_EXTERNAL_DMABUF_MEMORY },
        { VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME, FF_VK_EXT_DRM_MODIFIER_FLAGS     },
        { VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,        FF_VK_EXT_EXTERNAL_FD_MEMORY     },
        { VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,     FF_VK_EXT_EXTERNAL_FD_SEM        },
        { VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME,      FF_VK_EXT_EXTERNAL_HOST_MEMORY   },
        { VK_EXT_DEBUG_UTILS_EXTENSION_NAME,               FF_VK_EXT_DEBUG_UTILS            },
#ifdef _WIN32
        { VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,     FF_VK_EXT_EXTERNAL_WIN32_MEMORY  },
        { VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME,  FF_VK_EXT_EXTERNAL_WIN32_SEM     },
#endif
    };

    FFVulkanExtensions mask = 0x0;

    for (int i = 0; i < nb_extensions; i++) {
        for (int j = 0; j < FF_ARRAY_ELEMS(extension_map); j++) {
            if (!strcmp(extensions[i], extension_map[j].name)) {
                mask |= extension_map[j].flag;
                continue;
            }
        }
    }

    return mask;
}

/**
 * Function loader.
 * Vulkan function from scratch loading happens in 3 stages - the first one
 * is before any initialization has happened, and you have neither an instance
 * structure nor a device structure. At this stage, you can only get the bare
 * minimals to initialize an instance.
 * The second stage is when you have an instance. At this stage, you can
 * initialize a VkDevice, and have an idea of what extensions each device
 * supports.
 * Finally, in the third stage, you can proceed and load all core functions,
 * plus you can be sure that any extensions you've enabled during device
 * initialization will be available.
 */
static inline int ff_vk_load_functions(AVHWDeviceContext *ctx,
                                       FFVulkanFunctions *vk,
                                       uint64_t extensions_mask,
                                       int has_inst, int has_dev)
{
    AVVulkanDeviceContext *hwctx = ctx->hwctx;

    static const struct FunctionLoadInfo {
        int req_inst;
        int req_dev;
        size_t struct_offset;
        FFVulkanExtensions ext_flag;
        const char *names[3];
    } vk_load_info[] = {
        FN_LIST(PFN_LOAD_INFO)
#ifdef _WIN32
        FN_LIST_WIN32(PFN_LOAD_INFO)
#endif
    };

    for (int i = 0; i < FF_ARRAY_ELEMS(vk_load_info); i++) {
        const struct FunctionLoadInfo *load = &vk_load_info[i];
        PFN_vkVoidFunction fn;

        if (load->req_dev  && !has_dev)
            continue;
        if (load->req_inst && !has_inst)
            continue;

        for (int j = 0; j < FF_ARRAY_ELEMS(load->names); j++) {
            const char *name = load->names[j];

            if (load->req_dev)
                fn = vk->GetDeviceProcAddr(hwctx->act_dev, name);
            else if (load->req_inst)
                fn = hwctx->get_proc_addr(hwctx->inst, name);
            else
                fn = hwctx->get_proc_addr(NULL, name);

            if (fn)
                break;
        }

        if (!fn && ((extensions_mask &~ FF_VK_EXT_NO_FLAG) & load->ext_flag)) {
            av_log(ctx, AV_LOG_ERROR, "Loader error, function \"%s\" indicated "
                   "as supported, but got NULL function pointer!\n", load->names[0]);
            return AVERROR_EXTERNAL;
        }

        *(PFN_vkVoidFunction *)((uint8_t *)vk + load->struct_offset) = fn;
    }

    return 0;
}

#endif /* AVUTIL_VULKAN_LOADER_H */
