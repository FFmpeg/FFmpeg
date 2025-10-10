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

#include <assert.h>
#include <stddef.h>

#include "checkasm.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/macros.h"
#include "libavutil/mem_internal.h"
#include "libavcodec/vp3dsp.h"

enum {
    MAX_STRIDE          = 64,
    MIN_STRIDE          = 8,
    /// Horizontal tests operate on 4x8 blocks
    HORIZONTAL_BUF_SIZE = ((8 /* lines */ - 1) * MAX_STRIDE + 4 /* width */ + 7 /* misalignment */),
    /// Vertical tests operate on 8x4 blocks
    VERTICAL_BUF_SIZE   = ((4 /* lines */ - 1) * MAX_STRIDE + 8 /* width */ + 7 /* misalignment */),
};

#define randomize_buffers(buf0, buf1, size)                \
    do {                                                   \
        static_assert(sizeof(buf0[0]) == 1 && sizeof(buf1[0]) == 1, \
                      "Pointer arithmetic needs to be adapted"); \
        for (size_t k = 0; k < (size & ~3); k += 4) {      \
            uint32_t r = rnd();                            \
            AV_WN32A(buf0 + k, r);                         \
            AV_WN32A(buf1 + k, r);                         \
        }                                                  \
        for (size_t k = size & ~3; k < size; ++k)          \
            buf0[k] = buf1[k] = rnd();                     \
    } while (0)


static void vp3_check_loop_filter(void)
{
    DECLARE_ALIGNED(8, uint8_t, hor_buf0)[HORIZONTAL_BUF_SIZE];
    DECLARE_ALIGNED(8, uint8_t, hor_buf1)[HORIZONTAL_BUF_SIZE];
    DECLARE_ALIGNED(8, uint8_t, ver_buf0)[VERTICAL_BUF_SIZE];
    DECLARE_ALIGNED(8, uint8_t, ver_buf1)[VERTICAL_BUF_SIZE];
    DECLARE_ALIGNED(16, int, bounding_values_array)[256 + 4];
    int *const bounding_values = bounding_values_array + 127;
    VP3DSPContext vp3dsp;
    static const struct {
        const char *name;
        size_t offset;
        int lines_above, lines_below;
        int pixels_left, pixels_right;
        unsigned alignment;
        int horizontal;
    } tests[] = {
#define TEST(NAME) .name = #NAME, .offset = offsetof(VP3DSPContext, NAME)
        { TEST(v_loop_filter_unaligned), 2, 1, 0, 7, 1, 0 },
        { TEST(h_loop_filter_unaligned), 0, 7, 2, 1, 1, 1 },
        { TEST(v_loop_filter),           2, 1, 0, 7, VP3_LOOP_FILTER_NO_UNALIGNED_SUPPORT ? 8 : 1, 0 },
        { TEST(h_loop_filter),           0, 7, 2, 1, VP3_LOOP_FILTER_NO_UNALIGNED_SUPPORT ? 8 : 1, 1 },
    };
    declare_func(void, uint8_t *src, ptrdiff_t stride, int *bounding_values);

    ff_vp3dsp_init(&vp3dsp);

    int filter_limit = rnd() % 128;

    ff_vp3dsp_set_bounding_values(bounding_values_array, filter_limit);

    for (size_t i = 0; i < FF_ARRAY_ELEMS(tests); ++i) {
        void (*loop_filter)(uint8_t *, ptrdiff_t, int*) = *(void(**)(uint8_t *, ptrdiff_t, int*))((char*)&vp3dsp + tests[i].offset);

        if (check_func(loop_filter, "%s", tests[i].name)) {
            uint8_t  *buf0 = tests[i].horizontal ? hor_buf0 : ver_buf0;
            uint8_t  *buf1 = tests[i].horizontal ? hor_buf1 : ver_buf1;
            size_t bufsize = tests[i].horizontal ? HORIZONTAL_BUF_SIZE : VERTICAL_BUF_SIZE;
            ptrdiff_t stride = (rnd() % (MAX_STRIDE / MIN_STRIDE) + 1) * MIN_STRIDE;
            // Don't always use pointers that are aligned to 8.
            size_t offset = FFALIGN(tests[i].pixels_left, tests[i].alignment) +
                            (rnd() % (MIN_STRIDE / tests[i].alignment)) * tests[i].alignment
                            + stride * tests[i].lines_above;
            uint8_t *dst0 = buf0 + offset, *dst1 = buf1 + offset;

            if (rnd() & 1) {
                // Flip stride.
                dst1  += (tests[i].lines_below - tests[i].lines_above) * stride;
                dst0  += (tests[i].lines_below - tests[i].lines_above) * stride;
                stride = -stride;
            }

            randomize_buffers(buf0, buf1, bufsize);
            call_ref(dst0, stride, bounding_values);
            call_new(dst1, stride, bounding_values);
            if (memcmp(buf0, buf1, bufsize))
                fail();
            bench_new(dst0, stride, bounding_values);
        }
    }
}

void checkasm_check_vp3dsp(void)
{
    vp3_check_loop_filter();
}
