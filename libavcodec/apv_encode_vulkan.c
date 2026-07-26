/*
 * Copyright (c) 2026 Lynne <dev@lynne.ee>
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

#include <math.h>
#include <stdlib.h>

#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/vulkan.h"

#include "avcodec.h"
#include "codec_internal.h"
#include "encode.h"
#include "hwconfig.h"
#include "internal.h"

#include "apv.h"
#include "cbs.h"
#include "cbs_apv.h"

extern const unsigned char ff_apv_encode_dct_comp_spv_data[];
extern const unsigned int ff_apv_encode_dct_comp_spv_len;

extern const unsigned char ff_apv_encode_tiles_comp_spv_data[];
extern const unsigned int ff_apv_encode_tiles_comp_spv_len;

extern const unsigned char ff_seg_gather_comp_spv_data[];
extern const unsigned int ff_seg_gather_comp_spv_len;

#define APV_DEFAULT_QMAT 16
#define APV_MAX_NUM_COMP 4

typedef struct DCTPushData {
    int     frame_dim[2];
    int     tile_count[2];
    int     tile_mb_dim[2];
    int     log2_chroma_sub[2];
    int     num_comp;
    int     bit_depth;
    float   qf[APV_MAX_NUM_COMP]; /* per-component fact/(level_scale*2^qp_shift) */
    uint8_t qmat[64];             /* quantisation matrix, raster order */
} DCTPushData;

typedef struct EntropyPushData {
    VkDeviceAddress bytestream;
    int      tile_count[2];
    int      num_comp;
    uint32_t slot_size;
    uint32_t comp_base;        /* component index this dispatch's z=0 maps to */
    uint32_t blocks_per_tile;  /* uniform coeff stride, in blocks */
    int      frame_mb[2];      /* frame size in MBs (luma basis) */
    int      tile_mb_dim[2];   /* full-tile size in MBs */
    uint32_t blocks_per_mb;    /* blocks per MB of this dispatch's components */
} EntropyPushData;

typedef struct CompactPushData {
    VkDeviceAddress sparse;
    VkDeviceAddress compacted;
    uint32_t        slot_size;
} CompactPushData;

typedef struct VulkanEncodeAPVFrameData {
    FFVkBuffer *coeffs_ref;
    FFVkBuffer *bytestream_ref;
    FFVkBuffer *compacted_ref;
    FFVkBuffer *sizes_ref;

    int64_t pts;
    int64_t duration;
    void   *frame_opaque;
    AVBufferRef *frame_opaque_ref;
    int     flags;
} VulkanEncodeAPVFrameData;

typedef struct VulkanEncodeAPVContext {
    const AVClass *class;

    FFVulkanContext s;
    AVVulkanDeviceQueueFamily *qf;
    FFVkExecPool exec_pool;

    FFVulkanShader shd_dct;
    FFVulkanShader shd_entropy[2];   /* [0] luma-sized, [1] chroma-sized */
    FFVulkanShader shd_compact;

    /* Per-frame buffer pools */
    AVRefStructPool *coeffs_pool;
    AVRefStructPool *bytestream_pool;
    AVRefStructPool *gathered_pool;
    AVRefStructPool *compacted_pool;
    AVRefStructPool *sizes_pool;

    /* DCT/quantize push constants -- encoder-constant, built once at init. */
    DCTPushData dct_push;

    /* CBS used to assemble the output packet */
    CodedBitstreamContext *cbc;
    CodedBitstreamFragment au;

    AVFrame *frame;

    /* Async machinery */
    int async_depth;
    int in_flight;
    VulkanEncodeAPVFrameData *exec_ctx_info;

    /* Derived per-encoder state */
    int frame_mb_x, frame_mb_y; /* MBs in the frame (luma basis) */
    int tile_cols, tile_rows;
    int tile_mb_w, tile_mb_h;   /* MBs per tile (luma basis) */
    int tile_count;
    int blocks_per_mb;          /* luma; always 4 */
    int chroma_blocks_per_mb;   /* 4 for 4:4:4, 2 for 4:2:2 */
    int num_comp;
    int bit_depth;
    enum AVPixelFormat sw_format;

    int profile_idc;
    int level_idc;
    int band_idc;
    int chroma_format_idc;

    size_t coeffs_size;        /* total size of coeffs buffer */
    size_t bytestream_size;    /* total size of bytestream buffer */
    size_t slot_size;          /* per-tile-component bytestream slot size */
    size_t sizes_size;         /* total size of sizes buffer */

    /* User options */
    int tile_w_mbs_opt;
    int tile_h_mbs_opt;
    int qp_y;
    int qp_c;
    int qmatrix;                /* APV_QMATRIX_*: quantisation matrix select */

    /* Benchmark knob (env APV_VULKAN_HEADERS_ONLY): the GPU still encodes,
     * but the tiles are never downloaded and packets carry headers only. */
    int headers_only;

    /* Benchmark knob (env APV_VULKAN_SKIP_ENTROPY): skip the entropy
     * dispatch to isolate the DCT pass. Implies headers_only. */
    int skip_entropy;
} VulkanEncodeAPVContext;

/*
 * HEVC default 8x8 intra scaling list (ITU-T H.265, Table 7-6): flat through
 * the low-frequency core, a gentle ramp toward the high-frequency corner.
 * Raster order; the matrix is symmetric, so APV's [y][x]/[x][y] indexing is
 * immaterial. APV and HEVC share the "16 = neutral" convention, so the list
 * transfers without rescaling.
 */
static const uint8_t apv_qmat_hevc_intra[64] = {
    16, 16, 16, 16, 17, 18, 21, 24,
    16, 16, 16, 16, 17, 19, 22, 25,
    16, 16, 17, 18, 20, 22, 25, 29,
    16, 16, 18, 21, 24, 27, 31, 36,
    17, 17, 20, 24, 30, 35, 41, 47,
    18, 19, 22, 27, 35, 44, 54, 65,
    21, 22, 25, 31, 41, 54, 70, 88,
    24, 25, 29, 36, 47, 65, 88, 115,
};

enum {
    APV_QMATRIX_FLAT = 0, /* uniform 16 (the spec default) */
    APV_QMATRIX_HEVC = 1, /* HEVC default intra scaling list */
};

