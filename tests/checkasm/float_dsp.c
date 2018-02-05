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

#include "config.h"

#include <float.h>
#include <stdint.h>

#include "libavutil/float_dsp.h"
#include "libavutil/internal.h"
#include "checkasm.h"

#define LEN 256

#define randomize_buffer(buf)                 \
do {                                          \
    int i;                                    \
    double bmg[2], stddev = 10.0, mean = 0.0; \
                                              \
    for (i = 0; i < LEN; i += 2) {            \
        av_bmg_get(&checkasm_lfg, bmg);       \
        buf[i]     = bmg[0] * stddev + mean;  \
        buf[i + 1] = bmg[1] * stddev + mean;  \
    }                                         \
} while(0);

static void test_vector_fmul(const float *src0, const float *src1)
{
    LOCAL_ALIGNED_32(float, cdst, [LEN]);
    LOCAL_ALIGNED_32(float, odst, [LEN]);
    int i;

    declare_func(void, float *dst, const float *src0, const float *src1,
                 int len);

    call_ref(cdst, src0, src1, LEN);
    call_new(odst, src0, src1, LEN);
    for (i = 0; i < LEN; i++) {
        if (!float_near_abs_eps(cdst[i], odst[i], FLT_EPSILON)) {
            fprintf(stderr, "%d: %- .12f - %- .12f = % .12g\n",
                    i, cdst[i], odst[i], cdst[i] - odst[i]);
            fail();
            break;
        }
    }
    bench_new(odst, src0, src1, LEN);
}

#define ARBITRARY_FMUL_ADD_CONST 0.005
static void test_vector_fmul_add(const float *src0, const float *src1, const float *src2)
{
    LOCAL_ALIGNED_32(float, cdst, [LEN]);
    LOCAL_ALIGNED_32(float, odst, [LEN]);
    int i;

    declare_func(void, float *dst, const float *src0, const float *src1,
                     const float *src2, int len);

    call_ref(cdst, src0, src1, src2, LEN);
    call_new(odst, src0, src1, src2, LEN);
    for (i = 0; i < LEN; i++) {
        if (!float_near_abs_eps(cdst[i], odst[i], ARBITRARY_FMUL_ADD_CONST)) {
            fprintf(stderr, "%d: %- .12f - %- .12f = % .12g\n",
                    i, cdst[i], odst[i], cdst[i] - odst[i]);
            fail();
            break;
        }
    }
    bench_new(odst, src0, src1, src2, LEN);
}

static void test_vector_fmul_scalar(const float *src0, const float *src1)
{
    LOCAL_ALIGNED_16(float, cdst, [LEN]);
    LOCAL_ALIGNED_16(float, odst, [LEN]);
    int i;

    declare_func(void, float *dst, const float *src, float mul, int len);

    call_ref(cdst, src0, src1[0], LEN);
    call_new(odst, src0, src1[0], LEN);
        for (i = 0; i < LEN; i++) {
            if (!float_near_abs_eps(cdst[i], odst[i], FLT_EPSILON)) {
                fprintf(stderr, "%d: %- .12f - %- .12f = % .12g\n",
                        i, cdst[i], odst[i], cdst[i] - odst[i]);
                fail();
                break;
            }
        }
    bench_new(odst, src0, src1[0], LEN);
}

#define ARBITRARY_FMUL_WINDOW_CONST 0.008
static void test_vector_fmul_window(const float *src0, const float *src1, const float *win)
{
    LOCAL_ALIGNED_16(float, cdst, [LEN]);
    LOCAL_ALIGNED_16(float, odst, [LEN]);
    int i;

    declare_func(void, float *dst, const float *src0, const float *src1,
                 const float *win, int len);

    call_ref(cdst, src0, src1, win, LEN / 2);
    call_new(odst, src0, src1, win, LEN / 2);
    for (i = 0; i < LEN; i++) {
        if (!float_near_abs_eps(cdst[i], odst[i], ARBITRARY_FMUL_WINDOW_CONST)) {
            fprintf(stderr, "%d: %- .12f - %- .12f = % .12g\n",
                    i, cdst[i], odst[i], cdst[i] - odst[i]);
            fail();
            break;
        }
    }
    bench_new(odst, src0, src1, win, LEN / 2);
}

