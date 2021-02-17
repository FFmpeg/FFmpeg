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

#include <float.h>
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

static void check_horiz_slice(float *dst_ref, float *dst_new)
{
    int steps = 2;
    float nu = 0.101f;
    float bscale = 1.112f;

    declare_func(void, float *dst, int w, int h, int steps, float nu, float bscale);
    call_ref(dst_ref, WIDTH, HEIGHT, steps, nu, bscale);
    call_new(dst_new, WIDTH, HEIGHT, steps, nu, bscale);
    if (!float_near_abs_eps_array(dst_ref, dst_new, 0.01f, PIXELS)) {
         fail();
    }
    bench_new(dst_new, WIDTH, HEIGHT, 1, nu, bscale);
}

static void check_postscale_slice(float *dst_ref, float *dst_new)
{
    float postscale = 0.0603f;

    declare_func(void, float *dst, int len, float postscale, float min, float max);
    call_ref(dst_ref, PIXELS, postscale, -FLT_MAX, FLT_MAX);
    call_new(dst_new, PIXELS, postscale, -FLT_MAX, FLT_MAX);
    if (!float_near_abs_eps_array(dst_ref, dst_new, FLT_EPSILON, PIXELS)) {
        fail();
    }
    bench_new(dst_new, PIXELS, postscale, -FLT_MAX, FLT_MAX);
}

void checkasm_check_vf_gblur(void)
{
    float *dst_ref = av_malloc(BUF_SIZE);
    float *dst_new = av_malloc(BUF_SIZE);
    GBlurContext s;

    randomize_buffers(dst_ref, PIXELS);
    memcpy(dst_new, dst_ref, BUF_SIZE);

    ff_gblur_init(&s);

    if (check_func(s.horiz_slice, "horiz_slice")) {
        check_horiz_slice(dst_ref, dst_new);
    }
    report("horiz_slice");

    randomize_buffers(dst_ref, PIXELS);
    memcpy(dst_new, dst_ref, BUF_SIZE);
    if (check_func(s.postscale_slice, "postscale_slice")) {
        check_postscale_slice(dst_ref, dst_new);
    }
    report("postscale_slice");

    av_freep(&dst_ref);
    av_freep(&dst_new);
}
