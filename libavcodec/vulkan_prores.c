/*
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

#include "proresdec.h"
#include "vulkan_decode.h"
#include "hwaccel_internal.h"
#include "libavutil/mem.h"
#include "libavutil/vulkan.h"
#include "libavutil/vulkan_spirv.h"

extern const char *ff_source_common_comp;
extern const char *ff_source_dct_comp;
extern const char *ff_source_prores_vld_comp;
extern const char *ff_source_prores_idct_comp;

const FFVulkanDecodeDescriptor ff_vk_dec_prores_desc = {
    .codec_id    = AV_CODEC_ID_PRORES,
    .queue_flags = VK_QUEUE_COMPUTE_BIT,
};

typedef struct ProresVulkanDecodePicture {
    FFVulkanDecodePicture vp;

    AVBufferRef *metadata_buf;

    uint32_t bitstream_start;
    uint32_t bitstream_size;
    uint32_t slice_num;

    uint32_t slice_offsets_sz, mb_params_sz;
} ProresVulkanDecodePicture;

typedef struct ProresVulkanDecodeContext {
    FFVulkanShader vld;
    FFVulkanShader idct;

    AVBufferPool *metadata_pool;
} ProresVulkanDecodeContext;

typedef struct ProresVkParameters {
    VkDeviceAddress slice_data;
    uint32_t bitstream_size;

    uint16_t width;
    uint16_t height;
    uint16_t mb_width;
    uint16_t mb_height;
    uint16_t slice_width;
    uint16_t slice_height;
    uint8_t  log2_slice_width;
    uint8_t  log2_chroma_w;
    uint8_t  depth;
    uint8_t  alpha_info;
    uint8_t  bottom_field;

    uint8_t  qmat_luma  [64];
    uint8_t  qmat_chroma[64];
} ProresVkParameters;

static int vk_prores_start_frame(AVCodecContext          *avctx,
                                 const AVBufferRef       *buffer_ref,
                                 av_unused const uint8_t *buffer,
                                 av_unused uint32_t       size)
{
    ProresContext             *pr = avctx->priv_data;
    FFVulkanDecodeContext    *dec = avctx->internal->hwaccel_priv_data;
    FFVulkanDecodeShared     *ctx = dec->shared_ctx;
    ProresVulkanDecodeContext *pv = ctx->sd_ctx;
    ProresVulkanDecodePicture *pp = pr->hwaccel_picture_private;
    FFVulkanDecodePicture     *vp = &pp->vp;

    int err;

    pp->slice_offsets_sz = (pr->slice_count + 1) * sizeof(uint32_t);
    pp->mb_params_sz     = pr->mb_width * pr->mb_height * sizeof(uint8_t);

    /* Host map the input slices data if supported */
    if (!vp->slices_buf && ctx->s.extensions & FF_VK_EXT_EXTERNAL_HOST_MEMORY)
        RET(ff_vk_host_map_buffer(&ctx->s, &vp->slices_buf, buffer_ref->data,
                                  buffer_ref,
                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                  VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT));

    /* Allocate slice offsets buffer */
    RET(ff_vk_get_pooled_buffer(&ctx->s, &pv->metadata_pool,
                                &pp->metadata_buf,
                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                NULL, pp->slice_offsets_sz + pp->mb_params_sz,
                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT));

    /* Prepare frame to be used */
    RET(ff_vk_decode_prepare_frame_sdr(dec, pr->frame, vp, 1,
                                       FF_VK_REP_NATIVE, 0));

    pp->slice_num = 0;
    pp->bitstream_start = pp->bitstream_size = 0;

fail:
    return err;
}

