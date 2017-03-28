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

#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "libavcodec/audiodsp.h"

#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"

#include "checkasm.h"

#define MAX_SIZE (32 * 128)

#define randomize_float(buf, len)                               \
    do {                                                        \
        int i;                                                  \
        for (i = 0; i < len; i++) {                             \
            float f = (float)rnd() / (UINT_MAX >> 5) - 16.0f;   \
            buf[i] = f;                                         \
        }                                                       \
    } while (0)

#define randomize_int(buf, len, size, bits)                         \
    do {                                                            \
        int i;                                                      \
        for (i = 0; i < len; i++) {                                 \
            uint ## size ## _t r = rnd() & ((1LL << bits) - 1);     \
            AV_WN ## size ## A(buf + i, -(1LL << (bits - 1)) + r);  \
        }                                                           \
    } while (0)

void checkasm_check_audiodsp(void)
{
    AudioDSPContext adsp;

    ff_audiodsp_init(&adsp);

    if (check_func(adsp.scalarproduct_int16, "audiodsp.scalarproduct_int16")) {
        LOCAL_ALIGNED(32, int16_t, v1, [MAX_SIZE]);
        LOCAL_ALIGNED(32, int16_t, v2, [MAX_SIZE]);
        unsigned int len_bits_minus4, v1_bits, v2_bits, len;
        int32_t res0, res1;

        declare_func_emms(AV_CPU_FLAG_MMX, int32_t, const int16_t *v1, const int16_t *v2, int len);

        // generate random 5-12bit vector length
        len_bits_minus4 = rnd() % 8;
        len = rnd() & ((1 << len_bits_minus4) - 1);
        len = 16 * FFMAX(len, 1);

        // generate the bit counts for each of the vectors such that the result
        // fits into int32
        v1_bits = 1 + rnd() % 15;
        v2_bits = FFMIN(32 - (len_bits_minus4 + 4) - v1_bits - 1, 15);

        randomize_int(v1, MAX_SIZE, 16, v1_bits + 1);
        randomize_int(v2, MAX_SIZE, 16, v2_bits + 1);

        res0 = call_ref(v1, v2, len);
        res1 = call_new(v1, v2, len);
        if (res0 != res1)
            fail();
        bench_new(v1, v2, MAX_SIZE);
    }

    if (check_func(adsp.vector_clip_int32, "audiodsp.vector_clip_int32")) {
        LOCAL_ALIGNED(32, int32_t, src,  [MAX_SIZE]);
        LOCAL_ALIGNED(32, int32_t, dst0, [MAX_SIZE]);
        LOCAL_ALIGNED(32, int32_t, dst1, [MAX_SIZE]);
        int32_t val1, val2, min, max;
        int len;

        declare_func_emms(AV_CPU_FLAG_MMX, void, int32_t *dst, const int32_t *src,
                          int32_t min, int32_t max, unsigned int len);

        val1 = ((int32_t)rnd());
        val1 = FFSIGN(val1) * (val1 & ((1 << 24) - 1));
        val2 = ((int32_t)rnd());
        val2 = FFSIGN(val2) * (val2 & ((1 << 24) - 1));

        min = FFMIN(val1, val2);
        max = FFMAX(val1, val2);

        randomize_int(src, MAX_SIZE, 32, 32);

        len = rnd() % 128;
        len = 32 * FFMAX(len, 1);

        call_ref(dst0, src, min, max, len);
        call_new(dst1, src, min, max, len);
        if (memcmp(dst0, dst1, len * sizeof(*dst0)))
            fail();
        bench_new(dst1, src, min, max, MAX_SIZE);
    }

    if (check_func(adsp.vector_clipf, "audiodsp.vector_clipf")) {
        LOCAL_ALIGNED(32, float, src, [MAX_SIZE]);
        LOCAL_ALIGNED(32, float, dst0, [MAX_SIZE]);
        LOCAL_ALIGNED(32, float, dst1, [MAX_SIZE]);
        float val1, val2, min, max;
        int i, len;

        declare_func_emms(AV_CPU_FLAG_MMX, void, float *dst, const float *src,
                          int len, float min, float max);

        val1 = (float)rnd() / (UINT_MAX >> 1) - 1.0f;
        val2 = (float)rnd() / (UINT_MAX >> 1) - 1.0f;

        min = FFMIN(val1, val2);
        max = FFMAX(val1, val2);

        randomize_float(src, MAX_SIZE);

        len = rnd() % 128;
        len = 16 * FFMAX(len, 1);

        call_ref(dst0, src, len, min, max);
        call_new(dst1, src, len, min, max);
        for (i = 0; i < len; i++) {
            if (!float_near_ulp_array(dst0, dst1, 3, len))
                fail();
        }
        bench_new(dst1, src, MAX_SIZE, min, max);
    }

    report("audiodsp");
}
