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

#ifndef SWSCALE_AARCH64_OPS_IMPL_H
#define SWSCALE_AARCH64_OPS_IMPL_H

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

/* Similar to SwsPixelType */
typedef enum SwsAArch64PixelType {
    AARCH64_PIXEL_U8,
    AARCH64_PIXEL_U16,
    AARCH64_PIXEL_U32,
    AARCH64_PIXEL_F32,
    AARCH64_PIXEL_TYPE_NB,
} SwsAArch64PixelType;

/* Similar to SwsOpType */
typedef enum SwsAArch64OpType {
    AARCH64_SWS_OP_NONE = 0,
    AARCH64_SWS_OP_PROCESS,
    AARCH64_SWS_OP_PROCESS_RETURN,
    AARCH64_SWS_OP_READ_BIT,
    AARCH64_SWS_OP_READ_NIBBLE,
    AARCH64_SWS_OP_READ_PACKED,
    AARCH64_SWS_OP_READ_PLANAR,
    AARCH64_SWS_OP_WRITE_BIT,
    AARCH64_SWS_OP_WRITE_NIBBLE,
    AARCH64_SWS_OP_WRITE_PACKED,
    AARCH64_SWS_OP_WRITE_PLANAR,
    AARCH64_SWS_OP_SWAP_BYTES,
    AARCH64_SWS_OP_SWIZZLE,
    AARCH64_SWS_OP_UNPACK,
    AARCH64_SWS_OP_PACK,
    AARCH64_SWS_OP_LSHIFT,
    AARCH64_SWS_OP_RSHIFT,
    AARCH64_SWS_OP_CLEAR,
    AARCH64_SWS_OP_CONVERT,
    AARCH64_SWS_OP_EXPAND,
    AARCH64_SWS_OP_MIN,
    AARCH64_SWS_OP_MAX,
    AARCH64_SWS_OP_SCALE,
    AARCH64_SWS_OP_LINEAR,
    AARCH64_SWS_OP_DITHER,
    AARCH64_SWS_OP_TYPE_NB,
} SwsAArch64OpType;

/* Each nibble in the mask corresponds to one component. */
typedef uint16_t SwsAArch64OpMask;

/**
 * Affine coefficient mask for linear op. Packs a 4x5 matrix in execution
 * order, where the offset is the first element, with 2 bits per element:
 *   00: m[i][j] == 0
 *   01: m[i][j] == 1
 *   11: m[i][j] is any other coefficient
 */
typedef uint64_t SwsAArch64LinearOpMask;

typedef struct SwsAArch64LinearOp {
    SwsAArch64LinearOpMask mask;
    uint8_t fmla;
} SwsAArch64LinearOp;

typedef struct SwsAArch64DitherOp {
    uint16_t y_offset;
    uint8_t size_log2;
} SwsAArch64DitherOp;

/**
 * SwsAArch64OpImplParams describes the parameters for an SwsAArch64OpType
 * operation. It consists of simplified parameters from the SwsOp structure,
 * with the purpose of being straight-forward to implement and execute.
 */
typedef struct SwsAArch64OpImplParams {
    SwsAArch64OpType    op;
    SwsAArch64OpMask    mask;
    SwsAArch64PixelType type;
    uint8_t block_size;
    union {
        uint8_t             shift;
        SwsAArch64OpMask    swizzle;
        SwsAArch64OpMask    pack;
        SwsAArch64PixelType to_type;
        SwsAArch64LinearOp  linear;
        SwsAArch64DitherOp  dither;
    };
} SwsAArch64OpImplParams;

/* SwsAArch64OpMask-related helpers. */

#define MASK_SET(mask, idx, val) do { (mask) |= (((val) & 0xf) << ((idx) << 2)); } while (0)

#define LINEAR_MASK_SET(mask, idx, jdx, val) do {                                       \
    (mask) |= ((((SwsAArch64LinearOpMask) (val)) & 3) << (2 * ((5 * (idx) + (jdx)))));  \
} while (0)
#define LINEAR_MASK_0 0
#define LINEAR_MASK_1 1
#define LINEAR_MASK_X 3

/**
 * These values will be used by ops_asmgen to access fields inside of
 * SwsOpExec and SwsOpImpl. The sizes are checked below when compiling
 * for AArch64 to make sure there is no mismatch.
 */
#define offsetof_exec_in         0
#define offsetof_exec_out       32
#define offsetof_exec_in_bump  128
#define offsetof_exec_out_bump 160
#define offsetof_impl_cont       0
#define offsetof_impl_priv      16
#define sizeof_impl             32

#if ARCH_AARCH64 && HAVE_NEON
static_assert(offsetof_exec_in       == offsetof(SwsOpExec, in),       "SwsOpExec layout mismatch");
static_assert(offsetof_exec_out      == offsetof(SwsOpExec, out),      "SwsOpExec layout mismatch");
static_assert(offsetof_exec_in_bump  == offsetof(SwsOpExec, in_bump),  "SwsOpExec layout mismatch");
static_assert(offsetof_exec_out_bump == offsetof(SwsOpExec, out_bump), "SwsOpExec layout mismatch");
static_assert(offsetof_impl_cont     == offsetof(SwsOpImpl, cont),     "SwsOpImpl layout mismatch");
static_assert(offsetof_impl_priv     == offsetof(SwsOpImpl, priv),     "SwsOpImpl layout mismatch");
#endif

#endif /* SWSCALE_AARCH64_OPS_IMPL_H */
