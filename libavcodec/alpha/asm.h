/*
 * Alpha optimized DSP utils
 * Copyright (c) 2002 Falk Hueffner <falk@debian.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef LIBAVCODEC_ALPHA_ASM_H
#define LIBAVCODEC_ALPHA_ASM_H

#include <stdint.h>

#define AMASK_BWX (1 << 0)
#define AMASK_FIX (1 << 1)
#define AMASK_MVI (1 << 8)

static inline uint64_t BYTE_VEC(uint64_t x)
{
    x |= x <<  8;
    x |= x << 16;
    x |= x << 32;
    return x;
}
static inline uint64_t WORD_VEC(uint64_t x)
{
    x |= x << 16;
    x |= x << 32;
    return x;
}

static inline int32_t ldl(const void* p)
{
    return *(const int32_t*) p;
}
static inline uint64_t ldq(const void* p)
{
    return *(const uint64_t*) p;
}
/* FIXME ccc doesn't seem to get it? Use inline asm?  */
static inline uint64_t ldq_u(const void* p)
{
    return *(const uint64_t*) ((uintptr_t) p & ~7ul);
}
static inline void stl(uint32_t l, void* p)
{
    *(uint32_t*) p = l;
}
static inline void stq(uint64_t l, void* p)
{
    *(uint64_t*) p = l;
}

#ifdef __GNUC__
#define OPCODE1(name)						\
static inline uint64_t name(uint64_t l)				\
{								\
    uint64_t r;							\
    asm (#name " %1, %0" : "=r" (r) : "r" (l));			\
    return r;							\
}

#define OPCODE2(name)						\
static inline uint64_t name(uint64_t l1, uint64_t l2)		\
{								\
    uint64_t r;							\
    asm (#name " %1, %2, %0" : "=r" (r) : "r" (l1), "rI" (l2));	\
    return r;							\
}

/* We don't want gcc to move this around or combine it with another
   rpcc, so mark it volatile.  */
static inline uint64_t rpcc(void)
{
    uint64_t r;
    asm volatile ("rpcc %0" : "=r" (r));
    return r;
}

static inline uint64_t uldq(const void* v)
{
    struct foo {
	unsigned long l;
    } __attribute__((packed));

    return ((const struct foo*) v)->l;
}

#elif defined(__DECC)		/* Compaq "ccc" compiler */

#include <c_asm.h>
#define OPCODE1(name)							\
static inline uint64_t name(uint64_t l)					\
{									\
    return asm (#name " %a0, %v0", l);					\
}

#define OPCODE2(name)							\
static inline uint64_t name(uint64_t l1, uint64_t l2)			\
{									\
    return asm (#name " %a0, %a1, %v0", l1, l2);			\
}

static inline uint64_t rpcc(void)
{
    return asm  ("rpcc %v0");
}

static inline uint64_t uldq(const void* v)
{
    return *(const __unaligned uint64_t *) v;
}

#endif

OPCODE1(amask);
OPCODE1(unpkbw);
OPCODE1(pkwb);
OPCODE2(extql);
OPCODE2(extqh);
OPCODE2(zap);
OPCODE2(cmpbge);
OPCODE2(minsw4);
OPCODE2(minuw4);
OPCODE2(minub8);
OPCODE2(maxsw4);
OPCODE2(maxuw4);
OPCODE2(perr);

#endif /* LIBAVCODEC_ALPHA_ASM_H */
