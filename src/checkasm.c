/*
 * Copyright © 2025, Niklas Haas
 * Copyright © 2018, VideoLAN and dav1d authors
 * Copyright © 2018, Two Orioles, LLC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "checkasm_config.h"

#if HAVE_PTHREAD_SETAFFINITY_NP
  /* _GNU_SOURCE is required for pthread_setaffinity_np and CPU_SET on glibc. */
  #ifndef _GNU_SOURCE
    #define _GNU_SOURCE
  #endif
#endif

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "checkasm/checkasm.h"
#include "checkasm/test.h"
#include "cpu.h"
#include "function.h"
#include "html_data.h"
#include "internal.h"
#include "stats.h"

#ifndef _WIN32
  #if HAVE_PTHREAD_SETAFFINITY_NP
    #include <pthread.h>
    #if HAVE_PTHREAD_NP_H
      #include <pthread_np.h>
    #endif
  #endif
#endif

#ifdef _WIN32
  #include <windows.h>
#endif

#if HAVE_PRCTL
  #include <sys/prctl.h>
#endif

/* Internal state */
static CheckasmConfig cfg;
static CheckasmStats  stats; /* temporary buffer for function measurements */

/* Current function/test state, reset after each test run */
static struct {
    CheckasmFuncTree tree;

    /* (Re)set by check_cpu_flag() */
    const CheckasmCpuInfo *cpu;
    int                    cpu_name_printed;
    CheckasmCpu            cpu_flags;
    int                    cpu_suffix_length;
    const char            *test_name;
    int                    should_fail;
    int                    report_idx;

    /* (Re)set per function (check_func, bench_finish) */
    CheckasmFunc        *func;
    CheckasmFuncVersion *func_ver;
    char                *func_variant;
    uint64_t             cycles;

    /* Overall stats for this test run */
    int    num_funcs;                   /* known functions */
    int    num_checked;                 /* checked versions */
    int    num_failed;                  /* failed versions */
    int    num_benched;                 /* benched versions */
    int    prev_checked, prev_failed;   /* reset by report() */
    int    saved_checked, saved_failed; /* for restoring after a crash */
    double var_sum, var_max;
} current;

/* Global state for the entire checkasm_run() call */
static struct {
    /* Miscellaneous global state (cosmetic) */
    int max_function_name_length;
    int max_report_name_length;

    /* Timing code measurements (aggregated over multiple trials) */
    CheckasmMeasurement nop_cycles;
    CheckasmMeasurement perf_scale;

    /* Runtime constants */
    uint64_t target_cycles;
    int      skip_tests;
} state;

CheckasmCpu checkasm_get_cpu_flags(void)
{
    return current.cpu_flags;
}

const CheckasmCpuInfo *checkasm_get_cpu_info(void)
{
    return current.cpu;
}

/* Get the suffix of the specified cpu flag */
static const char *cpu_suffix(const CheckasmCpuInfo *cpu)
{
    return cpu ? cpu->suffix : "c";
}

static const char *ver_suffix(const CheckasmFuncVersion *ver)
{
    return ver->suffix ? ver->suffix : cpu_suffix(ver->cpu);
}

/* Returns the coefficient of variation (CV) */
static double relative_error(double lvar)
{
    return sqrt(exp(lvar) - 1.0);
}

static inline char separator(CheckasmFormat format)
{
    switch (format) {
    case CHECKASM_FORMAT_CSV: return ',';
    case CHECKASM_FORMAT_TSV: return '\t';
    default:                  return 0;
    }
}

static void json_var(CheckasmJson *json, const char *key, const char *unit,
                     const CheckasmVar var)
{
    if (key)
        checkasm_json_push(json, key, '{');
    if (unit)
        checkasm_json_str(json, "unit", unit);
    checkasm_json(json, "mode", "%g", checkasm_mode(var));
    checkasm_json(json, "median", "%g", checkasm_median(var));
    checkasm_json(json, "mean", "%g", checkasm_mean(var));
    checkasm_json(json, "lowerCI", "%g", checkasm_sample(var, -1.96));
    checkasm_json(json, "upperCI", "%g", checkasm_sample(var, 1.96));
    checkasm_json(json, "stdDev", "%g", checkasm_stddev(var));
    checkasm_json(json, "logMean", "%g", var.lmean);
    checkasm_json(json, "logVar", "%g", var.lvar);
    if (key)
        checkasm_json_pop(json, '}');
}

static void json_measurement(CheckasmJson *json, const char *key, const char *unit,
                             const CheckasmMeasurement measurement)
{
    const CheckasmVar result = checkasm_measurement_result(measurement);
    if (key)
        checkasm_json_push(json, key, '{');
    json_var(json, NULL, unit, result);
    checkasm_json(json, "numMeasurements", "%d", measurement.nb_measurements);

    if (measurement.stats.nb_samples) {
        json_var(json, "regressionSlope", unit,
                 checkasm_stats_estimate(&measurement.stats));
        checkasm_json_push(json, "rawData", '[');
        for (int i = 0; i < measurement.stats.nb_samples; i++) {
            const CheckasmSample s = measurement.stats.samples[i];
            checkasm_json(json, NULL, "{ \"iters\": %d, \"cycles\": %" PRIu64 " }",
                          s.count, s.sum);
        }
        checkasm_json_pop(json, ']');
    }

    if (key)
        checkasm_json_pop(json, '}');
}

