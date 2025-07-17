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

#include <string.h>
#include "checkasm.h"

#include "libavfilter/vf_blackdetect.h"
#include "libavutil/mem_internal.h"

#define WIDTH  256
#define HEIGHT 16
#define STRIDE (WIDTH + 32)

static void check_blackdetect(int depth)
{
    LOCAL_ALIGNED_32(uint8_t, in, [HEIGHT * STRIDE]);

    declare_func(unsigned, const uint8_t *in, ptrdiff_t stride,
                 ptrdiff_t width, ptrdiff_t height,
                 unsigned threshold);

    memset(in, 0, HEIGHT * STRIDE);
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++)
            in[y * STRIDE + x] = rnd() & 0xFF;
    }

    const unsigned threshold = 16 << (depth - 8);

    int w = WIDTH;
    if (depth == 16)
        w /= 2;

    if (check_func(ff_blackdetect_get_fn(depth), "blackdetect%d", depth)) {
        /* Ensure odd tail is handled correctly */
        unsigned count_ref = call_ref(in, STRIDE, w - 8, HEIGHT, threshold);
        unsigned count_new = call_new(in, STRIDE, w - 8, HEIGHT, threshold);
        if (count_ref != count_new) {
            fprintf(stderr, "blackdetect%d: count mismatch: %u != %u\n",
                    depth, count_ref, count_new);
            fail();
        }
        bench_new(in, STRIDE, w, HEIGHT, 16);
    }
}

void checkasm_check_blackdetect(void)
{
    check_blackdetect(8);
    report("blackdetect8");

    check_blackdetect(16);
    report("blackdetect16");
}
