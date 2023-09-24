/*
 * Common code and tables of the AAC fixed- and floating-point decoders
 * Copyright (c) 2005-2006 Oded Shimon ( ods15 ods15 dyndns org )
 * Copyright (c) 2006-2007 Maxim Gavrilov ( maxim.gavrilov gmail com )
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
 * Common code and tables of the AAC fixed- and floating-point decoders
 */

#include "aac.h"
#include "aacdectab.h"
#include "aactab.h"
#include "vlc.h"

#include "libavutil/attributes.h"
#include "libavutil/thread.h"

const int8_t ff_tags_per_config[16] = { 0, 1, 1, 2, 3, 3, 4, 5, 0, 0, 0, 5, 5, 16, 5, 0 };

const uint8_t ff_aac_channel_layout_map[16][16][3] = {
    { { TYPE_SCE, 0, AAC_CHANNEL_FRONT }, },
    { { TYPE_CPE, 0, AAC_CHANNEL_FRONT }, },
    { { TYPE_SCE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 0, AAC_CHANNEL_FRONT }, },
    { { TYPE_SCE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 0, AAC_CHANNEL_FRONT }, { TYPE_SCE, 1, AAC_CHANNEL_BACK }, },
    { { TYPE_SCE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 1, AAC_CHANNEL_BACK }, },
    { { TYPE_SCE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 1, AAC_CHANNEL_BACK }, { TYPE_LFE, 0, AAC_CHANNEL_LFE  }, },
    { { TYPE_SCE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 1, AAC_CHANNEL_FRONT }, { TYPE_CPE, 2, AAC_CHANNEL_BACK }, { TYPE_LFE, 0, AAC_CHANNEL_LFE  }, },
    { { 0, } },
    { { 0, } },
    { { 0, } },
    { { TYPE_SCE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 1, AAC_CHANNEL_BACK }, { TYPE_SCE, 1, AAC_CHANNEL_BACK }, { TYPE_LFE, 0, AAC_CHANNEL_LFE  }, },
    { { TYPE_SCE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 1, AAC_CHANNEL_BACK }, { TYPE_CPE, 2, AAC_CHANNEL_BACK }, { TYPE_LFE, 0, AAC_CHANNEL_LFE  }, },
    {
      { TYPE_SCE, 0, AAC_CHANNEL_FRONT }, // SCE1 = FC,
      { TYPE_CPE, 0, AAC_CHANNEL_FRONT }, // CPE1 = FLc and FRc,
      { TYPE_CPE, 1, AAC_CHANNEL_FRONT }, // CPE2 = FL and FR,
      { TYPE_CPE, 2, AAC_CHANNEL_BACK  }, // CPE3 = SiL and SiR,
      { TYPE_CPE, 3, AAC_CHANNEL_BACK  }, // CPE4 = BL and BR,
      { TYPE_SCE, 1, AAC_CHANNEL_BACK  }, // SCE2 = BC,
      { TYPE_LFE, 0, AAC_CHANNEL_LFE   }, // LFE1 = LFE1,
      { TYPE_LFE, 1, AAC_CHANNEL_LFE   }, // LFE2 = LFE2,
      { TYPE_SCE, 2, AAC_CHANNEL_FRONT }, // SCE3 = TpFC,
      { TYPE_CPE, 4, AAC_CHANNEL_FRONT }, // CPE5 = TpFL and TpFR,
      { TYPE_CPE, 5, AAC_CHANNEL_SIDE  }, // CPE6 = TpSiL and TpSiR,
      { TYPE_SCE, 3, AAC_CHANNEL_SIDE  }, // SCE4 = TpC,
      { TYPE_CPE, 6, AAC_CHANNEL_BACK  }, // CPE7 = TpBL and TpBR,
      { TYPE_SCE, 4, AAC_CHANNEL_BACK  }, // SCE5 = TpBC,
      { TYPE_SCE, 5, AAC_CHANNEL_FRONT }, // SCE6 = BtFC,
      { TYPE_CPE, 7, AAC_CHANNEL_FRONT }, // CPE8 = BtFL and BtFR
    },
    { { TYPE_SCE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 1, AAC_CHANNEL_BACK }, { TYPE_LFE, 0, AAC_CHANNEL_LFE }, { TYPE_CPE, 2, AAC_CHANNEL_FRONT  }, },
    { { 0, } },
};

