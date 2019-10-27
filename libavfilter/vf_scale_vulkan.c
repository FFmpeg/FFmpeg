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
#include "scale_eval.h"
#include "internal.h"

#define CGROUPS (int [3]){ 32, 32, 1 }

enum ScalerFunc {
    F_BILINEAR = 0,
    F_NEAREST,

    F_NB,
};

typedef struct ScaleVulkanContext {
    VulkanFilterContext vkctx;

    int initialized;
    FFVkExecContext *exec;
    VulkanPipeline *pl;

    /* Shader updators, must be in the main filter struct */
    VkDescriptorImageInfo input_images[3];
    VkDescriptorImageInfo output_images[3];

    enum ScalerFunc scaler;
    char *output_format_string;
    char *w_expr;
    char *h_expr;
} ScaleVulkanContext;

static const char scale_bilinear[] = {
    C(0, void scale_bilinear(int idx, ivec2 pos)                                )
    C(0, {                                                                      )
    C(1,     const vec2 npos = (vec2(pos) + 0.5f) / imageSize(output_img[idx]); )
    C(1,     imageStore(output_img[idx], pos, texture(input_img[idx], npos));   )
    C(0, }                                                                      )
};

static av_cold int init_filter(AVFilterContext *ctx, AVFrame *in)
{
    int err;
    VkSampler *sampler;
    VkFilter sampler_mode;
    ScaleVulkanContext *s = ctx->priv;

    switch (s->scaler) {
    case F_NEAREST:
        sampler_mode = VK_FILTER_NEAREST;
        break;
    case F_BILINEAR:
        sampler_mode = VK_FILTER_LINEAR;
        break;
    };

    /* Create a sampler */
    sampler = ff_vk_init_sampler(ctx, 0, sampler_mode);
    if (!sampler)
        return AVERROR_EXTERNAL;

    s->pl = ff_vk_create_pipeline(ctx);
    if (!s->pl)
        return AVERROR(ENOMEM);

    { /* Create the shader */
        VulkanDescriptorSetBinding desc_i[2] = {
            {
                .name       = "input_img",
                .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .dimensions = 2,
                .elems      = av_pix_fmt_count_planes(s->vkctx.input_format),
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
                .elems      = av_pix_fmt_count_planes(s->vkctx.output_format),
                .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
                .updater    = s->output_images,
            },
        };

        SPIRVShader *shd = ff_vk_init_shader(ctx, s->pl, "scale_compute",
                                             VK_SHADER_STAGE_COMPUTE_BIT);
        if (!shd)
            return AVERROR(ENOMEM);

        ff_vk_set_compute_shader_sizes(ctx, shd, CGROUPS);

        RET(ff_vk_add_descriptor_set(ctx, s->pl, shd, desc_i, 2, 0)); /* set 0 */

        GLSLD(   scale_bilinear                                               );
        GLSLC(0, void main()                                                  );
        GLSLC(0, {                                                            );
        GLSLC(1,     ivec2 size;                                              );
        GLSLC(1,     ivec2 pos = ivec2(gl_GlobalInvocationID.xy);             );

        for (int i = 0; i < desc_i[1].elems; i++) {
            GLSLC(0,                                                          );
            GLSLF(1,  size = imageSize(output_img[%i]);                     ,i);
            GLSLC(1,  if (IS_WITHIN(pos, size))                               );
            switch (s->scaler) {
            case F_NEAREST:
            case F_BILINEAR:
                GLSLF(2, scale_bilinear(%i, pos);                           ,i);
                break;
            };
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
    ScaleVulkanContext *s = avctx->priv;
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

static int scale_vulkan_filter_frame(AVFilterLink *link, AVFrame *in)
{
    int err;
    AVFilterContext *ctx = link->dst;
    ScaleVulkanContext *s = ctx->priv;
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

static int scale_vulkan_config_output(AVFilterLink *outlink)
{
    int err;
    AVFilterContext *avctx = outlink->src;
    ScaleVulkanContext *s  = avctx->priv;
    AVFilterLink *inlink   = outlink->src->inputs[0];

    err = ff_scale_eval_dimensions(s, s->w_expr, s->h_expr, inlink, outlink,
                                   &s->vkctx.output_width,
                                   &s->vkctx.output_height);
    if (err < 0)
        return err;

    s->vkctx.output_format = s->vkctx.input_format;

    err = ff_vk_filter_config_output(outlink);
    if (err < 0)
        return err;

    if (inlink->sample_aspect_ratio.num)
        outlink->sample_aspect_ratio = av_mul_q((AVRational){outlink->h * inlink->w, outlink->w * inlink->h}, inlink->sample_aspect_ratio);
    else
        outlink->sample_aspect_ratio = inlink->sample_aspect_ratio;

    return 0;
}

static void scale_vulkan_uninit(AVFilterContext *avctx)
{
    ScaleVulkanContext *s = avctx->priv;

    ff_vk_filter_uninit(avctx);

    s->initialized = 0;
}

#define OFFSET(x) offsetof(ScaleVulkanContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)
static const AVOption scale_vulkan_options[] = {
    { "w", "Output video width",  OFFSET(w_expr), AV_OPT_TYPE_STRING, {.str = "iw"}, .flags = FLAGS },
    { "h", "Output video height", OFFSET(h_expr), AV_OPT_TYPE_STRING, {.str = "ih"}, .flags = FLAGS },
    { "scaler", "Scaler function", OFFSET(scaler), AV_OPT_TYPE_INT, {.i64 = F_BILINEAR}, 0, F_NB, .flags = FLAGS, "scaler" },
        { "bilinear", "Bilinear interpolation (fastest)", 0, AV_OPT_TYPE_CONST, {.i64 = F_BILINEAR}, 0, 0, .flags = FLAGS, "scaler" },
        { "nearest", "Nearest (useful for pixel art)", 0, AV_OPT_TYPE_CONST, {.i64 = F_NEAREST}, 0, 0, .flags = FLAGS, "scaler" },
    { NULL },
};

AVFILTER_DEFINE_CLASS(scale_vulkan);

static const AVFilterPad scale_vulkan_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = &scale_vulkan_filter_frame,
        .config_props = &ff_vk_filter_config_input,
    },
    { NULL }
};

static const AVFilterPad scale_vulkan_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = &scale_vulkan_config_output,
    },
    { NULL }
};

AVFilter ff_vf_scale_vulkan = {
    .name           = "scale_vulkan",
    .description    = NULL_IF_CONFIG_SMALL("Scale Vulkan frames"),
    .priv_size      = sizeof(ScaleVulkanContext),
    .init           = &ff_vk_filter_init,
    .uninit         = &scale_vulkan_uninit,
    .query_formats  = &ff_vk_filter_query_formats,
    .inputs         = scale_vulkan_inputs,
    .outputs        = scale_vulkan_outputs,
    .priv_class     = &scale_vulkan_class,
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
