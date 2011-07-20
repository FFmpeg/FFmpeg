/*
 * Various fixed-point math operations
 *
 * Copyright (c) 2008 Vladimir Voroshilov
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <inttypes.h>
#include <limits.h>
#include <assert.h>

#include "avcodec.h"
#include "celp_math.h"

/**
 * Cosine table: base_cos[i] = (1<<15) * cos(i*PI/64)
 */
static const int16_t tab_cos[65] =
{
  32767,  32738,  32617,  32421,  32145,  31793,  31364,  30860,
  30280,  29629,  28905,  28113,  27252,  26326,  25336,  24285,
  23176,  22011,  20793,  19525,  18210,  16851,  15451,  14014,
  12543,  11043,   9515,   7965,   6395,   4810,   3214,   1609,
      1,  -1607,  -3211,  -4808,  -6393,  -7962,  -9513, -11040,
 -12541, -14012, -15449, -16848, -18207, -19523, -20791, -22009,
 -23174, -24283, -25334, -26324, -27250, -28111, -28904, -29627,
 -30279, -30858, -31363, -31792, -32144, -32419, -32616, -32736, -32768,
};

static const uint16_t exp2a[]=
{
     0,  1435,  2901,  4400,  5931,  7496,  9096, 10730,
 12400, 14106, 15850, 17632, 19454, 21315, 23216, 25160,
 27146, 29175, 31249, 33368, 35534, 37747, 40009, 42320,
 44682, 47095, 49562, 52082, 54657, 57289, 59979, 62727,
};

static const uint16_t exp2b[]=
{
     3,   712,  1424,  2134,  2845,  3557,  4270,  4982,
  5696,  6409,  7124,  7839,  8554,  9270,  9986, 10704,
 11421, 12138, 12857, 13576, 14295, 15014, 15734, 16455,
 17176, 17898, 18620, 19343, 20066, 20790, 21514, 22238,
};

int16_t ff_cos(uint16_t arg)
{
    uint8_t offset= arg;
    uint8_t ind = arg >> 8;

    assert(arg <= 0x3fff);

    return tab_cos[ind] + (offset * (tab_cos[ind+1] - tab_cos[ind]) >> 8);
}

int ff_exp2(uint16_t power)
{
    unsigned int result= exp2a[power>>10] + 0x10000;

    assert(power <= 0x7fff);

    result= (result<<3) + ((result*exp2b[(power>>5)&31])>>17);
    return result + ((result*(power&31)*89)>>22);
}

/**
 * Table used to compute log2(x)
 *
 * tab_log2[i] = (1<<15) * log2(1 + i/32), i=0..32
 */
static const uint16_t tab_log2[33] =
{
      4,   1459,   2870,   4240,   5572,   6867,   8127,   9355,
  10552,  11719,  12858,  13971,  15057,  16120,  17158,  18175,
  19170,  20145,  21100,  22036,  22954,  23854,  24738,  25605,
  26457,  27294,  28116,  28924,  29719,  30500,  31269,  32025,  32769,
};

int ff_log2(uint32_t value)
{
    uint8_t  power_int;
    uint8_t  frac_x0;
    uint16_t frac_dx;

    // Stripping zeros from beginning
    power_int = av_log2(value);
    value <<= (31 - power_int);

    // b31 is always non-zero now
    frac_x0 = (value & 0x7c000000) >> 26; // b26-b31 and [32..63] -> [0..31]
    frac_dx = (value & 0x03fff800) >> 11;

    value = tab_log2[frac_x0];
    value += (frac_dx * (tab_log2[frac_x0+1] - tab_log2[frac_x0])) >> 15;

    return (power_int << 15) + value;
}

float ff_dot_productf(const float* a, const float* b, int length)
{
    float sum = 0;
    int i;

    for(i=0; i<length; i++)
        sum += a[i] * b[i];

    return sum;
}
