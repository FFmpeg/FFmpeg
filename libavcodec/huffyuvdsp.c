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

#include <stdint.h>

#include "config.h"
#include "libavutil/attributes.h"
#include "mathops.h"
#include "huffyuvdsp.h"

static void add_hfyu_left_pred_bgr32_c(uint8_t *dst, const uint8_t *src,
                                       intptr_t w, uint8_t *left)
{
    int i;
    uint8_t r = left[R], g = left[G], b = left[B], a = left[A];

    for (i = 0; i < w; i++) {
        b += src[4 * i + B];
        g += src[4 * i + G];
        r += src[4 * i + R];
        a += src[4 * i + A];

        dst[4 * i + B] = b;
        dst[4 * i + G] = g;
        dst[4 * i + R] = r;
        dst[4 * i + A] = a;
    }

    left[B] = b;
    left[G] = g;
    left[R] = r;
    left[A] = a;
}

av_cold void ff_huffyuvdsp_init(HuffYUVDSPContext *c)
{
    c->add_hfyu_left_pred_bgr32 = add_hfyu_left_pred_bgr32_c;

    if (ARCH_X86)
        ff_huffyuvdsp_init_x86(c);
}
