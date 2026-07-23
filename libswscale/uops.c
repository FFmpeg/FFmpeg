/**
 * Copyright (C) 2026 Niklas Haas
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

#include <stdbool.h>

#include "libavutil/avassert.h"
#include "libavutil/mem.h"
#include "libavutil/refstruct.h"

#include "ops.h"
#include "uops.h"
#include "uops_list.h"

int ff_sws_uop_cmp(const SwsUOp *a, const SwsUOp *b)
{
    if (a->type != b->type)
        return (int) a->type - b->type;
    if (a->uop != b->uop)
        return (int) a->uop - b->uop;
    if (a->mask != b->mask)
        return (int) a->mask - b->mask;
    return memcmp(&a->par, &b->par, sizeof(a->par));
}

static const struct {
    char abbr[32];
} uop_names[SWS_UOP_TYPE_NB] = {
#define UOP_NAME(OP, ABBR) [OP] = { ABBR },
    UOPS_LIST(UOP_NAME)
#undef UOP_NAME
};

static SwsPixel pixel_from_q64(SwsPixelType type, AVRational64 val)
{
    av_assert1(val.den != 0);
    switch (type) {
    case SWS_PIXEL_U8:  return (SwsPixel) { .u8  = val.num / val.den };
    case SWS_PIXEL_U16: return (SwsPixel) { .u16 = val.num / val.den };
    case SWS_PIXEL_U32: return (SwsPixel) { .u32 = val.num / val.den };
    case SWS_PIXEL_F32: return (SwsPixel) { .f32 = (float) val.num / val.den };
    case SWS_PIXEL_NONE:
    case SWS_PIXEL_TYPE_NB: break;
    }

    av_unreachable("Invalid pixel type!");
    return (SwsPixel) {0};
}

#define Q2PIXEL(val) pixel_from_q64(op->type, val)

static bool pixel_is_1s(SwsPixelType type, SwsPixel val)
{
    switch (ff_sws_pixel_type_size(type)) {
    case 1: return val.u8  == UINT8_MAX;
    case 2: return val.u16 == UINT16_MAX;
    case 4: return val.u32 == UINT32_MAX;
    default: break;
    }

    av_unreachable("Invalid pixel type!");
    return false;
}

void ff_sws_uop_name(const SwsUOp *op, char buf[SWS_UOP_NAME_MAX])
{
    AVBPrint bp;
    av_bprint_init_for_buffer(&bp, buf, SWS_UOP_NAME_MAX);

    if (op->type != SWS_PIXEL_NONE)
        av_bprintf(&bp, "%s_", ff_sws_pixel_type_name(op->type));
    av_bprintf(&bp, "%s", uop_names[op->uop].abbr);

    if (op->mask)
        av_bprintf(&bp, "_%s", ff_sws_comp_mask_str(op->mask));

    const SwsUOpParams *par = &op->par;
    switch (op->uop) {
    case SWS_UOP_READ_PLANAR_FH:
    case SWS_UOP_READ_PLANAR_FV:
    case SWS_UOP_READ_PLANAR_FV_FMA:
        av_bprintf(&bp, "_%s", ff_sws_pixel_type_name(par->filter.type));
        break;
    case SWS_UOP_RW_SHUFFLE:
        av_bprintf(&bp, "_%x_%u_%u", par->shuffle.clear_value,
                   par->shuffle.read_size, par->shuffle.write_size);
        break;
    case SWS_UOP_LSHIFT:
    case SWS_UOP_RSHIFT:
        av_bprintf(&bp, "_%u", par->shift.amount);
        break;
    case SWS_UOP_PERMUTE:
    case SWS_UOP_COPY:
        av_bprint_chars(&bp, '_', 1);
        for (int i = 0; i < par->move.num_moves; i++)
            av_bprint_chars(&bp, "txyzw"[par->move.dst[i] + 1], 1);
        av_bprint_chars(&bp, '_', 1);
        for (int i = 0; i < par->move.num_moves; i++)
            av_bprint_chars(&bp, "txyzw"[par->move.src[i] + 1], 1);
        break;
    case SWS_UOP_PACK:
    case SWS_UOP_UNPACK:
        av_bprint_chars(&bp, '_', 1);
        for (int i = 0; i < 4 && par->pack.pattern[i]; i++)
            av_bprintf(&bp, "%x", par->pack.pattern[i]);
        break;
    case SWS_UOP_CLEAR:
        av_bprint_chars(&bp, '_', 1);
        for (int i = 0; i < 4; i++) {
            if (!SWS_COMP_TEST(op->mask, i))
                continue;
            else if (SWS_COMP_TEST(par->clear.one, i))
                av_bprint_chars(&bp, '1', 1);
            else if (SWS_COMP_TEST(par->clear.zero, i))
                av_bprint_chars(&bp, '0', 1);
            else
                av_bprint_chars(&bp, 'x', 1);
        }
        break;
    case SWS_UOP_LINEAR:
    case SWS_UOP_LINEAR_FMA:
        for (int i = 0; i < 4; i++) {
            if (!SWS_COMP_TEST(op->mask, i))
                continue;
            av_bprint_chars(&bp, '_', 1);
            for (int j = 0; j < 5; j++) {
                if (par->lin.one & SWS_MASK(i, j))
                    av_bprint_chars(&bp, '1', 1);
                else if (par->lin.zero & SWS_MASK(i, j))
                    av_bprint_chars(&bp, '0', 1);
                else if (par->lin.exact & SWS_MASK(i, j))
                    av_bprint_chars(&bp, 'X', 1);
                else
                    av_bprint_chars(&bp, 'x', 1);
            }
        }
        break;
    case SWS_UOP_DITHER:
        for (int i = 0; i < 4; i++) {
            if (SWS_COMP_TEST(op->mask, i))
                av_bprintf(&bp, "_%d", par->dither.y_offset[i]);
        }
        const unsigned size = 1u << par->dither.size_log2;
        av_bprintf(&bp, "_%ux%u", size, size);
        break;
    case SWS_UOP_LUT_3D:
        av_bprintf(&bp, "_%s", par->lut3d.dynamic ? "dynamic" : "static");
        break;
    }

    av_assert0(av_bprint_is_complete(&bp));
}

static void uop_uninit(SwsUOp *uop)
{
    switch (uop->uop) {
    case SWS_UOP_DITHER:
        av_refstruct_unref(&uop->data.ptr);
        break;
    case SWS_UOP_READ_PLANAR_FH:
    case SWS_UOP_READ_PLANAR_FV:
    case SWS_UOP_READ_PLANAR_FV_FMA:
        av_refstruct_unref(&uop->data.kernel);
        break;
    case SWS_UOP_LUT_3D:
        av_refstruct_unref(&uop->data.lut3d);
        break;
    }

    *uop = (SwsUOp) {0};
}

void ff_sws_uop_list_free(SwsUOpList **p_ops)
{
    SwsUOpList *ops = *p_ops;
    if (!ops)
        return;

    for (int i = 0; i < ops->num_ops; i++)
        uop_uninit(&ops->ops[i]);

    av_freep(&ops->ops);
    av_free(ops);
    *p_ops = NULL;
}

SwsUOpList *ff_sws_uop_list_alloc(void)
{
    return av_mallocz(sizeof(SwsUOpList));
}

int ff_sws_uop_list_append(SwsUOpList *uops, SwsUOp *uop)
{
    if (!av_dynarray2_add((void **) &uops->ops, &uops->num_ops,
                          sizeof(*uop), (uint8_t *) uop))
    {
        uop_uninit(uop);
        return AVERROR(ENOMEM);
    }

    *uop = (SwsUOp) {0};
    return 0;
}

void ff_sws_uop_list_remove_at(SwsUOpList *uops, int index, int count)
{
    const int end = uops->num_ops - count;
    av_assert2(index >= 0 && count >= 0 && index + count <= uops->num_ops);
    for (int i = 0; i < count; i++)
        uop_uninit(&uops->ops[index + i]);
    for (int i = index; i < end; i++)
        uops->ops[i] = uops->ops[i + count];
    uops->num_ops = end;
}

int ff_sws_dither_height(const SwsDitherUOp *dither)
{
    int max_offset = 0;
    for (int i = 0; i < 4; i++)
        max_offset = FFMAX(max_offset, dither->y_offset[i]);
    return (1 << dither->size_log2) + max_offset;
}

static SwsPixelType pixel_type_to_int(const SwsPixelType type)
{
    switch (ff_sws_pixel_type_size(type)) {
    case 1: return SWS_PIXEL_U8;
    case 2: return SWS_PIXEL_U16;
    case 4: return SWS_PIXEL_U32;
    default: break;
    }

    av_unreachable("Invalid pixel type!");
    return SWS_PIXEL_NONE;
}

static bool exact_product_f32(float a, float b)
{
    volatile float prod   = a * b;
    volatile float result = b ? prod / b : 0.0f;
    return !b || result == a;
}

static bool exact_prod(SwsPixelType type, SwsPixel coef,
                       const SwsComps *comps, int idx)
{
    const AVRational64 minq = comps->min[idx];
    const AVRational64 maxq = comps->max[idx];
    if (ff_sws_pixel_type_is_int(type))
        return true;
    else if (!minq.den || !maxq.den)
        return false; /* unknown bounds */

    const SwsPixel min = pixel_from_q64(type, minq);
    const SwsPixel max = pixel_from_q64(type, maxq);
    switch (type) {
    case SWS_PIXEL_F32:
        return exact_product_f32(coef.f32, min.f32) &&
               exact_product_f32(coef.f32, max.f32);
    }

    av_unreachable("Invalid pixel type!");
    return false;
}

