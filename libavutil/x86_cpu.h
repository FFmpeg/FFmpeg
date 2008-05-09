/*
 * copyright (c) 2006 Michael Niedermayer <michaelni@gmx.at>
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

#ifndef FFMPEG_X86CPU_H
#define FFMPEG_X86CPU_H

#include <stdint.h>
#include "config.h"

#ifdef ARCH_X86_64
#    define REG_a "rax"
#    define REG_b "rbx"
#    define REG_c "rcx"
#    define REG_d "rdx"
#    define REG_D "rdi"
#    define REG_S "rsi"
#    define PTR_SIZE "8"
typedef int64_t x86_reg;

#    define REG_SP "rsp"
#    define REG_BP "rbp"
#    define REGBP   rbp
#    define REGa    rax
#    define REGb    rbx
#    define REGc    rcx
#    define REGd    rdx
#    define REGSP   rsp

#else

#    define REG_a "eax"
#    define REG_b "ebx"
#    define REG_c "ecx"
#    define REG_d "edx"
#    define REG_D "edi"
#    define REG_S "esi"
#    define PTR_SIZE "4"
typedef int32_t x86_reg;

#    define REG_SP "esp"
#    define REG_BP "ebp"
#    define REGBP   ebp
#    define REGa    eax
#    define REGb    ebx
#    define REGc    ecx
#    define REGd    edx
#    define REGSP   esp
#endif

#if defined(ARCH_X86_64) || (defined(ARCH_X86_32) && defined(HAVE_EBX_AVAILABLE) && defined(HAVE_EBP_AVAILABLE))
#    define HAVE_7REGS 1
#endif

#if defined(ARCH_X86_64) && defined(PIC)
#    define BROKEN_RELOCATIONS 1
#endif

#endif /* FFMPEG_X86CPU_H */
