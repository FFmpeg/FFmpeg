/*
 * Apple ProRes encoder
 *
 * Copyright (c) 2011 Anatoliy Wasserman
 * Copyright (c) 2012 Konstantin Shishkov
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

#include "libavutil/buffer.h"
#include "libavutil/macros.h"
#include "libavutil/mem.h"
#include "libavutil/mem_internal.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/vulkan_spirv.h"
#include "libavutil/hwcontext_vulkan.h"
#include "libavutil/vulkan_loader.h"
#include "libavutil/vulkan.h"
#include "avcodec.h"
#include "codec.h"
#include "codec_internal.h"
#include "encode.h"
#include "packet.h"
#include "put_bits.h"
#include "profiles.h"
#include "bytestream.h"
#include "proresdata.h"
#include "proresenc_kostya_common.h"
#include "hwconfig.h"

#define DCTSIZE 8

typedef struct ProresDataTables {
    int16_t qmat[128][64];
    int16_t qmat_chroma[128][64];
} ProresDataTables;

typedef struct SliceDataInfo {
    int plane;
    int line_add;
    int bits_per_sample;
} SliceDataInfo;

typedef struct EncodeSliceInfo {
    VkDeviceAddress bytestream;
    VkDeviceAddress seek_table;
} EncodeSliceInfo;

typedef struct SliceData {
    uint32_t mbs_per_slice;
    int16_t rows[MAX_PLANES * MAX_MBS_PER_SLICE * 256];
} SliceData;

typedef struct SliceScore {
    int bits[MAX_STORED_Q][4];
    int error[MAX_STORED_Q][4];
    int total_bits[MAX_STORED_Q];
    int total_error[MAX_STORED_Q];
    int overquant;
    int buf_start;
    int quant;
} SliceScore;

typedef struct VulkanEncodeProresFrameData {
    /* Intermediate buffers */
    AVBufferRef *out_data_ref[2];
    AVBufferRef *slice_data_ref[2];
    AVBufferRef *slice_score_ref[2];
    AVBufferRef *frame_size_ref[2];

    /* Copied from the source */
    int64_t pts;
    int64_t duration;
    void        *frame_opaque;
    AVBufferRef *frame_opaque_ref;
    enum AVColorTransferCharacteristic color_trc;
    enum AVColorSpace colorspace;
    enum AVColorPrimaries color_primaries;
    int key_frame;
    int flags;
} VulkanEncodeProresFrameData;

typedef struct ProresVulkanContext {
    ProresContext ctx;

    /* Vulkan state */
    FFVulkanContext vkctx;
    AVVulkanDeviceQueueFamily *qf;
    FFVkExecPool e;
    AVVulkanDeviceQueueFamily *transfer_qf;
    FFVkExecPool transfer_exec_pool;
    AVBufferPool *pkt_buf_pool;
    AVBufferPool *slice_data_buf_pool;
    AVBufferPool *slice_score_buf_pool;
    AVBufferPool *frame_size_buf_pool;

    FFVulkanShader alpha_data_shd;
    FFVulkanShader slice_data_shd[2];
    FFVulkanShader estimate_slice_shd;
    FFVulkanShader encode_slice_shd;
    FFVulkanShader trellis_node_shd;
    FFVkBuffer prores_data_tables_buf;

    int *slice_quants;
    SliceScore *slice_scores;
    ProresDataTables *tables;

    int in_flight;
    int async_depth;
    AVFrame *frame;
    VulkanEncodeProresFrameData *exec_ctx_info;
} ProresVulkanContext;

extern const unsigned char ff_prores_ks_alpha_data_comp_spv_data[];
extern const unsigned int ff_prores_ks_alpha_data_comp_spv_len;

extern const unsigned char ff_prores_ks_slice_data_comp_spv_data[];
extern const unsigned int ff_prores_ks_slice_data_comp_spv_len;

extern const unsigned char ff_prores_ks_estimate_slice_comp_spv_data[];
extern const unsigned int ff_prores_ks_estimate_slice_comp_spv_len;

extern const unsigned char ff_prores_ks_trellis_node_comp_spv_data[];
extern const unsigned int ff_prores_ks_trellis_node_comp_spv_len;

extern const unsigned char ff_prores_ks_encode_slice_comp_spv_data[];
extern const unsigned int ff_prores_ks_encode_slice_comp_spv_len;