static int vk_prores_decode_slice(AVCodecContext *avctx,
                                  const uint8_t  *data,
                                  uint32_t        size)
{
    ProresContext             *pr = avctx->priv_data;
    ProresVulkanDecodePicture *pp = pr->hwaccel_picture_private;
    FFVulkanDecodePicture     *vp = &pp->vp;

    FFVkBuffer *slice_offset = (FFVkBuffer *)pp->metadata_buf->data;
    FFVkBuffer *slices_buf   = vp->slices_buf ? (FFVkBuffer *)vp->slices_buf->data : NULL;

    /* Skip picture header */
    if (slices_buf && slices_buf->host_ref && !pp->slice_num)
        pp->bitstream_size = data - slices_buf->mapped_mem;

    AV_WN32(slice_offset->mapped_mem + (pp->slice_num + 0) * sizeof(uint32_t),
            pp->bitstream_size);
    AV_WN32(slice_offset->mapped_mem + (pp->slice_num + 1) * sizeof(uint32_t),
            pp->bitstream_size += size);

    if (!slices_buf || !slices_buf->host_ref) {
        int err = ff_vk_decode_add_slice(avctx, vp, data, size, 0,
                                         &pp->slice_num, NULL);
        if (err < 0)
            return err;
    } else {
        pp->slice_num++;
    }

    return 0;
}

