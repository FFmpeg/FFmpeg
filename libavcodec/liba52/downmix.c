/*
 * downmix.c
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

#define CONVERT(acmod,output) (((output) << 3) + (acmod))

int a52_downmix_init (int input, int flags, level_t * level,
		      level_t clev, level_t slev)
{
    static uint8_t table[11][8] = {
	{A52_CHANNEL,	A52_DOLBY,	A52_STEREO,	A52_STEREO,
	 A52_STEREO,	A52_STEREO,	A52_STEREO,	A52_STEREO},
	{A52_MONO,	A52_MONO,	A52_MONO,	A52_MONO,
	 A52_MONO,	A52_MONO,	A52_MONO,	A52_MONO},
	{A52_CHANNEL,	A52_DOLBY,	A52_STEREO,	A52_STEREO,
	 A52_STEREO,	A52_STEREO,	A52_STEREO,	A52_STEREO},
	{A52_CHANNEL,	A52_DOLBY,	A52_STEREO,	A52_3F,
	 A52_STEREO,	A52_3F,		A52_STEREO,	A52_3F},
	{A52_CHANNEL,	A52_DOLBY,	A52_STEREO,	A52_STEREO,
	 A52_2F1R,	A52_2F1R,	A52_2F1R,	A52_2F1R},
	{A52_CHANNEL,	A52_DOLBY,	A52_STEREO,	A52_STEREO,
	 A52_2F1R,	A52_3F1R,	A52_2F1R,	A52_3F1R},
	{A52_CHANNEL,	A52_DOLBY,	A52_STEREO,	A52_3F,
	 A52_2F2R,	A52_2F2R,	A52_2F2R,	A52_2F2R},
	{A52_CHANNEL,	A52_DOLBY,	A52_STEREO,	A52_3F,
	 A52_2F2R,	A52_3F2R,	A52_2F2R,	A52_3F2R},
	{A52_CHANNEL1,	A52_MONO,	A52_MONO,	A52_MONO,
	 A52_MONO,	A52_MONO,	A52_MONO,	A52_MONO},
	{A52_CHANNEL2,	A52_MONO,	A52_MONO,	A52_MONO,
	 A52_MONO,	A52_MONO,	A52_MONO,	A52_MONO},
	{A52_CHANNEL,	A52_DOLBY,	A52_STEREO,	A52_DOLBY,
	 A52_DOLBY,	A52_DOLBY,	A52_DOLBY,	A52_DOLBY}
    };
    int output;

    output = flags & A52_CHANNEL_MASK;
    if (output > A52_DOLBY)
	return -1;

    output = table[output][input & 7];

    if (output == A52_STEREO &&
	(input == A52_DOLBY || (input == A52_3F && clev == LEVEL (LEVEL_3DB))))
	output = A52_DOLBY;

    if (flags & A52_ADJUST_LEVEL) {
	level_t adjust;

	switch (CONVERT (input & 7, output)) {

	case CONVERT (A52_3F, A52_MONO):
	    adjust = DIV (LEVEL_3DB, LEVEL (1) + clev);
	    break;

	case CONVERT (A52_STEREO, A52_MONO):
	case CONVERT (A52_2F2R, A52_2F1R):
	case CONVERT (A52_3F2R, A52_3F1R):
	level_3db:
	    adjust = LEVEL (LEVEL_3DB);
	    break;

	case CONVERT (A52_3F2R, A52_2F1R):
	    if (clev < LEVEL (LEVEL_PLUS3DB - 1))
		goto level_3db;
	    /* break thru */
	case CONVERT (A52_3F, A52_STEREO):
	case CONVERT (A52_3F1R, A52_2F1R):
	case CONVERT (A52_3F1R, A52_2F2R):
	case CONVERT (A52_3F2R, A52_2F2R):
	    adjust = DIV (1, LEVEL (1) + clev);
	    break;

	case CONVERT (A52_2F1R, A52_MONO):
	    adjust = DIV (LEVEL_PLUS3DB, LEVEL (2) + slev);
	    break;

	case CONVERT (A52_2F1R, A52_STEREO):
	case CONVERT (A52_3F1R, A52_3F):
	    adjust = DIV (1, LEVEL (1) + MUL_C (slev, LEVEL_3DB));
	    break;

	case CONVERT (A52_3F1R, A52_MONO):
	    adjust = DIV (LEVEL_3DB, LEVEL (1) + clev + MUL_C (slev, 0.5));
	    break;

	case CONVERT (A52_3F1R, A52_STEREO):
	    adjust = DIV (1, LEVEL (1) + clev + MUL_C (slev, LEVEL_3DB));
	    break;

	case CONVERT (A52_2F2R, A52_MONO):
	    adjust = DIV (LEVEL_3DB, LEVEL (1) + slev);
	    break;

	case CONVERT (A52_2F2R, A52_STEREO):
	case CONVERT (A52_3F2R, A52_3F):
	    adjust = DIV (1, LEVEL (1) + slev);
	    break;

	case CONVERT (A52_3F2R, A52_MONO):
	    adjust = DIV (LEVEL_3DB, LEVEL (1) + clev + slev);
	    break;

	case CONVERT (A52_3F2R, A52_STEREO):
	    adjust = DIV (1, LEVEL (1) + clev + slev);
	    break;

	case CONVERT (A52_MONO, A52_DOLBY):
	    adjust = LEVEL (LEVEL_PLUS3DB);
	    break;

	case CONVERT (A52_3F, A52_DOLBY):
	case CONVERT (A52_2F1R, A52_DOLBY):
	    adjust = LEVEL (1 / (1 + LEVEL_3DB));
	    break;

	case CONVERT (A52_3F1R, A52_DOLBY):
	case CONVERT (A52_2F2R, A52_DOLBY):
	    adjust = LEVEL (1 / (1 + 2 * LEVEL_3DB));
	    break;

	case CONVERT (A52_3F2R, A52_DOLBY):
	    adjust = LEVEL (1 / (1 + 3 * LEVEL_3DB));
	    break;

	default:
	    return output;
	}

	*level = MUL_L (*level, adjust);
    }

    return output;
}

