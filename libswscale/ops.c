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
extern const SwsOpBackend backend_x86;
extern const SwsOpBackend backend_vulkan;

const SwsOpBackend * const ff_sws_op_backends[] = {
    &backend_murder,
#if ARCH_X86_64 && HAVE_X86ASM
    &backend_x86,
#endif
    &backend_c,
#if CONFIG_VULKAN
    &backend_vulkan,
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

int ff_sws_pixel_type_size(SwsPixelType type)
{
    switch (type) {
    case SWS_PIXEL_U8:  return sizeof(uint8_t);
    case SWS_PIXEL_U16: return sizeof(uint16_t);
    case SWS_PIXEL_U32: return sizeof(uint32_t);
    case SWS_PIXEL_F32: return sizeof(float);
    case SWS_PIXEL_NONE: break;
    case SWS_PIXEL_TYPE_NB: break;
    }

    av_unreachable("Invalid pixel type!");
    return 0;
}

bool ff_sws_pixel_type_is_int(SwsPixelType type)
{
    switch (type) {
    case SWS_PIXEL_U8:
    case SWS_PIXEL_U16:
    case SWS_PIXEL_U32:
        return true;
    case SWS_PIXEL_F32:
        return false;
    case SWS_PIXEL_NONE:
    case SWS_PIXEL_TYPE_NB: break;
    }

    av_unreachable("Invalid pixel type!");
    return false;
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
    case SWS_OP_INVALID:     return "SWS_OP_INVALID";
    case SWS_OP_TYPE_NB: break;
    }

    av_unreachable("Invalid operation type!");
    return "ERR";
}

/* biased towards `a` */
static AVRational av_min_q(AVRational a, AVRational b)
{
    return av_cmp_q(a, b) == 1 ? b : a;
}

static AVRational av_max_q(AVRational a, AVRational b)
{
    return av_cmp_q(a, b) == -1 ? b : a;
}

void ff_sws_apply_op_q(const SwsOp *op, AVRational x[4])
{
    uint64_t mask[4];
    int shift[4];

    switch (op->op) {
    case SWS_OP_READ:
    case SWS_OP_WRITE:
        return;
    case SWS_OP_UNPACK: {
        av_assert1(ff_sws_pixel_type_is_int(op->type));
        ff_sws_pack_op_decode(op, mask, shift);
        unsigned val = x[0].num;
        for (int i = 0; i < 4; i++)
            x[i] = Q((val >> shift[i]) & mask[i]);
        return;
    }
    case SWS_OP_PACK: {
        av_assert1(ff_sws_pixel_type_is_int(op->type));
        ff_sws_pack_op_decode(op, mask, shift);
        unsigned val = 0;
        for (int i = 0; i < 4; i++)
            val |= (x[i].num & mask[i]) << shift[i];
        x[0] = Q(val);
        return;
    }
    case SWS_OP_SWAP_BYTES:
        av_assert1(ff_sws_pixel_type_is_int(op->type));
        switch (ff_sws_pixel_type_size(op->type)) {
        case 2:
            for (int i = 0; i < 4; i++)
                x[i].num = av_bswap16(x[i].num);
            break;
        case 4:
            for (int i = 0; i < 4; i++)
                x[i].num = av_bswap32(x[i].num);
            break;
        }
        return;
    case SWS_OP_CLEAR:
        for (int i = 0; i < 4; i++) {
            if (op->c.q4[i].den)
                x[i] = op->c.q4[i];
        }
        return;
    case SWS_OP_LSHIFT: {
        av_assert1(ff_sws_pixel_type_is_int(op->type));
        AVRational mult = Q(1 << op->c.u);
        for (int i = 0; i < 4; i++)
            x[i] = x[i].den ? av_mul_q(x[i], mult) : x[i];
        return;
    }
    case SWS_OP_RSHIFT: {
        av_assert1(ff_sws_pixel_type_is_int(op->type));
        for (int i = 0; i < 4; i++)
            x[i] = x[i].den ? Q((x[i].num / x[i].den) >> op->c.u) : x[i];
        return;
    }
    case SWS_OP_SWIZZLE: {
        const AVRational orig[4] = { x[0], x[1], x[2], x[3] };
        for (int i = 0; i < 4; i++)
            x[i] = orig[op->swizzle.in[i]];
        return;
    }
    case SWS_OP_CONVERT:
        if (ff_sws_pixel_type_is_int(op->convert.to)) {
            const AVRational scale = ff_sws_pixel_expand(op->type, op->convert.to);
            for (int i = 0; i < 4; i++) {
                x[i] = x[i].den ? Q(x[i].num / x[i].den) : x[i];
                if (op->convert.expand)
                    x[i] = av_mul_q(x[i], scale);
            }
        }
        return;
    case SWS_OP_DITHER:
        av_assert1(!ff_sws_pixel_type_is_int(op->type));
        for (int i = 0; i < 4; i++) {
            if (op->dither.y_offset[i] >= 0 && x[i].den)
                x[i] = av_add_q(x[i], av_make_q(1, 2));
        }
        return;
    case SWS_OP_MIN:
        for (int i = 0; i < 4; i++)
            x[i] = av_min_q(x[i], op->c.q4[i]);
        return;
    case SWS_OP_MAX:
        for (int i = 0; i < 4; i++)
            x[i] = av_max_q(x[i], op->c.q4[i]);
        return;
    case SWS_OP_LINEAR: {
        av_assert1(!ff_sws_pixel_type_is_int(op->type));
        const AVRational orig[4] = { x[0], x[1], x[2], x[3] };
        for (int i = 0; i < 4; i++) {
            AVRational sum = op->lin.m[i][4];
            for (int j = 0; j < 4; j++)
                sum = av_add_q(sum, av_mul_q(orig[j], op->lin.m[i][j]));
            x[i] = sum;
        }
        return;
    }
    case SWS_OP_SCALE:
        for (int i = 0; i < 4; i++)
            x[i] = x[i].den ? av_mul_q(x[i], op->c.q) : x[i];
        return;
    case SWS_OP_FILTER_H:
    case SWS_OP_FILTER_V:
        /* Filters have normalized energy by definition, so they don't
         * conceptually modify individual components */
        return;
    }

    av_unreachable("Invalid operation type!");
}

