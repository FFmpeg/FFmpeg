/*
 * Lagged Fibonacci PRNG
 * Copyright (c) 2008 Michael Niedermayer
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
#include <math.h>
#include "lfg.h"
#include "md5.h"
#include "intreadwrite.h"
#include "attributes.h"

av_cold void av_lfg_init(AVLFG *c, unsigned int seed)
{
    uint8_t tmp[16] = { 0 };
    int i;

    for (i = 8; i < 64; i += 4) {
        AV_WL32(tmp, seed);
        tmp[4] = i;
        av_md5_sum(tmp, tmp, 16);
        c->state[i    ] = AV_RL32(tmp);
        c->state[i + 1] = AV_RL32(tmp + 4);
        c->state[i + 2] = AV_RL32(tmp + 8);
        c->state[i + 3] = AV_RL32(tmp + 12);
    }
    c->index = 0;
}

void av_bmg_get(AVLFG *lfg, double out[2])
{
    double x1, x2, w;

    do {
        x1 = 2.0 / UINT_MAX * av_lfg_get(lfg) - 1.0;
        x2 = 2.0 / UINT_MAX * av_lfg_get(lfg) - 1.0;
        w  = x1 * x1 + x2 * x2;
    } while (w >= 1.0);

    w = sqrt((-2.0 * log(w)) / w);
    out[0] = x1 * w;
    out[1] = x2 * w;
}

#ifdef TEST
#include "log.h"
#include "timer.h"

int main(void)
{
    int x = 0;
    int i, j;
    AVLFG state;

    av_lfg_init(&state, 0xdeadbeef);
    for (j = 0; j < 10000; j++) {
        START_TIMER
        for (i = 0; i < 624; i++) {
            //av_log(NULL, AV_LOG_ERROR, "%X\n", av_lfg_get(&state));
            x += av_lfg_get(&state);
        }
        STOP_TIMER("624 calls of av_lfg_get");
    }
    av_log(NULL, AV_LOG_ERROR, "final value:%X\n", x);

    /* BMG usage example */
    {
        double mean   = 1000;
        double stddev = 53;

        av_lfg_init(&state, 42);

        for (i = 0; i < 1000; i += 2) {
            double bmg_out[2];
            av_bmg_get(&state, bmg_out);
            av_log(NULL, AV_LOG_INFO,
                   "%f\n%f\n",
                   bmg_out[0] * stddev + mean,
                   bmg_out[1] * stddev + mean);
        }
    }

    return 0;
}
#endif
