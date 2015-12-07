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
#include <stdlib.h>

#include "libavutil/internal.h"
#include "libavutil/intfloat.h"
#include "libavcodec/dca.h"
#include "libavcodec/dcadsp.h"
#include "libavcodec/dcadata.h"

#include "checkasm.h"

#define randomize_lfe_fir(size)                                 \
    do {                                                        \
        int i;                                                  \
        for (i = 0; i < size; i++) {                            \
            float f = (float)rnd() / (UINT_MAX >> 1) - 1.0f;    \
            in[i] = f;                                          \
        }                                                       \
        for (i = 0; i < 256; i++) {                             \
            float f = (float)rnd() / (UINT_MAX >> 1) - 1.0f;    \
            coeffs[i] = f;                                      \
        }                                                       \
    } while (0)

#define check_lfe_fir(decifactor, eps)                                  \
    do {                                                                \
        LOCAL_ALIGNED_16(float, in,     [256 / decifactor]);            \
        LOCAL_ALIGNED_16(float, out0,   [decifactor * 2]);              \
        LOCAL_ALIGNED_16(float, out1,   [decifactor * 2]);              \
        LOCAL_ALIGNED_16(float, coeffs, [256]);                         \
        int i;                                                          \
        const float * in_ptr = in + (256 / decifactor) - 1;             \
        declare_func(void, float *out, const float *in, const float *coeffs); \
        /* repeat the test several times */                             \
        for (i = 0; i < 32; i++) {                                      \
            int j;                                                      \
            memset(out0,    0, sizeof(*out0) * 2 * decifactor);         \
            memset(out1, 0xFF, sizeof(*out1) * 2 * decifactor);         \
            randomize_lfe_fir(256 / decifactor);                        \
            call_ref(out0, in_ptr, coeffs);                             \
            call_new(out1, in_ptr, coeffs);                             \
            for (j = 0; j < 2 * decifactor; j++) {                      \
                if (!float_near_abs_eps(out0[j], out1[j], eps)) {       \
                    if (0) {                                            \
                        union av_intfloat32 x, y; x.f = out0[j]; y.f = out1[j]; \
                        fprintf(stderr, "%3d: %11g (0x%08x); %11g (0x%08x)\n", \
                                j, x.f, x.i, y.f, y.i);                 \
                    }                                                   \
                    fail();                                             \
                    break;                                              \
                }                                                       \
            }                                                           \
            bench_new(out1, in_ptr, coeffs);                            \
        }                                                               \
    } while (0)

#define randomize_decode_hf()                                   \
    do {                                                        \
        int i;                                                  \
        for (i = 0; i < DCA_SUBBANDS; i++) {                    \
            vq_num[i]   = rnd() >> 22;                          \
            scale[i][0] = rnd() >> 26;                          \
            scale[i][1] = INT32_MIN;                            \
        }                                                       \
    } while (0)

void checkasm_check_dcadsp(void)
{
    DCADSPContext c;

    ff_dcadsp_init(&c);

    /* values are limited to {-8, 8} so absolute epsilon is good enough */
    if (check_func(c.lfe_fir[0], "dca_lfe_fir0"))
        check_lfe_fir(32, 1.0e-6f);

    if (check_func(c.lfe_fir[1], "dca_lfe_fir1"))
        check_lfe_fir(64, 1.0e-6f);

    if (check_func(c.decode_hf,  "dca_decode_hf")) {
        LOCAL_ALIGNED_16(float,   dst0,   [DCA_SUBBANDS], [8]);
        LOCAL_ALIGNED_16(float,   dst1,   [DCA_SUBBANDS], [8]);
        LOCAL_ALIGNED_16(int32_t, scale,  [DCA_SUBBANDS], [2]);
        LOCAL_ALIGNED_16(int32_t, vq_num, [DCA_SUBBANDS]);
        intptr_t start, end = 32, offset;

        declare_func(void, float[DCA_SUBBANDS][8], const int32_t[DCA_SUBBANDS],
                     const int8_t[1024][DCA_SUBBANDS], intptr_t, int32_t[DCA_SUBBANDS][2],
                     intptr_t, intptr_t);

        for (start = 0; start < 32; start++) {
            for (offset = 0; offset < 32; offset += 8) {
                int j;
                for (j = 0; j < DCA_SUBBANDS; j++) {
                    memset(dst0[j], 0, sizeof(*(dst0[j])) * 8);
                    memset(dst1[j], 0, sizeof(*(dst1[j])) * 8);
                }
                randomize_decode_hf();

                call_ref(dst0, vq_num, ff_dca_high_freq_vq, offset, scale, start, end);
                call_new(dst1, vq_num, ff_dca_high_freq_vq, offset, scale, start, end);

                for (j = 0; j < 8 * DCA_SUBBANDS; j++) {
                    if (!float_near_ulp(dst0[j>>3][j&7], dst1[j>>3][j&7], 1)) {
                        fail();
                        break;
                    }
                }

                bench_new(dst1, vq_num, ff_dca_high_freq_vq, offset, scale, start, end);
            }
        }
    }

    report("dcadsp");
}
