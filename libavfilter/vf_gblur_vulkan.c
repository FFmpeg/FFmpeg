/*
 * copyright (c) 2021-2022 Wu Jianhua <jianhua.wu@intel.com>
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
#include "vulkan_spirv.h"
#include "internal.h"
#include "video.h"

#define CGS 32
#define GBLUR_MAX_KERNEL_SIZE 127

typedef struct GBlurVulkanContext {
    FFVulkanContext vkctx;

    int initialized;
    FFVkExecPool e;
    FFVkQueueFamilyCtx qf;
    VkSampler sampler;
    FFVulkanPipeline pl_hor;
    FFVkSPIRVShader shd_hor;
    FFVkBuffer params_hor;
    FFVulkanPipeline pl_ver;
    FFVkSPIRVShader shd_ver;
    FFVkBuffer params_ver;

    int size;
    int sizeV;
    int planes;
    float sigma;
    float sigmaV;
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
}

static int init_gblur_pipeline(GBlurVulkanContext *s, FFVulkanPipeline *pl,
                               FFVkSPIRVShader *shd, FFVkBuffer *params_buf,
                               int ksize, float sigma, FFVkSPIRVCompiler *spv)
{
    int err = 0;
    uint8_t *kernel_mapped;
    uint8_t *spv_data;
    size_t spv_len;
    void *spv_opaque = NULL;

    const int planes = av_pix_fmt_count_planes(s->vkctx.output_format);

    FFVulkanDescriptorSetBinding buf_desc = {
        .name        = "data",
        .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .mem_quali   = "readonly",
        .mem_layout  = "std430",
        .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
        .buf_content = NULL,
    };

    char *kernel_def = av_asprintf("float kernel[%i];", ksize);
    if (!kernel_def)
        return AVERROR(ENOMEM);

    buf_desc.buf_content = kernel_def;

    RET(ff_vk_pipeline_descriptor_set_add(&s->vkctx, pl, shd, &buf_desc, 1, 1, 0));

    GLSLD(   gblur_func                                               );
    GLSLC(0, void main()                                              );
    GLSLC(0, {                                                        );
    GLSLC(1,     ivec2 size;                                          );
    GLSLC(1,     const ivec2 pos = ivec2(gl_GlobalInvocationID.xy);   );
    for (int i = 0; i < planes; i++) {
        GLSLC(0,                                                      );
        GLSLF(1,  size = imageSize(output_images[%i]);              ,i);
        GLSLC(1,  if (!IS_WITHIN(pos, size))                          );
        GLSLC(2,      return;                                         );
        if (s->planes & (1 << i)) {
            GLSLF(1,      gblur(pos, %i);                           ,i);
        } else {
            GLSLF(1, vec4 res = texture(input_images[%i], pos);     ,i);
            GLSLF(1, imageStore(output_images[%i], pos, res);       ,i);
        }
    }
    GLSLC(0, }                                                        );

    RET(spv->compile_shader(spv, s, shd, &spv_data, &spv_len, "main",
                            &spv_opaque));
    RET(ff_vk_shader_create(&s->vkctx, shd, spv_data, spv_len, "main"));

    RET(ff_vk_init_compute_pipeline(&s->vkctx, pl, shd));
    RET(ff_vk_exec_pipeline_register(&s->vkctx, &s->e, pl));

    RET(ff_vk_create_buf(&s->vkctx, params_buf, sizeof(float) * ksize, NULL, NULL,
                         VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT));
    RET(ff_vk_map_buffer(&s->vkctx, params_buf, &kernel_mapped, 0));

    init_gaussian_kernel((float *)kernel_mapped, sigma, ksize);

    RET(ff_vk_unmap_buffer(&s->vkctx, params_buf, 1));

    RET(ff_vk_set_descriptor_buffer(&s->vkctx, pl, NULL, 1, 0, 0,
                                    params_buf->address, params_buf->size,
                                    VK_FORMAT_UNDEFINED));

fail:
    av_free(kernel_def);
    if (spv_opaque)
        spv->free_shader(spv, &spv_opaque);
    return err;
}

static av_cold int init_filter(AVFilterContext *ctx, AVFrame *in)
{
    int err = 0;
    GBlurVulkanContext *s = ctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;
    const int planes = av_pix_fmt_count_planes(s->vkctx.output_format);

    FFVkSPIRVShader *shd;
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
    RET(ff_vk_shader_init(&s->pl_hor, &s->shd_hor, "gblur_hor_compute",
                          VK_SHADER_STAGE_COMPUTE_BIT, 0));
    RET(ff_vk_shader_init(&s->pl_ver, &s->shd_ver, "gblur_ver_compute",
                          VK_SHADER_STAGE_COMPUTE_BIT, 0));

    desc = (FFVulkanDescriptorSetBinding []) {
        {
            .name       = "input_images",
            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .dimensions = 2,
            .elems      = planes,
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
            .samplers   = DUP_SAMPLER(s->sampler),
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

    init_gaussian_params(s);

    {
        shd = &s->shd_hor;
        ff_vk_shader_set_compute_sizes(shd, 32, 1, 1);

        RET(ff_vk_pipeline_descriptor_set_add(vkctx, &s->pl_hor, shd, desc, 2, 0, 0));

        GLSLC(0, #define OFFSET (vec2(i, 0.0)));
        RET(init_gblur_pipeline(s, &s->pl_hor, shd, &s->params_hor, s->size, s->sigma, spv));
    }

    {
        shd = &s->shd_ver;
        ff_vk_shader_set_compute_sizes(shd, 1, 32, 1);

        RET(ff_vk_pipeline_descriptor_set_add(vkctx, &s->pl_ver, shd, desc, 2, 0, 0));

        GLSLC(0, #define OFFSET (vec2(0.0, i)));
        RET(init_gblur_pipeline(s, &s->pl_ver, shd, &s->params_ver, s->sizeV, s->sigmaV, spv));
    }

    s->initialized = 1;

fail:
    if (spv)
        spv->uninit(&spv);

    return err;
}

static av_cold void gblur_vulkan_uninit(AVFilterContext *avctx)
{
    GBlurVulkanContext *s = avctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;
    FFVulkanFunctions *vk = &vkctx->vkfn;

    ff_vk_exec_pool_free(vkctx, &s->e);
    ff_vk_pipeline_free(vkctx, &s->pl_hor);
    ff_vk_pipeline_free(vkctx, &s->pl_ver);
    ff_vk_shader_free(vkctx, &s->shd_hor);
    ff_vk_shader_free(vkctx, &s->shd_ver);
    ff_vk_free_buf(vkctx, &s->params_hor);
    ff_vk_free_buf(vkctx, &s->params_ver);

    if (s->sampler)
        vk->DestroySampler(vkctx->hwctx->act_dev, s->sampler,
                           vkctx->hwctx->alloc);

    ff_vk_uninit(&s->vkctx);

    s->initialized = 0;
}

static int gblur_vulkan_filter_frame(AVFilterLink *link, AVFrame *in)
{
    int err;
    AVFrame *tmp = NULL, *out = NULL;
    AVFilterContext *ctx = link->dst;
    GBlurVulkanContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    tmp = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!tmp) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    if (!s->initialized)
        RET(init_filter(ctx, in));

    RET(ff_vk_filter_process_2pass(&s->vkctx, &s->e,
                                   (FFVulkanPipeline *[2]){ &s->pl_hor, &s->pl_ver },
                                   out, tmp, in, s->sampler, NULL, 0));

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
    .flags          = AVFILTER_FLAG_HWDEVICE,
};
