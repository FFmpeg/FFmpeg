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

#ifndef CHECKASM_PERF_ARM_H
#define CHECKASM_PERF_ARM_H

#if !defined(_MSC_VER) && (!defined(__thumb__) || defined(__thumb2__))

  #include <stdint.h>

static inline uint64_t checkasm_counter(void)
{
    uint32_t cycle_counter;
        /* This requires enabling user mode access to the cycle counter (which
         * can only be done from kernel space).
         *
         * On architectures before ARMv7, this timer isn't accessible, but we
         * can still assemble the "mrc" instruction for reading it (provided
         * that we're building either in ARM or Thumb2 mode; this instruction
         * isn't available in Thumb1) and try accessing it with a signal
         * handler.
         */
  #if defined(__ARM_ARCH) && __ARM_ARCH >= 7
    /* This barrier can't be assembled unless we're targeting armv7; providing
     * .inst equivalents below. */
    __asm__ __volatile__("isb" ::: "memory");
  #elif defined(__thumb2__)
    /* Thumb2 representation of "isb" */
    __asm__ __volatile__(".inst.w 0xf3bf8f6f" ::: "memory");
  #else
    /* ARM representation of "isb" */
    __asm__ __volatile__(".inst 0xf57ff06f" ::: "memory");
  #endif
    __asm__ __volatile__("mrc p15, 0, %0, c9, c13, 0" : "=r"(cycle_counter)::"memory");
    return cycle_counter;
}

  #define CHECKASM_PERF_ASM()    checkasm_counter()
  #define CHECKASM_PERF_ASM_NAME "armv7 (ccnt)"
  #define CHECKASM_PERF_ASM_UNIT "cycle"

#else

  #undef CHECKASM_PERF_ASM
  #undef CHECKASM_PERF_ASM_NAME
  #undef CHECKASM_PERF_ASM_UNIT

#endif /* !defined(_MSC_VER) && (!defined(__thumb__) || defined(__thumb2__)) */
#endif /* CHECKASM_PERF_ARM_H */
