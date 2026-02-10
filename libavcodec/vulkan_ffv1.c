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

#include "vulkan_decode.h"
#include "hwaccel_internal.h"

#include "ffv1.h"
#include "ffv1_vulkan.h"
#include "libavutil/mem.h"

#define RGB_LINECACHE 2

extern const unsigned char ff_ffv1_dec_setup_comp_spv_data[];
extern const unsigned int ff_ffv1_dec_setup_comp_spv_len;

extern const unsigned char ff_ffv1_dec_reset_comp_spv_data[];
extern const unsigned int ff_ffv1_dec_reset_comp_spv_len;

extern const unsigned char ff_ffv1_dec_reset_golomb_comp_spv_data[];
extern const unsigned int ff_ffv1_dec_reset_golomb_comp_spv_len;

extern const unsigned char ff_ffv1_dec_comp_spv_data[];
extern const unsigned int ff_ffv1_dec_comp_spv_len;

extern const unsigned char ff_ffv1_dec_rgb_comp_spv_data[];
extern const unsigned int ff_ffv1_dec_rgb_comp_spv_len;

extern const unsigned char ff_ffv1_dec_golomb_comp_spv_data[];
extern const unsigned int ff_ffv1_dec_golomb_comp_spv_len;

extern const unsigned char ff_ffv1_dec_rgb_golomb_comp_spv_data[];
extern const unsigned int ff_ffv1_dec_rgb_golomb_comp_spv_len;

const FFVulkanDecodeDescriptor ff_vk_dec_ffv1_desc = {
    .codec_id         = AV_CODEC_ID_FFV1,
    .queue_flags      = VK_QUEUE_COMPUTE_BIT,
};

typedef struct FFv1VulkanDecodePicture {
    FFVulkanDecodePicture vp;

    AVBufferRef *slice_state;
    uint32_t plane_state_size;
    uint32_t slice_state_size;
    uint32_t slice_data_size;

    AVBufferRef *slice_feedback_buf;
    uint32_t    *slice_offset;
    int          slice_num;
    int          crc_checked;
} FFv1VulkanDecodePicture;

typedef struct FFv1VulkanDecodeContext {
    AVBufferRef *intermediate_frames_ref;

    FFVulkanShader setup;
    FFVulkanShader reset;
    FFVulkanShader decode;

    FFVkBuffer rangecoder_buf;
    FFVkBuffer quant_buf;
    FFVkBuffer crc_buf;

    AVBufferPool *slice_state_pool;
    AVBufferPool *slice_feedback_pool;
} FFv1VulkanDecodeContext;