const int16_t ff_aac_channel_map[3][4][6] = {
    {
      { AV_CHAN_FRONT_CENTER,        AV_CHAN_FRONT_LEFT_OF_CENTER, AV_CHAN_FRONT_RIGHT_OF_CENTER, AV_CHAN_FRONT_LEFT,        AV_CHAN_FRONT_RIGHT,        AV_CHAN_NONE },
      { AV_CHAN_UNUSED,              AV_CHAN_NONE,                 AV_CHAN_NONE,                  AV_CHAN_NONE,              AV_CHAN_NONE,               AV_CHAN_NONE },
      { AV_CHAN_UNUSED,              AV_CHAN_SIDE_LEFT,            AV_CHAN_SIDE_RIGHT,            AV_CHAN_BACK_LEFT,         AV_CHAN_BACK_RIGHT,         AV_CHAN_BACK_CENTER },
      { AV_CHAN_LOW_FREQUENCY,       AV_CHAN_LOW_FREQUENCY_2,      AV_CHAN_NONE,                  AV_CHAN_NONE,              AV_CHAN_NONE,               AV_CHAN_NONE },
    },
    {
      { AV_CHAN_TOP_FRONT_CENTER,    AV_CHAN_NONE,                 AV_CHAN_NONE,                  AV_CHAN_TOP_FRONT_LEFT,    AV_CHAN_TOP_FRONT_RIGHT,    AV_CHAN_NONE },
      { AV_CHAN_UNUSED,              AV_CHAN_TOP_SIDE_LEFT,        AV_CHAN_TOP_SIDE_RIGHT,        AV_CHAN_NONE,              AV_CHAN_NONE,               AV_CHAN_TOP_CENTER},
      { AV_CHAN_UNUSED,              AV_CHAN_NONE,                 AV_CHAN_NONE,                  AV_CHAN_TOP_BACK_LEFT,     AV_CHAN_TOP_BACK_RIGHT,     AV_CHAN_TOP_BACK_CENTER},
      { AV_CHAN_NONE,                AV_CHAN_NONE,                 AV_CHAN_NONE,                  AV_CHAN_NONE,              AV_CHAN_NONE,               AV_CHAN_NONE},
    },
    {
      { AV_CHAN_BOTTOM_FRONT_CENTER, AV_CHAN_NONE,                 AV_CHAN_NONE,                  AV_CHAN_BOTTOM_FRONT_LEFT, AV_CHAN_BOTTOM_FRONT_RIGHT, AV_CHAN_NONE },
      { AV_CHAN_NONE,                AV_CHAN_NONE,                 AV_CHAN_NONE,                  AV_CHAN_NONE,              AV_CHAN_NONE,               AV_CHAN_NONE },
      { AV_CHAN_NONE,                AV_CHAN_NONE,                 AV_CHAN_NONE,                  AV_CHAN_NONE,              AV_CHAN_NONE,               AV_CHAN_NONE },
      { AV_CHAN_NONE,                AV_CHAN_NONE,                 AV_CHAN_NONE,                  AV_CHAN_NONE,              AV_CHAN_NONE,               AV_CHAN_NONE },
    },
};

