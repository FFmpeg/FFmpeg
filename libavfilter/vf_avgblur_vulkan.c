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

#include "libavutil/opt.h"
#include "vulkan.h"
#include "internal.h"

#define CGS 32

typedef struct AvgBlurVulkanContext {
    VulkanFilterContext vkctx;

    int initialized;
    FFVkExecContext *exec;
    VulkanPipeline *pl_hor;
    VulkanPipeline *pl_ver;

    /* Shader updators, must be in the main filter struct */
    VkDescriptorImageInfo input_images[3];
    VkDescriptorImageInfo tmp_images[3];
    VkDescriptorImageInfo output_images[3];

    int size_x;
    int size_y;
    int planes;
} AvgBlurVulkanContext;

static const char blur_kernel[] = {
    C(0, shared vec4 cache[DIR(gl_WorkGroupSize) + FILTER_RADIUS*2 + 1];           )
    C(0,                                                                           )
    C(0, void distort(const ivec2 pos, const int idx)                              )
    C(0, {                                                                         )
    C(1,     const uint cp = DIR(gl_LocalInvocationID) + FILTER_RADIUS;            )
    C(0,                                                                           )
    C(1,     cache[cp] = texture(input_img[idx], pos);                             )
    C(0,                                                                           )
    C(1,     const ivec2 loc_l = pos - INC(FILTER_RADIUS);                         )
    C(1,     cache[cp - FILTER_RADIUS] = texture(input_img[idx], loc_l);           )
    C(0,                                                                           )
    C(1,     const ivec2 loc_h = pos + INC(DIR(gl_WorkGroupSize));                 )
    C(1,     cache[cp + DIR(gl_WorkGroupSize)] = texture(input_img[idx], loc_h);   )
    C(0,                                                                           )
    C(1,     barrier();                                                            )
    C(0,                                                                           )
    C(1,     vec4 sum = vec4(0);                                                   )
    C(1,     for (int p = -FILTER_RADIUS; p <= FILTER_RADIUS; p++)                 )
    C(2,         sum += cache[cp + p];                                             )
    C(0,                                                                           )
    C(1,     sum /= vec4(FILTER_RADIUS*2 + 1);                                     )
    C(1,     imageStore(output_img[idx], pos, sum);                                )
    C(0, }                                                                         )
};

