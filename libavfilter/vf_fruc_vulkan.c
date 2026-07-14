/*
 * Copyright (C) 2026 Philip Langdale <philipl@overt.org>
 *
 * Based on vf_framerate - Copyright (C) 2012 Mark Himsley
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

/**
 * @file
 * Frame rate up-conversion filter that synthesises intermediate frames using
 * the NVIDIA Vulkan optical flow extension (VK_NV_optical_flow).
 */

#include "libavutil/avassert.h"
#include "libavutil/eval.h"
#include "libavutil/internal.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "vulkan_filter.h"
#include "filters.h"
#include "video.h"

extern const unsigned char ff_fruc_grayscale_comp_spv_data[];
extern const unsigned int  ff_fruc_grayscale_comp_spv_len;
extern const unsigned char ff_fruc_interpolate_comp_spv_data[];
extern const unsigned int  ff_fruc_interpolate_comp_spv_len;

static const char *const var_names[] = {
    "source_fps",
    NULL
};

enum var_name {
    VAR_SOURCE_FPS,
    VARS_NB
};

typedef struct GrayscalePushData {
    float luma_weights[4][4]; ///< per-plane RGB->Y weights (dotted with each plane's texel)
    int32_t planes;           ///< number of input planes sampled per frame
} GrayscalePushData;

typedef struct InterpolatePushData {
    float   t;
    int32_t planes;
    float   luma_weights[4][4]; ///< per-plane RGB->Y weights, matching the grayscale pass
    float   plane_size[4][2];   ///< visible texel extent of each plane
} InterpolatePushData;

/* Double-buffer the per-pair optical flow resources so one pair's flow execution
 * overlaps the previous pair's interpolations instead of stalling on shared
 * images. The session bakes in its image bindings, so each slot owns its session,
 * grayscale and flow images. Two suffice: the flow engine is serial. */
#define FRUC_NB_SLOTS 2

typedef struct FRUCFlowSlot {
    VkOpticalFlowSessionNV session;

    VkImage        gray_img[2];     ///< grayscale inputs (INPUT, REFERENCE)
    VkDeviceMemory gray_mem[2];
    VkImageView    gray_view[2];

    VkImage        flow_img[2];     ///< [0] forward, [1] backward
    VkDeviceMemory flow_mem[2];
    VkImageView    flow_view[2];      ///< native (SFIXED5) view, bound to the OF session
    VkImageView    flow_sint_view[2]; ///< R16G16_SINT reinterpret view for sampling

    /* sem_interp value reached by the pair that last used this slot; the next
     * pair to reuse it waits here before overwriting the flow images. Zero (the
     * initial value) is satisfied immediately, covering the first use. */
    uint64_t       interp_done;
} FRUCFlowSlot;

typedef struct FRUCVulkanContext {
    FFVulkanContext vkctx;

    AVVulkanDeviceQueueFamily *qf;      ///< compute queue family
    AVVulkanDeviceQueueFamily *qf_of;   ///< optical flow queue family

    FFVkExecPool e;                     ///< compute execution pool
    FFVkExecPool e_of;                  ///< optical flow execution pool

    FFVulkanShader grayscale;
    FFVulkanShader interpolate;
    VkSampler      sampler;        ///< linear sampler for the video planes
    VkSampler      flow_sampler;   ///< nearest sampler for the flow vectors

    /* Timeline semaphores order the pipelined cross-queue submissions and make
     * each stage's writes visible to the next. Timeline is required for two
     * reasons:
     * * Each optical flow source pair may yield multiple interpolations, and
     *   each interpolation should wait on the flow. Multi-wait requires timeline
     *   semaphores
     * * We don't know how many interpolations will be done from a single optical
     *   flow pair ahead of time, so we cannot simply signal completion after the
     *   last one.  Instead we increment a timeline semaphore after each one and
     *   then the next flow calculation waits on the final timeline value. */
    VkSemaphore    sem_gray;        ///< grayscale (compute) -> optical flow
    VkSemaphore    sem_flow;        ///< optical flow -> interpolation (compute)
    VkSemaphore    sem_interp;      ///< interpolation reads -> next pair optical flow
    uint64_t       gen;             ///< source pair generation (sem_gray/sem_flow value)
    uint64_t       interp_value;    ///< monotonic interpolation counter (sem_interp value)

    /* Optical flow session parameters (images live per-slot, see slots[]). */
    VkOpticalFlowGridSizeFlagsNV grid_bit;
    int                          grid_size;
    VkFormat                     input_format;   ///< grayscale input format
    VkFormat                     flow_format;    ///< flow vector format

    /* Tuning options. */
    int                          perf_level;     ///< VkOpticalFlowPerformanceLevelNV
    int                          opt_grid_size;  ///< requested grid in pixels (0 = finest)

    int width;          ///< luma width
    int height;         ///< luma height
    int flow_width;
    int flow_height;
    float luma_weights[4][4]; ///< RGB->Y weights for the grayscale pass
    int   gray_planes;        ///< number of input planes the grayscale pass samples

    /* Double-buffered optical flow resources, indexed by (gen % FRUC_NB_SLOTS). */
    FRUCFlowSlot slots[FRUC_NB_SLOTS];

    int flow_valid;                 ///< flow computed for current (f0, f1) pair

    // parameters
    char       *requested_frame_rate;   ///< output fps as an expression
    AVRational dest_frame_rate;         ///< output frames per second

    AVRational srce_time_base;          ///< timebase of source
    AVRational dest_time_base;          ///< timebase of destination

    AVFrame *work;

    AVFrame *f0;                        ///< last frame
    AVFrame *f1;                        ///< current frame
    int64_t pts0;                       ///< last frame pts in dest_time_base
    int64_t pts1;                       ///< current frame pts in dest_time_base
    int64_t delta;                      ///< pts1 to pts0 delta
    int flush;                          ///< 1 if the filter is being flushed
    int64_t start_pts;                  ///< pts of the first output frame
    int64_t n;                          ///< output frame counter
} FRUCVulkanContext;

#define OFFSET(x) offsetof(FRUCVulkanContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)