static void cpu_info_json(void *priv, const char *fmt, ...)
{
    CheckasmJson *json = priv;
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    checkasm_json_str(json, NULL, buf);
}

struct IterState {
    const char  *test;
    const char  *report;
    CheckasmJson json;
};

static void print_bench_header(struct IterState *const iter)
{
    const CheckasmVar   nop_cycles = checkasm_measurement_result(state.nop_cycles);
    const CheckasmVar   perf_scale = checkasm_measurement_result(state.perf_scale);
    const CheckasmVar   nop_time   = checkasm_var_mul(nop_cycles, perf_scale);
    CheckasmJson *const json       = &iter->json;

    switch (cfg.format) {
    case CHECKASM_FORMAT_TSV:
    case CHECKASM_FORMAT_CSV:
        if (cfg.verbose) {
            const char sep = separator(cfg.format);
            printf("name%csuffix%c%ss%cstddev%cnanoseconds\n", sep, sep,
                   checkasm_perf.unit, sep, sep);
            printf("nop%c%c%.4f%c%.5f%c%.4f\n", sep, sep, checkasm_mode(nop_cycles), sep,
                   checkasm_stddev(nop_cycles), sep, checkasm_mode(nop_time));
        }
        break;
    case CHECKASM_FORMAT_HTML:
        printf("<!doctype html>\n"
               "<html>\n"
               "<head>\n"
               "  <meta charset=\"utf-8\"/>\n"
               "  <title>checkasm report</title>\n"
               "  <script type=\"module\">\n"
               "    %s"
               "    %s"
               "  </script>\n"
               "  <style>\n"
               "    %s"
               "  </style>\n"
               "  <script type=\"application/json\" id=\"report-data\">\n",
               checkasm_chart_js, checkasm_js, checkasm_css);
        FALLTHROUGH;
    case CHECKASM_FORMAT_JSON:
        checkasm_json_push(json, NULL, '{');
        checkasm_json_str(json, "checkasmVersion", CHECKASM_VERSION);
        checkasm_json(json, "numChecked", "%d", current.num_checked);
        checkasm_json(json, "numFailed", "%d", current.num_failed);
        checkasm_json(json, "targetCycles", "%" PRIu64, state.target_cycles);
        checkasm_json(json, "numBenchmarks", "%d", current.num_benched);
        checkasm_json_push(json, "config", '{');
        if (cfg.test_pattern)
            checkasm_json_str(json, "testPattern", cfg.test_pattern);
        if (cfg.function_pattern)
            checkasm_json_str(json, "functionPattern", cfg.function_pattern);
        checkasm_json(json, "benchUsec", "%u", cfg.bench_usec);
        checkasm_json(json, "seed", "%u", cfg.seed);
        checkasm_json(json, "repeat", "%u", cfg.repeat);
        if (cfg.cpu_affinity_set)
            checkasm_json(json, "cpuAffinity", "%u", cfg.cpu_affinity);
        checkasm_json_pop(json, '}'); /* close config */
        checkasm_json_push(json, "cpuInfo", '[');
        checkasm_cpu_info(cpu_info_json, json, &cfg);
        checkasm_json_pop(json, ']');
        checkasm_json_push(json, "cpuFlags", '{');
        for (const CheckasmCpuInfo *info = cfg.cpu_flags; info->flag; info++) {
            const int available = (cfg.cpu & info->flag) == info->flag;
            checkasm_json_push(json, info->suffix, '{');
            checkasm_json_str(json, "name", info->name);
            checkasm_json(json, "available", available ? "true" : "false");
            checkasm_json_pop(json, '}');
        }
        checkasm_json_pop(json, '}'); /* close cpuFlags */
        checkasm_json_push(json, "tests", '[');
        for (const CheckasmTest *test = cfg.tests; test->func; test++)
            checkasm_json_str(json, NULL, test->name);
        checkasm_json_pop(json, ']'); /* close tests */
        char perf_scale_unit[32];
        snprintf(perf_scale_unit, sizeof(perf_scale_unit), "nsec/%s", checkasm_perf.unit);
        json_measurement(json, "nopCycles", checkasm_perf.unit, state.nop_cycles);
        json_measurement(json, "timerScale", perf_scale_unit, state.perf_scale);
        json_var(json, "nopTime", checkasm_perf.unit, nop_time);
        checkasm_json(json, "numFunctions", "%d", current.num_funcs);
        checkasm_json_push(json, "functions", '{');
        break;
    case CHECKASM_FORMAT_PRETTY:
        checkasm_fprintf(stdout, COLOR_YELLOW, "Benchmark results:\n");
        checkasm_fprintf(stdout, COLOR_GREEN, "  name%*ss",
                         5 + state.max_function_name_length, checkasm_perf.unit);
        if (cfg.verbose) {
            checkasm_fprintf(stdout, COLOR_GREEN, " +/- stddev %*s", 26,
                             "time (nanoseconds)");
        }
        checkasm_fprintf(stdout, COLOR_GREEN, " (vs ref)\n");
        if (cfg.verbose) {
            printf("  nop:%*.1f +/- %-7.1f %11.1f ns +/- %-6.1f\n",
                   6 + state.max_function_name_length, checkasm_mode(nop_cycles),
                   checkasm_stddev(nop_cycles), checkasm_mode(nop_time),
                   checkasm_stddev(nop_time));
        }
        break;
    }
}

