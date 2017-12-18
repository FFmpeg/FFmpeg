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
#include "libavfilter/interlace.h"
#include "libavutil/intreadwrite.h"

#define WIDTH 256
#define WIDTH_PADDED 256 + 32
#define SRC_SIZE WIDTH_PADDED*3

#define randomize_buffers(buf, size)      \
    do {                                  \
        int j;                            \
        uint8_t *tmp_buf = (uint8_t *)buf;\
        for (j = 0; j < size; j++)        \
            tmp_buf[j] = rnd() & 0xFF;    \
    } while (0)

static void check_lowpass_line(int depth){
    LOCAL_ALIGNED_32(uint8_t, src,     [SRC_SIZE]);
    LOCAL_ALIGNED_32(uint8_t, dst_ref, [WIDTH_PADDED]);
    LOCAL_ALIGNED_32(uint8_t, dst_new, [WIDTH_PADDED]);
    int w = WIDTH;
    int mref = WIDTH_PADDED * -1;
    int pref = WIDTH_PADDED;
    int i, depth_byte;
    InterlaceContext s;

    declare_func(void, uint8_t *dstp, ptrdiff_t linesize, const uint8_t *srcp,
                 ptrdiff_t mref, ptrdiff_t pref, int clip_max);

    s.lowpass = 1;
    s.lowpass = VLPF_LIN;
    depth_byte = depth >> 3;
    w /= depth_byte;

    memset(src,     0, SRC_SIZE);
    memset(dst_ref, 0, WIDTH_PADDED);
    memset(dst_new, 0, WIDTH_PADDED);
    randomize_buffers(src, SRC_SIZE);

    ff_interlace_init(&s, depth);

    if (check_func(s.lowpass_line, "lowpass_line_%d", depth)) {
        for (i = 0; i < 32; i++) { /* simulate crop */
            call_ref(dst_ref, w, src + WIDTH_PADDED, mref - i*depth_byte, pref, 0);
            call_new(dst_new, w, src + WIDTH_PADDED, mref - i*depth_byte, pref, 0);
            if (memcmp(dst_ref, dst_new, WIDTH - i))
                fail();
        }
        bench_new(dst_new, w, src + WIDTH_PADDED, mref, pref, 0);
    }
}
void checkasm_check_vf_interlace(void)
{
    check_lowpass_line(8);
    report("lowpass_line_8");

    check_lowpass_line(16);
    report("lowpass_line_16");
}
