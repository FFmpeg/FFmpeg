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

#define CGROUPS (int [3]){ 32, 32, 1 }

typedef struct ChromaticAberrationVulkanContext {
    VulkanFilterContext vkctx;

    int initialized;
    FFVkExecContext *exec;
    VulkanPipeline *pl;

    /* Shader updators, must be in the main filter struct */
    VkDescriptorImageInfo input_images[3];
    VkDescriptorImageInfo output_images[3];

    /* Push constants / options */
    struct {
        float dist[2];
    } opts;
} ChromaticAberrationVulkanContext;

static const char distort_chroma_kernel[] = {
    C(0, void distort_rgb(ivec2 size, ivec2 pos)                               )
    C(0, {                                                                     )
    C(1,     const vec2 p = ((vec2(pos)/vec2(size)) - 0.5f)*2.0f;              )
    C(1,     const vec2 o = p * (dist - 1.0f);                                 )
    C(0,                                                                       )
    C(1,     vec4 res;                                                         )
    C(1,     res.r = texture(input_img[0], ((p - o)/2.0f) + 0.5f).r;           )
    C(1,     res.g = texture(input_img[0], ((p    )/2.0f) + 0.5f).g;           )
    C(1,     res.b = texture(input_img[0], ((p + o)/2.0f) + 0.5f).b;           )
    C(1,     res.a = texture(input_img[0], ((p    )/2.0f) + 0.5f).a;           )
    C(1,     imageStore(output_img[0], pos, res);                              )
    C(0, }                                                                     )
    C(0,                                                                       )
    C(0, void distort_chroma(int idx, ivec2 size, ivec2 pos)                   )
    C(0, {                                                                     )
    C(1,     vec2 p = ((vec2(pos)/vec2(size)) - 0.5f)*2.0f;                    )
    C(1,     float d = sqrt(p.x*p.x + p.y*p.y);                                )
    C(1,     p *= d / (d*     dist);                                           )
    C(1,     vec4 res = texture(input_img[idx], (p/2.0f) + 0.5f);              )
    C(1,     imageStore(output_img[idx], pos, res);                            )
    C(0, }                                                                     )
};

static av_cold int init_filter(AVFilterContext *ctx, AVFrame *in)
{
    int err;
    ChromaticAberrationVulkanContext *s = ctx->priv;

    /* Create a sampler */
    VkSampler *sampler = ff_vk_init_sampler(ctx, 0, VK_FILTER_LINEAR);
    if (!sampler)
        return AVERROR_EXTERNAL;

    s->pl = ff_vk_create_pipeline(ctx);
    if (!s->pl)
        return AVERROR(ENOMEM);

    /* Normalize options */
    s->opts.dist[0] = (s->opts.dist[0] / 100.0f) + 1.0f;
    s->opts.dist[1] = (s->opts.dist[1] / 100.0f) + 1.0f;

    { /* Create the shader */
        const int planes = av_pix_fmt_count_planes(s->vkctx.output_format);
        VulkanDescriptorSetBinding desc_i[2] = {
            {
                .name       = "input_img",
                .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .dimensions = 2,
                .elems      = planes,
                .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
                .updater    = s->input_images,
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
                .updater    = s->output_images,
            },
        };

        SPIRVShader *shd = ff_vk_init_shader(ctx, s->pl, "chromaber_compute",
                                             VK_SHADER_STAGE_COMPUTE_BIT);
        if (!shd)
            return AVERROR(ENOMEM);

        ff_vk_set_compute_shader_sizes(ctx, shd, CGROUPS);

        GLSLC(0, layout(push_constant, std430) uniform pushConstants {        );
        GLSLC(1,    vec2 dist;                                                );
        GLSLC(0, };                                                           );
        GLSLC(0,                                                              );

        ff_vk_add_push_constant(ctx, s->pl, 0, sizeof(s->opts),
                                VK_SHADER_STAGE_COMPUTE_BIT);

        RET(ff_vk_add_descriptor_set(ctx, s->pl, shd, desc_i, 2, 0)); /* set 0 */

        GLSLD(   distort_chroma_kernel                                        );
        GLSLC(0, void main()                                                  );
        GLSLC(0, {                                                            );
        GLSLC(1,     ivec2 pos = ivec2(gl_GlobalInvocationID.xy);             );
        if (planes == 1) {
            GLSLC(1, distort_rgb(imageSize(output_img[0]), pos);              );
        } else {
            GLSLC(1, ivec2 size = imageSize(output_img[0]);                   );
            GLSLC(1, vec2 npos = vec2(pos)/vec2(size);                        );
            GLSLC(1, vec4 res = texture(input_img[0], npos);                  );
            GLSLC(1, imageStore(output_img[0], pos, res);                     );
            for (int i = 1; i < planes; i++) {
                GLSLC(0,                                                      );
                GLSLF(1,  size = imageSize(output_img[%i]);                 ,i);
                GLSLC(1,  if (IS_WITHIN(pos, size)) {                         );
                GLSLF(2,      distort_chroma(%i, size, pos);                ,i);
                GLSLC(1,  } else {                                            );
                GLSLC(2,    npos = vec2(pos)/vec2(size);                      );
                GLSLF(2,    res = texture(input_img[%i], npos);             ,i);
                GLSLF(2,    imageStore(output_img[%i], pos, res);           ,i);
                GLSLC(1, }                                                    );
            }
        }
        GLSLC(0, }                                                            );

        RET(ff_vk_compile_shader(ctx, shd, "main"));
    }

    RET(ff_vk_init_pipeline_layout(ctx, s->pl));
    RET(ff_vk_init_compute_pipeline(ctx, s->pl));

    /* Execution context */
    RET(ff_vk_create_exec_ctx(ctx, &s->exec,
                              s->vkctx.hwctx->queue_family_comp_index));

    s->initialized = 1;

    return 0;

fail:
    return err;
}

