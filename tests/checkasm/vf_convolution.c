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
#include "libavfilter/convolution.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem_internal.h"

#define WIDTH 512
#define HEIGHT 512
#define SRC_STRIDE 512
#define PIXELS (WIDTH * HEIGHT)

#define randomize_buffers(buf, size)      \
    do {                                  \
        int j;                            \
        uint8_t *tmp_buf = (uint8_t *)buf;\
        for (j = 0; j< size; j++)         \
            tmp_buf[j] = rnd() & 0xFF;    \
    } while (0)

static void check_sobel(const char * report_name)
{
    LOCAL_ALIGNED_32(uint8_t, src,     [PIXELS]);
    LOCAL_ALIGNED_32(uint8_t, dst_ref, [PIXELS]);
    LOCAL_ALIGNED_32(uint8_t, dst_new, [PIXELS]);
    const int height = WIDTH;
    const int width  = HEIGHT;
    const int stride = SRC_STRIDE;
    const int dstride = SRC_STRIDE;
    int mode = 0;
    const uint8_t *c[49];
    const int radius = 1;
    const int bpc = 1;
    const int step = mode == MATRIX_COLUMN ? 16 : 1;
    const int slice_start = 0;
    const int slice_end = height;
    int y;
    const int sizew = mode == MATRIX_COLUMN ? height : width;
    float scale = 2;
    float delta = 10;

    ConvolutionContext s;

    declare_func(void, uint8_t *dst, int width, float scale, float delta, const int *const matrix,
                 const uint8_t *c[], int peak, int radius, int dstride, int stride, int size);

    s.scale = scale;
    s.delta = delta;
    s.depth = 8;
    s.nb_planes = 3;
    s.planes = 15;
    ff_sobel_init(&s, s.depth, s.nb_planes);

    memset(dst_ref, 0, PIXELS);
    memset(dst_new, 0, PIXELS);
    randomize_buffers(src, PIXELS);

    if (check_func(s.filter[0], "%s", report_name)) {
        for (y = slice_start; y < slice_end; y += step) {
            const int xoff = mode == MATRIX_COLUMN ? (y - slice_start) * bpc : radius * bpc;
            const int yoff = mode == MATRIX_COLUMN ? radius * dstride : 0;

            s.setup[0](radius, c, src, stride, radius, width, y, height, bpc);
            call_ref(dst_ref + yoff + xoff, sizew - 2 * radius,
                     scale, delta, NULL, c, 0, radius,
                     dstride, stride, slice_end - step);
            call_new(dst_new + yoff + xoff, sizew - 2 * radius,
                     scale, delta, NULL, c, 0, radius,
                     dstride, stride, slice_end - step);
            if (memcmp(dst_ref + yoff + xoff, dst_new + yoff + xoff, slice_end - step))
                fail();
            bench_new(dst_new + yoff + xoff, sizew - 2 * radius,
                      scale, delta, NULL, c, 0, radius,
                      dstride, stride, slice_end - step);
            if (mode != MATRIX_COLUMN)
                dst_ref += dstride;
        }
    }

}

void checkasm_check_vf_sobel(void)
{
    check_sobel("sobel");
    report("convolution:sobel");
}
