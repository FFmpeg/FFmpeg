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

/**
 * @file test.h
 * @brief Test writing API for checkasm
 *
 * This header provides the API used within test functions to declare, call,
 * report, and benchmark different implementations of functions.
 */

#ifndef CHECKASM_TEST_H
#define CHECKASM_TEST_H

#include <stdint.h>

#include "checkasm/attributes.h"
#include "checkasm/checkasm.h"
#include "checkasm/perf.h"
#include "checkasm/platform.h"

/**
 * @def checkasm_check_func(func, name, ...)
 * @brief Check if a function should be tested and set up function references
 *
 * Determines if the given function implementation should be tested, identified
 * by the function pointer, and if so, sets up the function pointers for
 * subsequent calls to checkasm_call_ref() and checkasm_call_new().
 *
 * @param[in] func Function pointer to test, or 0 to skip
 * @param[in] name Printf-style format string for the function name
 * @param[in] ... Format arguments for the function name
 * @return Non-zero if testing should proceed, 0 if this function should be skipped
 *
 * @code
 * if (checkasm_check_func(get_my_func(checkasm_get_cpu_flags()), "my_func")) {
 *     // checkasm_call_ref() and checkasm_call_new() are now ready to use
 *     int ref_result = checkasm_call_ref(args...);
 *     int new_result = checkasm_call_new(args...);
 *     if (ref_result != new_result) {
 *         if (checkasm_fail())
 *             fprintf(stderr, "expected %d, got %d\n", ref_result, new_result);
 *     }
 * }
 * checkasm_report("my_func");
 * @endcode
 *
 * @see checkasm_func_ref, checkasm_func_new
 * @see checkasm_call_ref(), checkasm_call_new()
 * @see checkasm_check_key()
 */
#define checkasm_check_func(func, ...)                                                   \
    (checkasm_key_ref                                                                    \
     = checkasm_check_key((checkasm_key_new = (CheckasmKey) (func)), __VA_ARGS__))

/**
 * @brief Check if a key should be tested
 *
 * Determines if the given implementation should be tested, identified by an
 * arbitrary CheckasmKey.
 *
 * @note Unlike checkasm_check_func(), this does not set up any references for
 *       checkasm_call_ref() or checkasm_call_new(), and is intended for use
 *       with e.g. nontrivial function wrappers.
 *
 * @param[in] key Arbitrary CheckasmKey to test, or 0 to skip
 * @param[in] name Printf-style format string for the function name
 * @param[in] ... Format arguments for the function name
 * @return Non-zero if testing should proceed, 0 if this function should be skipped
 *
 * @return Reference key if testing should proceed, 0 to skip
 * @see checkasm_check_func()
 */
CHECKASM_API CheckasmKey checkasm_check_key(CheckasmKey key, const char *name, ...)
    CHECKASM_PRINTF(2, 3);

/**
 * @brief Set a custom variant identifier for the next checkasm_check_func() call
 *
 * Mark the next call to checkasm_check_func() or checkasm_check_key() as a
 * variant, with a customizable suffix. This will be used in reports instead of
 * the default suffix (equivalent to checkasm_get_cpu_suffix()).
 *
 * @note Variant functions are ineligible for being used as references for other
 *       functions, and are intended to test non-standard behavior such as
 *       non-bitexact, uncached or aligned implementations.
 *
 * @param[in] id Printf-style format string for the variant identifier
 * @param[in] ... Format arguments for the identifier
 * @since v1.2.0
 */
CHECKASM_API void checkasm_set_func_variant(const char *id, ...) CHECKASM_PRINTF(1, 2);

/**
 * @brief Mark the current function as failed with a custom message
 *
 * Records a test failure with a printf-style formatted message.
 *
 * @param[in] msg Printf-style format string describing the failure
 * @param[in] ... Format arguments
 * @return 1 if the failure details should be printed verbosely, 0 otherwise
 *
 * @code
 * if (output != expected)
 *    checkasm_fail_func("%s:%d", filename, line);
 * @endcode
 *
 * @note This is typically called via checkasm_fail() rather than directly.
 * @see checkasm_fail()
 */
