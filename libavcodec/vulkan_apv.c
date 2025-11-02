/*
 * Copyright (c) 2025 Lynne <dev@lynne.ee>
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

#include "vulkan_decode.h"
#include "hwaccel_internal.h"

#include "apv_decode.h"
#include "libavutil/mem.h"

extern const unsigned char ff_apv_decode_comp_spv_data[];
extern const unsigned int ff_apv_decode_comp_spv_len;

extern const unsigned char ff_apv_idct_comp_spv_data[];
extern const unsigned int ff_apv_idct_comp_spv_len;

const FFVulkanDecodeDescriptor ff_vk_dec_apv_desc = {
    .codec_id         = AV_CODEC_ID_APV,
    .queue_flags      = VK_QUEUE_COMPUTE_BIT,
};

typedef struct APVVulkanDecodePicture {
    FFVulkanDecodePicture vp;

    AVBufferRef *frame_data_buf;
    uint32_t    *frame_data;
    int          tile_num;
} APVVulkanDecodePicture;

typedef struct APVVulkanDecodeContext {
    FFVulkanShader decode;
    FFVulkanShader idct;

    AVBufferPool *frame_data_pool;
} APVVulkanDecodeContext;

typedef struct DecodePushData {
    VkDeviceAddress tile_data;
    int tile_count[2];
    int log2_chroma_sub[2];
    int components;
    int bit_depth;
} DecodePushData;

static int vk_apv_start_frame(AVCodecContext          *avctx,
                              const AVBufferRef       *buffer_ref,
                              av_unused const uint8_t *buffer,
                              av_unused uint32_t       size)
{
    int err;
    APVDecodeContext *apv = avctx->priv_data;
    FFVulkanDecodeContext *dec = avctx->internal->hwaccel_priv_data;
    FFVulkanDecodeShared *ctx = dec->shared_ctx;
    APVVulkanDecodeContext *apvvk = ctx->sd_ctx;

    APVVulkanDecodePicture *apvvp = apv->hwaccel_picture_private;
    FFVulkanDecodePicture *vp = &apvvp->vp;

    /* Host map the input tile data if supported */
    if (ctx->s.extensions & FF_VK_EXT_EXTERNAL_HOST_MEMORY)
        ff_vk_host_map_buffer(&ctx->s, &vp->slices_buf, buffer_ref->data,
                              buffer_ref,
                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                              VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);

    /* Allocate frame data buffer */
    int fd_size = (2*4*APV_MAX_TILE_COUNT)*APV_MAX_NUM_COMP +
                  (64 + APV_MAX_TILE_COUNT)*APV_MAX_NUM_COMP +
                  (APV_MAX_TILE_COLS + 1 + APV_MAX_TILE_ROWS + 1)*2;

    err = ff_vk_get_pooled_buffer(&ctx->s, &apvvk->frame_data_pool,
                                  &apvvp->frame_data_buf,
                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                  VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                  NULL, fd_size,
                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    if (err < 0)
        return err;

    /* Frame data */
    FFVkBuffer *frame_data = (FFVkBuffer *)apvvp->frame_data_buf->data;
    uint8_t *fd = frame_data->mapped_mem;

    fd += 2*4*APV_MAX_TILE_COUNT*APV_MAX_NUM_COMP; /* Tile offsets go first */

    /* per-component qmatrix and QPs */
    for (int i = 0; i < APV_MAX_NUM_COMP; i++)
        memcpy(fd + 64*i,
               apv->cur_raw_frame->frame_header.quantization_matrix.q_matrix[i],
               64);
    fd += 64*APV_MAX_NUM_COMP;

    for (int i = 0; i < APV_MAX_NUM_COMP; i++) {
        for (int j = 0; j < APV_MAX_TILE_COUNT; j++)
            fd[j] = apv->cur_raw_frame->tile[j].tile_header.tile_qp[i];
        fd += APV_MAX_TILE_COUNT;
    }

    /* tile col/row offset */
    memcpy(fd, apv->tile_info.col_starts, (APV_MAX_TILE_COLS+1)*2);
    fd += (APV_MAX_TILE_COLS+1)*2;
    memcpy(fd, apv->tile_info.row_starts, (APV_MAX_TILE_ROWS+1)*2);

    /* Prepare frame to be used */
    err = ff_vk_decode_prepare_frame_sdr(dec, apv->output_frame, vp, 1,
                                         FF_VK_REP_NATIVE, 0);
    if (err < 0)
        return err;

    return 0;
}

