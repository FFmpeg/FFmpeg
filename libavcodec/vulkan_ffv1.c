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
#include "libavutil/vulkan_spirv.h"
#include "libavutil/mem.h"

#define RGB_LINECACHE 2

extern const char *ff_source_common_comp;
extern const char *ff_source_rangecoder_comp;
extern const char *ff_source_ffv1_vlc_comp;
extern const char *ff_source_ffv1_common_comp;
extern const char *ff_source_ffv1_dec_setup_comp;
extern const char *ff_source_ffv1_reset_comp;
extern const char *ff_source_ffv1_dec_comp;

const FFVulkanDecodeDescriptor ff_vk_dec_ffv1_desc = {
    .codec_id         = AV_CODEC_ID_FFV1,
    .decode_extension = FF_VK_EXT_PUSH_DESCRIPTOR,
    .queue_flags      = VK_QUEUE_COMPUTE_BIT,
};

typedef struct FFv1VulkanDecodePicture {
    FFVulkanDecodePicture vp;

    AVBufferRef *slice_state;
    uint32_t plane_state_size;
    uint32_t slice_state_size;
    uint32_t slice_data_size;

    AVBufferRef *slice_offset_buf;
    uint32_t    *slice_offset;
    int          slice_num;

    AVBufferRef *slice_status_buf;
    int crc_checked;
} FFv1VulkanDecodePicture;

typedef struct FFv1VulkanDecodeContext {
    AVBufferRef *intermediate_frames_ref[2]; /* 16/32 bit */

    FFVulkanShader setup;
    FFVulkanShader reset[2]; /* AC/Golomb */
    FFVulkanShader decode[2][2][2]; /* 16/32 bit, AC/Golomb, Normal/RGB */

    FFVkBuffer rangecoder_static_buf;
    FFVkBuffer quant_buf;
    FFVkBuffer crc_tab_buf;

    AVBufferPool *slice_state_pool;
    AVBufferPool *slice_offset_pool;
    AVBufferPool *slice_status_pool;
} FFv1VulkanDecodeContext;

typedef struct FFv1VkParameters {
    VkDeviceAddress slice_data;
    VkDeviceAddress slice_state;

    int fmt_lut[4];
    uint32_t img_size[2];
    uint32_t chroma_shift[2];

    uint32_t plane_state_size;
    uint32_t crcref;
    int rct_offset;

    uint8_t extend_lookup[8];
    uint8_t bits_per_raw_sample;
    uint8_t quant_table_count;
    uint8_t version;
    uint8_t micro_version;
    uint8_t key_frame;
    uint8_t planes;
    uint8_t codec_planes;
    uint8_t color_planes;
    uint8_t transparency;
    uint8_t planar_rgb;
    uint8_t colorspace;
    uint8_t ec;
    uint8_t golomb;
    uint8_t check_crc;
    uint8_t padding[3];
} FFv1VkParameters;

