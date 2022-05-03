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
#include "libavfilter/vf_threshold_init.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem_internal.h"

#define WIDTH 256
#define WIDTH_PADDED 256 + 32

#define randomize_buffers(buf, size)     \
    do {                                 \
       int j;                            \
       uint8_t *tmp_buf = (uint8_t *)buf;\
       for (j = 0; j < size; j++)        \
           tmp_buf[j] = rnd() & 0xFF;    \
    } while (0)

static void check_threshold(int depth){
    LOCAL_ALIGNED_32(uint8_t, in       , [WIDTH_PADDED]);
    LOCAL_ALIGNED_32(uint8_t, threshold, [WIDTH_PADDED]);
    LOCAL_ALIGNED_32(uint8_t, min      , [WIDTH_PADDED]);
    LOCAL_ALIGNED_32(uint8_t, max      , [WIDTH_PADDED]);
    LOCAL_ALIGNED_32(uint8_t, out_ref  , [WIDTH_PADDED]);
    LOCAL_ALIGNED_32(uint8_t, out_new  , [WIDTH_PADDED]);
    ptrdiff_t line_size = WIDTH_PADDED;
    int w = WIDTH;

    declare_func(void, const uint8_t *in, const uint8_t *threshold,
                 const uint8_t *min, const uint8_t *max, uint8_t *out,
                 ptrdiff_t ilinesize, ptrdiff_t tlinesize,
                 ptrdiff_t flinesize, ptrdiff_t slinesize,
                 ptrdiff_t olinesize, int w, int h);

    ThresholdContext s;
    s.depth = depth;
    ff_threshold_init(&s);

    memset(in,     0, WIDTH_PADDED);
    memset(threshold, 0, WIDTH_PADDED);
    memset(min, 0, WIDTH_PADDED);
    memset(max, 0, WIDTH_PADDED);
    memset(out_ref, 0, WIDTH_PADDED);
    memset(out_new, 0, WIDTH_PADDED);
    randomize_buffers(in, WIDTH);
    randomize_buffers(threshold, WIDTH);
    randomize_buffers(min, WIDTH);
    randomize_buffers(max, WIDTH);

    if (depth == 16)
        w /= 2;

    if (check_func(s.threshold, "threshold%d", depth)) {
        call_ref(in, threshold, min, max, out_ref, line_size, line_size, line_size, line_size, line_size, w, 1);
        call_new(in, threshold, min, max, out_new, line_size, line_size, line_size, line_size, line_size, w, 1);
        if (memcmp(out_ref, out_new, WIDTH))
            fail();
        bench_new(in, threshold, min, max, out_new, line_size, line_size, line_size, line_size, line_size, w, 1);
    }
}

void checkasm_check_vf_threshold(void)
{
    check_threshold(8);
    report("threshold8");

    check_threshold(16);
    report("threshold16");
}