static int vk_apv_decode_slice(AVCodecContext *avctx,
                               const uint8_t  *data,
                               uint32_t        size)
{
    APVDecodeContext *apv = avctx->priv_data;

    APVVulkanDecodePicture *apvvp = apv->hwaccel_picture_private;
    FFVulkanDecodePicture *vp = &apvvp->vp;

    FFVkBuffer *frame_data = (FFVkBuffer *)apvvp->frame_data_buf->data;
    FFVkBuffer *slices_buf = vp->slices_buf ? (FFVkBuffer *)vp->slices_buf->data : NULL;

    if (slices_buf && slices_buf->host_ref) {
        AV_WN32(frame_data->mapped_mem + (2*apvvp->tile_num + 0)*sizeof(uint32_t),
                data - slices_buf->mapped_mem);
        AV_WN32(frame_data->mapped_mem + (2*apvvp->tile_num + 1)*sizeof(uint32_t),
                size);

        apvvp->tile_num++;
    } else {
        int err = ff_vk_decode_add_slice(avctx, vp, data, size, 0,
                                         &apvvp->tile_num,
                                         (const uint32_t **)&apvvp->frame_data);
        if (err < 0)
            return err;

        AV_WN32(frame_data->mapped_mem + (2*(apvvp->tile_num - 1) + 0)*sizeof(uint32_t),
                apvvp->frame_data[apvvp->tile_num - 1]);
        AV_WN32(frame_data->mapped_mem + (2*(apvvp->tile_num - 1) + 1)*sizeof(uint32_t),
                size);
    }

    return 0;
}

