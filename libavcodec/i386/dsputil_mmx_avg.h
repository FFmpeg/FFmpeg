/*
 * DSP utils : average functions are compiled twice for 3dnow/mmx2
 * Copyright (c) 2000, 2001 Gerard Lantau.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * MMX optimization by Nick Kurshev <nickols_k@mail.ru>
 */

static void DEF(put_pixels_x2)(UINT8 *block, const UINT8 *pixels, int line_size, int h)
{
  int dh, hh;
  UINT8 *p;
  const UINT8 *pix;
  p = block;
  pix = pixels;
  hh=h>>2;
  dh=h&3;
  while(hh--) {
    __asm __volatile(
	"movq	(%1), %%mm0\n\t"
	"movq	1(%1), %%mm1\n\t"
	"movq	(%1, %2), %%mm2\n\t"
	"movq	1(%1, %2), %%mm3\n\t"
	"movq	(%1, %2, 2), %%mm4\n\t"
	"movq	1(%1, %2, 2), %%mm5\n\t"
	"movq	(%1, %3), %%mm6\n\t"
	"movq	1(%1, %3), %%mm7\n\t"
	PAVGB"  %%mm1, %%mm0\n\t"
	PAVGB"  %%mm3, %%mm2\n\t"
	PAVGB"  %%mm5, %%mm4\n\t"
	PAVGB"  %%mm7, %%mm6\n\t"
	"movq	%%mm0, (%0)\n\t"
	"movq	%%mm2, (%0, %2)\n\t"
	"movq	%%mm4, (%0, %2, 2)\n\t"
	"movq	%%mm6, (%0, %3)\n\t"
	::"r"(p), "r"(pix), "r" (line_size), "r" (line_size*3)
	:"memory");
     pix += line_size*4; p += line_size*4;
  }
  while(dh--) {
    __asm __volatile(
	"movq	%1, %%mm0\n\t"
	"movq	1%1, %%mm1\n\t"
	PAVGB"  %%mm1, %%mm0\n\t"
	"movq	%%mm0, %0\n\t"
	:"=m"(*p)
	:"m"(*pix)
	:"memory");
     pix += line_size; p += line_size;
  }
}

static void DEF(put_pixels_y2)(UINT8 *block, const UINT8 *pixels, int line_size, int h)
{
  int dh, hh;
  UINT8 *p;
  const UINT8 *pix;
  p = block;
  pix = pixels;

  hh=h>>1;
  dh=h&1;
  while(hh--) {
    __asm __volatile(
	"movq	%2, %%mm0\n\t"
	"movq	%3, %%mm1\n\t"
	"movq	%4, %%mm2\n\t"
	PAVGB"  %%mm1, %%mm0\n\t"
	PAVGB"  %%mm2, %%mm1\n\t"
	"movq	%%mm0, %0\n\t"
	"movq	%%mm1, %1\n\t"
	:"=m"(*p), "=m"(*(p+line_size))
	:"m"(*pix), "m"(*(pix+line_size)),
	 "m"(*(pix+line_size*2))
	:"memory");
     pix += line_size*2;
     p += line_size*2;
  }
  if(dh) {
    __asm __volatile(
	"movq	%1, %%mm0\n\t"
	"movq	%2, %%mm1\n\t"
	PAVGB"  %%mm1, %%mm0\n\t"
	"movq	%%mm0, %0\n\t"
	:"=m"(*p)
	:"m"(*pix),
	 "m"(*(pix+line_size))
	:"memory");
  }
}

