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
#include "libavutil/mips/mmiutils.h"

void ff_dct_unquantize_h263_intra_mmi(MpegEncContext *s, int16_t *block,
        int n, int qscale)
{
    int64_t level, qmul, qadd, nCoeffs;
    double ftmp[6];
    mips_reg addr[1];
    DECLARE_VAR_ALL64;

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
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"
        "packsswh   %[qmul],    %[qmul],        %[qmul]                 \n\t"
        "packsswh   %[qmul],    %[qmul],        %[qmul]                 \n\t"
        "packsswh   %[qadd],    %[qadd],        %[qadd]                 \n\t"
        "packsswh   %[qadd],    %[qadd],        %[qadd]                 \n\t"
        "psubh      %[ftmp0],   %[ftmp0],       %[qadd]                 \n\t"
        "xor        %[ftmp5],   %[ftmp5],       %[ftmp5]                \n\t"
        ".p2align   4                                                   \n\t"

        "1:                                                             \n\t"
        PTR_ADDU   "%[addr0],   %[block],       %[nCoeffs]              \n\t"
        MMI_LDC1(%[ftmp1], %[addr0], 0x00)
        MMI_LDC1(%[ftmp2], %[addr0], 0x08)
        "mov.d      %[ftmp3],   %[ftmp1]                                \n\t"
        "mov.d      %[ftmp4],   %[ftmp2]                                \n\t"
        "pmullh     %[ftmp1],   %[ftmp1],       %[qmul]                 \n\t"
        "pmullh     %[ftmp2],   %[ftmp2],       %[qmul]                 \n\t"
        "pcmpgth    %[ftmp3],   %[ftmp3],       %[ftmp5]                \n\t"
        "pcmpgth    %[ftmp4],   %[ftmp4],       %[ftmp5]                \n\t"
        "xor        %[ftmp1],   %[ftmp1],       %[ftmp3]                \n\t"
        "xor        %[ftmp2],   %[ftmp2],       %[ftmp4]                \n\t"
        "paddh      %[ftmp1],   %[ftmp1],       %[ftmp0]                \n\t"
        "paddh      %[ftmp2],   %[ftmp2],       %[ftmp0]                \n\t"
        "xor        %[ftmp3],   %[ftmp3],       %[ftmp1]                \n\t"
        "xor        %[ftmp4],   %[ftmp4],       %[ftmp2]                \n\t"
        "pcmpeqh    %[ftmp1],   %[ftmp1],       %[ftmp0]                \n\t"
        "pcmpeqh    %[ftmp2],   %[ftmp2],       %[ftmp0]                \n\t"
        "pandn      %[ftmp1],   %[ftmp1],       %[ftmp3]                \n\t"
        "pandn      %[ftmp2],   %[ftmp2],       %[ftmp4]                \n\t"
        PTR_ADDIU  "%[nCoeffs], %[nCoeffs],     0x10                    \n\t"
        MMI_SDC1(%[ftmp1], %[addr0], 0x00)
        MMI_SDC1(%[ftmp2], %[addr0], 0x08)
        "blez       %[nCoeffs], 1b                                      \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          RESTRICT_ASM_ALL64
          [addr0]"=&r"(addr[0])
        : [block]"r"((mips_reg)(block+nCoeffs)),
          [nCoeffs]"r"((mips_reg)(2*(-nCoeffs))),
          [qmul]"f"(qmul),                  [qadd]"f"(qadd)
        : "memory"
    );

    block[0] = level;
}

