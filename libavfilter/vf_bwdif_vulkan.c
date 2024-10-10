/*
 * Copyright (c) Lynne
 * Copyright (C) 2018 Philip Langdale <philipl@overt.org>
 * Copyright (C) 2016 Thomas Mundt <loudmax@yahoo.de>
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
#include "libavutil/vulkan_spirv.h"
#include "vulkan_filter.h"
#include "yadif.h"
#include "filters.h"

typedef struct BWDIFVulkanContext {
    YADIFContext yadif;
    FFVulkanContext vkctx;

    int initialized;
    FFVkExecPool e;
    FFVkQueueFamilyCtx qf;
    VkSampler sampler;
    FFVulkanShader shd;
} BWDIFVulkanContext;

typedef struct BWDIFParameters {
    int parity;
    int tff;
    int current_field;
} BWDIFParameters;

extern const char *ff_source_bwdif_comp;

static av_cold int init_filter(AVFilterContext *ctx)
{
    int err;
    uint8_t *spv_data;
    size_t spv_len;
    void *spv_opaque = NULL;
    BWDIFVulkanContext *s = ctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;
    const int planes = av_pix_fmt_count_planes(s->vkctx.output_format);
    FFVulkanShader *shd;
    FFVkSPIRVCompiler *spv;
    FFVulkanDescriptorSetBinding *desc;

    spv = ff_vk_spirv_init();
    if (!spv) {
        av_log(ctx, AV_LOG_ERROR, "Unable to initialize SPIR-V compiler!\n");
        return AVERROR_EXTERNAL;
    }

    ff_vk_qf_init(vkctx, &s->qf, VK_QUEUE_COMPUTE_BIT);
    RET(ff_vk_exec_pool_init(vkctx, &s->qf, &s->e, s->qf.nb_queues*4, 0, 0, 0, NULL));
    RET(ff_vk_init_sampler(vkctx, &s->sampler, 1, VK_FILTER_NEAREST));

    RET(ff_vk_shader_init(vkctx, &s->shd, "bwdif",
                          VK_SHADER_STAGE_COMPUTE_BIT,
                          NULL, 0,
                          1, 64, 1,
                          0));
    shd = &s->shd;

    desc = (FFVulkanDescriptorSetBinding []) {
        {
            .name       = "prev",
            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .dimensions = 2,
            .elems      = planes,
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
            .samplers   = DUP_SAMPLER(s->sampler),
        },
        {
            .name       = "cur",
            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .dimensions = 2,
            .elems      = planes,
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
            .samplers   = DUP_SAMPLER(s->sampler),
        },
        {
            .name       = "next",
            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .dimensions = 2,
            .elems      = planes,
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
            .samplers   = DUP_SAMPLER(s->sampler),
        },
        {
            .name       = "dst",
            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .mem_layout = ff_vk_shader_rep_fmt(s->vkctx.output_format, FF_VK_REP_FLOAT),
            .mem_quali  = "writeonly",
            .dimensions = 2,
            .elems      = planes,
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };

    RET(ff_vk_shader_add_descriptor_set(vkctx, &s->shd, desc, 4, 0, 0));

    GLSLC(0, layout(push_constant, std430) uniform pushConstants {                 );
    GLSLC(1,    int parity;                                                        );
    GLSLC(1,    int tff;                                                           );
    GLSLC(1,    int current_field;                                                 );
    GLSLC(0, };                                                                    );

    ff_vk_shader_add_push_const(&s->shd, 0, sizeof(BWDIFParameters),
                                VK_SHADER_STAGE_COMPUTE_BIT);

    GLSLD(ff_source_bwdif_comp                                                     );
    GLSLC(0, void main()                                                           );
    GLSLC(0, {                                                                     );
    GLSLC(1,     ivec2 size;                                                       );
    GLSLC(1,     const ivec2 pos = ivec2(gl_GlobalInvocationID.xy);                );
    GLSLC(1,     bool filter_field = ((pos.y ^ parity) & 1) == 1;                  );
    GLSLF(1,     bool is_intra = filter_field && (current_field == %i);           ,YADIF_FIELD_END);
    GLSLC(1,     bool field_parity = (parity ^ tff) != 0;                          );
    GLSLC(0,                                                                       );
    GLSLC(1,     size = imageSize(dst[0]);                                         );
    GLSLC(1,     if (!IS_WITHIN(pos, size)) {                                      );
    GLSLC(2,         return;                                                       );
    GLSLC(1,     } else if (is_intra) {                                            );
    for (int i = 0; i < planes; i++) {
        if (i == 1) {
            GLSLF(2, size = imageSize(dst[%i]);                                    ,i);
            GLSLC(2, if (!IS_WITHIN(pos, size))                                    );
            GLSLC(3,     return;                                                   );
        }
        GLSLF(2,     process_plane_intra(%i, pos);                                 ,i);
    }
    GLSLC(1,     } else if (filter_field) {                                        );
    for (int i = 0; i < planes; i++) {
        if (i == 1) {
            GLSLF(2, size = imageSize(dst[%i]);                                    ,i);
            GLSLC(2, if (!IS_WITHIN(pos, size))                                    );
            GLSLC(3,     return;                                                   );
        }
        GLSLF(2,     process_plane(%i, pos, filter_field, is_intra, field_parity); ,i);
    }
    GLSLC(1,     } else {                                                          );
    for (int i = 0; i < planes; i++) {
        if (i == 1) {
            GLSLF(2, size = imageSize(dst[%i]);                                    ,i);
            GLSLC(2, if (!IS_WITHIN(pos, size))                                    );
            GLSLC(3,     return;                                                   );
        }
        GLSLF(2,     imageStore(dst[%i], pos, texture(cur[%i], pos));              ,i, i);
    }
    GLSLC(1,     }                                                                 );
    GLSLC(0, }                                                                     );

    RET(spv->compile_shader(vkctx, spv, &s->shd, &spv_data, &spv_len, "main",
                            &spv_opaque));
    RET(ff_vk_shader_link(vkctx, &s->shd, spv_data, spv_len, "main"));

    RET(ff_vk_shader_register_exec(vkctx, &s->e, &s->shd));

    s->initialized = 1;

fail:
    if (spv_opaque)
        spv->free_shader(spv, &spv_opaque);
    if (spv)
        spv->uninit(&spv);

    return err;
}

static void bwdif_vulkan_filter_frame(AVFilterContext *ctx, AVFrame *dst,
                                      int parity, int tff)
{
    BWDIFVulkanContext *s = ctx->priv;
    YADIFContext *y = &s->yadif;
    BWDIFParameters params = {
        .parity = parity,
        .tff = tff,
        .current_field = y->current_field,
    };

    ff_vk_filter_process_Nin(&s->vkctx, &s->e, &s->shd, dst,
                             (AVFrame *[]){ y->prev, y->cur, y->next }, 3,
                             s->sampler, &params, sizeof(params));

    if (y->current_field == YADIF_FIELD_END)
        y->current_field = YADIF_FIELD_NORMAL;
}

static void bwdif_vulkan_uninit(AVFilterContext *avctx)
{
    BWDIFVulkanContext *s = avctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;
    FFVulkanFunctions *vk = &vkctx->vkfn;

    ff_vk_exec_pool_free(vkctx, &s->e);
    ff_vk_shader_free(vkctx, &s->shd);

    if (s->sampler)
        vk->DestroySampler(vkctx->hwctx->act_dev, s->sampler,
                           vkctx->hwctx->alloc);

    ff_vk_uninit(&s->vkctx);

    ff_yadif_uninit(avctx);

    s->initialized = 0;
}

static int bwdif_vulkan_config_input(AVFilterLink *inlink)
{
    FilterLink *l = ff_filter_link(inlink);
    AVHWFramesContext *input_frames;
    AVFilterContext *avctx = inlink->dst;
    BWDIFVulkanContext *s = avctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;

    if (!l->hw_frames_ctx) {
        av_log(inlink->dst, AV_LOG_ERROR, "Vulkan filtering requires a "
               "hardware frames context on the input.\n");
        return AVERROR(EINVAL);
    }

    input_frames = (AVHWFramesContext *)l->hw_frames_ctx->data;
    if (input_frames->format != AV_PIX_FMT_VULKAN)
        return AVERROR(EINVAL);

    /* Extract the device and default output format from the first input. */
    if (avctx->inputs[0] != inlink)
        return 0;

    /* Save the ref, without reffing it */
    vkctx->input_frames_ref = l->hw_frames_ctx;

    /* Defaults */
    vkctx->output_format = input_frames->sw_format;
    vkctx->output_width  = inlink->w;
    vkctx->output_height = inlink->h;

    return 0;
}

