/*
 * AC-3 tables
 * copyright (c) 2001 Fabrice Bellard
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

/**
 * @file
 * tables taken directly from the AC-3 spec.
 */

#include "libavutil/channel_layout.h"
#include "libavutil/mathematics.h"

#include "ac3tab.h"

/**
 * Possible frame sizes.
 * from ATSC A/52 Table 5.18 Frame Size Code Table.
 */
const uint16_t ff_ac3_frame_size_tab[38][3] = {
    { 64,   69,   96   },
    { 64,   70,   96   },
    { 80,   87,   120  },
    { 80,   88,   120  },
    { 96,   104,  144  },
    { 96,   105,  144  },
    { 112,  121,  168  },
    { 112,  122,  168  },
    { 128,  139,  192  },
    { 128,  140,  192  },
    { 160,  174,  240  },
    { 160,  175,  240  },
    { 192,  208,  288  },
    { 192,  209,  288  },
    { 224,  243,  336  },
    { 224,  244,  336  },
    { 256,  278,  384  },
    { 256,  279,  384  },
    { 320,  348,  480  },
    { 320,  349,  480  },
    { 384,  417,  576  },
    { 384,  418,  576  },
    { 448,  487,  672  },
    { 448,  488,  672  },
    { 512,  557,  768  },
    { 512,  558,  768  },
    { 640,  696,  960  },
    { 640,  697,  960  },
    { 768,  835,  1152 },
    { 768,  836,  1152 },
    { 896,  975,  1344 },
    { 896,  976,  1344 },
    { 1024, 1114, 1536 },
    { 1024, 1115, 1536 },
    { 1152, 1253, 1728 },
    { 1152, 1254, 1728 },
    { 1280, 1393, 1920 },
    { 1280, 1394, 1920 },
};

/**
 * Map audio coding mode (acmod) to number of full-bandwidth channels.
 * from ATSC A/52 Table 5.8 Audio Coding Mode
 */
const uint8_t ff_ac3_channels_tab[8] = {
    2, 1, 2, 3, 3, 4, 4, 5
};

/**
 * Table to remap channels from AC-3 order to SMPTE order.
 * [channel_mode][lfe][ch]
 */
const uint8_t ff_ac3_dec_channel_map[8][2][6] = {
    COMMON_CHANNEL_MAP
    { { 0, 1, 2, 3,    }, { 0, 1, 4, 2, 3,   } },
    { { 0, 2, 1, 3, 4, }, { 0, 2, 1, 5, 3, 4 } },
};

/* possible frequencies */
const int ff_ac3_sample_rate_tab[] = { 48000, 44100, 32000, 0 };

/* possible bitrates */
const uint16_t ff_ac3_bitrate_tab[19] = {
    32, 40, 48, 56, 64, 80, 96, 112, 128,
    160, 192, 224, 256, 320, 384, 448, 512, 576, 640
};

/**
 * Table of bin locations for rematrixing bands
 * reference: Section 7.5.2 Rematrixing : Frequency Band Definitions
 */
const uint8_t ff_ac3_rematrix_band_tab[5] = { 13, 25, 37, 61, 253 };

/**
 * Table E2.16 Default Coupling Banding Structure
 */
const uint8_t ff_eac3_default_cpl_band_struct[18] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1
};

const uint8_t ff_ac3_bap_tab[64]= {
    0, 1, 1, 1, 1, 1, 2, 2, 3, 3,
    3, 4, 4, 5, 5, 6, 6, 6, 6, 7,
    7, 7, 7, 8, 8, 8, 8, 9, 9, 9,
    9, 10, 10, 10, 10, 11, 11, 11, 11, 12,
    12, 12, 12, 13, 13, 13, 13, 14, 14, 14,
    14, 14, 14, 14, 14, 15, 15, 15, 15, 15,
    15, 15, 15, 15,
};

const uint8_t ff_ac3_slow_decay_tab[4]={
    0x0f, 0x11, 0x13, 0x15,
};

const uint8_t ff_ac3_fast_decay_tab[4]={
    0x3f, 0x53, 0x67, 0x7b,
};

const uint16_t ff_ac3_slow_gain_tab[4]= {
    0x540, 0x4d8, 0x478, 0x410,
};

const uint16_t ff_ac3_db_per_bit_tab[4]= {
    0x000, 0x700, 0x900, 0xb00,
};

const int16_t ff_ac3_floor_tab[8]= {
    0x2f0, 0x2b0, 0x270, 0x230, 0x1f0, 0x170, 0x0f0, 0xf800,
};

const uint16_t ff_ac3_fast_gain_tab[8]= {
    0x080, 0x100, 0x180, 0x200, 0x280, 0x300, 0x380, 0x400,
};

/** Adjustments in dB gain */
const float ff_ac3_gain_levels[9] = {
    LEVEL_PLUS_3DB,
    LEVEL_PLUS_1POINT5DB,
    LEVEL_ONE,
    LEVEL_MINUS_1POINT5DB,
    LEVEL_MINUS_3DB,
    LEVEL_MINUS_4POINT5DB,
    LEVEL_MINUS_6DB,
    LEVEL_ZERO,
    LEVEL_MINUS_9DB
};

const uint64_t ff_eac3_custom_channel_map_locations[16][2] = {
    { 1, AV_CH_FRONT_LEFT },
    { 1, AV_CH_FRONT_CENTER },
    { 1, AV_CH_FRONT_RIGHT },
    { 1, AV_CH_SIDE_LEFT },
    { 1, AV_CH_SIDE_RIGHT },
    { 0, AV_CH_FRONT_LEFT_OF_CENTER | AV_CH_FRONT_RIGHT_OF_CENTER },
    { 0, AV_CH_BACK_LEFT | AV_CH_BACK_RIGHT },
    { 0, AV_CH_BACK_CENTER },
    { 0, AV_CH_TOP_CENTER },
    { 0, AV_CH_SURROUND_DIRECT_LEFT | AV_CH_SURROUND_DIRECT_RIGHT },
    { 0, AV_CH_WIDE_LEFT | AV_CH_WIDE_RIGHT },
    { 0, AV_CH_TOP_FRONT_LEFT | AV_CH_TOP_FRONT_RIGHT},
    { 0, AV_CH_TOP_FRONT_CENTER },
    { 0, AV_CH_TOP_BACK_LEFT | AV_CH_TOP_BACK_RIGHT },
    { 0, AV_CH_LOW_FREQUENCY_2 },
    { 1, AV_CH_LOW_FREQUENCY },
};
