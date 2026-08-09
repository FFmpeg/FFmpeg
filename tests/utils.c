/*
 * Copyright © 2026, Niklas Haas
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "tests.h"

#include "src/internal.h"

static CHECKASM_ALIGN(union {
    uint8_t  u8[4096];
    uint16_t u16[2048];
    float    f32[1024];
    double   f64[512];
}) buf;

/* We need a wrapper function because checkasm_declare() can't handle void
 * parameter lists. This should inline into the benchmark loop. */
#define WRAP_VOID(RETVAL, FUNC)                                                          \
    static ALWAYS_INLINE RETVAL wrap_##FUNC(int unused)                                  \
    {                                                                                    \
        return FUNC();                                                                   \
    }

WRAP_VOID(int,      checkasm_rand)
WRAP_VOID(int8_t,   checkasm_rand_int8)
WRAP_VOID(uint8_t,  checkasm_rand_uint8)
WRAP_VOID(int16_t,  checkasm_rand_int16)
WRAP_VOID(uint16_t, checkasm_rand_uint16)
WRAP_VOID(int32_t,  checkasm_rand_int32)
WRAP_VOID(uint32_t, checkasm_rand_uint32)
WRAP_VOID(float,    checkasm_rand_float32)
WRAP_VOID(int64_t,  checkasm_rand_int64)
WRAP_VOID(uint64_t, checkasm_rand_uint64)
WRAP_VOID(double,   checkasm_rand_float64)

static void selftest_test_prng(void)
{
#define CHECK_RAND(RETVAL, FUNC)                                                         \
    if (checkasm_check_func(checkasm_##FUNC, #FUNC)) {                                   \
        checkasm_declare(RETVAL, int);                                                   \
        checkasm_bench(wrap_checkasm_##FUNC, 0);                                         \
    }

    CHECK_RAND(int,      rand)
    CHECK_RAND(int8_t,   rand_int8)
    CHECK_RAND(uint8_t,  rand_uint8)
    CHECK_RAND(int16_t,  rand_int16)
    CHECK_RAND(uint16_t, rand_uint16)
    CHECK_RAND(int32_t,  rand_int32)
    CHECK_RAND(uint32_t, rand_uint32)
    CHECK_RAND(float,    rand_float32)
    CHECK_RAND(int64_t,  rand_int64)
    CHECK_RAND(uint64_t, rand_uint64)
    CHECK_RAND(double,   rand_float64)

    checkasm_report("prng");
}

static void selftest_test_randomize(void)
{
    if (checkasm_check_func(checkasm_randomize, "randomize")) {
        checkasm_declare(void, void *buf, size_t bytes);
        checkasm_bench_new(buf.u8, sizeof(buf.u8));
    }

    if (checkasm_check_func(checkasm_randomize_mask8, "randomize_mask8")) {
        checkasm_declare(void, uint8_t *buf, int width, uint8_t mask);
        checkasm_bench_new(buf.u8, ARRAY_SIZE(buf.u8), 0x7F);
    }

    if (checkasm_check_func(checkasm_randomize_mask16, "randomize_mask16")) {
        checkasm_declare(void, uint16_t *buf, int width, uint16_t mask);
        checkasm_bench_new(buf.u16, ARRAY_SIZE(buf.u16), 0x7FFF);
    }

    if (checkasm_check_func(checkasm_randomize_range, "randomize_range")) {
        checkasm_declare(void, double *buf, int width, double range);
        checkasm_bench_new(buf.f64, ARRAY_SIZE(buf.f64), 100.0);
    }

    if (checkasm_check_func(checkasm_randomize_rangef, "randomize_rangef")) {
        checkasm_declare(void, float *buf, int width, float range);
        checkasm_bench_new(buf.f32, ARRAY_SIZE(buf.f32), 100.0f);
    }

    if (checkasm_check_func(checkasm_randomize_interval, "randomize_interval")) {
        checkasm_declare(void, double *buf, int width, double low, double high);
        checkasm_bench_new(buf.f64, ARRAY_SIZE(buf.f64), -100.0, 100.0);
    }

    if (checkasm_check_func(checkasm_randomize_intervalf, "randomize_intervalf")) {
        checkasm_declare(void, float *buf, int width, float low, float high);
        checkasm_bench_new(buf.f32, ARRAY_SIZE(buf.f32), -100.0f, 100.0f);
    }

    if (checkasm_check_func(checkasm_randomize_dist, "randomize_dist")) {
        checkasm_declare(void, double *buf, int width, CheckasmDist dist);
        checkasm_bench_new(buf.f64, ARRAY_SIZE(buf.f64), checkasm_dist_standard);
    }

    if (checkasm_check_func(checkasm_randomize_distf, "randomize_distf")) {
        checkasm_declare(void, float *buf, int width, CheckasmDist dist);
        checkasm_bench_new(buf.f32, ARRAY_SIZE(buf.f32), checkasm_dist_standard);
    }

    if (checkasm_check_func(checkasm_randomize_norm, "randomize_norm")) {
        checkasm_declare(void, double *buf, int width);
        checkasm_bench_new(buf.f64, ARRAY_SIZE(buf.f64));
    }

    if (checkasm_check_func(checkasm_randomize_normf, "randomize_normf")) {
        checkasm_declare(void, float *buf, int width);
        checkasm_bench_new(buf.f32, ARRAY_SIZE(buf.f32));
    }

    checkasm_report("randomize");
}

static void selftest_test_clear(void)
{
    if (checkasm_check_func(checkasm_clear, "clear")) {
        checkasm_declare(void, void *buf, size_t bytes);
        checkasm_bench_new(buf.u8, sizeof(buf.u8));
    }

    if (checkasm_check_func(checkasm_clear8, "clear8")) {
        checkasm_declare(void, uint8_t *buf, int width, uint8_t val);
        checkasm_bench_new(buf.u8, ARRAY_SIZE(buf.u8), 0xAA);
    }

    if (checkasm_check_func(checkasm_clear16, "clear16")) {
        checkasm_declare(void, uint16_t *buf, int width, uint16_t val);
        checkasm_bench_new(buf.u16, ARRAY_SIZE(buf.u16), 0xAAAA);
    }

    checkasm_report("clear");
}

static void selftest_test_init(void)
{
    if (checkasm_check_func(checkasm_init, "init")) {
        checkasm_declare(void, void *buf, size_t bytes);
        checkasm_bench_new(buf.u8, sizeof(buf.u8));
    }

    if (checkasm_check_func(checkasm_init_mask8, "init_mask8")) {
        checkasm_declare(void, uint8_t *buf, int width, uint8_t mask);
        checkasm_bench_new(buf.u8, ARRAY_SIZE(buf.u8), 0x7F);
    }

    if (checkasm_check_func(checkasm_init_mask16, "init_mask16")) {
        checkasm_declare(void, uint16_t *buf, int width, uint16_t mask);
        checkasm_bench_new(buf.u16, ARRAY_SIZE(buf.u16), 0x7FFF);
    }

    checkasm_report("init");
}

void selftest_check_utils(void)
{
    selftest_test_prng();
    selftest_test_randomize();
    selftest_test_clear();
    selftest_test_init();
}
