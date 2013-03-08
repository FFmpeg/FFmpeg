/*
 * Copyright (c) 2012 Ronald S. Bultje <rsbultje@gmail.com>
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

#ifndef AVUTIL_ATOMIC_WIN32_H
#define AVUTIL_ATOMIC_WIN32_H

#include <windows.h>

#include "atomic.h"

#define avpriv_atomic_int_get atomic_int_get_win32
static inline int atomic_int_get_win32(volatile int *ptr)
{
    MemoryBarrier();
    return *ptr;
}

#define avpriv_atomic_int_set atomic_int_set_win32
static inline void atomic_int_set_win32(volatile int *ptr, int val)
{
    *ptr = val;
    MemoryBarrier();
}

#define avpriv_atomic_int_add_and_fetch atomic_int_add_and_fetch_win32
static inline int atomic_int_add_and_fetch_win32(volatile int *ptr, int inc)
{
    return inc + InterlockedExchangeAdd(ptr, inc);
}

#define avpriv_atomic_ptr_cas atomic_ptr_cas_win32
static inline void *atomic_ptr_cas_win32(void * volatile *ptr,
                                         void *oldval, void *newval)
{
    return InterlockedCompareExchangePointer(ptr, newval, oldval);
}

#endif /* AVUTIL_ATOMIC_WIN32_H */
