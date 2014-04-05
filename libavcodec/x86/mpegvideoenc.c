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
#include "libavutil/x86/asm.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/dct.h"
#include "libavcodec/mpegvideo.h"

/* not permutated inverse zigzag_direct + 1 for MMX quantizer */
DECLARE_ALIGNED(16, static uint16_t, inv_zigzag_direct16)[64];

#if HAVE_MMX_INLINE
#define COMPILE_TEMPLATE_MMXEXT 0
#define COMPILE_TEMPLATE_SSE2   0
#define COMPILE_TEMPLATE_SSSE3  0
#define RENAME(a) a ## _MMX
#define RENAMEl(a) a ## _mmx
#include "mpegvideoenc_template.c"
#endif /* HAVE_MMX_INLINE */

#if HAVE_MMXEXT_INLINE
#undef COMPILE_TEMPLATE_SSSE3
#undef COMPILE_TEMPLATE_SSE2
#undef COMPILE_TEMPLATE_MMXEXT
#define COMPILE_TEMPLATE_MMXEXT 1
#define COMPILE_TEMPLATE_SSE2   0
#define COMPILE_TEMPLATE_SSSE3  0
#undef RENAME
#undef RENAMEl
#define RENAME(a) a ## _MMXEXT
#define RENAMEl(a) a ## _mmxext
#include "mpegvideoenc_template.c"
#endif /* HAVE_MMXEXT_INLINE */

#if HAVE_SSE2_INLINE
#undef COMPILE_TEMPLATE_MMXEXT
#undef COMPILE_TEMPLATE_SSE2
#undef COMPILE_TEMPLATE_SSSE3
#define COMPILE_TEMPLATE_MMXEXT 0
#define COMPILE_TEMPLATE_SSE2   1
#define COMPILE_TEMPLATE_SSSE3  0
#undef RENAME
#undef RENAMEl
#define RENAME(a) a ## _SSE2
#define RENAMEl(a) a ## _sse2
#include "mpegvideoenc_template.c"
#endif /* HAVE_SSE2_INLINE */

#if HAVE_SSSE3_INLINE
#undef COMPILE_TEMPLATE_MMXEXT
#undef COMPILE_TEMPLATE_SSE2
#undef COMPILE_TEMPLATE_SSSE3
#define COMPILE_TEMPLATE_MMXEXT 0
#define COMPILE_TEMPLATE_SSE2   1
#define COMPILE_TEMPLATE_SSSE3  1
#undef RENAME
#undef RENAMEl
#define RENAME(a) a ## _SSSE3
#define RENAMEl(a) a ## _sse2
#include "mpegvideoenc_template.c"
#endif /* HAVE_SSSE3_INLINE */

#if HAVE_INLINE_ASM
static void  denoise_dct_mmx(MpegEncContext *s, int16_t *block){
    const int intra= s->mb_intra;
    int *sum= s->dct_error_sum[intra];
    uint16_t *offset= s->dct_offset[intra];

    s->dct_count[intra]++;

    __asm__ volatile(
        "pxor %%mm7, %%mm7                      \n\t"
        "1:                                     \n\t"
        "pxor %%mm0, %%mm0                      \n\t"
        "pxor %%mm1, %%mm1                      \n\t"
        "movq (%0), %%mm2                       \n\t"
        "movq 8(%0), %%mm3                      \n\t"
        "pcmpgtw %%mm2, %%mm0                   \n\t"
        "pcmpgtw %%mm3, %%mm1                   \n\t"
        "pxor %%mm0, %%mm2                      \n\t"
        "pxor %%mm1, %%mm3                      \n\t"
        "psubw %%mm0, %%mm2                     \n\t"
        "psubw %%mm1, %%mm3                     \n\t"
        "movq %%mm2, %%mm4                      \n\t"
        "movq %%mm3, %%mm5                      \n\t"
        "psubusw (%2), %%mm2                    \n\t"
        "psubusw 8(%2), %%mm3                   \n\t"
        "pxor %%mm0, %%mm2                      \n\t"
        "pxor %%mm1, %%mm3                      \n\t"
        "psubw %%mm0, %%mm2                     \n\t"
        "psubw %%mm1, %%mm3                     \n\t"
        "movq %%mm2, (%0)                       \n\t"
        "movq %%mm3, 8(%0)                      \n\t"
        "movq %%mm4, %%mm2                      \n\t"
        "movq %%mm5, %%mm3                      \n\t"
        "punpcklwd %%mm7, %%mm4                 \n\t"
        "punpckhwd %%mm7, %%mm2                 \n\t"
        "punpcklwd %%mm7, %%mm5                 \n\t"
        "punpckhwd %%mm7, %%mm3                 \n\t"
        "paddd (%1), %%mm4                      \n\t"
        "paddd 8(%1), %%mm2                     \n\t"
        "paddd 16(%1), %%mm5                    \n\t"
        "paddd 24(%1), %%mm3                    \n\t"
        "movq %%mm4, (%1)                       \n\t"
        "movq %%mm2, 8(%1)                      \n\t"
        "movq %%mm5, 16(%1)                     \n\t"
        "movq %%mm3, 24(%1)                     \n\t"
        "add $16, %0                            \n\t"
        "add $32, %1                            \n\t"
        "add $16, %2                            \n\t"
        "cmp %3, %0                             \n\t"
            " jb 1b                             \n\t"
        : "+r" (block), "+r" (sum), "+r" (offset)
        : "r"(block+64)
    );
}

static void  denoise_dct_sse2(MpegEncContext *s, int16_t *block){
    const int intra= s->mb_intra;
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
#endif /* HAVE_INLINE_ASM */

av_cold void ff_dct_encode_init_x86(MpegEncContext *s)
{
    const int dct_algo = s->avctx->dct_algo;
    int i;

    for (i = 0; i < 64; i++)
        inv_zigzag_direct16[ff_zigzag_direct[i]] = i + 1;

    if (dct_algo == FF_DCT_AUTO || dct_algo == FF_DCT_MMX) {
#if HAVE_MMX_INLINE
        int cpu_flags = av_get_cpu_flags();
        if (INLINE_MMX(cpu_flags)) {
            s->dct_quantize = dct_quantize_MMX;
            s->denoise_dct  = denoise_dct_mmx;
        }
#endif
#if HAVE_MMXEXT_INLINE
        if (INLINE_MMXEXT(cpu_flags))
            s->dct_quantize = dct_quantize_MMXEXT;
#endif
#if HAVE_SSE2_INLINE
        if (INLINE_SSE2(cpu_flags)) {
            s->dct_quantize = dct_quantize_SSE2;
            s->denoise_dct  = denoise_dct_sse2;
        }
#endif
#if HAVE_SSSE3_INLINE
        if (INLINE_SSSE3(cpu_flags))
            s->dct_quantize = dct_quantize_SSSE3;
#endif
    }
}
