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

#define VK_NO_PROTOTYPES
#define VK_ENABLE_BETA_EXTENSIONS

#include "config.h"
#include "ffplay_renderer.h"

#if (SDL_VERSION_ATLEAST(2, 0, 6) && CONFIG_LIBPLACEBO)
/* Get PL_API_VER */
#include <libplacebo/config.h>
#define HAVE_VULKAN_RENDERER (PL_API_VER >= 278)
#else
#define HAVE_VULKAN_RENDERER 0
#endif

#if HAVE_VULKAN_RENDERER

#if defined(_WIN32) && !defined(VK_USE_PLATFORM_WIN32_KHR)
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#include <libplacebo/vulkan.h>
#include <libplacebo/utils/frame_queue.h>
#include <libplacebo/utils/libav.h>
#include <SDL_vulkan.h>

#include "libavutil/bprint.h"
#include "libavutil/mem.h"

#endif

struct VkRenderer {
    const AVClass *class;

    int (*create)(VkRenderer *renderer, SDL_Window *window, AVDictionary *dict);

    int (*get_hw_dev)(VkRenderer *renderer, AVBufferRef **dev);

    int (*display)(VkRenderer *renderer, AVFrame *frame);

    int (*resize)(VkRenderer *renderer, int width, int height);

    void (*destroy)(VkRenderer *renderer);
};

#if HAVE_VULKAN_RENDERER

typedef struct RendererContext {
    VkRenderer api;

    // Can be NULL when vulkan instance is created by avutil
    pl_vk_inst placebo_instance;
    pl_vulkan placebo_vulkan;
    pl_swapchain swapchain;
    VkSurfaceKHR vk_surface;
    pl_renderer renderer;
    pl_tex tex[4];

    pl_log vk_log;

    AVBufferRef *hw_device_ref;
    AVBufferRef *hw_frame_ref;
    enum AVPixelFormat *transfer_formats;
    AVHWFramesConstraints *constraints;

    PFN_vkGetInstanceProcAddr get_proc_addr;
    // This field is a copy from pl_vk_inst->instance or hw_device_ref instance.
    VkInstance inst;

    AVFrame *vk_frame;
} RendererContext;

static void vk_log_cb(void *log_priv, enum pl_log_level level,
                      const char *msg)
{
    static const int level_map[] = {
            AV_LOG_QUIET,
            AV_LOG_FATAL,
            AV_LOG_ERROR,
            AV_LOG_WARNING,
            AV_LOG_INFO,
            AV_LOG_DEBUG,
            AV_LOG_TRACE,
    };

    if (level > 0 && level < FF_ARRAY_ELEMS(level_map))
        av_log(log_priv, level_map[level], "%s\n", msg);
}

// Should keep sync with optional_device_exts inside hwcontext_vulkan.c
static const char *optional_device_exts[] = {
    /* Misc or required by other extensions */
    VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME,
    VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
    VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME,
    VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME,
    VK_EXT_PHYSICAL_DEVICE_DRM_EXTENSION_NAME,
    VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME,
    VK_KHR_COOPERATIVE_MATRIX_EXTENSION_NAME,

    /* Imports/exports */
    VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
    VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,
    VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME,
    VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
    VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME,
#ifdef _WIN32
    VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,
    VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME,
#endif

    /* Video encoding/decoding */
    VK_KHR_VIDEO_QUEUE_EXTENSION_NAME,
    VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME,
    VK_KHR_VIDEO_DECODE_H264_EXTENSION_NAME,
    VK_KHR_VIDEO_DECODE_H265_EXTENSION_NAME,
    "VK_MESA_video_decode_av1",
};

static inline int enable_debug(const AVDictionary *opt)
{
    AVDictionaryEntry *entry = av_dict_get(opt, "debug", NULL, 0);
    int debug = entry && strtol(entry->value, NULL, 10);
    return debug;
}

static void hwctx_lock_queue(void *priv, uint32_t qf, uint32_t qidx)
{
    AVHWDeviceContext *avhwctx = priv;
    const AVVulkanDeviceContext *hwctx = avhwctx->hwctx;
    hwctx->lock_queue(avhwctx, qf, qidx);
}

