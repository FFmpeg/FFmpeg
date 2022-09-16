/*
 * Copyright (c) 2011  Justin Ruggles
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

#include "libavutil/common.h"
#include "libavutil/mathematics.h"
#include "adx.h"

void ff_adx_calculate_coeffs(int cutoff, int sample_rate, int bits, int *coeff)
{
    double a, b, c;

    a = M_SQRT2 - cos(2.0 * M_PI * cutoff / sample_rate);
    b = M_SQRT2 - 1.0;
    c = (a - sqrt((a + b) * (a - b))) / b;

    coeff[0] = lrintf(c * 2.0  * (1 << bits));
    coeff[1] = lrintf(-(c * c) * (1 << bits));
}