CHECKASM_API int checkasm_fail_func(const char *msg, ...) CHECKASM_PRINTF(1, 2);

/**
 * @def checkasm_fail()
 * @brief Mark the current test as failed
 *
 * Records a test failure with the current file and line number. This is the
 * most common way to indicate test failure. The test will continue executing,
 * but any future calls to checkasm_check_func() for this function will return 0.
 *
 * @return 1 if the failure details should be printed verbosely, 0 otherwise
 *
 * @code
 * if (output != expected) {
 *     if (checkasm_fail())
 *         fprintf(stderr, "expected %d, got %d\n", expected, output);
 * }
 * @endcode
 */
#define checkasm_fail() checkasm_fail_func("%s:%d", __FILE__, __LINE__)

/**
 * @brief Report test outcome for a named group of functions.
 *
 * Prints the result (pass/fail) for a named group of functions. Typically
 * called at the end of a test, as well as after any larger block of similar
 * functions.
 *
 * @note Since v1.0.1, this is optional. If not called before the end of a
 * test, any remaining results will be reported directly under the name of the
 * associated test itself.
 *
 * @param[in] name Printf-style format string for the test case name
 * @param[in] ... Format arguments
 *
 * @code
 * for (int w = 4; w < 128; w <<= 1) {
 *     if (checkasm_check_func(.., "blockcopy_%dbpc_w%d", bits, w)) {
 *         // ...
 *     }
 * }
 *
 * checkasm_report("blockcopy_%dbpc", bits);
 * @endcode
 */
CHECKASM_API void checkasm_report(const char *name, ...) CHECKASM_PRINTF(1, 2);

/**
 * @def checkasm_declare(ret, ...)
 * @brief Declare a function signature for testing
 *
 * Declares the function prototype that will be tested. This must be called
 * before using checkasm_call_checked(), checkasm_call_ref(), or
 * checkasm_call_new(). The first argument is the return type, and remaining
 * arguments are the function parameters (naming parameters is optional).
 *
 * @param ret Return type of the function
 * @param ... Function parameter types
 *
 * @code
 * // Declare signature for: int add(int a, int b)
 * declare_func(int, int a, int b);
 *
 * // Can also omit parameter names for brevity:
 * declare_func(int, int, int);
 * @endcode
 *
 * @see checkasm_call_checked(), checkasm_call_call_ref(), checkasm_call_new()
 */
#define checkasm_declare(ret, ...)                                                       \
    checkasm_declare_impl(ret, __VA_ARGS__);                                             \
    typedef ret func_type(__VA_ARGS__);                                                  \
    (void) ((func_type *) NULL)

/**
 * @def checkasm_declare_emms(cpu_flags, ret, ...)
 * @brief Declare signature for non-ABI compliant MMX functions (x86 only)
 *
 * Variant of checkasm_declare() for MMX functions that omit calling emms
 * before returning to the caller. This is used for optimized MMX kernels
 * that expect the caller to run emms manually.
 *
 * @param cpu_flags Mask of CPU flags under which to enable the extra emms call
 * @param ret Return type of the function
 * @param ... Function parameter types
 *
 * @note MMX code normally needs to call emms before any floating-point code
 *       can be executed. Since this instruction can be very slow, many MMX
 *       kernels (used inside loops) are designed to omit emms and instead
 *       expect the caller to run emms manually after the loop. This macro
 *       will omit the emms check and instead explicitly run emms after calling
 *       the function (only when any of the specified cpu_flags are active).
 *
 * @note On non-x86 platforms, this is equivalent to checkasm_declare().
 *
 * @see checkasm_declare()
 */
#ifndef checkasm_declare_emms
  #define checkasm_declare_emms(cpu_flags, ret, ...) checkasm_declare(ret, __VA_ARGS__)
#endif