static void hwctx_unlock_queue(void *priv, uint32_t qf, uint32_t qidx)
{
    AVHWDeviceContext *avhwctx = priv;
    const AVVulkanDeviceContext *hwctx = avhwctx->hwctx;
    hwctx->unlock_queue(avhwctx, qf, qidx);
}

static int add_instance_extension(const char **ext, unsigned num_ext,
                                  const AVDictionary *opt,
                                  AVDictionary **dict)
{
    const char *inst_ext_key = "instance_extensions";
    AVDictionaryEntry *entry;
    AVBPrint buf;
    char *ext_list = NULL;
    int ret;

    av_bprint_init(&buf, 0, AV_BPRINT_SIZE_AUTOMATIC);
    for (int i = 0; i < num_ext; i++) {
        if (i)
            av_bprintf(&buf, "+%s", ext[i]);
        else
            av_bprintf(&buf, "%s", ext[i]);
    }

    entry = av_dict_get(opt, inst_ext_key, NULL, 0);
    if (entry && entry->value && entry->value[0]) {
        if (num_ext)
            av_bprintf(&buf, "+");
        av_bprintf(&buf, "%s", entry->value);
    }

    ret = av_bprint_finalize(&buf, &ext_list);
    if (ret < 0)
        return ret;
    return av_dict_set(dict, inst_ext_key, ext_list, AV_DICT_DONT_STRDUP_VAL);
}

