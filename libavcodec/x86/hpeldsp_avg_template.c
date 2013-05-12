/*
 * DSP utils : average functions are compiled twice for 3dnow/mmxext
 * Copyright (c) 2000, 2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer
 *
 * MMX optimization by Nick Kurshev <nickols_k@mail.ru>
 * mostly rewritten by Michael Niedermayer <michaelni@gmx.at>
 * and improved by Zdenek Kabelac <kabi@users.sf.net>
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

//FIXME the following could be optimized too ...
static void DEF(put_no_rnd_pixels16_x2)(uint8_t *block,
                                        const uint8_t *pixels,
                                        ptrdiff_t line_size, int h)
{
    DEF(ff_put_no_rnd_pixels8_x2)(block,     pixels,     line_size, h);
    DEF(ff_put_no_rnd_pixels8_x2)(block + 8, pixels + 8, line_size, h);
}

static void DEF(put_pixels16_y2)(uint8_t *block, const uint8_t *pixels,
                                 ptrdiff_t line_size, int h)
{
    DEF(ff_put_pixels8_y2)(block,     pixels,     line_size, h);
    DEF(ff_put_pixels8_y2)(block + 8, pixels + 8, line_size, h);
}

static void DEF(put_no_rnd_pixels16_y2)(uint8_t *block,
                                        const uint8_t *pixels,
                                        ptrdiff_t line_size, int h)
{
    DEF(ff_put_no_rnd_pixels8_y2)(block,     pixels,     line_size, h);
    DEF(ff_put_no_rnd_pixels8_y2)(block + 8, pixels + 8, line_size, h);
}

static void DEF(avg_pixels16)(uint8_t *block, const uint8_t *pixels,
                              ptrdiff_t line_size, int h)
{
    DEF(ff_avg_pixels8)(block,     pixels,     line_size, h);
    DEF(ff_avg_pixels8)(block + 8, pixels + 8, line_size, h);
}

static void DEF(avg_pixels16_x2)(uint8_t *block, const uint8_t *pixels,
                                 ptrdiff_t line_size, int h)
{
    DEF(ff_avg_pixels8_x2)(block,     pixels,     line_size, h);
    DEF(ff_avg_pixels8_x2)(block + 8, pixels + 8, line_size, h);
}

static void DEF(avg_pixels16_y2)(uint8_t *block, const uint8_t *pixels,
                                 ptrdiff_t line_size, int h)
{
    DEF(ff_avg_pixels8_y2)(block,     pixels,     line_size, h);
    DEF(ff_avg_pixels8_y2)(block + 8, pixels + 8, line_size, h);
}

static void DEF(avg_pixels16_xy2)(uint8_t *block, const uint8_t *pixels,
                                  ptrdiff_t line_size, int h)
{
    DEF(ff_avg_pixels8_xy2)(block,     pixels,     line_size, h);
    DEF(ff_avg_pixels8_xy2)(block + 8, pixels + 8, line_size, h);
}
