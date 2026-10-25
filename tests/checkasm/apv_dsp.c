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
#include <string.h>

#include "checkasm.h"

#include "libavutil/common.h"
#include "libavutil/mem_internal.h"
#include "libavcodec/apv_dsp.h"

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

static const uint8_t apv_level_scale[6] = { 40, 45, 51, 57, 64, 71 };

// Make a set of quantised coefficients by sending a random residual
// through a forward transform and quantisation: this keeps every
// intermediate in the ranges a real encoder would produce (completely
// random coefficients overflow the use of 16x16->32 multiplies in the
// SIMD paths).
static void generate_block(int16_t *input, const int16_t *qmatrix,
                           int bit_depth, int qp_shift, int dense)
{
    // 64-bit intermediates here avoid rounding issues.
    int64_t residual[8][8], tmp[8][8];
    int amplitude;

    if (dense) {
        // Dense blocks for benchmarking are fully random and uniform
        // over the range.
        amplitude = (1 << bit_depth) - 1;
    } else {
        // Random blocks for testing bias the amplitude, both to get
        // smaller typical coefficients (including many zeroes) and to
        // make clipping on the ends of the range more likely.
        amplitude = (3 << (bit_depth - 2)) >> (rnd() % 8);
    }
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++)
            residual[y][x] =
                (int64_t)(rnd() % (2 * amplitude + 1)) - amplitude;
    }

    // Forward transform rows.
    for (int y = 0; y < 8; y++) {
        for (int i = 0; i < 8; i++) {
            int64_t sum = 0;
            for (int j = 0; j < 8; j++)
                sum += apv_trans_matrix[i][j] * residual[y][j];
            tmp[y][i] = sum;
        }
    }
    // Forward transform columns, renormalise, and apply quantisation
    // to make the coefficient level.
    for (int x = 0; x < 8; x++) {
        for (int i = 0; i < 8; i++) {
            int64_t coeff, step, level;

            coeff = 0;
            for (int j = 0; j < 8; j++)
                coeff += apv_trans_matrix[i][j] * tmp[j][x];
            coeff = ROUNDED_DIV(coeff,
                                INT64_C(1) << (bit_depth + 3));

            step  = qmatrix[i * 8 + x] << qp_shift;
            level = ROUNDED_DIV(coeff * (INT64_C(1) << (bit_depth - 2)),
                                step);

            input[i * 8 + x] = av_clip_int16(level);
        }
    }

    // Levels made as above never reach the clip after dequantisation,
    // so sometimes replace one coefficient with a huge value.  A single
    // coefficient like this does not overflow the intermediates.
    if (!dense && rnd() % 8 == 0) {
        int pos   = rnd() % 64;
        int level = 16384 + (rnd() % 16384);
        input[pos] = rnd() % 2 ? level : -level;
    }
}

static void check_decode_transquant(int bit_depth)
{
    LOCAL_ALIGNED_32( int16_t, input,      [64]);
    LOCAL_ALIGNED_32( int16_t, qmatrix,    [64]);
    LOCAL_ALIGNED_32(uint16_t, new_output, [64]);
    LOCAL_ALIGNED_32(uint16_t, ref_output, [64]);

    declare_func(void,
                 void *output,
                 ptrdiff_t pitch,
                 const int16_t *input,
                 const int16_t *qmatrix,
                 int bit_depth,
                 int qp_shift);

    for (int n = 0; n < 256; n++) {
        // Fully random QP for each block.
        int qp = rnd() % (3 + 6 * bit_depth + 1);
        int level_scale = apv_level_scale[qp % 6];
        int qp_shift = qp / 6;

        // The reference encoder uses a flat 16-everywhere matrix by
        // default, while any other matrix must be supplied manually by
        // the user.  Use the default matrix half the time, and a
        // completely random matrix otherwise.
        int flat = rnd() % 2;
        if (flat) {
            for (int i = 0; i < 64; i++)
                qmatrix[i] = level_scale * 16;
        } else {
            for (int i = 0; i < 64; i++)
                qmatrix[i] = level_scale * (1 + rnd() % 255);
        }

        generate_block(input, qmatrix, bit_depth, qp_shift, 0);

        call_ref(ref_output, 16, input, qmatrix, bit_depth, qp_shift);
        call_new(new_output, 16, input, qmatrix, bit_depth, qp_shift);

        if (memcmp(new_output, ref_output, 64 * sizeof(*ref_output))) {
            fail();
            break;
        }
    }

    {
        // Benchmark on a dense block with QP = 12 and a flat matrix.
        // Almost all coefficients will be nonzero.
        int qp_shift = 2;
        for (int i = 0; i < 64; i++)
            qmatrix[i] = apv_level_scale[0] * 16;
        generate_block(input, qmatrix, bit_depth, qp_shift, 1);

        call_ref(ref_output, 16, input, qmatrix, bit_depth, qp_shift);
        call_new(new_output, 16, input, qmatrix, bit_depth, qp_shift);
        if (memcmp(new_output, ref_output, 64 * sizeof(*ref_output)))
            fail();

        bench_new(new_output, 16, input, qmatrix, bit_depth, qp_shift);
    }
}

void checkasm_check_apv_dsp(void)
{
    APVDSPContext dsp;

    ff_apv_dsp_init(&dsp);

    for (int bit_depth = 10; bit_depth <= 16; bit_depth += 2) {
        if (check_func(dsp.decode_transquant,
                       "decode_transquant_%d", bit_depth))
            check_decode_transquant(bit_depth);
    }

    report("decode_transquant");
}
