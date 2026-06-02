/**
 * Copyright (C) 2025 Niklas Haas
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

#include "libavutil/avassert.h"
#include "libavutil/mem.h"
#include "libavutil/rational.h"

#include "ops_chain.h"

#define Q(N) ((AVRational) { N, 1 })

SwsOpChain *ff_sws_op_chain_alloc(void)
{
    return av_mallocz(sizeof(SwsOpChain));
}

void ff_sws_op_chain_free_cb(void *ptr)
{
    if (!ptr)
        return;

    SwsOpChain *chain = ptr;
    for (int i = 0; i < chain->num_impl + 1; i++) {
        if (chain->free[i])
            chain->free[i](&chain->impl[i].priv);
    }

    av_free(chain);
}

int ff_sws_op_chain_append(SwsOpChain *chain, SwsFuncPtr func,
                           void (*free)(SwsOpPriv *), const SwsOpPriv *priv)
{
    const int idx = chain->num_impl;
    if (idx == SWS_MAX_OPS)
        return AVERROR(EINVAL);

    av_assert1(func);
    chain->impl[idx].cont = func;
    chain->impl[idx + 1].priv = *priv;
    chain->free[idx + 1] = free;
    chain->num_impl++;
    return 0;
}

#define q2pixel(type, q) ((q).den ? (type) (q).num / (q).den : 0)

int ff_sws_setup_scale(const SwsImplParams *params, SwsImplResult *out)
{
    const SwsOp *op = params->op;
    const AVRational factor = op->scale.factor;
    switch (op->type) {
    case SWS_PIXEL_U8:  out->priv.u8[0]  = q2pixel(uint8_t,  factor); break;
    case SWS_PIXEL_U16: out->priv.u16[0] = q2pixel(uint16_t, factor); break;
    case SWS_PIXEL_U32: out->priv.u32[0] = q2pixel(uint32_t, factor); break;
    case SWS_PIXEL_F32: out->priv.f32[0] = q2pixel(float,    factor); break;
    default: return AVERROR(EINVAL);
    }

    return 0;
}

int ff_sws_setup_clamp(const SwsImplParams *params, SwsImplResult *out)
{
    const SwsOp *op = params->op;
    for (int i = 0; i < 4; i++) {
        const AVRational limit = op->clamp.limit[i];
        switch (op->type) {
        case SWS_PIXEL_U8:  out->priv.u8[i]  = q2pixel(uint8_t,  limit); break;
        case SWS_PIXEL_U16: out->priv.u16[i] = q2pixel(uint16_t, limit); break;
        case SWS_PIXEL_U32: out->priv.u32[i] = q2pixel(uint32_t, limit); break;
        case SWS_PIXEL_F32: out->priv.f32[i] = q2pixel(float,    limit); break;
        default: return AVERROR(EINVAL);
        }
    }

    return 0;
}

int ff_sws_setup_clear(const SwsImplParams *params, SwsImplResult *out)
{
    const SwsOp *op = params->op;
    for (int i = 0; i < 4; i++) {
        const AVRational value = op->clear.value[i];
        if (!value.den)
            continue;
        switch (op->type) {
        case SWS_PIXEL_U8:  out->priv.u8[i]  = q2pixel(uint8_t,  value); break;
        case SWS_PIXEL_U16: out->priv.u16[i] = q2pixel(uint16_t, value); break;
        case SWS_PIXEL_U32: out->priv.u32[i] = q2pixel(uint32_t, value); break;
        case SWS_PIXEL_F32: out->priv.f32[i] = q2pixel(float,    value); break;
        default: return AVERROR(EINVAL);
        }
    }

    return 0;
}

int ff_sws_uop_lookup(SwsContext *ctx, const SwsOpTable *const tables[],
                      int num_tables, const SwsUOp *uop, const int block_size,
                      SwsOpChain *chain)
{
    const unsigned cpu_flags = av_get_cpu_flags();
    const SwsOpEntry *match = NULL;
    int ret;

    SwsImplParams params = {
        .ctx = ctx,
        .uop = uop
    };

    for (int n = 0; !match && n < num_tables; n++) {
        const SwsOpTable *table = params.table = tables[n];
        if (table->block_size && table->block_size != block_size ||
            table->cpu_flags & ~cpu_flags)
            continue;

        for (int i = 0; table->entries[i]; i++) {
            const SwsOpEntry *entry = table->entries[i];
            const SwsUOp entry_uop = {
                .uop  = entry->uop,
                .type = entry->type,
                .mask = entry->mask,
                .par  = entry->par,
            };

            if (ff_sws_uop_cmp(uop, &entry_uop) != 0)
                continue;
            if (entry->check && !entry->check(&params))
                continue;

            match = entry;
            break;
        }
    }

    if (!match) {
        char name[64];
        ff_sws_uop_name(uop, name);
        av_log(ctx, AV_LOG_DEBUG, "No implementation found for: %s\n", name);
        return AVERROR(ENOTSUP);
    }

    SwsImplResult res = {0};
    if (match->setup) {
        ret = match->setup(&params, &res);
        if (ret < 0)
            return ret;
    }

    ret = ff_sws_op_chain_append(chain, res.func ? res.func : match->func,
                                 res.free, &res.priv);
    if (ret < 0) {
        if (res.free)
            res.free(&res.priv);
        return ret;
    }

    for (int i = 0; i < 4; i++) {
        chain->over_read[i]  = FFMAX(chain->over_read[i],  res.over_read[i]);
        chain->over_write[i] = FFMAX(chain->over_write[i], res.over_write[i]);
    }

    chain->cpu_flags |= params.table->cpu_flags;
    return 0;
}

int ff_sws_setup_scalar(const SwsImplParams *params, SwsImplResult *out)
{
    const SwsUOp *uop = params->uop;
    const SwsPixel scalar = uop->data.scalar;
    switch (uop->type) {
    case SWS_PIXEL_U8:  out->priv.u8[0]  = scalar.u8;  break;
    case SWS_PIXEL_U16: out->priv.u16[0] = scalar.u16; break;
    case SWS_PIXEL_U32: out->priv.u32[0] = scalar.u32; break;
    case SWS_PIXEL_F32: out->priv.f32[0] = scalar.f32; break;
    default: return AVERROR(EINVAL);
    }

    return 0;
}

int ff_sws_setup_vec4(const SwsImplParams *params, SwsImplResult *out)
{
    const SwsUOp *uop = params->uop;
    for (int i = 0; i < 4; i++) {
        const SwsPixel vi = uop->data.vec4[i];
        switch (uop->type) {
        case SWS_PIXEL_U8:  out->priv.u8[i]  = vi.u8;  break;
        case SWS_PIXEL_U16: out->priv.u16[i] = vi.u16; break;
        case SWS_PIXEL_U32: out->priv.u32[i] = vi.u32; break;
        case SWS_PIXEL_F32: out->priv.f32[i] = vi.f32; break;
        default: return AVERROR(EINVAL);
        }
    }

    return 0;
}
