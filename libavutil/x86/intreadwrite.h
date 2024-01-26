/*
 * Copyright (c) 2010 Alexander Strange <astrange@ithinksw.com>
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

#ifndef AVUTIL_X86_INTREADWRITE_H
#define AVUTIL_X86_INTREADWRITE_H

#include <stdint.h>
#include "config.h"
#include "libavutil/attributes.h"

#if HAVE_MMX

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
