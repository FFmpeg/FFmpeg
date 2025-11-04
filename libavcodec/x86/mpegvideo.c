/*
 * Optimized for ia32 CPUs by Nick Kurshev <nickols_k@mail.ru>
 * H.263, MPEG-1, MPEG-2 dequantizer & draw_edges by Michael Niedermayer <michaelni@gmx.at>
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
#include "libavutil/avassert.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/asm.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/mpegvideo.h"
#include "libavcodec/mpegvideodata.h"
#include "libavcodec/mpegvideo_unquantize.h"

#if HAVE_MMX_INLINE

#define SPLATW(reg) "punpcklwd    %%" #reg ", %%" #reg "\n\t" \
                    "pshufd   $0, %%" #reg ", %%" #reg "\n\t"

#if HAVE_SSSE3_INLINE

static void dct_unquantize_h263_intra_ssse3(const MPVContext *s,
                                            int16_t *block, int n, int qscale)
{
    x86_reg qmul = (unsigned)qscale << 1;
    int level, qadd;

    av_assert2(s->block_last_index[n]>=0 || s->h263_aic);

    if (!s->h263_aic) {
        if (n < 4)
            level = block[0] * s->y_dc_scale;
        else
            level = block[0] * s->c_dc_scale;
        qadd = (qscale - 1) | 1;
    }else{
        qadd = 0;
        level= block[0];
    }
    x86_reg offset = s->ac_pred ? 63 << 1 : s->intra_scantable.raster_end[s->block_last_index[n]] << 1;

__asm__ volatile(
                "movd          %k1, %%xmm0     \n\t" //qmul
                "lea      (%2, %0), %1         \n\t"
                "neg            %0             \n\t"
                "movd           %3, %%xmm1     \n\t" //qadd
                SPLATW(xmm0)
                SPLATW(xmm1)

                ".p2align 4                    \n\t"
                "1:                            \n\t"
                "movdqa   (%1, %0), %%xmm2     \n\t"
                "movdqa 16(%1, %0), %%xmm3     \n\t"

                "movdqa     %%xmm1, %%xmm4     \n\t"
                "movdqa     %%xmm1, %%xmm5     \n\t"

                "psignw     %%xmm2, %%xmm4     \n\t" // sgn(block[i])*qadd
                "psignw     %%xmm3, %%xmm5     \n\t" // sgn(block[i])*qadd

                "pmullw     %%xmm0, %%xmm2     \n\t"
                "pmullw     %%xmm0, %%xmm3     \n\t"

                "paddw      %%xmm4, %%xmm2     \n\t"
                "paddw      %%xmm5, %%xmm3     \n\t"

                "movdqa     %%xmm2, (%1, %0)   \n\t"
                "movdqa     %%xmm3, 16(%1, %0) \n\t"

                "add           $32, %0         \n\t"
                "jng            1b             \n\t"
                : "+r"(offset), "+r"(qmul)
                : "r" (block), "rm" (qadd)
                : XMM_CLOBBERS("%xmm0", "%xmm1", "%xmm2", "%xmm3", "%xmm4", "%xmm5",) "memory"
        );
        block[0]= level;
}


static void dct_unquantize_h263_inter_ssse3(const MPVContext *s,
                                            int16_t *block, int n, int qscale)
{
    int qmul = qscale << 1;
    int qadd = (qscale - 1) | 1;

    av_assert2(s->block_last_index[n]>=0 || s->h263_aic);

    x86_reg offset = s->inter_scantable.raster_end[s->block_last_index[n]] << 1;

__asm__ volatile(
                "movd           %2, %%xmm0     \n\t" //qmul
                "movd           %3, %%xmm1     \n\t" //qadd
                "add            %1, %0         \n\t"
                "neg            %1             \n\t"
                SPLATW(xmm0)
                SPLATW(xmm1)

                ".p2align 4                    \n\t"
                "1:                            \n\t"
                "movdqa   (%0, %1), %%xmm2     \n\t"
                "movdqa 16(%0, %1), %%xmm3     \n\t"

                "movdqa     %%xmm1, %%xmm4     \n\t"
                "movdqa     %%xmm1, %%xmm5     \n\t"

                "psignw     %%xmm2, %%xmm4     \n\t" // sgn(block[i])*qadd
                "psignw     %%xmm3, %%xmm5     \n\t" // sgn(block[i])*qadd

                "pmullw     %%xmm0, %%xmm2     \n\t"
                "pmullw     %%xmm0, %%xmm3     \n\t"

                "paddw      %%xmm4, %%xmm2     \n\t"
                "paddw      %%xmm5, %%xmm3     \n\t"

                "movdqa     %%xmm2, (%0, %1)   \n\t"
                "movdqa     %%xmm3, 16(%0, %1) \n\t"

                "add           $32, %1         \n\t"
                "jng 1b                        \n\t"
                : "+r" (block), "+r" (offset)
                : "rm"(qmul), "rm" (qadd)
                : XMM_CLOBBERS("%xmm0", "%xmm1", "%xmm2", "%xmm3", "%xmm4", "%xmm5",) "memory"
        );
}
#endif

static void dct_unquantize_mpeg1_intra_mmx(const MPVContext *s,
                                           int16_t *block, int n, int qscale)
{
    x86_reg nCoeffs;
    const uint16_t *quant_matrix;
    int block0;

    av_assert2(s->block_last_index[n]>=0);

    nCoeffs= s->intra_scantable.raster_end[ s->block_last_index[n] ]+1;

    if (n < 4)
        block0 = block[0] * s->y_dc_scale;
    else
        block0 = block[0] * s->c_dc_scale;
    /* XXX: only MPEG-1 */
    quant_matrix = s->intra_matrix;
    x86_reg offset = -2 * nCoeffs;
