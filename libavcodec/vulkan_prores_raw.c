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

#include "prores_raw.h"
#include "libavutil/vulkan_spirv.h"
#include "libavutil/mem.h"

extern const char *ff_source_common_comp;
extern const char *ff_source_prores_raw_decode_comp;
extern const char *ff_source_prores_raw_idct_comp;

const FFVulkanDecodeDescriptor ff_vk_dec_prores_raw_desc = {
    .codec_id         = AV_CODEC_ID_PRORES_RAW,
    .decode_extension = FF_VK_EXT_PUSH_DESCRIPTOR,
    .queue_flags      = VK_QUEUE_COMPUTE_BIT,
};

typedef struct ProResRAWVulkanDecodePicture {
    FFVulkanDecodePicture vp;

    AVBufferRef *frame_data_buf;
    uint32_t nb_tiles;
} ProResRAWVulkanDecodePicture;

typedef struct ProResRAWVulkanDecodeContext {
    FFVulkanShader decode;
    FFVulkanShader idct;

    AVBufferPool *frame_data_pool;
} ProResRAWVulkanDecodeContext;

typedef struct DecodePushData {
    VkDeviceAddress pkt_data;
    int32_t frame_size[2];
    int32_t tile_size[2];
    uint8_t  qmat[64];
} DecodePushData;

typedef struct TileData {
    int32_t pos[2];
    uint32_t offset;
    uint32_t size;
} TileData;

