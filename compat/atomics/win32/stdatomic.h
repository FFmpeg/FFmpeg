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

#ifndef COMPAT_ATOMICS_WIN32_STDATOMIC_H
#define COMPAT_ATOMICS_WIN32_STDATOMIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <intrin.h>

#include "libavutil/attributes.h"

typedef enum {
    memory_order_relaxed,
    memory_order_consume,
    memory_order_acquire,
    memory_order_release,
    memory_order_acq_rel,
    memory_order_seq_cst
} memory_order;

#define FF_ATOMIC_LOCK_FREE 2

/* x86 aligns the 8-byte automatic objects to 4 bytes, where an 8-byte access
 * that crosses a cache line is not single-copy atomic. The typedefs carry
 * the alignment, the qualifier form cannot. */
#ifdef _M_IX86
#define FF_ATOMIC_ALIGN64 __declspec(align(8))
#else
#define FF_ATOMIC_ALIGN64
#endif

/* Only compiler reordering has to be inhibited. */
static av_always_inline void ff_atomic_signal_fence(memory_order order)
{
    if (order != memory_order_relaxed)
        _ReadWriteBarrier();
}

/*
 * Everything is built on the compiler intrinsics, which are available for every
 * target and build type. SDK helpers are not very helpful here, as things are
 * just not available in some configurations. The plain accesses go through the
 * __iso_volatile intrinsics, which the compiler lowers to a single atomic
 * access of the width W for every naturally aligned object, including the
 * 8-byte ones on x86. The interlocked intrinsics are full barriers on x86
 * and x64, the ARM targets have acquire, release and no fence variants of
 * them.
 */
#if defined(_M_ARM64) || defined(_M_ARM64EC)

/*
 * The load-acquire and store-release instructions are sequentially consistent
 * among themselves, so no access needs a barrier. This relies on every acquire
 * load being the RCsc ldar, the RCpc ldapr of __load_acquire would need a
 * barrier after the seq_cst stores. ARM64EC compiles to the same code with the
 * x64 ABI.
 */
#define WIN32_ATOMIC_LOAD_STORE(name, type, W)                              \
static av_always_inline type                                                \
ff_atomic_load_##name(const volatile void *object, memory_order order)      \
{                                                                           \
    if (order != memory_order_relaxed)                                      \
        return (type)__ldar##W((unsigned __int##W volatile *)object);       \
    return (type)__iso_volatile_load##W((const volatile __int##W *)object); \
}                                                                           \
                                                                            \
