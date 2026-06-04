@page writing_tests Writing Tests

This guide covers best practices and common patterns for writing checkasm tests.

@tableofcontents

@section test_structure Basic Test Structure

Before diving into advanced patterns and best practices, familiarize yourself
with the basic test structure by reading the @ref quick_start.

The typical test workflow is:
1. Allocate (aligned) buffers for test data
2. Declare the function signature with checkasm_declare()
3. Check if the function should be tested with checkasm_check_func()
4. Initialize test inputs and clear output buffers
5. Call both reference (checkasm_call_ref()) and new (checkasm_call_new()) implementations
6. Compare results with checkasm_check2d() or similar
7. Benchmark the new implementation with checkasm_bench_new()
8. Report results with checkasm_report() (optional)

@section naming_conventions API Naming Conventions

checkasm supports two API naming styles:

@subsection modern_api Modern API (Recommended)

The modern API uses the `checkasm_` prefix for all functions:

@code{.c}
checkasm_declare(void, uint8_t *dst, const uint8_t *src, int len);
checkasm_check_func(dsp->func, "func_name")
checkasm_call_ref(dst_c, src, len);
checkasm_call_new(dst_a, src, len);
checkasm_check1d(uint8_t, dst_c, dst_a, len, "dst");
checkasm_bench_new(dst_a, src, len);
checkasm_report("func_name");
checkasm_fail()
checkasm_alternate(buf0, buf1)
// ...
@endcode

@subsection legacy_api Legacy/Short API

For convenience and backwards compatibility, shorter aliases are available:

@code{.c}
declare_func(void, uint8_t *dst, const uint8_t *src, int len);
check_func(dsp->func, "func_name")
call_ref(dst_c, src, len);
call_new(dst_a, src, len);
// checkasm_check1d() has no short alias
bench_new(dst_a, src, len);
report("func_name");
fail()
alternate(buf0, buf1)
@endcode

@note Both naming styles are fully supported and can be mixed within the same
test file. However, for consistency and readability in documentation and new
code, we recommend using the modern `checkasm_` prefixed names.

@section best_practices Best Practices

The remainder of this guide focuses on best practices, common patterns, and advanced testing scenarios.

@subsection bp_buffer_allocation Buffer Allocation

Always use properly aligned buffers for testing:

@code{.c}
// For simple arrays
CHECKASM_ALIGN(uint8_t buf[1024]);

// For 2D buffers with automatic padding and stride calculation
// - Defines `dst` as a pointer to a 64x32 area, and `dst_stride` (in bytes)
BUF_RECT(uint8_t, dst, 64, 32);
@endcode

**Important:** Apply CHECKASM_ALIGN() to each buffer individually:

@code{.c}
// Correct
CHECKASM_ALIGN(uint8_t buf1[32]);
CHECKASM_ALIGN(uint8_t buf2[32]);

// Wrong - only buf1 will be aligned
CHECKASM_ALIGN(uint8_t buf1[32], buf2[32]);
@endcode

@subsection bp_buffer_init Buffer Initialization

checkasm provides several buffer initialization functions:

@code{.c}
// Random data (uniformly distributed)
RANDOMIZE_BUF(buf);
checkasm_randomize(buf, sizeof(buf)); // Equivalent to the above macro
checkasm_randomize_mask8 (buf8,  width, 0x0F); // Constrain value range
checkasm_randomize_mask16(buf16, width, 1023); // Random 10-bit values

// Pathological test patterns (mix of edge cases and random bytes)
INITIALIZE_BUF(buf);
checkasm_init(buf, sizeof(buf)); // Equivalent to the above macro
checkasm_init_mask16(buf16, width, (1 << 12) - 1); // Constrain to 12-bit range

// Clear to a constant value
CLEAR_BUF(buf);
checkasm_clear(buf, sizeof(buf)); // Equivalent to the above macro
checkasm_clear16(buf16, width, 0x1234);
@endcode

For 2D buffers created with BUF_RECT():

@code{.c}
BUF_RECT(uint8_t, src, 64, 32);
INITIALIZE_BUF_RECT(src);  // Initialize with pathological test bytes
RANDOMIZE_BUF_RECT(tmp);   // Fill with random data
CLEAR_BUF_RECT(dst);       // Clear to constant data (currently 0xAA)
@endcode