static int add_device_extension(const AVDictionary *opt,
                                AVDictionary **dict)
{
    const char *dev_ext_key = "device_extensions";
    AVDictionaryEntry *entry;
    AVBPrint buf;
    char *ext_list = NULL;
    int ret;

    av_bprint_init(&buf, 0, AV_BPRINT_SIZE_AUTOMATIC);
    av_bprintf(&buf, "%s", VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    for (int i = 0; i < pl_vulkan_num_recommended_extensions; i++)
        av_bprintf(&buf, "+%s", pl_vulkan_recommended_extensions[i]);

    entry = av_dict_get(opt, dev_ext_key, NULL, 0);
    if (entry && entry->value && entry->value[0])
        av_bprintf(&buf, "+%s", entry->value);

    ret = av_bprint_finalize(&buf, &ext_list);
    if (ret < 0)
        return ret;
    return av_dict_set(dict, dev_ext_key, ext_list, AV_DICT_DONT_STRDUP_VAL);
}

static const char *select_device(const AVDictionary *opt)
{
    const AVDictionaryEntry *entry;

    entry = av_dict_get(opt, "device", NULL, 0);
    if (entry)
        return entry->value;
    return NULL;
}

static int create_vk_by_hwcontext(VkRenderer *renderer,
                                  const char **ext, unsigned num_ext,
                                  const AVDictionary *opt)
{
    RendererContext *ctx = (RendererContext *) renderer;
    AVHWDeviceContext *dev;
    AVVulkanDeviceContext *hwctx;
    AVDictionary *dict = NULL;
    int ret;

    ret = add_instance_extension(ext, num_ext, opt, &dict);
    if (ret < 0)
        return ret;
    ret = add_device_extension(opt, &dict);
    if (ret) {
        av_dict_free(&dict);
        return ret;
    }

    ret = av_hwdevice_ctx_create(&ctx->hw_device_ref, AV_HWDEVICE_TYPE_VULKAN,
                                 select_device(opt), dict, 0);
    av_dict_free(&dict);
    if (ret < 0)
        return ret;

    dev = (AVHWDeviceContext *) ctx->hw_device_ref->data;
    hwctx = dev->hwctx;

    // There is no way to pass SDL GetInstanceProcAddr to hwdevice.
    // Check the result and return error if they don't match.
    if (hwctx->get_proc_addr != SDL_Vulkan_GetVkGetInstanceProcAddr()) {
        av_log(renderer, AV_LOG_ERROR,
               "hwdevice and SDL use different get_proc_addr. "
               "Try -vulkan_params create_by_placebo=1\n");
        return AVERROR_PATCHWELCOME;
    }

    ctx->get_proc_addr = hwctx->get_proc_addr;
    ctx->inst = hwctx->inst;
    ctx->placebo_vulkan = pl_vulkan_import(ctx->vk_log,
        pl_vulkan_import_params(
            .instance = hwctx->inst,
            .get_proc_addr = hwctx->get_proc_addr,
            .phys_device = hwctx->phys_dev,
            .device         = hwctx->act_dev,
            .extensions     = hwctx->enabled_dev_extensions,
            .num_extensions = hwctx->nb_enabled_dev_extensions,
            .features       = &hwctx->device_features,
            .lock_queue     = hwctx_lock_queue,
            .unlock_queue   = hwctx_unlock_queue,
            .queue_ctx      = dev,
            .queue_graphics = {
                .index = hwctx->queue_family_index,
                .count = hwctx->nb_graphics_queues,
            },
            .queue_compute = {
                .index = hwctx->queue_family_comp_index,
                .count = hwctx->nb_comp_queues,
            },
            .queue_transfer = {
                .index = hwctx->queue_family_tx_index,
                .count = hwctx->nb_tx_queues,
            },
        ));
    if (!ctx->placebo_vulkan)
        return AVERROR_EXTERNAL;

    return 0;
}

static void placebo_lock_queue(struct AVHWDeviceContext *dev_ctx,
                       uint32_t queue_family, uint32_t index)
{
    RendererContext *ctx = dev_ctx->user_opaque;
    pl_vulkan vk = ctx->placebo_vulkan;
    vk->lock_queue(vk, queue_family, index);
}

static void placebo_unlock_queue(struct AVHWDeviceContext *dev_ctx,
                         uint32_t queue_family,
                         uint32_t index)
{
    RendererContext *ctx = dev_ctx->user_opaque;
    pl_vulkan vk = ctx->placebo_vulkan;
    vk->unlock_queue(vk, queue_family, index);
}

static int get_decode_queue(VkRenderer *renderer, int *index, int *count)
{
    RendererContext *ctx = (RendererContext *) renderer;
    VkQueueFamilyProperties *queue_family_prop = NULL;
    uint32_t num_queue_family_prop = 0;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties get_queue_family_prop;
    PFN_vkGetInstanceProcAddr get_proc_addr = ctx->get_proc_addr;

    *index = -1;
    *count = 0;
    get_queue_family_prop = (PFN_vkGetPhysicalDeviceQueueFamilyProperties)
            get_proc_addr(ctx->placebo_instance->instance,
                          "vkGetPhysicalDeviceQueueFamilyProperties");
    get_queue_family_prop(ctx->placebo_vulkan->phys_device,
                          &num_queue_family_prop, NULL);
    if (!num_queue_family_prop)
        return AVERROR_EXTERNAL;

    queue_family_prop = av_calloc(num_queue_family_prop,
                                  sizeof(*queue_family_prop));
    if (!queue_family_prop)
        return AVERROR(ENOMEM);

    get_queue_family_prop(ctx->placebo_vulkan->phys_device,
                          &num_queue_family_prop,
                          queue_family_prop);

    for (int i = 0; i < num_queue_family_prop; i++) {
        if (queue_family_prop[i].queueFlags & VK_QUEUE_VIDEO_DECODE_BIT_KHR) {
            *index = i;
            *count = queue_family_prop[i].queueCount;
            break;
        }
    }
    av_free(queue_family_prop);

    return 0;
}

static int create_vk_by_placebo(VkRenderer *renderer,
                                const char **ext, unsigned num_ext,
                                const AVDictionary *opt)
{
    RendererContext *ctx = (RendererContext *) renderer;
    AVHWDeviceContext *device_ctx;
    AVVulkanDeviceContext *vk_dev_ctx;
    int decode_index;
    int decode_count;
    int ret;

    ctx->get_proc_addr = SDL_Vulkan_GetVkGetInstanceProcAddr();

    ctx->placebo_instance = pl_vk_inst_create(ctx->vk_log, pl_vk_inst_params(
            .get_proc_addr = ctx->get_proc_addr,
            .debug = enable_debug(opt),
            .extensions = ext,
            .num_extensions = num_ext
    ));
    if (!ctx->placebo_instance) {
        return AVERROR_EXTERNAL;
    }
    ctx->inst = ctx->placebo_instance->instance;

    ctx->placebo_vulkan = pl_vulkan_create(ctx->vk_log, pl_vulkan_params(
            .instance = ctx->placebo_instance->instance,
            .get_proc_addr = ctx->placebo_instance->get_proc_addr,
            .surface = ctx->vk_surface,
            .allow_software = false,
            .opt_extensions = optional_device_exts,
            .num_opt_extensions = FF_ARRAY_ELEMS(optional_device_exts),
            .extra_queues = VK_QUEUE_VIDEO_DECODE_BIT_KHR,
            .device_name = select_device(opt),
    ));
    if (!ctx->placebo_vulkan)
        return AVERROR_EXTERNAL;
    ctx->hw_device_ref = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_VULKAN);
    if (!ctx->hw_device_ref) {
        return AVERROR(ENOMEM);
    }

    device_ctx = (AVHWDeviceContext *) ctx->hw_device_ref->data;
    device_ctx->user_opaque = ctx;

    vk_dev_ctx = device_ctx->hwctx;
    vk_dev_ctx->lock_queue = placebo_lock_queue,
            vk_dev_ctx->unlock_queue = placebo_unlock_queue;

    vk_dev_ctx->get_proc_addr = ctx->placebo_instance->get_proc_addr;

    vk_dev_ctx->inst = ctx->placebo_instance->instance;
    vk_dev_ctx->phys_dev = ctx->placebo_vulkan->phys_device;
    vk_dev_ctx->act_dev = ctx->placebo_vulkan->device;

    vk_dev_ctx->device_features = *ctx->placebo_vulkan->features;

    vk_dev_ctx->enabled_inst_extensions = ctx->placebo_instance->extensions;
    vk_dev_ctx->nb_enabled_inst_extensions = ctx->placebo_instance->num_extensions;

    vk_dev_ctx->enabled_dev_extensions = ctx->placebo_vulkan->extensions;
    vk_dev_ctx->nb_enabled_dev_extensions = ctx->placebo_vulkan->num_extensions;

    vk_dev_ctx->queue_family_index = ctx->placebo_vulkan->queue_graphics.index;
    vk_dev_ctx->nb_graphics_queues = ctx->placebo_vulkan->queue_graphics.count;

    vk_dev_ctx->queue_family_tx_index = ctx->placebo_vulkan->queue_transfer.index;
    vk_dev_ctx->nb_tx_queues = ctx->placebo_vulkan->queue_transfer.count;

    vk_dev_ctx->queue_family_comp_index = ctx->placebo_vulkan->queue_compute.index;
    vk_dev_ctx->nb_comp_queues = ctx->placebo_vulkan->queue_compute.count;

    ret = get_decode_queue(renderer, &decode_index, &decode_count);
    if (ret < 0)
        return ret;

    vk_dev_ctx->queue_family_decode_index = decode_index;
    vk_dev_ctx->nb_decode_queues = decode_count;

    ret = av_hwdevice_ctx_init(ctx->hw_device_ref);
    if (ret < 0)
        return ret;

    return 0;
}

