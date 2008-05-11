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

#ifndef FFMPEG_ACELP_MATH_H
#define FFMPEG_ACELP_MATH_H

#include <stdint.h>

/**
 * \brief fixed-point implementation of cosine in [0; PI) domain
 * \param arg fixed-point cosine argument, 0 <= arg < 0x4000
 *
 * \return value of (1<<15) * cos(arg * PI / (1<<14)), -0x8000 <= result <= 0x7fff
 */
int16_t ff_cos(uint16_t arg);

/**
 * \brief fixed-point implementation of exp2(x) in [0; 1] domain
 * \param power argument to exp2, 0 <= power <= 0x7fff
 *
 * \return value of (1<<20) * exp2(power / (1<<15))
 *         0x8000c <= result <= 0xfffea
 */
int ff_exp2(uint16_t power);

/**
 * \brief Calculates log2(x)
 * \param value function argument, 0 < value <= 7fff ffff
 *
 * \return value of (1<<15) * log2(value)
 */
int ff_log2(uint32_t value);

/**
 * \brief Calculates sum of array element multiplications
 * \param speech input data array
 * \param length number of elements
 * \param offset offset for calculation of sum of s[i]*s[i+offset]
 * \param shift right shift by this value will be done before multiplication
 *
 * \return sum of multiplications
 *
 * \note array must be at least length+offset long!
 */
static int sum_of_squares(const int16_t* speech, int length, int offset, int shift)
{
    const int16_t* speech_end;
    int sum = 0;

    for(speech_end=speech+length; speech<speech_end; speech++)
       sum += (speech[0] * speech[offset]) >> shift;

    return sum;
}

/**
 * \brief Shift value left or right depending on sign of offset parameter.
 * \param value value to shift
 * \param offset shift offset
 *
 * \return value << offset, if offset>=0; value >> -offset - otherwise
 */
static inline int bidir_sal(int value, int offset)
{
    if(offset < 0) return value >> -offset;
    else           return value <<  offset;
}

#endif /* FFMPEG_ACELP_MATH_H */
