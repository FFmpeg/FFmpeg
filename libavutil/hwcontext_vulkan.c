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

#include "config.h"
#include "pixdesc.h"
#include "avstring.h"
#include "imgutils.h"
#include "hwcontext.h"
#include "hwcontext_internal.h"
#include "hwcontext_vulkan.h"

#if CONFIG_LIBDRM
#include <unistd.h>
#include <xf86drm.h>
#include <drm_fourcc.h>
#include "hwcontext_drm.h"
#if CONFIG_VAAPI
#include <va/va_drmcommon.h>
#include "hwcontext_vaapi.h"
#endif
#endif

#if CONFIG_CUDA
#include "hwcontext_cuda_internal.h"
#include "cuda_check.h"
#define CHECK_CU(x) FF_CUDA_CHECK_DL(cuda_cu, cu, x)
#endif

typedef struct VulkanQueueCtx {
    VkFence fence;
    VkQueue queue;
    int was_synchronous;

    /* Buffer dependencies */
    AVBufferRef **buf_deps;
    int nb_buf_deps;
    int buf_deps_alloc_size;
} VulkanQueueCtx;

typedef struct VulkanExecCtx {
    VkCommandPool pool;
    VkCommandBuffer *bufs;
    VulkanQueueCtx *queues;
    int nb_queues;
    int cur_queue_idx;
} VulkanExecCtx;

typedef struct VulkanDevicePriv {
    /* Properties */
    VkPhysicalDeviceProperties2 props;
    VkPhysicalDeviceMemoryProperties mprops;
    VkPhysicalDeviceExternalMemoryHostPropertiesEXT hprops;

    /* Queues */
    uint32_t qfs[3];
    int num_qfs;

    /* Debug callback */
    VkDebugUtilsMessengerEXT debug_ctx;

    /* Extensions */
    uint64_t extensions;

    /* Settings */
    int use_linear_images;

    /* Nvidia */
    int dev_is_nvidia;
} VulkanDevicePriv;

typedef struct VulkanFramesPriv {
    /* Image conversions */
    VulkanExecCtx conv_ctx;

    /* Image transfers */
    VulkanExecCtx upload_ctx;
    VulkanExecCtx download_ctx;
} VulkanFramesPriv;

typedef struct AVVkFrameInternal {
#if CONFIG_CUDA
    /* Importing external memory into cuda is really expensive so we keep the
     * memory imported all the time */
    AVBufferRef *cuda_fc_ref; /* Need to keep it around for uninit */
    CUexternalMemory ext_mem[AV_NUM_DATA_POINTERS];
    CUmipmappedArray cu_mma[AV_NUM_DATA_POINTERS];
    CUarray cu_array[AV_NUM_DATA_POINTERS];
    CUexternalSemaphore cu_sem[AV_NUM_DATA_POINTERS];
#endif
} AVVkFrameInternal;

#define GET_QUEUE_COUNT(hwctx, graph, comp, tx) (                   \
    graph ?  hwctx->nb_graphics_queues :                            \
    comp  ? (hwctx->nb_comp_queues ?                                \
             hwctx->nb_comp_queues : hwctx->nb_graphics_queues) :   \
    tx    ? (hwctx->nb_tx_queues ? hwctx->nb_tx_queues :            \
             (hwctx->nb_comp_queues ?                               \
              hwctx->nb_comp_queues : hwctx->nb_graphics_queues)) : \
    0                                                               \
)

#define VK_LOAD_PFN(inst, name) PFN_##name pfn_##name = (PFN_##name)           \
                                              vkGetInstanceProcAddr(inst, #name)

#define DEFAULT_USAGE_FLAGS (VK_IMAGE_USAGE_SAMPLED_BIT      |                 \
                             VK_IMAGE_USAGE_STORAGE_BIT      |                 \
                             VK_IMAGE_USAGE_TRANSFER_SRC_BIT |                 \
                             VK_IMAGE_USAGE_TRANSFER_DST_BIT)

#define ADD_VAL_TO_LIST(list, count, val)                                      \
    do {                                                                       \
        list = av_realloc_array(list, sizeof(*list), ++count);                 \
        if (!list) {                                                           \
            err = AVERROR(ENOMEM);                                             \
            goto fail;                                                         \
        }                                                                      \
        list[count - 1] = av_strdup(val);                                      \
        if (!list[count - 1]) {                                                \
            err = AVERROR(ENOMEM);                                             \
            goto fail;                                                         \
        }                                                                      \
    } while(0)

static const struct {
    enum AVPixelFormat pixfmt;
    const VkFormat vkfmts[3];
} vk_pixfmt_map[] = {
    { AV_PIX_FMT_GRAY8,   { VK_FORMAT_R8_UNORM } },
    { AV_PIX_FMT_GRAY16,  { VK_FORMAT_R16_UNORM } },
    { AV_PIX_FMT_GRAYF32, { VK_FORMAT_R32_SFLOAT } },

    { AV_PIX_FMT_NV12, { VK_FORMAT_R8_UNORM, VK_FORMAT_R8G8_UNORM } },
    { AV_PIX_FMT_P010, { VK_FORMAT_R16_UNORM, VK_FORMAT_R16G16_UNORM } },
    { AV_PIX_FMT_P016, { VK_FORMAT_R16_UNORM, VK_FORMAT_R16G16_UNORM } },

    { AV_PIX_FMT_YUV420P, { VK_FORMAT_R8_UNORM, VK_FORMAT_R8_UNORM, VK_FORMAT_R8_UNORM } },
    { AV_PIX_FMT_YUV422P, { VK_FORMAT_R8_UNORM, VK_FORMAT_R8_UNORM, VK_FORMAT_R8_UNORM } },
    { AV_PIX_FMT_YUV444P, { VK_FORMAT_R8_UNORM, VK_FORMAT_R8_UNORM, VK_FORMAT_R8_UNORM } },

    { AV_PIX_FMT_YUV420P16, { VK_FORMAT_R16_UNORM, VK_FORMAT_R16_UNORM, VK_FORMAT_R16_UNORM } },
    { AV_PIX_FMT_YUV422P16, { VK_FORMAT_R16_UNORM, VK_FORMAT_R16_UNORM, VK_FORMAT_R16_UNORM } },
    { AV_PIX_FMT_YUV444P16, { VK_FORMAT_R16_UNORM, VK_FORMAT_R16_UNORM, VK_FORMAT_R16_UNORM } },

    { AV_PIX_FMT_ABGR,   { VK_FORMAT_A8B8G8R8_UNORM_PACK32 } },
    { AV_PIX_FMT_BGRA,   { VK_FORMAT_B8G8R8A8_UNORM } },
    { AV_PIX_FMT_RGBA,   { VK_FORMAT_R8G8B8A8_UNORM } },
    { AV_PIX_FMT_RGB24,  { VK_FORMAT_R8G8B8_UNORM } },
    { AV_PIX_FMT_BGR24,  { VK_FORMAT_B8G8R8_UNORM } },
    { AV_PIX_FMT_RGB48,  { VK_FORMAT_R16G16B16_UNORM } },
    { AV_PIX_FMT_RGBA64, { VK_FORMAT_R16G16B16A16_UNORM } },
    { AV_PIX_FMT_RGB565, { VK_FORMAT_R5G6B5_UNORM_PACK16 } },
    { AV_PIX_FMT_BGR565, { VK_FORMAT_B5G6R5_UNORM_PACK16 } },
    { AV_PIX_FMT_BGR0,   { VK_FORMAT_B8G8R8A8_UNORM } },
    { AV_PIX_FMT_0BGR,   { VK_FORMAT_A8B8G8R8_UNORM_PACK32 } },
    { AV_PIX_FMT_RGB0,   { VK_FORMAT_R8G8B8A8_UNORM } },

    { AV_PIX_FMT_GBRPF32, { VK_FORMAT_R32_SFLOAT, VK_FORMAT_R32_SFLOAT, VK_FORMAT_R32_SFLOAT } },
};

const VkFormat *av_vkfmt_from_pixfmt(enum AVPixelFormat p)
{
    for (enum AVPixelFormat i = 0; i < FF_ARRAY_ELEMS(vk_pixfmt_map); i++)
        if (vk_pixfmt_map[i].pixfmt == p)
            return vk_pixfmt_map[i].vkfmts;
    return NULL;
}

static int pixfmt_is_supported(AVVulkanDeviceContext *hwctx, enum AVPixelFormat p,
                               int linear)
{
    const VkFormat *fmt = av_vkfmt_from_pixfmt(p);
    int planes = av_pix_fmt_count_planes(p);

    if (!fmt)
        return 0;

    for (int i = 0; i < planes; i++) {
        VkFormatFeatureFlags flags;
        VkFormatProperties2 prop = {
            .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2,
        };
        vkGetPhysicalDeviceFormatProperties2(hwctx->phys_dev, fmt[i], &prop);
        flags = linear ? prop.formatProperties.linearTilingFeatures :
                         prop.formatProperties.optimalTilingFeatures;
        if (!(flags & DEFAULT_USAGE_FLAGS))
            return 0;
    }

    return 1;
}

enum VulkanExtensions {
    EXT_EXTERNAL_DMABUF_MEMORY = 1ULL <<  0, /* VK_EXT_external_memory_dma_buf */
    EXT_DRM_MODIFIER_FLAGS     = 1ULL <<  1, /* VK_EXT_image_drm_format_modifier */
    EXT_EXTERNAL_FD_MEMORY     = 1ULL <<  2, /* VK_KHR_external_memory_fd */
    EXT_EXTERNAL_FD_SEM        = 1ULL <<  3, /* VK_KHR_external_semaphore_fd */
    EXT_EXTERNAL_HOST_MEMORY   = 1ULL <<  4, /* VK_EXT_external_memory_host */

    EXT_NO_FLAG                = 1ULL << 63,
};

typedef struct VulkanOptExtension {
    const char *name;
    uint64_t flag;
} VulkanOptExtension;

static const VulkanOptExtension optional_instance_exts[] = {
    /* For future use */
};

static const VulkanOptExtension optional_device_exts[] = {
    { VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,               EXT_EXTERNAL_FD_MEMORY,     },
    { VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,          EXT_EXTERNAL_DMABUF_MEMORY, },
    { VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME,        EXT_DRM_MODIFIER_FLAGS,     },
    { VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,            EXT_EXTERNAL_FD_SEM,        },
    { VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME,             EXT_EXTERNAL_HOST_MEMORY,   },
};

/* Converts return values to strings */
static const char *vk_ret2str(VkResult res)
{
#define CASE(VAL) case VAL: return #VAL
    switch (res) {
    CASE(VK_SUCCESS);
    CASE(VK_NOT_READY);
    CASE(VK_TIMEOUT);
    CASE(VK_EVENT_SET);
    CASE(VK_EVENT_RESET);
    CASE(VK_INCOMPLETE);
    CASE(VK_ERROR_OUT_OF_HOST_MEMORY);
    CASE(VK_ERROR_OUT_OF_DEVICE_MEMORY);
    CASE(VK_ERROR_INITIALIZATION_FAILED);
    CASE(VK_ERROR_DEVICE_LOST);
    CASE(VK_ERROR_MEMORY_MAP_FAILED);
    CASE(VK_ERROR_LAYER_NOT_PRESENT);
    CASE(VK_ERROR_EXTENSION_NOT_PRESENT);
    CASE(VK_ERROR_FEATURE_NOT_PRESENT);
    CASE(VK_ERROR_INCOMPATIBLE_DRIVER);
    CASE(VK_ERROR_TOO_MANY_OBJECTS);
    CASE(VK_ERROR_FORMAT_NOT_SUPPORTED);
    CASE(VK_ERROR_FRAGMENTED_POOL);
    CASE(VK_ERROR_SURFACE_LOST_KHR);
    CASE(VK_ERROR_NATIVE_WINDOW_IN_USE_KHR);
    CASE(VK_SUBOPTIMAL_KHR);
    CASE(VK_ERROR_OUT_OF_DATE_KHR);
    CASE(VK_ERROR_INCOMPATIBLE_DISPLAY_KHR);
    CASE(VK_ERROR_VALIDATION_FAILED_EXT);
    CASE(VK_ERROR_INVALID_SHADER_NV);
    CASE(VK_ERROR_OUT_OF_POOL_MEMORY);
    CASE(VK_ERROR_INVALID_EXTERNAL_HANDLE);
    CASE(VK_ERROR_NOT_PERMITTED_EXT);
    CASE(VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT);
    CASE(VK_ERROR_INVALID_DEVICE_ADDRESS_EXT);
    CASE(VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT);
    default: return "Unknown error";
    }
#undef CASE
}

static VkBool32 vk_dbg_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                VkDebugUtilsMessageTypeFlagsEXT messageType,
                                const VkDebugUtilsMessengerCallbackDataEXT *data,
                                void *priv)
{
    int l;
    AVHWDeviceContext *ctx = priv;

    switch (severity) {
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT: l = AV_LOG_VERBOSE; break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:    l = AV_LOG_INFO;    break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT: l = AV_LOG_WARNING; break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:   l = AV_LOG_ERROR;   break;
    default:                                              l = AV_LOG_DEBUG;   break;
    }

    av_log(ctx, l, "%s\n", data->pMessage);
    for (int i = 0; i < data->cmdBufLabelCount; i++)
        av_log(ctx, l, "\t%i: %s\n", i, data->pCmdBufLabels[i].pLabelName);

    return 0;
}

static int check_extensions(AVHWDeviceContext *ctx, int dev, AVDictionary *opts,
                            const char * const **dst, uint32_t *num, int debug)
{
    const char *tstr;
    const char **extension_names = NULL;
    VulkanDevicePriv *p = ctx->internal->priv;
    AVVulkanDeviceContext *hwctx = ctx->hwctx;
    int err = 0, found, extensions_found = 0;

    const char *mod;
    int optional_exts_num;
    uint32_t sup_ext_count;
    char *user_exts_str = NULL;
    AVDictionaryEntry *user_exts;
    VkExtensionProperties *sup_ext;
    const VulkanOptExtension *optional_exts;

    if (!dev) {
        mod = "instance";
        optional_exts = optional_instance_exts;
        optional_exts_num = FF_ARRAY_ELEMS(optional_instance_exts);
        user_exts = av_dict_get(opts, "instance_extensions", NULL, 0);
        if (user_exts) {
            user_exts_str = av_strdup(user_exts->value);
            if (!user_exts_str) {
                err = AVERROR(ENOMEM);
                goto fail;
            }
        }
        vkEnumerateInstanceExtensionProperties(NULL, &sup_ext_count, NULL);
        sup_ext = av_malloc_array(sup_ext_count, sizeof(VkExtensionProperties));
        if (!sup_ext)
            return AVERROR(ENOMEM);
        vkEnumerateInstanceExtensionProperties(NULL, &sup_ext_count, sup_ext);
    } else {
        mod = "device";
        optional_exts = optional_device_exts;
        optional_exts_num = FF_ARRAY_ELEMS(optional_device_exts);
        user_exts = av_dict_get(opts, "device_extensions", NULL, 0);
        if (user_exts) {
            user_exts_str = av_strdup(user_exts->value);
            if (!user_exts_str) {
                err = AVERROR(ENOMEM);
                goto fail;
            }
        }
        vkEnumerateDeviceExtensionProperties(hwctx->phys_dev, NULL,
                                             &sup_ext_count, NULL);
        sup_ext = av_malloc_array(sup_ext_count, sizeof(VkExtensionProperties));
        if (!sup_ext)
            return AVERROR(ENOMEM);
        vkEnumerateDeviceExtensionProperties(hwctx->phys_dev, NULL,
                                             &sup_ext_count, sup_ext);
    }

    for (int i = 0; i < optional_exts_num; i++) {
        tstr = optional_exts[i].name;
        found = 0;
        for (int j = 0; j < sup_ext_count; j++) {
            if (!strcmp(tstr, sup_ext[j].extensionName)) {
                found = 1;
                break;
            }
        }
        if (!found)
            continue;

        av_log(ctx, AV_LOG_VERBOSE, "Using %s extension \"%s\"\n", mod, tstr);
        p->extensions |= optional_exts[i].flag;
        ADD_VAL_TO_LIST(extension_names, extensions_found, tstr);
    }

    if (debug && !dev) {
        tstr = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
        found = 0;
        for (int j = 0; j < sup_ext_count; j++) {
            if (!strcmp(tstr, sup_ext[j].extensionName)) {
                found = 1;
                break;
            }
        }
        if (found) {
            av_log(ctx, AV_LOG_VERBOSE, "Using %s extension \"%s\"\n", mod, tstr);
            ADD_VAL_TO_LIST(extension_names, extensions_found, tstr);
        } else {
            av_log(ctx, AV_LOG_ERROR, "Debug extension \"%s\" not found!\n",
                   tstr);
            err = AVERROR(EINVAL);
            goto fail;
        }
    }

    if (user_exts_str) {
        char *save, *token = av_strtok(user_exts_str, "+", &save);
        while (token) {
            found = 0;
            for (int j = 0; j < sup_ext_count; j++) {
                if (!strcmp(token, sup_ext[j].extensionName)) {
                    found = 1;
                    break;
                }
            }
            if (found) {
                av_log(ctx, AV_LOG_VERBOSE, "Using %s extension \"%s\"\n", mod, token);
                ADD_VAL_TO_LIST(extension_names, extensions_found, token);
            } else {
                av_log(ctx, AV_LOG_WARNING, "%s extension \"%s\" not found, excluding.\n",
                       mod, token);
            }
            token = av_strtok(NULL, "+", &save);
        }
    }

    *dst = extension_names;
    *num = extensions_found;

    av_free(user_exts_str);
    av_free(sup_ext);
    return 0;

fail:
    if (extension_names)
        for (int i = 0; i < extensions_found; i++)
            av_free((void *)extension_names[i]);
    av_free(extension_names);
    av_free(user_exts_str);
    av_free(sup_ext);
    return err;
}

