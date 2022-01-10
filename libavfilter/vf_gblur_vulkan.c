/*
 * copyright (c) 2021-2022 Wu Jianhua <jianhua.wu@intel.com>
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
#define GBLUR_MAX_KERNEL_SIZE 127

typedef struct GBlurVulkanContext {
    FFVulkanContext vkctx;
    FFVkQueueFamilyCtx qf;
    FFVkExecContext *exec;
    FFVulkanPipeline *pl_hor;
    FFVulkanPipeline *pl_ver;
    FFVkBuffer params_buf_hor;
    FFVkBuffer params_buf_ver;

    VkDescriptorImageInfo input_images[3];
    VkDescriptorImageInfo tmp_images[3];
    VkDescriptorImageInfo output_images[3];
    VkDescriptorBufferInfo params_desc_hor;
    VkDescriptorBufferInfo params_desc_ver;

    int initialized;
    int size;
    int sizeV;
    int planes;
    float sigma;
    float sigmaV;
    AVFrame *tmpframe;
} GBlurVulkanContext;

static const char gblur_func[] = {
    C(0, void gblur(const ivec2 pos, const int index)                           )
    C(0, {                                                                      )
    C(1,     vec4 sum = texture(input_images[index], pos) * kernel[0];          )
    C(0,                                                                        )
    C(1,     for(int i = 1; i < kernel.length(); i++) {                         )
    C(2,         sum += texture(input_images[index], pos + OFFSET) * kernel[i]; )
    C(2,         sum += texture(input_images[index], pos - OFFSET) * kernel[i]; )
    C(1,     }                                                                  )
    C(0,                                                                        )
    C(1,     imageStore(output_images[index], pos, sum);                        )
    C(0, }                                                                      )
};

static inline float gaussian(float sigma, float x)
{
    return 1.0 / (sqrt(2.0 * M_PI) * sigma) *
           exp(-(x * x) / (2.0 * sigma * sigma));
}

static inline float gaussian_simpson_integration(float sigma, float a, float b)
{
    return (b - a) * (1.0 / 6.0) * ((gaussian(sigma, a) +
           4.0 * gaussian(sigma, (a + b) * 0.5) + gaussian(sigma, b)));
}

static void init_gaussian_kernel(float *kernel, float sigma, float kernel_size)
{
    int x;
    float sum;

    sum = 0;
    for (x = 0; x < kernel_size; x++) {
        kernel[x] = gaussian_simpson_integration(sigma, x - 0.5f, x + 0.5f);
        if (!x)
            sum += kernel[x];
        else
            sum += kernel[x] * 2.0;
    }
    /* Normalized */
    sum = 1.0 / sum;
    for (x = 0; x < kernel_size; x++) {
        kernel[x] *= sum;
    }
}

static inline void init_kernel_size(GBlurVulkanContext *s, int *out_size)
{
    int size = *out_size;

    if (!(size & 1)) {
        av_log(s, AV_LOG_WARNING, "The kernel size should be odd\n");
        size++;
    }

    *out_size = (size >> 1) + 1;
}

static av_cold void init_gaussian_params(GBlurVulkanContext *s)
{
    if (s->sigmaV <= 0)
        s->sigmaV = s->sigma;

    init_kernel_size(s, &s->size);

    if (s->sizeV <= 0)
        s->sizeV = s->size;
    else
        init_kernel_size(s, &s->sizeV);

    s->tmpframe = NULL;
}