/*
 * The active quantisation-matrix value at raster index i. Both the q_matrix
 * signalled in the frame header and the encoder's pf table are derived from
 * this single accessor, so they cannot disagree -- a mismatch would quantise
 * against a different matrix than the decoder dequantises with.
 */
static int apv_qmatrix_value(int qmatrix, int i)
{
    return qmatrix == APV_QMATRIX_HEVC ? apv_qmat_hevc_intra[i]
                                       : APV_DEFAULT_QMAT;
}

static const uint8_t apv_level_scale[6] = { 40, 45, 51, 57, 64, 71 };

static int chroma_format_from_pix_fmt(enum AVPixelFormat sw_fmt)
{
    switch (sw_fmt) {
    case AV_PIX_FMT_YUV422P10:
    case AV_PIX_FMT_YUV422P12:
        return APV_CHROMA_FORMAT_422;
    case AV_PIX_FMT_YUV444P10:
    case AV_PIX_FMT_YUV444P12:
        return APV_CHROMA_FORMAT_444;
    case AV_PIX_FMT_GRAY10:
    case AV_PIX_FMT_GRAY12:
        return APV_CHROMA_FORMAT_400;
    case AV_PIX_FMT_YUVA444P10:
    case AV_PIX_FMT_YUVA444P12:
        return APV_CHROMA_FORMAT_4444;
    default:
        return -1;
    }
}

static int profile_idc_from_pix_fmt(enum AVPixelFormat sw_fmt)
{
    switch (sw_fmt) {
    case AV_PIX_FMT_GRAY10:       return APV_PROFILE_400_10;
    case AV_PIX_FMT_YUV422P10:    return APV_PROFILE_422_10;
    case AV_PIX_FMT_YUV422P12:    return APV_PROFILE_422_12;
    case AV_PIX_FMT_YUV444P10:    return APV_PROFILE_444_10;
    case AV_PIX_FMT_YUV444P12:    return APV_PROFILE_444_12;
    case AV_PIX_FMT_YUVA444P10:   return APV_PROFILE_4444_10;
    case AV_PIX_FMT_YUVA444P12:   return APV_PROFILE_4444_12;
    default:                      return -1;
    }
}

