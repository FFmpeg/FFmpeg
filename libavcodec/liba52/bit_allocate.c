/*
 * bit_allocate.c
 * Copyright (C) 2000-2003 Michel Lespinasse <walken@zoy.org>
 * Copyright (C) 1999-2000 Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
 *
 * This file is part of a52dec, a free ATSC A-52 stream decoder.
 * See http://liba52.sourceforge.net/ for updates.
 *
 * a52dec is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * a52dec is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "a52.h"
#include "a52_internal.h"

static int hthtab[3][50] = {
    {0x730, 0x730, 0x7c0, 0x800, 0x820, 0x840, 0x850, 0x850, 0x860, 0x860,
     0x860, 0x860, 0x860, 0x870, 0x870, 0x870, 0x880, 0x880, 0x890, 0x890,
     0x8a0, 0x8a0, 0x8b0, 0x8b0, 0x8c0, 0x8c0, 0x8d0, 0x8e0, 0x8f0, 0x900,
     0x910, 0x910, 0x910, 0x910, 0x900, 0x8f0, 0x8c0, 0x870, 0x820, 0x7e0,
     0x7a0, 0x770, 0x760, 0x7a0, 0x7c0, 0x7c0, 0x6e0, 0x400, 0x3c0, 0x3c0},
    {0x710, 0x710, 0x7a0, 0x7f0, 0x820, 0x830, 0x840, 0x850, 0x850, 0x860,
     0x860, 0x860, 0x860, 0x860, 0x870, 0x870, 0x870, 0x880, 0x880, 0x880,
     0x890, 0x890, 0x8a0, 0x8a0, 0x8b0, 0x8b0, 0x8c0, 0x8c0, 0x8e0, 0x8f0,
     0x900, 0x910, 0x910, 0x910, 0x910, 0x900, 0x8e0, 0x8b0, 0x870, 0x820,
     0x7e0, 0x7b0, 0x760, 0x770, 0x7a0, 0x7c0, 0x780, 0x5d0, 0x3c0, 0x3c0},
    {0x680, 0x680, 0x750, 0x7b0, 0x7e0, 0x810, 0x820, 0x830, 0x840, 0x850,
     0x850, 0x850, 0x860, 0x860, 0x860, 0x860, 0x860, 0x860, 0x860, 0x860,
     0x870, 0x870, 0x870, 0x870, 0x880, 0x880, 0x880, 0x890, 0x8a0, 0x8b0,
     0x8c0, 0x8d0, 0x8e0, 0x8f0, 0x900, 0x910, 0x910, 0x910, 0x900, 0x8f0,
     0x8d0, 0x8b0, 0x840, 0x7f0, 0x790, 0x760, 0x7a0, 0x7c0, 0x7b0, 0x720}
};

static int8_t baptab[305] = {
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,	/* 93 padding elems */

    16, 16, 16, 16, 16, 16, 16, 16, 16, 14, 14, 14, 14, 14, 14, 14,
    14, 12, 12, 12, 12, 11, 11, 11, 11, 10, 10, 10, 10,  9,  9,  9,
     9,  8,  8,  8,  8,  7,  7,  7,  7,  6,  6,  6,  6,  5,  5,  5,
     5,  4,  4, -3, -3,  3,  3,  3, -2, -2, -1, -1, -1, -1, -1,  0,

     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0					/* 148 padding elems */
};

static int bndtab[30] = {21, 22,  23,  24,  25,  26,  27,  28,  31,  34,
			 37, 40,  43,  46,  49,  55,  61,  67,  73,  79,
			 85, 97, 109, 121, 133, 157, 181, 205, 229, 253};

static int8_t latab[256] = {
    -64, -63, -62, -61, -60, -59, -58, -57, -56, -55, -54, -53,
    -52, -52, -51, -50, -49, -48, -47, -47, -46, -45, -44, -44,
    -43, -42, -41, -41, -40, -39, -38, -38, -37, -36, -36, -35,
    -35, -34, -33, -33, -32, -32, -31, -30, -30, -29, -29, -28,
    -28, -27, -27, -26, -26, -25, -25, -24, -24, -23, -23, -22,
    -22, -21, -21, -21, -20, -20, -19, -19, -19, -18, -18, -18,
    -17, -17, -17, -16, -16, -16, -15, -15, -15, -14, -14, -14,
    -13, -13, -13, -13, -12, -12, -12, -12, -11, -11, -11, -11,
    -10, -10, -10, -10, -10,  -9,  -9,  -9,  -9,  -9,  -8,  -8,
     -8,  -8,  -8,  -8,  -7,  -7,  -7,  -7,  -7,  -7,  -6,  -6,
     -6,  -6,  -6,  -6,  -6,  -6,  -5,  -5,  -5,  -5,  -5,  -5,
     -5,  -5,  -4,  -4,  -4,  -4,  -4,  -4,  -4,  -4,  -4,  -4,
     -4,  -3,  -3,  -3,  -3,  -3,  -3,  -3,  -3,  -3,  -3,  -3,
     -3,  -3,  -3,  -2,  -2,  -2,  -2,  -2,  -2,  -2,  -2,  -2,
     -2,  -2,  -2,  -2,  -2,  -2,  -2,  -2,  -2,  -2,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0
};

#define UPDATE_LEAK() 		\
do {				\
    fastleak += fdecay;		\
    if (fastleak > psd + fgain)	\
	fastleak = psd + fgain;	\
    slowleak += sdecay;		\
    if (slowleak > psd + sgain)	\
	slowleak = psd + sgain;	\
} while (0)