/**
 * @def checkasm_call(func, ...)
 * @brief Call a function with signal handling
 *
 * Calls an arbitrary function while handling signals (crashes, segfaults, etc.).
 * Use this for calling the reference implementation or other nominally-safe code.
 * For testing assembly/optimized implementations, use checkasm_call_checked()
 * instead, which provides additional validation for common assembly mistakes.
 *
 * @param func Function pointer to call
 * @param ... Arguments to pass to the function
 *
 * @note This only handles signals; use checkasm_call_checked() for full validation.
 * @see checkasm_call_checked()
 */
#define checkasm_call(func, ...)                                                         \
    (checkasm_set_signal_handler_state(1), (func) (__VA_ARGS__));                        \
    checkasm_set_signal_handler_state(0)

/**
 * @def checkasm_call_checked(func, ...)
 * @brief Call an assembly function with full validation
 *
 * Calls an assembly/optimized function (matching the signature declared by
 * checkasm_declare()) while handling signals and checking for common assembly
 * errors like stack corruption, clobbered registers, register size mismatches
 * and ABI violations. Use this for calling the implementation being tested.
 *
 * @param func Function pointer to call (must match declared signature)
 * @param ... Arguments to pass to the function
 *
 * @note Requires prior call to checkasm_declare().
 * @see checkasm_declare(), checkasm_call_ref(), checkasm_call_new()
 */
#ifndef checkasm_call_checked
  #define checkasm_call_checked(func, ...)                                               \
      (checkasm_set_signal_handler_state(1),                                             \
       checkasm_push_stack_guard((uintptr_t[16]) { 0, 0 }),                              \
       ((func_type *) (func))(__VA_ARGS__));                                             \
      checkasm_pop_stack_guard();                                                        \
      checkasm_set_signal_handler_state(0)
#endif

/**
 * @brief Mark a block of tests as expected to fail
 *
 * Marks the following test functions as expected to fail when any of the
 * specified CPU flags are set. Returns whether these functions should be
 * executed.
 *
 * @param[in] cpu_flags CPU flags for which failure is expected (or -1 for all)
 * @return 1 if functions should be executed, 0 if they should be skipped
 *
 * @note All functions inside such a block *must* fail, otherwise the whole
 *       test will be considered failed. This is used for testing that known-broken
 *       implementations are properly detected as broken.
 *
 * @note This is not normally useful for end users; it is mainly defined for
 *       use inside checkasm's internal test suite.
 *
 * @code
 * if (checkasm_should_fail(CPU_FLAG_SSE2)) {
 *     // This implementation is known to be broken on SSE2
 *     if (checkasm_check_func(broken_func_sse2, "broken_func")) {
 *         checkasm_call_new(); // should fail
 *     }
 * }
 * @endcode
 */
CHECKASM_API int checkasm_should_fail(CheckasmCpu cpu_flags);

/**
 * @brief Key identifying the reference implementation
 *
 * Set by checkasm_check_func() to point to the reference key for the function
 * currently being tested.
 *
 * @see checkasm_check_func(), checkasm_func_ref, checkasm_key_new
 * @since v1.0.1
 */
static CheckasmKey checkasm_key_ref;

/**
 * @brief Key identifying the implementation being tested
 *
 * Set by checkasm_check_func() to point to the key passed to it.
 *
 * @see checkasm_check_func(), checkasm_func_new, checkasm_key_ref
 * @since v1.0.1
 */
static CheckasmKey checkasm_key_new;

/**
 * @def checkasm_func_ref
 * @brief Function pointer to the reference implementation
 *
 * This is just a typed version of checkasm_key_ref, cast to the type declared
 * by checkasm_declare().
 *
 * @see checkasm_func_new(), checkasm_check_func()
 */
#define checkasm_func_ref ((func_type *) checkasm_key_ref)

/**
 * @def checkasm_func_new
 * @brief Function pointer to the implementation being tested
 *
 * This is just a typed version of checkasm_key_new, cast to the type declared
 * by checkasm_declare().
 *
 * @note This is read-only. To test a different function, use
 *       checkasm_call_checked() instead of checkasm_call_new().
 *
 * @see checkasm_func_ref(), checkasm_check_func()
 */
