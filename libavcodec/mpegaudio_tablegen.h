/*
 * Header file for hardcoded mpegaudiodec tables
 *
 * Copyright (c) 2009 Reimar Döffinger <Reimar.Doeffinger@gmx.de>
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

#ifndef AVCODEC_MPEGAUDIO_TABLEGEN_H
#define AVCODEC_MPEGAUDIO_TABLEGEN_H

#include <stdint.h>
#include <math.h>
#include "libavutil/attributes.h"

#define TABLE_4_3_SIZE (8191 + 16)*4
#if CONFIG_HARDCODED_TABLES
#define mpegaudio_tableinit()
#include "libavcodec/mpegaudio_tables.h"
#else
static int8_t   table_4_3_exp[TABLE_4_3_SIZE];
static uint32_t table_4_3_value[TABLE_4_3_SIZE];
static uint32_t exp_table_fixed[512];
static uint32_t expval_table_fixed[512][16];
static float exp_table_float[512];
static float expval_table_float[512][16];

#define FRAC_BITS 23
#define IMDCT_SCALAR 1.759

static av_cold void mpegaudio_tableinit(void)
{
    int i, value, exponent;
    static const double exp2_lut[4] = {
        1.00000000000000000000, /* 2 ^ (0 * 0.25) */
        1.18920711500272106672, /* 2 ^ (1 * 0.25) */
        M_SQRT2               , /* 2 ^ (2 * 0.25) */
        1.68179283050742908606, /* 2 ^ (3 * 0.25) */
    };
    static double pow43_lut[16];
    double exp2_base = 2.11758236813575084767080625169910490512847900390625e-22; // 2^(-72)
    double exp2_val;
    double pow43_val = 0;
    for (i = 0; i < 16; ++i)
        pow43_lut[i] = i * cbrt(i);

    for (i = 1; i < TABLE_4_3_SIZE; i++) {
        double f, fm;
        int e, m;
        double value = i / 4;
        if ((i & 3) == 0)
            pow43_val = value / IMDCT_SCALAR * cbrt(value);
        f  = pow43_val * exp2_lut[i & 3];
        fm = frexp(f, &e);
        m  = llrint(fm * (1LL << 31));
        e += FRAC_BITS - 31 + 5 - 100;

        /* normalized to FRAC_BITS */
        table_4_3_value[i] =  m;
        table_4_3_exp[i]   = -e;
    }
    for (exponent = 0; exponent < 512; exponent++) {
        if (exponent && (exponent & 3) == 0)
            exp2_base *= 2;
        exp2_val = exp2_base * exp2_lut[exponent & 3] / IMDCT_SCALAR;
        for (value = 0; value < 16; value++) {
            double f = pow43_lut[value] * exp2_val;
            expval_table_fixed[exponent][value] = (f < 0xFFFFFFFF ? llrint(f) : 0xFFFFFFFF);
            expval_table_float[exponent][value] = f;
        }
        exp_table_fixed[exponent] = expval_table_fixed[exponent][1];
        exp_table_float[exponent] = expval_table_float[exponent][1];
    }
}
#endif /* CONFIG_HARDCODED_TABLES */

#endif /* AVCODEC_MPEGAUDIO_TABLEGEN_H */
