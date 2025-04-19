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

#include "checkasm.h"

#include "libavutil/attributes.h"
#include "libavutil/mem_internal.h"
#include "libavcodec/apv_dsp.h"


static void check_decode_transquant_8(void)
{
    LOCAL_ALIGNED_16(int16_t, input,      [64]);
    LOCAL_ALIGNED_16(int16_t, qmatrix,    [64]);
    LOCAL_ALIGNED_16(uint8_t, new_output, [64]);
    LOCAL_ALIGNED_16(uint8_t, ref_output, [64]);

    declare_func(void,
                 void *output,
                 ptrdiff_t pitch,
                 const int16_t *input,
                 const int16_t *qmatrix,
                 int bit_depth,
                 int qp_shift);

    for (int i = 0; i < 64; i++) {
        // Any signed 12-bit integer.
        input[i] = rnd() % 2048 - 1024;

        // qmatrix input is premultiplied by level_scale, so
        // range is 1 to 255 * 71.  Interesting values are all
        // at the low end of that, though.
        qmatrix[i] = rnd() % 16 + 16;
    }

    call_ref(ref_output, 8, input, qmatrix, 8, 4);
    call_new(new_output, 8, input, qmatrix, 8, 4);

    if (memcmp(new_output, ref_output, 64 * sizeof(*ref_output)))
        fail();

    bench_new(new_output, 8, input, qmatrix, 8, 4);
}

static void check_decode_transquant_10(void)
{
    LOCAL_ALIGNED_16( int16_t, input,      [64]);
    LOCAL_ALIGNED_16( int16_t, qmatrix,    [64]);
    LOCAL_ALIGNED_16(uint16_t, new_output, [64]);
    LOCAL_ALIGNED_16(uint16_t, ref_output, [64]);

    declare_func(void,
                 void *output,
                 ptrdiff_t pitch,
                 const int16_t *input,
                 const int16_t *qmatrix,
                 int bit_depth,
                 int qp_shift);

    for (int i = 0; i < 64; i++) {
        // Any signed 14-bit integer.
        input[i] = rnd() % 16384 - 8192;

        // qmatrix input is premultiplied by level_scale, so
        // range is 1 to 255 * 71.  Interesting values are all
        // at the low end of that, though.
        qmatrix[i] = 16; //rnd() % 16 + 16;
    }

    call_ref(ref_output, 16, input, qmatrix, 10, 4);
    call_new(new_output, 16, input, qmatrix, 10, 4);

    if (memcmp(new_output, ref_output, 64 * sizeof(*ref_output)))
        fail();

    bench_new(new_output, 16, input, qmatrix, 10, 4);
}

void checkasm_check_apv_dsp(void)
{
    APVDSPContext dsp;

    ff_apv_dsp_init(&dsp);

    if (check_func(dsp.decode_transquant, "decode_transquant_8"))
        check_decode_transquant_8();

    if (check_func(dsp.decode_transquant, "decode_transquant_10"))
        check_decode_transquant_10();

    report("decode_transquant");
}
