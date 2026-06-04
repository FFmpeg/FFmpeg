@mainpage Introduction

**checkasm** is a robust, portable testing and benchmarking framework specifically designed for
validating optimized assembly implementations against reference C code. Originally developed for the
x264 encoder project, checkasm has grown considerably while in use by FFmpeg and dav1d,
and now provides comprehensive correctness verification, crash detection,
and accurate performance measurements across multiple architectures.

This project in particular stems from an effort to combine and improve upon multiple diverging
checkasm forks in the wild (x264, FFmpeg, dav1d, etc.). It is a direct descendent of the dav1d
variant, with all relevant enhancements from the FFmpeg fork merged in. It has since evolved
into a fully fledged cross-platform standalone library and benchmarking framework.

@tableofcontents

@section features Key Features

@subsection features_correctness Correctness Verification
- **Automated comparison** between reference (C) and optimized (assembly) implementations, including
  support for fuzzing implementations over multiple seeds and data patterns.
- **Detection of ABI violations** such as register misuse, illegal opcodes,
  or missing state cleanup.
- **Extensive data handling utilities** for defining, initializing, and comparing
  input/output buffers of various types, sizes and dimensionalities.
- **Crash resilience** including signal handling and stack smashing detection.

@subsection features_benchmarking Performance Benchmarking
- **Accurate cycle counting** using platform-specific high-resolution timers.
- **Statistical analysis** with robust outlier rejection, logarithmic regression and confidence intervals.
- **Comparative measurements** showing speedup of optimized code including comprehensive HTML reports.
- **Minimal overhead** to ensure realistic performance data, and high throughput.

@subsection features_platform Platform Support

The following architectures are explicitly supported for asm verification, with
the listed detection mechanisms for typical asm mistakes:

- **x86, x86-64**: registers, stack, AVX2, MMX/FPU state
- **ARM, ARM64 (aarch64)**: registers, stack, VFP state
- **RISC-V**: registers, stack, vector state
- **LoongArch (32, 64)**: registers
- **PowerPC (64le)**: *none*

In addition, hardware timers are available for benchmarking purposes on all of
the listed platforms (except RISCV-V), with fall-backs to generic OS-specific APIs.

@section links Project Links

