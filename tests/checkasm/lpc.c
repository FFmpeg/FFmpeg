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

#include "libavutil/avassert.h"
#include "libavutil/mem_internal.h"

#include "libavcodec/lpc.h"

#include "checkasm.h"

#define randomize_int32(buf, len)                                         \
    do {                                                                  \
        for (int i = 0; i < len; i++) {                                   \
            int32_t f = ((int)(UINT32_MAX >> 17)) - ((int)(rnd() >> 16)); \
            buf[i] = f;                                                   \
        }                                                                 \
    } while (0)

#define EPS 0.005

static void test_window(int len)
{
    LOCAL_ALIGNED(16, int32_t, src, [5000]);
    LOCAL_ALIGNED(16, double, dst0, [5000]);
    LOCAL_ALIGNED(16, double, dst1, [5000]);

    declare_func(void, const int32_t *in, ptrdiff_t len, double *out);

    randomize_int32(src, len);

    call_ref(src, len, dst0);
    call_new(src, len, dst1);

    for (int i = 0; i < len; i++) {
        if (!double_near_abs_eps(dst0[i], dst1[i], EPS)) {
            fprintf(stderr, "%d: %- .12f - %- .12f = % .12g\n",
                    i, dst0[i], dst1[i], dst0[i] - dst1[i]);
            fail();
            break;
        }
    }

    bench_new(src, 4608 + (len & 1), dst1);
}

static void test_compute_autocorr(ptrdiff_t len, int lag)
{
    const double eps = EPS * (double)len;
    LOCAL_ALIGNED(32, double, src, [5000 + 2 + MAX_LPC_ORDER]);
    LOCAL_ALIGNED(16, double, dst0, [MAX_LPC_ORDER + 1]);
    LOCAL_ALIGNED(16, double, dst1, [MAX_LPC_ORDER + 1]);

    declare_func(void, const double *in, ptrdiff_t len, int lag, double *out);

    av_assert0(lag >= 0 && lag <= MAX_LPC_ORDER);

    for (int i = 0; i < MAX_LPC_ORDER; i++)
        src[i] = 0.;

    src += MAX_LPC_ORDER;

    for (int i = 0; i < 5000 + 2; i++) {
        src[i] = (double)rnd() / (double)UINT_MAX;
    }

    call_ref(src, len, lag, dst0);
    call_new(src, len, lag, dst1);

    for (size_t i = 0; i <= lag; i++) {
        if (!double_near_abs_eps(dst0[i], dst1[i], eps)) {
            fprintf(stderr, "%zu: %- .12f - %- .12f = % .12g\n",
                    i, dst0[i], dst1[i], dst0[i] - dst1[i]);
            fail();
            break;
        }
    }

    bench_new(src, 4608 + (len & 1), lag, dst1);
}

void checkasm_check_lpc(void)
{
    LPCContext ctx;
    int len = 2000 + rnd() % 3000;
    static const int lags[] = { 8, 12, };

    ff_lpc_init(&ctx, 32, 16, FF_LPC_TYPE_DEFAULT);

    if (check_func(ctx.lpc_apply_welch_window, "apply_welch_window_even")) {
        test_window(len & ~1);
    }
    report("apply_welch_window_even");

    if (check_func(ctx.lpc_apply_welch_window, "apply_welch_window_odd")) {
        test_window(len | 1);
    }
    report("apply_welch_window_odd");
    ff_lpc_end(&ctx);

    for (size_t i = 0; i < FF_ARRAY_ELEMS(lags); i++) {
        ff_lpc_init(&ctx, len, lags[i], FF_LPC_TYPE_DEFAULT);
        if (check_func(ctx.lpc_compute_autocorr, "autocorr_%d_even", lags[i]))
            test_compute_autocorr(len & ~1, lags[i]);
#if !ARCH_X86
        if (check_func(ctx.lpc_compute_autocorr, "autocorr_%d_odd", lags[i]))
            test_compute_autocorr(len | 1, lags[i]);
#endif
        ff_lpc_end(&ctx);
    }
    report("compute_autocorr");
}
