/*
 * Copyright (c) 2024 FFmpeg contributors
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <string.h>

#include "libavutil/intreadwrite.h"
#include "libavutil/mem_internal.h"

#include "libavcodec/hevc/dsp.h"

#include "checkasm.h"

#define randomize_buffers(buf, size)            \
    do {                                        \
        int j;                                  \
        for (j = 0; j < size; j++) {            \
            int16_t r = rnd() & 0x7FFF;         \
            if (rnd() & 1) r = -r;              \
            AV_WN16A(buf + j, r);               \
        }                                       \
    } while (0)

static void check_dequant(HEVCDSPContext *h, int bit_depth)
{
    int i;
    LOCAL_ALIGNED(32, int16_t, coeffs0, [32 * 32]);
    LOCAL_ALIGNED(32, int16_t, coeffs1, [32 * 32]);

    for (i = 2; i <= 5; i++) {
        int block_size = 1 << i;
        int size = block_size * block_size;
        declare_func(void, int16_t *coeffs, int16_t log2_size);

        if (check_func(h->dequant, "hevc_dequant_%dx%d_%d",
                       block_size, block_size, bit_depth)) {
            randomize_buffers(coeffs0, size);
            memcpy(coeffs1, coeffs0, sizeof(*coeffs0) * size);

            call_ref(coeffs0, i);
            call_new(coeffs1, i);
            if (memcmp(coeffs0, coeffs1, sizeof(*coeffs0) * size))
                fail();
            bench_new(coeffs1, i);
        }
    }
}

void checkasm_check_hevc_dequant(void)
{
    int bit_depth;

    for (bit_depth = 8; bit_depth <= 12; bit_depth += 2) {
        HEVCDSPContext h;

        ff_hevc_dsp_init(&h, bit_depth);
        check_dequant(&h, bit_depth);
    }
    report("dequant");
}
