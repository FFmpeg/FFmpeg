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

#include "libavcodec/avcodec.h"
#include "libavcodec/idctdsp.h"

#include "libavutil/common.h"
#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem_internal.h"

#define IDCTDSP_TEST(func) { #func, offsetof(IDCTDSPContext, func) },

typedef struct {
    const char *name;
    size_t offset;
} test;

#define RANDOMIZE_BUFFER16(name, size)          \
    do {                                        \
        int i;                                  \
        for (i = 0; i < size; ++i) {            \
            uint16_t r = rnd() % 0x201 - 0x100; \
            AV_WN16A(name##0 + i, r);           \
            AV_WN16A(name##1 + i, r);           \
        }                                       \
    } while (0)

#define RANDOMIZE_BUFFER8(name, size)         \
    do {                                      \
        int i;                                \
        for (i = 0; i < size; ++i) {          \
            uint8_t r = rnd();                \
            name##0[i] = r;                   \
            name##1[i] = r;                   \
        }                                     \
    } while (0)

static void check_add_put_clamped(void)
{
    /* Source buffers are only as big as needed, since any over-read won't affect results */
    LOCAL_ALIGNED_16(int16_t, src0, [64]);
    LOCAL_ALIGNED_16(int16_t, src1, [64]);
    /* Destination buffers have borders of one row above/below and 8 columns left/right to catch overflows */
    LOCAL_ALIGNED_8(uint8_t, dst0, [10 * 24]);
    LOCAL_ALIGNED_8(uint8_t, dst1, [10 * 24]);

    AVCodecContext avctx = { 0 };
    IDCTDSPContext h;

    const test tests[] = {
        IDCTDSP_TEST(add_pixels_clamped)
        IDCTDSP_TEST(put_pixels_clamped)
        IDCTDSP_TEST(put_signed_pixels_clamped)
    };

    ff_idctdsp_init(&h, &avctx);

    for (size_t t = 0; t < FF_ARRAY_ELEMS(tests); ++t) {
        void (*func)(const int16_t *, uint8_t * ptrdiff_t) = *(void **)((intptr_t) &h + tests[t].offset);
        if (check_func(func, "idctdsp.%s", tests[t].name)) {
            declare_func(void, const int16_t *, uint8_t *, ptrdiff_t);
            RANDOMIZE_BUFFER16(src, 64);
            RANDOMIZE_BUFFER8(dst, 10 * 24);
            call_ref(src0, dst0 + 24 + 8, 24);
            call_new(src1, dst1 + 24 + 8, 24);
            if (memcmp(dst0, dst1, 10 * 24))
                fail();
            bench_new(src1, dst1 + 24 + 8, 24);
        }
    }
}

void checkasm_check_idctdsp(void)
{
    check_add_put_clamped();
    report("idctdsp");
}
