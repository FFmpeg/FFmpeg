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

#include <stdint.h>

#include "checkasm_config.h"
#include "cpu.h"
#include "internal.h"

#if ARCH_X86

int checkasm_check_vzeroupper = 0;

COLD int checkasm_get_check_vzeroupper(void)
{
    return checkasm_check_vzeroupper;
}

void checkasm_warmup_avx(void);
void checkasm_warmup_avx512(void);
void checkasm_dirty_ymm_state(void);

static void noop(void)
{
}

static size_t get_model_name(char *name)
{
    CpuidRegisters r;

    checkasm_cpu_cpuid(&r, 0x80000000, 0);
    if (r.eax >= 0x80000004) {
        /* processor brand string */
        CpuidRegisters *buf = (CpuidRegisters *) name;
        checkasm_cpu_cpuid(buf + 0, 0x80000002, 0);
        checkasm_cpu_cpuid(buf + 1, 0x80000003, 0);
        checkasm_cpu_cpuid(buf + 2, 0x80000004, 0);
    } else {
        /* use manufacturer id as a fallback */
        checkasm_cpu_cpuid(&r, 0, 0);
        memcpy(name + 0, &r.ebx, 4);
        memcpy(name + 4, &r.edx, 4);
        memcpy(name + 8, &r.ecx, 4);
        name[12] = '\0';
    }

    /* trim trailing whitespace */
    size_t len = strlen(name);
    while (len && name[len - 1] == ' ')
        len--;
    name[len] = '\0';
    return len;
}

static unsigned get_cpuid(void)
{
    CpuidRegisters r;

    checkasm_cpu_cpuid(&r, 0, 0);
    const uint32_t max_leaf = r.eax;
    if (!max_leaf)
        return 0;

    checkasm_cpu_cpuid(&r, 1, 0);
    const uint32_t cpuid_sig = r.eax;
    return cpuid_sig;
}

COLD char *checkasm_get_x86_cpuid(char *buf, size_t buflen)
{
    if (buflen < 64)
        return NULL;

    const size_t   len   = get_model_name(buf);
    const unsigned cpuid = get_cpuid();
    if (cpuid)
        snprintf(buf + len, buflen - len, " (%08X)", cpuid);
    return buf;
}

COLD void checkasm_init_x86(void)
{
    CpuidRegisters r;

    checkasm_cpu_cpuid(&r, 0, 0);
    const uint32_t max_leaf = r.eax;
    if (max_leaf < 13)
        return;

    checkasm_cpu_cpuid(&r, 1, 0);
    if (~r.ecx & 0x18000000 /* OSXSAVE/AVX */)
        return;

    checkasm_cpu_cpuid(&r, 13, 1);
    if (!(r.eax & 0x04)) /* XCR1 not supported */
        return;

    /* Check that the state is clean after touching XMM registers (with a
     * non-VEX-encoded instruction), without vzeroupper, after using YMM
     * with vzeroupper. (This currently fails on Zen 4 CPUs.) */
    checkasm_dirty_ymm_state();
    const uint64_t xcr1 = checkasm_cpu_xgetbv(1);
    if (xcr1 & 0x04) /* always-dirty ymm state */
        return;

#if ARCH_X86_32 && defined(_WIN32)
    /* x86_32 processes on Windows can spuriously get the dirty ymm bit set
     * while running; skip checking this aspect. */
#else
    checkasm_check_vzeroupper = 1;
#endif
}

typedef void (*checkasm_simd_warmup_func)(void);
static COLD checkasm_simd_warmup_func get_simd_warmup(void)
{
    checkasm_simd_warmup_func simd_warmup = noop;
    CpuidRegisters            r;
    checkasm_cpu_cpuid(&r, 0, 0);
    const uint32_t max_leaf = r.eax;
    if (max_leaf < 1)
        return simd_warmup;

    checkasm_cpu_cpuid(&r, 1, 0);
    if (~r.ecx & 0x18000000) /* OSXSAVE/AVX */
        return simd_warmup;

    const uint64_t xcr0 = checkasm_cpu_xgetbv(0);
    if (~xcr0 & 0x6) /* XMM/YMM */
        return simd_warmup;

    simd_warmup = checkasm_warmup_avx;
    if (max_leaf < 7 || ~xcr0 & 0xe0) /* ZMM/OPMASK */
        return simd_warmup;

    checkasm_cpu_cpuid(&r, 7, 0);
    if (r.ebx & 0x00000020) /* AVX512F */
        simd_warmup = checkasm_warmup_avx512;

    return simd_warmup;
}

void checkasm_simd_warmup(void)
{
    static checkasm_simd_warmup_func simd_warmup = NULL;
    if (!simd_warmup)
        simd_warmup = get_simd_warmup();

    simd_warmup();
}

#endif
