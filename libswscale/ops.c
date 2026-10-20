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
#include "libavutil/avstring.h"
#include "libavutil/bprint.h"
#include "libavutil/bswap.h"
#include "libavutil/mem.h"
#include "libavutil/rational.h"
#include "libavutil/refstruct.h"

#include "format.h"
#include "ops.h"
#include "ops_internal.h"

extern const SwsOpBackend backend_c;
extern const SwsOpBackend backend_murder;
extern const SwsOpBackend backend_aarch64;
extern const SwsOpBackend backend_x86;
#if CONFIG_SWSCALE_VULKAN
extern const SwsOpBackend backend_spirv;
#endif

const SwsOpBackend * const ff_sws_op_backends[] = {
    &backend_murder,
#if ARCH_AARCH64 && HAVE_NEON
    &backend_aarch64,
#elif ARCH_X86_64 && HAVE_X86ASM
    &backend_x86,
#endif
    &backend_c,
#if CONFIG_SWSCALE_VULKAN
    &backend_spirv,
#endif
    NULL
};

const char *ff_sws_pixel_type_name(SwsPixelType type)
{
    switch (type) {
    case SWS_PIXEL_U8:   return "u8";
    case SWS_PIXEL_U16:  return "u16";
    case SWS_PIXEL_U32:  return "u32";
    case SWS_PIXEL_F32:  return "f32";
    case SWS_PIXEL_NONE: return "none";
    case SWS_PIXEL_TYPE_NB: break;
    }

    av_unreachable("Invalid pixel type!");
    return "ERR";
}

const char *ff_sws_op_type_name(SwsOpType op)
{
    switch (op) {
    case SWS_OP_READ:        return "SWS_OP_READ";
    case SWS_OP_WRITE:       return "SWS_OP_WRITE";
    case SWS_OP_SWAP_BYTES:  return "SWS_OP_SWAP_BYTES";
    case SWS_OP_SWIZZLE:     return "SWS_OP_SWIZZLE";
    case SWS_OP_UNPACK:      return "SWS_OP_UNPACK";
    case SWS_OP_PACK:        return "SWS_OP_PACK";
    case SWS_OP_LSHIFT:      return "SWS_OP_LSHIFT";
    case SWS_OP_RSHIFT:      return "SWS_OP_RSHIFT";
    case SWS_OP_CLEAR:       return "SWS_OP_CLEAR";
    case SWS_OP_CONVERT:     return "SWS_OP_CONVERT";
    case SWS_OP_MIN:         return "SWS_OP_MIN";
    case SWS_OP_MAX:         return "SWS_OP_MAX";
    case SWS_OP_SCALE:       return "SWS_OP_SCALE";
    case SWS_OP_LINEAR:      return "SWS_OP_LINEAR";
    case SWS_OP_DITHER:      return "SWS_OP_DITHER";
    case SWS_OP_FILTER_H:    return "SWS_OP_FILTER_H";
    case SWS_OP_FILTER_V:    return "SWS_OP_FILTER_V";
    case SWS_OP_LUT_3D:      return "SWS_OP_LUT_3D";
    case SWS_OP_INVALID:     return "SWS_OP_INVALID";
    case SWS_OP_TYPE_NB: break;
    }

    av_unreachable("Invalid operation type!");
    return "ERR";
}

SwsCompMask ff_sws_comp_mask_q4(const AVRational64 q[4])
{
    SwsCompMask mask = 0;
    for (int i = 0; i < 4; i++) {
        if (q[i].den)
            mask |= SWS_COMP(i);
    }
    return mask;
}

void ff_sws_comp_mask_swizzle(SwsCompMask *mask, const SwsSwizzleOp *swiz)
{
    const SwsCompMask orig = *mask;
    SwsCompMask res = 0;
    for (int i = 0; i < 4; i++) {
        const int src = swiz->in[i];
        if (SWS_COMP_TEST(orig, src))
            res |= SWS_COMP(i);
    }

    *mask = res;
}

SwsCompMask ff_sws_comp_mask_needed(const SwsOp *op)
{
    SwsCompMask mask = 0;
    for (int i = 0; i < 4; i++) {
        if (SWS_OP_NEEDED(op, i))
            mask |= SWS_COMP(i);
    }
    return mask;
}

int ff_sws_rw_op_planes(const SwsOp *op)
{
    av_assert2(op->op == SWS_OP_READ || op->op == SWS_OP_WRITE);
    switch (op->rw.mode) {
    case SWS_RW_PLANAR:  return op->rw.elems;
    case SWS_RW_PACKED:  return 1;
    case SWS_RW_PALETTE: return 2;
    }

    av_unreachable("Invalid read/write mode!");
    return 0;
}

static inline int op_index(const SwsOp *op)
{
    SwsOpList *ops = op->ops;
    av_assert1(op >= ops->ops && op < ops->ops + ops->num_ops);
    return op - ops->ops;
}

const SwsOp *ff_sws_op_prev(const SwsOp *op)
{
    if (!op)
        return NULL;
    SwsOpList *ops = op->ops;
    const int idx = op_index(op);
    return idx > 0 ? &ops->ops[idx - 1] : NULL;
}

