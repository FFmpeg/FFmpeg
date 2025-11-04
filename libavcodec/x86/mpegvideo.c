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

#if HAVE_SSE2_INLINE

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

static void dct_unquantize_mpeg1_intra_ssse3(const MPVContext *s,
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
                "movd           %3, %%xmm6     \n\t"
                "pcmpeqw    %%xmm7, %%xmm7     \n\t"
                "psrlw         $15, %%xmm7     \n\t"
                SPLATW(xmm6)
                ".p2align 4                    \n\t"
                "1:                            \n\t"
                "movdqa   (%2, %0), %%xmm4     \n\t"
                "movdqa 16(%2, %0), %%xmm5     \n\t"
                "movdqa   (%1, %0), %%xmm0     \n\t"
                "movdqa 16(%1, %0), %%xmm1     \n\t"
                "pmullw     %%xmm6, %%xmm4     \n\t" // q=qscale*quant_matrix[i]
                "pmullw     %%xmm6, %%xmm5     \n\t" // q=qscale*quant_matrix[i]
                "pabsw      %%xmm0, %%xmm2     \n\t" // abs(block[i])
                "pabsw      %%xmm1, %%xmm3     \n\t" // abs(block[i])
                "pmullw     %%xmm4, %%xmm2     \n\t" // abs(block[i])*q
                "pmullw     %%xmm5, %%xmm3     \n\t" // abs(block[i])*q
                "psraw          $3, %%xmm2     \n\t"
                "psraw          $3, %%xmm3     \n\t"
                "psubw      %%xmm7, %%xmm2     \n\t"
                "psubw      %%xmm7, %%xmm3     \n\t"
                "por        %%xmm7, %%xmm2     \n\t"
                "por        %%xmm7, %%xmm3     \n\t"
                "psignw     %%xmm0, %%xmm2     \n\t"
                "psignw     %%xmm1, %%xmm3     \n\t"
                "movdqa     %%xmm2, (%1, %0)   \n\t"
                "movdqa     %%xmm3, 16(%1, %0) \n\t"

                "add           $32, %0         \n\t"
                "js 1b                         \n\t"
                : "+r" (offset)
                : "r" (block+nCoeffs), "r"(quant_matrix+nCoeffs), "rm" (qscale)
                : XMM_CLOBBERS("%xmm0", "%xmm1", "%xmm2", "%xmm3", "%xmm4", "%xmm5", "%xmm6", "%xmm7",)
                  "memory"
        );
    block[0]= block0;
}

static void dct_unquantize_mpeg1_inter_ssse3(const MPVContext *s,
                                             int16_t *block, int n, int qscale)
{
    x86_reg nCoeffs;
    const uint16_t *quant_matrix;

    av_assert2(s->block_last_index[n]>=0);

    nCoeffs= s->intra_scantable.raster_end[ s->block_last_index[n] ]+1;

        quant_matrix = s->inter_matrix;
    x86_reg offset = -2 * nCoeffs;
__asm__ volatile(
                "movd           %3, %%xmm6     \n\t"
                "pcmpeqw    %%xmm7, %%xmm7     \n\t"
                "psrlw         $15, %%xmm7     \n\t"
                SPLATW(xmm6)
                ".p2align 4                    \n\t"
                "1:                            \n\t"
                "movdqa   (%2, %0), %%xmm4     \n\t"
                "movdqa 16(%2, %0), %%xmm5     \n\t"
                "movdqa   (%1, %0), %%xmm0     \n\t"
                "movdqa 16(%1, %0), %%xmm1     \n\t"
                "pmullw     %%xmm6, %%xmm4     \n\t" // q=qscale*quant_matrix[i]
                "pmullw     %%xmm6, %%xmm5     \n\t" // q=qscale*quant_matrix[i]
                "pabsw      %%xmm0, %%xmm2     \n\t" // abs(block[i])
                "pabsw      %%xmm1, %%xmm3     \n\t" // abs(block[i])
                "paddw      %%xmm2, %%xmm2     \n\t" // abs(block[i])*2
                "paddw      %%xmm3, %%xmm3     \n\t" // abs(block[i])*2
                "paddw      %%xmm7, %%xmm2     \n\t" // abs(block[i])*2 + 1
                "paddw      %%xmm7, %%xmm3     \n\t" // abs(block[i])*2 + 1
                "pmullw     %%xmm4, %%xmm2     \n\t" // (abs(block[i])*2 + 1)*q
                "pmullw     %%xmm5, %%xmm3     \n\t" // (abs(block[i])*2 + 1)*q
                "psraw          $4, %%xmm2     \n\t"
                "psraw          $4, %%xmm3     \n\t"
                "psubw      %%xmm7, %%xmm2     \n\t"
                "psubw      %%xmm7, %%xmm3     \n\t"
                "por        %%xmm7, %%xmm2     \n\t"
                "por        %%xmm7, %%xmm3     \n\t"
                "psignw     %%xmm0, %%xmm2     \n\t"
                "psignw     %%xmm1, %%xmm3     \n\t"
                "movdqa     %%xmm2, (%1, %0)   \n\t"
                "movdqa     %%xmm3, 16(%1, %0) \n\t"

                "add           $32, %0         \n\t"
                "js 1b                         \n\t"
                : "+r" (offset)
                : "r" (block+nCoeffs), "r"(quant_matrix+nCoeffs), "rm" (qscale)
                : XMM_CLOBBERS("%xmm0", "%xmm1", "%xmm2", "%xmm3", "%xmm4", "%xmm5", "%xmm6", "%xmm7",)
                  "memory"
        );
}

