/*
 * copyright (c) 2021-2022 Wu Jianhua <jianhua.wu@intel.com>
 * The blend modes are based on the blend.c.
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
#include "internal.h"
#include "framesync.h"
#include "blend.h"

#define CGS 32

#define IN_TOP    0
#define IN_BOTTOM 1

typedef struct FilterParamsVulkan {
    const char *blend;
    const char *blend_func;
    double opacity;
    enum BlendMode mode;
} FilterParamsVulkan;

typedef struct BlendVulkanContext {
    FFVulkanContext vkctx;
    FFVkQueueFamilyCtx qf;
    FFVkExecContext *exec;
    FFVulkanPipeline *pl;
    FFFrameSync fs;

    VkDescriptorImageInfo top_images[3];
    VkDescriptorImageInfo bottom_images[3];
    VkDescriptorImageInfo output_images[3];

    FilterParamsVulkan params[4];
    double all_opacity;
    enum BlendMode all_mode;

    int initialized;
} BlendVulkanContext;

#define DEFINE_BLEND_MODE(MODE, EXPR) \
static const char blend_##MODE[] = "blend_"#MODE; \
static const char blend_##MODE##_func[] = { \
    C(0, vec4 blend_##MODE(vec4 top, vec4 bottom, float opacity) {   ) \
    C(1,     vec4 dst = EXPR;                                        ) \
    C(1,     return dst;                                             ) \
    C(0, }                                                           ) \
};

#define A top
#define B bottom

#define FN(EXPR) A + ((EXPR) - A) * opacity

DEFINE_BLEND_MODE(NORMAL, A * opacity + B * (1.0f - opacity))
DEFINE_BLEND_MODE(MULTIPLY, FN(1.0f * A * B / 1.0f))

static inline void init_blend_func(FilterParamsVulkan *param)
{
#define CASE(MODE) case BLEND_##MODE: \
            param->blend = blend_##MODE;\
            param->blend_func =  blend_##MODE##_func; \
            break;

    switch (param->mode) {
    CASE(NORMAL)
    CASE(MULTIPLY)
    default: param->blend = NULL; break;
    }

#undef CASE
}

static int config_params(AVFilterContext *avctx)
{
    BlendVulkanContext *s = avctx->priv;

    for (int plane = 0; plane < FF_ARRAY_ELEMS(s->params); plane++) {
        FilterParamsVulkan *param = &s->params[plane];

        if (s->all_mode >= 0)
            param->mode = s->all_mode;
        if (s->all_opacity < 1)
            param->opacity = s->all_opacity;

        init_blend_func(param);
        if (!param->blend) {
            av_log(avctx, AV_LOG_ERROR,
                   "Currently the blend mode specified is not supported yet.\n");
            return AVERROR(EINVAL);
        }
    }

    return 0;
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    int ret = ff_filter_process_command(ctx, cmd, args, res, res_len, flags);
    if (ret < 0)
        return ret;

    return config_params(ctx);
}

static av_cold int init_filter(AVFilterContext *avctx)
{
    int err = 0;
    FFVkSampler *sampler;
    FFVkSPIRVShader *shd;
    BlendVulkanContext *s = avctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;
    const int planes = av_pix_fmt_count_planes(s->vkctx.output_format);

    ff_vk_qf_init(vkctx, &s->qf, VK_QUEUE_COMPUTE_BIT, 0);

    sampler = ff_vk_init_sampler(vkctx, 1, VK_FILTER_LINEAR);
    if (!sampler)
        return AVERROR_EXTERNAL;

    s->pl = ff_vk_create_pipeline(vkctx, &s->qf);
    if (!s->pl)
        return AVERROR(ENOMEM);

    {
        FFVulkanDescriptorSetBinding image_descs[] = {
            {
                .name       = "top_images",
                .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .dimensions = 2,
                .elems      = planes,
                .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
                .updater    = s->top_images,
                .sampler    = sampler,
            },
            {
                .name       = "bottom_images",
                .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .dimensions = 2,
                .elems      = planes,
                .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
                .updater    = s->bottom_images,
                .sampler    = sampler,
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

        shd = ff_vk_init_shader(s->pl, "blend_compute", image_descs[0].stages);
        if (!shd)
            return AVERROR(ENOMEM);

        ff_vk_set_compute_shader_sizes(shd, (int [3]){ CGS, CGS, 1 });
        RET(ff_vk_add_descriptor_set(vkctx, s->pl, shd, image_descs, FF_ARRAY_ELEMS(image_descs), 0));

        for (int i = 0, j = 0; i < planes; i++) {
            for (j = 0; j < i; j++)
                if (s->params[i].blend_func == s->params[j].blend_func)
                    break;
            /* note: the bracket is needed, for GLSLD is a macro with multiple statements. */
            if (j == i) {
                GLSLD(s->params[i].blend_func);
            }
        }

        GLSLC(0, void main()                                                    );
        GLSLC(0, {                                                              );
        GLSLC(1,     ivec2 size;                                                );
        GLSLC(1,     const ivec2 pos = ivec2(gl_GlobalInvocationID.xy);         );
        for (int i = 0; i < planes; i++) {
            GLSLC(0,                                                            );
            GLSLF(1, size = imageSize(output_images[%i]);                     ,i);
            GLSLC(1, if (IS_WITHIN(pos, size)) {                                );
            GLSLF(2,     const vec4 top = texture(top_images[%i], pos);       ,i);
            GLSLF(2,     const vec4 bottom = texture(bottom_images[%i], pos); ,i);
            GLSLF(2,     const float opacity = %f;                            ,s->params[i].opacity);
            GLSLF(2,     vec4 dst = %s(top, bottom, opacity);                 ,s->params[i].blend);
            GLSLC(0,                                                            );
            GLSLF(2,     imageStore(output_images[%i], pos, dst);             ,i);
            GLSLC(1, }                                                          );
        }
        GLSLC(0, }                                                              );

        RET(ff_vk_compile_shader(vkctx, shd, "main"));
        RET(ff_vk_init_pipeline_layout(vkctx, s->pl));
        RET(ff_vk_init_compute_pipeline(vkctx, s->pl));
    }

    RET(ff_vk_create_exec_ctx(vkctx, &s->exec, &s->qf));

    s->initialized = 1;

