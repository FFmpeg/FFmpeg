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

#include <float.h>

#include "libavutil/mem_internal.h"

#include "libavcodec/vorbisdsp.h"

#include "checkasm.h"

#define LEN 512

#define randomize_buffer(buf)                 \
do {                                          \
    double bmg[2], stddev = 10.0, mean = 0.0; \
                                              \
    for (int i = 0; i < LEN; i += 2) {        \
        av_bmg_get(&checkasm_lfg, bmg);       \
        buf[i]     = bmg[0] * stddev + mean;  \
        buf[i + 1] = bmg[1] * stddev + mean;  \
    }                                         \
} while(0);

static void test_inverse_coupling(void)
{
    LOCAL_ALIGNED_16(float, src0,  [LEN]);
    LOCAL_ALIGNED_16(float, src1,  [LEN]);
    LOCAL_ALIGNED_16(float, cdst,  [LEN]);
    LOCAL_ALIGNED_16(float, odst,  [LEN]);
    LOCAL_ALIGNED_16(float, cdst1, [LEN]);
    LOCAL_ALIGNED_16(float, odst1, [LEN]);

    declare_func(void, float *restrict mag, float *restrict ang,
                 ptrdiff_t blocksize);

    randomize_buffer(src0);
    randomize_buffer(src1);

    memcpy(cdst,  src0, LEN * sizeof(*src0));
    memcpy(cdst1, src1, LEN * sizeof(*src1));
    memcpy(odst,  src0, LEN * sizeof(*src0));
    memcpy(odst1, src1, LEN * sizeof(*src1));

    call_ref(cdst, cdst1, LEN);
    call_new(odst, odst1, LEN);
    for (int i = 0; i < LEN; i++) {
        if (!float_near_abs_eps(cdst[i],  odst[i],  FLT_EPSILON) ||
            !float_near_abs_eps(cdst1[i], odst1[i], FLT_EPSILON)) {
            fprintf(stderr, "%d: %- .12f - %- .12f = % .12g\n",
                    i, cdst[i], odst[i], cdst[i] - odst[i]);
            fprintf(stderr, "%d: %- .12f - %- .12f = % .12g\n",
                    i, cdst1[i], odst1[i], cdst1[i] - odst1[i]);
            fail();
            break;
        }
    }
    bench_new(src0, src1, LEN);
}

void checkasm_check_vorbisdsp(void)
{
    VorbisDSPContext dsp;

    ff_vorbisdsp_init(&dsp);

    if (check_func(dsp.vorbis_inverse_coupling, "inverse_coupling"))
        test_inverse_coupling();
    report("inverse_coupling");
}
