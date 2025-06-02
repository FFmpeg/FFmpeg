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

#include <float.h>
#include "libavutil/vulkan_spirv.h"
#include "libavutil/opt.h"
#include "libavutil/timestamp.h"
#include "vulkan_filter.h"

#include "filters.h"
#include "video.h"

typedef struct BlackDetectVulkanContext {
    FFVulkanContext vkctx;

    int initialized;
    FFVkExecPool e;
    AVVulkanDeviceQueueFamily *qf;
    FFVulkanShader shd;
    AVBufferPool *sum_buf_pool;

    double black_min_duration_time;
    double picture_black_ratio_th;
    double pixel_black_th;
    int    alpha;

    int64_t black_start;
} BlackDetectVulkanContext;

typedef struct BlackDetectPushData {
    float threshold;
} BlackDetectPushData;

typedef struct BlackDetectBuf {
#define SLICES 16
    uint32_t slice_sum[SLICES];
} BlackDetectBuf;

static av_cold int init_filter(AVFilterContext *ctx)
{
    int err;
    uint8_t *spv_data;
    size_t spv_len;
    void *spv_opaque = NULL;
    BlackDetectVulkanContext *s = ctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;
    FFVulkanShader *shd;
    FFVkSPIRVCompiler *spv;
    FFVulkanDescriptorSetBinding *desc;
    const int plane = s->alpha ? 3 : 0;

    const AVPixFmtDescriptor *pixdesc = av_pix_fmt_desc_get(s->vkctx.input_format);
    if (pixdesc->flags & AV_PIX_FMT_FLAG_RGB) {
        av_log(ctx, AV_LOG_ERROR, "RGB inputs are not supported\n");
        return AVERROR(ENOTSUP);
    }

    spv = ff_vk_spirv_init();
    if (!spv) {
        av_log(ctx, AV_LOG_ERROR, "Unable to initialize SPIR-V compiler!\n");
        return AVERROR_EXTERNAL;
    }

    s->qf = ff_vk_qf_find(vkctx, VK_QUEUE_COMPUTE_BIT, 0);
    if (!s->qf) {
        av_log(ctx, AV_LOG_ERROR, "Device has no compute queues\n");
        err = AVERROR(ENOTSUP);
        goto fail;
    }

    RET(ff_vk_exec_pool_init(vkctx, s->qf, &s->e, s->qf->num*4, 0, 0, 0, NULL));
    RET(ff_vk_shader_init(vkctx, &s->shd, "blackdetect",
                          VK_SHADER_STAGE_COMPUTE_BIT,
                          (const char *[]) { "GL_KHR_shader_subgroup_ballot" }, 1,
                          32, 32, 1,
                          0));
    shd = &s->shd;

    GLSLC(0, layout(push_constant, std430) uniform pushConstants {            );
    GLSLC(1,     float threshold;                                             );
    GLSLC(0, };                                                               );

    ff_vk_shader_add_push_const(shd, 0, sizeof(BlackDetectPushData),
                                VK_SHADER_STAGE_COMPUTE_BIT);

    desc = (FFVulkanDescriptorSetBinding []) {
        {
            .name       = "input_img",
            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .mem_layout = ff_vk_shader_rep_fmt(s->vkctx.input_format, FF_VK_REP_FLOAT),
            .mem_quali  = "readonly",
            .dimensions = 2,
            .elems      = av_pix_fmt_count_planes(s->vkctx.input_format),
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
        }, {
            .name        = "sum_buffer",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .buf_content = "uint slice_sum[];",
        }
    };

    RET(ff_vk_shader_add_descriptor_set(vkctx, &s->shd, desc, 2, 0, 0));

    GLSLC(0, shared uint wg_sum;                                              );
    GLSLC(0,                                                                  );
    GLSLC(0, void main()                                                      );
    GLSLC(0, {                                                                );
    GLSLC(1,     wg_sum = 0u;                                                 );
    GLSLC(1,     barrier();                                                   );
    GLSLC(0,                                                                  );
    GLSLC(1,     const ivec2 pos = ivec2(gl_GlobalInvocationID.xy);           );
    GLSLF(1,     if (!IS_WITHIN(pos, imageSize(input_img[%d])))               ,plane);
    GLSLC(2,         return;                                                  );
    GLSLF(1,     float value = imageLoad(input_img[%d], pos).x;               ,plane);
    GLSLC(1,     uvec4 isblack = subgroupBallot(value <= threshold);          );
    GLSLC(1,     if (subgroupElect())                                         );
    GLSLC(2,         atomicAdd(wg_sum, subgroupBallotBitCount(isblack));      );
    GLSLC(1,     barrier();                                                   );
    GLSLC(1,     if (gl_LocalInvocationIndex == 0u)                           );
    GLSLF(2,         atomicAdd(slice_sum[gl_WorkGroupID.x %% %du], wg_sum);   ,SLICES);
    GLSLC(0, }                                                                );

    RET(spv->compile_shader(vkctx, spv, &s->shd, &spv_data, &spv_len, "main",
                            &spv_opaque));
    RET(ff_vk_shader_link(vkctx, &s->shd, spv_data, spv_len, "main"));

    RET(ff_vk_shader_register_exec(vkctx, &s->e, &s->shd));

    s->black_start = AV_NOPTS_VALUE;
    s->initialized = 1;

fail:
    if (spv_opaque)
        spv->free_shader(spv, &spv_opaque);
    if (spv)
        spv->uninit(&spv);

    return err;
}