See @ref memory for a full list of buffer initialization functions.

@subsection bp_test_loops Testing Multiple Configurations

Test functions across multiple configurations (e.g. block sizes) to ensure
comprehensive coverage:

@code{.c}
BUF_RECT(uint8_t, src,   128, 128);
BUF_RECT(uint8_t, dst_c, 128, 128);
BUF_RECT(uint8_t, dst_a, 128, 128);

checkasm_declare(void, uint8_t *dst, ptrdiff_t dst_stride,
                       const uint8_t *src, ptrdiff_t src_stride,
                       int w, int h);

// Test various power-of-two block sizes, from 4x4 to 128x128
for (int h = 4; h <= 128; h <<= 1) {
    for (int w = 4; w <= 128; w <<= 1) {
        if (checkasm_check_func(get_func(w), "func_%dx%d", w, h)) {
            // Initialize test data - doing this inside the loop picks a
            // different data pattern for each test
            INITIALIZE_BUF(src);
            CLEAR_BUF(dst_c);
            CLEAR_BUF(dst_a);

            // Test this configuration
            checkasm_call_ref(dst_c, dst_c_stride, src, src_stride, w, h);
            checkasm_call_new(dst_a, dst_a_stride, src, src_stride, w, h);

            checkasm_check_rect_padded(dst_c, dst_c_stride,
                                       dst_a, dst_a_stride, w, h, "dst");

            // Benchmark this configuration
            checkasm_bench_new(checkasm_alternate(dst_a, dst_c), dst_a_stride,
                               src, src_stride, w, h);
        }
    }
}

checkasm_report("func");
@endcode

@subsection bp_random_params Using Random Parameters

Use checkasm_rand() and related functions to generate diverse test inputs:

@code{.c}
// Random integers
int byte = checkasm_rand() & 0xFF;
uint32_t flags = checkasm_rand_uint32();

// Random floats
double d = checkasm_randf();  // [0.0, 1.0)
float f = (float) checkasm_randf() * 100.0f;  // [0.0, 100.0)

// Normal distribution
double normal = checkasm_rand_norm();  // mean=0, stddev=1
checkasm_randomize_normf(buf, len);    // Fill buffer with N(0,1)
@endcode

See @ref rng for a full list of random number generation functions.

@subsection bp_reporting Organizing Reports

Group related functions and use checkasm_report() strategically:

@code{.c}
static void check_add_functions(const DSPContext *dsp)
{
    // Test multiple related functions
    for (int bpc = 8; bpc <= 12; bpc += 2) {
        if (checkasm_check_func(get_add8(bpc), "add8_%dbpc", bpc)) {
            // Test add8
        }

        if (checkasm_check_func(get_add16(bpc), "add16_%dbpc", bpc)) {
            // Test add16
        }
    }

    // Report once for the whole group
    checkasm_report("add");
}
@endcode

For very simple tests with only a few functions, for which there is no
logical grouping, or for miscellaneous functions, this can be left out.
Any functions without a checkasm_report() call after them will be implicitly
reported under the name of the test itself. Note that this only makes sense
if such functions are the last functions being tested in a given test, since
any later checkasm_report() call would otherwise include all prior functions.

@section common_patterns Common Test Patterns

@subsection pattern_2d 2D Buffer Processing

For functions operating on 2D buffers with stride:

@code{.c}
static void check_filter(const DSPContext *dsp)
{
    // Define padded 64x64 buffers
    BUF_RECT(uint8_t, src,   64, 64);
    BUF_RECT(uint8_t, dst_c, 64, 64);
    BUF_RECT(uint8_t, dst_a, 64, 64);

    checkasm_declare(void, uint8_t *dst, ptrdiff_t dst_stride,
                     const uint8_t *src, ptrdiff_t src_stride,
                     int w, int h);

    for (int w = 4; w <= 64; w <<= 1) {
        if (checkasm_check_func(dsp->filter, "filter_w%d", w)) {
            // Check multiple heights
            for (int h = 4; h <= 64; h <<= 1) {
                INITIALIZE_BUF_RECT(src);
                CLEAR_BUF_RECT(dst_c);
                CLEAR_BUF_RECT(dst_a);

                checkasm_call_ref(dst_c, dst_c_stride, src, src_stride, w, h);
                checkasm_call_new(dst_a, dst_a_stride, src, src_stride, w, h);

                // Check buffers including padding
                checkasm_check_rect_padded(dst_c, dst_c_stride,
                                           dst_a, dst_a_stride,
                                           w, h, "dst");
            }

            // Benchmark only the full height configuration
            checkasm_bench_new(checkasm_alternate(dst_a, dst_c), dst_a_stride,
                               src, src_stride, w, 64);
        }
    }

    checkasm_report("filter");
}
@endcode

