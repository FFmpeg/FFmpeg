/*
 * Lossless video DSP utils
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
#include "libavutil/x86/asm.h"
#include "../lossless_videodsp.h"
#include "libavutil/x86/cpu.h"

void ff_add_bytes_mmx(uint8_t *dst, uint8_t *src, ptrdiff_t w);
void ff_add_bytes_sse2(uint8_t *dst, uint8_t *src, ptrdiff_t w);

void ff_add_median_pred_mmxext(uint8_t *dst, const uint8_t *top,
                               const uint8_t *diff, ptrdiff_t w,
                               int *left, int *left_top);
void ff_add_median_pred_sse2(uint8_t *dst, const uint8_t *top,
                             const uint8_t *diff, ptrdiff_t w,
                             int *left, int *left_top);

int  ff_add_left_pred_ssse3(uint8_t *dst, const uint8_t *src,
                            ptrdiff_t w, int left);
int  ff_add_left_pred_unaligned_ssse3(uint8_t *dst, const uint8_t *src,
                                      ptrdiff_t w, int left);

int ff_add_left_pred_int16_ssse3(uint16_t *dst, const uint16_t *src, unsigned mask, ptrdiff_t w, unsigned acc);
int ff_add_left_pred_int16_sse4(uint16_t *dst, const uint16_t *src, unsigned mask, ptrdiff_t w, unsigned acc);

#if HAVE_INLINE_ASM && HAVE_7REGS && ARCH_X86_32
static void add_median_pred_cmov(uint8_t *dst, const uint8_t *top,
                                 const uint8_t *diff, ptrdiff_t w,
                                 int *left, int *left_top)
{
    x86_reg w2 = -w;
    x86_reg x;
    int l  = *left     & 0xff;
    int tl = *left_top & 0xff;
    int t;
    __asm__ volatile (
        "mov          %7, %3            \n"
        "1:                             \n"
        "movzbl (%3, %4), %2            \n"
        "mov          %2, %k3           \n"
        "sub         %b1, %b3           \n"
        "add         %b0, %b3           \n"
        "mov          %2, %1            \n"
        "cmp          %0, %2            \n"
        "cmovg        %0, %2            \n"
        "cmovg        %1, %0            \n"
        "cmp         %k3, %0            \n"
        "cmovg       %k3, %0            \n"
        "mov          %7, %3            \n"
        "cmp          %2, %0            \n"
        "cmovl        %2, %0            \n"
        "add    (%6, %4), %b0           \n"
        "mov         %b0, (%5, %4)      \n"
        "inc          %4                \n"
        "jl           1b                \n"
        : "+&q"(l), "+&q"(tl), "=&r"(t), "=&q"(x), "+&r"(w2)
        : "r"(dst + w), "r"(diff + w), "rm"(top + w)
    );
    *left     = l;
    *left_top = tl;
}
#endif

void ff_llviddsp_init_x86(LLVidDSPContext *c)
{
    int cpu_flags = av_get_cpu_flags();

#if HAVE_INLINE_ASM && HAVE_7REGS && ARCH_X86_32
    if (cpu_flags & AV_CPU_FLAG_CMOV)
        c->add_median_pred = add_median_pred_cmov;
#endif

    if (ARCH_X86_32 && EXTERNAL_MMX(cpu_flags)) {
        c->add_bytes = ff_add_bytes_mmx;
    }

    if (ARCH_X86_32 && EXTERNAL_MMXEXT(cpu_flags)) {
        /* slower than cmov version on AMD */
        if (!(cpu_flags & AV_CPU_FLAG_3DNOW))
            c->add_median_pred = ff_add_median_pred_mmxext;
    }

    if (EXTERNAL_SSE2(cpu_flags)) {
        c->add_bytes       = ff_add_bytes_sse2;
        c->add_median_pred = ff_add_median_pred_sse2;
    }

    if (EXTERNAL_SSSE3(cpu_flags)) {
        c->add_left_pred = ff_add_left_pred_ssse3;
        c->add_left_pred_int16 = ff_add_left_pred_int16_ssse3;
    }

    if (EXTERNAL_SSSE3_FAST(cpu_flags)) {
        c->add_left_pred = ff_add_left_pred_unaligned_ssse3;
    }

    if (EXTERNAL_SSE4(cpu_flags)) {
        c->add_left_pred_int16 = ff_add_left_pred_int16_sse4;
    }
}
