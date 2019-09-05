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
#include "libavfilter/gblur.h"

#define WIDTH 256
#define HEIGHT 256
#define PIXELS (WIDTH * HEIGHT)
#define BUF_SIZE (PIXELS * 4)

#define randomize_buffers(buf, size)             \
    do {                                         \
        int j;                                   \
        float *tmp_buf = (float *)buf;           \
        for (j = 0; j < size; j++)               \
            tmp_buf[j] = (float)(rnd() & 0xFF); \
    } while (0)

void checkasm_check_vf_gblur(void)
{
    float *dst_ref = av_malloc(BUF_SIZE);
    float *dst_new = av_malloc(BUF_SIZE);
    int w = WIDTH;
    int h = HEIGHT;
    int steps = 2;
    float nu = 0.101f;
    float bscale = 1.112f;
    GBlurContext s;

    declare_func(void, float *dst, int w, int h, int steps, float nu, float bscale);

    randomize_buffers(dst_ref, PIXELS);
    memcpy(dst_new, dst_ref, BUF_SIZE);

    ff_gblur_init(&s);

    if (check_func(s.horiz_slice, "horiz_slice")) {
        call_ref(dst_ref, w, h, steps, nu, bscale);
        call_new(dst_new, w, h, steps, nu, bscale);

        if (!float_near_abs_eps_array(dst_ref, dst_new, 0.01f, PIXELS)) {
            fail();
        }
        bench_new(dst_new, w, h, 1, nu, bscale);
    }
    report("horiz_slice");
    av_freep(&dst_ref);
    av_freep(&dst_new);
}