#define COMPUTE_MASK()				\
do {						\
    if (psd > dbknee)				\
	mask -= (psd - dbknee) >> 2;		\
    if (mask > hth [i >> halfrate])		\
	mask = hth [i >> halfrate];		\
    mask -= snroffset + 128 * deltba[i];	\
    mask = (mask > 0) ? 0 : ((-mask) >> 5);	\
    mask -= floor;				\
} while (0)

void a52_bit_allocate (a52_state_t * state, ba_t * ba, int bndstart,
		       int start, int end, int fastleak, int slowleak,
		       expbap_t * expbap)
{
    static int slowgain[4] = {0x540, 0x4d8, 0x478, 0x410};
    static int dbpbtab[4]  = {0xc00, 0x500, 0x300, 0x100};
    static int floortab[8] = {0x910, 0x950, 0x990, 0x9d0,
			      0xa10, 0xa90, 0xb10, 0x1400};

    int i, j;
    uint8_t * exp;
    int8_t * bap;
    int fdecay, fgain, sdecay, sgain, dbknee, floor, snroffset;
    int psd, mask;
    int8_t * deltba;
    int * hth;
    int halfrate;

    halfrate = state->halfrate;
    fdecay = (63 + 20 * ((state->bai >> 7) & 3)) >> halfrate;	/* fdcycod */
    fgain = 128 + 128 * (ba->bai & 7);				/* fgaincod */
    sdecay = (15 + 2 * (state->bai >> 9)) >> halfrate;		/* sdcycod */
    sgain = slowgain[(state->bai >> 5) & 3];			/* sgaincod */
    dbknee = dbpbtab[(state->bai >> 3) & 3];			/* dbpbcod */
    hth = hthtab[state->fscod];
    /*
     * if there is no delta bit allocation, make deltba point to an area
     * known to contain zeroes. baptab+156 here.
     */
    deltba = (ba->deltbae == DELTA_BIT_NONE) ? baptab + 156 : ba->deltba;
    floor = floortab[state->bai & 7];				/* floorcod */
    snroffset = 960 - 64 * state->csnroffst - 4 * (ba->bai >> 3) + floor;
    floor >>= 5;

    exp = expbap->exp;
    bap = expbap->bap;

    i = bndstart;
    j = start;
    if (start == 0) {	/* not the coupling channel */
	int lowcomp;

	lowcomp = 0;
	j = end - 1;
	do {
	    if (i < j) {
		if (exp[i+1] == exp[i] - 2)
		    lowcomp = 384;
		else if (lowcomp && (exp[i+1] > exp[i]))
		    lowcomp -= 64;
	    }
	    psd = 128 * exp[i];
	    mask = psd + fgain + lowcomp;
	    COMPUTE_MASK ();
	    bap[i] = (baptab+156)[mask + 4 * exp[i]];
	    i++;
	} while ((i < 3) || ((i < 7) && (exp[i] > exp[i-1])));
	fastleak = psd + fgain;
	slowleak = psd + sgain;

	while (i < 7) {
	    if (i < j) {
		if (exp[i+1] == exp[i] - 2)
		    lowcomp = 384;
		else if (lowcomp && (exp[i+1] > exp[i]))
		    lowcomp -= 64;
	    }
	    psd = 128 * exp[i];
	    UPDATE_LEAK ();
	    mask = ((fastleak + lowcomp < slowleak) ?
		    fastleak + lowcomp : slowleak);
	    COMPUTE_MASK ();
	    bap[i] = (baptab+156)[mask + 4 * exp[i]];
	    i++;
	}

	if (end == 7)	/* lfe channel */
	    return;

	do {
	    if (exp[i+1] == exp[i] - 2)
		lowcomp = 320;
	    else if (lowcomp && (exp[i+1] > exp[i]))
		lowcomp -= 64;
	    psd = 128 * exp[i];
	    UPDATE_LEAK ();
	    mask = ((fastleak + lowcomp < slowleak) ?
		    fastleak + lowcomp : slowleak);
	    COMPUTE_MASK ();
	    bap[i] = (baptab+156)[mask + 4 * exp[i]];
	    i++;
	} while (i < 20);

	while (lowcomp > 128) {		/* two iterations maximum */
	    lowcomp -= 128;
	    psd = 128 * exp[i];
	    UPDATE_LEAK ();
	    mask = ((fastleak + lowcomp < slowleak) ?
		    fastleak + lowcomp : slowleak);
	    COMPUTE_MASK ();
	    bap[i] = (baptab+156)[mask + 4 * exp[i]];
	    i++;
	}
	j = i;
    }

    do {
	int startband, endband;

	startband = j;
	endband = (bndtab[i-20] < end) ? bndtab[i-20] : end;
	psd = 128 * exp[j++];
	while (j < endband) {
	    int next, delta;

	    next = 128 * exp[j++];
	    delta = next - psd;
	    switch (delta >> 9) {
	    case -6: case -5: case -4: case -3: case -2:
		psd = next;
		break;
	    case -1:
		psd = next + latab[(-delta) >> 1];
		break;
	    case 0:
		psd += latab[delta >> 1];
		break;
	    }
	}
	/* minpsd = -289 */
	UPDATE_LEAK ();
	mask = (fastleak < slowleak) ? fastleak : slowleak;
	COMPUTE_MASK ();
	i++;
	j = startband;
	do {
	    /* max(mask+4*exp)=147=-(minpsd+fgain-deltba-snroffset)>>5+4*exp */
	    /* min(mask+4*exp)=-156=-(sgain-deltba-snroffset)>>5 */
	    bap[j] = (baptab+156)[mask + 4 * exp[j]];
	} while (++j < endband);
    } while (j < end);
}
