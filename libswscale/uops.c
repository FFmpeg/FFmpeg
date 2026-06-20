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
#include "libavutil/tree.h"

#include "ops.h"
#include "ops_internal.h"
#include "uops.h"

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
    char full[32];
    char abbr[32];
    char macro[32];
} uop_names[SWS_UOP_TYPE_NB] = {
#define UOP_NAME(OP, ABBR) [SWS_UOP_##OP] = { "SWS_UOP_" #OP, ABBR, #OP }
    UOP_NAME(INVALID,           "invalid"),
    UOP_NAME(READ_PLANAR,       "read_planar"),
    UOP_NAME(READ_PLANAR_FH,    "read_planar_fh"),
    UOP_NAME(READ_PLANAR_FV,    "read_planar_fv"),
    UOP_NAME(READ_PLANAR_FV_FMA,"read_planar_fv_fma"),
    UOP_NAME(READ_PACKED,       "read_packed"),
    UOP_NAME(READ_NIBBLE,       "read_nibble"),
    UOP_NAME(READ_BIT,          "read_bit"),
    UOP_NAME(READ_PALETTE,      "read_palette"),
    UOP_NAME(WRITE_PLANAR,      "write_planar"),
    UOP_NAME(WRITE_PACKED,      "write_packed"),
    UOP_NAME(WRITE_NIBBLE,      "write_nibble"),
    UOP_NAME(WRITE_BIT,         "write_bit"),
    UOP_NAME(PERMUTE,           "permute"),
    UOP_NAME(COPY,              "copy"),
    UOP_NAME(MOVE,              "move"),
    UOP_NAME(SWAP_BYTES,        "swap_bytes"),
    UOP_NAME(EXPAND_BIT,        "expand_bit"),
    UOP_NAME(EXPAND_PAIR,       "expand_pair"),
    UOP_NAME(EXPAND_QUAD,       "expand_quad"),
    UOP_NAME(TO_U8,             "to_u8"),
    UOP_NAME(TO_U16,            "to_u16"),
    UOP_NAME(TO_U32,            "to_u32"),
    UOP_NAME(TO_F32,            "to_f32"),
    UOP_NAME(SCALE,             "scale"),
    UOP_NAME(LINEAR,            "linear"),
    UOP_NAME(LINEAR_FMA,        "linear_fma"),
    UOP_NAME(ADD,               "add"),
    UOP_NAME(MIN,               "min"),
    UOP_NAME(MAX,               "max"),
    UOP_NAME(UNPACK,            "unpack"),
    UOP_NAME(PACK,              "pack"),
    UOP_NAME(LSHIFT,            "lshift"),
    UOP_NAME(RSHIFT,            "rshift"),
    UOP_NAME(CLEAR,             "clear"),
    UOP_NAME(DITHER,            "dither"),
#undef UOP_NAME
};

static const struct {
    char full[16];
    char prefix[8];
} pixel_types[SWS_PIXEL_TYPE_NB] = {
    [SWS_PIXEL_NONE] = { "SWS_PIXEL_NONE", ""     },
    [SWS_PIXEL_U8]   = { "SWS_PIXEL_U8",   "U8_"  },
    [SWS_PIXEL_U16]  = { "SWS_PIXEL_U16",  "U16_" },
    [SWS_PIXEL_U32]  = { "SWS_PIXEL_U32",  "U32_" },
    [SWS_PIXEL_F32]  = { "SWS_PIXEL_F32",  "F32_" },
};

static SwsPixel pixel_from_q(SwsPixelType type, AVRational val)
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

#define Q2PIXEL(val) pixel_from_q(op->type, val)

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
    case SWS_UOP_LSHIFT:
    case SWS_UOP_RSHIFT:
        av_bprintf(&bp, "_%u", par->shift.amount);
        break;
    case SWS_UOP_PERMUTE:
        av_bprint_chars(&bp, '_', 1);
        for (int i = 0; i < 4; i++)
            av_bprint_chars(&bp, "xyzw"[par->swizzle.in[i]], 1);
        break;
    case SWS_UOP_COPY:
        av_bprint_chars(&bp, '_', 1);
        for (int i = 0; i < 4; i++) {
            if (SWS_COMP_TEST(op->mask, i))
                av_bprint_chars(&bp, "xyzw"[par->swizzle.in[i]], 1);
        }
        break;
    case SWS_UOP_MOVE:
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
    }

    av_assert0(av_bprint_is_complete(&bp));
}

