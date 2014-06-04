/*
 * MMX optimized DSP utils
 * Copyright (c) 2000, 2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
 *
 * MMX optimization by Nick Kurshev <nickols_k@mail.ru>
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
#include "libavcodec/dct.h"
#include "libavcodec/dsputil.h"
#include "libavcodec/mpegvideo.h"
#include "dsputil_x86.h"

void ff_get_pixels_mmx(int16_t *block, const uint8_t *pixels, int line_size);
void ff_get_pixels_sse2(int16_t *block, const uint8_t *pixels, int line_size);
void ff_diff_pixels_mmx(int16_t *block, const uint8_t *s1, const uint8_t *s2,
                        int stride);
void ff_diff_pixels_sse2(int16_t *block, const uint8_t *s1, const uint8_t *s2,
                         int stride);
int ff_pix_sum16_mmx(uint8_t *pix, int line_size);
int ff_pix_sum16_sse2(uint8_t *pix, int line_size);
int ff_pix_sum16_xop(uint8_t *pix, int line_size);
int ff_pix_norm1_mmx(uint8_t *pix, int line_size);
int ff_pix_norm1_sse2(uint8_t *pix, int line_size);
int ff_sum_abs_dctelem_mmx(int16_t *block);
int ff_sum_abs_dctelem_mmxext(int16_t *block);
int ff_sum_abs_dctelem_sse2(int16_t *block);
int ff_sum_abs_dctelem_ssse3(int16_t *block);
int ff_sse8_mmx(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                int line_size, int h);
int ff_sse16_mmx(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                 int line_size, int h);
int ff_sse16_sse2(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                  int line_size, int h);
int ff_hf_noise8_mmx(uint8_t *pix1, int lsize, int h);
int ff_hf_noise16_mmx(uint8_t *pix1, int lsize, int h);

#define hadamard_func(cpu)                                              \
    int ff_hadamard8_diff_ ## cpu(MpegEncContext *s, uint8_t *src1,     \
                                  uint8_t *src2, int stride, int h);    \
    int ff_hadamard8_diff16_ ## cpu(MpegEncContext *s, uint8_t *src1,   \
                                    uint8_t *src2, int stride, int h);

hadamard_func(mmx)
hadamard_func(mmxext)
hadamard_func(sse2)
hadamard_func(ssse3)

#if HAVE_YASM

static int nsse16_mmx(MpegEncContext *c, uint8_t *pix1, uint8_t *pix2,
                      int line_size, int h)
{
    int score1, score2;

    if (c)
        score1 = c->dsp.sse[0](c, pix1, pix2, line_size, h);
    else
        score1 = ff_sse16_mmx(c, pix1, pix2, line_size, h);
    score2 = ff_hf_noise16_mmx(pix1, line_size, h) + ff_hf_noise8_mmx(pix1+8, line_size, h)
           - ff_hf_noise16_mmx(pix2, line_size, h) - ff_hf_noise8_mmx(pix2+8, line_size, h);

    if (c)
        return score1 + FFABS(score2) * c->avctx->nsse_weight;
    else
        return score1 + FFABS(score2) * 8;
}

static int nsse8_mmx(MpegEncContext *c, uint8_t *pix1, uint8_t *pix2,
                     int line_size, int h)
{
    int score1 = ff_sse8_mmx(c, pix1, pix2, line_size, h);
    int score2 = ff_hf_noise8_mmx(pix1, line_size, h) -
                 ff_hf_noise8_mmx(pix2, line_size, h);

    if (c)
        return score1 + FFABS(score2) * c->avctx->nsse_weight;
    else
        return score1 + FFABS(score2) * 8;
}

#endif /* HAVE_YASM */

#if HAVE_INLINE_ASM

