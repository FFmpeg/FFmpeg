/*
 * The simplest mpeg encoder (well, it was the simplest!)
 * Copyright (c) 2000,2001 Fabrice Bellard
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
#include "libavcodec/avcodec.h"
#include "libavcodec/dct.h"
#include "libavcodec/mpegvideoenc.h"

/* not permutated inverse zigzag_direct + 1 for MMX quantizer */
DECLARE_ALIGNED(16, static const uint16_t, inv_zigzag_direct16)[64] = {
    1,  2,  6,  7,  15, 16, 28, 29,
    3,  5,  8,  14, 17, 27, 30, 43,
    4,  9,  13, 18, 26, 31, 42, 44,
    10, 12, 19, 25, 32, 41, 45, 54,
    11, 20, 24, 33, 40, 46, 53, 55,
    21, 23, 34, 39, 47, 52, 56, 61,
    22, 35, 38, 48, 51, 57, 60, 62,
    36, 37, 49, 50, 58, 59, 63, 64,
};

#if HAVE_6REGS

#if HAVE_SSE2_INLINE
#define COMPILE_TEMPLATE_SSSE3  0
#define RENAME(a)      a ## _sse2
#include "mpegvideoenc_template.c"
#endif /* HAVE_SSE2_INLINE */

#if HAVE_SSSE3_INLINE
#undef COMPILE_TEMPLATE_SSSE3
#define COMPILE_TEMPLATE_SSSE3  1
#undef RENAME
#define RENAME(a)      a ## _ssse3
#include "mpegvideoenc_template.c"
#endif /* HAVE_SSSE3_INLINE */

#endif /* HAVE_6REGS */

#if HAVE_INLINE_ASM
#if HAVE_SSE2_INLINE
static void denoise_dct_sse2(MPVEncContext *const s, int16_t block[])
{
    const int intra = s->c.mb_intra;
    int *sum= s->dct_error_sum[intra];
    uint16_t *offset= s->dct_offset[intra];

    s->dct_count[intra]++;

    __asm__ volatile(
        "pxor %%xmm7, %%xmm7                    \n\t"
        "1:                                     \n\t"
        "pxor %%xmm0, %%xmm0                    \n\t"
        "pxor %%xmm1, %%xmm1                    \n\t"
        "movdqa (%0), %%xmm2                    \n\t"
        "movdqa 16(%0), %%xmm3                  \n\t"
        "pcmpgtw %%xmm2, %%xmm0                 \n\t"
        "pcmpgtw %%xmm3, %%xmm1                 \n\t"
        "pxor %%xmm0, %%xmm2                    \n\t"
        "pxor %%xmm1, %%xmm3                    \n\t"
        "psubw %%xmm0, %%xmm2                   \n\t"
        "psubw %%xmm1, %%xmm3                   \n\t"
        "movdqa %%xmm2, %%xmm4                  \n\t"
        "movdqa %%xmm3, %%xmm5                  \n\t"
        "psubusw (%2), %%xmm2                   \n\t"
        "psubusw 16(%2), %%xmm3                 \n\t"
        "pxor %%xmm0, %%xmm2                    \n\t"
        "pxor %%xmm1, %%xmm3                    \n\t"
        "psubw %%xmm0, %%xmm2                   \n\t"
        "psubw %%xmm1, %%xmm3                   \n\t"
        "movdqa %%xmm2, (%0)                    \n\t"
        "movdqa %%xmm3, 16(%0)                  \n\t"
        "movdqa %%xmm4, %%xmm6                  \n\t"
        "movdqa %%xmm5, %%xmm0                  \n\t"
        "punpcklwd %%xmm7, %%xmm4               \n\t"
        "punpckhwd %%xmm7, %%xmm6               \n\t"
        "punpcklwd %%xmm7, %%xmm5               \n\t"
        "punpckhwd %%xmm7, %%xmm0               \n\t"
        "paddd (%1), %%xmm4                     \n\t"
        "paddd 16(%1), %%xmm6                   \n\t"
        "paddd 32(%1), %%xmm5                   \n\t"
        "paddd 48(%1), %%xmm0                   \n\t"
        "movdqa %%xmm4, (%1)                    \n\t"
        "movdqa %%xmm6, 16(%1)                  \n\t"
        "movdqa %%xmm5, 32(%1)                  \n\t"
        "movdqa %%xmm0, 48(%1)                  \n\t"
        "add $32, %0                            \n\t"
        "add $64, %1                            \n\t"
        "add $32, %2                            \n\t"
        "cmp %3, %0                             \n\t"
            " jb 1b                             \n\t"
        : "+r" (block), "+r" (sum), "+r" (offset)
        : "r"(block+64)
          XMM_CLOBBERS_ONLY("%xmm0", "%xmm1", "%xmm2", "%xmm3",
                            "%xmm4", "%xmm5", "%xmm6", "%xmm7")
    );
}
#endif /* HAVE_SSE2_INLINE */
#endif /* HAVE_INLINE_ASM */

av_cold void ff_dct_encode_init_x86(MPVEncContext *const s)
{
    const int dct_algo = s->c.avctx->dct_algo;

    if (dct_algo == FF_DCT_AUTO || dct_algo == FF_DCT_MMX) {
#if HAVE_MMX_INLINE
        int cpu_flags = av_get_cpu_flags();
#if HAVE_SSE2_INLINE
        if (INLINE_SSE2(cpu_flags)) {
#if HAVE_6REGS
            s->dct_quantize = dct_quantize_sse2;
#endif
            s->denoise_dct  = denoise_dct_sse2;
        }
#endif
#if HAVE_6REGS && HAVE_SSSE3_INLINE
        if (INLINE_SSSE3(cpu_flags))
            s->dct_quantize = dct_quantize_ssse3;
#endif
#endif
    }
}
