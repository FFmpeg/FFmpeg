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

static av_cold void qdm2_init_vlc(void)
{
    vlc_tab_level.table           = &qdm2_table[qdm2_vlc_offs[0]];
    vlc_tab_level.table_allocated = qdm2_vlc_offs[1] - qdm2_vlc_offs[0];
    init_vlc(&vlc_tab_level, 8, 24,
             vlc_tab_level_huffbits, 1, 1,
             vlc_tab_level_huffcodes, 2, 2,
             INIT_VLC_USE_NEW_STATIC | INIT_VLC_LE);

    vlc_tab_diff.table           = &qdm2_table[qdm2_vlc_offs[1]];
    vlc_tab_diff.table_allocated = qdm2_vlc_offs[2] - qdm2_vlc_offs[1];
    init_vlc(&vlc_tab_diff, 8, 37,
             vlc_tab_diff_huffbits, 1, 1,
             vlc_tab_diff_huffcodes, 2, 2,
             INIT_VLC_USE_NEW_STATIC | INIT_VLC_LE);

    vlc_tab_run.table           = &qdm2_table[qdm2_vlc_offs[2]];
    vlc_tab_run.table_allocated = qdm2_vlc_offs[3] - qdm2_vlc_offs[2];
    init_vlc(&vlc_tab_run, 5, 6,
             vlc_tab_run_huffbits, 1, 1,
             vlc_tab_run_huffcodes, 1, 1,
             INIT_VLC_USE_NEW_STATIC | INIT_VLC_LE);

    fft_level_exp_alt_vlc.table           = &qdm2_table[qdm2_vlc_offs[3]];
    fft_level_exp_alt_vlc.table_allocated = qdm2_vlc_offs[4] -
                                            qdm2_vlc_offs[3];
    init_vlc(&fft_level_exp_alt_vlc, 8, 28,
             fft_level_exp_alt_huffbits, 1, 1,
             fft_level_exp_alt_huffcodes, 2, 2,
             INIT_VLC_USE_NEW_STATIC | INIT_VLC_LE);

    fft_level_exp_vlc.table           = &qdm2_table[qdm2_vlc_offs[4]];
    fft_level_exp_vlc.table_allocated = qdm2_vlc_offs[5] - qdm2_vlc_offs[4];
    init_vlc(&fft_level_exp_vlc, 8, 20,
             fft_level_exp_huffbits, 1, 1,
             fft_level_exp_huffcodes, 2, 2,
             INIT_VLC_USE_NEW_STATIC | INIT_VLC_LE);

    fft_stereo_exp_vlc.table           = &qdm2_table[qdm2_vlc_offs[5]];
    fft_stereo_exp_vlc.table_allocated = qdm2_vlc_offs[6] -
                                         qdm2_vlc_offs[5];
    init_vlc(&fft_stereo_exp_vlc, 6, 7,
             fft_stereo_exp_huffbits, 1, 1,
             fft_stereo_exp_huffcodes, 1, 1,
             INIT_VLC_USE_NEW_STATIC | INIT_VLC_LE);

    fft_stereo_phase_vlc.table           = &qdm2_table[qdm2_vlc_offs[6]];
    fft_stereo_phase_vlc.table_allocated = qdm2_vlc_offs[7] -
                                           qdm2_vlc_offs[6];
    init_vlc(&fft_stereo_phase_vlc, 6, 9,
             fft_stereo_phase_huffbits, 1, 1,
             fft_stereo_phase_huffcodes, 1, 1,
             INIT_VLC_USE_NEW_STATIC | INIT_VLC_LE);

    vlc_tab_tone_level_idx_hi1.table =
        &qdm2_table[qdm2_vlc_offs[7]];
    vlc_tab_tone_level_idx_hi1.table_allocated = qdm2_vlc_offs[8] -
                                                 qdm2_vlc_offs[7];
    init_vlc(&vlc_tab_tone_level_idx_hi1, 8, 20,
             vlc_tab_tone_level_idx_hi1_huffbits, 1, 1,
             vlc_tab_tone_level_idx_hi1_huffcodes, 2, 2,
             INIT_VLC_USE_NEW_STATIC | INIT_VLC_LE);

    vlc_tab_tone_level_idx_mid.table =
        &qdm2_table[qdm2_vlc_offs[8]];
    vlc_tab_tone_level_idx_mid.table_allocated = qdm2_vlc_offs[9] -
                                                 qdm2_vlc_offs[8];
    init_vlc(&vlc_tab_tone_level_idx_mid, 8, 24,
             vlc_tab_tone_level_idx_mid_huffbits, 1, 1,
             vlc_tab_tone_level_idx_mid_huffcodes, 2, 2,
             INIT_VLC_USE_NEW_STATIC | INIT_VLC_LE);

    vlc_tab_tone_level_idx_hi2.table =
        &qdm2_table[qdm2_vlc_offs[9]];
    vlc_tab_tone_level_idx_hi2.table_allocated = qdm2_vlc_offs[10] -
                                                 qdm2_vlc_offs[9];
    init_vlc(&vlc_tab_tone_level_idx_hi2, 8, 24,
             vlc_tab_tone_level_idx_hi2_huffbits, 1, 1,
             vlc_tab_tone_level_idx_hi2_huffcodes, 2, 2,
             INIT_VLC_USE_NEW_STATIC | INIT_VLC_LE);

    vlc_tab_type30.table           = &qdm2_table[qdm2_vlc_offs[10]];
    vlc_tab_type30.table_allocated = qdm2_vlc_offs[11] - qdm2_vlc_offs[10];
    init_vlc(&vlc_tab_type30, 6, 9,
             vlc_tab_type30_huffbits, 1, 1,
             vlc_tab_type30_huffcodes, 1, 1,
             INIT_VLC_USE_NEW_STATIC | INIT_VLC_LE);

    vlc_tab_type34.table           = &qdm2_table[qdm2_vlc_offs[11]];
    vlc_tab_type34.table_allocated = qdm2_vlc_offs[12] - qdm2_vlc_offs[11];
    init_vlc(&vlc_tab_type34, 5, 10,
             vlc_tab_type34_huffbits, 1, 1,
             vlc_tab_type34_huffcodes, 1, 1,
             INIT_VLC_USE_NEW_STATIC | INIT_VLC_LE);

    vlc_tab_fft_tone_offset[0].table =
        &qdm2_table[qdm2_vlc_offs[12]];
    vlc_tab_fft_tone_offset[0].table_allocated = qdm2_vlc_offs[13] -
                                                 qdm2_vlc_offs[12];
    init_vlc(&vlc_tab_fft_tone_offset[0], 8, 23,
             vlc_tab_fft_tone_offset_0_huffbits, 1, 1,
             vlc_tab_fft_tone_offset_0_huffcodes, 2, 2,
             INIT_VLC_USE_NEW_STATIC | INIT_VLC_LE);

    vlc_tab_fft_tone_offset[1].table =
        &qdm2_table[qdm2_vlc_offs[13]];
    vlc_tab_fft_tone_offset[1].table_allocated = qdm2_vlc_offs[14] -
                                                 qdm2_vlc_offs[13];
    init_vlc(&vlc_tab_fft_tone_offset[1], 8, 28,
             vlc_tab_fft_tone_offset_1_huffbits, 1, 1,
             vlc_tab_fft_tone_offset_1_huffcodes, 2, 2,
             INIT_VLC_USE_NEW_STATIC | INIT_VLC_LE);

    vlc_tab_fft_tone_offset[2].table =
        &qdm2_table[qdm2_vlc_offs[14]];
    vlc_tab_fft_tone_offset[2].table_allocated = qdm2_vlc_offs[15] -
                                                 qdm2_vlc_offs[14];
    init_vlc(&vlc_tab_fft_tone_offset[2], 8, 32,
             vlc_tab_fft_tone_offset_2_huffbits, 1, 1,
             vlc_tab_fft_tone_offset_2_huffcodes, 2, 2,
             INIT_VLC_USE_NEW_STATIC | INIT_VLC_LE);

    vlc_tab_fft_tone_offset[3].table =
        &qdm2_table[qdm2_vlc_offs[15]];
    vlc_tab_fft_tone_offset[3].table_allocated = qdm2_vlc_offs[16] -
                                                 qdm2_vlc_offs[15];
    init_vlc(&vlc_tab_fft_tone_offset[3], 8, 35,
             vlc_tab_fft_tone_offset_3_huffbits, 1, 1,
             vlc_tab_fft_tone_offset_3_huffcodes, 2, 2,
             INIT_VLC_USE_NEW_STATIC | INIT_VLC_LE);

    vlc_tab_fft_tone_offset[4].table =
        &qdm2_table[qdm2_vlc_offs[16]];
    vlc_tab_fft_tone_offset[4].table_allocated = qdm2_vlc_offs[17] -
                                                 qdm2_vlc_offs[16];
    init_vlc(&vlc_tab_fft_tone_offset[4], 8, 38,
             vlc_tab_fft_tone_offset_4_huffbits, 1, 1,
             vlc_tab_fft_tone_offset_4_huffcodes, 2, 2,
             INIT_VLC_USE_NEW_STATIC | INIT_VLC_LE);
}

#endif /* CONFIG_HARDCODED_TABLES */

#endif /* AVCODEC_QDM2_TABLEGEN_H */
