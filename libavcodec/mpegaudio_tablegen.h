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

#ifndef MPEGAUDIO_TABLEGEN_H
#define MPEGAUDIO_TABLEGEN_H

#include <stdint.h>
// do not use libavutil/mathematics.h since this is compiled both
// for the host and the target and config.h is only valid for the target
#include <math.h>

#define TABLE_4_3_SIZE (8191 + 16)*4
#if CONFIG_HARDCODED_TABLES
#define mpegaudio_tableinit()
#include "libavcodec/mpegaudio_tables.h"
#else
static int8_t  table_4_3_exp[TABLE_4_3_SIZE];
static uint32_t table_4_3_value[TABLE_4_3_SIZE];
static uint32_t exp_table[512];
static uint32_t expval_table[512][16];

static void mpegaudio_tableinit(void)
{
    int i;
    for(i=1;i<TABLE_4_3_SIZE;i++) {
        double value = i/4;
        double f, fm;
        int e, m;
        f = value * cbrtf(value) * pow(2, (i&3)*0.25);
        fm = frexp(f, &e);
        m = (uint32_t)(fm*(1LL<<31) + 0.5);
        e+= FRAC_BITS - 31 + 5 - 100;

        /* normalized to FRAC_BITS */
        table_4_3_value[i] = m;
        table_4_3_exp[i] = -e;
    }
    for(i=0; i<512*16; i++){
        double value = i & 15;
        int exponent= (i>>4);
        double f= value * cbrtf(value) * pow(2, (exponent-400)*0.25 + FRAC_BITS + 5);
        expval_table[exponent][i&15]= llrint(f);
        if((i&15)==1)
            exp_table[exponent]= llrint(f);
    }
}
#endif /* CONFIG_HARDCODED_TABLES */

#endif /* MPEGAUDIO_TABLEGEN_H */
