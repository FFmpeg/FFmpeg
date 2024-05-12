/*
 * Copyright (c) 2016 Alexandra Hájková
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

#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "libavutil/mem_internal.h"

#include "libavcodec/lossless_audiodsp.h"

#include "checkasm.h"

#define randomize_buf(buf, len) \
    do { \
        for (int i = 0; i < len; i++) \
            buf[i] = rnd(); \
    } while (0)

static void check_scalarproduct_and_madd_int16(LLAudDSPContext *c)
{
#define BUF_SIZE 1088 // multiple of 16
    LOCAL_ALIGNED_16(int16_t, v1, [BUF_SIZE]);
    LOCAL_ALIGNED_16(int16_t, v2, [BUF_SIZE]);
    LOCAL_ALIGNED_16(int16_t, v3, [BUF_SIZE]);
    int mul;

    declare_func(int32_t, int16_t *, const int16_t *, const int16_t *,
                          int, int);

    randomize_buf(v1, BUF_SIZE);
    randomize_buf(v2, BUF_SIZE);
    randomize_buf(v3, BUF_SIZE);
    mul = (int16_t)rnd();

    if (check_func(c->scalarproduct_and_madd_int16,
                   "scalarproduct_and_madd_int16")) {
        LOCAL_ALIGNED_16(int16_t, dst0, [BUF_SIZE]);
        LOCAL_ALIGNED_16(int16_t, dst1, [BUF_SIZE]);
        int ref, val;

        memcpy(dst0, v1, sizeof (*dst0) * BUF_SIZE);
        memcpy(dst1, v1, sizeof (*dst1) * BUF_SIZE);
        ref = call_ref(dst0, v2, v3, BUF_SIZE, mul);
        val = call_new(dst1, v2, v3, BUF_SIZE, mul);
        if (memcmp(dst0, dst1, sizeof (*dst0) * BUF_SIZE) != 0 || ref != val)
            fail();

        bench_new(v1, v2, v3, BUF_SIZE, mul);
    }

    report("scalarproduct_and_madd_int16");
}

static void check_scalarproduct_and_madd_int32(LLAudDSPContext *c)
{
#define BUF_SIZE 1088 // multiple of 16
    LOCAL_ALIGNED_16(int16_t, v1, [BUF_SIZE]);
    LOCAL_ALIGNED_16(int32_t, v2, [BUF_SIZE]);
    LOCAL_ALIGNED_16(int16_t, v3, [BUF_SIZE]);
    int mul;

    declare_func(int32_t, int16_t *, const int32_t *, const int16_t *,
                          int, int);

    randomize_buf(v1, BUF_SIZE);
    randomize_buf(v2, BUF_SIZE);
    randomize_buf(v3, BUF_SIZE);
    mul = (int16_t)rnd();

    if (check_func(c->scalarproduct_and_madd_int32,
                   "scalarproduct_and_madd_int32")) {
        LOCAL_ALIGNED_16(int16_t, dst0, [BUF_SIZE]);
        LOCAL_ALIGNED_16(int16_t, dst1, [BUF_SIZE]);
        int ref, val;

        memcpy(dst0, v1, sizeof (*dst0) * BUF_SIZE);
        memcpy(dst1, v1, sizeof (*dst1) * BUF_SIZE);
        ref = call_ref(dst0, v2, v3, BUF_SIZE, mul);
        val = call_new(dst1, v2, v3, BUF_SIZE, mul);
        if (memcmp(dst0, dst1, sizeof (*dst0) * BUF_SIZE) != 0 || ref != val)
            fail();

        bench_new(v1, v2, v3, BUF_SIZE, mul);
    }

    report("scalarproduct_and_madd_int32");
}

void checkasm_check_llauddsp(void)
{
    LLAudDSPContext c;

    ff_llauddsp_init(&c);
    check_scalarproduct_and_madd_int16(&c);
    check_scalarproduct_and_madd_int32(&c);
}