const SwsOp *ff_sws_op_next(const SwsOp *op)
{
    if (!op)
        return NULL;
    SwsOpList *ops = op->ops;
    const int idx = op_index(op);
    return idx < ops->num_ops - 1 ? &ops->ops[idx + 1] : NULL;
}

SwsPixelType ff_sws_op_output_type(const SwsOp *op)
{
    SwsPixelType type = SWS_PIXEL_NONE;
    for (int i = 0; op && i < 4; i++) {
        if (!SWS_OP_NEEDED(op, i) || !op->outputs[i].type)
            continue;
        else if (!type)
            type = op->outputs[i].type;
        else if (op->outputs[i].type != type)
            return SWS_PIXEL_NONE;
    }

    return type;
}

SwsPixelType ff_sws_op_input_type(const SwsOp *op)
{
    return ff_sws_op_output_type(ff_sws_op_prev(op));
}

/* biased towards `a` */
static AVRational64 av_min_q64(AVRational64 a, AVRational64 b)
{
    return ff_cmp_q64(a, b) == 1 ? b : a;
}

static AVRational64 av_max_q64(AVRational64 a, AVRational64 b)
{
    return ff_cmp_q64(a, b) == -1 ? b : a;
}

void ff_sws_apply_op_q(const SwsOp *op, AVRational64 x[4])
{
    uint64_t mask[4];
    int shift[4];

    switch (op->op) {
    case SWS_OP_READ:
    case SWS_OP_WRITE:
        return;
    case SWS_OP_UNPACK: {
        ff_sws_pack_decode(op->pack.pattern, mask, shift);
        unsigned val = x[0].num;
        for (int i = 0; i < 4; i++)
            x[i] = Q((val >> shift[i]) & mask[i]);
        return;
    }
    case SWS_OP_PACK: {
        ff_sws_pack_decode(op->pack.pattern, mask, shift);
        unsigned val = 0;
        for (int i = 0; i < 4; i++)
            val |= (x[i].num & mask[i]) << shift[i];
        x[0] = Q(val);
        return;
    }
    case SWS_OP_SWAP_BYTES:
        switch (ff_sws_op_input_type(op)) {
        case SWS_PIXEL_U16:
            for (int i = 0; i < 4; i++) {
                av_assert2(x[i].num >= 0 && x[i].num <= UINT16_MAX);
                x[i].num = av_bswap16(x[i].num);
            }
            return;
        case SWS_PIXEL_U32:
            for (int i = 0; i < 4; i++) {
                av_assert2(x[i].num >= 0 && x[i].num <= UINT32_MAX);
                x[i].num = av_bswap32(x[i].num);
            }
            return;
        }
        av_unreachable("Invalid pixel type for SWS_OP_SWAP_BYTES!");
        return;
    case SWS_OP_CLEAR:
        for (int i = 0; i < 4; i++) {
            if (SWS_COMP_TEST(op->clear.mask, i))
                x[i] = op->clear.value[i];
        }
        return;
    case SWS_OP_LSHIFT: {
        AVRational64 mult = Q(1 << op->shift.amount);
        for (int i = 0; i < 4; i++)
            x[i] = x[i].den ? ff_mul_q64(x[i], mult) : x[i];
        return;
    }
    case SWS_OP_RSHIFT: {
        for (int i = 0; i < 4; i++)
            x[i] = x[i].den ? Q((x[i].num / x[i].den) >> op->shift.amount) : x[i];
        return;
    }
    case SWS_OP_SWIZZLE: {
        const AVRational64 orig[4] = { x[0], x[1], x[2], x[3] };
        for (int i = 0; i < 4; i++)
            x[i] = orig[op->swizzle.in[i]];
        return;
    }
    case SWS_OP_CONVERT:
        if (ff_sws_pixel_type_is_int(op->convert.to)) {
            for (int i = 0; i < 4; i++)
                x[i] = x[i].den ? Q(x[i].num / x[i].den) : x[i];
        }
        return;
    case SWS_OP_DITHER:
        for (int i = 0; i < 4; i++) {
            if (op->dither.y_offset[i] >= 0 && x[i].den)
                x[i] = ff_add_q64(x[i], ff_make_q64(1, 2));
        }
        return;
    case SWS_OP_MIN:
        for (int i = 0; i < 4; i++)
            x[i] = av_min_q64(x[i], op->clamp.limit[i]);
        return;
    case SWS_OP_MAX:
        for (int i = 0; i < 4; i++)
            x[i] = av_max_q64(x[i], op->clamp.limit[i]);
        return;
    case SWS_OP_LINEAR: {
        const AVRational64 orig[4] = { x[0], x[1], x[2], x[3] };
        for (int i = 0; i < 4; i++) {
            AVRational64 sum = op->lin.m[i][4];
            for (int j = 0; j < 4; j++)
                sum = ff_add_q64(sum, ff_mul_q64(orig[j], op->lin.m[i][j]));
            x[i] = sum;
        }
        return;
    }
    case SWS_OP_SCALE:
        for (int i = 0; i < 4; i++)
            x[i] = x[i].den ? ff_mul_q64(x[i], op->scale.factor) : x[i];
        return;
    case SWS_OP_FILTER_H:
    case SWS_OP_FILTER_V:
        /* Filters have normalized energy by definition, so they don't
         * conceptually modify individual components */
        return;
    case SWS_OP_LUT_3D:
        /* 3D LUTs are treated as a black box, so set those values to NaN */
        for (int i = 0; i < 3; i++)
            x[i] = (AVRational64) {0};
        return;
    }

    av_unreachable("Invalid operation type!");
}

