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

#endif /* SWSCALE_OPS_INTERNAL_H */
