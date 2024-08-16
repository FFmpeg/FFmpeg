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
#include "vulkan_filter.h"
#include "vulkan_spirv.h"
#include "yadif.h"
#include "filters.h"

typedef struct BWDIFVulkanContext {
    YADIFContext yadif;
    FFVulkanContext vkctx;

    int initialized;
    FFVkExecPool e;
    FFVkQueueFamilyCtx qf;
    VkSampler sampler;
    FFVulkanPipeline pl;
    FFVkSPIRVShader shd;
} BWDIFVulkanContext;

typedef struct BWDIFParameters {
    int parity;
    int tff;
    int current_field;
} BWDIFParameters;

static const char filter_fn[] = {
    "const vec4 coef_lf[2] = { vec4(4309), vec4(213), };\n"
    "const vec4 coef_hf[3] = { vec4(5570), vec4(3801), vec4(1016) };\n"
    "const vec4 coef_sp[2] = { vec4(5077), vec4(981), };\n"
    C(0,                                                                                              )
    C(0, vec4 process_intra(vec4 cur[4])                                                              )
    C(0, {                                                                                            )
    C(1,     return (coef_sp[0]*(cur[1] + cur[2]) - coef_sp[1]*(cur[0] + cur[3])) / (1 << 13);        )
    C(0, }                                                                                            )
    C(0,                                                                                              )
    C(0, vec4 process_line(vec4 prev2[5], vec4 prev1[2], vec4 cur[4], vec4 next1[2], vec4 next2[5])   )
    C(0, {                                                                                            )
    C(1,     vec4 fc = cur[1];                                                                        )
    C(1,     vec4 fe = cur[2];                                                                        )
    C(1,     vec4 fs = prev2[2] + next2[2];                                                           )
    C(1,     vec4 fd = fs / 2;                                                                        )
    C(0,                                                                                              )
    C(1,     vec4 temp_diff[3];                                                                       )
    C(1,     temp_diff[0] = abs(prev2[2] - next2[2]);                                                 )
    C(1,     temp_diff[1] = (abs(prev1[0] - fc) + abs(prev1[1] - fe)) / 2;                            )
    C(1,     temp_diff[1] = (abs(next1[0] - fc) + abs(next1[1] - fe)) / 2;                            )
    C(1,     vec4 diff = max(temp_diff[0] / 2, max(temp_diff[1], temp_diff[2]));                      )
    C(1,     bvec4 diff_mask = equal(diff, vec4(0));                                                  )
    C(0,                                                                                              )
    C(1,     vec4 fbs = prev2[1] + next2[1];                                                          )
    C(1,     vec4 ffs = prev2[3] + next2[3];                                                          )
    C(1,     vec4 fb = (fbs / 2) - fc;                                                                )
    C(1,     vec4 ff = (ffs / 2) - fe;                                                                )
    C(1,     vec4 dc = fd - fc;                                                                       )
    C(1,     vec4 de = fd - fe;                                                                       )
    C(1,     vec4 mmax = max(de, max(dc, min(fb, ff)));                                               )
    C(1,     vec4 mmin = min(de, min(dc, max(fb, ff)));                                               )
    C(1,     diff = max(diff, max(mmin, -mmax));                                                      )
    C(0,                                                                                              )
"    vec4 interpolate_all = (((coef_hf[0]*(fs) - coef_hf[1]*(fbs + ffs) +\n"
"                              coef_hf[2]*(prev2[0] + next2[0] + prev2[4] + next2[4])) / 4) +\n"
"                            coef_lf[0]*(fc + fe) - coef_lf[1]*(cur[0] + cur[3])) / (1 << 13);\n"
"    vec4 interpolate_cur = (coef_sp[0]*(fc + fe) - coef_sp[1]*(cur[0] + cur[3])) / (1 << 13);\n"
    C(0,                                                                                              )
    C(1,     bvec4 interpolate_cnd1 = greaterThan(abs(fc - fe), temp_diff[0]);                        )
    C(1,     vec4 interpol = mix(interpolate_cur, interpolate_all, interpolate_cnd1);                 )
    C(1,     interpol = clamp(interpol, fd - diff, fd + diff);                                        )
    C(1,     return mix(interpol, fd, diff_mask);                                                     )
    C(0, }                                                                                            )
};

