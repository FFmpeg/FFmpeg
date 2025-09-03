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

void ff_sws_op_chain_free(SwsOpChain *chain)
{
    if (!chain)
        return;

    for (int i = 0; i < chain->num_impl + 1; i++) {
        if (chain->free[i])
            chain->free[i](chain->impl[i].priv.ptr);
    }

    av_free(chain);
}

int ff_sws_op_chain_append(SwsOpChain *chain, SwsFuncPtr func,
                           void (*free)(void *), const SwsOpPriv *priv)
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

/**
 * Match an operation against a reference operation. Returns a score for how
 * well the reference matches the operation, or 0 if there is no match.
 *
 * If `ref->comps` has any flags set, they must be set in `op` as well.
 * Likewise, if `ref->comps` has any components marked as unused, they must be
 * marked as as unused in `ops` as well.
 *
 * For SWS_OP_LINEAR, `ref->linear.mask` must be a strict superset of
 * `op->linear.mask`, but may not contain any columns explicitly ignored by
 * `op->comps.unused`.
 *
 * For SWS_OP_READ, SWS_OP_WRITE, SWS_OP_SWAP_BYTES and SWS_OP_SWIZZLE, the
 * exact type is not checked, just the size.
 *
 * Components set in `next.unused` are ignored when matching. If `flexible`
 * is true, the op body is ignored - only the operation, pixel type, and
 * component masks are checked.
 */
static int op_match(const SwsOp *op, const SwsOpEntry *entry, const SwsComps next)
{
    int score = 10;
    if (op->op != entry->op)
        return 0;

    switch (op->op) {
    case SWS_OP_READ:
    case SWS_OP_WRITE:
    case SWS_OP_SWAP_BYTES:
    case SWS_OP_SWIZZLE:
        /* Only the size matters for these operations */
        if (ff_sws_pixel_type_size(op->type) != ff_sws_pixel_type_size(entry->type))
            return 0;
        break;
    default:
        if (op->type != entry->type)
            return 0;
        break;
    }

    for (int i = 0; i < 4; i++) {
        if (entry->unused[i]) {
            if (op->comps.unused[i])
                score += 1; /* Operating on fewer components is better .. */
            else
                return 0; /* .. but not too few! */
        }
    }

    if (op->op == SWS_OP_CLEAR) {
        /* Clear pattern must match exactly, regardless of `entry->flexible` */
        for (int i = 0; i < 4; i++) {
            if (!next.unused[i] && entry->unused[i] != !!op->c.q4[i].den)
                return 0;
        }
    }

    /* Flexible variants always match, but lower the score to prioritize more
     * specific implementations if they exist */
    if (entry->flexible)
        return score - 5;

    switch (op->op) {
    case SWS_OP_INVALID:
        return 0;
    case SWS_OP_READ:
    case SWS_OP_WRITE:
        if (op->rw.elems   != entry->rw.elems ||
            op->rw.frac    != entry->rw.frac  ||
            (op->rw.elems > 1 && op->rw.packed != entry->rw.packed))
            return 0;
        return score;
    case SWS_OP_SWAP_BYTES:
        return score;
    case SWS_OP_PACK:
    case SWS_OP_UNPACK:
        for (int i = 0; i < 4 && op->pack.pattern[i]; i++) {
            if (op->pack.pattern[i] != entry->pack.pattern[i])
                return 0;
        }
        return score;
    case SWS_OP_CLEAR:
        for (int i = 0; i < 4; i++) {
            if (!op->c.q4[i].den)
                continue;
            if (av_cmp_q(op->c.q4[i], Q(entry->clear_value)) && !next.unused[i])
                return 0;
        }
        return score;
    case SWS_OP_LSHIFT:
    case SWS_OP_RSHIFT:
        av_assert1(entry->flexible);
        return score;
    case SWS_OP_SWIZZLE:
        for (int i = 0; i < 4; i++) {
            if (op->swizzle.in[i] != entry->swizzle.in[i] && !next.unused[i])
                return 0;
        }
        return score;
    case SWS_OP_CONVERT:
        if (op->convert.to     != entry->convert.to ||
            op->convert.expand != entry->convert.expand)
            return 0;
        return score;
    case SWS_OP_DITHER:
        return op->dither.size_log2 == entry->dither_size ? score : 0;
    case SWS_OP_MIN:
    case SWS_OP_MAX:
        av_assert1(entry->flexible);
        return score;
    case SWS_OP_LINEAR:
        /* All required elements must be present */
        if (op->lin.mask & ~entry->linear_mask)
            return 0;
        /* To avoid operating on possibly undefined memory, filter out
         * implementations that operate on more input components */
        for (int i = 0; i < 4; i++) {
            if ((entry->linear_mask & SWS_MASK_COL(i)) && op->comps.unused[i])
                return 0;
        }
        /* Prioritize smaller implementations */
        score += av_popcount(SWS_MASK_ALL ^ entry->linear_mask);
        return score;
    case SWS_OP_SCALE:
        return score;
    case SWS_OP_TYPE_NB:
        break;
    }

    av_unreachable("Invalid operation type!");
    return 0;
}