void ff_dct_unquantize_h263_inter_mmi(MpegEncContext *s, int16_t *block,
        int n, int qscale)
{
    int64_t qmul, qadd, nCoeffs;
    double ftmp[6];
    mips_reg addr[1];
    DECLARE_VAR_ALL64;

    qmul = qscale << 1;
    qadd = (qscale - 1) | 1;
    av_assert2(s->block_last_index[n]>=0 || s->h263_aic);
    nCoeffs = s->inter_scantable.raster_end[s->block_last_index[n]];

    __asm__ volatile (
        "packsswh   %[qmul],    %[qmul],        %[qmul]                 \n\t"
        "packsswh   %[qmul],    %[qmul],        %[qmul]                 \n\t"
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"
        "packsswh   %[qadd],    %[qadd],        %[qadd]                 \n\t"
        "packsswh   %[qadd],    %[qadd],        %[qadd]                 \n\t"
        "psubh      %[ftmp0],   %[ftmp0],       %[qadd]                 \n\t"
        "xor        %[ftmp5],   %[ftmp5],       %[ftmp5]                \n\t"
        ".p2align   4                                                   \n\t"
        "1:                                                             \n\t"
        PTR_ADDU   "%[addr0],   %[block],       %[nCoeffs]              \n\t"
        MMI_LDC1(%[ftmp1], %[addr0], 0x00)
        MMI_LDC1(%[ftmp2], %[addr0], 0x08)
        "mov.d      %[ftmp3],   %[ftmp1]                                \n\t"
        "mov.d      %[ftmp4],   %[ftmp2]                                \n\t"
        "pmullh     %[ftmp1],   %[ftmp1],       %[qmul]                 \n\t"
        "pmullh     %[ftmp2],   %[ftmp2],       %[qmul]                 \n\t"
        "pcmpgth    %[ftmp3],   %[ftmp3],       %[ftmp5]                \n\t"
        "pcmpgth    %[ftmp4],   %[ftmp4],       %[ftmp5]                \n\t"
        "xor        %[ftmp1],   %[ftmp1],       %[ftmp3]                \n\t"
        "xor        %[ftmp2],   %[ftmp2],       %[ftmp4]                \n\t"
        "paddh      %[ftmp1],   %[ftmp1],       %[ftmp0]                \n\t"
        "paddh      %[ftmp2],   %[ftmp2],       %[ftmp0]                \n\t"
        "xor        %[ftmp3],   %[ftmp3],       %[ftmp1]                \n\t"
        "xor        %[ftmp4],   %[ftmp4],       %[ftmp2]                \n\t"
        "pcmpeqh    %[ftmp1],   %[ftmp1],       %[ftmp0]                \n\t"
        "pcmpeqh    %[ftmp2],   %[ftmp2],       %[ftmp0]                \n\t"
        "pandn      %[ftmp1],   %[ftmp1],       %[ftmp3]                \n\t"
        "pandn      %[ftmp2],   %[ftmp2],       %[ftmp4]                \n\t"
        PTR_ADDIU  "%[nCoeffs], %[nCoeffs],     0x10                    \n\t"
        MMI_SDC1(%[ftmp1], %[addr0], 0x00)
        MMI_SDC1(%[ftmp2], %[addr0], 0x08)
        "blez       %[nCoeffs], 1b                                      \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          RESTRICT_ASM_ALL64
          [addr0]"=&r"(addr[0])
        : [block]"r"((mips_reg)(block+nCoeffs)),
          [nCoeffs]"r"((mips_reg)(2*(-nCoeffs))),
          [qmul]"f"(qmul),                  [qadd]"f"(qadd)
        : "memory"
    );
}