static void print_bench_footer(struct IterState *const iter)
{
    const double        err_rel = relative_error(current.var_sum / current.num_benched);
    const double        err_max = relative_error(current.var_max);
    CheckasmJson *const json    = &iter->json;

    switch (cfg.format) {
    case CHECKASM_FORMAT_TSV:
    case CHECKASM_FORMAT_CSV: break;
    case CHECKASM_FORMAT_PRETTY:
        if (cfg.verbose) {
            printf(" - average timing error: %.3f%% across %d benchmarks "
                   "(maximum %.3f%%)\n",
                   100.0 * err_rel, current.num_benched, 100.0 * err_max);
        }
        break;
    case CHECKASM_FORMAT_HTML:
    case CHECKASM_FORMAT_JSON:
        checkasm_json_pop(json, '}'); /* close functions */
        checkasm_json(json, "averageError", "%g", err_rel);
        checkasm_json(json, "maximumError", "%g", err_max);
        checkasm_json_pop(json, '}'); /* close root */

        if (cfg.format == CHECKASM_FORMAT_HTML) {
            printf("  </script>\n"
                   "  <meta name=\"viewport\" content=\"width=device-width, "
                   "initial-scale=1\">\n"
                   "</head>\n"
                   "%s"
                   "</html>\n",
                   checkasm_html_body);
        }
        break;
    }
}

static void print_bench_iter(const CheckasmFunc *const f, struct IterState *const iter)
{
    CheckasmJson *const json = &iter->json;
    const char          sep  = separator(cfg.format);
    if (!f)
        return;

    print_bench_iter(f->child[0], iter);

    const CheckasmFuncVersion *ref        = &f->versions;
    const CheckasmFuncVersion *v          = ref;
    const CheckasmVar          nop_cycles = checkasm_measurement_result(state.nop_cycles);
    const CheckasmVar          perf_scale = checkasm_measurement_result(state.perf_scale);

    /* Defer pushing the function header until we know that we have at least one
     * benchmark to report */
    int json_func_pushed = 0;

    do {
        if (v->cycles.nb_measurements) {
            const CheckasmVar raw     = checkasm_measurement_result(v->cycles);
            const CheckasmVar raw_ref = checkasm_measurement_result(ref->cycles);

            const CheckasmVar cycles     = checkasm_var_sub(raw, nop_cycles);
            const CheckasmVar cycles_ref = checkasm_var_sub(raw_ref, nop_cycles);
            const CheckasmVar ratio      = checkasm_var_div(cycles_ref, cycles);
            const CheckasmVar raw_time   = checkasm_var_mul(raw, perf_scale);
            const CheckasmVar time       = checkasm_var_mul(cycles, perf_scale);

            switch (cfg.format) {
            case CHECKASM_FORMAT_HTML:
            case CHECKASM_FORMAT_JSON:
                if (!json_func_pushed) {
                    checkasm_json_push(json, f->name, '{');
                    checkasm_json_str(json, "testName", f->test_name);
                    if (f->report_name)
                        checkasm_json_str(json, "reportName", f->report_name);
                    checkasm_json_push(json, "versions", '{');
                    json_func_pushed = 1;
                }

                checkasm_json_push(json, ver_suffix(v), '{');
                json_measurement(json, "rawCycles", checkasm_perf.unit, v->cycles);
                json_var(json, "rawTime", "nsec", raw_time);
                json_var(json, "adjustedCycles", checkasm_perf.unit, cycles);
                json_var(json, "adjustedTime", "nsec", time);
                if (v != ref && ref->cycles.nb_measurements)
                    json_var(json, "ratio", NULL, checkasm_var_div(cycles_ref, cycles));
                checkasm_json_pop(json, '}'); /* close version */
                break;
            case CHECKASM_FORMAT_TSV:
            case CHECKASM_FORMAT_CSV:
                printf("%s%c%s%c%.4f%c%.5f%c%.4f\n", f->name, sep, ver_suffix(v),
                       sep, checkasm_mode(cycles), sep, checkasm_stddev(cycles), sep,
                       checkasm_mode(time));
                break;
            case CHECKASM_FORMAT_PRETTY:;
                const int pad = 12 + state.max_function_name_length
                              - printf("  %s_%s:", f->name, ver_suffix(v));
                printf("%*.1f", imax(pad, 0), checkasm_mode(cycles));
                if (cfg.verbose) {
                    printf(" +/- %-7.1f %11.1f ns +/- %-6.1f", checkasm_stddev(cycles),
                           checkasm_mode(time), checkasm_stddev(time));
                }
                if (v != ref && ref->cycles.nb_measurements) {
                    const double ratio_lo = checkasm_sample(ratio, -1.0);
                    const double ratio_hi = checkasm_sample(ratio, 1.0);
                    const int    color    = ratio_lo >= 10.0 ? COLOR_GREEN
                                          : ratio_hi >= 1.1 && ratio_lo >= 1.0 ? COLOR_DEFAULT
                                          : ratio_hi >= 1.0 ? COLOR_YELLOW
                                                            : COLOR_RED;
                    printf(" (");
                    checkasm_fprintf(stdout, color, "%5.2fx", checkasm_mode(ratio));
                    printf(")");
                }
                printf("\n");
                break;
            }
        }
    } while ((v = v->next));

    if (json_func_pushed) {
        checkasm_json_pop(json, '}'); /* close versions */
        checkasm_json_pop(json, '}'); /* close function */
    }

    print_bench_iter(f->child[1], iter);
}

