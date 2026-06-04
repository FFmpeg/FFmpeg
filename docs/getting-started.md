@page getting_started Getting Started

This guide will walk you through installing checkasm and writing your first test.

@tableofcontents

@section installation Installation

You can either load checkasm as a library (e.g. via `pkg-config`), or include it directly in your project's build system.

@subsection meson_submodules Meson using wrap files (recommended)

First, create `subprojects/checkasm.wrap`:

@code{.ini}
[wrap-git]
url = https://code.videolan.org/videolan/checkasm.git
revision = release # or a specific tag/release
directory = checkasm
@endcode

Then integrate it into your build system:

@code{.meson}
# This first attempts loading checkasm as an external dependency using the
# appropriate platform-specific method (e.g. pkg-config on POSIX systems),
# and falls back to using the bundled version inside `subprojects/checkasm`
# otherwise.
checkasm_dependency = dependency('checkasm',
    # Extracts the `checkasm_dep` variable from the `checkasm` subproject.
    fallback: ['checkasm', 'checkasm_dep'],
    required: false
)

# Alternatively, you can directly force use of the bundled version:
# checkasm_dependency = subproject('checkasm').get_variable('checkasm_dep')

if checkasm_dependency.found()
    checkasm = executable('checkasm',
        checkasm_sources,
        dependencies: checkasm_dependency,
    )

    test('checkasm', checkasm, suite: 'checkasm')
    benchmark('checkasm', checkasm, suite: 'checkasm', args: '--bench')
endif
@endcode

@subsection meson_wrap Meson using submodules (alternative)

As an alternative, you may use git submodules to include checkasm as a subproject.
This may be preferred in some environments where the build system cannot access
the internet during configuration time, or if you're already using submodules
in your project.

@code{.bash}
git submodule init
git submodule add -b release https://code.videolan.org/videolan/checkasm subprojects/checkasm
# or checkout a specific tag/release
@endcode

Then declare the dependency in your `meson.build` as usual. (See the previous section)

@subsection manual_installation Manual Installation

You can also build and install checkasm manually:

@code{.bash}
git clone https://github.com/videolan/checkasm.git && cd checkasm
meson setup builddir -Dprefix=$PREFIX # (set optional build prefix)
meson compile -C builddir
meson install -C builddir
@endcode

This is discouraged in favor of using Meson subprojects or distribution packages,
but may be useful inside containerized environments, CI systems or custom
build roots.

@section quick_start Quick Start Example

Let's create a simple test for a vector addition function that operates on buffers.

@subsection quick_start1 1. Prerequisites

Let's assume you have a reference implementation and an optimized version,
alongside a way of detecting CPU features and choosing the implementation
based on that:

@code{.h}
// my_dsp.h
#include <stdint.h>

enum {
    CPU_FLAG_AVX = 1 << 0,
};

unsigned detect_cpu_flags(void);

typedef void (*add8_func_t)(uint16_t *dst, const uint8_t *src1,
                            const uint8_t *src2, size_t len);

add8_func_t get_add8_func(unsigned cpu_flags);
@endcode

@code{.c}
// my_dsp.c

#include "my_cpu.h"

// Reference implementation (pure C)
static void add8_c(uint16_t *dst, const uint8_t *src1,
                   const uint8_t *src2, size_t len)
{
    for (size_t i = 0; i < len; i++)
        dst[i] = src1[i] + src2[i];
}

// Optimized implementation (pretend this is assembly)
static void add8_avx(uint16_t *dst, const uint8_t *src1,
                     const uint8_t *src2, size_t len)
{
    // Assembly optimized version would go here
    add8_c(dst, src1, src2, len);
}

add8_func_t get_add8_func(unsigned cpu_flags)
{
    if (cpu_flags & CPU_FLAG_AVX)
        return add8_avx;
    return add8_c;
}
@endcode

@subsection quick_start2 2. Write the Test

Create your test file:

@code{.c}
// check_dsp.c

#include <checkasm/checkasm.h>
#include <checkasm/test.h>

#include "my_dsp.h"

#define WIDTH 1024

