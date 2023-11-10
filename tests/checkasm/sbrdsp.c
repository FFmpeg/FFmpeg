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

#include "libavcodec/sbrdsp.h"
#include <float.h>

#include "checkasm.h"

#define randomize(buf, len) do {                                \
    int i;                                                      \
    for (i = 0; i < len; i++) {                                 \
        const INTFLOAT f = (INTFLOAT)rnd() / UINT_MAX;          \
        (buf)[i] = f;                                           \
    }                                                           \
} while (0)

#define EPS 0.0001

static void test_sum64x5(void)
{
    LOCAL_ALIGNED_16(INTFLOAT, dst0, [64 + 256]);
    LOCAL_ALIGNED_16(INTFLOAT, dst1, [64 + 256]);

    declare_func(void, INTFLOAT *z);

    randomize((INTFLOAT *)dst0, 64 + 256);
    memcpy(dst1, dst0, (64 + 256) * sizeof(INTFLOAT));
    call_ref(dst0);
    call_new(dst1);
    if (!float_near_abs_eps_array(dst0, dst1, EPS, 64 + 256))
        fail();
    bench_new(dst1);
}

static void test_sum_square(void)
{
    INTFLOAT res0;
    INTFLOAT res1;
    LOCAL_ALIGNED_16(INTFLOAT, src, [256], [2]);
    double t = 4 * 256;

    declare_func_float(INTFLOAT, INTFLOAT (*x)[2], int n);

    randomize((INTFLOAT *)src, 256 * 2);
    res0 = call_ref(src, 256);
    res1 = call_new(src, 256);
    if (!float_near_abs_eps(res0, res1, t * 2 * FLT_EPSILON))
        fail();
    bench_new(src, 256);
}

static void test_neg_odd_64(void)
{
    LOCAL_ALIGNED_16(INTFLOAT, dst0, [64]);
    LOCAL_ALIGNED_16(INTFLOAT, dst1, [64]);

    declare_func(void, INTFLOAT *x);

    randomize((INTFLOAT *)dst0, 64);
    memcpy(dst1, dst0, (64) * sizeof(INTFLOAT));
    call_ref(dst0);
    call_new(dst1);
    if (!float_near_abs_eps_array(dst0, dst1, EPS, 64))
        fail();
    bench_new(dst1);
}

static void test_qmf_pre_shuffle(void)
{
    LOCAL_ALIGNED_16(INTFLOAT, dst0, [128]);
    LOCAL_ALIGNED_16(INTFLOAT, dst1, [128]);

    declare_func(void, INTFLOAT *z);

    randomize((INTFLOAT *)dst0, 128);
    memcpy(dst1, dst0, (128) * sizeof(INTFLOAT));
    call_ref(dst0);
    call_new(dst1);
    if (!float_near_abs_eps_array(dst0, dst1, EPS, 128))
        fail();
    bench_new(dst1);
}

static void test_qmf_post_shuffle(void)
{
    LOCAL_ALIGNED_16(INTFLOAT, src, [64]);
    LOCAL_ALIGNED_16(INTFLOAT, dst0, [32], [2]);
    LOCAL_ALIGNED_16(INTFLOAT, dst1, [32], [2]);

    declare_func(void, INTFLOAT W[32][2], const INTFLOAT *z);

    randomize((INTFLOAT *)src, 64);
    call_ref(dst0, src);
    call_new(dst1, src);
    if (!float_near_abs_eps_array((INTFLOAT *)dst0, (INTFLOAT *)dst1, EPS, 64))
        fail();
    bench_new(dst1, src);
}

static void test_qmf_deint_neg(void)
{
    LOCAL_ALIGNED_16(INTFLOAT, src, [64]);
    LOCAL_ALIGNED_16(INTFLOAT, dst0, [64]);
    LOCAL_ALIGNED_16(INTFLOAT, dst1, [64]);

    declare_func(void, INTFLOAT *v, const INTFLOAT *src);

    randomize((INTFLOAT *)src, 64);
    call_ref(dst0, src);
    call_new(dst1, src);
    if (!float_near_abs_eps_array(dst0, dst1, EPS, 64))
        fail();
    bench_new(dst1, src);
}