enum {
    SWS_COMP_IDENTITY = SWS_COMP_ZERO | SWS_COMP_EXACT |
                        SWS_COMP_COPY | SWS_COMP_CONST,

    SWS_COMP_DIRTY = ~(SWS_COMP_COPY | SWS_COMP_CONST),
};

/* merge_comp_flags() forms a monoid with SWS_COMP_IDENTITY as the null element */
static SwsCompFlags merge_comp_flags(SwsCompFlags a, SwsCompFlags b)
{
    const SwsCompFlags flags_or  = SWS_COMP_SWAPPED;
    const SwsCompFlags flags_and = SWS_COMP_IDENTITY;
    return ((a & b) & flags_and) | ((a | b) & flags_or);
}

static void apply_filter_weights(SwsOpPad *pad, const SwsFilterWeights *weights)
{
    const AVRational64 posw = { weights->sum_positive, SWS_FILTER_SCALE };
    const AVRational64 negw = { weights->sum_negative, SWS_FILTER_SCALE };

    /* Only point sampling preserves exactness */
    if (weights->filter_size != 1)
        pad->flags &= ~SWS_COMP_EXACT;

    /* Update min/max assuming extremes */
    const AVRational64 min = pad->min, max = pad->max;
    pad->min = ff_add_q64(ff_mul_q64(min, posw), ff_mul_q64(max, negw));
    pad->max = ff_add_q64(ff_mul_q64(min, negw), ff_mul_q64(max, posw));
}

static void update_output_pads(SwsOpList *ops, SwsOp *op)
{
    static const SwsOpPad dummy[4] = {0};
    const SwsOp *prev = ff_sws_op_prev(op);
    const SwsOpPad *in = prev ? prev->outputs : dummy;
    SwsOpPad *out = op->outputs;
    SwsCompMask invalid = 0;
    for (int i = 0; i < 4; i++) {
        out[i].type   = SWS_PIXEL_NONE;
        out[i].flags  = SWS_COMP_IDENTITY;
        out[i].dep_in = SWS_COMP_NONE;
    }

    switch (op->op) {
    case SWS_OP_LINEAR:
    case SWS_OP_DITHER:
    case SWS_OP_SWAP_BYTES:
    case SWS_OP_UNPACK:
    case SWS_OP_LUT_3D:
        break; /* special cases, handled below */
    default: {
        AVRational64 min[4], max[4];
        for (int i = 0; i < 4; i++) {
            min[i] = in[i].min;
            max[i] = in[i].max;
        }

        ff_sws_apply_op_q(op, min);
        ff_sws_apply_op_q(op, max);
        for (int i = 0; i < 4; i++) {
            out[i].min = min[i];
            out[i].max = max[i];
        }
        break;
    }
    }

#define FORWARD(I, J, EXPR) do {                                                \
    SwsOpPad *dst = &out[I];                                                    \
    const SwsOpPad *src = &in[J];                                               \
    SwsCompFlags flags = src->flags;                                            \
    if (!src->type || dst->type && dst->type != src->type)                      \
        invalid |= SWS_COMP(I);                                                 \
    dst->flags = merge_comp_flags(dst->flags, (EXPR));                          \
    dst->dep_in |= src->dep_in;                                                 \
    dst->type = src->type;                                                      \
} while (0)

