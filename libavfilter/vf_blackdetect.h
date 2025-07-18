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

#ifndef AVFILTER_BLACKDETECT_H
#define AVFILTER_BLACKDETECT_H

#include <stddef.h>
#include <stdint.h>

typedef unsigned (*ff_blackdetect_fn)(const uint8_t *src, ptrdiff_t stride,
                                                   ptrdiff_t width, ptrdiff_t height,
                                                   unsigned threshold);

ff_blackdetect_fn ff_blackdetect_get_fn_x86(int depth);

static unsigned count_pixels8_c(const uint8_t *src, ptrdiff_t stride,
                                ptrdiff_t width, ptrdiff_t height,
                                unsigned threshold)
{
    unsigned int counter = 0;
    while (height--) {
        for (int x = 0; x < width; x++)
            counter += src[x] <= threshold;
        src += stride;
    }
    return counter;
}

static unsigned count_pixels16_c(const uint8_t *src, ptrdiff_t stride,
                                 ptrdiff_t width, ptrdiff_t height,
                                 unsigned threshold)
{
    unsigned int counter = 0;
    while (height--) {
        const uint16_t *src16 = (const uint16_t *) src;
        for (int x = 0; x < width; x++)
            counter += src16[x] <= threshold;
        src += stride;
    }
    return counter;
}


static inline ff_blackdetect_fn ff_blackdetect_get_fn(int depth)
{
    ff_blackdetect_fn fn = NULL;
#if ARCH_X86
    fn = ff_blackdetect_get_fn_x86(depth);
#endif

    if (!fn)
        fn = depth == 8 ? count_pixels8_c : count_pixels16_c;
    return fn;
}

#endif /* AVFILTER_BLACKDETECT_H */
