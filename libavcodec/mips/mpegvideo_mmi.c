/*
 * Loongson SIMD optimized mpegvideo
 *
 * Copyright (c) 2015 Loongson Technology Corporation Limited
 * Copyright (c) 2015 Zhou Xiaoyong <zhouxiaoyong@loongson.cn>
 *                    Zhang Shuangshuang <zhangshuangshuang@ict.ac.cn>
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

#include "mpegvideo_mips.h"

void ff_dct_unquantize_h263_intra_mmi(MpegEncContext *s, int16_t *block,
        int n, int qscale)
{
    int64_t level, qmul, qadd, nCoeffs;

    qmul = qscale << 1;
    av_assert2(s->block_last_index[n]>=0 || s->h263_aic);

    if (!s->h263_aic) {
        if (n<4)
            level = block[0] * s->y_dc_scale;
        else
            level = block[0] * s->c_dc_scale;
        qadd = (qscale-1) | 1;
    } else {
        qadd = 0;
        level = block[0];
    }

    if(s->ac_pred)
        nCoeffs = 63;
    else
        nCoeffs = s->inter_scantable.raster_end[s->block_last_index[n]];

    __asm__ volatile (
        "xor $f12, $f12, $f12           \r\n"
        "lwc1 $f12, %1                  \n\r"
        "xor $f10, $f10, $f10           \r\n"
        "lwc1 $f10, %2                  \r\n"
        "xor $f14, $f14, $f14           \r\n"
        "packsswh $f12, $f12, $f12      \r\n"
        "packsswh $f12, $f12, $f12      \r\n"
        "packsswh $f10, $f10, $f10      \r\n"
        "packsswh $f10, $f10, $f10      \r\n"
        "psubh $f14, $f14, $f10         \r\n"
        "xor $f8, $f8, $f8              \r\n"
        ".p2align 4                     \r\n"
        "1:                             \r\n"
        "daddu $8, %0, %3               \r\n"
        "gsldlc1 $f0, 7($8)             \r\n"
        "gsldrc1 $f0, 0($8)             \r\n"
        "gsldlc1 $f2, 15($8)            \r\n"
        "gsldrc1 $f2, 8($8)             \r\n"
        "mov.d $f4, $f0                 \r\n"
        "mov.d $f6, $f2                 \r\n"
        "pmullh $f0, $f0, $f12          \r\n"
        "pmullh $f2, $f2, $f12          \r\n"
        "pcmpgth $f4, $f4, $f8          \r\n"
        "pcmpgth $f6, $f6, $f8          \r\n"
        "xor $f0, $f0, $f4              \r\n"
        "xor $f2, $f2, $f6              \r\n"
        "paddh $f0, $f0, $f14           \r\n"
        "paddh $f2, $f2, $f14           \r\n"
        "xor $f4, $f4, $f0              \r\n"
        "xor $f6, $f6, $f2              \r\n"
        "pcmpeqh $f0, $f0, $f14         \r\n"
        "pcmpeqh $f2, $f2, $f14         \r\n"
        "pandn $f0, $f0, $f4            \r\n"
        "pandn $f2, $f2, $f6            \r\n"
        "gssdlc1 $f0, 7($8)             \r\n"
        "gssdrc1 $f0, 0($8)             \r\n"
        "gssdlc1 $f2, 15($8)            \r\n"
        "gssdrc1 $f2, 8($8)             \r\n"
        "addi %3, %3, 16                \r\n"
        "blez %3, 1b                    \r\n"
        ::"r"(block+nCoeffs),"m"(qmul),"m"(qadd),"r"(2*(-nCoeffs))
        :"$8","memory"
    );

    block[0] = level;
}

void ff_dct_unquantize_h263_inter_mmi(MpegEncContext *s, int16_t *block,
        int n, int qscale)
{
    int64_t qmul, qadd, nCoeffs;

    qmul = qscale << 1;
    qadd = (qscale - 1) | 1;
    av_assert2(s->block_last_index[n]>=0 || s->h263_aic);
    nCoeffs = s->inter_scantable.raster_end[s->block_last_index[n]];

    __asm__ volatile (
        "xor $f12, $f12, $f12           \r\n"
        "lwc1 $f12, %1                  \r\n"
        "xor $f10, $f10, $f10           \r\n"
        "lwc1 $f10, %2                  \r\n"
        "packsswh $f12, $f12, $f12      \r\n"
        "packsswh $f12, $f12, $f12      \r\n"
        "xor $f14, $f14, $f14           \r\n"
        "packsswh $f10, $f10, $f10      \r\n"
        "packsswh $f10, $f10, $f10      \r\n"
        "psubh $f14, $f14, $f10         \r\n"
        "xor $f8, $f8, $f8              \r\n"
        ".p2align 4                     \r\n"
        "1:                             \r\n"
        "daddu $8, %0, %3               \r\n"
        "gsldlc1 $f0, 7($8)             \r\n"
        "gsldrc1 $f0, 0($8)             \r\n"
        "gsldlc1 $f2, 15($8)            \r\n"
        "gsldrc1 $f2, 8($8)             \r\n"
        "mov.d $f4, $f0                 \r\n"
        "mov.d $f6, $f2                 \r\n"
        "pmullh $f0, $f0, $f12          \r\n"
        "pmullh $f2, $f2, $f12          \r\n"
        "pcmpgth $f4, $f4, $f8          \r\n"
        "pcmpgth $f6, $f6, $f8          \r\n"
        "xor $f0, $f0, $f4              \r\n"
        "xor $f2, $f2, $f6              \r\n"
        "paddh $f0, $f0, $f14           \r\n"
        "paddh $f2, $f2, $f14           \r\n"
        "xor $f4, $f4, $f0              \r\n"
        "xor $f6, $f6, $f2              \r\n"
        "pcmpeqh $f0, $f0, $f14         \r\n"
        "pcmpeqh $f2, $f2, $f14         \r\n"
        "pandn $f0, $f0, $f4            \r\n"
        "pandn $f2, $f2, $f6            \r\n"
        "gssdlc1 $f0, 7($8)             \r\n"
        "gssdrc1 $f0, 0($8)             \r\n"
        "gssdlc1 $f2, 15($8)            \r\n"
        "gssdrc1 $f2, 8($8)             \r\n"
        "addi %3, %3, 16                \r\n"
        "blez %3, 1b                    \r\n"
        ::"r"(block+nCoeffs),"m"(qmul),"m"(qadd),"r"(2*(-nCoeffs))
        : "$8","memory"
    );
}

void ff_dct_unquantize_mpeg1_intra_mmi(MpegEncContext *s, int16_t *block,
        int n, int qscale)
{
    int64_t nCoeffs;
    const uint16_t *quant_matrix;
    int block0;

    av_assert2(s->block_last_index[n]>=0);
    nCoeffs = s->intra_scantable.raster_end[s->block_last_index[n]] + 1;

    if (n<4)
        block0 = block[0] * s->y_dc_scale;
    else
        block0 = block[0] * s->c_dc_scale;

    /* XXX: only mpeg1 */
    quant_matrix = s->intra_matrix;

    __asm__ volatile (
        "pcmpeqh $f14, $f14, $f14       \r\n"
        "dli $10, 15                    \r\n"
        "dmtc1 $10, $f16                \r\n"
        "xor $f12, $f12, $f12           \r\n"
        "lwc1 $f12, %2                  \r\n"
        "psrlh $f14, $f14, $f16         \r\n"
        "packsswh $f12, $f12, $f12      \r\n"
        "packsswh $f12, $f12, $f12      \r\n"
        "or $8, %3, $0                  \r\n"
        ".p2align 4                     \r\n"
        "1:                             \r\n"
        "gsldxc1 $f0, 0($8, %0)         \r\n"
        "gsldxc1 $f2, 8($8, %0)         \r\n"
        "mov.d $f16, $f0                \r\n"
        "mov.d $f18, $f2                \r\n"
        "gsldxc1 $f8, 0($8, %1)         \r\n"
        "gsldxc1 $f10, 8($8, %1)        \r\n"
        "pmullh $f8, $f8, $f12          \r\n"
        "pmullh $f10, $f10, $f12        \r\n"
        "xor $f4, $f4, $f4              \r\n"
        "xor $f6, $f6, $f6              \r\n"
        "pcmpgth $f4, $f4, $f0          \r\n"
        "pcmpgth $f6, $f6, $f2          \r\n"
        "xor $f0, $f0, $f4              \r\n"
        "xor $f2, $f2, $f6              \r\n"
        "psubh $f0, $f0, $f4            \r\n"
        "psubh $f2, $f2, $f6            \r\n"
        "pmullh $f0, $f0, $f8           \r\n"
        "pmullh $f2, $f2, $f10          \r\n"
        "xor $f8, $f8, $f8              \r\n"
        "xor $f10, $f10, $f10           \r\n"
        "pcmpeqh $f8, $f8, $f16         \r\n"
        "pcmpeqh $f10, $f10, $f18       \r\n"
        "dli $10, 3                     \r\n"
        "dmtc1 $10, $f16                \r\n"
        "psrah $f0, $f0, $f16           \r\n"
        "psrah $f2, $f2, $f16           \r\n"
        "psubh $f0, $f0, $f14           \r\n"
        "psubh $f2, $f2, $f14           \r\n"
        "or $f0, $f0, $f14              \r\n"
        "or $f2, $f2, $f14              \r\n"
        "xor $f0, $f0, $f4              \r\n"
        "xor $f2, $f2, $f6              \r\n"
        "psubh $f0, $f0, $f4            \r\n"
        "psubh $f2, $f2, $f6            \r\n"
        "pandn $f8, $f8, $f0            \r\n"
        "pandn $f10, $f10, $f2          \r\n"
        "gssdxc1 $f8, 0($8, %0)         \r\n"
        "gssdxc1 $f10, 8($8, %0)        \r\n"
        "addi $8, $8, 16                \r\n"
        "bltz $8, 1b                    \r\n"
        ::"r"(block+nCoeffs),"r"(quant_matrix+nCoeffs),"m"(qscale),
          "g"(-2*nCoeffs)
        : "$8","$10","memory"
    );

    block[0] = block0;
}

