/*
 * Copyright (c) 2002 Brian Foley
 * Copyright (c) 2002 Dieter Shirley
 * Copyright (c) 2003-2004 Romain Dolbeau <romain@dolbeau.org>
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

#include "config.h"

#include "libavutil/attributes.h"
#include "libavutil/ppc/util_altivec.h"

#include "libavcodec/pixblockdsp.h"

#include "pixblockdsp_vsx.h"

static void get_pixels_vsx(int16_t *restrict block, const uint8_t *pixels,
                           ptrdiff_t stride)
{
    int i;
    for (i = 0; i < 8; i++) {
        vec_s16 shorts = vsx_ld_u8_s16(0, pixels);

        vec_vsx_st(shorts, i * 16, block);

        pixels += stride;
    }
}

static void diff_pixels_vsx(int16_t *restrict block, const uint8_t *s1,
                            const uint8_t *s2, ptrdiff_t stride)
{
    int i;
    vec_s16 shorts1, shorts2;
    for (i = 0; i < 8; i++) {
        shorts1 = vsx_ld_u8_s16(0, s1);
        shorts2 = vsx_ld_u8_s16(0, s2);

        shorts1 = vec_sub(shorts1, shorts2);

        vec_vsx_st(shorts1, 0, block);

        s1    += stride;
        s2    += stride;
        block += 8;
    }
}

av_cold void ff_pixblockdsp_init_vsx(PixblockDSPContext *c,
                                     unsigned high_bit_depth)
{
    c->diff_pixels = diff_pixels_vsx;

    if (!high_bit_depth)
        c->get_pixels = get_pixels_vsx;
}
