/*
 * BlackFin halfpel functions
 *
 * Copyright (C) 2007 Marc Hoffman <marc.hoffman@analog.com>
 * Copyright (c) 2006 Michael Benjamin <michael.benjamin@analog.com>
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

#include <stddef.h>
#include <stdint.h>

#include "libavutil/attributes.h"
#include "libavutil/bfin/attributes.h"
#include "libavcodec/hpeldsp.h"
#include "pixels.h"

void ff_bfin_put_pixels8uc_no_rnd(uint8_t *block, const uint8_t *s0,
                                  const uint8_t *s1, int line_size,
                                  int h) attribute_l1_text;
void ff_bfin_put_pixels16uc_no_rnd(uint8_t *block, const uint8_t *s0,
                                   const uint8_t *s1, int line_size,
                                   int h) attribute_l1_text;

static void bfin_put_pixels8(uint8_t *block, const uint8_t *pixels,
                             ptrdiff_t line_size, int h)
{
    ff_bfin_put_pixels8uc(block, pixels, pixels, line_size, line_size, h);
}

static void bfin_put_pixels8_x2(uint8_t *block, const uint8_t *pixels,
                                ptrdiff_t line_size, int h)
{
    ff_bfin_put_pixels8uc(block, pixels, pixels + 1, line_size, line_size, h);
}

static void bfin_put_pixels8_y2(uint8_t *block, const uint8_t *pixels,
                                ptrdiff_t line_size, int h)
{
    ff_bfin_put_pixels8uc(block, pixels, pixels + line_size,
                          line_size, line_size, h);
}

static void bfin_put_pixels8_xy2(uint8_t *block, const uint8_t *s0,
                                 ptrdiff_t line_size, int h)
{
    ff_bfin_z_put_pixels8_xy2(block, s0, line_size, line_size, h);
}

static void bfin_put_pixels16(uint8_t *block, const uint8_t *pixels,
                              ptrdiff_t line_size, int h)
{
    ff_bfin_put_pixels16uc(block, pixels, pixels, line_size, line_size, h);
}

static void bfin_put_pixels16_x2(uint8_t *block, const uint8_t *pixels,
                                 ptrdiff_t line_size, int h)
{
    ff_bfin_put_pixels16uc(block, pixels, pixels + 1, line_size, line_size, h);
}

static void bfin_put_pixels16_y2(uint8_t *block, const uint8_t *pixels,
                                 ptrdiff_t line_size, int h)
{
    ff_bfin_put_pixels16uc(block, pixels, pixels + line_size,
                           line_size, line_size, h);
}

static void bfin_put_pixels16_xy2(uint8_t *block, const uint8_t *s0,
                                  ptrdiff_t line_size, int h)
{
    ff_bfin_z_put_pixels16_xy2(block, s0, line_size, line_size, h);
}

static void bfin_put_pixels8_no_rnd(uint8_t *block, const uint8_t *pixels,
                                    ptrdiff_t line_size, int h)
{
    ff_bfin_put_pixels8uc_no_rnd(block, pixels, pixels, line_size, h);
}

static void bfin_put_pixels8_x2_no_rnd(uint8_t *block, const uint8_t *pixels,
                                       ptrdiff_t line_size, int h)
{
    ff_bfin_put_pixels8uc_no_rnd(block, pixels, pixels + 1, line_size, h);
}

static void bfin_put_pixels8_y2_no_rnd(uint8_t *block, const uint8_t *pixels,
                                       ptrdiff_t line_size, int h)
{
    ff_bfin_put_pixels8uc_no_rnd(block, pixels, pixels + line_size,
                                 line_size, h);
}

static void bfin_put_pixels16_no_rnd(uint8_t *block, const uint8_t *pixels,
                                     ptrdiff_t line_size, int h)
{
    ff_bfin_put_pixels16uc_no_rnd(block, pixels, pixels, line_size, h);
}

static void bfin_put_pixels16_x2_no_rnd(uint8_t *block, const uint8_t *pixels,
                                        ptrdiff_t line_size, int h)
{
    ff_bfin_put_pixels16uc_no_rnd(block, pixels, pixels + 1, line_size, h);
}

static void bfin_put_pixels16_y2_no_rnd(uint8_t *block, const uint8_t *pixels,
                                        ptrdiff_t line_size, int h)
{
    ff_bfin_put_pixels16uc_no_rnd(block, pixels, pixels + line_size,
                                  line_size, h);
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

    c->put_no_rnd_pixels_tab[1][0] = bfin_put_pixels8_no_rnd;
    c->put_no_rnd_pixels_tab[1][1] = bfin_put_pixels8_x2_no_rnd;
    c->put_no_rnd_pixels_tab[1][2] = bfin_put_pixels8_y2_no_rnd;

    c->put_no_rnd_pixels_tab[0][0] = bfin_put_pixels16_no_rnd;
    c->put_no_rnd_pixels_tab[0][1] = bfin_put_pixels16_x2_no_rnd;
    c->put_no_rnd_pixels_tab[0][2] = bfin_put_pixels16_y2_no_rnd;
}