#define RESET(I) do {                                                           \
    SwsOpPad *dst = &out[I];                                                    \
    dst->type = SWS_PIXEL_NONE;                                                 \
    dst->flags = 0;                                                             \
    dst->min = dst->max = (AVRational64) {0};                                   \
    dst->dep_in = SWS_COMP_NONE;                                                \
} while (0)

    switch (op->op) {
    case SWS_OP_READ:
        /* Active components are taken from the user-provided values,
         * other components are explicitly stripped */
        for (int i = 0; i < op->rw.elems; i++) {
            int idx = 0;
            switch (op->rw.mode) {
            case SWS_RW_PALETTE: idx = i; break;
            case SWS_RW_PACKED:  idx = i; break;
            case SWS_RW_PLANAR:  idx = ops->plane_src[i]; break;
            }

            SwsOpPad *src = &ops->inputs[idx];
            SwsOpPad *dst = &out[i];
            dst->type   = op->rw.type;
            dst->flags  = src->flags & SWS_COMP_DIRTY;
            dst->min    = src->min;
            dst->max    = src->max;
            dst->dep_in = SWS_COMP(i);

            if (op->rw.filter.op) {
                apply_filter_weights(dst, op->rw.filter.kernel);
                dst->type = op->rw.filter.type;
            } else if (op->rw.mode == SWS_RW_PLANAR && !op->rw.frac) {
                /* Don't mark packed or fractional reads as a copy, because
                 * the read operation implicitly unpacks the data into
                 * separate components. The only case in which op lists
                 * involving such reads can be refcopies is in the case of
                 * a true no-op, which is covered by the no-op check. */
                dst->flags |= SWS_COMP_COPY;
            }
        }

        /* Forward unused/untouched components */
        for (int i = op->rw.elems; i < 4; i++)
            FORWARD(i, i, flags);
        break;
    case SWS_OP_SWAP_BYTES:
        for (int i = 0; i < 4; i++) {
            FORWARD(i, i, (flags ^ SWS_COMP_SWAPPED) & SWS_COMP_DIRTY);
            out[i].min = in[i].min;
            out[i].max = in[i].max;
        }
        break;
    case SWS_OP_WRITE:
        av_assert0(op->rw.elems <= 4);
        for (int i = 0; i < op->rw.elems; i++)
            av_assert1(in[i].type == op->rw.type);
        for (int i = 0; i < 4; i++)
            FORWARD(i, i, flags);
        break;
    case SWS_OP_LSHIFT:
    case SWS_OP_RSHIFT:
        for (int i = 0; i < 4; i++)
            FORWARD(i, i, flags & SWS_COMP_DIRTY);
        break;
    case SWS_OP_MIN:
    case SWS_OP_MAX: {
        for (int i = 0; i < 4; i++) {
            SwsOpPad *dst = &out[i];
            FORWARD(i, i, flags);
            if (op->clamp.limit[i].den)
                dst->flags &= SWS_COMP_DIRTY;

            /* Reset undefined bounds to known range */
            AVRational64 *bound = op->op == SWS_OP_MIN ? &dst->max : &dst->min;
            if (!bound->den)
                *bound = op->clamp.limit[i];
        }
        break;
    }
    case SWS_OP_DITHER:
        for (int i = 0; i < 4; i++) {
            FORWARD(i, i, flags);
            out[i].min = in[i].min;
            out[i].max = in[i].max;
            if (op->dither.y_offset[i] < 0)
                continue;
            /* Strip zero flag because of the nonzero dithering offset */
            out[i].flags &= ~SWS_COMP_ZERO & SWS_COMP_DIRTY;
            out[i].min = ff_add_q64(out[i].min, op->dither.min);
            out[i].max = ff_add_q64(out[i].max, op->dither.max);
        }
        break;
    case SWS_OP_UNPACK:
        for (int i = 0; i < 4; i++) {
            const int pattern = op->pack.pattern[i];
            if (pattern) {
                av_assert1(pattern < 32);
                FORWARD(i, 0, flags & SWS_COMP_DIRTY);
                out[i].min = Q(0);
                out[i].max = Q((1ULL << pattern) - 1);
            } else
                RESET(i);
        }
        break;
    case SWS_OP_PACK:
        for (int i = 0; i < 4; i++) {
            if (op->pack.pattern[i])
                FORWARD(0, i, flags & SWS_COMP_DIRTY);
            if (i > 0) /* clear remaining pads for sanity */
                RESET(i);
        }
        break;
    case SWS_OP_CLEAR:
        for (int i = 0; i < 4; i++) {
            if (SWS_COMP_TEST(op->clear.mask, i)) {
                out[i].type = op->clear.type[i];
                out[i].flags = SWS_COMP_CONST;
                if (op->clear.value[i].num == 0)
                    out[i].flags |= SWS_COMP_ZERO;
                if (op->clear.value[i].den == 1)
                    out[i].flags |= SWS_COMP_EXACT;
            } else {
                FORWARD(i, i, flags);
            }
        }
        break;
    case SWS_OP_SWIZZLE:
        for (int i = 0; i < 4; i++)
            FORWARD(i, op->swizzle.in[i], flags);
        break;
    case SWS_OP_CONVERT:
        for (int i = 0; i < 4; i++) {
            FORWARD(i, i, flags);
            out[i].type = op->convert.to;
            if (!(in[i].flags & SWS_COMP_EXACT))
                out[i].flags &= SWS_COMP_DIRTY;
            if (ff_sws_pixel_type_is_int(op->convert.to))
                out[i].flags |= SWS_COMP_EXACT;
        }
        break;
    case SWS_OP_LINEAR:
        for (int i = 0; i < 4; i++) {
            SwsOpPad *dst = &out[i];
            bool first = true;
            dst->type = op->lin.type[i];
            dst->min = dst->max = Q(0);
            for (int j = 0; j < 4; j++) {
                const SwsOpPad *src = &in[j];
                const AVRational64 k = op->lin.m[i][j];
                AVRational64 mink = ff_mul_q64(src->min, k);
                AVRational64 maxk = ff_mul_q64(src->max, k);
                if (!k.num)
                    continue;
                FORWARD(i, j, flags);
                if (k.den != 1) /* fractional coefficient */
                    dst->flags &= ~SWS_COMP_EXACT;
                if (k.num < 0)
                    FFSWAP(AVRational64, mink, maxk);
                dst->min = ff_add_q64(dst->min, mink);
                dst->max = ff_add_q64(dst->max, maxk);
                if (!first || ff_cmp_q64(k, Q(1)))
                    dst->flags &= SWS_COMP_DIRTY;
                first = false;
            }
            if (op->lin.m[i][4].num) { /* nonzero offset */
                dst->flags &= ~SWS_COMP_ZERO & SWS_COMP_DIRTY;
                if (op->lin.m[i][4].den != 1) /* fractional offset */
                    dst->flags &= ~SWS_COMP_EXACT;
                dst->min = ff_add_q64(dst->min, op->lin.m[i][4]);
                dst->max = ff_add_q64(dst->max, op->lin.m[i][4]);
            }
        }
        break;
    case SWS_OP_SCALE:
        for (int i = 0; i < 4; i++) {
            FORWARD(i, i, flags & SWS_COMP_DIRTY);
            if (op->scale.factor.den != 1) /* fractional scale */
                out[i].flags &= ~SWS_COMP_EXACT;
            if (op->scale.factor.num < 0)
                FFSWAP(AVRational64, out[i].min, out[i].max);
        }
        break;
    case SWS_OP_FILTER_H:
    case SWS_OP_FILTER_V: {
        for (int i = 0; i < 4; i++) {
            FORWARD(i, i, flags & SWS_COMP_DIRTY);
            apply_filter_weights(&out[i], op->filter.kernel);
            out[i].type = op->filter.type;
        }
        break;
    }
    case SWS_OP_LUT_3D:
        for (int i = 0; i < 3; i++) {
            /* 3x3 dependency matrix; strip all information */
            for (int j = 0; j < 3; j++)
                FORWARD(i, j, flags & 0);
            /* LUT output domain is always scaled to full 16-bit range */
            out[i].min = Q(0);
            out[i].max = Q(UINT16_MAX);
        }
        /* Pass through alpha channel untouched */
        FORWARD(3, 3, flags);
        out[3].min = in[3].min;
        out[3].max = in[3].max;
        break;
    case SWS_OP_INVALID:
    case SWS_OP_TYPE_NB:
        av_unreachable("Invalid operation type!");
    }

    for (int i = 0; i < 4; i++) {
        if (!out[i].type || SWS_COMP_TEST(invalid, i))
            RESET(i);
    }