/* Creates a VkInstance */
static int create_instance(AVHWDeviceContext *ctx, AVDictionary *opts)
{
    int err = 0;
    VkResult ret;
    VulkanDevicePriv *p = ctx->internal->priv;
    AVVulkanDeviceContext *hwctx = ctx->hwctx;
    AVDictionaryEntry *debug_opt = av_dict_get(opts, "debug", NULL, 0);
    const int debug_mode = debug_opt && strtol(debug_opt->value, NULL, 10);
    VkApplicationInfo application_info = {
        .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pEngineName        = "libavutil",
        .apiVersion         = VK_API_VERSION_1_1,
        .engineVersion      = VK_MAKE_VERSION(LIBAVUTIL_VERSION_MAJOR,
                                              LIBAVUTIL_VERSION_MINOR,
                                              LIBAVUTIL_VERSION_MICRO),
    };
    VkInstanceCreateInfo inst_props = {
        .sType            = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &application_info,
    };

    /* Check for present/missing extensions */
    err = check_extensions(ctx, 0, opts, &inst_props.ppEnabledExtensionNames,
                           &inst_props.enabledExtensionCount, debug_mode);
    if (err < 0)
        return err;

    if (debug_mode) {
        static const char *layers[] = { "VK_LAYER_KHRONOS_validation" };
        inst_props.ppEnabledLayerNames = layers;
        inst_props.enabledLayerCount = FF_ARRAY_ELEMS(layers);
    }

    /* Try to create the instance */
    ret = vkCreateInstance(&inst_props, hwctx->alloc, &hwctx->inst);

    /* Check for errors */
    if (ret != VK_SUCCESS) {
        av_log(ctx, AV_LOG_ERROR, "Instance creation failure: %s\n",
               vk_ret2str(ret));
        for (int i = 0; i < inst_props.enabledExtensionCount; i++)
            av_free((void *)inst_props.ppEnabledExtensionNames[i]);
        av_free((void *)inst_props.ppEnabledExtensionNames);
        return AVERROR_EXTERNAL;
    }

    if (debug_mode) {
        VkDebugUtilsMessengerCreateInfoEXT dbg = {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT    |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT    |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = vk_dbg_callback,
            .pUserData = ctx,
        };
        VK_LOAD_PFN(hwctx->inst, vkCreateDebugUtilsMessengerEXT);

        pfn_vkCreateDebugUtilsMessengerEXT(hwctx->inst, &dbg,
                                           hwctx->alloc, &p->debug_ctx);
    }

    hwctx->enabled_inst_extensions = inst_props.ppEnabledExtensionNames;
    hwctx->nb_enabled_inst_extensions = inst_props.enabledExtensionCount;

    return 0;
}

typedef struct VulkanDeviceSelection {
    uint8_t uuid[VK_UUID_SIZE]; /* Will use this first unless !has_uuid */
    int has_uuid;
    const char *name; /* Will use this second unless NULL */
    uint32_t pci_device; /* Will use this third unless 0x0 */
    uint32_t vendor_id; /* Last resort to find something deterministic */
    int index; /* Finally fall back to index */
} VulkanDeviceSelection;

static const char *vk_dev_type(enum VkPhysicalDeviceType type)
{
    switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated";
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   return "discrete";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    return "virtual";
    case VK_PHYSICAL_DEVICE_TYPE_CPU:            return "software";
    default:                                     return "unknown";
    }
}

/* Finds a device */
static int find_device(AVHWDeviceContext *ctx, VulkanDeviceSelection *select)
{
    int err = 0, choice = -1;
    uint32_t num;
    VkResult ret;
    VkPhysicalDevice *devices = NULL;
    VkPhysicalDeviceIDProperties *idp = NULL;
    VkPhysicalDeviceProperties2 *prop = NULL;
    AVVulkanDeviceContext *hwctx = ctx->hwctx;

    ret = vkEnumeratePhysicalDevices(hwctx->inst, &num, NULL);
    if (ret != VK_SUCCESS || !num) {
        av_log(ctx, AV_LOG_ERROR, "No devices found: %s!\n", vk_ret2str(ret));
        return AVERROR(ENODEV);
    }

    devices = av_malloc_array(num, sizeof(VkPhysicalDevice));
    if (!devices)
        return AVERROR(ENOMEM);

    ret = vkEnumeratePhysicalDevices(hwctx->inst, &num, devices);
    if (ret != VK_SUCCESS) {
        av_log(ctx, AV_LOG_ERROR, "Failed enumerating devices: %s\n",
               vk_ret2str(ret));
        err = AVERROR(ENODEV);
        goto end;
    }

    prop = av_mallocz_array(num, sizeof(*prop));
    if (!prop) {
        err = AVERROR(ENOMEM);
        goto end;
    }

    idp = av_mallocz_array(num, sizeof(*idp));
    if (!idp) {
        err = AVERROR(ENOMEM);
        goto end;
    }

    av_log(ctx, AV_LOG_VERBOSE, "GPU listing:\n");
    for (int i = 0; i < num; i++) {
        idp[i].sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
        prop[i].sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        prop[i].pNext = &idp[i];

        vkGetPhysicalDeviceProperties2(devices[i], &prop[i]);
        av_log(ctx, AV_LOG_VERBOSE, "    %d: %s (%s) (0x%x)\n", i,
               prop[i].properties.deviceName,
               vk_dev_type(prop[i].properties.deviceType),
               prop[i].properties.deviceID);
    }

    if (select->has_uuid) {
        for (int i = 0; i < num; i++) {
            if (!strncmp(idp[i].deviceUUID, select->uuid, VK_UUID_SIZE)) {
                choice = i;
                goto end;
             }
        }
        av_log(ctx, AV_LOG_ERROR, "Unable to find device by given UUID!\n");
        err = AVERROR(ENODEV);
        goto end;
    } else if (select->name) {
        av_log(ctx, AV_LOG_VERBOSE, "Requested device: %s\n", select->name);
        for (int i = 0; i < num; i++) {
            if (strstr(prop[i].properties.deviceName, select->name)) {
                choice = i;
                goto end;
             }
        }
        av_log(ctx, AV_LOG_ERROR, "Unable to find device \"%s\"!\n",
               select->name);
        err = AVERROR(ENODEV);
        goto end;
    } else if (select->pci_device) {
        av_log(ctx, AV_LOG_VERBOSE, "Requested device: 0x%x\n", select->pci_device);
        for (int i = 0; i < num; i++) {
            if (select->pci_device == prop[i].properties.deviceID) {
                choice = i;
                goto end;
            }
        }
        av_log(ctx, AV_LOG_ERROR, "Unable to find device with PCI ID 0x%x!\n",
               select->pci_device);
        err = AVERROR(EINVAL);
        goto end;
    } else if (select->vendor_id) {
        av_log(ctx, AV_LOG_VERBOSE, "Requested vendor: 0x%x\n", select->vendor_id);
        for (int i = 0; i < num; i++) {
            if (select->vendor_id == prop[i].properties.vendorID) {
                choice = i;
                goto end;
            }
        }
        av_log(ctx, AV_LOG_ERROR, "Unable to find device with Vendor ID 0x%x!\n",
               select->vendor_id);
        err = AVERROR(ENODEV);
        goto end;
    } else {
        if (select->index < num) {
            choice = select->index;
            goto end;
        }
        av_log(ctx, AV_LOG_ERROR, "Unable to find device with index %i!\n",
               select->index);
        err = AVERROR(ENODEV);
        goto end;
    }

end:
    if (choice > -1)
        hwctx->phys_dev = devices[choice];

    av_free(devices);
    av_free(prop);
    av_free(idp);

    return err;
}

static int search_queue_families(AVHWDeviceContext *ctx, VkDeviceCreateInfo *cd)
{
    uint32_t num;
    float *weights;
    VkQueueFamilyProperties *qs = NULL;
    AVVulkanDeviceContext *hwctx = ctx->hwctx;
    int graph_index = -1, comp_index = -1, tx_index = -1;
    VkDeviceQueueCreateInfo *pc = (VkDeviceQueueCreateInfo *)cd->pQueueCreateInfos;

    /* First get the number of queue families */
    vkGetPhysicalDeviceQueueFamilyProperties(hwctx->phys_dev, &num, NULL);
    if (!num) {
        av_log(ctx, AV_LOG_ERROR, "Failed to get queues!\n");
        return AVERROR_EXTERNAL;
    }

    /* Then allocate memory */
    qs = av_malloc_array(num, sizeof(VkQueueFamilyProperties));
    if (!qs)
        return AVERROR(ENOMEM);

    /* Finally retrieve the queue families */
    vkGetPhysicalDeviceQueueFamilyProperties(hwctx->phys_dev, &num, qs);

#define SEARCH_FLAGS(expr, out)                                                \
    for (int i = 0; i < num; i++) {                                            \
        const VkQueueFlagBits flags = qs[i].queueFlags;                        \
        if (expr) {                                                            \
            out = i;                                                           \
            break;                                                             \
        }                                                                      \
    }

    SEARCH_FLAGS(flags & VK_QUEUE_GRAPHICS_BIT, graph_index)

    SEARCH_FLAGS((flags &  VK_QUEUE_COMPUTE_BIT) && (i != graph_index),
                 comp_index)

    SEARCH_FLAGS((flags & VK_QUEUE_TRANSFER_BIT) && (i != graph_index) &&
                 (i != comp_index), tx_index)

#undef SEARCH_FLAGS
#define ADD_QUEUE(fidx, graph, comp, tx)                                                 \
    av_log(ctx, AV_LOG_VERBOSE, "Using queue family %i (total queues: %i) for %s%s%s\n", \
           fidx, qs[fidx].queueCount, graph ? "graphics " : "",                          \
           comp ? "compute " : "", tx ? "transfers " : "");                              \
    av_log(ctx, AV_LOG_VERBOSE, "    QF %i flags: %s%s%s%s\n", fidx,                     \
           ((qs[fidx].queueFlags) & VK_QUEUE_GRAPHICS_BIT) ? "(graphics) " : "",         \
           ((qs[fidx].queueFlags) & VK_QUEUE_COMPUTE_BIT) ? "(compute) " : "",           \
           ((qs[fidx].queueFlags) & VK_QUEUE_TRANSFER_BIT) ? "(transfers) " : "",        \
           ((qs[fidx].queueFlags) & VK_QUEUE_SPARSE_BINDING_BIT) ? "(sparse) " : "");    \
    pc[cd->queueCreateInfoCount].queueFamilyIndex = fidx;                                \
    pc[cd->queueCreateInfoCount].queueCount = qs[fidx].queueCount;                       \
    weights = av_malloc(qs[fidx].queueCount * sizeof(float));                            \
    pc[cd->queueCreateInfoCount].pQueuePriorities = weights;                             \
    if (!weights)                                                                        \
        goto fail;                                                                       \
    for (int i = 0; i < qs[fidx].queueCount; i++)                                        \
        weights[i] = 1.0f;                                                               \
    cd->queueCreateInfoCount++;

    ADD_QUEUE(graph_index, 1, comp_index < 0, tx_index < 0 && comp_index < 0)
    hwctx->queue_family_index      = graph_index;
    hwctx->queue_family_comp_index = graph_index;
    hwctx->queue_family_tx_index   = graph_index;
    hwctx->nb_graphics_queues      = qs[graph_index].queueCount;

    if (comp_index != -1) {
        ADD_QUEUE(comp_index, 0, 1, tx_index < 0)
        hwctx->queue_family_tx_index   = comp_index;
        hwctx->queue_family_comp_index = comp_index;
        hwctx->nb_comp_queues          = qs[comp_index].queueCount;
    }

    if (tx_index != -1) {
        ADD_QUEUE(tx_index, 0, 0, 1)
        hwctx->queue_family_tx_index = tx_index;
        hwctx->nb_tx_queues          = qs[tx_index].queueCount;
    }

#undef ADD_QUEUE
    av_free(qs);

    return 0;

fail:
    av_freep(&pc[0].pQueuePriorities);
    av_freep(&pc[1].pQueuePriorities);
    av_freep(&pc[2].pQueuePriorities);
    av_free(qs);

    return AVERROR(ENOMEM);
}

static int create_exec_ctx(AVHWFramesContext *hwfc, VulkanExecCtx *cmd,
                           int queue_family_index, int num_queues)
{
    VkResult ret;
    AVVulkanDeviceContext *hwctx = hwfc->device_ctx->hwctx;

    VkCommandPoolCreateInfo cqueue_create = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags              = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex   = queue_family_index,
    };
    VkCommandBufferAllocateInfo cbuf_create = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = num_queues,
    };

    cmd->nb_queues = num_queues;

    cmd->queues = av_mallocz(num_queues * sizeof(*cmd->queues));
    if (!cmd->queues)
        return AVERROR(ENOMEM);

    cmd->bufs = av_mallocz(num_queues * sizeof(*cmd->bufs));
    if (!cmd->bufs)
        return AVERROR(ENOMEM);

    /* Create command pool */
    ret = vkCreateCommandPool(hwctx->act_dev, &cqueue_create,
                              hwctx->alloc, &cmd->pool);
    if (ret != VK_SUCCESS) {
        av_log(hwfc, AV_LOG_ERROR, "Command pool creation failure: %s\n",
               vk_ret2str(ret));
        return AVERROR_EXTERNAL;
    }

    cbuf_create.commandPool = cmd->pool;

    /* Allocate command buffer */
    ret = vkAllocateCommandBuffers(hwctx->act_dev, &cbuf_create, cmd->bufs);
    if (ret != VK_SUCCESS) {
        av_log(hwfc, AV_LOG_ERROR, "Command buffer alloc failure: %s\n",
               vk_ret2str(ret));
        return AVERROR_EXTERNAL;
    }

    for (int i = 0; i < num_queues; i++) {
        VulkanQueueCtx *q = &cmd->queues[i];
        vkGetDeviceQueue(hwctx->act_dev, queue_family_index, i, &q->queue);
        q->was_synchronous = 1;
    }

    return 0;
}

static void free_exec_ctx(AVHWFramesContext *hwfc, VulkanExecCtx *cmd)
{
    AVVulkanDeviceContext *hwctx = hwfc->device_ctx->hwctx;

    /* Make sure all queues have finished executing */
    for (int i = 0; i < cmd->nb_queues; i++) {
        VulkanQueueCtx *q = &cmd->queues[i];

        if (q->fence && !q->was_synchronous) {
            vkWaitForFences(hwctx->act_dev, 1, &q->fence, VK_TRUE, UINT64_MAX);
            vkResetFences(hwctx->act_dev, 1, &q->fence);
        }

        /* Free the fence */
        if (q->fence)
            vkDestroyFence(hwctx->act_dev, q->fence, hwctx->alloc);

        /* Free buffer dependencies */
        for (int j = 0; j < q->nb_buf_deps; j++)
            av_buffer_unref(&q->buf_deps[j]);
        av_free(q->buf_deps);
    }

    if (cmd->bufs)
        vkFreeCommandBuffers(hwctx->act_dev, cmd->pool, cmd->nb_queues, cmd->bufs);
    if (cmd->pool)
        vkDestroyCommandPool(hwctx->act_dev, cmd->pool, hwctx->alloc);

    av_freep(&cmd->bufs);
    av_freep(&cmd->queues);
}