static bool check_filter_fma(SwsContext *ctx, SwsUOpFlags flags, const SwsOp *op)
{
    if (!(flags & SWS_UOP_FLAG_FMA))
        return false;
    if (!(ctx->flags & SWS_BITEXACT))
        return true;
    if (!ff_sws_pixel_type_is_int(op->type))
        return false;

    const int bits = ff_sws_pixel_type_size(op->type) * 8;
    const uint64_t max_val = UINT64_MAX >> (64 - bits);

    /* Maximum value representable losslessly as float. Note that this is
     * currently true only for U8, but that may change if we ever update the
     * value of SWS_FILTER_SCALE. */
    return max_val * SWS_FILTER_SCALE <= (1 << 22);
}

static int translate_rw_op(SwsContext *ctx, SwsUOpList *ops, SwsUOpFlags flags,
                           const SwsOp *op)
{
    SwsUOp uop = {
        .type = op->type,
        .mask = SWS_COMP_MASK(op->rw.elems > 0, op->rw.elems > 1,
                              op->rw.elems > 2, op->rw.elems > 3),
    };

    /* Non-filtered reads don't care about the exact pixel contents */
    if (!op->rw.filter.op)
        uop.type = pixel_type_to_int(op->type);

    const bool is_read = op->op == SWS_OP_READ;
    if (op->rw.filter.op) {
        if (op->op == SWS_OP_WRITE || op->rw.frac || op->rw.mode != SWS_RW_PLANAR)
            return AVERROR(ENOTSUP);
        uop.par.filter.type = op->rw.filter.type;
        uop.data.kernel = av_refstruct_ref(op->rw.filter.kernel);
        if (op->rw.filter.op == SWS_OP_FILTER_H) {
            uop.uop = SWS_UOP_READ_PLANAR_FH;
        } else if (check_filter_fma(ctx, flags, op)) {
            uop.uop = SWS_UOP_READ_PLANAR_FV_FMA;
        } else {
            uop.uop = SWS_UOP_READ_PLANAR_FV;
        }
    } else if (op->rw.mode == SWS_RW_PACKED && op->rw.elems > 1) {
        if (op->rw.frac)
            return AVERROR(ENOTSUP);
        uop.uop = is_read ? SWS_UOP_READ_PACKED : SWS_UOP_WRITE_PACKED;
    } else if (op->rw.mode == SWS_RW_PALETTE) {
        if (op->rw.frac || !is_read)
            return AVERROR(ENOTSUP);
        uop.uop = SWS_UOP_READ_PALETTE;
    } else if (op->rw.frac == 3) {
        uop.uop = is_read ? SWS_UOP_READ_BIT : SWS_UOP_WRITE_BIT;
    } else if (op->rw.frac == 1) {
        uop.uop = is_read ? SWS_UOP_READ_NIBBLE : SWS_UOP_WRITE_NIBBLE;
    } else {
        av_assert0(!op->rw.frac);
        uop.uop = is_read ? SWS_UOP_READ_PLANAR : SWS_UOP_WRITE_PLANAR;
    }

    const int planes = ff_sws_rw_op_planes(op);
    if (op->op == SWS_OP_READ) {
        ops->planes_in  |= SWS_COMP_ELEMS(planes);
    } else {
        ops->planes_out |= SWS_COMP_ELEMS(planes);
    }

    return ff_sws_uop_list_append(ops, &uop);
}