#if FF_API_OLD_CHANNEL_LAYOUT
const uint64_t ff_aac_channel_layout[] = {
    AV_CH_LAYOUT_MONO,
    AV_CH_LAYOUT_STEREO,
    AV_CH_LAYOUT_SURROUND,
    AV_CH_LAYOUT_4POINT0,
    AV_CH_LAYOUT_5POINT0_BACK,
    AV_CH_LAYOUT_5POINT1_BACK,
    AV_CH_LAYOUT_7POINT1_WIDE_BACK,
    AV_CH_LAYOUT_6POINT1_BACK,
    AV_CH_LAYOUT_7POINT1,
    AV_CH_LAYOUT_22POINT2,
    AV_CH_LAYOUT_7POINT1_TOP_BACK,
    0,
};
#endif

const AVChannelLayout ff_aac_ch_layout[] = {
    AV_CHANNEL_LAYOUT_MONO,
    AV_CHANNEL_LAYOUT_STEREO,
    AV_CHANNEL_LAYOUT_SURROUND,
    AV_CHANNEL_LAYOUT_4POINT0,
    AV_CHANNEL_LAYOUT_5POINT0_BACK,
    AV_CHANNEL_LAYOUT_5POINT1_BACK,
    AV_CHANNEL_LAYOUT_7POINT1_WIDE_BACK,
    AV_CHANNEL_LAYOUT_6POINT1_BACK,
    AV_CHANNEL_LAYOUT_7POINT1,
    AV_CHANNEL_LAYOUT_22POINT2,
    AV_CHANNEL_LAYOUT_7POINT1_TOP_BACK,
    { 0 },
};

VLCElem ff_vlc_scalefactors[352];
const VLCElem *ff_vlc_spectral[11];

/// Huffman tables for SBR

static const uint8_t t_huffman_env_1_5dB_bits[121] = {
    18, 18, 18, 18, 18, 18, 19, 19,
    19, 19, 19, 19, 19, 19, 19, 19,
    19, 19, 19, 19, 19, 19, 19, 19,
    19, 19, 19, 19, 19, 19, 19, 19,
    19, 19, 17, 18, 16, 17, 18, 17,
    16, 16, 16, 16, 15, 14, 14, 13,
    13, 12, 11, 10,  9,  8,  7,  6,
     5,  4,  3,  2,  2,  3,  4,  5,
     6,  7,  8,  9, 10, 12, 13, 14,
    14, 15, 16, 17, 16, 19, 19, 19,
    19, 19, 19, 19, 19, 19, 19, 19,
    19, 19, 19, 19, 19, 19, 19, 19,
    19, 19, 19, 19, 19, 19, 19, 19,
    19, 19, 19, 19, 19, 19, 19, 19,
    19, 19, 19, 19, 19, 19, 19, 19,
    19,
};

static const uint32_t t_huffman_env_1_5dB_codes[121] = {
    0x3ffd6, 0x3ffd7, 0x3ffd8, 0x3ffd9, 0x3ffda, 0x3ffdb, 0x7ffb8, 0x7ffb9,
    0x7ffba, 0x7ffbb, 0x7ffbc, 0x7ffbd, 0x7ffbe, 0x7ffbf, 0x7ffc0, 0x7ffc1,
    0x7ffc2, 0x7ffc3, 0x7ffc4, 0x7ffc5, 0x7ffc6, 0x7ffc7, 0x7ffc8, 0x7ffc9,
    0x7ffca, 0x7ffcb, 0x7ffcc, 0x7ffcd, 0x7ffce, 0x7ffcf, 0x7ffd0, 0x7ffd1,
    0x7ffd2, 0x7ffd3, 0x1ffe6, 0x3ffd4, 0x0fff0, 0x1ffe9, 0x3ffd5, 0x1ffe7,
    0x0fff1, 0x0ffec, 0x0ffed, 0x0ffee, 0x07ff4, 0x03ff9, 0x03ff7, 0x01ffa,
    0x01ff9, 0x00ffb, 0x007fc, 0x003fc, 0x001fd, 0x000fd, 0x0007d, 0x0003d,
    0x0001d, 0x0000d, 0x00005, 0x00001, 0x00000, 0x00004, 0x0000c, 0x0001c,
    0x0003c, 0x0007c, 0x000fc, 0x001fc, 0x003fd, 0x00ffa, 0x01ff8, 0x03ff6,
    0x03ff8, 0x07ff5, 0x0ffef, 0x1ffe8, 0x0fff2, 0x7ffd4, 0x7ffd5, 0x7ffd6,
    0x7ffd7, 0x7ffd8, 0x7ffd9, 0x7ffda, 0x7ffdb, 0x7ffdc, 0x7ffdd, 0x7ffde,
    0x7ffdf, 0x7ffe0, 0x7ffe1, 0x7ffe2, 0x7ffe3, 0x7ffe4, 0x7ffe5, 0x7ffe6,
    0x7ffe7, 0x7ffe8, 0x7ffe9, 0x7ffea, 0x7ffeb, 0x7ffec, 0x7ffed, 0x7ffee,
    0x7ffef, 0x7fff0, 0x7fff1, 0x7fff2, 0x7fff3, 0x7fff4, 0x7fff5, 0x7fff6,
    0x7fff7, 0x7fff8, 0x7fff9, 0x7fffa, 0x7fffb, 0x7fffc, 0x7fffd, 0x7fffe,
    0x7ffff,
};