int ff_sws_op_compile_tables(const SwsOpTable *const tables[], int num_tables,
                             SwsOpList *ops, const int block_size,
                             SwsOpChain *chain)
{
    static const SwsOp dummy = { .comps.unused = { true, true, true, true }};
    const SwsOp *next = ops->num_ops > 1 ? &ops->ops[1] : &dummy;
    const unsigned cpu_flags = av_get_cpu_flags();
    const SwsOpEntry *best = NULL;
    const SwsOp *op = &ops->ops[0];
    int ret, best_score = 0, best_cpu_flags;
    SwsOpPriv priv = {0};

    for (int n = 0; n < num_tables; n++) {
        const SwsOpTable *table = tables[n];
        if (table->block_size && table->block_size != block_size ||
            table->cpu_flags & ~cpu_flags)
            continue;

        for (int i = 0; table->entries[i]; i++) {
            const SwsOpEntry *entry = table->entries[i];
            int score = op_match(op, entry, next->comps);
            if (score > best_score) {
                best_score = score;
                best_cpu_flags = table->cpu_flags;
                best = entry;
            }
        }
    }

    if (!best)
        return AVERROR(ENOTSUP);

    if (best->setup) {
        ret = best->setup(op, &priv);
        if (ret < 0)
            return ret;
    }

    chain->cpu_flags |= best_cpu_flags;
    ret = ff_sws_op_chain_append(chain, best->func, best->free, &priv);
    if (ret < 0) {
        if (best->free)
            best->free(&priv);
        return ret;
    }

    ops->ops++;
    ops->num_ops--;
    return ops->num_ops ? AVERROR(EAGAIN) : 0;
}

#define q2pixel(type, q) ((q).den ? (type) (q).num / (q).den : 0)

int ff_sws_setup_u8(const SwsOp *op, SwsOpPriv *out)
{
    out->u8[0] = op->c.u;
    return 0;
}

int ff_sws_setup_u(const SwsOp *op, SwsOpPriv *out)
{
    switch (op->type) {
    case SWS_PIXEL_U8:  out->u8[0]  = op->c.u; return 0;
    case SWS_PIXEL_U16: out->u16[0] = op->c.u; return 0;
    case SWS_PIXEL_U32: out->u32[0] = op->c.u; return 0;
    case SWS_PIXEL_F32: out->f32[0] = op->c.u; return 0;
    default: return AVERROR(EINVAL);
    }
}

int ff_sws_setup_q(const SwsOp *op, SwsOpPriv *out)
{
    switch (op->type) {
    case SWS_PIXEL_U8:  out->u8[0]  = q2pixel(uint8_t,  op->c.q); return 0;
    case SWS_PIXEL_U16: out->u16[0] = q2pixel(uint16_t, op->c.q); return 0;
    case SWS_PIXEL_U32: out->u32[0] = q2pixel(uint32_t, op->c.q); return 0;
    case SWS_PIXEL_F32: out->f32[0] = q2pixel(float,    op->c.q); return 0;
    default: return AVERROR(EINVAL);
    }

    return 0;
}

int ff_sws_setup_q4(const SwsOp *op, SwsOpPriv *out)
{
    for (int i = 0; i < 4; i++) {
        switch (op->type) {
        case SWS_PIXEL_U8:  out->u8[i]  = q2pixel(uint8_t,  op->c.q4[i]); break;
        case SWS_PIXEL_U16: out->u16[i] = q2pixel(uint16_t, op->c.q4[i]); break;
        case SWS_PIXEL_U32: out->u32[i] = q2pixel(uint32_t, op->c.q4[i]); break;
        case SWS_PIXEL_F32: out->f32[i] = q2pixel(float,    op->c.q4[i]); break;
        default: return AVERROR(EINVAL);
        }
    }

    return 0;
}