#endif /* HAVE_SSSE3_INLINE */

static void dct_unquantize_mpeg2_intra_sse2(const MPVContext *s,
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
                "movd           %3, %%xmm6     \n\t"
                SPLATW(xmm6)
                ".p2align 4                    \n\t"
                "1:                            \n\t"
                "movdqa   (%1, %0), %%xmm0     \n\t"
                "movdqa 16(%1, %0), %%xmm1     \n\t"
                "movdqa   (%2, %0), %%xmm4     \n\t"
                "movdqa 16(%2, %0), %%xmm5     \n\t"
                "pmullw     %%xmm6, %%xmm4     \n\t" // q=qscale*quant_matrix[i]
                "pmullw     %%xmm6, %%xmm5     \n\t" // q=qscale*quant_matrix[i]
                "movdqa     %%xmm0, %%xmm2     \n\t"
                "movdqa     %%xmm1, %%xmm3     \n\t"
                "psrlw         $12, %%xmm2     \n\t" // block[i] < 0 ? 0xf : 0
                "psrlw         $12, %%xmm3     \n\t" // (block[i] is in the -2048..2047 range)
                "pmullw     %%xmm4, %%xmm0     \n\t" // block[i]*q
                "pmullw     %%xmm5, %%xmm1     \n\t" // block[i]*q
                "paddw      %%xmm2, %%xmm0     \n\t" // bias negative block[i]
                "paddw      %%xmm3, %%xmm1     \n\t" // so that a right-shift
                "psraw          $4, %%xmm0     \n\t" // is equivalent to divide
                "psraw          $4, %%xmm1     \n\t" // with rounding towards zero
                "movdqa     %%xmm0, (%1, %0)   \n\t"
                "movdqa     %%xmm1, 16(%1, %0) \n\t"

                "add           $32, %0         \n\t"
                "jng 1b                        \n\t"
                : "+r" (offset)
                : "r" (block+nCoeffs), "r"(quant_matrix+nCoeffs), "rm" (qscale)
                : XMM_CLOBBERS("%xmm0", "%xmm1", "%xmm2", "%xmm3", "%xmm4", "%xmm5", "%xmm6",)
                  "memory"
        );
    block[0]= block0;
        //Note, we do not do mismatch control for intra as errors cannot accumulate
}

#if HAVE_SSSE3_INLINE

