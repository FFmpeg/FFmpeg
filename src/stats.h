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

#ifndef CHECKASM_STATS_H
#define CHECKASM_STATS_H

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

typedef struct CheckasmVar {
    double lmean, lvar; /* log mean and variance */
} CheckasmVar;

/* Sample the PDF of a random variable at the given quantile */
static inline double checkasm_sample(const CheckasmVar x, const double q)
{
    return exp(x.lmean + q * sqrt(x.lvar));
}

static inline double checkasm_median(const CheckasmVar x)
{
    return exp(x.lmean);
}

static inline double checkasm_mode(const CheckasmVar x)
{
    return exp(x.lmean - x.lvar);
}

static inline double checkasm_mean(const CheckasmVar x)
{
    return exp(x.lmean + 0.5 * x.lvar);
}

static inline double checkasm_stddev(const CheckasmVar x)
{
    return exp(x.lmean + 0.5 * x.lvar) * sqrt(exp(x.lvar) - 1.0);
}

static inline CheckasmVar checkasm_var_const(double x)
{
    return (CheckasmVar) { log(x), 0.0 };
}

/* Assumes independent random variables */
CheckasmVar checkasm_var_scale(CheckasmVar a, double s);
CheckasmVar checkasm_var_pow(CheckasmVar a, double exp);
CheckasmVar checkasm_var_add(CheckasmVar a, CheckasmVar b); /* approximation */
CheckasmVar checkasm_var_sub(CheckasmVar a, CheckasmVar b); /* approximation */
CheckasmVar checkasm_var_mul(CheckasmVar a, CheckasmVar b);
CheckasmVar checkasm_var_div(CheckasmVar a, CheckasmVar b);
CheckasmVar checkasm_var_inv(CheckasmVar a);

/* Statistical analysis helpers */
typedef struct CheckasmSample {
    uint64_t sum;   /* batched sum of data points */
    int      count; /* number of data points in batch */
} CheckasmSample;

typedef struct CheckasmStats {
    /* With a ~12% exponential growth on the number of data points per sample,
     * 256 samples can effectively represent many billions of data points */
#define CHECKASM_STATS_SAMPLES 256
    CheckasmSample samples[CHECKASM_STATS_SAMPLES];
    int            nb_samples;
    int            next_count;
} CheckasmStats;

static inline void checkasm_stats_reset(CheckasmStats *const stats)
{
    stats->nb_samples = 0;
    stats->next_count = 1;
}

static inline void checkasm_stats_add(CheckasmStats *const stats, const CheckasmSample s)
{
    if (s.sum > 0 && s.count > 0) {
        assert(stats->nb_samples < CHECKASM_STATS_SAMPLES);
        stats->samples[stats->nb_samples++] = s;
    }
}

static inline void checkasm_stats_count_grow(CheckasmStats *const stats, uint64_t cycles,
                                             uint64_t target_cycles)
{
    if (cycles < target_cycles >> 10) { /* sum[(1+1/64)^n | n < 200] */
        /* Function is very fast, increase iteration count dramatically */
        stats->next_count <<= 1;
    } else if (stats->next_count < 1 << 25) {
        /* Grow more slowly at 1/64 = ~1.5% growth */
        stats->next_count = ((stats->next_count << 6) + stats->next_count + 63) >> 6;
    }
}

CheckasmVar checkasm_stats_estimate(const CheckasmStats *stats);

typedef struct CheckasmMeasurement {
    CheckasmVar   product;
    int           nb_measurements;
    CheckasmStats stats; /* last measurement run */
} CheckasmMeasurement;

static inline void checkasm_measurement_init(CheckasmMeasurement *measurement)
{
    measurement->product          = checkasm_var_const(1.0);
    measurement->nb_measurements  = 0;
    measurement->stats.nb_samples = 0;
}

static inline void checkasm_measurement_update(CheckasmMeasurement *measurement,
                                               const CheckasmStats  stats)
{
    const CheckasmVar est = checkasm_stats_estimate(&stats);
    measurement->product  = checkasm_var_mul(measurement->product, est);
    measurement->nb_measurements++;
    measurement->stats.nb_samples = stats.nb_samples;
    memcpy(measurement->stats.samples, stats.samples,
           sizeof(stats.samples[0]) * stats.nb_samples);
}

static inline CheckasmVar
checkasm_measurement_result(const CheckasmMeasurement measurement)
{
    return checkasm_var_pow(measurement.product, 1.0 / measurement.nb_measurements);
}

#endif /* CHECKASM_STATS_H */
