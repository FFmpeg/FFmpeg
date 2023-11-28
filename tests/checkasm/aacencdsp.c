/*
 * Copyright (c) 2023 Institue of Software Chinese Academy of Sciences (ISCAS).
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

#include "libavutil/mem.h"
#include "libavutil/mem_internal.h"

#include "libavcodec/aacenc.h"

#include "checkasm.h"

#define randomize_float(buf, len)                               \
    do {                                                        \
        int i;                                                  \
        for (i = 0; i < len; i++) {                             \
            float f = (float)rnd() / (UINT_MAX >> 5) - 16.0f;   \
            buf[i] = f;                                         \
        }                                                       \
    } while (0)

static void test_abs_pow34(AACEncContext *s) {
#define BUF_SIZE 1024
    LOCAL_ALIGNED_32(float, in, [BUF_SIZE]);

    declare_func(void, float *, const float *, int);

    randomize_float(in, BUF_SIZE);

    if (check_func(s->abs_pow34, "abs_pow34")) {
        LOCAL_ALIGNED_32(float, out, [BUF_SIZE]);
        LOCAL_ALIGNED_32(float, out2, [BUF_SIZE]);

        call_ref(out, in, BUF_SIZE);
        call_new(out2, in, BUF_SIZE);

        if (memcmp(out, out2, BUF_SIZE * sizeof(float)) != 0)
            fail();

        bench_new(out, in, BUF_SIZE);
    }

    report("abs_pow34");
}


void checkasm_check_aacencdsp(void)
{
    AACEncContext s = { 0 };
    ff_aac_dsp_init(&s);

    test_abs_pow34(&s);
}
