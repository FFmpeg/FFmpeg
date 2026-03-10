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

static uint8_t sws_pixel_to_aarch64(SwsPixelType type)
{
    switch (type) {
    case SWS_PIXEL_U8:  return AARCH64_PIXEL_U8;
    case SWS_PIXEL_U16: return AARCH64_PIXEL_U16;
    case SWS_PIXEL_U32: return AARCH64_PIXEL_U32;
    case SWS_PIXEL_F32: return AARCH64_PIXEL_F32;
    }
    return 0;
}

/**
 * The column index order for SwsLinearOp.mask follows the affine transform
 * order, where the offset is the last element. SwsAArch64LinearOpMask, on
 * the other hand, follows execution order, where the offset is the first
 * element.
 */
static int linear_index_from_sws_op(int idx)
{
    const int reorder_col[5] = { 1, 2, 3, 4, 0 };
    return reorder_col[idx];
}

/**
 * Convert SwsOp to a SwsAArch64OpImplParams. Read the comments regarding
 * SwsAArch64OpImplParams in ops_impl.h for more information.
 */
static int convert_to_aarch64_impl(SwsContext *ctx, const SwsOpList *ops, int n,
                                   int block_size, SwsAArch64OpImplParams *out)
{
    const SwsOp *op = &ops->ops[n];
    const SwsOp *next = n + 1 < ops->num_ops ? &ops->ops[n + 1] : op;

    out->block_size = block_size;

    /**
     * Most SwsOp work on fields described by next->comps.unused.
     * The few that don't will override this field later.
     */
    out->mask = 0;
    for (int i = 0; i < 4; i++) {
        if (!next->comps.unused[i])
            MASK_SET(out->mask, i, 1);
    }

    out->type = sws_pixel_to_aarch64(op->type);

    /* Map SwsOpType to SwsAArch64OpType */
    switch (op->op) {
    case SWS_OP_READ:
        if (op->rw.filter)
            return AVERROR(ENOTSUP);
        /**
         * The different types of read operations have been split into
         * their own SwsAArch64OpType to simplify the implementation.
         */
        if (op->rw.frac == 1)
            out->op = AARCH64_SWS_OP_READ_NIBBLE;
        else if (op->rw.frac == 3)
            out->op = AARCH64_SWS_OP_READ_BIT;
        else if (op->rw.packed && op->rw.elems != 1)
            out->op = AARCH64_SWS_OP_READ_PACKED;
        else
            out->op = AARCH64_SWS_OP_READ_PLANAR;
        break;
    case SWS_OP_WRITE:
        if (op->rw.filter)
            return AVERROR(ENOTSUP);
        /**
         * The different types of write operations have been split into
         * their own SwsAArch64OpType to simplify the implementation.
         */
        if (op->rw.frac == 1)
            out->op = AARCH64_SWS_OP_WRITE_NIBBLE;
        else if (op->rw.frac == 3)
            out->op = AARCH64_SWS_OP_WRITE_BIT;
        else if (op->rw.packed && op->rw.elems != 1)
            out->op = AARCH64_SWS_OP_WRITE_PACKED;
        else
            out->op = AARCH64_SWS_OP_WRITE_PLANAR;
        break;
    case SWS_OP_SWAP_BYTES: out->op = AARCH64_SWS_OP_SWAP_BYTES; break;
    case SWS_OP_SWIZZLE:    out->op = AARCH64_SWS_OP_SWIZZLE;    break;
    case SWS_OP_UNPACK:     out->op = AARCH64_SWS_OP_UNPACK;     break;
    case SWS_OP_PACK:       out->op = AARCH64_SWS_OP_PACK;       break;
    case SWS_OP_LSHIFT:     out->op = AARCH64_SWS_OP_LSHIFT;     break;
    case SWS_OP_RSHIFT:     out->op = AARCH64_SWS_OP_RSHIFT;     break;
    case SWS_OP_CLEAR:      out->op = AARCH64_SWS_OP_CLEAR;      break;
    case SWS_OP_CONVERT:
        out->op = op->convert.expand ? AARCH64_SWS_OP_EXPAND : AARCH64_SWS_OP_CONVERT;
        break;
    case SWS_OP_MIN:        out->op = AARCH64_SWS_OP_MIN;        break;
    case SWS_OP_MAX:        out->op = AARCH64_SWS_OP_MAX;        break;
    case SWS_OP_SCALE:      out->op = AARCH64_SWS_OP_SCALE;      break;
    case SWS_OP_LINEAR:     out->op = AARCH64_SWS_OP_LINEAR;     break;
    case SWS_OP_DITHER:     out->op = AARCH64_SWS_OP_DITHER;     break;
    }

    switch (out->op) {
    case AARCH64_SWS_OP_READ_BIT:
    case AARCH64_SWS_OP_READ_NIBBLE:
    case AARCH64_SWS_OP_READ_PACKED:
    case AARCH64_SWS_OP_READ_PLANAR:
    case AARCH64_SWS_OP_WRITE_BIT:
    case AARCH64_SWS_OP_WRITE_NIBBLE:
    case AARCH64_SWS_OP_WRITE_PACKED:
    case AARCH64_SWS_OP_WRITE_PLANAR:
        switch (op->rw.elems) {
        case 1: out->mask = 0x0001; break;
        case 2: out->mask = 0x0011; break;
        case 3: out->mask = 0x0111; break;
        case 4: out->mask = 0x1111; break;
        };
        break;
    case AARCH64_SWS_OP_SWAP_BYTES:
        /* Only the element size matters, not the type. */
        if (out->type == AARCH64_PIXEL_F32)
            out->type = AARCH64_PIXEL_U32;
        break;
    case AARCH64_SWS_OP_SWIZZLE:
        out->mask = 0;
        MASK_SET(out->mask, 0, op->swizzle.in[0] != 0);
        MASK_SET(out->mask, 1, op->swizzle.in[1] != 1);
        MASK_SET(out->mask, 2, op->swizzle.in[2] != 2);
        MASK_SET(out->mask, 3, op->swizzle.in[3] != 3);
        MASK_SET(out->swizzle, 0, op->swizzle.in[0]);
        MASK_SET(out->swizzle, 1, op->swizzle.in[1]);
        MASK_SET(out->swizzle, 2, op->swizzle.in[2]);
        MASK_SET(out->swizzle, 3, op->swizzle.in[3]);
        /* The element size and type don't matter. */
        out->block_size = block_size * ff_sws_pixel_type_size(op->type);
        out->type = AARCH64_PIXEL_U8;
        break;
    case AARCH64_SWS_OP_UNPACK:
        MASK_SET(out->pack, 0, op->pack.pattern[0]);
        MASK_SET(out->pack, 1, op->pack.pattern[1]);
        MASK_SET(out->pack, 2, op->pack.pattern[2]);
        MASK_SET(out->pack, 3, op->pack.pattern[3]);
        break;
    case AARCH64_SWS_OP_PACK:
        out->mask = 0;
        MASK_SET(out->mask, 0, !op->comps.unused[0]);
        MASK_SET(out->mask, 1, !op->comps.unused[1]);
        MASK_SET(out->mask, 2, !op->comps.unused[2]);
        MASK_SET(out->mask, 3, !op->comps.unused[3]);
        MASK_SET(out->pack, 0, op->pack.pattern[0]);
        MASK_SET(out->pack, 1, op->pack.pattern[1]);
        MASK_SET(out->pack, 2, op->pack.pattern[2]);
        MASK_SET(out->pack, 3, op->pack.pattern[3]);
        break;
    case AARCH64_SWS_OP_LSHIFT:
    case AARCH64_SWS_OP_RSHIFT:
        out->shift = op->c.u;
        break;
    case AARCH64_SWS_OP_CLEAR:
        out->mask = 0;
        MASK_SET(out->mask, 0, !!op->c.q4[0].den);
        MASK_SET(out->mask, 1, !!op->c.q4[1].den);
        MASK_SET(out->mask, 2, !!op->c.q4[2].den);
        MASK_SET(out->mask, 3, !!op->c.q4[3].den);
        break;
    case AARCH64_SWS_OP_EXPAND:
    case AARCH64_SWS_OP_CONVERT:
        out->to_type = sws_pixel_to_aarch64(op->convert.to);
        break;
    case AARCH64_SWS_OP_LINEAR:
        /**
         * The out->linear.mask field packs the 4x5 matrix from SwsLinearOp as
         * 2 bits per element:
         *   00: m[i][j] == 0
         *   01: m[i][j] == 1
         *   11: m[i][j] is any other coefficient
         */
        out->mask = 0;
        for (int i = 0; i < 4; i++) {
            /* Skip unused or identity rows */
            if (op->comps.unused[i] || !(op->lin.mask & SWS_MASK_ROW(i)))
                continue;
            MASK_SET(out->mask, i, 1);
            for (int j = 0; j < 5; j++) {
                int jj = linear_index_from_sws_op(j);
                if (!av_cmp_q(op->lin.m[i][j], av_make_q(1, 1)))
                    LINEAR_MASK_SET(out->linear.mask, i, jj, LINEAR_MASK_1);
                else if (av_cmp_q(op->lin.m[i][j], av_make_q(0, 1)))
                    LINEAR_MASK_SET(out->linear.mask, i, jj, LINEAR_MASK_X);
            }
        }
        out->linear.fmla = !(ctx->flags & SWS_BITEXACT);
        break;
    case AARCH64_SWS_OP_DITHER:
        out->mask = 0;
        MASK_SET(out->mask, 0, op->dither.y_offset[0] >= 0);
        MASK_SET(out->mask, 1, op->dither.y_offset[1] >= 0);
        MASK_SET(out->mask, 2, op->dither.y_offset[2] >= 0);
        MASK_SET(out->mask, 3, op->dither.y_offset[3] >= 0);
        MASK_SET(out->dither.y_offset, 0, op->dither.y_offset[0]);
        MASK_SET(out->dither.y_offset, 1, op->dither.y_offset[1]);
        MASK_SET(out->dither.y_offset, 2, op->dither.y_offset[2]);
        MASK_SET(out->dither.y_offset, 3, op->dither.y_offset[3]);
        out->dither.size_log2 = op->dither.size_log2;
        break;
    }

    return 0;
}
