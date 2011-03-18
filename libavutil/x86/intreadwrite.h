/*
 * Copyright (c) 2010 Alexander Strange <astrange@ithinksw.com>
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

#ifndef AVUTIL_X86_INTREADWRITE_H
#define AVUTIL_X86_INTREADWRITE_H

#include <stdint.h>
#include "config.h"
#include "libavutil/attributes.h"

#if HAVE_MMX

#if !HAVE_FAST_64BIT && defined(__MMX__)

#define AV_COPY64 AV_COPY64
static av_always_inline void AV_COPY64(void *d, const void *s)
{
    __asm__("movq   %1, %%mm0  \n\t"
            "movq   %%mm0, %0  \n\t"
            : "=m"(*(uint64_t*)d)
            : "m" (*(const uint64_t*)s)
            : "mm0");
}

#define AV_SWAP64 AV_SWAP64
static av_always_inline void AV_SWAP64(void *a, void *b)
{
    __asm__("movq   %1, %%mm0  \n\t"
            "movq   %0, %%mm1  \n\t"
            "movq   %%mm0, %0  \n\t"
            "movq   %%mm1, %1  \n\t"
            : "+m"(*(uint64_t*)a), "+m"(*(uint64_t*)b)
            ::"mm0", "mm1");
}

#define AV_ZERO64 AV_ZERO64
static av_always_inline void AV_ZERO64(void *d)
{
    __asm__("pxor %%mm0, %%mm0  \n\t"
            "movq %%mm0, %0     \n\t"
            : "=m"(*(uint64_t*)d)
            :: "mm0");
}

#endif /* !HAVE_FAST_64BIT && defined(__MMX__) */

#ifdef __SSE__

#define AV_COPY128 AV_COPY128
static av_always_inline void AV_COPY128(void *d, const void *s)
{
    struct v {uint64_t v[2];};

    __asm__("movaps   %1, %%xmm0  \n\t"
            "movaps   %%xmm0, %0  \n\t"
            : "=m"(*(struct v*)d)
            : "m" (*(const struct v*)s)
            : "xmm0");
}

#endif /* __SSE__ */

#ifdef __SSE2__

#define AV_ZERO128 AV_ZERO128
static av_always_inline void AV_ZERO128(void *d)
{
    struct v {uint64_t v[2];};

    __asm__("pxor %%xmm0, %%xmm0  \n\t"
            "movdqa   %%xmm0, %0  \n\t"
            : "=m"(*(struct v*)d)
            :: "xmm0");
}

#endif /* __SSE2__ */

#endif /* HAVE_MMX */

#endif /* AVUTIL_X86_INTREADWRITE_H */