static int vsad_intra16_mmx(MpegEncContext *v, uint8_t *pix, uint8_t *dummy,
                            int line_size, int h)
{
    int tmp;

    av_assert2((((int) pix) & 7) == 0);
    av_assert2((line_size & 7) == 0);

#define SUM(in0, in1, out0, out1)               \
    "movq (%0), %%mm2\n"                        \
    "movq 8(%0), %%mm3\n"                       \
    "add %2,%0\n"                               \
    "movq %%mm2, " #out0 "\n"                   \
    "movq %%mm3, " #out1 "\n"                   \
    "psubusb " #in0 ", %%mm2\n"                 \
    "psubusb " #in1 ", %%mm3\n"                 \
    "psubusb " #out0 ", " #in0 "\n"             \
    "psubusb " #out1 ", " #in1 "\n"             \
    "por %%mm2, " #in0 "\n"                     \
    "por %%mm3, " #in1 "\n"                     \
    "movq " #in0 ", %%mm2\n"                    \
    "movq " #in1 ", %%mm3\n"                    \
    "punpcklbw %%mm7, " #in0 "\n"               \
    "punpcklbw %%mm7, " #in1 "\n"               \
    "punpckhbw %%mm7, %%mm2\n"                  \
    "punpckhbw %%mm7, %%mm3\n"                  \
    "paddw " #in1 ", " #in0 "\n"                \
    "paddw %%mm3, %%mm2\n"                      \
    "paddw %%mm2, " #in0 "\n"                   \
    "paddw " #in0 ", %%mm6\n"


    __asm__ volatile (
        "movl    %3, %%ecx\n"
        "pxor %%mm6, %%mm6\n"
        "pxor %%mm7, %%mm7\n"
        "movq  (%0), %%mm0\n"
        "movq 8(%0), %%mm1\n"
        "add %2, %0\n"
        "jmp 2f\n"
        "1:\n"

        SUM(%%mm4, %%mm5, %%mm0, %%mm1)
        "2:\n"
        SUM(%%mm0, %%mm1, %%mm4, %%mm5)

        "subl $2, %%ecx\n"
        "jnz 1b\n"

        "movq  %%mm6, %%mm0\n"
        "psrlq $32,   %%mm6\n"
        "paddw %%mm6, %%mm0\n"
        "movq  %%mm0, %%mm6\n"
        "psrlq $16,   %%mm0\n"
        "paddw %%mm6, %%mm0\n"
        "movd  %%mm0, %1\n"
        : "+r" (pix), "=r" (tmp)
        : "r" ((x86_reg) line_size), "m" (h)
        : "%ecx");

    return tmp & 0xFFFF;
}
#undef SUM

static int vsad_intra16_mmxext(MpegEncContext *v, uint8_t *pix, uint8_t *dummy,
                               int line_size, int h)
{
    int tmp;

    av_assert2((((int) pix) & 7) == 0);
    av_assert2((line_size & 7) == 0);

#define SUM(in0, in1, out0, out1)               \
    "movq (%0), " #out0 "\n"                    \
    "movq 8(%0), " #out1 "\n"                   \
    "add %2, %0\n"                              \
    "psadbw " #out0 ", " #in0 "\n"              \
    "psadbw " #out1 ", " #in1 "\n"              \
    "paddw " #in1 ", " #in0 "\n"                \
    "paddw " #in0 ", %%mm6\n"

    __asm__ volatile (
        "movl %3, %%ecx\n"
        "pxor %%mm6, %%mm6\n"
        "pxor %%mm7, %%mm7\n"
        "movq (%0), %%mm0\n"
        "movq 8(%0), %%mm1\n"
        "add %2, %0\n"
        "jmp 2f\n"
        "1:\n"

        SUM(%%mm4, %%mm5, %%mm0, %%mm1)
        "2:\n"
        SUM(%%mm0, %%mm1, %%mm4, %%mm5)

        "subl $2, %%ecx\n"
        "jnz 1b\n"

        "movd %%mm6, %1\n"
        : "+r" (pix), "=r" (tmp)
        : "r" ((x86_reg) line_size), "m" (h)
        : "%ecx");

    return tmp;
}
#undef SUM

