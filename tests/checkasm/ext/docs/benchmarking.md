@page benchmarking Benchmarking

This guide explains how to use checkasm's benchmarking capabilities to accurately measure
and compare the performance of optimized assembly implementations against reference code.

@tableofcontents

@section bench_basic Basics

@subsection bench_workflow Benchmark Workflow

Benchmarking in checkasm follows the same structure as correctness testing, with
an additional call to checkasm_bench_new():

@code{.c}
BUF_RECT(uint8_t, src,   64, 64);
BUF_RECT(uint8_t, dst_c, 64, 64);
BUF_RECT(uint8_t, dst_a, 64, 64);

checkasm_declare(void, uint8_t *dst, ptrdiff_t dst_stride,
                       const uint8_t *src, ptrdiff_t src_stride,
                       int w, int h);

if (checkasm_check_func(dsp->filter, "filter_64x64")) {
    INITIALIZE_BUF_RECT(src);
    CLEAR_BUF_RECT(dst_c);
    CLEAR_BUF_RECT(dst_a);

    // Correctness testing
    checkasm_call_ref(dst_c, dst_c_stride, src, src_stride, 64, 64);
    checkasm_call_new(dst_a, dst_a_stride, src, src_stride, 64, 64);
    checkasm_check_rect_padded(dst_c, dst_c_stride,
                               dst_a, dst_a_stride, 64, 64, "dst");

    // Benchmarking
    checkasm_bench_new(checkasm_alternate(dst_a, dst_c), dst_a_stride,
                       src, src_stride, 64, 64);
}

checkasm_report("filter");
@endcode

@subsection bench_cli Running Benchmarks

Enable benchmarking with the `--bench` flag:

@code{.bash}
# Quick benchmark (uses default duration)
./checkasm --bench

# Longer benchmark for more accurate results (10ms per function)
./checkasm --bench --duration=10000

# Benchmark specific functions only
./checkasm --bench --function='filter_*'

# Verbose output showing all timing measurements
./checkasm --bench --verbose
@endcode

The `--duration` parameter controls how long (in microseconds) each function is
benchmarked. Longer durations provide more accurate results but take more time.
The default is typically sufficient for most cases.

@subsection bench_export Exporting Results

checkasm can export benchmark results in multiple formats:

@code{.bash}
# CSV format (suitable for spreadsheets)
./checkasm --bench --csv > results.csv           # without column headers
./checkasm --bench --csv --verbose > results.csv # with column headers

# JSON format (for programmatic analysis, includes all data)
./checkasm --bench --json > results.json

# HTML format (interactive visualizations)
./checkasm --bench --html > results.html
@endcode

The JSON output format includes all measurement data and detailed statistical
parameters, including kernel density estimates, regression parameters, and confidence
intervals. The HTML output displays this same data in the form of interactive charts.

@section bench_methodology Statistical Methodology

@subsection bench_lognormal Log-Normal Distribution Modeling

checkasm models execution time as a log-normal distribution, which is well-suited
for performance measurements because:

1. Execution time is always positive
2. Performance variations tend to be multiplicative (e.g. power states) rather than additive
3. Outliers (e.g., from cache misses or interrupts) naturally fall into the long tail

The statistical estimator tracks two parameters:
- **Log mean** (μ): the logarithm of the median execution time
- **Log variance** (σ²): the variance of log(execution time)

From these, checkasm computes:
- **Mode**: most likely execution time = exp(μ - σ²)
- **Median**: middle execution time = exp(μ)
- **Mean**: average execution time = exp(μ + σ²/2)
- **Standard deviation**: sqrt(exp(2μ + σ²) × (exp(σ²) - 1))
- **Upper/Lower 95% confidence intervals**: exp(μ ± 1.96 × σ)

@subsection bench_regression Linear Regression

checkasm performs linear regression in log-space on the relationship between
iteration count and total execution time:

@code{.plaintext}
log(per_call_time) = log(total_time) - log(iterations)
@endcode

This approach:
- Automatically handles the multiplicative nature of timing variations
- Provides robust outlier rejection through regression residuals
- Separates per-call time from measurement overhead
- Computes confidence intervals for the estimates

@subsection bench_geometric Geometric Mean for Multiple Runs

