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

#include "filters.h"
#include "vulkan_filter.h"
#include "libavutil/vulkan_loader.h"

int ff_vk_filter_init_context(AVFilterContext *avctx, FFVulkanContext *s,
                              AVBufferRef *frames_ref,
                              int width, int height, enum AVPixelFormat sw_format)
{
    int err;
    AVHWFramesContext *frames_ctx;
    AVHWDeviceContext *device_ctx;
    AVVulkanFramesContext *vk_frames;
    AVVulkanDeviceContext *vk_dev;
    AVBufferRef *device_ref = avctx->hw_device_ctx;

    /* Check if context is reusable as-is */
    if (frames_ref) {
        int no_storage = 0;
        FFVulkanFunctions *vk;
        VkImageUsageFlagBits usage_req;
        const VkFormat *sub = av_vkfmt_from_pixfmt(sw_format);

        frames_ctx = (AVHWFramesContext *)frames_ref->data;
        device_ctx = (AVHWDeviceContext *)frames_ctx->device_ref->data;
        vk_frames = frames_ctx->hwctx;
        vk_dev = device_ctx->hwctx;

        /* Width and height mismatch */
        if (width != frames_ctx->width ||
            height != frames_ctx->height)
            goto skip;

        /* Format mismatch */
        if (sw_format != frames_ctx->sw_format)
            goto skip;

        /* Don't let linear through. */
        if (vk_frames->tiling == VK_IMAGE_TILING_LINEAR)
            goto skip;

        s->extensions = ff_vk_extensions_to_mask(vk_dev->enabled_dev_extensions,
                                                 vk_dev->nb_enabled_dev_extensions);

        /* More advanced format checks */
        err = ff_vk_load_functions(device_ctx, &s->vkfn, s->extensions, 1, 1);
        if (err < 0)
            return err;
        vk = &s->vkfn;

        /* Usage mismatch */
        usage_req = VK_IMAGE_USAGE_SAMPLED_BIT |
                    VK_IMAGE_USAGE_STORAGE_BIT;

        /* If format supports hardware encoding, make sure
         * the context includes it. */
        if (vk_frames->format[1] == VK_FORMAT_UNDEFINED &&
            (s->extensions & (FF_VK_EXT_VIDEO_ENCODE_QUEUE |
                              FF_VK_EXT_VIDEO_MAINTENANCE_1))) {
            VkFormatProperties3 fprops = {
                .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3,
            };
            VkFormatProperties2 prop = {
                .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2,
                .pNext = &fprops,
            };
            vk->GetPhysicalDeviceFormatProperties2(vk_dev->phys_dev,
                                                   vk_frames->format[0],
                                                   &prop);
            if (fprops.optimalTilingFeatures & VK_FORMAT_FEATURE_2_VIDEO_ENCODE_INPUT_BIT_KHR)
                usage_req |= VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR;
        }

        if ((vk_frames->usage & usage_req) != usage_req)
            goto skip;

        /* Check if the subformats can do storage */
        for (int i = 0; sub[i] != VK_FORMAT_UNDEFINED; i++) {
            VkFormatProperties2 prop = {
                .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2,
            };
            vk->GetPhysicalDeviceFormatProperties2(vk_dev->phys_dev, sub[i],
                                                   &prop);
            no_storage |= !(prop.formatProperties.optimalTilingFeatures &
                            VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT);
        }

        /* Check if it's usable */
        if (no_storage) {
skip:
            av_log(avctx, AV_LOG_VERBOSE, "Cannot reuse context, creating a new one\n");
            device_ref = frames_ctx->device_ref;
            frames_ref = NULL;
        } else {
            av_log(avctx, AV_LOG_VERBOSE, "Reusing existing frames context\n");
            frames_ref = av_buffer_ref(frames_ref);
            if (!frames_ref)
                return AVERROR(ENOMEM);
        }
    }

    if (!frames_ref) {
        if (!device_ref) {
            av_log(avctx, AV_LOG_ERROR,
                   "Vulkan filtering requires a device context!\n");
            return AVERROR(EINVAL);
        }

        frames_ref = av_hwframe_ctx_alloc(device_ref);

        frames_ctx = (AVHWFramesContext *)frames_ref->data;
        frames_ctx->format    = AV_PIX_FMT_VULKAN;
        frames_ctx->sw_format = sw_format;
        frames_ctx->width     = width;
        frames_ctx->height    = height;

        vk_frames = frames_ctx->hwctx;
        vk_frames->tiling = VK_IMAGE_TILING_OPTIMAL;
        vk_frames->usage  = VK_IMAGE_USAGE_SAMPLED_BIT |
                            VK_IMAGE_USAGE_STORAGE_BIT |
                            VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

        err = av_hwframe_ctx_init(frames_ref);
        if (err < 0) {
            av_buffer_unref(&frames_ref);
            return err;
        }

        device_ctx = (AVHWDeviceContext *)frames_ctx->device_ref->data;
        vk_dev = device_ctx->hwctx;
    }

    s->extensions = ff_vk_extensions_to_mask(vk_dev->enabled_dev_extensions,
                                             vk_dev->nb_enabled_dev_extensions);
    s->extensions |= ff_vk_extensions_to_mask(vk_dev->enabled_inst_extensions,
                                              vk_dev->nb_enabled_inst_extensions);

    err = ff_vk_load_functions(device_ctx, &s->vkfn, s->extensions, 1, 1);
    if (err < 0) {
        av_buffer_unref(&frames_ref);
        return err;
    }

    s->frames_ref = frames_ref;
    s->frames = frames_ctx;
    s->hwfc = vk_frames;
    s->device = device_ctx;
    s->hwctx = device_ctx->hwctx;

    err = ff_vk_load_props(s);
    if (err < 0)
        av_buffer_unref(&s->frames_ref);

    return err;
}