static int vsad16_mmx(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                      int line_size, int h)
{
    int tmp;

    av_assert2((((int) pix1) & 7) == 0);
    av_assert2((((int) pix2) & 7) == 0);
    av_assert2((line_size & 7) == 0);

#define SUM(in0, in1, out0, out1)       \
    "movq (%0), %%mm2\n"                \
    "movq (%1), " #out0 "\n"            \
    "movq 8(%0), %%mm3\n"               \
    "movq 8(%1), " #out1 "\n"           \
    "add %3, %0\n"                      \
    "add %3, %1\n"                      \
    "psubb " #out0 ", %%mm2\n"          \
    "psubb " #out1 ", %%mm3\n"          \
    "pxor %%mm7, %%mm2\n"               \
    "pxor %%mm7, %%mm3\n"               \
    "movq %%mm2, " #out0 "\n"           \
    "movq %%mm3, " #out1 "\n"           \
    "psubusb " #in0 ", %%mm2\n"         \
    "psubusb " #in1 ", %%mm3\n"         \
    "psubusb " #out0 ", " #in0 "\n"     \
    "psubusb " #out1 ", " #in1 "\n"     \
    "por %%mm2, " #in0 "\n"             \
    "por %%mm3, " #in1 "\n"             \
    "movq " #in0 ", %%mm2\n"            \
    "movq " #in1 ", %%mm3\n"            \
    "punpcklbw %%mm7, " #in0 "\n"       \
    "punpcklbw %%mm7, " #in1 "\n"       \
    "punpckhbw %%mm7, %%mm2\n"          \
    "punpckhbw %%mm7, %%mm3\n"          \
    "paddw " #in1 ", " #in0 "\n"        \
    "paddw %%mm3, %%mm2\n"              \
    "paddw %%mm2, " #in0 "\n"           \
    "paddw " #in0 ", %%mm6\n"


    __asm__ volatile (
        "movl %4, %%ecx\n"
        "pxor %%mm6, %%mm6\n"
        "pcmpeqw %%mm7, %%mm7\n"
        "psllw $15, %%mm7\n"
        "packsswb %%mm7, %%mm7\n"
        "movq (%0), %%mm0\n"
        "movq (%1), %%mm2\n"
        "movq 8(%0), %%mm1\n"
        "movq 8(%1), %%mm3\n"
        "add %3, %0\n"
        "add %3, %1\n"
        "psubb %%mm2, %%mm0\n"
        "psubb %%mm3, %%mm1\n"
        "pxor %%mm7, %%mm0\n"
        "pxor %%mm7, %%mm1\n"
        "jmp 2f\n"
        "1:\n"

        SUM(%%mm4, %%mm5, %%mm0, %%mm1)
        "2:\n"
        SUM(%%mm0, %%mm1, %%mm4, %%mm5)

        "subl $2, %%ecx\n"
        "jnz 1b\n"

        "movq %%mm6, %%mm0\n"
        "psrlq $32, %%mm6\n"
        "paddw %%mm6, %%mm0\n"
        "movq %%mm0, %%mm6\n"
        "psrlq $16, %%mm0\n"
        "paddw %%mm6, %%mm0\n"
        "movd %%mm0, %2\n"
        : "+r" (pix1), "+r" (pix2), "=r" (tmp)
        : "r" ((x86_reg) line_size), "m" (h)
        : "%ecx");

    return tmp & 0x7FFF;
}
#undef SUM

static int vsad16_mmxext(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                         int line_size, int h)
{
    int tmp;

    av_assert2((((int) pix1) & 7) == 0);
    av_assert2((((int) pix2) & 7) == 0);
    av_assert2((line_size & 7) == 0);

#define SUM(in0, in1, out0, out1)               \
    "movq (%0), " #out0 "\n"                    \
    "movq (%1), %%mm2\n"                        \
    "movq 8(%0), " #out1 "\n"                   \
    "movq 8(%1), %%mm3\n"                       \
    "add %3, %0\n"                              \
    "add %3, %1\n"                              \
    "psubb %%mm2, " #out0 "\n"                  \
    "psubb %%mm3, " #out1 "\n"                  \
    "pxor %%mm7, " #out0 "\n"                   \
    "pxor %%mm7, " #out1 "\n"                   \
    "psadbw " #out0 ", " #in0 "\n"              \
    "psadbw " #out1 ", " #in1 "\n"              \
    "paddw " #in1 ", " #in0 "\n"                \
    "paddw " #in0 ", %%mm6\n    "

    __asm__ volatile (
        "movl %4, %%ecx\n"
        "pxor %%mm6, %%mm6\n"
        "pcmpeqw %%mm7, %%mm7\n"
        "psllw $15, %%mm7\n"
        "packsswb %%mm7, %%mm7\n"
        "movq (%0), %%mm0\n"
        "movq (%1), %%mm2\n"
        "movq 8(%0), %%mm1\n"
        "movq 8(%1), %%mm3\n"
        "add %3, %0\n"
        "add %3, %1\n"
        "psubb %%mm2, %%mm0\n"
        "psubb %%mm3, %%mm1\n"
        "pxor %%mm7, %%mm0\n"
        "pxor %%mm7, %%mm1\n"
        "jmp 2f\n"
        "1:\n"

        SUM(%%mm4, %%mm5, %%mm0, %%mm1)
        "2:\n"
        SUM(%%mm0, %%mm1, %%mm4, %%mm5)

        "subl $2, %%ecx\n"
        "jnz 1b\n"

        "movd %%mm6, %2\n"
        : "+r" (pix1), "+r" (pix2), "=r" (tmp)
        : "r" ((x86_reg) line_size), "m" (h)
        : "%ecx");

    return tmp;
}
#undef SUM


