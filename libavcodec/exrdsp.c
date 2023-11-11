/*
 * This file is part of FFmpeg.
 *
 * Copyright (c) 2006 Industrial Light & Magic, a division of Lucas Digital Ltd. LLC
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

#include <stdint.h>

#include "libavutil/attributes.h"
#include "exrdsp.h"
#include "config.h"

static void reorder_pixels_scalar(uint8_t *dst, const uint8_t *src, ptrdiff_t size)
{
    const uint8_t *t1 = src;
    int half_size     = size / 2;
    const uint8_t *t2 = src + half_size;
    uint8_t *s        = dst;
    int i;

    for (i = 0; i < half_size; i++) {
        *(s++) = *(t1++);
        *(s++) = *(t2++);
    }
}

static void predictor_scalar(uint8_t *src, ptrdiff_t size)
{
    /* Unrolled: `src[i + 1] += src[i] - 128;` */
    if ((size & 1) == 0) {
        src[1] += src[0] ^ 0x80;
        src++;
        size--;
    }

    for (ptrdiff_t i = 1; i < size; i += 2) {
        uint8_t a = src[i] + src[i - 1];

        src[i] = a;
        src[i + 1] += a;
        src[i] ^= 0x80;
    }
}

av_cold void ff_exrdsp_init(ExrDSPContext *c)
{
    c->reorder_pixels   = reorder_pixels_scalar;
    c->predictor        = predictor_scalar;

#if ARCH_RISCV
    ff_exrdsp_init_riscv(c);
#elif ARCH_X86
    ff_exrdsp_init_x86(c);
#endif
}
