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

#ifndef COMPAT_ATOMICS_STDATOMIC_H
#define COMPAT_ATOMICS_STDATOMIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "libavutil/attributes.h"

typedef enum {
    memory_order_relaxed,
    memory_order_consume,
    memory_order_acquire,
    memory_order_release,
    memory_order_acq_rel,
    memory_order_seq_cst
} memory_order;

/*
 * The implementation header provides the primitives:
 * - FF_ATOMIC_LOCK_FREE, the value of the ATOMIC_*_LOCK_FREE macros.
 * - FF_ATOMIC_ALIGN64, the declaration specifier that aligns the 8-byte
 *   types to 8 bytes, empty where they are naturally aligned.
 * - ff_atomic_thread_fence(order) and ff_atomic_signal_fence(order).
 * - Helpers for the widths W = bool, 8, 16, 32 and 64, selected by the size
 *   of the object, taking it as a void pointer and the values as the
 *   unsigned integer type of the width, which every value of an object of
 *   the width converts to, bool for the bool set:
 *     type ff_atomic_load_W(object, memory_order order)
 *     void ff_atomic_store_W(object, type desired, memory_order order)
 *     type ff_atomic_exchange_W(object, type desired, memory_order order)
 *     bool ff_atomic_cas_W(object, void *expected, type desired, memory_order order)
 *     type ff_atomic_fetch_{add,sub,or,xor,and}_W(object, type operand, memory_order order)
 * The _Generic dispatch below selects one by the object type, the argument
 * and result conversions happen at the call.
 */
#include "stdatomic_impl.h"

typedef bool               atomic_bool;
typedef char               atomic_char;
typedef signed char        atomic_schar;
typedef unsigned char      atomic_uchar;
typedef short              atomic_short;
typedef unsigned short     atomic_ushort;
typedef int                atomic_int;
typedef unsigned int       atomic_uint;
typedef long               atomic_long;
typedef unsigned long      atomic_ulong;
typedef FF_ATOMIC_ALIGN64 long long          atomic_llong;
typedef FF_ATOMIC_ALIGN64 unsigned long long atomic_ullong;
typedef unsigned char      atomic_char8_t;
typedef uint_least16_t     atomic_char16_t;
typedef uint_least32_t     atomic_char32_t;
typedef wchar_t            atomic_wchar_t;
typedef int_least8_t       atomic_int_least8_t;
typedef uint_least8_t      atomic_uint_least8_t;
typedef int_least16_t      atomic_int_least16_t;
typedef uint_least16_t     atomic_uint_least16_t;
typedef int_least32_t      atomic_int_least32_t;
typedef uint_least32_t     atomic_uint_least32_t;
typedef FF_ATOMIC_ALIGN64 int_least64_t      atomic_int_least64_t;
typedef FF_ATOMIC_ALIGN64 uint_least64_t     atomic_uint_least64_t;
typedef int_fast8_t        atomic_int_fast8_t;
typedef uint_fast8_t       atomic_uint_fast8_t;
typedef int_fast16_t       atomic_int_fast16_t;
typedef uint_fast16_t      atomic_uint_fast16_t;
typedef int_fast32_t       atomic_int_fast32_t;
typedef uint_fast32_t      atomic_uint_fast32_t;
typedef FF_ATOMIC_ALIGN64 int_fast64_t       atomic_int_fast64_t;
typedef FF_ATOMIC_ALIGN64 uint_fast64_t      atomic_uint_fast64_t;
typedef intptr_t           atomic_intptr_t;
typedef uintptr_t          atomic_uintptr_t;
typedef size_t             atomic_size_t;
typedef ptrdiff_t          atomic_ptrdiff_t;
typedef FF_ATOMIC_ALIGN64 intmax_t           atomic_intmax_t;
typedef FF_ATOMIC_ALIGN64 uintmax_t          atomic_uintmax_t;