void ff_dct_unquantize_mpeg1_intra_mmi(MpegEncContext *s, int16_t *block,
        int n, int qscale)
{
    int64_t nCoeffs;
    const uint16_t *quant_matrix;
    int block0;
    double ftmp[10];
    uint64_t tmp[1];
    mips_reg addr[1];
    DECLARE_VAR_ALL64;
    DECLARE_VAR_ADDRT;

    av_assert2(s->block_last_index[n]>=0);
    nCoeffs = s->intra_scantable.raster_end[s->block_last_index[n]] + 1;

    if (n<4)
        block0 = block[0] * s->y_dc_scale;
    else
        block0 = block[0] * s->c_dc_scale;

    /* XXX: only mpeg1 */
    quant_matrix = s->intra_matrix;

    __asm__ volatile (
        "dli        %[tmp0],    0x0f                                    \n\t"
        "pcmpeqh    %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"
        "dmtc1      %[tmp0],    %[ftmp4]                                \n\t"
        "dmtc1      %[qscale],  %[ftmp1]                                \n\t"
        "psrlh      %[ftmp0],   %[ftmp0],       %[ftmp4]                \n\t"
        "packsswh   %[ftmp1],   %[ftmp1],       %[ftmp1]                \n\t"
        "packsswh   %[ftmp1],   %[ftmp1],       %[ftmp1]                \n\t"
        "or         %[addr0],   %[nCoeffs],     $0                      \n\t"
        ".p2align   4                                                   \n\t"

        "1:                                                             \n\t"
        MMI_LDXC1(%[ftmp2], %[addr0], %[block], 0x00)
        MMI_LDXC1(%[ftmp3], %[addr0], %[block], 0x08)
        "mov.d      %[ftmp4],   %[ftmp2]                                \n\t"
        "mov.d      %[ftmp5],   %[ftmp3]                                \n\t"
        MMI_LDXC1(%[ftmp6], %[addr0], %[quant], 0x00)
        MMI_LDXC1(%[ftmp7], %[addr0], %[quant], 0x08)
        "pmullh     %[ftmp6],   %[ftmp6],       %[ftmp1]                \n\t"
        "pmullh     %[ftmp7],   %[ftmp7],       %[ftmp1]                \n\t"
        "xor        %[ftmp8],   %[ftmp8],       %[ftmp8]                \n\t"
        "xor        %[ftmp9],   %[ftmp9],       %[ftmp9]                \n\t"
        "pcmpgth    %[ftmp8],   %[ftmp8],       %[ftmp2]                \n\t"
        "pcmpgth    %[ftmp9],   %[ftmp9],       %[ftmp3]                \n\t"
        "xor        %[ftmp2],   %[ftmp2],       %[ftmp8]                \n\t"
        "xor        %[ftmp3],   %[ftmp3],       %[ftmp9]                \n\t"
        "psubh      %[ftmp2],   %[ftmp2],       %[ftmp8]                \n\t"
        "psubh      %[ftmp3],   %[ftmp3],       %[ftmp9]                \n\t"
        "pmullh     %[ftmp2],   %[ftmp2],       %[ftmp6]                \n\t"
        "pmullh     %[ftmp3],   %[ftmp3],       %[ftmp7]                \n\t"
        "xor        %[ftmp6],   %[ftmp6],       %[ftmp6]                \n\t"
        "xor        %[ftmp7],   %[ftmp7],       %[ftmp7]                \n\t"
        "pcmpeqh    %[ftmp6],   %[ftmp6],       %[ftmp4]                \n\t"
        "dli        %[tmp0],    0x03                                    \n\t"
        "pcmpeqh    %[ftmp7],   %[ftmp7],       %[ftmp5]                \n\t"
        "dmtc1      %[tmp0],    %[ftmp4]                                \n\t"
        "psrah      %[ftmp2],   %[ftmp2],       %[ftmp4]                \n\t"
        "psrah      %[ftmp3],   %[ftmp3],       %[ftmp4]                \n\t"
        "psubh      %[ftmp2],   %[ftmp2],       %[ftmp0]                \n\t"
        "psubh      %[ftmp3],   %[ftmp3],       %[ftmp0]                \n\t"
        "or         %[ftmp2],   %[ftmp2],       %[ftmp0]                \n\t"
        "or         %[ftmp3],   %[ftmp3],       %[ftmp0]                \n\t"
        "xor        %[ftmp2],   %[ftmp2],       %[ftmp8]                \n\t"
        "xor        %[ftmp3],   %[ftmp3],       %[ftmp9]                \n\t"
        "psubh      %[ftmp2],   %[ftmp2],       %[ftmp8]                \n\t"
        "psubh      %[ftmp3],   %[ftmp3],       %[ftmp9]                \n\t"
        "pandn      %[ftmp6],   %[ftmp6],       %[ftmp2]                \n\t"
        "pandn      %[ftmp7],   %[ftmp7],       %[ftmp3]                \n\t"
        MMI_SDXC1(%[ftmp6], %[addr0], %[block], 0x00)
        MMI_SDXC1(%[ftmp7], %[addr0], %[block], 0x08)
        PTR_ADDIU  "%[addr0],   %[addr0],       0x10                    \n\t"
        "bltz       %[addr0],   1b                                      \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),            [ftmp9]"=&f"(ftmp[9]),
          [tmp0]"=&r"(tmp[0]),
          RESTRICT_ASM_ALL64
          RESTRICT_ASM_ADDRT
          [addr0]"=&r"(addr[0])
        : [block]"r"((mips_reg)(block+nCoeffs)),
          [quant]"r"((mips_reg)(quant_matrix+nCoeffs)),
          [nCoeffs]"r"((mips_reg)(2*(-nCoeffs))),
          [qscale]"r"(qscale)
        : "memory"
    );

    block[0] = block0;
}

