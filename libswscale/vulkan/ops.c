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
#include "../uops.h"

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

static int create_dither_buf(FFVulkanOpsCtx *s, const SwsUOp *uop,
                             FFVkBuffer *buf)
{
    int err;

    const int size = 1 << uop->par.dither.size_log2;
    const int rows = ff_sws_dither_height(&uop->par.dither);
    err = ff_vk_create_buf(&s->vkctx, buf,
                           rows*size*sizeof(float), NULL, NULL,
                           VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    if (err < 0)
        return err;

    float *dither_data;
    err = ff_vk_map_buffer(&s->vkctx, buf, (uint8_t **)&dither_data, 0);
    if (err < 0)
        goto fail;

    for (int i = 0; i < rows*size; i++)
        dither_data[i] = uop->data.ptr[i].f32;

    ff_vk_unmap_buffer(&s->vkctx, buf, 1);

    return 0;

fail:
    ff_vk_free_buf(&s->vkctx, buf);
    return err;
}

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
    int f32_0;
    int u32_cid[5];

    int dither_ptr_elem_id;

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

    VulkanPriv *priv;
    FFVulkanDescriptorSetBinding buf_desc[MAX_DATA_BUFS]; /* set 1 */
    const SwsOpList *ops;
    int bitexact;

    /* Per-invocation state of the main function */
    int in_img[4], out_img[4];  /* image handles, indexed by plane */
    int gid, gi2;               /* invocation position, as uvec2 and ivec2 */
    int src_gid, dst_gid;       /* image position to read from / write to */
} SPIRVIDs;

/* All integer pixel types are represented as 32-bit unsigned integers */
static int type_scalar(const SPIRVIDs *id, SwsPixelType type)
{
    av_assert1(type != SWS_PIXEL_NONE);
    return ff_sws_pixel_type_is_int(type) ? id->u32_type : id->f32_type;
}

static int type_vec4(const SPIRVIDs *id, SwsPixelType type)
{
    av_assert1(type != SWS_PIXEL_NONE);
    return ff_sws_pixel_type_is_int(type) ? id->u32vec4_type : id->f32vec4_type;
}

static int const_zero(const SPIRVIDs *id, SwsPixelType type)
{
    av_assert1(type != SWS_PIXEL_NONE);
    return ff_sws_pixel_type_is_int(type) ? id->u32_cid[0] : id->f32_0;
}

static int const_pixel(SPICtx *spi, const SPIRVIDs *id, SwsPixelType type,
                       SwsPixel px)
{
    switch (type) {
    case SWS_PIXEL_U8:  return spi_OpConstantUInt(spi, id->u32_type, px.u8);
    case SWS_PIXEL_U16: return spi_OpConstantUInt(spi, id->u32_type, px.u16);
    case SWS_PIXEL_U32: return spi_OpConstantUInt(spi, id->u32_type, px.u32);
    case SWS_PIXEL_F32: return spi_OpConstantFloat(spi, id->f32_type, px.f32);
    default: break;
    }

    av_unreachable("Invalid pixel type!");
    return 0;
}

static int const_splat(SPICtx *spi, const SPIRVIDs *id, SwsPixelType type,
                       SwsPixel px)
{
    const int c = const_pixel(spi, id, type, px);
    return spi_OpConstantComposite(spi, type_vec4(id, type), c, c, c, c);
}

/* Shift and mask vectors for (un)packing each component */
static void define_pack_consts(SPICtx *spi, const SPIRVIDs *id, const SwsUOp *uop,
                               int *shift_id, int *mask_id)
{
    uint64_t mask[4];
    int shift[4], tmp[4];
    ff_sws_pack_decode(uop->par.pack.pattern, mask, shift);
    for (int i = 0; i < 4; i++)
        tmp[i] = spi_OpConstantUInt(spi, id->u32_type, shift[i]);
    *shift_id = spi_OpConstantComposite(spi, id->u32vec4_type,
                                        tmp[0], tmp[1], tmp[2], tmp[3]);
    for (int i = 0; i < 4; i++)
        tmp[i] = spi_OpConstantUInt(spi, id->u32_type, mask[i]);
    *mask_id = spi_OpConstantComposite(spi, id->u32vec4_type,
                                       tmp[0], tmp[1], tmp[2], tmp[3]);
}

/* Arithmetic on operands of the given pixel type */
static int emit_add(SPICtx *spi, SwsPixelType type, int res_type, int a, int b)
{
    return ff_sws_pixel_type_is_int(type) ? spi_OpIAdd(spi, res_type, a, b)
                                          : spi_OpFAdd(spi, res_type, a, b);
}

static int emit_mul(SPICtx *spi, SwsPixelType type, int res_type, int a, int b)
{
    return ff_sws_pixel_type_is_int(type) ? spi_OpIMul(spi, res_type, a, b)
                                          : spi_OpFMul(spi, res_type, a, b);
}

