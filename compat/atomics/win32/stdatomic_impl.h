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

#ifndef COMPAT_ATOMICS_WIN32_STDATOMIC_IMPL_H
#define COMPAT_ATOMICS_WIN32_STDATOMIC_IMPL_H

#include <string.h>

#include <intrin.h>

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

#endif /* COMPAT_ATOMICS_WIN32_STDATOMIC_IMPL_H */