static const uint8_t f_huffman_env_1_5dB_bits[121] = {
    19, 19, 20, 20, 20, 20, 20, 20,
    20, 19, 20, 20, 20, 20, 19, 20,
    19, 19, 20, 18, 20, 20, 20, 19,
    20, 20, 20, 19, 20, 19, 18, 19,
    18, 18, 17, 18, 17, 17, 17, 16,
    16, 16, 15, 15, 14, 13, 13, 12,
    12, 11, 10,  9,  9,  8,  7,  6,
     5,  4,  3,  2,  2,  3,  4,  5,
     6,  8,  8,  9, 10, 11, 11, 11,
    12, 12, 13, 13, 14, 14, 16, 16,
    17, 17, 18, 18, 18, 18, 18, 18,
    18, 20, 19, 20, 20, 20, 20, 20,
    20, 19, 20, 20, 20, 20, 19, 20,
    18, 20, 20, 19, 19, 20, 20, 20,
    20, 20, 20, 20, 20, 20, 20, 20,
    20,
};

static const uint32_t f_huffman_env_1_5dB_codes[121] = {
    0x7ffe7, 0x7ffe8, 0xfffd2, 0xfffd3, 0xfffd4, 0xfffd5, 0xfffd6, 0xfffd7,
    0xfffd8, 0x7ffda, 0xfffd9, 0xfffda, 0xfffdb, 0xfffdc, 0x7ffdb, 0xfffdd,
    0x7ffdc, 0x7ffdd, 0xfffde, 0x3ffe4, 0xfffdf, 0xfffe0, 0xfffe1, 0x7ffde,
    0xfffe2, 0xfffe3, 0xfffe4, 0x7ffdf, 0xfffe5, 0x7ffe0, 0x3ffe8, 0x7ffe1,
    0x3ffe0, 0x3ffe9, 0x1ffef, 0x3ffe5, 0x1ffec, 0x1ffed, 0x1ffee, 0x0fff4,
    0x0fff3, 0x0fff0, 0x07ff7, 0x07ff6, 0x03ffa, 0x01ffa, 0x01ff9, 0x00ffa,
    0x00ff8, 0x007f9, 0x003fb, 0x001fc, 0x001fa, 0x000fb, 0x0007c, 0x0003c,
    0x0001c, 0x0000c, 0x00005, 0x00001, 0x00000, 0x00004, 0x0000d, 0x0001d,
    0x0003d, 0x000fa, 0x000fc, 0x001fb, 0x003fa, 0x007f8, 0x007fa, 0x007fb,
    0x00ff9, 0x00ffb, 0x01ff8, 0x01ffb, 0x03ff8, 0x03ff9, 0x0fff1, 0x0fff2,
    0x1ffea, 0x1ffeb, 0x3ffe1, 0x3ffe2, 0x3ffea, 0x3ffe3, 0x3ffe6, 0x3ffe7,
    0x3ffeb, 0xfffe6, 0x7ffe2, 0xfffe7, 0xfffe8, 0xfffe9, 0xfffea, 0xfffeb,
    0xfffec, 0x7ffe3, 0xfffed, 0xfffee, 0xfffef, 0xffff0, 0x7ffe4, 0xffff1,
    0x3ffec, 0xffff2, 0xffff3, 0x7ffe5, 0x7ffe6, 0xffff4, 0xffff5, 0xffff6,
    0xffff7, 0xffff8, 0xffff9, 0xffffa, 0xffffb, 0xffffc, 0xffffd, 0xffffe,
    0xfffff,
};

