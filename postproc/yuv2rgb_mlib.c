/* 
 * yuv2rgb_mlib.c, Software YUV to RGB coverter using mediaLib
 *
 *  Copyright (C) 2000, Håkan Hjort <d95hjort@dtek.chalmers.se>
 *  All Rights Reserved.
 *
 *  This file is part of mpeg2dec, a free MPEG-2 video decoder
 *
 *  mpeg2dec is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  mpeg2dec is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GNU Make; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <mlib_types.h>
#include <mlib_status.h>
#include <mlib_sys.h>
#include <mlib_video.h>

static void mlib_YUV2ARGB420_32(uint8_t* image, uint8_t* py, 
			 uint8_t* pu, uint8_t* pv, 
			 unsigned h_size, unsigned v_size, 
			 unsigned rgb_stride, unsigned y_stride, unsigned uv_stride)
{
  mlib_VideoColorYUV2ARGB420(image, py, pu, pv, h_size,
			     v_size, rgb_stride, y_stride, uv_stride);
}

static void mlib_YUV2ABGR420_32(uint8_t* image, uint8_t* py, 
			 uint8_t* pu, uint8_t* pv, 
			 unsigned h_size, unsigned v_size, 
			 unsigned rgb_stride, unsigned y_stride, unsigned uv_stride)
{
  mlib_VideoColorYUV2ABGR420(image, py, pu, pv, h_size,
			     v_size, rgb_stride, y_stride, uv_stride);
}

static void mlib_YUV2RGB420_24(uint8_t* image, uint8_t* py, 
			 uint8_t* pu, uint8_t* pv, 
			 unsigned h_size, unsigned v_size, 
			 unsigned rgb_stride, unsigned y_stride, unsigned uv_stride)
{
  mlib_VideoColorYUV2RGB420(image, py, pu, pv, h_size,
			    v_size, rgb_stride, y_stride, uv_stride);
}


yuv2rgb_fun yuv2rgb_init_mlib(unsigned bpp, int mode) 
{  

	if( bpp == 24 ) 
	{
		if( mode == MODE_RGB )
			return mlib_YUV2RGB420_24;
  }

	if( bpp == 32 ) 
	{
		if( mode == MODE_RGB )
			return mlib_YUV2ARGB420_32;
		else if( mode == MODE_BGR )
			return mlib_YUV2ABGR420_32;
	}
  
	return NULL;
}