void ff_dct_unquantize_mpeg1_inter_mmi(MpegEncContext *s, int16_t *block,
        int n, int qscale)
{
    int64_t nCoeffs;
    const uint16_t *quant_matrix;

    av_assert2(s->block_last_index[n] >= 0);
    nCoeffs = s->intra_scantable.raster_end[s->block_last_index[n]] + 1;
    quant_matrix = s->inter_matrix;

    __asm__ volatile (
        "pcmpeqh $f14, $f14, $f14       \r\n"
        "dli $10, 15                    \r\n"
        "dmtc1 $10, $f16                \r\n"
        "xor $f12, $f12, $f12           \r\n"
        "lwc1 $f12, %2                  \r\n"
        "psrlh $f14, $f14, $f16         \r\n"
        "packsswh $f12, $f12, $f12      \r\n"
        "packsswh $f12, $f12, $f12      \r\n"
        "or $8, %3, $0                  \r\n"
        ".p2align 4                     \r\n"
        "1:                             \r\n"
        "gsldxc1 $f0, 0($8, %0)         \r\n"
        "gsldxc1 $f2, 8($8, %0)         \r\n"
        "mov.d $f16, $f0                \r\n"
        "mov.d $f18, $f2                \r\n"
        "gsldxc1 $f8, 0($8, %1)         \r\n"
        "gsldxc1 $f10, 8($8, %1)        \r\n"
        "pmullh $f8, $f8, $f12          \r\n"
        "pmullh $f10, $f10, $f12        \r\n"
        "xor $f4, $f4, $f4              \r\n"
        "xor $f6, $f6, $f6              \r\n"
        "pcmpgth $f4, $f4, $f0          \r\n"
        "pcmpgth $f6, $f6, $f2          \r\n"
        "xor $f0, $f0, $f4              \r\n"
        "xor $f2, $f2, $f6              \r\n"
        "psubh $f0, $f0, $f4            \r\n"
        "psubh $f2, $f2, $f6            \r\n"
        "paddh $f0, $f0, $f0            \r\n"
        "paddh $f2, $f2, $f2            \r\n"
        "paddh $f0, $f0, $f14           \r\n"
        "paddh $f2, $f2, $f14           \r\n"
        "pmullh $f0, $f0, $f8           \r\n"
        "pmullh $f2, $f2, $f10          \r\n"
        "xor $f8, $f8, $f8              \r\n"
        "xor $f10, $f10, $f10           \r\n"
        "pcmpeqh $f8, $f8, $f16         \r\n"
        "pcmpeqh $f10, $f10, $f18       \r\n"
        "dli $10, 4                     \r\n"
        "dmtc1 $10, $f16                \r\n"
        "psrah $f0, $f0, $f16           \r\n"
        "psrah $f2, $f2, $f16           \r\n"
        "psubh $f0, $f0, $f14           \r\n"
        "psubh $f2, $f2, $f14           \r\n"
        "or $f0, $f0, $f14              \r\n"
        "or $f2, $f2, $f14              \r\n"
        "xor $f0, $f0, $f4              \r\n"
        "xor $f2, $f2, $f6              \r\n"
        "psubh $f0, $f0, $f4            \r\n"
        "psubh $f2, $f2, $f6            \r\n"
        "pandn $f8, $f8, $f0            \r\n"
        "pandn $f10, $f10, $f2          \r\n"
        "gssdxc1 $f8, 0($8, %0)         \r\n"
        "gssdxc1 $f10, 8($8, %0)        \r\n"
        "addi $8, $8, 16                \r\n"
        "bltz $8, 1b                    \r\n"
        ::"r"(block+nCoeffs),"r"(quant_matrix+nCoeffs),"m"(qscale),
          "g"(-2*nCoeffs)
        :"$8","$10","memory"
    );
}