static void print_benchmarks(void)
{
    struct IterState iter = { .json.file = stdout };
    print_bench_header(&iter);
    print_bench_iter(current.tree.root, &iter);
    print_bench_footer(&iter);
    assert(iter.json.level == 0);
}

/* Decide whether or not the current function needs to be benchmarked */
int checkasm_bench_func(void)
{
    return !current.num_failed && cfg.bench && !checkasm_interrupted;
}

int checkasm_bench_runs(void)
{
    if (checkasm_interrupted)
        return 0;

    /* This limit should be impossible to hit in practice */
    if (stats.nb_samples == CHECKASM_STATS_SAMPLES)
        return 0;

    /* Try and gather at least 30 samples for statistical validity, even if
     * it means exceeding the time budget */
    if (current.cycles < state.target_cycles || stats.nb_samples < 30)
        return stats.next_count;
    else
        return 0;
}

/* Update benchmark results of the current function */
void checkasm_bench_update(const int iterations, const uint64_t cycles)
{
    checkasm_stats_add(&stats, (CheckasmSample) { cycles, iterations });
    checkasm_stats_count_grow(&stats, cycles, state.target_cycles);
    current.cycles += cycles;

    /* Emit this periodically while benchmarking, to avoid the SIMD
     * units turning on and off during long bench runs of non-SIMD
     * functions */
#if ARCH_X86
    checkasm_simd_warmup();
#endif
}

void checkasm_bench_finish(void)
{
    CheckasmFuncVersion *const v = current.func_ver;
    if (v && current.cycles) {
        const CheckasmVar cycles = checkasm_stats_estimate(&stats);

        /* Accumulate multiple bench_new() calls */
        checkasm_measurement_update(&v->cycles, stats);

        /* Keep track of min/max/avg (log) variance */
        current.var_sum += cycles.lvar;
        current.var_max = fmax(current.var_max, cycles.lvar);
        current.num_benched++;
    }

    checkasm_stats_reset(&stats);
    current.cycles = 0;
}

/* Compares a string with a wildcard pattern. */
static int wildstrcmp(const char *str, const char *pattern)
{
    const char *wild = strchr(pattern, '*');
    if (wild) {
        const size_t len = wild - pattern;
        if (strncmp(str, pattern, len))
            return 1;
        while (*++wild == '*')
            ;
        if (!*wild)
            return 0;
        str += len;
        while (*str && wildstrcmp(str, wild))
            str++;
        return !*str;
    }
    return strcmp(str, pattern);
}

static void handle_interrupt(void);

/* Perform tests and benchmarks for the specified
 * cpu flag if supported by the host */
static void check_cpu_flag(const CheckasmCpuInfo *cpu)
{
    const CheckasmCpu prev_cpu_flags = current.cpu_flags;
    if (cpu) {
        current.cpu_flags |= cpu->flag & cfg.cpu;
    } else {
        /* Also include any CPU flags not related to the CPU flags list */
        current.cpu_flags = cfg.cpu;
        for (const CheckasmCpuInfo *info = cfg.cpu_flags; info->flag; info++)
            current.cpu_flags &= ~info->flag;
    }

    if (cpu && current.cpu_flags == prev_cpu_flags)
        return;

    current.func              = NULL;
    current.report_idx        = 1;
    current.cpu               = cpu;
    current.cpu_name_printed  = 0;
    current.cpu_suffix_length = (int) strlen(cpu_suffix(cpu)) + 1;
    if (cfg.set_cpu_flags)
        cfg.set_cpu_flags(current.cpu_flags);

    for (const CheckasmTest *test = cfg.tests; test->func; test++) {
        if (cfg.test_pattern && wildstrcmp(test->name, cfg.test_pattern))
            continue;
        current.test_name = test->name;

        if (checkasm_save_context(checkasm_context)) {
            const char *signal = checkasm_get_last_signal_desc();
            handle_interrupt();
            checkasm_fail_func("%s", signal);

            /* We want to associate this (and any prior) failures with the
             * correct report group, so remember the failure state until we
             * reach the same position in the test() function again */
            current.func_ver->state = CHECKASM_FUNC_CRASHED;
            current.saved_checked   = current.num_checked - current.prev_checked;
            current.saved_failed    = current.num_failed - current.prev_failed;
            current.num_failed      = current.prev_failed;
            current.num_checked     = current.prev_checked;
            current.func            = NULL;
        }

        checkasm_srand(cfg.seed);
        current.should_fail = 0; // reset between tests
        test->func();
        checkasm_report(NULL); // catch any un-reported functions

        if (cfg.bench && !state.skip_tests) {
            /* Measure NOP and perf scale after each test+CPU flag configuration */
            handle_interrupt();
            checkasm_measure_nop_cycles(&state.nop_cycles, state.target_cycles);
            handle_interrupt();
            checkasm_measure_perf_scale(&state.perf_scale);
        }

        free(current.func_variant);
        current.func_variant = NULL;
    }
}

/* Print the name of the current CPU flag, but only do it once */
static void print_cpu_name(void)
{
    if (!current.cpu_name_printed) {
        checkasm_fprintf(stderr, COLOR_YELLOW, "%s:\n",
                         current.cpu ? current.cpu->name : "C");
        current.cpu_name_printed = 1;
    }
}

