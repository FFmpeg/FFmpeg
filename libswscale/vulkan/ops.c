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

#if HAVE_SPIRV_HEADERS_SPIRV_H || HAVE_SPIRV_UNIFIED1_SPIRV_H
#include "spvasm.h"
#endif

static void ff_sws_vk_uninit(AVRefStructOpaque opaque, void *obj)
{
    FFVulkanOpsCtx *s = obj;

#if CONFIG_LIBSHADERC || CONFIG_LIBGLSLANG
    if (s->spvc)
        s->spvc->uninit(&s->spvc);
#endif
    ff_vk_uninit(&s->vkctx);
}

int ff_sws_vk_init(SwsContext *sws, AVBufferRef *dev_ref)
{
    int err;
    SwsInternal *c = sws_internal(sws);

    if (!c->hw_priv) {
        c->hw_priv = av_refstruct_alloc_ext(sizeof(FFVulkanOpsCtx), 0, NULL,
                                            ff_sws_vk_uninit);
        if (!c->hw_priv)
            return AVERROR(ENOMEM);
    }

    FFVulkanOpsCtx *s = c->hw_priv;
    if (s->vkctx.device_ref && s->vkctx.device_ref->data != dev_ref->data) {
        /* Reinitialize with new context */
        ff_vk_uninit(&s->vkctx);
    } else if (s->vkctx.device_ref && s->vkctx.device_ref->data == dev_ref->data) {
        return 0;
    }

    err = ff_vk_init(&s->vkctx, sws, dev_ref, NULL);
    if (err < 0)
        return err;

    s->qf = ff_vk_qf_find(&s->vkctx, VK_QUEUE_COMPUTE_BIT, 0);
    if (!s->qf) {
        av_log(sws, AV_LOG_ERROR, "Device has no compute queues\n");
        return AVERROR(ENOTSUP);
    }

#if CONFIG_LIBSHADERC || CONFIG_LIBGLSLANG
    if (!s->spvc) {
        s->spvc = ff_vk_spirv_init();
        if (!s->spvc)
            return AVERROR(ENOMEM);
    }
#endif

    return 0;
}

AVBufferRef *ff_sws_vk_device_ref(SwsContext *sws)
{
    SwsInternal *c = sws_internal(sws);
    FFVulkanOpsCtx *s = c->hw_priv;
    return s ? s->vkctx.device_ref : NULL;
}

#define MAX_DITHER_BUFS 4
#define MAX_FILT_BUFS 4
#define MAX_DATA_BUFS (MAX_DITHER_BUFS + MAX_FILT_BUFS*4)

typedef struct VulkanPriv {
    FFVulkanOpsCtx *s;
    FFVkExecPool e;
    FFVulkanShader shd;
    FFVkBuffer data_bufs[MAX_DATA_BUFS];
    int nb_data_bufs;
    enum FFVkShaderRepFormat src_rep;
    enum FFVkShaderRepFormat dst_rep;
    int interlaced;
} VulkanPriv;

static void process(const SwsFrame *dst, const SwsFrame *src, int y, int h,
                    const SwsPass *pass)
{
    VulkanPriv *p = (VulkanPriv *) pass->priv;
    FFVkExecContext *ec = ff_vk_exec_get(&p->s->vkctx, &p->e);
    FFVulkanFunctions *vk = &p->s->vkctx.vkfn;
    ff_vk_exec_start(&p->s->vkctx, ec);

    AVFrame *src_f = (AVFrame *) src->avframe;
    AVFrame *dst_f = (AVFrame *) dst->avframe;
    ff_vk_exec_add_dep_frame(&p->s->vkctx, ec, src_f,
                             VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                             VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    ff_vk_exec_add_dep_frame(&p->s->vkctx, ec, dst_f,
                             VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                             VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);

    VkImageView src_views[AV_NUM_DATA_POINTERS];
    VkImageView dst_views[AV_NUM_DATA_POINTERS];
    ff_vk_create_imageviews(&p->s->vkctx, ec, src_views, src_f, p->src_rep);
    ff_vk_create_imageviews(&p->s->vkctx, ec, dst_views, dst_f, p->dst_rep);

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
        uint32_t field = pass->graph ? pass->graph->field : 0;
        ff_vk_shader_update_push_const(&p->s->vkctx, ec, &p->shd,
                                       VK_SHADER_STAGE_COMPUTE_BIT,
                                       0, sizeof(field), &field);
    }

    ff_vk_exec_bind_shader(&p->s->vkctx, ec, &p->shd);

    vk->CmdDispatch(ec->buf,
                    FFALIGN(dst->width, p->shd.lg_size[0])/p->shd.lg_size[0],
                    FFALIGN(dst->height, p->shd.lg_size[1])/p->shd.lg_size[1],
                    1);

    ff_vk_exec_submit(&p->s->vkctx, ec);
    ff_vk_exec_wait(&p->s->vkctx, ec);
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

static int create_filter_buf(FFVulkanOpsCtx *s, VulkanPriv *p,
                             const SwsFilterWeights *wd, FFVkBuffer *buf)
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
    ff_vk_free_buf(&p->s->vkctx, buf);
    return 0;
}

static int create_dither_buf(FFVulkanOpsCtx *s, VulkanPriv *p,
                             const SwsDitherOp *dd, FFVkBuffer *buf)
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
            const AVRational r = dd->matrix[i*size + j];
            dither_data[i*size + j] = r.num/(float)r.den;
        }
    }

    ff_vk_unmap_buffer(&s->vkctx, buf, 1);

    return 0;