@subsection pattern_state State-Based Functions

For functions that modify internal state or have side effects:

@code{.c}
static void check_decoder(const DecoderContext *dec)
{
    DecoderState state_c, state_a;
    uint8_t bitstream[128];

    // Initialize bitstream with random data
    RANDOMIZE_BUF(bitstream);

    checkasm_declare(int, DecoderState *state, const uint8_t *data, int len);

    if (checkasm_check_func(dec->decode, "decode")) {
        // Initialize both states identically
        init_decoder_state(&state_c, bitstream, 128);
        init_decoder_state(&state_a, bitstream, 128);

        // Decode and compare
        int result_c = checkasm_call_ref(&state_c, bitstream, 128);
        int result_a = checkasm_call_new(&state_a, bitstream, 128);

        // Compare return values
        if (result_c != result_a) {
            if (checkasm_fail()) {
                fprintf(stderr, "return value mismatch: %d vs %d\n",
                        result_c, result_a);
            }
        }

        // Compare final states (optional)
        //
        // The validity of this check depends on whether or not `DecoderState`
        // has padding bytes or non-deterministic internal state.
        checkasm_check1d(uint8_t, &state_c, &state_a, sizeof(DecoderState),
                         "decoder state");

        checkasm_bench_new(&state_a, bitstream, 128);
    }

    checkasm_report("decode");
}
@endcode

@subsection pattern_multi_output Multiple Outputs

For functions that produce multiple output values:

@code{.c}
static void check_stats(const DSPContext *dsp)
{
    CHECKASM_ALIGN(uint8_t buf[64 * 64]);

    checkasm_declare(int, const uint8_t *buf, int len,
                     unsigned *variance, unsigned *sum);

    if (checkasm_check_func(dsp->compute_stats, "compute_stats")) {
        unsigned var_c, var_a, sum_c, sum_a;

        INITIALIZE_BUF(buf);

        int result_c = checkasm_call_ref(buf, 64*64, &var_c, &sum_c);
        int result_a = checkasm_call_new(buf, 64*64, &var_a, &sum_a);

        // Compare all outputs
        if (result_c != result_a || var_c != var_a || sum_c != sum_a) {
            if (checkasm_fail()) {
                fprintf(stderr, "result: %d vs %d, var: %u vs %u, sum: %u vs %u\n",
                        result_c, result_a, var_c, var_a, sum_c, sum_a);
            }
        }

        checkasm_bench_new(buf, 64*64, &var_a, &sum_a);
    }

    checkasm_report("compute_stats");
}
@endcode

@subsection pattern_custom_input Custom Input Generation

For functions requiring specific test patterns:

@code{.c}
// Generate worst-case inputs to stress the implementation
static void generate_worst_case(uint16_t *buf, int len, int bitdepth_max)
{
    // Create reverse sorted sequence of input values
    for (int i = 0; i < len; i++)
        buf[i] = (len - 1 - i) & bitdepth_max;
}

static void check_transform(const DSPContext *dsp)
{
    #define WIDTH 64
    CHECKASM_ALIGN(int16_t src  [WIDTH]);
    CHECKASM_ALIGN(int16_t dst_c[WIDTH]);
    CHECKASM_ALIGN(int16_t dst_a[WIDTH]);

    checkasm_declare(void, int16_t *dst, const int16_t *src, int width);

    if (checkasm_check_func(dsp->transform, "transform")) {
        // Test with both random and worst-case inputs
        for (int pattern = 0; pattern < 2; pattern++) {
            if (pattern == 0) {
                INITIALIZE_BUF(src); // Random input
            } else {
                generate_worst_case(src, WIDTH, 32767); // Worst case pattern
            }

            CLEAR_BUF(dst_c);
            CLEAR_BUF(dst_a);

            checkasm_call_ref(dst_c, src, WIDTH);
            checkasm_call_new(dst_a, src, WIDTH);

            checkasm_check1d(int16_t, dst_c, dst_a, WIDTH, "dst");
        }

        checkasm_bench_new(checkasm_alternate(dst_a, dst_c), src, WIDTH);
    }

    checkasm_report("transform");
}
@endcode

