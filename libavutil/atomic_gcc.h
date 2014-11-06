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

#ifndef AVUTIL_ATOMIC_GCC_H
#define AVUTIL_ATOMIC_GCC_H

#include <stdint.h>

#include "atomic.h"

#define avpriv_atomic_int_get atomic_int_get_gcc
static inline int atomic_int_get_gcc(volatile int *ptr)
{
#if HAVE_ATOMIC_COMPARE_EXCHANGE
    return __atomic_load_n(ptr, __ATOMIC_SEQ_CST);
#else
    __sync_synchronize();
    return *ptr;
#endif
}

#define avpriv_atomic_int_set atomic_int_set_gcc
static inline void atomic_int_set_gcc(volatile int *ptr, int val)
{
#if HAVE_ATOMIC_COMPARE_EXCHANGE
    __atomic_store_n(ptr, val, __ATOMIC_SEQ_CST);
#else
    *ptr = val;
    __sync_synchronize();
#endif
}

#define avpriv_atomic_int_add_and_fetch atomic_int_add_and_fetch_gcc
static inline int atomic_int_add_and_fetch_gcc(volatile int *ptr, int inc)
{
#if HAVE_ATOMIC_COMPARE_EXCHANGE
    return __atomic_add_fetch(ptr, inc, __ATOMIC_SEQ_CST);
#else
    return __sync_add_and_fetch(ptr, inc);
#endif
}

#define avpriv_atomic_ptr_cas atomic_ptr_cas_gcc
static inline void *atomic_ptr_cas_gcc(void * volatile *ptr,
                                       void *oldval, void *newval)
{
#if HAVE_SYNC_VAL_COMPARE_AND_SWAP
#ifdef __ARMCC_VERSION
    // armcc will throw an error if ptr is not an integer type
    volatile uintptr_t *tmp = (volatile uintptr_t*)ptr;
    return (void*)__sync_val_compare_and_swap(tmp, oldval, newval);
#else
    return __sync_val_compare_and_swap(ptr, oldval, newval);
#endif
#else
    __atomic_compare_exchange_n(ptr, &oldval, newval, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return oldval;
#endif
}

#endif /* AVUTIL_ATOMIC_GCC_H */
