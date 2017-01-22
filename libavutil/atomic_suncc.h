/*
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

#ifndef AVUTIL_ATOMIC_SUNCC_H
#define AVUTIL_ATOMIC_SUNCC_H

#include <atomic.h>
#include <mbarrier.h>

#include "atomic.h"

#define avpriv_atomic_int_get atomic_int_get_suncc
static inline int atomic_int_get_suncc(volatile int *ptr)
{
    __machine_rw_barrier();
    return *ptr;
}

#define avpriv_atomic_int_set atomic_int_set_suncc
static inline void atomic_int_set_suncc(volatile int *ptr, int val)
{
    *ptr = val;
    __machine_rw_barrier();
}

#define avpriv_atomic_int_add_and_fetch atomic_int_add_and_fetch_suncc
static inline int atomic_int_add_and_fetch_suncc(volatile int *ptr, int inc)
{
    return atomic_add_int_nv(ptr, inc);
}

#define avpriv_atomic_ptr_cas atomic_ptr_cas_suncc
static inline void *atomic_ptr_cas_suncc(void * volatile *ptr,
                                         void *oldval, void *newval)
{
    return atomic_cas_ptr(ptr, oldval, newval);
}

#endif /* AVUTIL_ATOMIC_SUNCC_H */
