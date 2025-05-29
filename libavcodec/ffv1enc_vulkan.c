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

#include "libavutil/mem.h"
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
#include "ffv1_vulkan.h"

/* Parallel Golomb alignment */
#define LG_ALIGN_W 32
#define LG_ALIGN_H 32

/* Unlike the decoder, we need 4 lines (but really only 3) */
#define RGB_LINECACHE 4

typedef struct VulkanEncodeFFv1FrameData {
    /* Output data */
    AVBufferRef *out_data_ref;

    /* Results data */
    AVBufferRef *results_data_ref;

    /* Copied from the source */
    int64_t pts;
    int64_t duration;
    void        *frame_opaque;
    AVBufferRef *frame_opaque_ref;

    int key_frame;
} VulkanEncodeFFv1FrameData;

typedef struct VulkanEncodeFFv1Context {
    FFV1Context ctx;
    AVFrame *frame;

    FFVulkanContext s;
    AVVulkanDeviceQueueFamily *qf;
    FFVkExecPool exec_pool;

    AVVulkanDeviceQueueFamily *transfer_qf;
    FFVkExecPool transfer_exec_pool;

    VkBufferCopy *buf_regions;
    VulkanEncodeFFv1FrameData *exec_ctx_info;
    int in_flight;
    int async_depth;
    size_t max_heap_size;

    FFVulkanShader setup;
    FFVulkanShader rct_search;
    FFVulkanShader reset;
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

    /* Slice results buffer */
    AVBufferPool *results_data_pool;

    /* Intermediate frame pool */
    AVBufferRef *intermediate_frames_ref;

    /* Representation mode */
    enum FFVkShaderRepFormat rep_fmt;

    int num_h_slices;
    int num_v_slices;
    int force_pcm;
    int optimize_rct;

    int is_rgb;
    int ppi;
    int chunks;
} VulkanEncodeFFv1Context;

extern const char *ff_source_common_comp;
extern const char *ff_source_rangecoder_comp;
extern const char *ff_source_ffv1_vlc_comp;
extern const char *ff_source_ffv1_common_comp;
extern const char *ff_source_ffv1_reset_comp;
extern const char *ff_source_ffv1_rct_search_comp;
extern const char *ff_source_ffv1_enc_setup_comp;
extern const char *ff_source_ffv1_enc_comp;

typedef struct FFv1VkParameters {
    VkDeviceAddress slice_state;
    VkDeviceAddress scratch_data;
    VkDeviceAddress out_data;

    int32_t fmt_lut[4];
    int32_t sar[2];
    uint32_t chroma_shift[2];

    uint32_t plane_state_size;
    uint32_t context_count;
    uint32_t crcref;
    uint32_t slice_size_max;
    int      rct_offset;

    uint8_t extend_lookup[8];
    uint8_t bits_per_raw_sample;
    uint8_t context_model;
    uint8_t version;
    uint8_t micro_version;
    uint8_t force_pcm;
    uint8_t key_frame;
    uint8_t components;
    uint8_t planes;
    uint8_t codec_planes;
    uint8_t planar_rgb;
    uint8_t transparency;
    uint8_t colorspace;
    uint8_t pic_mode;
    uint8_t ec;
    uint8_t ppi;
    uint8_t chunks;
    uint8_t rct_search;
    uint8_t padding[3];
} FFv1VkParameters;

static void add_push_data(FFVulkanShader *shd)
{
    GLSLC(0, layout(push_constant, scalar) uniform pushConstants {            );
    GLSLC(1,    u8buf slice_state;                                            );
    GLSLC(1,    u8buf scratch_data;                                           );
    GLSLC(1,    u8buf out_data;                                               );
    GLSLC(0,                                                                  );
    GLSLC(1,    ivec4 fmt_lut;                                                );
    GLSLC(1,    ivec2 sar;                                                    );
    GLSLC(1,    uvec2 chroma_shift;                                           );
    GLSLC(0,                                                                  );
    GLSLC(1,    uint plane_state_size;                                        );
    GLSLC(1,    uint context_count;                                           );
    GLSLC(1,    uint32_t crcref;                                              );
    GLSLC(1,    uint32_t slice_size_max;                                      );
    GLSLC(1,    int rct_offset;                                               );
    GLSLC(0,                                                                  );
    GLSLC(1,    uint8_t extend_lookup[8];                                     );
    GLSLC(1,    uint8_t bits_per_raw_sample;                                  );
    GLSLC(1,    uint8_t context_model;                                        );
    GLSLC(1,    uint8_t version;                                              );
    GLSLC(1,    uint8_t micro_version;                                        );
    GLSLC(1,    uint8_t force_pcm;                                            );
    GLSLC(1,    uint8_t key_frame;                                            );
    GLSLC(1,    uint8_t components;                                           );
    GLSLC(1,    uint8_t planes;                                               );
    GLSLC(1,    uint8_t codec_planes;                                         );
    GLSLC(1,    uint8_t planar_rgb;                                           );
    GLSLC(1,    uint8_t transparency;                                         );
    GLSLC(1,    uint8_t colorspace;                                           );
    GLSLC(1,    uint8_t pic_mode;                                             );
    GLSLC(1,    uint8_t ec;                                                   );
    GLSLC(1,    uint8_t ppi;                                                  );
    GLSLC(1,    uint8_t chunks;                                               );
    GLSLC(1,    uint8_t rct_search;                                           );
    GLSLC(1,    uint8_t padding[3];                                           );
    GLSLC(0, };                                                               );
    ff_vk_shader_add_push_const(shd, 0, sizeof(FFv1VkParameters),
                                VK_SHADER_STAGE_COMPUTE_BIT);
}

typedef struct FFv1VkRCTSearchParameters {
    int fmt_lut[4];
    int rct_offset;
    uint8_t planar_rgb;
    uint8_t transparency;
    uint8_t key_frame;
    uint8_t force_pcm;
    uint8_t version;
    uint8_t micro_version;
    uint8_t padding[2];
} FFv1VkRCTSearchParameters;

