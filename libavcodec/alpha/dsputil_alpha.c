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

#include "asm.h"
#include "../dsputil.h"

void simple_idct_axp(DCTELEM *block);

static void put_pixels_clamped_axp(const DCTELEM *block, UINT8 *pixels, 
				   int line_size)
{
    int i = 8;
    do {
	UINT64 shorts;

	shorts = ldq(block);
	shorts = maxsw4(shorts, 0);
	shorts = minsw4(shorts, WORD_VEC(0x00ff));
	stl(pkwb(shorts), pixels);

	shorts = ldq(block + 4);
	shorts = maxsw4(shorts, 0);
	shorts = minsw4(shorts, WORD_VEC(0x00ff));
	stl(pkwb(shorts), pixels + 4);

	pixels += line_size;
	block += 8;
    } while (--i);
}

static void add_pixels_clamped_axp(const DCTELEM *block, UINT8 *pixels, 
				   int line_size)
{
    int i = 8;
    do {
	UINT64 shorts; 

	shorts = ldq(block);
	shorts &= ~WORD_VEC(0x8000); /* clear highest bit to avoid overflow */
	shorts += unpkbw(ldl(pixels));
	shorts &= ~WORD_VEC(0x8000); /* hibit would be set for e. g. -2 + 3 */
	shorts = minuw4(shorts, WORD_VEC(0x4000)); /* set neg. to 0x4000 */
	shorts &= ~WORD_VEC(0x4000); /* ...and zap them */
	shorts = minsw4(shorts, WORD_VEC(0x00ff)); /* clamp to 255 */
	stl(pkwb(shorts), pixels);

	/* next 4 */
	shorts = ldq(block + 4);
	shorts &= ~WORD_VEC(0x8000);
	shorts += unpkbw(ldl(pixels + 4));
	shorts &= ~WORD_VEC(0x8000);
	shorts = minuw4(shorts, WORD_VEC(0x4000));
	shorts &= ~WORD_VEC(0x4000);
	shorts = minsw4(shorts, WORD_VEC(0x00ff));
	stl(pkwb(shorts), pixels + 4);

	pixels += line_size;
	block += 8;
    } while (--i);
}

/* Average 8 unsigned bytes in parallel: (b1 + b2) >> 1
   Since the immediate result could be greater than 255, we do the
   shift first. The result is too low by one if the bytes were both
   odd, so we need to add (l1 & l2) & BYTE_VEC(0x01).  */
static inline UINT64 avg2_no_rnd(UINT64 l1, UINT64 l2)
{
    UINT64 correction = (l1 & l2) & BYTE_VEC(0x01);
    l1 = (l1 & ~BYTE_VEC(0x01)) >> 1;
    l2 = (l2 & ~BYTE_VEC(0x01)) >> 1;
    return l1 + l2 + correction;
}

/* Average 8 bytes with rounding: (b1 + b2 + 1) >> 1
   The '1' only has an effect when one byte is even and the other odd,
   i. e. we also need to add (l1 ^ l2) & BYTE_VEC(0x01).
   Incidentally, that is equivalent to (l1 | l2) & BYTE_VEC(0x01).  */
static inline UINT64 avg2(UINT64 l1, UINT64 l2)
{
    UINT64 correction = (l1 | l2) & BYTE_VEC(0x01);
    l1 = (l1 & ~BYTE_VEC(0x01)) >> 1;
    l2 = (l2 & ~BYTE_VEC(0x01)) >> 1;
    return l1 + l2 + correction;
}

static inline UINT64 avg4(UINT64 l1, UINT64 l2, UINT64 l3, UINT64 l4)
{
    UINT64 r1 = ((l1 & ~BYTE_VEC(0x03)) >> 2)
	      + ((l2 & ~BYTE_VEC(0x03)) >> 2)
	      + ((l3 & ~BYTE_VEC(0x03)) >> 2)
	      + ((l4 & ~BYTE_VEC(0x03)) >> 2);
    UINT64 r2 = ((  (l1 & BYTE_VEC(0x03))
		  + (l2 & BYTE_VEC(0x03))
		  + (l3 & BYTE_VEC(0x03))
		  + (l4 & BYTE_VEC(0x03))
		  + BYTE_VEC(0x02)) >> 2) & BYTE_VEC(0x03);
    return r1 + r2;
}

