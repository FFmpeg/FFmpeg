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

#include "filters.h"
#include "video.h"

extern const unsigned char ff_avgblur_comp_spv_data[];
extern const unsigned int ff_avgblur_comp_spv_len;

typedef struct AvgBlurVulkanContext {
    FFVulkanContext vkctx;

    int initialized;
    FFVkExecPool e;
    AVVulkanDeviceQueueFamily *qf;
    FFVulkanShader shd;

    /* Push constants / options */
    struct {
        float filter_norm[4];
        int32_t filter_len[2];
        uint32_t planes;
    } opts;

    int size_x;
    int size_y;
} AvgBlurVulkanContext;

static av_cold int init_filter(AVFilterContext *ctx, AVFrame *in)
{
    int err;
    AvgBlurVulkanContext *s = ctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;
    const int planes = av_pix_fmt_count_planes(s->vkctx.output_format);

    s->qf = ff_vk_qf_find(vkctx, VK_QUEUE_COMPUTE_BIT, 0);
    if (!s->qf) {
        av_log(ctx, AV_LOG_ERROR, "Device has no compute queues\n");
        err = AVERROR(ENOTSUP);
        goto fail;
    }

    RET(ff_vk_exec_pool_init(vkctx, s->qf, &s->e, s->qf->num*4, 0, 0, 0, NULL));

    ff_vk_shader_load(&s->shd, VK_SHADER_STAGE_COMPUTE_BIT,
                      NULL, (uint32_t []) { 32, 1, planes }, 0);

    ff_vk_shader_add_push_const(&s->shd, 0, sizeof(s->opts),
                                VK_SHADER_STAGE_COMPUTE_BIT);

    const FFVulkanDescriptorSetBinding desc_set[] = {
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
    ff_vk_shader_add_descriptor_set(vkctx, &s->shd, desc_set, 2, 0, 0);

    RET(ff_vk_shader_link(vkctx, &s->shd,
                          ff_avgblur_comp_spv_data,
                          ff_avgblur_comp_spv_len, "main"));

    RET(ff_vk_shader_register_exec(vkctx, &s->e, &s->shd));

    s->initialized = 1;
    s->opts.filter_len[0] = s->size_x - 1;
    s->opts.filter_len[1] = s->size_y - 1;

    s->opts.filter_norm[0] = s->opts.filter_len[0]*2 + 1;
    s->opts.filter_norm[0] = 1.0/(s->opts.filter_norm[0]*s->opts.filter_norm[0]);
    s->opts.filter_norm[1] = s->opts.filter_norm[0];
    s->opts.filter_norm[2] = s->opts.filter_norm[0];
    s->opts.filter_norm[3] = s->opts.filter_norm[0];

fail:
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

    RET(ff_vk_filter_process_simple(&s->vkctx, &s->e, &s->shd,
                                    out, in, VK_NULL_HANDLE,
                                    &s->opts, sizeof(s->opts)));

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

    ff_vk_exec_pool_free(vkctx, &s->e);
    ff_vk_shader_free(vkctx, &s->shd);

    ff_vk_uninit(&s->vkctx);

    s->initialized = 0;
}

#define OFFSET(x) offsetof(AvgBlurVulkanContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)
static const AVOption avgblur_vulkan_options[] = {
    { "sizeX",  "Set horizontal radius", OFFSET(size_x), AV_OPT_TYPE_INT, { .i64 = 3 }, 1, 32, .flags = FLAGS },
    { "sizeY",  "Set vertical radius", OFFSET(size_y), AV_OPT_TYPE_INT, { .i64 = 3 }, 1, 32, .flags = FLAGS },
    { "planes", "Set planes to filter (bitmask)", OFFSET(opts.planes), AV_OPT_TYPE_INT, {.i64 = 0xF}, 0, 0xF, .flags = FLAGS },
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

const FFFilter ff_vf_avgblur_vulkan = {
    .p.name         = "avgblur_vulkan",
    .p.description  = NULL_IF_CONFIG_SMALL("Apply avgblur mask to input video"),
    .p.priv_class   = &avgblur_vulkan_class,
    .p.flags        = AVFILTER_FLAG_HWDEVICE,
    .priv_size      = sizeof(AvgBlurVulkanContext),
    .init           = &ff_vk_filter_init,
    .uninit         = &avgblur_vulkan_uninit,
    FILTER_INPUTS(avgblur_vulkan_inputs),
    FILTER_OUTPUTS(avgblur_vulkan_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_VULKAN),
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
