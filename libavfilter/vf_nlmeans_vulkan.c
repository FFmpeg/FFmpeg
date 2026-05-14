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

#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "vulkan_filter.h"

#include "filters.h"
#include "video.h"

extern const unsigned char ff_nlmeans_horizontal_comp_spv_data[];
extern const unsigned int  ff_nlmeans_horizontal_comp_spv_len;
extern const unsigned char ff_nlmeans_vertical_comp_spv_data[];
extern const unsigned int  ff_nlmeans_vertical_comp_spv_len;
extern const unsigned char ff_nlmeans_weights_comp_spv_data[];
extern const unsigned int  ff_nlmeans_weights_comp_spv_len;
extern const unsigned char ff_nlmeans_denoise_comp_spv_data[];
extern const unsigned int  ff_nlmeans_denoise_comp_spv_len;

/* Must be kept in sync with the definitions in the nlmeans_* shaders */
#define TYPE_ELEMS 4
#define TYPE_SIZE  (TYPE_ELEMS*4)
#define WG_SIZE 32

typedef struct NLMeansVulkanContext {
    FFVulkanContext vkctx;

    int initialized;
    FFVkExecPool e;
    AVVulkanDeviceQueueFamily *qf;

    AVBufferPool *integral_buf_pool;
    AVBufferPool *ws_buf_pool;

    FFVkBuffer xyoffsets_buf;

    FFVulkanShader shd_horizontal;
    FFVulkanShader shd_vertical;
    FFVulkanShader shd_weights;
    FFVulkanShader shd_denoise;

    int *xoffsets;
    int *yoffsets;
    int nb_offsets;
    float strength[4];
    int patch[4];

    struct nlmeans_opts {
        int r;
        double s;
        double sc[4];
        int p;
        int pc[4];
        int t;
    } opts;
} NLMeansVulkanContext;

typedef struct IntegralPushData {
    uint32_t width[4];
    uint32_t height[4];
    float    strength[4];
    uint32_t comp_off[4];
    uint32_t comp_plane[4];
    VkDeviceAddress integral_base;
    uint64_t integral_size;
    uint64_t int_stride;
    uint32_t xyoffs_start;
    uint32_t nb_components;
} IntegralPushData;