When checkasm_bench_new() is called multiple times for the same function
(e.g., testing different block sizes), the final reported value is the
**geometric mean** of all measurements:

@code{.plaintext}
geometric_mean = (x₁ × x₂ × ... × xₙ)^(1/n)
@endcode

The geometric mean is appropriate for performance measurements because it:
- Is not skewed by outliers as heavily as arithmetic mean
- Properly handles ratios and speedups across multiple orders of magnitude
- Provides a representative "typical" performance across configurations

@subsection bench_overhead Overhead Correction

checkasm measures and subtracts the overhead of:
1. The benchmarking loop itself
2. The function call mechanism
3. The timer read operation

This is done by measuring a no-op function and subtracting its measured time
from all benchmark results. The no-op overhead is reported at startup:

@code{.plaintext}
 - No-op overhead: 2.41 +/- 0.093 cycles per call (provisional)
@endcode

For accuracy, this is re-estimated periodically throughout the benchmarking
process to account for any drift. The final value is reported again at the end
if `--verbose` is enabled.

@section bench_best_practices Best Practices

@subsection bp_system_state System State

Despite all of the statistical techniques employed by checkasm to combat
short- and medium-term noise, there is an unavoidable dependence on long-term
changes in system state. For reliable benchmarking, consider:

**Power Management:**
- Disable CPU frequency scaling if possible:
  @code{.bash}
  # Linux: set CPU governor to performance mode
  sudo cpupower frequency-set --governor performance

  # Or for all CPUs
  for cpu in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
      echo performance | sudo tee $cpu
  done
  @endcode

- Disable turbo boost for consistent results:
  @code{.bash}
  # Intel
  echo 1 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo

  # AMD
  echo 0 | sudo tee /sys/devices/system/cpu/cpufreq/boost
  @endcode

**System Load:**
- Close unnecessary applications
- Avoid running benchmarks on heavily loaded systems
- Consider using `nice` to prioritize the benchmark process:
  @code{.bash}
  nice -n -20 ./checkasm --bench
  @endcode

**Thermal Throttling:**
- Ensure adequate cooling to prevent thermal throttling
- Allow sufficient cool-down time between benchmark runs
- Monitor CPU temperature during long benchmark sessions

@subsection bp_alignment Cache Alignment

checkasm automatically warms up caches before taking measurements, but you
should be aware of cache effects:

- **L1/L2 Cache**: Most optimized functions should fit in L2 cache
- **Data Working Set**: Buffers allocated with BUF_RECT() are properly aligned
  and sized to avoid cache conflicts. Failure to do so may lead to performance
  hits from unaligned memory accesses or cache thrashing.

For functions with large working sets that don't fit in cache, benchmark results
may reflect cache miss behavior, which is often realistic for real-world usage.

@subsection bp_alternating Buffer Alternation

Use checkasm_alternate() when benchmarking to prevent cache pollution:

@code{.c}
// Good: alternates between dst_a and dst_c to prevent cache hits from
// previous iterations
checkasm_bench_new(checkasm_alternate(dst_a, dst_c), dst_a_stride,
                   src, src_stride, w, h);

// Acceptable: always writes to dst_a
checkasm_bench_new(dst_a, dst_a_stride, src, src_stride, w, h);
@endcode

Alternating buffers ensures that benchmarks are not stalled by previous
access to the same data buffer from the prior loop iteration.

@subsection bp_realistic Realistic Test Data

Use realistic input data for benchmarks:

@code{.c}
// For general data processing: use INITIALIZE_BUF() which includes common edge cases
INITIALIZE_BUF_RECT(src);

// For specific patterns: use domain-appropriate data
checkasm_randomize_normf(audio_buf, len);  // Audio: normal distribution

// For worst-case analysis: test pathological inputs
generate_worst_case_pattern(buf, len);
@endcode

The input data can significantly affect performance due to:
- Data-dependent branches in the implementation
- SIMD instruction efficiency varying with data patterns
- Cache behavior depending on data values

If your function's performance varies significantly with the input data or
configuration, consider looping over all such configurations and running
checkasm_bench_new() for each, to measure an overall average.

@subsection bp_configurations Choosing Configurations

When benchmarking functions that support multiple sizes or configurations:

**Option 1: Benchmark all configurations**
@code{.c}
for (int w = 4; w <= 128; w <<= 1) {
    if (checkasm_check_func(dsp->filter, "filter_w%d", w)) {
        for (int h = 4; h <= 128; h <<= 1) {
            // Test for correctness
            // ...

            // Benchmark each configuration
            checkasm_bench_new(dst, dst_stride, src, src_stride, w, h);
        }
        // Reports geometric mean of all configurations
    }
}
@endcode

**Option 2: Benchmark representative sizes separately**
@code{.c}
const int sizes[][2] = { {16, 16}, {16, 32}, {64, 16}, {64, 32} };

for (int i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
    int w = sizes[i][0], h = sizes[i][1];

    // Separate check_func call = separate benchmark report
    if (checkasm_check_func(dsp->filter, "filter_%dx%d", w, h)) {
        // Test for correctness
        // ...

        checkasm_bench_new(dst, dst_stride, src, src_stride, w, h);
    }
}
@endcode

**Option 3: Benchmark only the limiting case**
@code{.c}
for (int w = 4; w <= 128; w <<= 1) {
    if (checkasm_check_func(dsp->filter, "filter_w%d", w)) {
        for (int h = 4; h <= 128; h <<= 1) {
            // Test all for correctness
            // ...
        }

        // Benchmark only the largest size
        checkasm_bench_new(dst, dst_stride, src, src_stride, w, 128);
    }
}
@endcode

Choose based on your needs:
- Option 1: General performance across all sizes
- Option 2: Specific performance for important sizes
- Option 3: Best-case or worst-case performance

@section bench_interpreting Interpreting Results

@subsection interp_output Understanding Output

checkasm's benchmark output provides several pieces of information. Pass
`--verbose` to see all timing measurements.

@code{.plaintext}
Benchmark results:
  name                         cycles +/- stddev         time (nanoseconds) (vs ref)
  nop:                            2.4 +/- 0.0             1.2 ns +/- 0.1
  filter_c:                      64.9 +/- 75.4           33.8 ns +/- 40.8
  filter_sse2:                   46.9 +/- 13.3           24.4 ns +/- 7.8    ( 1.22x)
  filter_avx2:                   20.6 +/- 1.2            10.7 ns +/- 1.4    ( 3.15x)
@endcode

**Columns:**
- **name**: Function name (with ISA suffix for optimized versions)
- **cycles**: Estimated CPU cycles per call (mean ± standard deviation)
- **time**: Estimated nanoseconds per call (mean ± standard deviation)
- **(vs ref)**: Speedup relative to the reference (C) implementation

**What to look for:**
- Lower cycle counts indicate better performance
- Standard deviation shows measurement reliability
- Speedup factors show optimization effectiveness

@subsection interp_variance High Variance

High standard deviation (large ± values) can indicate:

1. **System noise**: Background processes, interrupts, frequency scaling
   - Solution: Follow best practices in @ref bp_system_state

2. **Data-dependent performance**: Function runs faster/slower on different inputs
   - This may be legitimate behavior (e.g., early exit conditions)
   - Consider whether benchmark input is representative

3. **Cache effects**: Function doesn't fit in cache or has cache conflicts
   - May be realistic for large working sets
   - Ensure buffers are properly aligned

The benchmark summary reports average timing error:
@code{.plaintext}
 - average timing error: 17.371% across 15 benchmarks (maximum 65.404%)
@endcode

High maximum error typically indicates at least one very noisy measurement.

@subsection interp_comparison Comparing Implementations

When comparing optimized implementations:

**Absolute speedup:**
@code{.plaintext}
filter_avx2: 20.6 cycles    ( 3.15x)
@endcode
This implementation is 3.15× faster than the C reference.

**Relative comparison:**
@code{.plaintext}
filter_sse2: 46.9 cycles    ( 1.22x)
filter_avx2: 20.6 cycles    ( 3.15x)
@endcode
AVX2 is 46.9 / 20.6 = 2.28× faster than SSE2.

@subsection interp_regression Regression Detection

Use benchmark results to detect performance regressions:

1. **Baseline measurements**: Save benchmark results for your codebase:
   @code{.bash}
   ./checkasm --bench --json > baseline.json
   @endcode