#undef FORWARD
#undef RESET
}

static void update_input_pads(SwsOpList *ops, SwsOp *op, SwsCompMask need_out[4])
{
    SwsCompMask need_in[4] = {0};

    for (int i = 0; i < 4; i++)
        op->outputs[i].dep_out = need_out[i];

    switch (op->op) {
    case SWS_OP_READ:
    case SWS_OP_WRITE:
        for (int i = 0; i < op->rw.elems; i++)
            need_in[i] = (op->op == SWS_OP_WRITE) ? SWS_COMP(i) : 0;
        for (int i = op->rw.elems; i < 4; i++)
            need_in[i] = need_out[i];
        break;
    case SWS_OP_SWAP_BYTES:
    case SWS_OP_LSHIFT:
    case SWS_OP_RSHIFT:
    case SWS_OP_CONVERT:
    case SWS_OP_DITHER:
    case SWS_OP_MIN:
    case SWS_OP_MAX:
    case SWS_OP_SCALE:
    case SWS_OP_FILTER_H:
    case SWS_OP_FILTER_V:
        for (int i = 0; i < 4; i++)
            need_in[i] = need_out[i];
        break;
    case SWS_OP_UNPACK:
        for (int i = 0; i < 4 && op->pack.pattern[i]; i++)
            need_in[0] |= need_out[i];
        break;
    case SWS_OP_PACK:
        for (int i = 0; i < 4 && op->pack.pattern[i]; i++)
            need_in[i] = need_out[0];
        break;
    case SWS_OP_CLEAR:
        for (int i = 0; i < 4; i++) {
            if (!SWS_COMP_TEST(op->clear.mask, i))
                need_in[i] = need_out[i];
        }
        break;
    case SWS_OP_SWIZZLE:
        for (int i = 0; i < 4; i++)
            need_in[op->swizzle.in[i]] |= need_out[i];
        break;
    case SWS_OP_LINEAR:
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                if (op->lin.m[i][j].num)
                    need_in[j] |= need_out[i];
            }
        }
        break;
    case SWS_OP_LUT_3D:
        for (int i = 0; i < 3; i++)
            need_in[i] = need_out[0] | need_out[1] | need_out[2];
        need_in[3] = need_out[3];
        break;
    }

    memcpy(need_out, need_in, sizeof(need_in));
}

/* Infer + propagate output pad metadata */
void ff_sws_op_list_update(SwsOpList *ops)
{
    /* Forward pass, solves for output pad metadata */
    for (int n = 0; n < ops->num_ops; n++)
        update_output_pads(ops, &ops->ops[n]);

    /* Backwards pass, solves for output component dependencies */
    SwsCompMask need_out[4] = {0};
    for (int n = ops->num_ops - 1; n >= 0; n--)
        update_input_pads(ops, &ops->ops[n], need_out);
}