static int create(VkRenderer *renderer, SDL_Window *window, AVDictionary *opt)
{
    int ret = 0;
    unsigned num_ext = 0;
    const char **ext = NULL;
    int w, h;
    struct pl_log_params vk_log_params = {
            .log_cb = vk_log_cb,
            .log_level = PL_LOG_DEBUG,
            .log_priv = renderer,
    };
    RendererContext *ctx = (RendererContext *) renderer;
    AVDictionaryEntry *entry;

    ctx->vk_log = pl_log_create(PL_API_VER, &vk_log_params);

    if (!SDL_Vulkan_GetInstanceExtensions(window, &num_ext, NULL)) {
        av_log(NULL, AV_LOG_FATAL, "Failed to get vulkan extensions: %s\n",
               SDL_GetError());
        return AVERROR_EXTERNAL;
    }

    ext = av_calloc(num_ext, sizeof(*ext));
    if (!ext) {
        ret = AVERROR(ENOMEM);
        goto out;
    }

    SDL_Vulkan_GetInstanceExtensions(window, &num_ext, ext);

    entry = av_dict_get(opt, "create_by_placebo", NULL, 0);
    if (entry && strtol(entry->value, NULL, 10))
        ret = create_vk_by_placebo(renderer, ext, num_ext, opt);
    else
        ret = create_vk_by_hwcontext(renderer, ext, num_ext, opt);
    if (ret < 0)
        goto out;

    if (!SDL_Vulkan_CreateSurface(window, ctx->inst, &ctx->vk_surface)) {
        ret = AVERROR_EXTERNAL;
        goto out;
    }

    ctx->swapchain = pl_vulkan_create_swapchain(
            ctx->placebo_vulkan,
            pl_vulkan_swapchain_params(
                    .surface = ctx->vk_surface,
                    .present_mode = VK_PRESENT_MODE_FIFO_KHR));
    if (!ctx->swapchain) {
        ret = AVERROR_EXTERNAL;
        goto out;
    }

    SDL_Vulkan_GetDrawableSize(window, &w, &h);
    pl_swapchain_resize(ctx->swapchain, &w, &h);

    ctx->renderer = pl_renderer_create(ctx->vk_log, ctx->placebo_vulkan->gpu);
    if (!ctx->renderer) {
        ret = AVERROR_EXTERNAL;
        goto out;
    }

    ctx->vk_frame = av_frame_alloc();
    if (!ctx->vk_frame) {
        ret = AVERROR(ENOMEM);
        goto out;
    }

    ret = 0;

out:
    av_free(ext);
    return ret;
}