static void DEF(avg_pixels)(UINT8 *block, const UINT8 *pixels, int line_size, int h)
{
  int dh, hh;
  UINT8 *p;
  const UINT8 *pix;
  p = block;
  pix = pixels;
  hh=h>>2;
  dh=h&3;
  while(hh--) {
    __asm __volatile(
	"movq	(%0), %%mm0\n\t"
	"movq	(%1), %%mm1\n\t"
	"movq	(%0, %2), %%mm2\n\t"
	"movq	(%1, %2), %%mm3\n\t"
	"movq	(%0, %2, 2), %%mm4\n\t"
	"movq	(%1, %2, 2), %%mm5\n\t"
	"movq	(%0, %3), %%mm6\n\t"
	"movq	(%1, %3), %%mm7\n\t"
	PAVGB"  %%mm1, %%mm0\n\t"
	PAVGB"  %%mm3, %%mm2\n\t"
	PAVGB"  %%mm5, %%mm4\n\t"
	PAVGB"  %%mm7, %%mm6\n\t"
	"movq	%%mm0, (%0)\n\t"
	"movq	%%mm2, (%0, %2)\n\t"
	"movq	%%mm4, (%0, %2, 2)\n\t"
	"movq	%%mm6, (%0, %3)\n\t"
	::"r"(p), "r"(pix), "r" (line_size), "r" (line_size*3)
	:"memory");
     pix += line_size*4; p += line_size*4;
  }
  while(dh--) {
    __asm __volatile(
	"movq	%0, %%mm0\n\t"
	"movq	%1, %%mm1\n\t"
	PAVGB"  %%mm1, %%mm0\n\t"
	"movq	%%mm0, %0\n\t"
	:"+m"(*p)
	:"m"(*pix)
	:"memory");
     pix += line_size; p += line_size;
  }
}

static void DEF(avg_pixels_x2)( UINT8  *block, const UINT8 *pixels, int line_size, int h)
{
  int dh, hh;
  UINT8  *p;
  const UINT8 *pix;
  p = block;
  pix = pixels;
  hh=h>>1;
  dh=h&1;
  while(hh--) {
    __asm __volatile(
	"movq	%2, %%mm2\n\t"
	"movq	1%2, %%mm3\n\t"
	"movq	%3, %%mm4\n\t"
	"movq	1%3, %%mm5\n\t"
	"movq	%0, %%mm0\n\t"
	"movq	%1, %%mm1\n\t"
	PAVGB"	%%mm3, %%mm2\n\t"
	PAVGB"	%%mm2, %%mm0\n\t"
	PAVGB"	%%mm5, %%mm4\n\t"
	PAVGB"	%%mm4, %%mm1\n\t"
	"movq	%%mm0, %0\n\t"
	"movq	%%mm1, %1\n\t"
	:"+m"(*p), "+m"(*(p+line_size))
	:"m"(*pix), "m"(*(pix+line_size))
	:"memory");
   pix += line_size*2;
   p +=   line_size*2;
  }
  if(dh) {
    __asm __volatile(
	"movq	%1, %%mm1\n\t"
	"movq	1%1, %%mm2\n\t"
	"movq	%0, %%mm0\n\t"
	PAVGB"	%%mm2, %%mm1\n\t"
	PAVGB"	%%mm1, %%mm0\n\t"
	"movq	%%mm0, %0\n\t"
	:"+m"(*p)
	:"m"(*pix)
	:"memory");
  }
}

static void  DEF(avg_pixels_y2)( UINT8  *block, const UINT8 *pixels, int line_size, int h)
{
  int dh, hh;
  UINT8  *p;
  const UINT8 *pix;
  p = block;
  pix = pixels;
  hh=h>>1;
  dh=h&1;
  while(hh--) {
    __asm __volatile(
	"movq	%2, %%mm2\n\t"
	"movq	%3, %%mm3\n\t"
	"movq	%3, %%mm4\n\t"
	"movq	%4, %%mm5\n\t"
	"movq	%0, %%mm0\n\t"
	"movq	%1, %%mm1\n\t"
	PAVGB"	%%mm3, %%mm2\n\t"
	PAVGB"	%%mm2, %%mm0\n\t"
	PAVGB"	%%mm5, %%mm4\n\t"
	PAVGB"	%%mm4, %%mm1\n\t"
	"movq	%%mm0, %0\n\t"
	"movq	%%mm1, %1\n\t"
	:"+m"(*p), "+m"(*(p+line_size))
	:"m"(*pix), "m"(*(pix+line_size)), "m"(*(pix+line_size*2))
	:"memory");
   pix += line_size*2;
   p +=   line_size*2;
  }
  if(dh) {
    __asm __volatile(
	"movq	%1, %%mm1\n\t"
	"movq	%2, %%mm2\n\t"
	"movq	%0, %%mm0\n\t"
	PAVGB"	%%mm2, %%mm1\n\t"
	PAVGB"	%%mm1, %%mm0\n\t"
	"movq	%%mm0, %0\n\t"
	:"+m"(*p)
	:"m"(*pix), "m"(*(pix+line_size))
	:"memory");
  }
}

