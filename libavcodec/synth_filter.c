/*
 * copyright (c) 2008 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (C) 2016 foo86
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

#include "fft.h"
#include "dcadct.h"
#include "dcamath.h"
#include "synth_filter.h"

static void synth_filter_float(FFTContext *imdct,
                               float *synth_buf_ptr, int *synth_buf_offset,
                               float synth_buf2[32], const float window[512],
                               float out[32], const float in[32], float scale)
{
    float *synth_buf = synth_buf_ptr + *synth_buf_offset;
    int i, j;

    imdct->imdct_half(imdct, synth_buf, in);

    for (i = 0; i < 16; i++) {
        float a = synth_buf2[i     ];
        float b = synth_buf2[i + 16];
        float c = 0;
        float d = 0;
        for (j = 0; j < 512 - *synth_buf_offset; j += 64) {
            a += window[i + j     ] * (-synth_buf[15 - i + j      ]);
            b += window[i + j + 16] * ( synth_buf[     i + j      ]);
            c += window[i + j + 32] * ( synth_buf[16 + i + j      ]);
            d += window[i + j + 48] * ( synth_buf[31 - i + j      ]);
        }
        for (     ; j < 512; j += 64) {
            a += window[i + j     ] * (-synth_buf[15 - i + j - 512]);
            b += window[i + j + 16] * ( synth_buf[     i + j - 512]);
            c += window[i + j + 32] * ( synth_buf[16 + i + j - 512]);
            d += window[i + j + 48] * ( synth_buf[31 - i + j - 512]);
        }
        out[i     ] = a * scale;
        out[i + 16] = b * scale;
        synth_buf2[i     ] = c;
        synth_buf2[i + 16] = d;
    }

    *synth_buf_offset = (*synth_buf_offset - 32) & 511;
}

static void synth_filter_float_64(FFTContext *imdct,
                                  float *synth_buf_ptr, int *synth_buf_offset,
                                  float synth_buf2[64], const float window[1024],
                                  float out[64], const float in[64], float scale)
{
    float *synth_buf = synth_buf_ptr + *synth_buf_offset;
    int i, j;

    imdct->imdct_half(imdct, synth_buf, in);

    for (i = 0; i < 32; i++) {
        float a = synth_buf2[i     ];
        float b = synth_buf2[i + 32];
        float c = 0;
        float d = 0;
        for (j = 0; j < 1024 - *synth_buf_offset; j += 128) {
            a += window[i + j     ] * (-synth_buf[31 - i + j       ]);
            b += window[i + j + 32] * ( synth_buf[     i + j       ]);
            c += window[i + j + 64] * ( synth_buf[32 + i + j       ]);
            d += window[i + j + 96] * ( synth_buf[63 - i + j       ]);
        }
        for (     ; j < 1024; j += 128) {
            a += window[i + j     ] * (-synth_buf[31 - i + j - 1024]);
            b += window[i + j + 32] * ( synth_buf[     i + j - 1024]);
            c += window[i + j + 64] * ( synth_buf[32 + i + j - 1024]);
            d += window[i + j + 96] * ( synth_buf[63 - i + j - 1024]);
        }
        out[i     ] = a * scale;
        out[i + 32] = b * scale;
        synth_buf2[i     ] = c;
        synth_buf2[i + 32] = d;
    }

    *synth_buf_offset = (*synth_buf_offset - 64) & 1023;
}

static void synth_filter_fixed(DCADCTContext *imdct,
                               int32_t *synth_buf_ptr, int *synth_buf_offset,
                               int32_t synth_buf2[32], const int32_t window[512],
                               int32_t out[32], const int32_t in[32])
{
    int32_t *synth_buf = synth_buf_ptr + *synth_buf_offset;
    int i, j;

    imdct->imdct_half[0](synth_buf, in);

    for (i = 0; i < 16; i++) {
        int64_t a = synth_buf2[i     ] * (INT64_C(1) << 21);
        int64_t b = synth_buf2[i + 16] * (INT64_C(1) << 21);
        int64_t c = 0;
        int64_t d = 0;
        for (j = 0; j < 512 - *synth_buf_offset; j += 64) {
            a += (int64_t)window[i + j     ] * synth_buf[     i + j      ];
            b += (int64_t)window[i + j + 16] * synth_buf[15 - i + j      ];
            c += (int64_t)window[i + j + 32] * synth_buf[16 + i + j      ];
            d += (int64_t)window[i + j + 48] * synth_buf[31 - i + j      ];
        }
        for (     ; j < 512; j += 64) {
            a += (int64_t)window[i + j     ] * synth_buf[     i + j - 512];
            b += (int64_t)window[i + j + 16] * synth_buf[15 - i + j - 512];
            c += (int64_t)window[i + j + 32] * synth_buf[16 + i + j - 512];
            d += (int64_t)window[i + j + 48] * synth_buf[31 - i + j - 512];
        }
        out[i     ] = clip23(norm21(a));
        out[i + 16] = clip23(norm21(b));
        synth_buf2[i     ] = norm21(c);
        synth_buf2[i + 16] = norm21(d);
    }

    *synth_buf_offset = (*synth_buf_offset - 32) & 511;
}

static void synth_filter_fixed_64(DCADCTContext *imdct,
                                  int32_t *synth_buf_ptr, int *synth_buf_offset,
                                  int32_t synth_buf2[64], const int32_t window[1024],
                                  int32_t out[64], const int32_t in[64])
{
    int32_t *synth_buf = synth_buf_ptr + *synth_buf_offset;
    int i, j;

    imdct->imdct_half[1](synth_buf, in);

    for (i = 0; i < 32; i++) {
        int64_t a = synth_buf2[i     ] * (INT64_C(1) << 20);
        int64_t b = synth_buf2[i + 32] * (INT64_C(1) << 20);
        int64_t c = 0;
        int64_t d = 0;
        for (j = 0; j < 1024 - *synth_buf_offset; j += 128) {
            a += (int64_t)window[i + j     ] * synth_buf[     i + j       ];
            b += (int64_t)window[i + j + 32] * synth_buf[31 - i + j       ];
            c += (int64_t)window[i + j + 64] * synth_buf[32 + i + j       ];
            d += (int64_t)window[i + j + 96] * synth_buf[63 - i + j       ];
        }
        for (     ; j < 1024; j += 128) {
            a += (int64_t)window[i + j     ] * synth_buf[     i + j - 1024];
            b += (int64_t)window[i + j + 32] * synth_buf[31 - i + j - 1024];
            c += (int64_t)window[i + j + 64] * synth_buf[32 + i + j - 1024];
            d += (int64_t)window[i + j + 96] * synth_buf[63 - i + j - 1024];
        }
        out[i     ] = clip23(norm20(a));
        out[i + 32] = clip23(norm20(b));
        synth_buf2[i     ] = norm20(c);
        synth_buf2[i + 32] = norm20(d);
    }

    *synth_buf_offset = (*synth_buf_offset - 64) & 1023;
}

av_cold void ff_synth_filter_init(SynthFilterContext *c)
{
    c->synth_filter_float    = synth_filter_float;
    c->synth_filter_float_64 = synth_filter_float_64;
    c->synth_filter_fixed    = synth_filter_fixed;
    c->synth_filter_fixed_64 = synth_filter_fixed_64;

#if ARCH_AARCH64
    ff_synth_filter_init_aarch64(c);
#elif ARCH_ARM
    ff_synth_filter_init_arm(c);
#elif ARCH_X86
    ff_synth_filter_init_x86(c);
#endif
}