static VkCommandBuffer get_buf_exec_ctx(AVHWFramesContext *hwfc, VulkanExecCtx *cmd)
{
    return cmd->bufs[cmd->cur_queue_idx];
}

static void unref_exec_ctx_deps(AVHWFramesContext *hwfc, VulkanExecCtx *cmd)
{
    VulkanQueueCtx *q = &cmd->queues[cmd->cur_queue_idx];

    for (int j = 0; j < q->nb_buf_deps; j++)
        av_buffer_unref(&q->buf_deps[j]);
    q->nb_buf_deps = 0;
}

static int wait_start_exec_ctx(AVHWFramesContext *hwfc, VulkanExecCtx *cmd)
{
    VkResult ret;
    AVVulkanDeviceContext *hwctx = hwfc->device_ctx->hwctx;
    VulkanQueueCtx *q = &cmd->queues[cmd->cur_queue_idx];

    VkCommandBufferBeginInfo cmd_start = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    /* Create the fence and don't wait for it initially */
    if (!q->fence) {
        VkFenceCreateInfo fence_spawn = {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        };
        ret = vkCreateFence(hwctx->act_dev, &fence_spawn, hwctx->alloc,
                            &q->fence);
        if (ret != VK_SUCCESS) {
            av_log(hwfc, AV_LOG_ERROR, "Failed to queue frame fence: %s\n",
                   vk_ret2str(ret));
            return AVERROR_EXTERNAL;
        }
    } else if (!q->was_synchronous) {
        vkWaitForFences(hwctx->act_dev, 1, &q->fence, VK_TRUE, UINT64_MAX);
        vkResetFences(hwctx->act_dev, 1, &q->fence);
    }

    /* Discard queue dependencies */
    unref_exec_ctx_deps(hwfc, cmd);

    ret = vkBeginCommandBuffer(cmd->bufs[cmd->cur_queue_idx], &cmd_start);
    if (ret != VK_SUCCESS) {
        av_log(hwfc, AV_LOG_ERROR, "Unable to init command buffer: %s\n",
               vk_ret2str(ret));
        return AVERROR_EXTERNAL;
    }

    return 0;
}

static int add_buf_dep_exec_ctx(AVHWFramesContext *hwfc, VulkanExecCtx *cmd,
                                AVBufferRef * const *deps, int nb_deps)
{
    AVBufferRef **dst;
    VulkanQueueCtx *q = &cmd->queues[cmd->cur_queue_idx];

    if (!deps || !nb_deps)
        return 0;

    dst = av_fast_realloc(q->buf_deps, &q->buf_deps_alloc_size,
                          (q->nb_buf_deps + nb_deps) * sizeof(*dst));
    if (!dst)
        goto err;

    q->buf_deps = dst;

    for (int i = 0; i < nb_deps; i++) {
        q->buf_deps[q->nb_buf_deps] = av_buffer_ref(deps[i]);
        if (!q->buf_deps[q->nb_buf_deps])
            goto err;
        q->nb_buf_deps++;
    }

    return 0;

err:
    unref_exec_ctx_deps(hwfc, cmd);
    return AVERROR(ENOMEM);
}

static int submit_exec_ctx(AVHWFramesContext *hwfc, VulkanExecCtx *cmd,
                           VkSubmitInfo *s_info, int synchronous)
{
    VkResult ret;
    VulkanQueueCtx *q = &cmd->queues[cmd->cur_queue_idx];

    ret = vkEndCommandBuffer(cmd->bufs[cmd->cur_queue_idx]);
    if (ret != VK_SUCCESS) {
        av_log(hwfc, AV_LOG_ERROR, "Unable to finish command buffer: %s\n",
               vk_ret2str(ret));
        unref_exec_ctx_deps(hwfc, cmd);
        return AVERROR_EXTERNAL;
    }

    s_info->pCommandBuffers = &cmd->bufs[cmd->cur_queue_idx];
    s_info->commandBufferCount = 1;

    ret = vkQueueSubmit(q->queue, 1, s_info, q->fence);
    if (ret != VK_SUCCESS) {
        unref_exec_ctx_deps(hwfc, cmd);
        return AVERROR_EXTERNAL;
    }

    q->was_synchronous = synchronous;

    if (synchronous) {
        AVVulkanDeviceContext *hwctx = hwfc->device_ctx->hwctx;
        vkWaitForFences(hwctx->act_dev, 1, &q->fence, VK_TRUE, UINT64_MAX);
        vkResetFences(hwctx->act_dev, 1, &q->fence);
        unref_exec_ctx_deps(hwfc, cmd);
    } else { /* Rotate queues */
        cmd->cur_queue_idx = (cmd->cur_queue_idx + 1) % cmd->nb_queues;
    }

    return 0;
}

static void vulkan_device_free(AVHWDeviceContext *ctx)
{
    VulkanDevicePriv *p = ctx->internal->priv;
    AVVulkanDeviceContext *hwctx = ctx->hwctx;

    vkDestroyDevice(hwctx->act_dev, hwctx->alloc);

    if (p->debug_ctx) {
        VK_LOAD_PFN(hwctx->inst, vkDestroyDebugUtilsMessengerEXT);
        pfn_vkDestroyDebugUtilsMessengerEXT(hwctx->inst, p->debug_ctx,
                                            hwctx->alloc);
    }

    vkDestroyInstance(hwctx->inst, hwctx->alloc);

    for (int i = 0; i < hwctx->nb_enabled_inst_extensions; i++)
        av_free((void *)hwctx->enabled_inst_extensions[i]);
    av_free((void *)hwctx->enabled_inst_extensions);

    for (int i = 0; i < hwctx->nb_enabled_dev_extensions; i++)
        av_free((void *)hwctx->enabled_dev_extensions[i]);
    av_free((void *)hwctx->enabled_dev_extensions);
}

static int vulkan_device_create_internal(AVHWDeviceContext *ctx,
                                         VulkanDeviceSelection *dev_select,
                                         AVDictionary *opts, int flags)
{
    int err = 0;
    VkResult ret;
    AVDictionaryEntry *opt_d;
    VulkanDevicePriv *p = ctx->internal->priv;
    AVVulkanDeviceContext *hwctx = ctx->hwctx;
    VkPhysicalDeviceFeatures dev_features = { 0 };
    VkDeviceQueueCreateInfo queue_create_info[3] = {
        { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, },
        { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, },
        { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, },
    };

    VkDeviceCreateInfo dev_info = {
        .sType                = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext                = &hwctx->device_features,
        .pQueueCreateInfos    = queue_create_info,
        .queueCreateInfoCount = 0,
    };

    hwctx->device_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    ctx->free = vulkan_device_free;

    /* Create an instance if not given one */
    if ((err = create_instance(ctx, opts)))
        goto end;

    /* Find a device (if not given one) */
    if ((err = find_device(ctx, dev_select)))
        goto end;

    vkGetPhysicalDeviceFeatures(hwctx->phys_dev, &dev_features);
#define COPY_FEATURE(DST, NAME) (DST).features.NAME = dev_features.NAME;
    COPY_FEATURE(hwctx->device_features, shaderImageGatherExtended)
    COPY_FEATURE(hwctx->device_features, fragmentStoresAndAtomics)
    COPY_FEATURE(hwctx->device_features, vertexPipelineStoresAndAtomics)
    COPY_FEATURE(hwctx->device_features, shaderInt64)
#undef COPY_FEATURE

    /* Search queue family */
    if ((err = search_queue_families(ctx, &dev_info)))
        goto end;

    if ((err = check_extensions(ctx, 1, opts, &dev_info.ppEnabledExtensionNames,
                                &dev_info.enabledExtensionCount, 0))) {
        av_free((void *)queue_create_info[0].pQueuePriorities);
        av_free((void *)queue_create_info[1].pQueuePriorities);
        av_free((void *)queue_create_info[2].pQueuePriorities);
        goto end;
    }

    ret = vkCreateDevice(hwctx->phys_dev, &dev_info, hwctx->alloc,
                         &hwctx->act_dev);

    av_free((void *)queue_create_info[0].pQueuePriorities);
    av_free((void *)queue_create_info[1].pQueuePriorities);
    av_free((void *)queue_create_info[2].pQueuePriorities);

    if (ret != VK_SUCCESS) {
        av_log(ctx, AV_LOG_ERROR, "Device creation failure: %s\n",
               vk_ret2str(ret));
        for (int i = 0; i < dev_info.enabledExtensionCount; i++)
            av_free((void *)dev_info.ppEnabledExtensionNames[i]);
        av_free((void *)dev_info.ppEnabledExtensionNames);
        err = AVERROR_EXTERNAL;
        goto end;
    }

    /* Tiled images setting, use them by default */
    opt_d = av_dict_get(opts, "linear_images", NULL, 0);
    if (opt_d)
        p->use_linear_images = strtol(opt_d->value, NULL, 10);

    hwctx->enabled_dev_extensions = dev_info.ppEnabledExtensionNames;
    hwctx->nb_enabled_dev_extensions = dev_info.enabledExtensionCount;

end:
    return err;
}

static int vulkan_device_init(AVHWDeviceContext *ctx)
{
    uint32_t queue_num;
    AVVulkanDeviceContext *hwctx = ctx->hwctx;
    VulkanDevicePriv *p = ctx->internal->priv;

    /* Set device extension flags */
    for (int i = 0; i < hwctx->nb_enabled_dev_extensions; i++) {
        for (int j = 0; j < FF_ARRAY_ELEMS(optional_device_exts); j++) {
            if (!strcmp(hwctx->enabled_dev_extensions[i],
                        optional_device_exts[j].name)) {
                av_log(ctx, AV_LOG_VERBOSE, "Using device extension %s\n",
                       hwctx->enabled_dev_extensions[i]);
                p->extensions |= optional_device_exts[j].flag;
                break;
            }
        }
    }

    p->props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    p->props.pNext = &p->hprops;
    p->hprops.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT;

    vkGetPhysicalDeviceProperties2(hwctx->phys_dev, &p->props);
    av_log(ctx, AV_LOG_VERBOSE, "Using device: %s\n",
           p->props.properties.deviceName);
    av_log(ctx, AV_LOG_VERBOSE, "Alignments:\n");
    av_log(ctx, AV_LOG_VERBOSE, "    optimalBufferCopyRowPitchAlignment: %li\n",
           p->props.properties.limits.optimalBufferCopyRowPitchAlignment);
    av_log(ctx, AV_LOG_VERBOSE, "    minMemoryMapAlignment:              %li\n",
           p->props.properties.limits.minMemoryMapAlignment);
    if (p->extensions & EXT_EXTERNAL_HOST_MEMORY)
        av_log(ctx, AV_LOG_VERBOSE, "    minImportedHostPointerAlignment:    %li\n",
               p->hprops.minImportedHostPointerAlignment);

    p->dev_is_nvidia = (p->props.properties.vendorID == 0x10de);

    vkGetPhysicalDeviceQueueFamilyProperties(hwctx->phys_dev, &queue_num, NULL);
    if (!queue_num) {
        av_log(ctx, AV_LOG_ERROR, "Failed to get queues!\n");
        return AVERROR_EXTERNAL;
    }

#define CHECK_QUEUE(type, n)                                                         \
if (n >= queue_num) {                                                                \
    av_log(ctx, AV_LOG_ERROR, "Invalid %s queue index %i (device has %i queues)!\n", \
           type, n, queue_num);                                                      \
    return AVERROR(EINVAL);                                                          \
}

    CHECK_QUEUE("graphics", hwctx->queue_family_index)
    CHECK_QUEUE("upload",   hwctx->queue_family_tx_index)
    CHECK_QUEUE("compute",  hwctx->queue_family_comp_index)

#undef CHECK_QUEUE

    p->qfs[p->num_qfs++] = hwctx->queue_family_index;
    if ((hwctx->queue_family_tx_index != hwctx->queue_family_index) &&
        (hwctx->queue_family_tx_index != hwctx->queue_family_comp_index))
        p->qfs[p->num_qfs++] = hwctx->queue_family_tx_index;
    if ((hwctx->queue_family_comp_index != hwctx->queue_family_index) &&
        (hwctx->queue_family_comp_index != hwctx->queue_family_tx_index))
        p->qfs[p->num_qfs++] = hwctx->queue_family_comp_index;

    /* Get device capabilities */
    vkGetPhysicalDeviceMemoryProperties(hwctx->phys_dev, &p->mprops);

    return 0;
}

static int vulkan_device_create(AVHWDeviceContext *ctx, const char *device,
                                AVDictionary *opts, int flags)
{
    VulkanDeviceSelection dev_select = { 0 };
    if (device && device[0]) {
        char *end = NULL;
        dev_select.index = strtol(device, &end, 10);
        if (end == device) {
            dev_select.index = 0;
            dev_select.name  = device;
        }
    }

    return vulkan_device_create_internal(ctx, &dev_select, opts, flags);
}

static int vulkan_device_derive(AVHWDeviceContext *ctx,
                                AVHWDeviceContext *src_ctx,
                                AVDictionary *opts, int flags)
{
    av_unused VulkanDeviceSelection dev_select = { 0 };

    /* If there's only one device on the system, then even if its not covered
     * by the following checks (e.g. non-PCIe ARM GPU), having an empty
     * dev_select will mean it'll get picked. */
    switch(src_ctx->type) {
#if CONFIG_LIBDRM
#if CONFIG_VAAPI
    case AV_HWDEVICE_TYPE_VAAPI: {
        AVVAAPIDeviceContext *src_hwctx = src_ctx->hwctx;

        const char *vendor = vaQueryVendorString(src_hwctx->display);
        if (!vendor) {
            av_log(ctx, AV_LOG_ERROR, "Unable to get device info from VAAPI!\n");
            return AVERROR_EXTERNAL;
        }

        if (strstr(vendor, "Intel"))
            dev_select.vendor_id = 0x8086;
        if (strstr(vendor, "AMD"))
            dev_select.vendor_id = 0x1002;

        return vulkan_device_create_internal(ctx, &dev_select, opts, flags);
    }
#endif
    case AV_HWDEVICE_TYPE_DRM: {
        AVDRMDeviceContext *src_hwctx = src_ctx->hwctx;

        drmDevice *drm_dev_info;
        int err = drmGetDevice(src_hwctx->fd, &drm_dev_info);
        if (err) {
            av_log(ctx, AV_LOG_ERROR, "Unable to get device info from DRM fd!\n");
            return AVERROR_EXTERNAL;
        }

        if (drm_dev_info->bustype == DRM_BUS_PCI)
            dev_select.pci_device = drm_dev_info->deviceinfo.pci->device_id;

        drmFreeDevice(&drm_dev_info);

        return vulkan_device_create_internal(ctx, &dev_select, opts, flags);
    }
#endif
#if CONFIG_CUDA
    case AV_HWDEVICE_TYPE_CUDA: {
        AVHWDeviceContext *cuda_cu = src_ctx;
        AVCUDADeviceContext *src_hwctx = src_ctx->hwctx;
        AVCUDADeviceContextInternal *cu_internal = src_hwctx->internal;
        CudaFunctions *cu = cu_internal->cuda_dl;

        int ret = CHECK_CU(cu->cuDeviceGetUuid((CUuuid *)&dev_select.uuid,
                                               cu_internal->cuda_device));
        if (ret < 0) {
            av_log(ctx, AV_LOG_ERROR, "Unable to get UUID from CUDA!\n");
            return AVERROR_EXTERNAL;
        }

        dev_select.has_uuid = 1;

        return vulkan_device_create_internal(ctx, &dev_select, opts, flags);
    }
#endif
    default:
        return AVERROR(ENOSYS);
    }
}

