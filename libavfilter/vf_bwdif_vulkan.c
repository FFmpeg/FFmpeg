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
#include "yadif.h"
#include "filters.h"

typedef struct BWDIFVulkanContext {
    YADIFContext yadif;
    FFVulkanContext vkctx;

    int initialized;
    FFVkExecPool e;
    AVVulkanDeviceQueueFamily *qf;
    FFVulkanShader shd;
} BWDIFVulkanContext;

typedef struct BWDIFParameters {
    int parity;
    int tff;
    int current_field;
} BWDIFParameters;

extern const unsigned char ff_bwdif_comp_spv_data[];
extern const unsigned int ff_bwdif_comp_spv_len;

static av_cold int init_filter(AVFilterContext *ctx)
{
    int err;
    BWDIFVulkanContext *s = ctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;
    const int planes = av_pix_fmt_count_planes(s->vkctx.output_format);
    FFVulkanDescriptorSetBinding *desc;

    s->qf = ff_vk_qf_find(vkctx, VK_QUEUE_COMPUTE_BIT, 0);
    if (!s->qf) {
        av_log(ctx, AV_LOG_ERROR, "Device has no compute queues\n");
        err = AVERROR(ENOTSUP);
        goto fail;
    }

    RET(ff_vk_exec_pool_init(vkctx, s->qf, &s->e, s->qf->num*4, 0, 0, 0, NULL));

    ff_vk_shader_load(&s->shd, VK_SHADER_STAGE_COMPUTE_BIT, NULL,
                      (uint32_t [3]) { 1, 64, planes }, 0);

    desc = (FFVulkanDescriptorSetBinding []) {
        {
            .name       = "prev",
            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .elems      = planes,
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        {
            .name       = "cur",
            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .elems      = planes,
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        {
            .name       = "next",
            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .elems      = planes,
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        {
            .name       = "dst",
            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .elems      = planes,
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };

    ff_vk_shader_add_descriptor_set(vkctx, &s->shd, desc, 4, 0, 0);

    ff_vk_shader_add_push_const(&s->shd, 0, sizeof(BWDIFParameters),
                                VK_SHADER_STAGE_COMPUTE_BIT);

    RET(ff_vk_shader_link(vkctx, &s->shd,
                          ff_bwdif_comp_spv_data,
                          ff_bwdif_comp_spv_len, "main"));
    RET(ff_vk_shader_register_exec(vkctx, &s->e, &s->shd));

    s->initialized = 1;

fail:
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
                             VK_NULL_HANDLE, &params, sizeof(params));

    if (y->current_field == YADIF_FIELD_END)
        y->current_field = YADIF_FIELD_NORMAL;
}

static void bwdif_vulkan_uninit(AVFilterContext *avctx)
{
    BWDIFVulkanContext *s = avctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;

    ff_vk_exec_pool_free(vkctx, &s->e);
    ff_vk_shader_free(vkctx, &s->shd);

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

const FFFilter ff_vf_bwdif_vulkan = {
    .p.name         = "bwdif_vulkan",
    .p.description  = NULL_IF_CONFIG_SMALL("Deinterlace Vulkan frames via bwdif"),
    .p.priv_class   = &bwdif_vulkan_class,
    .p.flags        = AVFILTER_FLAG_HWDEVICE |
                      AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL,
    .priv_size      = sizeof(BWDIFVulkanContext),
    .init           = &ff_vk_filter_init,
    .uninit         = &bwdif_vulkan_uninit,
    FILTER_INPUTS(bwdif_vulkan_inputs),
    FILTER_OUTPUTS(bwdif_vulkan_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_VULKAN),
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