/* merge_comp_flags() forms a monoid with flags_identity as the null element */
static const SwsCompFlags flags_identity = SWS_COMP_ZERO | SWS_COMP_EXACT;
static SwsCompFlags merge_comp_flags(SwsCompFlags a, SwsCompFlags b)
{
    const SwsCompFlags flags_or  = SWS_COMP_GARBAGE;
    const SwsCompFlags flags_and = SWS_COMP_ZERO | SWS_COMP_EXACT;
    return ((a & b) & flags_and) | ((a | b) & flags_or);
}

/* Linearly propagate flags per component */
static void propagate_flags(SwsOp *op, const SwsComps *prev)
{
    for (int i = 0; i < 4; i++)
        op->comps.flags[i] = prev->flags[i];
}

/* Clear undefined values in dst with src */
static void clear_undefined_values(AVRational dst[4], const AVRational src[4])
{
    for (int i = 0; i < 4; i++) {
        if (dst[i].den == 0)
            dst[i] = src[i];
    }
}

static void apply_filter_weights(SwsComps *comps, const SwsComps *prev,
                                 const SwsFilterWeights *weights)
{
    const AVRational posw = { weights->sum_positive, SWS_FILTER_SCALE };
    const AVRational negw = { weights->sum_negative, SWS_FILTER_SCALE };
    for (int i = 0; i < 4; i++) {
        comps->flags[i] = prev->flags[i];
        /* Only point sampling preserves exactness */
        if (weights->filter_size != 1)
            comps->flags[i] &= ~SWS_COMP_EXACT;
        /* Update min/max assuming extremes */
        comps->min[i] = av_add_q(av_mul_q(prev->min[i], posw),
                                 av_mul_q(prev->max[i], negw));
        comps->max[i] = av_add_q(av_mul_q(prev->min[i], negw),
                                 av_mul_q(prev->max[i], posw));
    }
}

