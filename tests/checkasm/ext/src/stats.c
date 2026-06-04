/*
 * Copyright © 2025, Niklas Haas
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

#include <math.h>
#include <stdlib.h>

#include "stats.h"

CheckasmVar checkasm_var_scale(CheckasmVar a, double s)
{
    /* = checkasm_var_mul(a, checkasm_var_const(b)) */
    return (CheckasmVar) {
        .lmean = a.lmean + log(s),
        .lvar  = a.lvar,
    };
}

CheckasmVar checkasm_var_pow(CheckasmVar a, double exp)
{
    return (CheckasmVar) {
        .lmean = a.lmean * exp,
        .lvar  = a.lvar * exp * exp,
    };
}

CheckasmVar checkasm_var_add(const CheckasmVar a, const CheckasmVar b)
{
    /* Approximation assuming independent log-normal distributions */
    const double ma = exp(a.lmean + 0.5 * a.lvar);
    const double mb = exp(b.lmean + 0.5 * b.lvar);
    const double va = (exp(a.lvar) - 1.0) * exp(2.0 * a.lmean + a.lvar);
    const double vb = (exp(b.lvar) - 1.0) * exp(2.0 * b.lmean + b.lvar);
    const double m  = ma + mb;
    const double v  = va + vb;
    return (CheckasmVar) {
        .lmean = log(m * m / sqrt(v + m * m)),
        .lvar  = log(1.0 + v / (m * m)),
    };
}

CheckasmVar checkasm_var_sub(CheckasmVar a, CheckasmVar b)
{
    const double ma = exp(a.lmean + 0.5 * a.lvar);
    const double mb = exp(b.lmean + 0.5 * b.lvar);
    const double va = (exp(a.lvar) - 1.0) * exp(2.0 * a.lmean + a.lvar);
    const double vb = (exp(b.lvar) - 1.0) * exp(2.0 * b.lmean + b.lvar);
    const double m  = fmax(ma - mb, 1e-30); /* avoid negative mean */
    const double v  = va + vb;
    return (CheckasmVar) {
        .lmean = log(m * m / sqrt(v + m * m)),
        .lvar  = log(1.0 + v / (m * m)),
    };
}

CheckasmVar checkasm_var_mul(CheckasmVar a, CheckasmVar b)
{
    return (CheckasmVar) {
        .lmean = a.lmean + b.lmean,
        .lvar  = a.lvar + b.lvar,
    };
}

CheckasmVar checkasm_var_inv(CheckasmVar a)
{
    return (CheckasmVar) {
        .lmean = -a.lmean,
        .lvar  = a.lvar,
    };
}

CheckasmVar checkasm_var_div(CheckasmVar a, CheckasmVar b)
{
    return (CheckasmVar) {
        .lmean = a.lmean - b.lmean,
        .lvar  = a.lvar + b.lvar,
    };
}

CheckasmVar checkasm_stats_estimate(const CheckasmStats *const stats)
{
    if (!stats->nb_samples)
        return checkasm_var_const(0.0);

    /* Compute mean and variance */
    double sum = 0.0, sum2 = 0.0, sum_w2 = 0.0;
    int    count = 0;
    for (int i = 0; i < stats->nb_samples; i++) {
        const CheckasmSample s = stats->samples[i];
        const double         x = log((double) s.sum) - log((double) s.count);
        sum += x * s.count;
        sum2 += x * x * s.count;
        sum_w2 += (double) s.count * s.count;
        count += s.count;
    }

    assert(count > 0);
    const double mean = sum / count;
    const double denom = count - sum_w2 / count;
    double var;
    if (denom > 0.0) {
        var = fmax(sum2 - count * mean * mean, 0.0) / denom;
    } else {
        /* Lower bound on the variance predicted by the sample count alone */
        var = 1.0 / count;
    }

    return (CheckasmVar) { .lmean = mean, .lvar  = var };
}
