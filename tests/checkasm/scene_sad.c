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

static void check_scene_sad(int depth, int bswap)
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

    if (check_func(bswap ? ff_scene_sad_bswap_get_fn() : ff_scene_sad_get_fn(depth),
                   "scene_sad%d%s", depth, bswap ? "_bswap" : "")) {
        /* include rows narrower than and around each vector width */
        const int widths[] = { 1, 7, 8, 15, 16, 17, 31, 32, 33, 63, 64, 65, width };
        const int heights[] = { 0, 1, HEIGHT };
        uint64_t sum1, sum2;

        for (int w = 0; w < FF_ARRAY_ELEMS(widths); w++) {
            for (int h = 0; h < FF_ARRAY_ELEMS(heights); h++) {
                if (widths[w] > width)
                    continue;
                call_ref(src1, STRIDE, src2, STRIDE, widths[w], heights[h], &sum1);
                call_new(src1, STRIDE, src2, STRIDE, widths[w], heights[h], &sum2);
                if (sum1 != sum2) {
                    fprintf(stderr, "scene_sad%d%s: sum mismatch for %dx%d: %llu != %llu\n",
                            depth, bswap ? "_bswap" : "", widths[w], heights[h],
                            (unsigned long long) sum1, (unsigned long long) sum2);
                    fail();
                }
            }
        }
        if (depth == 8) {
            for (int offset1 = 0; offset1 < 16; offset1++) {
                for (int offset2 = 0; offset2 < 16; offset2++) {
                    /* Odd strides also exercise unaligned rows. */
                    call_ref(src1 + offset1, STRIDE - 1,
                             src2 + offset2, STRIDE - 3,
                             width - 17, HEIGHT, &sum1);
                    call_new(src1 + offset1, STRIDE - 1,
                             src2 + offset2, STRIDE - 3,
                             width - 17, HEIGHT, &sum2);
                    if (sum1 != sum2) {
                        fprintf(stderr, "scene_sad8: sum mismatch at offsets "
                                "%d/%d: %llu != %llu\n", offset1, offset2,
                                (unsigned long long) sum1,
                                (unsigned long long) sum2);
                        fail();
                    }
                }
            }
        }
        bench_new(src1, STRIDE, src2, STRIDE, width, HEIGHT, &sum2);
    }
}

void checkasm_check_scene_sad(void)
{
    const int depths[] = { 8, 10, 12, 14, 15, 16 };
    for (int i = 0; i < FF_ARRAY_ELEMS(depths); i++) {
        check_scene_sad(depths[i], 0);
        report("scene_sad%d", depths[i]);
    }
    check_scene_sad(16, 1);
    report("scene_sad16_bswap");
}
