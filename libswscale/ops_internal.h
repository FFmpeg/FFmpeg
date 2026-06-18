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
#include "ops_dispatch.h"

#define Q(N) ((AVRational) { N, 1 })

static inline AVRational ff_sws_pixel_expand(SwsPixelType from, SwsPixelType to)
{
    const int src = ff_sws_pixel_type_size(from);
    const int dst = ff_sws_pixel_type_size(to);
    if (src > dst)
        return Q(0);
    int scale = 1;
    for (int i = 1; i < dst / src; i++)
        scale = (scale << (src * 8)) | 1;
    return Q(scale);
}

static inline void ff_sws_pack_op_decode(const SwsOp *op, uint64_t mask[4], int shift[4])
{
    int size = 0;
    for (int i = 0; i < 4; i++)
        size += op->pack.pattern[i];
    for (int i = 0; i < 4; i++) {
        const int bits = op->pack.pattern[i];
        mask[i] = (UINT64_C(1) << bits) - 1;
        shift[i] = (i ? shift[i - 1] : size) - bits;
    }
}

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

/**
 * Split an op list into two at the given index. The split will be mediated
 * by a set of planar read/write operations, plus a swizzle (if necessary)
 * to re-order only used components. If a split is performed, both output
 * lists will be optimized before returning.
 *
 * @param ops1 The first part of the split op list. Will be modified in-place.
 * @param ops2 The second part of the split op list will be returned here, or
 *             NULL if no split was necessary.
 * @param index The index of the operation to split before. The operation
 *              itself will be absent from `ops1` and instead moved to the
 *              start of `ops2`.
 *
 * Returnse 0 or a negative error code.
 */
int ff_sws_op_list_split_at(SwsOpList *ops1, SwsOpList **ops2, int index);

/**
 * Reduce an op list into a reduced subset that operates only on a given
 * subset of planes. No effect if the output is not planar, or if the plane
 * mask is empty or equal to all planes.
 *
 * @param ops1 Updated in-place to contain only the selected planes.
 * @param ops2 The removed remainder is returned here, or NULL if no-op.
 * @param planes A mask of the plane indices to keep.
 *
 * Returns 0 or a negative error code.
 */
int ff_sws_op_list_split_planes(SwsOpList *ops1, SwsOpList **ops2, SwsCompMask planes);

#endif /* SWSCALE_OPS_INTERNAL_H */
