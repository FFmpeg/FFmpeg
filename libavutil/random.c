/*
 * Mersenne Twister PRNG algorithm
 * Copyright (c) 2006 Ryan Martell
 * Based on a C program for MT19937, with initialization improved 2002/1/26.
 * Coded by Takuji Nishimura and Makoto Matsumoto.
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
 * See http://en.wikipedia.org/wiki/Mersenne_twister
 * for an explanation of this algorithm.
 */
#include <stdio.h>
#include "random.h"


/* period parameters */
#define M 397
#define A 0x9908b0df /* constant vector a */
#define UPPER_MASK 0x80000000 /* most significant w-r bits */
#define LOWER_MASK 0x7fffffff /* least significant r bits */

/** Initializes mt[AV_RANDOM_N] with a seed. */
void av_random_init(AVRandomState *state, unsigned int seed)
{
    int index;

    /*
     This differs from the Wikipedia article.  Source is from the
     Makoto Matsumoto and Takuji Nishimura code, with the following comment:
     */
     /* See Knuth TAOCP Vol2. 3rd Ed. P.106 for multiplier. */
     /* In the previous versions, MSBs of the seed affect   */
     /* only MSBs of the array mt[].                        */
    state->mt[0] = seed & 0xffffffff;
    for (index = 1; index < AV_RANDOM_N; index++) {
        unsigned int prev= state->mt[index - 1];
        state->mt[index] = (1812433253UL * (prev ^ (prev>>30)) + index) & 0xffffffff;
    }
    state->index= index; // Will cause it to generate untempered numbers in the first iteration.
}

#if LIBAVUTIL_VERSION_MAJOR < 50
void av_init_random(unsigned int seed, AVRandomState *state)
{
    av_random_init(state, seed);
}
#endif

/** Generates AV_RANDOM_N words at one time (which will then be tempered later).
 * av_random calls this; you shouldn't. */
void av_random_generate_untempered_numbers(AVRandomState *state)
{
    int kk;
    unsigned int y;

    for (kk = 0; kk < AV_RANDOM_N - M; kk++) {
        y = (state->mt[kk] & UPPER_MASK) | (state->mt[kk + 1] & LOWER_MASK);
        state->mt[kk] = state->mt[kk + M] ^ (y >> 1) ^ ((y&1)*A);
    }
    for (; kk < AV_RANDOM_N - 1; kk++) {
        y = (state->mt[kk] & UPPER_MASK) | (state->mt[kk + 1] & LOWER_MASK);
        state->mt[kk] = state->mt[kk + (M - AV_RANDOM_N)] ^ (y >> 1) ^ ((y&1)*A);
    }
    y = (state->mt[AV_RANDOM_N - 1] & UPPER_MASK) | (state->mt[0] & LOWER_MASK);
    state->mt[AV_RANDOM_N - 1] = state->mt[M - 1] ^ (y >> 1) ^ ((y&1)*A);
    state->index = 0;
}

#ifdef TEST
#include "common.h"
#include "log.h"
int main(void)
{
    int x=0;
    int i, j;
    AVRandomState state;

    av_random_init(&state, 0xdeadbeef);
    for (j = 0; j < 10000; j++) {
        START_TIMER
        for (i = 0; i < 624; i++) {
            x+= av_random(&state);
        }
        STOP_TIMER("624 calls of av_random");
    }
    av_log(NULL, AV_LOG_ERROR, "final value:%X\n", x);
    return 0;
}
#endif