static void DEF(avg_pixels_xy2)( UINT8  *block, const UINT8 *pixels, int line_size, int h)
{
  UINT8  *p;
  const UINT8 *pix;
  p = block;
  pix = pixels;
  __asm __volatile(
	"pxor	%%mm7, %%mm7\n\t"
	"movq	%0, %%mm6\n\t"
	::"m"(mm_wtwo));
  do {
    __asm __volatile(
	"movq	%1, %%mm0\n\t"
	"movq	%2, %%mm1\n\t"
	"movq	1%1, %%mm4\n\t"
	"movq	1%2, %%mm5\n\t"
	"movq	%%mm0, %%mm2\n\t"
	"movq	%%mm1, %%mm3\n\t"
	"punpcklbw %%mm7, %%mm0\n\t"
	"punpcklbw %%mm7, %%mm1\n\t"
	"punpckhbw %%mm7, %%mm2\n\t"
	"punpckhbw %%mm7, %%mm3\n\t"
	"paddusw %%mm1, %%mm0\n\t"
	"paddusw %%mm3, %%mm2\n\t"
	"movq	%%mm4, %%mm1\n\t"
	"movq	%%mm5, %%mm3\n\t"
	"punpcklbw %%mm7, %%mm4\n\t"
	"punpcklbw %%mm7, %%mm5\n\t"
	"punpckhbw %%mm7, %%mm1\n\t"
	"punpckhbw %%mm7, %%mm3\n\t"
	"paddusw %%mm5, %%mm4\n\t"
	"paddusw %%mm3, %%mm1\n\t"
	"paddusw %%mm6, %%mm4\n\t"
	"paddusw %%mm6, %%mm1\n\t"
	"paddusw %%mm4, %%mm0\n\t"
	"paddusw %%mm1, %%mm2\n\t"
	"psrlw	$2, %%mm0\n\t"
	"psrlw	$2, %%mm2\n\t"
	"packuswb  %%mm2, %%mm0\n\t"
	PAVGB"	%0, %%mm0\n\t"
	"movq	%%mm0, %0\n\t"
	:"+m"(*p)
	:"m"(*pix),
	 "m"(*(pix+line_size))
	:"memory");
   pix += line_size;
   p +=   line_size ;
  } while(--h);
}

static void DEF(sub_pixels_x2)( DCTELEM  *block, const UINT8 *pixels, int line_size, int h)
{
  DCTELEM  *p;
  const UINT8 *pix;
  p = block;
  pix = pixels;
  __asm __volatile(
      "pxor	%%mm7, %%mm7":);
  do {
    __asm __volatile(
	"movq	1%1, %%mm2\n\t"
	"movq	%0, %%mm0\n\t"
	PAVGB"	%1, %%mm2\n\t"
	"movq	8%0, %%mm1\n\t"
	"movq	%%mm2, %%mm3\n\t"
	"punpcklbw %%mm7, %%mm2\n\t"
	"punpckhbw %%mm7, %%mm3\n\t"
	"psubsw %%mm2, %%mm0\n\t"
	"psubsw %%mm3, %%mm1\n\t"
	"movq	%%mm0, %0\n\t"
	"movq	%%mm1, 8%0\n\t"
	:"+m"(*p)
	:"m"(*pix)
	:"memory");
   pix += line_size;
   p +=   8;
 } while (--h);
}

static void DEF(sub_pixels_y2)( DCTELEM  *block, const UINT8 *pixels, int line_size, int h)
{
  DCTELEM  *p;
  const UINT8 *pix;
  p = block;
  pix = pixels;
  __asm __volatile(
      "pxor	%%mm7, %%mm7":);
  do {
    __asm __volatile(
	"movq	%2, %%mm2\n\t"
	"movq	%0, %%mm0\n\t"
	PAVGB"	%1, %%mm2\n\t"
	"movq	8%0, %%mm1\n\t"
	"movq	%%mm2, %%mm3\n\t"
	"punpcklbw %%mm7, %%mm2\n\t"
	"punpckhbw %%mm7, %%mm3\n\t"
	"psubsw %%mm2, %%mm0\n\t"
	"psubsw %%mm3, %%mm1\n\t"
	"movq	%%mm0, %0\n\t"
	"movq	%%mm1, 8%0\n\t"
	:"+m"(*p)
	:"m"(*pix), "m"(*(pix+line_size))
	:"memory");
   pix += line_size;
   p +=   8;
 } while (--h);
}

