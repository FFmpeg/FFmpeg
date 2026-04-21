/*
 * Copyright 2025 (c) Niklas Haas
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

#include "tinterlace.h"
#include "filters.h"
#include "video.h"

extern const unsigned char ff_interlace_comp_spv_data[];
extern const unsigned int ff_interlace_comp_spv_len;

typedef struct InterlaceVulkanContext {
    FFVulkanContext vkctx;

    int initialized;
    FFVkExecPool e;
    AVVulkanDeviceQueueFamily *qf;
    VkSampler sampler;
    FFVulkanShader shd;

    int mode;
    int lowpass;

    AVFrame *cur; /* first frame in pair */
} InterlaceVulkanContext;

static av_cold int init_filter(AVFilterContext *ctx)
{
    int err;
    InterlaceVulkanContext *s = ctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;
    const int planes = av_pix_fmt_count_planes(s->vkctx.output_format);

    s->qf = ff_vk_qf_find(vkctx, VK_QUEUE_COMPUTE_BIT, 0);
    if (!s->qf) {
        av_log(ctx, AV_LOG_ERROR, "Device has no compute queues\n");
        err = AVERROR(ENOTSUP);
        goto fail;
    }

    RET(ff_vk_exec_pool_init(vkctx, s->qf, &s->e, s->qf->num*4, 0, 0, 0, NULL));
    RET(ff_vk_init_sampler(vkctx, &s->sampler, 1,
                           s->lowpass == VLPF_OFF ? VK_FILTER_NEAREST
                                                  : VK_FILTER_LINEAR));

    SPEC_LIST_CREATE(sl, 2, 2*sizeof(int))
    SPEC_LIST_ADD(sl, 0, 32, s->lowpass);
    SPEC_LIST_ADD(sl, 1, 32, planes);

    ff_vk_shader_load(&s->shd, VK_SHADER_STAGE_COMPUTE_BIT,
                      sl, (uint32_t []) { 32, 1, planes }, 0);

    const FFVulkanDescriptorSetBinding desc[] = {
        { /* top_field */
            .type     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .stages   = VK_SHADER_STAGE_COMPUTE_BIT,
            .samplers = DUP_SAMPLER(s->sampler),
            .elems    = planes,
        },
        { /* bot_field */
            .type     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .stages   = VK_SHADER_STAGE_COMPUTE_BIT,
            .samplers = DUP_SAMPLER(s->sampler),
            .elems    = planes,
        },
        { /* output_img */
            .type   = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stages = VK_SHADER_STAGE_COMPUTE_BIT,
            .elems  = planes,
        },
    };
    ff_vk_shader_add_descriptor_set(vkctx, &s->shd, desc, 3, 0, 0);

    RET(ff_vk_shader_link(vkctx, &s->shd,
                          ff_interlace_comp_spv_data,
                          ff_interlace_comp_spv_len, "main"));

    RET(ff_vk_shader_register_exec(vkctx, &s->e, &s->shd));

    s->initialized = 1;

fail:
    return err;
}

static int interlace_vulkan_filter_frame(AVFilterLink *link, AVFrame *in)
{
    int err;
    AVFrame *out = NULL, *input_top, *input_bot;
    AVFilterContext *ctx = link->dst;
    InterlaceVulkanContext *s = ctx->priv;
    const AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    FilterLink *l = ff_filter_link(outlink);

    if (!s->initialized)
        RET(init_filter(ctx));

    /* Need both frames to filter */
    if (!s->cur) {
        s->cur = in;
        return 0;
    }

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    if (s->mode == MODE_TFF) {
        input_top = s->cur;
        input_bot = in;
    } else {
        input_top = in;
        input_bot = s->cur;
    }

    RET(ff_vk_filter_process_Nin(&s->vkctx, &s->e, &s->shd,
                                 out, (AVFrame *[]){ input_top, input_bot }, 2,
                                 s->sampler, 1, NULL, 0));

    err = av_frame_copy_props(out, s->cur);
    if (err < 0)
        goto fail;

    out->flags |= AV_FRAME_FLAG_INTERLACED;
    if (s->mode == MODE_TFF)
        out->flags |= AV_FRAME_FLAG_TOP_FIELD_FIRST;

    out->pts = av_rescale_q(out->pts, inlink->time_base, outlink->time_base);
    out->duration = av_rescale_q(1, av_inv_q(l->frame_rate), outlink->time_base);

    av_frame_free(&s->cur);
    av_frame_free(&in);

    return ff_filter_frame(outlink, out);

fail:
    av_frame_free(&s->cur);
    av_frame_free(&in);
    av_frame_free(&out);
    return err;
}

