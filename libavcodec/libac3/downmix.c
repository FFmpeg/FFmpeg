/*
 *
 *  downmix.c
 *    
 *	Copyright (C) Aaron Holtzman - Sept 1999
 *
 *	Originally based on code by Yuqing Deng.
 *
 *  This file is part of ac3dec, a free Dolby AC-3 stream decoder.
 *	
 *  ac3dec is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *   
 *  ac3dec is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *   
 *  You should have received a copy of the GNU General Public License
 *  along with GNU Make; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA. 
 *
 *
 */

#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "ac3.h"
#include "ac3_internal.h"


#define CONVERT(acmod,output) (((output) << 3) + (acmod))

int downmix_init (int input, int flags, float * level, float clev, float slev)
{
    static uint8_t table[11][8] = {
	{AC3_CHANNEL,  AC3_DOLBY,  AC3_STEREO, AC3_STEREO,
	 AC3_STEREO,   AC3_STEREO, AC3_STEREO, AC3_STEREO},
	{AC3_MONO,     AC3_MONO,   AC3_MONO,   AC3_MONO,
	 AC3_MONO,     AC3_MONO,   AC3_MONO,   AC3_MONO},
	{AC3_CHANNEL,  AC3_DOLBY,  AC3_STEREO, AC3_STEREO,
	 AC3_STEREO,   AC3_STEREO, AC3_STEREO, AC3_STEREO},
	{AC3_CHANNEL,  AC3_DOLBY,  AC3_STEREO, AC3_3F,
	 AC3_STEREO,   AC3_3F,     AC3_STEREO, AC3_3F},
	{AC3_CHANNEL,  AC3_DOLBY,  AC3_STEREO, AC3_STEREO,
	 AC3_2F1R,     AC3_2F1R,   AC3_2F1R,   AC3_2F1R},
	{AC3_CHANNEL,  AC3_DOLBY,  AC3_STEREO, AC3_STEREO,
	 AC3_2F1R,     AC3_3F1R,   AC3_2F1R,   AC3_3F1R},
	{AC3_CHANNEL,  AC3_DOLBY,  AC3_STEREO, AC3_3F,
	 AC3_2F2R,     AC3_2F2R,   AC3_2F2R,   AC3_2F2R},
	{AC3_CHANNEL,  AC3_DOLBY,  AC3_STEREO, AC3_3F,
	 AC3_2F2R,     AC3_3F2R,   AC3_2F2R,   AC3_3F2R},
	{AC3_CHANNEL1, AC3_MONO,   AC3_MONO,   AC3_MONO,
	 AC3_MONO,     AC3_MONO,   AC3_MONO,   AC3_MONO},
	{AC3_CHANNEL2, AC3_MONO,   AC3_MONO,   AC3_MONO,
	 AC3_MONO,     AC3_MONO,   AC3_MONO,   AC3_MONO},
	{AC3_CHANNEL,  AC3_DOLBY,  AC3_STEREO, AC3_DOLBY,
	 AC3_DOLBY,    AC3_DOLBY,  AC3_DOLBY,  AC3_DOLBY}
    };
    int output;

    output = flags & AC3_CHANNEL_MASK;
    if (output > AC3_DOLBY)
	return -1;

    output = table[output][input & 7];

    if ((output == AC3_STEREO) &&
	((input == AC3_DOLBY) || ((input == AC3_3F) && (clev == LEVEL_3DB))))
	output = AC3_DOLBY;

    if (flags & AC3_ADJUST_LEVEL)
	switch (CONVERT (input & 7, output)) {

	case CONVERT (AC3_3F, AC3_MONO):
	    *level *= LEVEL_3DB / (1 + clev);
	    break;

	case CONVERT (AC3_STEREO, AC3_MONO):
	case CONVERT (AC3_2F2R, AC3_2F1R):
	case CONVERT (AC3_3F2R, AC3_3F1R):
	level_3db:
	    *level *= LEVEL_3DB;
	    break;

	case CONVERT (AC3_3F2R, AC3_2F1R):
	    if (clev < LEVEL_PLUS3DB - 1)
		goto level_3db;
	    // break thru
	case CONVERT (AC3_3F, AC3_STEREO):
	case CONVERT (AC3_3F1R, AC3_2F1R):
	case CONVERT (AC3_3F1R, AC3_2F2R):
	case CONVERT (AC3_3F2R, AC3_2F2R):
	    *level /= 1 + clev;
	    break;

	case CONVERT (AC3_2F1R, AC3_MONO):
	    *level *= LEVEL_PLUS3DB / (2 + slev);
	    break;

	case CONVERT (AC3_2F1R, AC3_STEREO):
	case CONVERT (AC3_3F1R, AC3_3F):
	    *level /= 1 + slev * LEVEL_3DB;
	    break;

	case CONVERT (AC3_3F1R, AC3_MONO):
	    *level *= LEVEL_3DB / (1 + clev + 0.5 * slev);
	    break;

	case CONVERT (AC3_3F1R, AC3_STEREO):
	    *level /= 1 + clev + slev * LEVEL_3DB;
	    break;

	case CONVERT (AC3_2F2R, AC3_MONO):
	    *level *= LEVEL_3DB / (1 + slev);
	    break;

	case CONVERT (AC3_2F2R, AC3_STEREO):
	case CONVERT (AC3_3F2R, AC3_3F):
	    *level /= (1 + slev);
	    break;

	case CONVERT (AC3_3F2R, AC3_MONO):
	    *level *= LEVEL_3DB / (1 + clev + slev);
	    break;

	case CONVERT (AC3_3F2R, AC3_STEREO):
	    *level /= 1 + clev + slev;
	    break;

	case CONVERT (AC3_MONO, AC3_DOLBY):
	    *level *= LEVEL_PLUS3DB;
	    break;

	case CONVERT (AC3_3F, AC3_DOLBY):
	case CONVERT (AC3_2F1R, AC3_DOLBY):
	    *level *= 1 / (1 + LEVEL_3DB);
	    break;

	case CONVERT (AC3_3F1R, AC3_DOLBY):
	case CONVERT (AC3_2F2R, AC3_DOLBY):
	    *level *= 1 / (1 + 2 * LEVEL_3DB);
	    break;

	case CONVERT (AC3_3F2R, AC3_DOLBY):
	    *level *= 1 / (1 + 3 * LEVEL_3DB);
	    break;
    }

    return output;
}