static int vk_prores_end_frame(AVCodecContext *avctx)
{
    ProresContext             *pr = avctx->priv_data;
    FFVulkanDecodeContext    *dec = avctx->internal->hwaccel_priv_data;
    FFVulkanDecodeShared     *ctx = dec->shared_ctx;
    FFVulkanFunctions         *vk = &ctx->s.vkfn;
    ProresVulkanDecodeContext *pv = ctx->sd_ctx;
    ProresVulkanDecodePicture *pp = pr->hwaccel_picture_private;
    FFVulkanDecodePicture     *vp = &pp->vp;
    AVFrame                    *f = pr->frame;
    AVVkFrame                *vkf = (AVVkFrame *)f->data[0];

    ProresVkParameters pd;
    FFVkBuffer *slice_data, *metadata;
    VkImageMemoryBarrier2 img_bar[AV_NUM_DATA_POINTERS];
    VkBufferMemoryBarrier2 buf_bar[2];
    int nb_img_bar = 0, nb_buf_bar = 0, nb_imgs, i, err;
    const AVPixFmtDescriptor *pix_desc;

    if (!pp->slice_num)
        return 0;

    pix_desc = av_pix_fmt_desc_get(avctx->sw_pix_fmt);
    if (!pix_desc)
        return AVERROR(EINVAL);

    slice_data = (FFVkBuffer *)vp->slices_buf->data;
    metadata   = (FFVkBuffer *)pp->metadata_buf->data;

    pd = (ProresVkParameters) {
        .slice_data       = slice_data->address,
        .bitstream_size   = pp->bitstream_size,

        .width            = avctx->width,
        .height           = avctx->height,
        .mb_width         = pr->mb_width,
        .mb_height        = pr->mb_height,
        .slice_width      = pr->slice_count / pr->mb_height,
        .slice_height     = pr->mb_height,
        .log2_slice_width = av_log2(pr->slice_mb_width),
        .log2_chroma_w    = pix_desc->log2_chroma_w,
        .depth            = avctx->bits_per_raw_sample,
        .alpha_info       = pr->alpha_info,
        .bottom_field     = pr->first_field ^ (pr->frame_type == 1),
    };

    memcpy(pd.qmat_luma,   pr->qmat_luma,   sizeof(pd.qmat_luma  ));
    memcpy(pd.qmat_chroma, pr->qmat_chroma, sizeof(pd.qmat_chroma));

    FFVkExecContext *exec = ff_vk_exec_get(&ctx->s, &ctx->exec_pool);
    RET(ff_vk_exec_start(&ctx->s, exec));

    /* Prepare deps */
    RET(ff_vk_exec_add_dep_frame(&ctx->s, exec, f,
                                 VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT));

    RET(ff_vk_exec_mirror_sem_value(&ctx->s, exec, &vp->sem, &vp->sem_value, f));

    /* Transfer ownership to the exec context */
    RET(ff_vk_exec_add_dep_buf(&ctx->s, exec, &vp->slices_buf, 1, 0));
    vp->slices_buf = NULL;
    RET(ff_vk_exec_add_dep_buf(&ctx->s, exec, &pp->metadata_buf, 1, 0));
    pp->metadata_buf = NULL;

    vkf->layout[0] = VK_IMAGE_LAYOUT_UNDEFINED;
    vkf->access[0] = VK_ACCESS_2_NONE;

    nb_imgs = ff_vk_count_images(vkf);

    if (pr->first_field) {
        /* Input barrier */
        ff_vk_frame_barrier(&ctx->s, exec, f, img_bar, &nb_img_bar,
                            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                            VK_PIPELINE_STAGE_2_CLEAR_BIT,
                            VK_ACCESS_2_TRANSFER_WRITE_BIT,
                            VK_IMAGE_LAYOUT_GENERAL,
                            VK_QUEUE_FAMILY_IGNORED);

        vk->CmdPipelineBarrier2(exec->buf, &(VkDependencyInfo) {
            .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pBufferMemoryBarriers    = buf_bar,
            .bufferMemoryBarrierCount = nb_buf_bar,
            .pImageMemoryBarriers     = img_bar,
            .imageMemoryBarrierCount  = nb_img_bar,
        });
        nb_img_bar = nb_buf_bar = 0;

        /* Clear the input image since the vld shader does sparse writes, except for alpha */
        for (i = 0; i < FFMIN(nb_imgs, 3); ++i) {
            vk->CmdClearColorImage(exec->buf, vkf->img[i],
                                   VK_IMAGE_LAYOUT_GENERAL,
                                   &((VkClearColorValue) { 0 }),
                                   1, &((VkImageSubresourceRange) {
                                       .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                       .levelCount = 1,
                                       .layerCount = 1,
                                   }));
        }
    }

    /* Input barrier, or synchronization between clear and vld shader */
    ff_vk_frame_barrier(&ctx->s, exec, f, img_bar, &nb_img_bar,
                        pr->first_field ? VK_PIPELINE_STAGE_2_CLEAR_BIT : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                        VK_IMAGE_LAYOUT_GENERAL,
                        VK_QUEUE_FAMILY_IGNORED);

    buf_bar[nb_buf_bar++] = (VkBufferMemoryBarrier2) {
        .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .srcStageMask        = metadata->stage,
        .dstStageMask        = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .srcAccessMask       = metadata->access,
        .dstAccessMask       = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer              = metadata->buf,
        .offset              = pp->slice_offsets_sz,
        .size                = pp->mb_params_sz,
    };
    metadata->stage  = buf_bar[0].dstStageMask;
    metadata->access = buf_bar[0].dstAccessMask;

    vk->CmdPipelineBarrier2(exec->buf, &(VkDependencyInfo) {
        .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pBufferMemoryBarriers    = buf_bar,
        .bufferMemoryBarrierCount = nb_buf_bar,
        .pImageMemoryBarriers     = img_bar,
        .imageMemoryBarrierCount  = nb_img_bar,
    });
    nb_img_bar = nb_buf_bar = 0;

    /* Entropy decode */
    ff_vk_shader_update_desc_buffer(&ctx->s, exec, &pv->vld,
                                    0, 0, 0,
                                    metadata, 0,
                                    pp->slice_offsets_sz,
                                    VK_FORMAT_UNDEFINED);
    ff_vk_shader_update_desc_buffer(&ctx->s, exec, &pv->vld,
                                    0, 1, 0,
                                    metadata, pp->slice_offsets_sz,
                                    pp->mb_params_sz,
                                    VK_FORMAT_UNDEFINED);
    ff_vk_shader_update_img_array(&ctx->s, exec, &pv->vld,
                                  f, vp->view.out,
                                  0, 2,
                                  VK_IMAGE_LAYOUT_GENERAL,
                                  VK_NULL_HANDLE);

    ff_vk_exec_bind_shader(&ctx->s, exec, &pv->vld);
    ff_vk_shader_update_push_const(&ctx->s, exec, &pv->vld,
                                   VK_SHADER_STAGE_COMPUTE_BIT,
                                   0, sizeof(pd), &pd);

    vk->CmdDispatch(exec->buf, AV_CEIL_RSHIFT(pr->slice_count / pr->mb_height, 3), AV_CEIL_RSHIFT(pr->mb_height, 3),
                    3 + !!pr->alpha_info);

    /* Synchronize vld and idct shaders */
    ff_vk_frame_barrier(&ctx->s, exec, f, img_bar, &nb_img_bar,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                        VK_IMAGE_LAYOUT_GENERAL,
                        VK_QUEUE_FAMILY_IGNORED);

    buf_bar[nb_buf_bar++] = (VkBufferMemoryBarrier2) {
        .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .srcStageMask        = metadata->stage,
        .dstStageMask        = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .srcAccessMask       = metadata->access,
        .dstAccessMask       = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer              = metadata->buf,
        .offset              = pp->slice_offsets_sz,
        .size                = pp->mb_params_sz,
    };
    metadata->stage  = buf_bar[0].dstStageMask;
    metadata->access = buf_bar[0].dstAccessMask;

    vk->CmdPipelineBarrier2(exec->buf, &(VkDependencyInfo) {
        .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pBufferMemoryBarriers    = buf_bar,
        .bufferMemoryBarrierCount = nb_buf_bar,
        .pImageMemoryBarriers     = img_bar,
        .imageMemoryBarrierCount  = nb_img_bar,
    });
    nb_img_bar = nb_buf_bar = 0;

    /* Inverse transform */
    ff_vk_shader_update_desc_buffer(&ctx->s, exec, &pv->idct,
                                    0, 0, 0,
                                    metadata, pp->slice_offsets_sz,
                                    pp->mb_params_sz,
                                    VK_FORMAT_UNDEFINED);
    ff_vk_shader_update_img_array(&ctx->s, exec, &pv->idct,
                                  f, vp->view.out,
                                  0, 1,
                                  VK_IMAGE_LAYOUT_GENERAL,
                                  VK_NULL_HANDLE);

    ff_vk_exec_bind_shader(&ctx->s, exec, &pv->idct);
    ff_vk_shader_update_push_const(&ctx->s, exec, &pv->idct,
                                   VK_SHADER_STAGE_COMPUTE_BIT,
                                   0, sizeof(pd), &pd);

    vk->CmdDispatch(exec->buf, AV_CEIL_RSHIFT(pr->mb_width, 1), pr->mb_height, 3);

    RET(ff_vk_exec_submit(&ctx->s, exec));

fail:
    return err;
}