void ff_denoise_dct_mmi(MpegEncContext *s, int16_t *block)
{
    const int intra = s->mb_intra;
    int *sum = s->dct_error_sum[intra];
    uint16_t *offset = s->dct_offset[intra];

    s->dct_count[intra]++;

    __asm__ volatile(
        "xor $f14, $f14, $f14               \r\n"
        "1:                                 \r\n"
        "ldc1 $f4, 0(%[block])              \r\n"
        "xor $f0, $f0, $f0                  \r\n"
        "ldc1 $f6, 8(%[block])              \r\n"
        "xor $f2, $f2, $f2                  \r\n"
        "pcmpgth $f0, $f0, $f4              \r\n"
        "pcmpgth $f2, $f2, $f6              \r\n"
        "xor $f4, $f4, $f0                  \r\n"
        "xor $f6, $f6, $f2                  \r\n"
        "psubh $f4, $f4, $f0                \r\n"
        "psubh $f6, $f6, $f2                \r\n"
        "ldc1 $f12, 0(%[offset])            \r\n"
        "mov.d $f8, $f4                     \r\n"
        "psubush $f4, $f4, $f12             \r\n"
        "ldc1 $f12, 8(%[offset])            \r\n"
        "mov.d $f10, $f6                    \r\n"
        "psubush $f6, $f6, $f12             \r\n"
        "xor $f4, $f4, $f0                  \r\n"
        "xor $f6, $f6, $f2                  \r\n"
        "psubh $f4, $f4, $f0                \r\n"
        "psubh $f6, $f6, $f2                \r\n"
        "sdc1 $f4, 0(%[block])              \r\n"
        "sdc1 $f6, 8(%[block])              \r\n"
        "mov.d $f4, $f8                     \r\n"
        "mov.d $f6, $f10                    \r\n"
        "punpcklhw $f8, $f8, $f14           \r\n"
        "punpckhhw $f4, $f4, $f14           \r\n"
        "punpcklhw $f10, $f10, $f14         \r\n"
        "punpckhhw $f6, $f6, $f14           \r\n"
        "ldc1 $f0, 0(%[sum])                \r\n"
        "paddw $f8, $f8, $f0                \r\n"
        "ldc1 $f0, 8(%[sum])                \r\n"
        "paddw $f4, $f4, $f0                \r\n"
        "ldc1 $f0, 16(%[sum])               \r\n"
        "paddw $f10, $f10, $f0              \r\n"
        "ldc1 $f0, 24(%[sum])               \r\n"
        "paddw $f6, $f6, $f0                \r\n"
        "sdc1 $f8, 0(%[sum])                \r\n"
        "sdc1 $f4, 8(%[sum])                \r\n"
        "sdc1 $f10, 16(%[sum])              \r\n"
        "sdc1 $f6, 24(%[sum])               \r\n"
        "daddiu %[block], %[block], 16      \r\n"
        "daddiu %[sum], %[sum], 32          \r\n"
        "daddiu %[offset], %[offset], 16    \r\n"
        "dsubu $8, %[block1], %[block]      \r\n"
        "bgtz $8, 1b                        \r\n"
        : [block]"+r"(block),[sum]"+r"(sum),[offset]"+r"(offset)
        : [block1]"r"(block+64)
        : "$8","$f0","$f2","$f4","$f6","$f8","$f10","$f12","$f14"
    );
}

