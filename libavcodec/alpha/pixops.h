/*
 * Alpha optimized DSP utils
 * Copyright (c) 2002 Falk Hueffner <falk@debian.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* This file is intended to be #included with proper definitions of
 * PIXOPNAME, BTYPE, AVG2, AVG4 and STORE.  */

static void PIXOPNAME(_pixels_axp)(BTYPE *block, const UINT8 *pixels,
				   int line_size, int h)
{
    if ((size_t) pixels & 0x7) {
	do {
	    STORE(uldq(pixels), block);
	    pixels += line_size;
	    block  += line_size;
	} while (--h);
    } else {
	do {
	    STORE(ldq(pixels), block);
	    pixels += line_size;
	    block  += line_size;
	} while (--h);
    }
}

static void PIXOPNAME(_pixels_x2_axp)(BTYPE *block, const UINT8 *pixels,
				      int line_size, int h)
{
    if ((size_t) pixels & 0x7) {
	do {
	    UINT64 pix1, pix2;

	    pix1 = uldq(pixels);
	    pix2 = pix1 >> 8 | ((UINT64) pixels[8] << 56);
	    STORE(AVG2(pix1, pix2), block);
	    pixels += line_size;
	    block += line_size;
	} while (--h);
    } else {
	do {
	    UINT64 pix1, pix2;

	    pix1 = ldq(pixels);
	    pix2 = pix1 >> 8 | ((UINT64) pixels[8] << 56);
	    STORE(AVG2(pix1, pix2), block);
	    pixels += line_size;
	    block += line_size;
	} while (--h);
    }
}

static void PIXOPNAME(_pixels_y2_axp)(BTYPE *block, const UINT8 *pixels,
				      int line_size, int h)
{
    if ((size_t) pixels & 0x7) {
	UINT64 pix = uldq(pixels);
	do {
	    UINT64 next_pix;

	    pixels += line_size;
	    next_pix = uldq(pixels);
	    STORE(AVG2(pix, next_pix), block);
	    block += line_size;
	    pix = next_pix;
	} while (--h);
    } else {
	UINT64 pix = ldq(pixels);
	do {
	    UINT64 next_pix;

	    pixels += line_size;
	    next_pix = ldq(pixels);
	    STORE(AVG2(pix, next_pix), block);
	    block += line_size;
	    pix = next_pix;
	} while (--h);
    }
}

/* This could be further sped up by recycling AVG4 intermediate
  results from the previous loop pass.  */
static void PIXOPNAME(_pixels_xy2_axp)(BTYPE *block, const UINT8 *pixels,
				       int line_size, int h)
{
    if ((size_t) pixels & 0x7) {
	UINT64 pix1 = uldq(pixels);
	UINT64 pix2 = pix1 >> 8 | ((UINT64) pixels[8] << 56);

	do {
	    UINT64 next_pix1, next_pix2;

	    pixels += line_size;
	    next_pix1 = uldq(pixels);
	    next_pix2 = next_pix1 >> 8 | ((UINT64) pixels[8] << 56);

	    STORE(AVG4(pix1, pix2, next_pix1, next_pix2), block);

	    block += line_size;
	    pix1 = next_pix1;
	    pix2 = next_pix2;
	} while (--h);
    } else {
	UINT64 pix1 = ldq(pixels);
	UINT64 pix2 = pix1 >> 8 | ((UINT64) pixels[8] << 56);

	do {
	    UINT64 next_pix1, next_pix2;

	    pixels += line_size;
	    next_pix1 = ldq(pixels);
	    next_pix2 = next_pix1 >> 8 | ((UINT64) pixels[8] << 56);

	    STORE(AVG4(pix1, pix2, next_pix1, next_pix2), block);

	    block += line_size;
	    pix1 = next_pix1;
	    pix2 = next_pix2;
	} while (--h);
    }
}
