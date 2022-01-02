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
#include "transpose.h"

#define CGS 32

typedef struct TransposeVulkanContext {
    FFVulkanContext vkctx;
    FFVkQueueFamilyCtx qf;
    FFVkExecContext *exec;
    FFVulkanPipeline *pl;

    VkDescriptorImageInfo input_images[3];
    VkDescriptorImageInfo output_images[3];

    int dir;
    int passthrough;
    int initialized;
} TransposeVulkanContext;

static av_cold int init_filter(AVFilterContext *ctx, AVFrame *in)
{
    int err = 0;
    FFVkSPIRVShader *shd;
    TransposeVulkanContext *s = ctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;
    const int planes = av_pix_fmt_count_planes(s->vkctx.output_format);

    FFVulkanDescriptorSetBinding image_descs[] = {
        {
            .name       = "input_images",
            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .dimensions = 2,
            .elems      = planes,
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
            .updater    = s->input_images,
        },
        {
            .name       = "output_images",
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

        shd = ff_vk_init_shader(s->pl, "transpose_compute", image_descs[0].stages);
        if (!shd)
            return AVERROR(ENOMEM);

        ff_vk_set_compute_shader_sizes(shd, (int [3]){ CGS, 1, 1 });
        RET(ff_vk_add_descriptor_set(vkctx, s->pl, shd, image_descs, FF_ARRAY_ELEMS(image_descs), 0));

        GLSLC(0, void main()                                               );
        GLSLC(0, {                                                         );
        GLSLC(1,     ivec2 size;                                           );
        GLSLC(1,     ivec2 pos = ivec2(gl_GlobalInvocationID.xy);          );
        for (int i = 0; i < planes; i++) {
            GLSLC(0,                                                       );
            GLSLF(1, size = imageSize(output_images[%i]);                ,i);
            GLSLC(1, if (IS_WITHIN(pos, size)) {                           );
            if (s->dir == TRANSPOSE_CCLOCK)
                GLSLF(2, vec4 res = texture(input_images[%i], ivec2(size.y - pos.y, pos.x)); ,i);
            else if (s->dir == TRANSPOSE_CLOCK_FLIP || s->dir == TRANSPOSE_CLOCK) {
                GLSLF(2, vec4 res = texture(input_images[%i], ivec2(size.yx - pos.yx));      ,i);
                if (s->dir == TRANSPOSE_CLOCK)
                    GLSLC(2, pos = ivec2(pos.x, size.y - pos.y);           );
            } else
                GLSLF(2, vec4 res = texture(input_images[%i], pos.yx);   ,i);
            GLSLF(2,     imageStore(output_images[%i], pos, res);        ,i);
            GLSLC(1, }                                                     );
        }
        GLSLC(0, }                                                         );

        RET(ff_vk_compile_shader(vkctx, shd, "main"));
        RET(ff_vk_init_pipeline_layout(vkctx, s->pl));
        RET(ff_vk_init_compute_pipeline(vkctx, s->pl));
    }

    RET(ff_vk_create_exec_ctx(vkctx, &s->exec, &s->qf));
    s->initialized = 1;

fail:
    return err;
}

static int process_frames(AVFilterContext *avctx, AVFrame *outframe, AVFrame *inframe)
{
    int err = 0;
    VkCommandBuffer cmd_buf;
    TransposeVulkanContext *s = avctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;
    FFVulkanFunctions *vk = &s->vkctx.vkfn;
    const int planes = av_pix_fmt_count_planes(s->vkctx.output_format);

    AVVkFrame *in  = (AVVkFrame *)inframe->data[0];
    AVVkFrame *out = (AVVkFrame *)outframe->data[0];

    const VkFormat *input_formats  = av_vkfmt_from_pixfmt(s->vkctx.input_format);
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

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    int err;
    AVFrame *out = NULL;
    AVFilterContext *ctx = inlink->dst;
    TransposeVulkanContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];

    if (s->passthrough)
        return ff_filter_frame(outlink, in);

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    if (!s->initialized)
        RET(init_filter(ctx, in));

    RET(process_frames(ctx, out, in));

    RET(av_frame_copy_props(out, in));

    if (in->sample_aspect_ratio.num)
        out->sample_aspect_ratio = in->sample_aspect_ratio;
    else {
        out->sample_aspect_ratio.num = in->sample_aspect_ratio.den;
        out->sample_aspect_ratio.den = in->sample_aspect_ratio.num;
    }

    av_frame_free(&in);

    return ff_filter_frame(outlink, out);

fail:
    av_frame_free(&in);
    av_frame_free(&out);
    return err;
}

