/*
 * Copyright © 2018, VideoLAN and dav1d authors
 * Copyright © 2018, Janne Grunau
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

#ifndef CHECKASM_ARM_ASM_S
#define CHECKASM_ARM_ASM_S

#ifndef __has_feature
#define __has_feature(x) 0
#endif

#if ARCH_AARCH64
#define x18 do_not_use_x18
#define w18 do_not_use_w18

#if HAVE_AS_ARCHEXT_SVE_DIRECTIVE
#define ENABLE_SVE  .arch_extension sve
#define DISABLE_SVE .arch_extension nosve
#else
#define ENABLE_SVE
#define DISABLE_SVE
#endif
#if HAVE_AS_ARCHEXT_SME_DIRECTIVE
#define ENABLE_SME  .arch_extension sme
#define DISABLE_SME .arch_extension nosme
#else
#define ENABLE_SME
#define DISABLE_SME
#endif

/* If we do support the .arch_extension directives, disable support for all
 * the extensions that we may use, in case they were implicitly enabled by
 * default. This makes it clear if we try to assemble an instruction
 * from an unintended extension set; we only allow assmbling such instructions
 * within regions where we explicitly enable those extensions. */
DISABLE_SVE
DISABLE_SME


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
#define GNU_PROPERTY_AARCH64_BTI (1 << 0)   // Has Branch Target Identification
#define AARCH64_VALID_JUMP_CALL_TARGET hint #38  // BTI 'jc'
#define AARCH64_VALID_CALL_TARGET      hint #34  // BTI 'c'
#define AARCH64_VALID_JUMP_TARGET      hint #36  // BTI 'j'
#else
#define GNU_PROPERTY_AARCH64_BTI 0          // No Branch Target Identification
#define AARCH64_VALID_JUMP_CALL_TARGET
#define AARCH64_VALID_CALL_TARGET
#define AARCH64_VALID_JUMP_TARGET
#endif

#if defined(__ARM_FEATURE_PAC_DEFAULT)

#if ((__ARM_FEATURE_PAC_DEFAULT & (1 << 0)) != 0) // authentication using key A
#define AARCH64_SIGN_LINK_REGISTER      paciasp
#define AARCH64_VALIDATE_LINK_REGISTER  autiasp
#elif ((__ARM_FEATURE_PAC_DEFAULT & (1 << 1)) != 0) // authentication using key B
#define AARCH64_SIGN_LINK_REGISTER      pacibsp
#define AARCH64_VALIDATE_LINK_REGISTER  autibsp
#else
#error Pointer authentication defines no valid key!
#endif
#if ((__ARM_FEATURE_PAC_DEFAULT & (1 << 2)) != 0) // authentication of leaf functions
#error Authentication of leaf functions is enabled but not supported in dav1d!
#endif
#define GNU_PROPERTY_AARCH64_PAC (1 << 1)

#elif defined(__APPLE__) && defined(__arm64e__)

#define GNU_PROPERTY_AARCH64_PAC 0
#define AARCH64_SIGN_LINK_REGISTER      pacibsp
#define AARCH64_VALIDATE_LINK_REGISTER  autibsp

#else /* __ARM_FEATURE_PAC_DEFAULT */

#define GNU_PROPERTY_AARCH64_PAC 0
#define AARCH64_SIGN_LINK_REGISTER
#define AARCH64_VALIDATE_LINK_REGISTER

#endif /* !__ARM_FEATURE_PAC_DEFAULT */

#if defined(__ARM_FEATURE_GCS_DEFAULT) && __ARM_FEATURE_GCS_DEFAULT == 1
#define GNU_PROPERTY_AARCH64_GCS (1 << 2)
#else
#define GNU_PROPERTY_AARCH64_GCS 0 /* No GCS */
#endif

#if (GNU_PROPERTY_AARCH64_BTI != 0 || GNU_PROPERTY_AARCH64_PAC != 0 || GNU_PROPERTY_AARCH64_GCS != 0) && defined(__ELF__)
        .pushsection .note.gnu.property, "a"
        .balign 8
        .long 4
        .long 0x10
        .long 0x5
        .asciz "GNU"
        .long 0xc0000000 /* GNU_PROPERTY_AARCH64_FEATURE_1_AND */
        .long 4
        .long (GNU_PROPERTY_AARCH64_BTI | GNU_PROPERTY_AARCH64_PAC | GNU_PROPERTY_AARCH64_GCS)
        .long 0
        .popsection
#endif /* (GNU_PROPERTY_AARCH64_BTI != 0 || GNU_PROPERTY_AARCH64_PAC != 0 || GNU_PROPERTY_AARCH64_GCS != 0) && defined(__ELF__) */
#endif /* ARCH_AARCH64 */

#if ARCH_ARM
        .syntax unified
#ifdef __ELF__
        .arch armv7-a
        .fpu neon
        .eabi_attribute 10, 0           // suppress Tag_FP_arch
        .eabi_attribute 12, 0           // suppress Tag_Advanced_SIMD_arch
        .section .note.GNU-stack,"",%progbits // Mark stack as non-executable
#endif /* __ELF__ */

#ifdef _WIN32
#define CONFIG_THUMB 1
#else
#define CONFIG_THUMB 0
#endif

#if CONFIG_THUMB
        .thumb
#define A @
#define T
#else
#define A
#define T @
#endif /* CONFIG_THUMB */
#endif /* ARCH_ARM */

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

#ifdef PREFIX
#define EXTERN CONCAT(_,PRIVATE_PREFIX)
#else
#define EXTERN PRIVATE_PREFIX
#endif

#define QUOTE2(x) #x
#define QUOTE(x) QUOTE2(x)

.macro function name, export=0, align=2, visible=0
    .macro endfunc
#ifdef __ELF__
        .size   \name, . - \name
#endif
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
        .align \align
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
#if ARCH_AARCH64
    .if \export
         AARCH64_VALID_CALL_TARGET
    .endif
#endif
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

#ifdef __APPLE__
#define L(x) L ## x
#else
#define L(x) .L ## x
#endif

#define X(x) CONCAT(EXTERN, x)

#endif /* CHECKASM_ARM_ASM_S */
