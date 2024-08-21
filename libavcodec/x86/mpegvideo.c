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
#include "libavutil/cpu.h"
#include "libavutil/x86/asm.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/mpegvideo.h"
#include "libavcodec/mpegvideodata.h"

#if HAVE_MMX_INLINE

static void dct_unquantize_h263_intra_mmx(MpegEncContext *s,
                                  int16_t *block, int n, int qscale)
{
    x86_reg level, qmul, qadd, nCoeffs;

    qmul = qscale << 1;

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
    if(s->ac_pred)
        nCoeffs=63;
    else
        nCoeffs= s->intra_scantable.raster_end[ s->block_last_index[n] ];

__asm__ volatile(
                "movd %1, %%mm6                 \n\t" //qmul
                "packssdw %%mm6, %%mm6          \n\t"
                "packssdw %%mm6, %%mm6          \n\t"
                "movd %2, %%mm5                 \n\t" //qadd
                "pxor %%mm7, %%mm7              \n\t"
                "packssdw %%mm5, %%mm5          \n\t"
                "packssdw %%mm5, %%mm5          \n\t"
                "psubw %%mm5, %%mm7             \n\t"
                "pxor %%mm4, %%mm4              \n\t"
                ".p2align 4                     \n\t"
                "1:                             \n\t"
                "movq (%0, %3), %%mm0           \n\t"
                "movq 8(%0, %3), %%mm1          \n\t"

                "pmullw %%mm6, %%mm0            \n\t"
                "pmullw %%mm6, %%mm1            \n\t"

                "movq (%0, %3), %%mm2           \n\t"
                "movq 8(%0, %3), %%mm3          \n\t"

                "pcmpgtw %%mm4, %%mm2           \n\t" // block[i] < 0 ? -1 : 0
                "pcmpgtw %%mm4, %%mm3           \n\t" // block[i] < 0 ? -1 : 0

                "pxor %%mm2, %%mm0              \n\t"
                "pxor %%mm3, %%mm1              \n\t"

                "paddw %%mm7, %%mm0             \n\t"
                "paddw %%mm7, %%mm1             \n\t"

                "pxor %%mm0, %%mm2              \n\t"
                "pxor %%mm1, %%mm3              \n\t"

                "pcmpeqw %%mm7, %%mm0           \n\t" // block[i] == 0 ? -1 : 0
                "pcmpeqw %%mm7, %%mm1           \n\t" // block[i] == 0 ? -1 : 0

                "pandn %%mm2, %%mm0             \n\t"
                "pandn %%mm3, %%mm1             \n\t"

                "movq %%mm0, (%0, %3)           \n\t"
                "movq %%mm1, 8(%0, %3)          \n\t"

                "add $16, %3                    \n\t"
                "jng 1b                         \n\t"
                ::"r" (block+nCoeffs), "rm"(qmul), "rm" (qadd), "r" (2*(-nCoeffs))
                : "memory"
        );
        block[0]= level;
}


static void dct_unquantize_h263_inter_mmx(MpegEncContext *s,
                                  int16_t *block, int n, int qscale)
{
    x86_reg qmul, qadd, nCoeffs;

    qmul = qscale << 1;
    qadd = (qscale - 1) | 1;

    av_assert2(s->block_last_index[n]>=0 || s->h263_aic);

    nCoeffs= s->inter_scantable.raster_end[ s->block_last_index[n] ];

__asm__ volatile(
                "movd %1, %%mm6                 \n\t" //qmul
                "packssdw %%mm6, %%mm6          \n\t"
                "packssdw %%mm6, %%mm6          \n\t"
                "movd %2, %%mm5                 \n\t" //qadd
                "pxor %%mm7, %%mm7              \n\t"
                "packssdw %%mm5, %%mm5          \n\t"
                "packssdw %%mm5, %%mm5          \n\t"
                "psubw %%mm5, %%mm7             \n\t"
                "pxor %%mm4, %%mm4              \n\t"
                ".p2align 4                     \n\t"
                "1:                             \n\t"
                "movq (%0, %3), %%mm0           \n\t"
                "movq 8(%0, %3), %%mm1          \n\t"

                "pmullw %%mm6, %%mm0            \n\t"
                "pmullw %%mm6, %%mm1            \n\t"

                "movq (%0, %3), %%mm2           \n\t"
                "movq 8(%0, %3), %%mm3          \n\t"

                "pcmpgtw %%mm4, %%mm2           \n\t" // block[i] < 0 ? -1 : 0
                "pcmpgtw %%mm4, %%mm3           \n\t" // block[i] < 0 ? -1 : 0

                "pxor %%mm2, %%mm0              \n\t"
                "pxor %%mm3, %%mm1              \n\t"

                "paddw %%mm7, %%mm0             \n\t"
                "paddw %%mm7, %%mm1             \n\t"

                "pxor %%mm0, %%mm2              \n\t"
                "pxor %%mm1, %%mm3              \n\t"

                "pcmpeqw %%mm7, %%mm0           \n\t" // block[i] == 0 ? -1 : 0
                "pcmpeqw %%mm7, %%mm1           \n\t" // block[i] == 0 ? -1 : 0

                "pandn %%mm2, %%mm0             \n\t"
                "pandn %%mm3, %%mm1             \n\t"

                "movq %%mm0, (%0, %3)           \n\t"
                "movq %%mm1, 8(%0, %3)          \n\t"

                "add $16, %3                    \n\t"
                "jng 1b                         \n\t"
                ::"r" (block+nCoeffs), "rm"(qmul), "rm" (qadd), "r" (2*(-nCoeffs))
                : "memory"
        );
}