static int count_idx(const int *arr, size_t size, int val)
{
    int num = 0;
    for (size_t i = 0; i < size; i++) {
        if (arr[i] == val)
            num++;
    }

    return num;
}

static int translate_swizzle(SwsUOpList *ops, const SwsOp *op)
{
    SwsUOp uop = {
        .uop  = SWS_UOP_PERMUTE,
        .type = pixel_type_to_int(op->type),
        .mask = ff_sws_comp_mask_needed(op),
    };
    SwsMoveUOp *par = &uop.par.move;

    /* Mask of components that are not yet satisfied */
    SwsCompMask todo = uop.mask;
    for (int i = 0; i < 4; i++) {
        if (op->swizzle.in[i] == i)
            todo &= ~SWS_COMP(i);
    }

    /* Mask of components whose value is required for the final output */
    SwsCompMask needed = 0;
    for (int i = 0; i < 4; i++) {
        if (SWS_OP_NEEDED(op, i))
            needed |= SWS_COMP(op->swizzle.in[i]);
    }

    /* Current mapping of registers to components */
    int idx[4 + 1] = { 0, 1, 2, 3, -1 }; /* +1 for tmp */

    /* Decompose the swizzle mask into a series of register-register moves */
    while (todo) {
        int dst = -1, src = -1;

        /* Find next unsatisfied dst <- src move that doesn't clobber a value */
        for (dst = 0; dst < 4; dst++) {
            if (!SWS_COMP_TEST(todo, dst))
                continue; /* already satisfied */
            const int cur = idx[dst];
            if (count_idx(idx, FF_ARRAY_ELEMS(idx), cur) == 1 && SWS_COMP_TEST(needed, cur))
                continue; /* clobbers last remaining, still-needed value */
            for (src = 0; src < FF_ARRAY_ELEMS(idx); src++) {
                if (idx[src] == op->swizzle.in[dst]) {
                    /* Prevent read-after-write dependency. */
                    if (par->num_moves > 0 && src == par->dst[par->num_moves - 1])
                        src = par->src[par->num_moves - 1];
                    break;
                }
            }
            av_assert1(src < FF_ARRAY_ELEMS(idx));
            todo &= ~SWS_COMP(dst);
            break;
        }

        if (dst == 4) {
            /* Stuck in a cycle, break it by saving to the scratch register */
            dst = 4;
            for (src = 0; src < 4; src++) {
                if (SWS_COMP_TEST(todo, src)) {
                    needed &= ~SWS_COMP(idx[src]);
                    break;
                }
            }
            av_assert1(src < 4);
        }

        av_assert0(par->num_moves < SWS_UOP_MOVE_MAX);
        par->dst[par->num_moves] = dst > 3 ? -1 : dst;
        par->src[par->num_moves] = src > 3 ? -1 : src;
        par->num_moves++;
        idx[dst] = idx[src];
    }

    /* Check for duplicates in the final register map */
    SwsCompMask seen = 0;
    for (int i = 0; i < 4; i++) {
        if (!SWS_COMP_TEST(uop.mask, i))
            continue;
        av_assert2(idx[i] >= 0); /* should be no tmp register */
        const SwsCompMask bit = SWS_COMP(idx[i]);
        if (seen & bit) {
            uop.uop = SWS_UOP_COPY;
            break;
        }
        seen |= bit;
    }

    /* Add any extra unused components to the mask, to prevent generating
     * duplicate uops like permute_xyz_txy_xyt and permute_xyzw_txy_xyt */
    for (int i = 0; i < 4; i++) {
        const SwsCompMask bit = SWS_COMP(i);
        if (!(seen & bit) && idx[i] == i)
            uop.mask |= bit;
    }

    return ff_sws_uop_list_append(ops, &uop);
}