static int vk_apv_end_frame(AVCodecContext *avctx)
{
    int err;
    APVDecodeContext *apv = avctx->priv_data;
    const CodedBitstreamAPVContext *apv_cbc = apv->cbc->priv_data;
    FFVulkanDecodeContext *dec = avctx->internal->hwaccel_priv_data;
    FFVulkanDecodeShared *ctx = dec->shared_ctx;
    APVVulkanDecodeContext *apvvk = ctx->sd_ctx;
    FFVulkanFunctions *vk = &ctx->s.vkfn;

    APVVulkanDecodePicture *apvvp = apv->hwaccel_picture_private;
    FFVulkanDecodePicture *vp = &apvvp->vp;

    FFVkBuffer *slices_buf = (FFVkBuffer *)vp->slices_buf->data;
    FFVkBuffer *frame_data_buf = (FFVkBuffer *)apvvp->frame_data_buf->data;

    AVHWFramesContext *hwfc = (AVHWFramesContext *)avctx->hw_frames_ctx->data;
    enum AVPixelFormat sw_format = hwfc->sw_format;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(sw_format);

    VkImageMemoryBarrier2 img_bar[8];
    int nb_img_bar = 0;

    FFVkExecContext *exec = ff_vk_exec_get(&ctx->s, &ctx->exec_pool);
    ff_vk_exec_start(&ctx->s, exec);

    /* Make sure the buffer is flushed */
    RET(ff_vk_flush_buffer(&ctx->s, frame_data_buf, 0, frame_data_buf->size, 1));

    /* Prepare deps */
    RET(ff_vk_exec_add_dep_frame(&ctx->s, exec, apv->output_frame,
                                 VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT));

    err = ff_vk_exec_mirror_sem_value(&ctx->s, exec, &vp->sem, &vp->sem_value,
                                      apv->output_frame);
    if (err < 0)
        return err;

    RET(ff_vk_exec_add_dep_buf(&ctx->s, exec, &vp->slices_buf, 1, 0));
    vp->slices_buf = NULL;
    RET(ff_vk_exec_add_dep_buf(&ctx->s, exec, &apvvp->frame_data_buf, 1, 0));
    apvvp->frame_data_buf = NULL;

    AVVkFrame *vkf = (AVVkFrame *)apv->output_frame->data[0];
    vkf->layout[0] = VK_IMAGE_LAYOUT_UNDEFINED;
    vkf->access[0] = VK_ACCESS_2_NONE;

    ff_vk_frame_barrier(&ctx->s, exec, apv->output_frame,
                        img_bar, &nb_img_bar,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        VK_PIPELINE_STAGE_2_CLEAR_BIT,
                        VK_ACCESS_2_TRANSFER_WRITE_BIT,
                        VK_IMAGE_LAYOUT_GENERAL,
                        VK_QUEUE_FAMILY_IGNORED);
    vk->CmdPipelineBarrier2(exec->buf, &(VkDependencyInfo) {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pImageMemoryBarriers = img_bar,
        .imageMemoryBarrierCount = nb_img_bar,
    });
    nb_img_bar = 0;

    /* Zero frame */
    for (int i = 0; i < ff_vk_count_images(vkf); i++)
        vk->CmdClearColorImage(exec->buf, vkf->img[i],
                               VK_IMAGE_LAYOUT_GENERAL,
                               &((VkClearColorValue) { 0 }),
                               1, &((VkImageSubresourceRange) {
                                   .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                   .levelCount = 1,
                                   .layerCount = 1,
                               }));

    /* Wait for the frame to get zeroed out before continuing */
    ff_vk_frame_barrier(&ctx->s, exec, apv->output_frame, img_bar, &nb_img_bar,
                        VK_PIPELINE_STAGE_2_CLEAR_BIT,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                        VK_IMAGE_LAYOUT_GENERAL,
                        VK_QUEUE_FAMILY_IGNORED);
    vk->CmdPipelineBarrier2(exec->buf, &(VkDependencyInfo) {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pImageMemoryBarriers = img_bar,
        .imageMemoryBarrierCount = nb_img_bar,
    });
    nb_img_bar = 0;

    /* Setup push data */
    DecodePushData pd = (DecodePushData) {
        .tile_data = slices_buf->address,
        .tile_count = { apv->tile_info.tile_cols, apv->tile_info.tile_rows },
        .log2_chroma_sub = { desc->log2_chroma_w, desc->log2_chroma_h },
        .components = desc->nb_components,
        .bit_depth = apv_cbc->bit_depth,
    };

    /* Decoding */
    ff_vk_shader_update_img_array(&ctx->s, exec, &apvvk->decode,
                                  apv->output_frame, vp->view.out,
                                  0, 0,
                                  VK_IMAGE_LAYOUT_GENERAL,
                                  VK_NULL_HANDLE);
    ff_vk_shader_update_desc_buffer(&ctx->s, exec, &apvvk->decode,
                                    0, 1, 0,
                                    frame_data_buf,
                                    0, frame_data_buf->size,
                                    VK_FORMAT_UNDEFINED);

    ff_vk_exec_bind_shader(&ctx->s, exec, &apvvk->decode);
    ff_vk_shader_update_push_const(&ctx->s, exec, &apvvk->decode,
                                   VK_SHADER_STAGE_COMPUTE_BIT,
                                   0, sizeof(pd), &pd);

    vk->CmdDispatch(exec->buf,
                    apv->tile_info.tile_cols, apv->tile_info.tile_rows,
                    desc->nb_components);

    /* Wait for all decoding to finish */
    ff_vk_frame_barrier(&ctx->s, exec, apv->output_frame, img_bar, &nb_img_bar,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                        VK_IMAGE_LAYOUT_GENERAL,
                        VK_QUEUE_FAMILY_IGNORED);
    vk->CmdPipelineBarrier2(exec->buf, &(VkDependencyInfo) {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pImageMemoryBarriers = img_bar,
        .imageMemoryBarrierCount = nb_img_bar,
    });
    nb_img_bar = 0;

    /* iDCT */
    ff_vk_shader_update_img_array(&ctx->s, exec, &apvvk->idct,
                                  apv->output_frame, vp->view.out,
                                  0, 0,
                                  VK_IMAGE_LAYOUT_GENERAL,
                                  VK_NULL_HANDLE);
    ff_vk_shader_update_desc_buffer(&ctx->s, exec, &apvvk->idct,
                                    0, 1, 0,
                                    frame_data_buf,
                                    0, frame_data_buf->size,
                                    VK_FORMAT_UNDEFINED);

    ff_vk_exec_bind_shader(&ctx->s, exec, &apvvk->idct);
    ff_vk_shader_update_push_const(&ctx->s, exec, &apvvk->idct,
                                   VK_SHADER_STAGE_COMPUTE_BIT,
                                   0, sizeof(pd), &pd);

    /* one workgroup per group of 8 horizontally adjacent transform blocks,
     * in the luma basis coords, in case a block is OOB writes/reads are ignored */
    int idct_cx = 0, idct_by = 0;
    for (int comp = 0; comp < desc->nb_components; comp++) {
        int sw = (comp == 0) ? 0 : desc->log2_chroma_w;
        int sh = (comp == 0) ? 0 : desc->log2_chroma_h;
        int bx = (avctx->coded_width  + (1 << (3 + sw)) - 1) >> (3 + sw);
        int by = (avctx->coded_height + (1 << (3 + sh)) - 1) >> (3 + sh);
        idct_cx = FFMAX(idct_cx, (bx + 7) >> 3);
        idct_by = FFMAX(idct_by, by);
    }
    vk->CmdDispatch(exec->buf, idct_cx, idct_by, desc->nb_components);

    err = ff_vk_exec_submit(&ctx->s, exec);
    if (err < 0)
        return err;

fail:
    return 0;
}