static int init_gblur_pipeline(GBlurVulkanContext *s, FFVulkanPipeline *pl, FFVkSPIRVShader *shd,
                               FFVkBuffer *params_buf, VkDescriptorBufferInfo *params_desc,
                               int ksize, float sigma)
{
    int err = 0;
    uint8_t *kernel_mapped;

    const int planes = av_pix_fmt_count_planes(s->vkctx.output_format);

    FFVulkanDescriptorSetBinding buf_desc = {
        .name        = "data",
        .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .mem_quali   = "readonly",
        .mem_layout  = "std430",
        .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
        .updater     = NULL,
        .buf_content = NULL,
    };

    char *kernel_def = av_asprintf("float kernel[%i];", ksize);
    if (!kernel_def)
        return AVERROR(ENOMEM);

    buf_desc.updater = params_desc;
    buf_desc.buf_content = kernel_def;

    RET(ff_vk_add_descriptor_set(&s->vkctx, pl, shd, &buf_desc, 1, 0));

    GLSLD(   gblur_func                                               );
    GLSLC(0, void main()                                              );
    GLSLC(0, {                                                        );
    GLSLC(1,     ivec2 size;                                          );
    GLSLC(1,     const ivec2 pos = ivec2(gl_GlobalInvocationID.xy);   );
    for (int i = 0; i < planes; i++) {
        GLSLC(0,                                                      );
        GLSLF(1,  size = imageSize(output_images[%i]);               ,i);
        GLSLC(1,  if (IS_WITHIN(pos, size)) {                         );
        if (s->planes & (1 << i)) {
            GLSLF(2,      gblur(pos, %i);                           ,i);
        } else {
            GLSLF(2, vec4 res = texture(input_images[%i], pos);      ,i);
            GLSLF(2, imageStore(output_images[%i], pos, res);        ,i);
        }
        GLSLC(1, }                                                    );
    }
    GLSLC(0, }                                                        );

    RET(ff_vk_compile_shader(&s->vkctx, shd, "main"));

    RET(ff_vk_init_pipeline_layout(&s->vkctx, pl));
    RET(ff_vk_init_compute_pipeline(&s->vkctx, pl));

    RET(ff_vk_create_buf(&s->vkctx, params_buf, sizeof(float) * ksize,
                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT));
    RET(ff_vk_map_buffers(&s->vkctx, params_buf, &kernel_mapped, 1, 0));

    init_gaussian_kernel((float *)kernel_mapped, sigma, ksize);

    RET(ff_vk_unmap_buffers(&s->vkctx, params_buf, 1, 1));

    params_desc->buffer = params_buf->buf;
    params_desc->range  = VK_WHOLE_SIZE;

    ff_vk_update_descriptor_set(&s->vkctx, pl, 1);

fail:
    av_free(kernel_def);
    return err;
}