int a52_downmix_coeff (level_t * coeff, int acmod, int output, level_t level,
		       level_t clev, level_t slev)
{
    level_t level_3db;

    level_3db = MUL_C (level, LEVEL_3DB);

    switch (CONVERT (acmod, output & A52_CHANNEL_MASK)) {

    case CONVERT (A52_CHANNEL, A52_CHANNEL):
    case CONVERT (A52_MONO, A52_MONO):
    case CONVERT (A52_STEREO, A52_STEREO):
    case CONVERT (A52_3F, A52_3F):
    case CONVERT (A52_2F1R, A52_2F1R):
    case CONVERT (A52_3F1R, A52_3F1R):
    case CONVERT (A52_2F2R, A52_2F2R):
    case CONVERT (A52_3F2R, A52_3F2R):
    case CONVERT (A52_STEREO, A52_DOLBY):
	coeff[0] = coeff[1] = coeff[2] = coeff[3] = coeff[4] = level;
	return 0;

    case CONVERT (A52_CHANNEL, A52_MONO):
	coeff[0] = coeff[1] = MUL_C (level, LEVEL_6DB);
	return 3;

    case CONVERT (A52_STEREO, A52_MONO):
	coeff[0] = coeff[1] = level_3db;
	return 3;

    case CONVERT (A52_3F, A52_MONO):
	coeff[0] = coeff[2] = level_3db;
	coeff[1] = MUL_C (MUL_L (level_3db, clev), LEVEL_PLUS6DB);
	return 7;

    case CONVERT (A52_2F1R, A52_MONO):
	coeff[0] = coeff[1] = level_3db;
	coeff[2] = MUL_L (level_3db, slev);
	return 7;

    case CONVERT (A52_2F2R, A52_MONO):
	coeff[0] = coeff[1] = level_3db;
	coeff[2] = coeff[3] = MUL_L (level_3db, slev);
	return 15;

    case CONVERT (A52_3F1R, A52_MONO):
	coeff[0] = coeff[2] = level_3db;
	coeff[1] = MUL_C (MUL_L (level_3db, clev), LEVEL_PLUS6DB);
	coeff[3] = MUL_L (level_3db, slev);
	return 15;

    case CONVERT (A52_3F2R, A52_MONO):
	coeff[0] = coeff[2] = level_3db;
	coeff[1] = MUL_C (MUL_L (level_3db, clev), LEVEL_PLUS6DB);
	coeff[3] = coeff[4] = MUL_L (level_3db, slev);
	return 31;

    case CONVERT (A52_MONO, A52_DOLBY):
	coeff[0] = level_3db;
	return 0;

    case CONVERT (A52_3F, A52_DOLBY):
	coeff[0] = coeff[2] = coeff[3] = coeff[4] = level;
	coeff[1] = level_3db;
	return 7;

    case CONVERT (A52_3F, A52_STEREO):
    case CONVERT (A52_3F1R, A52_2F1R):
    case CONVERT (A52_3F2R, A52_2F2R):
	coeff[0] = coeff[2] = coeff[3] = coeff[4] = level;
	coeff[1] = MUL_L (level, clev);
	return 7;

    case CONVERT (A52_2F1R, A52_DOLBY):
	coeff[0] = coeff[1] = level;
	coeff[2] = level_3db;
	return 7;

    case CONVERT (A52_2F1R, A52_STEREO):
	coeff[0] = coeff[1] = level;
	coeff[2] = MUL_L (level_3db, slev);
	return 7;

    case CONVERT (A52_3F1R, A52_DOLBY):
	coeff[0] = coeff[2] = level;
	coeff[1] = coeff[3] = level_3db;
	return 15;

    case CONVERT (A52_3F1R, A52_STEREO):
	coeff[0] = coeff[2] = level;
	coeff[1] = MUL_L (level, clev);
	coeff[3] = MUL_L (level_3db, slev);
	return 15;

    case CONVERT (A52_2F2R, A52_DOLBY):
	coeff[0] = coeff[1] = level;
	coeff[2] = coeff[3] = level_3db;
	return 15;

    case CONVERT (A52_2F2R, A52_STEREO):
	coeff[0] = coeff[1] = level;
	coeff[2] = coeff[3] = MUL_L (level, slev);
	return 15;

    case CONVERT (A52_3F2R, A52_DOLBY):
	coeff[0] = coeff[2] = level;
	coeff[1] = coeff[3] = coeff[4] = level_3db;
	return 31;

    case CONVERT (A52_3F2R, A52_2F1R):
	coeff[0] = coeff[2] = level;
	coeff[1] = MUL_L (level, clev);
	coeff[3] = coeff[4] = level_3db;
	return 31;

    case CONVERT (A52_3F2R, A52_STEREO):
	coeff[0] = coeff[2] = level;
	coeff[1] = MUL_L (level, clev);
	coeff[3] = coeff[4] = MUL_L (level, slev);
	return 31;

    case CONVERT (A52_3F1R, A52_3F):
	coeff[0] = coeff[1] = coeff[2] = level;
	coeff[3] = MUL_L (level_3db, slev);
	return 13;

    case CONVERT (A52_3F2R, A52_3F):
	coeff[0] = coeff[1] = coeff[2] = level;
	coeff[3] = coeff[4] = MUL_L (level, slev);
	return 29;

    case CONVERT (A52_2F2R, A52_2F1R):
	coeff[0] = coeff[1] = level;
	coeff[2] = coeff[3] = level_3db;
	return 12;

    case CONVERT (A52_3F2R, A52_3F1R):
	coeff[0] = coeff[1] = coeff[2] = level;
	coeff[3] = coeff[4] = level_3db;
	return 24;

    case CONVERT (A52_2F1R, A52_2F2R):
	coeff[0] = coeff[1] = level;
	coeff[2] = level_3db;
	return 0;

    case CONVERT (A52_3F1R, A52_2F2R):
	coeff[0] = coeff[2] = level;
	coeff[1] = MUL_L (level, clev);
	coeff[3] = level_3db;
	return 7;

    case CONVERT (A52_3F1R, A52_3F2R):
	coeff[0] = coeff[1] = coeff[2] = level;
	coeff[3] = level_3db;
	return 0;

    case CONVERT (A52_CHANNEL, A52_CHANNEL1):
	coeff[0] = level;
	coeff[1] = 0;
	return 0;

    case CONVERT (A52_CHANNEL, A52_CHANNEL2):
	coeff[0] = 0;
	coeff[1] = level;
	return 0;
    }

    return -1;	/* NOTREACHED */
}