#define checkasm_func_new ((func_type *) checkasm_key_new)

/**
 * @def checkasm_call_ref(...)
 * @brief Call the reference implementation
 *
 * Calls the reference (C) implementation with the specified arguments.
 * Must be preceded by a successful checkasm_check_func() call.
 *
 * @param ... Arguments to pass to the function
 * @return Return value from the reference implementation
 *
 * @see checkasm_check_func(), checkasm_call_new()
 */
#define checkasm_call_ref(...) checkasm_call(checkasm_func_ref, __VA_ARGS__)

/**
 * @def checkasm_call_new(...)
 * @brief Call the implementation being tested with validation
 *
 * Calls the optimized implementation being tested with the specified arguments,
 * including full validation for register clobbering, stack corruption, etc.
 * Must be preceded by a successful checkasm_check_func() call.
 *
 * @param ... Arguments to pass to the function
 * @return Return value from the optimized implementation
 *
 * @see checkasm_check_func(), checkasm_call_ref()
 */
#define checkasm_call_new(...) checkasm_call_checked(checkasm_func_new, __VA_ARGS__)

/**
 * @def checkasm_bench(func, ...)
 * @brief Benchmark a function
 *
 * Repeatedly calls a function to measure its performance. If benchmarking is
 * enabled, runs the function many times and collects timing statistics. If
 * benchmarking is disabled, simply calls the function once with validation.
 *
 * @param func Function pointer to benchmark
 * @param ... Arguments to pass to the function
 *
 * @note This function may be called multiple times within the same
 *       checkasm_check_func() block. If done, the timing results will be
 *       accumulated and reported as a geometric mean. This is useful for
 *       benchmarking multiple different input sizes or configurations that
 *       should be reported as a single average figure.
 *
 * @see checkasm_bench_new(), checkasm_alternate()
 */
#define checkasm_bench(func, ...)                                                        \
    do {                                                                                 \
        if (checkasm_bench_func()) {                                                     \
            func_type *const bench_func = (func);                                        \
            checkasm_set_signal_handler_state(1);                                        \
            for (int truns; (truns = checkasm_bench_runs());) {                          \
                uint64_t time;                                                           \
                CHECKASM_PERF_BENCH(truns, time, __VA_ARGS__);                           \
                checkasm_clear_cpu_state();                                              \
                checkasm_bench_update(truns, time);                                      \
            }                                                                            \
            checkasm_set_signal_handler_state(0);                                        \
            checkasm_bench_finish();                                                     \
        } else {                                                                         \
            const int tidx = 0;                                                          \
            (void) tidx;                                                                 \
            checkasm_call_checked(func, __VA_ARGS__);                                    \
        }                                                                                \
    } while (0)

/**
 * @def checkasm_bench_new(...)
 * @brief Benchmark the optimized implementation
 *
 * Convenience macro that benchmarks the optimized implementation set up by
 * checkasm_check_func(). Equivalent to checkasm_bench(checkasm_func_new, ...).
 *
 * @param ... Arguments to pass to the function
 *
 * @code
 * if (checkasm_check_func(get_my_func(checkasm_get_cpu_flags()), "my_func")) {
 *     // Test correctness
 *     checkasm_call_ref(output_ref, input);
 *     checkasm_call_new(output_new, input);
 *     checkasm_check(type, output_ref, output_new);
 *
 *     // Benchmark performance
 *     checkasm_bench_new(output_new, input);
 * }
 * checkasm_report("my_func");
 * @endcode
 *
 * @see checkasm_bench(), checkasm_check_func()
 */
#define checkasm_bench_new(...) checkasm_bench(checkasm_func_new, __VA_ARGS__)