int ff_vk_filter_config_input(AVFilterLink *inlink)
{
    FilterLink *l = ff_filter_link(inlink);
    AVHWFramesContext *input_frames;
    AVFilterContext *avctx = inlink->dst;
    FFVulkanContext *s = inlink->dst->priv;

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
    s->input_frames_ref = l->hw_frames_ctx;

    /* Defaults */
    s->input_format = input_frames->sw_format;
    s->output_format = input_frames->sw_format;
    s->output_width = inlink->w;
    s->output_height = inlink->h;

    return 0;
}

int ff_vk_filter_config_output(AVFilterLink *outlink)
{
    int err;
    FilterLink *l = ff_filter_link(outlink);
    FFVulkanContext *s = outlink->src->priv;

    av_buffer_unref(&l->hw_frames_ctx);

    err = ff_vk_filter_init_context(outlink->src, s, s->input_frames_ref,
                                    s->output_width, s->output_height,
                                    s->output_format);
    if (err < 0)
        return err;

    l->hw_frames_ctx = av_buffer_ref(s->frames_ref);
    if (!l->hw_frames_ctx)
        return AVERROR(ENOMEM);

    outlink->w = s->output_width;
    outlink->h = s->output_height;

    return err;
}

int ff_vk_filter_init(AVFilterContext *avctx)
{
    FFVulkanContext *s = avctx->priv;

    s->output_format = AV_PIX_FMT_NONE;

    return 0;
}

int ff_vk_filter_process_simple(FFVulkanContext *vkctx, FFVkExecPool *e,
                                FFVulkanShader *shd, AVFrame *out_f, AVFrame *in_f,
                                VkSampler sampler, void *push_src, size_t push_size)
{
    int err = 0;
    FFVulkanFunctions *vk = &vkctx->vkfn;
    VkImageView in_views[AV_NUM_DATA_POINTERS];
    VkImageView out_views[AV_NUM_DATA_POINTERS];
    VkImageMemoryBarrier2 img_bar[37];
    int nb_img_bar = 0;
    VkImageLayout in_layout = sampler != VK_NULL_HANDLE ?
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL :
                              VK_IMAGE_LAYOUT_GENERAL;

    /* Update descriptors and init the exec context */
    FFVkExecContext *exec = ff_vk_exec_get(vkctx, e);
    ff_vk_exec_start(vkctx, exec);

    RET(ff_vk_exec_add_dep_frame(vkctx, exec, out_f,
                                 VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT));
    RET(ff_vk_create_imageviews(vkctx, exec, out_views, out_f, FF_VK_REP_FLOAT));
    ff_vk_shader_update_img_array(vkctx, exec, shd, out_f, out_views, 0, !!in_f,
                                  VK_IMAGE_LAYOUT_GENERAL,
                                  VK_NULL_HANDLE);
    if (in_f) {
        RET(ff_vk_exec_add_dep_frame(vkctx, exec, in_f,
                                     VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                     VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT));
        RET(ff_vk_create_imageviews(vkctx, exec, in_views,  in_f, FF_VK_REP_FLOAT));
        ff_vk_shader_update_img_array(vkctx, exec, shd,  in_f,  in_views, 0, 0,
                                      in_layout,
                                      sampler);
    }

    /* Bind pipeline, update push data */
    ff_vk_exec_bind_shader(vkctx, exec, shd);
    if (push_src)
        ff_vk_shader_update_push_const(vkctx, exec, shd, VK_SHADER_STAGE_COMPUTE_BIT,
                                       0, push_size, push_src);

    /* Add data sync barriers */
    ff_vk_frame_barrier(vkctx, exec, out_f, img_bar, &nb_img_bar,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_SHADER_WRITE_BIT,
                        VK_IMAGE_LAYOUT_GENERAL,
                        VK_QUEUE_FAMILY_IGNORED);
    if (in_f)
        ff_vk_frame_barrier(vkctx, exec, in_f, img_bar, &nb_img_bar,
                            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                            VK_ACCESS_SHADER_READ_BIT,
                            in_layout,
                            VK_QUEUE_FAMILY_IGNORED);

    vk->CmdPipelineBarrier2(exec->buf, &(VkDependencyInfo) {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pImageMemoryBarriers = img_bar,
            .imageMemoryBarrierCount = nb_img_bar,
        });

    vk->CmdDispatch(exec->buf,
                    FFALIGN(vkctx->output_width,  shd->lg_size[0])/shd->lg_size[0],
                    FFALIGN(vkctx->output_height, shd->lg_size[1])/shd->lg_size[1],
                    shd->lg_size[2]);

    return ff_vk_exec_submit(vkctx, exec);
fail:
    ff_vk_exec_discard_deps(vkctx, exec);
    return err;
}

