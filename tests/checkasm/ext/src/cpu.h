/*
 * Copyright © 2018-2022, VideoLAN and dav1d authors
 * Copyright © 2018-2022, Two Orioles, LLC
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

#ifndef CHECKASM_CPU_H
#define CHECKASM_CPU_H

#include "checkasm_config.h"
#include <stddef.h>
#include <stdint.h>

#include "checkasm/attributes.h"
#include "checkasm/checkasm.h"
#include "internal.h"

#if ARCH_X86

PACKED(typedef struct { uint32_t eax, ebx, ecx, edx; }) CpuidRegisters;

CHECKASM_SELF_API void
checkasm_cpu_cpuid(CpuidRegisters *regs, unsigned leaf, unsigned subleaf);
CHECKASM_SELF_API uint64_t checkasm_cpu_xgetbv(unsigned xcr);

/* Initializes internal state for checkasm_checked_call(). */
void checkasm_init_x86(void);

/* Returns whether the vzeroupper state check is active. Exported only for use
 * inside the selftest. */
CHECKASM_SELF_API int checkasm_get_check_vzeroupper(void);

/* Returns cpuid and model name. */
char *checkasm_get_x86_cpuid(char *buf, size_t buflen);

/* YMM and ZMM registers on x86 are turned off to save power when they haven't
 * been used for some period of time. When they are used there will be a
 * "warmup" period during which performance will be reduced and inconsistent
 * which is problematic when trying to benchmark individual functions. We can
 * work around this by periodically issuing "dummy" instructions that uses
 * those registers to keep them powered on. */
void checkasm_simd_warmup(void);

#elif ARCH_RISCV

/* Gets the CPU identification registers. */
int checkasm_get_cpuids(uint32_t *vendor, uintptr_t *arch, uintptr_t *imp);
const char *checkasm_get_riscv_vendor_name(uint32_t vendorid);
const char *checkasm_get_riscv_arch_name(char *buf, size_t len,
                                         uint32_t vendorid, uintptr_t archid);

/* Checks if floating point registers are supported. */
CHECKASM_SELF_API int checkasm_has_float(void);

/* Checks if vector registers are supported. */
CHECKASM_SELF_API int checkasm_has_vector(void);

/* Returns the vector length in bits, 0 if unavailable. */
unsigned long checkasm_get_vlen(void);

void checkasm_checked_call_i(void);
void checkasm_checked_call_if(void);
void checkasm_checked_call_iv(void);
void checkasm_checked_call_ifv(void);

#elif ARCH_AARCH64

/* Returns a nonzero value if SVE is available, 0 otherwise */
int checkasm_has_sve(void);
/* Returns a nonzero value if SME is available, 0 otherwise */
int checkasm_has_sme(void);

  #if HAVE_SVE

/* Returns the SVE vector length in bits */
int checkasm_sve_length(void);

  #endif

  #if HAVE_SME

/* Returns the SME vector length in bits */
int checkasm_sme_length(void);

  #endif

#elif ARCH_ARM

void checkasm_init_arm(void);

void checkasm_checked_call_vfp(void *func, int dummy, ...);
void checkasm_checked_call_novfp(void *func, int dummy, ...);

/* Returns a nonzero value if VFP is available, 0 otherwise */
CHECKASM_SELF_API int checkasm_has_vfp(void);
/* Returns a nonzero value if VFP has 32 registers, 0 otherwise */
CHECKASM_SELF_API int checkasm_has_vfpd32(void);

#endif

void          checkasm_init_cpu(void);
unsigned long checkasm_getauxval(unsigned long);
const char *checkasm_get_jedec_vendor_name(unsigned bank, unsigned offset);

#if (ARCH_ARM || ARCH_AARCH64) && defined(__linux__)
const char *checkasm_get_arm_cpuinfo(char *buf, size_t buflen, int affinity);
#endif

#if (ARCH_ARM || ARCH_AARCH64) && defined(_WIN32)
const char *checkasm_get_arm_win32_reg(char *buf, size_t buflen, int affinity);
#endif

/* Iterate over all known CPU information and run the callback on each line */
void checkasm_cpu_info(void (*info_cb)(void *priv, const char *fmt, ...), void *priv,
                       const CheckasmConfig *config);

#endif /* CHECKASM_CPU_H */
