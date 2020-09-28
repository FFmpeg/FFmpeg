/*
 * Copyright (c) 2008 Mans Rullgard <mans@mansr.com>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "config.h"

#ifdef __ELF__
#   define ELF
#else
#   define ELF #
#endif

#if HAVE_AS_FUNC
#   define FUNC
#else
#   define FUNC #
#endif

#ifndef __has_feature
#   define __has_feature(x) 0
#endif

/* Support macros for the Armv8.5-A Branch Target Identification feature which
 * requires emitting a .note.gnu.property section with the appropriate
 * architecture-dependent feature bits set.
 * Read more: "ELF for the Arm® 64-bit Architecture"
 */
#if defined(__ARM_FEATURE_BTI_DEFAULT) && (__ARM_FEATURE_BTI_DEFAULT == 1)
#   define GNU_PROPERTY_AARCH64_BTI (1 << 0)   // Has BTI
#   define AARCH64_VALID_CALL_TARGET hint #34  // BTI 'c'
#   define AARCH64_VALID_JUMP_TARGET hint #38  // BTI 'j'
#else
#   define GNU_PROPERTY_AARCH64_BTI 0          // No BTI
#   define AARCH64_VALID_CALL_TARGET
#   define AARCH64_VALID_JUMP_TARGET
#endif

#if (GNU_PROPERTY_AARCH64_BTI != 0)
        .pushsection .note.gnu.property, "a"
        .balign 8
        .long 4
        .long 0x10
        .long 0x5
        .asciz "GNU"
        .long 0xc0000000 /* GNU_PROPERTY_AARCH64_FEATURE_1_AND */
        .long 4
        .long GNU_PROPERTY_AARCH64_BTI
        .long 0
        .popsection
#endif

.macro  function name, export=0, align=2
    .macro endfunc
ELF     .size   \name, . - \name
FUNC    .endfunc
        .purgem endfunc
    .endm
        .text
        .align          \align
    .if \export
        .global EXTERN_ASM\name
ELF     .type   EXTERN_ASM\name, %function
FUNC    .func   EXTERN_ASM\name
EXTERN_ASM\name:
        AARCH64_VALID_CALL_TARGET
    .else
ELF     .type   \name, %function
FUNC    .func   \name
\name:
    .endif
.endm

.macro  const   name, align=2, relocate=0
    .macro endconst
ELF     .size   \name, . - \name
        .purgem endconst
    .endm
#if HAVE_SECTION_DATA_REL_RO
.if \relocate
        .section        .data.rel.ro
.else
        .section        .rodata
.endif
#elif defined(_WIN32)
        .section        .rdata
#elif !defined(__MACH__)
        .section        .rodata
#else
        .const_data
#endif
        .align          \align
\name:
.endm

.macro  movrel rd, val, offset=0
#if CONFIG_PIC && defined(__APPLE__)
    .if \offset < 0
        adrp            \rd, \val@PAGE
        add             \rd, \rd, \val@PAGEOFF
        sub             \rd, \rd, -(\offset)
    .else
        adrp            \rd, \val+(\offset)@PAGE
        add             \rd, \rd, \val+(\offset)@PAGEOFF
    .endif
#elif CONFIG_PIC && defined(_WIN32)
    .if \offset < 0
        adrp            \rd, \val
        add             \rd, \rd, :lo12:\val
        sub             \rd, \rd, -(\offset)
    .else
        adrp            \rd, \val+(\offset)
        add             \rd, \rd, :lo12:\val+(\offset)
    .endif
#elif CONFIG_PIC
#   if __has_feature(hwaddress_sanitizer)
        adrp            \rd, :pg_hi21_nc:\val+(\offset)
#   else
        adrp            \rd, \val+(\offset)
#   endif
        add             \rd, \rd, :lo12:\val+(\offset)
#else
        ldr             \rd, =\val+\offset
#endif
.endm

#define GLUE(a, b) a ## b
#define JOIN(a, b) GLUE(a, b)
#define X(s) JOIN(EXTERN_ASM, s)

#define x18 do_not_use_x18
#define w18 do_not_use_w18
