/*
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */


#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/mem.h"
#include "libavutil/x86/asm.h"
#include "libavfilter/vf_spp.h"

#if HAVE_MMX_INLINE
static void hardthresh_mmx(int16_t dst[64], const int16_t src[64],
                           int qp, const uint8_t *permutation)
{
    int bias = 0; //FIXME
    unsigned int threshold1;

    threshold1 = qp * ((1<<4) - bias) - 1;

#define REQUANT_CORE(dst0, dst1, dst2, dst3, src0, src1, src2, src3)    \
    "movq " #src0 ", %%mm0      \n"                                     \
    "movq " #src1 ", %%mm1      \n"                                     \
    "movq " #src2 ", %%mm2      \n"                                     \
    "movq " #src3 ", %%mm3      \n"                                     \
    "psubw %%mm4, %%mm0         \n"                                     \
    "psubw %%mm4, %%mm1         \n"                                     \
    "psubw %%mm4, %%mm2         \n"                                     \
    "psubw %%mm4, %%mm3         \n"                                     \
    "paddusw %%mm5, %%mm0       \n"                                     \
    "paddusw %%mm5, %%mm1       \n"                                     \
    "paddusw %%mm5, %%mm2       \n"                                     \
    "paddusw %%mm5, %%mm3       \n"                                     \
    "paddw %%mm6, %%mm0         \n"                                     \
    "paddw %%mm6, %%mm1         \n"                                     \
    "paddw %%mm6, %%mm2         \n"                                     \
    "paddw %%mm6, %%mm3         \n"                                     \
    "psubusw %%mm6, %%mm0       \n"                                     \
    "psubusw %%mm6, %%mm1       \n"                                     \
    "psubusw %%mm6, %%mm2       \n"                                     \
    "psubusw %%mm6, %%mm3       \n"                                     \
    "psraw $3, %%mm0            \n"                                     \
    "psraw $3, %%mm1            \n"                                     \
    "psraw $3, %%mm2            \n"                                     \
    "psraw $3, %%mm3            \n"                                     \
                                                                        \
    "movq %%mm0, %%mm7          \n"                                     \
    "punpcklwd %%mm2, %%mm0     \n" /*A*/                               \
    "punpckhwd %%mm2, %%mm7     \n" /*C*/                               \
    "movq %%mm1, %%mm2          \n"                                     \
    "punpcklwd %%mm3, %%mm1     \n" /*B*/                               \
    "punpckhwd %%mm3, %%mm2     \n" /*D*/                               \
    "movq %%mm0, %%mm3          \n"                                     \
    "punpcklwd %%mm1, %%mm0     \n" /*A*/                               \
    "punpckhwd %%mm7, %%mm3     \n" /*C*/                               \
    "punpcklwd %%mm2, %%mm7     \n" /*B*/                               \
    "punpckhwd %%mm2, %%mm1     \n" /*D*/                               \
                                                                        \
    "movq %%mm0, " #dst0 "      \n"                                     \
    "movq %%mm7, " #dst1 "      \n"                                     \
    "movq %%mm3, " #dst2 "      \n"                                     \
    "movq %%mm1, " #dst3 "      \n"

    __asm__ volatile(
        "movd %2, %%mm4             \n"
        "movd %3, %%mm5             \n"
        "movd %4, %%mm6             \n"
        "packssdw %%mm4, %%mm4      \n"
        "packssdw %%mm5, %%mm5      \n"
        "packssdw %%mm6, %%mm6      \n"
        "packssdw %%mm4, %%mm4      \n"
        "packssdw %%mm5, %%mm5      \n"
        "packssdw %%mm6, %%mm6      \n"
        REQUANT_CORE(  (%1),  8(%1), 16(%1), 24(%1),  (%0), 8(%0), 64(%0), 72(%0))
        REQUANT_CORE(32(%1), 40(%1), 48(%1), 56(%1),16(%0),24(%0), 48(%0), 56(%0))
        REQUANT_CORE(64(%1), 72(%1), 80(%1), 88(%1),32(%0),40(%0), 96(%0),104(%0))
        REQUANT_CORE(96(%1),104(%1),112(%1),120(%1),80(%0),88(%0),112(%0),120(%0))
        : : "r" (src), "r" (dst), "g" (threshold1+1), "g" (threshold1+5), "g" (threshold1-4) //FIXME maybe more accurate then needed?
    );
    dst[0] = (src[0] + 4) >> 3;
}