static void mix2to1 (sample_t * dest, sample_t * src, sample_t bias)
{
    int i;

    for (i = 0; i < 256; i++)
	dest[i] += BIAS (src[i]);
}

static void mix3to1 (sample_t * samples, sample_t bias)
{
    int i;

    for (i = 0; i < 256; i++)
	samples[i] += BIAS (samples[i + 256] + samples[i + 512]);
}

static void mix4to1 (sample_t * samples, sample_t bias)
{
    int i;

    for (i = 0; i < 256; i++)
	samples[i] += BIAS (samples[i + 256] + samples[i + 512] +
			    samples[i + 768]);
}

static void mix5to1 (sample_t * samples, sample_t bias)
{
    int i;

    for (i = 0; i < 256; i++)
	samples[i] += BIAS (samples[i + 256] + samples[i + 512] +
			    samples[i + 768] + samples[i + 1024]);
}

static void mix3to2 (sample_t * samples, sample_t bias)
{
    int i;
    sample_t common;

    for (i = 0; i < 256; i++) {
	common = BIAS (samples[i + 256]);
	samples[i] += common;
	samples[i + 256] = samples[i + 512] + common;
    }
}

static void mix21to2 (sample_t * left, sample_t * right, sample_t bias)
{
    int i;
    sample_t common;

    for (i = 0; i < 256; i++) {
	common = BIAS (right[i + 256]);
	left[i] += common;
	right[i] += common;
    }
}

