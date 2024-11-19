/*
 * Copyright (c) 2024 Lynne <dev@lynne.ee>
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

#include "libavutil/crc.h"
#include "libavutil/vulkan.h"
#include "libavutil/vulkan_spirv.h"

#include "avcodec.h"
#include "internal.h"
#include "hwconfig.h"
#include "encode.h"
#include "libavutil/opt.h"
#include "codec_internal.h"

#include "ffv1.h"
#include "ffv1enc.h"

/* Parallel Golomb alignment */
#define LG_ALIGN_W 32
#define LG_ALIGN_H 32

typedef struct VulkanEncodeFFv1Context {
    FFV1Context ctx;

    FFVulkanContext s;
    FFVkQueueFamilyCtx qf;
    FFVkExecPool exec_pool;

    FFVulkanShader setup;
    FFVulkanShader reset;
    FFVulkanShader rct;
    FFVulkanShader enc;

    /* Constant read-only buffers */
    FFVkBuffer quant_buf;
    FFVkBuffer rangecoder_static_buf;
    FFVkBuffer crc_tab_buf;

    /* Slice data buffer pool */
    AVBufferPool *slice_data_pool;
    AVBufferRef *keyframe_slice_data_ref;

    /* Output data buffer */
    AVBufferPool *out_data_pool;

    /* Temporary data buffer */
    AVBufferPool *tmp_data_pool;

    /* Slice results buffer */
    AVBufferPool *results_data_pool;

    /* Intermediate frame pool */
    AVBufferRef *intermediate_frames_ref;

    /* Representation mode */
    enum FFVkShaderRepFormat rep_fmt;

    int num_h_slices;
    int num_v_slices;
    int force_pcm;

    int is_rgb;
    int ppi;
    int chunks;
} VulkanEncodeFFv1Context;

extern const char *ff_source_common_comp;
extern const char *ff_source_rangecoder_comp;
extern const char *ff_source_ffv1_vlc_comp;
extern const char *ff_source_ffv1_common_comp;
extern const char *ff_source_ffv1_reset_comp;
extern const char *ff_source_ffv1_enc_common_comp;
extern const char *ff_source_ffv1_enc_rct_comp;
extern const char *ff_source_ffv1_enc_vlc_comp;
extern const char *ff_source_ffv1_enc_ac_comp;
extern const char *ff_source_ffv1_enc_setup_comp;
extern const char *ff_source_ffv1_enc_comp;
extern const char *ff_source_ffv1_enc_rgb_comp;

typedef struct FFv1VkRCTParameters {
    int offset;
    uint8_t planar_rgb;
    uint8_t transparency;
    uint8_t padding[2];
} FFv1VkRCTParameters;

typedef struct FFv1VkResetParameters {
    VkDeviceAddress slice_state;
    uint32_t plane_state_size;
    uint32_t context_count;
    uint8_t codec_planes;
    uint8_t key_frame;
    uint8_t padding[3];
} FFv1VkResetParameters;

typedef struct FFv1VkParameters {
    VkDeviceAddress slice_state;
    VkDeviceAddress scratch_data;
    VkDeviceAddress out_data;
    uint64_t slice_size_max;

    int32_t sar[2];
    uint32_t chroma_shift[2];

    uint32_t plane_state_size;
    uint32_t context_count;
    uint32_t crcref;

    uint8_t bits_per_raw_sample;
    uint8_t context_model;
    uint8_t version;
    uint8_t micro_version;
    uint8_t force_pcm;
    uint8_t key_frame;
    uint8_t planes;
    uint8_t codec_planes;
    uint8_t transparency;
    uint8_t colorspace;
    uint8_t pic_mode;
    uint8_t ec;
    uint8_t ppi;
    uint8_t chunks;
    uint8_t padding[2];
} FFv1VkParameters;

static void add_push_data(FFVulkanShader *shd)
{
    GLSLC(0, layout(push_constant, scalar) uniform pushConstants {            );
    GLSLC(1,    u8buf slice_state;                                            );
    GLSLC(1,    u8buf scratch_data;                                           );
    GLSLC(1,    u8buf out_data;                                               );
    GLSLC(1,    uint64_t slice_size_max;                                      );
    GLSLC(0,                                                                  );
    GLSLC(1,    ivec2 sar;                                                    );
    GLSLC(1,    uvec2 chroma_shift;                                           );
    GLSLC(0,                                                                  );
    GLSLC(1,    uint plane_state_size;                                        );
    GLSLC(1,    uint context_count;                                           );
    GLSLC(1,    uint32_t crcref;                                              );
    GLSLC(0,                                                                  );
    GLSLC(1,    uint8_t bits_per_raw_sample;                                  );
    GLSLC(1,    uint8_t context_model;                                        );
    GLSLC(1,    uint8_t version;                                              );
    GLSLC(1,    uint8_t micro_version;                                        );
    GLSLC(1,    uint8_t force_pcm;                                            );
    GLSLC(1,    uint8_t key_frame;                                            );
    GLSLC(1,    uint8_t planes;                                               );
    GLSLC(1,    uint8_t codec_planes;                                         );
    GLSLC(1,    uint8_t transparency;                                         );
    GLSLC(1,    uint8_t colorspace;                                           );
    GLSLC(1,    uint8_t pic_mode;                                             );
    GLSLC(1,    uint8_t ec;                                                   );
    GLSLC(1,    uint8_t ppi;                                                  );
    GLSLC(1,    uint8_t chunks;                                               );
    GLSLC(1,    uint8_t padding[2];                                           );
    GLSLC(0, };                                                               );
    ff_vk_shader_add_push_const(shd, 0, sizeof(FFv1VkParameters),
                                VK_SHADER_STAGE_COMPUTE_BIT);
}