#define PHADDD(a, t)                            \
    "movq  " #a ", " #t "               \n\t"   \
    "psrlq    $32, " #a "               \n\t"   \
    "paddd " #t ", " #a "               \n\t"

/*
 * pmulhw:   dst[0 - 15] = (src[0 - 15] * dst[0 - 15])[16 - 31]
 * pmulhrw:  dst[0 - 15] = (src[0 - 15] * dst[0 - 15] + 0x8000)[16 - 31]
 * pmulhrsw: dst[0 - 15] = (src[0 - 15] * dst[0 - 15] + 0x4000)[15 - 30]
 */
#define PMULHRW(x, y, s, o)                     \
    "pmulhw " #s ", " #x "              \n\t"   \
    "pmulhw " #s ", " #y "              \n\t"   \
    "paddw  " #o ", " #x "              \n\t"   \
    "paddw  " #o ", " #y "              \n\t"   \
    "psraw      $1, " #x "              \n\t"   \
    "psraw      $1, " #y "              \n\t"
#define DEF(x) x ## _mmx
#define SET_RND MOVQ_WONE
#define SCALE_OFFSET 1

#include "dsputil_qns_template.c"

#undef DEF
#undef SET_RND
#undef SCALE_OFFSET
#undef PMULHRW

#define DEF(x) x ## _3dnow
#define SET_RND(x)
#define SCALE_OFFSET 0
#define PMULHRW(x, y, s, o)                     \
    "pmulhrw " #s ", " #x "             \n\t"   \
    "pmulhrw " #s ", " #y "             \n\t"

#include "dsputil_qns_template.c"

#undef DEF
#undef SET_RND
#undef SCALE_OFFSET
#undef PMULHRW

#if HAVE_SSSE3_INLINE
#undef PHADDD
#define DEF(x) x ## _ssse3
#define SET_RND(x)
#define SCALE_OFFSET -1

#define PHADDD(a, t)                            \
    "pshufw $0x0E, " #a ", " #t "       \n\t"   \
    /* faster than phaddd on core2 */           \
    "paddd " #t ", " #a "               \n\t"

#define PMULHRW(x, y, s, o)                     \
    "pmulhrsw " #s ", " #x "            \n\t"   \
    "pmulhrsw " #s ", " #y "            \n\t"

#include "dsputil_qns_template.c"

#undef DEF
#undef SET_RND
#undef SCALE_OFFSET
#undef PMULHRW
#undef PHADDD
#endif /* HAVE_SSSE3_INLINE */

#endif /* HAVE_INLINE_ASM */