__asm__ volatile(
                "pcmpeqw %%mm7, %%mm7           \n\t"
                "psrlw $15, %%mm7               \n\t"
                "movd %3, %%mm6                 \n\t"
                "packssdw %%mm6, %%mm6          \n\t"
                "packssdw %%mm6, %%mm6          \n\t"
                ".p2align 4                     \n\t"
                "1:                             \n\t"
                "movq (%1, %0), %%mm0           \n\t"
                "movq 8(%1, %0), %%mm1          \n\t"
                "movq (%2, %0), %%mm4           \n\t"
                "movq 8(%2, %0), %%mm5          \n\t"
                "pmullw %%mm6, %%mm4            \n\t" // q=qscale*quant_matrix[i]
                "pmullw %%mm6, %%mm5            \n\t" // q=qscale*quant_matrix[i]
                "pxor %%mm2, %%mm2              \n\t"
                "pxor %%mm3, %%mm3              \n\t"
                "pcmpgtw %%mm0, %%mm2           \n\t" // block[i] < 0 ? -1 : 0
                "pcmpgtw %%mm1, %%mm3           \n\t" // block[i] < 0 ? -1 : 0
                "pxor %%mm2, %%mm0              \n\t"
                "pxor %%mm3, %%mm1              \n\t"
                "psubw %%mm2, %%mm0             \n\t" // abs(block[i])
                "psubw %%mm3, %%mm1             \n\t" // abs(block[i])
                "pmullw %%mm4, %%mm0            \n\t" // abs(block[i])*q
                "pmullw %%mm5, %%mm1            \n\t" // abs(block[i])*q
                "pxor %%mm4, %%mm4              \n\t"
                "pxor %%mm5, %%mm5              \n\t" // FIXME slow
                "pcmpeqw (%1, %0), %%mm4        \n\t" // block[i] == 0 ? -1 : 0
                "pcmpeqw 8(%1, %0), %%mm5       \n\t" // block[i] == 0 ? -1 : 0
                "psraw $3, %%mm0                \n\t"
                "psraw $3, %%mm1                \n\t"
                "psubw %%mm7, %%mm0             \n\t"
                "psubw %%mm7, %%mm1             \n\t"
                "por %%mm7, %%mm0               \n\t"
                "por %%mm7, %%mm1               \n\t"
                "pxor %%mm2, %%mm0              \n\t"
                "pxor %%mm3, %%mm1              \n\t"
                "psubw %%mm2, %%mm0             \n\t"
                "psubw %%mm3, %%mm1             \n\t"
                "pandn %%mm0, %%mm4             \n\t"
                "pandn %%mm1, %%mm5             \n\t"
                "movq %%mm4, (%1, %0)           \n\t"
                "movq %%mm5, 8(%1, %0)          \n\t"

                "add $16, %0                    \n\t"
                "js 1b                          \n\t"
                : "+r" (offset)
                : "r" (block+nCoeffs), "r"(quant_matrix+nCoeffs), "rm" (qscale)
                : "memory"
        );
    block[0]= block0;
}