static void mix1to1 (float * samples, float level, float bias)
{
    int i;

    for (i = 0; i < 256; i++)
	samples[i] = samples[i] * level + bias;
}

static void move1to1 (float * src, float * dest, float level, float bias)
{
    int i;

    for (i = 0; i < 256; i++)
	dest[i] = src[i] * level + bias;
}

static void mix2to1 (float * samples, float level, float bias)
{
    int i;

    for (i = 0; i < 256; i++)
	samples[i] = (samples[i] + samples[i + 256]) * level + bias;
}

static void move2to1 (float * src, float * dest, float level, float bias)
{
    int i;

    for (i = 0; i < 256; i++)
	dest[i] = (src[i] + src[i + 256]) * level + bias;
}

static void mix3to1 (float * samples, float level, float clev, float bias)
{
    int i;

    for (i = 0; i < 256; i++)
	samples[i] = ((samples[i] + samples[i + 512]) * level +
		      samples[i + 256] * clev + bias);
}

static void mix21to1 (float * samples, float level, float slev, float bias)
{
    int i;

    for (i = 0; i < 256; i++)
	samples[i] = ((samples[i] + samples[i + 256]) * level +
		      samples[i + 512] * slev + bias);
}

static void mix31to1 (float * samples, float level, float clev, float slev,
		      float bias)
{
    int i;

    for (i = 0; i < 256; i++)
	samples[i] = ((samples[i] + samples[i + 512]) * level +
		      samples[i + 256] * clev + samples[i + 768] * slev +
		      bias);
}

static void mix22to1 (float * samples, float level, float slev, float bias)
{
    int i;

    for (i = 0; i < 256; i++)
	samples[i] = ((samples[i] + samples[i + 256]) * level +
		      (samples[i + 512] + samples[i + 768]) * slev + bias);
}

static void mix32to1 (float * samples, float level, float clev, float slev,
		      float bias)
{
    int i;

    for (i = 0; i < 256; i++)
	samples[i] = ((samples[i] + samples[i + 512]) * level +
		      samples[i + 256] * clev +
		      (samples[i + 768] + samples[i + 1024]) * slev + bias);
}

static void mix1to2 (float * src, float * dest, float level, float bias)
{
    int i;

    for (i = 0; i < 256; i++)
	dest[i] = src[i] = src[i] * level + bias;
}

static void mix3to2 (float * samples, float level, float clev, float bias)
{
    int i;
    float common;

    for (i = 0; i < 256; i++) {
	common = samples[i + 256] * clev + bias;
	samples[i] = samples[i] * level + common;
	samples[i + 256] = samples[i + 512] * level + common;
    }
}

static void mix21to2 (float * left, float * right, float level, float slev,
		      float bias)
{
    int i;
    float common;

    for (i = 0; i < 256; i++) {
	common = right[i + 256] * slev + bias;
	left[i] = left[i] * level + common;
	right[i] = right[i] * level + common;
    }
}

