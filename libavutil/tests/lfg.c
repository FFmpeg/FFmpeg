/*
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

#include "libavutil/log.h"
#include "libavutil/timer.h"
#include "libavutil/lfg.h"

int main(void)
{
    int x = 0;
    int i, j;
    AVLFG state;

    av_lfg_init(&state, 0xdeadbeef);
    for (j = 0; j < 10000; j++) {
        START_TIMER
        for (i = 0; i < 624; i++)
            x += av_lfg_get(&state);
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
