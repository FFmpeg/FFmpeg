/*
 * This file is part of MPlayer.
 *
 * MPlayer is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * MPlayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with MPlayer; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef MPLAYER_FASTMEMCPY_H
#define MPLAYER_FASTMEMCPY_H

#include <inttypes.h>
#include <string.h>
#include <stddef.h>

void * fast_memcpy(void * to, const void * from, size_t len);
void * mem2agpcpy(void * to, const void * from, size_t len);

#if ! defined(CONFIG_FASTMEMCPY) || ! (HAVE_MMX || HAVE_MMX2 || HAVE_AMD3DNOW /* || HAVE_SSE || HAVE_SSE2 */)
#define mem2agpcpy(a,b,c) memcpy(a,b,c)
#define fast_memcpy(a,b,c) memcpy(a,b,c)
#endif

static inline void * mem2agpcpy_pic(void * dst, const void * src, int bytesPerLine, int height, int dstStride, int srcStride)
{
    int i;
    void *retval=dst;

    if(dstStride == srcStride)
    {
        if (srcStride < 0) {
                src = (const uint8_t*)src + (height-1)*srcStride;
                dst = (uint8_t*)dst + (height-1)*dstStride;
                srcStride = -srcStride;
        }

        mem2agpcpy(dst, src, srcStride*height);
    }
    else
    {
        for(i=0; i<height; i++)
        {
            mem2agpcpy(dst, src, bytesPerLine);
            src = (const uint8_t*)src + srcStride;
            dst = (uint8_t*)dst + dstStride;
        }
    }

    return retval;
}

#define memcpy_pic(d, s, b, h, ds, ss) memcpy_pic2(d, s, b, h, ds, ss, 0)
#define my_memcpy_pic(d, s, b, h, ds, ss) memcpy_pic2(d, s, b, h, ds, ss, 1)

/**
 * \param limit2width always skip data between end of line and start of next
 *                    instead of copying the full block when strides are the same
 */
static inline void * memcpy_pic2(void * dst, const void * src,
                                 int bytesPerLine, int height,
                                 int dstStride, int srcStride, int limit2width)
{
    int i;
    void *retval=dst;

    if(!limit2width && dstStride == srcStride)
    {
        if (srcStride < 0) {
                src = (const uint8_t*)src + (height-1)*srcStride;
                dst = (uint8_t*)dst + (height-1)*dstStride;
                srcStride = -srcStride;
        }

        fast_memcpy(dst, src, srcStride*height);
    }
    else
    {
        for(i=0; i<height; i++)
        {
            fast_memcpy(dst, src, bytesPerLine);
            src = (const uint8_t*)src + srcStride;
            dst = (uint8_t*)dst + dstStride;
        }
    }

    return retval;
}

#endif /* MPLAYER_FASTMEMCPY_H */