static av_cold void transpose_vulkan_uninit(AVFilterContext *avctx)
{
    TransposeVulkanContext *s = avctx->priv;
    ff_vk_uninit(&s->vkctx);

    s->initialized = 0;
}

static int config_props_output(AVFilterLink *outlink)
{
    AVFilterContext *avctx = outlink->src;
    TransposeVulkanContext *s = avctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;
    AVFilterLink *inlink = avctx->inputs[0];

    if ((inlink->w >= inlink->h && s->passthrough == TRANSPOSE_PT_TYPE_LANDSCAPE) ||
        (inlink->w <= inlink->h && s->passthrough == TRANSPOSE_PT_TYPE_PORTRAIT)) {
        av_log(avctx, AV_LOG_VERBOSE,
               "w:%d h:%d -> w:%d h:%d (passthrough mode)\n",
               inlink->w, inlink->h, inlink->w, inlink->h);
        outlink->hw_frames_ctx = av_buffer_ref(inlink->hw_frames_ctx);
        return outlink->hw_frames_ctx ? 0 : AVERROR(ENOMEM);
    } else {
        s->passthrough = TRANSPOSE_PT_TYPE_NONE;
    }

    vkctx->output_width  = inlink->h;
    vkctx->output_height = inlink->w;

    if (inlink->sample_aspect_ratio.num)
        outlink->sample_aspect_ratio = av_div_q((AVRational) { 1, 1 },
                                                inlink->sample_aspect_ratio);
    else
        outlink->sample_aspect_ratio = inlink->sample_aspect_ratio;

    return ff_vk_filter_config_output(outlink);
}

#define OFFSET(x) offsetof(TransposeVulkanContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)

static const AVOption transpose_vulkan_options[] = {
    { "dir", "set transpose direction", OFFSET(dir), AV_OPT_TYPE_INT, { .i64 = TRANSPOSE_CCLOCK_FLIP }, 0, 7, FLAGS, "dir" },
        { "cclock_flip", "rotate counter-clockwise with vertical flip", 0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_CCLOCK_FLIP }, .flags=FLAGS, .unit = "dir" },
        { "clock",       "rotate clockwise",                            0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_CLOCK       }, .flags=FLAGS, .unit = "dir" },
        { "cclock",      "rotate counter-clockwise",                    0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_CCLOCK      }, .flags=FLAGS, .unit = "dir" },
        { "clock_flip",  "rotate clockwise with vertical flip",         0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_CLOCK_FLIP  }, .flags=FLAGS, .unit = "dir" },

    { "passthrough", "do not apply transposition if the input matches the specified geometry",
      OFFSET(passthrough), AV_OPT_TYPE_INT, {.i64=TRANSPOSE_PT_TYPE_NONE},  0, INT_MAX, FLAGS, "passthrough" },
        { "none",      "always apply transposition",   0, AV_OPT_TYPE_CONST, {.i64=TRANSPOSE_PT_TYPE_NONE},      INT_MIN, INT_MAX, FLAGS, "passthrough" },
        { "portrait",  "preserve portrait geometry",   0, AV_OPT_TYPE_CONST, {.i64=TRANSPOSE_PT_TYPE_PORTRAIT},  INT_MIN, INT_MAX, FLAGS, "passthrough" },
        { "landscape", "preserve landscape geometry",  0, AV_OPT_TYPE_CONST, {.i64=TRANSPOSE_PT_TYPE_LANDSCAPE}, INT_MIN, INT_MAX, FLAGS, "passthrough" },

    { NULL }
};

AVFILTER_DEFINE_CLASS(transpose_vulkan);

static const AVFilterPad transpose_vulkan_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = &filter_frame,
        .config_props = &ff_vk_filter_config_input,
    }
};

static const AVFilterPad transpose_vulkan_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = &config_props_output,
    }
};

const AVFilter ff_vf_transpose_vulkan = {
    .name           = "transpose_vulkan",
    .description    = NULL_IF_CONFIG_SMALL("Transpose Vulkan Filter"),
    .priv_size      = sizeof(TransposeVulkanContext),
    .init           = &ff_vk_filter_init,
    .uninit         = &transpose_vulkan_uninit,
    FILTER_INPUTS(transpose_vulkan_inputs),
    FILTER_OUTPUTS(transpose_vulkan_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_VULKAN),
    .priv_class     = &transpose_vulkan_class,
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
