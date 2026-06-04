/*
 * Copyright © 2025, Niklas Haas
 * Copyright © 2018, VideoLAN and dav1d authors
 * Copyright © 2018, Two Orioles, LLC
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

#include "checkasm_config.h"

#include <limits.h>
#include <stdio.h>

#include "checkasm/perf.h"
#include "checkasm/test.h"
#include "internal.h"
#include "perf_internal.h"
#include "stats.h"

#ifdef CHECKASM_PERF_ASM
static uint64_t perf_start_asm(void)
{
    return CHECKASM_PERF_ASM();
}

static uint64_t perf_stop_asm(uint64_t t)
{
    return CHECKASM_PERF_ASM() - t;
}
#endif

CheckasmPerf checkasm_perf;

const CheckasmPerf *checkasm_get_perf(void)
{
    return &checkasm_perf;
}

COLD int checkasm_perf_init(void)
{
    /* checkasm_gettime_nsec() is needed to validate asm timers */
    if (checkasm_gettime_nsec() == (uint64_t) -1) {
        fprintf(stderr, "checkasm: timers are not available on this system\n");
        return 1;
    }

#if defined(CHECKASM_PERF_ASM) && CHECKASM_HAVE_LONGJMP
    if (!checkasm_save_context(checkasm_context)) {
        /* Try calling the asm timer to see if it works */
        checkasm_set_signal_handler_state(1);
        CHECKASM_PERF_ASM();
        checkasm_set_signal_handler_state(0);

        checkasm_perf.start      = perf_start_asm;
        checkasm_perf.stop       = perf_stop_asm;
        checkasm_perf.name       = CHECKASM_PERF_ASM_NAME;
        checkasm_perf.unit       = CHECKASM_PERF_ASM_UNIT;
        checkasm_perf.asm_usable = 1;
    } else {
        fprintf(stderr, "checkasm: unable to access %s cycle counter\n",
                CHECKASM_PERF_ASM_NAME);
        checkasm_perf.asm_usable = 0;
    }

  #ifdef CHECKASM_PERF_ASM_INIT
    /* Try starting the timers, if possible */
    if (checkasm_perf.asm_usable && !checkasm_save_context(checkasm_context)) {
        checkasm_set_signal_handler_state(1);
        CHECKASM_PERF_ASM_INIT();
        checkasm_set_signal_handler_state(0);

        /* If starting the timers seems to work, run that on all cores. */
        checkasm_run_on_all_cores(CHECKASM_PERF_ASM_INIT);
    }
  #endif

    /* If we got an asm timer, validate that it works. */
    if (checkasm_perf.asm_usable) {
        if (!checkasm_perf_validate_start(&checkasm_perf))
            return 0;
        checkasm_perf.asm_usable = 0;
    }
#endif

#if HAVE_LINUX_PERF
    if (!checkasm_perf_init_linux(&checkasm_perf))
        return 0;
#endif

#if HAVE_MACOS_KPERF
    if (!checkasm_perf_init_macos(&checkasm_perf))
        return 0;
#endif

#if ARCH_ARM || ARCH_AARCH64
    if (!checkasm_perf_init_arm(&checkasm_perf))
        return 0;
#endif

    /* Generic fallback to gettime() if supported */
    checkasm_perf.start = checkasm_gettime_nsec;
    checkasm_perf.stop  = checkasm_gettime_nsec_diff;
    checkasm_perf.name  = "gettime";
    checkasm_perf.unit  = "nsec";
    return 0;
}

COLD int checkasm_perf_validate_start(const CheckasmPerf *perf)
{
    /* Try to make the loop long enough to be sure that the timer should
     * increment, if it is functional. */
    const uint64_t target_nsec  = 20000; /* 20 us */
    const uint64_t start_cycles = perf->start();
    const uint64_t start_nsec   = checkasm_gettime_nsec();

    /* Only loop as long as we get the initial timer value; we exit the loop
     * as soon as we see the timer return a different value.
     * This works for a timer where we can just call the ->start() function
     * repeatedly, getting new timer values. */
    while (perf->start() == start_cycles) {
        if (checkasm_gettime_nsec_diff(start_nsec) > target_nsec) {
            fprintf(stderr, "checkasm: %s timer doesn't increment\n", perf->name);
            return 1;
        }
    }

    return 0;
}

