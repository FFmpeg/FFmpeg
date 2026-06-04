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
 * @file checkasm.h
 * @brief Main checkasm API for test suite configuration and execution
 *
 * This header provides the primary checkasm API for setting up and running
 * assembly test suites, including configuration structures, test registration,
 * and benchmark execution. It defines the main entry points and configuration
 * options for checkasm-based test programs.
 */

#ifndef CHECKASM_CHECKASM_H
#define CHECKASM_CHECKASM_H

#include <stdint.h>

#include "checkasm/attributes.h"

/**
 * @defgroup config User-provided Configuration
 * @{
 *
 * User-provided preprocessor definitions for configuring the behavior of
 * the checkasm header files. These macros should be defined before including
 * checkasm.h, based on the availability of compiler features in the target
 * project.
 */

/**
 * @def CHECKASM_HAVE_GENERIC
 * @brief Enable C11 _Generic support
 *
 * When enabled (defined to a nonzero value), checkasm uses C11's _Generic
 * keyword to enable extra checks that rely on type information. This enables
 * register width checking and floating point state checks on supported
 * platforms. When disabled (defined to 0), these features are silently
 * disabled.
 *
 * By default (when not defined), this is automatically enabled for C11 and
 * later, and disabled for older C standards. Define this macro before
 * including checkasm.h to explicitly control the behavior.
 *
 * @note This is not needed when compiling with `-std=c11` or later.
 */
#ifndef CHECKASM_HAVE_GENERIC
  #if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
    #define CHECKASM_HAVE_GENERIC 1
  #else
    #define CHECKASM_HAVE_GENERIC 0
  #endif
#endif

/** @} */ /* end of config group */

/**
 * @brief Opaque type representing a set of CPU feature flags
 *
 * Bitfield type used to represent CPU capabilities and SIMD instruction set
 * support. The specific bit values are defined by the implementation.
 */
typedef uint64_t CheckasmCpu;

/**
 * @brief Opaque type used to identify function implementations
 *
 * Used internally by checkasm to track and match different variants of
 * functions being tested.
 */
typedef uintptr_t CheckasmKey;

/**
 * @brief Describes a CPU feature flag/capability
 *
 * Used to define the CPU features that the test suite should test against.
 * Tests will be run incrementally for each CPU feature set, with each test
 * inheriting flags from previously tested CPUs.
 */
typedef struct CheckasmCpuInfo {
    const char *name;   /**< Human-readable name (e.g., "SSE2", "AVX2") */
    const char *suffix; /**< Short suffix for function names (e.g., "sse2", "avx2") */
    CheckasmCpu flag;   /**< Bitmask flag value for this CPU feature */
} CheckasmCpuInfo;

/**
 * @brief Describes a single test function
 *
 * Represents one test function that will be invoked by the test suite.
 * Each test function typically tests a specific component or subsystem.
 */
typedef struct CheckasmTest {
    const char *name;   /**< Name of the test (used for filtering and reporting) */
    void (*func)(void); /**< Test function to invoke */
} CheckasmTest;

/**
 * @brief Output format for benchmark results
 *
 * Specifies how benchmark results should be formatted.
 *
 * @note In all cases, output is written to `stdout` by default.
 */
typedef enum CheckasmFormat {
    CHECKASM_FORMAT_PRETTY, /**< Pretty-printed (colored) text output (default) */
    CHECKASM_FORMAT_CSV,    /**< Comma-separated values with optional header */
    CHECKASM_FORMAT_TSV,    /**< Tab-separated values with optional header */
    CHECKASM_FORMAT_JSON,   /**< JSON structured output with all measurement data */
    CHECKASM_FORMAT_HTML,   /**< Interactive HTML report for web viewing */
} CheckasmFormat;

/**
 * @brief Configuration structure for the checkasm test suite
 *
 * This structure contains all configuration options for running checkasm tests,
 * including test selection, CPU feature flags, benchmarking options, and output
 * formatting. Initialize this structure with your project's tests and CPU flags
 * before calling checkasm_main() or checkasm_run().
 *
 * @code
 * CheckasmConfig config = {
 *     .cpu_flags     = my_cpu_flags,
 *     .tests         = my_tests,
 *     .cpu           = my_get_cpu_flags(),
 *     .set_cpu_flags = my_set_cpu_flags,
 * };
 *
 * return checkasm_main(&config, argc, argv);
 * @endcode
 *
 * @see checkasm_main(), checkasm_run()
 */
