/******************************************************************************
 * Copyright © 2025 Martin Storsjo
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
 *****************************************************************************/

#define PRIVATE_PREFIX selftest_

#include "src/checkasm_config.h"

#if ARCH_ARM

#include "src/arm/asm.S"

#ifndef __ARM_ARCH
#ifdef _M_ARM
#define __ARM_ARCH _M_ARM
#else
#define __ARM_ARCH 0
#endif
#endif

.macro v4bx rd
#if __ARM_ARCH >= 5 || defined(__ARM_ARCH_4T__)
        bx              \rd
#else
        mov             pc, \rd
#endif
.endm

function sigill_arm, export=1
        udf             #0
        v4bx            lr
endfunc

.macro clobber_r reg
function clobber_\reg, export=1
        mov             \reg, #0x42
        v4bx            lr
endfunc
.endm

.macro clobber_d reg
function clobber_\reg, export=1
        mov             r0, #0x42
        mov             r1, #0x42
        vmov            \reg, r0, r1
        v4bx            lr
endfunc
.endm

.irp r, r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11, r12
clobber_r \r
.endr

.irp r, d0, d1, d2, d3, d4, d5, d6, d7, d8, d9, d10, d11, d12, d13, d14, d15, d16, d17, d18, d19, d20, d21, d22, d23, d24, d25, d26, d27, d28, d29, d30, d31
clobber_d \r
.endr

// void clobber_stack_args_arm(int, int, int, int, int);
//
// Write into the stack of the caller, overwriting the arguments that are on
// the stack. This is legal to do.
function clobber_stack_args_arm, export=1
        mov             r0, #0x42
        str             r0, [sp, #0]
        v4bx            lr
endfunc

// void clobber_stack_arm(int, int, int, int, int);
//
// Write into the stack of the caller, writing past the arguments on the stack
// that we are allowed to touch.
function clobber_stack_arm, export=1
        mov             r0, #0x42
        str             r0, [sp, #0]
        str             r0, [sp, #4]
        v4bx            lr
endfunc

function clobber_fpscr_vfp, export=1
        // Flush to zero, default NaN mode, round towards zero
        ldr             r12, =0x3C00000
        fmxr            fpscr, r12
        v4bx            lr
endfunc

#endif