int ff_vk_filter_process_2pass(FFVulkanContext *vkctx, FFVkExecPool *e,
                               FFVulkanShader *shd_list[2],
                               AVFrame *out, AVFrame *tmp, AVFrame *in,
                               VkSampler sampler, void *push_src, size_t push_size)
{
    int err = 0;
    FFVulkanFunctions *vk = &vkctx->vkfn;
    VkImageView in_views[AV_NUM_DATA_POINTERS];
    VkImageView tmp_views[AV_NUM_DATA_POINTERS];
    VkImageView out_views[AV_NUM_DATA_POINTERS];
    VkImageMemoryBarrier2 img_bar[37];
    int nb_img_bar = 0;
    VkImageLayout in_layout = sampler != VK_NULL_HANDLE ?
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL :
                              VK_IMAGE_LAYOUT_GENERAL;

    /* Update descriptors and init the exec context */
    FFVkExecContext *exec = ff_vk_exec_get(vkctx, e);
    ff_vk_exec_start(vkctx, exec);

    RET(ff_vk_exec_add_dep_frame(vkctx, exec, in,
                                 VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT));
    RET(ff_vk_exec_add_dep_frame(vkctx, exec, tmp,
                                 VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT));
    RET(ff_vk_exec_add_dep_frame(vkctx, exec, out,
                                 VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT));

    RET(ff_vk_create_imageviews(vkctx, exec, in_views,  in, FF_VK_REP_FLOAT));
    RET(ff_vk_create_imageviews(vkctx, exec, tmp_views, tmp, FF_VK_REP_FLOAT));
    RET(ff_vk_create_imageviews(vkctx, exec, out_views, out, FF_VK_REP_FLOAT));

    ff_vk_frame_barrier(vkctx, exec, in, img_bar, &nb_img_bar,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_SHADER_READ_BIT,
                        in_layout,
                        VK_QUEUE_FAMILY_IGNORED);
    ff_vk_frame_barrier(vkctx, exec, tmp, img_bar, &nb_img_bar,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                        VK_IMAGE_LAYOUT_GENERAL,
                        VK_QUEUE_FAMILY_IGNORED);
    ff_vk_frame_barrier(vkctx, exec, out, img_bar, &nb_img_bar,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_SHADER_WRITE_BIT,
                        VK_IMAGE_LAYOUT_GENERAL,
                        VK_QUEUE_FAMILY_IGNORED);

    vk->CmdPipelineBarrier2(exec->buf, &(VkDependencyInfo) {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pImageMemoryBarriers = img_bar,
            .imageMemoryBarrierCount = nb_img_bar,
        });

    for (int i = 0; i < 2; i++) {
        FFVulkanShader *shd = shd_list[i];
        AVFrame *src_f = !i ? in : tmp;
        AVFrame *dst_f = !i ? tmp : out;
        VkImageView *src_views = !i ? in_views : tmp_views;
        VkImageView *dst_views = !i ? tmp_views : out_views;

        ff_vk_shader_update_img_array(vkctx, exec, shd, src_f, src_views, 0, 0,
                                      !i ? in_layout :
                                           VK_IMAGE_LAYOUT_GENERAL,
                                      sampler);
        ff_vk_shader_update_img_array(vkctx, exec, shd, dst_f, dst_views, 0, 1,
                                      VK_IMAGE_LAYOUT_GENERAL,
                                      VK_NULL_HANDLE);

        /* Bind pipeline, update push data */
        ff_vk_exec_bind_shader(vkctx, exec, shd);
        if (push_src)
            ff_vk_shader_update_push_const(vkctx, exec, shd, VK_SHADER_STAGE_COMPUTE_BIT,
                                           0, push_size, push_src);

        vk->CmdDispatch(exec->buf,
                        FFALIGN(vkctx->output_width,  shd->lg_size[0])/shd->lg_size[0],
                        FFALIGN(vkctx->output_height, shd->lg_size[1])/shd->lg_size[1],
                        shd->lg_size[2]);
    }

    return ff_vk_exec_submit(vkctx, exec);
fail:
    ff_vk_exec_discard_deps(vkctx, exec);
    return err;
}