static void mix21toS (sample_t * samples, sample_t bias)
{
    int i;
    sample_t surround;

    for (i = 0; i < 256; i++) {
	surround = samples[i + 512];
	samples[i] += BIAS (-surround);
	samples[i + 256] += BIAS (surround);
    }
}

static void mix31to2 (sample_t * samples, sample_t bias)
{
    int i;
    sample_t common;

    for (i = 0; i < 256; i++) {
	common = BIAS (samples[i + 256] + samples[i + 768]);
	samples[i] += common;
	samples[i + 256] = samples[i + 512] + common;
    }
}

static void mix31toS (sample_t * samples, sample_t bias)
{
    int i;
    sample_t common, surround;

    for (i = 0; i < 256; i++) {
	common = BIAS (samples[i + 256]);
	surround = samples[i + 768];
	samples[i] += common - surround;
	samples[i + 256] = samples[i + 512] + common + surround;
    }
}

static void mix22toS (sample_t * samples, sample_t bias)
{
    int i;
    sample_t surround;

    for (i = 0; i < 256; i++) {
	surround = samples[i + 512] + samples[i + 768];
	samples[i] += BIAS (-surround);
	samples[i + 256] += BIAS (surround);
    }
}

static void mix32to2 (sample_t * samples, sample_t bias)
{
    int i;
    sample_t common;

    for (i = 0; i < 256; i++) {
	common = BIAS (samples[i + 256]);
	samples[i] += common + samples[i + 768];
	samples[i + 256] = common + samples[i + 512] + samples[i + 1024];
    }
}