static void dct_unquantize_mpeg1_intra_mmx(MpegEncContext *s,
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
__asm__ volatile(
                "pcmpeqw %%mm7, %%mm7           \n\t"
                "psrlw $15, %%mm7               \n\t"
                "movd %2, %%mm6                 \n\t"
                "packssdw %%mm6, %%mm6          \n\t"
                "packssdw %%mm6, %%mm6          \n\t"
                "mov %3, %%"FF_REG_a"           \n\t"
                ".p2align 4                     \n\t"
                "1:                             \n\t"
                "movq (%0, %%"FF_REG_a"), %%mm0 \n\t"
                "movq 8(%0, %%"FF_REG_a"), %%mm1\n\t"
                "movq (%1, %%"FF_REG_a"), %%mm4 \n\t"
                "movq 8(%1, %%"FF_REG_a"), %%mm5\n\t"
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
                "pcmpeqw (%0, %%"FF_REG_a"), %%mm4 \n\t" // block[i] == 0 ? -1 : 0
                "pcmpeqw 8(%0, %%"FF_REG_a"), %%mm5\n\t" // block[i] == 0 ? -1 : 0
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
                "movq %%mm4, (%0, %%"FF_REG_a") \n\t"
                "movq %%mm5, 8(%0, %%"FF_REG_a")\n\t"

                "add $16, %%"FF_REG_a"          \n\t"
                "js 1b                          \n\t"
                ::"r" (block+nCoeffs), "r"(quant_matrix+nCoeffs), "rm" (qscale), "g" (-2*nCoeffs)
                : "%"FF_REG_a, "memory"
        );
    block[0]= block0;
}

static void dct_unquantize_mpeg1_inter_mmx(MpegEncContext *s,
                                     int16_t *block, int n, int qscale)
{
    x86_reg nCoeffs;
    const uint16_t *quant_matrix;

    av_assert2(s->block_last_index[n]>=0);

    nCoeffs= s->intra_scantable.raster_end[ s->block_last_index[n] ]+1;

        quant_matrix = s->inter_matrix;
__asm__ volatile(
                "pcmpeqw %%mm7, %%mm7           \n\t"
                "psrlw $15, %%mm7               \n\t"
                "movd %2, %%mm6                 \n\t"
                "packssdw %%mm6, %%mm6          \n\t"
                "packssdw %%mm6, %%mm6          \n\t"
                "mov %3, %%"FF_REG_a"           \n\t"
                ".p2align 4                     \n\t"
                "1:                             \n\t"
                "movq (%0, %%"FF_REG_a"), %%mm0 \n\t"
                "movq 8(%0, %%"FF_REG_a"), %%mm1\n\t"
                "movq (%1, %%"FF_REG_a"), %%mm4 \n\t"
                "movq 8(%1, %%"FF_REG_a"), %%mm5\n\t"
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
                "pcmpeqw (%0, %%"FF_REG_a"), %%mm4 \n\t" // block[i] == 0 ? -1 : 0
                "pcmpeqw 8(%0, %%"FF_REG_a"), %%mm5\n\t" // block[i] == 0 ? -1 : 0
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
                "movq %%mm4, (%0, %%"FF_REG_a") \n\t"
                "movq %%mm5, 8(%0, %%"FF_REG_a")\n\t"

                "add $16, %%"FF_REG_a"          \n\t"
                "js 1b                          \n\t"
                ::"r" (block+nCoeffs), "r"(quant_matrix+nCoeffs), "rm" (qscale), "g" (-2*nCoeffs)
                : "%"FF_REG_a, "memory"
        );
}