static int add_push_data(FFVulkanShader *shd)
{
    GLSLC(0, layout(push_constant, scalar) uniform pushConstants { );
    GLSLC(1,    u8buf    slice_data;                               );
    GLSLC(1,    uint     bitstream_size;                           );
    GLSLC(0,                                                       );
    GLSLC(1,    uint16_t width;                                    );
    GLSLC(1,    uint16_t height;                                   );
    GLSLC(1,    uint16_t mb_width;                                 );
    GLSLC(1,    uint16_t mb_height;                                );
    GLSLC(1,    uint16_t slice_width;                              );
    GLSLC(1,    uint16_t slice_height;                             );
    GLSLC(1,    uint8_t  log2_slice_width;                         );
    GLSLC(1,    uint8_t  log2_chroma_w;                            );
    GLSLC(1,    uint8_t  depth;                                    );
    GLSLC(1,    uint8_t  alpha_info;                               );
    GLSLC(1,    uint8_t  bottom_field;                             );
    GLSLC(0,                                                       );
    GLSLC(1,    uint8_t  qmat_luma  [8*8];                         );
    GLSLC(1,    uint8_t  qmat_chroma[8*8];                         );
    GLSLC(0, };                                                    );

    return ff_vk_shader_add_push_const(shd, 0, sizeof(ProresVkParameters),
                                       VK_SHADER_STAGE_COMPUTE_BIT);
}

