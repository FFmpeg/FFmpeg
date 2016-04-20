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

#include <stdint.h>
#include <string.h>

#include "libavutil/lfg.h"
#include "libavutil/log.h"

#include "libavcodec/rangecoder.h"

#define SIZE 10240

int main(void)
{
    RangeCoder c;
    uint8_t b[9 * SIZE];
    uint8_t r[9 * SIZE];
    int i;
    uint8_t state[10];
    AVLFG prng;

    av_lfg_init(&prng, 1);

    ff_init_range_encoder(&c, b, SIZE);
    ff_build_rac_states(&c, 0.05 * (1LL << 32), 128 + 64 + 32 + 16);

    memset(state, 128, sizeof(state));

    for (i = 0; i < SIZE; i++)
        r[i] = av_lfg_get(&prng) % 7;

    for (i = 0; i < SIZE; i++)
        put_rac(&c, state, r[i] & 1);

    ff_rac_terminate(&c);

    ff_init_range_decoder(&c, b, SIZE);

    memset(state, 128, sizeof(state));

    for (i = 0; i < SIZE; i++)
        if ((r[i] & 1) != get_rac(&c, state)) {
            av_log(NULL, AV_LOG_ERROR, "rac failure at %d\n", i);
            return 1;
        }

    return 0;
}
