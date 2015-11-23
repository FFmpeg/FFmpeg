/*
 * G.723.1 compatible decoder
 * Copyright (c) 2006 Benjamin Larsson
 * Copyright (c) 2010 Mohamed Naufal Basheer
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdint.h>

#include "libavutil/common.h"

#include "acelp_vectors.h"
#include "avcodec.h"
#include "celp_math.h"
#include "g723_1.h"

int ff_g723_1_scale_vector(int16_t *dst, const int16_t *vector, int length)
{
    int bits, max = 0;
    int i;

    for (i = 0; i < length; i++)
        max |= FFABS(vector[i]);

    max  = FFMIN(max, 0x7FFF);
    bits = ff_g723_1_normalize_bits(max, 15);

    for (i = 0; i < length; i++)
        dst[i] = vector[i] << bits >> 3;

    return bits - 3;
}

int ff_g723_1_normalize_bits(int num, int width)
{
    return width - av_log2(num) - 1;
}

int ff_g723_1_dot_product(const int16_t *a, const int16_t *b, int length)
{
    int sum = ff_dot_product(a, b, length);
    return av_sat_add32(sum, sum);
}

void ff_g723_1_get_residual(int16_t *residual, int16_t *prev_excitation,
                            int lag)
{
    int offset = PITCH_MAX - PITCH_ORDER / 2 - lag;
    int i;

    residual[0] = prev_excitation[offset];
    residual[1] = prev_excitation[offset + 1];

    offset += 2;
    for (i = 2; i < SUBFRAME_LEN + PITCH_ORDER - 1; i++)
        residual[i] = prev_excitation[offset + (i - 2) % lag];
}

void ff_g723_1_gen_dirac_train(int16_t *buf, int pitch_lag)
{
    int16_t vector[SUBFRAME_LEN];
    int i, j;

    memcpy(vector, buf, SUBFRAME_LEN * sizeof(*vector));
    for (i = pitch_lag; i < SUBFRAME_LEN; i += pitch_lag) {
        for (j = 0; j < SUBFRAME_LEN - i; j++)
            buf[i + j] += vector[j];
    }
}

void ff_g723_1_gen_acb_excitation(int16_t *vector, int16_t *prev_excitation,
                                  int pitch_lag, G723_1_Subframe *subfrm,
                                  enum Rate cur_rate)
{
    int16_t residual[SUBFRAME_LEN + PITCH_ORDER - 1];
    const int16_t *cb_ptr;
    int lag = pitch_lag + subfrm->ad_cb_lag - 1;

    int i;
    int sum;

    ff_g723_1_get_residual(residual, prev_excitation, lag);

    /* Select quantization table */
    if (cur_rate == RATE_6300 && pitch_lag < SUBFRAME_LEN - 2)
        cb_ptr = adaptive_cb_gain85;
    else
        cb_ptr = adaptive_cb_gain170;

    /* Calculate adaptive vector */
    cb_ptr += subfrm->ad_cb_gain * 20;
    for (i = 0; i < SUBFRAME_LEN; i++) {
        sum       = ff_g723_1_dot_product(residual + i, cb_ptr, PITCH_ORDER);
        vector[i] = av_sat_dadd32(1 << 15, sum) >> 16;
    }
}

/**
 * Convert LSP frequencies to LPC coefficients.
 *
 * @param lpc buffer for LPC coefficients
 */
static void lsp2lpc(int16_t *lpc)
{
    int f1[LPC_ORDER / 2 + 1];
    int f2[LPC_ORDER / 2 + 1];
    int i, j;

    /* Calculate negative cosine */
    for (j = 0; j < LPC_ORDER; j++) {
        int index  = (lpc[j] >> 7) & 0x1FF;
        int offset = lpc[j] & 0x7f;
        int temp1  = cos_tab[index] << 16;
        int temp2  = (cos_tab[index + 1] - cos_tab[index]) *
                     ((offset << 8) + 0x80) << 1;

        lpc[j] = -(av_sat_dadd32(1 << 15, temp1 + temp2) >> 16);
    }

    /*
     * Compute sum and difference polynomial coefficients
     * (bitexact alternative to lsp2poly() in lsp.c)
     */
    /* Initialize with values in Q28 */
    f1[0] = 1 << 28;
    f1[1] = (lpc[0] << 14) + (lpc[2] << 14);
    f1[2] = lpc[0] * lpc[2] + (2 << 28);

    f2[0] = 1 << 28;
    f2[1] = (lpc[1] << 14) + (lpc[3] << 14);
    f2[2] = lpc[1] * lpc[3] + (2 << 28);

    /*
     * Calculate and scale the coefficients by 1/2 in
     * each iteration for a final scaling factor of Q25
     */
    for (i = 2; i < LPC_ORDER / 2; i++) {
        f1[i + 1] = f1[i - 1] + MULL2(f1[i], lpc[2 * i]);
        f2[i + 1] = f2[i - 1] + MULL2(f2[i], lpc[2 * i + 1]);

        for (j = i; j >= 2; j--) {
            f1[j] = MULL2(f1[j - 1], lpc[2 * i]) +
                    (f1[j] >> 1) + (f1[j - 2] >> 1);
            f2[j] = MULL2(f2[j - 1], lpc[2 * i + 1]) +
                    (f2[j] >> 1) + (f2[j - 2] >> 1);
        }

        f1[0] >>= 1;
        f2[0] >>= 1;
        f1[1]   = ((lpc[2 * i]     << 16 >> i) + f1[1]) >> 1;
        f2[1]   = ((lpc[2 * i + 1] << 16 >> i) + f2[1]) >> 1;
    }

    /* Convert polynomial coefficients to LPC coefficients */
    for (i = 0; i < LPC_ORDER / 2; i++) {
        int64_t ff1 = f1[i + 1] + f1[i];
        int64_t ff2 = f2[i + 1] - f2[i];

        lpc[i]                 = av_clipl_int32(((ff1 + ff2) << 3) +
                                                (1 << 15)) >> 16;
        lpc[LPC_ORDER - i - 1] = av_clipl_int32(((ff1 - ff2) << 3) +
                                                (1 << 15)) >> 16;
    }
}

