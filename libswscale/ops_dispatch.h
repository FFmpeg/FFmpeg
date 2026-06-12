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

#ifndef SWSCALE_OPS_DISPATCH_H
#define SWSCALE_OPS_DISPATCH_H

#include <assert.h>

#include "libavutil/frame.h"
#include "graph.h"

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

    /**
     * Pointer bump, difference between stride and processed line size.
     *
     * Assumes that each read kernel increments pointers by the processed
     * block size, except when using horizontal filtering, in which case
     * this is always equal to the input stride.
     */
    ptrdiff_t in_bump[4];
    ptrdiff_t out_bump[4];

    /* Extra metadata, may or may not be useful */
    int32_t width, height;      /* Overall output image dimensions */
    int32_t slice_y, slice_h;   /* Start and height of current slice */
    int32_t block_size_in[4];   /* Size of a block of pixels in bytes */
    int32_t block_size_out[4];

    /* Subsampling factors for each plane */
    uint8_t in_sub_y[4], out_sub_y[4];
    uint8_t in_sub_x[4], out_sub_x[4];

    /**
     * Line bump; determines how many additional lines to advance (after
     * incrementing normally to the next line), for each filtered output line.
     *
     * Indexed by the line's true y coordinate. If NULL, then the bumps are
     * effectively all zero. Note that these bumps still need to be
     * multiplied by the corresponding line stride.
     */
    int32_t *in_bump_y;

    /**
     * Pixel offset map; for horizontal scaling, in bytes. Indexed by the x
     * coordinate of the output pixel. This is always aligned up to a multiple
     * of the block size, so implementations may safely over-read up to the
     * next block boundary.
     */
    int32_t *in_offset_x;
} SwsOpExec;

static_assert(sizeof(SwsOpExec) == 24 * sizeof(void *) +
                                   12 * sizeof(int32_t) +
                                   16 * sizeof(uint8_t) +
                                   2  * sizeof(void *),
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
    /* Function to execute */
    union {
        SwsOpFunc func;
        SwsPassFunc func_opaque;
    };

    /**
     * If `opaque` is true, then `func_opaque`, `priv` and `free` are directly
     * forwarded as `SwsPass.run`, `SwsPass.priv` and `SwsPass.free`
     * respectively.
     */
    bool opaque;

    /* Set by ff_sws_ops_compile(), informative */
    const struct SwsOpBackend *backend;

    /* Execution parameters for all functions */
    int slice_align; /* slice height alignment */
    int cpu_flags;   /* active set of CPU flags (informative) */

    /* Execution parameters for non-opaque functions only */
    int block_size;     /* number of pixels processed per iteration */
    int over_read[4];   /* implementation over-reads input by this many bytes */
    int over_write[4];  /* implementation over-writes output by this many bytes */

    /* Arbitrary private data */
    void *priv;
    void (*free)(void *priv);
} SwsCompiledOp;

void ff_sws_compiled_op_unref(SwsCompiledOp *comp);

typedef struct SwsOpBackend {
    const char *name; /* Descriptive name for this backend */
    SwsBackend flags; /* Set of SWS_BACKEND_* */

    /**
     * Compile an operation list to an implementation chain. May modify `ops`
     * freely; the original list will be freed automatically by the caller.
     *
     * Returns 0 or a negative error code.
     */
    int (*compile)(SwsContext *ctx, const SwsOpList *ops, SwsCompiledOp *out);

    /**
     * If NONE, backend only supports software frames.
     * Otherwise, frame hardware format must match hw_format for the backend
     * to be used.
     */
    enum AVPixelFormat hw_format;
} SwsOpBackend;

/* List of all backends, terminated by NULL */
extern const SwsOpBackend *const ff_sws_op_backends[];

/**
 * Attempt to compile a list of operations using a specific backend, or
 * the best available backend if `backend` is NULL.
 *
 * Returns 0 on success, or a negative error code on failure.
 */
int ff_sws_ops_compile(SwsContext *ctx, const SwsOpBackend *backend,
                       const SwsOpList *ops, SwsCompiledOp *out);

enum SwsOpCompileFlags {
    /* Automatically optimize the operations when compiling */
    SWS_OP_FLAG_OPTIMIZE = 1 << 0,

    /* Discard the compiled op lists instead of generating passes */
    SWS_OP_FLAG_DRY_RUN = 1 << 1,

    /* Split off copied/cleared planes into separate subpasses */
    SWS_OP_FLAG_SPLIT_MEMCPY = 1 << 2,
};

/**
 * Resolves an operation list to a graph pass. The last op must be a write.
 *
 * @param backend Force the use of a specific backend (Optional)
 * @param ops Operations to compile. Ownership passes to this function, and
 *            will be set to NULL, even on failure.
 * @param flags Set of SwsOpCompileFlags
 * @param input The input for the compiled passes. (Optional)
 * @param output The resulting final output pass will be stored here.
 *               Optional if using SWS_OP_FLAG_DRY_RUN.
 */
int ff_sws_compile_pass(SwsGraph *graph, const SwsOpBackend *backend,
                        SwsOpList **ops, int flags, SwsPass *input,
                        SwsPass **output);

#endif /* SWSCALE_OPS_DISPATCH_H */
