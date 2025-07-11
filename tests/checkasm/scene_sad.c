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
#include "libavfilter/scene_sad.h"
#include "libavutil/mem_internal.h"

#define WIDTH  256
#define HEIGHT 256
#define STRIDE WIDTH

#define randomize_buffers(type, buf, size, mask)  \
    do {                                          \
        int j;                                    \
        type *tmp_buf = (type *) buf;             \
        for (j = 0; j < size; j++)                \
            tmp_buf[j] = rnd() & mask;            \
    } while (0)

static void check_scene_sad(int depth)
{
    LOCAL_ALIGNED_32(uint8_t, src1, [WIDTH * HEIGHT * 2]);
    LOCAL_ALIGNED_32(uint8_t, src2, [WIDTH * HEIGHT * 2]);
    declare_func(void, const uint8_t *src1, ptrdiff_t stride1,
                       const uint8_t *src2, ptrdiff_t stride2,
                       ptrdiff_t width, ptrdiff_t height, uint64_t *sum);

    const int width = WIDTH >> (depth > 8);
    int mask = (1 << depth) - 1;
    if (depth <= 8) {
        randomize_buffers(uint8_t,  src1, width * HEIGHT, mask);
        randomize_buffers(uint8_t,  src2, width * HEIGHT, mask);
    } else if (depth <= 16) {
        randomize_buffers(uint16_t, src1, width * HEIGHT, mask);
        randomize_buffers(uint16_t, src2, width * HEIGHT, mask);
    }

    if (check_func(ff_scene_sad_get_fn(depth), "scene_sad%d", depth)) {
        uint64_t sum1, sum2;
        call_ref(src1, STRIDE, src2, STRIDE, width, HEIGHT, &sum1);
        call_new(src1, STRIDE, src2, STRIDE, width, HEIGHT, &sum2);
        if (sum1 != sum2) {
            fprintf(stderr, "scene_sad%d: sum mismatch: %llu != %llu\n",
                    depth, (unsigned long long) sum1, (unsigned long long) sum2);
            fail();
        }
        bench_new(src1, STRIDE, src2, STRIDE, WIDTH, HEIGHT, &sum2);
    }
}

void checkasm_check_scene_sad(void)
{
    const int depths[] = { 8, 10, 12, 14, 15, 16 };
    for (int i = 0; i < FF_ARRAY_ELEMS(depths); i++) {
        check_scene_sad(depths[i]);
        report("scene_sad%d", depths[i]);
    }
}
