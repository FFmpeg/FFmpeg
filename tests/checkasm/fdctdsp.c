/*
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

#include "checkasm.h"

#include "libavcodec/avcodec.h"
#include "libavcodec/fdctdsp.h"

#include "libavutil/common.h"
#include "libavutil/internal.h"
#include "libavutil/mem_internal.h"

static int int16_cmp_off_by_n(const int16_t *ref, const int16_t *test, size_t n, int accuracy)
{
    for (size_t i = 0; i < n; i++) {
        if (abs(ref[i] - test[i]) > accuracy)
            return 1;
    }
    return 0;
}

static void check_fdct(void)
{
    LOCAL_ALIGNED_16(int16_t, block0, [64]);
    LOCAL_ALIGNED_16(int16_t, block1, [64]);

    AVCodecContext avctx = {
        .bits_per_raw_sample = 8,
        .dct_algo = FF_DCT_AUTO,
    };
    FDCTDSPContext h;

    ff_fdctdsp_init(&h, &avctx);

    if (check_func(h.fdct, "fdct")) {
        declare_func(void, int16_t *);
        for (int i = 0; i < 64; i++) {
            uint8_t r = rnd();
            block0[i] = r;
            block1[i] = r;
        }
        call_ref(block0);
        call_new(block1);
        if (int16_cmp_off_by_n(block0, block1, 64, 2))
            fail();
        bench_new(block1);
    }
}

void checkasm_check_fdctdsp(void)
{
    check_fdct();
    report("fdctdsp");
}
