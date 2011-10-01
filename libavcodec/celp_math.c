/*
 * Various fixed-point math operations
 *
 * Copyright (c) 2008 Vladimir Voroshilov
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <inttypes.h>
#include <limits.h>
#include <assert.h>

#include "avcodec.h"
#include "mathops.h"
#include "celp_math.h"

#ifdef G729_BITEXACT
/**
 * Cosine table: base_cos[i] = (1<<15) * cos(i*PI/64)
 */
static const int16_t base_cos[64] =
{
  32767,  32729,  32610,  32413,  32138,  31786,  31357,  30853,
  30274,  29622,  28899,  28106,  27246,  26320,  25330,  24279,
  23170,  22006,  20788,  19520,  18205,  16846,  15447,  14010,
  12540,  11039,   9512,   7962,   6393,   4808,   3212,   1608,
      0,  -1608,  -3212,  -4808,  -6393,  -7962,  -9512, -11039,
 -12540, -14010, -15447, -16846, -18205, -19520, -20788, -22006,
 -23170, -24279, -25330, -26320, -27246, -28106, -28899, -29622,
 -30274, -30853, -31357, -31786, -32138, -32413, -32610, -32729
};

/**
 * Slope used to compute cos(x)
 *
 * cos(ind*64+offset) = base_cos[ind]+offset*slope_cos[ind]
 * values multiplied by 1<<19
 */
static const int16_t slope_cos[64] =
{
   -632,  -1893,  -3150,  -4399,  -5638,  -6863,  -8072,  -9261,
 -10428, -11570, -12684, -13767, -14817, -15832, -16808, -17744,
 -18637, -19486, -20287, -21039, -21741, -22390, -22986, -23526,
 -24009, -24435, -24801, -25108, -25354, -25540, -25664, -25726,
 -25726, -25664, -25540, -25354, -25108, -24801, -24435, -24009,
 -23526, -22986, -22390, -21741, -21039, -20287, -19486, -18637,
 -17744, -16808, -15832, -14817, -13767, -12684, -11570, -10428,
  -9261,  -8072,  -6863,  -5638,  -4399,  -3150,  -1893,   -632
};

/**
 * Table used to compute exp2(x)
 *
 * tab_exp2[i] = (1<<14) * exp2(i/32) = 2^(i/32) i=0..32
 */
static const uint16_t tab_exp2[33] =
{
  16384, 16743, 17109, 17484, 17867, 18258, 18658, 19066, 19484, 19911,
  20347, 20792, 21247, 21713, 22188, 22674, 23170, 23678, 24196, 24726,
  25268, 25821, 26386, 26964, 27554, 28158, 28774, 29405, 30048, 30706,
  31379, 32066, 32767
};

int16_t ff_cos(uint16_t arg)
{
    uint8_t offset= arg;
    uint8_t ind = arg >> 8;

    assert(arg < 0x4000);

    return FFMAX(base_cos[ind] + ((slope_cos[ind] * offset) >> 12), -0x8000);
}

int ff_exp2(uint16_t power)
{
    uint16_t frac_x0;
    uint16_t frac_dx;
    int result;

    assert(power <= 0x7fff);

    frac_x0 = power >> 10;
    frac_dx = (power & 0x03ff) << 5;

    result = tab_exp2[frac_x0] << 15;
    result += frac_dx * (tab_exp2[frac_x0+1] - tab_exp2[frac_x0]);

    return result >> 10;
}

#else // G729_BITEXACT

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

#endif // else G729_BITEXACT

/**
 * Table used to compute log2(x)
 *
 * tab_log2[i] = (1<<15) * log2(1 + i/32), i=0..32
 */
static const uint16_t tab_log2[33] =
{
#ifdef G729_BITEXACT
      0,   1455,   2866,   4236,   5568,   6863,   8124,   9352,
  10549,  11716,  12855,  13967,  15054,  16117,  17156,  18172,
  19167,  20142,  21097,  22033,  22951,  23852,  24735,  25603,
  26455,  27291,  28113,  28922,  29716,  30497,  31266,  32023,  32767,
#else
      4,   1459,   2870,   4240,   5572,   6867,   8127,   9355,
  10552,  11719,  12858,  13971,  15057,  16120,  17158,  18175,
  19170,  20145,  21100,  22036,  22954,  23854,  24738,  25605,
  26457,  27294,  28116,  28924,  29719,  30500,  31269,  32025,  32769,
#endif
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

int64_t ff_dot_product(const int16_t *a, const int16_t *b, int length)
{
    int i;
    int64_t sum = 0;

    for (i = 0; i < length; i++)
        sum += MUL16(a[i], b[i]);

    return sum;
}

float ff_dot_productf(const float* a, const float* b, int length)
{
    float sum = 0;
    int i;

    for(i=0; i<length; i++)
        sum += a[i] * b[i];

    return sum;
}
