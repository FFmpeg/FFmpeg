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

/*
 * based on vlc_atomic.h from VLC
 * Copyright (C) 2010 Rémi Denis-Courmont
 */

#ifndef COMPAT_ATOMICS_GCC_STDATOMIC_H
#define COMPAT_ATOMICS_GCC_STDATOMIC_H

#include <stddef.h>
#include <stdint.h>

#define ATOMIC_FLAG_INIT 0

#define ATOMIC_VAR_INIT(value) (value)

#define atomic_init(obj, value) \
do {                            \
    *(obj) = (value);           \
} while(0)

#define kill_dependency(y) ((void)0)

#define atomic_thread_fence(order) \
    __sync_synchronize()

#define atomic_signal_fence(order) \
    ((void)0)

#define atomic_is_lock_free(obj) 0

typedef         _Bool      atomic_flag;
typedef         _Bool      atomic_bool;
typedef          char      atomic_char;
typedef   signed char      atomic_schar;
typedef unsigned char      atomic_uchar;
typedef          short     atomic_short;
typedef unsigned short     atomic_ushort;
typedef          int       atomic_int;
typedef unsigned int       atomic_uint;
typedef          long      atomic_long;
typedef unsigned long      atomic_ulong;
typedef          long long atomic_llong;
typedef unsigned long long atomic_ullong;
typedef          wchar_t   atomic_wchar_t;
typedef       int_least8_t atomic_int_least8_t;
typedef      uint_least8_t atomic_uint_least8_t;
typedef      int_least16_t atomic_int_least16_t;
typedef     uint_least16_t atomic_uint_least16_t;
typedef      int_least32_t atomic_int_least32_t;
typedef     uint_least32_t atomic_uint_least32_t;
typedef      int_least64_t atomic_int_least64_t;
typedef     uint_least64_t atomic_uint_least64_t;
typedef       int_fast8_t atomic_int_fast8_t;
typedef      uint_fast8_t atomic_uint_fast8_t;
typedef      int_fast16_t atomic_int_fast16_t;
typedef     uint_fast16_t atomic_uint_fast16_t;
typedef      int_fast32_t atomic_int_fast32_t;
typedef     uint_fast32_t atomic_uint_fast32_t;
typedef      int_fast64_t atomic_int_fast64_t;
typedef     uint_fast64_t atomic_uint_fast64_t;
typedef          intptr_t atomic_intptr_t;
typedef         uintptr_t atomic_uintptr_t;
typedef            size_t atomic_size_t;
typedef         ptrdiff_t atomic_ptrdiff_t;
typedef          intmax_t atomic_intmax_t;
typedef         uintmax_t atomic_uintmax_t;

#define atomic_store(object, desired)   \
do {                                    \
    *(object) = (desired);              \
    __sync_synchronize();               \
} while (0)

#define atomic_store_explicit(object, desired, order) \
    atomic_store(object, desired)

#define atomic_load(object) \
    (__sync_synchronize(), *(object))

#define atomic_load_explicit(object, order) \
    atomic_load(object)

#define atomic_exchange(object, desired)                            \
({                                                                  \
    __typeof__(object) _obj = (object);                             \
    __typeof__(*object) _old;                                       \
    do                                                              \
        _old = atomic_load(_obj);                                   \
    while (!__sync_bool_compare_and_swap(_obj, _old, (desired)));   \
    _old;                                                           \
})

#define atomic_exchange_explicit(object, desired, order) \
    atomic_exchange(object, desired)

#define atomic_compare_exchange_strong(object, expected, desired)   \
({                                                                  \
    __typeof__(object) _exp = (expected);                           \
    __typeof__(*object) _old = *_exp;                               \
    *_exp = __sync_val_compare_and_swap((object), _old, (desired)); \
    *_exp == _old;                                                  \
})

#define atomic_compare_exchange_strong_explicit(object, expected, desired, success, failure) \
    atomic_compare_exchange_strong(object, expected, desired)

#define atomic_compare_exchange_weak(object, expected, desired) \
    atomic_compare_exchange_strong(object, expected, desired)

#define atomic_compare_exchange_weak_explicit(object, expected, desired, success, failure) \
    atomic_compare_exchange_weak(object, expected, desired)

#define atomic_fetch_add(object, operand) \
    __sync_fetch_and_add(object, operand)

#define atomic_fetch_add_explicit(object, operand, order) \
    atomic_fetch_add(object, operand)

#define atomic_fetch_sub(object, operand) \
    __sync_fetch_and_sub(object, operand)

#define atomic_fetch_sub_explicit(object, operand, order) \
    atomic_fetch_sub(object, operand)

#define atomic_fetch_or(object, operand) \
    __sync_fetch_and_or(object, operand)

#define atomic_fetch_or_explicit(object, operand, order) \
    atomic_fetch_or(object, operand)

#define atomic_fetch_xor(object, operand) \
    __sync_fetch_and_xor(object, operand)

#define atomic_fetch_xor_explicit(object, operand, order) \
    atomic_fetch_xor(object, operand)

#define atomic_fetch_and(object, operand) \
    __sync_fetch_and_and(object, operand)

#define atomic_fetch_and_explicit(object, operand, order) \
    atomic_fetch_and(object, operand)

#define atomic_flag_test_and_set(object) \
    atomic_exchange(object, 1)

#define atomic_flag_test_and_set_explicit(object, order) \
    atomic_flag_test_and_set(object)

#define atomic_flag_clear(object) \
    atomic_store(object, 0)

#define atomic_flag_clear_explicit(object, order) \
    atomic_flag_clear(object)

#endif /* COMPAT_ATOMICS_GCC_STDATOMIC_H */
