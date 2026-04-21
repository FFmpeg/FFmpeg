/*
 * copyright (c) 2021-2022 Wu Jianhua <jianhua.wu@intel.com>
 * Copyright (c) Lynne
 *
 * The blend modes are based on the blend.c.
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
#include "blend.h"
#include "video.h"

extern const unsigned char ff_blend_comp_spv_data[];
extern const unsigned int ff_blend_comp_spv_len;

#define IN_TOP    0
#define IN_BOTTOM 1

typedef struct FilterParamsVulkan {
    float opacity[4];
    int mode[4];
} FilterParamsVulkan;

typedef struct BlendVulkanContext {
    FFVulkanContext vkctx;
    FFFrameSync fs;

    int initialized;
    FFVkExecPool e;
    AVVulkanDeviceQueueFamily *qf;
    FFVulkanShader shd;

    FilterParamsVulkan params;
    float all_opacity;
    /* enum BlendMode */
    int all_mode;
} BlendVulkanContext;

static int config_params(AVFilterContext *avctx)
{
    BlendVulkanContext *s = avctx->priv;

    if (s->all_opacity < 1.0) {
        s->params.opacity[0] = s->all_opacity;
        s->params.opacity[1] = s->all_opacity;
        s->params.opacity[2] = s->all_opacity;
        s->params.opacity[3] = s->all_opacity;
    }

    if (s->all_mode >= 0) {
        s->params.mode[0] = s->all_mode;
        s->params.mode[1] = s->all_mode;
        s->params.mode[2] = s->all_mode;
        s->params.mode[3] = s->all_mode;
    }

    return 0;
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    int ret = ff_filter_process_command(ctx, cmd, args, res, res_len, flags);
    if (ret < 0)
        return ret;

    return config_params(ctx);
}

static av_cold int init_filter(AVFilterContext *avctx)
{
    int err = 0;
    BlendVulkanContext *s = avctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;
    const int planes = av_pix_fmt_count_planes(s->vkctx.output_format);
    FFVulkanDescriptorSetBinding *desc;

    config_params(avctx);

    s->qf = ff_vk_qf_find(vkctx, VK_QUEUE_COMPUTE_BIT, 0);
    if (!s->qf) {
        av_log(avctx, AV_LOG_ERROR, "Device has no compute queues\n");
        err = AVERROR(ENOTSUP);
        goto fail;
    }

    RET(ff_vk_exec_pool_init(vkctx, s->qf, &s->e, s->qf->num*4, 0, 0, 0, NULL));

    SPEC_LIST_CREATE(sl, 1, 1*sizeof(uint32_t))
    SPEC_LIST_ADD(sl, 0, 32, planes);

    ff_vk_shader_load(&s->shd, VK_SHADER_STAGE_COMPUTE_BIT, sl,
                      (int []) { 32, 32, 1 }, 0);

    ff_vk_shader_add_push_const(&s->shd, 0, sizeof(s->params),
                                VK_SHADER_STAGE_COMPUTE_BIT);

    desc = (FFVulkanDescriptorSetBinding []) {
        { /* top_images */
            .type   = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stages = VK_SHADER_STAGE_COMPUTE_BIT,
            .elems  = planes,
        },
        { /* bottom_images */
            .type   = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stages = VK_SHADER_STAGE_COMPUTE_BIT,
            .elems  = planes,
        },
        { /* output_images */
            .type   = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stages = VK_SHADER_STAGE_COMPUTE_BIT,
            .elems  = planes,
        },
    };
    ff_vk_shader_add_descriptor_set(vkctx, &s->shd, desc, 3, 0, 0);

    RET(ff_vk_shader_link(vkctx, &s->shd,
                          ff_blend_comp_spv_data,
                          ff_blend_comp_spv_len, "main"));

    RET(ff_vk_shader_register_exec(vkctx, &s->e, &s->shd));

    s->initialized = 1;

fail:
    return err;
}

static int blend_frame(FFFrameSync *fs)
{
    int err;
    AVFilterContext *avctx = fs->parent;
    BlendVulkanContext *s = avctx->priv;
    AVFilterLink *outlink = avctx->outputs[0];
    AVFrame *top, *bottom, *out;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    RET(ff_framesync_get_frame(fs, IN_TOP,    &top,    0));
    RET(ff_framesync_get_frame(fs, IN_BOTTOM, &bottom, 0));

    RET(av_frame_copy_props(out, top));

    if (!s->initialized) {
        AVHWFramesContext *top_fc = (AVHWFramesContext*)top->hw_frames_ctx->data;
        AVHWFramesContext *bottom_fc = (AVHWFramesContext*)bottom->hw_frames_ctx->data;
        if (top_fc->sw_format != bottom_fc->sw_format) {
            av_log(avctx, AV_LOG_ERROR,
                   "Currently the sw format of the bottom video need to match the top!\n");
            err = AVERROR(EINVAL);
            goto fail;
        }
        RET(init_filter(avctx));
    }

    RET(ff_vk_filter_process_Nin(&s->vkctx, &s->e, &s->shd,
                                 out, (AVFrame *[]){ top, bottom }, 2,
                                 VK_NULL_HANDLE, 1, &s->params, sizeof(s->params)));

    return ff_filter_frame(outlink, out);

fail:
    av_frame_free(&out);
    return err;
}

