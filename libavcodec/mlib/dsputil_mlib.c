/*
 * Sun mediaLib optimized DSP utils
 * Copyright (c) 2001 Fabrice Bellard.
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

#include "../dsputil.h"

#include <mlib_types.h>
#include <mlib_status.h>
#include <mlib_sys.h>
#include <mlib_video.h>


static void put_pixels_mlib (uint8_t * dest, const uint8_t * ref,
			     int stride, int height)
{
    assert(height == 16 || height == 8);
    if (height == 16)
	mlib_VideoCopyRef_U8_U8_8x16(dest, (uint8_t *)ref, stride);
    else
	mlib_VideoCopyRef_U8_U8_8x8 (dest, (uint8_t *)ref, stride);
}

static void put_pixels_x2_mlib (uint8_t * dest, const uint8_t * ref,
				int stride, int height)
{
    assert(height == 16 || height == 8);
    if (height == 16)
	mlib_VideoInterpX_U8_U8_8x16(dest, (uint8_t *)ref, stride, stride);
    else
	mlib_VideoInterpX_U8_U8_8x8 (dest, (uint8_t *)ref, stride, stride);
}

static void put_pixels_y2_mlib (uint8_t * dest, const uint8_t * ref,
				int stride, int height)
{
    assert(height == 16 || height == 8);
    if (height == 16)
	mlib_VideoInterpY_U8_U8_8x16(dest, (uint8_t *)ref, stride, stride);
    else
	mlib_VideoInterpY_U8_U8_8x8 (dest, (uint8_t *)ref, stride, stride);
}

static void put_pixels_xy2_mlib(uint8_t * dest, const uint8_t * ref,
				int stride, int height)
{
    assert(height == 16 || height == 8);
    if (height == 16) 
	mlib_VideoInterpXY_U8_U8_8x16(dest, (uint8_t *)ref, stride, stride);
    else
	mlib_VideoInterpXY_U8_U8_8x8 (dest, (uint8_t *)ref, stride, stride);
}

static void avg_pixels_mlib (uint8_t * dest, const uint8_t * ref,
			     int stride, int height)
{
    assert(height == 16 || height == 8);
    if (height == 16)
	mlib_VideoCopyRefAve_U8_U8_8x16(dest, (uint8_t *)ref, stride);
    else
	mlib_VideoCopyRefAve_U8_U8_8x8 (dest, (uint8_t *)ref, stride);
}

static void avg_pixels_x2_mlib (uint8_t * dest, const uint8_t * ref,
				int stride, int height)
{
    assert(height == 16 || height == 8);
    if (height == 16)
	mlib_VideoInterpAveX_U8_U8_8x16(dest, (uint8_t *)ref, stride, stride);
    else
	mlib_VideoInterpAveX_U8_U8_8x8 (dest, (uint8_t *)ref, stride, stride);
}

static void avg_pixels_y2_mlib (uint8_t * dest, const uint8_t * ref,
				int stride, int height)
{
    assert(height == 16 || height == 8);
    if (height == 16)
	mlib_VideoInterpAveY_U8_U8_8x16(dest, (uint8_t *)ref, stride, stride);
    else
	mlib_VideoInterpAveY_U8_U8_8x8 (dest, (uint8_t *)ref, stride, stride);
}

static void avg_pixels_xy2_mlib (uint8_t * dest, const uint8_t * ref,
				 int stride, int height)
{
    assert(height == 16 || height == 8);
    if (height == 16)
	mlib_VideoInterpAveXY_U8_U8_8x16(dest, (uint8_t *)ref, stride, stride);
    else
	mlib_VideoInterpAveXY_U8_U8_8x8 (dest, (uint8_t *)ref, stride, stride);
}


static void add_pixels_clamped_mlib(const DCTELEM *block, UINT8 *pixels, int line_size)
{
    mlib_VideoAddBlock_U8_S16(pixels, (mlib_s16 *)block, line_size);
}


void ff_idct_mlib(DCTELEM *data)
{
    mlib_VideoIDCT8x8_S16_S16 (data, data);
}


void ff_fdct_mlib(DCTELEM *data)
{
    mlib_VideoDCT8x8_S16_S16 (data, data);
}

void dsputil_init_mlib(void)
{
    av_fdct = ff_fdct_mlib;
    ff_idct = ff_idct_mlib;

    put_pixels_tab[0] = put_pixels_mlib;
    put_pixels_tab[1] = put_pixels_x2_mlib;
    put_pixels_tab[2] = put_pixels_y2_mlib;
    put_pixels_tab[3] = put_pixels_xy2_mlib;

    avg_pixels_tab[0] = avg_pixels_mlib;
    avg_pixels_tab[1] = avg_pixels_x2_mlib;
    avg_pixels_tab[2] = avg_pixels_y2_mlib;
    avg_pixels_tab[3] = avg_pixels_xy2_mlib;
    
    put_no_rnd_pixels_tab[0] = put_pixels_mlib;
    
    add_pixels_clamped = add_pixels_clamped_mlib;
}
