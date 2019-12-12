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

#include "libavcodec/opusdsp.h"

#include "checkasm.h"

#define randomize_float(buf, len)                               \
    do {                                                        \
        for (int i = 0; i < len; i++) {                         \
            float f = (float)rnd() / (UINT_MAX >> 5) - 16.0f;   \
            buf[i] = f;                                         \
        }                                                       \
    } while (0)

#define EPS 0.005
#define MAX_SIZE (960)

/* period is between 15 and 1022, inclusive */
static void test_postfilter(int period)
{
    LOCAL_ALIGNED(16, float, data0, [MAX_SIZE + 1024]);
    LOCAL_ALIGNED(16, float, data1, [MAX_SIZE + 1024]);

    /* This filter can explode very easily, so use a tapset from the codec.
     * In the codec these are usually multiplied by at least 0.09375f,
     * so its outside the largest filter value, but the filter is still stable
     * so use it. */
    float gains[3] = { 0.3066406250f, 0.2170410156f, 0.1296386719f };

    /* The codec will always call with an offset which is aligned once
     * (period + 2) is subtracted, but here we have to align it outselves. */
    int offset = FFALIGN(period + 2, 4);

    declare_func(void, float *data, int period, float *gains, int len);

    randomize_float(data0, MAX_SIZE + 1024);
    memcpy(data1, data0, (MAX_SIZE + 1024)*sizeof(float));

    call_ref(data0 + offset, period, gains, MAX_SIZE);
    call_new(data1 + offset, period, gains, MAX_SIZE);

    if (!float_near_abs_eps_array(data0 + offset, data1 + offset, EPS, MAX_SIZE))
        fail();
    bench_new(data1 + offset, period, gains, MAX_SIZE);
}

static void test_deemphasis(void)
{
    LOCAL_ALIGNED(16, float, src, [FFALIGN(MAX_SIZE, 4)]);
    LOCAL_ALIGNED(16, float, dst0, [FFALIGN(MAX_SIZE, 4)]);
    LOCAL_ALIGNED(16, float, dst1, [FFALIGN(MAX_SIZE, 4)]);
    float coeff0 = (float)rnd() / (UINT_MAX >> 5) - 16.0f, coeff1 = coeff0;

    declare_func_float(float, float *out, float *in, float coeff, int len);

    randomize_float(src, MAX_SIZE);

    coeff0 = call_ref(dst0, src, coeff0, MAX_SIZE);
    coeff1 = call_new(dst1, src, coeff1, MAX_SIZE);

    if (!float_near_abs_eps(coeff0, coeff1, EPS) ||
        !float_near_abs_eps_array(dst0, dst1, EPS, MAX_SIZE))
        fail();
    bench_new(dst1, src, coeff1, MAX_SIZE);
}

void checkasm_check_opusdsp(void)
{
    OpusDSP ctx;
    ff_opus_dsp_init(&ctx);

    if (check_func(ctx.postfilter, "postfilter_15"))
        test_postfilter(15);
    report("postfilter_15");

    if (check_func(ctx.postfilter, "postfilter_512"))
        test_postfilter(512);
    report("postfilter_512");

    if (check_func(ctx.postfilter, "postfilter_1022"))
        test_postfilter(1022);
    report("postfilter_1022");

    if (check_func(ctx.deemphasis, "deemphasis"))
        test_deemphasis();
    report("deemphasis");
}
