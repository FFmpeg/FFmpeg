/*
 * Copyright © 2025, Martin Storsjo
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

#ifndef CHECKASM_CONFIG_H
#define CHECKASM_CONFIG_H

#include "checkasm/header_config.h"

#ifdef CHECKASM_HAVE_GENERATED_H
#include "checkasm_config_generated.h"
#endif

#ifndef __has_include
  #define __has_include(x) 0
#endif

#ifndef ARCH_AARCH64
  #define ARCH_AARCH64 CHECKASM_ARCH_AARCH64
#endif

#ifndef ARCH_ARM
  #define ARCH_ARM CHECKASM_ARCH_ARM
#endif

#ifndef ARCH_LOONGARCH
  #define ARCH_LOONGARCH CHECKASM_ARCH_LOONGARCH
#endif

#ifndef ARCH_LOONGARCH32
  #define ARCH_LOONGARCH32 CHECKASM_ARCH_LOONGARCH32
#endif

#ifndef ARCH_LOONGARCH64
  #define ARCH_LOONGARCH64 CHECKASM_ARCH_LOONGARCH64
#endif

#ifndef ARCH_PPC64LE
  #define ARCH_PPC64LE CHECKASM_ARCH_PPC64LE
#endif

#ifndef ARCH_RISCV
  #define ARCH_RISCV CHECKASM_ARCH_RISCV
#endif

#ifndef ARCH_RV32
  #define ARCH_RV32 CHECKASM_ARCH_RV32
#endif

#ifndef ARCH_RV64
  #define ARCH_RV64 CHECKASM_ARCH_RV64
#endif

#ifndef ARCH_X86
  #define ARCH_X86 CHECKASM_ARCH_X86
#endif

#ifndef ARCH_X86_32
  #define ARCH_X86_32 CHECKASM_ARCH_X86_32
#endif

#ifndef ARCH_X86_64
  #define ARCH_X86_64 CHECKASM_ARCH_X86_64
#endif

#ifndef CHECKASM_VERSION
  #define CHECKASM_VERSION "unknown"
#endif

#ifndef HAVE_ELF_AUX_INFO
  #if defined(__FreeBSD__)
    /* Also OpenBSD since 7.6 */
    #define HAVE_ELF_AUX_INFO 1
  #else
    #define HAVE_ELF_AUX_INFO 0
  #endif
#endif

#ifndef HAVE_GETAUXVAL
  #if defined(__linux__)
    /* Since glibc 2.16 (2012), since Android 18 (4.3) */
    #define HAVE_GETAUXVAL 1
  #else
    #define HAVE_GETAUXVAL 0
  #endif
#endif

#ifndef HAVE_LINUX_PERF
  #if defined(__linux__) && __has_include(<linux/perf_event.h>)
    #define HAVE_LINUX_PERF 1
  #else
    #define HAVE_LINUX_PERF 0
  #endif
#endif

#ifndef HAVE_IOCTL
  #if defined(__linux__) || defined(__APPLE__) || defined(__DragonFly__)                 \
      || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)             \
      || defined(__unix__)
    /* Or just !defined(_WIN32), plus defines for identifying RiscOS? */
    #define HAVE_IOCTL 1
  #else
    #define HAVE_IOCTL 0
  #endif
#endif

#ifndef HAVE_ISATTY
  #if defined(__linux__) || defined(__APPLE__) || defined(__DragonFly__)                 \
      || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)             \
      || defined(__unix__) || defined(__OS2__)
    #define HAVE_ISATTY 1
  #else
    #define HAVE_ISATTY 0
  #endif
#endif

#ifndef HAVE_SIGACTION
  #if defined(__linux__) || defined(__APPLE__) || defined(__DragonFly__)                 \
      || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)             \
      || defined(__unix__) || defined(__OS2__)
    #define HAVE_SIGACTION 1
  #else
    #define HAVE_SIGACTION 0
  #endif
#endif

#ifndef HAVE_SIGLONGJMP
  #if defined(__linux__) || defined(__APPLE__) || defined(__DragonFly__)                 \
      || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)             \
      || defined(__unix__) || defined(__OS2__)
    #define HAVE_SIGLONGJMP 1
  #else
    #define HAVE_SIGLONGJMP 0
  #endif
#endif

#ifndef HAVE_STDBIT_H
  #if __has_include(<stdbit.h>)
    #define HAVE_STDBIT_H 1
  #else
    #define HAVE_STDBIT_H 0
  #endif
#endif

#ifndef HAVE_PTHREAD_NP_H
  #if __has_include(<pthread_np.h>)
    /* DragonFlyBSD, FreeBSD */
    #define HAVE_PTHREAD_NP_H 1
  #else
    #define HAVE_PTHREAD_NP_H 0
  #endif
#endif

#ifndef HAVE_PTHREAD_SETAFFINITY_NP
  #if (defined(__linux__) && !defined(__ANDROID__)) || defined(__DragonFly__)            \
      || defined(__FreeBSD__)
    #define HAVE_PTHREAD_SETAFFINITY_NP 1
  #else
    #define HAVE_PTHREAD_SETAFFINITY_NP 0
  #endif
#endif

#ifndef HAVE_CLOCK_GETTIME
  #if defined(__linux__) || defined(__APPLE__) || defined(__DragonFly__)                 \
      || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    #define HAVE_CLOCK_GETTIME 1
  #else
    #define HAVE_CLOCK_GETTIME 0
  #endif
#endif

#ifndef HAVE_PRCTL
  #if defined(__linux__)
    #define HAVE_PRCTL 1
  #else
    #define HAVE_PRCTL 0
  #endif
#endif

#ifndef PREFIX
  /* This one is different; this one is defined/undefined, not defined to
   * 0/1. */
  #if (defined(_WIN32) && ARCH_X86_32) || defined(__APPLE__) || defined(__OS2__)
    #define PREFIX
  #else
    #undef PREFIX
  #endif
#endif

#if ARCH_AARCH64
  /* Just default to 0 unless the caller has detected support and set it
   * in the generated header. */
  #ifndef HAVE_AS_ARCHEXT_SVE_DIRECTIVE
    #define HAVE_AS_ARCHEXT_SVE_DIRECTIVE 0
  #endif
  #ifndef HAVE_AS_ARCHEXT_SME_DIRECTIVE
    #define HAVE_AS_ARCHEXT_SME_DIRECTIVE 0
  #endif
  #ifndef HAVE_SVE
    #define HAVE_SVE 0
  #endif
  #ifndef HAVE_SME
    #define HAVE_SME 0
  #endif
#endif

#if ARCH_RISCV
  #ifndef HAVE_ASM_HWPROBE_H
    #if __has_include(<asm/hwprobe.h>)
      #define HAVE_ASM_HWPROBE_H 1
    #else
      #define HAVE_ASM_HWPROBE_H 0
    #endif
  #endif

  #ifndef HAVE_SYS_HWPROBE_H
    #if __has_include(<sys/hwprobe.h>)
      #define HAVE_SYS_HWPROBE_H 1
    #else
      #define HAVE_SYS_HWPROBE_H 0
    #endif
  #endif
#endif

/**
 * Exports symbols for internal use inside the checkasm self-tests.
 */
#ifndef CHECKASM_SELF_API
  #ifdef CHECKASM_BUILDING_TESTS
    #define CHECKASM_SELF_API CHECKASM_API
  #else
    #define CHECKASM_SELF_API
  #endif
#endif

#endif /* CHECKASM_CONFIG_H */