static void report_black_region(AVFilterContext *ctx, int64_t black_end)
{
    BlackDetectVulkanContext *s = ctx->priv;
    const AVFilterLink *inlink = ctx->inputs[0];
    if (s->black_start == AV_NOPTS_VALUE)
        return;

    if ((black_end - s->black_start) >= s->black_min_duration_time / av_q2d(inlink->time_base)) {
        av_log(s, AV_LOG_INFO,
               "black_start:%s black_end:%s black_duration:%s\n",
               av_ts2timestr(s->black_start, &inlink->time_base),
               av_ts2timestr(black_end, &inlink->time_base),
               av_ts2timestr(black_end - s->black_start, &inlink->time_base));
    }
}

static void evaluate(AVFilterLink *link, AVFrame *in,
                     const BlackDetectBuf *sum)
{
    AVFilterContext *ctx = link->dst;
    BlackDetectVulkanContext *s = ctx->priv;
    FilterLink *inl = ff_filter_link(link);
    uint64_t nb_black_pixels = 0;
    double ratio;

    for (int i = 0; i < FF_ARRAY_ELEMS(sum->slice_sum); i++)
        nb_black_pixels += sum->slice_sum[i];

    ratio = (double) nb_black_pixels / (link->w * link->h);

    av_log(ctx, AV_LOG_DEBUG,
           "frame:%"PRId64" picture_black_ratio:%f pts:%s t:%s type:%c\n",
           inl->frame_count_out, ratio,
           av_ts2str(in->pts), av_ts2timestr(in->pts, &in->time_base),
           av_get_picture_type_char(in->pict_type));

    if (ratio >= s->picture_black_ratio_th) {
        if (s->black_start == AV_NOPTS_VALUE) {
            s->black_start = in->pts;
            av_dict_set(&in->metadata, "lavfi.black_start",
                av_ts2timestr(in->pts, &in->time_base), 0);
        }
    } else if (s->black_start != AV_NOPTS_VALUE) {
        report_black_region(ctx, in->pts);
        av_dict_set(&in->metadata, "lavfi.black_end",
            av_ts2timestr(in->pts, &in->time_base), 0);
        s->black_start = AV_NOPTS_VALUE;
    }
}

