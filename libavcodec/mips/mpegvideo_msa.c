/*
 * Copyright (c) 2015 Manojkumar Bhosale (Manojkumar.Bhosale@imgtec.com)
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

#include "libavutil/mips/generic_macros_msa.h"
#include "h263dsp_mips.h"

static void h263_dct_unquantize_msa(int16_t *block, int16_t qmul,
                                    int16_t qadd, int8_t n_coeffs,
                                    uint8_t loop_start)
{
    int16_t *block_dup = block;
    int32_t level, cnt;
    v8i16 block_vec, qmul_vec, qadd_vec, sub;
    v8i16 add, mask, mul, zero_mask;

    qmul_vec = __msa_fill_h(qmul);
    qadd_vec = __msa_fill_h(qadd);
    for (cnt = 0; cnt < (n_coeffs >> 3); cnt++) {
        block_vec = LD_SH(block_dup + loop_start);
        mask = __msa_clti_s_h(block_vec, 0);
        zero_mask = __msa_ceqi_h(block_vec, 0);
        mul = block_vec * qmul_vec;
        sub = mul - qadd_vec;
        add = mul + qadd_vec;
        add = (v8i16) __msa_bmnz_v((v16u8) add, (v16u8) sub, (v16u8) mask);
        block_vec = (v8i16) __msa_bmnz_v((v16u8) add, (v16u8) block_vec,
                                         (v16u8) zero_mask);
        ST_SH(block_vec, block_dup + loop_start);
        block_dup += 8;
    }

    cnt = ((n_coeffs >> 3) * 8) + loop_start;

    for (; cnt <= n_coeffs; cnt++) {
        level = block[cnt];
        if (level) {
            if (level < 0) {
                level = level * qmul - qadd;
            } else {
                level = level * qmul + qadd;
            }
            block[cnt] = level;
        }
    }
}

static int32_t mpeg2_dct_unquantize_inter_msa(int16_t *block,
                                              int32_t qscale,
                                              const int16_t *quant_matrix)
{
    int32_t cnt, sum_res = -1;
    v8i16 block_vec, block_neg, qscale_vec, mask;
    v8i16 block_org0, block_org1, block_org2, block_org3;
    v8i16 quant_m0, quant_m1, quant_m2, quant_m3;
    v8i16 sum, mul, zero_mask;
    v4i32 mul_vec, qscale_l, qscale_r, quant_m_r, quant_m_l;
    v4i32 block_l, block_r, sad;

    qscale_vec = __msa_fill_h(qscale);
    for (cnt = 0; cnt < 2; cnt++) {
        LD_SH4(block, 8, block_org0, block_org1, block_org2, block_org3);
        LD_SH4(quant_matrix, 8, quant_m0, quant_m1, quant_m2, quant_m3);
        mask = __msa_clti_s_h(block_org0, 0);
        zero_mask = __msa_ceqi_h(block_org0, 0);
        block_neg = -block_org0;
        block_vec = (v8i16) __msa_bmnz_v((v16u8) block_org0, (v16u8) block_neg,
                                         (v16u8) mask);
        block_vec <<= 1;
        block_vec += 1;
        UNPCK_SH_SW(block_vec, block_r, block_l);
        UNPCK_SH_SW(qscale_vec, qscale_r, qscale_l);
        UNPCK_SH_SW(quant_m0, quant_m_r, quant_m_l);
        mul_vec = block_l * qscale_l;
        mul_vec *= quant_m_l;
        block_l = mul_vec >> 4;
        mul_vec = block_r * qscale_r;
        mul_vec *= quant_m_r;
        block_r = mul_vec >> 4;
        mul = (v8i16) __msa_pckev_h((v8i16) block_l, (v8i16) block_r);
        block_neg = - mul;
        sum = (v8i16) __msa_bmnz_v((v16u8) mul, (v16u8) block_neg,
                                   (v16u8) mask);
        sum = (v8i16) __msa_bmnz_v((v16u8) sum, (v16u8) block_org0,
                                   (v16u8) zero_mask);
        ST_SH(sum, block);
        block += 8;
        quant_matrix += 8;
        sad = __msa_hadd_s_w(sum, sum);
        sum_res += HADD_SW_S32(sad);
        mask = __msa_clti_s_h(block_org1, 0);
        zero_mask = __msa_ceqi_h(block_org1, 0);
        block_neg = - block_org1;
        block_vec = (v8i16) __msa_bmnz_v((v16u8) block_org1, (v16u8) block_neg,
                                         (v16u8) mask);
        block_vec <<= 1;
        block_vec += 1;
        UNPCK_SH_SW(block_vec, block_r, block_l);
        UNPCK_SH_SW(qscale_vec, qscale_r, qscale_l);
        UNPCK_SH_SW(quant_m1, quant_m_r, quant_m_l);
        mul_vec = block_l * qscale_l;
        mul_vec *= quant_m_l;
        block_l = mul_vec >> 4;
        mul_vec = block_r * qscale_r;
        mul_vec *= quant_m_r;
        block_r = mul_vec >> 4;
        mul = __msa_pckev_h((v8i16) block_l, (v8i16) block_r);
        block_neg = - mul;
        sum = (v8i16) __msa_bmnz_v((v16u8) mul, (v16u8) block_neg,
                                   (v16u8) mask);
        sum = (v8i16) __msa_bmnz_v((v16u8) sum, (v16u8) block_org1,
                                   (v16u8) zero_mask);
        ST_SH(sum, block);

        block += 8;
        quant_matrix += 8;
        sad = __msa_hadd_s_w(sum, sum);
        sum_res += HADD_SW_S32(sad);
        mask = __msa_clti_s_h(block_org2, 0);
        zero_mask = __msa_ceqi_h(block_org2, 0);
        block_neg = - block_org2;
        block_vec = (v8i16) __msa_bmnz_v((v16u8) block_org2, (v16u8) block_neg,
                                         (v16u8) mask);
        block_vec <<= 1;
        block_vec += 1;
        UNPCK_SH_SW(block_vec, block_r, block_l);
        UNPCK_SH_SW(qscale_vec, qscale_r, qscale_l);
        UNPCK_SH_SW(quant_m2, quant_m_r, quant_m_l);
        mul_vec = block_l * qscale_l;
        mul_vec *= quant_m_l;
        block_l = mul_vec >> 4;
        mul_vec = block_r * qscale_r;
        mul_vec *= quant_m_r;
        block_r = mul_vec >> 4;
        mul = __msa_pckev_h((v8i16) block_l, (v8i16) block_r);
        block_neg = - mul;
        sum = (v8i16) __msa_bmnz_v((v16u8) mul, (v16u8) block_neg,
                                   (v16u8) mask);
        sum = (v8i16) __msa_bmnz_v((v16u8) sum, (v16u8) block_org2,
                                   (v16u8) zero_mask);
        ST_SH(sum, block);

        block += 8;
        quant_matrix += 8;
        sad = __msa_hadd_s_w(sum, sum);
        sum_res += HADD_SW_S32(sad);
        mask = __msa_clti_s_h(block_org3, 0);
        zero_mask = __msa_ceqi_h(block_org3, 0);
        block_neg = - block_org3;
        block_vec = (v8i16) __msa_bmnz_v((v16u8) block_org3, (v16u8) block_neg,
                                         (v16u8) mask);
        block_vec <<= 1;
        block_vec += 1;
        UNPCK_SH_SW(block_vec, block_r, block_l);
        UNPCK_SH_SW(qscale_vec, qscale_r, qscale_l);
        UNPCK_SH_SW(quant_m3, quant_m_r, quant_m_l);
        mul_vec = block_l * qscale_l;
        mul_vec *= quant_m_l;
        block_l = mul_vec >> 4;
        mul_vec = block_r * qscale_r;
        mul_vec *= quant_m_r;
        block_r = mul_vec >> 4;
        mul = __msa_pckev_h((v8i16) block_l, (v8i16) block_r);
        block_neg = - mul;
        sum = (v8i16) __msa_bmnz_v((v16u8) mul, (v16u8) block_neg,
                                   (v16u8) mask);
        sum = (v8i16) __msa_bmnz_v((v16u8) sum, (v16u8) block_org3,
                                   (v16u8) zero_mask);
        ST_SH(sum, block);

        block += 8;
        quant_matrix += 8;
        sad = __msa_hadd_s_w(sum, sum);
        sum_res += HADD_SW_S32(sad);
    }

    return sum_res;
}

void ff_dct_unquantize_h263_intra_msa(MpegEncContext *s,
                                      int16_t *block, int32_t index,
                                      int32_t qscale)
{
    int32_t qmul, qadd;
    int32_t nCoeffs;

    av_assert2(s->block_last_index[index] >= 0 || s->h263_aic);

    qmul = qscale << 1;

    if (!s->h263_aic) {
        block[0] *= index < 4 ? s->y_dc_scale : s->c_dc_scale;
        qadd = (qscale - 1) | 1;
    } else {
        qadd = 0;
    }
    if (s->ac_pred)
        nCoeffs = 63;
    else
        nCoeffs = s->inter_scantable.raster_end[s->block_last_index[index]];

    h263_dct_unquantize_msa(block, qmul, qadd, nCoeffs, 1);
}

void ff_dct_unquantize_h263_inter_msa(MpegEncContext *s,
                                      int16_t *block, int32_t index,
                                      int32_t qscale)
{
    int32_t qmul, qadd;
    int32_t nCoeffs;

    av_assert2(s->block_last_index[index] >= 0);

    qadd = (qscale - 1) | 1;
    qmul = qscale << 1;

    nCoeffs = s->inter_scantable.raster_end[s->block_last_index[index]];

    h263_dct_unquantize_msa(block, qmul, qadd, nCoeffs, 0);
}

void ff_dct_unquantize_mpeg2_inter_msa(MpegEncContext *s,
                                       int16_t *block, int32_t index,
                                       int32_t qscale)
{
    const uint16_t *quant_matrix;
    int32_t sum = -1;

    quant_matrix = s->inter_matrix;

    sum = mpeg2_dct_unquantize_inter_msa(block, qscale, quant_matrix);

    block[63] ^= sum & 1;
}