static int emit_min(SPICtx *spi, const SPIRVIDs *id, SwsPixelType type,
                    int res_type, int a, int b)
{
    const int op = ff_sws_pixel_type_is_int(type) ? GLSLstd450UMin : GLSLstd450FMin;
    return spi_OpExtInst(spi, res_type, id->glfn, op, a, b);
}

static int emit_max(SPICtx *spi, const SPIRVIDs *id, SwsPixelType type,
                    int res_type, int a, int b)
{
    const int op = ff_sws_pixel_type_is_int(type) ? GLSLstd450UMax : GLSLstd450FMax;
    return spi_OpExtInst(spi, res_type, id->glfn, op, a, b);
}

/* Converts between float and integer values, if needed */
static int emit_convert(SPICtx *spi, SwsPixelType from, SwsPixelType to,
                        int res_type, int a)
{
    if (ff_sws_pixel_type_is_int(from) == ff_sws_pixel_type_is_int(to))
        return a;
    return ff_sws_pixel_type_is_int(to) ? spi_OpConvertFToU(spi, res_type, a)
                                        : spi_OpConvertUToF(spi, res_type, a);
}

/* Section 1: Function to define all shader header data, and decorations */
static void define_shader_header(SPICtx *spi, SPIRVIDs *id)
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
}

/* Section 2: Define all types and constants */
static void define_shader_consts(SPICtx *spi, SPIRVIDs *id)
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
    id->f32_0 = spi_OpConstantFloat(spi, f32_type, 0);
    for (int i = 0; i < 5; i++)
        id->u32_cid[i] = spi_OpConstantUInt(spi, u32_type, i);
}

