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

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "libavutil/intreadwrite.h"
#include "libavutil/macros.h"
#include "libavutil/mem_internal.h"

#include "libavcodec/mathops.h"
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

static void test_rv34_idct_add(const RV34DSPContext *const s)
{
    enum {
        MAX_STRIDE = 256, ///< arbitrary, should be divisible by four
    };
    declare_func_emms(AV_CPU_FLAG_MMXEXT, void, uint8_t *dst, ptrdiff_t stride, int16_t *block);

    if (check_func(s->rv34_idct_add, "rv34_idct_add")) {
        DECLARE_ALIGNED_16(int16_t, block_ref)[4*4];
        DECLARE_ALIGNED_16(int16_t, block_new)[4*4];

        DECLARE_ALIGNED_4(uint8_t, dst_ref)[4*MAX_STRIDE + 4];
        DECLARE_ALIGNED_4(uint8_t, dst_new)[4*MAX_STRIDE + 4];

        ptrdiff_t stride = FFALIGN(1 + rnd() % MAX_STRIDE, 4);
        uint8_t *dst_refp = dst_ref, *dst_newp = dst_new;

        if (rnd() & 1) { // negate stride
            dst_refp += 3 * stride;
            dst_newp += 3 * stride;
            stride    = -stride;
        }

        for (size_t i = 0; i < FF_ARRAY_ELEMS(block_ref); ++i)
            block_ref[i] = sign_extend(rnd(), 10);
        for (size_t i = 0; i < sizeof(dst_ref); i += 4)
            AV_WN32A(dst_ref + i, rnd());
        memcpy(block_new, block_ref, sizeof(block_new));
        memcpy(dst_new, dst_ref, sizeof(dst_new));

        call_ref(dst_refp, stride, block_ref);
        call_new(dst_newp, stride, block_new);

        if (memcmp(dst_ref, dst_new, sizeof(dst_new)) ||
            memcmp(block_ref, block_new, sizeof(block_new)))
            fail();

        bench_new(dst_new, stride, block_new);
    }

    report("rv34_idct_add");
}

void checkasm_check_rv34dsp(void)
{
    RV34DSPContext s = { 0 };
    ff_rv34dsp_init(&s);

    test_rv34_inv_transform_dc(&s);
    test_rv34_idct_dc_add(&s);
    test_rv34_idct_add(&s);
}
