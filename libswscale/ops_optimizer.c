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
#include <libavutil/bswap.h>
#include "libavutil/rational.h"

#include "ops.h"
#include "ops_internal.h"

#define RET(x)                                                                 \
    do {                                                                       \
        if ((ret = (x)) < 0)                                                   \
            return ret;                                                        \
    } while (0)

/* Returns true for operations that are independent per channel. These can
 * usually be commuted freely other such operations. */
static bool op_type_is_independent(SwsOpType op)
{
    switch (op) {
    case SWS_OP_SWAP_BYTES:
    case SWS_OP_LSHIFT:
    case SWS_OP_RSHIFT:
    case SWS_OP_CONVERT:
    case SWS_OP_DITHER:
    case SWS_OP_MIN:
    case SWS_OP_MAX:
    case SWS_OP_SCALE:
        return true;
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

/* merge_comp_flags() forms a monoid with flags_identity as the null element */
static const unsigned flags_identity = SWS_COMP_ZERO | SWS_COMP_EXACT;
static unsigned merge_comp_flags(unsigned a, unsigned b)
{
    const unsigned flags_or  = SWS_COMP_GARBAGE;
    const unsigned flags_and = SWS_COMP_ZERO | SWS_COMP_EXACT;
    return ((a & b) & flags_and) | ((a | b) & flags_or);
}

/* Infer + propagate known information about components */
void ff_sws_op_list_update_comps(SwsOpList *ops)
{
    SwsComps next = { .unused = {true, true, true, true} };
    SwsComps prev = { .flags = {
        SWS_COMP_GARBAGE, SWS_COMP_GARBAGE, SWS_COMP_GARBAGE, SWS_COMP_GARBAGE,
    }};

    /* Forwards pass, propagates knowledge about the incoming pixel values */
    for (int n = 0; n < ops->num_ops; n++) {
        SwsOp *op = &ops->ops[n];

        /* Prefill min/max values automatically; may have to be fixed in
         * special cases */
        memcpy(op->comps.min, prev.min, sizeof(prev.min));
        memcpy(op->comps.max, prev.max, sizeof(prev.max));

        if (op->op != SWS_OP_SWAP_BYTES) {
            ff_sws_apply_op_q(op, op->comps.min);
            ff_sws_apply_op_q(op, op->comps.max);
        }

        switch (op->op) {
        case SWS_OP_READ:
            for (int i = 0; i < op->rw.elems; i++) {
                if (ff_sws_pixel_type_is_int(op->type)) {
                    int bits = 8 * ff_sws_pixel_type_size(op->type);
                    if (!op->rw.packed && ops->src.desc) {
                        /* Use legal value range from pixdesc if available;
                         * we don't need to do this for packed formats because
                         * non-byte-aligned packed formats will necessarily go
                         * through SWS_OP_UNPACK anyway */
                        for (int c = 0; c < 4; c++) {
                            if (ops->src.desc->comp[c].plane == i) {
                                bits = ops->src.desc->comp[c].depth;
                                break;
                            }
                        }
                    }

                    op->comps.flags[i] = SWS_COMP_EXACT;
                    op->comps.min[i] = Q(0);
                    op->comps.max[i] = Q((1ULL << bits) - 1);
                }
            }
            for (int i = op->rw.elems; i < 4; i++)
                op->comps.flags[i] = prev.flags[i];
            break;
        case SWS_OP_WRITE:
            for (int i = 0; i < op->rw.elems; i++)
                av_assert1(!(prev.flags[i] & SWS_COMP_GARBAGE));
            /* fall through */
        case SWS_OP_SWAP_BYTES:
        case SWS_OP_LSHIFT:
        case SWS_OP_RSHIFT:
        case SWS_OP_MIN:
        case SWS_OP_MAX:
            /* Linearly propagate flags per component */
            for (int i = 0; i < 4; i++)
                op->comps.flags[i] = prev.flags[i];
            break;
        case SWS_OP_DITHER:
            /* Strip zero flag because of the nonzero dithering offset */
            for (int i = 0; i < 4; i++)
                op->comps.flags[i] = prev.flags[i] & ~SWS_COMP_ZERO;
            break;
        case SWS_OP_UNPACK:
            for (int i = 0; i < 4; i++) {
                if (op->pack.pattern[i])
                    op->comps.flags[i] = prev.flags[0];
                else
                    op->comps.flags[i] = SWS_COMP_GARBAGE;
            }
            break;
        case SWS_OP_PACK: {
            unsigned flags = flags_identity;
            for (int i = 0; i < 4; i++) {
                if (op->pack.pattern[i])
                    flags = merge_comp_flags(flags, prev.flags[i]);
                if (i > 0) /* clear remaining comps for sanity */
                    op->comps.flags[i] = SWS_COMP_GARBAGE;
            }
            op->comps.flags[0] = flags;
            break;
        }
        case SWS_OP_CLEAR:
            for (int i = 0; i < 4; i++) {
                if (op->c.q4[i].den) {
                    if (op->c.q4[i].num == 0) {
                        op->comps.flags[i] = SWS_COMP_ZERO | SWS_COMP_EXACT;
                    } else if (op->c.q4[i].den == 1) {
                        op->comps.flags[i] = SWS_COMP_EXACT;
                    }
                } else {
                    op->comps.flags[i] = prev.flags[i];
                }
            }
            break;
        case SWS_OP_SWIZZLE:
            for (int i = 0; i < 4; i++)
                op->comps.flags[i] = prev.flags[op->swizzle.in[i]];
            break;
        case SWS_OP_CONVERT:
            for (int i = 0; i < 4; i++) {
                op->comps.flags[i] = prev.flags[i];
                if (ff_sws_pixel_type_is_int(op->convert.to))
                    op->comps.flags[i] |= SWS_COMP_EXACT;
            }
            break;
        case SWS_OP_LINEAR:
            for (int i = 0; i < 4; i++) {
                unsigned flags = flags_identity;
                AVRational min = Q(0), max = Q(0);
                for (int j = 0; j < 4; j++) {
                    const AVRational k = op->lin.m[i][j];
                    AVRational mink = av_mul_q(prev.min[j], k);
                    AVRational maxk = av_mul_q(prev.max[j], k);
                    if (k.num) {
                        flags = merge_comp_flags(flags, prev.flags[j]);
                        if (k.den != 1) /* fractional coefficient */
                            flags &= ~SWS_COMP_EXACT;
                        if (k.num < 0)
                            FFSWAP(AVRational, mink, maxk);
                        min = av_add_q(min, mink);
                        max = av_add_q(max, maxk);
                    }
                }
                if (op->lin.m[i][4].num) { /* nonzero offset */
                    flags &= ~SWS_COMP_ZERO;
                    if (op->lin.m[i][4].den != 1) /* fractional offset */
                        flags &= ~SWS_COMP_EXACT;
                    min = av_add_q(min, op->lin.m[i][4]);
                    max = av_add_q(max, op->lin.m[i][4]);
                }
                op->comps.flags[i] = flags;
                op->comps.min[i] = min;
                op->comps.max[i] = max;
            }
            break;
        case SWS_OP_SCALE:
            for (int i = 0; i < 4; i++) {
                op->comps.flags[i] = prev.flags[i];
                if (op->c.q.den != 1) /* fractional scale */
                    op->comps.flags[i] &= ~SWS_COMP_EXACT;
                if (op->c.q.num < 0)
                    FFSWAP(AVRational, op->comps.min[i], op->comps.max[i]);
            }
            break;

        case SWS_OP_INVALID:
        case SWS_OP_TYPE_NB:
            av_unreachable("Invalid operation type!");
        }

        prev = op->comps;
    }

    /* Backwards pass, solves for component dependencies */
    for (int n = ops->num_ops - 1; n >= 0; n--) {
        SwsOp *op = &ops->ops[n];

        switch (op->op) {
        case SWS_OP_READ:
        case SWS_OP_WRITE:
            for (int i = 0; i < op->rw.elems; i++)
                op->comps.unused[i] = op->op == SWS_OP_READ;
            for (int i = op->rw.elems; i < 4; i++)
                op->comps.unused[i] = next.unused[i];
            break;
        case SWS_OP_SWAP_BYTES:
        case SWS_OP_LSHIFT:
        case SWS_OP_RSHIFT:
        case SWS_OP_CONVERT:
        case SWS_OP_DITHER:
        case SWS_OP_MIN:
        case SWS_OP_MAX:
        case SWS_OP_SCALE:
            for (int i = 0; i < 4; i++)
                op->comps.unused[i] = next.unused[i];
            break;
        case SWS_OP_UNPACK: {
            bool unused = true;
            for (int i = 0; i < 4; i++) {
                if (op->pack.pattern[i])
                    unused &= next.unused[i];
                op->comps.unused[i] = i > 0;
            }
            op->comps.unused[0] = unused;
            break;
        }
        case SWS_OP_PACK:
            for (int i = 0; i < 4; i++) {
                if (op->pack.pattern[i])
                    op->comps.unused[i] = next.unused[0];
                else
                    op->comps.unused[i] = true;
            }
            break;
        case SWS_OP_CLEAR:
            for (int i = 0; i < 4; i++) {
                if (op->c.q4[i].den)
                    op->comps.unused[i] = true;
                else
                    op->comps.unused[i] = next.unused[i];
            }
            break;
        case SWS_OP_SWIZZLE: {
            bool unused[4] = { true, true, true, true };
            for (int i = 0; i < 4; i++)
                unused[op->swizzle.in[i]] &= next.unused[i];
            for (int i = 0; i < 4; i++)
                op->comps.unused[i] = unused[i];
            break;
        }
        case SWS_OP_LINEAR:
            for (int j = 0; j < 4; j++) {
                bool unused = true;
                for (int i = 0; i < 4; i++) {
                    if (op->lin.m[i][j].num)
                        unused &= next.unused[i];
                }
                op->comps.unused[j] = unused;
            }
            break;
        }

        next = op->comps;
    }
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
static bool extract_scalar(const SwsLinearOp *c, SwsComps prev, SwsComps next,
                           SwsConst *out_scale)
{
    SwsConst scale = {0};

    /* There are components not on the main diagonal */
    if (c->mask & ~SWS_MASK_DIAG4)
        return false;

    for (int i = 0; i < 4; i++) {
        const AVRational s = c->m[i][i];
        if ((prev.flags[i] & SWS_COMP_ZERO) || next.unused[i])
            continue;
        if (scale.q.den && av_cmp_q(s, scale.q))
            return false;
        scale.q = s;
    }

    if (scale.q.den)
        *out_scale = scale;
    return scale.q.den;
}

/* Extracts an integer clear operation (subset) from the given linear op. */
static bool extract_constant_rows(SwsLinearOp *c, SwsComps prev,
                                  SwsConst *out_clear)
{
    SwsConst clear = {0};
    bool ret = false;

    for (int i = 0; i < 4; i++) {
        bool const_row = c->m[i][4].den == 1; /* offset is integer */
        for (int j = 0; j < 4; j++) {
            const_row &= c->m[i][j].num == 0 || /* scalar is zero */
                         (prev.flags[j] & SWS_COMP_ZERO); /* input is zero */
        }
        if (const_row && (c->mask & SWS_MASK_ROW(i))) {
            clear.q4[i] = c->m[i][4];
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

    for (int i = 0; i < 4; i++) {
        int idx = -1;
        for (int j = 0; j < 4; j++) {
            if (!c.m[i][j].num || (prev.flags[j] & SWS_COMP_ZERO))
                continue;
            if (idx >= 0)
                return false; /* multiple inputs */
            idx = j;
        }

        if (idx >= 0 && idx != i) {
            /* Move coefficient to the diagonal */
            c.m[i][i] = c.m[i][idx];
            c.m[i][idx] = Q(0);
            swiz.in[i] = idx;
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

    for (int n = 0; n < ops->num_ops;) {
        SwsOp dummy = {0};
        SwsOp *op = &ops->ops[n];
        SwsOp *prev = n ? &ops->ops[n - 1] : &dummy;
        SwsOp *next = n + 1 < ops->num_ops ? &ops->ops[n + 1] : &dummy;

        /* common helper variable */
        bool noop = true;

        switch (op->op) {
        case SWS_OP_READ:
            /* Optimized further into refcopy / memcpy */
            if (next->op == SWS_OP_WRITE &&
                next->rw.elems == op->rw.elems &&
                next->rw.packed == op->rw.packed &&
                next->rw.frac == op->rw.frac)
            {
                ff_sws_op_list_remove_at(ops, n, 2);
                av_assert1(ops->num_ops == 0);
                return 0;
            }

            /* Skip reading extra unneeded components */
            if (!op->rw.packed) {
                int needed = op->rw.elems;
                while (needed > 0 && next->comps.unused[needed - 1])
                    needed--;
                if (op->rw.elems != needed) {
                    op->rw.elems = needed;
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
                op->c.u += next->c.u;
                ff_sws_op_list_remove_at(ops, n + 1, 1);
                goto retry;
            }

            /* No-op shift */
            if (!op->c.u) {
                ff_sws_op_list_remove_at(ops, n, 1);
                goto retry;
            }
            break;

        case SWS_OP_CLEAR:
            for (int i = 0; i < 4; i++) {
                if (!op->c.q4[i].den)
                    continue;

                if ((prev->comps.flags[i] & SWS_COMP_ZERO) &&
                    !(prev->comps.flags[i] & SWS_COMP_GARBAGE) &&
                    op->c.q4[i].num == 0)
                {
                    /* Redundant clear-to-zero of zero component */
                    op->c.q4[i].den = 0;
                } else if (next->comps.unused[i]) {
                    /* Unnecessary clear of unused component */
                    op->c.q4[i] = (AVRational) {0, 0};
                } else if (op->c.q4[i].den) {
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
                    if (next->c.q4[i].den)
                        op->c.q4[i] = next->c.q4[i];
                }
                ff_sws_op_list_remove_at(ops, n + 1, 1);
                goto retry;
            }

            /* Prefer to clear as late as possible, to avoid doing
             * redundant work */
            if ((op_type_is_independent(next->op) && next->op != SWS_OP_SWAP_BYTES) ||
                next->op == SWS_OP_SWIZZLE)
            {
                if (next->op == SWS_OP_CONVERT)
                    op->type = next->convert.to;
                ff_sws_apply_op_q(next, op->c.q4);
                FFSWAP(SwsOp, *op, *next);
                goto retry;
            }
            break;

        case SWS_OP_SWIZZLE: {
            bool seen[4] = {0};
            bool has_duplicates = false;
            for (int i = 0; i < 4; i++) {
                if (next->comps.unused[i])
                    continue;
                if (op->swizzle.in[i] != i)
                    noop = false;
                has_duplicates |= seen[op->swizzle.in[i]];
                seen[op->swizzle.in[i]] = true;
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

            /* Try to push swizzles with duplicates towards the output */
            if (has_duplicates && op_type_is_independent(next->op)) {
                if (next->op == SWS_OP_CONVERT)
                    op->type = next->convert.to;
                if (next->op == SWS_OP_MIN || next->op == SWS_OP_MAX) {
                    /* Un-swizzle the next operation */
                    const SwsConst c = next->c;
                    for (int i = 0; i < 4; i++) {
                        if (!next->comps.unused[i])
                            next->c.q4[op->swizzle.in[i]] = c.q4[i];
                    }
                }
                FFSWAP(SwsOp, *op, *next);
                goto retry;
            }

            /* Move swizzle out of the way between two converts so that
             * they may be merged */
            if (prev->op == SWS_OP_CONVERT && next->op == SWS_OP_CONVERT) {
                op->type = next->convert.to;
                FFSWAP(SwsOp, *op, *next);
                goto retry;
            }
            break;
        }

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
                !av_cmp_q(next->c.q, ff_sws_pixel_expand(op->type, op->convert.to)))
            {
                op->convert.expand = true;
                ff_sws_op_list_remove_at(ops, n + 1, 1);
                goto retry;
            }
            break;

        case SWS_OP_MIN:
            for (int i = 0; i < 4; i++) {
                if (next->comps.unused[i] || !op->c.q4[i].den)
                    continue;
                if (av_cmp_q(op->c.q4[i], prev->comps.max[i]) < 0)
                    noop = false;
            }

            if (noop) {
                ff_sws_op_list_remove_at(ops, n, 1);
                goto retry;
            }
            break;

        case SWS_OP_MAX:
            for (int i = 0; i < 4; i++) {
                if (next->comps.unused[i] || !op->c.q4[i].den)
                    continue;
                if (av_cmp_q(prev->comps.min[i], op->c.q4[i]) < 0)
                    noop = false;
            }

            if (noop) {
                ff_sws_op_list_remove_at(ops, n, 1);
                goto retry;
            }
            break;

        case SWS_OP_DITHER:
            for (int i = 0; i < 4; i++) {
                noop &= (prev->comps.flags[i] & SWS_COMP_EXACT) ||
                        next->comps.unused[i];
            }

            if (noop) {
                ff_sws_op_list_remove_at(ops, n, 1);
                goto retry;
            }
            break;

        case SWS_OP_LINEAR: {
            SwsSwizzleOp swizzle;
            SwsConst c;

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
                if (!next->comps.unused[i] || !(op->lin.mask & row))
                    continue;
                for (int j = 0; j < 5; j++)
                    op->lin.m[i][j] = Q(i == j);
                op->lin.mask &= ~row;
                goto retry;
            }

            /* Convert constant rows to explicit clear instruction */
            if (extract_constant_rows(&op->lin, prev->comps, &c)) {
                RET(ff_sws_op_list_insert_at(ops, n + 1, &(SwsOp) {
                    .op    = SWS_OP_CLEAR,
                    .type  = op->type,
                    .comps = op->comps,
                    .c     = c,
                }));
                goto retry;
            }

            /* Multiplication by scalar constant */
            if (extract_scalar(&op->lin, prev->comps, next->comps, &c)) {
                op->op = SWS_OP_SCALE;
                op->c  = c;
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
            const int factor2 = exact_log2_q(op->c.q);

            /* No-op scaling */
            if (op->c.q.num == 1 && op->c.q.den == 1) {
                ff_sws_op_list_remove_at(ops, n, 1);
                goto retry;
            }

            /* Scaling by integer before conversion to int */
            if (op->c.q.den == 1 &&
                next->op == SWS_OP_CONVERT &&
                ff_sws_pixel_type_is_int(next->convert.to))
            {
                op->type = next->convert.to;
                FFSWAP(SwsOp, *op, *next);
                goto retry;
            }

            /* Scaling by exact power of two */
            if (factor2 && ff_sws_pixel_type_is_int(op->type)) {
                op->op = factor2 > 0 ? SWS_OP_LSHIFT : SWS_OP_RSHIFT;
                op->c.u = FFABS(factor2);
                goto retry;
            }
            break;
        }
        }

        /* No optimization triggered, move on to next operation */
        n++;
    }

    return 0;
}

int ff_sws_solve_shuffle(const SwsOpList *const ops, uint8_t shuffle[],
                         int size, uint8_t clear_val,
                         int *read_bytes, int *write_bytes)
{
    const SwsOp read = ops->ops[0];
    const int read_size = ff_sws_pixel_type_size(read.type);
    uint32_t mask[4] = {0};

    if (!ops->num_ops || read.op != SWS_OP_READ)
        return AVERROR(EINVAL);
    if (read.rw.frac || (!read.rw.packed && read.rw.elems > 1))
        return AVERROR(ENOTSUP);

    for (int i = 0; i < read.rw.elems; i++)
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
                if (!op->c.q4[i].den)
                    continue;
                if (op->c.q4[i].num != 0 || !clear_val)
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
            if (op->rw.frac || (!op->rw.packed && op->rw.elems > 1))
                return AVERROR(ENOTSUP);

            /* Initialize to no-op */
            memset(shuffle, clear_val, size);

            const int write_size  = ff_sws_pixel_type_size(op->type);
            const int read_chunk  = read.rw.elems * read_size;
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