static int vulkan_frames_get_constraints(AVHWDeviceContext *ctx,
                                         const void *hwconfig,
                                         AVHWFramesConstraints *constraints)
{
    int count = 0;
    AVVulkanDeviceContext *hwctx = ctx->hwctx;
    VulkanDevicePriv *p = ctx->internal->priv;

    for (enum AVPixelFormat i = 0; i < AV_PIX_FMT_NB; i++)
        count += pixfmt_is_supported(hwctx, i, p->use_linear_images);

#if CONFIG_CUDA
    if (p->dev_is_nvidia)
        count++;
#endif

    constraints->valid_sw_formats = av_malloc_array(count + 1,
                                                    sizeof(enum AVPixelFormat));
    if (!constraints->valid_sw_formats)
        return AVERROR(ENOMEM);

    count = 0;
    for (enum AVPixelFormat i = 0; i < AV_PIX_FMT_NB; i++)
        if (pixfmt_is_supported(hwctx, i, p->use_linear_images))
            constraints->valid_sw_formats[count++] = i;

#if CONFIG_CUDA
    if (p->dev_is_nvidia)
        constraints->valid_sw_formats[count++] = AV_PIX_FMT_CUDA;
#endif
    constraints->valid_sw_formats[count++] = AV_PIX_FMT_NONE;

    constraints->min_width  = 0;
    constraints->min_height = 0;
    constraints->max_width  = p->props.properties.limits.maxImageDimension2D;
    constraints->max_height = p->props.properties.limits.maxImageDimension2D;

    constraints->valid_hw_formats = av_malloc_array(2, sizeof(enum AVPixelFormat));
    if (!constraints->valid_hw_formats)
        return AVERROR(ENOMEM);

    constraints->valid_hw_formats[0] = AV_PIX_FMT_VULKAN;
    constraints->valid_hw_formats[1] = AV_PIX_FMT_NONE;

    return 0;
}

static int alloc_mem(AVHWDeviceContext *ctx, VkMemoryRequirements *req,
                     VkMemoryPropertyFlagBits req_flags, const void *alloc_extension,
                     VkMemoryPropertyFlagBits *mem_flags, VkDeviceMemory *mem)
{
    VkResult ret;
    int index = -1;
    VulkanDevicePriv *p = ctx->internal->priv;
    AVVulkanDeviceContext *dev_hwctx = ctx->hwctx;
    VkMemoryAllocateInfo alloc_info = {
        .sType          = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext          = alloc_extension,
        .allocationSize = req->size,
    };

    /* The vulkan spec requires memory types to be sorted in the "optimal"
     * order, so the first matching type we find will be the best/fastest one */
    for (int i = 0; i < p->mprops.memoryTypeCount; i++) {
        /* The memory type must be supported by the requirements (bitfield) */
        if (!(req->memoryTypeBits & (1 << i)))
            continue;

        /* The memory type flags must include our properties */
        if ((p->mprops.memoryTypes[i].propertyFlags & req_flags) != req_flags)
            continue;

        /* Found a suitable memory type */
        index = i;
        break;
    }

    if (index < 0) {
        av_log(ctx, AV_LOG_ERROR, "No memory type found for flags 0x%x\n",
               req_flags);
        return AVERROR(EINVAL);
    }

    alloc_info.memoryTypeIndex = index;

    ret = vkAllocateMemory(dev_hwctx->act_dev, &alloc_info,
                           dev_hwctx->alloc, mem);
    if (ret != VK_SUCCESS) {
        av_log(ctx, AV_LOG_ERROR, "Failed to allocate memory: %s\n",
               vk_ret2str(ret));
        return AVERROR(ENOMEM);
    }

    *mem_flags |= p->mprops.memoryTypes[index].propertyFlags;

    return 0;
}

static void vulkan_free_internal(AVVkFrameInternal *internal)
{
    if (!internal)
        return;

#if CONFIG_CUDA
    if (internal->cuda_fc_ref) {
        AVHWFramesContext *cuda_fc = (AVHWFramesContext *)internal->cuda_fc_ref->data;
        int planes = av_pix_fmt_count_planes(cuda_fc->sw_format);
        AVHWDeviceContext *cuda_cu = cuda_fc->device_ctx;
        AVCUDADeviceContext *cuda_dev = cuda_cu->hwctx;
        AVCUDADeviceContextInternal *cu_internal = cuda_dev->internal;
        CudaFunctions *cu = cu_internal->cuda_dl;

        for (int i = 0; i < planes; i++) {
            if (internal->cu_sem[i])
                CHECK_CU(cu->cuDestroyExternalSemaphore(internal->cu_sem[i]));
            if (internal->cu_mma[i])
                CHECK_CU(cu->cuMipmappedArrayDestroy(internal->cu_mma[i]));
            if (internal->ext_mem[i])
                CHECK_CU(cu->cuDestroyExternalMemory(internal->ext_mem[i]));
        }

        av_buffer_unref(&internal->cuda_fc_ref);
    }
#endif

    av_free(internal);
}

static void vulkan_frame_free(void *opaque, uint8_t *data)
{
    AVVkFrame *f = (AVVkFrame *)data;
    AVHWFramesContext *hwfc = opaque;
    AVVulkanDeviceContext *hwctx = hwfc->device_ctx->hwctx;
    int planes = av_pix_fmt_count_planes(hwfc->sw_format);

    vulkan_free_internal(f->internal);

    for (int i = 0; i < planes; i++) {
        vkDestroyImage(hwctx->act_dev, f->img[i], hwctx->alloc);
        vkFreeMemory(hwctx->act_dev, f->mem[i], hwctx->alloc);
        vkDestroySemaphore(hwctx->act_dev, f->sem[i], hwctx->alloc);
    }

    av_free(f);
}

static int alloc_bind_mem(AVHWFramesContext *hwfc, AVVkFrame *f,
                          void *alloc_pnext, size_t alloc_pnext_stride)
{
    int err;
    VkResult ret;
    AVHWDeviceContext *ctx = hwfc->device_ctx;
    VulkanDevicePriv *p = ctx->internal->priv;
    const int planes = av_pix_fmt_count_planes(hwfc->sw_format);
    VkBindImageMemoryInfo bind_info[AV_NUM_DATA_POINTERS] = { { 0 } };

    AVVulkanDeviceContext *hwctx = ctx->hwctx;

    for (int i = 0; i < planes; i++) {
        int use_ded_mem;
        VkImageMemoryRequirementsInfo2 req_desc = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
            .image = f->img[i],
        };
        VkMemoryDedicatedAllocateInfo ded_alloc = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
            .pNext = (void *)(((uint8_t *)alloc_pnext) + i*alloc_pnext_stride),
        };
        VkMemoryDedicatedRequirements ded_req = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS,
        };
        VkMemoryRequirements2 req = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
            .pNext = &ded_req,
        };

        vkGetImageMemoryRequirements2(hwctx->act_dev, &req_desc, &req);

        if (f->tiling == VK_IMAGE_TILING_LINEAR)
            req.memoryRequirements.size = FFALIGN(req.memoryRequirements.size,
                                                  p->props.properties.limits.minMemoryMapAlignment);

        /* In case the implementation prefers/requires dedicated allocation */
        use_ded_mem = ded_req.prefersDedicatedAllocation |
                      ded_req.requiresDedicatedAllocation;
        if (use_ded_mem)
            ded_alloc.image = f->img[i];

        /* Allocate memory */
        if ((err = alloc_mem(ctx, &req.memoryRequirements,
                             f->tiling == VK_IMAGE_TILING_LINEAR ?
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT :
                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                             use_ded_mem ? &ded_alloc : (void *)ded_alloc.pNext,
                             &f->flags, &f->mem[i])))
            return err;

        f->size[i] = req.memoryRequirements.size;
        bind_info[i].sType  = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO;
        bind_info[i].image  = f->img[i];
        bind_info[i].memory = f->mem[i];
    }

    /* Bind the allocated memory to the images */
    ret = vkBindImageMemory2(hwctx->act_dev, planes, bind_info);
    if (ret != VK_SUCCESS) {
        av_log(ctx, AV_LOG_ERROR, "Failed to bind memory: %s\n",
               vk_ret2str(ret));
        return AVERROR_EXTERNAL;
    }

    return 0;
}

enum PrepMode {
    PREP_MODE_WRITE,
    PREP_MODE_RO_SHADER,
    PREP_MODE_EXTERNAL_EXPORT,
};

static int prepare_frame(AVHWFramesContext *hwfc, VulkanExecCtx *ectx,
                         AVVkFrame *frame, enum PrepMode pmode)
{
    int err;
    uint32_t dst_qf;
    VkImageLayout new_layout;
    VkAccessFlags new_access;
    const int planes = av_pix_fmt_count_planes(hwfc->sw_format);

    VkImageMemoryBarrier img_bar[AV_NUM_DATA_POINTERS] = { 0 };

    VkSubmitInfo s_info = {
        .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pSignalSemaphores    = frame->sem,
        .signalSemaphoreCount = planes,
    };

    VkPipelineStageFlagBits wait_st[AV_NUM_DATA_POINTERS];
    for (int i = 0; i < planes; i++)
        wait_st[i] = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

    switch (pmode) {
    case PREP_MODE_WRITE:
        new_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        new_access = VK_ACCESS_TRANSFER_WRITE_BIT;
        dst_qf     = VK_QUEUE_FAMILY_IGNORED;
        break;
    case PREP_MODE_RO_SHADER:
        new_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        new_access = VK_ACCESS_TRANSFER_READ_BIT;
        dst_qf     = VK_QUEUE_FAMILY_IGNORED;
        break;
    case PREP_MODE_EXTERNAL_EXPORT:
        new_layout = VK_IMAGE_LAYOUT_GENERAL;
        new_access = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        dst_qf     = VK_QUEUE_FAMILY_EXTERNAL_KHR;
        s_info.pWaitSemaphores = frame->sem;
        s_info.pWaitDstStageMask = wait_st;
        s_info.waitSemaphoreCount = planes;
        break;
    }

    if ((err = wait_start_exec_ctx(hwfc, ectx)))
        return err;

    /* Change the image layout to something more optimal for writes.
     * This also signals the newly created semaphore, making it usable
     * for synchronization */
    for (int i = 0; i < planes; i++) {
        img_bar[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        img_bar[i].srcAccessMask = 0x0;
        img_bar[i].dstAccessMask = new_access;
        img_bar[i].oldLayout = frame->layout[i];
        img_bar[i].newLayout = new_layout;
        img_bar[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        img_bar[i].dstQueueFamilyIndex = dst_qf;
        img_bar[i].image = frame->img[i];
        img_bar[i].subresourceRange.levelCount = 1;
        img_bar[i].subresourceRange.layerCount = 1;
        img_bar[i].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

        frame->layout[i] = img_bar[i].newLayout;
        frame->access[i] = img_bar[i].dstAccessMask;
    }

    vkCmdPipelineBarrier(get_buf_exec_ctx(hwfc, ectx),
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, NULL, 0, NULL, planes, img_bar);

    return submit_exec_ctx(hwfc, ectx, &s_info, 0);
}

static int create_frame(AVHWFramesContext *hwfc, AVVkFrame **frame,
                        VkImageTiling tiling, VkImageUsageFlagBits usage,
                        void *create_pnext)
{
    int err;
    VkResult ret;
    AVHWDeviceContext *ctx = hwfc->device_ctx;
    VulkanDevicePriv *p = ctx->internal->priv;
    AVVulkanDeviceContext *hwctx = ctx->hwctx;
    enum AVPixelFormat format = hwfc->sw_format;
    const VkFormat *img_fmts = av_vkfmt_from_pixfmt(format);
    const int planes = av_pix_fmt_count_planes(format);

    VkExportSemaphoreCreateInfo ext_sem_info = {
        .sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO,
        .handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT,
    };

    VkSemaphoreCreateInfo sem_spawn = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = p->extensions & EXT_EXTERNAL_FD_SEM ? &ext_sem_info : NULL,
    };

    AVVkFrame *f = av_vk_frame_alloc();
    if (!f) {
        av_log(ctx, AV_LOG_ERROR, "Unable to allocate memory for AVVkFrame!\n");
        return AVERROR(ENOMEM);
    }

    /* Create the images */
    for (int i = 0; i < planes; i++) {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(format);
        int w = hwfc->width;
        int h = hwfc->height;
        const int p_w = i > 0 ? AV_CEIL_RSHIFT(w, desc->log2_chroma_w) : w;
        const int p_h = i > 0 ? AV_CEIL_RSHIFT(h, desc->log2_chroma_h) : h;

        VkImageCreateInfo image_create_info = {
            .sType                 = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext                 = create_pnext,
            .imageType             = VK_IMAGE_TYPE_2D,
            .format                = img_fmts[i],
            .extent.width          = p_w,
            .extent.height         = p_h,
            .extent.depth          = 1,
            .mipLevels             = 1,
            .arrayLayers           = 1,
            .flags                 = VK_IMAGE_CREATE_ALIAS_BIT,
            .tiling                = tiling,
            .initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED,
            .usage                 = usage,
            .samples               = VK_SAMPLE_COUNT_1_BIT,
            .pQueueFamilyIndices   = p->qfs,
            .queueFamilyIndexCount = p->num_qfs,
            .sharingMode           = p->num_qfs > 1 ? VK_SHARING_MODE_CONCURRENT :
                                                      VK_SHARING_MODE_EXCLUSIVE,
        };

        ret = vkCreateImage(hwctx->act_dev, &image_create_info,
                            hwctx->alloc, &f->img[i]);
        if (ret != VK_SUCCESS) {
            av_log(ctx, AV_LOG_ERROR, "Image creation failure: %s\n",
                   vk_ret2str(ret));
            err = AVERROR(EINVAL);
            goto fail;
        }

        /* Create semaphore */
        ret = vkCreateSemaphore(hwctx->act_dev, &sem_spawn,
                                hwctx->alloc, &f->sem[i]);
        if (ret != VK_SUCCESS) {
            av_log(hwctx, AV_LOG_ERROR, "Failed to create semaphore: %s\n",
                   vk_ret2str(ret));
            return AVERROR_EXTERNAL;
        }

        f->layout[i] = image_create_info.initialLayout;
        f->access[i] = 0x0;
    }

    f->flags     = 0x0;
    f->tiling    = tiling;

    *frame = f;
    return 0;

fail:
    vulkan_frame_free(hwfc, (uint8_t *)f);
    return err;
}

/* Checks if an export flag is enabled, and if it is ORs it with *iexp */
static void try_export_flags(AVHWFramesContext *hwfc,
                             VkExternalMemoryHandleTypeFlags *comp_handle_types,
                             VkExternalMemoryHandleTypeFlagBits *iexp,
                             VkExternalMemoryHandleTypeFlagBits exp)
{
    VkResult ret;
    AVVulkanFramesContext *hwctx = hwfc->hwctx;
    AVVulkanDeviceContext *dev_hwctx = hwfc->device_ctx->hwctx;
    VkExternalImageFormatProperties eprops = {
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES_KHR,
    };
    VkImageFormatProperties2 props = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,
        .pNext = &eprops,
    };
    VkPhysicalDeviceExternalImageFormatInfo enext = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO,
        .handleType = exp,
    };
    VkPhysicalDeviceImageFormatInfo2 pinfo = {
        .sType  = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
        .pNext  = !exp ? NULL : &enext,
        .format = av_vkfmt_from_pixfmt(hwfc->sw_format)[0],
        .type   = VK_IMAGE_TYPE_2D,
        .tiling = hwctx->tiling,
        .usage  = hwctx->usage,
        .flags  = VK_IMAGE_CREATE_ALIAS_BIT,
    };

    ret = vkGetPhysicalDeviceImageFormatProperties2(dev_hwctx->phys_dev,
                                                    &pinfo, &props);
    if (ret == VK_SUCCESS) {
        *iexp |= exp;
        *comp_handle_types |= eprops.externalMemoryProperties.compatibleHandleTypes;
    }
}

