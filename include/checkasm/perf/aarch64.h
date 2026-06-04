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

#ifndef CHECKASM_PERF_AARCH64_H
#define CHECKASM_PERF_AARCH64_H

#include <stdint.h>

#if defined(__APPLE__)

  /* Don't attempt to read pmccntr directly; this is never accessible
   * on Darwin in any known configurations. */
  #undef CHECKASM_PERF_ASM
  #undef CHECKASM_PERF_ASM_NAME
  #undef CHECKASM_PERF_ASM_UNIT

#elif defined(_MSC_VER) && !defined(__clang__)
  #include <windows.h>

  #define CHECKASM_PERF_ASM()                                                            \
      (_InstructionSynchronizationBarrier(), ReadTimeStampCounter())
  #define CHECKASM_PERF_ASM_NAME   "aarch64 (pmccntr)"
  #define CHECKASM_PERF_ASM_UNIT   "cycle"
  #define CHECKASM_PERF_ASM_USABLE 1

#else /* !MSVC */

static inline uint64_t checkasm_pmccntr(void)
{
    uint64_t cycle_counter;
    /* This requires enabling user mode access to the cycle counter
     * (which can only be done from kernel space). */
    __asm__ __volatile__("isb\nmrs %0, pmccntr_el0" : "=r"(cycle_counter)::"memory");
    return cycle_counter;
}
  #define CHECKASM_PERF_ASM()    checkasm_pmccntr()
  #define CHECKASM_PERF_ASM_NAME "aarch64 (pmccntr)"
  #define CHECKASM_PERF_ASM_UNIT "cycle"

#endif /* MSVC */

#endif /* CHECKASM_PERF_AARCH64_H */