int checkasm_run_on_all_cores(void (*func)(void))
{
#if HAVE_PTHREAD_SETAFFINITY_NP && defined(CPU_SET)
    cpu_set_t mask;
    if (pthread_getaffinity_np(pthread_self(), sizeof(mask), &mask))
        return 1;

    int ret = 0;
    for (int c = 0; c < CPU_SETSIZE; c++) {
        if (CPU_ISSET(c, &mask)) {
            cpu_set_t set;
            CPU_ZERO(&set);
            CPU_SET(c, &set);
            if (pthread_setaffinity_np(pthread_self(), sizeof(set), &set)) {
                ret = 1;
                break;
            }
            func();
        }
    }
    pthread_setaffinity_np(pthread_self(), sizeof(mask), &mask);
    return ret;
#else
    return 1;
#endif
}

static int set_cpu_affinity(const unsigned affinity)
{
    int affinity_err;

#ifdef _WIN32
    HANDLE process = GetCurrentProcess();
  #if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
    BOOL(WINAPI * spdcs)(HANDLE, const ULONG *, ULONG) = (void *) GetProcAddress(
        GetModuleHandleW(L"kernel32.dll"), "SetProcessDefaultCpuSets");
    if (spdcs)
        affinity_err = !spdcs(process, (ULONG[]) { (ULONG) affinity + 256 }, 1);
    else
  #endif
    {
        if (affinity < sizeof(DWORD_PTR) * 8)
            affinity_err = !SetProcessAffinityMask(process, (DWORD_PTR) 1 << affinity);
        else
            affinity_err = 1;
    }
#elif HAVE_PTHREAD_SETAFFINITY_NP && defined(CPU_SET)
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(affinity, &set);
    affinity_err = pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
#else
    (void) affinity;
    (void) affinity_err;
    fprintf(stderr, "checkasm: --affinity is not supported on your system\n");
    return 1;
#endif

    if (affinity_err) {
        fprintf(stderr, "checkasm: invalid cpu affinity (%u)\n", affinity);
        return 1;
    } else {
        fprintf(stderr, "checkasm: running on cpu %u\n", affinity);
        return 0;
    }
}

void checkasm_list_cpu_flags(const CheckasmConfig *cfg)
{
    checkasm_setup_fprintf();

    for (const CheckasmCpuInfo *info = cfg->cpu_flags; info->flag; info++) {
        if ((cfg->cpu & info->flag) == info->flag)
            checkasm_fprintf(stdout, COLOR_GREEN, "%s", info->suffix);
        else
            checkasm_fprintf(stdout, COLOR_RED, "~%s", info->suffix);
        printf(info[1].flag ? ", " : "\n");
    }
}

void checkasm_list_tests(const CheckasmConfig *config)
{
    for (const CheckasmTest *test = config->tests; test->func; test++)
        printf("%s\n", test->name);
}

static void print_functions(const CheckasmFunc *const f)
{
    if (f) {
        print_functions(f->child[0]);
        const CheckasmFuncVersion *v = &f->versions;
        printf("%s (%s", f->name, ver_suffix(v));
        while ((v = v->next))
            printf(", %s", ver_suffix(v));
        printf(")\n");
        print_functions(f->child[1]);
    }
}

void checkasm_list_functions(const CheckasmConfig *config)
{
    memset(&state, 0, sizeof(state));
    memset(&current, 0, sizeof(current));
    state.skip_tests = 1;
    cfg              = *config;

    check_cpu_flag(NULL);
    for (const CheckasmCpuInfo *info = cfg.cpu_flags; info->flag; info++)
        check_cpu_flag(info);

    print_functions(current.tree.root);
    checkasm_func_tree_uninit(&current.tree);
}

static void cpu_fprintf(void *priv, const char *fmt, ...)
{
    FILE *f = priv;

    va_list ap;
    va_start(ap, fmt);
    fprintf(f, " - CPU: ");
    vfprintf(f, fmt, ap);
    fprintf(f, "\n");
    va_end(ap);
}

static COLD void print_info(void)
{
    checkasm_fprintf(stderr, COLOR_YELLOW, "checkasm:\n");
    checkasm_cpu_info(cpu_fprintf, stderr, &cfg);

    if (cfg.bench) {
        fprintf(stderr, " - Timing source: %s\n", checkasm_perf.name);
        if (cfg.verbose) {
            const CheckasmVar perf_scale = checkasm_measurement_result(state.perf_scale);
            const CheckasmVar nop_cycles = checkasm_measurement_result(state.nop_cycles);
            const CheckasmVar mhz = checkasm_var_div(checkasm_var_const(1e3), perf_scale);
            fprintf(stderr,
                    " - Timing resolution: %.4f +/- %.3f ns/%s (%.0f +/- %.1f "
                    "MHz) (provisional)\n",
                    checkasm_mode(perf_scale), checkasm_stddev(perf_scale),
                    checkasm_perf.unit, checkasm_mode(mhz), checkasm_stddev(mhz));

            fprintf(stderr,
                    " - No-op overhead: %.2f +/- %.3f %ss per call (provisional)\n",
                    checkasm_mode(nop_cycles), checkasm_stddev(nop_cycles),
                    checkasm_perf.unit);
        }
        fprintf(stderr, " - Bench duration: %d µs per function (%" PRIu64 " %ss)\n",
                cfg.bench_usec, state.target_cycles, checkasm_perf.unit);
    }
    fprintf(stderr, " - Random seed: %u\n", cfg.seed);
}

