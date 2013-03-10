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

#include <stddef.h>
#include <stdint.h>

#include "libavutil/attributes.h"
#include "hpeldsp_arm.h"

void ff_put_pixels16_armv6(uint8_t *, const uint8_t *, ptrdiff_t, int);
void ff_put_pixels16_x2_armv6(uint8_t *, const uint8_t *, ptrdiff_t, int);
void ff_put_pixels16_y2_armv6(uint8_t *, const uint8_t *, ptrdiff_t, int);

void ff_put_pixels16_x2_no_rnd_armv6(uint8_t *, const uint8_t *, ptrdiff_t, int);
void ff_put_pixels16_y2_no_rnd_armv6(uint8_t *, const uint8_t *, ptrdiff_t, int);

void ff_avg_pixels16_armv6(uint8_t *, const uint8_t *, ptrdiff_t, int);

void ff_put_pixels8_armv6(uint8_t *, const uint8_t *, ptrdiff_t, int);
void ff_put_pixels8_x2_armv6(uint8_t *, const uint8_t *, ptrdiff_t, int);
void ff_put_pixels8_y2_armv6(uint8_t *, const uint8_t *, ptrdiff_t, int);

void ff_put_pixels8_x2_no_rnd_armv6(uint8_t *, const uint8_t *, ptrdiff_t, int);
void ff_put_pixels8_y2_no_rnd_armv6(uint8_t *, const uint8_t *, ptrdiff_t, int);

void ff_avg_pixels8_armv6(uint8_t *, const uint8_t *, ptrdiff_t, int);

av_cold void ff_hpeldsp_init_armv6(HpelDSPContext *c, int flags)
{
    c->put_pixels_tab[0][0] = ff_put_pixels16_armv6;
    c->put_pixels_tab[0][1] = ff_put_pixels16_x2_armv6;
    c->put_pixels_tab[0][2] = ff_put_pixels16_y2_armv6;
/*     c->put_pixels_tab[0][3] = ff_put_pixels16_xy2_armv6; */
    c->put_pixels_tab[1][0] = ff_put_pixels8_armv6;
    c->put_pixels_tab[1][1] = ff_put_pixels8_x2_armv6;
    c->put_pixels_tab[1][2] = ff_put_pixels8_y2_armv6;
/*     c->put_pixels_tab[1][3] = ff_put_pixels8_xy2_armv6; */

    c->put_no_rnd_pixels_tab[0][0] = ff_put_pixels16_armv6;
    c->put_no_rnd_pixels_tab[0][1] = ff_put_pixels16_x2_no_rnd_armv6;
    c->put_no_rnd_pixels_tab[0][2] = ff_put_pixels16_y2_no_rnd_armv6;
/*     c->put_no_rnd_pixels_tab[0][3] = ff_put_pixels16_xy2_no_rnd_armv6; */
    c->put_no_rnd_pixels_tab[1][0] = ff_put_pixels8_armv6;
    c->put_no_rnd_pixels_tab[1][1] = ff_put_pixels8_x2_no_rnd_armv6;
    c->put_no_rnd_pixels_tab[1][2] = ff_put_pixels8_y2_no_rnd_armv6;
/*     c->put_no_rnd_pixels_tab[1][3] = ff_put_pixels8_xy2_no_rnd_armv6; */

    c->avg_pixels_tab[0][0] = ff_avg_pixels16_armv6;
    c->avg_pixels_tab[1][0] = ff_avg_pixels8_armv6;
}