COLD int checkasm_perf_validate_start_stop(const CheckasmPerf *perf)
{
    /* Try to make the loop long enough to be sure that the timer should
     * increment, if it is functional. */
    const uint64_t target_nsec = 20000; /* 20 us */
    const uint64_t start_nsec  = checkasm_gettime_nsec();

    uint64_t cycles = perf->start();
    /* For timers that require a pair of start/stop calls, run a busy loop
     * until long enough has passed, that the timer should have incremented. */
    while (checkasm_gettime_nsec_diff(start_nsec) <= target_nsec) {
        for (int i = 0; i < 100; i++)
            checkasm_noop(NULL);
    }
    cycles = perf->stop(cycles);

    if (cycles == 0) {
        /* The timer doesn't seem to increment at all. */
        fprintf(stderr, "checkasm: %s timer doesn't increment\n", perf->name);
        return 1;
    }

    return 0;
}

/* Measure the overhead of the timing code */
COLD void checkasm_measure_nop_cycles(CheckasmMeasurement *meas, uint64_t target_cycles)
{
    CheckasmStats stats;
    checkasm_stats_reset(&stats);
    stats.next_count = 128; /* ensure we use ASM timers if available */

    void (*const bench_func)(void *) = checkasm_noop;
    void *const ptr0 = (void *) 0x1000, *const ptr1 = (void *) 0x2000;

    const CheckasmPerf perf = checkasm_perf;
    (void) perf;

    for (uint64_t total_cycles = 0; total_cycles < target_cycles;) {
        int      count  = stats.next_count;
        uint64_t cycles = 0;

        /* Spin up the CPU */
        for (int i = 0; i < 100; i++)
            checkasm_noop(NULL);

        /* Measure the overhead of the timing code (in cycles) */
        CHECKASM_PERF_BENCH(count, cycles, alternate(ptr0, ptr1));
        total_cycles += cycles;

        checkasm_stats_add(&stats, (CheckasmSample) { cycles, count });
        checkasm_stats_count_grow(&stats, cycles, target_cycles);
        if (stats.nb_samples == (int) ARRAY_SIZE(stats.samples))
            break;
    }

    checkasm_measurement_update(meas, stats);
}

COLD void checkasm_measure_perf_scale(CheckasmMeasurement *meas)
{
    const CheckasmPerf perf = checkasm_perf;
    if (!strcmp(perf.unit, "nsec")) {
        *meas = (CheckasmMeasurement) {
            .product         = checkasm_var_const(1.0),
            .nb_measurements = 1,
        };
        return;
    }

    /* Try to make the loop long enough to be measurable, but not too long
     * to avoid being affected by CPU frequency scaling or preemption */
    const uint64_t target_nsec = 100000; /* 100 us */

    /* Estimate the time per loop iteration in two different ways */
    CheckasmStats stats;
    checkasm_stats_reset(&stats);
    stats.next_count = 100;

    while (stats.nb_samples < (int) ARRAY_SIZE(stats.samples)) {
        const int iters = stats.next_count;

        /* Warm up the CPU a tiny bit */
        for (int i = 0; i < 100; i++)
            checkasm_noop(NULL);

        uint64_t cycles;
        cycles = perf.start();
        for (int i = 0; i < iters; i++)
            checkasm_noop(NULL);
        cycles = perf.stop(cycles);

        /* Measure the same loop with wallclock time instead of cycles */
        uint64_t nsec = checkasm_gettime_nsec();
        for (int i = 0; i < iters; i++)
            checkasm_noop(NULL);
        nsec = checkasm_gettime_nsec_diff(nsec);

        assert(cycles <= INT_MAX);
        checkasm_stats_add(&stats, (CheckasmSample) { nsec, (int) cycles });
        checkasm_stats_count_grow(&stats, nsec, target_nsec);
        if (nsec > target_nsec)
            break;
    }

    checkasm_measurement_update(meas, stats);
}