static void dct_unquantize_mpeg2_inter_ssse3(const MPVContext *s,
                                             int16_t *block, int n, int qscale)
{
    av_assert2(s->block_last_index[n]>=0);

    x86_reg qscale2 = s->q_scale_type ? ff_mpeg2_non_linear_qscale[qscale] : (unsigned)qscale << 1;
    x86_reg offset  = s->intra_scantable.raster_end[s->block_last_index[n]] << 1;
    const void *quant_matrix = (const char*)s->inter_matrix + offset;


__asm__ volatile(
                "movd          %k1, %%xmm6     \n\t"
                "lea      (%2, %0), %1         \n\t"
                "neg            %0             \n\t"
                SPLATW(xmm6)
                "pcmpeqw    %%xmm7, %%xmm7     \n\t"
                "psrldq        $14, %%xmm7     \n\t"
                ".p2align 4                    \n\t"
                "1:                            \n\t"
                "movdqa   (%3, %0), %%xmm4     \n\t"
                "movdqa 16(%3, %0), %%xmm5     \n\t"
                "movdqa   (%1, %0), %%xmm0     \n\t"
                "movdqa 16(%1, %0), %%xmm1     \n\t"
                "pmullw     %%xmm6, %%xmm4     \n\t" // q=qscale*quant_matrix[i]
                "pmullw     %%xmm6, %%xmm5     \n\t" // q=qscale*quant_matrix[i]
                "pabsw      %%xmm0, %%xmm2     \n\t" // abs(block[i])
                "pabsw      %%xmm1, %%xmm3     \n\t" // abs(block[i])
                "paddw      %%xmm2, %%xmm2     \n\t" // abs(block[i])*2
                "paddw      %%xmm3, %%xmm3     \n\t" // abs(block[i])*2
                "pmullw     %%xmm4, %%xmm2     \n\t" // abs(block[i])*2*q
                "pmullw     %%xmm5, %%xmm3     \n\t" // abs(block[i])*2*q
                "paddw      %%xmm4, %%xmm2     \n\t" // (abs(block[i])*2 + 1)*q
                "paddw      %%xmm5, %%xmm3     \n\t" // (abs(block[i])*2 + 1)*q
                "psrlw          $5, %%xmm2     \n\t"
                "psrlw          $5, %%xmm3     \n\t"
                "psignw     %%xmm0, %%xmm2     \n\t"
                "psignw     %%xmm1, %%xmm3     \n\t"
                "movdqa     %%xmm2, (%1, %0)   \n\t"
                "movdqa     %%xmm3, 16(%1, %0) \n\t"
                "pxor       %%xmm2, %%xmm7     \n\t"
                "pxor       %%xmm3, %%xmm7     \n\t"

                "add           $32, %0         \n\t"
                "jng 1b                        \n\t"
                "movd      124(%2), %%xmm0     \n\t"
                "movhlps    %%xmm7, %%xmm6     \n\t"
                "pxor       %%xmm6, %%xmm7     \n\t"
                "pshufd $1, %%xmm7, %%xmm6     \n\t"
                "pxor       %%xmm6, %%xmm7     \n\t"
                "pshuflw $1, %%xmm7, %%xmm6    \n\t"
                "pxor       %%xmm6, %%xmm7     \n\t"
                "pslld         $31, %%xmm7     \n\t"
                "psrld         $15, %%xmm7     \n\t"
                "pxor       %%xmm7, %%xmm0     \n\t"
                "movd       %%xmm0, 124(%2)    \n\t"

                : "+r"(offset), "+r" (qscale2)
                : "r" (block), "r"(quant_matrix)
                : XMM_CLOBBERS("%xmm0", "%xmm1", "%xmm2", "%xmm3", "%xmm4", "%xmm5", "%xmm6", "%xmm7",)
                  "memory"
        );
}

#endif /* HAVE_SSSE3_INLINE */
#endif /* HAVE_SSE2_INLINE */

av_cold void ff_mpv_unquantize_init_x86(MPVUnquantDSPContext *s, int bitexact)
{
#if HAVE_SSE2_INLINE
    int cpu_flags = av_get_cpu_flags();

    if (INLINE_SSE2(cpu_flags)) {
        if (!bitexact)
            s->dct_unquantize_mpeg2_intra = dct_unquantize_mpeg2_intra_sse2;
    }
#if HAVE_SSSE3_INLINE
    if (INLINE_SSSE3(cpu_flags)) {
        s->dct_unquantize_h263_intra  = dct_unquantize_h263_intra_ssse3;
        s->dct_unquantize_h263_inter  = dct_unquantize_h263_inter_ssse3;
        s->dct_unquantize_mpeg1_intra = dct_unquantize_mpeg1_intra_ssse3;
        s->dct_unquantize_mpeg1_inter = dct_unquantize_mpeg1_inter_ssse3;
        s->dct_unquantize_mpeg2_inter = dct_unquantize_mpeg2_inter_ssse3;
    }
#endif /* HAVE_SSSE3_INLINE */
#endif /* HAVE_SSE2_INLINE */
}
