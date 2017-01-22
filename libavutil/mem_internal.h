/*
 * Copyright (c) 2002 Fabrice Bellard
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

#ifndef AVUTIL_MEM_INTERNAL_H
#define AVUTIL_MEM_INTERNAL_H

#include "avassert.h"
#include "mem.h"

static inline int ff_fast_malloc(void *ptr, unsigned int *size, size_t min_size, int zero_realloc)
{
    void *val;

    memcpy(&val, ptr, sizeof(val));
    if (min_size <= *size) {
        av_assert0(val || !min_size);
        return 0;
    }
    min_size = FFMAX(min_size + min_size / 16 + 32, min_size);
    av_freep(ptr);
    val = zero_realloc ? av_mallocz(min_size) : av_malloc(min_size);
    memcpy(ptr, &val, sizeof(val));
    if (!val)
        min_size = 0;
    *size = min_size;
    return 1;
}
#endif /* AVUTIL_MEM_INTERNAL_H */
