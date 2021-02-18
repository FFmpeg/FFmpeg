/*
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

#include <stdint.h>
#include <string.h>

#include "libavutil/lfg.h"
#include "libavutil/log.h"

#include "libavcodec/rangecoder.h"

#define SIZE 1240

/**
 * Check if at the current position there is a valid looking termination
 * @param version version 0 requires the decoder to know the data size in bytes
 *                version 1 needs about 1 bit more space but does not need to
 *                          carry the size from encoder to decoder
 * @returns negative AVERROR code on error or non negative.
 */
static int rac_check_termination(RangeCoder *c, int version)
{
    if (version == 1) {
        RangeCoder tmp = *c;
        get_rac(c, (uint8_t[]) { 129 });

        if (c->bytestream == tmp.bytestream && c->bytestream > c->bytestream_start)
            tmp.low -= *--tmp.bytestream;
        tmp.bytestream_end = tmp.bytestream;

        if (get_rac(&tmp, (uint8_t[]) { 129 }))
            return AVERROR_INVALIDDATA;
    } else {
        if (c->bytestream_end != c->bytestream)
            return AVERROR_INVALIDDATA;
    }
    return 0;
}

int main(void)
{
    RangeCoder c;
    uint8_t b[9 * SIZE] = {0};
    uint8_t r[9 * SIZE];
    int i, p, actual_length, version;
    uint8_t state[10];
    AVLFG prng;

    av_lfg_init(&prng, 1);
    for (version = 0; version < 2; version++) {
        for (p = 0; p< 1024; p++) {
            ff_init_range_encoder(&c, b, SIZE);
            ff_build_rac_states(&c, (1LL << 32) / 20, 128 + 64 + 32 + 16);

            memset(state, 128, sizeof(state));

            for (i = 0; i < SIZE; i++)
                r[i] = av_lfg_get(&prng) % 7;

            for (i = 0; i < SIZE; i++)
                put_rac(&c, state, r[i] & 1);

            actual_length = ff_rac_terminate(&c, version);

            ff_init_range_decoder(&c, b, version ? SIZE : actual_length);

            memset(state, 128, sizeof(state));

            for (i = 0; i < SIZE; i++)
                if ((r[i] & 1) != get_rac(&c, state)) {
                    av_log(NULL, AV_LOG_ERROR, "rac failure at %d pass %d version %d\n", i, p, version);
                    return 1;
                }

            if (rac_check_termination(&c, version) < 0) {
                av_log(NULL, AV_LOG_ERROR, "rac failure at termination pass %d version %d\n", p, version);
                return 1;
            }
            if (c.bytestream - c.bytestream_start - actual_length != version) {
                av_log(NULL, AV_LOG_ERROR, "rac failure at pass %d version %d\n", p, version);
                return 1;
            }
        }
    }

    return 0;
}
