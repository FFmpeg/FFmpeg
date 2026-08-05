/*
 * Copyright (C) 2026 Ramiro Polla
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

/**
 * NOTE: This file is #include'd directly by both the NEON backend and
 *       the sws_ops_aarch64 tool.
 */

#include "libavutil/error.h"
#include "libavutil/rational.h"
#include "libswscale/ops.h"

#include "ops_impl.h"

static void swizzle_emit(SwsAArch64OpImplParams *out, uint8_t dst, uint8_t src)
{
    int idx = out->par.move.num_moves++;
    out->par.move.dst[idx] = dst;
    out->par.move.src[idx] = src;
}

static void convert_swizzle_to_moves(const SwsOp *op, SwsAArch64OpImplParams *out)
{
    SwsSwizzleOp swizzle = {
        .in = {
            op->swizzle.in[0],
            op->swizzle.in[1],
            op->swizzle.in[2],
            op->swizzle.in[3],
        }
    };

    /* Compute used vectors (src and dst) */
    uint8_t src_used[4] = { 0 };
    bool done[4] = { true, true, true, true };
    LOOP(out->mask, dst) {
        uint8_t src = swizzle.in[dst];
        src_used[src]++;
        done[dst] = false;
    }

    /* First perform unobstructed copies. */
    for (bool progress = true; progress; ) {
        progress = false;
        for (int dst = 0; dst < 4; dst++) {
            if (done[dst] || src_used[dst])
                continue;
            uint8_t src = swizzle.in[dst];
            swizzle_emit(out, dst, src);
            src_used[src]--;
            done[dst] = true;
            progress = true;
        }
    }

    /* Then swap and rotate remaining operations. */
    for (int dst = 0; dst < 4; dst++) {
        if (done[dst])
            continue;

        swizzle_emit(out, -1, dst);

        uint8_t cur_dst = dst;
        uint8_t src = swizzle.in[cur_dst];
        while (src != dst) {
            swizzle_emit(out, cur_dst, src);
            done[cur_dst] = true;
            cur_dst = src;
            src = swizzle.in[cur_dst];
        }

        swizzle_emit(out, cur_dst, -1);
        done[cur_dst] = true;
    }
}

/**
 * Convert SwsOp to a SwsAArch64OpImplParams. Read the comments regarding
 * SwsAArch64OpImplParams in ops_impl.h for more information.
 */