static av_cold int init(AVFilterContext *avctx)
{
    BlendVulkanContext *s = avctx->priv;

    s->fs.on_event = blend_frame;

    return ff_vk_filter_init(avctx);
}

static av_cold void uninit(AVFilterContext *avctx)
{
    BlendVulkanContext *s = avctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;

    ff_vk_exec_pool_free(vkctx, &s->e);
    ff_vk_shader_free(vkctx, &s->shd);

    ff_vk_uninit(&s->vkctx);
    ff_framesync_uninit(&s->fs);

    s->initialized = 0;
}

static int config_props_output(AVFilterLink *outlink)
{
    int err;
    FilterLink *outl       = ff_filter_link(outlink);
    AVFilterContext *avctx = outlink->src;
    BlendVulkanContext *s = avctx->priv;
    AVFilterLink *toplink = avctx->inputs[IN_TOP];
    FilterLink   *tl      = ff_filter_link(toplink);
    AVFilterLink *bottomlink = avctx->inputs[IN_BOTTOM];

    if (toplink->w != bottomlink->w || toplink->h != bottomlink->h) {
        av_log(avctx, AV_LOG_ERROR, "First input link %s parameters "
                "(size %dx%d) do not match the corresponding "
                "second input link %s parameters (size %dx%d)\n",
                avctx->input_pads[IN_TOP].name, toplink->w, toplink->h,
                avctx->input_pads[IN_BOTTOM].name, bottomlink->w, bottomlink->h);
        return AVERROR(EINVAL);
    }

    outlink->sample_aspect_ratio = toplink->sample_aspect_ratio;
    outl->frame_rate = tl->frame_rate;

    RET(ff_vk_filter_config_output(outlink));

    RET(ff_framesync_init_dualinput(&s->fs, avctx));

    RET(ff_framesync_configure(&s->fs));
    outlink->time_base = s->fs.time_base;

    RET(config_params(avctx));

fail:
    return err;
}

static int activate(AVFilterContext *avctx)
{
    BlendVulkanContext *s = avctx->priv;
    return ff_framesync_activate(&s->fs);
}

#define OFFSET(x) offsetof(BlendVulkanContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)

