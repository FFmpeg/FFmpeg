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

#include "libavutil/attributes.h"
#include "libavutil/avassert.h"
#include "libavutil/bswap.h"
#include "libavutil/rational.h"

#include "ops.h"
#include "ops_internal.h"

#define RET(x)                                                                 \
    do {                                                                       \
        if ((ret = (x)) < 0)                                                   \
            return ret;                                                        \
    } while (0)

/**
 * Try to commute a clear op with the next operation. Makes any adjustments
 * to the operations as needed, but does not perform the actual commutation.
 *
 * Returns whether successful.
 */
static bool op_commute_clear(SwsOp *op, SwsOp *next)
{
    SwsClearOp tmp = {0};

    av_assert1(op->op == SWS_OP_CLEAR);
    switch (next->op) {
    case SWS_OP_CONVERT:
        op->type = next->convert.to;
        av_fallthrough;
    case SWS_OP_LSHIFT:
    case SWS_OP_RSHIFT:
    case SWS_OP_DITHER:
    case SWS_OP_MIN:
    case SWS_OP_MAX:
    case SWS_OP_SCALE:
    case SWS_OP_READ:
    case SWS_OP_FILTER_H:
    case SWS_OP_FILTER_V:
        ff_sws_apply_op_q(next, op->clear.value);
        return true;
    case SWS_OP_SWIZZLE:
        op->clear.mask = ff_sws_comp_mask_swizzle(op->clear.mask, next->swizzle);
        ff_sws_apply_op_q(next, op->clear.value);
        return true;
    case SWS_OP_SWAP_BYTES:
        switch (next->type) {
        case SWS_PIXEL_U16:
            ff_sws_apply_op_q(next, op->clear.value); /* always works */
            return true;
        case SWS_PIXEL_U32:
            for (int i = 0; i < 4; i++) {
                if (!SWS_COMP_TEST(op->clear.mask, i))
                    continue;
                uint32_t v = av_bswap32(op->clear.value[i].num);
                if (v > INT_MAX)
                    return false; /* can't represent as AVRational anymore */
                tmp.value[i] = Q(v);
            }
            op->clear = tmp;
            return true;
        default:
            return false;
        }
    case SWS_OP_INVALID:
    case SWS_OP_WRITE:
    case SWS_OP_LINEAR:
    case SWS_OP_PACK:
    case SWS_OP_UNPACK:
    case SWS_OP_CLEAR:
        return false;
    case SWS_OP_TYPE_NB:
        break;
    }

    av_unreachable("Invalid operation type!");
    return false;
}

 /**
  * Try to commute a swizzle op with the next operation. Makes any adjustments
  * to the operations as needed, but does not perform the actual commutation.
  *
  * Returns whether successful.
  */
static bool op_commute_swizzle(SwsOp *op, SwsOp *next)
{
    bool seen[4] = {0};

    av_assert1(op->op == SWS_OP_SWIZZLE);
    switch (next->op) {
    case SWS_OP_CONVERT:
        op->type = next->convert.to;
        av_fallthrough;
    case SWS_OP_SWAP_BYTES:
    case SWS_OP_LSHIFT:
    case SWS_OP_RSHIFT:
    case SWS_OP_SCALE:
    case SWS_OP_FILTER_H:
    case SWS_OP_FILTER_V:
        return true;

    /**
     * We can commute per-channel ops only if the per-channel constants are the
     * same for all duplicated channels; e.g.:
     *   SWIZZLE {0, 0, 0, 3}
     *   NEXT    {x, x, x, w}
     * ->
     *   NEXT    {x, _, _, w}
     *   SWIZZLE {0, 0, 0, 3}
     */
    case SWS_OP_MIN:
    case SWS_OP_MAX: {
        const SwsClampOp c = next->clamp;
        for (int i = 0; i < 4; i++) {
            if (!SWS_OP_NEEDED(op, i))
                continue;
            const int j = op->swizzle.in[i];
            if (seen[j] && av_cmp_q(next->clamp.limit[j], c.limit[i]))
                return false;
            next->clamp.limit[j] = c.limit[i];
            seen[j] = true;
        }
        return true;
    }

    case SWS_OP_DITHER: {
        const SwsDitherOp d = next->dither;
        for (int i = 0; i < 4; i++) {
            if (!SWS_OP_NEEDED(op, i))
                continue;
            const int j = op->swizzle.in[i];
            if (seen[j] && next->dither.y_offset[j] != d.y_offset[i])
                return false;
            next->dither.y_offset[j] = d.y_offset[i];
            seen[j] = true;
        }
        return true;
    }

    case SWS_OP_INVALID:
    case SWS_OP_READ:
    case SWS_OP_WRITE:
    case SWS_OP_SWIZZLE:
    case SWS_OP_CLEAR:
    case SWS_OP_LINEAR:
    case SWS_OP_PACK:
    case SWS_OP_UNPACK:
        return false;
    case SWS_OP_TYPE_NB:
        break;
    }

    av_unreachable("Invalid operation type!");
    return false;
}