static int bwdif_vulkan_config_output(AVFilterLink *outlink)
{
    FilterLink *l = ff_filter_link(outlink);
    int err;
    AVFilterContext *avctx = outlink->src;
    BWDIFVulkanContext *s = avctx->priv;
    YADIFContext *y = &s->yadif;
    FFVulkanContext *vkctx = &s->vkctx;

    av_buffer_unref(&l->hw_frames_ctx);

    err = ff_vk_filter_init_context(avctx, vkctx, vkctx->input_frames_ref,
                                    vkctx->output_width, vkctx->output_height,
                                    vkctx->output_format);
    if (err < 0)
        return err;

    /* For logging */
    vkctx->class = y->class;

    l->hw_frames_ctx = av_buffer_ref(vkctx->frames_ref);
    if (!l->hw_frames_ctx)
        return AVERROR(ENOMEM);

    err = ff_yadif_config_output_common(outlink);
    if (err < 0)
        return err;

    y->csp = av_pix_fmt_desc_get(vkctx->frames->sw_format);
    y->filter = bwdif_vulkan_filter_frame;

    if (AV_CEIL_RSHIFT(outlink->w, y->csp->log2_chroma_w) < 4 || AV_CEIL_RSHIFT(outlink->h, y->csp->log2_chroma_h) < 4) {
        av_log(avctx, AV_LOG_ERROR, "Video with planes less than 4 columns or lines is not supported\n");
        return AVERROR(EINVAL);
    }

    return init_filter(avctx);
}

static const AVClass bwdif_vulkan_class = {
    .class_name = "bwdif_vulkan",
    .item_name  = av_default_item_name,
    .option     = ff_yadif_options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_FILTER,
};

static const AVFilterPad bwdif_vulkan_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame  = ff_yadif_filter_frame,
        .config_props = &bwdif_vulkan_config_input,
    },
};

static const AVFilterPad bwdif_vulkan_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .request_frame = ff_yadif_request_frame,
        .config_props = &bwdif_vulkan_config_output,
    },
};

const AVFilter ff_vf_bwdif_vulkan = {
    .name           = "bwdif_vulkan",
    .description    = NULL_IF_CONFIG_SMALL("Deinterlace Vulkan frames via bwdif"),
    .priv_size      = sizeof(BWDIFVulkanContext),
    .init           = &ff_vk_filter_init,
    .uninit         = &bwdif_vulkan_uninit,
    FILTER_INPUTS(bwdif_vulkan_inputs),
    FILTER_OUTPUTS(bwdif_vulkan_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_VULKAN),
    .priv_class     = &bwdif_vulkan_class,
    .flags          = AVFILTER_FLAG_HWDEVICE |
                      AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL,
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
