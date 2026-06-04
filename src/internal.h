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

#ifndef CHECKASM_INTERNAL_H
#define CHECKASM_INTERNAL_H

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "checkasm/attributes.h"
#include "checkasm/test.h"
#include "longjmp.h"
#include "stats.h"

#ifdef __GNUC__
  #define COLD __attribute__((cold))
#else
  #define COLD
#endif

#ifndef __has_attribute
  #define __has_attribute(x) 0
#endif

#ifdef _MSC_VER
  #define NOINLINE __declspec(noinline)
#elif __has_attribute(noclone)
  #define NOINLINE __attribute__((noinline, noclone))
#else
  #define NOINLINE __attribute__((noinline))
#endif

#ifdef _MSC_VER
    #define NORETURN __declspec(noreturn)
#else
    #include <stdnoreturn.h>
    #define NORETURN noreturn
#endif

#ifdef _MSC_VER
  #define ALWAYS_INLINE __forceinline
#else
  #define ALWAYS_INLINE inline __attribute__((always_inline))
#endif

#ifdef __GNUC__
  #define THREAD_LOCAL __thread
#else
  #define THREAD_LOCAL _Thread_local
#endif

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#ifdef _MSC_VER
  #define PACKED(...) __pragma(pack(push, 1)) __VA_ARGS__ __pragma(pack(pop))
#else
  #define PACKED(...) __VA_ARGS__ __attribute__((__packed__))
#endif

#if __has_attribute(fallthrough)
    #define FALLTHROUGH __attribute__((fallthrough))
#else
    #define FALLTHROUGH do {} while (0)
#endif

void checkasm_srand(unsigned seed);

/* Internal variant of checkasm_fail_func() that also jumps back to the signal
 * handler */
NORETURN void checkasm_fail_abort(const char *msg, ...) CHECKASM_PRINTF(1, 2);

#define COLOR_DEFAULT -1
#define COLOR_RED     31
#define COLOR_GREEN   32
#define COLOR_YELLOW  33
#define COLOR_BLUE    34
#define COLOR_MAGENTA 35
#define COLOR_CYAN    36
#define COLOR_WHITE   37

/* Colored variant of fprintf for terminals that support it */
void checkasm_setup_fprintf(void);
void checkasm_fprintf(FILE *const f, const int color, const char *const fmt, ...)
    CHECKASM_PRINTF(3, 4);

/* Light-weight helper for printing nested JSON objects */
typedef struct CheckasmJson {
    FILE *file;
    int   level;
    int   nonempty;
} CheckasmJson;

void checkasm_json(CheckasmJson *json, const char *key, const char *fmt, ...)
    CHECKASM_PRINTF(3, 4);
void checkasm_json_str(CheckasmJson *json, const char *key, const char *str);
void checkasm_json_push(CheckasmJson *json, const char *const key, char type);
void checkasm_json_pop(CheckasmJson *json, char type);

/* Platform specific signal handling */
void        checkasm_set_signal_handlers(void);
const char *checkasm_get_last_signal_desc(void);

/* Set to 1 if the process should terminate. The current test will continue
 * executing until the next report() call, then the process will exit. */
extern volatile sig_atomic_t checkasm_interrupted;

extern checkasm_jmp_buf checkasm_context;

/* Platform specific timing code */
extern CheckasmPerf checkasm_perf;

int checkasm_perf_init(void);
int checkasm_perf_init_linux(CheckasmPerf *perf);
int checkasm_perf_init_macos(CheckasmPerf *perf);
int checkasm_perf_init_arm(CheckasmPerf *perf);
int checkasm_perf_validate_start(const CheckasmPerf *perf);
int checkasm_perf_validate_start_stop(const CheckasmPerf *perf);

int checkasm_run_on_all_cores(void (*func)(void));

uint64_t checkasm_gettime_nsec(void);
uint64_t checkasm_gettime_nsec_diff(uint64_t t); /* subtracts t */
unsigned checkasm_seed(void);
void     checkasm_noop(void *);

/* These functions update the measurements in `meas` directly; must be initialized */
void checkasm_measure_nop_cycles(CheckasmMeasurement *meas, uint64_t target_cycles);
void checkasm_measure_perf_scale(CheckasmMeasurement *meas); /* ns per cycle */

/* Miscellaneous helpers */
static inline int imax(const int a, const int b)
{
    return a > b ? a : b;
}

static inline int imin(const int a, const int b)
{
    return a < b ? a : b;
}

static inline void *checkasm_handle_oom(void *ptr)
{
    if (!ptr) {
        fprintf(stderr, "checkasm: out of memory\n");
        exit(1);
    }
    return ptr;
}

/* Allocate a zero-initialized block, clean up and exit on failure */
static inline void *checkasm_mallocz(const size_t size)
{
    return checkasm_handle_oom(calloc(1, size));
}

static inline char *checkasm_strdup(const char *str)
{
    return checkasm_handle_oom(strdup(str));
}

char *checkasm_vasprintf(const char *fmt, va_list arg);

#endif /* CHECKASM_INTERNAL_H */
