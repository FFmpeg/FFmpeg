/*
 * MMX optimized DSP utils
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

#include "../dsputil.h"
#include "../simple_idct.h"

int mm_flags; /* multimedia extension flags */

int pix_abs16x16_mmx(UINT8 *blk1, UINT8 *blk2, int lx, int h);
int pix_abs16x16_sse(UINT8 *blk1, UINT8 *blk2, int lx, int h);
int pix_abs16x16_x2_mmx(UINT8 *blk1, UINT8 *blk2, int lx, int h);
int pix_abs16x16_y2_mmx(UINT8 *blk1, UINT8 *blk2, int lx, int h);
int pix_abs16x16_xy2_mmx(UINT8 *blk1, UINT8 *blk2, int lx, int h);

/* external functions, from idct_mmx.c */
void ff_mmx_idct(DCTELEM *block);
void ff_mmxext_idct(DCTELEM *block);

/* pixel operations */
static const unsigned long long int mm_wone __attribute__ ((aligned(8))) = 0x0001000100010001;
static const unsigned long long int mm_wtwo __attribute__ ((aligned(8))) = 0x0002000200020002;
//static const unsigned short mm_wone[4] __attribute__ ((aligned(8))) = { 0x1, 0x1, 0x1, 0x1 };
//static const unsigned short mm_wtwo[4] __attribute__ ((aligned(8))) = { 0x2, 0x2, 0x2, 0x2 };

/***********************************/
/* 3Dnow specific */

#define DEF(x) x ## _3dnow
/* for Athlons PAVGUSB is prefered */
#define PAVGB "pavgusb"

#include "dsputil_mmx_avg.h"

#undef DEF
#undef PAVGB

/***********************************/
/* MMX2 specific */

#define DEF(x) x ## _sse

/* Introduced only in MMX2 set */
#define PAVGB "pavgb"

#include "dsputil_mmx_avg.h"

#undef DEF
#undef PAVGB

/***********************************/
/* standard MMX */

static void get_pixels_mmx(DCTELEM *block, const UINT8 *pixels, int line_size)
{
    DCTELEM *p;
    const UINT8 *pix;
    int i;

    /* read the pixels */
    p = block;
    pix = pixels;
    __asm __volatile("pxor %%mm7, %%mm7":);
    for(i=0;i<4;i++) {
	__asm __volatile(
		"movq	%1, %%mm0\n\t"
		"movq	%2, %%mm1\n\t"
		"movq	%%mm0, %%mm2\n\t"
		"movq	%%mm1, %%mm3\n\t"
		"punpcklbw %%mm7, %%mm0\n\t"
		"punpckhbw %%mm7, %%mm2\n\t"
		"punpcklbw %%mm7, %%mm1\n\t"
		"punpckhbw %%mm7, %%mm3\n\t"
		"movq	%%mm0, %0\n\t"
		"movq	%%mm2, 8%0\n\t"
		"movq	%%mm1, 16%0\n\t"
		"movq	%%mm3, 24%0\n\t"
		:"=m"(*p)
		:"m"(*pix), "m"(*(pix+line_size))
		:"memory");
        pix += line_size*2;
        p += 16;
    }
}

static void put_pixels_clamped_mmx(const DCTELEM *block, UINT8 *pixels, int line_size)
{
    const DCTELEM *p;
    UINT8 *pix;
    int i;

    /* read the pixels */
    p = block;
    pix = pixels;
    for(i=0;i<2;i++) {
	__asm __volatile(
		"movq	%3, %%mm0\n\t"
		"movq	8%3, %%mm1\n\t"
		"movq	16%3, %%mm2\n\t"
		"movq	24%3, %%mm3\n\t"
		"movq	32%3, %%mm4\n\t"
		"movq	40%3, %%mm5\n\t"
		"movq	48%3, %%mm6\n\t"
		"movq	56%3, %%mm7\n\t"
		"packuswb %%mm1, %%mm0\n\t"
		"packuswb %%mm3, %%mm2\n\t"
		"packuswb %%mm5, %%mm4\n\t"
		"packuswb %%mm7, %%mm6\n\t"
		"movq	%%mm0, (%0)\n\t"
		"movq	%%mm2, (%0, %1)\n\t"
		"movq	%%mm4, (%0, %1, 2)\n\t"
		"movq	%%mm6, (%0, %2)\n\t"
		::"r" (pix), "r" (line_size), "r" (line_size*3), "m"(*p)
		:"memory");
        pix += line_size*4;
        p += 32;
    }
}

