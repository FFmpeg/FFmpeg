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
#include "libavfilter/avfilter.h"
#include "libavfilter/vf_eq.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem_internal.h"

#define WIDTH 256
#define HEIGHT 256
#define SRC_STRIDE 256
#define PIXELS (WIDTH * HEIGHT)
#define RANDOM_RANGE 80000
#define SCALE 10000

#define randomize_buffers(buf, size)      \
    do {                                  \
        int j;                            \
        uint8_t *tmp_buf = (uint8_t *)buf;\
        for (j = 0; j< size; j++)         \
            tmp_buf[j] = rnd() & 0xFF;    \
    } while (0)

static void check_eq(void)
{
    LOCAL_ALIGNED_32(uint8_t, src,     [PIXELS]);
    LOCAL_ALIGNED_32(uint8_t, dst_ref, [PIXELS]);
    LOCAL_ALIGNED_32(uint8_t, dst_new, [PIXELS]);
    int w = WIDTH;
    int h = HEIGHT;
    int src_stride = SRC_STRIDE;
    int dst_stride = SRC_STRIDE;
    EQParameters pa;
    EQContext eq;
    declare_func(void, EQParameters *param, uint8_t *dst, int dst_stride,
                 const uint8_t *src, int src_stride, int w, int h);

    double rand_contrast = (int)(rnd() % (RANDOM_RANGE * 2) - RANDOM_RANGE) /
                           (SCALE * 1.0);
    double rand_brightness = (int)(rnd() % (SCALE * 2) - SCALE) /
                             (SCALE * 1.0);
    pa.contrast = rand_contrast;
    pa.brightness = rand_brightness;

    memset(dst_ref, 0, PIXELS);
    memset(dst_new, 0, PIXELS);
    randomize_buffers(src, PIXELS);
    ff_eq_init(&eq);

    if (check_func(eq.process, "process")) {
        call_ref(&pa, dst_ref, dst_stride, src, src_stride, w, h);
        call_new(&pa, dst_new, dst_stride, src, src_stride, w, h);
        if (memcmp(dst_ref, dst_new, PIXELS))
            fail();
        bench_new(&pa, dst_new, dst_stride, src, src_stride, w, h);
    }
}

void checkasm_check_vf_eq(void)
{
    check_eq();
    report("eq");
}
