/*
 * Copyright (c) 2024 Rémi Denis-Courmont
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

#include "libavcodec/h263dsp.h"
#include "libavutil/mem.h"
#include "libavutil/mem_internal.h"

typedef void (*filter)(uint8_t *src, int stride, int qscale);

static void check_loop_filter(char dim, filter func)
{
    LOCAL_ALIGNED_16(uint8_t, buf0, [32 * 32]);
    LOCAL_ALIGNED_16(uint8_t, buf1, [32 * 32]);
    int qscale = rnd() % 32;

    declare_func_emms(AV_CPU_FLAG_MMX, void, uint8_t *, int, int);

    for (size_t y = 0; y < 32; y++)
        for (size_t x = 0; x < 32; x++)
            buf0[y * 32 + x] = buf1[y * 32 + x] = rnd();

    if (check_func(func, "h263dsp.%c_loop_filter", dim)) {
        call_ref(buf0 + 8 * 33, 32, qscale);
        call_new(buf1 + 8 * 33, 32, qscale);

        if (memcmp(buf0, buf1, 32 * 32))
            fail();

        bench_new(buf1 + 8 * 33, 32, 1);
    }
}

void checkasm_check_h263dsp(void)
{
    H263DSPContext ctx;

    ff_h263dsp_init(&ctx);
    check_loop_filter('h', ctx.h263_h_loop_filter);
    check_loop_filter('v', ctx.h263_v_loop_filter);
    report("loop_filter");
}