void ff_dct_unquantize_mpeg1_inter_mmi(MpegEncContext *s, int16_t *block,
        int n, int qscale)
{
    int64_t nCoeffs;
    const uint16_t *quant_matrix;
    double ftmp[10];
    uint64_t tmp[1];
    mips_reg addr[1];
    DECLARE_VAR_ALL64;
    DECLARE_VAR_ADDRT;

    av_assert2(s->block_last_index[n] >= 0);
    nCoeffs = s->intra_scantable.raster_end[s->block_last_index[n]] + 1;
    quant_matrix = s->inter_matrix;

    __asm__ volatile (
        "dli        %[tmp0],    0x0f                                    \n\t"
        "pcmpeqh    %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"
        "dmtc1      %[tmp0],    %[ftmp4]                                \n\t"
        "dmtc1      %[qscale],  %[ftmp1]                                \n\t"
        "psrlh      %[ftmp0],   %[ftmp0],       %[ftmp4]                \n\t"
        "packsswh   %[ftmp1],   %[ftmp1],       %[ftmp1]                \n\t"
        "packsswh   %[ftmp1],   %[ftmp1],       %[ftmp1]                \n\t"
        "or         %[addr0],   %[nCoeffs],     $0                      \n\t"
        ".p2align   4                                                   \n\t"

        "1:                                                             \n\t"
        MMI_LDXC1(%[ftmp2], %[addr0], %[block], 0x00)
        MMI_LDXC1(%[ftmp3], %[addr0], %[block], 0x08)
        "mov.d      %[ftmp4],   %[ftmp2]                                \n\t"
        "mov.d      %[ftmp5],   %[ftmp3]                                \n\t"
        MMI_LDXC1(%[ftmp6], %[addr0], %[quant], 0x00)
        MMI_LDXC1(%[ftmp7], %[addr0], %[quant], 0x08)
        "pmullh     %[ftmp6],   %[ftmp6],       %[ftmp1]                \n\t"
        "pmullh     %[ftmp7],   %[ftmp7],       %[ftmp1]                \n\t"
        "xor        %[ftmp8],   %[ftmp8],       %[ftmp8]                \n\t"
        "xor        %[ftmp9],   %[ftmp9],       %[ftmp9]                \n\t"
        "pcmpgth    %[ftmp8],   %[ftmp8],       %[ftmp2]                \n\t"
        "pcmpgth    %[ftmp9],   %[ftmp9],       %[ftmp3]                \n\t"
        "xor        %[ftmp2],   %[ftmp2],       %[ftmp8]                \n\t"
        "xor        %[ftmp3],   %[ftmp3],       %[ftmp9]                \n\t"
        "psubh      %[ftmp2],   %[ftmp2],       %[ftmp8]                \n\t"
        "psubh      %[ftmp3],   %[ftmp3],       %[ftmp9]                \n\t"
        "paddh      %[ftmp2],   %[ftmp2],       %[ftmp2]                \n\t"
        "paddh      %[ftmp3],   %[ftmp3],       %[ftmp3]                \n\t"
        "paddh      %[ftmp2],   %[ftmp2],       %[ftmp0]                \n\t"
        "paddh      %[ftmp3],   %[ftmp3],       %[ftmp0]                \n\t"
        "pmullh     %[ftmp2],   %[ftmp2],       %[ftmp6]                \n\t"
        "pmullh     %[ftmp3],   %[ftmp3],       %[ftmp7]                \n\t"
        "xor        %[ftmp6],   %[ftmp6],       %[ftmp6]                \n\t"
        "xor        %[ftmp7],   %[ftmp7],       %[ftmp7]                \n\t"
        "pcmpeqh    %[ftmp6],   %[ftmp6],       %[ftmp4]                \n\t"
        "dli        %[tmp0],    0x04                                    \n\t"
        "pcmpeqh    %[ftmp7],   %[ftmp7],       %[ftmp5]                \n\t"
        "dmtc1      %[tmp0],    %[ftmp4]                                \n\t"
        "psrah      %[ftmp2],   %[ftmp2],       %[ftmp4]                \n\t"
        "psrah      %[ftmp3],   %[ftmp3],       %[ftmp4]                \n\t"
        "psubh      %[ftmp2],   %[ftmp2],       %[ftmp0]                \n\t"
        "psubh      %[ftmp3],   %[ftmp3],       %[ftmp0]                \n\t"
        "or         %[ftmp2],   %[ftmp2],       %[ftmp0]                \n\t"
        "or         %[ftmp3],   %[ftmp3],       %[ftmp0]                \n\t"
        "xor        %[ftmp2],   %[ftmp2],       %[ftmp8]                \n\t"
        "xor        %[ftmp3],   %[ftmp3],       %[ftmp9]                \n\t"
        "psubh      %[ftmp2],   %[ftmp2],       %[ftmp8]                \n\t"
        "psubh      %[ftmp3],   %[ftmp3],       %[ftmp9]                \n\t"
        "pandn      %[ftmp6],   %[ftmp6],       %[ftmp2]                \n\t"
        "pandn      %[ftmp7],   %[ftmp7],       %[ftmp3]                \n\t"
        MMI_SDXC1(%[ftmp6], %[addr0], %[block], 0x00)
        MMI_SDXC1(%[ftmp7], %[addr0], %[block], 0x08)
        PTR_ADDIU  "%[addr0],   %[addr0],       0x10                    \n\t"
        "bltz       %[addr0],   1b                                      \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),            [ftmp9]"=&f"(ftmp[9]),
          [tmp0]"=&r"(tmp[0]),
          RESTRICT_ASM_ALL64
          RESTRICT_ASM_ADDRT
          [addr0]"=&r"(addr[0])
        : [block]"r"((mips_reg)(block+nCoeffs)),
          [quant]"r"((mips_reg)(quant_matrix+nCoeffs)),
          [nCoeffs]"r"((mips_reg)(2*(-nCoeffs))),
          [qscale]"r"(qscale)
        : "memory"
    );
}