/* Section 3: Define bindings */
static void define_shader_bindings(const SwsOpList *ops, SPICtx *spi, SPIRVIDs *id,
                                   int in_img_count, int out_img_count)
{
    id->dither_ptr_elem_id = spi_OpTypePointer(spi, SpvStorageClassUniform,
                                               id->f32_type);

    const SwsOp *op_w = ff_sws_op_list_output(ops);
    const SwsOp *op_r = ff_sws_op_list_input(ops);

    /* Define image types for descriptors */
    id->out_img_type = spi_OpTypeImage(spi, type_scalar(id, op_w->rw.type),
                                       2, 0, 0, 0, 2, SpvImageFormatUnknown);
    id->out_img_array_id = spi_OpTypeArray(spi, id->out_img_type, spi_get_id(spi),
                                           id->u32_cid[out_img_count]);

    id->in_img_type = 0;
    id->in_img_array_id = 0;
    if (op_r) {
        /* If the formats match, we have to reuse the types due to SPIR-V not
         * allowing redundant type defines */
        int match = type_scalar(id, op_w->rw.type) ==
                    type_scalar(id, op_r->rw.type);
        id->in_img_type = match ? id->out_img_type :
                          spi_OpTypeImage(spi, type_scalar(id, op_r->rw.type),
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
    id->gid_var = spi_OpVariable(spi, spi_get_id(spi), id->u32vec3_tptr,
                                 SpvStorageClassInput, 0);
    id->iface[id->nb_iface++] = id->gid_var;
    spi_OpDecorate(spi, id->gid_var, SpvDecorationBuiltIn,
                   SpvBuiltInGlobalInvocationId);

    if (op_r) {
        id->in_img_var = spi_OpVariable(spi, spi_get_id(spi), id->in_img_tptr,
                                        SpvStorageClassUniformConstant, 0);
        id->iface[id->nb_iface++] = id->in_img_var;
        spi_OpDecorate(spi, id->in_img_var, SpvDecorationNonWritable);
        spi_OpDecorate(spi, id->in_img_var, SpvDecorationDescriptorSet, 0);
        spi_OpDecorate(spi, id->in_img_var, SpvDecorationBinding, 0);
    }

    id->out_img_var = spi_OpVariable(spi, spi_get_id(spi), id->out_img_tptr,
                                     SpvStorageClassUniformConstant, 0);
    id->iface[id->nb_iface++] = id->out_img_var;
    spi_OpDecorate(spi, id->out_img_var, SpvDecorationNonReadable);
    spi_OpDecorate(spi, id->out_img_var, SpvDecorationDescriptorSet, 0);
    spi_OpDecorate(spi, id->out_img_var, SpvDecorationBinding, 1);

    if (id->interlaced) {
        id->push_const_struct_id = spi_get_id(spi);
        spi_OpTypeStruct(spi, id->push_const_struct_id, id->u32_type);
        spi_OpDecorate(spi, id->push_const_struct_id, SpvDecorationBlock);
        spi_OpMemberDecorate(spi, id->push_const_struct_id, 0,
                             SpvDecorationOffset, 0);
        id->push_const_ptr_id = spi_OpTypePointer(spi, SpvStorageClassPushConstant,
                                                  id->push_const_struct_id);
        id->push_const_elem_ptr_id = spi_OpTypePointer(spi, SpvStorageClassPushConstant,
                                                       id->u32_type);
        id->push_const_var_id = spi_OpVariable(spi, spi_get_id(spi),
                                               id->push_const_ptr_id,
                                               SpvStorageClassPushConstant, 0);
        id->iface[id->nb_iface++] = id->push_const_var_id;
    }
}

/* Must be called after all interface variables have been defined */
static void define_entry_point(FFVulkanShader *shd, SPICtx *spi, SPIRVIDs *id)
{
    spi_OpEntryPoint(spi, SpvExecutionModelGLCompute, id->ep, "main",
                     id->iface, id->nb_iface);
    spi_OpExecutionMode(spi, id->ep, SpvExecutionModeLocalSize,
                        shd->lg_size, 3);
}

static int insert_vmat_linear(const SwsUOp *uop, SPICtx *spi, SPIRVIDs *id,
                              int data)
{
    const SwsPixel (*m)[5] = uop->data.mat4x5;
    int col[4], tmp[4];
    for (int j = 0; j < 4; j++) {
        for (int i = 0; i < 4; i++)
            tmp[i] = const_pixel(spi, id, SWS_PIXEL_F32, m[i][j]);
        col[j] = spi_OpConstantComposite(spi, id->f32vec4_type,
                                         tmp[0], tmp[1], tmp[2], tmp[3]);
    }
    int mat = spi_OpConstantComposite(spi, id->f32mat4_type,
                                      col[0], col[1], col[2], col[3]);

    for (int i = 0; i < 4; i++)
        tmp[i] = const_pixel(spi, id, SWS_PIXEL_F32, m[i][4]);
    int off = spi_OpConstantComposite(spi, id->f32vec4_type,
                                      tmp[0], tmp[1], tmp[2], tmp[3]);

    data = spi_OpMatrixTimesVector(spi, id->f32vec4_type, mat, data);
    return spi_OpFAdd(spi, id->f32vec4_type, off, data);
}

static int insert_bitexact_linear(const SwsUOp *uop, SPICtx *spi, SPIRVIDs *id,
                                  int data)
{
    const SwsPixelType op_type = uop->type;
    const SwsPixel (*m)[5] = uop->data.mat4x5;
    const uint32_t zero = uop->par.lin.zero;
    int type_s = type_scalar(id, op_type);
    int type_v = type_vec4(id, op_type);

    int tmp[4];
    tmp[0] = spi_OpCompositeExtract(spi, type_s, data, 0);
    tmp[1] = spi_OpCompositeExtract(spi, type_s, data, 1);
    tmp[2] = spi_OpCompositeExtract(spi, type_s, data, 2);
    tmp[3] = spi_OpCompositeExtract(spi, type_s, data, 3);

    /* Same evaluation order as the C backend: offset first, then each column
     * from left to right, skipping zero coefficients */
    int res[4];
    for (int j = 0; j < 4; j++) {
        if (!SWS_COMP_TEST(uop->mask, j)) {
            res[j] = tmp[j];
            continue;
        }

        bool acc = !(zero & SWS_MASK_OFF(j));
        res[j] = acc ? const_pixel(spi, id, op_type, m[j][4])
                     : const_zero(id, op_type);
        for (int i = 0; i < 4; i++) {
            if (zero & SWS_MASK(j, i))
                continue;

            int k = const_pixel(spi, id, op_type, m[j][i]);
            int v = emit_mul(spi, op_type, type_s, tmp[i], k);
            spi_OpDecorate(spi, v, SpvDecorationNoContraction);
            if (acc) {
                v = emit_add(spi, op_type, type_s, res[j], v);
                spi_OpDecorate(spi, v, SpvDecorationNoContraction);
            }
            res[j] = v;
            acc = true;
        }
    }

    int result = spi_OpCompositeConstruct(spi, type_v,
                                          res[0], res[1], res[2], res[3]);

    /* Integer results are defined modulo the bit depth of the pixel type */
    if (op_type == SWS_PIXEL_U8 || op_type == SWS_PIXEL_U16) {
        const unsigned mask = op_type == SWS_PIXEL_U8 ? UINT8_MAX : UINT16_MAX;
        const int mask_v = const_splat(spi, id, SWS_PIXEL_U32, (SwsPixel) { .u32 = mask });
        result = spi_OpBitwiseAnd(spi, type_v, result, mask_v);
    }
    return result;
}

/* Registers the last created buffer in descriptor set 1, returns its binding */
static int add_buf_binding(SPIRVIDs *id)
{
    VulkanPriv *p = id->priv;
    id->buf_desc[p->nb_data_bufs] = (FFVulkanDescriptorSetBinding) {
        .type   = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .stages = VK_SHADER_STAGE_COMPUTE_BIT,
    };
    return p->nb_data_bufs++;
}

static int emit_read_filtered(SPICtx *spi, SPIRVIDs *id, const SwsUOp *uop,
                              int *data)
{
    const SwsOpList *ops = id->ops;
    const SwsFilterWeights *wd = uop->data.kernel;
    VulkanPriv *p = id->priv;
    if (p->nb_data_bufs >= MAX_DATA_BUFS)
        return AVERROR(ENOTSUP);

    int err = create_filter_buf(p->s, wd, &p->data_bufs[p->nb_data_bufs]);
    if (err < 0)
        return err;
    const int binding = add_buf_binding(id);

    /* Uniform buffer in descriptor set 1:
     * struct { float w[dst_size][filter_size]; int o[dst_size]; } */
    const int fs_id = spi_OpConstantUInt(spi, id->u32_type, wd->filter_size);
    const int ds_id = spi_OpConstantUInt(spi, id->u32_type, wd->dst_size);

    int arr_w_in = spi_OpTypeArray(spi, id->f32_type, spi_get_id(spi), fs_id);
    spi_OpDecorate(spi, arr_w_in, SpvDecorationArrayStride, sizeof(float));
    int arr_w_out = spi_OpTypeArray(spi, arr_w_in, spi_get_id(spi), ds_id);
    spi_OpDecorate(spi, arr_w_out, SpvDecorationArrayStride,
                   wd->filter_size*sizeof(float));
    int arr_o = spi_OpTypeArray(spi, id->i32_type, spi_get_id(spi), ds_id);
    spi_OpDecorate(spi, arr_o, SpvDecorationArrayStride, sizeof(int32_t));

    int struct_id = spi_get_id(spi);
    spi_OpTypeStruct(spi, struct_id, arr_w_out, arr_o);
    spi_OpDecorate(spi, struct_id, SpvDecorationBlock);
    spi_OpMemberDecorate(spi, struct_id, 0, SpvDecorationOffset, 0);
    spi_OpMemberDecorate(spi, struct_id, 1, SpvDecorationOffset,
                         wd->num_weights*sizeof(float));

    int ptr = spi_OpTypePointer(spi, SpvStorageClassUniform, struct_id);
    int buf = spi_OpVariable(spi, spi_get_id(spi), ptr, SpvStorageClassUniform, 0);
    spi_OpDecorate(spi, buf, SpvDecorationDescriptorSet, 1);
    spi_OpDecorate(spi, buf, SpvDecorationBinding, binding);
    id->iface[id->nb_iface++] = buf;

    const int o_ptr_type = spi_OpTypePointer(spi, SpvStorageClassUniform,
                                             id->i32_type);
    const int is_h = uop->uop == SWS_UOP_READ_PLANAR_FH;
    const int src_interlaced = ops->src.interlaced;

    const int read_vtype = type_vec4(id, uop->type);

    /* Buffer array index along the filtered axis: pos.x (H) or pos.y (V) */
    int axis = spi_OpCompositeExtract(spi, id->u32_type, id->gid, is_h ? 0 : 1);

    /* int o = filter_o[axis]; */
    int o_ptr = spi_OpAccessChain(spi, o_ptr_type, buf, id->u32_cid[1], axis);
    int o = spi_OpLoad(spi, id->i32_type, o_ptr, SpvMemoryAccessMaskNone, 0);

    /* Signed pixel position, for the non-filtered coordinate axis */
    int pos_x = spi_OpCompositeExtract(spi, id->i32_type, id->gi2, 0);
    int pos_y = spi_OpCompositeExtract(spi, id->i32_type, id->gi2, 1);

    /* For interlaced horizontal filtering, the y coordinate of every tap is
     * the (constant) destination y mapped into the source image. */
    if (src_interlaced && is_h) {
        pos_y = spi_OpShiftLeftLogical(spi, id->i32_type, pos_y, id->u32_cid[1]);
        pos_y = spi_OpIAdd(spi, id->i32_type, pos_y, id->field_i32);
    }

    /* Accumulators, initialized to zero */
    int acc[4] = { id->f32_0, id->f32_0, id->f32_0, id->f32_0 };

    for (int t = 0; t < wd->filter_size; t++) {
        const int tap = spi_OpConstantInt(spi, id->i32_type, t);

        /* float w = filter_w[axis][t]; */
        int w_ptr = spi_OpAccessChain(spi, id->dither_ptr_elem_id, buf,
                                      id->u32_cid[0], axis, tap);
        int w = spi_OpLoad(spi, id->f32_type, w_ptr,
                           SpvMemoryAccessMaskNone, 0);

        /* Source coordinate, filtered axis offset by the tap index */
        int c = t ? spi_OpIAdd(spi, id->i32_type, o, tap) : o;
        /* For interlaced vertical filtering, the per-tap source row is
         * field-local; map it to the actual image row. */
        if (src_interlaced && !is_h) {
            c = spi_OpShiftLeftLogical(spi, id->i32_type, c, id->u32_cid[1]);
            c = spi_OpIAdd(spi, id->i32_type, c, id->field_i32);
        }
        int coord = is_h ?
            spi_OpCompositeConstruct(spi, id->i32vec2_type, c, pos_y) :
            spi_OpCompositeConstruct(spi, id->i32vec2_type, pos_x, c);

        for (int e = 0; e < 4; e++) {
            if (!SWS_COMP_TEST(uop->mask, e))
                continue;
            int px = spi_OpImageRead(spi, read_vtype,
                                     id->in_img[ops->plane_src[e]], coord,
                                     SpvImageOperandsMaskNone);
            px = spi_OpCompositeExtract(spi, type_scalar(id, uop->type), px, 0);
            px = emit_convert(spi, uop->type, SWS_PIXEL_F32, id->f32_type, px);
            px = spi_OpFMul(spi, id->f32_type, w, px);
            acc[e] = spi_OpFAdd(spi, id->f32_type, acc[e], px);
        }
    }

    *data = spi_OpCompositeConstruct(spi, id->f32vec4_type,
                                     acc[0], acc[1], acc[2], acc[3]);
    return 0;
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
        rep = ff_sws_pixel_type_is_int(op->rw.type) ? FF_VK_REP_UINT
                                                    : FF_VK_REP_FLOAT;
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

static int emit_read(SPICtx *spi, SPIRVIDs *id, const SwsUOp *uop, int *data)
{
    const SwsOpList *ops = id->ops;
    const int type_v = type_vec4(id, uop->type);
    const int type_s = type_scalar(id, uop->type);
    const int zero = const_zero(id, uop->type);

    switch (uop->uop) {
    case SWS_UOP_READ_PLANAR_FH:
    case SWS_UOP_READ_PLANAR_FV:
        if (uop->par.filter.type != SWS_PIXEL_F32)
            return AVERROR(ENOTSUP);
        return emit_read_filtered(spi, id, uop, data);
    case SWS_UOP_READ_PACKED:
        *data = spi_OpImageRead(spi, type_v, id->in_img[ops->plane_src[0]],
                                id->src_gid, SpvImageOperandsMaskNone);
        break;
    case SWS_UOP_READ_PLANAR: {
        int tmp[4] = { zero, zero, zero, zero };
        for (int i = 0; i < 4; i++) {
            if (!SWS_COMP_TEST(uop->mask, i))
                continue;
            tmp[i] = spi_OpImageRead(spi, type_v,
                                     id->in_img[ops->plane_src[i]], id->src_gid,
                                     SpvImageOperandsMaskNone);
            tmp[i] = spi_OpCompositeExtract(spi, type_s, tmp[i], 0);
        }
        *data = spi_OpCompositeConstruct(spi, type_v,
                                         tmp[0], tmp[1], tmp[2], tmp[3]);
        break;
    }
    default:
        return AVERROR(ENOTSUP);
    }

    return 0;
}

static int emit_write(SPICtx *spi, SPIRVIDs *id, const SwsUOp *uop, int *data)
{
    const SwsOpList *ops = id->ops;
    const int type_v = type_vec4(id, uop->type);
    const int type_s = type_scalar(id, uop->type);

    switch (uop->uop) {
    case SWS_UOP_WRITE_PACKED:
        spi_OpImageWrite(spi, id->out_img[ops->plane_dst[0]], id->dst_gid, *data,
                         SpvImageOperandsMaskNone);
        break;
    case SWS_UOP_WRITE_PLANAR:
        for (int i = 0; i < 4; i++) {
            if (!SWS_COMP_TEST(uop->mask, i))
                continue;
            int tmp = spi_OpCompositeExtract(spi, type_s, *data, i);
            tmp = spi_OpCompositeConstruct(spi, type_v, tmp, tmp, tmp, tmp);
            spi_OpImageWrite(spi, id->out_img[ops->plane_dst[i]], id->dst_gid, tmp,
                             SpvImageOperandsMaskNone);
        }
        break;
    default:
        return AVERROR(ENOTSUP);
    }

    return 0;
}

static int emit_clamp(SPICtx *spi, SPIRVIDs *id, const SwsUOp *uop, int *data)
{
    const SwsPixelType type = uop->type;
    const int type_v = type_vec4(id, type);
    const int type_s = type_scalar(id, type);

    for (int i = 0; i < 4; i++) {
        if (!SWS_COMP_TEST(uop->mask, i))
            continue;
        int lim = const_pixel(spi, id, type, uop->data.vec4[i]);
        int tmp = spi_OpCompositeExtract(spi, type_s, *data, i);
        if (uop->uop == SWS_UOP_MIN)
            tmp = emit_min(spi, id, type, type_s, tmp, lim);
        else
            tmp = emit_max(spi, id, type, type_s, tmp, lim);
        *data = spi_OpCompositeInsert(spi, type_v, tmp, *data, i);
    }

    return 0;
}

static int emit_dither(SPICtx *spi, SPIRVIDs *id, const SwsUOp *uop, int *data)
{
    if (uop->type != SWS_PIXEL_F32)
        return AVERROR(ENOTSUP);

    VulkanPriv *p = id->priv;
    if (p->nb_data_bufs >= MAX_DATA_BUFS)
        return AVERROR(ENOTSUP);

    int err = create_dither_buf(p->s, uop, &p->data_bufs[p->nb_data_bufs]);
    if (err < 0)
        return err;
    const int binding = add_buf_binding(id);

    /* Uniform buffer in descriptor set 1: struct { float m[rows][size]; } */
    const int size = 1 << uop->par.dither.size_log2;
    const int rows = ff_sws_dither_height(&uop->par.dither);
    const int size_id = spi_OpConstantUInt(spi, id->u32_type, size);
    const int rows_id = spi_OpConstantUInt(spi, id->u32_type, rows);
    const int mask_id = spi_OpConstantUInt(spi, id->u32_type, size - 1);

    int arr_1d = spi_OpTypeArray(spi, id->f32_type, spi_get_id(spi), size_id);
    spi_OpDecorate(spi, arr_1d, SpvDecorationArrayStride, sizeof(float));
    int arr_2d = spi_OpTypeArray(spi, arr_1d, spi_get_id(spi), rows_id);
    spi_OpDecorate(spi, arr_2d, SpvDecorationArrayStride, size*sizeof(float));

    int struct_id = spi_get_id(spi);
    spi_OpTypeStruct(spi, struct_id, arr_2d);
    spi_OpDecorate(spi, struct_id, SpvDecorationBlock);
    spi_OpMemberDecorate(spi, struct_id, 0, SpvDecorationOffset, 0);

    int ptr = spi_OpTypePointer(spi, SpvStorageClassUniform, struct_id);
    int buf = spi_OpVariable(spi, spi_get_id(spi), ptr, SpvStorageClassUniform, 0);
    spi_OpDecorate(spi, buf, SpvDecorationDescriptorSet, 1);
    spi_OpDecorate(spi, buf, SpvDecorationBinding, binding);
    id->iface[id->nb_iface++] = buf;

    /* The matrix is padded to allow offsetting the wrapped row */
    int x_id = spi_OpCompositeExtract(spi, id->u32_type, id->gid, 0);
    int y_id = spi_OpCompositeExtract(spi, id->u32_type, id->gid, 1);
    x_id = spi_OpBitwiseAnd(spi, id->u32_type, x_id, mask_id);
    y_id = spi_OpBitwiseAnd(spi, id->u32_type, y_id, mask_id);
    for (int i = 0; i < 4; i++) {
        if (!SWS_COMP_TEST(uop->mask, i))
            continue;

        int y_off = spi_OpConstantUInt(spi, id->u32_type,
                                       uop->par.dither.y_offset[i]);
        int row = spi_OpIAdd(spi, id->u32_type, y_id, y_off);

        int val_ptr = spi_OpAccessChain(spi, id->dither_ptr_elem_id, buf,
                                        id->u32_cid[0], row, x_id);
        int val = spi_OpLoad(spi, id->f32_type, val_ptr,
                             SpvMemoryAccessMaskNone, 0);

        int tmp = spi_OpCompositeExtract(spi, id->f32_type, *data, i);
        tmp = spi_OpFAdd(spi, id->f32_type, tmp, val);
        *data = spi_OpCompositeInsert(spi, id->f32vec4_type, tmp, *data, i);
    }

    return 0;
}

static int emit_linear(SPICtx *spi, SPIRVIDs *id, const SwsUOp *uop, int *data)
{
    /* Matrix operations are only defined for floats */
    if (id->bitexact || ff_sws_pixel_type_is_int(uop->type))
        *data = insert_bitexact_linear(uop, spi, id, *data);
    else
        *data = insert_vmat_linear(uop, spi, id, *data);
    return 0;
}

static int emit_pack(SPICtx *spi, SPIRVIDs *id, const SwsUOp *uop, int *data)
{
    const int type_v = type_vec4(id, uop->type);
    const int type_s = type_scalar(id, uop->type);

    int shift, mask;
    define_pack_consts(spi, id, uop, &shift, &mask);
    *data = spi_OpBitwiseAnd(spi, type_v, *data, mask);
    *data = spi_OpShiftLeftLogical(spi, type_v, *data, shift);
    int val = spi_OpCompositeExtract(spi, type_s, *data, 0);
    for (int i = 1; i < 4; i++) {
        int tmp = spi_OpCompositeExtract(spi, type_s, *data, i);
        val = spi_OpBitwiseOr(spi, type_s, val, tmp);
    }
    *data = spi_OpCompositeConstruct(spi, type_v, val, val, val, val);
    return 0;
}

static int emit_shader(SwsContext *sws, VulkanPriv *p, FFVulkanOpsCtx *s,
                       const SwsOpList *ops, const SwsUOpList *uops,
                       FFVulkanShader *shd, SPICtx *spi)
{
    SPIRVIDs spid_data = { 0 }, *id = &spid_data;
    id->priv = p;
    id->ops = ops;
    id->bitexact = !!(sws->flags & SWS_BITEXACT);

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

    /* Define shader header sections */
    define_shader_header(spi, id);
    define_shader_consts(spi, id);
    define_shader_bindings(ops, spi, id, in_img_count, out_img_count);

    /* Main function starts here */
    id->ep = spi_get_id(spi);
    spi_OpFunction(spi, id->ep, id->void_type, 0, id->void_fn_type);
    spi_OpLabel(spi, spi_get_id(spi));

    /* Load input image handles */
    for (int i = 0; i < in_img_count; i++) {
        /* Deref array and then the pointer */
        int img = spi_OpAccessChain(spi, id->in_img_sptr,
                                    id->in_img_var, id->u32_cid[i]);
        id->in_img[i] = spi_OpLoad(spi, id->in_img_type, img,
                               SpvMemoryAccessMaskNone, 0);
    }

    /* Load output image handles */
    for (int i = 0; i < out_img_count; i++) {
        int img = spi_OpAccessChain(spi, id->out_img_sptr,
                                    id->out_img_var, id->u32_cid[i]);
        id->out_img[i] = spi_OpLoad(spi, id->out_img_type, img,
                                SpvMemoryAccessMaskNone, 0);
    }

    /* Load gl_GlobalInvocationID */
    id->gid = spi_OpLoad(spi, id->u32vec3_type, id->gid_var,
                         SpvMemoryAccessMaskNone, 0);

    /* ivec2(gl_GlobalInvocationID.xy) */
    id->gid = spi_OpVectorShuffle(spi, id->u32vec2_type, id->gid, id->gid, 0, 1);
    id->gi2 = spi_OpBitcast(spi, id->i32vec2_type, id->gid);

    /* For interlaced sources/destinations the shader operates on field-local
     * coordinates, while images contain the full frame. Map the y axis to the
     * actual image row: image_y = field_y * 2 + field. */
    int dst_gi2 = id->gi2;
    id->dst_gid = id->src_gid = id->gid;
    if (id->interlaced) {
        int field_u32_ptr = spi_OpAccessChain(spi, id->push_const_elem_ptr_id,
                                              id->push_const_var_id,
                                              id->u32_cid[0]);
        int field_u32 = spi_OpLoad(spi, id->u32_type, field_u32_ptr,
                                   SpvMemoryAccessMaskNone, 0);
        id->field_i32 = spi_OpBitcast(spi, id->i32_type, field_u32);

        int img_y_i32 = spi_OpShiftLeftLogical(spi, id->i32_type,
                                               spi_OpCompositeExtract(spi, id->i32_type, id->gi2, 1),
                                               id->u32_cid[1]);
        img_y_i32 = spi_OpIAdd(spi, id->i32_type, img_y_i32, id->field_i32);

        int gi2_x = spi_OpCompositeExtract(spi, id->i32_type, id->gi2, 0);
        int mapped_gi2 = spi_OpCompositeConstruct(spi, id->i32vec2_type,
                                                  gi2_x, img_y_i32);
        int mapped_gid = spi_OpBitcast(spi, id->u32vec2_type, mapped_gi2);

        if (ops->src.interlaced)
            id->src_gid = mapped_gid;
        if (ops->dst.interlaced) {
            id->dst_gid = mapped_gid;
            dst_gi2 = mapped_gi2;
        }
    }

    /* imageSize(id->out_img[0]); */
    int img1_s = spi_OpImageQuerySize(spi, id->i32vec2_type, id->out_img[0]);
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
    const SwsPixelType type0 = uops->ops[0].type;
    const int zero0 = const_zero(id, type0);
    int data = spi_OpCompositeConstruct(spi, type_vec4(id, type0),
                                        zero0, zero0, zero0, zero0);

    /* Operations */
    for (int n = 0; n < uops->num_ops; n++) {
        const SwsUOp *uop = &uops->ops[n];
        const int type_v = type_vec4(id, uop->type);

        switch (uop->uop) {
        case SWS_UOP_READ_PLANAR:
        case SWS_UOP_READ_PLANAR_FH:
        case SWS_UOP_READ_PLANAR_FV:
        case SWS_UOP_READ_PACKED:
            err = emit_read(spi, id, uop, &data);
            break;
        case SWS_UOP_WRITE_PLANAR:
        case SWS_UOP_WRITE_PACKED:
            err = emit_write(spi, id, uop, &data);
            break;
        case SWS_UOP_CLEAR:
            for (int i = 0; i < 4; i++) {
                if (!SWS_COMP_TEST(uop->mask, i))
                    continue;
                int val = const_pixel(spi, id, uop->type, uop->data.vec4[i]);
                data = spi_OpCompositeInsert(spi, type_v, val, data, i);
            }
            break;
        case SWS_UOP_PERMUTE:
        case SWS_UOP_COPY: {
            const SwsMoveUOp *move = &uop->par.move;
            int idx[5] = { -1, 0, 1, 2, 3 }; /* tmp, x, y, z, w */
            for (int i = 0; i < move->num_moves; i++)
                idx[move->dst[i] + 1] = idx[move->src[i] + 1];

            data = spi_OpVectorShuffle(spi, type_v, data, data,
                                       idx[1], idx[2], idx[3], idx[4]);
            break;
        }
        case SWS_UOP_TO_U8:
        case SWS_UOP_TO_U16:
        case SWS_UOP_TO_U32:
            data = emit_convert(spi, uop->type, SWS_PIXEL_U32,
                                id->u32vec4_type, data);
            break;
        case SWS_UOP_TO_F32:
            data = emit_convert(spi, uop->type, SWS_PIXEL_F32,
                                id->f32vec4_type, data);
            break;
        case SWS_UOP_EXPAND_PAIR:
        case SWS_UOP_EXPAND_QUAD: {
            /* The SPIR-V compiler should optimize this multiplication into
             * a shift and or, if faster */
            const uint32_t k = uop->uop == SWS_UOP_EXPAND_PAIR ? 0x101 : 0x1010101;
            const int k_v = const_splat(spi, id, SWS_PIXEL_U32, (SwsPixel) { .u32 = k });
            data = spi_OpIMul(spi, type_v, data, k_v);
            break;
        }
        case SWS_UOP_LSHIFT:
        case SWS_UOP_RSHIFT: {
            const SwsPixel amount = { .u32 = uop->par.shift.amount };
            const int shift_v = const_splat(spi, id, SWS_PIXEL_U32, amount);
            if (uop->uop == SWS_UOP_LSHIFT)
                data = spi_OpShiftLeftLogical(spi, type_v, data, shift_v);
            else
                data = spi_OpShiftRightLogical(spi, type_v, data, shift_v);
            break;
        }
        case SWS_UOP_SCALE:
            data = emit_mul(spi, uop->type, type_v, data,
                            const_splat(spi, id, uop->type, uop->data.scalar));
            break;
        case SWS_UOP_ADD: {
            const int zero = const_zero(id, uop->type);
            int arg[4] = { zero, zero, zero, zero };
            for (int i = 0; i < 4; i++) {
                if (!SWS_COMP_TEST(uop->mask, i))
                    continue;
                arg[i] = const_pixel(spi, id, uop->type, uop->data.vec4[i]);
            }

            int arg_v = spi_OpConstantComposite(spi, type_v, arg[0], arg[1], arg[2], arg[3]);
            data = emit_add(spi, uop->type, type_v, data, arg_v);
            break;
        }
        case SWS_UOP_MIN:
        case SWS_UOP_MAX:
            err = emit_clamp(spi, id, uop, &data);
            break;
        case SWS_UOP_DITHER:
            err = emit_dither(spi, id, uop, &data);
            break;
        case SWS_UOP_LINEAR:
            err = emit_linear(spi, id, uop, &data);
            break;
        case SWS_UOP_UNPACK: {
            int shift, mask;
            define_pack_consts(spi, id, uop, &shift, &mask);
            data = spi_OpVectorShuffle(spi, type_v, data, data, 0, 0, 0, 0);
            data = spi_OpShiftRightLogical(spi, type_v, data, shift);
            data = spi_OpBitwiseAnd(spi, type_v, data, mask);
            break;
        }
        case SWS_UOP_PACK:
            err = emit_pack(spi, id, uop, &data);
            break;
        default:
            return AVERROR(ENOTSUP);
        }

        if (err < 0)
            return err;
    }

    /* Return and finalize */
    spi_OpReturn(spi);
    spi_OpFunctionEnd(spi);

    if (p->nb_data_bufs)
        ff_vk_shader_add_descriptor_set(&s->vkctx, shd, id->buf_desc,
                                        p->nb_data_bufs, 1);

    define_entry_point(shd, spi, id);
    return 0;
}

static int add_ops_spirv(SwsContext *sws, VulkanPriv *p, FFVulkanOpsCtx *s,
                         const SwsOpList *ops, const SwsUOpList *uops,
                         FFVulkanShader *shd)
{
    uint8_t *spvbuf = NULL;
    SPICtx spi_context, *spi = &spi_context;
    spi_init(spi);

    int err = emit_shader(sws, p, s, ops, uops, shd, spi);
    if (err < 0)
        goto fail;

    int len = spi_end(spi, &spvbuf);
    if (len < 0) {
        err = len;
        goto fail;
    }

    err = ff_vk_shader_link(&s->vkctx, shd, spvbuf, len, "main");

fail:
    av_free(spvbuf);
    spi_uninit(spi);
    return err;
}

static int compile_spirv(SwsContext *sws, const SwsOpList *ops,
                         SwsCompiledOp *out)
{
    SwsUOpList *uops = NULL;
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

    uops = ff_sws_uop_list_alloc();
    if (!uops) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    err = ff_sws_ops_translate(sws, ops, SWS_UOP_FLAG_ADD, uops);
    if (err < 0)
        goto fail;

    err = add_ops_spirv(sws, p, s, ops, uops, &p->shd);
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

    av_log(sws, AV_LOG_DEBUG, "Compiled micro-ops:\n");
    for (int i = 0; i < uops->num_ops; i++) {
        char name[SWS_UOP_NAME_MAX];
        ff_sws_uop_name(&uops->ops[i], name);
        av_log(sws, AV_LOG_DEBUG, "    %s\n", name);
    }

    ff_sws_uop_list_free(&uops);
    return 0;

fail:
    ff_sws_uop_list_free(&uops);
    free_fn(p);
    return err;
}

const SwsOpBackend backend_spirv = {
    .name      = "spirv",
    .flags     = SWS_BACKEND_SPIRV,
    .compile   = compile_spirv,
    .hw_format = AV_PIX_FMT_VULKAN,
};