static void dct_unquantize_mpeg1_inter_mmx(const MPVContext *s,
                                           int16_t *block, int n, int qscale)
{
    x86_reg nCoeffs;
    const uint16_t *quant_matrix;

    av_assert2(s->block_last_index[n]>=0);

    nCoeffs= s->intra_scantable.raster_end[ s->block_last_index[n] ]+1;

        quant_matrix = s->inter_matrix;
    x86_reg offset = -2 * nCoeffs;
__asm__ volatile(
                "pcmpeqw %%mm7, %%mm7           \n\t"
                "psrlw $15, %%mm7               \n\t"
                "movd %3, %%mm6                 \n\t"
                "packssdw %%mm6, %%mm6          \n\t"
                "packssdw %%mm6, %%mm6          \n\t"
                ".p2align 4                     \n\t"
                "1:                             \n\t"
                "movq (%1, %0), %%mm0           \n\t"
                "movq 8(%1, %0), %%mm1          \n\t"
                "movq (%2, %0), %%mm4           \n\t"
                "movq 8(%2, %0), %%mm5          \n\t"
                "pmullw %%mm6, %%mm4            \n\t" // q=qscale*quant_matrix[i]
                "pmullw %%mm6, %%mm5            \n\t" // q=qscale*quant_matrix[i]
                "pxor %%mm2, %%mm2              \n\t"
                "pxor %%mm3, %%mm3              \n\t"
                "pcmpgtw %%mm0, %%mm2           \n\t" // block[i] < 0 ? -1 : 0
                "pcmpgtw %%mm1, %%mm3           \n\t" // block[i] < 0 ? -1 : 0
                "pxor %%mm2, %%mm0              \n\t"
                "pxor %%mm3, %%mm1              \n\t"
                "psubw %%mm2, %%mm0             \n\t" // abs(block[i])
                "psubw %%mm3, %%mm1             \n\t" // abs(block[i])
                "paddw %%mm0, %%mm0             \n\t" // abs(block[i])*2
                "paddw %%mm1, %%mm1             \n\t" // abs(block[i])*2
                "paddw %%mm7, %%mm0             \n\t" // abs(block[i])*2 + 1
                "paddw %%mm7, %%mm1             \n\t" // abs(block[i])*2 + 1
                "pmullw %%mm4, %%mm0            \n\t" // (abs(block[i])*2 + 1)*q
                "pmullw %%mm5, %%mm1            \n\t" // (abs(block[i])*2 + 1)*q
                "pxor %%mm4, %%mm4              \n\t"
                "pxor %%mm5, %%mm5              \n\t" // FIXME slow
                "pcmpeqw (%1, %0), %%mm4        \n\t" // block[i] == 0 ? -1 : 0
                "pcmpeqw 8(%1, %0), %%mm5       \n\t" // block[i] == 0 ? -1 : 0
                "psraw $4, %%mm0                \n\t"
                "psraw $4, %%mm1                \n\t"
                "psubw %%mm7, %%mm0             \n\t"
                "psubw %%mm7, %%mm1             \n\t"
                "por %%mm7, %%mm0               \n\t"
                "por %%mm7, %%mm1               \n\t"
                "pxor %%mm2, %%mm0              \n\t"
                "pxor %%mm3, %%mm1              \n\t"
                "psubw %%mm2, %%mm0             \n\t"
                "psubw %%mm3, %%mm1             \n\t"
                "pandn %%mm0, %%mm4             \n\t"
                "pandn %%mm1, %%mm5             \n\t"
                "movq %%mm4, (%1, %0)           \n\t"
                "movq %%mm5, 8(%1, %0)          \n\t"

                "add $16, %0                    \n\t"
                "js 1b                          \n\t"
                : "+r" (offset)
                : "r" (block+nCoeffs), "r"(quant_matrix+nCoeffs), "rm" (qscale)
                : "memory"
        );
}

