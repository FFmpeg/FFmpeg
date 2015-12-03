/*
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/common.h"


// clip a signed integer into the (-2^23), (2^23-1) range
static inline int dca_clip23(int a)
{
    return av_clip_intp2(a, 23);
}

static inline int32_t dca_norm(int64_t a, int bits)
{
    if (bits > 0)
        return (int32_t)((a + (INT64_C(1) << (bits - 1))) >> bits);
    else
        return (int32_t)a;
}

static inline int64_t dca_round(int64_t a, int bits)
{
    if (bits > 0)
        return (a + (INT64_C(1) << (bits - 1))) & ~((INT64_C(1) << bits) - 1);
    else
        return a;
}
