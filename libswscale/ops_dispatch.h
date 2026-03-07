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

    /* Pointer bump, difference between stride and processed line size */
    ptrdiff_t in_bump[4];
    ptrdiff_t out_bump[4];

    /* Extra metadata, may or may not be useful */
    int32_t width, height;      /* Overall image dimensions */
    int32_t slice_y, slice_h;   /* Start and height of current slice */
    int32_t block_size_in;      /* Size of a block of pixels in bytes */
    int32_t block_size_out;

    /* Subsampling factors for each plane */
    uint8_t in_sub_y[4], out_sub_y[4];
    uint8_t in_sub_x[4], out_sub_x[4];
} SwsOpExec;

static_assert(sizeof(SwsOpExec) == 24 * sizeof(void *) +
                                   6  * sizeof(int32_t) +
                                   16 * sizeof(uint8_t),
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

    /* Execution parameters for all functions */
    int slice_align; /* slice height alignment */
    int cpu_flags;   /* active set of CPU flags (informative) */

    /* Execution parameters for non-opaque functions only */
    int block_size;  /* number of pixels processed per iteration */
    int over_read;   /* implementation over-reads input by this many bytes */
    int over_write;  /* implementation over-writes output by this many bytes */

    /* Arbitrary private data */
    void *priv;
    void (*free)(void *priv);
} SwsCompiledOp;

#endif /* SWSCALE_OPS_DISPATCH_H */