static void add_pixels_clamped_mmx(const DCTELEM *block, UINT8 *pixels, int line_size)
{
    const DCTELEM *p;
    UINT8 *pix;
    int i;

    /* read the pixels */
    p = block;
    pix = pixels;
	__asm __volatile("pxor	%%mm7, %%mm7":);
    for(i=0;i<4;i++) {
	__asm __volatile(
		"movq	%2, %%mm0\n\t"
		"movq	8%2, %%mm1\n\t"
		"movq	16%2, %%mm2\n\t"
		"movq	24%2, %%mm3\n\t"
		"movq	%0, %%mm4\n\t"
		"movq	%1, %%mm6\n\t"
		"movq	%%mm4, %%mm5\n\t"
		"punpcklbw %%mm7, %%mm4\n\t"
		"punpckhbw %%mm7, %%mm5\n\t"
		"paddsw	%%mm4, %%mm0\n\t"
		"paddsw	%%mm5, %%mm1\n\t"
		"movq	%%mm6, %%mm5\n\t"
		"punpcklbw %%mm7, %%mm6\n\t"
		"punpckhbw %%mm7, %%mm5\n\t"
		"paddsw	%%mm6, %%mm2\n\t"
		"paddsw	%%mm5, %%mm3\n\t"
		"packuswb %%mm1, %%mm0\n\t"
		"packuswb %%mm3, %%mm2\n\t"
		"movq	%%mm0, %0\n\t"
		"movq	%%mm2, %1\n\t"
		:"+m"(*pix), "+m"(*(pix+line_size))
		:"m"(*p)
		:"memory");
        pix += line_size*2;
        p += 16;
    }
}

static void put_pixels_mmx(UINT8 *block, const UINT8 *pixels, int line_size, int h)
{
    int dh, hh;
    UINT8 *p;
    const UINT8 *pix;
    p   = block;
    pix = pixels;
    hh=h>>2;
    dh=h&3;
    while(hh--) {
    __asm __volatile(
	"movq	(%1), %%mm0		\n\t"
	"movq	(%1, %2), %%mm1		\n\t"
	"movq	(%1, %2, 2), %%mm2	\n\t"
	"movq	(%1, %3), %%mm3		\n\t"
	"movq	%%mm0, (%0)		\n\t"
	"movq	%%mm1, (%0, %2)		\n\t"
	"movq	%%mm2, (%0, %2, 2)	\n\t"
	"movq	%%mm3, (%0, %3)		\n\t"
	::"r"(p), "r"(pix), "r"(line_size), "r"(line_size*3)
	:"memory");
        pix = pix + line_size*4;
        p =   p   + line_size*4;
    }
    while(dh--) {
     __asm __volatile(
	"movq	%1, %%mm0\n\t"
	"movq	%%mm0, %0\n\t"
	:"=m"(*p)
	:"m"(*pix)
	:"memory");
        pix = pix + line_size;
        p =   p   + line_size;
    }
}

static void put_pixels_x2_mmx(UINT8 *block, const UINT8 *pixels, int line_size, int h)
{
  UINT8 *p;
  const UINT8 *pix;
  p = block;
  pix = pixels;
  __asm __volatile(
	"pxor	%%mm7, %%mm7\n\t"
	"movq	%0, %%mm4\n\t"
	::"m"(mm_wone));
  do {
    __asm __volatile(
	"movq	%1, %%mm0\n\t"
	"movq	1%1, %%mm1\n\t"
	"movq	%%mm0, %%mm2\n\t"
	"movq	%%mm1, %%mm3\n\t"
	"punpcklbw %%mm7, %%mm0\n\t"
	"punpcklbw %%mm7, %%mm1\n\t"
	"punpckhbw %%mm7, %%mm2\n\t"
	"punpckhbw %%mm7, %%mm3\n\t"
	"paddusw %%mm1, %%mm0\n\t"
	"paddusw %%mm3, %%mm2\n\t"
	"paddusw %%mm4, %%mm0\n\t"
	"paddusw %%mm4, %%mm2\n\t"
	"psrlw	$1, %%mm0\n\t"
	"psrlw	$1, %%mm2\n\t"
	"packuswb  %%mm2, %%mm0\n\t"
	"movq	%%mm0, %0\n\t"
	:"=m"(*p)
	:"m"(*pix)
	:"memory");
   pix += line_size; p += line_size;
  } while (--h);
}

