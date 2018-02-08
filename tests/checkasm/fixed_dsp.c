/*
 * Copyright (c) 2015 James Almer
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

#include "checkasm.h"
#include "libavutil/common.h"
#include "libavutil/fixed_dsp.h"
#include "libavutil/internal.h"
#include "libavutil/mem.h"

#define BUF_SIZE 256

#define randomize_buffers()                   \
    do {                                      \
        int i;                                \
        for (i = 0; i < BUF_SIZE; i++) {      \
            src0[i] = sign_extend(rnd(), 24); \
            src1[i] = sign_extend(rnd(), 24); \
            src2[i] = sign_extend(rnd(), 24); \
        }                                     \
    } while (0)

static void check_vector_fmul(const int *src0, const int *src1)
{
    LOCAL_ALIGNED_32(int, ref, [BUF_SIZE]);
    LOCAL_ALIGNED_32(int, new, [BUF_SIZE]);

    declare_func(void, int *dst, const int *src0, const int *src1, int len);

    call_ref(ref, src0, src1, BUF_SIZE);
    call_new(new, src0, src1, BUF_SIZE);
    if (memcmp(ref, new, BUF_SIZE * sizeof(int)))
        fail();
    bench_new(new, src0, src1, BUF_SIZE);
}

static void check_vector_fmul_add(const int *src0, const int *src1, const int *src2)
{
    LOCAL_ALIGNED_32(int, ref, [BUF_SIZE]);
    LOCAL_ALIGNED_32(int, new, [BUF_SIZE]);

    declare_func(void, int *dst, const int *src0, const int *src1, const int *src2, int len);

    call_ref(ref, src0, src1, src2, BUF_SIZE);
    call_new(new, src0, src1, src2, BUF_SIZE);
    if (memcmp(ref, new, BUF_SIZE * sizeof(int)))
        fail();
    bench_new(new, src0, src1, src2, BUF_SIZE);
}

static void check_vector_fmul_window(const int32_t *src0, const int32_t *src1, const int32_t *win)
{
    LOCAL_ALIGNED_32(int32_t, ref, [BUF_SIZE]);
    LOCAL_ALIGNED_32(int32_t, new, [BUF_SIZE]);

    declare_func(void, int32_t *dst, const int32_t *src0, const int32_t *src1, const int32_t *win, int len);

    call_ref(ref, src0, src1, win, BUF_SIZE / 2);
    call_new(new, src0, src1, win, BUF_SIZE / 2);
    if (memcmp(ref, new, BUF_SIZE * sizeof(int32_t)))
        fail();
    bench_new(new, src0, src1, win, BUF_SIZE / 2);
}

static void check_vector_fmul_window_scaled(const int32_t *src0, const int32_t *src1, const int32_t *win)
{
    LOCAL_ALIGNED_16(int16_t, ref, [BUF_SIZE]);
    LOCAL_ALIGNED_16(int16_t, new, [BUF_SIZE]);

    declare_func(void, int16_t *dst, const int32_t *src0, const int32_t *src1, const int32_t *win, int len, uint8_t bits);

    call_ref(ref, src0, src1, win, BUF_SIZE / 2, 2);
    call_new(new, src0, src1, win, BUF_SIZE / 2, 2);
    if (memcmp(ref, new, BUF_SIZE * sizeof(int16_t)))
        fail();
    bench_new(new, src0, src1, win, BUF_SIZE / 2, 2);
}

static void check_butterflies(const int *src0, const int *src1)
{
    LOCAL_ALIGNED_16(int, ref0, [BUF_SIZE]);
    LOCAL_ALIGNED_16(int, ref1, [BUF_SIZE]);
    LOCAL_ALIGNED_16(int, new0, [BUF_SIZE]);
    LOCAL_ALIGNED_16(int, new1, [BUF_SIZE]);

    declare_func(void, int *av_restrict src0, int *av_restrict src1, int len);

    memcpy(ref0, src0, BUF_SIZE * sizeof(*src0));
    memcpy(ref1, src1, BUF_SIZE * sizeof(*src1));
    memcpy(new0, src0, BUF_SIZE * sizeof(*src0));
    memcpy(new1, src1, BUF_SIZE * sizeof(*src1));

    call_ref(ref0, ref1, BUF_SIZE);
    call_new(new0, new1, BUF_SIZE);
    if (memcmp(ref0, new0, BUF_SIZE * sizeof(*ref0)) ||
        memcmp(ref1, new1, BUF_SIZE * sizeof(*ref1)))
        fail();
    memcpy(new0, src0, BUF_SIZE * sizeof(*src0));
    memcpy(new1, src1, BUF_SIZE * sizeof(*src1));
    bench_new(new0, new1, BUF_SIZE);
}

static void check_scalarproduct_fixed(const int *src0, const int *src1)
{
    int ref, new;

    declare_func(int, const int *src0, const int *src1, int len);

    ref = call_ref(src0, src1, BUF_SIZE);
    new = call_new(src0, src1, BUF_SIZE);
    if (ref != new)
        fail();
    bench_new(src0, src1, BUF_SIZE);
}

void checkasm_check_fixed_dsp(void)
{
    LOCAL_ALIGNED_32(int32_t, src0, [BUF_SIZE]);
    LOCAL_ALIGNED_32(int32_t, src1, [BUF_SIZE]);
    LOCAL_ALIGNED_32(int32_t, src2, [BUF_SIZE]);
    AVFixedDSPContext *fdsp = avpriv_alloc_fixed_dsp(1);

    randomize_buffers();
    if (check_func(fdsp->vector_fmul, "vector_fmul_fixed"))
        check_vector_fmul(src0, src1);
    if (check_func(fdsp->vector_fmul_add, "vector_fmul_add_fixed"))
        check_vector_fmul_add(src0, src1, src2);
    if (check_func(fdsp->vector_fmul_reverse, "vector_fmul_reverse_fixed"))
        check_vector_fmul(src0, src1);
    if (check_func(fdsp->vector_fmul_window, "vector_fmul_window_fixed"))
        check_vector_fmul_window(src0, src1, src2);
    if (check_func(fdsp->vector_fmul_window_scaled, "vector_fmul_window_scaled_fixed"))
        check_vector_fmul_window_scaled(src0, src1, src2);
    report("vector_fmul");
    if (check_func(fdsp->butterflies_fixed, "butterflies_fixed"))
        check_butterflies(src0, src1);
    report("butterflies_fixed");
    if (check_func(fdsp->scalarproduct_fixed, "scalarproduct_fixed"))
        check_scalarproduct_fixed(src0, src1);
    report("scalarproduct_fixed");

    av_freep(&fdsp);
}
