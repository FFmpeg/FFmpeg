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

#ifndef COMPAT_ATOMICS_DUMMY_STDATOMIC_IMPL_H
#define COMPAT_ATOMICS_DUMMY_STDATOMIC_IMPL_H

#include <string.h>

#define FF_ATOMIC_LOCK_FREE 0
#define FF_ATOMIC_ALIGN64

#define ff_atomic_thread_fence(order) \
    ((void)(order))

#define ff_atomic_signal_fence(order) \
    ((void)(order))

/*
 * The accesses go through memcpy instead of casted pointers to stay clear
 * of strict-aliasing violations for types like long that have the size of
 * an intN_t without being compatible with it. They are ordinary accesses,
 * a volatile qualification of the object is not honoured.
 */
#define DUMMY_ATOMIC_FETCH(op, OP, name, type)                                \
static av_always_inline type ff_atomic_##op##name(volatile void *object,      \
                                                  type operand,               \
                                                  memory_order order)         \
{                                                                             \
    type old = ff_atomic_load##name(object, order);                           \
    ff_atomic_store##name(object, old OP operand, order);                     \
    return old;                                                               \
}

#define DUMMY_ATOMIC_OPS(name, type)                                          \
static av_always_inline type                                                  \
ff_atomic_load_##name(const volatile void *object, memory_order order)        \
{                                                                             \
    (void)order;                                                              \
    type v;                                                                   \
    memcpy(&v, (const void *)object, sizeof(v));                              \
    return v;                                                                 \
}                                                                             \
                                                                              \
static av_always_inline void ff_atomic_store_##name(volatile void *object,    \
                                                    type desired,             \
                                                    memory_order order)       \
{                                                                             \
    (void)order;                                                              \
    memcpy((void *)object, &desired, sizeof(desired));                        \
}                                                                             \
                                                                              \
static av_always_inline type ff_atomic_exchange_##name(volatile void *object, \
                                                       type desired,          \
                                                       memory_order order)    \
{                                                                             \
    type old = ff_atomic_load_##name(object, order);                          \
    ff_atomic_store_##name(object, desired, order);                           \
    return old;                                                               \
}                                                                             \
                                                                              \
static av_always_inline bool ff_atomic_cas_##name(volatile void *object,      \
                                                  void *expected,             \
                                                  type desired,               \
                                                  memory_order order)         \
{                                                                             \
    type old = ff_atomic_load_##name(object, order);                          \
    if (old == ff_atomic_load_##name(expected, order)) {                      \
        ff_atomic_store_##name(object, desired, order);                       \
        return 1;                                                             \
    }                                                                         \
    ff_atomic_store_##name(expected, old, order);                             \
    return 0;                                                                 \
}                                                                             \
                                                                              \
DUMMY_ATOMIC_FETCH(fetch_add, +, _##name, type)                               \
DUMMY_ATOMIC_FETCH(fetch_sub, -, _##name, type)                               \
DUMMY_ATOMIC_FETCH(fetch_or,  |, _##name, type)                               \
DUMMY_ATOMIC_FETCH(fetch_xor, ^, _##name, type)                               \
DUMMY_ATOMIC_FETCH(fetch_and, &, _##name, type)

/* bool objects get their own set, for normalization */
DUMMY_ATOMIC_OPS(bool, bool)
DUMMY_ATOMIC_OPS(8,    uint8_t)
DUMMY_ATOMIC_OPS(16,   uint16_t)
DUMMY_ATOMIC_OPS(32,   uint32_t)
DUMMY_ATOMIC_OPS(64,   uint64_t)

#undef DUMMY_ATOMIC_OPS
#undef DUMMY_ATOMIC_FETCH

#endif /* COMPAT_ATOMICS_DUMMY_STDATOMIC_IMPL_H */