static void test_add8(const CheckasmCpu cpu)
{
    // Declare aligned buffers for testing
    CHECKASM_ALIGN(uint8_t src1[WIDTH]);
    CHECKASM_ALIGN(uint8_t src2[WIDTH]);
    CHECKASM_ALIGN(uint16_t dst_c[WIDTH]);
    CHECKASM_ALIGN(uint16_t dst_a[WIDTH]);

    // Declare the function signature
    checkasm_declare(void, uint16_t *, const uint8_t *, const uint8_t *, size_t);

    if (checkasm_check_func(get_add8_func(cpu), "add_8")) {
        // Initialize source buffers with quasi-random test vectors
        INITIALIZE_BUF(src1);
        INITIALIZE_BUF(src2);

        // Test with various buffer sizes
        for (int w = 1; w <= WIDTH; w <<= 1) {
            // Clear destination buffers before each test
            CLEAR_BUF(dst_c);
            CLEAR_BUF(dst_a);

            // Call reference and optimized implementations
            checkasm_call_ref(dst_c, src1, src2, w);
            checkasm_call_new(dst_a, src1, src2, w);

            // Compare results - checkasm_check1d will report any mismatches
            checkasm_check1d(uint16_t, dst_c, dst_a, w, "sum");
        }

        // Benchmark the optimized version on the largest buffer size
        checkasm_bench_new(checkasm_alternate(dst_c, dst_a), src1, src2, WIDTH);
    }
}

static void check_dsp(void)
{
    const CheckasmCpu cpu = checkasm_get_cpu_flags();

    // Test all related functions and report as a single function group
    test_add8(cpu);
    // test_add16(cpu);
    // ...
    checkasm_report("add");

    // Check more function groups
    // ...
}

// Test registry
static const CheckasmTest tests[] = {
    { "dsp", check_dsp },
    {0} // array terminator
};

// CPU flag registry
static const CheckasmCpuInfo cpu_flags[] = {
    { "AVX", "avx", CPU_FLAG_AVX },
    {0} // array terminator
};

int main(int argc, const char *argv[]) {
    CheckasmConfig config = {
        .tests     = tests,
        .cpu_flags = cpu_flags,
        .cpu       = detect_cpu_flags(),
    };

    return checkasm_main(&config, argc, argv);
}
@endcode

@subsection quick_start3 3. Build and Run

@code{.bash}
# Compile (example using gcc directly)
gcc -o check_dsp my_dsp.c check_dsp.c $(pkg-config --cflags --libs checkasm)
# or use `meson compile` if using Meson

# Run all tests
./check_dsp
@endcode

@section options Command-Line Options

checkasm provides several useful command-line options:

@code{.bash}
# List all available functions
./checkasm --list-functions

# Run specific functions (supports wildcards)
./checkasm --function=add_*_8bpc

# Run benchmarks
./checkasm --bench

# Run specified test with higher benchmark duration (here: 10 ms)
./checkasm --test=pixel --bench --duration=10000

# Enable verbose output
./checkasm --verbose
@endcode

The `--help` output shows all available options:

@code{.txt}
Usage: checkasm [options...] <random seed>
    <random seed>              Use fixed value to seed the PRNG
Options:
    --affinity=<cpu>           Run the process on CPU <cpu>
    --bench -b                 Benchmark the tested functions
    --csv, --tsv, --json,      Choose output format for benchmarks
    --html
    --function=<pattern> -f    Test only the functions matching <pattern>
    --help -h                  Print this usage info
    --list-cpu-flags           List available cpu flags
    --list-functions           List available functions
    --list-tests               List available tests
    --duration=<μs>            Benchmark duration (per function) in μs
    --repeat[=<N>]             Repeat tests N times, on successive seeds
    --test=<pattern> -t        Test only <pattern>
    --verbose -v               Print verbose timing info and failure data
@endcode

---

@section getting_started_next_steps Next Steps

Now that you've set up checkasm and written your first test, learn how to
integrate it properly with your project's CPU detection and dispatch mechanisms.

**Next:** @ref integration