static const AVOption fruc_vulkan_options[] = {
    { "fps", "A string describing the desired output frame rate",
      OFFSET(requested_frame_rate), AV_OPT_TYPE_STRING, { .str = "60" }, 0, 0, FLAGS },
    { "perf", "Optical flow performance level (quality versus speed)",
      OFFSET(perf_level), AV_OPT_TYPE_INT, { .i64 = VK_OPTICAL_FLOW_PERFORMANCE_LEVEL_SLOW_NV },
      VK_OPTICAL_FLOW_PERFORMANCE_LEVEL_SLOW_NV, VK_OPTICAL_FLOW_PERFORMANCE_LEVEL_FAST_NV,
      FLAGS, .unit = "perf" },
        { "slow",   "Highest quality, slowest", 0, AV_OPT_TYPE_CONST,
          { .i64 = VK_OPTICAL_FLOW_PERFORMANCE_LEVEL_SLOW_NV },   0, 0, FLAGS, .unit = "perf" },
        { "medium", "Balanced quality and speed", 0, AV_OPT_TYPE_CONST,
          { .i64 = VK_OPTICAL_FLOW_PERFORMANCE_LEVEL_MEDIUM_NV }, 0, 0, FLAGS, .unit = "perf" },
        { "fast",   "Lowest quality, fastest", 0, AV_OPT_TYPE_CONST,
          { .i64 = VK_OPTICAL_FLOW_PERFORMANCE_LEVEL_FAST_NV },   0, 0, FLAGS, .unit = "perf" },
    { "grid", "Optical flow output grid size in pixels (coarser is faster)",
      OFFSET(opt_grid_size), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 8, FLAGS, .unit = "grid" },
        { "auto", "Finest grid the device supports", 0, AV_OPT_TYPE_CONST,
          { .i64 = 0 }, 0, 0, FLAGS, .unit = "grid" },
        { "1", "1x1", 0, AV_OPT_TYPE_CONST, { .i64 = 1 }, 0, 0, FLAGS, .unit = "grid" },
        { "2", "2x2", 0, AV_OPT_TYPE_CONST, { .i64 = 2 }, 0, 0, FLAGS, .unit = "grid" },
        { "4", "4x4", 0, AV_OPT_TYPE_CONST, { .i64 = 4 }, 0, 0, FLAGS, .unit = "grid" },
        { "8", "8x8", 0, AV_OPT_TYPE_CONST, { .i64 = 8 }, 0, 0, FLAGS, .unit = "grid" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(fruc_vulkan);

static VkFormat pick_of_format(FRUCVulkanContext *s, VkOpticalFlowUsageFlagsNV usage,
                               VkFormat preferred)
{
    FFVulkanContext *vkctx = &s->vkctx;
    FFVulkanFunctions *vk = &vkctx->vkfn;
    VkOpticalFlowImageFormatInfoNV info = {
        .sType = VK_STRUCTURE_TYPE_OPTICAL_FLOW_IMAGE_FORMAT_INFO_NV,
        .usage = usage,
    };
    VkOpticalFlowImageFormatPropertiesNV *props;
    VkFormat result = VK_FORMAT_UNDEFINED;
    uint32_t count = 0;

    vk->GetPhysicalDeviceOpticalFlowImageFormatsNV(vkctx->hwctx->phys_dev, &info,
                                                   &count, NULL);
    if (!count)
        return VK_FORMAT_UNDEFINED;

    props = av_calloc(count, sizeof(*props));
    if (!props)
        return VK_FORMAT_UNDEFINED;
    for (uint32_t i = 0; i < count; i++)
        props[i].sType = VK_STRUCTURE_TYPE_OPTICAL_FLOW_IMAGE_FORMAT_PROPERTIES_NV;

    vk->GetPhysicalDeviceOpticalFlowImageFormatsNV(vkctx->hwctx->phys_dev, &info,
                                                   &count, props);

    result = props[0].format;
    for (uint32_t i = 0; i < count; i++) {
        av_log(s, AV_LOG_VERBOSE, "Optical flow usage 0x%x supports format %d\n",
               usage, props[i].format);
        if (props[i].format == preferred) {
            result = preferred;
            break;
        }
    }

    av_free(props);
    return result;
}

static int create_of_image(FRUCVulkanContext *s, VkImage *img, VkDeviceMemory *mem,
                           VkImageView *view, VkFormat format, int width, int height,
                           VkOpticalFlowUsageFlagsNV of_usage, VkImageUsageFlags usage,
                           VkImageCreateFlags create_flags)
{
    FFVulkanContext *vkctx = &s->vkctx;
    FFVulkanFunctions *vk = &vkctx->vkfn;
    AVVulkanDeviceContext *hwctx = vkctx->hwctx;
    VkResult ret;
    int err;

    VkOpticalFlowImageFormatInfoNV of_info = {
        .sType = VK_STRUCTURE_TYPE_OPTICAL_FLOW_IMAGE_FORMAT_INFO_NV,
        .usage = of_usage,
    };
    VkImageViewCreateInfo view_info = {
        .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format   = format,
        .components = ff_comp_identity_map,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = 1,
        },
    };

    /* Exclusive sharing: the images are used by both the compute and the
     * optical flow queue family, and maintenance9 - which init_filter()
     * requires - preserves their contents across the two without explicit
     * queue family ownership transfers. */
    err = ff_vk_image_create(vkctx, img, mem, width, height, format, 1,
                             VK_IMAGE_TILING_OPTIMAL, usage, create_flags,
                             &of_info);
    if (err < 0)
        return err;

    view_info.image = *img;
    ret = vk->CreateImageView(hwctx->act_dev, &view_info, hwctx->alloc, view);
    if (ret != VK_SUCCESS) {
        av_log(s, AV_LOG_ERROR, "Failed to create optical flow image view: %s\n",
               ff_vk_ret2str(ret));
        return AVERROR_EXTERNAL;
    }

    return 0;
}

/* Transition the persistent optical flow images to VK_IMAGE_LAYOUT_GENERAL,
 * which is the layout they remain in for the lifetime of the filter. */
static int init_image_layouts(FRUCVulkanContext *s)
{
    FFVulkanContext *vkctx = &s->vkctx;
    FFVulkanFunctions *vk = &vkctx->vkfn;
    FFVkExecContext *exec = ff_vk_exec_get(vkctx, &s->e);
    VkImageMemoryBarrier2 bar[4 * FRUC_NB_SLOTS];
    int nb_bar = 0;
    int err;

    err = ff_vk_exec_start(vkctx, exec);
    if (err < 0)
        return err;

    for (int slot = 0; slot < FRUC_NB_SLOTS; slot++) {
        FRUCFlowSlot *fs = &s->slots[slot];
        VkImage imgs[4] = { fs->gray_img[0], fs->gray_img[1],
                            fs->flow_img[0], fs->flow_img[1] };

        for (int i = 0; i < 4; i++) {
            bar[nb_bar++] = (VkImageMemoryBarrier2) {
                .sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                .srcAccessMask = 0,
                .dstStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                .oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout     = VK_IMAGE_LAYOUT_GENERAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image         = imgs[i],
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .levelCount = 1,
                    .layerCount = 1,
                },
            };
        }
    }

    vk->CmdPipelineBarrier2(exec->buf, &(VkDependencyInfo) {
        .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pImageMemoryBarriers    = bar,
        .imageMemoryBarrierCount = nb_bar,
    });

    err = ff_vk_exec_submit(vkctx, exec);
    if (err < 0)
        return err;
    ff_vk_exec_wait(vkctx, exec);

    return 0;
}

static int packed_rgb_channel(const AVPixFmtDescriptor *desc, VkFormat vkfmt,
                              int comp)
{
    const AVComponentDescriptor *c = &desc->comp[comp];

    switch (vkfmt) {
    /* These are all sampled as their logical components, whatever order the
     * pix fmt uses to lay them out in host memory, so the component index is
     * already the channel. */
    case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
    case VK_FORMAT_B8G8R8_UNORM:
    case VK_FORMAT_B8G8R8A8_UNORM:
        return comp;
    default:
        return c->offset / ((c->depth + 7) / 8);
    }
}

static int packed_luma_channel(const AVPixFmtDescriptor *desc, VkFormat vkfmt)
{
    const AVComponentDescriptor *c = &desc->comp[0];

    switch (vkfmt) {
    /* xv30 packs V, Y, U, so luma is the logical G channel. */
    case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
        return 1;
    default:
        return c->offset / ((c->depth + 7) / 8);
    }
}

static av_cold int check_sw_format(AVFilterContext *avctx, enum AVPixelFormat sw_format)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(sw_format);

    if (!desc || !av_vkfmt_from_pixfmt(sw_format))
        return AVERROR(EINVAL);

    if (desc->flags & AV_PIX_FMT_FLAG_BAYER) {
        av_log(avctx, AV_LOG_ERROR, "Bayer input (%s) is not supported\n",
               desc->name);
        return AVERROR(ENOTSUP);
    }

    if (!(desc->flags & AV_PIX_FMT_FLAG_RGB) && desc->nb_components > 1 &&
        !(desc->flags & AV_PIX_FMT_FLAG_PLANAR) &&
        (desc->log2_chroma_w || desc->log2_chroma_h)) {
        av_log(avctx, AV_LOG_ERROR, "Subsampled packed YUV input (%s) is not "
               "supported\n", desc->name);
        return AVERROR(ENOTSUP);
    }

    /* Everything is sampled and stored through FF_VK_REP_FLOAT views. Formats
     * with more than 16 bits of integer per component (rgb96, rgba128, gray32,
     * gbrap32) have no float representation at all, so the image views would
     * fail to be created. */
    if (desc->comp[0].depth > 16 && !(desc->flags & AV_PIX_FMT_FLAG_FLOAT)) {
        av_log(avctx, AV_LOG_ERROR, "Input format %s has no floating point "
               "shader representation\n", desc->name);
        return AVERROR(ENOTSUP);
    }

    return 0;
}

/* Whether an optimally tiled image last used by queue family "from" keeps its
 * contents when used by queue family "to" without an explicit ownership
 * transfer. */
static int qf_transfer_preserves(FFVulkanContext *vkctx, uint32_t from, uint32_t to)
{
    uint32_t mask;

    if (from >= (uint32_t)vkctx->tot_nb_qfs || to >= 32)
        return 0;

    mask = vkctx->ownership_props[from].optimalImageTransferToQueueFamilies;
    return !!(mask & (1U << to));
}