static av_always_inline void ff_atomic_store_##name(volatile void *object,  \
                                                    type desired,           \
                                                    memory_order order)     \
{                                                                           \
    if (order == memory_order_relaxed)                                      \
        __iso_volatile_store##W((volatile __int##W *)object,                \
                                (__int##W)desired);                         \
    else                                                                    \
        __stlr##W((unsigned __int##W volatile *)object,                     \
                  (unsigned __int##W)desired);                              \
}

#define WIN32_ATOMIC_ORDERED(Intrinsic, suffix) Intrinsic##suffix
#define WIN32_ATOMIC_XCHG_REL _rel

static av_always_inline void ff_atomic_thread_fence(memory_order order)
{
    if (order == memory_order_acquire || order == memory_order_consume)
        __dmb(_ARM64_BARRIER_ISHLD);
    else if (order != memory_order_relaxed)
        __dmb(_ARM64_BARRIER_ISH);
}

#elif defined(_M_ARM)

/*
 * The plain accesses need explicit barriers, the inner shareable one after an
 * acquire load and before a release store, on both sides of a seq_cst store.
 * The exchange has no documented release variant, the full one serves. The
 * 8-byte plain accesses are not single-copy atomic without LPAE, the 64-bit
 * load and store are the exclusive ones.
 */
#define WIN32_ATOMIC_LOAD_STORE(name, type, W)                               \
static av_always_inline type                                                 \
ff_atomic_load_##name(const volatile void *object, memory_order order)       \
{                                                                            \
    type value =                                                             \
        (type)__iso_volatile_load##W((const volatile __int##W *)object);     \
    if (order != memory_order_relaxed)                                       \
        __dmb(_ARM_BARRIER_ISH);                                             \
    return value;                                                            \
}                                                                            \
                                                                             \
static av_always_inline void ff_atomic_store_##name(volatile void *object,   \
                                                    type desired,            \
                                                    memory_order order)      \
{                                                                            \
    if (order != memory_order_relaxed)                                       \
        __dmb(_ARM_BARRIER_ISH);                                             \
    __iso_volatile_store##W((volatile __int##W *)object, (__int##W)desired); \
    if (order == memory_order_seq_cst)                                       \
        __dmb(_ARM_BARRIER_ISH);                                             \
}

#define WIN32_ATOMIC_ORDERED(Intrinsic, suffix) Intrinsic##suffix
#define WIN32_ATOMIC_XCHG_REL

static av_always_inline void ff_atomic_thread_fence(memory_order order)
{
    if (order != memory_order_relaxed)
        __dmb(_ARM_BARRIER_ISH);
}

#else

/*
 * x86 and x64 are strongly ordered, only the compiler has to be kept from
 * reordering, and only the seq_cst store needs the full barrier after it.
 * A locked instruction is the cheaper full barrier.
 */
static av_always_inline void win32_atomic_full_fence(void)
{
#ifdef _M_X64
    __faststorefence();
#else
    long barrier;
    _InterlockedOr(&barrier, 0);
#endif
}

#define WIN32_ATOMIC_LOAD_STORE(name, type, W)                               \
static av_always_inline type                                                 \
ff_atomic_load_##name(const volatile void *object, memory_order order)       \
{                                                                            \
    type value =                                                             \
        (type)__iso_volatile_load##W((const volatile __int##W *)object);     \
    if (order != memory_order_relaxed)                                       \
        _ReadWriteBarrier();                                                 \
    return value;                                                            \
}                                                                            \
                                                                             \
static av_always_inline void ff_atomic_store_##name(volatile void *object,   \
                                                    type desired,            \
                                                    memory_order order)      \
{                                                                            \
    if (order != memory_order_relaxed)                                       \
        _ReadWriteBarrier();                                                 \
    __iso_volatile_store##W((volatile __int##W *)object, (__int##W)desired); \
    if (order == memory_order_seq_cst)                                       \
        win32_atomic_full_fence();                                           \
}

#define WIN32_ATOMIC_ORDERED(Intrinsic, suffix) Intrinsic
#define WIN32_ATOMIC_XCHG_REL

static av_always_inline void ff_atomic_thread_fence(memory_order order)
{
    if (order == memory_order_seq_cst)
        win32_atomic_full_fence();
    else if (order != memory_order_relaxed)
        _ReadWriteBarrier();
}

#endif

#define WIN32_ATOMIC_CALL(Intrinsic, rel, order, ...)                   \
    ((order) == memory_order_relaxed                                    \
         ? WIN32_ATOMIC_ORDERED(Intrinsic, _nf)(__VA_ARGS__)            \
   : (order) == memory_order_release                                    \
         ? WIN32_ATOMIC_ORDERED(Intrinsic, rel)(__VA_ARGS__)            \
   : (order) == memory_order_acquire || (order) == memory_order_consume \
         ? WIN32_ATOMIC_ORDERED(Intrinsic, _acq)(__VA_ARGS__)           \
         : Intrinsic(__VA_ARGS__))

/*
 * x86 has no 64-bit interlocked intrinsics besides the compare exchange, the
 * others are compare exchange loops. The SDK provides such loops, but they
 * do the arithmetic in signed types, so the wraparound is undefined behaviour
 * there.
 */
#ifdef _M_IX86
#define WIN32_ATOMIC_CAS_LOOP64(name, expr)                                 \
static av_always_inline long long                                           \
win32_Interlocked##name##64(volatile long long *object, long long operand)  \
{                                                                           \
    long long old = __iso_volatile_load64(object), prev;                    \
    while ((prev = _InterlockedCompareExchange64(object, (long long)(expr), \
                                                 old)) != old)              \
        old = prev;                                                         \
    return old;                                                             \
}
WIN32_ATOMIC_CAS_LOOP64(Exchange,    operand)
WIN32_ATOMIC_CAS_LOOP64(ExchangeAdd, (uint64_t)old + (uint64_t)operand)
WIN32_ATOMIC_CAS_LOOP64(Or,          old | operand)
WIN32_ATOMIC_CAS_LOOP64(Xor,         old ^ operand)
WIN32_ATOMIC_CAS_LOOP64(And,         old & operand)
#undef WIN32_ATOMIC_CAS_LOOP64
#define win32_InterlockedCompareExchange64 _InterlockedCompareExchange64
#endif