/**
 * Try to commute a filter op with the previous operation. Makes any
 * adjustments to the operations as needed, but does not perform the actual
 * commutation.
 *
 * Returns whether successful.
 */
static bool op_commute_filter(SwsOp *op, SwsOp *prev)
{
    switch (prev->op) {
    case SWS_OP_SWIZZLE:
    case SWS_OP_SCALE:
    case SWS_OP_LINEAR:
    case SWS_OP_DITHER:
        prev->type = SWS_PIXEL_F32;
        return true;
    case SWS_OP_CONVERT:
        if (prev->convert.to == SWS_PIXEL_F32) {
            av_assert0(!prev->convert.expand);
            FFSWAP(SwsPixelType, op->type, prev->type);
            return true;
        }
        return false;
    case SWS_OP_INVALID:
    case SWS_OP_READ:
    case SWS_OP_WRITE:
    case SWS_OP_SWAP_BYTES:
    case SWS_OP_UNPACK:
    case SWS_OP_PACK:
    case SWS_OP_LSHIFT:
    case SWS_OP_RSHIFT:
    case SWS_OP_CLEAR:
    case SWS_OP_MIN:
    case SWS_OP_MAX:
    case SWS_OP_FILTER_H:
    case SWS_OP_FILTER_V:
        return false;
    case SWS_OP_TYPE_NB:
        break;
    }

    av_unreachable("Invalid operation type!");
    return false;
}

/* returns log2(x) only if x is a power of two, or 0 otherwise */
static int exact_log2(const int x)
{
    int p;
    if (x <= 0)
        return 0;
    p = av_log2(x);
    return (1 << p) == x ? p : 0;
}

static int exact_log2_q(const AVRational x)
{
    if (x.den == 1)
        return exact_log2(x.num);
    else if (x.num == 1)
        return -exact_log2(x.den);
    else
        return 0;
}

/**
 * If a linear operation can be reduced to a scalar multiplication, returns
 * the corresponding scaling factor, or 0 otherwise.
 */
static bool extract_scalar(const SwsLinearOp *c, SwsComps comps, SwsComps prev,
                           SwsScaleOp *out_scale)
{
    SwsScaleOp scale = {0};

    /* There are components not on the main diagonal */
    if (c->mask & ~SWS_MASK_DIAG4)
        return false;

    for (int i = 0; i < 4; i++) {
        const AVRational s = c->m[i][i];
        if ((prev.flags[i]  & SWS_COMP_ZERO) ||
            (comps.flags[i] & SWS_COMP_GARBAGE))
            continue;
        if (scale.factor.den && av_cmp_q(s, scale.factor))
            return false;
        scale.factor = s;
    }

    if (scale.factor.den)
        *out_scale = scale;
    return scale.factor.den;
}

/* Extracts an integer clear operation (subset) from the given linear op. */
static bool extract_constant_rows(SwsLinearOp *c, SwsComps prev,
                                  SwsClearOp *out_clear)
{
    SwsClearOp clear = {0};
    bool ret = false;

    for (int i = 0; i < 4; i++) {
        bool const_row = c->m[i][4].den == 1; /* offset is integer */
        for (int j = 0; j < 4; j++) {
            const_row &= c->m[i][j].num == 0 || /* scalar is zero */
                         (prev.flags[j] & SWS_COMP_ZERO); /* input is zero */
        }
        if (const_row && (c->mask & SWS_MASK_ROW(i))) {
            clear.mask |= SWS_COMP(i);
            clear.value[i] = c->m[i][4];
            for (int j = 0; j < 5; j++)
                c->m[i][j] = Q(i == j);
            c->mask &= ~SWS_MASK_ROW(i);
            ret = true;
        }
    }

    if (ret)
        *out_clear = clear;
    return ret;
}