static int run_rct_search(AVCodecContext *avctx, FFVkExecContext *exec,
                          AVFrame *enc_in, VkImageView *enc_in_views,
                          FFVkBuffer *slice_data_buf, uint32_t slice_data_size)
{
    VulkanEncodeFFv1Context *fv = avctx->priv_data;
    FFV1Context *f = &fv->ctx;
    FFVulkanFunctions *vk = &fv->s.vkfn;
    AVHWFramesContext *src_hwfc = (AVHWFramesContext *)enc_in->hw_frames_ctx->data;
    FFv1VkRCTSearchParameters pd;

    /* Update descriptors */
    ff_vk_shader_update_desc_buffer(&fv->s, exec, &fv->rct_search,
                                    0, 0, 0,
                                    slice_data_buf,
                                    0, slice_data_size*f->slice_count,
                                    VK_FORMAT_UNDEFINED);
    ff_vk_shader_update_img_array(&fv->s, exec, &fv->rct_search,
                                  enc_in, enc_in_views,
                                  0, 1,
                                  VK_IMAGE_LAYOUT_GENERAL,
                                  VK_NULL_HANDLE);

    ff_vk_exec_bind_shader(&fv->s, exec, &fv->rct_search);

    pd = (FFv1VkRCTSearchParameters) {
        .rct_offset = 1 << f->bits_per_raw_sample,
        .planar_rgb = ff_vk_mt_is_np_rgb(src_hwfc->sw_format) &&
                      (ff_vk_count_images((AVVkFrame *)enc_in->data[0]) > 1),
        .transparency = f->transparency,
        .key_frame = f->key_frame,
        .force_pcm = fv->force_pcm,
        .version = f->version,
        .micro_version = f->micro_version,
    };

    if (avctx->sw_pix_fmt == AV_PIX_FMT_GBRP10 ||
        avctx->sw_pix_fmt == AV_PIX_FMT_GBRP12 ||
        avctx->sw_pix_fmt == AV_PIX_FMT_GBRP14)
        memcpy(pd.fmt_lut, (int [4]) { 2, 1, 0, 3 }, 4*sizeof(int));
    else
        ff_vk_set_perm(avctx->sw_pix_fmt, pd.fmt_lut, 1);

    ff_vk_shader_update_push_const(&fv->s, exec, &fv->rct_search,
                                   VK_SHADER_STAGE_COMPUTE_BIT,
                                   0, sizeof(pd), &pd);

    vk->CmdDispatch(exec->buf, fv->ctx.num_h_slices, fv->ctx.num_v_slices, 1);

    return 0;
}