static av_cold int init_filter(AVFilterContext *avctx)
{
    int err;
    FRUCVulkanContext *s = avctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;
    FFVulkanFunctions *vk = &vkctx->vkfn;
    const int planes = av_pix_fmt_count_planes(vkctx->output_format);
    VkOpticalFlowGridSizeFlagsNV grids;
    VkResult ret;

   /* The core Vulkan support covers more formats than we can actually support
    * in the filter, so reject anything we can't handle up front.
    */
    RET(check_sw_format(avctx, vkctx->output_format));

    s->width  = vkctx->output_width;
    s->height = vkctx->output_height;

    /* Optical flow tracks luma. YUV carries it directly, RGB has it derived so the
     * flow follows brightness. The value only feeds the flow engine and is never
     * written out, so a fixed BT.709 matrix suffices for all RGB inputs. */
    {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(vkctx->output_format);
        const VkFormat *vkfmts = av_vkfmt_from_pixfmt(vkctx->output_format);
        static const float bt709[3] = { 0.2126f, 0.7152f, 0.0722f }; /* R, G, B */

        memset(s->luma_weights, 0, sizeof(s->luma_weights));
        s->gray_planes = 1;

        if (desc->flags & AV_PIX_FMT_FLAG_PLANAR) {
            if (desc->flags & AV_PIX_FMT_FLAG_RGB) {
                /* One component per plane: weight each plane by its component's
                 * coefficient (comp[c] is R, G, B for c = 0, 1, 2). */
                s->gray_planes = av_pix_fmt_count_planes(vkctx->output_format);
                for (int c = 0; c < 3; c++)
                    s->luma_weights[desc->comp[c].plane][0] = bt709[c];
            } else {
                /* Planar and semi-planar YUV: plane 0 is luma already. */
                s->luma_weights[0][0] = 1.0f;
            }
        } else if (desc->nb_components > 1) {
            /* Packed: every component shares plane 0's texel, so the weights
             * select channels rather than planes. */
            if (desc->flags & AV_PIX_FMT_FLAG_RGB) {
                for (int c = 0; c < 3; c++)
                    s->luma_weights[0][packed_rgb_channel(desc, vkfmts[0], c)] = bt709[c];
            } else {
                /* Packed 4:4:4 YUV: luma is a single component, but not
                 * necessarily the first channel. */
                s->luma_weights[0][packed_luma_channel(desc, vkfmts[0])] = 1.0f;
            }
        } else {
            /* Gray: the one channel is luma. */
            s->luma_weights[0][0] = 1.0f;
        }

        /* Special handling is required for formats that store fewer than
         * 16bits of data in 16bits of storage. Some of these formats align the
         * bits to the LSB end, and if these values are interpreted directly as
         * 16bit values, they will be incorrect. While we could imagine passing
         * the depth/shift information separately to the shader, we can apply
         * the adjustment to the luma weights instead.
         *
         * Adjustment is not done if shift+depth == 16. In this case, the data
         * is MSB aligned, and should be treated as a 16bit value.
         */
        for (int p = 0; p < s->gray_planes; p++) {
            const AVComponentDescriptor *comp = NULL;
            float scale;

            if (vkfmts[p] != VK_FORMAT_R16_UNORM)
                continue;

            for (int c = 0; c < desc->nb_components; c++) {
                if (desc->comp[c].plane == p) {
                    comp = &desc->comp[c];
                    break;
                }
            }
            if (!comp)
                return AVERROR_BUG;

            if (comp->shift + comp->depth == 16)
                continue;

            scale = 65535.0f / (((1U << comp->depth) - 1U) << comp->shift);
            for (int c = 0; c < 4; c++)
                s->luma_weights[p][c] *= scale;
        }
    }

    if (!(vkctx->extensions & FF_VK_EXT_OPTICAL_FLOW)) {
        av_log(avctx, AV_LOG_ERROR, "Vulkan device does not support the "
               "VK_NV_optical_flow extension\n");
        return AVERROR(ENOTSUP);
    }

    /* The extension being enabled is not enough; its feature has to have been
     * enabled at device creation too. Our own device setup does that, but a
     * device handed to us by the caller may not have. */
    {
        const VkPhysicalDeviceOpticalFlowFeaturesNV *of;
        of = ff_vk_find_struct(vkctx->hwctx->device_features.pNext,
                               VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPTICAL_FLOW_FEATURES_NV);
        if (!of || !of->opticalFlow) {
            av_log(avctx, AV_LOG_ERROR, "Vulkan device was created without the "
                   "opticalFlow feature enabled\n");
            return AVERROR(ENOTSUP);
        }
    }

    /* The optical flow images must be passed between the compute and optical
     * flow queue families; maintenance9 allows us to avoid the explicit
     * ownership transfers that are otherwise required. */
    if (!(vkctx->extensions & FF_VK_EXT_MAINTENANCE_9)) {
        av_log(avctx, AV_LOG_ERROR, "Vulkan device does not support the "
               "VK_KHR_maintenance9 extension\n");
        return AVERROR(ENOTSUP);
    }

    {
        const VkPhysicalDeviceMaintenance9FeaturesKHR *m9;
        m9 = ff_vk_find_struct(vkctx->hwctx->device_features.pNext,
                               VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_9_FEATURES_KHR);
        if (!m9 || !m9->maintenance9) {
            av_log(avctx, AV_LOG_ERROR, "Vulkan device was created without the "
                   "maintenance9 feature enabled\n");
            return AVERROR(ENOTSUP);
        }
    }

    s->qf = ff_vk_qf_find(vkctx, VK_QUEUE_COMPUTE_BIT, 0);
    if (!s->qf) {
        av_log(avctx, AV_LOG_ERROR, "Device has no compute queues\n");
        return AVERROR(ENOTSUP);
    }

    s->qf_of = ff_vk_qf_find(vkctx, VK_QUEUE_OPTICAL_FLOW_BIT_NV, 0);
    if (!s->qf_of) {
        av_log(avctx, AV_LOG_ERROR, "Device has no optical flow queues\n");
        return AVERROR(ENOTSUP);
    }

    /* Even with maintenance9 enabled, we can't assume that we can do implicit
     * ownership transfers between the queues we care about; the driver doesn't
     * have to support this, so we must check for declared support. */
    if (s->qf_of->idx != s->qf->idx &&
        (!qf_transfer_preserves(vkctx, s->qf->idx, s->qf_of->idx) ||
         !qf_transfer_preserves(vkctx, s->qf_of->idx, s->qf->idx))) {
        av_log(avctx, AV_LOG_ERROR, "The compute (%d) and optical flow (%d) queue "
               "families cannot exchange optimally tiled images without explicit "
               "queue family ownership transfers\n", s->qf->idx, s->qf_of->idx);
        return AVERROR(ENOTSUP);
    }

    RET(ff_vk_exec_pool_init(vkctx, s->qf, &s->e, FF_VK_DEFAULT_EXEC_CONTEXTS, 0, 0, 0, NULL));
    /* One optical flow context per slot so that the optical flow execution for
     * the next frame pair can be recorded and submitted without first
     * host-waiting the previous pair's execution to retire its command buffer. */
    RET(ff_vk_exec_pool_init(vkctx, s->qf_of, &s->e_of,
                             FRUC_NB_SLOTS, 0, 0, 0, NULL));
    RET(ff_vk_init_sampler(vkctx, &s->sampler, 0, VK_FILTER_LINEAR));
    /* Flow is sampled through an integer view and its format has no linear
     * filtering support, so use nearest. Requires normalised coords and as we
     * have only one mip level, the mipmap filtering mode is irrelevant. */
    RET(ff_vk_init_sampler(vkctx, &s->flow_sampler, 0, VK_FILTER_NEAREST));

    {
        VkSemaphoreTypeCreateInfo sem_type_info = {
            .sType         = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
            .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
            .initialValue  = 0,
        };
        VkSemaphoreCreateInfo sem_info = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = &sem_type_info,
        };
        if (vk->CreateSemaphore(vkctx->hwctx->act_dev, &sem_info,
                                vkctx->hwctx->alloc, &s->sem_gray) != VK_SUCCESS ||
            vk->CreateSemaphore(vkctx->hwctx->act_dev, &sem_info,
                                vkctx->hwctx->alloc, &s->sem_flow) != VK_SUCCESS ||
            vk->CreateSemaphore(vkctx->hwctx->act_dev, &sem_info,
                                vkctx->hwctx->alloc, &s->sem_interp) != VK_SUCCESS) {
            av_log(avctx, AV_LOG_ERROR, "Failed to create synchronization semaphores\n");
            return AVERROR_EXTERNAL;
        }
    }

    /* Select the optical flow image formats and grid size. */
    s->input_format = pick_of_format(s, VK_OPTICAL_FLOW_USAGE_INPUT_BIT_NV,
                                     VK_FORMAT_R8_UNORM);
    if (s->input_format != VK_FORMAT_R8_UNORM) {
        av_log(avctx, AV_LOG_ERROR, "Optical flow R8 input format unavailable\n");
        return AVERROR(ENOTSUP);
    }
    /* The engine emits flow vectors as signed fixed point (SFIXED5); theoretically
     * it could be something else, but this is all we've seen on real hardware. */
    s->flow_format = pick_of_format(s, VK_OPTICAL_FLOW_USAGE_OUTPUT_BIT_NV,
                                    VK_FORMAT_R16G16_S10_5_NV);
    if (s->flow_format != VK_FORMAT_R16G16_S10_5_NV) {
        av_log(avctx, AV_LOG_ERROR, "Optical flow SFIXED5 vector format "
               "unavailable (got %d)\n", s->flow_format);
        return AVERROR(ENOTSUP);
    }

    static const struct {
        int                          size;
        VkOpticalFlowGridSizeFlagsNV bit;
    } grid_map[] = {
        { 1, VK_OPTICAL_FLOW_GRID_SIZE_1X1_BIT_NV },
        { 2, VK_OPTICAL_FLOW_GRID_SIZE_2X2_BIT_NV },
        { 4, VK_OPTICAL_FLOW_GRID_SIZE_4X4_BIT_NV },
        { 8, VK_OPTICAL_FLOW_GRID_SIZE_8X8_BIT_NV },
    };

    grids = vkctx->optical_flow_props.supportedOutputGridSizes;
    if (s->opt_grid_size) {
        /* Honour an explicit grid request, erroring if the device lacks it. */
        VkOpticalFlowGridSizeFlagsNV want = 0;
        for (int i = 0; i < FF_ARRAY_ELEMS(grid_map); i++)
            if (grid_map[i].size == s->opt_grid_size)
                want = grid_map[i].bit;
        if (!want || !(grids & want)) {
            av_log(avctx, AV_LOG_ERROR, "Requested optical flow grid size %d is not "
                   "supported by the device (supported mask 0x%x)\n",
                   s->opt_grid_size, grids);
            return AVERROR(ENOTSUP);
        }
        s->grid_size = s->opt_grid_size;
        s->grid_bit  = want;
    } else {
        /* Auto: pick the finest (smallest) grid the device supports. */
        s->grid_size = 0;
        for (int i = 0; i < FF_ARRAY_ELEMS(grid_map); i++) {
            if (grids & grid_map[i].bit) {
                s->grid_size = grid_map[i].size;
                s->grid_bit  = grid_map[i].bit;
                break;
            }
        }
        if (!s->grid_size) {
            av_log(avctx, AV_LOG_ERROR, "No supported optical flow output grid size\n");
            return AVERROR(ENOTSUP);
        }
    }

    s->flow_width  = (s->width  + s->grid_size - 1) / s->grid_size;
    s->flow_height = (s->height + s->grid_size - 1) / s->grid_size;

    av_log(avctx, AV_LOG_INFO, "optical flow: perf %d, grid %d, flow %dx%d, bidir=%d, "
           "min %dx%d max %dx%d\n", s->perf_level, s->grid_size, s->flow_width, s->flow_height,
           vkctx->optical_flow_props.bidirectionalFlowSupported,
           vkctx->optical_flow_props.minWidth, vkctx->optical_flow_props.minHeight,
           vkctx->optical_flow_props.maxWidth, vkctx->optical_flow_props.maxHeight);

    {
        const VkPhysicalDeviceOpticalFlowPropertiesNV *ofp = &vkctx->optical_flow_props;

        if ((uint32_t)s->width  < ofp->minWidth  || (uint32_t)s->width  > ofp->maxWidth ||
            (uint32_t)s->height < ofp->minHeight || (uint32_t)s->height > ofp->maxHeight) {
            av_log(avctx, AV_LOG_ERROR, "Frame size %dx%d is outside the range the "
                   "device optical flow engine supports (%ux%u to %ux%u)\n",
                   s->width, s->height,
                   ofp->minWidth, ofp->minHeight, ofp->maxWidth, ofp->maxHeight);
            return AVERROR(ENOTSUP);
        }
    }

    if (!vkctx->optical_flow_props.bidirectionalFlowSupported) {
        av_log(avctx, AV_LOG_ERROR, "Device optical flow engine does not support "
               "bidirectional flow, which this filter requires\n");
        return AVERROR(ENOTSUP);
    }

    /* Create the persistent optical flow images and sessions, one set per slot
     * so consecutive source pairs round-robin between independent resources. */
    for (int slot = 0; slot < FRUC_NB_SLOTS; slot++) {
        FRUCFlowSlot *fs = &s->slots[slot];

        RET(create_of_image(s, &fs->gray_img[0], &fs->gray_mem[0], &fs->gray_view[0],
                            s->input_format, s->width, s->height,
                            VK_OPTICAL_FLOW_USAGE_INPUT_BIT_NV,
                            VK_IMAGE_USAGE_STORAGE_BIT, 0));
        RET(create_of_image(s, &fs->gray_img[1], &fs->gray_mem[1], &fs->gray_view[1],
                            s->input_format, s->width, s->height,
                            VK_OPTICAL_FLOW_USAGE_INPUT_BIT_NV,
                            VK_IMAGE_USAGE_STORAGE_BIT, 0));
        /* The flow images must be created mutable, as we need to sample the raw
        * integers through an R16G16_SINT view. SFIXED5 advertises SAMPLED_IMAGE,
        * but if you try and use a float sampler, it will read the values as
        * R16G16_SFLOAT, resulting in garbage. So we have to read the raw bits and
        * rescale them (value/32) ourselves. */
        RET(create_of_image(s, &fs->flow_img[0], &fs->flow_mem[0], &fs->flow_view[0],
                            s->flow_format, s->flow_width, s->flow_height,
                            VK_OPTICAL_FLOW_USAGE_OUTPUT_BIT_NV,
                            VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT));
        RET(create_of_image(s, &fs->flow_img[1], &fs->flow_mem[1], &fs->flow_view[1],
                            s->flow_format, s->flow_width, s->flow_height,
                            VK_OPTICAL_FLOW_USAGE_OUTPUT_BIT_NV,
                            VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT));

        for (int i = 0; i < 2; i++) {
            VkImageViewCreateInfo view_info = {
                .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image    = fs->flow_img[i],
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format   = VK_FORMAT_R16G16_SINT,
                .components = ff_comp_identity_map,
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .levelCount = 1,
                    .layerCount = 1,
                },
            };
            if (vk->CreateImageView(vkctx->hwctx->act_dev, &view_info,
                                    vkctx->hwctx->alloc, &fs->flow_sint_view[i]) != VK_SUCCESS) {
                av_log(avctx, AV_LOG_ERROR, "Failed to create flow SINT view\n");
                return AVERROR_EXTERNAL;
            }
        }

        ret = vk->CreateOpticalFlowSessionNV(vkctx->hwctx->act_dev,
            &(VkOpticalFlowSessionCreateInfoNV) {
                .sType            = VK_STRUCTURE_TYPE_OPTICAL_FLOW_SESSION_CREATE_INFO_NV,
                .width            = s->width,
                .height           = s->height,
                .imageFormat      = s->input_format,
                .flowVectorFormat = s->flow_format,
                .outputGridSize   = s->grid_bit,
                .performanceLevel = s->perf_level,
                .flags            = VK_OPTICAL_FLOW_SESSION_CREATE_BOTH_DIRECTIONS_BIT_NV,
            }, vkctx->hwctx->alloc, &fs->session);
        if (ret != VK_SUCCESS) {
            av_log(avctx, AV_LOG_ERROR, "Failed to create optical flow session: %s\n",
                   ff_vk_ret2str(ret));
            return AVERROR_EXTERNAL;
        }

        static const VkOpticalFlowSessionBindingPointNV binding_points[] = {
            VK_OPTICAL_FLOW_SESSION_BINDING_POINT_INPUT_NV,
            VK_OPTICAL_FLOW_SESSION_BINDING_POINT_REFERENCE_NV,
            VK_OPTICAL_FLOW_SESSION_BINDING_POINT_FLOW_VECTOR_NV,
            VK_OPTICAL_FLOW_SESSION_BINDING_POINT_BACKWARD_FLOW_VECTOR_NV,
        };
        const VkImageView binding_views[] = {
            fs->gray_view[0], fs->gray_view[1],
            fs->flow_view[0], fs->flow_view[1],
        };
        for (int i = 0; i < FF_ARRAY_ELEMS(binding_points); i++) {
            ret = vk->BindOpticalFlowSessionImageNV(vkctx->hwctx->act_dev, fs->session,
                binding_points[i], binding_views[i], VK_IMAGE_LAYOUT_GENERAL);
            if (ret != VK_SUCCESS) {
                av_log(avctx, AV_LOG_ERROR, "Failed to bind optical flow session "
                       "image: %s\n", ff_vk_ret2str(ret));
                return AVERROR_EXTERNAL;
            }
        }
    }

    RET(init_image_layouts(s));

    /* Grayscale extraction shader. */
    ff_vk_shader_load(&s->grayscale, VK_SHADER_STAGE_COMPUTE_BIT, NULL,
                      (uint32_t []) { 32, 32, 1 }, 0);
    ff_vk_shader_add_push_const(&s->grayscale, 0, sizeof(GrayscalePushData),
                                VK_SHADER_STAGE_COMPUTE_BIT);
    {
        const FFVulkanDescriptorSetBinding desc[] = {
            {
                .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .dimensions = 2,
                .elems      = s->gray_planes,
                .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
                .samplers   = DUP_SAMPLER(s->sampler),
            },
            {
                .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .dimensions = 2,
                .elems      = s->gray_planes,
                .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
                .samplers   = DUP_SAMPLER(s->sampler),
            },
            {
                .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .mem_layout = "r8",
                .mem_quali  = "writeonly",
                .dimensions = 2,
                .elems      = 2,
                .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
            },
        };
        ff_vk_shader_add_descriptor_set(vkctx, &s->grayscale, desc, 3, 0);
    }
    RET(ff_vk_shader_link(vkctx, &s->grayscale,
                          ff_fruc_grayscale_comp_spv_data,
                          ff_fruc_grayscale_comp_spv_len, "main"));
    RET(ff_vk_shader_register_exec(vkctx, &s->e, &s->grayscale));

    /* Motion compensated interpolation shader. */
    ff_vk_shader_load(&s->interpolate, VK_SHADER_STAGE_COMPUTE_BIT, NULL,
                      (uint32_t []) { 32, 32, 1 }, 0);
    ff_vk_shader_add_push_const(&s->interpolate, 0, sizeof(InterpolatePushData),
                                VK_SHADER_STAGE_COMPUTE_BIT);
    {
        const FFVulkanDescriptorSetBinding desc[] = {
            {
                .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .dimensions = 2,
                .elems      = planes,
                .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
                .samplers   = DUP_SAMPLER(s->sampler),
            },
            {
                .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .dimensions = 2,
                .elems      = planes,
                .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
                .samplers   = DUP_SAMPLER(s->sampler),
            },
            {
                .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .mem_quali  = "writeonly",
                .dimensions = 2,
                .elems      = planes,
                .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
            },
            {
                .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .dimensions = 2,
                .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
                .samplers   = DUP_SAMPLER(s->flow_sampler),
            },
            {
                .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .dimensions = 2,
                .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
                .samplers   = DUP_SAMPLER(s->flow_sampler),
            },
        };
        ff_vk_shader_add_descriptor_set(vkctx, &s->interpolate, desc, 5, 0);
    }
    RET(ff_vk_shader_link(vkctx, &s->interpolate,
                          ff_fruc_interpolate_comp_spv_data,
                          ff_fruc_interpolate_comp_spv_len, "main"));
    RET(ff_vk_shader_register_exec(vkctx, &s->e, &s->interpolate));

