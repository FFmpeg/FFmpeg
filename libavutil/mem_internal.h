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

#include "config.h"

#include <stdint.h>

#include "avassert.h"
#include "mem.h"

// Some broken preprocessors need a second expansion
// to be forced to tokenize __VA_ARGS__
#define E1(x) x

#define LOCAL_ALIGNED_A(a, t, v, s, o, ...)             \
    uint8_t la_##v[sizeof(t s o) + (a)];                \
    t (*v) o = (void *)FFALIGN((uintptr_t)la_##v, a)

#define LOCAL_ALIGNED_D(a, t, v, s, o, ...)             \
    DECLARE_ALIGNED(a, t, la_##v) s o;                  \
    t (*v) o = la_##v

#define LOCAL_ALIGNED(a, t, v, ...) LOCAL_ALIGNED_##a(t, v, __VA_ARGS__)

#if HAVE_LOCAL_ALIGNED
#   define LOCAL_ALIGNED_4(t, v, ...) E1(LOCAL_ALIGNED_D(4, t, v, __VA_ARGS__,,))
#else
#   define LOCAL_ALIGNED_4(t, v, ...) E1(LOCAL_ALIGNED_A(4, t, v, __VA_ARGS__,,))
#endif

#if HAVE_LOCAL_ALIGNED
#   define LOCAL_ALIGNED_8(t, v, ...) E1(LOCAL_ALIGNED_D(8, t, v, __VA_ARGS__,,))
#else
#   define LOCAL_ALIGNED_8(t, v, ...) E1(LOCAL_ALIGNED_A(8, t, v, __VA_ARGS__,,))
#endif

#if HAVE_LOCAL_ALIGNED
#   define LOCAL_ALIGNED_16(t, v, ...) E1(LOCAL_ALIGNED_D(16, t, v, __VA_ARGS__,,))
#else
#   define LOCAL_ALIGNED_16(t, v, ...) E1(LOCAL_ALIGNED_A(16, t, v, __VA_ARGS__,,))
#endif

#if HAVE_LOCAL_ALIGNED
#   define LOCAL_ALIGNED_32(t, v, ...) E1(LOCAL_ALIGNED_D(32, t, v, __VA_ARGS__,,))
#else
#   define LOCAL_ALIGNED_32(t, v, ...) E1(LOCAL_ALIGNED_A(32, t, v, __VA_ARGS__,,))
#endif

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