static void mix11to1 (float * front, float * rear, float level, float slev,
		      float bias)
{
    int i;

    for (i = 0; i < 256; i++)
	front[i] = front[i] * level + rear[i] * slev + bias;
}

static void mix31to2 (float * samples, float level, float clev, float slev,
		      float bias)
{
    int i;
    float common;

    for (i = 0; i < 256; i++) {
	common = samples[i + 256] * clev + samples[i + 768] * slev + bias;
	samples[i] = samples[i] * level + common;
	samples[i + 256] = samples[i + 512] * level + common;
    }
}

static void mix32to2 (float * samples, float level, float clev, float slev,
		      float bias)
{
    int i;
    float common;

    for (i = 0; i < 256; i++) {
	common = samples[i + 256] * clev + bias;
	samples[i] = samples[i] * level + common + samples[i + 768] * slev;
	samples[i + 256] = (samples[i + 512] * level + common +
			    samples[i + 1024] * slev);
    }
}

static void mix21toS (float * samples, float level, float level3db, float bias)
{
    int i;
    float surround;

    for (i = 0; i < 256; i++) {
	surround = samples[i + 512] * level3db;
	samples[i] = samples[i] * level - surround + bias;
	samples[i + 256] = samples[i + 256] * level + surround + bias;
    }
}

static void mix22toS (float * samples, float level, float level3db, float bias)
{
    int i;
    float surround;

    for (i = 0; i < 256; i++) {
	surround = (samples[i + 512] + samples[i + 768]) * level3db;
	samples[i] = samples[i] * level - surround + bias;
	samples[i + 256] = samples[i + 256] * level + surround + bias;
    }
}

static void mix31toS (float * samples, float level, float level3db, float bias)
{
    int i;
    float common, surround;

    for (i = 0; i < 256; i++) {
	common = samples[i + 256] * level3db + bias;
	surround = samples[i + 768] * level3db;
	samples[i] = samples[i] * level + common - surround;
	samples[i + 256] = samples[i + 512] * level + common + surround;
    }
}

static void mix32toS (float * samples, float level, float level3db, float bias)
{
    int i;
    float common, surround;

    for (i = 0; i < 256; i++) {
	common = samples[i + 256] * level3db + bias;
	surround = (samples[i + 768] + samples[i + 1024]) * level3db;
	samples[i] = samples[i] * level + common - surround;
	samples[i + 256] = samples[i + 512] * level + common + surround;
    }
}