static void mix32toS (sample_t * samples, sample_t bias)
{
    int i;
    sample_t common, surround;

    for (i = 0; i < 256; i++) {
	common = BIAS (samples[i + 256]);
	surround = samples[i + 768] + samples[i + 1024];
	samples[i] += common - surround;
	samples[i + 256] = samples[i + 512] + common + surround;
    }
}

static void move2to1 (sample_t * src, sample_t * dest, sample_t bias)
{
    int i;

    for (i = 0; i < 256; i++)
	dest[i] = BIAS (src[i] + src[i + 256]);
}

static void zero (sample_t * samples)
{
    int i;

    for (i = 0; i < 256; i++)
	samples[i] = 0;
}

void a52_downmix (sample_t * samples, int acmod, int output, sample_t bias,
		  level_t clev, level_t slev)
{
    switch (CONVERT (acmod, output & A52_CHANNEL_MASK)) {

    case CONVERT (A52_CHANNEL, A52_CHANNEL2):
	memcpy (samples, samples + 256, 256 * sizeof (sample_t));
	break;

    case CONVERT (A52_CHANNEL, A52_MONO):
    case CONVERT (A52_STEREO, A52_MONO):
    mix_2to1:
	mix2to1 (samples, samples + 256, bias);
	break;

    case CONVERT (A52_2F1R, A52_MONO):
	if (slev == 0)
	    goto mix_2to1;
    case CONVERT (A52_3F, A52_MONO):
    mix_3to1:
	mix3to1 (samples, bias);
	break;

    case CONVERT (A52_3F1R, A52_MONO):
	if (slev == 0)
	    goto mix_3to1;
    case CONVERT (A52_2F2R, A52_MONO):
	if (slev == 0)
	    goto mix_2to1;
	mix4to1 (samples, bias);
	break;

    case CONVERT (A52_3F2R, A52_MONO):
	if (slev == 0)
	    goto mix_3to1;
	mix5to1 (samples, bias);
	break;

    case CONVERT (A52_MONO, A52_DOLBY):
	memcpy (samples + 256, samples, 256 * sizeof (sample_t));
	break;

    case CONVERT (A52_3F, A52_STEREO):
    case CONVERT (A52_3F, A52_DOLBY):
    mix_3to2:
	mix3to2 (samples, bias);
	break;

    case CONVERT (A52_2F1R, A52_STEREO):
	if (slev == 0)
	    break;
	mix21to2 (samples, samples + 256, bias);
	break;

    case CONVERT (A52_2F1R, A52_DOLBY):
	mix21toS (samples, bias);
	break;

    case CONVERT (A52_3F1R, A52_STEREO):
	if (slev == 0)
	    goto mix_3to2;
	mix31to2 (samples, bias);
	break;

    case CONVERT (A52_3F1R, A52_DOLBY):
	mix31toS (samples, bias);
	break;

    case CONVERT (A52_2F2R, A52_STEREO):
	if (slev == 0)
	    break;
	mix2to1 (samples, samples + 512, bias);
	mix2to1 (samples + 256, samples + 768, bias);
	break;

    case CONVERT (A52_2F2R, A52_DOLBY):
	mix22toS (samples, bias);
	break;

    case CONVERT (A52_3F2R, A52_STEREO):
	if (slev == 0)
	    goto mix_3to2;
	mix32to2 (samples, bias);
	break;

    case CONVERT (A52_3F2R, A52_DOLBY):
	mix32toS (samples, bias);
	break;

    case CONVERT (A52_3F1R, A52_3F):
	if (slev == 0)
	    break;
	mix21to2 (samples, samples + 512, bias);
	break;

    case CONVERT (A52_3F2R, A52_3F):
	if (slev == 0)
	    break;
	mix2to1 (samples, samples + 768, bias);
	mix2to1 (samples + 512, samples + 1024, bias);
	break;

    case CONVERT (A52_3F1R, A52_2F1R):
	mix3to2 (samples, bias);
	memcpy (samples + 512, samples + 768, 256 * sizeof (sample_t));
	break;

    case CONVERT (A52_2F2R, A52_2F1R):
	mix2to1 (samples + 512, samples + 768, bias);
	break;

    case CONVERT (A52_3F2R, A52_2F1R):
	mix3to2 (samples, bias);
	move2to1 (samples + 768, samples + 512, bias);
	break;

    case CONVERT (A52_3F2R, A52_3F1R):
	mix2to1 (samples + 768, samples + 1024, bias);
	break;

    case CONVERT (A52_2F1R, A52_2F2R):
	memcpy (samples + 768, samples + 512, 256 * sizeof (sample_t));
	break;

    case CONVERT (A52_3F1R, A52_2F2R):
	mix3to2 (samples, bias);
	memcpy (samples + 512, samples + 768, 256 * sizeof (sample_t));
	break;

    case CONVERT (A52_3F2R, A52_2F2R):
	mix3to2 (samples, bias);
	memcpy (samples + 512, samples + 768, 256 * sizeof (sample_t));
	memcpy (samples + 768, samples + 1024, 256 * sizeof (sample_t));
	break;

    case CONVERT (A52_3F1R, A52_3F2R):
	memcpy (samples + 1024, samples + 768, 256 * sizeof (sample_t));
	break;
    }
}