void ff_dct_unquantize_mpeg2_intra_mmi(MpegEncContext *s, int16_t *block,
        int n, int qscale)
{
    uint64_t nCoeffs;
    const uint16_t *quant_matrix;
    int block0;

    assert(s->block_last_index[n]>=0);

    if (s->alternate_scan)
        nCoeffs = 63;
    else
        nCoeffs = s->intra_scantable.raster_end[s->block_last_index[n]];

    if (n < 4)
        block0 = block[0] * s->y_dc_scale;
    else
        block0 = block[0] * s->c_dc_scale;

    quant_matrix = s->intra_matrix;

    __asm__ volatile (
        "pcmpeqh $f14, $f14, $f14           \r\n"
        "dli $10, 15                        \r\n"
        "dmtc1 $10, $f16                    \r\n"
        "xor $f12, $f12, $f12               \r\n"
        "lwc1 $f12, %[qscale]               \r\n"
        "psrlh $f14, $f14, $f16             \r\n"
        "packsswh $f12, $f12, $f12          \r\n"
        "packsswh $f12, $f12, $f12          \r\n"
        "or $8, %[ncoeffs], $0              \r\n"
        ".p2align 4                         \r\n"
        "1:                                 \r\n"
        "gsldxc1 $f0, 0($8, %[block])       \r\n"
        "gsldxc1 $f2, 8($8, %[block])       \r\n"
        "mov.d $f16, $f0                    \r\n"
        "mov.d $f18, $f2                    \r\n"
        "gsldxc1 $f8, 0($8, %[quant])       \r\n"
        "gsldxc1 $f10, 0($8, %[quant])      \r\n"
        "pmullh $f8, $f8, $f12              \r\n"
        "pmullh $f10, $f10, $f12            \r\n"
        "xor $f4, $f4, $f4                  \r\n"
        "xor $f6, $f6, $f6                  \r\n"
        "pcmpgth $f4, $f4, $f0              \r\n"
        "pcmpgth $f6, $f6, $f2              \r\n"
        "xor $f0, $f0, $f4                  \r\n"
        "xor $f2, $f2, $f6                  \r\n"
        "psubh $f0, $f0, $f4                \r\n"
        "psubh $f2, $f2, $f6                \r\n"
        "pmullh $f0, $f0, $f8               \r\n"
        "pmullh $f2, $f2, $f10              \r\n"
        "xor $f8, $f8, $f8                  \r\n"
        "xor $f10, $f10, $f10               \r\n"
        "pcmpeqh $f8, $f8, $f16             \r\n"
        "pcmpeqh $f10 ,$f10, $f18           \r\n"
        "dli $10, 3                         \r\n"
        "dmtc1 $10, $f16                    \r\n"
        "psrah $f0, $f0, $f16               \r\n"
        "psrah $f2, $f2, $f16               \r\n"
        "xor $f0, $f0, $f4                  \r\n"
        "xor $f2, $f2, $f6                  \r\n"
        "psubh $f0, $f0, $f4                \r\n"
        "psubh $f2, $f2, $f6                \r\n"
        "pandn $f8, $f8, $f0                \r\n"
        "pandn $f10, $f10, $f2              \r\n"
        "gssdxc1 $f8, 0($8, %[block])       \r\n"
        "gssdxc1 $f10, 8($8, %[block])      \r\n"
        "daddiu $8, $8, 16                  \r\n"
        "blez $8, 1b                        \r\n"
        ::[block]"r"(block+nCoeffs),[quant]"r"(quant_matrix+nCoeffs),
          [qscale]"m"(qscale),[ncoeffs]"g"(-2*nCoeffs)
        : "$8","$10","$f0","$f2","$f4","$f6","$f8","$f10","$f12","$f14","$f16",
          "$f18"
    );

    block[0]= block0;
}
