/*
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

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#if HAVE_UNISTD_H
#include <unistd.h> /* for getopt */
#endif
#if !HAVE_GETOPT
#include "compat/getopt.c"
#endif

#include "libavutil/common.h"
#include "libavutil/cpu.h"
#include "libavutil/internal.h"
#include "libavutil/lfg.h"
#include "libavutil/log.h"
#include "libavutil/random_seed.h"
#include "libavutil/float_dsp.h"

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
        av_log(NULL, AV_LOG_ERROR, "vector_fmul failed\n");

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
        av_log(NULL, AV_LOG_ERROR, "vector_fmac_scalar failed\n");

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
        av_log(NULL, AV_LOG_ERROR, "vector_fmul_scalar failed\n");

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
        av_log(NULL, AV_LOG_ERROR, "vector_dmul_scalar failed\n");

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
        av_log(NULL, AV_LOG_ERROR, "vector_fmul_window failed\n");

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
        av_log(NULL, AV_LOG_ERROR, "vector_fmul_add failed\n");

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
        av_log(NULL, AV_LOG_ERROR, "vector_fmul_reverse failed\n");

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
        av_log(NULL, AV_LOG_ERROR, "butterflies_float failed\n");

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
        av_log(NULL, AV_LOG_ERROR, "scalarproduct_float failed\n");

    return ret;
}

int main(int argc, char **argv)
{
    int ret = 0, seeded = 0;
    uint32_t seed;
    AVFloatDSPContext *fdsp, *cdsp;
    AVLFG lfg;

    LOCAL_ALIGNED(32, float, src0, [LEN]);
    LOCAL_ALIGNED(32, float, src1, [LEN]);
    LOCAL_ALIGNED(32, float, src2, [LEN]);
    LOCAL_ALIGNED(32, double, dbl_src0, [LEN]);
    LOCAL_ALIGNED(32, double, dbl_src1, [LEN]);

    for (;;) {
        int arg = getopt(argc, argv, "s:c:");
        if (arg == -1)
            break;
        switch (arg) {
        case 's':
            seed = strtoul(optarg, NULL, 10);
            seeded = 1;
            break;
        case 'c':
        {
            int cpuflags = av_get_cpu_flags();

            if (av_parse_cpu_caps(&cpuflags, optarg) < 0)
                return 1;

            av_force_cpu_flags(cpuflags);
            break;
        }
        }
    }
    if (!seeded)
        seed = av_get_random_seed();

    av_log(NULL, AV_LOG_INFO, "float_dsp-test: %s %u\n", seeded ? "seed" : "random seed", seed);

    fdsp = avpriv_float_dsp_alloc(1);
    av_force_cpu_flags(0);
    cdsp = avpriv_float_dsp_alloc(1);

    if (!fdsp || !cdsp) {
        ret = 1;
        goto end;
    }

    av_lfg_init(&lfg, seed);

    fill_float_array(&lfg, src0, LEN);
    fill_float_array(&lfg, src1, LEN);
    fill_float_array(&lfg, src2, LEN);

    fill_double_array(&lfg, dbl_src0, LEN);
    fill_double_array(&lfg, dbl_src1, LEN);

    if (test_vector_fmul(fdsp, cdsp, src0, src1))
        ret -= 1 << 0;
    if (test_vector_fmac_scalar(fdsp, cdsp, src2, src0, src1[0]))
        ret -= 1 << 1;
    if (test_vector_fmul_scalar(fdsp, cdsp, src0, src1[0]))
        ret -= 1 << 2;
    if (test_vector_fmul_window(fdsp, cdsp, src0, src1, src2))
        ret -= 1 << 3;
    if (test_vector_fmul_add(fdsp, cdsp, src0, src1, src2))
        ret -= 1 << 4;
    if (test_vector_fmul_reverse(fdsp, cdsp, src0, src1))
        ret -= 1 << 5;
    if (test_butterflies_float(fdsp, cdsp, src0, src1))
        ret -= 1 << 6;
    if (test_scalarproduct_float(fdsp, cdsp, src0, src1))
        ret -= 1 << 7;
    if (test_vector_dmul_scalar(fdsp, cdsp, dbl_src0, dbl_src1[0]))
        ret -= 1 << 8;

end:
    av_freep(&fdsp);
    av_freep(&cdsp);
    return ret;
}
