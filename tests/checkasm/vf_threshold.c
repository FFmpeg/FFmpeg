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

#define WIDTH        256
#define HEIGHT       3
#define WIDTH_PADDED (WIDTH + 32)
#define LINESIZE_MAX (WIDTH_PADDED + 32)
#define BUF_SIZE     (LINESIZE_MAX * HEIGHT)

#define randomize_buffers(buf, size)     \
    do {                                 \
       int j;                            \
       uint8_t *tmp_buf = (uint8_t *)buf;\
       for (j = 0; j < size; j++)        \
           tmp_buf[j] = rnd() & 0xFF;    \
    } while (0)

static void check_threshold(int depth){
    LOCAL_ALIGNED_32(uint8_t, in       , [BUF_SIZE]);
    LOCAL_ALIGNED_32(uint8_t, threshold, [BUF_SIZE]);
    LOCAL_ALIGNED_32(uint8_t, min      , [BUF_SIZE]);
    LOCAL_ALIGNED_32(uint8_t, max      , [BUF_SIZE]);
    LOCAL_ALIGNED_32(uint8_t, out_ref  , [BUF_SIZE]);
    LOCAL_ALIGNED_32(uint8_t, out_new  , [BUF_SIZE]);
    ptrdiff_t line_size = WIDTH_PADDED;
    int step = depth > 8 ? 2 : 1;
    int w = WIDTH / step;

    declare_func(void, const uint8_t *in, const uint8_t *threshold,
                 const uint8_t *min, const uint8_t *max, uint8_t *out,
                 ptrdiff_t ilinesize, ptrdiff_t tlinesize,
                 ptrdiff_t flinesize, ptrdiff_t slinesize,
                 ptrdiff_t olinesize, int w, int h);

    ThresholdContext s;
    s.depth = depth;
    ff_threshold_init(&s);

    memset(in,        0, BUF_SIZE);
    memset(threshold, 0, BUF_SIZE);
    memset(min,       0, BUF_SIZE);
    memset(max,       0, BUF_SIZE);
    randomize_buffers(in,        BUF_SIZE);
    randomize_buffers(threshold, BUF_SIZE);
    randomize_buffers(min,       BUF_SIZE);
    randomize_buffers(max,       BUF_SIZE);

    if (check_func(s.threshold, "threshold%d", depth)) {
        for (int i = 1; i < w; i++) {
            memset(out_ref, 0xAA, BUF_SIZE);
            memset(out_new, 0xAA, BUF_SIZE);
            call_ref(in, threshold, min, max, out_ref,
                     line_size, line_size, line_size, line_size, line_size, i, 1);
            call_new(in, threshold, min, max, out_new,
                     line_size, line_size, line_size, line_size, line_size, i, 1);
            if (memcmp(out_ref, out_new, BUF_SIZE))
                fail();
        }
        memset(out_ref, 0xAA, BUF_SIZE);
        memset(out_new, 0xAA, BUF_SIZE);
        call_ref(in, threshold, min, max, out_ref,
                 line_size, line_size, line_size, line_size, line_size, w, HEIGHT);
        call_new(in, threshold, min, max, out_new,
                 line_size, line_size, line_size, line_size, line_size, w, HEIGHT);
        if (memcmp(out_ref, out_new, BUF_SIZE))
            fail();
        memset(out_ref, 0xAA, BUF_SIZE);
        memset(out_new, 0xAA, BUF_SIZE);
        call_ref(in, threshold, min, max, out_ref,
                 line_size + 0,  line_size + 8,  line_size + 16,
                 line_size + 24, line_size + 32, w, HEIGHT);
        call_new(in, threshold, min, max, out_new,
                 line_size + 0,  line_size + 8,  line_size + 16,
                 line_size + 24, line_size + 32, w, HEIGHT);
        if (memcmp(out_ref, out_new, BUF_SIZE))
            fail();
        bench_new(in, threshold, min, max, out_new,
                  line_size, line_size, line_size, line_size, line_size, w, 1);
    }
}

void checkasm_check_vf_threshold(void)
{
    check_threshold(8);
    report("threshold8");

    check_threshold(10);
    report("threshold10");

    check_threshold(12);
    report("threshold12");

    check_threshold(16);
    report("threshold16");
}
