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
#include "../mpegvideo.h"

#include <mlib_types.h>
#include <mlib_status.h>
#include <mlib_sys.h>
#include <mlib_video.h>


/* copy block, width 16 pixel, height 8/16 */

static void put_pixels16_mlib (uint8_t * dest, const uint8_t * ref,
			       int stride, int height)
{
    assert(height == 16 || height == 8);
    if (height == 16)
	mlib_VideoCopyRef_U8_U8_16x16(dest, (uint8_t *)ref, stride);
    else
	mlib_VideoCopyRef_U8_U8_16x8 (dest, (uint8_t *)ref, stride);
}

static void put_pixels16_x2_mlib (uint8_t * dest, const uint8_t * ref,
				  int stride, int height)
{
    assert(height == 16 || height == 8);
    if (height == 16)
	mlib_VideoInterpX_U8_U8_16x16(dest, (uint8_t *)ref, stride, stride);
    else
	mlib_VideoInterpX_U8_U8_16x8 (dest, (uint8_t *)ref, stride, stride);
}

static void put_pixels16_y2_mlib (uint8_t * dest, const uint8_t * ref,
				  int stride, int height)
{
    assert(height == 16 || height == 8);
    if (height == 16)
	mlib_VideoInterpY_U8_U8_16x16(dest, (uint8_t *)ref, stride, stride);
    else
	mlib_VideoInterpY_U8_U8_16x8 (dest, (uint8_t *)ref, stride, stride);
}

static void put_pixels16_xy2_mlib(uint8_t * dest, const uint8_t * ref,
				  int stride, int height)
{
    assert(height == 16 || height == 8);
    if (height == 16) 
	mlib_VideoInterpXY_U8_U8_16x16(dest, (uint8_t *)ref, stride, stride);
    else
	mlib_VideoInterpXY_U8_U8_16x8 (dest, (uint8_t *)ref, stride, stride);
}


/* copy block, width 8 pixel, height 8/16 */

static void put_pixels8_mlib (uint8_t * dest, const uint8_t * ref,
			      int stride, int height)
{
    assert(height == 16 || height == 8);
    if (height == 16)
	mlib_VideoCopyRef_U8_U8_8x16(dest, (uint8_t *)ref, stride);
    else
	mlib_VideoCopyRef_U8_U8_8x8 (dest, (uint8_t *)ref, stride);
}

static void put_pixels8_x2_mlib (uint8_t * dest, const uint8_t * ref,
				 int stride, int height)
{
    assert(height == 16 || height == 8);
    if (height == 16)
	mlib_VideoInterpX_U8_U8_8x16(dest, (uint8_t *)ref, stride, stride);
    else
	mlib_VideoInterpX_U8_U8_8x8 (dest, (uint8_t *)ref, stride, stride);
}

static void put_pixels8_y2_mlib (uint8_t * dest, const uint8_t * ref,
				 int stride, int height)
{
    assert(height == 16 || height == 8);
    if (height == 16)
	mlib_VideoInterpY_U8_U8_8x16(dest, (uint8_t *)ref, stride, stride);
    else
	mlib_VideoInterpY_U8_U8_8x8 (dest, (uint8_t *)ref, stride, stride);
}

static void put_pixels8_xy2_mlib(uint8_t * dest, const uint8_t * ref,
				 int stride, int height)
{
    assert(height == 16 || height == 8);
    if (height == 16) 
	mlib_VideoInterpXY_U8_U8_8x16(dest, (uint8_t *)ref, stride, stride);
    else
	mlib_VideoInterpXY_U8_U8_8x8 (dest, (uint8_t *)ref, stride, stride);
}


/* average/merge dest+source block, width 16 pixel, height 8/16 */

static void avg_pixels16_mlib (uint8_t * dest, const uint8_t * ref,
			       int stride, int height)
{
    assert(height == 16 || height == 8);
    if (height == 16)
	mlib_VideoCopyRefAve_U8_U8_16x16(dest, (uint8_t *)ref, stride);
    else
	mlib_VideoCopyRefAve_U8_U8_16x8 (dest, (uint8_t *)ref, stride);
}

static void avg_pixels16_x2_mlib (uint8_t * dest, const uint8_t * ref,
				  int stride, int height)
{
    assert(height == 16 || height == 8);
    if (height == 16)
	mlib_VideoInterpAveX_U8_U8_16x16(dest, (uint8_t *)ref, stride, stride);
    else
	mlib_VideoInterpAveX_U8_U8_16x8 (dest, (uint8_t *)ref, stride, stride);
}

static void avg_pixels16_y2_mlib (uint8_t * dest, const uint8_t * ref,
				  int stride, int height)
{
    assert(height == 16 || height == 8);
    if (height == 16)
	mlib_VideoInterpAveY_U8_U8_16x16(dest, (uint8_t *)ref, stride, stride);
    else
	mlib_VideoInterpAveY_U8_U8_16x8 (dest, (uint8_t *)ref, stride, stride);
}

static void avg_pixels16_xy2_mlib (uint8_t * dest, const uint8_t * ref,
				   int stride, int height)
{
    assert(height == 16 || height == 8);
    if (height == 16)
	mlib_VideoInterpAveXY_U8_U8_16x16(dest, (uint8_t *)ref, stride, stride);
    else
	mlib_VideoInterpAveXY_U8_U8_16x8 (dest, (uint8_t *)ref, stride, stride);
}