/* Unswizzle a linear operation by aligning single-input rows with
 * their corresponding diagonal */
static bool extract_swizzle(SwsLinearOp *op, SwsComps prev, SwsSwizzleOp *out_swiz)
{
    SwsSwizzleOp swiz = SWS_SWIZZLE(0, 1, 2, 3);
    SwsLinearOp c = *op;

    /* Find non-zero coefficients in the main 4x4 matrix */
    uint32_t nonzero = 0;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            if (!c.m[i][j].num || (prev.flags[j] & SWS_COMP_ZERO))
                continue;
            nonzero |= SWS_MASK(i, j);
        }
    }

    /* If a value is unique in its row and the target column is
     * empty, move it there and update the input swizzle */
    for (int i = 0; i < 4; i++) {
        if (nonzero & SWS_MASK_COL(i))
            continue; /* target column is not empty */
        for (int j = 0; j < 4; j++) {
            if ((nonzero & SWS_MASK_ROW(i)) == SWS_MASK(i, j)) {
                /* Move coefficient to the diagonal */
                c.m[i][i] = c.m[i][j];
                c.m[i][j] = Q(0);
                swiz.in[i] = j;
                break;
            }
        }
    }

    if (swiz.mask == SWS_SWIZZLE(0, 1, 2, 3).mask)
        return false; /* no swizzle was identified */

    c.mask = ff_sws_linear_mask(c);
    *out_swiz = swiz;
    *op = c;
    return true;
}