@section advanced_topics Advanced Topics

@subsection adv_float Floating-Point Comparison

For functions producing floating-point results, use tolerance-based comparison:

@code{.c}
static void check_float_func(const DSPContext *dsp)
{
    #define WIDTH 128
    CHECKASM_ALIGN(float src  [WIDTH]);
    CHECKASM_ALIGN(float dst_c[WIDTH]);
    CHECKASM_ALIGN(float dst_a[WIDTH]);

    checkasm_declare(void, float *dst, const float *src, int len);

    if (checkasm_check_func(dsp->process_float, "process_float")) {
        checkasm_randomize_normf(src, WIDTH);

        checkasm_call_ref(dst_c, src, WIDTH);
        checkasm_call_new(dst_a, src, WIDTH);

        // Compare with ULP (Units in Last Place) tolerance
        //   Note: cannot use checkasm_check1d() here
        const int max_ulp = 1;
        checkasm_check2d(float_ulp, dst_c, 0, dst_a, 0, WIDTH, 1, "dst", max_ulp);

        // Or use absolute epsilon tolerance
        // if (!checkasm_float_near_abs_eps_array(dst_c, dst_a, 1e-6f, WIDTH)) {
        //     checkasm_fail();
        // }

        checkasm_bench_new(checkasm_alternate(dst_a, dst_c), src, WIDTH);
    }

    checkasm_report("process_float");
}
@endcode

@subsection adv_padding Padding and Over-Write Detection

Detect when functions write beyond their intended boundaries:

@code{.c}
static void check_bounds(const DSPContext *dsp)
{
    BUF_RECT(uint8_t, dst_c, 64, 64);
    BUF_RECT(uint8_t, dst_a, 64, 64);

    checkasm_declare(void, uint8_t *dst, ptrdiff_t stride, int w, int h);

    if (checkasm_check_func(dsp->fill, "fill")) {
        const int w = 64, h = 64;

        CLEAR_BUF_RECT(dst_c);
        CLEAR_BUF_RECT(dst_a);

        checkasm_call_ref(dst_c, dst_c_stride, w, h);
        checkasm_call_new(dst_a, dst_a_stride, w, h);

        // Standard check (no padding)
        checkasm_check2d(uint8_t, dst_c, dst_c_stride,
                                  dst_a, dst_a_stride, w, h, "dst");

        // Check with padding detection (detects writes outside w×h)
        checkasm_check_rect_padded(dst_c, dst_c_stride,
                                   dst_a, dst_a_stride, w, h, "dst");

        // Allow over-write up to 16-element alignment on right edge
        checkasm_check_rect_padded_align(dst_c, dst_c_stride,
                                         dst_a, dst_a_stride,
                                         w, h, "dst", 16, 1);

        checkasm_bench_new(checkasm_alternate(dst_a, dst_c), dst_a_stride, w, h);
    }

    checkasm_report("fill");
}
@endcode

@subsection bench_multiple Benchmarking Multiple Configurations

For functions that can be benchmarked at multiple configurations:

@code{.c}
if (checkasm_check_func(dsp->filter, "filter_w%d", w)) {
    for (int h = 4; h <= 64; h <<= 1) {
        // Test all heights for correctness
        checkasm_call_ref(dst_c, dst_c_stride, src, src_stride, w, h);
        checkasm_call_new(dst_a, dst_a_stride, src, src_stride, w, h);
        checkasm_check2d(uint8_t, dst_c, dst_c_stride,
                                  dst_a, dst_a_stride, w, h, "dst");

        // Benchmark each configuration
        checkasm_bench_new(checkasm_alternate(dst_a, dst_c), dst_a_stride,
                           src, src_stride, w, h);
    }

    // The framework will report the geometric mean of all benchmark runs for
    // the same checkasm_check_func() call
}

