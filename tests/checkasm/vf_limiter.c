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
#include "libavfilter/limiter.h"
#include "libavfilter/vf_limiter_init.h"
#include "libavutil/mem_internal.h"
#include <stddef.h>
#include <stdint.h>

#define WIDTH  256
#define HEIGHT 16
#define WIDTH_PADDED (WIDTH + 32)

static void check_limiter(int depth)
{
    LOCAL_ALIGNED_32(uint8_t, src,     [HEIGHT * WIDTH_PADDED]);
    LOCAL_ALIGNED_32(uint8_t, dst_ref, [HEIGHT * WIDTH_PADDED]);
    LOCAL_ALIGNED_32(uint8_t, dst_new, [HEIGHT * WIDTH_PADDED]);

    declare_func(void, const uint8_t *, uint8_t *,
                 ptrdiff_t, ptrdiff_t, int, int, int, int);

    const int min =  16 << (depth - 8);
    const int max = 235 << (depth - 8);

    int w = WIDTH;
    if (depth == 16)
        w /= 2;

    LimiterDSPContext s;
    ff_limiter_init(&s, depth);

    if (check_func(s.limiter, "limiter%d", depth)) {
        memset(dst_ref, 0, HEIGHT * WIDTH_PADDED);
        memset(dst_new, 0, HEIGHT * WIDTH_PADDED);
        checkasm_randomize(src, HEIGHT * WIDTH_PADDED);

        call_ref(src, dst_ref, WIDTH_PADDED, WIDTH_PADDED, w, HEIGHT, min, max);
        call_new(src, dst_new, WIDTH_PADDED, WIDTH_PADDED, w, HEIGHT, min, max);

        if (memcmp(dst_ref, dst_new, HEIGHT * WIDTH_PADDED))
            fail();

        bench_new(src, dst_new, WIDTH_PADDED, WIDTH_PADDED, w, HEIGHT, min, max);
    }
}

void checkasm_check_vf_limiter(void)
{
    check_limiter(8);
    report("limiter8");

    check_limiter(16);
    report("limiter16");
}