void ff_dct_unquantize_mpeg2_intra_mmi(MpegEncContext *s, int16_t *block,
        int n, int qscale)
{
    uint64_t nCoeffs;
    const uint16_t *quant_matrix;
    int block0;
    double ftmp[10];
    uint64_t tmp[1];
    mips_reg addr[1];
    DECLARE_VAR_ALL64;
    DECLARE_VAR_ADDRT;

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
        "dli        %[tmp0],    0x0f                                    \n\t"
        "pcmpeqh    %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"
        "mtc1       %[tmp0],    %[ftmp3]                                \n\t"
        "mtc1       %[qscale],  %[ftmp9]                                \n\t"
        "psrlh      %[ftmp0],   %[ftmp0],       %[ftmp3]                \n\t"
        "packsswh   %[ftmp9],   %[ftmp9],       %[ftmp9]                \n\t"
        "packsswh   %[ftmp9],   %[ftmp9],       %[ftmp9]                \n\t"
        "or         %[addr0],   %[nCoeffs],     $0                      \n\t"
        ".p2align   4                                                   \n\t"

        "1:                                                             \n\t"
        MMI_LDXC1(%[ftmp1], %[addr0], %[block], 0x00)
        MMI_LDXC1(%[ftmp2], %[addr0], %[block], 0x08)
        "mov.d      %[ftmp3],   %[ftmp1]                                \n\t"
        "mov.d      %[ftmp4],   %[ftmp2]                                \n\t"
        MMI_LDXC1(%[ftmp5], %[addr0], %[quant], 0x00)
        MMI_LDXC1(%[ftmp6], %[addr0], %[quant], 0x08)
        "pmullh     %[ftmp5],   %[ftmp5],       %[ftmp9]                \n\t"
        "pmullh     %[ftmp6],   %[ftmp6],       %[ftmp9]                \n\t"
        "xor        %[ftmp7],   %[ftmp7],       %[ftmp7]                \n\t"
        "xor        %[ftmp8],   %[ftmp8],       %[ftmp8]                \n\t"
        "pcmpgth    %[ftmp7],   %[ftmp7],       %[ftmp1]                \n\t"
        "pcmpgth    %[ftmp8],   %[ftmp8],       %[ftmp2]                \n\t"
        "xor        %[ftmp1],   %[ftmp1],       %[ftmp7]                \n\t"
        "xor        %[ftmp2],   %[ftmp2],       %[ftmp8]                \n\t"
        "psubh      %[ftmp1],   %[ftmp1],       %[ftmp7]                \n\t"
        "psubh      %[ftmp2],   %[ftmp2],       %[ftmp8]                \n\t"
        "pmullh     %[ftmp1],   %[ftmp1],       %[ftmp5]                \n\t"
        "pmullh     %[ftmp2],   %[ftmp2],       %[ftmp6]                \n\t"
        "xor        %[ftmp5],   %[ftmp5],       %[ftmp5]                \n\t"
        "xor        %[ftmp6],   %[ftmp6],       %[ftmp6]                \n\t"
        "pcmpeqh    %[ftmp5],   %[ftmp5],       %[ftmp3]                \n\t"
        "dli        %[tmp0],    0x03                                    \n\t"
        "pcmpeqh    %[ftmp6] ,  %[ftmp6],       %[ftmp4]                \n\t"
        "mtc1       %[tmp0],    %[ftmp3]                                \n\t"
        "psrah      %[ftmp1],   %[ftmp1],       %[ftmp3]                \n\t"
        "psrah      %[ftmp2],   %[ftmp2],       %[ftmp3]                \n\t"
        "xor        %[ftmp1],   %[ftmp1],       %[ftmp7]                \n\t"
        "xor        %[ftmp2],   %[ftmp2],       %[ftmp8]                \n\t"
        "psubh      %[ftmp1],   %[ftmp1],       %[ftmp7]                \n\t"
        "psubh      %[ftmp2],   %[ftmp2],       %[ftmp8]                \n\t"
        "pandn      %[ftmp5],   %[ftmp5],       %[ftmp1]                \n\t"
        "pandn      %[ftmp6],   %[ftmp6],       %[ftmp2]                \n\t"
        MMI_SDXC1(%[ftmp5], %[addr0], %[block], 0x00)
        MMI_SDXC1(%[ftmp6], %[addr0], %[block], 0x08)
        PTR_ADDIU  "%[addr0],   %[addr0],       0x10                    \n\t"
        "blez       %[addr0],   1b                                      \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),            [ftmp9]"=&f"(ftmp[9]),
          [tmp0]"=&r"(tmp[0]),
          RESTRICT_ASM_ALL64
          RESTRICT_ASM_ADDRT
          [addr0]"=&r"(addr[0])
        : [block]"r"((mips_reg)(block+nCoeffs)),
          [quant]"r"((mips_reg)(quant_matrix+nCoeffs)),
          [nCoeffs]"r"((mips_reg)(2*(-nCoeffs))),
          [qscale]"r"(qscale)
        : "memory"
    );

    block[0]= block0;
}

