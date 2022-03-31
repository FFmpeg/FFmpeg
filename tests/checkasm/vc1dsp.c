/*
 * Copyright (c) 2022 Ben Avison
 *
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

#include "libavcodec/vc1dsp.h"

#include "libavutil/common.h"
#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem_internal.h"

#define VC1DSP_TEST(func) { #func, offsetof(VC1DSPContext, func) },

typedef struct {
    const char *name;
    size_t offset;
} test;

#define RANDOMIZE_BUFFER8_MID_WEIGHTED(name, size)  \
    do {                                            \
        uint8_t *p##0 = name##0, *p##1 = name##1;   \
        int i = (size);                             \
        while (i-- > 0) {                           \
            int x = 0x80 | (rnd() & 0x7F);          \
            x >>= rnd() % 9;                        \
            if (rnd() & 1)                          \
                x = -x;                             \
            *p##1++ = *p##0++ = 0x80 + x;           \
        }                                           \
    } while (0)

static void check_loop_filter(void)
{
    /* Deblocking filter buffers are big enough to hold a 16x16 block,
     * plus 16 columns left and 4 rows above to hold filter inputs
     * (depending on whether v or h neighbouring block edge, oversized
     * horizontally to maintain 16-byte alignment) plus 16 columns and
     * 4 rows below to catch write overflows */
    LOCAL_ALIGNED_16(uint8_t, filter_buf0, [24 * 48]);
    LOCAL_ALIGNED_16(uint8_t, filter_buf1, [24 * 48]);

    VC1DSPContext h;

    const test tests[] = {
        VC1DSP_TEST(vc1_v_loop_filter4)
        VC1DSP_TEST(vc1_h_loop_filter4)
        VC1DSP_TEST(vc1_v_loop_filter8)
        VC1DSP_TEST(vc1_h_loop_filter8)
        VC1DSP_TEST(vc1_v_loop_filter16)
        VC1DSP_TEST(vc1_h_loop_filter16)
    };

    ff_vc1dsp_init(&h);

    for (size_t t = 0; t < FF_ARRAY_ELEMS(tests); ++t) {
        void (*func)(uint8_t *, ptrdiff_t, int) = *(void **)((intptr_t) &h + tests[t].offset);
        declare_func_emms(AV_CPU_FLAG_MMX, void, uint8_t *, ptrdiff_t, int);
        if (check_func(func, "vc1dsp.%s", tests[t].name)) {
            for (int count = 1000; count > 0; --count) {
                int pq = rnd() % 31 + 1;
                RANDOMIZE_BUFFER8_MID_WEIGHTED(filter_buf, 24 * 48);
                call_ref(filter_buf0 + 4 * 48 + 16, 48, pq);
                call_new(filter_buf1 + 4 * 48 + 16, 48, pq);
                if (memcmp(filter_buf0, filter_buf1, 24 * 48))
                    fail();
            }
        }
        for (int j = 0; j < 24; ++j)
            for (int i = 0; i < 48; ++i)
                filter_buf1[j * 48 + i] = 0x60 + 0x40 * (i >= 16 && j >= 4);
        if (check_func(func, "vc1dsp.%s_bestcase", tests[t].name))
            bench_new(filter_buf1 + 4 * 48 + 16, 48, 1);
        if (check_func(func, "vc1dsp.%s_worstcase", tests[t].name))
            bench_new(filter_buf1 + 4 * 48 + 16, 48, 31);
    }
}

void checkasm_check_vc1dsp(void)
{
    check_loop_filter();
    report("loop_filter");
}