static const uint8_t t_huffman_env_bal_1_5dB_bits[49] = {
    16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 12, 11,  9,  7,  5,  3,
     1,  2,  4,  6,  8, 11, 12, 15,
    16, 16, 16, 16, 16, 16, 16, 17,
    17, 17, 17, 17, 17, 17, 17, 17,
    17,
};

static const uint32_t t_huffman_env_bal_1_5dB_codes[49] = {
    0x0ffe4, 0x0ffe5, 0x0ffe6, 0x0ffe7, 0x0ffe8, 0x0ffe9, 0x0ffea, 0x0ffeb,
    0x0ffec, 0x0ffed, 0x0ffee, 0x0ffef, 0x0fff0, 0x0fff1, 0x0fff2, 0x0fff3,
    0x0fff4, 0x0ffe2, 0x00ffc, 0x007fc, 0x001fe, 0x0007e, 0x0001e, 0x00006,
    0x00000, 0x00002, 0x0000e, 0x0003e, 0x000fe, 0x007fd, 0x00ffd, 0x07ff0,
    0x0ffe3, 0x0fff5, 0x0fff6, 0x0fff7, 0x0fff8, 0x0fff9, 0x0fffa, 0x1fff6,
    0x1fff7, 0x1fff8, 0x1fff9, 0x1fffa, 0x1fffb, 0x1fffc, 0x1fffd, 0x1fffe,
    0x1ffff,
};

static const uint8_t f_huffman_env_bal_1_5dB_bits[49] = {
    18, 18, 18, 18, 18, 18, 18, 18,
    18, 18, 18, 18, 18, 18, 18, 16,
    17, 14, 11, 11,  8,  7,  4,  2,
     1,  3,  5,  6,  9, 11, 12, 15,
    16, 18, 18, 18, 18, 18, 18, 18,
    18, 18, 18, 18, 18, 18, 18, 19,
    19,
};

static const uint32_t f_huffman_env_bal_1_5dB_codes[49] = {
    0x3ffe2, 0x3ffe3, 0x3ffe4, 0x3ffe5, 0x3ffe6, 0x3ffe7, 0x3ffe8, 0x3ffe9,
    0x3ffea, 0x3ffeb, 0x3ffec, 0x3ffed, 0x3ffee, 0x3ffef, 0x3fff0, 0x0fff7,
    0x1fff0, 0x03ffc, 0x007fe, 0x007fc, 0x000fe, 0x0007e, 0x0000e, 0x00002,
    0x00000, 0x00006, 0x0001e, 0x0003e, 0x001fe, 0x007fd, 0x00ffe, 0x07ffa,
    0x0fff6, 0x3fff1, 0x3fff2, 0x3fff3, 0x3fff4, 0x3fff5, 0x3fff6, 0x3fff7,
    0x3fff8, 0x3fff9, 0x3fffa, 0x3fffb, 0x3fffc, 0x3fffd, 0x3fffe, 0x7fffe,
    0x7ffff,
};

