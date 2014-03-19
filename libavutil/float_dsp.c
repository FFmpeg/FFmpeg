/*
 * Copyright 2005 Balatoni Denes
 * Copyright 2006 Loren Merritt
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "config.h"
#include "attributes.h"
#include "float_dsp.h"

static void vector_fmul_c(float *dst, const float *src0, const float *src1,
                          int len)
{
    int i;
    for (i = 0; i < len; i++)
        dst[i] = src0[i] * src1[i];
}

static void vector_fmac_scalar_c(float *dst, const float *src, float mul,
                                 int len)
{
    int i;
    for (i = 0; i < len; i++)
        dst[i] += src[i] * mul;
}

static void vector_fmul_scalar_c(float *dst, const float *src, float mul,
                                 int len)
{
    int i;
    for (i = 0; i < len; i++)
        dst[i] = src[i] * mul;
}

static void vector_dmul_scalar_c(double *dst, const double *src, double mul,
                                 int len)
{
    int i;
    for (i = 0; i < len; i++)
        dst[i] = src[i] * mul;
}

static void vector_fmul_window_c(float *dst, const float *src0,
                                 const float *src1, const float *win, int len)
{
    int i, j;

    dst  += len;
    win  += len;
    src0 += len;

    for (i = -len, j = len - 1; i < 0; i++, j--) {
        float s0 = src0[i];
        float s1 = src1[j];
        float wi = win[i];
        float wj = win[j];
        dst[i] = s0 * wj - s1 * wi;
        dst[j] = s0 * wi + s1 * wj;
    }
}

static void vector_fmul_add_c(float *dst, const float *src0, const float *src1,
                              const float *src2, int len){
    int i;

    for (i = 0; i < len; i++)
        dst[i] = src0[i] * src1[i] + src2[i];
}

static void vector_fmul_reverse_c(float *dst, const float *src0,
                                  const float *src1, int len)
{
    int i;

    src1 += len-1;
    for (i = 0; i < len; i++)
        dst[i] = src0[i] * src1[-i];
}

static void butterflies_float_c(float *av_restrict v1, float *av_restrict v2,
                                int len)
{
    int i;

    for (i = 0; i < len; i++) {
        float t = v1[i] - v2[i];
        v1[i] += v2[i];
        v2[i] = t;
    }
}

float avpriv_scalarproduct_float_c(const float *v1, const float *v2, int len)
{
    float p = 0.0;
    int i;

    for (i = 0; i < len; i++)
        p += v1[i] * v2[i];

    return p;
}

av_cold void avpriv_float_dsp_init(AVFloatDSPContext *fdsp, int bit_exact)
{
    fdsp->vector_fmul = vector_fmul_c;
    fdsp->vector_fmac_scalar = vector_fmac_scalar_c;
    fdsp->vector_fmul_scalar = vector_fmul_scalar_c;
    fdsp->vector_dmul_scalar = vector_dmul_scalar_c;
    fdsp->vector_fmul_window = vector_fmul_window_c;
    fdsp->vector_fmul_add = vector_fmul_add_c;
    fdsp->vector_fmul_reverse = vector_fmul_reverse_c;
    fdsp->butterflies_float = butterflies_float_c;
    fdsp->scalarproduct_float = avpriv_scalarproduct_float_c;

#if   ARCH_AARCH64
    ff_float_dsp_init_aarch64(fdsp);
#elif ARCH_ARM
    ff_float_dsp_init_arm(fdsp);
#elif ARCH_PPC
    ff_float_dsp_init_ppc(fdsp, bit_exact);
#elif ARCH_X86
    ff_float_dsp_init_x86(fdsp);
#elif ARCH_MIPS
    ff_float_dsp_init_mips(fdsp);
#endif
}

#ifdef TEST

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "cpu.h"
#include "internal.h"
#include "lfg.h"
#include "log.h"
#include "mem.h"
#include "random_seed.h"

#define LEN 240

static void fill_float_array(AVLFG *lfg, float *a, int len)
{
    int i;
    double bmg[2], stddev = 10.0, mean = 0.0;

    for (i = 0; i < len; i += 2) {
        av_bmg_get(lfg, bmg);
        a[i]     = bmg[0] * stddev + mean;
        a[i + 1] = bmg[1] * stddev + mean;
    }
}
static int compare_floats(const float *a, const float *b, int len,
                          float max_diff)
{
    int i;
    for (i = 0; i < len; i++) {
        if (fabsf(a[i] - b[i]) > max_diff) {
            av_log(NULL, AV_LOG_ERROR, "%d: %- .12f - %- .12f = % .12g\n",
                   i, a[i], b[i], a[i] - b[i]);
            return -1;
        }
    }
    return 0;
}

static void fill_double_array(AVLFG *lfg, double *a, int len)
{
    int i;
    double bmg[2], stddev = 10.0, mean = 0.0;

    for (i = 0; i < len; i += 2) {
        av_bmg_get(lfg, bmg);
        a[i]     = bmg[0] * stddev + mean;
        a[i + 1] = bmg[1] * stddev + mean;
    }
}

static int compare_doubles(const double *a, const double *b, int len,
                           double max_diff)
{
    int i;

    for (i = 0; i < len; i++) {
        if (fabs(a[i] - b[i]) > max_diff) {
            av_log(NULL, AV_LOG_ERROR, "%d: %- .12f - %- .12f = % .12g\n",
                   i, a[i], b[i], a[i] - b[i]);
            return -1;
        }
    }
    return 0;
}

static int test_vector_fmul(AVFloatDSPContext *fdsp, AVFloatDSPContext *cdsp,
                            const float *v1, const float *v2)
{
    LOCAL_ALIGNED(32, float, cdst, [LEN]);
    LOCAL_ALIGNED(32, float, odst, [LEN]);
    int ret;

    cdsp->vector_fmul(cdst, v1, v2, LEN);
    fdsp->vector_fmul(odst, v1, v2, LEN);

    if (ret = compare_floats(cdst, odst, LEN, FLT_EPSILON))
        av_log(NULL, AV_LOG_ERROR, "%s failed\n", __func__);

    return ret;
}

#define ARBITRARY_FMAC_SCALAR_CONST 0.005
static int test_vector_fmac_scalar(AVFloatDSPContext *fdsp, AVFloatDSPContext *cdsp,
                                   const float *v1, const float *src0, float scale)
{
    LOCAL_ALIGNED(32, float, cdst, [LEN]);
    LOCAL_ALIGNED(32, float, odst, [LEN]);
    int ret;

    memcpy(cdst, v1, LEN * sizeof(*v1));
    memcpy(odst, v1, LEN * sizeof(*v1));

    cdsp->vector_fmac_scalar(cdst, src0, scale, LEN);
    fdsp->vector_fmac_scalar(odst, src0, scale, LEN);

    if (ret = compare_floats(cdst, odst, LEN, ARBITRARY_FMAC_SCALAR_CONST))
        av_log(NULL, AV_LOG_ERROR, "%s failed\n", __func__);

    return ret;
}

static int test_vector_fmul_scalar(AVFloatDSPContext *fdsp, AVFloatDSPContext *cdsp,
                                   const float *v1, float scale)
{
    LOCAL_ALIGNED(32, float, cdst, [LEN]);
    LOCAL_ALIGNED(32, float, odst, [LEN]);
    int ret;

    cdsp->vector_fmul_scalar(cdst, v1, scale, LEN);
    fdsp->vector_fmul_scalar(odst, v1, scale, LEN);

    if (ret = compare_floats(cdst, odst, LEN, FLT_EPSILON))
        av_log(NULL, AV_LOG_ERROR, "%s failed\n", __func__);

    return ret;
}

static int test_vector_dmul_scalar(AVFloatDSPContext *fdsp, AVFloatDSPContext *cdsp,
                                   const double *v1, double scale)
{
    LOCAL_ALIGNED(32, double, cdst, [LEN]);
    LOCAL_ALIGNED(32, double, odst, [LEN]);
    int ret;

    cdsp->vector_dmul_scalar(cdst, v1, scale, LEN);
    fdsp->vector_dmul_scalar(odst, v1, scale, LEN);

    if (ret = compare_doubles(cdst, odst, LEN, DBL_EPSILON))
        av_log(NULL, AV_LOG_ERROR, "%s failed\n", __func__);

    return ret;
}

#define ARBITRARY_FMUL_WINDOW_CONST 0.008
static int test_vector_fmul_window(AVFloatDSPContext *fdsp, AVFloatDSPContext *cdsp,
                                   const float *v1, const float *v2, const float *v3)
{
    LOCAL_ALIGNED(32, float, cdst, [LEN]);
    LOCAL_ALIGNED(32, float, odst, [LEN]);
    int ret;

    cdsp->vector_fmul_window(cdst, v1, v2, v3, LEN / 2);
    fdsp->vector_fmul_window(odst, v1, v2, v3, LEN / 2);

    if (ret = compare_floats(cdst, odst, LEN, ARBITRARY_FMUL_WINDOW_CONST))
        av_log(NULL, AV_LOG_ERROR, "%s failed\n", __func__);

    return ret;
}

#define ARBITRARY_FMUL_ADD_CONST 0.005
static int test_vector_fmul_add(AVFloatDSPContext *fdsp, AVFloatDSPContext *cdsp,
                                const float *v1, const float *v2, const float *v3)
{
    LOCAL_ALIGNED(32, float, cdst, [LEN]);
    LOCAL_ALIGNED(32, float, odst, [LEN]);
    int ret;

    cdsp->vector_fmul_add(cdst, v1, v2, v3, LEN);
    fdsp->vector_fmul_add(odst, v1, v2, v3, LEN);

    if (ret = compare_floats(cdst, odst, LEN, ARBITRARY_FMUL_ADD_CONST))
        av_log(NULL, AV_LOG_ERROR, "%s failed\n", __func__);

    return ret;
}

static int test_vector_fmul_reverse(AVFloatDSPContext *fdsp, AVFloatDSPContext *cdsp,
                                    const float *v1, const float *v2)
{
    LOCAL_ALIGNED(32, float, cdst, [LEN]);
    LOCAL_ALIGNED(32, float, odst, [LEN]);
    int ret;

    cdsp->vector_fmul_reverse(cdst, v1, v2, LEN);
    fdsp->vector_fmul_reverse(odst, v1, v2, LEN);

    if (ret = compare_floats(cdst, odst, LEN, FLT_EPSILON))
        av_log(NULL, AV_LOG_ERROR, "%s failed\n", __func__);

    return ret;
}

static int test_butterflies_float(AVFloatDSPContext *fdsp, AVFloatDSPContext *cdsp,
                                  const float *v1, const float *v2)
{
    LOCAL_ALIGNED(32, float, cv1, [LEN]);
    LOCAL_ALIGNED(32, float, cv2, [LEN]);
    LOCAL_ALIGNED(32, float, ov1, [LEN]);
    LOCAL_ALIGNED(32, float, ov2, [LEN]);
    int ret;

    memcpy(cv1, v1, LEN * sizeof(*v1));
    memcpy(cv2, v2, LEN * sizeof(*v2));
    memcpy(ov1, v1, LEN * sizeof(*v1));
    memcpy(ov2, v2, LEN * sizeof(*v2));

    cdsp->butterflies_float(cv1, cv2, LEN);
    fdsp->butterflies_float(ov1, ov2, LEN);

    if ((ret = compare_floats(cv1, ov1, LEN, FLT_EPSILON)) ||
        (ret = compare_floats(cv2, ov2, LEN, FLT_EPSILON)))
        av_log(NULL, AV_LOG_ERROR, "%s failed\n", __func__);

    return ret;
}

#define ARBITRARY_SCALARPRODUCT_CONST 0.2
static int test_scalarproduct_float(AVFloatDSPContext *fdsp, AVFloatDSPContext *cdsp,
                                    const float *v1, const float *v2)
{
    float cprod, oprod;
    int ret;

    cprod = cdsp->scalarproduct_float(v1, v2, LEN);
    oprod = fdsp->scalarproduct_float(v1, v2, LEN);

    if (ret = compare_floats(&cprod, &oprod, 1, ARBITRARY_SCALARPRODUCT_CONST))
        av_log(NULL, AV_LOG_ERROR, "%s failed\n", __func__);

    return ret;
}

int main(int argc, char **argv)
{
    int ret = 0;
    uint32_t seed;
    AVFloatDSPContext fdsp, cdsp;
    AVLFG lfg;

    LOCAL_ALIGNED(32, float, src0, [LEN]);
    LOCAL_ALIGNED(32, float, src1, [LEN]);
    LOCAL_ALIGNED(32, float, src2, [LEN]);
    LOCAL_ALIGNED(32, double, dbl_src0, [LEN]);
    LOCAL_ALIGNED(32, double, dbl_src1, [LEN]);

    if (argc > 2 && !strcmp(argv[1], "-s"))
        seed = strtoul(argv[2], NULL, 10);
    else
        seed = av_get_random_seed();

    av_log(NULL, AV_LOG_INFO, "float_dsp-test: random seed %u\n", seed);

    av_lfg_init(&lfg, seed);

    fill_float_array(&lfg, src0, LEN);
    fill_float_array(&lfg, src1, LEN);
    fill_float_array(&lfg, src2, LEN);

    fill_double_array(&lfg, dbl_src0, LEN);
    fill_double_array(&lfg, dbl_src1, LEN);

    avpriv_float_dsp_init(&fdsp, 1);
    av_set_cpu_flags_mask(0);
    avpriv_float_dsp_init(&cdsp, 1);

    if (test_vector_fmul(&fdsp, &cdsp, src0, src1))
        ret -= 1 << 0;
    if (test_vector_fmac_scalar(&fdsp, &cdsp, src2, src0, src1[0]))
        ret -= 1 << 1;
    if (test_vector_fmul_scalar(&fdsp, &cdsp, src0, src1[0]))
        ret -= 1 << 2;
    if (test_vector_fmul_window(&fdsp, &cdsp, src0, src1, src2))
        ret -= 1 << 3;
    if (test_vector_fmul_add(&fdsp, &cdsp, src0, src1, src2))
        ret -= 1 << 4;
    if (test_vector_fmul_reverse(&fdsp, &cdsp, src0, src1))
        ret -= 1 << 5;
    if (test_butterflies_float(&fdsp, &cdsp, src0, src1))
        ret -= 1 << 6;
    if (test_scalarproduct_float(&fdsp, &cdsp, src0, src1))
        ret -= 1 << 7;
    if (test_vector_dmul_scalar(&fdsp, &cdsp, dbl_src0, dbl_src1[0]))
        ret -= 1 << 8;

    return ret;
}

#endif /* TEST */
