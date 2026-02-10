/*
 * copyright (c) 2021 Wu Jianhua <jianhua.wu@intel.com>
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

#include "libavutil/opt.h"
#include "vulkan_filter.h"

#include "filters.h"
#include "transpose.h"
#include "video.h"

extern const unsigned char ff_transpose_comp_spv_data[];
extern const unsigned int ff_transpose_comp_spv_len;

typedef struct TransposeVulkanContext {
    FFVulkanContext vkctx;

    int initialized;
    FFVkExecPool e;
    AVVulkanDeviceQueueFamily *qf;
    FFVulkanShader shd;

    int dir;
    int passthrough;
} TransposeVulkanContext;

static av_cold int init_filter(AVFilterContext *ctx, AVFrame *in)
{
    int err;
    TransposeVulkanContext *s = ctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;
    const int planes = av_pix_fmt_count_planes(s->vkctx.output_format);

    s->qf = ff_vk_qf_find(vkctx, VK_QUEUE_COMPUTE_BIT, 0);
    if (!s->qf) {
        av_log(ctx, AV_LOG_ERROR, "Device has no compute queues\n");
        err = AVERROR(ENOTSUP);
        goto fail;
    }

    RET(ff_vk_exec_pool_init(vkctx, s->qf, &s->e, s->qf->num*4, 0, 0, 0, NULL));

    ff_vk_shader_load(&s->shd, VK_SHADER_STAGE_COMPUTE_BIT, NULL,
                      (uint32_t []) { 32, 1, planes }, 0);

    ff_vk_shader_add_push_const(&s->shd, 0, sizeof(int),
                                VK_SHADER_STAGE_COMPUTE_BIT);

    const FFVulkanDescriptorSetBinding desc[] = {
        { /* input_img */
            .type   = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stages = VK_SHADER_STAGE_COMPUTE_BIT,
            .elems  = planes,
        },
        { /* output_img */
            .type   = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stages = VK_SHADER_STAGE_COMPUTE_BIT,
            .elems  = planes,
        },
    };
    ff_vk_shader_add_descriptor_set(vkctx, &s->shd, desc, 2, 0, 0);

    RET(ff_vk_shader_link(vkctx, &s->shd,
                          ff_transpose_comp_spv_data,
                          ff_transpose_comp_spv_len, "main"));

    RET(ff_vk_shader_register_exec(vkctx, &s->e, &s->shd));

    s->initialized = 1;

fail:
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

    RET(ff_vk_filter_process_simple(&s->vkctx, &s->e, &s->shd, out, in,
                                    VK_NULL_HANDLE, 1, &s->dir, sizeof(int)));

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
    FFVulkanContext *vkctx = &s->vkctx;

    ff_vk_exec_pool_free(vkctx, &s->e);
    ff_vk_shader_free(vkctx, &s->shd);

    ff_vk_uninit(&s->vkctx);

    s->initialized = 0;
}

static int config_props_output(AVFilterLink *outlink)
{
    FilterLink *outl = ff_filter_link(outlink);
    AVFilterContext *avctx = outlink->src;
    TransposeVulkanContext *s = avctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;
    AVFilterLink *inlink = avctx->inputs[0];
    FilterLink *inl = ff_filter_link(inlink);

    if ((inlink->w >= inlink->h && s->passthrough == TRANSPOSE_PT_TYPE_LANDSCAPE) ||
        (inlink->w <= inlink->h && s->passthrough == TRANSPOSE_PT_TYPE_PORTRAIT)) {
        av_log(avctx, AV_LOG_VERBOSE,
               "w:%d h:%d -> w:%d h:%d (passthrough mode)\n",
               inlink->w, inlink->h, inlink->w, inlink->h);
        outl->hw_frames_ctx = av_buffer_ref(inl->hw_frames_ctx);
        return outl->hw_frames_ctx ? 0 : AVERROR(ENOMEM);
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
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM | \
               AV_OPT_FLAG_RUNTIME_PARAM)

static const AVOption transpose_vulkan_options[] = {
    { "dir", "set transpose direction", OFFSET(dir), AV_OPT_TYPE_INT, { .i64 = TRANSPOSE_CCLOCK_FLIP }, 0, 7, FLAGS, .unit = "dir" },
        { "cclock_flip", "rotate counter-clockwise with vertical flip", 0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_CCLOCK_FLIP }, .flags=FLAGS, .unit = "dir" },
        { "clock",       "rotate clockwise",                            0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_CLOCK       }, .flags=FLAGS, .unit = "dir" },
        { "cclock",      "rotate counter-clockwise",                    0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_CCLOCK      }, .flags=FLAGS, .unit = "dir" },
        { "clock_flip",  "rotate clockwise with vertical flip",         0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_CLOCK_FLIP  }, .flags=FLAGS, .unit = "dir" },

    { "passthrough", "do not apply transposition if the input matches the specified geometry",
      OFFSET(passthrough), AV_OPT_TYPE_INT, {.i64=TRANSPOSE_PT_TYPE_NONE},  0, INT_MAX, FLAGS, .unit = "passthrough" },
        { "none",      "always apply transposition",   0, AV_OPT_TYPE_CONST, {.i64=TRANSPOSE_PT_TYPE_NONE},      INT_MIN, INT_MAX, FLAGS, .unit = "passthrough" },
        { "portrait",  "preserve portrait geometry",   0, AV_OPT_TYPE_CONST, {.i64=TRANSPOSE_PT_TYPE_PORTRAIT},  INT_MIN, INT_MAX, FLAGS, .unit = "passthrough" },
        { "landscape", "preserve landscape geometry",  0, AV_OPT_TYPE_CONST, {.i64=TRANSPOSE_PT_TYPE_LANDSCAPE}, INT_MIN, INT_MAX, FLAGS, .unit = "passthrough" },

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

const FFFilter ff_vf_transpose_vulkan = {
    .p.name         = "transpose_vulkan",
    .p.description  = NULL_IF_CONFIG_SMALL("Transpose Vulkan Filter"),
    .p.priv_class   = &transpose_vulkan_class,
    .p.flags        = AVFILTER_FLAG_HWDEVICE,
    .priv_size      = sizeof(TransposeVulkanContext),
    .init           = &ff_vk_filter_init,
    .uninit         = &transpose_vulkan_uninit,
    FILTER_INPUTS(transpose_vulkan_inputs),
    FILTER_OUTPUTS(transpose_vulkan_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_VULKAN),
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