void ff_denoise_dct_mmi(MpegEncContext *s, int16_t *block)
{
    const int intra = s->mb_intra;
    int *sum = s->dct_error_sum[intra];
    uint16_t *offset = s->dct_offset[intra];
    double ftmp[8];
    mips_reg addr[1];
    DECLARE_VAR_ALL64;

    s->dct_count[intra]++;

    __asm__ volatile(
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"
        "1:                                                             \n\t"
        MMI_LDC1(%[ftmp1], %[block], 0x00)
        "xor        %[ftmp2],   %[ftmp2],       %[ftmp2]                \n\t"
        MMI_LDC1(%[ftmp3], %[block], 0x08)
        "xor        %[ftmp4],   %[ftmp4],       %[ftmp4]                \n\t"
        "pcmpgth    %[ftmp2],   %[ftmp2],       %[ftmp1]                \n\t"
        "pcmpgth    %[ftmp4],   %[ftmp4],       %[ftmp3]                \n\t"
        "xor        %[ftmp1],   %[ftmp1],       %[ftmp2]                \n\t"
        "xor        %[ftmp3],   %[ftmp3],       %[ftmp4]                \n\t"
        "psubh      %[ftmp1],   %[ftmp1],       %[ftmp2]                \n\t"
        "psubh      %[ftmp3],   %[ftmp3],       %[ftmp4]                \n\t"
        MMI_LDC1(%[ftmp6], %[offset], 0x00)
        "mov.d      %[ftmp5],   %[ftmp1]                                \n\t"
        "psubush    %[ftmp1],   %[ftmp1],       %[ftmp6]                \n\t"
        MMI_LDC1(%[ftmp6], %[offset], 0x08)
        "mov.d      %[ftmp7],   %[ftmp3]                                \n\t"
        "psubush    %[ftmp3],   %[ftmp3],       %[ftmp6]                \n\t"
        "xor        %[ftmp1],   %[ftmp1],       %[ftmp2]                \n\t"
        "xor        %[ftmp3],   %[ftmp3],       %[ftmp4]                \n\t"
        "psubh      %[ftmp1],   %[ftmp1],       %[ftmp2]                \n\t"
        "psubh      %[ftmp3],   %[ftmp3],       %[ftmp4]                \n\t"
        MMI_SDC1(%[ftmp1], %[block], 0x00)
        MMI_SDC1(%[ftmp3], %[block], 0x08)
        "mov.d      %[ftmp1],   %[ftmp5]                                \n\t"
        "mov.d      %[ftmp3],   %[ftmp7]                                \n\t"
        "punpcklhw  %[ftmp5],   %[ftmp5],       %[ftmp0]                \n\t"
        "punpckhhw  %[ftmp1],   %[ftmp1],       %[ftmp0]                \n\t"
        "punpcklhw  %[ftmp7],   %[ftmp7],       %[ftmp0]                \n\t"
        "punpckhhw  %[ftmp3],   %[ftmp3],       %[ftmp0]                \n\t"
        MMI_LDC1(%[ftmp2], %[sum], 0x00)
        "paddw      %[ftmp5],   %[ftmp5],       %[ftmp2]                \n\t"
        MMI_LDC1(%[ftmp2], %[sum], 0x08)
        "paddw      %[ftmp1],   %[ftmp1],       %[ftmp2]                \n\t"
        MMI_LDC1(%[ftmp2], %[sum], 0x10)
        "paddw      %[ftmp7],   %[ftmp7],       %[ftmp2]                \n\t"
        MMI_LDC1(%[ftmp2], %[sum], 0x18)
        "paddw      %[ftmp3],   %[ftmp3],       %[ftmp2]                \n\t"
        MMI_SDC1(%[ftmp5], %[sum], 0x00)
        MMI_SDC1(%[ftmp1], %[sum], 0x08)
        MMI_SDC1(%[ftmp7], %[sum], 0x10)
        MMI_SDC1(%[ftmp3], %[sum], 0x18)
        PTR_ADDIU  "%[block],   %[block],       0x10                    \n\t"
        PTR_ADDIU  "%[sum],     %[sum],         0x20                    \n\t"
        PTR_SUBU   "%[addr0],   %[block1],      %[block]                \n\t"
        PTR_ADDIU  "%[offset],  %[offset],      0x10                    \n\t"
        "bgtz       %[addr0],   1b                                      \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
          RESTRICT_ASM_ALL64
          [addr0]"=&r"(addr[0]),
          [block]"+&r"(block),              [sum]"+&r"(sum),
          [offset]"+&r"(offset)
        : [block1]"r"(block+64)
        : "memory"
    );
}
