/*
 * SIMD-optimized LPC functions
 * Copyright (c) 2007 Loren Merritt
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

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/mem_internal.h"
#include "libavutil/x86/asm.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/lpc.h"

DECLARE_ASM_CONST(16, double, pd_1)[2] = { 1.0, 1.0 };
DECLARE_ASM_CONST(16, double, pd_2)[2] = { 2.0, 2.0 };

#if HAVE_SSE2_INLINE

static void lpc_apply_welch_window_sse2(const int32_t *data, int len,
                                        double *w_data)
{
    double c = 2.0 / (len-1.0);
    int n2 = len>>1;
    x86_reg i = -n2*sizeof(int32_t);
    x86_reg j =  n2*sizeof(int32_t);
    __asm__ volatile(
        "movsd   %4,     %%xmm7                \n\t"
        "movapd  "MANGLE(pd_1)", %%xmm6        \n\t"
        "movapd  "MANGLE(pd_2)", %%xmm5        \n\t"
        "movlhps %%xmm7, %%xmm7                \n\t"
        "subpd   %%xmm5, %%xmm7                \n\t"
        "addsd   %%xmm6, %%xmm7                \n\t"
        "test    $1,     %5                    \n\t"
        "jz      2f                            \n\t"
#define WELCH(MOVPD, offset)\
        "1:                                    \n\t"\
        "movapd   %%xmm7,  %%xmm1              \n\t"\
        "mulpd    %%xmm1,  %%xmm1              \n\t"\
        "movapd   %%xmm6,  %%xmm0              \n\t"\
        "subpd    %%xmm1,  %%xmm0              \n\t"\
        "pshufd   $0x4e,   %%xmm0, %%xmm1      \n\t"\
        "cvtpi2pd (%3,%0), %%xmm2              \n\t"\
        "cvtpi2pd "#offset"*4(%3,%1), %%xmm3   \n\t"\
        "mulpd    %%xmm0,  %%xmm2              \n\t"\
        "mulpd    %%xmm1,  %%xmm3              \n\t"\
        "movapd   %%xmm2, (%2,%0,2)            \n\t"\
        MOVPD"    %%xmm3, "#offset"*8(%2,%1,2) \n\t"\
        "subpd    %%xmm5,  %%xmm7              \n\t"\
        "sub      $8,      %1                  \n\t"\
        "add      $8,      %0                  \n\t"\
        "jl 1b                                 \n\t"\

        WELCH("movupd", -1)
        "jmp 3f                                \n\t"
        "2:                                    \n\t"
        WELCH("movapd", -2)
        "3:                                    \n\t"
        :"+&r"(i), "+&r"(j)
        :"r"(w_data+n2), "r"(data+n2), "m"(c), "r"(len)
         NAMED_CONSTRAINTS_ARRAY_ADD(pd_1,pd_2)
         XMM_CLOBBERS_ONLY("%xmm0", "%xmm1", "%xmm2", "%xmm3",
                                    "%xmm5", "%xmm6", "%xmm7")
    );
#undef WELCH
}

static void lpc_compute_autocorr_sse2(const double *data, int len, int lag,
                                      double *autoc)
{
    int j;

    if((x86_reg)data & 15)
        data++;

    for(j=0; j<lag; j+=2){
        x86_reg i = -len*sizeof(double);
        if(j == lag-2) {
            __asm__ volatile(
                "movsd    "MANGLE(pd_1)", %%xmm0    \n\t"
                "movsd    "MANGLE(pd_1)", %%xmm1    \n\t"
                "movsd    "MANGLE(pd_1)", %%xmm2    \n\t"
                "1:                                 \n\t"
                "movapd   (%2,%0), %%xmm3           \n\t"
                "movupd -8(%3,%0), %%xmm4           \n\t"
                "movapd   (%3,%0), %%xmm5           \n\t"
                "mulpd     %%xmm3, %%xmm4           \n\t"
                "mulpd     %%xmm3, %%xmm5           \n\t"
                "mulpd -16(%3,%0), %%xmm3           \n\t"
                "addpd     %%xmm4, %%xmm1           \n\t"
                "addpd     %%xmm5, %%xmm0           \n\t"
                "addpd     %%xmm3, %%xmm2           \n\t"
                "add       $16,    %0               \n\t"
                "jl 1b                              \n\t"
                "movhlps   %%xmm0, %%xmm3           \n\t"
                "movhlps   %%xmm1, %%xmm4           \n\t"
                "movhlps   %%xmm2, %%xmm5           \n\t"
                "addsd     %%xmm3, %%xmm0           \n\t"
                "addsd     %%xmm4, %%xmm1           \n\t"
                "addsd     %%xmm5, %%xmm2           \n\t"
                "movsd     %%xmm0,   (%1)           \n\t"
                "movsd     %%xmm1,  8(%1)           \n\t"
                "movsd     %%xmm2, 16(%1)           \n\t"
                :"+&r"(i)
                :"r"(autoc+j), "r"(data+len), "r"(data+len-j)
                 NAMED_CONSTRAINTS_ARRAY_ADD(pd_1)
                :"memory"
            );
        } else {
            __asm__ volatile(
                "movsd    "MANGLE(pd_1)", %%xmm0    \n\t"
                "movsd    "MANGLE(pd_1)", %%xmm1    \n\t"
                "1:                                 \n\t"
                "movapd   (%3,%0), %%xmm3           \n\t"
                "movupd -8(%4,%0), %%xmm4           \n\t"
                "mulpd     %%xmm3, %%xmm4           \n\t"
                "mulpd    (%4,%0), %%xmm3           \n\t"
                "addpd     %%xmm4, %%xmm1           \n\t"
                "addpd     %%xmm3, %%xmm0           \n\t"
                "add       $16,    %0               \n\t"
                "jl 1b                              \n\t"
                "movhlps   %%xmm0, %%xmm3           \n\t"
                "movhlps   %%xmm1, %%xmm4           \n\t"
                "addsd     %%xmm3, %%xmm0           \n\t"
                "addsd     %%xmm4, %%xmm1           \n\t"
                "movsd     %%xmm0, %1               \n\t"
                "movsd     %%xmm1, %2               \n\t"
                :"+&r"(i), "=m"(autoc[j]), "=m"(autoc[j+1])
                :"r"(data+len), "r"(data+len-j)
                 NAMED_CONSTRAINTS_ARRAY_ADD(pd_1)
            );
        }
    }
}

#endif /* HAVE_SSE2_INLINE */

av_cold void ff_lpc_init_x86(LPCContext *c)
{
#if HAVE_SSE2_INLINE
    int cpu_flags = av_get_cpu_flags();

    if (INLINE_SSE2_SLOW(cpu_flags)) {
        c->lpc_apply_welch_window = lpc_apply_welch_window_sse2;
        c->lpc_compute_autocorr   = lpc_compute_autocorr_sse2;
    }
#endif /* HAVE_SSE2_INLINE */
}