static int print_summary(void)
{
    /* Exclude C/ref versions from count reported to user */
    const int num_checked_asm = current.num_checked - current.num_funcs;
    if (current.num_failed) {
        fprintf(stderr, "checkasm: %d of %d tests failed\n", current.num_failed,
                num_checked_asm);
    } else if (num_checked_asm) {
        fprintf(stderr, "checkasm: all %d tests passed\n", num_checked_asm);
    } else {
        fprintf(stderr, "checkasm: no tests to perform\n");
    }

    if (current.num_benched && !current.num_failed)
        print_benchmarks();

    return current.num_failed > 0;
}

static void handle_interrupt(void)
{
    if (checkasm_interrupted) {
        fprintf(stderr, "checkasm: interrupted\n");
        print_summary();
        exit(128 + checkasm_interrupted);
    }
}

int checkasm_run(const CheckasmConfig *config)
{
#if !HAVE_HTML_DATA
    if (cfg.format == CHECKASM_FORMAT_HTML) {
        fprintf(stderr, "checkasm: built without HTML support\n");
        return 1;
    }
#endif

    memset(&state, 0, sizeof(state));
    memset(&current, 0, sizeof(current));
    cfg = *config;

    checkasm_set_signal_handlers();
#if HAVE_PRCTL && defined(PR_SET_UNALIGN)
    prctl(PR_SET_UNALIGN, PR_UNALIGN_SIGBUS);
#endif
    if (cfg.cpu_affinity_set)
        set_cpu_affinity(cfg.cpu_affinity);
    checkasm_setup_fprintf();

    if (!cfg.seed && !cfg.seed_set)
        cfg.seed = checkasm_seed();
    if (!cfg.repeat)
        cfg.repeat = 1;
    if (!cfg.bench_usec)
        cfg.bench_usec = 1000;

    if (cfg.bench) {
        if (checkasm_perf_init())
            return 1;

        checkasm_stats_reset(&stats);
        checkasm_measurement_init(&state.nop_cycles);
        checkasm_measurement_init(&state.perf_scale);
        checkasm_measure_perf_scale(&state.perf_scale);

        /* Use the low estimate to compute the number of target cycles, to
         * ensure we reach the required number of cycles with confidence */
        const CheckasmVar perf_scale   = checkasm_measurement_result(state.perf_scale);
        const double      low_estimate = checkasm_sample(perf_scale, -1.0);
        if (low_estimate <= 0.0) {
            fprintf(stderr,
                    "checkasm: cycle counter seems to be non-functional "
                    "(invalid timer scale: %.4f %ss/nsec)\n",
                    checkasm_mode(perf_scale), checkasm_perf.unit);
            return 1;
        }

        state.target_cycles = (uint64_t) (1e3 * cfg.bench_usec / low_estimate);
        checkasm_measure_nop_cycles(&state.nop_cycles, state.target_cycles);
    }

    checkasm_init_cpu();

    print_info();

    for (unsigned i = 0; i < cfg.repeat; i++) {
        if (i > 0) {
            checkasm_fprintf(stderr, COLOR_YELLOW, "\nTest #%d:\n", i + 1);
            fprintf(stderr, " - Random seed: %u\n", cfg.seed);
        }

        check_cpu_flag(NULL);
        for (const CheckasmCpuInfo *info = cfg.cpu_flags; info->flag; info++)
            check_cpu_flag(info);

        int res = print_summary();
        checkasm_func_tree_uninit(&current.tree);
        if (res)
            return res;

        memset(&current, 0, sizeof(current));
        cfg.seed++;
    }

    return 0;
}

/* Decide whether or not the specified function needs to be tested and
 * allocate/initialize data structures if needed. Returns a pointer to a
 * reference function if the function should be tested, otherwise NULL */
CheckasmKey checkasm_check_key(const CheckasmKey version, const char *const name, ...)
{
    char    name_buf[256];
    va_list arg;

    if (checkasm_interrupted)
        goto skip;

    va_start(arg, name);
    int name_length = vsnprintf(name_buf, sizeof(name_buf), name, arg);
    va_end(arg);

    if (!version || name_length <= 0 || (size_t) name_length >= sizeof(name_buf)
        || (cfg.function_pattern && wildstrcmp(name_buf, cfg.function_pattern)))
        goto skip;

    CheckasmFunc *const  f   = checkasm_func_get(&current.tree, name_buf);
    CheckasmFuncVersion *v   = &f->versions;
    CheckasmKey          ref = version;

    if (v->key) {
        CheckasmFuncVersion *prev;
        do {
            if (v->state == CHECKASM_FUNC_CRASHED) {
                /* This function threw a signal last time; so restore the
                 * retained test state for the next report() call */
                v->state = CHECKASM_FUNC_FAILED;
                current.num_checked += current.saved_checked;
                current.num_failed += current.saved_failed;
                current.saved_checked = 0;
                current.saved_failed  = 0;
                current.func          = f;
                current.func_ver      = v;
                for (CheckasmFunc *fp = f; fp; fp = fp->prev)
                    fp->report_idx = current.report_idx;
            }

            /* Skip functions without a working reference */
            if (!v->cpu && v->state != CHECKASM_FUNC_OK)
                goto skip;

            /* Only test functions that haven't already been tested */
            if (v->key == version)
                goto skip;

            /* Exclude failed or variant functions from being used as ref */
            if (v->state == CHECKASM_FUNC_OK && !v->suffix)
                ref = v->key;

            prev = v;
        } while ((v = v->next));

        v = prev->next = checkasm_mallocz(sizeof(CheckasmFuncVersion));
    }

    if (current.func_variant) {
        v->suffix = current.func_variant;
        current.func_variant = NULL;
        name_length += (int) strlen(v->suffix) + 1;
    } else {
        name_length += current.cpu_suffix_length;
    }

    if (name_length > state.max_function_name_length)
        state.max_function_name_length = name_length;

    v->key   = version;
    v->state = CHECKASM_FUNC_OK;
    v->cpu   = current.cpu;
    if (ref == version)
        current.num_funcs++;

    if (state.skip_tests)
        goto skip;

    /* Associate this function with each other function that was last used
     * as part of the same report group */
    if (f->report_idx < current.report_idx) {
        f->report_idx = current.report_idx;
        f->prev       = current.func;
        f->test_name  = current.test_name;
    }

    current.func     = f;
    current.func_ver = v;
    current.num_checked++;
    checkasm_srand(cfg.seed);

    if (cfg.bench) {
#if ARCH_X86
        checkasm_simd_warmup();
#endif
        checkasm_measurement_init(&v->cycles);
    }

    return ref;

skip:
    free(current.func_variant);
    current.func_variant = NULL;
    return 0;
}