static av_cold int init_filter(AVFilterContext *ctx)
{
    int err;
    uint8_t *spv_data;
    size_t spv_len;
    void *spv_opaque = NULL;
    BWDIFVulkanContext *s = ctx->priv;
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
    RET(ff_vk_init_sampler(vkctx, &s->sampler, 1, VK_FILTER_NEAREST));
    RET(ff_vk_shader_init(&s->pl, &s->shd, "bwdif_compute",
                          VK_SHADER_STAGE_COMPUTE_BIT, 0));
    shd = &s->shd;

    ff_vk_shader_set_compute_sizes(shd, 1, 64, 1);

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
            .mem_layout = ff_vk_shader_rep_fmt(s->vkctx.output_format),
            .mem_quali  = "writeonly",
            .dimensions = 2,
            .elems      = planes,
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };

    RET(ff_vk_pipeline_descriptor_set_add(vkctx, &s->pl, shd, desc, 4, 0, 0));

    GLSLC(0, layout(push_constant, std430) uniform pushConstants {                 );
    GLSLC(1,    int parity;                                                        );
    GLSLC(1,    int tff;                                                           );
    GLSLC(1,    int current_field;                                                 );
    GLSLC(0, };                                                                    );

    ff_vk_add_push_constant(&s->pl, 0, sizeof(BWDIFParameters),
                            VK_SHADER_STAGE_COMPUTE_BIT);

    GLSLD(   filter_fn                                                             );
    GLSLC(0, void main()                                                           );
    GLSLC(0, {                                                                     );
    GLSLC(1,     vec4 res;                                                         );
    GLSLC(1,     ivec2 size;                                                       );
    GLSLC(1,     vec4 dcur[4];                                                     );
    GLSLC(1,     vec4 prev1[2];                                                    );
    GLSLC(1,     vec4 next1[2];                                                    );
    GLSLC(1,     vec4 prev2[5];                                                    );
    GLSLC(1,     vec4 next2[5];                                                    );
    GLSLC(1,     const ivec2 pos = ivec2(gl_GlobalInvocationID.xy);                );
    GLSLC(1,     bool filter_field = ((pos.y ^ parity) & 1) == 1;                  );
    GLSLF(1,     bool is_intra = filter_field && (current_field == %i);           ,YADIF_FIELD_END);
    GLSLC(1,     bool field_parity = (parity ^ tff) != 0;                          );
    GLSLC(0,                                                                       );

    for (int i = 0; i < planes; i++) {
        GLSLC(0,                                                                   );
        GLSLF(1, size = imageSize(dst[%i]);                                      ,i);
        GLSLC(1, if (!IS_WITHIN(pos, size)) {                                      );
        GLSLC(2,     return;                                                       );
        GLSLC(1, } else if (is_intra) {                                            );
        GLSLF(2,     dcur[0] = texture(cur[%i], pos - ivec2(0, 3));              ,i);
        GLSLF(2,     dcur[1] = texture(cur[%i], pos - ivec2(0, 1));              ,i);
        GLSLF(2,     dcur[2] = texture(cur[%i], pos + ivec2(0, 1));              ,i);
        GLSLF(2,     dcur[3] = texture(cur[%i], pos + ivec2(0, 3));              ,i);
        GLSLC(0,                                                                   );
        GLSLC(2,     res = process_intra(dcur);                                    );
        GLSLF(2,     imageStore(dst[%i], pos, res);                              ,i);
        GLSLC(1, } else if (filter_field) {                                        );
        GLSLF(2,     dcur[0] = texture(cur[%i], pos - ivec2(0, 3));              ,i);
        GLSLF(2,     dcur[1] = texture(cur[%i], pos - ivec2(0, 1));              ,i);
        GLSLF(2,     dcur[2] = texture(cur[%i], pos + ivec2(0, 1));              ,i);
        GLSLF(2,     dcur[3] = texture(cur[%i], pos + ivec2(0, 3));              ,i);
        GLSLC(0,                                                                   );
        GLSLF(2,     prev1[0] = texture(prev[%i], pos - ivec2(0, 1));            ,i);
        GLSLF(2,     prev1[1] = texture(prev[%i], pos + ivec2(0, 1));            ,i);
        GLSLC(0,                                                                   );
        GLSLF(2,     next1[0] = texture(next[%i], pos - ivec2(0, 1));            ,i);
        GLSLF(2,     next1[1] = texture(next[%i], pos + ivec2(0, 1));            ,i);
        GLSLC(0,                                                                   );
        GLSLC(2,     if (field_parity) {                                           );
        GLSLF(3,         prev2[0] = texture(prev[%i], pos - ivec2(0, 4));        ,i);
        GLSLF(3,         prev2[1] = texture(prev[%i], pos - ivec2(0, 2));        ,i);
        GLSLF(3,         prev2[2] = texture(prev[%i], pos);                      ,i);
        GLSLF(3,         prev2[3] = texture(prev[%i], pos + ivec2(0, 2));        ,i);
        GLSLF(3,         prev2[4] = texture(prev[%i], pos + ivec2(0, 4));        ,i);
        GLSLC(0,                                                                   );
        GLSLF(3,         next2[0] = texture(cur[%i], pos - ivec2(0, 4));         ,i);
        GLSLF(3,         next2[1] = texture(cur[%i], pos - ivec2(0, 2));         ,i);
        GLSLF(3,         next2[2] = texture(cur[%i], pos);                       ,i);
        GLSLF(3,         next2[3] = texture(cur[%i], pos + ivec2(0, 2));         ,i);
        GLSLF(3,         next2[4] = texture(cur[%i], pos + ivec2(0, 4));         ,i);
        GLSLC(2,     } else {                                                      );
        GLSLF(3,         prev2[0] = texture(cur[%i], pos - ivec2(0, 4));         ,i);
        GLSLF(3,         prev2[1] = texture(cur[%i], pos - ivec2(0, 2));         ,i);
        GLSLF(3,         prev2[2] = texture(cur[%i], pos);                       ,i);
        GLSLF(3,         prev2[3] = texture(cur[%i], pos + ivec2(0, 2));         ,i);
        GLSLF(3,         prev2[4] = texture(cur[%i], pos + ivec2(0, 4));         ,i);
        GLSLC(0,                                                                   );
        GLSLF(3,         next2[0] = texture(next[%i], pos - ivec2(0, 4));        ,i);
        GLSLF(3,         next2[1] = texture(next[%i], pos - ivec2(0, 2));        ,i);
        GLSLF(3,         next2[2] = texture(next[%i], pos);                      ,i);
        GLSLF(3,         next2[3] = texture(next[%i], pos + ivec2(0, 2));        ,i);
        GLSLF(3,         next2[4] = texture(next[%i], pos + ivec2(0, 4));        ,i);
        GLSLC(2,     }                                                             );
        GLSLC(0,                                                                   );
        GLSLC(2,     res = process_line(prev2, prev1, dcur, next1, next2);         );
        GLSLF(2,     imageStore(dst[%i], pos, res);                              ,i);
        GLSLC(1, } else {                                                          );
        GLSLF(2,     res = texture(cur[%i], pos);                                ,i);
        GLSLF(2,     imageStore(dst[%i], pos, res);                              ,i);
        GLSLC(1, }                                                                 );
    }

    GLSLC(0, }                                                                     );

    RET(spv->compile_shader(spv, ctx, &s->shd, &spv_data, &spv_len, "main",
                            &spv_opaque));
    RET(ff_vk_shader_create(vkctx, &s->shd, spv_data, spv_len, "main"));

    RET(ff_vk_init_compute_pipeline(vkctx, &s->pl, &s->shd));
    RET(ff_vk_exec_pipeline_register(vkctx, &s->e, &s->pl));

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

    ff_vk_filter_process_Nin(&s->vkctx, &s->e, &s->pl, dst,
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
    ff_vk_pipeline_free(vkctx, &s->pl);
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
