# checkasm - for all your asm checking needs

**checkasm** is a tool for verifying the correctness of assembly code, as well as performance benchmarking.

## Usage

For a complete guide on getting started with checkasm, see the
[Getting Started](https://checkasm.videolan.me/getting_started.html) page.

```
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
```

## Supported platforms

The following architectures are explicitly supported for asm verification, with the listed detection mechanisms for typical asm mistakes:

- **x86, x86-64**: registers, stack, AVX2, MMX/FPU state
- **ARM, ARM64 (aarch64)**: registers, stack, VFP state
- **RISC-V**: registers, stack, vector state
- **LoongArch (32, 64)**: registers
- **PowerPC (64le)**: *none*

In addition, hardware timers are available for benchmarking purposes on all of the listed platforms (except RISCV-V), with fall-backs to generic OS-specific APIs.

## Integration into your project

You can either load checkasm as a library (e.g. via `pkg-config`), or include
it directly in your project's build system. See the
[Getting Started: Installation](https://checkasm.videolan.me/getting_started.html#installation)
and [Integration Guide](https://checkasm.videolan.me/integration.html) for more information.

### Code example

Here is what short example test demonstrating the public API. Check the
[Quick Start Example](https://checkasm.videolan.me/getting_started.html#quick_start)
for a full example.

```c
static void test_add8(const CheckasmCpu cpu)
{
    #define WIDTH 1024

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
```

For a complete tutorial on writing tests, read the
[Writing Tests](https://checkasm.videolan.me/writing_tests.html) page.

### Example outputs

This is what the output looks like, when using `--verbose` mode to print all
timing data (on a pretty noisy/busy system). For more information about the
interpretation and usefulness of these results, refer to the
[Benchmarking](https://checkasm.videolan.me/benchmarking.html) guide.

```
checkasm:
 - CPU: AMD Ryzen 9 9950X3D 16-Core Processor (00B40F40)
 - Timing source: x86 (rdtsc)
 - Timing overhead: 79.3 +/- 17.01 cycles per iteration
 - Timing resolution: 0.2326 +/- 0.023 ns/cycle (4300 +/- 423.1 MHz)
 - Bench duration: 2000 µs per function (8620136 cycles)
 - Random seed: 3173025505
SSE2:
 - msac.decode_symbol [OK]
 - msac.decode_bool   [OK]
 - msac.decode_hi_tok [OK]
AVX2:
 - msac.decode_symbol [OK]
checkasm: all 8 tests passed
Benchmark results:
  name                               cycles +/- stddev         time (nanoseconds) (vs ref)
  msac_decode_bool_c:                   7.3 +/- 0.5               2 ns +/- 0
  msac_decode_bool_sse2:                5.4 +/- 0.6               1 ns +/- 0      ( 1.35x)
  msac_decode_bool_adapt_c:             7.1 +/- 0.6               2 ns +/- 0
  msac_decode_bool_adapt_sse2:          7.4 +/- 0.6               2 ns +/- 0      ( 0.97x)
  msac_decode_bool_equi_c:              5.9 +/- 0.6               1 ns +/- 0
  msac_decode_bool_equi_sse2:           4.0 +/- 0.6               1 ns +/- 0      ( 1.48x)
  msac_decode_hi_tok_c:                92.9 +/- 19.7             22 ns +/- 5
  msac_decode_hi_tok_sse2:             52.3 +/- 7.1              12 ns +/- 2      ( 1.78x)
  msac_decode_symbol_adapt4_c:         19.3 +/- 1.2               4 ns +/- 1
  msac_decode_symbol_adapt4_sse2:      12.8 +/- 0.6               3 ns +/- 0      ( 1.51x)
  msac_decode_symbol_adapt8_c:         27.6 +/- 1.1               6 ns +/- 1
  msac_decode_symbol_adapt8_sse2:      13.2 +/- 0.6               3 ns +/- 0      ( 2.10x)
  msac_decode_symbol_adapt16_c:        44.1 +/- 1.2              10 ns +/- 1
  msac_decode_symbol_adapt16_sse2:     14.6 +/- 0.6               3 ns +/- 0      ( 3.03x)
  msac_decode_symbol_adapt16_avx2:     12.8 +/- 0.6               3 ns +/- 0      ( 3.45x)
```

And this is what a failure could look like:

```
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
```

## History and authors

This project was forked from [dav1d's](https://code.videolan.org/videolan/dav1d) internal copy of checkasm, which was itself a more-or-less up-to-date version of the various checkasm versions that existed in FFmpeg, x264 and so on.

This choice was made because dav1d was the closest to being feature complete, while also being permissively licensed and using a modern CI and build system. Some changes have been ported over from FFmpeg's copy of checkasm, with permission to relicense.

checkasm's original authors include Henrik Gramner, Loren Merritt, Fiona Glaser and others. This fork is maintained by Niklas Haas.