void a52_upmix (sample_t * samples, int acmod, int output)
{
    switch (CONVERT (acmod, output & A52_CHANNEL_MASK)) {

    case CONVERT (A52_CHANNEL, A52_CHANNEL2):
	memcpy (samples + 256, samples, 256 * sizeof (sample_t));
	break;

    case CONVERT (A52_3F2R, A52_MONO):
	zero (samples + 1024);
    case CONVERT (A52_3F1R, A52_MONO):
    case CONVERT (A52_2F2R, A52_MONO):
	zero (samples + 768);
    case CONVERT (A52_3F, A52_MONO):
    case CONVERT (A52_2F1R, A52_MONO):
	zero (samples + 512);
    case CONVERT (A52_CHANNEL, A52_MONO):
    case CONVERT (A52_STEREO, A52_MONO):
	zero (samples + 256);
	break;

    case CONVERT (A52_3F2R, A52_STEREO):
    case CONVERT (A52_3F2R, A52_DOLBY):
	zero (samples + 1024);
    case CONVERT (A52_3F1R, A52_STEREO):
    case CONVERT (A52_3F1R, A52_DOLBY):
	zero (samples + 768);
    case CONVERT (A52_3F, A52_STEREO):
    case CONVERT (A52_3F, A52_DOLBY):
    mix_3to2:
	memcpy (samples + 512, samples + 256, 256 * sizeof (sample_t));
	zero (samples + 256);
	break;

    case CONVERT (A52_2F2R, A52_STEREO):
    case CONVERT (A52_2F2R, A52_DOLBY):
	zero (samples + 768);
    case CONVERT (A52_2F1R, A52_STEREO):
    case CONVERT (A52_2F1R, A52_DOLBY):
	zero (samples + 512);
	break;

    case CONVERT (A52_3F2R, A52_3F):
	zero (samples + 1024);
    case CONVERT (A52_3F1R, A52_3F):
    case CONVERT (A52_2F2R, A52_2F1R):
	zero (samples + 768);
	break;

    case CONVERT (A52_3F2R, A52_3F1R):
	zero (samples + 1024);
	break;

    case CONVERT (A52_3F2R, A52_2F1R):
	zero (samples + 1024);
    case CONVERT (A52_3F1R, A52_2F1R):
    mix_31to21:
	memcpy (samples + 768, samples + 512, 256 * sizeof (sample_t));
	goto mix_3to2;

    case CONVERT (A52_3F2R, A52_2F2R):
	memcpy (samples + 1024, samples + 768, 256 * sizeof (sample_t));
	goto mix_31to21;
    }
}
