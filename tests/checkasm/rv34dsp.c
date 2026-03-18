/*
 * Copyright (c) 2024 Institute of Software Chinese Academy of Sciences (ISCAS).
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

#include "libavutil/mem.h"
#include "libavutil/mem_internal.h"

#include "libavcodec/rv34dsp.h"

#include "checkasm.h"

#define randomize(buf, len) \
    do { \
        for (int i = 0; i < len; i++) \
            buf[i] = rnd(); \
    } while (0)

static void test_rv34_inv_transform_dc(RV34DSPContext *s) {
    declare_func(void, int16_t *block);

    if (check_func(s->rv34_inv_transform_dc, "rv34_inv_transform_dc")) {
        DECLARE_ALIGNED_16(int16_t, p1)[4*4];
        DECLARE_ALIGNED_16(int16_t, p2)[4*4];

        randomize(p1, FF_ARRAY_ELEMS(p1));
        memcpy(p2, p1, sizeof(p1));

        call_ref(p1);
        call_new(p2);

        if (memcmp(p1, p2, sizeof(p1))) {
            fail();
        }

        bench_new(p1);
    }

    report("rv34_inv_transform_dc");
}

static void test_rv34_idct_dc_add(RV34DSPContext *s) {
    declare_func(void, uint8_t *dst, ptrdiff_t stride, int dc);

    if (check_func(s->rv34_idct_dc_add, "rv34_idct_dc_add")) {
        DECLARE_ALIGNED_16(uint8_t, p1)[4*4];
        DECLARE_ALIGNED_16(uint8_t, p2)[4*4];

        randomize(p1, FF_ARRAY_ELEMS(p1));
        memcpy(p2, p1, sizeof(p1));

        call_ref(p1, 4, 5);
        call_new(p2, 4, 5);

        if (memcmp(p1, p2, sizeof(p1))) {
            fail();
        }

        bench_new(p1, 4, 5);
    }

    report("rv34_idct_dc_add");
}

void checkasm_check_rv34dsp(void)
{
    RV34DSPContext s = { 0 };
    ff_rv34dsp_init(&s);

    test_rv34_inv_transform_dc(&s);
    test_rv34_idct_dc_add(&s);
}
