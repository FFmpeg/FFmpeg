/**
 * Copyright (C) 2026 Lynne
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
#include "libavutil/refstruct.h"

#include "../graph.h"
#include "../ops_internal.h"
#include "../swscale_internal.h"

#include "ops.h"

#include "spvasm.h"

static void vk_ops_ctx_uninit(AVRefStructOpaque opaque, void *obj)
{
    FFVulkanOpsCtx *s = obj;

    ff_vk_uninit(&s->vkctx);
}

int ff_sws_vk_init(SwsContext *sws, AVBufferRef *dev_ref)
{
    int err;
    SwsInternal *c = sws_internal(sws);

    if (!c->hw_priv) {
        c->hw_priv = av_refstruct_alloc_ext(sizeof(FFVulkanOpsCtx), 0, NULL,
                                            vk_ops_ctx_uninit);
        if (!c->hw_priv)
            return AVERROR(ENOMEM);
    }

    FFVulkanOpsCtx *s = c->hw_priv;
    if (s->vkctx.device_ref) {
        if (s->vkctx.device_ref->data == dev_ref->data)
            return 0;
        /* Reinitialize with new context */
        ff_vk_uninit(&s->vkctx);
    }

    err = ff_vk_init(&s->vkctx, sws, dev_ref, NULL);
    if (err < 0)
        return err;

    s->qf = ff_vk_qf_find(&s->vkctx, VK_QUEUE_COMPUTE_BIT, 0);
    if (!s->qf) {
        av_log(sws, AV_LOG_ERROR, "Device has no compute queues\n");
        return AVERROR(ENOTSUP);
    }

    return 0;
}

AVBufferRef *ff_sws_vk_device_ref(SwsContext *sws)
{
    SwsInternal *c = sws_internal(sws);
    FFVulkanOpsCtx *s = c->hw_priv;
    return s ? s->vkctx.device_ref : NULL;
}

#define MAX_OPS 32
#define MAX_DITHER_BUFS 4
#define MAX_FILT_BUFS 4
#define MAX_DATA_BUFS (MAX_DITHER_BUFS + MAX_FILT_BUFS*4)

typedef struct VulkanPriv {
    FFVulkanOpsCtx *s;
    FFVkExecPool e;
    FFVulkanShader shd;
    FFVkBuffer data_bufs[MAX_DATA_BUFS];
    int nb_data_bufs;
    VkFormat src_view_fmt[AV_NUM_DATA_POINTERS];
    VkFormat dst_view_fmt[AV_NUM_DATA_POINTERS];
    int interlaced;
} VulkanPriv;