fail:
    return err;
}

static int process_frames(AVFilterContext *avctx, AVFrame *out_frame, AVFrame *top_frame, AVFrame *bottom_frame)
{
    int err = 0;
    VkCommandBuffer cmd_buf;
    BlendVulkanContext *s = avctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;
    FFVulkanFunctions *vk = &s->vkctx.vkfn;
    const int planes = av_pix_fmt_count_planes(s->vkctx.output_format);

    AVVkFrame *out    = (AVVkFrame *)out_frame->data[0];
    AVVkFrame *top    = (AVVkFrame *)top_frame->data[0];
    AVVkFrame *bottom = (AVVkFrame *)bottom_frame->data[0];

    AVHWFramesContext *top_fc    = (AVHWFramesContext*)top_frame->hw_frames_ctx->data;
    AVHWFramesContext *bottom_fc = (AVHWFramesContext*)bottom_frame->hw_frames_ctx->data;

    const VkFormat *top_formats    = av_vkfmt_from_pixfmt(top_fc->sw_format);
    const VkFormat *bottom_formats = av_vkfmt_from_pixfmt(bottom_fc->sw_format);
    const VkFormat *output_formats = av_vkfmt_from_pixfmt(s->vkctx.output_format);

    ff_vk_start_exec_recording(vkctx, s->exec);
    cmd_buf = ff_vk_get_exec_buf(s->exec);

    for (int i = 0; i < planes; i++) {
        RET(ff_vk_create_imageview(vkctx, s->exec,
                                   &s->top_images[i].imageView, top->img[i],
                                   top_formats[i],
                                   ff_comp_identity_map));

        RET(ff_vk_create_imageview(vkctx, s->exec,
                                   &s->bottom_images[i].imageView, bottom->img[i],
                                   bottom_formats[i],
                                   ff_comp_identity_map));

        RET(ff_vk_create_imageview(vkctx, s->exec,
                                   &s->output_images[i].imageView, out->img[i],
                                   output_formats[i],
                                   ff_comp_identity_map));

        s->top_images[i].imageLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        s->bottom_images[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        s->output_images[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    }

    ff_vk_update_descriptor_set(vkctx, s->pl, 0);

    for (int i = 0; i < planes; i++) {
        VkImageMemoryBarrier barriers[] = {
            {
                .sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask               = 0,
                .dstAccessMask               = VK_ACCESS_SHADER_READ_BIT,
                .oldLayout                   = top->layout[i],
                .newLayout                   = s->top_images[i].imageLayout,
                .srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED,
                .image                       = top->img[i],
                .subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .subresourceRange.levelCount = 1,
                .subresourceRange.layerCount = 1,
            },
            {
                .sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask               = 0,
                .dstAccessMask               = VK_ACCESS_SHADER_READ_BIT,
                .oldLayout                   = bottom->layout[i],
                .newLayout                   = s->bottom_images[i].imageLayout,
                .srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED,
                .image                       = bottom->img[i],
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

        top->layout[i] = barriers[0].newLayout;
        top->access[i] = barriers[0].dstAccessMask;

        bottom->layout[i] = barriers[1].newLayout;
        bottom->access[i] = barriers[1].dstAccessMask;

        out->layout[i] = barriers[2].newLayout;
        out->access[i] = barriers[2].dstAccessMask;
    }

    ff_vk_bind_pipeline_exec(vkctx, s->exec, s->pl);
    vk->CmdDispatch(cmd_buf, FFALIGN(s->vkctx.output_width, CGS) / CGS,
                    FFALIGN(s->vkctx.output_height, CGS) / CGS, 1);

    ff_vk_add_exec_dep(vkctx, s->exec, top_frame, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    ff_vk_add_exec_dep(vkctx, s->exec, bottom_frame, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    ff_vk_add_exec_dep(vkctx, s->exec, out_frame, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);

    err = ff_vk_submit_exec_queue(vkctx, s->exec);
    if (err)
        return err;

    ff_vk_qf_rotate(&s->qf);

    return 0;

fail:
    ff_vk_discard_exec_deps(s->exec);
    return err;
}

static int blend_frame(FFFrameSync *fs)
{
    int err;
    AVFilterContext *avctx = fs->parent;
    BlendVulkanContext *s = avctx->priv;
    AVFilterLink *outlink = avctx->outputs[0];
    AVFrame *top, *bottom, *out;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    RET(ff_framesync_get_frame(fs, IN_TOP,    &top,    0));
    RET(ff_framesync_get_frame(fs, IN_BOTTOM, &bottom, 0));

    RET(av_frame_copy_props(out, top));

    if (!s->initialized) {
        AVHWFramesContext *top_fc = (AVHWFramesContext*)top->hw_frames_ctx->data;
        AVHWFramesContext *bottom_fc = (AVHWFramesContext*)bottom->hw_frames_ctx->data;
        if (top_fc->sw_format != bottom_fc->sw_format) {
            av_log(avctx, AV_LOG_ERROR,
                   "Currently the sw format of the bottom video need to match the top!\n");
            return AVERROR(EINVAL);
        }
        RET(init_filter(avctx));
    }

    RET(process_frames(avctx, out, top, bottom));

    return ff_filter_frame(outlink, out);

fail:
    av_frame_free(&out);
    return err;
}

static av_cold int init(AVFilterContext *avctx)
{
    BlendVulkanContext *s = avctx->priv;

    s->fs.on_event = blend_frame;

    return ff_vk_filter_init(avctx);
}

static av_cold void uninit(AVFilterContext *avctx)
{
    BlendVulkanContext *s = avctx->priv;

    ff_framesync_uninit(&s->fs);

    ff_vk_uninit(&s->vkctx);

    s->initialized = 0;
}

static int config_props_output(AVFilterLink *outlink)
{
    int err;
    AVFilterContext *avctx = outlink->src;
    BlendVulkanContext *s = avctx->priv;
    AVFilterLink *toplink = avctx->inputs[IN_TOP];
    AVFilterLink *bottomlink = avctx->inputs[IN_BOTTOM];

    if (toplink->w != bottomlink->w || toplink->h != bottomlink->h) {
        av_log(avctx, AV_LOG_ERROR, "First input link %s parameters "
                "(size %dx%d) do not match the corresponding "
                "second input link %s parameters (size %dx%d)\n",
                avctx->input_pads[IN_TOP].name, toplink->w, toplink->h,
                avctx->input_pads[IN_BOTTOM].name, bottomlink->w, bottomlink->h);
        return AVERROR(EINVAL);
    }

    outlink->sample_aspect_ratio = toplink->sample_aspect_ratio;
    outlink->frame_rate = toplink->frame_rate;

    RET(ff_vk_filter_config_output(outlink));

    RET(ff_framesync_init_dualinput(&s->fs, avctx));

    RET(ff_framesync_configure(&s->fs));
    outlink->time_base = s->fs.time_base;

    RET(config_params(avctx));

fail:
    return err;
}

static int activate(AVFilterContext *avctx)
{
    BlendVulkanContext *s = avctx->priv;
    return ff_framesync_activate(&s->fs);
}

#define OFFSET(x) offsetof(BlendVulkanContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)

static const AVOption blend_vulkan_options[] = {
    { "c0_mode", "set component #0 blend mode", OFFSET(params[0].mode), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, BLEND_NB - 1, FLAGS, "mode" },
    { "c1_mode", "set component #1 blend mode", OFFSET(params[1].mode), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, BLEND_NB - 1, FLAGS, "mode" },
    { "c2_mode", "set component #2 blend mode", OFFSET(params[2].mode), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, BLEND_NB - 1, FLAGS, "mode" },
    { "c3_mode", "set component #3 blend mode", OFFSET(params[3].mode), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, BLEND_NB - 1, FLAGS, "mode" },
    { "all_mode", "set blend mode for all components", OFFSET(all_mode), AV_OPT_TYPE_INT, { .i64 = -1 }, -1, BLEND_NB - 1, FLAGS, "mode" },
        { "normal",   "", 0, AV_OPT_TYPE_CONST, { .i64 = BLEND_NORMAL   }, 0, 0, FLAGS, "mode" },
        { "multiply", "", 0, AV_OPT_TYPE_CONST, { .i64 = BLEND_MULTIPLY }, 0, 0, FLAGS, "mode" },

    { "c0_opacity",  "set color component #0 opacity", OFFSET(params[0].opacity), AV_OPT_TYPE_DOUBLE, { .dbl = 1 }, 0, 1, FLAGS },
    { "c1_opacity",  "set color component #1 opacity", OFFSET(params[1].opacity), AV_OPT_TYPE_DOUBLE, { .dbl = 1 }, 0, 1, FLAGS },
    { "c2_opacity",  "set color component #2 opacity", OFFSET(params[2].opacity), AV_OPT_TYPE_DOUBLE, { .dbl = 1 }, 0, 1, FLAGS },
    { "c3_opacity",  "set color component #3 opacity", OFFSET(params[3].opacity), AV_OPT_TYPE_DOUBLE, { .dbl = 1 }, 0, 1, FLAGS },
    { "all_opacity", "set opacity for all color components", OFFSET(all_opacity), AV_OPT_TYPE_DOUBLE, { .dbl = 1 }, 0, 1, FLAGS },

    { NULL }
};

AVFILTER_DEFINE_CLASS(blend_vulkan);

static const AVFilterPad blend_vulkan_inputs[] = {
    {
        .name         = "top",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = &ff_vk_filter_config_input,
    },
    {
        .name         = "bottom",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = &ff_vk_filter_config_input,
    },
};


static const AVFilterPad blend_vulkan_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = &config_props_output,
    }
};

const AVFilter ff_vf_blend_vulkan = {
    .name            = "blend_vulkan",
    .description     = NULL_IF_CONFIG_SMALL("Blend two video frames in Vulkan"),
    .priv_size       = sizeof(BlendVulkanContext),
    .init            = &init,
    .uninit          = &uninit,
    .activate        = &activate,
    FILTER_INPUTS(blend_vulkan_inputs),
    FILTER_OUTPUTS(blend_vulkan_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_VULKAN),
    .priv_class      = &blend_vulkan_class,
    .flags_internal  = FF_FILTER_FLAG_HWFRAME_AWARE,
    .process_command = &process_command,
};
