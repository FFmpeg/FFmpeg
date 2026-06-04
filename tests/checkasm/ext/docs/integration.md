@page integration Integration Guide

This guide covers how to integrate checkasm into existing or new projects.

@tableofcontents

@section config_options Configuration Options

Checkasm uses optional C11 features inside public header files. For safety, these are only enabled
conservatively based on the C11 standard version signaled by the compiler. Sometimes, these checks
could be relaxed, such as when the target project is explicitly compiled with `-std=c99` or older,
but using a modern compiler that would still understand and accept C11 features. In this case,
these feature checks may be checked by the user and defined before including checkasm.h. See
@ref config for a list of such options.

@section cpu_flags CPU Flags

CPU flags represent instruction set extensions and features that your optimized implementations
depend on (e.g., SSE2, AVX2, NEON). You must define an array of these flags, so checkasm can
systematically test each implementation variant.

@note checkasm does not provide CPU detection or runtime dispatch functionality on its own.
It is a pure testing framework, and as such, should not be used as a runtime dependency of your
project. This means that your project must implement its own CPU feature detection and dispatch
mechanisms for production use. checkasm plugs into these existing mechanisms during testing.

@subsection defining_cpu_flags Defining CPU Flags

Assuming you have a set of CPU flags defined in your project, e.g.,

@code{.h}
// my_cpu.h

enum {
    CPU_FLAG_SSE2    = 1 << 0,
    CPU_FLAG_SSSE3   = 1 << 1,
    CPU_FLAG_SSE41   = 1 << 2,
    CPU_FLAG_AVX2    = 1 << 3,
    CPU_FLAG_AVX512  = 1 << 4,
    // ...
};

typedef uint64_t MyCpuFlags;
MyCpuFlags detect_cpu_flags(void);
@endcode

Then create a CheckasmCpuInfo array describing each flag, terminated by `{0}`:

@code{.c}
// checkasm.c

static const CheckasmCpuInfo cpu_flags[] = {
    { "SSE2",   "sse2",   CPU_FLAG_SSE2   },
    { "SSSE3",  "ssse3",  CPU_FLAG_SSSE3  },
    { "SSE4.1", "sse41",  CPU_FLAG_SSE41  },
    { "AVX2",   "avx2",   CPU_FLAG_AVX2   },
    { "AVX512", "avx512", CPU_FLAG_AVX512 },
    {0} // array terminator
};

// This ordering means:
// - SSE2 functions are tested with just CPU_FLAG_SSE2
// - SSSE3 functions are tested with CPU_FLAG_SSE2 | CPU_FLAG_SSSE3
// - SSE4.1 functions are tested with CPU_FLAG_SSE2 | CPU_FLAG_SSSE3 | CPU_FLAG_SSE41
// - And so on...
@endcode

Each entry contains:
- **name**: Human-readable name displayed in output (e.g., "SSE4.1")
- **suffix**: Short suffix used in function names and filtering (e.g., "sse41")
- **flag**: The bitfield value from your CPU flag enum

@note Flags are tested in the order defined in the array. Each test inherits
flags from all previous entries, allowing checkasm to test progressively more
advanced instruction sets.

Register the CPU flags with checkasm via the CheckasmConfig structure:

@code{.c}
// checkasm.c

static const CheckasmCpuInfo cpu_flags[] = {
    // ...
    {0}
};

int main(int argc, const char *argv[])
{
    CheckasmConfig config = {
        .cpu_flags = cpu_flags,
        // ...
    };

    // Set initial CPU flags using your own runtime detection function
    config.cpu = detect_cpu_flags();

    return checkasm_main(&config, argc, argv);
}
@endcode

@subsection extra_cpu_flags Extra CPU Flags

You can include additional flags in CheckasmConfig.cpu that aren't in CheckasmConfig.cpu_flags.
These are transparently passed through to checkasm_get_cpu_flags() and can be used for
modifier flags like `CPU_FLAG_FAST_*` that don't require separate testing, but should
instead always be assumed to be available when matching function implementations.

@section selecting_functions Selecting Functions

There are two common strategies for selecting the correct function implementation during tests,
depending on how your project structures its dispatch mechanism:

@note Choose the strategy that matches your project's existing architecture. If you are
developing a new library, we recommend the first approach.

@subsection mask_callback Strategy 1: Mask Callback