static int init_decode_shader(AVCodecContext *avctx, FFVulkanContext *s,
                              FFVkExecPool *pool, FFVkSPIRVCompiler *spv,
                              FFVulkanShader *shd, int max_num_mbs,
                              int interlaced)
{
    int err;
    AVHWFramesContext *dec_frames_ctx;
    dec_frames_ctx = (AVHWFramesContext *)avctx->hw_frames_ctx->data;

    uint8_t *spv_data;
    size_t spv_len;
    void *spv_opaque = NULL;

    RET(ff_vk_shader_init(s, shd, "prores_vld",
                          VK_SHADER_STAGE_COMPUTE_BIT,
                          (const char *[]) { "GL_EXT_buffer_reference",
                                             "GL_EXT_buffer_reference2" }, 2,
                          8, 8, 1,
                          0));

    av_bprintf(&shd->src, "#define GET_BITS_SMEM %d\n", 4);

    if (interlaced)
        av_bprintf(&shd->src, "#define INTERLACED\n");

    /* Common codec header */
    GLSLD(ff_source_common_comp);

    RET(add_push_data(shd));

    FFVulkanDescriptorSetBinding desc_set[] = {
        {
            .name        = "slice_offsets_buf",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .mem_quali   = "readonly",
            .buf_content = "uint32_t slice_offsets",
            .buf_elems   = max_num_mbs + 1,
        },
        {
            .name        = "quant_idx_buf",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .mem_quali   = "writeonly",
            .buf_content = "uint8_t quant_idx",
            .buf_elems   = max_num_mbs,
        },
        {
            .name       = "dst",
            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .dimensions = 2,
            .mem_layout = ff_vk_shader_rep_fmt(dec_frames_ctx->sw_format,
                                               FF_VK_REP_NATIVE),
            .mem_quali  = "writeonly",
            .elems      = av_pix_fmt_count_planes(dec_frames_ctx->sw_format),
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };
    RET(ff_vk_shader_add_descriptor_set(s, shd, desc_set, 3, 0, 0));

    GLSLD(ff_source_prores_vld_comp);

    RET(spv->compile_shader(s, spv, shd, &spv_data, &spv_len, "main",
                            &spv_opaque));
    RET(ff_vk_shader_link(s, shd, spv_data, spv_len, "main"));

    RET(ff_vk_shader_register_exec(s, pool, shd));

fail:
    if (spv_opaque)
        spv->free_shader(spv, &spv_opaque);

    return 0;
}

static int init_idct_shader(AVCodecContext *avctx, FFVulkanContext *s,
                            FFVkExecPool *pool, FFVkSPIRVCompiler *spv,
                            FFVulkanShader *shd, int max_num_mbs,
                            int interlaced)
{
    int err;
    AVHWFramesContext *dec_frames_ctx;
    dec_frames_ctx = (AVHWFramesContext *)avctx->hw_frames_ctx->data;

    uint8_t *spv_data;
    size_t spv_len;
    void *spv_opaque = NULL;

    RET(ff_vk_shader_init(s, shd, "prores_idct",
                          VK_SHADER_STAGE_COMPUTE_BIT,
                          (const char *[]) { "GL_EXT_buffer_reference",
                                             "GL_EXT_buffer_reference2" }, 2,
                          32, 2, 1,
                          0));

    if (interlaced)
        av_bprintf(&shd->src, "#define INTERLACED\n");

    /* Common codec header */
    GLSLD(ff_source_common_comp);

    RET(add_push_data(shd));

    FFVulkanDescriptorSetBinding desc_set[] = {
        {
            .name        = "quant_idx_buf",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .mem_quali   = "readonly",
            .buf_content = "uint8_t quant_idx",
            .buf_elems   = max_num_mbs,
        },
        {
            .name       = "dst",
            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .dimensions = 2,
            .mem_layout = ff_vk_shader_rep_fmt(dec_frames_ctx->sw_format,
                                               FF_VK_REP_NATIVE),
            .elems      = av_pix_fmt_count_planes(dec_frames_ctx->sw_format),
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };
    RET(ff_vk_shader_add_descriptor_set(s, shd, desc_set, 2, 0, 0));

    GLSLC(0, #define NB_BLOCKS 4*2);
    GLSLD(ff_source_dct_comp);

    GLSLD(ff_source_prores_idct_comp);

    RET(spv->compile_shader(s, spv, shd, &spv_data, &spv_len, "main",
                            &spv_opaque));
    RET(ff_vk_shader_link(s, shd, spv_data, spv_len, "main"));

    RET(ff_vk_shader_register_exec(s, pool, shd));

fail:
    if (spv_opaque)
        spv->free_shader(spv, &spv_opaque);

    return 0;
}

static void vk_decode_prores_uninit(FFVulkanDecodeShared *ctx)
{
    ProresVulkanDecodeContext *pv = ctx->sd_ctx;

    ff_vk_shader_free(&ctx->s, &pv->vld);
    ff_vk_shader_free(&ctx->s, &pv->idct);

    av_buffer_pool_uninit(&pv->metadata_pool);

    av_freep(&pv);
}

static int vk_decode_prores_init(AVCodecContext *avctx)
{
    FFVulkanDecodeContext *dec = avctx->internal->hwaccel_priv_data;
    FFVulkanDecodeShared  *ctx = NULL;
    ProresContext          *pr = avctx->priv_data;

    ProresVulkanDecodeContext *pv;
    FFVkSPIRVCompiler *spv;
    int max_num_mbs, err;

    max_num_mbs = (avctx->coded_width >> 4) * (avctx->coded_height >> 4);

    spv = ff_vk_spirv_init();
    if (!spv) {
        av_log(avctx, AV_LOG_ERROR, "Unable to initialize SPIR-V compiler!\n");
        return AVERROR_EXTERNAL;
    }

    err = ff_vk_decode_init(avctx);
    if (err < 0)
        return err;
    ctx = dec->shared_ctx;

    pv = ctx->sd_ctx = av_mallocz(sizeof(*pv));
    if (!pv) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    ctx->sd_ctx_free = vk_decode_prores_uninit;

    RET(init_decode_shader(avctx, &ctx->s, &ctx->exec_pool,
                           spv, &pv->vld, max_num_mbs, pr->frame_type != 0));
    RET(init_idct_shader(avctx, &ctx->s, &ctx->exec_pool,
                         spv, &pv->idct, max_num_mbs, pr->frame_type != 0));

    err = 0;

fail:
    spv->uninit(&spv);

    return err;
}

static void vk_prores_free_frame_priv(AVRefStructOpaque _hwctx, void *data)
{
    AVHWDeviceContext    *dev_ctx = _hwctx.nc;
    ProresVulkanDecodePicture *pp = data;

    ff_vk_decode_free_frame(dev_ctx, &pp->vp);

    av_buffer_unref(&pp->metadata_buf);
}

const FFHWAccel ff_prores_vulkan_hwaccel = {
    .p.name                = "prores_vulkan",
    .p.type                = AVMEDIA_TYPE_VIDEO,
    .p.id                  = AV_CODEC_ID_PRORES,
    .p.pix_fmt             = AV_PIX_FMT_VULKAN,
    .start_frame           = &vk_prores_start_frame,
    .decode_slice          = &vk_prores_decode_slice,
    .end_frame             = &vk_prores_end_frame,
    .free_frame_priv       = &vk_prores_free_frame_priv,
    .frame_priv_data_size  = sizeof(ProresVulkanDecodePicture),
    .init                  = &vk_decode_prores_init,
    .update_thread_context = &ff_vk_update_thread_context,
    .uninit                = &ff_vk_decode_uninit,
    .frame_params          = &ff_vk_frame_params,
    .priv_data_size        = sizeof(FFVulkanDecodeContext),
    .caps_internal         = HWACCEL_CAP_ASYNC_SAFE | HWACCEL_CAP_THREAD_SAFE,
};
