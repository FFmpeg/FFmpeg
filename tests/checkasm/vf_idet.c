/*
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

#include "checkasm.h"

#include "libavfilter/vf_idetdsp.h"
#include "libavutil/mem_internal.h"

#define WIDTH 512

static void check_idet(int depth)
{
    IDETDSPContext dsp;

    LOCAL_ALIGNED_32(uint8_t, in0, [WIDTH]);
    LOCAL_ALIGNED_32(uint8_t, in1, [WIDTH]);
    LOCAL_ALIGNED_32(uint8_t, in2, [WIDTH]);

    declare_func(int, const uint8_t *a, const uint8_t *b,
                 const uint8_t *c, int w);

    ff_idet_dsp_init(&dsp, depth > 8);

    for (int x = 0; x < WIDTH; x++) {
        in0[x] = rnd() & 0xFF;
        in1[x] = rnd() & 0xFF;
        in2[x] = rnd() & 0xFF;
    }

    if (check_func(dsp.filter_line, "idet%d", depth)) {
        /* Ensure odd tail is handled correctly */
        int res_ref = call_ref(in0, in1, in2, WIDTH - 8);
        int res_new = call_new(in0, in1, in2, WIDTH - 8);
        if (res_ref != res_new) {
            fprintf(stderr, "idet%d: result mismatch: %u != %u\n",
                    depth, res_ref, res_new);
            fail();
        }
        bench_new(in0, in1, in2, WIDTH);
    }
}

void checkasm_check_idet(void)
{
    check_idet(8);
    report("idet8");

    check_idet(16);
    report("idet16");
}