checkasm_report("filter_w%d", w);
@endcode

Alternatively, you could call checkasm_check_func() on each configuration
to get a separate benchmark report for each size, or call checkasm_bench_new()
only on the largest input size to test the limiting behavior.

@subsection adv_bitdepth Multi-Bitdepth Testing

For codecs supporting multiple bit depths:

@code{.c}
static void check_pixfunc(void)
{
    DSPContext dsp;

    #define WIDTH 64
    CHECKASM_ALIGN(uint16_t src  [WIDTH]);
    CHECKASM_ALIGN(uint16_t dst_c[WIDTH]);
    CHECKASM_ALIGN(uint16_t dst_a[WIDTH]);

    checkasm_declare(void, uint16_t *dst, const uint16_t *src, int len);

    for (int bpc = 10; bpc <= 12; bpc += 2) {
        const int bitdepth_max = (1 << bpc) - 1;
        dsp_context_init(&dsp, checkasm_get_cpu_flags(), bpc);

        if (checkasm_check_func(dsp->process, "process_%dbpc", bpc)) {
            // Randomize within valid bit depth range
            checkasm_randomize_mask16(src, WIDTH, bitdepth_max);

            checkasm_call_ref(dst_c, src, WIDTH);
            checkasm_call_new(dst_a, src, WIDTH);
            checkasm_check1d(uint16_t, dst_c, dst_a, WIDTH, "dst");
            checkasm_bench_new(checkasm_alternate(dst_a, dst_c), src, WIDTH);
        }
    }

    checkasm_report("process");
}
@endcode

Alternatively, you may prefer to compile the test file itself multiple times,
using preprocessor definitions like `-DBITDEPTH=10` etc.

@subsection adv_failure Custom Failure Reporting

Provide detailed diagnostics when tests fail:

@code{.c}
static void check_complex(const DSPContext *dsp)
{
    // ...

    if (checkasm_check_func(dsp->complex, "complex")) {
        for (int param = 0; param < 16; param++) {
            int result_c = checkasm_call_ref(param);
            int result_a = checkasm_call_new(param);

            // Check return value
            if (result_c != result_a) {
                if (checkasm_fail()) {
                    // This branch is only executed if verbose error diagnostics
                    // are requested by the user
                    fprintf(stderr, "return mismatch for param=%d: %d vs %d\n",
                            param, result_c, result_a);
                }
            }
        }
    }

    checkasm_report("complex");
}
@endcode

@subsection adv_indirect Calling Functions Through Wrappers

When the function being tested must be called indirectly through a wrapper,
you may use checkasm_call() and checkasm_call_checked() to invoke an arbitrary
helper function.

In this case, the declared function type must be the type of the wrapper, not
the inner function passed to checkasm_check_func(). You may then access the
untyped reference/tested function pointers via @ref checkasm_key_ref and
@ref checkasm_key_new "":

@code{.c}
typedef int (my_func)(int);

// Wrapper that invokes the actual function
static int sum_upto_n(my_func *func, int count)
{
    int sum = 0;
    for (int i = 0; i < count; i++)
      sum += func(i);
    return sum;
}

static void check_wrapper(void)
{
    // Declare the signature of the wrapper, not the inner function
    checkasm_declare(int, my_func *, int);

    if (checkasm_check_func(get_my_func(), "my_wrapped_func")) {
        const int count = checkasm_rand() % 100;

        // Cast checkasm_key_new to the appropriate type and pass to wrapper
        const my_func *my_ref = (my_func *) checkasm_key_ref;
        const my_func *my_new = (my_func *) checkasm_key_new;
        int sum_c = checkasm_call(sum_upto_n, my_ref, count);
        int sum_a = checkasm_call_checked(sum_upto_n, my_new, count);

        if (sum_c != sum_a)
            checkasm_fail();
    }
}
@endcode

@note When using a pattern like this, the value passed to checkasm_check_func()
may not even need to be a function pointer. It could be an arbitrary pointer
or pointer-sized integer, such as a configuration struct or index into a dispatch
table, so long as it uniquely identifies the underlying implementation being
tested.

@subsection adv_mmx MMX Functions (x86)