static AVBufferRef *vulkan_pool_alloc(void *opaque, int size)
{
    int err;
    AVVkFrame *f;
    AVBufferRef *avbuf = NULL;
    AVHWFramesContext *hwfc = opaque;
    AVVulkanFramesContext *hwctx = hwfc->hwctx;
    VulkanDevicePriv *p = hwfc->device_ctx->internal->priv;
    VulkanFramesPriv *fp = hwfc->internal->priv;
    VkExportMemoryAllocateInfo eminfo[AV_NUM_DATA_POINTERS];
    VkExternalMemoryHandleTypeFlags e = 0x0;

    VkExternalMemoryImageCreateInfo eiinfo = {
        .sType       = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
        .pNext       = hwctx->create_pnext,
    };

    if (p->extensions & EXT_EXTERNAL_FD_MEMORY)
        try_export_flags(hwfc, &eiinfo.handleTypes, &e,
                         VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT);

    if (p->extensions & EXT_EXTERNAL_DMABUF_MEMORY)
        try_export_flags(hwfc, &eiinfo.handleTypes, &e,
                         VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT);

    for (int i = 0; i < av_pix_fmt_count_planes(hwfc->sw_format); i++) {
        eminfo[i].sType       = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
        eminfo[i].pNext       = hwctx->alloc_pnext[i];
        eminfo[i].handleTypes = e;
    }

    err = create_frame(hwfc, &f, hwctx->tiling, hwctx->usage,
                       eiinfo.handleTypes ? &eiinfo : NULL);
    if (err)
        return NULL;

    err = alloc_bind_mem(hwfc, f, eminfo, sizeof(*eminfo));
    if (err)
        goto fail;

    err = prepare_frame(hwfc, &fp->conv_ctx, f, PREP_MODE_WRITE);
    if (err)
        goto fail;

    avbuf = av_buffer_create((uint8_t *)f, sizeof(AVVkFrame),
                             vulkan_frame_free, hwfc, 0);
    if (!avbuf)
        goto fail;

    return avbuf;

fail:
    vulkan_frame_free(hwfc, (uint8_t *)f);
    return NULL;
}

static void vulkan_frames_uninit(AVHWFramesContext *hwfc)
{
    VulkanFramesPriv *fp = hwfc->internal->priv;

    free_exec_ctx(hwfc, &fp->conv_ctx);
    free_exec_ctx(hwfc, &fp->upload_ctx);
    free_exec_ctx(hwfc, &fp->download_ctx);
}

static int vulkan_frames_init(AVHWFramesContext *hwfc)
{
    int err;
    AVVkFrame *f;
    AVVulkanFramesContext *hwctx = hwfc->hwctx;
    VulkanFramesPriv *fp = hwfc->internal->priv;
    AVVulkanDeviceContext *dev_hwctx = hwfc->device_ctx->hwctx;
    VulkanDevicePriv *p = hwfc->device_ctx->internal->priv;

    /* Default pool flags */
    hwctx->tiling = hwctx->tiling ? hwctx->tiling : p->use_linear_images ?
                    VK_IMAGE_TILING_LINEAR : VK_IMAGE_TILING_OPTIMAL;

    if (!hwctx->usage)
        hwctx->usage = DEFAULT_USAGE_FLAGS;

    err = create_exec_ctx(hwfc, &fp->conv_ctx,
                          dev_hwctx->queue_family_comp_index,
                          GET_QUEUE_COUNT(dev_hwctx, 0, 1, 0));
    if (err)
        goto fail;

    err = create_exec_ctx(hwfc, &fp->upload_ctx,
                          dev_hwctx->queue_family_tx_index,
                          GET_QUEUE_COUNT(dev_hwctx, 0, 0, 1));
    if (err)
        goto fail;

    err = create_exec_ctx(hwfc, &fp->download_ctx,
                          dev_hwctx->queue_family_tx_index, 1);
    if (err)
        goto fail;

    /* Test to see if allocation will fail */
    err = create_frame(hwfc, &f, hwctx->tiling, hwctx->usage,
                       hwctx->create_pnext);
    if (err)
        goto fail;

    vulkan_frame_free(hwfc, (uint8_t *)f);

    /* If user did not specify a pool, hwfc->pool will be set to the internal one
     * in hwcontext.c just after this gets called */
    if (!hwfc->pool) {
        hwfc->internal->pool_internal = av_buffer_pool_init2(sizeof(AVVkFrame),
                                                             hwfc, vulkan_pool_alloc,
                                                             NULL);
        if (!hwfc->internal->pool_internal) {
            err = AVERROR(ENOMEM);
            goto fail;
        }
    }

    return 0;

fail:
    free_exec_ctx(hwfc, &fp->conv_ctx);
    free_exec_ctx(hwfc, &fp->upload_ctx);
    free_exec_ctx(hwfc, &fp->download_ctx);

    return err;
}

static int vulkan_get_buffer(AVHWFramesContext *hwfc, AVFrame *frame)
{
    frame->buf[0] = av_buffer_pool_get(hwfc->pool);
    if (!frame->buf[0])
        return AVERROR(ENOMEM);

    frame->data[0] = frame->buf[0]->data;
    frame->format  = AV_PIX_FMT_VULKAN;
    frame->width   = hwfc->width;
    frame->height  = hwfc->height;

    return 0;
}

static int vulkan_transfer_get_formats(AVHWFramesContext *hwfc,
                                       enum AVHWFrameTransferDirection dir,
                                       enum AVPixelFormat **formats)
{
    enum AVPixelFormat *fmts = av_malloc_array(2, sizeof(*fmts));
    if (!fmts)
        return AVERROR(ENOMEM);

    fmts[0] = hwfc->sw_format;
    fmts[1] = AV_PIX_FMT_NONE;

    *formats = fmts;
    return 0;
}

typedef struct VulkanMapping {
    AVVkFrame *frame;
    int flags;
} VulkanMapping;

static void vulkan_unmap_frame(AVHWFramesContext *hwfc, HWMapDescriptor *hwmap)
{
    VulkanMapping *map = hwmap->priv;
    AVVulkanDeviceContext *hwctx = hwfc->device_ctx->hwctx;
    const int planes = av_pix_fmt_count_planes(hwfc->sw_format);

    /* Check if buffer needs flushing */
    if ((map->flags & AV_HWFRAME_MAP_WRITE) &&
        !(map->frame->flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        VkResult ret;
        VkMappedMemoryRange flush_ranges[AV_NUM_DATA_POINTERS] = { { 0 } };

        for (int i = 0; i < planes; i++) {
            flush_ranges[i].sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            flush_ranges[i].memory = map->frame->mem[i];
            flush_ranges[i].size   = VK_WHOLE_SIZE;
        }

        ret = vkFlushMappedMemoryRanges(hwctx->act_dev, planes,
                                        flush_ranges);
        if (ret != VK_SUCCESS) {
            av_log(hwfc, AV_LOG_ERROR, "Failed to flush memory: %s\n",
                   vk_ret2str(ret));
        }
    }

    for (int i = 0; i < planes; i++)
        vkUnmapMemory(hwctx->act_dev, map->frame->mem[i]);

    av_free(map);
}

static int vulkan_map_frame_to_mem(AVHWFramesContext *hwfc, AVFrame *dst,
                                   const AVFrame *src, int flags)
{
    VkResult ret;
    int err, mapped_mem_count = 0;
    AVVkFrame *f = (AVVkFrame *)src->data[0];
    AVVulkanDeviceContext *hwctx = hwfc->device_ctx->hwctx;
    const int planes = av_pix_fmt_count_planes(hwfc->sw_format);

    VulkanMapping *map = av_mallocz(sizeof(VulkanMapping));
    if (!map)
        return AVERROR(EINVAL);

    if (src->format != AV_PIX_FMT_VULKAN) {
        av_log(hwfc, AV_LOG_ERROR, "Cannot map from pixel format %s!\n",
               av_get_pix_fmt_name(src->format));
        err = AVERROR(EINVAL);
        goto fail;
    }

    if (!(f->flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) ||
        !(f->tiling == VK_IMAGE_TILING_LINEAR)) {
        av_log(hwfc, AV_LOG_ERROR, "Unable to map frame, not host visible "
               "and linear!\n");
        err = AVERROR(EINVAL);
        goto fail;
    }

    dst->width  = src->width;
    dst->height = src->height;

    for (int i = 0; i < planes; i++) {
        ret = vkMapMemory(hwctx->act_dev, f->mem[i], 0,
                          VK_WHOLE_SIZE, 0, (void **)&dst->data[i]);
        if (ret != VK_SUCCESS) {
            av_log(hwfc, AV_LOG_ERROR, "Failed to map image memory: %s\n",
                vk_ret2str(ret));
            err = AVERROR_EXTERNAL;
            goto fail;
        }
        mapped_mem_count++;
    }

    /* Check if the memory contents matter */
    if (((flags & AV_HWFRAME_MAP_READ) || !(flags & AV_HWFRAME_MAP_OVERWRITE)) &&
        !(f->flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        VkMappedMemoryRange map_mem_ranges[AV_NUM_DATA_POINTERS] = { { 0 } };
        for (int i = 0; i < planes; i++) {
            map_mem_ranges[i].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            map_mem_ranges[i].size = VK_WHOLE_SIZE;
            map_mem_ranges[i].memory = f->mem[i];
        }

        ret = vkInvalidateMappedMemoryRanges(hwctx->act_dev, planes,
                                             map_mem_ranges);
        if (ret != VK_SUCCESS) {
            av_log(hwfc, AV_LOG_ERROR, "Failed to invalidate memory: %s\n",
                   vk_ret2str(ret));
            err = AVERROR_EXTERNAL;
            goto fail;
        }
    }

    for (int i = 0; i < planes; i++) {
        VkImageSubresource sub = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        };
        VkSubresourceLayout layout;
        vkGetImageSubresourceLayout(hwctx->act_dev, f->img[i], &sub, &layout);
        dst->linesize[i] = layout.rowPitch;
    }

    map->frame = f;
    map->flags = flags;

    err = ff_hwframe_map_create(src->hw_frames_ctx, dst, src,
                                &vulkan_unmap_frame, map);
    if (err < 0)
        goto fail;

    return 0;

fail:
    for (int i = 0; i < mapped_mem_count; i++)
        vkUnmapMemory(hwctx->act_dev, f->mem[i]);

    av_free(map);
    return err;
}

#if CONFIG_LIBDRM
static void vulkan_unmap_from(AVHWFramesContext *hwfc, HWMapDescriptor *hwmap)
{
    VulkanMapping *map = hwmap->priv;
    AVVulkanDeviceContext *hwctx = hwfc->device_ctx->hwctx;
    const int planes = av_pix_fmt_count_planes(hwfc->sw_format);

    for (int i = 0; i < planes; i++) {
        vkDestroyImage(hwctx->act_dev, map->frame->img[i], hwctx->alloc);
        vkFreeMemory(hwctx->act_dev, map->frame->mem[i], hwctx->alloc);
        vkDestroySemaphore(hwctx->act_dev, map->frame->sem[i], hwctx->alloc);
    }

    av_freep(&map->frame);
}

static const struct {
    uint32_t drm_fourcc;
    VkFormat vk_format;
} vulkan_drm_format_map[] = {
    { DRM_FORMAT_R8,       VK_FORMAT_R8_UNORM       },
    { DRM_FORMAT_R16,      VK_FORMAT_R16_UNORM      },
    { DRM_FORMAT_GR88,     VK_FORMAT_R8G8_UNORM     },
    { DRM_FORMAT_RG88,     VK_FORMAT_R8G8_UNORM     },
    { DRM_FORMAT_GR1616,   VK_FORMAT_R16G16_UNORM   },
    { DRM_FORMAT_RG1616,   VK_FORMAT_R16G16_UNORM   },
    { DRM_FORMAT_ARGB8888, VK_FORMAT_B8G8R8A8_UNORM },
    { DRM_FORMAT_XRGB8888, VK_FORMAT_B8G8R8A8_UNORM },
    { DRM_FORMAT_ABGR8888, VK_FORMAT_R8G8B8A8_UNORM },
    { DRM_FORMAT_XBGR8888, VK_FORMAT_R8G8B8A8_UNORM },
};

static inline VkFormat drm_to_vulkan_fmt(uint32_t drm_fourcc)
{
    for (int i = 0; i < FF_ARRAY_ELEMS(vulkan_drm_format_map); i++)
        if (vulkan_drm_format_map[i].drm_fourcc == drm_fourcc)
            return vulkan_drm_format_map[i].vk_format;
    return VK_FORMAT_UNDEFINED;
}

static int vulkan_map_from_drm_frame_desc(AVHWFramesContext *hwfc, AVVkFrame **frame,
                                          AVDRMFrameDescriptor *desc)
{
    int err = 0;
    VkResult ret;
    AVVkFrame *f;
    int bind_counts = 0;
    AVHWDeviceContext *ctx = hwfc->device_ctx;
    AVVulkanDeviceContext *hwctx = ctx->hwctx;
    VulkanDevicePriv *p = ctx->internal->priv;
    VulkanFramesPriv *fp = hwfc->internal->priv;
    AVVulkanFramesContext *frames_hwctx = hwfc->hwctx;
    const AVPixFmtDescriptor *fmt_desc = av_pix_fmt_desc_get(hwfc->sw_format);
    const int has_modifiers = p->extensions & EXT_DRM_MODIFIER_FLAGS;
    VkSubresourceLayout plane_data[AV_NUM_DATA_POINTERS] = { 0 };
    VkBindImageMemoryInfo bind_info[AV_NUM_DATA_POINTERS] = { 0 };
    VkBindImagePlaneMemoryInfo plane_info[AV_NUM_DATA_POINTERS] = { 0 };
    VkExternalMemoryHandleTypeFlagBits htype = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

    VK_LOAD_PFN(hwctx->inst, vkGetMemoryFdPropertiesKHR);

    for (int i = 0; i < desc->nb_layers; i++) {
        if (drm_to_vulkan_fmt(desc->layers[i].format) == VK_FORMAT_UNDEFINED) {
            av_log(ctx, AV_LOG_ERROR, "Unsupported DMABUF layer format %#08x!\n",
                   desc->layers[i].format);
            return AVERROR(EINVAL);
        }
    }

    if (!(f = av_vk_frame_alloc())) {
        av_log(ctx, AV_LOG_ERROR, "Unable to allocate memory for AVVkFrame!\n");
        err = AVERROR(ENOMEM);
        goto fail;
    }

    f->tiling = has_modifiers ? VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT :
                desc->objects[0].format_modifier == DRM_FORMAT_MOD_LINEAR ?
                VK_IMAGE_TILING_LINEAR : VK_IMAGE_TILING_OPTIMAL;

    for (int i = 0; i < desc->nb_layers; i++) {
        const int planes = desc->layers[i].nb_planes;
        VkImageDrmFormatModifierExplicitCreateInfoEXT drm_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT,
            .drmFormatModifier = desc->objects[0].format_modifier,
            .drmFormatModifierPlaneCount = planes,
            .pPlaneLayouts = (const VkSubresourceLayout *)&plane_data,
        };

        VkExternalMemoryImageCreateInfo einfo = {
            .sType       = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
            .pNext       = has_modifiers ? &drm_info : NULL,
            .handleTypes = htype,
        };

        VkSemaphoreCreateInfo sem_spawn = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        };

        const int p_w = i > 0 ? AV_CEIL_RSHIFT(hwfc->width, fmt_desc->log2_chroma_w) : hwfc->width;
        const int p_h = i > 0 ? AV_CEIL_RSHIFT(hwfc->height, fmt_desc->log2_chroma_h) : hwfc->height;

        VkImageCreateInfo image_create_info = {
            .sType                 = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext                 = &einfo,
            .imageType             = VK_IMAGE_TYPE_2D,
            .format                = drm_to_vulkan_fmt(desc->layers[i].format),
            .extent.width          = p_w,
            .extent.height         = p_h,
            .extent.depth          = 1,
            .mipLevels             = 1,
            .arrayLayers           = 1,
            .flags                 = VK_IMAGE_CREATE_ALIAS_BIT,
            .tiling                = f->tiling,
            .initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED, /* specs say so */
            .usage                 = frames_hwctx->usage,
            .samples               = VK_SAMPLE_COUNT_1_BIT,
            .pQueueFamilyIndices   = p->qfs,
            .queueFamilyIndexCount = p->num_qfs,
            .sharingMode           = p->num_qfs > 1 ? VK_SHARING_MODE_CONCURRENT :
                                                      VK_SHARING_MODE_EXCLUSIVE,
        };

        for (int j = 0; j < planes; j++) {
            plane_data[j].offset     = desc->layers[i].planes[j].offset;
            plane_data[j].rowPitch   = desc->layers[i].planes[j].pitch;
            plane_data[j].size       = 0; /* The specs say so for all 3 */
            plane_data[j].arrayPitch = 0;
            plane_data[j].depthPitch = 0;
        }

        /* Create image */
        ret = vkCreateImage(hwctx->act_dev, &image_create_info,
                            hwctx->alloc, &f->img[i]);
        if (ret != VK_SUCCESS) {
            av_log(ctx, AV_LOG_ERROR, "Image creation failure: %s\n",
                   vk_ret2str(ret));
            err = AVERROR(EINVAL);
            goto fail;
        }

        ret = vkCreateSemaphore(hwctx->act_dev, &sem_spawn,
                                hwctx->alloc, &f->sem[i]);
        if (ret != VK_SUCCESS) {
            av_log(hwctx, AV_LOG_ERROR, "Failed to create semaphore: %s\n",
                   vk_ret2str(ret));
            return AVERROR_EXTERNAL;
        }

        /* We'd import a semaphore onto the one we created using
         * vkImportSemaphoreFdKHR but unfortunately neither DRM nor VAAPI
         * offer us anything we could import and sync with, so instead
         * just signal the semaphore we created. */

        f->layout[i] = image_create_info.initialLayout;
        f->access[i] = 0x0;
    }

    for (int i = 0; i < desc->nb_objects; i++) {
        int use_ded_mem = 0;
        VkMemoryFdPropertiesKHR fdmp = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR,
        };
        VkMemoryRequirements req = {
            .size = desc->objects[i].size,
        };
        VkImportMemoryFdInfoKHR idesc = {
            .sType      = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
            .handleType = htype,
            .fd         = dup(desc->objects[i].fd),
        };
        VkMemoryDedicatedAllocateInfo ded_alloc = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
            .pNext = &idesc,
        };

        ret = pfn_vkGetMemoryFdPropertiesKHR(hwctx->act_dev, htype,
                                             idesc.fd, &fdmp);
        if (ret != VK_SUCCESS) {
            av_log(hwfc, AV_LOG_ERROR, "Failed to get FD properties: %s\n",
                   vk_ret2str(ret));
            err = AVERROR_EXTERNAL;
            close(idesc.fd);
            goto fail;
        }

        req.memoryTypeBits = fdmp.memoryTypeBits;

        /* Dedicated allocation only makes sense if there's a one to one mapping
         * between images and the memory backing them, so only check in this
         * case. */
        if (desc->nb_layers == desc->nb_objects) {
            VkImageMemoryRequirementsInfo2 req_desc = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
                .image = f->img[i],
            };
            VkMemoryDedicatedRequirements ded_req = {
                .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS,
            };
            VkMemoryRequirements2 req2 = {
                .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
                .pNext = &ded_req,
            };

            vkGetImageMemoryRequirements2(hwctx->act_dev, &req_desc, &req2);

            use_ded_mem = ded_req.prefersDedicatedAllocation |
                          ded_req.requiresDedicatedAllocation;
            if (use_ded_mem)
                ded_alloc.image = f->img[i];
        }

        err = alloc_mem(ctx, &req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        use_ded_mem ? &ded_alloc : ded_alloc.pNext,
                        &f->flags, &f->mem[i]);
        if (err) {
            close(idesc.fd);
            return err;
        }

        f->size[i] = desc->objects[i].size;
    }

    for (int i = 0; i < desc->nb_layers; i++) {
        const int planes = desc->layers[i].nb_planes;
        const int signal_p = has_modifiers && (planes > 1);
        for (int j = 0; j < planes; j++) {
            VkImageAspectFlagBits aspect = j == 0 ? VK_IMAGE_ASPECT_MEMORY_PLANE_0_BIT_EXT :
                                           j == 1 ? VK_IMAGE_ASPECT_MEMORY_PLANE_1_BIT_EXT :
                                                    VK_IMAGE_ASPECT_MEMORY_PLANE_2_BIT_EXT;

            plane_info[bind_counts].sType = VK_STRUCTURE_TYPE_BIND_IMAGE_PLANE_MEMORY_INFO;
            plane_info[bind_counts].planeAspect = aspect;

            bind_info[bind_counts].sType  = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO;
            bind_info[bind_counts].pNext  = signal_p ? &plane_info[bind_counts] : NULL;
            bind_info[bind_counts].image  = f->img[i];
            bind_info[bind_counts].memory = f->mem[desc->layers[i].planes[j].object_index];
            bind_info[bind_counts].memoryOffset = desc->layers[i].planes[j].offset;
            bind_counts++;
        }
    }

    /* Bind the allocated memory to the images */
    ret = vkBindImageMemory2(hwctx->act_dev, bind_counts, bind_info);
    if (ret != VK_SUCCESS) {
        av_log(ctx, AV_LOG_ERROR, "Failed to bind memory: %s\n",
               vk_ret2str(ret));
        return AVERROR_EXTERNAL;
    }

    /* NOTE: This is completely uneccesary and unneeded once we can import
     * semaphores from DRM. Otherwise we have to activate the semaphores.
     * We're reusing the exec context that's also used for uploads/downloads. */
    err = prepare_frame(hwfc, &fp->conv_ctx, f, PREP_MODE_RO_SHADER);
    if (err)
        goto fail;

    *frame = f;

    return 0;