fail:
    ff_vk_free_buf(&p->s->vkctx, buf);
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
            err = create_dither_buf(s, p, &op->dither,
                                    &p->data_bufs[p->nb_data_bufs]);
            if (err < 0)
                goto fail;
            p->nb_data_bufs++;
        } else if (op->op == SWS_OP_FILTER_H || op->op == SWS_OP_FILTER_V) {
            av_assert0(p->nb_data_bufs + 1 <= FF_ARRAY_ELEMS(p->data_bufs));
            err = create_filter_buf(s, p, op->filter.kernel,
                                    &p->data_bufs[p->nb_data_bufs]);
            if (err < 0)
                goto fail;
            p->nb_data_bufs++;
        } else if ((op->op == SWS_OP_READ ||
                    op->op == SWS_OP_WRITE) && op->rw.filter.op) {
            av_assert0(p->nb_data_bufs + 1 <= FF_ARRAY_ELEMS(p->data_bufs));
            err = create_filter_buf(s, p, op->rw.filter.kernel,
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

#if HAVE_SPIRV_HEADERS_SPIRV_H || HAVE_SPIRV_UNIFIED1_SPIRV_H
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

typedef struct SPIRVIDs {
    int in_vars[3 + MAX_DATA_BUFS + 1];

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
    int f32_p;
    int f32_0;
    int u32_cid[5];

    int const_ids[128];
    int nb_const_ids;

    int linear_deco_off[16];
    int linear_deco_ops[16];
    int nb_linear_ops;

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

/* Section 1: Function to define all shader header data, and decorations */
static void define_shader_header(SwsContext *sws, FFVulkanShader *shd,
                                 const SwsOpList *ops, SPICtx *spi, SPIRVIDs *id)
{
    spi_OpCapability(spi, SpvCapabilityShader); /* Shader type */

    /* Declare required capabilities */
    spi_OpCapability(spi, SpvCapabilityInt16);
    spi_OpCapability(spi, SpvCapabilityInt8);
    spi_OpCapability(spi, SpvCapabilityImageQuery);
    spi_OpCapability(spi, SpvCapabilityStorageImageReadWithoutFormat);
    spi_OpCapability(spi, SpvCapabilityStorageImageWriteWithoutFormat);
    spi_OpCapability(spi, SpvCapabilityStorageBuffer8BitAccess);
    /* Import the GLSL set of functions (used for min/max) */
    id->glfn = spi_OpExtInstImport(spi, "GLSL.std.450");

    /* Next section starts here */
    spi_OpMemoryModel(spi, SpvAddressingModelLogical, SpvMemoryModelGLSL450);

    /* Entrypoint */
    id->ep = spi_OpEntryPoint(spi, SpvExecutionModelGLCompute, "main",
                                id->in_vars,
                                3 + id->nb_dither_bufs + id->nb_filter_bufs +
                                (id->interlaced ? 1 : 0));
    spi_OpExecutionMode(spi, id->ep, SpvExecutionModeLocalSize,
                        shd->lg_size, 3);

    if (id->interlaced) {
        spi_OpDecorate(spi, id->push_const_struct_id, SpvDecorationBlock);
        spi_OpMemberDecorate(spi, id->push_const_struct_id, 0,
                             SpvDecorationOffset, 0);
    }

    /* gl_GlobalInvocationID descriptor decorations */
    spi_OpDecorate(spi, id->in_vars[0], SpvDecorationBuiltIn,
                   SpvBuiltInGlobalInvocationId);

    /* Input image descriptor decorations */
    spi_OpDecorate(spi, id->in_vars[1], SpvDecorationNonWritable);
    spi_OpDecorate(spi, id->in_vars[1], SpvDecorationDescriptorSet, 0);
    spi_OpDecorate(spi, id->in_vars[1], SpvDecorationBinding, 0);

    /* Output image descriptor decorations */
    spi_OpDecorate(spi, id->in_vars[2], SpvDecorationNonReadable);
    spi_OpDecorate(spi, id->in_vars[2], SpvDecorationDescriptorSet, 0);
    spi_OpDecorate(spi, id->in_vars[2], SpvDecorationBinding, 1);

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
        av_assert0((id->nb_linear_ops + 1) <= FF_ARRAY_ELEMS(id->linear_deco_off));

        int nb_ops = 0;
        for (int j = 0; j < 4; j++) {
            nb_ops += !!op->lin.m[j][0].num;
            nb_ops += op->lin.m[j][0].num && op->lin.m[j][4].num;
            for (int i = 1; i < 4; i++) {
                nb_ops += !!op->lin.m[j][i].num;
                nb_ops += op->lin.m[j][i].num &&
                          (op->lin.m[j][0].num || op->lin.m[j][4].num);
            }
        }

        id->linear_deco_off[id->nb_linear_ops] = spi_reserve(spi, nb_ops*4*3);
        id->linear_deco_ops[id->nb_linear_ops] = nb_ops;
        id->nb_linear_ops++;
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
    id->f32_p = spi_OpUndef(spi, f32_type);
    id->f32_0 = spi_OpConstantFloat(spi, f32_type, 0);
    for (int i = 0; i < 5; i++)
        id->u32_cid[i] = spi_OpConstantUInt(spi, u32_type, i);

    /* Operation constants */
    id->nb_const_ids = 0;
    for (int n = 0; n < ops->num_ops; n++) {
        /* Make sure there's always enough space for the maximum number of
         * constants a single operation needs (currently linear, 31 consts). */
        av_assert0((id->nb_const_ids + 31) <= FF_ARRAY_ELEMS(id->const_ids));
        const SwsOp *op = &ops->ops[n];
        switch (op->op) {
        case SWS_OP_CONVERT:
            if (ff_sws_pixel_type_is_int(op->convert.to) && op->convert.expand) {
                AVRational m = ff_sws_pixel_expand(op->type, op->convert.to);
                int tmp = spi_OpConstantUInt(spi, id->u32_type, m.num);
                tmp = spi_OpConstantComposite(spi, id->u32vec4_type,
                                              tmp, tmp, tmp, tmp);
                id->const_ids[id->nb_const_ids++] = tmp;
            }
            break;
        case SWS_OP_CLEAR:
            for (int i = 0; i < 4; i++) {
                if (!SWS_COMP_TEST(op->clear.mask, i))
                    continue;
                AVRational cv = op->clear.value[i];
                if (op->type == SWS_PIXEL_F32) {
                    float q = (float)cv.num/cv.den;
                    id->const_ids[id->nb_const_ids++] =
                        spi_OpConstantFloat(spi, f32_type, q);
                } else {
                    av_assert0(cv.den == 1);
                    id->const_ids[id->nb_const_ids++] =
                        spi_OpConstantUInt(spi, u32_type, cv.num);
                }
            }
            break;
        case SWS_OP_LSHIFT:
        case SWS_OP_RSHIFT: {
            int tmp = spi_OpConstantUInt(spi, u32_type, op->shift.amount);
            tmp = spi_OpConstantComposite(spi, id->u32vec4_type,
                                          tmp, tmp, tmp, tmp);
            id->const_ids[id->nb_const_ids++] = tmp;
            break;
        }
        case SWS_OP_SCALE: {
            int tmp;
            if (op->type == SWS_PIXEL_F32) {
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
            id->const_ids[id->nb_const_ids++] = tmp;
            break;
        }
        case SWS_OP_MIN:
        case SWS_OP_MAX:
            for (int i = 0; i < 4; i++) {
                int tmp;
                AVRational cl = op->clamp.limit[i];
                if (!op->clamp.limit[i].den) {
                    continue;
                } else if (op->type == SWS_PIXEL_F32) {
                    float q = (float)cl.num/((float)cl.den);
                    tmp = spi_OpConstantFloat(spi, f32_type, q);
                } else {
                    av_assert0(cl.den == 1);
                    tmp = spi_OpConstantUInt(spi, u32_type, cl.num);
                }
                id->const_ids[id->nb_const_ids++] = tmp;
            }
            break;
        case SWS_OP_DITHER:
            for (int i = 0; i < 4; i++) {
                if (op->dither.y_offset[i] < 0)
                    continue;
                int tmp = spi_OpConstantUInt(spi, u32_type, op->dither.y_offset[i]);
                id->const_ids[id->nb_const_ids++] = tmp;
            }
            break;
        case SWS_OP_LINEAR: {
            int tmp;
            float val;
            for (int i = 0; i < 4; i++) {
                for (int j = 0; j < 4; j++) {
                    int k = sws->flags & SWS_BITEXACT ? i : j;
                    int l = sws->flags & SWS_BITEXACT ? j : i;
                    val = op->lin.m[k][l].num/(float)op->lin.m[k][l].den;
                    id->const_ids[id->nb_const_ids++] =
                        spi_OpConstantFloat(spi, f32_type, val);
                }
                tmp = spi_OpConstantComposite(spi, id->f32vec4_type,
                                              id->const_ids[id->nb_const_ids - 4],
                                              id->const_ids[id->nb_const_ids - 3],
                                              id->const_ids[id->nb_const_ids - 2],
                                              id->const_ids[id->nb_const_ids - 1]);
                id->const_ids[id->nb_const_ids++] = tmp;
            }

            tmp = spi_OpConstantComposite(spi, id->f32mat4_type,
                                          id->const_ids[id->nb_const_ids - 5*4 + 4],
                                          id->const_ids[id->nb_const_ids - 5*3 + 4],
                                          id->const_ids[id->nb_const_ids - 5*2 + 4],
                                          id->const_ids[id->nb_const_ids - 5*1 + 4]);
            id->const_ids[id->nb_const_ids++] = tmp;

            for (int i = 0; i < 4; i++) {
                val = op->lin.m[i][4].num/(float)op->lin.m[i][4].den;
                id->const_ids[id->nb_const_ids++] =
                    spi_OpConstantFloat(spi, f32_type, val);
            }

            tmp = spi_OpConstantComposite(spi, id->f32vec4_type,
                                          id->const_ids[id->nb_const_ids - 4],
                                          id->const_ids[id->nb_const_ids - 3],
                                          id->const_ids[id->nb_const_ids - 2],
                                          id->const_ids[id->nb_const_ids - 1]);
            id->const_ids[id->nb_const_ids++] = tmp;
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
                                       op_w->type == SWS_PIXEL_F32 ?
                                       id->f32_type : id->u32_type,
                                       2, 0, 0, 0, 2, SpvImageFormatUnknown);
    id->out_img_array_id = spi_OpTypeArray(spi, id->out_img_type, spi_get_id(spi),
                                           id->u32_cid[out_img_count]);

    id->in_img_type = 0;
    id->in_img_array_id = 0;
    if (op_r) {
        /* If the formats match, we have to reuse the types due to SPIR-V not
         * allowing redundant type defines */
        int match = ((op_w->type == SWS_PIXEL_F32) ==
                     (op_r->type == SWS_PIXEL_F32));
        id->in_img_type = match ? id->out_img_type :
                            spi_OpTypeImage(spi,
                                            op_r->type == SWS_PIXEL_F32 ?
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
        id->in_img_tptr= spi_OpTypePointer(spi, SpvStorageClassUniformConstant,
                                           id->in_img_array_id);
        id->in_img_sptr= spi_OpTypePointer(spi, SpvStorageClassUniformConstant,
                                           id->in_img_type);
    }

    /* Define inputs */
    spi_OpVariable(spi, id->in_vars[0], id->u32vec3_tptr,
                   SpvStorageClassInput, 0);
    if (op_r) {
        spi_OpVariable(spi, id->in_vars[1], id->in_img_tptr,
                       SpvStorageClassUniformConstant, 0);
    }
    spi_OpVariable(spi, id->in_vars[2], id->out_img_tptr,
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

static int insert_vmat_linear(const SwsOp *op, SPICtx *spi, SPIRVIDs *id,
                              int data, int const_off)
{
    data =  spi_OpMatrixTimesVector(spi, id->f32vec4_type,
                                    id->const_ids[const_off + 4*5],
                                    data);
    return spi_OpFAdd(spi, id->f32vec4_type,
                      id->const_ids[const_off + 4*5 + 1 + 4], data);
}

static int insert_bitexact_linear(const SwsOp *op, SPICtx *spi, SPIRVIDs *id,
                                  int data, int linear_ops_idx, int const_off)
{
    int type_s = op->type == SWS_PIXEL_F32 ? id->f32_type : id->u32_type;
    int type_v = op->type == SWS_PIXEL_F32 ? id->f32vec4_type : id->u32vec4_type;

    int tmp[4];
    tmp[0] = spi_OpCompositeExtract(spi, type_s, data, 0);
    tmp[1] = spi_OpCompositeExtract(spi, type_s, data, 1);
    tmp[2] = spi_OpCompositeExtract(spi, type_s, data, 2);
    tmp[3] = spi_OpCompositeExtract(spi, type_s, data, 3);

    int off = spi_reserve(spi, 0); /* Current offset */
    spi->off = id->linear_deco_off[linear_ops_idx];
    for (int i = 0; i < id->linear_deco_ops[linear_ops_idx]; i++)
        spi_OpDecorate(spi, spi->id + i, SpvDecorationNoContraction);
    spi->off = off;

    int res[4];
    for (int j = 0; j < 4; j++) {
        res[j] = op->type == SWS_PIXEL_F32 ? id->f32_0 : id->u32_cid[0];
        if (op->lin.m[j][0].num)
            res[j] = spi_OpFMul(spi, type_s, tmp[0],
                                id->const_ids[const_off + j*5 + 0]);

        if (op->lin.m[j][0].num && op->lin.m[j][4].num)
            res[j] = spi_OpFAdd(spi, type_s,
                                id->const_ids[const_off + 4*5 + 1 + j], res[j]);
        else if (op->lin.m[j][4].num)
            res[j] = id->const_ids[const_off + 4*5 + 1 + j];

        for (int i = 1; i < 4; i++) {
            if (!op->lin.m[j][i].num)
                continue;

            int v = spi_OpFMul(spi, type_s, tmp[i],
                               id->const_ids[const_off + j*5 + i]);
            if (op->lin.m[j][0].num || op->lin.m[j][4].num)
                res[j] = spi_OpFAdd(spi, type_s, res[j], v);
            else
                res[j] = v;
        }
    }

    return spi_OpCompositeConstruct(spi, type_v,
                                    res[0], res[1], res[2], res[3]);
}

static int read_filtered(SPICtx *spi, SPIRVIDs *id, const SwsOpList *ops,
                         const SwsOp *op, const struct FilterData *f,
                         const int *in_img, int gid, int gi2)
{
    const int is_h = f->filter == SWS_OP_FILTER_H;
    const int src_interlaced = ops->src.interlaced;

    const int src_float = op->type == SWS_PIXEL_F32;
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
    int acc_s[4] = { id->f32_0, id->f32_0, id->f32_0, id->f32_0 };
    int acc_v = id->f32_0;
    if (op->rw.mode == SWS_RW_PACKED)
        acc_v = spi_OpCompositeConstruct(spi, id->f32vec4_type,
                                         id->f32_0, id->f32_0,
                                         id->f32_0, id->f32_0);

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

        if (op->rw.mode == SWS_RW_PACKED) {
            int px = spi_OpImageRead(spi, read_vtype,
                                     in_img[ops->plane_src[0]], coord,
                                     SpvImageOperandsMaskNone);
            if (!src_float)
                px = spi_OpConvertUToF(spi, id->f32vec4_type, px);
            px = spi_OpVectorTimesScalar(spi, id->f32vec4_type, px, w);
            acc_v = spi_OpFAdd(spi, id->f32vec4_type, acc_v, px);
        } else {
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
                acc_s[e] = spi_OpFAdd(spi, id->f32_type, acc_s[e], px);
            }
        }
    }

    if (op->rw.mode == SWS_RW_PACKED)
        return acc_v;
    return spi_OpCompositeConstruct(spi, id->f32vec4_type,
                                    acc_s[0], acc_s[1], acc_s[2], acc_s[3]);
}

static int add_ops_spirv(SwsContext *sws, VulkanPriv *p, FFVulkanOpsCtx *s,
                         const SwsOpList *ops, FFVulkanShader *shd)
{
    uint8_t spvbuf[1024*16];
    SPICtx spi_context = { 0 }, *spi = &spi_context;
    SPIRVIDs spid_data = { 0 }, *id = &spid_data;
    spi_init(spi, spvbuf, sizeof(spvbuf));

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
    int out_img_count = ff_sws_rw_op_planes(op_w);
    p->dst_rep = op_w->type == SWS_PIXEL_F32 ? FF_VK_REP_FLOAT : FF_VK_REP_UINT;

    const SwsOp *op_r = ff_sws_op_list_input(ops);
    int in_img_count = op_r ? ff_sws_rw_op_planes(op_r) : 0;
    if (op_r)
        p->src_rep = op_r->type == SWS_PIXEL_F32 ? FF_VK_REP_FLOAT : FF_VK_REP_UINT;

    FFVulkanDescriptorSetBinding desc_set[MAX_DATA_BUFS] = {
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
    ff_vk_shader_add_descriptor_set(&s->vkctx, shd, desc_set, 2, 0, 0);

    /* Create dither buffers */
    int err = create_bufs(s, p, ops);
    if (err < 0)
        return err;

    /* Entrypoint inputs; gl_GlobalInvocationID, input and output images, dither */
    id->in_vars[0] = spi_get_id(spi);
    id->in_vars[1] = spi_get_id(spi);
    id->in_vars[2] = spi_get_id(spi);

    /* Create dither and filter buffer descriptor set. Both are collected in
     * op order, so the bindings match the buffer order from create_bufs().*/
    id->nb_dither_bufs = 0;
    id->nb_filter_bufs = 0;
    int nb_data_bufs = 0;
    for (int n = 0; n < ops->num_ops; n++) {
        const SwsOp *op = &ops->ops[n];
        int var_id = 0;

        if (op->op == SWS_OP_DITHER) {
            if (id->nb_dither_bufs >= MAX_DITHER_BUFS)
                return AVERROR(ENOTSUP);
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

        id->in_vars[3 + nb_data_bufs] = var_id;
        desc_set[nb_data_bufs++] = (FFVulkanDescriptorSetBinding) {
            .type        = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
        };
    }
    if (nb_data_bufs)
        ff_vk_shader_add_descriptor_set(&s->vkctx, shd, desc_set,
                                        nb_data_bufs, 1, 0);

    if (id->interlaced) {
        id->push_const_struct_id = spi_get_id(spi);
        id->push_const_var_id = spi_get_id(spi);
        id->in_vars[3 + id->nb_dither_bufs + id->nb_filter_bufs] =
            id->push_const_var_id;
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
                                    id->in_vars[1], id->u32_cid[i]);
        in_img[i] = spi_OpLoad(spi, id->in_img_type, img,
                               SpvMemoryAccessMaskNone, 0);
    }

    /* Load output image handles */
    int out_img[4];
    for (int i = 0; i < out_img_count; i++) {
        int img = spi_OpAccessChain(spi, id->out_img_sptr,
                                    id->in_vars[2], id->u32_cid[i]);
        out_img[i] = spi_OpLoad(spi, id->out_img_type, img,
                                SpvMemoryAccessMaskNone, 0);
    }

    /* Load gl_GlobalInvocationID */
    int gid = spi_OpLoad(spi, id->u32vec3_type, id->in_vars[0],
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
    if (ops->ops[0].type == SWS_PIXEL_F32)
        data = spi_OpCompositeConstruct(spi, id->f32vec4_type,
                                        id->f32_p, id->f32_p,
                                        id->f32_p, id->f32_p);
    else
        data = spi_OpCompositeConstruct(spi, id->u32vec4_type,
                                        id->u32_p, id->u32_p,
                                        id->u32_p, id->u32_p);

    /* Keep track of which constant/buffer to use */
    int nb_const_ids = 0;
    int nb_dither_bufs = 0;
    int nb_linear_ops = 0;
    int nb_filter_used = 0;

    /* Operations */
    for (int n = 0; n < ops->num_ops; n++) {
        const SwsOp *op = &ops->ops[n];
        SwsPixelType cur_type = op->op == SWS_OP_CONVERT ?
                                op->convert.to : op->type;
        int type_v = cur_type == SWS_PIXEL_F32 ?
                     id->f32vec4_type : id->u32vec4_type;
        int type_s = cur_type == SWS_PIXEL_F32 ?
                     id->f32_type : id->u32_type;
        int uid = cur_type == SWS_PIXEL_F32 ?
                  id->f32_p : id->u32_p;

        switch (op->op) {
        case SWS_OP_READ:
            if (op->rw.frac || op->rw.filter.op) {
                return AVERROR(ENOTSUP);
            } else if (op->rw.filter.op) {
                data = read_filtered(spi, id, ops, op,
                                     &id->filt[nb_filter_used++],
                                     in_img, gid, gi2);
            } else if (op->rw.mode == SWS_RW_PACKED) {
                data = spi_OpImageRead(spi, type_v, in_img[ops->plane_src[0]],
                                       src_gid, SpvImageOperandsMaskNone);
            } else {
                int tmp[4] = { uid, uid, uid, uid };
                for (int i = 0; i < op->rw.elems; i++) {
                    tmp[i] = spi_OpImageRead(spi, type_v,
                                             in_img[ops->plane_src[i]], src_gid,
                                             SpvImageOperandsMaskNone);
                    tmp[i] = spi_OpCompositeExtract(spi, type_s, tmp[i], 0);
                }
                data = spi_OpCompositeConstruct(spi, type_v,
                                                tmp[0], tmp[1], tmp[2], tmp[3]);
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
                if (!op->clear.value[i].den)
                    continue;
                data = spi_OpCompositeInsert(spi, type_v,
                                             id->const_ids[nb_const_ids++],
                                             data, i);
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
            if (ff_sws_pixel_type_is_int(cur_type) && op->convert.expand)
                data = spi_OpIMul(spi, type_v, data, id->const_ids[nb_const_ids++]);
            else if (op->type == SWS_PIXEL_F32 && type_s == id->u32_type)
                data = spi_OpConvertFToU(spi, type_v, data);
            else if (op->type != SWS_PIXEL_F32 && type_s == id->f32_type)
                data = spi_OpConvertUToF(spi, type_v, data);
            break;
        case SWS_OP_LSHIFT:
            data = spi_OpShiftLeftLogical(spi, type_v, data,
                                          id->const_ids[nb_const_ids++]);
            break;
        case SWS_OP_RSHIFT:
            data = spi_OpShiftRightLogical(spi, type_v, data,
                                           id->const_ids[nb_const_ids++]);
            break;
        case SWS_OP_SCALE:
            if (op->type == SWS_PIXEL_F32)
                data = spi_OpFMul(spi, type_v, data,
                                  id->const_ids[nb_const_ids++]);
            else
                data = spi_OpIMul(spi, type_v, data,
                                  id->const_ids[nb_const_ids++]);
            break;
        case SWS_OP_MIN:
        case SWS_OP_MAX: {
            int t = op->type == SWS_PIXEL_F32 ?
                    op->op == SWS_OP_MIN ? GLSLstd450FMin : GLSLstd450FMax :
                    op->op == SWS_OP_MIN ? GLSLstd450UMin : GLSLstd450UMax;
            for (int i = 0; i < 4; i++) {
                if (!op->clamp.limit[i].den)
                    continue;
                int tmp = spi_OpCompositeExtract(spi, type_s, data, i);
                tmp = spi_OpExtInst(spi, type_s, id->glfn, t,
                                    tmp, id->const_ids[nb_const_ids++]);
                data = spi_OpCompositeInsert(spi, type_v, tmp, data, i);
            }
            break;
        }
        case SWS_OP_DITHER: {
            int did = nb_dither_bufs++;
            int x_id = spi_OpCompositeExtract(spi, id->u32_type, gid, 0);
            int y_pos = spi_OpCompositeExtract(spi, id->u32_type, gid, 1);
            x_id = spi_OpBitwiseAnd(spi, id->u32_type, x_id,
                                    id->dither[did].mask_id);
            for (int i = 0; i < 4; i++) {
                if (op->dither.y_offset[i] < 0)
                    continue;

                int y_id = spi_OpIAdd(spi, id->u32_type, y_pos,
                                      id->const_ids[nb_const_ids++]);
                y_id = spi_OpBitwiseAnd(spi, id->u32_type, y_id,
                                        id->dither[did].mask_id);

                int ptr = spi_OpAccessChain(spi, id->dither_ptr_elem_id,
                                            id->dither[did].id, id->u32_cid[0],
                                            y_id, x_id);
                int val = spi_OpLoad(spi, id->f32_type, ptr,
                                     SpvMemoryAccessMaskNone, 0);

                int tmp = spi_OpCompositeExtract(spi, type_s, data, i);
                tmp = spi_OpFAdd(spi, type_s, tmp, val);
                data = spi_OpCompositeInsert(spi, type_v, tmp, data, i);
            }
            break;
        }
        case SWS_OP_LINEAR: {
            if (sws->flags & SWS_BITEXACT)
                data = insert_bitexact_linear(op, spi, id, data, nb_linear_ops, nb_const_ids);
            else
                data = insert_vmat_linear(op, spi, id, data, nb_const_ids);
            nb_linear_ops++;
            nb_const_ids += 5*5 + 1;
            break;
        }
        case SWS_OP_UNPACK:
            if (ops->src.format == AV_PIX_FMT_X2BGR10)
                data = spi_OpVectorShuffle(spi, type_v, data, data, 3, 2, 1, 0);
            else
                data = spi_OpVectorShuffle(spi, type_v, data, data, 3, 0, 1, 2);
            break;
        case SWS_OP_PACK:
            if (ops->dst.format == AV_PIX_FMT_X2BGR10)
                data = spi_OpVectorShuffle(spi, type_v, data, data, 3, 2, 1, 0);
            else
                data = spi_OpVectorShuffle(spi, type_v, data, data, 1, 2, 3, 0);
            break;
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
#endif

#if CONFIG_LIBSHADERC || CONFIG_LIBGLSLANG
static void add_desc_read_write(FFVulkanDescriptorSetBinding *out_desc,
                                enum FFVkShaderRepFormat *out_rep,
                                const SwsOp *op)
{
    const char *img_type = op->type == SWS_PIXEL_F32 ? "rgba32f"  :
                           op->type == SWS_PIXEL_U32 ? "rgba32ui" :
                           op->type == SWS_PIXEL_U16 ? "rgba16ui" :
                                                       "rgba8ui";

    *out_desc = (FFVulkanDescriptorSetBinding) {
        .name = op->op == SWS_OP_WRITE ? "dst_img" : "src_img",
        .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .mem_layout = img_type,
        .mem_quali = op->op == SWS_OP_WRITE ? "writeonly" : "readonly",
        .dimensions = 2,
        .elems = 4,
        .stages = VK_SHADER_STAGE_COMPUTE_BIT,
    };

    *out_rep = op->type == SWS_PIXEL_F32 ? FF_VK_REP_FLOAT : FF_VK_REP_UINT;
}

#define QSTR "(%i/%i%s)"
#define QTYPE(Q) (Q).num, (Q).den, cur_type == SWS_PIXEL_F32 ? ".0f" : ""

static void read_glsl(const SwsOpList *ops, const SwsOp *op, FFVulkanShader *shd,
                      int idx, const char *type_name,
                      const char *type_v, const char *type_s)
{
    const SwsFilterWeights *wd = op->rw.filter.kernel;
    const int interlaced = ops->src.interlaced;
    if (op->rw.filter.op) {
        const char *axis    = op->rw.filter.op == SWS_OP_FILTER_H ? "pos.x" : "pos.y";
        const char *coord_x = op->rw.filter.op == SWS_OP_FILTER_H ? "o + i" : "pos.x";
        const char *coord_y;
        if (op->rw.filter.op == SWS_OP_FILTER_H)
            coord_y = interlaced ? "spos.y" : "pos.y";
        else
            coord_y = interlaced ? "((o + i) * 2 + int(params.field))" : "o + i";
        GLSLC(1, tmp = vec4(0);                                               );
        av_bprintf(&shd->src, "    int o = filter_o%i[%s];\n", idx, axis);
        av_bprintf(&shd->src, "    for (int i = 0; i < %i; i++) {\n",
                   wd->filter_size);
        av_bprintf(&shd->src, "        float w = filter_w%i[%s][i];\n",
                   idx, axis);
        if (op->rw.mode == SWS_RW_PACKED) {
            GLSLF(2, tmp += w * %s(imageLoad(src_img[%i], ivec2(%s, %s)));     ,
                  type_v, ops->plane_src[0], coord_x, coord_y);
        } else {
            for (int i = 0; i < op->rw.elems; i++)
                GLSLF(2,
                      tmp.%c += w * %s(imageLoad(src_img[%i], ivec2(%s, %s))[0]); ,
                      "xyzw"[i], type_s, ops->plane_src[i], coord_x, coord_y);
        }
        GLSLC(1, }                                                            );
        GLSLC(1, f32 = tmp;                                                   );
    } else {
        const char *src_pos = interlaced ? "spos" : "pos";
        if (op->rw.mode == SWS_RW_PACKED) {
            GLSLF(1, %s = %s(imageLoad(src_img[%i], %s));                      ,
                  type_name, type_v, ops->plane_src[0], src_pos);
        } else {
            for (int i = 0; i < op->rw.elems; i++)
                GLSLF(1, %s.%c = %s(imageLoad(src_img[%i], %s)[0]);            ,
                      type_name, "xyzw"[i], type_s, ops->plane_src[i], src_pos);
        }
    }
}

static int add_ops_glsl(SwsContext *sws, VulkanPriv *p, FFVulkanOpsCtx *s,
                        const SwsOpList *ops, FFVulkanShader *shd)
{
    int err;
    uint8_t *spv_data;
    size_t spv_len;
    void *spv_opaque = NULL;
    const int interlaced = ops->src.interlaced || ops->dst.interlaced;

    err = ff_vk_shader_init(&s->vkctx, shd, "sws_pass",
                            VK_SHADER_STAGE_COMPUTE_BIT,
                            NULL, 0, 32, 32, 1, 0);
    if (err < 0)
        return err;

    p->interlaced = interlaced;
    if (interlaced)
        ff_vk_shader_add_push_const(shd, 0, sizeof(uint32_t),
                                    VK_SHADER_STAGE_COMPUTE_BIT);

    int nb_desc = 0;
    FFVulkanDescriptorSetBinding buf_desc[8];

    const SwsOp *read  = ff_sws_op_list_input(ops);
    const SwsOp *write = ff_sws_op_list_output(ops);
    if (read)
        add_desc_read_write(&buf_desc[nb_desc++], &p->src_rep, read);
    add_desc_read_write(&buf_desc[nb_desc++], &p->dst_rep, write);
    ff_vk_shader_add_descriptor_set(&s->vkctx, shd, buf_desc, nb_desc, 0, 0);

    err = create_bufs(s, p, ops);
    if (err < 0)
        return err;

    nb_desc = 0;
    char data_buf_name[MAX_DATA_BUFS][256];
    char data_str_name[MAX_DATA_BUFS][256];
    for (int n = 0; n < ops->num_ops; n++) {
        const SwsOp *op = &ops->ops[n];
        if (op->op == SWS_OP_DITHER) {
            int size = (1 << op->dither.size_log2);
            av_assert0(size < 8192);
            snprintf(data_buf_name[nb_desc], 256, "dither_buf%i", n);
            snprintf(data_str_name[nb_desc], 256, "float dither_mat%i[%i][%i];",
                     n, size, size);
            buf_desc[nb_desc] = (FFVulkanDescriptorSetBinding) {
                .name        = data_buf_name[nb_desc],
                .type        = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
                .mem_layout  = "scalar",
                .buf_content = data_str_name[nb_desc],
            };
            nb_desc++;
        } else if (op->op == SWS_OP_FILTER_H || op->op == SWS_OP_FILTER_V ||
                   ((op->op == SWS_OP_READ || op->op == SWS_OP_WRITE) &&
                    op->rw.filter.op)) {
            const SwsFilterWeights *wd = (op->op == SWS_OP_READ ||
                                          op->op == SWS_OP_WRITE) ?
                                         op->rw.filter.kernel : op->filter.kernel;
            snprintf(data_buf_name[nb_desc], 256, "filter_buf%i", n);
            snprintf(data_str_name[nb_desc], 256,
                     "float filter_w%i[%i][%i];\n"
                 "    int filter_o%i[%i];",
                     n, wd->dst_size, wd->filter_size,
                     n, wd->dst_size);
            buf_desc[nb_desc] = (FFVulkanDescriptorSetBinding) {
                .name        = data_buf_name[nb_desc],
                .type        = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
                .mem_layout  = "scalar",
                .buf_content = data_str_name[nb_desc],
            };
            nb_desc++;
        }
    }
    if (nb_desc)
        ff_vk_shader_add_descriptor_set(&s->vkctx, shd, buf_desc,
                                        nb_desc, 1, 0);

    if (interlaced) {
        GLSLC(0, layout(push_constant, std430) uniform pushConstants {        );
        GLSLC(1,     uint field;                                              );
        GLSLC(0, } params;                                                    );
        GLSLC(0,                                                              );
    }

    GLSLC(0, void main()                                                      );
    GLSLC(0, {                                                                );
    GLSLC(1,     ivec2 pos = ivec2(gl_GlobalInvocationID.xy);                 );
    GLSLC(1,     ivec2 size = imageSize(dst_img[0]);                          );
    if (ops->src.interlaced)
        GLSLC(1, ivec2 spos = ivec2(pos.x, pos.y * 2 + int(params.field));    );
    if (ops->dst.interlaced)
        GLSLC(1, ivec2 dpos = ivec2(pos.x, pos.y * 2 + int(params.field));    );
    if (ops->dst.interlaced) {
        GLSLC(1, if (any(greaterThanEqual(dpos, size)))                       );
    } else {
        GLSLC(1, if (any(greaterThanEqual(pos, size)))                        );
    }
    GLSLC(2,         return;                                                  );
    GLSLC(0,                                                                  );
    GLSLC(1,     u8vec4 u8;                                                   );
    GLSLC(1,     u16vec4 u16;                                                 );
    GLSLC(1,     u32vec4 u32;                                                 );
    GLSLC(1,     precise f32vec4 f32;                                         );
    GLSLC(1,     precise f32vec4 tmp;                                         );
    GLSLC(0,                                                                  );

    for (int n = 0; n < ops->num_ops; n++) {
        const SwsOp *op = &ops->ops[n];
        SwsPixelType cur_type = op->op == SWS_OP_CONVERT ? op->convert.to :
                                op->type;
        const char *type_name = ff_sws_pixel_type_name(cur_type);
        const char *type_v = cur_type == SWS_PIXEL_F32 ? "f32vec4" :
                             cur_type == SWS_PIXEL_U32 ? "u32vec4" :
                             cur_type == SWS_PIXEL_U16 ? "u16vec4" : "u8vec4";
        const char *type_s = cur_type == SWS_PIXEL_F32 ? "float" :
                             cur_type == SWS_PIXEL_U32 ? "uint32_t" :
                             cur_type == SWS_PIXEL_U16 ? "uint16_t" : "uint8_t";
        av_bprintf(&shd->src, "    // %s\n", ff_sws_op_type_name(op->op));

        switch (op->op) {
        case SWS_OP_READ: {
            if (op->rw.frac)
                return AVERROR(ENOTSUP);
            read_glsl(ops, op, shd, n, type_name, type_v, type_s);
            break;
        }
        case SWS_OP_WRITE: {
            const char *dst_pos = ops->dst.interlaced ? "dpos" : "pos";
            if (op->rw.frac || op->rw.filter.op) {
                return AVERROR(ENOTSUP);
            } else if (op->rw.mode == SWS_RW_PACKED) {
                GLSLF(1, imageStore(dst_img[%i], %s, %s(%s));                   ,
                      ops->plane_dst[0], dst_pos, type_v, type_name);
            } else {
                for (int i = 0; i < op->rw.elems; i++)
                    GLSLF(1, imageStore(dst_img[%i], %s, %s(%s[%i]));           ,
                          ops->plane_dst[i], dst_pos, type_v, type_name, i);
            }
            break;
        }
        case SWS_OP_SWIZZLE: {
            av_bprintf(&shd->src, "    %s = %s.", type_name, type_name);
            for (int i = 0; i < 4; i++)
                av_bprintf(&shd->src, "%c", "xyzw"[op->swizzle.in[i]]);
            av_bprintf(&shd->src, ";\n");
            break;
        }
        case SWS_OP_CLEAR: {
            for (int i = 0; i < 4; i++) {
                if (!SWS_COMP_TEST(op->clear.mask, i))
                    continue;
                av_bprintf(&shd->src, "    %s.%c = %s"QSTR";\n", type_name,
                           "xyzw"[i], type_s, QTYPE(op->clear.value[i]));
            }
            break;
        }
        case SWS_OP_SCALE:
            av_bprintf(&shd->src, "    %s = %s * "QSTR";\n",
                       type_name, type_name, QTYPE(op->scale.factor));
            break;
        case SWS_OP_MIN:
        case SWS_OP_MAX:
            for (int i = 0; i < 4; i++) {
                if (!op->clamp.limit[i].den)
                    continue;
                av_bprintf(&shd->src, "    %s.%c = %s(%s.%c, "QSTR");\n",
                           type_name, "xyzw"[i],
                           op->op == SWS_OP_MIN ? "min" : "max",
                           type_name, "xyzw"[i], QTYPE(op->clamp.limit[i]));
            }
            break;
        case SWS_OP_LSHIFT:
        case SWS_OP_RSHIFT:
            av_bprintf(&shd->src, "    %s %s= %i;\n", type_name,
                       op->op == SWS_OP_LSHIFT ? "<<" : ">>", op->shift.amount);
            break;
        case SWS_OP_CONVERT:
            if (ff_sws_pixel_type_is_int(cur_type) && op->convert.expand) {
                const AVRational sc = ff_sws_pixel_expand(op->type, op->convert.to);
                av_bprintf(&shd->src, "    %s = %s((%s*%i)/%i);\n",
                           type_name, type_v, ff_sws_pixel_type_name(op->type),
                           sc.num, sc.den);
            } else {
                av_bprintf(&shd->src, "    %s = %s(%s);\n",
                           type_name, type_v, ff_sws_pixel_type_name(op->type));
            }
            break;
        case SWS_OP_DITHER: {
            int size = (1 << op->dither.size_log2);
            for (int i = 0; i < 4; i++) {
                if (op->dither.y_offset[i] < 0)
                    continue;
                av_bprintf(&shd->src, "    %s.%c += dither_mat%i[(pos.y + %i) & %i]"
                                                                "[pos.x & %i];\n",
                           type_name, "xyzw"[i], n,
                           op->dither.y_offset[i], size - 1,
                           size - 1);
            }
            break;
        }
        case SWS_OP_LINEAR:
            for (int i = 0; i < 4; i++) {
                if (op->lin.m[i][4].num)
                    av_bprintf(&shd->src, "    tmp.%c = "QSTR";\n", "xyzw"[i],
                               QTYPE(op->lin.m[i][4]));
                else
                    av_bprintf(&shd->src, "    tmp.%c = 0;\n", "xyzw"[i]);
                for (int j = 0; j < 4; j++) {
                    if (!op->lin.m[i][j].num)
                        continue;
                    av_bprintf(&shd->src, "    tmp.%c += f32.%c*"QSTR";\n",
                               "xyzw"[i], "xyzw"[j], QTYPE(op->lin.m[i][j]));
                }
            }
            av_bprintf(&shd->src, "    f32 = tmp;\n");
            break;
        case SWS_OP_UNPACK:
            /* MSB->LSB indexing */
            av_bprintf(&shd->src, "    %s = %s.%s;\n", type_name, type_name,
                       ops->src.format == AV_PIX_FMT_X2BGR10 ? "wzyx" : "wxyz");
            break;
        case SWS_OP_PACK:
            /* LSB->MSB indexing */
            av_bprintf(&shd->src, "    %s = %s.%s;\n", type_name, type_name,
                       ops->dst.format == AV_PIX_FMT_X2BGR10 ? "wzyx" : "yzwx");
            break;
        default:
            return AVERROR(ENOTSUP);
        }
    }

    GLSLC(0, }                                                                );

    err = s->spvc->compile_shader(&s->vkctx, s->spvc, shd,
                                  &spv_data, &spv_len, "main",
                                  &spv_opaque);
    if (err < 0)
        return err;

    err = ff_vk_shader_link(&s->vkctx, shd, spv_data, spv_len, "main");

    if (spv_opaque)
        s->spvc->free_shader(s->spvc, &spv_opaque);

    if (err < 0)
        return err;

    return 0;
}
#endif

static int compile(SwsContext *sws, const SwsOpList *ops, SwsCompiledOp *out,
                   int glsl)
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

    if (ops->src.format == AV_PIX_FMT_BGR0 ||
        ops->src.format == AV_PIX_FMT_BGRA ||
        ops->dst.format == AV_PIX_FMT_BGR0 ||
        ops->dst.format == AV_PIX_FMT_BGRA) {
        VkFormatProperties2 prop = {
            .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2,
        };
        FFVulkanFunctions *vk = &s->vkctx.vkfn;
        vk->GetPhysicalDeviceFormatProperties2(s->vkctx.hwctx->phys_dev,
                                               VK_FORMAT_B8G8R8A8_UNORM,
                                               &prop);
        if (!(prop.formatProperties.optimalTilingFeatures &
              VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT_BIT)) {
            err = AVERROR(ENOTSUP);
            goto fail;
        }
    }

    if (glsl) {
        err = AVERROR(ENOTSUP);
#if CONFIG_LIBSHADERC || CONFIG_LIBGLSLANG
        err = add_ops_glsl(sws, p, s, ops, &p->shd);
#endif
    } else {
        err = AVERROR(ENOTSUP);
#if HAVE_SPIRV_HEADERS_SPIRV_H || HAVE_SPIRV_UNIFIED1_SPIRV_H
        err = add_ops_spirv(sws, p, s, ops, &p->shd);
#endif
    }
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
        .func_opaque = process,
        .priv        = p,
        .free        = free_fn,
    };

    return 0;

fail:
    free_fn(p);
    return err;
}

#if HAVE_SPIRV_HEADERS_SPIRV_H || HAVE_SPIRV_UNIFIED1_SPIRV_H
static int compile_spirv(SwsContext *sws, const SwsOpList *ops,
                         SwsCompiledOp *out)
{
    return compile(sws, ops, out, 0);
}

const SwsOpBackend backend_spirv = {
    .name      = "spirv",
    .flags     = SWS_BACKEND_SPIRV,
    .compile   = compile_spirv,
    .hw_format = AV_PIX_FMT_VULKAN,
};
#endif

#if CONFIG_LIBSHADERC || CONFIG_LIBGLSLANG
static int compile_glsl(SwsContext *sws, const SwsOpList *ops,
                        SwsCompiledOp *out)
{
    return compile(sws, ops, out, 1);
}

const SwsOpBackend backend_glsl = {
    .name      = "glsl",
    .flags     = SWS_BACKEND_GLSL,
    .compile   = compile_glsl,
    .hw_format = AV_PIX_FMT_VULKAN,
};
#endif