static void put_pixels_y2_mmx(UINT8 *block, const UINT8 *pixels, int line_size, int h)
{
  UINT8 *p;
  const UINT8 *pix;
  p = block;
  pix = pixels;
  __asm __volatile(
	"pxor	%%mm7, %%mm7\n\t"
	"movq	%0, %%mm4\n\t"
	::"m"(mm_wone));
  do {
    __asm __volatile(
	"movq	%1, %%mm0\n\t"
	"movq	%2, %%mm1\n\t"
	"movq	%%mm0, %%mm2\n\t"
	"movq	%%mm1, %%mm3\n\t"
	"punpcklbw %%mm7, %%mm0\n\t"
	"punpcklbw %%mm7, %%mm1\n\t"
	"punpckhbw %%mm7, %%mm2\n\t"
	"punpckhbw %%mm7, %%mm3\n\t"
	"paddusw %%mm1, %%mm0\n\t"
	"paddusw %%mm3, %%mm2\n\t"
	"paddusw %%mm4, %%mm0\n\t"
	"paddusw %%mm4, %%mm2\n\t"
	"psrlw	$1, %%mm0\n\t"
	"psrlw	$1, %%mm2\n\t"
	"packuswb  %%mm2, %%mm0\n\t"
	"movq	%%mm0, %0\n\t"
	:"=m"(*p)
	:"m"(*pix),
	 "m"(*(pix+line_size))
	:"memory");
   pix += line_size;
   p += line_size;
  } while (--h);
}

static void put_pixels_xy2_mmx(UINT8 *block, const UINT8 *pixels, int line_size, int h)
{
  UINT8 *p;
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
	"movq	%%mm0, %0\n\t"
	:"=m"(*p)
	:"m"(*pix),
	 "m"(*(pix+line_size))
	:"memory");
   pix += line_size;
   p += line_size;
  } while(--h);
}

static void   put_no_rnd_pixels_x2_mmx( UINT8  *block, const UINT8 *pixels, int line_size, int h)
{
  UINT8  *p;
  const UINT8 *pix;
  p = block;
  pix = pixels;
  __asm __volatile("pxor %%mm7, %%mm7\n\t":);
  do {
    __asm __volatile(
	"movq	%1, %%mm0\n\t"
	"movq	1%1, %%mm1\n\t"
	"movq	%%mm0, %%mm2\n\t"
	"movq	%%mm1, %%mm3\n\t"
	"punpcklbw %%mm7, %%mm0\n\t"
	"punpcklbw %%mm7, %%mm1\n\t"
	"punpckhbw %%mm7, %%mm2\n\t"
	"punpckhbw %%mm7, %%mm3\n\t"
	"paddusw %%mm1, %%mm0\n\t"
	"paddusw %%mm3, %%mm2\n\t"
	"psrlw	$1, %%mm0\n\t"
	"psrlw	$1, %%mm2\n\t"
	"packuswb  %%mm2, %%mm0\n\t"
	"movq	%%mm0, %0\n\t"
	:"=m"(*p)
	:"m"(*pix)
	:"memory");
   pix += line_size;
   p +=   line_size;
  } while (--h);
}

static void put_no_rnd_pixels_y2_mmx( UINT8  *block, const UINT8 *pixels, int line_size, int h)
{
  UINT8  *p;
  const UINT8 *pix;
  p = block;
  pix = pixels;
  __asm __volatile("pxor %%mm7, %%mm7\n\t":);
  do {
    __asm __volatile(
	"movq	%1, %%mm0\n\t"
	"movq	%2, %%mm1\n\t"
	"movq	%%mm0, %%mm2\n\t"
	"movq	%%mm1, %%mm3\n\t"
	"punpcklbw %%mm7, %%mm0\n\t"
	"punpcklbw %%mm7, %%mm1\n\t"
	"punpckhbw %%mm7, %%mm2\n\t"
	"punpckhbw %%mm7, %%mm3\n\t"
	"paddusw %%mm1, %%mm0\n\t"
	"paddusw %%mm3, %%mm2\n\t"
	"psrlw	$1, %%mm0\n\t"
	"psrlw	$1, %%mm2\n\t"
	"packuswb  %%mm2, %%mm0\n\t"
	"movq	%%mm0, %0\n\t"
	:"=m"(*p)
	:"m"(*pix),
	 "m"(*(pix+line_size))
	:"memory");
   pix += line_size;
   p +=   line_size;
  } while(--h);
}