static const uint8_t t_huffman_env_3_0dB_bits[63] = {
    18, 18, 19, 19, 19, 19, 19, 19,
    19, 19, 19, 19, 19, 19, 19, 19,
    19, 17, 16, 16, 16, 14, 14, 14,
    13, 12, 11,  8,  6,  4,  2,  1,
     3,  5,  7,  9, 11, 13, 14, 14,
    15, 16, 17, 18, 19, 19, 19, 19,
    19, 19, 19, 19, 19, 19, 19, 19,
    19, 19, 19, 19, 19, 19, 19,
};

static const uint32_t t_huffman_env_3_0dB_codes[63] = {
    0x3ffed, 0x3ffee, 0x7ffde, 0x7ffdf, 0x7ffe0, 0x7ffe1, 0x7ffe2, 0x7ffe3,
    0x7ffe4, 0x7ffe5, 0x7ffe6, 0x7ffe7, 0x7ffe8, 0x7ffe9, 0x7ffea, 0x7ffeb,
    0x7ffec, 0x1fff4, 0x0fff7, 0x0fff9, 0x0fff8, 0x03ffb, 0x03ffa, 0x03ff8,
    0x01ffa, 0x00ffc, 0x007fc, 0x000fe, 0x0003e, 0x0000e, 0x00002, 0x00000,
    0x00006, 0x0001e, 0x0007e, 0x001fe, 0x007fd, 0x01ffb, 0x03ff9, 0x03ffc,
    0x07ffa, 0x0fff6, 0x1fff5, 0x3ffec, 0x7ffed, 0x7ffee, 0x7ffef, 0x7fff0,
    0x7fff1, 0x7fff2, 0x7fff3, 0x7fff4, 0x7fff5, 0x7fff6, 0x7fff7, 0x7fff8,
    0x7fff9, 0x7fffa, 0x7fffb, 0x7fffc, 0x7fffd, 0x7fffe, 0x7ffff,
};

static const uint8_t f_huffman_env_3_0dB_bits[63] = {
    20, 20, 20, 20, 20, 20, 20, 18,
    19, 19, 19, 19, 18, 18, 20, 19,
    17, 18, 17, 16, 16, 15, 14, 12,
    11, 10,  9,  8,  6,  4,  2,  1,
     3,  5,  8,  9, 10, 11, 12, 13,
    14, 15, 15, 16, 16, 17, 17, 18,
    18, 18, 20, 19, 19, 19, 20, 19,
    19, 20, 20, 20, 20, 20, 20,
};

static const uint32_t f_huffman_env_3_0dB_codes[63] = {
    0xffff0, 0xffff1, 0xffff2, 0xffff3, 0xffff4, 0xffff5, 0xffff6, 0x3fff3,
    0x7fff5, 0x7ffee, 0x7ffef, 0x7fff6, 0x3fff4, 0x3fff2, 0xffff7, 0x7fff0,
    0x1fff5, 0x3fff0, 0x1fff4, 0x0fff7, 0x0fff6, 0x07ff8, 0x03ffb, 0x00ffd,
    0x007fd, 0x003fd, 0x001fd, 0x000fd, 0x0003e, 0x0000e, 0x00002, 0x00000,
    0x00006, 0x0001e, 0x000fc, 0x001fc, 0x003fc, 0x007fc, 0x00ffc, 0x01ffc,
    0x03ffa, 0x07ff9, 0x07ffa, 0x0fff8, 0x0fff9, 0x1fff6, 0x1fff7, 0x3fff5,
    0x3fff6, 0x3fff1, 0xffff8, 0x7fff1, 0x7fff2, 0x7fff3, 0xffff9, 0x7fff7,
    0x7fff4, 0xffffa, 0xffffb, 0xffffc, 0xffffd, 0xffffe, 0xfffff,
};

