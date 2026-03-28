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

#include "../ops_internal.h"
#include "../swscale_internal.h"

#include "ops.h"

static void ff_sws_vk_uninit(AVRefStructOpaque opaque, void *obj)
{
    FFVulkanOpsCtx *s = obj;

#if CONFIG_LIBSHADERC || CONFIG_LIBGLSLANG
    if (s->spvc)
        s->spvc->uninit(&s->spvc);
#endif
    ff_vk_exec_pool_free(&s->vkctx, &s->e);
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
        ff_vk_exec_pool_free(&s->vkctx, &s->e);
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

    err = ff_vk_exec_pool_init(&s->vkctx, s->qf, &s->e, 1,
                               0, 0, 0, NULL);
    if (err < 0)
        return err;

#if CONFIG_LIBSHADERC || CONFIG_LIBGLSLANG
    if (!s->spvc) {
        s->spvc = ff_vk_spirv_init();
        if (!s->spvc)
            return AVERROR(ENOMEM);
    }
#endif

    return 0;
}

typedef struct VulkanPriv {
    FFVulkanOpsCtx *s;
    FFVulkanShader shd;
    enum FFVkShaderRepFormat src_rep;
    enum FFVkShaderRepFormat dst_rep;
} VulkanPriv;

static void process(const SwsFrame *dst, const SwsFrame *src, int y, int h,
                    const SwsPass *pass)
{
    VulkanPriv *p = (VulkanPriv *) pass->priv;
    FFVkExecContext *ec = ff_vk_exec_get(&p->s->vkctx, &p->s->e);
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

    ff_vk_exec_bind_shader(&p->s->vkctx, ec, &p->shd);

    vk->CmdDispatch(ec->buf,
                    FFALIGN(src_f->width, p->shd.lg_size[0])/p->shd.lg_size[0],
                    FFALIGN(src_f->height, p->shd.lg_size[1])/p->shd.lg_size[1],
                    1);

    ff_vk_exec_submit(&p->s->vkctx, ec);
    ff_vk_exec_wait(&p->s->vkctx, ec);
}

static void free_fn(void *priv)
{
    VulkanPriv *p = priv;
    ff_vk_shader_free(&p->s->vkctx, &p->shd);
    av_free(priv);
}

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
        .elems = op->rw.packed ? 1 : op->rw.elems,
        .stages = VK_SHADER_STAGE_COMPUTE_BIT,
    };

    *out_rep = op->type == SWS_PIXEL_F32 ? FF_VK_REP_FLOAT : FF_VK_REP_UINT;
}

#define QSTR "(%i/%i%s)"
#define QTYPE(i) op->c.q4[i].num, op->c.q4[i].den,       \
                 cur_type == SWS_PIXEL_F32 ? ".0f" : ""