2. **After changes**: Run benchmarks again:
   @code{.bash}
   ./checkasm --bench --json > current.json
   @endcode

3. **Compare**: Look for functions that got slower
   - Small variations (< 5%) are typically noise
   - Changes > 10% warrant investigation
   - Changes > 20% are likely real regressions or improvements

@section bench_advanced Advanced Topics

@subsection adv_microbench Microbenchmarking Pitfalls

Be aware of common microbenchmarking issues:

- **Dead Code Elimination:** If the optimized function's results aren't used, the compiler might optimize
  it away, especially when compiling with link time optimization. This would
  usually be seen as unrealistically low cycle counts.
- **Constant Folding:** Always use INITIALIZE_BUF() or RANDOMIZE_BUF() to ensure inputs aren't
  compile-time constants that could be folded away.
- **Branch Prediction:** Running the same code path repeatedly (as benchmarks do) leads to perfect
  branch prediction, which may not reflect real-world performance that involve
  mixed function calls (e.g. varying block sizes). This is generally acceptable
  since you're comparing implementations under the same conditions, but may hide
  performance gains from e.g. branchless implementations.
- **Memory Hierarchy:** Benchmarks often measure L1/L2 cache performance, not DRAM performance.
  For functions with large working sets, real-world performance may be lower
  than benchmarks suggest, which puts a bound on the realistically achievable
  speedup from SIMD optimizations.

@subsection adv_platform Platform Considerations

@subsubsection adv_timer Timer Resolution
checkasm reports timer resolution at startup:
@code{.plaintext}
 - Timing source: x86 (rdtsc)
 - Timing resolution: 0.5976 +/- 0.057 ns/cycle (1644 +/- 156.8 MHz) (provisional)
@endcode

- x86/x86_64: rdtsc (cycle counter) - very high resolution
- ARM/AArch64: pmccntr (cycle counter) - high resolution
- LoongArch: rdtime (tick counter) - high resolution
- PowerPC 64le: mfspr (tick counter) - medium resolution
- Other/Fallback: OS-provided timers - lower resolution

Lower resolution timers may require longer `--duration` for accurate results.

@subsubsection adv_freq_scaling Frequency Scaling
The timer resolution includes clock frequency estimation. If CPU frequency
scaling is enabled, this estimate may be inaccurate. However, this affects
only the conversion to nanoseconds, not cycle counts. For most accurate results,
disable frequency scaling, or compare only raw cycle counts (for platforms with
access to high-resolution cycle counters).

@subsubsection adv_cross_platform Cross-Platform Comparison
Comparing cycle counts across different CPUs is meaningful when:
- Both CPUs are from the same architecture family
- Both run at similar clock speeds
- You account for microarchitectural differences

For cross-platform comparison, use relative speedup (optimized vs C) rather
than absolute cycle counts.

@subsection adv_html HTML Report Overview

The HTML report provides detailed statistical visualizations:

@subsubsection adv_kde_regression Kernel Density Estimate (left chart)
- Shows the probability distribution of execution times
- Peak indicates most likely execution time (mode)
- Wider distribution = higher variance
- Derived from log-normal distribution fit

@subsubsection adv_raw_measurements Raw Measurements (right chart)
- X-axis: iteration count (how many times function was called in one measurement)
- Y-axis: total time for all iterations in one measurement
- Line: linear regression fit
- Shaded area: 95% confidence interval
- Points far from line: potential outliers

@subsubsection adv_metrics Metrics Table
- **Adjusted cycles/time**: After overhead subtraction (use this for comparisons)
- **Raw cycles/time**: Before overhead subtraction (may be more reliable for sub-10-cycle functions)

@section bench_tips Tips and Tricks

@subsection tips_reproducible Reproducible Benchmarks

For reproducible results:

1. **Use fixed random seed**:
   @code{.bash}
   ./checkasm --bench 12345  # Use seed 12345
   @endcode
   This ensures the same test data patterns across runs.

2. **Document system state**:
   - CPU model and frequency settings
   - Compiler version and flags
   - Operating system and kernel version
   - checkasm version/commit

3. **Multiple runs**:
   Run benchmarks multiple times and verify consistency:
   @code{.bash}
   for i in {1..5}; do
       ./checkasm --bench --function='filter_*' | tee run_$i.txt
   done
   @endcode