fail:
    return err;
}

static void of_image_barrier(VkImageMemoryBarrier2 *bar, VkImage img,
                             VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                             VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access)
{
    *bar = (VkImageMemoryBarrier2) {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask  = src_stage,
        .srcAccessMask = src_access,
        .dstStageMask  = dst_stage,
        .dstAccessMask = dst_access,
        .oldLayout     = VK_IMAGE_LAYOUT_GENERAL,
        .newLayout     = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image         = img,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = 1,
        },
    };
}

/* Compute the forward and backward optical flow between f0 and f1. */
static int compute_flow(AVFilterContext *avctx)
{
    int err;
    FRUCVulkanContext *s = avctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;
    FFVulkanFunctions *vk = &vkctx->vkfn;
    FFVkExecContext *exec;
    VkImageView f0_views[AV_NUM_DATA_POINTERS];
    VkImageView f1_views[AV_NUM_DATA_POINTERS];
    /* f0 and f1 each contribute one barrier per VkImage (a multi-image
     * sw_format such as planar RGB or a separate alpha plane has several),
     * plus the two single-image grayscale targets. */
    VkImageMemoryBarrier2 img_bar[2 * AV_NUM_DATA_POINTERS + 2];
    int nb_img_bar;

    /* This pair's generation; sem_gray and sem_flow are signalled with it. */
    s->gen++;

    /* Round-robin slot for this pair's optical flow resources. */
    FRUCFlowSlot *fs = &s->slots[s->gen % FRUC_NB_SLOTS];

    /* --- Grayscale extraction on the compute queue. --- */
    exec = ff_vk_exec_get(vkctx, &s->e);
    err = ff_vk_exec_start(vkctx, exec);
    if (err < 0)
        return err;

    RET(ff_vk_exec_add_dep_frame(vkctx, exec, s->f0,
                                 VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT));
    RET(ff_vk_exec_add_dep_frame(vkctx, exec, s->f1,
                                 VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT));
    /* Wait for the prior occupant of this slot (FRUC_NB_SLOTS pairs ago) to
     * finish reading its grayscale images on the flow engine before overwriting
     * them; the slot's first uses have no prior occupant. */
    if (s->gen > FRUC_NB_SLOTS)
        ff_vk_exec_add_dep_wait_sem(vkctx, exec, s->sem_flow, s->gen - FRUC_NB_SLOTS,
                                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    /* Serialize the sem_gray signals: the exec pool may round-robin consecutive
     * generations onto different queues, which have no implicit ordering, so
     * wait for the previous generation's signal before emitting this one.
     * Otherwise generation N+1 could signal the smaller value N+1 before N,
     * which is invalid for a timeline semaphore. Value 0 is the initial state. */
    ff_vk_exec_add_dep_wait_sem(vkctx, exec, s->sem_gray, s->gen - 1,
                                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    ff_vk_exec_add_dep_signal_sem(vkctx, exec, s->sem_gray, s->gen,
                                  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    RET(ff_vk_create_imageviews(vkctx, exec, f0_views, s->f0, FF_VK_REP_FLOAT));
    RET(ff_vk_create_imageviews(vkctx, exec, f1_views, s->f1, FF_VK_REP_FLOAT));

    for (int p = 0; p < s->gray_planes; p++) {
        ff_vk_shader_update_img(vkctx, exec, &s->grayscale, 0, 0, p,
                                f0_views[p], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                s->sampler);
        ff_vk_shader_update_img(vkctx, exec, &s->grayscale, 0, 1, p,
                                f1_views[p], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                s->sampler);
    }
    ff_vk_shader_update_img(vkctx, exec, &s->grayscale, 0, 2, 0,
                            fs->gray_view[0], VK_IMAGE_LAYOUT_GENERAL, VK_NULL_HANDLE);
    ff_vk_shader_update_img(vkctx, exec, &s->grayscale, 0, 2, 1,
                            fs->gray_view[1], VK_IMAGE_LAYOUT_GENERAL, VK_NULL_HANDLE);

    ff_vk_exec_bind_shader(vkctx, exec, &s->grayscale);
    {
        GrayscalePushData pd;
        memcpy(pd.luma_weights, s->luma_weights, sizeof(pd.luma_weights));
        pd.planes = s->gray_planes;
        ff_vk_shader_update_push_const(vkctx, exec, &s->grayscale,
                                       VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                       sizeof(GrayscalePushData), &pd);
    }

    nb_img_bar = 0;
    ff_vk_frame_barrier(vkctx, exec, s->f0, img_bar, &nb_img_bar,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_SHADER_READ_BIT,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_QUEUE_FAMILY_IGNORED);
    ff_vk_frame_barrier(vkctx, exec, s->f1, img_bar, &nb_img_bar,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_SHADER_READ_BIT,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_QUEUE_FAMILY_IGNORED);
    of_image_barrier(&img_bar[nb_img_bar++], fs->gray_img[0],
                     VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0,
                     VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT);
    of_image_barrier(&img_bar[nb_img_bar++], fs->gray_img[1],
                     VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0,
                     VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT);

    vk->CmdPipelineBarrier2(exec->buf, &(VkDependencyInfo) {
        .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pImageMemoryBarriers    = img_bar,
        .imageMemoryBarrierCount = nb_img_bar,
    });

    vk->CmdDispatch(exec->buf,
                    FFALIGN(s->width,  s->grayscale.lg_size[0]) / s->grayscale.lg_size[0],
                    FFALIGN(s->height, s->grayscale.lg_size[1]) / s->grayscale.lg_size[1],
                    1);

    /* sem_gray orders the optical flow submission after this one
     * and makes the grayscale writes visible across the queue boundary. */
    RET(ff_vk_exec_submit(vkctx, exec));

    /* --- Optical flow execution on the optical flow queue. --- */
    exec = ff_vk_exec_get(vkctx, &s->e_of);
    err = ff_vk_exec_start(vkctx, exec);
    if (err < 0)
        return err;

    /* Wait for the grayscale writes, signal once the flow has been written. */
    ff_vk_exec_add_dep_wait_sem(vkctx, exec, s->sem_gray, s->gen,
                                VK_PIPELINE_STAGE_2_OPTICAL_FLOW_BIT_NV);
    /* Wait for the slot's prior occupant to finish sampling its flow images
     * (fs->interp_done) before overwriting them; 0 is the initial state of an
     * unused slot and is satisfied immediately. */
    ff_vk_exec_add_dep_wait_sem(vkctx, exec, s->sem_interp, fs->interp_done,
                                VK_PIPELINE_STAGE_2_OPTICAL_FLOW_BIT_NV);
    /* Serialize the sem_flow signals for the same reason as sem_gray above: the
     * optical flow contexts may span multiple queues, so wait for the previous
     * generation's flow signal before signaling this one to keep the timeline
     * values monotonic. Value 0 is the initial state. */
    ff_vk_exec_add_dep_wait_sem(vkctx, exec, s->sem_flow, s->gen - 1,
                                VK_PIPELINE_STAGE_2_OPTICAL_FLOW_BIT_NV);
    ff_vk_exec_add_dep_signal_sem(vkctx, exec, s->sem_flow, s->gen,
                                  VK_PIPELINE_STAGE_2_OPTICAL_FLOW_BIT_NV);

    /* The flow images stay in VK_IMAGE_LAYOUT_GENERAL and the semaphores handle
     * cross-queue visibility, so no image barriers are needed here. */
    vk->CmdOpticalFlowExecuteNV(exec->buf, fs->session,
        &(VkOpticalFlowExecuteInfoNV) {
            .sType = VK_STRUCTURE_TYPE_OPTICAL_FLOW_EXECUTE_INFO_NV,
        });

    /* sem_flow orders the interpolation after the flow execution
     * and makes the flow writes visible on the compute queue. */
    RET(ff_vk_exec_submit(vkctx, exec));

    s->flow_valid = 1;
    return 0;

fail:
    ff_vk_exec_discard(vkctx, exec);
    return err;
}

/* Visible texel extent of a plane: plane 0 (and non-planar / alpha) is full size,
 * chroma planes are subsampled. Mirrors hwcontext_vulkan's get_plane_wh, which is
 * how the frames context sizes each plane's image. */
static void plane_wh(const AVPixFmtDescriptor *desc, int width, int height,
                     int plane, uint32_t *w, uint32_t *h)
{
    int sub = plane && plane != 3 && (desc->flags & AV_PIX_FMT_FLAG_PLANAR) &&
              !(desc->flags & AV_PIX_FMT_FLAG_RGB);

    *w = sub ? AV_CEIL_RSHIFT(width,  desc->log2_chroma_w) : width;
    *h = sub ? AV_CEIL_RSHIFT(height, desc->log2_chroma_h) : height;
}

/* Produce the motion compensated output frame at temporal position t. */
static int interpolate_frame(AVFilterContext *avctx, AVFrame *out, float t)
{
    int err;
    FRUCVulkanContext *s = avctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;
    FFVulkanFunctions *vk = &vkctx->vkfn;
    FFVkExecContext *exec;
    VkImageView f0_views[AV_NUM_DATA_POINTERS];
    VkImageView f1_views[AV_NUM_DATA_POINTERS];
    VkImageView out_views[AV_NUM_DATA_POINTERS];
    /* out, f0 and f1 each contribute one barrier per VkImage (a multi-image
     * sw_format such as planar RGB or a separate alpha plane has several),
     * plus the two single-image flow fields. */
    VkImageMemoryBarrier2 img_bar[3 * AV_NUM_DATA_POINTERS + 2];
    int nb_img_bar;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(vkctx->output_format);
    InterpolatePushData pd = {
        .t         = t,
        .planes    = av_pix_fmt_count_planes(vkctx->output_format),
    };
    memcpy(pd.luma_weights, s->luma_weights, sizeof(pd.luma_weights));
    /* The shader works in the visible frame's coordinate space; the source and
     * output images may each be allocated larger than that. */
    for (int i = 0; i < pd.planes; i++) {
        uint32_t w, h;
        plane_wh(desc, s->width, s->height, i, &w, &h);
        pd.plane_size[i][0] = w;
        pd.plane_size[i][1] = h;
    }

    /* Not via RET: the fail label discards deps on exec, which is not yet
     * acquired here, and compute_flow cleans up its own exec on failure. */
    if (!s->flow_valid) {
        err = compute_flow(avctx);
        if (err < 0)
            return err;
    }

    /* Same slot compute_flow selected for this pair's generation. */
    FRUCFlowSlot *fs = &s->slots[s->gen % FRUC_NB_SLOTS];

    exec = ff_vk_exec_get(vkctx, &s->e);
    err = ff_vk_exec_start(vkctx, exec);
    if (err < 0)
        return err;

    /* Every interpolation of this pair waits on the same flow result, keyed by
     * the pair generation. */
    ff_vk_exec_add_dep_wait_sem(vkctx, exec, s->sem_flow, s->gen,
                                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    /* Chain sem_interp: wait the previous value, signal the next. Keeps the
     * signals monotonic and lets the next pair's optical flow fence on the final
     * value before overwriting the flow images. */
    ff_vk_exec_add_dep_wait_sem(vkctx, exec, s->sem_interp, s->interp_value,
                                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    ff_vk_exec_add_dep_signal_sem(vkctx, exec, s->sem_interp, ++s->interp_value,
                                  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    /* Record this pair's value so the next occupant of the slot can fence on it. */
    fs->interp_done = s->interp_value;

    RET(ff_vk_exec_add_dep_frame(vkctx, exec, out,
                                 VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT));
    RET(ff_vk_exec_add_dep_frame(vkctx, exec, s->f0,
                                 VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT));
    RET(ff_vk_exec_add_dep_frame(vkctx, exec, s->f1,
                                 VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT));

    RET(ff_vk_create_imageviews(vkctx, exec, f0_views,  s->f0, FF_VK_REP_FLOAT));
    RET(ff_vk_create_imageviews(vkctx, exec, f1_views,  s->f1, FF_VK_REP_FLOAT));
    RET(ff_vk_create_imageviews(vkctx, exec, out_views, out,   FF_VK_REP_FLOAT));

    ff_vk_shader_update_img_array(vkctx, exec, &s->interpolate, s->f0, f0_views,
                                  0, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                  s->sampler);
    ff_vk_shader_update_img_array(vkctx, exec, &s->interpolate, s->f1, f1_views,
                                  0, 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                  s->sampler);
    ff_vk_shader_update_img_array(vkctx, exec, &s->interpolate, out, out_views,
                                  0, 2, VK_IMAGE_LAYOUT_GENERAL, VK_NULL_HANDLE);
    ff_vk_shader_update_img(vkctx, exec, &s->interpolate, 0, 3, 0,
                            fs->flow_sint_view[0], VK_IMAGE_LAYOUT_GENERAL, s->flow_sampler);
    ff_vk_shader_update_img(vkctx, exec, &s->interpolate, 0, 4, 0,
                            fs->flow_sint_view[1], VK_IMAGE_LAYOUT_GENERAL, s->flow_sampler);

    ff_vk_exec_bind_shader(vkctx, exec, &s->interpolate);
    ff_vk_shader_update_push_const(vkctx, exec, &s->interpolate,
                                   VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pd), &pd);

    nb_img_bar = 0;
    ff_vk_frame_barrier(vkctx, exec, out, img_bar, &nb_img_bar,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_SHADER_WRITE_BIT,
                        VK_IMAGE_LAYOUT_GENERAL,
                        VK_QUEUE_FAMILY_IGNORED);
    ff_vk_frame_barrier(vkctx, exec, s->f0, img_bar, &nb_img_bar,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_SHADER_READ_BIT,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_QUEUE_FAMILY_IGNORED);
    ff_vk_frame_barrier(vkctx, exec, s->f1, img_bar, &nb_img_bar,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_SHADER_READ_BIT,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_QUEUE_FAMILY_IGNORED);
    of_image_barrier(&img_bar[nb_img_bar++], fs->flow_img[0],
                     VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0,
                     VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
    of_image_barrier(&img_bar[nb_img_bar++], fs->flow_img[1],
                     VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0,
                     VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);

    vk->CmdPipelineBarrier2(exec->buf, &(VkDependencyInfo) {
        .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pImageMemoryBarriers    = img_bar,
        .imageMemoryBarrierCount = nb_img_bar,
    });

    vk->CmdDispatch(exec->buf,
                    FFALIGN(s->width,  s->interpolate.lg_size[0]) / s->interpolate.lg_size[0],
                    FFALIGN(s->height, s->interpolate.lg_size[1]) / s->interpolate.lg_size[1],
                    1);

    return ff_vk_exec_submit(vkctx, exec);

fail:
    ff_vk_exec_discard(vkctx, exec);
    return err;
}

static int copy_frame(AVFilterContext *avctx, AVFrame *out, AVFrame *src)
{
    int err;
    FRUCVulkanContext *s = avctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;
    FFVulkanFunctions *vk = &vkctx->vkfn;
    FFVkExecContext *exec;
    AVVkFrame *src_vk = (AVVkFrame *)src->data[0];
    AVVkFrame *out_vk = (AVVkFrame *)out->data[0];
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(vkctx->output_format);
    const int nb_planes = av_pix_fmt_count_planes(vkctx->output_format);
    const int src_nb_images = ff_vk_count_images(src_vk);
    const int out_nb_images = ff_vk_count_images(out_vk);
    /* src and out each contribute one barrier per VkImage; a multi-image
     * sw_format such as planar RGB or a separate alpha plane has several. */
    VkImageMemoryBarrier2 img_bar[2 * AV_NUM_DATA_POINTERS];
    int nb_img_bar = 0;

    exec = ff_vk_exec_get(vkctx, &s->e);
    err = ff_vk_exec_start(vkctx, exec);
    if (err < 0)
        return err;

    RET(ff_vk_exec_add_dep_frame(vkctx, exec, src,
                                 VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                 VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT));
    RET(ff_vk_exec_add_dep_frame(vkctx, exec, out,
                                 VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                 VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT));

    ff_vk_frame_barrier(vkctx, exec, src, img_bar, &nb_img_bar,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        VK_PIPELINE_STAGE_2_COPY_BIT,
                        VK_ACCESS_2_TRANSFER_READ_BIT,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_QUEUE_FAMILY_IGNORED);
    ff_vk_frame_barrier(vkctx, exec, out, img_bar, &nb_img_bar,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        VK_PIPELINE_STAGE_2_COPY_BIT,
                        VK_ACCESS_2_TRANSFER_WRITE_BIT,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_QUEUE_FAMILY_IGNORED);

    vk->CmdPipelineBarrier2(exec->buf, &(VkDependencyInfo) {
        .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pImageMemoryBarriers    = img_bar,
        .imageMemoryBarrierCount = nb_img_bar,
    });

    for (int i = 0; i < nb_planes; i++) {
        uint32_t w, h;
        plane_wh(desc, s->width, s->height, i, &w, &h);
        VkImageCopy region = {
            .srcSubresource = { .aspectMask = ff_vk_aspect_flag(src, i), .layerCount = 1 },
            .dstSubresource = { .aspectMask = ff_vk_aspect_flag(out, i), .layerCount = 1 },
            .extent         = { w, h, 1 },
        };
        vk->CmdCopyImage(exec->buf,
                         src_vk->img[FFMIN(i, src_nb_images - 1)],
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         out_vk->img[FFMIN(i, out_nb_images - 1)],
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         1, &region);
    }

    return ff_vk_exec_submit(vkctx, exec);

fail:
    ff_vk_exec_discard(vkctx, exec);
    return err;
}

/* Emit src unchanged. The frame must be copied rather than cloned: a clone keeps
 * the input's frames context, and the frame has to match the output context the
 * downstream link is configured for. */
static int passthrough_frame(AVFilterContext *ctx, AVFrame **work, AVFrame *src)
{
    AVFilterLink *outlink = ctx->outputs[0];
    int ret;

    *work = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!*work)
        return AVERROR(ENOMEM);
    ret = av_frame_copy_props(*work, src);
    if (ret < 0)
        goto fail;
    ret = copy_frame(ctx, *work, src);
    if (ret < 0)
        goto fail;
    return 0;
fail:
    av_frame_free(work);
    return ret;
}

static int process_work_frame(AVFilterContext *ctx)
{
    FRUCVulkanContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    int64_t work_pts;
    int64_t interpolate8;
    int ret;

    if (!s->f1)
        return 0;
    if (!s->f0 && !s->flush)
        return 0;

    work_pts = s->start_pts + av_rescale_q(s->n, av_inv_q(s->dest_frame_rate),
                                           s->dest_time_base);

    if (work_pts >= s->pts1 && !s->flush)
        return 0;

    if (!s->f0) {
        av_assert1(s->flush);
        ret = passthrough_frame(ctx, &s->work, s->f1);
        if (ret < 0)
            return ret;
        /* We are flushing, so f1 is not needed once passed through. Free it so
         * the flush terminates instead of re-emitting it every activation. */
        av_frame_free(&s->f1);
    } else {
        if (work_pts >= s->pts1 + s->delta && s->flush)
            return 0;

        interpolate8 = av_rescale(work_pts - s->pts0, 256, s->delta);
        if (interpolate8 >= 256) {
            ret = passthrough_frame(ctx, &s->work, s->f1);
            if (ret < 0)
                return ret;
        } else if (interpolate8 <= 0) {
            ret = passthrough_frame(ctx, &s->work, s->f0);
            if (ret < 0)
                return ret;
        } else {
            float t = (float)(work_pts - s->pts0) / (float)s->delta;
            s->work = ff_get_video_buffer(outlink, outlink->w, outlink->h);
            if (!s->work)
                return AVERROR(ENOMEM);
            ret = av_frame_copy_props(s->work, s->f0);
            if (ret < 0) {
                av_frame_free(&s->work);
                return ret;
            }
            ret = interpolate_frame(ctx, s->work, t);
            if (ret < 0) {
                av_frame_free(&s->work);
                return ret;
            }
        }
    }

    s->work->pts = work_pts;
    s->n++;

    return 1;
}

static int activate(AVFilterContext *ctx)
{
    int ret, status;
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    FRUCVulkanContext *s = ctx->priv;
    AVFrame *inpicref;
    int64_t pts;

    FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink);

retry:
    ret = process_work_frame(ctx);
    if (ret < 0)
        return ret;
    else if (ret == 1)
        return ff_filter_frame(outlink, s->work);

    ret = ff_inlink_consume_frame(inlink, &inpicref);
    if (ret < 0)
        return ret;

    if (inpicref) {
        if (inpicref->flags & AV_FRAME_FLAG_INTERLACED)
            av_log(ctx, AV_LOG_WARNING, "Interlaced frame found - the output will not be correct.\n");

        if (inpicref->pts == AV_NOPTS_VALUE) {
            av_log(ctx, AV_LOG_WARNING, "Ignoring frame without PTS.\n");
            av_frame_free(&inpicref);
        }
    }

    if (inpicref) {
        pts = av_rescale_q(inpicref->pts, s->srce_time_base, s->dest_time_base);

        if (s->f1 && pts == s->pts1) {
            av_log(ctx, AV_LOG_WARNING, "Ignoring frame with same PTS.\n");
            av_frame_free(&inpicref);
        }
    }

    if (inpicref) {
        av_frame_free(&s->f0);
        s->f0 = s->f1;
        s->pts0 = s->pts1;
        s->f1 = inpicref;
        s->pts1 = pts;
        s->delta = s->pts1 - s->pts0;
        s->flow_valid = 0;

        if (s->delta < 0) {
            av_log(ctx, AV_LOG_WARNING, "PTS discontinuity.\n");
            s->start_pts = s->pts1;
            s->n = 0;
            av_frame_free(&s->f0);
        }

        if (s->start_pts == AV_NOPTS_VALUE)
            s->start_pts = s->pts1;

        goto retry;
    }

    if (ff_inlink_acknowledge_status(inlink, &status, &pts)) {
        if (!s->flush) {
            s->flush = 1;
            goto retry;
        }
        ff_outlink_set_status(outlink, status, pts);
        return 0;
    }

    FF_FILTER_FORWARD_WANTED(outlink, inlink);

    return FFERROR_NOT_READY;
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    FRUCVulkanContext *s = ctx->priv;

    s->srce_time_base = inlink->time_base;

    return ff_vk_filter_config_input(inlink);
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = ctx->inputs[0];
    FilterLink *il = ff_filter_link(inlink);
    FilterLink *ol = ff_filter_link(outlink);
    FRUCVulkanContext *s = ctx->priv;
    double var_values[VARS_NB], res;
    int err;
    int exact;

    ff_dlog(ctx, "config_output()\n");

    ff_dlog(ctx,
           "config_output() input time base:%u/%u (%f)\n",
           ctx->inputs[0]->time_base.num,ctx->inputs[0]->time_base.den,
           av_q2d(ctx->inputs[0]->time_base));

    // The fps option is an expression evaluated against the source frame rate
    var_values[VAR_SOURCE_FPS]    = av_q2d(il->frame_rate);
    err = av_expr_parse_and_eval(&res, s->requested_frame_rate,
                                 var_names, var_values,
                                 NULL, NULL, NULL, NULL, NULL, 0, ctx);
    if (err < 0)
        return err;

    s->dest_frame_rate = av_d2q(res, INT_MAX);
    if (s->dest_frame_rate.num <= 0 || s->dest_frame_rate.den <= 0) {
        av_log(ctx, AV_LOG_ERROR,
               "Invalid output frame rate '%s' (must evaluate to a positive value)\n",
               s->requested_frame_rate);
        return AVERROR(EINVAL);
    }

    // make sure timebase is small enough to hold the framerate

    exact = av_reduce(&s->dest_time_base.num, &s->dest_time_base.den,
                      av_gcd((int64_t)s->srce_time_base.num * s->dest_frame_rate.num,
                             (int64_t)s->srce_time_base.den * s->dest_frame_rate.den ),
                      (int64_t)s->srce_time_base.den * s->dest_frame_rate.num, INT_MAX);

    /* The source-timebase-derived reduction above can collapse to a zero time
     * base (av_reduce() bounds its result, so the numerator can underflow to
     * zero, leaving 0/1) when the source timebase shares no useful factors with
     * the requested rate, which would make the output timebase unusable. Fall
     * back to the plain 1/fps timebase in that case so a valid timebase is
     * always produced. */
    if (!s->dest_time_base.num || !s->dest_time_base.den) {
        exact = av_reduce(&s->dest_time_base.num, &s->dest_time_base.den,
                          s->dest_frame_rate.den, s->dest_frame_rate.num, INT_MAX);
    }

    av_log(ctx, AV_LOG_INFO,
           "time base:%u/%u -> %u/%u exact:%d\n",
           s->srce_time_base.num, s->srce_time_base.den,
           s->dest_time_base.num, s->dest_time_base.den, exact);
    if (!exact) {
        av_log(ctx, AV_LOG_WARNING, "Timebase conversion is not exact\n");
    }

    err = ff_vk_filter_config_output(outlink);
    if (err < 0)
        return err;

    ol->frame_rate = s->dest_frame_rate;
    outlink->time_base = s->dest_time_base;

    ff_dlog(ctx,
           "config_output() output time base:%u/%u (%f) w:%d h:%d\n",
           outlink->time_base.num, outlink->time_base.den,
           av_q2d(outlink->time_base),
           outlink->w, outlink->h);

    return init_filter(ctx);
}

static av_cold int init(AVFilterContext *avctx)
{
    FRUCVulkanContext *s = avctx->priv;

    s->start_pts = AV_NOPTS_VALUE;

    return ff_vk_filter_init(avctx);
}

static av_cold void uninit(AVFilterContext *avctx)
{
    FRUCVulkanContext *s = avctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;
    FFVulkanFunctions *vk = &vkctx->vkfn;

    /* Free the execution pools first: this waits for every submitted command
     * buffer to retire (ff_vk_exec_pool_free fences each context). The pooled
     * submissions wait on and signal the timeline semaphores and reference the
     * optical flow images below, so those objects must outlive the wait — destroying
     * an in-use semaphore would leave a submission's fence permanently unsignaled
     * and deadlock the wait. */
    ff_vk_exec_pool_free(vkctx, &s->e);
    ff_vk_exec_pool_free(vkctx, &s->e_of);

    if (s->sem_gray)
        vk->DestroySemaphore(vkctx->hwctx->act_dev, s->sem_gray, vkctx->hwctx->alloc);
    if (s->sem_flow)
        vk->DestroySemaphore(vkctx->hwctx->act_dev, s->sem_flow, vkctx->hwctx->alloc);
    if (s->sem_interp)
        vk->DestroySemaphore(vkctx->hwctx->act_dev, s->sem_interp, vkctx->hwctx->alloc);
    for (int slot = 0; slot < FRUC_NB_SLOTS; slot++) {
        FRUCFlowSlot *fs = &s->slots[slot];
        if (fs->session)
            vk->DestroyOpticalFlowSessionNV(vkctx->hwctx->act_dev, fs->session,
                                            vkctx->hwctx->alloc);
        for (int i = 0; i < 2; i++) {
            if (fs->gray_view[i])
                vk->DestroyImageView(vkctx->hwctx->act_dev, fs->gray_view[i], vkctx->hwctx->alloc);
            ff_vk_image_free(vkctx, &fs->gray_img[i], &fs->gray_mem[i]);
            if (fs->flow_view[i])
                vk->DestroyImageView(vkctx->hwctx->act_dev, fs->flow_view[i], vkctx->hwctx->alloc);
            if (fs->flow_sint_view[i])
                vk->DestroyImageView(vkctx->hwctx->act_dev, fs->flow_sint_view[i], vkctx->hwctx->alloc);
            ff_vk_image_free(vkctx, &fs->flow_img[i], &fs->flow_mem[i]);
        }
    }

    ff_vk_shader_free(vkctx, &s->grayscale);
    ff_vk_shader_free(vkctx, &s->interpolate);

    if (s->sampler)
        vk->DestroySampler(vkctx->hwctx->act_dev, s->sampler, vkctx->hwctx->alloc);
    if (s->flow_sampler)
        vk->DestroySampler(vkctx->hwctx->act_dev, s->flow_sampler, vkctx->hwctx->alloc);

    ff_vk_uninit(vkctx);

    av_frame_free(&s->f0);
    av_frame_free(&s->f1);
}

static const AVFilterPad fruc_vulkan_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
    },
};

static const AVFilterPad fruc_vulkan_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
};

const FFFilter ff_vf_fruc_vulkan = {
    .p.name        = "fruc_vulkan",
    .p.description = NULL_IF_CONFIG_SMALL("Frame rate up-conversion using the Vulkan NV optical flow extension"),
    .p.priv_class  = &fruc_vulkan_class,
    .p.flags       = AVFILTER_FLAG_HWDEVICE,
    .priv_size     = sizeof(FRUCVulkanContext),
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(fruc_vulkan_inputs),
    FILTER_OUTPUTS(fruc_vulkan_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_VULKAN),
    .activate      = activate,
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