static int add_ops_glsl(VulkanPriv *p, FFVulkanOpsCtx *s,
                        SwsOpList *ops, FFVulkanShader *shd)
{
    int err;
    uint8_t *spv_data;
    size_t spv_len;
    void *spv_opaque = NULL;

    /* Interlaced formats are not currently supported */
    if (ops->src.interlaced || ops->dst.interlaced)
        return AVERROR(ENOTSUP);

    err = ff_vk_shader_init(&s->vkctx, shd, "sws_pass",
                            VK_SHADER_STAGE_COMPUTE_BIT,
                            NULL, 0, 32, 32, 1, 0);
    if (err < 0)
        return err;

    int nb_desc = 0;
    FFVulkanDescriptorSetBinding buf_desc[8];

    const SwsOp *read  = ff_sws_op_list_input(ops);
    const SwsOp *write = ff_sws_op_list_output(ops);
    if (read)
        add_desc_read_write(&buf_desc[nb_desc++], &p->src_rep, read);
    add_desc_read_write(&buf_desc[nb_desc++], &p->dst_rep, write);
    ff_vk_shader_add_descriptor_set(&s->vkctx, shd, buf_desc, nb_desc, 0, 0);

    GLSLC(0, void main()                                                      );
    GLSLC(0, {                                                                );
    GLSLC(1,     ivec2 pos = ivec2(gl_GlobalInvocationID.xy);                 );
    GLSLC(1,     ivec2 size = imageSize(src_img[0]);                          );
    GLSLC(1,     if (any(greaterThanEqual(pos, size)))                        );
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
            if (op->rw.frac || op->rw.filter) {
                return AVERROR(ENOTSUP);
            } else if (op->rw.packed) {
                GLSLF(1, %s = %s(imageLoad(src_img[0], pos)).%c%c%c%c;         ,
                      type_name, type_v, "xyzw"[ops->order_src.in[0]],
                                         "xyzw"[ops->order_src.in[1]],
                                         "xyzw"[ops->order_src.in[2]],
                                         "xyzw"[ops->order_src.in[3]]);
            } else {
                for (int i = 0; i < op->rw.elems; i++)
                    GLSLF(1, %s.%c = %s(imageLoad(src_img[%i], pos)[0]);       ,
                          type_name, "xyzw"[i], type_s, ops->order_src.in[i]);
            }
            break;
        }
        case SWS_OP_WRITE: {
            if (op->rw.frac || op->rw.filter) {
                return AVERROR(ENOTSUP);
            } else if (op->rw.packed) {
                GLSLF(1, imageStore(dst_img[0], pos, %s(%s).%c%c%c%c);         ,
                      type_v, type_name, "xyzw"[ops->order_dst.in[0]],
                                         "xyzw"[ops->order_dst.in[1]],
                                         "xyzw"[ops->order_dst.in[2]],
                                         "xyzw"[ops->order_dst.in[3]]);
            } else {
                for (int i = 0; i < op->rw.elems; i++)
                    GLSLF(1, imageStore(dst_img[%i], pos, %s(%s[%i]));         ,
                          ops->order_dst.in[i], type_v, type_name, i);
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
                if (!op->c.q4[i].den)
                    continue;
                av_bprintf(&shd->src, "    %s.%c = %s"QSTR";\n", type_name,
                           "xyzw"[i], type_s, QTYPE(i));
            }
            break;
        }
        case SWS_OP_SCALE:
            av_bprintf(&shd->src, "    %s = %s*%i/%i;\n",
                       type_name, type_name, op->c.q.num, op->c.q.den);
            break;
        case SWS_OP_MIN:
        case SWS_OP_MAX:
            for (int i = 0; i < 4; i++) {
                if (!op->c.q4[i].den)
                    continue;
                av_bprintf(&shd->src, "    %s.%c = %s(%s.%c, "QSTR");\n",
                           type_name, "xyzw"[i],
                           op->op == SWS_OP_MIN ? "min" : "max",
                           type_name, "xyzw"[i],
                           op->c.q4[i].num, op->c.q4[i].den,
                           cur_type == SWS_PIXEL_F32 ? ".0f" : "");
            }
            break;
        case SWS_OP_LSHIFT:
        case SWS_OP_RSHIFT:
            av_bprintf(&shd->src, "    %s %s= %i;\n", type_name,
                       op->op == SWS_OP_LSHIFT ? "<<" : ">>", op->c.u);
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
        case SWS_OP_DITHER:
            av_bprintf(&shd->src, "    precise const float dm%i[%i][%i] = {\n",
                       n, 1 << op->dither.size_log2, 1 << op->dither.size_log2);
            int size = (1 << op->dither.size_log2);
            for (int i = 0; i < size; i++) {
                av_bprintf(&shd->src, "        { ");
                for (int j = 0; j < size; j++)
                    av_bprintf(&shd->src, "%i/%i.0, ",
                               op->dither.matrix[i*size + j].num,
                               op->dither.matrix[i*size + j].den);
                av_bprintf(&shd->src, "}, %s\n", i == (size - 1) ? "\n    };" : "");
            }
            for (int i = 0; i < 4; i++) {
                if (op->dither.y_offset[i] < 0)
                    continue;
                av_bprintf(&shd->src, "    %s.%c += dm%i[(pos.y + %i) & %i]"
                                                        "[pos.x & %i];\n",
                           type_name, "xyzw"[i], n,
                           op->dither.y_offset[i], size - 1,
                           size - 1);
            }
            break;
        case SWS_OP_LINEAR:
            for (int i = 0; i < 4; i++) {
                if (op->lin.m[i][4].num)
                    av_bprintf(&shd->src, "    tmp.%c = (%i/%i.0);\n", "xyzw"[i],
                               op->lin.m[i][4].num, op->lin.m[i][4].den);
                else
                    av_bprintf(&shd->src, "    tmp.%c = 0;\n", "xyzw"[i]);
                for (int j = 0; j < 4; j++) {
                    if (!op->lin.m[i][j].num)
                        continue;
                    av_bprintf(&shd->src, "    tmp.%c += f32.%c*(%i/%i.0);\n",
                               "xyzw"[i], "xyzw"[j],
                               op->lin.m[i][j].num, op->lin.m[i][j].den);
                }
            }
            av_bprintf(&shd->src, "    f32 = tmp;\n");
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

static int compile(SwsContext *sws, SwsOpList *ops, SwsCompiledOp *out)
{
    int err;
    SwsInternal *c = sws_internal(sws);
    FFVulkanOpsCtx *s = c->hw_priv;
    if (!s)
        return AVERROR(ENOTSUP);

    VulkanPriv p = {
        .s = s,
    };

#if CONFIG_LIBSHADERC || CONFIG_LIBGLSLANG
    {
        err = add_ops_glsl(&p, s, ops, &p.shd);
        if (err < 0)
            return err;
    }
#else
    return AVERROR(ENOTSUP);
#endif

    err = ff_vk_shader_register_exec(&s->vkctx, &s->e, &p.shd);
    if (err < 0)
        return err;

    *out = (SwsCompiledOp) {
        .opaque      = true,
        .func_opaque = process,
        .priv        = av_memdup(&p, sizeof(p)),
        .free        = free_fn,
    };
    return 0;
}

const SwsOpBackend backend_vulkan = {
    .name      = "vulkan",
    .compile   = compile,
    .hw_format = AV_PIX_FMT_VULKAN,
};