MMX functions on x86 often omit the `emms` instruction before returning, expecting
the caller to execute it manually after a loop. The `emms` instruction is necessary
to clear MMX state before any floating-point code can execute, but it can be very
slow, so optimized loops that call into MMX kernels usually defer it to the loop
end to minimize overhead.

Use checkasm_declare_emms() for such (non-ABI-compliant) functions:

@code{.c}
static void check_sad_mmx(const DSPContext *dsp)
{
    #define SIZE 16
    CHECKASM_ALIGN(uint8_t src [SIZE * SIZE]);
    CHECKASM_ALIGN(uint8_t ref [SIZE * SIZE]);

    // Declaring with CPU_FLAG_MMX enables an automatic emms after calling
    // this function whenever CPU_FLAG_MMX is active
    checkasm_declare_emms(CPU_FLAG_MMX, int, const uint8_t *src,
                          const uint8_t *ref, ptrdiff_t stride);

    if (checkasm_check_func(dsp->sad_16x16, "sad_16x16")) {
        INITIALIZE_BUF(src);
        INITIALIZE_BUF(ref);

        // checkasm will automatically call emms after checkasm_call_new()
        int result_c = checkasm_call_ref(src, ref, SIZE);
        int result_a = checkasm_call_new(src, ref, SIZE);

        if (result_c != result_a) {
            if (checkasm_fail()) {
                fprintf(stderr, "sad mismatch: %d vs %d\n", result_c, result_a);
            }
        }

        // checkasm will also call emms after benchmarking iterations
        checkasm_bench_new(src, ref, SIZE);
    }

    checkasm_report("sad");
}
@endcode

The first parameter is a CPU flag mask (e.g., `CPU_FLAG_MMX | CPU_FLAG_MMXEXT`).
When any of the specified CPU flags are active, checkasm will call `emms` after
each checkasm_call_new() and benchmark run. On non-x86 platforms, checkasm_declare_emms()
is equivalent to checkasm_declare().

@note Modern SIMD instruction sets (SSE and later) do not use MMX registers and
      therefore don't require `emms`. Only use checkasm_declare_emms() for legacy
      MMX-only code or MMXEXT functions that explicitly use MMX registers.

@section tips_tricks Tips and Tricks

@subsection tips_deterministic Deterministic Testing

checkasm uses a seeded PRNG for reproducible tests. To test with a specific seed:

@code{.bash}
./checkasm 12345  # Use seed 12345
@endcode

Failed tests will print the seed used, allowing you to reproduce failures:

@code{.txt}
checkasm: using random seed 987654321
...
sad_16x16: FAILED (ref:1234 new:1235)
@endcode

@subsection tips_selective Selective Testing

Test specific functions or groups:

@code{.bash}
# Test only functions matching pattern
./checkasm --function='add_*'

# Test only a specific test module
./checkasm --test=math

# Combine both
./checkasm --test=dsp --function='blend_*'
@endcode

@subsection tips_verbose Verbose Output

Enable verbose mode for detailed failure information:

@code{.bash}
./checkasm --verbose
@endcode

This shows hexdumps of differing buffer regions automatically, when using
the built-in checkasm_check*() series of buffer comparison helpers.

@subsection tips_bench Benchmarking Tips

Run benchmarks with appropriate duration:

@code{.bash}
# Quick benchmark (default)
./checkasm --bench

# Longer benchmark for more accurate results (10ms per function)
./checkasm --bench --duration=10000

# Export results in different formats
./checkasm --bench --csv  > results.csv
./checkasm --bench --json > results.json
./checkasm --bench --html > results.html
@endcode

@subsection tips_helpers Helper Macros

Create helper macros to reduce repetition in your tests:

@code{.c}
#define TEST_FILTER(name, w, h)                                       \
    if (checkasm_check_func(dsp->name, #name "_%dx%d", w, h)) {       \
        test_filter_##name(dsp, w, h);                                \
        checkasm_bench_new(dst, dst_stride, src, src_stride, w, h);   \
    }

// Usage:
TEST_FILTER(blur,    16, 16);
TEST_FILTER(blur,    32, 32);
TEST_FILTER(sharpen, 16, 16);
@endcode

---

@section tests_next_steps Next Steps

Now that you've mastered writing tests, learn how to accurately measure and
compare the performance of your optimized implementations using checkasm's
benchmarking capabilities.

**Next:** @ref benchmarking
