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

#include "../ops_internal.h"
#include "../swscale_internal.h"
#include "libavutil/mem.h"
#include "ops.h"

void ff_sws_vk_uninit(SwsContext *sws)
{
    SwsInternal *c = sws_internal(sws);
    FFVulkanOpsCtx *s = c->hw_priv;
    if (!s)
        return;

#if CONFIG_LIBSHADERC || CONFIG_LIBGLSLANG
    if (s->spvc)
        s->spvc->uninit(&s->spvc);
#endif
    ff_vk_exec_pool_free(&s->vkctx, &s->e);
    ff_vk_uninit(&s->vkctx);
    av_freep(&c->hw_priv);
}

int ff_sws_vk_init(SwsContext *sws, AVBufferRef *dev_ref)
{
    int err;
    SwsInternal *c = sws_internal(sws);

    if (!c->hw_priv) {
        c->hw_priv = av_mallocz(sizeof(FFVulkanOpsCtx));
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

static void process(const SwsOpExec *exec, const void *priv,
                    int x_start, int y_start, int x_end, int y_end)
{
    VulkanPriv *p = (VulkanPriv *)priv;
    FFVkExecContext *ec = ff_vk_exec_get(&p->s->vkctx, &p->s->e);
    FFVulkanFunctions *vk = &p->s->vkctx.vkfn;
    ff_vk_exec_start(&p->s->vkctx, ec);

    AVFrame *src_f = (AVFrame *) exec->in_frame->avframe;
    AVFrame *dst_f = (AVFrame *) exec->out_frame->avframe;
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

    for (int n = 0; n < ops->num_ops; n++) {
        const SwsOp *op = &ops->ops[n];
        /* Set initial type */
        if (op->op == SWS_OP_READ || op->op == SWS_OP_WRITE ||
            op->op == SWS_OP_CLEAR) {
            if (op->rw.frac)
                return AVERROR(ENOTSUP);
        }
        if (op->op == SWS_OP_READ || op->op == SWS_OP_WRITE) {
            const char *img_type = op->type == SWS_PIXEL_F32 ? "rgba32f"  :
                                   op->type == SWS_PIXEL_U32 ? "rgba32ui" :
                                   op->type == SWS_PIXEL_U16 ? "rgba16ui" :
                                                               "rgba8ui";
            buf_desc[nb_desc++] = (FFVulkanDescriptorSetBinding) {
                .name = op->op == SWS_OP_WRITE ? "dst_img" : "src_img",
                .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .mem_layout = img_type,
                .mem_quali = op->op == SWS_OP_WRITE ? "writeonly" : "readonly",
                .dimensions = 2,
                .elems = (op->rw.packed ? 1 : op->rw.elems),
                .stages = VK_SHADER_STAGE_COMPUTE_BIT,
            };
            if (op->op == SWS_OP_READ)
                p->src_rep = op->type == SWS_PIXEL_F32 ? FF_VK_REP_FLOAT :
                             FF_VK_REP_UINT;
            else
                p->dst_rep = op->type == SWS_PIXEL_F32 ? FF_VK_REP_FLOAT :
                             FF_VK_REP_UINT;
        }
    }

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
    GLSLC(1,     f32vec4 f32;                                                 );
    GLSLC(0,                                                                  );

    const char *type_name = ff_sws_pixel_type_name(ops->ops[0].type);
    for (int n = 0; n < ops->num_ops; n++) {
        const SwsOp *op = &ops->ops[n];
        const char *type_v = op->type == SWS_PIXEL_F32 ? "f32vec4" :
                             op->type == SWS_PIXEL_U32 ? "u32vec4" :
                             op->type == SWS_PIXEL_U16 ? "u16vec4" : "u8vec4";
        const char *type_s = op->type == SWS_PIXEL_F32 ? "float" :
                             op->type == SWS_PIXEL_U32 ? "uint32_t" :
                             op->type == SWS_PIXEL_U16 ? "uint16_t" : "uint8_t";
        switch (op->op) {
        case SWS_OP_READ: {
            if (op->rw.packed) {
                GLSLF(1, %s = %s(imageLoad(src_img[0], pos));                  ,
                      type_name, type_v);
            } else {
                for (int i = 0; i < (op->rw.packed ? 1 : op->rw.elems); i++)
                    GLSLF(1, %s.%c = %s(imageLoad(src_img[%i], pos)[0]);      ,
                          type_name, "xyzw"[i], type_s, i);
            }
            break;
        }
        case SWS_OP_WRITE: {
            if (op->rw.packed) {
                GLSLF(1, imageStore(dst_img[0], pos, %s(%s));                  ,
                      type_v, type_name);
            } else {
                for (int i = 0; i < (op->rw.packed ? 1 : op->rw.elems); i++)
                    GLSLF(1, imageStore(dst_img[%i], pos, %s(%s[%i]));         ,
                          i, type_v, type_name, i);
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
                av_bprintf(&shd->src, "    %s.%c = %s(%i/%i%s);\n", type_name,
                           "xyzw"[i], type_s, op->c.q4[i].num, op->c.q4[i].den,
                           op->type == SWS_PIXEL_F32 ? ".0f" : "");
            }
            break;
        }
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
        .slice_align = 0,
        .block_size  = 1,
        .func        = process,
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