static void test_qmf_deint_bfly(void)
{
    LOCAL_ALIGNED_16(INTFLOAT, src0, [64]);
    LOCAL_ALIGNED_16(INTFLOAT, src1, [64]);
    LOCAL_ALIGNED_16(INTFLOAT, dst0, [128]);
    LOCAL_ALIGNED_16(INTFLOAT, dst1, [128]);

    declare_func(void, INTFLOAT *v, const INTFLOAT *src0, const INTFLOAT *src1);

    memset(dst0, 0, 128 * sizeof(INTFLOAT));
    memset(dst1, 0, 128 * sizeof(INTFLOAT));

    randomize((INTFLOAT *)src0, 64);
    randomize((INTFLOAT *)src1, 64);
    call_ref(dst0, src0, src1);
    call_new(dst1, src0, src1);
    if (!float_near_abs_eps_array(dst0, dst1, EPS, 128))
        fail();
    bench_new(dst1, src0, src1);
}

static void test_autocorrelate(void)
{
    LOCAL_ALIGNED_16(INTFLOAT, src, [40], [2]);
    LOCAL_ALIGNED_16(INTFLOAT, dst0, [3], [2][2]);
    LOCAL_ALIGNED_16(INTFLOAT, dst1, [3], [2][2]);

    declare_func(void, const INTFLOAT x[40][2], INTFLOAT phi[3][2][2]);

    memset(dst0, 0, 3 * 2 * 2 * sizeof(INTFLOAT));
    memset(dst1, 0, 3 * 2 * 2 * sizeof(INTFLOAT));

    randomize((INTFLOAT *)src, 80);
    call_ref(src, dst0);
    call_new(src, dst1);
    if (!float_near_abs_eps_array((INTFLOAT *)dst0, (INTFLOAT *)dst1, EPS, 3 * 2 * 2))
        fail();
    bench_new(src, dst1);
}

static void test_hf_gen(void)
{
    LOCAL_ALIGNED_16(INTFLOAT, low, [128], [2]);
    LOCAL_ALIGNED_16(INTFLOAT, alpha0, [2]);
    LOCAL_ALIGNED_16(INTFLOAT, alpha1, [2]);
    LOCAL_ALIGNED_16(INTFLOAT, dst0, [128], [2]);
    LOCAL_ALIGNED_16(INTFLOAT, dst1, [128], [2]);
    INTFLOAT bw = (INTFLOAT)rnd() / UINT_MAX;
    int i;

    declare_func(void, INTFLOAT (*X_high)[2], const INTFLOAT (*X_low)[2],
                       const INTFLOAT alpha0[2], const INTFLOAT alpha1[2],
                       INTFLOAT bw, int start, int end);

    randomize((INTFLOAT *)low, 128 * 2);
    randomize((INTFLOAT *)alpha0, 2);
    randomize((INTFLOAT *)alpha1, 2);
    for (i = 2; i < 64; i += 2) {
        memset(dst0, 0, 128 * 2 * sizeof(INTFLOAT));
        memset(dst1, 0, 128 * 2 * sizeof(INTFLOAT));
        call_ref(dst0, low, alpha0, alpha1, bw, i, 128);
        call_new(dst1, low, alpha0, alpha1, bw, i, 128);
        if (!float_near_abs_eps_array((INTFLOAT *)dst0, (INTFLOAT *)dst1, EPS, 128 * 2))
            fail();
        bench_new(dst1, low, alpha0, alpha1, bw, i, 128);
    }
}

