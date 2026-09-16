/*
 * Copyright (c) 2026 Devin Heitmueller <dheitmueller@ltnglobal.com>
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

#include "libavutil/mem_internal.h"

#include "checkasm.h"
#include "libavcodec/bitpacked_dec_init.h"

#define MAX_WIDTH 3840

static void randomize_buffers(uint8_t *src0, uint8_t *src1, int len)
{
    for (int i = 0; i < len; i++) {
        uint8_t value = rnd();
        src0[i] = value;
        src1[i] = value;
    }
}

void checkasm_check_bitpackeddec(void)
{
    static const int widths[] = { 8, 16, 24, 32, 40, 48, 56, MAX_WIDTH };
    static const int tail_widths[] = { 2, 6, 10, 14, 16 };
    struct BitpackedContext h;

    ff_bitpackeddec_init(&h);

    if (check_func(h.unpack_yuv422p10, "bitpacked_unpack_yuv422p10")) {
        LOCAL_ALIGNED_16(uint8_t,  src0, [MAX_WIDTH * 5 / 2]);
        LOCAL_ALIGNED_16(uint8_t,  src1, [MAX_WIDTH * 5 / 2]);
        LOCAL_ALIGNED_16(uint16_t, y0,   [MAX_WIDTH]);
        LOCAL_ALIGNED_16(uint16_t, y1,   [MAX_WIDTH]);
        LOCAL_ALIGNED_16(uint16_t, u0,   [MAX_WIDTH / 2]);
        LOCAL_ALIGNED_16(uint16_t, u1,   [MAX_WIDTH / 2]);
        LOCAL_ALIGNED_16(uint16_t, v0,   [MAX_WIDTH / 2]);
        LOCAL_ALIGNED_16(uint16_t, v1,   [MAX_WIDTH / 2]);
        declare_func(void, const uint8_t *src, uint16_t *y, uint16_t *u,
                     uint16_t *v, int width);

        for (int i = 0; i < FF_ARRAY_ELEMS(widths); i++) {
            const int width = widths[i];
            const int src_size = width * 5 / 2;

            randomize_buffers(src0, src1, src_size);
            memset(y0, 0, width * sizeof(*y0));
            memset(y1, 0, width * sizeof(*y1));
            memset(u0, 0, width / 2 * sizeof(*u0));
            memset(u1, 0, width / 2 * sizeof(*u1));
            memset(v0, 0, width / 2 * sizeof(*v0));
            memset(v1, 0, width / 2 * sizeof(*v1));

            call_ref(src0, y0, u0, v0, width);
            call_new(src1, y1, u1, v1, width);
            if (memcmp(src0, src1, src_size) ||
                memcmp(y0, y1, width * sizeof(*y0)) ||
                memcmp(u0, u1, width / 2 * sizeof(*u0)) ||
                memcmp(v0, v1, width / 2 * sizeof(*v0)))
                fail();
        }

        for (int i = 0; i < FF_ARRAY_ELEMS(tail_widths); i++) {
            const int width = tail_widths[i];
            const int bulk = width & ~7;
            const int src_size = width * 5 / 2;

            randomize_buffers(src0, src1, src_size);
            bitpacked_unpack_yuv422p10_c(src0, y0, u0, v0, width);
            if (bulk)
                h.unpack_yuv422p10(src1, y1, u1, v1, bulk);
            bitpacked_unpack_yuv422p10_c(src1 + bulk * 5 / 2,
                                         y1 + bulk, u1 + bulk / 2, v1 + bulk / 2,
                                         width - bulk);
            if (memcmp(src0, src1, src_size) ||
                memcmp(y0, y1, width * sizeof(*y0)) ||
                memcmp(u0, u1, width / 2 * sizeof(*u0)) ||
                memcmp(v0, v1, width / 2 * sizeof(*v0)))
                fail();
        }

        randomize_buffers(src0, src1, MAX_WIDTH * 5 / 2);
        bench_new(src1, y1, u1, v1, MAX_WIDTH);
    }
    report("bitpacked_unpack_yuv422p10");
}