static int translate_dither_op(SwsUOpList *ops, const SwsOp *op)
{
    SwsUOp uop = {
        .type = op->type,
        .uop  = SWS_UOP_DITHER,
        .par.dither.size_log2 = op->dither.size_log2,
    };

    if (op->dither.size_log2 == 0) {
        /* Constant offset */
        const SwsPixel val = Q2PIXEL(op->dither.matrix[0]);
        uop.uop = SWS_UOP_ADD;
        for (int i = 0; i < 4; i++) {
            if (!SWS_OP_NEEDED(op, i) || op->dither.y_offset[i] < 0)
                continue;
            uop.mask |= SWS_COMP(i);
            uop.data.vec4[i] = val;
        }

        return ff_sws_uop_list_append(ops, &uop);
    }

    const int size = 1 << op->dither.size_log2;
    for (int i = 0; i < 4; i++) {
        if (!SWS_OP_NEEDED(op, i) || op->dither.y_offset[i] < 0)
            continue;
        const uint8_t off = op->dither.y_offset[i] & (size - 1);
        uop.mask |= SWS_COMP(i);
        uop.par.dither.y_offset[i] = off;
    }

    /* Allocate extra rows to allow over-reading for row offsets. Note that
     * y_offset is currently never larger than 5, so the extra space needed
     * for this over-allocation is bounded by 5 * size * sizeof(float),
     * typically 320 bytes for a 16x16 dither matrix. */
    const int stride   = size * sizeof(SwsPixel);
    const int num_rows = ff_sws_dither_height(&uop.par.dither);
    SwsPixel *matrix = uop.data.ptr = av_refstruct_allocz(num_rows * stride);
    if (!matrix)
        return AVERROR(ENOMEM);

    for (int i = 0; i < size * size; i++)
        matrix[i] = Q2PIXEL(op->dither.matrix[i]);
    memcpy(&matrix[size * size], matrix, (num_rows - size) * stride);

    return ff_sws_uop_list_append(ops, &uop);
}