static int process_frames(AVFilterContext *avctx, AVFrame *out_f, AVFrame *in_f)
{
    int err = 0;
    ChromaticAberrationVulkanContext *s = avctx->priv;
    AVVkFrame *in = (AVVkFrame *)in_f->data[0];
    AVVkFrame *out = (AVVkFrame *)out_f->data[0];
    int planes = av_pix_fmt_count_planes(s->vkctx.output_format);

    for (int i = 0; i < planes; i++) {
        RET(ff_vk_create_imageview(avctx, &s->input_images[i].imageView, in->img[i],
                                   av_vkfmt_from_pixfmt(s->vkctx.input_format)[i],
                                   ff_comp_identity_map));

        RET(ff_vk_create_imageview(avctx, &s->output_images[i].imageView, out->img[i],
                                   av_vkfmt_from_pixfmt(s->vkctx.output_format)[i],
                                   ff_comp_identity_map));

        s->input_images[i].imageLayout  = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        s->output_images[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    }

    ff_vk_update_descriptor_set(avctx, s->pl, 0);

    ff_vk_start_exec_recording(avctx, s->exec);

    for (int i = 0; i < planes; i++) {
        VkImageMemoryBarrier bar[2] = {
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

        out->layout[i] = bar[1].newLayout;
        out->access[i] = bar[1].dstAccessMask;
    }

    ff_vk_bind_pipeline_exec(avctx, s->exec, s->pl);

    ff_vk_update_push_exec(avctx, s->exec, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(s->opts), &s->opts);

    vkCmdDispatch(s->exec->buf,
                  FFALIGN(s->vkctx.output_width,  CGROUPS[0])/CGROUPS[0],
                  FFALIGN(s->vkctx.output_height, CGROUPS[1])/CGROUPS[1], 1);

    ff_vk_add_exec_dep(avctx, s->exec, in_f, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    ff_vk_add_exec_dep(avctx, s->exec, out_f, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);

    err = ff_vk_submit_exec_queue(avctx, s->exec);
    if (err)
        return err;

    for (int i = 0; i < planes; i++) {
        ff_vk_destroy_imageview(avctx, &s->input_images[i].imageView);
        ff_vk_destroy_imageview(avctx, &s->output_images[i].imageView);
    }

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

    RET(process_frames(ctx, out, in));

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

    ff_vk_filter_uninit(avctx);

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
    { NULL }
};

static const AVFilterPad chromaber_vulkan_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = &ff_vk_filter_config_output,
    },
    { NULL }
};

AVFilter ff_vf_chromaber_vulkan = {
    .name           = "chromaber_vulkan",
    .description    = NULL_IF_CONFIG_SMALL("Offset chroma of input video (chromatic aberration)"),
    .priv_size      = sizeof(ChromaticAberrationVulkanContext),
    .init           = &ff_vk_filter_init,
    .uninit         = &chromaber_vulkan_uninit,
    .query_formats  = &ff_vk_filter_query_formats,
    .inputs         = chromaber_vulkan_inputs,
    .outputs        = chromaber_vulkan_outputs,
    .priv_class     = &chromaber_vulkan_class,
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