int ff_sws_op_list_optimize(SwsOpList *ops)
{
    int ret;

retry:
    ff_sws_op_list_update_comps(ops);

    /* Try to push filters towards the input; do this first to unblock
     * in-place optimizations like linear op fusion */
    for (int n = 1; n < ops->num_ops; n++) {
        SwsOp *op = &ops->ops[n];
        SwsOp *prev = &ops->ops[n - 1];

        switch (op->op) {
        case SWS_OP_FILTER_H:
        case SWS_OP_FILTER_V:
            if (op_commute_filter(op, prev)) {
                FFSWAP(SwsOp, *op, *prev);
                goto retry;
            }
            break;
        }
    }

    /* Apply all in-place optimizations (that do not re-order the list) */
    for (int n = 0; n < ops->num_ops; n++) {
        SwsOp dummy = {0};
        SwsOp *op = &ops->ops[n];
        SwsOp *prev = n ? &ops->ops[n - 1] : &dummy;
        SwsOp *next = n + 1 < ops->num_ops ? &ops->ops[n + 1] : &dummy;

        /* common helper variable */
        bool noop = true;

        if (!SWS_OP_NEEDED(op, 0) && !SWS_OP_NEEDED(op, 1) &&
            !SWS_OP_NEEDED(op, 2) && !SWS_OP_NEEDED(op, 3) &&
            op->op != SWS_OP_WRITE)
        {
            /* Remove any operation whose output is not needed */
            ff_sws_op_list_remove_at(ops, n, 1);
            goto retry;
        }

        switch (op->op) {
        case SWS_OP_READ:
            /* "Compress" planar reads where not all components are needed */
            if (!op->rw.packed) {
                SwsSwizzleOp swiz = SWS_SWIZZLE(0, 1, 2, 3);
                int nb_planes = 0;
                for (int i = 0; i < op->rw.elems; i++) {
                    if (!SWS_OP_NEEDED(op, i)) {
                        swiz.in[i] = 3 - (i - nb_planes); /* map to unused plane */
                        continue;
                    }

                    const int idx = nb_planes++;
                    av_assert1(idx <= i);
                    ops->plane_src[idx] = ops->plane_src[i];
                    swiz.in[i] = idx;
                }

                if (nb_planes < op->rw.elems) {
                    op->rw.elems = nb_planes;
                    RET(ff_sws_op_list_insert_at(ops, n + 1, &(SwsOp) {
                        .op = SWS_OP_SWIZZLE,
                        .type = op->rw.filter ? SWS_PIXEL_F32 : op->type,
                        .swizzle = swiz,
                    }));
                    goto retry;
                }
            }
            break;

        case SWS_OP_SWAP_BYTES:
            /* Redundant (double) swap */
            if (next->op == SWS_OP_SWAP_BYTES) {
                ff_sws_op_list_remove_at(ops, n, 2);
                goto retry;
            }
            break;

        case SWS_OP_UNPACK:
            /* Redundant unpack+pack */
            if (next->op == SWS_OP_PACK && next->type == op->type &&
                next->pack.pattern[0] == op->pack.pattern[0] &&
                next->pack.pattern[1] == op->pack.pattern[1] &&
                next->pack.pattern[2] == op->pack.pattern[2] &&
                next->pack.pattern[3] == op->pack.pattern[3])
            {
                ff_sws_op_list_remove_at(ops, n, 2);
                goto retry;
            }
            break;

        case SWS_OP_LSHIFT:
        case SWS_OP_RSHIFT:
            /* Two shifts in the same direction */
            if (next->op == op->op) {
                op->shift.amount += next->shift.amount;
                ff_sws_op_list_remove_at(ops, n + 1, 1);
                goto retry;
            }

            /* No-op shift */
            if (!op->shift.amount) {
                ff_sws_op_list_remove_at(ops, n, 1);
                goto retry;
            }
            break;

        case SWS_OP_CLEAR:
            for (int i = 0; i < 4; i++) {
                if (!SWS_COMP_TEST(op->clear.mask, i))
                    continue;

                if ((prev->comps.flags[i] & SWS_COMP_ZERO) &&
                    !(prev->comps.flags[i] & SWS_COMP_GARBAGE) &&
                    op->clear.value[i].num == 0)
                {
                    /* Redundant clear-to-zero of zero component */
                    op->clear.mask ^= SWS_COMP(i);
                } else if (!SWS_OP_NEEDED(op, i)) {
                    /* Unnecessary clear of unused component */
                    op->clear.mask ^= SWS_COMP(i);
                } else {
                    noop = false;
                }
            }

            if (noop) {
                ff_sws_op_list_remove_at(ops, n, 1);
                goto retry;
            }

            /* Transitive clear */
            if (next->op == SWS_OP_CLEAR) {
                for (int i = 0; i < 4; i++) {
                    if (SWS_COMP_TEST(next->clear.mask, i))
                        op->clear.value[i] = next->clear.value[i];
                }
                op->clear.mask |= next->clear.mask;
                ff_sws_op_list_remove_at(ops, n + 1, 1);
                goto retry;
            }
            break;

        case SWS_OP_SWIZZLE:
            for (int i = 0; i < 4; i++) {
                if (!SWS_OP_NEEDED(op, i))
                    continue;
                if (op->swizzle.in[i] != i)
                    noop = false;
            }

            /* Identity swizzle */
            if (noop) {
                ff_sws_op_list_remove_at(ops, n, 1);
                goto retry;
            }

            /* Transitive swizzle */
            if (next->op == SWS_OP_SWIZZLE) {
                const SwsSwizzleOp orig = op->swizzle;
                for (int i = 0; i < 4; i++)
                    op->swizzle.in[i] = orig.in[next->swizzle.in[i]];
                ff_sws_op_list_remove_at(ops, n + 1, 1);
                goto retry;
            }

            /* Swizzle planes instead of components, if possible */
            if (prev->op == SWS_OP_READ && !prev->rw.packed) {
                for (int dst = 0; dst < prev->rw.elems; dst++) {
                    const int src = op->swizzle.in[dst];
                    if (src > dst && src < prev->rw.elems) {
                        FFSWAP(int, ops->plane_src[dst], ops->plane_src[src]);
                        for (int i = dst; i < 4; i++) {
                            if (op->swizzle.in[i] == dst)
                                op->swizzle.in[i] = src;
                            else if (op->swizzle.in[i] == src)
                                op->swizzle.in[i] = dst;
                        }
                        goto retry;
                    }
                }
            }

            if (next->op == SWS_OP_WRITE && !next->rw.packed) {
                for (int dst = 0; dst < next->rw.elems; dst++) {
                    const int src = op->swizzle.in[dst];
                    if (src > dst && src < next->rw.elems) {
                        FFSWAP(int, ops->plane_dst[dst], ops->plane_dst[src]);
                        FFSWAP(int, op->swizzle.in[dst], op->swizzle.in[src]);
                        goto retry;
                    }
                }
            }
            break;

        case SWS_OP_CONVERT:
            /* No-op conversion */
            if (op->type == op->convert.to) {
                ff_sws_op_list_remove_at(ops, n, 1);
                goto retry;
            }

            /* Transitive conversion */
            if (next->op == SWS_OP_CONVERT &&
                op->convert.expand == next->convert.expand)
            {
                av_assert1(op->convert.to == next->type);
                op->convert.to = next->convert.to;
                ff_sws_op_list_remove_at(ops, n + 1, 1);
                goto retry;
            }

            /* Conversion followed by integer expansion */
            if (next->op == SWS_OP_SCALE && !op->convert.expand &&
                ff_sws_pixel_type_is_int(op->type) &&
                ff_sws_pixel_type_is_int(op->convert.to) &&
                !av_cmp_q(next->scale.factor,
                          ff_sws_pixel_expand(op->type, op->convert.to)))
            {
                op->convert.expand = true;
                ff_sws_op_list_remove_at(ops, n + 1, 1);
                goto retry;
            }
            break;

        case SWS_OP_MIN:
            for (int i = 0; i < 4; i++) {
                if (!SWS_OP_NEEDED(op, i) || !op->clamp.limit[i].den)
                    continue;
                if (av_cmp_q(op->clamp.limit[i], prev->comps.max[i]) < 0)
                    noop = false;
            }

            if (noop) {
                ff_sws_op_list_remove_at(ops, n, 1);
                goto retry;
            }
            break;

        case SWS_OP_MAX:
            for (int i = 0; i < 4; i++) {
                if (!SWS_OP_NEEDED(op, i) || !op->clamp.limit[i].den)
                    continue;
                if (av_cmp_q(prev->comps.min[i], op->clamp.limit[i]) < 0)
                    noop = false;
            }

            if (noop) {
                ff_sws_op_list_remove_at(ops, n, 1);
                goto retry;
            }
            break;

        case SWS_OP_DITHER:
            for (int i = 0; i < 4; i++) {
                if (op->dither.y_offset[i] < 0)
                    continue;
                if (!SWS_OP_NEEDED(op, i) || (prev->comps.flags[i] & SWS_COMP_EXACT)) {
                    op->dither.y_offset[i] = -1; /* unnecessary dither */
                    goto retry;
                } else {
                    noop = false;
                }
            }

            if (noop) {
                ff_sws_op_list_remove_at(ops, n, 1);
                goto retry;
            }
            break;

        case SWS_OP_LINEAR: {
            SwsSwizzleOp swizzle;
            SwsClearOp clear;
            SwsScaleOp scale;

            /* No-op (identity) linear operation */
            if (!op->lin.mask) {
                ff_sws_op_list_remove_at(ops, n, 1);
                goto retry;
            }

            if (next->op == SWS_OP_LINEAR) {
                /* 5x5 matrix multiplication after appending [ 0 0 0 0 1 ] */
                const SwsLinearOp m1 = op->lin;
                const SwsLinearOp m2 = next->lin;
                for (int i = 0; i < 4; i++) {
                    for (int j = 0; j < 5; j++) {
                        AVRational sum = Q(0);
                        for (int k = 0; k < 4; k++)
                            sum = av_add_q(sum, av_mul_q(m2.m[i][k], m1.m[k][j]));
                        if (j == 4) /* m1.m[4][j] == 1 */
                            sum = av_add_q(sum, m2.m[i][4]);
                        op->lin.m[i][j] = sum;
                    }
                }
                op->lin.mask = ff_sws_linear_mask(op->lin);
                ff_sws_op_list_remove_at(ops, n + 1, 1);
                goto retry;
            }

            /* Optimize away zero columns */
            for (int j = 0; j < 4; j++) {
                const uint32_t col = SWS_MASK_COL(j);
                if (!(prev->comps.flags[j] & SWS_COMP_ZERO) || !(op->lin.mask & col))
                    continue;
                for (int i = 0; i < 4; i++)
                    op->lin.m[i][j] = Q(i == j);
                op->lin.mask &= ~col;
                goto retry;
            }

            /* Optimize away unused rows */
            for (int i = 0; i < 4; i++) {
                const uint32_t row = SWS_MASK_ROW(i);
                if (SWS_OP_NEEDED(op, i) || !(op->lin.mask & row))
                    continue;
                for (int j = 0; j < 5; j++)
                    op->lin.m[i][j] = Q(i == j);
                op->lin.mask &= ~row;
                goto retry;
            }

            /* Convert constant rows to explicit clear instruction */
            if (extract_constant_rows(&op->lin, prev->comps, &clear)) {
                RET(ff_sws_op_list_insert_at(ops, n + 1, &(SwsOp) {
                    .op    = SWS_OP_CLEAR,
                    .type  = op->type,
                    .comps = op->comps,
                    .clear = clear,
                }));
                goto retry;
            }

            /* Multiplication by scalar constant */
            if (extract_scalar(&op->lin, op->comps, prev->comps, &scale)) {
                op->op    = SWS_OP_SCALE;
                op->scale = scale;
                goto retry;
            }

            /* Swizzle by fixed pattern */
            if (extract_swizzle(&op->lin, prev->comps, &swizzle)) {
                RET(ff_sws_op_list_insert_at(ops, n, &(SwsOp) {
                    .op      = SWS_OP_SWIZZLE,
                    .type    = op->type,
                    .swizzle = swizzle,
                }));
                goto retry;
            }
            break;
        }

        case SWS_OP_SCALE: {
            const int factor2 = exact_log2_q(op->scale.factor);

            /* No-op scaling */
            if (op->scale.factor.num == 1 && op->scale.factor.den == 1) {
                ff_sws_op_list_remove_at(ops, n, 1);
                goto retry;
            }

            /* Merge consecutive scaling operations (that don't overflow) */
            if (next->op == SWS_OP_SCALE) {
                int64_t p = op->scale.factor.num * (int64_t) next->scale.factor.num;
                int64_t q = op->scale.factor.den * (int64_t) next->scale.factor.den;
                if (FFABS(p) <= INT_MAX && FFABS(q) <= INT_MAX) {
                    av_reduce(&op->scale.factor.num, &op->scale.factor.den, p, q, INT_MAX);
                    ff_sws_op_list_remove_at(ops, n + 1, 1);
                    goto retry;
                }
            }

            /* Scaling by exact power of two */
            if (factor2 && ff_sws_pixel_type_is_int(op->type)) {
                op->op = factor2 > 0 ? SWS_OP_LSHIFT : SWS_OP_RSHIFT;
                op->shift.amount = FFABS(factor2);
                goto retry;
            }
            break;
        }

        case SWS_OP_FILTER_H:
        case SWS_OP_FILTER_V:
            /* Merge with prior simple planar read */
            if (prev->op == SWS_OP_READ && !prev->rw.filter &&
                !prev->rw.packed && !prev->rw.frac) {
                prev->rw.filter = op->op;
                prev->rw.kernel = av_refstruct_ref(op->filter.kernel);
                ff_sws_op_list_remove_at(ops, n, 1);
                goto retry;
            }
            break;
        }
    }

    /* Push clears to the back to void any unused components */
    for (int n = 0; n < ops->num_ops - 1; n++) {
        SwsOp *op = &ops->ops[n];
        SwsOp *next = &ops->ops[n + 1];

        switch (op->op) {
        case SWS_OP_CLEAR:
            if (op_commute_clear(op, next)) {
                FFSWAP(SwsOp, *op, *next);
                goto retry;
            }
            break;
        }
    }

    /* Apply any remaining preferential re-ordering optimizations; do these
     * last because they are more likely to block other optimizations if done
     * too aggressively */
    for (int n = 0; n < ops->num_ops - 1; n++) {
        SwsOp *op = &ops->ops[n];
        SwsOp *next = &ops->ops[n + 1];

        switch (op->op) {
        case SWS_OP_SWIZZLE: {
            /* Try to push swizzles towards the output */
            if (op_commute_swizzle(op, next)) {
                FFSWAP(SwsOp, *op, *next);
                goto retry;
            }
            break;
        }

        case SWS_OP_SCALE:
            /* Scaling by integer before conversion to int */
            if (op->scale.factor.den == 1 && next->op == SWS_OP_CONVERT &&
                ff_sws_pixel_type_is_int(next->convert.to))
            {
                op->type = next->convert.to;
                FFSWAP(SwsOp, *op, *next);
                goto retry;
            }
            break;
        }
    }

    return 0;
}