static void softthresh_mmx(int16_t dst[64], const int16_t src[64],
                           int qp, const uint8_t *permutation)
{
    int bias = 0; //FIXME
    unsigned int threshold1;

    threshold1 = qp*((1<<4) - bias) - 1;

#undef REQUANT_CORE
#define REQUANT_CORE(dst0, dst1, dst2, dst3, src0, src1, src2, src3)    \
    "movq " #src0 ", %%mm0      \n"                                     \
    "movq " #src1 ", %%mm1      \n"                                     \
    "pxor %%mm6, %%mm6          \n"                                     \
    "pxor %%mm7, %%mm7          \n"                                     \
    "pcmpgtw %%mm0, %%mm6       \n"                                     \
    "pcmpgtw %%mm1, %%mm7       \n"                                     \
    "pxor %%mm6, %%mm0          \n"                                     \
    "pxor %%mm7, %%mm1          \n"                                     \
    "psubusw %%mm4, %%mm0       \n"                                     \
    "psubusw %%mm4, %%mm1       \n"                                     \
    "pxor %%mm6, %%mm0          \n"                                     \
    "pxor %%mm7, %%mm1          \n"                                     \
    "movq " #src2 ", %%mm2      \n"                                     \
    "movq " #src3 ", %%mm3      \n"                                     \
    "pxor %%mm6, %%mm6          \n"                                     \
    "pxor %%mm7, %%mm7          \n"                                     \
    "pcmpgtw %%mm2, %%mm6       \n"                                     \
    "pcmpgtw %%mm3, %%mm7       \n"                                     \
    "pxor %%mm6, %%mm2          \n"                                     \
    "pxor %%mm7, %%mm3          \n"                                     \
    "psubusw %%mm4, %%mm2       \n"                                     \
    "psubusw %%mm4, %%mm3       \n"                                     \
    "pxor %%mm6, %%mm2          \n"                                     \
    "pxor %%mm7, %%mm3          \n"                                     \
                                                                        \
    "paddsw %%mm5, %%mm0        \n"                                     \
    "paddsw %%mm5, %%mm1        \n"                                     \
    "paddsw %%mm5, %%mm2        \n"                                     \
    "paddsw %%mm5, %%mm3        \n"                                     \
    "psraw $3, %%mm0            \n"                                     \
    "psraw $3, %%mm1            \n"                                     \
    "psraw $3, %%mm2            \n"                                     \
    "psraw $3, %%mm3            \n"                                     \
                                                                        \
    "movq %%mm0, %%mm7          \n"                                     \
    "punpcklwd %%mm2, %%mm0     \n" /*A*/                               \
    "punpckhwd %%mm2, %%mm7     \n" /*C*/                               \
    "movq %%mm1, %%mm2          \n"                                     \
    "punpcklwd %%mm3, %%mm1     \n" /*B*/                               \
    "punpckhwd %%mm3, %%mm2     \n" /*D*/                               \
    "movq %%mm0, %%mm3          \n"                                     \
    "punpcklwd %%mm1, %%mm0     \n" /*A*/                               \
    "punpckhwd %%mm7, %%mm3     \n" /*C*/                               \
    "punpcklwd %%mm2, %%mm7     \n" /*B*/                               \
    "punpckhwd %%mm2, %%mm1     \n" /*D*/                               \
                                                                        \
    "movq %%mm0, " #dst0 "      \n"                                     \
    "movq %%mm7, " #dst1 "      \n"                                     \
    "movq %%mm3, " #dst2 "      \n"                                     \
    "movq %%mm1, " #dst3 "      \n"

    __asm__ volatile(
        "movd %2, %%mm4             \n"
        "movd %3, %%mm5             \n"
        "packssdw %%mm4, %%mm4      \n"
        "packssdw %%mm5, %%mm5      \n"
        "packssdw %%mm4, %%mm4      \n"
        "packssdw %%mm5, %%mm5      \n"
        REQUANT_CORE(  (%1),  8(%1), 16(%1), 24(%1),  (%0), 8(%0), 64(%0), 72(%0))
        REQUANT_CORE(32(%1), 40(%1), 48(%1), 56(%1),16(%0),24(%0), 48(%0), 56(%0))
        REQUANT_CORE(64(%1), 72(%1), 80(%1), 88(%1),32(%0),40(%0), 96(%0),104(%0))
        REQUANT_CORE(96(%1),104(%1),112(%1),120(%1),80(%0),88(%0),112(%0),120(%0))
        : : "r" (src), "r" (dst), "g" (threshold1), "rm" (4) //FIXME maybe more accurate then needed?
    );

    dst[0] = (src[0] + 4) >> 3;
}

static void store_slice_mmx(uint8_t *dst, const int16_t *src,
                            int dst_stride, int src_stride,
                            int width, int height, int log2_scale,
                            const uint8_t dither[8][8])
{
    int y;

    for (y = 0; y < height; y++) {
        uint8_t *dst1 = dst;
        const int16_t *src1 = src;
        __asm__ volatile(
            "movq (%3), %%mm3           \n"
            "movq (%3), %%mm4           \n"
            "movd %4, %%mm2             \n"
            "pxor %%mm0, %%mm0          \n"
            "punpcklbw %%mm0, %%mm3     \n"
            "punpckhbw %%mm0, %%mm4     \n"
            "psraw %%mm2, %%mm3         \n"
            "psraw %%mm2, %%mm4         \n"
            "movd %5, %%mm2             \n"
            "1:                         \n"
            "movq (%0), %%mm0           \n"
            "movq 8(%0), %%mm1          \n"
            "paddw %%mm3, %%mm0         \n"
            "paddw %%mm4, %%mm1         \n"
            "psraw %%mm2, %%mm0         \n"
            "psraw %%mm2, %%mm1         \n"
            "packuswb %%mm1, %%mm0      \n"
            "movq %%mm0, (%1)           \n"
            "add $16, %0                \n"
            "add $8, %1                 \n"
            "cmp %2, %1                 \n"
            " jb 1b                     \n"
            : "+r" (src1), "+r"(dst1)
            : "r"(dst + width), "r"(dither[y]), "g"(log2_scale), "g"(MAX_LEVEL - log2_scale)
        );
        src += src_stride;
        dst += dst_stride;
    }
}

#endif /* HAVE_MMX_INLINE */

av_cold void ff_spp_init_x86(SPPContext *s)
{
#if HAVE_MMX_INLINE
    int cpu_flags = av_get_cpu_flags();

    if (cpu_flags & AV_CPU_FLAG_MMX) {
        s->store_slice = store_slice_mmx;
        if (av_get_int(s->dct, "bits_per_sample", NULL) <= 8) {
            switch (s->mode) {
            case 0: s->requantize = hardthresh_mmx; break;
            case 1: s->requantize = softthresh_mmx; break;
            }
        }
    }
#endif
}