static const uint8_t t_huffman_env_bal_3_0dB_bits[25] = {
    13, 13, 13, 13, 13, 13, 13, 12,
     8,  7,  4,  3,  1,  2,  5,  6,
     9, 13, 13, 13, 13, 13, 13, 14,
    14,
};

static const uint16_t t_huffman_env_bal_3_0dB_codes[25] = {
    0x1ff2, 0x1ff3, 0x1ff4, 0x1ff5, 0x1ff6, 0x1ff7, 0x1ff8, 0x0ff8,
    0x00fe, 0x007e, 0x000e, 0x0006, 0x0000, 0x0002, 0x001e, 0x003e,
    0x01fe, 0x1ff9, 0x1ffa, 0x1ffb, 0x1ffc, 0x1ffd, 0x1ffe, 0x3ffe,
    0x3fff,
};

static const uint8_t f_huffman_env_bal_3_0dB_bits[25] = {
    13, 13, 13, 13, 13, 14, 14, 11,
     8,  7,  4,  2,  1,  3,  5,  6,
     9, 12, 13, 14, 14, 14, 14, 14,
    14,
};

static const uint16_t f_huffman_env_bal_3_0dB_codes[25] = {
    0x1ff7, 0x1ff8, 0x1ff9, 0x1ffa, 0x1ffb, 0x3ff8, 0x3ff9, 0x07fc,
    0x00fe, 0x007e, 0x000e, 0x0002, 0x0000, 0x0006, 0x001e, 0x003e,
    0x01fe, 0x0ffa, 0x1ff6, 0x3ffa, 0x3ffb, 0x3ffc, 0x3ffd, 0x3ffe,
    0x3fff,
};

static const uint8_t t_huffman_noise_3_0dB_bits[63] = {
    13, 13, 13, 13, 13, 13, 13, 13,
    13, 13, 13, 13, 13, 13, 13, 13,
    13, 13, 13, 13, 13, 13, 13, 13,
    13, 13, 11,  8,  6,  4,  3,  1,
     2,  5,  8, 10, 13, 13, 13, 13,
    13, 13, 13, 13, 13, 13, 13, 13,
    13, 13, 13, 13, 13, 13, 13, 13,
    13, 13, 13, 13, 13, 14, 14,
};

static const uint16_t t_huffman_noise_3_0dB_codes[63] = {
    0x1fce, 0x1fcf, 0x1fd0, 0x1fd1, 0x1fd2, 0x1fd3, 0x1fd4, 0x1fd5,
    0x1fd6, 0x1fd7, 0x1fd8, 0x1fd9, 0x1fda, 0x1fdb, 0x1fdc, 0x1fdd,
    0x1fde, 0x1fdf, 0x1fe0, 0x1fe1, 0x1fe2, 0x1fe3, 0x1fe4, 0x1fe5,
    0x1fe6, 0x1fe7, 0x07f2, 0x00fd, 0x003e, 0x000e, 0x0006, 0x0000,
    0x0002, 0x001e, 0x00fc, 0x03f8, 0x1fcc, 0x1fe8, 0x1fe9, 0x1fea,
    0x1feb, 0x1fec, 0x1fcd, 0x1fed, 0x1fee, 0x1fef, 0x1ff0, 0x1ff1,
    0x1ff2, 0x1ff3, 0x1ff4, 0x1ff5, 0x1ff6, 0x1ff7, 0x1ff8, 0x1ff9,
    0x1ffa, 0x1ffb, 0x1ffc, 0x1ffd, 0x1ffe, 0x3ffe, 0x3fff,
};

static const uint8_t t_huffman_noise_bal_3_0dB_bits[25] = {
    8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 5, 2, 1, 3, 6, 8,
    8, 8, 8, 8, 8, 8, 8, 8,
    8,
};

static const uint8_t t_huffman_noise_bal_3_0dB_codes[25] = {
    0xec, 0xed, 0xee, 0xef, 0xf0, 0xf1, 0xf2, 0xf3,
    0xf4, 0xf5, 0x1c, 0x02, 0x00, 0x06, 0x3a, 0xf6,
    0xf7, 0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe,
    0xff,
};

