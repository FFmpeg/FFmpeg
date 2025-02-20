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

#include "libavutil/avassert.h"
#include "libavutil/vulkan_spirv.h"
#include "libavutil/opt.h"
#include "libavutil/timestamp.h"
#include "vulkan_filter.h"

#include "filters.h"

typedef struct SceneDetectVulkanContext {
    FFVulkanContext vkctx;

    int initialized;
    FFVkExecPool e;
    AVVulkanDeviceQueueFamily *qf;
    FFVulkanShader shd;
    AVBufferPool *det_buf_pool;

    double threshold;
    int sc_pass;

    int nb_planes;
    double prev_mafd;
    AVFrame *prev;
    AVFrame *cur;
} SceneDetectVulkanContext;

typedef struct SceneDetectBuf {
#define SLICES 16
    uint32_t frame_sad[SLICES];
} SceneDetectBuf;

static av_cold int init_filter(AVFilterContext *ctx)
{
    int err;
    uint8_t *spv_data;
    size_t spv_len;
    void *spv_opaque = NULL;
    SceneDetectVulkanContext *s = ctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;
    FFVulkanShader *shd;
    FFVkSPIRVCompiler *spv;
    FFVulkanDescriptorSetBinding *desc;

    const AVPixFmtDescriptor *pixdesc = av_pix_fmt_desc_get(s->vkctx.input_format);
    const int lumaonly = !(pixdesc->flags & AV_PIX_FMT_FLAG_RGB) &&
                         (pixdesc->flags & AV_PIX_FMT_FLAG_PLANAR);
    s->nb_planes = lumaonly ? 1 : av_pix_fmt_count_planes(s->vkctx.input_format);

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
    RET(ff_vk_shader_init(vkctx, &s->shd, "scdet",
                          VK_SHADER_STAGE_COMPUTE_BIT,
                          (const char *[]) { "GL_KHR_shader_subgroup_arithmetic" }, 1,
                          32, 32, 1,
                          0));
    shd = &s->shd;

    desc = (FFVulkanDescriptorSetBinding []) {
        {
            .name       = "prev_img",
            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .mem_layout = ff_vk_shader_rep_fmt(s->vkctx.input_format, FF_VK_REP_UINT),
            .mem_quali  = "readonly",
            .dimensions = 2,
            .elems      = av_pix_fmt_count_planes(s->vkctx.input_format),
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
        }, {
            .name       = "cur_img",
            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .mem_layout = ff_vk_shader_rep_fmt(s->vkctx.input_format, FF_VK_REP_UINT),
            .mem_quali  = "readonly",
            .dimensions = 2,
            .elems      = av_pix_fmt_count_planes(s->vkctx.input_format),
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
        }, {
            .name        = "sad_buffer",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .buf_content = "uint frame_sad[];",
        }
    };

    RET(ff_vk_shader_add_descriptor_set(vkctx, &s->shd, desc, 3, 0, 0));

    GLSLC(0, shared uint wg_sum;                                              );
    GLSLC(0, void main()                                                      );
    GLSLC(0, {                                                                );
    GLSLF(1,     const uint slice = gl_WorkGroupID.x %% %u;            ,SLICES);
    GLSLC(1,     const ivec2 pos = ivec2(gl_GlobalInvocationID.xy);           );
    GLSLC(1,     wg_sum = 0;                                                  );
    GLSLC(1,     barrier();                                                   );
    for (int i = 0; i < s->nb_planes; i++) {
        GLSLF(1, if (IS_WITHIN(pos, imageSize(cur_img[%d]))) {              ,i);
        GLSLF(2,     uvec4 prev = imageLoad(prev_img[%d], pos);             ,i);
        GLSLF(2,     uvec4 cur  = imageLoad(cur_img[%d],  pos);             ,i);
        GLSLC(2,     uvec4 sad = abs(ivec4(cur) - ivec4(prev));               );
        GLSLC(2,     uint sum = subgroupAdd(sad.x + sad.y + sad.z);           );
        GLSLC(2,     if (subgroupElect())                                     );
        GLSLC(3,         atomicAdd(wg_sum, sum);                              );
        GLSLC(1, }                                                            );
    }
    GLSLC(1,     barrier();                                                   );
    GLSLC(1,     if (gl_LocalInvocationIndex == 0)                            );
    GLSLC(2,         atomicAdd(frame_sad[slice], wg_sum);                     );
    GLSLC(0, }                                                                );

    RET(spv->compile_shader(vkctx, spv, &s->shd, &spv_data, &spv_len, "main",
                            &spv_opaque));
    RET(ff_vk_shader_link(vkctx, &s->shd, spv_data, spv_len, "main"));

    RET(ff_vk_shader_register_exec(vkctx, &s->e, &s->shd));

    s->initialized = 1;

fail:
    if (spv_opaque)
        spv->free_shader(spv, &spv_opaque);
    if (spv)
        spv->uninit(&spv);

    return err;
}

