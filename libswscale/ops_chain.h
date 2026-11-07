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

#include "libavutil/avassert.h"
#include "libavutil/avconfig.h"
#include "libavutil/cpu.h"
#include "libavutil/mem.h"

#include "ops_internal.h"
#include "swscale_internal.h"

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

typedef struct SwsUOpTable SwsUOpTable;

/**
 * Per-kernel execution context.
 *
 * Note: This struct is hard-coded in assembly, so do not change the layout.
 */
typedef void (*SwsFuncPtr)(void);
typedef struct SwsOpImpl {
    SwsFuncPtr func;   /* Function pointer for this operation. */
    char       priv[]; /* Private data for this operation. */
} SwsOpImpl;

#if ARCH_X86_64 || ARCH_AARCH64
static_assert(offsetof(SwsOpImpl, priv) == 8, "SwsOpImpl layout mismatch");
static_assert(alignof(SwsOpImpl) == sizeof(void *), "SwsOpImpl alignment mismatch");
#endif

/**
 * Compiled "chain" of operations, which can be dispatched efficiently.
 * Effectively just a list of function pointers, alongside a small amount of
 * private data for each operation.
 */
typedef struct SwsOpChain {
    /* Generic data buffer. This contains a packed representation of
     * continuation addresses and kernel private data. This is placed at
     * the front of the struct so SIMD kernels can access it directly. */
    DECLARE_ALIGNED_8(uint8_t, impl)[1024];
    int impl_size;

    /* Metadata for the chain */
    int cpu_flags;      /* set of all used CPU flags */
    int over_read[4];   /* chain over-reads input by this many bytes */
    int over_write[4];  /* chain over-writes output by this many bytes */

    /* Extra references owned by each kernel */
    const void *ref[16];
    int num_ref;
} SwsOpChain;

SwsOpChain *ff_sws_op_chain_alloc(void);
void ff_sws_op_chain_free_cb(void *chain);
static inline void ff_sws_op_chain_free(SwsOpChain *chain)
{
    ff_sws_op_chain_free_cb(chain);
}

/* Returns 0 on success, or a negative error code. */
int ff_sws_op_chain_append(SwsOpChain *chain, SwsFuncPtr func, const void *ref,
                           const void *data, size_t data_size);

/**
 * Attach a SwsOpChain and all metadata to a SwsCompiledOp (as out->priv).
 *
 * Note: This does not initialize *out. The caller is responsible for
 * assigning out->func.
 */
void ff_sws_op_chain_attach(const SwsOpChain *chain, SwsCompiledOp *out);

typedef struct SwsImplParams {
    const SwsUOpTable *table;
    const SwsUOp *uop;
    SwsContext *ctx;
} SwsImplParams;

typedef struct SwsImplResult {
    SwsFuncPtr func; /* overrides `SwsUOpEntry.func` if non-NULL */
    int over_read[4];  /* implementation over-reads input by this many bytes */
    int over_write[4]; /* implementation over-writes output by this many bytes */
    const void *ref; /* external data backing `priv`, ownership passes to caller */

    /* Private data for this kernel */
    size_t priv_size;
    uint8_t priv[128];
} SwsImplResult;

typedef struct SwsUOpEntry {
    /* Kernel metadata; reduced size subset of SwsUOp (sans data) */
    SwsUOpType uop;
    SwsPixelType type;
    SwsCompMask mask;
    SwsUOpParams par;

    /* Kernel implementation */
    SwsFuncPtr func;
    int (*setup)(const SwsImplParams *params, SwsImplResult *out); /* optional */
    bool (*check)(const SwsImplParams *params); /* optional, return true if supported */
} SwsUOpEntry;

/* Setup helpers for SwsUOp data */
int ff_sws_setup_scalar(const SwsImplParams *params, SwsImplResult *out);
int ff_sws_setup_vec4(const SwsImplParams *params, SwsImplResult *out);

#define SWS_OP_PRIV_PACK(out, data) ff_sws_op_priv_pack(out, &data, sizeof(data))
static inline void ff_sws_op_priv_pack(SwsImplResult *out, const void *data, size_t size)
{
    av_assert0(out->priv_size + size <= sizeof(out->priv));
    memcpy(&out->priv[out->priv_size], data, size);
    out->priv_size += size;
}

static inline void ff_sws_op_priv_pack_refptr(SwsImplResult *out, const void *data)
{
    ff_sws_op_priv_pack(out, &data, sizeof(data));
    av_assert0(!out->ref);
    out->ref = data;
}

struct SwsUOpTable {
    unsigned cpu_flags;   /* required CPU flags for this table */
    int block_size;       /* fixed block size of this table */
    const SwsUOpEntry *entries[]; /* terminated by NULL */
};

/**
 * "Compile" a single uop by looking it up in a list of fixed size uop tables,
 * in decreasing order of preference.
 *
 * Returns 0 or a negative error code.
 */
int ff_sws_uop_lookup(SwsContext *ctx, const SwsUOpTable *const tables[],
                      int num_tables, const SwsUOp *uop, const int block_size,
                      SwsOpChain *chain);

#endif
