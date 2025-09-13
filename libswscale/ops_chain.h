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

#ifndef SWSCALE_OPS_CHAIN_H
#define SWSCALE_OPS_CHAIN_H

#include "libavutil/cpu.h"

#include "ops_internal.h"

/**
 * Helpers for SIMD implementations based on chained kernels, using a
 * continuation passing style to link them together.
 *
 * The basic idea here is to "link" together a series of different operation
 * kernels by constructing a list of kernel addresses into an SwsOpChain. Each
 * kernel will load the address of the next kernel (the "continuation") from
 * this struct, and jump directly into it; using an internal function signature
 * that is an implementation detail of the specific backend.
 */

/**
 * Private data for each kernel.
 */
typedef union SwsOpPriv {
    DECLARE_ALIGNED_16(char, data)[16];

    /* Common types */
    void *ptr;
    uint8_t   u8[16];
    uint16_t u16[8];
    uint32_t u32[4];
    float    f32[4];
} SwsOpPriv;

static_assert(sizeof(SwsOpPriv) == 16, "SwsOpPriv size mismatch");

/* Setup helpers */
int ff_sws_setup_u(const SwsOp *op, SwsOpPriv *out);
int ff_sws_setup_u8(const SwsOp *op, SwsOpPriv *out);
int ff_sws_setup_q(const SwsOp *op, SwsOpPriv *out);
int ff_sws_setup_q4(const SwsOp *op, SwsOpPriv *out);

/**
 * Per-kernel execution context.
 *
 * Note: This struct is hard-coded in assembly, so do not change the layout.
 */
typedef void (*SwsFuncPtr)(void);
typedef struct SwsOpImpl {
    SwsFuncPtr cont; /* [offset =  0] Continuation for this operation. */
    SwsOpPriv  priv; /* [offset = 16] Private data for this operation. */
} SwsOpImpl;

static_assert(sizeof(SwsOpImpl) == 32,         "SwsOpImpl layout mismatch");
static_assert(offsetof(SwsOpImpl, priv) == 16, "SwsOpImpl layout mismatch");

/**
 * Compiled "chain" of operations, which can be dispatched efficiently.
 * Effectively just a list of function pointers, alongside a small amount of
 * private data for each operation.
 */
typedef struct SwsOpChain {
#define SWS_MAX_OPS 16
    SwsOpImpl impl[SWS_MAX_OPS + 1]; /* reserve extra space for the entrypoint */
    void (*free[SWS_MAX_OPS + 1])(void *);
    int num_impl;
    int cpu_flags; /* set of all used CPU flags */
} SwsOpChain;

SwsOpChain *ff_sws_op_chain_alloc(void);
void ff_sws_op_chain_free_cb(void *chain);
static inline void ff_sws_op_chain_free(SwsOpChain *chain)
{
    ff_sws_op_chain_free_cb(chain);
}

/* Returns 0 on success, or a negative error code. */
int ff_sws_op_chain_append(SwsOpChain *chain, SwsFuncPtr func,
                           void (*free)(void *), const SwsOpPriv *priv);

typedef struct SwsOpEntry {
    /* Kernel metadata; reduced size subset of SwsOp */
    SwsOpType op;
    SwsPixelType type;
    bool flexible; /* if true, only the type and op are matched */
    bool unused[4]; /* for kernels which operate on a subset of components */

    union { /* extra data defining the operation, unless `flexible` is true */
        SwsReadWriteOp rw;
        SwsPackOp      pack;
        SwsSwizzleOp   swizzle;
        SwsConvertOp   convert;
        uint32_t       linear_mask; /* subset of SwsLinearOp */
        int            dither_size; /* subset of SwsDitherOp */
        int            clear_value; /* clear value for integer clears */
    };

    /* Kernel implementation */
    SwsFuncPtr func;
    int (*setup)(const SwsOp *op, SwsOpPriv *out); /* optional */
    void (*free)(void *priv);
} SwsOpEntry;

typedef struct SwsOpTable {
    unsigned cpu_flags;   /* required CPU flags for this table */
    int block_size;       /* fixed block size of this table */
    const SwsOpEntry *entries[]; /* terminated by NULL */
} SwsOpTable;

/**
 * "Compile" a single op by looking it up in a list of fixed size op tables.
 * See `op_match` in `ops.c` for details on how the matching works.
 *
 * Returns 0, AVERROR(EAGAIN), or a negative error code.
 */
int ff_sws_op_compile_tables(const SwsOpTable *const tables[], int num_tables,
                             SwsOpList *ops, const int block_size,
                             SwsOpChain *chain);

#endif