static int get_hw_dev(VkRenderer *renderer, AVBufferRef **dev)
{
    RendererContext *ctx = (RendererContext *) renderer;

    *dev = ctx->hw_device_ref;
    return 0;
}

static int create_hw_frame(VkRenderer *renderer, AVFrame *frame)
{
    RendererContext *ctx = (RendererContext *) renderer;
    AVHWFramesContext *src_hw_frame = (AVHWFramesContext *)
            frame->hw_frames_ctx->data;
    AVHWFramesContext *hw_frame;
    AVVulkanFramesContext *vk_frame_ctx;
    int ret;

    if (ctx->hw_frame_ref) {
        hw_frame = (AVHWFramesContext *) ctx->hw_frame_ref->data;

        if (hw_frame->width == frame->width &&
            hw_frame->height == frame->height &&
            hw_frame->sw_format == src_hw_frame->sw_format)
            return 0;

        av_buffer_unref(&ctx->hw_frame_ref);
    }

    if (!ctx->constraints) {
        ctx->constraints = av_hwdevice_get_hwframe_constraints(
                ctx->hw_device_ref, NULL);
        if (!ctx->constraints)
            return AVERROR(ENOMEM);
    }

    // Check constraints and skip create hwframe. Don't take it as error since
    // we can fallback to memory copy from GPU to CPU.
    if ((ctx->constraints->max_width &&
         ctx->constraints->max_width < frame->width) ||
        (ctx->constraints->max_height &&
         ctx->constraints->max_height < frame->height) ||
        (ctx->constraints->min_width &&
         ctx->constraints->min_width > frame->width) ||
        (ctx->constraints->min_height &&
         ctx->constraints->min_height > frame->height))
        return 0;

    if (ctx->constraints->valid_sw_formats) {
        enum AVPixelFormat *sw_formats = ctx->constraints->valid_sw_formats;
        while (*sw_formats != AV_PIX_FMT_NONE) {
            if (*sw_formats == src_hw_frame->sw_format)
                break;
            sw_formats++;
        }
        if (*sw_formats == AV_PIX_FMT_NONE)
            return 0;
    }

    ctx->hw_frame_ref = av_hwframe_ctx_alloc(ctx->hw_device_ref);
    if (!ctx->hw_frame_ref)
        return AVERROR(ENOMEM);

    hw_frame = (AVHWFramesContext *) ctx->hw_frame_ref->data;
    hw_frame->format = AV_PIX_FMT_VULKAN;
    hw_frame->sw_format = src_hw_frame->sw_format;
    hw_frame->width = frame->width;
    hw_frame->height = frame->height;

    if (frame->format == AV_PIX_FMT_CUDA) {
        vk_frame_ctx = hw_frame->hwctx;
        vk_frame_ctx->flags = AV_VK_FRAME_FLAG_DISABLE_MULTIPLANE;
    }

    ret = av_hwframe_ctx_init(ctx->hw_frame_ref);
    if (ret < 0) {
        av_log(renderer, AV_LOG_ERROR, "Create hwframe context failed, %s\n",
               av_err2str(ret));
        return ret;
    }

    av_hwframe_transfer_get_formats(ctx->hw_frame_ref,
                                    AV_HWFRAME_TRANSFER_DIRECTION_TO,
                                    &ctx->transfer_formats, 0);

    return 0;
}

