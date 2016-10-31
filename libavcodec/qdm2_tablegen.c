/*
 * Generate a header file for hardcoded QDM2 tables
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

#include <stdlib.h>
#include "tableprint_vlc.h"
#define CONFIG_HARDCODED_TABLES 0
#include "qdm2_tablegen.h"

int main(void)
{
    softclip_table_init();
    rnd_table_init();
    init_noise_samples();

    write_fileheader();

    WRITE_ARRAY("static const", uint16_t, softclip_table);
    WRITE_ARRAY("static const", float, noise_table);
    WRITE_ARRAY("static const", float, noise_samples);

    WRITE_2D_ARRAY("static const", uint8_t, random_dequant_index);
    WRITE_2D_ARRAY("static const", uint8_t, random_dequant_type24);

    qdm2_init_vlc();

    WRITE_2D_ARRAY("static const", VLC_TYPE, qdm2_table);
    WRITE_VLC_TYPE("static const", vlc_tab_level, qdm2_table);
    WRITE_VLC_TYPE("static const", vlc_tab_diff, qdm2_table);
    WRITE_VLC_TYPE("static const", vlc_tab_run, qdm2_table);
    WRITE_VLC_TYPE("static const", fft_level_exp_alt_vlc, qdm2_table);
    WRITE_VLC_TYPE("static const", fft_level_exp_vlc, qdm2_table);
    WRITE_VLC_TYPE("static const", fft_stereo_exp_vlc, qdm2_table);
    WRITE_VLC_TYPE("static const", fft_stereo_phase_vlc, qdm2_table);
    WRITE_VLC_TYPE("static const", vlc_tab_tone_level_idx_hi1, qdm2_table);
    WRITE_VLC_TYPE("static const", vlc_tab_tone_level_idx_mid, qdm2_table);
    WRITE_VLC_TYPE("static const", vlc_tab_tone_level_idx_hi2, qdm2_table);
    WRITE_VLC_TYPE("static const", vlc_tab_type30, qdm2_table);
    WRITE_VLC_TYPE("static const", vlc_tab_type34, qdm2_table);
    WRITE_VLC_ARRAY("static const", vlc_tab_fft_tone_offset, qdm2_table);

    return 0;
}
