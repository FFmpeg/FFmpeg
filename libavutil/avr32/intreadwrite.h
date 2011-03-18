/*
 * Copyright (c) 2009 Mans Rullgard <mans@mansr.com>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVUTIL_AVR32_INTREADWRITE_H
#define AVUTIL_AVR32_INTREADWRITE_H

#include <stdint.h>
#include "config.h"
#include "libavutil/bswap.h"

/*
 * AVR32 does not support unaligned memory accesses, except for the AP
 * series which suppports unaligned 32-bit loads and stores.  16-bit
 * and 64-bit accesses must be aligned to 16 and 32 bits, respectively.
 * This means we cannot use the byte-swapping load/store instructions
 * here.
 *
 * For 16-bit, 24-bit, and (on UC series) 32-bit loads, we instead use
 * the LDINS.B instruction, which gcc fails to utilise with the
 * generic code.  GCC also fails to use plain LD.W and ST.W even for
 * AP processors, so we override the generic code.  The 64-bit
 * versions are improved by using our optimised 32-bit functions.
 */

#define AV_RL16 AV_RL16
static av_always_inline uint16_t AV_RL16(const void *p)
{
    uint16_t v;
    __asm__ ("ld.ub    %0,   %1  \n\t"
             "ldins.b  %0:l, %2  \n\t"
             : "=&r"(v)
             : "m"(*(const uint8_t*)p), "RKs12"(*((const uint8_t*)p+1)));
    return v;
}

#define AV_RB16 AV_RB16
static av_always_inline uint16_t AV_RB16(const void *p)
{
    uint16_t v;
    __asm__ ("ld.ub    %0,   %2  \n\t"
             "ldins.b  %0:l, %1  \n\t"
             : "=&r"(v)
             : "RKs12"(*(const uint8_t*)p), "m"(*((const uint8_t*)p+1)));
    return v;
}

#define AV_RB24 AV_RB24
static av_always_inline uint32_t AV_RB24(const void *p)
{
    uint32_t v;
    __asm__ ("ld.ub    %0,   %3  \n\t"
             "ldins.b  %0:l, %2  \n\t"
             "ldins.b  %0:u, %1  \n\t"
             : "=&r"(v)
             : "RKs12"(* (const uint8_t*)p),
               "RKs12"(*((const uint8_t*)p+1)),
               "m"    (*((const uint8_t*)p+2)));
    return v;
}

#define AV_RL24 AV_RL24
static av_always_inline uint32_t AV_RL24(const void *p)
{
    uint32_t v;
    __asm__ ("ld.ub    %0,   %1  \n\t"
             "ldins.b  %0:l, %2  \n\t"
             "ldins.b  %0:u, %3  \n\t"
             : "=&r"(v)
             : "m"    (* (const uint8_t*)p),
               "RKs12"(*((const uint8_t*)p+1)),
               "RKs12"(*((const uint8_t*)p+2)));
    return v;
}

#if ARCH_AVR32_AP

#define AV_RB32 AV_RB32
static av_always_inline uint32_t AV_RB32(const void *p)
{
    uint32_t v;
    __asm__ ("ld.w %0, %1" : "=r"(v) : "m"(*(const uint32_t*)p));
    return v;
}

#define AV_WB32 AV_WB32
static av_always_inline void AV_WB32(void *p, uint32_t v)
{
    __asm__ ("st.w %0, %1" : "=m"(*(uint32_t*)p) : "r"(v));
}

/* These two would be defined by generic code, but we need them sooner. */
#define AV_RL32(p)    av_bswap32(AV_RB32(p))
#define AV_WL32(p, v) AV_WB32(p, av_bswap32(v))

#define AV_WB64 AV_WB64
static av_always_inline void AV_WB64(void *p, uint64_t v)
{
    union { uint64_t v; uint32_t hl[2]; } vv = { v };
    AV_WB32(p, vv.hl[0]);
    AV_WB32((uint32_t*)p+1, vv.hl[1]);
}

#define AV_WL64 AV_WL64
static av_always_inline void AV_WL64(void *p, uint64_t v)
{
    union { uint64_t v; uint32_t hl[2]; } vv = { v };
    AV_WL32(p, vv.hl[1]);
    AV_WL32((uint32_t*)p+1, vv.hl[0]);
}

#else /* ARCH_AVR32_AP */

#define AV_RB32 AV_RB32
static av_always_inline uint32_t AV_RB32(const void *p)
{
    uint32_t v;
    __asm__ ("ld.ub    %0,   %4  \n\t"
             "ldins.b  %0:l, %3  \n\t"
             "ldins.b  %0:u, %2  \n\t"
             "ldins.b  %0:t, %1  \n\t"
             : "=&r"(v)
             : "RKs12"(* (const uint8_t*)p),
               "RKs12"(*((const uint8_t*)p+1)),
               "RKs12"(*((const uint8_t*)p+2)),
               "m"    (*((const uint8_t*)p+3)));
    return v;
}

#define AV_RL32 AV_RL32
static av_always_inline uint32_t AV_RL32(const void *p)
{
    uint32_t v;
    __asm__ ("ld.ub    %0,   %1  \n\t"
             "ldins.b  %0:l, %2  \n\t"
             "ldins.b  %0:u, %3  \n\t"
             "ldins.b  %0:t, %4  \n\t"
             : "=&r"(v)
             : "m"    (* (const uint8_t*)p),
               "RKs12"(*((const uint8_t*)p+1)),
               "RKs12"(*((const uint8_t*)p+2)),
               "RKs12"(*((const uint8_t*)p+3)));
    return v;
}

#endif /* ARCH_AVR32_AP */

#define AV_RB64 AV_RB64
static av_always_inline uint64_t AV_RB64(const void *p)
{
    union { uint64_t v; uint32_t hl[2]; } v;
    v.hl[0] = AV_RB32(p);
    v.hl[1] = AV_RB32((const uint32_t*)p+1);
    return v.v;
}

#define AV_RL64 AV_RL64
static av_always_inline uint64_t AV_RL64(const void *p)
{
    union { uint64_t v; uint32_t hl[2]; } v;
    v.hl[1] = AV_RL32(p);
    v.hl[0] = AV_RL32((const uint32_t*)p+1);
    return v.v;
}

#endif /* AVUTIL_AVR32_INTREADWRITE_H */
