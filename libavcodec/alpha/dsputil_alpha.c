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

void put_pixels_clamped_mvi_asm(const DCTELEM *block, uint8_t *pixels,
				int line_size);
void add_pixels_clamped_mvi_asm(const DCTELEM *block, uint8_t *pixels, 
				int line_size);

#if 0
/* These functions were the base for the optimized assembler routines,
   and remain here for documentation purposes.  */
static void put_pixels_clamped_mvi(const DCTELEM *block, uint8_t *pixels, 
                                   int line_size)
{
    int i = 8;
    uint64_t clampmask = zap(-1, 0xaa); /* 0x00ff00ff00ff00ff */

    ASM_ACCEPT_MVI;

    do {
        uint64_t shorts0, shorts1;

        shorts0 = ldq(block);
        shorts0 = maxsw4(shorts0, 0);
        shorts0 = minsw4(shorts0, clampmask);
        stl(pkwb(shorts0), pixels);

        shorts1 = ldq(block + 4);
        shorts1 = maxsw4(shorts1, 0);
        shorts1 = minsw4(shorts1, clampmask);
        stl(pkwb(shorts1), pixels + 4);

        pixels += line_size;
        block += 8;
    } while (--i);
}

void add_pixels_clamped_mvi(const DCTELEM *block, uint8_t *pixels, 
                            int line_size)
{
    int h = 8;
    /* Keep this function a leaf function by generating the constants
       manually (mainly for the hack value ;-).  */
    uint64_t clampmask = zap(-1, 0xaa); /* 0x00ff00ff00ff00ff */
    uint64_t signmask  = zap(-1, 0x33);
    signmask ^= signmask >> 1;  /* 0x8000800080008000 */

    ASM_ACCEPT_MVI;

    do {
        uint64_t shorts0, pix0, signs0;
        uint64_t shorts1, pix1, signs1;

        shorts0 = ldq(block);
        shorts1 = ldq(block + 4);

        pix0    = unpkbw(ldl(pixels));
        /* Signed subword add (MMX paddw).  */
        signs0  = shorts0 & signmask;
        shorts0 &= ~signmask;
        shorts0 += pix0;
        shorts0 ^= signs0;
        /* Clamp. */
        shorts0 = maxsw4(shorts0, 0);
        shorts0 = minsw4(shorts0, clampmask);   

        /* Next 4.  */
        pix1    = unpkbw(ldl(pixels + 4));
        signs1  = shorts1 & signmask;
        shorts1 &= ~signmask;
        shorts1 += pix1;
        shorts1 ^= signs1;
        shorts1 = maxsw4(shorts1, 0);
        shorts1 = minsw4(shorts1, clampmask);

        stl(pkwb(shorts0), pixels);
        stl(pkwb(shorts1), pixels + 4);

        pixels += line_size;
        block += 8;
    } while (--h);
}
#endif

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
        put_pixels_clamped = put_pixels_clamped_mvi_asm;
        add_pixels_clamped = add_pixels_clamped_mvi_asm;
    }
}