static void spirv_process(const SwsFrame *dst, const SwsFrame *src,
                          int y, int h, const SwsPass *pass)
{
    VulkanPriv *p = (VulkanPriv *) pass->priv;
    FFVkExecContext *ec = ff_vk_exec_get(&p->s->vkctx, &p->e);
    FFVulkanFunctions *vk = &p->s->vkctx.vkfn;
    int err = ff_vk_exec_start(&p->s->vkctx, ec);
    if (err < 0)
        goto error;

    AVFrame *src_f = (AVFrame *) src->avframe;
    AVFrame *dst_f = (AVFrame *) dst->avframe;
    err = ff_vk_exec_add_dep_frame(&p->s->vkctx, ec, src_f,
                                   VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                   VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    if (err < 0)
        goto fail;
    err = ff_vk_exec_add_dep_frame(&p->s->vkctx, ec, dst_f,
                                   VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                   VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    if (err < 0)
        goto fail;

    VkImageView src_views[AV_NUM_DATA_POINTERS];
    VkImageView dst_views[AV_NUM_DATA_POINTERS];
    err = ff_vk_create_imageviews_fmt(&p->s->vkctx, ec, src_views, src_f,
                                      p->src_view_fmt);
    if (err < 0)
        goto fail;
    err = ff_vk_create_imageviews_fmt(&p->s->vkctx, ec, dst_views, dst_f,
                                      p->dst_view_fmt);
    if (err < 0)
        goto fail;

    ff_vk_shader_update_img_array(&p->s->vkctx, ec, &p->shd, src_f, src_views,
                                  0, 0, VK_IMAGE_LAYOUT_GENERAL, VK_NULL_HANDLE);
    ff_vk_shader_update_img_array(&p->s->vkctx, ec, &p->shd, dst_f, dst_views,
                                  0, 1, VK_IMAGE_LAYOUT_GENERAL, VK_NULL_HANDLE);

    int nb_img_bar = 0;
    VkImageMemoryBarrier2 img_bar[8];
    ff_vk_frame_barrier(&p->s->vkctx, ec, src_f, img_bar, &nb_img_bar,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_SHADER_READ_BIT,
                        VK_IMAGE_LAYOUT_GENERAL,
                        VK_QUEUE_FAMILY_IGNORED);
    ff_vk_frame_barrier(&p->s->vkctx, ec, dst_f, img_bar, &nb_img_bar,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_SHADER_WRITE_BIT,
                        VK_IMAGE_LAYOUT_GENERAL,
                        VK_QUEUE_FAMILY_IGNORED);
    vk->CmdPipelineBarrier2(ec->buf, &(VkDependencyInfo) {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pImageMemoryBarriers = img_bar,
        .imageMemoryBarrierCount = nb_img_bar,
    });

    if (p->interlaced) {
        uint32_t field = pass->graph ? pass->graph->dst.field : 0;
        ff_vk_shader_update_push_const(&p->s->vkctx, ec, &p->shd,
                                       VK_SHADER_STAGE_COMPUTE_BIT,
                                       0, sizeof(field), &field);
    }

    ff_vk_exec_bind_shader(&p->s->vkctx, ec, &p->shd);

    vk->CmdDispatch(ec->buf,
                    FFALIGN(dst->width, p->shd.lg_size[0])/p->shd.lg_size[0],
                    FFALIGN(dst->height, p->shd.lg_size[1])/p->shd.lg_size[1],
                    1);

    err = ff_vk_exec_submit(&p->s->vkctx, ec);
    if (err < 0)
        goto error;
    ff_vk_exec_wait(&p->s->vkctx, ec);
    return;

fail:
    ff_vk_exec_discard(&p->s->vkctx, ec);
error:
    /* SwsPassFunc has no way to return errors */
    av_log(&p->s->vkctx, AV_LOG_ERROR, "Vulkan pass failed: %s\n",
           av_err2str(err));
}

static void free_fn(void *priv)
{
    VulkanPriv *p = priv;
    ff_vk_exec_pool_free(&p->s->vkctx, &p->e);
    ff_vk_shader_free(&p->s->vkctx, &p->shd);
    for (int i = 0; i < p->nb_data_bufs; i++)
        ff_vk_free_buf(&p->s->vkctx, &p->data_bufs[i]);
    av_refstruct_unref(&p->s);
    av_free(priv);
}

static int create_filter_buf(FFVulkanOpsCtx *s, const SwsFilterWeights *wd,
                             FFVkBuffer *buf)
{
    int err;

    /* Weights */
    err = ff_vk_create_buf(&s->vkctx, buf,
                           wd->num_weights*sizeof(float) +
                           wd->dst_size*sizeof(int32_t), NULL, NULL,
                           VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    if (err < 0)
        goto fail;

    float *weights_data;
    err = ff_vk_map_buffer(&s->vkctx, buf,
                           (uint8_t **)&weights_data, 0);
    if (err < 0)
        goto fail;
    for (int i = 0; i < wd->num_weights; i++)
        weights_data[i] = (float) wd->weights[i] / SWS_FILTER_SCALE;

    memcpy(weights_data + wd->num_weights,
           wd->offsets, wd->dst_size*sizeof(int32_t));

    ff_vk_unmap_buffer(&s->vkctx, buf, 1);

    return 0;

fail:
    ff_vk_free_buf(&s->vkctx, buf);
    return err;
}

static int create_dither_buf(FFVulkanOpsCtx *s, const SwsDitherOp *dd,
                             FFVkBuffer *buf)
{
    int err;

    int size = (1 << dd->size_log2);
    err = ff_vk_create_buf(&s->vkctx, buf,
                           size*size*sizeof(float), NULL, NULL,
                           VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    if (err < 0)
        return err;

    float *dither_data;
    err = ff_vk_map_buffer(&s->vkctx, buf, (uint8_t **)&dither_data, 0);
    if (err < 0)
        goto fail;

    for (int i = 0; i < size; i++) {
        for (int j = 0; j < size; j++) {
            const AVRational64 r = dd->matrix[i*size + j];
            dither_data[i*size + j] = r.num/(float)r.den;
        }
    }

    ff_vk_unmap_buffer(&s->vkctx, buf, 1);

    return 0;

fail:
    ff_vk_free_buf(&s->vkctx, buf);
    return err;
}

static int create_bufs(FFVulkanOpsCtx *s, VulkanPriv *p, const SwsOpList *ops)
{
    int err;
    p->nb_data_bufs = 0;
    for (int n = 0; n < ops->num_ops; n++) {
        const SwsOp *op = &ops->ops[n];
        if (op->op == SWS_OP_DITHER) {
            av_assert0(p->nb_data_bufs + 1 <= FF_ARRAY_ELEMS(p->data_bufs));
            err = create_dither_buf(s, &op->dither,
                                    &p->data_bufs[p->nb_data_bufs]);
            if (err < 0)
                goto fail;
            p->nb_data_bufs++;
        } else if (op->op == SWS_OP_READ && op->rw.filter.op) {
            av_assert0(p->nb_data_bufs + 1 <= FF_ARRAY_ELEMS(p->data_bufs));
            err = create_filter_buf(s, op->rw.filter.kernel,
                                    &p->data_bufs[p->nb_data_bufs]);
            if (err < 0)
                goto fail;
            p->nb_data_bufs++;
        }
    }

    return 0;

fail:
    for (int i = 0; i < p->nb_data_bufs; i++)
        ff_vk_free_buf(&p->s->vkctx, &p->data_bufs[i]);
    return err;
}

struct DitherData {
    int size;
    int arr_1d_id;
    int arr_2d_id;
    int struct_id;
    int struct_ptr_id;
    int id;
    int mask_id;
    int binding;
};

struct FilterData {
    SwsOpType filter;
    int filter_size;
    int dst_size;
    int num_weights;

    int arr_w_in_id;
    int arr_w_out_id;
    int arr_o_id;
    int struct_id;
    int struct_ptr_id;

    int id; /* buffer ID */
    int binding; /* descriptor idx in desc set 1 */

    int tap_const_base;
};

typedef struct LinearIDs {
    int m[4][4];  /* matrix coefficients */
    int mat;      /* full matrix, only valid without SWS_BITEXACT */
    int off[4];   /* offset coefficients */
    int off_vec;
    int deco_off; /* space reserved for NoContraction decorations */
    int nb_deco;
} LinearIDs;

/* Constants and buffers used by a single op */
typedef struct OpIDs {
    union {
        int vec;                          /* LSHIFT, RSHIFT, SCALE */
        int comp[4];                      /* CLEAR, MIN, MAX, DITHER */
        struct { int shift, mask; } pack; /* PACK, UNPACK */
        LinearIDs lin;                    /* LINEAR */
    };
    int buf; /* index into dither[] or filt[] */
} OpIDs;

typedef struct SPIRVIDs {
    /* Entrypoint interface variables */
    int iface[3 + MAX_DATA_BUFS + 1];
    int nb_iface;

    int gid_var;
    int in_img_var;
    int out_img_var;

    int glfn;
    int ep;

    /* Types */
    int void_type;
    int b_type;
    int u32_type;
    int i32_type;
    int f32_type;
    int void_fn_type;

    /* Define vector types */
    int bvec2_type;
    int u32vec2_type;
    int i32vec2_type;

    int u32vec3_type;

    int u32vec4_type;
    int f32vec4_type;
    int f32mat4_type;

    /* Constants */
    int u32_p;
    int f32_0;
    int u32_cid[5];

    OpIDs op[MAX_OPS];

    struct DitherData dither[MAX_DITHER_BUFS];
    int dither_ptr_elem_id;
    int nb_dither_bufs;

    struct FilterData filt[MAX_FILT_BUFS];
    int filt_o_ptr_id;
    int nb_filter_bufs;

    int out_img_type;
    int out_img_array_id;

    int in_img_type;
    int in_img_array_id;

    /* Pointer types for images */
    int u32vec3_tptr;
    int out_img_tptr;
    int out_img_sptr;

    int in_img_tptr;
    int in_img_sptr;

    /* Interlaced handling */
    int interlaced;
    int push_const_struct_id;
    int push_const_ptr_id;
    int push_const_elem_ptr_id;
    int push_const_var_id;
    int field_i32;
} SPIRVIDs;

/* Number of arithmetic instructions emitted by insert_bitexact_linear() */
static int bitexact_linear_nb_ops(const SwsLinearOp *lin)
{
    int nb_ops = 0;
    for (int j = 0; j < 4; j++) {
        nb_ops += !!lin->m[j][0].num;
        nb_ops += lin->m[j][0].num && lin->m[j][4].num;
        for (int i = 1; i < 4; i++) {
            nb_ops += !!lin->m[j][i].num;
            nb_ops += lin->m[j][i].num &&
                      (lin->m[j][0].num || lin->m[j][4].num);
        }
    }
    return nb_ops;
}

/* Section 1: Function to define all shader header data, and decorations */
static void define_shader_header(SwsContext *sws, FFVulkanShader *shd,
                                 const SwsOpList *ops, SPICtx *spi, SPIRVIDs *id)
{
    spi_OpCapability(spi, SpvCapabilityShader); /* Shader type */

    /* Declare required capabilities */
    spi_OpCapability(spi, SpvCapabilityImageQuery);
    spi_OpCapability(spi, SpvCapabilityStorageImageReadWithoutFormat);
    spi_OpCapability(spi, SpvCapabilityStorageImageWriteWithoutFormat);
    /* Import the GLSL set of functions (used for min/max) */
    id->glfn = spi_OpExtInstImport(spi, "GLSL.std.450");

    /* Next section starts here */
    spi_OpMemoryModel(spi, SpvAddressingModelLogical, SpvMemoryModelGLSL450);

    /* Entrypoint */
    id->ep = spi_OpEntryPoint(spi, SpvExecutionModelGLCompute, "main",
                              id->iface, id->nb_iface);
    spi_OpExecutionMode(spi, id->ep, SpvExecutionModeLocalSize,
                        shd->lg_size, 3);

    if (id->interlaced) {
        spi_OpDecorate(spi, id->push_const_struct_id, SpvDecorationBlock);
        spi_OpMemberDecorate(spi, id->push_const_struct_id, 0,
                             SpvDecorationOffset, 0);
    }

    /* gl_GlobalInvocationID descriptor decorations */
    spi_OpDecorate(spi, id->gid_var, SpvDecorationBuiltIn,
                   SpvBuiltInGlobalInvocationId);

    /* Input image descriptor decorations */
    if (id->in_img_var) {
        spi_OpDecorate(spi, id->in_img_var, SpvDecorationNonWritable);
        spi_OpDecorate(spi, id->in_img_var, SpvDecorationDescriptorSet, 0);
        spi_OpDecorate(spi, id->in_img_var, SpvDecorationBinding, 0);
    }

    /* Output image descriptor decorations */
    spi_OpDecorate(spi, id->out_img_var, SpvDecorationNonReadable);
    spi_OpDecorate(spi, id->out_img_var, SpvDecorationDescriptorSet, 0);
    spi_OpDecorate(spi, id->out_img_var, SpvDecorationBinding, 1);

    for (int i = 0; i < id->nb_dither_bufs; i++) {
        spi_OpDecorate(spi, id->dither[i].arr_1d_id, SpvDecorationArrayStride,
                       sizeof(float));
        spi_OpDecorate(spi, id->dither[i].arr_2d_id, SpvDecorationArrayStride,
                       id->dither[i].size*sizeof(float));
        spi_OpDecorate(spi, id->dither[i].struct_id, SpvDecorationBlock);
        spi_OpMemberDecorate(spi, id->dither[i].struct_id, 0, SpvDecorationOffset, 0);
        spi_OpDecorate(spi, id->dither[i].id, SpvDecorationDescriptorSet, 1);
        spi_OpDecorate(spi, id->dither[i].id, SpvDecorationBinding,
                       id->dither[i].binding);
    }

    for (int i = 0; i < id->nb_filter_bufs; i++) {
        struct FilterData *f = &id->filt[i];
        spi_OpDecorate(spi, f->arr_w_in_id, SpvDecorationArrayStride,
                       sizeof(float));
        spi_OpDecorate(spi, f->arr_w_out_id, SpvDecorationArrayStride,
                       f->filter_size*sizeof(float));
        spi_OpDecorate(spi, f->arr_o_id, SpvDecorationArrayStride,
                       sizeof(int32_t));
        spi_OpDecorate(spi, f->struct_id, SpvDecorationBlock);
        spi_OpMemberDecorate(spi, f->struct_id, 0, SpvDecorationOffset, 0);
        spi_OpMemberDecorate(spi, f->struct_id, 1, SpvDecorationOffset,
                             f->num_weights*sizeof(float));
        spi_OpDecorate(spi, f->id, SpvDecorationDescriptorSet, 1);
        spi_OpDecorate(spi, f->id, SpvDecorationBinding, f->binding);
    }

    if (!(sws->flags & SWS_BITEXACT))
        return;

    /* All linear arithmetic ops must be decorated with NoContraction */
    for (int n = 0; n < ops->num_ops; n++) {
        const SwsOp *op = &ops->ops[n];
        if (op->op != SWS_OP_LINEAR)
            continue;

        const int nb_ops = bitexact_linear_nb_ops(&op->lin);
        LinearIDs *lin = &id->op[n].lin;
        lin->deco_off = spi_reserve(spi, nb_ops*4*3);
        lin->nb_deco  = nb_ops;
    }
}

/* Section 2: Define all types and constants */
static void define_shader_consts(SwsContext *sws, const SwsOpList *ops,
                                 SPICtx *spi, SPIRVIDs *id)
{
    /* Define scalar types */
    id->void_type    = spi_OpTypeVoid(spi);
    id->b_type       = spi_OpTypeBool(spi);
    int u32_type =
        id->u32_type = spi_OpTypeInt(spi, 32, 0);
    id->i32_type     = spi_OpTypeInt(spi, 32, 1);
    int f32_type =
        id->f32_type = spi_OpTypeFloat(spi, 32);
    id->void_fn_type = spi_OpTypeFunction(spi, id->void_type, NULL, 0);

    /* Define vector types */
    id->bvec2_type   = spi_OpTypeVector(spi, id->b_type,   2);
    id->u32vec2_type = spi_OpTypeVector(spi, u32_type, 2);
    id->i32vec2_type = spi_OpTypeVector(spi, id->i32_type, 2);

    id->u32vec3_type = spi_OpTypeVector(spi, u32_type, 3);

    id->u32vec4_type = spi_OpTypeVector(spi, u32_type, 4);
    id->f32vec4_type = spi_OpTypeVector(spi, f32_type, 4);
    id->f32mat4_type = spi_OpTypeMatrix(spi, id->f32vec4_type, 4);

    /* Constants */
    id->u32_p = spi_OpUndef(spi, u32_type);
    id->f32_0 = spi_OpConstantFloat(spi, f32_type, 0);
    for (int i = 0; i < 5; i++)
        id->u32_cid[i] = spi_OpConstantUInt(spi, u32_type, i);

    /* Operation constants */
    for (int n = 0; n < ops->num_ops; n++) {
        const SwsOp *op = &ops->ops[n];
        OpIDs *oid = &id->op[n];
        SwsPixelType type_in = ff_sws_op_input_type(op);
        switch (op->op) {
        case SWS_OP_CLEAR:
            for (int i = 0; i < 4; i++) {
                if (!SWS_COMP_TEST(op->clear.mask, i))
                    continue;
                AVRational64 cv = op->clear.value[i];
                if (op->clear.type[i] == SWS_PIXEL_F32) {
                    float q = (float)cv.num/cv.den;
                    oid->comp[i] = spi_OpConstantFloat(spi, f32_type, q);
                } else {
                    av_assert0(cv.den == 1);
                    oid->comp[i] = spi_OpConstantUInt(spi, u32_type, cv.num);
                }
            }
            break;
        case SWS_OP_PACK:
        case SWS_OP_UNPACK: {
            uint64_t mask[4];
            int shift[4], tmp[4];
            ff_sws_pack_op_decode(op, mask, shift);
            for (int i = 0; i < 4; i++)
                tmp[i] = spi_OpConstantUInt(spi, u32_type, shift[i]);
            oid->pack.shift = spi_OpConstantComposite(spi, id->u32vec4_type,
                                                      tmp[0], tmp[1], tmp[2], tmp[3]);
            for (int i = 0; i < 4; i++)
                tmp[i] = spi_OpConstantUInt(spi, u32_type, mask[i]);
            oid->pack.mask = spi_OpConstantComposite(spi, id->u32vec4_type,
                                                     tmp[0], tmp[1], tmp[2], tmp[3]);
            break;
        }
        case SWS_OP_LSHIFT:
        case SWS_OP_RSHIFT: {
            int tmp = spi_OpConstantUInt(spi, u32_type, op->shift.amount);
            oid->vec = spi_OpConstantComposite(spi, id->u32vec4_type,
                                               tmp, tmp, tmp, tmp);
            break;
        }
        case SWS_OP_SCALE: {
            int tmp;
            if (type_in == SWS_PIXEL_F32) {
                float q = op->scale.factor.num/(float)op->scale.factor.den;
                tmp = spi_OpConstantFloat(spi, f32_type, q);
                tmp = spi_OpConstantComposite(spi, id->f32vec4_type,
                                              tmp, tmp, tmp, tmp);
            } else {
                av_assert0(op->scale.factor.den == 1);
                tmp = spi_OpConstantUInt(spi, u32_type, op->scale.factor.num);
                tmp = spi_OpConstantComposite(spi, id->u32vec4_type,
                                              tmp, tmp, tmp, tmp);
            }
            oid->vec = tmp;
            break;
        }
        case SWS_OP_MIN:
        case SWS_OP_MAX:
            for (int i = 0; i < 4; i++) {
                int tmp;
                AVRational64 cl = op->clamp.limit[i];
                if (!op->clamp.limit[i].den) {
                    continue;
                } else if (type_in == SWS_PIXEL_F32) {
                    float q = (float)cl.num/((float)cl.den);
                    tmp = spi_OpConstantFloat(spi, f32_type, q);
                } else {
                    av_assert0(cl.den == 1);
                    tmp = spi_OpConstantUInt(spi, u32_type, cl.num);
                }
                oid->comp[i] = tmp;
            }
            break;
        case SWS_OP_DITHER:
            for (int i = 0; i < 4; i++) {
                if (op->dither.y_offset[i] < 0)
                    continue;
                oid->comp[i] = spi_OpConstantUInt(spi, u32_type,
                                                  op->dither.y_offset[i]);
            }
            break;
        case SWS_OP_LINEAR: {
            LinearIDs *lin = &oid->lin;
            int vec[4], tmp[4];
            float val;
            for (int i = 0; i < 4; i++) {
                for (int j = 0; j < 4; j++) {
                    int k = sws->flags & SWS_BITEXACT ? i : j;
                    int l = sws->flags & SWS_BITEXACT ? j : i;
                    val = op->lin.m[k][l].num/(float)op->lin.m[k][l].den;
                    tmp[j] = lin->m[k][l] = spi_OpConstantFloat(spi, f32_type, val);
                }
                vec[i] = spi_OpConstantComposite(spi, id->f32vec4_type,
                                                 tmp[0], tmp[1], tmp[2], tmp[3]);
            }

            lin->mat = spi_OpConstantComposite(spi, id->f32mat4_type,
                                               vec[0], vec[1], vec[2], vec[3]);

            for (int i = 0; i < 4; i++) {
                val = op->lin.m[i][4].num/(float)op->lin.m[i][4].den;
                lin->off[i] = spi_OpConstantFloat(spi, f32_type, val);
            }

            lin->off_vec = spi_OpConstantComposite(spi, id->f32vec4_type,
                                                   lin->off[0], lin->off[1],
                                                   lin->off[2], lin->off[3]);
            break;
        }
        default:
            break;
        }
    }
}

/* Section 3: Define bindings */
static void define_shader_bindings(const SwsOpList *ops, SPICtx *spi, SPIRVIDs *id,
                                   int in_img_count, int out_img_count)
{
    id->dither_ptr_elem_id = spi_OpTypePointer(spi, SpvStorageClassUniform,
                                               id->f32_type);

    struct DitherData *dither = id->dither;
    for (int i = 0; i < id->nb_dither_bufs; i++) {
        int size_id = spi_OpConstantUInt(spi, id->u32_type, dither[i].size);
        dither[i].mask_id = spi_OpConstantUInt(spi, id->u32_type, dither[i].size - 1);
        spi_OpTypeArray(spi, id->f32_type, dither[i].arr_1d_id, size_id);
        spi_OpTypeArray(spi, dither[i].arr_1d_id, dither[i].arr_2d_id, size_id);
        spi_OpTypeStruct(spi, dither[i].struct_id, dither[i].arr_2d_id);
        dither[i].struct_ptr_id = spi_OpTypePointer(spi, SpvStorageClassUniform,
                                                    dither[i].struct_id);
        dither[i].id = spi_OpVariable(spi, dither[i].id, dither[i].struct_ptr_id,
                                      SpvStorageClassUniform, 0);
    }

    /* Filter buffers: struct { float w[dst_size][filter_size]; int o[dst_size]; } */
    id->filt_o_ptr_id = 0;
    if (id->nb_filter_bufs)
        id->filt_o_ptr_id = spi_OpTypePointer(spi, SpvStorageClassUniform,
                                              id->i32_type);

    for (int i = 0; i < id->nb_filter_bufs; i++) {
        struct FilterData *f = &id->filt[i];
        int fs_id = spi_OpConstantUInt(spi, id->u32_type, f->filter_size);
        int ds_id = spi_OpConstantUInt(spi, id->u32_type, f->dst_size);

        spi_OpTypeArray(spi, id->f32_type, f->arr_w_in_id, fs_id);
        spi_OpTypeArray(spi, f->arr_w_in_id, f->arr_w_out_id, ds_id);
        spi_OpTypeArray(spi, id->i32_type, f->arr_o_id, ds_id);
        spi_OpTypeStruct(spi, f->struct_id, f->arr_w_out_id, f->arr_o_id);
        f->struct_ptr_id = spi_OpTypePointer(spi, SpvStorageClassUniform,
                                             f->struct_id);
        f->id = spi_OpVariable(spi, f->id, f->struct_ptr_id,
                               SpvStorageClassUniform, 0);

        /* Signed tap-index constants 0..filter_size-1 (consecutive <id>s) */
        f->tap_const_base = spi_OpConstantInt(spi, id->i32_type, 0);
        for (int t = 1; t < f->filter_size; t++)
            spi_OpConstantInt(spi, id->i32_type, t);
    }

    const SwsOp *op_w = ff_sws_op_list_output(ops);
    const SwsOp *op_r = ff_sws_op_list_input(ops);

    /* Define image types for descriptors */
    id->out_img_type = spi_OpTypeImage(spi,
                                       op_w->rw.type == SWS_PIXEL_F32 ?
                                       id->f32_type : id->u32_type,
                                       2, 0, 0, 0, 2, SpvImageFormatUnknown);
    id->out_img_array_id = spi_OpTypeArray(spi, id->out_img_type, spi_get_id(spi),
                                           id->u32_cid[out_img_count]);

    id->in_img_type = 0;
    id->in_img_array_id = 0;
    if (op_r) {
        /* If the formats match, we have to reuse the types due to SPIR-V not
         * allowing redundant type defines */
        int match = ((op_w->rw.type == SWS_PIXEL_F32) ==
                     (op_r->rw.type == SWS_PIXEL_F32));
        id->in_img_type = match ? id->out_img_type :
                          spi_OpTypeImage(spi,
                                          op_r->rw.type == SWS_PIXEL_F32 ?
                                          id->f32_type : id->u32_type,
                                          2, 0, 0, 0, 2, SpvImageFormatUnknown);
        id->in_img_array_id = spi_OpTypeArray(spi, id->in_img_type, spi_get_id(spi),
                                              id->u32_cid[in_img_count]);
    }

    /* Pointer types for images */
    id->u32vec3_tptr = spi_OpTypePointer(spi, SpvStorageClassInput,
                                         id->u32vec3_type);
    id->out_img_tptr = spi_OpTypePointer(spi, SpvStorageClassUniformConstant,
                                         id->out_img_array_id);
    id->out_img_sptr = spi_OpTypePointer(spi, SpvStorageClassUniformConstant,
                                         id->out_img_type);

    id->in_img_tptr = 0;
    id->in_img_sptr = 0;
    if (op_r) {
        id->in_img_tptr = spi_OpTypePointer(spi, SpvStorageClassUniformConstant,
                                            id->in_img_array_id);
        id->in_img_sptr = spi_OpTypePointer(spi, SpvStorageClassUniformConstant,
                                            id->in_img_type);
    }

    /* Define inputs */
    spi_OpVariable(spi, id->gid_var, id->u32vec3_tptr,
                   SpvStorageClassInput, 0);
    if (op_r) {
        spi_OpVariable(spi, id->in_img_var, id->in_img_tptr,
                       SpvStorageClassUniformConstant, 0);
    }
    spi_OpVariable(spi, id->out_img_var, id->out_img_tptr,
                   SpvStorageClassUniformConstant, 0);

    if (id->interlaced) {
        spi_OpTypeStruct(spi, id->push_const_struct_id, id->u32_type);
        id->push_const_ptr_id = spi_OpTypePointer(spi, SpvStorageClassPushConstant,
                                                  id->push_const_struct_id);
        id->push_const_elem_ptr_id = spi_OpTypePointer(spi, SpvStorageClassPushConstant,
                                                       id->u32_type);
        spi_OpVariable(spi, id->push_const_var_id, id->push_const_ptr_id,
                       SpvStorageClassPushConstant, 0);
    }
}

static int insert_vmat_linear(SPICtx *spi, SPIRVIDs *id, const LinearIDs *lin,
                              int data)
{
    data = spi_OpMatrixTimesVector(spi, id->f32vec4_type, lin->mat, data);
    return spi_OpFAdd(spi, id->f32vec4_type, lin->off_vec, data);
}

static int insert_bitexact_linear(const SwsOp *op, SPICtx *spi, SPIRVIDs *id,
                                  const LinearIDs *lin, int data)
{
    const SwsPixelType op_type = ff_sws_op_input_type(op);
    int type_s = op_type == SWS_PIXEL_F32 ? id->f32_type : id->u32_type;
    int type_v = op_type == SWS_PIXEL_F32 ? id->f32vec4_type : id->u32vec4_type;

    int tmp[4];
    tmp[0] = spi_OpCompositeExtract(spi, type_s, data, 0);
    tmp[1] = spi_OpCompositeExtract(spi, type_s, data, 1);
    tmp[2] = spi_OpCompositeExtract(spi, type_s, data, 2);
    tmp[3] = spi_OpCompositeExtract(spi, type_s, data, 3);

    int off = spi_reserve(spi, 0); /* Current offset */
    spi->off = lin->deco_off;
    for (int i = 0; i < lin->nb_deco; i++)
        spi_OpDecorate(spi, spi->id + i, SpvDecorationNoContraction);
    spi->off = off;
    const int first_id = spi->id;

    int res[4];
    for (int j = 0; j < 4; j++) {
        res[j] = op_type == SWS_PIXEL_F32 ? id->f32_0 : id->u32_cid[0];
        if (op->lin.m[j][0].num)
            res[j] = spi_OpFMul(spi, type_s, tmp[0], lin->m[j][0]);

        if (op->lin.m[j][0].num && op->lin.m[j][4].num)
            res[j] = spi_OpFAdd(spi, type_s, lin->off[j], res[j]);
        else if (op->lin.m[j][4].num)
            res[j] = lin->off[j];

        for (int i = 1; i < 4; i++) {
            if (!op->lin.m[j][i].num)
                continue;

            int v = spi_OpFMul(spi, type_s, tmp[i], lin->m[j][i]);
            if (op->lin.m[j][0].num || op->lin.m[j][4].num)
                res[j] = spi_OpFAdd(spi, type_s, res[j], v);
            else
                res[j] = v;
        }
    }

    /* The decorations above assume one ID per arithmetic op */
    av_assert0(spi->id - first_id == lin->nb_deco);

    return spi_OpCompositeConstruct(spi, type_v,
                                    res[0], res[1], res[2], res[3]);
}

static int read_filtered(SPICtx *spi, SPIRVIDs *id, const SwsOpList *ops,
                         const SwsOp *op, const struct FilterData *f,
                         const int *in_img, int gid, int gi2)
{
    const int is_h = f->filter == SWS_OP_FILTER_H;
    const int src_interlaced = ops->src.interlaced;

    const int src_float = op->rw.type == SWS_PIXEL_F32;
    const int read_vtype = src_float ? id->f32vec4_type : id->u32vec4_type;

    /* Buffer array index along the filtered axis: pos.x (H) or pos.y (V) */
    int axis = spi_OpCompositeExtract(spi, id->u32_type, gid, is_h ? 0 : 1);

    /* int o = filter_o[axis]; */
    int o_ptr = spi_OpAccessChain(spi, id->filt_o_ptr_id, f->id,
                                  id->u32_cid[1], axis);
    int o = spi_OpLoad(spi, id->i32_type, o_ptr, SpvMemoryAccessMaskNone, 0);

    /* Signed pixel position, for the non-filtered coordinate axis */
    int pos_x = spi_OpCompositeExtract(spi, id->i32_type, gi2, 0);
    int pos_y = spi_OpCompositeExtract(spi, id->i32_type, gi2, 1);

    /* For interlaced horizontal filtering, the y coordinate of every tap is
     * the (constant) destination y mapped into the source image. */
    if (src_interlaced && is_h) {
        pos_y = spi_OpShiftLeftLogical(spi, id->i32_type, pos_y, id->u32_cid[1]);
        pos_y = spi_OpIAdd(spi, id->i32_type, pos_y, id->field_i32);
    }

    /* Accumulators, initialized to zero */
    int acc[4] = { id->f32_0, id->f32_0, id->f32_0, id->f32_0 };

    for (int t = 0; t < f->filter_size; t++) {
        /* float w = filter_w[axis][t]; */
        int w_ptr = spi_OpAccessChain(spi, id->dither_ptr_elem_id, f->id,
                                      id->u32_cid[0], axis,
                                      f->tap_const_base + t);
        int w = spi_OpLoad(spi, id->f32_type, w_ptr,
                           SpvMemoryAccessMaskNone, 0);

        /* Source coordinate, filtered axis offset by the tap index */
        int c = t ? spi_OpIAdd(spi, id->i32_type, o, f->tap_const_base + t) : o;
        /* For interlaced vertical filtering, the per-tap source row is
         * field-local; map it to the actual image row. */
        if (src_interlaced && !is_h) {
            c = spi_OpShiftLeftLogical(spi, id->i32_type, c, id->u32_cid[1]);
            c = spi_OpIAdd(spi, id->i32_type, c, id->field_i32);
        }
        int coord = is_h ?
            spi_OpCompositeConstruct(spi, id->i32vec2_type, c, pos_y) :
            spi_OpCompositeConstruct(spi, id->i32vec2_type, pos_x, c);

        for (int e = 0; e < op->rw.elems; e++) {
            int px = spi_OpImageRead(spi, read_vtype,
                                     in_img[ops->plane_src[e]], coord,
                                     SpvImageOperandsMaskNone);
            if (src_float) {
                px = spi_OpCompositeExtract(spi, id->f32_type, px, 0);
            } else {
                px = spi_OpCompositeExtract(spi, id->u32_type, px, 0);
                px = spi_OpConvertUToF(spi, id->f32_type, px);
            }
            px = spi_OpFMul(spi, id->f32_type, w, px);
            acc[e] = spi_OpFAdd(spi, id->f32_type, acc[e], px);
        }
    }

    return spi_OpCompositeConstruct(spi, id->f32vec4_type,
                                    acc[0], acc[1], acc[2], acc[3]);
}

/* Sub-packed formats are accessed as a single raw word per pixel, which is
 * then (un)packed manually. */
static VkFormat packed_word_fmt(const SwsOp *op)
{
    if (!op || op->rw.mode != SWS_RW_PACKED || op->rw.elems != 1)
        return VK_FORMAT_UNDEFINED;

    switch (op->rw.type) {
    case SWS_PIXEL_U8:  return VK_FORMAT_R8_UINT;
    case SWS_PIXEL_U16: return VK_FORMAT_R16_UINT;
    case SWS_PIXEL_U32: return VK_FORMAT_R32_UINT;
    default:            return VK_FORMAT_UNDEFINED;
    }
}

static int get_view_fmts(VkFormat view_fmts[AV_NUM_DATA_POINTERS],
                         enum AVPixelFormat fmt, const SwsOp *op)
{
    const VkFormat *vk_fmts = av_vkfmt_from_pixfmt(fmt);
    const VkFormat word_fmt = packed_word_fmt(op);
    enum FFVkShaderRepFormat rep = FF_VK_REP_NATIVE;
    if (op)
        rep = op->rw.type == SWS_PIXEL_F32 ? FF_VK_REP_FLOAT : FF_VK_REP_UINT;
    if (!vk_fmts)
        return AVERROR(ENOTSUP);

    for (int i = 0; i < av_pix_fmt_count_planes(fmt); i++) {
        view_fmts[i] = word_fmt ? word_fmt : ff_vk_map_fmt_to_rep(vk_fmts[i], rep);
        if (view_fmts[i] == VK_FORMAT_UNDEFINED)
            return AVERROR(ENOTSUP);
    }

    return 0;
}

/* Plane indices refer to actual frame planes, so the image handle arrays
 * have to cover the highest plane referenced, not just the plane count. */
static int rw_op_img_count(const SwsOp *op, const uint8_t *planes)
{
    int count = 0;
    for (int i = 0; i < ff_sws_rw_op_planes(op); i++)
        count = FFMAX(count, planes[i] + 1);
    return count;
}

static int add_ops_spirv(SwsContext *sws, VulkanPriv *p, FFVulkanOpsCtx *s,
                         const SwsOpList *ops, FFVulkanShader *shd)
{
    uint8_t spvbuf[1024*16];
    SPICtx spi_context = { 0 }, *spi = &spi_context;
    SPIRVIDs spid_data = { 0 }, *id = &spid_data;
    spi_init(spi, spvbuf, sizeof(spvbuf));

    if (ops->num_ops > MAX_OPS)
        return AVERROR(ENOTSUP);

    id->interlaced = ops->src.interlaced || ops->dst.interlaced;
    p->interlaced = id->interlaced;

    ff_vk_shader_load(shd, VK_SHADER_STAGE_COMPUTE_BIT, NULL,
                      (uint32_t []) { 32, 32, 1 }, 0);
    shd->precompiled = 0;

    if (id->interlaced)
        ff_vk_shader_add_push_const(shd, 0, sizeof(uint32_t),
                                    VK_SHADER_STAGE_COMPUTE_BIT);

    /* Image ops, to determine types */
    const SwsOp *op_w = ff_sws_op_list_output(ops);
    int out_img_count = rw_op_img_count(op_w, ops->plane_dst);
    const SwsOp *op_r = ff_sws_op_list_input(ops);
    int in_img_count = op_r ? rw_op_img_count(op_r, ops->plane_src) : 0;

    int err = get_view_fmts(p->dst_view_fmt, ops->dst.format, op_w);
    if (err < 0)
        return err;
    err = get_view_fmts(p->src_view_fmt, ops->src.format, op_r);
    if (err < 0)
        return err;

    FFVulkanDescriptorSetBinding img_desc[2] = {
        {
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stages = VK_SHADER_STAGE_COMPUTE_BIT,
            .elems = 4,
        },
        {
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stages = VK_SHADER_STAGE_COMPUTE_BIT,
            .elems = 4,
        },
    };
    ff_vk_shader_add_descriptor_set(&s->vkctx, shd, img_desc, 2, 0);

    /* Create dither buffers */
    err = create_bufs(s, p, ops);
    if (err < 0)
        return err;

    /* Entrypoint inputs; gl_GlobalInvocationID, input and output images, dither */
    id->gid_var     = id->iface[id->nb_iface++] = spi_get_id(spi);
    if (op_r)
        id->in_img_var = id->iface[id->nb_iface++] = spi_get_id(spi);
    id->out_img_var = id->iface[id->nb_iface++] = spi_get_id(spi);

    /* Create dither and filter buffer descriptor set. Both are collected in
     * op order, so the bindings match the buffer order from create_bufs().*/
    id->nb_dither_bufs = 0;
    id->nb_filter_bufs = 0;
    FFVulkanDescriptorSetBinding buf_desc[MAX_DATA_BUFS];
    int nb_data_bufs = 0;
    for (int n = 0; n < ops->num_ops; n++) {
        const SwsOp *op = &ops->ops[n];
        int var_id = 0;

        if (op->op == SWS_OP_DITHER) {
            if (id->nb_dither_bufs >= MAX_DITHER_BUFS)
                return AVERROR(ENOTSUP);
            id->op[n].buf = id->nb_dither_bufs;
            struct DitherData *d = &id->dither[id->nb_dither_bufs++];
            d->size      = 1 << op->dither.size_log2;
            d->arr_1d_id = spi_get_id(spi);
            d->arr_2d_id = spi_get_id(spi);
            d->struct_id = spi_get_id(spi);
            d->id        = spi_get_id(spi);
            d->binding   = nb_data_bufs;
            var_id       = d->id;
        } else if (op->op == SWS_OP_READ && op->rw.filter.op) {
            if (id->nb_filter_bufs >= MAX_FILT_BUFS)
                return AVERROR(ENOTSUP);
            const SwsFilterWeights *wd = op->rw.filter.kernel;
            id->op[n].buf = id->nb_filter_bufs;
            struct FilterData *f = &id->filt[id->nb_filter_bufs++];
            f->filter       = op->rw.filter.op;
            f->filter_size  = wd->filter_size;
            f->dst_size     = wd->dst_size;
            f->num_weights  = wd->num_weights;
            f->arr_w_in_id  = spi_get_id(spi);
            f->arr_w_out_id = spi_get_id(spi);
            f->arr_o_id     = spi_get_id(spi);
            f->struct_id    = spi_get_id(spi);
            f->id           = spi_get_id(spi);
            f->binding      = nb_data_bufs;
            var_id          = f->id;
        } else {
            continue;
        }

        id->iface[id->nb_iface++] = var_id;
        buf_desc[nb_data_bufs++] = (FFVulkanDescriptorSetBinding) {
            .type        = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
        };
    }
    if (nb_data_bufs)
        ff_vk_shader_add_descriptor_set(&s->vkctx, shd, buf_desc,
                                        nb_data_bufs, 1);

    if (id->interlaced) {
        id->push_const_struct_id = spi_get_id(spi);
        id->push_const_var_id = id->iface[id->nb_iface++] = spi_get_id(spi);
    }

    /* Define shader header sections */
    define_shader_header(sws, shd, ops, spi, id);
    define_shader_consts(sws, ops, spi, id);
    define_shader_bindings(ops, spi, id, in_img_count, out_img_count);

    /* Main function starts here */
    spi_OpFunction(spi, id->ep, id->void_type, 0, id->void_fn_type);
    spi_OpLabel(spi, spi_get_id(spi));

    /* Load input image handles */
    int in_img[4] = { 0 };
    for (int i = 0; i < in_img_count; i++) {
        /* Deref array and then the pointer */
        int img = spi_OpAccessChain(spi, id->in_img_sptr,
                                    id->in_img_var, id->u32_cid[i]);
        in_img[i] = spi_OpLoad(spi, id->in_img_type, img,
                               SpvMemoryAccessMaskNone, 0);
    }

    /* Load output image handles */
    int out_img[4] = { 0 };
    for (int i = 0; i < out_img_count; i++) {
        int img = spi_OpAccessChain(spi, id->out_img_sptr,
                                    id->out_img_var, id->u32_cid[i]);
        out_img[i] = spi_OpLoad(spi, id->out_img_type, img,
                                SpvMemoryAccessMaskNone, 0);
    }

    /* Load gl_GlobalInvocationID */
    int gid = spi_OpLoad(spi, id->u32vec3_type, id->gid_var,
                         SpvMemoryAccessMaskNone, 0);

    /* ivec2(gl_GlobalInvocationID.xy) */
    gid = spi_OpVectorShuffle(spi, id->u32vec2_type, gid, gid, 0, 1);
    int gi2 = spi_OpBitcast(spi, id->i32vec2_type, gid);

    /* For interlaced sources/destinations the shader operates on field-local
     * coordinates, while images contain the full frame. Map the y axis to the
     * actual image row: image_y = field_y * 2 + field. */
    int dst_gid = gid, dst_gi2 = gi2;
    int src_gid = gid;
    if (id->interlaced) {
        int field_u32_ptr = spi_OpAccessChain(spi, id->push_const_elem_ptr_id,
                                              id->push_const_var_id,
                                              id->u32_cid[0]);
        int field_u32 = spi_OpLoad(spi, id->u32_type, field_u32_ptr,
                                   SpvMemoryAccessMaskNone, 0);
        id->field_i32 = spi_OpBitcast(spi, id->i32_type, field_u32);

        int img_y_i32 = spi_OpShiftLeftLogical(spi, id->i32_type,
                                               spi_OpCompositeExtract(spi, id->i32_type, gi2, 1),
                                               id->u32_cid[1]);
        img_y_i32 = spi_OpIAdd(spi, id->i32_type, img_y_i32, id->field_i32);

        int gi2_x = spi_OpCompositeExtract(spi, id->i32_type, gi2, 0);
        int mapped_gi2 = spi_OpCompositeConstruct(spi, id->i32vec2_type,
                                                  gi2_x, img_y_i32);
        int mapped_gid = spi_OpBitcast(spi, id->u32vec2_type, mapped_gi2);

        if (ops->src.interlaced)
            src_gid = mapped_gid;
        if (ops->dst.interlaced) {
            dst_gid = mapped_gid;
            dst_gi2 = mapped_gi2;
        }
    }

    /* imageSize(out_img[0]); */
    int img1_s = spi_OpImageQuerySize(spi, id->i32vec2_type, out_img[0]);
    int scmp = spi_OpSGreaterThanEqual(spi, id->bvec2_type, dst_gi2, img1_s);
    scmp = spi_OpAny(spi, id->b_type, scmp);

    /* if (out of bounds) return */
    int quit_label = spi_get_id(spi), merge_label = spi_get_id(spi);
    spi_OpSelectionMerge(spi, merge_label, SpvSelectionControlMaskNone);
    spi_OpBranchConditional(spi, scmp, quit_label, merge_label, 0);

    spi_OpLabel(spi, quit_label);
    spi_OpReturn(spi); /* Quit if out of bounds here */
    spi_OpLabel(spi, merge_label);

    /* Initialize main data state */
    int data;
    if (ff_sws_op_output_type(&ops->ops[0]) == SWS_PIXEL_F32)
        data = spi_OpCompositeConstruct(spi, id->f32vec4_type,
                                        id->f32_0, id->f32_0,
                                        id->f32_0, id->f32_0);
    else
        data = spi_OpCompositeConstruct(spi, id->u32vec4_type,
                                        id->u32_p, id->u32_p,
                                        id->u32_p, id->u32_p);

    /* Operations */
    for (int n = 0; n < ops->num_ops; n++) {
        const SwsOp *op = &ops->ops[n];
        const OpIDs *oid = &id->op[n];
        SwsPixelType type_in  = ff_sws_op_input_type(op);
        SwsPixelType type_out = ff_sws_op_output_type(op);
        if (op->op == SWS_OP_WRITE)
            type_out = type_in;
        int type_v = type_out == SWS_PIXEL_F32 ?
                     id->f32vec4_type : id->u32vec4_type;
        int type_s = type_out == SWS_PIXEL_F32 ?
                     id->f32_type : id->u32_type;
        int uid = type_out == SWS_PIXEL_F32 ?
                  id->f32_0 : id->u32_p;

        switch (op->op) {
        case SWS_OP_READ:
            if (op->rw.frac) {
                return AVERROR(ENOTSUP);
            } else if (op->rw.filter.op) {
                if (op->rw.mode != SWS_RW_PLANAR ||
                    op->rw.filter.type != SWS_PIXEL_F32)
                    return AVERROR(ENOTSUP);
                data = read_filtered(spi, id, ops, op,
                                     &id->filt[oid->buf],
                                     in_img, gid, gi2);
            } else if (op->rw.mode == SWS_RW_PACKED) {
                data = spi_OpImageRead(spi, type_v, in_img[ops->plane_src[0]],
                                       src_gid, SpvImageOperandsMaskNone);
            } else if (op->rw.mode == SWS_RW_PLANAR) {
                int tmp[4] = { uid, uid, uid, uid };
                for (int i = 0; i < op->rw.elems; i++) {
                    tmp[i] = spi_OpImageRead(spi, type_v,
                                             in_img[ops->plane_src[i]], src_gid,
                                             SpvImageOperandsMaskNone);
                    tmp[i] = spi_OpCompositeExtract(spi, type_s, tmp[i], 0);
                }
                data = spi_OpCompositeConstruct(spi, type_v,
                                                tmp[0], tmp[1], tmp[2], tmp[3]);
            } else {
                return AVERROR(ENOTSUP);
            }
            break;
        case SWS_OP_WRITE:
            if (op->rw.frac || op->rw.filter.op) {
                return AVERROR(ENOTSUP);
            } else if (op->rw.mode == SWS_RW_PACKED) {
                spi_OpImageWrite(spi, out_img[ops->plane_dst[0]], dst_gid, data,
                                 SpvImageOperandsMaskNone);
            } else {
                for (int i = 0; i < op->rw.elems; i++) {
                    int tmp = spi_OpCompositeExtract(spi, type_s, data, i);
                    tmp = spi_OpCompositeConstruct(spi, type_v, tmp, tmp, tmp, tmp);
                    spi_OpImageWrite(spi, out_img[ops->plane_dst[i]], dst_gid, tmp,
                                     SpvImageOperandsMaskNone);
                }
            }
            break;
        case SWS_OP_CLEAR:
            for (int i = 0; i < 4; i++) {
                if (!SWS_COMP_TEST(op->clear.mask, i))
                    continue;
                data = spi_OpCompositeInsert(spi, type_v, oid->comp[i], data, i);
            }
            break;
        case SWS_OP_SWIZZLE:
            data = spi_OpVectorShuffle(spi, type_v, data, data,
                                       op->swizzle.in[0],
                                       op->swizzle.in[1],
                                       op->swizzle.in[2],
                                       op->swizzle.in[3]);
            break;
        case SWS_OP_CONVERT:
            if (type_in == SWS_PIXEL_F32 && type_s == id->u32_type)
                data = spi_OpConvertFToU(spi, type_v, data);
            else if (type_in != SWS_PIXEL_F32 && type_s == id->f32_type)
                data = spi_OpConvertUToF(spi, type_v, data);
            break;
        case SWS_OP_LSHIFT:
            data = spi_OpShiftLeftLogical(spi, type_v, data, oid->vec);
            break;
        case SWS_OP_RSHIFT:
            data = spi_OpShiftRightLogical(spi, type_v, data, oid->vec);
            break;
        case SWS_OP_SCALE:
            if (type_in == SWS_PIXEL_F32)
                data = spi_OpFMul(spi, type_v, data, oid->vec);
            else
                data = spi_OpIMul(spi, type_v, data, oid->vec);
            break;
        case SWS_OP_MIN:
        case SWS_OP_MAX: {
            int t = type_in == SWS_PIXEL_F32 ?
                    op->op == SWS_OP_MIN ? GLSLstd450FMin : GLSLstd450FMax :
                    op->op == SWS_OP_MIN ? GLSLstd450UMin : GLSLstd450UMax;
            for (int i = 0; i < 4; i++) {
                if (!op->clamp.limit[i].den)
                    continue;
                int tmp = spi_OpCompositeExtract(spi, type_s, data, i);
                tmp = spi_OpExtInst(spi, type_s, id->glfn, t, tmp, oid->comp[i]);
                data = spi_OpCompositeInsert(spi, type_v, tmp, data, i);
            }
            break;
        }
        case SWS_OP_DITHER: {
            const struct DitherData *d = &id->dither[oid->buf];
            int x_id = spi_OpCompositeExtract(spi, id->u32_type, gid, 0);
            int y_pos = spi_OpCompositeExtract(spi, id->u32_type, gid, 1);
            x_id = spi_OpBitwiseAnd(spi, id->u32_type, x_id, d->mask_id);
            for (int i = 0; i < 4; i++) {
                if (op->dither.y_offset[i] < 0)
                    continue;

                int y_id = spi_OpIAdd(spi, id->u32_type, y_pos, oid->comp[i]);
                y_id = spi_OpBitwiseAnd(spi, id->u32_type, y_id, d->mask_id);

                int ptr = spi_OpAccessChain(spi, id->dither_ptr_elem_id, d->id,
                                            id->u32_cid[0], y_id, x_id);
                int val = spi_OpLoad(spi, id->f32_type, ptr,
                                     SpvMemoryAccessMaskNone, 0);

                int tmp = spi_OpCompositeExtract(spi, type_s, data, i);
                tmp = spi_OpFAdd(spi, type_s, tmp, val);
                data = spi_OpCompositeInsert(spi, type_v, tmp, data, i);
            }
            break;
        }
        case SWS_OP_LINEAR: {
            if (type_in != SWS_PIXEL_F32)
                return AVERROR(ENOTSUP);
            if (sws->flags & SWS_BITEXACT)
                data = insert_bitexact_linear(op, spi, id, &oid->lin, data);
            else
                data = insert_vmat_linear(spi, id, &oid->lin, data);
            break;
        }
        case SWS_OP_UNPACK:
            data = spi_OpVectorShuffle(spi, type_v, data, data, 0, 0, 0, 0);
            data = spi_OpShiftRightLogical(spi, type_v, data, oid->pack.shift);
            data = spi_OpBitwiseAnd(spi, type_v, data, oid->pack.mask);
            break;
        case SWS_OP_PACK: {
            data = spi_OpBitwiseAnd(spi, type_v, data, oid->pack.mask);
            data = spi_OpShiftLeftLogical(spi, type_v, data, oid->pack.shift);
            int val = spi_OpCompositeExtract(spi, type_s, data, 0);
            for (int i = 1; i < 4; i++) {
                int tmp = spi_OpCompositeExtract(spi, type_s, data, i);
                val = spi_OpBitwiseOr(spi, type_s, val, tmp);
            }
            data = spi_OpCompositeConstruct(spi, type_v, val, val, val, val);
            break;
        }
        default:
            return AVERROR(ENOTSUP);
        }
    }

    /* Return and finalize */
    spi_OpReturn(spi);
    spi_OpFunctionEnd(spi);

    int len = spi_end(spi);
    if (len < 0)
        return AVERROR_INVALIDDATA;

    return ff_vk_shader_link(&s->vkctx, shd, spvbuf, len, "main");
}

static int compile_spirv(SwsContext *sws, const SwsOpList *ops,
                         SwsCompiledOp *out)
{
    int err;
    SwsInternal *c = sws_internal(sws);
    FFVulkanOpsCtx *s = c->hw_priv;
    if (!s)
        return AVERROR(ENOTSUP);

    VulkanPriv *p = av_mallocz(sizeof(*p));
    if (!p)
        return AVERROR(ENOMEM);
    p->s = av_refstruct_ref(c->hw_priv);

    err = ff_vk_exec_pool_init(&s->vkctx, s->qf, &p->e, 1,
                               0, 0, 0, NULL);
    if (err < 0)
        goto fail;

    err = add_ops_spirv(sws, p, s, ops, &p->shd);
    if (err < 0)
        goto fail;

    err = ff_vk_shader_register_exec(&s->vkctx, &p->e, &p->shd);
    if (err < 0)
        goto fail;

    for (int i = 0; i < p->nb_data_bufs; i++)
        ff_vk_shader_update_desc_buffer(&s->vkctx, &p->e.contexts[0], &p->shd,
                                        1, i, 0, &p->data_bufs[i],
                                        0, VK_WHOLE_SIZE, VK_FORMAT_UNDEFINED);

    *out = (SwsCompiledOp) {
        .opaque      = true,
        .func_opaque = spirv_process,
        .priv        = p,
        .free        = free_fn,
    };

    return 0;

fail:
    free_fn(p);
    return err;
}

const SwsOpBackend backend_spirv = {
    .name      = "spirv",
    .flags     = SWS_BACKEND_SPIRV,
    .compile   = compile_spirv,
    .hw_format = AV_PIX_FMT_VULKAN,
};