static inline int check_hw_transfer(RendererContext *ctx, AVFrame *frame)
{
    if (!ctx->hw_frame_ref || !ctx->transfer_formats)
        return 0;

    for (int i = 0; ctx->transfer_formats[i] != AV_PIX_FMT_NONE; i++)
        if (ctx->transfer_formats[i] == frame->format)
            return 1;

    return 0;
}

static inline int move_to_output_frame(RendererContext *ctx, AVFrame *frame)
{
    int ret = av_frame_copy_props(ctx->vk_frame, frame);
    if (ret < 0)
        return ret;
    av_frame_unref(frame);
    av_frame_move_ref(frame, ctx->vk_frame);
    return 0;
}

static int map_frame(VkRenderer *renderer, AVFrame *frame, int use_hw_frame)
{
    RendererContext *ctx = (RendererContext *) renderer;
    int ret;

    if (use_hw_frame && !ctx->hw_frame_ref)
        return AVERROR(ENOSYS);

    // Try map data first
    av_frame_unref(ctx->vk_frame);
    if (use_hw_frame) {
        ctx->vk_frame->hw_frames_ctx = av_buffer_ref(ctx->hw_frame_ref);
        ctx->vk_frame->format = AV_PIX_FMT_VULKAN;
    }
    ret = av_hwframe_map(ctx->vk_frame, frame, 0);
    if (!ret)
        return move_to_output_frame(ctx, frame);

    if (ret != AVERROR(ENOSYS))
        av_log(NULL, AV_LOG_FATAL, "Map frame failed: %s\n", av_err2str(ret));
    return ret;
}

static int transfer_frame(VkRenderer *renderer, AVFrame *frame, int use_hw_frame)
{
    RendererContext *ctx = (RendererContext *) renderer;
    int ret;

    if (use_hw_frame && !check_hw_transfer(ctx, frame))
        return AVERROR(ENOSYS);

    av_frame_unref(ctx->vk_frame);
    if (use_hw_frame)
        av_hwframe_get_buffer(ctx->hw_frame_ref, ctx->vk_frame, 0);
    ret = av_hwframe_transfer_data(ctx->vk_frame, frame, 1);
    if (!ret)
        return move_to_output_frame(ctx, frame);

    if (ret != AVERROR(ENOSYS))
        av_log(NULL, AV_LOG_FATAL, "Transfer frame failed: %s\n",
               av_err2str(ret));
    return ret;
}

static int convert_frame(VkRenderer *renderer, AVFrame *frame)
{
    int ret;

    if (!frame->hw_frames_ctx)
        return 0;

    if (frame->format == AV_PIX_FMT_VULKAN)
        return 0;

    ret = create_hw_frame(renderer, frame);
    if (ret < 0)
        return ret;

    for (int use_hw = 1; use_hw >=0; use_hw--) {
        ret = map_frame(renderer, frame, use_hw);
        if (!ret)
            return 0;
        if (ret != AVERROR(ENOSYS))
            return ret;

        ret = transfer_frame(renderer, frame, use_hw);
        if (!ret)
            return 0;
        if (ret != AVERROR(ENOSYS))
            return ret;
    }

    return ret;
}

