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

#include <stddef.h>
#include <stdint.h>

#include "checkasm.h"

#include "libavutil/intreadwrite.h"
#include "libavutil/mem_internal.h"
#include "libavutil/pixelutils.h"

enum {
    LOG2_MIN_DIMENSION = 1,
    LOG2_MAX_DIMENSION = 5,
    BUF_SIZE           = 4096, ///< arbitrary
};

#define randomize_buffer(buf)                              \
    do {                                                   \
        for (size_t k = 0; k < sizeof(buf); k += 4) {      \
            uint32_t r = rnd();                            \
            AV_WN32A(buf + k, r);                          \
        }                                                  \
    } while (0)

static void checkasm_check_sad(void)
{
    DECLARE_ALIGNED(32, uint8_t, buf1)[BUF_SIZE];
    DECLARE_ALIGNED(32, uint8_t, buf2)[BUF_SIZE];
    int inited = 0;

    declare_func(int, const uint8_t *src1, ptrdiff_t stride1,
                      const uint8_t *src2, ptrdiff_t stride2);

    for (int i = LOG2_MIN_DIMENSION; i <= LOG2_MAX_DIMENSION; ++i) {
        const size_t width = 1 << i, height = 1 << i;

        for (int aligned = 0; aligned <= 2; ++aligned) {
            av_pixelutils_sad_fn fn = av_pixelutils_get_sad_fn(i, i, aligned, NULL);
            if (check_func(fn, "sad_%zux%zu_%d", width, width, aligned)) {
                const uint8_t *src1 = buf1 + ((aligned != 0) ? 0 : rnd() % width);
                const uint8_t *src2 = buf2 + ((aligned == 2) ? 0 : rnd() % width);
                // stride * (height - 1) needs to be so small that the alignment offset
                // and the last line fit into the remaining buffer.
                size_t   max_stride = (BUF_SIZE - 2 * width) / (height - 1);
                ptrdiff_t   stride1 = 1 + rnd() % max_stride;
                ptrdiff_t   stride2 = 1 + rnd() % max_stride;

                if (aligned != 0)
                    stride1 &= ~(width - 1);
                if (aligned == 2)
                    stride2 &= ~(width - 1);

                if (rnd() & 1) { // negate stride
                    src1   += (height - 1) * stride1;
                    stride1 = -stride1;
                }
                if (rnd() & 1) { // negate stride
                    src2   += (height - 1) * stride2;
                    stride2 = -stride2;
                }

                if (!inited) {
                    randomize_buffer(buf1);
                    randomize_buffer(buf2);
                    inited = 1;
                }
                int res_ref = call_ref(src1, stride1, src2, stride2);
                int ref_new = call_new(src1, stride1, src2, stride2);
                if (res_ref != ref_new)
                    fail();

                bench_new(src1, stride1, src2, stride2);
            }
        }
    }
}

void checkasm_check_pixelutils(void)
{
    checkasm_check_sad();
    report("sad");
}
