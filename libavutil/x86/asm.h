/*
 * copyright (c) 2006 Michael Niedermayer <michaelni@gmx.at>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVUTIL_X86_ASM_H
#define AVUTIL_X86_ASM_H

#include <stdint.h>
#include "config.h"

typedef struct xmm_reg { uint64_t a, b; } xmm_reg;
typedef struct ymm_reg { uint64_t a, b, c, d; } ymm_reg;

#if ARCH_X86_64
#    define FF_OPSIZE "q"
#    define FF_REG_a "rax"
#    define FF_REG_b "rbx"
#    define FF_REG_c "rcx"
#    define FF_REG_d "rdx"
#    define FF_REG_D "rdi"
#    define FF_REG_S "rsi"
#    define FF_PTR_SIZE "8"
typedef int64_t x86_reg;

#    define FF_REG_SP "rsp"
#    define FF_REG_BP "rbp"
#    define FF_REGBP   rbp
#    define FF_REGa    rax
#    define FF_REGb    rbx
#    define FF_REGc    rcx
#    define FF_REGd    rdx
#    define FF_REGSP   rsp

#elif ARCH_X86_32

#    define FF_OPSIZE "l"
#    define FF_REG_a "eax"
#    define FF_REG_b "ebx"
#    define FF_REG_c "ecx"
#    define FF_REG_d "edx"
#    define FF_REG_D "edi"
#    define FF_REG_S "esi"
#    define FF_PTR_SIZE "4"
typedef int32_t x86_reg;

#    define FF_REG_SP "esp"
#    define FF_REG_BP "ebp"
#    define FF_REGBP   ebp
#    define FF_REGa    eax
#    define FF_REGb    ebx
#    define FF_REGc    ecx
#    define FF_REGd    edx
#    define FF_REGSP   esp
#else
typedef int x86_reg;
#endif

#define HAVE_7REGS (ARCH_X86_64 || (HAVE_EBX_AVAILABLE && HAVE_EBP_AVAILABLE))
#define HAVE_6REGS (ARCH_X86_64 || (HAVE_EBX_AVAILABLE || HAVE_EBP_AVAILABLE))

#if ARCH_X86_64 && defined(PIC)
#    define BROKEN_RELOCATIONS 1
#endif

/*
 * If gcc is not set to support sse (-msse) it will not accept xmm registers
 * in the clobber list for inline asm. XMM_CLOBBERS takes a list of xmm
 * registers to be marked as clobbered and evaluates to nothing if they are
 * not supported, or to the list itself if they are supported. Since a clobber
 * list may not be empty, XMM_CLOBBERS_ONLY should be used if the xmm
 * registers are the only in the clobber list.
 * For example a list with "eax" and "xmm0" as clobbers should become:
 * : XMM_CLOBBERS("xmm0",) "eax"
 * and a list with only "xmm0" should become:
 * XMM_CLOBBERS_ONLY("xmm0")
 */
#if HAVE_XMM_CLOBBERS
#    define XMM_CLOBBERS(...)        __VA_ARGS__
#    define XMM_CLOBBERS_ONLY(...) : __VA_ARGS__
#else
#    define XMM_CLOBBERS(...)
#    define XMM_CLOBBERS_ONLY(...)
#endif

/* Use to export labels from asm. */
#define LABEL_MANGLE(a) EXTERN_PREFIX #a

// Use rip-relative addressing if compiling PIC code on x86-64.
#if ARCH_X86_64 && defined(PIC)
#    define LOCAL_MANGLE(a) #a "(%%rip)"
#else
#    define LOCAL_MANGLE(a) #a
#endif

#define MANGLE(a) EXTERN_PREFIX LOCAL_MANGLE(a)

#endif /* AVUTIL_X86_ASM_H */