static void op_ref(SwsOp *op)
{
    switch (op->op) {
    case SWS_OP_READ:
        if (op->rw.filter.kernel)
            av_refstruct_ref(op->rw.filter.kernel);
        break;
    case SWS_OP_DITHER:
        av_refstruct_ref(op->dither.matrix);
        break;
    case SWS_OP_FILTER_H:
    case SWS_OP_FILTER_V:
        av_refstruct_ref(op->filter.kernel);
        break;
    case SWS_OP_LUT_3D:
        av_refstruct_ref_c(op->lut3d.lut);
        break;
    }
}

static void op_unref(SwsOp *op)
{
    switch (op->op) {
    case SWS_OP_READ:
        av_refstruct_unref(&op->rw.filter.kernel);
        break;
    case SWS_OP_DITHER:
        av_refstruct_unref(&op->dither.matrix);
        break;
    case SWS_OP_FILTER_H:
    case SWS_OP_FILTER_V:
        av_refstruct_unref(&op->filter.kernel);
        break;
    case SWS_OP_LUT_3D:
        av_refstruct_unref(&op->lut3d.lut);
        break;
    }

    *op = (SwsOp) {0};
}

SwsOpList *ff_sws_op_list_alloc(void)
{
    SwsOpList *ops = av_mallocz(sizeof(SwsOpList));
    if (!ops)
        return NULL;

    for (int i = 0; i < 4; i++)
        ops->plane_src[i] = ops->plane_dst[i] = i;
    ff_fmt_clear(&ops->src);
    ff_fmt_clear(&ops->dst);
    return ops;
}

void ff_sws_op_list_free(SwsOpList **p_ops)
{
    SwsOpList *ops = *p_ops;
    if (!ops)
        return;

    for (int i = 0; i < ops->num_ops; i++)
        op_unref(&ops->ops[i]);

    av_freep(&ops->ops);
    av_free(ops);
    *p_ops = NULL;
}

SwsOpList *ff_sws_op_list_duplicate(const SwsOpList *ops)
{
    SwsOpList *copy = av_malloc(sizeof(*copy));
    if (!copy)
        return NULL;

    int num = ops->num_ops;
    if (num)
        num = 1 << av_ceil_log2(num);

    *copy = *ops;
    copy->ops = av_memdup(ops->ops, num * sizeof(ops->ops[0]));
    if (!copy->ops) {
        av_free(copy);
        return NULL;
    }

    for (int i = 0; i < copy->num_ops; i++) {
        op_ref(&copy->ops[i]);
        copy->ops[i].ops = copy;
    }

    return copy;
}

const SwsOp *ff_sws_op_list_input(const SwsOpList *ops)
{
    if (!ops->num_ops)
        return NULL;

    const SwsOp *read = &ops->ops[0];
    return read->op == SWS_OP_READ ? read : NULL;
}

const SwsOp *ff_sws_op_list_output(const SwsOpList *ops)
{
    if (!ops->num_ops)
        return NULL;

    const SwsOp *write = &ops->ops[ops->num_ops - 1];
    return write->op == SWS_OP_WRITE ? write : NULL;
}

void ff_sws_op_list_remove_at(SwsOpList *ops, int index, int count)
{
    const int end = ops->num_ops - count;
    av_assert2(index >= 0 && count >= 0 && index + count <= ops->num_ops);
    for (int i = 0; i < count; i++)
        op_unref(&ops->ops[index + i]);
    for (int i = index; i < end; i++)
        ops->ops[i] = ops->ops[i + count];
    ops->num_ops = end;
}

int ff_sws_op_list_insert_at(SwsOpList *ops, int index, SwsOp *op)
{
    void *ret = av_dynarray2_add((void **) &ops->ops, &ops->num_ops, sizeof(*op), NULL);
    if (!ret) {
        op_unref(op);
        return AVERROR(ENOMEM);
    }

    for (int i = ops->num_ops - 1; i > index; i--)
        ops->ops[i] = ops->ops[i - 1];
    ops->ops[index] = *op;
    ops->ops[index].ops = ops;
    return 0;
}

int ff_sws_op_list_append(SwsOpList *ops, SwsOp *op)
{
    return ff_sws_op_list_insert_at(ops, ops->num_ops, op);
}

bool ff_sws_op_list_is_noop(const SwsOpList *ops)
{
    if (!ops->num_ops)
        return true;

    const SwsOp *read  = ff_sws_op_list_input(ops);
    const SwsOp *write = ff_sws_op_list_output(ops);
    if (!read || !write || ops->num_ops > 2 ||
        read->rw.type != write->rw.type ||
        read->rw.mode != write->rw.mode ||
        read->rw.elems != write->rw.elems ||
        read->rw.frac != write->rw.frac ||
        read->rw.filter.op || write->rw.filter.op)
        return false;

    /**
     * Note that this check is unlikely to ever be hit in practice, since it
     * would imply the existence of planar formats with different plane orders
     * between them, e.g. rgbap <-> gbrap, which doesn't currently exist.
     * However, the check is cheap and lets me sleep at night.
     */
    const int num_planes = ff_sws_rw_op_planes(read);
    for (int i = 0; i < num_planes; i++) {
        if (ops->plane_src[i] != ops->plane_dst[i])
            return false;
    }

    return true;
}

