/*
 * Copyright © 2025, Niklas Haas
 * Copyright © 2018, VideoLAN and dav1d authors
 * Copyright © 2018, Two Orioles, LLC
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

#ifndef CHECKASM_HEADER_CONFIG_H
#define CHECKASM_HEADER_CONFIG_H

#ifdef CHECKASM_HAVE_HEADER_GENERATED_H
  #include "checkasm_header_config_generated.h"
#endif

#if defined(__aarch64__) || defined(_M_ARM64) || defined(__arm64ec__)                    \
    || defined(_M_ARM64EC)
  #ifndef CHECKASM_ARCH_AARCH64
    #define CHECKASM_ARCH_AARCH64 1
  #endif
#elif defined(__arm__) || defined(_M_ARM)
  #ifndef CHECKASM_ARCH_ARM
    #define CHECKASM_ARCH_ARM 1
  #endif
#elif defined(__x86_64__) || defined(_M_AMD64)
  #ifndef CHECKASM_ARCH_X86
    #define CHECKASM_ARCH_X86    1
    #define CHECKASM_ARCH_X86_64 1
  #endif
#elif defined(__i386__) || defined(_M_IX86)
  #ifndef CHECKASM_ARCH_X86
    #define CHECKASM_ARCH_X86    1
    #define CHECKASM_ARCH_X86_32 1
  #endif
#elif defined(__powerpc64__) && defined(__LITTLE_ENDIAN__)
  #ifndef CHECKASM_ARCH_PPC64LE
    #define CHECKASM_ARCH_PPC64LE 1
  #endif
#elif defined(__riscv)
  #ifndef CHECKASM_ARCH_RISCV
    #define CHECKASM_ARCH_RISCV 1
    #if __riscv_xlen == 64
      #define CHECKASM_ARCH_RV64 1
    #else
      #define CHECKASM_ARCH_RV32 1
    #endif
  #endif
#elif defined(__loongarch__)
  #ifndef CHECKASM_ARCH_LOONGARCH
    #define CHECKASM_ARCH_LOONGARCH 1
    #if defined(__loongarch64)
      #define CHECKASM_ARCH_LOONGARCH64 1
    #else
      #define CHECKASM_ARCH_LOONGARCH32 1
    #endif
  #endif
#endif

#ifndef CHECKASM_ARCH_AARCH64
  #define CHECKASM_ARCH_AARCH64 0
#endif

#ifndef CHECKASM_ARCH_ARM
  #define CHECKASM_ARCH_ARM 0
#endif

#ifndef CHECKASM_ARCH_X86
  #define CHECKASM_ARCH_X86 0
#endif

#ifndef CHECKASM_ARCH_X86_64
  #define CHECKASM_ARCH_X86_64 0
#endif

#ifndef CHECKASM_ARCH_X86_32
  #define CHECKASM_ARCH_X86_32 0
#endif

#ifndef CHECKASM_ARCH_PPC64LE
  #define CHECKASM_ARCH_PPC64LE 0
#endif

#ifndef CHECKASM_ARCH_RISCV
  #define CHECKASM_ARCH_RISCV 0
#endif

#ifndef CHECKASM_ARCH_RV64
  #define CHECKASM_ARCH_RV64 0
#endif

#ifndef CHECKASM_ARCH_RV32
  #define CHECKASM_ARCH_RV32 0
#endif

#ifndef CHECKASM_ARCH_LOONGARCH
  #define CHECKASM_ARCH_LOONGARCH 0
#endif

#ifndef CHECKASM_ARCH_LOONGARCH64
  #define CHECKASM_ARCH_LOONGARCH64 0
#endif

#ifndef CHECKASM_ARCH_LOONGARCH32
  #define CHECKASM_ARCH_LOONGARCH32 0
#endif

#endif /* CHECKASM_HEADER_CONFIG_H */
