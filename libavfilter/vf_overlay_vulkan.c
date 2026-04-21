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

#include "libavutil/opt.h"
#include "vulkan_filter.h"

#include "filters.h"
#include "framesync.h"
#include "video.h"

extern const unsigned char ff_overlay_comp_spv_data[];
extern const unsigned int ff_overlay_comp_spv_len;

typedef struct OverlayVulkanContext {
    FFVulkanContext vkctx;
    FFFrameSync fs;

    int initialized;
    FFVkExecPool e;
    AVVulkanDeviceQueueFamily *qf;
    FFVulkanShader shd;

    /* Push constants / options */
    struct {
        int32_t o_offset[2*4];
        int32_t o_size[2*4];
    } opts;

    int overlay_x;
    int overlay_y;
    int overlay_w;
    int overlay_h;
} OverlayVulkanContext;

static av_cold int init_filter(AVFilterContext *ctx)
{
    int err;
    OverlayVulkanContext *s = ctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;
    const int planes = av_pix_fmt_count_planes(s->vkctx.output_format);
    const int ialpha = av_pix_fmt_desc_get(s->vkctx.input_format)->flags & AV_PIX_FMT_FLAG_ALPHA;
    const AVPixFmtDescriptor *pix_desc = av_pix_fmt_desc_get(s->vkctx.output_format);
    FFVulkanShader *shd = &s->shd;

    s->qf = ff_vk_qf_find(vkctx, VK_QUEUE_COMPUTE_BIT, 0);
    if (!s->qf) {
        av_log(ctx, AV_LOG_ERROR, "Device has no compute queues\n");
        err = AVERROR(ENOTSUP);
        goto fail;
    }

    RET(ff_vk_exec_pool_init(vkctx, s->qf, &s->e, s->qf->num*4, 0, 0, 0, NULL));

    SPEC_LIST_CREATE(sl, 2, 2*sizeof(uint32_t))
    SPEC_LIST_ADD(sl, 0, 32, planes);
    SPEC_LIST_ADD(sl, 1, 32, ialpha);

    ff_vk_shader_load(&s->shd, VK_SHADER_STAGE_COMPUTE_BIT, sl,
                      (int []) { 32, 32, 1 }, 0);

    ff_vk_shader_add_push_const(&s->shd, 0, sizeof(s->opts),
                                VK_SHADER_STAGE_COMPUTE_BIT);

    const FFVulkanDescriptorSetBinding desc[] = {
        { /* main_img */
            .type   = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stages = VK_SHADER_STAGE_COMPUTE_BIT,
            .elems  = planes,
        },
        { /* overlay_img */
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
    ff_vk_shader_add_descriptor_set(vkctx, &s->shd, desc, 3, 0, 0);

    RET(ff_vk_shader_link(vkctx, shd,
                          ff_overlay_comp_spv_data,
                          ff_overlay_comp_spv_len, "main"));

    RET(ff_vk_shader_register_exec(vkctx, &s->e, &s->shd));

    s->opts.o_offset[0] = s->overlay_x;
    s->opts.o_offset[1] = s->overlay_y;
    s->opts.o_offset[2] = s->opts.o_offset[0] >> pix_desc->log2_chroma_w;
    s->opts.o_offset[3] = s->opts.o_offset[1] >> pix_desc->log2_chroma_h;
    s->opts.o_offset[4] = s->opts.o_offset[0] >> pix_desc->log2_chroma_w;
    s->opts.o_offset[5] = s->opts.o_offset[1] >> pix_desc->log2_chroma_h;

    s->opts.o_size[0] = s->overlay_w;
    s->opts.o_size[1] = s->overlay_h;
    s->opts.o_size[2] = s->opts.o_size[0] >> pix_desc->log2_chroma_w;
    s->opts.o_size[3] = s->opts.o_size[1] >> pix_desc->log2_chroma_h;
    s->opts.o_size[4] = s->opts.o_size[0] >> pix_desc->log2_chroma_w;
    s->opts.o_size[5] = s->opts.o_size[1] >> pix_desc->log2_chroma_h;

    s->initialized = 1;

fail:
    return err;
}

static int overlay_vulkan_blend(FFFrameSync *fs)
{
    int err;
    AVFilterContext *ctx = fs->parent;
    OverlayVulkanContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *input_main, *input_overlay, *out;

    err = ff_framesync_get_frame(fs, 0, &input_main, 0);
    if (err < 0)
        goto fail;
    err = ff_framesync_get_frame(fs, 1, &input_overlay, 0);
    if (err < 0)
        goto fail;

    if (!input_main || !input_overlay)
        return 0;

    if (!s->initialized) {
        AVHWFramesContext *main_fc = (AVHWFramesContext*)input_main->hw_frames_ctx->data;
        AVHWFramesContext *overlay_fc = (AVHWFramesContext*)input_overlay->hw_frames_ctx->data;
        if (main_fc->sw_format != overlay_fc->sw_format) {
            av_log(ctx, AV_LOG_ERROR, "Mismatching sw formats!\n");
            return AVERROR(EINVAL);
        }

        s->overlay_w = input_overlay->width;
        s->overlay_h = input_overlay->height;

        RET(init_filter(ctx));
    }

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    RET(ff_vk_filter_process_Nin(&s->vkctx, &s->e, &s->shd,
                                 out, (AVFrame *[]){ input_main, input_overlay }, 2,
                                 VK_NULL_HANDLE, 1, &s->opts, sizeof(s->opts)));

    err = av_frame_copy_props(out, input_main);
    if (err < 0)
        goto fail;

    return ff_filter_frame(outlink, out);

fail:
    av_frame_free(&out);
    return err;
}

static int overlay_vulkan_config_output(AVFilterLink *outlink)
{
    int err;
    AVFilterContext *avctx = outlink->src;
    OverlayVulkanContext *s = avctx->priv;

    err = ff_vk_filter_config_output(outlink);
    if (err < 0)
        return err;

    err = ff_framesync_init_dualinput(&s->fs, avctx);
    if (err < 0)
        return err;

    return ff_framesync_configure(&s->fs);
}

static int overlay_vulkan_activate(AVFilterContext *avctx)
{
    OverlayVulkanContext *s = avctx->priv;

    return ff_framesync_activate(&s->fs);
}

static av_cold int overlay_vulkan_init(AVFilterContext *avctx)
{
    OverlayVulkanContext *s = avctx->priv;

    s->fs.on_event = &overlay_vulkan_blend;

    return ff_vk_filter_init(avctx);
}

static void overlay_vulkan_uninit(AVFilterContext *avctx)
{
    OverlayVulkanContext *s = avctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;

    ff_vk_exec_pool_free(vkctx, &s->e);
    ff_vk_shader_free(vkctx, &s->shd);

    ff_vk_uninit(&s->vkctx);
    ff_framesync_uninit(&s->fs);

    s->initialized = 0;
}

#define OFFSET(x) offsetof(OverlayVulkanContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)
static const AVOption overlay_vulkan_options[] = {
    { "x", "Set horizontal offset", OFFSET(overlay_x), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, .flags = FLAGS },
    { "y", "Set vertical offset",   OFFSET(overlay_y), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, .flags = FLAGS },
    { NULL },
};

AVFILTER_DEFINE_CLASS(overlay_vulkan);

static const AVFilterPad overlay_vulkan_inputs[] = {
    {
        .name         = "main",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = &ff_vk_filter_config_input,
    },
    {
        .name         = "overlay",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = &ff_vk_filter_config_input,
    },
};

static const AVFilterPad overlay_vulkan_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = &overlay_vulkan_config_output,
    },
};

const FFFilter ff_vf_overlay_vulkan = {
    .p.name         = "overlay_vulkan",
    .p.description  = NULL_IF_CONFIG_SMALL("Overlay a source on top of another"),
    .p.priv_class   = &overlay_vulkan_class,
    .p.flags        = AVFILTER_FLAG_HWDEVICE,
    .priv_size      = sizeof(OverlayVulkanContext),
    .init           = &overlay_vulkan_init,
    .uninit         = &overlay_vulkan_uninit,
    .activate       = &overlay_vulkan_activate,
    FILTER_INPUTS(overlay_vulkan_inputs),
    FILTER_OUTPUTS(overlay_vulkan_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_VULKAN),
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