static void add_push_data(FFVulkanShader *shd)
{
    GLSLC(0, layout(push_constant, scalar) uniform pushConstants {  );
    GLSLC(1,    u8buf slice_data;                                   );
    GLSLC(1,    u8buf slice_state;                                  );
    GLSLC(0,                                                        );
    GLSLC(1,    ivec4 fmt_lut;                                      );
    GLSLC(1,    uvec2 img_size;                                     );
    GLSLC(1,    uvec2 chroma_shift;                                 );
    GLSLC(0,                                                        );
    GLSLC(1,    uint plane_state_size;                              );
    GLSLC(1,    uint32_t crcref;                                    );
    GLSLC(1,    int rct_offset;                                     );
    GLSLC(0,                                                        );
    GLSLC(1,    uint8_t extend_lookup[8];                           );
    GLSLC(1,    uint8_t bits_per_raw_sample;                        );
    GLSLC(1,    uint8_t quant_table_count;                          );
    GLSLC(1,    uint8_t version;                                    );
    GLSLC(1,    uint8_t micro_version;                              );
    GLSLC(1,    uint8_t key_frame;                                  );
    GLSLC(1,    uint8_t planes;                                     );
    GLSLC(1,    uint8_t codec_planes;                               );
    GLSLC(1,    uint8_t color_planes;                               );
    GLSLC(1,    uint8_t transparency;                               );
    GLSLC(1,    uint8_t planar_rgb;                                 );
    GLSLC(1,    uint8_t colorspace;                                 );
    GLSLC(1,    uint8_t ec;                                         );
    GLSLC(1,    uint8_t golomb;                                     );
    GLSLC(1,    uint8_t check_crc;                                  );
    GLSLC(1,    uint8_t padding[3];                                 );
    GLSLC(0, };                                                     );
    ff_vk_shader_add_push_const(shd, 0, sizeof(FFv1VkParameters),
                                VK_SHADER_STAGE_COMPUTE_BIT);
}

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
                                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
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

    /* Allocate slice offsets buffer */
    err = ff_vk_get_pooled_buffer(&ctx->s, &fv->slice_offset_pool,
                                  &fp->slice_offset_buf,
                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                  VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                  NULL, 2*f->slice_count*sizeof(uint32_t),
                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    if (err < 0)
        return err;

    /* Allocate slice status buffer */
    err = ff_vk_get_pooled_buffer(&ctx->s, &fv->slice_status_pool,
                                  &fp->slice_status_buf,
                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                  VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                  NULL, 2*f->slice_count*sizeof(uint32_t),
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

        err = av_hwframe_get_buffer(fv->intermediate_frames_ref[f->use32bit],
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

    FFVkBuffer *slice_offset = (FFVkBuffer *)fp->slice_offset_buf->data;
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
    FFv1VkParameters pd;
    FFv1VkResetParameters pd_reset;

    AVHWFramesContext *hwfc = (AVHWFramesContext *)avctx->hw_frames_ctx->data;
    enum AVPixelFormat sw_format = hwfc->sw_format;

    int bits = f->avctx->bits_per_raw_sample > 0 ? f->avctx->bits_per_raw_sample : 8;
    int is_rgb = !(f->colorspace == 0 && sw_format != AV_PIX_FMT_YA8) &&
                 !(sw_format == AV_PIX_FMT_YA8);
    int color_planes = av_pix_fmt_desc_get(avctx->sw_pix_fmt)->nb_components;

    FFVulkanShader *reset_shader;
    FFVulkanShader *decode_shader;

    FFv1VulkanDecodePicture *fp = f->hwaccel_picture_private;
    FFVulkanDecodePicture *vp = &fp->vp;

    FFVkBuffer *slices_buf = (FFVkBuffer *)vp->slices_buf->data;
    FFVkBuffer *slice_state = (FFVkBuffer *)fp->slice_state->data;
    FFVkBuffer *slice_offset = (FFVkBuffer *)fp->slice_offset_buf->data;
    FFVkBuffer *slice_status = (FFVkBuffer *)fp->slice_status_buf->data;

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
    RET(ff_vk_exec_add_dep_buf(&ctx->s, exec, &fp->slice_status_buf, 1, 1));
    RET(ff_vk_exec_add_dep_buf(&ctx->s, exec, &vp->slices_buf, 1, 0));
    vp->slices_buf = NULL;
    RET(ff_vk_exec_add_dep_buf(&ctx->s, exec, &fp->slice_offset_buf, 1, 0));
    fp->slice_offset_buf = NULL;

    /* Entry barrier for the slice state */
    buf_bar[nb_buf_bar++] = (VkBufferMemoryBarrier2) {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .srcStageMask = slice_state->stage,
        .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .srcAccessMask = slice_state->access,
        .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                         VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = slice_state->buf,
        .offset = 0,
        .size = fp->slice_data_size*f->slice_count,
    };

    vk->CmdPipelineBarrier2(exec->buf, &(VkDependencyInfo) {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pImageMemoryBarriers = img_bar,
        .imageMemoryBarrierCount = nb_img_bar,
        .pBufferMemoryBarriers = buf_bar,
        .bufferMemoryBarrierCount = nb_buf_bar,
    });
    slice_state->stage = buf_bar[0].dstStageMask;
    slice_state->access = buf_bar[0].dstAccessMask;
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
                                    slice_offset,
                                    0, 2*f->slice_count*sizeof(uint32_t),
                                    VK_FORMAT_UNDEFINED);
    ff_vk_shader_update_desc_buffer(&ctx->s, exec, &fv->setup,
                                    1, 2, 0,
                                    slice_status,
                                    0, 2*f->slice_count*sizeof(uint32_t),
                                    VK_FORMAT_UNDEFINED);

    ff_vk_exec_bind_shader(&ctx->s, exec, &fv->setup);
    pd = (FFv1VkParameters) {
        .slice_data = slices_buf->address,
        .slice_state  = slice_state->address + f->slice_count*fp->slice_data_size,

        .img_size[0] = f->picture.f->width,
        .img_size[1] = f->picture.f->height,
        .chroma_shift[0] = f->chroma_h_shift,
        .chroma_shift[1] = f->chroma_v_shift,

        .plane_state_size = fp->plane_state_size,
        .crcref = f->crcref,
        .rct_offset = 1 << bits,

        .bits_per_raw_sample = bits,
        .quant_table_count = f->quant_table_count,
        .version = f->version,
        .micro_version = f->micro_version,
        .key_frame = f->picture.f->flags & AV_FRAME_FLAG_KEY,
        .planes = av_pix_fmt_count_planes(sw_format),
        .codec_planes = f->plane_count,
        .color_planes = color_planes,
        .transparency = f->transparency,
        .planar_rgb = ff_vk_mt_is_np_rgb(sw_format) &&
                      (ff_vk_count_images((AVVkFrame *)f->picture.f->data[0]) > 1),
        .colorspace = f->colorspace,
        .ec = f->ec,
        .golomb = f->ac == AC_GOLOMB_RICE,
        .check_crc = !!(avctx->err_recognition & AV_EF_CRCCHECK),
    };
    for (int i = 0; i < f->quant_table_count; i++)
        pd.extend_lookup[i] = (f->quant_tables[i][3][127] != 0) ||
                              (f->quant_tables[i][4][127] != 0);


    /* For some reason the C FFv1 encoder/decoder treats these differently */
    if (sw_format == AV_PIX_FMT_GBRP10 || sw_format == AV_PIX_FMT_GBRP12 ||
        sw_format == AV_PIX_FMT_GBRP14)
        memcpy(pd.fmt_lut, (int [4]) { 2, 1, 0, 3 }, 4*sizeof(int));
    else if (sw_format == AV_PIX_FMT_X2BGR10)
        memcpy(pd.fmt_lut, (int [4]) { 0, 2, 1, 3 }, 4*sizeof(int));
    else
        ff_vk_set_perm(sw_format, pd.fmt_lut, 0);

    ff_vk_shader_update_push_const(&ctx->s, exec, &fv->setup,
                                   VK_SHADER_STAGE_COMPUTE_BIT,
                                   0, sizeof(pd), &pd);

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
    reset_shader = &fv->reset[f->ac == AC_GOLOMB_RICE];
    ff_vk_shader_update_desc_buffer(&ctx->s, exec, reset_shader,
                                    1, 0, 0,
                                    slice_state,
                                    0, fp->slice_data_size*f->slice_count,
                                    VK_FORMAT_UNDEFINED);

    ff_vk_exec_bind_shader(&ctx->s, exec, reset_shader);

    pd_reset = (FFv1VkResetParameters) {
        .slice_state = slice_state->address + f->slice_count*fp->slice_data_size,
        .plane_state_size = fp->plane_state_size,
        .codec_planes = f->plane_count,
        .key_frame = f->picture.f->flags & AV_FRAME_FLAG_KEY,
        .version = f->version,
        .micro_version = f->micro_version,
    };
    for (int i = 0; i < f->quant_table_count; i++)
        pd_reset.context_count[i] = f->context_count[i];

    ff_vk_shader_update_push_const(&ctx->s, exec, reset_shader,
                                   VK_SHADER_STAGE_COMPUTE_BIT,
                                   0, sizeof(pd_reset), &pd_reset);

    /* Sync between setup and reset shaders */
    buf_bar[nb_buf_bar++] = (VkBufferMemoryBarrier2) {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .srcStageMask = slice_state->stage,
        .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .srcAccessMask = slice_state->access,
        .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = slice_state->buf,
        .offset = 0,
        .size = fp->slice_data_size*f->slice_count,
    };
    vk->CmdPipelineBarrier2(exec->buf, &(VkDependencyInfo) {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pImageMemoryBarriers = img_bar,
        .imageMemoryBarrierCount = nb_img_bar,
        .pBufferMemoryBarriers = buf_bar,
        .bufferMemoryBarrierCount = nb_buf_bar,
    });
    slice_state->stage = buf_bar[0].dstStageMask;
    slice_state->access = buf_bar[0].dstAccessMask;
    nb_buf_bar = 0;
    nb_img_bar = 0;

    vk->CmdDispatch(exec->buf, f->num_h_slices, f->num_v_slices,
                    f->plane_count);

    /* Decode */
    decode_shader = &fv->decode[f->use32bit][f->ac == AC_GOLOMB_RICE][is_rgb];
    ff_vk_shader_update_desc_buffer(&ctx->s, exec, decode_shader,
                                    1, 0, 0,
                                    slice_state,
                                    0, fp->slice_data_size*f->slice_count,
                                    VK_FORMAT_UNDEFINED);
    ff_vk_shader_update_img_array(&ctx->s, exec, decode_shader,
                                  decode_dst, decode_dst_view,
                                  1, 1,
                                  VK_IMAGE_LAYOUT_GENERAL,
                                  VK_NULL_HANDLE);
    ff_vk_shader_update_desc_buffer(&ctx->s, exec, decode_shader,
                                    1, 2, 0,
                                    slice_status,
                                    0, 2*f->slice_count*sizeof(uint32_t),
                                    VK_FORMAT_UNDEFINED);
    if (is_rgb)
        ff_vk_shader_update_img_array(&ctx->s, exec, decode_shader,
                                      f->picture.f, vp->view.out,
                                      1, 3,
                                      VK_IMAGE_LAYOUT_GENERAL,
                                      VK_NULL_HANDLE);

    ff_vk_exec_bind_shader(&ctx->s, exec, decode_shader);
    ff_vk_shader_update_push_const(&ctx->s, exec, decode_shader,
                                   VK_SHADER_STAGE_COMPUTE_BIT,
                                   0, sizeof(pd), &pd);

    /* Sync between reset and decode shaders */
    buf_bar[nb_buf_bar++] = (VkBufferMemoryBarrier2) {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .srcStageMask = slice_state->stage,
        .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .srcAccessMask = slice_state->access,
        .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                         VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = slice_state->buf,
        .offset = fp->slice_data_size*f->slice_count,
        .size = f->slice_count*(fp->slice_state_size - fp->slice_data_size),
    };

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
    slice_state->stage = buf_bar[0].dstStageMask;
    slice_state->access = buf_bar[0].dstAccessMask;
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

static void define_shared_code(FFVulkanShader *shd, int use32bit)
{
    int smp_bits = use32bit ? 32 : 16;

    GLSLC(0, #define DECODE                                              );

    av_bprintf(&shd->src, "#define RGB_LINECACHE %i\n"                   ,RGB_LINECACHE);
    av_bprintf(&shd->src, "#define CONTEXT_SIZE %i\n"                    ,CONTEXT_SIZE);
    av_bprintf(&shd->src, "#define MAX_QUANT_TABLE_MASK 0x%x\n"          ,MAX_QUANT_TABLE_MASK);

    GLSLF(0, #define TYPE int%i_t                                        ,smp_bits);
    GLSLF(0, #define VTYPE2 i%ivec2                                      ,smp_bits);
    GLSLF(0, #define VTYPE3 i%ivec3                                      ,smp_bits);
    GLSLD(ff_source_rangecoder_comp);
    GLSLD(ff_source_ffv1_common_comp);
}

static int init_setup_shader(FFV1Context *f, FFVulkanContext *s,
                             FFVkExecPool *pool, FFVkSPIRVCompiler *spv,
                             FFVulkanShader *shd)
{
    int err;
    FFVulkanDescriptorSetBinding *desc_set;

    uint8_t *spv_data;
    size_t spv_len;
    void *spv_opaque = NULL;

    RET(ff_vk_shader_init(s, shd, "ffv1_dec_setup",
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

    desc_set = (FFVulkanDescriptorSetBinding []) {
        {
            .name        = "rangecoder_static_buf",
            .type        = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .mem_layout  = "scalar",
            .buf_content = "uint8_t zero_one_state[512];",
        },
        {
            .name        = "crc_ieee_buf",
            .type        = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .mem_layout  = "scalar",
            .buf_content = "uint32_t crc_ieee[256];",
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

    RET(ff_vk_shader_add_descriptor_set(s, shd, desc_set, 3, 1, 0));

    define_shared_code(shd, 0 /* Irrelevant */);

    desc_set = (FFVulkanDescriptorSetBinding []) {
        {
            .name        = "slice_data_buf",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .buf_content = "SliceContext slice_ctx",
            .buf_elems   = f->max_slice_count,
        },
        {
            .name        = "slice_offsets_buf",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .mem_quali   = "readonly",
            .buf_content = "uint32_t slice_offsets",
            .buf_elems   = 2*f->max_slice_count,
        },
        {
            .name        = "slice_status_buf",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .mem_quali   = "writeonly",
            .buf_content = "uint32_t slice_status",
            .buf_elems   = 2*f->max_slice_count,
        },
    };
    RET(ff_vk_shader_add_descriptor_set(s, shd, desc_set, 3, 0, 0));

    GLSLD(ff_source_ffv1_dec_setup_comp);

    RET(spv->compile_shader(s, spv, shd, &spv_data, &spv_len, "main",
                            &spv_opaque));
    RET(ff_vk_shader_link(s, shd, spv_data, spv_len, "main"));

    RET(ff_vk_shader_register_exec(s, pool, shd));

fail:
    if (spv_opaque)
        spv->free_shader(spv, &spv_opaque);

    return err;
}

static int init_reset_shader(FFV1Context *f, FFVulkanContext *s,
                             FFVkExecPool *pool, FFVkSPIRVCompiler *spv,
                             FFVulkanShader *shd, int ac)
{
    int err;
    FFVulkanDescriptorSetBinding *desc_set;

    uint8_t *spv_data;
    size_t spv_len;
    void *spv_opaque = NULL;
    int wg_dim = FFMIN(s->props.properties.limits.maxComputeWorkGroupSize[0], 1024);

    RET(ff_vk_shader_init(s, shd, "ffv1_dec_reset",
                          VK_SHADER_STAGE_COMPUTE_BIT,
                          (const char *[]) { "GL_EXT_buffer_reference",
                                             "GL_EXT_buffer_reference2" }, 2,
                          wg_dim, 1, 1,
                          0));

    if (ac == AC_GOLOMB_RICE)
        av_bprintf(&shd->src, "#define GOLOMB\n");

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
    RET(ff_vk_shader_add_descriptor_set(s, shd, desc_set, 2, 1, 0));

    define_shared_code(shd, 0 /* Bit depth irrelevant for the reset shader */);
    if (ac == AC_GOLOMB_RICE)
        GLSLD(ff_source_ffv1_vlc_comp);

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
    RET(ff_vk_shader_add_descriptor_set(s, shd, desc_set, 1, 0, 0));

    GLSLD(ff_source_ffv1_reset_comp);

    RET(spv->compile_shader(s, spv, shd, &spv_data, &spv_len, "main",
                            &spv_opaque));
    RET(ff_vk_shader_link(s, shd, spv_data, spv_len, "main"));

    RET(ff_vk_shader_register_exec(s, pool, shd));

fail:
    if (spv_opaque)
        spv->free_shader(spv, &spv_opaque);

    return err;
}

static int init_decode_shader(FFV1Context *f, FFVulkanContext *s,
                              FFVkExecPool *pool, FFVkSPIRVCompiler *spv,
                              FFVulkanShader *shd,
                              AVHWFramesContext *dec_frames_ctx,
                              AVHWFramesContext *out_frames_ctx,
                              int use32bit, int ac, int rgb)
{
    int err;
    FFVulkanDescriptorSetBinding *desc_set;

    uint8_t *spv_data;
    size_t spv_len;
    void *spv_opaque = NULL;
    int use_cached_reader = ac != AC_GOLOMB_RICE &&
                            s->driver_props.driverID == VK_DRIVER_ID_MESA_RADV;

    RET(ff_vk_shader_init(s, shd, "ffv1_dec",
                          VK_SHADER_STAGE_COMPUTE_BIT,
                          (const char *[]) { "GL_EXT_buffer_reference",
                                             "GL_EXT_buffer_reference2" }, 2,
                          use_cached_reader ? CONTEXT_SIZE : 1, 1, 1,
                          0));

    if (ac == AC_GOLOMB_RICE)
        av_bprintf(&shd->src, "#define GOLOMB\n");

    if (rgb)
        av_bprintf(&shd->src, "#define RGB\n");

    if (use_cached_reader)
        av_bprintf(&shd->src, "#define CACHED_SYMBOL_READER 1\n");

    /* Common codec header */
    GLSLD(ff_source_common_comp);

    add_push_data(shd);

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

    RET(ff_vk_shader_add_descriptor_set(s, shd, desc_set, 2, 1, 0));

    define_shared_code(shd, use32bit);
    if (ac == AC_GOLOMB_RICE)
        GLSLD(ff_source_ffv1_vlc_comp);

    desc_set = (FFVulkanDescriptorSetBinding []) {
        {
            .name        = "slice_data_buf",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .buf_content = "SliceContext slice_ctx",
            .buf_elems   = f->max_slice_count,
        },
        {
            .name       = "dec",
            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .dimensions = 2,
            .mem_layout = ff_vk_shader_rep_fmt(dec_frames_ctx->sw_format,
                                               FF_VK_REP_NATIVE),
            .elems      = av_pix_fmt_count_planes(dec_frames_ctx->sw_format),
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        {
            .name        = "slice_status_buf",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .mem_quali   = "writeonly",
            .buf_content = "uint32_t slice_status",
            .buf_elems   = 2*f->max_slice_count,
        },
        {
            .name       = "dst",
            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .dimensions = 2,
            .mem_layout = ff_vk_shader_rep_fmt(out_frames_ctx->sw_format,
                                               FF_VK_REP_NATIVE),
            .mem_quali  = "writeonly",
            .elems      = av_pix_fmt_count_planes(out_frames_ctx->sw_format),
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };
    RET(ff_vk_shader_add_descriptor_set(s, shd, desc_set, 3 + rgb, 0, 0));

    GLSLD(ff_source_ffv1_dec_comp);

    RET(spv->compile_shader(s, spv, shd, &spv_data, &spv_len, "main",
                            &spv_opaque));
    RET(ff_vk_shader_link(s, shd, spv_data, spv_len, "main"));

    RET(ff_vk_shader_register_exec(s, pool, shd));

fail:
    if (spv_opaque)
        spv->free_shader(spv, &spv_opaque);

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
        av_log(avctx, AV_LOG_ERROR, "Unable to initialize frame pool with format %s: %s\n",
               av_get_pix_fmt_name(sw_format), av_err2str(err));
        av_buffer_unref(dst);
        return err;
    }

    return 0;
}

static void vk_decode_ffv1_uninit(FFVulkanDecodeShared *ctx)
{
    FFv1VulkanDecodeContext *fv = ctx->sd_ctx;

    ff_vk_shader_free(&ctx->s, &fv->setup);

    for (int i = 0; i < 2; i++) /* 16/32 bit */
        av_buffer_unref(&fv->intermediate_frames_ref[i]);

    for (int i = 0; i < 2; i++) /* AC/Golomb */
        ff_vk_shader_free(&ctx->s, &fv->reset[i]);

    for (int i = 0; i < 2; i++) /* 16/32 bit */
        for (int j = 0; j < 2; j++) /* AC/Golomb */
            for (int k = 0; k < 2; k++) /* Normal/RGB */
                ff_vk_shader_free(&ctx->s, &fv->decode[i][j][k]);

    ff_vk_free_buf(&ctx->s, &fv->quant_buf);
    ff_vk_free_buf(&ctx->s, &fv->rangecoder_static_buf);
    ff_vk_free_buf(&ctx->s, &fv->crc_tab_buf);

    av_buffer_pool_uninit(&fv->slice_state_pool);
    av_buffer_pool_uninit(&fv->slice_offset_pool);
    av_buffer_pool_uninit(&fv->slice_status_pool);

    av_freep(&fv);
}

static int vk_decode_ffv1_init(AVCodecContext *avctx)
{
    int err;
    FFV1Context *f = avctx->priv_data;
    FFVulkanDecodeContext *dec = avctx->internal->hwaccel_priv_data;
    FFVulkanDecodeShared *ctx = NULL;
    FFv1VulkanDecodeContext *fv;
    FFVkSPIRVCompiler *spv;

    if (f->version < 3 ||
        (f->version == 4 && f->micro_version > 3))
        return AVERROR(ENOTSUP);

    spv = ff_vk_spirv_init();
    if (!spv) {
        av_log(avctx, AV_LOG_ERROR, "Unable to initialize SPIR-V compiler!\n");
        return AVERROR_EXTERNAL;
    }

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

    /* Intermediate frame pool for RCT */
    for (int i = 0; i < 2; i++) { /* 16/32 bit */
        RET(init_indirect(avctx, &ctx->s, &fv->intermediate_frames_ref[i],
                          i ? AV_PIX_FMT_GBRAP32 : AV_PIX_FMT_GBRAP16));
    }

    /* Setup shader */
    RET(init_setup_shader(f, &ctx->s, &ctx->exec_pool, spv, &fv->setup));

    /* Reset shaders */
    for (int i = 0; i < 2; i++) { /* AC/Golomb */
        RET(init_reset_shader(f, &ctx->s, &ctx->exec_pool,
                              spv, &fv->reset[i], !i ? AC_RANGE_CUSTOM_TAB : 0));
    }

    /* Decode shaders */
    for (int i = 0; i < 2; i++) { /* 16/32 bit */
        for (int j = 0; j < 2; j++) { /* AC/Golomb */
            for (int k = 0; k < 2; k++) { /* Normal/RGB */
                AVHWFramesContext *dec_frames_ctx;
                dec_frames_ctx = k ? (AVHWFramesContext *)fv->intermediate_frames_ref[i]->data :
                                     (AVHWFramesContext *)avctx->hw_frames_ctx->data;
                RET(init_decode_shader(f, &ctx->s, &ctx->exec_pool,
                                       spv, &fv->decode[i][j][k],
                                       dec_frames_ctx,
                                       (AVHWFramesContext *)avctx->hw_frames_ctx->data,
                                       i,
                                       !j ? AC_RANGE_CUSTOM_TAB : AC_GOLOMB_RICE,
                                       k));
            }
        }
    }

    /* Range coder data */
    RET(ff_ffv1_vk_init_state_transition_data(&ctx->s,
                                              &fv->rangecoder_static_buf,
                                              f));

    /* Quantization table data */
    RET(ff_ffv1_vk_init_quant_table_data(&ctx->s,
                                         &fv->quant_buf,
                                         f));

    /* CRC table buffer */
    RET(ff_ffv1_vk_init_crc_table_data(&ctx->s,
                                       &fv->crc_tab_buf,
                                       f));

    /* Update setup global descriptors */
    RET(ff_vk_shader_update_desc_buffer(&ctx->s, &ctx->exec_pool.contexts[0],
                                        &fv->setup, 0, 0, 0,
                                        &fv->rangecoder_static_buf,
                                        0, fv->rangecoder_static_buf.size,
                                        VK_FORMAT_UNDEFINED));
    RET(ff_vk_shader_update_desc_buffer(&ctx->s, &ctx->exec_pool.contexts[0],
                                        &fv->setup, 0, 1, 0,
                                        &fv->crc_tab_buf,
                                        0, fv->crc_tab_buf.size,
                                        VK_FORMAT_UNDEFINED));

    /* Update decode global descriptors */
    for (int i = 0; i < 2; i++) { /* 16/32 bit */
        for (int j = 0; j < 2; j++) { /* AC/Golomb */
            for (int k = 0; k < 2; k++) { /* Normal/RGB */
                RET(ff_vk_shader_update_desc_buffer(&ctx->s, &ctx->exec_pool.contexts[0],
                                                    &fv->decode[i][j][k], 0, 0, 0,
                                                    &fv->rangecoder_static_buf,
                                                    0, fv->rangecoder_static_buf.size,
                                                    VK_FORMAT_UNDEFINED));
                RET(ff_vk_shader_update_desc_buffer(&ctx->s, &ctx->exec_pool.contexts[0],
                                                    &fv->decode[i][j][k], 0, 1, 0,
                                                    &fv->quant_buf,
                                                    0, fv->quant_buf.size,
                                                    VK_FORMAT_UNDEFINED));
            }
        }
    }

fail:
    spv->uninit(&spv);

    return err;
}

static void vk_ffv1_free_frame_priv(AVRefStructOpaque _hwctx, void *data)
{
    AVHWDeviceContext *dev_ctx = _hwctx.nc;
    AVVulkanDeviceContext *hwctx = dev_ctx->hwctx;

    FFv1VulkanDecodePicture *fp = data;
    FFVulkanDecodePicture *vp = &fp->vp;
    FFVkBuffer *slice_status = (FFVkBuffer *)fp->slice_status_buf->data;

    ff_vk_decode_free_frame(dev_ctx, vp);

    /* Invalidate slice/output data if needed */
    if (!(slice_status->flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        VkMappedMemoryRange invalidate_data = {
            .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .memory = slice_status->mem,
            .offset = 0,
            .size = 2*fp->slice_num*sizeof(uint32_t),
        };
        vp->invalidate_memory_ranges(hwctx->act_dev,
                                     1, &invalidate_data);
    }

    for (int i = 0; i < fp->slice_num; i++) {
        uint32_t crc_res = 0;
        if (fp->crc_checked)
            crc_res = AV_RN32(slice_status->mapped_mem + 2*i*sizeof(uint32_t) + 0);
        uint32_t status = AV_RN32(slice_status->mapped_mem + 2*i*sizeof(uint32_t) + 4);
        if (status || crc_res)
            av_log(dev_ctx, AV_LOG_ERROR, "Slice %i status: 0x%x, CRC 0x%x\n",
                   i, status, crc_res);
    }

    av_buffer_unref(&vp->slices_buf);
    av_buffer_unref(&fp->slice_state);
    av_buffer_unref(&fp->slice_offset_buf);
    av_buffer_unref(&fp->slice_status_buf);
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
    .decode_params         = &ff_vk_params_invalidate,
    .flush                 = &ff_vk_decode_flush,
    .uninit                = &ff_vk_decode_uninit,
    .frame_params          = &ff_vk_frame_params,
    .priv_data_size        = sizeof(FFVulkanDecodeContext),
    .caps_internal         = HWACCEL_CAP_ASYNC_SAFE | HWACCEL_CAP_THREAD_SAFE,
};