static void   put_no_rnd_pixels_xy2_mmx( UINT8  *block, const UINT8 *pixels, int line_size, int h)
{
  UINT8  *p;
  const UINT8 *pix;
  p = block;
  pix = pixels;
  __asm __volatile(
	"pxor	%%mm7, %%mm7\n\t"
	"movq	%0, %%mm6\n\t"
	::"m"(mm_wone));
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
	"movq	%%mm0, %0\n\t"
	:"=m"(*p)
	:"m"(*pix),
	 "m"(*(pix+line_size))
	:"memory");
   pix += line_size;
   p +=   line_size;
  } while(--h);
}

static void avg_pixels_mmx(UINT8 *block, const UINT8 *pixels, int line_size, int h)
{
  UINT8  *p;
  const UINT8 *pix;
  p = block;
  pix = pixels;
  __asm __volatile(
	"pxor	%%mm7, %%mm7\n\t"
	"movq	%0, %%mm6\n\t"
	::"m"(mm_wone));
  do {
    __asm __volatile(
	"movq	%0, %%mm0\n\t"
	"movq	%1, %%mm1\n\t"
	"movq	%%mm0, %%mm2\n\t"
	"movq	%%mm1, %%mm3\n\t"
	"punpcklbw %%mm7, %%mm0\n\t"
	"punpcklbw %%mm7, %%mm1\n\t"
	"punpckhbw %%mm7, %%mm2\n\t"
	"punpckhbw %%mm7, %%mm3\n\t"
	"paddusw %%mm1, %%mm0\n\t"
	"paddusw %%mm3, %%mm2\n\t"
	"paddusw %%mm6, %%mm0\n\t"
	"paddusw %%mm6, %%mm2\n\t"
	"psrlw	$1, %%mm0\n\t"
	"psrlw	$1, %%mm2\n\t"
	"packuswb  %%mm2, %%mm0\n\t"
	"movq	%%mm0, %0\n\t"
	:"+m"(*p)
	:"m"(*pix)
	:"memory");
   pix += line_size;
   p +=   line_size;
  }
  while (--h);
}

static void   avg_pixels_x2_mmx( UINT8  *block, const UINT8 *pixels, int line_size, int h)
{
  UINT8  *p;
  const UINT8 *pix;
  p = block;
  pix = pixels;
  __asm __volatile(
	"pxor	%%mm7, %%mm7\n\t"
	"movq	%0, %%mm6\n\t"
	::"m"(mm_wone));
  do {
    __asm __volatile(
	"movq	%1, %%mm1\n\t"
	"movq	%0, %%mm0\n\t"
	"movq	1%1, %%mm4\n\t"
	"movq	%%mm0, %%mm2\n\t"
	"movq	%%mm1, %%mm3\n\t"
	"movq	%%mm4, %%mm5\n\t"
	"punpcklbw %%mm7, %%mm1\n\t"
	"punpckhbw %%mm7, %%mm3\n\t"
	"punpcklbw %%mm7, %%mm4\n\t"
	"punpckhbw %%mm7, %%mm5\n\t"
	"punpcklbw %%mm7, %%mm0\n\t"
	"punpckhbw %%mm7, %%mm2\n\t"
	"paddusw %%mm4, %%mm1\n\t"
	"paddusw %%mm5, %%mm3\n\t"
	"paddusw %%mm6, %%mm1\n\t"
	"paddusw %%mm6, %%mm3\n\t"
	"psrlw	$1, %%mm1\n\t"
	"psrlw	$1, %%mm3\n\t"
	"paddusw %%mm6, %%mm0\n\t"
	"paddusw %%mm6, %%mm2\n\t"
	"paddusw %%mm1, %%mm0\n\t"
	"paddusw %%mm3, %%mm2\n\t"
	"psrlw	$1, %%mm0\n\t"
	"psrlw	$1, %%mm2\n\t"
	"packuswb  %%mm2, %%mm0\n\t"
	"movq	%%mm0, %0\n\t"
	:"+m"(*p)
	:"m"(*pix)
	:"memory");
   pix += line_size;
   p +=   line_size;
  } while (--h);
}