static int generate_entry_struct(void *opaque, void *key)
{
    const SwsUOp *ref = opaque;
    const SwsUOp *uop = key;
    AVBPrint *bp = ref->data.opaque;
    char name[SWS_UOP_NAME_MAX];
    ff_sws_uop_name(uop, name);
    av_bprintf(bp, " \\\n    MACRO(__VA_ARGS__, %-40s", name);
    av_bprintf(bp, ", .type = %-13s, .uop = %-24s, .mask = 0x%x",
               pixel_types[uop->type].full, uop_names[uop->uop].full, uop->mask);

    const SwsUOpParams *par = &uop->par;
    switch (uop->uop) {
    case SWS_UOP_READ_PLANAR_FH:
    case SWS_UOP_READ_PLANAR_FV:
    case SWS_UOP_READ_PLANAR_FV_FMA:
        av_bprintf(bp, ", .par.filter.type = %s", pixel_types[par->filter.type].full);
        break;
    case SWS_UOP_LSHIFT:
    case SWS_UOP_RSHIFT:
        av_bprintf(bp, ", .par.shift.amount = %u", par->shift.amount);
        break;
    case SWS_UOP_PERMUTE:
    case SWS_UOP_COPY:
        av_bprintf(bp, ", .par.swizzle.in = {%d, %d, %d, %d}",
                   par->swizzle.in[0], par->swizzle.in[1],
                   par->swizzle.in[2], par->swizzle.in[3]);
        break;
    case SWS_UOP_MOVE:
        av_bprintf(bp, ", .par.move.num_moves = %d", par->move.num_moves);
        av_bprintf(bp, ", .par.move.dst = {%d, %d, %d, %d, %d, %d}",
                   par->move.dst[0], par->move.dst[1], par->move.dst[2],
                   par->move.dst[3], par->move.dst[4], par->move.dst[5]);
        av_bprintf(bp, ", .par.move.src = {%d, %d, %d, %d, %d, %d}",
                   par->move.src[0], par->move.src[1], par->move.src[2],
                   par->move.src[3], par->move.src[4], par->move.src[5]);
        break;
    case SWS_UOP_PACK:
    case SWS_UOP_UNPACK:
        av_bprintf(bp, ", .par.pack.pattern = {%d, %d, %d, %d}",
                   par->pack.pattern[0], par->pack.pattern[1],
                   par->pack.pattern[2], par->pack.pattern[3]);
        break;
    case SWS_UOP_CLEAR:
        av_bprintf(bp, ", .par.clear.one = 0x%x, .par.clear.zero = 0x%x",
                   par->clear.one, par->clear.zero);
        break;
    case SWS_UOP_LINEAR:
    case SWS_UOP_LINEAR_FMA:
        av_bprintf(bp, ", .par.lin.one = 0x%x, .par.lin.zero = 0x%x",
                   par->lin.one, par->lin.zero);
        if (uop->uop == SWS_UOP_LINEAR_FMA)
            av_bprintf(bp, ", .par.lin.exact = 0x%x", par->lin.exact);
        break;
    case SWS_UOP_DITHER:
        av_bprintf(bp, ", .par.dither = { .y_offset = {%u, %u, %u, %u}, .size_log2 = %u }",
                   par->dither.y_offset[0], par->dither.y_offset[1],
                   par->dither.y_offset[2], par->dither.y_offset[3],
                   par->dither.size_log2);
        break;
    }

    av_bprintf(bp, ")");
    return 0;
}

