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
#include "qdm2data.h"

#define SOFTCLIP_THRESHOLD 27600
#define HARDCLIP_THRESHOLD 35716

#if CONFIG_HARDCODED_TABLES
#define softclip_table_init()
#define rnd_table_init()
#define init_noise_samples()
#define qdm2_init_vlc()
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
    uint32_t ldw;
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
            random_dequant_index[i][j] = ldw / random_seed;
            ldw %= random_seed;
            random_seed /= 3;
        }
    }
    for (i = 0; i < 128 ;i++) {
        random_seed = 25;
        ldw = i;
        for (j = 0; j < 3 ;j++) {
            random_dequant_type24[i][j] = ldw / random_seed;
            ldw %= random_seed;
            random_seed /= 5;
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

static VLC vlc_tab_level;
static VLC vlc_tab_diff;
static VLC vlc_tab_run;
static VLC fft_level_exp_alt_vlc;
static VLC fft_level_exp_vlc;
static VLC fft_stereo_exp_vlc;
static VLC fft_stereo_phase_vlc;
static VLC vlc_tab_tone_level_idx_hi1;
static VLC vlc_tab_tone_level_idx_mid;
static VLC vlc_tab_tone_level_idx_hi2;
static VLC vlc_tab_type30;
static VLC vlc_tab_type34;
static VLC vlc_tab_fft_tone_offset[5];

static const uint16_t qdm2_vlc_offs[] = {
    0,260,566,598,894,1166,1230,1294,1678,1950,2214,2278,2310,2570,2834,3124,3448,3838,
};

static VLC_TYPE qdm2_table[3838][2];

static av_cold void build_vlc(VLC *vlc, int nb_bits, int nb_codes, int idx,
                              const uint8_t tab[][2])
{
    vlc->table           = &qdm2_table[qdm2_vlc_offs[idx]];
    vlc->table_allocated = qdm2_vlc_offs[idx + 1] - qdm2_vlc_offs[idx];
    ff_init_vlc_from_lengths(vlc, nb_bits, nb_codes,
                             &tab[0][1], 2, &tab[0][0], 2, 1,
                             0, INIT_VLC_USE_NEW_STATIC | INIT_VLC_LE, NULL);
}

static av_cold void qdm2_init_vlc(void)
{
    build_vlc(&vlc_tab_level, 8, 24, 0, tab_level);
    build_vlc(&vlc_tab_diff,  8, 33, 1, tab_diff);
    build_vlc(&vlc_tab_run,   5,  6, 2, tab_run);

    build_vlc(&fft_level_exp_alt_vlc, 8, 28, 3, fft_level_exp_alt);
    build_vlc(&fft_level_exp_vlc,     8, 20, 4, fft_level_exp);

    build_vlc(&fft_stereo_exp_vlc,   6, 7, 5, fft_stereo_exp);
    build_vlc(&fft_stereo_phase_vlc, 6, 9, 6, fft_stereo_phase);

    build_vlc(&vlc_tab_tone_level_idx_hi1, 8, 20, 7, tab_tone_level_idx_hi1);
    build_vlc(&vlc_tab_tone_level_idx_mid, 8, 13, 8, tab_tone_level_idx_mid);
    build_vlc(&vlc_tab_tone_level_idx_hi2, 8, 18, 9, tab_tone_level_idx_hi2);

    build_vlc(&vlc_tab_type30, 6,  9, 10, tab_type30);
    build_vlc(&vlc_tab_type34, 5, 10, 11, tab_type34);

    build_vlc(&vlc_tab_fft_tone_offset[0], 8, 23, 12, tab_fft_tone_offset_0);
    build_vlc(&vlc_tab_fft_tone_offset[1], 8, 28, 13, tab_fft_tone_offset_1);
    build_vlc(&vlc_tab_fft_tone_offset[2], 8, 31, 14, tab_fft_tone_offset_2);
    build_vlc(&vlc_tab_fft_tone_offset[3], 8, 34, 15, tab_fft_tone_offset_3);
    build_vlc(&vlc_tab_fft_tone_offset[4], 8, 37, 16, tab_fft_tone_offset_4);
}

#endif /* CONFIG_HARDCODED_TABLES */

#endif /* AVCODEC_QDM2_TABLEGEN_H */