static int translate_linear_op(SwsContext *ctx, SwsUOpList *ops,
                               SwsUOpFlags flags, const SwsOp *op,
                               const SwsComps *input)
{
    SwsUOp uop = {
        .type = op->type,
        .uop  = SWS_UOP_LINEAR,
    };

    const uint32_t mask = ff_sws_linear_mask(&op->lin);
    const bool bitexact = ctx->flags & SWS_BITEXACT;
    uint32_t exact = 0;

    for (int i = 0; i < 4; i++) {
        if (!SWS_OP_NEEDED(op, i) || !(mask & SWS_MASK_ROW(i))) {
            uop.par.lin.zero |= SWS_MASK_ROW(i);
            continue;
        }
        uop.mask |= SWS_COMP(i);
        bool nonzero = (op->lin.m[i][4].num != 0);
        for (int j = 0; j < 5; j++) {
            const AVRational64 k = op->lin.m[i][j];
            const SwsPixel px = Q2PIXEL(k);
            uop.data.mat4[i][j] = px;
            if (k.num == 0)
                uop.par.lin.zero |= SWS_MASK(i, j);
            else if (j < 4 && k.num == k.den)
                uop.par.lin.one |= SWS_MASK(i, j);
            else if (j < 4 && nonzero && (!bitexact || exact_prod(uop.type, px, input, j)))
                exact |= SWS_MASK(i, j);
            if (k.num != 0)
                nonzero = true;
        }
    }

    if (flags & SWS_UOP_FLAG_FMA) {
        /* multiplication by 1 and 0 are always exact by definition */
        uop.uop = SWS_UOP_LINEAR_FMA;
        uop.par.lin.exact = exact | uop.par.lin.zero | uop.par.lin.one;
    }

    return ff_sws_uop_list_append(ops, &uop);
}

static bool is_expand_bit(SwsPixelType type, AVRational64 factor)
{
    if (factor.den != 1)
        return false;

    switch (type) {
    case SWS_PIXEL_U8:  return factor.num == UINT8_MAX;
    case SWS_PIXEL_U16: return factor.num == UINT16_MAX;
    case SWS_PIXEL_U32: return factor.num == UINT32_MAX;
    case SWS_PIXEL_F32: return false;
    case SWS_PIXEL_NONE:
    case SWS_PIXEL_TYPE_NB: break;
    }

    av_unreachable("Invalid pixel type!");
    return false;
}

