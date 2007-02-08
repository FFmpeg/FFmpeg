/*
 * Mersenne Twister Random Algorithm
 * Copyright (c) 2006 Ryan Martell.
 * Based on A C-program for MT19937, with initialization improved 2002/1/26. Coded by
 * Takuji Nishimura and Makoto Matsumoto.
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

#ifndef AV_RANDOM_H
#define AV_RANDOM_H

#define AV_RANDOM_N 624

typedef struct {
    unsigned int mt[AV_RANDOM_N]; ///< the array for the state vector
    int index; ///< current untempered value we use as the base.
} AVRandomState;


void av_init_random(unsigned int seed, AVRandomState *state); ///< to be inlined, the struct must be visible, so it doesn't make sense to try and keep it opaque with malloc/free like calls
void av_random_generate_untempered_numbers(AVRandomState *state); ///< Regenerate the untempered numbers (must be done every 624 iterations, or it will loop)

/** generates a random number on [0,0xffffffff]-interval */
static inline unsigned int av_random(AVRandomState *state)
{
    unsigned int y;

    // regenerate the untempered numbers if we should...
    if (state->index >= AV_RANDOM_N)
        av_random_generate_untempered_numbers(state);

    // grab one...
    y = state->mt[state->index++];

    /* Now temper (Mersenne Twister coefficients) The coefficients for MT19937 are.. */
    y ^= (y >> 11);
    y ^= (y << 7) & 0x9d2c5680;
    y ^= (y << 15) & 0xefc60000;
    y ^= (y >> 18);

    return y;
}

/** return random in range [0-1] as double */
static inline double av_random_real1(AVRandomState *state)
{
    /* divided by 2^32-1 */
    return av_random(state) * (1.0 / 4294967296.0);
}

// only available if DEBUG is defined in the .c file
void av_benchmark_random(void);
#endif // AV_RANDOM_H