static int init_slice_data_pipeline(ProresVulkanContext *pv, FFVulkanShader *shd, int blocks_per_mb)
{
    int err = 0;
    FFVulkanContext *vkctx = &pv->vkctx;
    FFVulkanDescriptorSetBinding *desc;

    SPEC_LIST_CREATE(sl, 5, 5 * sizeof(uint32_t))
    SPEC_LIST_ADD(sl, 0, 32, pv->ctx.mbs_per_slice);
    SPEC_LIST_ADD(sl, 1, 32, blocks_per_mb);
    SPEC_LIST_ADD(sl, 2, 32, pv->ctx.mb_width);
    SPEC_LIST_ADD(sl, 3, 32, pv->ctx.pictures_per_frame);
    SPEC_LIST_ADD(sl, 16, 32, blocks_per_mb * pv->ctx.mbs_per_slice); /* nb_blocks */

    ff_vk_shader_load(shd, VK_SHADER_STAGE_COMPUTE_BIT, sl,
                      (uint32_t []) { DCTSIZE, blocks_per_mb, pv->ctx.mbs_per_slice }, 0);

    desc = (FFVulkanDescriptorSetBinding []) {
        {
            .name        = "SliceBuffer",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        {
            .name       = "planes",
            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
            .elems      = av_pix_fmt_count_planes(vkctx->frames->sw_format),
        },
    };
    RET(ff_vk_shader_add_descriptor_set(vkctx, shd, desc, 2, 0, 0));

    ff_vk_shader_add_push_const(shd, 0, sizeof(SliceDataInfo), VK_SHADER_STAGE_COMPUTE_BIT);

    RET(ff_vk_shader_link(vkctx, shd,
                          ff_prores_ks_slice_data_comp_spv_data,
                          ff_prores_ks_slice_data_comp_spv_len, "main"));

    RET(ff_vk_shader_register_exec(vkctx, &pv->e, shd));

fail:
    return err;
}

static int init_alpha_data_pipeline(ProresVulkanContext *pv, FFVulkanShader* shd)
{
    int err = 0;
    FFVulkanContext *vkctx = &pv->vkctx;
    FFVulkanDescriptorSetBinding *desc;

    SPEC_LIST_CREATE(sl, 4, 4 * sizeof(uint32_t))
    SPEC_LIST_ADD(sl, 0, 32, pv->ctx.alpha_bits);
    SPEC_LIST_ADD(sl, 1, 32, pv->ctx.slices_width);
    SPEC_LIST_ADD(sl, 2, 32, pv->ctx.mb_width);
    SPEC_LIST_ADD(sl, 3, 32, pv->ctx.mbs_per_slice);

    ff_vk_shader_load(shd, VK_SHADER_STAGE_COMPUTE_BIT, sl,
                      (uint32_t []) { 16, 16, 1 }, 0);

    desc = (FFVulkanDescriptorSetBinding []) {
        {
            .name        = "SliceBuffer",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        {
            .name       = "plane",
            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };
    RET(ff_vk_shader_add_descriptor_set(vkctx, shd, desc, 2, 0, 0));

    RET(ff_vk_shader_link(vkctx, shd,
                          ff_prores_ks_alpha_data_comp_spv_data,
                          ff_prores_ks_alpha_data_comp_spv_len, "main"));

    RET(ff_vk_shader_register_exec(vkctx, &pv->e, shd));

fail:
    return err;
}

static int init_estimate_slice_pipeline(ProresVulkanContext *pv, FFVulkanShader* shd)
{
    int err = 0;
    FFVulkanContext *vkctx = &pv->vkctx;
    FFVulkanDescriptorSetBinding *desc;
    int subgroup_size = vkctx->subgroup_props.maxSubgroupSize;
    int dim_x = pv->ctx.alpha_bits ? subgroup_size : (subgroup_size / 3) * 3;

    SPEC_LIST_CREATE(sl, 8, 8 * sizeof(uint32_t))
    SPEC_LIST_ADD(sl, 0, 32, pv->ctx.mbs_per_slice);
    SPEC_LIST_ADD(sl, 1, 32, pv->ctx.chroma_factor);
    SPEC_LIST_ADD(sl, 2, 32, pv->ctx.alpha_bits);
    SPEC_LIST_ADD(sl, 3, 32, pv->ctx.num_planes);
    SPEC_LIST_ADD(sl, 4, 32, pv->ctx.slices_per_picture);
    SPEC_LIST_ADD(sl, 5, 32, pv->ctx.force_quant ? 0 : pv->ctx.profile_info->min_quant);
    SPEC_LIST_ADD(sl, 6, 32, pv->ctx.force_quant ? 0 : pv->ctx.profile_info->max_quant);
    SPEC_LIST_ADD(sl, 7, 32, pv->ctx.bits_per_mb);

    ff_vk_shader_load(shd, VK_SHADER_STAGE_COMPUTE_BIT, sl,
                      (uint32_t []) { dim_x, 1, 1 }, 0);

    desc = (FFVulkanDescriptorSetBinding []) {
        {
            .name        = "SliceBuffer",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        {
            .name        = "SliceScores",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        {
            .name        = "ProresDataTables",
            .type        = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };
    RET(ff_vk_shader_add_descriptor_set(vkctx, shd, desc, 3, 0, 0));

    RET(ff_vk_shader_link(vkctx, shd,
                          ff_prores_ks_estimate_slice_comp_spv_data,
                          ff_prores_ks_estimate_slice_comp_spv_len, "main"));
    RET(ff_vk_shader_register_exec(vkctx, &pv->e, shd));

fail:
    return err;
}

static int init_trellis_node_pipeline(ProresVulkanContext *pv, FFVulkanShader* shd)
{
    int err = 0;
    FFVulkanContext *vkctx = &pv->vkctx;
    FFVulkanDescriptorSetBinding *desc;
    int subgroup_size = vkctx->subgroup_props.maxSubgroupSize;
    int num_subgroups = FFALIGN(pv->ctx.mb_height, subgroup_size) / subgroup_size;

    SPEC_LIST_CREATE(sl, 8, 8 * sizeof(uint32_t))
    SPEC_LIST_ADD(sl, 0, 32, pv->ctx.slices_width);
    SPEC_LIST_ADD(sl, 1, 32, num_subgroups);
    SPEC_LIST_ADD(sl, 2, 32, pv->ctx.num_planes);
    SPEC_LIST_ADD(sl, 3, 32, pv->ctx.force_quant);
    SPEC_LIST_ADD(sl, 4, 32, pv->ctx.profile_info->min_quant);
    SPEC_LIST_ADD(sl, 5, 32, pv->ctx.profile_info->max_quant);
    SPEC_LIST_ADD(sl, 6, 32, pv->ctx.mbs_per_slice);
    SPEC_LIST_ADD(sl, 7, 32, pv->ctx.bits_per_mb);

    ff_vk_shader_load(shd, VK_SHADER_STAGE_COMPUTE_BIT, sl,
                      (uint32_t []) { pv->ctx.mb_height, 1, 1 }, 0);

    desc = (FFVulkanDescriptorSetBinding []) {
        {
            .name        = "FrameSize",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        {
            .name        = "SliceScores",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };
    RET(ff_vk_shader_add_descriptor_set(vkctx, shd, desc, 2, 0, 0));

    RET(ff_vk_shader_link(vkctx, shd,
                          ff_prores_ks_trellis_node_comp_spv_data,
                          ff_prores_ks_trellis_node_comp_spv_len, "main"));

    RET(ff_vk_shader_register_exec(vkctx, &pv->e, shd));

fail:
    return err;
}

static int init_encode_slice_pipeline(ProresVulkanContext *pv, FFVulkanShader* shd)
{
    int err = 0;
    FFVulkanContext *vkctx = &pv->vkctx;
    FFVulkanDescriptorSetBinding *desc;

    SPEC_LIST_CREATE(sl, 6, 6 * sizeof(uint32_t))
    SPEC_LIST_ADD(sl, 0, 32, pv->ctx.mbs_per_slice);
    SPEC_LIST_ADD(sl, 1, 32, pv->ctx.chroma_factor);
    SPEC_LIST_ADD(sl, 2, 32, pv->ctx.alpha_bits);
    SPEC_LIST_ADD(sl, 3, 32, pv->ctx.num_planes);
    SPEC_LIST_ADD(sl, 4, 32, pv->ctx.slices_per_picture);
    SPEC_LIST_ADD(sl, 5, 32, pv->ctx.force_quant ? pv->ctx.force_quant : pv->ctx.profile_info->max_quant);

    ff_vk_shader_load(shd, VK_SHADER_STAGE_COMPUTE_BIT, sl,
                      (uint32_t []) { 64, 1, 1 }, 0);

    desc = (FFVulkanDescriptorSetBinding []) {
        {
            .name        = "SliceBuffer",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        {
            .name        = "SliceScores",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        {
            .name        = "ProresDataTables",
            .type        = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };
    RET(ff_vk_shader_add_descriptor_set(vkctx, shd, desc, 3, 0, 0));

    ff_vk_shader_add_push_const(shd, 0, sizeof(EncodeSliceInfo), VK_SHADER_STAGE_COMPUTE_BIT);

    RET(ff_vk_shader_link(vkctx, shd,
                          ff_prores_ks_encode_slice_comp_spv_data,
                          ff_prores_ks_encode_slice_comp_spv_len, "main"));

    RET(ff_vk_shader_register_exec(vkctx, &pv->e, shd));

fail:
    return err;
}

static int vulkan_encode_prores_submit_frame(AVCodecContext *avctx, FFVkExecContext *exec,
                                             AVFrame *frame, int picture_idx)
{
    ProresVulkanContext *pv = avctx->priv_data;
    ProresContext *ctx = &pv->ctx;
    VulkanEncodeProresFrameData *pd = exec->opaque;
    FFVulkanContext *vkctx = &pv->vkctx;
    FFVulkanFunctions *vk = &vkctx->vkfn;
    int err = 0, nb_img_bar = 0, i, is_chroma;
    int min_quant = ctx->profile_info->min_quant;
    int max_quant = ctx->profile_info->max_quant;
    int subgroup_size = vkctx->subgroup_props.maxSubgroupSize;
    int estimate_dim_x = ctx->alpha_bits ? subgroup_size : (subgroup_size / 3) * 3;
    int transfer_slices = vkctx->extensions & FF_VK_EXT_EXTERNAL_HOST_MEMORY;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(vkctx->frames->sw_format);
    VkImageView views[AV_NUM_DATA_POINTERS];
    VkImageMemoryBarrier2 img_bar[AV_NUM_DATA_POINTERS];
    FFVkBuffer *pkt_vk_buf, *slice_data_buf, *slice_score_buf, *frame_size_buf;
    SliceDataInfo slice_data_info;
    EncodeSliceInfo encode_info;
    FFVulkanShader *shd;

    /* Start recording */
    ff_vk_exec_start(vkctx, exec);

    /* Get a pooled buffer for writing output data */
    RET(ff_vk_get_pooled_buffer(vkctx, &pv->pkt_buf_pool, &pd->out_data_ref[picture_idx],
                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                VK_BUFFER_USAGE_TRANSFER_SRC_BIT, NULL,
                                ctx->frame_size_upper_bound + FF_INPUT_BUFFER_MIN_SIZE,
                                transfer_slices ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
                                                : (VK_MEMORY_PROPERTY_HOST_CACHED_BIT |
                                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)));
    pkt_vk_buf = (FFVkBuffer*)pd->out_data_ref[picture_idx]->data;
    ff_vk_exec_add_dep_buf(vkctx, exec, &pd->out_data_ref[picture_idx], 1, 1);

    /* Allocate buffer for writing slice data */
    RET(ff_vk_get_pooled_buffer(vkctx, &pv->slice_data_buf_pool, &pd->slice_data_ref[picture_idx],
                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, NULL,
                                ctx->slices_per_picture * sizeof(SliceData),
                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT));
    slice_data_buf = (FFVkBuffer*)pd->slice_data_ref[picture_idx]->data;
    ff_vk_exec_add_dep_buf(vkctx, exec, &pd->slice_data_ref[picture_idx], 1, 1);

    /* Allocate buffer for writing slice scores */
    RET(ff_vk_get_pooled_buffer(vkctx, &pv->slice_score_buf_pool, &pd->slice_score_ref[picture_idx],
                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, NULL,
                                ctx->slices_per_picture * sizeof(SliceScore),
                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT));
    slice_score_buf = (FFVkBuffer*)pd->slice_score_ref[picture_idx]->data;
    ff_vk_exec_add_dep_buf(vkctx, exec, &pd->slice_score_ref[picture_idx], 1, 1);

    /* Allocate buffer for writing frame size */
    RET(ff_vk_get_pooled_buffer(vkctx, &pv->frame_size_buf_pool, &pd->frame_size_ref[picture_idx],
                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, NULL,
                                sizeof(int),
                                VK_MEMORY_PROPERTY_HOST_CACHED_BIT |
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));
    frame_size_buf = (FFVkBuffer*)pd->frame_size_ref[picture_idx]->data;
    ff_vk_exec_add_dep_buf(vkctx, exec, &pd->frame_size_ref[picture_idx], 1, 1);

    /* Generate barriers and image views for frame images. */
    RET(ff_vk_exec_add_dep_frame(vkctx, exec, frame,
                                 VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT));
    RET(ff_vk_create_imageviews(vkctx, exec, views, frame, FF_VK_REP_INT));
    ff_vk_frame_barrier(vkctx, exec, frame, img_bar, &nb_img_bar,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_SHADER_READ_BIT,
                        VK_IMAGE_LAYOUT_GENERAL,
                        VK_QUEUE_FAMILY_IGNORED);

    /* Submit the image barriers. */
    vk->CmdPipelineBarrier2(exec->buf, &(VkDependencyInfo) {
                                           .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                           .pImageMemoryBarriers = img_bar,
                                           .imageMemoryBarrierCount = nb_img_bar,
                                       });

    /* Apply FDCT on input image data for future passes */
    slice_data_info = (SliceDataInfo) {
        .line_add = ctx->pictures_per_frame == 1 ? 0 : picture_idx ^ !(frame->flags & AV_FRAME_FLAG_TOP_FIELD_FIRST),
    };
    for (i = 0; i < ctx->num_planes; i++) {
        is_chroma = (i == 1 || i == 2);
        shd = &pv->slice_data_shd[!is_chroma || ctx->chroma_factor == CFACTOR_Y444];
        if (i < 3) {
            slice_data_info.plane = i;
            slice_data_info.bits_per_sample = desc->comp[i].depth;
            ff_vk_shader_update_desc_buffer(vkctx, exec, shd, 0, 0, 0,
                                            slice_data_buf, 0, slice_data_buf->size,
                                            VK_FORMAT_UNDEFINED);
            ff_vk_shader_update_img_array(vkctx, exec, shd, frame, views, 0, 1,
                                          VK_IMAGE_LAYOUT_GENERAL, VK_NULL_HANDLE);
            ff_vk_exec_bind_shader(vkctx, exec, shd);
            ff_vk_shader_update_push_const(vkctx, exec, shd, VK_SHADER_STAGE_COMPUTE_BIT,
                                           0, sizeof(SliceDataInfo), &slice_data_info);
            vk->CmdDispatch(exec->buf, ctx->slices_width, ctx->mb_height, 1);
        } else {
            ff_vk_shader_update_desc_buffer(vkctx, exec, &pv->alpha_data_shd, 0, 0, 0,
                                            slice_data_buf, 0, slice_data_buf->size,
                                            VK_FORMAT_UNDEFINED);
            ff_vk_shader_update_img(vkctx, exec, &pv->alpha_data_shd, 0, 1, 0, views[3],
                                    VK_IMAGE_LAYOUT_GENERAL, VK_NULL_HANDLE);
            ff_vk_exec_bind_shader(vkctx, exec, &pv->alpha_data_shd);
            vk->CmdDispatch(exec->buf, ctx->mb_width, ctx->mb_height, 1);
        }
    }

    /* Wait for writes to slice buffer. */
    vk->CmdPipelineBarrier2(exec->buf, &(VkDependencyInfo) {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pBufferMemoryBarriers = & (VkBufferMemoryBarrier2) {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .pNext = NULL,
            .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = slice_data_buf->buf,
            .offset = 0,
            .size = slice_data_buf->size,
        },
        .bufferMemoryBarrierCount = 1,
    });

    /* Estimate slice bits and error for each quant */
    ff_vk_shader_update_desc_buffer(vkctx, exec, &pv->estimate_slice_shd, 0, 0, 0,
                                    slice_data_buf, 0, slice_data_buf->size,
                                    VK_FORMAT_UNDEFINED);
    ff_vk_shader_update_desc_buffer(vkctx, exec, &pv->estimate_slice_shd, 0, 1, 0,
                                    slice_score_buf, 0, slice_score_buf->size,
                                    VK_FORMAT_UNDEFINED);
    ff_vk_shader_update_desc_buffer(vkctx, exec, &pv->estimate_slice_shd, 0, 2, 0,
                                    &pv->prores_data_tables_buf, 0, pv->prores_data_tables_buf.size,
                                    VK_FORMAT_UNDEFINED);
    ff_vk_exec_bind_shader(vkctx, exec, &pv->estimate_slice_shd);
    vk->CmdDispatch(exec->buf, (ctx->slices_per_picture * ctx->num_planes + estimate_dim_x - 1) / estimate_dim_x,
                               ctx->force_quant ? 1 : (max_quant - min_quant + 1), 1);

    /* Wait for writes to score buffer. */
    vk->CmdPipelineBarrier2(exec->buf, &(VkDependencyInfo) {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pBufferMemoryBarriers = & (VkBufferMemoryBarrier2) {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .pNext = NULL,
            .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = slice_score_buf->buf,
            .offset = 0,
            .size = slice_score_buf->size,
        },
        .bufferMemoryBarrierCount = 1,
    });

    /* Compute optimal quant value for each slice */
    ff_vk_shader_update_desc_buffer(vkctx, exec, &pv->trellis_node_shd, 0, 0, 0,
                                    frame_size_buf, 0, frame_size_buf->size,
                                    VK_FORMAT_UNDEFINED);
    ff_vk_shader_update_desc_buffer(vkctx, exec, &pv->trellis_node_shd, 0, 1, 0,
                                    slice_score_buf, 0, slice_score_buf->size,
                                    VK_FORMAT_UNDEFINED);
    ff_vk_exec_bind_shader(vkctx, exec, &pv->trellis_node_shd);
    vk->CmdDispatch(exec->buf, 1, 1, 1);

    /* Wait for writes to quant buffer. */
    vk->CmdPipelineBarrier2(exec->buf, &(VkDependencyInfo) {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pBufferMemoryBarriers = & (VkBufferMemoryBarrier2) {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .pNext = NULL,
            .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = frame_size_buf->buf,
            .offset = 0,
            .size = frame_size_buf->size,
        },
        .bufferMemoryBarrierCount = 1,
    });

    /* Encode slices. */
    encode_info = (EncodeSliceInfo) {
        .seek_table = pkt_vk_buf->address,
        .bytestream = pkt_vk_buf->address + ctx->slices_per_picture * 2,
    };
    ff_vk_shader_update_desc_buffer(vkctx, exec, &pv->encode_slice_shd, 0, 0, 0,
                                    slice_data_buf, 0, slice_data_buf->size,
                                    VK_FORMAT_UNDEFINED);
    ff_vk_shader_update_desc_buffer(vkctx, exec, &pv->encode_slice_shd, 0, 1, 0,
                                    slice_score_buf, 0, slice_score_buf->size,
                                    VK_FORMAT_UNDEFINED);
    ff_vk_shader_update_desc_buffer(vkctx, exec, &pv->encode_slice_shd, 0, 2, 0,
                                    &pv->prores_data_tables_buf, 0, pv->prores_data_tables_buf.size,
                                    VK_FORMAT_UNDEFINED);
    ff_vk_exec_bind_shader(vkctx, exec, &pv->encode_slice_shd);
    ff_vk_shader_update_push_const(vkctx, exec, &pv->encode_slice_shd,
                                   VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(encode_info), &encode_info);
    vk->CmdDispatch(exec->buf, FFALIGN(ctx->slices_per_picture, 64) / 64,
                               ctx->num_planes, 1);

fail:
    return err;
}

static uint8_t *write_frame_header(AVCodecContext *avctx, ProresContext *ctx,
                                   uint8_t **orig_buf, int flags,
                                   enum AVColorPrimaries color_primaries,
                                   enum AVColorTransferCharacteristic color_trc,
                                   enum AVColorSpace colorspace)
{
    uint8_t *buf, *tmp;
    uint8_t frame_flags;

    // frame atom
    *orig_buf += 4;                            // frame size
    bytestream_put_be32  (orig_buf, FRAME_ID); // frame container ID
    buf = *orig_buf;

    // frame header
    tmp = buf;
    buf += 2;                                   // frame header size will be stored here
    bytestream_put_be16  (&buf, ctx->chroma_factor != CFACTOR_Y422 || ctx->alpha_bits ? 1 : 0);
    bytestream_put_buffer(&buf, (uint8_t*)ctx->vendor, 4);
    bytestream_put_be16  (&buf, avctx->width);
    bytestream_put_be16  (&buf, avctx->height);

    frame_flags = ctx->chroma_factor << 6;
    if (avctx->flags & AV_CODEC_FLAG_INTERLACED_DCT)
        frame_flags |= (flags & AV_FRAME_FLAG_TOP_FIELD_FIRST) ? 0x04 : 0x08;
    bytestream_put_byte  (&buf, frame_flags);

    bytestream_put_byte  (&buf, 0);             // reserved
    bytestream_put_byte  (&buf, color_primaries);
    bytestream_put_byte  (&buf, color_trc);
    bytestream_put_byte  (&buf, colorspace);
    bytestream_put_byte  (&buf, ctx->alpha_bits >> 3);
    bytestream_put_byte  (&buf, 0);             // reserved
    if (ctx->quant_sel != QUANT_MAT_DEFAULT) {
        bytestream_put_byte  (&buf, 0x03);      // matrix flags - both matrices are present
        bytestream_put_buffer(&buf, ctx->quant_mat, 64);        // luma quantisation matrix
        bytestream_put_buffer(&buf, ctx->quant_chroma_mat, 64); // chroma quantisation matrix
    } else {
        bytestream_put_byte  (&buf, 0x00);      // matrix flags - default matrices are used
    }
    bytestream_put_be16  (&tmp, buf - *orig_buf); // write back frame header size
    return buf;
}

static int get_packet(AVCodecContext *avctx, FFVkExecContext *exec, AVPacket *pkt)
{
    ProresVulkanContext *pv = avctx->priv_data;
    ProresContext *ctx = &pv->ctx;
    VulkanEncodeProresFrameData *pd = exec->opaque;
    FFVulkanContext *vkctx = &pv->vkctx;
    FFVulkanFunctions *vk = &vkctx->vkfn;
    FFVkExecContext *transfer_exec;
    uint8_t *orig_buf, *buf, *slice_sizes;
    uint8_t *picture_size_pos;
    int picture_idx, err = 0;
    int frame_size, picture_size;
    int pkt_size = ctx->frame_size_upper_bound;
    int transfer_slices = vkctx->extensions & FF_VK_EXT_EXTERNAL_HOST_MEMORY;
    FFVkBuffer *out_data_buf, *frame_size_buf;
    VkMappedMemoryRange invalidate_data;
    AVBufferRef *mapped_ref;
    FFVkBuffer *mapped_buf;

    /* Allocate packet */
    RET(ff_get_encode_buffer(avctx, pkt, pkt_size + FF_INPUT_BUFFER_MIN_SIZE, 0));

    /* Initialize packet. */
    pkt->pts      = pd->pts;
    pkt->dts      = pd->pts;
    pkt->duration = pd->duration;
    pkt->flags   |= AV_PKT_FLAG_KEY * pd->key_frame;

    if (avctx->flags & AV_CODEC_FLAG_COPY_OPAQUE) {
        pkt->opaque          = pd->frame_opaque;
        pkt->opaque_ref      = pd->frame_opaque_ref;
        pd->frame_opaque_ref = NULL;
    }

    /* Write frame atom */
    orig_buf = pkt->data;
    buf = write_frame_header(avctx,  ctx, &orig_buf, pd->flags,
                             pd->color_primaries, pd->color_trc,
                             pd->colorspace);

    /* Make sure encoding's done */
    ff_vk_exec_wait(vkctx, exec);

    /* Roll transfer execution context */
    if (transfer_slices) {
        RET(ff_vk_host_map_buffer(vkctx, &mapped_ref, pkt->data, pkt->buf,
                                  VK_BUFFER_USAGE_TRANSFER_DST_BIT));
        mapped_buf = (FFVkBuffer *)mapped_ref->data;
        transfer_exec = ff_vk_exec_get(vkctx, &pv->transfer_exec_pool);
        ff_vk_exec_start(vkctx, transfer_exec);
    }

    for (picture_idx = 0; picture_idx < ctx->pictures_per_frame; picture_idx++) {
        /* Fetch buffers for the current picture. */
        out_data_buf = (FFVkBuffer *)pd->out_data_ref[picture_idx]->data;
        frame_size_buf = (FFVkBuffer *)pd->frame_size_ref[picture_idx]->data;

        /* Invalidate slice/output data if needed */
        invalidate_data = (VkMappedMemoryRange) {
            .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .offset = 0,
            .size = VK_WHOLE_SIZE,
        };
        if (!(frame_size_buf->flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            invalidate_data.memory = frame_size_buf->mem;
            vk->InvalidateMappedMemoryRanges(vkctx->hwctx->act_dev, 1, &invalidate_data);
        }

        /* Write picture header */
        picture_size_pos = buf + 1;
        bytestream_put_byte(&buf, 0x40);          // picture header size (in bits)
        buf += 4;                                 // picture data size will be stored here
        bytestream_put_be16(&buf, ctx->slices_per_picture);
        bytestream_put_byte(&buf, av_log2(ctx->mbs_per_slice) << 4); // slice width and height in MBs

        /* Skip over seek table */
        slice_sizes = buf;
        buf += ctx->slices_per_picture * 2;

        /* Calculate final size */
        buf += *(int*)frame_size_buf->mapped_mem;

        if (transfer_slices) {
            /* Perform host mapped transfer of slice data */
            ff_vk_exec_add_dep_buf(vkctx, transfer_exec, &pd->out_data_ref[picture_idx], 1, 0);
            ff_vk_exec_add_dep_buf(vkctx, transfer_exec, &mapped_ref, 1, 0);
            vk->CmdCopyBuffer(transfer_exec->buf, out_data_buf->buf, mapped_buf->buf, 1, & (VkBufferCopy) {
                .srcOffset = 0,
                .dstOffset = mapped_buf->virtual_offset + slice_sizes - pkt->data,
                .size = buf - slice_sizes,
            });
        } else {
            /* Fallback to regular memcpy if transfer is not available */
            if (!(out_data_buf->flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
                invalidate_data.memory = out_data_buf->mem;
                vk->InvalidateMappedMemoryRanges(vkctx->hwctx->act_dev, 1, &invalidate_data);
            }
            memcpy(slice_sizes, out_data_buf->mapped_mem, buf - slice_sizes);
            av_buffer_unref(&pd->out_data_ref[picture_idx]);
        }

        /* Write picture size with header */
        picture_size = buf - (picture_size_pos - 1);
        bytestream_put_be32(&picture_size_pos, picture_size);

        /* Slice output buffers no longer needed */
        av_buffer_unref(&pd->slice_data_ref[picture_idx]);
        av_buffer_unref(&pd->slice_score_ref[picture_idx]);
        av_buffer_unref(&pd->frame_size_ref[picture_idx]);
    }

    /* Write frame size in header */
    orig_buf -= 8;
    frame_size = buf - orig_buf;
    bytestream_put_be32(&orig_buf, frame_size);

    av_shrink_packet(pkt, frame_size);
    av_log(avctx, AV_LOG_VERBOSE, "Encoded data: %iMiB\n", pkt->size / (1024*1024));

    /* Wait for slice transfer */
    if (transfer_slices) {
        RET(ff_vk_exec_submit(vkctx, transfer_exec));
        ff_vk_exec_wait(vkctx, transfer_exec);
    }

fail:
    return err;
}

static int vulkan_encode_prores_receive_packet(AVCodecContext *avctx, AVPacket *pkt)
{
    int err;
    ProresVulkanContext *pv = avctx->priv_data;
    ProresContext *ctx = &pv->ctx;
    VulkanEncodeProresFrameData *pd;
    FFVkExecContext *exec;
    AVFrame *frame;

    while (1) {
        /* Roll an execution context */
        exec = ff_vk_exec_get(&pv->vkctx, &pv->e);

        /* If it had a frame, immediately output it */
        if (exec->had_submission) {
            exec->had_submission = 0;
            pv->in_flight--;
            return get_packet(avctx, exec, pkt);
        }

        /* Get next frame to encode */
        frame = pv->frame;
        err = ff_encode_get_frame(avctx, frame);
        if (err < 0 && err != AVERROR_EOF) {
            return err;
        } else if (err == AVERROR_EOF) {
            if (!pv->in_flight)
                return err;
            continue;
        }

        /* Encode frame */
        pd = exec->opaque;
        pd->color_primaries = frame->color_primaries;
        pd->color_trc = frame->color_trc;
        pd->colorspace = frame->colorspace;
        pd->pts = frame->pts;
        pd->duration = frame->duration;
        pd->flags = frame->flags;
        if (avctx->flags & AV_CODEC_FLAG_COPY_OPAQUE) {
            pd->frame_opaque     = frame->opaque;
            pd->frame_opaque_ref = frame->opaque_ref;
            frame->opaque_ref    = NULL;
        }

        err = vulkan_encode_prores_submit_frame(avctx, exec, frame, 0);
        if (ctx->pictures_per_frame > 1)
            vulkan_encode_prores_submit_frame(avctx, exec, frame, 1);

        /* Submit execution context */
        ff_vk_exec_submit(&pv->vkctx, exec);
        av_frame_unref(frame);
        if (err < 0)
            return err;

        pv->in_flight++;
        if (pv->in_flight < pv->async_depth)
            return AVERROR(EAGAIN);
    }

    return 0;
}

static av_cold int encode_close(AVCodecContext *avctx)
{
    ProresVulkanContext *pv = avctx->priv_data;
    ProresContext *ctx = &pv->ctx;
    FFVulkanContext *vkctx = &pv->vkctx;

    ff_vk_exec_pool_free(vkctx, &pv->e);
    ff_vk_exec_pool_free(vkctx, &pv->transfer_exec_pool);

    if (ctx->alpha_bits)
        ff_vk_shader_free(vkctx, &pv->alpha_data_shd);

    ff_vk_shader_free(vkctx, &pv->slice_data_shd[0]);
    ff_vk_shader_free(vkctx, &pv->slice_data_shd[1]);
    ff_vk_shader_free(vkctx, &pv->estimate_slice_shd);
    ff_vk_shader_free(vkctx, &pv->encode_slice_shd);
    ff_vk_shader_free(vkctx, &pv->trellis_node_shd);

    ff_vk_free_buf(vkctx, &pv->prores_data_tables_buf);

    av_buffer_pool_uninit(&pv->pkt_buf_pool);
    av_buffer_pool_uninit(&pv->slice_data_buf_pool);
    av_buffer_pool_uninit(&pv->slice_score_buf_pool);
    av_buffer_pool_uninit(&pv->frame_size_buf_pool);

    ff_vk_uninit(vkctx);

    return 0;
}

static av_cold int encode_init(AVCodecContext *avctx)
{
    ProresVulkanContext *pv = avctx->priv_data;
    ProresContext *ctx = &pv->ctx;
    int err = 0, i, q;
    FFVulkanContext *vkctx = &pv->vkctx;

    /* Init vulkan */
    RET(ff_vk_init(vkctx, avctx, NULL, avctx->hw_frames_ctx));

    pv->qf = ff_vk_qf_find(vkctx, VK_QUEUE_COMPUTE_BIT, 0);
    if (!pv->qf) {
        av_log(avctx, AV_LOG_ERROR, "Device has no compute queues!\n");
        return AVERROR(ENOTSUP);
    }

    RET(ff_vk_exec_pool_init(vkctx, pv->qf, &pv->e, 1, 0, 0, 0, NULL));

    pv->transfer_qf = ff_vk_qf_find(vkctx, VK_QUEUE_TRANSFER_BIT, 0);
    if (!pv->transfer_qf) {
        av_log(avctx, AV_LOG_ERROR, "Device has no transfer queues!\n");
        return err;
    }

    RET(ff_vk_exec_pool_init(vkctx, pv->transfer_qf, &pv->transfer_exec_pool,
                             pv->async_depth, 0, 0, 0, NULL));

    /* Init common prores structures */
    err = ff_prores_kostya_encode_init(avctx, ctx, vkctx->frames->sw_format);
    if (err < 0)
        return err;

    /* Temporary frame */
    pv->frame = av_frame_alloc();
    if (!pv->frame)
        return AVERROR(ENOMEM);

    /* Async data pool */
    pv->async_depth = pv->e.pool_size;
    pv->exec_ctx_info = av_calloc(pv->async_depth, sizeof(*pv->exec_ctx_info));
    if (!pv->exec_ctx_info)
        return AVERROR(ENOMEM);
    for (int i = 0; i < pv->async_depth; i++)
        pv->e.contexts[i].opaque = &pv->exec_ctx_info[i];

    /* Compile shaders used by encoder */
    init_slice_data_pipeline(pv, &pv->slice_data_shd[0], 2);
    init_slice_data_pipeline(pv, &pv->slice_data_shd[1], 4);
    init_estimate_slice_pipeline(pv, &pv->estimate_slice_shd);
    init_trellis_node_pipeline(pv, &pv->trellis_node_shd);
    init_encode_slice_pipeline(pv, &pv->encode_slice_shd);

    if (ctx->alpha_bits)
        init_alpha_data_pipeline(pv, &pv->alpha_data_shd);

    /* Create prores data tables uniform buffer. */
    RET(ff_vk_create_buf(vkctx, &pv->prores_data_tables_buf,
                         sizeof(ProresDataTables), NULL, NULL,
                         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                         VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT));
    RET(ff_vk_map_buffer(vkctx, &pv->prores_data_tables_buf, (void *)&pv->tables, 0));
    for (q = 0; q < MAX_STORED_Q; ++q) {
        for (i = 0; i < 64; i++) {
            pv->tables->qmat[q][i] = ctx->quants[q][ctx->scantable[i]];
            pv->tables->qmat_chroma[q][i] = ctx->quants_chroma[q][ctx->scantable[i]];
        }
    }
    for (q = MAX_STORED_Q; q < 128; ++q) {
        for (i = 0; i < 64; i++) {
            pv->tables->qmat[q][i] = ctx->quant_mat[ctx->scantable[i]] * q;
            pv->tables->qmat_chroma[q][i] = ctx->quant_chroma_mat[ctx->scantable[i]] * q;
        }
    }

fail:
    return err;
}

#define OFFSET(x) offsetof(ProresVulkanContext, x)
#define VE     AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM

static const AVOption options[] = {
    { "mbs_per_slice", "macroblocks per slice", OFFSET(ctx.mbs_per_slice),
        AV_OPT_TYPE_INT, { .i64 = 8 }, 1, MAX_MBS_PER_SLICE, VE },
    { "profile",       NULL, OFFSET(ctx.profile), AV_OPT_TYPE_INT,
        { .i64 = PRORES_PROFILE_AUTO },
        PRORES_PROFILE_AUTO, PRORES_PROFILE_4444XQ, VE, .unit = "profile" },
    { "auto",         NULL, 0, AV_OPT_TYPE_CONST, { .i64 = PRORES_PROFILE_AUTO },
        0, 0, VE, .unit = "profile" },
    { "proxy",         NULL, 0, AV_OPT_TYPE_CONST, { .i64 = PRORES_PROFILE_PROXY },
        0, 0, VE, .unit = "profile" },
    { "lt",            NULL, 0, AV_OPT_TYPE_CONST, { .i64 = PRORES_PROFILE_LT },
        0, 0, VE, .unit = "profile" },
    { "standard",      NULL, 0, AV_OPT_TYPE_CONST, { .i64 = PRORES_PROFILE_STANDARD },
        0, 0, VE, .unit = "profile" },
    { "hq",            NULL, 0, AV_OPT_TYPE_CONST, { .i64 = PRORES_PROFILE_HQ },
        0, 0, VE, .unit = "profile" },
    { "4444",          NULL, 0, AV_OPT_TYPE_CONST, { .i64 = PRORES_PROFILE_4444 },
        0, 0, VE, .unit = "profile" },
    { "4444xq",        NULL, 0, AV_OPT_TYPE_CONST, { .i64 = PRORES_PROFILE_4444XQ },
        0, 0, VE, .unit = "profile" },
    { "vendor", "vendor ID", OFFSET(ctx.vendor),
        AV_OPT_TYPE_STRING, { .str = "Lavc" }, 0, 0, VE },
    { "bits_per_mb", "desired bits per macroblock", OFFSET(ctx.bits_per_mb),
        AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 8192, VE },
    { "quant_mat", "quantiser matrix", OFFSET(ctx.quant_sel), AV_OPT_TYPE_INT,
        { .i64 = -1 }, -1, QUANT_MAT_DEFAULT, VE, .unit = "quant_mat" },
    { "auto",          NULL, 0, AV_OPT_TYPE_CONST, { .i64 = -1 },
        0, 0, VE, .unit = "quant_mat" },
    { "proxy",         NULL, 0, AV_OPT_TYPE_CONST, { .i64 = QUANT_MAT_PROXY },
        0, 0, VE, .unit = "quant_mat" },
    { "lt",            NULL, 0, AV_OPT_TYPE_CONST, { .i64 = QUANT_MAT_LT },
        0, 0, VE, .unit = "quant_mat" },
    { "standard",      NULL, 0, AV_OPT_TYPE_CONST, { .i64 = QUANT_MAT_STANDARD },
        0, 0, VE, .unit = "quant_mat" },
    { "hq",            NULL, 0, AV_OPT_TYPE_CONST, { .i64 = QUANT_MAT_HQ },
        0, 0, VE, .unit = "quant_mat" },
    { "default",       NULL, 0, AV_OPT_TYPE_CONST, { .i64 = QUANT_MAT_DEFAULT },
        0, 0, VE, .unit = "quant_mat" },
    { "alpha_bits", "bits for alpha plane", OFFSET(ctx.alpha_bits), AV_OPT_TYPE_INT,
        { .i64 = 16 }, 0, 16, VE },
    { "async_depth", "Internal parallelization depth", OFFSET(async_depth), AV_OPT_TYPE_INT,
            { .i64 = 1 }, 1, INT_MAX, VE },
    { NULL }
};

static const AVClass proresenc_class = {
    .class_name = "ProRes vulkan encoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVCodecHWConfigInternal *const prores_ks_hw_configs[] = {
    HW_CONFIG_ENCODER_FRAMES(VULKAN, VULKAN),
    HW_CONFIG_ENCODER_DEVICE(NONE,  VULKAN),
    NULL,
};

const FFCodec ff_prores_ks_vulkan_encoder = {
    .p.name         = "prores_ks_vulkan",
    CODEC_LONG_NAME("Apple ProRes (iCodec Pro)"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_PRORES,
    .priv_data_size = sizeof(ProresVulkanContext),
    .init           = encode_init,
    .close          = encode_close,
    FF_CODEC_RECEIVE_PACKET_CB(&vulkan_encode_prores_receive_packet),
    .p.capabilities = AV_CODEC_CAP_DELAY |
                      AV_CODEC_CAP_HARDWARE |
                      AV_CODEC_CAP_ENCODER_FLUSH |
                      AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    CODEC_PIXFMTS(AV_PIX_FMT_VULKAN),
    .hw_configs     = prores_ks_hw_configs,
    .color_ranges   = AVCOL_RANGE_MPEG,
    .p.priv_class   = &proresenc_class,
    .p.profiles     = NULL_IF_CONFIG_SMALL(ff_prores_profiles),
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP | FF_CODEC_CAP_EOF_FLUSH,
};
