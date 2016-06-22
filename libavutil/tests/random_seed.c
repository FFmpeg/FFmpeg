/*
 * Copyright (c) 2009 Baptiste Coudurier <baptiste.coudurier@gmail.com>
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

#define TEST 1
#include "libavutil/random_seed.c"

#undef printf
#define N 256
#include <stdio.h>

int main(void)
{
    int i, j, retry;
    uint32_t seeds[N];

    for (retry=0; retry<3; retry++){
        for (i=0; i<N; i++){
            seeds[i] = av_get_random_seed();
            for (j=0; j<i; j++)
                if (seeds[j] == seeds[i])
                    goto retry;
        }
        printf("seeds OK\n");
        return 0;
        retry:;
    }
    printf("FAIL at %d with %X\n", j, seeds[j]);
    return 1;
}