/* average/merge dest+source block, width 8 pixel, height 8/16 */

static void avg_pixels8_mlib (uint8_t * dest, const uint8_t * ref,
			      int stride, int height)
{
    assert(height == 16 || height == 8);
    if (height == 16)
	mlib_VideoCopyRefAve_U8_U8_8x16(dest, (uint8_t *)ref, stride);
    else
	mlib_VideoCopyRefAve_U8_U8_8x8 (dest, (uint8_t *)ref, stride);
}

static void avg_pixels8_x2_mlib (uint8_t * dest, const uint8_t * ref,
				 int stride, int height)
{
    assert(height == 16 || height == 8);
    if (height == 16)
	mlib_VideoInterpAveX_U8_U8_8x16(dest, (uint8_t *)ref, stride, stride);
    else
	mlib_VideoInterpAveX_U8_U8_8x8 (dest, (uint8_t *)ref, stride, stride);
}

static void avg_pixels8_y2_mlib (uint8_t * dest, const uint8_t * ref,
				 int stride, int height)
{
    assert(height == 16 || height == 8);
    if (height == 16)
	mlib_VideoInterpAveY_U8_U8_8x16(dest, (uint8_t *)ref, stride, stride);
    else
	mlib_VideoInterpAveY_U8_U8_8x8 (dest, (uint8_t *)ref, stride, stride);
}

static void avg_pixels8_xy2_mlib (uint8_t * dest, const uint8_t * ref,
				  int stride, int height)
{
    assert(height == 16 || height == 8);
    if (height == 16)
	mlib_VideoInterpAveXY_U8_U8_8x16(dest, (uint8_t *)ref, stride, stride);
    else
	mlib_VideoInterpAveXY_U8_U8_8x8 (dest, (uint8_t *)ref, stride, stride);
}


static void (*put_pixels_clamped)(const DCTELEM *block, uint8_t *pixels, int line_size);


static void add_pixels_clamped_mlib(const DCTELEM *block, uint8_t *pixels, int line_size)
{
    mlib_VideoAddBlock_U8_S16(pixels, (mlib_s16 *)block, line_size);
}


/* XXX: those functions should be suppressed ASAP when all IDCTs are
   converted */
static void ff_idct_put_mlib(uint8_t *dest, int line_size, DCTELEM *data)
{
    mlib_VideoIDCT8x8_S16_S16 (data, data);
    put_pixels_clamped(data, dest, line_size);
}

static void ff_idct_add_mlib(uint8_t *dest, int line_size, DCTELEM *data)
{
    mlib_VideoIDCT8x8_S16_S16 (data, data);
    mlib_VideoAddBlock_U8_S16(dest, (mlib_s16 *)data, line_size);
}

static void ff_fdct_mlib(DCTELEM *data)
{
    mlib_VideoDCT8x8_S16_S16 (data, data);
}

void dsputil_init_mlib(DSPContext* c, unsigned mask)
{
    c->put_pixels_tab[0][0] = put_pixels16_mlib;
    c->put_pixels_tab[0][1] = put_pixels16_x2_mlib;
    c->put_pixels_tab[0][2] = put_pixels16_y2_mlib;
    c->put_pixels_tab[0][3] = put_pixels16_xy2_mlib;
    c->put_pixels_tab[1][0] = put_pixels8_mlib;
    c->put_pixels_tab[1][1] = put_pixels8_x2_mlib;
    c->put_pixels_tab[1][2] = put_pixels8_y2_mlib;
    c->put_pixels_tab[1][3] = put_pixels8_xy2_mlib;

    c->avg_pixels_tab[0][0] = avg_pixels16_mlib;
    c->avg_pixels_tab[0][1] = avg_pixels16_x2_mlib;
    c->avg_pixels_tab[0][2] = avg_pixels16_y2_mlib;
    c->avg_pixels_tab[0][3] = avg_pixels16_xy2_mlib;
    c->avg_pixels_tab[1][0] = avg_pixels8_mlib;
    c->avg_pixels_tab[1][1] = avg_pixels8_x2_mlib;
    c->avg_pixels_tab[1][2] = avg_pixels8_y2_mlib;
    c->avg_pixels_tab[1][3] = avg_pixels8_xy2_mlib;

    c->put_no_rnd_pixels_tab[0][0] = put_pixels16_mlib;
    c->put_no_rnd_pixels_tab[1][0] = put_pixels8_mlib;

    c->add_pixels_clamped = add_pixels_clamped_mlib;
    put_pixels_clamped = c->put_pixels_clamped;
}

void MPV_common_init_mlib(MpegEncContext *s)
{
    int i;

    if(s->avctx->dct_algo==FF_DCT_AUTO || s->avctx->dct_algo==FF_DCT_MLIB){
	s->fdct = ff_fdct_mlib;
    }

    if(s->avctx->idct_algo==FF_IDCT_AUTO || s->avctx->idct_algo==FF_IDCT_MLIB){
        s->idct_put= ff_idct_put_mlib;
        s->idct_add= ff_idct_add_mlib;
        s->idct_permutation_type= FF_NO_IDCT_PERM;
    }
}