#define ARBITRARY_FMAC_SCALAR_CONST 0.005
static void test_vector_fmac_scalar(const float *src0, const float *src1, const float *src2)
{
    LOCAL_ALIGNED_32(float, cdst, [LEN]);
    LOCAL_ALIGNED_32(float, odst, [LEN]);
    int i;

    declare_func(void, float *dst, const float *src, float mul, int len);

    memcpy(cdst, src2, LEN * sizeof(*src2));
    memcpy(odst, src2, LEN * sizeof(*src2));

    call_ref(cdst, src0, src1[0], LEN);
    call_new(odst, src0, src1[0], LEN);
    for (i = 0; i < LEN; i++) {
        if (!float_near_abs_eps(cdst[i], odst[i], ARBITRARY_FMAC_SCALAR_CONST)) {
            fprintf(stderr, "%d: %- .12f - %- .12f = % .12g\n",
                    i, cdst[i], odst[i], cdst[i] - odst[i]);
            fail();
            break;
        }
    }
    memcpy(odst, src2, LEN * sizeof(*src2));
    bench_new(odst, src0, src1[0], LEN);
}

static void test_vector_dmul_scalar(const double *src0, const double *src1)
{
    LOCAL_ALIGNED_32(double, cdst, [LEN]);
    LOCAL_ALIGNED_32(double, odst, [LEN]);
    int i;

    declare_func(void, double *dst, const double *src, double mul, int len);

    call_ref(cdst, src0, src1[0], LEN);
    call_new(odst, src0, src1[0], LEN);
    for (i = 0; i < LEN; i++) {
        double t = fabs(src1[0]) + fabs(src0[i]) + fabs(src1[0] * src0[i]) + 1.0;
        if (!double_near_abs_eps(cdst[i], odst[i], t * 2 * DBL_EPSILON)) {
            fprintf(stderr, "%d: %- .12f - %- .12f = % .12g\n", i,
                    cdst[i], odst[i], cdst[i] - odst[i]);
            fail();
            break;
        }
    }
    bench_new(odst, src0, src1[0], LEN);
}

#define ARBITRARY_DMAC_SCALAR_CONST 0.005
static void test_vector_dmac_scalar(const double *src0, const double *src1, const double *src2)
{
    LOCAL_ALIGNED_32(double, cdst, [LEN]);
    LOCAL_ALIGNED_32(double, odst, [LEN]);
    int i;

    declare_func(void, double *dst, const double *src, double mul, int len);

    memcpy(cdst, src2, LEN * sizeof(*src2));
    memcpy(odst, src2, LEN * sizeof(*src2));
    call_ref(cdst, src0, src1[0], LEN);
    call_new(odst, src0, src1[0], LEN);
    for (i = 0; i < LEN; i++) {
        if (!double_near_abs_eps(cdst[i], odst[i], ARBITRARY_DMAC_SCALAR_CONST)) {
            fprintf(stderr, "%d: %- .12f - %- .12f = % .12g\n",
                    i, cdst[i], odst[i], cdst[i] - odst[i]);
            fail();
            break;
        }
    }
    memcpy(odst, src2, LEN * sizeof(*src2));
    bench_new(odst, src0, src1[0], LEN);
}

static void test_butterflies_float(const float *src0, const float *src1)
{
    LOCAL_ALIGNED_16(float,  cdst,  [LEN]);
    LOCAL_ALIGNED_16(float,  odst,  [LEN]);
    LOCAL_ALIGNED_16(float,  cdst1, [LEN]);
    LOCAL_ALIGNED_16(float,  odst1, [LEN]);
    int i;

    declare_func(void, float *av_restrict src0, float *av_restrict src1,
    int len);

    memcpy(cdst,  src0, LEN * sizeof(*src0));
    memcpy(cdst1, src1, LEN * sizeof(*src1));
    memcpy(odst,  src0, LEN * sizeof(*src0));
    memcpy(odst1, src1, LEN * sizeof(*src1));

    call_ref(cdst, cdst1, LEN);
    call_new(odst, odst1, LEN);
    for (i = 0; i < LEN; i++) {
        if (!float_near_abs_eps(cdst[i],  odst[i],  FLT_EPSILON) ||
            !float_near_abs_eps(cdst1[i], odst1[i], FLT_EPSILON)) {
            fprintf(stderr, "%d: %- .12f - %- .12f = % .12g\n",
                    i, cdst[i], odst[i], cdst[i] - odst[i]);
            fprintf(stderr, "%d: %- .12f - %- .12f = % .12g\n",
                    i, cdst1[i], odst1[i], cdst1[i] - odst1[i]);
            fail();
            break;
        }
    }
    memcpy(odst,  src0, LEN * sizeof(*src0));
    memcpy(odst1, src1, LEN * sizeof(*src1));
    bench_new(odst, odst1, LEN);
}