/**
 * @def checkasm_alternate(a, b)
 * @brief Alternate between two values during benchmarking
 *
 * Returns one of two values, alternating between them across benchmark
 * iterations. Intended for use within bench_new() calls for functions that
 * modify their input buffers. This ensures throughput (not latency) is
 * measured by preventing data dependencies between iterations.
 *
 * @param a First value
 * @param b Second value
 * @return Either a or b depending on the current benchmark iteration
 *
 * @code
 * CHECKASM_ALIGN(uint8_t buf0[SIZE]);
 * CHECKASM_ALIGN(uint8_t buf1[SIZE]);
 * bench_new(alternate(buf0, buf1), size);
 * // Each iteration uses a different buffer, preventing stalls
 * @endcode
 */
#define checkasm_alternate(a, b) ((tidx & 1) ? (b) : (a))

/**
 * @addtogroup aliases Short-hand Aliases
 * @brief Convenience aliases for common checkasm functions and macros
 *
 * These shorter names are provided for convenience and backwards compatibility.
 * They are functionally identical to their checkasm_* counterparts.
 * @{
 */
#define fail              checkasm_fail
#define report            checkasm_report
#define check_func        checkasm_check_func
#define check_key         checkasm_check_key
#define func_ref          checkasm_func_ref
#define func_new          checkasm_func_new
#define call_ref          checkasm_call_ref
#define call_new          checkasm_call_new
#define bench_new         checkasm_bench_new
#define alternate         checkasm_alternate
#define declare_func      checkasm_declare
#define declare_func_emms checkasm_declare_emms
/** @} */

/**
 * @addtogroup internal Internal Implementation Details
 * @brief Internal functions and structures not part of the public API
 *
 * These functions and types are used internally by checkasm macros and should
 * not be called or referenced directly by test code.
 * @{
 */

/**
 * @brief Enable or disable signal handling
 * @param[in] enabled Non-zero to enable signal handling, 0 to disable
 */
CHECKASM_API void checkasm_set_signal_handler_state(int enabled);

/**
 * @brief Push stack guard values for corruption detection
 * @param[in] guard Array of guard values to push
 */
CHECKASM_API void checkasm_push_stack_guard(uintptr_t guard[2]);
CHECKASM_API void checkasm_pop_stack_guard(void);

/**
 * @def checkasm_clear_cpu_state()
 * @brief Clear CPU state after running a function
 *
 * Clears any processor state that might be left over after running a function.
 * On x86, this typically includes running EMMS to clear MMX state.
 */
#ifndef checkasm_clear_cpu_state
  #define checkasm_clear_cpu_state()                                                     \
      do {                                                                               \
      } while (0)
#endif

typedef struct CheckasmPerf {
    /**
     * @brief Start timing measurement
     * @return Timestamp value to pass to stop()
     * @note Only used when ASM timers are not available
     */
    uint64_t (*start)(void);

    /**
     * @brief Stop timing measurement
     * @param[in] start_time Timestamp from start()
     * @return Elapsed time in the specified unit
     */
    uint64_t (*stop)(uint64_t start_time);

    /** @brief Name of the timing mechanism (e.g., "clock_gettime") */
    const char *name;

    /** @brief Unit of measurement (e.g., "ns", "cycles") */
    const char *unit;

#ifdef CHECKASM_PERF_ASM
    /** @brief Whether inline ASM timing instructions are usable */
    int asm_usable;
#endif
} CheckasmPerf;

#define CHECKASM_PERF_CALL4(...)                                                         \
    do {                                                                                 \
        int tidx = 0;                                                                    \
        bench_func(__VA_ARGS__);                                                         \
        tidx = 1;                                                                        \
        bench_func(__VA_ARGS__);                                                         \
        tidx = 2;                                                                        \
        bench_func(__VA_ARGS__);                                                         \
        tidx = 3;                                                                        \
        bench_func(__VA_ARGS__);                                                         \
        (void) tidx;                                                                     \
    } while (0)