static int vk_ffv1_start_frame(AVCodecContext          *avctx,
                               const AVBufferRef       *buffer_ref,
                               av_unused const uint8_t *buffer,
                               av_unused uint32_t       size)
{
    int err;
    FFVulkanDecodeContext *dec = avctx->internal->hwaccel_priv_data;
    FFVulkanDecodeShared *ctx = dec->shared_ctx;
    FFv1VulkanDecodeContext *fv = ctx->sd_ctx;
    FFV1Context *f = avctx->priv_data;

    FFv1VulkanDecodePicture *fp = f->hwaccel_picture_private;
    FFVulkanDecodePicture *vp = &fp->vp;

    AVHWFramesContext *hwfc = (AVHWFramesContext *)avctx->hw_frames_ctx->data;
    enum AVPixelFormat sw_format = hwfc->sw_format;

    int max_contexts;
    int is_rgb = !(f->colorspace == 0 && sw_format != AV_PIX_FMT_YA8) &&
                 !(sw_format == AV_PIX_FMT_YA8);

    fp->slice_num = 0;

    max_contexts = 0;
    for (int i = 0; i < f->quant_table_count; i++)
        max_contexts = FFMAX(f->context_count[i], max_contexts);

    /* Allocate slice buffer data */
    if (f->ac == AC_GOLOMB_RICE)
        fp->plane_state_size = 8;
    else
        fp->plane_state_size = CONTEXT_SIZE;

    fp->plane_state_size *= max_contexts;
    fp->slice_state_size = fp->plane_state_size*f->plane_count;

    fp->slice_data_size = 256; /* Overestimation for the SliceContext struct */
    fp->slice_state_size += fp->slice_data_size;
    fp->slice_state_size = FFALIGN(fp->slice_state_size, 8);

    fp->crc_checked = f->ec && (avctx->err_recognition & AV_EF_CRCCHECK);

    /* Host map the input slices data if supported */
    if (ctx->s.extensions & FF_VK_EXT_EXTERNAL_HOST_MEMORY)
        ff_vk_host_map_buffer(&ctx->s, &vp->slices_buf, buffer_ref->data,
                              buffer_ref,
                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                              VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);

    /* Allocate slice state data */
    if (f->picture.f->flags & AV_FRAME_FLAG_KEY) {
        err = ff_vk_get_pooled_buffer(&ctx->s, &fv->slice_state_pool,
                                      &fp->slice_state,
                                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                      NULL, f->slice_count*fp->slice_state_size,
                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (err < 0)
            return err;
    } else {
        FFv1VulkanDecodePicture *fpl = f->hwaccel_last_picture_private;
        fp->slice_state = av_buffer_ref(fpl->slice_state);
        if (!fp->slice_state)
            return AVERROR(ENOMEM);
    }

    /* Allocate slice offsets/status buffer */
    err = ff_vk_get_pooled_buffer(&ctx->s, &fv->slice_feedback_pool,
                                  &fp->slice_feedback_buf,
                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                  NULL, 2*(2*f->slice_count*sizeof(uint32_t)),
                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    if (err < 0)
        return err;

    /* Prepare frame to be used */
    err = ff_vk_decode_prepare_frame_sdr(dec, f->picture.f, vp, 1,
                                         FF_VK_REP_NATIVE, 0);
    if (err < 0)
        return err;

    /* Create a temporaty frame for RGB */
    if (is_rgb) {
        vp->dpb_frame = av_frame_alloc();
        if (!vp->dpb_frame)
            return AVERROR(ENOMEM);

        err = av_hwframe_get_buffer(fv->intermediate_frames_ref,
                                    vp->dpb_frame, 0);
        if (err < 0)
            return err;
    }

    return 0;
}

static int vk_ffv1_decode_slice(AVCodecContext *avctx,
                                const uint8_t  *data,
                                uint32_t        size)
{
    FFV1Context *f = avctx->priv_data;

    FFv1VulkanDecodePicture *fp = f->hwaccel_picture_private;
    FFVulkanDecodePicture *vp = &fp->vp;

    FFVkBuffer *slice_offset = (FFVkBuffer *)fp->slice_feedback_buf->data;
    FFVkBuffer *slices_buf = vp->slices_buf ? (FFVkBuffer *)vp->slices_buf->data : NULL;

    if (slices_buf && slices_buf->host_ref) {
        AV_WN32(slice_offset->mapped_mem + (2*fp->slice_num + 0)*sizeof(uint32_t),
                data - slices_buf->mapped_mem);
        AV_WN32(slice_offset->mapped_mem + (2*fp->slice_num + 1)*sizeof(uint32_t),
                size);

        fp->slice_num++;
    } else {
        int err = ff_vk_decode_add_slice(avctx, vp, data, size, 0,
                                         &fp->slice_num,
                                         (const uint32_t **)&fp->slice_offset);
        if (err < 0)
            return err;

        AV_WN32(slice_offset->mapped_mem + (2*(fp->slice_num - 1) + 0)*sizeof(uint32_t),
                fp->slice_offset[fp->slice_num - 1]);
        AV_WN32(slice_offset->mapped_mem + (2*(fp->slice_num - 1) + 1)*sizeof(uint32_t),
                size);
    }

    return 0;
}

static int vk_ffv1_end_frame(AVCodecContext *avctx)
{
    int err;
    FFVulkanDecodeContext *dec = avctx->internal->hwaccel_priv_data;
    FFVulkanDecodeShared *ctx = dec->shared_ctx;
    FFVulkanFunctions *vk = &ctx->s.vkfn;

    FFV1Context *f = avctx->priv_data;
    FFv1VulkanDecodeContext *fv = ctx->sd_ctx;

    AVHWFramesContext *hwfc = (AVHWFramesContext *)avctx->hw_frames_ctx->data;
    enum AVPixelFormat sw_format = hwfc->sw_format;

    int is_rgb = !(f->colorspace == 0 && sw_format != AV_PIX_FMT_YA8) &&
                 !(sw_format == AV_PIX_FMT_YA8);
    int color_planes = av_pix_fmt_desc_get(avctx->sw_pix_fmt)->nb_components;

    FFVulkanShader *reset_shader;
    FFVulkanShader *decode_shader;

    FFv1VulkanDecodePicture *fp = f->hwaccel_picture_private;
    FFVulkanDecodePicture *vp = &fp->vp;

    FFVkBuffer *slices_buf = (FFVkBuffer *)vp->slices_buf->data;
    FFVkBuffer *slice_state = (FFVkBuffer *)fp->slice_state->data;
    FFVkBuffer *slice_feedback = (FFVkBuffer *)fp->slice_feedback_buf->data;

    VkImageView rct_image_views[AV_NUM_DATA_POINTERS];

    AVFrame *decode_dst = is_rgb ? vp->dpb_frame : f->picture.f;
    VkImageView *decode_dst_view = is_rgb ? rct_image_views : vp->view.out;

    VkImageMemoryBarrier2 img_bar[37];
    int nb_img_bar = 0;
    VkBufferMemoryBarrier2 buf_bar[8];
    int nb_buf_bar = 0;

    FFVkExecContext *exec = ff_vk_exec_get(&ctx->s, &ctx->exec_pool);
    ff_vk_exec_start(&ctx->s, exec);

    /* Prepare deps */
    RET(ff_vk_exec_add_dep_frame(&ctx->s, exec, f->picture.f,
                                 VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT));

    err = ff_vk_exec_mirror_sem_value(&ctx->s, exec, &vp->sem, &vp->sem_value,
                                      f->picture.f);
    if (err < 0)
        return err;

    if (is_rgb) {
        RET(ff_vk_create_imageviews(&ctx->s, exec, rct_image_views,
                                    vp->dpb_frame, FF_VK_REP_NATIVE));
        RET(ff_vk_exec_add_dep_frame(&ctx->s, exec, vp->dpb_frame,
                                     VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                     VK_PIPELINE_STAGE_2_CLEAR_BIT));
        ff_vk_frame_barrier(&ctx->s, exec, decode_dst, img_bar, &nb_img_bar,
                            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                            VK_ACCESS_2_TRANSFER_WRITE_BIT,
                            VK_IMAGE_LAYOUT_GENERAL,
                            VK_QUEUE_FAMILY_IGNORED);
    }

    if (!(f->picture.f->flags & AV_FRAME_FLAG_KEY)) {
        FFv1VulkanDecodePicture *fpl = f->hwaccel_last_picture_private;
        FFVulkanDecodePicture *vpl = &fpl->vp;

        /* Wait on the previous frame */
        RET(ff_vk_exec_add_dep_wait_sem(&ctx->s, exec, vpl->sem, vpl->sem_value,
                                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT));
    }

    RET(ff_vk_exec_add_dep_buf(&ctx->s, exec, &fp->slice_state, 1, 1));
    RET(ff_vk_exec_add_dep_buf(&ctx->s, exec, &fp->slice_feedback_buf, 1, 1));
    RET(ff_vk_exec_add_dep_buf(&ctx->s, exec, &vp->slices_buf, 1, 0));
    vp->slices_buf = NULL;

    /* Entry barrier for the slice state (not preserved between frames) */
    if (!(f->picture.f->flags & AV_FRAME_FLAG_KEY))
        ff_vk_buf_barrier(buf_bar[nb_buf_bar++], slice_state,
                          ALL_COMMANDS_BIT, NONE_KHR, NONE_KHR,
                          COMPUTE_SHADER_BIT, SHADER_STORAGE_READ_BIT,
                                              SHADER_STORAGE_WRITE_BIT,
                          0, fp->slice_data_size*f->slice_count);
    else
        ff_vk_buf_barrier(buf_bar[nb_buf_bar++], slice_state,
                          COMPUTE_SHADER_BIT, SHADER_STORAGE_READ_BIT,
                                              SHADER_STORAGE_WRITE_BIT,
                          COMPUTE_SHADER_BIT, SHADER_STORAGE_READ_BIT,
                                              SHADER_STORAGE_WRITE_BIT,
                          0, fp->slice_data_size*f->slice_count);
    vk->CmdPipelineBarrier2(exec->buf, &(VkDependencyInfo) {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pImageMemoryBarriers = img_bar,
        .imageMemoryBarrierCount = nb_img_bar,
        .pBufferMemoryBarriers = buf_bar,
        .bufferMemoryBarrierCount = nb_buf_bar,
    });
    nb_buf_bar = 0;
    nb_img_bar = 0;

    /* Setup shader */
    ff_vk_shader_update_desc_buffer(&ctx->s, exec, &fv->setup,
                                    1, 0, 0,
                                    slice_state,
                                    0, fp->slice_data_size*f->slice_count,
                                    VK_FORMAT_UNDEFINED);
    ff_vk_shader_update_desc_buffer(&ctx->s, exec, &fv->setup,
                                    1, 1, 0,
                                    slice_feedback,
                                    0, 2*f->slice_count*sizeof(uint32_t),
                                    VK_FORMAT_UNDEFINED);
    ff_vk_shader_update_desc_buffer(&ctx->s, exec, &fv->setup,
                                    1, 2, 0,
                                    slice_feedback,
                                    2*f->slice_count*sizeof(uint32_t),
                                    VK_WHOLE_SIZE,
                                    VK_FORMAT_UNDEFINED);

    ff_vk_exec_bind_shader(&ctx->s, exec, &fv->setup);

    FFv1ShaderParams pd = {
        .slice_data = slices_buf->address,

        .img_size[0] = f->picture.f->width,
        .img_size[1] = f->picture.f->height,

        .plane_state_size = fp->plane_state_size,
        .key_frame = f->picture.f->flags & AV_FRAME_FLAG_KEY,
        .crcref = f->crcref,
        .micro_version = f->micro_version,
    };

    for (int i = 0; i < f->quant_table_count; i++) {
        pd.context_count[i] = f->context_count[i];
        pd.extend_lookup[i] = f->quant_tables[i][3][127] ||
                              f->quant_tables[i][4][127];
    }

    /* For some reason the C FFv1 encoder/decoder treats these differently */
    if (sw_format == AV_PIX_FMT_GBRP10 || sw_format == AV_PIX_FMT_GBRP12 ||
        sw_format == AV_PIX_FMT_GBRP14)
        memcpy(pd.fmt_lut, (int [4]) { 2, 1, 0, 3 }, 4*sizeof(int));
    else
        ff_vk_set_perm(sw_format, pd.fmt_lut, 0);

    ff_vk_shader_update_push_const(&ctx->s, exec, &fv->setup,
                                   VK_SHADER_STAGE_COMPUTE_BIT,
                                   0, sizeof(FFv1ShaderParams), &pd);

    vk->CmdDispatch(exec->buf, f->num_h_slices, f->num_v_slices, 1);

    if (is_rgb) {
        AVVkFrame *vkf = (AVVkFrame *)vp->dpb_frame->data[0];
        for (int i = 0; i < color_planes; i++)
            vk->CmdClearColorImage(exec->buf, vkf->img[i], VK_IMAGE_LAYOUT_GENERAL,
                                   &((VkClearColorValue) { 0 }),
                                   1, &((VkImageSubresourceRange) {
                                       .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                       .levelCount = 1,
                                       .layerCount = 1,
                                   }));
    }

    /* Reset shader */
    reset_shader = &fv->reset;
    ff_vk_shader_update_desc_buffer(&ctx->s, exec, reset_shader,
                                    1, 0, 0,
                                    slice_state,
                                    0, fp->slice_data_size*f->slice_count,
                                    VK_FORMAT_UNDEFINED);
    ff_vk_shader_update_desc_buffer(&ctx->s, exec, reset_shader,
                                    1, 1, 0,
                                    slice_state,
                                    f->slice_count*fp->slice_data_size,
                                    VK_WHOLE_SIZE,
                                    VK_FORMAT_UNDEFINED);

    ff_vk_exec_bind_shader(&ctx->s, exec, reset_shader);
    ff_vk_shader_update_push_const(&ctx->s, exec, reset_shader,
                                   VK_SHADER_STAGE_COMPUTE_BIT,
                                   0, sizeof(FFv1ShaderParams), &pd);

    /* Sync between setup and reset shaders */
    ff_vk_buf_barrier(buf_bar[nb_buf_bar++], slice_state,
                      COMPUTE_SHADER_BIT, SHADER_STORAGE_READ_BIT,
                                          SHADER_STORAGE_WRITE_BIT,
                      COMPUTE_SHADER_BIT, SHADER_STORAGE_READ_BIT, NONE_KHR,
                      0, fp->slice_data_size*f->slice_count);
    /* Probability data barrier */
    if (!(f->picture.f->flags & AV_FRAME_FLAG_KEY))
        ff_vk_buf_barrier(buf_bar[nb_buf_bar++], slice_state,
                          ALL_COMMANDS_BIT, NONE_KHR, NONE_KHR,
                          COMPUTE_SHADER_BIT, SHADER_STORAGE_WRITE_BIT, NONE_KHR,
                          fp->slice_data_size*f->slice_count, VK_WHOLE_SIZE);
    else
        ff_vk_buf_barrier(buf_bar[nb_buf_bar++], slice_state,
                          COMPUTE_SHADER_BIT, SHADER_STORAGE_READ_BIT,
                                              SHADER_STORAGE_WRITE_BIT,
                          COMPUTE_SHADER_BIT, SHADER_STORAGE_WRITE_BIT, NONE_KHR,
                          fp->slice_data_size*f->slice_count, VK_WHOLE_SIZE);
    vk->CmdPipelineBarrier2(exec->buf, &(VkDependencyInfo) {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pImageMemoryBarriers = img_bar,
        .imageMemoryBarrierCount = nb_img_bar,
        .pBufferMemoryBarriers = buf_bar,
        .bufferMemoryBarrierCount = nb_buf_bar,
    });
    nb_buf_bar = 0;
    nb_img_bar = 0;

    vk->CmdDispatch(exec->buf, f->num_h_slices, f->num_v_slices,
                    f->plane_count);

    /* Decode */
    decode_shader = &fv->decode;
    ff_vk_shader_update_desc_buffer(&ctx->s, exec, decode_shader,
                                    1, 0, 0,
                                    slice_state,
                                    0, fp->slice_data_size*f->slice_count,
                                    VK_FORMAT_UNDEFINED);
    ff_vk_shader_update_desc_buffer(&ctx->s, exec, decode_shader,
                                    1, 1, 0,
                                    slice_feedback,
                                    0, 2*f->slice_count*sizeof(uint32_t),
                                    VK_FORMAT_UNDEFINED);
    ff_vk_shader_update_desc_buffer(&ctx->s, exec, decode_shader,
                                    1, 2, 0,
                                    slice_feedback,
                                    2*f->slice_count*sizeof(uint32_t),
                                    VK_WHOLE_SIZE,
                                    VK_FORMAT_UNDEFINED);
    ff_vk_shader_update_desc_buffer(&ctx->s, exec, decode_shader,
                                    1, 3, 0,
                                    slice_state,
                                    f->slice_count*fp->slice_data_size,
                                    VK_WHOLE_SIZE,
                                    VK_FORMAT_UNDEFINED);

    ff_vk_shader_update_img_array(&ctx->s, exec, decode_shader,
                                  decode_dst, decode_dst_view,
                                  1, 4,
                                  VK_IMAGE_LAYOUT_GENERAL,
                                  VK_NULL_HANDLE);
    if (is_rgb)
        ff_vk_shader_update_img_array(&ctx->s, exec, decode_shader,
                                      f->picture.f, vp->view.out,
                                      1, 5,
                                      VK_IMAGE_LAYOUT_GENERAL,
                                      VK_NULL_HANDLE);

    ff_vk_exec_bind_shader(&ctx->s, exec, decode_shader);
    ff_vk_shader_update_push_const(&ctx->s, exec, decode_shader,
                                   VK_SHADER_STAGE_COMPUTE_BIT,
                                   0, sizeof(FFv1ShaderParams), &pd);

    /* Sync probabilities between reset and decode shaders */
    ff_vk_buf_barrier(buf_bar[nb_buf_bar++], slice_state,
                      COMPUTE_SHADER_BIT, SHADER_STORAGE_READ_BIT, NONE_KHR,
                      COMPUTE_SHADER_BIT, SHADER_STORAGE_READ_BIT,
                                          SHADER_STORAGE_WRITE_BIT,
                      0, fp->slice_data_size*f->slice_count);
    ff_vk_buf_barrier(buf_bar[nb_buf_bar++], slice_state,
                      COMPUTE_SHADER_BIT, SHADER_STORAGE_WRITE_BIT, NONE_KHR,
                      COMPUTE_SHADER_BIT, SHADER_STORAGE_READ_BIT,
                                          SHADER_STORAGE_WRITE_BIT,
                      fp->slice_data_size*f->slice_count, VK_WHOLE_SIZE);
    /* Input frame barrier */
    ff_vk_frame_barrier(&ctx->s, exec, f->picture.f, img_bar, &nb_img_bar,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_SHADER_WRITE_BIT |
                        (!is_rgb ? VK_ACCESS_SHADER_READ_BIT : 0),
                        VK_IMAGE_LAYOUT_GENERAL,
                        VK_QUEUE_FAMILY_IGNORED);
    if (is_rgb)
        ff_vk_frame_barrier(&ctx->s, exec, vp->dpb_frame, img_bar, &nb_img_bar,
                            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                            VK_IMAGE_LAYOUT_GENERAL,
                            VK_QUEUE_FAMILY_IGNORED);

    vk->CmdPipelineBarrier2(exec->buf, &(VkDependencyInfo) {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pImageMemoryBarriers = img_bar,
        .imageMemoryBarrierCount = nb_img_bar,
        .pBufferMemoryBarriers = buf_bar,
        .bufferMemoryBarrierCount = nb_buf_bar,
    });
    nb_img_bar = 0;
    nb_buf_bar = 0;

    vk->CmdDispatch(exec->buf, f->num_h_slices, f->num_v_slices, 1);

    err = ff_vk_exec_submit(&ctx->s, exec);
    if (err < 0)
        return err;

    /* We don't need the temporary frame after decoding */
    av_frame_free(&vp->dpb_frame);

fail:
    return 0;
}

static int init_setup_shader(FFV1Context *f, FFVulkanContext *s,
                             FFVkExecPool *pool, FFVulkanShader *shd,
                             VkSpecializationInfo *sl)
{
    int err;

    ff_vk_shader_load(shd, VK_SHADER_STAGE_COMPUTE_BIT, sl,
                      (uint32_t []) { 1, 1, 1 }, 0);

    ff_vk_shader_add_push_const(shd, 0, sizeof(FFv1ShaderParams),
                                VK_SHADER_STAGE_COMPUTE_BIT);

    const FFVulkanDescriptorSetBinding desc_set_const[] = {
        { /* rangecoder_buf */
            .type   = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        { /* crc_ieee_buf */
            .type   = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .stages = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };
    ff_vk_shader_add_descriptor_set(s, shd, desc_set_const, 2, 1, 0);

    const FFVulkanDescriptorSetBinding desc_set[] = {
        { /* slice_data_buf */
            .type   = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        { /* slice_offsets_buf */
            .type   = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        { /* slice_status_buf */
            .type   = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };
    ff_vk_shader_add_descriptor_set(s, shd, desc_set, 3, 0, 0);

    RET(ff_vk_shader_link(s, shd,
                          ff_ffv1_dec_setup_comp_spv_data,
                          ff_ffv1_dec_setup_comp_spv_len, "main"));

    RET(ff_vk_shader_register_exec(s, pool, shd));

fail:
    return err;
}

static int init_reset_shader(FFV1Context *f, FFVulkanContext *s,
                             FFVkExecPool *pool, FFVulkanShader *shd,
                             VkSpecializationInfo *sl, int ac)
{
    int err;
    int wg_dim = FFMIN(s->props.properties.limits.maxComputeWorkGroupSize[0], 1024);

    ff_vk_shader_load(shd, VK_SHADER_STAGE_COMPUTE_BIT, sl,
                      (uint32_t []) { wg_dim, 1, 1 }, 0);

    ff_vk_shader_add_push_const(shd, 0, sizeof(FFv1ShaderParams),
                                VK_SHADER_STAGE_COMPUTE_BIT);

    const FFVulkanDescriptorSetBinding desc_set_const[] = {
        { /* rangecoder_buf */
            .type   = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };
    ff_vk_shader_add_descriptor_set(s, shd, desc_set_const, 1, 1, 0);

    const FFVulkanDescriptorSetBinding desc_set[] = {
        { /* slice_data_buf */
            .type   = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        { /* slice_state_buf */
            .type   = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };
    ff_vk_shader_add_descriptor_set(s, shd, desc_set, 2, 0, 0);

    if (ac == AC_GOLOMB_RICE)
        RET(ff_vk_shader_link(s, shd,
                              ff_ffv1_dec_reset_golomb_comp_spv_data,
                              ff_ffv1_dec_reset_golomb_comp_spv_len, "main"));
    else
        RET(ff_vk_shader_link(s, shd,
                              ff_ffv1_dec_reset_comp_spv_data,
                              ff_ffv1_dec_reset_comp_spv_len, "main"));

    RET(ff_vk_shader_register_exec(s, pool, shd));

fail:
    return err;
}

static int init_decode_shader(FFV1Context *f, FFVulkanContext *s,
                              FFVkExecPool *pool, FFVulkanShader *shd,
                              AVHWFramesContext *dec_frames_ctx,
                              AVHWFramesContext *out_frames_ctx,
                              VkSpecializationInfo *sl, int ac, int rgb)
{
    int err;

    uint32_t wg_x = ac != AC_GOLOMB_RICE ? CONTEXT_SIZE : 1;
    ff_vk_shader_load(shd, VK_SHADER_STAGE_COMPUTE_BIT, sl,
                      (uint32_t []) { wg_x, 1, 1 }, 0);

    ff_vk_shader_add_push_const(shd, 0, sizeof(FFv1ShaderParams),
                                VK_SHADER_STAGE_COMPUTE_BIT);

    const FFVulkanDescriptorSetBinding desc_set_const[] = {
        { /* rangecoder_buf */
            .type   = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        { /* quant_buf */
            .type   = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .stages = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };
    ff_vk_shader_add_descriptor_set(s, shd, desc_set_const, 2, 1, 0);

    const FFVulkanDescriptorSetBinding desc_set[] = {
        { /* slice_data_buf */
            .type   = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        { /* slice_offsets_buf */
            .type   = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        { /* slice_status_buf */
            .type   = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        { /* slice_state_buf */
            .type   = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        { /* dec */
            .type   = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stages = VK_SHADER_STAGE_COMPUTE_BIT,
            .elems  = av_pix_fmt_count_planes(dec_frames_ctx->sw_format),
        },
        { /* dst */
            .type   = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stages = VK_SHADER_STAGE_COMPUTE_BIT,
            .elems  = av_pix_fmt_count_planes(out_frames_ctx->sw_format),
        },
    };
    ff_vk_shader_add_descriptor_set(s, shd, desc_set, 5 + rgb, 0, 0);

    if (ac == AC_GOLOMB_RICE) {
        if (rgb)
            ff_vk_shader_link(s, shd,
                              ff_ffv1_dec_rgb_golomb_comp_spv_data,
                              ff_ffv1_dec_rgb_golomb_comp_spv_len, "main");
        else
            ff_vk_shader_link(s, shd,
                              ff_ffv1_dec_golomb_comp_spv_data,
                              ff_ffv1_dec_golomb_comp_spv_len, "main");
    } else {
        if (rgb)
            ff_vk_shader_link(s, shd,
                              ff_ffv1_dec_rgb_comp_spv_data,
                              ff_ffv1_dec_rgb_comp_spv_len, "main");
        else
            ff_vk_shader_link(s, shd,
                              ff_ffv1_dec_comp_spv_data,
                              ff_ffv1_dec_comp_spv_len, "main");
    }

    RET(ff_vk_shader_register_exec(s, pool, shd));

fail:
    return err;
}

static int init_indirect(AVCodecContext *avctx, FFVulkanContext *s,
                         AVBufferRef **dst, enum AVPixelFormat sw_format)
{
    int err;
    AVHWFramesContext *frames_ctx;
    AVVulkanFramesContext *vk_frames;
    FFV1Context *f = avctx->priv_data;

    *dst = av_hwframe_ctx_alloc(s->device_ref);
    if (!(*dst))
        return AVERROR(ENOMEM);

    frames_ctx = (AVHWFramesContext *)((*dst)->data);
    frames_ctx->format    = AV_PIX_FMT_VULKAN;
    frames_ctx->sw_format = sw_format;
    frames_ctx->width     = s->frames->width;
    frames_ctx->height    = f->num_v_slices*RGB_LINECACHE;

    vk_frames = frames_ctx->hwctx;
    vk_frames->tiling    = VK_IMAGE_TILING_OPTIMAL;
    vk_frames->img_flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
    vk_frames->usage     = VK_IMAGE_USAGE_STORAGE_BIT |
                           VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    err = av_hwframe_ctx_init(*dst);
    if (err < 0) {
        av_log(avctx, AV_LOG_ERROR,
               "Unable to initialize frame pool with format %s: %s\n",
               av_get_pix_fmt_name(sw_format), av_err2str(err));
        av_buffer_unref(dst);
        return err;
    }

    return 0;
}

static void vk_decode_ffv1_uninit(FFVulkanDecodeShared *ctx)
{
    FFv1VulkanDecodeContext *fv = ctx->sd_ctx;

    av_buffer_unref(&fv->intermediate_frames_ref);

    ff_vk_shader_free(&ctx->s, &fv->setup);
    ff_vk_shader_free(&ctx->s, &fv->reset);
    ff_vk_shader_free(&ctx->s, &fv->decode);

    ff_vk_free_buf(&ctx->s, &fv->rangecoder_buf);
    ff_vk_free_buf(&ctx->s, &fv->quant_buf);
    ff_vk_free_buf(&ctx->s, &fv->crc_buf);

    av_buffer_pool_uninit(&fv->slice_state_pool);
    av_buffer_pool_uninit(&fv->slice_feedback_pool);

    av_freep(&fv);
}

static int vk_decode_ffv1_init(AVCodecContext *avctx)
{
    int err;
    FFV1Context *f = avctx->priv_data;
    FFVulkanDecodeContext *dec = avctx->internal->hwaccel_priv_data;
    FFVulkanDecodeShared *ctx = NULL;
    FFv1VulkanDecodeContext *fv;

    if (f->version < 3 ||
        (f->version == 4 && f->micro_version > 3))
        return AVERROR(ENOTSUP);

    err = ff_vk_decode_init(avctx);
    if (err < 0)
        return err;
    ctx = dec->shared_ctx;

    fv = ctx->sd_ctx = av_mallocz(sizeof(*fv));
    if (!fv) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    ctx->sd_ctx_free = &vk_decode_ffv1_uninit;

    AVHWFramesContext *hwfc = (AVHWFramesContext *)avctx->hw_frames_ctx->data;
    AVHWFramesContext *dctx = hwfc;
    enum AVPixelFormat sw_format = hwfc->sw_format;
    int is_rgb = !(f->colorspace == 0 && sw_format != AV_PIX_FMT_YA8) &&
                 !(sw_format == AV_PIX_FMT_YA8);

    /* Intermediate frame pool for RCT */
    if (is_rgb) {
        RET(init_indirect(avctx, &ctx->s, &fv->intermediate_frames_ref,
                          f->use32bit ? AV_PIX_FMT_GBRAP32 : AV_PIX_FMT_GBRAP16));
        dctx = (AVHWFramesContext *)fv->intermediate_frames_ref->data;
    }

    SPEC_LIST_CREATE(sl, 15, 15*sizeof(uint32_t))
    ff_ffv1_vk_set_common_sl(avctx, f, sl, sw_format);

    if (RGB_LINECACHE != 2)
        SPEC_LIST_ADD(sl, 0, 32, RGB_LINECACHE);

    if (f->ec && !!(avctx->err_recognition & AV_EF_CRCCHECK))
        SPEC_LIST_ADD(sl, 1, 32, 1);

    /* Setup shader */
    RET(init_setup_shader(f, &ctx->s, &ctx->exec_pool, &fv->setup, sl));

    /* Reset shader */
    RET(init_reset_shader(f, &ctx->s, &ctx->exec_pool, &fv->reset, sl, f->ac));

    /* Decode shaders */
    RET(init_decode_shader(f, &ctx->s, &ctx->exec_pool, &fv->decode,
                           dctx, hwfc, sl, f->ac, is_rgb));

    /* Init static data */
    RET(ff_ffv1_vk_init_state_transition_data(&ctx->s, &fv->rangecoder_buf, f));
    RET(ff_ffv1_vk_init_crc_table_data(&ctx->s, &fv->crc_buf, f));
    RET(ff_ffv1_vk_init_quant_table_data(&ctx->s, &fv->quant_buf, f));

    /* Update setup global descriptors */
    RET(ff_vk_shader_update_desc_buffer(&ctx->s, &ctx->exec_pool.contexts[0],
                                        &fv->setup, 0, 0, 0,
                                        &fv->rangecoder_buf,
                                        0, 512*sizeof(uint8_t),
                                        VK_FORMAT_UNDEFINED));
    RET(ff_vk_shader_update_desc_buffer(&ctx->s, &ctx->exec_pool.contexts[0],
                                        &fv->setup, 0, 1, 0,
                                        &fv->crc_buf,
                                        0, 256*sizeof(uint32_t),
                                        VK_FORMAT_UNDEFINED));

    /* Update decode global descriptors */
    RET(ff_vk_shader_update_desc_buffer(&ctx->s, &ctx->exec_pool.contexts[0],
                                        &fv->decode, 0, 0, 0,
                                        &fv->rangecoder_buf,
                                        0, 512*sizeof(uint8_t),
                                        VK_FORMAT_UNDEFINED));
    RET(ff_vk_shader_update_desc_buffer(&ctx->s, &ctx->exec_pool.contexts[0],
                                        &fv->decode, 0, 1, 0,
                                        &fv->quant_buf,
                                        0, VK_WHOLE_SIZE,
                                        VK_FORMAT_UNDEFINED));

fail:
    return err;
}

static void vk_ffv1_free_frame_priv(AVRefStructOpaque _hwctx, void *data)
{
    AVHWDeviceContext *dev_ctx = _hwctx.nc;
    AVVulkanDeviceContext *hwctx = dev_ctx->hwctx;

    FFv1VulkanDecodePicture *fp = data;
    FFVulkanDecodePicture *vp = &fp->vp;
    FFVkBuffer *slice_feedback = (FFVkBuffer *)fp->slice_feedback_buf->data;
    uint8_t *ssp = slice_feedback->mapped_mem + 2*fp->slice_num*sizeof(uint32_t);

    ff_vk_decode_free_frame(dev_ctx, vp);

    /* Invalidate slice/output data if needed */
    if (!(slice_feedback->flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        VkMappedMemoryRange invalidate_data = {
            .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .memory = slice_feedback->mem,
            .offset = 0,
            .size = 2*fp->slice_num*sizeof(uint32_t),
        };
        vp->invalidate_memory_ranges(hwctx->act_dev,
                                     1, &invalidate_data);
    }

    int slice_error_cnt = 0;
    int crc_mismatch_cnt = 0;
    uint32_t max_overread = 0;
    for (int i = 0; i < fp->slice_num; i++) {
        uint32_t crc_res = 0;
        if (fp->crc_checked)
            crc_res = AV_RN32(ssp + 2*i*sizeof(uint32_t) + 0);
        uint32_t overread = AV_RN32(ssp + 2*i*sizeof(uint32_t) + 4);
        max_overread = FFMAX(overread, max_overread);
        slice_error_cnt += !!overread;
        crc_mismatch_cnt += !!crc_res;
    }
    if (slice_error_cnt || crc_mismatch_cnt)
        av_log(dev_ctx, AV_LOG_ERROR, "Decode status: %i slices overread (%i bytes max), "
                                      "%i CRCs mismatched\n",
               slice_error_cnt, max_overread, crc_mismatch_cnt);

    av_buffer_unref(&fp->slice_state);
    av_buffer_unref(&fp->slice_feedback_buf);
}

const FFHWAccel ff_ffv1_vulkan_hwaccel = {
    .p.name                = "ffv1_vulkan",
    .p.type                = AVMEDIA_TYPE_VIDEO,
    .p.id                  = AV_CODEC_ID_FFV1,
    .p.pix_fmt             = AV_PIX_FMT_VULKAN,
    .start_frame           = &vk_ffv1_start_frame,
    .decode_slice          = &vk_ffv1_decode_slice,
    .end_frame             = &vk_ffv1_end_frame,
    .free_frame_priv       = &vk_ffv1_free_frame_priv,
    .frame_priv_data_size  = sizeof(FFv1VulkanDecodePicture),
    .init                  = &vk_decode_ffv1_init,
    .update_thread_context = &ff_vk_update_thread_context,
    .uninit                = &ff_vk_decode_uninit,
    .frame_params          = &ff_vk_frame_params,
    .priv_data_size        = sizeof(FFVulkanDecodeContext),
    .caps_internal         = HWACCEL_CAP_ASYNC_SAFE | HWACCEL_CAP_THREAD_SAFE,
};
