/*
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

#include "filters.h"
#include "video.h"

typedef struct AvgBlurVulkanContext {
    FFVulkanContext vkctx;

    int initialized;
    FFVkExecPool e;
    FFVkQueueFamilyCtx qf;
    VkSampler sampler;
    FFVulkanPipeline pl;
    FFVkSPIRVShader shd;

    /* Push constants / options */
    struct {
        float filter_norm[4];
        int32_t filter_len[2];
    } opts;

    int size_x;
    int size_y;
    int planes;
} AvgBlurVulkanContext;

static const char blur_kernel[] = {
    C(0, void distort(const ivec2 pos, const int idx)                         )
    C(0, {                                                                    )
    C(1,     vec4 sum = vec4(0);                                              )
    C(1,     for (int y = -filter_len.y; y <= filter_len.y; y++)              )
    C(1,        for (int x = -filter_len.x; x <= filter_len.x; x++)           )
    C(2,            sum += texture(input_img[idx], pos + ivec2(x, y));        )
    C(0,                                                                      )
    C(1,     imageStore(output_img[idx], pos, sum * filter_norm);             )
    C(0, }                                                                    )
};

static av_cold int init_filter(AVFilterContext *ctx, AVFrame *in)
{
    int err;
    uint8_t *spv_data;
    size_t spv_len;
    void *spv_opaque = NULL;
    AvgBlurVulkanContext *s = ctx->priv;
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
    RET(ff_vk_shader_init(&s->pl, &s->shd, "avgblur_compute",
                          VK_SHADER_STAGE_COMPUTE_BIT, 0));
    shd = &s->shd;

    ff_vk_shader_set_compute_sizes(shd, 32, 1, 1);

    desc = (FFVulkanDescriptorSetBinding []) {
        {
            .name       = "input_img",
            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .dimensions = 2,
            .elems      = planes,
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
            .samplers   = DUP_SAMPLER(s->sampler),
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

    RET(ff_vk_pipeline_descriptor_set_add(vkctx, &s->pl, shd, desc, 2, 0, 0));

    GLSLC(0, layout(push_constant, std430) uniform pushConstants {        );
    GLSLC(1,    vec4 filter_norm;                                         );
    GLSLC(1,    ivec2 filter_len;                                         );
    GLSLC(0, };                                                           );
    GLSLC(0,                                                              );

    ff_vk_add_push_constant(&s->pl, 0, sizeof(s->opts),
                            VK_SHADER_STAGE_COMPUTE_BIT);

    GLSLD(   blur_kernel                                                  );
    GLSLC(0, void main()                                                  );
    GLSLC(0, {                                                            );
    GLSLC(1,     ivec2 size;                                              );
    GLSLC(1,     const ivec2 pos = ivec2(gl_GlobalInvocationID.xy);       );
    for (int i = 0; i < planes; i++) {
        GLSLC(0,                                                          );
        GLSLF(1,  size = imageSize(output_img[%i]);                     ,i);
        GLSLC(1,  if (!IS_WITHIN(pos, size))                              );
        GLSLC(2,      return;                                             );
        if (s->planes & (1 << i)) {
            GLSLF(1, distort(pos, %i);                                  ,i);
        } else {
            GLSLF(1, vec4 res = texture(input_img[%i], pos);            ,i);
            GLSLF(1, imageStore(output_img[%i], pos, res);              ,i);
        }
    }
    GLSLC(0, }                                                            );

    RET(spv->compile_shader(spv, ctx, &s->shd, &spv_data, &spv_len, "main",
                            &spv_opaque));
    RET(ff_vk_shader_create(vkctx, &s->shd, spv_data, spv_len, "main"));

    RET(ff_vk_init_compute_pipeline(vkctx, &s->pl, &s->shd));
    RET(ff_vk_exec_pipeline_register(vkctx, &s->e, &s->pl));

    s->initialized = 1;
    s->opts.filter_len[0] = s->size_x - 1;
    s->opts.filter_len[1] = s->size_y - 1;

    s->opts.filter_norm[0] = s->opts.filter_len[0]*2 + 1;
    s->opts.filter_norm[0] = 1.0/(s->opts.filter_norm[0]*s->opts.filter_norm[0]);
    s->opts.filter_norm[1] = s->opts.filter_norm[0];
    s->opts.filter_norm[2] = s->opts.filter_norm[0];
    s->opts.filter_norm[3] = s->opts.filter_norm[0];

fail:
    if (spv_opaque)
        spv->free_shader(spv, &spv_opaque);
    if (spv)
        spv->uninit(&spv);

    return err;
}

static int avgblur_vulkan_filter_frame(AVFilterLink *link, AVFrame *in)
{
    int err;
    AVFrame *out = NULL;
    AVFilterContext *ctx = link->dst;
    AvgBlurVulkanContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    if (!s->initialized)
        RET(init_filter(ctx, in));

    RET(ff_vk_filter_process_simple(&s->vkctx, &s->e, &s->pl,
                                    out, in, s->sampler, &s->opts, sizeof(s->opts)));

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

static void avgblur_vulkan_uninit(AVFilterContext *avctx)
{
    AvgBlurVulkanContext *s = avctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;
    FFVulkanFunctions *vk = &vkctx->vkfn;

    ff_vk_exec_pool_free(vkctx, &s->e);
    ff_vk_pipeline_free(vkctx, &s->pl);
    ff_vk_shader_free(vkctx, &s->shd);

    if (s->sampler)
        vk->DestroySampler(vkctx->hwctx->act_dev, s->sampler,
                           vkctx->hwctx->alloc);

    ff_vk_uninit(&s->vkctx);

    s->initialized = 0;
}

#define OFFSET(x) offsetof(AvgBlurVulkanContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)
static const AVOption avgblur_vulkan_options[] = {
    { "sizeX",  "Set horizontal radius", OFFSET(size_x), AV_OPT_TYPE_INT, { .i64 = 3 }, 1, 32, .flags = FLAGS },
    { "sizeY",  "Set vertical radius", OFFSET(size_y), AV_OPT_TYPE_INT, { .i64 = 3 }, 1, 32, .flags = FLAGS },
    { "planes", "Set planes to filter (bitmask)", OFFSET(planes), AV_OPT_TYPE_INT, {.i64 = 0xF}, 0, 0xF, .flags = FLAGS },
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
};

static const AVFilterPad avgblur_vulkan_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = &ff_vk_filter_config_output,
    },
};

const AVFilter ff_vf_avgblur_vulkan = {
    .name           = "avgblur_vulkan",
    .description    = NULL_IF_CONFIG_SMALL("Apply avgblur mask to input video"),
    .priv_size      = sizeof(AvgBlurVulkanContext),
    .init           = &ff_vk_filter_init,
    .uninit         = &avgblur_vulkan_uninit,
    FILTER_INPUTS(avgblur_vulkan_inputs),
    FILTER_OUTPUTS(avgblur_vulkan_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_VULKAN),
    .priv_class     = &avgblur_vulkan_class,
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
    .flags          = AVFILTER_FLAG_HWDEVICE,
};
