/*
 * Copyright (c) 2015 Janne Grunau
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with Libav; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "libavutil/internal.h"
#include "libavutil/intfloat.h"
#include "libavcodec/dcadata.h"
#include "libavcodec/synth_filter.h"

#include "checkasm.h"

#define BUF_SIZE 32

#define randomize_input()                                       \
    do {                                                        \
        int i;                                                  \
        for (i = 0; i < BUF_SIZE; i++) {                        \
            float f = (float)rnd() / (UINT_MAX >> 5) - 16.0f;   \
            in[i] = f;                                          \
        }                                                       \
    } while (0)

void checkasm_check_synth_filter(void)
{
    FFTContext imdct;
    SynthFilterContext synth;

    ff_mdct_init(&imdct, 6, 1, 1.0);
    ff_synth_filter_init(&synth);

    if (check_func(synth.synth_filter_float, "synth_filter_float")) {
        LOCAL_ALIGNED(32, float,   out0,   [BUF_SIZE]);
        LOCAL_ALIGNED(32, float,   out1,   [BUF_SIZE]);
        LOCAL_ALIGNED(32, float,   out_b,  [BUF_SIZE]);
        LOCAL_ALIGNED(32, float,   in,     [BUF_SIZE]);
        LOCAL_ALIGNED(32, float,   buf2_0, [BUF_SIZE]);
        LOCAL_ALIGNED(32, float,   buf2_1, [BUF_SIZE]);
        LOCAL_ALIGNED(32, float,   buf2_b, [BUF_SIZE]);
        LOCAL_ALIGNED(32, float,   buf0,   [512]);
        LOCAL_ALIGNED(32, float,   buf1,   [512]);
        LOCAL_ALIGNED(32, float,   buf_b,  [512]);
        float scale = 1.0f;
        int i, offset0 = 0, offset1 = 0, offset_b = 0;

        declare_func(void, FFTContext *, float *, int *, float[32], const float[512],
                     float[32], float[32], float);

        memset(buf2_0, 0, sizeof(*buf2_0) * BUF_SIZE);
        memset(buf2_1, 0, sizeof(*buf2_1) * BUF_SIZE);
        memset(buf2_b, 0, sizeof(*buf2_b) * BUF_SIZE);
        memset(buf0, 0, sizeof(*buf2_0) * 512);
        memset(buf1, 0, sizeof(*buf2_1) * 512);
        memset(buf_b, 0, sizeof(*buf2_b) * 512);

        /* more than 1 synth_buf_offset wrap-around */
        for (i = 0; i < 20; i++) {
            int j;
            const float * window = (i & 1) ? ff_dca_fir_32bands_perfect : ff_dca_fir_32bands_nonperfect;

            memset(out0, 0, sizeof(*out0) * BUF_SIZE);
            memset(out1, 0, sizeof(*out1) * BUF_SIZE);
            memset(out_b, 0, sizeof(*out_b) * BUF_SIZE);

            randomize_input();

            call_ref(&imdct, buf0, &offset0, buf2_0, window,
                     out0, in, scale);
            call_new(&imdct, buf1, &offset1, buf2_1, window,
                     out1, in, scale);

            if (offset0 != offset1) {
                fail();
                fprintf(stderr, "offsets do not match: %d, %d", offset0, offset1);
                break;
            }

            for (j = 0; j < BUF_SIZE; j++) {
                if (!float_near_abs_eps_ulp(out0[j],   out1[j],   7.0e-7, 16) ||
                    !float_near_abs_eps_ulp(buf2_0[j], buf2_1[j], 7.0e-7, 16)) {
                    union av_intfloat32 o0, o1, b0, b1;

                    fail();
                    o0.f = out0[j];   o1.f = out1[j];
                    b0.f = buf2_0[j], b1.f = buf2_1[j];
                    fprintf(stderr, "out:  %11g (0x%08x); %11g (0x%08x); abs diff %11g\n",
                            o0.f, o0.i, o1.f, o1.i, fabsf(o0.f - o1.f));
                    fprintf(stderr, "buf2: %11g (0x%08x); %11g (0x%08x); abs diff %11g\n",
                            b0.f, b0.i, b1.f, b1.i, fabsf(b0.f - b1.f));
                    break;
                }
            }

            bench_new(&imdct, buf_b, &offset_b, buf2_b, window,
                      out_b, in, scale);
        }
    }
    ff_mdct_end(&imdct);

    report("synth_filter");
}
