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

#include "perf_internal.h"

#if HAVE_MACOS_KPERF

  #include <dlfcn.h>

  #include "internal.h"

  #define CFGWORD_EL0A64EN_MASK       (0x20000)
  #define CPMU_CORE_CYCLE             0x02
  #define KPC_CLASS_FIXED_MASK        (1 << 0)
  #define KPC_CLASS_CONFIGURABLE_MASK (1 << 1)
  #define COUNTERS_COUNT              10
  #define CONFIG_COUNT                8
  #define KPC_MASK                    (KPC_CLASS_CONFIGURABLE_MASK | KPC_CLASS_FIXED_MASK)

static int (*kpc_get_thread_counters)(int, unsigned int, void *);
static int kperf_init;

static inline uint64_t kperf_cycles(void)
{
    uint64_t counters[COUNTERS_COUNT];
    if (kpc_get_thread_counters(0, COUNTERS_COUNT, counters))
        return -1;

    return counters[0];
}

static uint64_t perf_start(void)
{
    return kperf_cycles();
}

static uint64_t perf_stop(uint64_t t)
{
    return kperf_cycles() - t;
}

COLD int checkasm_perf_init_macos(CheckasmPerf *perf)
{
    uint64_t config[COUNTERS_COUNT] = { 0 };

    if (kperf_init)
        goto done;

    void *kperf
        = dlopen("/System/Library/PrivateFrameworks/kperf.framework/kperf", RTLD_LAZY);
    if (!kperf) {
        fprintf(stderr, "checkasm: Unable to load kperf: %s\n", dlerror());
        return 1;
    }

    int (*kpc_force_all_ctrs_set)(int)          = dlsym(kperf, "kpc_force_all_ctrs_set");
    int (*kpc_set_counting)(uint32_t)           = dlsym(kperf, "kpc_set_counting");
    int (*kpc_set_thread_counting)(uint32_t)    = dlsym(kperf, "kpc_set_thread_counting");
    int (*kpc_set_config)(uint32_t, void *)     = dlsym(kperf, "kpc_set_config");
    uint32_t (*kpc_get_counter_count)(uint32_t) = dlsym(kperf, "kpc_get_counter_count");
    uint32_t (*kpc_get_config_count)(uint32_t)  = dlsym(kperf, "kpc_get_config_count");
    kpc_get_thread_counters                     = dlsym(kperf, "kpc_get_thread_counters");

    if (!kpc_get_thread_counters) {
        fprintf(stderr, "checkasm: Unable to load kpc_get_thread_counters\n");
        return 1;
    }

    if (!kpc_get_counter_count || kpc_get_counter_count(KPC_MASK) != COUNTERS_COUNT) {
        fprintf(stderr, "checkasm: Unexpected kpc_get_counter_count\n");
        return 1;
    }
    if (!kpc_get_config_count || kpc_get_config_count(KPC_MASK) != CONFIG_COUNT) {
        fprintf(stderr, "checkasm: Unexpected kpc_get_config_count\n");
        return 1;
    }

    config[0] = CPMU_CORE_CYCLE | CFGWORD_EL0A64EN_MASK;

    if (!kpc_set_config || kpc_set_config(KPC_MASK, config)) {
        fprintf(stderr, "checkasm: The kperf API needs to be run as root\n");
        return 1;
    }
    if (!kpc_force_all_ctrs_set || kpc_force_all_ctrs_set(1)) {
        fprintf(stderr, "checkasm: kpc_force_all_ctrs_set failed\n");
        return 1;
    }
    if (!kpc_set_counting || kpc_set_counting(KPC_MASK)) {
        fprintf(stderr, "checkasm: kpc_set_counting failed\n");
        return 1;
    }
    if (!kpc_set_counting || kpc_set_thread_counting(KPC_MASK)) {
        fprintf(stderr, "checkasm: kpc_set_thread_counting failed\n");
        return 1;
    }

    kperf_init = 1;
done:
    perf->start = perf_start;
    perf->stop  = perf_stop;
    perf->name  = "macOS (kperf)";
    perf->unit  = "cycle";
    return checkasm_perf_validate_start(perf);
}

#endif /* HAVE_MACOS_KPERF */
