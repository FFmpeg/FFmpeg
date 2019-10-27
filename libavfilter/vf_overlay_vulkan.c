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
#include "framesync.h"

#define CGROUPS (int [3]){ 32, 32, 1 }

typedef struct OverlayVulkanContext {
    VulkanFilterContext vkctx;

    int initialized;
    VulkanPipeline *pl;
    FFVkExecContext *exec;
    FFFrameSync fs;
    FFVkBuffer params_buf;

    /* Shader updators, must be in the main filter struct */
    VkDescriptorImageInfo main_images[3];
    VkDescriptorImageInfo overlay_images[3];
    VkDescriptorImageInfo output_images[3];
    VkDescriptorBufferInfo params_desc;

    int overlay_x;
    int overlay_y;
    int overlay_w;
    int overlay_h;
} OverlayVulkanContext;

static const char overlay_noalpha[] = {
    C(0, void overlay_noalpha(int i, ivec2 pos)                                )
    C(0, {                                                                     )
    C(1,     if ((o_offset[i].x <= pos.x) && (o_offset[i].y <= pos.y) &&
                 (pos.x < (o_offset[i].x + o_size[i].x)) &&
                 (pos.y < (o_offset[i].y + o_size[i].y))) {                    )
    C(2,         vec4 res = texture(overlay_img[i], pos - o_offset[i]);        )
    C(2,         imageStore(output_img[i], pos, res);                          )
    C(1,     } else {                                                          )
    C(2,         vec4 res = texture(main_img[i], pos);                         )
    C(2,         imageStore(output_img[i], pos, res);                          )
    C(1,     }                                                                 )
    C(0, }                                                                     )
};