VLC ff_aac_sbr_vlc[10];

static av_cold void aacdec_common_init(void)
{
#define SBR_INIT_VLC_STATIC(num, size) \
    VLC_INIT_STATIC(&ff_aac_sbr_vlc[num], 9, sbr_tmp[num].table_size / sbr_tmp[num].elem_size,     \
                    sbr_tmp[num].sbr_bits ,                      1,                      1, \
                    sbr_tmp[num].sbr_codes, sbr_tmp[num].elem_size, sbr_tmp[num].elem_size, \
                    size)
#define SBR_VLC_ROW(name) \
    { name ## _codes, name ## _bits, sizeof(name ## _codes), sizeof(name ## _codes[0]) }
    static const struct {
        const void *sbr_codes, *sbr_bits;
        const unsigned int table_size, elem_size;
    } sbr_tmp[] = {
        SBR_VLC_ROW(t_huffman_env_1_5dB),
        SBR_VLC_ROW(f_huffman_env_1_5dB),
        SBR_VLC_ROW(t_huffman_env_bal_1_5dB),
        SBR_VLC_ROW(f_huffman_env_bal_1_5dB),
        SBR_VLC_ROW(t_huffman_env_3_0dB),
        SBR_VLC_ROW(f_huffman_env_3_0dB),
        SBR_VLC_ROW(t_huffman_env_bal_3_0dB),
        SBR_VLC_ROW(f_huffman_env_bal_3_0dB),
        SBR_VLC_ROW(t_huffman_noise_3_0dB),
        SBR_VLC_ROW(t_huffman_noise_bal_3_0dB),
    };

    static VLCElem vlc_buf[304 + 270 + 550 + 300 + 328 +
                           294 + 306 + 268 + 510 + 366 + 462];
    VLCInitState state = VLC_INIT_STATE(vlc_buf);

    for (unsigned i = 0; i < 11; i++) {
#define TAB_WRAP_SIZE(name) name[i], sizeof(name[i][0]), sizeof(name[i][0])
        ff_vlc_spectral[i] =
            ff_vlc_init_tables_sparse(&state, 8, ff_aac_spectral_sizes[i],
                                      TAB_WRAP_SIZE(ff_aac_spectral_bits),
                                      TAB_WRAP_SIZE(ff_aac_spectral_codes),
                                      TAB_WRAP_SIZE(ff_aac_codebook_vector_idx),
                                      0);
    }

    VLC_INIT_STATIC_TABLE(ff_vlc_scalefactors, 7,
                          FF_ARRAY_ELEMS(ff_aac_scalefactor_code),
                          ff_aac_scalefactor_bits,
                          sizeof(ff_aac_scalefactor_bits[0]),
                          sizeof(ff_aac_scalefactor_bits[0]),
                          ff_aac_scalefactor_code,
                          sizeof(ff_aac_scalefactor_code[0]),
                          sizeof(ff_aac_scalefactor_code[0]), 0);

    // SBR VLC table initialization
    SBR_INIT_VLC_STATIC(0, 1098);
    SBR_INIT_VLC_STATIC(1, 1092);
    SBR_INIT_VLC_STATIC(2, 768);
    SBR_INIT_VLC_STATIC(3, 1026);
    SBR_INIT_VLC_STATIC(4, 1058);
    SBR_INIT_VLC_STATIC(5, 1052);
    SBR_INIT_VLC_STATIC(6, 544);
    SBR_INIT_VLC_STATIC(7, 544);
    SBR_INIT_VLC_STATIC(8, 592);
    SBR_INIT_VLC_STATIC(9, 512);
}

av_cold void ff_aacdec_common_init_once(void)
{
    static AVOnce init_static_once = AV_ONCE_INIT;
    ff_thread_once(&init_static_once, aacdec_common_init);
}