typedef struct CheckasmConfig {
    /**
     * @brief List of CPU flags understood by the implementation
     *
     * Array of CPU features that will be tested in incremental order,
     * terminated by an entry with `CheckasmCpuInfo.flag == 0` (i.e. `{0}`).
     *
     * Each test run inherits any active flags from previously tested CPUs.
     * This allows testing progressively more advanced instruction sets.
     */
    const CheckasmCpuInfo *cpu_flags;

    /**
     * @brief Array of test functions to execute
     *
     * Array of test functions to execute, terminated by an entry with
     * `CheckasmTest.func == NULL` (i.e. `{0}`.
     */
    const CheckasmTest *tests;

    /**
     * @brief Detected CPU flags for the current system
     *
     * Set this to the detected CPU capabilities of the system. Any extra flags
     * not included in cpu_flags will also be transparently included in
     * checkasm_get_cpu_flags(), and can be used to signal flags that should
     * be assumed to always be enabled (e.g., CPU_FLAG_FAST_* modifiers).
     */
    CheckasmCpu cpu;

    /**
     * @brief Callback invoked when active CPU flags change
     *
     * If provided, this function will be called whenever the active set of
     * CPU flags changes, with the new set of flags as argument. This includes
     * once at the start of the program with the baseline set of flags.
     *
     * Use this to update global function pointers, internal static variables,
     * or dispatch tables.
     */
    void (*set_cpu_flags)(CheckasmCpu new_flags);

    /**
     * @brief Pattern for filtering which tests to run
     *
     * Shell-style wildcard pattern (e.g., "video_*") to select tests.
     * NULL means run all tests.
     */
    const char *test_pattern;

    /**
     * @brief Pattern for filtering which functions within tests to run
     *
     * Shell-style wildcard pattern to select specific functions. Matched
     * against the names passed to checkasm_check_func(). NULL means run all
     * functions.
     */
    const char *function_pattern;

    /**
     * @brief Enable benchmarking
     *
     * When nonzero, enables performance benchmarking of tested functions.
     * Set to 1 to enable with default settings.
     */
    int bench;

    /**
     * @brief Target benchmark duration in microseconds
     *
     * Target time (in µs) to spend benchmarking each function.
     * Defaults to 1000 µs if left unset when bench is enabled.
     *
     * @note Very slow functions may execute for a longer duration to ensure
     *       enough samples are collected for accurate measurement.
     */
    unsigned bench_usec;

    /** @brief Output format for benchmark results */
    CheckasmFormat format;

    /**
     * @brief Enable verbose output
     *
     * When nonzero, prints detailed timing information, failure diagnostics,
     * and extra terminal output (including table headers and extra information
     * about the active configuration).
     */
    int verbose;

    /** @brief Enable using the seed value
     *
     * If nonzero, the value in the seed field will be used even if it may be
     * zero.
     */
    int seed_set;

    /**
     * @brief Random number generator seed
     *
     * If nonzero or if seed_set is nonzero, use this seed for deterministic
     * random number generation. If zero and seed_set is zero, a seed will
     * be chosen based on the current time.
     */
    unsigned seed;

    /**
     * @brief Number of times to repeat tests
     *
     * Repeat the test (and benchmark, if enabled) this many times using
     * successive seeds. Setting to -1 effectively tests every possible seed
     * (useful for exhaustive testing).
     */
    unsigned repeat;

    /** @brief Enable process pinning via cpu_affinity
     *
     * If nonzero, the test process will be pinned to the CPU core specified
     * in cpu_affinity.
     *
     * @warning This will override the CPU affinity of the calling process, and
     *          will persist even after checkasm_run() returns.
     */
    int cpu_affinity_set;

    /**
     * @brief CPU core ID for process pinning
     *
     * If cpu_affinity_set is nonzero, pin the test process to this CPU core.
     */
    unsigned cpu_affinity;
} CheckasmConfig;

/**
 * @brief Get the current active set of CPU flags
 *
 * Returns the currently active (masked) set of CPU flags. During test execution,
 * this reflects which CPU features are currently being tested. May be called
 * from within test functions to choose an implementation to test.
 *
 * @return Current CPU feature flags as a bitmask
 *
 * @note The returned value changes as checkasm iterates through different CPU
 *       feature sets during testing.
 */