static int vulkan_encode_ffv1_submit_frame(AVCodecContext *avctx,
                                           FFVkExecContext *exec,
                                           const AVFrame *pict)
{
    int err;
    VulkanEncodeFFv1Context *fv = avctx->priv_data;
    FFV1Context *f = &fv->ctx;
    FFVulkanFunctions *vk = &fv->s.vkfn;

    VulkanEncodeFFv1FrameData *fd = exec->opaque;
    FFv1VkParameters pd;

    /* Slice data */
    AVBufferRef *slice_data_ref;
    FFVkBuffer *slice_data_buf;
    uint32_t plane_state_size;
    uint32_t slice_state_size;
    uint32_t slice_data_size;

    /* Output data */
    size_t maxsize;
    FFVkBuffer *out_data_buf;

    /* Results data */
    FFVkBuffer *results_data_buf;

    int has_inter = avctx->gop_size > 1;
    uint32_t context_count = f->context_count[f->context_model];
    const AVPixFmtDescriptor *fmt_desc = av_pix_fmt_desc_get(avctx->sw_pix_fmt);

    AVFrame *src = (AVFrame *)pict;
    VkImageView src_views[AV_NUM_DATA_POINTERS];

    AVFrame *tmp = NULL;
    VkImageView tmp_views[AV_NUM_DATA_POINTERS];

    VkImageMemoryBarrier2 img_bar[37];
    int nb_img_bar = 0;
    VkBufferMemoryBarrier2 buf_bar[8];
    int nb_buf_bar = 0;

    /* Start recording */
    ff_vk_exec_start(&fv->s, exec);

    /* Frame state */
    f->cur_enc_frame = pict;
    if (avctx->gop_size == 0 || f->picture_number % avctx->gop_size == 0) {
        av_buffer_unref(&fv->keyframe_slice_data_ref);
        f->key_frame = fd->key_frame = 1;
        f->gob_count++;
    } else {
        f->key_frame = fd->key_frame = 0;
    }

    f->slice_count = f->max_slice_count;

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

    /* Allocate slice data buffer */
    slice_data_ref = fv->keyframe_slice_data_ref;
    if (!slice_data_ref) {
        RET(ff_vk_get_pooled_buffer(&fv->s, &fv->slice_data_pool,
                                    &slice_data_ref,
                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                    NULL, slice_state_size*f->slice_count,
                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT));

        /* Only save it if we're going to use it again */
        if (has_inter)
            fv->keyframe_slice_data_ref = slice_data_ref;
    }
    slice_data_buf = (FFVkBuffer *)slice_data_ref->data;
    ff_vk_exec_add_dep_buf(&fv->s, exec, &slice_data_ref, 1, has_inter);

    /* Allocate results buffer */
    RET(ff_vk_get_pooled_buffer(&fv->s, &fv->results_data_pool,
                                &fd->results_data_ref,
                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                NULL, 2*f->slice_count*sizeof(uint64_t),
                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT));
    results_data_buf = (FFVkBuffer *)fd->results_data_ref->data;
    ff_vk_exec_add_dep_buf(&fv->s, exec, &fd->results_data_ref, 1, 1);

    /* Output buffer size */
    maxsize = ff_ffv1_encode_buffer_size(avctx);
    maxsize = FFMIN(maxsize, fv->s.props_11.maxMemoryAllocationSize);

    /* Allocate output buffer */
    RET(ff_vk_get_pooled_buffer(&fv->s, &fv->out_data_pool,
                                &fd->out_data_ref,
                                VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                NULL, maxsize,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                (maxsize < fv->max_heap_size ?
                                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT : 0x0) |
                                (!(fv->s.extensions & FF_VK_EXT_EXTERNAL_HOST_MEMORY) ?
                                 VK_MEMORY_PROPERTY_HOST_CACHED_BIT : 0x0)));
    out_data_buf = (FFVkBuffer *)fd->out_data_ref->data;
    ff_vk_exec_add_dep_buf(&fv->s, exec, &fd->out_data_ref, 1, 1);

    /* Prepare input frame */
    RET(ff_vk_exec_add_dep_frame(&fv->s, exec, src,
                                 VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT));

    RET(ff_vk_create_imageviews(&fv->s, exec, src_views, src,
                                fv->rep_fmt));
    ff_vk_frame_barrier(&fv->s, exec, src, img_bar, &nb_img_bar,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_SHADER_READ_BIT,
                        VK_IMAGE_LAYOUT_GENERAL,
                        VK_QUEUE_FAMILY_IGNORED);

    if (fv->is_rgb) {
        /* Create a temporaty frame */
        tmp = av_frame_alloc();
        if (!(tmp))
            return AVERROR(ENOMEM);

        RET(av_hwframe_get_buffer(fv->intermediate_frames_ref,
                                  tmp, 0));

        RET(ff_vk_exec_add_dep_frame(&fv->s, exec, tmp,
                                     VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                     VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT));
        RET(ff_vk_create_imageviews(&fv->s, exec, tmp_views,
                                    tmp,
                                    fv->rep_fmt));
    }

    /* Setup shader */
    ff_vk_shader_update_desc_buffer(&fv->s, exec, &fv->setup,
                                    1, 0, 0,
                                    slice_data_buf,
                                    0, slice_data_size*f->slice_count,
                                    VK_FORMAT_UNDEFINED);
    ff_vk_shader_update_img_array(&fv->s, exec, &fv->setup,
                                  src, src_views,
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

    if (fv->optimize_rct) {
        RET(run_rct_search(avctx, exec,
                           src, src_views,
                           slice_data_buf, slice_data_size));

        buf_bar[nb_buf_bar++] = (VkBufferMemoryBarrier2) {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = slice_data_buf->stage,
            .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .srcAccessMask = slice_data_buf->access,
            .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = slice_data_buf->buf,
            .size = slice_data_size*f->slice_count,
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
        .out_data = out_data_buf->address,
        .bits_per_raw_sample = f->bits_per_raw_sample,
        .sar[0] = pict->sample_aspect_ratio.num,
        .sar[1] = pict->sample_aspect_ratio.den,
        .chroma_shift[0] = f->chroma_h_shift,
        .chroma_shift[1] = f->chroma_v_shift,
        .plane_state_size = plane_state_size,
        .context_count = context_count,
        .crcref = f->crcref,
        .rct_offset = 1 << f->bits_per_raw_sample,
        .slice_size_max = out_data_buf->size / f->slice_count,
        .context_model = fv->ctx.context_model,
        .version = f->version,
        .micro_version = f->micro_version,
        .force_pcm = fv->force_pcm,
        .key_frame = f->key_frame,
        .components = fmt_desc->nb_components,
        .planes = av_pix_fmt_count_planes(avctx->sw_pix_fmt),
        .codec_planes = f->plane_count,
        .planar_rgb = ff_vk_mt_is_np_rgb(avctx->sw_pix_fmt) &&
                      (ff_vk_count_images((AVVkFrame *)src->data[0]) > 1),
        .transparency = f->transparency,
        .colorspace = f->colorspace,
        .pic_mode = !(pict->flags & AV_FRAME_FLAG_INTERLACED) ? 3 :
                    !(pict->flags & AV_FRAME_FLAG_TOP_FIELD_FIRST) ? 2 : 1,
        .ec = f->ec,
        .ppi = fv->ppi,
        .chunks = fv->chunks,
        .rct_search = fv->optimize_rct,
    };

    /* For some reason the C FFv1 encoder/decoder treats these differently */
    if (avctx->sw_pix_fmt == AV_PIX_FMT_GBRP10 ||
        avctx->sw_pix_fmt == AV_PIX_FMT_GBRP12 ||
        avctx->sw_pix_fmt == AV_PIX_FMT_GBRP14)
        memcpy(pd.fmt_lut, (int [4]) { 2, 1, 0, 3 }, 4*sizeof(int));
    else
        ff_vk_set_perm(avctx->sw_pix_fmt, pd.fmt_lut, 1);

    for (int i = 0; i < f->quant_table_count; i++)
        pd.extend_lookup[i] = (f->quant_tables[i][3][127] != 0) ||
                              (f->quant_tables[i][4][127] != 0);
    ff_vk_shader_update_push_const(&fv->s, exec, &fv->setup,
                                   VK_SHADER_STAGE_COMPUTE_BIT,
                                   0, sizeof(pd), &pd);
    vk->CmdDispatch(exec->buf, fv->ctx.num_h_slices, fv->ctx.num_v_slices, 1);

    /* Clean up temporary image */
    if (fv->is_rgb) {
        AVVkFrame *vkf = (AVVkFrame *)tmp->data[0];
        vk->CmdClearColorImage(exec->buf, vkf->img[0], VK_IMAGE_LAYOUT_GENERAL,
                               &((VkClearColorValue) { 0 }),
                               1, &((VkImageSubresourceRange) {
                                   .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                   .levelCount = 1,
                                   .layerCount = 1,
                               }));
    }

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
            .codec_planes = f->plane_count,
            .key_frame = f->key_frame,
        };
        for (int i = 0; i < f->quant_table_count; i++)
            pd_reset.context_count[i] = f->context_count[i];

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

    if (fv->is_rgb) {
        ff_vk_frame_barrier(&fv->s, exec, tmp, img_bar, &nb_img_bar,
                            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                            VK_IMAGE_LAYOUT_GENERAL,
                            VK_QUEUE_FAMILY_IGNORED);
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
                                  src, src_views,
                                  1, 1,
                                  VK_IMAGE_LAYOUT_GENERAL,
                                  VK_NULL_HANDLE);
    ff_vk_shader_update_desc_buffer(&fv->s, exec,
                                    &fv->enc, 1, 2, 0,
                                    results_data_buf,
                                    0, results_data_buf->size,
                                    VK_FORMAT_UNDEFINED);
    if (fv->is_rgb)
        ff_vk_shader_update_img_array(&fv->s, exec, &fv->enc,
                                      tmp, tmp_views,
                                      1, 3,
                                      VK_IMAGE_LAYOUT_GENERAL,
                                      VK_NULL_HANDLE);

    ff_vk_exec_bind_shader(&fv->s, exec, &fv->enc);
    ff_vk_shader_update_push_const(&fv->s, exec, &fv->enc,
                                   VK_SHADER_STAGE_COMPUTE_BIT,
                                   0, sizeof(pd), &pd);
    vk->CmdDispatch(exec->buf, fv->ctx.num_h_slices, fv->ctx.num_v_slices, 1);

    /* Submit */
    err = ff_vk_exec_submit(&fv->s, exec);
    if (err < 0)
        return err;

    f->picture_number++;

    /* This, if needed, was referenced by the execution context
     * as it was declared as a dependency. */
    av_frame_free(&tmp);
    return 0;

fail:
    av_frame_free(&tmp);
    ff_vk_exec_discard_deps(&fv->s, exec);

    return err;
}

static int transfer_slices(AVCodecContext *avctx,
                           VkBufferCopy *buf_regions, int nb_regions,
                           VulkanEncodeFFv1FrameData *fd,
                           uint8_t *dst, AVBufferRef *dst_ref)
{
    int err;
    VulkanEncodeFFv1Context *fv = avctx->priv_data;
    FFVulkanFunctions *vk = &fv->s.vkfn;
    FFVkExecContext *exec;

    FFVkBuffer *out_data_buf = (FFVkBuffer *)fd->out_data_ref->data;

    AVBufferRef *mapped_ref;
    FFVkBuffer *mapped_buf;

    VkBufferMemoryBarrier2 buf_bar[8];
    int nb_buf_bar = 0;

    err = ff_vk_host_map_buffer(&fv->s, &mapped_ref, dst, dst_ref,
                                VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    if (err < 0)
        return err;

    mapped_buf = (FFVkBuffer *)mapped_ref->data;

    /* Transfer the slices */
    exec = ff_vk_exec_get(&fv->s, &fv->transfer_exec_pool);
    ff_vk_exec_start(&fv->s, exec);

    ff_vk_exec_add_dep_buf(&fv->s, exec, &fd->out_data_ref, 1, 0);
    fd->out_data_ref = NULL; /* Ownership passed */

    ff_vk_exec_add_dep_buf(&fv->s, exec, &mapped_ref, 1, 0);
    mapped_ref = NULL; /* Ownership passed */

    /* Ensure the output buffer is finished */
    buf_bar[nb_buf_bar++] = (VkBufferMemoryBarrier2) {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .srcStageMask = out_data_buf->stage,
        .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        .srcAccessMask = out_data_buf->access,
        .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = out_data_buf->buf,
        .size = VK_WHOLE_SIZE,
        .offset = 0,
    };
    vk->CmdPipelineBarrier2(exec->buf, &(VkDependencyInfo) {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pBufferMemoryBarriers = buf_bar,
        .bufferMemoryBarrierCount = nb_buf_bar,
    });
    out_data_buf->stage = buf_bar[0].dstStageMask;
    out_data_buf->access = buf_bar[0].dstAccessMask;
    nb_buf_bar = 0;

    for (int i = 0; i < nb_regions; i++)
        buf_regions[i].dstOffset += mapped_buf->virtual_offset;

    vk->CmdCopyBuffer(exec->buf,
                      out_data_buf->buf, mapped_buf->buf,
                      nb_regions, buf_regions);

    /* Submit */
    err = ff_vk_exec_submit(&fv->s, exec);
    if (err < 0)
        return err;

    /* We need the encoded data immediately */
    ff_vk_exec_wait(&fv->s, exec);

    return 0;
}

static int get_packet(AVCodecContext *avctx, FFVkExecContext *exec,
                      AVPacket *pkt)
{
    int err;
    VulkanEncodeFFv1Context *fv = avctx->priv_data;
    FFV1Context *f = &fv->ctx;
    FFVulkanFunctions *vk = &fv->s.vkfn;
    VulkanEncodeFFv1FrameData *fd = exec->opaque;

    FFVkBuffer *out_data_buf = (FFVkBuffer *)fd->out_data_ref->data;
    FFVkBuffer *results_data_buf = (FFVkBuffer *)fd->results_data_ref->data;
    uint64_t *sc;

    /* Make sure encoding's done */
    ff_vk_exec_wait(&fv->s, exec);

    /* Invalidate slice/output data if needed */
    if (!(results_data_buf->flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        VkMappedMemoryRange invalidate_data = {
            .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .memory = results_data_buf->mem,
            .offset = 0,
            .size = VK_WHOLE_SIZE,
        };
        vk->InvalidateMappedMemoryRanges(fv->s.hwctx->act_dev,
                                         1, &invalidate_data);
    }

    /* Calculate final size */
    pkt->size = 0;
    for (int i = 0; i < f->slice_count; i++) {
        sc = &((uint64_t *)results_data_buf->mapped_mem)[i*2];
        av_log(avctx, AV_LOG_DEBUG, "Slice %i size = %"PRIu64", "
                                    "src offset = %"PRIu64"\n",
               i, sc[0], sc[1]);

        fv->buf_regions[i] = (VkBufferCopy) {
            .srcOffset = sc[1],
            .dstOffset = pkt->size,
            .size = sc[0],
        };
        pkt->size += sc[0];
    }
    av_log(avctx, AV_LOG_VERBOSE, "Encoded data: %iMiB\n", pkt->size / (1024*1024));
    av_buffer_unref(&fd->results_data_ref); /* No need for this buffer anymore */

    /* Allocate packet */
    if ((err = ff_get_encode_buffer(avctx, pkt, pkt->size, 0)) < 0)
        return err;

    pkt->pts      = fd->pts;
    pkt->dts      = fd->pts;
    pkt->duration = fd->duration;
    pkt->flags   |= AV_PKT_FLAG_KEY * fd->key_frame;

    if (avctx->flags & AV_CODEC_FLAG_COPY_OPAQUE) {
        pkt->opaque          = fd->frame_opaque;
        pkt->opaque_ref      = fd->frame_opaque_ref;
        fd->frame_opaque_ref = NULL;
    }

    /* Try using host mapped memory transfers first */
    if (fv->s.extensions & FF_VK_EXT_EXTERNAL_HOST_MEMORY) {
        err = transfer_slices(avctx, fv->buf_regions, f->slice_count, fd,
                              pkt->data, pkt->buf);
        if (err >= 0)
            return err;
    }

    /* Invalidate slice/output data if needed */
    if (!(out_data_buf->flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        VkMappedMemoryRange invalidate_data = {
            .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .memory = out_data_buf->mem,
            .offset = 0,
            .size = VK_WHOLE_SIZE,
        };
        vk->InvalidateMappedMemoryRanges(fv->s.hwctx->act_dev,
                                         1, &invalidate_data);
    }

    /* Copy each slice */
    for (int i = 0; i < f->slice_count; i++) {
        VkBufferCopy *region = &fv->buf_regions[i];
        memcpy(pkt->data + region->dstOffset,
               out_data_buf->mapped_mem + region->srcOffset,
               region->size);
    }

    av_buffer_unref(&fd->out_data_ref);

    return 0;
}

static int vulkan_encode_ffv1_receive_packet(AVCodecContext *avctx,
                                             AVPacket *pkt)
{
    int err;
    VulkanEncodeFFv1Context *fv = avctx->priv_data;
    VulkanEncodeFFv1FrameData *fd;
    FFVkExecContext *exec;
    AVFrame *frame;

    while (1) {
        /* Roll an execution context */
        exec = ff_vk_exec_get(&fv->s, &fv->exec_pool);

        /* If it had a frame, immediately output it */
        if (exec->had_submission) {
            exec->had_submission = 0;
            fv->in_flight--;
            return get_packet(avctx, exec, pkt);
        }

        /* Get next frame to encode */
        frame = fv->frame;
        err = ff_encode_get_frame(avctx, frame);
        if (err < 0 && err != AVERROR_EOF) {
            return err;
        } else if (err == AVERROR_EOF) {
            if (!fv->in_flight)
                return err;
            continue;
        }

        /* Encode frame */
        fd = exec->opaque;
        fd->pts = frame->pts;
        fd->duration = frame->duration;
        if (avctx->flags & AV_CODEC_FLAG_COPY_OPAQUE) {
            fd->frame_opaque     = frame->opaque;
            fd->frame_opaque_ref = frame->opaque_ref;
            frame->opaque_ref    = NULL;
        }

        err = vulkan_encode_ffv1_submit_frame(avctx, exec, frame);
        av_frame_unref(frame);
        if (err < 0)
            return err;

        fv->in_flight++;
        if (fv->in_flight < fv->async_depth)
            return AVERROR(EAGAIN);
    }

    return 0;
}

static int init_indirect(AVCodecContext *avctx, enum AVPixelFormat sw_format)
{
    int err;
    VulkanEncodeFFv1Context *fv = avctx->priv_data;
    FFV1Context *f = &fv->ctx;
    AVHWFramesContext *frames_ctx;
    AVVulkanFramesContext *vk_frames;

    fv->intermediate_frames_ref = av_hwframe_ctx_alloc(fv->s.device_ref);
    if (!fv->intermediate_frames_ref)
        return AVERROR(ENOMEM);

    frames_ctx = (AVHWFramesContext *)fv->intermediate_frames_ref->data;
    frames_ctx->format    = AV_PIX_FMT_VULKAN;
    frames_ctx->sw_format = sw_format;
    frames_ctx->width     = fv->s.frames->width;
    frames_ctx->height    = f->num_v_slices*RGB_LINECACHE;

    vk_frames = frames_ctx->hwctx;
    vk_frames->tiling    = VK_IMAGE_TILING_OPTIMAL;
    vk_frames->usage     = VK_IMAGE_USAGE_STORAGE_BIT |
                           VK_IMAGE_USAGE_TRANSFER_DST_BIT;
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

    av_bprintf(&shd->src, "#define RGB_LINECACHE %i\n"                   ,RGB_LINECACHE);
    av_bprintf(&shd->src, "#define CONTEXT_SIZE %i\n"                    ,CONTEXT_SIZE);
    av_bprintf(&shd->src, "#define MAX_QUANT_TABLE_MASK 0x%x\n"          ,MAX_QUANT_TABLE_MASK);

    if (f->ac == AC_GOLOMB_RICE) {
        av_bprintf(&shd->src, "#define PB_UNALIGNED\n"                   );
        av_bprintf(&shd->src, "#define GOLOMB\n"                         );
    }

    if (fv->is_rgb)
        av_bprintf(&shd->src, "#define RGB\n");

    GLSLF(0, #define TYPE int%i_t                                        ,smp_bits);
    GLSLF(0, #define VTYPE2 i%ivec2                                      ,smp_bits);
    GLSLF(0, #define VTYPE3 i%ivec3                                      ,smp_bits);
    GLSLD(ff_source_rangecoder_comp);

    if (f->ac == AC_GOLOMB_RICE)
        GLSLD(ff_source_ffv1_vlc_comp);

    GLSLD(ff_source_ffv1_common_comp);
}

static int init_rct_search_shader(AVCodecContext *avctx, FFVkSPIRVCompiler *spv)
{
    int err;
    VulkanEncodeFFv1Context *fv = avctx->priv_data;
    FFV1Context *f = &fv->ctx;
    FFVulkanShader *shd = &fv->rct_search;
    FFVulkanDescriptorSetBinding *desc_set;

    uint8_t *spv_data;
    size_t spv_len;
    void *spv_opaque = NULL;

    RET(ff_vk_shader_init(&fv->s, shd, "ffv1_rct_search",
                          VK_SHADER_STAGE_COMPUTE_BIT,
                          (const char *[]) { "GL_EXT_buffer_reference",
                                             "GL_EXT_buffer_reference2",
                                             "GL_EXT_null_initializer" }, 3,
                          32, 32, 1,
                          0));

    /* Common codec header */
    GLSLD(ff_source_common_comp);

    GLSLC(0, layout(push_constant, scalar) uniform pushConstants {             );
    GLSLC(1,    ivec4 fmt_lut;                                                 );
    GLSLC(1,    int rct_offset;                                                );
    GLSLC(1,    uint8_t planar_rgb;                                            );
    GLSLC(1,    uint8_t transparency;                                          );
    GLSLC(1,    uint8_t key_frame;                                             );
    GLSLC(1,    uint8_t force_pcm;                                             );
    GLSLC(1,    uint8_t version;                                               );
    GLSLC(1,    uint8_t micro_version;                                         );
    GLSLC(1,    uint8_t padding[3];                                            );
    GLSLC(0, };                                                                );
    ff_vk_shader_add_push_const(shd, 0, sizeof(FFv1VkResetParameters),
                                VK_SHADER_STAGE_COMPUTE_BIT);

    av_bprintf(&shd->src, "#define MAX_QUANT_TABLES %i\n", MAX_QUANT_TABLES);
    av_bprintf(&shd->src, "#define MAX_CONTEXT_INPUTS %i\n", MAX_CONTEXT_INPUTS);
    av_bprintf(&shd->src, "#define MAX_QUANT_TABLE_SIZE %i\n", MAX_QUANT_TABLE_SIZE);

    /* Never used */
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
    RET(ff_vk_shader_add_descriptor_set(&fv->s, shd, desc_set, 2, 1, 1));

    define_shared_code(avctx, shd);

    desc_set = (FFVulkanDescriptorSetBinding []) {
        {
            .name        = "slice_data_buf",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .buf_content = "SliceContext slice_ctx",
            .buf_elems   = f->max_slice_count,
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

    GLSLD(ff_source_ffv1_rct_search_comp);

    RET(spv->compile_shader(&fv->s, spv, shd, &spv_data, &spv_len, "main",
                            &spv_opaque));
    RET(ff_vk_shader_link(&fv->s, shd, spv_data, spv_len, "main"));

    RET(ff_vk_shader_register_exec(&fv->s, &fv->exec_pool, shd));

fail:
    if (spv_opaque)
        spv->free_shader(spv, &spv_opaque);

    return err;
}

static int init_setup_shader(AVCodecContext *avctx, FFVkSPIRVCompiler *spv)
{
    int err;
    VulkanEncodeFFv1Context *fv = avctx->priv_data;
    FFV1Context *f = &fv->ctx;
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

    /* Common codec header */
    GLSLD(ff_source_common_comp);
    add_push_data(shd);

    av_bprintf(&shd->src, "#define MAX_QUANT_TABLES %i\n", MAX_QUANT_TABLES);
    av_bprintf(&shd->src, "#define MAX_CONTEXT_INPUTS %i\n", MAX_CONTEXT_INPUTS);
    av_bprintf(&shd->src, "#define MAX_QUANT_TABLE_SIZE %i\n", MAX_QUANT_TABLE_SIZE);
    av_bprintf(&shd->src, "#define FULL_RENORM\n");

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
            .buf_content = "SliceContext slice_ctx",
            .buf_elems   = f->max_slice_count,
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
    FFV1Context *f = &fv->ctx;
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

    /* Common codec header */
    GLSLD(ff_source_common_comp);

    GLSLC(0, layout(push_constant, scalar) uniform pushConstants {             );
    GLSLF(1,    uint context_count[%i];                                        ,MAX_QUANT_TABLES);
    GLSLC(1,    u8buf slice_state;                                             );
    GLSLC(1,    uint plane_state_size;                                         );
    GLSLC(1,    uint8_t codec_planes;                                          );
    GLSLC(1,    uint8_t key_frame;                                             );
    GLSLC(1,    uint8_t version;                                               );
    GLSLC(1,    uint8_t micro_version;                                         );
    GLSLC(1,    uint8_t padding[1];                                            );
    GLSLC(0, };                                                                );
    ff_vk_shader_add_push_const(shd, 0, sizeof(FFv1VkResetParameters),
                                VK_SHADER_STAGE_COMPUTE_BIT);

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
            .buf_content = "SliceContext slice_ctx",
            .buf_elems   = f->max_slice_count,
        },
    };
    RET(ff_vk_shader_add_descriptor_set(&fv->s, shd, desc_set, 1, 0, 0));

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

static int init_encode_shader(AVCodecContext *avctx, FFVkSPIRVCompiler *spv)
{
    int err;
    VulkanEncodeFFv1Context *fv = avctx->priv_data;
    FFV1Context *f = &fv->ctx;
    FFVulkanShader *shd = &fv->enc;
    FFVulkanDescriptorSetBinding *desc_set;

    uint8_t *spv_data;
    size_t spv_len;
    void *spv_opaque = NULL;
    int use_cached_reader = fv->ctx.ac != AC_GOLOMB_RICE;

    RET(ff_vk_shader_init(&fv->s, shd, "ffv1_enc",
                          VK_SHADER_STAGE_COMPUTE_BIT,
                          (const char *[]) { "GL_EXT_buffer_reference",
                                             "GL_EXT_buffer_reference2" }, 2,
                          use_cached_reader ? CONTEXT_SIZE : 1, 1, 1,
                          0));

    /* Common codec header */
    GLSLD(ff_source_common_comp);

    add_push_data(shd);

    av_bprintf(&shd->src, "#define MAX_QUANT_TABLES %i\n", MAX_QUANT_TABLES);
    av_bprintf(&shd->src, "#define MAX_CONTEXT_INPUTS %i\n", MAX_CONTEXT_INPUTS);
    av_bprintf(&shd->src, "#define MAX_QUANT_TABLE_SIZE %i\n", MAX_QUANT_TABLE_SIZE);

    if (use_cached_reader)
        av_bprintf(&shd->src, "#define CACHED_SYMBOL_READER 1\n");

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
            .buf_content = "SliceContext slice_ctx",
            .buf_elems   = f->max_slice_count,
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
            .name        = "results_data_buf",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .mem_quali   = "writeonly",
            .buf_content = "uint64_t slice_results[2048];",
        },
        { /* place holder for desc_set[3] */
        },
    };
    if (fv->is_rgb) {
        AVHWFramesContext *intermediate_frames_ctx;
        intermediate_frames_ctx = (AVHWFramesContext *)fv->intermediate_frames_ref->data;
        desc_set[3] = (FFVulkanDescriptorSetBinding) {
            .name       = "tmp",
            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .dimensions = 2,
            .mem_layout = ff_vk_shader_rep_fmt(intermediate_frames_ctx->sw_format,
                                               FF_VK_REP_NATIVE),
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
        };
    }
    RET(ff_vk_shader_add_descriptor_set(&fv->s, shd, desc_set, 3 + fv->is_rgb, 0, 0));

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

static av_cold int vulkan_encode_ffv1_init(AVCodecContext *avctx)
{
    int err;
    size_t maxsize, max_heap_size, max_host_size;
    VulkanEncodeFFv1Context *fv = avctx->priv_data;
    FFV1Context *f = &fv->ctx;
    FFVkSPIRVCompiler *spv;

    if ((err = ff_ffv1_common_init(avctx, f)) < 0)
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

    /* We target version 4.3 */
    if (f->version == 4 && f->micro_version > 4)
        f->micro_version = 3;

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

        if (f->num_h_slices <= 0 && f->num_v_slices <= 0) {
            if (avctx->slices) {
                err = ff_ffv1_encode_determine_slices(avctx);
                if (err < 0)
                    return err;
            } else {
                f->num_h_slices = 32;
                f->num_v_slices = 32;
            }
        } else if (f->num_h_slices && f->num_v_slices <= 0) {
            f->num_v_slices = MAX_SLICES / f->num_h_slices;
        } else if (f->num_v_slices && f->num_h_slices <= 0) {
            f->num_h_slices = MAX_SLICES / f->num_v_slices;
        }

        f->num_h_slices = FFMIN(f->num_h_slices, avctx->width);
        f->num_v_slices = FFMIN(f->num_v_slices, avctx->height);

        if (f->num_h_slices * f->num_v_slices > MAX_SLICES) {
            av_log(avctx, AV_LOG_ERROR, "Too many slices (%i), maximum supported "
                                        "by the standard is %i\n",
                   f->num_h_slices * f->num_v_slices, MAX_SLICES);
            return AVERROR_PATCHWELCOME;
        }
    }

    f->max_slice_count = f->num_h_slices * f->num_v_slices;

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
        } else if (f->ac == AC_GOLOMB_RICE) {
            av_log(avctx, AV_LOG_ERROR, "PCM coding requires range coding\n");
            return AVERROR_INVALIDDATA;
        }
    }

    /* Init Vulkan */
    err = ff_vk_init(&fv->s, avctx, NULL, avctx->hw_frames_ctx);
    if (err < 0)
        return err;

    fv->qf = ff_vk_qf_find(&fv->s, VK_QUEUE_COMPUTE_BIT, 0);
    if (!fv->qf) {
        av_log(avctx, AV_LOG_ERROR, "Device has no compute queues!\n");
        return err;
    }

    /* Try to measure VRAM size */
    max_heap_size = 0;
    max_host_size = 0;
    for (int i = 0; i < fv->s.mprops.memoryHeapCount; i++) {
        if (fv->s.mprops.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
            max_heap_size = FFMAX(fv->max_heap_size,
                                  fv->s.mprops.memoryHeaps[i].size);
        if (!(fv->s.mprops.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT))
            max_host_size = FFMAX(max_host_size,
                                  fv->s.mprops.memoryHeaps[i].size);
    }
    fv->max_heap_size = max_heap_size;

    maxsize = ff_ffv1_encode_buffer_size(avctx);
    if (maxsize > fv->s.props_11.maxMemoryAllocationSize) {
        av_log(avctx, AV_LOG_WARNING, "Encoding buffer size (%zu) larger "
                                      "than maximum device allocation (%zu), clipping\n",
               maxsize, fv->s.props_11.maxMemoryAllocationSize);
        maxsize = fv->s.props_11.maxMemoryAllocationSize;
    }

    if (max_heap_size < maxsize) {
        av_log(avctx, AV_LOG_WARNING, "Encoding buffer (%zu) larger than VRAM (%zu), "
                                      "using host memory (slower)\n",
               maxsize, fv->max_heap_size);

        /* Keep 1/2th of RAM as headroom */
        max_heap_size = max_host_size - (max_host_size >> 1);
    } else {
        /* Keep 1/8th of VRAM as headroom */
        max_heap_size = max_heap_size - (max_heap_size >> 3);
    }

    av_log(avctx, AV_LOG_INFO, "Async buffers: %zuMiB per context, %zuMiB total, depth: %i\n",
           maxsize / (1024*1024),
           (fv->async_depth * maxsize) / (1024*1024),
           fv->async_depth);

    err = ff_vk_exec_pool_init(&fv->s, fv->qf, &fv->exec_pool,
                               fv->async_depth,
                               0, 0, 0, NULL);
    if (err < 0)
        return err;

    fv->transfer_qf = ff_vk_qf_find(&fv->s, VK_QUEUE_TRANSFER_BIT, 0);
    if (!fv->transfer_qf) {
        av_log(avctx, AV_LOG_ERROR, "Device has no transfer queues!\n");
        return err;
    }

    err = ff_vk_exec_pool_init(&fv->s, fv->transfer_qf, &fv->transfer_exec_pool,
                               1,
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

    /* Init rct search shader */
    fv->optimize_rct = fv->is_rgb && f->version >= 4 &&
                       !fv->force_pcm && fv->optimize_rct;
    if (fv->optimize_rct) {
        err = init_rct_search_shader(avctx, spv);
        if (err < 0) {
            spv->uninit(&spv);
            return err;
        }
    }

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

    if (fv->is_rgb) {
        enum AVPixelFormat intermediate_fmt = get_supported_rgb_buffer_fmt(avctx);
        if (intermediate_fmt == AV_PIX_FMT_NONE) {
            av_log(avctx, AV_LOG_ERROR, "Unable to find a supported compatible "
                                        "pixel format for RCT buffer!\n");
            return AVERROR(ENOTSUP);
        }

        RET(init_indirect(avctx, intermediate_fmt));
    }

    /* Encode shader */
    err = init_encode_shader(avctx, spv);
    if (err < 0) {
        spv->uninit(&spv);
        return err;
    }

    spv->uninit(&spv);

    /* Range coder data */
    err = ff_ffv1_vk_init_state_transition_data(&fv->s,
                                                &fv->rangecoder_static_buf,
                                                f);
    if (err < 0)
        return err;

    /* Quantization table data */
    err = ff_ffv1_vk_init_quant_table_data(&fv->s,
                                           &fv->quant_buf,
                                           f);
    if (err < 0)
        return err;

    /* CRC table buffer */
    err = ff_ffv1_vk_init_crc_table_data(&fv->s,
                                         &fv->crc_tab_buf,
                                         f);
    if (err < 0)
        return err;

    /* Update setup global descriptors */
    RET(ff_vk_shader_update_desc_buffer(&fv->s, &fv->exec_pool.contexts[0],
                                        &fv->setup, 0, 0, 0,
                                        &fv->rangecoder_static_buf,
                                        0, fv->rangecoder_static_buf.size,
                                        VK_FORMAT_UNDEFINED));

    /* Update encode global descriptors */
    RET(ff_vk_shader_update_desc_buffer(&fv->s, &fv->exec_pool.contexts[0],
                                        &fv->enc, 0, 0, 0,
                                        &fv->rangecoder_static_buf,
                                        0, fv->rangecoder_static_buf.size,
                                        VK_FORMAT_UNDEFINED));
    RET(ff_vk_shader_update_desc_buffer(&fv->s, &fv->exec_pool.contexts[0],
                                        &fv->enc, 0, 1, 0,
                                        &fv->quant_buf,
                                        0, fv->quant_buf.size,
                                        VK_FORMAT_UNDEFINED));
    RET(ff_vk_shader_update_desc_buffer(&fv->s, &fv->exec_pool.contexts[0],
                                        &fv->enc, 0, 2, 0,
                                        &fv->crc_tab_buf,
                                        0, fv->crc_tab_buf.size,
                                        VK_FORMAT_UNDEFINED));

    /* Temporary frame */
    fv->frame = av_frame_alloc();
    if (!fv->frame)
        return AVERROR(ENOMEM);

    /* Async data pool */
    fv->async_depth = fv->exec_pool.pool_size;
    fv->exec_ctx_info = av_calloc(fv->async_depth, sizeof(*fv->exec_ctx_info));
    if (!fv->exec_ctx_info)
        return AVERROR(ENOMEM);
    for (int i = 0; i < fv->async_depth; i++)
        fv->exec_pool.contexts[i].opaque = &fv->exec_ctx_info[i];

    fv->buf_regions = av_malloc_array(f->max_slice_count, sizeof(*fv->buf_regions));
    if (!fv->buf_regions)
        return AVERROR(ENOMEM);

fail:
    return err;
}

static av_cold int vulkan_encode_ffv1_close(AVCodecContext *avctx)
{
    VulkanEncodeFFv1Context *fv = avctx->priv_data;

    ff_vk_exec_pool_free(&fv->s, &fv->exec_pool);
    ff_vk_exec_pool_free(&fv->s, &fv->transfer_exec_pool);

    ff_vk_shader_free(&fv->s, &fv->enc);
    ff_vk_shader_free(&fv->s, &fv->reset);
    ff_vk_shader_free(&fv->s, &fv->setup);
    ff_vk_shader_free(&fv->s, &fv->rct_search);

    if (fv->exec_ctx_info) {
        for (int i = 0; i < fv->async_depth; i++) {
            VulkanEncodeFFv1FrameData *fd = &fv->exec_ctx_info[i];
            av_buffer_unref(&fd->out_data_ref);
            av_buffer_unref(&fd->results_data_ref);
            av_buffer_unref(&fd->frame_opaque_ref);
        }
    }
    av_free(fv->exec_ctx_info);

    av_buffer_unref(&fv->intermediate_frames_ref);

    av_buffer_pool_uninit(&fv->results_data_pool);

    av_buffer_pool_uninit(&fv->out_data_pool);

    av_buffer_unref(&fv->keyframe_slice_data_ref);
    av_buffer_pool_uninit(&fv->slice_data_pool);

    ff_vk_free_buf(&fv->s, &fv->quant_buf);
    ff_vk_free_buf(&fv->s, &fv->rangecoder_static_buf);
    ff_vk_free_buf(&fv->s, &fv->crc_tab_buf);

    av_free(fv->buf_regions);
    av_frame_free(&fv->frame);
    ff_vk_uninit(&fv->s);

    return 0;
}

#define OFFSET(x) offsetof(VulkanEncodeFFv1Context, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption vulkan_encode_ffv1_options[] = {
    { "slicecrc", "Protect slices with CRCs", OFFSET(ctx.ec), AV_OPT_TYPE_INT,
            { .i64 = -1 }, -1, 2, VE },
    { "context", "Context model", OFFSET(ctx.context_model), AV_OPT_TYPE_INT,
            { .i64 = 0 }, 0, 1, VE },
    { "coder", "Coder type", OFFSET(ctx.ac), AV_OPT_TYPE_INT,
            { .i64 = AC_RANGE_CUSTOM_TAB }, -2, 2, VE, .unit = "coder" },
        { "rice", "Golomb rice", 0, AV_OPT_TYPE_CONST,
            { .i64 = AC_GOLOMB_RICE }, INT_MIN, INT_MAX, VE, .unit = "coder" },
        { "range_def", "Range with default table", 0, AV_OPT_TYPE_CONST,
            { .i64 = AC_RANGE_DEFAULT_TAB_FORCE }, INT_MIN, INT_MAX, VE, .unit = "coder" },
        { "range_tab", "Range with custom table", 0, AV_OPT_TYPE_CONST,
            { .i64 = AC_RANGE_CUSTOM_TAB }, INT_MIN, INT_MAX, VE, .unit = "coder" },
    { "qtable", "Quantization table", OFFSET(ctx.qtable), AV_OPT_TYPE_INT,
            { .i64 = -1 }, -1, 2, VE , .unit = "qtable"},
        { "default", NULL, 0, AV_OPT_TYPE_CONST,
            { .i64 = QTABLE_DEFAULT }, INT_MIN, INT_MAX, VE, .unit = "qtable" },
        { "8bit", NULL, 0, AV_OPT_TYPE_CONST,
            { .i64 = QTABLE_8BIT }, INT_MIN, INT_MAX, VE, .unit = "qtable" },
        { "greater8bit", NULL, 0, AV_OPT_TYPE_CONST,
            { .i64 = QTABLE_GT8BIT }, INT_MIN, INT_MAX, VE, .unit = "qtable" },

    { "slices_h", "Number of horizontal slices", OFFSET(num_h_slices), AV_OPT_TYPE_INT,
            { .i64 = -1 }, -1, MAX_SLICES, VE },
    { "slices_v", "Number of vertical slices", OFFSET(num_v_slices), AV_OPT_TYPE_INT,
            { .i64 = -1 }, -1, MAX_SLICES, VE },

    { "force_pcm", "Code all slices with no prediction", OFFSET(force_pcm), AV_OPT_TYPE_BOOL,
            { .i64 = 0 }, 0, 1, VE },

    { "rct_search", "Run a search for RCT parameters (level 4 only)", OFFSET(optimize_rct), AV_OPT_TYPE_BOOL,
            { .i64 = 1 }, 0, 1, VE },

    { "async_depth", "Internal parallelization depth", OFFSET(async_depth), AV_OPT_TYPE_INT,
            { .i64 = 1 }, 1, INT_MAX, VE },

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
    FF_CODEC_RECEIVE_PACKET_CB(&vulkan_encode_ffv1_receive_packet),
    .close          = &vulkan_encode_ffv1_close,
    .p.priv_class   = &vulkan_encode_ffv1_class,
    .p.capabilities = AV_CODEC_CAP_DELAY |
                      AV_CODEC_CAP_HARDWARE |
                      AV_CODEC_CAP_DR1 |
                      AV_CODEC_CAP_ENCODER_FLUSH |
                      AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP | FF_CODEC_CAP_EOF_FLUSH,
    .defaults       = vulkan_encode_ffv1_defaults,
    CODEC_PIXFMTS(AV_PIX_FMT_VULKAN),
    .hw_configs     = vulkan_encode_ffv1_hw_configs,
    .p.wrapper_name = "vulkan",
};