uint32_t ff_sws_linear_mask(const SwsLinearOp *c)
{
    uint32_t mask = 0;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 5; j++) {
            if (ff_cmp_q64(c->m[i][j], Q(i == j)))
                mask |= SWS_MASK(i, j);
        }
    }
    return mask;
}

static const char *describe_comp_flags(SwsCompFlags flags)
{
    if (flags & SWS_COMP_ZERO)
        return "zero";
    else if (flags & SWS_COMP_SWAPPED)
        return "swapped";
    else if (flags & SWS_COMP_CONST)
        return "const";
    else if (flags & SWS_COMP_COPY)
        return "copy";
    else if (flags & SWS_COMP_EXACT)
        return "exact";
    else
        return "normal";
}

static void print_deps(AVBPrint *bp, SwsCompMask dep)
{
    av_bprintf(bp, " {%c%c%c%c}",
               SWS_COMP_TEST(dep, 0) ? 'x' : ' ',
               SWS_COMP_TEST(dep, 1) ? 'y' : ' ',
               SWS_COMP_TEST(dep, 2) ? 'z' : ' ',
               SWS_COMP_TEST(dep, 3) ? 'w' : ' ');
}

static void print_q(AVBPrint *bp, const AVRational64 q)
{
    if (!q.den) {
        av_bprintf(bp, "%s", q.num > 0 ? "inf" : q.num < 0 ? "-inf" : "nan");
    } else if (q.den == 1) {
        av_bprintf(bp, "%"PRId64, q.num);
    } else if (q.num > 1000 || q.num < -1000 || q.den > 1000 || q.den < -1000) {
        av_bprintf(bp, "%f", ff_q2d_64(q));
    } else {
        av_bprintf(bp, "%"PRId64"/%"PRId64, q.num, q.den);
    }
}

static void print_q4(AVBPrint *bp, const AVRational64 q4[4], SwsCompMask mask)
{
    av_bprintf(bp, "{");
    for (int i = 0; i < 4; i++) {
        if (i)
            av_bprintf(bp, " ");
        if (!SWS_COMP_TEST(mask, i)) {
            av_bprintf(bp, "_");
        } else {
            print_q(bp, q4[i]);
        }
    }
    av_bprintf(bp, "}");
}

static const char *const rw_mode_names[] = {
    [SWS_RW_PLANAR]     = "planar",
    [SWS_RW_PACKED]     = "packed",
    [SWS_RW_PALETTE]    = "palette"
};