static void   avg_pixels_y2_mmx( UINT8  *block, const UINT8 *pixels, int line_size, int h)
{
  UINT8  *p;
  const UINT8 *pix;
  p = block;
  pix = pixels;
  __asm __volatile(
	"pxor	%%mm7, %%mm7\n\t"
	"movq	%0, %%mm6\n\t"
	::"m"(mm_wone));
  do {
    __asm __volatile(
	"movq	%1, %%mm1\n\t"
	"movq	%0, %%mm0\n\t"
	"movq	%2, %%mm4\n\t"
	"movq	%%mm0, %%mm2\n\t"
	"movq	%%mm1, %%mm3\n\t"
	"movq	%%mm4, %%mm5\n\t"
	"punpcklbw %%mm7, %%mm1\n\t"
	"punpckhbw %%mm7, %%mm3\n\t"
	"punpcklbw %%mm7, %%mm4\n\t"
	"punpckhbw %%mm7, %%mm5\n\t"
	"punpcklbw %%mm7, %%mm0\n\t"
	"punpckhbw %%mm7, %%mm2\n\t"
	"paddusw %%mm4, %%mm1\n\t"
	"paddusw %%mm5, %%mm3\n\t"
	"paddusw %%mm6, %%mm1\n\t"
	"paddusw %%mm6, %%mm3\n\t"
	"psrlw	$1, %%mm1\n\t"
	"psrlw	$1, %%mm3\n\t"
	"paddusw %%mm6, %%mm0\n\t"
	"paddusw %%mm6, %%mm2\n\t"
	"paddusw %%mm1, %%mm0\n\t"
	"paddusw %%mm3, %%mm2\n\t"
	"psrlw	$1, %%mm0\n\t"
	"psrlw	$1, %%mm2\n\t"
	"packuswb  %%mm2, %%mm0\n\t"
	"movq	%%mm0, %0\n\t"
	:"+m"(*p)
	:"m"(*pix), "m"(*(pix+line_size))
	:"memory");
   pix += line_size;
   p +=   line_size ;
  } while(--h);
}

static void   avg_pixels_xy2_mmx( UINT8  *block, const UINT8 *pixels, int line_size, int h)
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
	"movq	%3, %%mm5\n\t"
	"psrlw	$2, %%mm0\n\t"
	"movq	%0, %%mm1\n\t"
	"psrlw	$2, %%mm2\n\t"
	"movq	%%mm1, %%mm3\n\t"
	"punpcklbw %%mm7, %%mm1\n\t"
	"punpckhbw %%mm7, %%mm3\n\t"
	"paddusw %%mm1, %%mm0\n\t"
	"paddusw %%mm3, %%mm2\n\t"
	"paddusw %%mm5, %%mm0\n\t"
	"paddusw %%mm5, %%mm2\n\t"
	"psrlw	$1, %%mm0\n\t"
	"psrlw	$1, %%mm2\n\t"
	"packuswb  %%mm2, %%mm0\n\t"
	"movq	%%mm0, %0\n\t"
	:"+m"(*p)
	:"m"(*pix),
	 "m"(*(pix+line_size)), "m"(mm_wone)
	:"memory");
   pix += line_size;
   p +=   line_size ;
  } while(--h);
}

static void avg_no_rnd_pixels_mmx( UINT8  *block, const UINT8 *pixels, int line_size, int h)
{
  UINT8  *p;
  const UINT8 *pix;
  p = block;
  pix = pixels;
  __asm __volatile("pxor %%mm7, %%mm7\n\t":);
  do {
    __asm __volatile(
	"movq	%1, %%mm0\n\t"
	"movq	%0, %%mm1\n\t"
	"movq	%%mm0, %%mm2\n\t"
	"movq	%%mm1, %%mm3\n\t"
	"punpcklbw %%mm7, %%mm0\n\t"
	"punpcklbw %%mm7, %%mm1\n\t"
	"punpckhbw %%mm7, %%mm2\n\t"
	"punpckhbw %%mm7, %%mm3\n\t"
	"paddusw %%mm1, %%mm0\n\t"
	"paddusw %%mm3, %%mm2\n\t"
	"psrlw	$1, %%mm0\n\t"
	"psrlw	$1, %%mm2\n\t"
	"packuswb  %%mm2, %%mm0\n\t"
	"movq	%%mm0, %0\n\t"
	:"+m"(*p)
	:"m"(*pix)
	:"memory");
   pix += line_size;
   p +=   line_size ;
  } while (--h);
}

