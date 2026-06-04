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

#ifndef CHECKASM_TESTS_H
#define CHECKASM_TESTS_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "src/checkasm_config.h"

#include <checkasm/test.h>
#include <checkasm/utils.h>

enum {
    SELFTEST_CPU_FLAG_BAD_C = 1 << 0, // dummy flag for "bad" C implementations
#if ARCH_X86
    SELFTEST_CPU_FLAG_X86    = 1 << 1,
    SELFTEST_CPU_FLAG_MMX    = 1 << 2,
    SELFTEST_CPU_FLAG_SSE2   = 1 << 3,
    SELFTEST_CPU_FLAG_AVX2   = 1 << 4,
    SELFTEST_CPU_FLAG_AVX512 = 1 << 5,
#elif ARCH_RISCV
    SELFTEST_CPU_FLAG_RVI    = 1 << 1,
    SELFTEST_CPU_FLAG_RVF    = 1 << 2,
    SELFTEST_CPU_FLAG_RVV    = 1 << 3,
#elif ARCH_AARCH64
    SELFTEST_CPU_FLAG_AARCH64 = 1 << 1,
#elif ARCH_ARM
    SELFTEST_CPU_FLAG_ARM    = 1 << 1,
    SELFTEST_CPU_FLAG_VFP    = 1 << 2,
    SELFTEST_CPU_FLAG_VFPD32 = 1 << 3,
#endif
};

#define DEF_GETTER(FLAG, NAME, func_type, fallback)                                      \
    static func_type *get_##NAME(void)                                                   \
    {                                                                                    \
        return (checkasm_get_cpu_flags() & FLAG) ? selftest_##NAME : fallback;           \
    }

/* Should return the arch-specific flags */
uint64_t selftest_get_cpu_flags_x86(void);
uint64_t selftest_get_cpu_flags_riscv(void);
uint64_t selftest_get_cpu_flags_aarch64(void);
uint64_t selftest_get_cpu_flags_arm(void);

/**
 * Copy `size` (power-of-two) bytes from aligned buffers `src` to `dst`.
 */
typedef void(copy_func)(uint8_t *dst, const uint8_t *src, size_t size);
void selftest_test_copy(copy_func *func, const char *name, int min_width);

#define DEF_COPY_FUNC(NAME)                                                              \
    void selftest_##NAME(uint8_t *dst, const uint8_t *src, size_t size)

#define DEF_COPY_GETTER(FLAG, NAME) DEF_GETTER(FLAG, NAME, copy_func, selftest_copy_c)

/* Reference function for copy routines */
static inline DEF_COPY_FUNC(copy_c)
{
    memcpy(dst, src, size);
}

/**
 * Do nothing. Used to test side effects, stack corruption etc.
 * The singular int parameter is just to have at least one parameter,
 * which is required by `declare_func`.
 */
typedef void(noop_func)(int unused);
void selftest_test_noop(noop_func *func, const char *name);

#define DEF_NOOP_FUNC(NAME) void selftest_##NAME(int unused)
#define DEF_NOOP_GETTER(FLAG, NAME) DEF_GETTER(FLAG, NAME, noop_func, NULL)

/* Used for testing floating point operations */
typedef float(float_func)(float input);
typedef double(double_func)(double input);
void selftest_test_float(float_func *func, const char *name, float input);

#define DEF_FLOAT_FUNC(NAME) float selftest_##NAME(float input)

/* Platform-specific tests */
void selftest_check_generic(void);
void selftest_check_x86(void);
void selftest_check_riscv(void);
void selftest_check_aarch64(void);
void selftest_check_arm(void);

#endif /* CHECKASM_TESTS_H */