static int generate_entry_args(void *opaque, void *key)
{
    const SwsUOp *ref = opaque;
    const SwsUOp *uop = key;
    AVBPrint *bp = ref->data.opaque;
    char name[SWS_UOP_NAME_MAX];
    ff_sws_uop_name(uop, name);
    av_bprintf(bp, " \\\n    MACRO(__VA_ARGS__, %-40s, %-13s, %-24s, 0x%x",
               name, pixel_types[uop->type].full, uop_names[uop->uop].full, uop->mask);

    const SwsUOpParams *par = &uop->par;
    switch (uop->uop) {
    case SWS_UOP_READ_PLANAR_FH:
    case SWS_UOP_READ_PLANAR_FV:
    case SWS_UOP_READ_PLANAR_FV_FMA:
        av_bprintf(bp, ", %s", pixel_types[par->filter.type].full);
        break;
    case SWS_UOP_LSHIFT:
    case SWS_UOP_RSHIFT:
        av_bprintf(bp, ", %u", par->shift.amount);
        break;
    case SWS_UOP_PERMUTE:
    case SWS_UOP_COPY:
        av_bprintf(bp, ", %d, %d, %d, %d",
                   par->swizzle.in[0], par->swizzle.in[1],
                   par->swizzle.in[2], par->swizzle.in[3]);
        break;
    case SWS_UOP_MOVE:
        av_bprintf(bp, ", %d", par->move.num_moves);
        av_bprintf(bp, ", %d, %d, %d, %d, %d, %d",
                   par->move.dst[0], par->move.dst[1], par->move.dst[2],
                   par->move.dst[3], par->move.dst[4], par->move.dst[5]);
        av_bprintf(bp, ", %d, %d, %d, %d, %d, %d",
                   par->move.src[0], par->move.src[1], par->move.src[2],
                   par->move.src[3], par->move.src[4], par->move.src[5]);
        break;
    case SWS_UOP_PACK:
    case SWS_UOP_UNPACK:
        av_bprintf(bp, ", %d, %d, %d, %d",
                   par->pack.pattern[0], par->pack.pattern[1],
                   par->pack.pattern[2], par->pack.pattern[3]);
        break;
    case SWS_UOP_CLEAR:
        av_bprintf(bp, ", 0x%05x, 0x%05x", par->clear.one, par->clear.zero);
        break;
    case SWS_UOP_LINEAR:
    case SWS_UOP_LINEAR_FMA:
        av_bprintf(bp, ", 0x%05x, 0x%05x", par->lin.one, par->lin.zero);
        if (uop->uop == SWS_UOP_LINEAR_FMA)
            av_bprintf(bp, ", 0x%05x", par->lin.exact);
        break;
    case SWS_UOP_DITHER:
        av_bprintf(bp, ", %u, %u, %u, %u, %u",
                   par->dither.y_offset[0], par->dither.y_offset[1],
                   par->dither.y_offset[2], par->dither.y_offset[3],
                   par->dither.size_log2);
        break;
    }

    av_bprintf(bp, ")");
    return 0;
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
    const AVRational minq = comps->min[idx];
    const AVRational maxq = comps->max[idx];
    if (ff_sws_pixel_type_is_int(type))
        return true;
    else if (!minq.den || !maxq.den)
        return false; /* unknown bounds */

    const SwsPixel min = pixel_from_q(type, minq);
    const SwsPixel max = pixel_from_q(type, maxq);
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

static int translate_move(SwsUOpList *ops, const SwsOp *op)
{
    SwsUOp uop = {
        .uop  = SWS_UOP_MOVE,
        .type = pixel_type_to_int(op->type),
    };
    SwsMoveUOp *par = &uop.par.move;

    /* Mask of components that are not yet satisfied */
    SwsCompMask todo = ff_sws_comp_mask_needed(op);
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

    return ff_sws_uop_list_append(ops, &uop);
}

static int translate_swizzle(SwsUOpList *ops, SwsUOpFlags flags, const SwsOp *op)
{
    if (flags & SWS_UOP_FLAG_MOVE)
        return translate_move(ops, op);

    SwsUOp uop = {
        .type = pixel_type_to_int(op->type),
        .uop  = SWS_UOP_PERMUTE,
        .par.swizzle.in = {0, 1, 2, 3},
    };

    SwsCompMask needed = ff_sws_comp_mask_needed(op);
    SwsCompMask seen = 0;
    for (int i = 0; i < 4; i++) {
        if (!SWS_COMP_TEST(needed, i))
            continue;
        const int src = op->swizzle.in[i];
        if (SWS_COMP_TEST(seen, src))
            uop.uop = SWS_UOP_COPY; /* Swizzle mask contains duplicates */
        seen |= SWS_COMP(src);
        uop.par.swizzle.in[i] = src;
    }

    if (uop.uop == SWS_UOP_PERMUTE) {
        /* Prevent overlap by moving unused components to unseen indices */
        for (int i = 0; i < 4; i++) {
            if (SWS_COMP_TEST(needed, i))
                continue;

            /* Prefer identity mapping if possible */
            int unused = i;
            if (SWS_COMP_TEST(seen, i)) {
                for (int j = 0; j < 4; j++) {
                    if (!SWS_COMP_TEST(seen, j)) {
                        unused = j;
                        break;
                    }
                }
            }

            uop.par.swizzle.in[i] = unused;
            seen |= SWS_COMP(unused);
        }
    }

    if (uop.uop == SWS_UOP_COPY) {
        /* Remove remaining trivial / identity components from the mask */
        for (int i = 0; i < 4; i++) {
            if (uop.par.swizzle.in[i] == i)
                needed &= ~SWS_COMP(i);
        }

        uop.mask = needed;
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

    const bool bitexact = ctx->flags & SWS_BITEXACT;
    uint32_t exact = 0;

    for (int i = 0; i < 4; i++) {
        if (SWS_OP_NEEDED(op, i) && (op->lin.mask & SWS_MASK_ROW(i)))
            uop.mask |= SWS_COMP(i);
        bool nonzero = (op->lin.m[i][4].num != 0);
        for (int j = 0; j < 5; j++) {
            const AVRational k = op->lin.m[i][j];
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

static bool is_expand_bit(SwsPixelType type, AVRational factor)
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
        return translate_swizzle(uops, flags, op);
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
            const AVRational v = op->clear.value[i];
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
        int ret = translate_op(ctx, uops, flags, &ops->ops[i], &input);
        if (ret < 0)
            return ret;
        input = ops->ops[i].comps;
    }
    return 0;
}

static int register_uop(struct AVTreeNode **root, const SwsUOp *uop)
{
    SwsUOp *key = av_memdup(uop, sizeof(*uop));
    if (!key)
        return AVERROR(ENOMEM);
    memset(&key->data, 0, sizeof(key->data));

    struct AVTreeNode *node = av_tree_node_alloc();
    if (!node) {
        av_free(key);
        return AVERROR(ENOMEM);
    }

    av_tree_insert(root, key, ff_sws_uop_cmp_v, &node);
    if (node) {
        av_free(node);
        av_free(key);
    }
    return 0;
}

static int register_flags(SwsContext *ctx, const SwsOpList *ops, SwsUOpFlags flags)
{
    SwsUOpList *uops = ff_sws_uop_list_alloc();
    if (!uops)
        return AVERROR(ENOMEM);

    int ret = ff_sws_ops_translate(ctx, ops, flags, uops);
    if (ret < 0)
        goto fail;

    struct AVTreeNode **root = ctx->opaque;
    for (int i = 0; i < uops->num_ops; i++) {
        ret = register_uop(root, &uops->ops[i]);
        if (ret < 0)
            goto fail;
    }

fail:
    ff_sws_uop_list_free(&uops);
    return ret;
}

static const SwsUOpFlags uop_flags[] = {
    0,
    SWS_UOP_FLAG_FMA | SWS_UOP_FLAG_MOVE, /* x86 backend */
};

static int register_uops(SwsContext *ctx, const SwsOpList *ops,
                         SwsCompiledOp *out)
{
    for (int i = 0; i < FF_ARRAY_ELEMS(uop_flags); i++) {
        int ret = register_flags(ctx, ops, uop_flags[i]);
        if (ret < 0)
            return ret;
    }

    *out = (SwsCompiledOp) {0}; /* dummy value, will be immediately freed */
    return 0;
}

/* Dummy backend that just registers all seen uops */
static const SwsOpBackend backend_uops = {
    .name    = "uops_gen",
    .compile = register_uops,
};

static int register_all_uops(SwsContext *ctx, void *graph, SwsOpList *ops)
{
    /* ff_sws_compile_pass() takes over ownership of `ops` */
    SwsOpList *copy = ff_sws_op_list_duplicate(ops);
    if (!copy)
        return AVERROR(ENOMEM);

    const int flags = SWS_OP_FLAG_DRY_RUN | SWS_OP_FLAG_SPLIT_MEMCPY;
    return ff_sws_compile_pass(graph, &backend_uops, &copy, flags, NULL, NULL);
}

static const SwsFlags flags[] = {
    0,
    SWS_ACCURATE_RND,   /* may insert extra 1x1 dither ops (for accurate rounding) */
    SWS_BITEXACT,       /* prevents some FMA optimizations */
    SWS_ACCURATE_RND | SWS_BITEXACT,
};

/* Limit the range of av_tree_enumerate() to only matching uop and type */
static int enum_type(void *opaque, void *elem)
{
    const SwsUOp *a = opaque, *b = elem;
    if (a->type != b->type)
        return (int) b->type - a->type;
    if (a->uop != b->uop)
        return (int) b->uop - a->uop;
    return 0;
}

static int free_uop_key(void *opaque, void *key)
{
    av_free(key);
    return 0;
}

int ff_sws_uops_macros_gen(char **out_str)
{
    int ret;
    struct AVTreeNode *root = NULL;

    AVBPrint bprint, *const bp = &bprint;
    av_bprint_init(bp, 0, AV_BPRINT_SIZE_UNLIMITED);

    /* Allocate dummy graph and context for ff_sws_compile_pass() */
    SwsGraph *graph = ff_sws_graph_alloc();
    if (!graph)
        return AVERROR(ENOMEM);

    SwsContext *ctx = graph->ctx = sws_alloc_context();
    if (!ctx) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    /* Use this to plumb the tree state through all the layers of abstraction */
    ctx->opaque = &root;
    ctx->scaler = SWS_SCALE_BILINEAR; /* cheaper to generate filter kernels */

    /* Register all unique uops over every relevant combination of flags */
    for (int i = 0; i < FF_ARRAY_ELEMS(flags); i++) {
        ctx->flags = flags[i];
        ret = ff_sws_enum_op_lists(ctx, graph, AV_PIX_FMT_NONE, AV_PIX_FMT_NONE,
                                   register_all_uops);
        if (ret < 0)
            goto fail;
    }

    /**
     * Additionally make sure planar reads/writes are always available for all
     * formats, because checkasm depends on them to be able to verify the
     * input/output of any other operations.
     */
    for (enum SwsPixelType type = SWS_PIXEL_NONE+1; type < SWS_PIXEL_TYPE_NB; type++) {
        if (!ff_sws_pixel_type_is_int(type))
            continue;
        for (int elems = 1; elems <= 4; elems++) {
            for (int rw = 0; rw < 2; rw++) {
                SwsUOp uop = {
                    .type = type,
                    .uop  = rw ? SWS_UOP_WRITE_PLANAR : SWS_UOP_READ_PLANAR,
                    .mask = SWS_COMP_ELEMS(elems),
                };

                ret = register_uop(&root, &uop);
                if (ret < 0)
                    goto fail;
            }
        }
    }

    #define BPRINT_STR(str) av_bprint_append_data(bp, str, strlen(str))
    BPRINT_STR(
"/**\n"
" * This file is automatically generated. Do not edit manually.\n"
" * To regenerate, run: make fate-sws-uops-macros GEN=1\n"
" */\n"
"\n"
"#ifndef SWSCALE_UOPS_MACROS_H\n"
"#define SWSCALE_UOPS_MACROS_H\n"
"\n"
"/**\n"
" * Boilerplate helper macros, for template-based backends. These will be\n"
" * instantiated like this, with parameters in struct order:\n"
" *   MACRO(__VA_ARGS__, NAME, UOP, TYPE, MASK, [PARAMS,])\n"
" * The _STRUCT variants pass all arguments in C struct syntax, while the\n"
" * plain variants give them as separate C values (e.g. for use in calls)\n"
" */\n"
"#define SWS_GLUE3(x, y, z) x ## _ ## y ## _ ## z\n"
"#define SWS_FOR(TYPE, UOP, MACRO, ...) \\\n"
"    SWS_GLUE3(SWS_FOR, TYPE, UOP)(MACRO, __VA_ARGS__)\n"
"#define SWS_FOR_STRUCT(TYPE, UOP, MACRO, ...) \\\n"
"    SWS_GLUE3(SWS_FOR_STRUCT, TYPE, UOP)(MACRO, __VA_ARGS__)\n"
"\n");

    SwsUOp key = { .data.opaque = bp };
    for (key.type = SWS_PIXEL_NONE + 1; key.type < SWS_PIXEL_TYPE_NB; key.type++) {
        for (key.uop = SWS_UOP_INVALID + 1; key.uop < SWS_UOP_TYPE_NB; key.uop++) {
            const char *macro  = uop_names[key.uop].macro;
            const char *prefix = pixel_types[key.type].prefix;
            av_bprintf(bp, "#define SWS_FOR_%s%s(MACRO, ...)", prefix, macro);
            av_tree_enumerate(root, &key, enum_type, generate_entry_args);
            av_bprintf(bp, "\n");
            av_bprintf(bp, "#define SWS_FOR_STRUCT_%s%s(MACRO, ...)", prefix, macro);
            av_tree_enumerate(root, &key, enum_type, generate_entry_struct);
            av_bprintf(bp, "\n");
        }
    }

    BPRINT_STR("\n#endif /* SWSCALE_UOPS_MACROS_H */");
    ret = av_bprint_finalize(bp, out_str);

fail:
    av_bprint_finalize(bp, NULL);
    av_tree_enumerate(root, NULL, NULL, free_uop_key);
    av_tree_destroy(root);
    ff_sws_graph_free(&graph);
    sws_free_context(&ctx);
    return ret;
}
