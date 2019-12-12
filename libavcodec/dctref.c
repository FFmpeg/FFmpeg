/*
 * reference discrete cosine transform (double precision)
 * Copyright (C) 2009 Dylan Yudaken
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

/**
 * @file
 * reference discrete cosine transform (double precision)
 *
 * @author Dylan Yudaken (dyudaken at gmail)
 *
 * @note This file could be optimized a lot, but is for
 * reference and so readability is better.
 */

#include "libavutil/mathematics.h"
#include "dctref.h"

static double coefficients[8 * 8];

/**
 * Initialize the double precision discrete cosine transform
 * functions fdct & idct.
 */
av_cold void ff_ref_dct_init(void)
{
    unsigned int i, j;

    for (j = 0; j < 8; ++j) {
        coefficients[j] = sqrt(0.125);
        for (i = 8; i < 64; i += 8) {
            coefficients[i + j] = 0.5 * cos(i * (j + 0.5) * M_PI / 64.0);
        }
    }
}

/**
 * Transform 8x8 block of data with a double precision forward DCT <br>
 * This is a reference implementation.
 *
 * @param block pointer to 8x8 block of data to transform
 */
void ff_ref_fdct(short *block)
{
    /* implement the equation: block = coefficients * block * coefficients' */

    unsigned int i, j, k;
    double out[8 * 8];

    /* out = coefficients * block */
    for (i = 0; i < 64; i += 8) {
        for (j = 0; j < 8; ++j) {
            double tmp = 0;
            for (k = 0; k < 8; ++k) {
                tmp += coefficients[i + k] * block[k * 8 + j];
            }
            out[i + j] = tmp * 8;
        }
    }

    /* block = out * (coefficients') */
    for (j = 0; j < 8; ++j) {
        for (i = 0; i < 64; i += 8) {
            double tmp = 0;
            for (k = 0; k < 8; ++k) {
                tmp += out[i + k] * coefficients[j * 8 + k];
            }
            block[i + j] = floor(tmp + 0.499999999999);
        }
    }
}

/**
 * Transform 8x8 block of data with a double precision inverse DCT <br>
 * This is a reference implementation.
 *
 * @param block pointer to 8x8 block of data to transform
 */
void ff_ref_idct(short *block)
{
    /* implement the equation: block = (coefficients') * block * coefficients */

    unsigned int i, j, k;
    double out[8 * 8];

    /* out = block * coefficients */
    for (i = 0; i < 64; i += 8) {
        for (j = 0; j < 8; ++j) {
            double tmp = 0;
            for (k = 0; k < 8; ++k) {
                tmp += block[i + k] * coefficients[k * 8 + j];
            }
            out[i + j] = tmp;
        }
    }

    /* block = (coefficients') * out */
    for (i = 0; i < 8; ++i) {
        for (j = 0; j < 8; ++j) {
            double tmp = 0;
            for (k = 0; k < 64; k += 8) {
                tmp += coefficients[k + i] * out[k + j];
            }
            block[i * 8 + j] = floor(tmp + 0.5);
        }
    }
}