void downmix (float * samples, int acmod, int output, float level, float bias,
	      float clev, float slev)
{
    switch (CONVERT (acmod, output & AC3_CHANNEL_MASK)) {

    case CONVERT (AC3_3F2R, AC3_3F2R):
	mix1to1 (samples + 1024, level, bias);
    case CONVERT (AC3_3F1R, AC3_3F1R):
    case CONVERT (AC3_2F2R, AC3_2F2R):
	mix1to1 (samples + 768, level, bias);
    case CONVERT (AC3_3F, AC3_3F):
    case CONVERT (AC3_2F1R, AC3_2F1R):
    mix_3to3:
	mix1to1 (samples + 512, level, bias);
    case CONVERT (AC3_CHANNEL, AC3_CHANNEL):
    case CONVERT (AC3_STEREO, AC3_STEREO):
    case CONVERT (AC3_STEREO, AC3_DOLBY):
    mix_2to2:
	mix1to1 (samples + 256, level, bias);
    case CONVERT (AC3_CHANNEL, AC3_CHANNEL1):
    case CONVERT (AC3_MONO, AC3_MONO):
	mix1to1 (samples, level, bias);
	break;

    case CONVERT (AC3_CHANNEL, AC3_CHANNEL2):
    mix_1to1_b:
	mix1to1 (samples + 256, level, bias);
	break;

    case CONVERT (AC3_STEREO, AC3_MONO):
    mix_2to1:
	mix2to1 (samples, level * LEVEL_3DB, bias);
	break;

    case CONVERT (AC3_2F1R, AC3_MONO):
	if (slev == 0)
	    goto mix_2to1;
	mix21to1 (samples, level * LEVEL_3DB, level * slev * LEVEL_3DB, bias);
	break;

    case CONVERT (AC3_2F2R, AC3_MONO):
	if (slev == 0)
	    goto mix_2to1;
	mix22to1 (samples, level * LEVEL_3DB, level * slev * LEVEL_3DB, bias);
	break;

    case CONVERT (AC3_3F, AC3_MONO):
    mix_3to1:
	mix3to1 (samples, level * LEVEL_3DB, level * clev * LEVEL_PLUS3DB,
		 bias);
	break;

    case CONVERT (AC3_3F1R, AC3_MONO):
	if (slev == 0)
	    goto mix_3to1;
	mix31to1 (samples, level * LEVEL_3DB, level * clev * LEVEL_PLUS3DB,
		  level * slev * LEVEL_3DB, bias);
	break;

    case CONVERT (AC3_3F2R, AC3_MONO):
	if (slev == 0)
	    goto mix_3to1;
	mix32to1 (samples, level * LEVEL_3DB, level * clev * LEVEL_PLUS3DB,
		  level * slev * LEVEL_3DB, bias);
	break;

    case CONVERT (AC3_CHANNEL, AC3_MONO):
	mix2to1 (samples, level * LEVEL_6DB, bias);
	break;

    case CONVERT (AC3_MONO, AC3_DOLBY):
	mix1to2 (samples, samples + 256, level * LEVEL_3DB, bias);
	break;

    case CONVERT (AC3_3F, AC3_DOLBY):
	clev = LEVEL_3DB;
    case CONVERT (AC3_3F, AC3_STEREO):
    mix_3to2:
	mix3to2 (samples, level, level * clev, bias);
	break;

    case CONVERT (AC3_2F1R, AC3_DOLBY):
	mix21toS (samples, level, level * LEVEL_3DB, bias);
	break;

    case CONVERT (AC3_3F1R, AC3_DOLBY):
	mix31toS (samples, level, level * LEVEL_3DB, bias);
	break;

    case CONVERT (AC3_2F2R, AC3_DOLBY):
	mix22toS (samples, level, level * LEVEL_3DB, bias);
	break;

    case CONVERT (AC3_3F2R, AC3_DOLBY):
	mix32toS (samples, level, level * LEVEL_3DB, bias);
	break;

    case CONVERT (AC3_2F1R, AC3_STEREO):
	if (slev == 0)
	    goto mix_2to2;
	mix21to2 (samples, samples + 256, level, level * slev * LEVEL_3DB,
		  bias);
	break;

    case CONVERT (AC3_3F1R, AC3_STEREO):
	if (slev == 0)
	    goto mix_3to2;
	mix31to2 (samples, level, level * clev, level * slev * LEVEL_3DB,
		  bias);
	break;

    case CONVERT (AC3_2F2R, AC3_STEREO):
	if (slev == 0)
	    goto mix_2to2;
	mix11to1 (samples, samples + 512, level, level * slev, bias);
	mix11to1 (samples + 256, samples + 768, level, level * slev, bias);
	break;

    case CONVERT (AC3_3F2R, AC3_STEREO):
	if (slev == 0)
	    goto mix_3to2;
	mix32to2 (samples, level, level * clev, level * slev, bias);
	break;

    case CONVERT (AC3_3F1R, AC3_3F):
	if (slev == 0)
	    goto mix_3to3;
	mix21to2 (samples, samples + 512, level, level * slev * LEVEL_3DB,
		  bias);

    case CONVERT (AC3_3F2R, AC3_3F):
	if (slev == 0)
	    goto mix_3to3;
	mix11to1 (samples, samples + 768, level, level * slev, bias);
	mix11to1 (samples + 512, samples + 1024, level, level * slev, bias);
	goto mix_1to1_b;

    case CONVERT (AC3_2F1R, AC3_2F2R):
	mix1to2 (samples + 512, samples + 768, level * LEVEL_3DB, bias);
	goto mix_2to2;

    case CONVERT (AC3_3F1R, AC3_3F2R):
	mix1to2 (samples + 768, samples + 1024, level * LEVEL_3DB, bias);
	goto mix_3to3;

    case CONVERT (AC3_2F2R, AC3_2F1R):
	mix2to1 (samples + 512, level * LEVEL_3DB, bias);
	goto mix_2to2;

    case CONVERT (AC3_3F2R, AC3_3F1R):
	mix2to1 (samples + 768, level * LEVEL_3DB, bias);
	goto mix_3to3;

    case CONVERT (AC3_3F1R, AC3_2F2R):
	mix3to2 (samples, level, level * clev, bias);
	mix1to2 (samples + 768, samples + 512, level * LEVEL_3DB, bias);
	break;

    case CONVERT (AC3_3F1R, AC3_2F1R):
	mix3to2 (samples, level, level * clev, bias);
	move1to1 (samples + 768, samples + 512, level, bias);
	break;

    case CONVERT (AC3_3F2R, AC3_2F1R):
	mix3to2 (samples, level, level * clev, bias);
	move2to1 (samples + 768, samples + 512, level * LEVEL_3DB, bias);
	break;

    case CONVERT (AC3_3F2R, AC3_2F2R):
	mix3to2 (samples, level, level * clev, bias);
	move1to1 (samples + 768, samples + 512, level, bias);
	move1to1 (samples + 1024, samples + 768, level, bias);
	break;

    }
}