int ff_vk_filter_process_Nin(FFVulkanContext *vkctx, FFVkExecPool *e,
                             FFVulkanShader *shd,
                             AVFrame *out, AVFrame *in[], int nb_in,
                             VkSampler sampler, void *push_src, size_t push_size)
{
    int err = 0;
    FFVulkanFunctions *vk = &vkctx->vkfn;
    VkImageView in_views[16][AV_NUM_DATA_POINTERS];
    VkImageView out_views[AV_NUM_DATA_POINTERS];
    VkImageMemoryBarrier2 img_bar[128];
    int nb_img_bar = 0;
    VkImageLayout in_layout = sampler != VK_NULL_HANDLE ?
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL :
                              VK_IMAGE_LAYOUT_GENERAL;

    /* Update descriptors and init the exec context */
    FFVkExecContext *exec = ff_vk_exec_get(vkctx, e);
    ff_vk_exec_start(vkctx, exec);

    /* Add deps and create temporary imageviews */
    RET(ff_vk_exec_add_dep_frame(vkctx, exec, out,
                                 VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT));
    RET(ff_vk_create_imageviews(vkctx, exec, out_views, out, FF_VK_REP_FLOAT));
    for (int i = 0; i < nb_in; i++) {
        RET(ff_vk_exec_add_dep_frame(vkctx, exec, in[i],
                                     VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                     VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT));
        RET(ff_vk_create_imageviews(vkctx, exec, in_views[i], in[i], FF_VK_REP_FLOAT));
    }

    /* Update descriptor sets */
    ff_vk_shader_update_img_array(vkctx, exec, shd, out, out_views, 0, nb_in,
                                  VK_IMAGE_LAYOUT_GENERAL,
                                  VK_NULL_HANDLE);
    for (int i = 0; i < nb_in; i++)
        ff_vk_shader_update_img_array(vkctx, exec, shd, in[i], in_views[i], 0, i,
                                      in_layout,
                                      sampler);

    /* Bind pipeline, update push data */
    ff_vk_exec_bind_shader(vkctx, exec, shd);
    if (push_src)
        ff_vk_shader_update_push_const(vkctx, exec, shd, VK_SHADER_STAGE_COMPUTE_BIT,
                                       0, push_size, push_src);

    /* Add data sync barriers */
    ff_vk_frame_barrier(vkctx, exec, out, img_bar, &nb_img_bar,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_SHADER_WRITE_BIT,
                        VK_IMAGE_LAYOUT_GENERAL,
                        VK_QUEUE_FAMILY_IGNORED);
    for (int i = 0; i < nb_in; i++)
        ff_vk_frame_barrier(vkctx, exec, in[i], img_bar, &nb_img_bar,
                            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                            VK_ACCESS_SHADER_READ_BIT,
                            in_layout,
                            VK_QUEUE_FAMILY_IGNORED);

    vk->CmdPipelineBarrier2(exec->buf, &(VkDependencyInfo) {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pImageMemoryBarriers = img_bar,
            .imageMemoryBarrierCount = nb_img_bar,
        });

    vk->CmdDispatch(exec->buf,
                    FFALIGN(vkctx->output_width,  shd->lg_size[0])/shd->lg_size[0],
                    FFALIGN(vkctx->output_height, shd->lg_size[1])/shd->lg_size[1],
                    shd->lg_size[2]);

    return ff_vk_exec_submit(vkctx, exec);
fail:
    ff_vk_exec_discard_deps(vkctx, exec);
    return err;
}
