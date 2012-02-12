/*
 * AC-3 tables
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
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

#ifndef AVCODEC_AC3TAB_H
#define AVCODEC_AC3TAB_H

#include "libavutil/common.h"
#include "ac3.h"

#if CONFIG_HARDCODED_TABLES
#   define HCONST const
#else
#   define HCONST
#endif

extern const uint16_t ff_ac3_frame_size_tab[38][3];
extern const uint8_t  ff_ac3_channels_tab[8];
extern const uint16_t avpriv_ac3_channel_layout_tab[8];
extern const uint8_t  ff_ac3_enc_channel_map[8][2][6];
extern const uint8_t  ff_ac3_dec_channel_map[8][2][6];
extern const uint16_t ff_ac3_sample_rate_tab[3];
extern const uint16_t ff_ac3_bitrate_tab[19];
extern const uint8_t  ff_ac3_rematrix_band_tab[5];
extern const uint8_t  ff_eac3_default_cpl_band_struct[18];
extern const int16_t  ff_ac3_window[AC3_WINDOW_SIZE/2];
extern const uint8_t  ff_ac3_log_add_tab[260];
extern const uint16_t ff_ac3_hearing_threshold_tab[AC3_CRITICAL_BANDS][3];
extern const uint8_t  ff_ac3_bap_tab[64];
extern const uint8_t  ff_ac3_slow_decay_tab[4];
extern const uint8_t  ff_ac3_fast_decay_tab[4];
extern const uint16_t ff_ac3_slow_gain_tab[4];
extern const uint16_t ff_ac3_db_per_bit_tab[4];
extern const int16_t  ff_ac3_floor_tab[8];
extern const uint16_t ff_ac3_fast_gain_tab[8];
extern const uint16_t ff_eac3_default_chmap[8];
extern const uint8_t  ff_ac3_band_start_tab[AC3_CRITICAL_BANDS+1];
extern HCONST uint8_t ff_ac3_bin_to_band_tab[253];

/** Custom channel map locations bitmask
 *  Other channels described in documentation:
 *      Lc/Rc pair, Lrs/Rrs pair, Ts, Lsd/Rsd pair,
 *      Lw/Rw pair, Lvh/Rvh pair, Cvh, Reserved, LFE2
 */
enum CustomChannelMapLocation{
    AC3_CHMAP_L=        1<<(15-0),
    AC3_CHMAP_C=        1<<(15-1),
    AC3_CHMAP_R=        1<<(15-2),
    AC3_CHMAP_L_SUR=    1<<(15-3),
    AC3_CHMAP_R_SUR =   1<<(15-4),
    AC3_CHMAP_C_SUR=    1<<(15-7),
    AC3_CHMAP_LFE =     1<<(15-15)
};

#endif /* AVCODEC_AC3TAB_H */