If your project has a `mask_cpu_flags` (or `cpu_flags_override`) function that
updates an internal static bitmask used internally by dispatch table getters, e.g.:

@code{.c}
// my_cpu.c
static unsigned cpu_flags      = 0;
static unsigned cpu_flags_mask = -1;

unsigned get_cpu_flags(void)
{
    return cpu_flags & cpu_flags_mask;
}

void mask_cpu_flags(unsigned flags)
{
    cpu_flags_mask = flags;
}
@endcode

@code{.c}
// my_foo_dsp.c
void foo_dsp_init(foo_dsp *dsp)
{
    const unsigned cpu_flags = get_cpu_flags();

    // Initialize with C implementations
    dsp->add = add_c;
    dsp->sub = sub_c;

    // Override with optimized versions based on cpu_flags
    if (cpu_flags & CPU_FLAG_SSE2) {
        dsp->add = add_sse2;
    }

    if (cpu_flags & CPU_FLAG_AVX2) {
        dsp->add = add_avx2;
        dsp->sub = sub_avx2;
    }

    // ...
}
@endcode

Then, in your checkasm main file, you can set that directly as a callback in CheckasmConfig.set_cpu_flags:

@code{.c}
// You may need a wrapper to fix the function signature
static void set_cpu_flags(CheckasmCpu cpu)
{
    mask_cpu_flags((unsigned) cpu);
}

CheckasmConfig config = {
    // ...
    .set_cpu_flags = set_cpu_flags,
};
@endcode

With this approach, checkasm will automatically call your `set_cpu_flags()` function whenever
it changes the active CPU feature set during testing.

@note This will always be a subset of the initially detected CPU flags provided in CheckasmConfig.cpu,
so there is no meaningful distinction between a `mask_cpu_flags` (that masks out real CPU
flags) and a `cpu_flags_override` (that overrides them wholesale). Both can be used
as a callback.

@subsection direct_getters Strategy 2: Direct Getters

If your project uses dispatch functions that directly accept a CPU mask parameter (e.g.,
`void foo_dsp_init(foo_dsp *dsp, unsigned cpu_flags)`), you can call them within
each test using checkasm_get_cpu_flags():

@code{.c}
// my_foo_dsp.c
void foo_dsp_init(foo_dsp *dsp, unsigned cpu_flags)
{
    // Initialize *dsp based on the provided CPU flags
}
@endcode

Then, in your checkasm test files:

@code{.c}
// check_foo_dsp.c
void check_foo_dsp(void)
{
    foo_dsp dsp;
    foo_dsp_init(&dsp, checkasm_get_cpu_flags());  // Get current test flags

    // Now test dsp.add, dsp.sub, etc.
    // ...
}
@endcode

The same applies if your project uses individual function getters like
`add_func_t get_add_func(unsigned cpu_flags)` instead of dispatch tables / dsp structs.

@section organizing_tests Organizing Multiple Tests

For larger projects, organize tests by module:

@code{.c}
// Test module declarations
void checkasm_check_mc(void);
void checkasm_check_pixel(void);
void checkasm_check_filmgrain(void);
// ...

static const CheckasmTest tests[] = {
    { "mc",        checkasm_check_mc },
    { "pixel",     checkasm_check_pixel },
    { "filmgrain", checkasm_check_filmgrain },
    // ...
    {0}
};
@endcode

Then implement each test in separate files:

@code{.c}
// check_mc.c
#include <checkasm/test.h>
#include "mc_dsp.h"

static void test_mc_func1(const mc_dsp *dsp);
static void test_mc_func2(const mc_dsp *dsp);
static void test_mc_func3(const mc_dsp *dsp);
// ...

void checkasm_check_mc(void)
{
    mc_dsp dsp;
    mc_dsp_init(&dsp, checkasm_get_cpu_flags());

    test_func1(&dsp);
    test_func2(&dsp);
    test_func3(&dsp);
    checkasm_report("group1");

    test_func4(&dsp);
    test_func5(&dsp);
    test_func6(&dsp);
    checkasm_report("group2");

    // ...
}
@endcode

---

@section integration_next_steps Next Steps

Now that you know how to integrate checkasm with your project's architecture,
dive deeper into best practices and advanced patterns for writing comprehensive
tests.

**Next:** @ref writing_tests
