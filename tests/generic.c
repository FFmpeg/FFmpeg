/*
 * Copyright © 2024, Marvin Scholz
 * Copyright © 2019, VideoLAN and dav1d authors
 * Copyright © 2019, Two Orioles, LLC
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

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tests.h"

void selftest_test_copy(copy_func fun, const char *name, const int min_width)
{
#define WIDTH 256
    BUF_RECT(uint8_t, c_dst, WIDTH, 1);
    BUF_RECT(uint8_t, a_dst, WIDTH, 1);

    CHECKASM_ALIGN(uint8_t src[WIDTH]);
    INITIALIZE_BUF(src);

    checkasm_declare(void, uint8_t *dest, const uint8_t *src, size_t n);

    for (int w = min_width; w <= WIDTH; w *= 2) {
        if (checkasm_check_func(fun, "%s_%d", name, w)) {
            CLEAR_BUF_RECT(c_dst);
            CLEAR_BUF_RECT(a_dst);

            /* Make sure that the destination buffer actually differs from
             * the source buffer, to make sure that a skipped write does
             * trigger a failure. */
            for (int i = 0; i < w; i++)
                c_dst[i] = a_dst[i] = ~src[i];
            checkasm_call_ref(c_dst, src, w);
            checkasm_call_new(a_dst, src, w);

            /* Test all checkasm_check() variants */
            checkasm_check1d(uint8_t, c_dst, a_dst, w, "1d");
            checkasm_check2d(uint8_t, c_dst, 0, a_dst, 0, w, 1, "2d");
            checkasm_check_rect(c_dst, c_dst_stride, a_dst, a_dst_stride, w, 1, "rect");

            checkasm_check1d_padded(uint8_t, c_dst, a_dst, w, "1d_padded", 1, 64);
            checkasm_check2d_padded(uint8_t, c_dst, 0, a_dst, 0, w, 1,
                                    "2d_padded", 1, 1, 16);
            checkasm_check_rect_padded(c_dst, c_dst_stride, a_dst, a_dst_stride, w, 1,
                                       "rect_padded");

            checkasm_check_rect_padded_align(c_dst, c_dst_stride, a_dst, a_dst_stride, w, 1,
                                             "rect_align", 1, 1);

            checkasm_bench_new(a_dst, src, w);
        }
    }

    checkasm_report("%s", name);
#undef WIDTH
}

void selftest_test_noop(noop_func fun, const char *name)
{
    checkasm_declare(void, int);

    if (checkasm_check_func(fun, "%s", name)) {
        /* don't call unchecked because some of these functions are designed to
         * e.g. intentionally corrupt the stack */
        (void) checkasm_func_ref;
        checkasm_call_new(0);
    }

    checkasm_report("%s", name);
}

void selftest_test_float(float_func fun, const char *name, const float input)
{
    checkasm_declare(float, float);

    if (checkasm_check_func(fun, "%s", name)) {
        float x = checkasm_call_ref(input);
        float y = checkasm_call_new(input);
        if (!checkasm_float_near_abs_eps(x, y, FLT_EPSILON)) {
            if (checkasm_fail())
                fprintf(stderr, "expected %f, got %f\n", x, y);
        }
    }

    checkasm_report("%s", name);
}

static void selftest_test_double(double_func fun, const char *name,
                                 const double input)
{
    checkasm_declare(double, double);

    if (checkasm_check_func(fun, "%s", name)) {
        double x = checkasm_call_ref(input);
        double y = checkasm_call_new(input);

        if (!checkasm_double_near_abs_eps(x, y, DBL_EPSILON)) {
            if (checkasm_fail())
                fprintf(stderr, "expected %f, got %f\n", x, y);
        }
    }

    checkasm_report("%s", name);
}

static DEF_COPY_FUNC(overwrite_left)
{
    memcpy(dst, src, size);
    dst[-1] = dst[-2] = dst[-3] = dst[-4] = 0xAC;
}

static DEF_COPY_FUNC(overwrite_right)
{
    memcpy(dst, src, size);
    dst[size] = dst[size + 1] = dst[size + 2] = dst[size + 3] = 0xAC;
}

static DEF_COPY_FUNC(underwrite)
{
    if (size < 4)
        return;
    memcpy(dst, src, size - 4);
}

static DEF_NOOP_FUNC(segfault)
{
    volatile int *bad = NULL;
    *bad              = 0;
}

static DEF_FLOAT_FUNC(sqrt)
{
    return sqrtf(input);
}

static int identity_ref(const int x)
{
    return x;
}

/* Just make this a separate function to test the checked wrappers */
static int identity_new(const int x)
{
    return x;
}