static int convert_to_aarch64_impl(SwsContext *ctx, const SwsOpList *ops, int n,
                                   int block_size, SwsAArch64OpImplParams *out)
{
    const SwsOp *op = &ops->ops[n];

    out->block_size = block_size;

    /**
     * Most SwsOp work on fields described by SWS_OP_NEEDED().
     * The few that don't will override this field later.
     */
    out->mask = 0;
    for (int i = 0; i < 4; i++) {
        if (SWS_OP_NEEDED(op, i))
            out->mask |= SWS_COMP(i);
    }

    out->type = op->type;

    /* Map SwsOpType to SwsUOpType */
    switch (op->op) {
    case SWS_OP_READ:
        if (op->rw.filter.op)
            return AVERROR(ENOTSUP);
        /**
         * The different types of read operations have been split into
         * their own SwsUOpType to simplify the implementation.
         */
        if (op->rw.frac == 1)
            out->uop = SWS_UOP_READ_NIBBLE;
        else if (op->rw.frac == 3)
            out->uop = SWS_UOP_READ_BIT;
        else if (op->rw.mode == SWS_RW_PACKED && op->rw.elems > 1)
            out->uop = SWS_UOP_READ_PACKED;
        else if (op->rw.mode == SWS_RW_PACKED || op->rw.mode == SWS_RW_PLANAR)
            out->uop = SWS_UOP_READ_PLANAR;
        else
            return AVERROR(ENOTSUP);
        break;
    case SWS_OP_WRITE:
        if (op->rw.filter.op)
            return AVERROR(ENOTSUP);
        /**
         * The different types of write operations have been split into
         * their own SwsUOpType to simplify the implementation.
         */
        if (op->rw.frac == 1)
            out->uop = SWS_UOP_WRITE_NIBBLE;
        else if (op->rw.frac == 3)
            out->uop = SWS_UOP_WRITE_BIT;
        else if (op->rw.mode == SWS_RW_PACKED && op->rw.elems > 1)
            out->uop = SWS_UOP_WRITE_PACKED;
        else if (op->rw.mode == SWS_RW_PACKED || op->rw.mode == SWS_RW_PLANAR)
            out->uop = SWS_UOP_WRITE_PLANAR;
        else
            return AVERROR(ENOTSUP);
        break;
    case SWS_OP_SWAP_BYTES: out->uop = SWS_UOP_SWAP_BYTES; break;
    case SWS_OP_SWIZZLE: {
        /**
         * Detect whether copies are needed or if a simple permute is
         * enough.
         */
        out->uop = SWS_UOP_PERMUTE;
        SwsCompMask seen = 0;
        LOOP(out->mask, i) {
            uint8_t src = op->swizzle.in[i];
            if (seen & SWS_COMP(src)) {
                out->uop = SWS_UOP_COPY;
                break;
            }
            seen |= SWS_COMP(src);
        }
        break;
    }
    case SWS_OP_UNPACK:     out->uop = SWS_UOP_UNPACK;     break;
    case SWS_OP_PACK:       out->uop = SWS_UOP_PACK;       break;
    case SWS_OP_LSHIFT:     out->uop = SWS_UOP_LSHIFT;     break;
    case SWS_OP_RSHIFT:     out->uop = SWS_UOP_RSHIFT;     break;
    case SWS_OP_CLEAR:      out->uop = SWS_UOP_CLEAR;      break;
    case SWS_OP_CONVERT:
        if (op->convert.expand) {
            switch (op->convert.to) {
            case SWS_PIXEL_U16: out->uop = SWS_UOP_EXPAND_PAIR; break;
            case SWS_PIXEL_U32: out->uop = SWS_UOP_EXPAND_QUAD; break;
            }
        } else {
            switch (op->convert.to) {
            case SWS_PIXEL_U8:  out->uop = SWS_UOP_TO_U8;  break;
            case SWS_PIXEL_U16: out->uop = SWS_UOP_TO_U16; break;
            case SWS_PIXEL_U32: out->uop = SWS_UOP_TO_U32; break;
            case SWS_PIXEL_F32: out->uop = SWS_UOP_TO_F32; break;
            }
        }
        break;
    case SWS_OP_MIN:
    case SWS_OP_MAX:
        out->uop = (op->op == SWS_OP_MIN) ? SWS_UOP_MIN : SWS_UOP_MAX;
        out->mask &= ff_sws_comp_mask_q4(op->clamp.limit);
        break;
    case SWS_OP_SCALE:      out->uop = SWS_UOP_SCALE;      break;
    case SWS_OP_LINEAR:
        out->uop = (ctx->flags & SWS_BITEXACT)
                 ? SWS_UOP_LINEAR
                 : SWS_UOP_LINEAR_FMA;
        break;
    case SWS_OP_DITHER:     out->uop = SWS_UOP_DITHER;     break;
    default:
        return AVERROR(ENOTSUP);
    }

    switch (out->uop) {
    case SWS_UOP_READ_BIT:
    case SWS_UOP_READ_NIBBLE:
    case SWS_UOP_READ_PACKED:
    case SWS_UOP_READ_PLANAR:
    case SWS_UOP_WRITE_BIT:
    case SWS_UOP_WRITE_NIBBLE:
    case SWS_UOP_WRITE_PACKED:
    case SWS_UOP_WRITE_PLANAR:
        switch (op->rw.elems) {
        case 1: out->mask = SWS_COMP_ELEMS(1); break;
        case 2: out->mask = SWS_COMP_ELEMS(2); break;
        case 3: out->mask = SWS_COMP_ELEMS(3); break;
        case 4: out->mask = SWS_COMP_ELEMS(4); break;
        };
        break;
    case SWS_UOP_PERMUTE:
    case SWS_UOP_COPY:
        /* Recompute mask taking identity swizzle into account */
        out->mask = 0;
        for (int i = 0; i < 4; i++) {
            if (SWS_OP_NEEDED(op, i) && op->swizzle.in[i] != i)
                out->mask |= SWS_COMP(i);
        }
        convert_swizzle_to_moves(op, out);
        /* The element size and type don't matter. */
        out->block_size = block_size * ff_sws_pixel_type_size(op->type);
        out->type = SWS_PIXEL_U8;
        break;
    case SWS_UOP_UNPACK:
        for (int i = 0; i < 4; i++)
            out->par.pack.pattern[i] = op->pack.pattern[i];
        break;
    case SWS_UOP_PACK:
        out->mask = 0;
        for (int i = 0; i < 4 && op->pack.pattern[i]; i++)
            out->mask |= SWS_COMP(i);
        for (int i = 0; i < 4; i++)
            out->par.pack.pattern[i] = op->pack.pattern[i];
        break;
    case SWS_UOP_LSHIFT:
    case SWS_UOP_RSHIFT:
        out->par.shift.amount = op->shift.amount;
        break;
    case SWS_UOP_CLEAR:
        out->mask = 0;
        for (int i = 0; i < 4; i++) {
            if (op->clear.mask & SWS_COMP(i)) {
                out->mask |= SWS_COMP(i);
                if (op->clear.value[i].num == 0) {
                    out->par.clear.zero |= SWS_COMP(i);
                } else {
                    uint32_t val = op->clear.value[i].num / op->clear.value[i].den;
                    if ((op->type == SWS_PIXEL_U8  && val == UINT8_MAX)  ||
                        (op->type == SWS_PIXEL_U16 && val == UINT16_MAX) ||
                        (op->type == SWS_PIXEL_U32 && val == UINT32_MAX))
                        out->par.clear.one |= SWS_COMP(i);
                }
            }
        }
        break;
    case SWS_UOP_LINEAR:
    case SWS_UOP_LINEAR_FMA:
        out->mask = 0;
        const uint32_t lin_mask = ff_sws_linear_mask(&op->lin);
        for (int i = 0; i < 4; i++) {
            if (!SWS_OP_NEEDED(op, i) || !(lin_mask & SWS_MASK_ROW(i))) {
                for (int j = 0; j < 5; j++)
                    out->par.lin.zero |= SWS_MASK(i, j);
                continue;
            }
            out->mask |= SWS_COMP(i);
            for (int j = 0; j < 5; j++) {
                const AVRational64 k = op->lin.m[i][j];
                if (j < 4 && k.num == k.den)
                    out->par.lin.one |= SWS_MASK(i, j);
                else if (k.num == 0)
                    out->par.lin.zero |= SWS_MASK(i, j);
            }
        }
        break;
    case SWS_UOP_DITHER:
        out->mask = SWS_COMP_MASK(op->dither.y_offset[0] >= 0,
                                  op->dither.y_offset[1] >= 0,
                                  op->dither.y_offset[2] >= 0,
                                  op->dither.y_offset[3] >= 0);
        LOOP(out->mask, i) {
            out->par.dither.y_offset[i] = op->dither.y_offset[i];
        }
        out->par.dither.size_log2 = op->dither.size_log2;
        break;
    }

    switch (out->uop) {
    case SWS_UOP_READ_BIT:
    case SWS_UOP_READ_NIBBLE:
    case SWS_UOP_READ_PACKED:
    case SWS_UOP_READ_PLANAR:
    case SWS_UOP_WRITE_BIT:
    case SWS_UOP_WRITE_NIBBLE:
    case SWS_UOP_WRITE_PACKED:
    case SWS_UOP_WRITE_PLANAR:
    case SWS_UOP_SWAP_BYTES:
    case SWS_UOP_CLEAR:
        /* Only the element size matters, not the type. */
        if (out->type == SWS_PIXEL_F32)
            out->type = SWS_PIXEL_U32;
        break;
    }

    return 0;
}
