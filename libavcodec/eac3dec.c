/*
 * E-AC-3 decoder
 * Copyright (c) 2007 Bartlomiej Wolowiec <bartek.wolowiec@gmail.com>
 * Copyright (c) 2008 Justin Ruggles
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "avcodec.h"
#include "ac3.h"
#include "ac3_parser.h"
#include "ac3dec.h"
#include "ac3dec_data.h"

/** gain adaptive quantization mode */
typedef enum {
    EAC3_GAQ_NO =0,
    EAC3_GAQ_12,
    EAC3_GAQ_14,
    EAC3_GAQ_124
} EAC3GaqMode;

#define EAC3_SR_CODE_REDUCED  3

/** lrint(M_SQRT2*cos(2*M_PI/12)*(1<<23)) */
#define COEFF_0 10273905LL

/** lrint(M_SQRT2*cos(0*M_PI/12)*(1<<23)) = lrint(M_SQRT2*(1<<23)) */
#define COEFF_1 11863283LL

/** lrint(M_SQRT2*cos(5*M_PI/12)*(1<<23)) */
#define COEFF_2  3070444LL

/**
 * Calculate 6-point IDCT of the pre-mantissas.
 * All calculations are 24-bit fixed-point.
 */
static void idct6(int pre_mant[6])
{
    int tmp;
    int even0, even1, even2, odd0, odd1, odd2;

    odd1 = pre_mant[1] - pre_mant[3] - pre_mant[5];

    even2 = ( pre_mant[2]                * COEFF_0) >> 23;
    tmp   = ( pre_mant[4]                * COEFF_1) >> 23;
    odd0  = ((pre_mant[1] + pre_mant[5]) * COEFF_2) >> 23;

    even0 = pre_mant[0] + (tmp >> 1);
    even1 = pre_mant[0] - tmp;

    tmp = even0;
    even0 = tmp + even2;
    even2 = tmp - even2;

    tmp = odd0;
    odd0 = tmp + pre_mant[1] + pre_mant[3];
    odd2 = tmp + pre_mant[5] - pre_mant[3];

    pre_mant[0] = even0 + odd0;
    pre_mant[1] = even1 + odd1;
    pre_mant[2] = even2 + odd2;
    pre_mant[3] = even2 - odd2;
    pre_mant[4] = even1 - odd1;
    pre_mant[5] = even0 - odd0;
}