void ff_sws_op_desc(AVBPrint *bp, const SwsOp *op)
{
    const char *name  = ff_sws_op_type_name(op->op);
    const SwsCompMask mask = ff_sws_comp_mask_needed(op);

    switch (op->op) {
    case SWS_OP_INVALID:
    case SWS_OP_SWAP_BYTES:
        av_bprintf(bp, "%s", name);
        break;
    case SWS_OP_READ:
    case SWS_OP_WRITE:
        av_bprintf(bp, "%-20s: %dx %s %s >> %d", name,
                   op->rw.elems, ff_sws_pixel_type_name(op->rw.type),
                   rw_mode_names[op->rw.mode], op->rw.frac);
        if (!op->rw.filter.op)
            break;
        const SwsFilterWeights *kernel = op->rw.filter.kernel;
        av_bprintf(bp, " + %d tap %s filter (%c)",
                   kernel->filter_size, kernel->name,
                   op->rw.filter.op == SWS_OP_FILTER_H ? 'H' : 'V');
        break;
    case SWS_OP_LSHIFT:
        av_bprintf(bp, "%-20s: << %u", name, op->shift.amount);
        break;
    case SWS_OP_RSHIFT:
        av_bprintf(bp, "%-20s: >> %u", name, op->shift.amount);
        break;
    case SWS_OP_PACK:
    case SWS_OP_UNPACK:
        av_bprintf(bp, "%-20s: {%d %d %d %d}", name,
                   op->pack.pattern[0], op->pack.pattern[1],
                   op->pack.pattern[2], op->pack.pattern[3]);
        break;
    case SWS_OP_CLEAR:
        av_bprintf(bp, "%-20s: ", name);
        print_q4(bp, op->clear.value, mask & op->clear.mask);
        break;
    case SWS_OP_SWIZZLE:
        av_bprintf(bp, "%-20s: %d%d%d%d", name,
                   op->swizzle.x, op->swizzle.y, op->swizzle.z, op->swizzle.w);
        break;
    case SWS_OP_CONVERT:
        av_bprintf(bp, "%-20s: %s -> %s", name,
                   ff_sws_pixel_type_name(ff_sws_op_input_type(op)),
                   ff_sws_pixel_type_name(op->convert.to));
        break;
    case SWS_OP_DITHER:
        av_bprintf(bp, "%-20s: %dx%d matrix + {%d %d %d %d}", name,
                   1 << op->dither.size_log2, 1 << op->dither.size_log2,
                   op->dither.y_offset[0], op->dither.y_offset[1],
                   op->dither.y_offset[2], op->dither.y_offset[3]);
        break;
    case SWS_OP_MIN:
        av_bprintf(bp, "%-20s: x <= ", name);
        print_q4(bp, op->clamp.limit, mask & ff_sws_comp_mask_q4(op->clamp.limit));
        break;
    case SWS_OP_MAX:
        av_bprintf(bp, "%-20s: ", name);
        print_q4(bp, op->clamp.limit, mask & ff_sws_comp_mask_q4(op->clamp.limit));
        av_bprintf(bp, " <= x");
        break;
    case SWS_OP_LINEAR:
        av_bprintf(bp, "%-20s: [", name);
        for (int i = 0; i < 4; i++) {
            av_bprintf(bp, "%s[", i ? " " : "");
            for (int j = 0; j < 5; j++) {
                av_bprintf(bp, j ? " " : "");
                print_q(bp, op->lin.m[i][j]);
            }
            av_bprintf(bp, "]");
        }
        av_bprintf(bp, "]");
        break;
    case SWS_OP_SCALE:
        av_bprintf(bp, "%-20s: * %"PRId64, name, op->scale.factor.num);
        if (op->scale.factor.den != 1)
            av_bprintf(bp, "/%"PRId64, op->scale.factor.den);
        break;
    case SWS_OP_FILTER_H:
    case SWS_OP_FILTER_V: {
        const SwsFilterWeights *kernel = op->filter.kernel;
        av_bprintf(bp, "%-20s: %d -> %d %s (%d taps)", name,
                   kernel->src_size, kernel->dst_size,
                   kernel->name, kernel->filter_size);
        break;
    }
    case SWS_OP_LUT_3D:
        av_bprintf(bp, "%-20s: %s", name, op->lut3d.dynamic ? "dynamic" : "static");
        break;
    case SWS_OP_TYPE_NB:
        break;
    }
}

static void desc_plane_order(AVBPrint *bp, int nb_planes, const uint8_t *order)
{
    bool inorder = true;
    for (int i = 0; i < nb_planes; i++)
        inorder &= order[i] == i;
    if (inorder)
        return;

    av_bprintf(bp, ", via {");
    for (int i = 0; i < nb_planes; i++)
        av_bprintf(bp, "%s%d", i ? ", " : "", order[i]);
    av_bprintf(bp, "}");
}

void ff_sws_op_list_print(void *log, int lev, int lev_extra,
                          const SwsOpList *ops)
{
    AVBPrint bpc, *bp = &bpc;
    if (!ops->num_ops) {
        av_log(log, lev, "  (empty)\n");
        return;
    }

    av_bprint_init(bp, 0, AV_BPRINT_SIZE_AUTOMATIC);

    for (int n = 0; n < ops->num_ops; n++) {
        const SwsOp *op = &ops->ops[n];
        const SwsCompMask mask = ff_sws_comp_mask_needed(op);
        av_bprint_clear(bp);
        av_bprintf(bp, "  ");
        ff_sws_op_desc(bp, op);
        if (op->op == SWS_OP_READ || op->op == SWS_OP_WRITE) {
            const int planes = ff_sws_rw_op_planes(op);
            desc_plane_order(bp, planes,
                op->op == SWS_OP_READ ? ops->plane_src : ops->plane_dst);
        }

        av_assert0(av_bprint_is_complete(bp));
        av_log(log, lev, "%s\n", bp->str);

        for (int i = 0; i < 4; i++) {
            const SwsOpPad *pad = &op->outputs[i];
            if (!SWS_COMP_TEST(mask, i))
                continue;
            av_bprint_clear(bp);
            av_bprintf(bp, "    %c: %3s,", "XYZW"[i], ff_sws_pixel_type_name(pad->type));
            print_deps(bp, pad->dep_in);
            av_bprintf(bp, " ->");
            print_deps(bp, pad->dep_out);
            av_bprintf(bp, " %7s", describe_comp_flags(pad->flags));

            if (pad->min.den || pad->max.den) {
                if (pad->flags & SWS_COMP_ZERO) {
                    /* don't print any value */
                } else if (pad->flags & SWS_COMP_CONST) {
                    av_bprintf(bp, " = ");
                    av_assert1(ff_cmp_q64(pad->min, pad->max) == 0);
                    print_q(bp, pad->min);
                } else {
                    av_bprintf(bp, " in [");
                    print_q(bp, pad->min);
                    av_bprintf(bp, ", ");
                    print_q(bp, pad->max);
                    av_bprintf(bp, "]");
                }
            }

            av_assert0(av_bprint_is_complete(bp));
            av_log(log, lev_extra, "%s\n", bp->str);
        }
    }
}
