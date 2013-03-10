/*
 * BlackFin DSPUTILS
 *
 * Copyright (C) 2007 Marc Hoffman <marc.hoffman@analog.com>
 * Copyright (c) 2006 Michael Benjamin <michael.benjamin@analog.com>
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
#include "libavcodec/hpeldsp.h"
#include "hpeldsp_bfin.h"

static void bfin_put_pixels8 (uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h)
{
    ff_bfin_put_pixels8uc (block, pixels, pixels, line_size, line_size, h);
}

static void bfin_put_pixels8_x2(uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h)
{
    ff_bfin_put_pixels8uc (block, pixels, pixels+1, line_size, line_size, h);
}

static void bfin_put_pixels8_y2 (uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h)
{
    ff_bfin_put_pixels8uc (block, pixels, pixels+line_size, line_size, line_size, h);
}

static void bfin_put_pixels8_xy2 (uint8_t *block, const uint8_t *s0, ptrdiff_t line_size, int h)
{
    ff_bfin_z_put_pixels8_xy2 (block,s0,line_size, line_size, h);
}

static void bfin_put_pixels16 (uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h)
{
    ff_bfin_put_pixels16uc (block, pixels, pixels, line_size, line_size, h);
}

static void bfin_put_pixels16_x2 (uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h)
{
    ff_bfin_put_pixels16uc (block, pixels, pixels+1, line_size, line_size, h);
}

static void bfin_put_pixels16_y2 (uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h)
{
    ff_bfin_put_pixels16uc (block, pixels, pixels+line_size, line_size, line_size, h);
}

static void bfin_put_pixels16_xy2 (uint8_t *block, const uint8_t *s0, ptrdiff_t line_size, int h)
{
    ff_bfin_z_put_pixels16_xy2 (block,s0,line_size, line_size, h);
}

static void bfin_put_pixels8_nornd (uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h)
{
    ff_bfin_put_pixels8uc_nornd (block, pixels, pixels, line_size, h);
}

static void bfin_put_pixels8_x2_nornd (uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h)
{
    ff_bfin_put_pixels8uc_nornd (block, pixels, pixels+1, line_size, h);
}

static void bfin_put_pixels8_y2_nornd (uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h)
{
    ff_bfin_put_pixels8uc_nornd (block, pixels, pixels+line_size, line_size, h);
}


static void bfin_put_pixels16_nornd (uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h)
{
    ff_bfin_put_pixels16uc_nornd (block, pixels, pixels, line_size, h);
}

static void bfin_put_pixels16_x2_nornd (uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h)
{
    ff_bfin_put_pixels16uc_nornd (block, pixels, pixels+1, line_size, h);
}

static void bfin_put_pixels16_y2_nornd (uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h)
{
    ff_bfin_put_pixels16uc_nornd (block, pixels, pixels+line_size, line_size, h);
}

av_cold void ff_hpeldsp_init_bfin(HpelDSPContext *c, int flags)
{
    c->put_pixels_tab[0][0] = bfin_put_pixels16;
    c->put_pixels_tab[0][1] = bfin_put_pixels16_x2;
    c->put_pixels_tab[0][2] = bfin_put_pixels16_y2;
    c->put_pixels_tab[0][3] = bfin_put_pixels16_xy2;

    c->put_pixels_tab[1][0] = bfin_put_pixels8;
    c->put_pixels_tab[1][1] = bfin_put_pixels8_x2;
    c->put_pixels_tab[1][2] = bfin_put_pixels8_y2;
    c->put_pixels_tab[1][3] = bfin_put_pixels8_xy2;

    c->put_no_rnd_pixels_tab[1][0] = bfin_put_pixels8_nornd;
    c->put_no_rnd_pixels_tab[1][1] = bfin_put_pixels8_x2_nornd;
    c->put_no_rnd_pixels_tab[1][2] = bfin_put_pixels8_y2_nornd;
/*     c->put_no_rnd_pixels_tab[1][3] = ff_bfin_put_pixels8_xy2_nornd; */

    c->put_no_rnd_pixels_tab[0][0] = bfin_put_pixels16_nornd;
    c->put_no_rnd_pixels_tab[0][1] = bfin_put_pixels16_x2_nornd;
    c->put_no_rnd_pixels_tab[0][2] = bfin_put_pixels16_y2_nornd;
/*     c->put_no_rnd_pixels_tab[0][3] = ff_bfin_put_pixels16_xy2_nornd; */
}
