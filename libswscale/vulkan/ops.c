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

    return 0;
}

typedef struct VulkanPriv {
    FFVulkanOpsCtx *s;
    FFVulkanShader shd;
} VulkanPriv;

static void process(const SwsOpExec *exec, const void *priv,
                    int x_start, int y_start, int x_end, int y_end)
{
    const VulkanPriv *p = priv;
    FFVkExecContext *ec = ff_vk_exec_get(&p->s->vkctx, &p->s->e);
    ff_vk_exec_start(&p->s->vkctx, ec);


    ff_vk_exec_submit(&p->s->vkctx, ec);
}

static void free_fn(void *priv)
{
    VulkanPriv *p = priv;
    ff_vk_shader_free(&p->s->vkctx, &p->shd);
    av_free(priv);
}

static int compile(SwsContext *sws, SwsOpList *ops, SwsCompiledOp *out)
{
    SwsInternal *c = sws_internal(sws);
    FFVulkanOpsCtx *s = c->hw_priv;
    if (!s)
        return AVERROR(ENOTSUP);

    VulkanPriv p = {
        .s = c->hw_priv,
    };

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