fail:
    for (int i = 0; i < desc->nb_layers; i++) {
        vkDestroyImage(hwctx->act_dev, f->img[i], hwctx->alloc);
        vkDestroySemaphore(hwctx->act_dev, f->sem[i], hwctx->alloc);
    }
    for (int i = 0; i < desc->nb_objects; i++)
        vkFreeMemory(hwctx->act_dev, f->mem[i], hwctx->alloc);

    av_free(f);

    return err;
}

static int vulkan_map_from_drm(AVHWFramesContext *hwfc, AVFrame *dst,
                               const AVFrame *src, int flags)
{
    int err = 0;
    AVVkFrame *f;
    VulkanMapping *map = NULL;

    err = vulkan_map_from_drm_frame_desc(hwfc, &f,
                                         (AVDRMFrameDescriptor *)src->data[0]);
    if (err)
        return err;

    /* The unmapping function will free this */
    dst->data[0] = (uint8_t *)f;
    dst->width   = src->width;
    dst->height  = src->height;

    map = av_mallocz(sizeof(VulkanMapping));
    if (!map)
        goto fail;

    map->frame = f;
    map->flags = flags;

    err = ff_hwframe_map_create(dst->hw_frames_ctx, dst, src,
                                &vulkan_unmap_from, map);
    if (err < 0)
        goto fail;

    av_log(hwfc, AV_LOG_DEBUG, "Mapped DRM object to Vulkan!\n");

    return 0;

fail:
    vulkan_frame_free(hwfc->device_ctx->hwctx, (uint8_t *)f);
    av_free(map);
    return err;
}

#if CONFIG_VAAPI
static int vulkan_map_from_vaapi(AVHWFramesContext *dst_fc,
                                 AVFrame *dst, const AVFrame *src,
                                 int flags)
{
    int err;
    AVFrame *tmp = av_frame_alloc();
    AVHWFramesContext *vaapi_fc = (AVHWFramesContext*)src->hw_frames_ctx->data;
    AVVAAPIDeviceContext *vaapi_ctx = vaapi_fc->device_ctx->hwctx;
    VASurfaceID surface_id = (VASurfaceID)(uintptr_t)src->data[3];

    if (!tmp)
        return AVERROR(ENOMEM);

    /* We have to sync since like the previous comment said, no semaphores */
    vaSyncSurface(vaapi_ctx->display, surface_id);

    tmp->format = AV_PIX_FMT_DRM_PRIME;

    err = av_hwframe_map(tmp, src, flags);
    if (err < 0)
        goto fail;

    err = vulkan_map_from_drm(dst_fc, dst, tmp, flags);
    if (err < 0)
        goto fail;

    err = ff_hwframe_map_replace(dst, src);

fail:
    av_frame_free(&tmp);
    return err;
}
#endif
#endif

#if CONFIG_CUDA
static int vulkan_export_to_cuda(AVHWFramesContext *hwfc,
                                 AVBufferRef *cuda_hwfc,
                                 const AVFrame *frame)
{
    int err;
    VkResult ret;
    AVVkFrame *dst_f;
    AVVkFrameInternal *dst_int;
    AVHWDeviceContext *ctx = hwfc->device_ctx;
    AVVulkanDeviceContext *hwctx = ctx->hwctx;
    const int planes = av_pix_fmt_count_planes(hwfc->sw_format);
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(hwfc->sw_format);
    VK_LOAD_PFN(hwctx->inst, vkGetMemoryFdKHR);
    VK_LOAD_PFN(hwctx->inst, vkGetSemaphoreFdKHR);

    AVHWFramesContext *cuda_fc = (AVHWFramesContext*)cuda_hwfc->data;
    AVHWDeviceContext *cuda_cu = cuda_fc->device_ctx;
    AVCUDADeviceContext *cuda_dev = cuda_cu->hwctx;
    AVCUDADeviceContextInternal *cu_internal = cuda_dev->internal;
    CudaFunctions *cu = cu_internal->cuda_dl;
    CUarray_format cufmt = desc->comp[0].depth > 8 ? CU_AD_FORMAT_UNSIGNED_INT16 :
                                                     CU_AD_FORMAT_UNSIGNED_INT8;

    dst_f = (AVVkFrame *)frame->data[0];

    dst_int = dst_f->internal;
    if (!dst_int || !dst_int->cuda_fc_ref) {
        if (!dst_f->internal)
            dst_f->internal = dst_int = av_mallocz(sizeof(*dst_f->internal));

        if (!dst_int) {
            err = AVERROR(ENOMEM);
            goto fail;
        }

        dst_int->cuda_fc_ref = av_buffer_ref(cuda_hwfc);
        if (!dst_int->cuda_fc_ref) {
            err = AVERROR(ENOMEM);
            goto fail;
        }

        for (int i = 0; i < planes; i++) {
            CUDA_EXTERNAL_MEMORY_MIPMAPPED_ARRAY_DESC tex_desc = {
                .offset = 0,
                .arrayDesc = {
                    .Width  = i > 0 ? AV_CEIL_RSHIFT(hwfc->width, desc->log2_chroma_w)
                                    : hwfc->width,
                    .Height = i > 0 ? AV_CEIL_RSHIFT(hwfc->height, desc->log2_chroma_h)
                                    : hwfc->height,
                    .Depth = 0,
                    .Format = cufmt,
                    .NumChannels = 1 + ((planes == 2) && i),
                    .Flags = 0,
                },
                .numLevels = 1,
            };
            CUDA_EXTERNAL_MEMORY_HANDLE_DESC ext_desc = {
                .type = CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD,
                .size = dst_f->size[i],
            };
            VkMemoryGetFdInfoKHR export_info = {
                .sType      = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,
                .memory     = dst_f->mem[i],
                .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT_KHR,
            };
            VkSemaphoreGetFdInfoKHR sem_export = {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR,
                .semaphore = dst_f->sem[i],
                .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT,
            };
            CUDA_EXTERNAL_SEMAPHORE_HANDLE_DESC ext_sem_desc = {
                .type = CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD,
            };

            ret = pfn_vkGetMemoryFdKHR(hwctx->act_dev, &export_info,
                                       &ext_desc.handle.fd);
            if (ret != VK_SUCCESS) {
                av_log(hwfc, AV_LOG_ERROR, "Unable to export the image as a FD!\n");
                err = AVERROR_EXTERNAL;
                goto fail;
            }

            ret = CHECK_CU(cu->cuImportExternalMemory(&dst_int->ext_mem[i], &ext_desc));
            if (ret < 0) {
                err = AVERROR_EXTERNAL;
                goto fail;
            }

            ret = CHECK_CU(cu->cuExternalMemoryGetMappedMipmappedArray(&dst_int->cu_mma[i],
                                                                       dst_int->ext_mem[i],
                                                                       &tex_desc));
            if (ret < 0) {
                err = AVERROR_EXTERNAL;
                goto fail;
            }

            ret = CHECK_CU(cu->cuMipmappedArrayGetLevel(&dst_int->cu_array[i],
                                                        dst_int->cu_mma[i], 0));
            if (ret < 0) {
                err = AVERROR_EXTERNAL;
                goto fail;
            }

            ret = pfn_vkGetSemaphoreFdKHR(hwctx->act_dev, &sem_export,
                                          &ext_sem_desc.handle.fd);
            if (ret != VK_SUCCESS) {
                av_log(ctx, AV_LOG_ERROR, "Failed to export semaphore: %s\n",
                       vk_ret2str(ret));
                err = AVERROR_EXTERNAL;
                goto fail;
            }

            ret = CHECK_CU(cu->cuImportExternalSemaphore(&dst_int->cu_sem[i],
                                                         &ext_sem_desc));
            if (ret < 0) {
                err = AVERROR_EXTERNAL;
                goto fail;
            }
        }
    }

    return 0;

fail:
    return err;
}

static int vulkan_transfer_data_from_cuda(AVHWFramesContext *hwfc,
                                          AVFrame *dst, const AVFrame *src)
{
    int err;
    VkResult ret;
    CUcontext dummy;
    AVVkFrame *dst_f;
    AVVkFrameInternal *dst_int;
    const int planes = av_pix_fmt_count_planes(hwfc->sw_format);
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(hwfc->sw_format);

    AVHWFramesContext *cuda_fc = (AVHWFramesContext*)src->hw_frames_ctx->data;
    AVHWDeviceContext *cuda_cu = cuda_fc->device_ctx;
    AVCUDADeviceContext *cuda_dev = cuda_cu->hwctx;
    AVCUDADeviceContextInternal *cu_internal = cuda_dev->internal;
    CudaFunctions *cu = cu_internal->cuda_dl;
    CUDA_EXTERNAL_SEMAPHORE_WAIT_PARAMS s_w_par[AV_NUM_DATA_POINTERS] = { 0 };
    CUDA_EXTERNAL_SEMAPHORE_SIGNAL_PARAMS s_s_par[AV_NUM_DATA_POINTERS] = { 0 };

    ret = CHECK_CU(cu->cuCtxPushCurrent(cuda_dev->cuda_ctx));
    if (ret < 0) {
        err = AVERROR_EXTERNAL;
        goto fail;
    }

    dst_f = (AVVkFrame *)dst->data[0];

    ret = vulkan_export_to_cuda(hwfc, src->hw_frames_ctx, dst);
    if (ret < 0) {
        goto fail;
    }
    dst_int = dst_f->internal;

    ret = CHECK_CU(cu->cuWaitExternalSemaphoresAsync(dst_int->cu_sem, s_w_par,
                                                     planes, cuda_dev->stream));
    if (ret < 0) {
        err = AVERROR_EXTERNAL;
        goto fail;
    }

    for (int i = 0; i < planes; i++) {
        CUDA_MEMCPY2D cpy = {
            .srcMemoryType = CU_MEMORYTYPE_DEVICE,
            .srcDevice     = (CUdeviceptr)src->data[i],
            .srcPitch      = src->linesize[i],
            .srcY          = 0,

            .dstMemoryType = CU_MEMORYTYPE_ARRAY,
            .dstArray      = dst_int->cu_array[i],
            .WidthInBytes  = (i > 0 ? AV_CEIL_RSHIFT(hwfc->width, desc->log2_chroma_w)
                                    : hwfc->width) * desc->comp[i].step,
            .Height        = i > 0 ? AV_CEIL_RSHIFT(hwfc->height, desc->log2_chroma_h)
                                   : hwfc->height,
        };

        ret = CHECK_CU(cu->cuMemcpy2DAsync(&cpy, cuda_dev->stream));
        if (ret < 0) {
            err = AVERROR_EXTERNAL;
            goto fail;
        }
    }

    ret = CHECK_CU(cu->cuSignalExternalSemaphoresAsync(dst_int->cu_sem, s_s_par,
                                                       planes, cuda_dev->stream));
    if (ret < 0) {
        err = AVERROR_EXTERNAL;
        goto fail;
    }

    CHECK_CU(cu->cuCtxPopCurrent(&dummy));

    av_log(hwfc, AV_LOG_VERBOSE, "Transfered CUDA image to Vulkan!\n");

    return 0;

fail:
    CHECK_CU(cu->cuCtxPopCurrent(&dummy));
    vulkan_free_internal(dst_int);
    dst_f->internal = NULL;
    av_buffer_unref(&dst->buf[0]);
    return err;
}
#endif

