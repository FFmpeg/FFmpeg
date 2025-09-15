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

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/asm.h"
#include "libavutil/x86/cpu.h"
#include "libavfilter/vf_idetdsp.h"

#if HAVE_X86ASM

/* declares main callable idet_filter_line_sse2() */
#define FUNC_MAIN_DECL(KIND, SPAN)                                        \
int ff_idet_filter_line_##KIND(const uint8_t *a, const uint8_t *b,        \
                               const uint8_t *c, int w);                  \
static int idet_filter_line_##KIND(const uint8_t *a, const uint8_t *b,    \
                                   const uint8_t *c, int w) {             \
    int sum = 0;                                                          \
    const int left_over = w & (SPAN - 1);                                 \
    w -= left_over;                                                       \
    if (w > 0)                                                            \
        sum += ff_idet_filter_line_##KIND(a, b, c, w);                    \
    if (left_over > 0)                                                    \
        sum += ff_idet_filter_line_c(a + w, b + w, c + w, left_over);     \
    return sum;                                                           \
}


#define FUNC_MAIN_DECL_16bit(KIND, SPAN)                                       \
int ff_idet_filter_line_16bit_##KIND(const uint8_t *a, const uint8_t *b,       \
                                     const uint8_t *c, int w);                 \
static int idet_filter_line_16bit_##KIND(const uint8_t *a, const uint8_t *b,   \
                                         const uint8_t *c, int w) {            \
    int sum = 0;                                                               \
    const int left_over = w & (SPAN - 1);                                      \
    const int w_main = w - left_over;                                          \
    const int offset = w_main << 1;                                            \
    if (w_main > 0)                                                            \
        sum += ff_idet_filter_line_16bit_##KIND(a, b, c, w_main);              \
    if (left_over > 0) {                                                       \
        sum += ff_idet_filter_line_c_16bit(a + offset, b + offset, c + offset, \
                                           left_over);                         \
    }                                                                          \
    return sum;                                                                \
}

FUNC_MAIN_DECL(sse2, 16)
FUNC_MAIN_DECL_16bit(sse2, 8)

FUNC_MAIN_DECL(avx2, 32)
FUNC_MAIN_DECL_16bit(avx2, 16)

FUNC_MAIN_DECL(avx512icl, 64)
FUNC_MAIN_DECL_16bit(avx512icl, 32)

#endif
av_cold void ff_idet_dsp_init_x86(IDETDSPContext *dsp, int depth)
{
#if HAVE_X86ASM
    const int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_SSE2(cpu_flags)) {
        dsp->filter_line = depth > 8 ? idet_filter_line_16bit_sse2 : idet_filter_line_sse2;
    }
    if (EXTERNAL_AVX2(cpu_flags)) {
        dsp->filter_line = depth > 8 ? idet_filter_line_16bit_avx2 : idet_filter_line_avx2;
    }
    if (EXTERNAL_AVX512ICL(cpu_flags)) {
        dsp->filter_line = depth > 8 ? idet_filter_line_16bit_avx512icl : idet_filter_line_avx512icl;
    }
#endif // HAVE_X86ASM
}
