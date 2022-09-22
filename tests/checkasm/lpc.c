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

#include "libavutil/mem_internal.h"

#include "libavcodec/lpc.h"

#include "checkasm.h"

#define randomize_int32(buf, len)       \
    do {                                \
        for (int i = 0; i < len; i++) { \
            int32_t f = rnd() >> 8;     \
            buf[i] = f;                 \
        }                               \
    } while (0)

#define EPS 0.005

static void test_window(int len)
{
    LOCAL_ALIGNED(16, int32_t, src, [5000]);
    LOCAL_ALIGNED(16, double, dst0, [5000]);
    LOCAL_ALIGNED(16, double, dst1, [5000]);

    declare_func(void, int32_t *in, int len, double *out);

    randomize_int32(src, len);

    call_ref(src, len, dst0);
    call_new(src, len, dst1);

    if (!double_near_abs_eps_array(dst0, dst1, EPS, len))
        fail();

    bench_new(src, len, dst1);
}

void checkasm_check_lpc(void)
{
    LPCContext ctx;
    ff_lpc_init(&ctx, 32, 16, FF_LPC_TYPE_DEFAULT);

    if (check_func(ctx.lpc_apply_welch_window, "apply_welch_window_even")) {
        for (int i = 0; i < 64; i += 2)
            test_window(i);
    }
    report("apply_welch_window_even");

    if (check_func(ctx.lpc_apply_welch_window, "apply_welch_window_odd")) {
        for (int i = 1; i < 64; i += 2)
            test_window(i);
    }
    report("apply_welch_window_odd");

    if (check_func(ctx.lpc_apply_welch_window, "apply_welch_window_2560"))
        test_window(2560);
    report("apply_welch_window_2560");

    if (check_func(ctx.lpc_apply_welch_window, "apply_welch_window_4096"))
        test_window(4096);
    report("apply_welch_window_4096");

    if (check_func(ctx.lpc_apply_welch_window, "apply_welch_window_4097"))
        test_window(4097);
    report("apply_welch_window_4097");

    ff_lpc_end(&ctx);
}