/*
 * Emulation of the _Atomic qualifier form for integer and pointer types of at
 * most 8 bytes. This has very limited support, and will fail to compile when
 * used with other types. Notably:
 * - The specifier form "_Atomic(T)" is not supported, a macro cannot
 *   provide both forms.
 * - Loads and exchanges of pointer objects return void *, not T *. Function
 *   pointer objects rely on the common extension converting them to and
 *   from void *.
 * - Enumerated types are not supported, MSVC does not match them against
 *   their underlying type in _Generic.
 * - atomic_fetch_add() and atomic_fetch_sub() stay restricted to integer
 *   types, on pointers they would have to scale by the pointee size.
 * - Plain accesses to atomic objects, _Atomic qualified or the atomic_*
 *   typedefs, are ordinary accesses, unlike in C11 where they are
 *   implicitly atomic. Every access has to go through the atomic_*
 *   functions.
 * - The 8-byte typedefs carry the alignment their accesses need to be
 *   atomic where the compiler does not provide it, on 32-bit x86, the
 *   qualifier form cannot.
 */
#define _Atomic

#define atomic_init(obj, value) ((void)(*(obj) = (value)))

#define kill_dependency(y) (y)

#define ATOMIC_BOOL_LOCK_FREE     FF_ATOMIC_LOCK_FREE
#define ATOMIC_CHAR_LOCK_FREE     FF_ATOMIC_LOCK_FREE
#define ATOMIC_CHAR8_T_LOCK_FREE  FF_ATOMIC_LOCK_FREE
#define ATOMIC_CHAR16_T_LOCK_FREE FF_ATOMIC_LOCK_FREE
#define ATOMIC_CHAR32_T_LOCK_FREE FF_ATOMIC_LOCK_FREE
#define ATOMIC_WCHAR_T_LOCK_FREE  FF_ATOMIC_LOCK_FREE
#define ATOMIC_SHORT_LOCK_FREE    FF_ATOMIC_LOCK_FREE
#define ATOMIC_INT_LOCK_FREE      FF_ATOMIC_LOCK_FREE
#define ATOMIC_LONG_LOCK_FREE     FF_ATOMIC_LOCK_FREE
#define ATOMIC_LLONG_LOCK_FREE    FF_ATOMIC_LOCK_FREE
#define ATOMIC_POINTER_LOCK_FREE  FF_ATOMIC_LOCK_FREE

#define atomic_is_lock_free(obj) ((void)(obj), (bool)(FF_ATOMIC_LOCK_FREE == 2))

#define atomic_thread_fence(order) ff_atomic_thread_fence(order)
#define atomic_signal_fence(order) ff_atomic_signal_fence(order)