static int translate_op(SwsContext *ctx, SwsUOpList *uops, SwsUOpFlags flags,
                        const SwsOp *op, const SwsComps *input)
{
    switch (op->op) {
    case SWS_OP_FILTER_H:
    case SWS_OP_FILTER_V:
        return AVERROR(ENOTSUP); /* always handled by subpass splitting */
    case SWS_OP_READ:
    case SWS_OP_WRITE:
        return translate_rw_op(ctx, uops, flags, op);
    case SWS_OP_SWIZZLE:
        return translate_swizzle(uops, op);
    case SWS_OP_DITHER:
        return translate_dither_op(uops, op);
    case SWS_OP_LINEAR:
        return translate_linear_op(ctx, uops, flags, op, input);
    default:
        break;
    }

    /* Default handling for "simple" ops */
    SwsUOp uop = {
        .type = op->type,
        .uop  = SWS_UOP_INVALID,
        .mask = ff_sws_comp_mask_needed(op),
    };

    switch (op->op) {
    case SWS_OP_CONVERT:
        if (op->convert.expand) {
            av_assert0(op->type == SWS_PIXEL_U8);
            switch (op->convert.to) {
            case SWS_PIXEL_U16: uop.uop = SWS_UOP_EXPAND_PAIR; break;
            case SWS_PIXEL_U32: uop.uop = SWS_UOP_EXPAND_QUAD; break;
            }
        } else {
            switch (op->convert.to) {
            case SWS_PIXEL_U8:  uop.uop = SWS_UOP_TO_U8;  break;
            case SWS_PIXEL_U16: uop.uop = SWS_UOP_TO_U16; break;
            case SWS_PIXEL_U32: uop.uop = SWS_UOP_TO_U32; break;
            case SWS_PIXEL_F32: uop.uop = SWS_UOP_TO_F32; break;
            }
        }
        break;
    case SWS_OP_UNPACK:
    case SWS_OP_PACK:
        uop.uop = op->op == SWS_OP_PACK ? SWS_UOP_PACK : SWS_UOP_UNPACK;
        uop.mask = 0;
        for (int i = 0; i < 4 && op->pack.pattern[i]; i++) {
            uop.par.pack.pattern[i] = op->pack.pattern[i];
            if (op->op == SWS_OP_PACK || SWS_OP_NEEDED(op, i))
                uop.mask |= SWS_COMP(i);
        }
        break;
    case SWS_OP_LSHIFT:
    case SWS_OP_RSHIFT:
        uop.uop = op->op == SWS_OP_LSHIFT ? SWS_UOP_LSHIFT : SWS_UOP_RSHIFT;
        uop.par.shift.amount = op->shift.amount;
        break;
    case SWS_OP_CLEAR:
        uop.uop = SWS_UOP_CLEAR;
        uop.type = pixel_type_to_int(op->type);
        uop.mask &= op->clear.mask;
        for (int i = 0; i < 4; i++) {
            if (!SWS_COMP_TEST(op->clear.mask, i))
                continue;
            const AVRational64 v = op->clear.value[i];
            const SwsPixel px = Q2PIXEL(op->clear.value[i]);
            uop.data.vec4[i] = px;
            if (v.num == 0)
                uop.par.clear.zero |= SWS_COMP(i);
            else if (pixel_is_1s(op->type, px))
                uop.par.clear.one |= SWS_COMP(i);
        }
        break;
    case SWS_OP_SCALE:
        if (is_expand_bit(op->type, op->scale.factor)) {
            uop.uop = SWS_UOP_EXPAND_BIT;
        } else {
            uop.uop = SWS_UOP_SCALE;
            uop.data.scalar = Q2PIXEL(op->scale.factor);
        }
        break;
    case SWS_OP_MIN:
    case SWS_OP_MAX:
        uop.uop = op->op == SWS_OP_MIN ? SWS_UOP_MIN : SWS_UOP_MAX;
        uop.mask &= ff_sws_comp_mask_q4(op->clamp.limit);
        for (int i = 0; i < 4; i++) {
            if (SWS_COMP_TEST(uop.mask, i))
                uop.data.vec4[i] = Q2PIXEL(op->clamp.limit[i]);
        }
        break;
    case SWS_OP_SWAP_BYTES:
        uop.uop = SWS_UOP_SWAP_BYTES;
        uop.type = pixel_type_to_int(op->type);
        break;
    case SWS_OP_LUT_3D:
        uop.uop = SWS_UOP_LUT_3D;
        uop.par.lut3d.dynamic = op->lut3d.dynamic;
        uop.data.lut3d = av_refstruct_ref_c(op->lut3d.lut);
        break;
    default:
        return AVERROR(ENOTSUP);
    }

    av_assert0(uop.uop != SWS_UOP_INVALID);
    return ff_sws_uop_list_append(uops, &uop);
}

int ff_sws_ops_translate(SwsContext *ctx, const SwsOpList *ops,
                         SwsUOpFlags flags, SwsUOpList *uops)
{
    SwsComps input = ops->comps_src;
    for (int i = 0; i < ops->num_ops; i++) {
        const SwsOp *op = &ops->ops[i];
        const int pixel_size = ff_sws_pixel_type_size(op->type);
        if (pixel_size > uops->pixel_size_max)
            uops->pixel_size_max = pixel_size;

        int ret = translate_op(ctx, uops, flags, op, &input);
        if (ret < 0)
            return ret;
        input = ops->ops[i].comps;
    }

    return ff_sws_uop_list_optimize(ctx, flags, uops);
}