static int display(VkRenderer *renderer, AVFrame *frame)
{
    struct pl_swapchain_frame swap_frame = {0};
    struct pl_frame pl_frame = {0};
    struct pl_frame target = {0};
    RendererContext *ctx = (RendererContext *) renderer;
    int ret = 0;
    struct pl_color_space hint = {0};

    ret = convert_frame(renderer, frame);
    if (ret < 0)
        return ret;

    if (!pl_map_avframe_ex(ctx->placebo_vulkan->gpu, &pl_frame, pl_avframe_params(
            .frame = frame,
            .tex = ctx->tex))) {
        av_log(NULL, AV_LOG_ERROR, "pl_map_avframe_ex failed\n");
        return AVERROR_EXTERNAL;
    }

    pl_color_space_from_avframe(&hint, frame);
    pl_swapchain_colorspace_hint(ctx->swapchain, &hint);
    if (!pl_swapchain_start_frame(ctx->swapchain, &swap_frame)) {
        av_log(NULL, AV_LOG_ERROR, "start frame failed\n");
        ret = AVERROR_EXTERNAL;
        goto out;
    }

    pl_frame_from_swapchain(&target, &swap_frame);
    if (!pl_render_image(ctx->renderer, &pl_frame, &target,
                         &pl_render_default_params)) {
        av_log(NULL, AV_LOG_ERROR, "pl_render_image failed\n");
        ret = AVERROR_EXTERNAL;
        goto out;
    }

    if (!pl_swapchain_submit_frame(ctx->swapchain)) {
        av_log(NULL, AV_LOG_ERROR, "pl_swapchain_submit_frame failed\n");
        ret = AVERROR_EXTERNAL;
        goto out;
    }
    pl_swapchain_swap_buffers(ctx->swapchain);

out:
    pl_unmap_avframe(ctx->placebo_vulkan->gpu, &pl_frame);
    return ret;
}

static int resize(VkRenderer *renderer, int width, int height)
{
    RendererContext *ctx = (RendererContext *) renderer;

    if (!pl_swapchain_resize(ctx->swapchain, &width, &height))
        return AVERROR_EXTERNAL;
    return 0;
}

static void destroy(VkRenderer *renderer)
{
    RendererContext *ctx = (RendererContext *) renderer;
    PFN_vkDestroySurfaceKHR vkDestroySurfaceKHR;

    av_frame_free(&ctx->vk_frame);
    av_freep(&ctx->transfer_formats);
    av_hwframe_constraints_free(&ctx->constraints);
    av_buffer_unref(&ctx->hw_frame_ref);

    if (ctx->placebo_vulkan) {
        for (int i = 0; i < FF_ARRAY_ELEMS(ctx->tex); i++)
            pl_tex_destroy(ctx->placebo_vulkan->gpu, &ctx->tex[i]);
        pl_renderer_destroy(&ctx->renderer);
        pl_swapchain_destroy(&ctx->swapchain);
        pl_vulkan_destroy(&ctx->placebo_vulkan);
    }

    if (ctx->vk_surface) {
        vkDestroySurfaceKHR = (PFN_vkDestroySurfaceKHR)
                ctx->get_proc_addr(ctx->inst, "vkDestroySurfaceKHR");
        vkDestroySurfaceKHR(ctx->inst, ctx->vk_surface, NULL);
        ctx->vk_surface = VK_NULL_HANDLE;
    }

    av_buffer_unref(&ctx->hw_device_ref);
    pl_vk_inst_destroy(&ctx->placebo_instance);

    pl_log_destroy(&ctx->vk_log);
}

static const AVClass vulkan_renderer_class = {
        .class_name = "Vulkan Renderer",
        .item_name  = av_default_item_name,
        .version    = LIBAVUTIL_VERSION_INT,
};

VkRenderer *vk_get_renderer(void)
{
    RendererContext *ctx = av_mallocz(sizeof(*ctx));
    VkRenderer *renderer;

    if (!ctx)
        return NULL;

    renderer = &ctx->api;
    renderer->class = &vulkan_renderer_class;
    renderer->get_hw_dev = get_hw_dev;
    renderer->create = create;
    renderer->display = display;
    renderer->resize = resize;
    renderer->destroy = destroy;

    return renderer;
}

#else

VkRenderer *vk_get_renderer(void)
{
    return NULL;
}

#endif

int vk_renderer_create(VkRenderer *renderer, SDL_Window *window,
                       AVDictionary *opt)
{
    return renderer->create(renderer, window, opt);
}

int vk_renderer_get_hw_dev(VkRenderer *renderer, AVBufferRef **dev)
{
    return renderer->get_hw_dev(renderer, dev);
}

int vk_renderer_display(VkRenderer *renderer, AVFrame *frame)
{
    return renderer->display(renderer, frame);
}

int vk_renderer_resize(VkRenderer *renderer, int width, int height)
{
    return renderer->resize(renderer, width, height);
}

void vk_renderer_destroy(VkRenderer *renderer)
{
    renderer->destroy(renderer);
}
