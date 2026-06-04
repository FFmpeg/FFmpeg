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

#ifndef CHECKASM_PERF_PPC64LE_H
#define CHECKASM_PERF_PPC64LE_H

#include <stdint.h>

static inline uint64_t checkasm_mfspr(void)
{
    uint32_t tbu, tbl, temp;

    __asm__ __volatile__("1:\n"
                         "mfspr %2,269\n"
                         "mfspr %0,268\n"
                         "mfspr %1,269\n"
                         "cmpw   %2,%1\n"
                         "bne    1b\n"
                         : "=r"(tbl), "=r"(tbu), "=r"(temp)
                         :
                         : "cc");

    return (((uint64_t) tbu) << 32) | (uint64_t) tbl;
}

#define CHECKASM_PERF_ASM()    checkasm_mfspr()
#define CHECKASM_PERF_ASM_NAME "ppc64le (mfspr)"
#define CHECKASM_PERF_ASM_UNIT "tick"

#endif /* CHECKASM_PERF_PPC64LE_H */
