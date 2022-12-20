/*
 * Copyright (c) 2019 James Darnley
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <string.h>
#include "checkasm.h"
#include "libavcodec/v210dec_init.h"

static uint32_t get_v210(void)
{
    uint32_t t0 = rnd() & 0x3ff,
             t1 = rnd() & 0x3ff,
             t2 = rnd() & 0x3ff;
    uint32_t value =  t0
                   | (t1 << 10)
                   | (t2 << 20);
    return value;
}

#define NUM_SAMPLES 2048

static void randomize_buffers(uint32_t *src0, uint32_t *src1, int len)
{
    for (int i = 0; i < len; i++) {
        uint32_t value = get_v210();
        src0[i] = value;
        src1[i] = value;
    }
}

void checkasm_check_v210dec(void)
{
    V210DecContext h;

    h.aligned_input = 0;
    ff_v210dec_init(&h);

    if (check_func(h.unpack_frame, "v210_unpack")) {
        uint32_t src0[NUM_SAMPLES/3];
        uint32_t src1[NUM_SAMPLES/3];
        uint16_t y0[NUM_SAMPLES/2 + 26];
        uint16_t y1[NUM_SAMPLES/2 + 26];
        uint16_t u0[NUM_SAMPLES/4 + 13];
        uint16_t u1[NUM_SAMPLES/4 + 13];
        uint16_t v0[NUM_SAMPLES/4 + 13];
        uint16_t v1[NUM_SAMPLES/4 + 13];
        declare_func(void, const uint32_t *src, uint16_t *y, uint16_t *u, uint16_t *v, int width);
        const int pixels = NUM_SAMPLES / 2 / 6 * 6;

        randomize_buffers(src0, src1, NUM_SAMPLES/3);
        call_ref(src0, y0, u0, v0, pixels);
        call_new(src1, y1, u1, v1, pixels);
        if (memcmp(src0, src1, NUM_SAMPLES/3 * sizeof src0[0])
                || memcmp(y0, y1, pixels * sizeof y0[0])
                || memcmp(u0, u1, pixels/2 * sizeof u0[0])
                || memcmp(v0, v1, pixels/2 * sizeof v0[0]))
            fail();
        bench_new(src1, y1, u1, v1, pixels);
    }
    report("v210_unpack");
}
