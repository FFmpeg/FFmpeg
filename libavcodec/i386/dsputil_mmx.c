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

int pix_abs16x16_mmx(UINT8 *blk1, UINT8 *blk2, int lx);
int pix_abs16x16_x2_mmx(UINT8 *blk1, UINT8 *blk2, int lx);
int pix_abs16x16_y2_mmx(UINT8 *blk1, UINT8 *blk2, int lx);
int pix_abs16x16_xy2_mmx(UINT8 *blk1, UINT8 *blk2, int lx);

int pix_abs16x16_mmx2(UINT8 *blk1, UINT8 *blk2, int lx);
int pix_abs16x16_x2_mmx2(UINT8 *blk1, UINT8 *blk2, int lx);
int pix_abs16x16_y2_mmx2(UINT8 *blk1, UINT8 *blk2, int lx);
int pix_abs16x16_xy2_mmx2(UINT8 *blk1, UINT8 *blk2, int lx);

int pix_abs8x8_mmx(UINT8 *blk1, UINT8 *blk2, int lx);
int pix_abs8x8_x2_mmx(UINT8 *blk1, UINT8 *blk2, int lx);
int pix_abs8x8_y2_mmx(UINT8 *blk1, UINT8 *blk2, int lx);
int pix_abs8x8_xy2_mmx(UINT8 *blk1, UINT8 *blk2, int lx);

int pix_abs8x8_mmx2(UINT8 *blk1, UINT8 *blk2, int lx);
int pix_abs8x8_x2_mmx2(UINT8 *blk1, UINT8 *blk2, int lx);
int pix_abs8x8_y2_mmx2(UINT8 *blk1, UINT8 *blk2, int lx);
int pix_abs8x8_xy2_mmx2(UINT8 *blk1, UINT8 *blk2, int lx);


/* external functions, from idct_mmx.c */
void ff_mmx_idct(DCTELEM *block);
void ff_mmxext_idct(DCTELEM *block);

/* pixel operations */
static const unsigned long long int mm_wone __attribute__ ((aligned(8))) = 0x0001000100010001LL;
static const unsigned long long int mm_wtwo __attribute__ ((aligned(8))) = 0x0002000200020002LL;
//static const unsigned short mm_wone[4] __attribute__ ((aligned(8))) = { 0x1, 0x1, 0x1, 0x1 };
//static const unsigned short mm_wtwo[4] __attribute__ ((aligned(8))) = { 0x2, 0x2, 0x2, 0x2 };

#define JUMPALIGN() __asm __volatile (".balign 8"::)
#define MOVQ_ZERO(regd)  __asm __volatile ("pxor %%" #regd ", %%" #regd ::)

