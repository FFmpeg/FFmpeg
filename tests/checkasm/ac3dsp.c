/*
 * Copyright (c) 2023 Institue of Software Chinese Academy of Sciences (ISCAS).
 * Copyright (c) 2024 Geoff Hill <geoff@geoffhill.org>
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

#include <stdint.h>
#include <string.h>

#include "libavutil/mem.h"
#include "libavutil/mem_internal.h"

#include "libavcodec/ac3dsp.h"

#include "checkasm.h"

#define randomize_exp(buf, len)        \
    do {                               \
        int i;                         \
        for (i = 0; i < len; i++) {    \
            buf[i] = (uint8_t)rnd();   \
        }                              \
    } while (0)

#define randomize_i24(buf, len)          \
    do {                                 \
        int i;                           \
        for (i = 0; i < len; i++) {      \
            int32_t v = (int32_t)rnd();  \
            int32_t u = (v & 0xFFFFFF);  \
            buf[i] = (v < 0) ? -u : u;   \
        }                                \
    } while (0)

#define randomize_float(buf, len)                               \
    do {                                                        \
        int i;                                                  \
        for (i = 0; i < len; i++) {                             \
            float f = (float)rnd() / (UINT_MAX >> 5) - 16.0f;   \
            buf[i] = f;                                         \
        }                                                       \
    } while (0)

static void check_ac3_exponent_min(AC3DSPContext *c) {
#define MAX_COEFS 256
#define MAX_CTXT 6
#define EXP_SIZE (MAX_CTXT * MAX_COEFS)

    LOCAL_ALIGNED_16(uint8_t, src, [EXP_SIZE]);
    LOCAL_ALIGNED_16(uint8_t, v1, [EXP_SIZE]);
    LOCAL_ALIGNED_16(uint8_t, v2, [EXP_SIZE]);
    int n;

    declare_func(void, uint8_t *, int, int);

    for (n = 0; n < MAX_CTXT; ++n) {
        if (check_func(c->ac3_exponent_min, "ac3_exponent_min_reuse%d", n)) {
            randomize_exp(src, EXP_SIZE);

            memcpy(v1, src, EXP_SIZE);
            memcpy(v2, src, EXP_SIZE);

            call_ref(v1, n, MAX_COEFS);
            call_new(v2, n, MAX_COEFS);

            if (memcmp(v1, v2, EXP_SIZE) != 0)
                fail();

            bench_new(v2, n, MAX_COEFS);
        }
    }

    report("ac3_exponent_min");
}

static void check_ac3_extract_exponents(AC3DSPContext *c) {
#define MAX_EXPS 3072
    LOCAL_ALIGNED_16(int32_t, src, [MAX_EXPS]);
    LOCAL_ALIGNED_16(uint8_t, v1, [MAX_EXPS]);
    LOCAL_ALIGNED_16(uint8_t, v2, [MAX_EXPS]);
    int n;

    declare_func(void, uint8_t *, int32_t *, int);

    for (n = 512; n <= MAX_EXPS; n += 256) {
        if (check_func(c->extract_exponents, "ac3_extract_exponents_n%d", n)) {
            randomize_i24(src, n);

            call_ref(v1, src, n);
            call_new(v2, src, n);

            if (memcmp(v1, v2, n) != 0)
                fail();

            bench_new(v1, src, n);
        }
    }

    report("ac3_extract_exponents");
}

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

static void check_ac3_sum_square_butterfly_int32(AC3DSPContext *c) {
#define ELEMS 240
    LOCAL_ALIGNED_16(int32_t, lt, [ELEMS]);
    LOCAL_ALIGNED_16(int32_t, rt, [ELEMS]);
    LOCAL_ALIGNED_16(uint64_t, v1, [4]);
    LOCAL_ALIGNED_16(uint64_t, v2, [4]);

    declare_func(void, int64_t[4], const int32_t *, const int32_t *, int);

    randomize_i24(lt, ELEMS);
    randomize_i24(rt, ELEMS);

    if (check_func(c->sum_square_butterfly_int32,
                   "ac3_sum_square_bufferfly_int32")) {
        call_ref(v1, lt, rt, ELEMS);
        call_new(v2, lt, rt, ELEMS);

        if (memcmp(v1, v2, sizeof(int64_t[4])) != 0)
            fail();

        bench_new(v2, lt, rt, ELEMS);
    }

    report("ac3_sum_square_butterfly_int32");
}

static void check_ac3_sum_square_butterfly_float(AC3DSPContext *c) {
    LOCAL_ALIGNED_32(float, lt, [ELEMS]);
    LOCAL_ALIGNED_32(float, rt, [ELEMS]);
    LOCAL_ALIGNED_16(float, v1, [4]);
    LOCAL_ALIGNED_16(float, v2, [4]);

    declare_func(void, float[4], const float *, const float *, int);

    randomize_float(lt, ELEMS);
    randomize_float(rt, ELEMS);

    if (check_func(c->sum_square_butterfly_float,
                   "ac3_sum_square_bufferfly_float")) {
        call_ref(v1, lt, rt, ELEMS);
        call_new(v2, lt, rt, ELEMS);

        if (!float_near_ulp_array(v1, v2, 11, 4))
            fail();

        bench_new(v2, lt, rt, ELEMS);
    }

    report("ac3_sum_square_butterfly_float");
}

void checkasm_check_ac3dsp(void)
{
    AC3DSPContext c;
    ff_ac3dsp_init(&c);

    check_ac3_exponent_min(&c);
    check_ac3_extract_exponents(&c);
    check_float_to_fixed24(&c);
    check_ac3_sum_square_butterfly_int32(&c);
    check_ac3_sum_square_butterfly_float(&c);
}