CHECKASM_API CheckasmCpu checkasm_get_cpu_flags(void);

/**
 * @brief Get the CPU flag currently being tested
 *
 * Returns the CheckasmCpuInfo structure for the CPU flag currently being
 * tested, or NULL if testing the baseline configuration with no additional
 * CPU flags.
 *
 * @return Currently active CPU flag info, or NULL
 *
 * @note Unlike checkasm_get_cpu_flags(), this only reflects the currently
 *       running test, and does not include any information about previously
 *       tested CPU flags.
 *
 * @since v1.2.0
 */
CHECKASM_API const CheckasmCpuInfo *checkasm_get_cpu_info(void);

/**
 * @brief Get the suffix for the current CPU flag, or "c" if none
 * @since v1.2.0
 */
static inline const char *checkasm_get_cpu_suffix(void)
{
    const CheckasmCpuInfo *info = checkasm_get_cpu_info();
    return info ? info->suffix : "c";
}

/**
 * @brief Print available CPU flags to stdout.
 *
 * Prints a list of all CPU flags/features that are available for testing
 * based on the configuration, as well as CPU flags which are defined but
 * unsupported on the system.
 *
 * @param[in] config Configuration containing CPU flag definitions
 */
CHECKASM_API void checkasm_list_cpu_flags(const CheckasmConfig *config);

/**
 * @brief Print available tests
 *
 * Prints a list of all test functions registered in the configuration.
 * Useful for discovering what tests are available and for use with
 * test pattern filtering.
 *
 * @param[in] config Configuration containing test definitions
 */
CHECKASM_API void checkasm_list_tests(const CheckasmConfig *config);

/**
 * @brief Print available functions within tests
 *
 * Prints a detailed list of all functions being tested across all registered
 * tests. Useful for discovering what can be filtered with function patterns.
 *
 * @param[in] config Configuration containing test definitions
 *
 * @note This requires executing all tests to gather information about the
 *       available functions. During this process, checkasm_check_func() always
 *       returns 0 to skip the actual testing. However, any side effects from
 *       test functions will still occur, unless properly guarded.
 */
CHECKASM_API void checkasm_list_functions(const CheckasmConfig *config);

/**
 * @brief Run all tests and benchmarks matching the specified patterns
 *
 * Executes the checkasm test suite according to the configuration. Tests
 * and functions are filtered according to test_pattern and function_pattern
 * if specified. Benchmarks are run if bench is enabled.
 *
 * @param[in] config Configuration structure with all test parameters
 * @return 0 on success (all tests passed), negative error code on failure
 *
 * @note This is the lower-level entry point. Most users should use
 *       checkasm_main() instead, which handles argument parsing.
 *
 * @warning This function may override the processor state in subtle ways,
 *          including enabling high-precision performance timers, installing
 *          signal handlers and configuring the terminal output.
 *
 * @see checkasm_main()
 */
CHECKASM_API int checkasm_run(const CheckasmConfig *config);

/**
 * @brief Main entry point for checkasm test programs
 *
 * Convenience wrapper around checkasm_run() that parses command-line arguments
 * and updates the config accordingly. This is the recommended entry point for
 * most checkasm test programs. Call this from your main() function.
 *
 * Before calling this function, initialize config with the minimum set of
 * project-specific fields:
 * - config.cpu_flags: Array of CPU features to test
 * - config.tests: Array of test functions
 * - config.cpu: Detected CPU capabilities
 *
 * Command-line arguments like --bench, --test, --function, --seed, etc. are
 * automatically parsed and applied to the config.
 *
 * @param[in,out] config Configuration structure (will be modified by argument parsing)
 * @param[in] argc Argument count from main()
 * @param[in] argv Argument vector from main()
 * @return 0 on success, non-zero on failure (suitable for return from main())
 *
 * @code
 * int main(int argc, const char *argv[]) {
 *     CheckasmConfig config = {
 *         .cpu_flags     = my_cpu_flags,
 *         .tests         = my_tests,
 *         .cpu           = my_get_cpu_flags(),
 *         .set_cpu_flags = my_set_cpu_flags,
 *     };
 *     return checkasm_main(&config, argc, argv);
 * }
 * @endcode
 *
 * @see checkasm_run()
 */
CHECKASM_API int checkasm_main(CheckasmConfig *config, int argc, const char *argv[]);

#endif /* CHECKASM_CHECKASM_H */
