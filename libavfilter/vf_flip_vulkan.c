/*
 * copyright (c) 2021 Wu Jianhua <jianhua.wu@intel.com>
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
#include "libavutil/vulkan_spirv.h"
#include "vulkan_filter.h"

#include "filters.h"
#include "video.h"

enum FlipType {
    FLIP_VERTICAL,
    FLIP_HORIZONTAL,
    FLIP_BOTH
};

typedef struct FlipVulkanContext {
    FFVulkanContext vkctx;

    int initialized;
    FFVkExecPool e;
    FFVkQueueFamilyCtx qf;
    FFVulkanShader shd;
    VkSampler sampler;
} FlipVulkanContext;

static av_cold int init_filter(AVFilterContext *ctx, AVFrame *in, enum FlipType type)
{
    int err = 0;
    uint8_t *spv_data;
    size_t spv_len;
    void *spv_opaque = NULL;
    FlipVulkanContext *s = ctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;
    const int planes = av_pix_fmt_count_planes(s->vkctx.output_format);
    FFVulkanShader *shd = &s->shd;
    FFVkSPIRVCompiler *spv;
    FFVulkanDescriptorSetBinding *desc;

    spv = ff_vk_spirv_init();
    if (!spv) {
        av_log(ctx, AV_LOG_ERROR, "Unable to initialize SPIR-V compiler!\n");
        return AVERROR_EXTERNAL;
    }

    ff_vk_qf_init(vkctx, &s->qf, VK_QUEUE_COMPUTE_BIT);
    RET(ff_vk_exec_pool_init(vkctx, &s->qf, &s->e, s->qf.nb_queues*4, 0, 0, 0, NULL));
    RET(ff_vk_init_sampler(vkctx, &s->sampler, 1, VK_FILTER_LINEAR));
    RET(ff_vk_shader_init(vkctx, &s->shd, "flip",
                          VK_SHADER_STAGE_COMPUTE_BIT,
                          NULL, 0,
                          32, 32, 1,
                          0));

    desc = (FFVulkanDescriptorSetBinding []) {
        {
            .name       = "input_image",
            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .dimensions = 2,
            .elems      = planes,
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
            .samplers   = DUP_SAMPLER(s->sampler),
        },
        {
            .name       = "output_image",
            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .mem_layout = ff_vk_shader_rep_fmt(s->vkctx.output_format, FF_VK_REP_FLOAT),
            .mem_quali  = "writeonly",
            .dimensions = 2,
            .elems      = planes,
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };

    RET(ff_vk_shader_add_descriptor_set(vkctx, &s->shd, desc, 2, 0, 0));

    GLSLC(0, void main()                                                                    );
    GLSLC(0, {                                                                              );
    GLSLC(1,     ivec2 size;                                                                );
    GLSLC(1,     const ivec2 pos = ivec2(gl_GlobalInvocationID.xy);                         );
    for (int i = 0; i < planes; i++) {
        GLSLC(0,                                                                            );
        GLSLF(1, size = imageSize(output_image[%i]);                                      ,i);
        GLSLC(1, if (IS_WITHIN(pos, size)) {                                                );
        switch (type)
        {
        case FLIP_HORIZONTAL:
            GLSLF(2, vec4 res = texture(input_image[%i], ivec2(size.x - pos.x, pos.y));   ,i);
            break;
        case FLIP_VERTICAL:
            GLSLF(2, vec4 res = texture(input_image[%i], ivec2(pos.x, size.y - pos.y));   ,i);
            break;
        case FLIP_BOTH:
            GLSLF(2, vec4 res = texture(input_image[%i], ivec2(size.xy - pos.xy));,         i);
            break;
        default:
            GLSLF(2, vec4 res = texture(input_image[%i], pos);                            ,i);
            break;
        }
        GLSLF(2,     imageStore(output_image[%i], pos, res);                              ,i);
        GLSLC(1, }                                                                          );
    }
    GLSLC(0, }                                                                              );

    RET(spv->compile_shader(vkctx, spv, shd, &spv_data, &spv_len, "main",
                            &spv_opaque));
    RET(ff_vk_shader_link(vkctx, shd, spv_data, spv_len, "main"));

    RET(ff_vk_shader_register_exec(vkctx, &s->e, &s->shd));

    s->initialized = 1;

fail:
    if (spv_opaque)
        spv->free_shader(spv, &spv_opaque);
    if (spv)
        spv->uninit(&spv);

    return err;
}

static av_cold void flip_vulkan_uninit(AVFilterContext *avctx)
{
    FlipVulkanContext *s = avctx->priv;

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

static int filter_frame(AVFilterLink *link, AVFrame *in, enum FlipType type)
{
    int err;
    AVFrame *out = NULL;
    AVFilterContext *ctx = link->dst;
    FlipVulkanContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    if (!s->initialized)
        RET(init_filter(ctx, in, type));

    RET(ff_vk_filter_process_simple(&s->vkctx, &s->e, &s->shd, out, in,
                                    s->sampler, NULL, 0));

    RET(av_frame_copy_props(out, in));

    av_frame_free(&in);

    return ff_filter_frame(outlink, out);

fail:
    av_frame_free(&in);
    av_frame_free(&out);
    return err;
}

static int hflip_vulkan_filter_frame(AVFilterLink *link, AVFrame *in)
{
    return filter_frame(link, in, FLIP_HORIZONTAL);
}

static int vflip_vulkan_filter_frame(AVFilterLink *link, AVFrame *in)
{
    return filter_frame(link, in, FLIP_VERTICAL);
}

static int flip_vulkan_filter_frame(AVFilterLink *link, AVFrame *in)
{
    return filter_frame(link, in, FLIP_BOTH);
}

static const AVFilterPad flip_vulkan_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = &ff_vk_filter_config_output,
    }
};

static const AVOption hflip_vulkan_options[] = {
    { NULL },
};

AVFILTER_DEFINE_CLASS(hflip_vulkan);

static const AVFilterPad hflip_vulkan_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = &hflip_vulkan_filter_frame,
        .config_props = &ff_vk_filter_config_input,
    }
};

const AVFilter ff_vf_hflip_vulkan = {
    .name           = "hflip_vulkan",
    .description    = NULL_IF_CONFIG_SMALL("Horizontally flip the input video in Vulkan"),
    .priv_size      = sizeof(FlipVulkanContext),
    .init           = &ff_vk_filter_init,
    .uninit         = &flip_vulkan_uninit,
    FILTER_INPUTS(hflip_vulkan_inputs),
    FILTER_OUTPUTS(flip_vulkan_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_VULKAN),
    .priv_class     = &hflip_vulkan_class,
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};

static const AVOption vflip_vulkan_options[] = {
    { NULL },
};

AVFILTER_DEFINE_CLASS(vflip_vulkan);

static const AVFilterPad vflip_vulkan_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = &vflip_vulkan_filter_frame,
        .config_props = &ff_vk_filter_config_input,
    }
};

const AVFilter ff_vf_vflip_vulkan = {
    .name           = "vflip_vulkan",
    .description    = NULL_IF_CONFIG_SMALL("Vertically flip the input video in Vulkan"),
    .priv_size      = sizeof(FlipVulkanContext),
    .init           = &ff_vk_filter_init,
    .uninit         = &flip_vulkan_uninit,
    FILTER_INPUTS(vflip_vulkan_inputs),
    FILTER_OUTPUTS(flip_vulkan_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_VULKAN),
    .priv_class     = &vflip_vulkan_class,
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};

static const AVOption flip_vulkan_options[] = {
    { NULL },
};

AVFILTER_DEFINE_CLASS(flip_vulkan);

static const AVFilterPad flip_vulkan_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = &flip_vulkan_filter_frame,
        .config_props = &ff_vk_filter_config_input,
    }
};

const AVFilter ff_vf_flip_vulkan = {
    .name           = "flip_vulkan",
    .description    = NULL_IF_CONFIG_SMALL("Flip both horizontally and vertically"),
    .priv_size      = sizeof(FlipVulkanContext),
    .init           = &ff_vk_filter_init,
    .uninit         = &flip_vulkan_uninit,
    FILTER_INPUTS(flip_vulkan_inputs),
    FILTER_OUTPUTS(flip_vulkan_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_VULKAN),
    .priv_class     = &flip_vulkan_class,
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
    .flags          = AVFILTER_FLAG_HWDEVICE,
};