static void dct_unquantize_mpeg2_intra_mmx(const MPVContext *s,
                                           int16_t *block, int n, int qscale)
{
    x86_reg nCoeffs;
    const uint16_t *quant_matrix;
    int block0;

    av_assert2(s->block_last_index[n]>=0);

    if (s->q_scale_type) qscale = ff_mpeg2_non_linear_qscale[qscale];
    else                 qscale <<= 1;

    nCoeffs= s->intra_scantable.raster_end[ s->block_last_index[n] ];

    if (n < 4)
        block0 = block[0] * s->y_dc_scale;
    else
        block0 = block[0] * s->c_dc_scale;
    quant_matrix = s->intra_matrix;
    x86_reg offset = -2 * nCoeffs;
__asm__ volatile(
                "movd %3, %%mm6                 \n\t"
                "packssdw %%mm6, %%mm6          \n\t"
                "packssdw %%mm6, %%mm6          \n\t"
                ".p2align 4                     \n\t"
                "1:                             \n\t"
                "movq (%1, %0), %%mm0           \n\t"
                "movq 8(%1, %0), %%mm1          \n\t"
                "movq (%2, %0), %%mm4           \n\t"
                "movq 8(%2, %0), %%mm5          \n\t"
                "pmullw %%mm6, %%mm4            \n\t" // q=qscale*quant_matrix[i]
                "pmullw %%mm6, %%mm5            \n\t" // q=qscale*quant_matrix[i]
                "movq %%mm0, %%mm2              \n\t"
                "movq %%mm1, %%mm3              \n\t"
                "psrlw $12, %%mm2               \n\t" // block[i] < 0 ? 0xf : 0
                "psrlw $12, %%mm3               \n\t" // (block[i] is in the -2048..2047 range)
                "pmullw %%mm4, %%mm0            \n\t" // block[i]*q
                "pmullw %%mm5, %%mm1            \n\t" // block[i]*q
                "paddw %%mm2, %%mm0             \n\t" // bias negative block[i]
                "paddw %%mm3, %%mm1             \n\t" // so that a right-shift
                "psraw $4, %%mm0                \n\t" // is equivalent to divide
                "psraw $4, %%mm1                \n\t" // with rounding towards zero
                "movq %%mm0, (%1, %0)           \n\t"
                "movq %%mm1, 8(%1, %0)          \n\t"

                "add $16, %0                    \n\t"
                "jng 1b                         \n\t"
                : "+r" (offset)
                : "r" (block+nCoeffs), "r"(quant_matrix+nCoeffs), "rm" (qscale)
                : "memory"
        );
    block[0]= block0;
        //Note, we do not do mismatch control for intra as errors cannot accumulate
}

