/******************************************************************************
 * Copyright © 2018, VideoLAN and dav1d authors
 * Copyright © 2015 Martin Storsjo
 * Copyright © 2015 Janne Grunau
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

#include "checkasm_config.h"

#if ARCH_AARCH64

#include "arm/asm.S"

const register_init, align=4
        .quad 0x21f86d66c8ca00ce
        .quad 0x75b6ba21077c48ad
        .quad 0xed56bb2dcb3c7736
        .quad 0x8bda43d3fd1a7e06
        .quad 0xb64a9c9e5d318408
        .quad 0xdf9a54b303f1d3a3
        .quad 0x4a75479abd64e097
        .quad 0x249214109d5d1c88
        .quad 0x1a1b2550a612b48c
        .quad 0x79445c159ce79064
        .quad 0x2eed899d5a28ddcd
        .quad 0x86b2536fcd8cf636
        .quad 0xb0856806085e7943
        .quad 0x3f2bf84fc0fcca4e
        .quad 0xacbd382dcf5b8de2
        .quad 0xd229e1f5b281303f
        .quad 0x71aeaff20b095fd9
        .quad 0xab63e2e11fa38ed9
endconst


const error_message_register
        .asciz "failed to preserve register"
error_message_stack:
        .asciz "stack clobbered"
endconst

.macro  movrel rd, val, offset=0
#if defined(__APPLE__)
  .if \offset < 0
        adrp            \rd, \val@PAGE
        add             \rd, \rd, \val@PAGEOFF
        sub             \rd, \rd, -(\offset)
  .else
        adrp            \rd, \val+(\offset)@PAGE
        add             \rd, \rd, \val+(\offset)@PAGEOFF
  .endif
#elif defined(PIC) && defined(_WIN32)
  .if \offset < 0
        adrp            \rd, \val
        add             \rd, \rd, :lo12:\val
        sub             \rd, \rd, -(\offset)
  .else
        adrp            \rd, \val+(\offset)
        add             \rd, \rd, :lo12:\val+(\offset)
  .endif
#elif __has_feature(hwaddress_sanitizer)
        adrp            \rd, :pg_hi21_nc:\val+(\offset)
        movk            \rd, #:prel_g3:\val+0x100000000
        add             \rd, \rd, :lo12:\val+(\offset)
#elif defined(PIC)
        adrp            \rd, \val+(\offset)
        add             \rd, \rd, :lo12:\val+(\offset)
#else
        ldr             \rd, =\val+\offset
#endif
.endm

// max number of args used by any asm function.
#define MAX_ARGS 15

// + 16 for stack canary reference
#define ARG_STACK (((8*(MAX_ARGS - 8) + 15) & ~15) + 16)

function checked_call, export=1, visible=1
        sub             sp,  sp,  #160
        stp             x29, x30, [sp, #144]
        add             x29, sp,  #144
        stp             x19, x20, [sp, #128]
        stp             x21, x22, [sp, #112]
        stp             x23, x24, [sp, #96]
        stp             x25, x26, [sp, #80]
        stp             x27, x28, [sp, #64]
        stp             d8,  d9,  [sp, #48]
        stp             d10, d11, [sp, #32]
        stp             d12, d13, [sp, #16]
        stp             d14, d15, [sp]

        movrel          x9, register_init
        ldp             d8,  d9,  [x9], #16
        ldp             d10, d11, [x9], #16
        ldp             d12, d13, [x9], #16
        ldp             d14, d15, [x9], #16
        ldp             x19, x20, [x9], #16
        ldp             x21, x22, [x9], #16
        ldp             x23, x24, [x9], #16
        ldp             x25, x26, [x9], #16
        ldp             x27, x28, [x9], #16

        sub             sp,  sp,  #ARG_STACK
.equ pos, 0
.rept MAX_ARGS-8
        // Skip the first 8 args, that are loaded into registers
        ldr             x9, [x29, #16 + pos]
        str             x9, [sp, #pos]
.equ pos, pos + 8
.endr

        // For stack overflows, the callee is free to overwrite the parameters
        // that were passed on the stack (if any), so we can only check after
        // that point. First figure out how many parameters the function
        // really took on the stack:
        ldr             w16, [x29, #16 + (MAX_ARGS-8)*8]
        // Calculate the number of 8 byte stack slots used for arguments
        sub             w17, w16, #8
        // if (w17 < 0) w17 = 0;
        bic             w17, w17, w17, asr #31
        // Load the first non-parameter value from the stack, that should be
        // left untouched by the function. Store a copy of it inverted, so that
        // e.g. overwriting everything with zero would be noticed.
        ldr             x17, [sp, x17, lsl #3]
        mvn             x17, x17
        str             x17, [sp, #ARG_STACK-8]
        // Calculate the location of the target function pointer
        add             x17, x29, #16 + (MAX_ARGS-8)*8 + 8
        add             x17, x17, x16, lsl #3
        // Load the target function pointer
        ldr             x16, [x17]
        // Load the clobber mask
        ldr             w17, [x17, #8]

        // Clobber the upper half of register parameters, for 32 bit parameters
        // indicated by clobber_mask
        mov             x8, #0xbeef00000000
        movk            x8, #0xdead, lsl #48
.macro clobber_upper reg, bit
        tst             w17, #(1 << \bit)
        csel            x9, xzr, x8, eq
        orr             \reg, \reg, x9
.endm
.irpc n, 01234567
        clobber_upper   x\n, \n
.endr
#ifndef __APPLE__
        // Also clobber the upper half of 32 bit parameters on the stack,
        // on non-Apple platforms, according to clobber_mask
        mov             w10, #(1 << 8)
        lsr             x8, x8, #32 // Shift the clobber pattern down to the low end
.equ pos, 0
.rept MAX_ARGS-8
        tst             w17, w10
        b.eq            1f
        str             w8, [sp, #pos + 4]
1:
        lsl             w10, w10, #1
.equ pos, pos + 8
.endr
#endif

        // Fill x8-x15,x17 with garbage. This doesn't have to be preserved,
        // but avoids relying on them having any particular value.
        movrel          x17, register_init
        ldp             x8,  x9,  [x17], #32
        ldp             x10, x11, [x17], #32
        ldp             x12, x13, [x17], #32
        ldp             x14, x15, [x17], #32
        ldr             x17,      [x17]

        // Call the target function
        blr             x16

        // Load the number of stack parameters, stack canary and its reference
        ldr             w16, [x29, #16 + (MAX_ARGS-8)*8]
        sub             w17, w16, #8
        bic             w17, w17, w17, asr #31
        ldr             x16, [sp, x17, lsl #3]
        ldr             x17, [sp, #ARG_STACK-8]
        mvn             x17, x17

        add             sp,  sp,  #ARG_STACK
        // Back up the return value
        stp             x0,  x1,  [sp, #-16]!

        cmp             x16, x17
        b.ne            2f

        movrel          x9, register_init
        movi            v3.8h,  #0

.macro check_reg_neon reg1, reg2
        ldr             q1,  [x9], #16
        uzp1            v2.2d,  v\reg1\().2d, v\reg2\().2d
        eor             v1.16b, v1.16b, v2.16b
        orr             v3.16b, v3.16b, v1.16b
.endm
        check_reg_neon  8,  9
        check_reg_neon  10, 11
        check_reg_neon  12, 13
        check_reg_neon  14, 15
        uqxtn           v3.8b,  v3.8h
        umov            x3,  v3.d[0]

.macro check_reg reg1, reg2
        ldp             x0,  x1,  [x9], #16
        eor             x0,  x0,  \reg1
        eor             x1,  x1,  \reg2
        orr             x3,  x3,  x0
        orr             x3,  x3,  x1
.endm
        check_reg       x19, x20
        check_reg       x21, x22
        check_reg       x23, x24
        check_reg       x25, x26
        check_reg       x27, x28

        cbz             x3,  0f

        movrel          x0, error_message_register
        b               1f
2:
        movrel          x0, error_message_stack
1:
        b               X(fail_abort)
0:
        ldp             x0,  x1,  [sp]
        ldp             d14, d15, [sp, #16]
        ldp             d12, d13, [sp, #32]
        ldp             d10, d11, [sp, #48]
        ldp             d8,  d9,  [sp, #64]
        ldp             x27, x28, [sp, #80]
        ldp             x25, x26, [sp, #96]
        ldp             x23, x24, [sp, #112]
        ldp             x21, x22, [sp, #128]
        ldp             x19, x20, [sp, #144]
        ldp             x29, x30, [sp, #160]
        add             sp,  sp,  #176
        ret
endfunc

#if HAVE_SVE
ENABLE_SVE
function sve_length, export=1
        cntb            x0
        lsl             x0,  x0,  #3
        ret
endfunc
DISABLE_SVE
#endif

#if HAVE_SME
ENABLE_SME
function sme_length, export=1
        smstart
        cntb            x0
        lsl             x0,  x0,  #3
        smstop
        ret
endfunc
DISABLE_SME
#endif

#endif