static double evaluate(AVFilterContext *ctx, const SceneDetectBuf *buf)
{
    SceneDetectVulkanContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(s->vkctx.input_format);
    const AVFilterLink *inlink = ctx->inputs[0];
    uint64_t count;
    double mafd, diff;

    uint64_t sad = 0;
    for (int i = 0; i < SLICES; i++)
        sad += buf->frame_sad[i];

    av_assert2(s->nb_planes == 1 || !(desc->log2_chroma_w || desc->log2_chroma_h));
    count = s->nb_planes * inlink->w * inlink->h;
    mafd = (double) sad * 100.0 / count / (1ULL << desc->comp[0].depth);
    diff = fabs(mafd - s->prev_mafd);
    s->prev_mafd = mafd;

    return av_clipf(FFMIN(mafd, diff), 0.0, 100.0);
}

static int scdet_vulkan_filter_frame(AVFilterLink *link, AVFrame *in)
{
    int err;
    AVFilterContext *ctx = link->dst;
    SceneDetectVulkanContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];

    VkImageView prev_views[AV_NUM_DATA_POINTERS];
    VkImageView cur_views[AV_NUM_DATA_POINTERS];
    VkImageMemoryBarrier2 img_bar[8];
    int nb_img_bar = 0;

    FFVulkanContext *vkctx = &s->vkctx;
    FFVulkanFunctions *vk = &vkctx->vkfn;
    FFVkExecContext *exec = NULL;
    AVBufferRef *buf = NULL;
    FFVkBuffer *buf_vk;

    SceneDetectBuf *sad;
    double score = 0.0;
    char str[64];

    if (!s->initialized)
        RET(init_filter(ctx));

    av_frame_free(&s->prev);
    s->prev = s->cur;
    s->cur = av_frame_clone(in);
    if (!s->prev)
        goto done;

    RET(ff_vk_get_pooled_buffer(vkctx, &s->det_buf_pool, &buf,
                                VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                NULL,
                                sizeof(SceneDetectBuf),
                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));
    buf_vk = (FFVkBuffer *)buf->data;
    sad = (SceneDetectBuf *) buf_vk->mapped_mem;

    exec = ff_vk_exec_get(vkctx, &s->e);
    ff_vk_exec_start(vkctx, exec);

    RET(ff_vk_exec_add_dep_frame(vkctx, exec, s->prev,
                                 VK_PIPELINE_STAGE_2_NONE,
                                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT));
    RET(ff_vk_create_imageviews(vkctx, exec, prev_views, s->prev, FF_VK_REP_UINT));

    ff_vk_shader_update_img_array(vkctx, exec, &s->shd, s->prev, prev_views, 0, 0,
                                  VK_IMAGE_LAYOUT_GENERAL, VK_NULL_HANDLE);

    ff_vk_frame_barrier(vkctx, exec, s->prev, img_bar, &nb_img_bar,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_SHADER_READ_BIT,
                        VK_IMAGE_LAYOUT_GENERAL,
                        VK_QUEUE_FAMILY_IGNORED);

    RET(ff_vk_exec_add_dep_frame(vkctx, exec, s->cur,
                                 VK_PIPELINE_STAGE_2_NONE,
                                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT));
    RET(ff_vk_create_imageviews(vkctx, exec, cur_views, s->cur, FF_VK_REP_UINT));

    ff_vk_shader_update_img_array(vkctx, exec, &s->shd, s->cur, cur_views, 0, 1,
                                  VK_IMAGE_LAYOUT_GENERAL, VK_NULL_HANDLE);

    ff_vk_frame_barrier(vkctx, exec, s->cur, img_bar, &nb_img_bar,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_SHADER_READ_BIT,
                        VK_IMAGE_LAYOUT_GENERAL,
                        VK_QUEUE_FAMILY_IGNORED);

    /* zero buffer */
    vk->CmdPipelineBarrier2(exec->buf, &(VkDependencyInfo) {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pBufferMemoryBarriers = &(VkBufferMemoryBarrier2) {
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
                .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = buf_vk->buf,
                .size = buf_vk->size,
                .offset = 0,
            },
            .bufferMemoryBarrierCount = 1,
        });

    vk->CmdFillBuffer(exec->buf, buf_vk->buf, 0, buf_vk->size, 0x0);

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
                .buffer = buf_vk->buf,
                .size = buf_vk->size,
                .offset = 0,
            },
            .bufferMemoryBarrierCount = 1,
        });

    RET(ff_vk_shader_update_desc_buffer(&s->vkctx, exec, &s->shd, 0, 2, 0,
                                        buf_vk, 0, buf_vk->size,
                                        VK_FORMAT_UNDEFINED));

    ff_vk_exec_bind_shader(vkctx, exec, &s->shd);

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
                .buffer = buf_vk->buf,
                .size = buf_vk->size,
                .offset = 0,
            },
            .bufferMemoryBarrierCount = 1,
        });

    RET(ff_vk_exec_submit(vkctx, exec));
    ff_vk_exec_wait(vkctx, exec);
    score = evaluate(ctx, sad);