#define FF_ATOMIC_SIZE(op, size)               \
    _Generic((char (*)[size])0,                \
             char (*)[1]: ff_atomic_##op##_8,  \
             char (*)[2]: ff_atomic_##op##_16, \
             char (*)[4]: ff_atomic_##op##_32, \
             char (*)[8]: ff_atomic_##op##_64)

static av_always_inline uintptr_t
ff_atomic_load_ptr(const volatile void *object, memory_order order)
{
    return FF_ATOMIC_SIZE(load, sizeof(void *))(object, order);
}

static av_always_inline void ff_atomic_store_ptr(volatile void *object,
                                                 const volatile void *desired,
                                                 memory_order order)
{
/* The alias analysis of MSVC for ARM before 19.44 does not track a pointer
 * converted to an integer for the store intrinsics, so the stores initializing
 * the pointed-to object are eliminated as dead. The interlocked exchange is not
 * affected, and with its result unused it compiles to the same store as a plain
 * store would without the bug. */
#if defined(_MSC_VER) && !defined(__clang__) && _MSC_VER < 1944 && \
    (defined(_M_ARM) || defined(_M_ARM64) || defined(_M_ARM64EC))
    FF_ATOMIC_SIZE(exchange, sizeof(void *))(object, (uintptr_t)desired, order);
#else
    FF_ATOMIC_SIZE(store, sizeof(void *))(object, (uintptr_t)desired, order);
#endif
}

static av_always_inline uintptr_t
ff_atomic_exchange_ptr(volatile void *object, const volatile void *desired,
                       memory_order order)
{
    return FF_ATOMIC_SIZE(exchange, sizeof(void *))(object, (uintptr_t)desired,
                                                    order);
}

static av_always_inline bool ff_atomic_cas_ptr(volatile void *object,
                                               void *expected,
                                               const volatile void *desired,
                                               memory_order order)
{
    return FF_ATOMIC_SIZE(cas, sizeof(void *))(object, expected,
                                               (uintptr_t)desired, order);
}

void ff_atomic_unsupported(void);

/* the pointer helpers, for the objects of the pointer size only */
#define FF_ATOMIC_PTR(op, object)                            \
    _Generic((char (*)[sizeof(*(object))])0,                 \
             char (*)[sizeof(void *)]: ff_atomic_##op##_ptr, \
             default: ff_atomic_unsupported)

/* MSVC before 19.44 does not treat char as its own type in _Generic. It matches
 * either signed char or unsigned char, depending on /J, and rejects a separate
 * char association as a duplicate.
 * <https://developercommunity.visualstudio.com/t/_Generic-char-signed-char-unsigned-cha/1228885> */
#if defined(_MSC_VER) && !defined(__clang__) && _MSC_VER < 1944
#define FF_ATOMIC_CHAR(x)
#else
#define FF_ATOMIC_CHAR(x) char: x,
#endif

#define FF_ATOMIC_INT(op, object)                                       \
             FF_ATOMIC_CHAR(FF_ATOMIC_SIZE(op, sizeof(*(object))))      \
             signed char:        FF_ATOMIC_SIZE(op, sizeof(*(object))), \
             unsigned char:      FF_ATOMIC_SIZE(op, sizeof(*(object))), \
             short:              FF_ATOMIC_SIZE(op, sizeof(*(object))), \
             unsigned short:     FF_ATOMIC_SIZE(op, sizeof(*(object))), \
             int:                FF_ATOMIC_SIZE(op, sizeof(*(object))), \
             unsigned int:       FF_ATOMIC_SIZE(op, sizeof(*(object))), \
             long:               FF_ATOMIC_SIZE(op, sizeof(*(object))), \
             unsigned long:      FF_ATOMIC_SIZE(op, sizeof(*(object))), \
             long long:          FF_ATOMIC_SIZE(op, sizeof(*(object))), \
             unsigned long long: FF_ATOMIC_SIZE(op, sizeof(*(object)))

/* bool objects have their own set for the normalization */
#define FF_ATOMIC_FN(op, object)                          \
    _Generic(*(object),                                   \
             bool:    ff_atomic_##op##_bool,              \
             FF_ATOMIC_INT(op, object),                   \
             default: FF_ATOMIC_PTR(op, object))

/* the fetch operations are for the integer objects only, bool is excluded
 * and on pointers they would have to scale by the pointee size */
#define FF_ATOMIC_FETCH_FN(op, object)                    \
    _Generic(*(object), FF_ATOMIC_INT(op, object))

/* the width or uintptr_t result back to the non-atomic type C */
#define ff_atomic_c(object, x)                            \
    _Generic(*(object),                                   \
             bool:               (bool)(x),               \
             FF_ATOMIC_CHAR((char)(x))                    \
             signed char:        (signed char)(x),        \
             unsigned char:      (unsigned char)(x),      \
             short:              (short)(x),              \
             unsigned short:     (unsigned short)(x),     \
             int:                (int)(x),                \
             unsigned int:       (unsigned int)(x),       \
             long:               (long)(x),               \
             unsigned long:      (unsigned long)(x),      \
             long long:          (long long)(x),          \
             unsigned long long: (unsigned long long)(x), \
             default:            (void *)(uintptr_t)(x))

#define atomic_load_explicit(object, order) \
    ff_atomic_c(object, FF_ATOMIC_FN(load, object)(object, order))

#define atomic_load(object) \
    atomic_load_explicit(object, memory_order_seq_cst)

#define atomic_store_explicit(object, desired, order) \
    FF_ATOMIC_FN(store, object)(object, desired, order)

#define atomic_store(object, desired) \
    atomic_store_explicit(object, desired, memory_order_seq_cst)

#define atomic_exchange_explicit(object, desired, order) \
    ff_atomic_c(object, FF_ATOMIC_FN(exchange, object)(object, desired, order))

#define atomic_exchange(object, desired) \
    atomic_exchange_explicit(object, desired, memory_order_seq_cst)

static av_always_inline memory_order ff_atomic_cas_order(memory_order success,
                                                         memory_order failure)
{
    if (success == memory_order_release && failure != memory_order_relaxed)
        return memory_order_acq_rel;
    return success;
}

/* expected has to point to an object of the width of the atomic one */
#define atomic_compare_exchange_strong_explicit(object, expected, desired, \
                                                success, failure)          \
    ((void)_Generic((char (*)[sizeof(*(expected))])0,                      \
                    char (*)[sizeof(*(object))]: 0),                       \
     FF_ATOMIC_FN(cas, object)(object, expected, desired,                  \
                               ff_atomic_cas_order(success, failure)))

