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
extern const char *ff_source_prores_raw_comp;

const FFVulkanDecodeDescriptor ff_vk_dec_prores_raw_desc = {
    .codec_id         = AV_CODEC_ID_PRORES_RAW,
    .decode_extension = FF_VK_EXT_PUSH_DESCRIPTOR,
    .queue_flags      = VK_QUEUE_COMPUTE_BIT,
};

typedef struct ProResRAWVulkanDecodePicture {
    FFVulkanDecodePicture vp;

    AVBufferRef *tile_data;
    uint32_t nb_tiles;
} ProResRAWVulkanDecodePicture;

typedef struct ProResRAWVulkanDecodeContext {
    FFVulkanShader decode[2];

    AVBufferPool *tile_data_pool;

    FFVkBuffer uniform_buf;
} ProResRAWVulkanDecodeContext;

typedef struct DecodePushData {
    VkDeviceAddress tile_data;
    VkDeviceAddress pkt_data;
    uint32_t frame_size[2];
    uint32_t tile_size[2];
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
    err = ff_vk_get_pooled_buffer(&ctx->s, &prv->tile_data_pool,
                                  &pp->tile_data,
                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                  VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                  NULL, prr->nb_tiles*sizeof(TileData),
                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    if (err < 0)
        return err;

    /* Prepare frame to be used */
    err = ff_vk_decode_prepare_frame_sdr(dec, prr->frame, vp, 1,
                                         FF_VK_REP_FLOAT, 0);
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

    FFVkBuffer *tile_data_buf = (FFVkBuffer *)pp->tile_data->data;
    TileData *td = (TileData *)tile_data_buf->mapped_mem;
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
    FFVkBuffer *tile_data = (FFVkBuffer *)pp->tile_data->data;

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

    RET(ff_vk_exec_add_dep_buf(&ctx->s, exec, &pp->tile_data, 1, 0));
    pp->tile_data = NULL;
    RET(ff_vk_exec_add_dep_buf(&ctx->s, exec, &vp->slices_buf, 1, 0));
    vp->slices_buf = NULL;

    ff_vk_frame_barrier(&ctx->s, exec, prr->frame, img_bar, &nb_img_bar,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        VK_ACCESS_2_TRANSFER_WRITE_BIT,
                        VK_IMAGE_LAYOUT_GENERAL,
                        VK_QUEUE_FAMILY_IGNORED);

    vk->CmdPipelineBarrier2(exec->buf, &(VkDependencyInfo) {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pImageMemoryBarriers = img_bar,
        .imageMemoryBarrierCount = nb_img_bar,
    });
    nb_img_bar = 0;

    FFVulkanShader *decode_shader = &prv->decode[prr->version];
    ff_vk_shader_update_img_array(&ctx->s, exec, decode_shader,
                                  prr->frame, vp->view.out,
                                  0, 0,
                                  VK_IMAGE_LAYOUT_GENERAL,
                                  VK_NULL_HANDLE);

    ff_vk_exec_bind_shader(&ctx->s, exec, decode_shader);

    /* Update push data */
    DecodePushData pd_decode = (DecodePushData) {
        .tile_data = tile_data->address,
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

    err = ff_vk_exec_submit(&ctx->s, exec);
    if (err < 0)
        return err;

fail:
    return 0;
}

static int init_decode_shader(ProResRAWContext *prr, FFVulkanContext *s,
                              FFVkExecPool *pool, FFVkSPIRVCompiler *spv,
                              FFVulkanShader *shd, int version)
{
    int err;
    FFVulkanDescriptorSetBinding *desc_set;
    int parallel_rows = 1;

    uint8_t *spv_data;
    size_t spv_len;
    void *spv_opaque = NULL;

    if (s->props.properties.limits.maxComputeWorkGroupInvocations < 512 ||
        s->props.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
        parallel_rows = 0;

    RET(ff_vk_shader_init(s, shd, "prores_raw",
                          VK_SHADER_STAGE_COMPUTE_BIT,
                          (const char *[]) { "GL_EXT_buffer_reference",
                                             "GL_EXT_buffer_reference2",
                                             "GL_EXT_null_initializer" }, 3,
                          parallel_rows ? 8 : 1 /* 8x8 transforms, 8-point width */,
                          version == 0 ? 8 : 16 /* Horizontal blocks */,
                          4 /* Components */,
                          0));

    if (parallel_rows)
        GLSLC(0, #define PARALLEL_ROWS                                               );

    /* Common codec header */
    GLSLD(ff_source_common_comp);

    GLSLC(0, layout(buffer_reference, buffer_reference_align = 16) buffer TileData { );
    GLSLC(1,    ivec2 pos;                                                           );
    GLSLC(1,    uint offset;                                                         );
    GLSLC(1,    uint size;                                                           );
    GLSLC(0, };                                                                      );
    GLSLC(0,                                                                         );
    GLSLC(0, layout(push_constant, scalar) uniform pushConstants {                   );
    GLSLC(1,    TileData tile_data;                                                  );
    GLSLC(1,    u8buf pkt_data;                                                      );
    GLSLC(1,    uvec2 frame_size;                                                    );
    GLSLC(1,    uvec2 tile_size;                                                     );
    GLSLC(1,    uint8_t qmat[64];                                                    );
    GLSLC(0, };                                                                      );
    GLSLC(0,                                                                         );
    ff_vk_shader_add_push_const(shd, 0, sizeof(DecodePushData),
                                VK_SHADER_STAGE_COMPUTE_BIT);

    desc_set = (FFVulkanDescriptorSetBinding []) {
        {
            .name       = "dst",
            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .mem_layout = "r16",
            .mem_quali  = "writeonly",
            .dimensions = 2,
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };
    RET(ff_vk_shader_add_descriptor_set(s, shd, desc_set, 1, 0, 0));

    desc_set = (FFVulkanDescriptorSetBinding []) {
        {
            .name        = "dct_scale_buf",
            .type        = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .mem_layout  = "scalar",
            .buf_content = "float idct_8x8_scales[64];",
        },
        {
            .name        = "scan_buf",
            .type        = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .mem_layout  = "scalar",
            .buf_content = "uint8_t scan[64];",
        },
        {
            .name        = "dc_cb_buf",
            .type        = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .mem_layout  = "scalar",
            .buf_content = "uint8_t dc_cb[13];",
        },
        {
            .name        = "ac_cb_buf",
            .type        = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .mem_layout  = "scalar",
            .buf_content = "int16_t ac_cb[95];",
        },
        {
            .name        = "rn_cb_buf",
            .type        = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .mem_layout  = "scalar",
            .buf_content = "int16_t rn_cb[28];",
        },
        {
            .name        = "ln_cb_buf",
            .type        = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .mem_layout  = "scalar",
            .buf_content = "int16_t ln_cb[15];",
        },
    };
    RET(ff_vk_shader_add_descriptor_set(s, shd, desc_set, 6, 1, 0));

    GLSLD(ff_source_prores_raw_comp);

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

    ff_vk_shader_free(&ctx->s, &fv->decode[0]);
    ff_vk_shader_free(&ctx->s, &fv->decode[1]);

    ff_vk_free_buf(&ctx->s, &fv->uniform_buf);

    av_buffer_pool_uninit(&fv->tile_data_pool);

    av_freep(&fv);
}

static int vk_decode_prores_raw_init(AVCodecContext *avctx)
{
    int err;
    ProResRAWContext *prr = avctx->priv_data;
    FFVulkanDecodeContext *dec = avctx->internal->hwaccel_priv_data;

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
    RET(init_decode_shader(prr, &ctx->s, &ctx->exec_pool, spv, &prv->decode[0], 0));
    RET(init_decode_shader(prr, &ctx->s, &ctx->exec_pool, spv, &prv->decode[1], 1));

    /* Size in bytes of each codebook table */
    size_t cb_size[5] = {
        13*sizeof(uint8_t),
        95*sizeof(int16_t),
        28*sizeof(int16_t),
        15*sizeof(int16_t),
    };

    /* Offset of each codebook table */
    size_t cb_offset[5];
    size_t ua = ctx->s.props.properties.limits.minUniformBufferOffsetAlignment;
    cb_offset[0] = 64*sizeof(float) + 64*sizeof(uint8_t);
    cb_offset[1] = cb_offset[0] + FFALIGN(cb_size[0], ua);
    cb_offset[2] = cb_offset[1] + FFALIGN(cb_size[1], ua);
    cb_offset[3] = cb_offset[2] + FFALIGN(cb_size[2], ua);
    cb_offset[4] = cb_offset[3] + FFALIGN(cb_size[3], ua);

    RET(ff_vk_create_buf(&ctx->s, &prv->uniform_buf,
                         64*sizeof(float) + 64*sizeof(uint8_t) + cb_offset[4] + 256,
                         NULL, NULL,
                         VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT));

    uint8_t *uniform_buf;
    RET(ff_vk_map_buffer(&ctx->s, &prv->uniform_buf, &uniform_buf, 0));

    /* DCT scales */
    float *dct_scale_buf = (float *)uniform_buf;
    double idct_8_scales[8] = {
        cos(4.0*M_PI/16.0) / 2.0,
        cos(1.0*M_PI/16.0) / 2.0,
        cos(2.0*M_PI/16.0) / 2.0,
        cos(3.0*M_PI/16.0) / 2.0,
        cos(4.0*M_PI/16.0) / 2.0,
        cos(5.0*M_PI/16.0) / 2.0,
        cos(6.0*M_PI/16.0) / 2.0,
        cos(7.0*M_PI/16.0) / 2.0,
    };
    for (int i = 0; i < 64; i++)
        dct_scale_buf[i] = (float)(idct_8_scales[i >> 3] *
                                   idct_8_scales[i  & 7]);

    /* Scan table */
    uint8_t *scan_buf = uniform_buf + 64*sizeof(float);
    for (int i = 0; i < 64; i++)
        scan_buf[prr->scan[i]] = i;

    /* Codebooks */
    memcpy(uniform_buf + cb_offset[0], ff_prores_raw_dc_cb,
           sizeof(ff_prores_raw_dc_cb));
    memcpy(uniform_buf + cb_offset[1], ff_prores_raw_ac_cb,
           sizeof(ff_prores_raw_ac_cb));
    memcpy(uniform_buf + cb_offset[2], ff_prores_raw_rn_cb,
           sizeof(ff_prores_raw_rn_cb));
    memcpy(uniform_buf + cb_offset[3], ff_prores_raw_ln_cb,
           sizeof(ff_prores_raw_ln_cb));

    RET(ff_vk_unmap_buffer(&ctx->s, &prv->uniform_buf, 1));

    /* Done; update descriptors */
    for (int i = 0; i < 2; i++) {
        RET(ff_vk_shader_update_desc_buffer(&ctx->s, &ctx->exec_pool.contexts[0],
                                            &prv->decode[i], 1, 0, 0,
                                            &prv->uniform_buf,
                                            0, 64*sizeof(float),
                                            VK_FORMAT_UNDEFINED));
        RET(ff_vk_shader_update_desc_buffer(&ctx->s, &ctx->exec_pool.contexts[0],
                                            &prv->decode[i], 1, 1, 0,
                                            &prv->uniform_buf,
                                            64*sizeof(float), 64*sizeof(uint8_t),
                                            VK_FORMAT_UNDEFINED));
        for (int j = 0; j < 4; j++)
            RET(ff_vk_shader_update_desc_buffer(&ctx->s, &ctx->exec_pool.contexts[0],
                                                &prv->decode[i], 1, 2 + j, 0,
                                                &prv->uniform_buf,
                                                cb_offset[j], cb_size[j],
                                                VK_FORMAT_UNDEFINED));
    }

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
    .decode_params         = &ff_vk_params_invalidate,
    .flush                 = &ff_vk_decode_flush,
    .uninit                = &ff_vk_decode_uninit,
    .frame_params          = &ff_vk_frame_params,
    .priv_data_size        = sizeof(FFVulkanDecodeContext),
    .caps_internal         = HWACCEL_CAP_ASYNC_SAFE | HWACCEL_CAP_THREAD_SAFE,
};