int ff_sws_solve_shuffle(const SwsOpList *const ops, uint8_t shuffle[],
                         int size, uint8_t clear_val,
                         int *read_bytes, int *write_bytes)
{
    if (!ops->num_ops)
        return AVERROR(EINVAL);

    const SwsOp *read = ff_sws_op_list_input(ops);
    if (!read || read->rw.frac || read->rw.filter ||
        (!read->rw.packed && read->rw.elems > 1))
        return AVERROR(ENOTSUP);

    const int read_size = ff_sws_pixel_type_size(read->type);
    uint32_t mask[4] = {0};
    for (int i = 0; i < read->rw.elems; i++)
        mask[i] = 0x01010101 * i * read_size + 0x03020100;

    for (int opidx = 1; opidx < ops->num_ops; opidx++) {
        const SwsOp *op = &ops->ops[opidx];
        switch (op->op) {
        case SWS_OP_SWIZZLE: {
            uint32_t orig[4] = { mask[0], mask[1], mask[2], mask[3] };
            for (int i = 0; i < 4; i++)
                mask[i] = orig[op->swizzle.in[i]];
            break;
        }

        case SWS_OP_SWAP_BYTES:
            for (int i = 0; i < 4; i++) {
                switch (ff_sws_pixel_type_size(op->type)) {
                case 2: mask[i] = av_bswap16(mask[i]); break;
                case 4: mask[i] = av_bswap32(mask[i]); break;
                }
            }
            break;

        case SWS_OP_CLEAR:
            for (int i = 0; i < 4; i++) {
                if (!SWS_COMP_TEST(op->clear.mask, i))
                    continue;
                if (op->clear.value[i].num != 0 || !clear_val)
                    return AVERROR(ENOTSUP);
                mask[i] = 0x1010101ul * clear_val;
            }
            break;

        case SWS_OP_CONVERT: {
            if (!op->convert.expand)
                return AVERROR(ENOTSUP);
            for (int i = 0; i < 4; i++) {
                switch (ff_sws_pixel_type_size(op->type)) {
                case 1: mask[i] = 0x01010101 * (mask[i] & 0xFF);   break;
                case 2: mask[i] = 0x00010001 * (mask[i] & 0xFFFF); break;
                }
            }
            break;
        }

        case SWS_OP_WRITE: {
            if (op->rw.frac || op->rw.filter ||
                (!op->rw.packed && op->rw.elems > 1))
                return AVERROR(ENOTSUP);

            /* Initialize to no-op */
            memset(shuffle, clear_val, size);

            const int write_size  = ff_sws_pixel_type_size(op->type);
            const int read_chunk  = read->rw.elems * read_size;
            const int write_chunk = op->rw.elems * write_size;
            const int num_groups  = size / FFMAX(read_chunk, write_chunk);
            for (int n = 0; n < num_groups; n++) {
                const int base_in  = n * read_chunk;
                const int base_out = n * write_chunk;
                for (int i = 0; i < op->rw.elems; i++) {
                    const int offset = base_out + i * write_size;
                    for (int b = 0; b < write_size; b++) {
                        const uint8_t idx = mask[i] >> (b * 8);
                        if (idx != clear_val)
                            shuffle[offset + b] = base_in + idx;
                    }
                }
            }

            *read_bytes  = num_groups * read_chunk;
            *write_bytes = num_groups * write_chunk;
            return num_groups;
        }

        default:
            return AVERROR(ENOTSUP);
        }
    }

    return AVERROR(EINVAL);
}