static int init_dct_shader(AVCodecContext *avctx)
{
    int err;
    VulkanEncodeAPVContext *ev = avctx->priv_data;
    FFVulkanShader *shd = &ev->shd_dct;

    SPEC_LIST_CREATE(sl, 1, sizeof(uint32_t))
    SPEC_LIST_ADD(sl, 16, 32, 4); /* nb_blocks: blocks_per_mb per workgroup */

    ff_vk_shader_load(shd, VK_SHADER_STAGE_COMPUTE_BIT, sl,
                      (uint32_t []) { 8, 4, 1 }, 0);

    ff_vk_shader_add_push_const(shd, 0, sizeof(DCTPushData),
                                VK_SHADER_STAGE_COMPUTE_BIT);

    const FFVulkanDescriptorSetBinding desc_set[] = {
        {
            .name   = "coeffs_buf",
            .type   = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        {
            .name   = "src",
            .type   = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stages = VK_SHADER_STAGE_COMPUTE_BIT,
            .elems  = av_pix_fmt_count_planes(ev->sw_format),
        },
    };
    ff_vk_shader_add_descriptor_set(&ev->s, shd, desc_set, 2, 0);

    RET(ff_vk_shader_link(&ev->s, shd,
                          ff_apv_encode_dct_comp_spv_data,
                          ff_apv_encode_dct_comp_spv_len, "main"));

    RET(ff_vk_shader_register_exec(&ev->s, &ev->exec_pool, shd));

fail:
    return err;
}

static int init_entropy_shader(AVCodecContext *avctx, int blocks_per_mb,
                               FFVulkanShader *shd)
{
    int err;
    VulkanEncodeAPVContext *ev = avctx->priv_data;

    /* One workgroup per tile-component, one invocation per transform block.
     * Luma and chroma tile-components hold different block counts under
     * chroma sub-sampling, so each gets a pipeline with its own size. */
    uint32_t wg = ev->tile_mb_w * ev->tile_mb_h * blocks_per_mb;

    ff_vk_shader_load(shd, VK_SHADER_STAGE_COMPUTE_BIT, NULL,
                      (uint32_t []) { wg, 1, 1 }, 0);

    ff_vk_shader_add_push_const(shd, 0, sizeof(EntropyPushData),
                                VK_SHADER_STAGE_COMPUTE_BIT);

    const FFVulkanDescriptorSetBinding desc_set[] = {
        {
            .name   = "coeffs_buf",
            .type   = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        {
            .name   = "sizes_buf",
            .type   = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };
    ff_vk_shader_add_descriptor_set(&ev->s, shd, desc_set, 2, 0);

    RET(ff_vk_shader_link(&ev->s, shd,
                          ff_apv_encode_tiles_comp_spv_data,
                          ff_apv_encode_tiles_comp_spv_len, "main"));

    RET(ff_vk_shader_register_exec(&ev->s, &ev->exec_pool, shd));

fail:
    return err;
}

static int init_compact_shader(AVCodecContext *avctx)
{
    int err;
    VulkanEncodeAPVContext *ev = avctx->priv_data;
    FFVulkanShader *shd = &ev->shd_compact;

    ff_vk_shader_load(shd, VK_SHADER_STAGE_COMPUTE_BIT, NULL,
                      (uint32_t []) { 256, 1, 1 }, 0);

    ff_vk_shader_add_push_const(shd, 0, sizeof(CompactPushData),
                                VK_SHADER_STAGE_COMPUTE_BIT);

    const FFVulkanDescriptorSetBinding desc_set[] = {
        {
            .name   = "sizes_buf",
            .type   = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };
    ff_vk_shader_add_descriptor_set(&ev->s, shd, desc_set, 1, 0);

    RET(ff_vk_shader_link(&ev->s, shd,
                          ff_seg_gather_comp_spv_data,
                          ff_seg_gather_comp_spv_len, "main"));

    RET(ff_vk_shader_register_exec(&ev->s, &ev->exec_pool, shd));

fail:
    return err;
}

/*
 * The DCT/quantize shader's push constants are entirely encoder-constant:
 * frame geometry, the per-component quant scale qf, and the quantisation
 * matrix. Build them once -- nothing here changes between frames.
 */
static void build_dct_push_const(AVCodecContext *avctx)
{
    VulkanEncodeAPVContext *ev = avctx->priv_data;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(ev->sw_format);
    DCTPushData *pd = &ev->dct_push;
    const double fact = (double)(1 << (ev->bit_depth - 1));

    pd->frame_dim[0]       = avctx->width;
    pd->frame_dim[1]       = avctx->height;
    pd->tile_count[0]      = ev->tile_cols;
    pd->tile_count[1]      = ev->tile_rows;
    pd->tile_mb_dim[0]     = ev->tile_mb_w;
    pd->tile_mb_dim[1]     = ev->tile_mb_h;
    pd->log2_chroma_sub[0] = desc->log2_chroma_w;
    pd->log2_chroma_sub[1] = desc->log2_chroma_h;
    pd->num_comp           = ev->num_comp;
    pd->bit_depth          = ev->bit_depth;

    /*
     * qf[c] = fact / (level_scale * 2^qp_shift). The encoder uses one QP per
     * component, so this never varies by tile. Component 3 is alpha
     * (4:4:4:4): full-resolution, so it takes the luma QP.
     */
    for (int c = 0; c < APV_MAX_NUM_COMP; c++) {
        int qp = (c == 0 || c == 3) ? ev->qp_y : ev->qp_c;
        int level_scale = apv_level_scale[qp % 6];
        int qp_shift = qp / 6;
        pd->qf[c] =
            (float)(fact / ((double)level_scale * (double)(1 << qp_shift)));
    }

    /*
     * The 8-bit quantisation matrix. The shader stages it to shared memory
     * and quantises with 1024 / qmat[i], the reciprocal partner of the
     * decoder's per-coefficient dequant -- the same matrix that gets
     * signalled in the frame header.
     */
    for (int i = 0; i < 64; i++)
        pd->qmat[i] = apv_qmatrix_value(ev->qmatrix, i);
}

static int submit_frame(AVCodecContext *avctx, FFVkExecContext *exec,
                        AVFrame *frame)
{
    int err = 0;
    VulkanEncodeAPVContext *ev = avctx->priv_data;
    FFVulkanFunctions *vk = &ev->s.vkfn;
    VulkanEncodeAPVFrameData *fd = exec->opaque;
    VkImageView views[AV_NUM_DATA_POINTERS];

    VkImageMemoryBarrier2 img_bar[AV_NUM_DATA_POINTERS];
    int nb_img_bar = 0;
    VkBufferMemoryBarrier2 buf_bar[4];
    int nb_buf_bar = 0;

    FFVkBuffer *coeffs_buf;
    FFVkBuffer *bytestream_buf;

    FFVkBuffer *gathered_buf = NULL;
    FFVkBuffer *compacted_buf;
    FFVkBuffer *sizes_buf;

    /* Allocate per-frame buffers */
    RET(ff_vk_get_pooled_buffer(&ev->s, &ev->coeffs_pool, &fd->coeffs_ref,
                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                NULL, ev->coeffs_size,
                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT));
    coeffs_buf = fd->coeffs_ref;

    /* The entropy shader writes the bitstream here, sparsely -- one
     * worst-case-sized slot per tile-component. Device-local, so those GPU
     * writes stay in VRAM and never cross PCIe. */
    RET(ff_vk_get_pooled_buffer(&ev->s, &ev->bytestream_pool,
                                &fd->bytestream_ref,
                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                NULL, ev->bytestream_size,
                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT));
    bytestream_buf = fd->bytestream_ref;

    /* The compaction shader gathers the sparse slots into here, contiguous.
     * Device-local: shader stores over the bus are unreliably slow on some
     * drivers, so the transfer to the host is left to the copy engine. */
    RET(ff_vk_get_pooled_buffer(&ev->s, &ev->gathered_pool,
                                &gathered_buf,
                                VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                NULL, ev->bytestream_size,
                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT));

    /* Copy-engine destination the CPU assembles the packet from.
     * Host-visible + host-cached so the readback is a fast cached copy. */
    RET(ff_vk_get_pooled_buffer(&ev->s, &ev->compacted_pool,
                                &fd->compacted_ref,
                                VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                NULL, ev->bytestream_size,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                VK_MEMORY_PROPERTY_HOST_CACHED_BIT));
    compacted_buf = fd->compacted_ref;

    RET(ff_vk_get_pooled_buffer(&ev->s, &ev->sizes_pool, &fd->sizes_ref,
                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                NULL, ev->sizes_size,
                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));
    sizes_buf = fd->sizes_ref;

    ff_vk_exec_start(&ev->s, exec);

    ff_vk_exec_add_dep_refstruct(&ev->s, exec, fd->coeffs_ref);
    ff_vk_exec_add_dep_refstruct(&ev->s, exec, fd->bytestream_ref);
    ff_vk_exec_add_dep_refstruct(&ev->s, exec, fd->compacted_ref);
    ff_vk_exec_add_dep_refstruct(&ev->s, exec, fd->sizes_ref);

    RET(ff_vk_exec_add_dep_frame(&ev->s, exec, frame,
                                 VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT));

    RET(ff_vk_create_imageviews(&ev->s, exec, views, frame, FF_VK_REP_INT));

    ff_vk_frame_barrier(&ev->s, exec, frame,
                        img_bar, &nb_img_bar,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_SHADER_READ_BIT,
                        VK_IMAGE_LAYOUT_GENERAL,
                        VK_QUEUE_FAMILY_IGNORED);

    vk->CmdPipelineBarrier2(exec->buf, &(VkDependencyInfo) {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pImageMemoryBarriers = img_bar,
        .imageMemoryBarrierCount = nb_img_bar,
    });
    nb_img_bar = 0;

    /* DCT + Quantize pass */
    {
        ff_vk_shader_update_desc_buffer(&ev->s, exec, &ev->shd_dct,
                                        0, 0, 0,
                                        coeffs_buf, 0, coeffs_buf->size,
                                        VK_FORMAT_UNDEFINED);
        ff_vk_shader_update_img_array(&ev->s, exec, &ev->shd_dct,
                                      frame, views,
                                      0, 1,
                                      VK_IMAGE_LAYOUT_GENERAL,
                                      VK_NULL_HANDLE);

        ff_vk_exec_bind_shader(&ev->s, exec, &ev->shd_dct);
        ff_vk_shader_update_push_const(&ev->s, exec, &ev->shd_dct,
                                       VK_SHADER_STAGE_COMPUTE_BIT,
                                       0, sizeof(ev->dct_push), &ev->dct_push);

        vk->CmdDispatch(exec->buf,
                        ev->frame_mb_x, ev->frame_mb_y, ev->num_comp);
    }

    /* Barrier: wait for coeff writes before entropy */
    ff_vk_buf_barrier(buf_bar[nb_buf_bar++], coeffs_buf,
                      COMPUTE_SHADER_BIT, SHADER_WRITE_BIT, NONE,
                      COMPUTE_SHADER_BIT, SHADER_READ_BIT, NONE,
                      0, coeffs_buf->size);

    vk->CmdPipelineBarrier2(exec->buf, &(VkDependencyInfo) {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pBufferMemoryBarriers = buf_bar,
        .bufferMemoryBarrierCount = nb_buf_bar,
    });
    nb_buf_bar = 0;

    /*
     * Entropy encoding pass. Luma (component 0) and chroma (components
     * 1..num_comp-1) run as two dispatches: under chroma sub-sampling their
     * tile-components hold different block counts, hence different workgroup
     * sizes -- one pipeline each. The two write disjoint memory and need no
     * barrier between them, so the GPU is free to overlap them.
     */
    for (int p = 0; !ev->skip_entropy && p < 2; p++) {
        FFVulkanShader *shd = &ev->shd_entropy[p];
        uint32_t z_comps = (p == 0) ? 1 : ev->num_comp - 1;

        if (z_comps == 0)
            continue;   /* 4:0:0 (monochrome) has no chroma components */

        EntropyPushData pd = {
            .bytestream      = bytestream_buf->address,
            .tile_count      = { ev->tile_cols, ev->tile_rows },
            .num_comp        = ev->num_comp,
            .slot_size       = (uint32_t)ev->slot_size,
            .comp_base       = (uint32_t)p,
            .blocks_per_tile = (uint32_t)ev->tile_mb_w * ev->tile_mb_h *
                               ev->blocks_per_mb,
            .frame_mb        = { ev->frame_mb_x, ev->frame_mb_y },
            .tile_mb_dim     = { ev->tile_mb_w, ev->tile_mb_h },
            .blocks_per_mb   = (uint32_t)(p == 0 ? ev->blocks_per_mb
                                                 : ev->chroma_blocks_per_mb),
        };

        ff_vk_shader_update_desc_buffer(&ev->s, exec, shd, 0, 0, 0,
                                        coeffs_buf, 0, coeffs_buf->size,
                                        VK_FORMAT_UNDEFINED);
        ff_vk_shader_update_desc_buffer(&ev->s, exec, shd, 0, 1, 0,
                                        sizes_buf, 0, sizes_buf->size,
                                        VK_FORMAT_UNDEFINED);

        ff_vk_exec_bind_shader(&ev->s, exec, shd);
        ff_vk_shader_update_push_const(&ev->s, exec, shd,
                                       VK_SHADER_STAGE_COMPUTE_BIT,
                                       0, sizeof(pd), &pd);

        vk->CmdDispatch(exec->buf, ev->tile_cols, ev->tile_rows, z_comps);
    }

    /* Compaction pass: gather the sparse per-tile-component slots into one
     * contiguous device-local buffer, then read it back with the copy
     * engine. */
    if (!ev->headers_only) {
        ff_vk_buf_barrier(buf_bar[nb_buf_bar++], bytestream_buf,
                          COMPUTE_SHADER_BIT, SHADER_WRITE_BIT, NONE,
                          COMPUTE_SHADER_BIT, SHADER_READ_BIT, NONE,
                          0, bytestream_buf->size);
        ff_vk_buf_barrier(buf_bar[nb_buf_bar++], sizes_buf,
                          COMPUTE_SHADER_BIT, SHADER_WRITE_BIT, NONE,
                          COMPUTE_SHADER_BIT, SHADER_READ_BIT, NONE,
                          0, sizes_buf->size);
        vk->CmdPipelineBarrier2(exec->buf, &(VkDependencyInfo) {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pBufferMemoryBarriers = buf_bar,
            .bufferMemoryBarrierCount = nb_buf_bar,
        });
        nb_buf_bar = 0;

        CompactPushData pd = {
            .sparse    = bytestream_buf->address,
            .compacted = gathered_buf->address,
            .slot_size = (uint32_t)ev->slot_size,
        };

        ff_vk_shader_update_desc_buffer(&ev->s, exec, &ev->shd_compact,
                                        0, 0, 0,
                                        sizes_buf, 0, sizes_buf->size,
                                        VK_FORMAT_UNDEFINED);
        ff_vk_exec_bind_shader(&ev->s, exec, &ev->shd_compact);
        ff_vk_shader_update_push_const(&ev->s, exec, &ev->shd_compact,
                                       VK_SHADER_STAGE_COMPUTE_BIT,
                                       0, sizeof(pd), &pd);

        vk->CmdDispatch(exec->buf, ev->tile_count * ev->num_comp, 1, 1);

        /* The gathered size is only known once the encode is done, so the
         * whole buffer is copied; the slots are sized to the entropy coder's
         * worst case, which keeps this close to the payload size. */
        ff_vk_buf_barrier(buf_bar[nb_buf_bar++], gathered_buf,
                          COMPUTE_SHADER_BIT, SHADER_WRITE_BIT, NONE,
                          TRANSFER_BIT, TRANSFER_READ_BIT, NONE,
                          0, gathered_buf->size);
        vk->CmdPipelineBarrier2(exec->buf, &(VkDependencyInfo) {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pBufferMemoryBarriers = buf_bar,
            .bufferMemoryBarrierCount = nb_buf_bar,
        });
        nb_buf_bar = 0;

        vk->CmdCopyBuffer(exec->buf, gathered_buf->buf, compacted_buf->buf,
                          1, &(VkBufferCopy) { .size = ev->bytestream_size });
    }

    ff_vk_exec_move_dep_refstruct(&ev->s, exec, &gathered_buf);
    err = ff_vk_exec_submit(&ev->s, exec);
    if (err < 0)
        goto fail;

    return 0;

fail:
    av_refstruct_unref(&gathered_buf);
    ff_vk_exec_discard_deps(&ev->s, exec);
    return err;
}

static int build_packet(AVCodecContext *avctx, FFVkExecContext *exec,
                        AVPacket *pkt)
{
    int err = 0;
    VulkanEncodeAPVContext *ev = avctx->priv_data;
    FFVulkanFunctions *vk = &ev->s.vkfn;
    VulkanEncodeAPVFrameData *fd = exec->opaque;
    FFVkBuffer *compacted_buf = fd->compacted_ref;
    FFVkBuffer *sizes_buf     = fd->sizes_ref;
    APVRawFrame *raw_frame = NULL;

    /* Wait for the GPU encode to finish */
    ff_vk_exec_wait(&ev->s, exec);

    const uint32_t *sizes = NULL;
    static uint8_t headers_only_tile;   /* 1-byte token tile data */

    /* Headers-only benchmark mode never touches the GPU output. */
    if (!ev->headers_only) {
        /* Invalidate mapped memory if needed */
        if (!(compacted_buf->flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            VkMappedMemoryRange r = {
                .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                .memory = compacted_buf->mem,
                .offset = 0,
                .size = VK_WHOLE_SIZE,
            };
            vk->InvalidateMappedMemoryRanges(ev->s.hwctx->act_dev, 1, &r);
        }
        if (!(sizes_buf->flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            VkMappedMemoryRange r = {
                .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                .memory = sizes_buf->mem,
                .offset = 0,
                .size = VK_WHOLE_SIZE,
            };
            vk->InvalidateMappedMemoryRanges(ev->s.hwctx->act_dev, 1, &r);
        }
        sizes = (const uint32_t *)sizes_buf->mapped_mem;
    }

    /* Allocate the cbs frame structure */
    raw_frame = av_mallocz(sizeof(*raw_frame));
    if (!raw_frame)
        return AVERROR(ENOMEM);

    raw_frame->pbu_header.pbu_type = APV_PBU_PRIMARY_FRAME;
    raw_frame->pbu_header.group_id = 1;

    APVRawFrameHeader *fh = &raw_frame->frame_header;
    fh->frame_info.profile_idc = ev->profile_idc;
    fh->frame_info.level_idc = ev->level_idc;
    fh->frame_info.band_idc = ev->band_idc;
    fh->frame_info.frame_width = avctx->width;
    fh->frame_info.frame_height = avctx->height;
    fh->frame_info.chroma_format_idc = ev->chroma_format_idc;
    fh->frame_info.bit_depth_minus8 = ev->bit_depth - 8;
    fh->frame_info.capture_time_distance = 0;

    fh->color_description_present_flag = 0;
    /* Inferred values when the flag is 0, per the spec. */
    fh->color_primaries          = 2;
    fh->transfer_characteristics = 2;
    fh->matrix_coefficients      = 2;
    fh->full_range_flag          = 0;

    /* compute_pf_table() builds the encoder's pf scale from the same matrix;
     * the two must stay in sync. use_q_matrix is only signalled when the
     * matrix is non-uniform (a flat 16 matrix is the inferred default). */
    fh->use_q_matrix = ev->qmatrix != APV_QMATRIX_FLAT;
    for (int c = 0; c < ev->num_comp; c++)
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++)
                fh->quantization_matrix.q_matrix[c][y][x] =
                    apv_qmatrix_value(ev->qmatrix, y * 8 + x);

    fh->tile_info.tile_width_in_mbs = ev->tile_mb_w;
    fh->tile_info.tile_height_in_mbs = ev->tile_mb_h;
    fh->tile_info.tile_size_present_in_fh_flag = 0;

    /* Populate each tile. The compacted buffer holds each tile-component's
     * data back to back, in (tile, component) order -- the same layout the
     * gather shader produced. */
    uint32_t comp_off = 0;
    for (int t = 0; t < ev->tile_count; t++) {
        APVRawTile *tile = &raw_frame->tile[t];
        uint32_t total_tile_data = 0;

        tile->tile_header.tile_header_size =
            4 + ev->num_comp * (4 + 1) + 1;
        tile->tile_header.tile_index = t;

        for (int c = 0; c < ev->num_comp; c++) {
            uint32_t sz;
            if (ev->headers_only) {
                /* No readback: one token byte (CBS requires size >= 1). */
                sz = 1;
                tile->tile_data[c] = &headers_only_tile;
            } else {
                sz = sizes[t * ev->num_comp + c];
                tile->tile_data[c] = compacted_buf->mapped_mem + comp_off;
                comp_off += sz;
            }
            tile->tile_header.tile_data_size[c] = sz;
            tile->tile_header.tile_qp[c] =
                (c == 0 || c == 3) ? ev->qp_y : ev->qp_c;
            total_tile_data += sz;
        }
        tile->tile_header.reserved_zero_8bits = 0;
        tile->tile_dummy_byte_size = 0;
        tile->tile_dummy_byte = NULL;

        raw_frame->tile_size[t] =
            tile->tile_header.tile_header_size + total_tile_data;
    }

    /* Assemble fragment using cbs_apv */
    ff_cbs_fragment_reset(&ev->au);

    err = ff_cbs_insert_unit_content(&ev->au, -1, APV_PBU_PRIMARY_FRAME,
                                     raw_frame, NULL);
    if (err < 0) {
        av_freep(&raw_frame);
        return err;
    }
    /* raw_frame is now owned by the fragment unit */
    raw_frame = NULL;

    /* Assemble straight into the packet: ff_cbs_write_packet() hands pkt a
     * reference to CBS's own assembled buffer -- no copy. */
    err = ff_cbs_write_packet(ev->cbc, pkt, &ev->au);
    if (err < 0)
        return err;

    pkt->pts      = fd->pts;
    pkt->dts      = fd->pts;
    pkt->duration = fd->duration;
    pkt->flags   |= AV_PKT_FLAG_KEY; /* APV is all intra */

    if (avctx->flags & AV_CODEC_FLAG_COPY_OPAQUE) {
        pkt->opaque          = fd->frame_opaque;
        pkt->opaque_ref      = fd->frame_opaque_ref;
        fd->frame_opaque_ref = NULL;
    }

    av_log(avctx, AV_LOG_VERBOSE, "Encoded APV frame: %i bytes (%.2f MiB)\n",
           pkt->size, pkt->size / (1024.0 * 1024.0));

    av_refstruct_unref(&fd->coeffs_ref);
    av_refstruct_unref(&fd->bytestream_ref);
    av_refstruct_unref(&fd->compacted_ref);
    av_refstruct_unref(&fd->sizes_ref);

    return 0;
}

static int vulkan_encode_apv_receive_packet(AVCodecContext *avctx,
                                            AVPacket *pkt)
{
    int err;
    VulkanEncodeAPVContext *ev = avctx->priv_data;
    VulkanEncodeAPVFrameData *fd;
    FFVkExecContext *exec;
    AVFrame *frame;

    while (1) {
        exec = ff_vk_exec_get(&ev->s, &ev->exec_pool);

        if (exec->had_submission) {
            exec->had_submission = 0;
            ev->in_flight--;
            return build_packet(avctx, exec, pkt);
        }

        frame = ev->frame;
        err = ff_encode_get_frame(avctx, frame);
        if (err < 0 && err != AVERROR_EOF)
            return err;
        else if (err == AVERROR_EOF) {
            if (!ev->in_flight)
                return err;
            continue;
        }

        fd = exec->opaque;
        fd->pts = frame->pts;
        fd->duration = frame->duration;
        fd->flags = frame->flags;
        if (avctx->flags & AV_CODEC_FLAG_COPY_OPAQUE) {
            fd->frame_opaque     = frame->opaque;
            fd->frame_opaque_ref = frame->opaque_ref;
            frame->opaque_ref    = NULL;
        }

        err = submit_frame(avctx, exec, frame);
        av_frame_unref(frame);
        if (err < 0)
            return err;

        ev->in_flight++;
        if (ev->in_flight < ev->async_depth)
            return AVERROR(EAGAIN);
    }
    return 0;
}

static av_cold int vulkan_encode_apv_close(AVCodecContext *avctx)
{
    VulkanEncodeAPVContext *ev = avctx->priv_data;

    ff_vk_exec_pool_free(&ev->s, &ev->exec_pool);

    ff_vk_shader_free(&ev->s, &ev->shd_dct);
    ff_vk_shader_free(&ev->s, &ev->shd_entropy[0]);
    ff_vk_shader_free(&ev->s, &ev->shd_entropy[1]);
    ff_vk_shader_free(&ev->s, &ev->shd_compact);

    if (ev->exec_ctx_info) {
        for (int i = 0; i < ev->async_depth; i++) {
            VulkanEncodeAPVFrameData *fd = &ev->exec_ctx_info[i];
            av_refstruct_unref(&fd->coeffs_ref);
            av_refstruct_unref(&fd->bytestream_ref);
            av_refstruct_unref(&fd->compacted_ref);
            av_refstruct_unref(&fd->sizes_ref);
            av_buffer_unref(&fd->frame_opaque_ref);
        }
        av_freep(&ev->exec_ctx_info);
    }

    av_refstruct_pool_uninit(&ev->coeffs_pool);
    av_refstruct_pool_uninit(&ev->bytestream_pool);
    av_refstruct_pool_uninit(&ev->gathered_pool);
    av_refstruct_pool_uninit(&ev->compacted_pool);
    av_refstruct_pool_uninit(&ev->sizes_pool);

    ff_cbs_fragment_free(&ev->au);
    ff_cbs_close(&ev->cbc);

    av_frame_free(&ev->frame);
    ff_vk_uninit(&ev->s);

    return 0;
}

static av_cold int vulkan_encode_apv_init(AVCodecContext *avctx)
{
    int err;
    VulkanEncodeAPVContext *ev = avctx->priv_data;
    AVHWFramesContext *hwfc;

    if (!avctx->hw_frames_ctx) {
        av_log(avctx, AV_LOG_ERROR, "An AVHWFramesContext is required.\n");
        return AVERROR(EINVAL);
    }
    hwfc = (AVHWFramesContext *)avctx->hw_frames_ctx->data;
    ev->sw_format = hwfc->sw_format;

    ev->profile_idc = profile_idc_from_pix_fmt(ev->sw_format);
    ev->chroma_format_idc = chroma_format_from_pix_fmt(ev->sw_format);
    if (ev->profile_idc < 0 || ev->chroma_format_idc < 0) {
        av_log(avctx, AV_LOG_ERROR, "Unsupported sw_format %s for APV.\n",
               av_get_pix_fmt_name(ev->sw_format));
        return AVERROR(EINVAL);
    }

    /* All four APV chroma formats are supported -- 4:0:0, 4:2:2, 4:4:4 and
     * 4:4:4:4. The profile_idc / chroma_format_idc checks above already
     * reject any pixel format that is not one of them. */
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(ev->sw_format);
    ev->bit_depth = desc->comp[0].depth;
    ev->num_comp  = desc->nb_components;
    ev->blocks_per_mb = 4; /* luma: 16x16 MB -> 4 8x8 blocks */
    ev->chroma_blocks_per_mb = 4 >> (desc->log2_chroma_w + desc->log2_chroma_h);
    ev->level_idc = 33; /* placeholder, real value depends on resolution and bitrate */
    ev->band_idc = 0;

    /* Frame dimensions in macroblocks */
    ev->frame_mb_x = (avctx->width  + APV_MB_WIDTH  - 1) / APV_MB_WIDTH;
    ev->frame_mb_y = (avctx->height + APV_MB_HEIGHT - 1) / APV_MB_HEIGHT;

    /* The 20x20 tile grid cap is structural (fixed-size arrays everywhere);
     * the spec additionally demands tiles of at least 16x8 MBs. Each
     * tile-component maps to one entropy workgroup, one invocation per
     * transform block. */
    int grid_tw = (ev->frame_mb_x + APV_MAX_TILE_COLS - 1) / APV_MAX_TILE_COLS;
    int grid_th = (ev->frame_mb_y + APV_MAX_TILE_ROWS - 1) / APV_MAX_TILE_ROWS;
    int min_tw = FFMAX(APV_MIN_TILE_WIDTH_IN_MBS,  grid_tw);
    int min_th = FFMAX(APV_MIN_TILE_HEIGHT_IN_MBS, grid_th);

    /* tile_w/tile_h pick the tile size in MBs; 0 selects the spec minimum.
     * An explicit request below the spec minimum is honoured down to the
     * grid cap -- non-conformant, but more tiles mean shorter (serial)
     * entropy streams, which is the decode speed lever. */
    ev->tile_mb_w = ev->tile_w_mbs_opt > 0 ? ev->tile_w_mbs_opt : min_tw;
    ev->tile_mb_h = ev->tile_h_mbs_opt > 0 ? ev->tile_h_mbs_opt : min_th;
    ev->tile_mb_w = FFMIN(FFMAX(ev->tile_mb_w, grid_tw), ev->frame_mb_x);
    ev->tile_mb_h = FFMIN(FFMAX(ev->tile_mb_h, grid_th), ev->frame_mb_y);
    if (ev->tile_mb_w < APV_MIN_TILE_WIDTH_IN_MBS ||
        ev->tile_mb_h < APV_MIN_TILE_HEIGHT_IN_MBS)
        av_log(avctx, AV_LOG_WARNING,
               "Tile size %dx%d MBs is below the spec minimum of %dx%d: "
               "NON-CONFORMANT bitstream, most decoders will reject it.\n",
               ev->tile_mb_w, ev->tile_mb_h,
               APV_MIN_TILE_WIDTH_IN_MBS, APV_MIN_TILE_HEIGHT_IN_MBS);

    /* Left to default, grow the tile toward 1024 transform blocks (the
     * entropy workgroup ceiling) while it still divides the frame. Bigger
     * tiles mean fewer tile-components, which the compaction pass strongly
     * prefers -- it is the dominant win for throughput. */
    if (!ev->tile_w_mbs_opt && !ev->tile_h_mbs_opt) {
        while (ev->tile_mb_w * 2 <= ev->frame_mb_x &&
               ev->frame_mb_x % (ev->tile_mb_w * 2) == 0 &&
               (ev->tile_mb_w * 2) * ev->tile_mb_h * ev->blocks_per_mb <= 1024)
            ev->tile_mb_w *= 2;
        while (ev->tile_mb_h * 2 <= ev->frame_mb_y &&
               ev->frame_mb_y % (ev->tile_mb_h * 2) == 0 &&
               ev->tile_mb_w * (ev->tile_mb_h * 2) * ev->blocks_per_mb <= 1024)
            ev->tile_mb_h *= 2;
    }

    /* Ceil division: the rightmost column / bottom row of tiles take the
     * remainder MBs (spec-legal; the tile grid is closed at the frame edge,
     * so those tiles may be smaller than the signalled tile size). */
    ev->tile_cols = (ev->frame_mb_x + ev->tile_mb_w - 1) / ev->tile_mb_w;
    ev->tile_rows = (ev->frame_mb_y + ev->tile_mb_h - 1) / ev->tile_mb_h;
    ev->tile_count = ev->tile_cols * ev->tile_rows;

    if (ev->tile_count > APV_MAX_TILE_COUNT) {
        av_log(avctx, AV_LOG_ERROR, "Too many tiles (%d).\n", ev->tile_count);
        return AVERROR(EINVAL);
    }

    /* The entropy shader runs one invocation per block in a tile-component
     * and its shared buffers are sized for 1024. */
    if (ev->tile_mb_w * ev->tile_mb_h * ev->blocks_per_mb > 1024) {
        av_log(avctx, AV_LOG_ERROR,
               "Tile-component has too many transform blocks (%d > 1024).\n",
               ev->tile_mb_w * ev->tile_mb_h * ev->blocks_per_mb);
        return AVERROR_PATCHWELCOME;
    }

    /* qp_chroma left at 0 means "use the luma QP". */
    if (ev->qp_c == 0)
        ev->qp_c = ev->qp_y;

    /* Validate QP range */
    int max_qp = 3 + ev->bit_depth * 6;
    if (ev->qp_y < 0 || ev->qp_y > max_qp || ev->qp_c < 0 || ev->qp_c > max_qp) {
        av_log(avctx, AV_LOG_ERROR,
               "QP out of range [0, %d]: qp_y=%d, qp_c=%d.\n",
               max_qp, ev->qp_y, ev->qp_c);
        return AVERROR(EINVAL);
    }

    /* Buffer sizing */
    size_t blocks_per_tile = (size_t)ev->tile_mb_w * ev->tile_mb_h * ev->blocks_per_mb;
    ev->coeffs_size = (size_t)ev->tile_count * ev->num_comp *
                      blocks_per_tile * APV_BLK_COEFFS * sizeof(int16_t);

    /* Worst-case per-tile-component bytestream: each coefficient at most ~32 bits.
     * Round up generously. */
    ev->slot_size = blocks_per_tile * APV_BLK_COEFFS * 8;
    ev->slot_size = FFALIGN(ev->slot_size, 64);
    ev->bytestream_size = (size_t)ev->tile_count * ev->num_comp * ev->slot_size;
    ev->sizes_size = (size_t)ev->tile_count * ev->num_comp * sizeof(uint32_t);

    av_log(avctx, AV_LOG_VERBOSE,
           "APV Vulkan encoder: %dx%d, %d tiles (%dx%d MBs each), "
           "qp_y=%d qp_c=%d, coeffs=%zu KiB, bytestream=%zu KiB\n",
           avctx->width, avctx->height, ev->tile_count,
           ev->tile_mb_w, ev->tile_mb_h, ev->qp_y, ev->qp_c,
           ev->coeffs_size / 1024, ev->bytestream_size / 1024);

    ev->headers_only = !!getenv("APV_VULKAN_HEADERS_ONLY");
    ev->skip_entropy = !!getenv("APV_VULKAN_SKIP_ENTROPY");
    if (ev->skip_entropy)
        ev->headers_only = 1;   /* the bitstream is never produced */
    if (ev->headers_only)
        av_log(avctx, AV_LOG_WARNING,
               "APV_VULKAN_HEADERS_ONLY set: tiles will not be downloaded "
               "or assembled; output packets contain headers only.\n");
    if (ev->skip_entropy)
        av_log(avctx, AV_LOG_WARNING,
               "APV_VULKAN_SKIP_ENTROPY set: entropy dispatch skipped "
               "(DCT-only benchmark mode).\n");

    /* Init Vulkan */
    err = ff_vk_init(&ev->s, avctx, NULL, avctx->hw_frames_ctx);
    if (err < 0)
        return err;

    ev->qf = ff_vk_qf_find(&ev->s, VK_QUEUE_COMPUTE_BIT, 0);
    if (!ev->qf) {
        av_log(avctx, AV_LOG_ERROR, "Device has no compute queues!\n");
        return AVERROR(ENOTSUP);
    }

    err = ff_vk_exec_pool_init(&ev->s, ev->qf, &ev->exec_pool,
                               ev->async_depth, 0, 0, 0, NULL);
    if (err < 0)
        return err;

    /* Init CBS for assembling output */
    err = ff_cbs_init(&ev->cbc, AV_CODEC_ID_APV, avctx);
    if (err < 0)
        return err;

    /* Shaders */
    err = init_dct_shader(avctx);
    if (err < 0)
        return err;
    err = init_entropy_shader(avctx, ev->blocks_per_mb, &ev->shd_entropy[0]);
    if (err < 0)
        return err;
    err = init_entropy_shader(avctx, ev->chroma_blocks_per_mb,
                              &ev->shd_entropy[1]);
    if (err < 0)
        return err;
    err = init_compact_shader(avctx);
    if (err < 0)
        return err;

    /* The DCT/quantize shader's push constants never change frame to frame;
     * build them once. */
    build_dct_push_const(avctx);

    ev->frame = av_frame_alloc();
    if (!ev->frame)
        return AVERROR(ENOMEM);

    /* Async data pool */
    ev->async_depth = ev->exec_pool.pool_size;
    ev->exec_ctx_info = av_calloc(ev->async_depth, sizeof(*ev->exec_ctx_info));
    if (!ev->exec_ctx_info)
        return AVERROR(ENOMEM);
    for (int i = 0; i < ev->async_depth; i++)
        ev->exec_pool.contexts[i].opaque = &ev->exec_ctx_info[i];

    return 0;
}

#define OFFSET(x) offsetof(VulkanEncodeAPVContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption vulkan_encode_apv_options[] = {
    { "qp",          "Quantization parameter (luma)", OFFSET(qp_y),
        AV_OPT_TYPE_INT, { .i64 = 22 }, 0, 255, VE },
    { "qp_chroma",   "Chroma quantization parameter (0 = same as luma qp)", OFFSET(qp_c),
        AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 255, VE },
    { "qmatrix",     "Quantization matrix", OFFSET(qmatrix),
        AV_OPT_TYPE_INT, { .i64 = APV_QMATRIX_HEVC }, 0, 1, VE, "qmatrix" },
        { "flat",    "Uniform matrix, all 16 (APV spec default)", 0,
            AV_OPT_TYPE_CONST, { .i64 = APV_QMATRIX_FLAT }, 0, 0, VE, "qmatrix" },
        { "hevc",    "HEVC default intra scaling list (mild perceptual shaping)", 0,
            AV_OPT_TYPE_CONST, { .i64 = APV_QMATRIX_HEVC }, 0, 0, VE, "qmatrix" },
    /* The minimum legal tile is 16x8 MBs; the maxima are this encoder's
     * ceiling of 1024 transform blocks per tile-component (256 MBs): with
     * the other dimension at its minimum, width <= 32 and height <= 16. A
     * value of 0 is the sentinel for the adaptive per-frame default. */
    { "tile_width",  "Tile width in macroblocks (0 = adaptive, auto-sized per frame)", OFFSET(tile_w_mbs_opt),
        AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 32, VE },
    { "tile_height", "Tile height in macroblocks (0 = adaptive, auto-sized per frame)", OFFSET(tile_h_mbs_opt),
        AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 16, VE },
    { "async_depth", "Internal parallelization depth", OFFSET(async_depth),
        AV_OPT_TYPE_INT, { .i64 = 1 }, 1, INT_MAX, VE },
    { NULL }
};

static const FFCodecDefault vulkan_encode_apv_defaults[] = {
    { "g", "1" },
    { NULL },
};

static const AVClass vulkan_encode_apv_class = {
    .class_name = "apv_vulkan",
    .item_name  = av_default_item_name,
    .option     = vulkan_encode_apv_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVCodecHWConfigInternal *const vulkan_encode_apv_hw_configs[] = {
    HW_CONFIG_ENCODER_FRAMES(VULKAN, VULKAN),
    NULL,
};

const FFCodec ff_apv_vulkan_encoder = {
    .p.name         = "apv_vulkan",
    CODEC_LONG_NAME("Advanced Professional Video (Vulkan)"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_APV,
    .priv_data_size = sizeof(VulkanEncodeAPVContext),
    .init           = &vulkan_encode_apv_init,
    FF_CODEC_RECEIVE_PACKET_CB(&vulkan_encode_apv_receive_packet),
    .close          = &vulkan_encode_apv_close,
    .p.priv_class   = &vulkan_encode_apv_class,
    .p.capabilities = AV_CODEC_CAP_DELAY |
                      AV_CODEC_CAP_HARDWARE |
                      AV_CODEC_CAP_DR1 |
                      AV_CODEC_CAP_ENCODER_FLUSH |
                      AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP | FF_CODEC_CAP_EOF_FLUSH,
    .defaults       = vulkan_encode_apv_defaults,
    CODEC_PIXFMTS(AV_PIX_FMT_VULKAN),
    .hw_configs     = vulkan_encode_apv_hw_configs,
    .p.wrapper_name = "vulkan",
};
