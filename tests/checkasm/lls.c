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

#include <float.h>
#include "libavutil/lls.h"
#include "checkasm.h"

#define randomize_buffer(buf)                  \
do {                                           \
    double bmg[2], stddev = 10.0;              \
                                               \
    for (size_t i = 0; i < MAX_VARS_ALIGN; i += 2) { \
        av_bmg_get(&checkasm_lfg, bmg);        \
        buf[i]     = bmg[0] * stddev;          \
        buf[i + 1] = bmg[1] * stddev;          \
    }                                          \
} while(0);

static void test_update(LLSModel *lls, const double *var)
{
    double refcovar[MAX_VARS][MAX_VARS];
    declare_func(void, LLSModel *, const double *);

    call_ref(lls, var);

    for (size_t i = 0; i < MAX_VARS; i++)
        for (size_t j = 0; j < MAX_VARS; j++)
            refcovar[i][j] = lls->covariance[i][j];

    memset(lls->covariance, 0, sizeof (lls->covariance));
    call_new(lls, var);

    for (size_t i = 0; i < lls->indep_count; i++)
        for (size_t j = i; j < lls->indep_count; j++)
            if (!double_near_abs_eps(refcovar[i][j], lls->covariance[i][j],
                                     8 * DBL_EPSILON)) {
                fprintf(stderr, "%zu, %zu: %- .12f - %- .12f = % .12g\n", i, j,
                        refcovar[i][j], lls->covariance[i][j],
                        refcovar[i][j] - lls->covariance[i][j]);
                fail();
            }

    bench_new(lls, var);
}

#define EPS 0.2
static void test_evaluate(LLSModel *lls, const double *param, int order)
{
    double refprod, newprod;
    declare_func_float(double, LLSModel *, const double *, int);

    refprod = call_ref(lls, param, order);
    newprod = call_new(lls, param, order);

    if (!double_near_abs_eps(refprod, newprod, EPS)) {
        fprintf(stderr, "%- .12f - %- .12f = % .12g\n",
                refprod, newprod, refprod - newprod);
        fail();
    }

    if (order == lls->indep_count)
        bench_new(lls, param, order);
}

void checkasm_check_lls(void)
{
    static const unsigned char counts[] = { 8, 12, MAX_VARS, };

    for (size_t i = 0; i < FF_ARRAY_ELEMS(counts); i++) {
        LOCAL_ALIGNED_32(double, var, [MAX_VARS_ALIGN]);
        LOCAL_ALIGNED_32(double, param, [FFALIGN(MAX_VARS+2,4)]);
        LLSModel lls;

        avpriv_init_lls(&lls, counts[i]);
        randomize_buffer(var);
        randomize_buffer(param);

        if (check_func(lls.update_lls, "update_lls_%d", counts[i]))
            test_update(&lls, var);
        for (size_t j = 0; j <= i; j++)
            if (check_func(lls.evaluate_lls, "evaluate_lls_%d_%d", counts[i],
                           counts[j]))
                test_evaluate(&lls, param + 1, counts[j]);
    }
    report("lls");
}