#define ARBITRARY_SCALARPRODUCT_CONST 0.2
static void test_scalarproduct_float(const float *src0, const float *src1)
{
    float cprod, oprod;

    declare_func_float(float, const float *src0, const float *src1, int len);

    cprod = call_ref(src0, src1, LEN);
    oprod = call_new(src0, src1, LEN);
    if (!float_near_abs_eps(cprod, oprod, ARBITRARY_SCALARPRODUCT_CONST)) {
        fprintf(stderr, "%- .12f - %- .12f = % .12g\n",
                cprod, oprod, cprod - oprod);
        fail();
    }
    bench_new(src0, src1, LEN);
}

void checkasm_check_float_dsp(void)
{
    LOCAL_ALIGNED_32(float,  src0,     [LEN]);
    LOCAL_ALIGNED_32(float,  src1,     [LEN]);
    LOCAL_ALIGNED_32(float,  src2,     [LEN]);
    LOCAL_ALIGNED_16(float,  src3,     [LEN]);
    LOCAL_ALIGNED_16(float,  src4,     [LEN]);
    LOCAL_ALIGNED_16(float,  src5,     [LEN]);
    LOCAL_ALIGNED_32(double, dbl_src0, [LEN]);
    LOCAL_ALIGNED_32(double, dbl_src1, [LEN]);
    LOCAL_ALIGNED_32(double, dbl_src2, [LEN]);
    AVFloatDSPContext *fdsp = avpriv_float_dsp_alloc(1);

    if (!fdsp) {
        fprintf(stderr, "floatdsp: Out of memory error\n");
        return;
    }

    randomize_buffer(src0);
    randomize_buffer(src1);
    randomize_buffer(src2);
    randomize_buffer(src3);
    randomize_buffer(src4);
    randomize_buffer(src5);
    randomize_buffer(dbl_src0);
    randomize_buffer(dbl_src1);
    randomize_buffer(dbl_src2);

    if (check_func(fdsp->vector_fmul, "vector_fmul"))
        test_vector_fmul(src0, src1);
    if (check_func(fdsp->vector_fmul_add, "vector_fmul_add"))
        test_vector_fmul_add(src0, src1, src2);
    if (check_func(fdsp->vector_fmul_scalar, "vector_fmul_scalar"))
        test_vector_fmul_scalar(src3, src4);
    if (check_func(fdsp->vector_fmul_reverse, "vector_fmul_reverse"))
        test_vector_fmul(src0, src1);
    if (check_func(fdsp->vector_fmul_window, "vector_fmul_window"))
        test_vector_fmul_window(src3, src4, src5);
    report("vector_fmul");
    if (check_func(fdsp->vector_fmac_scalar, "vector_fmac_scalar"))
        test_vector_fmac_scalar(src0, src1, src2);
    report("vector_fmac");
    if (check_func(fdsp->vector_dmul_scalar, "vector_dmul_scalar"))
        test_vector_dmul_scalar(dbl_src0, dbl_src1);
    report("vector_dmul");
    if (check_func(fdsp->vector_dmac_scalar, "vector_dmac_scalar"))
        test_vector_dmac_scalar(dbl_src0, dbl_src1, dbl_src2);
    report("vector_dmac");
    if (check_func(fdsp->butterflies_float, "butterflies_float"))
        test_butterflies_float(src3, src4);
    report("butterflies_float");
    if (check_func(fdsp->scalarproduct_float, "scalarproduct_float"))
        test_scalarproduct_float(src3, src4);
    report("scalarproduct_float");

    av_freep(&fdsp);
}
