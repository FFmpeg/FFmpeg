/*
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

#include "vulkan_filter.h"

static int vulkan_filter_set_device(AVFilterContext *avctx,
                                    AVBufferRef *device)
{
    FFVulkanContext *s = avctx->priv;

    av_buffer_unref(&s->device_ref);

    s->device_ref = av_buffer_ref(device);
    if (!s->device_ref)
        return AVERROR(ENOMEM);

    s->device = (AVHWDeviceContext*)s->device_ref->data;
    s->hwctx  = s->device->hwctx;

    return 0;
}

static int vulkan_filter_set_frames(AVFilterContext *avctx,
                                    AVBufferRef *frames)
{
    FFVulkanContext *s = avctx->priv;

    av_buffer_unref(&s->frames_ref);

    s->frames_ref = av_buffer_ref(frames);
    if (!s->frames_ref)
        return AVERROR(ENOMEM);

    return 0;
}

int ff_vk_filter_config_input(AVFilterLink *inlink)
{
    int err;
    AVFilterContext *avctx = inlink->dst;
    FFVulkanContext *s = avctx->priv;
    FFVulkanFunctions *vk = &s->vkfn;
    AVHWFramesContext *input_frames;

    if (!inlink->hw_frames_ctx) {
        av_log(avctx, AV_LOG_ERROR, "Vulkan filtering requires a "
               "hardware frames context on the input.\n");
        return AVERROR(EINVAL);
    }

    /* Extract the device and default output format from the first input. */
    if (avctx->inputs[0] != inlink)
        return 0;

    input_frames = (AVHWFramesContext *)inlink->hw_frames_ctx->data;
    if (input_frames->format != AV_PIX_FMT_VULKAN)
        return AVERROR(EINVAL);

    err = vulkan_filter_set_device(avctx, input_frames->device_ref);
    if (err < 0)
        return err;
    err = vulkan_filter_set_frames(avctx, inlink->hw_frames_ctx);
    if (err < 0)
        return err;

    s->extensions = ff_vk_extensions_to_mask(s->hwctx->enabled_dev_extensions,
                                             s->hwctx->nb_enabled_dev_extensions);

    err = ff_vk_load_functions(s->device, &s->vkfn, s->extensions, 1, 1);
    if (err < 0)
        return err;

    vk->GetPhysicalDeviceProperties(s->hwctx->phys_dev, &s->props);
    vk->GetPhysicalDeviceMemoryProperties(s->hwctx->phys_dev, &s->mprops);

    /* Default output parameters match input parameters. */
    s->input_format = input_frames->sw_format;
    if (s->output_format == AV_PIX_FMT_NONE)
        s->output_format = input_frames->sw_format;
    if (!s->output_width)
        s->output_width  = inlink->w;
    if (!s->output_height)
        s->output_height = inlink->h;

    return 0;
}

int ff_vk_filter_config_output_inplace(AVFilterLink *outlink)
{
    int err;
    AVFilterContext *avctx = outlink->src;
    FFVulkanContext *s = avctx->priv;

    av_buffer_unref(&outlink->hw_frames_ctx);

    if (!s->device_ref) {
        if (!avctx->hw_device_ctx) {
            av_log(avctx, AV_LOG_ERROR, "Vulkan filtering requires a "
                   "Vulkan device.\n");
            return AVERROR(EINVAL);
        }

        err = vulkan_filter_set_device(avctx, avctx->hw_device_ctx);
        if (err < 0)
            return err;
    }

    outlink->hw_frames_ctx = av_buffer_ref(s->frames_ref);
    if (!outlink->hw_frames_ctx)
        return AVERROR(ENOMEM);

    outlink->w = s->output_width;
    outlink->h = s->output_height;

    return 0;
}

int ff_vk_filter_config_output(AVFilterLink *outlink)
{
    int err;
    AVFilterContext *avctx = outlink->src;
    FFVulkanContext *s = avctx->priv;
    AVBufferRef *output_frames_ref;
    AVHWFramesContext *output_frames;

    av_buffer_unref(&outlink->hw_frames_ctx);

    if (!s->device_ref) {
        if (!avctx->hw_device_ctx) {
            av_log(avctx, AV_LOG_ERROR, "Vulkan filtering requires a "
                   "Vulkan device.\n");
            return AVERROR(EINVAL);
        }

        err = vulkan_filter_set_device(avctx, avctx->hw_device_ctx);
        if (err < 0)
            return err;
    }

    output_frames_ref = av_hwframe_ctx_alloc(s->device_ref);
    if (!output_frames_ref) {
        err = AVERROR(ENOMEM);
        goto fail;
    }
    output_frames = (AVHWFramesContext*)output_frames_ref->data;

    output_frames->format    = AV_PIX_FMT_VULKAN;
    output_frames->sw_format = s->output_format;
    output_frames->width     = s->output_width;
    output_frames->height    = s->output_height;

    err = av_hwframe_ctx_init(output_frames_ref);
    if (err < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to initialise output "
               "frames: %d.\n", err);
        goto fail;
    }

    outlink->hw_frames_ctx = output_frames_ref;
    outlink->w = s->output_width;
    outlink->h = s->output_height;

    return 0;
fail:
    av_buffer_unref(&output_frames_ref);
    return err;
}

int ff_vk_filter_init(AVFilterContext *avctx)
{
    FFVulkanContext *s = avctx->priv;

    s->output_format = AV_PIX_FMT_NONE;

    return 0;
}