static int vulkan_map_to(AVHWFramesContext *hwfc, AVFrame *dst,
                         const AVFrame *src, int flags)
{
    av_unused VulkanDevicePriv *p = hwfc->device_ctx->internal->priv;

    switch (src->format) {
#if CONFIG_LIBDRM
#if CONFIG_VAAPI
    case AV_PIX_FMT_VAAPI:
        if (p->extensions & EXT_EXTERNAL_DMABUF_MEMORY)
            return vulkan_map_from_vaapi(hwfc, dst, src, flags);
#endif
    case AV_PIX_FMT_DRM_PRIME:
        if (p->extensions & EXT_EXTERNAL_DMABUF_MEMORY)
            return vulkan_map_from_drm(hwfc, dst, src, flags);
#endif
    default:
        return AVERROR(ENOSYS);
    }
}

#if CONFIG_LIBDRM
typedef struct VulkanDRMMapping {
    AVDRMFrameDescriptor drm_desc;
    AVVkFrame *source;
} VulkanDRMMapping;

static void vulkan_unmap_to_drm(AVHWFramesContext *hwfc, HWMapDescriptor *hwmap)
{
    AVDRMFrameDescriptor *drm_desc = hwmap->priv;

    for (int i = 0; i < drm_desc->nb_objects; i++)
        close(drm_desc->objects[i].fd);

    av_free(drm_desc);
}

static inline uint32_t vulkan_fmt_to_drm(VkFormat vkfmt)
{
    for (int i = 0; i < FF_ARRAY_ELEMS(vulkan_drm_format_map); i++)
        if (vulkan_drm_format_map[i].vk_format == vkfmt)
            return vulkan_drm_format_map[i].drm_fourcc;
    return DRM_FORMAT_INVALID;
}

static int vulkan_map_to_drm(AVHWFramesContext *hwfc, AVFrame *dst,
                             const AVFrame *src, int flags)
{
    int err = 0;
    VkResult ret;
    AVVkFrame *f = (AVVkFrame *)src->data[0];
    VulkanDevicePriv *p = hwfc->device_ctx->internal->priv;
    VulkanFramesPriv *fp = hwfc->internal->priv;
    AVVulkanDeviceContext *hwctx = hwfc->device_ctx->hwctx;
    const int planes = av_pix_fmt_count_planes(hwfc->sw_format);
    VK_LOAD_PFN(hwctx->inst, vkGetMemoryFdKHR);
    VkImageDrmFormatModifierPropertiesEXT drm_mod = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_PROPERTIES_EXT,
    };

    AVDRMFrameDescriptor *drm_desc = av_mallocz(sizeof(*drm_desc));
    if (!drm_desc)
        return AVERROR(ENOMEM);

    err = prepare_frame(hwfc, &fp->conv_ctx, f, PREP_MODE_EXTERNAL_EXPORT);
    if (err < 0)
        goto end;

    err = ff_hwframe_map_create(src->hw_frames_ctx, dst, src, &vulkan_unmap_to_drm, drm_desc);
    if (err < 0)
        goto end;

    if (p->extensions & EXT_DRM_MODIFIER_FLAGS) {
        VK_LOAD_PFN(hwctx->inst, vkGetImageDrmFormatModifierPropertiesEXT);
        ret = pfn_vkGetImageDrmFormatModifierPropertiesEXT(hwctx->act_dev, f->img[0],
                                                           &drm_mod);
        if (ret != VK_SUCCESS) {
            av_log(hwfc, AV_LOG_ERROR, "Failed to retrieve DRM format modifier!\n");
            err = AVERROR_EXTERNAL;
            goto end;
        }
    }

    for (int i = 0; (i < planes) && (f->mem[i]); i++) {
        VkMemoryGetFdInfoKHR export_info = {
            .sType      = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,
            .memory     = f->mem[i],
            .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
        };

        ret = pfn_vkGetMemoryFdKHR(hwctx->act_dev, &export_info,
                                   &drm_desc->objects[i].fd);
        if (ret != VK_SUCCESS) {
            av_log(hwfc, AV_LOG_ERROR, "Unable to export the image as a FD!\n");
            err = AVERROR_EXTERNAL;
            goto end;
        }

        drm_desc->nb_objects++;
        drm_desc->objects[i].size = f->size[i];
        drm_desc->objects[i].format_modifier = drm_mod.drmFormatModifier;
    }

    drm_desc->nb_layers = planes;
    for (int i = 0; i < drm_desc->nb_layers; i++) {
        VkSubresourceLayout layout;
        VkImageSubresource sub = {
            .aspectMask = p->extensions & EXT_DRM_MODIFIER_FLAGS ?
                          VK_IMAGE_ASPECT_MEMORY_PLANE_0_BIT_EXT :
                          VK_IMAGE_ASPECT_COLOR_BIT,
        };
        VkFormat plane_vkfmt = av_vkfmt_from_pixfmt(hwfc->sw_format)[i];

        drm_desc->layers[i].format    = vulkan_fmt_to_drm(plane_vkfmt);
        drm_desc->layers[i].nb_planes = 1;

        if (drm_desc->layers[i].format == DRM_FORMAT_INVALID) {
            av_log(hwfc, AV_LOG_ERROR, "Cannot map to DRM layer, unsupported!\n");
            err = AVERROR_PATCHWELCOME;
            goto end;
        }

        drm_desc->layers[i].planes[0].object_index = FFMIN(i, drm_desc->nb_objects - 1);

        if (f->tiling == VK_IMAGE_TILING_OPTIMAL)
            continue;

        vkGetImageSubresourceLayout(hwctx->act_dev, f->img[i], &sub, &layout);
        drm_desc->layers[i].planes[0].offset       = layout.offset;
        drm_desc->layers[i].planes[0].pitch        = layout.rowPitch;
    }

    dst->width   = src->width;
    dst->height  = src->height;
    dst->data[0] = (uint8_t *)drm_desc;

    av_log(hwfc, AV_LOG_VERBOSE, "Mapped AVVkFrame to a DRM object!\n");

    return 0;

end:
    av_free(drm_desc);
    return err;
}

#if CONFIG_VAAPI
static int vulkan_map_to_vaapi(AVHWFramesContext *hwfc, AVFrame *dst,
                               const AVFrame *src, int flags)
{
    int err;
    AVFrame *tmp = av_frame_alloc();
    if (!tmp)
        return AVERROR(ENOMEM);

    tmp->format = AV_PIX_FMT_DRM_PRIME;

    err = vulkan_map_to_drm(hwfc, tmp, src, flags);
    if (err < 0)
        goto fail;

    err = av_hwframe_map(dst, tmp, flags);
    if (err < 0)
        goto fail;

    err = ff_hwframe_map_replace(dst, src);

fail:
    av_frame_free(&tmp);
    return err;
}
#endif
#endif

static int vulkan_map_from(AVHWFramesContext *hwfc, AVFrame *dst,
                           const AVFrame *src, int flags)
{
    av_unused VulkanDevicePriv *p = hwfc->device_ctx->internal->priv;

    switch (dst->format) {
#if CONFIG_LIBDRM
    case AV_PIX_FMT_DRM_PRIME:
        if (p->extensions & EXT_EXTERNAL_DMABUF_MEMORY)
            return vulkan_map_to_drm(hwfc, dst, src, flags);
#if CONFIG_VAAPI
    case AV_PIX_FMT_VAAPI:
        if (p->extensions & EXT_EXTERNAL_DMABUF_MEMORY)
            return vulkan_map_to_vaapi(hwfc, dst, src, flags);
#endif
#endif
    default:
        return vulkan_map_frame_to_mem(hwfc, dst, src, flags);
    }
}

typedef struct ImageBuffer {
    VkBuffer buf;
    VkDeviceMemory mem;
    VkMemoryPropertyFlagBits flags;
    int mapped_mem;
} ImageBuffer;

static void free_buf(void *opaque, uint8_t *data)
{
    AVHWDeviceContext *ctx = opaque;
    AVVulkanDeviceContext *hwctx = ctx->hwctx;
    ImageBuffer *vkbuf = (ImageBuffer *)data;

    if (vkbuf->buf)
        vkDestroyBuffer(hwctx->act_dev, vkbuf->buf, hwctx->alloc);
    if (vkbuf->mem)
        vkFreeMemory(hwctx->act_dev, vkbuf->mem, hwctx->alloc);

    av_free(data);
}

static int create_buf(AVHWDeviceContext *ctx, AVBufferRef **buf, size_t imp_size,
                      int height, int *stride, VkBufferUsageFlags usage,
                      VkMemoryPropertyFlagBits flags, void *create_pnext,
                      void *alloc_pnext)
{
    int err;
    VkResult ret;
    int use_ded_mem;
    AVVulkanDeviceContext *hwctx = ctx->hwctx;
    VulkanDevicePriv *p = ctx->internal->priv;

    VkBufferCreateInfo buf_spawn = {
        .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext       = create_pnext,
        .usage       = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VkBufferMemoryRequirementsInfo2 req_desc = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2,
    };
    VkMemoryDedicatedAllocateInfo ded_alloc = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
        .pNext = alloc_pnext,
    };
    VkMemoryDedicatedRequirements ded_req = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS,
    };
    VkMemoryRequirements2 req = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
        .pNext = &ded_req,
    };

    ImageBuffer *vkbuf = av_mallocz(sizeof(*vkbuf));
    if (!vkbuf)
        return AVERROR(ENOMEM);

    vkbuf->mapped_mem = !!imp_size;

    if (!vkbuf->mapped_mem) {
        *stride = FFALIGN(*stride, p->props.properties.limits.optimalBufferCopyRowPitchAlignment);
        buf_spawn.size = height*(*stride);
        buf_spawn.size = FFALIGN(buf_spawn.size, p->props.properties.limits.minMemoryMapAlignment);
    } else {
        buf_spawn.size = imp_size;
    }

    ret = vkCreateBuffer(hwctx->act_dev, &buf_spawn, NULL, &vkbuf->buf);
    if (ret != VK_SUCCESS) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create buffer: %s\n",
               vk_ret2str(ret));
        return AVERROR_EXTERNAL;
    }

    req_desc.buffer = vkbuf->buf;

    vkGetBufferMemoryRequirements2(hwctx->act_dev, &req_desc, &req);

    /* In case the implementation prefers/requires dedicated allocation */
    use_ded_mem = ded_req.prefersDedicatedAllocation |
                  ded_req.requiresDedicatedAllocation;
    if (use_ded_mem)
        ded_alloc.buffer = vkbuf->buf;

    err = alloc_mem(ctx, &req.memoryRequirements, flags,
                    use_ded_mem ? &ded_alloc : (void *)ded_alloc.pNext,
                    &vkbuf->flags, &vkbuf->mem);
    if (err)
        return err;

    ret = vkBindBufferMemory(hwctx->act_dev, vkbuf->buf, vkbuf->mem, 0);
    if (ret != VK_SUCCESS) {
        av_log(ctx, AV_LOG_ERROR, "Failed to bind memory to buffer: %s\n",
               vk_ret2str(ret));
        free_buf(ctx, (uint8_t *)vkbuf);
        return AVERROR_EXTERNAL;
    }

    *buf = av_buffer_create((uint8_t *)vkbuf, sizeof(*vkbuf), free_buf, ctx, 0);
    if (!(*buf)) {
        free_buf(ctx, (uint8_t *)vkbuf);
        return AVERROR(ENOMEM);
    }

    return 0;
}