static void test_hf_g_filt(void)
{
    LOCAL_ALIGNED_16(INTFLOAT, high, [128], [40][2]);
    LOCAL_ALIGNED_16(INTFLOAT, g_filt, [128]);
    LOCAL_ALIGNED_16(INTFLOAT, dst0, [128], [2]);
    LOCAL_ALIGNED_16(INTFLOAT, dst1, [128], [2]);

    declare_func(void, INTFLOAT (*Y)[2], const INTFLOAT (*X_high)[40][2],
                       const INTFLOAT *g_filt, int m_max, intptr_t ixh);

    randomize((INTFLOAT *)high, 128 * 40 * 2);
    randomize((INTFLOAT *)g_filt, 128);

    call_ref(dst0, high, g_filt, 128, 20);
    call_new(dst1, high, g_filt, 128, 20);
    if (!float_near_abs_eps_array((INTFLOAT *)dst0, (INTFLOAT *)dst1, EPS, 128 * 2))
        fail();
    bench_new(dst1, high, g_filt, 128, 20);
}

static void test_hf_apply_noise(const SBRDSPContext *sbrdsp)
{
    LOCAL_ALIGNED_16(AAC_FLOAT, s_m, [128]);
    LOCAL_ALIGNED_16(AAC_FLOAT, q_filt, [128]);
    LOCAL_ALIGNED_16(INTFLOAT, ref, [128], [2]);
    LOCAL_ALIGNED_16(INTFLOAT, dst0, [128], [2]);
    LOCAL_ALIGNED_16(INTFLOAT, dst1, [128], [2]);
    int noise = 0x2a;
    int i, j;

    declare_func(void, INTFLOAT (*Y)[2], const AAC_FLOAT *s_m,
                       const AAC_FLOAT *q_filt, int noise,
                       int kx, int m_max);

    randomize((INTFLOAT *)ref, 128 * 2);

    for (int i = 0; i < 128; i++)
        s_m[i] = (rnd() & 1) ? ((INTFLOAT)rnd() / UINT_MAX) : (INTFLOAT)0;

    randomize((INTFLOAT *)q_filt, 128);

    for (i = 0; i < 4; i++) {
        if (check_func(sbrdsp->hf_apply_noise[i], "hf_apply_noise_%d", i)) {
            for (j = 0; j < 2; j++) {
                memcpy(dst0, ref, 128 * 2 * sizeof(INTFLOAT));
                memcpy(dst1, ref, 128 * 2 * sizeof(INTFLOAT));
                call_ref(dst0, s_m, q_filt, noise, j, 128);
                call_new(dst1, s_m, q_filt, noise, j, 128);
                if (!float_near_abs_eps_array((INTFLOAT *)dst0, (INTFLOAT *)dst1, EPS, 128 * 2))
                    fail();
                bench_new(dst1, s_m, q_filt, noise, j, 128);
            }
        }
    }
}

void checkasm_check_sbrdsp(void)
{
    SBRDSPContext sbrdsp;

    ff_sbrdsp_init(&sbrdsp);

    if (check_func(sbrdsp.sum64x5, "sum64x5"))
        test_sum64x5();
    report("sum64x5");

    if (check_func(sbrdsp.sum_square, "sum_square"))
        test_sum_square();
    report("sum_square");

    if (check_func(sbrdsp.neg_odd_64, "neg_odd_64"))
        test_neg_odd_64();
    report("neg_odd_64");

    if (check_func(sbrdsp.qmf_pre_shuffle, "qmf_pre_shuffle"))
        test_qmf_pre_shuffle();
    report("qmf_pre_shuffle");

    if (check_func(sbrdsp.qmf_post_shuffle, "qmf_post_shuffle"))
        test_qmf_post_shuffle();
    report("qmf_post_shuffle");

    if (check_func(sbrdsp.qmf_deint_neg, "qmf_deint_neg"))
        test_qmf_deint_neg();
    report("qmf_deint_neg");

    if (check_func(sbrdsp.qmf_deint_bfly, "qmf_deint_bfly"))
        test_qmf_deint_bfly();
    report("qmf_deint_bfly");

    if (check_func(sbrdsp.autocorrelate, "autocorrelate"))
        test_autocorrelate();
    report("autocorrelate");

    if (check_func(sbrdsp.hf_gen, "hf_gen"))
        test_hf_gen();
    report("hf_gen");

    if (check_func(sbrdsp.hf_g_filt, "hf_g_filt"))
        test_hf_g_filt();
    report("hf_g_filt");

    test_hf_apply_noise(&sbrdsp);
    report("hf_apply_noise");
}