static void   avg_no_rnd_pixels_x2_mmx( UINT8  *block, const UINT8 *pixels, int line_size, int h)
{
  UINT8  *p;
  const UINT8 *pix;
  p = block;
  pix = pixels;
  __asm __volatile(
      "pxor	%%mm7, %%mm7\n\t":);
  do {
    __asm __volatile(
	"movq	%1, %%mm0\n\t"
	"movq	1%1, %%mm1\n\t"
	"movq	%0, %%mm4\n\t"
	"movq	%%mm0, %%mm2\n\t"
	"movq	%%mm1, %%mm3\n\t"
	"movq	%%mm4, %%mm5\n\t"
	"punpcklbw %%mm7, %%mm0\n\t"
	"punpcklbw %%mm7, %%mm1\n\t"
	"punpckhbw %%mm7, %%mm2\n\t"
	"punpckhbw %%mm7, %%mm3\n\t"
	"punpcklbw %%mm7, %%mm4\n\t"
	"punpckhbw %%mm7, %%mm5\n\t"
	"paddusw %%mm1, %%mm0\n\t"
	"paddusw %%mm3, %%mm2\n\t"
	"psrlw	$1, %%mm0\n\t"
	"psrlw	$1, %%mm2\n\t"
	"paddusw %%mm4, %%mm0\n\t"
	"paddusw %%mm5, %%mm2\n\t"
	"psrlw	$1, %%mm0\n\t"
	"psrlw	$1, %%mm2\n\t"
	"packuswb  %%mm2, %%mm0\n\t"
	"movq	%%mm0, %0\n\t"
	:"+m"(*p)
	:"m"(*pix)
	:"memory");
   pix += line_size;
   p +=   line_size;
 } while (--h);
}

static void   avg_no_rnd_pixels_y2_mmx( UINT8  *block, const UINT8 *pixels, int line_size, int h)
{
  UINT8  *p;
  const UINT8 *pix;
  p = block;
  pix = pixels;
  __asm __volatile(
      "pxor	%%mm7, %%mm7\n\t":);
  do {
    __asm __volatile(
	"movq	%1, %%mm0\n\t"
	"movq	%2, %%mm1\n\t"
	"movq	%0, %%mm4\n\t"
	"movq	%%mm0, %%mm2\n\t"
	"movq	%%mm1, %%mm3\n\t"
	"movq	%%mm4, %%mm5\n\t"
	"punpcklbw %%mm7, %%mm0\n\t"
	"punpcklbw %%mm7, %%mm1\n\t"
	"punpckhbw %%mm7, %%mm2\n\t"
	"punpckhbw %%mm7, %%mm3\n\t"
	"punpcklbw %%mm7, %%mm4\n\t"
	"punpckhbw %%mm7, %%mm5\n\t"
	"paddusw %%mm1, %%mm0\n\t"
	"paddusw %%mm3, %%mm2\n\t"
	"psrlw	$1, %%mm0\n\t"
	"psrlw	$1, %%mm2\n\t"
	"paddusw %%mm4, %%mm0\n\t"
	"paddusw %%mm5, %%mm2\n\t"
	"psrlw	$1, %%mm0\n\t"
	"psrlw	$1, %%mm2\n\t"
	"packuswb  %%mm2, %%mm0\n\t"
	"movq	%%mm0, %0\n\t"
	:"+m"(*p)
	:"m"(*pix), "m"(*(pix+line_size))
	:"memory");
   pix += line_size;
   p +=   line_size ;
  } while(--h);
}

static void   avg_no_rnd_pixels_xy2_mmx( UINT8  *block, const UINT8 *pixels, int line_size, int h)
{
  UINT8  *p;
  const UINT8 *pix;
  p = block;
  pix = pixels;
  __asm __volatile(
	"pxor	%%mm7, %%mm7\n\t"
	"movq	%0, %%mm6\n\t"
	::"m"(mm_wone));
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
	"movq	%0, %%mm1\n\t"
	"psrlw	$2, %%mm0\n\t"
	"movq	%%mm1, %%mm3\n\t"
	"psrlw	$2, %%mm2\n\t"
	"punpcklbw %%mm7, %%mm1\n\t"
	"punpckhbw %%mm7, %%mm3\n\t"
	"paddusw %%mm1, %%mm0\n\t"
	"paddusw %%mm3, %%mm2\n\t"
	"psrlw	$1, %%mm0\n\t"
	"psrlw	$1, %%mm2\n\t"
	"packuswb  %%mm2, %%mm0\n\t"
	"movq	%%mm0, %0\n\t"
	:"+m"(*p)
	:"m"(*pix),
	 "m"(*(pix+line_size))
	:"memory");
   pix += line_size;
   p += line_size;
  } while(--h);
}