static av_cold int init_filter(AVFilterContext *ctx, AVFrame *in)
{
    int err = 0;
    GBlurVulkanContext *s = ctx->priv;
    FFVkSPIRVShader *shd;
    const int planes = av_pix_fmt_count_planes(s->vkctx.output_format);

    FFVulkanDescriptorSetBinding image_descs[] = {
        {
            .name       = "input_images",
            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .dimensions = 2,
            .elems      = planes,
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        {
            .name       = "output_images",
            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .mem_layout = ff_vk_shader_rep_fmt(s->vkctx.output_format),
            .mem_quali  = "writeonly",
            .dimensions = 2,
            .elems      = planes,
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };

    image_descs[0].sampler = ff_vk_init_sampler(&s->vkctx, 1, VK_FILTER_LINEAR);
    if (!image_descs[0].sampler)
        return AVERROR_EXTERNAL;

    init_gaussian_params(s);

    ff_vk_qf_init(&s->vkctx, &s->qf, VK_QUEUE_COMPUTE_BIT, 0);

    {
        /* Create shader for the horizontal pass */
        image_descs[0].updater = s->input_images;
        image_descs[1].updater = s->tmp_images;

        s->pl_hor = ff_vk_create_pipeline(&s->vkctx, &s->qf);
        if (!s->pl_hor) {
            err = AVERROR(ENOMEM);
            goto fail;
        }

        shd = ff_vk_init_shader(s->pl_hor, "gblur_compute_hor", image_descs[0].stages);
        if (!shd) {
            err = AVERROR(ENOMEM);
            goto fail;
        }

        ff_vk_set_compute_shader_sizes(shd, (int [3]){ CGS, 1, 1 });
        RET(ff_vk_add_descriptor_set(&s->vkctx, s->pl_hor, shd, image_descs, FF_ARRAY_ELEMS(image_descs), 0));

        GLSLC(0, #define OFFSET (vec2(i, 0.0)));
        RET(init_gblur_pipeline(s, s->pl_hor, shd, &s->params_buf_hor, &s->params_desc_hor,
                                s->size, s->sigma));
    }

    {
        /* Create shader for the vertical pass */
        image_descs[0].updater = s->tmp_images;
        image_descs[1].updater = s->output_images;

        s->pl_ver = ff_vk_create_pipeline(&s->vkctx, &s->qf);
        if (!s->pl_ver) {
            err = AVERROR(ENOMEM);
            goto fail;
        }

        shd = ff_vk_init_shader(s->pl_ver, "gblur_compute_ver", image_descs[0].stages);
        if (!shd) {
            err = AVERROR(ENOMEM);
            goto fail;
        }

        ff_vk_set_compute_shader_sizes(shd, (int [3]){ 1, CGS, 1 });
        RET(ff_vk_add_descriptor_set(&s->vkctx, s->pl_ver, shd, image_descs, FF_ARRAY_ELEMS(image_descs), 0));

        GLSLC(0, #define OFFSET (vec2(0.0, i)));
        RET(init_gblur_pipeline(s, s->pl_ver, shd, &s->params_buf_ver, &s->params_desc_ver,
                                s->sizeV, s->sigmaV));
    }

    RET(ff_vk_create_exec_ctx(&s->vkctx, &s->exec, &s->qf));

    s->initialized = 1;

fail:
    return err;
}

static av_cold void gblur_vulkan_uninit(AVFilterContext *avctx)
{
    GBlurVulkanContext *s = avctx->priv;

    av_frame_free(&s->tmpframe);

    ff_vk_free_buf(&s->vkctx, &s->params_buf_hor);
    ff_vk_free_buf(&s->vkctx, &s->params_buf_ver);
    ff_vk_uninit(&s->vkctx);

    s->initialized = 0;
}

static int process_frames(AVFilterContext *avctx, AVFrame *outframe, AVFrame *inframe)
{
    int err;
    VkCommandBuffer cmd_buf;
    GBlurVulkanContext *s = avctx->priv;
    FFVulkanFunctions *vk = &s->vkctx.vkfn;

    const int planes = av_pix_fmt_count_planes(s->vkctx.output_format);

    AVVkFrame *in  = (AVVkFrame *)inframe->data[0];
    AVVkFrame *out = (AVVkFrame *)outframe->data[0];
    AVVkFrame *tmp = (AVVkFrame *)s->tmpframe->data[0];

    const VkFormat *input_formats  = av_vkfmt_from_pixfmt(s->vkctx.input_format);
    const VkFormat *output_formats = av_vkfmt_from_pixfmt(s->vkctx.output_format);

    ff_vk_start_exec_recording(&s->vkctx, s->exec);
    cmd_buf = ff_vk_get_exec_buf(s->exec);

    for (int i = 0; i < planes; i++) {
        RET(ff_vk_create_imageview(&s->vkctx, s->exec, &s->input_images[i].imageView,
                                   in->img[i],
                                   input_formats[i],
                                   ff_comp_identity_map));

        RET(ff_vk_create_imageview(&s->vkctx, s->exec, &s->tmp_images[i].imageView,
                                   tmp->img[i],
                                   output_formats[i],
                                   ff_comp_identity_map));

        RET(ff_vk_create_imageview(&s->vkctx, s->exec, &s->output_images[i].imageView,
                                   out->img[i],
                                   output_formats[i],
                                   ff_comp_identity_map));

        s->input_images[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        s->tmp_images[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        s->output_images[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    }

    ff_vk_update_descriptor_set(&s->vkctx, s->pl_hor, 0);
    ff_vk_update_descriptor_set(&s->vkctx, s->pl_ver, 0);

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
                .dstAccessMask               = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
                .oldLayout                   = tmp->layout[i],
                .newLayout                   = s->tmp_images[i].imageLayout,
                .srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED,
                .image                       = tmp->img[i],
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

        tmp->layout[i] = barriers[1].newLayout;
        tmp->access[i] = barriers[1].dstAccessMask;

        out->layout[i] = barriers[2].newLayout;
        out->access[i] = barriers[2].dstAccessMask;
    }

    ff_vk_bind_pipeline_exec(&s->vkctx, s->exec, s->pl_hor);

    vk->CmdDispatch(cmd_buf, FFALIGN(s->vkctx.output_width, CGS)/CGS,
                    s->vkctx.output_height, 1);

    ff_vk_bind_pipeline_exec(&s->vkctx, s->exec, s->pl_ver);

    vk->CmdDispatch(cmd_buf,s->vkctx.output_width,
                    FFALIGN(s->vkctx.output_height, CGS)/CGS, 1);

    ff_vk_add_exec_dep(&s->vkctx, s->exec, inframe, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    ff_vk_add_exec_dep(&s->vkctx, s->exec, outframe, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);

    err = ff_vk_submit_exec_queue(&s->vkctx, s->exec);
    if (err)
        return err;

    ff_vk_qf_rotate(&s->qf);

    return 0;

fail:
    ff_vk_discard_exec_deps(s->exec);
    return err;
}

static int gblur_vulkan_filter_frame(AVFilterLink *link, AVFrame *in)
{
    int err;
    AVFrame *out = NULL;
    AVFilterContext *ctx = link->dst;
    GBlurVulkanContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    if (!s->initialized) {
        RET(init_filter(ctx, in));
        s->tmpframe = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!s->tmpframe) {
            err = AVERROR(ENOMEM);
            goto fail;
        }
    }

    RET(process_frames(ctx, out, in));

    RET(av_frame_copy_props(out, in));

    av_frame_free(&in);

    return ff_filter_frame(outlink, out);

fail:
    av_frame_free(&in);
    av_frame_free(&out);
    av_frame_free(&s->tmpframe);

    return err;
}

#define OFFSET(x) offsetof(GBlurVulkanContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)
static const AVOption gblur_vulkan_options[] = {
    { "sigma",  "Set sigma",                OFFSET(sigma),  AV_OPT_TYPE_FLOAT, { .dbl = 0.5 }, 0.01, 1024.0,                FLAGS },
    { "sigmaV", "Set vertical sigma",       OFFSET(sigmaV), AV_OPT_TYPE_FLOAT, { .dbl = 0   }, 0.0,  1024.0,                FLAGS },
    { "planes", "Set planes to filter",     OFFSET(planes), AV_OPT_TYPE_INT,   { .i64 = 0xF }, 0,    0xF,                   FLAGS },
    { "size",   "Set kernel size",          OFFSET(size),   AV_OPT_TYPE_INT,   { .i64 = 19  }, 1,    GBLUR_MAX_KERNEL_SIZE, FLAGS },
    { "sizeV",  "Set vertical kernel size", OFFSET(sizeV),  AV_OPT_TYPE_INT,   { .i64 = 0   }, 0,    GBLUR_MAX_KERNEL_SIZE, FLAGS },
    { NULL },
};

AVFILTER_DEFINE_CLASS(gblur_vulkan);

static const AVFilterPad gblur_vulkan_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = &gblur_vulkan_filter_frame,
        .config_props = &ff_vk_filter_config_input,
    }
};

static const AVFilterPad gblur_vulkan_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = &ff_vk_filter_config_output,
    }
};

const AVFilter ff_vf_gblur_vulkan = {
    .name           = "gblur_vulkan",
    .description    = NULL_IF_CONFIG_SMALL("Gaussian Blur in Vulkan"),
    .priv_size      = sizeof(GBlurVulkanContext),
    .init           = &ff_vk_filter_init,
    .uninit         = &gblur_vulkan_uninit,
    FILTER_INPUTS(gblur_vulkan_inputs),
    FILTER_OUTPUTS(gblur_vulkan_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_VULKAN),
    .priv_class     = &gblur_vulkan_class,
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