static int blackdetect_vulkan_filter_frame(AVFilterLink *link, AVFrame *in)
{
    int err;
    AVFilterContext *ctx = link->dst;
    BlackDetectVulkanContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];

    VkImageView in_views[AV_NUM_DATA_POINTERS];
    VkImageMemoryBarrier2 img_bar[4];
    int nb_img_bar = 0;

    FFVulkanContext *vkctx = &s->vkctx;
    FFVulkanFunctions *vk = &vkctx->vkfn;
    FFVkExecContext *exec = NULL;
    AVBufferRef *sum_buf = NULL;
    FFVkBuffer *sum_vk;

    BlackDetectBuf *sum;
    BlackDetectPushData push_data;

    if (in->color_range == AVCOL_RANGE_JPEG || s->alpha) {
        push_data.threshold = s->pixel_black_th;
    } else {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(vkctx->input_format);
        const int depth = desc->comp[0].depth;
        const int ymin = 16  << (depth - 8);
        const int ymax = 235 << (depth - 8);
        const int imax = (1 << depth) - 1;
        push_data.threshold = (s->pixel_black_th * (ymax - ymin) + ymin) / imax;
    }

    if (!s->initialized)
        RET(init_filter(ctx));

    err = ff_vk_get_pooled_buffer(vkctx, &s->sum_buf_pool, &sum_buf,
                                  VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                  NULL,
                                  sizeof(BlackDetectBuf),
                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (err < 0)
        return err;
    sum_vk = (FFVkBuffer *)sum_buf->data;
    sum = (BlackDetectBuf *) sum_vk->mapped_mem;

    exec = ff_vk_exec_get(vkctx, &s->e);
    ff_vk_exec_start(vkctx, exec);

    RET(ff_vk_exec_add_dep_frame(vkctx, exec, in,
                                 VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT));
    RET(ff_vk_create_imageviews(vkctx, exec, in_views, in, FF_VK_REP_FLOAT));

    ff_vk_shader_update_img_array(vkctx, exec, &s->shd, in, in_views, 0, 0,
                                  VK_IMAGE_LAYOUT_GENERAL, VK_NULL_HANDLE);

    ff_vk_frame_barrier(vkctx, exec, in, img_bar, &nb_img_bar,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_SHADER_READ_BIT,
                        VK_IMAGE_LAYOUT_GENERAL,
                        VK_QUEUE_FAMILY_IGNORED);

    /* zero sum buffer */
    vk->CmdPipelineBarrier2(exec->buf, &(VkDependencyInfo) {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pBufferMemoryBarriers = &(VkBufferMemoryBarrier2) {
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
                .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = sum_vk->buf,
                .size = sum_vk->size,
                .offset = 0,
            },
            .bufferMemoryBarrierCount = 1,
        });

    vk->CmdFillBuffer(exec->buf, sum_vk->buf, 0, sum_vk->size, 0x0);

    vk->CmdPipelineBarrier2(exec->buf, &(VkDependencyInfo) {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pImageMemoryBarriers = img_bar,
            .imageMemoryBarrierCount = nb_img_bar,
            .pBufferMemoryBarriers = &(VkBufferMemoryBarrier2) {
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                                 VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = sum_vk->buf,
                .size = sum_vk->size,
                .offset = 0,
            },
            .bufferMemoryBarrierCount = 1,
        });

    RET(ff_vk_shader_update_desc_buffer(&s->vkctx, exec, &s->shd, 0, 1, 0,
                                        sum_vk, 0, sum_vk->size,
                                        VK_FORMAT_UNDEFINED));

    ff_vk_exec_bind_shader(vkctx, exec, &s->shd);
    ff_vk_shader_update_push_const(vkctx, exec, &s->shd, VK_SHADER_STAGE_COMPUTE_BIT,
                                   0, sizeof(push_data), &push_data);

    vk->CmdDispatch(exec->buf,
                    FFALIGN(in->width,  s->shd.lg_size[0]) / s->shd.lg_size[0],
                    FFALIGN(in->height, s->shd.lg_size[1]) / s->shd.lg_size[1],
                    s->shd.lg_size[2]);

    vk->CmdPipelineBarrier2(exec->buf, &(VkDependencyInfo) {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pBufferMemoryBarriers = &(VkBufferMemoryBarrier2) {
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT,
                .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                                 VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = sum_vk->buf,
                .size = sum_vk->size,
                .offset = 0,
            },
            .bufferMemoryBarrierCount = 1,
        });

    RET(ff_vk_exec_submit(vkctx, exec));
    ff_vk_exec_wait(vkctx, exec);
    evaluate(link, in, sum);

    av_buffer_unref(&sum_buf);
    return ff_filter_frame(outlink, in);

fail:
    if (exec)
        ff_vk_exec_discard_deps(&s->vkctx, exec);
    av_frame_free(&in);
    av_buffer_unref(&sum_buf);
    return err;
}

