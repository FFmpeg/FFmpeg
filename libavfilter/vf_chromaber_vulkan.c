/*
 * Copyright (c) Lynne
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

#include "libavutil/random_seed.h"
#include "libavutil/opt.h"
#include "vulkan_filter.h"

#include "filters.h"
#include "video.h"

extern const unsigned char ff_chromaber_comp_spv_data[];
extern const unsigned int ff_chromaber_comp_spv_len;

typedef struct ChromaticAberrationVulkanContext {
    FFVulkanContext vkctx;

    int initialized;
    FFVkExecPool e;
    AVVulkanDeviceQueueFamily *qf;
    FFVulkanShader shd;
    VkSampler sampler;

    /* Push constants / options */
    struct {
        float dist[2];
        uint32_t single_plane;
    } opts;
} ChromaticAberrationVulkanContext;

static av_cold int init_filter(AVFilterContext *ctx, AVFrame *in)
{
    int err;
    ChromaticAberrationVulkanContext *s = ctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;
    const int planes = av_pix_fmt_count_planes(s->vkctx.output_format);
    FFVulkanShader *shd = &s->shd;

    /* Normalize options */
    s->opts.dist[0] = (s->opts.dist[0] / 100.0f) + 1.0f;
    s->opts.dist[1] = (s->opts.dist[1] / 100.0f) + 1.0f;

    s->qf = ff_vk_qf_find(vkctx, VK_QUEUE_COMPUTE_BIT, 0);
    if (!s->qf) {
        av_log(ctx, AV_LOG_ERROR, "Device has no compute queues\n");
        err = AVERROR(ENOTSUP);
        goto fail;
    }

    RET(ff_vk_exec_pool_init(vkctx, s->qf, &s->e, s->qf->num*4, 0, 0, 0, NULL));
    RET(ff_vk_init_sampler(vkctx, &s->sampler, 0, VK_FILTER_LINEAR));

    ff_vk_shader_load(&s->shd, VK_SHADER_STAGE_COMPUTE_BIT, NULL,
                      (uint32_t []) { 32, 32, 1 }, 0);

    ff_vk_shader_add_push_const(&s->shd, 0, sizeof(s->opts),
                                VK_SHADER_STAGE_COMPUTE_BIT);

    const FFVulkanDescriptorSetBinding desc[] = {
        { /* input_img */
            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
            .elems      = planes,
        },
        { /* output_img */
            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
            .elems      = planes,
        },
    };
    ff_vk_shader_add_descriptor_set(vkctx, &s->shd, desc, 2, 0, 0);

    RET(ff_vk_shader_link(vkctx, shd,
                          ff_chromaber_comp_spv_data,
                          ff_chromaber_comp_spv_len, "main"));

    RET(ff_vk_shader_register_exec(vkctx, &s->e, &s->shd));

    s->opts.single_plane = planes == 1;
    s->initialized = 1;

fail:
    return err;
}

static int chromaber_vulkan_filter_frame(AVFilterLink *link, AVFrame *in)
{
    int err;
    AVFilterContext *ctx = link->dst;
    ChromaticAberrationVulkanContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];

    AVFrame *out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    if (!s->initialized)
        RET(init_filter(ctx, in));

    RET(ff_vk_filter_process_simple(&s->vkctx, &s->e, &s->shd, out, in,
                                    s->sampler, 1, &s->opts, sizeof(s->opts)));

    err = av_frame_copy_props(out, in);
    if (err < 0)
        goto fail;

    av_frame_free(&in);

    return ff_filter_frame(outlink, out);

fail:
    av_frame_free(&in);
    av_frame_free(&out);
    return err;
}

static void chromaber_vulkan_uninit(AVFilterContext *avctx)
{
    ChromaticAberrationVulkanContext *s = avctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;
    FFVulkanFunctions *vk = &vkctx->vkfn;

    ff_vk_exec_pool_free(vkctx, &s->e);
    ff_vk_shader_free(vkctx, &s->shd);

    if (s->sampler)
        vk->DestroySampler(vkctx->hwctx->act_dev, s->sampler,
                           vkctx->hwctx->alloc);

    ff_vk_uninit(&s->vkctx);

    s->initialized = 0;
}

#define OFFSET(x) offsetof(ChromaticAberrationVulkanContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)
static const AVOption chromaber_vulkan_options[] = {
    { "dist_x", "Set horizontal distortion amount", OFFSET(opts.dist[0]), AV_OPT_TYPE_FLOAT, {.dbl = 0.0f}, -10.0f, 10.0f, .flags = FLAGS },
    { "dist_y", "Set vertical distortion amount",   OFFSET(opts.dist[1]), AV_OPT_TYPE_FLOAT, {.dbl = 0.0f}, -10.0f, 10.0f, .flags = FLAGS },
    { NULL },
};

AVFILTER_DEFINE_CLASS(chromaber_vulkan);

static const AVFilterPad chromaber_vulkan_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = &chromaber_vulkan_filter_frame,
        .config_props = &ff_vk_filter_config_input,
    },
};

static const AVFilterPad chromaber_vulkan_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = &ff_vk_filter_config_output,
    },
};

const FFFilter ff_vf_chromaber_vulkan = {
    .p.name         = "chromaber_vulkan",
    .p.description  = NULL_IF_CONFIG_SMALL("Offset chroma of input video (chromatic aberration)"),
    .p.priv_class   = &chromaber_vulkan_class,
    .p.flags        = AVFILTER_FLAG_HWDEVICE,
    .priv_size      = sizeof(ChromaticAberrationVulkanContext),
    .init           = &ff_vk_filter_init,
    .uninit         = &chromaber_vulkan_uninit,
    FILTER_INPUTS(chromaber_vulkan_inputs),
    FILTER_OUTPUTS(chromaber_vulkan_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_VULKAN),
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