av_cold void ff_dsputilenc_init_mmx(DSPContext *c, AVCodecContext *avctx,
                                    unsigned high_bit_depth)
{
    int cpu_flags = av_get_cpu_flags();
    const int dct_algo = avctx->dct_algo;

    if (EXTERNAL_MMX(cpu_flags)) {
        if (!high_bit_depth)
            c->get_pixels = ff_get_pixels_mmx;
        c->diff_pixels = ff_diff_pixels_mmx;
        c->pix_sum     = ff_pix_sum16_mmx;
        c->pix_norm1   = ff_pix_norm1_mmx;
    }

    if (EXTERNAL_SSE2(cpu_flags))
        if (!high_bit_depth)
            c->get_pixels = ff_get_pixels_sse2;

#if HAVE_INLINE_ASM
    if (INLINE_MMX(cpu_flags)) {
        if (!high_bit_depth &&
            (dct_algo == FF_DCT_AUTO || dct_algo == FF_DCT_MMX))
            c->fdct = ff_fdct_mmx;

        c->vsad[4] = vsad_intra16_mmx;

        if (!(avctx->flags & CODEC_FLAG_BITEXACT)) {
            c->vsad[0]      = vsad16_mmx;
            c->try_8x8basis = try_8x8basis_mmx;
        }
        c->add_8x8basis = add_8x8basis_mmx;
    }

    if (INLINE_AMD3DNOW(cpu_flags)) {
        if (!(avctx->flags & CODEC_FLAG_BITEXACT)) {
            c->try_8x8basis = try_8x8basis_3dnow;
        }
        c->add_8x8basis = add_8x8basis_3dnow;
    }

    if (INLINE_MMXEXT(cpu_flags)) {
        if (!high_bit_depth &&
            (dct_algo == FF_DCT_AUTO || dct_algo == FF_DCT_MMX))
            c->fdct = ff_fdct_mmxext;

        c->vsad[4]         = vsad_intra16_mmxext;

        if (!(avctx->flags & CODEC_FLAG_BITEXACT)) {
            c->vsad[0] = vsad16_mmxext;
        }
    }

    if (INLINE_SSE2(cpu_flags)) {
        if (!high_bit_depth &&
            (dct_algo == FF_DCT_AUTO || dct_algo == FF_DCT_MMX))
            c->fdct = ff_fdct_sse2;
    }

#if HAVE_SSSE3_INLINE
    if (INLINE_SSSE3(cpu_flags)) {
        if (!(avctx->flags & CODEC_FLAG_BITEXACT)) {
            c->try_8x8basis = try_8x8basis_ssse3;
        }
        c->add_8x8basis    = add_8x8basis_ssse3;
    }
#endif
#endif /* HAVE_INLINE_ASM */

    if (EXTERNAL_MMX(cpu_flags)) {
        c->hadamard8_diff[0] = ff_hadamard8_diff16_mmx;
        c->hadamard8_diff[1] = ff_hadamard8_diff_mmx;
        c->sum_abs_dctelem   = ff_sum_abs_dctelem_mmx;
        c->sse[0]            = ff_sse16_mmx;
        c->sse[1]            = ff_sse8_mmx;
#if HAVE_YASM
        c->nsse[0]           = nsse16_mmx;
        c->nsse[1]           = nsse8_mmx;
#endif
    }

    if (EXTERNAL_MMXEXT(cpu_flags)) {
        c->hadamard8_diff[0] = ff_hadamard8_diff16_mmxext;
        c->hadamard8_diff[1] = ff_hadamard8_diff_mmxext;
        c->sum_abs_dctelem   = ff_sum_abs_dctelem_mmxext;
    }

    if (EXTERNAL_SSE2(cpu_flags)) {
        c->sse[0] = ff_sse16_sse2;
        c->sum_abs_dctelem   = ff_sum_abs_dctelem_sse2;
        c->diff_pixels = ff_diff_pixels_sse2;
        c->pix_sum     = ff_pix_sum16_sse2;
        c->pix_norm1   = ff_pix_norm1_sse2;

#if HAVE_ALIGNED_STACK
        c->hadamard8_diff[0] = ff_hadamard8_diff16_sse2;
        c->hadamard8_diff[1] = ff_hadamard8_diff_sse2;
#endif
    }

    if (EXTERNAL_SSSE3(cpu_flags)) {
        c->sum_abs_dctelem   = ff_sum_abs_dctelem_ssse3;
#if HAVE_ALIGNED_STACK
        c->hadamard8_diff[0] = ff_hadamard8_diff16_ssse3;
        c->hadamard8_diff[1] = ff_hadamard8_diff_ssse3;
#endif
    }

    if (EXTERNAL_XOP(cpu_flags)) {
        c->pix_sum           = ff_pix_sum16_xop;
    }

    ff_dsputil_init_pix_mmx(c, avctx);
}