/* Skips mapping of host mapped buffers but still invalidates them */
static int map_buffers(AVHWDeviceContext *ctx, AVBufferRef **bufs, uint8_t *mem[],
                       int nb_buffers, int invalidate)
{
    VkResult ret;
    AVVulkanDeviceContext *hwctx = ctx->hwctx;
    VkMappedMemoryRange invalidate_ctx[AV_NUM_DATA_POINTERS];
    int invalidate_count = 0;

    for (int i = 0; i < nb_buffers; i++) {
        ImageBuffer *vkbuf = (ImageBuffer *)bufs[i]->data;
        if (vkbuf->mapped_mem)
            continue;

        ret = vkMapMemory(hwctx->act_dev, vkbuf->mem, 0,
                          VK_WHOLE_SIZE, 0, (void **)&mem[i]);
        if (ret != VK_SUCCESS) {
            av_log(ctx, AV_LOG_ERROR, "Failed to map buffer memory: %s\n",
                   vk_ret2str(ret));
            return AVERROR_EXTERNAL;
        }
    }

    if (!invalidate)
        return 0;

    for (int i = 0; i < nb_buffers; i++) {
        ImageBuffer *vkbuf = (ImageBuffer *)bufs[i]->data;
        const VkMappedMemoryRange ival_buf = {
            .sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .memory = vkbuf->mem,
            .size   = VK_WHOLE_SIZE,
        };
        if (vkbuf->flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
            continue;
        invalidate_ctx[invalidate_count++] = ival_buf;
    }

    if (invalidate_count) {
        ret = vkInvalidateMappedMemoryRanges(hwctx->act_dev, invalidate_count,
                                             invalidate_ctx);
        if (ret != VK_SUCCESS)
            av_log(ctx, AV_LOG_WARNING, "Failed to invalidate memory: %s\n",
                   vk_ret2str(ret));
    }

    return 0;
}

static int unmap_buffers(AVHWDeviceContext *ctx, AVBufferRef **bufs,
                         int nb_buffers, int flush)
{
    int err = 0;
    VkResult ret;
    AVVulkanDeviceContext *hwctx = ctx->hwctx;
    VkMappedMemoryRange flush_ctx[AV_NUM_DATA_POINTERS];
    int flush_count = 0;

    if (flush) {
        for (int i = 0; i < nb_buffers; i++) {
            ImageBuffer *vkbuf = (ImageBuffer *)bufs[i]->data;
            const VkMappedMemoryRange flush_buf = {
                .sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                .memory = vkbuf->mem,
                .size   = VK_WHOLE_SIZE,
            };
            if (vkbuf->flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
                continue;
            flush_ctx[flush_count++] = flush_buf;
        }
    }

    if (flush_count) {
        ret = vkFlushMappedMemoryRanges(hwctx->act_dev, flush_count, flush_ctx);
        if (ret != VK_SUCCESS) {
            av_log(ctx, AV_LOG_ERROR, "Failed to flush memory: %s\n",
                    vk_ret2str(ret));
            err = AVERROR_EXTERNAL; /* We still want to try to unmap them */
        }
    }

    for (int i = 0; i < nb_buffers; i++) {
        ImageBuffer *vkbuf = (ImageBuffer *)bufs[i]->data;
        if (vkbuf->mapped_mem)
            continue;

        vkUnmapMemory(hwctx->act_dev, vkbuf->mem);
    }

    return err;
}

static int transfer_image_buf(AVHWFramesContext *hwfc, const AVFrame *f,
                              AVBufferRef **bufs, const int *buf_stride, int w,
                              int h, enum AVPixelFormat pix_fmt, int to_buf)
{
    int err;
    AVVkFrame *frame = (AVVkFrame *)f->data[0];
    VulkanFramesPriv *fp = hwfc->internal->priv;

    int bar_num = 0;
    VkPipelineStageFlagBits sem_wait_dst[AV_NUM_DATA_POINTERS];

    const int planes = av_pix_fmt_count_planes(pix_fmt);
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);

    VkImageMemoryBarrier img_bar[AV_NUM_DATA_POINTERS] = { 0 };
    VulkanExecCtx *ectx = to_buf ? &fp->download_ctx : &fp->upload_ctx;
    VkCommandBuffer cmd_buf = get_buf_exec_ctx(hwfc, ectx);

    VkSubmitInfo s_info = {
        .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pSignalSemaphores    = frame->sem,
        .pWaitSemaphores      = frame->sem,
        .pWaitDstStageMask    = sem_wait_dst,
        .signalSemaphoreCount = planes,
        .waitSemaphoreCount   = planes,
    };

    if ((err = wait_start_exec_ctx(hwfc, ectx)))
        return err;

    /* Change the image layout to something more optimal for transfers */
    for (int i = 0; i < planes; i++) {
        VkImageLayout new_layout = to_buf ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL :
                                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        VkAccessFlags new_access = to_buf ? VK_ACCESS_TRANSFER_READ_BIT :
                                            VK_ACCESS_TRANSFER_WRITE_BIT;

        sem_wait_dst[i] = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

        /* If the layout matches and we have read access skip the barrier */
        if ((frame->layout[i] == new_layout) && (frame->access[i] & new_access))
            continue;

        img_bar[bar_num].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        img_bar[bar_num].srcAccessMask = 0x0;
        img_bar[bar_num].dstAccessMask = new_access;
        img_bar[bar_num].oldLayout = frame->layout[i];
        img_bar[bar_num].newLayout = new_layout;
        img_bar[bar_num].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        img_bar[bar_num].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        img_bar[bar_num].image = frame->img[i];
        img_bar[bar_num].subresourceRange.levelCount = 1;
        img_bar[bar_num].subresourceRange.layerCount = 1;
        img_bar[bar_num].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

        frame->layout[i] = img_bar[bar_num].newLayout;
        frame->access[i] = img_bar[bar_num].dstAccessMask;

        bar_num++;
    }

    if (bar_num)
        vkCmdPipelineBarrier(cmd_buf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                             0, NULL, 0, NULL, bar_num, img_bar);

    /* Schedule a copy for each plane */
    for (int i = 0; i < planes; i++) {
        ImageBuffer *vkbuf = (ImageBuffer *)bufs[i]->data;
        const int p_w = i > 0 ? AV_CEIL_RSHIFT(w, desc->log2_chroma_w) : w;
        const int p_h = i > 0 ? AV_CEIL_RSHIFT(h, desc->log2_chroma_h) : h;
        VkBufferImageCopy buf_reg = {
            .bufferOffset = 0,
            /* Buffer stride isn't in bytes, it's in samples, the implementation
             * uses the image's VkFormat to know how many bytes per sample
             * the buffer has. So we have to convert by dividing. Stupid.
             * Won't work with YUVA or other planar formats with alpha. */
            .bufferRowLength = buf_stride[i] / desc->comp[i].step,
            .bufferImageHeight = p_h,
            .imageSubresource.layerCount = 1,
            .imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .imageOffset = { 0, 0, 0, },
            .imageExtent = { p_w, p_h, 1, },
        };

        if (to_buf)
            vkCmdCopyImageToBuffer(cmd_buf, frame->img[i], frame->layout[i],
                                   vkbuf->buf, 1, &buf_reg);
        else
            vkCmdCopyBufferToImage(cmd_buf, vkbuf->buf, frame->img[i],
                                   frame->layout[i], 1, &buf_reg);
    }

    /* When uploading, do this asynchronously if the source is refcounted by
     * keeping the buffers as a submission dependency.
     * The hwcontext is guaranteed to not be freed until all frames are freed
     * in the frames_unint function.
     * When downloading to buffer, do this synchronously and wait for the
     * queue submission to finish executing */
    if (!to_buf) {
        int ref;
        for (ref = 0; ref < AV_NUM_DATA_POINTERS; ref++) {
            if (!f->buf[ref])
                break;
            if ((err = add_buf_dep_exec_ctx(hwfc, ectx, &f->buf[ref], 1)))
                return err;
        }
        if (ref && (err = add_buf_dep_exec_ctx(hwfc, ectx, bufs, planes)))
            return err;
        return submit_exec_ctx(hwfc, ectx, &s_info, !ref);
    } else {
        return submit_exec_ctx(hwfc, ectx, &s_info,    1);
    }
}

static int vulkan_transfer_data_from_mem(AVHWFramesContext *hwfc, AVFrame *dst,
                                         const AVFrame *src)
{
    int err = 0;
    AVFrame tmp;
    AVVkFrame *f = (AVVkFrame *)dst->data[0];
    AVHWDeviceContext *dev_ctx = hwfc->device_ctx;
    AVBufferRef *bufs[AV_NUM_DATA_POINTERS] = { 0 };
    const int planes = av_pix_fmt_count_planes(src->format);
    int log2_chroma = av_pix_fmt_desc_get(src->format)->log2_chroma_h;
    VulkanDevicePriv *p = hwfc->device_ctx->internal->priv;
    int host_mapped[AV_NUM_DATA_POINTERS] = { 0 };
    int map_host = p->extensions & EXT_EXTERNAL_HOST_MEMORY;

    if ((src->format != AV_PIX_FMT_NONE && !av_vkfmt_from_pixfmt(src->format))) {
        av_log(hwfc, AV_LOG_ERROR, "Unsupported source pixel format!\n");
        return AVERROR(EINVAL);
    }

    if (src->width > hwfc->width || src->height > hwfc->height)
        return AVERROR(EINVAL);

    /* For linear, host visiable images */
    if (f->tiling == VK_IMAGE_TILING_LINEAR &&
        f->flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        AVFrame *map = av_frame_alloc();
        if (!map)
            return AVERROR(ENOMEM);
        map->format = src->format;

        err = vulkan_map_frame_to_mem(hwfc, map, dst, AV_HWFRAME_MAP_WRITE);
        if (err)
            return err;

        err = av_frame_copy(map, src);
        av_frame_free(&map);
        return err;
    }

    /* Create buffers */
    for (int i = 0; i < planes; i++) {
        int h = src->height;
        int p_height = i > 0 ? AV_CEIL_RSHIFT(h, log2_chroma) : h;
        size_t p_size = FFABS(src->linesize[i]) * p_height;

        VkImportMemoryHostPointerInfoEXT import_desc = {
            .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT,
            .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,
            .pHostPointer = src->data[i],
        };

        /* We can only map images with positive stride and alignment appropriate
         * for the device. */
        host_mapped[i] = map_host && src->linesize[i] > 0 &&
                         !(p_size % p->hprops.minImportedHostPointerAlignment) &&
                         !(((uintptr_t)import_desc.pHostPointer) %
                           p->hprops.minImportedHostPointerAlignment);
        p_size = host_mapped[i] ? p_size : 0;

        tmp.linesize[i] = FFABS(src->linesize[i]);
        err = create_buf(dev_ctx, &bufs[i], p_size, p_height, &tmp.linesize[i],
                         VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, NULL,
                         host_mapped[i] ? &import_desc : NULL);
        if (err)
            goto end;
    }

    /* Map, copy image to buffer, unmap */
    if ((err = map_buffers(dev_ctx, bufs, tmp.data, planes, 0)))
        goto end;

    for (int i = 0; i < planes; i++) {
        int h = src->height;
        int p_height = i > 0 ? AV_CEIL_RSHIFT(h, log2_chroma) : h;

        if (host_mapped[i])
            continue;

        av_image_copy_plane(tmp.data[i], tmp.linesize[i],
                            (const uint8_t *)src->data[i], src->linesize[i],
                            FFMIN(tmp.linesize[i], FFABS(src->linesize[i])),
                            p_height);
    }

    if ((err = unmap_buffers(dev_ctx, bufs, planes, 1)))
        goto end;

    /* Copy buffers to image */
    err = transfer_image_buf(hwfc, dst, bufs, tmp.linesize,
                             src->width, src->height, src->format, 0);

end:
    for (int i = 0; i < planes; i++)
        av_buffer_unref(&bufs[i]);

    return err;
}

static int vulkan_transfer_data_to(AVHWFramesContext *hwfc, AVFrame *dst,
                                        const AVFrame *src)
{
    av_unused VulkanDevicePriv *p = hwfc->device_ctx->internal->priv;

    switch (src->format) {
#if CONFIG_CUDA
    case AV_PIX_FMT_CUDA:
        if ((p->extensions & EXT_EXTERNAL_FD_MEMORY) &&
            (p->extensions & EXT_EXTERNAL_FD_SEM))
            return vulkan_transfer_data_from_cuda(hwfc, dst, src);
#endif
    default:
        if (src->hw_frames_ctx)
            return AVERROR(ENOSYS);
        else
            return vulkan_transfer_data_from_mem(hwfc, dst, src);
    }
}

#if CONFIG_CUDA
static int vulkan_transfer_data_to_cuda(AVHWFramesContext *hwfc, AVFrame *dst,
                                      const AVFrame *src)
{
    int err;
    VkResult ret;
    CUcontext dummy;
    AVVkFrame *dst_f;
    AVVkFrameInternal *dst_int;
    const int planes = av_pix_fmt_count_planes(hwfc->sw_format);
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(hwfc->sw_format);

    AVHWFramesContext *cuda_fc = (AVHWFramesContext*)dst->hw_frames_ctx->data;
    AVHWDeviceContext *cuda_cu = cuda_fc->device_ctx;
    AVCUDADeviceContext *cuda_dev = cuda_cu->hwctx;
    AVCUDADeviceContextInternal *cu_internal = cuda_dev->internal;
    CudaFunctions *cu = cu_internal->cuda_dl;

    ret = CHECK_CU(cu->cuCtxPushCurrent(cuda_dev->cuda_ctx));
    if (ret < 0) {
        err = AVERROR_EXTERNAL;
        goto fail;
    }

    dst_f = (AVVkFrame *)src->data[0];

    err = vulkan_export_to_cuda(hwfc, dst->hw_frames_ctx, src);
    if (err < 0) {
        goto fail;
    }

    dst_int = dst_f->internal;

    for (int i = 0; i < planes; i++) {
        CUDA_MEMCPY2D cpy = {
            .dstMemoryType = CU_MEMORYTYPE_DEVICE,
            .dstDevice     = (CUdeviceptr)dst->data[i],
            .dstPitch      = dst->linesize[i],
            .dstY          = 0,

            .srcMemoryType = CU_MEMORYTYPE_ARRAY,
            .srcArray      = dst_int->cu_array[i],
            .WidthInBytes  = (i > 0 ? AV_CEIL_RSHIFT(hwfc->width, desc->log2_chroma_w)
                                    : hwfc->width) * desc->comp[i].step,
            .Height        = i > 0 ? AV_CEIL_RSHIFT(hwfc->height, desc->log2_chroma_h)
                                   : hwfc->height,
        };

        ret = CHECK_CU(cu->cuMemcpy2DAsync(&cpy, cuda_dev->stream));
        if (ret < 0) {
            err = AVERROR_EXTERNAL;
            goto fail;
        }
    }

    CHECK_CU(cu->cuCtxPopCurrent(&dummy));

    av_log(hwfc, AV_LOG_VERBOSE, "Transfered Vulkan image to CUDA!\n");

    return 0;

fail:
    CHECK_CU(cu->cuCtxPopCurrent(&dummy));
    vulkan_free_internal(dst_int);
    dst_f->internal = NULL;
    av_buffer_unref(&dst->buf[0]);
    return err;
}
#endif

static int vulkan_transfer_data_to_mem(AVHWFramesContext *hwfc, AVFrame *dst,
                                       const AVFrame *src)
{
    int err = 0;
    AVFrame tmp;
    AVVkFrame *f = (AVVkFrame *)src->data[0];
    AVHWDeviceContext *dev_ctx = hwfc->device_ctx;
    AVBufferRef *bufs[AV_NUM_DATA_POINTERS] = { 0 };
    const int planes = av_pix_fmt_count_planes(dst->format);
    int log2_chroma = av_pix_fmt_desc_get(dst->format)->log2_chroma_h;
    VulkanDevicePriv *p = hwfc->device_ctx->internal->priv;
    int host_mapped[AV_NUM_DATA_POINTERS] = { 0 };
    int map_host = p->extensions & EXT_EXTERNAL_HOST_MEMORY;

    if (dst->width > hwfc->width || dst->height > hwfc->height)
        return AVERROR(EINVAL);

    /* For linear, host visiable images */
    if (f->tiling == VK_IMAGE_TILING_LINEAR &&
        f->flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        AVFrame *map = av_frame_alloc();
        if (!map)
            return AVERROR(ENOMEM);
        map->format = dst->format;

        err = vulkan_map_frame_to_mem(hwfc, map, src, AV_HWFRAME_MAP_READ);
        if (err)
            return err;

        err = av_frame_copy(dst, map);
        av_frame_free(&map);
        return err;
    }

    /* Create buffers */
    for (int i = 0; i < planes; i++) {
        int h = dst->height;
        int p_height = i > 0 ? AV_CEIL_RSHIFT(h, log2_chroma) : h;
        size_t p_size = FFABS(dst->linesize[i]) * p_height;

        VkImportMemoryHostPointerInfoEXT import_desc = {
            .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT,
            .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,
            .pHostPointer = dst->data[i],
        };

        /* We can only map images with positive stride and alignment appropriate
         * for the device. */
        host_mapped[i] = map_host && dst->linesize[i] > 0 &&
                         !(p_size % p->hprops.minImportedHostPointerAlignment) &&
                         !(((uintptr_t)import_desc.pHostPointer) %
                           p->hprops.minImportedHostPointerAlignment);
        p_size = host_mapped[i] ? p_size : 0;

        tmp.linesize[i] = FFABS(dst->linesize[i]);
        err = create_buf(dev_ctx, &bufs[i], p_size, p_height,
                         &tmp.linesize[i], VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, NULL,
                         host_mapped[i] ? &import_desc : NULL);
        if (err)
            goto end;
    }

    /* Copy image to buffer */
    if ((err = transfer_image_buf(hwfc, src, bufs, tmp.linesize,
                                  dst->width, dst->height, dst->format, 1)))
        goto end;

    /* Map, copy buffer to frame, unmap */
    if ((err = map_buffers(dev_ctx, bufs, tmp.data, planes, 1)))
        goto end;

    for (int i = 0; i < planes; i++) {
        int h = dst->height;
        int p_height = i > 0 ? AV_CEIL_RSHIFT(h, log2_chroma) : h;

        if (host_mapped[i])
            continue;

        av_image_copy_plane(dst->data[i], dst->linesize[i],
                            (const uint8_t *)tmp.data[i], tmp.linesize[i],
                            FFMIN(tmp.linesize[i], FFABS(dst->linesize[i])),
                            p_height);
    }

    err = unmap_buffers(dev_ctx, bufs, planes, 0);

end:
    for (int i = 0; i < planes; i++)
        av_buffer_unref(&bufs[i]);

    return err;
}

static int vulkan_transfer_data_from(AVHWFramesContext *hwfc, AVFrame *dst,
                                     const AVFrame *src)
{
    av_unused VulkanDevicePriv *p = hwfc->device_ctx->internal->priv;

    switch (dst->format) {
#if CONFIG_CUDA
    case AV_PIX_FMT_CUDA:
        if ((p->extensions & EXT_EXTERNAL_FD_MEMORY) &&
            (p->extensions & EXT_EXTERNAL_FD_SEM))
            return vulkan_transfer_data_to_cuda(hwfc, dst, src);
#endif
    default:
        if (dst->hw_frames_ctx)
            return AVERROR(ENOSYS);
        else
            return vulkan_transfer_data_to_mem(hwfc, dst, src);
    }
}

static int vulkan_frames_derive_to(AVHWFramesContext *dst_fc,
                                   AVHWFramesContext *src_fc, int flags)
{
    return vulkan_frames_init(dst_fc);
}

AVVkFrame *av_vk_frame_alloc(void)
{
    return av_mallocz(sizeof(AVVkFrame));
}

const HWContextType ff_hwcontext_type_vulkan = {
    .type                   = AV_HWDEVICE_TYPE_VULKAN,
    .name                   = "Vulkan",

    .device_hwctx_size      = sizeof(AVVulkanDeviceContext),
    .device_priv_size       = sizeof(VulkanDevicePriv),
    .frames_hwctx_size      = sizeof(AVVulkanFramesContext),
    .frames_priv_size       = sizeof(VulkanFramesPriv),

    .device_init            = &vulkan_device_init,
    .device_create          = &vulkan_device_create,
    .device_derive          = &vulkan_device_derive,

    .frames_get_constraints = &vulkan_frames_get_constraints,
    .frames_init            = vulkan_frames_init,
    .frames_get_buffer      = vulkan_get_buffer,
    .frames_uninit          = vulkan_frames_uninit,

    .transfer_get_formats   = vulkan_transfer_get_formats,
    .transfer_data_to       = vulkan_transfer_data_to,
    .transfer_data_from     = vulkan_transfer_data_from,

    .map_to                 = vulkan_map_to,
    .map_from               = vulkan_map_from,
    .frames_derive_to       = &vulkan_frames_derive_to,

    .pix_fmts = (const enum AVPixelFormat []) {
        AV_PIX_FMT_VULKAN,
        AV_PIX_FMT_NONE
    },
};