static void dct_unquantize_mpeg2_inter_mmx(const MPVContext *s,
                                           int16_t *block, int n, int qscale)
{
    av_assert2(s->block_last_index[n]>=0);

    x86_reg qscale2 = s->q_scale_type ? ff_mpeg2_non_linear_qscale[qscale] : (unsigned)qscale << 1;
    x86_reg offset  = s->intra_scantable.raster_end[s->block_last_index[n]] << 1;
    const void *quant_matrix = (const char*)s->inter_matrix + offset;


__asm__ volatile(
                "movd          %k1, %%mm6      \n\t"
                "lea      (%2, %0), %1         \n\t"
                "neg            %0             \n\t"
                "pcmpeqw %%mm7, %%mm7           \n\t"
                "psrlq $48, %%mm7               \n\t"
                "packssdw %%mm6, %%mm6          \n\t"
                "packssdw %%mm6, %%mm6          \n\t"
                ".p2align 4                     \n\t"
                "1:                             \n\t"
                "movq     (%1, %0), %%mm0      \n\t"
                "movq    8(%1, %0), %%mm1      \n\t"
                "movq     (%3, %0), %%mm4      \n\t"
                "movq    8(%3, %0), %%mm5      \n\t"
                "pmullw %%mm6, %%mm4            \n\t" // q=qscale*quant_matrix[i]
                "pmullw %%mm6, %%mm5            \n\t" // q=qscale*quant_matrix[i]
                "pxor %%mm2, %%mm2              \n\t"
                "pxor %%mm3, %%mm3              \n\t"
                "pcmpgtw %%mm0, %%mm2           \n\t" // block[i] < 0 ? -1 : 0
                "pcmpgtw %%mm1, %%mm3           \n\t" // block[i] < 0 ? -1 : 0
                "pxor %%mm2, %%mm0              \n\t"
                "pxor %%mm3, %%mm1              \n\t"
                "psubw %%mm2, %%mm0             \n\t" // abs(block[i])
                "psubw %%mm3, %%mm1             \n\t" // abs(block[i])
                "paddw %%mm0, %%mm0             \n\t" // abs(block[i])*2
                "paddw %%mm1, %%mm1             \n\t" // abs(block[i])*2
                "pmullw %%mm4, %%mm0            \n\t" // abs(block[i])*2*q
                "pmullw %%mm5, %%mm1            \n\t" // abs(block[i])*2*q
                "paddw %%mm4, %%mm0             \n\t" // (abs(block[i])*2 + 1)*q
                "paddw %%mm5, %%mm1             \n\t" // (abs(block[i])*2 + 1)*q
                "pxor %%mm4, %%mm4              \n\t"
                "pxor %%mm5, %%mm5              \n\t" // FIXME slow
                "pcmpeqw  (%1, %0), %%mm4      \n\t" // block[i] == 0 ? -1 : 0
                "pcmpeqw 8(%1, %0), %%mm5      \n\t" // block[i] == 0 ? -1 : 0
                "psrlw $5, %%mm0                \n\t"
                "psrlw $5, %%mm1                \n\t"
                "pxor %%mm2, %%mm0              \n\t"
                "pxor %%mm3, %%mm1              \n\t"
                "psubw %%mm2, %%mm0             \n\t"
                "psubw %%mm3, %%mm1             \n\t"
                "pandn %%mm0, %%mm4             \n\t"
                "pandn %%mm1, %%mm5             \n\t"
                "pxor %%mm4, %%mm7              \n\t"
                "pxor %%mm5, %%mm7              \n\t"
                "movq        %%mm4, (%1, %0)   \n\t"
                "movq        %%mm5, 8(%1, %0)  \n\t"

                "add           $16, %0          \n\t"
                "jng 1b                         \n\t"
                "movd      124(%2), %%mm0      \n\t"
                "movq %%mm7, %%mm6              \n\t"
                "psrlq $32, %%mm7               \n\t"
                "pxor %%mm6, %%mm7              \n\t"
                "movq %%mm7, %%mm6              \n\t"
                "psrlq $16, %%mm7               \n\t"
                "pxor %%mm6, %%mm7              \n\t"
                "pslld $31, %%mm7               \n\t"
                "psrlq $15, %%mm7               \n\t"
                "pxor %%mm7, %%mm0              \n\t"
                "movd        %%mm0, 124(%2)    \n\t"

                : "+r"(offset), "+r" (qscale2)
                : "r" (block), "r"(quant_matrix)
                : "memory"
        );
}

#endif /* HAVE_MMX_INLINE */

av_cold void ff_mpv_unquantize_init_x86(MPVUnquantDSPContext *s, int bitexact)
{
#if HAVE_MMX_INLINE
    int cpu_flags = av_get_cpu_flags();

    if (INLINE_MMX(cpu_flags)) {
        s->dct_unquantize_mpeg1_intra = dct_unquantize_mpeg1_intra_mmx;
        s->dct_unquantize_mpeg1_inter = dct_unquantize_mpeg1_inter_mmx;
        if (!bitexact)
            s->dct_unquantize_mpeg2_intra = dct_unquantize_mpeg2_intra_mmx;
        s->dct_unquantize_mpeg2_inter = dct_unquantize_mpeg2_inter_mmx;
    }
#if HAVE_SSSE3_INLINE
    if (INLINE_SSSE3(cpu_flags)) {
        s->dct_unquantize_h263_intra  = dct_unquantize_h263_intra_ssse3;
        s->dct_unquantize_h263_inter  = dct_unquantize_h263_inter_ssse3;
    }
#endif /* HAVE_SSSE3_INLINE */
#endif /* HAVE_MMX_INLINE */
}
