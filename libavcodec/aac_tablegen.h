/*
 * Header file for hardcoded AAC tables
 *
 * Copyright (c) 2010 Alex Converse <alex.converse@gmail.com>
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

#ifndef AVCODEC_AAC_TABLEGEN_H
#define AVCODEC_AAC_TABLEGEN_H

#include "aac_tablegen_decl.h"

#if CONFIG_HARDCODED_TABLES
#include "libavcodec/aac_tables.h"
#else
#include "libavutil/mathematics.h"
float ff_aac_pow2sf_tab[428];
float ff_aac_pow34sf_tab[428];

av_cold void ff_aac_tableinit(void)
{
    int i;

    /* 2^(i/16) for 0 <= i <= 15 */
    static const float exp2_lut[] = {
        1.00000000000000000000,
        1.04427378242741384032,
        1.09050773266525765921,
        1.13878863475669165370,
        1.18920711500272106672,
        1.24185781207348404859,
        1.29683955465100966593,
        1.35425554693689272830,
        1.41421356237309504880,
        1.47682614593949931139,
        1.54221082540794082361,
        1.61049033194925430818,
        1.68179283050742908606,
        1.75625216037329948311,
        1.83400808640934246349,
        1.91520656139714729387,
    };
    float t1 = 8.8817841970012523233890533447265625e-16; // 2^(-50)
    float t2 = 3.63797880709171295166015625e-12; // 2^(-38)
    int t1_inc_cur, t2_inc_cur;
    int t1_inc_prev = 0;
    int t2_inc_prev = 8;

    for (i = 0; i < 428; i++) {
        t1_inc_cur = 4 * (i % 4);
        t2_inc_cur = (8 + 3*i) % 16;
        if (t1_inc_cur < t1_inc_prev)
            t1 *= 2;
        if (t2_inc_cur < t2_inc_prev)
            t2 *= 2;
        // A much more efficient and accurate way of doing:
        // ff_aac_pow2sf_tab[i] = pow(2, (i - POW_SF2_ZERO) / 4.0);
        // ff_aac_pow34sf_tab[i] = pow(ff_aac_pow2sf_tab[i], 3.0/4.0);
        ff_aac_pow2sf_tab[i] = t1 * exp2_lut[t1_inc_cur];
        ff_aac_pow34sf_tab[i] = t2 * exp2_lut[t2_inc_cur];
        t1_inc_prev = t1_inc_cur;
        t2_inc_prev = t2_inc_cur;
    }
}
#endif /* CONFIG_HARDCODED_TABLES */

#endif /* AVCODEC_AAC_TABLEGEN_H */