static av_cold int init_filter(AVFilterContext *ctx)
{
    int err;
    OverlayVulkanContext *s = ctx->priv;
    VkSampler *sampler = ff_vk_init_sampler(ctx, 1, VK_FILTER_LINEAR);
    if (!sampler)
        return AVERROR_EXTERNAL;

    s->pl = ff_vk_create_pipeline(ctx);
    if (!s->pl)
        return AVERROR(ENOMEM);

    { /* Create the shader */
        const int planes = av_pix_fmt_count_planes(s->vkctx.output_format);

        VulkanDescriptorSetBinding desc_i[3] = {
            {
                .name       = "main_img",
                .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .dimensions = 2,
                .elems      = planes,
                .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
                .updater    = s->main_images,
                .samplers   = DUP_SAMPLER_ARRAY4(*sampler),
            },
            {
                .name       = "overlay_img",
                .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .dimensions = 2,
                .elems      = planes,
                .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
                .updater    = s->overlay_images,
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

        VulkanDescriptorSetBinding desc_b = {
            .name        = "params",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .mem_quali   = "readonly",
            .mem_layout  = "std430",
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .updater     = &s->params_desc,
            .buf_content = "ivec2 o_offset[3], o_size[3];",
        };

        SPIRVShader *shd = ff_vk_init_shader(ctx, s->pl, "overlay_compute",
                                             VK_SHADER_STAGE_COMPUTE_BIT);
        if (!shd)
            return AVERROR(ENOMEM);

        ff_vk_set_compute_shader_sizes(ctx, shd, CGROUPS);

        RET(ff_vk_add_descriptor_set(ctx, s->pl, shd,  desc_i, 3, 0)); /* set 0 */
        RET(ff_vk_add_descriptor_set(ctx, s->pl, shd, &desc_b, 1, 0)); /* set 1 */

        GLSLD(   overlay_noalpha                                              );
        GLSLC(0, void main()                                                  );
        GLSLC(0, {                                                            );
        GLSLC(1,     ivec2 pos = ivec2(gl_GlobalInvocationID.xy);             );
        GLSLF(1,     int planes = %i;                                  ,planes);
        GLSLC(1,     for (int i = 0; i < planes; i++) {                       );
        GLSLC(2,         overlay_noalpha(i, pos);                             );
        GLSLC(1,     }                                                        );
        GLSLC(0, }                                                            );

        RET(ff_vk_compile_shader(ctx, shd, "main"));
    }

    RET(ff_vk_init_pipeline_layout(ctx, s->pl));
    RET(ff_vk_init_compute_pipeline(ctx, s->pl));

    { /* Create and update buffer */
        const AVPixFmtDescriptor *desc;

        /* NOTE: std430 requires the same identical struct layout, padding and
         * alignment as C, so we're allowed to do this, as this will map
         * exactly to what the shader recieves */
        struct {
            int32_t o_offset[2*3];
            int32_t o_size[2*3];
        } *par;

        err = ff_vk_create_buf(ctx, &s->params_buf,
                               sizeof(*par),
                               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        if (err)
            return err;

        err = ff_vk_map_buffers(ctx, &s->params_buf, (uint8_t **)&par, 1, 0);
        if (err)
            return err;

        desc = av_pix_fmt_desc_get(s->vkctx.output_format);

        par->o_offset[0] = s->overlay_x;
        par->o_offset[1] = s->overlay_y;
        par->o_offset[2] = par->o_offset[0] >> desc->log2_chroma_w;
        par->o_offset[3] = par->o_offset[1] >> desc->log2_chroma_h;
        par->o_offset[4] = par->o_offset[0] >> desc->log2_chroma_w;
        par->o_offset[5] = par->o_offset[1] >> desc->log2_chroma_h;

        par->o_size[0] = s->overlay_w;
        par->o_size[1] = s->overlay_h;
        par->o_size[2] = par->o_size[0] >> desc->log2_chroma_w;
        par->o_size[3] = par->o_size[1] >> desc->log2_chroma_h;
        par->o_size[4] = par->o_size[0] >> desc->log2_chroma_w;
        par->o_size[5] = par->o_size[1] >> desc->log2_chroma_h;

        err = ff_vk_unmap_buffers(ctx, &s->params_buf, 1, 1);
        if (err)
            return err;

        s->params_desc.buffer = s->params_buf.buf;
        s->params_desc.range  = VK_WHOLE_SIZE;

        ff_vk_update_descriptor_set(ctx, s->pl, 1);
    }

    /* Execution context */
    RET(ff_vk_create_exec_ctx(ctx, &s->exec,
                              s->vkctx.hwctx->queue_family_comp_index));

    s->initialized = 1;

    return 0;

fail:
    return err;
}

static int process_frames(AVFilterContext *avctx, AVFrame *out_f,
                          AVFrame *main_f, AVFrame *overlay_f)
{
    int err;
    OverlayVulkanContext *s = avctx->priv;
    int planes = av_pix_fmt_count_planes(s->vkctx.output_format);

    AVVkFrame *out     = (AVVkFrame *)out_f->data[0];
    AVVkFrame *main    = (AVVkFrame *)main_f->data[0];
    AVVkFrame *overlay = (AVVkFrame *)overlay_f->data[0];

    AVHWFramesContext *main_fc = (AVHWFramesContext*)main_f->hw_frames_ctx->data;
    AVHWFramesContext *overlay_fc = (AVHWFramesContext*)overlay_f->hw_frames_ctx->data;

    for (int i = 0; i < planes; i++) {
        RET(ff_vk_create_imageview(avctx, &s->main_images[i].imageView, main->img[i],
                                   av_vkfmt_from_pixfmt(main_fc->sw_format)[i],
                                   ff_comp_identity_map));

        RET(ff_vk_create_imageview(avctx, &s->overlay_images[i].imageView, overlay->img[i],
                                   av_vkfmt_from_pixfmt(overlay_fc->sw_format)[i],
                                   ff_comp_identity_map));

        RET(ff_vk_create_imageview(avctx, &s->output_images[i].imageView, out->img[i],
                                   av_vkfmt_from_pixfmt(s->vkctx.output_format)[i],
                                   ff_comp_identity_map));

        s->main_images[i].imageLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        s->overlay_images[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        s->output_images[i].imageLayout  = VK_IMAGE_LAYOUT_GENERAL;
    }

    ff_vk_update_descriptor_set(avctx, s->pl, 0);

    ff_vk_start_exec_recording(avctx, s->exec);

    for (int i = 0; i < planes; i++) {
        VkImageMemoryBarrier bar[3] = {
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                .oldLayout = main->layout[i],
                .newLayout = s->main_images[i].imageLayout,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = main->img[i],
                .subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .subresourceRange.levelCount = 1,
                .subresourceRange.layerCount = 1,
            },
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                .oldLayout = overlay->layout[i],
                .newLayout = s->overlay_images[i].imageLayout,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = overlay->img[i],
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

        main->layout[i]    = bar[0].newLayout;
        main->access[i]    = bar[0].dstAccessMask;

        overlay->layout[i] = bar[1].newLayout;
        overlay->access[i] = bar[1].dstAccessMask;

        out->layout[i]     = bar[2].newLayout;
        out->access[i]     = bar[2].dstAccessMask;
    }

    ff_vk_bind_pipeline_exec(avctx, s->exec, s->pl);

    vkCmdDispatch(s->exec->buf,
                  FFALIGN(s->vkctx.output_width,  CGROUPS[0])/CGROUPS[0],
                  FFALIGN(s->vkctx.output_height, CGROUPS[1])/CGROUPS[1], 1);

    ff_vk_add_exec_dep(avctx, s->exec, main_f, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    ff_vk_add_exec_dep(avctx, s->exec, overlay_f, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    ff_vk_add_exec_dep(avctx, s->exec, out_f, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);

    err = ff_vk_submit_exec_queue(avctx, s->exec);
    if (err)
        return err;

fail:

    for (int i = 0; i < planes; i++) {
        ff_vk_destroy_imageview(avctx, &s->main_images[i].imageView);
        ff_vk_destroy_imageview(avctx, &s->overlay_images[i].imageView);
        ff_vk_destroy_imageview(avctx, &s->output_images[i].imageView);
    }

    return err;
}

static int overlay_vulkan_blend(FFFrameSync *fs)
{
    int err;
    AVFilterContext *ctx = fs->parent;
    OverlayVulkanContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *input_main, *input_overlay, *out;

    err = ff_framesync_get_frame(fs, 0, &input_main, 0);
    if (err < 0)
        goto fail;
    err = ff_framesync_get_frame(fs, 1, &input_overlay, 0);
    if (err < 0)
        goto fail;

    if (!input_main || !input_overlay)
        return 0;

    if (!s->initialized) {
        AVHWFramesContext *main_fc = (AVHWFramesContext*)input_main->hw_frames_ctx->data;
        AVHWFramesContext *overlay_fc = (AVHWFramesContext*)input_overlay->hw_frames_ctx->data;
        if (main_fc->sw_format != overlay_fc->sw_format) {
            av_log(ctx, AV_LOG_ERROR, "Mismatching sw formats!\n");
            return AVERROR(EINVAL);
        }

        s->overlay_w = input_overlay->width;
        s->overlay_h = input_overlay->height;

        RET(init_filter(ctx));
    }

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    RET(process_frames(ctx, out, input_main, input_overlay));

    err = av_frame_copy_props(out, input_main);
    if (err < 0)
        goto fail;

    return ff_filter_frame(outlink, out);

fail:
    av_frame_free(&out);
    return err;
}

static int overlay_vulkan_config_output(AVFilterLink *outlink)
{
    int err;
    AVFilterContext *avctx = outlink->src;
    OverlayVulkanContext *s = avctx->priv;

    err = ff_vk_filter_config_output(outlink);
    if (err < 0)
        return err;

    err = ff_framesync_init_dualinput(&s->fs, avctx);
    if (err < 0)
        return err;

    return ff_framesync_configure(&s->fs);
}

static int overlay_vulkan_activate(AVFilterContext *avctx)
{
    OverlayVulkanContext *s = avctx->priv;

    return ff_framesync_activate(&s->fs);
}

static av_cold int overlay_vulkan_init(AVFilterContext *avctx)
{
    OverlayVulkanContext *s = avctx->priv;

    s->fs.on_event = &overlay_vulkan_blend;

    return ff_vk_filter_init(avctx);
}

static void overlay_vulkan_uninit(AVFilterContext *avctx)
{
    OverlayVulkanContext *s = avctx->priv;

    ff_vk_filter_uninit(avctx);
    ff_framesync_uninit(&s->fs);

    ff_vk_free_buf(avctx, &s->params_buf);

    s->initialized = 0;
}

#define OFFSET(x) offsetof(OverlayVulkanContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)
static const AVOption overlay_vulkan_options[] = {
    { "x", "Set horizontal offset", OFFSET(overlay_x), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, .flags = FLAGS },
    { "y", "Set vertical offset",   OFFSET(overlay_y), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, .flags = FLAGS },
    { NULL },
};

AVFILTER_DEFINE_CLASS(overlay_vulkan);

static const AVFilterPad overlay_vulkan_inputs[] = {
    {
        .name         = "main",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = &ff_vk_filter_config_input,
    },
    {
        .name         = "overlay",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = &ff_vk_filter_config_input,
    },
    { NULL }
};

static const AVFilterPad overlay_vulkan_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = &overlay_vulkan_config_output,
    },
    { NULL }
};

AVFilter ff_vf_overlay_vulkan = {
    .name           = "overlay_vulkan",
    .description    = NULL_IF_CONFIG_SMALL("Overlay a source on top of another"),
    .priv_size      = sizeof(OverlayVulkanContext),
    .init           = &overlay_vulkan_init,
    .uninit         = &overlay_vulkan_uninit,
    .query_formats  = &ff_vk_filter_query_formats,
    .activate       = &overlay_vulkan_activate,
    .inputs         = overlay_vulkan_inputs,
    .outputs        = overlay_vulkan_outputs,
    .priv_class     = &overlay_vulkan_class,
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