#define CHECKASM_PERF_CALL16(...)                                                        \
    do {                                                                                 \
        CHECKASM_PERF_CALL4(__VA_ARGS__);                                                \
        CHECKASM_PERF_CALL4(__VA_ARGS__);                                                \
        CHECKASM_PERF_CALL4(__VA_ARGS__);                                                \
        CHECKASM_PERF_CALL4(__VA_ARGS__);                                                \
    } while (0)

/* Naive loop; used when perf.start/stop() is expected to be slow or imprecise, or when
 * we have no ASM cycle counters, or when the number of iterations is low */
#define CHECKASM_PERF_BENCH_SIMPLE(count, time, ...)                                     \
    do {                                                                                 \
        time = perf.start();                                                             \
        for (int tidx = 0; tidx < count; tidx++)                                         \
            bench_func(__VA_ARGS__);                                                     \
        time = perf.stop(time);                                                          \
    } while (0)

/* Unrolled loop with inline outlier rejection; used when we have asm cycle counters */
#define CHECKASM_PERF_BENCH_ASM(total_count, time, ...)                                  \
    do {                                                                                 \
        int      tcount_trim = 0;                                                        \
        uint64_t tsum_trim   = 0;                                                        \
        for (int titer = 0; titer < total_count; titer += 32) {                          \
            uint64_t t = CHECKASM_PERF_ASM();                                            \
            CHECKASM_PERF_CALL16(__VA_ARGS__);                                           \
            CHECKASM_PERF_CALL16(__VA_ARGS__);                                           \
            t = CHECKASM_PERF_ASM() - t;                                                 \
            if (t * tcount_trim <= tsum_trim * 4 && (titer > 0 || total_count < 1000)) { \
                tsum_trim += t;                                                          \
                tcount_trim++;                                                           \
            }                                                                            \
        }                                                                                \
        time        = tsum_trim;                                                         \
        total_count = tcount_trim << 5;                                                  \
    } while (0)

/* Select the best benchmarking method at runtime */
CHECKASM_API const CheckasmPerf *checkasm_get_perf(void);

#ifdef CHECKASM_PERF_ASM
  #ifndef CHECKASM_PERF_ASM_USABLE
    #define CHECKASM_PERF_ASM_USABLE perf.asm_usable
  #endif
  #define CHECKASM_PERF_BENCH(count, time, ...)                                          \
      do {                                                                               \
          const CheckasmPerf perf = *checkasm_get_perf();                                \
          if (CHECKASM_PERF_ASM_USABLE && count >= 128) {                                \
              CHECKASM_PERF_BENCH_ASM(count, time, __VA_ARGS__);                         \
          } else {                                                                       \
              CHECKASM_PERF_BENCH_SIMPLE(count, time, __VA_ARGS__);                      \
          }                                                                              \
      } while (0)
#else /* !CHECKASM_PERF_ASM */
  #define CHECKASM_PERF_BENCH(count, time, ...)                                          \
      do {                                                                               \
          const CheckasmPerf perf = *checkasm_get_perf();                                \
          CHECKASM_PERF_BENCH_SIMPLE(count, time, __VA_ARGS__);                          \
      } while (0)
#endif

/**
 * @brief Check if current function should be benchmarked
 * @return Non-zero if benchmarking is enabled for the current function
 */
CHECKASM_API int checkasm_bench_func(void);

/**
 * @brief Get number of iterations for current benchmark run
 * @return Number of iterations to run, or 0 if benchmarking is complete
 */
CHECKASM_API int checkasm_bench_runs(void);

/**
 * @brief Update benchmark statistics with timing results
 * @param[in] iterations Number of iterations that were run
 * @param[in] cycles Sum of elapsed time/cycles for those iterations
 */
CHECKASM_API void checkasm_bench_update(int iterations, uint64_t cycles);

/**
 * @brief Finalize and store benchmark results
 */
CHECKASM_API void checkasm_bench_finish(void);

/**
 * @brief Suppress unused variable warnings
 */
static inline void checkasm_unused(void)
{
    (void) checkasm_key_ref;
    (void) checkasm_key_new;
}

/** @} */ /* internal */

#endif /* CHECKASM_TEST_H */