#ifndef PIC
#define MOVQ_WONE(regd)  __asm __volatile ("movq %0, %%" #regd " \n\t" ::"m"(mm_wone))
#define MOVQ_WTWO(regd)  __asm __volatile ("movq %0, %%" #regd " \n\t" ::"m"(mm_wtwo))
#else
// for shared library it's better to use this way for accessing constants
// pcmpeqd -> -1
#define MOVQ_WONE(regd) \
    __asm __volatile ( \
       "pcmpeqd %%" #regd ", %%" #regd " \n\t" \
       "psrlw $15, %%" #regd ::)

#define MOVQ_WTWO(regd) \
    __asm __volatile ( \
       "pcmpeqd %%" #regd ", %%" #regd " \n\t" \
       "psrlw $15, %%" #regd " \n\t" \
       "psllw $1, %%" #regd ::)
#endif

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
    MOVQ_ZERO(mm7);
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

    /* read the pixels */
    p = block;
    pix = pixels;
    /* unrolled loop */
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

    // if here would be an exact copy of the code above
    // compiler would generate some very strange code
    // thus using "r"
    __asm __volatile(
	    "movq	(%3), %%mm0\n\t"
	    "movq	8(%3), %%mm1\n\t"
	    "movq	16(%3), %%mm2\n\t"
	    "movq	24(%3), %%mm3\n\t"
	    "movq	32(%3), %%mm4\n\t"
	    "movq	40(%3), %%mm5\n\t"
	    "movq	48(%3), %%mm6\n\t"
	    "movq	56(%3), %%mm7\n\t"
	    "packuswb %%mm1, %%mm0\n\t"
	    "packuswb %%mm3, %%mm2\n\t"
	    "packuswb %%mm5, %%mm4\n\t"
	    "packuswb %%mm7, %%mm6\n\t"
	    "movq	%%mm0, (%0)\n\t"
	    "movq	%%mm2, (%0, %1)\n\t"
	    "movq	%%mm4, (%0, %1, 2)\n\t"
	    "movq	%%mm6, (%0, %2)\n\t"
	    ::"r" (pix), "r" (line_size), "r" (line_size*3), "r"(p)
	    :"memory");
}

static void add_pixels_clamped_mmx(const DCTELEM *block, UINT8 *pixels, int line_size)
{
    const DCTELEM *p;
    UINT8 *pix;
    int i;

    /* read the pixels */
    p = block;
    pix = pixels;
    MOVQ_ZERO(mm7);
    i = 4;
    while (i) {
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
        i--;
    };
}

static void put_pixels_mmx(UINT8 *block, const UINT8 *pixels, int line_size, int h)
{
    int hh;
    UINT8 *p;
    const UINT8 *pix;

    p   = block;
    pix = pixels; // 2s
#if 0
    do {
      __asm __volatile(
	"movq	%1, %%mm0\n\t"
	"movq	%%mm0, %0\n\t"
	:"=m"(*p)
	:"m"(*pix)
	:"memory");
	pix += line_size;
	p += line_size;
    } while (--h);
#else
    // this optimized code is not very usefull
    // the above loop is definitely faster
    // at least on Celeron 500MHz
    hh = h & 3;
    while (hh) {
      __asm __volatile(
	  "movq	%1, %%mm0\n\t"
	  "movq	%%mm0, %0\n\t"
	  :"=m"(*p)
	  :"m"(*pix)
	  :"memory");
	pix += line_size;
	p += line_size;
	hh--;
    }
    hh=h>>2;
    while (hh) {
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
        pix += line_size*4;
	p += line_size*4;
        hh--;
    }
#endif
}

static void put_pixels_x2_mmx(UINT8 *block, const UINT8 *pixels, int line_size, int h)
{
  UINT8 *p;
  const UINT8 *pix;
  p = block;
  pix = pixels;
  MOVQ_ZERO(mm7);
  MOVQ_WONE(mm4);
  JUMPALIGN();
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
  MOVQ_ZERO(mm7);
  MOVQ_WONE(mm4);
  JUMPALIGN();
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
  pix = pixels; // 1s
  MOVQ_ZERO(mm7);
  MOVQ_WTWO(mm6);
  JUMPALIGN();
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
  MOVQ_ZERO(mm7);
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
  MOVQ_ZERO(mm7);
  JUMPALIGN();
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
  MOVQ_ZERO(mm7);
  MOVQ_WONE(mm6);
  JUMPALIGN();
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
  MOVQ_ZERO(mm7);
  MOVQ_WONE(mm6);
  JUMPALIGN();
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
  MOVQ_ZERO(mm7);
  MOVQ_WONE(mm6);
  JUMPALIGN();
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
  MOVQ_ZERO(mm7);
  MOVQ_WONE(mm6);
  JUMPALIGN();
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
  MOVQ_ZERO(mm7);
  // this doesn't seem to be used offten - so
  // the inside usage of mm_wone is not optimized
  MOVQ_WTWO(mm6);
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
  MOVQ_ZERO(mm7);
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
  MOVQ_ZERO(mm7);
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
  MOVQ_ZERO(mm7);
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
  MOVQ_ZERO(mm7);
  MOVQ_WONE(mm6);
  JUMPALIGN();
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
  MOVQ_ZERO(mm7);
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
  MOVQ_ZERO(mm7);
  MOVQ_WONE(mm6);
  JUMPALIGN();
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
  MOVQ_ZERO(mm7);
  MOVQ_WONE(mm6);
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
  MOVQ_ZERO(mm7);
  MOVQ_WTWO(mm6);
  JUMPALIGN();
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

static void clear_blocks_mmx(DCTELEM *blocks)
{
        asm volatile(
                "pxor %%mm7, %%mm7		\n\t"
                "movl $-128*6, %%eax		\n\t"
                "1:				\n\t"
                "movq %%mm7, (%0, %%eax)	\n\t"
                "movq %%mm7, 8(%0, %%eax)	\n\t"
                "movq %%mm7, 16(%0, %%eax)	\n\t"
                "movq %%mm7, 24(%0, %%eax)	\n\t"
                "addl $32, %%eax		\n\t"
                " js 1b				\n\t"
                : : "r" (((int)blocks)+128*6)
                : "%eax"
        );
}

static void just_return() { return; }

void dsputil_init_mmx(void)
{
    mm_flags = mm_support();
#if 1
    printf("libavcodec: CPU flags:");
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
        clear_blocks= clear_blocks_mmx;
       
        pix_abs16x16     = pix_abs16x16_mmx;
        pix_abs16x16_x2  = pix_abs16x16_x2_mmx;
        pix_abs16x16_y2  = pix_abs16x16_y2_mmx;
        pix_abs16x16_xy2 = pix_abs16x16_xy2_mmx;
        pix_abs8x8    = pix_abs8x8_mmx;
        pix_abs8x8_x2 = pix_abs8x8_x2_mmx;
        pix_abs8x8_y2 = pix_abs8x8_y2_mmx;
        pix_abs8x8_xy2= pix_abs8x8_xy2_mmx;
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
            pix_abs16x16    = pix_abs16x16_mmx2;
            pix_abs16x16_x2 = pix_abs16x16_x2_mmx2;
            pix_abs16x16_y2 = pix_abs16x16_y2_mmx2;
            pix_abs16x16_xy2= pix_abs16x16_xy2_mmx2;
            
            pix_abs8x8    = pix_abs8x8_mmx2;
            pix_abs8x8_x2 = pix_abs8x8_x2_mmx2;
            pix_abs8x8_y2 = pix_abs8x8_y2_mmx2;
            pix_abs8x8_xy2= pix_abs8x8_xy2_mmx2;
            
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

#if 0
    // for speed testing
    get_pixels = just_return;
    put_pixels_clamped = just_return;
    add_pixels_clamped = just_return;

    pix_abs16x16 = just_return;
    pix_abs16x16_x2 = just_return;
    pix_abs16x16_y2 = just_return;
    pix_abs16x16_xy2 = just_return;

    put_pixels_tab[0] = just_return;
    put_pixels_tab[1] = just_return;
    put_pixels_tab[2] = just_return;
    put_pixels_tab[3] = just_return;

    put_no_rnd_pixels_tab[0] = just_return;
    put_no_rnd_pixels_tab[1] = just_return;
    put_no_rnd_pixels_tab[2] = just_return;
    put_no_rnd_pixels_tab[3] = just_return;

    avg_pixels_tab[0] = just_return;
    avg_pixels_tab[1] = just_return;
    avg_pixels_tab[2] = just_return;
    avg_pixels_tab[3] = just_return;

    avg_no_rnd_pixels_tab[0] = just_return;
    avg_no_rnd_pixels_tab[1] = just_return;
    avg_no_rnd_pixels_tab[2] = just_return;
    avg_no_rnd_pixels_tab[3] = just_return;

    sub_pixels_tab[0] = just_return;
    sub_pixels_tab[1] = just_return;
    sub_pixels_tab[2] = just_return;
    sub_pixels_tab[3] = just_return;

    //av_fdct = just_return;
    //ff_idct = just_return;
#endif
}
