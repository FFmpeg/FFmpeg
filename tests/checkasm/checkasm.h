/*
 * Assembly testing and benchmarking tool
 * Copyright (c) 2015 Henrik Gramner
 * Copyright (c) 2008 Loren Merritt
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef TESTS_CHECKASM_CHECKASM_H
#define TESTS_CHECKASM_CHECKASM_H

#include <stdint.h>
#include "config.h"
#include "libavutil/avstring.h"
#include "libavutil/cpu.h"
#include "libavutil/internal.h"
#include "libavutil/lfg.h"
#include "libavutil/timer.h"

void checkasm_check_alacdsp(void);
void checkasm_check_blend(void);
void checkasm_check_bswapdsp(void);
void checkasm_check_colorspace(void);
void checkasm_check_flacdsp(void);
void checkasm_check_fmtconvert(void);
void checkasm_check_h264dsp(void);
void checkasm_check_h264pred(void);
void checkasm_check_h264qpel(void);
void checkasm_check_jpeg2000dsp(void);
void checkasm_check_pixblockdsp(void);
void checkasm_check_synth_filter(void);
void checkasm_check_v210enc(void);
void checkasm_check_vp9dsp(void);
void checkasm_check_videodsp(void);

void *checkasm_check_func(void *func, const char *name, ...) av_printf_format(2, 3);
int checkasm_bench_func(void);
void checkasm_fail_func(const char *msg, ...) av_printf_format(1, 2);
void checkasm_update_bench(int iterations, uint64_t cycles);
void checkasm_report(const char *name, ...) av_printf_format(1, 2);

/* float compare utilities */
int float_near_ulp(float a, float b, unsigned max_ulp);
int float_near_abs_eps(float a, float b, float eps);
int float_near_abs_eps_ulp(float a, float b, float eps, unsigned max_ulp);
int float_near_ulp_array(const float *a, const float *b, unsigned max_ulp,
                         unsigned len);
int float_near_abs_eps_array(const float *a, const float *b, float eps,
                             unsigned len);
int float_near_abs_eps_array_ulp(const float *a, const float *b, float eps,
                                 unsigned max_ulp, unsigned len);

extern AVLFG checkasm_lfg;
#define rnd() av_lfg_get(&checkasm_lfg)

static av_unused void *func_ref, *func_new;

#define BENCH_RUNS 1000 /* Trade-off between accuracy and speed */

/* Decide whether or not the specified function needs to be tested */
#define check_func(func, ...) (func_ref = checkasm_check_func((func_new = func), __VA_ARGS__))

/* Declare the function prototype. The first argument is the return value, the remaining
 * arguments are the function parameters. Naming parameters is optional. */
#define declare_func(ret, ...) declare_new(ret, __VA_ARGS__) typedef ret func_type(__VA_ARGS__)
#define declare_func_emms(cpu_flags, ret, ...) declare_new_emms(cpu_flags, ret, __VA_ARGS__) typedef ret func_type(__VA_ARGS__)

/* Indicate that the current test has failed */
#define fail() checkasm_fail_func("%s:%d", av_basename(__FILE__), __LINE__)

/* Print the test outcome */
#define report checkasm_report

/* Call the reference function */
#define call_ref(...) ((func_type *)func_ref)(__VA_ARGS__)

#if ARCH_X86 && HAVE_YASM
/* Verifies that clobbered callee-saved registers are properly saved and restored
 * and that either no MMX registers are touched or emms is issued */
void checkasm_checked_call(void *func, ...);
/* Verifies that clobbered callee-saved registers are properly saved and restored
 * and issues emms for asm functions which are not required to do so */
void checkasm_checked_call_emms(void *func, ...);

#if ARCH_X86_64
/* Evil hack: detect incorrect assumptions that 32-bit ints are zero-extended to 64-bit.
 * This is done by clobbering the stack with junk around the stack pointer and calling the
 * assembly function through checked_call() with added dummy arguments which forces all
 * real arguments to be passed on the stack and not in registers. For 32-bit arguments the
 * upper half of the 64-bit register locations on the stack will now contain junk which will
 * cause misbehaving functions to either produce incorrect output or segfault. Note that
 * even though this works extremely well in practice, it's technically not guaranteed
 * and false negatives is theoretically possible, but there can never be any false positives.
 */