static int init_decode_shader(AVCodecContext *avctx, FFVulkanContext *s,
                              FFVkExecPool *pool, FFVulkanShader *shd)
{
    int err;
    AVHWFramesContext *dec_frames_ctx;
    dec_frames_ctx = (AVHWFramesContext *)avctx->hw_frames_ctx->data;

    ff_vk_shader_load(shd, VK_SHADER_STAGE_COMPUTE_BIT, NULL,
                      (uint32_t []) { 1, 1, 1 }, 0);
    ff_vk_shader_add_push_const(shd, 0, sizeof(DecodePushData),
                                VK_SHADER_STAGE_COMPUTE_BIT);

    const FFVulkanDescriptorSetBinding desc_set[] = {
        {
            .name       = "dst",
            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .elems      = av_pix_fmt_count_planes(dec_frames_ctx->sw_format),
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        {
            .name        = "frame_data_buf",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
        }
    };
    ff_vk_shader_add_descriptor_set(s, shd, desc_set, 2, 0, 0);

    RET(ff_vk_shader_link(s, shd,
                          ff_apv_decode_comp_spv_data,
                          ff_apv_decode_comp_spv_len, "main"));

    RET(ff_vk_shader_register_exec(s, pool, shd));

fail:
    return err;
}

static int init_idct_shader(AVCodecContext *avctx, FFVulkanContext *s,
                            FFVkExecPool *pool, FFVulkanShader *shd)
{
    int err;
    AVHWFramesContext *dec_frames_ctx;
    dec_frames_ctx = (AVHWFramesContext *)avctx->hw_frames_ctx->data;

    SPEC_LIST_CREATE(sl, 1 + 64, (1 + 64)*sizeof(uint32_t))
    SPEC_LIST_ADD(sl, 16, 32, 8); /* nb_blocks per workgroup */

    const double idct_8_scales[8] = {
        cos(4.0*M_PI/16.0) / 2.0, cos(1.0*M_PI/16.0) / 2.0,
        cos(2.0*M_PI/16.0) / 2.0, cos(3.0*M_PI/16.0) / 2.0,
        cos(4.0*M_PI/16.0) / 2.0, cos(5.0*M_PI/16.0) / 2.0,
        cos(6.0*M_PI/16.0) / 2.0, cos(7.0*M_PI/16.0) / 2.0,
    };
    for (int i = 0; i < 64; i++)
        SPEC_LIST_ADD(sl, 18 + i, 32,
                      av_float2int(idct_8_scales[i >> 3]*idct_8_scales[i & 7]));

    ff_vk_shader_load(shd, VK_SHADER_STAGE_COMPUTE_BIT, sl,
                      (uint32_t []) { 32, 2, 1 }, 0);
    ff_vk_shader_add_push_const(shd, 0, sizeof(DecodePushData),
                                VK_SHADER_STAGE_COMPUTE_BIT);

    FFVulkanDescriptorSetBinding desc_set[] = {
        {
            .name       = "dst",
            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
            .elems      = av_pix_fmt_count_planes(dec_frames_ctx->sw_format),
        },
        {
            .name        = "frame_data_buf",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };
    ff_vk_shader_add_descriptor_set(s, shd, desc_set, 2, 0, 0);

    RET(ff_vk_shader_link(s, shd,
                          ff_apv_idct_comp_spv_data,
                          ff_apv_idct_comp_spv_len, "main"));

    RET(ff_vk_shader_register_exec(s, pool, shd));

fail:
    return err;
}

static void vk_decode_apv_uninit(FFVulkanDecodeShared *ctx)
{
    APVVulkanDecodeContext *apvvk = ctx->sd_ctx;

    ff_vk_shader_free(&ctx->s, &apvvk->decode);
    ff_vk_shader_free(&ctx->s, &apvvk->idct);

    av_buffer_pool_uninit(&apvvk->frame_data_pool);

    av_freep(&apvvk);
}

static int vk_decode_apv_init(AVCodecContext *avctx)
{
    int err;
    FFVulkanDecodeContext *dec = avctx->internal->hwaccel_priv_data;

    err = ff_vk_decode_init(avctx);
    if (err < 0)
        return err;

    FFVulkanDecodeShared *ctx = dec->shared_ctx;
    APVVulkanDecodeContext *apvvk = ctx->sd_ctx = av_mallocz(sizeof(*apvvk));
    if (!apvvk) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    ctx->sd_ctx_free = &vk_decode_apv_uninit;

    RET(init_decode_shader(avctx, &ctx->s, &ctx->exec_pool,
                           &apvvk->decode));

    RET(init_idct_shader(avctx, &ctx->s, &ctx->exec_pool,
                         &apvvk->idct));

fail:
    return err;
}

static void vk_apv_free_frame_priv(AVRefStructOpaque _hwctx, void *data)
{
    AVHWDeviceContext *dev_ctx = _hwctx.nc;

    APVVulkanDecodePicture *apvvp = data;
    FFVulkanDecodePicture *vp = &apvvp->vp;

    ff_vk_decode_free_frame(dev_ctx, vp);

    av_buffer_unref(&apvvp->frame_data_buf);
}

const FFHWAccel ff_apv_vulkan_hwaccel = {
    .p.name                = "apv_vulkan",
    .p.type                = AVMEDIA_TYPE_VIDEO,
    .p.id                  = AV_CODEC_ID_APV,
    .p.pix_fmt             = AV_PIX_FMT_VULKAN,
    .start_frame           = &vk_apv_start_frame,
    .decode_slice          = &vk_apv_decode_slice,
    .end_frame             = &vk_apv_end_frame,
    .free_frame_priv       = &vk_apv_free_frame_priv,
    .frame_priv_data_size  = sizeof(APVVulkanDecodePicture),
    .init                  = &vk_decode_apv_init,
    .update_thread_context = &ff_vk_update_thread_context,
    .uninit                = &ff_vk_decode_uninit,
    .frame_params          = &ff_vk_frame_params,
    .priv_data_size        = sizeof(FFVulkanDecodeContext),
    .caps_internal         = HWACCEL_CAP_ASYNC_SAFE | HWACCEL_CAP_THREAD_SAFE,
};
