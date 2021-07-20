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

#include "macos_kperf.h"
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>

#define KPERF_LIST                                             \
    F(int, kpc_get_counting, void)                             \
    F(int, kpc_force_all_ctrs_set, int)                        \
    F(int, kpc_set_counting, uint32_t)                         \
    F(int, kpc_set_thread_counting, uint32_t)                  \
    F(int, kpc_set_config, uint32_t, void *)                   \
    F(int, kpc_get_config, uint32_t, void *)                   \
    F(int, kpc_set_period, uint32_t, void *)                   \
    F(int, kpc_get_period, uint32_t, void *)                   \
    F(uint32_t, kpc_get_counter_count, uint32_t)               \
    F(uint32_t, kpc_get_config_count, uint32_t)                \
    F(int, kperf_sample_get, int *)                            \
    F(int, kpc_get_thread_counters, int, unsigned int, void *)

#define F(ret, name, ...)                                      \
    typedef ret name##proc(__VA_ARGS__);                       \
    static name##proc *name = NULL;
KPERF_LIST
#undef F

#define CFGWORD_EL0A32EN_MASK (0x10000)
#define CFGWORD_EL0A64EN_MASK (0x20000)
#define CFGWORD_EL1EN_MASK    (0x40000)
#define CFGWORD_EL3EN_MASK    (0x80000)
#define CFGWORD_ALLMODES_MASK (0xf0000)

#define CPMU_NONE 0
#define CPMU_CORE_CYCLE 0x02
#define CPMU_INST_A64 0x8c
#define CPMU_INST_BRANCH 0x8d
#define CPMU_SYNC_DC_LOAD_MISS 0xbf
#define CPMU_SYNC_DC_STORE_MISS 0xc0
#define CPMU_SYNC_DTLB_MISS 0xc1
#define CPMU_SYNC_ST_HIT_YNGR_LD 0xc4
#define CPMU_SYNC_BR_ANY_MISP 0xcb
#define CPMU_FED_IC_MISS_DEM 0xd3
#define CPMU_FED_ITLB_MISS 0xd4

#define KPC_CLASS_FIXED_MASK        (1 << 0)
#define KPC_CLASS_CONFIGURABLE_MASK (1 << 1)
#define KPC_CLASS_POWER_MASK        (1 << 2)
#define KPC_CLASS_RAWPMU_MASK       (1 << 3)

#define COUNTERS_COUNT 10
#define CONFIG_COUNT 8
#define KPC_MASK (KPC_CLASS_CONFIGURABLE_MASK | KPC_CLASS_FIXED_MASK)

static int ff_kperf_was_init = 0;

int ff_kperf_init()
{
    uint64_t config[COUNTERS_COUNT] = {0};
    void *kperf = NULL;

    if (ff_kperf_was_init)
        return 0;

    kperf = dlopen("/System/Library/PrivateFrameworks/kperf.framework/Versions/A/kperf", RTLD_LAZY);
    if (!kperf) {
        fprintf(stderr, "kperf: kperf = %p\n", kperf);
        return -1;
    }

#define F(ret, name, ...)                            \
    name = (name##proc *)(dlsym(kperf, #name));      \
    if (!name) {                                     \
        fprintf(stderr, "kperf: %s = %p\n", #name, (void *)name);    \
        return -1;                                   \
    }
    KPERF_LIST
#undef F

    if (kpc_get_counter_count(KPC_MASK) != COUNTERS_COUNT) {
        fprintf(stderr, "kperf: wrong fixed counters count\n");
        return -1;
    }

    if (kpc_get_config_count(KPC_MASK) != CONFIG_COUNT) {
        fprintf(stderr, "kperf: wrong fixed config count\n");
        return -1;
    }

    config[0] = CPMU_CORE_CYCLE | CFGWORD_EL0A64EN_MASK;
    // config[3] = CPMU_INST_BRANCH | CFGWORD_EL0A64EN_MASK;
    // config[4] = CPMU_SYNC_BR_ANY_MISP | CFGWORD_EL0A64EN_MASK;
    // config[5] = CPMU_INST_A64 | CFGWORD_EL0A64EN_MASK;

    if (kpc_set_config(KPC_MASK, config)) {
        fprintf(stderr, "kperf: kpc_set_config failed\n");
        return -1;
    }

    if (kpc_force_all_ctrs_set(1)) {
        fprintf(stderr, "kperf: kpc_force_all_ctrs_set failed\n");
        return -1;
    }

    if (kpc_set_counting(KPC_MASK)) {
        fprintf(stderr, "kperf: kpc_set_counting failed\n");
        return -1;
    }

    if (kpc_set_thread_counting(KPC_MASK)) {
        fprintf(stderr, "kperf: kpc_set_thread_counting failed\n");
        return -1;
    }

    ff_kperf_was_init = 1;

    return 0;
}

uint64_t ff_kperf_cycles()
{
    uint64_t counters[COUNTERS_COUNT];
    if (kpc_get_thread_counters(0, COUNTERS_COUNT, counters)) {
        return -1;
    }

    return counters[0];
}