static av_cold int init_filter(AVFilterContext *ctx, AVFrame *in)
{
    int err;
    SPIRVShader *shd;
    AvgBlurVulkanContext *s = ctx->priv;
    const int planes = av_pix_fmt_count_planes(s->vkctx.output_format);
    VkSampler *sampler = ff_vk_init_sampler(ctx, 1, VK_FILTER_LINEAR);

    VulkanDescriptorSetBinding desc_i[2] = {
        {
            .name       = "input_img",
            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .dimensions = 2,
            .elems      = planes,
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
            .samplers   = DUP_SAMPLER_ARRAY4(*sampler),
        },
        {
            .name       = "output_img",
            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .mem_layout = ff_vk_shader_rep_fmt(s->vkctx.output_format),
            .mem_quali  = "writeonly",
            .dimensions = 2,
            .elems      = planes,
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };

    if (!sampler)
        return AVERROR_EXTERNAL;

    { /* Create shader for the horizontal pass */
        desc_i[0].updater = s->input_images;
        desc_i[1].updater = s->tmp_images;

        s->pl_hor = ff_vk_create_pipeline(ctx);
        if (!s->pl_hor)
            return AVERROR(ENOMEM);

        shd = ff_vk_init_shader(ctx, s->pl_hor, "avgblur_compute_hor",
                                VK_SHADER_STAGE_COMPUTE_BIT);

        ff_vk_set_compute_shader_sizes(ctx, shd, (int [3]){ CGS, 1, 1 });

        RET(ff_vk_add_descriptor_set(ctx, s->pl_hor, shd, desc_i, 2, 0));

        GLSLF(0, #define FILTER_RADIUS (%i)                     ,s->size_x - 1);
        GLSLC(0, #define INC(x) (ivec2(x, 0))                                 );
        GLSLC(0, #define DIR(var) (var.x)                                     );
        GLSLD(   blur_kernel                                                  );
        GLSLC(0, void main()                                                  );
        GLSLC(0, {                                                            );
        GLSLC(1,     ivec2 size;                                              );
        GLSLC(1,     const ivec2 pos = ivec2(gl_GlobalInvocationID.xy);       );
        for (int i = 0; i < planes; i++) {
            GLSLC(0,                                                          );
            GLSLF(1,  size = imageSize(output_img[%i]);                     ,i);
            GLSLC(1,  if (IS_WITHIN(pos, size)) {                             );
            if (s->planes & (1 << i)) {
                GLSLF(2, distort(pos, %i);                                  ,i);
            } else {
                GLSLF(2, vec4 res = texture(input_img[%i], pos);            ,i);
                GLSLF(2, imageStore(output_img[%i], pos, res);              ,i);
            }
            GLSLC(1, }                                                        );
        }
        GLSLC(0, }                                                            );

        RET(ff_vk_compile_shader(ctx, shd, "main"));

        RET(ff_vk_init_pipeline_layout(ctx, s->pl_hor));
        RET(ff_vk_init_compute_pipeline(ctx, s->pl_hor));
    }

    { /* Create shader for the vertical pass */
        desc_i[0].updater = s->tmp_images;
        desc_i[1].updater = s->output_images;

        s->pl_ver = ff_vk_create_pipeline(ctx);
        if (!s->pl_ver)
            return AVERROR(ENOMEM);

        shd = ff_vk_init_shader(ctx, s->pl_ver, "avgblur_compute_ver",
                                VK_SHADER_STAGE_COMPUTE_BIT);

        ff_vk_set_compute_shader_sizes(ctx, shd, (int [3]){ 1, CGS, 1 });

        RET(ff_vk_add_descriptor_set(ctx, s->pl_ver, shd, desc_i, 2, 0));

        GLSLF(0, #define FILTER_RADIUS (%i)                     ,s->size_y - 1);
        GLSLC(0, #define INC(x) (ivec2(0, x))                                 );
        GLSLC(0, #define DIR(var) (var.y)                                     );
        GLSLD(   blur_kernel                                                  );
        GLSLC(0, void main()                                                  );
        GLSLC(0, {                                                            );
        GLSLC(1,     ivec2 size;                                              );
        GLSLC(1,     const ivec2 pos = ivec2(gl_GlobalInvocationID.xy);       );
        for (int i = 0; i < planes; i++) {
            GLSLC(0,                                                          );
            GLSLF(1,  size = imageSize(output_img[%i]);                     ,i);
            GLSLC(1,  if (IS_WITHIN(pos, size)) {                             );
            if (s->planes & (1 << i)) {
                GLSLF(2, distort(pos, %i);                                  ,i);
            } else {
                GLSLF(2, vec4 res = texture(input_img[%i], pos);            ,i);
                GLSLF(2, imageStore(output_img[%i], pos, res);              ,i);
            }
            GLSLC(1, }                                                        );
        }
        GLSLC(0, }                                                            );

        RET(ff_vk_compile_shader(ctx, shd, "main"));

        RET(ff_vk_init_pipeline_layout(ctx, s->pl_ver));
        RET(ff_vk_init_compute_pipeline(ctx, s->pl_ver));
    }

    /* Execution context */
    RET(ff_vk_create_exec_ctx(ctx, &s->exec,
                              s->vkctx.hwctx->queue_family_comp_index));

    s->initialized = 1;

    return 0;

fail:
    return err;
}

static int process_frames(AVFilterContext *avctx, AVFrame *out_f, AVFrame *tmp_f, AVFrame *in_f)
{
    int err;
    AvgBlurVulkanContext *s = avctx->priv;
    AVVkFrame *in = (AVVkFrame *)in_f->data[0];
    AVVkFrame *tmp = (AVVkFrame *)tmp_f->data[0];
    AVVkFrame *out = (AVVkFrame *)out_f->data[0];
    int planes = av_pix_fmt_count_planes(s->vkctx.output_format);

    for (int i = 0; i < planes; i++) {
        RET(ff_vk_create_imageview(avctx, &s->input_images[i].imageView, in->img[i],
                                   av_vkfmt_from_pixfmt(s->vkctx.input_format)[i],
                                   ff_comp_identity_map));

        RET(ff_vk_create_imageview(avctx, &s->tmp_images[i].imageView, tmp->img[i],
                                   av_vkfmt_from_pixfmt(s->vkctx.output_format)[i],
                                   ff_comp_identity_map));

        RET(ff_vk_create_imageview(avctx, &s->output_images[i].imageView, out->img[i],
                                   av_vkfmt_from_pixfmt(s->vkctx.output_format)[i],
                                   ff_comp_identity_map));

        s->input_images[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        s->tmp_images[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        s->output_images[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    }

    ff_vk_update_descriptor_set(avctx, s->pl_hor, 0);
    ff_vk_update_descriptor_set(avctx, s->pl_ver, 0);

    ff_vk_start_exec_recording(avctx, s->exec);

    for (int i = 0; i < planes; i++) {
        VkImageMemoryBarrier bar[] = {
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                .oldLayout = in->layout[i],
                .newLayout = s->input_images[i].imageLayout,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = in->img[i],
                .subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .subresourceRange.levelCount = 1,
                .subresourceRange.layerCount = 1,
            },
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
                .oldLayout = tmp->layout[i],
                .newLayout = s->tmp_images[i].imageLayout,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = tmp->img[i],
                .subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .subresourceRange.levelCount = 1,
                .subresourceRange.layerCount = 1,
            },
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                .oldLayout = out->layout[i],
                .newLayout = s->output_images[i].imageLayout,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = out->img[i],
                .subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .subresourceRange.levelCount = 1,
                .subresourceRange.layerCount = 1,
            },
        };

        vkCmdPipelineBarrier(s->exec->buf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                             0, NULL, 0, NULL, FF_ARRAY_ELEMS(bar), bar);

        in->layout[i]  = bar[0].newLayout;
        in->access[i]  = bar[0].dstAccessMask;

        tmp->layout[i] = bar[1].newLayout;
        tmp->access[i] = bar[1].dstAccessMask;

        out->layout[i] = bar[2].newLayout;
        out->access[i] = bar[2].dstAccessMask;
    }

    ff_vk_bind_pipeline_exec(avctx, s->exec, s->pl_hor);

    vkCmdDispatch(s->exec->buf, FFALIGN(s->vkctx.output_width, CGS)/CGS,
                  s->vkctx.output_height, 1);

    ff_vk_bind_pipeline_exec(avctx, s->exec, s->pl_ver);

    vkCmdDispatch(s->exec->buf, s->vkctx.output_width,
                  FFALIGN(s->vkctx.output_height, CGS)/CGS, 1);

    ff_vk_add_exec_dep(avctx, s->exec, in_f, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    ff_vk_add_exec_dep(avctx, s->exec, out_f, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);

    err = ff_vk_submit_exec_queue(avctx, s->exec);
    if (err)
        return err;

fail:

    for (int i = 0; i < planes; i++) {
        ff_vk_destroy_imageview(avctx, &s->input_images[i].imageView);
        ff_vk_destroy_imageview(avctx, &s->tmp_images[i].imageView);
        ff_vk_destroy_imageview(avctx, &s->output_images[i].imageView);
    }

    return err;
}

static int avgblur_vulkan_filter_frame(AVFilterLink *link, AVFrame *in)
{
    int err;
    AVFrame *tmp = NULL, *out = NULL;
    AVFilterContext *ctx = link->dst;
    AvgBlurVulkanContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    tmp = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    if (!s->initialized)
        RET(init_filter(ctx, in));

    RET(process_frames(ctx, out, tmp, in));

    err = av_frame_copy_props(out, in);
    if (err < 0)
        goto fail;

    av_frame_free(&in);
    av_frame_free(&tmp);

    return ff_filter_frame(outlink, out);

fail:
    av_frame_free(&in);
    av_frame_free(&tmp);
    av_frame_free(&out);
    return err;
}

static void avgblur_vulkan_uninit(AVFilterContext *avctx)
{
    AvgBlurVulkanContext *s = avctx->priv;

    ff_vk_filter_uninit(avctx);

    s->initialized = 0;
}

#define OFFSET(x) offsetof(AvgBlurVulkanContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)
static const AVOption avgblur_vulkan_options[] = {
    { "sizeX",  "Set horizontal radius", OFFSET(size_x), AV_OPT_TYPE_INT, {.i64 = 3}, 1, 32, .flags = FLAGS },
    { "planes", "Set planes to filter (bitmask)", OFFSET(planes), AV_OPT_TYPE_INT, {.i64 = 0xF}, 0, 0xF, .flags = FLAGS },
    { "sizeY",  "Set vertical radius", OFFSET(size_y), AV_OPT_TYPE_INT, {.i64 = 3}, 1, 32, .flags = FLAGS },
    { NULL },
};

AVFILTER_DEFINE_CLASS(avgblur_vulkan);

static const AVFilterPad avgblur_vulkan_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = &avgblur_vulkan_filter_frame,
        .config_props = &ff_vk_filter_config_input,
    },
    { NULL }
};

static const AVFilterPad avgblur_vulkan_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = &ff_vk_filter_config_output,
    },
    { NULL }
};

AVFilter ff_vf_avgblur_vulkan = {
    .name           = "avgblur_vulkan",
    .description    = NULL_IF_CONFIG_SMALL("Apply avgblur mask to input video"),
    .priv_size      = sizeof(AvgBlurVulkanContext),
    .init           = &ff_vk_filter_init,
    .uninit         = &avgblur_vulkan_uninit,
    .query_formats  = &ff_vk_filter_query_formats,
    .inputs         = avgblur_vulkan_inputs,
    .outputs        = avgblur_vulkan_outputs,
    .priv_class     = &avgblur_vulkan_class,
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
