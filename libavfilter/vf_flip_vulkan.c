/*
 * copyright (c) 2021 Wu Jianhua <jianhua.wu@intel.com>
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
#include "internal.h"

#define CGS 32

enum FlipType {
    FLIP_VERTICAL,
    FLIP_HORIZONTAL,
    FLIP_BOTH
};

typedef struct FlipVulkanContext {
    FFVulkanContext vkctx;
    FFVkQueueFamilyCtx qf;
    FFVkExecContext *exec;
    FFVulkanPipeline *pl;

    VkDescriptorImageInfo input_images[3];
    VkDescriptorImageInfo output_images[3];

    int initialized;
} FlipVulkanContext;

static av_cold int init_filter(AVFilterContext *ctx, AVFrame *in, enum FlipType type)
{
    int err = 0;
    FFVkSPIRVShader *shd;
    FlipVulkanContext *s = ctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;
    const int planes = av_pix_fmt_count_planes(s->vkctx.output_format);

    FFVulkanDescriptorSetBinding image_descs[] = {
        {
            .name       = "input_image",
            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .dimensions = 2,
            .elems      = planes,
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
            .updater    = s->input_images,
        },
        {
            .name       = "output_image",
            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .mem_layout = ff_vk_shader_rep_fmt(s->vkctx.output_format),
            .mem_quali  = "writeonly",
            .dimensions = 2,
            .elems      = planes,
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
            .updater    = s->output_images,
        },
    };

    image_descs[0].sampler = ff_vk_init_sampler(vkctx, 1, VK_FILTER_LINEAR);
    if (!image_descs[0].sampler)
            return AVERROR_EXTERNAL;

    ff_vk_qf_init(vkctx, &s->qf, VK_QUEUE_COMPUTE_BIT, 0);

    {
        s->pl = ff_vk_create_pipeline(vkctx, &s->qf);
        if (!s->pl)
            return AVERROR(ENOMEM);

        shd = ff_vk_init_shader(s->pl, "flip_compute", image_descs[0].stages);
        if (!shd)
            return AVERROR(ENOMEM);

        ff_vk_set_compute_shader_sizes(shd, (int [3]){ CGS, 1, 1 });
        RET(ff_vk_add_descriptor_set(vkctx, s->pl, shd, image_descs, FF_ARRAY_ELEMS(image_descs), 0));

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

        RET(ff_vk_compile_shader(vkctx, shd, "main"));
        RET(ff_vk_init_pipeline_layout(vkctx, s->pl));
        RET(ff_vk_init_compute_pipeline(vkctx, s->pl));
    }

    RET(ff_vk_create_exec_ctx(vkctx, &s->exec, &s->qf));
    s->initialized = 1;

fail:
    return err;
}

static av_cold void flip_vulkan_uninit(AVFilterContext *avctx)
{
    FlipVulkanContext *s = avctx->priv;
    ff_vk_uninit(&s->vkctx);

    s->initialized = 0;
}

static int process_frames(AVFilterContext *avctx, AVFrame *outframe, AVFrame *inframe)
{
    int err = 0;
    VkCommandBuffer cmd_buf;
    FlipVulkanContext *s = avctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;
    FFVulkanFunctions *vk = &s->vkctx.vkfn;
    AVVkFrame *in = (AVVkFrame *)inframe->data[0];
    AVVkFrame *out = (AVVkFrame *)outframe->data[0];
    const int planes = av_pix_fmt_count_planes(s->vkctx.output_format);
    const VkFormat *input_formats = av_vkfmt_from_pixfmt(s->vkctx.input_format);
    const VkFormat *output_formats = av_vkfmt_from_pixfmt(s->vkctx.output_format);

    ff_vk_start_exec_recording(vkctx, s->exec);
    cmd_buf = ff_vk_get_exec_buf(s->exec);

    for (int i = 0; i < planes; i++) {
        RET(ff_vk_create_imageview(vkctx, s->exec,
                                   &s->input_images[i].imageView, in->img[i],
                                   input_formats[i],
                                   ff_comp_identity_map));

        RET(ff_vk_create_imageview(vkctx, s->exec,
                                   &s->output_images[i].imageView, out->img[i],
                                   output_formats[i],
                                   ff_comp_identity_map));

        s->input_images[i].imageLayout  = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        s->output_images[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    }

    ff_vk_update_descriptor_set(vkctx, s->pl, 0);

    for (int i = 0; i < planes; i++) {
        VkImageMemoryBarrier barriers[] = {
            {
                .sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask               = 0,
                .dstAccessMask               = VK_ACCESS_SHADER_READ_BIT,
                .oldLayout                   = in->layout[i],
                .newLayout                   = s->input_images[i].imageLayout,
                .srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED,
                .image                       = in->img[i],
                .subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .subresourceRange.levelCount = 1,
                .subresourceRange.layerCount = 1,
            },
            {
                .sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask               = 0,
                .dstAccessMask               = VK_ACCESS_SHADER_WRITE_BIT,
                .oldLayout                   = out->layout[i],
                .newLayout                   = s->output_images[i].imageLayout,
                .srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED,
                .image                       = out->img[i],
                .subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .subresourceRange.levelCount = 1,
                .subresourceRange.layerCount = 1,
            },
        };

        vk->CmdPipelineBarrier(cmd_buf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                               0, NULL, 0, NULL, FF_ARRAY_ELEMS(barriers), barriers);

        in->layout[i]  = barriers[0].newLayout;
        in->access[i]  = barriers[0].dstAccessMask;

        out->layout[i] = barriers[1].newLayout;
        out->access[i] = barriers[1].dstAccessMask;
    }

    ff_vk_bind_pipeline_exec(vkctx, s->exec, s->pl);
    vk->CmdDispatch(cmd_buf, FFALIGN(s->vkctx.output_width, CGS)/CGS,
                    s->vkctx.output_height, 1);

    ff_vk_add_exec_dep(vkctx, s->exec, inframe, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    ff_vk_add_exec_dep(vkctx, s->exec, outframe, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);

    err = ff_vk_submit_exec_queue(vkctx, s->exec);
    if (err)
        return err;

    ff_vk_qf_rotate(&s->qf);

    return 0;
fail:
    ff_vk_discard_exec_deps(s->exec);
    return err;
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

    RET(process_frames(ctx, out, in));

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
};