done:
    snprintf(str, sizeof(str), "%0.3f", s->prev_mafd);
    av_dict_set(&in->metadata, "lavfi.scd.mafd", str, 0);
    snprintf(str, sizeof(str), "%0.3f", score);
    av_dict_set(&in->metadata, "lavfi.scd.score", str, 0);

    if (score >= s->threshold) {
        const char *pts = av_ts2timestr(in->pts, &link->time_base);
        av_dict_set(&in->metadata, "lavfi.scd.time", pts, 0);
        av_log(s, AV_LOG_INFO, "lavfi.scd.score: %.3f, lavfi.scd.time: %s\n",
               score, pts);
    }

    av_buffer_unref(&buf);
    if (!s->sc_pass || score >= s->threshold)
        return ff_filter_frame(outlink, in);
    else {
        av_frame_free(&in);
        return 0;
    }

fail:
    if (exec)
        ff_vk_exec_discard_deps(&s->vkctx, exec);
    av_frame_free(&in);
    av_buffer_unref(&buf);
    return err;
}

static void scdet_vulkan_uninit(AVFilterContext *avctx)
{
    SceneDetectVulkanContext *s = avctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;

    av_frame_free(&s->prev);
    av_frame_free(&s->cur);

    ff_vk_exec_pool_free(vkctx, &s->e);
    ff_vk_shader_free(vkctx, &s->shd);

    av_buffer_pool_uninit(&s->det_buf_pool);

    ff_vk_uninit(&s->vkctx);

    s->initialized = 0;
}

#define OFFSET(x) offsetof(SceneDetectVulkanContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)
static const AVOption scdet_vulkan_options[] = {
    { "threshold",   "set scene change detect threshold",        OFFSET(threshold),  AV_OPT_TYPE_DOUBLE,   {.dbl = 10.},     0,  100., FLAGS },
    { "t",           "set scene change detect threshold",        OFFSET(threshold),  AV_OPT_TYPE_DOUBLE,   {.dbl = 10.},     0,  100., FLAGS },
    { "sc_pass",     "Set the flag to pass scene change frames", OFFSET(sc_pass),    AV_OPT_TYPE_BOOL,     {.i64 = 0  },     0,    1,  FLAGS },
    { "s",           "Set the flag to pass scene change frames", OFFSET(sc_pass),    AV_OPT_TYPE_BOOL,     {.i64 = 0  },     0,    1,  FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(scdet_vulkan);

static const AVFilterPad scdet_vulkan_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = &scdet_vulkan_filter_frame,
        .config_props = &ff_vk_filter_config_input,
    },
};

static const AVFilterPad scdet_vulkan_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = &ff_vk_filter_config_output,
    },
};

const FFFilter ff_vf_scdet_vulkan = {
    .p.name         = "scdet_vulkan",
    .p.description  = NULL_IF_CONFIG_SMALL("Detect video scene change"),
    .p.priv_class   = &scdet_vulkan_class,
    .p.flags        = AVFILTER_FLAG_HWDEVICE,
    .priv_size      = sizeof(SceneDetectVulkanContext),
    .init           = &ff_vk_filter_init,
    .uninit         = &scdet_vulkan_uninit,
    FILTER_INPUTS(scdet_vulkan_inputs),
    FILTER_OUTPUTS(scdet_vulkan_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_VULKAN),
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