static void dct_unquantize_mpeg2_intra_mmx(MpegEncContext *s,
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
__asm__ volatile(
                "pcmpeqw %%mm7, %%mm7           \n\t"
                "psrlw $15, %%mm7               \n\t"
                "movd %2, %%mm6                 \n\t"
                "packssdw %%mm6, %%mm6          \n\t"
                "packssdw %%mm6, %%mm6          \n\t"
                "mov %3, %%"FF_REG_a"           \n\t"
                ".p2align 4                     \n\t"
                "1:                             \n\t"
                "movq (%0, %%"FF_REG_a"), %%mm0 \n\t"
                "movq 8(%0, %%"FF_REG_a"), %%mm1\n\t"
                "movq (%1, %%"FF_REG_a"), %%mm4 \n\t"
                "movq 8(%1, %%"FF_REG_a"), %%mm5\n\t"
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
                "pcmpeqw (%0, %%"FF_REG_a"), %%mm4 \n\t" // block[i] == 0 ? -1 : 0
                "pcmpeqw 8(%0, %%"FF_REG_a"), %%mm5\n\t" // block[i] == 0 ? -1 : 0
                "psraw $4, %%mm0                \n\t"
                "psraw $4, %%mm1                \n\t"
                "pxor %%mm2, %%mm0              \n\t"
                "pxor %%mm3, %%mm1              \n\t"
                "psubw %%mm2, %%mm0             \n\t"
                "psubw %%mm3, %%mm1             \n\t"
                "pandn %%mm0, %%mm4             \n\t"
                "pandn %%mm1, %%mm5             \n\t"
                "movq %%mm4, (%0, %%"FF_REG_a") \n\t"
                "movq %%mm5, 8(%0, %%"FF_REG_a")\n\t"

                "add $16, %%"FF_REG_a"          \n\t"
                "jng 1b                         \n\t"
                ::"r" (block+nCoeffs), "r"(quant_matrix+nCoeffs), "rm" (qscale), "g" (-2*nCoeffs)
                : "%"FF_REG_a, "memory"
        );
    block[0]= block0;
        //Note, we do not do mismatch control for intra as errors cannot accumulate
}

static void dct_unquantize_mpeg2_inter_mmx(MpegEncContext *s,
                                     int16_t *block, int n, int qscale)
{
    x86_reg nCoeffs;
    const uint16_t *quant_matrix;

    av_assert2(s->block_last_index[n]>=0);

    if (s->q_scale_type) qscale = ff_mpeg2_non_linear_qscale[qscale];
    else                 qscale <<= 1;

    nCoeffs= s->intra_scantable.raster_end[ s->block_last_index[n] ];

        quant_matrix = s->inter_matrix;
__asm__ volatile(
                "pcmpeqw %%mm7, %%mm7           \n\t"
                "psrlq $48, %%mm7               \n\t"
                "movd %2, %%mm6                 \n\t"
                "packssdw %%mm6, %%mm6          \n\t"
                "packssdw %%mm6, %%mm6          \n\t"
                "mov %3, %%"FF_REG_a"           \n\t"
                ".p2align 4                     \n\t"
                "1:                             \n\t"
                "movq (%0, %%"FF_REG_a"), %%mm0 \n\t"
                "movq 8(%0, %%"FF_REG_a"), %%mm1\n\t"
                "movq (%1, %%"FF_REG_a"), %%mm4 \n\t"
                "movq 8(%1, %%"FF_REG_a"), %%mm5\n\t"
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
                "pcmpeqw (%0, %%"FF_REG_a"), %%mm4 \n\t" // block[i] == 0 ? -1 : 0
                "pcmpeqw 8(%0, %%"FF_REG_a"), %%mm5\n\t" // block[i] == 0 ? -1 : 0
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
                "movq %%mm4, (%0, %%"FF_REG_a") \n\t"
                "movq %%mm5, 8(%0, %%"FF_REG_a")\n\t"

                "add $16, %%"FF_REG_a"          \n\t"
                "jng 1b                         \n\t"
                "movd 124(%0, %3), %%mm0        \n\t"
                "movq %%mm7, %%mm6              \n\t"
                "psrlq $32, %%mm7               \n\t"
                "pxor %%mm6, %%mm7              \n\t"
                "movq %%mm7, %%mm6              \n\t"
                "psrlq $16, %%mm7               \n\t"
                "pxor %%mm6, %%mm7              \n\t"
                "pslld $31, %%mm7               \n\t"
                "psrlq $15, %%mm7               \n\t"
                "pxor %%mm7, %%mm0              \n\t"
                "movd %%mm0, 124(%0, %3)        \n\t"

                ::"r" (block+nCoeffs), "r"(quant_matrix+nCoeffs), "rm" (qscale), "r" (-2*nCoeffs)
                : "%"FF_REG_a, "memory"
        );
}

#endif /* HAVE_MMX_INLINE */

av_cold void ff_mpv_common_init_x86(MpegEncContext *s)
{
#if HAVE_MMX_INLINE
    int cpu_flags = av_get_cpu_flags();

    if (INLINE_MMX(cpu_flags)) {
        s->dct_unquantize_h263_intra = dct_unquantize_h263_intra_mmx;
        s->dct_unquantize_h263_inter = dct_unquantize_h263_inter_mmx;
        s->dct_unquantize_mpeg1_intra = dct_unquantize_mpeg1_intra_mmx;
        s->dct_unquantize_mpeg1_inter = dct_unquantize_mpeg1_inter_mmx;
        if (!(s->avctx->flags & AV_CODEC_FLAG_BITEXACT))
            s->dct_unquantize_mpeg2_intra = dct_unquantize_mpeg2_intra_mmx;
        s->dct_unquantize_mpeg2_inter = dct_unquantize_mpeg2_inter_mmx;
    }
#endif /* HAVE_MMX_INLINE */
}