/* Infer + propagate known information about components */
void ff_sws_op_list_update_comps(SwsOpList *ops)
{
    SwsComps prev = { .flags = {
        SWS_COMP_GARBAGE, SWS_COMP_GARBAGE, SWS_COMP_GARBAGE, SWS_COMP_GARBAGE,
    }};

    /* Forwards pass, propagates knowledge about the incoming pixel values */
    for (int n = 0; n < ops->num_ops; n++) {
        SwsOp *op = &ops->ops[n];

        switch (op->op) {
        case SWS_OP_READ:
        case SWS_OP_LINEAR:
        case SWS_OP_SWAP_BYTES:
        case SWS_OP_UNPACK:
        case SWS_OP_FILTER_H:
        case SWS_OP_FILTER_V:
            break; /* special cases, handled below */
        default:
            memcpy(op->comps.min, prev.min, sizeof(prev.min));
            memcpy(op->comps.max, prev.max, sizeof(prev.max));
            ff_sws_apply_op_q(op, op->comps.min);
            ff_sws_apply_op_q(op, op->comps.max);
            break;
        }

        switch (op->op) {
        case SWS_OP_READ:
            /* Active components are taken from the user-provided values,
             * other components are explicitly stripped */
            for (int i = 0; i < op->rw.elems; i++) {
                const int idx = op->rw.packed ? i : ops->plane_src[i];
                op->comps.flags[i] = ops->comps_src.flags[idx];
                op->comps.min[i]   = ops->comps_src.min[idx];
                op->comps.max[i]   = ops->comps_src.max[idx];
            }
            for (int i = op->rw.elems; i < 4; i++) {
                op->comps.flags[i] = prev.flags[i];
                op->comps.min[i]   = prev.min[i];
                op->comps.max[i]   = prev.max[i];
            }

            if (op->rw.filter) {
                const SwsComps prev = op->comps;
                apply_filter_weights(&op->comps, &prev, op->rw.kernel);
            }
            break;
        case SWS_OP_SWAP_BYTES:
            for (int i = 0; i < 4; i++) {
                op->comps.flags[i] = prev.flags[i] ^ SWS_COMP_SWAPPED;
                op->comps.min[i]   = prev.min[i];
                op->comps.max[i]   = prev.max[i];
            }
            break;
        case SWS_OP_WRITE:
            for (int i = 0; i < op->rw.elems; i++)
                av_assert1(!(prev.flags[i] & SWS_COMP_GARBAGE));
            /* fall through */
        case SWS_OP_LSHIFT:
        case SWS_OP_RSHIFT:
            propagate_flags(op, &prev);
            break;
        case SWS_OP_MIN:
            propagate_flags(op, &prev);
            clear_undefined_values(op->comps.max, op->c.q4);
            break;
        case SWS_OP_MAX:
            propagate_flags(op, &prev);
            clear_undefined_values(op->comps.min, op->c.q4);
            break;
        case SWS_OP_DITHER:
            /* Strip zero flag because of the nonzero dithering offset */
            for (int i = 0; i < 4; i++)
                op->comps.flags[i] = prev.flags[i] & ~SWS_COMP_ZERO;
            break;
        case SWS_OP_UNPACK:
            for (int i = 0; i < 4; i++) {
                const int pattern = op->pack.pattern[i];
                if (pattern) {
                    av_assert1(pattern < 32);
                    op->comps.flags[i] = prev.flags[0];
                    op->comps.min[i]   = Q(0);
                    op->comps.max[i]   = Q((1ULL << pattern) - 1);
                } else
                    op->comps.flags[i] = SWS_COMP_GARBAGE;
            }
            break;
        case SWS_OP_PACK: {
            SwsCompFlags flags = flags_identity;
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
                    op->comps.flags[i] = 0;
                    if (op->c.q4[i].num == 0)
                        op->comps.flags[i] |= SWS_COMP_ZERO;
                    if (op->c.q4[i].den == 1)
                        op->comps.flags[i] |= SWS_COMP_EXACT;
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
                SwsCompFlags flags = flags_identity;
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
        case SWS_OP_FILTER_H:
        case SWS_OP_FILTER_V: {
            apply_filter_weights(&op->comps, &prev, op->filter.kernel);
            break;
        }

        case SWS_OP_INVALID:
        case SWS_OP_TYPE_NB:
            av_unreachable("Invalid operation type!");
        }

        prev = op->comps;
    }

    /* Backwards pass, solves for component dependencies */
    bool need_out[4] = { false, false, false, false };
    for (int n = ops->num_ops - 1; n >= 0; n--) {
        SwsOp *op = &ops->ops[n];
        bool need_in[4] = { false, false, false, false };

        for (int i = 0; i < 4; i++) {
            if (!need_out[i]) {
                op->comps.flags[i] = SWS_COMP_GARBAGE;
                op->comps.min[i] = op->comps.max[i] = (AVRational) {0};
            }
        }

        switch (op->op) {
        case SWS_OP_READ:
        case SWS_OP_WRITE:
            for (int i = 0; i < op->rw.elems; i++)
                need_in[i] = op->op == SWS_OP_WRITE;
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
                if (!op->c.q4[i].den)
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
        }

        for (int i = 0; i < 4; i++) {
            need_out[i] = need_in[i];
            op->comps.unused[i] = !need_in[i];
        }
    }
}

static void op_uninit(SwsOp *op)
{
    switch (op->op) {
    case SWS_OP_READ:
        av_refstruct_unref(&op->rw.kernel);
        break;
    case SWS_OP_DITHER:
        av_refstruct_unref(&op->dither.matrix);
        break;
    case SWS_OP_FILTER_H:
    case SWS_OP_FILTER_V:
        av_refstruct_unref(&op->filter.kernel);
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
        op_uninit(&ops->ops[i]);

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
        const SwsOp *op = &copy->ops[i];
        switch (op->op) {
        case SWS_OP_READ:
            if (op->rw.kernel)
                av_refstruct_ref(op->rw.kernel);
            break;
        case SWS_OP_DITHER:
            av_refstruct_ref(op->dither.matrix);
            break;
        case SWS_OP_FILTER_H:
        case SWS_OP_FILTER_V:
            av_refstruct_ref(op->filter.kernel);
            break;
        }
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
        op_uninit(&ops->ops[index + i]);
    for (int i = index; i < end; i++)
        ops->ops[i] = ops->ops[i + count];
    ops->num_ops = end;
}

int ff_sws_op_list_insert_at(SwsOpList *ops, int index, SwsOp *op)
{
    void *ret = av_dynarray2_add((void **) &ops->ops, &ops->num_ops, sizeof(*op), NULL);
    if (!ret) {
        op_uninit(op);
        return AVERROR(ENOMEM);
    }

    for (int i = ops->num_ops - 1; i > index; i--)
        ops->ops[i] = ops->ops[i - 1];
    ops->ops[index] = *op;
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
        read->type != write->type ||
        read->rw.packed != write->rw.packed ||
        read->rw.elems != write->rw.elems ||
        read->rw.frac != write->rw.frac)
        return false;

    /**
     * Note that this check is unlikely to ever be hit in practice, since it
     * would imply the existence of planar formats with different plane orders
     * between them, e.g. rgbap <-> gbrap, which doesn't currently exist.
     * However, the check is cheap and lets me sleep at night.
     */
    const int num_planes = read->rw.packed ? 1 : read->rw.elems;
    for (int i = 0; i < num_planes; i++) {
        if (ops->plane_src[i] != ops->plane_dst[i])
            return false;
    }

    return true;
}

int ff_sws_op_list_max_size(const SwsOpList *ops)
{
    int max_size = 0;
    for (int i = 0; i < ops->num_ops; i++) {
        const int size = ff_sws_pixel_type_size(ops->ops[i].type);
        max_size = FFMAX(max_size, size);
    }

    return max_size;
}

uint32_t ff_sws_linear_mask(const SwsLinearOp c)
{
    uint32_t mask = 0;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 5; j++) {
            if (av_cmp_q(c.m[i][j], Q(i == j)))
                mask |= SWS_MASK(i, j);
        }
    }
    return mask;
}

