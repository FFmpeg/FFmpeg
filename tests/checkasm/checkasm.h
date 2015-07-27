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

#ifndef CHECKASM_H
#define CHECKASM_H

#include <stdint.h>
#include "config.h"
#include "libavutil/avstring.h"
#include "libavutil/lfg.h"
#include "libavutil/timer.h"

void checkasm_check_bswapdsp(void);
void checkasm_check_h264pred(void);
void checkasm_check_h264qpel(void);

intptr_t (*checkasm_check_func(intptr_t (*func)(), const char *name, ...))() av_printf_format(2, 3);
int checkasm_bench_func(void);
void checkasm_fail_func(const char *msg, ...) av_printf_format(1, 2);
void checkasm_update_bench(int iterations, uint64_t cycles);
void checkasm_report(const char *name, ...) av_printf_format(1, 2);

extern AVLFG checkasm_lfg;
#define rnd() av_lfg_get(&checkasm_lfg)

static av_unused intptr_t (*func_ref)();
static av_unused intptr_t (*func_new)();

#define BENCH_RUNS 1000 /* Trade-off between accuracy and speed */

/* Decide whether or not the specified function needs to be tested */
#define check_func(func, ...) ((func_new = (intptr_t (*)())func) &&\
                              (func_ref = checkasm_check_func(func_new, __VA_ARGS__)))

/* Indicate that the current test has failed */
#define fail() checkasm_fail_func("%s:%d", av_basename(__FILE__), __LINE__)

/* Print the test outcome */
#define report checkasm_report

/* Call the reference function */
#define call_ref(...) func_ref(__VA_ARGS__)

#if ARCH_X86 && HAVE_YASM
/* Verifies that clobbered callee-saved registers are properly saved and restored */
intptr_t checkasm_checked_call(intptr_t (*func)(), ...);
#endif

/* Call the function */
#if ARCH_X86_64 && HAVE_YASM
/* Evil hack: detect incorrect assumptions that 32-bit ints are zero-extended to 64-bit.
 * This is done by clobbering the stack with junk around the stack pointer and calling the
 * assembly function through x264_checkasm_call with added dummy arguments which forces all
 * real arguments to be passed on the stack and not in registers. For 32-bit arguments the
 * upper half of the 64-bit register locations on the stack will now contain junk which will
 * cause misbehaving functions to either produce incorrect output or segfault. Note that
 * even though this works extremely well in practice, it's technically not guaranteed
 * and false negatives is theoretically possible, but there can never be any false positives.
 */
void checkasm_stack_clobber(uint64_t clobber, ...);
#define CLOB (UINT64_C(0xdeadbeefdeadbeef))
#define call_new(...) (checkasm_stack_clobber(CLOB,CLOB,CLOB,CLOB,CLOB,CLOB,CLOB,CLOB,CLOB,CLOB,CLOB,\
                                              CLOB,CLOB,CLOB,CLOB,CLOB,CLOB,CLOB,CLOB,CLOB,CLOB),\
                      checkasm_checked_call(func_new, 0, 0, 0, 0, 0, __VA_ARGS__))
#elif ARCH_X86_32 && HAVE_YASM
#define call_new(...) checkasm_checked_call(func_new, __VA_ARGS__)
#else
#define call_new(...) func_new(__VA_ARGS__)
#endif

/* Benchmark the function */
#ifdef AV_READ_TIME
#define bench_new(...)\
    do {\
        if (checkasm_bench_func()) {\
            intptr_t (*tfunc)() = func_new;\
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
            checkasm_update_bench(tcount, tsum);\
        }\
    } while (0)
#else
#define bench_new(...) while(0)
#endif

#endif