#define WIN32_ATOMIC_OP(op, Intrinsic, rel, expr, name, type)            \
static av_always_inline type ff_atomic_##op##name(volatile void *object, \
                                                  type operand,          \
                                                  memory_order order)    \
{                                                                        \
    return WIN32_ATOMIC_CALL(Intrinsic, rel, order, object, expr);       \
}

/* P and S are the prefix and the width suffix of the intrinsics. The
 * expected value is copied instead of accessed through a pointer of the
 * helper type, its object has the type of the caller. */
#define WIN32_ATOMIC_RMW(name, type, P, S)                                  \
static av_always_inline bool ff_atomic_cas_##name(volatile void *object,    \
                                                  void *expected,           \
                                                  type desired,             \
                                                  memory_order order)       \
{                                                                           \
    type old, prev;                                                         \
    memcpy(&old, expected, sizeof(old));                                    \
    prev = WIN32_ATOMIC_CALL(P##CompareExchange##S, _rel, order,            \
                             object, desired, old);                         \
    if (prev == old)                                                        \
        return 1;                                                           \
    memcpy(expected, &prev, sizeof(prev));                                  \
    return 0;                                                               \
}                                                                           \
                                                                            \
WIN32_ATOMIC_OP(exchange,  P##Exchange##S,    WIN32_ATOMIC_XCHG_REL,        \
                operand, _##name, type)                                     \
WIN32_ATOMIC_OP(fetch_add, P##ExchangeAdd##S, _rel, operand, _##name, type) \
WIN32_ATOMIC_OP(fetch_sub, P##ExchangeAdd##S, _rel,                         \
                (type)(0 - (uint64_t)operand), _##name, type)               \
WIN32_ATOMIC_OP(fetch_or,  P##Or##S,  _rel, operand, _##name, type)         \
WIN32_ATOMIC_OP(fetch_xor, P##Xor##S, _rel, operand, _##name, type)         \
WIN32_ATOMIC_OP(fetch_and, P##And##S, _rel, operand, _##name, type)

/* bool objects get their own set, for normalization */
WIN32_ATOMIC_LOAD_STORE(bool, bool,               8)
WIN32_ATOMIC_LOAD_STORE(8,    unsigned char,      8)
WIN32_ATOMIC_LOAD_STORE(16,   unsigned short,     16)
WIN32_ATOMIC_LOAD_STORE(32,   unsigned long,      32)
#ifndef _M_ARM
WIN32_ATOMIC_LOAD_STORE(64,   unsigned long long, 64)
#else
static av_always_inline unsigned long long
ff_atomic_load_64(const volatile void *object, memory_order order)
{
    unsigned long long value = __ldrexd((const volatile __int64 *)object);
    if (order != memory_order_relaxed)
        __dmb(_ARM_BARRIER_ISH);
    return value;
}

static av_always_inline void ff_atomic_store_64(volatile void *object,
                                                unsigned long long desired,
                                                memory_order order)
{
    if (order == memory_order_relaxed)
        _InterlockedExchange64_nf((volatile long long *)object, desired);
    else
        _InterlockedExchange64((volatile long long *)object, desired);
}
#endif

WIN32_ATOMIC_RMW(bool, bool,               _Interlocked, 8)
WIN32_ATOMIC_RMW(8,    unsigned char,      _Interlocked, 8)
WIN32_ATOMIC_RMW(16,   unsigned short,     _Interlocked, 16)
WIN32_ATOMIC_RMW(32,   unsigned long,      _Interlocked, )
#ifdef _M_IX86
WIN32_ATOMIC_RMW(64,   unsigned long long, win32_Interlocked, 64)
#else
WIN32_ATOMIC_RMW(64,   unsigned long long, _Interlocked, 64)
#endif

#undef WIN32_ATOMIC_LOAD_STORE
#undef WIN32_ATOMIC_RMW
#undef WIN32_ATOMIC_OP
#undef WIN32_ATOMIC_CALL
#undef WIN32_ATOMIC_ORDERED
#undef WIN32_ATOMIC_XCHG_REL
#undef win32_InterlockedCompareExchange64

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
    FF_ATOMIC_SIZE(store, sizeof(void *))(object, (uintptr_t)desired, order);
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

#endif /* COMPAT_ATOMICS_WIN32_STDATOMIC_H */
