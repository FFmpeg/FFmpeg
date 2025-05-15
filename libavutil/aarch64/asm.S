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

#if HAVE_AS_ARCH_DIRECTIVE
        .arch           AS_ARCH_LEVEL
#endif

#if HAVE_AS_ARCHEXT_DOTPROD_DIRECTIVE
#define ENABLE_DOTPROD  .arch_extension dotprod
#define DISABLE_DOTPROD .arch_extension nodotprod
#else
#define ENABLE_DOTPROD
#define DISABLE_DOTPROD
#endif

#if HAVE_AS_ARCHEXT_I8MM_DIRECTIVE
#define ENABLE_I8MM  .arch_extension i8mm
#define DISABLE_I8MM .arch_extension noi8mm
#else
#define ENABLE_I8MM
#define DISABLE_I8MM
#endif

#if HAVE_AS_ARCHEXT_SVE_DIRECTIVE
#define ENABLE_SVE  .arch_extension sve
#define DISABLE_SVE .arch_extension nosve
#else
#define ENABLE_SVE
#define DISABLE_SVE
#endif

#if HAVE_AS_ARCHEXT_SVE2_DIRECTIVE
#define ENABLE_SVE2  .arch_extension sve2
#define DISABLE_SVE2 .arch_extension nosve2
#else
#define ENABLE_SVE2
#define DISABLE_SVE2
#endif

DISABLE_DOTPROD
DISABLE_I8MM
DISABLE_SVE
DISABLE_SVE2


/* Support macros for
 *   - Armv8.3-A Pointer Authentication and
 *   - Armv8.5-A Branch Target Identification
 * features which require emitting a .note.gnu.property section with the
 * appropriate architecture-dependent feature bits set.
 *
 * |AARCH64_SIGN_LINK_REGISTER| and |AARCH64_VALIDATE_LINK_REGISTER| expand to
 * PACIxSP and AUTIxSP, respectively. |AARCH64_SIGN_LINK_REGISTER| should be
 * used immediately before saving the LR register (x30) to the stack.
 * |AARCH64_VALIDATE_LINK_REGISTER| should be used immediately after restoring
 * it. Note |AARCH64_SIGN_LINK_REGISTER|'s modifications to LR must be undone
 * with |AARCH64_VALIDATE_LINK_REGISTER| before RET. The SP register must also
 * have the same value at the two points. For example:
 *
 *   .global f
 *   f:
 *     AARCH64_SIGN_LINK_REGISTER
 *     stp x29, x30, [sp, #-96]!
 *     mov x29, sp
 *     ...
 *     ldp x29, x30, [sp], #96
 *     AARCH64_VALIDATE_LINK_REGISTER
 *     ret
 *
 * |AARCH64_VALID_CALL_TARGET| expands to BTI 'c'. Either it, or
 * |AARCH64_SIGN_LINK_REGISTER|, must be used at every point that may be an
 * indirect call target. In particular, all symbols exported from a file must
 * begin with one of these macros. For example, a leaf function that does not
 * save LR can instead use |AARCH64_VALID_CALL_TARGET|:
 *
 *   .globl return_zero
 *   return_zero:
 *     AARCH64_VALID_CALL_TARGET
 *     mov x0, #0
 *     ret
 *
 * A non-leaf function which does not immediately save LR may need both macros
 * because |AARCH64_SIGN_LINK_REGISTER| appears late. For example, the function
 * may jump to an alternate implementation before setting up the stack:
 *
 *   .globl with_early_jump
 *   with_early_jump:
 *     AARCH64_VALID_CALL_TARGET
 *     cmp x0, #128
 *     b.lt .Lwith_early_jump_128
 *     AARCH64_SIGN_LINK_REGISTER
 *     stp x29, x30, [sp, #-96]!
 *     mov x29, sp
 *     ...
 *     ldp x29, x30, [sp], #96
 *     AARCH64_VALIDATE_LINK_REGISTER
 *     ret
 *
 *  .Lwith_early_jump_128:
 *     ...
 *     ret
 *
 * These annotations are only required with indirect calls. Private symbols that
 * are only the target of direct calls do not require annotations. Also note
 * that |AARCH64_VALID_CALL_TARGET| is only valid for indirect calls (BLR), not
 * indirect jumps (BR). Indirect jumps in assembly are supported through
 * |AARCH64_VALID_JUMP_TARGET|. Landing Pads which shall serve for jumps and
 * calls can be created using |AARCH64_VALID_JUMP_CALL_TARGET|.
 *
 * Although not necessary, it is safe to use these macros in 32-bit ARM
 * assembly. This may be used to simplify dual 32-bit and 64-bit files.
 *
 * References:
 * - "ELF for the Arm® 64-bit Architecture"
 *   https: *github.com/ARM-software/abi-aa/blob/master/aaelf64/aaelf64.rst
 * - "Providing protection for complex software"
 *   https://developer.arm.com/architectures/learn-the-architecture/providing-protection-for-complex-software
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

#if defined(__ARM_FEATURE_PAC_DEFAULT)
#   if ((__ARM_FEATURE_PAC_DEFAULT & (1 << 0)) != 0) // authentication using key A
#       define AARCH64_SIGN_LINK_REGISTER      paciasp
#       define AARCH64_VALIDATE_LINK_REGISTER  autiasp
#   elif ((__ARM_FEATURE_PAC_DEFAULT & (1 << 1)) != 0) // authentication using key B
#       define AARCH64_SIGN_LINK_REGISTER      pacibsp
#       define AARCH64_VALIDATE_LINK_REGISTER  autibsp
#   else
#       error Pointer authentication defines no valid key!
#   endif
#   if ((__ARM_FEATURE_PAC_DEFAULT & (1 << 2)) != 0)
#       error Authentication of leaf functions is enabled but not supported in FFmpeg!
#   endif
#   define GNU_PROPERTY_AARCH64_PAC (1 << 1)
#else
#   define GNU_PROPERTY_AARCH64_PAC 0
#   define AARCH64_SIGN_LINK_REGISTER
#   define AARCH64_VALIDATE_LINK_REGISTER
#endif


#if (GNU_PROPERTY_AARCH64_BTI != 0 || GNU_PROPERTY_AARCH64_PAC != 0) && defined(__ELF__)
        .pushsection .note.gnu.property, "a"
        .balign 8
        .long 4
        .long 0x10
        .long 0x5
        .asciz "GNU"
        .long 0xc0000000 /* GNU_PROPERTY_AARCH64_FEATURE_1_AND */
        .long 4
        .long (GNU_PROPERTY_AARCH64_BTI | GNU_PROPERTY_AARCH64_PAC)
        .long 0
        .popsection
#endif

.macro  function name, export=0, align=4
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

.macro  const   name, align=4, relocate=0
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
