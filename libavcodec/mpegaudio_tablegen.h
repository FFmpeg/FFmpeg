/*
 * Header file for hardcoded mpegaudiodec tables
 *
 * Copyright (c) 2009 Reimar Döffinger <Reimar.Doeffinger@gmx.de>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVCODEC_MPEGAUDIO_TABLEGEN_H
#define AVCODEC_MPEGAUDIO_TABLEGEN_H

#include <stdint.h>
#include <math.h>

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

static void mpegaudio_tableinit(void)
{
    int i, value, exponent;
    for (i = 1; i < TABLE_4_3_SIZE; i++) {
        double value = i / 4;
        double f, fm;
        int e, m;
        f  = value * cbrtf(value) * pow(2, (i & 3) * 0.25);
        fm = frexp(f, &e);
        m  = (uint32_t)(fm * (1LL << 31) + 0.5);
        e += FRAC_BITS - 31 + 5 - 100;

        /* normalized to FRAC_BITS */
        table_4_3_value[i] =  m;
        table_4_3_exp[i]   = -e;
    }
    for (exponent = 0; exponent < 512; exponent++) {
        for (value = 0; value < 16; value++) {
            double f = (double)value * cbrtf(value) * pow(2, (exponent - 400) * 0.25 + FRAC_BITS + 5);
            expval_table_fixed[exponent][value] = llrint(f);
            expval_table_float[exponent][value] = f;
        }
        exp_table_fixed[exponent] = expval_table_fixed[exponent][1];
        exp_table_float[exponent] = expval_table_float[exponent][1];
    }
}
#endif /* CONFIG_HARDCODED_TABLES */

#endif /* AVCODEC_MPEGAUDIO_TABLEGEN_H */