static const AVOption blend_vulkan_options[] = {
    { "c0_mode", "set component #0 blend mode", OFFSET(params.mode[0]), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, BLEND_NB - 1, FLAGS, .unit = "mode" },
    { "c1_mode", "set component #1 blend mode", OFFSET(params.mode[1]), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, BLEND_NB - 1, FLAGS, .unit = "mode" },
    { "c2_mode", "set component #2 blend mode", OFFSET(params.mode[2]), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, BLEND_NB - 1, FLAGS, .unit = "mode" },
    { "c3_mode", "set component #3 blend mode", OFFSET(params.mode[3]), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, BLEND_NB - 1, FLAGS, .unit = "mode" },
    { "all_mode", "set blend mode for all components", OFFSET(all_mode), AV_OPT_TYPE_INT, { .i64 = -1 }, -1, BLEND_NB - 1, FLAGS, .unit = "mode" },
      { "addition",   "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_ADDITION},   0, 0, FLAGS, .unit = "mode" },
      { "addition128","", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_GRAINMERGE}, 0, 0, FLAGS, .unit = "mode" },
      { "grainmerge", "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_GRAINMERGE}, 0, 0, FLAGS, .unit = "mode" },
      { "and",        "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_AND},        0, 0, FLAGS, .unit = "mode" },
      { "average",    "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_AVERAGE},    0, 0, FLAGS, .unit = "mode" },
      { "burn",       "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_BURN},       0, 0, FLAGS, .unit = "mode" },
      { "darken",     "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_DARKEN},     0, 0, FLAGS, .unit = "mode" },
      { "difference", "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_DIFFERENCE}, 0, 0, FLAGS, .unit = "mode" },
      { "difference128", "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_GRAINEXTRACT}, 0, 0, FLAGS, .unit = "mode" },
      { "grainextract", "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_GRAINEXTRACT}, 0, 0, FLAGS, .unit = "mode" },
      { "divide",     "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_DIVIDE},     0, 0, FLAGS, .unit = "mode" },
      { "dodge",      "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_DODGE},      0, 0, FLAGS, .unit = "mode" },
      { "exclusion",  "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_EXCLUSION},  0, 0, FLAGS, .unit = "mode" },
      { "extremity",  "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_EXTREMITY},  0, 0, FLAGS, .unit = "mode" },
      { "freeze",     "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_FREEZE},     0, 0, FLAGS, .unit = "mode" },
      { "glow",       "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_GLOW},       0, 0, FLAGS, .unit = "mode" },
      { "hardlight",  "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_HARDLIGHT},  0, 0, FLAGS, .unit = "mode" },
      { "hardmix",    "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_HARDMIX},    0, 0, FLAGS, .unit = "mode" },
      { "heat",       "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_HEAT},       0, 0, FLAGS, .unit = "mode" },
      { "lighten",    "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_LIGHTEN},    0, 0, FLAGS, .unit = "mode" },
      { "linearlight","", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_LINEARLIGHT},0, 0, FLAGS, .unit = "mode" },
      { "multiply",   "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_MULTIPLY},   0, 0, FLAGS, .unit = "mode" },
      { "multiply128","", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_MULTIPLY128},0, 0, FLAGS, .unit = "mode" },
      { "negation",   "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_NEGATION},   0, 0, FLAGS, .unit = "mode" },
      { "normal",     "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_NORMAL},     0, 0, FLAGS, .unit = "mode" },
      { "or",         "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_OR},         0, 0, FLAGS, .unit = "mode" },
      { "overlay",    "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_OVERLAY},    0, 0, FLAGS, .unit = "mode" },
      { "phoenix",    "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_PHOENIX},    0, 0, FLAGS, .unit = "mode" },
      { "pinlight",   "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_PINLIGHT},   0, 0, FLAGS, .unit = "mode" },
      { "reflect",    "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_REFLECT},    0, 0, FLAGS, .unit = "mode" },
      { "screen",     "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_SCREEN},     0, 0, FLAGS, .unit = "mode" },
      { "softlight",  "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_SOFTLIGHT},  0, 0, FLAGS, .unit = "mode" },
      { "subtract",   "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_SUBTRACT},   0, 0, FLAGS, .unit = "mode" },
      { "vividlight", "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_VIVIDLIGHT}, 0, 0, FLAGS, .unit = "mode" },
      { "xor",        "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_XOR},        0, 0, FLAGS, .unit = "mode" },
      { "softdifference","", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_SOFTDIFFERENCE}, 0, 0, FLAGS, .unit = "mode" },
      { "geometric",  "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_GEOMETRIC},  0, 0, FLAGS, .unit = "mode" },
      { "harmonic",   "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_HARMONIC},   0, 0, FLAGS, .unit = "mode" },
      { "bleach",     "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_BLEACH},     0, 0, FLAGS, .unit = "mode" },
      { "stain",      "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_STAIN},      0, 0, FLAGS, .unit = "mode" },
      { "interpolate","", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_INTERPOLATE},0, 0, FLAGS, .unit = "mode" },
      { "hardoverlay","", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_HARDOVERLAY},0, 0, FLAGS, .unit = "mode" },

    { "c0_opacity",  "set color component #0 opacity", OFFSET(params.opacity[0]), AV_OPT_TYPE_FLOAT, { .dbl = 1.0 }, 0, 1, FLAGS },
    { "c1_opacity",  "set color component #1 opacity", OFFSET(params.opacity[1]), AV_OPT_TYPE_FLOAT, { .dbl = 1.0 }, 0, 1, FLAGS },
    { "c2_opacity",  "set color component #2 opacity", OFFSET(params.opacity[2]), AV_OPT_TYPE_FLOAT, { .dbl = 1.0 }, 0, 1, FLAGS },
    { "c3_opacity",  "set color component #3 opacity", OFFSET(params.opacity[3]), AV_OPT_TYPE_FLOAT, { .dbl = 1.0 }, 0, 1, FLAGS },
    { "all_opacity", "set opacity for all color components", OFFSET(all_opacity), AV_OPT_TYPE_FLOAT, { .dbl = 1.0 }, 0, 1, FLAGS },

    { NULL }
};

AVFILTER_DEFINE_CLASS(blend_vulkan);

static const AVFilterPad blend_vulkan_inputs[] = {
    {
        .name         = "top",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = &ff_vk_filter_config_input,
    },
    {
        .name         = "bottom",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = &ff_vk_filter_config_input,
    },
};


static const AVFilterPad blend_vulkan_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = &config_props_output,
    }
};

const FFFilter ff_vf_blend_vulkan = {
    .p.name          = "blend_vulkan",
    .p.description   = NULL_IF_CONFIG_SMALL("Blend two video frames in Vulkan"),
    .p.priv_class    = &blend_vulkan_class,
    .p.flags         = AVFILTER_FLAG_HWDEVICE,
    .priv_size       = sizeof(BlendVulkanContext),
    .init            = &init,
    .uninit          = &uninit,
    .activate        = &activate,
    FILTER_INPUTS(blend_vulkan_inputs),
    FILTER_OUTPUTS(blend_vulkan_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_VULKAN),
    .flags_internal  = FF_FILTER_FLAG_HWFRAME_AWARE,
    .process_command = &process_command,
};