static void blackdetect_vulkan_uninit(AVFilterContext *avctx)
{
    BlackDetectVulkanContext *s = avctx->priv;
    AVFilterLink *inlink = avctx->inputs[0];
    FilterLink *inl = ff_filter_link(inlink);
    FFVulkanContext *vkctx = &s->vkctx;

    report_black_region(avctx, inl->current_pts);

    ff_vk_exec_pool_free(vkctx, &s->e);
    ff_vk_shader_free(vkctx, &s->shd);

    av_buffer_pool_uninit(&s->sum_buf_pool);

    ff_vk_uninit(&s->vkctx);

    s->initialized = 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    BlackDetectVulkanContext *s = ctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(vkctx->input_format);

    if (s->alpha && !(desc->flags & AV_PIX_FMT_FLAG_ALPHA)) {
        av_log(ctx, AV_LOG_ERROR, "Input format %s does not have an alpha channel\n",
               av_get_pix_fmt_name(vkctx->input_format));
        return AVERROR(EINVAL);
    }

    if (desc->flags & (AV_PIX_FMT_FLAG_RGB | AV_PIX_FMT_FLAG_XYZ) ||
        !(desc->flags & AV_PIX_FMT_FLAG_PLANAR)) {
        av_log(ctx, AV_LOG_ERROR, "Input format %s is not planar YUV\n",
               av_get_pix_fmt_name(vkctx->input_format));
        return AVERROR(EINVAL);
    }

    return ff_vk_filter_config_output(outlink);
}

#define OFFSET(x) offsetof(BlackDetectVulkanContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)
static const AVOption blackdetect_vulkan_options[] = {
    { "d",                  "set minimum detected black duration in seconds", OFFSET(black_min_duration_time), AV_OPT_TYPE_DOUBLE, {.dbl=2}, 0, DBL_MAX, FLAGS },
    { "black_min_duration", "set minimum detected black duration in seconds", OFFSET(black_min_duration_time), AV_OPT_TYPE_DOUBLE, {.dbl=2}, 0, DBL_MAX, FLAGS },
    { "picture_black_ratio_th", "set the picture black ratio threshold", OFFSET(picture_black_ratio_th), AV_OPT_TYPE_DOUBLE, {.dbl=.98}, 0, 1, FLAGS },
    { "pic_th",                 "set the picture black ratio threshold", OFFSET(picture_black_ratio_th), AV_OPT_TYPE_DOUBLE, {.dbl=.98}, 0, 1, FLAGS },
    { "pixel_black_th", "set the pixel black threshold", OFFSET(pixel_black_th), AV_OPT_TYPE_DOUBLE, {.dbl=.10}, 0, 1, FLAGS },
    { "pix_th",         "set the pixel black threshold", OFFSET(pixel_black_th), AV_OPT_TYPE_DOUBLE, {.dbl=.10}, 0, 1, FLAGS },
    { "alpha",          "check alpha instead of luma", OFFSET(alpha), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(blackdetect_vulkan);

static const AVFilterPad blackdetect_vulkan_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = &blackdetect_vulkan_filter_frame,
        .config_props = &ff_vk_filter_config_input,
    },
};

static const AVFilterPad blackdetect_vulkan_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = &config_output,
    },
};

const FFFilter ff_vf_blackdetect_vulkan = {
    .p.name         = "blackdetect_vulkan",
    .p.description  = NULL_IF_CONFIG_SMALL("Detect video intervals that are (almost) black."),
    .p.priv_class   = &blackdetect_vulkan_class,
    .p.flags        = AVFILTER_FLAG_HWDEVICE,
    .priv_size      = sizeof(BlackDetectVulkanContext),
    .init           = &ff_vk_filter_init,
    .uninit         = &blackdetect_vulkan_uninit,
    FILTER_INPUTS(blackdetect_vulkan_inputs),
    FILTER_OUTPUTS(blackdetect_vulkan_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_VULKAN),
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