static int run_rct(AVCodecContext *avctx, FFVkExecContext *exec,
                   AVFrame *enc_in, VkImageView *enc_in_views,
                   AVFrame **intermediate_frame, VkImageView *intermediate_views,
                   VkImageMemoryBarrier2 *img_bar, int *nb_img_bar,
                   VkBufferMemoryBarrier2 *buf_bar, int *nb_buf_bar,
                   FFVkBuffer *slice_data_buf, uint32_t slice_data_size)
{
    int err;
    VulkanEncodeFFv1Context *fv = avctx->priv_data;
    FFV1Context *f = &fv->ctx;
    FFVulkanFunctions *vk = &fv->s.vkfn;
    AVHWFramesContext *src_hwfc = (AVHWFramesContext *)enc_in->hw_frames_ctx->data;
    FFv1VkRCTParameters pd;

    /* Create a temporaty frame */
    *intermediate_frame = av_frame_alloc();
    if (!(*intermediate_frame))
        return AVERROR(ENOMEM);

    RET(av_hwframe_get_buffer(fv->intermediate_frames_ref,
                              *intermediate_frame, 0));

    RET(ff_vk_exec_add_dep_frame(&fv->s, exec, *intermediate_frame,
                                 VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT));
    RET(ff_vk_create_imageviews(&fv->s, exec, intermediate_views,
                                *intermediate_frame,
                                fv->rep_fmt));

    /* Update descriptors */
    ff_vk_shader_update_desc_buffer(&fv->s, exec, &fv->rct,
                                    1, 0, 0,
                                    slice_data_buf,
                                    0, slice_data_size*f->slice_count,
                                    VK_FORMAT_UNDEFINED);
    ff_vk_shader_update_img_array(&fv->s, exec, &fv->rct,
                                  enc_in, enc_in_views,
                                  1, 1,
                                  VK_IMAGE_LAYOUT_GENERAL,
                                  VK_NULL_HANDLE);
    ff_vk_shader_update_img_array(&fv->s, exec, &fv->rct,
                                  *intermediate_frame, intermediate_views,
                                  1, 2,
                                  VK_IMAGE_LAYOUT_GENERAL,
                                  VK_NULL_HANDLE);

    ff_vk_frame_barrier(&fv->s, exec, *intermediate_frame, img_bar, nb_img_bar,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_SHADER_WRITE_BIT,
                        VK_IMAGE_LAYOUT_GENERAL,
                        VK_QUEUE_FAMILY_IGNORED);

    /* Prep the input/output images */
    vk->CmdPipelineBarrier2(exec->buf, &(VkDependencyInfo) {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pImageMemoryBarriers = img_bar,
            .imageMemoryBarrierCount = *nb_img_bar,
            .pBufferMemoryBarriers = buf_bar,
            .bufferMemoryBarrierCount = *nb_buf_bar,
    });
    *nb_img_bar = 0;
    if (*nb_buf_bar) {
        slice_data_buf->stage = buf_bar[0].dstStageMask;
        slice_data_buf->access = buf_bar[0].dstAccessMask;
        *nb_buf_bar = 0;
    }

    /* Run the shader */
    ff_vk_exec_bind_shader(&fv->s, exec, &fv->rct);
    pd = (FFv1VkRCTParameters) {
        .offset = 1 << f->bits_per_raw_sample,
        .planar_rgb = ff_vk_mt_is_np_rgb(src_hwfc->sw_format) &&
                      (ff_vk_count_images((AVVkFrame *)enc_in->data[0]) > 1),
        .transparency = f->transparency,
    };
    ff_vk_shader_update_push_const(&fv->s, exec, &fv->rct,
                                   VK_SHADER_STAGE_COMPUTE_BIT,
                                   0, sizeof(pd), &pd);

    vk->CmdDispatch(exec->buf, fv->ctx.num_h_slices, fv->ctx.num_v_slices, 1);

    /* Add a post-dispatch barrier before encoding */
    ff_vk_frame_barrier(&fv->s, exec, *intermediate_frame, img_bar, nb_img_bar,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_SHADER_READ_BIT,
                        VK_IMAGE_LAYOUT_GENERAL,
                        VK_QUEUE_FAMILY_IGNORED);

fail:
    return err;
}