static inline UINT64 avg4_no_rnd(UINT64 l1, UINT64 l2, UINT64 l3, UINT64 l4)
{
    UINT64 r1 = ((l1 & ~BYTE_VEC(0x03)) >> 2)
	      + ((l2 & ~BYTE_VEC(0x03)) >> 2)
	      + ((l3 & ~BYTE_VEC(0x03)) >> 2)
	      + ((l4 & ~BYTE_VEC(0x03)) >> 2);
    UINT64 r2 = (( (l1 & BYTE_VEC(0x03))
		 + (l2 & BYTE_VEC(0x03))
		 + (l3 & BYTE_VEC(0x03))
		 + (l4 & BYTE_VEC(0x03))
		 + BYTE_VEC(0x01)) >> 2) & BYTE_VEC(0x03);
    return r1 + r2;
}

#define PIXOPNAME(suffix) put ## suffix
#define BTYPE UINT8
#define AVG2 avg2
#define AVG4 avg4
#define STORE(l, b) stq(l, b)
#include "pixops.h"
#undef PIXOPNAME
#undef BTYPE
#undef AVG2
#undef AVG4
#undef STORE

#define PIXOPNAME(suffix) put_no_rnd ## suffix
#define BTYPE UINT8
#define AVG2 avg2_no_rnd
#define AVG4 avg4_no_rnd
#define STORE(l, b) stq(l, b)
#include "pixops.h"
#undef PIXOPNAME
#undef BTYPE
#undef AVG2
#undef AVG4
#undef STORE

/* The following functions are untested.  */
#if 0

#define PIXOPNAME(suffix) avg ## suffix
#define BTYPE UINT8
#define AVG2 avg2
#define AVG4 avg4
#define STORE(l, b) stq(AVG2(l, ldq(b)), b);
#include "pixops.h"
#undef PIXOPNAME
#undef BTYPE
#undef AVG2
#undef AVG4
#undef STORE

#define PIXOPNAME(suffix) avg_no_rnd ## suffix
#define BTYPE UINT8
#define AVG2 avg2_no_rnd
#define AVG4 avg4_no_rnd
#define STORE(l, b) stq(AVG2(l, ldq(b)), b);
#include "pixops.h"
#undef PIXOPNAME
#undef BTYPE
#undef AVG2
#undef AVG4
#undef STORE

#define PIXOPNAME(suffix) sub ## suffix
#define BTYPE DCTELEM
#define AVG2 avg2
#define AVG4 avg4
#define STORE(l, block) do {		\
    UINT64 xxx = l;			\
    (block)[0] -= (xxx >>  0) & 0xff;	\
    (block)[1] -= (xxx >>  8) & 0xff;	\
    (block)[2] -= (xxx >> 16) & 0xff;	\
    (block)[3] -= (xxx >> 24) & 0xff;	\
    (block)[4] -= (xxx >> 32) & 0xff;	\
    (block)[5] -= (xxx >> 40) & 0xff;	\
    (block)[6] -= (xxx >> 48) & 0xff;	\
    (block)[7] -= (xxx >> 56) & 0xff;	\
} while (0)
#include "pixops.h"
#undef PIXOPNAME
#undef BTYPE
#undef AVG2
#undef AVG4
#undef STORE

#endif

void dsputil_init_alpha(void)
{
    put_pixels_tab[0] = put_pixels_axp;
    put_pixels_tab[1] = put_pixels_x2_axp;
    put_pixels_tab[2] = put_pixels_y2_axp;
    put_pixels_tab[3] = put_pixels_xy2_axp;

    put_no_rnd_pixels_tab[0] = put_pixels_axp;
    put_no_rnd_pixels_tab[1] = put_no_rnd_pixels_x2_axp;
    put_no_rnd_pixels_tab[2] = put_no_rnd_pixels_y2_axp;
    put_no_rnd_pixels_tab[3] = put_no_rnd_pixels_xy2_axp;

    /* amask clears all bits that correspond to present features.  */
    if (amask(AMASK_MVI) == 0) {
	fprintf(stderr, "MVI extension detected\n");
	put_pixels_clamped = put_pixels_clamped_axp;
	add_pixels_clamped = add_pixels_clamped_axp;
    }
}
