/*
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

#include <stdint.h>

#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/common.h"

#include "apv.h"
#include "apv_dsp.h"


static const int8_t apv_trans_matrix[8][8] = {
    {  64,  64,  64,  64,  64,  64,  64,  64 },
    {  89,  75,  50,  18, -18, -50, -75, -89 },
    {  84,  35, -35, -84, -84, -35,  35,  84 },
    {  75, -18, -89, -50,  50,  89,  18, -75 },
    {  64, -64, -64,  64,  64, -64, -64,  64 },
    {  50, -89,  18,  75, -75, -18,  89, -50 },
    {  35, -84,  84, -35, -35,  84, -84,  35 },
    {  18, -50,  75, -89,  89, -75,  50, -18 },
};

static void apv_decode_transquant_c(void *output,
                                    ptrdiff_t pitch,
                                    const int16_t *input_flat,
                                    const int16_t *qmatrix_flat,
                                    int bit_depth,
                                    int qp_shift)
{
    const int16_t (*input)[8]   = (const int16_t(*)[8])input_flat;
    const int16_t (*qmatrix)[8] = (const int16_t(*)[8])qmatrix_flat;

    int16_t scaled_coeff[8][8];
    int32_t recon_sample[8][8];

    // Dequant.
    {
        // Note that level_scale was already combined into qmatrix
        // before we got here.
        int bd_shift = bit_depth + 3 - 5;

        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                int coeff = (input[y][x] * qmatrix[y][x] * (1 << qp_shift) +
                             (1 << (bd_shift - 1))) >> bd_shift;

                scaled_coeff[y][x] =
                    av_clip(coeff, APV_MIN_TRANS_COEFF,
                                   APV_MAX_TRANS_COEFF);
            }
        }
    }

    // Transform.
    {
        int32_t tmp[8][8];

        // Vertical transform of columns.
        for (int x = 0; x < 8; x++) {
            for (int i = 0; i < 8; i++) {
                int sum = 0;
                for (int j = 0; j < 8; j++)
                    sum += apv_trans_matrix[j][i] * scaled_coeff[j][x];
                tmp[i][x] = sum;
            }
        }

        // Renormalise.
        for (int x = 0; x < 8; x++) {
            for (int y = 0; y < 8; y++)
                tmp[y][x] = (tmp[y][x] + 64) >> 7;
        }

        // Horizontal transform of rows.
        for (int y = 0; y < 8; y++) {
            for (int i = 0; i < 8; i++) {
                int sum = 0;
                for (int j = 0; j < 8; j++)
                    sum += apv_trans_matrix[j][i] * tmp[y][j];
                recon_sample[y][i] = sum;
            }
        }
    }

    // Output.
    if (bit_depth == 8) {
        uint8_t *ptr = output;
        int bd_shift = 20 - bit_depth;

        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                int sample = ((recon_sample[y][x] +
                               (1 << (bd_shift - 1))) >> bd_shift) +
                    (1 << (bit_depth - 1));
                ptr[x] = av_clip_uintp2(sample, bit_depth);
            }
            ptr += pitch;
        }
    } else {
        uint16_t *ptr = output;
        int bd_shift = 20 - bit_depth;
        pitch /= 2; // Pitch was in bytes, 2 bytes per sample.

        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                int sample = ((recon_sample[y][x] +
                               (1 << (bd_shift - 1))) >> bd_shift) +
                    (1 << (bit_depth - 1));
                ptr[x] = av_clip_uintp2(sample, bit_depth);
            }
            ptr += pitch;
        }
    }
}

av_cold void ff_apv_dsp_init(APVDSPContext *dsp)
{
    dsp->decode_transquant = apv_decode_transquant_c;

#if ARCH_X86_64
    ff_apv_dsp_init_x86_64(dsp);
#endif
}