static int vulkan_encode_ffv1_frame(AVCodecContext *avctx, AVPacket *pkt,
                                    const AVFrame *pict, int *got_packet)
{
    int err;
    VulkanEncodeFFv1Context *fv = avctx->priv_data;
    FFV1Context *f = &fv->ctx;
    FFVulkanFunctions *vk = &fv->s.vkfn;
    FFVkExecContext *exec;

    FFv1VkParameters pd;

    AVFrame *intermediate_frame = NULL;

    /* Temporary data */
    size_t tmp_data_size;
    AVBufferRef *tmp_data_ref;
    FFVkBuffer *tmp_data_buf;

    /* Slice data */
    AVBufferRef *slice_data_ref;
    FFVkBuffer *slice_data_buf;
    uint32_t plane_state_size;
    uint32_t slice_state_size;
    uint32_t slice_data_size;

    /* Output data */
    size_t maxsize;
    AVBufferRef *out_data_ref;
    FFVkBuffer *out_data_buf;
    uint8_t *buf_p;

    /* Results data */
    AVBufferRef *results_data_ref;
    FFVkBuffer *results_data_buf;
    uint64_t *sc;

    int has_inter = avctx->gop_size > 1;
    uint32_t context_count = f->context_count[f->context_model];

    VkImageView in_views[AV_NUM_DATA_POINTERS];
    VkImageView intermediate_views[AV_NUM_DATA_POINTERS];

    AVFrame *enc_in = (AVFrame *)pict;
    VkImageView *enc_in_views = in_views;

    VkMappedMemoryRange invalidate_data[2];
    int nb_invalidate_data = 0;

    VkImageMemoryBarrier2 img_bar[37];
    int nb_img_bar = 0;
    VkBufferMemoryBarrier2 buf_bar[8];
    int nb_buf_bar = 0;

    if (!pict)
        return 0;

    exec = ff_vk_exec_get(&fv->s, &fv->exec_pool);
    ff_vk_exec_start(&fv->s, exec);

    /* Frame state */
    f->cur_enc_frame = pict;
    if (avctx->gop_size == 0 || f->picture_number % avctx->gop_size == 0) {
        av_buffer_unref(&fv->keyframe_slice_data_ref);
        f->key_frame = 1;
        f->gob_count++;
    } else {
        f->key_frame = 0;
    }

    f->max_slice_count = f->num_h_slices * f->num_v_slices;
    f->slice_count = f->max_slice_count;

    /* Allocate temporary data buffer */
    tmp_data_size = f->slice_count*CONTEXT_SIZE;
    err = ff_vk_get_pooled_buffer(&fv->s, &fv->tmp_data_pool,
                                  &tmp_data_ref,
                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                  VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                  NULL, tmp_data_size,
                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (err < 0)
        return err;
    tmp_data_buf = (FFVkBuffer *)tmp_data_ref->data;

    /* Allocate slice buffer data */
    if (f->ac == AC_GOLOMB_RICE)
        plane_state_size = 8;
    else
        plane_state_size = CONTEXT_SIZE;

    plane_state_size *= context_count;
    slice_state_size = plane_state_size*f->plane_count;

    slice_data_size = 256; /* Overestimation for the SliceContext struct */
    slice_state_size += slice_data_size;
    slice_state_size = FFALIGN(slice_state_size, 8);

    slice_data_ref = fv->keyframe_slice_data_ref;
    if (!slice_data_ref) {
        /* Allocate slice data buffer */
        err = ff_vk_get_pooled_buffer(&fv->s, &fv->slice_data_pool,
                                      &slice_data_ref,
                                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                      NULL, slice_state_size*f->slice_count,
                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (err < 0)
            return err;

        /* Only save it if we're going to use it again */
        if (has_inter)
            fv->keyframe_slice_data_ref = slice_data_ref;
    }
    slice_data_buf = (FFVkBuffer *)slice_data_ref->data;

    /* Allocate results buffer */
    err = ff_vk_get_pooled_buffer(&fv->s, &fv->results_data_pool,
                                  &results_data_ref,
                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                  VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                  NULL, 2*f->slice_count*sizeof(uint64_t),
                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    if (err < 0)
        return err;
    results_data_buf = (FFVkBuffer *)results_data_ref->data;

    /* Output buffer size */
    maxsize = avctx->width*avctx->height*(1 + f->transparency);
    if (f->chroma_planes)
        maxsize += AV_CEIL_RSHIFT(avctx->width, f->chroma_h_shift) *
                   AV_CEIL_RSHIFT(f->height, f->chroma_v_shift)*2;
    maxsize += f->slice_count * 800;
    if (f->version > 3) {
        maxsize *= f->bits_per_raw_sample + 1;
    } else {
        maxsize += f->slice_count * 2 * (avctx->width + avctx->height);
        maxsize *= 8*(2*f->bits_per_raw_sample + 5);
    }
    maxsize >>= 3;
    maxsize += FF_INPUT_BUFFER_MIN_SIZE;

    /* Allocate output buffer */
    err = ff_vk_get_pooled_buffer(&fv->s, &fv->out_data_pool,
                                  &out_data_ref,
                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                  VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                  NULL, maxsize,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                  VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
    if (err < 0)
        return err;

    out_data_buf = (FFVkBuffer *)out_data_ref->data;
    pkt->data = out_data_buf->mapped_mem;
    pkt->size = out_data_buf->size;
    pkt->buf = out_data_ref;

    /* Add dependencies */
    ff_vk_exec_add_dep_buf(&fv->s, exec, &tmp_data_ref, 1, 0);
    ff_vk_exec_add_dep_buf(&fv->s, exec, &results_data_ref, 1, 0);
    ff_vk_exec_add_dep_buf(&fv->s, exec, &slice_data_ref, 1, has_inter);
    ff_vk_exec_add_dep_buf(&fv->s, exec, &out_data_ref, 1, 1);
    RET(ff_vk_exec_add_dep_frame(&fv->s, exec, enc_in,
                                 VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT));

    RET(ff_vk_create_imageviews(&fv->s, exec, enc_in_views, enc_in,
                                fv->rep_fmt));
    ff_vk_frame_barrier(&fv->s, exec, enc_in, img_bar, &nb_img_bar,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_SHADER_READ_BIT,
                        VK_IMAGE_LAYOUT_GENERAL,
                        VK_QUEUE_FAMILY_IGNORED);

    /* Setup shader needs the original input */
    ff_vk_shader_update_desc_buffer(&fv->s, exec, &fv->setup,
                                    1, 0, 0,
                                    slice_data_buf,
                                    0, slice_data_size*f->slice_count,
                                    VK_FORMAT_UNDEFINED);
    ff_vk_shader_update_img_array(&fv->s, exec, &fv->setup,
                                  enc_in, enc_in_views,
                                  1, 1,
                                  VK_IMAGE_LAYOUT_GENERAL,
                                  VK_NULL_HANDLE);

    /* Add a buffer barrier between previous and current frame */
    if (!f->key_frame) {
        buf_bar[nb_buf_bar++] = (VkBufferMemoryBarrier2) {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = slice_data_buf->stage,
            .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .srcAccessMask = slice_data_buf->access,
            .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                             VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = slice_data_buf->buf,
            .size = VK_WHOLE_SIZE,
            .offset = 0,
        };
    }

    vk->CmdPipelineBarrier2(exec->buf, &(VkDependencyInfo) {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pImageMemoryBarriers = img_bar,
        .imageMemoryBarrierCount = nb_img_bar,
        .pBufferMemoryBarriers = buf_bar,
        .bufferMemoryBarrierCount = nb_buf_bar,
    });
    nb_img_bar = 0;
    if (nb_buf_bar) {
        slice_data_buf->stage = buf_bar[0].dstStageMask;
        slice_data_buf->access = buf_bar[0].dstAccessMask;
        nb_buf_bar = 0;
    }

    /* Run setup shader */
    ff_vk_exec_bind_shader(&fv->s, exec, &fv->setup);
    pd = (FFv1VkParameters) {
        .slice_state = slice_data_buf->address + f->slice_count*256,
        .scratch_data = tmp_data_buf->address,
        .out_data = out_data_buf->address,
        .slice_size_max = out_data_buf->size / f->slice_count,
        .bits_per_raw_sample = f->bits_per_raw_sample,
        .sar[0] = pict->sample_aspect_ratio.num,
        .sar[1] = pict->sample_aspect_ratio.den,
        .chroma_shift[0] = f->chroma_h_shift,
        .chroma_shift[1] = f->chroma_v_shift,
        .plane_state_size = plane_state_size,
        .context_count = context_count,
        .crcref = f->crcref,
        .context_model = fv->ctx.context_model,
        .version = f->version,
        .micro_version = f->micro_version,
        .force_pcm = fv->force_pcm,
        .key_frame = f->key_frame,
        .planes = av_pix_fmt_count_planes(avctx->sw_pix_fmt),
        .codec_planes = f->plane_count,
        .transparency = f->transparency,
        .colorspace = f->colorspace,
        .pic_mode = !(pict->flags & AV_FRAME_FLAG_INTERLACED) ? 3 :
                    !(pict->flags & AV_FRAME_FLAG_TOP_FIELD_FIRST) ? 2 : 1,
        .ec = f->ec,
        .ppi = fv->ppi,
        .chunks = fv->chunks,
    };
    ff_vk_shader_update_push_const(&fv->s, exec, &fv->setup,
                                   VK_SHADER_STAGE_COMPUTE_BIT,
                                   0, sizeof(pd), &pd);
    vk->CmdDispatch(exec->buf, fv->ctx.num_h_slices, fv->ctx.num_v_slices, 1);

    /* Setup shader modified the slice data buffer */
    buf_bar[nb_buf_bar++] = (VkBufferMemoryBarrier2) {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .srcStageMask = slice_data_buf->stage,
        .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .srcAccessMask = slice_data_buf->access,
        .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                         VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = slice_data_buf->buf,
        .size = slice_data_size*f->slice_count,
        .offset = 0,
    };

    if (f->key_frame || f->version > 3) {
        FFv1VkResetParameters pd_reset;

        ff_vk_shader_update_desc_buffer(&fv->s, exec, &fv->reset,
                                        1, 0, 0,
                                        slice_data_buf,
                                        0, slice_data_size*f->slice_count,
                                        VK_FORMAT_UNDEFINED);

        /* Run setup shader */
        ff_vk_exec_bind_shader(&fv->s, exec, &fv->reset);
        pd_reset = (FFv1VkResetParameters) {
            .slice_state = slice_data_buf->address + f->slice_count*256,
            .plane_state_size = plane_state_size,
            .context_count = context_count,
            .codec_planes = f->plane_count,
            .key_frame = f->key_frame,
        };
        ff_vk_shader_update_push_const(&fv->s, exec, &fv->reset,
                                       VK_SHADER_STAGE_COMPUTE_BIT,
                                       0, sizeof(pd_reset), &pd_reset);

        /* Sync between setup and reset shaders */
        vk->CmdPipelineBarrier2(exec->buf, &(VkDependencyInfo) {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pBufferMemoryBarriers = buf_bar,
            .bufferMemoryBarrierCount = nb_buf_bar,
        });
        slice_data_buf->stage = buf_bar[0].dstStageMask;
        slice_data_buf->access = buf_bar[0].dstAccessMask;
        nb_buf_bar = 0;

        vk->CmdDispatch(exec->buf, fv->ctx.num_h_slices, fv->ctx.num_v_slices,
                        f->plane_count);
    }

    /* Run RCT shader */
    if (fv->is_rgb) {
        RET(run_rct(avctx, exec,
                    enc_in, enc_in_views,
                    &intermediate_frame, intermediate_views,
                    img_bar, &nb_img_bar, buf_bar, &nb_buf_bar,
                    slice_data_buf, slice_data_size));

        /* Use the new frame */
        enc_in = intermediate_frame;
        enc_in_views = intermediate_views;
    }

    /* If the reset shader ran, insert a barrier now. */
    if (f->key_frame || f->version > 3) {
        /* Reset shader modified the slice data buffer */
        buf_bar[nb_buf_bar++] = (VkBufferMemoryBarrier2) {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = slice_data_buf->stage,
            .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .srcAccessMask = slice_data_buf->access,
            .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                             VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = slice_data_buf->buf,
            .size = slice_data_buf->size - slice_data_size*f->slice_count,
            .offset = slice_data_size*f->slice_count,
        };
    }

    /* Final barrier before encoding */
    vk->CmdPipelineBarrier2(exec->buf, &(VkDependencyInfo) {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pImageMemoryBarriers = img_bar,
        .imageMemoryBarrierCount = nb_img_bar,
        .pBufferMemoryBarriers = buf_bar,
        .bufferMemoryBarrierCount = nb_buf_bar,
    });
    nb_img_bar = 0;
    if (nb_buf_bar) {
        slice_data_buf->stage = buf_bar[0].dstStageMask;
        slice_data_buf->access = buf_bar[0].dstAccessMask;
        nb_buf_bar = 0;
    }

    /* Main encode shader */
    ff_vk_shader_update_desc_buffer(&fv->s, exec, &fv->enc,
                                    1, 0, 0,
                                    slice_data_buf,
                                    0, slice_data_size*f->slice_count,
                                    VK_FORMAT_UNDEFINED);
    ff_vk_shader_update_img_array(&fv->s, exec, &fv->enc,
                                  enc_in, enc_in_views,
                                  1, 1,
                                  VK_IMAGE_LAYOUT_GENERAL,
                                  VK_NULL_HANDLE);
    ff_vk_shader_update_desc_buffer(&fv->s, exec,
                                    &fv->enc, 1, 2, 0,
                                    results_data_buf,
                                    0, results_data_buf->size,
                                    VK_FORMAT_UNDEFINED);

    ff_vk_exec_bind_shader(&fv->s, exec, &fv->enc);
    ff_vk_shader_update_push_const(&fv->s, exec, &fv->enc,
                                   VK_SHADER_STAGE_COMPUTE_BIT,
                                   0, sizeof(pd), &pd);
    vk->CmdDispatch(exec->buf, fv->ctx.num_h_slices, fv->ctx.num_v_slices, 1);

    /* Submit */
    err = ff_vk_exec_submit(&fv->s, exec);
    if (err < 0)
        return err;

    /* We need the encoded data immediately */
    ff_vk_exec_wait(&fv->s, exec);
    av_frame_free(&intermediate_frame);

    /* Invalidate slice/output data if needed */
    if (!(results_data_buf->flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
        invalidate_data[nb_invalidate_data++] = (VkMappedMemoryRange) {
            .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .memory = results_data_buf->mem,
            .offset = 0,
            .size = VK_WHOLE_SIZE,
        };
    if (!(out_data_buf->flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
        invalidate_data[nb_invalidate_data++] = (VkMappedMemoryRange) {
            .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .memory = out_data_buf->mem,
            .offset = 0,
            .size = VK_WHOLE_SIZE,
        };
    if (nb_invalidate_data)
        vk->InvalidateMappedMemoryRanges(fv->s.hwctx->act_dev,
                                         nb_invalidate_data, invalidate_data);

    /* First slice is in-place */
    buf_p = pkt->data;
    sc = &((uint64_t *)results_data_buf->mapped_mem)[0];
    av_log(avctx, AV_LOG_DEBUG, "Slice size = %"PRIu64" (max %i), src offset = %"PRIu64"\n",
           sc[0], pkt->size / f->slice_count, sc[1]);
    av_assert0(sc[0] < pd.slice_size_max);
    av_assert0(sc[0] < (1 << 24));
    buf_p += sc[0];

    /* We have to copy the rest */
    for (int i = 1; i < f->slice_count; i++) {
        uint64_t bytes;
        uint8_t *bs_start;

        sc = &((uint64_t *)results_data_buf->mapped_mem)[i*2];
        bytes = sc[0];
        bs_start = pkt->data + sc[1];

        av_log(avctx, AV_LOG_DEBUG, "Slice %i size = %"PRIu64" (max %"PRIu64"), "
                                    "src offset = %"PRIu64"\n",
               i, bytes, pd.slice_size_max, sc[1]);
        av_assert0(bytes < pd.slice_size_max);
        av_assert0(bytes < (1 << 24));

        memmove(buf_p, bs_start, bytes);

        buf_p += bytes;
    }

    f->picture_number++;
    pkt->size = buf_p - pkt->data;
    pkt->flags |= AV_PKT_FLAG_KEY * f->key_frame;
    *got_packet = 1;

    av_log(avctx, AV_LOG_VERBOSE, "Total data = %i\n",
           pkt->size);

fail:
    /* Frames added as a dep are always referenced, so we only need to
     * clean this up. */
    av_frame_free(&intermediate_frame);

    return 0;
}

static int init_indirect(AVCodecContext *avctx, enum AVPixelFormat sw_format)
{
    int err;
    VulkanEncodeFFv1Context *fv = avctx->priv_data;
    AVHWFramesContext *frames_ctx;
    AVVulkanFramesContext *vk_frames;

    fv->intermediate_frames_ref = av_hwframe_ctx_alloc(fv->s.device_ref);
    if (!fv->intermediate_frames_ref)
        return AVERROR(ENOMEM);

    frames_ctx = (AVHWFramesContext *)fv->intermediate_frames_ref->data;
    frames_ctx->format    = AV_PIX_FMT_VULKAN;
    frames_ctx->sw_format = sw_format;
    frames_ctx->width     = FFALIGN(fv->s.frames->width, 32);
    frames_ctx->height    = FFALIGN(fv->s.frames->height, 32);

    vk_frames = frames_ctx->hwctx;
    vk_frames->tiling    = VK_IMAGE_TILING_OPTIMAL;
    vk_frames->usage     = VK_IMAGE_USAGE_STORAGE_BIT;
    vk_frames->img_flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;

    err = av_hwframe_ctx_init(fv->intermediate_frames_ref);
    if (err < 0) {
        av_log(avctx, AV_LOG_ERROR, "Unable to initialize frame pool with format %s: %s\n",
               av_get_pix_fmt_name(sw_format), av_err2str(err));
        av_buffer_unref(&fv->intermediate_frames_ref);
        return err;
    }

    return 0;
}

static int check_support(AVHWFramesConstraints *constraints,
                         enum AVPixelFormat fmt)
{
    for (int i = 0; constraints->valid_sw_formats[i]; i++) {
        if (constraints->valid_sw_formats[i] == fmt)
            return 1;
    }
    return 0;
}

static enum AVPixelFormat get_supported_rgb_buffer_fmt(AVCodecContext *avctx)
{
    VulkanEncodeFFv1Context *fv = avctx->priv_data;

    enum AVPixelFormat fmt;
    AVHWFramesConstraints *constraints;
    constraints = av_hwdevice_get_hwframe_constraints(fv->s.device_ref,
                                                      NULL);

    /* What we'd like to optimally have */
    fmt = fv->ctx.use32bit ?
          (fv->ctx.transparency ? AV_PIX_FMT_RGBA128 : AV_PIX_FMT_RGB96) :
          (fv->ctx.transparency ? AV_PIX_FMT_RGBA64  : AV_PIX_FMT_RGB48);
    if (check_support(constraints, fmt))
        goto end;

    if (fv->ctx.use32bit) {
        if (check_support(constraints, (fmt = AV_PIX_FMT_RGBA128)))
            goto end;
    } else {
        if (check_support(constraints, (fmt = AV_PIX_FMT_RGBA64)))
            goto end;

        if (!fv->ctx.transparency &&
            check_support(constraints, (fmt = AV_PIX_FMT_RGB96)))
                goto end;

        if (check_support(constraints, (fmt = AV_PIX_FMT_RGBA128)))
            goto end;
    }

    fmt = AV_PIX_FMT_NONE;

end:
    av_hwframe_constraints_free(&constraints);
    return fmt;
}

static void define_shared_code(AVCodecContext *avctx, FFVulkanShader *shd)
{
    VulkanEncodeFFv1Context *fv = avctx->priv_data;
    FFV1Context *f = &fv->ctx;
    int smp_bits = fv->ctx.use32bit ? 32 : 16;

    av_bprintf(&shd->src, "#define CONTEXT_SIZE %i\n"                    ,CONTEXT_SIZE);
    av_bprintf(&shd->src, "#define MAX_QUANT_TABLE_MASK 0x%x\n"          ,MAX_QUANT_TABLE_MASK);

    if (f->ac == AC_GOLOMB_RICE) {
        av_bprintf(&shd->src, "#define PB_UNALIGNED\n"                   );
        av_bprintf(&shd->src, "#define GOLOMB\n"                         );
    }

    GLSLF(0, #define TYPE int%i_t                                        ,smp_bits);
    GLSLF(0, #define VTYPE2 i%ivec2                                      ,smp_bits);
    GLSLF(0, #define VTYPE3 i%ivec3                                      ,smp_bits);
    GLSLD(ff_source_common_comp);
    GLSLD(ff_source_rangecoder_comp);

    if (f->ac == AC_GOLOMB_RICE)
        GLSLD(ff_source_ffv1_vlc_comp);

    GLSLD(ff_source_ffv1_common_comp);
}

static int init_setup_shader(AVCodecContext *avctx, FFVkSPIRVCompiler *spv)
{
    int err;
    VulkanEncodeFFv1Context *fv = avctx->priv_data;
    FFVulkanShader *shd = &fv->setup;
    FFVulkanDescriptorSetBinding *desc_set;

    uint8_t *spv_data;
    size_t spv_len;
    void *spv_opaque = NULL;

    RET(ff_vk_shader_init(&fv->s, shd, "ffv1_setup",
                          VK_SHADER_STAGE_COMPUTE_BIT,
                          (const char *[]) { "GL_EXT_buffer_reference",
                                             "GL_EXT_buffer_reference2" }, 2,
                          1, 1, 1,
                          0));

    av_bprintf(&shd->src, "#define MAX_QUANT_TABLES %i\n", MAX_QUANT_TABLES);
    av_bprintf(&shd->src, "#define MAX_CONTEXT_INPUTS %i\n", MAX_CONTEXT_INPUTS);
    av_bprintf(&shd->src, "#define MAX_QUANT_TABLE_SIZE %i\n", MAX_QUANT_TABLE_SIZE);

    desc_set = (FFVulkanDescriptorSetBinding []) {
        {
            .name        = "rangecoder_static_buf",
            .type        = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .mem_layout  = "scalar",
            .buf_content = "uint8_t zero_one_state[512];",
        },
        { /* This descriptor is never used */
            .name        = "quant_buf",
            .type        = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .mem_layout  = "scalar",
            .buf_content = "int16_t quant_table[MAX_QUANT_TABLES]"
                           "[MAX_CONTEXT_INPUTS][MAX_QUANT_TABLE_SIZE];",
        },
    };
    RET(ff_vk_shader_add_descriptor_set(&fv->s, shd, desc_set, 2, 1, 0));

    define_shared_code(avctx, shd);

    desc_set = (FFVulkanDescriptorSetBinding []) {
        {
            .name        = "slice_data_buf",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .buf_content = "SliceContext slice_ctx[1024];",
        },
        {
            .name       = "src",
            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .dimensions = 2,
            .mem_layout = ff_vk_shader_rep_fmt(fv->s.frames->sw_format,
                                               fv->rep_fmt),
            .elems      = av_pix_fmt_count_planes(fv->s.frames->sw_format),
            .mem_quali  = "readonly",
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };
    RET(ff_vk_shader_add_descriptor_set(&fv->s, shd, desc_set, 2, 0, 0));

    add_push_data(shd);

    GLSLD(ff_source_ffv1_enc_setup_comp);

    RET(spv->compile_shader(&fv->s, spv, shd, &spv_data, &spv_len, "main",
                            &spv_opaque));
    RET(ff_vk_shader_link(&fv->s, shd, spv_data, spv_len, "main"));

    RET(ff_vk_shader_register_exec(&fv->s, &fv->exec_pool, shd));

fail:
    if (spv_opaque)
        spv->free_shader(spv, &spv_opaque);

    return err;
}

static int init_reset_shader(AVCodecContext *avctx, FFVkSPIRVCompiler *spv)
{
    int err;
    VulkanEncodeFFv1Context *fv = avctx->priv_data;
    FFVulkanShader *shd = &fv->reset;
    FFVulkanDescriptorSetBinding *desc_set;

    uint8_t *spv_data;
    size_t spv_len;
    void *spv_opaque = NULL;
    int wg_dim = FFMIN(fv->s.props.properties.limits.maxComputeWorkGroupSize[0], 1024);

    RET(ff_vk_shader_init(&fv->s, shd, "ffv1_reset",
                          VK_SHADER_STAGE_COMPUTE_BIT,
                          (const char *[]) { "GL_EXT_buffer_reference",
                                             "GL_EXT_buffer_reference2" }, 2,
                          wg_dim, 1, 1,
                          0));

    av_bprintf(&shd->src, "#define MAX_QUANT_TABLES %i\n", MAX_QUANT_TABLES);
    av_bprintf(&shd->src, "#define MAX_CONTEXT_INPUTS %i\n", MAX_CONTEXT_INPUTS);
    av_bprintf(&shd->src, "#define MAX_QUANT_TABLE_SIZE %i\n", MAX_QUANT_TABLE_SIZE);

    desc_set = (FFVulkanDescriptorSetBinding []) {
        {
            .name        = "rangecoder_static_buf",
            .type        = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .mem_layout  = "scalar",
            .buf_content = "uint8_t zero_one_state[512];",
        },
        {
            .name        = "quant_buf",
            .type        = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .mem_layout  = "scalar",
            .buf_content = "int16_t quant_table[MAX_QUANT_TABLES]"
                           "[MAX_CONTEXT_INPUTS][MAX_QUANT_TABLE_SIZE];",
        },
    };
    RET(ff_vk_shader_add_descriptor_set(&fv->s, shd, desc_set, 2, 1, 0));

    define_shared_code(avctx, shd);

    desc_set = (FFVulkanDescriptorSetBinding []) {
        {
            .name        = "slice_data_buf",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .mem_quali   = "readonly",
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .buf_content = "SliceContext slice_ctx[1024];",
        },
    };
    RET(ff_vk_shader_add_descriptor_set(&fv->s, shd, desc_set, 1, 0, 0));

    GLSLC(0, layout(push_constant, scalar) uniform pushConstants {             );
    GLSLC(1,    u8buf slice_state;                                             );
    GLSLC(1,    uint plane_state_size;                                         );
    GLSLC(1,    uint context_count;                                            );
    GLSLC(1,    uint8_t codec_planes;                                          );
    GLSLC(1,    uint8_t key_frame;                                             );
    GLSLC(1,    uint8_t padding[3];                                            );
    GLSLC(0, };                                                                );
    ff_vk_shader_add_push_const(shd, 0, sizeof(FFv1VkResetParameters),
                                VK_SHADER_STAGE_COMPUTE_BIT);

    GLSLD(ff_source_ffv1_reset_comp);

    RET(spv->compile_shader(&fv->s, spv, shd, &spv_data, &spv_len, "main",
                            &spv_opaque));
    RET(ff_vk_shader_link(&fv->s, shd, spv_data, spv_len, "main"));

    RET(ff_vk_shader_register_exec(&fv->s, &fv->exec_pool, shd));

fail:
    if (spv_opaque)
        spv->free_shader(spv, &spv_opaque);

    return err;
}

static int init_rct_shader(AVCodecContext *avctx, FFVkSPIRVCompiler *spv)
{
    int err;
    VulkanEncodeFFv1Context *fv = avctx->priv_data;
    FFVulkanShader *shd = &fv->rct;
    FFVulkanDescriptorSetBinding *desc_set;

    uint8_t *spv_data;
    size_t spv_len;
    void *spv_opaque = NULL;
    int wg_count = sqrt(fv->s.props.properties.limits.maxComputeWorkGroupInvocations);

    enum AVPixelFormat intermediate_fmt = get_supported_rgb_buffer_fmt(avctx);
    if (intermediate_fmt == AV_PIX_FMT_NONE) {
        av_log(avctx, AV_LOG_ERROR, "Unable to find a supported compatible "
                                    "pixel format for RCT buffer!\n");
        return AVERROR(ENOTSUP);
    }

    RET(init_indirect(avctx, intermediate_fmt));

    RET(ff_vk_shader_init(&fv->s, shd, "ffv1_rct",
                          VK_SHADER_STAGE_COMPUTE_BIT,
                          (const char *[]) { "GL_EXT_buffer_reference",
                                             "GL_EXT_buffer_reference2" }, 2,
                          wg_count, wg_count, 1,
                          0));

    av_bprintf(&shd->src, "#define MAX_QUANT_TABLES %i\n", MAX_QUANT_TABLES);
    av_bprintf(&shd->src, "#define MAX_CONTEXT_INPUTS %i\n", MAX_CONTEXT_INPUTS);
    av_bprintf(&shd->src, "#define MAX_QUANT_TABLE_SIZE %i\n", MAX_QUANT_TABLE_SIZE);

    desc_set = (FFVulkanDescriptorSetBinding []) {
        {
            .name        = "rangecoder_static_buf",
            .type        = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .mem_layout  = "scalar",
            .buf_content = "uint8_t zero_one_state[512];",
        },
        {
            .name        = "quant_buf",
            .type        = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .mem_layout  = "scalar",
            .buf_content = "int16_t quant_table[MAX_QUANT_TABLES]"
                           "[MAX_CONTEXT_INPUTS][MAX_QUANT_TABLE_SIZE];",
        },
    };
    RET(ff_vk_shader_add_descriptor_set(&fv->s, shd, desc_set, 2, 1, 0));

    define_shared_code(avctx, shd);

    desc_set = (FFVulkanDescriptorSetBinding []) {
        {
            .name        = "slice_data_buf",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .mem_quali   = "readonly",
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .buf_content = "SliceContext slice_ctx[1024];",
        },
        {
            .name       = "src",
            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .dimensions = 2,
            .mem_layout = ff_vk_shader_rep_fmt(fv->s.frames->sw_format,
                                               fv->rep_fmt),
            .elems      = av_pix_fmt_count_planes(fv->s.frames->sw_format),
            .mem_quali  = "readonly",
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        {
            .name       = "dst",
            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .dimensions = 2,
            .mem_layout = ff_vk_shader_rep_fmt(intermediate_fmt,
                                               fv->rep_fmt),
            .elems      = av_pix_fmt_count_planes(intermediate_fmt),
            .mem_quali  = "writeonly",
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };
    RET(ff_vk_shader_add_descriptor_set(&fv->s, shd, desc_set, 3, 0, 0));

    GLSLC(0, layout(push_constant, scalar) uniform pushConstants {             );
    GLSLC(1,    int offset;                                                    );
    GLSLC(1,    uint8_t planar_rgb;                                            );
    GLSLC(1,    uint8_t transparency;                                          );
    GLSLC(1,    uint8_t padding[2];                                            );
    GLSLC(0, };                                                                );
    ff_vk_shader_add_push_const(shd, 0, sizeof(FFv1VkRCTParameters),
                                VK_SHADER_STAGE_COMPUTE_BIT);

    GLSLD(ff_source_ffv1_enc_rct_comp);

    RET(spv->compile_shader(&fv->s, spv, shd, &spv_data, &spv_len, "main",
                            &spv_opaque));
    RET(ff_vk_shader_link(&fv->s, shd, spv_data, spv_len, "main"));

    RET(ff_vk_shader_register_exec(&fv->s, &fv->exec_pool, shd));

fail:
    if (spv_opaque)
        spv->free_shader(spv, &spv_opaque);

    return err;
}

static int init_encode_shader(AVCodecContext *avctx, FFVkSPIRVCompiler *spv)
{
    int err;
    VulkanEncodeFFv1Context *fv = avctx->priv_data;
    FFV1Context *f = &fv->ctx;
    FFVulkanShader *shd = &fv->enc;
    FFVulkanDescriptorSetBinding *desc_set;

    AVHWFramesContext *frames_ctx = fv->intermediate_frames_ref ?
                                    (AVHWFramesContext *)fv->intermediate_frames_ref->data :
                                    fv->s.frames;

    uint8_t *spv_data;
    size_t spv_len;
    void *spv_opaque = NULL;

    RET(ff_vk_shader_init(&fv->s, shd, "ffv1_enc",
                          VK_SHADER_STAGE_COMPUTE_BIT,
                          (const char *[]) { "GL_EXT_buffer_reference",
                                             "GL_EXT_buffer_reference2" }, 2,
                          1, 1, 1,
                          0));

    av_bprintf(&shd->src, "#define MAX_QUANT_TABLES %i\n", MAX_QUANT_TABLES);
    av_bprintf(&shd->src, "#define MAX_CONTEXT_INPUTS %i\n", MAX_CONTEXT_INPUTS);
    av_bprintf(&shd->src, "#define MAX_QUANT_TABLE_SIZE %i\n", MAX_QUANT_TABLE_SIZE);

    desc_set = (FFVulkanDescriptorSetBinding []) {
        {
            .name        = "rangecoder_static_buf",
            .type        = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .mem_layout  = "scalar",
            .buf_content = "uint8_t zero_one_state[512];",
        },
        {
            .name        = "quant_buf",
            .type        = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .mem_layout  = "scalar",
            .buf_content = "int16_t quant_table[MAX_QUANT_TABLES]"
                           "[MAX_CONTEXT_INPUTS][MAX_QUANT_TABLE_SIZE];",
        },
        {
            .name        = "crc_ieee_buf",
            .type        = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .mem_layout  = "scalar",
            .buf_content = "uint32_t crc_ieee[256];",
        },
    };

    RET(ff_vk_shader_add_descriptor_set(&fv->s, shd, desc_set, 3, 1, 0));

    define_shared_code(avctx, shd);

    desc_set = (FFVulkanDescriptorSetBinding []) {
        {
            .name        = "slice_data_buf",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .buf_content = "SliceContext slice_ctx[1024];",
        },
        {
            .name       = "src",
            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .dimensions = 2,
            .mem_layout = ff_vk_shader_rep_fmt(frames_ctx->sw_format,
                                               fv->rep_fmt),
            .elems      = av_pix_fmt_count_planes(frames_ctx->sw_format),
            .mem_quali  = "readonly",
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        {
            .name        = "results_data_buf",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .mem_quali   = "writeonly",
            .buf_content = "uint64_t slice_results[2048];",
        },
    };
    RET(ff_vk_shader_add_descriptor_set(&fv->s, shd, desc_set, 3, 0, 0));

    add_push_data(shd);

    /* Assemble the shader body */
    GLSLD(ff_source_ffv1_enc_common_comp);

    if (f->ac == AC_GOLOMB_RICE)
        GLSLD(ff_source_ffv1_enc_vlc_comp);
    else
        GLSLD(ff_source_ffv1_enc_ac_comp);

    if (fv->is_rgb)
        GLSLD(ff_source_ffv1_enc_rgb_comp);
    else
        GLSLD(ff_source_ffv1_enc_comp);

    RET(spv->compile_shader(&fv->s, spv, shd, &spv_data, &spv_len, "main",
                            &spv_opaque));
    RET(ff_vk_shader_link(&fv->s, shd, spv_data, spv_len, "main"));

    RET(ff_vk_shader_register_exec(&fv->s, &fv->exec_pool, shd));

fail:
    if (spv_opaque)
        spv->free_shader(spv, &spv_opaque);

    return err;
}

static int init_state_transition_data(AVCodecContext *avctx)
{
    int err;
    VulkanEncodeFFv1Context *fv = avctx->priv_data;

    uint8_t *buf_mapped;
    size_t buf_len = 512*sizeof(uint8_t);

    RET(ff_vk_create_buf(&fv->s, &fv->rangecoder_static_buf,
                         buf_len,
                         NULL, NULL,
                         VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT));
    RET(ff_vk_map_buffer(&fv->s, &fv->rangecoder_static_buf,
                         &buf_mapped, 0));

    for (int i = 1; i < 256; i++) {
        buf_mapped[256 + i] = fv->ctx.state_transition[i];
        buf_mapped[256 - i] = 256 - (int)fv->ctx.state_transition[i];
    }

    RET(ff_vk_unmap_buffer(&fv->s, &fv->rangecoder_static_buf, 1));

    /* Update descriptors */
    RET(ff_vk_shader_update_desc_buffer(&fv->s, &fv->exec_pool.contexts[0],
                                        &fv->setup, 0, 0, 0,
                                        &fv->rangecoder_static_buf,
                                        0, fv->rangecoder_static_buf.size,
                                        VK_FORMAT_UNDEFINED));
    RET(ff_vk_shader_update_desc_buffer(&fv->s, &fv->exec_pool.contexts[0],
                                        &fv->enc, 0, 0, 0,
                                        &fv->rangecoder_static_buf,
                                        0, fv->rangecoder_static_buf.size,
                                        VK_FORMAT_UNDEFINED));

fail:
    return err;
}

static int init_quant_table_data(AVCodecContext *avctx)
{
    int err;
    VulkanEncodeFFv1Context *fv = avctx->priv_data;

    int16_t *buf_mapped;
    size_t buf_len = MAX_QUANT_TABLES*
                     MAX_CONTEXT_INPUTS*
                     MAX_QUANT_TABLE_SIZE*sizeof(int16_t);

    RET(ff_vk_create_buf(&fv->s, &fv->quant_buf,
                         buf_len,
                         NULL, NULL,
                         VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT));
    RET(ff_vk_map_buffer(&fv->s, &fv->quant_buf, (void *)&buf_mapped, 0));

    memcpy(buf_mapped, fv->ctx.quant_tables,
           sizeof(fv->ctx.quant_tables));

    RET(ff_vk_unmap_buffer(&fv->s, &fv->quant_buf, 1));
    RET(ff_vk_shader_update_desc_buffer(&fv->s, &fv->exec_pool.contexts[0],
                                        &fv->enc, 0, 1, 0,
                                        &fv->quant_buf,
                                        0, fv->quant_buf.size,
                                        VK_FORMAT_UNDEFINED));

fail:
    return err;
}

static int init_crc_table_data(AVCodecContext *avctx)
{
    int err;
    VulkanEncodeFFv1Context *fv = avctx->priv_data;

    uint32_t *buf_mapped;
    size_t buf_len = 256*sizeof(int32_t);

    RET(ff_vk_create_buf(&fv->s, &fv->crc_tab_buf,
                         buf_len,
                         NULL, NULL,
                         VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT));
    RET(ff_vk_map_buffer(&fv->s, &fv->crc_tab_buf, (void *)&buf_mapped, 0));

    memcpy(buf_mapped, av_crc_get_table(AV_CRC_32_IEEE), buf_len);

    RET(ff_vk_unmap_buffer(&fv->s, &fv->crc_tab_buf, 1));
    RET(ff_vk_shader_update_desc_buffer(&fv->s, &fv->exec_pool.contexts[0],
                                        &fv->enc, 0, 2, 0,
                                        &fv->crc_tab_buf,
                                        0, fv->crc_tab_buf.size,
                                        VK_FORMAT_UNDEFINED));

fail:
    return err;
}

static av_cold int vulkan_encode_ffv1_init(AVCodecContext *avctx)
{
    int err;
    VulkanEncodeFFv1Context *fv = avctx->priv_data;
    FFV1Context *f = &fv->ctx;
    FFVkSPIRVCompiler *spv;

    if ((err = ff_ffv1_common_init(avctx)) < 0)
        return err;

    if (f->ac == 1)
        f->ac = AC_RANGE_CUSTOM_TAB;

    err = ff_ffv1_encode_setup_plane_info(avctx, avctx->sw_pix_fmt);
    if (err < 0)
        return err;

    /* Target version 3 by default */
    f->version = 3;

    err = ff_ffv1_encode_init(avctx);
    if (err < 0)
        return err;

    /* Rice coding did not support high bit depths */
    if (f->bits_per_raw_sample > (f->version > 3 ? 16 : 8)) {
        if (f->ac == AC_GOLOMB_RICE) {
            av_log(avctx, AV_LOG_WARNING, "bits_per_raw_sample > 8, "
                                          "forcing range coder\n");
            f->ac = AC_RANGE_CUSTOM_TAB;
        }
    }

    if (f->version < 4 && avctx->gop_size > 1) {
        av_log(avctx, AV_LOG_ERROR, "Using inter frames requires version 4 (-level 4)\n");
        return AVERROR_INVALIDDATA;
    }

    if (f->version == 4 && avctx->strict_std_compliance > FF_COMPLIANCE_EXPERIMENTAL) {
        av_log(avctx, AV_LOG_ERROR, "Version 4 is experimental and requires -strict -2\n");
            return AVERROR_INVALIDDATA;
    }

    //if (fv->ctx.ac == AC_GOLOMB_RICE) {
    if (0) {
        int w_a = FFALIGN(avctx->width, LG_ALIGN_W);
        int h_a = FFALIGN(avctx->height, LG_ALIGN_H);
        int w_sl, h_sl;

        /* Pixels per line an invocation handles */
        int ppi = 0;
        /* Chunk size */
        int chunks = 0;

        do {
            if (ppi < 2)
                ppi++;
            chunks++;
            w_sl = w_a / (LG_ALIGN_W*ppi);
            h_sl = h_a / (LG_ALIGN_H*chunks);
        } while (w_sl > MAX_SLICES / h_sl);

        av_log(avctx, AV_LOG_VERBOSE, "Slice config: %ix%i, %i total\n",
               LG_ALIGN_W*ppi, LG_ALIGN_H*chunks, w_sl*h_sl);
        av_log(avctx, AV_LOG_VERBOSE, "Horizontal slices: %i (%i pixels per invoc)\n",
               w_sl, ppi);
        av_log(avctx, AV_LOG_VERBOSE, "Vertical slices: %i (%i chunks)\n",
               h_sl, chunks);

        f->num_h_slices = w_sl;
        f->num_v_slices = h_sl;

        fv->ppi = ppi;
        fv->chunks = chunks;
    } else {
        f->num_h_slices = fv->num_h_slices;
        f->num_v_slices = fv->num_v_slices;

        if (f->num_h_slices <= 0)
            f->num_h_slices = 32;
        if (f->num_v_slices <= 0)
            f->num_v_slices = 32;

        f->num_h_slices = FFMIN(f->num_h_slices, avctx->width);
        f->num_v_slices = FFMIN(f->num_v_slices, avctx->height);
    }

    if ((err = ff_ffv1_write_extradata(avctx)) < 0)
        return err;

    if (f->version < 4) {
        if (((f->chroma_h_shift > 0) && (avctx->width % (64 << f->chroma_h_shift))) ||
            ((f->chroma_v_shift > 0) && (avctx->height % (64 << f->chroma_v_shift)))) {
            av_log(avctx, AV_LOG_ERROR, "Encoding frames with subsampling and unaligned "
                                        "dimensions is only supported in version 4 (-level 4)\n");
            return AVERROR_PATCHWELCOME;
        }
    }

    if (fv->force_pcm) {
        if (f->version < 4) {
            av_log(avctx, AV_LOG_ERROR, "PCM coding only supported by version 4 (-level 4)\n");
            return AVERROR_INVALIDDATA;
        } else if (f->ac != AC_RANGE_CUSTOM_TAB) {
            av_log(avctx, AV_LOG_ERROR, "PCM coding requires range coding\n");
            return AVERROR_INVALIDDATA;
        }
    }

    /* Init Vulkan */
    err = ff_vk_init(&fv->s, avctx, NULL, avctx->hw_frames_ctx);
    if (err < 0)
        return err;

    err = ff_vk_qf_init(&fv->s, &fv->qf, VK_QUEUE_COMPUTE_BIT);
    if (err < 0) {
        av_log(avctx, AV_LOG_ERROR, "Device has no compute queues!\n");
        return err;
    }

    err = ff_vk_exec_pool_init(&fv->s, &fv->qf, &fv->exec_pool,
                               1, /* Single-threaded for now */
                               0, 0, 0, NULL);
    if (err < 0)
        return err;

    spv = ff_vk_spirv_init();
    if (!spv) {
        av_log(avctx, AV_LOG_ERROR, "Unable to initialize SPIR-V compiler!\n");
        return AVERROR_EXTERNAL;
    }

    /* Detect the special RGB coding mode */
    fv->is_rgb = !(f->colorspace == 0 && avctx->sw_pix_fmt != AV_PIX_FMT_YA8) &&
                 !(avctx->sw_pix_fmt == AV_PIX_FMT_YA8);

    /* bits_per_raw_sample use regular unsigned representation,
     * but in higher bit depths, the data is casted to int16_t */
    fv->rep_fmt = FF_VK_REP_UINT;
    if (!fv->is_rgb && f->bits_per_raw_sample > 8)
        fv->rep_fmt = FF_VK_REP_INT;

    /* Init setup shader */
    err = init_setup_shader(avctx, spv);
    if (err < 0) {
        spv->uninit(&spv);
        return err;
    }

    /* Init reset shader */
    err = init_reset_shader(avctx, spv);
    if (err < 0) {
        spv->uninit(&spv);
        return err;
    }

    /* Init RCT shader */
    if (fv->is_rgb) {
        err = init_rct_shader(avctx, spv);
        if (err < 0) {
            spv->uninit(&spv);
            return err;
        }
    }

    /* Encode shader */
    err = init_encode_shader(avctx, spv);
    if (err < 0) {
        spv->uninit(&spv);
        return err;
    }

    spv->uninit(&spv);

    /* Range coder data */
    err = init_state_transition_data(avctx);
    if (err < 0)
        return err;

    /* Quantization table data */
    err = init_quant_table_data(avctx);
    if (err < 0)
        return err;

    /* CRC table buffer */
    err = init_crc_table_data(avctx);
    if (err < 0)
        return err;

    return 0;
}

static av_cold int vulkan_encode_ffv1_close(AVCodecContext *avctx)
{
    VulkanEncodeFFv1Context *fv = avctx->priv_data;

    ff_vk_exec_pool_free(&fv->s, &fv->exec_pool);

    ff_vk_shader_free(&fv->s, &fv->enc);
    ff_vk_shader_free(&fv->s, &fv->rct);
    ff_vk_shader_free(&fv->s, &fv->reset);
    ff_vk_shader_free(&fv->s, &fv->setup);

    av_buffer_unref(&fv->intermediate_frames_ref);

    av_buffer_pool_uninit(&fv->results_data_pool);

    av_buffer_pool_uninit(&fv->out_data_pool);
    av_buffer_pool_uninit(&fv->tmp_data_pool);

    av_buffer_unref(&fv->keyframe_slice_data_ref);
    av_buffer_pool_uninit(&fv->slice_data_pool);

    ff_vk_free_buf(&fv->s, &fv->quant_buf);
    ff_vk_free_buf(&fv->s, &fv->rangecoder_static_buf);
    ff_vk_free_buf(&fv->s, &fv->crc_tab_buf);

    ff_vk_uninit(&fv->s);

    return 0;
}

#define OFFSET(x) offsetof(VulkanEncodeFFv1Context, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption vulkan_encode_ffv1_options[] = {
    { "slicecrc", "Protect slices with CRCs", OFFSET(ctx.ec), AV_OPT_TYPE_BOOL,
            { .i64 = -1 }, -1, 1, VE },
    { "context", "Context model", OFFSET(ctx.context_model), AV_OPT_TYPE_INT,
            { .i64 = 0 }, 0, 1, VE },
    { "coder", "Coder type", OFFSET(ctx.ac), AV_OPT_TYPE_INT,
            { .i64 = AC_RANGE_CUSTOM_TAB }, -2, 2, VE, .unit = "coder" },
        { "rice", "Golomb rice", 0, AV_OPT_TYPE_CONST,
            { .i64 = AC_GOLOMB_RICE }, INT_MIN, INT_MAX, VE, .unit = "coder" },
        { "range_tab", "Range with custom table", 0, AV_OPT_TYPE_CONST,
            { .i64 = AC_RANGE_CUSTOM_TAB }, INT_MIN, INT_MAX, VE, .unit = "coder" },
    { "qtable", "Quantization table", OFFSET(ctx.qtable), AV_OPT_TYPE_INT,
            { .i64 = -1 }, -1, 2, VE },

    { "slices_h", "Number of horizontal slices", OFFSET(num_h_slices), AV_OPT_TYPE_INT,
            { .i64 = -1 }, -1, 32, VE },
    { "slices_v", "Number of vertical slices", OFFSET(num_v_slices), AV_OPT_TYPE_INT,
            { .i64 = -1 }, -1, 32, VE },

    { "force_pcm", "Code all slices with no prediction", OFFSET(force_pcm), AV_OPT_TYPE_BOOL,
            { .i64 = 0 }, 0, 1, VE },

    { NULL }
};

static const FFCodecDefault vulkan_encode_ffv1_defaults[] = {
    { "g", "1" },
    { NULL },
};

static const AVClass vulkan_encode_ffv1_class = {
    .class_name = "ffv1_vulkan",
    .item_name  = av_default_item_name,
    .option     = vulkan_encode_ffv1_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const AVCodecHWConfigInternal *const vulkan_encode_ffv1_hw_configs[] = {
    HW_CONFIG_ENCODER_FRAMES(VULKAN, VULKAN),
    NULL,
};

const FFCodec ff_ffv1_vulkan_encoder = {
    .p.name         = "ffv1_vulkan",
    CODEC_LONG_NAME("FFmpeg video codec #1 (Vulkan)"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_FFV1,
    .priv_data_size = sizeof(VulkanEncodeFFv1Context),
    .init           = &vulkan_encode_ffv1_init,
    FF_CODEC_ENCODE_CB(vulkan_encode_ffv1_frame),
    .close          = &vulkan_encode_ffv1_close,
    .p.priv_class   = &vulkan_encode_ffv1_class,
    .p.capabilities = AV_CODEC_CAP_DELAY |
                      AV_CODEC_CAP_HARDWARE |
                      AV_CODEC_CAP_DR1 |
                      AV_CODEC_CAP_ENCODER_FLUSH |
                      AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP | FF_CODEC_CAP_EOF_FLUSH,
    .defaults       = vulkan_encode_ffv1_defaults,
    .p.pix_fmts = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_VULKAN,
        AV_PIX_FMT_NONE,
    },
    .hw_configs     = vulkan_encode_ffv1_hw_configs,
    .p.wrapper_name = "vulkan",
};