static const char *describe_lin_mask(uint32_t mask)
{
    /* Try to be fairly descriptive without assuming too much */
    static const struct {
        char name[24];
        uint32_t mask;
    } patterns[] = {
        { "noop",               0 },
        { "luma",               SWS_MASK_LUMA },
        { "alpha",              SWS_MASK_ALPHA },
        { "luma+alpha",         SWS_MASK_LUMA | SWS_MASK_ALPHA },
        { "dot3",               0x7 },
        { "dot4",               0xF },
        { "row0",               SWS_MASK_ROW(0) },
        { "row0+alpha",         SWS_MASK_ROW(0) | SWS_MASK_ALPHA },
        { "col0",               SWS_MASK_COL(0) },
        { "col0+off3",          SWS_MASK_COL(0) | SWS_MASK_OFF3 },
        { "off3",               SWS_MASK_OFF3 },
        { "off3+alpha",         SWS_MASK_OFF3 | SWS_MASK_ALPHA },
        { "diag3",              SWS_MASK_DIAG3 },
        { "diag4",              SWS_MASK_DIAG4 },
        { "diag3+alpha",        SWS_MASK_DIAG3 | SWS_MASK_ALPHA },
        { "diag3+off3",         SWS_MASK_DIAG3 | SWS_MASK_OFF3 },
        { "diag3+off3+alpha",   SWS_MASK_DIAG3 | SWS_MASK_OFF3 | SWS_MASK_ALPHA },
        { "diag4+off4",         SWS_MASK_DIAG4 | SWS_MASK_OFF4 },
        { "matrix3",            SWS_MASK_MAT3 },
        { "matrix3+off3",       SWS_MASK_MAT3 | SWS_MASK_OFF3 },
        { "matrix3+off3+alpha", SWS_MASK_MAT3 | SWS_MASK_OFF3 | SWS_MASK_ALPHA },
        { "matrix4",            SWS_MASK_MAT4 },
        { "matrix4+off4",       SWS_MASK_MAT4 | SWS_MASK_OFF4 },
    };

    for (int i = 0; i < FF_ARRAY_ELEMS(patterns); i++) {
        if (!(mask & ~patterns[i].mask))
            return patterns[i].name;
    }

    av_unreachable("Invalid linear mask!");
    return "ERR";
}

