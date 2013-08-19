/*
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

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/float_dsp.h"
#include "cpu.h"
#include "asm.h"

void ff_vector_fmul_sse(float *dst, const float *src0, const float *src1,
                        int len);
void ff_vector_fmul_avx(float *dst, const float *src0, const float *src1,
                        int len);

void ff_vector_fmac_scalar_sse(float *dst, const float *src, float mul,
                               int len);
void ff_vector_fmac_scalar_avx(float *dst, const float *src, float mul,
                               int len);

void ff_vector_fmul_scalar_sse(float *dst, const float *src, float mul,
                               int len);

void ff_vector_dmul_scalar_sse2(double *dst, const double *src,
                                double mul, int len);
void ff_vector_dmul_scalar_avx(double *dst, const double *src,
                               double mul, int len);

void ff_vector_fmul_add_sse(float *dst, const float *src0, const float *src1,
                            const float *src2, int len);
void ff_vector_fmul_add_avx(float *dst, const float *src0, const float *src1,
                            const float *src2, int len);

void ff_vector_fmul_reverse_sse(float *dst, const float *src0,
                                const float *src1, int len);
void ff_vector_fmul_reverse_avx(float *dst, const float *src0,
                                const float *src1, int len);

float ff_scalarproduct_float_sse(const float *v1, const float *v2, int order);

void ff_butterflies_float_sse(float *src0, float *src1, int len);

#if HAVE_6REGS && HAVE_INLINE_ASM
static void vector_fmul_window_3dnowext(float *dst, const float *src0,
                                        const float *src1, const float *win,
                                        int len)
{
    x86_reg i = -len * 4;
    x86_reg j =  len * 4 - 8;
    __asm__ volatile (
        "1:                             \n"
        "pswapd (%5, %1), %%mm1         \n"
        "movq   (%5, %0), %%mm0         \n"
        "pswapd (%4, %1), %%mm5         \n"
        "movq   (%3, %0), %%mm4         \n"
        "movq      %%mm0, %%mm2         \n"
        "movq      %%mm1, %%mm3         \n"
        "pfmul     %%mm4, %%mm2         \n" // src0[len + i] * win[len + i]
        "pfmul     %%mm5, %%mm3         \n" // src1[j]       * win[len + j]
        "pfmul     %%mm4, %%mm1         \n" // src0[len + i] * win[len + j]
        "pfmul     %%mm5, %%mm0         \n" // src1[j]       * win[len + i]
        "pfadd     %%mm3, %%mm2         \n"
        "pfsub     %%mm0, %%mm1         \n"
        "pswapd    %%mm2, %%mm2         \n"
        "movq      %%mm1, (%2, %0)      \n"
        "movq      %%mm2, (%2, %1)      \n"
        "sub          $8, %1            \n"
        "add          $8, %0            \n"
        "jl           1b                \n"
        "femms                          \n"
        : "+r"(i), "+r"(j)
        : "r"(dst + len), "r"(src0 + len), "r"(src1), "r"(win + len)
    );
}

static void vector_fmul_window_sse(float *dst, const float *src0,
                                   const float *src1, const float *win, int len)
{
    x86_reg i = -len * 4;
    x86_reg j =  len * 4 - 16;
    __asm__ volatile (
        "1:                             \n"
        "movaps      (%5, %1), %%xmm1   \n"
        "movaps      (%5, %0), %%xmm0   \n"
        "movaps      (%4, %1), %%xmm5   \n"
        "movaps      (%3, %0), %%xmm4   \n"
        "shufps $0x1b, %%xmm1, %%xmm1   \n"
        "shufps $0x1b, %%xmm5, %%xmm5   \n"
        "movaps        %%xmm0, %%xmm2   \n"
        "movaps        %%xmm1, %%xmm3   \n"
        "mulps         %%xmm4, %%xmm2   \n" // src0[len + i] * win[len + i]
        "mulps         %%xmm5, %%xmm3   \n" // src1[j]       * win[len + j]
        "mulps         %%xmm4, %%xmm1   \n" // src0[len + i] * win[len + j]
        "mulps         %%xmm5, %%xmm0   \n" // src1[j]       * win[len + i]
        "addps         %%xmm3, %%xmm2   \n"
        "subps         %%xmm0, %%xmm1   \n"
        "shufps $0x1b, %%xmm2, %%xmm2   \n"
        "movaps        %%xmm1, (%2, %0) \n"
        "movaps        %%xmm2, (%2, %1) \n"
        "sub              $16, %1       \n"
        "add              $16, %0       \n"
        "jl                1b           \n"
        : "+r"(i), "+r"(j)
        : "r"(dst + len), "r"(src0 + len), "r"(src1), "r"(win + len)
    );
}
#endif /* HAVE_6REGS && HAVE_INLINE_ASM */

av_cold void ff_float_dsp_init_x86(AVFloatDSPContext *fdsp)
{
    int cpu_flags = av_get_cpu_flags();

#if HAVE_6REGS && HAVE_INLINE_ASM
    if (INLINE_AMD3DNOWEXT(cpu_flags)) {
        fdsp->vector_fmul_window  = vector_fmul_window_3dnowext;
    }
    if (INLINE_SSE(cpu_flags)) {
        fdsp->vector_fmul_window = vector_fmul_window_sse;
    }
#endif
    if (EXTERNAL_SSE(cpu_flags)) {
        fdsp->vector_fmul = ff_vector_fmul_sse;
        fdsp->vector_fmac_scalar = ff_vector_fmac_scalar_sse;
        fdsp->vector_fmul_scalar = ff_vector_fmul_scalar_sse;
        fdsp->vector_fmul_add    = ff_vector_fmul_add_sse;
        fdsp->vector_fmul_reverse = ff_vector_fmul_reverse_sse;
        fdsp->scalarproduct_float = ff_scalarproduct_float_sse;
        fdsp->butterflies_float   = ff_butterflies_float_sse;
    }
    if (EXTERNAL_SSE2(cpu_flags)) {
        fdsp->vector_dmul_scalar = ff_vector_dmul_scalar_sse2;
    }
    if (EXTERNAL_AVX(cpu_flags)) {
        fdsp->vector_fmul = ff_vector_fmul_avx;
        fdsp->vector_fmac_scalar = ff_vector_fmac_scalar_avx;
        fdsp->vector_dmul_scalar = ff_vector_dmul_scalar_avx;
        fdsp->vector_fmul_add    = ff_vector_fmul_add_avx;
        fdsp->vector_fmul_reverse = ff_vector_fmul_reverse_avx;
    }
}
