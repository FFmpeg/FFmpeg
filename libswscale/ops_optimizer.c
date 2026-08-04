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
        ff_sws_apply_op_q(next, op->clear.value);
        return true;
    case SWS_OP_FILTER_H:
    case SWS_OP_FILTER_V:
        op->type = next->filter.type;
        return true;
    case SWS_OP_SWIZZLE:
        ff_sws_comp_mask_swizzle(&op->clear.mask, &next->swizzle);
        ff_sws_apply_op_q(next, op->clear.value);
        return true;
    case SWS_OP_SWAP_BYTES:
        switch (next->type) {
        case SWS_PIXEL_U16:
        case SWS_PIXEL_U32:
            ff_sws_apply_op_q(next, op->clear.value); /* always representable */
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
    case SWS_OP_LUT_3D:
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
        return true;
    case SWS_OP_FILTER_H:
    case SWS_OP_FILTER_V:
        op->type = next->filter.type;
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
            if (seen[j] && av_cmp_q64(next->clamp.limit[j], c.limit[i]))
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
    case SWS_OP_LUT_3D:
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
    av_assert0(!ff_sws_pixel_type_is_int(op->filter.type));

    switch (prev->op) {
    case SWS_OP_SWIZZLE:
    case SWS_OP_SCALE:
    case SWS_OP_LINEAR:
    case SWS_OP_DITHER:
        prev->type = op->filter.type;
        return true;
    case SWS_OP_CONVERT:
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
    case SWS_OP_LUT_3D:
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

static int exact_log2_q64(const AVRational64 x)
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
static bool extract_scalar(const SwsLinearOp *c,
                           const SwsComps *comps, const SwsComps *prev,
                           SwsScaleOp *out_scale)
{
    SwsScaleOp scale = {0};

    /* There are components not on the main diagonal */
    if (ff_sws_linear_mask(c) & ~SWS_MASK_DIAG4)
        return false;

    for (int i = 0; i < 4; i++) {
        const AVRational64 s = c->m[i][i];
        if ((prev->flags[i]  & SWS_COMP_ZERO) ||
            (comps->flags[i] & SWS_COMP_GARBAGE))
            continue;
        if (scale.factor.den && av_cmp_q64(s, scale.factor))
            return false;
        scale.factor = s;
    }

    if (scale.factor.den)
        *out_scale = scale;
    return scale.factor.den;
}

/* Extracts an integer clear operation (subset) from the given linear op. */
static bool extract_constant_rows(SwsLinearOp *c, const SwsComps *prev,
                                  SwsClearOp *out_clear)
{
    const uint32_t mask = ff_sws_linear_mask(c);
    SwsClearOp clear = {0};
    bool ret = false;

    for (int i = 0; i < 4; i++) {
        bool const_row = c->m[i][4].den == 1; /* offset is integer */
        for (int j = 0; j < 4; j++) {
            const_row &= c->m[i][j].num == 0 || /* scalar is zero */
                         (prev->flags[j] & SWS_COMP_ZERO); /* input is zero */
        }
        if (const_row && (mask & SWS_MASK_ROW(i))) {
            clear.mask |= SWS_COMP(i);
            clear.value[i] = c->m[i][4];
            for (int j = 0; j < 5; j++)
                c->m[i][j] = Q(i == j);
            ret = true;
        }
    }

    if (ret)
        *out_clear = clear;
    return ret;
}

/* Unswizzle a linear operation by aligning single-input rows with
 * their corresponding diagonal */
static bool extract_swizzle(SwsLinearOp *op, const SwsComps *prev,
                            SwsSwizzleOp *out_swiz)
{
    SwsSwizzleOp swiz = SWS_SWIZZLE(0, 1, 2, 3);
    SwsLinearOp c = *op;

    /* Find non-zero coefficients in the main 4x4 matrix */
    uint32_t nonzero = 0;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            if (!c.m[i][j].num || (prev->flags[j] & SWS_COMP_ZERO))
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

    *out_swiz = swiz;
    *op = c;
    return true;
}

static int op_result_is_exact(const SwsOp *op)
{
    for (int i = 0; i < 4; i++) {
        if (SWS_OP_NEEDED(op, i) && !(op->comps.flags[i] & SWS_COMP_EXACT))
            return false;
    }

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

            /* Merge filter with prior conversion */
            if (prev->op == SWS_OP_CONVERT && !prev->convert.expand) {
                int size_from = ff_sws_pixel_type_size(prev->type);
                int size_to   = ff_sws_pixel_type_size(op->type);
                av_assert1(prev->convert.to == op->type);
                if (size_from < size_to) {
                    op->type = prev->type;
                    ff_sws_op_list_remove_at(ops, n - 1, 1);
                    goto retry;
                }
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
        const SwsCompMask needed = ff_sws_comp_mask_needed(op);
        bool noop = true;

        if (!needed && op->op != SWS_OP_WRITE) {
            /* Remove any operation whose output is not needed */
            ff_sws_op_list_remove_at(ops, n, 1);
            goto retry;
        }

        switch (op->op) {
        case SWS_OP_READ:
            /* "Compress" planar reads where not all components are needed */
            if (op->rw.mode == SWS_RW_PLANAR) {
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
                        .type = op->rw.filter.op ? op->rw.filter.type : op->type,
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
            if (prev->op == SWS_OP_READ && prev->rw.mode == SWS_RW_PLANAR) {
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

            if (next->op == SWS_OP_WRITE && next->rw.mode == SWS_RW_PLANAR) {
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
                !av_cmp_q64(next->scale.factor,
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
                if (av_cmp_q64(op->clamp.limit[i], prev->comps.max[i]) >= 0)
                    op->clamp.limit[i] = (AVRational64) {0}; /* no-op */
                else
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
                if (av_cmp_q64(prev->comps.min[i], op->clamp.limit[i]) >= 0)
                    op->clamp.limit[i] = (AVRational64) {0};
                else
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
            const uint32_t mask = ff_sws_linear_mask(&op->lin);
            SwsSwizzleOp swizzle;
            SwsClearOp clear;
            SwsScaleOp scale;

            /* No-op (identity) linear operation */
            if (!mask) {
                ff_sws_op_list_remove_at(ops, n, 1);
                goto retry;
            }

            if (next->op == SWS_OP_LINEAR) {
                /* 5x5 matrix multiplication after appending [ 0 0 0 0 1 ] */
                const SwsLinearOp m1 = op->lin;
                const SwsLinearOp m2 = next->lin;
                for (int i = 0; i < 4; i++) {
                    for (int j = 0; j < 5; j++) {
                        AVRational64 sum = Q(0);
                        for (int k = 0; k < 4; k++)
                            sum = av_add_q64(sum, av_mul_q64(m2.m[i][k], m1.m[k][j]));
                        if (j == 4) /* m1.m[4][j] == 1 */
                            sum = av_add_q64(sum, m2.m[i][4]);
                        op->lin.m[i][j] = sum;
                    }
                }
                ff_sws_op_list_remove_at(ops, n + 1, 1);
                goto retry;
            }

            /* Optimize away zero columns */
            for (int j = 0; j < 4; j++) {
                const uint32_t col = SWS_MASK_COL(j);
                if (!(prev->comps.flags[j] & SWS_COMP_ZERO) || !(mask & col))
                    continue;
                for (int i = 0; i < 4; i++)
                    op->lin.m[i][j] = Q(i == j);
                goto retry;
            }

            /* Optimize away unused rows */
            for (int i = 0; i < 4; i++) {
                const uint32_t row = SWS_MASK_ROW(i);
                if (SWS_OP_NEEDED(op, i) || !(mask & row))
                    continue;
                for (int j = 0; j < 5; j++)
                    op->lin.m[i][j] = Q(i == j);
                goto retry;
            }

            /* Convert constant rows to explicit clear instruction */
            if (extract_constant_rows(&op->lin, &prev->comps, &clear)) {
                RET(ff_sws_op_list_insert_at(ops, n + 1, &(SwsOp) {
                    .op    = SWS_OP_CLEAR,
                    .type  = op->type,
                    .comps = op->comps,
                    .clear = clear,
                }));
                goto retry;
            }

            /* Multiplication by scalar constant */
            if (extract_scalar(&op->lin, &op->comps, &prev->comps, &scale)) {
                op->op    = SWS_OP_SCALE;
                op->scale = scale;
                goto retry;
            }

            /* Swizzle by fixed pattern */
            if (extract_swizzle(&op->lin, &prev->comps, &swizzle)) {
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
            const int factor2 = exact_log2_q64(op->scale.factor);

            /* No-op scaling */
            if (op->scale.factor.num == 1 && op->scale.factor.den == 1) {
                ff_sws_op_list_remove_at(ops, n, 1);
                goto retry;
            }

            /* Merge consecutive scaling operations */
            if (next->op == SWS_OP_SCALE) {
                op->scale.factor = av_mul_q64(op->scale.factor, next->scale.factor);
                ff_sws_op_list_remove_at(ops, n + 1, 1);
                goto retry;
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
            if (prev->op == SWS_OP_READ && !prev->rw.filter.op &&
                prev->rw.mode == SWS_RW_PLANAR && !prev->rw.frac) {
                prev->rw.filter.op = op->op;
                prev->rw.filter.kernel = av_refstruct_ref(op->filter.kernel);
                prev->rw.filter.type = op->filter.type;
                ff_sws_op_list_remove_at(ops, n, 1);
                goto retry;
            }
            break;

        case SWS_OP_LUT_3D:
            /* Eliminate unnecessary 3DLUT */
            if (!(needed & SWS_COMP_ELEMS(3))) {
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
            /* Exact integer multiplication */
            if (op->scale.factor.den == 1 && next->op == SWS_OP_CONVERT &&
                ff_sws_pixel_type_is_int(next->convert.to) &&
                op_result_is_exact(op))
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

static int select_planes(SwsOpList *ops, SwsCompMask planes)
{
    SwsSwizzleOp swiz = SWS_SWIZZLE(0, 1, 2, 3);
    SwsOp *write = &ops->ops[ops->num_ops - 1];
    av_assert0(write->op == SWS_OP_WRITE);

    write->rw.elems = 0;
    for (int src = 0; src < 4; src++) {
        if (!SWS_COMP_TEST(planes, src))
            continue; /* plane not selected */
        const int dst = write->rw.elems++;
        av_assert2(src >= dst);
        swiz.in[dst] = src;
        FFSWAP(int, ops->plane_dst[dst], ops->plane_dst[src]);
    }

    /* Insert swizzle to select desired planes */
    int ret = ff_sws_op_list_insert_at(ops, ops->num_ops - 1, &(SwsOp) {
        .op      = SWS_OP_SWIZZLE,
        .type    = write->type,
        .swizzle = swiz,
    });
    if (ret < 0)
        return ret;

    /* The optimizer will take care of the rest */
    return ff_sws_op_list_optimize(ops);
}

int ff_sws_op_list_split_planes(SwsOpList *ops1, SwsOpList **out_ops2, SwsCompMask planes)
{
    const SwsOp *write = ff_sws_op_list_output(ops1);
    if (!write || write->rw.mode != SWS_RW_PLANAR) {
        *out_ops2 = NULL;
        return 0;
    }

    const SwsCompMask full = SWS_COMP_ELEMS(write->rw.elems);
    const SwsCompMask mask1 = planes & full;
    const SwsCompMask mask2 = full ^ mask1;
    if (!mask1 || !mask2) {
        /* Nothing to filter */
        *out_ops2 = NULL;
        return 0;
    }

    SwsOpList *ops2 = ff_sws_op_list_duplicate(ops1);
    if (!ops2)
        return AVERROR(ENOMEM);

    int ret;
    if ((ret = select_planes(ops1, mask1)) < 0 ||
        (ret = select_planes(ops2, mask2)) < 0)
    {
        ff_sws_op_list_free(&ops2);
        return ret;
    }

    *out_ops2 = ops2;
    return 0;
}

int ff_sws_shuffle_mask(const SwsUOp *uop, int8_t shuffle[], int size)
{
    const SwsShuffleUOp *par = &uop->par.shuffle;
    av_assert1(uop->uop == SWS_UOP_RW_SHUFFLE);
    av_assert1(par->write_size <= sizeof(uop->data.shuffle.mask));
    av_assert1(size <= INT8_MAX);

    const int num_groups = size / FFMAX(par->read_size, par->write_size);
    if (!num_groups)
        return AVERROR(EINVAL);

    memset(shuffle, 0, size);
    for (int n = 0; n < num_groups; n++) {
        const int base_in  = n * par->read_size;
        const int base_out = n * par->write_size;
        for (int i = 0; i < par->write_size; i++) {
            const int8_t idx = uop->data.shuffle.mask[i];
            shuffle[base_out + i] = idx + (idx >= 0) * base_in;
        }
    }

    return num_groups;
}

static bool pixel_is_repeating(SwsPixelType type, SwsPixel val)
{
    switch (ff_sws_pixel_type_size(type)) {
    case 1: return true;
    case 2: return val.u16 == val.u8 * 0x101ul;
    case 4: return val.u32 == val.u8 * 0x1010101ul;
    default: break;
    }

    av_unreachable("Invalid pixel type!");
    return false;
}

static int solve_shuffle(const SwsUOpList *const uops, SwsUOp *out)
{
    if (!uops->num_ops)
        return AVERROR(EINVAL);
    const SwsUOp *read = &uops->ops[0];
    switch (read->uop) {
    case SWS_UOP_READ_PACKED:
        break;
    case SWS_UOP_READ_PLANAR:
        if (read->mask != SWS_COMP_ELEMS(1))
             return AVERROR(ENOTSUP);
        break;
    default:
        return AVERROR(ENOTSUP);
    }

    const int read_size = ff_sws_pixel_type_size(read->type);
    uint32_t mask[4] = {0};
    int clear_val = -1;
    int read_elems = 0;
    for (int i = 0; i < 4; i++) {
        if (SWS_COMP_TEST(read->mask, i)) {
            mask[i] = 0x01010101 * i * read_size + 0x03020100;
            read_elems++;
        }
    }

    for (int opidx = 1; opidx < uops->num_ops; opidx++) {
        const SwsUOp *uop = &uops->ops[opidx];
        const SwsUOpParams *par = &uop->par;
        switch (uop->uop) {
        case SWS_UOP_COPY:
        case SWS_UOP_PERMUTE: {
            uint32_t tmp;
            for (int i = 0; i < par->move.num_moves; i++) {
                const int dst_idx = par->move.dst[i];
                const int src_idx = par->move.src[i];
                uint32_t *src = src_idx < 0 ? &tmp : &mask[src_idx];
                uint32_t *dst = dst_idx < 0 ? &tmp : &mask[dst_idx];
                *dst = *src;
            }
            break;
        }

        case SWS_UOP_SWAP_BYTES:
            for (int i = 0; i < 4; i++) {
                switch (ff_sws_pixel_type_size(uop->type)) {
                case 2: mask[i] = av_bswap16(mask[i]); break;
                case 4: mask[i] = av_bswap32(mask[i]); break;
                }
            }
            break;

        case SWS_UOP_CLEAR:
            for (int i = 0; i < 4; i++) {
                if (!SWS_COMP_TEST(uop->mask, i))
                    continue;
                SwsPixel val = uop->data.vec4[i];
                if (!pixel_is_repeating(uop->type, val) ||
                    (clear_val >= 0 && clear_val != val.u8))
                    return AVERROR(ENOTSUP); /* would require different bytes */
                mask[i] = 0xFFFFFFFFul; /* (uint8_t[4]) { -1, -1, -1, -1 } */
                clear_val = val.u8;
            }
            break;

        case SWS_UOP_EXPAND_PAIR:
        case SWS_UOP_EXPAND_QUAD:
            for (int i = 0; i < 4; i++)
                mask[i] = 0x01010101 * (mask[i] & 0xFF);
            break;

        case SWS_UOP_WRITE_PLANAR:
            if (uop->mask != SWS_COMP_ELEMS(1))
                return AVERROR(ENOTSUP);
            av_fallthrough;
        case SWS_UOP_WRITE_PACKED: {
            const int write_elems = av_popcount(uop->mask);
            const int write_size  = ff_sws_pixel_type_size(uop->type);
            *out = (SwsUOp) {
                .uop  = SWS_UOP_RW_SHUFFLE,
                .type = SWS_PIXEL_U8,
                .mask = SWS_COMP_ELEMS(1), /* single plane for now */
            };

            SwsShuffleUOp *par = &out->par.shuffle;
            SwsShuffleMask *data = &out->data.shuffle;
            *par = (SwsShuffleUOp) {
                .read_size   = read_elems * read_size,
                .write_size  = write_elems * write_size,
                .clear_value = clear_val >= 0 ? clear_val : 0,
            };

            /* Generate baseline shuffle for a single pixel */
            data->pixels = 1;
            for (int i = 0; i < write_elems; i++) {
                const int offset = i * write_size;
                for (int b = 0; b < write_size; b++)
                    data->mask[offset + b] = mask[i] >> (b * 8);
            }

            /* Expand as many times as needed to round up to the size of the
             * shuffle uop data mask */
            int8_t tmp[FF_ARRAY_ELEMS(data->mask)];
            const int num_groups = ff_sws_shuffle_mask(out, tmp, sizeof(tmp));
            if (num_groups < 0)
                return num_groups;
            memcpy(data->mask, tmp, sizeof(tmp));
            par->read_size  *= num_groups;
            par->write_size *= num_groups;
            data->pixels = num_groups;
            return 0;
        }

        default:
            return AVERROR(ENOTSUP);
        }
    }

    return AVERROR(EINVAL);
}

int ff_sws_uop_list_optimize(SwsContext *ctx, SwsUOpFlags flags, SwsUOpList *uops)
{
    /* Try promoting the entire uop list to a packed shuffle operation */
    if (flags & SWS_UOP_FLAG_PSHUFB) {
        SwsUOp shuffle;
        int ret = solve_shuffle(uops, &shuffle);
        if (ret >= 0) {
            ff_sws_uop_list_remove_at(uops, 0, uops->num_ops);
            return ff_sws_uop_list_append(uops, &shuffle);
        } else if (ret < 0 && ret != AVERROR(ENOTSUP)) {
            return ret;
        }
    }

#if 0
    static const SwsUOp dummy = {0};

retry:
    for (int i = 0; i < uops->num_ops; i++) {
        const SwsUOp *next = i < uops->num_ops - 1 ? &uops->ops[i + 1] : &dummy;
        SwsUOp *op = &uops->ops[i];

        switch (op->uop) {
            /* placeholder */
        }
    }
#endif

    return 0;
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
    if (read && read->rw.filter.op == SWS_OP_FILTER_V) {
        fmt->height = read->rw.filter.kernel->dst_size;
    } else if (read && read->rw.filter.op == SWS_OP_FILTER_H) {
        fmt->width = read->rw.filter.kernel->dst_size;
    }
}

int ff_sws_op_list_split_at(SwsOpList *ops1, SwsOpList **out_ops2, int index)
{
    int ret;
    if (index <= 0 || index >= ops1->num_ops) {
        *out_ops2 = NULL;
        return 0;
    }

    const SwsOp *op = &ops1->ops[index];
    const SwsOp *prev = &ops1->ops[index - 1];

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
        const int idx = swiz_wr.in[i];
        ops1->plane_dst[i] = ops2->plane_src[i] = i;
        ops2->comps_src.flags[i]  = prev->comps.flags[idx];
        ops2->comps_src.min[i]    = prev->comps.min[idx];
        ops2->comps_src.max[i]    = prev->comps.max[idx];
    }

    ff_sws_op_list_remove_at(ops1, index, ops1->num_ops - index);
    ff_sws_op_list_remove_at(ops2, 0, index);
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

    *out_ops2 = ops2;
    return 0;

fail:
    ff_sws_op_list_free(&ops2);
    return ret;
}