void checkasm_set_func_variant(const char *id_fmt, ...)
{
    va_list arg;
    va_start(arg, id_fmt);
    assert(!current.func_variant);
    current.func_variant = checkasm_vasprintf(id_fmt, arg);
    va_end(arg);
}

/* Indicate that the current test has failed, return whether verbose printing
 * is requested. */
static int fail_internal(const char *const msg, va_list arg)
{
    CheckasmFuncVersion *const v = current.func_ver;
    if (v && v->state == CHECKASM_FUNC_OK) {
        if (!current.should_fail) {
            print_cpu_name();
            checkasm_fprintf(stderr, COLOR_RED, "FAILURE:");
            fprintf(stderr, " %s_%s (", current.func->name, ver_suffix(v));
            vfprintf(stderr, msg, arg);
            fputs(")\n", stderr);
        }

        v->state = CHECKASM_FUNC_FAILED;
        current.num_failed++;
    }
    return cfg.verbose && !current.should_fail;
}

int checkasm_fail_func(const char *const msg, ...)
{
    va_list arg;
    va_start(arg, msg);
    const int ret = fail_internal(msg, arg);
    va_end(arg);
    return ret;
}

void checkasm_fail_abort(const char *const msg, ...)
{
    va_list arg;
    va_start(arg, msg);
    fail_internal(msg, arg);
    va_end(arg);

    checkasm_load_context(checkasm_context);
    abort(); // in case we don't have a longjmp implementation
}

int checkasm_should_fail(CheckasmCpu cpu_flags)
{
    current.should_fail = !!(current.cpu_flags & cpu_flags);

#if CHECKASM_HAVE_LONGJMP
    return 1; /* we can catch any crashes */
#else
    /* If our signal handler isn't working, we shouldn't run tests that
     * are expected to fail, as they may rely on the signal handler. */
    return !current.should_fail;
#endif
}

/* Print the outcome of all tests performed since
 * the last time this function was called */
void checkasm_report(const char *const name, ...)
{
    char report_name[256];

    /* Calculate the amount of padding required to make the output vertically aligned */
    int length = (int) strlen(current.test_name);
    if (name) {
        va_list arg;
        va_start(arg, name);
        length += 1 + vsnprintf(report_name, sizeof(report_name), name, arg);
        va_end(arg);
    }

    if (length > state.max_report_name_length)
        state.max_report_name_length = length;

    const int new_checked = current.num_checked - current.prev_checked;
    if (new_checked) {
        int pad_length = (int) state.max_report_name_length + 3; // strlen(" - ")
        assert(!state.skip_tests);

        int fails = current.num_failed - current.prev_failed;
        if (current.should_fail)
            current.num_failed = current.prev_failed + (new_checked - fails);

        /* Omit "OK" for non-verbose non-benchmark C function successes */
        const int want_print = current.num_failed != current.prev_failed
                            || current.should_fail || cfg.verbose || cfg.bench
                            || current.cpu;

        if (want_print) {
            print_cpu_name();
            if (name) {
                pad_length -= fprintf(stderr, " - %s.%s", current.test_name, report_name);
            } else {
                pad_length -= fprintf(stderr, " - %s", current.test_name);
            }
            fprintf(stderr, "%*c", imax(pad_length, 0) + 2, '[');

            if (current.num_failed == current.prev_failed) {
                checkasm_fprintf(stderr, COLOR_GREEN,
                                 current.should_fail ? "EXPECTED" : "OK");
            } else if (!current.should_fail)
                checkasm_fprintf(stderr, COLOR_RED, "FAILED");
            else
                checkasm_fprintf(stderr, COLOR_RED, "%d/%d EXPECTED", fails, new_checked);
            fprintf(stderr, "]\n");
        }

        current.prev_checked = current.num_checked;
        current.prev_failed  = current.num_failed;
    }

    /* Store the report name with each function in this report group */
    CheckasmFunc *func = current.func;
    while (func) {
        if (name && !func->report_name)
            func->report_name = checkasm_strdup(report_name);
        func = func->prev;
    }

    current.func = NULL; /* reset current function for new report */
    current.report_idx++;
    handle_interrupt();
}

