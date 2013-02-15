/*
 * Header file for hardcoded QDM2 tables
 *
 * Copyright (c) 2010 Reimar Döffinger <Reimar.Doeffinger@gmx.de>
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

#ifndef AVCODEC_QDM2_TABLEGEN_H
#define AVCODEC_QDM2_TABLEGEN_H

#include <stdint.h>
#include <math.h>
#include "libavutil/attributes.h"

#define SOFTCLIP_THRESHOLD 27600
#define HARDCLIP_THRESHOLD 35716

#if CONFIG_HARDCODED_TABLES
#define softclip_table_init()
#define rnd_table_init()
#define init_noise_samples()
#include "libavcodec/qdm2_tables.h"
#else
static uint16_t softclip_table[HARDCLIP_THRESHOLD - SOFTCLIP_THRESHOLD + 1];
static float noise_table[4096 + 20];
static uint8_t random_dequant_index[256][5];
static uint8_t random_dequant_type24[128][3];
static float noise_samples[128];

static av_cold void softclip_table_init(void) {
    int i;
    double dfl = SOFTCLIP_THRESHOLD - 32767;
    float delta = 1.0 / -dfl;
    for (i = 0; i < HARDCLIP_THRESHOLD - SOFTCLIP_THRESHOLD + 1; i++)
        softclip_table[i] = SOFTCLIP_THRESHOLD - ((int)(sin((float)i * delta) * dfl) & 0x0000FFFF);
}


// random generated table
static av_cold void rnd_table_init(void) {
    int i,j;
    uint32_t ldw,hdw;
    uint64_t tmp64_1;
    uint64_t random_seed = 0;
    float delta = 1.0 / 16384.0;
    for(i = 0; i < 4096 ;i++) {
        random_seed = random_seed * 214013 + 2531011;
        noise_table[i] = (delta * (float)(((int32_t)random_seed >> 16) & 0x00007FFF)- 1.0) * 1.3;
    }

    for (i = 0; i < 256 ;i++) {
        random_seed = 81;
        ldw = i;
        for (j = 0; j < 5 ;j++) {
            random_dequant_index[i][j] = (uint8_t)((ldw / random_seed) & 0xFF);
            ldw = (uint32_t)ldw % (uint32_t)random_seed;
            tmp64_1 = (random_seed * 0x55555556);
            hdw = (uint32_t)(tmp64_1 >> 32);
            random_seed = (uint64_t)(hdw + (ldw >> 31));
        }
    }
    for (i = 0; i < 128 ;i++) {
        random_seed = 25;
        ldw = i;
        for (j = 0; j < 3 ;j++) {
            random_dequant_type24[i][j] = (uint8_t)((ldw / random_seed) & 0xFF);
            ldw = (uint32_t)ldw % (uint32_t)random_seed;
            tmp64_1 = (random_seed * 0x66666667);
            hdw = (uint32_t)(tmp64_1 >> 33);
            random_seed = hdw + (ldw >> 31);
        }
    }
}


static av_cold void init_noise_samples(void) {
    int i;
    unsigned random_seed = 0;
    float delta = 1.0 / 16384.0;
    for (i = 0; i < 128;i++) {
        random_seed = random_seed * 214013 + 2531011;
        noise_samples[i] = (delta * (float)((random_seed >> 16) & 0x00007fff) - 1.0);
    }
}
#endif /* CONFIG_HARDCODED_TABLES */

#endif /* AVCODEC_QDM2_TABLEGEN_H */