static void selftest_test_retval(void)
{
    const uint64_t flags = checkasm_get_cpu_flags();

    checkasm_declare(int, int);

    if (checkasm_check_func(flags ? identity_new : identity_ref, "identity")) {
        for (int i = 0; i < 10; i++) {
            int x = checkasm_call_ref(i);
            int y = checkasm_call_new(i);
            if (x != y) {
                if (checkasm_fail())
                    fprintf(stderr, "expected %d, got %d\n", x, y);
            }
        }
    }

    checkasm_report("identity");
}

static int truncate_c(const float x)
{
    return (int) x;
}

static void selftest_test_float_arg(void)
{
    checkasm_declare(int, float);

    if (checkasm_check_func(truncate_c, "truncate")) {
        for (float f = 0.0f; f <= 10.0f; f += 0.5f) {
            int x = checkasm_call_ref(f);
            int y = checkasm_call_new(f);
            if (x != y) {
                if (checkasm_fail())
                    fprintf(stderr, "expected %d, got %d\n", x, y);
            }
        }
    }

    checkasm_report("truncate");
}

static void selftest_test_double_arg(void)
{
    checkasm_declare(long, double);

    if (checkasm_check_func(lrint, "lrint")) {
        for (float f = 0.0f; f <= 10.0f; f += 0.5f) {
            long x = checkasm_call_ref(f);
            long y = checkasm_call_new(f);

            if (x != y) {
                if (checkasm_fail())
                    fprintf(stderr, "expected %ld, got %ld\n", x, y);
            }
        }
    }

    checkasm_report("lrint");
}

DEF_COPY_GETTER(SELFTEST_CPU_FLAG_BAD_C, overwrite_left)
DEF_COPY_GETTER(SELFTEST_CPU_FLAG_BAD_C, overwrite_right)
DEF_COPY_GETTER(SELFTEST_CPU_FLAG_BAD_C, underwrite)
DEF_NOOP_GETTER(SELFTEST_CPU_FLAG_BAD_C, segfault)

/* Ensure we can call declare_func() inside check_func() */
static void selftest_test_check_declare(void)
{
    /* Pick a function that will actually crash, to ensure the error
     * handling still works in this case */
    noop_func *func = get_segfault();

    if (checkasm_check_func(func, "check_declare")) {
        checkasm_declare(void, int);

        checkasm_call_ref(0);
        checkasm_call_new(0);
    }

    checkasm_report("check_declare");
}

typedef int (int_func)(int);
static int wrapper(int_func *func, int arg)
{
    return func(arg);
}

static void selftest_test_wrappers(void)
{
    if (checkasm_check_func(identity_ref, "override_funcs")) {
        checkasm_declare(int, int);
        int x = checkasm_call(identity_ref, 12345);
        int y = checkasm_call_checked(identity_new, 12345);
        if (x != y)
            checkasm_fail();
    }

    if (checkasm_check_func(identity_ref, "wrapper_func")) {
        checkasm_declare(int, int_func *, int); // type of wrapper
        int x = checkasm_call(wrapper, (int_func *) checkasm_key_ref, 12345);
        int y = checkasm_call_checked(wrapper, (int_func *) checkasm_key_new, 12345);
        if (x != y)
            checkasm_fail();
    }

    checkasm_report("wrappers");
}

static void selftest_test_variants(void)
{
    for (int i = 0; i < 2; i++) {
        checkasm_set_func_variant(i ? "new" : "ref");
        if (checkasm_check_func(i ? identity_new : identity_ref, "func_id")) {
            checkasm_declare(int, int);
            int x = checkasm_call_ref(12345);
            int y = checkasm_call_new(12345);
            if (x != y)
                checkasm_fail();
            checkasm_bench_new(12345);
        }
    }

    checkasm_report("func_id");
}

void selftest_check_generic(void)
{
    selftest_test_copy(selftest_copy_c, "copy_generic", 1);
    selftest_test_float(selftest_sqrt, "sqrt_generic", 2.0f);
    selftest_test_float_arg();
    selftest_test_double(sqrt, "sqrt", 2.);
    selftest_test_double_arg();
    selftest_test_retval();
    selftest_test_wrappers();
    selftest_test_variants();

    if (!checkasm_should_fail(SELFTEST_CPU_FLAG_BAD_C))
        return;

    selftest_test_copy(get_overwrite_left(), "overwrite_left", 1);
    selftest_test_copy(get_overwrite_right(), "overwrite_right", 1);
    selftest_test_copy(get_underwrite(), "underwrite", 1);
    selftest_test_noop(get_segfault(), "segfault");
    selftest_test_check_declare();
}