- [**GitLab Repository**](https://code.videolan.org/videolan/checkasm)
- [**Issue Tracker**](https://code.videolan.org/videolan/checkasm/issues)
- [VideoLAN Homepage](https://www.videolan.org/)

@subsection intro_next_steps Next Steps

Continue learning about checkasm through the following guides:

- @ref getting_started for a quick start guide
- @ref integration for a guide on how to integrate checkasm into your project
- @ref writing_tests for an in-depth explanation of writing tests
- @ref benchmarking for tips and tricks on benchmarking with checkasm

Or explore the full API reference:

- @ref checkasm.h -- @copybrief checkasm.h
- @ref test.h -- @copybrief test.h
- @ref utils.h -- @copybrief utils.h

@section example_outputs Example Outputs

@subsection example_benchmark Benchmark Output

This is what the output looks like, when using `--verbose` mode to print all
timing data (on a pretty noisy/busy system):

@code{.plaintext}
checkasm:
 - CPU: Intel(R) Core(TM) Ultra 7 258V (000B06D1)
 - Timing source: x86 (rdtsc)
 - Timing resolution: 0.5976 +/- 0.057 ns/cycle (1644 +/- 156.8 MHz) (provisional)
 - No-op overhead: 2.41 +/- 0.093 cycles per call (provisional)
 - Bench duration: 10000 µs per function (18221161 cycles)
 - Random seed: 3203178780
C:
 - msac.decode_symbol [OK]
 - msac.decode_bool   [OK]
 - msac.decode_hi_tok [OK]
SSE2:
 - msac.decode_symbol [OK]
 - msac.decode_bool   [OK]
 - msac.decode_hi_tok [OK]
AVX2:
 - msac.decode_symbol [OK]
checkasm: all 15 tests passed
Benchmark results:
  name                               cycles +/- stddev         time (nanoseconds) (vs ref)
  nop:                                  2.4 +/- 0.0             1.2 ns +/- 0.1
  msac_decode_bool_c:                   6.3 +/- 0.2             3.3 ns +/- 0.4
  msac_decode_bool_sse2:                5.7 +/- 0.2             3.0 ns +/- 0.4    ( 1.10x)
  msac_decode_bool_adapt_c:             5.9 +/- 0.6             3.1 ns +/- 0.5
  msac_decode_bool_adapt_sse2:          6.3 +/- 0.2             3.3 ns +/- 0.4    ( 0.94x)
  msac_decode_bool_equi_c:              5.1 +/- 0.2             2.6 ns +/- 0.3
  msac_decode_bool_equi_sse2:           2.8 +/- 0.1             1.4 ns +/- 0.2    ( 1.84x)
  msac_decode_hi_tok_c:                64.9 +/- 75.4           33.8 ns +/- 40.8
  msac_decode_hi_tok_sse2:             46.9 +/- 13.3           24.4 ns +/- 7.8    ( 1.22x)
  msac_decode_symbol_adapt4_c:         12.6 +/- 0.7             6.5 ns +/- 0.9
  msac_decode_symbol_adapt4_sse2:      12.3 +/- 0.8             6.4 ns +/- 0.9    ( 1.02x)
  msac_decode_symbol_adapt8_c:         20.5 +/- 2.3            10.7 ns +/- 1.8
  msac_decode_symbol_adapt8_sse2:      12.1 +/- 0.8             6.3 ns +/- 0.8    ( 1.68x)
  msac_decode_symbol_adapt16_c:        33.6 +/- 1.2            17.5 ns +/- 2.1
  msac_decode_symbol_adapt16_sse2:     13.8 +/- 0.6             7.2 ns +/- 0.9    ( 2.42x)
  msac_decode_symbol_adapt16_avx2:     20.6 +/- 1.2            10.7 ns +/- 1.4    ( 1.62x)
 - average timing error: 17.371% across 15 benchmarks (maximum 65.404%)
@endcode

@subsection example_errors Error Reporting

This is what various failures could look like:

@code{.plaintext}
checkasm:
 - CPU: AMD Ryzen 9 9950X3D 16-Core Processor (00B40F40)
 - Random seed: 3689286425
x86:
 - x86.copy                [OK]
FAILURE: sigill_x86 (illegal instruction)
 - x86.sigill              [FAILED]
FAILURE: corrupt_stack_x86 (stack corruption)
 - x86.corrupt_stack       [FAILED]
FAILURE: clobber_r9_x86 (failed to preserve register: r9)
FAILURE: clobber_r10_x86 (failed to preserve register: r10)
FAILURE: clobber_r11_x86 (failed to preserve register: r11)
FAILURE: clobber_r12_x86 (failed to preserve register: r12)
FAILURE: clobber_r13_x86 (failed to preserve register: r13)
FAILURE: clobber_r14_x86 (failed to preserve register: r14)
 - x86.clobber             [FAILED]
FAILURE: underwrite_64_x86 (../tests/generic.c:56)
dst data (64x1):
  0:  45 b3 dc aa 68 90 a4 5d cf d9 d9 9b d0 34     45 b3 dc aa 68 90 a4 5d cf d9 d9 9b d0 34    ..............
      1b 88 37 0f 14 f1 77 f4 94 bf 53 a3 21 3f     1b 88 37 0f 14 f1 77 f4 94 bf 53 a3 21 3f    ..............
      b5 64 77 fc 75 6b a9 2a 38 8a c7 e8 f9 1d     b5 64 77 fc 75 6b a9 2a 38 8a c7 e8 f9 1d    ..............
      b4 80 d6 f4 34 1d c6 2b fd a8 e5 83 51 bb     b4 80 d6 f4 34 1d c6 2b fd a8 e5 83 51 bb    ..............
      72 f6 e5 c1 47 80 63 f2                       72 f6 e5 c1 aa aa aa aa                      ....xxxx
 - generic.underwrite      [FAILED]
@endcode

@section license License

checkasm is licensed under the **2-clause BSD license**. See the LICENSE file in the repository for full details.