#define atomic_compare_exchange_strong(object, expected, desired)      \
    atomic_compare_exchange_strong_explicit(object, expected, desired, \
                                            memory_order_seq_cst,      \
                                            memory_order_seq_cst)

#define atomic_compare_exchange_weak_explicit(object, expected, desired, \
                                              success, failure)          \
    atomic_compare_exchange_strong_explicit(object, expected, desired,   \
                                            success, failure)

#define atomic_compare_exchange_weak(object, expected, desired) \
    atomic_compare_exchange_strong(object, expected, desired)

#define ff_atomic_fetch(object, operand, op, order) \
    ff_atomic_c(object, FF_ATOMIC_FETCH_FN(op, object)(object, operand, order))

#define atomic_fetch_add_explicit(object, operand, order) \
    ff_atomic_fetch(object, operand, fetch_add, order)

#define atomic_fetch_sub_explicit(object, operand, order) \
    ff_atomic_fetch(object, operand, fetch_sub, order)

#define atomic_fetch_or_explicit(object, operand, order) \
    ff_atomic_fetch(object, operand, fetch_or, order)

#define atomic_fetch_xor_explicit(object, operand, order) \
    ff_atomic_fetch(object, operand, fetch_xor, order)

#define atomic_fetch_and_explicit(object, operand, order) \
    ff_atomic_fetch(object, operand, fetch_and, order)

#define atomic_fetch_add(object, operand) \
    atomic_fetch_add_explicit(object, operand, memory_order_seq_cst)

#define atomic_fetch_sub(object, operand) \
    atomic_fetch_sub_explicit(object, operand, memory_order_seq_cst)

#define atomic_fetch_or(object, operand) \
    atomic_fetch_or_explicit(object, operand, memory_order_seq_cst)

#define atomic_fetch_xor(object, operand) \
    atomic_fetch_xor_explicit(object, operand, memory_order_seq_cst)

#define atomic_fetch_and(object, operand) \
    atomic_fetch_and_explicit(object, operand, memory_order_seq_cst)

typedef struct atomic_flag {
    atomic_bool value;
} atomic_flag;

#define ATOMIC_FLAG_INIT { 0 }

#define atomic_flag_test_and_set_explicit(object, order) \
    atomic_exchange_explicit(&(object)->value, 1, order)

#define atomic_flag_test_and_set(object) \
    atomic_flag_test_and_set_explicit(object, memory_order_seq_cst)

#define atomic_flag_clear_explicit(object, order) \
    atomic_store_explicit(&(object)->value, 0, order)

#define atomic_flag_clear(object) \
    atomic_flag_clear_explicit(object, memory_order_seq_cst)

#endif /* COMPAT_ATOMICS_STDATOMIC_H */