static char describe_comp_flags(SwsCompFlags flags)
{
    if (flags & SWS_COMP_GARBAGE)
        return 'X';
    else if (flags & SWS_COMP_ZERO)
        return '0';
    else if (flags & SWS_COMP_SWAPPED)
        return 'z';
    else if (flags & SWS_COMP_EXACT)
        return '+';
    else
        return '.';
}

static void print_q(AVBPrint *bp, const AVRational q, bool ignore_den0)
{
    if (!q.den && ignore_den0) {
        av_bprintf(bp, "_");
    } else if (!q.den) {
        av_bprintf(bp, "%s", q.num > 0 ? "inf" : q.num < 0 ? "-inf" : "nan");
    } else if (q.den == 1) {
        av_bprintf(bp, "%d", q.num);
    } else if (abs(q.num) > 1000 || abs(q.den) > 1000) {
        av_bprintf(bp, "%f", av_q2d(q));
    } else {
        av_bprintf(bp, "%d/%d", q.num, q.den);
    }
}

static void print_q4(AVBPrint *bp, const AVRational q4[4], bool ignore_den0,
                     const bool unused[4])
{
    av_bprintf(bp, "{");
    for (int i = 0; i < 4; i++) {
        if (i)
            av_bprintf(bp, " ");
        if (unused && unused[i]) {
            av_bprintf(bp, "_");
        } else {
            print_q(bp, q4[i], ignore_den0);
        }
    }
    av_bprintf(bp, "}");
}