static void print_usage(const char *const progname)
{
    fprintf(stderr,
            "Usage: %s [options...] <random seed>\n"
            "    <random seed>              Use fixed value to seed the PRNG\n"
            "Options:\n"
            "    --affinity=<cpu>           Run the process on CPU <cpu>\n"
            "    --bench -b                 Benchmark the tested functions\n"
            "    --csv, --tsv, --json,      Choose output format for benchmarks\n"
            "    --html\n"
            "    --function=<pattern> -f    Test only the functions matching "
            "<pattern>\n"
            "    --help -h                  Print this usage info\n"
            "    --list-cpu-flags           List available cpu flags\n"
            "    --list-functions           List available functions\n"
            "    --list-tests               List available tests\n"
            "    --duration=<μs>            Benchmark duration (per function) in "
            "μs\n"
            "    --repeat[=<N>]             Repeat tests N times, on successive seeds\n"
            "    --test=<pattern> -t        Test only <pattern>\n"
            "    --verbose -v               Print verbose timing info and failure "
            "data\n",
            progname);
}

static int parseu(unsigned *const dst, const char *const str, const int base)
{
    unsigned long val;
    char         *end;
    errno = 0;
    val   = strtoul(str, &end, base);
    if (errno || end == str || *end)
        return 0;
#if !defined(__SIZEOF_LONG__) || !defined(__SIZEOF_INT__) || __SIZEOF_LONG__ > __SIZEOF_INT__
    /* This condition is split out; it can cause -Wtype-limits warnings on
     * 32 bit platforms and on Windows:
     * warning: result of comparison 'unsigned long' > 4294967295 is always false [-Wtautological-type-limit-compare] */
    if (val > (unsigned) -1)
        return 0;
#endif
    *dst = (unsigned) val;
    return 1;
}

int checkasm_main(CheckasmConfig *config, int argc, const char *argv[])
{
    while (argc > 1) {
        if (!strncmp(argv[1], "--help", 6) || !strcmp(argv[1], "-h")) {
            print_usage(argv[0]);
            return 0;
        } else if (!strcmp(argv[1], "--list-cpu-flags")
                   || !strcmp(argv[1], "--list-cpuflags")) {
            checkasm_list_cpu_flags(config);
            return 0;
        } else if (!strcmp(argv[1], "--list-tests")) {
            checkasm_list_tests(config);
            return 0;
        } else if (!strcmp(argv[1], "--list-functions")) {
            checkasm_list_functions(config);
            return 0;
        } else if (!strcmp(argv[1], "--bench") || !strcmp(argv[1], "-b")) {
            config->bench = 1;
        } else if (!strncmp(argv[1], "--bench=", 8)) {
            config->bench            = 1;
            config->function_pattern = argv[1] + 8;
        } else if (!strcmp(argv[1], "--csv")) {
            config->format = CHECKASM_FORMAT_CSV;
        } else if (!strcmp(argv[1], "--tsv")) {
            config->format = CHECKASM_FORMAT_TSV;
        } else if (!strcmp(argv[1], "--json")) {
            config->format = CHECKASM_FORMAT_JSON;
        } else if (!strcmp(argv[1], "--html")) {
#if HAVE_HTML_DATA
            config->format = CHECKASM_FORMAT_HTML;
#else
            fprintf(stderr, "checkasm: built without HTML support\n");
            return 1;
#endif
        } else if (!strncmp(argv[1], "--duration=", 11)) {
            const char *const s = argv[1] + 11;
            if (!parseu(&config->bench_usec, s, 10)) {
                fprintf(stderr, "checkasm: invalid duration (%s)\n", s);
                print_usage(argv[0]);
                return 1;
            }
        } else if (!strncmp(argv[1], "--test=", 7)) {
            config->test_pattern = argv[1] + 7;
        } else if (!strcmp(argv[1], "-t")) {
            config->test_pattern = argc > 1 ? argv[2] : "";
            argc--;
            argv++;
        } else if (!strncmp(argv[1], "--function=", 11)) {
            config->function_pattern = argv[1] + 11;
        } else if (!strcmp(argv[1], "-f")) {
            config->function_pattern = argc > 1 ? argv[2] : "";
            argc--;
            argv++;
        } else if (!strcmp(argv[1], "--verbose") || !strcmp(argv[1], "-v")) {
            config->verbose = 1;
        } else if (!strncmp(argv[1], "--affinity=", 11)) {
            const char *const s      = argv[1] + 11;
            config->cpu_affinity_set = 1;
            if (!parseu(&config->cpu_affinity, s, 16)) {
                fprintf(stderr, "checkasm: invalid cpu affinity (%s)\n", s);
                print_usage(argv[0]);
                return 1;
            }
        } else if (!strncmp(argv[1], "--repeat=", 9)) {
            const char *const s = argv[1] + 9;
            if (!parseu(&config->repeat, s, 10)) {
                fprintf(stderr, "checkasm: invalid number of repetitions (%s)\n", s);
                print_usage(argv[0]);
                return 1;
            }
        } else if (!strcmp(argv[1], "--repeat")) {
            config->repeat = UINT_MAX;
        } else {
            config->seed_set = 1;
            if (!parseu(&config->seed, argv[1], 10)) {
                fprintf(stderr, "checkasm: unknown option (%s)\n", argv[1]);
                print_usage(argv[0]);
                return 1;
            }
        }

        argc--;
        argv++;
    }

    return checkasm_run(config);
}