static int vk_prores_raw_start_frame(AVCodecContext          *avctx,
                                     const AVBufferRef       *buffer_ref,
                                     av_unused const uint8_t *buffer,
                                     av_unused uint32_t       size)
{
    int err;
    FFVulkanDecodeContext *dec = avctx->internal->hwaccel_priv_data;
    FFVulkanDecodeShared *ctx = dec->shared_ctx;
    ProResRAWVulkanDecodeContext *prv = ctx->sd_ctx;
    ProResRAWContext *prr = avctx->priv_data;

    ProResRAWVulkanDecodePicture *pp = prr->hwaccel_picture_private;
    FFVulkanDecodePicture *vp = &pp->vp;

    /* Host map the input tile data if supported */
    if (ctx->s.extensions & FF_VK_EXT_EXTERNAL_HOST_MEMORY)
        ff_vk_host_map_buffer(&ctx->s, &vp->slices_buf, buffer_ref->data,
                              buffer_ref,
                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                              VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);

    /* Allocate tile data */
    err = ff_vk_get_pooled_buffer(&ctx->s, &prv->frame_data_pool,
                                  &pp->frame_data_buf,
                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                  VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                  NULL, prr->nb_tiles*sizeof(TileData),
                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    if (err < 0)
        return err;

    /* Prepare frame to be used */
    err = ff_vk_decode_prepare_frame_sdr(dec, prr->frame, vp, 1,
                                         FF_VK_REP_NATIVE, 0);
    if (err < 0)
        return err;

    return 0;
}

static int vk_prores_raw_decode_slice(AVCodecContext *avctx,
                                      const uint8_t  *data,
                                      uint32_t        size)
{
    ProResRAWContext *prr = avctx->priv_data;

    ProResRAWVulkanDecodePicture *pp = prr->hwaccel_picture_private;
    FFVulkanDecodePicture *vp = &pp->vp;

    FFVkBuffer *frame_data_buf = (FFVkBuffer *)pp->frame_data_buf->data;
    TileData *td = (TileData *)frame_data_buf->mapped_mem;
    FFVkBuffer *slices_buf = vp->slices_buf ? (FFVkBuffer *)vp->slices_buf->data : NULL;

    td[pp->nb_tiles].pos[0] = prr->tiles[pp->nb_tiles].x;
    td[pp->nb_tiles].pos[1] = prr->tiles[pp->nb_tiles].y;
    td[pp->nb_tiles].size = size;

    if (vp->slices_buf && slices_buf->host_ref) {
        td[pp->nb_tiles].offset = data - slices_buf->mapped_mem;
        pp->nb_tiles++;
    } else {
        int err;
        td[pp->nb_tiles].offset = vp->slices_size;
        err = ff_vk_decode_add_slice(avctx, vp, data, size, 0,
                                     &pp->nb_tiles, NULL);
        if (err < 0)
            return err;
    }

    return 0;
}

static int vk_prores_raw_end_frame(AVCodecContext *avctx)
{
    int err;
    FFVulkanDecodeContext *dec = avctx->internal->hwaccel_priv_data;
    FFVulkanDecodeShared *ctx = dec->shared_ctx;
    FFVulkanFunctions *vk = &ctx->s.vkfn;

    ProResRAWContext *prr = avctx->priv_data;
    ProResRAWVulkanDecodeContext *prv = ctx->sd_ctx;

    ProResRAWVulkanDecodePicture *pp = prr->hwaccel_picture_private;
    FFVulkanDecodePicture *vp = &pp->vp;

    FFVkBuffer *slices_buf = (FFVkBuffer *)vp->slices_buf->data;
    FFVkBuffer *frame_data_buf = (FFVkBuffer *)pp->frame_data_buf->data;

    VkImageMemoryBarrier2 img_bar[8];
    int nb_img_bar = 0;

    FFVkExecContext *exec = ff_vk_exec_get(&ctx->s, &ctx->exec_pool);
    ff_vk_exec_start(&ctx->s, exec);

    /* Prepare deps */
    RET(ff_vk_exec_add_dep_frame(&ctx->s, exec, prr->frame,
                                 VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT));

    err = ff_vk_exec_mirror_sem_value(&ctx->s, exec, &vp->sem, &vp->sem_value,
                                      prr->frame);
    if (err < 0)
        return err;

    RET(ff_vk_exec_add_dep_buf(&ctx->s, exec, &pp->frame_data_buf, 1, 0));
    pp->frame_data_buf = NULL;
    RET(ff_vk_exec_add_dep_buf(&ctx->s, exec, &vp->slices_buf, 1, 0));
    vp->slices_buf = NULL;

    AVVkFrame *vkf = (AVVkFrame *)prr->frame->data[0];
    vkf->layout[0] = VK_IMAGE_LAYOUT_UNDEFINED;
    vkf->access[0] = VK_ACCESS_2_NONE;

    ff_vk_frame_barrier(&ctx->s, exec, prr->frame, img_bar, &nb_img_bar,
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

    vk->CmdClearColorImage(exec->buf, vkf->img[0],
                           VK_IMAGE_LAYOUT_GENERAL,
                           &((VkClearColorValue) { 0 }),
                           1, &((VkImageSubresourceRange) {
                               .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                               .levelCount = 1,
                               .layerCount = 1,
                           }));

    ff_vk_frame_barrier(&ctx->s, exec, prr->frame, img_bar, &nb_img_bar,
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

    FFVulkanShader *decode_shader = &prv->decode;
    ff_vk_shader_update_img_array(&ctx->s, exec, decode_shader,
                                  prr->frame, vp->view.out,
                                  0, 0,
                                  VK_IMAGE_LAYOUT_GENERAL,
                                  VK_NULL_HANDLE);
    ff_vk_shader_update_desc_buffer(&ctx->s, exec, decode_shader,
                                    0, 1, 0,
                                    frame_data_buf,
                                    0, prr->nb_tiles*sizeof(TileData),
                                    VK_FORMAT_UNDEFINED);

    ff_vk_exec_bind_shader(&ctx->s, exec, decode_shader);

    /* Update push data */
    DecodePushData pd_decode = (DecodePushData) {
        .pkt_data = slices_buf->address,
        .frame_size[0] = avctx->width,
        .frame_size[1] = avctx->height,
        .tile_size[0] = prr->tw,
        .tile_size[1] = prr->th,
    };
    memcpy(pd_decode.qmat, prr->qmat, 64);
    ff_vk_shader_update_push_const(&ctx->s, exec, decode_shader,
                                   VK_SHADER_STAGE_COMPUTE_BIT,
                                   0, sizeof(pd_decode), &pd_decode);

    vk->CmdDispatch(exec->buf, prr->nb_tw, prr->nb_th, 1);

    ff_vk_frame_barrier(&ctx->s, exec, prr->frame, img_bar, &nb_img_bar,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                        VK_IMAGE_LAYOUT_GENERAL,
                        VK_QUEUE_FAMILY_IGNORED);

    FFVulkanShader *idct_shader = &prv->idct;
    ff_vk_shader_update_img_array(&ctx->s, exec, idct_shader,
                                  prr->frame, vp->view.out,
                                  0, 0,
                                  VK_IMAGE_LAYOUT_GENERAL,
                                  VK_NULL_HANDLE);
    ff_vk_shader_update_desc_buffer(&ctx->s, exec, idct_shader,
                                    0, 1, 0,
                                    frame_data_buf,
                                    0, prr->nb_tiles*sizeof(TileData),
                                    VK_FORMAT_UNDEFINED);
    ff_vk_exec_bind_shader(&ctx->s, exec, idct_shader);
    ff_vk_shader_update_push_const(&ctx->s, exec, idct_shader,
                                   VK_SHADER_STAGE_COMPUTE_BIT,
                                   0, sizeof(pd_decode), &pd_decode);

    vk->CmdPipelineBarrier2(exec->buf, &(VkDependencyInfo) {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pImageMemoryBarriers = img_bar,
        .imageMemoryBarrierCount = nb_img_bar,
    });
    nb_img_bar = 0;

    vk->CmdDispatch(exec->buf, prr->nb_tw, prr->nb_th, 1);

    err = ff_vk_exec_submit(&ctx->s, exec);
    if (err < 0)
        return err;

fail:
    return 0;
}

static int add_common_data(AVCodecContext *avctx, FFVulkanContext *s,
                           FFVulkanShader *shd, int writeonly)
{
    AVHWFramesContext *dec_frames_ctx;
    dec_frames_ctx = (AVHWFramesContext *)avctx->hw_frames_ctx->data;

    /* Common codec header */
    GLSLD(ff_source_common_comp);

    GLSLC(0, struct TileData {                                                       );
    GLSLC(1,    ivec2 pos;                                                           );
    GLSLC(1,    uint offset;                                                         );
    GLSLC(1,    uint size;                                                           );
    GLSLC(0, };                                                                      );
    GLSLC(0,                                                                         );
    GLSLC(0, layout(push_constant, scalar) uniform pushConstants {                   );
    GLSLC(1,    u8buf pkt_data;                                                      );
    GLSLC(1,    ivec2 frame_size;                                                    );
    GLSLC(1,    ivec2 tile_size;                                                     );
    GLSLC(1,    uint8_t qmat[64];                                                    );
    GLSLC(0, };                                                                      );
    GLSLC(0,                                                                         );
    ff_vk_shader_add_push_const(shd, 0, sizeof(DecodePushData),
                                VK_SHADER_STAGE_COMPUTE_BIT);

    FFVulkanDescriptorSetBinding *desc_set;
    desc_set = (FFVulkanDescriptorSetBinding []) {
        {
            .name       = "dst",
            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .mem_layout = ff_vk_shader_rep_fmt(dec_frames_ctx->sw_format,
                                               FF_VK_REP_NATIVE),
            .mem_quali  = writeonly ? "writeonly" : NULL,
            .dimensions = 2,
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        {
            .name        = "frame_data_buf",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .mem_layout  = "scalar",
            .mem_quali   = "readonly",
            .buf_content = "TileData tile_data[];",
        },
    };

    return ff_vk_shader_add_descriptor_set(s, shd, desc_set, 2, 0, 0);
}

static int init_decode_shader(AVCodecContext *avctx, FFVulkanContext *s,
                              FFVkExecPool *pool, FFVkSPIRVCompiler *spv,
                              FFVulkanShader *shd, int version)
{
    int err;
    uint8_t *spv_data;
    size_t spv_len;
    void *spv_opaque = NULL;

    RET(ff_vk_shader_init(s, shd, "prores_raw",
                          VK_SHADER_STAGE_COMPUTE_BIT,
                          (const char *[]) { "GL_EXT_buffer_reference",
                                             "GL_EXT_buffer_reference2",
                                             "GL_EXT_null_initializer" }, 3,
                          4, 1, 1, 0));

    RET(add_common_data(avctx, s, shd, 1));

    GLSLD(ff_source_prores_raw_decode_comp);

    RET(spv->compile_shader(s, spv, shd, &spv_data, &spv_len, "main",
                            &spv_opaque));
    RET(ff_vk_shader_link(s, shd, spv_data, spv_len, "main"));

    RET(ff_vk_shader_register_exec(s, pool, shd));

fail:
    if (spv_opaque)
        spv->free_shader(spv, &spv_opaque);

    return err;
}

static int init_idct_shader(AVCodecContext *avctx, FFVulkanContext *s,
                            FFVkExecPool *pool, FFVkSPIRVCompiler *spv,
                            FFVulkanShader *shd, int version)
{
    int err;
    uint8_t *spv_data;
    size_t spv_len;
    void *spv_opaque = NULL;

    RET(ff_vk_shader_init(s, shd, "prores_raw",
                          VK_SHADER_STAGE_COMPUTE_BIT,
                          (const char *[]) { "GL_EXT_buffer_reference",
                                             "GL_EXT_buffer_reference2" }, 2,
                          8,
                          version == 0 ? 8 : 16 /* Horizontal blocks */,
                          4 /* Components */,
                          0));

    RET(add_common_data(avctx, s, shd, 0));

    GLSLD(ff_source_prores_raw_idct_comp);

    RET(spv->compile_shader(s, spv, shd, &spv_data, &spv_len, "main",
                            &spv_opaque));
    RET(ff_vk_shader_link(s, shd, spv_data, spv_len, "main"));

    RET(ff_vk_shader_register_exec(s, pool, shd));

fail:
    if (spv_opaque)
        spv->free_shader(spv, &spv_opaque);

    return err;
}

static void vk_decode_prores_raw_uninit(FFVulkanDecodeShared *ctx)
{
    ProResRAWVulkanDecodeContext *fv = ctx->sd_ctx;

    ff_vk_shader_free(&ctx->s, &fv->decode);
    ff_vk_shader_free(&ctx->s, &fv->idct);

    av_buffer_pool_uninit(&fv->frame_data_pool);

    av_freep(&fv);
}

static int vk_decode_prores_raw_init(AVCodecContext *avctx)
{
    int err;
    FFVulkanDecodeContext *dec = avctx->internal->hwaccel_priv_data;
    ProResRAWContext *prr = avctx->priv_data;

    FFVkSPIRVCompiler *spv = ff_vk_spirv_init();
    if (!spv) {
        av_log(avctx, AV_LOG_ERROR, "Unable to initialize SPIR-V compiler!\n");
        return AVERROR_EXTERNAL;
    }

    err = ff_vk_decode_init(avctx);
    if (err < 0)
        return err;

    FFVulkanDecodeShared *ctx = dec->shared_ctx;
    ProResRAWVulkanDecodeContext *prv = ctx->sd_ctx = av_mallocz(sizeof(*prv));
    if (!prv) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    ctx->sd_ctx_free = &vk_decode_prores_raw_uninit;

    /* Setup decode shader */
    RET(init_decode_shader(avctx, &ctx->s, &ctx->exec_pool, spv, &prv->decode,
                           prr->version));
    RET(init_idct_shader(avctx, &ctx->s, &ctx->exec_pool, spv, &prv->idct,
                         prr->version));

fail:
    spv->uninit(&spv);

    return err;
}

static void vk_prores_raw_free_frame_priv(AVRefStructOpaque _hwctx, void *data)
{
    AVHWDeviceContext *dev_ctx = _hwctx.nc;

    ProResRAWVulkanDecodePicture *pp = data;
    FFVulkanDecodePicture *vp = &pp->vp;

    ff_vk_decode_free_frame(dev_ctx, vp);

    av_buffer_unref(&pp->frame_data_buf);
}

const FFHWAccel ff_prores_raw_vulkan_hwaccel = {
    .p.name                = "prores_raw_vulkan",
    .p.type                = AVMEDIA_TYPE_VIDEO,
    .p.id                  = AV_CODEC_ID_PRORES_RAW,
    .p.pix_fmt             = AV_PIX_FMT_VULKAN,
    .start_frame           = &vk_prores_raw_start_frame,
    .decode_slice          = &vk_prores_raw_decode_slice,
    .end_frame             = &vk_prores_raw_end_frame,
    .free_frame_priv       = &vk_prores_raw_free_frame_priv,
    .frame_priv_data_size  = sizeof(ProResRAWVulkanDecodePicture),
    .init                  = &vk_decode_prores_raw_init,
    .update_thread_context = &ff_vk_update_thread_context,
    .uninit                = &ff_vk_decode_uninit,
    .frame_params          = &ff_vk_frame_params,
    .priv_data_size        = sizeof(FFVulkanDecodeContext),
    .caps_internal         = HWACCEL_CAP_ASYNC_SAFE | HWACCEL_CAP_THREAD_SAFE,
};
