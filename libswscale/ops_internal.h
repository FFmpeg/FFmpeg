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

#ifndef SWSCALE_OPS_INTERNAL_H
#define SWSCALE_OPS_INTERNAL_H

#include "libavutil/mem_internal.h"

#include "ops.h"

#define Q(N) ((AVRational) { N, 1 })

static inline AVRational ff_sws_pixel_expand(SwsPixelType from, SwsPixelType to)
{
    const int src = ff_sws_pixel_type_size(from);
    const int dst = ff_sws_pixel_type_size(to);
    int scale = 0;
    for (int i = 0; i < dst / src; i++)
        scale = scale << src * 8 | 1;
    return Q(scale);
}

static inline void ff_sws_pack_op_decode(const SwsOp *op, uint64_t mask[4], int shift[4])
{
    const int size = ff_sws_pixel_type_size(op->type) * 8;
    for (int i = 0; i < 4; i++) {
        const int bits = op->pack.pattern[i];
        mask[i] = (UINT64_C(1) << bits) - 1;
        shift[i] = (i ? shift[i - 1] : size) - bits;
    }
}

/**
 * Global execution context for all compiled functions.
 *
 * Note: This struct is hard-coded in assembly, so do not change the layout
 * without updating the corresponding assembly definitions.
 */
typedef struct SwsOpExec {
    /* The data pointers point to the first pixel to process */
    const uint8_t *in[4];
    uint8_t *out[4];

    /* Separation between lines in bytes */
    ptrdiff_t in_stride[4];
    ptrdiff_t out_stride[4];

    /* Pointer bump, difference between stride and processed line size */
    ptrdiff_t in_bump[4];
    ptrdiff_t out_bump[4];

    /* Extra metadata, may or may not be useful */
    int32_t width, height;      /* Overall image dimensions */
    int32_t slice_y, slice_h;   /* Start and height of current slice */
    int32_t block_size_in;      /* Size of a block of pixels in bytes */
    int32_t block_size_out;
} SwsOpExec;

static_assert(sizeof(SwsOpExec) == 24 * sizeof(void *) + 6 * sizeof(int32_t),
              "SwsOpExec layout mismatch");

/**
 * Process a given range of pixel blocks.
 *
 * Note: `bx_start` and `bx_end` are in units of `SwsCompiledOp.block_size`.
 */
typedef void (*SwsOpFunc)(const SwsOpExec *exec, const void *priv,
                          int bx_start, int y_start, int bx_end, int y_end);

#define SWS_DECL_FUNC(NAME) \
    void NAME(const SwsOpExec *, const void *, int, int, int, int)

typedef struct SwsCompiledOp {
    SwsOpFunc func;

    int block_size; /* number of pixels processed per iteration */
    int over_read;  /* implementation over-reads input by this many bytes */
    int over_write; /* implementation over-writes output by this many bytes */
    int cpu_flags;  /* active set of CPU flags (informative) */

    /* Arbitrary private data */
    void *priv;
    void (*free)(void *priv);
} SwsCompiledOp;

typedef struct SwsOpBackend {
    const char *name; /* Descriptive name for this backend */

    /**
     * Compile an operation list to an implementation chain. May modify `ops`
     * freely; the original list will be freed automatically by the caller.
     *
     * Returns 0 or a negative error code.
     */
    int (*compile)(SwsContext *ctx, SwsOpList *ops, SwsCompiledOp *out);
} SwsOpBackend;

/* List of all backends, terminated by NULL */
extern const SwsOpBackend *const ff_sws_op_backends[];

/**
 * Attempt to compile a list of operations using a specific backend.
 *
 * Returns 0 on success, or a negative error code on failure.
 */
int ff_sws_ops_compile_backend(SwsContext *ctx, const SwsOpBackend *backend,
                               const SwsOpList *ops, SwsCompiledOp *out);

/**
 * Compile a list of operations using the best available backend.
 *
 * Returns 0 on success, or a negative error code on failure.
 */
int ff_sws_ops_compile(SwsContext *ctx, const SwsOpList *ops, SwsCompiledOp *out);

/**
 * "Solve" an op list into a fixed shuffle mask, with an optional ability to
 * also directly clear the output value (for e.g. rgb24 -> rgb0). This can
 * accept any operation chain that only consists of the following operations:
 *
 * - SWS_OP_READ (non-planar, non-fractional)
 * - SWS_OP_SWIZZLE
 * - SWS_OP_SWAP_BYTES
 * - SWS_OP_CLEAR to zero (when clear_val is specified)
 * - SWS_OP_CONVERT (integer expand)
 * - SWS_OP_WRITE (non-planar, non-fractional)
 *
 * Basically, any operation that purely consists of moving around and reordering
 * bytes within a single plane, can be turned into a shuffle mask.
 *
 * @param ops         The operation list to decompose.
 * @param shuffle     The output shuffle mask.
 * @param size        The size (in bytes) of the output shuffle mask.
 * @param clear_val   If nonzero, this index will be used to clear the output.
 * @param read_bytes  Returns the number of bytes read per shuffle iteration.
 * @param write_bytes Returns the number of bytes written per shuffle iteration.
 *
 * @return  The number of pixels processed per iteration, or a negative error
            code; in particular AVERROR(ENOTSUP) for unsupported operations.
 */
int ff_sws_solve_shuffle(const SwsOpList *ops, uint8_t shuffle[], int size,
                         uint8_t clear_val, int *read_bytes, int *write_bytes);

#endif /* SWSCALE_OPS_INTERNAL_H */
