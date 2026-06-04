/*
 * Copyright © 2018-2022, VideoLAN and dav1d authors
 * Copyright © 2018-2022, Two Orioles, LLC
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

#ifndef CHECKASM_PERF_INTERNAL_H
#define CHECKASM_PERF_INTERNAL_H

#include "checkasm_config.h"

#if ARCH_AARCH64

  #include <stdint.h>

  #if !defined(__APPLE__) && (!defined(_MSC_VER) || defined(__clang__))

static inline void checkasm_pmccntr_enable(void)
{
    uint64_t cfg, cen, flt;

    __asm__ __volatile__("mrs %[cfg], pmcr_el0       \n\t"
                         "mrs %[cen], pmcntenset_el0 \n\t"
                         "mrs %[flt], pmccfiltr_el0  \n\t"
                         "orr %[cfg], %[cfg], 1      \n\t"
                         "orr %[cen], %[cen], 1<<31  \n\t"
                         "bic %[flt], %[flt], (1<<30)\n\t"
                         "bic %[flt], %[flt], (1<<28)\n\t"
                         "msr pmcntenset_el0, %[cen] \n\t"
                         "msr pmcr_el0, %[cfg]       \n\t"
                         "msr pmccfiltr_el0, %[flt]  \n\t"
                         : [cfg] "=&r"(cfg), [cen] "=&r"(cen), [flt] "=&r"(flt));
}

    #define CHECKASM_PERF_ASM_INIT checkasm_pmccntr_enable

  #endif

#elif ARCH_ARM

  #if !defined(_MSC_VER) && (!defined(__thumb__) || defined(__thumb2__))

static inline void checkasm_counter_enable(void)
{
    // PMCR.E (bit 0) = 1
    __asm__ __volatile__("mcr p15, 0, %0, c9, c12, 0" ::"r"(1));
    // PMCNTENSET.C (bit 31) = 1
    __asm__ __volatile__("mcr p15, 0, %0, c9, c12, 1" ::"r"(1 << 31));
}

    #define CHECKASM_PERF_ASM_INIT checkasm_counter_enable

  #endif

#endif

/* This define isn't set by Meson, but can be deduced straight from the
 * other defines. */
#if defined(__APPLE__) && ARCH_AARCH64
  /* This only depends on <dlfcn.h>, and isn't needed on other architectures. */
  #define HAVE_MACOS_KPERF 1
#else
  #define HAVE_MACOS_KPERF 0
#endif

#endif /* CHECKASM_PERF_INTERNAL_H */