/**
 * Determine a suitable intermediate buffer format for a given combination
 * of pixel types and number of planes. The exact interpretation of these
 * formats does not matter at all; since they will only ever be used as
 * temporary intermediate buffers. We still need to pick *some* format as
 * a consequence of ff_sws_graph_add_pass() taking an AVPixelFormat for the
 * output buffer.
 */
static enum AVPixelFormat get_planar_fmt(SwsPixelType type, int nb_planes)
{
    switch (ff_sws_pixel_type_size(type)) {
    case 1:
        switch (nb_planes) {
        case 1: return AV_PIX_FMT_GRAY8;
        case 2: return AV_PIX_FMT_YUV444P; // FIXME: no 2-plane planar fmt
        case 3: return AV_PIX_FMT_YUV444P;
        case 4: return AV_PIX_FMT_YUVA444P;
        }
        break;
    case 2:
        switch (nb_planes) {
        case 1: return AV_PIX_FMT_GRAY16;
        case 2: return AV_PIX_FMT_YUV444P16; // FIXME: no 2-plane planar fmt
        case 3: return AV_PIX_FMT_YUV444P16;
        case 4: return AV_PIX_FMT_YUVA444P16;
        }
        break;
    case 4:
        switch (nb_planes) {
        case 1: return AV_PIX_FMT_GRAYF32;
        case 2: return AV_PIX_FMT_GBRPF32; // FIXME: no 2-plane planar fmt
        case 3: return AV_PIX_FMT_GBRPF32;
        case 4: return AV_PIX_FMT_GBRAPF32;
        }
        break;
    }

