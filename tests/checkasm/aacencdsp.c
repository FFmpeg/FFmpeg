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

#include "libavutil/mem_internal.h"

#include "libavcodec/aacenc_utils.h"
#include "libavcodec/aacencdsp.h"
#include "libavcodec/aactab.h"

#include "checkasm.h"

#define randomize_float(buf, len)                               \
    do {                                                        \
        int i;                                                  \
        for (i = 0; i < len; i++) {                             \
            float f = (float)rnd() / (UINT_MAX >> 5) - 16.0f;   \
            buf[i] = f;                                         \
        }                                                       \
    } while (0)

#define randomize_elem(tab) (tab[rnd() % FF_ARRAY_ELEMS(tab)])

static void test_abs_pow34(AACEncDSPContext *s)
{
#define BUF_SIZE 1024
    LOCAL_ALIGNED_32(float, in, [BUF_SIZE]);

    declare_func(void, float *, const float *, int);

    randomize_float(in, BUF_SIZE);

    if (check_func(s->abs_pow34, "abs_pow34")) {
        LOCAL_ALIGNED_32(float, out, [BUF_SIZE]);
        LOCAL_ALIGNED_32(float, out2, [BUF_SIZE]);

        call_ref(out, in, BUF_SIZE);
        call_new(out2, in, BUF_SIZE);

        if (!float_near_ulp_array(out, out2, 1, BUF_SIZE))
            fail();

        bench_new(out, in, BUF_SIZE);
    }

    report("abs_pow34");
}

static void test_quant_bands(AACEncDSPContext *s)
{
    int maxval = randomize_elem(aac_cb_maxval);
    float q34 = (float)rnd() / (UINT_MAX / 1024);
    float rounding = (rnd() & 1) ? ROUND_TO_ZERO : ROUND_STANDARD;
    LOCAL_ALIGNED_16(float, in, [BUF_SIZE]);
    LOCAL_ALIGNED_16(float, scaled, [BUF_SIZE]);

    declare_func(void, int *, const float *, const float *, int, int, int,
                 const float, const float);

    randomize_float(in, BUF_SIZE);
    randomize_float(scaled, BUF_SIZE);

    for (int sign = 0; sign <= 1; sign++) {
        if (check_func(s->quant_bands, "quant_bands_%s",
                       sign ? "signed" : "unsigned")) {
            LOCAL_ALIGNED_32(int, out, [BUF_SIZE]);
            LOCAL_ALIGNED_32(int, out2, [BUF_SIZE]);

            call_ref(out, in, scaled, BUF_SIZE, sign, maxval, q34, rounding);
            call_new(out2, in, scaled, BUF_SIZE, sign, maxval, q34, rounding);

            if (memcmp(out, out2, BUF_SIZE * sizeof (int)))
                fail();

            bench_new(out, in, scaled, BUF_SIZE, sign, maxval, q34, rounding);
        }
    }

    report("quant_bands");
}

void checkasm_check_aacencdsp(void)
{
    AACEncDSPContext s = { 0 };
    ff_aacenc_dsp_init(&s);

    test_abs_pow34(&s);
    test_quant_bands(&s);
}
