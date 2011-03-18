/*
 * Copyright (c) 2008 Mans Rullgard <mans@mansr.com>
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

#ifndef AVUTIL_PPC_INTREADWRITE_H
#define AVUTIL_PPC_INTREADWRITE_H

#include <stdint.h>
#include "config.h"

#if HAVE_XFORM_ASM

#define AV_RL16 AV_RL16
static av_always_inline uint16_t AV_RL16(const void *p)
{
    uint16_t v;
    __asm__ ("lhbrx   %0, %y1" : "=r"(v) : "Z"(*(const uint16_t*)p));
    return v;
}

#define AV_WL16 AV_WL16
static av_always_inline void AV_WL16(void *p, uint16_t v)
{
    __asm__ ("sthbrx  %1, %y0" : "=Z"(*(uint16_t*)p) : "r"(v));
}

#define AV_RL32 AV_RL32
static av_always_inline uint32_t AV_RL32(const void *p)
{
    uint32_t v;
    __asm__ ("lwbrx   %0, %y1" : "=r"(v) : "Z"(*(const uint32_t*)p));
    return v;
}

#define AV_WL32 AV_WL32
static av_always_inline void AV_WL32(void *p, uint32_t v)
{
    __asm__ ("stwbrx  %1, %y0" : "=Z"(*(uint32_t*)p) : "r"(v));
}

#if HAVE_LDBRX

#define AV_RL64 AV_RL64
static av_always_inline uint64_t AV_RL64(const void *p)
{
    uint64_t v;
    __asm__ ("ldbrx   %0, %y1" : "=r"(v) : "Z"(*(const uint64_t*)p));
    return v;
}

#define AV_WL64 AV_WL64
static av_always_inline void AV_WL64(void *p, uint64_t v)
{
    __asm__ ("stdbrx  %1, %y0" : "=Z"(*(uint64_t*)p) : "r"(v));
}

#else

#define AV_RL64 AV_RL64
static av_always_inline uint64_t AV_RL64(const void *p)
{
    union { uint64_t v; uint32_t hl[2]; } v;
    __asm__ ("lwbrx   %0, %y2  \n\t"
             "lwbrx   %1, %y3  \n\t"
             : "=&r"(v.hl[1]), "=r"(v.hl[0])
             : "Z"(*(const uint32_t*)p), "Z"(*((const uint32_t*)p+1)));
    return v.v;
}

#define AV_WL64 AV_WL64
static av_always_inline void AV_WL64(void *p, uint64_t v)
{
    union { uint64_t v; uint32_t hl[2]; } vv = { v };
    __asm__ ("stwbrx  %2, %y0  \n\t"
             "stwbrx  %3, %y1  \n\t"
             : "=Z"(*(uint32_t*)p), "=Z"(*((uint32_t*)p+1))
             : "r"(vv.hl[1]), "r"(vv.hl[0]));
}

#endif /* HAVE_LDBRX */

#endif /* HAVE_XFORM_ASM */

/*
 * GCC fails miserably on the packed struct version which is used by
 * default, so we override it here.
 */

#define AV_RB64(p) (*(const uint64_t *)(p))
#define AV_WB64(p, v) (*(uint64_t *)(p) = (v))

#endif /* AVUTIL_PPC_INTREADWRITE_H */