static void sub_pixels_mmx( DCTELEM  *block, const UINT8 *pixels, int line_size, int h)
{
  DCTELEM  *p;
  const UINT8 *pix;
  p = block;
  pix = pixels;
  __asm __volatile("pxor %%mm7, %%mm7":);
  do {
    __asm __volatile(
	"movq	%0, %%mm0\n\t"
	"movq	%1, %%mm2\n\t"
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

static void sub_pixels_x2_mmx( DCTELEM  *block, const UINT8 *pixels, int line_size, int h)
{
  DCTELEM  *p;
  const UINT8 *pix;
  p = block;
  pix = pixels;
  __asm __volatile(
      "pxor	%%mm7, %%mm7\n\t"
      "movq	%0, %%mm6"
      ::"m"(mm_wone));
  do {
    __asm __volatile(
	"movq	%0, %%mm0\n\t"
	"movq	%1, %%mm2\n\t"
	"movq	8%0, %%mm1\n\t"
	"movq	1%1, %%mm4\n\t"
	"movq	%%mm2, %%mm3\n\t"
	"movq	%%mm4, %%mm5\n\t"
	"punpcklbw %%mm7, %%mm2\n\t"
	"punpckhbw %%mm7, %%mm3\n\t"
	"punpcklbw %%mm7, %%mm4\n\t"
	"punpckhbw %%mm7, %%mm5\n\t"
	"paddusw %%mm4, %%mm2\n\t"
	"paddusw %%mm5, %%mm3\n\t"
	"paddusw %%mm6, %%mm2\n\t"
	"paddusw %%mm6, %%mm3\n\t"
	"psrlw	$1, %%mm2\n\t"
	"psrlw	$1, %%mm3\n\t"
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

static void sub_pixels_y2_mmx( DCTELEM  *block, const UINT8 *pixels, int line_size, int h)
{
  DCTELEM  *p;
  const UINT8 *pix;
  p = block;
  pix = pixels;
  __asm __volatile(
      "pxor	%%mm7, %%mm7\n\t"
      "movq	%0, %%mm6"
      ::"m"(mm_wone));
  do {
    __asm __volatile(
	"movq	%0, %%mm0\n\t"
	"movq	%1, %%mm2\n\t"
	"movq	8%0, %%mm1\n\t"
	"movq	%2, %%mm4\n\t"
	"movq	%%mm2, %%mm3\n\t"
	"movq	%%mm4, %%mm5\n\t"
	"punpcklbw %%mm7, %%mm2\n\t"
	"punpckhbw %%mm7, %%mm3\n\t"
	"punpcklbw %%mm7, %%mm4\n\t"
	"punpckhbw %%mm7, %%mm5\n\t"
	"paddusw %%mm4, %%mm2\n\t"
	"paddusw %%mm5, %%mm3\n\t"
	"paddusw %%mm6, %%mm2\n\t"
	"paddusw %%mm6, %%mm3\n\t"
	"psrlw	$1, %%mm2\n\t"
	"psrlw	$1, %%mm3\n\t"
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

static void   sub_pixels_xy2_mmx( DCTELEM  *block, const UINT8 *pixels, int line_size, int h)
{
  DCTELEM  *p;
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
	"movq	%0, %%mm1\n\t"
	"movq	8%0, %%mm3\n\t"
	"psrlw	$2, %%mm0\n\t"
	"psrlw	$2, %%mm2\n\t"
	"psubsw %%mm0, %%mm1\n\t"
	"psubsw %%mm2, %%mm3\n\t"
	"movq	%%mm1, %0\n\t"
	"movq	%%mm3, 8%0\n\t"
	:"+m"(*p)
	:"m"(*pix),
	 "m"(*(pix+line_size))
	:"memory");
   pix += line_size;
   p +=   8 ;
  } while(--h);
}

void dsputil_init_mmx(void)
{
    mm_flags = mm_support();
#if 0
    printf("CPU flags:");
    if (mm_flags & MM_MMX)
        printf(" mmx");
    if (mm_flags & MM_MMXEXT)
        printf(" mmxext");
    if (mm_flags & MM_3DNOW)
        printf(" 3dnow");
    if (mm_flags & MM_SSE)
        printf(" sse");
    if (mm_flags & MM_SSE2)
        printf(" sse2");
    printf("\n");
#endif

    if (mm_flags & MM_MMX) {
        get_pixels = get_pixels_mmx;
        put_pixels_clamped = put_pixels_clamped_mmx;
        add_pixels_clamped = add_pixels_clamped_mmx;
        
        pix_abs16x16 = pix_abs16x16_mmx;
        pix_abs16x16_x2 = pix_abs16x16_x2_mmx;
        pix_abs16x16_y2 = pix_abs16x16_y2_mmx;
        pix_abs16x16_xy2 = pix_abs16x16_xy2_mmx;
        av_fdct = fdct_mmx;

        put_pixels_tab[0] = put_pixels_mmx;
        put_pixels_tab[1] = put_pixels_x2_mmx;
        put_pixels_tab[2] = put_pixels_y2_mmx;
        put_pixels_tab[3] = put_pixels_xy2_mmx;

        put_no_rnd_pixels_tab[0] = put_pixels_mmx;
        put_no_rnd_pixels_tab[1] = put_no_rnd_pixels_x2_mmx;
        put_no_rnd_pixels_tab[2] = put_no_rnd_pixels_y2_mmx;
        put_no_rnd_pixels_tab[3] = put_no_rnd_pixels_xy2_mmx;
        
        avg_pixels_tab[0] = avg_pixels_mmx;
        avg_pixels_tab[1] = avg_pixels_x2_mmx;
        avg_pixels_tab[2] = avg_pixels_y2_mmx;
        avg_pixels_tab[3] = avg_pixels_xy2_mmx;

        avg_no_rnd_pixels_tab[0] = avg_no_rnd_pixels_mmx;
        avg_no_rnd_pixels_tab[1] = avg_no_rnd_pixels_x2_mmx;
        avg_no_rnd_pixels_tab[2] = avg_no_rnd_pixels_y2_mmx;
        avg_no_rnd_pixels_tab[3] = avg_no_rnd_pixels_xy2_mmx;
        
        sub_pixels_tab[0] = sub_pixels_mmx;
        sub_pixels_tab[1] = sub_pixels_x2_mmx;
        sub_pixels_tab[2] = sub_pixels_y2_mmx;
        sub_pixels_tab[3] = sub_pixels_xy2_mmx;

        if (mm_flags & MM_MMXEXT) {
            pix_abs16x16 = pix_abs16x16_sse;
        }

        if (mm_flags & MM_SSE) {
            put_pixels_tab[1] = put_pixels_x2_sse;
            put_pixels_tab[2] = put_pixels_y2_sse;
            
            avg_pixels_tab[0] = avg_pixels_sse;
            avg_pixels_tab[1] = avg_pixels_x2_sse;
            avg_pixels_tab[2] = avg_pixels_y2_sse;
            avg_pixels_tab[3] = avg_pixels_xy2_sse;

            sub_pixels_tab[1] = sub_pixels_x2_sse;
            sub_pixels_tab[2] = sub_pixels_y2_sse;
        } else if (mm_flags & MM_3DNOW) {
            put_pixels_tab[1] = put_pixels_x2_3dnow;
            put_pixels_tab[2] = put_pixels_y2_3dnow;
            
            avg_pixels_tab[0] = avg_pixels_3dnow;
            avg_pixels_tab[1] = avg_pixels_x2_3dnow;
            avg_pixels_tab[2] = avg_pixels_y2_3dnow;
            avg_pixels_tab[3] = avg_pixels_xy2_3dnow;

            sub_pixels_tab[1] = sub_pixels_x2_3dnow;
            sub_pixels_tab[2] = sub_pixels_y2_3dnow;
        }

        /* idct */
        if (mm_flags & MM_MMXEXT) {
            ff_idct = ff_mmxext_idct;
        } else {
            ff_idct = ff_mmx_idct;
        }
#ifdef SIMPLE_IDCT
//	ff_idct = simple_idct;
	ff_idct = simple_idct_mmx;
#endif
    }
}
