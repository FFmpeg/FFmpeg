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

#include "libavcodec/ac3dsp.h"

#include "checkasm.h"

#define randomize_float(buf, len)                               \
    do {                                                        \
        int i;                                                  \
        for (i = 0; i < len; i++) {                             \
            float f = (float)rnd() / (UINT_MAX >> 5) - 16.0f;   \
            buf[i] = f;                                         \
        }                                                       \
    } while (0)

static void check_float_to_fixed24(AC3DSPContext *c) {
#define BUF_SIZE 1024
    LOCAL_ALIGNED_32(float, src, [BUF_SIZE]);

    declare_func(void, int32_t *, const float *, size_t);

    randomize_float(src, BUF_SIZE);

    if (check_func(c->float_to_fixed24, "float_to_fixed24")) {
        LOCAL_ALIGNED_32(int32_t, dst, [BUF_SIZE]);
        LOCAL_ALIGNED_32(int32_t, dst2, [BUF_SIZE]);

        call_ref(dst, src, BUF_SIZE);
        call_new(dst2, src, BUF_SIZE);

        if (memcmp(dst, dst2, BUF_SIZE) != 0)
            fail();

        bench_new(dst, src, BUF_SIZE);
    }


    report("float_to_fixed24");
}

void checkasm_check_ac3dsp(void)
{
    AC3DSPContext c;
    ff_ac3dsp_init(&c);

    check_float_to_fixed24(&c);
}