static av_cold int init_integral_pipeline(FFVulkanContext *vkctx, FFVkExecPool *exec,
                                          FFVulkanShader *shd_horizontal,
                                          FFVulkanShader *shd_vertical,
                                          int planes)
{
    int err;
    FFVulkanShader *shd;

    /* Horizontal pass */
    shd = shd_horizontal;
    ff_vk_shader_load(shd, VK_SHADER_STAGE_COMPUTE_BIT, NULL,
                      (uint32_t []) { WG_SIZE, 1, 1 }, 0);

    ff_vk_shader_add_push_const(shd, 0, sizeof(IntegralPushData),
                                VK_SHADER_STAGE_COMPUTE_BIT);

    RET(ff_vk_shader_link(vkctx, shd,
                          ff_nlmeans_horizontal_comp_spv_data,
                          ff_nlmeans_horizontal_comp_spv_len, "main"));

    RET(ff_vk_shader_register_exec(vkctx, exec, shd));

    /* Vertical pass */
    shd = shd_vertical;
    ff_vk_shader_load(shd, VK_SHADER_STAGE_COMPUTE_BIT, NULL,
                      (uint32_t []) { WG_SIZE, 1, 1 }, 0);

    ff_vk_shader_add_push_const(shd, 0, sizeof(IntegralPushData),
                                VK_SHADER_STAGE_COMPUTE_BIT);

    const FFVulkanDescriptorSetBinding desc_set_img[] = {
        { /* input_img */
            .type   = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stages = VK_SHADER_STAGE_COMPUTE_BIT,
            .elems  = planes,
        },
    };
    ff_vk_shader_add_descriptor_set(vkctx, shd, desc_set_img, 1, 0, 0);

    const FFVulkanDescriptorSetBinding desc_set_xyoffsets[] = {
        { /* xyoffsets_buffer */
            .type   = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };
    ff_vk_shader_add_descriptor_set(vkctx, shd, desc_set_xyoffsets, 1, 1, 0);

    RET(ff_vk_shader_link(vkctx, shd,
                          ff_nlmeans_vertical_comp_spv_data,
                          ff_nlmeans_vertical_comp_spv_len, "main"));

    RET(ff_vk_shader_register_exec(vkctx, exec, shd));

fail:
    return err;
}

typedef struct WeightsPushData {
    uint32_t width[4];
    uint32_t height[4];
    uint32_t ws_offset[4];
    uint32_t ws_stride[4];
    int32_t  patch_size[4];
    float    strength[4];
    uint32_t comp_off[4];
    uint32_t comp_plane[4];
    VkDeviceAddress integral_base;
    uint64_t integral_size;
    uint64_t int_stride;
    uint32_t xyoffs_start;
    uint32_t ws_count;
    uint32_t nb_components;
} WeightsPushData;

static av_cold int init_weights_pipeline(FFVulkanContext *vkctx, FFVkExecPool *exec,
                                         FFVulkanShader *shd, int planes)
{
    int err;

    ff_vk_shader_load(shd, VK_SHADER_STAGE_COMPUTE_BIT, NULL,
                      (uint32_t []) { WG_SIZE, WG_SIZE, 1 }, 0);

    ff_vk_shader_add_push_const(shd, 0, sizeof(WeightsPushData),
                                VK_SHADER_STAGE_COMPUTE_BIT);

    const FFVulkanDescriptorSetBinding desc_set[] = {
        { /* input_img */
            .type   = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stages = VK_SHADER_STAGE_COMPUTE_BIT,
            .elems  = planes,
        },
        { /* weights_buffer */
            .type   = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        { /* sums_buffer */
            .type   = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };
    ff_vk_shader_add_descriptor_set(vkctx, shd, desc_set, 3, 0, 0);

    const FFVulkanDescriptorSetBinding desc_set_xyoffsets[] = {
        { /* xyoffsets_buffer */
            .type   = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };
    ff_vk_shader_add_descriptor_set(vkctx, shd, desc_set_xyoffsets, 1, 1, 0);

    RET(ff_vk_shader_link(vkctx, shd,
                          ff_nlmeans_weights_comp_spv_data,
                          ff_nlmeans_weights_comp_spv_len, "main"));

    RET(ff_vk_shader_register_exec(vkctx, exec, shd));

fail:
    return err;
}

typedef struct DenoisePushData {
    uint32_t comp_off[4];
    uint32_t comp_plane[4];
    uint32_t ws_offset[4];
    uint32_t ws_stride[4];
    uint32_t ws_count;
    uint32_t t;
    uint32_t nb_components;
} DenoisePushData;

static av_cold int init_denoise_pipeline(FFVulkanContext *vkctx, FFVkExecPool *exec,
                                         FFVulkanShader *shd, int planes)
{
    int err;

    ff_vk_shader_load(shd, VK_SHADER_STAGE_COMPUTE_BIT, NULL,
                      (uint32_t []) { WG_SIZE, WG_SIZE, 1 }, 0);

    ff_vk_shader_add_push_const(shd, 0, sizeof(DenoisePushData),
                                VK_SHADER_STAGE_COMPUTE_BIT);

    const FFVulkanDescriptorSetBinding desc_set_img[] = {
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
    ff_vk_shader_add_descriptor_set(vkctx, shd, desc_set_img, 2, 0, 0);

    const FFVulkanDescriptorSetBinding desc_set_ws[] = {
        { /* weights_buffer */
            .type   = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        { /* sums_buffer */
            .type   = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };
    ff_vk_shader_add_descriptor_set(vkctx, shd, desc_set_ws, 2, 0, 0);

    RET(ff_vk_shader_link(vkctx, shd,
                          ff_nlmeans_denoise_comp_spv_data,
                          ff_nlmeans_denoise_comp_spv_len, "main"));

    RET(ff_vk_shader_register_exec(vkctx, exec, shd));

fail:
    return err;
}

static av_cold int init_filter(AVFilterContext *ctx)
{
    int rad, err;
    int xcnt = 0, ycnt = 0;
    NLMeansVulkanContext *s = ctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;
    const int planes = av_pix_fmt_count_planes(s->vkctx.output_format);
    int *offsets_buf;
    int offsets_dispatched = 0, nb_dispatches = 0;

    if (!(s->opts.r & 1)) {
        s->opts.r |= 1;
        av_log(ctx, AV_LOG_WARNING, "Research size should be odd, setting to %i",
               s->opts.r);
    }

    if (!(s->opts.p & 1)) {
        s->opts.p |= 1;
        av_log(ctx, AV_LOG_WARNING, "Patch size should be odd, setting to %i",
               s->opts.p);
    }

    for (int i = 0; i < 4; i++) {
        double str = !isnan(s->opts.sc[i]) ? s->opts.sc[i] : s->opts.s;
        int ps = (s->opts.pc[i] ? s->opts.pc[i] : s->opts.p);
        if (str == 0.0) {
            s->strength[i] = 0.0;
        } else {
            str  = 10.0f*str;
            str *= -str;
            str  = 255.0*255.0 / str;
            s->strength[i] = str;
        }
        if (!(ps & 1)) {
            ps |= 1;
            av_log(ctx, AV_LOG_WARNING, "Patch size should be odd, setting to %i",
                   ps);
        }
        s->patch[i] = ps / 2;
    }

    rad = s->opts.r/2;
    s->nb_offsets = (2*rad + 1)*(2*rad + 1) - 1;
    s->xoffsets = av_malloc(s->nb_offsets*sizeof(*s->xoffsets));
    s->yoffsets = av_malloc(s->nb_offsets*sizeof(*s->yoffsets));
    s->nb_offsets = 0;

    for (int x = -rad; x <= rad; x++) {
        for (int y = -rad; y <= rad; y++) {
            if (!x && !y)
                continue;

            s->xoffsets[xcnt++] = x;
            s->yoffsets[ycnt++] = y;
            s->nb_offsets++;
        }
    }

    RET(ff_vk_create_buf(&s->vkctx, &s->xyoffsets_buf, 2*s->nb_offsets*sizeof(int32_t), NULL, NULL,
                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT));
    RET(ff_vk_map_buffer(&s->vkctx, &s->xyoffsets_buf, (uint8_t **)&offsets_buf, 0));

    for (int i = 0; i < 2*s->nb_offsets; i += 2) {
        offsets_buf[i + 0] = s->xoffsets[i >> 1];
        offsets_buf[i + 1] = s->yoffsets[i >> 1];
    }

    RET(ff_vk_unmap_buffer(&s->vkctx, &s->xyoffsets_buf, 1));

    s->opts.t = FFMIN(s->opts.t, (FFALIGN(s->nb_offsets, TYPE_ELEMS) / TYPE_ELEMS));

    s->qf = ff_vk_qf_find(vkctx, VK_QUEUE_COMPUTE_BIT, 0);
    if (!s->qf) {
        av_log(ctx, AV_LOG_ERROR, "Device has no compute queues\n");
        err = AVERROR(ENOTSUP);
        goto fail;
    }

    RET(ff_vk_exec_pool_init(vkctx, s->qf, &s->e, 1, 0, 0, 0, NULL));

    RET(init_integral_pipeline(vkctx, &s->e, &s->shd_horizontal, &s->shd_vertical,
                               planes));

    RET(init_weights_pipeline(vkctx, &s->e, &s->shd_weights, planes));

    RET(init_denoise_pipeline(vkctx, &s->e, &s->shd_denoise, planes));

    RET(ff_vk_shader_update_desc_buffer(vkctx, &s->e.contexts[0], &s->shd_vertical,
                                        1, 0, 0,
                                        &s->xyoffsets_buf, 0, s->xyoffsets_buf.size,
                                        VK_FORMAT_UNDEFINED));

    RET(ff_vk_shader_update_desc_buffer(vkctx, &s->e.contexts[0], &s->shd_weights,
                                        1, 0, 0,
                                        &s->xyoffsets_buf, 0, s->xyoffsets_buf.size,
                                        VK_FORMAT_UNDEFINED));

    do {
        int wg_invoc = FFMIN((s->nb_offsets - offsets_dispatched)/TYPE_ELEMS, s->opts.t);
        offsets_dispatched += wg_invoc * TYPE_ELEMS;
        nb_dispatches++;
    } while (offsets_dispatched < s->nb_offsets);

    av_log(ctx, AV_LOG_VERBOSE, "Filter initialized, %i x/y offsets, %i dispatches\n",
           s->nb_offsets, nb_dispatches);

    s->initialized = 1;

fail:
    return err;
}

static int denoise_pass(NLMeansVulkanContext *s, FFVkExecContext *exec,
                        FFVkBuffer *ws_vk, uint32_t comp_offs[4], uint32_t comp_planes[4],
                        uint32_t ws_offset[4], uint32_t ws_stride[4],
                        uint32_t ws_count, uint32_t t, uint32_t nb_components)
{
    FFVulkanContext *vkctx = &s->vkctx;
    FFVulkanFunctions *vk = &vkctx->vkfn;

    DenoisePushData pd = {
        { comp_offs[0], comp_offs[1], comp_offs[2], comp_offs[3] },
        { comp_planes[0], comp_planes[1], comp_planes[2], comp_planes[3] },
        { ws_offset[0], ws_offset[1], ws_offset[2], ws_offset[3] },
        { ws_stride[0], ws_stride[1], ws_stride[2], ws_stride[3] },
        ws_count,
        t,
        nb_components,
    };

    /* Denoise pass pipeline */
    ff_vk_exec_bind_shader(vkctx, exec, &s->shd_denoise);

    /* Push data */
    ff_vk_shader_update_push_const(vkctx, exec, &s->shd_denoise,
                                   VK_SHADER_STAGE_COMPUTE_BIT,
                                   0, sizeof(pd), &pd);

    VkBufferMemoryBarrier2 buf_bar;
    ff_vk_buf_barrier(buf_bar, ws_vk,
                      COMPUTE_SHADER_BIT, SHADER_STORAGE_READ_BIT,
                                          SHADER_STORAGE_WRITE_BIT,
                      COMPUTE_SHADER_BIT, SHADER_STORAGE_READ_BIT, NONE_KHR,
                      0, VK_WHOLE_SIZE);
    vk->CmdPipelineBarrier2(exec->buf, &(VkDependencyInfo) {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pBufferMemoryBarriers = &buf_bar,
            .bufferMemoryBarrierCount = 1,
        });

    /* End of denoise pass */
    vk->CmdDispatch(exec->buf,
                    FFALIGN(vkctx->output_width,  s->shd_denoise.lg_size[0])/s->shd_denoise.lg_size[0],
                    FFALIGN(vkctx->output_height, s->shd_denoise.lg_size[1])/s->shd_denoise.lg_size[1],
                    av_pix_fmt_count_planes(s->vkctx.output_format));

    return 0;
}

static int nlmeans_vulkan_filter_frame(AVFilterLink *link, AVFrame *in)
{
    int err;
    AVFrame *out = NULL;
    AVFilterContext *ctx = link->dst;
    NLMeansVulkanContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    FFVulkanContext *vkctx = &s->vkctx;
    FFVulkanFunctions *vk = &vkctx->vkfn;

    const AVPixFmtDescriptor *desc;
    int comp_offs[4];
    int comp_planes[4];
    int plane_widths[4];
    int plane_heights[4];

    int offsets_dispatched = 0;

    /* Integral */
    AVBufferRef *integral_buf = NULL;
    FFVkBuffer *integral_vk;
    size_t int_stride;
    size_t int_size;

    /* Weights/sums */
    AVBufferRef *ws_buf = NULL;
    FFVkBuffer *ws_vk;
    uint32_t ws_count = 0;
    uint32_t ws_offset[4];
    uint32_t ws_stride[4];
    size_t ws_size;

    FFVkExecContext *exec;
    VkImageView in_views[AV_NUM_DATA_POINTERS];
    VkImageView out_views[AV_NUM_DATA_POINTERS];
    VkImageMemoryBarrier2 img_bar[8];
    int nb_img_bar = 0;
    VkBufferMemoryBarrier2 buf_bar[2];
    int nb_buf_bar = 0;

    if (!s->initialized)
        RET(init_filter(ctx));

    desc = av_pix_fmt_desc_get(vkctx->output_format);
    if (!desc)
        return AVERROR(EINVAL);

    /* Integral image */
    int_stride = FFALIGN(vkctx->output_width, s->shd_vertical.lg_size[0]) * TYPE_SIZE;
    int_size = FFALIGN(vkctx->output_height, s->shd_horizontal.lg_size[0]) * int_stride;

    /* Plane dimensions */
    for (int i = 0; i < desc->nb_components; i++) {
        plane_widths[i] = !i || (i == 3) ? vkctx->output_width : AV_CEIL_RSHIFT(vkctx->output_width, desc->log2_chroma_w);
        plane_heights[i] = !i || (i == 3) ? vkctx->output_height : AV_CEIL_RSHIFT(vkctx->output_height, desc->log2_chroma_h);
        plane_widths[i]  = FFALIGN(plane_widths[i],  s->shd_denoise.lg_size[0]);
        plane_heights[i] = FFALIGN(plane_heights[i], s->shd_denoise.lg_size[1]);

        comp_offs[i] = desc->comp[i].offset / (FFALIGN(desc->comp[i].depth, 8)/8);
        comp_planes[i] = desc->comp[i].plane;

        ws_stride[i] = plane_widths[i];
        ws_offset[i] = ws_count;
        ws_count += ws_stride[i] * plane_heights[i];
    }

    ws_size = ws_count * sizeof(float);

    /* Buffers */
    err = ff_vk_get_pooled_buffer(&s->vkctx, &s->integral_buf_pool, &integral_buf,
                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                  VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                  NULL,
                                  int_size * s->opts.t * desc->nb_components,
                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (err < 0)
        return err;
    integral_vk = (FFVkBuffer *)integral_buf->data;

    err = ff_vk_get_pooled_buffer(&s->vkctx, &s->ws_buf_pool, &ws_buf,
                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                  VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                  NULL,
                                  ws_size * s-> opts.t * 2,
                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (err < 0)
        return err;
    ws_vk = (FFVkBuffer *)ws_buf->data;

    /* Output frame */
    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    /* Execution context */
    exec = ff_vk_exec_get(&s->vkctx, &s->e);
    ff_vk_exec_start(vkctx, exec);

    /* Dependencies */
    RET(ff_vk_exec_add_dep_frame(vkctx, exec, in,
                                 VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT));
    RET(ff_vk_exec_add_dep_frame(vkctx, exec, out,
                                 VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT));

    RET(ff_vk_exec_add_dep_buf(vkctx, exec, &integral_buf, 1, 0));
    integral_buf = NULL;

    RET(ff_vk_exec_add_dep_buf(vkctx, exec, &ws_buf,       1, 0));
    ws_buf = NULL;

    /* Input frame prep */
    RET(ff_vk_create_imageviews(vkctx, exec, in_views, in, FF_VK_REP_FLOAT));
    ff_vk_frame_barrier(vkctx, exec, in, img_bar, &nb_img_bar,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_SHADER_READ_BIT,
                        VK_IMAGE_LAYOUT_GENERAL,
                        VK_QUEUE_FAMILY_IGNORED);

    /* Output frame prep */
    RET(ff_vk_create_imageviews(vkctx, exec, out_views, out, FF_VK_REP_FLOAT));
    ff_vk_frame_barrier(vkctx, exec, out, img_bar, &nb_img_bar,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_SHADER_WRITE_BIT,
                        VK_IMAGE_LAYOUT_GENERAL,
                        VK_QUEUE_FAMILY_IGNORED);

    ff_vk_buf_barrier(buf_bar[nb_buf_bar++], ws_vk,
                      ALL_COMMANDS_BIT, NONE_KHR, NONE_KHR,
                      TRANSFER_BIT,     TRANSFER_WRITE_BIT, NONE_KHR,
                      0, VK_WHOLE_SIZE);
    ff_vk_buf_barrier(buf_bar[nb_buf_bar++], integral_vk,
                      ALL_COMMANDS_BIT,   NONE_KHR, NONE_KHR,
                      COMPUTE_SHADER_BIT, SHADER_STORAGE_READ_BIT, NONE_KHR,
                      0, VK_WHOLE_SIZE);
    vk->CmdPipelineBarrier2(exec->buf, &(VkDependencyInfo) {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pImageMemoryBarriers = img_bar,
            .imageMemoryBarrierCount = nb_img_bar,
            .pBufferMemoryBarriers = buf_bar,
            .bufferMemoryBarrierCount = nb_buf_bar,
        });
    nb_buf_bar = 0;
    nb_img_bar = 0;

    /* Buffer zeroing */
    vk->CmdFillBuffer(exec->buf, ws_vk->buf, 0, ws_vk->size, 0x0);

    /* Update integral descriptors */
    ff_vk_shader_update_img_array(vkctx, exec, &s->shd_vertical, in, in_views, 0, 0,
                                  VK_IMAGE_LAYOUT_GENERAL, VK_NULL_HANDLE);
    /* Update weights descriptors */
    ff_vk_shader_update_img_array(vkctx, exec, &s->shd_weights, in, in_views, 0, 0,
                                  VK_IMAGE_LAYOUT_GENERAL, VK_NULL_HANDLE);
    RET(ff_vk_shader_update_desc_buffer(&s->vkctx, exec, &s->shd_weights, 0, 1, 0,
                                        ws_vk, 0, ws_size * s-> opts.t,
                                        VK_FORMAT_UNDEFINED));
    RET(ff_vk_shader_update_desc_buffer(&s->vkctx, exec, &s->shd_weights, 0, 2, 0,
                                        ws_vk, ws_size * s-> opts.t, ws_size * s-> opts.t,
                                        VK_FORMAT_UNDEFINED));

    /* Update denoise descriptors */
    ff_vk_shader_update_img_array(vkctx, exec, &s->shd_denoise, in, in_views, 0, 0,
                                  VK_IMAGE_LAYOUT_GENERAL, VK_NULL_HANDLE);
    ff_vk_shader_update_img_array(vkctx, exec, &s->shd_denoise, out, out_views, 0, 1,
                                  VK_IMAGE_LAYOUT_GENERAL, VK_NULL_HANDLE);
    RET(ff_vk_shader_update_desc_buffer(&s->vkctx, exec, &s->shd_denoise, 1, 0, 0,
                                        ws_vk, 0, ws_size * s-> opts.t,
                                        VK_FORMAT_UNDEFINED));
    RET(ff_vk_shader_update_desc_buffer(&s->vkctx, exec, &s->shd_denoise, 1, 1, 0,
                                        ws_vk, ws_size * s-> opts.t, ws_size * s-> opts.t,
                                        VK_FORMAT_UNDEFINED));

    VkPipelineStageFlagBits2 ws_stage = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    VkAccessFlagBits2 ws_access = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    do {
        int wg_invoc = FFMIN((s->nb_offsets - offsets_dispatched)/TYPE_ELEMS, s->opts.t);
        IntegralPushData pd = {
            { plane_widths[0], plane_widths[1], plane_widths[2], plane_widths[3] },
            { plane_heights[0], plane_heights[1], plane_heights[2], plane_heights[3] },
            { s->strength[0], s->strength[1], s->strength[2], s->strength[3], },
            { comp_offs[0], comp_offs[1], comp_offs[2], comp_offs[3] },
            { comp_planes[0], comp_planes[1], comp_planes[2], comp_planes[3] },
            integral_vk->address,
            (uint64_t)int_size,
            (uint64_t)int_stride,
            offsets_dispatched,
            desc->nb_components,
        };

        /* Vertical pass */
        ff_vk_buf_barrier(buf_bar[nb_buf_bar++], integral_vk,
                          COMPUTE_SHADER_BIT, SHADER_STORAGE_READ_BIT, NONE_KHR,
                          COMPUTE_SHADER_BIT, SHADER_STORAGE_WRITE_BIT, NONE_KHR,
                          0, VK_WHOLE_SIZE);
        vk->CmdPipelineBarrier2(exec->buf, &(VkDependencyInfo) {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pBufferMemoryBarriers = buf_bar,
            .bufferMemoryBarrierCount = nb_buf_bar,
        });
        nb_buf_bar = 0;

        ff_vk_exec_bind_shader(vkctx, exec, &s->shd_vertical);
        ff_vk_shader_update_push_const(vkctx, exec, &s->shd_vertical,
                                       VK_SHADER_STAGE_COMPUTE_BIT,
                                       0, sizeof(pd), &pd);
        vk->CmdDispatch(exec->buf,
                        FFALIGN(vkctx->output_width, s->shd_vertical.lg_size[0]) /
                            s->shd_vertical.lg_size[0],
                        desc->nb_components,
                        wg_invoc);

        /* Horizontal pass */
        ff_vk_buf_barrier(buf_bar[nb_buf_bar++], integral_vk,
                          COMPUTE_SHADER_BIT, SHADER_STORAGE_WRITE_BIT, NONE_KHR,
                          COMPUTE_SHADER_BIT, SHADER_STORAGE_READ_BIT,
                                              SHADER_STORAGE_WRITE_BIT,
                          0, VK_WHOLE_SIZE);
        vk->CmdPipelineBarrier2(exec->buf, &(VkDependencyInfo) {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pBufferMemoryBarriers = buf_bar,
            .bufferMemoryBarrierCount = nb_buf_bar,
        });
        nb_buf_bar = 0;

        ff_vk_exec_bind_shader(vkctx, exec, &s->shd_horizontal);
        ff_vk_shader_update_push_const(vkctx, exec, &s->shd_horizontal,
                                       VK_SHADER_STAGE_COMPUTE_BIT,
                                       0, sizeof(pd), &pd);
        vk->CmdDispatch(exec->buf,
                        FFALIGN(vkctx->output_height, s->shd_horizontal.lg_size[0]) /
                            s->shd_horizontal.lg_size[0],
                        desc->nb_components,
                        wg_invoc);

        /* Weights pass */
        ff_vk_buf_barrier(buf_bar[nb_buf_bar++], integral_vk,
                          COMPUTE_SHADER_BIT, SHADER_STORAGE_READ_BIT,
                                              SHADER_STORAGE_WRITE_BIT,
                          COMPUTE_SHADER_BIT, SHADER_STORAGE_READ_BIT, NONE_KHR,
                          0, VK_WHOLE_SIZE);
        buf_bar[nb_buf_bar++] = (VkBufferMemoryBarrier2) {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = ws_stage,
            .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .srcAccessMask = ws_access,
            .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = ws_vk->buf,
            .size = ws_vk->size,
            .offset = 0,
        };
        vk->CmdPipelineBarrier2(exec->buf, &(VkDependencyInfo) {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pBufferMemoryBarriers = buf_bar,
            .bufferMemoryBarrierCount = nb_buf_bar,
        });
        nb_buf_bar = 0;
        ws_stage = buf_bar[1].dstStageMask;
        ws_access = buf_bar[1].dstAccessMask;

        WeightsPushData wpd = {
            { plane_widths[0], plane_widths[1], plane_widths[2], plane_widths[3] },
            { plane_heights[0], plane_heights[1], plane_heights[2], plane_heights[3] },
            { ws_offset[0], ws_offset[1], ws_offset[2], ws_offset[3] },
            { ws_stride[0], ws_stride[1], ws_stride[2], ws_stride[3] },
            { s->patch[0], s->patch[1], s->patch[2], s->patch[3] },
            { s->strength[0], s->strength[1], s->strength[2], s->strength[3], },
            { comp_offs[0], comp_offs[1], comp_offs[2], comp_offs[3] },
            { comp_planes[0], comp_planes[1], comp_planes[2], comp_planes[3] },
            integral_vk->address,
            (uint64_t)int_size,
            (uint64_t)int_stride,
            offsets_dispatched,
            ws_count,
            desc->nb_components,
        };
        ff_vk_exec_bind_shader(vkctx, exec, &s->shd_weights);
        ff_vk_shader_update_push_const(vkctx, exec, &s->shd_weights,
                                        VK_SHADER_STAGE_COMPUTE_BIT,
                                        0, sizeof(wpd), &wpd);
        vk->CmdDispatch(exec->buf,
                        FFALIGN(vkctx->output_width, s->shd_weights.lg_size[0]) /
                            s->shd_weights.lg_size[0],
                        FFALIGN(vkctx->output_height, s->shd_weights.lg_size[1]) /
                            s->shd_weights.lg_size[1],
                        wg_invoc * desc->nb_components);

        offsets_dispatched += wg_invoc * TYPE_ELEMS;
    } while (offsets_dispatched < s->nb_offsets);

    RET(denoise_pass(s, exec, ws_vk, comp_offs, comp_planes, ws_offset, ws_stride,
                     ws_count, s->opts.t, desc->nb_components));

    err = ff_vk_exec_submit(vkctx, exec);
    if (err < 0)
        return err;

    err = av_frame_copy_props(out, in);
    if (err < 0)
        goto fail;

    av_frame_free(&in);

    return ff_filter_frame(outlink, out);

fail:
    av_buffer_unref(&integral_buf);
    av_buffer_unref(&ws_buf);
    av_frame_free(&in);
    av_frame_free(&out);
    return err;
}

static void nlmeans_vulkan_uninit(AVFilterContext *avctx)
{
    NLMeansVulkanContext *s = avctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;

    ff_vk_exec_pool_free(vkctx, &s->e);
    ff_vk_shader_free(vkctx, &s->shd_horizontal);
    ff_vk_shader_free(vkctx, &s->shd_vertical);
    ff_vk_shader_free(vkctx, &s->shd_weights);
    ff_vk_shader_free(vkctx, &s->shd_denoise);

    av_buffer_pool_uninit(&s->integral_buf_pool);
    av_buffer_pool_uninit(&s->ws_buf_pool);

    ff_vk_uninit(&s->vkctx);

    av_freep(&s->xoffsets);
    av_freep(&s->yoffsets);

    s->initialized = 0;
}

#define OFFSET(x) offsetof(NLMeansVulkanContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)
static const AVOption nlmeans_vulkan_options[] = {
    { "s",  "denoising strength for all components", OFFSET(opts.s), AV_OPT_TYPE_DOUBLE, { .dbl = 1.0 }, 0.0, 100.0, FLAGS },
    { "p",  "patch size for all components", OFFSET(opts.p), AV_OPT_TYPE_INT, { .i64 = 3*2+1 }, 0, 99, FLAGS },
    { "r",  "research window size", OFFSET(opts.r), AV_OPT_TYPE_INT, { .i64 = 7*2+1 }, 0, 99, FLAGS },
    { "t",  "parallelism", OFFSET(opts.t), AV_OPT_TYPE_INT, { .i64 = 8 }, 1, 64, FLAGS },

    { "s1", "denoising strength for component 1", OFFSET(opts.sc[0]), AV_OPT_TYPE_DOUBLE, { .dbl = NAN }, 0.0, 100.0, FLAGS },
    { "s2", "denoising strength for component 2", OFFSET(opts.sc[1]), AV_OPT_TYPE_DOUBLE, { .dbl = NAN }, 0.0, 100.0, FLAGS },
    { "s3", "denoising strength for component 3", OFFSET(opts.sc[2]), AV_OPT_TYPE_DOUBLE, { .dbl = NAN }, 0.0, 100.0, FLAGS },
    { "s4", "denoising strength for component 4", OFFSET(opts.sc[3]), AV_OPT_TYPE_DOUBLE, { .dbl = NAN }, 0.0, 100.0, FLAGS },

    { "p1", "patch size for component 1", OFFSET(opts.pc[0]), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 99, FLAGS },
    { "p2", "patch size for component 2", OFFSET(opts.pc[1]), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 99, FLAGS },
    { "p3", "patch size for component 3", OFFSET(opts.pc[2]), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 99, FLAGS },
    { "p4", "patch size for component 4", OFFSET(opts.pc[3]), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 99, FLAGS },

    { NULL }
};

AVFILTER_DEFINE_CLASS(nlmeans_vulkan);

static const AVFilterPad nlmeans_vulkan_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = &nlmeans_vulkan_filter_frame,
        .config_props = &ff_vk_filter_config_input,
    },
};

static const AVFilterPad nlmeans_vulkan_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = &ff_vk_filter_config_output,
    },
};

const FFFilter ff_vf_nlmeans_vulkan = {
    .p.name         = "nlmeans_vulkan",
    .p.description  = NULL_IF_CONFIG_SMALL("Non-local means denoiser (Vulkan)"),
    .p.priv_class   = &nlmeans_vulkan_class,
    .p.flags        = AVFILTER_FLAG_HWDEVICE,
    .priv_size      = sizeof(NLMeansVulkanContext),
    .init           = &ff_vk_filter_init,
    .uninit         = &nlmeans_vulkan_uninit,
    FILTER_INPUTS(nlmeans_vulkan_inputs),
    FILTER_OUTPUTS(nlmeans_vulkan_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_VULKAN),
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