void checkasm_stack_clobber(uint64_t clobber, ...);
#define declare_new(ret, ...) ret (*checked_call)(void *, int, int, int, int, int, __VA_ARGS__)\
                              = (void *)checkasm_checked_call;
#define declare_new_emms(cpu_flags, ret, ...) \
    ret (*checked_call)(void *, int, int, int, int, int, __VA_ARGS__) = \
        ((cpu_flags) & av_get_cpu_flags()) ? (void *)checkasm_checked_call_emms : \
                                             (void *)checkasm_checked_call;
#define CLOB (UINT64_C(0xdeadbeefdeadbeef))
#define call_new(...) (checkasm_stack_clobber(CLOB,CLOB,CLOB,CLOB,CLOB,CLOB,CLOB,CLOB,CLOB,CLOB,CLOB,\
                                              CLOB,CLOB,CLOB,CLOB,CLOB,CLOB,CLOB,CLOB,CLOB,CLOB),\
                      checked_call(func_new, 0, 0, 0, 0, 0, __VA_ARGS__))
#elif ARCH_X86_32
#define declare_new(ret, ...) ret (*checked_call)(void *, __VA_ARGS__) = (void *)checkasm_checked_call;
#define declare_new_emms(cpu_flags, ret, ...) ret (*checked_call)(void *, __VA_ARGS__) = \
        ((cpu_flags) & av_get_cpu_flags()) ? (void *)checkasm_checked_call_emms :        \
                                             (void *)checkasm_checked_call;
#define call_new(...) checked_call(func_new, __VA_ARGS__)
#endif
#elif ARCH_ARM && HAVE_ARMV5TE_EXTERNAL
/* Use a dummy argument, to offset the real parameters by 2, not only 1.
 * This makes sure that potential 8-byte-alignment of parameters is kept the same
 * even when the extra parameters have been removed. */
void checkasm_checked_call_vfp(void *func, int dummy, ...);
void checkasm_checked_call_novfp(void *func, int dummy, ...);
extern void (*checkasm_checked_call)(void *func, int dummy, ...);
#define declare_new(ret, ...) ret (*checked_call)(void *, int dummy, __VA_ARGS__) = (void *)checkasm_checked_call;
#define call_new(...) checked_call(func_new, 0, __VA_ARGS__)
#elif ARCH_AARCH64 && !defined(__APPLE__)
void checkasm_checked_call(void *func, ...);
#define declare_new(ret, ...) ret (*checked_call)(void *, __VA_ARGS__) = (void *)checkasm_checked_call;
#define call_new(...) checked_call(func_new, __VA_ARGS__)
#else
#define declare_new(ret, ...)
#define declare_new_emms(cpu_flags, ret, ...)
/* Call the function */
#define call_new(...) ((func_type *)func_new)(__VA_ARGS__)
#endif

#ifndef declare_new_emms
#define declare_new_emms(cpu_flags, ret, ...) declare_new(ret, __VA_ARGS__)
#endif

/* Benchmark the function */
#ifdef AV_READ_TIME
#define bench_new(...)\
    do {\
        if (checkasm_bench_func()) {\
            func_type *tfunc = func_new;\
            uint64_t tsum = 0;\
            int ti, tcount = 0;\
            for (ti = 0; ti < BENCH_RUNS; ti++) {\
                uint64_t t = AV_READ_TIME();\
                tfunc(__VA_ARGS__);\
                tfunc(__VA_ARGS__);\
                tfunc(__VA_ARGS__);\
                tfunc(__VA_ARGS__);\
                t = AV_READ_TIME() - t;\
                if (t*tcount <= tsum*4 && ti > 0) {\
                    tsum += t;\
                    tcount++;\
                }\
            }\
            emms_c();\
            checkasm_update_bench(tcount, tsum);\
        }\
    } while (0)
#else
#define bench_new(...) while(0)
#endif

#endif /* TESTS_CHECKASM_CHECKASM_H */
