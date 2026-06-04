/*
 * Copyright © 2018, VideoLAN and dav1d authors
 * Copyright © 2023, Nathan Egge
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
 */

#ifndef CHECKASM_RISCV_ASM_S
#define CHECKASM_RISCV_ASM_S

#if !defined(PIC)
#if defined(__PIC__)
#define PIC __PIC__
#elif defined(__pic__)
#define PIC __pic__
#endif
#endif

#ifndef PRIVATE_PREFIX
#define PRIVATE_PREFIX checkasm_
#endif

#define PASTE(a,b) a ## b
#define CONCAT(a,b) PASTE(a,b)

#define QUOTE2(x) #x
#define QUOTE(x) QUOTE2(x)

#ifdef PREFIX
#define EXTERN CONCAT(_,PRIVATE_PREFIX)
#else
#define EXTERN PRIVATE_PREFIX
#endif

.macro arch ext:req, more:vararg
    .option arch, +\ext
    .ifnb \more
        arch \more
    .endif
.endm

.macro function name, export=0, ext=, visible=0
    .macro endfunc
#ifdef __ELF__
        .size   \name, . - \name
#endif
        .option pop
        .purgem endfunc
    .endm
#if defined(_WIN32) && defined(BUILDING_DLL)
    .if \export && \visible
        .section .drectve
        .ascii "-export:"
        .ascii QUOTE(EXTERN)
        .ascii "\name "
    .endif
#endif
        .text
        .option push
    .ifnb \ext
        arch \ext
    .endif
    .if \export
        .global EXTERN\name
#ifdef __ELF__
        .type   EXTERN\name, %function
        .if !\visible
            .hidden EXTERN\name
        .endif
#elif defined(__MACH__)
        .if !\visible
            .private_extern EXTERN\name
        .endif
#endif
EXTERN\name:
    .else
#ifdef __ELF__
        .type \name, %function
#endif
    .endif
\name:
.endm

.macro  const   name, export=0, align=2
    .macro endconst
#ifdef __ELF__
        .size   \name, . - \name
#endif
        .purgem endconst
    .endm
#if defined(_WIN32)
        .section        .rdata
#elif !defined(__MACH__)
        .section        .rodata
#else
        .const_data
#endif
        .align          \align
    .if \export
        .global EXTERN\name
#ifdef __ELF__
        .hidden EXTERN\name
#elif defined(__MACH__)
        .private_extern EXTERN\name
#endif
EXTERN\name:
    .endif
\name:
.endm

.macro thread_local name, align=3, quads=1
    .macro end_thread_local
        .size  \name, . - \name
        .purgem end_thread_local
    .endm
        .section .tbss, "waT"
        .align \align
        .hidden \name
\name:
    .rept \quads
        .quad 0
    .endr
        end_thread_local
.endm

.macro data name, align=2, words=1
    .macro end_data
        .size  \name, . - \name
        .purgem end_data
    .endm
        .section .data
        .align \align
        .hidden \name
\name:
    .rept \words
        .word 0
    .endr
        end_data
.endm

#ifndef __riscv_zicfilp
.macro  lpad    lpl
        auipc   zero, \lpl
.endm
#endif

#define L(x) .L ## x

#define MA      (1 << 7)
#define TA      (1 << 6)
#define E8      (0 << 3)
#define E16     (1 << 3)
#define E32     (2 << 3)
#define E64     (3 << 3)
#define M1      0
#define M2      1
#define M4      2
#define M8      3
#define MF2     7
#define MF4     6
#define MF8     5

#endif /* CHECKASM_RISCV_ASM_S */
