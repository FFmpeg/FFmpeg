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
    char full[24];
    char abbr[16];
} uop_names[SWS_UOP_TYPE_NB] = {
#define UOP_NAME(OP, ABBR) [SWS_UOP_##OP] = { "SWS_UOP_" #OP, ABBR }
    UOP_NAME(INVALID,           "invalid"),
    UOP_NAME(READ_PLANAR,       "read_planar"),
    UOP_NAME(READ_PLANAR_FH,    "read_planar_fh"),
    UOP_NAME(READ_PLANAR_FV,    "read_planar_fv"),
    UOP_NAME(READ_PACKED,       "read_packed"),
    UOP_NAME(READ_NIBBLE,       "read_nibble"),
    UOP_NAME(READ_BIT,          "read_bit"),
    UOP_NAME(WRITE_PLANAR,      "write_planar"),
    UOP_NAME(WRITE_PACKED,      "write_packed"),
    UOP_NAME(WRITE_NIBBLE,      "write_nibble"),
    UOP_NAME(WRITE_BIT,         "write_bit"),
    UOP_NAME(PERMUTE,           "permute"),
    UOP_NAME(COPY,              "copy"),
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

    if (op->mask) {
        av_bprint_chars(&bp, '_', 1);
        for (int i = 0; i < 4; i++) {
            if (SWS_COMP_TEST(op->mask, i))
                av_bprint_chars(&bp, "xyzw"[i], 1);
        }
    }

    const SwsUOpParams *par = &op->par;
    switch (op->uop) {
    case SWS_UOP_LSHIFT:
    case SWS_UOP_RSHIFT:
        av_bprintf(&bp, "_%u", par->shift.amount);
        break;
    case SWS_UOP_PERMUTE:
    case SWS_UOP_COPY:
        av_bprint_chars(&bp, '_', 1);
        for (int i = 0; i < 4; i++) {
            if (SWS_COMP_TEST(op->mask, i))
                av_bprint_chars(&bp, "xyzw"[par->swizzle.in[i]], 1);
        }
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
        for (int i = 0; i < 4; i++) {
            if (!SWS_COMP_TEST(op->mask, i))
                continue;
            av_bprint_chars(&bp, '_', 1);
            for (int j = 0; j < 5; j++) {
                if (par->lin.one & SWS_MASK(i, j))
                    av_bprint_chars(&bp, '1', 1);
                else if (par->lin.zero & SWS_MASK(i, j))
                    av_bprint_chars(&bp, '0', 1);
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

static void uop_uninit(SwsUOp *uop)
{
    switch (uop->uop) {
    case SWS_UOP_DITHER:
        av_refstruct_unref(&uop->data.ptr);
        break;
    case SWS_UOP_READ_PLANAR_FH:
    case SWS_UOP_READ_PLANAR_FV:
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

static int translate_rw_op(SwsUOpList *ops, const SwsOp *op)
{
    SwsUOp uop = {
        .type = op->type,
        .mask = SWS_COMP_MASK(op->rw.elems > 0, op->rw.elems > 1,
                              op->rw.elems > 2, op->rw.elems > 3),
    };

    /* Non-filtered reads don't care about the exact pixel contents */
    if (!op->rw.filter)
        uop.type = pixel_type_to_int(op->type);

    const bool is_read = op->op == SWS_OP_READ;
    if (op->rw.filter) {
        if (op->op == SWS_OP_WRITE || op->rw.frac || op->rw.packed)
            return AVERROR(ENOTSUP);
        uop.uop = op->rw.filter == SWS_OP_FILTER_H
                    ? SWS_UOP_READ_PLANAR_FH
                    : SWS_UOP_READ_PLANAR_FV;
        uop.data.kernel = av_refstruct_ref(op->rw.kernel);
    } else if (op->rw.packed && op->rw.elems > 1) {
        if (op->rw.frac)
            return AVERROR(ENOTSUP);
        uop.uop = is_read ? SWS_UOP_READ_PACKED : SWS_UOP_WRITE_PACKED;
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

static int translate_swizzle(SwsUOpList *ops, const SwsOp *op)
{
    SwsUOp uop = {
        .type = pixel_type_to_int(op->type),
        .uop  = SWS_UOP_PERMUTE,
        .mask = ff_sws_comp_mask_needed(op),
        .par.swizzle.in = {0, 1, 2, 3},
    };

    SwsCompMask seen = 0;
    for (int i = 0; i < 4; i++) {
        if (!SWS_COMP_TEST(uop.mask, i))
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
            if (SWS_COMP_TEST(uop.mask, i))
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

    /* Remove remaining trivial / identity components from the mask */
    for (int i = 0; i < 4; i++) {
        if (uop.par.swizzle.in[i] == i)
            uop.mask &= ~SWS_COMP(i);
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

static int translate_linear_op(SwsUOpList *ops, const SwsOp *op)
{
    SwsUOp uop = {
        .type = op->type,
        .uop  = SWS_UOP_LINEAR,
    };

    for (int i = 0; i < 4; i++) {
        if (SWS_OP_NEEDED(op, i) && (op->lin.mask & SWS_MASK_ROW(i)))
            uop.mask |= SWS_COMP(i);
        for (int j = 0; j < 5; j++) {
            const AVRational k = op->lin.m[i][j];
            uop.data.mat4[i][j] = Q2PIXEL(k);
            if (k.num == 0)
                uop.par.lin.zero |= SWS_MASK(i, j);
            else if (k.num == k.den)
                uop.par.lin.one |= SWS_MASK(i, j);
        }
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

static int translate_op(SwsUOpList *uops, const SwsOp *op)
{
    switch (op->op) {
    case SWS_OP_FILTER_H:
    case SWS_OP_FILTER_V:
        return AVERROR(ENOTSUP); /* always handled by subpass splitting */
    case SWS_OP_READ:
    case SWS_OP_WRITE:
        return translate_rw_op(uops, op);
    case SWS_OP_SWIZZLE:
        return translate_swizzle(uops, op);
    case SWS_OP_DITHER:
        return translate_dither_op(uops, op);
    case SWS_OP_LINEAR:
        return translate_linear_op(uops, op);
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
    }

    av_assert0(uop.uop != SWS_UOP_INVALID);
    return ff_sws_uop_list_append(uops, &uop);
}

int ff_sws_ops_translate(const SwsOpList *ops, SwsUOpList *uops)
{
    for (int i = 0; i < ops->num_ops; i++) {
        int ret = translate_op(uops, &ops->ops[i]);
        if (ret < 0)
            return ret;
    }
    return 0;
}