void ff_sws_op_desc(AVBPrint *bp, const SwsOp *op, const bool unused[4])
{
    const char *name  = ff_sws_op_type_name(op->op);

    switch (op->op) {
    case SWS_OP_INVALID:
    case SWS_OP_SWAP_BYTES:
        av_bprintf(bp, "%s", name);
        break;
    case SWS_OP_READ:
    case SWS_OP_WRITE:
        av_bprintf(bp, "%-20s: %d elem(s) %s >> %d", name,
                   op->rw.elems,  op->rw.packed ? "packed" : "planar",
                   op->rw.frac);
        if (!op->rw.filter)
            break;
        const SwsFilterWeights *kernel = op->rw.kernel;
        av_bprintf(bp, " + %d tap %s filter (%c)",
                   kernel->filter_size, kernel->name,
                   op->rw.filter == SWS_OP_FILTER_H ? 'H' : 'V');
        break;
    case SWS_OP_LSHIFT:
        av_bprintf(bp, "%-20s: << %u", name, op->c.u);
        break;
    case SWS_OP_RSHIFT:
        av_bprintf(bp, "%-20s: >> %u", name, op->c.u);
        break;
    case SWS_OP_PACK:
    case SWS_OP_UNPACK:
        av_bprintf(bp, "%-20s: {%d %d %d %d}", name,
                   op->pack.pattern[0], op->pack.pattern[1],
                   op->pack.pattern[2], op->pack.pattern[3]);
        break;
    case SWS_OP_CLEAR:
        av_bprintf(bp, "%-20s: ", name);
        print_q4(bp, op->c.q4, true, unused);
        break;
    case SWS_OP_SWIZZLE:
        av_bprintf(bp, "%-20s: %d%d%d%d", name,
                   op->swizzle.x, op->swizzle.y, op->swizzle.z, op->swizzle.w);
        break;
    case SWS_OP_CONVERT:
        av_bprintf(bp, "%-20s: %s -> %s%s", name,
                   ff_sws_pixel_type_name(op->type),
                   ff_sws_pixel_type_name(op->convert.to),
                   op->convert.expand ? " (expand)" : "");
        break;
    case SWS_OP_DITHER:
        av_bprintf(bp, "%-20s: %dx%d matrix + {%d %d %d %d}", name,
                   1 << op->dither.size_log2, 1 << op->dither.size_log2,
                   op->dither.y_offset[0], op->dither.y_offset[1],
                   op->dither.y_offset[2], op->dither.y_offset[3]);
        break;
    case SWS_OP_MIN:
        av_bprintf(bp, "%-20s: x <= ", name);
        print_q4(bp, op->c.q4, true, unused);
        break;
    case SWS_OP_MAX:
        av_bprintf(bp, "%-20s: ", name);
        print_q4(bp, op->c.q4, true, unused);
        av_bprintf(bp, " <= x");
        break;
    case SWS_OP_LINEAR:
        av_bprintf(bp, "%-20s: %s [", name, describe_lin_mask(op->lin.mask));
        for (int i = 0; i < 4; i++) {
            av_bprintf(bp, "%s[", i ? " " : "");
            for (int j = 0; j < 5; j++) {
                av_bprintf(bp, j ? " " : "");
                print_q(bp, op->lin.m[i][j], false);
            }
            av_bprintf(bp, "]");
        }
        av_bprintf(bp, "]");
        break;
    case SWS_OP_SCALE:
        av_bprintf(bp, "%-20s: * %d/%d", name, op->c.q.num, op->c.q.den);
        break;
    case SWS_OP_FILTER_H:
    case SWS_OP_FILTER_V: {
        const SwsFilterWeights *kernel = op->filter.kernel;
        av_bprintf(bp, "%-20s: %d -> %d %s (%d taps)", name,
                   kernel->src_size, kernel->dst_size,
                   kernel->name, kernel->filter_size);
        break;
    }
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
    AVBPrint bp;
    if (!ops->num_ops) {
        av_log(log, lev, "  (empty)\n");
        return;
    }

    av_bprint_init(&bp, 0, AV_BPRINT_SIZE_AUTOMATIC);

    for (int i = 0; i < ops->num_ops; i++) {
        const SwsOp *op   = &ops->ops[i];
        const SwsOp *next = i + 1 < ops->num_ops ? &ops->ops[i + 1] : op;
        av_bprint_clear(&bp);
        av_bprintf(&bp, "  [%3s %c%c%c%c -> %c%c%c%c] ",
                   ff_sws_pixel_type_name(op->type),
                   op->comps.unused[0] ? 'X' : '.',
                   op->comps.unused[1] ? 'X' : '.',
                   op->comps.unused[2] ? 'X' : '.',
                   op->comps.unused[3] ? 'X' : '.',
                   next->comps.unused[0] ? 'X' : describe_comp_flags(op->comps.flags[0]),
                   next->comps.unused[1] ? 'X' : describe_comp_flags(op->comps.flags[1]),
                   next->comps.unused[2] ? 'X' : describe_comp_flags(op->comps.flags[2]),
                   next->comps.unused[3] ? 'X' : describe_comp_flags(op->comps.flags[3]));

        ff_sws_op_desc(&bp, op, next->comps.unused);

        if (op->op == SWS_OP_READ || op->op == SWS_OP_WRITE) {
            const int planes = op->rw.packed ? 1 : op->rw.elems;
            desc_plane_order(&bp, planes,
                op->op == SWS_OP_READ ? ops->plane_src : ops->plane_dst);
        }

        av_assert0(av_bprint_is_complete(&bp));
        av_log(log, lev, "%s\n", bp.str);

        if (op->comps.min[0].den || op->comps.min[1].den ||
            op->comps.min[2].den || op->comps.min[3].den ||
            op->comps.max[0].den || op->comps.max[1].den ||
            op->comps.max[2].den || op->comps.max[3].den)
        {
            av_bprint_clear(&bp);
            av_bprintf(&bp, "    min: ");
            print_q4(&bp, op->comps.min, false, next->comps.unused);
            av_bprintf(&bp, ", max: ");
            print_q4(&bp, op->comps.max, false, next->comps.unused);
            av_assert0(av_bprint_is_complete(&bp));
            av_log(log, lev_extra, "%s\n", bp.str);
        }

    }

    av_log(log, lev, "    (X = unused, z = byteswapped, + = exact, 0 = zero)\n");
}

static int enum_ops_fmt(SwsContext *ctx, void *opaque,
                        enum AVPixelFormat src_fmt, enum AVPixelFormat dst_fmt,
                        int (*cb)(SwsContext *ctx, void *opaque, SwsOpList *ops))
{
    int ret;
    const SwsPixelType type = SWS_PIXEL_F32;
    SwsOpList *ops = ff_sws_op_list_alloc();
    if (!ops)
        return AVERROR(ENOMEM);

    ff_fmt_from_pixfmt(src_fmt, &ops->src);
    ff_fmt_from_pixfmt(dst_fmt, &ops->dst);
    ops->src.width  = ops->dst.width  = 16;
    ops->src.height = ops->dst.height = 16;

    bool incomplete = ff_infer_colors(&ops->src.color, &ops->dst.color);
    if (ff_sws_decode_pixfmt(ops, src_fmt) < 0 ||
        ff_sws_decode_colors(ctx, type, ops, &ops->src, &incomplete) < 0 ||
        ff_sws_encode_colors(ctx, type, ops, &ops->src, &ops->dst, &incomplete) < 0 ||
        ff_sws_encode_pixfmt(ops, dst_fmt) < 0)
    {
        ret = 0; /* silently skip unsupported formats */
        goto fail;
    }

    ret = ff_sws_op_list_optimize(ops);
    if (ret < 0)
        goto fail;

    ret = cb(ctx, opaque, ops);
    if (ret < 0)
        goto fail;

fail:
    ff_sws_op_list_free(&ops);
    return ret;
}

int ff_sws_enum_op_lists(SwsContext *ctx, void *opaque,
                         enum AVPixelFormat src_fmt, enum AVPixelFormat dst_fmt,
                         int (*cb)(SwsContext *ctx, void *opaque, SwsOpList *ops))
{
    const AVPixFmtDescriptor *src_start = av_pix_fmt_desc_next(NULL);
    const AVPixFmtDescriptor *dst_start = src_start;
    if (src_fmt != AV_PIX_FMT_NONE)
        src_start = av_pix_fmt_desc_get(src_fmt);
    if (dst_fmt != AV_PIX_FMT_NONE)
        dst_start = av_pix_fmt_desc_get(dst_fmt);

    const AVPixFmtDescriptor *src, *dst;
    for (src = src_start; src; src = av_pix_fmt_desc_next(src)) {
        const enum AVPixelFormat src_f = av_pix_fmt_desc_get_id(src);
        for (dst = dst_start; dst; dst = av_pix_fmt_desc_next(dst)) {
            const enum AVPixelFormat dst_f = av_pix_fmt_desc_get_id(dst);
            int ret = enum_ops_fmt(ctx, opaque, src_f, dst_f, cb);
            if (ret < 0)
                return ret;
            if (dst_fmt != AV_PIX_FMT_NONE)
                break;
        }
        if (src_fmt != AV_PIX_FMT_NONE)
            break;
    }

    return 0;
}

struct EnumOpaque {
    void *opaque;
    int (*cb)(SwsContext *ctx, void *opaque, SwsOp *op);
};

static int enum_ops(SwsContext *ctx, void *opaque, SwsOpList *ops)
{
    struct EnumOpaque *priv = opaque;
    for (int i = 0; i < ops->num_ops; i++) {
        int ret = priv->cb(ctx, priv->opaque, &ops->ops[i]);
        if (ret < 0)
            return ret;
    }
    return 0;
}

int ff_sws_enum_ops(SwsContext *ctx, void *opaque,
                    enum AVPixelFormat src_fmt, enum AVPixelFormat dst_fmt,
                    int (*cb)(SwsContext *ctx, void *opaque, SwsOp *op))
{
    struct EnumOpaque priv = { opaque, cb };
    return ff_sws_enum_op_lists(ctx, &priv, src_fmt, dst_fmt, enum_ops);
}