static void interlace_vulkan_uninit(AVFilterContext *avctx)
{
    InterlaceVulkanContext *s = avctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;
    FFVulkanFunctions *vk = &vkctx->vkfn;

    av_frame_free(&s->cur);

    ff_vk_exec_pool_free(vkctx, &s->e);
    ff_vk_shader_free(vkctx, &s->shd);

    if (s->sampler)
        vk->DestroySampler(vkctx->hwctx->act_dev, s->sampler,
                           vkctx->hwctx->alloc);

    ff_vk_uninit(&s->vkctx);

    s->initialized = 0;
}

static int config_out_props(AVFilterLink *outlink)
{
    AVFilterLink *inlink = outlink->src->inputs[0];
    const FilterLink *il = ff_filter_link(inlink);
    FilterLink *ol = ff_filter_link(outlink);

    ol->frame_rate = av_mul_q(il->frame_rate, av_make_q(1, 2));
    outlink->time_base = av_mul_q(inlink->time_base, av_make_q(2, 1));
    return ff_vk_filter_config_output(outlink);
}

#define OFFSET(x) offsetof(InterlaceVulkanContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)
static const AVOption interlace_vulkan_options[] = {
    { "scan",              "scanning mode", OFFSET(mode), AV_OPT_TYPE_INT, {.i64 = MODE_TFF}, 0, 1, FLAGS, .unit = "mode"},
    { "tff",               "top field first",                              0, AV_OPT_TYPE_CONST, {.i64 = MODE_TFF}, INT_MIN, INT_MAX, FLAGS, .unit = "mode"},
    { "bff",               "bottom field first",                           0, AV_OPT_TYPE_CONST, {.i64 = MODE_BFF}, INT_MIN, INT_MAX, FLAGS, .unit = "mode"},
    { "lowpass",           "set vertical low-pass filter", OFFSET(lowpass), AV_OPT_TYPE_INT,   {.i64 = VLPF_LIN}, 0, 2, FLAGS, .unit = "lowpass" },
    {     "off",           "disable vertical low-pass filter",             0, AV_OPT_TYPE_CONST, {.i64 = VLPF_OFF}, INT_MIN, INT_MAX, FLAGS, .unit = "lowpass" },
    {     "linear",        "linear vertical low-pass filter",              0, AV_OPT_TYPE_CONST, {.i64 = VLPF_LIN}, INT_MIN, INT_MAX, FLAGS, .unit = "lowpass" },
    {     "complex",       "complex vertical low-pass filter",             0, AV_OPT_TYPE_CONST, {.i64 = VLPF_CMP}, INT_MIN, INT_MAX, FLAGS, .unit = "lowpass" },
    { NULL },
};

AVFILTER_DEFINE_CLASS(interlace_vulkan);

static const AVFilterPad interlace_vulkan_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = &interlace_vulkan_filter_frame,
        .config_props = &ff_vk_filter_config_input,
    },
};

static const AVFilterPad interlace_vulkan_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = &config_out_props,
    },
};

const FFFilter ff_vf_interlace_vulkan = {
    .p.name         = "interlace_vulkan",
    .p.description  = NULL_IF_CONFIG_SMALL("Convert progressive video into interlaced."),
    .p.priv_class   = &interlace_vulkan_class,
    .p.flags        = AVFILTER_FLAG_HWDEVICE,
    .priv_size      = sizeof(InterlaceVulkanContext),
    .init           = &ff_vk_filter_init,
    .uninit         = &interlace_vulkan_uninit,
    FILTER_INPUTS(interlace_vulkan_inputs),
    FILTER_OUTPUTS(interlace_vulkan_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_VULKAN),
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