    av_unreachable("Invalid pixel type or number of planes?");
    return AV_PIX_FMT_NONE;
}

static void get_input_size(const SwsOpList *ops, SwsFormat *fmt)
{
    fmt->width  = ops->src.width;
    fmt->height = ops->src.height;

    const SwsOp *read = ff_sws_op_list_input(ops);
    if (read && read->rw.filter == SWS_OP_FILTER_V) {
        fmt->height = read->rw.kernel->dst_size;
    } else if (read && read->rw.filter == SWS_OP_FILTER_H) {
        fmt->width = read->rw.kernel->dst_size;
    }
}

int ff_sws_op_list_subpass(SwsOpList *ops1, SwsOpList **out_rest)
{
    const SwsOp *op;
    int ret, idx;

    for (idx = 0; idx < ops1->num_ops; idx++) {
        op = &ops1->ops[idx];
        if (op->op == SWS_OP_FILTER_H || op->op == SWS_OP_FILTER_V)
            break;
    }

    if (idx == ops1->num_ops) {
        *out_rest = NULL;
        return 0;
    }

    av_assert0(idx > 0);
    const SwsOp *prev = &ops1->ops[idx - 1];

    SwsOpList *ops2 = ff_sws_op_list_duplicate(ops1);
    if (!ops2)
        return AVERROR(ENOMEM);

    /**
     * Not all components may be needed; but we need the ones that *are*
     * used to be contiguous for the write/read operations. So, first
     * compress them into a linearly ascending list of components
     */
    int nb_planes = 0;
    SwsSwizzleOp swiz_wr = SWS_SWIZZLE(0, 1, 2, 3);
    SwsSwizzleOp swiz_rd = SWS_SWIZZLE(0, 1, 2, 3);
    for (int i = 0; i < 4; i++) {
        if (SWS_OP_NEEDED(prev, i)) {
            const int o = nb_planes++;
            swiz_wr.in[o] = i;
            swiz_rd.in[i] = o;
        }
    }

    /* Determine metadata for the intermediate format */
    const SwsPixelType type = op->type;
    ops2->src.format = get_planar_fmt(type, nb_planes);
    ops2->src.desc = av_pix_fmt_desc_get(ops2->src.format);
    get_input_size(ops1, &ops2->src);
    ops1->dst = ops2->src;

    for (int i = 0; i < nb_planes; i++) {
        ops1->plane_dst[i] = ops2->plane_src[i] = i;
        ops2->comps_src.flags[i] = prev->comps.flags[swiz_wr.in[i]];
    }

    ff_sws_op_list_remove_at(ops1, idx, ops1->num_ops - idx);
    ff_sws_op_list_remove_at(ops2, 0, idx);
    op = NULL; /* the above command may invalidate op */

    if (swiz_wr.mask != SWS_SWIZZLE(0, 1, 2, 3).mask) {
        ret = ff_sws_op_list_append(ops1, &(SwsOp) {
            .op      = SWS_OP_SWIZZLE,
            .type    = type,
            .swizzle = swiz_wr,
        });
        if (ret < 0)
            goto fail;
    }

    ret = ff_sws_op_list_append(ops1, &(SwsOp) {
        .op       = SWS_OP_WRITE,
        .type     = type,
        .rw.elems = nb_planes,
    });
    if (ret < 0)
        goto fail;

    ret = ff_sws_op_list_insert_at(ops2, 0, &(SwsOp) {
        .op        = SWS_OP_READ,
        .type      = type,
        .rw.elems  = nb_planes,
    });
    if (ret < 0)
        goto fail;

    if (swiz_rd.mask != SWS_SWIZZLE(0, 1, 2, 3).mask) {
        ret = ff_sws_op_list_insert_at(ops2, 1, &(SwsOp) {
            .op      = SWS_OP_SWIZZLE,
            .type    = type,
            .swizzle = swiz_rd,
        });
        if (ret < 0)
            goto fail;
    }

    ret = ff_sws_op_list_optimize(ops1);
    if (ret < 0)
        goto fail;

    ret = ff_sws_op_list_optimize(ops2);
    if (ret < 0)
        goto fail;

    *out_rest = ops2;
    return 0;

fail:
    ff_sws_op_list_free(&ops2);
    return ret;
}