void ff_g723_1_lsp_interpolate(int16_t *lpc, int16_t *cur_lsp,
                               int16_t *prev_lsp)
{
    int i;
    int16_t *lpc_ptr = lpc;

    /* cur_lsp * 0.25 + prev_lsp * 0.75 */
    ff_acelp_weighted_vector_sum(lpc, cur_lsp, prev_lsp,
                                 4096, 12288, 1 << 13, 14, LPC_ORDER);
    ff_acelp_weighted_vector_sum(lpc + LPC_ORDER, cur_lsp, prev_lsp,
                                 8192, 8192, 1 << 13, 14, LPC_ORDER);
    ff_acelp_weighted_vector_sum(lpc + 2 * LPC_ORDER, cur_lsp, prev_lsp,
                                 12288, 4096, 1 << 13, 14, LPC_ORDER);
    memcpy(lpc + 3 * LPC_ORDER, cur_lsp, LPC_ORDER * sizeof(*lpc));

    for (i = 0; i < SUBFRAMES; i++) {
        lsp2lpc(lpc_ptr);
        lpc_ptr += LPC_ORDER;
    }
}

void ff_g723_1_inverse_quant(int16_t *cur_lsp, int16_t *prev_lsp,
                             uint8_t *lsp_index, int bad_frame)
{
    int min_dist, pred;
    int i, j, temp, stable;

    /* Check for frame erasure */
    if (!bad_frame) {
        min_dist     = 0x100;
        pred         = 12288;
    } else {
        min_dist     = 0x200;
        pred         = 23552;
        lsp_index[0] = lsp_index[1] = lsp_index[2] = 0;
    }

    /* Get the VQ table entry corresponding to the transmitted index */
    cur_lsp[0] = lsp_band0[lsp_index[0]][0];
    cur_lsp[1] = lsp_band0[lsp_index[0]][1];
    cur_lsp[2] = lsp_band0[lsp_index[0]][2];
    cur_lsp[3] = lsp_band1[lsp_index[1]][0];
    cur_lsp[4] = lsp_band1[lsp_index[1]][1];
    cur_lsp[5] = lsp_band1[lsp_index[1]][2];
    cur_lsp[6] = lsp_band2[lsp_index[2]][0];
    cur_lsp[7] = lsp_band2[lsp_index[2]][1];
    cur_lsp[8] = lsp_band2[lsp_index[2]][2];
    cur_lsp[9] = lsp_band2[lsp_index[2]][3];

    /* Add predicted vector & DC component to the previously quantized vector */
    for (i = 0; i < LPC_ORDER; i++) {
        temp        = ((prev_lsp[i] - dc_lsp[i]) * pred + (1 << 14)) >> 15;
        cur_lsp[i] += dc_lsp[i] + temp;
    }

    for (i = 0; i < LPC_ORDER; i++) {
        cur_lsp[0]             = FFMAX(cur_lsp[0], 0x180);
        cur_lsp[LPC_ORDER - 1] = FFMIN(cur_lsp[LPC_ORDER - 1], 0x7e00);

        /* Stability check */
        for (j = 1; j < LPC_ORDER; j++) {
            temp = min_dist + cur_lsp[j - 1] - cur_lsp[j];
            if (temp > 0) {
                temp >>= 1;
                cur_lsp[j - 1] -= temp;
                cur_lsp[j]     += temp;
            }
        }
        stable = 1;
        for (j = 1; j < LPC_ORDER; j++) {
            temp = cur_lsp[j - 1] + min_dist - cur_lsp[j] - 4;
            if (temp > 0) {
                stable = 0;
                break;
            }
        }
        if (stable)
            break;
    }
    if (!stable)
        memcpy(cur_lsp, prev_lsp, LPC_ORDER * sizeof(*cur_lsp));
}
